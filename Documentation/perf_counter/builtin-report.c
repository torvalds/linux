#include "util/util.h"
#include "builtin.h"

#include "util/list.h"
#include "util/cache.h"
#include "util/rbtree.h"
#include "util/symbol.h"
#include "util/string.h"

#include "perf.h"

#include "util/parse-options.h"
#include "util/parse-events.h"

#define SHOW_KERNEL	1
#define SHOW_USER	2
#define SHOW_HV		4

static char		const *input_name = "perf.data";
static char		*vmlinux = NULL;
static char		*sort_order = "comm,dso";
static int		input;
static int		show_mask = SHOW_KERNEL | SHOW_USER | SHOW_HV;

static int		dump_trace = 0;
static int		verbose;
static int		full_paths;

static unsigned long	page_size;
static unsigned long	mmap_window = 32;

const char *perf_event_names[] = {
	[PERF_EVENT_MMAP]   = " PERF_EVENT_MMAP",
	[PERF_EVENT_MUNMAP] = " PERF_EVENT_MUNMAP",
	[PERF_EVENT_COMM]   = " PERF_EVENT_COMM",
};

struct ip_event {
	struct perf_event_header header;
	__u64 ip;
	__u32 pid, tid;
};
struct mmap_event {
	struct perf_event_header header;
	__u32 pid, tid;
	__u64 start;
	__u64 len;
	__u64 pgoff;
	char filename[PATH_MAX];
};
struct comm_event {
	struct perf_event_header header;
	__u32 pid,tid;
	char comm[16];
};

typedef union event_union {
	struct perf_event_header header;
	struct ip_event ip;
	struct mmap_event mmap;
	struct comm_event comm;
} event_t;

static LIST_HEAD(dsos);
static struct dso *kernel_dso;

static void dsos__add(struct dso *dso)
{
	list_add_tail(&dso->node, &dsos);
}

static struct dso *dsos__find(const char *name)
{
	struct dso *pos;

	list_for_each_entry(pos, &dsos, node)
		if (strcmp(pos->name, name) == 0)
			return pos;
	return NULL;
}

static struct dso *dsos__findnew(const char *name)
{
	struct dso *dso = dsos__find(name);
	int nr;

	if (dso == NULL) {
		dso = dso__new(name, 0);
		if (!dso)
			goto out_delete_dso;

		nr = dso__load(dso, NULL);
		if (nr < 0) {
			fprintf(stderr, "Failed to open: %s\n", name);
			goto out_delete_dso;
		}
		if (!nr) {
			fprintf(stderr,
		"Failed to find debug symbols for: %s, maybe install a debug package?\n",
					name);
		}

		dsos__add(dso);
	}

	return dso;

out_delete_dso:
	dso__delete(dso);
	return NULL;
}

static void dsos__fprintf(FILE *fp)
{
	struct dso *pos;

	list_for_each_entry(pos, &dsos, node)
		dso__fprintf(pos, fp);
}

static int load_kernel(void)
{
	int err;

	kernel_dso = dso__new("[kernel]", 0);
	if (!kernel_dso)
		return -1;

	err = dso__load_kernel(kernel_dso, vmlinux, NULL);
	if (err) {
		dso__delete(kernel_dso);
		kernel_dso = NULL;
	} else
		dsos__add(kernel_dso);

	return err;
}

static int strcommon(const char *pathname, const char *cwd, int cwdlen)
{
	int n = 0;

	while (pathname[n] == cwd[n] && n < cwdlen)
		++n;

	return n;
}

struct map {
	struct list_head node;
	uint64_t	 start;
	uint64_t	 end;
	uint64_t	 pgoff;
	struct dso	 *dso;
};

static struct map *map__new(struct mmap_event *event, char *cwd, int cwdlen)
{
	struct map *self = malloc(sizeof(*self));

	if (self != NULL) {
		const char *filename = event->filename;
		char newfilename[PATH_MAX];

		if (cwd) {
			int n = strcommon(filename, cwd, cwdlen);
			if (n == cwdlen) {
				snprintf(newfilename, sizeof(newfilename),
					 ".%s", filename + n);
				filename = newfilename;
			}
		}

		self->start = event->start;
		self->end   = event->start + event->len;
		self->pgoff = event->pgoff;

		self->dso = dsos__findnew(filename);
		if (self->dso == NULL)
			goto out_delete;
	}
	return self;
out_delete:
	free(self);
	return NULL;
}

struct thread;

struct thread {
	struct rb_node	 rb_node;
	struct list_head maps;
	pid_t		 pid;
	char		 *comm;
};

static struct thread *thread__new(pid_t pid)
{
	struct thread *self = malloc(sizeof(*self));

	if (self != NULL) {
		self->pid = pid;
		self->comm = NULL;
		INIT_LIST_HEAD(&self->maps);
	}

	return self;
}

static int thread__set_comm(struct thread *self, const char *comm)
{
	self->comm = strdup(comm);
	return self->comm ? 0 : -ENOMEM;
}

static struct rb_root threads;

static struct thread *threads__findnew(pid_t pid)
{
	struct rb_node **p = &threads.rb_node;
	struct rb_node *parent = NULL;
	struct thread *th;

	while (*p != NULL) {
		parent = *p;
		th = rb_entry(parent, struct thread, rb_node);

		if (th->pid == pid)
			return th;

		if (pid < th->pid)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	th = thread__new(pid);
	if (th != NULL) {
		rb_link_node(&th->rb_node, parent, p);
		rb_insert_color(&th->rb_node, &threads);
	}
	return th;
}

static void thread__insert_map(struct thread *self, struct map *map)
{
	list_add_tail(&map->node, &self->maps);
}

static struct map *thread__find_map(struct thread *self, uint64_t ip)
{
	struct map *pos;

	if (self == NULL)
		return NULL;

	list_for_each_entry(pos, &self->maps, node)
		if (ip >= pos->start && ip <= pos->end)
			return pos;

	return NULL;
}

/*
 * histogram, sorted on item, collects counts
 */

static struct rb_root hist;

struct hist_entry {
	struct rb_node	 rb_node;

	struct thread	 *thread;
	struct map	 *map;
	struct dso	 *dso;
	struct symbol	 *sym;
	uint64_t	 ip;
	char		 level;

	uint32_t	 count;
};

/*
 * configurable sorting bits
 */

struct sort_entry {
	struct list_head list;

	char *header;

	int64_t (*cmp)(struct hist_entry *, struct hist_entry *);
	size_t	(*print)(FILE *fp, struct hist_entry *);
};

static int64_t
sort__thread_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return right->thread->pid - left->thread->pid;
}

static size_t
sort__thread_print(FILE *fp, struct hist_entry *self)
{
	return fprintf(fp, " %16s:%5d", self->thread->comm ?: "", self->thread->pid);
}

static struct sort_entry sort_thread = {
	.header = "         Command: Pid ",
	.cmp	= sort__thread_cmp,
	.print	= sort__thread_print,
};

static int64_t
sort__comm_cmp(struct hist_entry *left, struct hist_entry *right)
{
	char *comm_l = left->thread->comm;
	char *comm_r = right->thread->comm;

	if (!comm_l || !comm_r) {
		if (!comm_l && !comm_r)
			return 0;
		else if (!comm_l)
			return -1;
		else
			return 1;
	}

	return strcmp(comm_l, comm_r);
}

static size_t
sort__comm_print(FILE *fp, struct hist_entry *self)
{
	return fprintf(fp, " %16s", self->thread->comm ?: "<unknown>");
}

static struct sort_entry sort_comm = {
	.header = "         Command",
	.cmp	= sort__comm_cmp,
	.print	= sort__comm_print,
};

static int64_t
sort__dso_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct dso *dso_l = left->dso;
	struct dso *dso_r = right->dso;

	if (!dso_l || !dso_r) {
		if (!dso_l && !dso_r)
			return 0;
		else if (!dso_l)
			return -1;
		else
			return 1;
	}

	return strcmp(dso_l->name, dso_r->name);
}

static size_t
sort__dso_print(FILE *fp, struct hist_entry *self)
{
	return fprintf(fp, " %64s", self->dso ? self->dso->name : "<unknown>");
}

static struct sort_entry sort_dso = {
	.header = "                                                    Shared Object",
	.cmp	= sort__dso_cmp,
	.print	= sort__dso_print,
};

static int64_t
sort__sym_cmp(struct hist_entry *left, struct hist_entry *right)
{
	uint64_t ip_l, ip_r;

	if (left->sym == right->sym)
		return 0;

	ip_l = left->sym ? left->sym->start : left->ip;
	ip_r = right->sym ? right->sym->start : right->ip;

	return (int64_t)(ip_r - ip_l);
}

static size_t
sort__sym_print(FILE *fp, struct hist_entry *self)
{
	size_t ret = 0;

	if (verbose)
		ret += fprintf(fp, " %#018llx", (unsigned long long)self->ip);

	ret += fprintf(fp, " %s: %s",
			self->dso ? self->dso->name : "<unknown>",
			self->sym ? self->sym->name : "<unknown>");

	return ret;
}

static struct sort_entry sort_sym = {
	.header = "Shared Object: Symbol",
	.cmp	= sort__sym_cmp,
	.print	= sort__sym_print,
};

struct sort_dimension {
	char *name;
	struct sort_entry *entry;
	int taken;
};

static struct sort_dimension sort_dimensions[] = {
	{ .name = "pid",	.entry = &sort_thread,	},
	{ .name = "comm",	.entry = &sort_comm,	},
	{ .name = "dso",	.entry = &sort_dso,	},
	{ .name = "symbol",	.entry = &sort_sym,	},
};

static LIST_HEAD(hist_entry__sort_list);

static int sort_dimension__add(char *tok)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sort_dimensions); i++) {
		struct sort_dimension *sd = &sort_dimensions[i];

		if (sd->taken)
			continue;

		if (strcmp(tok, sd->name))
			continue;

		list_add_tail(&sd->entry->list, &hist_entry__sort_list);
		sd->taken = 1;
		return 0;
	}

	return -ESRCH;
}

static void setup_sorting(void)
{
	char *tmp, *tok, *str = strdup(sort_order);

	for (tok = strtok_r(str, ", ", &tmp);
			tok; tok = strtok_r(NULL, ", ", &tmp))
		sort_dimension__add(tok);

	free(str);
}

static int64_t
hist_entry__cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct sort_entry *se;
	int64_t cmp = 0;

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		cmp = se->cmp(left, right);
		if (cmp)
			break;
	}

	return cmp;
}

static size_t
hist_entry__fprintf(FILE *fp, struct hist_entry *self, uint64_t total_samples)
{
	struct sort_entry *se;
	size_t ret;

	if (total_samples) {
		ret = fprintf(fp, "    %5.2f%%",
				(self->count * 100.0) / total_samples);
	} else
		ret = fprintf(fp, "%12d ", self->count);

	list_for_each_entry(se, &hist_entry__sort_list, list)
		ret += se->print(fp, self);

	ret += fprintf(fp, "\n");

	return ret;
}

/*
 * collect histogram counts
 */

static int
hist_entry__add(struct thread *thread, struct map *map, struct dso *dso,
		struct symbol *sym, uint64_t ip, char level)
{
	struct rb_node **p = &hist.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *he;
	struct hist_entry entry = {
		.thread	= thread,
		.map	= map,
		.dso	= dso,
		.sym	= sym,
		.ip	= ip,
		.level	= level,
		.count	= 1,
	};
	int cmp;

	while (*p != NULL) {
		parent = *p;
		he = rb_entry(parent, struct hist_entry, rb_node);

		cmp = hist_entry__cmp(&entry, he);

		if (!cmp) {
			he->count++;
			return 0;
		}

		if (cmp < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	he = malloc(sizeof(*he));
	if (!he)
		return -ENOMEM;
	*he = entry;
	rb_link_node(&he->rb_node, parent, p);
	rb_insert_color(&he->rb_node, &hist);

	return 0;
}

/*
 * reverse the map, sort on count.
 */

static struct rb_root output_hists;

static void output__insert_entry(struct hist_entry *he)
{
	struct rb_node **p = &output_hists.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *iter;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct hist_entry, rb_node);

		if (he->count > iter->count)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&he->rb_node, parent, p);
	rb_insert_color(&he->rb_node, &output_hists);
}

static void output__resort(void)
{
	struct rb_node *next = rb_first(&hist);
	struct hist_entry *n;

	while (next) {
		n = rb_entry(next, struct hist_entry, rb_node);
		next = rb_next(&n->rb_node);

		rb_erase(&n->rb_node, &hist);
		output__insert_entry(n);
	}
}

static size_t output__fprintf(FILE *fp, uint64_t total_samples)
{
	struct hist_entry *pos;
	struct sort_entry *se;
	struct rb_node *nd;
	size_t ret = 0;

	fprintf(fp, "#\n");

	fprintf(fp, "# Overhead");
	list_for_each_entry(se, &hist_entry__sort_list, list)
		fprintf(fp, " %s", se->header);
	fprintf(fp, "\n");

	fprintf(fp, "# ........");
	list_for_each_entry(se, &hist_entry__sort_list, list) {
		int i;

		fprintf(fp, " ");
		for (i = 0; i < strlen(se->header); i++)
			fprintf(fp, ".");
	}
	fprintf(fp, "\n");

	fprintf(fp, "#\n");

	for (nd = rb_first(&output_hists); nd; nd = rb_next(nd)) {
		pos = rb_entry(nd, struct hist_entry, rb_node);
		ret += hist_entry__fprintf(fp, pos, total_samples);
	}

	return ret;
}


static int __cmd_report(void)
{
	unsigned long offset = 0;
	unsigned long head = 0;
	struct stat stat;
	char *buf;
	event_t *event;
	int ret, rc = EXIT_FAILURE;
	uint32_t size;
	unsigned long total = 0, total_mmap = 0, total_comm = 0, total_unknown = 0;
	char cwd[PATH_MAX], *cwdp = cwd;
	int cwdlen;

	input = open(input_name, O_RDONLY);
	if (input < 0) {
		perror("failed to open file");
		exit(-1);
	}

	ret = fstat(input, &stat);
	if (ret < 0) {
		perror("failed to stat file");
		exit(-1);
	}

	if (!stat.st_size) {
		fprintf(stderr, "zero-sized file, nothing to do!\n");
		exit(0);
	}

	if (load_kernel() < 0) {
		perror("failed to load kernel symbols");
		return EXIT_FAILURE;
	}

	if (!full_paths) {
		if (getcwd(cwd, sizeof(cwd)) == NULL) {
			perror("failed to get the current directory");
			return EXIT_FAILURE;
		}
		cwdlen = strlen(cwd);
	} else {
		cwdp = NULL;
		cwdlen = 0;
	}
remap:
	buf = (char *)mmap(NULL, page_size * mmap_window, PROT_READ,
			   MAP_SHARED, input, offset);
	if (buf == MAP_FAILED) {
		perror("failed to mmap file");
		exit(-1);
	}

more:
	event = (event_t *)(buf + head);

	size = event->header.size;
	if (!size)
		size = 8;

	if (head + event->header.size >= page_size * mmap_window) {
		unsigned long shift = page_size * (head / page_size);
		int ret;

		ret = munmap(buf, page_size * mmap_window);
		assert(ret == 0);

		offset += shift;
		head -= shift;
		goto remap;
	}

	size = event->header.size;
	if (!size)
		goto broken_event;

	if (event->header.misc & PERF_EVENT_MISC_OVERFLOW) {
		char level;
		int show = 0;
		struct dso *dso = NULL;
		struct thread *thread = threads__findnew(event->ip.pid);
		uint64_t ip = event->ip.ip;
		struct map *map = NULL;

		if (dump_trace) {
			fprintf(stderr, "%p [%p]: PERF_EVENT (IP, %d): %d: %p\n",
				(void *)(offset + head),
				(void *)(long)(event->header.size),
				event->header.misc,
				event->ip.pid,
				(void *)(long)ip);
		}

		if (thread == NULL) {
			fprintf(stderr, "problem processing %d event, skipping it.\n",
				event->header.type);
			goto broken_event;
		}

		if (event->header.misc & PERF_EVENT_MISC_KERNEL) {
			show = SHOW_KERNEL;
			level = 'k';

			dso = kernel_dso;

		} else if (event->header.misc & PERF_EVENT_MISC_USER) {

			show = SHOW_USER;
			level = '.';

			map = thread__find_map(thread, ip);
			if (map != NULL) {
				dso = map->dso;
				ip -= map->start + map->pgoff;
			}

		} else {
			show = SHOW_HV;
			level = 'H';
		}

		if (show & show_mask) {
			struct symbol *sym = dso__find_symbol(dso, ip);

			if (hist_entry__add(thread, map, dso, sym, ip, level)) {
				fprintf(stderr,
		"problem incrementing symbol count, skipping event\n");
				goto broken_event;
			}
		}
		total++;
	} else switch (event->header.type) {
	case PERF_EVENT_MMAP: {
		struct thread *thread = threads__findnew(event->mmap.pid);
		struct map *map = map__new(&event->mmap, cwdp, cwdlen);

		if (dump_trace) {
			fprintf(stderr, "%p [%p]: PERF_EVENT_MMAP: [%p(%p) @ %p]: %s\n",
				(void *)(offset + head),
				(void *)(long)(event->header.size),
				(void *)(long)event->mmap.start,
				(void *)(long)event->mmap.len,
				(void *)(long)event->mmap.pgoff,
				event->mmap.filename);
		}
		if (thread == NULL || map == NULL) {
			fprintf(stderr, "problem processing PERF_EVENT_MMAP, skipping event.\n");
			goto broken_event;
		}
		thread__insert_map(thread, map);
		total_mmap++;
		break;
	}
	case PERF_EVENT_COMM: {
		struct thread *thread = threads__findnew(event->comm.pid);

		if (dump_trace) {
			fprintf(stderr, "%p [%p]: PERF_EVENT_COMM: %s:%d\n",
				(void *)(offset + head),
				(void *)(long)(event->header.size),
				event->comm.comm, event->comm.pid);
		}
		if (thread == NULL ||
		    thread__set_comm(thread, event->comm.comm)) {
			fprintf(stderr, "problem processing PERF_EVENT_COMM, skipping event.\n");
			goto broken_event;
		}
		total_comm++;
		break;
	}
	default: {
broken_event:
		if (dump_trace)
			fprintf(stderr, "%p [%p]: skipping unknown header type: %d\n",
					(void *)(offset + head),
					(void *)(long)(event->header.size),
					event->header.type);

		total_unknown++;

		/*
		 * assume we lost track of the stream, check alignment, and
		 * increment a single u64 in the hope to catch on again 'soon'.
		 */

		if (unlikely(head & 7))
			head &= ~7ULL;

		size = 8;
	}
	}

	head += size;

	if (offset + head < stat.st_size)
		goto more;

	rc = EXIT_SUCCESS;
	close(input);

	if (dump_trace) {
		fprintf(stderr, "      IP events: %10ld\n", total);
		fprintf(stderr, "    mmap events: %10ld\n", total_mmap);
		fprintf(stderr, "    comm events: %10ld\n", total_comm);
		fprintf(stderr, " unknown events: %10ld\n", total_unknown);

		return 0;
	}

	if (verbose >= 2)
		dsos__fprintf(stdout);

	output__resort();
	output__fprintf(stdout, total);

	return rc;
}

static const char * const report_usage[] = {
	"perf report [<options>] <command>",
	NULL
};

static const struct option options[] = {
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_STRING('k', "vmlinux", &vmlinux, "file", "vmlinux pathname"),
	OPT_STRING('s', "sort", &sort_order, "key[,key2...]",
		   "sort by key(s): pid, comm, dso, symbol. Default: pid,symbol"),
	OPT_BOOLEAN('P', "full-paths", &full_paths,
		    "Don't shorten the pathnames taking into account the cwd"),
	OPT_END()
};

int cmd_report(int argc, const char **argv, const char *prefix)
{
	symbol__init();

	page_size = getpagesize();

	parse_options(argc, argv, options, report_usage, 0);

	setup_sorting();

	setup_pager();

	return __cmd_report();
}

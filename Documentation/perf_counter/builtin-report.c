#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <getopt.h>
#include <assert.h>
#include <search.h>

#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/unistd.h>
#include <linux/types.h>

#include "../../include/linux/perf_counter.h"
#include "list.h"

#define SHOW_KERNEL	1
#define SHOW_USER	2
#define SHOW_HV		4

static char 		const *input_name = "output.perf";
static int		input;
static int		show_mask = SHOW_KERNEL | SHOW_USER | SHOW_HV;

static unsigned long	page_size;
static unsigned long	mmap_window = 32;

static const char *perf_event_names[] = {
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

struct section {
	struct list_head node;
	uint64_t	 start;
	uint64_t	 end;
	uint64_t	 offset;
	char		 name[0];
};

static struct section *section__new(uint64_t start, uint64_t size,
				    uint64_t offset, char *name)
{
	struct section *self = malloc(sizeof(*self) + strlen(name) + 1);

	if (self != NULL) {
		self->start  = start;
		self->end    = start + size;
		self->offset = offset;
		strcpy(self->name, name);
	}

	return self;
}

static void section__delete(struct section *self)
{
	free(self);
}

struct symbol {
	struct list_head node;
	uint64_t	 start;
	uint64_t	 end;
	char		 name[0];
};

static struct symbol *symbol__new(uint64_t start, uint64_t len, const char *name)
{
	struct symbol *self = malloc(sizeof(*self) + strlen(name) + 1);

	if (self != NULL) {
		self->start = start;
		self->end   = start + len;
		strcpy(self->name, name);
	}

	return self;
}

static void symbol__delete(struct symbol *self)
{
	free(self);
}

static size_t symbol__fprintf(struct symbol *self, FILE *fp)
{
	return fprintf(fp, " %lx-%lx %s\n",
		       self->start, self->end, self->name);
}

struct dso {
	struct list_head node;
	struct list_head sections;
	struct list_head syms;
	char		 name[0];
};

static struct dso *dso__new(const char *name)
{
	struct dso *self = malloc(sizeof(*self) + strlen(name) + 1);

	if (self != NULL) {
		strcpy(self->name, name);
		INIT_LIST_HEAD(&self->sections);
		INIT_LIST_HEAD(&self->syms);
	}

	return self;
}

static void dso__delete_sections(struct dso *self)
{
	struct section *pos, *n;

	list_for_each_entry_safe(pos, n, &self->sections, node)
		section__delete(pos);
}

static void dso__delete_symbols(struct dso *self)
{
	struct symbol *pos, *n;

	list_for_each_entry_safe(pos, n, &self->syms, node)
		symbol__delete(pos);
}

static void dso__delete(struct dso *self)
{
	dso__delete_sections(self);
	dso__delete_symbols(self);
	free(self);
}

static void dso__insert_symbol(struct dso *self, struct symbol *sym)
{
	list_add_tail(&sym->node, &self->syms);
}

static struct symbol *dso__find_symbol(struct dso *self, uint64_t ip)
{
	if (self == NULL)
		return NULL;

	struct symbol *pos;

	list_for_each_entry(pos, &self->syms, node)
		if (ip >= pos->start && ip <= pos->end)
			return pos;

	return NULL;
}

static int dso__load(struct dso *self)
{
	/* FIXME */
	return 0;
}

static size_t dso__fprintf(struct dso *self, FILE *fp)
{
	struct symbol *pos;
	size_t ret = fprintf(fp, "dso: %s\n", self->name);

	list_for_each_entry(pos, &self->syms, node)
		ret += symbol__fprintf(pos, fp);

	return ret;
}

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

	if (dso == NULL) {
		dso = dso__new(name);
		if (dso != NULL && dso__load(dso) < 0)
			goto out_delete_dso;

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

static int load_kallsyms(void)
{
	kernel_dso = dso__new("[kernel]");
	if (kernel_dso == NULL)
		return -1;

	FILE *file = fopen("/proc/kallsyms", "r");

	if (file == NULL)
		goto out_delete_dso;

	char *line = NULL;
	size_t n;

	while (!feof(file)) {
		unsigned long long start;
		char c, symbf[4096];

		if (getline(&line, &n, file) < 0)
			break;

		if (!line)
			goto out_delete_dso;

		if (sscanf(line, "%llx %c %s", &start, &c, symbf) == 3) {
			struct symbol *sym = symbol__new(start, 0x1000000, symbf);

			if (sym == NULL)
				goto out_delete_dso;

			dso__insert_symbol(kernel_dso, sym);
		}
	}

	dsos__add(kernel_dso);
	free(line);
	fclose(file);
	return 0;

out_delete_dso:
	dso__delete(kernel_dso);
	return -1;
}

struct map {
	struct list_head node;
	uint64_t	 start;
	uint64_t	 end;
	uint64_t	 pgoff;
	struct dso	 *dso;
};

static struct map *map__new(struct mmap_event *event)
{
	struct map *self = malloc(sizeof(*self));

	if (self != NULL) {
		self->start = event->start;
		self->end   = event->start + event->len;
		self->pgoff = event->pgoff;

		self->dso = dsos__findnew(event->filename);
		if (self->dso == NULL)
			goto out_delete;
	}
	return self;
out_delete:
	free(self);
	return NULL;
}

static size_t map__fprintf(struct map *self, FILE *fp)
{
	return fprintf(fp, " %lx-%lx %lx %s\n",
		       self->start, self->end, self->pgoff, self->dso->name);
}

struct symhist {
	struct list_head node;
	struct dso	 *dso;
	struct symbol	 *sym;
	uint32_t	 count;
	char		 level;
};

static struct symhist *symhist__new(struct symbol *sym, struct dso *dso,
				    char level)
{
	struct symhist *self = malloc(sizeof(*self));

	if (self != NULL) {
		self->sym   = sym;
		self->dso   = dso;
		self->level = level;
		self->count = 0;
	}

	return self;
}

static void symhist__delete(struct symhist *self)
{
	free(self);
}

static bool symhist__equal(struct symhist *self, struct symbol *sym,
			   struct dso *dso, char level)
{
	return self->level == level && self->sym == sym && self->dso == dso;
}

static void symhist__inc(struct symhist *self)
{
	++self->count;
}

static size_t symhist__fprintf(struct symhist *self, FILE *fp)
{
	size_t ret = fprintf(fp, "[%c] ", self->level);

	if (self->level != '.')
		ret += fprintf(fp, "%s", self->sym->name);
	else
		ret += fprintf(fp, "%s: %s",
			       self->dso ? self->dso->name : "<unknown",
			       self->sym ? self->sym->name : "<unknown>");
	return ret + fprintf(fp, ": %u\n", self->count);
}

struct thread {
	struct list_head node;
	struct list_head maps;
	struct list_head symhists;
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
		INIT_LIST_HEAD(&self->symhists);
	}

	return self;
}

static void thread__insert_symhist(struct thread *self,
				   struct symhist *symhist)
{
	list_add_tail(&symhist->node, &self->symhists);
}

static struct symhist *thread__symhists_find(struct thread *self,
					     struct symbol *sym,
					     struct dso *dso, char level)
{
	struct symhist *pos;

	list_for_each_entry(pos, &self->symhists, node)
		if (symhist__equal(pos, sym, dso, level))
			return pos;

	return NULL;
}

static int thread__symbol_incnew(struct thread *self, struct symbol *sym,
				 struct dso *dso, char level)
{
	struct symhist *symhist = thread__symhists_find(self, sym, dso, level);

	if (symhist == NULL) {
		symhist = symhist__new(sym, dso, level);
		if (symhist == NULL)
			goto out_error;
		thread__insert_symhist(self, symhist);
	}

	symhist__inc(symhist);
	return 0;
out_error:
	return -ENOMEM;
}

static int thread__set_comm(struct thread *self, const char *comm)
{
	self->comm = strdup(comm);
	return self->comm ? 0 : -ENOMEM;
}

static size_t thread__maps_fprintf(struct thread *self, FILE *fp)
{
	struct map *pos;
	size_t ret = 0;

	list_for_each_entry(pos, &self->maps, node)
		ret += map__fprintf(pos, fp);

	return ret;
}

static size_t thread__fprintf(struct thread *self, FILE *fp)
{
	struct symhist *pos;
	int ret = fprintf(fp, "thread: %d %s\n", self->pid, self->comm);

	list_for_each_entry(pos, &self->symhists, node)
		ret += symhist__fprintf(pos, fp);

	return ret;
}

static LIST_HEAD(threads);

static void threads__add(struct thread *thread)
{
	list_add_tail(&thread->node, &threads);
}

static struct thread *threads__find(pid_t pid)
{
	struct thread *pos;

	list_for_each_entry(pos, &threads, node)
		if (pos->pid == pid)
			return pos;
	return NULL;
}

static struct thread *threads__findnew(pid_t pid)
{
	struct thread *thread = threads__find(pid);

	if (thread == NULL) {
		thread = thread__new(pid);
		if (thread != NULL)
			threads__add(thread);
	}

	return thread;
}

static void thread__insert_map(struct thread *self, struct map *map)
{
	list_add_tail(&map->node, &self->maps);
}

static struct map *thread__find_map(struct thread *self, uint64_t ip)
{
	if (self == NULL)
		return NULL;

	struct map *pos;

	list_for_each_entry(pos, &self->maps, node)
		if (ip >= pos->start && ip <= pos->end)
			return pos;

	return NULL;
}

static void threads__fprintf(FILE *fp)
{
	struct thread *pos;

	list_for_each_entry(pos, &threads, node)
		thread__fprintf(pos, fp);
}

#if 0
static std::string resolve_user_symbol(int pid, uint64_t ip)
{
	std::string sym = "<unknown>";

	maps_t &m = maps[pid];
	maps_t::const_iterator mi = m.upper_bound(map(ip));
	if (mi == m.end())
		return sym;

	ip -= mi->start + mi->pgoff;

	symbols_t &s = dsos[mi->dso].syms;
	symbols_t::const_iterator si = s.upper_bound(symbol(ip));

	sym = mi->dso + ": <unknown>";

	if (si == s.begin())
		return sym;
	si--;

	if (si->start <= ip && ip < si->end)
		sym = mi->dso + ": " + si->name;
#if 0
	else if (si->start <= ip)
		sym = mi->dso + ": ?" + si->name;
#endif

	return sym;
}
#endif

static void display_help(void)
{
	printf(
	"Usage: perf-report [<options>]\n"
	" -i file   --input=<file>      # input file\n"
	);

	exit(0);
}

static void process_options(int argc, char *argv[])
{
	int error = 0;

	for (;;) {
		int option_index = 0;
		/** Options for getopt */
		static struct option long_options[] = {
			{"input",	required_argument,	NULL, 'i'},
			{"no-user",	no_argument,		NULL, 'u'},
			{"no-kernel",	no_argument,		NULL, 'k'},
			{"no-hv",	no_argument,		NULL, 'h'},
			{NULL,		0,			NULL,  0 }
		};
		int c = getopt_long(argc, argv, "+:i:kuh",
				    long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'i': input_name			= strdup(optarg); break;
		case 'k': show_mask &= ~SHOW_KERNEL; break;
		case 'u': show_mask &= ~SHOW_USER; break;
		case 'h': show_mask &= ~SHOW_HV; break;
		default: error = 1; break;
		}
	}

	if (error)
		display_help();
}

int cmd_report(int argc, char **argv)
{
	unsigned long offset = 0;
	unsigned long head = 0;
	struct stat stat;
	char *buf;
	event_t *event;
	int ret, rc = EXIT_FAILURE;
	unsigned long total = 0;

	page_size = getpagesize();

	process_options(argc, argv);

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

	if (load_kallsyms() < 0) {
		perror("failed to open kallsyms");
		return EXIT_FAILURE;
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

	if (head + event->header.size >= page_size * mmap_window) {
		unsigned long shift = page_size * (head / page_size);
		int ret;

		ret = munmap(buf, page_size * mmap_window);
		assert(ret == 0);

		offset += shift;
		head -= shift;
		goto remap;
	}


	if (!event->header.size) {
		fprintf(stderr, "zero-sized event at file offset %ld\n", offset + head);
		fprintf(stderr, "skipping %ld bytes of events.\n", stat.st_size - offset - head);
		goto done;
	}

	head += event->header.size;

	if (event->header.misc & PERF_EVENT_MISC_OVERFLOW) {
		char level;
		int show = 0;
		struct dso *dso = NULL;
		struct thread *thread = threads__findnew(event->ip.pid);

		if (thread == NULL)
			goto done;

		if (event->header.misc & PERF_EVENT_MISC_KERNEL) {
			show = SHOW_KERNEL;
			level = 'k';
			dso = kernel_dso;
		} else if (event->header.misc & PERF_EVENT_MISC_USER) {
			show = SHOW_USER;
			level = '.';
			struct map *map = thread__find_map(thread, event->ip.ip);
			if (map != NULL)
				dso = map->dso;
		} else {
			show = SHOW_HV;
			level = 'H';
		}

		if (show & show_mask) {
			struct symbol *sym = dso__find_symbol(dso, event->ip.ip);

			if (thread__symbol_incnew(thread, sym, dso, level))
				goto done;
		}
		total++;
	} else switch (event->header.type) {
	case PERF_EVENT_MMAP: {
		struct thread *thread = threads__findnew(event->mmap.pid);
		struct map *map = map__new(&event->mmap);

		if (thread == NULL || map == NULL )
			goto done;
		thread__insert_map(thread, map);
		break;
	}
	case PERF_EVENT_COMM: {
		struct thread *thread = threads__findnew(event->comm.pid);

		if (thread == NULL ||
		    thread__set_comm(thread, event->comm.comm))
			goto done;
		break;
	}
	}

	if (offset + head < stat.st_size)
		goto more;

	rc = EXIT_SUCCESS;
done:
	close(input);
	//dsos__fprintf(stdout);
	threads__fprintf(stdout);
#if 0
	std::map<std::string, int>::iterator hi = hist.begin();

	while (hi != hist.end()) {
		rev_hist.insert(std::pair<int, std::string>(hi->second, hi->first));
		hist.erase(hi++);
	}

	std::multimap<int, std::string>::const_iterator ri = rev_hist.begin();

	while (ri != rev_hist.end()) {
		printf(" %5.2f %s\n", (100.0 * ri->first)/total, ri->second.c_str());
		ri++;
	}
#endif
	return rc;
}


#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <getopt.h>

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

#include <set>
#include <map>
#include <string>


static char 		const *input_name = "output.perf";
static int		input;

static unsigned long	page_size;
static unsigned long	mmap_window = 32;

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
	uint64_t start;
	uint64_t end;

	uint64_t offset;

	std::string name;

	section() { };

	section(uint64_t stab) : end(stab) { };

	section(uint64_t start, uint64_t size, uint64_t offset, std::string name) :
		start(start), end(start + size), offset(offset), name(name)
	{ };

	bool operator < (const struct section &s) const {
		return end < s.end;
	};
};

typedef std::set<struct section> sections_t;

struct symbol {
	uint64_t start;
	uint64_t end;

	std::string name;

	symbol() { };

	symbol(uint64_t ip) : start(ip) { }

	symbol(uint64_t start, uint64_t len, std::string name) :
		start(start), end(start + len), name(name)
	{ };

	bool operator < (const struct symbol &s) const {
		return start < s.start;
	};
};

typedef std::set<struct symbol> symbols_t;

struct dso {
	sections_t sections;
	symbols_t syms;
};

static std::map<std::string, struct dso> dsos;

static void load_dso_sections(std::string dso_name)
{
	struct dso &dso = dsos[dso_name];

	std::string cmd = "readelf -DSW " + dso_name;

	FILE *file = popen(cmd.c_str(), "r");
	if (!file) {
		perror("failed to open pipe");
		exit(-1);
	}

	char *line = NULL;
	size_t n = 0;

	while (!feof(file)) {
		uint64_t addr, off, size;
		char name[32];

		if (getline(&line, &n, file) < 0)
			break;
		if (!line)
			break;

		if (sscanf(line, "  [%*2d] %16s %*14s %Lx %Lx %Lx",
					name, &addr, &off, &size) == 4) {

			dso.sections.insert(section(addr, size, addr - off, name));
		}
#if 0
		/*
		 * for reading readelf symbols (-s), however these don't seem
		 * to include nearly everything, so use nm for that.
		 */
		if (sscanf(line, " %*4d %*3d: %Lx %5Lu %*7s %*6s %*7s %3d %s",
			   &start, &size, &section, sym) == 4) {

			start -= dso.section_offsets[section];

			dso.syms.insert(symbol(start, size, std::string(sym)));
		}
#endif
	}
	pclose(file);
}

static void load_dso_symbols(std::string dso_name, std::string args)
{
	struct dso &dso = dsos[dso_name];

	std::string cmd = "nm -nSC " + args + " " + dso_name;

	FILE *file = popen(cmd.c_str(), "r");
	if (!file) {
		perror("failed to open pipe");
		exit(-1);
	}

	char *line = NULL;
	size_t n = 0;

	while (!feof(file)) {
		uint64_t start, size;
		char c;
		char sym[1024];

		if (getline(&line, &n, file) < 0)
			break;
		if (!line)
			break;


		if (sscanf(line, "%Lx %Lx %c %s", &start, &size, &c, sym) == 4) {
			sections_t::const_iterator si =
				dso.sections.upper_bound(section(start));
			if (si == dso.sections.end()) {
				printf("symbol in unknown section: %s\n", sym);
				continue;
			}

			start -= si->offset;

			dso.syms.insert(symbol(start, size, sym));
		}
	}
	pclose(file);
}

static void load_dso(std::string dso_name)
{
	load_dso_sections(dso_name);
	load_dso_symbols(dso_name, "-D"); /* dynamic symbols */
	load_dso_symbols(dso_name, "");   /* regular ones */
}

void load_kallsyms(void)
{
	struct dso &dso = dsos["[kernel]"];

	FILE *file = fopen("/proc/kallsyms", "r");
	if (!file) {
		perror("failed to open kallsyms");
		exit(-1);
	}

	char *line;
	size_t n;

	while (!feof(file)) {
		uint64_t start;
		char c;
		char sym[1024];

		if (getline(&line, &n, file) < 0)
			break;
		if (!line)
			break;

		if (sscanf(line, "%Lx %c %s", &start, &c, sym) == 3)
			dso.syms.insert(symbol(start, 0x1000000, std::string(sym)));
	}
	fclose(file);
}

struct map {
	uint64_t start;
	uint64_t end;
	uint64_t pgoff;

	std::string dso;

	map() { };

	map(uint64_t ip) : end(ip) { }

	map(mmap_event *mmap) {
		start = mmap->start;
		end = mmap->start + mmap->len;
		pgoff = mmap->pgoff;

		dso = std::string(mmap->filename);

		if (dsos.find(dso) == dsos.end())
			load_dso(dso);
	};

	bool operator < (const struct map &m) const {
		return end < m.end;
	};
};

typedef std::set<struct map> maps_t;

static std::map<int, maps_t> maps;

static std::map<int, std::string> comms;

static std::map<std::string, int> hist;
static std::multimap<int, std::string> rev_hist;

static std::string resolve_comm(int pid)
{
	std::string comm;

	std::map<int, std::string>::const_iterator ci = comms.find(pid);
	if (ci != comms.end()) {
		comm = ci->second;
	} else {
		char pid_str[30];

		sprintf(pid_str, ":%d", pid);
		comm = pid_str;
	}

	return comm;
}

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

static std::string resolve_kernel_symbol(uint64_t ip)
{
	std::string sym = "<unknown>";

	symbols_t &s = dsos["[kernel]"].syms;
	symbols_t::const_iterator si = s.upper_bound(symbol(ip));

	if (si == s.begin())
		return sym;
	si--;

	if (si->start <= ip && ip < si->end)
		sym = si->name;

	return sym;
}

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
			{NULL,		0,			NULL,  0 }
		};
		int c = getopt_long(argc, argv, "+:i:",
				    long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'i': input_name			= strdup(optarg); break;
		default: error = 1; break;
		}
	}

	if (error)
		display_help();
}

int main(int argc, char *argv[])
{
	unsigned long offset = 0;
	unsigned long head = 0;
	struct stat stat;
	char *buf;
	event_t *event;
	int ret;
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

	load_kallsyms();

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

		munmap(buf, page_size * mmap_window);
		offset += shift;
		head -= shift;
		goto remap;
	}
	head += event->header.size;

	if (event->header.misc & PERF_EVENT_MISC_OVERFLOW) {
		std::string comm, sym, level;
		char output[1024];

		if (event->header.misc & PERF_EVENT_MISC_KERNEL) {
			level = " [k] ";
			sym = resolve_kernel_symbol(event->ip.ip);
		} else if (event->header.misc & PERF_EVENT_MISC_USER) {
			level = " [.] ";
			sym = resolve_user_symbol(event->ip.pid, event->ip.ip);
		} else {
			level = " [H] ";
		}
		comm = resolve_comm(event->ip.pid);

		snprintf(output, sizeof(output), "%16s %s %s",
				comm.c_str(), level.c_str(), sym.c_str());
		hist[output]++;

		total++;

	} else switch (event->header.type) {
	case PERF_EVENT_MMAP:
		maps[event->mmap.pid].insert(map(&event->mmap));
		break;

	case PERF_EVENT_COMM:
		comms[event->comm.pid] = std::string(event->comm.comm);
		break;
	}

	if (offset + head < stat.st_size)
		goto more;

	close(input);

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

	return 0;
}


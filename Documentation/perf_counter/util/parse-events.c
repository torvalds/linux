
#include "../perf.h"
#include "util.h"
#include "parse-options.h"
#include "parse-events.h"
#include "exec_cmd.h"
#include "string.h"

extern char *strcasestr(const char *haystack, const char *needle);

int					nr_counters;

struct perf_counter_attr		attrs[MAX_COUNTERS];

struct event_symbol {
	__u8	type;
	__u64	config;
	char	*symbol;
};

#define C(x, y) .type = PERF_TYPE_##x, .config = PERF_COUNT_##y
#define CR(x, y) .type = PERF_TYPE_##x, .config = y

static struct event_symbol event_symbols[] = {
  { C(HARDWARE, CPU_CYCLES),		"cpu-cycles",		},
  { C(HARDWARE, CPU_CYCLES),		"cycles",		},
  { C(HARDWARE, INSTRUCTIONS),		"instructions",		},
  { C(HARDWARE, CACHE_REFERENCES),	"cache-references",	},
  { C(HARDWARE, CACHE_MISSES),		"cache-misses",		},
  { C(HARDWARE, BRANCH_INSTRUCTIONS),	"branch-instructions",	},
  { C(HARDWARE, BRANCH_INSTRUCTIONS),	"branches",		},
  { C(HARDWARE, BRANCH_MISSES),		"branch-misses",	},
  { C(HARDWARE, BUS_CYCLES),		"bus-cycles",		},

  { C(SOFTWARE, CPU_CLOCK),		"cpu-clock",		},
  { C(SOFTWARE, TASK_CLOCK),		"task-clock",		},
  { C(SOFTWARE, PAGE_FAULTS),		"page-faults",		},
  { C(SOFTWARE, PAGE_FAULTS),		"faults",		},
  { C(SOFTWARE, PAGE_FAULTS_MIN),	"minor-faults",		},
  { C(SOFTWARE, PAGE_FAULTS_MAJ),	"major-faults",		},
  { C(SOFTWARE, CONTEXT_SWITCHES),	"context-switches",	},
  { C(SOFTWARE, CONTEXT_SWITCHES),	"cs",			},
  { C(SOFTWARE, CPU_MIGRATIONS),	"cpu-migrations",	},
  { C(SOFTWARE, CPU_MIGRATIONS),	"migrations",		},
};

#define __PERF_COUNTER_FIELD(config, name) \
	((config & PERF_COUNTER_##name##_MASK) >> PERF_COUNTER_##name##_SHIFT)

#define PERF_COUNTER_RAW(config)	__PERF_COUNTER_FIELD(config, RAW)
#define PERF_COUNTER_CONFIG(config)	__PERF_COUNTER_FIELD(config, CONFIG)
#define PERF_COUNTER_TYPE(config)	__PERF_COUNTER_FIELD(config, TYPE)
#define PERF_COUNTER_ID(config)		__PERF_COUNTER_FIELD(config, EVENT)

static char *hw_event_names[] = {
	"CPU cycles",
	"instructions",
	"cache references",
	"cache misses",
	"branches",
	"branch misses",
	"bus cycles",
};

static char *sw_event_names[] = {
	"cpu clock ticks",
	"task clock ticks",
	"pagefaults",
	"context switches",
	"CPU migrations",
	"minor faults",
	"major faults",
};

#define MAX_ALIASES 8

static char *hw_cache [][MAX_ALIASES] = {
	{ "l1-d" ,	"l1d" ,	"l1", "l1-data-cache"			},
	{ "l1-i" ,	"l1i" ,	"l1-instruction-cache"		},
	{ "l2"  , },
	{ "dtlb", },
	{ "itlb", },
	{ "bpu" , "btb", "branch-cache", NULL },
};

static char *hw_cache_op [][MAX_ALIASES] = {
	{ "read"	, "load" },
	{ "write"	, "store" },
	{ "prefetch"	, "speculative-read", "speculative-load" },
};

static char *hw_cache_result [][MAX_ALIASES] = {
	{ "access", "ops" },
	{ "miss", },
};

char *event_name(int counter)
{
	__u64 config = attrs[counter].config;
	int type = attrs[counter].type;
	static char buf[32];

	if (attrs[counter].type == PERF_TYPE_RAW) {
		sprintf(buf, "raw 0x%llx", config);
		return buf;
	}

	switch (type) {
	case PERF_TYPE_HARDWARE:
		if (config < PERF_HW_EVENTS_MAX)
			return hw_event_names[config];
		return "unknown-hardware";

	case PERF_TYPE_HW_CACHE: {
		__u8 cache_type, cache_op, cache_result;
		static char name[100];

		cache_type   = (config >>  0) & 0xff;
		if (cache_type > PERF_COUNT_HW_CACHE_MAX)
			return "unknown-ext-hardware-cache-type";

		cache_op     = (config >>  8) & 0xff;
		if (cache_type > PERF_COUNT_HW_CACHE_OP_MAX)
			return "unknown-ext-hardware-cache-op-type";

		cache_result = (config >> 16) & 0xff;
		if (cache_type > PERF_COUNT_HW_CACHE_RESULT_MAX)
			return "unknown-ext-hardware-cache-result-type";

		sprintf(name, "%s:%s:%s",
			hw_cache[cache_type][0],
			hw_cache_op[cache_op][0],
			hw_cache_result[cache_result][0]);

		return name;
	}

	case PERF_TYPE_SOFTWARE:
		if (config < PERF_SW_EVENTS_MAX)
			return sw_event_names[config];
		return "unknown-software";

	default:
		break;
	}

	return "unknown";
}

static int parse_aliases(const char *str, char *names[][MAX_ALIASES], int size)
{
	int i, j;

	for (i = 0; i < size; i++) {
		for (j = 0; j < MAX_ALIASES; j++) {
			if (!names[i][j])
				break;
			if (strcasestr(str, names[i][j]))
				return i;
		}
	}

	return 0;
}

static int parse_generic_hw_symbols(const char *str, struct perf_counter_attr *attr)
{
	__u8 cache_type = -1, cache_op = 0, cache_result = 0;

	cache_type = parse_aliases(str, hw_cache, PERF_COUNT_HW_CACHE_MAX);
	/*
	 * No fallback - if we cannot get a clear cache type
	 * then bail out:
	 */
	if (cache_type == -1)
		return -EINVAL;

	cache_op = parse_aliases(str, hw_cache_op, PERF_COUNT_HW_CACHE_OP_MAX);
	/*
	 * Fall back to reads:
	 */
	if (cache_type == -1)
		cache_type = PERF_COUNT_HW_CACHE_OP_READ;

	cache_result = parse_aliases(str, hw_cache_result,
					PERF_COUNT_HW_CACHE_RESULT_MAX);
	/*
	 * Fall back to accesses:
	 */
	if (cache_result == -1)
		cache_result = PERF_COUNT_HW_CACHE_RESULT_ACCESS;

	attr->config = cache_type | (cache_op << 8) | (cache_result << 16);
	attr->type = PERF_TYPE_HW_CACHE;

	return 0;
}

/*
 * Each event can have multiple symbolic names.
 * Symbolic names are (almost) exactly matched.
 */
static int parse_event_symbols(const char *str, struct perf_counter_attr *attr)
{
	__u64 config, id;
	int type;
	unsigned int i;
	const char *sep, *pstr;

	if (str[0] == 'r' && hex2u64(str + 1, &config) > 0) {
		attr->type = PERF_TYPE_RAW;
		attr->config = config;

		return 0;
	}

	pstr = str;
	sep = strchr(pstr, ':');
	if (sep) {
		type = atoi(pstr);
		pstr = sep + 1;
		id = atoi(pstr);
		sep = strchr(pstr, ':');
		if (sep) {
			pstr = sep + 1;
			if (strchr(pstr, 'k'))
				attr->exclude_user = 1;
			if (strchr(pstr, 'u'))
				attr->exclude_kernel = 1;
		}
		attr->type = type;
		attr->config = id;

		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++) {
		if (!strncmp(str, event_symbols[i].symbol,
			     strlen(event_symbols[i].symbol))) {

			attr->type = event_symbols[i].type;
			attr->config = event_symbols[i].config;

			return 0;
		}
	}

	return parse_generic_hw_symbols(str, attr);
}

int parse_events(const struct option *opt, const char *str, int unset)
{
	struct perf_counter_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
again:
	if (nr_counters == MAX_COUNTERS)
		return -1;

	ret = parse_event_symbols(str, &attr);
	if (ret < 0)
		return ret;

	attrs[nr_counters] = attr;
	nr_counters++;

	str = strstr(str, ",");
	if (str) {
		str++;
		goto again;
	}

	return 0;
}

static const char * const event_type_descriptors[] = {
	"",
	"Hardware event",
	"Software event",
	"Tracepoint event",
	"Hardware cache event",
};

/*
 * Print the help text for the event symbols:
 */
void print_events(void)
{
	struct event_symbol *syms = event_symbols;
	unsigned int i, type, prev_type = -1;

	fprintf(stderr, "\n");
	fprintf(stderr, "List of pre-defined events (to be used in -e):\n");

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++, syms++) {
		type = syms->type + 1;
		if (type > ARRAY_SIZE(event_type_descriptors))
			type = 0;

		if (type != prev_type)
			fprintf(stderr, "\n");

		fprintf(stderr, "  %-30s [%s]\n", syms->symbol,
			event_type_descriptors[type]);

		prev_type = type;
	}

	fprintf(stderr, "\n");
	fprintf(stderr, "  %-30s [raw hardware event descriptor]\n",
		"rNNN");
	fprintf(stderr, "\n");

	exit(129);
}

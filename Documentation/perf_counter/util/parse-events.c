
#include "../perf.h"
#include "util.h"
#include "parse-options.h"
#include "parse-events.h"
#include "exec_cmd.h"

int nr_counters;

__u64			event_id[MAX_COUNTERS]		= { };
int			event_mask[MAX_COUNTERS];

struct event_symbol {
	__u64 event;
	char *symbol;
};

static struct event_symbol event_symbols[] = {
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_CPU_CYCLES),		"cpu-cycles",		},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_CPU_CYCLES),		"cycles",		},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_INSTRUCTIONS),		"instructions",		},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_CACHE_REFERENCES),		"cache-references",	},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_CACHE_MISSES),		"cache-misses",		},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_BRANCH_INSTRUCTIONS),	"branch-instructions",	},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_BRANCH_INSTRUCTIONS),	"branches",		},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_BRANCH_MISSES),		"branch-misses",	},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_BUS_CYCLES),		"bus-cycles",		},

	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_CPU_CLOCK),			"cpu-clock",		},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_TASK_CLOCK),		"task-clock",		},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_PAGE_FAULTS),		"page-faults",		},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_PAGE_FAULTS),		"faults",		},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_PAGE_FAULTS_MIN),		"minor-faults",		},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_PAGE_FAULTS_MAJ),		"major-faults",		},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_CONTEXT_SWITCHES),		"context-switches",	},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_CONTEXT_SWITCHES),		"cs",			},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_CPU_MIGRATIONS),		"cpu-migrations",	},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_CPU_MIGRATIONS),		"migrations",		},
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

char *event_name(int ctr)
{
	__u64 config = event_id[ctr];
	int type = PERF_COUNTER_TYPE(config);
	int id = PERF_COUNTER_ID(config);
	static char buf[32];

	if (PERF_COUNTER_RAW(config)) {
		sprintf(buf, "raw 0x%llx", PERF_COUNTER_CONFIG(config));
		return buf;
	}

	switch (type) {
	case PERF_TYPE_HARDWARE:
		if (id < PERF_HW_EVENTS_MAX)
			return hw_event_names[id];
		return "unknown-hardware";

	case PERF_TYPE_SOFTWARE:
		if (id < PERF_SW_EVENTS_MAX)
			return sw_event_names[id];
		return "unknown-software";

	default:
		break;
	}

	return "unknown";
}

/*
 * Each event can have multiple symbolic names.
 * Symbolic names are (almost) exactly matched.
 */
static __u64 match_event_symbols(const char *str)
{
	__u64 config, id;
	int type;
	unsigned int i;
	char mask_str[4];

	if (sscanf(str, "r%llx", &config) == 1)
		return config | PERF_COUNTER_RAW_MASK;

	switch (sscanf(str, "%d:%llu:%2s", &type, &id, mask_str)) {
		case 3:
			if (strchr(mask_str, 'k'))
				event_mask[nr_counters] |= EVENT_MASK_USER;
			if (strchr(mask_str, 'u'))
				event_mask[nr_counters] |= EVENT_MASK_KERNEL;
		case 2:
			return EID(type, id);

		default:
			break;
	}

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++) {
		if (!strncmp(str, event_symbols[i].symbol,
			     strlen(event_symbols[i].symbol)))
			return event_symbols[i].event;
	}

	return ~0ULL;
}

int parse_events(const struct option *opt, const char *str, int unset)
{
	__u64 config;

again:
	if (nr_counters == MAX_COUNTERS)
		return -1;

	config = match_event_symbols(str);
	if (config == ~0ULL)
		return -1;

	event_id[nr_counters] = config;
	nr_counters++;

	str = strstr(str, ",");
	if (str) {
		str++;
		goto again;
	}

	return 0;
}

/*
 * Create the help text for the event symbols:
 */
void create_events_help(char *events_help_msg)
{
	unsigned int i;
	char *str;
	__u64 e;

	str = events_help_msg;

	str += sprintf(str,
	"event name: [");

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++) {
		int type, id;

		e = event_symbols[i].event;
		type = PERF_COUNTER_TYPE(e);
		id = PERF_COUNTER_ID(e);

		if (i)
			str += sprintf(str, "|");

		str += sprintf(str, "%s",
				event_symbols[i].symbol);
	}

	str += sprintf(str, "|rNNN]");
}


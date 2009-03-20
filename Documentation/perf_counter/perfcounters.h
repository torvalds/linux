/*
 * Ioctls that can be done on a perf counter fd:
 */
#define PERF_COUNTER_IOC_ENABLE		_IO('$', 0)
#define PERF_COUNTER_IOC_DISABLE	_IO('$', 1)

/*
 * prctl(PR_TASK_PERF_COUNTERS_DISABLE) will (cheaply) disable all
 * counters in the current task.
 */
#define PR_TASK_PERF_COUNTERS_DISABLE	31
#define PR_TASK_PERF_COUNTERS_ENABLE    32

#define MAX_COUNTERS			64
#define MAX_NR_CPUS			256

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define rdclock()					\
({							\
	struct timespec ts;				\
							\
	clock_gettime(CLOCK_MONOTONIC, &ts);		\
	ts.tv_sec * 1000000000ULL + ts.tv_nsec;		\
})

/*
 * Pick up some kernel type conventions:
 */
#define __user
#define asmlinkage

typedef unsigned int		__u32;
typedef unsigned long long	__u64;
typedef long long		__s64;

/*
 * User-space ABI bits:
 */

/*
 * Generalized performance counter event types, used by the hw_event.type
 * parameter of the sys_perf_counter_open() syscall:
 */
enum hw_event_types {
	/*
	 * Common hardware events, generalized by the kernel:
	 */
	PERF_COUNT_CPU_CYCLES		=  0,
	PERF_COUNT_INSTRUCTIONS		=  1,
	PERF_COUNT_CACHE_REFERENCES	=  2,
	PERF_COUNT_CACHE_MISSES		=  3,
	PERF_COUNT_BRANCH_INSTRUCTIONS	=  4,
	PERF_COUNT_BRANCH_MISSES	=  5,
	PERF_COUNT_BUS_CYCLES		=  6,

	PERF_HW_EVENTS_MAX		=  7,

	/*
	 * Special "software" counters provided by the kernel, even if
	 * the hardware does not support performance counters. These
	 * counters measure various physical and sw events of the
	 * kernel (and allow the profiling of them as well):
	 */
	PERF_COUNT_CPU_CLOCK		= -1,
	PERF_COUNT_TASK_CLOCK		= -2,
	PERF_COUNT_PAGE_FAULTS		= -3,
	PERF_COUNT_CONTEXT_SWITCHES	= -4,
	PERF_COUNT_CPU_MIGRATIONS	= -5,

	PERF_SW_EVENTS_MIN		= -6,
};

/*
 * IRQ-notification data record type:
 */
enum perf_counter_record_type {
	PERF_RECORD_SIMPLE		=  0,
	PERF_RECORD_IRQ			=  1,
	PERF_RECORD_GROUP		=  2,
};

/*
 * Hardware event to monitor via a performance monitoring counter:
 */
struct perf_counter_hw_event {
	__s64			type;

	__u64			irq_period;
	__u64			record_type;
	__u64			read_format;

	__u64			disabled       :  1, /* off by default        */
				nmi	       :  1, /* NMI sampling          */
				raw	       :  1, /* raw event type        */
				inherit	       :  1, /* children inherit it   */
				pinned	       :  1, /* must always be on PMU */
				exclusive      :  1, /* only group on PMU     */
				exclude_user   :  1, /* don't count user      */
				exclude_kernel :  1, /* ditto kernel          */
				exclude_hv     :  1, /* ditto hypervisor      */
				exclude_idle   :  1, /* don't count when idle */

				__reserved_1   : 54;

	__u32			extra_config_len;
	__u32			__reserved_4;

	__u64			__reserved_2;
	__u64			__reserved_3;
};

#ifdef __x86_64__
# define __NR_perf_counter_open	295
#endif

#ifdef __i386__
# define __NR_perf_counter_open 333
#endif

#ifdef __powerpc__
#define __NR_perf_counter_open 319
#endif

asmlinkage int sys_perf_counter_open(

	struct perf_counter_hw_event	*hw_event_uptr		__user,
	pid_t				pid,
	int				cpu,
	int				group_fd,
	unsigned long			flags)
{
	int ret;

	ret = syscall(
		__NR_perf_counter_open, hw_event_uptr, pid, cpu, group_fd, flags);
#if defined(__x86_64__) || defined(__i386__)
	if (ret < 0 && ret > -4096) {
		errno = -ret;
		ret = -1;
	}
#endif
	return ret;
}

static char *hw_event_names [] = {
	"CPU cycles",
	"instructions",
	"cache references",
	"cache misses",
	"branches",
	"branch misses",
	"bus cycles",
};

static char *sw_event_names [] = {
	"cpu clock ticks",
	"task clock ticks",
	"pagefaults",
	"context switches",
	"CPU migrations",
};

struct event_symbol {
	int event;
	char *symbol;
};

static struct event_symbol event_symbols [] = {
	{PERF_COUNT_CPU_CYCLES,			"cpu-cycles",		},
	{PERF_COUNT_CPU_CYCLES,			"cycles",		},
	{PERF_COUNT_INSTRUCTIONS,		"instructions",		},
	{PERF_COUNT_CACHE_REFERENCES,		"cache-references",	},
	{PERF_COUNT_CACHE_MISSES,		"cache-misses",		},
	{PERF_COUNT_BRANCH_INSTRUCTIONS,	"branch-instructions",	},
	{PERF_COUNT_BRANCH_INSTRUCTIONS,	"branches",		},
	{PERF_COUNT_BRANCH_MISSES,		"branch-misses",	},
	{PERF_COUNT_BUS_CYCLES,			"bus-cycles",		},
	{PERF_COUNT_CPU_CLOCK,			"cpu-ticks",		},
	{PERF_COUNT_CPU_CLOCK,			"ticks",		},
	{PERF_COUNT_TASK_CLOCK,			"task-ticks",		},
	{PERF_COUNT_PAGE_FAULTS,		"page-faults",		},
	{PERF_COUNT_PAGE_FAULTS,		"faults",		},
	{PERF_COUNT_CONTEXT_SWITCHES,		"context-switches",	},
	{PERF_COUNT_CONTEXT_SWITCHES,		"cs",			},
	{PERF_COUNT_CPU_MIGRATIONS,		"cpu-migrations",	},
	{PERF_COUNT_CPU_MIGRATIONS,		"migrations",		},
};

static int type_valid(int type)
{
	if (type >= PERF_HW_EVENTS_MAX)
		return 0;
	if (type <= PERF_SW_EVENTS_MIN)
		return 0;

	return 1;
}

static char *event_name(int ctr)
{
	int type = event_id[ctr];
	static char buf[32];

	if (event_raw[ctr]) {
		sprintf(buf, "raw 0x%x", type);
		return buf;
	}
	if (!type_valid(type))
		return "unknown";

	if (type >= 0)
		return hw_event_names[type];

	return sw_event_names[-type-1];
}

/*
 * Each event can have multiple symbolic names.
 * Symbolic names are (almost) exactly matched.
 */
static int match_event_symbols(char *str)
{
	unsigned int i;

	if (isdigit(str[0]) || str[0] == '-')
		return atoi(str);

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++) {
		if (!strncmp(str, event_symbols[i].symbol,
			     strlen(event_symbols[i].symbol)))
			return event_symbols[i].event;
	}

	return PERF_HW_EVENTS_MAX;
}

static void parse_events(char *str)
{
	int type, raw;

again:
	nr_counters++;
	if (nr_counters == MAX_COUNTERS)
		display_help();

	raw = 0;
	if (*str == 'r') {
		raw = 1;
		++str;
		type = strtol(str, NULL, 16);
	} else {
		type = match_event_symbols(str);
		if (!type_valid(type))
			display_help();
	}

	event_id[nr_counters] = type;
	event_raw[nr_counters] = raw;

	str = strstr(str, ",");
	if (str) {
		str++;
		goto again;
	}
}


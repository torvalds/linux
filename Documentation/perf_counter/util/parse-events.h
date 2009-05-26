
/*
 * Parse symbolic events/counts passed in as options:
 */

extern int nr_counters;
extern __u64			event_id[MAX_COUNTERS];
extern int			event_mask[MAX_COUNTERS];

#define EVENT_MASK_KERNEL	1
#define EVENT_MASK_USER		2

extern char *event_name(int ctr);

extern int parse_events(const struct option *opt, const char *str, int unset);

#define EVENTS_HELP_MAX (128*1024)

extern void create_events_help(char *help_msg);


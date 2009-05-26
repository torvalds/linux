
extern int nr_counters;
extern __u64			event_id[MAX_COUNTERS];

extern int parse_events(const struct option *opt, const char *str, int unset);

#define EVENTS_HELP_MAX (128*1024)

extern void create_events_help(char *help_msg);


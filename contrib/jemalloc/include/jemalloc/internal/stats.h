#ifndef JEMALLOC_INTERNAL_STATS_H
#define JEMALLOC_INTERNAL_STATS_H

/*  OPTION(opt,		var_name,	default,	set_value_to) */
#define STATS_PRINT_OPTIONS						\
    OPTION('J',		json,		false,		true)		\
    OPTION('g',		general,	true,		false)		\
    OPTION('m',		merged,		config_stats,	false)		\
    OPTION('d',		destroyed,	config_stats,	false)		\
    OPTION('a',		unmerged,	config_stats,	false)		\
    OPTION('b',		bins,		true,		false)		\
    OPTION('l',		large,		true,		false)		\
    OPTION('x',		mutex,		true,		false)

enum {
#define OPTION(o, v, d, s) stats_print_option_num_##v,
    STATS_PRINT_OPTIONS
#undef OPTION
    stats_print_tot_num_options
};

/* Options for stats_print. */
extern bool opt_stats_print;
extern char opt_stats_print_opts[stats_print_tot_num_options+1];

/* Implements je_malloc_stats_print. */
void stats_print(void (*write_cb)(void *, const char *), void *cbopaque,
    const char *opts);

#endif /* JEMALLOC_INTERNAL_STATS_H */

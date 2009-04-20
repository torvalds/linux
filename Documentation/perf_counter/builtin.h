#ifndef BUILTIN_H
#define BUILTIN_H

#include "util.h"
#include "strbuf.h"

extern const char perf_version_string[];
extern const char perf_usage_string[];
extern const char perf_more_info_string[];

extern void list_common_cmds_help(void);
extern const char *help_unknown_cmd(const char *cmd);
extern void prune_packed_objects(int);
extern int read_line_with_nul(char *buf, int size, FILE *file);
extern int check_pager_config(const char *cmd);

extern int cmd_top(int argc, const char **argv, const char *prefix);
#endif

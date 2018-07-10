/* vi: set sw=4 ts=4: */
/*
 * Factored out of mpstat/iostat.
 *
 * Copyright (C) 2010 Marek Polacek <mmpolacek@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* Does str start with "cpu"? */
int FAST_FUNC starts_with_cpu(const char *str)
{
	return ((str[0] - 'c') | (str[1] - 'p') | (str[2] - 'u')) == 0;
}

/*
 * Get number of processors. Uses /proc/stat.
 * Return value 0 means one CPU and non SMP kernel.
 * Otherwise N means N processor(s) and SMP kernel.
 */
unsigned FAST_FUNC get_cpu_count(void)
{
	FILE *fp;
	char line[256];
	int proc_nr = -1;

	fp = xfopen_for_read("/proc/stat");
	while (fgets(line, sizeof(line), fp)) {
		if (!starts_with_cpu(line)) {
			if (proc_nr >= 0)
				break; /* we are past "cpuN..." lines */
			continue;
		}
		if (line[3] != ' ') { /* "cpuN" */
			int num_proc;
			if (sscanf(line + 3, "%u", &num_proc) == 1
			 && num_proc > proc_nr
			) {
				proc_nr = num_proc;
			}
		}
	}

	fclose(fp);
	return proc_nr + 1;
}

/*
 * arch/v850/kernel/procfs.c -- Introspection functions for /proc filesystem
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include "mach.h"

static int cpuinfo_print (struct seq_file *m, void *v)
{
	extern unsigned long loops_per_jiffy;
	
	seq_printf (m, "CPU-Family:	v850\nCPU-Arch:	%s\n", CPU_ARCH);

#ifdef CPU_MODEL_LONG
	seq_printf (m, "CPU-Model:	%s (%s)\n", CPU_MODEL, CPU_MODEL_LONG);
#else
	seq_printf (m, "CPU-Model:	%s\n", CPU_MODEL);
#endif

#ifdef CPU_CLOCK_FREQ
	seq_printf (m, "CPU-Clock:	%ld (%ld MHz)\n",
		    (long)CPU_CLOCK_FREQ,
		    (long)CPU_CLOCK_FREQ / 1000000);
#endif

	seq_printf (m, "BogoMips:	%lu.%02lu\n",
		    loops_per_jiffy/(500000/HZ),
		    (loops_per_jiffy/(5000/HZ)) % 100);

#ifdef PLATFORM_LONG
	seq_printf (m, "Platform:	%s (%s)\n", PLATFORM, PLATFORM_LONG);
#elif defined (PLATFORM)
	seq_printf (m, "Platform:	%s\n", PLATFORM);
#endif

	return 0;
}

static void *cpuinfo_start (struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? ((void *) 0x12345678) : NULL;
}

static void *cpuinfo_next (struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return cpuinfo_start (m, pos);
}

static void cpuinfo_stop (struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start	= cpuinfo_start,
	.next	= cpuinfo_next,
	.stop	= cpuinfo_stop,
	.show	= cpuinfo_print
};

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018, Christophe Leroy CS S.I.
 * <christophe.leroy@c-s.fr>
 *
 * This dumps the content of Segment Registers
 */

#include <linux/debugfs.h>

static void seg_show(struct seq_file *m, int i)
{
	u32 val = mfsr(i << 28);

	seq_printf(m, "0x%01x0000000-0x%01xfffffff ", i, i);
	seq_printf(m, "Kern key %d ", (val >> 30) & 1);
	seq_printf(m, "User key %d ", (val >> 29) & 1);
	if (val & 0x80000000) {
		seq_printf(m, "Device 0x%03x", (val >> 20) & 0x1ff);
		seq_printf(m, "-0x%05x", val & 0xfffff);
	} else {
		if (val & 0x10000000)
			seq_puts(m, "No Exec ");
		seq_printf(m, "VSID 0x%06x", val & 0xffffff);
	}
	seq_puts(m, "\n");
}

static int sr_show(struct seq_file *m, void *v)
{
	int i;

	seq_puts(m, "---[ User Segments ]---\n");
	for (i = 0; i < TASK_SIZE >> 28; i++)
		seg_show(m, i);

	seq_puts(m, "\n---[ Kernel Segments ]---\n");
	for (; i < 16; i++)
		seg_show(m, i);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(sr);

static int __init sr_init(void)
{
	debugfs_create_file("segment_registers", 0400, arch_debugfs_dir,
			    NULL, &sr_fops);
	return 0;
}
device_initcall(sr_init);

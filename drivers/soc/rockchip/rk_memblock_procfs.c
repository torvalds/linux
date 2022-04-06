// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Procfs for reserved memory blocks.
 *
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define K(size) ((unsigned long)((size) >> 10))

static int memblock_procfs_show(struct seq_file *m, void *private)
{
	struct memblock_type *type = m->private;
	struct memblock_region *reg;
	int i;
	phys_addr_t end;
	unsigned long z = 0, t = 0;

	for (i = 0; i < type->cnt; i++) {
		reg = &type->regions[i];
		end = reg->base + reg->size - 1;
		z = (unsigned long)reg->size;
		t += z;

		seq_printf(m, "%4d: ", i);
		seq_printf(m, "%pa..%pa (%10lu %s)\n", &reg->base, &end,
			   (z >= 1024) ? (K(z)) : z,
			   (z >= 1024) ? "KiB" : "Bytes");
	}
	seq_printf(m, "Total: %lu KiB\n", K(t));

	return 0;
}

static int __init rk_memblock_procfs_init(void)
{
	struct proc_dir_entry *root = proc_mkdir("rk_memblock", NULL);

	proc_create_single_data("memory", 0, root, memblock_procfs_show,
		&memblock.memory);
	proc_create_single_data("reserved", 0, root, memblock_procfs_show,
		&memblock.reserved);

	return 0;
}
late_initcall_sync(rk_memblock_procfs_init);

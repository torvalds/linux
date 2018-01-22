// SPDX-License-Identifier: GPL-2.0
/*
 * Based on arch/arm/kernel/atags_proc.c
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/byteorder.h>


static char bootinfo_tmp[1536] __initdata;

static void *bootinfo_copy;
static size_t bootinfo_size;

static ssize_t bootinfo_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(buf, count, ppos, bootinfo_copy,
				       bootinfo_size);
}

static const struct file_operations bootinfo_fops = {
	.read = bootinfo_read,
	.llseek = default_llseek,
};

void __init save_bootinfo(const struct bi_record *bi)
{
	const void *start = bi;
	size_t size = sizeof(bi->tag);

	while (be16_to_cpu(bi->tag) != BI_LAST) {
		uint16_t n = be16_to_cpu(bi->size);
		size += n;
		bi = (struct bi_record *)((unsigned long)bi + n);
	}

	if (size > sizeof(bootinfo_tmp)) {
		pr_err("Cannot save %zu bytes of bootinfo\n", size);
		return;
	}

	pr_info("Saving %zu bytes of bootinfo\n", size);
	memcpy(bootinfo_tmp, start, size);
	bootinfo_size = size;
}

static int __init init_bootinfo_procfs(void)
{
	/*
	 * This cannot go into save_bootinfo() because kmalloc and proc don't
	 * work yet when it is called.
	 */
	struct proc_dir_entry *pde;

	if (!bootinfo_size)
		return -EINVAL;

	bootinfo_copy = kmemdup(bootinfo_tmp, bootinfo_size, GFP_KERNEL);
	if (!bootinfo_copy)
		return -ENOMEM;

	pde = proc_create_data("bootinfo", 0400, NULL, &bootinfo_fops, NULL);
	if (!pde) {
		kfree(bootinfo_copy);
		return -ENOMEM;
	}

	return 0;
}

arch_initcall(init_bootinfo_procfs);

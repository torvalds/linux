/*
 * Code to handle transition of Linux booting another kernel.
 *
 * Copyright (C) 2002-2003 Eric Biederman  <ebiederm@xmission.com>
 * GameCube/ppc32 port Copyright (C) 2004 Albert Herranz
 * Copyright (C) 2005 IBM Corporation.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/kexec.h>
#include <linux/reboot.h>
#include <linux/threads.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/irq.h>
#include <linux/ftrace.h>

#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/sections.h>

void machine_kexec_mask_interrupts(void) {
	unsigned int i;

	for_each_irq(i) {
		struct irq_desc *desc = irq_to_desc(i);
		struct irq_chip *chip;

		if (!desc)
			continue;

		chip = irq_desc_get_chip(desc);
		if (!chip)
			continue;

		if (chip->irq_eoi && irqd_irq_inprogress(&desc->irq_data))
			chip->irq_eoi(&desc->irq_data);

		if (chip->irq_mask)
			chip->irq_mask(&desc->irq_data);

		if (chip->irq_disable && !irqd_irq_disabled(&desc->irq_data))
			chip->irq_disable(&desc->irq_data);
	}
}

void machine_crash_shutdown(struct pt_regs *regs)
{
	default_machine_crash_shutdown(regs);
}

/*
 * Do what every setup is needed on image and the
 * reboot code buffer to allow us to avoid allocations
 * later.
 */
int machine_kexec_prepare(struct kimage *image)
{
	if (ppc_md.machine_kexec_prepare)
		return ppc_md.machine_kexec_prepare(image);
	else
		return default_machine_kexec_prepare(image);
}

void machine_kexec_cleanup(struct kimage *image)
{
}

void arch_crash_save_vmcoreinfo(void)
{

#ifdef CONFIG_NEED_MULTIPLE_NODES
	VMCOREINFO_SYMBOL(node_data);
	VMCOREINFO_LENGTH(node_data, MAX_NUMNODES);
#endif
#ifndef CONFIG_NEED_MULTIPLE_NODES
	VMCOREINFO_SYMBOL(contig_page_data);
#endif
}

/*
 * Do not allocate memory (or fail in any way) in machine_kexec().
 * We are past the point of no return, committed to rebooting now.
 */
void machine_kexec(struct kimage *image)
{
	int save_ftrace_enabled;

	save_ftrace_enabled = __ftrace_enabled_save();

	if (ppc_md.machine_kexec)
		ppc_md.machine_kexec(image);
	else
		default_machine_kexec(image);

	__ftrace_enabled_restore(save_ftrace_enabled);

	/* Fall back to normal restart if we're still alive. */
	machine_restart(NULL);
	for(;;);
}

void __init reserve_crashkernel(void)
{
	unsigned long long crash_size, crash_base;
	int ret;

	/* this is necessary because of memblock_phys_mem_size() */
	memblock_analyze();

	/* use common parsing */
	ret = parse_crashkernel(boot_command_line, memblock_phys_mem_size(),
			&crash_size, &crash_base);
	if (ret == 0 && crash_size > 0) {
		crashk_res.start = crash_base;
		crashk_res.end = crash_base + crash_size - 1;
	}

	if (crashk_res.end == crashk_res.start) {
		crashk_res.start = crashk_res.end = 0;
		return;
	}

	/* We might have got these values via the command line or the
	 * device tree, either way sanitise them now. */

	crash_size = resource_size(&crashk_res);

#ifndef CONFIG_RELOCATABLE
	if (crashk_res.start != KDUMP_KERNELBASE)
		printk("Crash kernel location must be 0x%x\n",
				KDUMP_KERNELBASE);

	crashk_res.start = KDUMP_KERNELBASE;
#else
	if (!crashk_res.start) {
		/*
		 * unspecified address, choose a region of specified size
		 * can overlap with initrd (ignoring corruption when retained)
		 * ppc64 requires kernel and some stacks to be in first segemnt
		 */
		crashk_res.start = KDUMP_KERNELBASE;
	}

	crash_base = PAGE_ALIGN(crashk_res.start);
	if (crash_base != crashk_res.start) {
		printk("Crash kernel base must be aligned to 0x%lx\n",
				PAGE_SIZE);
		crashk_res.start = crash_base;
	}

#endif
	crash_size = PAGE_ALIGN(crash_size);
	crashk_res.end = crashk_res.start + crash_size - 1;

	/* The crash region must not overlap the current kernel */
	if (overlaps_crashkernel(__pa(_stext), _end - _stext)) {
		printk(KERN_WARNING
			"Crash kernel can not overlap current kernel\n");
		crashk_res.start = crashk_res.end = 0;
		return;
	}

	/* Crash kernel trumps memory limit */
	if (memory_limit && memory_limit <= crashk_res.end) {
		memory_limit = crashk_res.end + 1;
		printk("Adjusted memory limit for crashkernel, now 0x%llx\n",
		       (unsigned long long)memory_limit);
	}

	printk(KERN_INFO "Reserving %ldMB of memory at %ldMB "
			"for crashkernel (System RAM: %ldMB)\n",
			(unsigned long)(crash_size >> 20),
			(unsigned long)(crashk_res.start >> 20),
			(unsigned long)(memblock_phys_mem_size() >> 20));

	memblock_reserve(crashk_res.start, crash_size);
}

int overlaps_crashkernel(unsigned long start, unsigned long size)
{
	return (start + size) > crashk_res.start && start <= crashk_res.end;
}

/* Values we need to export to the second kernel via the device tree. */
static phys_addr_t kernel_end;
static phys_addr_t crashk_size;

static struct property kernel_end_prop = {
	.name = "linux,kernel-end",
	.length = sizeof(phys_addr_t),
	.value = &kernel_end,
};

static struct property crashk_base_prop = {
	.name = "linux,crashkernel-base",
	.length = sizeof(phys_addr_t),
	.value = &crashk_res.start,
};

static struct property crashk_size_prop = {
	.name = "linux,crashkernel-size",
	.length = sizeof(phys_addr_t),
	.value = &crashk_size,
};

static void __init export_crashk_values(struct device_node *node)
{
	struct property *prop;

	/* There might be existing crash kernel properties, but we can't
	 * be sure what's in them, so remove them. */
	prop = of_find_property(node, "linux,crashkernel-base", NULL);
	if (prop)
		prom_remove_property(node, prop);

	prop = of_find_property(node, "linux,crashkernel-size", NULL);
	if (prop)
		prom_remove_property(node, prop);

	if (crashk_res.start != 0) {
		prom_add_property(node, &crashk_base_prop);
		crashk_size = resource_size(&crashk_res);
		prom_add_property(node, &crashk_size_prop);
	}
}

static int __init kexec_setup(void)
{
	struct device_node *node;
	struct property *prop;

	node = of_find_node_by_path("/chosen");
	if (!node)
		return -ENOENT;

	/* remove any stale properties so ours can be found */
	prop = of_find_property(node, kernel_end_prop.name, NULL);
	if (prop)
		prom_remove_property(node, prop);

	/* information needed by userspace when using default_machine_kexec */
	kernel_end = __pa(_end);
	prom_add_property(node, &kernel_end_prop);

	export_crashk_values(node);

	of_node_put(node);
	return 0;
}
late_initcall(kexec_setup);

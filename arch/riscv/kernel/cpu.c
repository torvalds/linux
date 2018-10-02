/*
 * Copyright (C) 2012 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <asm/smp.h>

/*
 * Returns the hart ID of the given device tree node, or -1 if the device tree
 * node isn't a RISC-V hart.
 */
int riscv_of_processor_hartid(struct device_node *node)
{
	const char *isa, *status;
	u32 hart;

	if (!of_device_is_compatible(node, "riscv")) {
		pr_warn("Found incompatible CPU\n");
		return -(ENODEV);
	}

	if (of_property_read_u32(node, "reg", &hart)) {
		pr_warn("Found CPU without hart ID\n");
		return -(ENODEV);
	}
	if (hart >= NR_CPUS) {
		pr_info("Found hart ID %d, which is above NR_CPUs.  Disabling this hart\n", hart);
		return -(ENODEV);
	}

	if (of_property_read_string(node, "status", &status)) {
		pr_warn("CPU with hartid=%d has no \"status\" property\n", hart);
		return -(ENODEV);
	}
	if (strcmp(status, "okay")) {
		pr_info("CPU with hartid=%d has a non-okay status of \"%s\"\n", hart, status);
		return -(ENODEV);
	}

	if (of_property_read_string(node, "riscv,isa", &isa)) {
		pr_warn("CPU with hartid=%d has no \"riscv,isa\" property\n", hart);
		return -(ENODEV);
	}
	if (isa[0] != 'r' || isa[1] != 'v') {
		pr_warn("CPU with hartid=%d has an invalid ISA of \"%s\"\n", hart, isa);
		return -(ENODEV);
	}

	return hart;
}

#ifdef CONFIG_PROC_FS

static void print_isa(struct seq_file *f, const char *orig_isa)
{
	static const char *ext = "mafdc";
	const char *isa = orig_isa;
	const char *e;

	/*
	 * Linux doesn't support rv32e or rv128i, and we only support booting
	 * kernels on harts with the same ISA that the kernel is compiled for.
	 */
#if defined(CONFIG_32BIT)
	if (strncmp(isa, "rv32i", 5) != 0)
		return;
#elif defined(CONFIG_64BIT)
	if (strncmp(isa, "rv64i", 5) != 0)
		return;
#endif

	/* Print the base ISA, as we already know it's legal. */
	seq_puts(f, "isa\t\t: ");
	seq_write(f, isa, 5);
	isa += 5;

	/*
	 * Check the rest of the ISA string for valid extensions, printing those
	 * we find.  RISC-V ISA strings define an order, so we only print the
	 * extension bits when they're in order.
	 */
	for (e = ext; *e != '\0'; ++e) {
		if (isa[0] == e[0]) {
			seq_write(f, isa, 1);
			isa++;
		}
	}
	seq_puts(f, "\n");

	/*
	 * If we were given an unsupported ISA in the device tree then print
	 * a bit of info describing what went wrong.
	 */
	if (isa[0] != '\0')
		pr_info("unsupported ISA \"%s\" in device tree", orig_isa);
}

static void print_mmu(struct seq_file *f, const char *mmu_type)
{
#if defined(CONFIG_32BIT)
	if (strcmp(mmu_type, "riscv,sv32") != 0)
		return;
#elif defined(CONFIG_64BIT)
	if (strcmp(mmu_type, "riscv,sv39") != 0 &&
	    strcmp(mmu_type, "riscv,sv48") != 0)
		return;
#endif

	seq_printf(f, "mmu\t\t: %s\n", mmu_type+6);
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	*pos = cpumask_next(*pos - 1, cpu_online_mask);
	if ((*pos) < nr_cpu_ids)
		return (void *)(uintptr_t)(1 + *pos);
	return NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

static int c_show(struct seq_file *m, void *v)
{
	unsigned long cpu_id = (unsigned long)v - 1;
	struct device_node *node = of_get_cpu_node(cpuid_to_hartid_map(cpu_id),
						   NULL);
	const char *compat, *isa, *mmu;

	seq_printf(m, "processor\t: %lu\n", cpu_id);
	seq_printf(m, "hart\t\t: %lu\n", cpuid_to_hartid_map(cpu_id));
	if (!of_property_read_string(node, "riscv,isa", &isa))
		print_isa(m, isa);
	if (!of_property_read_string(node, "mmu-type", &mmu))
		print_mmu(m, mmu);
	if (!of_property_read_string(node, "compatible", &compat)
	    && strcmp(compat, "riscv"))
		seq_printf(m, "uarch\t\t: %s\n", compat);
	seq_puts(m, "\n");

	return 0;
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_show
};

#endif /* CONFIG_PROC_FS */

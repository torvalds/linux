// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <asm/hwcap.h>
#include <asm/smp.h>
#include <asm/pgtable.h>

/*
 * Returns the hart ID of the given device tree node, or -ENODEV if the node
 * isn't an enabled and valid RISC-V hart node.
 */
int riscv_of_processor_hartid(struct device_node *node, unsigned long *hart)
{
	const char *isa;

	if (!of_device_is_compatible(node, "riscv")) {
		pr_warn("Found incompatible CPU\n");
		return -ENODEV;
	}

	*hart = (unsigned long) of_get_cpu_hwid(node, 0);
	if (*hart == ~0UL) {
		pr_warn("Found CPU without hart ID\n");
		return -ENODEV;
	}

	if (!of_device_is_available(node)) {
		pr_info("CPU with hartid=%lu is not available\n", *hart);
		return -ENODEV;
	}

	if (of_property_read_string(node, "riscv,isa", &isa)) {
		pr_warn("CPU with hartid=%lu has no \"riscv,isa\" property\n", *hart);
		return -ENODEV;
	}
	if (isa[0] != 'r' || isa[1] != 'v') {
		pr_warn("CPU with hartid=%lu has an invalid ISA of \"%s\"\n", *hart, isa);
		return -ENODEV;
	}

	return 0;
}

/*
 * Find hart ID of the CPU DT node under which given DT node falls.
 *
 * To achieve this, we walk up the DT tree until we find an active
 * RISC-V core (HART) node and extract the cpuid from it.
 */
int riscv_of_parent_hartid(struct device_node *node, unsigned long *hartid)
{
	int rc;

	for (; node; node = node->parent) {
		if (of_device_is_compatible(node, "riscv")) {
			rc = riscv_of_processor_hartid(node, hartid);
			if (!rc)
				return 0;
		}
	}

	return -1;
}

#ifdef CONFIG_PROC_FS
#define __RISCV_ISA_EXT_DATA(UPROP, EXTID) \
	{							\
		.uprop = #UPROP,				\
		.isa_ext_id = EXTID,				\
	}
/*
 * Here are the ordering rules of extension naming defined by RISC-V
 * specification :
 * 1. All extensions should be separated from other multi-letter extensions
 *    by an underscore.
 * 2. The first letter following the 'Z' conventionally indicates the most
 *    closely related alphabetical extension category, IMAFDQLCBKJTPVH.
 *    If multiple 'Z' extensions are named, they should be ordered first
 *    by category, then alphabetically within a category.
 * 3. Standard supervisor-level extensions (starts with 'S') should be
 *    listed after standard unprivileged extensions.  If multiple
 *    supervisor-level extensions are listed, they should be ordered
 *    alphabetically.
 * 4. Non-standard extensions (starts with 'X') must be listed after all
 *    standard extensions. They must be separated from other multi-letter
 *    extensions by an underscore.
 */
static struct riscv_isa_ext_data isa_ext_arr[] = {
	__RISCV_ISA_EXT_DATA(sscofpmf, RISCV_ISA_EXT_SSCOFPMF),
	__RISCV_ISA_EXT_DATA(sstc, RISCV_ISA_EXT_SSTC),
	__RISCV_ISA_EXT_DATA(svinval, RISCV_ISA_EXT_SVINVAL),
	__RISCV_ISA_EXT_DATA(svpbmt, RISCV_ISA_EXT_SVPBMT),
	__RISCV_ISA_EXT_DATA(zicbom, RISCV_ISA_EXT_ZICBOM),
	__RISCV_ISA_EXT_DATA(zihintpause, RISCV_ISA_EXT_ZIHINTPAUSE),
	__RISCV_ISA_EXT_DATA("", RISCV_ISA_EXT_MAX),
};

static void print_isa_ext(struct seq_file *f)
{
	struct riscv_isa_ext_data *edata;
	int i = 0, arr_sz;

	arr_sz = ARRAY_SIZE(isa_ext_arr) - 1;

	/* No extension support available */
	if (arr_sz <= 0)
		return;

	for (i = 0; i <= arr_sz; i++) {
		edata = &isa_ext_arr[i];
		if (!__riscv_isa_extension_available(NULL, edata->isa_ext_id))
			continue;
		seq_printf(f, "_%s", edata->uprop);
	}
}

/*
 * These are the only valid base (single letter) ISA extensions as per the spec.
 * It also specifies the canonical order in which it appears in the spec.
 * Some of the extension may just be a place holder for now (B, K, P, J).
 * This should be updated once corresponding extensions are ratified.
 */
static const char base_riscv_exts[13] = "imafdqcbkjpvh";

static void print_isa(struct seq_file *f, const char *isa)
{
	int i;

	seq_puts(f, "isa\t\t: ");
	/* Print the rv[64/32] part */
	seq_write(f, isa, 4);
	for (i = 0; i < sizeof(base_riscv_exts); i++) {
		if (__riscv_isa_extension_available(NULL, base_riscv_exts[i] - 'a'))
			/* Print only enabled the base ISA extensions */
			seq_write(f, &base_riscv_exts[i], 1);
	}
	print_isa_ext(f);
	seq_puts(f, "\n");
}

static void print_mmu(struct seq_file *f)
{
	char sv_type[16];

#ifdef CONFIG_MMU
#if defined(CONFIG_32BIT)
	strncpy(sv_type, "sv32", 5);
#elif defined(CONFIG_64BIT)
	if (pgtable_l5_enabled)
		strncpy(sv_type, "sv57", 5);
	else if (pgtable_l4_enabled)
		strncpy(sv_type, "sv48", 5);
	else
		strncpy(sv_type, "sv39", 5);
#endif
#else
	strncpy(sv_type, "none", 5);
#endif /* CONFIG_MMU */
	seq_printf(f, "mmu\t\t: %s\n", sv_type);
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
	struct device_node *node = of_get_cpu_node(cpu_id, NULL);
	const char *compat, *isa;

	seq_printf(m, "processor\t: %lu\n", cpu_id);
	seq_printf(m, "hart\t\t: %lu\n", cpuid_to_hartid_map(cpu_id));
	if (!of_property_read_string(node, "riscv,isa", &isa))
		print_isa(m, isa);
	print_mmu(m);
	if (!of_property_read_string(node, "compatible", &compat)
	    && strcmp(compat, "riscv"))
		seq_printf(m, "uarch\t\t: %s\n", compat);
	seq_puts(m, "\n");
	of_node_put(node);

	return 0;
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_show
};

#endif /* CONFIG_PROC_FS */

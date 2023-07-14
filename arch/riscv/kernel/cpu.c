// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <linux/acpi.h>
#include <linux/cpu.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <asm/acpi.h>
#include <asm/cpufeature.h>
#include <asm/csr.h>
#include <asm/hwcap.h>
#include <asm/sbi.h>
#include <asm/smp.h>
#include <asm/pgtable.h>

/*
 * Returns the hart ID of the given device tree node, or -ENODEV if the node
 * isn't an enabled and valid RISC-V hart node.
 */
int riscv_of_processor_hartid(struct device_node *node, unsigned long *hart)
{
	int cpu;

	*hart = (unsigned long)of_get_cpu_hwid(node, 0);
	if (*hart == ~0UL) {
		pr_warn("Found CPU without hart ID\n");
		return -ENODEV;
	}

	cpu = riscv_hartid_to_cpuid(*hart);
	if (cpu < 0)
		return cpu;

	if (!cpu_possible(cpu))
		return -ENODEV;

	return 0;
}

int riscv_early_of_processor_hartid(struct device_node *node, unsigned long *hart)
{
	const char *isa;

	if (!of_device_is_compatible(node, "riscv")) {
		pr_warn("Found incompatible CPU\n");
		return -ENODEV;
	}

	*hart = (unsigned long)of_get_cpu_hwid(node, 0);
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

	if (IS_ENABLED(CONFIG_32BIT) && strncasecmp(isa, "rv32ima", 7))
		return -ENODEV;

	if (IS_ENABLED(CONFIG_64BIT) && strncasecmp(isa, "rv64ima", 7))
		return -ENODEV;

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

DEFINE_PER_CPU(struct riscv_cpuinfo, riscv_cpuinfo);

unsigned long riscv_cached_mvendorid(unsigned int cpu_id)
{
	struct riscv_cpuinfo *ci = per_cpu_ptr(&riscv_cpuinfo, cpu_id);

	return ci->mvendorid;
}
EXPORT_SYMBOL(riscv_cached_mvendorid);

unsigned long riscv_cached_marchid(unsigned int cpu_id)
{
	struct riscv_cpuinfo *ci = per_cpu_ptr(&riscv_cpuinfo, cpu_id);

	return ci->marchid;
}
EXPORT_SYMBOL(riscv_cached_marchid);

unsigned long riscv_cached_mimpid(unsigned int cpu_id)
{
	struct riscv_cpuinfo *ci = per_cpu_ptr(&riscv_cpuinfo, cpu_id);

	return ci->mimpid;
}
EXPORT_SYMBOL(riscv_cached_mimpid);

static int riscv_cpuinfo_starting(unsigned int cpu)
{
	struct riscv_cpuinfo *ci = this_cpu_ptr(&riscv_cpuinfo);

#if IS_ENABLED(CONFIG_RISCV_SBI)
	ci->mvendorid = sbi_spec_is_0_1() ? 0 : sbi_get_mvendorid();
	ci->marchid = sbi_spec_is_0_1() ? 0 : sbi_get_marchid();
	ci->mimpid = sbi_spec_is_0_1() ? 0 : sbi_get_mimpid();
#elif IS_ENABLED(CONFIG_RISCV_M_MODE)
	ci->mvendorid = csr_read(CSR_MVENDORID);
	ci->marchid = csr_read(CSR_MARCHID);
	ci->mimpid = csr_read(CSR_MIMPID);
#else
	ci->mvendorid = 0;
	ci->marchid = 0;
	ci->mimpid = 0;
#endif

	return 0;
}

static int __init riscv_cpuinfo_init(void)
{
	int ret;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "riscv/cpuinfo:starting",
				riscv_cpuinfo_starting, NULL);
	if (ret < 0) {
		pr_err("cpuinfo: failed to register hotplug callbacks.\n");
		return ret;
	}

	return 0;
}
arch_initcall(riscv_cpuinfo_init);

#ifdef CONFIG_PROC_FS

#define __RISCV_ISA_EXT_DATA(UPROP, EXTID) \
	{							\
		.uprop = #UPROP,				\
		.isa_ext_id = EXTID,				\
	}

/*
 * The canonical order of ISA extension names in the ISA string is defined in
 * chapter 27 of the unprivileged specification.
 *
 * Ordinarily, for in-kernel data structures, this order is unimportant but
 * isa_ext_arr defines the order of the ISA string in /proc/cpuinfo.
 *
 * The specification uses vague wording, such as should, when it comes to
 * ordering, so for our purposes the following rules apply:
 *
 * 1. All multi-letter extensions must be separated from other extensions by an
 *    underscore.
 *
 * 2. Additional standard extensions (starting with 'Z') must be sorted after
 *    single-letter extensions and before any higher-privileged extensions.

 * 3. The first letter following the 'Z' conventionally indicates the most
 *    closely related alphabetical extension category, IMAFDQLCBKJTPVH.
 *    If multiple 'Z' extensions are named, they must be ordered first by
 *    category, then alphabetically within a category.
 *
 * 3. Standard supervisor-level extensions (starting with 'S') must be listed
 *    after standard unprivileged extensions.  If multiple supervisor-level
 *    extensions are listed, they must be ordered alphabetically.
 *
 * 4. Standard machine-level extensions (starting with 'Zxm') must be listed
 *    after any lower-privileged, standard extensions.  If multiple
 *    machine-level extensions are listed, they must be ordered
 *    alphabetically.
 *
 * 5. Non-standard extensions (starting with 'X') must be listed after all
 *    standard extensions. If multiple non-standard extensions are listed, they
 *    must be ordered alphabetically.
 *
 * An example string following the order is:
 *    rv64imadc_zifoo_zigoo_zafoo_sbar_scar_zxmbaz_xqux_xrux
 *
 * New entries to this struct should follow the ordering rules described above.
 */
static struct riscv_isa_ext_data isa_ext_arr[] = {
	__RISCV_ISA_EXT_DATA(zicbom, RISCV_ISA_EXT_ZICBOM),
	__RISCV_ISA_EXT_DATA(zicboz, RISCV_ISA_EXT_ZICBOZ),
	__RISCV_ISA_EXT_DATA(zicntr, RISCV_ISA_EXT_ZICNTR),
	__RISCV_ISA_EXT_DATA(zicsr, RISCV_ISA_EXT_ZICSR),
	__RISCV_ISA_EXT_DATA(zifencei, RISCV_ISA_EXT_ZIFENCEI),
	__RISCV_ISA_EXT_DATA(zihintpause, RISCV_ISA_EXT_ZIHINTPAUSE),
	__RISCV_ISA_EXT_DATA(zihpm, RISCV_ISA_EXT_ZIHPM),
	__RISCV_ISA_EXT_DATA(zba, RISCV_ISA_EXT_ZBA),
	__RISCV_ISA_EXT_DATA(zbb, RISCV_ISA_EXT_ZBB),
	__RISCV_ISA_EXT_DATA(zbs, RISCV_ISA_EXT_ZBS),
	__RISCV_ISA_EXT_DATA(smaia, RISCV_ISA_EXT_SMAIA),
	__RISCV_ISA_EXT_DATA(ssaia, RISCV_ISA_EXT_SSAIA),
	__RISCV_ISA_EXT_DATA(sscofpmf, RISCV_ISA_EXT_SSCOFPMF),
	__RISCV_ISA_EXT_DATA(sstc, RISCV_ISA_EXT_SSTC),
	__RISCV_ISA_EXT_DATA(svinval, RISCV_ISA_EXT_SVINVAL),
	__RISCV_ISA_EXT_DATA(svnapot, RISCV_ISA_EXT_SVNAPOT),
	__RISCV_ISA_EXT_DATA(svpbmt, RISCV_ISA_EXT_SVPBMT),
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
	if (*pos == nr_cpu_ids)
		return NULL;

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
	struct riscv_cpuinfo *ci = per_cpu_ptr(&riscv_cpuinfo, cpu_id);
	struct device_node *node;
	const char *compat, *isa;

	seq_printf(m, "processor\t: %lu\n", cpu_id);
	seq_printf(m, "hart\t\t: %lu\n", cpuid_to_hartid_map(cpu_id));

	if (acpi_disabled) {
		node = of_get_cpu_node(cpu_id, NULL);
		if (!of_property_read_string(node, "riscv,isa", &isa))
			print_isa(m, isa);

		print_mmu(m);
		if (!of_property_read_string(node, "compatible", &compat) &&
		    strcmp(compat, "riscv"))
			seq_printf(m, "uarch\t\t: %s\n", compat);

		of_node_put(node);
	} else {
		if (!acpi_get_riscv_isa(NULL, cpu_id, &isa))
			print_isa(m, isa);

		print_mmu(m);
	}

	seq_printf(m, "mvendorid\t: 0x%lx\n", ci->mvendorid);
	seq_printf(m, "marchid\t\t: 0x%lx\n", ci->marchid);
	seq_printf(m, "mimpid\t\t: 0x%lx\n", ci->mimpid);
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

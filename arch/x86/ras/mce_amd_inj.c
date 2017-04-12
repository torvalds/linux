/*
 * A simple MCE injection facility for testing different aspects of the RAS
 * code. This driver should be built as module so that it can be loaded
 * on production kernels for testing purposes.
 *
 * This file may be distributed under the terms of the GNU General Public
 * License version 2.
 *
 * Copyright (c) 2010-15:  Borislav Petkov <bp@alien8.de>
 *			Advanced Micro Devices Inc.
 */

#include <linux/kobject.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/pci.h>

#include <asm/mce.h>
#include <asm/smp.h>
#include <asm/amd_nb.h>
#include <asm/irq_vectors.h>

#include "../kernel/cpu/mcheck/mce-internal.h"

/*
 * Collect all the MCi_XXX settings
 */
static struct mce i_mce;
static struct dentry *dfs_inj;

static u8 n_banks;

#define MAX_FLAG_OPT_SIZE	3
#define NBCFG			0x44

enum injection_type {
	SW_INJ = 0,	/* SW injection, simply decode the error */
	HW_INJ,		/* Trigger a #MC */
	DFR_INT_INJ,    /* Trigger Deferred error interrupt */
	THR_INT_INJ,    /* Trigger threshold interrupt */
	N_INJ_TYPES,
};

static const char * const flags_options[] = {
	[SW_INJ] = "sw",
	[HW_INJ] = "hw",
	[DFR_INT_INJ] = "df",
	[THR_INT_INJ] = "th",
	NULL
};

/* Set default injection to SW_INJ */
static enum injection_type inj_type = SW_INJ;

#define MCE_INJECT_SET(reg)						\
static int inj_##reg##_set(void *data, u64 val)				\
{									\
	struct mce *m = (struct mce *)data;				\
									\
	m->reg = val;							\
	return 0;							\
}

MCE_INJECT_SET(status);
MCE_INJECT_SET(misc);
MCE_INJECT_SET(addr);
MCE_INJECT_SET(synd);

#define MCE_INJECT_GET(reg)						\
static int inj_##reg##_get(void *data, u64 *val)			\
{									\
	struct mce *m = (struct mce *)data;				\
									\
	*val = m->reg;							\
	return 0;							\
}

MCE_INJECT_GET(status);
MCE_INJECT_GET(misc);
MCE_INJECT_GET(addr);
MCE_INJECT_GET(synd);

DEFINE_SIMPLE_ATTRIBUTE(status_fops, inj_status_get, inj_status_set, "%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(misc_fops, inj_misc_get, inj_misc_set, "%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(addr_fops, inj_addr_get, inj_addr_set, "%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(synd_fops, inj_synd_get, inj_synd_set, "%llx\n");

/*
 * Caller needs to be make sure this cpu doesn't disappear
 * from under us, i.e.: get_cpu/put_cpu.
 */
static int toggle_hw_mce_inject(unsigned int cpu, bool enable)
{
	u32 l, h;
	int err;

	err = rdmsr_on_cpu(cpu, MSR_K7_HWCR, &l, &h);
	if (err) {
		pr_err("%s: error reading HWCR\n", __func__);
		return err;
	}

	enable ? (l |= BIT(18)) : (l &= ~BIT(18));

	err = wrmsr_on_cpu(cpu, MSR_K7_HWCR, l, h);
	if (err)
		pr_err("%s: error writing HWCR\n", __func__);

	return err;
}

static int __set_inj(const char *buf)
{
	int i;

	for (i = 0; i < N_INJ_TYPES; i++) {
		if (!strncmp(flags_options[i], buf, strlen(flags_options[i]))) {
			inj_type = i;
			return 0;
		}
	}
	return -EINVAL;
}

static ssize_t flags_read(struct file *filp, char __user *ubuf,
			  size_t cnt, loff_t *ppos)
{
	char buf[MAX_FLAG_OPT_SIZE];
	int n;

	n = sprintf(buf, "%s\n", flags_options[inj_type]);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, n);
}

static ssize_t flags_write(struct file *filp, const char __user *ubuf,
			   size_t cnt, loff_t *ppos)
{
	char buf[MAX_FLAG_OPT_SIZE], *__buf;
	int err;

	if (cnt > MAX_FLAG_OPT_SIZE)
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt - 1] = 0;

	/* strip whitespace */
	__buf = strstrip(buf);

	err = __set_inj(__buf);
	if (err) {
		pr_err("%s: Invalid flags value: %s\n", __func__, __buf);
		return err;
	}

	*ppos += cnt;

	return cnt;
}

static const struct file_operations flags_fops = {
	.read           = flags_read,
	.write          = flags_write,
	.llseek         = generic_file_llseek,
};

/*
 * On which CPU to inject?
 */
MCE_INJECT_GET(extcpu);

static int inj_extcpu_set(void *data, u64 val)
{
	struct mce *m = (struct mce *)data;

	if (val >= nr_cpu_ids || !cpu_online(val)) {
		pr_err("%s: Invalid CPU: %llu\n", __func__, val);
		return -EINVAL;
	}
	m->extcpu = val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(extcpu_fops, inj_extcpu_get, inj_extcpu_set, "%llu\n");

static void trigger_mce(void *info)
{
	asm volatile("int $18");
}

static void trigger_dfr_int(void *info)
{
	asm volatile("int %0" :: "i" (DEFERRED_ERROR_VECTOR));
}

static void trigger_thr_int(void *info)
{
	asm volatile("int %0" :: "i" (THRESHOLD_APIC_VECTOR));
}

static u32 get_nbc_for_node(int node_id)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;
	u32 cores_per_node;

	cores_per_node = (c->x86_max_cores * smp_num_siblings) / amd_get_nodes_per_socket();

	return cores_per_node * node_id;
}

static void toggle_nb_mca_mst_cpu(u16 nid)
{
	struct pci_dev *F3 = node_to_amd_nb(nid)->misc;
	u32 val;
	int err;

	if (!F3)
		return;

	err = pci_read_config_dword(F3, NBCFG, &val);
	if (err) {
		pr_err("%s: Error reading F%dx%03x.\n",
		       __func__, PCI_FUNC(F3->devfn), NBCFG);
		return;
	}

	if (val & BIT(27))
		return;

	pr_err("%s: Set D18F3x44[NbMcaToMstCpuEn] which BIOS hasn't done.\n",
	       __func__);

	val |= BIT(27);
	err = pci_write_config_dword(F3, NBCFG, val);
	if (err)
		pr_err("%s: Error writing F%dx%03x.\n",
		       __func__, PCI_FUNC(F3->devfn), NBCFG);
}

static void prepare_msrs(void *info)
{
	struct mce m = *(struct mce *)info;
	u8 b = m.bank;

	wrmsrl(MSR_IA32_MCG_STATUS, m.mcgstatus);

	if (boot_cpu_has(X86_FEATURE_SMCA)) {
		if (m.inject_flags == DFR_INT_INJ) {
			wrmsrl(MSR_AMD64_SMCA_MCx_DESTAT(b), m.status);
			wrmsrl(MSR_AMD64_SMCA_MCx_DEADDR(b), m.addr);
		} else {
			wrmsrl(MSR_AMD64_SMCA_MCx_STATUS(b), m.status);
			wrmsrl(MSR_AMD64_SMCA_MCx_ADDR(b), m.addr);
		}

		wrmsrl(MSR_AMD64_SMCA_MCx_MISC(b), m.misc);
		wrmsrl(MSR_AMD64_SMCA_MCx_SYND(b), m.synd);
	} else {
		wrmsrl(MSR_IA32_MCx_STATUS(b), m.status);
		wrmsrl(MSR_IA32_MCx_ADDR(b), m.addr);
		wrmsrl(MSR_IA32_MCx_MISC(b), m.misc);
	}
}

static void do_inject(void)
{
	u64 mcg_status = 0;
	unsigned int cpu = i_mce.extcpu;
	u8 b = i_mce.bank;

	rdtscll(i_mce.tsc);

	if (i_mce.misc)
		i_mce.status |= MCI_STATUS_MISCV;

	if (i_mce.synd)
		i_mce.status |= MCI_STATUS_SYNDV;

	if (inj_type == SW_INJ) {
		mce_inject_log(&i_mce);
		return;
	}

	/* prep MCE global settings for the injection */
	mcg_status = MCG_STATUS_MCIP | MCG_STATUS_EIPV;

	if (!(i_mce.status & MCI_STATUS_PCC))
		mcg_status |= MCG_STATUS_RIPV;

	/*
	 * Ensure necessary status bits for deferred errors:
	 * - MCx_STATUS[Deferred]: make sure it is a deferred error
	 * - MCx_STATUS[UC] cleared: deferred errors are _not_ UC
	 */
	if (inj_type == DFR_INT_INJ) {
		i_mce.status |= MCI_STATUS_DEFERRED;
		i_mce.status |= (i_mce.status & ~MCI_STATUS_UC);
	}

	/*
	 * For multi node CPUs, logging and reporting of bank 4 errors happens
	 * only on the node base core. Refer to D18F3x44[NbMcaToMstCpuEn] for
	 * Fam10h and later BKDGs.
	 */
	if (static_cpu_has(X86_FEATURE_AMD_DCM) &&
	    b == 4 &&
	    boot_cpu_data.x86 < 0x17) {
		toggle_nb_mca_mst_cpu(amd_get_nb_id(cpu));
		cpu = get_nbc_for_node(amd_get_nb_id(cpu));
	}

	get_online_cpus();
	if (!cpu_online(cpu))
		goto err;

	toggle_hw_mce_inject(cpu, true);

	i_mce.mcgstatus = mcg_status;
	i_mce.inject_flags = inj_type;
	smp_call_function_single(cpu, prepare_msrs, &i_mce, 0);

	toggle_hw_mce_inject(cpu, false);

	switch (inj_type) {
	case DFR_INT_INJ:
		smp_call_function_single(cpu, trigger_dfr_int, NULL, 0);
		break;
	case THR_INT_INJ:
		smp_call_function_single(cpu, trigger_thr_int, NULL, 0);
		break;
	default:
		smp_call_function_single(cpu, trigger_mce, NULL, 0);
	}

err:
	put_online_cpus();

}

/*
 * This denotes into which bank we're injecting and triggers
 * the injection, at the same time.
 */
static int inj_bank_set(void *data, u64 val)
{
	struct mce *m = (struct mce *)data;

	if (val >= n_banks) {
		pr_err("Non-existent MCE bank: %llu\n", val);
		return -EINVAL;
	}

	m->bank = val;
	do_inject();

	return 0;
}

MCE_INJECT_GET(bank);

DEFINE_SIMPLE_ATTRIBUTE(bank_fops, inj_bank_get, inj_bank_set, "%llu\n");

static const char readme_msg[] =
"Description of the files and their usages:\n"
"\n"
"Note1: i refers to the bank number below.\n"
"Note2: See respective BKDGs for the exact bit definitions of the files below\n"
"as they mirror the hardware registers.\n"
"\n"
"status:\t Set MCi_STATUS: the bits in that MSR control the error type and\n"
"\t attributes of the error which caused the MCE.\n"
"\n"
"misc:\t Set MCi_MISC: provide auxiliary info about the error. It is mostly\n"
"\t used for error thresholding purposes and its validity is indicated by\n"
"\t MCi_STATUS[MiscV].\n"
"\n"
"synd:\t Set MCi_SYND: provide syndrome info about the error. Only valid on\n"
"\t Scalable MCA systems, and its validity is indicated by MCi_STATUS[SyndV].\n"
"\n"
"addr:\t Error address value to be written to MCi_ADDR. Log address information\n"
"\t associated with the error.\n"
"\n"
"cpu:\t The CPU to inject the error on.\n"
"\n"
"bank:\t Specify the bank you want to inject the error into: the number of\n"
"\t banks in a processor varies and is family/model-specific, therefore, the\n"
"\t supplied value is sanity-checked. Setting the bank value also triggers the\n"
"\t injection.\n"
"\n"
"flags:\t Injection type to be performed. Writing to this file will trigger a\n"
"\t real machine check, an APIC interrupt or invoke the error decoder routines\n"
"\t for AMD processors.\n"
"\n"
"\t Allowed error injection types:\n"
"\t  - \"sw\": Software error injection. Decode error to a human-readable \n"
"\t    format only. Safe to use.\n"
"\t  - \"hw\": Hardware error injection. Causes the #MC exception handler to \n"
"\t    handle the error. Be warned: might cause system panic if MCi_STATUS[PCC] \n"
"\t    is set. Therefore, consider setting (debugfs_mountpoint)/mce/fake_panic \n"
"\t    before injecting.\n"
"\t  - \"df\": Trigger APIC interrupt for Deferred error. Causes deferred \n"
"\t    error APIC interrupt handler to handle the error if the feature is \n"
"\t    is present in hardware. \n"
"\t  - \"th\": Trigger APIC interrupt for Threshold errors. Causes threshold \n"
"\t    APIC interrupt handler to handle the error. \n"
"\n";

static ssize_t
inj_readme_read(struct file *filp, char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	return simple_read_from_buffer(ubuf, cnt, ppos,
					readme_msg, strlen(readme_msg));
}

static const struct file_operations readme_fops = {
	.read		= inj_readme_read,
};

static struct dfs_node {
	char *name;
	struct dentry *d;
	const struct file_operations *fops;
	umode_t perm;
} dfs_fls[] = {
	{ .name = "status",	.fops = &status_fops, .perm = S_IRUSR | S_IWUSR },
	{ .name = "misc",	.fops = &misc_fops,   .perm = S_IRUSR | S_IWUSR },
	{ .name = "addr",	.fops = &addr_fops,   .perm = S_IRUSR | S_IWUSR },
	{ .name = "synd",	.fops = &synd_fops,   .perm = S_IRUSR | S_IWUSR },
	{ .name = "bank",	.fops = &bank_fops,   .perm = S_IRUSR | S_IWUSR },
	{ .name = "flags",	.fops = &flags_fops,  .perm = S_IRUSR | S_IWUSR },
	{ .name = "cpu",	.fops = &extcpu_fops, .perm = S_IRUSR | S_IWUSR },
	{ .name = "README",	.fops = &readme_fops, .perm = S_IRUSR | S_IRGRP | S_IROTH },
};

static int __init init_mce_inject(void)
{
	unsigned int i;
	u64 cap;

	rdmsrl(MSR_IA32_MCG_CAP, cap);
	n_banks = cap & MCG_BANKCNT_MASK;

	dfs_inj = debugfs_create_dir("mce-inject", NULL);
	if (!dfs_inj)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(dfs_fls); i++) {
		dfs_fls[i].d = debugfs_create_file(dfs_fls[i].name,
						    dfs_fls[i].perm,
						    dfs_inj,
						    &i_mce,
						    dfs_fls[i].fops);

		if (!dfs_fls[i].d)
			goto err_dfs_add;
	}

	return 0;

err_dfs_add:
	while (i-- > 0)
		debugfs_remove(dfs_fls[i].d);

	debugfs_remove(dfs_inj);
	dfs_inj = NULL;

	return -ENODEV;
}

static void __exit exit_mce_inject(void)
{

	debugfs_remove_recursive(dfs_inj);
	dfs_inj = NULL;

	memset(&dfs_fls, 0, sizeof(dfs_fls));
}
module_init(init_mce_inject);
module_exit(exit_mce_inject);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Borislav Petkov <bp@alien8.de>");
MODULE_AUTHOR("AMD Inc.");
MODULE_DESCRIPTION("MCE injection facility for RAS testing");

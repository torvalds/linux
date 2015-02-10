/*
 * A simple MCE injection facility for testing different aspects of the RAS
 * code. This driver should be built as module so that it can be loaded
 * on production kernels for testing purposes.
 *
 * This file may be distributed under the terms of the GNU General Public
 * License version 2.
 *
 * Copyright (c) 2010-14:  Borislav Petkov <bp@alien8.de>
 *			Advanced Micro Devices Inc.
 */

#include <linux/kobject.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <asm/mce.h>

#include "mce_amd.h"

/*
 * Collect all the MCi_XXX settings
 */
static struct mce i_mce;
static struct dentry *dfs_inj;

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

DEFINE_SIMPLE_ATTRIBUTE(status_fops, inj_status_get, inj_status_set, "%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(misc_fops, inj_misc_get, inj_misc_set, "%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(addr_fops, inj_addr_get, inj_addr_set, "%llx\n");

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

static int flags_get(void *data, u64 *val)
{
	struct mce *m = (struct mce *)data;

	*val = m->inject_flags;

	return 0;
}

static int flags_set(void *data, u64 val)
{
	struct mce *m = (struct mce *)data;

	m->inject_flags = (u8)val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(flags_fops, flags_get, flags_set, "%llu\n");

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

static void do_inject(void)
{
	u64 mcg_status = 0;
	unsigned int cpu = i_mce.extcpu;
	u8 b = i_mce.bank;

	if (!(i_mce.inject_flags & MCJ_EXCEPTION)) {
		amd_decode_mce(NULL, 0, &i_mce);
		return;
	}

	get_online_cpus();
	if (!cpu_online(cpu))
		goto err;

	/* prep MCE global settings for the injection */
	mcg_status = MCG_STATUS_MCIP | MCG_STATUS_EIPV;

	if (!(i_mce.status & MCI_STATUS_PCC))
		mcg_status |= MCG_STATUS_RIPV;

	toggle_hw_mce_inject(cpu, true);

	wrmsr_on_cpu(cpu, MSR_IA32_MCG_STATUS,
		     (u32)mcg_status, (u32)(mcg_status >> 32));

	wrmsr_on_cpu(cpu, MSR_IA32_MCx_STATUS(b),
		     (u32)i_mce.status, (u32)(i_mce.status >> 32));

	wrmsr_on_cpu(cpu, MSR_IA32_MCx_ADDR(b),
		     (u32)i_mce.addr, (u32)(i_mce.addr >> 32));

	wrmsr_on_cpu(cpu, MSR_IA32_MCx_MISC(b),
		     (u32)i_mce.misc, (u32)(i_mce.misc >> 32));

	toggle_hw_mce_inject(cpu, false);

	smp_call_function_single(cpu, trigger_mce, NULL, 0);

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

	if (val > 5) {
		if (boot_cpu_data.x86 != 0x15 || val > 6) {
			pr_err("Non-existent MCE bank: %llu\n", val);
			return -EINVAL;
		}
	}

	m->bank = val;
	do_inject();

	return 0;
}

static int inj_bank_get(void *data, u64 *val)
{
	struct mce *m = (struct mce *)data;

	*val = m->bank;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(bank_fops, inj_bank_get, inj_bank_set, "%llu\n");

struct dfs_node {
	char *name;
	struct dentry *d;
	const struct file_operations *fops;
} dfs_fls[] = {
	{ .name = "status",	.fops = &status_fops },
	{ .name = "misc",	.fops = &misc_fops },
	{ .name = "addr",	.fops = &addr_fops },
	{ .name = "bank",	.fops = &bank_fops },
	{ .name = "flags",	.fops = &flags_fops },
	{ .name = "cpu",	.fops = &extcpu_fops },
};

static int __init init_mce_inject(void)
{
	int i;

	dfs_inj = debugfs_create_dir("mce-inject", NULL);
	if (!dfs_inj)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(dfs_fls); i++) {
		dfs_fls[i].d = debugfs_create_file(dfs_fls[i].name,
						    S_IRUSR | S_IWUSR,
						    dfs_inj,
						    &i_mce,
						    dfs_fls[i].fops);

		if (!dfs_fls[i].d)
			goto err_dfs_add;
	}

	return 0;

err_dfs_add:
	while (--i >= 0)
		debugfs_remove(dfs_fls[i].d);

	debugfs_remove(dfs_inj);
	dfs_inj = NULL;

	return -ENOMEM;
}

static void __exit exit_mce_inject(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dfs_fls); i++)
		debugfs_remove(dfs_fls[i].d);

	memset(&dfs_fls, 0, sizeof(dfs_fls));

	debugfs_remove(dfs_inj);
	dfs_inj = NULL;
}
module_init(init_mce_inject);
module_exit(exit_mce_inject);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Borislav Petkov <bp@alien8.de>");
MODULE_AUTHOR("AMD Inc.");
MODULE_DESCRIPTION("MCE injection facility for RAS testing");

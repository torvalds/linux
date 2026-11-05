// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2026 Intel Corporation. */

/*
 * Intel driver to reset bitfix filters when they overflow.
 *
 * Each bitfix filter has limited slots to track corrected errors.
 * These slots can be filled by transient corrected errors (e.g.,
 * bit flips from particle strikes) that don't represent permanent
 * hardware defects.
 *
 * When the filter overflows (yellow status), reset it to reclaim
 * slots occupied by transient corrected errors. If the overflow
 * repeats frequently after reset, it indicates persistent hardware
 * defects that need attention: the system should be scheduled for
 * servicing.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cacheinfo.h>
#include <linux/cleanup.h>
#include <linux/cpufeature.h>
#include <linux/cpuhplock.h>
#include <linux/device-id/x86_cpu.h>
#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/topology.h>
#include <linux/types.h>
#include <linux/xarray.h>

#include <asm/cpu_device_id.h>
#include <asm/cpufeatures.h>
#include <asm/intel-family.h>
#include <asm/mce.h>
#include <asm/msr.h>
#include <asm/msr-index.h>

/*
 * A 10-minute observation period helps distinguish between:
 *
 *  - A long-term accumulation of transient corrected errors
 *    (filter stays clear after reset).
 *
 *  - Permanent defects (filter overflows again quickly).
 */
#define BFF_OVERFLOW_INTERVAL	secs_to_jiffies(10 * 60)

/* Intel bitfix filter control register defines */
#define MSR_MC0_BFF_CTL		0x000006c0
#define MSR_MCx_BFF_CTL(x)	(MSR_MC0_BFF_CTL + (x))
#define  MCI_BFF_RESET		BIT_ULL(0)

/* Machine check bank hardware unit types */
enum bff_bank_type {
	BFF_BANK_NONE = 0,
	BFF_BANK_DCU,
	BFF_BANK_DTLB,
	BFF_BANK_MLC,
	BFF_BANK_CCF,
	BFF_BANK_HSF,
	BFF_BANK_IOCACHE,
};

/*
 * Mapping from machine check bank numbers on Diamond Rapids CPU, that are
 * supported by a bitfix filter, to hardware unit type.
 */
static const enum bff_bank_type dmr_mcbanks[MAX_NR_BANKS] = {
	[1]	= BFF_BANK_DCU,
	[2]	= BFF_BANK_DTLB,
	[3]	= BFF_BANK_MLC,
	[6]	= BFF_BANK_CCF,
	[13]	= BFF_BANK_HSF,
	[17]	= BFF_BANK_IOCACHE
};

static const struct x86_cpu_id bff_cpu_ids[] __initconst = {
	X86_MATCH_VFM(INTEL_DIAMONDRAPIDS_X, dmr_mcbanks),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, bff_cpu_ids);

static const enum bff_bank_type *bff_bank_types;

static DEFINE_XARRAY(bff_bank_xa);

/* Diamond Rapids maps APICID[2] to the IMH instance within a socket. */
#define APICID_IMH_NUM		GENMASK(2, 2)
#define IMH_NUM(apicid)		FIELD_GET(APICID_IMH_NUM, apicid)
#define NUM_IMH_PER_SOCKET	2

static void bff_set_imh_id(struct mce *mce, unsigned long *id)
{
	int imh_num;

	imh_num = NUM_IMH_PER_SOCKET * topology_physical_package_id(mce->extcpu) +
			IMH_NUM(mce->apicid);

	*id |= imh_num;
}

static bool bff_set_cache_id(int cpu, int level, unsigned long *id)
{
	int cacheid;

	guard(cpus_read_lock)();

	cacheid = get_cpu_cacheinfo_id(cpu, level);
	if (cacheid == -1) {
		pr_warn("Could not get L%d cache id for CPU %d\n", level, cpu);
		return false;
	}

	*id |= cacheid;

	return true;
}

/*
 * Cache IDs are only unique within a cache level.
 * Include the MCA bank number so each BFF-capable hardware
 * resource has a unique tracking ID.
 */
#define BFF_ID_BANK_FIELD	GENMASK(63, 32)

static unsigned long bff_get_id(struct mce *mce)
{
	unsigned long id = FIELD_PREP(BFF_ID_BANK_FIELD, mce->bank);

	switch (bff_bank_types[mce->bank]) {
	case BFF_BANK_DCU:
	case BFF_BANK_DTLB:
		if (!bff_set_cache_id(mce->extcpu, 1, &id))
			return ULONG_MAX;
		break;

	case BFF_BANK_MLC:
		if (!bff_set_cache_id(mce->extcpu, 2, &id))
			return ULONG_MAX;
		break;

	case BFF_BANK_CCF:
		if (!bff_set_cache_id(mce->extcpu, 3, &id))
			return ULONG_MAX;
		break;

	case BFF_BANK_HSF:
	case BFF_BANK_IOCACHE:
		bff_set_imh_id(mce, &id);
		break;

	default:
		return ULONG_MAX;
	}

	return id;
}

/*
 * Save current timestamp for bff_id. Return true if it is within
 * BFF_OVERFLOW_INTERVAL of previous timestamp for this bff_id.
 */
static bool bff_overflow_is_frequent(unsigned long bff_id)
{
	unsigned long now = jiffies, interval_end;
	unsigned long *ts;

	if (bff_id == ULONG_MAX)
		return false;

	ts = xa_load(&bff_bank_xa, bff_id);
	if (!ts) {
		ts = kzalloc_obj(*ts);
		if (!ts) {
			pr_warn("Failed to allocate timestamp for bitfix filter 0x%lx\n", bff_id);
			return false;
		}
		if (xa_is_err(xa_store(&bff_bank_xa, bff_id, ts, GFP_KERNEL))) {
			kfree(ts);
			pr_warn("Failed to record timestamp for bitfix filter 0x%lx\n", bff_id);
			return false;
		}
		*ts = now;

		return false;
	}

	interval_end = *ts + BFF_OVERFLOW_INTERVAL;
	*ts = now;

	return time_before(now, interval_end);
}

static void bff_reset_and_report(struct mce *mce)
{
	/* Reset bitfix filter using the CPU that logged the yellow status */
	if (wrmsrq_on_cpu(mce->extcpu, MSR_MCx_BFF_CTL(mce->bank), MCI_BFF_RESET))
		pr_warn("Failed to reset bitfix filter for CPU %d Bank %d\n",
			mce->extcpu, mce->bank);

	/*
	 * Use the unique id for the bitfix filter instance that overflowed and
	 * check if this is a repeat within the BFF_OVERFLOW_INTERVAL. If it
	 * is, then report at WARN severity as the user may want to take action.
	 */
	if (bff_overflow_is_frequent(bff_get_id(mce))) {
		pr_warn_ratelimited(HW_ERR "Socket %d CPU %d Bank %d bitfix filter overflowed frequently\n",
				    mce->socketid, mce->extcpu, mce->bank);
	} else {
		pr_notice_ratelimited(HW_ERR "Socket %d CPU %d Bank %d bitfix filter overflowed\n",
				      mce->socketid, mce->extcpu, mce->bank);
	}
}

static int bff_mce_notify(struct notifier_block *nb, unsigned long val, void *data)
{
	struct mce *mce = data;

	/* TES is undefined for uncorrected errors. */
	if (mce->status & MCI_STATUS_UC)
		return NOTIFY_DONE;

	if (MCI_STATUS_TES(mce->status) != MCI_STATUS_TES_YELLOW)
		return NOTIFY_DONE;

	if (mce->bank >= MAX_NR_BANKS || bff_bank_types[mce->bank] == BFF_BANK_NONE)
		return NOTIFY_DONE;

	bff_reset_and_report(mce);

	return NOTIFY_DONE;
}

static struct notifier_block bff_notifier = {
	.notifier_call	= bff_mce_notify,
	.priority	= MCE_PRIO_EDAC,
};

static int __init bff_init(void)
{
	const struct x86_cpu_id *m;
	u64 core_caps, mcg_cap;

	if (!cpu_feature_enabled(X86_FEATURE_MCA))
		return -ENODEV;
	rdmsrq(MSR_IA32_MCG_CAP, mcg_cap);
	if (!(mcg_cap & MCG_TES_P))
		return -ENODEV;

	if (!cpu_feature_enabled(X86_FEATURE_CORE_CAPABILITIES))
		return -ENODEV;
	rdmsrq(MSR_IA32_CORE_CAPS, core_caps);
	if (!(core_caps & MSR_IA32_CORE_CAPS_BFF_RESET))
		return -ENODEV;

	m = x86_match_cpu(bff_cpu_ids);
	if (!m) {
		pr_info("CPU model not supported by the bitfix filter reset driver\n");
		return -ENODEV;
	}

	bff_bank_types = (const enum bff_bank_type *)m->driver_data;

	mce_register_decode_chain(&bff_notifier);

	return 0;
}

static void __exit bff_exit(void)
{
	unsigned long bff_id;
	unsigned long *ts;

	mce_unregister_decode_chain(&bff_notifier);

	xa_for_each(&bff_bank_xa, bff_id, ts)
		kfree(ts);
	xa_destroy(&bff_bank_xa);
}

module_init(bff_init);
module_exit(bff_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tony Luck");
MODULE_DESCRIPTION("Intel bitfix filter reset driver");

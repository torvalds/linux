/* Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/wakelock.h>
#include <linux/pm_qos_params.h>
#include <linux/sysfs.h>
#include <linux/stat.h>
#include <asm/sections.h>

#include "coresight.h"
#include <mach/sec_debug.h>

#define etm_writel(etm, cpu, val, off)	\
			__raw_writel((val), etm.base + (SZ_4K * cpu) + off)
#define etm_readl(etm, cpu, off)	\
			__raw_readl(etm.base + (SZ_4K * cpu) + off)

/*
 * Device registers:
 * 0x000 - 0x2FC: Trace		registers
 * 0x300 - 0x314: Management	registers
 * 0x318 - 0xEFC: Trace		registers
 *
 * Coresight registers
 * 0xF00 - 0xF9C: Management	registers
 * 0xFA0 - 0xFA4: Management	registers in PFTv1.0
 *		  Trace		registers in PFTv1.1
 * 0xFA8 - 0xFFC: Management	registers
 */

/* Trace registers (0x000-0x2FC) */
#define ETMCR			(0x000)
#define ETMCCR			(0x004)
#define ETMTRIGGER		(0x008)
#define ETMSR			(0x010)
#define ETMSCR			(0x014)
#define ETMTSSCR		(0x018)
#define ETMTEEVR		(0x020)
#define ETMTECR1		(0x024)
#define ETMFFLR			(0x02C)
#define ETMACVRn(n)		(0x040 + (n * 4))
#define ETMACTRn(n)		(0x080 + (n * 4))
#define ETMCNTRLDVRn(n)		(0x140 + (n * 4))
#define ETMCNTENRn(n)		(0x150 + (n * 4))
#define ETMCNTRLDEVRn(n)	(0x160 + (n * 4))
#define ETMCNTVRn(n)		(0x170 + (n * 4))
#define ETMSQ12EVR		(0x180)
#define ETMSQ21EVR		(0x184)
#define ETMSQ23EVR		(0x188)
#define ETMSQ31EVR		(0x18C)
#define ETMSQ32EVR		(0x190)
#define ETMSQ13EVR		(0x194)
#define ETMSQR			(0x19C)
#define ETMEXTOUTEVRn(n)	(0x1A0 + (n * 4))
#define ETMCIDCVRn(n)		(0x1B0 + (n * 4))
#define ETMCIDCMR		(0x1BC)
#define ETMIMPSPEC0		(0x1C0)
#define ETMIMPSPEC1		(0x1C4)
#define ETMIMPSPEC2		(0x1C8)
#define ETMIMPSPEC3		(0x1CC)
#define ETMIMPSPEC4		(0x1D0)
#define ETMIMPSPEC5		(0x1D4)
#define ETMIMPSPEC6		(0x1D8)
#define ETMIMPSPEC7		(0x1DC)
#define ETMSYNCFR		(0x1E0)
#define ETMIDR			(0x1E4)
#define ETMCCER			(0x1E8)
#define ETMEXTINSELR		(0x1EC)
#define ETMTESSEICR		(0x1F0)
#define ETMEIBCR		(0x1F4)
#define ETMTSEVR		(0x1F8)
#define ETMAUXCR		(0x1FC)
#define ETMTRACEIDR		(0x200)
#define ETMVMIDCVR		(0x240)
/* Management registers (0x300-0x314) */
#define ETMOSLAR		(0x300)
#define ETMOSLSR		(0x304)
#define ETMOSSRR		(0x308)
#define ETMPDCR			(0x310)
#define ETMPDSR			(0x314)

#define ETM_MAX_ADDR_CMP	(16)
#define ETM_MAX_CNTR		(4)
#define ETM_MAX_CTXID_CMP	(3)

#define ETM_MODE_EXCLUDE	BIT(0)
#define ETM_MODE_CYCACC		BIT(1)
#define ETM_MODE_STALL		BIT(2)
#define ETM_MODE_TIMESTAMP	BIT(3)
#define ETM_MODE_CTXID		BIT(4)
#define ETM_MODE_ALL		(0x1F)

#define ETM_EVENT_MASK		(0x1FFFF)
#define ETM_SYNC_MASK		(0xFFF)
#define ETM_ALL_MASK		(0xFFFFFFFF)

#define ETM_SEQ_STATE_MAX_VAL	(0x2)

enum {
	ETM_ADDR_TYPE_NONE,
	ETM_ADDR_TYPE_SINGLE,
	ETM_ADDR_TYPE_RANGE,
	ETM_ADDR_TYPE_START,
	ETM_ADDR_TYPE_STOP,
};

#define ETM_LOCK(cpu)							\
do {									\
	mb();								\
	etm_writel(etm, cpu, 0x0, CS_LAR);				\
} while (0)
#define ETM_UNLOCK(cpu)							\
do {									\
	etm_writel(etm, cpu, CS_UNLOCK_MAGIC, CS_LAR);			\
	mb();								\
} while (0)


#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "coresight."

#ifdef CONFIG_CORESIGHT_ETM_DEFAULT_ENABLE
static int etm_boot_enable = 1;
#else
static int etm_boot_enable;
#endif
module_param_named(
	etm_boot_enable, etm_boot_enable, int, S_IRUGO
);

struct etm_ctx {
	void __iomem			*base;
	bool				enabled;
	struct wake_lock		wake_lock;
	struct pm_qos_request_list	qos_req;
	struct mutex			mutex;
	struct device			*dev;
	struct kobject			*kobj;
	uint8_t				arch;
	uint8_t				nr_addr_cmp;
	uint8_t				nr_cntr;
	uint8_t				nr_ext_inp;
	uint8_t				nr_ext_out;
	uint8_t				nr_ctxid_cmp;
	uint8_t				reset;
	uint32_t			mode;
	uint32_t			ctrl;
	uint32_t			trigger_event;
	uint32_t			startstop_ctrl;
	uint32_t			enable_event;
	uint32_t			enable_ctrl1;
	uint32_t			fifofull_level;
	uint8_t				addr_idx;
	uint32_t			addr_val[ETM_MAX_ADDR_CMP];
	uint32_t			addr_acctype[ETM_MAX_ADDR_CMP];
	uint32_t			addr_type[ETM_MAX_ADDR_CMP];
	uint8_t				cntr_idx;
	uint32_t			cntr_rld_val[ETM_MAX_CNTR];
	uint32_t			cntr_event[ETM_MAX_CNTR];
	uint32_t			cntr_rld_event[ETM_MAX_CNTR];
	uint32_t			cntr_val[ETM_MAX_CNTR];
	uint32_t			seq_12_event;
	uint32_t			seq_21_event;
	uint32_t			seq_23_event;
	uint32_t			seq_31_event;
	uint32_t			seq_32_event;
	uint32_t			seq_13_event;
	uint32_t			seq_curr_state;
	uint8_t				ctxid_idx;
	uint32_t			ctxid_val[ETM_MAX_CTXID_CMP];
	uint32_t			ctxid_mask;
	uint32_t			sync_freq;
	uint32_t			timestamp_event;
};

static struct etm_ctx etm = {
	.trigger_event		= 0x406F,
	.enable_event		= 0x6F,
	.enable_ctrl1		= 0x1,
	.fifofull_level		= 0x28,
	.addr_val		= {(uint32_t) _stext, (uint32_t) _etext},
	.addr_type		= {ETM_ADDR_TYPE_RANGE, ETM_ADDR_TYPE_RANGE},
	.cntr_event		= {[0 ... (ETM_MAX_CNTR - 1)] = 0x406F},
	.cntr_rld_event		= {[0 ... (ETM_MAX_CNTR - 1)] = 0x406F},
	.seq_12_event		= 0x406F,
	.seq_21_event		= 0x406F,
	.seq_23_event		= 0x406F,
	.seq_31_event		= 0x406F,
	.seq_32_event		= 0x406F,
	.seq_13_event		= 0x406F,
	.sync_freq		= 0x80,
	.timestamp_event	= 0x406F,
};


/* ETM clock is derived from the processor clock and gets enabled on a
 * logical OR of below items on Krait (pass2 onwards):
 * 1.CPMR[ETMCLKEN] is 1
 * 2.ETMCR[PD] is 0
 * 3.ETMPDCR[PU] is 1
 * 4.Reset is asserted (core or debug)
 * 5.APB memory mapped requests (eg. EDAP access)
 *
 * 1., 2. and 3. above are permanent enables whereas 4. and 5. are temporary
 * enables
 *
 * We rely on 5. to be able to access ETMCR and then use 2. above for ETM
 * clock vote in the driver and the save-restore code uses 1. above
 * for its vote
 */
static void etm_set_pwrdwn(int cpu)
{
	uint32_t etmcr;

	etmcr = etm_readl(etm, cpu, ETMCR);
	etmcr |= BIT(0);
	etm_writel(etm, cpu, etmcr, ETMCR);
}

static void etm_clr_pwrdwn(int cpu)
{
	uint32_t etmcr;

	etmcr = etm_readl(etm, cpu, ETMCR);
	etmcr &= ~BIT(0);
	etm_writel(etm, cpu, etmcr, ETMCR);
}

static void etm_set_prog(int cpu)
{
	uint32_t etmcr;
	int count;

	etmcr = etm_readl(etm, cpu, ETMCR);
	etmcr |= BIT(10);
	etm_writel(etm, cpu, etmcr, ETMCR);

	for (count = TIMEOUT_US; BVAL(etm_readl(etm, cpu, ETMSR), 1) != 1
				&& count > 0; count--)
		udelay(1);
	WARN(count == 0, "timeout while setting prog bit, ETMSR: %#x\n",
	     etm_readl(etm, cpu, ETMSR));
}

static void etm_clr_prog(int cpu)
{
	uint32_t etmcr;
	int count;

	etmcr = etm_readl(etm, cpu, ETMCR);
	etmcr &= ~BIT(10);
	etm_writel(etm, cpu, etmcr, ETMCR);

	for (count = TIMEOUT_US; BVAL(etm_readl(etm, cpu, ETMSR), 1) != 0
				&& count > 0; count--)
		udelay(1);
	WARN(count == 0, "timeout while clearing prog bit, ETMSR: %#x\n",
	     etm_readl(etm, cpu, ETMSR));
}

static void __etm_enable(int cpu)
{
	int i;

	ETM_UNLOCK(cpu);
	/* Vote for ETM power/clock enable */
	etm_clr_pwrdwn(cpu);
	etm_set_prog(cpu);

	etm_writel(etm, cpu, etm.ctrl | BIT(10), ETMCR);
	etm_writel(etm, cpu, etm.trigger_event, ETMTRIGGER);
	etm_writel(etm, cpu, etm.startstop_ctrl, ETMTSSCR);
	etm_writel(etm, cpu, etm.enable_event, ETMTEEVR);
	etm_writel(etm, cpu, etm.enable_ctrl1, ETMTECR1);
	etm_writel(etm, cpu, etm.fifofull_level, ETMFFLR);
	for (i = 0; i < etm.nr_addr_cmp; i++) {
		etm_writel(etm, cpu, etm.addr_val[i], ETMACVRn(i));
		etm_writel(etm, cpu, etm.addr_acctype[i], ETMACTRn(i));
	}
	for (i = 0; i < etm.nr_cntr; i++) {
		etm_writel(etm, cpu, etm.cntr_rld_val[i], ETMCNTRLDVRn(i));
		etm_writel(etm, cpu, etm.cntr_event[i], ETMCNTENRn(i));
		etm_writel(etm, cpu, etm.cntr_rld_event[i], ETMCNTRLDEVRn(i));
		etm_writel(etm, cpu, etm.cntr_val[i], ETMCNTVRn(i));
	}
	etm_writel(etm, cpu, etm.seq_12_event, ETMSQ12EVR);
	etm_writel(etm, cpu, etm.seq_21_event, ETMSQ21EVR);
	etm_writel(etm, cpu, etm.seq_23_event, ETMSQ23EVR);
	etm_writel(etm, cpu, etm.seq_31_event, ETMSQ31EVR);
	etm_writel(etm, cpu, etm.seq_32_event, ETMSQ32EVR);
	etm_writel(etm, cpu, etm.seq_13_event, ETMSQ13EVR);
	etm_writel(etm, cpu, etm.seq_curr_state, ETMSQR);
	for (i = 0; i < etm.nr_ext_out; i++)
		etm_writel(etm, cpu, 0x0000406F, ETMEXTOUTEVRn(i));
	for (i = 0; i < etm.nr_ctxid_cmp; i++)
		etm_writel(etm, cpu, etm.ctxid_val[i], ETMCIDCVRn(i));
	etm_writel(etm, cpu, etm.ctxid_mask, ETMCIDCMR);
	etm_writel(etm, cpu, etm.sync_freq, ETMSYNCFR);
	etm_writel(etm, cpu, 0x00000000, ETMEXTINSELR);
	etm_writel(etm, cpu, etm.timestamp_event, ETMTSEVR);
	etm_writel(etm, cpu, 0x00000000, ETMAUXCR);
	etm_writel(etm, cpu, cpu+1, ETMTRACEIDR);
	etm_writel(etm, cpu, 0x00000000, ETMVMIDCVR);

	etm_clr_prog(cpu);
	ETM_LOCK(cpu);
}

int etm_enable(int pm_enable)
{
	int ret, cpu;

	if (!sec_debug_level.en.kernel_fault) {
		return -ENODEV;
	}

	if (etm.enabled) {
		dev_err(etm.dev, "ETM tracing already enabled\n");
		ret = -EPERM;
		goto err;
	}

	wake_lock(&etm.wake_lock);
	/* 1. causes all online cpus to come out of idle PC
	 * 2. prevents idle PC until save restore flag is enabled atomically
	 *
	 * we rely on the user to prevent hotplug on/off racing with this
	 * operation and to ensure cores where trace is expected to be turned
	 * on are already hotplugged on
	 */
	if (pm_enable)
		pm_qos_update_request(&etm.qos_req, 0);

	etb_disable();
	tpiu_disable();
	/* enable ETB first to avoid loosing any trace data */
	etb_enable();
	funnel_enable(0x0, 0x3);
	for_each_online_cpu(cpu)
		__etm_enable(cpu);

	etm.enabled = true;

	if (pm_enable)
		pm_qos_update_request(&etm.qos_req, PM_QOS_DEFAULT_VALUE);
	wake_unlock(&etm.wake_lock);

	return 0;
err:
	return ret;
}

static void __etm_disable(int cpu)
{
	ETM_UNLOCK(cpu);
	etm_set_prog(cpu);

	/* program trace enable to low by using always false event */
	etm_writel(etm, cpu, 0x6F | BIT(14), ETMTEEVR);

	/* Vote for ETM power/clock disable */
	etm_set_pwrdwn(cpu);
	ETM_LOCK(cpu);
}

int etm_disable(int pm_enable)
{
	int ret, cpu;

	if (!sec_debug_level.en.kernel_fault) {
		return -ENODEV;
	}

	if (!etm.enabled) {
		dev_err(etm.dev, "ETM tracing already disabled\n");
		ret = -EPERM;
		goto err;
	}

	wake_lock(&etm.wake_lock);
	/* 1. causes all online cpus to come out of idle PC
	 * 2. prevents idle PC until save restore flag is disabled atomically
	 *
	 * we rely on the user to prevent hotplug on/off racing with this
	 * operation and to ensure cores where trace is expected to be turned
	 * off are already hotplugged on
	 */
	if (pm_enable)
		pm_qos_update_request(&etm.qos_req, 0);

	for_each_online_cpu(cpu)
		__etm_disable(cpu);

	etb_dump();
	etb_disable();
	funnel_disable(0x0, 0x3);

	etm.enabled = false;

	if (pm_enable)
		pm_qos_update_request(&etm.qos_req, PM_QOS_DEFAULT_VALUE);
	wake_unlock(&etm.wake_lock);

	return 0;
err:
	return ret;
}

#define ETM_STORE(name, mask)						\
static ssize_t name##_store(struct kobject *kobj,			\
			struct kobj_attribute *attr,			\
			const char *buf, size_t n)			\
{									\
	unsigned long val;						\
									\
	if (sscanf(buf, "%lx", &val) != 1)				\
		return -EINVAL;						\
									\
	etm.name = val & mask;						\
	return n;							\
}

#define ETM_SHOW(name)							\
static ssize_t name##_show(struct kobject *kobj,			\
			struct kobj_attribute *attr,			\
			char *buf)					\
{									\
	unsigned long val = etm.name;					\
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);		\
}

#define ETM_ATTR(name)							\
static struct kobj_attribute name##_attr =				\
		__ATTR(name, S_IRUGO | S_IWUSR, name##_show, name##_store)
#define ETM_ATTR_RO(name)						\
static struct kobj_attribute name##_attr =				\
		__ATTR(name, S_IRUGO, name##_show, NULL)

static ssize_t enabled_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	int ret = 0;
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	mutex_lock(&etm.mutex);
	if (val)
		ret = etm_enable(1);
	else
		ret = etm_disable(1);
	mutex_unlock(&etm.mutex);

	if (ret)
		return ret;
	return n;
}
ETM_SHOW(enabled);
ETM_ATTR(enabled);

ETM_SHOW(nr_addr_cmp);
ETM_ATTR_RO(nr_addr_cmp);
ETM_SHOW(nr_cntr);
ETM_ATTR_RO(nr_cntr);
ETM_SHOW(nr_ctxid_cmp);
ETM_ATTR_RO(nr_ctxid_cmp);

/* Reset to trace everything i.e. exclude nothing. */
static ssize_t reset_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	int i;
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	mutex_lock(&etm.mutex);
	if (val) {
		etm.mode = ETM_MODE_EXCLUDE;
		etm.ctrl = 0x0;
		etm.trigger_event = 0x406F;
		etm.startstop_ctrl = 0x0;
		etm.enable_event = 0x6F;
		etm.enable_ctrl1 = 0x1000000;
		etm.fifofull_level = 0x28;
		etm.addr_idx = 0x0;
		for (i = 0; i < etm.nr_addr_cmp; i++) {
			etm.addr_val[i] = 0x0;
			etm.addr_acctype[i] = 0x0;
			etm.addr_type[i] = ETM_ADDR_TYPE_NONE;
		}
		etm.cntr_idx = 0x0;
		for (i = 0; i < etm.nr_cntr; i++) {
			etm.cntr_rld_val[i] = 0x0;
			etm.cntr_event[i] = 0x406F;
			etm.cntr_rld_event[i] = 0x406F;
			etm.cntr_val[i] = 0x0;
		}
		etm.seq_12_event = 0x406F;
		etm.seq_21_event = 0x406F;
		etm.seq_23_event = 0x406F;
		etm.seq_31_event = 0x406F;
		etm.seq_32_event = 0x406F;
		etm.seq_13_event = 0x406F;
		etm.seq_curr_state = 0x0;
		etm.ctxid_idx = 0x0;
		for (i = 0; i < etm.nr_ctxid_cmp; i++)
			etm.ctxid_val[i] = 0x0;
		etm.ctxid_mask = 0x0;
		etm.sync_freq = 0x80;
		etm.timestamp_event = 0x406F;
	}
	mutex_unlock(&etm.mutex);
	return n;
}
ETM_SHOW(reset);
ETM_ATTR(reset);

static ssize_t mode_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	mutex_lock(&etm.mutex);
	etm.mode = val & ETM_MODE_ALL;

	if (etm.mode & ETM_MODE_EXCLUDE)
		etm.enable_ctrl1 |= BIT(24);
	else
		etm.enable_ctrl1 &= ~BIT(24);

	if (etm.mode & ETM_MODE_CYCACC)
		etm.ctrl |= BIT(12);
	else
		etm.ctrl &= ~BIT(12);

	if (etm.mode & ETM_MODE_STALL)
		etm.ctrl |= BIT(7);
	else
		etm.ctrl &= ~BIT(7);

	if (etm.mode & ETM_MODE_TIMESTAMP)
		etm.ctrl |= BIT(28);
	else
		etm.ctrl &= ~BIT(28);
	if (etm.mode & ETM_MODE_CTXID)
		etm.ctrl |= (BIT(14) | BIT(15));
	else
		etm.ctrl &= ~(BIT(14) | BIT(15));
	mutex_unlock(&etm.mutex);

	return n;
}
ETM_SHOW(mode);
ETM_ATTR(mode);

ETM_STORE(trigger_event, ETM_EVENT_MASK);
ETM_SHOW(trigger_event);
ETM_ATTR(trigger_event);

ETM_STORE(enable_event, ETM_EVENT_MASK);
ETM_SHOW(enable_event);
ETM_ATTR(enable_event);

ETM_STORE(fifofull_level, ETM_ALL_MASK);
ETM_SHOW(fifofull_level);
ETM_ATTR(fifofull_level);

static ssize_t addr_idx_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;
	if (val >= etm.nr_addr_cmp)
		return -EINVAL;

	/* Use mutex to ensure index doesn't change while it gets dereferenced
	 * multiple times within a mutex block elsewhere.
	 */
	mutex_lock(&etm.mutex);
	etm.addr_idx = val;
	mutex_unlock(&etm.mutex);
	return n;
}
ETM_SHOW(addr_idx);
ETM_ATTR(addr_idx);

static ssize_t addr_single_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;
	uint8_t idx;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	mutex_lock(&etm.mutex);
	idx = etm.addr_idx;
	if (!(etm.addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      etm.addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		mutex_unlock(&etm.mutex);
		return -EPERM;
	}

	etm.addr_val[idx] = val;
	etm.addr_type[idx] = ETM_ADDR_TYPE_SINGLE;
	mutex_unlock(&etm.mutex);
	return n;
}
static ssize_t addr_single_show(struct kobject *kobj,
			struct kobj_attribute *attr,
			char *buf)
{
	unsigned long val;
	uint8_t idx;

	mutex_lock(&etm.mutex);
	idx = etm.addr_idx;
	if (!(etm.addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      etm.addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		mutex_unlock(&etm.mutex);
		return -EPERM;
	}

	val = etm.addr_val[idx];
	mutex_unlock(&etm.mutex);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
ETM_ATTR(addr_single);

static ssize_t addr_range_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val1, val2;
	uint8_t idx;

	if (sscanf(buf, "%lx %lx", &val1, &val2) != 2)
		return -EINVAL;
	/* lower address comparator cannot have a higher address value */
	if (val1 > val2)
		return -EINVAL;

	mutex_lock(&etm.mutex);
	idx = etm.addr_idx;
	if (idx % 2 != 0) {
		mutex_unlock(&etm.mutex);
		return -EPERM;
	}
	if (!((etm.addr_type[idx] == ETM_ADDR_TYPE_NONE &&
	       etm.addr_type[idx + 1] == ETM_ADDR_TYPE_NONE) ||
	      (etm.addr_type[idx] == ETM_ADDR_TYPE_RANGE &&
	       etm.addr_type[idx + 1] == ETM_ADDR_TYPE_RANGE))) {
		mutex_unlock(&etm.mutex);
		return -EPERM;
	}

	etm.addr_val[idx] = val1;
	etm.addr_type[idx] = ETM_ADDR_TYPE_RANGE;
	etm.addr_val[idx + 1] = val2;
	etm.addr_type[idx + 1] = ETM_ADDR_TYPE_RANGE;
	etm.enable_ctrl1 |= (1 << (idx/2));
	mutex_unlock(&etm.mutex);
	return n;
}
static ssize_t addr_range_show(struct kobject *kobj,
			struct kobj_attribute *attr,
			char *buf)
{
	unsigned long val1, val2;
	uint8_t idx;

	mutex_lock(&etm.mutex);
	idx = etm.addr_idx;
	if (idx % 2 != 0) {
		mutex_unlock(&etm.mutex);
		return -EPERM;
	}
	if (!((etm.addr_type[idx] == ETM_ADDR_TYPE_NONE &&
	       etm.addr_type[idx + 1] == ETM_ADDR_TYPE_NONE) ||
	      (etm.addr_type[idx] == ETM_ADDR_TYPE_RANGE &&
	       etm.addr_type[idx + 1] == ETM_ADDR_TYPE_RANGE))) {
		mutex_unlock(&etm.mutex);
		return -EPERM;
	}

	val1 = etm.addr_val[idx];
	val2 = etm.addr_val[idx + 1];
	mutex_unlock(&etm.mutex);
	return scnprintf(buf, PAGE_SIZE, "%#lx %#lx\n", val1, val2);
}
ETM_ATTR(addr_range);

static ssize_t addr_start_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;
	uint8_t idx;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	mutex_lock(&etm.mutex);
	idx = etm.addr_idx;
	if (!(etm.addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      etm.addr_type[idx] == ETM_ADDR_TYPE_START)) {
		mutex_unlock(&etm.mutex);
		return -EPERM;
	}

	etm.addr_val[idx] = val;
	etm.addr_type[idx] = ETM_ADDR_TYPE_START;
	etm.startstop_ctrl |= (1 << idx);
	etm.enable_ctrl1 |= BIT(25);
	mutex_unlock(&etm.mutex);
	return n;
}
static ssize_t addr_start_show(struct kobject *kobj,
			struct kobj_attribute *attr,
			char *buf)
{
	unsigned long val;
	uint8_t idx;

	mutex_lock(&etm.mutex);
	idx = etm.addr_idx;
	if (!(etm.addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      etm.addr_type[idx] == ETM_ADDR_TYPE_START)) {
		mutex_unlock(&etm.mutex);
		return -EPERM;
	}

	val = etm.addr_val[idx];
	mutex_unlock(&etm.mutex);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
ETM_ATTR(addr_start);

static ssize_t addr_stop_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;
	uint8_t idx;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	mutex_lock(&etm.mutex);
	idx = etm.addr_idx;
	if (!(etm.addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      etm.addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		mutex_unlock(&etm.mutex);
		return -EPERM;
	}

	etm.addr_val[idx] = val;
	etm.addr_type[idx] = ETM_ADDR_TYPE_STOP;
	etm.startstop_ctrl |= (1 << (idx + 16));
	etm.enable_ctrl1 |= BIT(25);
	mutex_unlock(&etm.mutex);
	return n;
}
static ssize_t addr_stop_show(struct kobject *kobj,
			struct kobj_attribute *attr,
			char *buf)
{
	unsigned long val;
	uint8_t idx;

	mutex_lock(&etm.mutex);
	idx = etm.addr_idx;
	if (!(etm.addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      etm.addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		mutex_unlock(&etm.mutex);
		return -EPERM;
	}

	val = etm.addr_val[idx];
	mutex_unlock(&etm.mutex);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
ETM_ATTR(addr_stop);

static ssize_t addr_acctype_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	mutex_lock(&etm.mutex);
	etm.addr_acctype[etm.addr_idx] = val;
	mutex_unlock(&etm.mutex);
	return n;
}
static ssize_t addr_acctype_show(struct kobject *kobj,
			struct kobj_attribute *attr,
			char *buf)
{
	unsigned long val;

	mutex_lock(&etm.mutex);
	val = etm.addr_acctype[etm.addr_idx];
	mutex_unlock(&etm.mutex);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
ETM_ATTR(addr_acctype);

static ssize_t cntr_idx_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;
	if (val >= etm.nr_cntr)
		return -EINVAL;

	/* Use mutex to ensure index doesn't change while it gets dereferenced
	 * multiple times within a mutex block elsewhere.
	 */
	mutex_lock(&etm.mutex);
	etm.cntr_idx = val;
	mutex_unlock(&etm.mutex);
	return n;
}
ETM_SHOW(cntr_idx);
ETM_ATTR(cntr_idx);

static ssize_t cntr_rld_val_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	mutex_lock(&etm.mutex);
	etm.cntr_rld_val[etm.cntr_idx] = val;
	mutex_unlock(&etm.mutex);
	return n;
}
static ssize_t cntr_rld_val_show(struct kobject *kobj,
			struct kobj_attribute *attr,
			char *buf)
{
	unsigned long val;
	mutex_lock(&etm.mutex);
	val = etm.cntr_rld_val[etm.cntr_idx];
	mutex_unlock(&etm.mutex);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
ETM_ATTR(cntr_rld_val);

static ssize_t cntr_event_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	mutex_lock(&etm.mutex);
	etm.cntr_event[etm.cntr_idx] = val & ETM_EVENT_MASK;
	mutex_unlock(&etm.mutex);
	return n;
}
static ssize_t cntr_event_show(struct kobject *kobj,
			struct kobj_attribute *attr,
			char *buf)
{
	unsigned long val;

	mutex_lock(&etm.mutex);
	val = etm.cntr_event[etm.cntr_idx];
	mutex_unlock(&etm.mutex);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
ETM_ATTR(cntr_event);

static ssize_t cntr_rld_event_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	mutex_lock(&etm.mutex);
	etm.cntr_rld_event[etm.cntr_idx] = val & ETM_EVENT_MASK;
	mutex_unlock(&etm.mutex);
	return n;
}
static ssize_t cntr_rld_event_show(struct kobject *kobj,
			struct kobj_attribute *attr,
			char *buf)
{
	unsigned long val;

	mutex_lock(&etm.mutex);
	val = etm.cntr_rld_event[etm.cntr_idx];
	mutex_unlock(&etm.mutex);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
ETM_ATTR(cntr_rld_event);

static ssize_t cntr_val_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	mutex_lock(&etm.mutex);
	etm.cntr_val[etm.cntr_idx] = val;
	mutex_unlock(&etm.mutex);
	return n;
}
static ssize_t cntr_val_show(struct kobject *kobj,
			struct kobj_attribute *attr,
			char *buf)
{
	unsigned long val;

	mutex_lock(&etm.mutex);
	val = etm.cntr_val[etm.cntr_idx];
	mutex_unlock(&etm.mutex);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
ETM_ATTR(cntr_val);

ETM_STORE(seq_12_event, ETM_EVENT_MASK);
ETM_SHOW(seq_12_event);
ETM_ATTR(seq_12_event);

ETM_STORE(seq_21_event, ETM_EVENT_MASK);
ETM_SHOW(seq_21_event);
ETM_ATTR(seq_21_event);

ETM_STORE(seq_23_event, ETM_EVENT_MASK);
ETM_SHOW(seq_23_event);
ETM_ATTR(seq_23_event);

ETM_STORE(seq_31_event, ETM_EVENT_MASK);
ETM_SHOW(seq_31_event);
ETM_ATTR(seq_31_event);

ETM_STORE(seq_32_event, ETM_EVENT_MASK);
ETM_SHOW(seq_32_event);
ETM_ATTR(seq_32_event);

ETM_STORE(seq_13_event, ETM_EVENT_MASK);
ETM_SHOW(seq_13_event);
ETM_ATTR(seq_13_event);

static ssize_t seq_curr_state_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;
	if (val > ETM_SEQ_STATE_MAX_VAL)
		return -EINVAL;

	etm.seq_curr_state = val;
	return n;
}
ETM_SHOW(seq_curr_state);
ETM_ATTR(seq_curr_state);

static ssize_t ctxid_idx_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;
	if (val >= etm.nr_ctxid_cmp)
		return -EINVAL;

	/* Use mutex to ensure index doesn't change while it gets dereferenced
	 * multiple times within a mutex block elsewhere.
	 */
	mutex_lock(&etm.mutex);
	etm.ctxid_idx = val;
	mutex_unlock(&etm.mutex);
	return n;
}
ETM_SHOW(ctxid_idx);
ETM_ATTR(ctxid_idx);

static ssize_t ctxid_val_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	mutex_lock(&etm.mutex);
	etm.ctxid_val[etm.ctxid_idx] = val;
	mutex_unlock(&etm.mutex);
	return n;
}
static ssize_t ctxid_val_show(struct kobject *kobj,
			struct kobj_attribute *attr,
			char *buf)
{
	unsigned long val;

	mutex_lock(&etm.mutex);
	val = etm.ctxid_val[etm.ctxid_idx];
	mutex_unlock(&etm.mutex);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
ETM_ATTR(ctxid_val);

ETM_STORE(ctxid_mask, ETM_ALL_MASK);
ETM_SHOW(ctxid_mask);
ETM_ATTR(ctxid_mask);

ETM_STORE(sync_freq, ETM_SYNC_MASK);
ETM_SHOW(sync_freq);
ETM_ATTR(sync_freq);

ETM_STORE(timestamp_event, ETM_EVENT_MASK);
ETM_SHOW(timestamp_event);
ETM_ATTR(timestamp_event);

static struct attribute *etm_attrs[] = {
	&nr_addr_cmp_attr.attr,
	&nr_cntr_attr.attr,
	&nr_ctxid_cmp_attr.attr,
	&reset_attr.attr,
	&mode_attr.attr,
	&trigger_event_attr.attr,
	&enable_event_attr.attr,
	&fifofull_level_attr.attr,
	&addr_idx_attr.attr,
	&addr_single_attr.attr,
	&addr_range_attr.attr,
	&addr_start_attr.attr,
	&addr_stop_attr.attr,
	&addr_acctype_attr.attr,
	&cntr_idx_attr.attr,
	&cntr_rld_val_attr.attr,
	&cntr_event_attr.attr,
	&cntr_rld_event_attr.attr,
	&cntr_val_attr.attr,
	&seq_12_event_attr.attr,
	&seq_21_event_attr.attr,
	&seq_23_event_attr.attr,
	&seq_31_event_attr.attr,
	&seq_32_event_attr.attr,
	&seq_13_event_attr.attr,
	&seq_curr_state_attr.attr,
	&ctxid_idx_attr.attr,
	&ctxid_val_attr.attr,
	&ctxid_mask_attr.attr,
	&sync_freq_attr.attr,
	&timestamp_event_attr.attr,
	NULL,
};

static struct attribute_group etm_attr_grp = {
	.attrs = etm_attrs,
};

static int __init etm_sysfs_init(void)
{
	int ret;

	etm.kobj = kobject_create_and_add("etm", coresight_get_modulekobj());
	if (!etm.kobj) {
		dev_err(etm.dev, "failed to create ETM sysfs kobject\n");
		ret = -ENOMEM;
		goto err_create;
	}

	ret = sysfs_create_file(etm.kobj, &enabled_attr.attr);
	if (ret) {
		dev_err(etm.dev, "failed to create ETM sysfs enabled"
		" attribute\n");
		goto err_file;
	}

	if (sysfs_create_group(etm.kobj, &etm_attr_grp))
		dev_err(etm.dev, "failed to create ETM sysfs group\n");

	return 0;
err_file:
	kobject_put(etm.kobj);
err_create:
	return ret;
}

static void etm_sysfs_exit(void)
{
	sysfs_remove_group(etm.kobj, &etm_attr_grp);
	sysfs_remove_file(etm.kobj, &enabled_attr.attr);
	kobject_put(etm.kobj);
}

static int __init etm_arch_init(void)
{
	/* use cpu 0 for setup */
	int cpu = 0;
	uint32_t etmidr;
	uint32_t etmccr;

	ETM_UNLOCK(cpu);
	/* Vote for ETM power/clock enable */
	etm_clr_pwrdwn(cpu);
	/* Set prog bit. It will be set from reset but this is included to
	 * ensure it is set
	 */
	etm_set_prog(cpu);

	/* find all capabilities */
	etmidr = etm_readl(etm, cpu, ETMIDR);
	etm.arch = BMVAL(etmidr, 4, 11);

	etmccr = etm_readl(etm, cpu, ETMCCR);
	etm.nr_addr_cmp = BMVAL(etmccr, 0, 3) * 2;
	etm.nr_cntr = BMVAL(etmccr, 13, 15);
	etm.nr_ext_inp = BMVAL(etmccr, 17, 19);
	etm.nr_ext_out = BMVAL(etmccr, 20, 22);
	etm.nr_ctxid_cmp = BMVAL(etmccr, 24, 25);

	/* Vote for ETM power/clock disable */
	etm_set_pwrdwn(cpu);
	ETM_LOCK(cpu);

	return 0;
}

static int __devinit etm_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;

	if (!sec_debug_level.en.kernel_fault) {
		pr_info("%s: debug level is low\n",__func__);
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -EINVAL;
		goto err_res;
	}

	etm.base = ioremap_nocache(res->start, resource_size(res));
	if (!etm.base) {
		ret = -EINVAL;
		goto err_ioremap;
	}

	etm.dev = &pdev->dev;

	mutex_init(&etm.mutex);
	wake_lock_init(&etm.wake_lock, WAKE_LOCK_SUSPEND, "coresight_etm");
	pm_qos_add_request(&etm.qos_req, PM_QOS_CPU_DMA_LATENCY,
						PM_QOS_DEFAULT_VALUE);

	ret = etm_arch_init();
	if (ret)
		goto err_arch;

	ret = etm_sysfs_init();
	if (ret)
		goto err_sysfs;

	etm.enabled = false;

	dev_info(etm.dev, "ETM initialized\n");

	if (etm_boot_enable)
		etm_enable(1);

	return 0;

err_sysfs:
err_arch:
	pm_qos_remove_request(&etm.qos_req);
	wake_lock_destroy(&etm.wake_lock);
	mutex_destroy(&etm.mutex);
	iounmap(etm.base);
err_ioremap:
err_res:
	dev_err(etm.dev, "ETM init failed\n");
	return ret;
}

static int etm_remove(struct platform_device *pdev)
{
	if (etm.enabled)
		etm_disable(1);
	etm_sysfs_exit();
	pm_qos_remove_request(&etm.qos_req);
	wake_lock_destroy(&etm.wake_lock);
	mutex_destroy(&etm.mutex);
	iounmap(etm.base);

	return 0;
}

static int etm_suspend(struct device *dev)
{
	etm_disable(1);
	return 0;
}

static int etm_resume(struct device *dev)
{
	etm_enable(1);
	return 0;
}

static const struct dev_pm_ops etm_pm = {
	.suspend = etm_suspend,
	.resume = etm_resume,
};

static struct platform_driver etm_driver = {
	.probe          = etm_probe,
	.remove         = etm_remove,
	.driver         = {
		.name	= "coresight_etm",
		.owner	= THIS_MODULE,
		.pm	= &etm_pm,
	},
};

int __init etm_init(void)
{
	return platform_driver_register(&etm_driver);
}

void etm_exit(void)
{
	platform_driver_unregister(&etm_driver);
}

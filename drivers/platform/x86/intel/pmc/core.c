// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Core SoC Power Management Controller Driver
 *
 * Copyright (c) 2016, Intel Corporation.
 * All Rights Reserved.
 *
 * Authors: Rajneesh Bhardwaj <rajneesh.bhardwaj@intel.com>
 *          Vishwanath Somayaji <vishwanath.somayaji@intel.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

enum header_type {
	HEADER_STATUS,
	HEADER_VALUE,
};

#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/units.h>

#include <asm/cpuid/api.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/msr.h>
#include <asm/tsc.h>

#include "core.h"
#include "ssram_telemetry.h"
#include "../pmt/telemetry.h"

/* Maximum number of modes supported by platfoms that has low power mode capability */
const char *pmc_lpm_modes[] = {
	"S0i2.0",
	"S0i2.1",
	"S0i2.2",
	"S0i3.0",
	"S0i3.1",
	"S0i3.2",
	"S0i3.3",
	"S0i3.4",
	NULL
};

/* PKGC MSRs are common across Intel Core SoCs */
const struct pmc_bit_map msr_map[] = {
	{"Package C2",                  MSR_PKG_C2_RESIDENCY},
	{"Package C3",                  MSR_PKG_C3_RESIDENCY},
	{"Package C6",                  MSR_PKG_C6_RESIDENCY},
	{"Package C7",                  MSR_PKG_C7_RESIDENCY},
	{"Package C8",                  MSR_PKG_C8_RESIDENCY},
	{"Package C9",                  MSR_PKG_C9_RESIDENCY},
	{"Package C10",                 MSR_PKG_C10_RESIDENCY},
	{}
};

static inline u32 pmc_core_reg_read(struct pmc *pmc, int reg_offset)
{
	return readl(pmc->regbase + reg_offset);
}

static inline void pmc_core_reg_write(struct pmc *pmc, int reg_offset,
				      u32 val)
{
	writel(val, pmc->regbase + reg_offset);
}

static inline u64 pmc_core_adjust_slp_s0_step(struct pmc *pmc, u32 value)
{
	/*
	 * ADL PCH does not have the SLP_S0 counter and LPM Residency counters are
	 * used as a workaround which uses 30.5 usec tick. All other client
	 * programs have the legacy SLP_S0 residency counter that is using the 122
	 * usec tick.
	 */
	const int lpm_adj_x2 = pmc->map->lpm_res_counter_step_x2;

	if (pmc->map == &adl_reg_map)
		return (u64)value * GET_X2_COUNTER((u64)lpm_adj_x2);
	else
		return (u64)value * pmc->map->slp_s0_res_counter_step;
}

static int set_etr3(struct pmc_dev *pmcdev)
{
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	const struct pmc_reg_map *map = pmc->map;
	u32 reg;

	if (!map->etr3_offset)
		return -EOPNOTSUPP;

	guard(mutex)(&pmcdev->lock);

	/* check if CF9 is locked */
	reg = pmc_core_reg_read(pmc, map->etr3_offset);
	if (reg & ETR3_CF9LOCK)
		return -EACCES;

	/* write CF9 global reset bit */
	reg |= ETR3_CF9GR;
	pmc_core_reg_write(pmc, map->etr3_offset, reg);

	reg = pmc_core_reg_read(pmc, map->etr3_offset);
	if (!(reg & ETR3_CF9GR))
		return -EIO;

	return 0;
}
static umode_t etr3_is_visible(struct kobject *kobj,
				struct attribute *attr,
				int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct pmc_dev *pmcdev = dev_get_drvdata(dev);
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	const struct pmc_reg_map *map = pmc->map;
	u32 reg;

	scoped_guard(mutex, &pmcdev->lock)
		reg = pmc_core_reg_read(pmc, map->etr3_offset);

	return reg & ETR3_CF9LOCK ? attr->mode & (SYSFS_PREALLOC | 0444) : attr->mode;
}

static ssize_t etr3_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct pmc_dev *pmcdev = dev_get_drvdata(dev);
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	const struct pmc_reg_map *map = pmc->map;
	u32 reg;

	if (!map->etr3_offset)
		return -EOPNOTSUPP;

	scoped_guard(mutex, &pmcdev->lock) {
		reg = pmc_core_reg_read(pmc, map->etr3_offset);
		reg &= ETR3_CF9GR | ETR3_CF9LOCK;
	}

	return sysfs_emit(buf, "0x%08x", reg);
}

static ssize_t etr3_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct pmc_dev *pmcdev = dev_get_drvdata(dev);
	int err;
	u32 reg;

	err = kstrtouint(buf, 16, &reg);
	if (err)
		return err;

	/* allow only CF9 writes */
	if (reg != ETR3_CF9GR)
		return -EINVAL;

	err = set_etr3(pmcdev);
	if (err)
		return err;

	return len;
}
static DEVICE_ATTR_RW(etr3);

static struct attribute *pmc_attrs[] = {
	&dev_attr_etr3.attr,
	NULL
};

static const struct attribute_group pmc_attr_group = {
	.attrs = pmc_attrs,
	.is_visible = etr3_is_visible,
};

static const struct attribute_group *pmc_dev_groups[] = {
	&pmc_attr_group,
	NULL
};

static int pmc_core_dev_state_get(void *data, u64 *val)
{
	struct pmc *pmc = data;
	const struct pmc_reg_map *map = pmc->map;
	u32 value;

	value = pmc_core_reg_read(pmc, map->slp_s0_offset);
	*val = pmc_core_adjust_slp_s0_step(pmc, value);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(pmc_core_dev_state, pmc_core_dev_state_get, NULL, "%llu\n");

static int pmc_core_pson_residency_get(void *data, u64 *val)
{
	struct pmc *pmc = data;
	const struct pmc_reg_map *map = pmc->map;
	u32 value;

	value = pmc_core_reg_read(pmc, map->pson_residency_offset);
	*val = (u64)value * map->pson_residency_counter_step;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(pmc_core_pson_residency, pmc_core_pson_residency_get, NULL, "%llu\n");

static int pmc_core_check_read_lock_bit(struct pmc *pmc)
{
	u32 value;

	value = pmc_core_reg_read(pmc, pmc->map->pm_cfg_offset);
	return value & BIT(pmc->map->pm_read_disable_bit);
}

static void pmc_core_slps0_display(struct pmc *pmc, struct device *dev,
				   struct seq_file *s)
{
	const struct pmc_bit_map **maps = pmc->map->slps0_dbg_maps;
	const struct pmc_bit_map *map;
	int offset = pmc->map->slps0_dbg_offset;
	u32 data;

	while (*maps) {
		map = *maps;
		data = pmc_core_reg_read(pmc, offset);
		offset += 4;
		while (map->name) {
			if (dev)
				dev_info(dev, "SLP_S0_DBG: %-32s\tState: %s\n",
					map->name,
					data & map->bit_mask ? "Yes" : "No");
			if (s)
				seq_printf(s, "SLP_S0_DBG: %-32s\tState: %s\n",
					   map->name,
					   data & map->bit_mask ? "Yes" : "No");
			++map;
		}
		++maps;
	}
}

static unsigned int pmc_core_lpm_get_arr_size(const struct pmc_bit_map **maps)
{
	unsigned int idx;

	for (idx = 0; maps[idx]; idx++)
		;/* Nothing */

	return idx;
}

static void pmc_core_lpm_display(struct pmc *pmc, struct device *dev,
				 struct seq_file *s, u32 offset, int pmc_index,
				 const char *str,
				 const struct pmc_bit_map **maps)
{
	unsigned int index, idx, len = 32, arr_size;
	u32 bit_mask, *lpm_regs;

	arr_size = pmc_core_lpm_get_arr_size(maps);
	lpm_regs = kmalloc_array(arr_size, sizeof(*lpm_regs), GFP_KERNEL);
	if (!lpm_regs)
		return;

	for (index = 0; index < arr_size; index++) {
		lpm_regs[index] = pmc_core_reg_read(pmc, offset);
		offset += 4;
	}

	for (idx = 0; idx < arr_size; idx++) {
		if (dev)
			dev_info(dev, "\nPMC%d:LPM_%s_%d:\t0x%x\n", pmc_index, str, idx,
				lpm_regs[idx]);
		if (s)
			seq_printf(s, "\nPMC%d:LPM_%s_%d:\t0x%x\n", pmc_index, str, idx,
				   lpm_regs[idx]);
		for (index = 0; maps[idx][index].name && index < len; index++) {
			bit_mask = maps[idx][index].bit_mask;
			if (dev)
				dev_info(dev, "PMC%d:%-30s %-30d\n", pmc_index,
					maps[idx][index].name,
					lpm_regs[idx] & bit_mask ? 1 : 0);
			if (s)
				seq_printf(s, "PMC%d:%-30s %-30d\n", pmc_index,
					   maps[idx][index].name,
					   lpm_regs[idx] & bit_mask ? 1 : 0);
		}
	}

	kfree(lpm_regs);
}

static bool slps0_dbg_latch;

static inline u8 pmc_core_reg_read_byte(struct pmc *pmc, int offset)
{
	return readb(pmc->regbase + offset);
}

static void pmc_core_display_map(struct seq_file *s, int index, int idx, int ip,
				 int pmc_index, u8 pf_reg, const struct pmc_bit_map **pf_map)
{
	seq_printf(s, "PMC%d:PCH IP: %-2d - %-32s\tState: %s\n",
		   pmc_index, ip, pf_map[idx][index].name,
		   pf_map[idx][index].bit_mask & pf_reg ? "Off" : "On");
}

static int pmc_core_ppfear_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pmcdev->pmcs); ++i) {
		struct pmc *pmc = pmcdev->pmcs[i];
		const struct pmc_bit_map **maps;
		u8 pf_regs[PPFEAR_MAX_NUM_ENTRIES];
		unsigned int index, iter, idx, ip = 0;

		if (!pmc)
			continue;

		maps = pmc->map->pfear_sts;
		iter = pmc->map->ppfear0_offset;

		for (index = 0; index < pmc->map->ppfear_buckets &&
		     index < PPFEAR_MAX_NUM_ENTRIES; index++, iter++)
			pf_regs[index] = pmc_core_reg_read_byte(pmc, iter);

		for (idx = 0; maps[idx]; idx++) {
			for (index = 0; maps[idx][index].name &&
			     index < pmc->map->ppfear_buckets * 8; ip++, index++)
				pmc_core_display_map(s, index, idx, ip, i,
						     pf_regs[index / 8], maps);
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_ppfear);

/* This function should return link status, 0 means ready */
static int pmc_core_mtpmc_link_status(struct pmc *pmc)
{
	u32 value;

	value = pmc_core_reg_read(pmc, SPT_PMC_PM_STS_OFFSET);
	return value & BIT(SPT_PMC_MSG_FULL_STS_BIT);
}

static int pmc_core_send_msg(struct pmc *pmc, u32 *addr_xram)
{
	u32 dest;
	int timeout;

	for (timeout = NUM_RETRIES; timeout > 0; timeout--) {
		if (pmc_core_mtpmc_link_status(pmc) == 0)
			break;
		msleep(5);
	}

	if (timeout <= 0 && pmc_core_mtpmc_link_status(pmc))
		return -EBUSY;

	dest = (*addr_xram & MTPMC_MASK) | (1U << 1);
	pmc_core_reg_write(pmc, SPT_PMC_MTPMC_OFFSET, dest);
	return 0;
}

static int pmc_core_mphy_pg_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	const struct pmc_bit_map *map = pmc->map->mphy_sts;
	u32 mphy_core_reg_low, mphy_core_reg_high;
	u32 val_low, val_high;
	unsigned int index;
	int err = 0;

	if (pmcdev->pmc_xram_read_bit) {
		seq_puts(s, "Access denied: please disable PMC_READ_DISABLE setting in BIOS.");
		return 0;
	}

	mphy_core_reg_low  = (SPT_PMC_MPHY_CORE_STS_0 << 16);
	mphy_core_reg_high = (SPT_PMC_MPHY_CORE_STS_1 << 16);

	guard(mutex)(&pmcdev->lock);

	err = pmc_core_send_msg(pmc, &mphy_core_reg_low);
	if (err)
		return err;

	msleep(10);
	val_low = pmc_core_reg_read(pmc, SPT_PMC_MFPMC_OFFSET);

	err = pmc_core_send_msg(pmc, &mphy_core_reg_high);
	if (err)
		return err;

	msleep(10);
	val_high = pmc_core_reg_read(pmc, SPT_PMC_MFPMC_OFFSET);

	for (index = 0; index < 8 && map[index].name; index++) {
		seq_printf(s, "%-32s\tState: %s\n",
			   map[index].name,
			   map[index].bit_mask & val_low ? "Not power gated" :
			   "Power gated");
	}

	for (index = 8; map[index].name; index++) {
		seq_printf(s, "%-32s\tState: %s\n",
			   map[index].name,
			   map[index].bit_mask & val_high ? "Not power gated" :
			   "Power gated");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_mphy_pg);

static int pmc_core_pll_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	const struct pmc_bit_map *map = pmc->map->pll_sts;
	u32 mphy_common_reg, val;
	unsigned int index;
	int err = 0;

	if (pmcdev->pmc_xram_read_bit) {
		seq_puts(s, "Access denied: please disable PMC_READ_DISABLE setting in BIOS.");
		return 0;
	}

	mphy_common_reg  = (SPT_PMC_MPHY_COM_STS_0 << 16);
	guard(mutex)(&pmcdev->lock);

	err = pmc_core_send_msg(pmc, &mphy_common_reg);
	if (err)
		return err;

	/* Observed PMC HW response latency for MTPMC-MFPMC is ~10 ms */
	msleep(10);
	val = pmc_core_reg_read(pmc, SPT_PMC_MFPMC_OFFSET);

	for (index = 0; map[index].name ; index++) {
		seq_printf(s, "%-32s\tState: %s\n",
			   map[index].name,
			   map[index].bit_mask & val ? "Active" : "Idle");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_pll);

int pmc_core_send_ltr_ignore(struct pmc_dev *pmcdev, u32 value, int ignore)
{
	struct pmc *pmc;
	const struct pmc_reg_map *map;
	u32 reg;
	unsigned int pmc_index;
	int ltr_index;

	ltr_index = value;
	/* For platforms with multiple pmcs, ltr index value given by user
	 * is based on the contiguous indexes from ltr_show output.
	 * pmc index and ltr index needs to be calculated from it.
	 */
	for (pmc_index = 0; pmc_index < ARRAY_SIZE(pmcdev->pmcs) && ltr_index >= 0; pmc_index++) {
		pmc = pmcdev->pmcs[pmc_index];

		if (!pmc)
			continue;

		map = pmc->map;
		if (ltr_index <= map->ltr_ignore_max)
			break;

		/* Along with IP names, ltr_show map includes CURRENT_PLATFORM
		 * and AGGREGATED_SYSTEM values per PMC. Take these two index
		 * values into account in ltr_index calculation. Also, to start
		 * ltr index from zero for next pmc, subtract it by 1.
		 */
		ltr_index = ltr_index - (map->ltr_ignore_max + 2) - 1;
	}

	if (pmc_index >= ARRAY_SIZE(pmcdev->pmcs) || ltr_index < 0)
		return -EINVAL;

	pr_debug("ltr_ignore for pmc%d: ltr_index:%d\n", pmc_index, ltr_index);

	guard(mutex)(&pmcdev->lock);

	reg = pmc_core_reg_read(pmc, map->ltr_ignore_offset);
	if (ignore)
		reg |= BIT(ltr_index);
	else
		reg &= ~BIT(ltr_index);
	pmc_core_reg_write(pmc, map->ltr_ignore_offset, reg);

	return 0;
}

static ssize_t pmc_core_ltr_write(struct pmc_dev *pmcdev,
				  const char __user *userbuf,
				  size_t count, int ignore)
{
	u32 value;
	int err;

	err = kstrtou32_from_user(userbuf, count, 10, &value);
	if (err)
		return err;

	err = pmc_core_send_ltr_ignore(pmcdev, value, ignore);

	return err ?: count;
}

static ssize_t pmc_core_ltr_ignore_write(struct file *file,
					 const char __user *userbuf,
					 size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct pmc_dev *pmcdev = s->private;

	return pmc_core_ltr_write(pmcdev, userbuf, count, 1);
}

static int pmc_core_ltr_ignore_show(struct seq_file *s, void *unused)
{
	return 0;
}
DEFINE_SHOW_STORE_ATTRIBUTE(pmc_core_ltr_ignore);

static ssize_t pmc_core_ltr_restore_write(struct file *file,
					  const char __user *userbuf,
					  size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct pmc_dev *pmcdev = s->private;

	return pmc_core_ltr_write(pmcdev, userbuf, count, 0);
}

static int pmc_core_ltr_restore_show(struct seq_file *s, void *unused)
{
	return 0;
}
DEFINE_SHOW_STORE_ATTRIBUTE(pmc_core_ltr_restore);

static void pmc_core_slps0_dbg_latch(struct pmc_dev *pmcdev, bool reset)
{
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	const struct pmc_reg_map *map = pmc->map;
	u32 fd;

	guard(mutex)(&pmcdev->lock);

	if (!reset && !slps0_dbg_latch)
		return;

	fd = pmc_core_reg_read(pmc, map->slps0_dbg_offset);
	if (reset)
		fd &= ~CNP_PMC_LATCH_SLPS0_EVENTS;
	else
		fd |= CNP_PMC_LATCH_SLPS0_EVENTS;
	pmc_core_reg_write(pmc, map->slps0_dbg_offset, fd);

	slps0_dbg_latch = false;
}

static int pmc_core_slps0_dbg_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;

	pmc_core_slps0_dbg_latch(pmcdev, false);
	pmc_core_slps0_display(pmcdev->pmcs[PMC_IDX_MAIN], NULL, s);
	pmc_core_slps0_dbg_latch(pmcdev, true);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_slps0_dbg);

static u32 convert_ltr_scale(u32 val)
{
	/*
	 * As per PCIE specification supporting document
	 * ECN_LatencyTolnReporting_14Aug08.pdf the Latency
	 * Tolerance Reporting data payload is encoded in a
	 * 3 bit scale and 10 bit value fields. Values are
	 * multiplied by the indicated scale to yield an absolute time
	 * value, expressible in a range from 1 nanosecond to
	 * 2^25*(2^10-1) = 34,326,183,936 nanoseconds.
	 *
	 * scale encoding is as follows:
	 *
	 * ----------------------------------------------
	 * |scale factor	|	Multiplier (ns)	|
	 * ----------------------------------------------
	 * |	0		|	1		|
	 * |	1		|	32		|
	 * |	2		|	1024		|
	 * |	3		|	32768		|
	 * |	4		|	1048576		|
	 * |	5		|	33554432	|
	 * |	6		|	Invalid		|
	 * |	7		|	Invalid		|
	 * ----------------------------------------------
	 */
	if (val > 5) {
		pr_warn("Invalid LTR scale factor.\n");
		return 0;
	}

	return 1U << (5 * val);
}

static int pmc_core_ltr_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	u64 decoded_snoop_ltr, decoded_non_snoop_ltr, val;
	u32 ltr_raw_data, scale;
	u16 snoop_ltr, nonsnoop_ltr;
	unsigned int i, index, ltr_index = 0;

	for (i = 0; i < ARRAY_SIZE(pmcdev->pmcs); ++i) {
		struct pmc *pmc;
		const struct pmc_bit_map *map;
		u32 ltr_ign_reg;

		pmc = pmcdev->pmcs[i];
		if (!pmc)
			continue;

		scoped_guard(mutex, &pmcdev->lock)
			ltr_ign_reg = pmc_core_reg_read(pmc, pmc->map->ltr_ignore_offset);

		map = pmc->map->ltr_show_sts;
		for (index = 0; map[index].name; index++) {
			bool ltr_ign_data;

			if (index > pmc->map->ltr_ignore_max)
				ltr_ign_data = false;
			else
				ltr_ign_data = ltr_ign_reg & BIT(index);

			decoded_snoop_ltr = decoded_non_snoop_ltr = 0;
			ltr_raw_data = pmc_core_reg_read(pmc,
							 map[index].bit_mask);
			snoop_ltr = ltr_raw_data & ~MTPMC_MASK;
			nonsnoop_ltr = (ltr_raw_data >> 0x10) & ~MTPMC_MASK;

			if (FIELD_GET(LTR_REQ_NONSNOOP, ltr_raw_data)) {
				scale = FIELD_GET(LTR_DECODED_SCALE, nonsnoop_ltr);
				val = FIELD_GET(LTR_DECODED_VAL, nonsnoop_ltr);
				decoded_non_snoop_ltr = val * convert_ltr_scale(scale);
			}
			if (FIELD_GET(LTR_REQ_SNOOP, ltr_raw_data)) {
				scale = FIELD_GET(LTR_DECODED_SCALE, snoop_ltr);
				val = FIELD_GET(LTR_DECODED_VAL, snoop_ltr);
				decoded_snoop_ltr = val * convert_ltr_scale(scale);
			}

			seq_printf(s, "%d\tPMC%d:%-32s\tLTR: RAW: 0x%-16x\tNon-Snoop(ns): %-16llu\tSnoop(ns): %-16llu\tLTR_IGNORE: %d\n",
				   ltr_index, i, map[index].name, ltr_raw_data,
				   decoded_non_snoop_ltr,
				   decoded_snoop_ltr, ltr_ign_data);
			ltr_index++;
		}
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_ltr);

static int pmc_core_s0ix_blocker_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	unsigned int pmcidx;

	for (pmcidx = 0; pmcidx < ARRAY_SIZE(pmcdev->pmcs); pmcidx++) {
		const struct pmc_bit_map **maps;
		unsigned int arr_size, r_idx;
		u32 offset, counter;
		struct pmc *pmc;

		pmc = pmcdev->pmcs[pmcidx];
		if (!pmc)
			continue;
		maps = pmc->map->s0ix_blocker_maps;
		offset = pmc->map->s0ix_blocker_offset;
		arr_size = pmc_core_lpm_get_arr_size(maps);

		for (r_idx = 0; r_idx < arr_size; r_idx++) {
			const struct pmc_bit_map *map;

			for (map = maps[r_idx]; map->name; map++) {
				if (!map->blk)
					continue;
				counter = pmc_core_reg_read(pmc, offset);
				seq_printf(s, "PMC%d:%-30s %-30d\n", pmcidx,
					   map->name, counter);
				offset += map->blk * S0IX_BLK_SIZE;
			}
		}
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_s0ix_blocker);

static void pmc_core_ltr_ignore_all(struct pmc_dev *pmcdev)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pmcdev->pmcs); i++) {
		struct pmc *pmc;
		u32 ltr_ign;

		pmc = pmcdev->pmcs[i];
		if (!pmc)
			continue;

		guard(mutex)(&pmcdev->lock);
		pmc->ltr_ign = pmc_core_reg_read(pmc, pmc->map->ltr_ignore_offset);

		/* ltr_ignore_max is the max index value for LTR ignore register */
		ltr_ign = pmc->ltr_ign | GENMASK(pmc->map->ltr_ignore_max, 0);
		pmc_core_reg_write(pmc, pmc->map->ltr_ignore_offset, ltr_ign);
	}

	/*
	 * Ignoring ME during suspend is blocking platforms with ADL PCH to get to
	 * deeper S0ix substate.
	 */
	pmc_core_send_ltr_ignore(pmcdev, 6, 0);
}

static void pmc_core_ltr_restore_all(struct pmc_dev *pmcdev)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pmcdev->pmcs); i++) {
		struct pmc *pmc;

		pmc = pmcdev->pmcs[i];
		if (!pmc)
			continue;

		guard(mutex)(&pmcdev->lock);
		pmc_core_reg_write(pmc, pmc->map->ltr_ignore_offset, pmc->ltr_ign);
	}
}

static inline u64 adjust_lpm_residency(struct pmc *pmc, u32 offset,
				       const int lpm_adj_x2)
{
	u64 lpm_res = pmc_core_reg_read(pmc, offset);

	return GET_X2_COUNTER((u64)lpm_adj_x2 * lpm_res);
}

static int pmc_core_substate_res_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	const int lpm_adj_x2 = pmc->map->lpm_res_counter_step_x2;
	u32 offset = pmc->map->lpm_residency_offset;
	int mode;

	seq_printf(s, "%-10s %-15s\n", "Substate", "Residency");

	pmc_for_each_mode(mode, pmcdev) {
		seq_printf(s, "%-10s %-15llu\n", pmc_lpm_modes[mode],
			   adjust_lpm_residency(pmc, offset + (4 * mode), lpm_adj_x2));
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_substate_res);

static int pmc_core_substate_sts_regs_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pmcdev->pmcs); ++i) {
		struct pmc *pmc = pmcdev->pmcs[i];
		const struct pmc_bit_map **maps;
		u32 offset;

		if (!pmc)
			continue;
		maps = pmc->map->lpm_sts;
		offset = pmc->map->lpm_status_offset;
		pmc_core_lpm_display(pmc, NULL, s, offset, i, "STATUS", maps);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_substate_sts_regs);

static int pmc_core_substate_l_sts_regs_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pmcdev->pmcs); ++i) {
		struct pmc *pmc = pmcdev->pmcs[i];
		const struct pmc_bit_map **maps;
		u32 offset;

		if (!pmc)
			continue;
		maps = pmc->map->lpm_sts;
		offset = pmc->map->lpm_live_status_offset;
		pmc_core_lpm_display(pmc, NULL, s, offset, i, "LIVE_STATUS", maps);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_substate_l_sts_regs);

static void pmc_core_substate_req_header_show(struct seq_file *s, int pmc_index,
					      enum header_type type)
{
	struct pmc_dev *pmcdev = s->private;
	int mode;

	seq_printf(s, "%40s |", "Element");
	pmc_for_each_mode(mode, pmcdev)
		seq_printf(s, " %9s |", pmc_lpm_modes[mode]);

	if (type == HEADER_STATUS) {
		seq_printf(s, " %9s |", "Status");
		seq_printf(s, " %11s |\n", "Live Status");
	} else {
		seq_printf(s, " %9s |\n", "Value");
	}
}

static int pmc_core_substate_blk_req_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	unsigned int pmc_idx;

	for (pmc_idx = 0; pmc_idx < ARRAY_SIZE(pmcdev->pmcs); pmc_idx++) {
		const struct pmc_bit_map **maps;
		unsigned int arr_size, r_idx;
		u32 offset, counter;
		u32 *lpm_req_regs;
		struct pmc *pmc;

		pmc = pmcdev->pmcs[pmc_idx];
		if (!pmc || !pmc->lpm_req_regs)
			continue;

		lpm_req_regs = pmc->lpm_req_regs;
		maps = pmc->map->s0ix_blocker_maps;
		offset = pmc->map->s0ix_blocker_offset;
		arr_size = pmc_core_lpm_get_arr_size(maps);

		/* Display the header */
		pmc_core_substate_req_header_show(s, pmc_idx, HEADER_VALUE);

		for (r_idx = 0; r_idx < arr_size; r_idx++) {
			const struct pmc_bit_map *map;

			for (map = maps[r_idx]; map->name; map++) {
				int mode;

				if (!map->blk)
					continue;

				counter = pmc_core_reg_read(pmc, offset);
				seq_printf(s, "pmc%u: %34s |", pmc_idx, map->name);
				pmc_for_each_mode(mode, pmcdev) {
					bool required = *lpm_req_regs & BIT(mode);

					seq_printf(s, " %9s |", required ? "Required" : " ");
				}
				seq_printf(s, " %9u |\n", counter);
				offset += map->blk * S0IX_BLK_SIZE;
				lpm_req_regs++;
			}
		}
	}
	return 0;
}

static int pmc_core_substate_blk_req_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmc_core_substate_blk_req_show, inode->i_private);
}

const struct file_operations pmc_core_substate_blk_req_fops = {
	.owner		= THIS_MODULE,
	.open		= pmc_core_substate_blk_req_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int pmc_core_substate_req_regs_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	u32 sts_offset;
	u32 sts_offset_live;
	u32 *lpm_req_regs;
	unsigned int mp, pmc_index;
	int num_maps;

	for (pmc_index = 0; pmc_index < ARRAY_SIZE(pmcdev->pmcs); ++pmc_index) {
		struct pmc *pmc = pmcdev->pmcs[pmc_index];
		const struct pmc_bit_map **maps;

		if (!pmc)
			continue;

		maps = pmc->map->lpm_sts;
		num_maps = pmc->map->lpm_num_maps;
		sts_offset = pmc->map->lpm_status_offset;
		sts_offset_live = pmc->map->lpm_live_status_offset;
		lpm_req_regs = pmc->lpm_req_regs;

		/*
		 * When there are multiple PMCs, though the PMC may exist, the
		 * requirement register discovery could have failed so check
		 * before accessing.
		 */
		if (!lpm_req_regs)
			continue;

		/* Display the header */
		pmc_core_substate_req_header_show(s, pmc_index, HEADER_STATUS);

		/* Loop over maps */
		for (mp = 0; mp < num_maps; mp++) {
			u32 req_mask = 0;
			u32 lpm_status;
			u32 lpm_status_live;
			const struct pmc_bit_map *map;
			int mode, i, len = 32;

			/*
			 * Capture the requirements and create a mask so that we only
			 * show an element if it's required for at least one of the
			 * enabled low power modes
			 */
			pmc_for_each_mode(mode, pmcdev)
				req_mask |= lpm_req_regs[mp + (mode * num_maps)];

			/* Get the last latched status for this map */
			lpm_status = pmc_core_reg_read(pmc, sts_offset + (mp * 4));

			/* Get the runtime status for this map */
			lpm_status_live = pmc_core_reg_read(pmc, sts_offset_live + (mp * 4));

			/*  Loop over elements in this map */
			map = maps[mp];
			for (i = 0; map[i].name && i < len; i++) {
				u32 bit_mask = map[i].bit_mask;

				if (!(bit_mask & req_mask)) {
					/*
					 * Not required for any enabled states
					 * so don't display
					 */
					continue;
				}

				/* Display the element name in the first column */
				seq_printf(s, "pmc%d: %34s |", pmc_index, map[i].name);

				/* Loop over the enabled states and display if required */
				pmc_for_each_mode(mode, pmcdev) {
					bool required = lpm_req_regs[mp + (mode * num_maps)] &
							bit_mask;
					seq_printf(s, " %9s |", required ? "Required" : " ");
				}

				/* In Status column, show the last captured state of this agent */
				seq_printf(s, " %9s |", lpm_status & bit_mask ? "Yes" : " ");

				/* In Live status column, show the live state of this agent */
				seq_printf(s, " %11s |", lpm_status_live & bit_mask ? "Yes" : " ");

				seq_puts(s, "\n");
			}
		}
	}
	return 0;
}

static int pmc_core_substate_req_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmc_core_substate_req_regs_show, inode->i_private);
}

const struct file_operations pmc_core_substate_req_regs_fops = {
	.owner		= THIS_MODULE,
	.open		= pmc_core_substate_req_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static unsigned int pmc_core_get_crystal_freq(void)
{
	unsigned int eax_denominator, ebx_numerator, ecx_hz, edx;

	if (boot_cpu_data.cpuid_level < CPUID_LEAF_TSC)
		return 0;

	eax_denominator = ebx_numerator = ecx_hz = edx = 0;

	/* TSC/Crystal ratio, plus optionally Crystal Hz */
	cpuid(CPUID_LEAF_TSC, &eax_denominator, &ebx_numerator, &ecx_hz, &edx);

	if (ebx_numerator == 0 || eax_denominator == 0)
		return 0;

	return ecx_hz;
}

static int pmc_core_die_c6_us_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	u64 die_c6_res, count;
	int ret;

	if (!pmcdev->crystal_freq) {
		dev_warn_once(&pmcdev->pdev->dev, "Crystal frequency unavailable\n");
		return -ENXIO;
	}

	ret = pmt_telem_read(pmcdev->punit_ep, pmcdev->die_c6_offset,
			     &count, 1);
	if (ret)
		return ret;

	die_c6_res = div64_u64(count * HZ_PER_MHZ, pmcdev->crystal_freq);
	seq_printf(s, "%llu\n", die_c6_res);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_die_c6_us);

static int pmc_core_lpm_latch_mode_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	bool c10;
	u32 reg;
	int mode;

	reg = pmc_core_reg_read(pmc, pmc->map->lpm_sts_latch_en_offset);
	if (reg & LPM_STS_LATCH_MODE) {
		seq_puts(s, "c10");
		c10 = false;
	} else {
		seq_puts(s, "[c10]");
		c10 = true;
	}

	pmc_for_each_mode(mode, pmcdev) {
		if ((BIT(mode) & reg) && !c10)
			seq_printf(s, " [%s]", pmc_lpm_modes[mode]);
		else
			seq_printf(s, " %s", pmc_lpm_modes[mode]);
	}

	seq_puts(s, " clear\n");

	return 0;
}

static ssize_t pmc_core_lpm_latch_mode_write(struct file *file,
					     const char __user *userbuf,
					     size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct pmc_dev *pmcdev = s->private;
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	bool clear = false, c10 = false;
	unsigned char buf[8];
	int m, mode;
	u32 reg;

	if (count > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, userbuf, count))
		return -EFAULT;
	buf[count] = '\0';

	/*
	 * Allowed strings are:
	 *	Any enabled substate, e.g. 'S0i2.0'
	 *	'c10'
	 *	'clear'
	 */
	mode = sysfs_match_string(pmc_lpm_modes, buf);

	/* Check string matches enabled mode */
	pmc_for_each_mode(m, pmcdev)
		if (mode == m)
			break;

	if (mode != m || mode < 0) {
		if (sysfs_streq(buf, "clear"))
			clear = true;
		else if (sysfs_streq(buf, "c10"))
			c10 = true;
		else
			return -EINVAL;
	}

	if (clear) {
		guard(mutex)(&pmcdev->lock);

		reg = pmc_core_reg_read(pmc, pmc->map->etr3_offset);
		reg |= ETR3_CLEAR_LPM_EVENTS;
		pmc_core_reg_write(pmc, pmc->map->etr3_offset, reg);

		return count;
	}

	if (c10) {
		guard(mutex)(&pmcdev->lock);

		reg = pmc_core_reg_read(pmc, pmc->map->lpm_sts_latch_en_offset);
		reg &= ~LPM_STS_LATCH_MODE;
		pmc_core_reg_write(pmc, pmc->map->lpm_sts_latch_en_offset, reg);

		return count;
	}

	/*
	 * For LPM mode latching we set the latch enable bit and selected mode
	 * and clear everything else.
	 */
	reg = LPM_STS_LATCH_MODE | BIT(mode);
	guard(mutex)(&pmcdev->lock);
	pmc_core_reg_write(pmc, pmc->map->lpm_sts_latch_en_offset, reg);

	return count;
}
DEFINE_PMC_CORE_ATTR_WRITE(pmc_core_lpm_latch_mode);

static int pmc_core_pkgc_show(struct seq_file *s, void *unused)
{
	struct pmc *pmc = s->private;
	const struct pmc_bit_map *map = pmc->map->msr_sts;
	u64 pcstate_count;
	unsigned int index;

	for (index = 0; map[index].name ; index++) {
		if (rdmsrq_safe(map[index].bit_mask, &pcstate_count))
			continue;

		pcstate_count *= 1000;
		do_div(pcstate_count, tsc_khz);
		seq_printf(s, "%-8s : %llu\n", map[index].name,
			   pcstate_count);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_pkgc);

static bool pmc_core_pri_verify(u32 lpm_pri, u8 *mode_order)
{
	unsigned int i, j;

	if (!lpm_pri)
		return false;
	/*
	 * Each byte contains the priority level for 2 modes (7:4 and 3:0).
	 * In a 32 bit register this allows for describing 8 modes. Store the
	 * levels and look for values out of range.
	 */
	for (i = 0; i < 8; i++) {
		int level = lpm_pri & GENMASK(3, 0);

		if (level >= LPM_MAX_NUM_MODES)
			return false;

		mode_order[i] = level;
		lpm_pri >>= 4;
	}

	/* Check that we have unique values */
	for (i = 0; i < LPM_MAX_NUM_MODES - 1; i++)
		for (j = i + 1; j < LPM_MAX_NUM_MODES; j++)
			if (mode_order[i] == mode_order[j])
				return false;

	return true;
}

void pmc_core_get_low_power_modes(struct pmc_dev *pmcdev)
{
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	u8 pri_order[LPM_MAX_NUM_MODES] = LPM_DEFAULT_PRI;
	u8 mode_order[LPM_MAX_NUM_MODES];
	u32 lpm_pri;
	u32 lpm_en;
	unsigned int i;
	int mode, p;

	/* Use LPM Maps to indicate support for substates */
	if (!pmc->map->lpm_num_maps)
		return;

	lpm_en = pmc_core_reg_read(pmc, pmc->map->lpm_en_offset);
	/* For MTL, BIT 31 is not an lpm mode but a enable bit.
	 * Lower byte is enough to cover the number of lpm modes for all
	 * platforms and hence mask the upper 3 bytes.
	 */
	pmcdev->num_lpm_modes = hweight32(lpm_en & 0xFF);

	/* Read 32 bit LPM_PRI register */
	lpm_pri = pmc_core_reg_read(pmc, pmc->map->lpm_priority_offset);


	/*
	 * If lpm_pri value passes verification, then override the default
	 * modes here. Otherwise stick with the default.
	 */
	if (pmc_core_pri_verify(lpm_pri, mode_order))
		/* Get list of modes in priority order */
		for (mode = 0; mode < LPM_MAX_NUM_MODES; mode++)
			pri_order[mode_order[mode]] = mode;
	else
		dev_dbg(&pmcdev->pdev->dev,
			 "Assuming a default substate order for this platform\n");

	/*
	 * Loop through all modes from lowest to highest priority,
	 * and capture all enabled modes in order
	 */
	i = 0;
	for (p = LPM_MAX_NUM_MODES - 1; p >= 0; p--) {
		int mode = pri_order[p];

		if (!(BIT(mode) & lpm_en))
			continue;

		pmcdev->lpm_en_modes[i++] = mode;
	}
}

int get_primary_reg_base(struct pmc *pmc)
{
	u64 slp_s0_addr;

	if (lpit_read_residency_count_address(&slp_s0_addr)) {
		pmc->base_addr = PMC_BASE_ADDR_DEFAULT;

		if (page_is_ram(PHYS_PFN(pmc->base_addr)))
			return -ENODEV;
	} else {
		pmc->base_addr = slp_s0_addr - pmc->map->slp_s0_offset;
	}

	pmc->regbase = ioremap(pmc->base_addr, pmc->map->regmap_length);
	if (!pmc->regbase)
		return -ENOMEM;
	return 0;
}

void pmc_core_punit_pmt_init(struct pmc_dev *pmcdev, u32 guid)
{
	struct telem_endpoint *ep;
	struct pci_dev *pcidev;

	pcidev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(10, 0));
	if (!pcidev) {
		dev_err(&pmcdev->pdev->dev, "PUNIT PMT device not found.");
		return;
	}

	ep = pmt_telem_find_and_register_endpoint(pcidev, guid, 0);
	pci_dev_put(pcidev);
	if (IS_ERR(ep)) {
		dev_err(&pmcdev->pdev->dev,
			"pmc_core: couldn't get DMU telem endpoint %ld",
			PTR_ERR(ep));
		return;
	}

	pmcdev->punit_ep = ep;

	pmcdev->has_die_c6 = true;
	pmcdev->die_c6_offset = MTL_PMT_DMU_DIE_C6_OFFSET;
}

void pmc_core_set_device_d3(unsigned int device)
{
	struct pci_dev *pcidev;

	pcidev = pci_get_device(PCI_VENDOR_ID_INTEL, device, NULL);
	if (pcidev) {
		if (!device_trylock(&pcidev->dev)) {
			pci_dev_put(pcidev);
			return;
		}
		if (!pcidev->dev.driver) {
			dev_info(&pcidev->dev, "Setting to D3hot\n");
			pci_set_power_state(pcidev, PCI_D3hot);
		}
		device_unlock(&pcidev->dev);
		pci_dev_put(pcidev);
	}
}

static bool pmc_core_is_pson_residency_enabled(struct pmc_dev *pmcdev)
{
	struct platform_device *pdev = pmcdev->pdev;
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	u8 val;

	if (!adev)
		return false;

	if (fwnode_property_read_u8(acpi_fwnode_handle(adev),
				    "intel-cec-pson-switching-enabled-in-s0",
				    &val))
		return false;

	return val == 1;
}

static void pmc_core_dbgfs_unregister(struct pmc_dev *pmcdev)
{
	debugfs_remove_recursive(pmcdev->dbgfs_dir);
}

static void pmc_core_dbgfs_register(struct pmc_dev *pmcdev, struct pmc_dev_info *pmc_dev_info)
{
	struct pmc *primary_pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	struct dentry *dir;

	dir = debugfs_create_dir("pmc_core", NULL);
	pmcdev->dbgfs_dir = dir;

	debugfs_create_file("slp_s0_residency_usec", 0444, dir, primary_pmc,
			    &pmc_core_dev_state);

	if (primary_pmc->map->pfear_sts)
		debugfs_create_file("pch_ip_power_gating_status", 0444, dir,
				    pmcdev, &pmc_core_ppfear_fops);

	debugfs_create_file("ltr_ignore", 0644, dir, pmcdev,
			    &pmc_core_ltr_ignore_fops);

	debugfs_create_file("ltr_restore", 0200, dir, pmcdev, &pmc_core_ltr_restore_fops);

	debugfs_create_file("ltr_show", 0444, dir, pmcdev, &pmc_core_ltr_fops);

	if (primary_pmc->map->s0ix_blocker_maps)
		debugfs_create_file("s0ix_blocker", 0444, dir, pmcdev, &pmc_core_s0ix_blocker_fops);

	debugfs_create_file("package_cstate_show", 0444, dir, primary_pmc,
			    &pmc_core_pkgc_fops);

	if (primary_pmc->map->pll_sts)
		debugfs_create_file("pll_status", 0444, dir, pmcdev,
				    &pmc_core_pll_fops);

	if (primary_pmc->map->mphy_sts)
		debugfs_create_file("mphy_core_lanes_power_gating_status",
				    0444, dir, pmcdev,
				    &pmc_core_mphy_pg_fops);

	if (primary_pmc->map->slps0_dbg_maps) {
		debugfs_create_file("slp_s0_debug_status", 0444,
				    dir, pmcdev,
				    &pmc_core_slps0_dbg_fops);

		debugfs_create_bool("slp_s0_dbg_latch", 0644,
				    dir, &slps0_dbg_latch);
	}

	if (primary_pmc->map->lpm_en_offset) {
		debugfs_create_file("substate_residencies", 0444,
				    pmcdev->dbgfs_dir, pmcdev,
				    &pmc_core_substate_res_fops);
	}

	if (primary_pmc->map->lpm_status_offset) {
		debugfs_create_file("substate_status_registers", 0444,
				    pmcdev->dbgfs_dir, pmcdev,
				    &pmc_core_substate_sts_regs_fops);
		debugfs_create_file("substate_live_status_registers", 0444,
				    pmcdev->dbgfs_dir, pmcdev,
				    &pmc_core_substate_l_sts_regs_fops);
		debugfs_create_file("lpm_latch_mode", 0644,
				    pmcdev->dbgfs_dir, pmcdev,
				    &pmc_core_lpm_latch_mode_fops);
	}

	if (primary_pmc->lpm_req_regs) {
		debugfs_create_file("substate_requirements", 0444,
				    pmcdev->dbgfs_dir, pmcdev,
				    pmc_dev_info->sub_req_show);
	}

	if (primary_pmc->map->pson_residency_offset && pmc_core_is_pson_residency_enabled(pmcdev)) {
		debugfs_create_file("pson_residency_usec", 0444,
				    pmcdev->dbgfs_dir, primary_pmc, &pmc_core_pson_residency);
	}

	if (pmcdev->has_die_c6) {
		debugfs_create_file("die_c6_us_show", 0444,
				    pmcdev->dbgfs_dir, pmcdev,
				    &pmc_core_die_c6_us_fops);
	}
}

static u32 pmc_core_find_guid(struct pmc_info *list, const struct pmc_reg_map *map)
{
	for (; list->map; ++list)
		if (list->map == map)
			return list->guid;

	return 0;
}

/*
 * This function retrieves low power mode requirement data from PMC Low
 * Power Mode (LPM) table.
 *
 * In telemetry space, the LPM table contains a 4 byte header followed
 * by 8 consecutive mode blocks (one for each LPM mode). Each block
 * has a 4 byte header followed by a set of registers that describe the
 * IP state requirements for the given mode. The IP mapping is platform
 * specific but the same for each block, making for easy analysis.
 * Platforms only use a subset of the space to track the requirements
 * for their IPs. Callers provide the requirement registers they use as
 * a list of indices. Each requirement register is associated with an
 * IP map that's maintained by the caller.
 *
 * Header
 * +----+----------------------------+----------------------------+
 * |  0 |      REVISION              |      ENABLED MODES         |
 * +----+--------------+-------------+-------------+--------------+
 *
 * Low Power Mode 0 Block
 * +----+--------------+-------------+-------------+--------------+
 * |  1 |     SUB ID   |     SIZE    |   MAJOR     |   MINOR      |
 * +----+--------------+-------------+-------------+--------------+
 * |  2 |           LPM0 Requirements 0                           |
 * +----+---------------------------------------------------------+
 * |    |                  ...                                    |
 * +----+---------------------------------------------------------+
 * | 29 |           LPM0 Requirements 27                          |
 * +----+---------------------------------------------------------+
 *
 * ...
 *
 * Low Power Mode 7 Block
 * +----+--------------+-------------+-------------+--------------+
 * |    |     SUB ID   |     SIZE    |   MAJOR     |   MINOR      |
 * +----+--------------+-------------+-------------+--------------+
 * | 60 |           LPM7 Requirements 0                           |
 * +----+---------------------------------------------------------+
 * |    |                  ...                                    |
 * +----+---------------------------------------------------------+
 * | 87 |           LPM7 Requirements 27                          |
 * +----+---------------------------------------------------------+
 *
 */
int pmc_core_pmt_get_lpm_req(struct pmc_dev *pmcdev, struct pmc *pmc, struct telem_endpoint *ep)
{
	const u8 *lpm_indices;
	int num_maps, mode_offset = 0;
	int ret, mode;
	int lpm_size;

	lpm_indices = pmc->map->lpm_reg_index;
	num_maps = pmc->map->lpm_num_maps;
	lpm_size = LPM_MAX_NUM_MODES * num_maps;

	pmc->lpm_req_regs = devm_kzalloc(&pmcdev->pdev->dev,
					 lpm_size * sizeof(u32),
					 GFP_KERNEL);
	if (!pmc->lpm_req_regs)
		return -ENOMEM;

	mode_offset = LPM_HEADER_OFFSET + LPM_MODE_OFFSET;
	pmc_for_each_mode(mode, pmcdev) {
		u32 *req_offset = pmc->lpm_req_regs + (mode * num_maps);
		int m;

		for (m = 0; m < num_maps; m++) {
			u8 sample_id = lpm_indices[m] + mode_offset;

			ret = pmt_telem_read32(ep, sample_id, req_offset, 1);
			if (ret) {
				dev_err(&pmcdev->pdev->dev,
					"couldn't read Low Power Mode requirements: %d\n", ret);
				return ret;
			}
			++req_offset;
		}
		mode_offset += LPM_REG_COUNT + LPM_MODE_OFFSET;
	}
	return ret;
}

int pmc_core_pmt_get_blk_sub_req(struct pmc_dev *pmcdev, struct pmc *pmc,
				 struct telem_endpoint *ep)
{
	u32 num_blocker, sample_offset;
	unsigned int index;
	u32 *req_offset;
	int ret;

	num_blocker = pmc->map->num_s0ix_blocker;
	sample_offset = pmc->map->blocker_req_offset;

	pmc->lpm_req_regs = devm_kcalloc(&pmcdev->pdev->dev, num_blocker,
					 sizeof(u32), GFP_KERNEL);
	if (!pmc->lpm_req_regs)
		return -ENOMEM;

	req_offset = pmc->lpm_req_regs;
	for (index = 0; index < num_blocker; index++, req_offset++) {
		ret = pmt_telem_read32(ep, index + sample_offset, req_offset, 1);
		if (ret) {
			dev_err(&pmcdev->pdev->dev,
				"couldn't read Low Power Mode requirements: %d\n", ret);
			return ret;
		}
	}
	return 0;
}

static int pmc_core_get_telem_info(struct pmc_dev *pmcdev, struct pmc_dev_info *pmc_dev_info)
{
	struct pci_dev *pcidev __free(pci_dev_put) = NULL;
	struct telem_endpoint *ep;
	unsigned int i;
	u32 guid;
	int ret;

	pcidev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(20, pmc_dev_info->pci_func));
	if (!pcidev)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(pmcdev->pmcs); ++i) {
		struct pmc *pmc;

		pmc = pmcdev->pmcs[i];
		if (!pmc)
			continue;

		guid = pmc_core_find_guid(pmcdev->regmap_list, pmc->map);
		if (!guid)
			return -ENXIO;

		ep = pmt_telem_find_and_register_endpoint(pcidev, guid, 0);
		if (IS_ERR(ep)) {
			dev_dbg(&pmcdev->pdev->dev, "couldn't get telem endpoint %pe", ep);
			return -EPROBE_DEFER;
		}

		ret = pmc_dev_info->sub_req(pmcdev, pmc, ep);
		pmt_telem_unregister_endpoint(ep);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pmc_reg_map *pmc_core_find_regmap(struct pmc_info *list, u16 devid)
{
	for (; list->map; ++list)
		if (devid == list->devid)
			return list->map;

	return NULL;
}

static int pmc_core_pmc_add(struct pmc_dev *pmcdev, unsigned int pmc_index)

{
	struct pmc_ssram_telemetry pmc_ssram_telemetry;
	const struct pmc_reg_map *map;
	struct pmc *pmc;
	int ret;

	ret = pmc_ssram_telemetry_get_pmc_info(pmc_index, &pmc_ssram_telemetry);
	if (ret)
		return ret;

	map = pmc_core_find_regmap(pmcdev->regmap_list, pmc_ssram_telemetry.devid);
	if (!map)
		return -ENODEV;

	pmc = pmcdev->pmcs[pmc_index];
	/* Memory for primary PMC has been allocated */
	if (!pmc) {
		pmc = devm_kzalloc(&pmcdev->pdev->dev, sizeof(*pmc), GFP_KERNEL);
		if (!pmc)
			return -ENOMEM;
	}

	pmc->map = map;
	pmc->base_addr = pmc_ssram_telemetry.base_addr;
	pmc->regbase = ioremap(pmc->base_addr, pmc->map->regmap_length);

	if (!pmc->regbase) {
		devm_kfree(&pmcdev->pdev->dev, pmc);
		return -ENOMEM;
	}

	pmcdev->pmcs[pmc_index] = pmc;

	return 0;
}

static int pmc_core_ssram_get_reg_base(struct pmc_dev *pmcdev)
{
	int ret;

	ret = pmc_core_pmc_add(pmcdev, PMC_IDX_MAIN);
	if (ret)
		return ret;

	pmc_core_pmc_add(pmcdev, PMC_IDX_IOE);
	pmc_core_pmc_add(pmcdev, PMC_IDX_PCH);

	return 0;
}

/*
 * When supported, ssram init is used to achieve all available PMCs.
 * If ssram init fails, this function uses legacy method to at least get the
 * primary PMC.
 */
int generic_core_init(struct pmc_dev *pmcdev, struct pmc_dev_info *pmc_dev_info)
{
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	bool ssram;
	int ret;

	pmcdev->suspend = pmc_dev_info->suspend;
	pmcdev->resume = pmc_dev_info->resume;

	ssram = pmc_dev_info->regmap_list != NULL;
	if (ssram) {
		pmcdev->regmap_list = pmc_dev_info->regmap_list;
		ret = pmc_core_ssram_get_reg_base(pmcdev);
		/*
		 * EAGAIN error code indicates Intel PMC SSRAM Telemetry driver
		 * has not finished probe and PMC info is not available yet. Try
		 * again later.
		 */
		if (ret == -EAGAIN)
			return -EPROBE_DEFER;

		if (ret) {
			dev_warn(&pmcdev->pdev->dev,
				 "Failed to get PMC info from SSRAM, %d, using legacy init\n", ret);
			ssram = false;
		}
	}

	if (!ssram) {
		pmc->map = pmc_dev_info->map;
		ret = get_primary_reg_base(pmc);
		if (ret)
			return ret;
	}

	pmc_core_get_low_power_modes(pmcdev);
	if (pmc_dev_info->dmu_guid)
		pmc_core_punit_pmt_init(pmcdev, pmc_dev_info->dmu_guid);

	if (ssram) {
		ret = pmc_core_get_telem_info(pmcdev, pmc_dev_info);
		if (ret)
			goto unmap_regbase;
	}

	return 0;

unmap_regbase:
	for (unsigned int i = 0; i < ARRAY_SIZE(pmcdev->pmcs); ++i) {
		struct pmc *pmc = pmcdev->pmcs[i];

		if (pmc && pmc->regbase)
			iounmap(pmc->regbase);
	}

	if (pmcdev->punit_ep)
		pmt_telem_unregister_endpoint(pmcdev->punit_ep);

	return ret;
}

static const struct x86_cpu_id intel_pmc_core_ids[] = {
	X86_MATCH_VFM(INTEL_SKYLAKE_L,		&spt_pmc_dev),
	X86_MATCH_VFM(INTEL_SKYLAKE,		&spt_pmc_dev),
	X86_MATCH_VFM(INTEL_KABYLAKE_L,		&spt_pmc_dev),
	X86_MATCH_VFM(INTEL_KABYLAKE,		&spt_pmc_dev),
	X86_MATCH_VFM(INTEL_CANNONLAKE_L,	&cnp_pmc_dev),
	X86_MATCH_VFM(INTEL_ICELAKE_L,		&icl_pmc_dev),
	X86_MATCH_VFM(INTEL_ICELAKE_NNPI,	&icl_pmc_dev),
	X86_MATCH_VFM(INTEL_COMETLAKE,		&cnp_pmc_dev),
	X86_MATCH_VFM(INTEL_COMETLAKE_L,	&cnp_pmc_dev),
	X86_MATCH_VFM(INTEL_TIGERLAKE_L,	&tgl_l_pmc_dev),
	X86_MATCH_VFM(INTEL_TIGERLAKE,		&tgl_pmc_dev),
	X86_MATCH_VFM(INTEL_ATOM_TREMONT,	&tgl_l_pmc_dev),
	X86_MATCH_VFM(INTEL_ATOM_TREMONT_L,	&icl_pmc_dev),
	X86_MATCH_VFM(INTEL_ROCKETLAKE,		&tgl_pmc_dev),
	X86_MATCH_VFM(INTEL_ALDERLAKE_L,	&tgl_l_pmc_dev),
	X86_MATCH_VFM(INTEL_ATOM_GRACEMONT,	&tgl_l_pmc_dev),
	X86_MATCH_VFM(INTEL_ALDERLAKE,		&adl_pmc_dev),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_P,	&tgl_l_pmc_dev),
	X86_MATCH_VFM(INTEL_RAPTORLAKE,		&adl_pmc_dev),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_S,	&adl_pmc_dev),
	X86_MATCH_VFM(INTEL_BARTLETTLAKE,       &adl_pmc_dev),
	X86_MATCH_VFM(INTEL_METEORLAKE_L,	&mtl_pmc_dev),
	X86_MATCH_VFM(INTEL_ARROWLAKE,		&arl_pmc_dev),
	X86_MATCH_VFM(INTEL_ARROWLAKE_H,	&arl_h_pmc_dev),
	X86_MATCH_VFM(INTEL_ARROWLAKE_U,	&arl_h_pmc_dev),
	X86_MATCH_VFM(INTEL_LUNARLAKE_M,	&lnl_pmc_dev),
	X86_MATCH_VFM(INTEL_PANTHERLAKE_L,	&ptl_pmc_dev),
	X86_MATCH_VFM(INTEL_WILDCATLAKE_L,	&wcl_pmc_dev),
	{}
};

MODULE_DEVICE_TABLE(x86cpu, intel_pmc_core_ids);

/*
 * This quirk can be used on those platforms where
 * the platform BIOS enforces 24Mhz crystal to shutdown
 * before PMC can assert SLP_S0#.
 */
static bool xtal_ignore;
static int quirk_xtal_ignore(const struct dmi_system_id *id)
{
	xtal_ignore = true;
	return 0;
}

static void pmc_core_xtal_ignore(struct pmc *pmc)
{
	u32 value;

	value = pmc_core_reg_read(pmc, pmc->map->pm_vric1_offset);
	/* 24MHz Crystal Shutdown Qualification Disable */
	value |= SPT_PMC_VRIC1_XTALSDQDIS;
	/* Low Voltage Mode Enable */
	value &= ~SPT_PMC_VRIC1_SLPS0LVEN;
	pmc_core_reg_write(pmc, pmc->map->pm_vric1_offset, value);
}

static const struct dmi_system_id pmc_core_dmi_table[]  = {
	{
	.callback = quirk_xtal_ignore,
	.ident = "HP Elite x2 1013 G3",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "HP"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP Elite x2 1013 G3"),
		},
	},
	{}
};

static void pmc_core_do_dmi_quirks(struct pmc *pmc)
{
	dmi_check_system(pmc_core_dmi_table);

	if (xtal_ignore)
		pmc_core_xtal_ignore(pmc);
}

static void pmc_core_clean_structure(struct platform_device *pdev)
{
	struct pmc_dev *pmcdev = platform_get_drvdata(pdev);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pmcdev->pmcs); ++i) {
		struct pmc *pmc = pmcdev->pmcs[i];

		if (pmc && pmc->regbase)
			iounmap(pmc->regbase);
	}

	if (pmcdev->punit_ep)
		pmt_telem_unregister_endpoint(pmcdev->punit_ep);

	platform_set_drvdata(pdev, NULL);
}

static int pmc_core_probe(struct platform_device *pdev)
{
	static bool device_initialized;
	struct pmc_dev *pmcdev;
	const struct x86_cpu_id *cpu_id;
	struct pmc_dev_info *pmc_dev_info;
	struct pmc *primary_pmc;
	int ret;

	if (device_initialized)
		return -ENODEV;

	pmcdev = devm_kzalloc(&pdev->dev, sizeof(*pmcdev), GFP_KERNEL);
	if (!pmcdev)
		return -ENOMEM;

	pmcdev->crystal_freq = pmc_core_get_crystal_freq();

	platform_set_drvdata(pdev, pmcdev);
	pmcdev->pdev = pdev;

	cpu_id = x86_match_cpu(intel_pmc_core_ids);
	if (!cpu_id)
		return -ENODEV;

	pmc_dev_info = (struct pmc_dev_info *)cpu_id->driver_data;

	/* Primary PMC */
	primary_pmc = devm_kzalloc(&pdev->dev, sizeof(*primary_pmc), GFP_KERNEL);
	if (!primary_pmc)
		return -ENOMEM;
	pmcdev->pmcs[PMC_IDX_MAIN] = primary_pmc;

	/* The last element in msr_map is empty */
	pmcdev->num_of_pkgc = ARRAY_SIZE(msr_map) - 1;
	pmcdev->pkgc_res_cnt = devm_kcalloc(&pdev->dev,
					    pmcdev->num_of_pkgc,
					    sizeof(*pmcdev->pkgc_res_cnt),
					    GFP_KERNEL);
	if (!pmcdev->pkgc_res_cnt)
		return -ENOMEM;

	ret = devm_mutex_init(&pdev->dev, &pmcdev->lock);
	if (ret)
		return ret;

	if (pmc_dev_info->init)
		ret = pmc_dev_info->init(pmcdev, pmc_dev_info);
	else
		ret = generic_core_init(pmcdev, pmc_dev_info);

	if (ret) {
		platform_set_drvdata(pdev, NULL);
		return ret;
	}

	pmcdev->pmc_xram_read_bit = pmc_core_check_read_lock_bit(primary_pmc);
	pmc_core_do_dmi_quirks(primary_pmc);

	pmc_core_dbgfs_register(pmcdev, pmc_dev_info);
	pm_report_max_hw_sleep(FIELD_MAX(SLP_S0_RES_COUNTER_MASK) *
			       pmc_core_adjust_slp_s0_step(primary_pmc, 1));

	device_initialized = true;
	dev_info(&pdev->dev, " initialized\n");

	return 0;
}

static void pmc_core_remove(struct platform_device *pdev)
{
	struct pmc_dev *pmcdev = platform_get_drvdata(pdev);
	pmc_core_dbgfs_unregister(pmcdev);
	pmc_core_clean_structure(pdev);
}

static bool warn_on_s0ix_failures;
module_param(warn_on_s0ix_failures, bool, 0644);
MODULE_PARM_DESC(warn_on_s0ix_failures, "Check and warn for S0ix failures");

static bool ltr_ignore_all_suspend = true;
module_param(ltr_ignore_all_suspend, bool, 0644);
MODULE_PARM_DESC(ltr_ignore_all_suspend, "Ignore all LTRs during suspend");

static __maybe_unused int pmc_core_suspend(struct device *dev)
{
	struct pmc_dev *pmcdev = dev_get_drvdata(dev);
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	unsigned int i;

	if (pmcdev->suspend)
		pmcdev->suspend(pmcdev);

	if (ltr_ignore_all_suspend)
		pmc_core_ltr_ignore_all(pmcdev);

	/* Check if the syspend will actually use S0ix */
	if (pm_suspend_via_firmware())
		return 0;

	/* Save PKGC residency for checking later */
	for (i = 0; i < pmcdev->num_of_pkgc; i++) {
		if (rdmsrq_safe(msr_map[i].bit_mask, &pmcdev->pkgc_res_cnt[i]))
			return -EIO;
	}

	/* Save S0ix residency for checking later */
	if (pmc_core_dev_state_get(pmc, &pmcdev->s0ix_counter))
		return -EIO;

	return 0;
}

static inline bool pmc_core_is_deepest_pkgc_failed(struct pmc_dev *pmcdev)
{
	u32 deepest_pkgc_msr = msr_map[pmcdev->num_of_pkgc - 1].bit_mask;
	u64 deepest_pkgc_residency;

	if (rdmsrq_safe(deepest_pkgc_msr, &deepest_pkgc_residency))
		return false;

	if (deepest_pkgc_residency == pmcdev->pkgc_res_cnt[pmcdev->num_of_pkgc - 1])
		return true;

	return false;
}

static inline bool pmc_core_is_s0ix_failed(struct pmc_dev *pmcdev)
{
	u64 s0ix_counter;

	if (pmc_core_dev_state_get(pmcdev->pmcs[PMC_IDX_MAIN], &s0ix_counter))
		return false;

	pm_report_hw_sleep_time((u32)(s0ix_counter - pmcdev->s0ix_counter));

	if (s0ix_counter == pmcdev->s0ix_counter)
		return true;

	return false;
}

int pmc_core_resume_common(struct pmc_dev *pmcdev)
{
	struct device *dev = &pmcdev->pdev->dev;
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	const struct pmc_bit_map **maps = pmc->map->lpm_sts;
	int offset = pmc->map->lpm_status_offset;
	unsigned int i;

	/* Check if the syspend used S0ix */
	if (pm_suspend_via_firmware())
		return 0;

	if (!pmc_core_is_s0ix_failed(pmcdev))
		return 0;

	if (!warn_on_s0ix_failures)
		return 0;

	if (pmc_core_is_deepest_pkgc_failed(pmcdev)) {
		/* S0ix failed because of deepest PKGC entry failure */
		dev_info(dev, "CPU did not enter %s!!! (%s cnt=0x%llx)\n",
			 msr_map[pmcdev->num_of_pkgc - 1].name,
			 msr_map[pmcdev->num_of_pkgc - 1].name,
			 pmcdev->pkgc_res_cnt[pmcdev->num_of_pkgc - 1]);

		for (i = 0; i < pmcdev->num_of_pkgc; i++) {
			u64 pc_cnt;

			if (!rdmsrq_safe(msr_map[i].bit_mask, &pc_cnt)) {
				dev_info(dev, "Prev %s cnt = 0x%llx, Current %s cnt = 0x%llx\n",
					 msr_map[i].name, pmcdev->pkgc_res_cnt[i],
					 msr_map[i].name, pc_cnt);
			}
		}
		return 0;
	}

	/* The real interesting case - S0ix failed - lets ask PMC why. */
	dev_warn(dev, "CPU did not enter SLP_S0!!! (S0ix cnt=%llu)\n",
		 pmcdev->s0ix_counter);

	if (pmc->map->slps0_dbg_maps)
		pmc_core_slps0_display(pmc, dev, NULL);

	for (i = 0; i < ARRAY_SIZE(pmcdev->pmcs); ++i) {
		struct pmc *pmc = pmcdev->pmcs[i];

		if (!pmc)
			continue;
		if (pmc->map->lpm_sts)
			pmc_core_lpm_display(pmc, dev, NULL, offset, i, "STATUS", maps);
	}

	return 0;
}

static __maybe_unused int pmc_core_resume(struct device *dev)
{
	struct pmc_dev *pmcdev = dev_get_drvdata(dev);

	if (ltr_ignore_all_suspend)
		pmc_core_ltr_restore_all(pmcdev);

	if (pmcdev->resume)
		return pmcdev->resume(pmcdev);

	return pmc_core_resume_common(pmcdev);
}

static const struct dev_pm_ops pmc_core_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pmc_core_suspend, pmc_core_resume)
};

static const struct acpi_device_id pmc_core_acpi_ids[] = {
	{"INT33A1", 0}, /* _HID for Intel Power Engine, _CID PNP0D80*/
	{ }
};
MODULE_DEVICE_TABLE(acpi, pmc_core_acpi_ids);

static struct platform_driver pmc_core_driver = {
	.driver = {
		.name = "intel_pmc_core",
		.acpi_match_table = ACPI_PTR(pmc_core_acpi_ids),
		.pm = &pmc_core_pm_ops,
		.dev_groups = pmc_dev_groups,
	},
	.probe = pmc_core_probe,
	.remove = pmc_core_remove,
};

module_platform_driver(pmc_core_driver);

MODULE_IMPORT_NS("INTEL_PMT_TELEMETRY");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel PMC Core Driver");

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

#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/suspend.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/msr.h>
#include <asm/tsc.h>

#include "core.h"

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
	int err;

	if (!map->etr3_offset)
		return -EOPNOTSUPP;

	mutex_lock(&pmcdev->lock);

	/* check if CF9 is locked */
	reg = pmc_core_reg_read(pmc, map->etr3_offset);
	if (reg & ETR3_CF9LOCK) {
		err = -EACCES;
		goto out_unlock;
	}

	/* write CF9 global reset bit */
	reg |= ETR3_CF9GR;
	pmc_core_reg_write(pmc, map->etr3_offset, reg);

	reg = pmc_core_reg_read(pmc, map->etr3_offset);
	if (!(reg & ETR3_CF9GR)) {
		err = -EIO;
		goto out_unlock;
	}

	err = 0;

out_unlock:
	mutex_unlock(&pmcdev->lock);
	return err;
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

	mutex_lock(&pmcdev->lock);
	reg = pmc_core_reg_read(pmc, map->etr3_offset);
	mutex_unlock(&pmcdev->lock);

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

	mutex_lock(&pmcdev->lock);

	reg = pmc_core_reg_read(pmc, map->etr3_offset);
	reg &= ETR3_CF9GR | ETR3_CF9LOCK;

	mutex_unlock(&pmcdev->lock);

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

static int pmc_core_lpm_get_arr_size(const struct pmc_bit_map **maps)
{
	int idx;

	for (idx = 0; maps[idx]; idx++)
		;/* Nothing */

	return idx;
}

static void pmc_core_lpm_display(struct pmc *pmc, struct device *dev,
				 struct seq_file *s, u32 offset, int pmc_index,
				 const char *str,
				 const struct pmc_bit_map **maps)
{
	int index, idx, len = 32, bit_mask, arr_size;
	u32 *lpm_regs;

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
	int i;

	for (i = 0; i < ARRAY_SIZE(pmcdev->pmcs); ++i) {
		struct pmc *pmc = pmcdev->pmcs[i];
		const struct pmc_bit_map **maps;
		u8 pf_regs[PPFEAR_MAX_NUM_ENTRIES];
		int index, iter, idx, ip = 0;

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
	int index, err = 0;

	if (pmcdev->pmc_xram_read_bit) {
		seq_puts(s, "Access denied: please disable PMC_READ_DISABLE setting in BIOS.");
		return 0;
	}

	mphy_core_reg_low  = (SPT_PMC_MPHY_CORE_STS_0 << 16);
	mphy_core_reg_high = (SPT_PMC_MPHY_CORE_STS_1 << 16);

	mutex_lock(&pmcdev->lock);

	if (pmc_core_send_msg(pmc, &mphy_core_reg_low) != 0) {
		err = -EBUSY;
		goto out_unlock;
	}

	msleep(10);
	val_low = pmc_core_reg_read(pmc, SPT_PMC_MFPMC_OFFSET);

	if (pmc_core_send_msg(pmc, &mphy_core_reg_high) != 0) {
		err = -EBUSY;
		goto out_unlock;
	}

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

out_unlock:
	mutex_unlock(&pmcdev->lock);
	return err;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_mphy_pg);

static int pmc_core_pll_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	const struct pmc_bit_map *map = pmc->map->pll_sts;
	u32 mphy_common_reg, val;
	int index, err = 0;

	if (pmcdev->pmc_xram_read_bit) {
		seq_puts(s, "Access denied: please disable PMC_READ_DISABLE setting in BIOS.");
		return 0;
	}

	mphy_common_reg  = (SPT_PMC_MPHY_COM_STS_0 << 16);
	mutex_lock(&pmcdev->lock);

	if (pmc_core_send_msg(pmc, &mphy_common_reg) != 0) {
		err = -EBUSY;
		goto out_unlock;
	}

	/* Observed PMC HW response latency for MTPMC-MFPMC is ~10 ms */
	msleep(10);
	val = pmc_core_reg_read(pmc, SPT_PMC_MFPMC_OFFSET);

	for (index = 0; map[index].name ; index++) {
		seq_printf(s, "%-32s\tState: %s\n",
			   map[index].name,
			   map[index].bit_mask & val ? "Active" : "Idle");
	}

out_unlock:
	mutex_unlock(&pmcdev->lock);
	return err;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_pll);

int pmc_core_send_ltr_ignore(struct pmc_dev *pmcdev, u32 value, int ignore)
{
	struct pmc *pmc;
	const struct pmc_reg_map *map;
	u32 reg;
	int pmc_index, ltr_index;

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

	mutex_lock(&pmcdev->lock);

	reg = pmc_core_reg_read(pmc, map->ltr_ignore_offset);
	if (ignore)
		reg |= BIT(ltr_index);
	else
		reg &= ~BIT(ltr_index);
	pmc_core_reg_write(pmc, map->ltr_ignore_offset, reg);

	mutex_unlock(&pmcdev->lock);

	return 0;
}

static ssize_t pmc_core_ltr_ignore_write(struct file *file,
					 const char __user *userbuf,
					 size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct pmc_dev *pmcdev = s->private;
	u32 buf_size, value;
	int err;

	buf_size = min_t(u32, count, 64);

	err = kstrtou32_from_user(userbuf, buf_size, 10, &value);
	if (err)
		return err;

	err = pmc_core_send_ltr_ignore(pmcdev, value, 1);

	return err == 0 ? count : err;
}

static int pmc_core_ltr_ignore_show(struct seq_file *s, void *unused)
{
	return 0;
}

static int pmc_core_ltr_ignore_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmc_core_ltr_ignore_show, inode->i_private);
}

static const struct file_operations pmc_core_ltr_ignore_ops = {
	.open           = pmc_core_ltr_ignore_open,
	.read           = seq_read,
	.write          = pmc_core_ltr_ignore_write,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static void pmc_core_slps0_dbg_latch(struct pmc_dev *pmcdev, bool reset)
{
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	const struct pmc_reg_map *map = pmc->map;
	u32 fd;

	mutex_lock(&pmcdev->lock);

	if (!reset && !slps0_dbg_latch)
		goto out_unlock;

	fd = pmc_core_reg_read(pmc, map->slps0_dbg_offset);
	if (reset)
		fd &= ~CNP_PMC_LATCH_SLPS0_EVENTS;
	else
		fd |= CNP_PMC_LATCH_SLPS0_EVENTS;
	pmc_core_reg_write(pmc, map->slps0_dbg_offset, fd);

	slps0_dbg_latch = false;

out_unlock:
	mutex_unlock(&pmcdev->lock);
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
	u64 decoded_snoop_ltr, decoded_non_snoop_ltr;
	u32 ltr_raw_data, scale, val;
	u16 snoop_ltr, nonsnoop_ltr;
	int i, index, ltr_index = 0;

	for (i = 0; i < ARRAY_SIZE(pmcdev->pmcs); ++i) {
		struct pmc *pmc = pmcdev->pmcs[i];
		const struct pmc_bit_map *map;

		if (!pmc)
			continue;

		map = pmc->map->ltr_show_sts;
		for (index = 0; map[index].name; index++) {
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

			seq_printf(s, "%d\tPMC%d:%-32s\tLTR: RAW: 0x%-16x\tNon-Snoop(ns): %-16llu\tSnoop(ns): %-16llu\n",
				   ltr_index, i, map[index].name, ltr_raw_data,
				   decoded_non_snoop_ltr,
				   decoded_snoop_ltr);
			ltr_index++;
		}
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_ltr);

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
	int i, mode;

	seq_printf(s, "%-10s %-15s\n", "Substate", "Residency");

	pmc_for_each_mode(i, mode, pmcdev) {
		seq_printf(s, "%-10s %-15llu\n", pmc_lpm_modes[mode],
			   adjust_lpm_residency(pmc, offset + (4 * mode), lpm_adj_x2));
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_substate_res);

static int pmc_core_substate_sts_regs_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	int i;

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
	int i;

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

static void pmc_core_substate_req_header_show(struct seq_file *s)
{
	struct pmc_dev *pmcdev = s->private;
	int i, mode;

	seq_printf(s, "%30s |", "Element");
	pmc_for_each_mode(i, mode, pmcdev)
		seq_printf(s, " %9s |", pmc_lpm_modes[mode]);

	seq_printf(s, " %9s |\n", "Status");
}

static int pmc_core_substate_req_regs_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	const struct pmc_bit_map **maps = pmc->map->lpm_sts;
	const struct pmc_bit_map *map;
	const int num_maps = pmc->map->lpm_num_maps;
	u32 sts_offset = pmc->map->lpm_status_offset;
	u32 *lpm_req_regs = pmc->lpm_req_regs;
	int mp;

	/* Display the header */
	pmc_core_substate_req_header_show(s);

	/* Loop over maps */
	for (mp = 0; mp < num_maps; mp++) {
		u32 req_mask = 0;
		u32 lpm_status;
		int mode, idx, i, len = 32;

		/*
		 * Capture the requirements and create a mask so that we only
		 * show an element if it's required for at least one of the
		 * enabled low power modes
		 */
		pmc_for_each_mode(idx, mode, pmcdev)
			req_mask |= lpm_req_regs[mp + (mode * num_maps)];

		/* Get the last latched status for this map */
		lpm_status = pmc_core_reg_read(pmc, sts_offset + (mp * 4));

		/*  Loop over elements in this map */
		map = maps[mp];
		for (i = 0; map[i].name && i < len; i++) {
			u32 bit_mask = map[i].bit_mask;

			if (!(bit_mask & req_mask))
				/*
				 * Not required for any enabled states
				 * so don't display
				 */
				continue;

			/* Display the element name in the first column */
			seq_printf(s, "%30s |", map[i].name);

			/* Loop over the enabled states and display if required */
			pmc_for_each_mode(idx, mode, pmcdev) {
				if (lpm_req_regs[mp + (mode * num_maps)] & bit_mask)
					seq_printf(s, " %9s |",
						   "Required");
				else
					seq_printf(s, " %9s |", " ");
			}

			/* In Status column, show the last captured state of this agent */
			if (lpm_status & bit_mask)
				seq_printf(s, " %9s |", "Yes");
			else
				seq_printf(s, " %9s |", " ");

			seq_puts(s, "\n");
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_substate_req_regs);

static int pmc_core_lpm_latch_mode_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	bool c10;
	u32 reg;
	int idx, mode;

	reg = pmc_core_reg_read(pmc, pmc->map->lpm_sts_latch_en_offset);
	if (reg & LPM_STS_LATCH_MODE) {
		seq_puts(s, "c10");
		c10 = false;
	} else {
		seq_puts(s, "[c10]");
		c10 = true;
	}

	pmc_for_each_mode(idx, mode, pmcdev) {
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
	int idx, m, mode;
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
	pmc_for_each_mode(idx, m, pmcdev)
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
		mutex_lock(&pmcdev->lock);

		reg = pmc_core_reg_read(pmc, pmc->map->etr3_offset);
		reg |= ETR3_CLEAR_LPM_EVENTS;
		pmc_core_reg_write(pmc, pmc->map->etr3_offset, reg);

		mutex_unlock(&pmcdev->lock);

		return count;
	}

	if (c10) {
		mutex_lock(&pmcdev->lock);

		reg = pmc_core_reg_read(pmc, pmc->map->lpm_sts_latch_en_offset);
		reg &= ~LPM_STS_LATCH_MODE;
		pmc_core_reg_write(pmc, pmc->map->lpm_sts_latch_en_offset, reg);

		mutex_unlock(&pmcdev->lock);

		return count;
	}

	/*
	 * For LPM mode latching we set the latch enable bit and selected mode
	 * and clear everything else.
	 */
	reg = LPM_STS_LATCH_MODE | BIT(mode);
	mutex_lock(&pmcdev->lock);
	pmc_core_reg_write(pmc, pmc->map->lpm_sts_latch_en_offset, reg);
	mutex_unlock(&pmcdev->lock);

	return count;
}
DEFINE_PMC_CORE_ATTR_WRITE(pmc_core_lpm_latch_mode);

static int pmc_core_pkgc_show(struct seq_file *s, void *unused)
{
	struct pmc *pmc = s->private;
	const struct pmc_bit_map *map = pmc->map->msr_sts;
	u64 pcstate_count;
	int index;

	for (index = 0; map[index].name ; index++) {
		if (rdmsrl_safe(map[index].bit_mask, &pcstate_count))
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
	int i, j;

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

static void pmc_core_get_low_power_modes(struct platform_device *pdev)
{
	struct pmc_dev *pmcdev = platform_get_drvdata(pdev);
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	u8 pri_order[LPM_MAX_NUM_MODES] = LPM_DEFAULT_PRI;
	u8 mode_order[LPM_MAX_NUM_MODES];
	u32 lpm_pri;
	u32 lpm_en;
	int mode, i, p;

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
		dev_warn(&pdev->dev, "Assuming a default substate order for this platform\n");

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

static void pmc_core_dbgfs_unregister(struct pmc_dev *pmcdev)
{
	debugfs_remove_recursive(pmcdev->dbgfs_dir);
}

static void pmc_core_dbgfs_register(struct pmc_dev *pmcdev)
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
			    &pmc_core_ltr_ignore_ops);

	debugfs_create_file("ltr_show", 0444, dir, pmcdev, &pmc_core_ltr_fops);

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
				    &pmc_core_substate_req_regs_fops);
	}
}

static const struct x86_cpu_id intel_pmc_core_ids[] = {
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE_L,		spt_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE,		spt_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE_L,		spt_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE,		spt_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(CANNONLAKE_L,	cnp_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_L,		icl_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_NNPI,	icl_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(COMETLAKE,		cnp_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(COMETLAKE_L,		cnp_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(TIGERLAKE_L,		tgl_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(TIGERLAKE,		tgl_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_TREMONT,	tgl_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_TREMONT_L,	icl_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(ROCKETLAKE,		tgl_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE_L,		tgl_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_GRACEMONT,	tgl_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE,		adl_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE_P,        tgl_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE,		adl_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE_S,	adl_core_init),
	X86_MATCH_INTEL_FAM6_MODEL(METEORLAKE_L,	mtl_core_init),
	{}
};

MODULE_DEVICE_TABLE(x86cpu, intel_pmc_core_ids);

static const struct pci_device_id pmc_pci_ids[] = {
	{ PCI_VDEVICE(INTEL, SPT_PMC_PCI_DEVICE_ID) },
	{ }
};

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
	int i;

	for (i = 0; i < ARRAY_SIZE(pmcdev->pmcs); ++i) {
		struct pmc *pmc = pmcdev->pmcs[i];

		if (pmc)
			iounmap(pmc->regbase);
	}

	if (pmcdev->ssram_pcidev) {
		pci_dev_put(pmcdev->ssram_pcidev);
		pci_disable_device(pmcdev->ssram_pcidev);
	}
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&pmcdev->lock);
}

static int pmc_core_probe(struct platform_device *pdev)
{
	static bool device_initialized;
	struct pmc_dev *pmcdev;
	const struct x86_cpu_id *cpu_id;
	int (*core_init)(struct pmc_dev *pmcdev);
	struct pmc *primary_pmc;
	int ret;

	if (device_initialized)
		return -ENODEV;

	pmcdev = devm_kzalloc(&pdev->dev, sizeof(*pmcdev), GFP_KERNEL);
	if (!pmcdev)
		return -ENOMEM;

	platform_set_drvdata(pdev, pmcdev);
	pmcdev->pdev = pdev;

	cpu_id = x86_match_cpu(intel_pmc_core_ids);
	if (!cpu_id)
		return -ENODEV;

	core_init = (int (*)(struct pmc_dev *))cpu_id->driver_data;

	/* Primary PMC */
	primary_pmc = devm_kzalloc(&pdev->dev, sizeof(*primary_pmc), GFP_KERNEL);
	if (!primary_pmc)
		return -ENOMEM;
	pmcdev->pmcs[PMC_IDX_MAIN] = primary_pmc;

	/*
	 * Coffee Lake has CPU ID of Kaby Lake and Cannon Lake PCH. So here
	 * Sunrisepoint PCH regmap can't be used. Use Cannon Lake PCH regmap
	 * in this case.
	 */
	if (core_init == spt_core_init && !pci_dev_present(pmc_pci_ids))
		core_init = cnp_core_init;

	mutex_init(&pmcdev->lock);
	ret = core_init(pmcdev);
	if (ret) {
		pmc_core_clean_structure(pdev);
		return ret;
	}

	pmcdev->pmc_xram_read_bit = pmc_core_check_read_lock_bit(primary_pmc);
	pmc_core_get_low_power_modes(pdev);
	pmc_core_do_dmi_quirks(primary_pmc);

	pmc_core_dbgfs_register(pmcdev);
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

static __maybe_unused int pmc_core_suspend(struct device *dev)
{
	struct pmc_dev *pmcdev = dev_get_drvdata(dev);
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];

	if (pmcdev->suspend)
		pmcdev->suspend(pmcdev);

	/* Check if the syspend will actually use S0ix */
	if (pm_suspend_via_firmware())
		return 0;

	/* Save PC10 residency for checking later */
	if (rdmsrl_safe(MSR_PKG_C10_RESIDENCY, &pmcdev->pc10_counter))
		return -EIO;

	/* Save S0ix residency for checking later */
	if (pmc_core_dev_state_get(pmc, &pmcdev->s0ix_counter))
		return -EIO;

	return 0;
}

static inline bool pmc_core_is_pc10_failed(struct pmc_dev *pmcdev)
{
	u64 pc10_counter;

	if (rdmsrl_safe(MSR_PKG_C10_RESIDENCY, &pc10_counter))
		return false;

	if (pc10_counter == pmcdev->pc10_counter)
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
	int i;

	/* Check if the syspend used S0ix */
	if (pm_suspend_via_firmware())
		return 0;

	if (!pmc_core_is_s0ix_failed(pmcdev))
		return 0;

	if (!warn_on_s0ix_failures)
		return 0;

	if (pmc_core_is_pc10_failed(pmcdev)) {
		/* S0ix failed because of PC10 entry failure */
		dev_info(dev, "CPU did not enter PC10!!! (PC10 cnt=0x%llx)\n",
			 pmcdev->pc10_counter);
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
	.remove_new = pmc_core_remove,
};

module_platform_driver(pmc_core_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel PMC Core Driver");

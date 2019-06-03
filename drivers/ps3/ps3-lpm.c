// SPDX-License-Identifier: GPL-2.0-only
/*
 * PS3 Logical Performance Monitor.
 *
 *  Copyright (C) 2007 Sony Computer Entertainment Inc.
 *  Copyright 2007 Sony Corp.
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <asm/smp.h>
#include <asm/time.h>
#include <asm/ps3.h>
#include <asm/lv1call.h>
#include <asm/cell-pmu.h>


/* BOOKMARK tag macros */
#define PS3_PM_BOOKMARK_START                    0x8000000000000000ULL
#define PS3_PM_BOOKMARK_STOP                     0x4000000000000000ULL
#define PS3_PM_BOOKMARK_TAG_KERNEL               0x1000000000000000ULL
#define PS3_PM_BOOKMARK_TAG_USER                 0x3000000000000000ULL
#define PS3_PM_BOOKMARK_TAG_MASK_HI              0xF000000000000000ULL
#define PS3_PM_BOOKMARK_TAG_MASK_LO              0x0F00000000000000ULL

/* CBE PM CONTROL register macros */
#define PS3_PM_CONTROL_PPU_TH0_BOOKMARK          0x00001000
#define PS3_PM_CONTROL_PPU_TH1_BOOKMARK          0x00000800
#define PS3_PM_CONTROL_PPU_COUNT_MODE_MASK       0x000C0000
#define PS3_PM_CONTROL_PPU_COUNT_MODE_PROBLEM    0x00080000
#define PS3_WRITE_PM_MASK                        0xFFFFFFFFFFFFFFFFULL

/* CBE PM START STOP register macros */
#define PS3_PM_START_STOP_PPU_TH0_BOOKMARK_START 0x02000000
#define PS3_PM_START_STOP_PPU_TH1_BOOKMARK_START 0x01000000
#define PS3_PM_START_STOP_PPU_TH0_BOOKMARK_STOP  0x00020000
#define PS3_PM_START_STOP_PPU_TH1_BOOKMARK_STOP  0x00010000
#define PS3_PM_START_STOP_START_MASK             0xFF000000
#define PS3_PM_START_STOP_STOP_MASK              0x00FF0000

/* CBE PM COUNTER register macres */
#define PS3_PM_COUNTER_MASK_HI                   0xFFFFFFFF00000000ULL
#define PS3_PM_COUNTER_MASK_LO                   0x00000000FFFFFFFFULL

/* BASE SIGNAL GROUP NUMBER macros */
#define PM_ISLAND2_BASE_SIGNAL_GROUP_NUMBER  0
#define PM_ISLAND2_SIGNAL_GROUP_NUMBER1      6
#define PM_ISLAND2_SIGNAL_GROUP_NUMBER2      7
#define PM_ISLAND3_BASE_SIGNAL_GROUP_NUMBER  7
#define PM_ISLAND4_BASE_SIGNAL_GROUP_NUMBER  15
#define PM_SPU_TRIGGER_SIGNAL_GROUP_NUMBER   17
#define PM_SPU_EVENT_SIGNAL_GROUP_NUMBER     18
#define PM_ISLAND5_BASE_SIGNAL_GROUP_NUMBER  18
#define PM_ISLAND6_BASE_SIGNAL_GROUP_NUMBER  24
#define PM_ISLAND7_BASE_SIGNAL_GROUP_NUMBER  49
#define PM_ISLAND8_BASE_SIGNAL_GROUP_NUMBER  52
#define PM_SIG_GROUP_SPU                     41
#define PM_SIG_GROUP_SPU_TRIGGER             42
#define PM_SIG_GROUP_SPU_EVENT               43
#define PM_SIG_GROUP_MFC_MAX                 60

/**
 * struct ps3_lpm_shadow_regs - Performance monitor shadow registers.
 *
 * @pm_control: Shadow of the processor's pm_control register.
 * @pm_start_stop: Shadow of the processor's pm_start_stop register.
 * @group_control: Shadow of the processor's group_control register.
 * @debug_bus_control: Shadow of the processor's debug_bus_control register.
 *
 * The logical performance monitor provides a write-only interface to
 * these processor registers.  These shadow variables cache the processor
 * register values for reading.
 *
 * The initial value of the shadow registers at lpm creation is
 * PS3_LPM_SHADOW_REG_INIT.
 */

struct ps3_lpm_shadow_regs {
	u64 pm_control;
	u64 pm_start_stop;
	u64 group_control;
	u64 debug_bus_control;
};

#define PS3_LPM_SHADOW_REG_INIT 0xFFFFFFFF00000000ULL

/**
 * struct ps3_lpm_priv - Private lpm device data.
 *
 * @open: An atomic variable indicating the lpm driver has been opened.
 * @rights: The lpm rigths granted by the system policy module.  A logical
 *  OR of enum ps3_lpm_rights.
 * @node_id: The node id of a BE processor whose performance monitor this
 *  lpar has the right to use.
 * @pu_id: The lv1 id of the logical PU.
 * @lpm_id: The lv1 id of this lpm instance.
 * @outlet_id: The outlet created by lv1 for this lpm instance.
 * @tb_count: The number of bytes of data held in the lv1 trace buffer.
 * @tb_cache: Kernel buffer to receive the data from the lv1 trace buffer.
 *  Must be 128 byte aligned.
 * @tb_cache_size: Size of the kernel @tb_cache buffer.  Must be 128 byte
 *  aligned.
 * @tb_cache_internal: An unaligned buffer allocated by this driver to be
 *  used for the trace buffer cache when ps3_lpm_open() is called with a
 *  NULL tb_cache argument.  Otherwise unused.
 * @shadow: Processor register shadow of type struct ps3_lpm_shadow_regs.
 * @sbd: The struct ps3_system_bus_device attached to this driver.
 *
 * The trace buffer is a buffer allocated and used internally to the lv1
 * hypervisor to collect trace data.  The trace buffer cache is a guest
 * buffer that accepts the trace data from the trace buffer.
 */

struct ps3_lpm_priv {
	atomic_t open;
	u64 rights;
	u64 node_id;
	u64 pu_id;
	u64 lpm_id;
	u64 outlet_id;
	u64 tb_count;
	void *tb_cache;
	u64 tb_cache_size;
	void *tb_cache_internal;
	struct ps3_lpm_shadow_regs shadow;
	struct ps3_system_bus_device *sbd;
};

enum {
	PS3_LPM_DEFAULT_TB_CACHE_SIZE = 0x4000,
};

/**
 * lpm_priv - Static instance of the lpm data.
 *
 * Since the exported routines don't support the notion of a device
 * instance we need to hold the instance in this static variable
 * and then only allow at most one instance at a time to be created.
 */

static struct ps3_lpm_priv *lpm_priv;

static struct device *sbd_core(void)
{
	BUG_ON(!lpm_priv || !lpm_priv->sbd);
	return &lpm_priv->sbd->core;
}

/**
 * use_start_stop_bookmark - Enable the PPU bookmark trace.
 *
 * And it enables PPU bookmark triggers ONLY if the other triggers are not set.
 * The start/stop bookmarks are inserted at ps3_enable_pm() and ps3_disable_pm()
 * to start/stop LPM.
 *
 * Used to get good quality of the performance counter.
 */

enum {use_start_stop_bookmark = 1,};

void ps3_set_bookmark(u64 bookmark)
{
	/*
	 * As per the PPE book IV, to avoid bookmark loss there must
	 * not be a traced branch within 10 cycles of setting the
	 * SPRN_BKMK register.  The actual text is unclear if 'within'
	 * includes cycles before the call.
	 */

	asm volatile("nop;nop;nop;nop;nop;nop;nop;nop;nop;");
	mtspr(SPRN_BKMK, bookmark);
	asm volatile("nop;nop;nop;nop;nop;nop;nop;nop;nop;");
}
EXPORT_SYMBOL_GPL(ps3_set_bookmark);

void ps3_set_pm_bookmark(u64 tag, u64 incident, u64 th_id)
{
	u64 bookmark;

	bookmark = (get_tb() & 0x00000000FFFFFFFFULL) |
		PS3_PM_BOOKMARK_TAG_KERNEL;
	bookmark = ((tag << 56) & PS3_PM_BOOKMARK_TAG_MASK_LO) |
		(incident << 48) | (th_id << 32) | bookmark;
	ps3_set_bookmark(bookmark);
}
EXPORT_SYMBOL_GPL(ps3_set_pm_bookmark);

/**
 * ps3_read_phys_ctr - Read physical counter registers.
 *
 * Each physical counter can act as one 32 bit counter or as two 16 bit
 * counters.
 */

u32 ps3_read_phys_ctr(u32 cpu, u32 phys_ctr)
{
	int result;
	u64 counter0415;
	u64 counter2637;

	if (phys_ctr >= NR_PHYS_CTRS) {
		dev_dbg(sbd_core(), "%s:%u: phys_ctr too big: %u\n", __func__,
			__LINE__, phys_ctr);
		return 0;
	}

	result = lv1_set_lpm_counter(lpm_priv->lpm_id, 0, 0, 0, 0, &counter0415,
				     &counter2637);
	if (result) {
		dev_err(sbd_core(), "%s:%u: lv1_set_lpm_counter failed: "
			"phys_ctr %u, %s\n", __func__, __LINE__, phys_ctr,
			ps3_result(result));
		return 0;
	}

	switch (phys_ctr) {
	case 0:
		return counter0415 >> 32;
	case 1:
		return counter0415 & PS3_PM_COUNTER_MASK_LO;
	case 2:
		return counter2637 >> 32;
	case 3:
		return counter2637 & PS3_PM_COUNTER_MASK_LO;
	default:
		BUG();
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ps3_read_phys_ctr);

/**
 * ps3_write_phys_ctr - Write physical counter registers.
 *
 * Each physical counter can act as one 32 bit counter or as two 16 bit
 * counters.
 */

void ps3_write_phys_ctr(u32 cpu, u32 phys_ctr, u32 val)
{
	u64 counter0415;
	u64 counter0415_mask;
	u64 counter2637;
	u64 counter2637_mask;
	int result;

	if (phys_ctr >= NR_PHYS_CTRS) {
		dev_dbg(sbd_core(), "%s:%u: phys_ctr too big: %u\n", __func__,
			__LINE__, phys_ctr);
		return;
	}

	switch (phys_ctr) {
	case 0:
		counter0415 = (u64)val << 32;
		counter0415_mask = PS3_PM_COUNTER_MASK_HI;
		counter2637 = 0x0;
		counter2637_mask = 0x0;
		break;
	case 1:
		counter0415 = (u64)val;
		counter0415_mask = PS3_PM_COUNTER_MASK_LO;
		counter2637 = 0x0;
		counter2637_mask = 0x0;
		break;
	case 2:
		counter0415 = 0x0;
		counter0415_mask = 0x0;
		counter2637 = (u64)val << 32;
		counter2637_mask = PS3_PM_COUNTER_MASK_HI;
		break;
	case 3:
		counter0415 = 0x0;
		counter0415_mask = 0x0;
		counter2637 = (u64)val;
		counter2637_mask = PS3_PM_COUNTER_MASK_LO;
		break;
	default:
		BUG();
	}

	result = lv1_set_lpm_counter(lpm_priv->lpm_id,
				     counter0415, counter0415_mask,
				     counter2637, counter2637_mask,
				     &counter0415, &counter2637);
	if (result)
		dev_err(sbd_core(), "%s:%u: lv1_set_lpm_counter failed: "
			"phys_ctr %u, val %u, %s\n", __func__, __LINE__,
			phys_ctr, val, ps3_result(result));
}
EXPORT_SYMBOL_GPL(ps3_write_phys_ctr);

/**
 * ps3_read_ctr - Read counter.
 *
 * Read 16 or 32 bits depending on the current size of the counter.
 * Counters 4, 5, 6 & 7 are always 16 bit.
 */

u32 ps3_read_ctr(u32 cpu, u32 ctr)
{
	u32 val;
	u32 phys_ctr = ctr & (NR_PHYS_CTRS - 1);

	val = ps3_read_phys_ctr(cpu, phys_ctr);

	if (ps3_get_ctr_size(cpu, phys_ctr) == 16)
		val = (ctr < NR_PHYS_CTRS) ? (val >> 16) : (val & 0xffff);

	return val;
}
EXPORT_SYMBOL_GPL(ps3_read_ctr);

/**
 * ps3_write_ctr - Write counter.
 *
 * Write 16 or 32 bits depending on the current size of the counter.
 * Counters 4, 5, 6 & 7 are always 16 bit.
 */

void ps3_write_ctr(u32 cpu, u32 ctr, u32 val)
{
	u32 phys_ctr;
	u32 phys_val;

	phys_ctr = ctr & (NR_PHYS_CTRS - 1);

	if (ps3_get_ctr_size(cpu, phys_ctr) == 16) {
		phys_val = ps3_read_phys_ctr(cpu, phys_ctr);

		if (ctr < NR_PHYS_CTRS)
			val = (val << 16) | (phys_val & 0xffff);
		else
			val = (val & 0xffff) | (phys_val & 0xffff0000);
	}

	ps3_write_phys_ctr(cpu, phys_ctr, val);
}
EXPORT_SYMBOL_GPL(ps3_write_ctr);

/**
 * ps3_read_pm07_control - Read counter control registers.
 *
 * Each logical counter has a corresponding control register.
 */

u32 ps3_read_pm07_control(u32 cpu, u32 ctr)
{
	return 0;
}
EXPORT_SYMBOL_GPL(ps3_read_pm07_control);

/**
 * ps3_write_pm07_control - Write counter control registers.
 *
 * Each logical counter has a corresponding control register.
 */

void ps3_write_pm07_control(u32 cpu, u32 ctr, u32 val)
{
	int result;
	static const u64 mask = 0xFFFFFFFFFFFFFFFFULL;
	u64 old_value;

	if (ctr >= NR_CTRS) {
		dev_dbg(sbd_core(), "%s:%u: ctr too big: %u\n", __func__,
			__LINE__, ctr);
		return;
	}

	result = lv1_set_lpm_counter_control(lpm_priv->lpm_id, ctr, val, mask,
					     &old_value);
	if (result)
		dev_err(sbd_core(), "%s:%u: lv1_set_lpm_counter_control "
			"failed: ctr %u, %s\n", __func__, __LINE__, ctr,
			ps3_result(result));
}
EXPORT_SYMBOL_GPL(ps3_write_pm07_control);

/**
 * ps3_read_pm - Read Other LPM control registers.
 */

u32 ps3_read_pm(u32 cpu, enum pm_reg_name reg)
{
	int result = 0;
	u64 val = 0;

	switch (reg) {
	case pm_control:
		return lpm_priv->shadow.pm_control;
	case trace_address:
		return CBE_PM_TRACE_BUF_EMPTY;
	case pm_start_stop:
		return lpm_priv->shadow.pm_start_stop;
	case pm_interval:
		result = lv1_set_lpm_interval(lpm_priv->lpm_id, 0, 0, &val);
		if (result) {
			val = 0;
			dev_dbg(sbd_core(), "%s:%u: lv1 set_interval failed: "
				"reg %u, %s\n", __func__, __LINE__, reg,
				ps3_result(result));
		}
		return (u32)val;
	case group_control:
		return lpm_priv->shadow.group_control;
	case debug_bus_control:
		return lpm_priv->shadow.debug_bus_control;
	case pm_status:
		result = lv1_get_lpm_interrupt_status(lpm_priv->lpm_id,
						      &val);
		if (result) {
			val = 0;
			dev_dbg(sbd_core(), "%s:%u: lv1 get_lpm_status failed: "
				"reg %u, %s\n", __func__, __LINE__, reg,
				ps3_result(result));
		}
		return (u32)val;
	case ext_tr_timer:
		return 0;
	default:
		dev_dbg(sbd_core(), "%s:%u: unknown reg: %d\n", __func__,
			__LINE__, reg);
		BUG();
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ps3_read_pm);

/**
 * ps3_write_pm - Write Other LPM control registers.
 */

void ps3_write_pm(u32 cpu, enum pm_reg_name reg, u32 val)
{
	int result = 0;
	u64 dummy;

	switch (reg) {
	case group_control:
		if (val != lpm_priv->shadow.group_control)
			result = lv1_set_lpm_group_control(lpm_priv->lpm_id,
							   val,
							   PS3_WRITE_PM_MASK,
							   &dummy);
		lpm_priv->shadow.group_control = val;
		break;
	case debug_bus_control:
		if (val != lpm_priv->shadow.debug_bus_control)
			result = lv1_set_lpm_debug_bus_control(lpm_priv->lpm_id,
							      val,
							      PS3_WRITE_PM_MASK,
							      &dummy);
		lpm_priv->shadow.debug_bus_control = val;
		break;
	case pm_control:
		if (use_start_stop_bookmark)
			val |= (PS3_PM_CONTROL_PPU_TH0_BOOKMARK |
				PS3_PM_CONTROL_PPU_TH1_BOOKMARK);
		if (val != lpm_priv->shadow.pm_control)
			result = lv1_set_lpm_general_control(lpm_priv->lpm_id,
							     val,
							     PS3_WRITE_PM_MASK,
							     0, 0, &dummy,
							     &dummy);
		lpm_priv->shadow.pm_control = val;
		break;
	case pm_interval:
		result = lv1_set_lpm_interval(lpm_priv->lpm_id, val,
					      PS3_WRITE_PM_MASK, &dummy);
		break;
	case pm_start_stop:
		if (val != lpm_priv->shadow.pm_start_stop)
			result = lv1_set_lpm_trigger_control(lpm_priv->lpm_id,
							     val,
							     PS3_WRITE_PM_MASK,
							     &dummy);
		lpm_priv->shadow.pm_start_stop = val;
		break;
	case trace_address:
	case ext_tr_timer:
	case pm_status:
		break;
	default:
		dev_dbg(sbd_core(), "%s:%u: unknown reg: %d\n", __func__,
			__LINE__, reg);
		BUG();
		break;
	}

	if (result)
		dev_err(sbd_core(), "%s:%u: lv1 set_control failed: "
			"reg %u, %s\n", __func__, __LINE__, reg,
			ps3_result(result));
}
EXPORT_SYMBOL_GPL(ps3_write_pm);

/**
 * ps3_get_ctr_size - Get the size of a physical counter.
 *
 * Returns either 16 or 32.
 */

u32 ps3_get_ctr_size(u32 cpu, u32 phys_ctr)
{
	u32 pm_ctrl;

	if (phys_ctr >= NR_PHYS_CTRS) {
		dev_dbg(sbd_core(), "%s:%u: phys_ctr too big: %u\n", __func__,
			__LINE__, phys_ctr);
		return 0;
	}

	pm_ctrl = ps3_read_pm(cpu, pm_control);
	return (pm_ctrl & CBE_PM_16BIT_CTR(phys_ctr)) ? 16 : 32;
}
EXPORT_SYMBOL_GPL(ps3_get_ctr_size);

/**
 * ps3_set_ctr_size - Set the size of a physical counter to 16 or 32 bits.
 */

void ps3_set_ctr_size(u32 cpu, u32 phys_ctr, u32 ctr_size)
{
	u32 pm_ctrl;

	if (phys_ctr >= NR_PHYS_CTRS) {
		dev_dbg(sbd_core(), "%s:%u: phys_ctr too big: %u\n", __func__,
			__LINE__, phys_ctr);
		return;
	}

	pm_ctrl = ps3_read_pm(cpu, pm_control);

	switch (ctr_size) {
	case 16:
		pm_ctrl |= CBE_PM_16BIT_CTR(phys_ctr);
		ps3_write_pm(cpu, pm_control, pm_ctrl);
		break;

	case 32:
		pm_ctrl &= ~CBE_PM_16BIT_CTR(phys_ctr);
		ps3_write_pm(cpu, pm_control, pm_ctrl);
		break;
	default:
		BUG();
	}
}
EXPORT_SYMBOL_GPL(ps3_set_ctr_size);

static u64 pm_translate_signal_group_number_on_island2(u64 subgroup)
{

	if (subgroup == 2)
		subgroup = 3;

	if (subgroup <= 6)
		return PM_ISLAND2_BASE_SIGNAL_GROUP_NUMBER + subgroup;
	else if (subgroup == 7)
		return PM_ISLAND2_SIGNAL_GROUP_NUMBER1;
	else
		return PM_ISLAND2_SIGNAL_GROUP_NUMBER2;
}

static u64 pm_translate_signal_group_number_on_island3(u64 subgroup)
{

	switch (subgroup) {
	case 2:
	case 3:
	case 4:
		subgroup += 2;
		break;
	case 5:
		subgroup = 8;
		break;
	default:
		break;
	}
	return PM_ISLAND3_BASE_SIGNAL_GROUP_NUMBER + subgroup;
}

static u64 pm_translate_signal_group_number_on_island4(u64 subgroup)
{
	return PM_ISLAND4_BASE_SIGNAL_GROUP_NUMBER + subgroup;
}

static u64 pm_translate_signal_group_number_on_island5(u64 subgroup)
{

	switch (subgroup) {
	case 3:
		subgroup = 4;
		break;
	case 4:
		subgroup = 6;
		break;
	default:
		break;
	}
	return PM_ISLAND5_BASE_SIGNAL_GROUP_NUMBER + subgroup;
}

static u64 pm_translate_signal_group_number_on_island6(u64 subgroup,
						       u64 subsubgroup)
{
	switch (subgroup) {
	case 3:
	case 4:
	case 5:
		subgroup += 1;
		break;
	default:
		break;
	}

	switch (subsubgroup) {
	case 4:
	case 5:
	case 6:
		subsubgroup += 2;
		break;
	case 7:
	case 8:
	case 9:
	case 10:
		subsubgroup += 4;
		break;
	case 11:
	case 12:
	case 13:
		subsubgroup += 5;
		break;
	default:
		break;
	}

	if (subgroup <= 5)
		return (PM_ISLAND6_BASE_SIGNAL_GROUP_NUMBER + subgroup);
	else
		return (PM_ISLAND6_BASE_SIGNAL_GROUP_NUMBER + subgroup
			+ subsubgroup - 1);
}

static u64 pm_translate_signal_group_number_on_island7(u64 subgroup)
{
	return PM_ISLAND7_BASE_SIGNAL_GROUP_NUMBER + subgroup;
}

static u64 pm_translate_signal_group_number_on_island8(u64 subgroup)
{
	return PM_ISLAND8_BASE_SIGNAL_GROUP_NUMBER + subgroup;
}

static u64 pm_signal_group_to_ps3_lv1_signal_group(u64 group)
{
	u64 island;
	u64 subgroup;
	u64 subsubgroup;

	subgroup = 0;
	subsubgroup = 0;
	island = 0;
	if (group < 1000) {
		if (group < 100) {
			if (20 <= group && group < 30) {
				island = 2;
				subgroup = group - 20;
			} else if (30 <= group && group < 40) {
				island = 3;
				subgroup = group - 30;
			} else if (40 <= group && group < 50) {
				island = 4;
				subgroup = group - 40;
			} else if (50 <= group && group < 60) {
				island = 5;
				subgroup = group - 50;
			} else if (60 <= group && group < 70) {
				island = 6;
				subgroup = group - 60;
			} else if (70 <= group && group < 80) {
				island = 7;
				subgroup = group - 70;
			} else if (80 <= group && group < 90) {
				island = 8;
				subgroup = group - 80;
			}
		} else if (200 <= group && group < 300) {
			island = 2;
			subgroup = group - 200;
		} else if (600 <= group && group < 700) {
			island = 6;
			subgroup = 5;
			subsubgroup = group - 650;
		}
	} else if (6000 <= group && group < 7000) {
		island = 6;
		subgroup = 5;
		subsubgroup = group - 6500;
	}

	switch (island) {
	case 2:
		return pm_translate_signal_group_number_on_island2(subgroup);
	case 3:
		return pm_translate_signal_group_number_on_island3(subgroup);
	case 4:
		return pm_translate_signal_group_number_on_island4(subgroup);
	case 5:
		return pm_translate_signal_group_number_on_island5(subgroup);
	case 6:
		return pm_translate_signal_group_number_on_island6(subgroup,
								   subsubgroup);
	case 7:
		return pm_translate_signal_group_number_on_island7(subgroup);
	case 8:
		return pm_translate_signal_group_number_on_island8(subgroup);
	default:
		dev_dbg(sbd_core(), "%s:%u: island not found: %llu\n", __func__,
			__LINE__, group);
		BUG();
		break;
	}
	return 0;
}

static u64 pm_bus_word_to_ps3_lv1_bus_word(u8 word)
{

	switch (word) {
	case 1:
		return 0xF000;
	case 2:
		return 0x0F00;
	case 4:
		return 0x00F0;
	case 8:
	default:
		return 0x000F;
	}
}

static int __ps3_set_signal(u64 lv1_signal_group, u64 bus_select,
			    u64 signal_select, u64 attr1, u64 attr2, u64 attr3)
{
	int ret;

	ret = lv1_set_lpm_signal(lpm_priv->lpm_id, lv1_signal_group, bus_select,
				 signal_select, attr1, attr2, attr3);
	if (ret)
		dev_err(sbd_core(),
			"%s:%u: error:%d 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx\n",
			__func__, __LINE__, ret, lv1_signal_group, bus_select,
			signal_select, attr1, attr2, attr3);

	return ret;
}

int ps3_set_signal(u64 signal_group, u8 signal_bit, u16 sub_unit,
		   u8 bus_word)
{
	int ret;
	u64 lv1_signal_group;
	u64 bus_select;
	u64 signal_select;
	u64 attr1, attr2, attr3;

	if (signal_group == 0)
		return __ps3_set_signal(0, 0, 0, 0, 0, 0);

	lv1_signal_group =
		pm_signal_group_to_ps3_lv1_signal_group(signal_group);
	bus_select = pm_bus_word_to_ps3_lv1_bus_word(bus_word);

	switch (signal_group) {
	case PM_SIG_GROUP_SPU_TRIGGER:
		signal_select = 1;
		signal_select = signal_select << (63 - signal_bit);
		break;
	case PM_SIG_GROUP_SPU_EVENT:
		signal_select = 1;
		signal_select = (signal_select << (63 - signal_bit)) | 0x3;
		break;
	default:
		signal_select = 0;
		break;
	}

	/*
	 * 0: physical object.
	 * 1: logical object.
	 * This parameter is only used for the PPE and SPE signals.
	 */
	attr1 = 1;

	/*
	 * This parameter is used to specify the target physical/logical
	 * PPE/SPE object.
	 */
	if (PM_SIG_GROUP_SPU <= signal_group &&
		signal_group < PM_SIG_GROUP_MFC_MAX)
		attr2 = sub_unit;
	else
		attr2 = lpm_priv->pu_id;

	/*
	 * This parameter is only used for setting the SPE signal.
	 */
	attr3 = 0;

	ret = __ps3_set_signal(lv1_signal_group, bus_select, signal_select,
			       attr1, attr2, attr3);
	if (ret)
		dev_err(sbd_core(), "%s:%u: __ps3_set_signal failed: %d\n",
			__func__, __LINE__, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(ps3_set_signal);

u32 ps3_get_hw_thread_id(int cpu)
{
	return get_hard_smp_processor_id(cpu);
}
EXPORT_SYMBOL_GPL(ps3_get_hw_thread_id);

/**
 * ps3_enable_pm - Enable the entire performance monitoring unit.
 *
 * When we enable the LPM, all pending writes to counters get committed.
 */

void ps3_enable_pm(u32 cpu)
{
	int result;
	u64 tmp;
	int insert_bookmark = 0;

	lpm_priv->tb_count = 0;

	if (use_start_stop_bookmark) {
		if (!(lpm_priv->shadow.pm_start_stop &
			(PS3_PM_START_STOP_START_MASK
			| PS3_PM_START_STOP_STOP_MASK))) {
			result = lv1_set_lpm_trigger_control(lpm_priv->lpm_id,
				(PS3_PM_START_STOP_PPU_TH0_BOOKMARK_START |
				PS3_PM_START_STOP_PPU_TH1_BOOKMARK_START |
				PS3_PM_START_STOP_PPU_TH0_BOOKMARK_STOP |
				PS3_PM_START_STOP_PPU_TH1_BOOKMARK_STOP),
				0xFFFFFFFFFFFFFFFFULL, &tmp);

			if (result)
				dev_err(sbd_core(), "%s:%u: "
					"lv1_set_lpm_trigger_control failed: "
					"%s\n", __func__, __LINE__,
					ps3_result(result));

			insert_bookmark = !result;
		}
	}

	result = lv1_start_lpm(lpm_priv->lpm_id);

	if (result)
		dev_err(sbd_core(), "%s:%u: lv1_start_lpm failed: %s\n",
			__func__, __LINE__, ps3_result(result));

	if (use_start_stop_bookmark && !result && insert_bookmark)
		ps3_set_bookmark(get_tb() | PS3_PM_BOOKMARK_START);
}
EXPORT_SYMBOL_GPL(ps3_enable_pm);

/**
 * ps3_disable_pm - Disable the entire performance monitoring unit.
 */

void ps3_disable_pm(u32 cpu)
{
	int result;
	u64 tmp;

	ps3_set_bookmark(get_tb() | PS3_PM_BOOKMARK_STOP);

	result = lv1_stop_lpm(lpm_priv->lpm_id, &tmp);

	if (result) {
		if (result != LV1_WRONG_STATE)
			dev_err(sbd_core(), "%s:%u: lv1_stop_lpm failed: %s\n",
				__func__, __LINE__, ps3_result(result));
		return;
	}

	lpm_priv->tb_count = tmp;

	dev_dbg(sbd_core(), "%s:%u: tb_count %llu (%llxh)\n", __func__, __LINE__,
		lpm_priv->tb_count, lpm_priv->tb_count);
}
EXPORT_SYMBOL_GPL(ps3_disable_pm);

/**
 * ps3_lpm_copy_tb - Copy data from the trace buffer to a kernel buffer.
 * @offset: Offset in bytes from the start of the trace buffer.
 * @buf: Copy destination.
 * @count: Maximum count of bytes to copy.
 * @bytes_copied: Pointer to a variable that will receive the number of
 *  bytes copied to @buf.
 *
 * On error @buf will contain any successfully copied trace buffer data
 * and bytes_copied will be set to the number of bytes successfully copied.
 */

int ps3_lpm_copy_tb(unsigned long offset, void *buf, unsigned long count,
		    unsigned long *bytes_copied)
{
	int result;

	*bytes_copied = 0;

	if (!lpm_priv->tb_cache)
		return -EPERM;

	if (offset >= lpm_priv->tb_count)
		return 0;

	count = min_t(u64, count, lpm_priv->tb_count - offset);

	while (*bytes_copied < count) {
		const unsigned long request = count - *bytes_copied;
		u64 tmp;

		result = lv1_copy_lpm_trace_buffer(lpm_priv->lpm_id, offset,
						   request, &tmp);
		if (result) {
			dev_dbg(sbd_core(), "%s:%u: 0x%lx bytes at 0x%lx\n",
				__func__, __LINE__, request, offset);

			dev_err(sbd_core(), "%s:%u: lv1_copy_lpm_trace_buffer "
				"failed: %s\n", __func__, __LINE__,
				ps3_result(result));
			return result == LV1_WRONG_STATE ? -EBUSY : -EINVAL;
		}

		memcpy(buf, lpm_priv->tb_cache, tmp);
		buf += tmp;
		*bytes_copied += tmp;
		offset += tmp;
	}
	dev_dbg(sbd_core(), "%s:%u: copied %lxh bytes\n", __func__, __LINE__,
		*bytes_copied);

	return 0;
}
EXPORT_SYMBOL_GPL(ps3_lpm_copy_tb);

/**
 * ps3_lpm_copy_tb_to_user - Copy data from the trace buffer to a user buffer.
 * @offset: Offset in bytes from the start of the trace buffer.
 * @buf: A __user copy destination.
 * @count: Maximum count of bytes to copy.
 * @bytes_copied: Pointer to a variable that will receive the number of
 *  bytes copied to @buf.
 *
 * On error @buf will contain any successfully copied trace buffer data
 * and bytes_copied will be set to the number of bytes successfully copied.
 */

int ps3_lpm_copy_tb_to_user(unsigned long offset, void __user *buf,
			    unsigned long count, unsigned long *bytes_copied)
{
	int result;

	*bytes_copied = 0;

	if (!lpm_priv->tb_cache)
		return -EPERM;

	if (offset >= lpm_priv->tb_count)
		return 0;

	count = min_t(u64, count, lpm_priv->tb_count - offset);

	while (*bytes_copied < count) {
		const unsigned long request = count - *bytes_copied;
		u64 tmp;

		result = lv1_copy_lpm_trace_buffer(lpm_priv->lpm_id, offset,
						   request, &tmp);
		if (result) {
			dev_dbg(sbd_core(), "%s:%u: 0x%lx bytes at 0x%lx\n",
				__func__, __LINE__, request, offset);
			dev_err(sbd_core(), "%s:%u: lv1_copy_lpm_trace_buffer "
				"failed: %s\n", __func__, __LINE__,
				ps3_result(result));
			return result == LV1_WRONG_STATE ? -EBUSY : -EINVAL;
		}

		result = copy_to_user(buf, lpm_priv->tb_cache, tmp);

		if (result) {
			dev_dbg(sbd_core(), "%s:%u: 0x%llx bytes at 0x%p\n",
				__func__, __LINE__, tmp, buf);
			dev_err(sbd_core(), "%s:%u: copy_to_user failed: %d\n",
				__func__, __LINE__, result);
			return -EFAULT;
		}

		buf += tmp;
		*bytes_copied += tmp;
		offset += tmp;
	}
	dev_dbg(sbd_core(), "%s:%u: copied %lxh bytes\n", __func__, __LINE__,
		*bytes_copied);

	return 0;
}
EXPORT_SYMBOL_GPL(ps3_lpm_copy_tb_to_user);

/**
 * ps3_get_and_clear_pm_interrupts -
 *
 * Clearing interrupts for the entire performance monitoring unit.
 * Reading pm_status clears the interrupt bits.
 */

u32 ps3_get_and_clear_pm_interrupts(u32 cpu)
{
	return ps3_read_pm(cpu, pm_status);
}
EXPORT_SYMBOL_GPL(ps3_get_and_clear_pm_interrupts);

/**
 * ps3_enable_pm_interrupts -
 *
 * Enabling interrupts for the entire performance monitoring unit.
 * Enables the interrupt bits in the pm_status register.
 */

void ps3_enable_pm_interrupts(u32 cpu, u32 thread, u32 mask)
{
	if (mask)
		ps3_write_pm(cpu, pm_status, mask);
}
EXPORT_SYMBOL_GPL(ps3_enable_pm_interrupts);

/**
 * ps3_enable_pm_interrupts -
 *
 * Disabling interrupts for the entire performance monitoring unit.
 */

void ps3_disable_pm_interrupts(u32 cpu)
{
	ps3_get_and_clear_pm_interrupts(cpu);
	ps3_write_pm(cpu, pm_status, 0);
}
EXPORT_SYMBOL_GPL(ps3_disable_pm_interrupts);

/**
 * ps3_lpm_open - Open the logical performance monitor device.
 * @tb_type: Specifies the type of trace buffer lv1 should use for this lpm
 *  instance, specified by one of enum ps3_lpm_tb_type.
 * @tb_cache: Optional user supplied buffer to use as the trace buffer cache.
 *  If NULL, the driver will allocate and manage an internal buffer.
 *  Unused when when @tb_type is PS3_LPM_TB_TYPE_NONE.
 * @tb_cache_size: The size in bytes of the user supplied @tb_cache buffer.
 *  Unused when @tb_cache is NULL or @tb_type is PS3_LPM_TB_TYPE_NONE.
 */

int ps3_lpm_open(enum ps3_lpm_tb_type tb_type, void *tb_cache,
	u64 tb_cache_size)
{
	int result;
	u64 tb_size;

	BUG_ON(!lpm_priv);
	BUG_ON(tb_type != PS3_LPM_TB_TYPE_NONE
		&& tb_type != PS3_LPM_TB_TYPE_INTERNAL);

	if (tb_type == PS3_LPM_TB_TYPE_NONE && tb_cache)
		dev_dbg(sbd_core(), "%s:%u: bad in vals\n", __func__, __LINE__);

	if (!atomic_add_unless(&lpm_priv->open, 1, 1)) {
		dev_dbg(sbd_core(), "%s:%u: busy\n", __func__, __LINE__);
		return -EBUSY;
	}

	/* Note tb_cache needs 128 byte alignment. */

	if (tb_type == PS3_LPM_TB_TYPE_NONE) {
		lpm_priv->tb_cache_size = 0;
		lpm_priv->tb_cache_internal = NULL;
		lpm_priv->tb_cache = NULL;
	} else if (tb_cache) {
		if (tb_cache != (void *)_ALIGN_UP((unsigned long)tb_cache, 128)
			|| tb_cache_size != _ALIGN_UP(tb_cache_size, 128)) {
			dev_err(sbd_core(), "%s:%u: unaligned tb_cache\n",
				__func__, __LINE__);
			result = -EINVAL;
			goto fail_align;
		}
		lpm_priv->tb_cache_size = tb_cache_size;
		lpm_priv->tb_cache_internal = NULL;
		lpm_priv->tb_cache = tb_cache;
	} else {
		lpm_priv->tb_cache_size = PS3_LPM_DEFAULT_TB_CACHE_SIZE;
		lpm_priv->tb_cache_internal = kzalloc(
			lpm_priv->tb_cache_size + 127, GFP_KERNEL);
		if (!lpm_priv->tb_cache_internal) {
			dev_err(sbd_core(), "%s:%u: alloc internal tb_cache "
				"failed\n", __func__, __LINE__);
			result = -ENOMEM;
			goto fail_malloc;
		}
		lpm_priv->tb_cache = (void *)_ALIGN_UP(
			(unsigned long)lpm_priv->tb_cache_internal, 128);
	}

	result = lv1_construct_lpm(lpm_priv->node_id, tb_type, 0, 0,
				ps3_mm_phys_to_lpar(__pa(lpm_priv->tb_cache)),
				lpm_priv->tb_cache_size, &lpm_priv->lpm_id,
				&lpm_priv->outlet_id, &tb_size);

	if (result) {
		dev_err(sbd_core(), "%s:%u: lv1_construct_lpm failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		result = -EINVAL;
		goto fail_construct;
	}

	lpm_priv->shadow.pm_control = PS3_LPM_SHADOW_REG_INIT;
	lpm_priv->shadow.pm_start_stop = PS3_LPM_SHADOW_REG_INIT;
	lpm_priv->shadow.group_control = PS3_LPM_SHADOW_REG_INIT;
	lpm_priv->shadow.debug_bus_control = PS3_LPM_SHADOW_REG_INIT;

	dev_dbg(sbd_core(), "%s:%u: lpm_id 0x%llx, outlet_id 0x%llx, "
		"tb_size 0x%llx\n", __func__, __LINE__, lpm_priv->lpm_id,
		lpm_priv->outlet_id, tb_size);

	return 0;

fail_construct:
	kfree(lpm_priv->tb_cache_internal);
	lpm_priv->tb_cache_internal = NULL;
fail_malloc:
fail_align:
	atomic_dec(&lpm_priv->open);
	return result;
}
EXPORT_SYMBOL_GPL(ps3_lpm_open);

/**
 * ps3_lpm_close - Close the lpm device.
 *
 */

int ps3_lpm_close(void)
{
	dev_dbg(sbd_core(), "%s:%u\n", __func__, __LINE__);

	lv1_destruct_lpm(lpm_priv->lpm_id);
	lpm_priv->lpm_id = 0;

	kfree(lpm_priv->tb_cache_internal);
	lpm_priv->tb_cache_internal = NULL;

	atomic_dec(&lpm_priv->open);
	return 0;
}
EXPORT_SYMBOL_GPL(ps3_lpm_close);

static int ps3_lpm_probe(struct ps3_system_bus_device *dev)
{
	dev_dbg(&dev->core, " -> %s:%u\n", __func__, __LINE__);

	if (lpm_priv) {
		dev_info(&dev->core, "%s:%u: called twice\n",
			__func__, __LINE__);
		return -EBUSY;
	}

	lpm_priv = kzalloc(sizeof(*lpm_priv), GFP_KERNEL);

	if (!lpm_priv)
		return -ENOMEM;

	lpm_priv->sbd = dev;
	lpm_priv->node_id = dev->lpm.node_id;
	lpm_priv->pu_id = dev->lpm.pu_id;
	lpm_priv->rights = dev->lpm.rights;

	dev_info(&dev->core, " <- %s:%u:\n", __func__, __LINE__);

	return 0;
}

static int ps3_lpm_remove(struct ps3_system_bus_device *dev)
{
	dev_dbg(&dev->core, " -> %s:%u:\n", __func__, __LINE__);

	ps3_lpm_close();

	kfree(lpm_priv);
	lpm_priv = NULL;

	dev_info(&dev->core, " <- %s:%u:\n", __func__, __LINE__);
	return 0;
}

static struct ps3_system_bus_driver ps3_lpm_driver = {
	.match_id = PS3_MATCH_ID_LPM,
	.core.name	= "ps3-lpm",
	.core.owner	= THIS_MODULE,
	.probe		= ps3_lpm_probe,
	.remove		= ps3_lpm_remove,
	.shutdown	= ps3_lpm_remove,
};

static int __init ps3_lpm_init(void)
{
	pr_debug("%s:%d:\n", __func__, __LINE__);
	return ps3_system_bus_driver_register(&ps3_lpm_driver);
}

static void __exit ps3_lpm_exit(void)
{
	pr_debug("%s:%d:\n", __func__, __LINE__);
	ps3_system_bus_driver_unregister(&ps3_lpm_driver);
}

module_init(ps3_lpm_init);
module_exit(ps3_lpm_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PS3 Logical Performance Monitor Driver");
MODULE_AUTHOR("Sony Corporation");
MODULE_ALIAS(PS3_MODULE_ALIAS_LPM);

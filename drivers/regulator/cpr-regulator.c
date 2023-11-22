// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/sort.h>
#include <linux/uaccess.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/debug-regulator.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/cpr-regulator.h>
#include <linux/panic_notifier.h>


/* Register Offsets for RB-CPR and Bit Definitions */

/* RBCPR Version Register */
#define REG_RBCPR_VERSION		0
#define RBCPR_VER_2			0x02

/* RBCPR Gate Count and Target Registers */
#define REG_RBCPR_GCNT_TARGET(n)	(0x60 + 4 * n)

#define RBCPR_GCNT_TARGET_GCNT_BITS	10
#define RBCPR_GCNT_TARGET_GCNT_SHIFT	12
#define RBCPR_GCNT_TARGET_GCNT_MASK	((1<<RBCPR_GCNT_TARGET_GCNT_BITS)-1)

/* RBCPR Sensor Mask and Bypass Registers */
#define REG_RBCPR_SENSOR_MASK0		0x20
#define RBCPR_SENSOR_MASK0_SENSOR(n)	(~BIT(n))
#define REG_RBCPR_SENSOR_BYPASS0	0x30

/* RBCPR Timer Control */
#define REG_RBCPR_TIMER_INTERVAL	0x44
#define REG_RBIF_TIMER_ADJUST		0x4C

#define RBIF_TIMER_ADJ_CONS_UP_BITS	4
#define RBIF_TIMER_ADJ_CONS_UP_MASK	((1<<RBIF_TIMER_ADJ_CONS_UP_BITS)-1)
#define RBIF_TIMER_ADJ_CONS_DOWN_BITS	4
#define RBIF_TIMER_ADJ_CONS_DOWN_MASK	((1<<RBIF_TIMER_ADJ_CONS_DOWN_BITS)-1)
#define RBIF_TIMER_ADJ_CONS_DOWN_SHIFT	4
#define RBIF_TIMER_ADJ_CLAMP_INT_BITS	8
#define RBIF_TIMER_ADJ_CLAMP_INT_MASK	((1<<RBIF_TIMER_ADJ_CLAMP_INT_BITS)-1)
#define RBIF_TIMER_ADJ_CLAMP_INT_SHIFT	8

/* RBCPR Config Register */
#define REG_RBIF_LIMIT			0x48
#define REG_RBCPR_STEP_QUOT		0x80
#define REG_RBIF_SW_VLEVEL		0x94

#define RBIF_LIMIT_CEILING_BITS		6
#define RBIF_LIMIT_CEILING_MASK		((1<<RBIF_LIMIT_CEILING_BITS)-1)
#define RBIF_LIMIT_CEILING_SHIFT	6
#define RBIF_LIMIT_FLOOR_BITS		6
#define RBIF_LIMIT_FLOOR_MASK		((1<<RBIF_LIMIT_FLOOR_BITS)-1)

#define RBIF_LIMIT_CEILING_DEFAULT	RBIF_LIMIT_CEILING_MASK
#define RBIF_LIMIT_FLOOR_DEFAULT	0
#define RBIF_SW_VLEVEL_DEFAULT		0x20

#define RBCPR_STEP_QUOT_STEPQUOT_BITS	8
#define RBCPR_STEP_QUOT_STEPQUOT_MASK	((1<<RBCPR_STEP_QUOT_STEPQUOT_BITS)-1)
#define RBCPR_STEP_QUOT_IDLE_CLK_BITS	4
#define RBCPR_STEP_QUOT_IDLE_CLK_MASK	((1<<RBCPR_STEP_QUOT_IDLE_CLK_BITS)-1)
#define RBCPR_STEP_QUOT_IDLE_CLK_SHIFT	8

/* RBCPR Control Register */
#define REG_RBCPR_CTL			0x90

#define RBCPR_CTL_LOOP_EN			BIT(0)
#define RBCPR_CTL_TIMER_EN			BIT(3)
#define RBCPR_CTL_SW_AUTO_CONT_ACK_EN		BIT(5)
#define RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN	BIT(6)
#define RBCPR_CTL_COUNT_MODE			BIT(10)
#define RBCPR_CTL_UP_THRESHOLD_BITS	4
#define RBCPR_CTL_UP_THRESHOLD_MASK	((1<<RBCPR_CTL_UP_THRESHOLD_BITS)-1)
#define RBCPR_CTL_UP_THRESHOLD_SHIFT	24
#define RBCPR_CTL_DN_THRESHOLD_BITS	4
#define RBCPR_CTL_DN_THRESHOLD_MASK	((1<<RBCPR_CTL_DN_THRESHOLD_BITS)-1)
#define RBCPR_CTL_DN_THRESHOLD_SHIFT	28

/* RBCPR Ack/Nack Response */
#define REG_RBIF_CONT_ACK_CMD		0x98
#define REG_RBIF_CONT_NACK_CMD		0x9C

/* RBCPR Result status Registers */
#define REG_RBCPR_RESULT_0		0xA0
#define REG_RBCPR_RESULT_1		0xA4

#define RBCPR_RESULT_1_SEL_FAST_BITS	3
#define RBCPR_RESULT_1_SEL_FAST(val)	(val & \
					((1<<RBCPR_RESULT_1_SEL_FAST_BITS) - 1))

#define RBCPR_RESULT0_BUSY_SHIFT	19
#define RBCPR_RESULT0_BUSY_MASK		BIT(RBCPR_RESULT0_BUSY_SHIFT)
#define RBCPR_RESULT0_ERROR_LT0_SHIFT	18
#define RBCPR_RESULT0_ERROR_SHIFT	6
#define RBCPR_RESULT0_ERROR_BITS	12
#define RBCPR_RESULT0_ERROR_MASK	((1<<RBCPR_RESULT0_ERROR_BITS)-1)
#define RBCPR_RESULT0_ERROR_STEPS_SHIFT	2
#define RBCPR_RESULT0_ERROR_STEPS_BITS	4
#define RBCPR_RESULT0_ERROR_STEPS_MASK	((1<<RBCPR_RESULT0_ERROR_STEPS_BITS)-1)
#define RBCPR_RESULT0_STEP_UP_SHIFT	1

/* RBCPR Interrupt Control Register */
#define REG_RBIF_IRQ_EN(n)		(0x100 + 4 * n)
#define REG_RBIF_IRQ_CLEAR		0x110
#define REG_RBIF_IRQ_STATUS		0x114

#define CPR_INT_DONE		BIT(0)
#define CPR_INT_MIN		BIT(1)
#define CPR_INT_DOWN		BIT(2)
#define CPR_INT_MID		BIT(3)
#define CPR_INT_UP		BIT(4)
#define CPR_INT_MAX		BIT(5)
#define CPR_INT_CLAMP		BIT(6)
#define CPR_INT_ALL	(CPR_INT_DONE | CPR_INT_MIN | CPR_INT_DOWN | \
			CPR_INT_MID | CPR_INT_UP | CPR_INT_MAX | CPR_INT_CLAMP)
#define CPR_INT_DEFAULT	(CPR_INT_UP | CPR_INT_DOWN)

#define CPR_NUM_RING_OSC	8

/* RBCPR Debug Resgister */
#define REG_RBCPR_DEBUG1		0x120
#define RBCPR_DEBUG1_QUOT_FAST_BITS	12
#define RBCPR_DEBUG1_QUOT_SLOW_BITS	12
#define RBCPR_DEBUG1_QUOT_SLOW_SHIFT	12

#define RBCPR_DEBUG1_QUOT_FAST(val)	(val & \
					((1<<RBCPR_DEBUG1_QUOT_FAST_BITS)-1))

#define RBCPR_DEBUG1_QUOT_SLOW(val)	((val>>RBCPR_DEBUG1_QUOT_SLOW_SHIFT) & \
					((1<<RBCPR_DEBUG1_QUOT_SLOW_BITS)-1))

/* RBCPR Aging Resgister */
#define REG_RBCPR_HTOL_AGE		0x160
#define RBCPR_HTOL_AGE_PAGE		BIT(1)
#define RBCPR_AGE_DATA_STATUS		BIT(2)

/* RBCPR Clock Control Register */
#define RBCPR_CLK_SEL_MASK	BIT(0)
#define RBCPR_CLK_SEL_19P2_MHZ	0
#define RBCPR_CLK_SEL_AHB_CLK	BIT(0)

/* CPR eFuse parameters */
#define CPR_FUSE_TARGET_QUOT_BITS	12
#define CPR_FUSE_TARGET_QUOT_BITS_MASK	((1<<CPR_FUSE_TARGET_QUOT_BITS)-1)
#define CPR_FUSE_RO_SEL_BITS		3
#define CPR_FUSE_RO_SEL_BITS_MASK	((1<<CPR_FUSE_RO_SEL_BITS)-1)

#define CPR_FUSE_MIN_QUOT_DIFF		50

#define BYTES_PER_FUSE_ROW		8

#define SPEED_BIN_NONE			UINT_MAX

#define FUSE_REVISION_UNKNOWN		(-1)
#define FUSE_MAP_NO_MATCH		(-1)
#define FUSE_PARAM_MATCH_ANY		0xFFFFFFFF

#define FLAGS_IGNORE_1ST_IRQ_STATUS	BIT(0)
#define FLAGS_SET_MIN_VOLTAGE		BIT(1)
#define FLAGS_UPLIFT_QUOT_VOLT		BIT(2)

/*
 * The number of individual aging measurements to perform which are then
 * averaged together in order to determine the final aging adjustment value.
 */
#define CPR_AGING_MEASUREMENT_ITERATIONS	16

/*
 * Aging measurements for the aged and unaged ring oscillators take place a few
 * microseconds apart.  If the vdd-supply voltage fluctuates between the two
 * measurements, then the difference between them will be incorrect.  The
 * difference could end up too high or too low.  This constant defines the
 * number of lowest and highest measurements to ignore when averaging.
 */
#define CPR_AGING_MEASUREMENT_FILTER	3

#define CPR_REGULATOR_DRIVER_NAME	"qcom,cpr-regulator"

/**
 * enum vdd_mx_vmin_method - Method to determine vmin for vdd-mx
 * %VDD_MX_VMIN_APC:			Equal to APC voltage
 * %VDD_MX_VMIN_APC_CORNER_CEILING:	Equal to PVS corner ceiling voltage
 * %VDD_MX_VMIN_APC_SLOW_CORNER_CEILING:
 *					Equal to slow speed corner ceiling
 * %VDD_MX_VMIN_MX_VMAX:		Equal to specified vdd-mx-vmax voltage
 * %VDD_MX_VMIN_APC_CORNER_MAP:		Equal to the APC corner mapped MX
 *					voltage
 */
enum vdd_mx_vmin_method {
	VDD_MX_VMIN_APC,
	VDD_MX_VMIN_APC_CORNER_CEILING,
	VDD_MX_VMIN_APC_SLOW_CORNER_CEILING,
	VDD_MX_VMIN_MX_VMAX,
	VDD_MX_VMIN_APC_FUSE_CORNER_MAP,
	VDD_MX_VMIN_APC_CORNER_MAP,
};

#define CPR_CORNER_MIN		1
#define CPR_FUSE_CORNER_MIN	1
/*
 * This is an arbitrary upper limit which is used in a sanity check in order to
 * avoid excessive memory allocation due to bad device tree data.
 */
#define CPR_FUSE_CORNER_LIMIT	100

struct quot_adjust_info {
	int speed_bin;
	int virtual_corner;
	int quot_adjust;
};

struct cpr_quot_scale {
	u32 offset;
	u32 multiplier;
};

struct cpr_aging_sensor_info {
	u32 sensor_id;
	int initial_quot_diff;
	int current_quot_diff;
};

struct cpr_aging_info {
	struct cpr_aging_sensor_info *sensor_info;
	int	num_aging_sensors;
	int	aging_corner;
	u32	aging_ro_kv;
	u32	*aging_derate;
	u32	aging_sensor_bypass;
	u32	max_aging_margin;
	u32	aging_ref_voltage;
	u32	cpr_ro_kv[CPR_NUM_RING_OSC];
	int	*voltage_adjust;

	bool	cpr_aging_error;
	bool	cpr_aging_done;
};

static const char * const vdd_apc_name[] =	{"vdd-apc-optional-prim",
						"vdd-apc-optional-sec",
						"vdd-apc"};

enum voltage_change_dir {
	NO_CHANGE,
	DOWN,
	UP,
};

struct cpr_regulator {
	struct list_head		list;
	struct regulator_desc		rdesc;
	struct regulator_dev		*rdev;
	bool				vreg_enabled;
	int				corner;
	int				ceiling_max;
	struct dentry			*debugfs;

	/* eFuse parameters */
	phys_addr_t	efuse_addr;
	void __iomem	*efuse_base;
	u64		*remapped_row;
	u32		remapped_row_base;
	int		num_remapped_rows;

	/* Process voltage parameters */
	u32		*pvs_corner_v;
	/* Process voltage variables */
	u32		pvs_bin;
	u32		speed_bin;
	u32		pvs_version;

	/* APC voltage regulator */
	struct regulator	*vdd_apc;

	/* Dependency parameters */
	struct regulator	*vdd_mx;
	int			vdd_mx_vmax;
	int			vdd_mx_vmin_method;
	int			vdd_mx_vmin;
	int			*vdd_mx_corner_map;

	struct regulator	*rpm_apc_vreg;
	int			*rpm_apc_corner_map;

	/* mem-acc regulator */
	struct regulator	*mem_acc_vreg;

	/* CPR parameters */
	u32		num_fuse_corners;
	u64		cpr_fuse_bits;
	bool		cpr_fuse_disable;
	bool		cpr_fuse_local;
	bool		cpr_fuse_redundant;
	int		cpr_fuse_revision;
	int		cpr_fuse_map_count;
	int		cpr_fuse_map_match;
	int		*cpr_fuse_target_quot;
	int		*cpr_fuse_ro_sel;
	int		*fuse_quot_offset;
	int		gcnt;

	unsigned int	cpr_irq;
	void __iomem	*rbcpr_base;
	phys_addr_t	rbcpr_clk_addr;
	struct mutex	cpr_mutex;

	int		*cpr_max_ceiling;
	int		*ceiling_volt;
	int		*floor_volt;
	int		*fuse_ceiling_volt;
	int		*fuse_floor_volt;
	int		*last_volt;
	int		*open_loop_volt;
	int		step_volt;

	int		*save_ctl;
	int		*save_irq;

	int		*vsens_corner_map;
	/* vsens status */
	bool		vsens_enabled;
	/* vsens regulators */
	struct regulator	*vdd_vsens_corner;
	struct regulator	*vdd_vsens_voltage;

	/* Config parameters */
	bool		enable;
	u32		ref_clk_khz;
	u32		timer_delay_us;
	u32		timer_cons_up;
	u32		timer_cons_down;
	u32		irq_line;
	u32		*step_quotient;
	u32		up_threshold;
	u32		down_threshold;
	u32		idle_clocks;
	u32		gcnt_time_us;
	u32		clamp_timer_interval;
	u32		vdd_apc_step_up_limit;
	u32		vdd_apc_step_down_limit;
	u32		flags;
	int		*corner_map;
	u32		num_corners;
	int		*quot_adjust;
	int		*mem_acc_corner_map;
	unsigned int	*vdd_mode_map;

	int			num_adj_cpus;
	int			*adj_cpus;
	cpumask_t		cpu_mask;
	bool			cpr_disabled_in_pc;
	struct notifier_block	pm_notifier;

	bool		is_cpr_suspended;

	struct cpr_aging_info	*aging_info;

	struct notifier_block	panic_notifier;
};

#define CPR_DEBUG_MASK_IRQ	BIT(0)
#define CPR_DEBUG_MASK_API	BIT(1)

static int cpr_debug_enable;
#if defined(CONFIG_DEBUG_FS)
static struct dentry *cpr_debugfs_base;
#endif

static DEFINE_MUTEX(cpr_regulator_list_mutex);
static LIST_HEAD(cpr_regulator_list);

#define cpr_debug(cpr_vreg, message, ...) \
	do { \
		if (cpr_debug_enable & CPR_DEBUG_MASK_API) \
			pr_info("%s: " message, (cpr_vreg)->rdesc.name, \
				##__VA_ARGS__); \
	} while (0)
#define cpr_debug_irq(cpr_vreg, message, ...) \
	do { \
		if (cpr_debug_enable & CPR_DEBUG_MASK_IRQ) \
			pr_info("%s: " message, (cpr_vreg)->rdesc.name, \
				##__VA_ARGS__); \
		else \
			pr_debug("%s: " message, (cpr_vreg)->rdesc.name, \
				##__VA_ARGS__); \
	} while (0)
#define cpr_info(cpr_vreg, message, ...) \
	pr_info("%s: " message, (cpr_vreg)->rdesc.name, ##__VA_ARGS__)
#define cpr_err(cpr_vreg, message, ...) \
	pr_err("%s: " message, (cpr_vreg)->rdesc.name, ##__VA_ARGS__)

static u64 cpr_read_remapped_efuse_row(struct cpr_regulator *cpr_vreg,
					u32 row_num)
{
	if (row_num - cpr_vreg->remapped_row_base
			>= cpr_vreg->num_remapped_rows) {
		cpr_err(cpr_vreg, "invalid row=%u, max remapped row=%u\n",
			row_num, cpr_vreg->remapped_row_base
					+ cpr_vreg->num_remapped_rows - 1);
		return 0;
	}

	return cpr_vreg->remapped_row[row_num - cpr_vreg->remapped_row_base];
}

static u64 cpr_read_efuse_row(struct cpr_regulator *cpr_vreg, u32 row_num,
				bool use_tz_api)
{
	u64 efuse_bits;

	if (cpr_vreg->remapped_row && row_num >= cpr_vreg->remapped_row_base)
		return cpr_read_remapped_efuse_row(cpr_vreg, row_num);

	if (!use_tz_api) {
		efuse_bits = readl_relaxed(cpr_vreg->efuse_base
			+ row_num * BYTES_PER_FUSE_ROW);
		return efuse_bits;
	}

	cpr_err(cpr_vreg, "read row %d unsuccessful, no support for tz_api",
			row_num);

	return 0;
}

/**
 * cpr_read_efuse_param() - read a parameter from one or two eFuse rows
 * @cpr_vreg:	Pointer to cpr_regulator struct for this regulator.
 * @row_start:	Fuse row number to start reading from.
 * @bit_start:	The LSB of the parameter to read from the fuse.
 * @bit_len:	The length of the parameter in bits.
 * @use_tz_api:	Flag to indicate if an SCM call should be used to read the fuse.
 *
 * This function reads a parameter of specified offset and bit size out of one
 * or two consecutive eFuse rows.  This allows for the reading of parameters
 * that happen to be split between two eFuse rows.
 *
 * Returns the fuse parameter on success or 0 on failure.
 */
static u64 cpr_read_efuse_param(struct cpr_regulator *cpr_vreg, int row_start,
		int bit_start, int bit_len, bool use_tz_api)
{
	u64 fuse[2];
	u64 param = 0;
	int bits_first, bits_second;

	if (bit_start < 0) {
		cpr_err(cpr_vreg, "Invalid LSB = %d specified\n", bit_start);
		return 0;
	}

	if (bit_len < 0 || bit_len > 64) {
		cpr_err(cpr_vreg, "Invalid bit length = %d specified\n",
			bit_len);
		return 0;
	}

	/* Allow bit indexing to start beyond the end of the start row. */
	if (bit_start >= 64) {
		row_start += bit_start >> 6; /* equivalent to bit_start / 64 */
		bit_start &= 0x3F;
	}

	fuse[0] = cpr_read_efuse_row(cpr_vreg, row_start, use_tz_api);

	if (bit_start == 0 && bit_len == 64) {
		param = fuse[0];
	} else if (bit_start + bit_len <= 64) {
		param = (fuse[0] >> bit_start) & ((1ULL << bit_len) - 1);
	} else {
		fuse[1] = cpr_read_efuse_row(cpr_vreg, row_start + 1,
						use_tz_api);
		bits_first = 64 - bit_start;
		bits_second = bit_len - bits_first;
		param = (fuse[0] >> bit_start) & ((1ULL << bits_first) - 1);
		param |= (fuse[1] & ((1ULL << bits_second) - 1)) << bits_first;
	}

	return param;
}

static bool cpr_is_allowed(struct cpr_regulator *cpr_vreg)
{
	if (cpr_vreg->cpr_fuse_disable || !cpr_vreg->enable)
		return false;
	else
		return true;
}

static void cpr_write(struct cpr_regulator *cpr_vreg, u32 offset, u32 value)
{
	writel_relaxed(value, cpr_vreg->rbcpr_base + offset);
}

static u32 cpr_read(struct cpr_regulator *cpr_vreg, u32 offset)
{
	return readl_relaxed(cpr_vreg->rbcpr_base + offset);
}

static void cpr_masked_write(struct cpr_regulator *cpr_vreg, u32 offset,
			     u32 mask, u32 value)
{
	u32 reg_val;

	reg_val = readl_relaxed(cpr_vreg->rbcpr_base + offset);
	reg_val &= ~mask;
	reg_val |= value & mask;
	writel_relaxed(reg_val, cpr_vreg->rbcpr_base + offset);
}

static void cpr_irq_clr(struct cpr_regulator *cpr_vreg)
{
	cpr_write(cpr_vreg, REG_RBIF_IRQ_CLEAR, CPR_INT_ALL);
}

static void cpr_irq_clr_nack(struct cpr_regulator *cpr_vreg)
{
	cpr_irq_clr(cpr_vreg);
	cpr_write(cpr_vreg, REG_RBIF_CONT_NACK_CMD, 1);
}

static void cpr_irq_clr_ack(struct cpr_regulator *cpr_vreg)
{
	cpr_irq_clr(cpr_vreg);
	cpr_write(cpr_vreg, REG_RBIF_CONT_ACK_CMD, 1);
}

static void cpr_irq_set(struct cpr_regulator *cpr_vreg, u32 int_bits)
{
	cpr_write(cpr_vreg, REG_RBIF_IRQ_EN(cpr_vreg->irq_line), int_bits);
}

static void cpr_ctl_modify(struct cpr_regulator *cpr_vreg, u32 mask, u32 value)
{
	cpr_masked_write(cpr_vreg, REG_RBCPR_CTL, mask, value);
}

static void cpr_ctl_enable(struct cpr_regulator *cpr_vreg, int corner)
{
	u32 val;

	if (cpr_vreg->is_cpr_suspended)
		return;

	/* Program Consecutive Up & Down */
	val = ((cpr_vreg->timer_cons_down & RBIF_TIMER_ADJ_CONS_DOWN_MASK)
			<< RBIF_TIMER_ADJ_CONS_DOWN_SHIFT) |
		(cpr_vreg->timer_cons_up & RBIF_TIMER_ADJ_CONS_UP_MASK);
	cpr_masked_write(cpr_vreg, REG_RBIF_TIMER_ADJUST,
			RBIF_TIMER_ADJ_CONS_UP_MASK |
			RBIF_TIMER_ADJ_CONS_DOWN_MASK, val);
	cpr_masked_write(cpr_vreg, REG_RBCPR_CTL,
			RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN |
			RBCPR_CTL_SW_AUTO_CONT_ACK_EN,
			cpr_vreg->save_ctl[corner]);
	cpr_irq_set(cpr_vreg, cpr_vreg->save_irq[corner]);

	if (cpr_is_allowed(cpr_vreg) && cpr_vreg->vreg_enabled &&
	    (cpr_vreg->ceiling_volt[corner] >
		cpr_vreg->floor_volt[corner]))
		val = RBCPR_CTL_LOOP_EN;
	else
		val = 0;
	cpr_ctl_modify(cpr_vreg, RBCPR_CTL_LOOP_EN, val);
}

static void cpr_ctl_disable(struct cpr_regulator *cpr_vreg)
{
	if (cpr_vreg->is_cpr_suspended)
		return;

	cpr_irq_set(cpr_vreg, 0);
	cpr_ctl_modify(cpr_vreg, RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN |
			RBCPR_CTL_SW_AUTO_CONT_ACK_EN, 0);
	cpr_masked_write(cpr_vreg, REG_RBIF_TIMER_ADJUST,
			RBIF_TIMER_ADJ_CONS_UP_MASK |
			RBIF_TIMER_ADJ_CONS_DOWN_MASK, 0);
	cpr_irq_clr(cpr_vreg);
	cpr_write(cpr_vreg, REG_RBIF_CONT_ACK_CMD, 1);
	cpr_write(cpr_vreg, REG_RBIF_CONT_NACK_CMD, 1);
	cpr_ctl_modify(cpr_vreg, RBCPR_CTL_LOOP_EN, 0);
}

static bool cpr_ctl_is_enabled(struct cpr_regulator *cpr_vreg)
{
	u32 reg_val;

	reg_val = cpr_read(cpr_vreg, REG_RBCPR_CTL);
	return reg_val & RBCPR_CTL_LOOP_EN;
}

static bool cpr_ctl_is_busy(struct cpr_regulator *cpr_vreg)
{
	u32 reg_val;

	reg_val = cpr_read(cpr_vreg, REG_RBCPR_RESULT_0);
	return reg_val & RBCPR_RESULT0_BUSY_MASK;
}

static void cpr_corner_save(struct cpr_regulator *cpr_vreg, int corner)
{
	cpr_vreg->save_ctl[corner] = cpr_read(cpr_vreg, REG_RBCPR_CTL);
	cpr_vreg->save_irq[corner] =
		cpr_read(cpr_vreg, REG_RBIF_IRQ_EN(cpr_vreg->irq_line));
}

static void cpr_corner_restore(struct cpr_regulator *cpr_vreg, int corner)
{
	u32 gcnt, ctl, irq, ro_sel, step_quot;
	int fuse_corner = cpr_vreg->corner_map[corner];
	int i;

	ro_sel = cpr_vreg->cpr_fuse_ro_sel[fuse_corner];
	gcnt = cpr_vreg->gcnt | (cpr_vreg->cpr_fuse_target_quot[fuse_corner] -
					cpr_vreg->quot_adjust[corner]);

	/* Program the step quotient and idle clocks */
	step_quot = ((cpr_vreg->idle_clocks & RBCPR_STEP_QUOT_IDLE_CLK_MASK)
			<< RBCPR_STEP_QUOT_IDLE_CLK_SHIFT) |
		(cpr_vreg->step_quotient[fuse_corner]
			& RBCPR_STEP_QUOT_STEPQUOT_MASK);
	cpr_write(cpr_vreg, REG_RBCPR_STEP_QUOT, step_quot);

	/* Clear the target quotient value and gate count of all ROs */
	for (i = 0; i < CPR_NUM_RING_OSC; i++)
		cpr_write(cpr_vreg, REG_RBCPR_GCNT_TARGET(i), 0);

	cpr_write(cpr_vreg, REG_RBCPR_GCNT_TARGET(ro_sel), gcnt);
	ctl = cpr_vreg->save_ctl[corner];
	cpr_write(cpr_vreg, REG_RBCPR_CTL, ctl);
	irq = cpr_vreg->save_irq[corner];
	cpr_irq_set(cpr_vreg, irq);
	cpr_debug(cpr_vreg, "gcnt = 0x%08x, ctl = 0x%08x, irq = 0x%08x\n",
		  gcnt, ctl, irq);
}

static void cpr_corner_switch(struct cpr_regulator *cpr_vreg, int corner)
{
	if (cpr_vreg->corner == corner)
		return;

	cpr_corner_restore(cpr_vreg, corner);
}

static int cpr_apc_set(struct cpr_regulator *cpr_vreg, u32 new_volt)
{
	int max_volt, rc;

	max_volt = cpr_vreg->ceiling_max;
	rc = regulator_set_voltage(cpr_vreg->vdd_apc, new_volt, max_volt);
	if (rc)
		cpr_err(cpr_vreg, "set: vdd_apc = %d uV: rc=%d\n",
			new_volt, rc);
	return rc;
}

static int cpr_mx_get(struct cpr_regulator *cpr_vreg, int corner, int apc_volt)
{
	int vdd_mx;
	int fuse_corner = cpr_vreg->corner_map[corner];
	int highest_fuse_corner = cpr_vreg->num_fuse_corners;

	switch (cpr_vreg->vdd_mx_vmin_method) {
	case VDD_MX_VMIN_APC:
		vdd_mx = apc_volt;
		break;
	case VDD_MX_VMIN_APC_CORNER_CEILING:
		vdd_mx = cpr_vreg->fuse_ceiling_volt[fuse_corner];
		break;
	case VDD_MX_VMIN_APC_SLOW_CORNER_CEILING:
		vdd_mx = cpr_vreg->fuse_ceiling_volt[highest_fuse_corner];
		break;
	case VDD_MX_VMIN_MX_VMAX:
		vdd_mx = cpr_vreg->vdd_mx_vmax;
		break;
	case VDD_MX_VMIN_APC_FUSE_CORNER_MAP:
		vdd_mx = cpr_vreg->vdd_mx_corner_map[fuse_corner];
		break;
	case VDD_MX_VMIN_APC_CORNER_MAP:
		vdd_mx = cpr_vreg->vdd_mx_corner_map[corner];
		break;
	default:
		vdd_mx = 0;
		break;
	}

	return vdd_mx;
}

static int cpr_mx_set(struct cpr_regulator *cpr_vreg, int corner,
		      int vdd_mx_vmin)
{
	int rc;
	int fuse_corner = cpr_vreg->corner_map[corner];

	rc = regulator_set_voltage(cpr_vreg->vdd_mx, vdd_mx_vmin,
				   cpr_vreg->vdd_mx_vmax);
	cpr_debug(cpr_vreg, "[corner:%d, fuse_corner:%d] %d uV\n", corner,
			fuse_corner, vdd_mx_vmin);

	if (!rc) {
		cpr_vreg->vdd_mx_vmin = vdd_mx_vmin;
	} else {
		cpr_err(cpr_vreg, "set: vdd_mx [corner:%d, fuse_corner:%d] = %d uV failed: rc=%d\n",
			corner, fuse_corner, vdd_mx_vmin, rc);
	}
	return rc;
}

static int cpr_scale_voltage(struct cpr_regulator *cpr_vreg, int corner,
			     int new_apc_volt, enum voltage_change_dir dir)
{
	int rc = 0, vdd_mx_vmin = 0;
	int mem_acc_corner = cpr_vreg->mem_acc_corner_map[corner];
	int fuse_corner = cpr_vreg->corner_map[corner];
	int apc_corner, vsens_corner;

	/* Determine the vdd_mx voltage */
	if (dir != NO_CHANGE && cpr_vreg->vdd_mx != NULL)
		vdd_mx_vmin = cpr_mx_get(cpr_vreg, corner, new_apc_volt);


	if (cpr_vreg->vdd_vsens_voltage && cpr_vreg->vsens_enabled) {
		rc = regulator_disable(cpr_vreg->vdd_vsens_voltage);
		if (!rc)
			cpr_vreg->vsens_enabled = false;
	}

	if (dir == DOWN) {
		if (!rc && cpr_vreg->mem_acc_vreg)
			rc = regulator_set_voltage(cpr_vreg->mem_acc_vreg,
					mem_acc_corner, mem_acc_corner);
		if (!rc && cpr_vreg->rpm_apc_vreg) {
			apc_corner = cpr_vreg->rpm_apc_corner_map[corner];
			rc = regulator_set_voltage(cpr_vreg->rpm_apc_vreg,
						apc_corner, apc_corner);
			if (rc)
				cpr_err(cpr_vreg, "apc_corner voting failed rc=%d\n",
						rc);
		}
	}

	if (!rc && vdd_mx_vmin && dir == UP) {
		if (vdd_mx_vmin != cpr_vreg->vdd_mx_vmin)
			rc = cpr_mx_set(cpr_vreg, corner, vdd_mx_vmin);
	}

	if (!rc)
		rc = cpr_apc_set(cpr_vreg, new_apc_volt);

	if (dir == UP) {
		if (!rc && cpr_vreg->mem_acc_vreg)
			rc = regulator_set_voltage(cpr_vreg->mem_acc_vreg,
					mem_acc_corner, mem_acc_corner);
		if (!rc && cpr_vreg->rpm_apc_vreg) {
			apc_corner = cpr_vreg->rpm_apc_corner_map[corner];
			rc = regulator_set_voltage(cpr_vreg->rpm_apc_vreg,
						apc_corner, apc_corner);
			if (rc)
				cpr_err(cpr_vreg, "apc_corner voting failed rc=%d\n",
						rc);
		}
	}

	if (!rc && vdd_mx_vmin && dir == DOWN) {
		if (vdd_mx_vmin != cpr_vreg->vdd_mx_vmin)
			rc = cpr_mx_set(cpr_vreg, corner, vdd_mx_vmin);
	}

	if (!rc && cpr_vreg->vdd_vsens_corner) {
		vsens_corner = cpr_vreg->vsens_corner_map[fuse_corner];
		rc = regulator_set_voltage(cpr_vreg->vdd_vsens_corner,
					vsens_corner, vsens_corner);
	}
	if (!rc && cpr_vreg->vdd_vsens_voltage) {
		rc = regulator_set_voltage(cpr_vreg->vdd_vsens_voltage,
					cpr_vreg->floor_volt[corner],
					cpr_vreg->ceiling_volt[corner]);
		if (!rc && !cpr_vreg->vsens_enabled) {
			rc = regulator_enable(cpr_vreg->vdd_vsens_voltage);
			if (!rc)
				cpr_vreg->vsens_enabled = true;
		}
	}

	return rc;
}

static void cpr_scale(struct cpr_regulator *cpr_vreg,
		      enum voltage_change_dir dir)
{
	u32 reg_val, error_steps, reg_mask;
	int last_volt, new_volt, corner, fuse_corner;
	u32 gcnt, quot;

	corner = cpr_vreg->corner;
	fuse_corner = cpr_vreg->corner_map[corner];

	reg_val = cpr_read(cpr_vreg, REG_RBCPR_RESULT_0);

	error_steps = (reg_val >> RBCPR_RESULT0_ERROR_STEPS_SHIFT)
				& RBCPR_RESULT0_ERROR_STEPS_MASK;
	last_volt = cpr_vreg->last_volt[corner];

	cpr_debug_irq(cpr_vreg,
			"last_volt[corner:%d, fuse_corner:%d] = %d uV\n",
			corner, fuse_corner, last_volt);

	gcnt = cpr_read(cpr_vreg, REG_RBCPR_GCNT_TARGET
			(cpr_vreg->cpr_fuse_ro_sel[fuse_corner]));
	quot = gcnt & ((1 << RBCPR_GCNT_TARGET_GCNT_SHIFT) - 1);

	if (dir == UP) {
		if (cpr_vreg->clamp_timer_interval
				&& error_steps < cpr_vreg->up_threshold) {
			/*
			 * Handle the case where another measurement started
			 * after the interrupt was triggered due to a core
			 * exiting from power collapse.
			 */
			error_steps = max(cpr_vreg->up_threshold,
					cpr_vreg->vdd_apc_step_up_limit);
		}
		cpr_debug_irq(cpr_vreg,
				"Up: cpr status = 0x%08x (error_steps=%d)\n",
				reg_val, error_steps);

		if (last_volt >= cpr_vreg->ceiling_volt[corner]) {
			cpr_debug_irq(cpr_vreg,
			"[corn:%d, fuse_corn:%d] @ ceiling: %d >= %d: NACK\n",
				corner, fuse_corner, last_volt,
				cpr_vreg->ceiling_volt[corner]);
			cpr_irq_clr_nack(cpr_vreg);

			cpr_debug_irq(cpr_vreg, "gcnt = 0x%08x (quot = %d)\n",
					gcnt, quot);

			/* Maximize the UP threshold */
			reg_mask = RBCPR_CTL_UP_THRESHOLD_MASK <<
					RBCPR_CTL_UP_THRESHOLD_SHIFT;
			reg_val = reg_mask;
			cpr_ctl_modify(cpr_vreg, reg_mask, reg_val);

			/* Disable UP interrupt */
			cpr_irq_set(cpr_vreg, CPR_INT_DEFAULT & ~CPR_INT_UP);

			return;
		}

		if (error_steps > cpr_vreg->vdd_apc_step_up_limit) {
			cpr_debug_irq(cpr_vreg,
				      "%d is over up-limit(%d): Clamp\n",
				      error_steps,
				      cpr_vreg->vdd_apc_step_up_limit);
			error_steps = cpr_vreg->vdd_apc_step_up_limit;
		}

		/* Calculate new voltage */
		new_volt = last_volt + (error_steps * cpr_vreg->step_volt);
		if (new_volt > cpr_vreg->ceiling_volt[corner]) {
			cpr_debug_irq(cpr_vreg,
				      "new_volt(%d) >= ceiling(%d): Clamp\n",
				      new_volt,
				      cpr_vreg->ceiling_volt[corner]);

			new_volt = cpr_vreg->ceiling_volt[corner];
		}

		if (cpr_scale_voltage(cpr_vreg, corner, new_volt, dir)) {
			cpr_irq_clr_nack(cpr_vreg);
			return;
		}
		cpr_vreg->last_volt[corner] = new_volt;

		/* Disable auto nack down */
		reg_mask = RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN;
		reg_val = 0;

		cpr_ctl_modify(cpr_vreg, reg_mask, reg_val);

		/* Re-enable default interrupts */
		cpr_irq_set(cpr_vreg, CPR_INT_DEFAULT);

		/* Ack */
		cpr_irq_clr_ack(cpr_vreg);

		cpr_debug_irq(cpr_vreg,
			"UP: -> new_volt[corner:%d, fuse_corner:%d] = %d uV\n",
			corner, fuse_corner, new_volt);
	} else if (dir == DOWN) {
		if (cpr_vreg->clamp_timer_interval
				&& error_steps < cpr_vreg->down_threshold) {
			/*
			 * Handle the case where another measurement started
			 * after the interrupt was triggered due to a core
			 * exiting from power collapse.
			 */
			error_steps = max(cpr_vreg->down_threshold,
					cpr_vreg->vdd_apc_step_down_limit);
		}
		cpr_debug_irq(cpr_vreg,
			      "Down: cpr status = 0x%08x (error_steps=%d)\n",
			      reg_val, error_steps);

		if (last_volt <= cpr_vreg->floor_volt[corner]) {
			cpr_debug_irq(cpr_vreg,
			"[corn:%d, fuse_corner:%d] @ floor: %d <= %d: NACK\n",
				corner, fuse_corner, last_volt,
				cpr_vreg->floor_volt[corner]);
			cpr_irq_clr_nack(cpr_vreg);

			cpr_debug_irq(cpr_vreg, "gcnt = 0x%08x (quot = %d)\n",
					gcnt, quot);

			/* Enable auto nack down */
			reg_mask = RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN;
			reg_val = RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN;

			cpr_ctl_modify(cpr_vreg, reg_mask, reg_val);

			/* Disable DOWN interrupt */
			cpr_irq_set(cpr_vreg, CPR_INT_DEFAULT & ~CPR_INT_DOWN);

			return;
		}

		if (error_steps > cpr_vreg->vdd_apc_step_down_limit) {
			cpr_debug_irq(cpr_vreg,
				      "%d is over down-limit(%d): Clamp\n",
				      error_steps,
				      cpr_vreg->vdd_apc_step_down_limit);
			error_steps = cpr_vreg->vdd_apc_step_down_limit;
		}

		/* Calculte new voltage */
		new_volt = last_volt - (error_steps * cpr_vreg->step_volt);
		if (new_volt < cpr_vreg->floor_volt[corner]) {
			cpr_debug_irq(cpr_vreg,
				      "new_volt(%d) < floor(%d): Clamp\n",
				      new_volt,
				      cpr_vreg->floor_volt[corner]);
			new_volt = cpr_vreg->floor_volt[corner];
		}

		if (cpr_scale_voltage(cpr_vreg, corner, new_volt, dir)) {
			cpr_irq_clr_nack(cpr_vreg);
			return;
		}
		cpr_vreg->last_volt[corner] = new_volt;

		/* Restore default threshold for UP */
		reg_mask = RBCPR_CTL_UP_THRESHOLD_MASK <<
				RBCPR_CTL_UP_THRESHOLD_SHIFT;
		reg_val = cpr_vreg->up_threshold <<
				RBCPR_CTL_UP_THRESHOLD_SHIFT;
		cpr_ctl_modify(cpr_vreg, reg_mask, reg_val);

		/* Re-enable default interrupts */
		cpr_irq_set(cpr_vreg, CPR_INT_DEFAULT);

		/* Ack */
		cpr_irq_clr_ack(cpr_vreg);

		cpr_debug_irq(cpr_vreg,
		"DOWN: -> new_volt[corner:%d, fuse_corner:%d] = %d uV\n",
			corner, fuse_corner, new_volt);
	}
}

static irqreturn_t cpr_irq_handler(int irq, void *dev)
{
	struct cpr_regulator *cpr_vreg = dev;
	u32 reg_val;

	mutex_lock(&cpr_vreg->cpr_mutex);

	reg_val = cpr_read(cpr_vreg, REG_RBIF_IRQ_STATUS);
	if (cpr_vreg->flags & FLAGS_IGNORE_1ST_IRQ_STATUS)
		reg_val = cpr_read(cpr_vreg, REG_RBIF_IRQ_STATUS);

	cpr_debug_irq(cpr_vreg, "IRQ_STATUS = 0x%02X\n", reg_val);

	if (!cpr_ctl_is_enabled(cpr_vreg)) {
		cpr_debug_irq(cpr_vreg, "CPR is disabled\n");
		goto _exit;
	} else if (cpr_ctl_is_busy(cpr_vreg)
			&& !cpr_vreg->clamp_timer_interval) {
		cpr_debug_irq(cpr_vreg, "CPR measurement is not ready\n");
		goto _exit;
	} else if (!cpr_is_allowed(cpr_vreg)) {
		reg_val = cpr_read(cpr_vreg, REG_RBCPR_CTL);
		cpr_err(cpr_vreg, "Interrupt broken? RBCPR_CTL = 0x%02X\n",
			reg_val);
		goto _exit;
	}

	/* Following sequence of handling is as per each IRQ's priority */
	if (reg_val & CPR_INT_UP) {
		cpr_scale(cpr_vreg, UP);
	} else if (reg_val & CPR_INT_DOWN) {
		cpr_scale(cpr_vreg, DOWN);
	} else if (reg_val & CPR_INT_MIN) {
		cpr_irq_clr_nack(cpr_vreg);
	} else if (reg_val & CPR_INT_MAX) {
		cpr_irq_clr_nack(cpr_vreg);
	} else if (reg_val & CPR_INT_MID) {
		/* RBCPR_CTL_SW_AUTO_CONT_ACK_EN is enabled */
		cpr_debug_irq(cpr_vreg, "IRQ occurred for Mid Flag\n");
	} else {
		cpr_debug_irq(cpr_vreg,
			"IRQ occurred for unknown flag (0x%08x)\n", reg_val);
	}

	/* Save register values for the corner */
	cpr_corner_save(cpr_vreg, cpr_vreg->corner);

_exit:
	mutex_unlock(&cpr_vreg->cpr_mutex);
	return IRQ_HANDLED;
}

/**
 * cmp_int() - int comparison function to be passed into the sort() function
 *		which leads to ascending sorting
 * @a:			First int value
 * @b:			Second int value
 *
 * Return: >0 if a > b, 0 if a == b, <0 if a < b
 */
static int cmp_int(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

static int cpr_get_aging_quot_delta(struct cpr_regulator *cpr_vreg,
			struct cpr_aging_sensor_info *aging_sensor_info)
{
	int quot_min, quot_max, is_aging_measurement, aging_measurement_count;
	int quot_min_scaled, quot_max_scaled, quot_delta_scaled_sum;
	int retries, rc = 0, sel_fast = 0, i, quot_delta_scaled;
	u32 val, gcnt_ref, gcnt;
	int *quot_delta_results, filtered_count;


	quot_delta_results = kcalloc(CPR_AGING_MEASUREMENT_ITERATIONS,
			sizeof(*quot_delta_results), GFP_ATOMIC);
	if (!quot_delta_results)
		return -ENOMEM;

	/* Clear the target quotient value and gate count of all ROs */
	for (i = 0; i < CPR_NUM_RING_OSC; i++)
		cpr_write(cpr_vreg, REG_RBCPR_GCNT_TARGET(i), 0);

	/* Program GCNT0/1 for getting aging data */
	gcnt_ref = (cpr_vreg->ref_clk_khz * cpr_vreg->gcnt_time_us) / 1000;
	gcnt = gcnt_ref * 3 / 2;
	val = (gcnt & RBCPR_GCNT_TARGET_GCNT_MASK) <<
			RBCPR_GCNT_TARGET_GCNT_SHIFT;
	cpr_write(cpr_vreg, REG_RBCPR_GCNT_TARGET(0), val);
	cpr_write(cpr_vreg, REG_RBCPR_GCNT_TARGET(1), val);

	val = cpr_read(cpr_vreg, REG_RBCPR_GCNT_TARGET(0));
	cpr_debug(cpr_vreg, "RBCPR_GCNT_TARGET0 = 0x%08x\n", val);

	val = cpr_read(cpr_vreg, REG_RBCPR_GCNT_TARGET(1));
	cpr_debug(cpr_vreg, "RBCPR_GCNT_TARGET1 = 0x%08x\n", val);

	/* Program TIMER_INTERVAL to zero */
	cpr_write(cpr_vreg, REG_RBCPR_TIMER_INTERVAL, 0);

	/* Bypass sensors in collapsible domain */
	if (cpr_vreg->aging_info->aging_sensor_bypass)
		cpr_write(cpr_vreg, REG_RBCPR_SENSOR_BYPASS0,
			(cpr_vreg->aging_info->aging_sensor_bypass &
		RBCPR_SENSOR_MASK0_SENSOR(aging_sensor_info->sensor_id)));

	/* Mask other sensors */
	cpr_write(cpr_vreg, REG_RBCPR_SENSOR_MASK0,
		RBCPR_SENSOR_MASK0_SENSOR(aging_sensor_info->sensor_id));
	val = cpr_read(cpr_vreg, REG_RBCPR_SENSOR_MASK0);
	cpr_debug(cpr_vreg, "RBCPR_SENSOR_MASK0 = 0x%08x\n", val);

	/* Enable cpr controller */
	cpr_ctl_modify(cpr_vreg, RBCPR_CTL_LOOP_EN, RBCPR_CTL_LOOP_EN);

	/* Make sure cpr starts measurement with toggling busy bit */
	mb();

	/* Wait and Ignore the first measurement. Time-out after 5ms */
	retries = 50;
	while (retries-- && cpr_ctl_is_busy(cpr_vreg))
		udelay(100);

	if (retries < 0) {
		cpr_err(cpr_vreg, "Aging calibration failed\n");
		rc = -EBUSY;
		goto _exit;
	}

	/* Set age page mode */
	cpr_write(cpr_vreg, REG_RBCPR_HTOL_AGE, RBCPR_HTOL_AGE_PAGE);

	aging_measurement_count = 0;
	quot_delta_scaled_sum = 0;

	for (i = 0; i < CPR_AGING_MEASUREMENT_ITERATIONS; i++) {
		/* Send cont nack */
		cpr_write(cpr_vreg, REG_RBIF_CONT_NACK_CMD, 1);

		/*
		 * Make sure cpr starts next measurement with
		 * toggling busy bit
		 */
		mb();

		/*
		 * Wait for controller to finish measurement
		 * and time-out after 5ms
		 */
		retries = 50;
		while (retries-- && cpr_ctl_is_busy(cpr_vreg))
			udelay(100);

		if (retries < 0) {
			cpr_err(cpr_vreg, "Aging calibration failed\n");
			rc = -EBUSY;
			goto _exit;
		}

		/* Check for PAGE_IS_AGE flag in status register */
		val = cpr_read(cpr_vreg, REG_RBCPR_HTOL_AGE);
		is_aging_measurement = val & RBCPR_AGE_DATA_STATUS;

		val = cpr_read(cpr_vreg, REG_RBCPR_RESULT_1);
		sel_fast = RBCPR_RESULT_1_SEL_FAST(val);
		cpr_debug(cpr_vreg, "RBCPR_RESULT_1 = 0x%08x\n", val);

		val = cpr_read(cpr_vreg, REG_RBCPR_DEBUG1);
		cpr_debug(cpr_vreg, "RBCPR_DEBUG1 = 0x%08x\n", val);

		if (sel_fast == 1) {
			quot_min = RBCPR_DEBUG1_QUOT_FAST(val);
			quot_max = RBCPR_DEBUG1_QUOT_SLOW(val);
		} else {
			quot_min = RBCPR_DEBUG1_QUOT_SLOW(val);
			quot_max = RBCPR_DEBUG1_QUOT_FAST(val);
		}

		/*
		 * Scale the quotients so that they are equivalent to the fused
		 * values.  This accounts for the difference in measurement
		 * interval times.
		 */

		quot_min_scaled = quot_min * (gcnt_ref + 1) / (gcnt + 1);
		quot_max_scaled = quot_max * (gcnt_ref + 1) / (gcnt + 1);

		quot_delta_scaled = 0;
		if (is_aging_measurement) {
			quot_delta_scaled = quot_min_scaled - quot_max_scaled;
			quot_delta_results[aging_measurement_count++] =
					quot_delta_scaled;
		}

		cpr_debug(cpr_vreg,
			"Age sensor[%d]: measurement[%d]: page_is_age=%u quot_min = %d, quot_max = %d quot_min_scaled = %d, quot_max_scaled = %d quot_delta_scaled = %d\n",
			aging_sensor_info->sensor_id, i, is_aging_measurement,
			quot_min, quot_max, quot_min_scaled, quot_max_scaled,
			quot_delta_scaled);
	}

	filtered_count
		= aging_measurement_count - CPR_AGING_MEASUREMENT_FILTER * 2;
	if (filtered_count > 0) {
		sort(quot_delta_results, aging_measurement_count,
			sizeof(*quot_delta_results), cmp_int, NULL);

		quot_delta_scaled_sum = 0;
		for (i = 0; i < filtered_count; i++)
			quot_delta_scaled_sum
				+= quot_delta_results[i
					+ CPR_AGING_MEASUREMENT_FILTER];

		aging_sensor_info->current_quot_diff
			= quot_delta_scaled_sum / filtered_count;
		cpr_debug(cpr_vreg,
			"Age sensor[%d]: average aging quotient delta = %d (count = %d)\n",
			aging_sensor_info->sensor_id,
			aging_sensor_info->current_quot_diff, filtered_count);
	} else {
		cpr_err(cpr_vreg, "%d aging measurements completed after %d iterations\n",
			aging_measurement_count,
			CPR_AGING_MEASUREMENT_ITERATIONS);
		rc = -EBUSY;
	}

_exit:
	/* Clear age page bit */
	cpr_write(cpr_vreg, REG_RBCPR_HTOL_AGE, 0x0);

	/* Disable the CPR controller after aging procedure */
	cpr_ctl_modify(cpr_vreg, RBCPR_CTL_LOOP_EN, 0x0);

	/* Clear the sensor bypass */
	if (cpr_vreg->aging_info->aging_sensor_bypass)
		cpr_write(cpr_vreg, REG_RBCPR_SENSOR_BYPASS0, 0x0);

	/* Unmask all sensors */
	cpr_write(cpr_vreg, REG_RBCPR_SENSOR_MASK0, 0x0);

	/* Clear gcnt0/1 registers */
	cpr_write(cpr_vreg, REG_RBCPR_GCNT_TARGET(0), 0x0);
	cpr_write(cpr_vreg, REG_RBCPR_GCNT_TARGET(1), 0x0);

	/* Program the delay count for the timer */
	val = (cpr_vreg->ref_clk_khz * cpr_vreg->timer_delay_us) / 1000;
	cpr_write(cpr_vreg, REG_RBCPR_TIMER_INTERVAL, val);

	kfree(quot_delta_results);

	return rc;
}

static void cpr_de_aging_adjustment(void *data)
{
	struct cpr_regulator *cpr_vreg = (struct cpr_regulator *)data;
	struct cpr_aging_info *aging_info = cpr_vreg->aging_info;
	struct cpr_aging_sensor_info *aging_sensor_info;
	int i, num_aging_sensors, retries, rc = 0;
	int max_quot_diff = 0, ro_sel = 0;
	u32 voltage_adjust, aging_voltage_adjust = 0;

	aging_sensor_info = aging_info->sensor_info;
	num_aging_sensors = aging_info->num_aging_sensors;

	for (i = 0; i < num_aging_sensors; i++, aging_sensor_info++) {
		retries = 2;
		while (retries--) {
			rc = cpr_get_aging_quot_delta(cpr_vreg,
					aging_sensor_info);
			if (!rc)
				break;
		}
		if (rc && retries < 0) {
			cpr_err(cpr_vreg, "error in age calibration: rc = %d\n",
				rc);
			aging_info->cpr_aging_error = true;
			return;
		}

		max_quot_diff = max(max_quot_diff,
					(aging_sensor_info->current_quot_diff -
					aging_sensor_info->initial_quot_diff));
	}

	cpr_debug(cpr_vreg, "Max aging quot delta = %d\n",
				max_quot_diff);
	aging_voltage_adjust = DIV_ROUND_UP(max_quot_diff * 1000000,
					aging_info->aging_ro_kv);

	for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners; i++) {
		/* Remove initial max aging adjustment */
		ro_sel = cpr_vreg->cpr_fuse_ro_sel[i];
		cpr_vreg->cpr_fuse_target_quot[i] -=
				(aging_info->cpr_ro_kv[ro_sel]
				* aging_info->max_aging_margin) / 1000000;
		aging_info->voltage_adjust[i] = 0;

		if (aging_voltage_adjust > 0) {
			/* Add required aging adjustment */
			voltage_adjust = (aging_voltage_adjust
					* aging_info->aging_derate[i]) / 1000;
			voltage_adjust = min(voltage_adjust,
						aging_info->max_aging_margin);
			cpr_vreg->cpr_fuse_target_quot[i] +=
					(aging_info->cpr_ro_kv[ro_sel]
					* voltage_adjust) / 1000000;
			aging_info->voltage_adjust[i] = voltage_adjust;
		}
	}
}

static int cpr_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct cpr_regulator *cpr_vreg = rdev_get_drvdata(rdev);

	return cpr_vreg->vreg_enabled;
}

static int cpr_regulator_enable(struct regulator_dev *rdev)
{
	struct cpr_regulator *cpr_vreg = rdev_get_drvdata(rdev);
	int rc;

	/* Enable dependency power before vdd_apc */
	if (cpr_vreg->vdd_mx) {
		rc = regulator_enable(cpr_vreg->vdd_mx);
		if (rc) {
			cpr_err(cpr_vreg, "regulator_enable: vdd_mx: rc=%d\n",
				rc);
			return rc;
		}
	}

	rc = regulator_enable(cpr_vreg->vdd_apc);
	if (rc) {
		cpr_err(cpr_vreg, "regulator_enable: vdd_apc: rc=%d\n", rc);
		return rc;
	}

	mutex_lock(&cpr_vreg->cpr_mutex);
	cpr_vreg->vreg_enabled = true;
	if (cpr_is_allowed(cpr_vreg) && cpr_vreg->corner) {
		cpr_irq_clr(cpr_vreg);
		cpr_corner_restore(cpr_vreg, cpr_vreg->corner);
		cpr_ctl_enable(cpr_vreg, cpr_vreg->corner);
	}
	mutex_unlock(&cpr_vreg->cpr_mutex);

	return rc;
}

static int cpr_regulator_disable(struct regulator_dev *rdev)
{
	struct cpr_regulator *cpr_vreg = rdev_get_drvdata(rdev);
	int rc;

	rc = regulator_disable(cpr_vreg->vdd_apc);
	if (!rc) {
		if (cpr_vreg->vdd_mx)
			rc = regulator_disable(cpr_vreg->vdd_mx);

		if (rc) {
			cpr_err(cpr_vreg, "regulator_disable: vdd_mx: rc=%d\n",
				rc);
			return rc;
		}

		mutex_lock(&cpr_vreg->cpr_mutex);
		cpr_vreg->vreg_enabled = false;
		if (cpr_is_allowed(cpr_vreg))
			cpr_ctl_disable(cpr_vreg);
		mutex_unlock(&cpr_vreg->cpr_mutex);
	} else {
		cpr_err(cpr_vreg, "regulator_disable: vdd_apc: rc=%d\n", rc);
	}

	return rc;
}

static int cpr_calculate_de_aging_margin(struct cpr_regulator *cpr_vreg)
{
	struct cpr_aging_info *aging_info = cpr_vreg->aging_info;
	enum voltage_change_dir change_dir = NO_CHANGE;
	u32 save_ctl, save_irq;
	cpumask_t tmp_mask;
	int rc, i, current_mode;

	save_ctl = cpr_read(cpr_vreg, REG_RBCPR_CTL);
	save_irq = cpr_read(cpr_vreg, REG_RBIF_IRQ_EN(cpr_vreg->irq_line));

	/* Disable interrupt and CPR */
	cpr_irq_set(cpr_vreg, 0);
	cpr_write(cpr_vreg, REG_RBCPR_CTL, 0);

	if (aging_info->aging_corner > cpr_vreg->corner)
		change_dir = UP;
	else if (aging_info->aging_corner < cpr_vreg->corner)
		change_dir = DOWN;

	/* set selected reference voltage for de-aging */
	rc = cpr_scale_voltage(cpr_vreg,
				aging_info->aging_corner,
				aging_info->aging_ref_voltage,
				change_dir);
	if (rc) {
		cpr_err(cpr_vreg, "Unable to set aging reference voltage, rc = %d\n",
			rc);
		return rc;
	}

	current_mode = regulator_get_mode(cpr_vreg->vdd_apc);
	if (current_mode < 0) {
		cpr_err(cpr_vreg, "Failed to get vdd-supply mode, error=%d\n",
			current_mode);
		return current_mode;
	}

	/* Force PWM mode */
	rc = regulator_set_mode(cpr_vreg->vdd_apc, REGULATOR_MODE_NORMAL);
	if (rc) {
		cpr_err(cpr_vreg, "unable to configure vdd-supply for mode=%u, rc=%d\n",
			REGULATOR_MODE_NORMAL, rc);
		return rc;
	}

	cpus_read_lock();
	cpumask_and(&tmp_mask, &cpr_vreg->cpu_mask, cpu_online_mask);
	if (!cpumask_empty(&tmp_mask)) {
		smp_call_function_any(&tmp_mask,
					cpr_de_aging_adjustment,
					cpr_vreg, true);
		aging_info->cpr_aging_done = true;
		if (!aging_info->cpr_aging_error)
			for (i = CPR_FUSE_CORNER_MIN;
					i <= cpr_vreg->num_fuse_corners; i++)
				cpr_info(cpr_vreg, "Corner[%d]: age adjusted target quot = %d\n",
					i, cpr_vreg->cpr_fuse_target_quot[i]);
	}

	cpus_read_unlock();

	/* Set to initial mode */
	rc = regulator_set_mode(cpr_vreg->vdd_apc, current_mode);
	if (rc) {
		cpr_err(cpr_vreg, "unable to configure vdd-supply for mode=%u, rc=%d\n",
			current_mode, rc);
		return rc;
	}

	/* Clear interrupts */
	cpr_irq_clr(cpr_vreg);

	/* Restore register values */
	cpr_irq_set(cpr_vreg, save_irq);
	cpr_write(cpr_vreg, REG_RBCPR_CTL, save_ctl);

	return rc;
}

/* Note that cpr_vreg->cpr_mutex must be held by the caller. */
static int cpr_regulator_set_voltage(struct regulator_dev *rdev,
		int corner, bool reset_quot)
{
	struct cpr_regulator *cpr_vreg = rdev_get_drvdata(rdev);
	struct cpr_aging_info *aging_info = cpr_vreg->aging_info;
	int rc;
	int new_volt;
	enum voltage_change_dir change_dir = NO_CHANGE;
	int fuse_corner = cpr_vreg->corner_map[corner];

	if (cpr_is_allowed(cpr_vreg)) {
		cpr_ctl_disable(cpr_vreg);
		new_volt = cpr_vreg->last_volt[corner];
	} else {
		new_volt = cpr_vreg->open_loop_volt[corner];
	}

	cpr_debug(cpr_vreg, "[corner:%d, fuse_corner:%d] = %d uV\n",
		corner, fuse_corner, new_volt);

	if (corner > cpr_vreg->corner)
		change_dir = UP;
	else if (corner < cpr_vreg->corner)
		change_dir = DOWN;

	/* Read age sensor data and apply de-aging adjustments */
	if (cpr_vreg->vreg_enabled && aging_info && !aging_info->cpr_aging_done
		&& (corner <= aging_info->aging_corner)) {
		rc = cpr_calculate_de_aging_margin(cpr_vreg);
		if (rc) {
			cpr_err(cpr_vreg, "failed in de-aging calibration: rc=%d\n",
				rc);
		} else {
			change_dir = NO_CHANGE;
			if (corner > aging_info->aging_corner)
				change_dir = UP;
			else if (corner  < aging_info->aging_corner)
				change_dir = DOWN;
		}
		reset_quot = true;
	}

	rc = cpr_scale_voltage(cpr_vreg, corner, new_volt, change_dir);
	if (rc)
		return rc;

	if (cpr_vreg->vdd_mode_map) {
		rc = regulator_set_mode(cpr_vreg->vdd_apc,
					cpr_vreg->vdd_mode_map[corner]);
		if (rc) {
			cpr_err(cpr_vreg, "unable to configure vdd-supply for mode=%u, rc=%d\n",
				cpr_vreg->vdd_mode_map[corner], rc);
			return rc;
		}
	}


	if (cpr_is_allowed(cpr_vreg) && cpr_vreg->vreg_enabled) {
		cpr_irq_clr(cpr_vreg);
		if (reset_quot)
			cpr_corner_restore(cpr_vreg, corner);
		else
			cpr_corner_switch(cpr_vreg, corner);
		cpr_ctl_enable(cpr_vreg, corner);
	}

	cpr_vreg->corner = corner;

	return rc;
}

static int cpr_regulator_set_voltage_op(struct regulator_dev *rdev,
		int corner, int corner_max, unsigned int *selector)
{
	struct cpr_regulator *cpr_vreg = rdev_get_drvdata(rdev);
	int rc;

	mutex_lock(&cpr_vreg->cpr_mutex);
	rc = cpr_regulator_set_voltage(rdev, corner, false);
	mutex_unlock(&cpr_vreg->cpr_mutex);

	return rc;
}

static int cpr_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct cpr_regulator *cpr_vreg = rdev_get_drvdata(rdev);

	return cpr_vreg->corner;
}

static const struct regulator_ops cpr_corner_ops = {
	.enable			= cpr_regulator_enable,
	.disable		= cpr_regulator_disable,
	.is_enabled		= cpr_regulator_is_enabled,
	.set_voltage		= cpr_regulator_set_voltage_op,
	.get_voltage		= cpr_regulator_get_voltage,
};

#ifdef CONFIG_PM
static int cpr_suspend(struct cpr_regulator *cpr_vreg)
{
	cpr_debug(cpr_vreg, "suspend\n");

	cpr_ctl_disable(cpr_vreg);

	cpr_irq_clr(cpr_vreg);

	return 0;
}

static int cpr_resume(struct cpr_regulator *cpr_vreg)

{
	cpr_debug(cpr_vreg, "resume\n");

	cpr_irq_clr(cpr_vreg);

	cpr_ctl_enable(cpr_vreg, cpr_vreg->corner);

	return 0;
}

static int cpr_regulator_suspend(struct platform_device *pdev,
				 pm_message_t state)
{
	struct cpr_regulator *cpr_vreg = platform_get_drvdata(pdev);
	int rc = 0;

	mutex_lock(&cpr_vreg->cpr_mutex);

	if (cpr_is_allowed(cpr_vreg))
		rc = cpr_suspend(cpr_vreg);

	cpr_vreg->is_cpr_suspended = true;

	mutex_unlock(&cpr_vreg->cpr_mutex);

	return rc;
}

static int cpr_regulator_resume(struct platform_device *pdev)
{
	struct cpr_regulator *cpr_vreg = platform_get_drvdata(pdev);
	int rc = 0;

	mutex_lock(&cpr_vreg->cpr_mutex);

	cpr_vreg->is_cpr_suspended = false;

	if (cpr_is_allowed(cpr_vreg))
		rc = cpr_resume(cpr_vreg);

	mutex_unlock(&cpr_vreg->cpr_mutex);

	return rc;
}
#else
#define cpr_regulator_suspend NULL
#define cpr_regulator_resume NULL
#endif

static int cpr_config(struct cpr_regulator *cpr_vreg, struct device *dev)
{
	int i;
	u32 val, gcnt, reg;
	void __iomem *rbcpr_clk;
	int size;

	if (cpr_vreg->rbcpr_clk_addr) {
		/* Use 19.2 MHz clock for CPR. */
		rbcpr_clk = ioremap(cpr_vreg->rbcpr_clk_addr, 4);
		if (!rbcpr_clk) {
			cpr_err(cpr_vreg, "Unable to map rbcpr_clk\n");
			return -EINVAL;
		}
		reg = readl_relaxed(rbcpr_clk);
		reg &= ~RBCPR_CLK_SEL_MASK;
		reg |= RBCPR_CLK_SEL_19P2_MHZ & RBCPR_CLK_SEL_MASK;
		writel_relaxed(reg, rbcpr_clk);
		iounmap(rbcpr_clk);
	}

	/* Disable interrupt and CPR */
	cpr_write(cpr_vreg, REG_RBIF_IRQ_EN(cpr_vreg->irq_line), 0);
	cpr_write(cpr_vreg, REG_RBCPR_CTL, 0);

	/* Program the default HW Ceiling, Floor and vlevel */
	val = ((RBIF_LIMIT_CEILING_DEFAULT & RBIF_LIMIT_CEILING_MASK)
			<< RBIF_LIMIT_CEILING_SHIFT)
		| (RBIF_LIMIT_FLOOR_DEFAULT & RBIF_LIMIT_FLOOR_MASK);
	cpr_write(cpr_vreg, REG_RBIF_LIMIT, val);
	cpr_write(cpr_vreg, REG_RBIF_SW_VLEVEL, RBIF_SW_VLEVEL_DEFAULT);

	/* Clear the target quotient value and gate count of all ROs */
	for (i = 0; i < CPR_NUM_RING_OSC; i++)
		cpr_write(cpr_vreg, REG_RBCPR_GCNT_TARGET(i), 0);

	/* Init and save gcnt */
	gcnt = (cpr_vreg->ref_clk_khz * cpr_vreg->gcnt_time_us) / 1000;
	gcnt = (gcnt & RBCPR_GCNT_TARGET_GCNT_MASK) <<
			RBCPR_GCNT_TARGET_GCNT_SHIFT;
	cpr_vreg->gcnt = gcnt;

	/* Program the delay count for the timer */
	val = (cpr_vreg->ref_clk_khz * cpr_vreg->timer_delay_us) / 1000;
	cpr_write(cpr_vreg, REG_RBCPR_TIMER_INTERVAL, val);
	cpr_info(cpr_vreg, "Timer count: 0x%0x (for %d us)\n", val,
		cpr_vreg->timer_delay_us);

	/* Program Consecutive Up & Down */
	val = ((cpr_vreg->timer_cons_down & RBIF_TIMER_ADJ_CONS_DOWN_MASK)
			<< RBIF_TIMER_ADJ_CONS_DOWN_SHIFT) |
	       (cpr_vreg->timer_cons_up & RBIF_TIMER_ADJ_CONS_UP_MASK) |
	       ((cpr_vreg->clamp_timer_interval & RBIF_TIMER_ADJ_CLAMP_INT_MASK)
			<< RBIF_TIMER_ADJ_CLAMP_INT_SHIFT);
	cpr_write(cpr_vreg, REG_RBIF_TIMER_ADJUST, val);

	/* Program the control register */
	cpr_vreg->up_threshold &= RBCPR_CTL_UP_THRESHOLD_MASK;
	cpr_vreg->down_threshold &= RBCPR_CTL_DN_THRESHOLD_MASK;
	val = (cpr_vreg->up_threshold << RBCPR_CTL_UP_THRESHOLD_SHIFT)
		| (cpr_vreg->down_threshold << RBCPR_CTL_DN_THRESHOLD_SHIFT);
	val |= RBCPR_CTL_TIMER_EN | RBCPR_CTL_COUNT_MODE;
	val |= RBCPR_CTL_SW_AUTO_CONT_ACK_EN;
	cpr_write(cpr_vreg, REG_RBCPR_CTL, val);

	cpr_irq_set(cpr_vreg, CPR_INT_DEFAULT);

	val = cpr_read(cpr_vreg, REG_RBCPR_VERSION);
	if (val <= RBCPR_VER_2)
		cpr_vreg->flags |= FLAGS_IGNORE_1ST_IRQ_STATUS;

	size = cpr_vreg->num_corners + 1;
	cpr_vreg->save_ctl = devm_kzalloc(dev, sizeof(int) * size, GFP_KERNEL);
	cpr_vreg->save_irq = devm_kzalloc(dev, sizeof(int) * size, GFP_KERNEL);
	if (!cpr_vreg->save_ctl || !cpr_vreg->save_irq)
		return -ENOMEM;

	for (i = 1; i < size; i++)
		cpr_corner_save(cpr_vreg, i);

	return 0;
}

static int cpr_fuse_is_setting_expected(struct cpr_regulator *cpr_vreg,
					u32 sel_array[5])
{
	u64 fuse_bits;
	u32 ret;

	fuse_bits = cpr_read_efuse_row(cpr_vreg, sel_array[0], sel_array[4]);
	ret = (fuse_bits >> sel_array[1]) & ((1 << sel_array[2]) - 1);
	if (ret == sel_array[3])
		ret = 1;
	else
		ret = 0;

	cpr_info(cpr_vreg, "[row:%d] = 0x%llx @%d:%d == %d ?: %s\n",
			sel_array[0], fuse_bits,
			sel_array[1], sel_array[2],
			sel_array[3],
			(ret == 1) ? "yes" : "no");
	return ret;
}

static int cpr_voltage_uplift_wa_inc_volt(struct cpr_regulator *cpr_vreg,
					struct device_node *of_node)
{
	u32 uplift_voltage;
	u32 uplift_max_volt = 0;
	int highest_fuse_corner = cpr_vreg->num_fuse_corners;
	int rc;

	rc = of_property_read_u32(of_node,
		"qcom,cpr-uplift-voltage", &uplift_voltage);
	if (rc < 0) {
		cpr_err(cpr_vreg, "cpr-uplift-voltage is missing, rc = %d", rc);
		return rc;
	}
	rc = of_property_read_u32(of_node,
		"qcom,cpr-uplift-max-volt", &uplift_max_volt);
	if (rc < 0) {
		cpr_err(cpr_vreg, "cpr-uplift-max-volt is missing, rc = %d",
			rc);
		return rc;
	}

	cpr_vreg->pvs_corner_v[highest_fuse_corner] += uplift_voltage;
	if (cpr_vreg->pvs_corner_v[highest_fuse_corner] > uplift_max_volt)
		cpr_vreg->pvs_corner_v[highest_fuse_corner] = uplift_max_volt;

	return rc;
}

static int cpr_adjust_init_voltages(struct device_node *of_node,
				struct cpr_regulator *cpr_vreg)
{
	int tuple_count, tuple_match, i;
	u32 index;
	u32 volt_adjust = 0;
	int len = 0;
	int rc = 0;

	if (!of_find_property(of_node, "qcom,cpr-init-voltage-adjustment",
				&len)) {
		/* No initial voltage adjustment needed. */
		return 0;
	}

	if (cpr_vreg->cpr_fuse_map_count) {
		if (cpr_vreg->cpr_fuse_map_match == FUSE_MAP_NO_MATCH) {
			/*
			 * No matching index to use for initial voltage
			 * adjustment.
			 */
			return 0;
		}
		tuple_count = cpr_vreg->cpr_fuse_map_count;
		tuple_match = cpr_vreg->cpr_fuse_map_match;
	} else {
		tuple_count = 1;
		tuple_match = 0;
	}

	if (len != cpr_vreg->num_fuse_corners * tuple_count * sizeof(u32)) {
		cpr_err(cpr_vreg, "qcom,cpr-init-voltage-adjustment length=%d is invalid\n",
			len);
		return -EINVAL;
	}

	for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners; i++) {
		index = tuple_match * cpr_vreg->num_fuse_corners
				+ i - CPR_FUSE_CORNER_MIN;
		rc = of_property_read_u32_index(of_node,
			"qcom,cpr-init-voltage-adjustment", index,
			&volt_adjust);
		if (rc) {
			cpr_err(cpr_vreg, "could not read qcom,cpr-init-voltage-adjustment index %u, rc=%d\n",
				index, rc);
			return rc;
		}

		if (volt_adjust) {
			cpr_vreg->pvs_corner_v[i] += volt_adjust;
			cpr_info(cpr_vreg, "adjusted initial voltage[%d]: %d -> %d uV\n",
				i, cpr_vreg->pvs_corner_v[i] - volt_adjust,
				cpr_vreg->pvs_corner_v[i]);
		}
	}

	return rc;
}

/*
 * Property qcom,cpr-fuse-init-voltage specifies the fuse position of the
 * initial voltage for each fuse corner. MSB of the fuse value is a sign
 * bit, and the remaining bits define the steps of the offset. Each step has
 * units of microvolts defined in the qcom,cpr-fuse-init-voltage-step property.
 * The initial voltages can be calculated using the formula:
 * pvs_corner_v[corner] = ceiling_volt[corner] + (sign * steps * step_size_uv)
 */
static int cpr_pvs_per_corner_init(struct device_node *of_node,
				struct cpr_regulator *cpr_vreg)
{
	u64 efuse_bits;
	int i, size, sign, steps, step_size_uv, rc;
	u32 *fuse_sel, *tmp, *ref_uv;
	struct property *prop;
	char *init_volt_str;

	init_volt_str = cpr_vreg->cpr_fuse_redundant
			? "qcom,cpr-fuse-redun-init-voltage"
			: "qcom,cpr-fuse-init-voltage";

	prop = of_find_property(of_node, init_volt_str, NULL);
	if (!prop) {
		cpr_err(cpr_vreg, "%s is missing\n", init_volt_str);
		return -EINVAL;
	}
	size = prop->length / sizeof(u32);
	if (size != cpr_vreg->num_fuse_corners * 4) {
		cpr_err(cpr_vreg,
			"fuse position for init voltages is invalid\n");
		return -EINVAL;
	}
	fuse_sel = kzalloc(sizeof(u32) * size, GFP_KERNEL);
	if (!fuse_sel)
		return -ENOMEM;
	rc = of_property_read_u32_array(of_node, init_volt_str,
							fuse_sel, size);
	if (rc < 0) {
		cpr_err(cpr_vreg,
			"read cpr-fuse-init-voltage failed, rc = %d\n", rc);
		kfree(fuse_sel);
		return rc;
	}
	rc = of_property_read_u32(of_node, "qcom,cpr-init-voltage-step",
							&step_size_uv);
	if (rc < 0) {
		cpr_err(cpr_vreg,
			"read cpr-init-voltage-step failed, rc = %d\n", rc);
		kfree(fuse_sel);
		return rc;
	}

	ref_uv = kzalloc((cpr_vreg->num_fuse_corners + 1) * sizeof(*ref_uv),
			GFP_KERNEL);
	if (!ref_uv) {
		kfree(fuse_sel);
		return -ENOMEM;
	}

	rc = of_property_read_u32_array(of_node, "qcom,cpr-init-voltage-ref",
		&ref_uv[CPR_FUSE_CORNER_MIN], cpr_vreg->num_fuse_corners);
	if (rc < 0) {
		cpr_err(cpr_vreg,
			"read qcom,cpr-init-voltage-ref failed, rc = %d\n", rc);
		kfree(fuse_sel);
		kfree(ref_uv);
		return rc;
	}

	tmp = fuse_sel;
	for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners; i++) {
		efuse_bits = cpr_read_efuse_param(cpr_vreg, fuse_sel[0],
					fuse_sel[1], fuse_sel[2], fuse_sel[3]);
		sign = (efuse_bits & (1 << (fuse_sel[2] - 1))) ? -1 : 1;
		steps = efuse_bits & ((1 << (fuse_sel[2] - 1)) - 1);
		cpr_vreg->pvs_corner_v[i] =
				ref_uv[i] + sign * steps * step_size_uv;
		cpr_vreg->pvs_corner_v[i] = DIV_ROUND_UP(
				cpr_vreg->pvs_corner_v[i],
				cpr_vreg->step_volt) *
				cpr_vreg->step_volt;
		cpr_debug(cpr_vreg, "corner %d: sign = %d, steps = %d, volt = %d uV\n",
			i, sign, steps, cpr_vreg->pvs_corner_v[i]);
		fuse_sel += 4;
	}

	rc = cpr_adjust_init_voltages(of_node, cpr_vreg);
	if (rc)
		goto done;

	for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners; i++) {
		if (cpr_vreg->pvs_corner_v[i]
		    > cpr_vreg->fuse_ceiling_volt[i]) {
			cpr_info(cpr_vreg, "Warning: initial voltage[%d] %d above ceiling %d\n",
				i, cpr_vreg->pvs_corner_v[i],
				cpr_vreg->fuse_ceiling_volt[i]);
			cpr_vreg->pvs_corner_v[i]
				= cpr_vreg->fuse_ceiling_volt[i];
		} else if (cpr_vreg->pvs_corner_v[i] <
				cpr_vreg->fuse_floor_volt[i]) {
			cpr_info(cpr_vreg, "Warning: initial voltage[%d] %d below floor %d\n",
				i, cpr_vreg->pvs_corner_v[i],
				cpr_vreg->fuse_floor_volt[i]);
			cpr_vreg->pvs_corner_v[i]
				= cpr_vreg->fuse_floor_volt[i];
		}
	}

done:
	kfree(tmp);
	kfree(ref_uv);

	return rc;
}

/*
 * A single PVS bin is stored in a fuse that's position is defined either
 * in the qcom,pvs-fuse-redun property or in the qcom,pvs-fuse property.
 * The fuse value defined in the qcom,pvs-fuse-redun-sel property is used
 * to pick between the primary or redudant PVS fuse position.
 * After the PVS bin value is read out successfully, it is used as the row
 * index to get initial voltages for each fuse corner from the voltage table
 * defined in the qcom,pvs-voltage-table property.
 */
static int cpr_pvs_single_bin_init(struct device_node *of_node,
				struct cpr_regulator *cpr_vreg)
{
	u64 efuse_bits;
	u32 pvs_fuse[4], pvs_fuse_redun_sel[5];
	int rc, i, stripe_size;
	bool redundant;
	size_t pvs_bins;
	u32 *tmp;

	rc = of_property_read_u32_array(of_node, "qcom,pvs-fuse-redun-sel",
						pvs_fuse_redun_sel, 5);
	if (rc < 0) {
		cpr_err(cpr_vreg, "pvs-fuse-redun-sel missing: rc=%d\n", rc);
		return rc;
	}

	redundant = cpr_fuse_is_setting_expected(cpr_vreg, pvs_fuse_redun_sel);
	if (redundant) {
		rc = of_property_read_u32_array(of_node, "qcom,pvs-fuse-redun",
								pvs_fuse, 4);
		if (rc < 0) {
			cpr_err(cpr_vreg, "pvs-fuse-redun missing: rc=%d\n",
				rc);
			return rc;
		}
	} else {
		rc = of_property_read_u32_array(of_node, "qcom,pvs-fuse",
							pvs_fuse, 4);
		if (rc < 0) {
			cpr_err(cpr_vreg, "pvs-fuse missing: rc=%d\n", rc);
			return rc;
		}
	}

	/* Construct PVS process # from the efuse bits */
	efuse_bits = cpr_read_efuse_row(cpr_vreg, pvs_fuse[0], pvs_fuse[3]);
	cpr_vreg->pvs_bin = (efuse_bits >> pvs_fuse[1]) &
				((1 << pvs_fuse[2]) - 1);
	pvs_bins = 1 << pvs_fuse[2];
	stripe_size = cpr_vreg->num_fuse_corners;
	tmp = kzalloc(sizeof(u32) * pvs_bins * stripe_size, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	rc = of_property_read_u32_array(of_node, "qcom,pvs-voltage-table",
						tmp, pvs_bins * stripe_size);
	if (rc < 0) {
		cpr_err(cpr_vreg, "pvs-voltage-table missing: rc=%d\n", rc);
		kfree(tmp);
		return rc;
	}

	for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners; i++)
		cpr_vreg->pvs_corner_v[i] = tmp[cpr_vreg->pvs_bin *
						stripe_size + i - 1];
	kfree(tmp);

	rc = cpr_adjust_init_voltages(of_node, cpr_vreg);
	if (rc)
		return rc;

	return 0;
}

/*
 * The function reads VDD_MX dependency parameters from device node.
 * Select the qcom,vdd-mx-corner-map length equal to either num_fuse_corners
 * or num_corners based on selected vdd-mx-vmin-method.
 */
static int cpr_parse_vdd_mx_parameters(struct platform_device *pdev,
					struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	u32 corner_map_len;
	int rc, len, size;

	rc = of_property_read_u32(of_node, "qcom,vdd-mx-vmax",
				&cpr_vreg->vdd_mx_vmax);
	if (rc < 0) {
		cpr_err(cpr_vreg, "vdd-mx-vmax missing: rc=%d\n", rc);
		return rc;
	}

	rc = of_property_read_u32(of_node, "qcom,vdd-mx-vmin-method",
			 &cpr_vreg->vdd_mx_vmin_method);
	if (rc < 0) {
		cpr_err(cpr_vreg, "vdd-mx-vmin-method missing: rc=%d\n",
			rc);
		return rc;
	}
	if (cpr_vreg->vdd_mx_vmin_method > VDD_MX_VMIN_APC_CORNER_MAP) {
		cpr_err(cpr_vreg, "Invalid vdd-mx-vmin-method(%d)\n",
			cpr_vreg->vdd_mx_vmin_method);
		return -EINVAL;
	}

	switch (cpr_vreg->vdd_mx_vmin_method) {
	case VDD_MX_VMIN_APC_FUSE_CORNER_MAP:
		corner_map_len = cpr_vreg->num_fuse_corners;
		break;
	case VDD_MX_VMIN_APC_CORNER_MAP:
		corner_map_len = cpr_vreg->num_corners;
		break;
	default:
		cpr_vreg->vdd_mx_corner_map = NULL;
		return 0;
	}

	if (!of_find_property(of_node, "qcom,vdd-mx-corner-map", &len)) {
		cpr_err(cpr_vreg, "qcom,vdd-mx-corner-map missing");
		return -EINVAL;
	}

	size = len / sizeof(u32);
	if (size != corner_map_len) {
		cpr_err(cpr_vreg,
			"qcom,vdd-mx-corner-map length=%d is invalid: required:%u\n",
			size, corner_map_len);
		return -EINVAL;
	}

	cpr_vreg->vdd_mx_corner_map = devm_kzalloc(&pdev->dev,
		(corner_map_len + 1) * sizeof(*cpr_vreg->vdd_mx_corner_map),
			GFP_KERNEL);
	if (!cpr_vreg->vdd_mx_corner_map)
		return -ENOMEM;

	rc = of_property_read_u32_array(of_node,
				"qcom,vdd-mx-corner-map",
				&cpr_vreg->vdd_mx_corner_map[1],
				corner_map_len);
	if (rc)
		cpr_err(cpr_vreg,
			"read qcom,vdd-mx-corner-map failed, rc = %d\n", rc);

	return rc;
}

#define MAX_CHARS_PER_INT	10

/*
 * The initial voltage for each fuse corner may be determined by one of two
 * possible styles of fuse. If qcom,cpr-fuse-init-voltage is present, then
 * the initial voltages are encoded in a fuse for each fuse corner. If it is
 * not present, then the initial voltages are all determined using a single
 * PVS bin fuse value.
 */
static int cpr_pvs_init(struct platform_device *pdev,
			       struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	int highest_fuse_corner = cpr_vreg->num_fuse_corners;
	int i, rc, pos;
	size_t buflen;
	char *buf;

	rc = of_property_read_u32(of_node, "qcom,cpr-apc-volt-step",
					&cpr_vreg->step_volt);
	if (rc < 0) {
		cpr_err(cpr_vreg, "read cpr-apc-volt-step failed, rc = %d\n",
			rc);
		return rc;
	} else if (cpr_vreg->step_volt == 0) {
		cpr_err(cpr_vreg, "apc voltage step size can't be set to 0.\n");
		return -EINVAL;
	}

	if (of_find_property(of_node, "qcom,cpr-fuse-init-voltage", NULL)) {
		rc = cpr_pvs_per_corner_init(of_node, cpr_vreg);
		if (rc < 0) {
			cpr_err(cpr_vreg, "get pvs per corner failed, rc = %d",
				rc);
			return rc;
		}
	} else {
		rc = cpr_pvs_single_bin_init(of_node, cpr_vreg);
		if (rc < 0) {
			cpr_err(cpr_vreg,
				"get pvs from single bin failed, rc = %d", rc);
			return rc;
		}
	}

	if (cpr_vreg->flags & FLAGS_UPLIFT_QUOT_VOLT) {
		rc = cpr_voltage_uplift_wa_inc_volt(cpr_vreg, of_node);
		if (rc < 0) {
			cpr_err(cpr_vreg, "pvs volt uplift wa apply failed: %d",
				rc);
			return rc;
		}
	}

	/*
	 * Allow the highest fuse corner's PVS voltage to define the ceiling
	 * voltage for that corner in order to support SoC's in which variable
	 * ceiling values are required.
	 */
	if (cpr_vreg->pvs_corner_v[highest_fuse_corner] >
		cpr_vreg->fuse_ceiling_volt[highest_fuse_corner])
		cpr_vreg->fuse_ceiling_volt[highest_fuse_corner] =
			cpr_vreg->pvs_corner_v[highest_fuse_corner];

	/*
	 * Restrict all fuse corner PVS voltages based upon per corner
	 * ceiling and floor voltages.
	 */
	for (i = CPR_FUSE_CORNER_MIN; i <= highest_fuse_corner; i++)
		if (cpr_vreg->pvs_corner_v[i] > cpr_vreg->fuse_ceiling_volt[i])
			cpr_vreg->pvs_corner_v[i]
				= cpr_vreg->fuse_ceiling_volt[i];
		else if (cpr_vreg->pvs_corner_v[i]
				< cpr_vreg->fuse_floor_volt[i])
			cpr_vreg->pvs_corner_v[i]
				= cpr_vreg->fuse_floor_volt[i];

	cpr_vreg->ceiling_max
		= cpr_vreg->fuse_ceiling_volt[highest_fuse_corner];

	/*
	 * Log ceiling, floor, and initial voltages since they are critical for
	 * all CPR debugging.
	 */
	buflen = cpr_vreg->num_fuse_corners * (MAX_CHARS_PER_INT + 2)
			* sizeof(*buf);
	buf = kzalloc(buflen, GFP_KERNEL);
	if (buf == NULL) {
		cpr_err(cpr_vreg, "Could not allocate memory for corner voltage logging\n");
		return 0;
	}

	for (i = CPR_FUSE_CORNER_MIN, pos = 0; i <= highest_fuse_corner; i++)
		pos += scnprintf(buf + pos, buflen - pos, "%u%s",
				cpr_vreg->pvs_corner_v[i],
				i < highest_fuse_corner ? " " : "");
	cpr_info(cpr_vreg, "pvs voltage: [%s] uV\n", buf);

	for (i = CPR_FUSE_CORNER_MIN, pos = 0; i <= highest_fuse_corner; i++)
		pos += scnprintf(buf + pos, buflen - pos, "%d%s",
				cpr_vreg->fuse_ceiling_volt[i],
				i < highest_fuse_corner ? " " : "");
	cpr_info(cpr_vreg, "ceiling voltage: [%s] uV\n", buf);

	for (i = CPR_FUSE_CORNER_MIN, pos = 0; i <= highest_fuse_corner; i++)
		pos += scnprintf(buf + pos, buflen - pos, "%d%s",
				cpr_vreg->fuse_floor_volt[i],
				i < highest_fuse_corner ? " " : "");
	cpr_info(cpr_vreg, "floor voltage: [%s] uV\n", buf);

	kfree(buf);
	return 0;
}

#define CPR_PROP_READ_U32(cpr_vreg, of_node, cpr_property, cpr_config, rc) \
do {									\
	if (!rc) {							\
		rc = of_property_read_u32(of_node,			\
				"qcom," cpr_property,			\
				cpr_config);				\
		if (rc) {						\
			cpr_err(cpr_vreg, "Missing " #cpr_property	\
				": rc = %d\n", rc);			\
		}							\
	}								\
} while (0)

static int cpr_apc_init(struct platform_device *pdev,
			       struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	int i, rc = 0;

	for (i = 0; i < ARRAY_SIZE(vdd_apc_name); i++) {
		cpr_vreg->vdd_apc = devm_regulator_get_optional(&pdev->dev,
					vdd_apc_name[i]);
		rc = PTR_ERR_OR_ZERO(cpr_vreg->vdd_apc);
		if (!IS_ERR_OR_NULL(cpr_vreg->vdd_apc))
			break;
	}

	if (rc) {
		if (rc != -EPROBE_DEFER)
			cpr_err(cpr_vreg, "devm_regulator_get: rc=%d\n", rc);
		return rc;
	}

	/* Check dependencies */
	if (of_find_property(of_node, "vdd-mx-supply", NULL)) {
		cpr_vreg->vdd_mx = devm_regulator_get(&pdev->dev, "vdd-mx");
		if (IS_ERR_OR_NULL(cpr_vreg->vdd_mx)) {
			rc = PTR_ERR_OR_ZERO(cpr_vreg->vdd_mx);
			if (rc != -EPROBE_DEFER)
				cpr_err(cpr_vreg,
					"devm_regulator_get: vdd-mx: rc=%d\n",
					rc);
			return rc;
		}
	}

	return 0;
}

static void cpr_apc_exit(struct cpr_regulator *cpr_vreg)
{
	if (cpr_vreg->vreg_enabled) {
		regulator_disable(cpr_vreg->vdd_apc);

		if (cpr_vreg->vdd_mx)
			regulator_disable(cpr_vreg->vdd_mx);
	}
}

static int cpr_voltage_uplift_wa_inc_quot(struct cpr_regulator *cpr_vreg,
					struct device_node *of_node)
{
	u32 delta_quot[3];
	int rc, i;

	rc = of_property_read_u32_array(of_node,
			"qcom,cpr-uplift-quotient", delta_quot, 3);
	if (rc < 0) {
		cpr_err(cpr_vreg, "cpr-uplift-quotient is missing: %d", rc);
		return rc;
	}
	for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners; i++)
		cpr_vreg->cpr_fuse_target_quot[i] += delta_quot[i-1];
	return rc;
}

static void cpr_parse_pvs_version_fuse(struct cpr_regulator *cpr_vreg,
				struct device_node *of_node)
{
	int rc;
	u64 fuse_bits;
	u32 fuse_sel[4];

	rc = of_property_read_u32_array(of_node,
			"qcom,pvs-version-fuse-sel", fuse_sel, 4);
	if (!rc) {
		fuse_bits = cpr_read_efuse_row(cpr_vreg,
				fuse_sel[0], fuse_sel[3]);
		cpr_vreg->pvs_version = (fuse_bits >> fuse_sel[1]) &
			((1 << fuse_sel[2]) - 1);
		cpr_info(cpr_vreg, "[row: %d]: 0x%llx, pvs_version = %d\n",
				fuse_sel[0], fuse_bits, cpr_vreg->pvs_version);
	} else {
		cpr_vreg->pvs_version = 0;
	}
}

/**
 * cpr_get_open_loop_voltage() - fill the open_loop_volt array with linearly
 *				 interpolated open-loop CPR voltage values.
 * @cpr_vreg:	Handle to the cpr-regulator device
 * @dev:	Device pointer for the cpr-regulator device
 * @corner_max:	Array of length (cpr_vreg->num_fuse_corners + 1) which maps from
 *		fuse corners to the highest virtual corner corresponding to a
 *		given fuse corner
 * @freq_map:	Array of length (cpr_vreg->num_corners + 1) which maps from
 *		virtual corners to frequencies in Hz.
 * @maps_valid:	Boolean which indicates if the values in corner_max and freq_map
 *		are valid.  If they are not valid, then the open_loop_volt
 *		values are not interpolated.
 */
static int cpr_get_open_loop_voltage(struct cpr_regulator *cpr_vreg,
		struct device *dev, const u32 *corner_max, const u32 *freq_map,
		bool maps_valid)
{
	int rc = 0;
	int i, j;
	u64 volt_high, volt_low, freq_high, freq_low, freq, temp, temp_limit;
	u32 *max_factor = NULL;

	cpr_vreg->open_loop_volt = devm_kzalloc(dev,
			sizeof(int) * (cpr_vreg->num_corners + 1), GFP_KERNEL);
	if (!cpr_vreg->open_loop_volt)
		return -ENOMEM;

	/*
	 * Set open loop voltage to be equal to per-fuse-corner initial voltage
	 * by default.  This ensures that the open loop voltage is valid for
	 * all virtual corners even if some virtual corner to frequency mappings
	 * are missing.  It also ensures that the voltage is valid for the
	 * higher corners not utilized by a given speed-bin.
	 */
	for (i = CPR_CORNER_MIN; i <= cpr_vreg->num_corners; i++)
		cpr_vreg->open_loop_volt[i]
			= cpr_vreg->pvs_corner_v[cpr_vreg->corner_map[i]];

	if (!maps_valid || !corner_max || !freq_map
	    || !of_find_property(dev->of_node,
				 "qcom,cpr-voltage-scaling-factor-max", NULL)) {
		/* Not using interpolation */
		return 0;
	}

	max_factor
	       = kzalloc(sizeof(*max_factor) * (cpr_vreg->num_fuse_corners + 1),
			 GFP_KERNEL);
	if (!max_factor)
		return -ENOMEM;

	rc = of_property_read_u32_array(dev->of_node,
			"qcom,cpr-voltage-scaling-factor-max",
			&max_factor[CPR_FUSE_CORNER_MIN],
			cpr_vreg->num_fuse_corners);
	if (rc) {
		cpr_debug(cpr_vreg, "failed to read qcom,cpr-voltage-scaling-factor-max; initial voltage interpolation not possible\n");
		kfree(max_factor);
		return 0;
	}

	for (j = CPR_FUSE_CORNER_MIN + 1; j <= cpr_vreg->num_fuse_corners;
	    j++) {
		freq_high = freq_map[corner_max[j]];
		freq_low = freq_map[corner_max[j - 1]];
		volt_high = cpr_vreg->pvs_corner_v[j];
		volt_low = cpr_vreg->pvs_corner_v[j - 1];
		if (freq_high <= freq_low || volt_high <= volt_low)
			continue;

		for (i = corner_max[j - 1] + 1; i < corner_max[j]; i++) {
			freq = freq_map[i];
			if (freq_high <= freq)
				continue;

			temp = (freq_high - freq) * (volt_high - volt_low);
			do_div(temp, (u32)(freq_high - freq_low));

			/*
			 * max_factor[j] has units of uV/MHz while freq values
			 * have units of Hz.  Divide by 1000000 to convert.
			 */
			temp_limit = (freq_high - freq) * max_factor[j];
			do_div(temp_limit, 1000000);

			cpr_vreg->open_loop_volt[i]
				= volt_high - min(temp, temp_limit);
			cpr_vreg->open_loop_volt[i]
				= DIV_ROUND_UP(cpr_vreg->open_loop_volt[i],
						cpr_vreg->step_volt)
					* cpr_vreg->step_volt;
		}
	}

	kfree(max_factor);
	return 0;
}

/*
 * Limit the per-virtual-corner open-loop voltages using the per-virtual-corner
 * ceiling and floor voltage values.  This must be called only after the
 * open_loop_volt, ceiling, and floor arrays have all been initialized.
 */
static int cpr_limit_open_loop_voltage(struct cpr_regulator *cpr_vreg)
{
	int i;

	for (i = CPR_CORNER_MIN; i <= cpr_vreg->num_corners; i++) {
		if (cpr_vreg->open_loop_volt[i] > cpr_vreg->ceiling_volt[i])
			cpr_vreg->open_loop_volt[i] = cpr_vreg->ceiling_volt[i];
		else if (cpr_vreg->open_loop_volt[i] < cpr_vreg->floor_volt[i])
			cpr_vreg->open_loop_volt[i] = cpr_vreg->floor_volt[i];
	}

	return 0;
}

/*
 * Fill an OPP table for the cpr-regulator device struct with pairs of
 * <virtual voltage corner number, open loop voltage> tuples.
 */
static int cpr_populate_opp_table(struct cpr_regulator *cpr_vreg,
				struct device *dev)
{
	int i, rc = 0;

	for (i = CPR_CORNER_MIN; i <= cpr_vreg->num_corners; i++) {
		rc |= dev_pm_opp_add(dev, i, cpr_vreg->open_loop_volt[i]);
		if (rc)
			cpr_debug(cpr_vreg, "could not add OPP entry <%d, %d>, rc=%d\n",
				i, cpr_vreg->open_loop_volt[i], rc);
	}
	if (rc)
		cpr_err(cpr_vreg, "adding OPP entry failed - OPP may not be enabled, rc=%d\n",
				rc);

	return 0;
}

/*
 * Conditionally reduce the per-virtual-corner ceiling voltages if certain
 * device tree flags are present.  This must be called only after the ceiling
 * array has been initialized and the open_loop_volt array values have been
 * initialized and limited to the existing floor to ceiling voltage range.
 */
static int cpr_reduce_ceiling_voltage(struct cpr_regulator *cpr_vreg,
				struct device *dev)
{
	bool reduce_to_fuse_open_loop, reduce_to_interpolated_open_loop;
	int i;

	reduce_to_fuse_open_loop = of_property_read_bool(dev->of_node,
				"qcom,cpr-init-voltage-as-ceiling");
	reduce_to_interpolated_open_loop = of_property_read_bool(dev->of_node,
				"qcom,cpr-scaled-init-voltage-as-ceiling");

	if (!reduce_to_fuse_open_loop && !reduce_to_interpolated_open_loop)
		return 0;

	for (i = CPR_CORNER_MIN; i <= cpr_vreg->num_corners; i++) {
		if (reduce_to_interpolated_open_loop &&
		    cpr_vreg->open_loop_volt[i] < cpr_vreg->ceiling_volt[i])
			cpr_vreg->ceiling_volt[i] = cpr_vreg->open_loop_volt[i];
		else if (reduce_to_fuse_open_loop &&
				cpr_vreg->pvs_corner_v[cpr_vreg->corner_map[i]]
				< cpr_vreg->ceiling_volt[i])
			cpr_vreg->ceiling_volt[i]
				= max((u32)cpr_vreg->floor_volt[i],
			       cpr_vreg->pvs_corner_v[cpr_vreg->corner_map[i]]);
		cpr_debug(cpr_vreg, "lowered ceiling[%d] = %d uV\n",
			i, cpr_vreg->ceiling_volt[i]);
	}

	return 0;
}

static int cpr_adjust_target_quot_offsets(struct platform_device *pdev,
					struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	int tuple_count, tuple_match, i;
	u32 index;
	u32 quot_offset_adjust = 0;
	int len = 0;
	int rc = 0;
	char *quot_offset_str;

	quot_offset_str = "qcom,cpr-quot-offset-adjustment";
	if (!of_find_property(of_node, quot_offset_str, &len)) {
		/* No static quotient adjustment needed. */
		return 0;
	}

	if (cpr_vreg->cpr_fuse_map_count) {
		if (cpr_vreg->cpr_fuse_map_match == FUSE_MAP_NO_MATCH) {
			/* No matching index to use for quotient adjustment. */
			return 0;
		}
		tuple_count = cpr_vreg->cpr_fuse_map_count;
		tuple_match = cpr_vreg->cpr_fuse_map_match;
	} else {
		tuple_count = 1;
		tuple_match = 0;
	}

	if (len != cpr_vreg->num_fuse_corners * tuple_count * sizeof(u32)) {
		cpr_err(cpr_vreg, "%s length=%d is invalid\n", quot_offset_str,
			len);
		return -EINVAL;
	}

	for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners; i++) {
		index = tuple_match * cpr_vreg->num_fuse_corners
				+ i - CPR_FUSE_CORNER_MIN;
		rc = of_property_read_u32_index(of_node, quot_offset_str, index,
			&quot_offset_adjust);
		if (rc) {
			cpr_err(cpr_vreg, "could not read %s index %u, rc=%d\n",
				quot_offset_str, index, rc);
			return rc;
		}

		if (quot_offset_adjust) {
			cpr_vreg->fuse_quot_offset[i] += quot_offset_adjust;
			cpr_info(cpr_vreg, "Corner[%d]: adjusted target quot = %d\n",
				i, cpr_vreg->fuse_quot_offset[i]);
		}
	}

	return rc;
}

static int cpr_get_fuse_quot_offset(struct cpr_regulator *cpr_vreg,
					struct platform_device *pdev,
					struct cpr_quot_scale *quot_scale)
{
	struct device *dev = &pdev->dev;
	struct property *prop;
	u32 *fuse_sel, *tmp, *offset_multiplier = NULL;
	int rc = 0, i, size, len;
	char *quot_offset_str;

	quot_offset_str = cpr_vreg->cpr_fuse_redundant
			? "qcom,cpr-fuse-redun-quot-offset"
			: "qcom,cpr-fuse-quot-offset";

	prop = of_find_property(dev->of_node, quot_offset_str, NULL);
	if (!prop) {
		cpr_debug(cpr_vreg, "%s not present\n", quot_offset_str);
		return 0;
	}

	size = prop->length / sizeof(u32);
	if (size != cpr_vreg->num_fuse_corners * 4) {
		cpr_err(cpr_vreg, "fuse position for quot offset is invalid\n");
		return -EINVAL;
	}

	fuse_sel = kzalloc(sizeof(u32) * size, GFP_KERNEL);
	if (!fuse_sel)
		return -ENOMEM;

	rc = of_property_read_u32_array(dev->of_node, quot_offset_str,
			fuse_sel, size);

	if (rc < 0) {
		cpr_err(cpr_vreg, "read %s failed, rc = %d\n", quot_offset_str,
			rc);
		kfree(fuse_sel);
		return rc;
	}

	cpr_vreg->fuse_quot_offset = devm_kzalloc(dev,
			sizeof(u32) * (cpr_vreg->num_fuse_corners + 1),
			GFP_KERNEL);
	if (!cpr_vreg->fuse_quot_offset) {
		kfree(fuse_sel);
		return -ENOMEM;
	}

	if (!of_find_property(dev->of_node,
				"qcom,cpr-fuse-quot-offset-scale", &len)) {
		cpr_debug(cpr_vreg, "qcom,cpr-fuse-quot-offset-scale not present\n");
	} else {
		if (len != cpr_vreg->num_fuse_corners * sizeof(u32)) {
			cpr_err(cpr_vreg, "the size of qcom,cpr-fuse-quot-offset-scale is invalid\n");
			kfree(fuse_sel);
			return -EINVAL;
		}

		offset_multiplier = kzalloc(sizeof(*offset_multiplier)
					* (cpr_vreg->num_fuse_corners + 1),
					GFP_KERNEL);
		if (!offset_multiplier) {
			kfree(fuse_sel);
			return -ENOMEM;
		}

		rc = of_property_read_u32_array(dev->of_node,
						"qcom,cpr-fuse-quot-offset-scale",
						&offset_multiplier[1],
						cpr_vreg->num_fuse_corners);
		if (rc < 0) {
			cpr_err(cpr_vreg, "read qcom,cpr-fuse-quot-offset-scale failed, rc = %d\n",
				rc);
			kfree(fuse_sel);
			goto out;
		}
	}

	tmp = fuse_sel;
	for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners; i++) {
		cpr_vreg->fuse_quot_offset[i] = cpr_read_efuse_param(cpr_vreg,
					fuse_sel[0], fuse_sel[1], fuse_sel[2],
					fuse_sel[3]);
		if (offset_multiplier)
			cpr_vreg->fuse_quot_offset[i] *= offset_multiplier[i];
		fuse_sel += 4;
	}

	rc = cpr_adjust_target_quot_offsets(pdev, cpr_vreg);
	kfree(tmp);
out:
	kfree(offset_multiplier);
	return rc;
}

/*
 * Adjust the per-virtual-corner open loop voltage with an offset specfied by a
 * device-tree property. This must be called after open-loop voltage scaling.
 */
static int cpr_virtual_corner_voltage_adjust(struct cpr_regulator *cpr_vreg,
						struct device *dev)
{
	char *prop_name = "qcom,cpr-virtual-corner-init-voltage-adjustment";
	int i, rc, tuple_count, tuple_match, index, len;
	u32 voltage_adjust;

	if (!of_find_property(dev->of_node, prop_name, &len)) {
		cpr_debug(cpr_vreg, "%s not specified\n", prop_name);
		return 0;
	}

	if (cpr_vreg->cpr_fuse_map_count) {
		if (cpr_vreg->cpr_fuse_map_match == FUSE_MAP_NO_MATCH) {
			/* No matching index to use for voltage adjustment. */
			return 0;
		}
		tuple_count = cpr_vreg->cpr_fuse_map_count;
		tuple_match = cpr_vreg->cpr_fuse_map_match;
	} else {
		tuple_count = 1;
		tuple_match = 0;
	}

	if (len != cpr_vreg->num_corners * tuple_count * sizeof(u32)) {
		cpr_err(cpr_vreg, "%s length=%d is invalid\n", prop_name,
			len);
		return -EINVAL;
	}

	for (i = CPR_CORNER_MIN; i <= cpr_vreg->num_corners; i++) {
		index = tuple_match * cpr_vreg->num_corners
				+ i - CPR_CORNER_MIN;
		rc = of_property_read_u32_index(dev->of_node, prop_name,
						index, &voltage_adjust);
		if (rc) {
			cpr_err(cpr_vreg, "could not read %s index %u, rc=%d\n",
				prop_name, index, rc);
			return rc;
		}

		if (voltage_adjust) {
			cpr_vreg->open_loop_volt[i] += (int)voltage_adjust;
			cpr_info(cpr_vreg, "corner=%d adjusted open-loop voltage=%d\n",
				i, cpr_vreg->open_loop_volt[i]);
		}
	}

	return 0;
}

/*
 * Adjust the per-virtual-corner quot with an offset specfied by a
 * device-tree property. This must be called after the quot-scaling adjustments
 * are completed.
 */
static int cpr_virtual_corner_quot_adjust(struct cpr_regulator *cpr_vreg,
						struct device *dev)
{
	char *prop_name = "qcom,cpr-virtual-corner-quotient-adjustment";
	int i, rc, tuple_count, tuple_match, index, len;
	u32 quot_adjust;

	if (!of_find_property(dev->of_node, prop_name, &len)) {
		cpr_debug(cpr_vreg, "%s not specified\n", prop_name);
		return 0;
	}

	if (cpr_vreg->cpr_fuse_map_count) {
		if (cpr_vreg->cpr_fuse_map_match == FUSE_MAP_NO_MATCH) {
			/* No matching index to use for quotient adjustment. */
			return 0;
		}
		tuple_count = cpr_vreg->cpr_fuse_map_count;
		tuple_match = cpr_vreg->cpr_fuse_map_match;
	} else {
		tuple_count = 1;
		tuple_match = 0;
	}

	if (len != cpr_vreg->num_corners * tuple_count * sizeof(u32)) {
		cpr_err(cpr_vreg, "%s length=%d is invalid\n", prop_name,
			len);
		return -EINVAL;
	}

	for (i = CPR_CORNER_MIN; i <= cpr_vreg->num_corners; i++) {
		index = tuple_match * cpr_vreg->num_corners
				+ i - CPR_CORNER_MIN;
		rc = of_property_read_u32_index(dev->of_node, prop_name,
						index, &quot_adjust);
		if (rc) {
			cpr_err(cpr_vreg, "could not read %s index %u, rc=%d\n",
				prop_name, index, rc);
			return rc;
		}

		if (quot_adjust) {
			cpr_vreg->quot_adjust[i] -= (int)quot_adjust;
			cpr_info(cpr_vreg, "corner=%d adjusted quotient=%d\n",
					i,
			cpr_vreg->cpr_fuse_target_quot[cpr_vreg->corner_map[i]]
						- cpr_vreg->quot_adjust[i]);
		}
	}

	return 0;
}

/*
 * cpr_get_corner_quot_adjustment() -- get the quot_adjust for each corner.
 *
 * Get the virtual corner to fuse corner mapping and virtual corner to APC clock
 * frequency mapping from device tree.
 * Calculate the quotient adjustment scaling factor for those corners mapping to
 * all fuse corners except for the lowest one using linear interpolation.
 * Calculate the quotient adjustment for each of these virtual corners using the
 * min of the calculated scaling factor and the constant max scaling factor
 * defined for each fuse corner in device tree.
 */
static int cpr_get_corner_quot_adjustment(struct cpr_regulator *cpr_vreg,
					struct device *dev)
{
	int rc = 0;
	int highest_fuse_corner = cpr_vreg->num_fuse_corners;
	int i, j, size;
	struct property *prop;
	bool corners_mapped, match_found;
	u32 *tmp, *freq_map = NULL;
	u32 corner, freq_corner;
	u32 *freq_max = NULL;
	u32 *scaling = NULL;
	u32 *max_factor = NULL;
	u32 *corner_max = NULL;
	bool maps_valid = false;

	prop = of_find_property(dev->of_node, "qcom,cpr-corner-map", NULL);

	if (prop) {
		size = prop->length / sizeof(u32);
		corners_mapped = true;
	} else {
		size = cpr_vreg->num_fuse_corners;
		corners_mapped = false;
	}

	cpr_vreg->corner_map = devm_kzalloc(dev, sizeof(int) * (size + 1),
					GFP_KERNEL);
	if (!cpr_vreg->corner_map)
		return -ENOMEM;
	cpr_vreg->num_corners = size;

	cpr_vreg->quot_adjust = devm_kzalloc(dev,
			sizeof(u32) * (cpr_vreg->num_corners + 1),
			GFP_KERNEL);
	if (!cpr_vreg->quot_adjust)
		return -ENOMEM;

	if (!corners_mapped) {
		for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners;
		     i++)
			cpr_vreg->corner_map[i] = i;
		goto free_arrays;
	} else {
		rc = of_property_read_u32_array(dev->of_node,
			"qcom,cpr-corner-map", &cpr_vreg->corner_map[1], size);

		if (rc) {
			cpr_err(cpr_vreg,
				"qcom,cpr-corner-map missing, rc = %d\n", rc);
			return rc;
		}

		/*
		 * Verify that the virtual corner to fuse corner mapping is
		 * valid.
		 */
		for (i = CPR_CORNER_MIN; i <= cpr_vreg->num_corners; i++) {
			if (cpr_vreg->corner_map[i] > cpr_vreg->num_fuse_corners
			    || cpr_vreg->corner_map[i] < CPR_FUSE_CORNER_MIN) {
				cpr_err(cpr_vreg, "qcom,cpr-corner-map contains an element %d which isn't in the allowed range [%d, %d]\n",
					cpr_vreg->corner_map[i],
					CPR_FUSE_CORNER_MIN,
					cpr_vreg->num_fuse_corners);
				return -EINVAL;
			}
		}
	}

	prop = of_find_property(dev->of_node,
			"qcom,cpr-speed-bin-max-corners", NULL);
	if (!prop) {
		cpr_debug(cpr_vreg, "qcom,cpr-speed-bin-max-corner missing\n");
		goto free_arrays;
	}

	size = prop->length / sizeof(u32);
	tmp = kcalloc(size, sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	rc = of_property_read_u32_array(dev->of_node,
		"qcom,cpr-speed-bin-max-corners", tmp, size);
	if (rc < 0) {
		kfree(tmp);
		cpr_err(cpr_vreg,
			"get cpr-speed-bin-max-corners failed, rc = %d\n", rc);
		return rc;
	}

	corner_max = kcalloc((cpr_vreg->num_fuse_corners + 1),
				sizeof(*corner_max), GFP_KERNEL);
	freq_max = kcalloc((cpr_vreg->num_fuse_corners + 1), sizeof(*freq_max),
				GFP_KERNEL);
	if (corner_max == NULL || freq_max == NULL) {
		kfree(tmp);
		rc = -ENOMEM;
		goto free_arrays;
	}

	/*
	 * Get the maximum virtual corner for each fuse corner based upon the
	 * speed_bin and pvs_version values.
	 */
	match_found = false;
	for (i = 0; i < size; i += cpr_vreg->num_fuse_corners + 2) {
		if (tmp[i] != cpr_vreg->speed_bin &&
		    tmp[i] != FUSE_PARAM_MATCH_ANY)
			continue;
		if (tmp[i + 1] != cpr_vreg->pvs_version &&
		    tmp[i + 1] != FUSE_PARAM_MATCH_ANY)
			continue;
		for (j = CPR_FUSE_CORNER_MIN;
		     j <= cpr_vreg->num_fuse_corners; j++)
			corner_max[j] = tmp[i + 2 + j - CPR_FUSE_CORNER_MIN];
		match_found = true;
		break;
	}
	kfree(tmp);

	if (!match_found) {
		cpr_debug(cpr_vreg, "No quotient adjustment possible for speed bin=%u, pvs version=%u\n",
			cpr_vreg->speed_bin, cpr_vreg->pvs_version);
		goto free_arrays;
	}

	/* Verify that fuse corner to max virtual corner mapping is valid. */
	for (i = CPR_FUSE_CORNER_MIN; i <= highest_fuse_corner; i++) {
		if (corner_max[i] < CPR_CORNER_MIN
		    || corner_max[i] > cpr_vreg->num_corners) {
			cpr_err(cpr_vreg, "Invalid corner=%d in qcom,cpr-speed-bin-max-corners\n",
				corner_max[i]);
			goto free_arrays;
		}
	}

	/*
	 * Return success if the virtual corner values read from
	 * qcom,cpr-speed-bin-max-corners property are incorrect.  This allows
	 * the driver to continue to run without quotient scaling.
	 */
	for (i = CPR_FUSE_CORNER_MIN + 1; i <= highest_fuse_corner; i++) {
		if (corner_max[i] <= corner_max[i - 1]) {
			cpr_err(cpr_vreg, "fuse corner=%d (%u) should be larger than the fuse corner=%d (%u)\n",
				i, corner_max[i], i - 1, corner_max[i - 1]);
			goto free_arrays;
		}
	}

	prop = of_find_property(dev->of_node,
			"qcom,cpr-corner-frequency-map", NULL);
	if (!prop) {
		cpr_debug(cpr_vreg, "qcom,cpr-corner-frequency-map missing\n");
		goto free_arrays;
	}

	size = prop->length / sizeof(u32);
	tmp = kcalloc(size, sizeof(*tmp), GFP_KERNEL);
	if (!tmp) {
		rc = -ENOMEM;
		goto free_arrays;
	}
	rc = of_property_read_u32_array(dev->of_node,
		"qcom,cpr-corner-frequency-map", tmp, size);
	if (rc < 0) {
		cpr_err(cpr_vreg,
			"get cpr-corner-frequency-map failed, rc = %d\n", rc);
		kfree(tmp);
		goto free_arrays;
	}
	freq_map = kcalloc(cpr_vreg->num_corners + 1, sizeof(*freq_map),
			GFP_KERNEL);
	if (!freq_map) {
		kfree(tmp);
		rc = -ENOMEM;
		goto free_arrays;
	}
	for (i = 0; i < size; i += 2) {
		corner = tmp[i];
		if ((corner < 1) || (corner > cpr_vreg->num_corners)) {
			cpr_err(cpr_vreg,
				"corner should be in 1~%d range: %d\n",
				cpr_vreg->num_corners, corner);
			continue;
		}
		freq_map[corner] = tmp[i + 1];
		cpr_debug(cpr_vreg,
				"Frequency at virtual corner %d is %d Hz.\n",
				corner, freq_map[corner]);
	}
	kfree(tmp);

	prop = of_find_property(dev->of_node,
			"qcom,cpr-quot-adjust-scaling-factor-max", NULL);
	if (!prop) {
		cpr_debug(cpr_vreg, "qcom,cpr-quot-adjust-scaling-factor-max missing\n");
		rc = 0;
		goto free_arrays;
	}

	size = prop->length / sizeof(u32);
	if ((size != 1) && (size != cpr_vreg->num_fuse_corners)) {
		cpr_err(cpr_vreg, "The size of qcom,cpr-quot-adjust-scaling-factor-max should be 1 or %d\n",
			cpr_vreg->num_fuse_corners);
		rc = 0;
		goto free_arrays;
	}

	max_factor = kcalloc(cpr_vreg->num_fuse_corners + 1,
				sizeof(*max_factor), GFP_KERNEL);
	if (!max_factor) {
		rc = -ENOMEM;
		goto free_arrays;
	}
	/*
	 * Leave max_factor[CPR_FUSE_CORNER_MIN ... highest_fuse_corner-1] = 0
	 * if cpr-quot-adjust-scaling-factor-max is a single value in order to
	 * maintain backward compatibility.
	 */
	i = (size == cpr_vreg->num_fuse_corners) ? CPR_FUSE_CORNER_MIN
						 : highest_fuse_corner;
	rc = of_property_read_u32_array(dev->of_node,
			"qcom,cpr-quot-adjust-scaling-factor-max",
			&max_factor[i], size);
	if (rc < 0) {
		cpr_debug(cpr_vreg, "could not read qcom,cpr-quot-adjust-scaling-factor-max, rc=%d\n",
			rc);
		rc = 0;
		goto free_arrays;
	}

	/*
	 * Get the quotient adjustment scaling factor, according to:
	 * scaling = min(1000 * (QUOT(corner_N) - QUOT(corner_N-1))
	 *		/ (freq(corner_N) - freq(corner_N-1)), max_factor)
	 *
	 * QUOT(corner_N):	quotient read from fuse for fuse corner N
	 * QUOT(corner_N-1):	quotient read from fuse for fuse corner (N - 1)
	 * freq(corner_N):	max frequency in MHz supported by fuse corner N
	 * freq(corner_N-1):	max frequency in MHz supported by fuse corner
	 *			 (N - 1)
	 */

	for (i = CPR_FUSE_CORNER_MIN; i <= highest_fuse_corner; i++)
		freq_max[i] = freq_map[corner_max[i]];
	for (i = CPR_FUSE_CORNER_MIN + 1; i <= highest_fuse_corner; i++) {
		if (freq_max[i] <= freq_max[i - 1] || freq_max[i - 1] == 0) {
			cpr_err(cpr_vreg, "fuse corner %d freq=%u should be larger than fuse corner %d freq=%u\n",
			      i, freq_max[i], i - 1, freq_max[i - 1]);
			rc = -EINVAL;
			goto free_arrays;
		}
	}
	scaling = kcalloc(cpr_vreg->num_fuse_corners + 1, sizeof(*scaling),
			GFP_KERNEL);
	if (!scaling) {
		rc = -ENOMEM;
		goto free_arrays;
	}
	/* Convert corner max frequencies from Hz to MHz. */
	for (i = CPR_FUSE_CORNER_MIN; i <= highest_fuse_corner; i++)
		freq_max[i] /= 1000000;

	for (i = CPR_FUSE_CORNER_MIN + 1; i <= highest_fuse_corner; i++) {
		if (cpr_vreg->fuse_quot_offset &&
			(cpr_vreg->cpr_fuse_ro_sel[i] !=
				cpr_vreg->cpr_fuse_ro_sel[i - 1])) {
			scaling[i] = 1000 * cpr_vreg->fuse_quot_offset[i]
				/ (freq_max[i] - freq_max[i - 1]);
		} else {
			scaling[i] = 1000 * (cpr_vreg->cpr_fuse_target_quot[i]
				      - cpr_vreg->cpr_fuse_target_quot[i - 1])
				  / (freq_max[i] - freq_max[i - 1]);
			if (cpr_vreg->cpr_fuse_target_quot[i]
				< cpr_vreg->cpr_fuse_target_quot[i - 1])
				scaling[i] = 0;
		}
		scaling[i] = min(scaling[i], max_factor[i]);
		cpr_info(cpr_vreg, "fuse corner %d quotient adjustment scaling factor: %d.%03d\n",
			i, scaling[i] / 1000, scaling[i] % 1000);
	}

	/*
	 * Walk through the virtual corners mapped to each fuse corner
	 * and calculate the quotient adjustment for each one using the
	 * following formula:
	 * quot_adjust = (freq_max - freq_corner) * scaling / 1000
	 *
	 * @freq_max: max frequency in MHz supported by the fuse corner
	 * @freq_corner: frequency in MHz corresponding to the virtual corner
	 */
	for (j = CPR_FUSE_CORNER_MIN + 1; j <= highest_fuse_corner; j++) {
		for (i = corner_max[j - 1] + 1; i < corner_max[j]; i++) {
			freq_corner = freq_map[i] / 1000000; /* MHz */
			if (freq_corner > 0) {
				cpr_vreg->quot_adjust[i] = scaling[j] *
				   (freq_max[j] - freq_corner) / 1000;
			}
		}
	}

	rc = cpr_virtual_corner_quot_adjust(cpr_vreg, dev);
	if (rc) {
		cpr_err(cpr_vreg, "count not adjust virtual-corner quot rc=%d\n",
			rc);
		goto free_arrays;
	}

	for (i = CPR_CORNER_MIN; i <= cpr_vreg->num_corners; i++)
		cpr_info(cpr_vreg, "adjusted quotient[%d] = %d\n", i,
			cpr_vreg->cpr_fuse_target_quot[cpr_vreg->corner_map[i]]
			- cpr_vreg->quot_adjust[i]);

	maps_valid = true;

free_arrays:
	if (!rc) {

		rc = cpr_get_open_loop_voltage(cpr_vreg, dev, corner_max,
						freq_map, maps_valid);
		if (rc) {
			cpr_err(cpr_vreg, "could not fill open loop voltage array, rc=%d\n",
				rc);
			goto free_arrays_1;
		}

		rc = cpr_virtual_corner_voltage_adjust(cpr_vreg, dev);
		if (rc)
			cpr_err(cpr_vreg, "count not adjust virtual-corner voltage rc=%d\n",
				rc);
	}

free_arrays_1:
	kfree(max_factor);
	kfree(scaling);
	kfree(freq_map);
	kfree(corner_max);
	kfree(freq_max);
	return rc;
}

/*
 * Check if the redundant set of CPR fuses should be used in place of the
 * primary set and configure the cpr_fuse_redundant element accordingly.
 */
static int cpr_check_redundant(struct platform_device *pdev,
		     struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	u32 cpr_fuse_redun_sel[5];
	int rc;

	if (of_find_property(of_node, "qcom,cpr-fuse-redun-sel", NULL)) {
		rc = of_property_read_u32_array(of_node,
			"qcom,cpr-fuse-redun-sel", cpr_fuse_redun_sel, 5);
		if (rc < 0) {
			cpr_err(cpr_vreg, "qcom,cpr-fuse-redun-sel missing: rc=%d\n",
				rc);
			return rc;
		}
		cpr_vreg->cpr_fuse_redundant
			= cpr_fuse_is_setting_expected(cpr_vreg,
						cpr_fuse_redun_sel);
	} else {
		cpr_vreg->cpr_fuse_redundant = false;
	}

	if (cpr_vreg->cpr_fuse_redundant)
		cpr_info(cpr_vreg, "using redundant fuse parameters\n");

	return 0;
}

static int cpr_read_fuse_revision(struct platform_device *pdev,
		     struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	u32 fuse_sel[4];
	int rc;

	if (of_find_property(of_node, "qcom,cpr-fuse-revision", NULL)) {
		rc = of_property_read_u32_array(of_node,
			"qcom,cpr-fuse-revision", fuse_sel, 4);
		if (rc < 0) {
			cpr_err(cpr_vreg, "qcom,cpr-fuse-revision read failed: rc=%d\n",
				rc);
			return rc;
		}
		cpr_vreg->cpr_fuse_revision
			= cpr_read_efuse_param(cpr_vreg, fuse_sel[0],
					fuse_sel[1], fuse_sel[2], fuse_sel[3]);
		cpr_info(cpr_vreg, "fuse revision = %d\n",
			cpr_vreg->cpr_fuse_revision);
	} else {
		cpr_vreg->cpr_fuse_revision = FUSE_REVISION_UNKNOWN;
	}

	return 0;
}

static int cpr_read_ro_select(struct platform_device *pdev,
				     struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	int rc = 0;
	u32 cpr_fuse_row[2];
	char *ro_sel_str;
	int *bp_ro_sel;
	int i;

	bp_ro_sel
		= kzalloc((cpr_vreg->num_fuse_corners + 1) * sizeof(*bp_ro_sel),
			GFP_KERNEL);
	if (!bp_ro_sel)
		return -ENOMEM;

	if (cpr_vreg->cpr_fuse_redundant) {
		rc = of_property_read_u32_array(of_node,
				"qcom,cpr-fuse-redun-row",
				cpr_fuse_row, 2);
		ro_sel_str = "qcom,cpr-fuse-redun-ro-sel";
	} else {
		rc = of_property_read_u32_array(of_node, "qcom,cpr-fuse-row",
				cpr_fuse_row, 2);
		ro_sel_str = "qcom,cpr-fuse-ro-sel";
	}
	if (rc)
		goto error;

	rc = of_property_read_u32_array(of_node, ro_sel_str,
		&bp_ro_sel[CPR_FUSE_CORNER_MIN], cpr_vreg->num_fuse_corners);
	if (rc) {
		cpr_err(cpr_vreg, "%s read error, rc=%d\n", ro_sel_str, rc);
		goto error;
	}

	for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners; i++)
		cpr_vreg->cpr_fuse_ro_sel[i]
			= cpr_read_efuse_param(cpr_vreg, cpr_fuse_row[0],
				bp_ro_sel[i], CPR_FUSE_RO_SEL_BITS,
				cpr_fuse_row[1]);

error:
	kfree(bp_ro_sel);

	return rc;
}

static int cpr_find_fuse_map_match(struct platform_device *pdev,
				     struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	int i, j, rc, tuple_size;
	int len = 0;
	u32 *tmp, val, ro;

	/* Specify default no match case. */
	cpr_vreg->cpr_fuse_map_match = FUSE_MAP_NO_MATCH;
	cpr_vreg->cpr_fuse_map_count = 0;

	if (!of_find_property(of_node, "qcom,cpr-fuse-version-map", &len)) {
		/* No mapping present. */
		return 0;
	}

	tuple_size = cpr_vreg->num_fuse_corners + 3;
	cpr_vreg->cpr_fuse_map_count = len / (sizeof(u32) * tuple_size);

	if (len == 0 || len % (sizeof(u32) * tuple_size)) {
		cpr_err(cpr_vreg, "qcom,cpr-fuse-version-map length=%d is invalid\n",
			len);
		return -EINVAL;
	}

	tmp = kzalloc(len, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	rc = of_property_read_u32_array(of_node, "qcom,cpr-fuse-version-map",
				tmp, cpr_vreg->cpr_fuse_map_count * tuple_size);
	if (rc) {
		cpr_err(cpr_vreg, "could not read qcom,cpr-fuse-version-map, rc=%d\n",
			rc);
		goto done;
	}

	/*
	 * qcom,cpr-fuse-version-map tuple format:
	 * <speed_bin, pvs_version, cpr_fuse_revision, ro_sel[1], ...,
	 *  ro_sel[n]> for n == number of fuse corners
	 */
	for (i = 0; i < cpr_vreg->cpr_fuse_map_count; i++) {
		if (tmp[i * tuple_size] != cpr_vreg->speed_bin
		    && tmp[i * tuple_size] != FUSE_PARAM_MATCH_ANY)
			continue;
		if (tmp[i * tuple_size + 1] != cpr_vreg->pvs_version
		    && tmp[i * tuple_size + 1] != FUSE_PARAM_MATCH_ANY)
			continue;
		if (tmp[i * tuple_size + 2] != cpr_vreg->cpr_fuse_revision
		    && tmp[i * tuple_size + 2] != FUSE_PARAM_MATCH_ANY)
			continue;
		for (j = 0; j < cpr_vreg->num_fuse_corners; j++) {
			val = tmp[i * tuple_size + 3 + j];
			ro = cpr_vreg->cpr_fuse_ro_sel[j + CPR_FUSE_CORNER_MIN];
			if (val != ro && val != FUSE_PARAM_MATCH_ANY)
				break;
		}
		if (j == cpr_vreg->num_fuse_corners) {
			cpr_vreg->cpr_fuse_map_match = i;
			break;
		}
	}

	if (cpr_vreg->cpr_fuse_map_match != FUSE_MAP_NO_MATCH)
		cpr_debug(cpr_vreg, "qcom,cpr-fuse-version-map tuple match found: %d\n",
			cpr_vreg->cpr_fuse_map_match);
	else
		cpr_debug(cpr_vreg, "qcom,cpr-fuse-version-map tuple match not found\n");

done:
	kfree(tmp);
	return rc;
}

static int cpr_minimum_quot_difference_adjustment(struct platform_device *pdev,
					struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	int tuple_count, tuple_match;
	int rc, i, len = 0;
	u32 index, adjust_quot = 0;
	u32 *min_diff_quot;

	if (!of_find_property(of_node, "qcom,cpr-fuse-min-quot-diff", NULL))
		/* No conditional adjustment needed on revised quotients. */
		return 0;

	if (!of_find_property(of_node, "qcom,cpr-min-quot-diff-adjustment",
						&len)) {
		cpr_err(cpr_vreg, "qcom,cpr-min-quot-diff-adjustment not specified\n");
		return -ENODEV;
	}

	if (cpr_vreg->cpr_fuse_map_count) {
		if (cpr_vreg->cpr_fuse_map_match == FUSE_MAP_NO_MATCH)
			/* No matching index to use for quotient adjustment. */
			return 0;
		tuple_count = cpr_vreg->cpr_fuse_map_count;
		tuple_match = cpr_vreg->cpr_fuse_map_match;
	} else {
		tuple_count = 1;
		tuple_match = 0;
	}

	if (len != cpr_vreg->num_fuse_corners * tuple_count * sizeof(u32)) {
		cpr_err(cpr_vreg, "qcom,cpr-min-quot-diff-adjustment length=%d is invalid\n",
					len);
		return -EINVAL;
	}

	min_diff_quot = kcalloc(cpr_vreg->num_fuse_corners,
				sizeof(*min_diff_quot), GFP_KERNEL);
	if (!min_diff_quot)
		return -ENOMEM;

	rc = of_property_read_u32_array(of_node, "qcom,cpr-fuse-min-quot-diff",
						min_diff_quot,
						cpr_vreg->num_fuse_corners);
	if (rc < 0) {
		cpr_err(cpr_vreg, "qcom,cpr-fuse-min-quot-diff reading failed, rc = %d\n",
							rc);
		goto error;
	}

	for (i = CPR_FUSE_CORNER_MIN + 1;
				i <= cpr_vreg->num_fuse_corners; i++) {
		if ((cpr_vreg->cpr_fuse_target_quot[i]
			- cpr_vreg->cpr_fuse_target_quot[i - 1])
		    <= (int)min_diff_quot[i - CPR_FUSE_CORNER_MIN]) {
			index = tuple_match * cpr_vreg->num_fuse_corners
					+ i - CPR_FUSE_CORNER_MIN;
			rc = of_property_read_u32_index(of_node,
						"qcom,cpr-min-quot-diff-adjustment",
						index, &adjust_quot);
			if (rc) {
				cpr_err(cpr_vreg, "could not read qcom,cpr-min-quot-diff-adjustment index %u, rc=%d\n",
							index, rc);
				goto error;
			}

			cpr_vreg->cpr_fuse_target_quot[i]
				= cpr_vreg->cpr_fuse_target_quot[i - 1]
					+ adjust_quot;
			cpr_info(cpr_vreg, "Corner[%d]: revised adjusted quotient = %d\n",
					i, cpr_vreg->cpr_fuse_target_quot[i]);
		}
	}

error:
	kfree(min_diff_quot);
	return rc;
}

static int cpr_adjust_target_quots(struct platform_device *pdev,
					struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	int tuple_count, tuple_match, i;
	u32 index;
	u32 quot_adjust = 0;
	int len = 0;
	int rc = 0;

	if (!of_find_property(of_node, "qcom,cpr-quotient-adjustment", &len)) {
		/* No static quotient adjustment needed. */
		return 0;
	}

	if (cpr_vreg->cpr_fuse_map_count) {
		if (cpr_vreg->cpr_fuse_map_match == FUSE_MAP_NO_MATCH) {
			/* No matching index to use for quotient adjustment. */
			return 0;
		}
		tuple_count = cpr_vreg->cpr_fuse_map_count;
		tuple_match = cpr_vreg->cpr_fuse_map_match;
	} else {
		tuple_count = 1;
		tuple_match = 0;
	}

	if (len != cpr_vreg->num_fuse_corners * tuple_count * sizeof(u32)) {
		cpr_err(cpr_vreg, "qcom,cpr-quotient-adjustment length=%d is invalid\n",
			len);
		return -EINVAL;
	}

	for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners; i++) {
		index = tuple_match * cpr_vreg->num_fuse_corners
				+ i - CPR_FUSE_CORNER_MIN;
		rc = of_property_read_u32_index(of_node,
			"qcom,cpr-quotient-adjustment", index, &quot_adjust);
		if (rc) {
			cpr_err(cpr_vreg, "could not read qcom,cpr-quotient-adjustment index %u, rc=%d\n",
				index, rc);
			return rc;
		}

		if (quot_adjust) {
			cpr_vreg->cpr_fuse_target_quot[i] += quot_adjust;
			cpr_info(cpr_vreg, "Corner[%d]: adjusted target quot = %d\n",
				i, cpr_vreg->cpr_fuse_target_quot[i]);
		}
	}

	rc = cpr_minimum_quot_difference_adjustment(pdev, cpr_vreg);
	if (rc)
		cpr_err(cpr_vreg, "failed to apply minimum quot difference rc=%d\n",
					rc);

	return rc;
}

static int cpr_check_allowed(struct platform_device *pdev,
					struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	char *allow_str = "qcom,cpr-allowed";
	int rc = 0, count;
	int tuple_count, tuple_match;
	u32 allow_status;

	if (!of_find_property(of_node, allow_str, &count))
		/* CPR is allowed for all fuse revisions. */
		return 0;

	count /= sizeof(u32);
	if (cpr_vreg->cpr_fuse_map_count) {
		if (cpr_vreg->cpr_fuse_map_match == FUSE_MAP_NO_MATCH)
			/* No matching index to use for CPR allowed. */
			return 0;
		tuple_count = cpr_vreg->cpr_fuse_map_count;
		tuple_match = cpr_vreg->cpr_fuse_map_match;
	} else {
		tuple_count = 1;
		tuple_match = 0;
	}

	if (count != tuple_count) {
		cpr_err(cpr_vreg, "%s count=%d is invalid\n", allow_str,
			count);
		return -EINVAL;
	}

	rc = of_property_read_u32_index(of_node, allow_str, tuple_match,
		&allow_status);
	if (rc) {
		cpr_err(cpr_vreg, "could not read %s index %u, rc=%d\n",
			allow_str, tuple_match, rc);
		return rc;
	}

	if (allow_status && !cpr_vreg->cpr_fuse_disable)
		cpr_vreg->cpr_fuse_disable = false;
	else
		cpr_vreg->cpr_fuse_disable = true;

	cpr_info(cpr_vreg, "CPR closed loop is %s for fuse revision %d\n",
		cpr_vreg->cpr_fuse_disable ? "disabled" : "enabled",
		cpr_vreg->cpr_fuse_revision);

	return rc;
}

static int cpr_check_de_aging_allowed(struct cpr_regulator *cpr_vreg,
				struct device *dev)
{
	struct device_node *of_node = dev->of_node;
	char *allow_str = "qcom,cpr-de-aging-allowed";
	int rc = 0, count;
	int tuple_count, tuple_match;
	u32 allow_status = 0;

	if (!of_find_property(of_node, allow_str, &count)) {
		/* CPR de-aging is not allowed for all fuse revisions. */
		return allow_status;
	}

	count /= sizeof(u32);
	if (cpr_vreg->cpr_fuse_map_count) {
		if (cpr_vreg->cpr_fuse_map_match == FUSE_MAP_NO_MATCH)
			/* No matching index to use for CPR de-aging allowed. */
			return 0;
		tuple_count = cpr_vreg->cpr_fuse_map_count;
		tuple_match = cpr_vreg->cpr_fuse_map_match;
	} else {
		tuple_count = 1;
		tuple_match = 0;
	}

	if (count != tuple_count) {
		cpr_err(cpr_vreg, "%s count=%d is invalid\n", allow_str,
			count);
		return -EINVAL;
	}

	rc = of_property_read_u32_index(of_node, allow_str, tuple_match,
		&allow_status);
	if (rc) {
		cpr_err(cpr_vreg, "could not read %s index %u, rc=%d\n",
			allow_str, tuple_match, rc);
		return rc;
	}

	cpr_info(cpr_vreg, "CPR de-aging is %s for fuse revision %d\n",
			allow_status ? "allowed" : "not allowed",
			cpr_vreg->cpr_fuse_revision);

	return allow_status;
}

static int cpr_aging_init(struct platform_device *pdev,
			struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	struct cpr_aging_info *aging_info;
	struct cpr_aging_sensor_info *sensor_info;
	int num_fuse_corners = cpr_vreg->num_fuse_corners;
	int i, rc = 0, len = 0, num_aging_sensors, ro_sel, bits;
	u32 *aging_sensor_id, *fuse_sel, *fuse_sel_orig;
	u32 sensor = 0, non_collapsible_sensor_mask = 0;
	u64 efuse_val;
	struct property *prop;

	if (!of_find_property(of_node, "qcom,cpr-aging-sensor-id", &len)) {
		/* No CPR de-aging adjustments needed */
		return 0;
	}

	if (len == 0) {
		cpr_err(cpr_vreg, "qcom,cpr-aging-sensor-id property format is invalid\n");
		return -EINVAL;
	}
	num_aging_sensors = len / sizeof(u32);
	cpr_debug(cpr_vreg, "No of aging sensors = %d\n", num_aging_sensors);

	if (cpumask_empty(&cpr_vreg->cpu_mask)) {
		cpr_err(cpr_vreg, "qcom,cpr-cpus property missing\n");
		return -EINVAL;
	}

	rc = cpr_check_de_aging_allowed(cpr_vreg, &pdev->dev);
	if (rc < 0) {
		cpr_err(cpr_vreg, "cpr_check_de_aging_allowed failed: rc=%d\n",
			rc);
		return rc;
	} else if (rc == 0) {
		/* CPR de-aging is not allowed for the current fuse combo */
		return 0;
	}

	aging_info = devm_kzalloc(&pdev->dev, sizeof(*aging_info),
				GFP_KERNEL);
	if (!aging_info)
		return -ENOMEM;

	cpr_vreg->aging_info = aging_info;
	aging_info->num_aging_sensors = num_aging_sensors;

	rc = of_property_read_u32(of_node, "qcom,cpr-aging-ref-corner",
			&aging_info->aging_corner);
	if (rc) {
		cpr_err(cpr_vreg, "qcom,cpr-aging-ref-corner missing rc=%d\n",
			rc);
		return rc;
	}

	CPR_PROP_READ_U32(cpr_vreg, of_node, "cpr-aging-ref-voltage",
			&aging_info->aging_ref_voltage, rc);
	if (rc)
		return rc;

	CPR_PROP_READ_U32(cpr_vreg, of_node, "cpr-max-aging-margin",
			&aging_info->max_aging_margin, rc);
	if (rc)
		return rc;

	CPR_PROP_READ_U32(cpr_vreg, of_node, "cpr-aging-ro-scaling-factor",
			&aging_info->aging_ro_kv, rc);
	if (rc)
		return rc;

	/* Check for DIV by 0 error */
	if (aging_info->aging_ro_kv == 0) {
		cpr_err(cpr_vreg, "invalid cpr-aging-ro-scaling-factor value: %u\n",
			aging_info->aging_ro_kv);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(of_node, "qcom,cpr-ro-scaling-factor",
			aging_info->cpr_ro_kv, CPR_NUM_RING_OSC);
	if (rc) {
		cpr_err(cpr_vreg, "qcom,cpr-ro-scaling-factor property read failed, rc = %d\n",
			rc);
		return rc;
	}

	if (of_find_property(of_node, "qcom,cpr-non-collapsible-sensors",
				&len)) {
		len = len / sizeof(u32);
		if (len <= 0 || len > 32) {
			cpr_err(cpr_vreg, "qcom,cpr-non-collapsible-sensors has an incorrect size\n");
			return -EINVAL;
		}

		for (i = 0; i < len; i++) {
			rc = of_property_read_u32_index(of_node,
						"qcom,cpr-non-collapsible-sensors",
						i, &sensor);
			if (rc) {
				cpr_err(cpr_vreg, "could not read qcom,cpr-non-collapsible-sensors index %u, rc=%d\n",
					i, rc);
				return rc;
			}

			if (sensor > 31) {
				cpr_err(cpr_vreg, "invalid non-collapsible sensor = %u\n",
					sensor);
				return -EINVAL;
			}

			non_collapsible_sensor_mask |= BIT(sensor);
		}

		/*
		 * Bypass the sensors in collapsible domain for
		 * de-aging measurements
		 */
		aging_info->aging_sensor_bypass =
						~(non_collapsible_sensor_mask);
		cpr_debug(cpr_vreg, "sensor bypass mask for aging = 0x%08x\n",
			aging_info->aging_sensor_bypass);
	}

	prop = of_find_property(pdev->dev.of_node, "qcom,cpr-aging-derate",
			NULL);
	if ((!prop) ||
		(prop->length != num_fuse_corners * sizeof(u32))) {
		cpr_err(cpr_vreg, "qcom,cpr-aging-derate incorrectly configured\n");
		return -EINVAL;
	}

	aging_sensor_id = kcalloc(num_aging_sensors, sizeof(*aging_sensor_id),
				GFP_KERNEL);
	fuse_sel = kcalloc(num_aging_sensors * 4, sizeof(*fuse_sel),
				GFP_KERNEL);
	aging_info->voltage_adjust = devm_kcalloc(&pdev->dev,
					num_fuse_corners + 1,
					sizeof(*aging_info->voltage_adjust),
					GFP_KERNEL);
	aging_info->sensor_info = devm_kcalloc(&pdev->dev, num_aging_sensors,
					sizeof(*aging_info->sensor_info),
					GFP_KERNEL);
	aging_info->aging_derate = devm_kcalloc(&pdev->dev,
					num_fuse_corners + 1,
					sizeof(*aging_info->aging_derate),
					GFP_KERNEL);

	if (!aging_info->aging_derate || !aging_sensor_id
		|| !aging_info->sensor_info || !fuse_sel
		|| !aging_info->voltage_adjust)
		goto err;

	rc = of_property_read_u32_array(of_node, "qcom,cpr-aging-sensor-id",
					aging_sensor_id, num_aging_sensors);
	if (rc) {
		cpr_err(cpr_vreg, "qcom,cpr-aging-sensor-id property read failed, rc = %d\n",
				rc);
		goto err;
	}

	for (i = 0; i < num_aging_sensors; i++)
		if (aging_sensor_id[i] < 0 || aging_sensor_id[i] > 31) {
			cpr_err(cpr_vreg, "Invalid aging sensor id: %u\n",
				aging_sensor_id[i]);
			rc = -EINVAL;
			goto err;
		}

	rc = of_property_read_u32_array(of_node, "qcom,cpr-aging-derate",
			&aging_info->aging_derate[CPR_FUSE_CORNER_MIN],
			num_fuse_corners);
	if (rc) {
		cpr_err(cpr_vreg, "qcom,cpr-aging-derate property read failed, rc = %d\n",
				rc);
		goto err;
	}

	rc = of_property_read_u32_array(of_node,
				"qcom,cpr-fuse-aging-init-quot-diff",
				fuse_sel, (num_aging_sensors * 4));
	if (rc) {
		cpr_err(cpr_vreg, "qcom,cpr-fuse-aging-init-quot-diff read failed, rc = %d\n",
				rc);
		goto err;
	}

	fuse_sel_orig = fuse_sel;
	sensor_info = aging_info->sensor_info;
	for (i = 0; i < num_aging_sensors; i++, sensor_info++) {
		sensor_info->sensor_id = aging_sensor_id[i];
		efuse_val = cpr_read_efuse_param(cpr_vreg, fuse_sel[0],
				fuse_sel[1], fuse_sel[2], fuse_sel[3]);
		bits = fuse_sel[2];
		sensor_info->initial_quot_diff = ((efuse_val & BIT(bits - 1)) ?
			-1 : 1) * (efuse_val & (BIT(bits - 1) - 1));

		cpr_debug(cpr_vreg, "Age sensor[%d] Initial quot diff = %d\n",
				sensor_info->sensor_id,
				sensor_info->initial_quot_diff);
		fuse_sel += 4;
	}

	/*
	 * Add max aging margin here. This can be adjusted later in
	 * de-aging algorithm.
	 */
	for (i = CPR_FUSE_CORNER_MIN; i <= num_fuse_corners; i++) {
		ro_sel = cpr_vreg->cpr_fuse_ro_sel[i];
		cpr_vreg->cpr_fuse_target_quot[i] +=
				(aging_info->cpr_ro_kv[ro_sel]
				* aging_info->max_aging_margin) / 1000000;
		aging_info->voltage_adjust[i] = aging_info->max_aging_margin;
		cpr_info(cpr_vreg, "Corner[%d]: age margin adjusted quotient = %d\n",
			i, cpr_vreg->cpr_fuse_target_quot[i]);
	}

	kfree(fuse_sel_orig);
err:
	kfree(aging_sensor_id);
	return rc;
}

static int cpr_cpu_map_init(struct cpr_regulator *cpr_vreg, struct device *dev)
{
	struct device_node *cpu_node;
	int i, cpu;

	if (!of_find_property(dev->of_node, "qcom,cpr-cpus",
				&cpr_vreg->num_adj_cpus)) {
		/* No adjustments based on online cores */
		return 0;
	}
	cpr_vreg->num_adj_cpus /= sizeof(u32);

	cpr_vreg->adj_cpus = devm_kcalloc(dev, cpr_vreg->num_adj_cpus,
					sizeof(int), GFP_KERNEL);
	if (!cpr_vreg->adj_cpus)
		return -ENOMEM;

	for (i = 0; i < cpr_vreg->num_adj_cpus; i++) {
		cpu_node = of_parse_phandle(dev->of_node, "qcom,cpr-cpus", i);
		if (!cpu_node) {
			cpr_err(cpr_vreg, "could not find CPU node %d\n", i);
			return -EINVAL;
		}
		cpr_vreg->adj_cpus[i] = -1;
		for_each_possible_cpu(cpu) {
			if (of_get_cpu_node(cpu, NULL) == cpu_node) {
				cpr_vreg->adj_cpus[i] = cpu;
				cpumask_set_cpu(cpu, &cpr_vreg->cpu_mask);
				break;
			}
		}
		of_node_put(cpu_node);
	}

	return 0;
}

static int cpr_init_cpr_efuse(struct platform_device *pdev,
				     struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	int i, rc = 0;
	bool scheme_fuse_valid = false;
	bool disable_fuse_valid = false;
	char *targ_quot_str;
	u32 cpr_fuse_row[2];
	u32 bp_cpr_disable, bp_scheme;
	size_t len;
	int *bp_target_quot;
	u64 fuse_bits, fuse_bits_2;
	u32 *target_quot_size;
	struct cpr_quot_scale *quot_scale;

	len = cpr_vreg->num_fuse_corners + 1;

	bp_target_quot = kcalloc(len, sizeof(*bp_target_quot), GFP_KERNEL);
	target_quot_size = kcalloc(len, sizeof(*target_quot_size), GFP_KERNEL);
	quot_scale = kcalloc(len, sizeof(*quot_scale), GFP_KERNEL);

	if (!bp_target_quot || !target_quot_size || !quot_scale) {
		rc = -ENOMEM;
		goto error;
	}

	if (cpr_vreg->cpr_fuse_redundant) {
		rc = of_property_read_u32_array(of_node,
				"qcom,cpr-fuse-redun-row",
				cpr_fuse_row, 2);
		targ_quot_str = "qcom,cpr-fuse-redun-target-quot";
	} else {
		rc = of_property_read_u32_array(of_node, "qcom,cpr-fuse-row",
				cpr_fuse_row, 2);
		targ_quot_str = "qcom,cpr-fuse-target-quot";
	}
	if (rc)
		goto error;

	rc = of_property_read_u32_array(of_node, targ_quot_str,
		&bp_target_quot[CPR_FUSE_CORNER_MIN],
		cpr_vreg->num_fuse_corners);
	if (rc < 0) {
		cpr_err(cpr_vreg, "missing %s: rc=%d\n", targ_quot_str, rc);
		goto error;
	}

	if (of_find_property(of_node, "qcom,cpr-fuse-target-quot-size", NULL)) {
		rc = of_property_read_u32_array(of_node,
			"qcom,cpr-fuse-target-quot-size",
			&target_quot_size[CPR_FUSE_CORNER_MIN],
			cpr_vreg->num_fuse_corners);
		if (rc < 0) {
			cpr_err(cpr_vreg, "error while reading qcom,cpr-fuse-target-quot-size: rc=%d\n",
				rc);
			goto error;
		}
	} else {
		/*
		 * Default fuse quotient parameter size to match target register
		 * size.
		 */
		for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners;
		     i++)
			target_quot_size[i] = CPR_FUSE_TARGET_QUOT_BITS;
	}

	if (of_find_property(of_node, "qcom,cpr-fuse-target-quot-scale",
				NULL)) {
		for (i = 0; i < cpr_vreg->num_fuse_corners; i++) {
			rc = of_property_read_u32_index(of_node,
				"qcom,cpr-fuse-target-quot-scale", i * 2,
				&quot_scale[i + CPR_FUSE_CORNER_MIN].offset);
			if (rc < 0) {
				cpr_err(cpr_vreg, "error while reading qcom,cpr-fuse-target-quot-scale: rc=%d\n",
					rc);
				goto error;
			}

			rc = of_property_read_u32_index(of_node,
				"qcom,cpr-fuse-target-quot-scale", i * 2 + 1,
			       &quot_scale[i + CPR_FUSE_CORNER_MIN].multiplier);
			if (rc < 0) {
				cpr_err(cpr_vreg, "error while reading qcom,cpr-fuse-target-quot-scale: rc=%d\n",
					rc);
				goto error;
			}
		}
	} else {
		/*
		 * In the default case, target quotients require no scaling so
		 * use offset = 0, multiplier = 1.
		 */
		for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners;
		     i++) {
			quot_scale[i].offset = 0;
			quot_scale[i].multiplier = 1;
		}
	}

	/* Read the control bits of eFuse */
	fuse_bits = cpr_read_efuse_row(cpr_vreg, cpr_fuse_row[0],
					cpr_fuse_row[1]);
	cpr_info(cpr_vreg, "[row:%d] = 0x%llx\n", cpr_fuse_row[0], fuse_bits);

	if (cpr_vreg->cpr_fuse_redundant) {
		if (of_find_property(of_node,
				"qcom,cpr-fuse-redun-bp-cpr-disable", NULL)) {
			CPR_PROP_READ_U32(cpr_vreg, of_node,
					  "cpr-fuse-redun-bp-cpr-disable",
					  &bp_cpr_disable, rc);
			disable_fuse_valid = true;
			if (of_find_property(of_node,
					"qcom,cpr-fuse-redun-bp-scheme",
					NULL)) {
				CPR_PROP_READ_U32(cpr_vreg, of_node,
						"cpr-fuse-redun-bp-scheme",
						&bp_scheme, rc);
				scheme_fuse_valid = true;
			}
			if (rc)
				goto error;
			fuse_bits_2 = fuse_bits;
		} else {
			u32 temp_row[2];

			/* Use original fuse if no optional property */
			if (of_find_property(of_node,
					"qcom,cpr-fuse-bp-cpr-disable", NULL)) {
				CPR_PROP_READ_U32(cpr_vreg, of_node,
					"cpr-fuse-bp-cpr-disable",
					&bp_cpr_disable, rc);
				disable_fuse_valid = true;
			}
			if (of_find_property(of_node,
					"qcom,cpr-fuse-bp-scheme",
					NULL)) {
				CPR_PROP_READ_U32(cpr_vreg, of_node,
						"cpr-fuse-bp-scheme",
						&bp_scheme, rc);
				scheme_fuse_valid = true;
			}
			rc = of_property_read_u32_array(of_node,
					"qcom,cpr-fuse-row",
					temp_row, 2);
			if (rc)
				goto error;

			fuse_bits_2 = cpr_read_efuse_row(cpr_vreg, temp_row[0],
							temp_row[1]);
			cpr_info(cpr_vreg, "[original row:%d] = 0x%llx\n",
				temp_row[0], fuse_bits_2);
		}
	} else {
		if (of_find_property(of_node, "qcom,cpr-fuse-bp-cpr-disable",
					NULL)) {
			CPR_PROP_READ_U32(cpr_vreg, of_node,
				"cpr-fuse-bp-cpr-disable", &bp_cpr_disable, rc);
			disable_fuse_valid = true;
		}
		if (of_find_property(of_node, "qcom,cpr-fuse-bp-scheme",
							NULL)) {
			CPR_PROP_READ_U32(cpr_vreg, of_node,
					"cpr-fuse-bp-scheme", &bp_scheme, rc);
			scheme_fuse_valid = true;
		}
		if (rc)
			goto error;
		fuse_bits_2 = fuse_bits;
	}

	if (disable_fuse_valid) {
		cpr_vreg->cpr_fuse_disable =
					(fuse_bits_2 >> bp_cpr_disable) & 0x01;
		cpr_info(cpr_vreg, "CPR disable fuse = %d\n",
			cpr_vreg->cpr_fuse_disable);
	} else {
		cpr_vreg->cpr_fuse_disable = false;
	}

	if (scheme_fuse_valid) {
		cpr_vreg->cpr_fuse_local = (fuse_bits_2 >> bp_scheme) & 0x01;
		cpr_info(cpr_vreg, "local = %d\n", cpr_vreg->cpr_fuse_local);
	} else {
		cpr_vreg->cpr_fuse_local = true;
	}

	for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners; i++) {
		cpr_vreg->cpr_fuse_target_quot[i]
			= cpr_read_efuse_param(cpr_vreg, cpr_fuse_row[0],
				bp_target_quot[i], target_quot_size[i],
				cpr_fuse_row[1]);
		/* Unpack the target quotient by scaling. */
		cpr_vreg->cpr_fuse_target_quot[i] *= quot_scale[i].multiplier;
		cpr_vreg->cpr_fuse_target_quot[i] += quot_scale[i].offset;
		cpr_info(cpr_vreg,
			"Corner[%d]: ro_sel = %d, target quot = %d\n", i,
			cpr_vreg->cpr_fuse_ro_sel[i],
			cpr_vreg->cpr_fuse_target_quot[i]);
	}

	rc = cpr_cpu_map_init(cpr_vreg, &pdev->dev);
	if (rc) {
		cpr_err(cpr_vreg, "CPR cpu map init failed: rc=%d\n", rc);
		goto error;
	}

	rc = cpr_aging_init(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "CPR aging init failed: rc=%d\n", rc);
		goto error;
	}

	rc = cpr_adjust_target_quots(pdev, cpr_vreg);
	if (rc)
		goto error;

	for (i = CPR_FUSE_CORNER_MIN + 1;
				i <= cpr_vreg->num_fuse_corners; i++) {
		if (cpr_vreg->cpr_fuse_target_quot[i]
				< cpr_vreg->cpr_fuse_target_quot[i - 1] &&
			cpr_vreg->cpr_fuse_ro_sel[i] ==
				cpr_vreg->cpr_fuse_ro_sel[i - 1]) {
			cpr_vreg->cpr_fuse_disable = true;
			cpr_err(cpr_vreg, "invalid quotient values; permanently disabling CPR\n");
		}
	}

	if (cpr_vreg->flags & FLAGS_UPLIFT_QUOT_VOLT) {
		cpr_voltage_uplift_wa_inc_quot(cpr_vreg, of_node);
		for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners;
		     i++) {
			cpr_info(cpr_vreg,
				"Corner[%d]: uplifted target quot = %d\n",
				i, cpr_vreg->cpr_fuse_target_quot[i]);
		}
	}

	/*
	 * Check whether the fuse-quot-offset is defined per fuse corner.
	 * If it is defined, use it (quot_offset) in the calculation
	 * below for obtaining scaling factor per fuse corner.
	 */
	rc = cpr_get_fuse_quot_offset(cpr_vreg, pdev, quot_scale);
	if (rc < 0)
		goto error;

	rc = cpr_get_corner_quot_adjustment(cpr_vreg, &pdev->dev);
	if (rc)
		goto error;

	cpr_vreg->cpr_fuse_bits = fuse_bits;
	if (!cpr_vreg->cpr_fuse_bits) {
		cpr_vreg->cpr_fuse_disable = true;
		cpr_err(cpr_vreg,
			"cpr_fuse_bits == 0; permanently disabling CPR\n");
	} else if (!cpr_vreg->fuse_quot_offset) {
		/*
		 * Check if the target quotients for the highest two fuse
		 * corners are too close together.
		 */
		int *quot = cpr_vreg->cpr_fuse_target_quot;
		int highest_fuse_corner = cpr_vreg->num_fuse_corners;
		u32 min_diff_quot;
		bool valid_fuse = true;

		min_diff_quot = CPR_FUSE_MIN_QUOT_DIFF;
		of_property_read_u32(of_node, "qcom,cpr-quot-min-diff",
							&min_diff_quot);

		if (quot[highest_fuse_corner] > quot[highest_fuse_corner - 1]) {
			if ((quot[highest_fuse_corner]
				- quot[highest_fuse_corner - 1])
					<= min_diff_quot)
				valid_fuse = false;
		} else {
			valid_fuse = false;
		}

		if (!valid_fuse) {
			cpr_vreg->cpr_fuse_disable = true;
			cpr_err(cpr_vreg, "invalid quotient values; permanently disabling CPR\n");
		}
	}
	rc = cpr_check_allowed(pdev, cpr_vreg);

error:
	kfree(bp_target_quot);
	kfree(target_quot_size);
	kfree(quot_scale);

	return rc;
}

static int cpr_init_cpr_voltages(struct cpr_regulator *cpr_vreg,
			struct device *dev)
{
	int i;
	int size = cpr_vreg->num_corners + 1;

	cpr_vreg->last_volt = devm_kzalloc(dev, sizeof(int) * size, GFP_KERNEL);
	if (!cpr_vreg->last_volt)
		return -EINVAL;

	for (i = CPR_CORNER_MIN; i <= cpr_vreg->num_corners; i++)
		cpr_vreg->last_volt[i] = cpr_vreg->open_loop_volt[i];

	return 0;
}

/*
 * This function fills the virtual_limit array with voltages read from the
 * prop_name device tree property if a given tuple in the property matches
 * the speedbin and PVS version fuses found on the chip.  Otherwise,
 * it fills the virtual_limit_array with corresponding values from the
 * fuse_limit_array.
 */
static int cpr_fill_override_voltage(struct cpr_regulator *cpr_vreg,
		struct device *dev, const char *prop_name, const char *label,
		int *virtual_limit, int *fuse_limit)
{
	int rc = 0;
	int i, j, size, pos;
	struct property *prop;
	bool match_found = false;
	size_t buflen;
	char *buf;
	u32 *tmp;

	prop = of_find_property(dev->of_node, prop_name, NULL);
	if (!prop)
		goto use_fuse_corner_limits;

	size = prop->length / sizeof(u32);
	if (size == 0 || size % (cpr_vreg->num_corners + 2)) {
		cpr_err(cpr_vreg, "%s property format is invalid; reusing per-fuse-corner limits\n",
			prop_name);
		goto use_fuse_corner_limits;
	}

	tmp = kcalloc(size, sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;
	rc = of_property_read_u32_array(dev->of_node, prop_name, tmp, size);
	if (rc < 0) {
		kfree(tmp);
		cpr_err(cpr_vreg, "%s reading failed, rc = %d\n", prop_name,
			rc);
		return rc;
	}

	/*
	 * Get limit voltage for each virtual corner based upon the speed_bin
	 * and pvs_version values.
	 */
	for (i = 0; i < size; i += cpr_vreg->num_corners + 2) {
		if (tmp[i] != cpr_vreg->speed_bin &&
		    tmp[i] != FUSE_PARAM_MATCH_ANY)
			continue;
		if (tmp[i + 1] != cpr_vreg->pvs_version &&
		    tmp[i + 1] != FUSE_PARAM_MATCH_ANY)
			continue;
		for (j = CPR_CORNER_MIN; j <= cpr_vreg->num_corners; j++)
			virtual_limit[j] = tmp[i + 2 + j - CPR_FUSE_CORNER_MIN];
		match_found = true;
		break;
	}
	kfree(tmp);

	if (!match_found)
		goto use_fuse_corner_limits;

	/*
	 * Log per-virtual-corner voltage limits since they are useful for
	 * baseline CPR debugging.
	 */
	buflen = cpr_vreg->num_corners * (MAX_CHARS_PER_INT + 2) * sizeof(*buf);
	buf = kzalloc(buflen, GFP_KERNEL);
	if (buf == NULL) {
		cpr_err(cpr_vreg, "Could not allocate memory for corner limit voltage logging\n");
		return 0;
	}

	for (i = CPR_CORNER_MIN, pos = 0; i <= cpr_vreg->num_corners; i++)
		pos += scnprintf(buf + pos, buflen - pos, "%d%s",
			virtual_limit[i], i < cpr_vreg->num_corners ? " " : "");
	cpr_info(cpr_vreg, "%s override voltage: [%s] uV\n", label, buf);
	kfree(buf);

	return rc;

use_fuse_corner_limits:
	for (i = CPR_CORNER_MIN; i <= cpr_vreg->num_corners; i++)
		virtual_limit[i] = fuse_limit[cpr_vreg->corner_map[i]];
	return rc;
}

/*
 * This function loads per-virtual-corner ceiling and floor voltages from device
 * tree if their respective device tree properties are present.  These limits
 * override those found in the per-fuse-corner arrays fuse_ceiling_volt and
 * fuse_floor_volt.
 */
static int cpr_init_ceiling_floor_override_voltages(
	struct cpr_regulator *cpr_vreg, struct device *dev)
{
	int rc, i;
	int size = cpr_vreg->num_corners + 1;

	cpr_vreg->ceiling_volt = devm_kzalloc(dev, sizeof(int) * size,
						GFP_KERNEL);
	cpr_vreg->floor_volt = devm_kzalloc(dev, sizeof(int) * size,
						GFP_KERNEL);
	cpr_vreg->cpr_max_ceiling = devm_kzalloc(dev, sizeof(int) * size,
						GFP_KERNEL);
	if (!cpr_vreg->ceiling_volt || !cpr_vreg->floor_volt ||
		!cpr_vreg->cpr_max_ceiling)
		return -ENOMEM;

	rc = cpr_fill_override_voltage(cpr_vreg, dev,
		"qcom,cpr-voltage-ceiling-override", "ceiling",
		cpr_vreg->ceiling_volt, cpr_vreg->fuse_ceiling_volt);
	if (rc)
		return rc;

	rc = cpr_fill_override_voltage(cpr_vreg, dev,
		"qcom,cpr-voltage-floor-override", "floor",
		cpr_vreg->floor_volt, cpr_vreg->fuse_floor_volt);
	if (rc)
		return rc;

	for (i = CPR_CORNER_MIN; i <= cpr_vreg->num_corners; i++) {
		if (cpr_vreg->floor_volt[i] > cpr_vreg->ceiling_volt[i]) {
			cpr_err(cpr_vreg, "virtual corner %d floor=%d uV > ceiling=%d uV\n",
				i, cpr_vreg->floor_volt[i],
				cpr_vreg->ceiling_volt[i]);
			return -EINVAL;
		}

		if (cpr_vreg->ceiling_max < cpr_vreg->ceiling_volt[i])
			cpr_vreg->ceiling_max = cpr_vreg->ceiling_volt[i];
		cpr_vreg->cpr_max_ceiling[i] = cpr_vreg->ceiling_volt[i];
	}

	return rc;
}

/*
 * This function computes the per-virtual-corner floor voltages from
 * per-virtual-corner ceiling voltages with an offset specified by a
 * device-tree property. This must be called after open-loop voltage
 * scaling, floor_volt array loading and the ceiling voltage is
 * conditionally reduced to the open-loop voltage. It selects the
 * maximum value between the calculated floor voltage values and
 * the floor_volt array values and stores them in the floor_volt array.
 */
static int cpr_init_floor_to_ceiling_range(
	struct cpr_regulator *cpr_vreg, struct device *dev)
{
	int rc, i, tuple_count, tuple_match, len, pos;
	u32 index, floor_volt_adjust = 0;
	char *prop_str, *buf;
	size_t buflen;

	prop_str = "qcom,cpr-floor-to-ceiling-max-range";

	if (!of_find_property(dev->of_node, prop_str, &len))
		return 0;

	if (cpr_vreg->cpr_fuse_map_count) {
		if (cpr_vreg->cpr_fuse_map_match == FUSE_MAP_NO_MATCH) {
			/*
			 * No matching index to use for floor-to-ceiling
			 * max range.
			 */
			return 0;
		}
		tuple_count = cpr_vreg->cpr_fuse_map_count;
		tuple_match = cpr_vreg->cpr_fuse_map_match;
	} else {
		tuple_count = 1;
		tuple_match = 0;
	}

	if (len != cpr_vreg->num_corners * tuple_count * sizeof(u32)) {
		cpr_err(cpr_vreg, "%s length=%d is invalid\n", prop_str, len);
		return -EINVAL;
	}

	for (i = CPR_CORNER_MIN; i <= cpr_vreg->num_corners; i++) {
		index = tuple_match * cpr_vreg->num_corners
				+ i - CPR_CORNER_MIN;
		rc = of_property_read_u32_index(dev->of_node, prop_str,
			index, &floor_volt_adjust);
		if (rc) {
			cpr_err(cpr_vreg, "could not read %s index %u, rc=%d\n",
				prop_str, index, rc);
			return rc;
		}

		if ((int)floor_volt_adjust >= 0) {
			cpr_vreg->floor_volt[i] = max(cpr_vreg->floor_volt[i],
						(cpr_vreg->ceiling_volt[i]
						- (int)floor_volt_adjust));
			cpr_vreg->floor_volt[i]
					= DIV_ROUND_UP(cpr_vreg->floor_volt[i],
							cpr_vreg->step_volt) *
							cpr_vreg->step_volt;
			if (cpr_vreg->open_loop_volt[i]
					< cpr_vreg->floor_volt[i])
				cpr_vreg->open_loop_volt[i]
						= cpr_vreg->floor_volt[i];
		}
	}

	/*
	 * Log per-virtual-corner voltage limits resulted after considering the
	 * floor-to-ceiling max range since they are useful for baseline CPR
	 * debugging.
	 */
	buflen = cpr_vreg->num_corners * (MAX_CHARS_PER_INT + 2) * sizeof(*buf);
	buf = kzalloc(buflen, GFP_KERNEL);
	if (buf == NULL) {
		cpr_err(cpr_vreg, "Could not allocate memory for corner limit voltage logging\n");
		return 0;
	}

	for (i = CPR_CORNER_MIN, pos = 0; i <= cpr_vreg->num_corners; i++)
		pos += scnprintf(buf + pos, buflen - pos, "%d%s",
			cpr_vreg->floor_volt[i],
			i < cpr_vreg->num_corners ? " " : "");
	cpr_info(cpr_vreg, "Final floor override voltages: [%s] uV\n", buf);
	kfree(buf);

	return 0;
}

static int cpr_init_step_quotient(struct platform_device *pdev,
		  struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	int len = 0;
	u32 step_quot[CPR_NUM_RING_OSC];
	int i, rc;

	if (!of_find_property(of_node, "qcom,cpr-step-quotient", &len)) {
		cpr_err(cpr_vreg, "qcom,cpr-step-quotient property missing\n");
		return -EINVAL;
	}

	if (len == sizeof(u32)) {
		/* Single step quotient used for all ring oscillators. */
		rc = of_property_read_u32(of_node, "qcom,cpr-step-quotient",
					step_quot);
		if (rc) {
			cpr_err(cpr_vreg, "could not read qcom,cpr-step-quotient, rc=%d\n",
				rc);
			return rc;
		}

		for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners;
		     i++)
			cpr_vreg->step_quotient[i] = step_quot[0];
	} else if (len == sizeof(u32) * CPR_NUM_RING_OSC) {
		/* Unique step quotient used per ring oscillator. */
		rc = of_property_read_u32_array(of_node,
			"qcom,cpr-step-quotient", step_quot, CPR_NUM_RING_OSC);
		if (rc) {
			cpr_err(cpr_vreg, "could not read qcom,cpr-step-quotient, rc=%d\n",
				rc);
			return rc;
		}

		for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners;
		     i++)
			cpr_vreg->step_quotient[i]
				= step_quot[cpr_vreg->cpr_fuse_ro_sel[i]];
	} else {
		cpr_err(cpr_vreg, "qcom,cpr-step-quotient has invalid length=%d\n",
			len);
		return -EINVAL;
	}

	for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners; i++)
		cpr_debug(cpr_vreg, "step_quotient[%d]=%u\n", i,
			cpr_vreg->step_quotient[i]);

	return 0;
}

static int cpr_init_cpr_parameters(struct platform_device *pdev,
					  struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	int rc = 0;

	CPR_PROP_READ_U32(cpr_vreg, of_node, "cpr-ref-clk",
			  &cpr_vreg->ref_clk_khz, rc);
	if (rc)
		return rc;
	CPR_PROP_READ_U32(cpr_vreg, of_node, "cpr-timer-delay",
			  &cpr_vreg->timer_delay_us, rc);
	if (rc)
		return rc;
	CPR_PROP_READ_U32(cpr_vreg, of_node, "cpr-timer-cons-up",
			  &cpr_vreg->timer_cons_up, rc);
	if (rc)
		return rc;
	CPR_PROP_READ_U32(cpr_vreg, of_node, "cpr-timer-cons-down",
			  &cpr_vreg->timer_cons_down, rc);
	if (rc)
		return rc;
	CPR_PROP_READ_U32(cpr_vreg, of_node, "cpr-irq-line",
			  &cpr_vreg->irq_line, rc);
	if (rc)
		return rc;

	rc = cpr_init_step_quotient(pdev, cpr_vreg);
	if (rc)
		return rc;

	CPR_PROP_READ_U32(cpr_vreg, of_node, "cpr-up-threshold",
			  &cpr_vreg->up_threshold, rc);
	if (rc)
		return rc;
	CPR_PROP_READ_U32(cpr_vreg, of_node, "cpr-down-threshold",
			  &cpr_vreg->down_threshold, rc);
	if (rc)
		return rc;
	cpr_info(cpr_vreg, "up threshold = %u, down threshold = %u\n",
		cpr_vreg->up_threshold, cpr_vreg->down_threshold);

	CPR_PROP_READ_U32(cpr_vreg, of_node, "cpr-idle-clocks",
			  &cpr_vreg->idle_clocks, rc);
	if (rc)
		return rc;
	CPR_PROP_READ_U32(cpr_vreg, of_node, "cpr-gcnt-time",
			  &cpr_vreg->gcnt_time_us, rc);
	if (rc)
		return rc;
	CPR_PROP_READ_U32(cpr_vreg, of_node, "vdd-apc-step-up-limit",
			  &cpr_vreg->vdd_apc_step_up_limit, rc);
	if (rc)
		return rc;
	CPR_PROP_READ_U32(cpr_vreg, of_node, "vdd-apc-step-down-limit",
			  &cpr_vreg->vdd_apc_step_down_limit, rc);
	if (rc)
		return rc;

	rc = of_property_read_u32(of_node, "qcom,cpr-clamp-timer-interval",
				  &cpr_vreg->clamp_timer_interval);
	if (rc && rc != -EINVAL) {
		cpr_err(cpr_vreg,
			"error reading qcom,cpr-clamp-timer-interval, rc=%d\n",
			rc);
		return rc;
	}

	cpr_vreg->clamp_timer_interval = min(cpr_vreg->clamp_timer_interval,
					(u32)RBIF_TIMER_ADJ_CLAMP_INT_MASK);

	/* Init module parameter with the DT value */
	cpr_vreg->enable = of_property_read_bool(of_node, "qcom,cpr-enable");
	cpr_info(cpr_vreg, "CPR is %s by default.\n",
		cpr_vreg->enable ? "enabled" : "disabled");

	return 0;
}

static void cpr_pm_disable(struct cpr_regulator *cpr_vreg, bool disable)
{
	u32 reg_val;

	if (cpr_vreg->is_cpr_suspended)
		return;

	reg_val = cpr_read(cpr_vreg, REG_RBCPR_CTL);

	if (disable) {
		/* Proceed only if CPR is enabled */
		if (!(reg_val & RBCPR_CTL_LOOP_EN))
			return;
		cpr_ctl_disable(cpr_vreg);
		cpr_vreg->cpr_disabled_in_pc = true;
	} else {
		/* Proceed only if CPR was disabled in PM_ENTER */
		if (!cpr_vreg->cpr_disabled_in_pc)
			return;
		cpr_vreg->cpr_disabled_in_pc = false;
		cpr_ctl_enable(cpr_vreg, cpr_vreg->corner);
	}

	/* Make sure register write is complete */
	mb();
}

static int cpr_pm_callback(struct notifier_block *nb,
			    unsigned long action, void *data)
{
	struct cpr_regulator *cpr_vreg = container_of(nb,
			struct cpr_regulator, pm_notifier);

	if (action != CPU_PM_ENTER && action != CPU_PM_ENTER_FAILED &&
			action != CPU_PM_EXIT)
		return NOTIFY_OK;

	switch (action) {
	case CPU_PM_ENTER:
		cpr_pm_disable(cpr_vreg, true);
		break;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		cpr_pm_disable(cpr_vreg, false);
		break;
	}

	return NOTIFY_OK;
}

static int cpr_init_pm_notification(struct cpr_regulator *cpr_vreg)
{
	int rc;

	/* enabled only for single-core designs */
	if (cpr_vreg->num_adj_cpus != 1) {
		pr_warn("qcom,cpr-cpus not defined or invalid %d\n",
					cpr_vreg->num_adj_cpus);
		return 0;
	}

	cpr_vreg->pm_notifier.notifier_call = cpr_pm_callback;
	rc = cpu_pm_register_notifier(&cpr_vreg->pm_notifier);
	if (rc)
		cpr_err(cpr_vreg, "Unable to register pm notifier rc=%d\n", rc);

	return rc;
}

static int cpr_rpm_apc_init(struct platform_device *pdev,
			       struct cpr_regulator *cpr_vreg)
{
	int rc, len = 0;
	struct device_node *of_node = pdev->dev.of_node;

	if (!of_find_property(of_node, "rpm-apc-supply", NULL))
		return 0;

	cpr_vreg->rpm_apc_vreg = devm_regulator_get(&pdev->dev, "rpm-apc");
	if (IS_ERR_OR_NULL(cpr_vreg->rpm_apc_vreg)) {
		rc = PTR_ERR_OR_ZERO(cpr_vreg->rpm_apc_vreg);
		if (rc != -EPROBE_DEFER)
			cpr_err(cpr_vreg, "devm_regulator_get: rpm-apc: rc=%d\n",
					rc);
		return rc;
	}

	if (!of_find_property(of_node, "qcom,rpm-apc-corner-map", &len)) {
		cpr_err(cpr_vreg,
			"qcom,rpm-apc-corner-map missing:\n");
		return -EINVAL;
	}
	if (len != cpr_vreg->num_corners * sizeof(u32)) {
		cpr_err(cpr_vreg,
			"qcom,rpm-apc-corner-map length=%d is invalid: required:%d\n",
			len, cpr_vreg->num_corners);
		return -EINVAL;
	}

	cpr_vreg->rpm_apc_corner_map = devm_kzalloc(&pdev->dev,
		(cpr_vreg->num_corners + 1) *
		sizeof(*cpr_vreg->rpm_apc_corner_map), GFP_KERNEL);
	if (!cpr_vreg->rpm_apc_corner_map)
		return -ENOMEM;

	rc = of_property_read_u32_array(of_node, "qcom,rpm-apc-corner-map",
		&cpr_vreg->rpm_apc_corner_map[1], cpr_vreg->num_corners);
	if (rc)
		cpr_err(cpr_vreg, "read qcom,rpm-apc-corner-map failed, rc = %d\n",
				rc);

	return rc;
}

static int cpr_parse_vdd_mode_config(struct platform_device *pdev,
			       struct cpr_regulator *cpr_vreg)
{
	int rc, len = 0, i, mode;
	struct device_node *of_node = pdev->dev.of_node;
	const char *prop_str = "qcom,cpr-vdd-mode-map";

	if (!of_find_property(of_node, prop_str, &len))
		return 0;

	if (len != cpr_vreg->num_corners * sizeof(u32)) {
		cpr_err(cpr_vreg, "%s length=%d is invalid: required:%d\n",
			prop_str, len, cpr_vreg->num_corners);
		return -EINVAL;
	}

	cpr_vreg->vdd_mode_map = devm_kcalloc(&pdev->dev,
						cpr_vreg->num_corners + 1,
						sizeof(*cpr_vreg->vdd_mode_map),
						GFP_KERNEL);
	if (!cpr_vreg->vdd_mode_map)
		return -ENOMEM;

	for (i = 0; i < cpr_vreg->num_corners; i++) {
		rc = of_property_read_u32_index(of_node, prop_str, i, &mode);
		if (rc) {
			cpr_err(cpr_vreg, "read %s index %d failed, rc = %d\n",
				prop_str, i, rc);
			return rc;
		}
		cpr_vreg->vdd_mode_map[i + CPR_CORNER_MIN]
					= mode ? REGULATOR_MODE_NORMAL
						: REGULATOR_MODE_IDLE;
	}

	return rc;
}

static int cpr_vsens_init(struct platform_device *pdev,
			       struct cpr_regulator *cpr_vreg)
{
	int rc = 0, len = 0;
	struct device_node *of_node = pdev->dev.of_node;

	if (of_find_property(of_node, "vdd-vsens-voltage-supply", NULL)) {
		cpr_vreg->vdd_vsens_voltage = devm_regulator_get(&pdev->dev,
							"vdd-vsens-voltage");
		if (IS_ERR_OR_NULL(cpr_vreg->vdd_vsens_voltage)) {
			rc = PTR_ERR(cpr_vreg->vdd_vsens_voltage);
			cpr_vreg->vdd_vsens_voltage = NULL;
			if (rc == -EPROBE_DEFER)
				return rc;
			/* device not found */
			cpr_debug(cpr_vreg, "regulator_get: vdd-vsens-voltage: rc=%d\n",
					rc);
			return 0;
		}
	}

	if (of_find_property(of_node, "vdd-vsens-corner-supply", NULL)) {
		cpr_vreg->vdd_vsens_corner = devm_regulator_get(&pdev->dev,
							"vdd-vsens-corner");
		if (IS_ERR_OR_NULL(cpr_vreg->vdd_vsens_corner)) {
			rc = PTR_ERR(cpr_vreg->vdd_vsens_corner);
			cpr_vreg->vdd_vsens_corner = NULL;
			if (rc == -EPROBE_DEFER)
				return rc;
			/* device not found */
			cpr_debug(cpr_vreg, "regulator_get: vdd-vsens-corner: rc=%d\n",
					rc);
			return 0;
		}

		if (!of_find_property(of_node, "qcom,vsens-corner-map", &len)) {
			cpr_err(cpr_vreg, "qcom,vsens-corner-map missing\n");
			return -EINVAL;
		}

		if (len != cpr_vreg->num_fuse_corners * sizeof(u32)) {
			cpr_err(cpr_vreg, "qcom,vsens-corner-map length=%d is invalid: required:%d\n",
				len, cpr_vreg->num_fuse_corners);
			return -EINVAL;
		}

		cpr_vreg->vsens_corner_map = devm_kcalloc(&pdev->dev,
					(cpr_vreg->num_fuse_corners + 1),
			sizeof(*cpr_vreg->vsens_corner_map), GFP_KERNEL);
		if (!cpr_vreg->vsens_corner_map)
			return -ENOMEM;

		rc = of_property_read_u32_array(of_node,
					"qcom,vsens-corner-map",
					&cpr_vreg->vsens_corner_map[1],
					cpr_vreg->num_fuse_corners);
		if (rc)
			cpr_err(cpr_vreg, "read qcom,vsens-corner-map failed, rc = %d\n",
				rc);
	}

	return rc;
}

static int cpr_init_cpr(struct platform_device *pdev,
			       struct cpr_regulator *cpr_vreg)
{
	struct resource *res;
	int rc = 0;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rbcpr_clk");
	if (res && res->start)
		cpr_vreg->rbcpr_clk_addr = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rbcpr");
	if (!res || !res->start) {
		cpr_err(cpr_vreg, "missing rbcpr address: res=%p\n", res);
		return -EINVAL;
	}
	cpr_vreg->rbcpr_base = devm_ioremap(&pdev->dev, res->start,
					    resource_size(res));

	/* Init CPR configuration parameters */
	rc = cpr_init_cpr_parameters(pdev, cpr_vreg);
	if (rc)
		return rc;

	rc = cpr_init_cpr_efuse(pdev, cpr_vreg);
	if (rc)
		return rc;

	/* Load per corner ceiling and floor voltages if they exist. */
	rc = cpr_init_ceiling_floor_override_voltages(cpr_vreg, &pdev->dev);
	if (rc)
		return rc;

	/*
	 * Limit open loop voltages based upon per corner ceiling and floor
	 * voltages.
	 */
	rc = cpr_limit_open_loop_voltage(cpr_vreg);
	if (rc)
		return rc;

	/*
	 * Fill the OPP table for this device with virtual voltage corner to
	 * open-loop voltage pairs.
	 */
	rc = cpr_populate_opp_table(cpr_vreg, &pdev->dev);
	if (rc)
		return rc;

	/* Reduce the ceiling voltage if allowed. */
	rc = cpr_reduce_ceiling_voltage(cpr_vreg, &pdev->dev);
	if (rc)
		return rc;

	/* Load CPR floor to ceiling range if exist. */
	rc = cpr_init_floor_to_ceiling_range(cpr_vreg, &pdev->dev);
	if (rc)
		return rc;

	/* Init all voltage set points of APC regulator for CPR */
	rc = cpr_init_cpr_voltages(cpr_vreg, &pdev->dev);
	if (rc)
		return rc;

	/* Get and Init interrupt */
	cpr_vreg->cpr_irq = platform_get_irq(pdev, 0);
	if (!cpr_vreg->cpr_irq) {
		cpr_err(cpr_vreg, "missing CPR IRQ\n");
		return -EINVAL;
	}

	/* Configure CPR HW but keep it disabled */
	rc = cpr_config(cpr_vreg, &pdev->dev);
	if (rc)
		return rc;

	rc = request_threaded_irq(cpr_vreg->cpr_irq, NULL, cpr_irq_handler,
				  IRQF_ONESHOT | IRQF_TRIGGER_RISING, "cpr",
				  cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "CPR: request irq failed for IRQ %d\n",
				cpr_vreg->cpr_irq);
		return rc;
	}

	return 0;
}

/*
 * Create a set of virtual fuse rows if optional device tree properties are
 * present.
 */
static int cpr_remap_efuse_data(struct platform_device *pdev,
				 struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	struct property *prop;
	u64 fuse_param;
	u32 *temp;
	int size, rc, i, bits, in_row, in_bit, out_row, out_bit;

	prop = of_find_property(of_node, "qcom,fuse-remap-source", NULL);
	if (!prop) {
		/* No fuse remapping needed. */
		return 0;
	}

	size = prop->length / sizeof(u32);
	if (size == 0 || size % 4) {
		cpr_err(cpr_vreg, "qcom,fuse-remap-source has invalid size=%d\n",
			size);
		return -EINVAL;
	}
	size /= 4;

	rc = of_property_read_u32(of_node, "qcom,fuse-remap-base-row",
				&cpr_vreg->remapped_row_base);
	if (rc) {
		cpr_err(cpr_vreg, "could not read qcom,fuse-remap-base-row, rc=%d\n",
			rc);
		return rc;
	}

	temp = kzalloc(sizeof(*temp) * size * 4, GFP_KERNEL);
	if (!temp)
		return -ENOMEM;

	rc = of_property_read_u32_array(of_node, "qcom,fuse-remap-source", temp,
					size * 4);
	if (rc) {
		cpr_err(cpr_vreg, "could not read qcom,fuse-remap-source, rc=%d\n",
			rc);
		goto done;
	}

	/*
	 * Format of tuples in qcom,fuse-remap-source property:
	 * <row bit-offset bit-count fuse-read-method>
	 */
	for (i = 0, bits = 0; i < size; i++)
		bits += temp[i * 4 + 2];

	cpr_vreg->num_remapped_rows = DIV_ROUND_UP(bits, 64);
	cpr_vreg->remapped_row = devm_kzalloc(&pdev->dev,
		sizeof(*cpr_vreg->remapped_row) * cpr_vreg->num_remapped_rows,
		GFP_KERNEL);
	if (!cpr_vreg->remapped_row) {
		rc = -ENOMEM;
		goto done;
	}

	for (i = 0, out_row = 0, out_bit = 0; i < size; i++) {
		in_row = temp[i * 4];
		in_bit = temp[i * 4 + 1];
		bits = temp[i * 4 + 2];

		while (bits > 64) {
			fuse_param = cpr_read_efuse_param(cpr_vreg, in_row,
					in_bit, 64, temp[i * 4 + 3]);

			cpr_vreg->remapped_row[out_row++]
				|= fuse_param << out_bit;
			if (out_bit > 0)
				cpr_vreg->remapped_row[out_row]
					|= fuse_param >> (64 - out_bit);

			bits -= 64;
			in_bit += 64;
		}

		fuse_param = cpr_read_efuse_param(cpr_vreg, in_row, in_bit,
						bits, temp[i * 4 + 3]);

		cpr_vreg->remapped_row[out_row] |= fuse_param << out_bit;
		if (bits < 64 - out_bit) {
			out_bit += bits;
		} else {
			out_row++;
			if (out_bit > 0)
				cpr_vreg->remapped_row[out_row]
					|= fuse_param >> (64 - out_bit);
			out_bit = bits - (64 - out_bit);
		}
	}

done:
	kfree(temp);
	return rc;
}

static int cpr_efuse_init(struct platform_device *pdev,
				 struct cpr_regulator *cpr_vreg)
{
	struct resource *res;
	int len;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "efuse_addr");
	if (!res || !res->start) {
		cpr_err(cpr_vreg, "efuse_addr missing: res=%p\n", res);
		return -EINVAL;
	}

	cpr_vreg->efuse_addr = res->start;
	len = resource_size(res);

	cpr_info(cpr_vreg, "efuse_addr = %pa (len=0x%x)\n", &res->start, len);

	cpr_vreg->efuse_base = ioremap(cpr_vreg->efuse_addr, len);
	if (!cpr_vreg->efuse_base) {
		cpr_err(cpr_vreg, "Unable to map efuse_addr %pa\n",
				&cpr_vreg->efuse_addr);
		return -EINVAL;
	}

	return 0;
}

static void cpr_efuse_free(struct cpr_regulator *cpr_vreg)
{
	iounmap(cpr_vreg->efuse_base);
}

static void cpr_parse_cond_min_volt_fuse(struct cpr_regulator *cpr_vreg,
						struct device_node *of_node)
{
	int rc;
	u32 fuse_sel[5];
	/*
	 * Restrict all pvs corner voltages to a minimum value of
	 * qcom,cpr-cond-min-voltage if the fuse defined in
	 * qcom,cpr-fuse-cond-min-volt-sel does not read back with
	 * the expected value.
	 */
	rc = of_property_read_u32_array(of_node,
			"qcom,cpr-fuse-cond-min-volt-sel", fuse_sel, 5);
	if (!rc) {
		if (!cpr_fuse_is_setting_expected(cpr_vreg, fuse_sel))
			cpr_vreg->flags |= FLAGS_SET_MIN_VOLTAGE;
	}
}

static void cpr_parse_speed_bin_fuse(struct cpr_regulator *cpr_vreg,
				struct device_node *of_node)
{
	int rc;
	u64 fuse_bits;
	u32 fuse_sel[4];
	u32 speed_bits;

	rc = of_property_read_u32_array(of_node,
			"qcom,speed-bin-fuse-sel", fuse_sel, 4);

	if (!rc) {
		fuse_bits = cpr_read_efuse_row(cpr_vreg,
				fuse_sel[0], fuse_sel[3]);
		speed_bits = (fuse_bits >> fuse_sel[1]) &
			((1 << fuse_sel[2]) - 1);
		cpr_info(cpr_vreg, "[row: %d]: 0x%llx, speed_bits = %d\n",
				fuse_sel[0], fuse_bits, speed_bits);
		cpr_vreg->speed_bin = speed_bits;
	} else {
		cpr_vreg->speed_bin = SPEED_BIN_NONE;
	}
}

static int cpr_voltage_uplift_enable_check(struct cpr_regulator *cpr_vreg,
					struct device_node *of_node)
{
	int rc;
	u32 fuse_sel[5];
	u32 uplift_speed_bin;

	rc = of_property_read_u32_array(of_node,
			"qcom,cpr-fuse-uplift-sel", fuse_sel, 5);
	if (!rc) {
		rc = of_property_read_u32(of_node,
				"qcom,cpr-uplift-speed-bin",
				&uplift_speed_bin);
		if (rc < 0) {
			cpr_err(cpr_vreg,
				"qcom,cpr-uplift-speed-bin missing\n");
			return rc;
		}
		if (cpr_fuse_is_setting_expected(cpr_vreg, fuse_sel)
			&& (uplift_speed_bin == cpr_vreg->speed_bin)
			&& !(cpr_vreg->flags & FLAGS_SET_MIN_VOLTAGE)) {
			cpr_vreg->flags |= FLAGS_UPLIFT_QUOT_VOLT;
		}
	}
	return 0;
}

/*
 * Read in the number of fuse corners and then allocate memory for arrays that
 * are sized based upon the number of fuse corners.
 */
static int cpr_fuse_corner_array_alloc(struct device *dev,
					struct cpr_regulator *cpr_vreg)
{
	int rc;
	size_t len;

	rc = of_property_read_u32(dev->of_node, "qcom,cpr-fuse-corners",
				&cpr_vreg->num_fuse_corners);
	if (rc < 0) {
		cpr_err(cpr_vreg, "qcom,cpr-fuse-corners missing: rc=%d\n", rc);
		return rc;
	}

	if (cpr_vreg->num_fuse_corners < CPR_FUSE_CORNER_MIN
	    || cpr_vreg->num_fuse_corners > CPR_FUSE_CORNER_LIMIT) {
		cpr_err(cpr_vreg, "corner count=%d is invalid\n",
			cpr_vreg->num_fuse_corners);
		return -EINVAL;
	}

	/*
	 * The arrays sized based on the fuse corner count ignore element 0
	 * in order to simplify indexing throughout the driver since min_uV = 0
	 * cannot be passed into a set_voltage() callback.
	 */
	len = cpr_vreg->num_fuse_corners + 1;

	cpr_vreg->pvs_corner_v = devm_kzalloc(dev,
			len * sizeof(*cpr_vreg->pvs_corner_v), GFP_KERNEL);
	cpr_vreg->cpr_fuse_target_quot = devm_kzalloc(dev,
		len * sizeof(*cpr_vreg->cpr_fuse_target_quot), GFP_KERNEL);
	cpr_vreg->cpr_fuse_ro_sel = devm_kzalloc(dev,
			len * sizeof(*cpr_vreg->cpr_fuse_ro_sel), GFP_KERNEL);
	cpr_vreg->fuse_ceiling_volt = devm_kzalloc(dev,
		len * (sizeof(*cpr_vreg->fuse_ceiling_volt)), GFP_KERNEL);
	cpr_vreg->fuse_floor_volt = devm_kzalloc(dev,
		len * (sizeof(*cpr_vreg->fuse_floor_volt)), GFP_KERNEL);
	cpr_vreg->step_quotient = devm_kzalloc(dev,
		len * sizeof(*cpr_vreg->step_quotient), GFP_KERNEL);

	if (cpr_vreg->pvs_corner_v == NULL || cpr_vreg->cpr_fuse_ro_sel == NULL
	    || cpr_vreg->fuse_ceiling_volt == NULL
	    || cpr_vreg->fuse_floor_volt == NULL
	    || cpr_vreg->cpr_fuse_target_quot == NULL
	    || cpr_vreg->step_quotient == NULL)
		return -ENOMEM;

	return 0;
}

static int cpr_voltage_plan_init(struct platform_device *pdev,
					struct cpr_regulator *cpr_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	int rc, i;
	u32 min_uv = 0;

	rc = of_property_read_u32_array(of_node, "qcom,cpr-voltage-ceiling",
		&cpr_vreg->fuse_ceiling_volt[CPR_FUSE_CORNER_MIN],
		cpr_vreg->num_fuse_corners);
	if (rc < 0) {
		cpr_err(cpr_vreg, "cpr-voltage-ceiling missing: rc=%d\n", rc);
		return rc;
	}

	rc = of_property_read_u32_array(of_node, "qcom,cpr-voltage-floor",
		&cpr_vreg->fuse_floor_volt[CPR_FUSE_CORNER_MIN],
		cpr_vreg->num_fuse_corners);
	if (rc < 0) {
		cpr_err(cpr_vreg, "cpr-voltage-floor missing: rc=%d\n", rc);
		return rc;
	}

	cpr_parse_cond_min_volt_fuse(cpr_vreg, of_node);
	rc = cpr_voltage_uplift_enable_check(cpr_vreg, of_node);
	if (rc < 0) {
		cpr_err(cpr_vreg, "voltage uplift enable check failed, %d\n",
			rc);
		return rc;
	}
	if (cpr_vreg->flags & FLAGS_SET_MIN_VOLTAGE) {
		of_property_read_u32(of_node, "qcom,cpr-cond-min-voltage",
					&min_uv);
		for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners;
		     i++)
			if (cpr_vreg->fuse_ceiling_volt[i] < min_uv) {
				cpr_vreg->fuse_ceiling_volt[i] = min_uv;
				cpr_vreg->fuse_floor_volt[i] = min_uv;
			} else if (cpr_vreg->fuse_floor_volt[i] < min_uv) {
				cpr_vreg->fuse_floor_volt[i] = min_uv;
			}
	}

	return 0;
}

static int cpr_mem_acc_init(struct platform_device *pdev,
				struct cpr_regulator *cpr_vreg)
{
	int rc, size;
	struct property *prop;
	char *corner_map_str;

	if (of_find_property(pdev->dev.of_node, "mem-acc-supply", NULL)) {
		cpr_vreg->mem_acc_vreg = devm_regulator_get(&pdev->dev,
							"mem-acc");
		if (IS_ERR_OR_NULL(cpr_vreg->mem_acc_vreg)) {
			rc = PTR_ERR_OR_ZERO(cpr_vreg->mem_acc_vreg);
			if (rc != -EPROBE_DEFER)
				cpr_err(cpr_vreg,
					"devm_regulator_get: mem-acc: rc=%d\n",
					rc);
			return rc;
		}
	}

	corner_map_str = "qcom,mem-acc-corner-map";
	prop = of_find_property(pdev->dev.of_node, corner_map_str, NULL);
	if (!prop) {
		corner_map_str = "qcom,cpr-corner-map";
		prop = of_find_property(pdev->dev.of_node, corner_map_str,
					NULL);
		if (!prop) {
			cpr_err(cpr_vreg, "qcom,cpr-corner-map missing\n");
			return -EINVAL;
		}
	}

	size = prop->length / sizeof(u32);
	cpr_vreg->mem_acc_corner_map = devm_kzalloc(&pdev->dev,
					sizeof(int) * (size + 1),
					GFP_KERNEL);

	rc = of_property_read_u32_array(pdev->dev.of_node, corner_map_str,
			&cpr_vreg->mem_acc_corner_map[CPR_FUSE_CORNER_MIN],
			size);
	if (rc) {
		cpr_err(cpr_vreg, "%s missing, rc = %d\n", corner_map_str, rc);
		return rc;
	}

	return 0;
}

#if defined(CONFIG_DEBUG_FS)

static int cpr_enable_set(void *data, u64 val)
{
	struct cpr_regulator *cpr_vreg = data;
	bool old_cpr_enable;

	mutex_lock(&cpr_vreg->cpr_mutex);

	old_cpr_enable = cpr_vreg->enable;
	cpr_vreg->enable = val;

	if (old_cpr_enable == cpr_vreg->enable)
		goto _exit;

	if (cpr_vreg->enable && cpr_vreg->cpr_fuse_disable) {
		cpr_info(cpr_vreg,
			"CPR permanently disabled due to fuse values\n");
		cpr_vreg->enable = false;
		goto _exit;
	}

	cpr_debug(cpr_vreg, "%s CPR [corner=%d, fuse_corner=%d]\n",
		cpr_vreg->enable ? "enabling" : "disabling",
		cpr_vreg->corner, cpr_vreg->corner_map[cpr_vreg->corner]);

	if (cpr_vreg->corner) {
		if (cpr_vreg->enable) {
			cpr_ctl_disable(cpr_vreg);
			cpr_irq_clr(cpr_vreg);
			cpr_corner_restore(cpr_vreg, cpr_vreg->corner);
			cpr_ctl_enable(cpr_vreg, cpr_vreg->corner);
		} else {
			cpr_ctl_disable(cpr_vreg);
			cpr_irq_set(cpr_vreg, 0);
		}
	}

_exit:
	mutex_unlock(&cpr_vreg->cpr_mutex);

	return 0;
}

static int cpr_enable_get(void *data, u64 *val)
{
	struct cpr_regulator *cpr_vreg = data;

	*val = cpr_vreg->enable;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(cpr_enable_fops, cpr_enable_get, cpr_enable_set,
			"%llu\n");

static int cpr_get_cpr_ceiling(void *data, u64 *val)
{
	struct cpr_regulator *cpr_vreg = data;

	*val = cpr_vreg->ceiling_volt[cpr_vreg->corner];

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(cpr_ceiling_fops, cpr_get_cpr_ceiling, NULL,
			"%llu\n");

static int cpr_get_cpr_floor(void *data, u64 *val)
{
	struct cpr_regulator *cpr_vreg = data;

	*val = cpr_vreg->floor_volt[cpr_vreg->corner];

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(cpr_floor_fops, cpr_get_cpr_floor, NULL,
			"%llu\n");

static int cpr_get_cpr_max_ceiling(void *data, u64 *val)
{
	struct cpr_regulator *cpr_vreg = data;

	*val = cpr_vreg->cpr_max_ceiling[cpr_vreg->corner];

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(cpr_max_ceiling_fops, cpr_get_cpr_max_ceiling, NULL,
			"%llu\n");

static ssize_t cpr_debug_info_read(struct file *file, char __user *buff,
				size_t count, loff_t *ppos)
{
	struct cpr_regulator *cpr_vreg = file->private_data;
	char *debugfs_buf;
	ssize_t len, ret = 0;
	u32 gcnt, ro_sel, ctl, irq_status, reg, error_steps;
	u32 step_dn, step_up, error, error_lt0, busy;
	int fuse_corner;

	debugfs_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!debugfs_buf)
		return -ENOMEM;

	mutex_lock(&cpr_vreg->cpr_mutex);

	fuse_corner = cpr_vreg->corner_map[cpr_vreg->corner];

	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
		"corner = %d, current_volt = %d uV\n",
		cpr_vreg->corner, cpr_vreg->last_volt[cpr_vreg->corner]);
	ret += len;

	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			"fuse_corner = %d, current_volt = %d uV\n",
			fuse_corner, cpr_vreg->last_volt[cpr_vreg->corner]);
	ret += len;

	ro_sel = cpr_vreg->cpr_fuse_ro_sel[fuse_corner];
	gcnt = cpr_read(cpr_vreg, REG_RBCPR_GCNT_TARGET(ro_sel));
	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			"rbcpr_gcnt_target (%u) = 0x%02X\n", ro_sel, gcnt);
	ret += len;

	ctl = cpr_read(cpr_vreg, REG_RBCPR_CTL);
	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			"rbcpr_ctl = 0x%02X\n", ctl);
	ret += len;

	irq_status = cpr_read(cpr_vreg, REG_RBIF_IRQ_STATUS);
	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			"rbcpr_irq_status = 0x%02X\n", irq_status);
	ret += len;

	reg = cpr_read(cpr_vreg, REG_RBCPR_RESULT_0);
	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			"rbcpr_result_0 = 0x%02X\n", reg);
	ret += len;

	step_dn = reg & 0x01;
	step_up = (reg >> RBCPR_RESULT0_STEP_UP_SHIFT) & 0x01;
	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			"  [step_dn = %u", step_dn);
	ret += len;

	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			", step_up = %u", step_up);
	ret += len;

	error_steps = (reg >> RBCPR_RESULT0_ERROR_STEPS_SHIFT)
				& RBCPR_RESULT0_ERROR_STEPS_MASK;
	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			", error_steps = %u", error_steps);
	ret += len;

	error = (reg >> RBCPR_RESULT0_ERROR_SHIFT) & RBCPR_RESULT0_ERROR_MASK;
	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			", error = %u", error);
	ret += len;

	error_lt0 = (reg >> RBCPR_RESULT0_ERROR_LT0_SHIFT) & 0x01;
	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			", error_lt_0 = %u", error_lt0);
	ret += len;

	busy = (reg >> RBCPR_RESULT0_BUSY_SHIFT) & 0x01;
	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			", busy = %u]\n", busy);
	ret += len;
	mutex_unlock(&cpr_vreg->cpr_mutex);

	ret = simple_read_from_buffer(buff, count, ppos, debugfs_buf, ret);
	kfree(debugfs_buf);
	return ret;
}

static const struct file_operations cpr_debug_info_fops = {
	.open = simple_open,
	.read = cpr_debug_info_read,
};

static ssize_t cpr_aging_debug_info_read(struct file *file, char __user *buff,
				size_t count, loff_t *ppos)
{
	struct cpr_regulator *cpr_vreg = file->private_data;
	struct cpr_aging_info *aging_info = cpr_vreg->aging_info;
	char *debugfs_buf;
	ssize_t len, ret = 0;
	int i;

	debugfs_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!debugfs_buf)
		return -ENOMEM;

	mutex_lock(&cpr_vreg->cpr_mutex);

	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			"aging_adj_volt = [");
	ret += len;

	for (i = CPR_FUSE_CORNER_MIN; i <= cpr_vreg->num_fuse_corners; i++) {
		len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
				" %d", aging_info->voltage_adjust[i]);
		ret += len;
	}

	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			" ]uV\n");
	ret += len;

	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			"aging_measurement_done = %s\n",
			aging_info->cpr_aging_done ? "true" : "false");
	ret += len;

	len = scnprintf(debugfs_buf + ret, PAGE_SIZE - ret,
			"aging_measurement_error = %s\n",
			aging_info->cpr_aging_error ? "true" : "false");
	ret += len;

	mutex_unlock(&cpr_vreg->cpr_mutex);

	ret = simple_read_from_buffer(buff, count, ppos, debugfs_buf, ret);
	kfree(debugfs_buf);
	return ret;
}

static const struct file_operations cpr_aging_debug_info_fops = {
	.open = simple_open,
	.read = cpr_aging_debug_info_read,
};

static void cpr_debugfs_init(struct cpr_regulator *cpr_vreg)
{
	struct dentry *temp;

	if (IS_ERR_OR_NULL(cpr_debugfs_base)) {
		cpr_err(cpr_vreg, "Could not create debugfs nodes since base directory is missing\n");
		return;
	}

	cpr_vreg->debugfs = debugfs_create_dir(cpr_vreg->rdesc.name,
						cpr_debugfs_base);
	if (IS_ERR_OR_NULL(cpr_vreg->debugfs)) {
		cpr_err(cpr_vreg, "debugfs directory creation failed\n");
		return;
	}

	temp = debugfs_create_file("debug_info", 0444, cpr_vreg->debugfs,
					cpr_vreg, &cpr_debug_info_fops);
	if (IS_ERR_OR_NULL(temp)) {
		cpr_err(cpr_vreg, "debug_info node creation failed\n");
		return;
	}

	temp = debugfs_create_file("cpr_enable", 0644, cpr_vreg->debugfs,
					cpr_vreg, &cpr_enable_fops);
	if (IS_ERR_OR_NULL(temp)) {
		cpr_err(cpr_vreg, "cpr_enable node creation failed\n");
		return;
	}

	temp = debugfs_create_file("cpr_ceiling", 0444, cpr_vreg->debugfs,
					cpr_vreg, &cpr_ceiling_fops);
	if (IS_ERR_OR_NULL(temp)) {
		cpr_err(cpr_vreg, "cpr_ceiling node creation failed\n");
		return;
	}

	temp = debugfs_create_file("cpr_floor", 0444, cpr_vreg->debugfs,
					cpr_vreg, &cpr_floor_fops);
	if (IS_ERR_OR_NULL(temp)) {
		cpr_err(cpr_vreg, "cpr_floor node creation failed\n");
		return;
	}

	temp = debugfs_create_file("cpr_max_ceiling", 0444, cpr_vreg->debugfs,
					cpr_vreg, &cpr_max_ceiling_fops);
	if (IS_ERR_OR_NULL(temp)) {
		cpr_err(cpr_vreg, "cpr_max_ceiling node creation failed\n");
		return;
	}

	if (cpr_vreg->aging_info) {
		temp = debugfs_create_file("aging_debug_info", 0444,
					cpr_vreg->debugfs, cpr_vreg,
					&cpr_aging_debug_info_fops);
		if (IS_ERR_OR_NULL(temp)) {
			cpr_err(cpr_vreg, "aging_debug_info node creation failed\n");
			return;
		}
	}

	debugfs_create_u32("cpr_debug_enable", 0644, cpr_vreg->debugfs,
					&cpr_debug_enable);
}

static void cpr_debugfs_remove(struct cpr_regulator *cpr_vreg)
{
	debugfs_remove_recursive(cpr_vreg->debugfs);
}

static void cpr_debugfs_base_init(void)
{
	cpr_debugfs_base = debugfs_create_dir("cpr-regulator", NULL);
	if (IS_ERR_OR_NULL(cpr_debugfs_base))
		pr_err("cpr-regulator debugfs base directory creation failed\n");
}

static void cpr_debugfs_base_remove(void)
{
	debugfs_remove_recursive(cpr_debugfs_base);
}

#else

static void cpr_debugfs_init(struct cpr_regulator *cpr_vreg)
{}

static void cpr_debugfs_remove(struct cpr_regulator *cpr_vreg)
{}

static void cpr_debugfs_base_init(void)
{}

static void cpr_debugfs_base_remove(void)
{}

#endif

/**
 * cpr_panic_callback() - panic notification callback function. This function
 *		is invoked when a kernel panic occurs.
 * @nfb:	Notifier block pointer of CPR regulator
 * @event:	Value passed unmodified to notifier function
 * @data:	Pointer passed unmodified to notifier function
 *
 * Return: NOTIFY_OK
 */
static int cpr_panic_callback(struct notifier_block *nfb,
			unsigned long event, void *data)
{
	struct cpr_regulator *cpr_vreg = container_of(nfb,
				struct cpr_regulator, panic_notifier);
	int corner, fuse_corner, volt;

	corner = cpr_vreg->corner;
	fuse_corner = cpr_vreg->corner_map[corner];
	if (cpr_is_allowed(cpr_vreg))
		volt = cpr_vreg->last_volt[corner];
	else
		volt = cpr_vreg->open_loop_volt[corner];

	cpr_err(cpr_vreg, "[corner:%d, fuse_corner:%d] = %d uV\n",
		corner, fuse_corner, volt);

	return NOTIFY_OK;
}

static int cpr_regulator_probe(struct platform_device *pdev)
{
	struct regulator_config reg_config = {};
	struct cpr_regulator *cpr_vreg;
	struct regulator_desc *rdesc;
	struct device *dev = &pdev->dev;
	struct regulator_init_data *init_data = pdev->dev.platform_data;
	int rc;

	if (!pdev->dev.of_node) {
		dev_err(dev, "Device tree node is missing\n");
		return -EINVAL;
	}

	cpr_vreg = devm_kzalloc(&pdev->dev, sizeof(struct cpr_regulator),
				GFP_KERNEL);
	if (!cpr_vreg)
		return -ENOMEM;

	init_data = of_get_regulator_init_data(&pdev->dev, pdev->dev.of_node,
						&cpr_vreg->rdesc);
	if (!init_data) {
		dev_err(dev, "regulator init data is missing\n");
		return -EINVAL;
	}
	init_data->constraints.input_uV = init_data->constraints.max_uV;
	init_data->constraints.valid_ops_mask
		|= REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS;

	cpr_vreg->rdesc.name = init_data->constraints.name;
	if (cpr_vreg->rdesc.name == NULL) {
		dev_err(dev, "regulator-name missing\n");
		return -EINVAL;
	}

	rc = cpr_fuse_corner_array_alloc(&pdev->dev, cpr_vreg);
	if (rc)
		return rc;

	rc = cpr_mem_acc_init(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "mem_acc initialization error rc=%d\n", rc);
		return rc;
	}

	rc = cpr_efuse_init(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "Wrong eFuse address specified: rc=%d\n", rc);
		return rc;
	}

	rc = cpr_remap_efuse_data(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "Could not remap fuse data: rc=%d\n", rc);
		return rc;
	}

	rc = cpr_check_redundant(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "Could not check redundant fuse: rc=%d\n",
			rc);
		goto err_out;
	}

	rc = cpr_read_fuse_revision(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "Could not read fuse revision: rc=%d\n", rc);
		goto err_out;
	}

	cpr_parse_speed_bin_fuse(cpr_vreg, dev->of_node);
	cpr_parse_pvs_version_fuse(cpr_vreg, dev->of_node);

	rc = cpr_read_ro_select(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "Could not read RO select: rc=%d\n", rc);
		goto err_out;
	}

	rc = cpr_find_fuse_map_match(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "Could not determine fuse mapping match: rc=%d\n",
			rc);
		goto err_out;
	}

	rc = cpr_voltage_plan_init(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "Wrong DT parameter specified: rc=%d\n", rc);
		goto err_out;
	}

	rc = cpr_pvs_init(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "Initialize PVS wrong: rc=%d\n", rc);
		goto err_out;
	}

	rc = cpr_vsens_init(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "Initialize vsens configuration failed rc=%d\n",
			rc);
		return rc;
	}

	rc = cpr_apc_init(pdev, cpr_vreg);
	if (rc) {
		if (rc != -EPROBE_DEFER)
			cpr_err(cpr_vreg, "Initialize APC wrong: rc=%d\n", rc);
		goto err_out;
	}

	rc = cpr_init_cpr(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "Initialize CPR failed: rc=%d\n", rc);
		goto err_out;
	}

	rc = cpr_rpm_apc_init(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "Initialize RPM APC regulator failed rc=%d\n",
			rc);
		return rc;
	}

	rc = cpr_parse_vdd_mode_config(pdev, cpr_vreg);
	if (rc) {
		cpr_err(cpr_vreg, "vdd-mode parsing failed, rc=%d\n", rc);
		return rc;
	}

	if (of_property_read_bool(pdev->dev.of_node,
				"qcom,disable-closed-loop-in-pc")) {
		rc = cpr_init_pm_notification(cpr_vreg);
		if (rc) {
			cpr_err(cpr_vreg,
				"cpr_init_pm_notification failed rc=%d\n", rc);
			return rc;
		}
	}

	/* Parse dependency parameters */
	if (cpr_vreg->vdd_mx) {
		rc = cpr_parse_vdd_mx_parameters(pdev, cpr_vreg);
		if (rc) {
			cpr_err(cpr_vreg, "parsing vdd_mx parameters failed: rc=%d\n",
				rc);
			goto err_out;
		}
	}

	cpr_efuse_free(cpr_vreg);

	/*
	 * Ensure that enable state accurately reflects the case in which CPR
	 * is permanently disabled.
	 */
	cpr_vreg->enable &= !cpr_vreg->cpr_fuse_disable;

	mutex_init(&cpr_vreg->cpr_mutex);

	rdesc			= &cpr_vreg->rdesc;
	rdesc->owner		= THIS_MODULE;
	rdesc->type		= REGULATOR_VOLTAGE;
	rdesc->ops		= &cpr_corner_ops;

	reg_config.dev = &pdev->dev;
	reg_config.init_data = init_data;
	reg_config.driver_data = cpr_vreg;
	reg_config.of_node = pdev->dev.of_node;
	cpr_vreg->rdev = regulator_register(&pdev->dev, rdesc, &reg_config);
	if (IS_ERR(cpr_vreg->rdev)) {
		rc = PTR_ERR(cpr_vreg->rdev);
		cpr_err(cpr_vreg, "regulator_register failed: rc=%d\n", rc);

		cpr_apc_exit(cpr_vreg);
		return rc;
	}

	platform_set_drvdata(pdev, cpr_vreg);
	cpr_debugfs_init(cpr_vreg);

	/* Register panic notification call back */
	cpr_vreg->panic_notifier.notifier_call = cpr_panic_callback;
	atomic_notifier_chain_register(&panic_notifier_list,
			&cpr_vreg->panic_notifier);

	mutex_lock(&cpr_regulator_list_mutex);
	list_add(&cpr_vreg->list, &cpr_regulator_list);
	mutex_unlock(&cpr_regulator_list_mutex);

	rc = devm_regulator_debug_register(&pdev->dev, cpr_vreg->rdev);
	if (rc)
		cpr_err(cpr_vreg, "Error registering CPR debug regulator, rc=%d\n", rc);

	return 0;

err_out:
	cpr_efuse_free(cpr_vreg);
	return rc;
}

static int cpr_regulator_remove(struct platform_device *pdev)
{
	struct cpr_regulator *cpr_vreg;

	cpr_vreg = platform_get_drvdata(pdev);
	if (cpr_vreg) {
		/* Disable CPR */
		if (cpr_is_allowed(cpr_vreg)) {
			cpr_ctl_disable(cpr_vreg);
			cpr_irq_set(cpr_vreg, 0);
		}

		mutex_lock(&cpr_regulator_list_mutex);
		list_del(&cpr_vreg->list);
		mutex_unlock(&cpr_regulator_list_mutex);

		atomic_notifier_chain_unregister(&panic_notifier_list,
			&cpr_vreg->panic_notifier);

		cpr_apc_exit(cpr_vreg);
		cpr_debugfs_remove(cpr_vreg);
		regulator_unregister(cpr_vreg->rdev);
	}

	return 0;
}

static const struct of_device_id cpr_regulator_match_table[] = {
	{ .compatible = CPR_REGULATOR_DRIVER_NAME, },
	{}
};
MODULE_DEVICE_TABLE(of, cpr_regulator_match_table);

static struct platform_driver cpr_regulator_driver = {
	.driver		= {
		.name	= CPR_REGULATOR_DRIVER_NAME,
		.of_match_table = cpr_regulator_match_table,
	},
	.probe		= cpr_regulator_probe,
	.remove		= cpr_regulator_remove,
	.suspend	= cpr_regulator_suspend,
	.resume		= cpr_regulator_resume,
};

/**
 * cpr_regulator_init() - register cpr-regulator driver
 *
 * This initialization function should be called in systems in which driver
 * registration ordering must be controlled precisely.
 */
int __init cpr_regulator_init(void)
{
	static bool initialized;

	if (initialized)
		return 0;

	initialized = true;
	cpr_debugfs_base_init();
	return platform_driver_register(&cpr_regulator_driver);
}

static void __exit cpr_regulator_exit(void)
{
	platform_driver_unregister(&cpr_regulator_driver);
	cpr_debugfs_base_remove();
}

MODULE_DESCRIPTION("CPR regulator driver");
MODULE_LICENSE("GPL");

arch_initcall(cpr_regulator_init);
module_exit(cpr_regulator_exit);

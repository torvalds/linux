// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Balsam CHIHI <bchihi@baylibre.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/thermal.h>
#include <dt-bindings/thermal/mediatek,lvts-thermal.h>

#include "../thermal_hwmon.h"

#define LVTS_MONCTL0(__base)	(__base + 0x0000)
#define LVTS_MONCTL1(__base)	(__base + 0x0004)
#define LVTS_MONCTL2(__base)	(__base + 0x0008)
#define LVTS_MONINT(__base)		(__base + 0x000C)
#define LVTS_MONINTSTS(__base)	(__base + 0x0010)
#define LVTS_MONIDET0(__base)	(__base + 0x0014)
#define LVTS_MONIDET1(__base)	(__base + 0x0018)
#define LVTS_MONIDET2(__base)	(__base + 0x001C)
#define LVTS_MONIDET3(__base)	(__base + 0x0020)
#define LVTS_H2NTHRE(__base)	(__base + 0x0024)
#define LVTS_HTHRE(__base)		(__base + 0x0028)
#define LVTS_OFFSETH(__base)	(__base + 0x0030)
#define LVTS_OFFSETL(__base)	(__base + 0x0034)
#define LVTS_MSRCTL0(__base)	(__base + 0x0038)
#define LVTS_MSRCTL1(__base)	(__base + 0x003C)
#define LVTS_TSSEL(__base)		(__base + 0x0040)
#define LVTS_CALSCALE(__base)	(__base + 0x0048)
#define LVTS_ID(__base)			(__base + 0x004C)
#define LVTS_CONFIG(__base)		(__base + 0x0050)
#define LVTS_EDATA00(__base)	(__base + 0x0054)
#define LVTS_EDATA01(__base)	(__base + 0x0058)
#define LVTS_EDATA02(__base)	(__base + 0x005C)
#define LVTS_EDATA03(__base)	(__base + 0x0060)
#define LVTS_MSR0(__base)		(__base + 0x0090)
#define LVTS_MSR1(__base)		(__base + 0x0094)
#define LVTS_MSR2(__base)		(__base + 0x0098)
#define LVTS_MSR3(__base)		(__base + 0x009C)
#define LVTS_IMMD0(__base)		(__base + 0x00A0)
#define LVTS_IMMD1(__base)		(__base + 0x00A4)
#define LVTS_IMMD2(__base)		(__base + 0x00A8)
#define LVTS_IMMD3(__base)		(__base + 0x00AC)
#define LVTS_PROTCTL(__base)	(__base + 0x00C0)
#define LVTS_PROTTA(__base)		(__base + 0x00C4)
#define LVTS_PROTTB(__base)		(__base + 0x00C8)
#define LVTS_PROTTC(__base)		(__base + 0x00CC)
#define LVTS_CLKEN(__base)		(__base + 0x00E4)

#define LVTS_PERIOD_UNIT			0
#define LVTS_GROUP_INTERVAL			0
#define LVTS_FILTER_INTERVAL		0
#define LVTS_SENSOR_INTERVAL		0
#define LVTS_HW_FILTER				0x0
#define LVTS_TSSEL_CONF				0x13121110
#define LVTS_CALSCALE_CONF			0x300
#define LVTS_MONINT_CONF			0x8300318C

#define LVTS_MONINT_OFFSET_SENSOR0		0xC
#define LVTS_MONINT_OFFSET_SENSOR1		0x180
#define LVTS_MONINT_OFFSET_SENSOR2		0x3000
#define LVTS_MONINT_OFFSET_SENSOR3		0x3000000

#define LVTS_INT_SENSOR0			0x0009001F
#define LVTS_INT_SENSOR1			0x001203E0
#define LVTS_INT_SENSOR2			0x00247C00
#define LVTS_INT_SENSOR3			0x1FC00000

#define LVTS_SENSOR_MAX				4
#define LVTS_GOLDEN_TEMP_MAX		62
#define LVTS_GOLDEN_TEMP_DEFAULT	50
#define LVTS_COEFF_A_MT8195			-250460
#define LVTS_COEFF_B_MT8195			250460
#define LVTS_COEFF_A_MT7988			-204650
#define LVTS_COEFF_B_MT7988			204650

#define LVTS_MSR_IMMEDIATE_MODE		0
#define LVTS_MSR_FILTERED_MODE		1

#define LVTS_MSR_READ_TIMEOUT_US	400
#define LVTS_MSR_READ_WAIT_US		(LVTS_MSR_READ_TIMEOUT_US / 2)

#define LVTS_HW_TSHUT_TEMP		105000

#define LVTS_MINIMUM_THRESHOLD		20000

static int golden_temp = LVTS_GOLDEN_TEMP_DEFAULT;
static int golden_temp_offset;

struct lvts_sensor_data {
	int dt_id;
	u8 cal_offsets[3];
};

struct lvts_ctrl_data {
	struct lvts_sensor_data lvts_sensor[LVTS_SENSOR_MAX];
	u8 valid_sensor_mask;
	int offset;
	int mode;
};

#define VALID_SENSOR_MAP(s0, s1, s2, s3) \
	.valid_sensor_mask = (((s0) ? BIT(0) : 0) | \
			      ((s1) ? BIT(1) : 0) | \
			      ((s2) ? BIT(2) : 0) | \
			      ((s3) ? BIT(3) : 0))

#define lvts_for_each_valid_sensor(i, lvts_ctrl) \
	for ((i) = 0; (i) < LVTS_SENSOR_MAX; (i)++) \
		if (!((lvts_ctrl)->valid_sensor_mask & BIT(i))) \
			continue; \
		else

struct lvts_data {
	const struct lvts_ctrl_data *lvts_ctrl;
	int num_lvts_ctrl;
	int temp_factor;
	int temp_offset;
	int gt_calib_bit_offset;
};

struct lvts_sensor {
	struct thermal_zone_device *tz;
	void __iomem *msr;
	void __iomem *base;
	int id;
	int dt_id;
	int low_thresh;
	int high_thresh;
};

struct lvts_ctrl {
	struct lvts_sensor sensors[LVTS_SENSOR_MAX];
	const struct lvts_data *lvts_data;
	u32 calibration[LVTS_SENSOR_MAX];
	u32 hw_tshut_raw_temp;
	u8 valid_sensor_mask;
	int mode;
	void __iomem *base;
	int low_thresh;
	int high_thresh;
};

struct lvts_domain {
	struct lvts_ctrl *lvts_ctrl;
	struct reset_control *reset;
	struct clk *clk;
	int num_lvts_ctrl;
	void __iomem *base;
	size_t calib_len;
	u8 *calib;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dom_dentry;
#endif
};

#ifdef CONFIG_MTK_LVTS_THERMAL_DEBUGFS

#define LVTS_DEBUG_FS_REGS(__reg)		\
{						\
	.name = __stringify(__reg),		\
	.offset = __reg(0),			\
}

static const struct debugfs_reg32 lvts_regs[] = {
	LVTS_DEBUG_FS_REGS(LVTS_MONCTL0),
	LVTS_DEBUG_FS_REGS(LVTS_MONCTL1),
	LVTS_DEBUG_FS_REGS(LVTS_MONCTL2),
	LVTS_DEBUG_FS_REGS(LVTS_MONINT),
	LVTS_DEBUG_FS_REGS(LVTS_MONINTSTS),
	LVTS_DEBUG_FS_REGS(LVTS_MONIDET0),
	LVTS_DEBUG_FS_REGS(LVTS_MONIDET1),
	LVTS_DEBUG_FS_REGS(LVTS_MONIDET2),
	LVTS_DEBUG_FS_REGS(LVTS_MONIDET3),
	LVTS_DEBUG_FS_REGS(LVTS_H2NTHRE),
	LVTS_DEBUG_FS_REGS(LVTS_HTHRE),
	LVTS_DEBUG_FS_REGS(LVTS_OFFSETH),
	LVTS_DEBUG_FS_REGS(LVTS_OFFSETL),
	LVTS_DEBUG_FS_REGS(LVTS_MSRCTL0),
	LVTS_DEBUG_FS_REGS(LVTS_MSRCTL1),
	LVTS_DEBUG_FS_REGS(LVTS_TSSEL),
	LVTS_DEBUG_FS_REGS(LVTS_CALSCALE),
	LVTS_DEBUG_FS_REGS(LVTS_ID),
	LVTS_DEBUG_FS_REGS(LVTS_CONFIG),
	LVTS_DEBUG_FS_REGS(LVTS_EDATA00),
	LVTS_DEBUG_FS_REGS(LVTS_EDATA01),
	LVTS_DEBUG_FS_REGS(LVTS_EDATA02),
	LVTS_DEBUG_FS_REGS(LVTS_EDATA03),
	LVTS_DEBUG_FS_REGS(LVTS_MSR0),
	LVTS_DEBUG_FS_REGS(LVTS_MSR1),
	LVTS_DEBUG_FS_REGS(LVTS_MSR2),
	LVTS_DEBUG_FS_REGS(LVTS_MSR3),
	LVTS_DEBUG_FS_REGS(LVTS_IMMD0),
	LVTS_DEBUG_FS_REGS(LVTS_IMMD1),
	LVTS_DEBUG_FS_REGS(LVTS_IMMD2),
	LVTS_DEBUG_FS_REGS(LVTS_IMMD3),
	LVTS_DEBUG_FS_REGS(LVTS_PROTCTL),
	LVTS_DEBUG_FS_REGS(LVTS_PROTTA),
	LVTS_DEBUG_FS_REGS(LVTS_PROTTB),
	LVTS_DEBUG_FS_REGS(LVTS_PROTTC),
	LVTS_DEBUG_FS_REGS(LVTS_CLKEN),
};

static int lvts_debugfs_init(struct device *dev, struct lvts_domain *lvts_td)
{
	struct debugfs_regset32 *regset;
	struct lvts_ctrl *lvts_ctrl;
	struct dentry *dentry;
	char name[64];
	int i;

	lvts_td->dom_dentry = debugfs_create_dir(dev_name(dev), NULL);
	if (IS_ERR(lvts_td->dom_dentry))
		return 0;

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++) {

		lvts_ctrl = &lvts_td->lvts_ctrl[i];

		sprintf(name, "controller%d", i);
		dentry = debugfs_create_dir(name, lvts_td->dom_dentry);
		if (IS_ERR(dentry))
			continue;

		regset = devm_kzalloc(dev, sizeof(*regset), GFP_KERNEL);
		if (!regset)
			continue;

		regset->base = lvts_ctrl->base;
		regset->regs = lvts_regs;
		regset->nregs = ARRAY_SIZE(lvts_regs);

		debugfs_create_regset32("registers", 0400, dentry, regset);
	}

	return 0;
}

static void lvts_debugfs_exit(struct lvts_domain *lvts_td)
{
	debugfs_remove_recursive(lvts_td->dom_dentry);
}

#else

static inline int lvts_debugfs_init(struct device *dev,
				    struct lvts_domain *lvts_td)
{
	return 0;
}

static void lvts_debugfs_exit(struct lvts_domain *lvts_td) { }

#endif

static int lvts_raw_to_temp(u32 raw_temp, int temp_factor)
{
	int temperature;

	temperature = ((s64)(raw_temp & 0xFFFF) * temp_factor) >> 14;
	temperature += golden_temp_offset;

	return temperature;
}

static u32 lvts_temp_to_raw(int temperature, int temp_factor)
{
	u32 raw_temp = ((s64)(golden_temp_offset - temperature)) << 14;

	raw_temp = div_s64(raw_temp, -temp_factor);

	return raw_temp;
}

static int lvts_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct lvts_sensor *lvts_sensor = thermal_zone_device_priv(tz);
	struct lvts_ctrl *lvts_ctrl = container_of(lvts_sensor, struct lvts_ctrl,
						   sensors[lvts_sensor->id]);
	const struct lvts_data *lvts_data = lvts_ctrl->lvts_data;
	void __iomem *msr = lvts_sensor->msr;
	u32 value;
	int rc;

	/*
	 * Measurement registers:
	 *
	 * LVTS_MSR[0-3] / LVTS_IMMD[0-3]
	 *
	 * Bits:
	 *
	 * 32-17: Unused
	 * 16	: Valid temperature
	 * 15-0	: Raw temperature
	 */
	rc = readl_poll_timeout(msr, value, value & BIT(16),
				LVTS_MSR_READ_WAIT_US, LVTS_MSR_READ_TIMEOUT_US);

	/*
	 * As the thermal zone temperature will read before the
	 * hardware sensor is fully initialized, we have to check the
	 * validity of the temperature returned when reading the
	 * measurement register. The thermal controller will set the
	 * valid bit temperature only when it is totally initialized.
	 *
	 * Otherwise, we may end up with garbage values out of the
	 * functionning temperature and directly jump to a system
	 * shutdown.
	 */
	if (rc)
		return -EAGAIN;

	*temp = lvts_raw_to_temp(value & 0xFFFF, lvts_data->temp_factor);

	return 0;
}

static void lvts_update_irq_mask(struct lvts_ctrl *lvts_ctrl)
{
	u32 masks[] = {
		LVTS_MONINT_OFFSET_SENSOR0,
		LVTS_MONINT_OFFSET_SENSOR1,
		LVTS_MONINT_OFFSET_SENSOR2,
		LVTS_MONINT_OFFSET_SENSOR3,
	};
	u32 value = 0;
	int i;

	value = readl(LVTS_MONINT(lvts_ctrl->base));

	for (i = 0; i < ARRAY_SIZE(masks); i++) {
		if (lvts_ctrl->sensors[i].high_thresh == lvts_ctrl->high_thresh
		    && lvts_ctrl->sensors[i].low_thresh == lvts_ctrl->low_thresh)
			value |= masks[i];
		else
			value &= ~masks[i];
	}

	writel(value, LVTS_MONINT(lvts_ctrl->base));
}

static bool lvts_should_update_thresh(struct lvts_ctrl *lvts_ctrl, int high)
{
	int i;

	if (high > lvts_ctrl->high_thresh)
		return true;

	lvts_for_each_valid_sensor(i, lvts_ctrl)
		if (lvts_ctrl->sensors[i].high_thresh == lvts_ctrl->high_thresh
		    && lvts_ctrl->sensors[i].low_thresh == lvts_ctrl->low_thresh)
			return false;

	return true;
}

static int lvts_set_trips(struct thermal_zone_device *tz, int low, int high)
{
	struct lvts_sensor *lvts_sensor = thermal_zone_device_priv(tz);
	struct lvts_ctrl *lvts_ctrl = container_of(lvts_sensor, struct lvts_ctrl,
						   sensors[lvts_sensor->id]);
	const struct lvts_data *lvts_data = lvts_ctrl->lvts_data;
	void __iomem *base = lvts_sensor->base;
	u32 raw_low = lvts_temp_to_raw(low != -INT_MAX ? low : LVTS_MINIMUM_THRESHOLD,
				       lvts_data->temp_factor);
	u32 raw_high = lvts_temp_to_raw(high, lvts_data->temp_factor);
	bool should_update_thresh;

	lvts_sensor->low_thresh = low;
	lvts_sensor->high_thresh = high;

	should_update_thresh = lvts_should_update_thresh(lvts_ctrl, high);
	if (should_update_thresh) {
		lvts_ctrl->high_thresh = high;
		lvts_ctrl->low_thresh = low;
	}
	lvts_update_irq_mask(lvts_ctrl);

	if (!should_update_thresh)
		return 0;

	/*
	 * Low offset temperature threshold
	 *
	 * LVTS_OFFSETL
	 *
	 * Bits:
	 *
	 * 14-0 : Raw temperature for threshold
	 */
	pr_debug("%s: Setting low limit temperature interrupt: %d\n",
		 thermal_zone_device_type(tz), low);
	writel(raw_low, LVTS_OFFSETL(base));

	/*
	 * High offset temperature threshold
	 *
	 * LVTS_OFFSETH
	 *
	 * Bits:
	 *
	 * 14-0 : Raw temperature for threshold
	 */
	pr_debug("%s: Setting high limit temperature interrupt: %d\n",
		 thermal_zone_device_type(tz), high);
	writel(raw_high, LVTS_OFFSETH(base));

	return 0;
}

static irqreturn_t lvts_ctrl_irq_handler(struct lvts_ctrl *lvts_ctrl)
{
	irqreturn_t iret = IRQ_NONE;
	u32 value;
	u32 masks[] = {
		LVTS_INT_SENSOR0,
		LVTS_INT_SENSOR1,
		LVTS_INT_SENSOR2,
		LVTS_INT_SENSOR3
	};
	int i;

	/*
	 * Interrupt monitoring status
	 *
	 * LVTS_MONINTST
	 *
	 * Bits:
	 *
	 * 31 : Interrupt for stage 3
	 * 30 : Interrupt for stage 2
	 * 29 : Interrupt for state 1
	 * 28 : Interrupt using filter on sensor 3
	 *
	 * 27 : Interrupt using immediate on sensor 3
	 * 26 : Interrupt normal to hot on sensor 3
	 * 25 : Interrupt high offset on sensor 3
	 * 24 : Interrupt low offset on sensor 3
	 *
	 * 23 : Interrupt hot threshold on sensor 3
	 * 22 : Interrupt cold threshold on sensor 3
	 * 21 : Interrupt using filter on sensor 2
	 * 20 : Interrupt using filter on sensor 1
	 *
	 * 19 : Interrupt using filter on sensor 0
	 * 18 : Interrupt using immediate on sensor 2
	 * 17 : Interrupt using immediate on sensor 1
	 * 16 : Interrupt using immediate on sensor 0
	 *
	 * 15 : Interrupt device access timeout interrupt
	 * 14 : Interrupt normal to hot on sensor 2
	 * 13 : Interrupt high offset interrupt on sensor 2
	 * 12 : Interrupt low offset interrupt on sensor 2
	 *
	 * 11 : Interrupt hot threshold on sensor 2
	 * 10 : Interrupt cold threshold on sensor 2
	 *  9 : Interrupt normal to hot on sensor 1
	 *  8 : Interrupt high offset interrupt on sensor 1
	 *
	 *  7 : Interrupt low offset interrupt on sensor 1
	 *  6 : Interrupt hot threshold on sensor 1
	 *  5 : Interrupt cold threshold on sensor 1
	 *  4 : Interrupt normal to hot on sensor 0
	 *
	 *  3 : Interrupt high offset interrupt on sensor 0
	 *  2 : Interrupt low offset interrupt on sensor 0
	 *  1 : Interrupt hot threshold on sensor 0
	 *  0 : Interrupt cold threshold on sensor 0
	 *
	 * We are interested in the sensor(s) responsible of the
	 * interrupt event. We update the thermal framework with the
	 * thermal zone associated with the sensor. The framework will
	 * take care of the rest whatever the kind of interrupt, we
	 * are only interested in which sensor raised the interrupt.
	 *
	 * sensor 3 interrupt: 0001 1111 1100 0000 0000 0000 0000 0000
	 *                  => 0x1FC00000
	 * sensor 2 interrupt: 0000 0000 0010 0100 0111 1100 0000 0000
	 *                  => 0x00247C00
	 * sensor 1 interrupt: 0000 0000 0001 0010 0000 0011 1110 0000
	 *                  => 0X001203E0
	 * sensor 0 interrupt: 0000 0000 0000 1001 0000 0000 0001 1111
	 *                  => 0x0009001F
	 */
	value = readl(LVTS_MONINTSTS(lvts_ctrl->base));

	/*
	 * Let's figure out which sensors raised the interrupt
	 *
	 * NOTE: the masks array must be ordered with the index
	 * corresponding to the sensor id eg. index=0, mask for
	 * sensor0.
	 */
	for (i = 0; i < ARRAY_SIZE(masks); i++) {

		if (!(value & masks[i]))
			continue;

		thermal_zone_device_update(lvts_ctrl->sensors[i].tz,
					   THERMAL_TRIP_VIOLATED);
		iret = IRQ_HANDLED;
	}

	/*
	 * Write back to clear the interrupt status (W1C)
	 */
	writel(value, LVTS_MONINTSTS(lvts_ctrl->base));

	return iret;
}

/*
 * Temperature interrupt handler. Even if the driver supports more
 * interrupt modes, we use the interrupt when the temperature crosses
 * the hot threshold the way up and the way down (modulo the
 * hysteresis).
 *
 * Each thermal domain has a couple of interrupts, one for hardware
 * reset and another one for all the thermal events happening on the
 * different sensors.
 *
 * The interrupt is configured for thermal events when crossing the
 * hot temperature limit. At each interrupt, we check in every
 * controller if there is an interrupt pending.
 */
static irqreturn_t lvts_irq_handler(int irq, void *data)
{
	struct lvts_domain *lvts_td = data;
	irqreturn_t aux, iret = IRQ_NONE;
	int i;

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++) {

		aux = lvts_ctrl_irq_handler(&lvts_td->lvts_ctrl[i]);
		if (aux != IRQ_HANDLED)
			continue;

		iret = IRQ_HANDLED;
	}

	return iret;
}

static struct thermal_zone_device_ops lvts_ops = {
	.get_temp = lvts_get_temp,
	.set_trips = lvts_set_trips,
};

static int lvts_sensor_init(struct device *dev, struct lvts_ctrl *lvts_ctrl,
					const struct lvts_ctrl_data *lvts_ctrl_data)
{
	struct lvts_sensor *lvts_sensor = lvts_ctrl->sensors;

	void __iomem *msr_regs[] = {
		LVTS_MSR0(lvts_ctrl->base),
		LVTS_MSR1(lvts_ctrl->base),
		LVTS_MSR2(lvts_ctrl->base),
		LVTS_MSR3(lvts_ctrl->base)
	};

	void __iomem *imm_regs[] = {
		LVTS_IMMD0(lvts_ctrl->base),
		LVTS_IMMD1(lvts_ctrl->base),
		LVTS_IMMD2(lvts_ctrl->base),
		LVTS_IMMD3(lvts_ctrl->base)
	};

	int i;

	lvts_for_each_valid_sensor(i, lvts_ctrl_data) {

		int dt_id = lvts_ctrl_data->lvts_sensor[i].dt_id;

		/*
		 * At this point, we don't know which id matches which
		 * sensor. Let's set arbitrally the id from the index.
		 */
		lvts_sensor[i].id = i;

		/*
		 * The thermal zone registration will set the trip
		 * point interrupt in the thermal controller
		 * register. But this one will be reset in the
		 * initialization after. So we need to post pone the
		 * thermal zone creation after the controller is
		 * setup. For this reason, we store the device tree
		 * node id from the data in the sensor structure
		 */
		lvts_sensor[i].dt_id = dt_id;

		/*
		 * We assign the base address of the thermal
		 * controller as a back pointer. So it will be
		 * accessible from the different thermal framework ops
		 * as we pass the lvts_sensor pointer as thermal zone
		 * private data.
		 */
		lvts_sensor[i].base = lvts_ctrl->base;

		/*
		 * Each sensor has its own register address to read from.
		 */
		lvts_sensor[i].msr = lvts_ctrl_data->mode == LVTS_MSR_IMMEDIATE_MODE ?
			imm_regs[i] : msr_regs[i];

		lvts_sensor[i].low_thresh = INT_MIN;
		lvts_sensor[i].high_thresh = INT_MIN;
	};

	lvts_ctrl->valid_sensor_mask = lvts_ctrl_data->valid_sensor_mask;

	return 0;
}

/*
 * The efuse blob values follows the sensor enumeration per thermal
 * controller. The decoding of the stream is as follow:
 *
 * MT8192 :
 * Stream index map for MCU Domain mt8192 :
 *
 * <-----mcu-tc#0-----> <-----sensor#0----->        <-----sensor#1----->
 *  0x01 | 0x02 | 0x03 | 0x04 | 0x05 | 0x06 | 0x07 | 0x08 | 0x09 | 0x0A | 0x0B
 *
 * <-----sensor#2----->        <-----sensor#3----->
 *  0x0C | 0x0D | 0x0E | 0x0F | 0x10 | 0x11 | 0x12 | 0x13
 *
 * <-----sensor#4----->        <-----sensor#5----->        <-----sensor#6----->        <-----sensor#7----->
 *  0x14 | 0x15 | 0x16 | 0x17 | 0x18 | 0x19 | 0x1A | 0x1B | 0x1C | 0x1D | 0x1E | 0x1F | 0x20 | 0x21 | 0x22 | 0x23
 *
 * Stream index map for AP Domain mt8192 :
 *
 * <-----sensor#0----->        <-----sensor#1----->
 *  0x24 | 0x25 | 0x26 | 0x27 | 0x28 | 0x29 | 0x2A | 0x2B
 *
 * <-----sensor#2----->        <-----sensor#3----->
 *  0x2C | 0x2D | 0x2E | 0x2F | 0x30 | 0x31 | 0x32 | 0x33
 *
 * <-----sensor#4----->        <-----sensor#5----->
 *  0x34 | 0x35 | 0x36 | 0x37 | 0x38 | 0x39 | 0x3A | 0x3B
 *
 * <-----sensor#6----->        <-----sensor#7----->        <-----sensor#8----->
 *  0x3C | 0x3D | 0x3E | 0x3F | 0x40 | 0x41 | 0x42 | 0x43 | 0x44 | 0x45 | 0x46 | 0x47
 *
 * MT8195 :
 * Stream index map for MCU Domain mt8195 :
 *
 * <-----mcu-tc#0-----> <-----sensor#0-----> <-----sensor#1----->
 *  0x01 | 0x02 | 0x03 | 0x04 | 0x05 | 0x06 | 0x07 | 0x08 | 0x09
 *
 * <-----mcu-tc#1-----> <-----sensor#2-----> <-----sensor#3----->
 *  0x0A | 0x0B | 0x0C | 0x0D | 0x0E | 0x0F | 0x10 | 0x11 | 0x12
 *
 * <-----mcu-tc#2-----> <-----sensor#4-----> <-----sensor#5-----> <-----sensor#6-----> <-----sensor#7----->
 *  0x13 | 0x14 | 0x15 | 0x16 | 0x17 | 0x18 | 0x19 | 0x1A | 0x1B | 0x1C | 0x1D | 0x1E | 0x1F | 0x20 | 0x21
 *
 * Stream index map for AP Domain mt8195 :
 *
 * <-----ap--tc#0-----> <-----sensor#0-----> <-----sensor#1----->
 *  0x22 | 0x23 | 0x24 | 0x25 | 0x26 | 0x27 | 0x28 | 0x29 | 0x2A
 *
 * <-----ap--tc#1-----> <-----sensor#2-----> <-----sensor#3----->
 *  0x2B | 0x2C | 0x2D | 0x2E | 0x2F | 0x30 | 0x31 | 0x32 | 0x33
 *
 * <-----ap--tc#2-----> <-----sensor#4-----> <-----sensor#5-----> <-----sensor#6----->
 *  0x34 | 0x35 | 0x36 | 0x37 | 0x38 | 0x39 | 0x3A | 0x3B | 0x3C | 0x3D | 0x3E | 0x3F
 *
 * <-----ap--tc#3-----> <-----sensor#7-----> <-----sensor#8----->
 *  0x40 | 0x41 | 0x42 | 0x43 | 0x44 | 0x45 | 0x46 | 0x47 | 0x48
 *
 * Note: In some cases, values don't strictly follow a little endian ordering.
 * The data description gives byte offsets constituting each calibration value
 * for each sensor.
 */
static int lvts_calibration_init(struct device *dev, struct lvts_ctrl *lvts_ctrl,
					const struct lvts_ctrl_data *lvts_ctrl_data,
					u8 *efuse_calibration,
					size_t calib_len)
{
	int i;

	lvts_for_each_valid_sensor(i, lvts_ctrl_data) {
		const struct lvts_sensor_data *sensor =
					&lvts_ctrl_data->lvts_sensor[i];

		if (sensor->cal_offsets[0] >= calib_len ||
		    sensor->cal_offsets[1] >= calib_len ||
		    sensor->cal_offsets[2] >= calib_len)
			return -EINVAL;

		lvts_ctrl->calibration[i] =
			(efuse_calibration[sensor->cal_offsets[0]] << 0) +
			(efuse_calibration[sensor->cal_offsets[1]] << 8) +
			(efuse_calibration[sensor->cal_offsets[2]] << 16);
	}

	return 0;
}

/*
 * The efuse bytes stream can be split into different chunk of
 * nvmems. This function reads and concatenate those into a single
 * buffer so it can be read sequentially when initializing the
 * calibration data.
 */
static int lvts_calibration_read(struct device *dev, struct lvts_domain *lvts_td,
					const struct lvts_data *lvts_data)
{
	struct device_node *np = dev_of_node(dev);
	struct nvmem_cell *cell;
	struct property *prop;
	const char *cell_name;

	of_property_for_each_string(np, "nvmem-cell-names", prop, cell_name) {
		size_t len;
		u8 *efuse;

		cell = of_nvmem_cell_get(np, cell_name);
		if (IS_ERR(cell)) {
			dev_err(dev, "Failed to get cell '%s'\n", cell_name);
			return PTR_ERR(cell);
		}

		efuse = nvmem_cell_read(cell, &len);

		nvmem_cell_put(cell);

		if (IS_ERR(efuse)) {
			dev_err(dev, "Failed to read cell '%s'\n", cell_name);
			return PTR_ERR(efuse);
		}

		lvts_td->calib = devm_krealloc(dev, lvts_td->calib,
					       lvts_td->calib_len + len, GFP_KERNEL);
		if (!lvts_td->calib) {
			kfree(efuse);
			return -ENOMEM;
		}

		memcpy(lvts_td->calib + lvts_td->calib_len, efuse, len);

		lvts_td->calib_len += len;

		kfree(efuse);
	}

	return 0;
}

static int lvts_golden_temp_init(struct device *dev, u8 *calib,
				 const struct lvts_data *lvts_data)
{
	u32 gt;

	/*
	 * The golden temp information is contained in the first 32-bit
	 * word  of efuse data at a specific bit offset.
	 */
	gt = (((u32 *)calib)[0] >> lvts_data->gt_calib_bit_offset) & 0xff;

	/* A zero value for gt means that device has invalid efuse data */
	if (!gt)
		return -ENODATA;

	if (gt < LVTS_GOLDEN_TEMP_MAX)
		golden_temp = gt;

	golden_temp_offset = golden_temp * 500 + lvts_data->temp_offset;

	return 0;
}

static int lvts_ctrl_init(struct device *dev, struct lvts_domain *lvts_td,
					const struct lvts_data *lvts_data)
{
	size_t size = sizeof(*lvts_td->lvts_ctrl) * lvts_data->num_lvts_ctrl;
	struct lvts_ctrl *lvts_ctrl;
	int i, ret;

	/*
	 * Create the calibration bytes stream from efuse data
	 */
	ret = lvts_calibration_read(dev, lvts_td, lvts_data);
	if (ret)
		return ret;

	ret = lvts_golden_temp_init(dev, lvts_td->calib, lvts_data);
	if (ret)
		return ret;

	lvts_ctrl = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!lvts_ctrl)
		return -ENOMEM;

	for (i = 0; i < lvts_data->num_lvts_ctrl; i++) {

		lvts_ctrl[i].base = lvts_td->base + lvts_data->lvts_ctrl[i].offset;
		lvts_ctrl[i].lvts_data = lvts_data;

		ret = lvts_sensor_init(dev, &lvts_ctrl[i],
				       &lvts_data->lvts_ctrl[i]);
		if (ret)
			return ret;

		ret = lvts_calibration_init(dev, &lvts_ctrl[i],
					    &lvts_data->lvts_ctrl[i],
					    lvts_td->calib,
					    lvts_td->calib_len);
		if (ret)
			return ret;

		/*
		 * The mode the ctrl will use to read the temperature
		 * (filtered or immediate)
		 */
		lvts_ctrl[i].mode = lvts_data->lvts_ctrl[i].mode;

		/*
		 * The temperature to raw temperature must be done
		 * after initializing the calibration.
		 */
		lvts_ctrl[i].hw_tshut_raw_temp =
			lvts_temp_to_raw(LVTS_HW_TSHUT_TEMP,
					 lvts_data->temp_factor);

		lvts_ctrl[i].low_thresh = INT_MIN;
		lvts_ctrl[i].high_thresh = INT_MIN;
	}

	/*
	 * We no longer need the efuse bytes stream, let's free it
	 */
	devm_kfree(dev, lvts_td->calib);

	lvts_td->lvts_ctrl = lvts_ctrl;
	lvts_td->num_lvts_ctrl = lvts_data->num_lvts_ctrl;

	return 0;
}

/*
 * At this point the configuration register is the only place in the
 * driver where we write multiple values. Per hardware constraint,
 * each write in the configuration register must be separated by a
 * delay of 2 us.
 */
static void lvts_write_config(struct lvts_ctrl *lvts_ctrl, u32 *cmds, int nr_cmds)
{
	int i;

	/*
	 * Configuration register
	 */
	for (i = 0; i < nr_cmds; i++) {
		writel(cmds[i], LVTS_CONFIG(lvts_ctrl->base));
		usleep_range(2, 4);
	}
}

static int lvts_irq_init(struct lvts_ctrl *lvts_ctrl)
{
	/*
	 * LVTS_PROTCTL : Thermal Protection Sensor Selection
	 *
	 * Bits:
	 *
	 * 19-18 : Sensor to base the protection on
	 * 17-16 : Strategy:
	 *         00 : Average of 4 sensors
	 *         01 : Max of 4 sensors
	 *         10 : Selected sensor with bits 19-18
	 *         11 : Reserved
	 */
	writel(BIT(16), LVTS_PROTCTL(lvts_ctrl->base));

	/*
	 * LVTS_PROTTA : Stage 1 temperature threshold
	 * LVTS_PROTTB : Stage 2 temperature threshold
	 * LVTS_PROTTC : Stage 3 temperature threshold
	 *
	 * Bits:
	 *
	 * 14-0: Raw temperature threshold
	 *
	 * writel(0x0, LVTS_PROTTA(lvts_ctrl->base));
	 * writel(0x0, LVTS_PROTTB(lvts_ctrl->base));
	 */
	writel(lvts_ctrl->hw_tshut_raw_temp, LVTS_PROTTC(lvts_ctrl->base));

	/*
	 * LVTS_MONINT : Interrupt configuration register
	 *
	 * The LVTS_MONINT register layout is the same as the LVTS_MONINTSTS
	 * register, except we set the bits to enable the interrupt.
	 */
	writel(LVTS_MONINT_CONF, LVTS_MONINT(lvts_ctrl->base));

	return 0;
}

static int lvts_domain_reset(struct device *dev, struct reset_control *reset)
{
	int ret;

	ret = reset_control_assert(reset);
	if (ret)
		return ret;

	return reset_control_deassert(reset);
}

/*
 * Enable or disable the clocks of a specified thermal controller
 */
static int lvts_ctrl_set_enable(struct lvts_ctrl *lvts_ctrl, int enable)
{
	/*
	 * LVTS_CLKEN : Internal LVTS clock
	 *
	 * Bits:
	 *
	 * 0 : enable / disable clock
	 */
	writel(enable, LVTS_CLKEN(lvts_ctrl->base));

	return 0;
}

static int lvts_ctrl_connect(struct device *dev, struct lvts_ctrl *lvts_ctrl)
{
	u32 id, cmds[] = { 0xC103FFFF, 0xC502FF55 };

	lvts_write_config(lvts_ctrl, cmds, ARRAY_SIZE(cmds));

	/*
	 * LVTS_ID : Get ID and status of the thermal controller
	 *
	 * Bits:
	 *
	 * 0-5	: thermal controller id
	 *   7	: thermal controller connection is valid
	 */
	id = readl(LVTS_ID(lvts_ctrl->base));
	if (!(id & BIT(7)))
		return -EIO;

	return 0;
}

static int lvts_ctrl_initialize(struct device *dev, struct lvts_ctrl *lvts_ctrl)
{
	/*
	 * Write device mask: 0xC1030000
	 */
	u32 cmds[] = {
		0xC1030E01, 0xC1030CFC, 0xC1030A8C, 0xC103098D, 0xC10308F1,
		0xC10307A6, 0xC10306B8, 0xC1030500, 0xC1030420, 0xC1030300,
		0xC1030030, 0xC10300F6, 0xC1030050, 0xC1030060, 0xC10300AC,
		0xC10300FC, 0xC103009D, 0xC10300F1, 0xC10300E1
	};

	lvts_write_config(lvts_ctrl, cmds, ARRAY_SIZE(cmds));

	return 0;
}

static int lvts_ctrl_calibrate(struct device *dev, struct lvts_ctrl *lvts_ctrl)
{
	int i;
	void __iomem *lvts_edata[] = {
		LVTS_EDATA00(lvts_ctrl->base),
		LVTS_EDATA01(lvts_ctrl->base),
		LVTS_EDATA02(lvts_ctrl->base),
		LVTS_EDATA03(lvts_ctrl->base)
	};

	/*
	 * LVTS_EDATA0X : Efuse calibration reference value for sensor X
	 *
	 * Bits:
	 *
	 * 20-0 : Efuse value for normalization data
	 */
	for (i = 0; i < LVTS_SENSOR_MAX; i++)
		writel(lvts_ctrl->calibration[i], lvts_edata[i]);

	return 0;
}

static int lvts_ctrl_configure(struct device *dev, struct lvts_ctrl *lvts_ctrl)
{
	u32 value;

	/*
	 * LVTS_TSSEL : Sensing point index numbering
	 *
	 * Bits:
	 *
	 * 31-24: ADC Sense 3
	 * 23-16: ADC Sense 2
	 * 15-8	: ADC Sense 1
	 * 7-0	: ADC Sense 0
	 */
	value = LVTS_TSSEL_CONF;
	writel(value, LVTS_TSSEL(lvts_ctrl->base));

	/*
	 * LVTS_CALSCALE : ADC voltage round
	 */
	value = 0x300;
	value = LVTS_CALSCALE_CONF;

	/*
	 * LVTS_MSRCTL0 : Sensor filtering strategy
	 *
	 * Filters:
	 *
	 * 000 : One sample
	 * 001 : Avg 2 samples
	 * 010 : 4 samples, drop min and max, avg 2 samples
	 * 011 : 6 samples, drop min and max, avg 4 samples
	 * 100 : 10 samples, drop min and max, avg 8 samples
	 * 101 : 18 samples, drop min and max, avg 16 samples
	 *
	 * Bits:
	 *
	 * 0-2  : Sensor0 filter
	 * 3-5  : Sensor1 filter
	 * 6-8  : Sensor2 filter
	 * 9-11 : Sensor3 filter
	 */
	value = LVTS_HW_FILTER << 9 |  LVTS_HW_FILTER << 6 |
			LVTS_HW_FILTER << 3 | LVTS_HW_FILTER;
	writel(value, LVTS_MSRCTL0(lvts_ctrl->base));

	/*
	 * LVTS_MONCTL1 : Period unit and group interval configuration
	 *
	 * The clock source of LVTS thermal controller is 26MHz.
	 *
	 * The period unit is a time base for all the interval delays
	 * specified in the registers. By default we use 12. The time
	 * conversion is done by multiplying by 256 and 1/26.10^6
	 *
	 * An interval delay multiplied by the period unit gives the
	 * duration in seconds.
	 *
	 * - Filter interval delay is a delay between two samples of
	 * the same sensor.
	 *
	 * - Sensor interval delay is a delay between two samples of
	 * different sensors.
	 *
	 * - Group interval delay is a delay between different rounds.
	 *
	 * For example:
	 *     If Period unit = C, filter delay = 1, sensor delay = 2, group delay = 1,
	 *     and two sensors, TS1 and TS2, are in a LVTS thermal controller
	 *     and then
	 *     Period unit time = C * 1/26M * 256 = 12 * 38.46ns * 256 = 118.149us
	 *     Filter interval delay = 1 * Period unit = 118.149us
	 *     Sensor interval delay = 2 * Period unit = 236.298us
	 *     Group interval delay = 1 * Period unit = 118.149us
	 *
	 *     TS1    TS1 ... TS1    TS2    TS2 ... TS2    TS1...
	 *        <--> Filter interval delay
	 *                       <--> Sensor interval delay
	 *                                             <--> Group interval delay
	 * Bits:
	 *      29 - 20 : Group interval
	 *      16 - 13 : Send a single interrupt when crossing the hot threshold (1)
	 *                or an interrupt everytime the hot threshold is crossed (0)
	 *       9 - 0  : Period unit
	 *
	 */
	value = LVTS_GROUP_INTERVAL << 20 | LVTS_PERIOD_UNIT;
	writel(value, LVTS_MONCTL1(lvts_ctrl->base));

	/*
	 * LVTS_MONCTL2 : Filtering and sensor interval
	 *
	 * Bits:
	 *
	 *      25-16 : Interval unit in PERIOD_UNIT between sample on
	 *              the same sensor, filter interval
	 *       9-0  : Interval unit in PERIOD_UNIT between each sensor
	 *
	 */
	value = LVTS_FILTER_INTERVAL << 16 | LVTS_SENSOR_INTERVAL;
	writel(value, LVTS_MONCTL2(lvts_ctrl->base));

	return lvts_irq_init(lvts_ctrl);
}

static int lvts_ctrl_start(struct device *dev, struct lvts_ctrl *lvts_ctrl)
{
	struct lvts_sensor *lvts_sensors = lvts_ctrl->sensors;
	struct thermal_zone_device *tz;
	u32 sensor_map = 0;
	int i;
	/*
	 * Bitmaps to enable each sensor on immediate and filtered modes, as
	 * described in MSRCTL1 and MONCTL0 registers below, respectively.
	 */
	u32 sensor_imm_bitmap[] = { BIT(4), BIT(5), BIT(6), BIT(9) };
	u32 sensor_filt_bitmap[] = { BIT(0), BIT(1), BIT(2), BIT(3) };

	u32 *sensor_bitmap = lvts_ctrl->mode == LVTS_MSR_IMMEDIATE_MODE ?
			     sensor_imm_bitmap : sensor_filt_bitmap;

	lvts_for_each_valid_sensor(i, lvts_ctrl) {

		int dt_id = lvts_sensors[i].dt_id;

		tz = devm_thermal_of_zone_register(dev, dt_id, &lvts_sensors[i],
						   &lvts_ops);
		if (IS_ERR(tz)) {
			/*
			 * This thermal zone is not described in the
			 * device tree. It is not an error from the
			 * thermal OF code POV, we just continue.
			 */
			if (PTR_ERR(tz) == -ENODEV)
				continue;

			return PTR_ERR(tz);
		}

		devm_thermal_add_hwmon_sysfs(dev, tz);

		/*
		 * The thermal zone pointer will be needed in the
		 * interrupt handler, we store it in the sensor
		 * structure. The thermal domain structure will be
		 * passed to the interrupt handler private data as the
		 * interrupt is shared for all the controller
		 * belonging to the thermal domain.
		 */
		lvts_sensors[i].tz = tz;

		/*
		 * This sensor was correctly associated with a thermal
		 * zone, let's set the corresponding bit in the sensor
		 * map, so we can enable the temperature monitoring in
		 * the hardware thermal controller.
		 */
		sensor_map |= sensor_bitmap[i];
	}

	/*
	 * The initialization of the thermal zones give us
	 * which sensor point to enable. If any thermal zone
	 * was not described in the device tree, it won't be
	 * enabled here in the sensor map.
	 */
	if (lvts_ctrl->mode == LVTS_MSR_IMMEDIATE_MODE) {
		/*
		 * LVTS_MSRCTL1 : Measurement control
		 *
		 * Bits:
		 *
		 * 9: Ignore MSRCTL0 config and do immediate measurement on sensor3
		 * 6: Ignore MSRCTL0 config and do immediate measurement on sensor2
		 * 5: Ignore MSRCTL0 config and do immediate measurement on sensor1
		 * 4: Ignore MSRCTL0 config and do immediate measurement on sensor0
		 *
		 * That configuration will ignore the filtering and the delays
		 * introduced in MONCTL1 and MONCTL2
		 */
		writel(sensor_map, LVTS_MSRCTL1(lvts_ctrl->base));
	} else {
		/*
		 * Bits:
		 *      9: Single point access flow
		 *    0-3: Enable sensing point 0-3
		 */
		writel(sensor_map | BIT(9), LVTS_MONCTL0(lvts_ctrl->base));
	}

	return 0;
}

static int lvts_domain_init(struct device *dev, struct lvts_domain *lvts_td,
					const struct lvts_data *lvts_data)
{
	struct lvts_ctrl *lvts_ctrl;
	int i, ret;

	ret = lvts_ctrl_init(dev, lvts_td, lvts_data);
	if (ret)
		return ret;

	ret = lvts_domain_reset(dev, lvts_td->reset);
	if (ret) {
		dev_dbg(dev, "Failed to reset domain");
		return ret;
	}

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++) {

		lvts_ctrl = &lvts_td->lvts_ctrl[i];

		/*
		 * Initialization steps:
		 *
		 * - Enable the clock
		 * - Connect to the LVTS
		 * - Initialize the LVTS
		 * - Prepare the calibration data
		 * - Select monitored sensors
		 * [ Configure sampling ]
		 * [ Configure the interrupt ]
		 * - Start measurement
		 */
		ret = lvts_ctrl_set_enable(lvts_ctrl, true);
		if (ret) {
			dev_dbg(dev, "Failed to enable LVTS clock");
			return ret;
		}

		ret = lvts_ctrl_connect(dev, lvts_ctrl);
		if (ret) {
			dev_dbg(dev, "Failed to connect to LVTS controller");
			return ret;
		}

		ret = lvts_ctrl_initialize(dev, lvts_ctrl);
		if (ret) {
			dev_dbg(dev, "Failed to initialize controller");
			return ret;
		}

		ret = lvts_ctrl_calibrate(dev, lvts_ctrl);
		if (ret) {
			dev_dbg(dev, "Failed to calibrate controller");
			return ret;
		}

		ret = lvts_ctrl_configure(dev, lvts_ctrl);
		if (ret) {
			dev_dbg(dev, "Failed to configure controller");
			return ret;
		}

		ret = lvts_ctrl_start(dev, lvts_ctrl);
		if (ret) {
			dev_dbg(dev, "Failed to start controller");
			return ret;
		}
	}

	return lvts_debugfs_init(dev, lvts_td);
}

static int lvts_probe(struct platform_device *pdev)
{
	const struct lvts_data *lvts_data;
	struct lvts_domain *lvts_td;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int irq, ret;

	lvts_td = devm_kzalloc(dev, sizeof(*lvts_td), GFP_KERNEL);
	if (!lvts_td)
		return -ENOMEM;

	lvts_data = of_device_get_match_data(dev);
	if (!lvts_data)
		return -ENODEV;

	lvts_td->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(lvts_td->clk))
		return dev_err_probe(dev, PTR_ERR(lvts_td->clk), "Failed to retrieve clock\n");

	res = platform_get_mem_or_io(pdev, 0);
	if (!res)
		return dev_err_probe(dev, (-ENXIO), "No IO resource\n");

	lvts_td->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(lvts_td->base))
		return dev_err_probe(dev, PTR_ERR(lvts_td->base), "Failed to map io resource\n");

	lvts_td->reset = devm_reset_control_get_by_index(dev, 0);
	if (IS_ERR(lvts_td->reset))
		return dev_err_probe(dev, PTR_ERR(lvts_td->reset), "Failed to get reset control\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	golden_temp_offset = lvts_data->temp_offset;

	ret = lvts_domain_init(dev, lvts_td, lvts_data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialize the lvts domain\n");

	/*
	 * At this point the LVTS is initialized and enabled. We can
	 * safely enable the interrupt.
	 */
	ret = devm_request_threaded_irq(dev, irq, NULL, lvts_irq_handler,
					IRQF_ONESHOT, dev_name(dev), lvts_td);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request interrupt\n");

	platform_set_drvdata(pdev, lvts_td);

	return 0;
}

static void lvts_remove(struct platform_device *pdev)
{
	struct lvts_domain *lvts_td;
	int i;

	lvts_td = platform_get_drvdata(pdev);

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++)
		lvts_ctrl_set_enable(&lvts_td->lvts_ctrl[i], false);

	lvts_debugfs_exit(lvts_td);
}

static const struct lvts_ctrl_data mt7988_lvts_ap_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT7988_CPU_0,
			  .cal_offsets = { 0x00, 0x01, 0x02 } },
			{ .dt_id = MT7988_CPU_1,
			  .cal_offsets = { 0x04, 0x05, 0x06 } },
			{ .dt_id = MT7988_ETH2P5G_0,
			  .cal_offsets = { 0x08, 0x09, 0x0a } },
			{ .dt_id = MT7988_ETH2P5G_1,
			  .cal_offsets = { 0x0c, 0x0d, 0x0e } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT7988_TOPS_0,
			   .cal_offsets = { 0x14, 0x15, 0x16 } },
			{ .dt_id = MT7988_TOPS_1,
			   .cal_offsets = { 0x18, 0x19, 0x1a } },
			{ .dt_id = MT7988_ETHWARP_0,
			   .cal_offsets = { 0x1c, 0x1d, 0x1e } },
			{ .dt_id = MT7988_ETHWARP_1,
			   .cal_offsets = { 0x20, 0x21, 0x22 } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x100,
	}
};

static int lvts_suspend(struct device *dev)
{
	struct lvts_domain *lvts_td;
	int i;

	lvts_td = dev_get_drvdata(dev);

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++)
		lvts_ctrl_set_enable(&lvts_td->lvts_ctrl[i], false);

	clk_disable_unprepare(lvts_td->clk);

	return 0;
}

static int lvts_resume(struct device *dev)
{
	struct lvts_domain *lvts_td;
	int i, ret;

	lvts_td = dev_get_drvdata(dev);

	ret = clk_prepare_enable(lvts_td->clk);
	if (ret)
		return ret;

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++)
		lvts_ctrl_set_enable(&lvts_td->lvts_ctrl[i], true);

	return 0;
}

/*
 * The MT8186 calibration data is stored as packed 3-byte little-endian
 * values using a weird layout that makes sense only when viewed as a 32-bit
 * hexadecimal word dump. Let's suppose SxBy where x = sensor number and
 * y = byte number where the LSB is y=0. We then have:
 *
 *   [S0B2-S0B1-S0B0-S1B2] [S1B1-S1B0-S2B2-S2B1] [S2B0-S3B2-S3B1-S3B0]
 *
 * However, when considering a byte stream, those appear as follows:
 *
 *   [S1B2] [S0B0[ [S0B1] [S0B2] [S2B1] [S2B2] [S1B0] [S1B1] [S3B0] [S3B1] [S3B2] [S2B0]
 *
 * Hence the rather confusing offsets provided below.
 */
static const struct lvts_ctrl_data mt8186_lvts_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT8186_LITTLE_CPU0,
			  .cal_offsets = { 5, 6, 7 } },
			{ .dt_id = MT8186_LITTLE_CPU1,
			  .cal_offsets = { 10, 11, 4 } },
			{ .dt_id = MT8186_LITTLE_CPU2,
			  .cal_offsets = { 15, 8, 9 } },
			{ .dt_id = MT8186_CAM,
			  .cal_offsets = { 12, 13, 14 } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8186_BIG_CPU0,
			  .cal_offsets = { 22, 23, 16 } },
			{ .dt_id = MT8186_BIG_CPU1,
			  .cal_offsets = { 27, 20, 21 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x100,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8186_NNA,
			  .cal_offsets = { 29, 30, 31 } },
			{ .dt_id = MT8186_ADSP,
			  .cal_offsets = { 34, 35, 28 } },
			{ .dt_id = MT8186_MFG,
			  .cal_offsets = { 39, 32, 33 } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 0),
		.offset = 0x200,
	}
};

static const struct lvts_ctrl_data mt8188_lvts_mcu_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT8188_MCU_LITTLE_CPU0,
			  .cal_offsets = { 22, 23, 24 } },
			{ .dt_id = MT8188_MCU_LITTLE_CPU1,
			  .cal_offsets = { 25, 26, 27 } },
			{ .dt_id = MT8188_MCU_LITTLE_CPU2,
			  .cal_offsets = { 28, 29, 30 } },
			{ .dt_id = MT8188_MCU_LITTLE_CPU3,
			  .cal_offsets = { 31, 32, 33 } },
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8188_MCU_BIG_CPU0,
			  .cal_offsets = { 34, 35, 36 } },
			{ .dt_id = MT8188_MCU_BIG_CPU1,
			  .cal_offsets = { 37, 38, 39 } },
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x100,
	}
};

static const struct lvts_ctrl_data mt8188_lvts_ap_data_ctrl[] = {
	{
		.lvts_sensor = {

			{ /* unused */ },
			{ .dt_id = MT8188_AP_APU,
			  .cal_offsets = { 40, 41, 42 } },
		},
		VALID_SENSOR_MAP(0, 1, 0, 0),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8188_AP_GPU1,
			  .cal_offsets = { 43, 44, 45 } },
			{ .dt_id = MT8188_AP_GPU2,
			  .cal_offsets = { 46, 47, 48 } },
			{ .dt_id = MT8188_AP_SOC1,
			  .cal_offsets = { 49, 50, 51 } },
		},
		VALID_SENSOR_MAP(1, 1, 1, 0),
		.offset = 0x100,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8188_AP_SOC2,
			  .cal_offsets = { 52, 53, 54 } },
			{ .dt_id = MT8188_AP_SOC3,
			  .cal_offsets = { 55, 56, 57 } },
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x200,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8188_AP_CAM1,
			  .cal_offsets = { 58, 59, 60 } },
			{ .dt_id = MT8188_AP_CAM2,
			  .cal_offsets = { 61, 62, 63 } },
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x300,
	}
};

static const struct lvts_ctrl_data mt8192_lvts_mcu_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_MCU_BIG_CPU0,
			  .cal_offsets = { 0x04, 0x05, 0x06 } },
			{ .dt_id = MT8192_MCU_BIG_CPU1,
			  .cal_offsets = { 0x08, 0x09, 0x0a } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x0,
		.mode = LVTS_MSR_FILTERED_MODE,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_MCU_BIG_CPU2,
			  .cal_offsets = { 0x0c, 0x0d, 0x0e } },
			{ .dt_id = MT8192_MCU_BIG_CPU3,
			  .cal_offsets = { 0x10, 0x11, 0x12 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x100,
		.mode = LVTS_MSR_FILTERED_MODE,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_MCU_LITTLE_CPU0,
			  .cal_offsets = { 0x14, 0x15, 0x16 } },
			{ .dt_id = MT8192_MCU_LITTLE_CPU1,
			  .cal_offsets = { 0x18, 0x19, 0x1a } },
			{ .dt_id = MT8192_MCU_LITTLE_CPU2,
			  .cal_offsets = { 0x1c, 0x1d, 0x1e } },
			{ .dt_id = MT8192_MCU_LITTLE_CPU3,
			  .cal_offsets = { 0x20, 0x21, 0x22 } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x200,
		.mode = LVTS_MSR_FILTERED_MODE,
	}
};

static const struct lvts_ctrl_data mt8192_lvts_ap_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_AP_VPU0,
			  .cal_offsets = { 0x24, 0x25, 0x26 } },
			{ .dt_id = MT8192_AP_VPU1,
			  .cal_offsets = { 0x28, 0x29, 0x2a } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_AP_GPU0,
			  .cal_offsets = { 0x2c, 0x2d, 0x2e } },
			{ .dt_id = MT8192_AP_GPU1,
			  .cal_offsets = { 0x30, 0x31, 0x32 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x100,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_AP_INFRA,
			  .cal_offsets = { 0x34, 0x35, 0x36 } },
			{ .dt_id = MT8192_AP_CAM,
			  .cal_offsets = { 0x38, 0x39, 0x3a } },
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x200,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_AP_MD0,
			  .cal_offsets = { 0x3c, 0x3d, 0x3e } },
			{ .dt_id = MT8192_AP_MD1,
			  .cal_offsets = { 0x40, 0x41, 0x42 } },
			{ .dt_id = MT8192_AP_MD2,
			  .cal_offsets = { 0x44, 0x45, 0x46 } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 0),
		.offset = 0x300,
	}
};

static const struct lvts_ctrl_data mt8195_lvts_mcu_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_MCU_BIG_CPU0,
			  .cal_offsets = { 0x04, 0x05, 0x06 } },
			{ .dt_id = MT8195_MCU_BIG_CPU1,
			  .cal_offsets = { 0x07, 0x08, 0x09 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_MCU_BIG_CPU2,
			  .cal_offsets = { 0x0d, 0x0e, 0x0f } },
			{ .dt_id = MT8195_MCU_BIG_CPU3,
			  .cal_offsets = { 0x10, 0x11, 0x12 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x100,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_MCU_LITTLE_CPU0,
			  .cal_offsets = { 0x16, 0x17, 0x18 } },
			{ .dt_id = MT8195_MCU_LITTLE_CPU1,
			  .cal_offsets = { 0x19, 0x1a, 0x1b } },
			{ .dt_id = MT8195_MCU_LITTLE_CPU2,
			  .cal_offsets = { 0x1c, 0x1d, 0x1e } },
			{ .dt_id = MT8195_MCU_LITTLE_CPU3,
			  .cal_offsets = { 0x1f, 0x20, 0x21 } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x200,
	}
};

static const struct lvts_ctrl_data mt8195_lvts_ap_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_AP_VPU0,
			  .cal_offsets = { 0x25, 0x26, 0x27 } },
			{ .dt_id = MT8195_AP_VPU1,
			  .cal_offsets = { 0x28, 0x29, 0x2a } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_AP_GPU0,
			  .cal_offsets = { 0x2e, 0x2f, 0x30 } },
			{ .dt_id = MT8195_AP_GPU1,
			  .cal_offsets = { 0x31, 0x32, 0x33 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x100,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_AP_VDEC,
			  .cal_offsets = { 0x37, 0x38, 0x39 } },
			{ .dt_id = MT8195_AP_IMG,
			  .cal_offsets = { 0x3a, 0x3b, 0x3c } },
			{ .dt_id = MT8195_AP_INFRA,
			  .cal_offsets = { 0x3d, 0x3e, 0x3f } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 0),
		.offset = 0x200,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_AP_CAM0,
			  .cal_offsets = { 0x43, 0x44, 0x45 } },
			{ .dt_id = MT8195_AP_CAM1,
			  .cal_offsets = { 0x46, 0x47, 0x48 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x300,
	}
};

static const struct lvts_data mt7988_lvts_ap_data = {
	.lvts_ctrl	= mt7988_lvts_ap_data_ctrl,
	.num_lvts_ctrl	= ARRAY_SIZE(mt7988_lvts_ap_data_ctrl),
	.temp_factor	= LVTS_COEFF_A_MT7988,
	.temp_offset	= LVTS_COEFF_B_MT7988,
	.gt_calib_bit_offset = 24,
};

static const struct lvts_data mt8186_lvts_data = {
	.lvts_ctrl	= mt8186_lvts_data_ctrl,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8186_lvts_data_ctrl),
	.temp_factor	= LVTS_COEFF_A_MT7988,
	.temp_offset	= LVTS_COEFF_B_MT7988,
	.gt_calib_bit_offset = 24,
};

static const struct lvts_data mt8188_lvts_mcu_data = {
	.lvts_ctrl	= mt8188_lvts_mcu_data_ctrl,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8188_lvts_mcu_data_ctrl),
	.temp_factor	= LVTS_COEFF_A_MT8195,
	.temp_offset	= LVTS_COEFF_B_MT8195,
	.gt_calib_bit_offset = 20,
};

static const struct lvts_data mt8188_lvts_ap_data = {
	.lvts_ctrl	= mt8188_lvts_ap_data_ctrl,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8188_lvts_ap_data_ctrl),
	.temp_factor	= LVTS_COEFF_A_MT8195,
	.temp_offset	= LVTS_COEFF_B_MT8195,
	.gt_calib_bit_offset = 20,
};

static const struct lvts_data mt8192_lvts_mcu_data = {
	.lvts_ctrl	= mt8192_lvts_mcu_data_ctrl,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8192_lvts_mcu_data_ctrl),
	.temp_factor	= LVTS_COEFF_A_MT8195,
	.temp_offset	= LVTS_COEFF_B_MT8195,
	.gt_calib_bit_offset = 24,
};

static const struct lvts_data mt8192_lvts_ap_data = {
	.lvts_ctrl	= mt8192_lvts_ap_data_ctrl,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8192_lvts_ap_data_ctrl),
	.temp_factor	= LVTS_COEFF_A_MT8195,
	.temp_offset	= LVTS_COEFF_B_MT8195,
	.gt_calib_bit_offset = 24,
};

static const struct lvts_data mt8195_lvts_mcu_data = {
	.lvts_ctrl	= mt8195_lvts_mcu_data_ctrl,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8195_lvts_mcu_data_ctrl),
	.temp_factor	= LVTS_COEFF_A_MT8195,
	.temp_offset	= LVTS_COEFF_B_MT8195,
	.gt_calib_bit_offset = 24,
};

static const struct lvts_data mt8195_lvts_ap_data = {
	.lvts_ctrl	= mt8195_lvts_ap_data_ctrl,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8195_lvts_ap_data_ctrl),
	.temp_factor	= LVTS_COEFF_A_MT8195,
	.temp_offset	= LVTS_COEFF_B_MT8195,
	.gt_calib_bit_offset = 24,
};

static const struct of_device_id lvts_of_match[] = {
	{ .compatible = "mediatek,mt7988-lvts-ap", .data = &mt7988_lvts_ap_data },
	{ .compatible = "mediatek,mt8186-lvts", .data = &mt8186_lvts_data },
	{ .compatible = "mediatek,mt8188-lvts-mcu", .data = &mt8188_lvts_mcu_data },
	{ .compatible = "mediatek,mt8188-lvts-ap", .data = &mt8188_lvts_ap_data },
	{ .compatible = "mediatek,mt8192-lvts-mcu", .data = &mt8192_lvts_mcu_data },
	{ .compatible = "mediatek,mt8192-lvts-ap", .data = &mt8192_lvts_ap_data },
	{ .compatible = "mediatek,mt8195-lvts-mcu", .data = &mt8195_lvts_mcu_data },
	{ .compatible = "mediatek,mt8195-lvts-ap", .data = &mt8195_lvts_ap_data },
	{},
};
MODULE_DEVICE_TABLE(of, lvts_of_match);

static const struct dev_pm_ops lvts_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(lvts_suspend, lvts_resume)
};

static struct platform_driver lvts_driver = {
	.probe = lvts_probe,
	.remove_new = lvts_remove,
	.driver = {
		.name = "mtk-lvts-thermal",
		.of_match_table = lvts_of_match,
		.pm = &lvts_pm_ops,
	},
};
module_platform_driver(lvts_driver);

MODULE_AUTHOR("Balsam CHIHI <bchihi@baylibre.com>");
MODULE_DESCRIPTION("MediaTek LVTS Thermal Driver");
MODULE_LICENSE("GPL");

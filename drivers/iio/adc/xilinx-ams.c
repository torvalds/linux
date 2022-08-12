// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AMS driver
 *
 *  Copyright (C) 2021 Xilinx, Inc.
 *
 *  Manish Narani <mnarani@xilinx.com>
 *  Rajnikant Bhojani <rajnikant.bhojani@xilinx.com>
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/devm-helpers.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

#include <linux/iio/events.h>
#include <linux/iio/iio.h>

/* AMS registers definitions */
#define AMS_ISR_0			0x010
#define AMS_ISR_1			0x014
#define AMS_IER_0			0x020
#define AMS_IER_1			0x024
#define AMS_IDR_0			0x028
#define AMS_IDR_1			0x02C
#define AMS_PS_CSTS			0x040
#define AMS_PL_CSTS			0x044

#define AMS_VCC_PSPLL0			0x060
#define AMS_VCC_PSPLL3			0x06C
#define AMS_VCCINT			0x078
#define AMS_VCCBRAM			0x07C
#define AMS_VCCAUX			0x080
#define AMS_PSDDRPLL			0x084
#define AMS_PSINTFPDDR			0x09C

#define AMS_VCC_PSPLL0_CH		48
#define AMS_VCC_PSPLL3_CH		51
#define AMS_VCCINT_CH			54
#define AMS_VCCBRAM_CH			55
#define AMS_VCCAUX_CH			56
#define AMS_PSDDRPLL_CH			57
#define AMS_PSINTFPDDR_CH		63

#define AMS_REG_CONFIG0			0x100
#define AMS_REG_CONFIG1			0x104
#define AMS_REG_CONFIG3			0x10C
#define AMS_REG_CONFIG4			0x110
#define AMS_REG_SEQ_CH0			0x120
#define AMS_REG_SEQ_CH1			0x124
#define AMS_REG_SEQ_CH2			0x118

#define AMS_VUSER0_MASK			BIT(0)
#define AMS_VUSER1_MASK			BIT(1)
#define AMS_VUSER2_MASK			BIT(2)
#define AMS_VUSER3_MASK			BIT(3)

#define AMS_TEMP			0x000
#define AMS_SUPPLY1			0x004
#define AMS_SUPPLY2			0x008
#define AMS_VP_VN			0x00C
#define AMS_VREFP			0x010
#define AMS_VREFN			0x014
#define AMS_SUPPLY3			0x018
#define AMS_SUPPLY4			0x034
#define AMS_SUPPLY5			0x038
#define AMS_SUPPLY6			0x03C
#define AMS_SUPPLY7			0x200
#define AMS_SUPPLY8			0x204
#define AMS_SUPPLY9			0x208
#define AMS_SUPPLY10			0x20C
#define AMS_VCCAMS			0x210
#define AMS_TEMP_REMOTE			0x214

#define AMS_REG_VAUX(x)			(0x40 + 4 * (x))

#define AMS_PS_RESET_VALUE		0xFFFF
#define AMS_PL_RESET_VALUE		0xFFFF

#define AMS_CONF0_CHANNEL_NUM_MASK	GENMASK(6, 0)

#define AMS_CONF1_SEQ_MASK		GENMASK(15, 12)
#define AMS_CONF1_SEQ_DEFAULT		FIELD_PREP(AMS_CONF1_SEQ_MASK, 0)
#define AMS_CONF1_SEQ_CONTINUOUS	FIELD_PREP(AMS_CONF1_SEQ_MASK, 2)
#define AMS_CONF1_SEQ_SINGLE_CHANNEL	FIELD_PREP(AMS_CONF1_SEQ_MASK, 3)

#define AMS_REG_SEQ0_MASK		GENMASK(15, 0)
#define AMS_REG_SEQ2_MASK		GENMASK(21, 16)
#define AMS_REG_SEQ1_MASK		GENMASK_ULL(37, 22)

#define AMS_PS_SEQ_MASK			GENMASK(21, 0)
#define AMS_PL_SEQ_MASK			GENMASK_ULL(59, 22)

#define AMS_ALARM_TEMP			0x140
#define AMS_ALARM_SUPPLY1		0x144
#define AMS_ALARM_SUPPLY2		0x148
#define AMS_ALARM_SUPPLY3		0x160
#define AMS_ALARM_SUPPLY4		0x164
#define AMS_ALARM_SUPPLY5		0x168
#define AMS_ALARM_SUPPLY6		0x16C
#define AMS_ALARM_SUPPLY7		0x180
#define AMS_ALARM_SUPPLY8		0x184
#define AMS_ALARM_SUPPLY9		0x188
#define AMS_ALARM_SUPPLY10		0x18C
#define AMS_ALARM_VCCAMS		0x190
#define AMS_ALARM_TEMP_REMOTE		0x194
#define AMS_ALARM_THRESHOLD_OFF_10	0x10
#define AMS_ALARM_THRESHOLD_OFF_20	0x20

#define AMS_ALARM_THR_DIRECT_MASK	BIT(1)
#define AMS_ALARM_THR_MIN		0x0000
#define AMS_ALARM_THR_MAX		(BIT(16) - 1)

#define AMS_ALARM_MASK			GENMASK_ULL(63, 0)
#define AMS_NO_OF_ALARMS		32
#define AMS_PL_ALARM_START		16
#define AMS_PL_ALARM_MASK		GENMASK(31, 16)
#define AMS_ISR0_ALARM_MASK		GENMASK(31, 0)
#define AMS_ISR1_ALARM_MASK		(GENMASK(31, 29) | GENMASK(4, 0))
#define AMS_ISR1_EOC_MASK		BIT(3)
#define AMS_ISR1_INTR_MASK		GENMASK_ULL(63, 32)
#define AMS_ISR0_ALARM_2_TO_0_MASK	GENMASK(2, 0)
#define AMS_ISR0_ALARM_6_TO_3_MASK	GENMASK(6, 3)
#define AMS_ISR0_ALARM_12_TO_7_MASK	GENMASK(13, 8)
#define AMS_CONF1_ALARM_2_TO_0_MASK	GENMASK(3, 1)
#define AMS_CONF1_ALARM_6_TO_3_MASK	GENMASK(11, 8)
#define AMS_CONF1_ALARM_12_TO_7_MASK	GENMASK(5, 0)
#define AMS_REGCFG1_ALARM_MASK  \
	(AMS_CONF1_ALARM_2_TO_0_MASK | AMS_CONF1_ALARM_6_TO_3_MASK | BIT(0))
#define AMS_REGCFG3_ALARM_MASK		AMS_CONF1_ALARM_12_TO_7_MASK

#define AMS_PS_CSTS_PS_READY		(BIT(27) | BIT(16))
#define AMS_PL_CSTS_ACCESS_MASK		BIT(1)

#define AMS_PL_MAX_FIXED_CHANNEL	10
#define AMS_PL_MAX_EXT_CHANNEL		20

#define AMS_INIT_POLL_TIME_US		200
#define AMS_INIT_TIMEOUT_US		10000
#define AMS_UNMASK_TIMEOUT_MS		500

/*
 * Following scale and offset value is derived from
 * UG580 (v1.7) December 20, 2016
 */
#define AMS_SUPPLY_SCALE_1VOLT_mV		1000
#define AMS_SUPPLY_SCALE_3VOLT_mV		3000
#define AMS_SUPPLY_SCALE_6VOLT_mV		6000
#define AMS_SUPPLY_SCALE_DIV_BIT	16

#define AMS_TEMP_SCALE			509314
#define AMS_TEMP_SCALE_DIV_BIT		16
#define AMS_TEMP_OFFSET			-((280230LL << 16) / 509314)

enum ams_alarm_bit {
	AMS_ALARM_BIT_TEMP = 0,
	AMS_ALARM_BIT_SUPPLY1 = 1,
	AMS_ALARM_BIT_SUPPLY2 = 2,
	AMS_ALARM_BIT_SUPPLY3 = 3,
	AMS_ALARM_BIT_SUPPLY4 = 4,
	AMS_ALARM_BIT_SUPPLY5 = 5,
	AMS_ALARM_BIT_SUPPLY6 = 6,
	AMS_ALARM_BIT_RESERVED = 7,
	AMS_ALARM_BIT_SUPPLY7 = 8,
	AMS_ALARM_BIT_SUPPLY8 = 9,
	AMS_ALARM_BIT_SUPPLY9 = 10,
	AMS_ALARM_BIT_SUPPLY10 = 11,
	AMS_ALARM_BIT_VCCAMS = 12,
	AMS_ALARM_BIT_TEMP_REMOTE = 13,
};

enum ams_seq {
	AMS_SEQ_VCC_PSPLL = 0,
	AMS_SEQ_VCC_PSBATT = 1,
	AMS_SEQ_VCCINT = 2,
	AMS_SEQ_VCCBRAM = 3,
	AMS_SEQ_VCCAUX = 4,
	AMS_SEQ_PSDDRPLL = 5,
	AMS_SEQ_INTDDR = 6,
};

enum ams_ps_pl_seq {
	AMS_SEQ_CALIB = 0,
	AMS_SEQ_RSVD_1 = 1,
	AMS_SEQ_RSVD_2 = 2,
	AMS_SEQ_TEST = 3,
	AMS_SEQ_RSVD_4 = 4,
	AMS_SEQ_SUPPLY4 = 5,
	AMS_SEQ_SUPPLY5 = 6,
	AMS_SEQ_SUPPLY6 = 7,
	AMS_SEQ_TEMP = 8,
	AMS_SEQ_SUPPLY2 = 9,
	AMS_SEQ_SUPPLY1 = 10,
	AMS_SEQ_VP_VN = 11,
	AMS_SEQ_VREFP = 12,
	AMS_SEQ_VREFN = 13,
	AMS_SEQ_SUPPLY3 = 14,
	AMS_SEQ_CURRENT_MON = 15,
	AMS_SEQ_SUPPLY7 = 16,
	AMS_SEQ_SUPPLY8 = 17,
	AMS_SEQ_SUPPLY9 = 18,
	AMS_SEQ_SUPPLY10 = 19,
	AMS_SEQ_VCCAMS = 20,
	AMS_SEQ_TEMP_REMOTE = 21,
	AMS_SEQ_MAX = 22
};

#define AMS_PS_SEQ_MAX		AMS_SEQ_MAX
#define AMS_SEQ(x)		(AMS_SEQ_MAX + (x))
#define PS_SEQ(x)		(x)
#define PL_SEQ(x)		(AMS_PS_SEQ_MAX + (x))
#define AMS_CTRL_SEQ_BASE	(AMS_PS_SEQ_MAX * 3)

#define AMS_CHAN_TEMP(_scan_index, _addr) { \
	.type = IIO_TEMP, \
	.indexed = 1, \
	.address = (_addr), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_OFFSET), \
	.event_spec = ams_temp_events, \
	.scan_index = _scan_index, \
	.num_event_specs = ARRAY_SIZE(ams_temp_events), \
}

#define AMS_CHAN_VOLTAGE(_scan_index, _addr, _alarm) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.address = (_addr), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_SCALE), \
	.event_spec = (_alarm) ? ams_voltage_events : NULL, \
	.scan_index = _scan_index, \
	.num_event_specs = (_alarm) ? ARRAY_SIZE(ams_voltage_events) : 0, \
}

#define AMS_PS_CHAN_TEMP(_scan_index, _addr) \
	AMS_CHAN_TEMP(PS_SEQ(_scan_index), _addr)
#define AMS_PS_CHAN_VOLTAGE(_scan_index, _addr) \
	AMS_CHAN_VOLTAGE(PS_SEQ(_scan_index), _addr, true)

#define AMS_PL_CHAN_TEMP(_scan_index, _addr) \
	AMS_CHAN_TEMP(PL_SEQ(_scan_index), _addr)
#define AMS_PL_CHAN_VOLTAGE(_scan_index, _addr, _alarm) \
	AMS_CHAN_VOLTAGE(PL_SEQ(_scan_index), _addr, _alarm)
#define AMS_PL_AUX_CHAN_VOLTAGE(_auxno) \
	AMS_CHAN_VOLTAGE(PL_SEQ(AMS_SEQ(_auxno)), AMS_REG_VAUX(_auxno), false)
#define AMS_CTRL_CHAN_VOLTAGE(_scan_index, _addr) \
	AMS_CHAN_VOLTAGE(PL_SEQ(AMS_SEQ(AMS_SEQ(_scan_index))), _addr, false)

/**
 * struct ams - This structure contains necessary state for xilinx-ams to operate
 * @base: physical base address of device
 * @ps_base: physical base address of PS device
 * @pl_base: physical base address of PL device
 * @clk: clocks associated with the device
 * @dev: pointer to device struct
 * @lock: to handle multiple user interaction
 * @intr_lock: to protect interrupt mask values
 * @alarm_mask: alarm configuration
 * @current_masked_alarm: currently masked due to alarm
 * @intr_mask: interrupt configuration
 * @ams_unmask_work: re-enables event once the event condition disappears
 *
 */
struct ams {
	void __iomem *base;
	void __iomem *ps_base;
	void __iomem *pl_base;
	struct clk *clk;
	struct device *dev;
	struct mutex lock;
	spinlock_t intr_lock;
	unsigned int alarm_mask;
	unsigned int current_masked_alarm;
	u64 intr_mask;
	struct delayed_work ams_unmask_work;
};

static inline void ams_ps_update_reg(struct ams *ams, unsigned int offset,
				     u32 mask, u32 data)
{
	u32 val, regval;

	val = readl(ams->ps_base + offset);
	regval = (val & ~mask) | (data & mask);
	writel(regval, ams->ps_base + offset);
}

static inline void ams_pl_update_reg(struct ams *ams, unsigned int offset,
				     u32 mask, u32 data)
{
	u32 val, regval;

	val = readl(ams->pl_base + offset);
	regval = (val & ~mask) | (data & mask);
	writel(regval, ams->pl_base + offset);
}

static void ams_update_intrmask(struct ams *ams, u64 mask, u64 val)
{
	u32 regval;

	ams->intr_mask = (ams->intr_mask & ~mask) | (val & mask);

	regval = ~(ams->intr_mask | ams->current_masked_alarm);
	writel(regval, ams->base + AMS_IER_0);

	regval = ~(FIELD_GET(AMS_ISR1_INTR_MASK, ams->intr_mask));
	writel(regval, ams->base + AMS_IER_1);

	regval = ams->intr_mask | ams->current_masked_alarm;
	writel(regval, ams->base + AMS_IDR_0);

	regval = FIELD_GET(AMS_ISR1_INTR_MASK, ams->intr_mask);
	writel(regval, ams->base + AMS_IDR_1);
}

static void ams_disable_all_alarms(struct ams *ams)
{
	/* disable PS module alarm */
	if (ams->ps_base) {
		ams_ps_update_reg(ams, AMS_REG_CONFIG1, AMS_REGCFG1_ALARM_MASK,
				  AMS_REGCFG1_ALARM_MASK);
		ams_ps_update_reg(ams, AMS_REG_CONFIG3, AMS_REGCFG3_ALARM_MASK,
				  AMS_REGCFG3_ALARM_MASK);
	}

	/* disable PL module alarm */
	if (ams->pl_base) {
		ams_pl_update_reg(ams, AMS_REG_CONFIG1, AMS_REGCFG1_ALARM_MASK,
				  AMS_REGCFG1_ALARM_MASK);
		ams_pl_update_reg(ams, AMS_REG_CONFIG3, AMS_REGCFG3_ALARM_MASK,
				  AMS_REGCFG3_ALARM_MASK);
	}
}

static void ams_update_ps_alarm(struct ams *ams, unsigned long alarm_mask)
{
	u32 cfg;
	u32 val;

	val = FIELD_GET(AMS_ISR0_ALARM_2_TO_0_MASK, alarm_mask);
	cfg = ~(FIELD_PREP(AMS_CONF1_ALARM_2_TO_0_MASK, val));

	val = FIELD_GET(AMS_ISR0_ALARM_6_TO_3_MASK, alarm_mask);
	cfg &= ~(FIELD_PREP(AMS_CONF1_ALARM_6_TO_3_MASK, val));

	ams_ps_update_reg(ams, AMS_REG_CONFIG1, AMS_REGCFG1_ALARM_MASK, cfg);

	val = FIELD_GET(AMS_ISR0_ALARM_12_TO_7_MASK, alarm_mask);
	cfg = ~(FIELD_PREP(AMS_CONF1_ALARM_12_TO_7_MASK, val));
	ams_ps_update_reg(ams, AMS_REG_CONFIG3, AMS_REGCFG3_ALARM_MASK, cfg);
}

static void ams_update_pl_alarm(struct ams *ams, unsigned long alarm_mask)
{
	unsigned long pl_alarm_mask;
	u32 cfg;
	u32 val;

	pl_alarm_mask = FIELD_GET(AMS_PL_ALARM_MASK, alarm_mask);

	val = FIELD_GET(AMS_ISR0_ALARM_2_TO_0_MASK, pl_alarm_mask);
	cfg = ~(FIELD_PREP(AMS_CONF1_ALARM_2_TO_0_MASK, val));

	val = FIELD_GET(AMS_ISR0_ALARM_6_TO_3_MASK, pl_alarm_mask);
	cfg &= ~(FIELD_PREP(AMS_CONF1_ALARM_6_TO_3_MASK, val));

	ams_pl_update_reg(ams, AMS_REG_CONFIG1, AMS_REGCFG1_ALARM_MASK, cfg);

	val = FIELD_GET(AMS_ISR0_ALARM_12_TO_7_MASK, pl_alarm_mask);
	cfg = ~(FIELD_PREP(AMS_CONF1_ALARM_12_TO_7_MASK, val));
	ams_pl_update_reg(ams, AMS_REG_CONFIG3, AMS_REGCFG3_ALARM_MASK, cfg);
}

static void ams_update_alarm(struct ams *ams, unsigned long alarm_mask)
{
	unsigned long flags;

	if (ams->ps_base)
		ams_update_ps_alarm(ams, alarm_mask);

	if (ams->pl_base)
		ams_update_pl_alarm(ams, alarm_mask);

	spin_lock_irqsave(&ams->intr_lock, flags);
	ams_update_intrmask(ams, AMS_ISR0_ALARM_MASK, ~alarm_mask);
	spin_unlock_irqrestore(&ams->intr_lock, flags);
}

static void ams_enable_channel_sequence(struct iio_dev *indio_dev)
{
	struct ams *ams = iio_priv(indio_dev);
	unsigned long long scan_mask;
	int i;
	u32 regval;

	/*
	 * Enable channel sequence. First 22 bits of scan_mask represent
	 * PS channels, and next remaining bits represent PL channels.
	 */

	/* Run calibration of PS & PL as part of the sequence */
	scan_mask = BIT(0) | BIT(AMS_PS_SEQ_MAX);
	for (i = 0; i < indio_dev->num_channels; i++)
		scan_mask |= BIT_ULL(indio_dev->channels[i].scan_index);

	if (ams->ps_base) {
		/* put sysmon in a soft reset to change the sequence */
		ams_ps_update_reg(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
				  AMS_CONF1_SEQ_DEFAULT);

		/* configure basic channels */
		regval = FIELD_GET(AMS_REG_SEQ0_MASK, scan_mask);
		writel(regval, ams->ps_base + AMS_REG_SEQ_CH0);

		regval = FIELD_GET(AMS_REG_SEQ2_MASK, scan_mask);
		writel(regval, ams->ps_base + AMS_REG_SEQ_CH2);

		/* set continuous sequence mode */
		ams_ps_update_reg(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
				  AMS_CONF1_SEQ_CONTINUOUS);
	}

	if (ams->pl_base) {
		/* put sysmon in a soft reset to change the sequence */
		ams_pl_update_reg(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
				  AMS_CONF1_SEQ_DEFAULT);

		/* configure basic channels */
		scan_mask = FIELD_GET(AMS_PL_SEQ_MASK, scan_mask);

		regval = FIELD_GET(AMS_REG_SEQ0_MASK, scan_mask);
		writel(regval, ams->pl_base + AMS_REG_SEQ_CH0);

		regval = FIELD_GET(AMS_REG_SEQ1_MASK, scan_mask);
		writel(regval, ams->pl_base + AMS_REG_SEQ_CH1);

		regval = FIELD_GET(AMS_REG_SEQ2_MASK, scan_mask);
		writel(regval, ams->pl_base + AMS_REG_SEQ_CH2);

		/* set continuous sequence mode */
		ams_pl_update_reg(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
				  AMS_CONF1_SEQ_CONTINUOUS);
	}
}

static int ams_init_device(struct ams *ams)
{
	u32 expect = AMS_PS_CSTS_PS_READY;
	u32 reg, value;
	int ret;

	/* reset AMS */
	if (ams->ps_base) {
		writel(AMS_PS_RESET_VALUE, ams->ps_base + AMS_VP_VN);

		ret = readl_poll_timeout(ams->base + AMS_PS_CSTS, reg, (reg & expect),
					 AMS_INIT_POLL_TIME_US, AMS_INIT_TIMEOUT_US);
		if (ret)
			return ret;

		/* put sysmon in a default state */
		ams_ps_update_reg(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
				  AMS_CONF1_SEQ_DEFAULT);
	}

	if (ams->pl_base) {
		value = readl(ams->base + AMS_PL_CSTS);
		if (value == 0)
			return 0;

		writel(AMS_PL_RESET_VALUE, ams->pl_base + AMS_VP_VN);

		/* put sysmon in a default state */
		ams_pl_update_reg(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
				  AMS_CONF1_SEQ_DEFAULT);
	}

	ams_disable_all_alarms(ams);

	/* Disable interrupt */
	ams_update_intrmask(ams, AMS_ALARM_MASK, AMS_ALARM_MASK);

	/* Clear any pending interrupt */
	writel(AMS_ISR0_ALARM_MASK, ams->base + AMS_ISR_0);
	writel(AMS_ISR1_ALARM_MASK, ams->base + AMS_ISR_1);

	return 0;
}

static int ams_enable_single_channel(struct ams *ams, unsigned int offset)
{
	u8 channel_num;

	switch (offset) {
	case AMS_VCC_PSPLL0:
		channel_num = AMS_VCC_PSPLL0_CH;
		break;
	case AMS_VCC_PSPLL3:
		channel_num = AMS_VCC_PSPLL3_CH;
		break;
	case AMS_VCCINT:
		channel_num = AMS_VCCINT_CH;
		break;
	case AMS_VCCBRAM:
		channel_num = AMS_VCCBRAM_CH;
		break;
	case AMS_VCCAUX:
		channel_num = AMS_VCCAUX_CH;
		break;
	case AMS_PSDDRPLL:
		channel_num = AMS_PSDDRPLL_CH;
		break;
	case AMS_PSINTFPDDR:
		channel_num = AMS_PSINTFPDDR_CH;
		break;
	default:
		return -EINVAL;
	}

	/* put sysmon in a soft reset to change the sequence */
	ams_ps_update_reg(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
			  AMS_CONF1_SEQ_DEFAULT);

	/* write the channel number */
	ams_ps_update_reg(ams, AMS_REG_CONFIG0, AMS_CONF0_CHANNEL_NUM_MASK,
			  channel_num);

	/* set single channel, sequencer off mode */
	ams_ps_update_reg(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
			  AMS_CONF1_SEQ_SINGLE_CHANNEL);

	return 0;
}

static int ams_read_vcc_reg(struct ams *ams, unsigned int offset, u32 *data)
{
	u32 expect = AMS_ISR1_EOC_MASK;
	u32 reg;
	int ret;

	ret = ams_enable_single_channel(ams, offset);
	if (ret)
		return ret;

	/* clear end-of-conversion flag, wait for next conversion to complete */
	writel(expect, ams->base + AMS_ISR_1);
	ret = readl_poll_timeout(ams->base + AMS_ISR_1, reg, (reg & expect),
				 AMS_INIT_POLL_TIME_US, AMS_INIT_TIMEOUT_US);
	if (ret)
		return ret;

	*data = readl(ams->base + offset);

	return 0;
}

static int ams_get_ps_scale(int address)
{
	int val;

	switch (address) {
	case AMS_SUPPLY1:
	case AMS_SUPPLY2:
	case AMS_SUPPLY3:
	case AMS_SUPPLY4:
	case AMS_SUPPLY9:
	case AMS_SUPPLY10:
	case AMS_VCCAMS:
		val = AMS_SUPPLY_SCALE_3VOLT_mV;
		break;
	case AMS_SUPPLY5:
	case AMS_SUPPLY6:
	case AMS_SUPPLY7:
	case AMS_SUPPLY8:
		val = AMS_SUPPLY_SCALE_6VOLT_mV;
		break;
	default:
		val = AMS_SUPPLY_SCALE_1VOLT_mV;
		break;
	}

	return val;
}

static int ams_get_pl_scale(struct ams *ams, int address)
{
	int val, regval;

	switch (address) {
	case AMS_SUPPLY1:
	case AMS_SUPPLY2:
	case AMS_SUPPLY3:
	case AMS_SUPPLY4:
	case AMS_SUPPLY5:
	case AMS_SUPPLY6:
	case AMS_VCCAMS:
	case AMS_VREFP:
	case AMS_VREFN:
		val = AMS_SUPPLY_SCALE_3VOLT_mV;
		break;
	case AMS_SUPPLY7:
		regval = readl(ams->pl_base + AMS_REG_CONFIG4);
		if (FIELD_GET(AMS_VUSER0_MASK, regval))
			val = AMS_SUPPLY_SCALE_6VOLT_mV;
		else
			val = AMS_SUPPLY_SCALE_3VOLT_mV;
		break;
	case AMS_SUPPLY8:
		regval = readl(ams->pl_base + AMS_REG_CONFIG4);
		if (FIELD_GET(AMS_VUSER1_MASK, regval))
			val = AMS_SUPPLY_SCALE_6VOLT_mV;
		else
			val = AMS_SUPPLY_SCALE_3VOLT_mV;
		break;
	case AMS_SUPPLY9:
		regval = readl(ams->pl_base + AMS_REG_CONFIG4);
		if (FIELD_GET(AMS_VUSER2_MASK, regval))
			val = AMS_SUPPLY_SCALE_6VOLT_mV;
		else
			val = AMS_SUPPLY_SCALE_3VOLT_mV;
		break;
	case AMS_SUPPLY10:
		regval = readl(ams->pl_base + AMS_REG_CONFIG4);
		if (FIELD_GET(AMS_VUSER3_MASK, regval))
			val = AMS_SUPPLY_SCALE_6VOLT_mV;
		else
			val = AMS_SUPPLY_SCALE_3VOLT_mV;
		break;
	case AMS_VP_VN:
	case AMS_REG_VAUX(0) ... AMS_REG_VAUX(15):
		val = AMS_SUPPLY_SCALE_1VOLT_mV;
		break;
	default:
		val = AMS_SUPPLY_SCALE_1VOLT_mV;
		break;
	}

	return val;
}

static int ams_get_ctrl_scale(int address)
{
	int val;

	switch (address) {
	case AMS_VCC_PSPLL0:
	case AMS_VCC_PSPLL3:
	case AMS_VCCINT:
	case AMS_VCCBRAM:
	case AMS_VCCAUX:
	case AMS_PSDDRPLL:
	case AMS_PSINTFPDDR:
		val = AMS_SUPPLY_SCALE_3VOLT_mV;
		break;
	default:
		val = AMS_SUPPLY_SCALE_1VOLT_mV;
		break;
	}

	return val;
}

static int ams_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val, int *val2, long mask)
{
	struct ams *ams = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&ams->lock);
		if (chan->scan_index >= AMS_CTRL_SEQ_BASE) {
			ret = ams_read_vcc_reg(ams, chan->address, val);
			if (ret)
				goto unlock_mutex;
			ams_enable_channel_sequence(indio_dev);
		} else if (chan->scan_index >= AMS_PS_SEQ_MAX)
			*val = readl(ams->pl_base + chan->address);
		else
			*val = readl(ams->ps_base + chan->address);

		ret = IIO_VAL_INT;
unlock_mutex:
		mutex_unlock(&ams->lock);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			if (chan->scan_index < AMS_PS_SEQ_MAX)
				*val = ams_get_ps_scale(chan->address);
			else if (chan->scan_index >= AMS_PS_SEQ_MAX &&
				 chan->scan_index < AMS_CTRL_SEQ_BASE)
				*val = ams_get_pl_scale(ams, chan->address);
			else
				*val = ams_get_ctrl_scale(chan->address);

			*val2 = AMS_SUPPLY_SCALE_DIV_BIT;
			return IIO_VAL_FRACTIONAL_LOG2;
		case IIO_TEMP:
			*val = AMS_TEMP_SCALE;
			*val2 = AMS_TEMP_SCALE_DIV_BIT;
			return IIO_VAL_FRACTIONAL_LOG2;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		/* Only the temperature channel has an offset */
		*val = AMS_TEMP_OFFSET;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ams_get_alarm_offset(int scan_index, enum iio_event_direction dir)
{
	int offset;

	if (scan_index >= AMS_PS_SEQ_MAX)
		scan_index -= AMS_PS_SEQ_MAX;

	if (dir == IIO_EV_DIR_FALLING) {
		if (scan_index < AMS_SEQ_SUPPLY7)
			offset = AMS_ALARM_THRESHOLD_OFF_10;
		else
			offset = AMS_ALARM_THRESHOLD_OFF_20;
	} else {
		offset = 0;
	}

	switch (scan_index) {
	case AMS_SEQ_TEMP:
		return AMS_ALARM_TEMP + offset;
	case AMS_SEQ_SUPPLY1:
		return AMS_ALARM_SUPPLY1 + offset;
	case AMS_SEQ_SUPPLY2:
		return AMS_ALARM_SUPPLY2 + offset;
	case AMS_SEQ_SUPPLY3:
		return AMS_ALARM_SUPPLY3 + offset;
	case AMS_SEQ_SUPPLY4:
		return AMS_ALARM_SUPPLY4 + offset;
	case AMS_SEQ_SUPPLY5:
		return AMS_ALARM_SUPPLY5 + offset;
	case AMS_SEQ_SUPPLY6:
		return AMS_ALARM_SUPPLY6 + offset;
	case AMS_SEQ_SUPPLY7:
		return AMS_ALARM_SUPPLY7 + offset;
	case AMS_SEQ_SUPPLY8:
		return AMS_ALARM_SUPPLY8 + offset;
	case AMS_SEQ_SUPPLY9:
		return AMS_ALARM_SUPPLY9 + offset;
	case AMS_SEQ_SUPPLY10:
		return AMS_ALARM_SUPPLY10 + offset;
	case AMS_SEQ_VCCAMS:
		return AMS_ALARM_VCCAMS + offset;
	case AMS_SEQ_TEMP_REMOTE:
		return AMS_ALARM_TEMP_REMOTE + offset;
	default:
		return 0;
	}
}

static const struct iio_chan_spec *ams_event_to_channel(struct iio_dev *dev,
							u32 event)
{
	int scan_index = 0, i;

	if (event >= AMS_PL_ALARM_START) {
		event -= AMS_PL_ALARM_START;
		scan_index = AMS_PS_SEQ_MAX;
	}

	switch (event) {
	case AMS_ALARM_BIT_TEMP:
		scan_index += AMS_SEQ_TEMP;
		break;
	case AMS_ALARM_BIT_SUPPLY1:
		scan_index += AMS_SEQ_SUPPLY1;
		break;
	case AMS_ALARM_BIT_SUPPLY2:
		scan_index += AMS_SEQ_SUPPLY2;
		break;
	case AMS_ALARM_BIT_SUPPLY3:
		scan_index += AMS_SEQ_SUPPLY3;
		break;
	case AMS_ALARM_BIT_SUPPLY4:
		scan_index += AMS_SEQ_SUPPLY4;
		break;
	case AMS_ALARM_BIT_SUPPLY5:
		scan_index += AMS_SEQ_SUPPLY5;
		break;
	case AMS_ALARM_BIT_SUPPLY6:
		scan_index += AMS_SEQ_SUPPLY6;
		break;
	case AMS_ALARM_BIT_SUPPLY7:
		scan_index += AMS_SEQ_SUPPLY7;
		break;
	case AMS_ALARM_BIT_SUPPLY8:
		scan_index += AMS_SEQ_SUPPLY8;
		break;
	case AMS_ALARM_BIT_SUPPLY9:
		scan_index += AMS_SEQ_SUPPLY9;
		break;
	case AMS_ALARM_BIT_SUPPLY10:
		scan_index += AMS_SEQ_SUPPLY10;
		break;
	case AMS_ALARM_BIT_VCCAMS:
		scan_index += AMS_SEQ_VCCAMS;
		break;
	case AMS_ALARM_BIT_TEMP_REMOTE:
		scan_index += AMS_SEQ_TEMP_REMOTE;
		break;
	default:
		break;
	}

	for (i = 0; i < dev->num_channels; i++)
		if (dev->channels[i].scan_index == scan_index)
			break;

	return &dev->channels[i];
}

static int ams_get_alarm_mask(int scan_index)
{
	int bit = 0;

	if (scan_index >= AMS_PS_SEQ_MAX) {
		bit = AMS_PL_ALARM_START;
		scan_index -= AMS_PS_SEQ_MAX;
	}

	switch (scan_index) {
	case AMS_SEQ_TEMP:
		return BIT(AMS_ALARM_BIT_TEMP + bit);
	case AMS_SEQ_SUPPLY1:
		return BIT(AMS_ALARM_BIT_SUPPLY1 + bit);
	case AMS_SEQ_SUPPLY2:
		return BIT(AMS_ALARM_BIT_SUPPLY2 + bit);
	case AMS_SEQ_SUPPLY3:
		return BIT(AMS_ALARM_BIT_SUPPLY3 + bit);
	case AMS_SEQ_SUPPLY4:
		return BIT(AMS_ALARM_BIT_SUPPLY4 + bit);
	case AMS_SEQ_SUPPLY5:
		return BIT(AMS_ALARM_BIT_SUPPLY5 + bit);
	case AMS_SEQ_SUPPLY6:
		return BIT(AMS_ALARM_BIT_SUPPLY6 + bit);
	case AMS_SEQ_SUPPLY7:
		return BIT(AMS_ALARM_BIT_SUPPLY7 + bit);
	case AMS_SEQ_SUPPLY8:
		return BIT(AMS_ALARM_BIT_SUPPLY8 + bit);
	case AMS_SEQ_SUPPLY9:
		return BIT(AMS_ALARM_BIT_SUPPLY9 + bit);
	case AMS_SEQ_SUPPLY10:
		return BIT(AMS_ALARM_BIT_SUPPLY10 + bit);
	case AMS_SEQ_VCCAMS:
		return BIT(AMS_ALARM_BIT_VCCAMS + bit);
	case AMS_SEQ_TEMP_REMOTE:
		return BIT(AMS_ALARM_BIT_TEMP_REMOTE + bit);
	default:
		return 0;
	}
}

static int ams_read_event_config(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir)
{
	struct ams *ams = iio_priv(indio_dev);

	return !!(ams->alarm_mask & ams_get_alarm_mask(chan->scan_index));
}

static int ams_write_event_config(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  enum iio_event_type type,
				  enum iio_event_direction dir,
				  int state)
{
	struct ams *ams = iio_priv(indio_dev);
	unsigned int alarm;

	alarm = ams_get_alarm_mask(chan->scan_index);

	mutex_lock(&ams->lock);

	if (state)
		ams->alarm_mask |= alarm;
	else
		ams->alarm_mask &= ~alarm;

	ams_update_alarm(ams, ams->alarm_mask);

	mutex_unlock(&ams->lock);

	return 0;
}

static int ams_read_event_value(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				enum iio_event_info info, int *val, int *val2)
{
	struct ams *ams = iio_priv(indio_dev);
	unsigned int offset = ams_get_alarm_offset(chan->scan_index, dir);

	mutex_lock(&ams->lock);

	if (chan->scan_index >= AMS_PS_SEQ_MAX)
		*val = readl(ams->pl_base + offset);
	else
		*val = readl(ams->ps_base + offset);

	mutex_unlock(&ams->lock);

	return IIO_VAL_INT;
}

static int ams_write_event_value(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir,
				 enum iio_event_info info, int val, int val2)
{
	struct ams *ams = iio_priv(indio_dev);
	unsigned int offset;

	mutex_lock(&ams->lock);

	/* Set temperature channel threshold to direct threshold */
	if (chan->type == IIO_TEMP) {
		offset = ams_get_alarm_offset(chan->scan_index, IIO_EV_DIR_FALLING);

		if (chan->scan_index >= AMS_PS_SEQ_MAX)
			ams_pl_update_reg(ams, offset,
					  AMS_ALARM_THR_DIRECT_MASK,
					  AMS_ALARM_THR_DIRECT_MASK);
		else
			ams_ps_update_reg(ams, offset,
					  AMS_ALARM_THR_DIRECT_MASK,
					  AMS_ALARM_THR_DIRECT_MASK);
	}

	offset = ams_get_alarm_offset(chan->scan_index, dir);
	if (chan->scan_index >= AMS_PS_SEQ_MAX)
		writel(val, ams->pl_base + offset);
	else
		writel(val, ams->ps_base + offset);

	mutex_unlock(&ams->lock);

	return 0;
}

static void ams_handle_event(struct iio_dev *indio_dev, u32 event)
{
	const struct iio_chan_spec *chan;

	chan = ams_event_to_channel(indio_dev, event);

	if (chan->type == IIO_TEMP) {
		/*
		 * The temperature channel only supports over-temperature
		 * events.
		 */
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(chan->type, chan->channel,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       iio_get_time_ns(indio_dev));
	} else {
		/*
		 * For other channels we don't know whether it is a upper or
		 * lower threshold event. Userspace will have to check the
		 * channel value if it wants to know.
		 */
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(chan->type, chan->channel,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
			       iio_get_time_ns(indio_dev));
	}
}

static void ams_handle_events(struct iio_dev *indio_dev, unsigned long events)
{
	unsigned int bit;

	for_each_set_bit(bit, &events, AMS_NO_OF_ALARMS)
		ams_handle_event(indio_dev, bit);
}

/**
 * ams_unmask_worker - ams alarm interrupt unmask worker
 * @work: work to be done
 *
 * The ZynqMP threshold interrupts are level sensitive. Since we can't make the
 * threshold condition go way from within the interrupt handler, this means as
 * soon as a threshold condition is present we would enter the interrupt handler
 * again and again. To work around this we mask all active threshold interrupts
 * in the interrupt handler and start a timer. In this timer we poll the
 * interrupt status and only if the interrupt is inactive we unmask it again.
 */
static void ams_unmask_worker(struct work_struct *work)
{
	struct ams *ams = container_of(work, struct ams, ams_unmask_work.work);
	unsigned int status, unmask;

	spin_lock_irq(&ams->intr_lock);

	status = readl(ams->base + AMS_ISR_0);

	/* Clear those bits which are not active anymore */
	unmask = (ams->current_masked_alarm ^ status) & ams->current_masked_alarm;

	/* Clear status of disabled alarm */
	unmask |= ams->intr_mask;

	ams->current_masked_alarm &= status;

	/* Also clear those which are masked out anyway */
	ams->current_masked_alarm &= ~ams->intr_mask;

	/* Clear the interrupts before we unmask them */
	writel(unmask, ams->base + AMS_ISR_0);

	ams_update_intrmask(ams, ~AMS_ALARM_MASK, ~AMS_ALARM_MASK);

	spin_unlock_irq(&ams->intr_lock);

	/* If still pending some alarm re-trigger the timer */
	if (ams->current_masked_alarm)
		schedule_delayed_work(&ams->ams_unmask_work,
				      msecs_to_jiffies(AMS_UNMASK_TIMEOUT_MS));
}

static irqreturn_t ams_irq(int irq, void *data)
{
	struct iio_dev *indio_dev = data;
	struct ams *ams = iio_priv(indio_dev);
	u32 isr0;

	spin_lock(&ams->intr_lock);

	isr0 = readl(ams->base + AMS_ISR_0);

	/* Only process alarms that are not masked */
	isr0 &= ~((ams->intr_mask & AMS_ISR0_ALARM_MASK) | ams->current_masked_alarm);
	if (!isr0) {
		spin_unlock(&ams->intr_lock);
		return IRQ_NONE;
	}

	/* Clear interrupt */
	writel(isr0, ams->base + AMS_ISR_0);

	/* Mask the alarm interrupts until cleared */
	ams->current_masked_alarm |= isr0;
	ams_update_intrmask(ams, ~AMS_ALARM_MASK, ~AMS_ALARM_MASK);

	ams_handle_events(indio_dev, isr0);

	schedule_delayed_work(&ams->ams_unmask_work,
			      msecs_to_jiffies(AMS_UNMASK_TIMEOUT_MS));

	spin_unlock(&ams->intr_lock);

	return IRQ_HANDLED;
}

static const struct iio_event_spec ams_temp_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) | BIT(IIO_EV_INFO_VALUE),
	},
};

static const struct iio_event_spec ams_voltage_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec ams_ps_channels[] = {
	AMS_PS_CHAN_TEMP(AMS_SEQ_TEMP, AMS_TEMP),
	AMS_PS_CHAN_TEMP(AMS_SEQ_TEMP_REMOTE, AMS_TEMP_REMOTE),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY1, AMS_SUPPLY1),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY2, AMS_SUPPLY2),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY3, AMS_SUPPLY3),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY4, AMS_SUPPLY4),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY5, AMS_SUPPLY5),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY6, AMS_SUPPLY6),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY7, AMS_SUPPLY7),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY8, AMS_SUPPLY8),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY9, AMS_SUPPLY9),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY10, AMS_SUPPLY10),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_VCCAMS, AMS_VCCAMS),
};

static const struct iio_chan_spec ams_pl_channels[] = {
	AMS_PL_CHAN_TEMP(AMS_SEQ_TEMP, AMS_TEMP),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY1, AMS_SUPPLY1, true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY2, AMS_SUPPLY2, true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_VREFP, AMS_VREFP, false),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_VREFN, AMS_VREFN, false),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY3, AMS_SUPPLY3, true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY4, AMS_SUPPLY4, true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY5, AMS_SUPPLY5, true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY6, AMS_SUPPLY6, true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_VCCAMS, AMS_VCCAMS, true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_VP_VN, AMS_VP_VN, false),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY7, AMS_SUPPLY7, true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY8, AMS_SUPPLY8, true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY9, AMS_SUPPLY9, true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY10, AMS_SUPPLY10, true),
	AMS_PL_AUX_CHAN_VOLTAGE(0),
	AMS_PL_AUX_CHAN_VOLTAGE(1),
	AMS_PL_AUX_CHAN_VOLTAGE(2),
	AMS_PL_AUX_CHAN_VOLTAGE(3),
	AMS_PL_AUX_CHAN_VOLTAGE(4),
	AMS_PL_AUX_CHAN_VOLTAGE(5),
	AMS_PL_AUX_CHAN_VOLTAGE(6),
	AMS_PL_AUX_CHAN_VOLTAGE(7),
	AMS_PL_AUX_CHAN_VOLTAGE(8),
	AMS_PL_AUX_CHAN_VOLTAGE(9),
	AMS_PL_AUX_CHAN_VOLTAGE(10),
	AMS_PL_AUX_CHAN_VOLTAGE(11),
	AMS_PL_AUX_CHAN_VOLTAGE(12),
	AMS_PL_AUX_CHAN_VOLTAGE(13),
	AMS_PL_AUX_CHAN_VOLTAGE(14),
	AMS_PL_AUX_CHAN_VOLTAGE(15),
};

static const struct iio_chan_spec ams_ctrl_channels[] = {
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_VCC_PSPLL, AMS_VCC_PSPLL0),
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_VCC_PSBATT, AMS_VCC_PSPLL3),
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_VCCINT, AMS_VCCINT),
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_VCCBRAM, AMS_VCCBRAM),
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_VCCAUX, AMS_VCCAUX),
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_PSDDRPLL, AMS_PSDDRPLL),
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_INTDDR, AMS_PSINTFPDDR),
};

static int ams_get_ext_chan(struct fwnode_handle *chan_node,
			    struct iio_chan_spec *channels, int num_channels)
{
	struct iio_chan_spec *chan;
	struct fwnode_handle *child;
	unsigned int reg, ext_chan;
	int ret;

	fwnode_for_each_child_node(chan_node, child) {
		ret = fwnode_property_read_u32(child, "reg", &reg);
		if (ret || reg > AMS_PL_MAX_EXT_CHANNEL + 30)
			continue;

		chan = &channels[num_channels];
		ext_chan = reg + AMS_PL_MAX_FIXED_CHANNEL - 30;
		memcpy(chan, &ams_pl_channels[ext_chan], sizeof(*channels));

		if (fwnode_property_read_bool(child, "xlnx,bipolar"))
			chan->scan_type.sign = 's';

		num_channels++;
	}

	return num_channels;
}

static void ams_iounmap_ps(void *data)
{
	struct ams *ams = data;

	iounmap(ams->ps_base);
}

static void ams_iounmap_pl(void *data)
{
	struct ams *ams = data;

	iounmap(ams->pl_base);
}

static int ams_init_module(struct iio_dev *indio_dev,
			   struct fwnode_handle *fwnode,
			   struct iio_chan_spec *channels)
{
	struct device *dev = indio_dev->dev.parent;
	struct ams *ams = iio_priv(indio_dev);
	int num_channels = 0;
	int ret;

	if (fwnode_property_match_string(fwnode, "compatible",
					 "xlnx,zynqmp-ams-ps") == 0) {
		ams->ps_base = fwnode_iomap(fwnode, 0);
		if (!ams->ps_base)
			return -ENXIO;
		ret = devm_add_action_or_reset(dev, ams_iounmap_ps, ams);
		if (ret < 0)
			return ret;

		/* add PS channels to iio device channels */
		memcpy(channels, ams_ps_channels, sizeof(ams_ps_channels));
		num_channels = ARRAY_SIZE(ams_ps_channels);
	} else if (fwnode_property_match_string(fwnode, "compatible",
						"xlnx,zynqmp-ams-pl") == 0) {
		ams->pl_base = fwnode_iomap(fwnode, 0);
		if (!ams->pl_base)
			return -ENXIO;

		ret = devm_add_action_or_reset(dev, ams_iounmap_pl, ams);
		if (ret < 0)
			return ret;

		/* Copy only first 10 fix channels */
		memcpy(channels, ams_pl_channels, AMS_PL_MAX_FIXED_CHANNEL * sizeof(*channels));
		num_channels += AMS_PL_MAX_FIXED_CHANNEL;
		num_channels = ams_get_ext_chan(fwnode, channels,
						num_channels);
	} else if (fwnode_property_match_string(fwnode, "compatible",
						"xlnx,zynqmp-ams") == 0) {
		/* add AMS channels to iio device channels */
		memcpy(channels, ams_ctrl_channels, sizeof(ams_ctrl_channels));
		num_channels += ARRAY_SIZE(ams_ctrl_channels);
	} else {
		return -EINVAL;
	}

	return num_channels;
}

static int ams_parse_firmware(struct iio_dev *indio_dev)
{
	struct ams *ams = iio_priv(indio_dev);
	struct iio_chan_spec *ams_channels, *dev_channels;
	struct device *dev = indio_dev->dev.parent;
	struct fwnode_handle *child = NULL;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	size_t ams_size, dev_size;
	int ret, ch_cnt = 0, i, rising_off, falling_off;
	unsigned int num_channels = 0;

	ams_size = ARRAY_SIZE(ams_ps_channels) + ARRAY_SIZE(ams_pl_channels) +
		ARRAY_SIZE(ams_ctrl_channels);

	/* Initialize buffer for channel specification */
	ams_channels = devm_kcalloc(dev, ams_size, sizeof(*ams_channels), GFP_KERNEL);
	if (!ams_channels)
		return -ENOMEM;

	if (fwnode_device_is_available(fwnode)) {
		ret = ams_init_module(indio_dev, fwnode, ams_channels);
		if (ret < 0)
			return ret;

		num_channels += ret;
	}

	fwnode_for_each_child_node(fwnode, child) {
		if (fwnode_device_is_available(child)) {
			ret = ams_init_module(indio_dev, child, ams_channels + num_channels);
			if (ret < 0) {
				fwnode_handle_put(child);
				return ret;
			}

			num_channels += ret;
		}
	}

	for (i = 0; i < num_channels; i++) {
		ams_channels[i].channel = ch_cnt++;

		if (ams_channels[i].scan_index < AMS_CTRL_SEQ_BASE) {
			/* set threshold to max and min for each channel */
			falling_off =
				ams_get_alarm_offset(ams_channels[i].scan_index,
						     IIO_EV_DIR_FALLING);
			rising_off =
				ams_get_alarm_offset(ams_channels[i].scan_index,
						     IIO_EV_DIR_RISING);
			if (ams_channels[i].scan_index >= AMS_PS_SEQ_MAX) {
				writel(AMS_ALARM_THR_MIN,
				       ams->pl_base + falling_off);
				writel(AMS_ALARM_THR_MAX,
				       ams->pl_base + rising_off);
			} else {
				writel(AMS_ALARM_THR_MIN,
				       ams->ps_base + falling_off);
				writel(AMS_ALARM_THR_MAX,
				       ams->ps_base + rising_off);
			}
		}
	}

	dev_size = array_size(sizeof(*dev_channels), num_channels);
	if (dev_size == SIZE_MAX)
		return -ENOMEM;

	dev_channels = devm_krealloc(dev, ams_channels, dev_size, GFP_KERNEL);
	if (!dev_channels)
		ret = -ENOMEM;

	indio_dev->channels = dev_channels;
	indio_dev->num_channels = num_channels;

	return 0;
}

static const struct iio_info iio_ams_info = {
	.read_raw = &ams_read_raw,
	.read_event_config = &ams_read_event_config,
	.write_event_config = &ams_write_event_config,
	.read_event_value = &ams_read_event_value,
	.write_event_value = &ams_write_event_value,
};

static const struct of_device_id ams_of_match_table[] = {
	{ .compatible = "xlnx,zynqmp-ams" },
	{ }
};
MODULE_DEVICE_TABLE(of, ams_of_match_table);

static void ams_clk_disable_unprepare(void *data)
{
	clk_disable_unprepare(data);
}

static int ams_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct ams *ams;
	int ret;
	int irq;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*ams));
	if (!indio_dev)
		return -ENOMEM;

	ams = iio_priv(indio_dev);
	mutex_init(&ams->lock);
	spin_lock_init(&ams->intr_lock);

	indio_dev->name = "xilinx-ams";

	indio_dev->info = &iio_ams_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ams->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ams->base))
		return PTR_ERR(ams->base);

	ams->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ams->clk))
		return PTR_ERR(ams->clk);

	ret = clk_prepare_enable(ams->clk);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(&pdev->dev, ams_clk_disable_unprepare, ams->clk);
	if (ret < 0)
		return ret;

	ret = devm_delayed_work_autocancel(&pdev->dev, &ams->ams_unmask_work,
					   ams_unmask_worker);
	if (ret < 0)
		return ret;

	ret = ams_parse_firmware(indio_dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failure in parsing DT\n");

	ret = ams_init_device(ams);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to initialize AMS\n");

	ams_enable_channel_sequence(indio_dev);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, &ams_irq, 0, "ams-irq",
			       indio_dev);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "failed to register interrupt\n");

	platform_set_drvdata(pdev, indio_dev);

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static int ams_suspend(struct device *dev)
{
	struct ams *ams = iio_priv(dev_get_drvdata(dev));

	clk_disable_unprepare(ams->clk);

	return 0;
}

static int ams_resume(struct device *dev)
{
	struct ams *ams = iio_priv(dev_get_drvdata(dev));

	return clk_prepare_enable(ams->clk);
}

static DEFINE_SIMPLE_DEV_PM_OPS(ams_pm_ops, ams_suspend, ams_resume);

static struct platform_driver ams_driver = {
	.probe = ams_probe,
	.driver = {
		.name = "xilinx-ams",
		.pm = pm_sleep_ptr(&ams_pm_ops),
		.of_match_table = ams_of_match_table,
	},
};
module_platform_driver(ams_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Xilinx, Inc.");

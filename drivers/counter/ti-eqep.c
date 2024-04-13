// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 David Lechner <david@lechnology.com>
 *
 * Counter driver for Texas Instruments Enhanced Quadrature Encoder Pulse (eQEP)
 */

#include <linux/bitops.h>
#include <linux/counter.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/types.h>

/* 32-bit registers */
#define QPOSCNT		0x0
#define QPOSINIT	0x4
#define QPOSMAX		0x8
#define QPOSCMP		0xc
#define QPOSILAT	0x10
#define QPOSSLAT	0x14
#define QPOSLAT		0x18
#define QUTMR		0x1c
#define QUPRD		0x20

/* 16-bit registers */
#define QWDTMR		0x0	/* 0x24 */
#define QWDPRD		0x2	/* 0x26 */
#define QDECCTL		0x4	/* 0x28 */
#define QEPCTL		0x6	/* 0x2a */
#define QCAPCTL		0x8	/* 0x2c */
#define QPOSCTL		0xa	/* 0x2e */
#define QEINT		0xc	/* 0x30 */
#define QFLG		0xe	/* 0x32 */
#define QCLR		0x10	/* 0x34 */
#define QFRC		0x12	/* 0x36 */
#define QEPSTS		0x14	/* 0x38 */
#define QCTMR		0x16	/* 0x3a */
#define QCPRD		0x18	/* 0x3c */
#define QCTMRLAT	0x1a	/* 0x3e */
#define QCPRDLAT	0x1c	/* 0x40 */

#define QDECCTL_QSRC_SHIFT	14
#define QDECCTL_QSRC		GENMASK(15, 14)
#define QDECCTL_SOEN		BIT(13)
#define QDECCTL_SPSEL		BIT(12)
#define QDECCTL_XCR		BIT(11)
#define QDECCTL_SWAP		BIT(10)
#define QDECCTL_IGATE		BIT(9)
#define QDECCTL_QAP		BIT(8)
#define QDECCTL_QBP		BIT(7)
#define QDECCTL_QIP		BIT(6)
#define QDECCTL_QSP		BIT(5)

#define QEPCTL_FREE_SOFT	GENMASK(15, 14)
#define QEPCTL_PCRM		GENMASK(13, 12)
#define QEPCTL_SEI		GENMASK(11, 10)
#define QEPCTL_IEI		GENMASK(9, 8)
#define QEPCTL_SWI		BIT(7)
#define QEPCTL_SEL		BIT(6)
#define QEPCTL_IEL		GENMASK(5, 4)
#define QEPCTL_PHEN		BIT(3)
#define QEPCTL_QCLM		BIT(2)
#define QEPCTL_UTE		BIT(1)
#define QEPCTL_WDE		BIT(0)

/* EQEP Inputs */
enum {
	TI_EQEP_SIGNAL_QEPA,	/* QEPA/XCLK */
	TI_EQEP_SIGNAL_QEPB,	/* QEPB/XDIR */
};

/* Position Counter Input Modes */
enum ti_eqep_count_func {
	TI_EQEP_COUNT_FUNC_QUAD_COUNT,
	TI_EQEP_COUNT_FUNC_DIR_COUNT,
	TI_EQEP_COUNT_FUNC_UP_COUNT,
	TI_EQEP_COUNT_FUNC_DOWN_COUNT,
};

struct ti_eqep_cnt {
	struct counter_device counter;
	struct regmap *regmap32;
	struct regmap *regmap16;
};

static struct ti_eqep_cnt *ti_eqep_count_from_counter(struct counter_device *counter)
{
	return counter_priv(counter);
}

static int ti_eqep_count_read(struct counter_device *counter,
			      struct counter_count *count, u64 *val)
{
	struct ti_eqep_cnt *priv = ti_eqep_count_from_counter(counter);
	u32 cnt;

	regmap_read(priv->regmap32, QPOSCNT, &cnt);
	*val = cnt;

	return 0;
}

static int ti_eqep_count_write(struct counter_device *counter,
			       struct counter_count *count, u64 val)
{
	struct ti_eqep_cnt *priv = ti_eqep_count_from_counter(counter);
	u32 max;

	regmap_read(priv->regmap32, QPOSMAX, &max);
	if (val > max)
		return -EINVAL;

	return regmap_write(priv->regmap32, QPOSCNT, val);
}

static int ti_eqep_function_read(struct counter_device *counter,
				 struct counter_count *count,
				 enum counter_function *function)
{
	struct ti_eqep_cnt *priv = ti_eqep_count_from_counter(counter);
	u32 qdecctl;

	regmap_read(priv->regmap16, QDECCTL, &qdecctl);

	switch ((qdecctl & QDECCTL_QSRC) >> QDECCTL_QSRC_SHIFT) {
	case TI_EQEP_COUNT_FUNC_QUAD_COUNT:
		*function = COUNTER_FUNCTION_QUADRATURE_X4;
		break;
	case TI_EQEP_COUNT_FUNC_DIR_COUNT:
		*function = COUNTER_FUNCTION_PULSE_DIRECTION;
		break;
	case TI_EQEP_COUNT_FUNC_UP_COUNT:
		*function = COUNTER_FUNCTION_INCREASE;
		break;
	case TI_EQEP_COUNT_FUNC_DOWN_COUNT:
		*function = COUNTER_FUNCTION_DECREASE;
		break;
	}

	return 0;
}

static int ti_eqep_function_write(struct counter_device *counter,
				  struct counter_count *count,
				  enum counter_function function)
{
	struct ti_eqep_cnt *priv = ti_eqep_count_from_counter(counter);
	enum ti_eqep_count_func qsrc;

	switch (function) {
	case COUNTER_FUNCTION_QUADRATURE_X4:
		qsrc = TI_EQEP_COUNT_FUNC_QUAD_COUNT;
		break;
	case COUNTER_FUNCTION_PULSE_DIRECTION:
		qsrc = TI_EQEP_COUNT_FUNC_DIR_COUNT;
		break;
	case COUNTER_FUNCTION_INCREASE:
		qsrc = TI_EQEP_COUNT_FUNC_UP_COUNT;
		break;
	case COUNTER_FUNCTION_DECREASE:
		qsrc = TI_EQEP_COUNT_FUNC_DOWN_COUNT;
		break;
	default:
		/* should never reach this path */
		return -EINVAL;
	}

	return regmap_write_bits(priv->regmap16, QDECCTL, QDECCTL_QSRC,
				 qsrc << QDECCTL_QSRC_SHIFT);
}

static int ti_eqep_action_read(struct counter_device *counter,
			       struct counter_count *count,
			       struct counter_synapse *synapse,
			       enum counter_synapse_action *action)
{
	struct ti_eqep_cnt *priv = ti_eqep_count_from_counter(counter);
	enum counter_function function;
	u32 qdecctl;
	int err;

	err = ti_eqep_function_read(counter, count, &function);
	if (err)
		return err;

	switch (function) {
	case COUNTER_FUNCTION_QUADRATURE_X4:
		/* In quadrature mode, the rising and falling edge of both
		 * QEPA and QEPB trigger QCLK.
		 */
		*action = COUNTER_SYNAPSE_ACTION_BOTH_EDGES;
		return 0;
	case COUNTER_FUNCTION_PULSE_DIRECTION:
		/* In direction-count mode only rising edge of QEPA is counted
		 * and QEPB gives direction.
		 */
		switch (synapse->signal->id) {
		case TI_EQEP_SIGNAL_QEPA:
			*action = COUNTER_SYNAPSE_ACTION_RISING_EDGE;
			return 0;
		case TI_EQEP_SIGNAL_QEPB:
			*action = COUNTER_SYNAPSE_ACTION_NONE;
			return 0;
		default:
			/* should never reach this path */
			return -EINVAL;
		}
	case COUNTER_FUNCTION_INCREASE:
	case COUNTER_FUNCTION_DECREASE:
		/* In up/down-count modes only QEPA is counted and QEPB is not
		 * used.
		 */
		switch (synapse->signal->id) {
		case TI_EQEP_SIGNAL_QEPA:
			err = regmap_read(priv->regmap16, QDECCTL, &qdecctl);
			if (err)
				return err;

			if (qdecctl & QDECCTL_XCR)
				*action = COUNTER_SYNAPSE_ACTION_BOTH_EDGES;
			else
				*action = COUNTER_SYNAPSE_ACTION_RISING_EDGE;
			return 0;
		case TI_EQEP_SIGNAL_QEPB:
			*action = COUNTER_SYNAPSE_ACTION_NONE;
			return 0;
		default:
			/* should never reach this path */
			return -EINVAL;
		}
	default:
		/* should never reach this path */
		return -EINVAL;
	}
}

static const struct counter_ops ti_eqep_counter_ops = {
	.count_read	= ti_eqep_count_read,
	.count_write	= ti_eqep_count_write,
	.function_read	= ti_eqep_function_read,
	.function_write	= ti_eqep_function_write,
	.action_read	= ti_eqep_action_read,
};

static int ti_eqep_position_ceiling_read(struct counter_device *counter,
					 struct counter_count *count,
					 u64 *ceiling)
{
	struct ti_eqep_cnt *priv = ti_eqep_count_from_counter(counter);
	u32 qposmax;

	regmap_read(priv->regmap32, QPOSMAX, &qposmax);

	*ceiling = qposmax;

	return 0;
}

static int ti_eqep_position_ceiling_write(struct counter_device *counter,
					  struct counter_count *count,
					  u64 ceiling)
{
	struct ti_eqep_cnt *priv = ti_eqep_count_from_counter(counter);

	if (ceiling != (u32)ceiling)
		return -ERANGE;

	regmap_write(priv->regmap32, QPOSMAX, ceiling);

	return 0;
}

static int ti_eqep_position_enable_read(struct counter_device *counter,
					struct counter_count *count, u8 *enable)
{
	struct ti_eqep_cnt *priv = ti_eqep_count_from_counter(counter);
	u32 qepctl;

	regmap_read(priv->regmap16, QEPCTL, &qepctl);

	*enable = !!(qepctl & QEPCTL_PHEN);

	return 0;
}

static int ti_eqep_position_enable_write(struct counter_device *counter,
					 struct counter_count *count, u8 enable)
{
	struct ti_eqep_cnt *priv = ti_eqep_count_from_counter(counter);

	regmap_write_bits(priv->regmap16, QEPCTL, QEPCTL_PHEN, enable ? -1 : 0);

	return 0;
}

static struct counter_comp ti_eqep_position_ext[] = {
	COUNTER_COMP_CEILING(ti_eqep_position_ceiling_read,
			     ti_eqep_position_ceiling_write),
	COUNTER_COMP_ENABLE(ti_eqep_position_enable_read,
			    ti_eqep_position_enable_write),
};

static struct counter_signal ti_eqep_signals[] = {
	[TI_EQEP_SIGNAL_QEPA] = {
		.id = TI_EQEP_SIGNAL_QEPA,
		.name = "QEPA"
	},
	[TI_EQEP_SIGNAL_QEPB] = {
		.id = TI_EQEP_SIGNAL_QEPB,
		.name = "QEPB"
	},
};

static const enum counter_function ti_eqep_position_functions[] = {
	COUNTER_FUNCTION_QUADRATURE_X4,
	COUNTER_FUNCTION_PULSE_DIRECTION,
	COUNTER_FUNCTION_INCREASE,
	COUNTER_FUNCTION_DECREASE,
};

static const enum counter_synapse_action ti_eqep_position_synapse_actions[] = {
	COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
	COUNTER_SYNAPSE_ACTION_RISING_EDGE,
	COUNTER_SYNAPSE_ACTION_NONE,
};

static struct counter_synapse ti_eqep_position_synapses[] = {
	{
		.actions_list	= ti_eqep_position_synapse_actions,
		.num_actions	= ARRAY_SIZE(ti_eqep_position_synapse_actions),
		.signal		= &ti_eqep_signals[TI_EQEP_SIGNAL_QEPA],
	},
	{
		.actions_list	= ti_eqep_position_synapse_actions,
		.num_actions	= ARRAY_SIZE(ti_eqep_position_synapse_actions),
		.signal		= &ti_eqep_signals[TI_EQEP_SIGNAL_QEPB],
	},
};

static struct counter_count ti_eqep_counts[] = {
	{
		.id		= 0,
		.name		= "QPOSCNT",
		.functions_list	= ti_eqep_position_functions,
		.num_functions	= ARRAY_SIZE(ti_eqep_position_functions),
		.synapses	= ti_eqep_position_synapses,
		.num_synapses	= ARRAY_SIZE(ti_eqep_position_synapses),
		.ext		= ti_eqep_position_ext,
		.num_ext	= ARRAY_SIZE(ti_eqep_position_ext),
	},
};

static const struct regmap_config ti_eqep_regmap32_config = {
	.name = "32-bit",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = QUPRD,
};

static const struct regmap_config ti_eqep_regmap16_config = {
	.name = "16-bit",
	.reg_bits = 16,
	.val_bits = 16,
	.reg_stride = 2,
	.max_register = QCPRDLAT,
};

static int ti_eqep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct counter_device *counter;
	struct ti_eqep_cnt *priv;
	void __iomem *base;
	int err;

	counter = devm_counter_alloc(dev, sizeof(*priv));
	if (!counter)
		return -ENOMEM;
	priv = counter_priv(counter);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->regmap32 = devm_regmap_init_mmio(dev, base,
					       &ti_eqep_regmap32_config);
	if (IS_ERR(priv->regmap32))
		return PTR_ERR(priv->regmap32);

	priv->regmap16 = devm_regmap_init_mmio(dev, base + 0x24,
					       &ti_eqep_regmap16_config);
	if (IS_ERR(priv->regmap16))
		return PTR_ERR(priv->regmap16);

	counter->name = dev_name(dev);
	counter->parent = dev;
	counter->ops = &ti_eqep_counter_ops;
	counter->counts = ti_eqep_counts;
	counter->num_counts = ARRAY_SIZE(ti_eqep_counts);
	counter->signals = ti_eqep_signals;
	counter->num_signals = ARRAY_SIZE(ti_eqep_signals);

	platform_set_drvdata(pdev, counter);

	/*
	 * Need to make sure power is turned on. On AM33xx, this comes from the
	 * parent PWMSS bus driver. On AM17xx, this comes from the PSC power
	 * domain.
	 */
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	err = counter_add(counter);
	if (err < 0) {
		pm_runtime_put_sync(dev);
		pm_runtime_disable(dev);
		return err;
	}

	return 0;
}

static void ti_eqep_remove(struct platform_device *pdev)
{
	struct counter_device *counter = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	counter_unregister(counter);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
}

static const struct of_device_id ti_eqep_of_match[] = {
	{ .compatible = "ti,am3352-eqep", },
	{ },
};
MODULE_DEVICE_TABLE(of, ti_eqep_of_match);

static struct platform_driver ti_eqep_driver = {
	.probe = ti_eqep_probe,
	.remove_new = ti_eqep_remove,
	.driver = {
		.name = "ti-eqep-cnt",
		.of_match_table = ti_eqep_of_match,
	},
};
module_platform_driver(ti_eqep_driver);

MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_DESCRIPTION("TI eQEP counter driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(COUNTER);

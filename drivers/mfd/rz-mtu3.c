// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G2L Multi-Function Timer Pulse Unit 3(MTU3a) Core driver
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rz-mtu3.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#include "rz-mtu3.h"

struct rz_mtu3_priv {
	void __iomem *mmio;
	struct reset_control *rstc;
	spinlock_t lock;
};

/******* MTU3 registers (original offset is +0x1200) *******/
static const unsigned long rz_mtu3_8bit_ch_reg_offs[][13] = {
	[RZ_MTU3_CHAN_0] = MTU_8BIT_CH_0(0x104, 0x090, 0x100, 0x128, 0x101, 0x102, 0x103, 0x126),
	[RZ_MTU3_CHAN_1] = MTU_8BIT_CH_1_2(0x184, 0x091, 0x185, 0x180, 0x194, 0x181, 0x182),
	[RZ_MTU3_CHAN_2] = MTU_8BIT_CH_1_2(0x204, 0x092, 0x205, 0x200, 0x20c, 0x201, 0x202),
	[RZ_MTU3_CHAN_3] = MTU_8BIT_CH_3_4_6_7(0x008, 0x093, 0x02c, 0x000, 0x04c, 0x002, 0x004, 0x005, 0x038),
	[RZ_MTU3_CHAN_4] = MTU_8BIT_CH_3_4_6_7(0x009, 0x094, 0x02d, 0x001, 0x04d, 0x003, 0x006, 0x007, 0x039),
	[RZ_MTU3_CHAN_5] = MTU_8BIT_CH_5(0xab2, 0x1eb, 0xab4, 0xab6, 0xa84, 0xa85, 0xa86, 0xa94, 0xa95, 0xa96, 0xaa4, 0xaa5, 0xaa6),
	[RZ_MTU3_CHAN_6] = MTU_8BIT_CH_3_4_6_7(0x808, 0x893, 0x82c, 0x800, 0x84c, 0x802, 0x804, 0x805, 0x838),
	[RZ_MTU3_CHAN_7] = MTU_8BIT_CH_3_4_6_7(0x809, 0x894, 0x82d, 0x801, 0x84d, 0x803, 0x806, 0x807, 0x839),
	[RZ_MTU3_CHAN_8] = MTU_8BIT_CH_8(0x404, 0x098, 0x400, 0x406, 0x401, 0x402, 0x403)
};

static const unsigned long rz_mtu3_16bit_ch_reg_offs[][12] = {
	[RZ_MTU3_CHAN_0] = MTU_16BIT_CH_0(0x106, 0x108, 0x10a, 0x10c, 0x10e, 0x120, 0x122),
	[RZ_MTU3_CHAN_1] = MTU_16BIT_CH_1_2(0x186, 0x188, 0x18a),
	[RZ_MTU3_CHAN_2] = MTU_16BIT_CH_1_2(0x206, 0x208, 0x20a),
	[RZ_MTU3_CHAN_3] = MTU_16BIT_CH_3_6(0x010, 0x018, 0x01a, 0x024, 0x026, 0x072),
	[RZ_MTU3_CHAN_4] = MTU_16BIT_CH_4_7(0x012, 0x01c, 0x01e, 0x028, 0x2a, 0x074, 0x076, 0x040, 0x044, 0x046, 0x048, 0x04a),
	[RZ_MTU3_CHAN_5] = MTU_16BIT_CH_5(0xa80, 0xa82, 0xa90, 0xa92, 0xaa0, 0xaa2),
	[RZ_MTU3_CHAN_6] = MTU_16BIT_CH_3_6(0x810, 0x818, 0x81a, 0x824, 0x826, 0x872),
	[RZ_MTU3_CHAN_7] = MTU_16BIT_CH_4_7(0x812, 0x81c, 0x81e, 0x828, 0x82a, 0x874, 0x876, 0x840, 0x844, 0x846, 0x848, 0x84a)
};

static const unsigned long rz_mtu3_32bit_ch_reg_offs[][5] = {
	[RZ_MTU3_CHAN_1] = MTU_32BIT_CH_1(0x1a0, 0x1a4, 0x1a8),
	[RZ_MTU3_CHAN_8] = MTU_32BIT_CH_8(0x408, 0x40c, 0x410, 0x414, 0x418)
};

static bool rz_mtu3_is_16bit_shared_reg(u16 offset)
{
	return (offset == RZ_MTU3_TDDRA || offset == RZ_MTU3_TDDRB ||
		offset == RZ_MTU3_TCDRA || offset == RZ_MTU3_TCDRB ||
		offset == RZ_MTU3_TCBRA || offset == RZ_MTU3_TCBRB ||
		offset == RZ_MTU3_TCNTSA || offset == RZ_MTU3_TCNTSB);
}

u16 rz_mtu3_shared_reg_read(struct rz_mtu3_channel *ch, u16 offset)
{
	struct rz_mtu3 *mtu = dev_get_drvdata(ch->dev->parent);
	struct rz_mtu3_priv *priv = mtu->priv_data;

	if (rz_mtu3_is_16bit_shared_reg(offset))
		return readw(priv->mmio + offset);
	else
		return readb(priv->mmio + offset);
}
EXPORT_SYMBOL_GPL(rz_mtu3_shared_reg_read);

u8 rz_mtu3_8bit_ch_read(struct rz_mtu3_channel *ch, u16 offset)
{
	struct rz_mtu3 *mtu = dev_get_drvdata(ch->dev->parent);
	struct rz_mtu3_priv *priv = mtu->priv_data;
	u16 ch_offs;

	ch_offs = rz_mtu3_8bit_ch_reg_offs[ch->channel_number][offset];

	return readb(priv->mmio + ch_offs);
}
EXPORT_SYMBOL_GPL(rz_mtu3_8bit_ch_read);

u16 rz_mtu3_16bit_ch_read(struct rz_mtu3_channel *ch, u16 offset)
{
	struct rz_mtu3 *mtu = dev_get_drvdata(ch->dev->parent);
	struct rz_mtu3_priv *priv = mtu->priv_data;
	u16 ch_offs;

	/* MTU8 doesn't have 16-bit registers */
	if (ch->channel_number == RZ_MTU3_CHAN_8)
		return 0;

	ch_offs = rz_mtu3_16bit_ch_reg_offs[ch->channel_number][offset];

	return readw(priv->mmio + ch_offs);
}
EXPORT_SYMBOL_GPL(rz_mtu3_16bit_ch_read);

u32 rz_mtu3_32bit_ch_read(struct rz_mtu3_channel *ch, u16 offset)
{
	struct rz_mtu3 *mtu = dev_get_drvdata(ch->dev->parent);
	struct rz_mtu3_priv *priv = mtu->priv_data;
	u16 ch_offs;

	if (ch->channel_number != RZ_MTU3_CHAN_1 && ch->channel_number != RZ_MTU3_CHAN_8)
		return 0;

	ch_offs = rz_mtu3_32bit_ch_reg_offs[ch->channel_number][offset];

	return readl(priv->mmio + ch_offs);
}
EXPORT_SYMBOL_GPL(rz_mtu3_32bit_ch_read);

void rz_mtu3_8bit_ch_write(struct rz_mtu3_channel *ch, u16 offset, u8 val)
{
	struct rz_mtu3 *mtu = dev_get_drvdata(ch->dev->parent);
	struct rz_mtu3_priv *priv = mtu->priv_data;
	u16 ch_offs;

	ch_offs = rz_mtu3_8bit_ch_reg_offs[ch->channel_number][offset];
	writeb(val, priv->mmio + ch_offs);
}
EXPORT_SYMBOL_GPL(rz_mtu3_8bit_ch_write);

void rz_mtu3_16bit_ch_write(struct rz_mtu3_channel *ch, u16 offset, u16 val)
{
	struct rz_mtu3 *mtu = dev_get_drvdata(ch->dev->parent);
	struct rz_mtu3_priv *priv = mtu->priv_data;
	u16 ch_offs;

	/* MTU8 doesn't have 16-bit registers */
	if (ch->channel_number == RZ_MTU3_CHAN_8)
		return;

	ch_offs = rz_mtu3_16bit_ch_reg_offs[ch->channel_number][offset];
	writew(val, priv->mmio + ch_offs);
}
EXPORT_SYMBOL_GPL(rz_mtu3_16bit_ch_write);

void rz_mtu3_32bit_ch_write(struct rz_mtu3_channel *ch, u16 offset, u32 val)
{
	struct rz_mtu3 *mtu = dev_get_drvdata(ch->dev->parent);
	struct rz_mtu3_priv *priv = mtu->priv_data;
	u16 ch_offs;

	if (ch->channel_number != RZ_MTU3_CHAN_1 && ch->channel_number != RZ_MTU3_CHAN_8)
		return;

	ch_offs = rz_mtu3_32bit_ch_reg_offs[ch->channel_number][offset];
	writel(val, priv->mmio + ch_offs);
}
EXPORT_SYMBOL_GPL(rz_mtu3_32bit_ch_write);

void rz_mtu3_shared_reg_write(struct rz_mtu3_channel *ch, u16 offset, u16 value)
{
	struct rz_mtu3 *mtu = dev_get_drvdata(ch->dev->parent);
	struct rz_mtu3_priv *priv = mtu->priv_data;

	if (rz_mtu3_is_16bit_shared_reg(offset))
		writew(value, priv->mmio + offset);
	else
		writeb((u8)value, priv->mmio + offset);
}
EXPORT_SYMBOL_GPL(rz_mtu3_shared_reg_write);

void rz_mtu3_shared_reg_update_bit(struct rz_mtu3_channel *ch, u16 offset,
				   u16 pos, u8 val)
{
	struct rz_mtu3 *mtu = dev_get_drvdata(ch->dev->parent);
	struct rz_mtu3_priv *priv = mtu->priv_data;
	unsigned long tmdr, flags;

	spin_lock_irqsave(&priv->lock, flags);
	tmdr = rz_mtu3_shared_reg_read(ch, offset);
	__assign_bit(pos, &tmdr, !!val);
	rz_mtu3_shared_reg_write(ch, offset, tmdr);
	spin_unlock_irqrestore(&priv->lock, flags);
}
EXPORT_SYMBOL_GPL(rz_mtu3_shared_reg_update_bit);

static u16 rz_mtu3_get_tstr_offset(struct rz_mtu3_channel *ch)
{
	u16 offset;

	switch (ch->channel_number) {
	case RZ_MTU3_CHAN_0:
	case RZ_MTU3_CHAN_1:
	case RZ_MTU3_CHAN_2:
	case RZ_MTU3_CHAN_3:
	case RZ_MTU3_CHAN_4:
	case RZ_MTU3_CHAN_8:
		offset = RZ_MTU3_TSTRA;
		break;
	case RZ_MTU3_CHAN_5:
		offset = RZ_MTU3_TSTR;
		break;
	case RZ_MTU3_CHAN_6:
	case RZ_MTU3_CHAN_7:
		offset = RZ_MTU3_TSTRB;
		break;
	default:
		offset = 0;
		break;
	}

	return offset;
}

static u8 rz_mtu3_get_tstr_bit_pos(struct rz_mtu3_channel *ch)
{
	u8 bitpos;

	switch (ch->channel_number) {
	case RZ_MTU3_CHAN_0:
	case RZ_MTU3_CHAN_1:
	case RZ_MTU3_CHAN_2:
	case RZ_MTU3_CHAN_6:
	case RZ_MTU3_CHAN_7:
		bitpos = ch->channel_number;
		break;
	case RZ_MTU3_CHAN_3:
		bitpos = 6;
		break;
	case RZ_MTU3_CHAN_4:
		bitpos = 7;
		break;
	case RZ_MTU3_CHAN_5:
		bitpos = 2;
		break;
	case RZ_MTU3_CHAN_8:
		bitpos = 3;
		break;
	default:
		bitpos = 0;
		break;
	}

	return bitpos;
}

static void rz_mtu3_start_stop_ch(struct rz_mtu3_channel *ch, bool start)
{
	struct rz_mtu3 *mtu = dev_get_drvdata(ch->dev->parent);
	struct rz_mtu3_priv *priv = mtu->priv_data;
	unsigned long flags, tstr;
	u16 offset;
	u8 bitpos;

	offset = rz_mtu3_get_tstr_offset(ch);
	bitpos = rz_mtu3_get_tstr_bit_pos(ch);

	/* start stop register shared by multiple timer channels */
	spin_lock_irqsave(&priv->lock, flags);

	tstr = rz_mtu3_shared_reg_read(ch, offset);
	__assign_bit(bitpos, &tstr, start);
	rz_mtu3_shared_reg_write(ch, offset, tstr);

	spin_unlock_irqrestore(&priv->lock, flags);
}

bool rz_mtu3_is_enabled(struct rz_mtu3_channel *ch)
{
	struct rz_mtu3 *mtu = dev_get_drvdata(ch->dev->parent);
	struct rz_mtu3_priv *priv = mtu->priv_data;
	unsigned long flags, tstr;
	u16 offset;
	u8 bitpos;

	offset = rz_mtu3_get_tstr_offset(ch);
	bitpos = rz_mtu3_get_tstr_bit_pos(ch);

	/* start stop register shared by multiple timer channels */
	spin_lock_irqsave(&priv->lock, flags);
	tstr = rz_mtu3_shared_reg_read(ch, offset);
	spin_unlock_irqrestore(&priv->lock, flags);

	return tstr & BIT(bitpos);
}
EXPORT_SYMBOL_GPL(rz_mtu3_is_enabled);

int rz_mtu3_enable(struct rz_mtu3_channel *ch)
{
	/* enable channel */
	rz_mtu3_start_stop_ch(ch, true);

	return 0;
}
EXPORT_SYMBOL_GPL(rz_mtu3_enable);

void rz_mtu3_disable(struct rz_mtu3_channel *ch)
{
	/* disable channel */
	rz_mtu3_start_stop_ch(ch, false);
}
EXPORT_SYMBOL_GPL(rz_mtu3_disable);

static void rz_mtu3_reset_assert(void *data)
{
	struct rz_mtu3 *mtu = dev_get_drvdata(data);
	struct rz_mtu3_priv *priv = mtu->priv_data;

	mfd_remove_devices(data);
	reset_control_assert(priv->rstc);
}

static const struct mfd_cell rz_mtu3_devs[] = {
	{
		.name = "rz-mtu3-counter",
	},
	{
		.name = "pwm-rz-mtu3",
	},
};

static int rz_mtu3_probe(struct platform_device *pdev)
{
	struct rz_mtu3_priv *priv;
	struct rz_mtu3 *ddata;
	unsigned int i;
	int ret;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->priv_data = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!ddata->priv_data)
		return -ENOMEM;

	priv = ddata->priv_data;

	priv->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->mmio))
		return PTR_ERR(priv->mmio);

	priv->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(priv->rstc))
		return PTR_ERR(priv->rstc);

	ddata->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ddata->clk))
		return PTR_ERR(ddata->clk);

	reset_control_deassert(priv->rstc);
	spin_lock_init(&priv->lock);
	platform_set_drvdata(pdev, ddata);

	for (i = 0; i < RZ_MTU_NUM_CHANNELS; i++) {
		ddata->channels[i].channel_number = i;
		ddata->channels[i].is_busy = false;
		mutex_init(&ddata->channels[i].lock);
	}

	ret = mfd_add_devices(&pdev->dev, 0, rz_mtu3_devs,
			      ARRAY_SIZE(rz_mtu3_devs), NULL, 0, NULL);
	if (ret < 0)
		goto err_assert;

	return devm_add_action_or_reset(&pdev->dev, rz_mtu3_reset_assert,
					&pdev->dev);

err_assert:
	reset_control_assert(priv->rstc);
	return ret;
}

static const struct of_device_id rz_mtu3_of_match[] = {
	{ .compatible = "renesas,rz-mtu3", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rz_mtu3_of_match);

static struct platform_driver rz_mtu3_driver = {
	.probe = rz_mtu3_probe,
	.driver	= {
		.name = "rz-mtu3",
		.of_match_table = rz_mtu3_of_match,
	},
};
module_platform_driver(rz_mtu3_driver);

MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G2L MTU3a Core Driver");
MODULE_LICENSE("GPL");

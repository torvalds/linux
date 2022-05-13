// SPDX-License-Identifier: ISC
/* Copyright (C) 2022 MediaTek Inc. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_gpio.h>
#include <linux/iopoll.h>
#include <linux/reset.h>
#include <linux/of_net.h>

#include "mt7915.h"

/* INFRACFG */
#define MT_INFRACFG_CONN2AP_SLPPROT	0x0d0
#define MT_INFRACFG_AP2CONN_SLPPROT	0x0d4

#define MT_INFRACFG_RX_EN_MASK		BIT(16)
#define MT_INFRACFG_TX_RDY_MASK		BIT(4)
#define MT_INFRACFG_TX_EN_MASK		BIT(0)

/* TOP POS */
#define MT_TOP_POS_FAST_CTRL		0x114
#define MT_TOP_POS_FAST_EN_MASK		BIT(3)

#define MT_TOP_POS_SKU			0x21c
#define MT_TOP_POS_SKU_MASK		GENMASK(31, 28)
#define MT_TOP_POS_SKU_ADIE_DBDC_MASK	BIT(2)

enum {
	ADIE_SB,
	ADIE_DBDC
};

static int
mt76_wmac_spi_read(struct mt7915_dev *dev, u8 adie, u32 addr, u32 *val)
{
	int ret;
	u32 cur;

	ret = read_poll_timeout(mt76_rr, cur, !(cur & MT_TOP_SPI_POLLING_BIT),
				USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
				dev, MT_TOP_SPI_BUSY_CR(adie));
	if (ret)
		return ret;

	mt76_wr(dev, MT_TOP_SPI_ADDR_CR(adie),
		MT_TOP_SPI_READ_ADDR_FORMAT | addr);
	mt76_wr(dev, MT_TOP_SPI_WRITE_DATA_CR(adie), 0);

	ret = read_poll_timeout(mt76_rr, cur, !(cur & MT_TOP_SPI_POLLING_BIT),
				USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
				dev, MT_TOP_SPI_BUSY_CR(adie));
	if (ret)
		return ret;

	*val = mt76_rr(dev, MT_TOP_SPI_READ_DATA_CR(adie));

	return 0;
}

static int
mt76_wmac_spi_write(struct mt7915_dev *dev, u8 adie, u32 addr, u32 val)
{
	int ret;
	u32 cur;

	ret = read_poll_timeout(mt76_rr, cur, !(cur & MT_TOP_SPI_POLLING_BIT),
				USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
				dev, MT_TOP_SPI_BUSY_CR(adie));
	if (ret)
		return ret;

	mt76_wr(dev, MT_TOP_SPI_ADDR_CR(adie),
		MT_TOP_SPI_WRITE_ADDR_FORMAT | addr);
	mt76_wr(dev, MT_TOP_SPI_WRITE_DATA_CR(adie), val);

	return read_poll_timeout(mt76_rr, cur, !(cur & MT_TOP_SPI_POLLING_BIT),
				 USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
				 dev, MT_TOP_SPI_BUSY_CR(adie));
}

static int
mt76_wmac_spi_rmw(struct mt7915_dev *dev, u8 adie,
		  u32 addr, u32 mask, u32 val)
{
	u32 cur, ret;

	ret = mt76_wmac_spi_read(dev, adie, addr, &cur);
	if (ret)
		return ret;

	cur &= ~mask;
	cur |= val;

	return mt76_wmac_spi_write(dev, adie, addr, cur);
}

static int
mt7986_wmac_adie_efuse_read(struct mt7915_dev *dev, u8 adie,
			    u32 addr, u32 *data)
{
	int ret, temp;
	u32 val, mask;

	ret = mt76_wmac_spi_write(dev, adie, MT_ADIE_EFUSE_CFG,
				  MT_ADIE_EFUSE_CTRL_MASK);
	if (ret)
		return ret;

	ret = mt76_wmac_spi_rmw(dev, adie, MT_ADIE_EFUSE2_CTRL, BIT(30), 0x0);
	if (ret)
		return ret;

	mask = (MT_ADIE_EFUSE_MODE_MASK | MT_ADIE_EFUSE_ADDR_MASK |
		MT_ADIE_EFUSE_KICK_MASK);
	val = FIELD_PREP(MT_ADIE_EFUSE_MODE_MASK, 0) |
	      FIELD_PREP(MT_ADIE_EFUSE_ADDR_MASK, addr) |
	      FIELD_PREP(MT_ADIE_EFUSE_KICK_MASK, 1);
	ret = mt76_wmac_spi_rmw(dev, adie, MT_ADIE_EFUSE2_CTRL, mask, val);
	if (ret)
		return ret;

	ret = read_poll_timeout(mt76_wmac_spi_read, temp,
				!temp && !FIELD_GET(MT_ADIE_EFUSE_KICK_MASK, val),
				USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
				dev, adie, MT_ADIE_EFUSE2_CTRL, &val);
	if (ret)
		return ret;

	ret = mt76_wmac_spi_read(dev, adie, MT_ADIE_EFUSE2_CTRL, &val);
	if (ret)
		return ret;

	if (FIELD_GET(MT_ADIE_EFUSE_VALID_MASK, val) == 1)
		ret = mt76_wmac_spi_read(dev, adie, MT_ADIE_EFUSE_RDATA0,
					 data);

	return ret;
}

static inline void mt76_wmac_spi_lock(struct mt7915_dev *dev)
{
	u32 cur;

	read_poll_timeout(mt76_rr, cur,
			  FIELD_GET(MT_SEMA_RFSPI_STATUS_MASK, cur),
			  1000, 1000 * MSEC_PER_SEC, false, dev,
			  MT_SEMA_RFSPI_STATUS);
}

static inline void mt76_wmac_spi_unlock(struct mt7915_dev *dev)
{
	mt76_wr(dev, MT_SEMA_RFSPI_RELEASE, 1);
}

static u32 mt76_wmac_rmw(void __iomem *base, u32 offset, u32 mask, u32 val)
{
	val |= readl(base + offset) & ~mask;
	writel(val, base + offset);

	return val;
}

static u8 mt7986_wmac_check_adie_type(struct mt7915_dev *dev)
{
	u32 val;

	val = readl(dev->sku + MT_TOP_POS_SKU);

	return FIELD_GET(MT_TOP_POS_SKU_ADIE_DBDC_MASK, val);
}

static int mt7986_wmac_consys_reset(struct mt7915_dev *dev, bool enable)
{
	if (!enable)
		return reset_control_assert(dev->rstc);

	mt76_wmac_rmw(dev->sku, MT_TOP_POS_FAST_CTRL,
		      MT_TOP_POS_FAST_EN_MASK,
		      FIELD_PREP(MT_TOP_POS_FAST_EN_MASK, 0x1));

	return reset_control_deassert(dev->rstc);
}

static int mt7986_wmac_gpio_setup(struct mt7915_dev *dev)
{
	struct pinctrl_state *state;
	struct pinctrl *pinctrl;
	int ret;
	u8 type;

	type = mt7986_wmac_check_adie_type(dev);
	pinctrl = devm_pinctrl_get(dev->mt76.dev);
	if (IS_ERR(pinctrl))
		return PTR_ERR(pinctrl);

	switch (type) {
	case ADIE_SB:
		state = pinctrl_lookup_state(pinctrl, "default");
		if (IS_ERR_OR_NULL(state))
			return -EINVAL;
		break;
	case ADIE_DBDC:
		state = pinctrl_lookup_state(pinctrl, "dbdc");
		if (IS_ERR_OR_NULL(state))
			return -EINVAL;
		break;
	}

	ret = pinctrl_select_state(pinctrl, state);
	if (ret)
		return ret;

	usleep_range(500, 1000);

	return 0;
}

static int mt7986_wmac_consys_lockup(struct mt7915_dev *dev, bool enable)
{
	int ret;
	u32 cur;

	mt76_wmac_rmw(dev->dcm, MT_INFRACFG_AP2CONN_SLPPROT,
		      MT_INFRACFG_RX_EN_MASK,
		      FIELD_PREP(MT_INFRACFG_RX_EN_MASK, enable));
	ret = read_poll_timeout(readl, cur, !(cur & MT_INFRACFG_RX_EN_MASK),
				USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
				dev->dcm + MT_INFRACFG_AP2CONN_SLPPROT);
	if (ret)
		return ret;

	mt76_wmac_rmw(dev->dcm, MT_INFRACFG_AP2CONN_SLPPROT,
		      MT_INFRACFG_TX_EN_MASK,
		      FIELD_PREP(MT_INFRACFG_TX_EN_MASK, enable));
	ret = read_poll_timeout(readl, cur, !(cur & MT_INFRACFG_TX_RDY_MASK),
				USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
				dev->dcm + MT_INFRACFG_AP2CONN_SLPPROT);
	if (ret)
		return ret;

	mt76_wmac_rmw(dev->dcm, MT_INFRACFG_CONN2AP_SLPPROT,
		      MT_INFRACFG_RX_EN_MASK,
		      FIELD_PREP(MT_INFRACFG_RX_EN_MASK, enable));
	mt76_wmac_rmw(dev->dcm, MT_INFRACFG_CONN2AP_SLPPROT,
		      MT_INFRACFG_TX_EN_MASK,
		      FIELD_PREP(MT_INFRACFG_TX_EN_MASK, enable));

	return 0;
}

static int mt7986_wmac_coninfra_check(struct mt7915_dev *dev)
{
	u32 cur;

	return read_poll_timeout(mt76_rr, cur, (cur == 0x02070000),
				 USEC_PER_MSEC, 50 * USEC_PER_MSEC,
				 false, dev, MT_CONN_INFRA_BASE);
}

static int mt7986_wmac_coninfra_setup(struct mt7915_dev *dev)
{
	struct device *pdev = dev->mt76.dev;
	struct reserved_mem *rmem;
	struct device_node *np;
	u32 val;

	np = of_parse_phandle(pdev->of_node, "memory-region", 0);
	if (!np)
		return -EINVAL;

	rmem = of_reserved_mem_lookup(np);
	if (!rmem)
		return -EINVAL;

	val = (rmem->base >> 16) & MT_TOP_MCU_EMI_BASE_MASK;

	/* Set conninfra subsys PLL check */
	mt76_rmw_field(dev, MT_INFRA_CKGEN_BUS,
		       MT_INFRA_CKGEN_BUS_RDY_SEL_MASK, 0x1);
	mt76_rmw_field(dev, MT_INFRA_CKGEN_BUS,
		       MT_INFRA_CKGEN_BUS_RDY_SEL_MASK, 0x1);

	mt76_rmw_field(dev, MT_TOP_MCU_EMI_BASE,
		       MT_TOP_MCU_EMI_BASE_MASK, val);

	mt76_wr(dev, MT_INFRA_BUS_EMI_START, rmem->base);
	mt76_wr(dev, MT_INFRA_BUS_EMI_END, rmem->size);

	mt76_rr(dev, MT_CONN_INFRA_EFUSE);

	/* Set conninfra sysram */
	mt76_wr(dev, MT_TOP_RGU_SYSRAM_PDN, 0);
	mt76_wr(dev, MT_TOP_RGU_SYSRAM_SLP, 1);

	return 0;
}

static int mt7986_wmac_sku_setup(struct mt7915_dev *dev, u32 *adie_type)
{
	int ret;
	u32 adie_main, adie_ext;

	mt76_rmw_field(dev, MT_CONN_INFRA_ADIE_RESET,
		       MT_CONN_INFRA_ADIE1_RESET_MASK, 0x1);
	mt76_rmw_field(dev, MT_CONN_INFRA_ADIE_RESET,
		       MT_CONN_INFRA_ADIE2_RESET_MASK, 0x1);

	mt76_wmac_spi_lock(dev);

	ret = mt76_wmac_spi_read(dev, 0, MT_ADIE_CHIP_ID, &adie_main);
	if (ret)
		goto out;

	ret = mt76_wmac_spi_read(dev, 1, MT_ADIE_CHIP_ID, &adie_ext);
	if (ret)
		goto out;

	*adie_type = FIELD_GET(MT_ADIE_CHIP_ID_MASK, adie_main) |
		     (MT_ADIE_CHIP_ID_MASK & adie_ext);

out:
	mt76_wmac_spi_unlock(dev);

	return 0;
}

static inline u16 mt7986_adie_idx(u8 adie, u32 adie_type)
{
	if (adie == 0)
		return u32_get_bits(adie_type, MT_ADIE_IDX0);
	else
		return u32_get_bits(adie_type, MT_ADIE_IDX1);
}

static inline bool is_7975(struct mt7915_dev *dev, u8 adie, u32 adie_type)
{
	return mt7986_adie_idx(adie, adie_type) == 0x7975;
}

static inline bool is_7976(struct mt7915_dev *dev, u8 adie, u32 adie_type)
{
	return mt7986_adie_idx(adie, adie_type) == 0x7976;
}

static int mt7986_wmac_adie_thermal_cal(struct mt7915_dev *dev, u8 adie)
{
	int ret;
	u32 data, val;

	ret = mt7986_wmac_adie_efuse_read(dev, adie, MT_ADIE_THADC_ANALOG,
					  &data);
	if (ret || FIELD_GET(MT_ADIE_ANA_EN_MASK, data)) {
		val = FIELD_GET(MT_ADIE_VRPI_SEL_EFUSE_MASK, data);
		ret = mt76_wmac_spi_rmw(dev, adie, MT_ADIE_RG_TOP_THADC_BG,
					MT_ADIE_VRPI_SEL_CR_MASK,
					FIELD_PREP(MT_ADIE_VRPI_SEL_CR_MASK, val));
		if (ret)
			return ret;

		val = FIELD_GET(MT_ADIE_PGA_GAIN_EFUSE_MASK, data);
		ret = mt76_wmac_spi_rmw(dev, adie, MT_ADIE_RG_TOP_THADC,
					MT_ADIE_PGA_GAIN_MASK,
					FIELD_PREP(MT_ADIE_PGA_GAIN_MASK, val));
		if (ret)
			return ret;
	}

	ret = mt7986_wmac_adie_efuse_read(dev, adie, MT_ADIE_THADC_SLOP,
					  &data);
	if (ret || FIELD_GET(MT_ADIE_ANA_EN_MASK, data)) {
		val = FIELD_GET(MT_ADIE_LDO_CTRL_EFUSE_MASK, data);

		return mt76_wmac_spi_rmw(dev, adie, MT_ADIE_RG_TOP_THADC,
					 MT_ADIE_LDO_CTRL_MASK,
					 FIELD_PREP(MT_ADIE_LDO_CTRL_MASK, val));
	}

	return 0;
}

static int
mt7986_read_efuse_xo_trim_7976(struct mt7915_dev *dev, u8 adie,
			       bool is_40m, int *result)
{
	int ret;
	u32 data, addr;

	addr = is_40m ? MT_ADIE_XTAL_AXM_40M_OSC : MT_ADIE_XTAL_AXM_80M_OSC;
	ret = mt7986_wmac_adie_efuse_read(dev, adie, addr, &data);
	if (ret)
		return ret;

	if (!FIELD_GET(MT_ADIE_XO_TRIM_EN_MASK, data)) {
		*result = 64;
	} else {
		*result = FIELD_GET(MT_ADIE_TRIM_MASK, data);
		addr = is_40m ? MT_ADIE_XTAL_TRIM1_40M_OSC :
				MT_ADIE_XTAL_TRIM1_80M_OSC;
		ret = mt7986_wmac_adie_efuse_read(dev, adie, addr, &data);
		if (ret)
			return ret;

		if (FIELD_GET(MT_ADIE_XO_TRIM_EN_MASK, data) &&
		    FIELD_GET(MT_ADIE_XTAL_DECREASE_MASK, data))
			*result -= FIELD_GET(MT_ADIE_EFUSE_TRIM_MASK, data);
		else if (FIELD_GET(MT_ADIE_XO_TRIM_EN_MASK, data))
			*result += FIELD_GET(MT_ADIE_EFUSE_TRIM_MASK, data);

		*result = max(0, min(127, *result));
	}

	return 0;
}

static int mt7986_wmac_adie_xtal_trim_7976(struct mt7915_dev *dev, u8 adie)
{
	int ret, trim_80m, trim_40m;
	u32 data, val, mode;

	ret = mt7986_wmac_adie_efuse_read(dev, adie, MT_ADIE_XO_TRIM_FLOW,
					  &data);
	if (ret || !FIELD_GET(BIT(1), data))
		return 0;

	ret = mt7986_read_efuse_xo_trim_7976(dev, adie, false, &trim_80m);
	if (ret)
		return ret;

	ret = mt7986_read_efuse_xo_trim_7976(dev, adie, true, &trim_40m);
	if (ret)
		return ret;

	ret = mt76_wmac_spi_read(dev, adie, MT_ADIE_RG_STRAP_PIN_IN, &val);
	if (ret)
		return ret;

	mode = FIELD_PREP(GENMASK(6, 4), val);
	if (!mode || mode == 0x2) {
		ret = mt76_wmac_spi_rmw(dev, adie, MT_ADIE_XTAL_C1,
					GENMASK(31, 24),
					FIELD_PREP(GENMASK(31, 24), trim_80m));
		if (ret)
			return ret;

		ret = mt76_wmac_spi_rmw(dev, adie, MT_ADIE_XTAL_C2,
					GENMASK(31, 24),
					FIELD_PREP(GENMASK(31, 24), trim_80m));
	} else if (mode == 0x3 || mode == 0x4 || mode == 0x6) {
		ret = mt76_wmac_spi_rmw(dev, adie, MT_ADIE_XTAL_C1,
					GENMASK(23, 16),
					FIELD_PREP(GENMASK(23, 16), trim_40m));
		if (ret)
			return ret;

		ret = mt76_wmac_spi_rmw(dev, adie, MT_ADIE_XTAL_C2,
					GENMASK(23, 16),
					FIELD_PREP(GENMASK(23, 16), trim_40m));
	}

	return ret;
}

static int mt7986_wmac_adie_patch_7976(struct mt7915_dev *dev, u8 adie)
{
	int ret;

	ret = mt76_wmac_spi_write(dev, adie, MT_ADIE_RG_TOP_THADC, 0x4a563b00);
	if (ret)
		return ret;

	ret = mt76_wmac_spi_write(dev, adie, MT_ADIE_RG_XO_01, 0x1d59080f);
	if (ret)
		return ret;

	return mt76_wmac_spi_write(dev, adie, MT_ADIE_RG_XO_03, 0x34c00fe0);
}

static int
mt7986_read_efuse_xo_trim_7975(struct mt7915_dev *dev, u8 adie,
			       u32 addr, u32 *result)
{
	int ret;
	u32 data;

	ret = mt7986_wmac_adie_efuse_read(dev, adie, addr, &data);
	if (ret)
		return ret;

	if ((data & MT_ADIE_XO_TRIM_EN_MASK)) {
		if ((data & MT_ADIE_XTAL_DECREASE_MASK))
			*result -= (data & MT_ADIE_EFUSE_TRIM_MASK);
		else
			*result += (data & MT_ADIE_EFUSE_TRIM_MASK);

		*result = (*result & MT_ADIE_TRIM_MASK);
	}

	return 0;
}

static int mt7986_wmac_adie_xtal_trim_7975(struct mt7915_dev *dev, u8 adie)
{
	int ret;
	u32 data, result = 0, value;

	ret = mt7986_wmac_adie_efuse_read(dev, adie, MT_ADIE_7975_XTAL_EN,
					  &data);
	if (ret || !(data & BIT(1)))
		return 0;

	ret = mt7986_wmac_adie_efuse_read(dev, adie, MT_ADIE_7975_XTAL_CAL,
					  &data);
	if (ret)
		return ret;

	if (data & MT_ADIE_XO_TRIM_EN_MASK)
		result = (data & MT_ADIE_TRIM_MASK);

	ret = mt7986_read_efuse_xo_trim_7975(dev, adie, MT_ADIE_7975_XO_TRIM2,
					     &result);
	if (ret)
		return ret;

	ret = mt7986_read_efuse_xo_trim_7975(dev, adie, MT_ADIE_7975_XO_TRIM3,
					     &result);
	if (ret)
		return ret;

	ret = mt7986_read_efuse_xo_trim_7975(dev, adie, MT_ADIE_7975_XO_TRIM4,
					     &result);
	if (ret)
		return ret;

	/* Update trim value to C1 and C2*/
	value = FIELD_GET(MT_ADIE_7975_XO_CTRL2_C1_MASK, result) |
		FIELD_GET(MT_ADIE_7975_XO_CTRL2_C2_MASK, result);
	ret = mt76_wmac_spi_rmw(dev, adie, MT_ADIE_7975_XO_CTRL2,
				MT_ADIE_7975_XO_CTRL2_MASK, value);
	if (ret)
		return ret;

	ret = mt76_wmac_spi_read(dev, adie, MT_ADIE_7975_XTAL, &value);
	if (ret)
		return ret;

	if (value & MT_ADIE_7975_XTAL_EN_MASK) {
		ret = mt76_wmac_spi_rmw(dev, adie, MT_ADIE_7975_XO_2,
					MT_ADIE_7975_XO_2_FIX_EN, 0x0);
		if (ret)
			return ret;
	}

	return mt76_wmac_spi_rmw(dev, adie, MT_ADIE_7975_XO_CTRL6,
				 MT_ADIE_7975_XO_CTRL6_MASK, 0x1);
}

static int mt7986_wmac_adie_patch_7975(struct mt7915_dev *dev, u8 adie)
{
	int ret;

	/* disable CAL LDO and fine tune RFDIG LDO */
	ret = mt76_wmac_spi_write(dev, adie, 0x348, 0x00000002);
	if (ret)
		return ret;

	ret = mt76_wmac_spi_write(dev, adie, 0x378, 0x00000002);
	if (ret)
		return ret;

	ret = mt76_wmac_spi_write(dev, adie, 0x3a8, 0x00000002);
	if (ret)
		return ret;

	ret = mt76_wmac_spi_write(dev, adie, 0x3d8, 0x00000002);
	if (ret)
		return ret;

	/* set CKA driving and filter */
	ret = mt76_wmac_spi_write(dev, adie, 0xa1c, 0x30000aaa);
	if (ret)
		return ret;

	/* set CKB LDO to 1.4V */
	ret = mt76_wmac_spi_write(dev, adie, 0xa84, 0x8470008a);
	if (ret)
		return ret;

	/* turn on SX0 LTBUF */
	ret = mt76_wmac_spi_write(dev, adie, 0x074, 0x00000002);
	if (ret)
		return ret;

	/* CK_BUF_SW_EN = 1 (all buf in manual mode.) */
	ret = mt76_wmac_spi_write(dev, adie, 0xaa4, 0x01001fc0);
	if (ret)
		return ret;

	/* BT mode/WF normal mode 00000005 */
	ret = mt76_wmac_spi_write(dev, adie, 0x070, 0x00000005);
	if (ret)
		return ret;

	/* BG thermal sensor offset update */
	ret = mt76_wmac_spi_write(dev, adie, 0x344, 0x00000088);
	if (ret)
		return ret;

	ret = mt76_wmac_spi_write(dev, adie, 0x374, 0x00000088);
	if (ret)
		return ret;

	ret = mt76_wmac_spi_write(dev, adie, 0x3a4, 0x00000088);
	if (ret)
		return ret;

	ret = mt76_wmac_spi_write(dev, adie, 0x3d4, 0x00000088);
	if (ret)
		return ret;

	/* set WCON VDD IPTAT to "0000" */
	ret = mt76_wmac_spi_write(dev, adie, 0xa80, 0x44d07000);
	if (ret)
		return ret;

	/* change back LTBUF SX3 drving to default value */
	ret = mt76_wmac_spi_write(dev, adie, 0xa88, 0x3900aaaa);
	if (ret)
		return ret;

	/* SM input cap off */
	ret = mt76_wmac_spi_write(dev, adie, 0x2c4, 0x00000000);
	if (ret)
		return ret;

	/* set CKB driving and filter */
	return mt76_wmac_spi_write(dev, adie, 0x2c8, 0x00000072);
}

static int mt7986_wmac_adie_cfg(struct mt7915_dev *dev, u8 adie, u32 adie_type)
{
	int ret;

	mt76_wmac_spi_lock(dev);
	ret = mt76_wmac_spi_write(dev, adie, MT_ADIE_CLK_EN, ~0);
	if (ret)
		goto out;

	if (is_7975(dev, adie, adie_type)) {
		ret = mt76_wmac_spi_rmw(dev, adie, MT_ADIE_7975_COCLK,
					BIT(1), 0x1);
		if (ret)
			goto out;

		ret = mt7986_wmac_adie_thermal_cal(dev, adie);
		if (ret)
			goto out;

		ret = mt7986_wmac_adie_xtal_trim_7975(dev, adie);
		if (ret)
			goto out;

		ret = mt7986_wmac_adie_patch_7975(dev, adie);
	} else if (is_7976(dev, adie, adie_type)) {
		if (mt7986_wmac_check_adie_type(dev) == ADIE_DBDC) {
			ret = mt76_wmac_spi_write(dev, adie,
						  MT_ADIE_WRI_CK_SEL, 0x1c);
			if (ret)
				goto out;
		}

		ret = mt7986_wmac_adie_thermal_cal(dev, adie);
		if (ret)
			goto out;

		ret = mt7986_wmac_adie_xtal_trim_7976(dev, adie);
		if (ret)
			goto out;

		ret = mt7986_wmac_adie_patch_7976(dev, adie);
	}
out:
	mt76_wmac_spi_unlock(dev);

	return ret;
}

static int
mt7986_wmac_afe_cal(struct mt7915_dev *dev, u8 adie, bool dbdc, u32 adie_type)
{
	int ret;
	u8 idx;

	mt76_wmac_spi_lock(dev);
	if (is_7975(dev, adie, adie_type))
		ret = mt76_wmac_spi_write(dev, adie,
					  MT_AFE_RG_ENCAL_WBTAC_IF_SW,
					  0x80000000);
	else
		ret = mt76_wmac_spi_write(dev, adie,
					  MT_AFE_RG_ENCAL_WBTAC_IF_SW,
					  0x88888005);
	if (ret)
		goto out;

	idx = dbdc ? ADIE_DBDC : adie;

	mt76_rmw_field(dev, MT_AFE_DIG_EN_01(idx),
		       MT_AFE_RG_WBG_EN_RCK_MASK, 0x1);
	usleep_range(60, 100);

	mt76_rmw(dev, MT_AFE_DIG_EN_01(idx),
		 MT_AFE_RG_WBG_EN_RCK_MASK, 0x0);

	mt76_rmw_field(dev, MT_AFE_DIG_EN_03(idx),
		       MT_AFE_RG_WBG_EN_BPLL_UP_MASK, 0x1);
	usleep_range(30, 100);

	mt76_rmw_field(dev, MT_AFE_DIG_EN_03(idx),
		       MT_AFE_RG_WBG_EN_WPLL_UP_MASK, 0x1);
	usleep_range(60, 100);

	mt76_rmw_field(dev, MT_AFE_DIG_EN_01(idx),
		       MT_AFE_RG_WBG_EN_TXCAL_MASK, 0x1f);
	usleep_range(800, 1000);

	mt76_rmw(dev, MT_AFE_DIG_EN_01(idx),
		 MT_AFE_RG_WBG_EN_TXCAL_MASK, 0x0);
	mt76_rmw(dev, MT_AFE_DIG_EN_03(idx),
		 MT_AFE_RG_WBG_EN_PLL_UP_MASK, 0x0);

	ret = mt76_wmac_spi_write(dev, adie, MT_AFE_RG_ENCAL_WBTAC_IF_SW,
				  0x5);

out:
	mt76_wmac_spi_unlock(dev);

	return ret;
}

static void mt7986_wmac_subsys_pll_initial(struct mt7915_dev *dev, u8 band)
{
	mt76_rmw(dev, MT_AFE_PLL_STB_TIME(band),
		 MT_AFE_PLL_STB_TIME_MASK, MT_AFE_PLL_STB_TIME_VAL);

	mt76_rmw(dev, MT_AFE_DIG_EN_02(band),
		 MT_AFE_PLL_CFG_MASK, MT_AFE_PLL_CFG_VAL);

	mt76_rmw(dev, MT_AFE_DIG_TOP_01(band),
		 MT_AFE_DIG_TOP_01_MASK, MT_AFE_DIG_TOP_01_VAL);
}

static void mt7986_wmac_subsys_setting(struct mt7915_dev *dev)
{
	/* Subsys pll init */
	mt7986_wmac_subsys_pll_initial(dev, 0);
	mt7986_wmac_subsys_pll_initial(dev, 1);

	/* Set legacy OSC control stable time*/
	mt76_rmw(dev, MT_CONN_INFRA_OSC_RC_EN,
		 MT_CONN_INFRA_OSC_RC_EN_MASK, 0x0);
	mt76_rmw(dev, MT_CONN_INFRA_OSC_CTRL,
		 MT_CONN_INFRA_OSC_STB_TIME_MASK, 0x80706);

	/* prevent subsys from power on/of in a short time interval */
	mt76_rmw(dev, MT_TOP_WFSYS_PWR,
		 MT_TOP_PWR_ACK_MASK | MT_TOP_PWR_KEY_MASK,
		 MT_TOP_PWR_KEY);
}

static int mt7986_wmac_bus_timeout(struct mt7915_dev *dev)
{
	mt76_rmw_field(dev, MT_INFRA_BUS_OFF_TIMEOUT,
		       MT_INFRA_BUS_TIMEOUT_LIMIT_MASK, 0x2);

	mt76_rmw_field(dev, MT_INFRA_BUS_OFF_TIMEOUT,
		       MT_INFRA_BUS_TIMEOUT_EN_MASK, 0xf);

	mt76_rmw_field(dev, MT_INFRA_BUS_ON_TIMEOUT,
		       MT_INFRA_BUS_TIMEOUT_LIMIT_MASK, 0xc);

	mt76_rmw_field(dev, MT_INFRA_BUS_ON_TIMEOUT,
		       MT_INFRA_BUS_TIMEOUT_EN_MASK, 0xf);

	return mt7986_wmac_coninfra_check(dev);
}

static void mt7986_wmac_clock_enable(struct mt7915_dev *dev, u32 adie_type)
{
	u32 cur;

	mt76_rmw_field(dev, MT_INFRA_CKGEN_BUS_WPLL_DIV_1,
		       MT_INFRA_CKGEN_DIV_SEL_MASK, 0x1);

	mt76_rmw_field(dev, MT_INFRA_CKGEN_BUS_WPLL_DIV_2,
		       MT_INFRA_CKGEN_DIV_SEL_MASK, 0x1);

	mt76_rmw_field(dev, MT_INFRA_CKGEN_BUS_WPLL_DIV_1,
		       MT_INFRA_CKGEN_DIV_EN_MASK, 0x1);

	mt76_rmw_field(dev, MT_INFRA_CKGEN_BUS_WPLL_DIV_2,
		       MT_INFRA_CKGEN_DIV_EN_MASK, 0x1);

	mt76_rmw_field(dev, MT_INFRA_CKGEN_RFSPI_WPLL_DIV,
		       MT_INFRA_CKGEN_DIV_SEL_MASK, 0x8);

	mt76_rmw_field(dev, MT_INFRA_CKGEN_RFSPI_WPLL_DIV,
		       MT_INFRA_CKGEN_DIV_EN_MASK, 0x1);

	mt76_rmw_field(dev, MT_INFRA_CKGEN_BUS,
		       MT_INFRA_CKGEN_BUS_CLK_SEL_MASK, 0x0);

	mt76_rmw_field(dev, MT_CONN_INFRA_HW_CTRL,
		       MT_CONN_INFRA_HW_CTRL_MASK, 0x1);

	mt76_rmw(dev, MT_TOP_CONN_INFRA_WAKEUP,
		 MT_TOP_CONN_INFRA_WAKEUP_MASK, 0x1);

	usleep_range(900, 1000);

	mt76_wmac_spi_lock(dev);
	if (is_7975(dev, 0, adie_type) || is_7976(dev, 0, adie_type)) {
		mt76_rmw_field(dev, MT_ADIE_SLP_CTRL_CK0(0),
			       MT_SLP_CTRL_EN_MASK, 0x1);

		read_poll_timeout(mt76_rr, cur, !(cur & MT_SLP_CTRL_BSY_MASK),
				  USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
				  dev, MT_ADIE_SLP_CTRL_CK0(0));
	}
	if (is_7975(dev, 1, adie_type) || is_7976(dev, 1, adie_type)) {
		mt76_rmw_field(dev, MT_ADIE_SLP_CTRL_CK0(1),
			       MT_SLP_CTRL_EN_MASK, 0x1);

		read_poll_timeout(mt76_rr, cur, !(cur & MT_SLP_CTRL_BSY_MASK),
				  USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
				  dev, MT_ADIE_SLP_CTRL_CK0(0));
	}
	mt76_wmac_spi_unlock(dev);

	mt76_rmw(dev, MT_TOP_CONN_INFRA_WAKEUP,
		 MT_TOP_CONN_INFRA_WAKEUP_MASK, 0x0);
	usleep_range(900, 1000);
}

static int mt7986_wmac_top_wfsys_wakeup(struct mt7915_dev *dev, bool enable)
{
	mt76_rmw_field(dev, MT_TOP_WFSYS_WAKEUP,
		       MT_TOP_WFSYS_WAKEUP_MASK, enable);

	usleep_range(900, 1000);

	if (!enable)
		return 0;

	return mt7986_wmac_coninfra_check(dev);
}

static int mt7986_wmac_wm_enable(struct mt7915_dev *dev, bool enable)
{
	u32 cur;

	mt76_rmw_field(dev, MT7986_TOP_WM_RESET,
		       MT7986_TOP_WM_RESET_MASK, enable);
	if (!enable)
		return 0;

	return read_poll_timeout(mt76_rr, cur, (cur == 0x1d1e),
				 USEC_PER_MSEC, 5000 * USEC_PER_MSEC, false,
				 dev, MT_TOP_CFG_ON_ROM_IDX);
}

static int mt7986_wmac_wfsys_poweron(struct mt7915_dev *dev, bool enable)
{
	u32 mask = MT_TOP_PWR_EN_MASK | MT_TOP_PWR_KEY_MASK;
	u32 cur;

	mt76_rmw(dev, MT_TOP_WFSYS_PWR, mask,
		 MT_TOP_PWR_KEY | FIELD_PREP(MT_TOP_PWR_EN_MASK, enable));

	return read_poll_timeout(mt76_rr, cur,
		(FIELD_GET(MT_TOP_WFSYS_RESET_STATUS_MASK, cur) == enable),
		USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
		dev, MT_TOP_WFSYS_RESET_STATUS);
}

static int mt7986_wmac_wfsys_setting(struct mt7915_dev *dev)
{
	int ret;
	u32 cur;

	/* Turn off wfsys2conn bus sleep protect */
	mt76_rmw(dev, MT_CONN_INFRA_WF_SLP_PROT,
		 MT_CONN_INFRA_WF_SLP_PROT_MASK, 0x0);

	ret = mt7986_wmac_wfsys_poweron(dev, true);
	if (ret)
		return ret;

	/* Check bus sleep protect */

	ret = read_poll_timeout(mt76_rr, cur,
				!(cur & MT_CONN_INFRA_CONN_WF_MASK),
				USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
				dev, MT_CONN_INFRA_WF_SLP_PROT_RDY);
	if (ret)
		return ret;

	ret = read_poll_timeout(mt76_rr, cur, !(cur & MT_SLP_WFDMA2CONN_MASK),
				USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
				dev, MT_SLP_STATUS);
	if (ret)
		return ret;

	return read_poll_timeout(mt76_rr, cur, (cur == 0x02060000),
				 USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
				 dev, MT_TOP_CFG_IP_VERSION_ADDR);
}

static void mt7986_wmac_wfsys_set_timeout(struct mt7915_dev *dev)
{
	u32 mask = MT_MCU_BUS_TIMEOUT_SET_MASK |
		   MT_MCU_BUS_TIMEOUT_CG_EN_MASK |
		   MT_MCU_BUS_TIMEOUT_EN_MASK;
	u32 val = FIELD_PREP(MT_MCU_BUS_TIMEOUT_SET_MASK, 1) |
		  FIELD_PREP(MT_MCU_BUS_TIMEOUT_CG_EN_MASK, 1) |
		  FIELD_PREP(MT_MCU_BUS_TIMEOUT_EN_MASK, 1);

	mt76_rmw(dev, MT_MCU_BUS_TIMEOUT, mask, val);

	mt76_wr(dev, MT_MCU_BUS_REMAP, 0x810f0000);

	mask = MT_MCU_BUS_DBG_TIMEOUT_SET_MASK |
	       MT_MCU_BUS_DBG_TIMEOUT_CK_EN_MASK |
	       MT_MCU_BUS_DBG_TIMEOUT_EN_MASK;
	val = FIELD_PREP(MT_MCU_BUS_DBG_TIMEOUT_SET_MASK, 0x3aa) |
	      FIELD_PREP(MT_MCU_BUS_DBG_TIMEOUT_CK_EN_MASK, 1) |
	      FIELD_PREP(MT_MCU_BUS_DBG_TIMEOUT_EN_MASK, 1);

	mt76_rmw(dev, MT_MCU_BUS_DBG_TIMEOUT, mask, val);
}

static int mt7986_wmac_sku_update(struct mt7915_dev *dev, u32 adie_type)
{
	u32 val;

	if (is_7976(dev, 0, adie_type) && is_7976(dev, 1, adie_type))
		val = 0xf;
	else if (is_7975(dev, 0, adie_type) && is_7975(dev, 1, adie_type))
		val = 0xd;
	else if (is_7976(dev, 0, adie_type))
		val = 0x7;
	else if (is_7975(dev, 1, adie_type))
		val = 0x8;
	else if (is_7976(dev, 1, adie_type))
		val = 0xa;
	else
		return -EINVAL;

	mt76_wmac_rmw(dev->sku, MT_TOP_POS_SKU, MT_TOP_POS_SKU_MASK,
		      FIELD_PREP(MT_TOP_POS_SKU_MASK, val));

	mt76_wr(dev, MT_CONNINFRA_SKU_DEC_ADDR, val);

	return 0;
}

static int
mt7986_wmac_adie_setup(struct mt7915_dev *dev, u8 adie, u32 adie_type)
{
	int ret;

	if (!(is_7975(dev, adie, adie_type) || is_7976(dev, adie, adie_type)))
		return 0;

	ret = mt7986_wmac_adie_cfg(dev, adie, adie_type);
	if (ret)
		return ret;

	ret = mt7986_wmac_afe_cal(dev, adie, false, adie_type);
	if (ret)
		return ret;

	if (!adie && (mt7986_wmac_check_adie_type(dev) == ADIE_DBDC))
		ret = mt7986_wmac_afe_cal(dev, adie, true, adie_type);

	return ret;
}

static int mt7986_wmac_subsys_powerup(struct mt7915_dev *dev, u32 adie_type)
{
	int ret;

	mt7986_wmac_subsys_setting(dev);

	ret = mt7986_wmac_bus_timeout(dev);
	if (ret)
		return ret;

	mt7986_wmac_clock_enable(dev, adie_type);

	return 0;
}

static int mt7986_wmac_wfsys_powerup(struct mt7915_dev *dev)
{
	int ret;

	ret = mt7986_wmac_wm_enable(dev, false);
	if (ret)
		return ret;

	ret = mt7986_wmac_wfsys_setting(dev);
	if (ret)
		return ret;

	mt7986_wmac_wfsys_set_timeout(dev);

	return mt7986_wmac_wm_enable(dev, true);
}

int mt7986_wmac_enable(struct mt7915_dev *dev)
{
	int ret;
	u32 adie_type;

	ret = mt7986_wmac_consys_reset(dev, true);
	if (ret)
		return ret;

	ret = mt7986_wmac_gpio_setup(dev);
	if (ret)
		return ret;

	ret = mt7986_wmac_consys_lockup(dev, false);
	if (ret)
		return ret;

	ret = mt7986_wmac_coninfra_check(dev);
	if (ret)
		return ret;

	ret = mt7986_wmac_coninfra_setup(dev);
	if (ret)
		return ret;

	ret = mt7986_wmac_sku_setup(dev, &adie_type);
	if (ret)
		return ret;

	ret = mt7986_wmac_adie_setup(dev, 0, adie_type);
	if (ret)
		return ret;

	ret = mt7986_wmac_adie_setup(dev, 1, adie_type);
	if (ret)
		return ret;

	ret = mt7986_wmac_subsys_powerup(dev, adie_type);
	if (ret)
		return ret;

	ret = mt7986_wmac_top_wfsys_wakeup(dev, true);
	if (ret)
		return ret;

	ret = mt7986_wmac_wfsys_powerup(dev);
	if (ret)
		return ret;

	return mt7986_wmac_sku_update(dev, adie_type);
}

void mt7986_wmac_disable(struct mt7915_dev *dev)
{
	u32 cur;

	mt7986_wmac_top_wfsys_wakeup(dev, true);

	/* Turn on wfsys2conn bus sleep protect */
	mt76_rmw_field(dev, MT_CONN_INFRA_WF_SLP_PROT,
		       MT_CONN_INFRA_WF_SLP_PROT_MASK, 0x1);

	/* Check wfsys2conn bus sleep protect */
	read_poll_timeout(mt76_rr, cur, !(cur ^ MT_CONN_INFRA_CONN),
			  USEC_PER_MSEC, 50 * USEC_PER_MSEC, false,
			  dev, MT_CONN_INFRA_WF_SLP_PROT_RDY);

	mt7986_wmac_wfsys_poweron(dev, false);

	/* Turn back wpll setting */
	mt76_rmw_field(dev, MT_AFE_DIG_EN_02(0), MT_AFE_MCU_BPLL_CFG_MASK, 0x2);
	mt76_rmw_field(dev, MT_AFE_DIG_EN_02(0), MT_AFE_WPLL_CFG_MASK, 0x2);

	/* Reset EMI */
	mt76_rmw_field(dev, MT_CONN_INFRA_EMI_REQ,
		       MT_CONN_INFRA_EMI_REQ_MASK, 0x1);
	mt76_rmw_field(dev, MT_CONN_INFRA_EMI_REQ,
		       MT_CONN_INFRA_EMI_REQ_MASK, 0x0);
	mt76_rmw_field(dev, MT_CONN_INFRA_EMI_REQ,
		       MT_CONN_INFRA_INFRA_REQ_MASK, 0x1);
	mt76_rmw_field(dev, MT_CONN_INFRA_EMI_REQ,
		       MT_CONN_INFRA_INFRA_REQ_MASK, 0x0);

	mt7986_wmac_top_wfsys_wakeup(dev, false);
	mt7986_wmac_consys_lockup(dev, true);
	mt7986_wmac_consys_reset(dev, false);
}

static int mt7986_wmac_init(struct mt7915_dev *dev)
{
	struct device *pdev = dev->mt76.dev;
	struct platform_device *pfdev = to_platform_device(pdev);

	dev->dcm = devm_platform_ioremap_resource(pfdev, 1);
	if (IS_ERR(dev->dcm))
		return PTR_ERR(dev->dcm);

	dev->sku = devm_platform_ioremap_resource(pfdev, 2);
	if (IS_ERR(dev->sku))
		return PTR_ERR(dev->sku);

	dev->rstc = devm_reset_control_get(pdev, "consys");
	if (IS_ERR(dev->rstc))
		return PTR_ERR(dev->rstc);

	return mt7986_wmac_enable(dev);
}

static int mt7986_wmac_probe(struct platform_device *pdev)
{
	void __iomem *mem_base;
	struct mt7915_dev *dev;
	struct mt76_dev *mdev;
	int irq, ret;
	u32 chip_id;

	chip_id = (uintptr_t)of_device_get_match_data(&pdev->dev);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	mem_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mem_base)) {
		dev_err(&pdev->dev, "Failed to get memory resource\n");
		return PTR_ERR(mem_base);
	}

	dev = mt7915_mmio_probe(&pdev->dev, mem_base, chip_id);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	mdev = &dev->mt76;
	ret = devm_request_irq(mdev->dev, irq, mt7915_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto free_device;

	mt76_wr(dev, MT_INT_MASK_CSR, 0);

	ret = mt7986_wmac_init(dev);
	if (ret)
		goto free_irq;

	ret = mt7915_register_device(dev);
	if (ret)
		goto free_irq;

	return 0;

free_irq:
	devm_free_irq(mdev->dev, irq, dev);

free_device:
	mt76_free_device(&dev->mt76);

	return ret;
}

static int mt7986_wmac_remove(struct platform_device *pdev)
{
	struct mt7915_dev *dev = platform_get_drvdata(pdev);

	mt7915_unregister_device(dev);

	return 0;
}

static const struct of_device_id mt7986_wmac_of_match[] = {
	{ .compatible = "mediatek,mt7986-wmac", .data = (u32 *)0x7986 },
	{},
};

struct platform_driver mt7986_wmac_driver = {
	.driver = {
		.name = "mt7986-wmac",
		.of_match_table = mt7986_wmac_of_match,
	},
	.probe = mt7986_wmac_probe,
	.remove = mt7986_wmac_remove,
};

MODULE_FIRMWARE(MT7986_FIRMWARE_WA);
MODULE_FIRMWARE(MT7986_FIRMWARE_WM);
MODULE_FIRMWARE(MT7986_FIRMWARE_WM_MT7975);
MODULE_FIRMWARE(MT7986_ROM_PATCH);
MODULE_FIRMWARE(MT7986_ROM_PATCH_MT7975);

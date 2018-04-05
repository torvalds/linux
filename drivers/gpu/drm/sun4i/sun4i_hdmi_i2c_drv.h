/*
 * Copyright (C) 2016 Maxime Ripard
 * Copyright (C) 2017 Chen-Yu Tsai
 * Copyright (C) 2017 Jonathan Liu
 * Copyright (C) 2017 Olliver Schinagl
 *
 * SPDX-License-Identifier:	GPL-2.0+
 *
 */

#ifndef _SUN4I_HDMI_I2C_DRV_H_
#define _SUN4I_HDMI_I2C_DRV_H_

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define SUN4I_HDMI_DDC_OFFSET		0x500

#define SUN4I_HDMI_DDC_CTRL_REG		0x00
#define SUN4I_HDMI_DDC_CTRL_ENABLE		BIT(31)
#define SUN4I_HDMI_DDC_CTRL_START_CMD		BIT(30)
#define SUN4I_HDMI_DDC_CTRL_FIFO_DIR_MASK	BIT(8)
#define SUN4I_HDMI_DDC_CTRL_FIFO_DIR_WRITE	(1 << 8)
#define SUN4I_HDMI_DDC_CTRL_FIFO_DIR_READ	(0 << 8)
#define SUN4I_HDMI_DDC_CTRL_RESET		BIT(0)

#define SUN4I_HDMI_DDC_ADDR_REG		0x04
#define SUN4I_HDMI_DDC_ADDR_SEGMENT(seg)	(((seg) << 24) & GENMASK(31, 24))
#define SUN4I_HDMI_DDC_ADDR_EDDC(addr)		(((addr) << 16) & GENMASK(23, 16))
#define SUN4I_HDMI_DDC_ADDR_OFFSET(off)		(((off) << 8) & GENMASK(15, 8))
#define SUN4I_HDMI_DDC_ADDR_SLAVE(addr)		((addr) & GENMASK(7, 0))

#define SUN4I_HDMI_DDC_INT_STATUS_REG	0x0c
#define SUN4I_HDMI_DDC_INT_STATUS_ILLEGAL_FIFO_OPERATION	BIT(7)
#define SUN4I_HDMI_DDC_INT_STATUS_DDC_RX_FIFO_UNDERFLOW		BIT(6)
#define SUN4I_HDMI_DDC_INT_STATUS_DDC_TX_FIFO_OVERFLOW		BIT(5)
#define SUN4I_HDMI_DDC_INT_STATUS_FIFO_REQUEST			BIT(4)
#define SUN4I_HDMI_DDC_INT_STATUS_ARBITRATION_ERROR		BIT(3)
#define SUN4I_HDMI_DDC_INT_STATUS_ACK_ERROR			BIT(2)
#define SUN4I_HDMI_DDC_INT_STATUS_BUS_ERROR			BIT(1)
#define SUN4I_HDMI_DDC_INT_STATUS_TRANSFER_COMPLETE		BIT(0)

#define SUN4I_HDMI_DDC_INT_STATUS_ERROR_MASK ( \
	SUN4I_HDMI_DDC_INT_STATUS_ILLEGAL_FIFO_OPERATION | \
	SUN4I_HDMI_DDC_INT_STATUS_DDC_RX_FIFO_UNDERFLOW | \
	SUN4I_HDMI_DDC_INT_STATUS_DDC_TX_FIFO_OVERFLOW | \
	SUN4I_HDMI_DDC_INT_STATUS_ARBITRATION_ERROR | \
	SUN4I_HDMI_DDC_INT_STATUS_ACK_ERROR | \
	SUN4I_HDMI_DDC_INT_STATUS_BUS_ERROR \
)

#define SUN4I_HDMI_DDC_FIFO_CTRL_REG	0x10
#define SUN4I_HDMI_DDC_FIFO_CTRL_CLEAR		BIT(31)
#define SUN4I_HDMI_DDC_FIFO_CTRL_RX_THRES_MASK	GENMASK(7, 4)
#define SUN4I_HDMI_DDC_FIFO_CTRL_RX_THRES(n)	\
	(((n) << 4) & SUN4I_HDMI_DDC_FIFO_CTRL_RX_THRES_MASK)
#define SUN4I_HDMI_DDC_FIFO_CTRL_RX_THRES_MAX	(BIT(4) - 1)
#define SUN4I_HDMI_DDC_FIFO_CTRL_TX_THRES_MASK	GENMASK(3, 0)
#define SUN4I_HDMI_DDC_FIFO_CTRL_TX_THRES(n)	\
	((n) & SUN4I_HDMI_DDC_FIFO_CTRL_TX_THRES_MASK)
#define SUN4I_HDMI_DDC_FIFO_CTRL_TX_THRES_MAX	(BIT(4) - 1)

#define SUN4I_HDMI_DDC_FIFO_DATA_REG	0x18

#define SUN4I_HDMI_DDC_BYTE_COUNT_REG	0x1c
#define SUN4I_HDMI_DDC_BYTE_COUNT_MAX		(BIT(10) - 1)

#define SUN4I_HDMI_DDC_CMD_REG		0x20
#define SUN4I_HDMI_DDC_CMD_EXPLICIT_EDDC_READ	0x6
#define SUN4I_HDMI_DDC_CMD_IMPLICIT_READ	0x5
#define SUN4I_HDMI_DDC_CMD_IMPLICIT_WRITE	0x3

#define SUN4I_HDMI_DDC_EXT_REG		0x24
#define SUN4I_HDMI_DDC_EXT_BUS_BUSY		BIT(10)
#define SUN4I_HDMI_DDC_EXT_SDA_STATE		BIT(9)
#define SUN4I_HDMI_DDC_EXT_SCK_STATE		BIT(8)
#define SUN4I_HDMI_DDC_EXT_SCL_LINE_CTRL	BIT(3)
#define SUN4I_HDMI_DDC_EXT_SCL_LINE_CTRL_EN	BIT(2)
#define SUN4I_HDMI_DDC_EXT_SDA_LINE_CTRL	BIT(1)
#define SUN4I_HDMI_DDC_EXT_SDA_LINE_CTRL_EN	BIT(0)

#define SUN4I_HDMI_DDC_CLK_REG		0x28
#define SUN4I_HDMI_DDC_CLK_M_OFFSET            3
#define SUN4I_HDMI_DDC_CLK_M_MASK              GENMASK(7, 4)
#define SUN4I_HDMI_DDC_CLK_N_MASK              GENMASK(3, 0)
#define SUN4I_HDMI_DDC_CLK_M(m)                        \
       (((m) << SUN4I_HDMI_DDC_CLK_M_OFFSET) & SUN4I_HDMI_DDC_CLK_M_MASK)
#define SUN4I_HDMI_DDC_CLK_N(n)                        \
       ((n) & SUN4I_HDMI_DDC_CLK_N_MASK)
#define SUN4I_HDMI_DDC_CLK_M_GET(reg)          \
       (((reg) & SUN4I_HDMI_DDC_CLK_M_MASK) >> SUN4I_HDMI_DDC_CLK_M_OFFSET)
#define SUN4I_HDMI_DDC_CLK_N_GET(reg)          \
       ((reg) & SUN4I_HDMI_DDC_CLK_N_MASK)

#define SUN4I_HDMI_DDC_LINE_CTRL_REG	0x40
#define SUN4I_HDMI_DDC_LINE_CTRL_SDA_ENABLE	BIT(9)
#define SUN4I_HDMI_DDC_LINE_CTRL_SCL_ENABLE	BIT(8)

#define SUN4I_HDMI_DDC_FIFO_SIZE	16

/* A31 specific */
#define SUN6I_HDMI_DDC_CTRL_REG		0x00
#define SUN6I_HDMI_DDC_CTRL_RESET		BIT(31)
#define SUN6I_HDMI_DDC_CTRL_START_CMD		BIT(27)
#define SUN6I_HDMI_DDC_CTRL_SDA_ENABLE		BIT(6)
#define SUN6I_HDMI_DDC_CTRL_SCL_ENABLE		BIT(4)
#define SUN6I_HDMI_DDC_CTRL_ENABLE		BIT(0)

#define SUN6I_HDMI_DDC_EXT_REG		0x04

#define SUN6I_HDMI_DDC_CMD_REG		0x08
#define SUN6I_HDMI_DDC_CMD_BYTE_COUNT(count)	((count) << 16)
/* command types in lower 3 bits are the same as sun4i */

#define SUN6I_HDMI_DDC_ADDR_REG		0x0c
#define SUN6I_HDMI_DDC_ADDR_SEGMENT(seg)	(((seg) << 24) & GENMASK(31, 24))
#define SUN6I_HDMI_DDC_ADDR_EDDC(addr)		(((addr) << 16) & GENMASK(23, 16))
#define SUN6I_HDMI_DDC_ADDR_OFFSET(off)		(((off) << 8) & GENMASK(15, 8))
#define SUN6I_HDMI_DDC_ADDR_SLAVE(addr)		(((addr) << 1) & GENMASK(7, 1))

#define SUN6I_HDMI_DDC_INT_STATUS_REG	0x14
#define SUN6I_HDMI_DDC_INT_STATUS_TIMEOUT	BIT(8)
/* lower 8 bits are the same as sun4i */

#define SUN6I_HDMI_DDC_FIFO_CTRL_REG	0x18
#define SUN6I_HDMI_DDC_FIFO_CTRL_CLEAR		BIT(15)
/* lower 9 bits are the same as sun4i */

#define SUN6I_HDMI_DDC_CLK_REG		0x20
/* DDC CLK bit fields are the same, but the formula is not */

#define SUN6I_HDMI_DDC_FIFO_DATA_REG	0x80

struct sun4i_hdmi_i2c_variant {
	char			*parent_clk_name;

	struct reg_field	ddc_clk_reg;
	u8			ddc_clk_pre_divider;
	u8			ddc_clk_m_offset;

	/* Register fields for I2C adapter */
	struct reg_field	field_ddc_en;
	struct reg_field	field_ddc_start;
	struct reg_field	field_ddc_reset;
	struct reg_field	field_ddc_addr_reg;
	struct reg_field	field_ddc_slave_addr;
	struct reg_field	field_ddc_int_mask;
	struct reg_field	field_ddc_int_status;
	struct reg_field	field_ddc_fifo_clear;
	struct reg_field	field_ddc_fifo_rx_thres;
	struct reg_field	field_ddc_fifo_tx_thres;
	struct reg_field	field_ddc_byte_count;
	struct reg_field	field_ddc_cmd;
	struct reg_field	field_ddc_sda_en;
	struct reg_field	field_ddc_sck_en;
	struct reg_field	field_ddc_bus_busy;
	struct reg_field	field_ddc_sda_state;
	struct reg_field	field_ddc_sck_state;
	struct reg_field	field_ddc_sda_line_ctrl_en;
	struct reg_field	field_ddc_sck_line_ctrl_en;
	struct reg_field	field_ddc_sda_line_ctrl;
	struct reg_field	field_ddc_sck_line_ctrl;


	/* DDC FIFO register offset */
	u32			ddc_fifo_reg;

	/*
	 * DDC FIFO threshold boundary conditions
	 *
	 * This is used to cope with the threshold boundary condition
	 * being slightly different on sun5i and sun6i.
	 *
	 * On sun5i the threshold is exclusive, i.e. does not include,
	 * the value of the threshold. ( > for RX; < for TX )
	 * On sun6i the threshold is inclusive, i.e. includes, the
	 * value of the threshold. ( >= for RX; <= for TX )
	 */
	bool			ddc_fifo_thres_incl;

	bool			ddc_fifo_has_dir;
};

struct sun4i_hdmi_i2c_drv {
	struct device		*dev;

	void __iomem		*base;
	struct regmap		*regmap;

	struct clk		*parent_clk;
	struct clk		*ddc_clk;
	uint32_t		clock_freq;

	struct i2c_adapter	adap;

	struct regmap_field	*field_ddc_en;
	struct regmap_field	*field_ddc_start;
	struct regmap_field	*field_ddc_reset;
	struct regmap_field	*field_ddc_addr_reg;
	struct regmap_field	*field_ddc_slave_addr;
	struct regmap_field	*field_ddc_int_mask;
	struct regmap_field	*field_ddc_int_status;
	struct regmap_field	*field_ddc_fifo_clear;
	struct regmap_field	*field_ddc_fifo_rx_thres;
	struct regmap_field	*field_ddc_fifo_tx_thres;
	struct regmap_field	*field_ddc_byte_count;
	struct regmap_field	*field_ddc_cmd;
	struct regmap_field	*field_ddc_sda_en;
	struct regmap_field	*field_ddc_sck_en;
	struct regmap_field	*field_ddc_bus_busy;
	struct regmap_field	*field_ddc_sda_state;
	struct regmap_field	*field_ddc_sck_state;
	struct regmap_field	*field_ddc_sda_line_ctrl;
	struct regmap_field	*field_ddc_sck_line_ctrl;
	struct regmap_field	*field_ddc_sda_line_ctrl_en;
	struct regmap_field	*field_ddc_sck_line_ctrl_en;


	const struct sun4i_hdmi_i2c_variant	*variant;
};

struct sun4i_hdmi_i2c_drv
*sun4i_hdmi_i2c_init(struct device *dev, void __iomem *base,
		     const struct of_device_id *of_id_table,
		     const struct regmap_config *regmap_config,
		     struct clk *parent_clk);

void sun4i_hdmi_i2c_fini(struct sun4i_hdmi_i2c_drv *drv);

struct sun4i_hdmi_i2c_drv *sun4i_hdmi_i2c_setup(struct device *dev,
						void __iomem *base,
						struct clk *clk);

#endif

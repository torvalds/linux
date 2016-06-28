/* drivers/media/platform/s5p-cec/exynos_hdmi_cecctrl.c
 *
 * Copyright (c) 2009, 2014 Samsung Electronics
 *		http://www.samsung.com/
 *
 * cec ftn file for Samsung TVOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/device.h>

#include "exynos_hdmi_cec.h"
#include "regs-cec.h"

#define S5P_HDMI_FIN			24000000
#define CEC_DIV_RATIO			320000

#define CEC_MESSAGE_BROADCAST_MASK	0x0F
#define CEC_MESSAGE_BROADCAST		0x0F
#define CEC_FILTER_THRESHOLD		0x15

void s5p_cec_set_divider(struct s5p_cec_dev *cec)
{
	u32 div_ratio, div_val;
	unsigned int reg;

	div_ratio  = S5P_HDMI_FIN / CEC_DIV_RATIO - 1;

	if (regmap_read(cec->pmu, EXYNOS_HDMI_PHY_CONTROL, &reg)) {
		dev_err(cec->dev, "failed to read phy control\n");
		return;
	}

	reg = (reg & ~(0x3FF << 16)) | (div_ratio << 16);

	if (regmap_write(cec->pmu, EXYNOS_HDMI_PHY_CONTROL, reg)) {
		dev_err(cec->dev, "failed to write phy control\n");
		return;
	}

	div_val = CEC_DIV_RATIO * 0.00005 - 1;

	writeb(0x0, cec->reg + S5P_CEC_DIVISOR_3);
	writeb(0x0, cec->reg + S5P_CEC_DIVISOR_2);
	writeb(0x0, cec->reg + S5P_CEC_DIVISOR_1);
	writeb(div_val, cec->reg + S5P_CEC_DIVISOR_0);
}

void s5p_cec_enable_rx(struct s5p_cec_dev *cec)
{
	u8 reg;

	reg = readb(cec->reg + S5P_CEC_RX_CTRL);
	reg |= S5P_CEC_RX_CTRL_ENABLE;
	writeb(reg, cec->reg + S5P_CEC_RX_CTRL);
}

void s5p_cec_mask_rx_interrupts(struct s5p_cec_dev *cec)
{
	u8 reg;

	reg = readb(cec->reg + S5P_CEC_IRQ_MASK);
	reg |= S5P_CEC_IRQ_RX_DONE;
	reg |= S5P_CEC_IRQ_RX_ERROR;
	writeb(reg, cec->reg + S5P_CEC_IRQ_MASK);
}

void s5p_cec_unmask_rx_interrupts(struct s5p_cec_dev *cec)
{
	u8 reg;

	reg = readb(cec->reg + S5P_CEC_IRQ_MASK);
	reg &= ~S5P_CEC_IRQ_RX_DONE;
	reg &= ~S5P_CEC_IRQ_RX_ERROR;
	writeb(reg, cec->reg + S5P_CEC_IRQ_MASK);
}

void s5p_cec_mask_tx_interrupts(struct s5p_cec_dev *cec)
{
	u8 reg;

	reg = readb(cec->reg + S5P_CEC_IRQ_MASK);
	reg |= S5P_CEC_IRQ_TX_DONE;
	reg |= S5P_CEC_IRQ_TX_ERROR;
	writeb(reg, cec->reg + S5P_CEC_IRQ_MASK);

}

void s5p_cec_unmask_tx_interrupts(struct s5p_cec_dev *cec)
{
	u8 reg;

	reg = readb(cec->reg + S5P_CEC_IRQ_MASK);
	reg &= ~S5P_CEC_IRQ_TX_DONE;
	reg &= ~S5P_CEC_IRQ_TX_ERROR;
	writeb(reg, cec->reg + S5P_CEC_IRQ_MASK);
}

void s5p_cec_reset(struct s5p_cec_dev *cec)
{
	u8 reg;

	writeb(S5P_CEC_RX_CTRL_RESET, cec->reg + S5P_CEC_RX_CTRL);
	writeb(S5P_CEC_TX_CTRL_RESET, cec->reg + S5P_CEC_TX_CTRL);

	reg = readb(cec->reg + 0xc4);
	reg &= ~0x1;
	writeb(reg, cec->reg + 0xc4);
}

void s5p_cec_tx_reset(struct s5p_cec_dev *cec)
{
	writeb(S5P_CEC_TX_CTRL_RESET, cec->reg + S5P_CEC_TX_CTRL);
}

void s5p_cec_rx_reset(struct s5p_cec_dev *cec)
{
	u8 reg;

	writeb(S5P_CEC_RX_CTRL_RESET, cec->reg + S5P_CEC_RX_CTRL);

	reg = readb(cec->reg + 0xc4);
	reg &= ~0x1;
	writeb(reg, cec->reg + 0xc4);
}

void s5p_cec_threshold(struct s5p_cec_dev *cec)
{
	writeb(CEC_FILTER_THRESHOLD, cec->reg + S5P_CEC_RX_FILTER_TH);
	writeb(0, cec->reg + S5P_CEC_RX_FILTER_CTRL);
}

void s5p_cec_copy_packet(struct s5p_cec_dev *cec, char *data,
			 size_t count, u8 retries)
{
	int i = 0;
	u8 reg;

	while (i < count) {
		writeb(data[i], cec->reg + (S5P_CEC_TX_BUFF0 + (i * 4)));
		i++;
	}

	writeb(count, cec->reg + S5P_CEC_TX_BYTES);
	reg = readb(cec->reg + S5P_CEC_TX_CTRL);
	reg |= S5P_CEC_TX_CTRL_START;
	reg &= ~0x70;
	reg |= retries << 4;

	if ((data[0] & CEC_MESSAGE_BROADCAST_MASK) == CEC_MESSAGE_BROADCAST) {
		dev_dbg(cec->dev, "Broadcast");
		reg |= S5P_CEC_TX_CTRL_BCAST;
	} else {
		dev_dbg(cec->dev, "No Broadcast");
		reg &= ~S5P_CEC_TX_CTRL_BCAST;
	}

	writeb(reg, cec->reg + S5P_CEC_TX_CTRL);
	dev_dbg(cec->dev, "cec-tx: cec count (%zu): %*ph", count,
		(int)count, data);
}

void s5p_cec_set_addr(struct s5p_cec_dev *cec, u32 addr)
{
	writeb(addr & 0x0F, cec->reg + S5P_CEC_LOGIC_ADDR);
}

u32 s5p_cec_get_status(struct s5p_cec_dev *cec)
{
	u32 status = 0;

	status = readb(cec->reg + S5P_CEC_STATUS_0);
	status |= readb(cec->reg + S5P_CEC_STATUS_1) << 8;
	status |= readb(cec->reg + S5P_CEC_STATUS_2) << 16;
	status |= readb(cec->reg + S5P_CEC_STATUS_3) << 24;

	dev_dbg(cec->dev, "status = 0x%x!\n", status);

	return status;
}

void s5p_clr_pending_tx(struct s5p_cec_dev *cec)
{
	writeb(S5P_CEC_IRQ_TX_DONE | S5P_CEC_IRQ_TX_ERROR,
					cec->reg + S5P_CEC_IRQ_CLEAR);
}

void s5p_clr_pending_rx(struct s5p_cec_dev *cec)
{
	writeb(S5P_CEC_IRQ_RX_DONE | S5P_CEC_IRQ_RX_ERROR,
					cec->reg + S5P_CEC_IRQ_CLEAR);
}

void s5p_cec_get_rx_buf(struct s5p_cec_dev *cec, u32 size, u8 *buffer)
{
	u32 i = 0;
	char debug[40];

	while (i < size) {
		buffer[i] = readb(cec->reg + S5P_CEC_RX_BUFF0 + (i * 4));
		sprintf(debug + i * 2, "%02x ", buffer[i]);
		i++;
	}
	dev_dbg(cec->dev, "cec-rx: cec size(%d): %s", size, debug);
}

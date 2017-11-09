/*
 *RockChip DP (Display port) register interface driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/grf.h>
#include "rk32_dp.h"


void rk32_edp_enable_video_mute(struct rk32_edp *edp, bool enable)
{
	u32 val;

	if (enable) {
		val = readl(edp->regs + VIDEO_CTL_1);
		val |= VIDEO_MUTE;
		writel(val, edp->regs + VIDEO_CTL_1);
	} else {
		val = readl(edp->regs + VIDEO_CTL_1);
		val &= ~VIDEO_MUTE;
		writel(val, edp->regs + VIDEO_CTL_1);
	}
}

void rk32_edp_stop_video(struct rk32_edp *edp)
{
	u32 val;

	val = readl(edp->regs + VIDEO_CTL_1);
	val &= ~VIDEO_EN;
	writel(val, edp->regs + VIDEO_CTL_1);
}

void rk32_edp_lane_swap(struct rk32_edp *edp, bool enable)
{
	u32 val;


	if (enable)
		val = LANE3_MAP_LOGIC_LANE_0 | LANE2_MAP_LOGIC_LANE_1 |
			LANE1_MAP_LOGIC_LANE_2 | LANE0_MAP_LOGIC_LANE_3;
	else
		val = LANE3_MAP_LOGIC_LANE_3 | LANE2_MAP_LOGIC_LANE_2 |
			LANE1_MAP_LOGIC_LANE_1 | LANE0_MAP_LOGIC_LANE_0;

	writel(val, edp->regs + LANE_MAP);
}

void rk32_edp_init_refclk(struct rk32_edp *edp)
{
	u32 val;
	/*struct rk32_edp_platdata *pdata = edp->dev->platform_data;
	struct analog_param *analog_param = pdata->analog_param;

	val = TX_TERMINAL_CTRL_50_OHM;
	writel(val, edp->regs + ANALOG_CTL_1);*/

	val = SEL_24M;
	writel(val, edp->regs + ANALOG_CTL_2);

	if (edp->soctype == SOC_RK3399)
		val = 0x1 << 0;
	else
		val = REF_CLK_24M;
	writel(val, edp->regs + PLL_REG_1);

	val = 0x95;
	writel(val, edp->regs + PLL_REG_2);

	val = 0x40;
	writel(val, edp->regs + PLL_REG_3);

	val = 0x58;
	writel(val, edp->regs + PLL_REG_4);

	val = 0x22;
	writel(val, edp->regs + PLL_REG_5);

	val = 0x19;
	writel(val, edp->regs + SSC_REG);
	val = 0x87;
	writel(val, edp->regs + TX_REG_COMMON);
	val = 0x03;
	writel(val, edp->regs + DP_AUX);
	val = 0x46;
	writel(val, edp->regs + DP_BIAS);
	val = 0x55;
	writel(val, edp->regs + DP_RESERVE2);


	/*val = DRIVE_DVDD_BIT_1_0625V | VCO_BIT_600_MICRO;
	writel(val, edp->regs + ANALOG_CTL_3);

	if (!analog_param) {
		val = PD_RING_OSC | AUX_TERMINAL_CTRL_50_OHM |
			TX_CUR1_2X | TX_CUR_16_MA;
		writel(val, edp->regs + PLL_FILTER_CTL_1);

		val = CH3_AMP_400_MV | CH2_AMP_400_MV |
			CH1_AMP_400_MV | CH0_AMP_400_MV;
		writel(val, edp->regs + TX_AMP_TUNING_CTL);
	} else {
		int tx_amp;

		val = PD_RING_OSC | TX_CUR1_2X | TX_CUR_16_MA;
		switch (analog_param->aux_tx_terminal_resistor) {
		case AUX_TX_37_5_OHM:
			val |= AUX_TERMINAL_CTRL_37_5_OHM;
			break;
		case AUX_TX_45_OHM:
			val |= AUX_TERMINAL_CTRL_45_OHM;
			break;
		case AUX_TX_50_OHM:
			val |= AUX_TERMINAL_CTRL_50_OHM;
			break;
		case AUX_TX_65_OHM:
			val |= AUX_TERMINAL_CTRL_65_OHM;
			break;
		}
		writel(val, edp->regs + PLL_FILTER_CTL_1);

		tx_amp = analog_param->tx_amplitude;
		if (tx_amp < 200000 || tx_amp > 500000) {
			dev_warn(edp->dev,
				 "TX amp out of range, defaulting to 400mV\n");
			tx_amp = 400000;
		}

		tx_amp = ((tx_amp - 400000) / 12500) & 0x1f;

		val = (tx_amp << CH3_AMP_SHIFT) | (tx_amp << CH2_AMP_SHIFT) |
			(tx_amp << CH1_AMP_SHIFT) | (tx_amp << CH0_AMP_SHIFT);
		writel(val, edp->regs + TX_AMP_TUNING_CTL);
	}*/
}

void rk32_edp_init_interrupt(struct rk32_edp *edp)
{
	/* Set interrupt pin assertion polarity as high */
	writel(INT_POL, edp->regs + INT_CTL);

	/* Clear pending valisers */
	writel(0xff, edp->regs + COMMON_INT_STA_1);
	writel(0x4f, edp->regs + COMMON_INT_STA_2);
	writel(0xff, edp->regs + COMMON_INT_STA_3);
	writel(0x27, edp->regs + COMMON_INT_STA_4);

	writel(0x7f, edp->regs + DP_INT_STA);

	/* 0:mask,1: unmask */
	writel(0x00, edp->regs + COMMON_INT_MASK_1);
	writel(0x00, edp->regs + COMMON_INT_MASK_2);
	writel(0x00, edp->regs + COMMON_INT_MASK_3);
	writel(0x00, edp->regs + COMMON_INT_MASK_4);
	writel(0x00, edp->regs + DP_INT_STA_MASK);
}

void rk32_edp_reset(struct rk32_edp *edp)
{
	u32 val;

	rk32_edp_stop_video(edp);
	rk32_edp_enable_video_mute(edp, 0);

	val = VID_CAP_FUNC_EN_N | AUD_FIFO_FUNC_EN_N |
		AUD_FUNC_EN_N | HDCP_FUNC_EN_N | SW_FUNC_EN_N;
	writel(val, edp->regs + FUNC_EN_1);

	val = SSC_FUNC_EN_N | AUX_FUNC_EN_N |
		SERDES_FIFO_FUNC_EN_N |
		LS_CLK_DOMAIN_FUNC_EN_N;
	writel(val, edp->regs + FUNC_EN_2);

	udelay(20);

	rk32_edp_lane_swap(edp, 0);

	writel(0x0, edp->regs + SYS_CTL_1);
	writel(0x40, edp->regs + SYS_CTL_2);
	writel(0x0, edp->regs + SYS_CTL_3);
	writel(0x0, edp->regs + SYS_CTL_4);

	writel(0x0, edp->regs + PKT_SEND_CTL);
	writel(0x0, edp->regs + HDCP_CTL);

	writel(0x5e, edp->regs + HPD_DEGLITCH_L);
	writel(0x1a, edp->regs + HPD_DEGLITCH_H);

	writel(0x10, edp->regs + LINK_DEBUG_CTL);

	writel(0x0, edp->regs + VIDEO_FIFO_THRD);
	writel(0x20, edp->regs + AUDIO_MARGIN);

	writel(0x4, edp->regs + M_VID_GEN_FILTER_TH);
	writel(0x2, edp->regs + M_AUD_GEN_FILTER_TH);

	writel(0x0, edp->regs + SOC_GENERAL_CTL);

}

void rk32_edp_config_interrupt(struct rk32_edp *edp)
{
	u32 val;

	/* 0: mask, 1: unmask */
	val = 0;
	writel(val, edp->regs + COMMON_INT_MASK_1);

	writel(val, edp->regs + COMMON_INT_MASK_2);

	writel(val, edp->regs + COMMON_INT_MASK_3);

	writel(val, edp->regs + COMMON_INT_MASK_4);

	writel(val, edp->regs + DP_INT_STA_MASK);
}

u32 rk32_edp_get_pll_lock_status(struct rk32_edp *edp)
{
	u32 val;

	val = readl(edp->regs + DEBUG_CTL);
	if (val & PLL_LOCK)
		return DP_PLL_LOCKED;
	else
		return DP_PLL_UNLOCKED;
}


void rk32_edp_analog_power_ctr(struct rk32_edp *edp, bool enable)
{
	u32 val;

	if (enable) {
		val = PD_EXP_BG | PD_AUX | PD_PLL |
			PD_CH3 | PD_CH2 | PD_CH1 | PD_CH0;
		writel(val, edp->regs + DP_PWRDN);
		udelay(10);
		writel(0x0, edp->regs + DP_PWRDN);
	} else {
		val = PD_EXP_BG | PD_AUX | PD_PLL |
			PD_CH3 | PD_CH2 | PD_CH1 | PD_CH0;
		writel(val, edp->regs + DP_PWRDN);
	}
}


void rk32_edp_init_analog_func(struct rk32_edp *edp)
{
	u32 val;
	int wt = 0;
	rk32_edp_analog_power_ctr(edp, 1);

	val = PLL_LOCK_CHG;
	writel(val, edp->regs + COMMON_INT_STA_1);

	val = readl(edp->regs + DEBUG_CTL);
	val &= ~(F_PLL_LOCK | PLL_LOCK_CTRL);
	writel(val, edp->regs + DEBUG_CTL);

	/* Power up PLL */
	while (wt < 100) {
		if (rk32_edp_get_pll_lock_status(edp) == DP_PLL_LOCKED) {
			dev_info(edp->dev, "edp pll locked\n");
			break;
		} else {
			wt++;
			udelay(5);
		}
	}

	/* Enable Serdes FIFO function and Link symbol clock domain module */
	val = readl(edp->regs + FUNC_EN_2);
	val &= ~(SERDES_FIFO_FUNC_EN_N | LS_CLK_DOMAIN_FUNC_EN_N
		| AUX_FUNC_EN_N | SSC_FUNC_EN_N);
	writel(val, edp->regs + FUNC_EN_2);
}

void rk32_edp_init_hpd(struct rk32_edp *edp)
{
	u32 val;

	val = HOTPLUG_CHG | HPD_LOST | PLUG;
	writel(val, edp->regs + COMMON_INT_STA_4);

	val = INT_HPD;
	writel(val, edp->regs + DP_INT_STA);

	val = readl(edp->regs + SYS_CTL_3);
	val |= (F_HPD | HPD_CTRL);
	writel(val, edp->regs + SYS_CTL_3);
}

void rk32_edp_reset_aux(struct rk32_edp *edp)
{
	u32 val;

	/* Disable AUX channel module */
	val = readl(edp->regs + FUNC_EN_2);
	val |= AUX_FUNC_EN_N;
	writel(val, edp->regs + FUNC_EN_2);
}

void rk32_edp_init_aux(struct rk32_edp *edp)
{
	u32 val;

	/* Clear inerrupts related to AUX channel */
	val = RPLY_RECEIV | AUX_ERR;
	writel(val, edp->regs + DP_INT_STA);

	rk32_edp_reset_aux(edp);

	/* Disable AUX transaction H/W retry */
	/*val = AUX_BIT_PERIOD_EXPECTED_DELAY(3) | AUX_HW_RETRY_COUNT_SEL(0)|
		AUX_HW_RETRY_INTERVAL_600_MICROSECONDS;
	writel(val, edp->regs + AUX_HW_RETRY_CTL) ;*/

	/* Receive AUX Channel DEFER commands equal to DEFFER_COUNT*64 */
	val = DEFER_CTRL_EN | DEFER_COUNT(1);
	writel(val, edp->regs + AUX_CH_DEFER_CTL);

	/* Enable AUX channel module */
	val = readl(edp->regs + FUNC_EN_2);
	val &= ~AUX_FUNC_EN_N;
	writel(val, edp->regs + FUNC_EN_2);
}

int rk32_edp_get_plug_in_status(struct rk32_edp *edp)
{
	u32 val;

	val = readl(edp->regs + SYS_CTL_3);
	if (val & HPD_STATUS)
		return 0;

	return -EINVAL;
}

void rk32_edp_enable_sw_function(struct rk32_edp *edp)
{
	u32 val;
	val = readl(edp->regs + FUNC_EN_1);
	val &= ~SW_FUNC_EN_N;
	writel(val, edp->regs + FUNC_EN_1);
}

int rk32_edp_start_aux_transaction(struct rk32_edp *edp)
{
	int val;
	int retval = 0;
	int timeout_loop = 0;
	int aux_timeout = 0;

	/* Enable AUX CH operation */
	val = readl(edp->regs + AUX_CH_CTL_2);
	val |= AUX_EN;
	writel(val, edp->regs + AUX_CH_CTL_2);

	/* Is AUX CH operation enabled? */
	val = readl(edp->regs + AUX_CH_CTL_2);
	while (val & AUX_EN) {
		aux_timeout++;
		if ((DP_TIMEOUT_LOOP_CNT * 10) < aux_timeout) {
			dev_err(edp->dev, "AUX CH enable timeout!\n");
			return -ETIMEDOUT;
		}
		val = readl(edp->regs + AUX_CH_CTL_2);
		udelay(100);
	}

	/* Is AUX CH command redply received? */
	val = readl(edp->regs + DP_INT_STA);
	while (!(val & RPLY_RECEIV)) {
		timeout_loop++;
		if (DP_TIMEOUT_LOOP_CNT < timeout_loop) {
			dev_err(edp->dev, "AUX CH command redply failed!\n");
			return -ETIMEDOUT;
		}
		val = readl(edp->regs + DP_INT_STA);
		udelay(10);
	}

	/* Clear interrupt source for AUX CH command redply */
	writel(RPLY_RECEIV, edp->regs + DP_INT_STA);

	/* Clear interrupt source for AUX CH access error */
	val = readl(edp->regs + DP_INT_STA);
	if (val & AUX_ERR) {
		writel(AUX_ERR, edp->regs + DP_INT_STA);
		return -EREMOTEIO;
	}

	/* Check AUX CH error access status */
	val = readl(edp->regs + AUX_CH_STA);
	if ((val & AUX_STATUS_MASK) != 0) {
		dev_err(edp->dev, "AUX CH error happens: %d\n\n",
			val & AUX_STATUS_MASK);
		return -EREMOTEIO;
	}

	return retval;
}

int rk32_edp_write_byte_to_dpcd(struct rk32_edp *edp,
				unsigned int val_addr,
				unsigned char data)
{
	u32 val;
	int i;
	int retval;

	for (i = 0; i < 3; i++) {
		/* Clear AUX CH data buffer */
		val = BUF_CLR;
		writel(val, edp->regs + BUFFER_DATA_CTL);

		/* Select DPCD device address */
		val = AUX_ADDR_7_0(val_addr);
		writel(val, edp->regs + DP_AUX_ADDR_7_0);
		val = AUX_ADDR_15_8(val_addr);
		writel(val, edp->regs + DP_AUX_ADDR_15_8);
		val = AUX_ADDR_19_16(val_addr);
		writel(val, edp->regs + DP_AUX_ADDR_19_16);

		/* Write data buffer */
		val = (unsigned int)data;
		writel(val, edp->regs + BUF_DATA_0);

		/*
		 * Set DisplayPort transaction and write 1 byte
		 * If bit 3 is 1, DisplayPort transaction.
		 * If Bit 3 is 0, I2C transaction.
		 */
		val = AUX_TX_COMM_DP_TRANSACTION | AUX_TX_COMM_WRITE;
		writel(val, edp->regs + AUX_CH_CTL_1);

		/* Start AUX transaction */
		retval = rk32_edp_start_aux_transaction(edp);
		if (retval == 0)
			break;
		else
			dev_dbg(edp->dev, "Aux Transaction fail!\n");
	}

	return retval;
}

int rk32_edp_read_byte_from_dpcd(struct rk32_edp *edp,
				unsigned int val_addr,
				unsigned char *data)
{
	u32 val;
	int i;
	int retval;

	for (i = 0; i < 10; i++) {
		/* Clear AUX CH data buffer */
		val = BUF_CLR;
		writel(val, edp->regs + BUFFER_DATA_CTL);

		/* Select DPCD device address */
		val = AUX_ADDR_7_0(val_addr);
		writel(val, edp->regs + DP_AUX_ADDR_7_0);
		val = AUX_ADDR_15_8(val_addr);
		writel(val, edp->regs + DP_AUX_ADDR_15_8);
		val = AUX_ADDR_19_16(val_addr);
		writel(val, edp->regs + DP_AUX_ADDR_19_16);

		/*
		 * Set DisplayPort transaction and read 1 byte
		 * If bit 3 is 1, DisplayPort transaction.
		 * If Bit 3 is 0, I2C transaction.
		 */
		val = AUX_TX_COMM_DP_TRANSACTION | AUX_TX_COMM_READ;
		writel(val, edp->regs + AUX_CH_CTL_1);

		/* Start AUX transaction */
		retval = rk32_edp_start_aux_transaction(edp);
		if (retval == 0)
			break;
		else
			dev_dbg(edp->dev, "Aux Transaction fail!\n");
	}

	/* Read data buffer */
	val = readl(edp->regs + BUF_DATA_0);
	*data = (unsigned char)(val & 0xff);

	return retval;
}

int rk32_edp_write_bytes_to_dpcd(struct rk32_edp *edp,
				unsigned int val_addr,
				unsigned int count,
				unsigned char data[])
{
	u32 val;
	unsigned int start_offset;
	unsigned int cur_data_count;
	unsigned int cur_data_idx;
	int i;
	int retval = 0;

	/* Clear AUX CH data buffer */
	val = BUF_CLR;
	writel(val, edp->regs + BUFFER_DATA_CTL);

	start_offset = 0;
	while (start_offset < count) {
		/* Buffer size of AUX CH is 16 * 4bytes */
		if ((count - start_offset) > 16)
			cur_data_count = 16;
		else
			cur_data_count = count - start_offset;

		for (i = 0; i < 10; i++) {
			/* Select DPCD device address */
			val = AUX_ADDR_7_0(val_addr + start_offset);
			writel(val, edp->regs + DP_AUX_ADDR_7_0);
			val = AUX_ADDR_15_8(val_addr + start_offset);
			writel(val, edp->regs + DP_AUX_ADDR_15_8);
			val = AUX_ADDR_19_16(val_addr + start_offset);
			writel(val, edp->regs + DP_AUX_ADDR_19_16);

			for (cur_data_idx = 0; cur_data_idx < cur_data_count;
			     cur_data_idx++) {
				val = data[start_offset + cur_data_idx];
				writel(val, edp->regs + BUF_DATA_0
							  + 4 * cur_data_idx);
			}

			/*
			 * Set DisplayPort transaction and write
			 * If bit 3 is 1, DisplayPort transaction.
			 * If Bit 3 is 0, I2C transaction.
			 */
			val = AUX_LENGTH(cur_data_count) |
				AUX_TX_COMM_DP_TRANSACTION | AUX_TX_COMM_WRITE;
			writel(val, edp->regs + AUX_CH_CTL_1);

			/* Start AUX transaction */
			retval = rk32_edp_start_aux_transaction(edp);
			if (retval == 0)
				break;
			else
				dev_dbg(edp->dev, "Aux Transaction fail!\n");
		}

		start_offset += cur_data_count;
	}

	return retval;
}

int rk32_edp_read_bytes_from_dpcd(struct rk32_edp *edp,
				unsigned int val_addr,
				unsigned int count,
				unsigned char data[])
{
	u32 val;
	unsigned int start_offset;
	unsigned int cur_data_count;
	unsigned int cur_data_idx;
	int i;
	int retval = 0;

	/* Clear AUX CH data buffer */
	val = BUF_CLR;
	writel(val, edp->regs + BUFFER_DATA_CTL);

	start_offset = 0;
	while (start_offset < count) {
		/* Buffer size of AUX CH is 16 * 4bytes */
		if ((count - start_offset) > 16)
			cur_data_count = 16;
		else
			cur_data_count = count - start_offset;

		/* AUX CH Request Transaction process */
		for (i = 0; i < 10; i++) {
			/* Select DPCD device address */
			val = AUX_ADDR_7_0(val_addr + start_offset);
			writel(val, edp->regs + DP_AUX_ADDR_7_0);
			val = AUX_ADDR_15_8(val_addr + start_offset);
			writel(val, edp->regs + DP_AUX_ADDR_15_8);
			val = AUX_ADDR_19_16(val_addr + start_offset);
			writel(val, edp->regs + DP_AUX_ADDR_19_16);

			/*
			 * Set DisplayPort transaction and read
			 * If bit 3 is 1, DisplayPort transaction.
			 * If Bit 3 is 0, I2C transaction.
			 */
			val = AUX_LENGTH(cur_data_count) |
				AUX_TX_COMM_DP_TRANSACTION | AUX_TX_COMM_READ;
			writel(val, edp->regs + AUX_CH_CTL_1);

			/* Start AUX transaction */
			retval = rk32_edp_start_aux_transaction(edp);
			if (retval == 0)
				break;
			else
				dev_dbg(edp->dev, "Aux Transaction fail!\n");
		}

		for (cur_data_idx = 0; cur_data_idx < cur_data_count;
		    cur_data_idx++) {
			val = readl(edp->regs + BUF_DATA_0
						 + 4 * cur_data_idx);
			data[start_offset + cur_data_idx] =
				(unsigned char)val;
		}

		start_offset += cur_data_count;
	}

	return retval;
}

int rk32_edp_select_i2c_device(struct rk32_edp *edp,
				unsigned int device_addr,
				unsigned int val_addr)
{
	u32 val;
	int retval;

	/* Set EDID device address */
	val = device_addr;
	writel(val, edp->regs + DP_AUX_ADDR_7_0);
	writel(0x0, edp->regs + DP_AUX_ADDR_15_8);
	writel(0x0, edp->regs + DP_AUX_ADDR_19_16);

	/* Set offset from base address of EDID device */
	writel(val_addr, edp->regs + BUF_DATA_0);

	/*
	 * Set I2C transaction and write address
	 * If bit 3 is 1, DisplayPort transaction.
	 * If Bit 3 is 0, I2C transaction.
	 */
	val = AUX_TX_COMM_I2C_TRANSACTION | AUX_TX_COMM_MOT |
		AUX_TX_COMM_WRITE;
	writel(val, edp->regs + AUX_CH_CTL_1);

	/* Start AUX transaction */
	retval = rk32_edp_start_aux_transaction(edp);
	if (retval != 0)
		dev_dbg(edp->dev, "Aux Transaction fail!\n");

	return retval;
}

int rk32_edp_read_byte_from_i2c(struct rk32_edp *edp,
				unsigned int device_addr,
				unsigned int val_addr,
				unsigned int *data)
{
	u32 val;
	int i;
	int retval;

	for (i = 0; i < 10; i++) {
		/* Clear AUX CH data buffer */
		val = BUF_CLR;
		writel(val, edp->regs + BUFFER_DATA_CTL);

		/* Select EDID device */
		retval = rk32_edp_select_i2c_device(edp, device_addr, val_addr);
		if (retval != 0) {
			dev_err(edp->dev, "Select EDID device fail!\n");
			continue;
		}

		/*
		 * Set I2C transaction and read data
		 * If bit 3 is 1, DisplayPort transaction.
		 * If Bit 3 is 0, I2C transaction.
		 */
		val = AUX_TX_COMM_I2C_TRANSACTION | AUX_TX_COMM_READ;
		writel(val, edp->regs + AUX_CH_CTL_1);

		/* Start AUX transaction */
		retval = rk32_edp_start_aux_transaction(edp);
		if (retval == 0)
			break;
		else
			dev_dbg(edp->dev, "Aux Transaction fail!\n");
	}

	/* Read data */
	if (retval == 0)
		*data = readl(edp->regs + BUF_DATA_0);

	return retval;
}

int rk32_edp_read_bytes_from_i2c(struct rk32_edp *edp,
				unsigned int device_addr,
				unsigned int val_addr,
				unsigned int count,
				unsigned char edid[])
{
	u32 val;
	unsigned int i, j;
	unsigned int cur_data_idx;
	unsigned int defer = 0;
	int retval = 0;

	for (i = 0; i < count; i += 16) {
		for (j = 0; j < 100; j++) {
			/* Clear AUX CH data buffer */
			val = BUF_CLR;
			writel(val, edp->regs + BUFFER_DATA_CTL);

			/* Set normal AUX CH command */
			val = readl(edp->regs + AUX_CH_CTL_2);
			val &= ~ADDR_ONLY;
			writel(val, edp->regs + AUX_CH_CTL_2);

			/*
			 * If Rx sends defer, Tx sends only reads
			 * request without sending addres
			 */
			if (!defer)
				retval = rk32_edp_select_i2c_device(edp,
						device_addr, val_addr + i);
			else
				defer = 0;

			/*
			 * Set I2C transaction and write data
			 * If bit 3 is 1, DisplayPort transaction.
			 * If Bit 3 is 0, I2C transaction.
			 */
			val = AUX_LENGTH(16) | AUX_TX_COMM_I2C_TRANSACTION |
				AUX_TX_COMM_READ;
			writel(val, edp->regs + AUX_CH_CTL_1);

			/* Start AUX transaction */
			retval = rk32_edp_start_aux_transaction(edp);
			if (retval == 0)
				break;
			else
				dev_dbg(edp->dev, "Aux Transaction fail!\n");

			/* Check if Rx sends defer */
			val = readl(edp->regs + AUX_RX_COMM);
			if (val == AUX_RX_COMM_AUX_DEFER ||
				val == AUX_RX_COMM_I2C_DEFER) {
				dev_err(edp->dev, "Defer: %d\n\n", val);
				defer = 1;
			}
		}

		for (cur_data_idx = 0; cur_data_idx < 16; cur_data_idx++) {
			val = readl(edp->regs + BUF_DATA_0 + 4 * cur_data_idx);
			edid[i + cur_data_idx] = (unsigned char)val;
		}
	}

	return retval;
}

void rk32_edp_set_link_bandwidth(struct rk32_edp *edp, u32 bwtype)
{
	u32 val;

	val = bwtype;
	if ((bwtype == LINK_RATE_2_70GBPS) || (bwtype == LINK_RATE_1_62GBPS))
		writel(val, edp->regs + LINK_BW_SET);
}

void rk32_edp_get_link_bandwidth(struct rk32_edp *edp, u32 *bwtype)
{
	u32 val;

	val = readl(edp->regs + LINK_BW_SET);
	*bwtype = val;
}

void rk32_edp_hw_link_training_en(struct rk32_edp *edp)
{
	u32 val;
	val = HW_LT_EN;
	writel(val, edp->regs + HW_LT_CTL);
}

int rk32_edp_wait_hw_lt_done(struct rk32_edp *edp)
{
	u32 val;
#if 0
	val = readl(edp->regs + HW_LT_CTL);
	return val&0x01;
#else
	val = readl(edp->regs + DP_INT_STA);
	if (val&HW_LT_DONE) {
		writel(val, edp->regs + DP_INT_STA);
		return 0;
	} else {
		return 1;
	}
#endif
}

int rk32_edp_get_hw_lt_status(struct rk32_edp *edp)
{
	u32 val;
	val = readl(edp->regs + HW_LT_CTL);
	return (val & HW_LT_ERR_CODE_MASK) >> 4;
}
void rk32_edp_set_lane_count(struct rk32_edp *edp, u32 count)
{
	u32 val;

	val = count;
	writel(val, edp->regs + LANE_CNT_SET);
}

void rk32_edp_get_lane_count(struct rk32_edp *edp, u32 *count)
{
	u32 val;

	val = readl(edp->regs + LANE_CNT_SET);
	*count = val;
}

void rk32_edp_enable_enhanced_mode(struct rk32_edp *edp, bool enable)
{
	u32 val;

	if (enable) {
		val = readl(edp->regs + SYS_CTL_4);
		val |= ENHANCED;
		writel(val, edp->regs + SYS_CTL_4);
	} else {
		val = readl(edp->regs + SYS_CTL_4);
		val &= ~ENHANCED;
		writel(val, edp->regs + SYS_CTL_4);
	}
}

void rk32_edp_set_training_pattern(struct rk32_edp *edp,
				 enum pattern_set pattern)
{
	u32 val;

	switch (pattern) {
	case PRBS7:
		val = SCRAMBLING_ENABLE | LINK_QUAL_PATTERN_SET_PRBS7;
		writel(val, edp->regs + TRAINING_PTN_SET);
		break;
	case D10_2:
		val = SCRAMBLING_ENABLE | LINK_QUAL_PATTERN_SET_D10_2;
		writel(val, edp->regs + TRAINING_PTN_SET);
		break;
	case TRAINING_PTN1:
		val = SCRAMBLING_DISABLE | SW_TRAINING_PATTERN_SET_PTN1;
		writel(val, edp->regs + TRAINING_PTN_SET);
		break;
	case TRAINING_PTN2:
		val = SCRAMBLING_DISABLE | SW_TRAINING_PATTERN_SET_PTN2;
		writel(val, edp->regs + TRAINING_PTN_SET);
		break;
	case DP_NONE:
		val = SCRAMBLING_ENABLE |
			LINK_QUAL_PATTERN_SET_DISABLE |
			SW_TRAINING_PATTERN_SET_DISABLE;
		writel(val, edp->regs + TRAINING_PTN_SET);
		break;
	default:
		break;
	}
}

void rk32_edp_set_lane0_pre_emphasis(struct rk32_edp *edp, u32 level)
{
	u32 val;

	val = level << PRE_EMPHASIS_SET_SHIFT;
	writel(val, edp->regs + LN0_LINK_TRAINING_CTL);
}

void rk32_edp_set_lane1_pre_emphasis(struct rk32_edp *edp, u32 level)
{
	u32 val;

	val = level << PRE_EMPHASIS_SET_SHIFT;
	writel(val, edp->regs + LN1_LINK_TRAINING_CTL);
}

void rk32_edp_set_lane2_pre_emphasis(struct rk32_edp *edp, u32 level)
{
	u32 val;

	val = level << PRE_EMPHASIS_SET_SHIFT;
	writel(val, edp->regs + LN2_LINK_TRAINING_CTL);
}

void rk32_edp_set_lane3_pre_emphasis(struct rk32_edp *edp, u32 level)
{
	u32 val;

	val = level << PRE_EMPHASIS_SET_SHIFT;
	writel(val, edp->regs + LN3_LINK_TRAINING_CTL);
}

void rk32_edp_set_lane0_link_training(struct rk32_edp *edp,
					u32 training_lane)
{
	u32 val;

	val = training_lane;
	writel(val, edp->regs + LN0_LINK_TRAINING_CTL);
}

void rk32_edp_set_lane1_link_training(struct rk32_edp *edp,
					u32 training_lane)
{
	u32 val;

	val = training_lane;
	writel(val, edp->regs + LN1_LINK_TRAINING_CTL);
}

void rk32_edp_set_lane2_link_training(struct rk32_edp *edp,
					u32 training_lane)
{
	u32 val;

	val = training_lane;
	writel(val, edp->regs + LN2_LINK_TRAINING_CTL);
}

void rk32_edp_set_lane3_link_training(struct rk32_edp *edp,
					u32 training_lane)
{
	u32 val;

	val = training_lane;
	writel(val, edp->regs + LN3_LINK_TRAINING_CTL);
}

u32 rk32_edp_get_lane0_link_training(struct rk32_edp *edp)
{
	u32 val;

	val = readl(edp->regs + LN0_LINK_TRAINING_CTL);
	return val;
}

u32 rk32_edp_get_lane1_link_training(struct rk32_edp *edp)
{
	u32 val;

	val = readl(edp->regs + LN1_LINK_TRAINING_CTL);
	return val;
}

u32 rk32_edp_get_lane2_link_training(struct rk32_edp *edp)
{
	u32 val;

	val = readl(edp->regs + LN2_LINK_TRAINING_CTL);
	return val;
}

u32 rk32_edp_get_lane3_link_training(struct rk32_edp *edp)
{
	u32 val;

	val = readl(edp->regs + LN3_LINK_TRAINING_CTL);
	return val;
}

void rk32_edp_reset_macro(struct rk32_edp *edp)
{
	/*u32 val;

	val = readl(edp->regs + PHY_TEST);
	val |= MACRO_RST;
	writel(val, edp->regs + PHY_TEST);


	udelay(10);

	val &= ~MACRO_RST;
	writel(val, edp->regs + PHY_TEST);*/
}

int rk32_edp_init_video(struct rk32_edp *edp)
{
	u32 val;

	val = VSYNC_DET | VID_FORMAT_CHG | VID_CLK_CHG;
	writel(val, edp->regs + COMMON_INT_STA_1);

	val = 0x0;
	writel(val, edp->regs + SYS_CTL_1);

	val = CHA_CRI(4) | CHA_CTRL;
	writel(val, edp->regs + SYS_CTL_2);

	/*val = 0x0;
	writel(val, edp->regs + SYS_CTL_3);*/

	val = VID_HRES_TH(2) | VID_VRES_TH(0);
	writel(val, edp->regs + VIDEO_CTL_8);

	return 0;
}

void rk32_edp_set_video_color_format(struct rk32_edp *edp,
			u32 color_dedpth,
			u32 color_space,
			u32 dynamic_range,
			u32 coeff)
{
	u32 val;

	/* Configure the input color dedpth, color space, dynamic range */
	val = (dynamic_range << IN_D_RANGE_SHIFT) |
		(color_dedpth << IN_BPC_SHIFT) |
		(color_space << IN_COLOR_F_SHIFT);
	writel(val, edp->regs + VIDEO_CTL_2);

	/* Set Input Color YCbCr Coefficients to ITU601 or ITU709 */
	val = readl(edp->regs + VIDEO_CTL_3);
	val &= ~IN_YC_COEFFI_MASK;
	if (coeff)
		val |= IN_YC_COEFFI_ITU709;
	else
		val |= IN_YC_COEFFI_ITU601;
	writel(val, edp->regs + VIDEO_CTL_3);
}

int rk32_edp_is_slave_video_stream_clock_on(struct rk32_edp *edp)
{
	u32 val;

	val = readl(edp->regs + SYS_CTL_1);
	writel(val, edp->regs + SYS_CTL_1);

	val = readl(edp->regs + SYS_CTL_1);

	if (!(val & DET_STA)) {
		dev_dbg(edp->dev, "Input stream clock not detected.\n");
		return -EINVAL;
	}

	val = readl(edp->regs + SYS_CTL_2);
	writel(val, edp->regs + SYS_CTL_2);

	val = readl(edp->regs + SYS_CTL_2);
	if (val & CHA_STA) {
		dev_dbg(edp->dev, "Input stream clk is changing\n");
		return -EINVAL;
	}

	return 0;
}


void rk32_edp_set_video_cr_mn(struct rk32_edp *edp,
		enum clock_recovery_m_value_type type,
		u32 m_value,
		u32 n_value)
{
	u32 val;

	if (type == REGISTER_M) {
		val = readl(edp->regs + SYS_CTL_4);
		val |= FIX_M_VID;
		writel(val, edp->regs + SYS_CTL_4);
		val = m_value & 0xff;
		writel(val, edp->regs + M_VID_0);
		val = (m_value >> 8) & 0xff;
		writel(val, edp->regs + M_VID_1);
		val = (m_value >> 16) & 0xff;
		writel(val, edp->regs + M_VID_2);

		val = n_value & 0xff;
		writel(val, edp->regs + N_VID_0);
		val = (n_value >> 8) & 0xff;
		writel(val, edp->regs + N_VID_1);
		val = (n_value >> 16) & 0xff;
		writel(val, edp->regs + N_VID_2);
	} else  {
		val = readl(edp->regs + SYS_CTL_4);
		val &= ~FIX_M_VID;
		writel(val, edp->regs + SYS_CTL_4);

		writel(0x00, edp->regs + N_VID_0);
		writel(0x80, edp->regs + N_VID_1);
		writel(0x00, edp->regs + N_VID_2);
	}
}

void rk32_edp_set_video_timing_mode(struct rk32_edp *edp, u32 type)
{
	u32 val;

	if (type == VIDEO_TIMING_FROM_CAPTURE) {
		val = readl(edp->regs + VIDEO_CTL_10);
		val &= ~F_SEL;
		writel(val, edp->regs + VIDEO_CTL_10);
	} else {
		val = readl(edp->regs + VIDEO_CTL_10);
		val |= F_SEL;
		writel(val, edp->regs + VIDEO_CTL_10);
	}
}

int rk32_edp_bist_cfg(struct rk32_edp *edp)
{
	struct video_info *video_info = &edp->video_info;
	struct rk_screen *screen = &edp->screen;
	u16 x_total, y_total, x_act;
	u32 val;
	x_total = screen->mode.left_margin + screen->mode.right_margin +
			screen->mode.xres + screen->mode.hsync_len;
	y_total = screen->mode.upper_margin + screen->mode.lower_margin +
			screen->mode.yres + screen->mode.vsync_len;
	x_act = screen->mode.xres;

	rk32_edp_set_video_cr_mn(edp, CALCULATED_M, 0, 0);
	rk32_edp_set_video_color_format(edp, video_info->color_depth,
					video_info->color_space,
					video_info->dynamic_range,
					video_info->ycbcr_coeff);

	val = y_total & 0xff;
	writel(val, edp->regs + TOTAL_LINE_CFG_L);
	val = (y_total >> 8);
	writel(val, edp->regs + TOTAL_LINE_CFG_H);
	val = (screen->mode.yres & 0xff);
	writel(val, edp->regs + ATV_LINE_CFG_L);
	val = (screen->mode.yres >> 8);
	writel(val, edp->regs + ATV_LINE_CFG_H);
	val = screen->mode.lower_margin;
	writel(val, edp->regs + VF_PORCH_REG);
	val = screen->mode.vsync_len;
	writel(val, edp->regs + VSYNC_CFG_REG);
	val = screen->mode.upper_margin;
	writel(val, edp->regs + VB_PORCH_REG);
	val = x_total & 0xff;
	writel(val, edp->regs + TOTAL_PIXELL_REG);
	val = x_total >> 8;
	writel(val, edp->regs + TOTAL_PIXELH_REG);
	val = (x_act & 0xff);
	writel(val, edp->regs + ATV_PIXELL_REG);
	val = (x_act >> 8);
	writel(val, edp->regs + ATV_PIXELH_REG);
	val = screen->mode.right_margin & 0xff;
	writel(val, edp->regs + HF_PORCHL_REG);
	val = screen->mode.right_margin >> 8;
	writel(val, edp->regs + HF_PORCHH_REG);
	val = screen->mode.hsync_len & 0xff;
	writel(val, edp->regs + HSYNC_CFGL_REG);
	val = screen->mode.hsync_len >> 8;
	writel(val, edp->regs + HSYNC_CFGH_REG);
	val = screen->mode.left_margin & 0xff;
	writel(val, edp->regs + HB_PORCHL_REG);
	val = screen->mode.left_margin  >> 8;
	writel(val, edp->regs + HB_PORCHH_REG);

	val = BIST_EN | BIST_WH_64 | BIST_TYPE_COLR_BAR;
	writel(val, edp->regs + VIDEO_CTL_4);

	val = readl(edp->regs + VIDEO_CTL_10);
	val &= ~F_SEL;
	writel(val, edp->regs + VIDEO_CTL_10);
	return 0;

}

void rk32_edp_enable_video_master(struct rk32_edp *edp, bool enable)
{
	/*u32 val;

	if (enable) {
		val = readl(edp->regs + SOC_GENERAL_CTL);
		val &= ~VIDEO_MODE_MASK;
		val |= VIDEO_MASTER_MODE_EN | VIDEO_MODE_MASTER_MODE;
		writel(val, edp->regs + SOC_GENERAL_CTL);
	} else {
		val = readl(edp->regs + SOC_GENERAL_CTL);
		val &= ~VIDEO_MODE_MASK;
		val |= VIDEO_MODE_SLAVE_MODE;
		writel(val, edp->regs + SOC_GENERAL_CTL);
	}*/
}

void rk32_edp_start_video(struct rk32_edp *edp)
{
	u32 val;

	val = readl(edp->regs + VIDEO_CTL_1);
	val |= VIDEO_EN;
	writel(val, edp->regs + VIDEO_CTL_1);
}

int rk32_edp_is_video_stream_on(struct rk32_edp *edp)
{
	u32 val;

	val = readl(edp->regs + SYS_CTL_3);
	writel(val, edp->regs + SYS_CTL_3);

	val = readl(edp->regs + SYS_CTL_3);
	if (!(val & STRM_VALID)) {
		dev_dbg(edp->dev, "Input video stream is not detected.\n");
		return -EINVAL;
	}

	return 0;
}

void rk32_edp_config_video_slave_mode(struct rk32_edp *edp,
			struct video_info *video_info)
{
	u32 val;

	val = readl(edp->regs + FUNC_EN_1);
	val &= ~(VID_FIFO_FUNC_EN_N | VID_CAP_FUNC_EN_N);
	writel(val, edp->regs + FUNC_EN_1);

	val = readl(edp->regs + VIDEO_CTL_10);
	val &= ~INTERACE_SCAN_CFG;
	val |= (video_info->interlaced << 2);
	writel(val, edp->regs + VIDEO_CTL_10);

	val = readl(edp->regs + VIDEO_CTL_10);
	val &= ~VSYNC_POLARITY_CFG;
	val |= (video_info->v_sync_polarity << 1);
	writel(val, edp->regs + VIDEO_CTL_10);

	val = readl(edp->regs + VIDEO_CTL_10);
	val &= ~HSYNC_POLARITY_CFG;
	val |= (video_info->h_sync_polarity << 0);
	writel(val, edp->regs + VIDEO_CTL_10);

	/*val = AUDIO_MODE_SPDIF_MODE | VIDEO_MODE_SLAVE_MODE;
	writel(val, edp->regs + SOC_GENERAL_CTL);*/
}

void rk32_edp_enable_scrambling(struct rk32_edp *edp)
{
	u32 val;

	val = readl(edp->regs + TRAINING_PTN_SET);
	val &= ~SCRAMBLING_DISABLE;
	writel(val, edp->regs + TRAINING_PTN_SET);
}

void rk32_edp_disable_scrambling(struct rk32_edp *edp)
{
	u32 val;

	val = readl(edp->regs + TRAINING_PTN_SET);
	val |= SCRAMBLING_DISABLE;
	writel(val, edp->regs + TRAINING_PTN_SET);
}

enum dp_irq_type rk32_edp_get_irq_type(struct rk32_edp *edp)
{
	u32 val;

	/* Parse hotplug interrupt status register */
	val = readl(edp->regs + COMMON_INT_STA_4);
	if (val & PLUG)
		return DP_IRQ_TYPE_HP_CABLE_IN;

	if (val & HPD_LOST)
		return DP_IRQ_TYPE_HP_CABLE_OUT;

	if (val & HOTPLUG_CHG)
		return DP_IRQ_TYPE_HP_CHANGE;

	return DP_IRQ_TYPE_UNKNOWN;
}

void rk32_edp_clear_hotplug_interrupts(struct rk32_edp *edp)
{
	u32 val;

	val = HOTPLUG_CHG | HPD_LOST | PLUG;
	writel(val, edp->regs + COMMON_INT_STA_4);

	val = INT_HPD;
	writel(val, edp->regs + DP_INT_STA);
}

/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Kalle Valo <kalle.valo@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/gpio.h>

#include "reg.h"
#include "boot.h"
#include "spi.h"
#include "event.h"

static void wl12xx_boot_enable_interrupts(struct wl12xx *wl)
{
	enable_irq(wl->irq);
}

void wl12xx_boot_target_enable_interrupts(struct wl12xx *wl)
{
	wl12xx_reg_write32(wl, ACX_REG_INTERRUPT_MASK, ~(wl->intr_mask));
	wl12xx_reg_write32(wl, HI_CFG, HI_CFG_DEF_VAL);
}

int wl12xx_boot_soft_reset(struct wl12xx *wl)
{
	unsigned long timeout;
	u32 boot_data;

	/* perform soft reset */
	wl12xx_reg_write32(wl, ACX_REG_SLV_SOFT_RESET, ACX_SLV_SOFT_RESET_BIT);

	/* SOFT_RESET is self clearing */
	timeout = jiffies + usecs_to_jiffies(SOFT_RESET_MAX_TIME);
	while (1) {
		boot_data = wl12xx_reg_read32(wl, ACX_REG_SLV_SOFT_RESET);
		wl12xx_debug(DEBUG_BOOT, "soft reset bootdata 0x%x", boot_data);
		if ((boot_data & ACX_SLV_SOFT_RESET_BIT) == 0)
			break;

		if (time_after(jiffies, timeout)) {
			/* 1.2 check pWhalBus->uSelfClearTime if the
			 * timeout was reached */
			wl12xx_error("soft reset timeout");
			return -1;
		}

		udelay(SOFT_RESET_STALL_TIME);
	}

	/* disable Rx/Tx */
	wl12xx_reg_write32(wl, ENABLE, 0x0);

	/* disable auto calibration on start*/
	wl12xx_reg_write32(wl, SPARE_A2, 0xffff);

	return 0;
}

int wl12xx_boot_init_seq(struct wl12xx *wl)
{
	u32 scr_pad6, init_data, tmp, elp_cmd, ref_freq;

	/*
	 * col #1: INTEGER_DIVIDER
	 * col #2: FRACTIONAL_DIVIDER
	 * col #3: ATTN_BB
	 * col #4: ALPHA_BB
	 * col #5: STOP_TIME_BB
	 * col #6: BB_PLL_LOOP_FILTER
	 */
	static const u32 LUT[REF_FREQ_NUM][LUT_PARAM_NUM] = {

		{   83, 87381,  0xB, 5, 0xF00,  3}, /* REF_FREQ_19_2*/
		{   61, 141154, 0xB, 5, 0x1450, 2}, /* REF_FREQ_26_0*/
		{   41, 174763, 0xC, 6, 0x2D00, 1}, /* REF_FREQ_38_4*/
		{   40, 0,      0xC, 6, 0x2EE0, 1}, /* REF_FREQ_40_0*/
		{   47, 162280, 0xC, 6, 0x2760, 1}  /* REF_FREQ_33_6        */
	};

	/* read NVS params */
	scr_pad6 = wl12xx_reg_read32(wl, SCR_PAD6);
	wl12xx_debug(DEBUG_BOOT, "scr_pad6 0x%x", scr_pad6);

	/* read ELP_CMD */
	elp_cmd = wl12xx_reg_read32(wl, ELP_CMD);
	wl12xx_debug(DEBUG_BOOT, "elp_cmd 0x%x", elp_cmd);

	/* set the BB calibration time to be 300 usec (PLL_CAL_TIME) */
	ref_freq = scr_pad6 & 0x000000FF;
	wl12xx_debug(DEBUG_BOOT, "ref_freq 0x%x", ref_freq);

	wl12xx_reg_write32(wl, PLL_CAL_TIME, 0x9);

	/*
	 * PG 1.2: set the clock buffer time to be 210 usec (CLK_BUF_TIME)
	 */
	wl12xx_reg_write32(wl, CLK_BUF_TIME, 0x6);

	/*
	 * set the clock detect feature to work in the restart wu procedure
	 * (ELP_CFG_MODE[14]) and Select the clock source type
	 * (ELP_CFG_MODE[13:12])
	 */
	tmp = ((scr_pad6 & 0x0000FF00) << 4) | 0x00004000;
	wl12xx_reg_write32(wl, ELP_CFG_MODE, tmp);

	/* PG 1.2: enable the BB PLL fix. Enable the PLL_LIMP_CLK_EN_CMD */
	elp_cmd |= 0x00000040;
	wl12xx_reg_write32(wl, ELP_CMD, elp_cmd);

	/* PG 1.2: Set the BB PLL stable time to be 1000usec
	 * (PLL_STABLE_TIME) */
	wl12xx_reg_write32(wl, CFG_PLL_SYNC_CNT, 0x20);

	/* PG 1.2: read clock request time */
	init_data = wl12xx_reg_read32(wl, CLK_REQ_TIME);

	/*
	 * PG 1.2: set the clock request time to be ref_clk_settling_time -
	 * 1ms = 4ms
	 */
	if (init_data > 0x21)
		tmp = init_data - 0x21;
	else
		tmp = 0;
	wl12xx_reg_write32(wl, CLK_REQ_TIME, tmp);

	/* set BB PLL configurations in RF AFE */
	wl12xx_reg_write32(wl, 0x003058cc, 0x4B5);

	/* set RF_AFE_REG_5 */
	wl12xx_reg_write32(wl, 0x003058d4, 0x50);

	/* set RF_AFE_CTRL_REG_2 */
	wl12xx_reg_write32(wl, 0x00305948, 0x11c001);

	/*
	 * change RF PLL and BB PLL divider for VCO clock and adjust VCO
	 * bais current(RF_AFE_REG_13)
	 */
	wl12xx_reg_write32(wl, 0x003058f4, 0x1e);

	/* set BB PLL configurations */
	tmp = LUT[ref_freq][LUT_PARAM_INTEGER_DIVIDER] | 0x00017000;
	wl12xx_reg_write32(wl, 0x00305840, tmp);

	/* set fractional divider according to Appendix C-BB PLL
	 * Calculations
	 */
	tmp = LUT[ref_freq][LUT_PARAM_FRACTIONAL_DIVIDER];
	wl12xx_reg_write32(wl, 0x00305844, tmp);

	/* set the initial data for the sigma delta */
	wl12xx_reg_write32(wl, 0x00305848, 0x3039);

	/*
	 * set the accumulator attenuation value, calibration loop1
	 * (alpha), calibration loop2 (beta), calibration loop3 (gamma) and
	 * the VCO gain
	 */
	tmp = (LUT[ref_freq][LUT_PARAM_ATTN_BB] << 16) |
		(LUT[ref_freq][LUT_PARAM_ALPHA_BB] << 12) | 0x1;
	wl12xx_reg_write32(wl, 0x00305854, tmp);

	/*
	 * set the calibration stop time after holdoff time expires and set
	 * settling time HOLD_OFF_TIME_BB
	 */
	tmp = LUT[ref_freq][LUT_PARAM_STOP_TIME_BB] | 0x000A0000;
	wl12xx_reg_write32(wl, 0x00305858, tmp);

	/*
	 * set BB PLL Loop filter capacitor3- BB_C3[2:0] and set BB PLL
	 * constant leakage current to linearize PFD to 0uA -
	 * BB_ILOOPF[7:3]
	 */
	tmp = LUT[ref_freq][LUT_PARAM_BB_PLL_LOOP_FILTER] | 0x00000030;
	wl12xx_reg_write32(wl, 0x003058f8, tmp);

	/*
	 * set regulator output voltage for n divider to
	 * 1.35-BB_REFDIV[1:0], set charge pump current- BB_CPGAIN[4:2],
	 * set BB PLL Loop filter capacitor2- BB_C2[7:5], set gain of BB
	 * PLL auto-call to normal mode- BB_CALGAIN_3DB[8]
	 */
	wl12xx_reg_write32(wl, 0x003058f0, 0x29);

	/* enable restart wakeup sequence (ELP_CMD[0]) */
	wl12xx_reg_write32(wl, ELP_CMD, elp_cmd | 0x1);

	/* restart sequence completed */
	udelay(2000);

	return 0;
}

int wl12xx_boot_run_firmware(struct wl12xx *wl)
{
	int loop, ret;
	u32 chip_id, interrupt;

	wl->chip.op_set_ecpu_ctrl(wl, ECPU_CONTROL_HALT);

	chip_id = wl12xx_reg_read32(wl, CHIP_ID_B);

	wl12xx_debug(DEBUG_BOOT, "chip id after firmware boot: 0x%x", chip_id);

	if (chip_id != wl->chip.id) {
		wl12xx_error("chip id doesn't match after firmware boot");
		return -EIO;
	}

	/* wait for init to complete */
	loop = 0;
	while (loop++ < INIT_LOOP) {
		udelay(INIT_LOOP_DELAY);
		interrupt = wl12xx_reg_read32(wl, ACX_REG_INTERRUPT_NO_CLEAR);

		if (interrupt == 0xffffffff) {
			wl12xx_error("error reading hardware complete "
				     "init indication");
			return -EIO;
		}
		/* check that ACX_INTR_INIT_COMPLETE is enabled */
		else if (interrupt & wl->chip.intr_init_complete) {
			wl12xx_reg_write32(wl, ACX_REG_INTERRUPT_ACK,
					   wl->chip.intr_init_complete);
			break;
		}
	}

	if (loop >= INIT_LOOP) {
		wl12xx_error("timeout waiting for the hardware to "
			     "complete initialization");
		return -EIO;
	}

	/* get hardware config command mail box */
	wl->cmd_box_addr = wl12xx_reg_read32(wl, REG_COMMAND_MAILBOX_PTR);

	/* get hardware config event mail box */
	wl->event_box_addr = wl12xx_reg_read32(wl, REG_EVENT_MAILBOX_PTR);

	/* set the working partition to its "running" mode offset */
	wl12xx_set_partition(wl,
			     wl->chip.p_table[PART_WORK].mem.start,
			     wl->chip.p_table[PART_WORK].mem.size,
			     wl->chip.p_table[PART_WORK].reg.start,
			     wl->chip.p_table[PART_WORK].reg.size);

	wl12xx_debug(DEBUG_MAILBOX, "cmd_box_addr 0x%x event_box_addr 0x%x",
		     wl->cmd_box_addr, wl->event_box_addr);

	/*
	 * in case of full asynchronous mode the firmware event must be
	 * ready to receive event from the command mailbox
	 */

	/* enable gpio interrupts */
	wl12xx_boot_enable_interrupts(wl);

	wl->chip.op_target_enable_interrupts(wl);

	/* unmask all mbox events  */
	wl->event_mask = 0xffffffff;

	ret = wl12xx_event_unmask(wl);
	if (ret < 0) {
		wl12xx_error("EVENT mask setting failed");
		return ret;
	}

	wl12xx_event_mbox_config(wl);

	/* firmware startup completed */
	return 0;
}

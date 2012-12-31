/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_ctrl.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Control interface for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include <mach/regs-mfc.h>

#include "mfc.h"
#include "mfc_mem.h"
#include "mfc_reg.h"
#include "mfc_log.h"
#include "mfc_cmd.h"
#include "mfc_dev.h"
#include "mfc_errno.h"
#include "mfc_pm.h"

#define MC_STATUS_TIMEOUT	1000	/* ms */

static bool mfc_reset(void)
{
	unsigned int mc_status;
	unsigned long timeo = jiffies;

	timeo += msecs_to_jiffies(MC_STATUS_TIMEOUT);

	/* Stop procedure */
	/* FIXME: F/W can be access invalid address */
	/* Reset VI */
	/*
	write_reg(0x3F7, MFC_SW_RESET);
	*/
	write_reg(0x3F6, MFC_SW_RESET);	/* Reset RISC */
	write_reg(0x3E2, MFC_SW_RESET);	/* All reset except for MC */
	mdelay(10);

	/* Check MC status */
	do {
		mc_status = (read_reg(MFC_MC_STATUS) & 0x3);

		if (mc_status == 0)
			break;

		schedule_timeout_uninterruptible(1);
		/* FiXME: cpu_relax() */
	} while (time_before(jiffies, timeo));

	if (mc_status != 0)
		return false;

	write_reg(0x0, MFC_SW_RESET);
	write_reg(0x3FE, MFC_SW_RESET);

	return true;
}

static void mfc_init_memctrl(void)
{
	/* Channel A, Port 0 */
	write_reg(mfc_mem_base(0), MFC_MC_DRAMBASE_ADR_A);
#if MFC_MAX_MEM_PORT_NUM == 1
	/* Channel B, Port 0 */
	write_reg(mfc_mem_base(0), MFC_MC_DRAMBASE_ADR_B);
#else
	/* Channel B, Port 1 */
	write_reg(mfc_mem_base(1), MFC_MC_DRAMBASE_ADR_B);
#endif
	mfc_dbg("Master A - 0x%08x\n",
		  read_reg(MFC_MC_DRAMBASE_ADR_A));
	mfc_dbg("Master B - 0x%08x\n",
		  read_reg(MFC_MC_DRAMBASE_ADR_B));
}

static void mfc_clear_cmds(void)
{
	write_reg(0xFFFFFFFF, MFC_SI_CH1_INST_ID);
	write_reg(0xFFFFFFFF, MFC_SI_CH2_INST_ID);

	write_reg(H2R_NOP, MFC_RISC2HOST_CMD);
	write_reg(R2H_NOP, MFC_HOST2RISC_CMD);
}

int mfc_load_firmware(const unsigned char *data, size_t size)
{
	volatile unsigned char *fw;

	if (!data || size == 0)
		return 0;

	/* MFC F/W area already 128KB aligned */
	fw = mfc_mem_addr(0);

	memcpy((void *)fw, data, size);

	mfc_mem_cache_clean((void *)fw, size);

	return 1;
}

int mfc_start(struct mfc_dev *dev)
{
	int ret;

	/* FIXME: when MFC start, load firmware again */
	/*
	dev->fw.state = mfc_load_firmware(dev->fw.info->data, dev->fw.info->size);
	*/

	mfc_clock_on();

	if (mfc_reset() == false) {
		mfc_clock_off();
		return MFC_FAIL;
	}

	mfc_init_memctrl();
	mfc_clear_cmds();

	ret = mfc_cmd_fw_start(dev);
	if (ret < 0) {
		mfc_clock_off();
		return ret;
	}

	ret = mfc_cmd_sys_init(dev);

	mfc_clock_off();

	return ret;
}

int mfc_sleep(struct mfc_dev *dev)
{
	int ret;

	mfc_clock_on();

	/* FIXME: add SFR backup? */

	ret = mfc_cmd_sys_sleep(dev);

	mfc_clock_off();

	/* FIXME: add mfc_power_off()? */

	/* FIXME: ret = 0 */
	return ret;
}

int mfc_wakeup(struct mfc_dev *dev)
{
	int ret;

	/* FIXME: add mfc_power_on()? */

	mfc_clock_on();

	if (mfc_reset() == false) {
		mfc_clock_off();
		return MFC_FAIL;
	}

	mfc_init_memctrl();
	mfc_clear_cmds();

	ret = mfc_cmd_sys_wakeup(dev);

	mfc_clock_off();

	/* FIXME: ret = 0 */
	return ret;
}


// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * linux/drivers/media/platform/samsung/s5p-mfc/s5p_mfc_ctrl.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include "s5p_mfc_cmd.h"
#include "s5p_mfc_common.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_intr.h"
#include "s5p_mfc_opr.h"
#include "s5p_mfc_pm.h"
#include "s5p_mfc_ctrl.h"

/* Allocate memory for firmware */
int s5p_mfc_alloc_firmware(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_priv_buf *fw_buf = &dev->fw_buf;
	int err;

	fw_buf->size = dev->variant->buf_size->fw;

	if (fw_buf->virt) {
		mfc_err("Attempting to allocate firmware when it seems that it is already loaded\n");
		return -ENOMEM;
	}

	err = s5p_mfc_alloc_priv_buf(dev, BANK_L_CTX, &dev->fw_buf);
	if (err) {
		mfc_err("Allocating bitprocessor buffer failed\n");
		return err;
	}

	return 0;
}

/* Load firmware */
int s5p_mfc_load_firmware(struct s5p_mfc_dev *dev)
{
	struct firmware *fw_blob;
	int i, err = -EINVAL;

	/* Firmware has to be present as a separate file or compiled
	 * into kernel. */
	mfc_debug_enter();

	if (dev->fw_get_done)
		return 0;

	for (i = MFC_FW_MAX_VERSIONS - 1; i >= 0; i--) {
		if (!dev->variant->fw_name[i])
			continue;
		err = request_firmware((const struct firmware **)&fw_blob,
				dev->variant->fw_name[i], &dev->plat_dev->dev);
		if (!err) {
			dev->fw_ver = (enum s5p_mfc_fw_ver) i;
			break;
		}
	}

	if (err != 0) {
		mfc_err("Firmware is not present in the /lib/firmware directory nor compiled in kernel\n");
		return -EINVAL;
	}
	if (fw_blob->size > dev->fw_buf.size) {
		mfc_err("MFC firmware is too big to be loaded\n");
		release_firmware(fw_blob);
		return -ENOMEM;
	}
	memcpy(dev->fw_buf.virt, fw_blob->data, fw_blob->size);
	wmb();
	dev->fw_get_done = true;
	release_firmware(fw_blob);
	mfc_debug_leave();
	return 0;
}

/* Release firmware memory */
int s5p_mfc_release_firmware(struct s5p_mfc_dev *dev)
{
	/* Before calling this function one has to make sure
	 * that MFC is no longer processing */
	s5p_mfc_release_priv_buf(dev, &dev->fw_buf);
	dev->fw_get_done = false;
	return 0;
}

static int s5p_mfc_bus_reset(struct s5p_mfc_dev *dev)
{
	unsigned int status;
	unsigned long timeout;

	/* Reset */
	mfc_write(dev, 0x1, S5P_FIMV_MFC_BUS_RESET_CTRL);
	timeout = jiffies + msecs_to_jiffies(MFC_BW_TIMEOUT);
	/* Check bus status */
	do {
		if (time_after(jiffies, timeout)) {
			mfc_err("Timeout while resetting MFC.\n");
			return -EIO;
		}
		status = mfc_read(dev, S5P_FIMV_MFC_BUS_RESET_CTRL);
	} while ((status & 0x2) == 0);
	return 0;
}

/* Reset the device */
int s5p_mfc_reset(struct s5p_mfc_dev *dev)
{
	unsigned int mc_status;
	unsigned long timeout;
	int i;

	mfc_debug_enter();

	if (IS_MFCV6_PLUS(dev)) {
		/* Zero Initialization of MFC registers */
		mfc_write(dev, 0, S5P_FIMV_RISC2HOST_CMD_V6);
		mfc_write(dev, 0, S5P_FIMV_HOST2RISC_CMD_V6);
		mfc_write(dev, 0, S5P_FIMV_FW_VERSION_V6);

		for (i = 0; i < S5P_FIMV_REG_CLEAR_COUNT_V6; i++)
			mfc_write(dev, 0, S5P_FIMV_REG_CLEAR_BEGIN_V6 + (i*4));

		/* check bus reset control before reset */
		if (dev->risc_on)
			if (s5p_mfc_bus_reset(dev))
				return -EIO;
		/* Reset
		 * set RISC_ON to 0 during power_on & wake_up.
		 * V6 needs RISC_ON set to 0 during reset also.
		 */
		if ((!dev->risc_on) || (!IS_MFCV7_PLUS(dev)))
			mfc_write(dev, 0, S5P_FIMV_RISC_ON_V6);

		mfc_write(dev, 0x1FFF, S5P_FIMV_MFC_RESET_V6);
		mfc_write(dev, 0, S5P_FIMV_MFC_RESET_V6);
	} else {
		/* Stop procedure */
		/*  reset RISC */
		mfc_write(dev, 0x3f6, S5P_FIMV_SW_RESET);
		/*  All reset except for MC */
		mfc_write(dev, 0x3e2, S5P_FIMV_SW_RESET);
		mdelay(10);

		timeout = jiffies + msecs_to_jiffies(MFC_BW_TIMEOUT);
		/* Check MC status */
		do {
			if (time_after(jiffies, timeout)) {
				mfc_err("Timeout while resetting MFC\n");
				return -EIO;
			}

			mc_status = mfc_read(dev, S5P_FIMV_MC_STATUS);

		} while (mc_status & 0x3);

		mfc_write(dev, 0x0, S5P_FIMV_SW_RESET);
		mfc_write(dev, 0x3fe, S5P_FIMV_SW_RESET);
	}

	mfc_debug_leave();
	return 0;
}

static inline void s5p_mfc_init_memctrl(struct s5p_mfc_dev *dev)
{
	if (IS_MFCV6_PLUS(dev)) {
		mfc_write(dev, dev->dma_base[BANK_L_CTX],
			  S5P_FIMV_RISC_BASE_ADDRESS_V6);
		mfc_debug(2, "Base Address : %pad\n",
			  &dev->dma_base[BANK_L_CTX]);
	} else {
		mfc_write(dev, dev->dma_base[BANK_L_CTX],
			  S5P_FIMV_MC_DRAMBASE_ADR_A);
		mfc_write(dev, dev->dma_base[BANK_R_CTX],
			  S5P_FIMV_MC_DRAMBASE_ADR_B);
		mfc_debug(2, "Bank1: %pad, Bank2: %pad\n",
			  &dev->dma_base[BANK_L_CTX],
			  &dev->dma_base[BANK_R_CTX]);
	}
}

static inline void s5p_mfc_clear_cmds(struct s5p_mfc_dev *dev)
{
	if (IS_MFCV6_PLUS(dev)) {
		/* Zero initialization should be done before RESET.
		 * Nothing to do here. */
	} else {
		mfc_write(dev, 0xffffffff, S5P_FIMV_SI_CH0_INST_ID);
		mfc_write(dev, 0xffffffff, S5P_FIMV_SI_CH1_INST_ID);
		mfc_write(dev, 0, S5P_FIMV_RISC2HOST_CMD);
		mfc_write(dev, 0, S5P_FIMV_HOST2RISC_CMD);
	}
}

/* Initialize hardware */
int s5p_mfc_init_hw(struct s5p_mfc_dev *dev)
{
	unsigned int ver;
	int ret;

	mfc_debug_enter();
	if (!dev->fw_buf.virt) {
		mfc_err("Firmware memory is not allocated.\n");
		return -EINVAL;
	}

	/* 0. MFC reset */
	mfc_debug(2, "MFC reset..\n");
	s5p_mfc_clock_on();
	dev->risc_on = 0;
	ret = s5p_mfc_reset(dev);
	if (ret) {
		mfc_err("Failed to reset MFC - timeout\n");
		return ret;
	}
	mfc_debug(2, "Done MFC reset..\n");
	/* 1. Set DRAM base Addr */
	s5p_mfc_init_memctrl(dev);
	/* 2. Initialize registers of channel I/F */
	s5p_mfc_clear_cmds(dev);
	/* 3. Release reset signal to the RISC */
	s5p_mfc_clean_dev_int_flags(dev);
	if (IS_MFCV6_PLUS(dev)) {
		dev->risc_on = 1;
		mfc_write(dev, 0x1, S5P_FIMV_RISC_ON_V6);
	}
	else
		mfc_write(dev, 0x3ff, S5P_FIMV_SW_RESET);

	if (IS_MFCV10(dev))
		mfc_write(dev, 0x0, S5P_FIMV_MFC_CLOCK_OFF_V10);

	mfc_debug(2, "Will now wait for completion of firmware transfer\n");
	if (s5p_mfc_wait_for_done_dev(dev, S5P_MFC_R2H_CMD_FW_STATUS_RET)) {
		mfc_err("Failed to load firmware\n");
		s5p_mfc_reset(dev);
		s5p_mfc_clock_off();
		return -EIO;
	}
	s5p_mfc_clean_dev_int_flags(dev);
	/* 4. Initialize firmware */
	ret = s5p_mfc_hw_call(dev->mfc_cmds, sys_init_cmd, dev);
	if (ret) {
		mfc_err("Failed to send command to MFC - timeout\n");
		s5p_mfc_reset(dev);
		s5p_mfc_clock_off();
		return ret;
	}
	mfc_debug(2, "Ok, now will wait for completion of hardware init\n");
	if (s5p_mfc_wait_for_done_dev(dev, S5P_MFC_R2H_CMD_SYS_INIT_RET)) {
		mfc_err("Failed to init hardware\n");
		s5p_mfc_reset(dev);
		s5p_mfc_clock_off();
		return -EIO;
	}
	dev->int_cond = 0;
	if (dev->int_err != 0 || dev->int_type !=
					S5P_MFC_R2H_CMD_SYS_INIT_RET) {
		/* Failure. */
		mfc_err("Failed to init firmware - error: %d int: %d\n",
						dev->int_err, dev->int_type);
		s5p_mfc_reset(dev);
		s5p_mfc_clock_off();
		return -EIO;
	}
	if (IS_MFCV6_PLUS(dev))
		ver = mfc_read(dev, S5P_FIMV_FW_VERSION_V6);
	else
		ver = mfc_read(dev, S5P_FIMV_FW_VERSION);

	mfc_debug(2, "MFC F/W version : %02xyy, %02xmm, %02xdd\n",
		(ver >> 16) & 0xFF, (ver >> 8) & 0xFF, ver & 0xFF);
	s5p_mfc_clock_off();
	mfc_debug_leave();
	return 0;
}


/* Deinitialize hardware */
void s5p_mfc_deinit_hw(struct s5p_mfc_dev *dev)
{
	s5p_mfc_clock_on();

	s5p_mfc_reset(dev);
	s5p_mfc_hw_call(dev->mfc_ops, release_dev_context_buffer, dev);

	s5p_mfc_clock_off();
}

int s5p_mfc_sleep(struct s5p_mfc_dev *dev)
{
	int ret;

	mfc_debug_enter();
	s5p_mfc_clock_on();
	s5p_mfc_clean_dev_int_flags(dev);
	ret = s5p_mfc_hw_call(dev->mfc_cmds, sleep_cmd, dev);
	if (ret) {
		mfc_err("Failed to send command to MFC - timeout\n");
		return ret;
	}
	if (s5p_mfc_wait_for_done_dev(dev, S5P_MFC_R2H_CMD_SLEEP_RET)) {
		mfc_err("Failed to sleep\n");
		return -EIO;
	}
	s5p_mfc_clock_off();
	dev->int_cond = 0;
	if (dev->int_err != 0 || dev->int_type !=
						S5P_MFC_R2H_CMD_SLEEP_RET) {
		/* Failure. */
		mfc_err("Failed to sleep - error: %d int: %d\n", dev->int_err,
								dev->int_type);
		return -EIO;
	}
	mfc_debug_leave();
	return ret;
}

static int s5p_mfc_v8_wait_wakeup(struct s5p_mfc_dev *dev)
{
	int ret;

	/* Release reset signal to the RISC */
	dev->risc_on = 1;
	mfc_write(dev, 0x1, S5P_FIMV_RISC_ON_V6);

	if (s5p_mfc_wait_for_done_dev(dev, S5P_MFC_R2H_CMD_FW_STATUS_RET)) {
		mfc_err("Failed to reset MFCV8\n");
		return -EIO;
	}
	mfc_debug(2, "Write command to wakeup MFCV8\n");
	ret = s5p_mfc_hw_call(dev->mfc_cmds, wakeup_cmd, dev);
	if (ret) {
		mfc_err("Failed to send command to MFCV8 - timeout\n");
		return ret;
	}

	if (s5p_mfc_wait_for_done_dev(dev, S5P_MFC_R2H_CMD_WAKEUP_RET)) {
		mfc_err("Failed to wakeup MFC\n");
		return -EIO;
	}
	return ret;
}

static int s5p_mfc_wait_wakeup(struct s5p_mfc_dev *dev)
{
	int ret;

	/* Send MFC wakeup command */
	ret = s5p_mfc_hw_call(dev->mfc_cmds, wakeup_cmd, dev);
	if (ret) {
		mfc_err("Failed to send command to MFC - timeout\n");
		return ret;
	}

	/* Release reset signal to the RISC */
	if (IS_MFCV6_PLUS(dev)) {
		dev->risc_on = 1;
		mfc_write(dev, 0x1, S5P_FIMV_RISC_ON_V6);
	} else {
		mfc_write(dev, 0x3ff, S5P_FIMV_SW_RESET);
	}

	if (s5p_mfc_wait_for_done_dev(dev, S5P_MFC_R2H_CMD_WAKEUP_RET)) {
		mfc_err("Failed to wakeup MFC\n");
		return -EIO;
	}
	return ret;
}

int s5p_mfc_wakeup(struct s5p_mfc_dev *dev)
{
	int ret;

	mfc_debug_enter();
	/* 0. MFC reset */
	mfc_debug(2, "MFC reset..\n");
	s5p_mfc_clock_on();
	dev->risc_on = 0;
	ret = s5p_mfc_reset(dev);
	if (ret) {
		mfc_err("Failed to reset MFC - timeout\n");
		s5p_mfc_clock_off();
		return ret;
	}
	mfc_debug(2, "Done MFC reset..\n");
	/* 1. Set DRAM base Addr */
	s5p_mfc_init_memctrl(dev);
	/* 2. Initialize registers of channel I/F */
	s5p_mfc_clear_cmds(dev);
	s5p_mfc_clean_dev_int_flags(dev);
	/* 3. Send MFC wakeup command and wait for completion*/
	if (IS_MFCV8_PLUS(dev))
		ret = s5p_mfc_v8_wait_wakeup(dev);
	else
		ret = s5p_mfc_wait_wakeup(dev);

	s5p_mfc_clock_off();
	if (ret)
		return ret;

	dev->int_cond = 0;
	if (dev->int_err != 0 || dev->int_type !=
						S5P_MFC_R2H_CMD_WAKEUP_RET) {
		/* Failure. */
		mfc_err("Failed to wakeup - error: %d int: %d\n", dev->int_err,
								dev->int_type);
		return -EIO;
	}
	mfc_debug_leave();
	return 0;
}

int s5p_mfc_open_mfc_inst(struct s5p_mfc_dev *dev, struct s5p_mfc_ctx *ctx)
{
	int ret = 0;

	ret = s5p_mfc_hw_call(dev->mfc_ops, alloc_instance_buffer, ctx);
	if (ret) {
		mfc_err("Failed allocating instance buffer\n");
		goto err;
	}

	if (ctx->type == MFCINST_DECODER) {
		ret = s5p_mfc_hw_call(dev->mfc_ops,
					alloc_dec_temp_buffers, ctx);
		if (ret) {
			mfc_err("Failed allocating temporary buffers\n");
			goto err_free_inst_buf;
		}
	}

	set_work_bit_irqsave(ctx);
	s5p_mfc_hw_call(dev->mfc_ops, try_run, dev);
	if (s5p_mfc_wait_for_done_ctx(ctx,
		S5P_MFC_R2H_CMD_OPEN_INSTANCE_RET, 0)) {
		/* Error or timeout */
		mfc_err("Error getting instance from hardware\n");
		ret = -EIO;
		goto err_free_desc_buf;
	}

	mfc_debug(2, "Got instance number: %d\n", ctx->inst_no);
	return ret;

err_free_desc_buf:
	if (ctx->type == MFCINST_DECODER)
		s5p_mfc_hw_call(dev->mfc_ops, release_dec_desc_buffer, ctx);
err_free_inst_buf:
	s5p_mfc_hw_call(dev->mfc_ops, release_instance_buffer, ctx);
err:
	return ret;
}

void s5p_mfc_close_mfc_inst(struct s5p_mfc_dev *dev, struct s5p_mfc_ctx *ctx)
{
	ctx->state = MFCINST_RETURN_INST;
	set_work_bit_irqsave(ctx);
	s5p_mfc_hw_call(dev->mfc_ops, try_run, dev);
	/* Wait until instance is returned or timeout occurred */
	if (s5p_mfc_wait_for_done_ctx(ctx,
				S5P_MFC_R2H_CMD_CLOSE_INSTANCE_RET, 0)){
		clear_work_bit_irqsave(ctx);
		mfc_err("Err returning instance\n");
	}

	/* Free resources */
	s5p_mfc_hw_call(dev->mfc_ops, release_codec_buffers, ctx);
	s5p_mfc_hw_call(dev->mfc_ops, release_instance_buffer, ctx);
	if (ctx->type == MFCINST_DECODER)
		s5p_mfc_hw_call(dev->mfc_ops, release_dec_desc_buffer, ctx);

	ctx->inst_no = MFC_NO_INSTANCE_SET;
	ctx->state = MFCINST_FREE;
}

/*
 * linux/drivers/media/platform/s5p-mfc/s5p_mfc_ctrl.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

/* Allocate memory for firmware */
int s5p_mfc_alloc_firmware(struct s5p_mfc_dev *dev)
{
	void *bank2_virt;
	dma_addr_t bank2_dma_addr;

	dev->fw_size = dev->variant->buf_size->fw;

	if (dev->fw_virt_addr) {
		mfc_err("Attempting to allocate firmware when it seems that it is already loaded\n");
		return -ENOMEM;
	}

	dev->fw_virt_addr = dma_alloc_coherent(dev->mem_dev_l, dev->fw_size,
					&dev->bank1, GFP_KERNEL);

	if (IS_ERR_OR_NULL(dev->fw_virt_addr)) {
		dev->fw_virt_addr = NULL;
		mfc_err("Allocating bitprocessor buffer failed\n");
		return -ENOMEM;
	}

	dev->bank1 = dev->bank1;

	if (HAS_PORTNUM(dev) && IS_TWOPORT(dev)) {
		bank2_virt = dma_alloc_coherent(dev->mem_dev_r, 1 << MFC_BASE_ALIGN_ORDER,
					&bank2_dma_addr, GFP_KERNEL);

		if (IS_ERR(dev->fw_virt_addr)) {
			mfc_err("Allocating bank2 base failed\n");
			dma_free_coherent(dev->mem_dev_l, dev->fw_size,
				dev->fw_virt_addr, dev->bank1);
			dev->fw_virt_addr = NULL;
			return -ENOMEM;
		}

		/* Valid buffers passed to MFC encoder with LAST_FRAME command
		 * should not have address of bank2 - MFC will treat it as a null frame.
		 * To avoid such situation we set bank2 address below the pool address.
		 */
		dev->bank2 = bank2_dma_addr - (1 << MFC_BASE_ALIGN_ORDER);

		dma_free_coherent(dev->mem_dev_r, 1 << MFC_BASE_ALIGN_ORDER,
			bank2_virt, bank2_dma_addr);

	} else {
		/* In this case bank2 can point to the same address as bank1.
		 * Firmware will always occupy the beginning of this area so it is
		 * impossible having a video frame buffer with zero address. */
		dev->bank2 = dev->bank1;
	}
	return 0;
}

/* Load firmware */
int s5p_mfc_load_firmware(struct s5p_mfc_dev *dev)
{
	struct firmware *fw_blob;
	int err;

	/* Firmare has to be present as a separate file or compiled
	 * into kernel. */
	mfc_debug_enter();

	err = request_firmware((const struct firmware **)&fw_blob,
				     dev->variant->fw_name, dev->v4l2_dev.dev);
	if (err != 0) {
		mfc_err("Firmware is not present in the /lib/firmware directory nor compiled in kernel\n");
		return -EINVAL;
	}
	if (fw_blob->size > dev->fw_size) {
		mfc_err("MFC firmware is too big to be loaded\n");
		release_firmware(fw_blob);
		return -ENOMEM;
	}
	if (!dev->fw_virt_addr) {
		mfc_err("MFC firmware is not allocated\n");
		release_firmware(fw_blob);
		return -EINVAL;
	}
	memcpy(dev->fw_virt_addr, fw_blob->data, fw_blob->size);
	wmb();
	release_firmware(fw_blob);
	mfc_debug_leave();
	return 0;
}

/* Reload firmware to MFC */
int s5p_mfc_reload_firmware(struct s5p_mfc_dev *dev)
{
	struct firmware *fw_blob;
	int err;

	/* Firmare has to be present as a separate file or compiled
	 * into kernel. */
	mfc_debug_enter();

	err = request_firmware((const struct firmware **)&fw_blob,
				     dev->variant->fw_name, dev->v4l2_dev.dev);
	if (err != 0) {
		mfc_err("Firmware is not present in the /lib/firmware directory nor compiled in kernel\n");
		return -EINVAL;
	}
	if (fw_blob->size > dev->fw_size) {
		mfc_err("MFC firmware is too big to be loaded\n");
		release_firmware(fw_blob);
		return -ENOMEM;
	}
	if (!dev->fw_virt_addr) {
		mfc_err("MFC firmware is not allocated\n");
		release_firmware(fw_blob);
		return -EINVAL;
	}
	memcpy(dev->fw_virt_addr, fw_blob->data, fw_blob->size);
	wmb();
	release_firmware(fw_blob);
	mfc_debug_leave();
	return 0;
}

/* Release firmware memory */
int s5p_mfc_release_firmware(struct s5p_mfc_dev *dev)
{
	/* Before calling this function one has to make sure
	 * that MFC is no longer processing */
	if (!dev->fw_virt_addr)
		return -EINVAL;
	dma_free_coherent(dev->mem_dev_l, dev->fw_size, dev->fw_virt_addr,
						dev->bank1);
	dev->fw_virt_addr = NULL;
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
		/* Reset IP */
		/*  except RISC, reset */
		mfc_write(dev, 0xFEE, S5P_FIMV_MFC_RESET_V6);
		/*  reset release */
		mfc_write(dev, 0x0, S5P_FIMV_MFC_RESET_V6);

		/* Zero Initialization of MFC registers */
		mfc_write(dev, 0, S5P_FIMV_RISC2HOST_CMD_V6);
		mfc_write(dev, 0, S5P_FIMV_HOST2RISC_CMD_V6);
		mfc_write(dev, 0, S5P_FIMV_FW_VERSION_V6);

		for (i = 0; i < S5P_FIMV_REG_CLEAR_COUNT_V6; i++)
			mfc_write(dev, 0, S5P_FIMV_REG_CLEAR_BEGIN_V6 + (i*4));

		/* Reset */
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
		mfc_write(dev, dev->bank1, S5P_FIMV_RISC_BASE_ADDRESS_V6);
		mfc_debug(2, "Base Address : %08x\n", dev->bank1);
	} else {
		mfc_write(dev, dev->bank1, S5P_FIMV_MC_DRAMBASE_ADR_A);
		mfc_write(dev, dev->bank2, S5P_FIMV_MC_DRAMBASE_ADR_B);
		mfc_debug(2, "Bank1: %08x, Bank2: %08x\n",
				dev->bank1, dev->bank2);
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
	if (!dev->fw_virt_addr) {
		mfc_err("Firmware memory is not allocated.\n");
		return -EINVAL;
	}

	/* 0. MFC reset */
	mfc_debug(2, "MFC reset..\n");
	s5p_mfc_clock_on();
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
	if (IS_MFCV6_PLUS(dev))
		mfc_write(dev, 0x1, S5P_FIMV_RISC_ON_V6);
	else
		mfc_write(dev, 0x3ff, S5P_FIMV_SW_RESET);
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
	mfc_debug(2, "Ok, now will write a command to init the system\n");
	if (s5p_mfc_wait_for_done_dev(dev, S5P_MFC_R2H_CMD_SYS_INIT_RET)) {
		mfc_err("Failed to load firmware\n");
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

int s5p_mfc_wakeup(struct s5p_mfc_dev *dev)
{
	int ret;

	mfc_debug_enter();
	/* 0. MFC reset */
	mfc_debug(2, "MFC reset..\n");
	s5p_mfc_clock_on();
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
	s5p_mfc_clean_dev_int_flags(dev);
	/* 3. Initialize firmware */
	ret = s5p_mfc_hw_call(dev->mfc_cmds, wakeup_cmd, dev);
	if (ret) {
		mfc_err("Failed to send command to MFC - timeout\n");
		return ret;
	}
	/* 4. Release reset signal to the RISC */
	if (IS_MFCV6_PLUS(dev))
		mfc_write(dev, 0x1, S5P_FIMV_RISC_ON_V6);
	else
		mfc_write(dev, 0x3ff, S5P_FIMV_SW_RESET);
	mfc_debug(2, "Ok, now will write a command to wakeup the system\n");
	if (s5p_mfc_wait_for_done_dev(dev, S5P_MFC_R2H_CMD_WAKEUP_RET)) {
		mfc_err("Failed to load firmware\n");
		return -EIO;
	}
	s5p_mfc_clock_off();
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


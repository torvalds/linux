/*
 * Firmware I/O code for mac80211 ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * Based on:
 * ST-Ericsson UMAC CW1200 driver which is
 * Copyright (c) 2010, ST-Ericsson
 * Author: Ajitpal Singh <ajitpal.singh@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/firmware.h>

#include "cw1200.h"
#include "fwio.h"
#include "hwio.h"
#include "hwbus.h"
#include "bh.h"

static int cw1200_get_hw_type(u32 config_reg_val, int *major_revision)
{
	int hw_type = -1;
	u32 silicon_type = (config_reg_val >> 24) & 0x7;
	u32 silicon_vers = (config_reg_val >> 31) & 0x1;

	switch (silicon_type) {
	case 0x00:
		*major_revision = 1;
		hw_type = HIF_9000_SILICON_VERSATILE;
		break;
	case 0x01:
	case 0x02: /* CW1x00 */
	case 0x04: /* CW1x60 */
		*major_revision = silicon_type;
		if (silicon_vers)
			hw_type = HIF_8601_VERSATILE;
		else
			hw_type = HIF_8601_SILICON;
		break;
	default:
		break;
	}

	return hw_type;
}

static int cw1200_load_firmware_cw1200(struct cw1200_common *priv)
{
	int ret, block, num_blocks;
	unsigned i;
	u32 val32;
	u32 put = 0, get = 0;
	u8 *buf = NULL;
	const char *fw_path;
	const struct firmware *firmware = NULL;

	/* Macroses are local. */
#define APB_WRITE(reg, val) \
	do { \
		ret = cw1200_apb_write_32(priv, CW1200_APB(reg), (val)); \
		if (ret < 0) \
			goto error; \
	} while (0)
#define APB_READ(reg, val) \
	do { \
		ret = cw1200_apb_read_32(priv, CW1200_APB(reg), &(val)); \
		if (ret < 0) \
			goto error; \
	} while (0)
#define REG_WRITE(reg, val) \
	do { \
		ret = cw1200_reg_write_32(priv, (reg), (val)); \
		if (ret < 0) \
			goto error; \
	} while (0)
#define REG_READ(reg, val) \
	do { \
		ret = cw1200_reg_read_32(priv, (reg), &(val)); \
		if (ret < 0) \
			goto error; \
	} while (0)

	switch (priv->hw_revision) {
	case CW1200_HW_REV_CUT10:
		fw_path = FIRMWARE_CUT10;
		if (!priv->sdd_path)
			priv->sdd_path = SDD_FILE_10;
		break;
	case CW1200_HW_REV_CUT11:
		fw_path = FIRMWARE_CUT11;
		if (!priv->sdd_path)
			priv->sdd_path = SDD_FILE_11;
		break;
	case CW1200_HW_REV_CUT20:
		fw_path = FIRMWARE_CUT20;
		if (!priv->sdd_path)
			priv->sdd_path = SDD_FILE_20;
		break;
	case CW1200_HW_REV_CUT22:
		fw_path = FIRMWARE_CUT22;
		if (!priv->sdd_path)
			priv->sdd_path = SDD_FILE_22;
		break;
	case CW1X60_HW_REV:
		fw_path = FIRMWARE_CW1X60;
		if (!priv->sdd_path)
			priv->sdd_path = SDD_FILE_CW1X60;
		break;
	default:
		pr_err("Invalid silicon revision %d.\n", priv->hw_revision);
		return -EINVAL;
	}

	/* Initialize common registers */
	APB_WRITE(DOWNLOAD_IMAGE_SIZE_REG, DOWNLOAD_ARE_YOU_HERE);
	APB_WRITE(DOWNLOAD_PUT_REG, 0);
	APB_WRITE(DOWNLOAD_GET_REG, 0);
	APB_WRITE(DOWNLOAD_STATUS_REG, DOWNLOAD_PENDING);
	APB_WRITE(DOWNLOAD_FLAGS_REG, 0);

	/* Write the NOP Instruction */
	REG_WRITE(ST90TDS_SRAM_BASE_ADDR_REG_ID, 0xFFF20000);
	REG_WRITE(ST90TDS_AHB_DPORT_REG_ID, 0xEAFFFFFE);

	/* Release CPU from RESET */
	REG_READ(ST90TDS_CONFIG_REG_ID, val32);
	val32 &= ~ST90TDS_CONFIG_CPU_RESET_BIT;
	REG_WRITE(ST90TDS_CONFIG_REG_ID, val32);

	/* Enable Clock */
	val32 &= ~ST90TDS_CONFIG_CPU_CLK_DIS_BIT;
	REG_WRITE(ST90TDS_CONFIG_REG_ID, val32);

	/* Load a firmware file */
	ret = request_firmware(&firmware, fw_path, priv->pdev);
	if (ret) {
		pr_err("Can't load firmware file %s.\n", fw_path);
		goto error;
	}

	buf = kmalloc(DOWNLOAD_BLOCK_SIZE, GFP_KERNEL | GFP_DMA);
	if (!buf) {
		pr_err("Can't allocate firmware load buffer.\n");
		ret = -ENOMEM;
		goto error;
	}

	/* Check if the bootloader is ready */
	for (i = 0; i < 100; i += 1 + i / 2) {
		APB_READ(DOWNLOAD_IMAGE_SIZE_REG, val32);
		if (val32 == DOWNLOAD_I_AM_HERE)
			break;
		mdelay(i);
	} /* End of for loop */

	if (val32 != DOWNLOAD_I_AM_HERE) {
		pr_err("Bootloader is not ready.\n");
		ret = -ETIMEDOUT;
		goto error;
	}

	/* Calculcate number of download blocks */
	num_blocks = (firmware->size - 1) / DOWNLOAD_BLOCK_SIZE + 1;

	/* Updating the length in Download Ctrl Area */
	val32 = firmware->size; /* Explicit cast from size_t to u32 */
	APB_WRITE(DOWNLOAD_IMAGE_SIZE_REG, val32);

	/* Firmware downloading loop */
	for (block = 0; block < num_blocks; block++) {
		size_t tx_size;
		size_t block_size;

		/* check the download status */
		APB_READ(DOWNLOAD_STATUS_REG, val32);
		if (val32 != DOWNLOAD_PENDING) {
			pr_err("Bootloader reported error %d.\n", val32);
			ret = -EIO;
			goto error;
		}

		/* loop until put - get <= 24K */
		for (i = 0; i < 100; i++) {
			APB_READ(DOWNLOAD_GET_REG, get);
			if ((put - get) <=
			    (DOWNLOAD_FIFO_SIZE - DOWNLOAD_BLOCK_SIZE))
				break;
			mdelay(i);
		}

		if ((put - get) > (DOWNLOAD_FIFO_SIZE - DOWNLOAD_BLOCK_SIZE)) {
			pr_err("Timeout waiting for FIFO.\n");
			ret = -ETIMEDOUT;
			goto error;
		}

		/* calculate the block size */
		tx_size = block_size = min((size_t)(firmware->size - put),
			(size_t)DOWNLOAD_BLOCK_SIZE);

		memcpy(buf, &firmware->data[put], block_size);
		if (block_size < DOWNLOAD_BLOCK_SIZE) {
			memset(&buf[block_size], 0,
			       DOWNLOAD_BLOCK_SIZE - block_size);
			tx_size = DOWNLOAD_BLOCK_SIZE;
		}

		/* send the block to sram */
		ret = cw1200_apb_write(priv,
			CW1200_APB(DOWNLOAD_FIFO_OFFSET +
				   (put & (DOWNLOAD_FIFO_SIZE - 1))),
			buf, tx_size);
		if (ret < 0) {
			pr_err("Can't write firmware block @ %d!\n",
			       put & (DOWNLOAD_FIFO_SIZE - 1));
			goto error;
		}

		/* update the put register */
		put += block_size;
		APB_WRITE(DOWNLOAD_PUT_REG, put);
	} /* End of firmware download loop */

	/* Wait for the download completion */
	for (i = 0; i < 300; i += 1 + i / 2) {
		APB_READ(DOWNLOAD_STATUS_REG, val32);
		if (val32 != DOWNLOAD_PENDING)
			break;
		mdelay(i);
	}
	if (val32 != DOWNLOAD_SUCCESS) {
		pr_err("Wait for download completion failed: 0x%.8X\n", val32);
		ret = -ETIMEDOUT;
		goto error;
	} else {
		pr_info("Firmware download completed.\n");
		ret = 0;
	}

error:
	kfree(buf);
	if (firmware)
		release_firmware(firmware);
	return ret;

#undef APB_WRITE
#undef APB_READ
#undef REG_WRITE
#undef REG_READ
}


static int config_reg_read(struct cw1200_common *priv, u32 *val)
{
	switch (priv->hw_type) {
	case HIF_9000_SILICON_VERSATILE: {
		u16 val16;
		int ret = cw1200_reg_read_16(priv,
					     ST90TDS_CONFIG_REG_ID,
					     &val16);
		if (ret < 0)
			return ret;
		*val = val16;
		return 0;
	}
	case HIF_8601_VERSATILE:
	case HIF_8601_SILICON:
	default:
		cw1200_reg_read_32(priv, ST90TDS_CONFIG_REG_ID, val);
		break;
	}
	return 0;
}

static int config_reg_write(struct cw1200_common *priv, u32 val)
{
	switch (priv->hw_type) {
	case HIF_9000_SILICON_VERSATILE:
		return cw1200_reg_write_16(priv,
					   ST90TDS_CONFIG_REG_ID,
					   (u16)val);
	case HIF_8601_VERSATILE:
	case HIF_8601_SILICON:
	default:
		return cw1200_reg_write_32(priv, ST90TDS_CONFIG_REG_ID, val);
		break;
	}
	return 0;
}

int cw1200_load_firmware(struct cw1200_common *priv)
{
	int ret;
	int i;
	u32 val32;
	u16 val16;
	int major_revision = -1;

	/* Read CONFIG Register */
	ret = cw1200_reg_read_32(priv, ST90TDS_CONFIG_REG_ID, &val32);
	if (ret < 0) {
		pr_err("Can't read config register.\n");
		goto out;
	}

	if (val32 == 0 || val32 == 0xffffffff) {
		pr_err("Bad config register value (0x%08x)\n", val32);
		ret = -EIO;
		goto out;
	}

	priv->hw_type = cw1200_get_hw_type(val32, &major_revision);
	if (priv->hw_type < 0) {
		pr_err("Can't deduce hardware type.\n");
		ret = -ENOTSUPP;
		goto out;
	}

	/* Set DPLL Reg value, and read back to confirm writes work */
	ret = cw1200_reg_write_32(priv, ST90TDS_TSET_GEN_R_W_REG_ID,
				  cw1200_dpll_from_clk(priv->hw_refclk));
	if (ret < 0) {
		pr_err("Can't write DPLL register.\n");
		goto out;
	}

	msleep(20);

	ret = cw1200_reg_read_32(priv,
		ST90TDS_TSET_GEN_R_W_REG_ID, &val32);
	if (ret < 0) {
		pr_err("Can't read DPLL register.\n");
		goto out;
	}

	if (val32 != cw1200_dpll_from_clk(priv->hw_refclk)) {
		pr_err("Unable to initialise DPLL register. Wrote 0x%.8X, Read 0x%.8X.\n",
		       cw1200_dpll_from_clk(priv->hw_refclk), val32);
		ret = -EIO;
		goto out;
	}

	/* Set wakeup bit in device */
	ret = cw1200_reg_read_16(priv, ST90TDS_CONTROL_REG_ID, &val16);
	if (ret < 0) {
		pr_err("set_wakeup: can't read control register.\n");
		goto out;
	}

	ret = cw1200_reg_write_16(priv, ST90TDS_CONTROL_REG_ID,
		val16 | ST90TDS_CONT_WUP_BIT);
	if (ret < 0) {
		pr_err("set_wakeup: can't write control register.\n");
		goto out;
	}

	/* Wait for wakeup */
	for (i = 0; i < 300; i += (1 + i / 2)) {
		ret = cw1200_reg_read_16(priv,
			ST90TDS_CONTROL_REG_ID, &val16);
		if (ret < 0) {
			pr_err("wait_for_wakeup: can't read control register.\n");
			goto out;
		}

		if (val16 & ST90TDS_CONT_RDY_BIT)
			break;

		msleep(i);
	}

	if ((val16 & ST90TDS_CONT_RDY_BIT) == 0) {
		pr_err("wait_for_wakeup: device is not responding.\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	switch (major_revision) {
	case 1:
		/* CW1200 Hardware detection logic : Check for CUT1.1 */
		ret = cw1200_ahb_read_32(priv, CW1200_CUT_ID_ADDR, &val32);
		if (ret) {
			pr_err("HW detection: can't read CUT ID.\n");
			goto out;
		}

		switch (val32) {
		case CW1200_CUT_11_ID_STR:
			pr_info("CW1x00 Cut 1.1 silicon detected.\n");
			priv->hw_revision = CW1200_HW_REV_CUT11;
			break;
		default:
			pr_info("CW1x00 Cut 1.0 silicon detected.\n");
			priv->hw_revision = CW1200_HW_REV_CUT10;
			break;
		}

		/* According to ST-E, CUT<2.0 has busted BA TID0-3.
		   Just disable it entirely...
		*/
		priv->ba_rx_tid_mask = 0;
		priv->ba_tx_tid_mask = 0;
		break;
	case 2: {
		u32 ar1, ar2, ar3;
		ret = cw1200_ahb_read_32(priv, CW1200_CUT2_ID_ADDR, &ar1);
		if (ret) {
			pr_err("(1) HW detection: can't read CUT ID\n");
			goto out;
		}
		ret = cw1200_ahb_read_32(priv, CW1200_CUT2_ID_ADDR + 4, &ar2);
		if (ret) {
			pr_err("(2) HW detection: can't read CUT ID.\n");
			goto out;
		}

		ret = cw1200_ahb_read_32(priv, CW1200_CUT2_ID_ADDR + 8, &ar3);
		if (ret) {
			pr_err("(3) HW detection: can't read CUT ID.\n");
			goto out;
		}

		if (ar1 == CW1200_CUT_22_ID_STR1 &&
		    ar2 == CW1200_CUT_22_ID_STR2 &&
		    ar3 == CW1200_CUT_22_ID_STR3) {
			pr_info("CW1x00 Cut 2.2 silicon detected.\n");
			priv->hw_revision = CW1200_HW_REV_CUT22;
		} else {
			pr_info("CW1x00 Cut 2.0 silicon detected.\n");
			priv->hw_revision = CW1200_HW_REV_CUT20;
		}
		break;
	}
	case 4:
		pr_info("CW1x60 silicon detected.\n");
		priv->hw_revision = CW1X60_HW_REV;
		break;
	default:
		pr_err("Unsupported silicon major revision %d.\n",
		       major_revision);
		ret = -ENOTSUPP;
		goto out;
	}

	/* Checking for access mode */
	ret = config_reg_read(priv, &val32);
	if (ret < 0) {
		pr_err("Can't read config register.\n");
		goto out;
	}

	if (!(val32 & ST90TDS_CONFIG_ACCESS_MODE_BIT)) {
		pr_err("Device is already in QUEUE mode!\n");
			ret = -EINVAL;
			goto out;
	}

	switch (priv->hw_type)  {
	case HIF_8601_SILICON:
		if (priv->hw_revision == CW1X60_HW_REV) {
			pr_err("Can't handle CW1160/1260 firmware load yet.\n");
			ret = -ENOTSUPP;
			goto out;
		}
		ret = cw1200_load_firmware_cw1200(priv);
		break;
	default:
		pr_err("Can't perform firmware load for hw type %d.\n",
		       priv->hw_type);
		ret = -ENOTSUPP;
		goto out;
	}
	if (ret < 0) {
		pr_err("Firmware load error.\n");
		goto out;
	}

	/* Enable interrupt signalling */
	priv->hwbus_ops->lock(priv->hwbus_priv);
	ret = __cw1200_irq_enable(priv, 1);
	priv->hwbus_ops->unlock(priv->hwbus_priv);
	if (ret < 0)
		goto unsubscribe;

	/* Configure device for MESSSAGE MODE */
	ret = config_reg_read(priv, &val32);
	if (ret < 0) {
		pr_err("Can't read config register.\n");
		goto unsubscribe;
	}
	ret = config_reg_write(priv, val32 & ~ST90TDS_CONFIG_ACCESS_MODE_BIT);
	if (ret < 0) {
		pr_err("Can't write config register.\n");
		goto unsubscribe;
	}

	/* Unless we read the CONFIG Register we are
	 * not able to get an interrupt
	 */
	mdelay(10);
	config_reg_read(priv, &val32);

out:
	return ret;

unsubscribe:
	/* Disable interrupt signalling */
	priv->hwbus_ops->lock(priv->hwbus_priv);
	ret = __cw1200_irq_enable(priv, 0);
	priv->hwbus_ops->unlock(priv->hwbus_priv);
	return ret;
}

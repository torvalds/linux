/**
 * Marvell BT-over-SDIO driver: SDIO interface related functions.
 *
 * Copyright (C) 2009, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 **/

#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/suspend.h>

#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>
#include <linux/devcoredump.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btmrvl_drv.h"
#include "btmrvl_sdio.h"

#define VERSION "1.0"

static struct memory_type_mapping mem_type_mapping_tbl[] = {
	{"ITCM", NULL, 0, 0xF0},
	{"DTCM", NULL, 0, 0xF1},
	{"SQRAM", NULL, 0, 0xF2},
	{"APU", NULL, 0, 0xF3},
	{"CIU", NULL, 0, 0xF4},
	{"ICU", NULL, 0, 0xF5},
	{"MAC", NULL, 0, 0xF6},
	{"EXT7", NULL, 0, 0xF7},
	{"EXT8", NULL, 0, 0xF8},
	{"EXT9", NULL, 0, 0xF9},
	{"EXT10", NULL, 0, 0xFA},
	{"EXT11", NULL, 0, 0xFB},
	{"EXT12", NULL, 0, 0xFC},
	{"EXT13", NULL, 0, 0xFD},
	{"EXTLAST", NULL, 0, 0xFE},
};

static const struct of_device_id btmrvl_sdio_of_match_table[] = {
	{ .compatible = "marvell,sd8897-bt" },
	{ .compatible = "marvell,sd8997-bt" },
	{ }
};

static irqreturn_t btmrvl_wake_irq_bt(int irq, void *priv)
{
	struct btmrvl_sdio_card *card = priv;
	struct device *dev = &card->func->dev;
	struct btmrvl_plt_wake_cfg *cfg = card->plt_wake_cfg;

	dev_info(dev, "wake by bt\n");
	cfg->wake_by_bt = true;
	disable_irq_nosync(irq);

	pm_wakeup_event(dev, 0);
	pm_system_wakeup();

	return IRQ_HANDLED;
}

/* This function parses device tree node using mmc subnode devicetree API.
 * The device node is saved in card->plt_of_node.
 * If the device tree node exists and includes interrupts attributes, this
 * function will request platform specific wakeup interrupt.
 */
static int btmrvl_sdio_probe_of(struct device *dev,
				struct btmrvl_sdio_card *card)
{
	struct btmrvl_plt_wake_cfg *cfg;
	int ret;

	if (!dev->of_node ||
	    !of_match_node(btmrvl_sdio_of_match_table, dev->of_node)) {
		dev_info(dev, "sdio device tree data not available\n");
		return -1;
	}

	card->plt_of_node = dev->of_node;

	card->plt_wake_cfg = devm_kzalloc(dev, sizeof(*card->plt_wake_cfg),
					  GFP_KERNEL);
	cfg = card->plt_wake_cfg;
	if (cfg && card->plt_of_node) {
		cfg->irq_bt = irq_of_parse_and_map(card->plt_of_node, 0);
		if (!cfg->irq_bt) {
			dev_err(dev, "fail to parse irq_bt from device tree\n");
			cfg->irq_bt = -1;
		} else {
			ret = devm_request_irq(dev, cfg->irq_bt,
					       btmrvl_wake_irq_bt,
					       0, "bt_wake", card);
			if (ret) {
				dev_err(dev,
					"Failed to request irq_bt %d (%d)\n",
					cfg->irq_bt, ret);
			}

			/* Configure wakeup (enabled by default) */
			device_init_wakeup(dev, true);
			disable_irq(cfg->irq_bt);
		}
	}

	return 0;
}

/* The btmrvl_sdio_remove() callback function is called
 * when user removes this module from kernel space or ejects
 * the card from the slot. The driver handles these 2 cases
 * differently.
 * If the user is removing the module, a MODULE_SHUTDOWN_REQ
 * command is sent to firmware and interrupt will be disabled.
 * If the card is removed, there is no need to send command
 * or disable interrupt.
 *
 * The variable 'user_rmmod' is used to distinguish these two
 * scenarios. This flag is initialized as FALSE in case the card
 * is removed, and will be set to TRUE for module removal when
 * module_exit function is called.
 */
static u8 user_rmmod;
static u8 sdio_ireg;

static const struct btmrvl_sdio_card_reg btmrvl_reg_8688 = {
	.cfg = 0x03,
	.host_int_mask = 0x04,
	.host_intstatus = 0x05,
	.card_status = 0x20,
	.sq_read_base_addr_a0 = 0x10,
	.sq_read_base_addr_a1 = 0x11,
	.card_fw_status0 = 0x40,
	.card_fw_status1 = 0x41,
	.card_rx_len = 0x42,
	.card_rx_unit = 0x43,
	.io_port_0 = 0x00,
	.io_port_1 = 0x01,
	.io_port_2 = 0x02,
	.int_read_to_clear = false,
};
static const struct btmrvl_sdio_card_reg btmrvl_reg_87xx = {
	.cfg = 0x00,
	.host_int_mask = 0x02,
	.host_intstatus = 0x03,
	.card_status = 0x30,
	.sq_read_base_addr_a0 = 0x40,
	.sq_read_base_addr_a1 = 0x41,
	.card_revision = 0x5c,
	.card_fw_status0 = 0x60,
	.card_fw_status1 = 0x61,
	.card_rx_len = 0x62,
	.card_rx_unit = 0x63,
	.io_port_0 = 0x78,
	.io_port_1 = 0x79,
	.io_port_2 = 0x7a,
	.int_read_to_clear = false,
};

static const struct btmrvl_sdio_card_reg btmrvl_reg_8887 = {
	.cfg = 0x00,
	.host_int_mask = 0x08,
	.host_intstatus = 0x0C,
	.card_status = 0x5C,
	.sq_read_base_addr_a0 = 0x6C,
	.sq_read_base_addr_a1 = 0x6D,
	.card_revision = 0xC8,
	.card_fw_status0 = 0x88,
	.card_fw_status1 = 0x89,
	.card_rx_len = 0x8A,
	.card_rx_unit = 0x8B,
	.io_port_0 = 0xE4,
	.io_port_1 = 0xE5,
	.io_port_2 = 0xE6,
	.int_read_to_clear = true,
	.host_int_rsr = 0x04,
	.card_misc_cfg = 0xD8,
};

static const struct btmrvl_sdio_card_reg btmrvl_reg_8897 = {
	.cfg = 0x00,
	.host_int_mask = 0x02,
	.host_intstatus = 0x03,
	.card_status = 0x50,
	.sq_read_base_addr_a0 = 0x60,
	.sq_read_base_addr_a1 = 0x61,
	.card_revision = 0xbc,
	.card_fw_status0 = 0xc0,
	.card_fw_status1 = 0xc1,
	.card_rx_len = 0xc2,
	.card_rx_unit = 0xc3,
	.io_port_0 = 0xd8,
	.io_port_1 = 0xd9,
	.io_port_2 = 0xda,
	.int_read_to_clear = true,
	.host_int_rsr = 0x01,
	.card_misc_cfg = 0xcc,
	.fw_dump_ctrl = 0xe2,
	.fw_dump_start = 0xe3,
	.fw_dump_end = 0xea,
};

static const struct btmrvl_sdio_card_reg btmrvl_reg_89xx = {
	.cfg = 0x00,
	.host_int_mask = 0x08,
	.host_intstatus = 0x0c,
	.card_status = 0x5c,
	.sq_read_base_addr_a0 = 0xf8,
	.sq_read_base_addr_a1 = 0xf9,
	.card_revision = 0xc8,
	.card_fw_status0 = 0xe8,
	.card_fw_status1 = 0xe9,
	.card_rx_len = 0xea,
	.card_rx_unit = 0xeb,
	.io_port_0 = 0xe4,
	.io_port_1 = 0xe5,
	.io_port_2 = 0xe6,
	.int_read_to_clear = true,
	.host_int_rsr = 0x04,
	.card_misc_cfg = 0xd8,
	.fw_dump_ctrl = 0xf0,
	.fw_dump_start = 0xf1,
	.fw_dump_end = 0xf8,
};

static const struct btmrvl_sdio_device btmrvl_sdio_sd8688 = {
	.helper		= "mrvl/sd8688_helper.bin",
	.firmware	= "mrvl/sd8688.bin",
	.reg		= &btmrvl_reg_8688,
	.support_pscan_win_report = false,
	.sd_blksz_fw_dl	= 64,
	.supports_fw_dump = false,
};

static const struct btmrvl_sdio_device btmrvl_sdio_sd8787 = {
	.helper		= NULL,
	.firmware	= "mrvl/sd8787_uapsta.bin",
	.reg		= &btmrvl_reg_87xx,
	.support_pscan_win_report = false,
	.sd_blksz_fw_dl	= 256,
	.supports_fw_dump = false,
};

static const struct btmrvl_sdio_device btmrvl_sdio_sd8797 = {
	.helper		= NULL,
	.firmware	= "mrvl/sd8797_uapsta.bin",
	.reg		= &btmrvl_reg_87xx,
	.support_pscan_win_report = false,
	.sd_blksz_fw_dl	= 256,
	.supports_fw_dump = false,
};

static const struct btmrvl_sdio_device btmrvl_sdio_sd8887 = {
	.helper		= NULL,
	.firmware	= "mrvl/sd8887_uapsta.bin",
	.reg		= &btmrvl_reg_8887,
	.support_pscan_win_report = true,
	.sd_blksz_fw_dl	= 256,
	.supports_fw_dump = false,
};

static const struct btmrvl_sdio_device btmrvl_sdio_sd8897 = {
	.helper		= NULL,
	.firmware	= "mrvl/sd8897_uapsta.bin",
	.reg		= &btmrvl_reg_8897,
	.support_pscan_win_report = true,
	.sd_blksz_fw_dl	= 256,
	.supports_fw_dump = true,
};

static const struct btmrvl_sdio_device btmrvl_sdio_sd8977 = {
	.helper         = NULL,
	.firmware       = "mrvl/sdsd8977_combo_v2.bin",
	.reg            = &btmrvl_reg_89xx,
	.support_pscan_win_report = true,
	.sd_blksz_fw_dl = 256,
	.supports_fw_dump = true,
};

static const struct btmrvl_sdio_device btmrvl_sdio_sd8987 = {
	.helper		= NULL,
	.firmware	= "mrvl/sd8987_uapsta.bin",
	.reg		= &btmrvl_reg_89xx,
	.support_pscan_win_report = true,
	.sd_blksz_fw_dl	= 256,
	.supports_fw_dump = true,
};

static const struct btmrvl_sdio_device btmrvl_sdio_sd8997 = {
	.helper         = NULL,
	.firmware       = "mrvl/sdsd8997_combo_v4.bin",
	.reg            = &btmrvl_reg_89xx,
	.support_pscan_win_report = true,
	.sd_blksz_fw_dl = 256,
	.supports_fw_dump = true,
};

static const struct sdio_device_id btmrvl_sdio_ids[] = {
	/* Marvell SD8688 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8688_BT),
			.driver_data = (unsigned long)&btmrvl_sdio_sd8688 },
	/* Marvell SD8787 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8787_BT),
			.driver_data = (unsigned long)&btmrvl_sdio_sd8787 },
	/* Marvell SD8787 Bluetooth AMP device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8787_BT_AMP),
			.driver_data = (unsigned long)&btmrvl_sdio_sd8787 },
	/* Marvell SD8797 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8797_BT),
			.driver_data = (unsigned long)&btmrvl_sdio_sd8797 },
	/* Marvell SD8887 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8887_BT),
			.driver_data = (unsigned long)&btmrvl_sdio_sd8887 },
	/* Marvell SD8897 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8897_BT),
			.driver_data = (unsigned long)&btmrvl_sdio_sd8897 },
	/* Marvell SD8977 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8977_BT),
			.driver_data = (unsigned long)&btmrvl_sdio_sd8977 },
	/* Marvell SD8987 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8987_BT),
			.driver_data = (unsigned long)&btmrvl_sdio_sd8987 },
	/* Marvell SD8997 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8997_BT),
			.driver_data = (unsigned long)&btmrvl_sdio_sd8997 },

	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(sdio, btmrvl_sdio_ids);

static int btmrvl_sdio_get_rx_unit(struct btmrvl_sdio_card *card)
{
	u8 reg;
	int ret;

	reg = sdio_readb(card->func, card->reg->card_rx_unit, &ret);
	if (!ret)
		card->rx_unit = reg;

	return ret;
}

static int btmrvl_sdio_read_fw_status(struct btmrvl_sdio_card *card, u16 *dat)
{
	u8 fws0, fws1;
	int ret;

	*dat = 0;

	fws0 = sdio_readb(card->func, card->reg->card_fw_status0, &ret);
	if (ret)
		return -EIO;

	fws1 = sdio_readb(card->func, card->reg->card_fw_status1, &ret);
	if (ret)
		return -EIO;

	*dat = (((u16) fws1) << 8) | fws0;

	return 0;
}

static int btmrvl_sdio_read_rx_len(struct btmrvl_sdio_card *card, u16 *dat)
{
	u8 reg;
	int ret;

	reg = sdio_readb(card->func, card->reg->card_rx_len, &ret);
	if (!ret)
		*dat = (u16) reg << card->rx_unit;

	return ret;
}

static int btmrvl_sdio_enable_host_int_mask(struct btmrvl_sdio_card *card,
								u8 mask)
{
	int ret;

	sdio_writeb(card->func, mask, card->reg->host_int_mask, &ret);
	if (ret) {
		BT_ERR("Unable to enable the host interrupt!");
		ret = -EIO;
	}

	return ret;
}

static int btmrvl_sdio_disable_host_int_mask(struct btmrvl_sdio_card *card,
								u8 mask)
{
	u8 host_int_mask;
	int ret;

	host_int_mask = sdio_readb(card->func, card->reg->host_int_mask, &ret);
	if (ret)
		return -EIO;

	host_int_mask &= ~mask;

	sdio_writeb(card->func, host_int_mask, card->reg->host_int_mask, &ret);
	if (ret < 0) {
		BT_ERR("Unable to disable the host interrupt!");
		return -EIO;
	}

	return 0;
}

static int btmrvl_sdio_poll_card_status(struct btmrvl_sdio_card *card, u8 bits)
{
	unsigned int tries;
	u8 status;
	int ret;

	for (tries = 0; tries < MAX_POLL_TRIES * 1000; tries++) {
		status = sdio_readb(card->func, card->reg->card_status,	&ret);
		if (ret)
			goto failed;
		if ((status & bits) == bits)
			return ret;

		udelay(1);
	}

	ret = -ETIMEDOUT;

failed:
	BT_ERR("FAILED! ret=%d", ret);

	return ret;
}

static int btmrvl_sdio_verify_fw_download(struct btmrvl_sdio_card *card,
								int pollnum)
{
	u16 firmwarestat;
	int tries, ret;

	 /* Wait for firmware to become ready */
	for (tries = 0; tries < pollnum; tries++) {
		sdio_claim_host(card->func);
		ret = btmrvl_sdio_read_fw_status(card, &firmwarestat);
		sdio_release_host(card->func);
		if (ret < 0)
			continue;

		if (firmwarestat == FIRMWARE_READY)
			return 0;

		msleep(100);
	}

	return -ETIMEDOUT;
}

static int btmrvl_sdio_download_helper(struct btmrvl_sdio_card *card)
{
	const struct firmware *fw_helper = NULL;
	const u8 *helper = NULL;
	int ret;
	void *tmphlprbuf = NULL;
	int tmphlprbufsz, hlprblknow, helperlen;
	u8 *helperbuf;
	u32 tx_len;

	ret = request_firmware(&fw_helper, card->helper,
						&card->func->dev);
	if ((ret < 0) || !fw_helper) {
		BT_ERR("request_firmware(helper) failed, error code = %d",
									ret);
		ret = -ENOENT;
		goto done;
	}

	helper = fw_helper->data;
	helperlen = fw_helper->size;

	BT_DBG("Downloading helper image (%d bytes), block size %d bytes",
						helperlen, SDIO_BLOCK_SIZE);

	tmphlprbufsz = ALIGN_SZ(BTM_UPLD_SIZE, BTSDIO_DMA_ALIGN);

	tmphlprbuf = kzalloc(tmphlprbufsz, GFP_KERNEL);
	if (!tmphlprbuf) {
		BT_ERR("Unable to allocate buffer for helper."
			" Terminating download");
		ret = -ENOMEM;
		goto done;
	}

	helperbuf = (u8 *) ALIGN_ADDR(tmphlprbuf, BTSDIO_DMA_ALIGN);

	/* Perform helper data transfer */
	tx_len = (FIRMWARE_TRANSFER_NBLOCK * SDIO_BLOCK_SIZE)
			- SDIO_HEADER_LEN;
	hlprblknow = 0;

	do {
		ret = btmrvl_sdio_poll_card_status(card,
					    CARD_IO_READY | DN_LD_CARD_RDY);
		if (ret < 0) {
			BT_ERR("Helper download poll status timeout @ %d",
				hlprblknow);
			goto done;
		}

		/* Check if there is more data? */
		if (hlprblknow >= helperlen)
			break;

		if (helperlen - hlprblknow < tx_len)
			tx_len = helperlen - hlprblknow;

		/* Little-endian */
		helperbuf[0] = ((tx_len & 0x000000ff) >> 0);
		helperbuf[1] = ((tx_len & 0x0000ff00) >> 8);
		helperbuf[2] = ((tx_len & 0x00ff0000) >> 16);
		helperbuf[3] = ((tx_len & 0xff000000) >> 24);

		memcpy(&helperbuf[SDIO_HEADER_LEN], &helper[hlprblknow],
				tx_len);

		/* Now send the data */
		ret = sdio_writesb(card->func, card->ioport, helperbuf,
				FIRMWARE_TRANSFER_NBLOCK * SDIO_BLOCK_SIZE);
		if (ret < 0) {
			BT_ERR("IO error during helper download @ %d",
				hlprblknow);
			goto done;
		}

		hlprblknow += tx_len;
	} while (true);

	BT_DBG("Transferring helper image EOF block");

	memset(helperbuf, 0x0, SDIO_BLOCK_SIZE);

	ret = sdio_writesb(card->func, card->ioport, helperbuf,
							SDIO_BLOCK_SIZE);
	if (ret < 0) {
		BT_ERR("IO error in writing helper image EOF block");
		goto done;
	}

	ret = 0;

done:
	kfree(tmphlprbuf);
	release_firmware(fw_helper);
	return ret;
}

static int btmrvl_sdio_download_fw_w_helper(struct btmrvl_sdio_card *card)
{
	const struct firmware *fw_firmware = NULL;
	const u8 *firmware = NULL;
	int firmwarelen, tmpfwbufsz, ret;
	unsigned int tries, offset;
	u8 base0, base1;
	void *tmpfwbuf = NULL;
	u8 *fwbuf;
	u16 len, blksz_dl = card->sd_blksz_fw_dl;
	int txlen = 0, tx_blocks = 0, count = 0;

	ret = request_firmware(&fw_firmware, card->firmware,
							&card->func->dev);
	if ((ret < 0) || !fw_firmware) {
		BT_ERR("request_firmware(firmware) failed, error code = %d",
									ret);
		ret = -ENOENT;
		goto done;
	}

	firmware = fw_firmware->data;
	firmwarelen = fw_firmware->size;

	BT_DBG("Downloading FW image (%d bytes)", firmwarelen);

	tmpfwbufsz = ALIGN_SZ(BTM_UPLD_SIZE, BTSDIO_DMA_ALIGN);
	tmpfwbuf = kzalloc(tmpfwbufsz, GFP_KERNEL);
	if (!tmpfwbuf) {
		BT_ERR("Unable to allocate buffer for firmware."
		       " Terminating download");
		ret = -ENOMEM;
		goto done;
	}

	/* Ensure aligned firmware buffer */
	fwbuf = (u8 *) ALIGN_ADDR(tmpfwbuf, BTSDIO_DMA_ALIGN);

	/* Perform firmware data transfer */
	offset = 0;
	do {
		ret = btmrvl_sdio_poll_card_status(card,
					CARD_IO_READY | DN_LD_CARD_RDY);
		if (ret < 0) {
			BT_ERR("FW download with helper poll status"
						" timeout @ %d", offset);
			goto done;
		}

		/* Check if there is more data ? */
		if (offset >= firmwarelen)
			break;

		for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
			base0 = sdio_readb(card->func,
					card->reg->sq_read_base_addr_a0, &ret);
			if (ret) {
				BT_ERR("BASE0 register read failed:"
					" base0 = 0x%04X(%d)."
					" Terminating download",
					base0, base0);
				ret = -EIO;
				goto done;
			}
			base1 = sdio_readb(card->func,
					card->reg->sq_read_base_addr_a1, &ret);
			if (ret) {
				BT_ERR("BASE1 register read failed:"
					" base1 = 0x%04X(%d)."
					" Terminating download",
					base1, base1);
				ret = -EIO;
				goto done;
			}

			len = (((u16) base1) << 8) | base0;
			if (len)
				break;

			udelay(10);
		}

		if (!len)
			break;
		else if (len > BTM_UPLD_SIZE) {
			BT_ERR("FW download failure @%d, invalid length %d",
								offset, len);
			ret = -EINVAL;
			goto done;
		}

		txlen = len;

		if (len & BIT(0)) {
			count++;
			if (count > MAX_WRITE_IOMEM_RETRY) {
				BT_ERR("FW download failure @%d, "
					"over max retry count", offset);
				ret = -EIO;
				goto done;
			}
			BT_ERR("FW CRC error indicated by the helper: "
				"len = 0x%04X, txlen = %d", len, txlen);
			len &= ~BIT(0);
			/* Set txlen to 0 so as to resend from same offset */
			txlen = 0;
		} else {
			count = 0;

			/* Last block ? */
			if (firmwarelen - offset < txlen)
				txlen = firmwarelen - offset;

			tx_blocks = DIV_ROUND_UP(txlen, blksz_dl);

			memcpy(fwbuf, &firmware[offset], txlen);
		}

		ret = sdio_writesb(card->func, card->ioport, fwbuf,
						tx_blocks * blksz_dl);

		if (ret < 0) {
			BT_ERR("FW download, writesb(%d) failed @%d",
							count, offset);
			sdio_writeb(card->func, HOST_CMD53_FIN,
						card->reg->cfg, &ret);
			if (ret)
				BT_ERR("writeb failed (CFG)");
		}

		offset += txlen;
	} while (true);

	BT_INFO("FW download over, size %d bytes", offset);

	ret = 0;

done:
	kfree(tmpfwbuf);
	release_firmware(fw_firmware);
	return ret;
}

static int btmrvl_sdio_card_to_host(struct btmrvl_private *priv)
{
	u16 buf_len = 0;
	int ret, num_blocks, blksz;
	struct sk_buff *skb = NULL;
	u32 type;
	u8 *payload;
	struct hci_dev *hdev = priv->btmrvl_dev.hcidev;
	struct btmrvl_sdio_card *card = priv->btmrvl_dev.card;

	if (!card || !card->func) {
		BT_ERR("card or function is NULL!");
		ret = -EINVAL;
		goto exit;
	}

	/* Read the length of data to be transferred */
	ret = btmrvl_sdio_read_rx_len(card, &buf_len);
	if (ret < 0) {
		BT_ERR("read rx_len failed");
		ret = -EIO;
		goto exit;
	}

	blksz = SDIO_BLOCK_SIZE;
	num_blocks = DIV_ROUND_UP(buf_len, blksz);

	if (buf_len <= SDIO_HEADER_LEN
	    || (num_blocks * blksz) > ALLOC_BUF_SIZE) {
		BT_ERR("invalid packet length: %d", buf_len);
		ret = -EINVAL;
		goto exit;
	}

	/* Allocate buffer */
	skb = bt_skb_alloc(num_blocks * blksz + BTSDIO_DMA_ALIGN, GFP_KERNEL);
	if (!skb) {
		BT_ERR("No free skb");
		ret = -ENOMEM;
		goto exit;
	}

	if ((unsigned long) skb->data & (BTSDIO_DMA_ALIGN - 1)) {
		skb_put(skb, (unsigned long) skb->data &
					(BTSDIO_DMA_ALIGN - 1));
		skb_pull(skb, (unsigned long) skb->data &
					(BTSDIO_DMA_ALIGN - 1));
	}

	payload = skb->data;

	ret = sdio_readsb(card->func, payload, card->ioport,
			  num_blocks * blksz);
	if (ret < 0) {
		BT_ERR("readsb failed: %d", ret);
		ret = -EIO;
		goto exit;
	}

	/* This is SDIO specific header length: byte[2][1][0], type: byte[3]
	 * (HCI_COMMAND = 1, ACL_DATA = 2, SCO_DATA = 3, 0xFE = Vendor)
	 */

	buf_len = payload[0];
	buf_len |= payload[1] << 8;
	buf_len |= payload[2] << 16;

	if (buf_len > blksz * num_blocks) {
		BT_ERR("Skip incorrect packet: hdrlen %d buffer %d",
		       buf_len, blksz * num_blocks);
		ret = -EIO;
		goto exit;
	}

	type = payload[3];

	switch (type) {
	case HCI_ACLDATA_PKT:
	case HCI_SCODATA_PKT:
	case HCI_EVENT_PKT:
		hci_skb_pkt_type(skb) = type;
		skb_put(skb, buf_len);
		skb_pull(skb, SDIO_HEADER_LEN);

		if (type == HCI_EVENT_PKT) {
			if (btmrvl_check_evtpkt(priv, skb))
				hci_recv_frame(hdev, skb);
		} else {
			hci_recv_frame(hdev, skb);
		}

		hdev->stat.byte_rx += buf_len;
		break;

	case MRVL_VENDOR_PKT:
		hci_skb_pkt_type(skb) = HCI_VENDOR_PKT;
		skb_put(skb, buf_len);
		skb_pull(skb, SDIO_HEADER_LEN);

		if (btmrvl_process_event(priv, skb))
			hci_recv_frame(hdev, skb);

		hdev->stat.byte_rx += buf_len;
		break;

	default:
		BT_ERR("Unknown packet type:%d", type);
		BT_ERR("hex: %*ph", blksz * num_blocks, payload);

		kfree_skb(skb);
		skb = NULL;
		break;
	}

exit:
	if (ret) {
		hdev->stat.err_rx++;
		kfree_skb(skb);
	}

	return ret;
}

static int btmrvl_sdio_process_int_status(struct btmrvl_private *priv)
{
	ulong flags;
	u8 ireg;
	struct btmrvl_sdio_card *card = priv->btmrvl_dev.card;

	spin_lock_irqsave(&priv->driver_lock, flags);
	ireg = sdio_ireg;
	sdio_ireg = 0;
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	sdio_claim_host(card->func);
	if (ireg & DN_LD_HOST_INT_STATUS) {
		if (priv->btmrvl_dev.tx_dnld_rdy)
			BT_DBG("tx_done already received: "
				" int_status=0x%x", ireg);
		else
			priv->btmrvl_dev.tx_dnld_rdy = true;
	}

	if (ireg & UP_LD_HOST_INT_STATUS)
		btmrvl_sdio_card_to_host(priv);

	sdio_release_host(card->func);

	return 0;
}

static int btmrvl_sdio_read_to_clear(struct btmrvl_sdio_card *card, u8 *ireg)
{
	struct btmrvl_adapter *adapter = card->priv->adapter;
	int ret;

	ret = sdio_readsb(card->func, adapter->hw_regs, 0, SDIO_BLOCK_SIZE);
	if (ret) {
		BT_ERR("sdio_readsb: read int hw_regs failed: %d", ret);
		return ret;
	}

	*ireg = adapter->hw_regs[card->reg->host_intstatus];
	BT_DBG("hw_regs[%#x]=%#x", card->reg->host_intstatus, *ireg);

	return 0;
}

static int btmrvl_sdio_write_to_clear(struct btmrvl_sdio_card *card, u8 *ireg)
{
	int ret;

	*ireg = sdio_readb(card->func, card->reg->host_intstatus, &ret);
	if (ret) {
		BT_ERR("sdio_readb: read int status failed: %d", ret);
		return ret;
	}

	if (*ireg) {
		/*
		 * DN_LD_HOST_INT_STATUS and/or UP_LD_HOST_INT_STATUS
		 * Clear the interrupt status register and re-enable the
		 * interrupt.
		 */
		BT_DBG("int_status = 0x%x", *ireg);

		sdio_writeb(card->func, ~(*ireg) & (DN_LD_HOST_INT_STATUS |
						    UP_LD_HOST_INT_STATUS),
			    card->reg->host_intstatus, &ret);
		if (ret) {
			BT_ERR("sdio_writeb: clear int status failed: %d", ret);
			return ret;
		}
	}

	return 0;
}

static void btmrvl_sdio_interrupt(struct sdio_func *func)
{
	struct btmrvl_private *priv;
	struct btmrvl_sdio_card *card;
	ulong flags;
	u8 ireg = 0;
	int ret;

	card = sdio_get_drvdata(func);
	if (!card || !card->priv) {
		BT_ERR("sbi_interrupt(%p) card or priv is NULL, card=%p",
		       func, card);
		return;
	}

	priv = card->priv;

	if (priv->surprise_removed)
		return;

	if (card->reg->int_read_to_clear)
		ret = btmrvl_sdio_read_to_clear(card, &ireg);
	else
		ret = btmrvl_sdio_write_to_clear(card, &ireg);

	if (ret)
		return;

	spin_lock_irqsave(&priv->driver_lock, flags);
	sdio_ireg |= ireg;
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	btmrvl_interrupt(priv);
}

static int btmrvl_sdio_register_dev(struct btmrvl_sdio_card *card)
{
	struct sdio_func *func;
	u8 reg;
	int ret;

	if (!card || !card->func) {
		BT_ERR("Error: card or function is NULL!");
		ret = -EINVAL;
		goto failed;
	}

	func = card->func;

	sdio_claim_host(func);

	ret = sdio_enable_func(func);
	if (ret) {
		BT_ERR("sdio_enable_func() failed: ret=%d", ret);
		ret = -EIO;
		goto release_host;
	}

	ret = sdio_claim_irq(func, btmrvl_sdio_interrupt);
	if (ret) {
		BT_ERR("sdio_claim_irq failed: ret=%d", ret);
		ret = -EIO;
		goto disable_func;
	}

	ret = sdio_set_block_size(card->func, SDIO_BLOCK_SIZE);
	if (ret) {
		BT_ERR("cannot set SDIO block size");
		ret = -EIO;
		goto release_irq;
	}

	reg = sdio_readb(func, card->reg->io_port_0, &ret);
	if (ret < 0) {
		ret = -EIO;
		goto release_irq;
	}

	card->ioport = reg;

	reg = sdio_readb(func, card->reg->io_port_1, &ret);
	if (ret < 0) {
		ret = -EIO;
		goto release_irq;
	}

	card->ioport |= (reg << 8);

	reg = sdio_readb(func, card->reg->io_port_2, &ret);
	if (ret < 0) {
		ret = -EIO;
		goto release_irq;
	}

	card->ioport |= (reg << 16);

	BT_DBG("SDIO FUNC%d IO port: 0x%x", func->num, card->ioport);

	if (card->reg->int_read_to_clear) {
		reg = sdio_readb(func, card->reg->host_int_rsr, &ret);
		if (ret < 0) {
			ret = -EIO;
			goto release_irq;
		}
		sdio_writeb(func, reg | 0x3f, card->reg->host_int_rsr, &ret);
		if (ret < 0) {
			ret = -EIO;
			goto release_irq;
		}

		reg = sdio_readb(func, card->reg->card_misc_cfg, &ret);
		if (ret < 0) {
			ret = -EIO;
			goto release_irq;
		}
		sdio_writeb(func, reg | 0x10, card->reg->card_misc_cfg, &ret);
		if (ret < 0) {
			ret = -EIO;
			goto release_irq;
		}
	}

	sdio_set_drvdata(func, card);

	sdio_release_host(func);

	return 0;

release_irq:
	sdio_release_irq(func);

disable_func:
	sdio_disable_func(func);

release_host:
	sdio_release_host(func);

failed:
	return ret;
}

static int btmrvl_sdio_unregister_dev(struct btmrvl_sdio_card *card)
{
	if (card && card->func) {
		sdio_claim_host(card->func);
		sdio_release_irq(card->func);
		sdio_disable_func(card->func);
		sdio_release_host(card->func);
		sdio_set_drvdata(card->func, NULL);
	}

	return 0;
}

static int btmrvl_sdio_enable_host_int(struct btmrvl_sdio_card *card)
{
	int ret;

	if (!card || !card->func)
		return -EINVAL;

	sdio_claim_host(card->func);

	ret = btmrvl_sdio_enable_host_int_mask(card, HIM_ENABLE);

	btmrvl_sdio_get_rx_unit(card);

	sdio_release_host(card->func);

	return ret;
}

static int btmrvl_sdio_disable_host_int(struct btmrvl_sdio_card *card)
{
	int ret;

	if (!card || !card->func)
		return -EINVAL;

	sdio_claim_host(card->func);

	ret = btmrvl_sdio_disable_host_int_mask(card, HIM_DISABLE);

	sdio_release_host(card->func);

	return ret;
}

static int btmrvl_sdio_host_to_card(struct btmrvl_private *priv,
				u8 *payload, u16 nb)
{
	struct btmrvl_sdio_card *card = priv->btmrvl_dev.card;
	int ret = 0;
	int blksz;
	int i = 0;
	u8 *buf = NULL;
	void *tmpbuf = NULL;
	int tmpbufsz;

	if (!card || !card->func) {
		BT_ERR("card or function is NULL!");
		return -EINVAL;
	}

	blksz = DIV_ROUND_UP(nb, SDIO_BLOCK_SIZE) * SDIO_BLOCK_SIZE;

	buf = payload;
	if ((unsigned long) payload & (BTSDIO_DMA_ALIGN - 1) ||
	    nb < blksz) {
		tmpbufsz = ALIGN_SZ(blksz, BTSDIO_DMA_ALIGN) +
			   BTSDIO_DMA_ALIGN;
		tmpbuf = kzalloc(tmpbufsz, GFP_KERNEL);
		if (!tmpbuf)
			return -ENOMEM;
		buf = (u8 *) ALIGN_ADDR(tmpbuf, BTSDIO_DMA_ALIGN);
		memcpy(buf, payload, nb);
	}

	sdio_claim_host(card->func);

	do {
		/* Transfer data to card */
		ret = sdio_writesb(card->func, card->ioport, buf,
				   blksz);
		if (ret < 0) {
			i++;
			BT_ERR("i=%d writesb failed: %d", i, ret);
			BT_ERR("hex: %*ph", nb, payload);
			ret = -EIO;
			if (i > MAX_WRITE_IOMEM_RETRY)
				goto exit;
		}
	} while (ret);

	priv->btmrvl_dev.tx_dnld_rdy = false;

exit:
	sdio_release_host(card->func);
	kfree(tmpbuf);

	return ret;
}

static int btmrvl_sdio_download_fw(struct btmrvl_sdio_card *card)
{
	int ret;
	u8 fws0;
	int pollnum = MAX_POLL_TRIES;

	if (!card || !card->func) {
		BT_ERR("card or function is NULL!");
		return -EINVAL;
	}

	if (!btmrvl_sdio_verify_fw_download(card, 1)) {
		BT_DBG("Firmware already downloaded!");
		return 0;
	}

	sdio_claim_host(card->func);

	/* Check if other function driver is downloading the firmware */
	fws0 = sdio_readb(card->func, card->reg->card_fw_status0, &ret);
	if (ret) {
		BT_ERR("Failed to read FW downloading status!");
		ret = -EIO;
		goto done;
	}
	if (fws0) {
		BT_DBG("BT not the winner (%#x). Skip FW downloading", fws0);

		/* Give other function more time to download the firmware */
		pollnum *= 10;
	} else {
		if (card->helper) {
			ret = btmrvl_sdio_download_helper(card);
			if (ret) {
				BT_ERR("Failed to download helper!");
				ret = -EIO;
				goto done;
			}
		}

		if (btmrvl_sdio_download_fw_w_helper(card)) {
			BT_ERR("Failed to download firmware!");
			ret = -EIO;
			goto done;
		}
	}

	/*
	 * winner or not, with this test the FW synchronizes when the
	 * module can continue its initialization
	 */
	if (btmrvl_sdio_verify_fw_download(card, pollnum)) {
		BT_ERR("FW failed to be active in time!");
		ret = -ETIMEDOUT;
		goto done;
	}

	sdio_release_host(card->func);

	return 0;

done:
	sdio_release_host(card->func);
	return ret;
}

static int btmrvl_sdio_wakeup_fw(struct btmrvl_private *priv)
{
	struct btmrvl_sdio_card *card = priv->btmrvl_dev.card;
	int ret = 0;

	if (!card || !card->func) {
		BT_ERR("card or function is NULL!");
		return -EINVAL;
	}

	sdio_claim_host(card->func);

	sdio_writeb(card->func, HOST_POWER_UP, card->reg->cfg, &ret);

	sdio_release_host(card->func);

	BT_DBG("wake up firmware");

	return ret;
}

static void btmrvl_sdio_dump_regs(struct btmrvl_private *priv)
{
	struct btmrvl_sdio_card *card = priv->btmrvl_dev.card;
	int ret = 0;
	unsigned int reg, reg_start, reg_end;
	char buf[256], *ptr;
	u8 loop, func, data;
	int MAX_LOOP = 2;

	btmrvl_sdio_wakeup_fw(priv);
	sdio_claim_host(card->func);

	for (loop = 0; loop < MAX_LOOP; loop++) {
		memset(buf, 0, sizeof(buf));
		ptr = buf;

		if (loop == 0) {
			/* Read the registers of SDIO function0 */
			func = loop;
			reg_start = 0;
			reg_end = 9;
		} else {
			func = 2;
			reg_start = 0;
			reg_end = 0x09;
		}

		ptr += sprintf(ptr, "SDIO Func%d (%#x-%#x): ",
			       func, reg_start, reg_end);
		for (reg = reg_start; reg <= reg_end; reg++) {
			if (func == 0)
				data = sdio_f0_readb(card->func, reg, &ret);
			else
				data = sdio_readb(card->func, reg, &ret);

			if (!ret) {
				ptr += sprintf(ptr, "%02x ", data);
			} else {
				ptr += sprintf(ptr, "ERR");
				break;
			}
		}

		BT_INFO("%s", buf);
	}

	sdio_release_host(card->func);
}

/* This function read/write firmware */
static enum
rdwr_status btmrvl_sdio_rdwr_firmware(struct btmrvl_private *priv,
				      u8 doneflag)
{
	struct btmrvl_sdio_card *card = priv->btmrvl_dev.card;
	int ret, tries;
	u8 ctrl_data = 0;

	sdio_writeb(card->func, FW_DUMP_HOST_READY, card->reg->fw_dump_ctrl,
		    &ret);

	if (ret) {
		BT_ERR("SDIO write err");
		return RDWR_STATUS_FAILURE;
	}

	for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
		ctrl_data = sdio_readb(card->func, card->reg->fw_dump_ctrl,
				       &ret);

		if (ret) {
			BT_ERR("SDIO read err");
			return RDWR_STATUS_FAILURE;
		}

		if (ctrl_data == FW_DUMP_DONE)
			break;
		if (doneflag && ctrl_data == doneflag)
			return RDWR_STATUS_DONE;
		if (ctrl_data != FW_DUMP_HOST_READY) {
			BT_INFO("The ctrl reg was changed, re-try again!");
			sdio_writeb(card->func, FW_DUMP_HOST_READY,
				    card->reg->fw_dump_ctrl, &ret);
			if (ret) {
				BT_ERR("SDIO write err");
				return RDWR_STATUS_FAILURE;
			}
		}
		usleep_range(100, 200);
	}

	if (ctrl_data == FW_DUMP_HOST_READY) {
		BT_ERR("Fail to pull ctrl_data");
		return RDWR_STATUS_FAILURE;
	}

	return RDWR_STATUS_SUCCESS;
}

/* This function dump sdio register and memory data */
static void btmrvl_sdio_coredump(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct btmrvl_sdio_card *card;
	struct btmrvl_private *priv;
	int ret = 0;
	unsigned int reg, reg_start, reg_end;
	enum rdwr_status stat;
	u8 *dbg_ptr, *end_ptr, *fw_dump_data, *fw_dump_ptr;
	u8 dump_num = 0, idx, i, read_reg, doneflag = 0;
	u32 memory_size, fw_dump_len = 0;

	card = sdio_get_drvdata(func);
	priv = card->priv;

	/* dump sdio register first */
	btmrvl_sdio_dump_regs(priv);

	if (!card->supports_fw_dump) {
		BT_ERR("Firmware dump not supported for this card!");
		return;
	}

	for (idx = 0; idx < ARRAY_SIZE(mem_type_mapping_tbl); idx++) {
		struct memory_type_mapping *entry = &mem_type_mapping_tbl[idx];

		if (entry->mem_ptr) {
			vfree(entry->mem_ptr);
			entry->mem_ptr = NULL;
		}
		entry->mem_size = 0;
	}

	btmrvl_sdio_wakeup_fw(priv);
	sdio_claim_host(card->func);

	BT_INFO("== btmrvl firmware dump start ==");

	stat = btmrvl_sdio_rdwr_firmware(priv, doneflag);
	if (stat == RDWR_STATUS_FAILURE)
		goto done;

	reg = card->reg->fw_dump_start;
	/* Read the number of the memories which will dump */
	dump_num = sdio_readb(card->func, reg, &ret);

	if (ret) {
		BT_ERR("SDIO read memory length err");
		goto done;
	}

	/* Read the length of every memory which will dump */
	for (idx = 0; idx < dump_num; idx++) {
		struct memory_type_mapping *entry = &mem_type_mapping_tbl[idx];

		stat = btmrvl_sdio_rdwr_firmware(priv, doneflag);
		if (stat == RDWR_STATUS_FAILURE)
			goto done;

		memory_size = 0;
		reg = card->reg->fw_dump_start;
		for (i = 0; i < 4; i++) {
			read_reg = sdio_readb(card->func, reg, &ret);
			if (ret) {
				BT_ERR("SDIO read err");
				goto done;
			}
			memory_size |= (read_reg << i*8);
			reg++;
		}

		if (memory_size == 0) {
			BT_INFO("Firmware dump finished!");
			sdio_writeb(card->func, FW_DUMP_READ_DONE,
				    card->reg->fw_dump_ctrl, &ret);
			if (ret) {
				BT_ERR("SDIO Write MEMDUMP_FINISH ERR");
				goto done;
			}
			break;
		}

		BT_INFO("%s_SIZE=0x%x", entry->mem_name, memory_size);
		entry->mem_ptr = vzalloc(memory_size + 1);
		entry->mem_size = memory_size;
		if (!entry->mem_ptr) {
			BT_ERR("Vzalloc %s failed", entry->mem_name);
			goto done;
		}

		fw_dump_len += (strlen("========Start dump ") +
				strlen(entry->mem_name) +
				strlen("========\n") +
				(memory_size + 1) +
				strlen("\n========End dump========\n"));

		dbg_ptr = entry->mem_ptr;
		end_ptr = dbg_ptr + memory_size;

		doneflag = entry->done_flag;
		BT_INFO("Start %s output, please wait...",
			entry->mem_name);

		do {
			stat = btmrvl_sdio_rdwr_firmware(priv, doneflag);
			if (stat == RDWR_STATUS_FAILURE)
				goto done;

			reg_start = card->reg->fw_dump_start;
			reg_end = card->reg->fw_dump_end;
			for (reg = reg_start; reg <= reg_end; reg++) {
				*dbg_ptr = sdio_readb(card->func, reg, &ret);
				if (ret) {
					BT_ERR("SDIO read err");
					goto done;
				}
				if (dbg_ptr < end_ptr)
					dbg_ptr++;
				else
					BT_ERR("Allocated buffer not enough");
			}

			if (stat != RDWR_STATUS_DONE) {
				continue;
			} else {
				BT_INFO("%s done: size=0x%tx",
					entry->mem_name,
					dbg_ptr - entry->mem_ptr);
				break;
			}
		} while (1);
	}

	BT_INFO("== btmrvl firmware dump end ==");

done:
	sdio_release_host(card->func);

	if (fw_dump_len == 0)
		return;

	fw_dump_data = vzalloc(fw_dump_len+1);
	if (!fw_dump_data) {
		BT_ERR("Vzalloc fw_dump_data fail!");
		return;
	}
	fw_dump_ptr = fw_dump_data;

	/* Dump all the memory data into single file, a userspace script will
	 * be used to split all the memory data to multiple files
	 */
	BT_INFO("== btmrvl firmware dump to /sys/class/devcoredump start");
	for (idx = 0; idx < dump_num; idx++) {
		struct memory_type_mapping *entry = &mem_type_mapping_tbl[idx];

		if (entry->mem_ptr) {
			strcpy(fw_dump_ptr, "========Start dump ");
			fw_dump_ptr += strlen("========Start dump ");

			strcpy(fw_dump_ptr, entry->mem_name);
			fw_dump_ptr += strlen(entry->mem_name);

			strcpy(fw_dump_ptr, "========\n");
			fw_dump_ptr += strlen("========\n");

			memcpy(fw_dump_ptr, entry->mem_ptr, entry->mem_size);
			fw_dump_ptr += entry->mem_size;

			strcpy(fw_dump_ptr, "\n========End dump========\n");
			fw_dump_ptr += strlen("\n========End dump========\n");

			vfree(mem_type_mapping_tbl[idx].mem_ptr);
			mem_type_mapping_tbl[idx].mem_ptr = NULL;
		}
	}

	/* fw_dump_data will be free in device coredump release function
	 * after 5 min
	 */
	dev_coredumpv(&card->func->dev, fw_dump_data, fw_dump_len, GFP_KERNEL);
	BT_INFO("== btmrvl firmware dump to /sys/class/devcoredump end");
}

static int btmrvl_sdio_probe(struct sdio_func *func,
					const struct sdio_device_id *id)
{
	int ret = 0;
	struct btmrvl_private *priv = NULL;
	struct btmrvl_sdio_card *card = NULL;

	BT_INFO("vendor=0x%x, device=0x%x, class=%d, fn=%d",
			id->vendor, id->device, id->class, func->num);

	card = devm_kzalloc(&func->dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->func = func;

	if (id->driver_data) {
		struct btmrvl_sdio_device *data = (void *) id->driver_data;
		card->helper = data->helper;
		card->firmware = data->firmware;
		card->reg = data->reg;
		card->sd_blksz_fw_dl = data->sd_blksz_fw_dl;
		card->support_pscan_win_report = data->support_pscan_win_report;
		card->supports_fw_dump = data->supports_fw_dump;
	}

	if (btmrvl_sdio_register_dev(card) < 0) {
		BT_ERR("Failed to register BT device!");
		return -ENODEV;
	}

	/* Disable the interrupts on the card */
	btmrvl_sdio_disable_host_int(card);

	if (btmrvl_sdio_download_fw(card)) {
		BT_ERR("Downloading firmware failed!");
		ret = -ENODEV;
		goto unreg_dev;
	}

	btmrvl_sdio_enable_host_int(card);

	/* Device tree node parsing and platform specific configuration*/
	btmrvl_sdio_probe_of(&func->dev, card);

	priv = btmrvl_add_card(card);
	if (!priv) {
		BT_ERR("Initializing card failed!");
		ret = -ENODEV;
		goto disable_host_int;
	}

	card->priv = priv;

	/* Initialize the interface specific function pointers */
	priv->hw_host_to_card = btmrvl_sdio_host_to_card;
	priv->hw_wakeup_firmware = btmrvl_sdio_wakeup_fw;
	priv->hw_process_int_status = btmrvl_sdio_process_int_status;

	if (btmrvl_register_hdev(priv)) {
		BT_ERR("Register hdev failed!");
		ret = -ENODEV;
		goto disable_host_int;
	}

	return 0;

disable_host_int:
	btmrvl_sdio_disable_host_int(card);
unreg_dev:
	btmrvl_sdio_unregister_dev(card);
	return ret;
}

static void btmrvl_sdio_remove(struct sdio_func *func)
{
	struct btmrvl_sdio_card *card;

	if (func) {
		card = sdio_get_drvdata(func);
		if (card) {
			/* Send SHUTDOWN command & disable interrupt
			 * if user removes the module.
			 */
			if (user_rmmod) {
				btmrvl_send_module_cfg_cmd(card->priv,
							MODULE_SHUTDOWN_REQ);
				btmrvl_sdio_disable_host_int(card);
			}

			BT_DBG("unregister dev");
			card->priv->surprise_removed = true;
			btmrvl_sdio_unregister_dev(card);
			btmrvl_remove_card(card->priv);
		}
	}
}

static int btmrvl_sdio_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct btmrvl_sdio_card *card;
	struct btmrvl_private *priv;
	mmc_pm_flag_t pm_flags;
	struct hci_dev *hcidev;

	if (func) {
		pm_flags = sdio_get_host_pm_caps(func);
		BT_DBG("%s: suspend: PM flags = 0x%x", sdio_func_id(func),
		       pm_flags);
		if (!(pm_flags & MMC_PM_KEEP_POWER)) {
			BT_ERR("%s: cannot remain alive while suspended",
			       sdio_func_id(func));
			return -ENOSYS;
		}
		card = sdio_get_drvdata(func);
		if (!card || !card->priv) {
			BT_ERR("card or priv structure is not valid");
			return 0;
		}
	} else {
		BT_ERR("sdio_func is not specified");
		return 0;
	}

	/* Enable platform specific wakeup interrupt */
	if (card->plt_wake_cfg && card->plt_wake_cfg->irq_bt >= 0 &&
	    device_may_wakeup(dev)) {
		card->plt_wake_cfg->wake_by_bt = false;
		enable_irq(card->plt_wake_cfg->irq_bt);
		enable_irq_wake(card->plt_wake_cfg->irq_bt);
	}

	priv = card->priv;
	priv->adapter->is_suspending = true;
	hcidev = priv->btmrvl_dev.hcidev;
	BT_DBG("%s: SDIO suspend", hcidev->name);
	hci_suspend_dev(hcidev);

	if (priv->adapter->hs_state != HS_ACTIVATED) {
		if (btmrvl_enable_hs(priv)) {
			BT_ERR("HS not activated, suspend failed!");
			/* Disable platform specific wakeup interrupt */
			if (card->plt_wake_cfg &&
			    card->plt_wake_cfg->irq_bt >= 0 &&
			    device_may_wakeup(dev)) {
				disable_irq_wake(card->plt_wake_cfg->irq_bt);
				disable_irq(card->plt_wake_cfg->irq_bt);
			}

			priv->adapter->is_suspending = false;
			return -EBUSY;
		}
	}

	priv->adapter->is_suspending = false;
	priv->adapter->is_suspended = true;

	/* We will keep the power when hs enabled successfully */
	if (priv->adapter->hs_state == HS_ACTIVATED) {
		BT_DBG("suspend with MMC_PM_KEEP_POWER");
		return sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
	}

	BT_DBG("suspend without MMC_PM_KEEP_POWER");
	return 0;
}

static int btmrvl_sdio_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct btmrvl_sdio_card *card;
	struct btmrvl_private *priv;
	mmc_pm_flag_t pm_flags;
	struct hci_dev *hcidev;

	if (func) {
		pm_flags = sdio_get_host_pm_caps(func);
		BT_DBG("%s: resume: PM flags = 0x%x", sdio_func_id(func),
		       pm_flags);
		card = sdio_get_drvdata(func);
		if (!card || !card->priv) {
			BT_ERR("card or priv structure is not valid");
			return 0;
		}
	} else {
		BT_ERR("sdio_func is not specified");
		return 0;
	}
	priv = card->priv;

	if (!priv->adapter->is_suspended) {
		BT_DBG("device already resumed");
		return 0;
	}

	priv->hw_wakeup_firmware(priv);
	priv->adapter->hs_state = HS_DEACTIVATED;
	hcidev = priv->btmrvl_dev.hcidev;
	BT_DBG("%s: HS DEACTIVATED in resume!", hcidev->name);
	priv->adapter->is_suspended = false;
	BT_DBG("%s: SDIO resume", hcidev->name);
	hci_resume_dev(hcidev);

	/* Disable platform specific wakeup interrupt */
	if (card->plt_wake_cfg && card->plt_wake_cfg->irq_bt >= 0 &&
	    device_may_wakeup(dev)) {
		disable_irq_wake(card->plt_wake_cfg->irq_bt);
		disable_irq(card->plt_wake_cfg->irq_bt);
		if (card->plt_wake_cfg->wake_by_bt)
			/* Undo our disable, since interrupt handler already
			 * did this.
			 */
			enable_irq(card->plt_wake_cfg->irq_bt);
	}

	return 0;
}

static const struct dev_pm_ops btmrvl_sdio_pm_ops = {
	.suspend	= btmrvl_sdio_suspend,
	.resume		= btmrvl_sdio_resume,
};

static struct sdio_driver bt_mrvl_sdio = {
	.name		= "btmrvl_sdio",
	.id_table	= btmrvl_sdio_ids,
	.probe		= btmrvl_sdio_probe,
	.remove		= btmrvl_sdio_remove,
	.drv = {
		.owner = THIS_MODULE,
		.coredump = btmrvl_sdio_coredump,
		.pm = &btmrvl_sdio_pm_ops,
	}
};

static int __init btmrvl_sdio_init_module(void)
{
	if (sdio_register_driver(&bt_mrvl_sdio) != 0) {
		BT_ERR("SDIO Driver Registration Failed");
		return -ENODEV;
	}

	/* Clear the flag in case user removes the card. */
	user_rmmod = 0;

	return 0;
}

static void __exit btmrvl_sdio_exit_module(void)
{
	/* Set the flag as user is removing this module. */
	user_rmmod = 1;

	sdio_unregister_driver(&bt_mrvl_sdio);
}

module_init(btmrvl_sdio_init_module);
module_exit(btmrvl_sdio_exit_module);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell BT-over-SDIO driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL v2");
MODULE_FIRMWARE("mrvl/sd8688_helper.bin");
MODULE_FIRMWARE("mrvl/sd8688.bin");
MODULE_FIRMWARE("mrvl/sd8787_uapsta.bin");
MODULE_FIRMWARE("mrvl/sd8797_uapsta.bin");
MODULE_FIRMWARE("mrvl/sd8887_uapsta.bin");
MODULE_FIRMWARE("mrvl/sd8897_uapsta.bin");
MODULE_FIRMWARE("mrvl/sdsd8977_combo_v2.bin");
MODULE_FIRMWARE("mrvl/sd8987_uapsta.bin");
MODULE_FIRMWARE("mrvl/sdsd8997_combo_v4.bin");

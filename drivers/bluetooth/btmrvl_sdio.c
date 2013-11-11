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

#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btmrvl_drv.h"
#include "btmrvl_sdio.h"

#define VERSION "1.0"

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
};

static const struct btmrvl_sdio_card_reg btmrvl_reg_88xx = {
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
};

static const struct btmrvl_sdio_device btmrvl_sdio_sd8688 = {
	.helper		= "mrvl/sd8688_helper.bin",
	.firmware	= "mrvl/sd8688.bin",
	.reg		= &btmrvl_reg_8688,
	.sd_blksz_fw_dl	= 64,
};

static const struct btmrvl_sdio_device btmrvl_sdio_sd8787 = {
	.helper		= NULL,
	.firmware	= "mrvl/sd8787_uapsta.bin",
	.reg		= &btmrvl_reg_87xx,
	.sd_blksz_fw_dl	= 256,
};

static const struct btmrvl_sdio_device btmrvl_sdio_sd8797 = {
	.helper		= NULL,
	.firmware	= "mrvl/sd8797_uapsta.bin",
	.reg		= &btmrvl_reg_87xx,
	.sd_blksz_fw_dl	= 256,
};

static const struct btmrvl_sdio_device btmrvl_sdio_sd8897 = {
	.helper		= NULL,
	.firmware	= "mrvl/sd8897_uapsta.bin",
	.reg		= &btmrvl_reg_88xx,
	.sd_blksz_fw_dl	= 256,
};

static const struct sdio_device_id btmrvl_sdio_ids[] = {
	/* Marvell SD8688 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, 0x9105),
			.driver_data = (unsigned long) &btmrvl_sdio_sd8688 },
	/* Marvell SD8787 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, 0x911A),
			.driver_data = (unsigned long) &btmrvl_sdio_sd8787 },
	/* Marvell SD8787 Bluetooth AMP device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, 0x911B),
			.driver_data = (unsigned long) &btmrvl_sdio_sd8787 },
	/* Marvell SD8797 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, 0x912A),
			.driver_data = (unsigned long) &btmrvl_sdio_sd8797 },
	/* Marvell SD8897 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, 0x912E),
			.driver_data = (unsigned long) &btmrvl_sdio_sd8897 },

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

		msleep(10);
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

			tx_blocks = (txlen + blksz_dl - 1) / blksz_dl;

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

	BT_DBG("FW download over, size %d bytes", offset);

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
	u8 *payload = NULL;
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
	skb = bt_skb_alloc(num_blocks * blksz + BTSDIO_DMA_ALIGN, GFP_ATOMIC);
	if (skb == NULL) {
		BT_ERR("No free skb");
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
		bt_cb(skb)->pkt_type = type;
		skb->dev = (void *)hdev;
		skb_put(skb, buf_len);
		skb_pull(skb, SDIO_HEADER_LEN);

		if (type == HCI_EVENT_PKT) {
			if (btmrvl_check_evtpkt(priv, skb))
				hci_recv_frame(skb);
		} else {
			hci_recv_frame(skb);
		}

		hdev->stat.byte_rx += buf_len;
		break;

	case MRVL_VENDOR_PKT:
		bt_cb(skb)->pkt_type = HCI_VENDOR_PKT;
		skb->dev = (void *)hdev;
		skb_put(skb, buf_len);
		skb_pull(skb, SDIO_HEADER_LEN);

		if (btmrvl_process_event(priv, skb))
			hci_recv_frame(skb);

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

static void btmrvl_sdio_interrupt(struct sdio_func *func)
{
	struct btmrvl_private *priv;
	struct btmrvl_sdio_card *card;
	ulong flags;
	u8 ireg = 0;
	int ret;

	card = sdio_get_drvdata(func);
	if (!card || !card->priv) {
		BT_ERR("sbi_interrupt(%p) card or priv is "
				"NULL, card=%p\n", func, card);
		return;
	}

	priv = card->priv;

	ireg = sdio_readb(card->func, card->reg->host_intstatus, &ret);
	if (ret) {
		BT_ERR("sdio_readb: read int status register failed");
		return;
	}

	if (ireg != 0) {
		/*
		 * DN_LD_HOST_INT_STATUS and/or UP_LD_HOST_INT_STATUS
		 * Clear the interrupt status register and re-enable the
		 * interrupt.
		 */
		BT_DBG("ireg = 0x%x", ireg);

		sdio_writeb(card->func, ~(ireg) & (DN_LD_HOST_INT_STATUS |
					UP_LD_HOST_INT_STATUS),
				card->reg->host_intstatus, &ret);
		if (ret) {
			BT_ERR("sdio_writeb: clear int status register failed");
			return;
		}
	}

	spin_lock_irqsave(&priv->driver_lock, flags);
	sdio_ireg |= ireg;
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	btmrvl_interrupt(priv);
}

static int btmrvl_sdio_register_dev(struct btmrvl_sdio_card *card)
{
	struct sdio_func *func;
	u8 reg;
	int ret = 0;

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
	int buf_block_len;
	int blksz;
	int i = 0;
	u8 *buf = NULL;
	void *tmpbuf = NULL;
	int tmpbufsz;

	if (!card || !card->func) {
		BT_ERR("card or function is NULL!");
		return -EINVAL;
	}

	buf = payload;
	if ((unsigned long) payload & (BTSDIO_DMA_ALIGN - 1)) {
		tmpbufsz = ALIGN_SZ(nb, BTSDIO_DMA_ALIGN);
		tmpbuf = kzalloc(tmpbufsz, GFP_KERNEL);
		if (!tmpbuf)
			return -ENOMEM;
		buf = (u8 *) ALIGN_ADDR(tmpbuf, BTSDIO_DMA_ALIGN);
		memcpy(buf, payload, nb);
	}

	blksz = SDIO_BLOCK_SIZE;
	buf_block_len = (nb + blksz - 1) / blksz;

	sdio_claim_host(card->func);

	do {
		/* Transfer data to card */
		ret = sdio_writesb(card->func, card->ioport, buf,
				   buf_block_len * blksz);
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

	sdio_release_host(card->func);

	/*
	 * winner or not, with this test the FW synchronizes when the
	 * module can continue its initialization
	 */
	if (btmrvl_sdio_verify_fw_download(card, pollnum)) {
		BT_ERR("FW failed to be active in time!");
		return -ETIMEDOUT;
	}

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

	priv->btmrvl_dev.psmode = 1;
	btmrvl_enable_ps(priv);

	priv->btmrvl_dev.gpio_gap = 0xffff;
	btmrvl_send_hscfg_cmd(priv);

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
			BT_DBG("unregester dev");
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

	priv = card->priv;

	if (priv->adapter->hs_state != HS_ACTIVATED) {
		if (btmrvl_enable_hs(priv)) {
			BT_ERR("HS not actived, suspend failed!");
			return -EBUSY;
		}
	}
	hcidev = priv->btmrvl_dev.hcidev;
	BT_DBG("%s: SDIO suspend", hcidev->name);
	hci_suspend_dev(hcidev);
	skb_queue_purge(&priv->adapter->tx_queue);

	priv->adapter->is_suspended = true;

	/* We will keep the power when hs enabled successfully */
	if (priv->adapter->hs_state == HS_ACTIVATED) {
		BT_DBG("suspend with MMC_PM_KEEP_POWER");
		return sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
	} else {
		BT_DBG("suspend without MMC_PM_KEEP_POWER");
		return 0;
	}
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

	priv->adapter->is_suspended = false;
	hcidev = priv->btmrvl_dev.hcidev;
	BT_DBG("%s: SDIO resume", hcidev->name);
	hci_resume_dev(hcidev);
	priv->hw_wakeup_firmware(priv);
	priv->adapter->hs_state = HS_DEACTIVATED;
	BT_DBG("%s: HS DEACTIVATED in resume!", hcidev->name);

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
MODULE_FIRMWARE("mrvl/sd8897_uapsta.bin");

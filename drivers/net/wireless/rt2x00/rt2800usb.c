/*
	Copyright (C) 2004 - 2009 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2800usb
	Abstract: rt2800usb device specific routines.
	Supported chipsets: RT2800U.
 */

#include <linux/crc-ccitt.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "rt2x00.h"
#include "rt2x00usb.h"
#include "rt2800lib.h"
#include "rt2800.h"
#include "rt2800usb.h"

/*
 * Allow hardware encryption to be disabled.
 */
static int modparam_nohwcrypt = 1;
module_param_named(nohwcrypt, modparam_nohwcrypt, bool, S_IRUGO);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption.");

/*
 * Firmware functions
 */
static char *rt2800usb_get_firmware_name(struct rt2x00_dev *rt2x00dev)
{
	return FIRMWARE_RT2870;
}

static bool rt2800usb_check_crc(const u8 *data, const size_t len)
{
	u16 fw_crc;
	u16 crc;

	/*
	 * The last 2 bytes in the firmware array are the crc checksum itself,
	 * this means that we should never pass those 2 bytes to the crc
	 * algorithm.
	 */
	fw_crc = (data[len - 2] << 8 | data[len - 1]);

	/*
	 * Use the crc ccitt algorithm.
	 * This will return the same value as the legacy driver which
	 * used bit ordering reversion on the both the firmware bytes
	 * before input input as well as on the final output.
	 * Obviously using crc ccitt directly is much more efficient.
	 */
	crc = crc_ccitt(~0, data, len - 2);

	/*
	 * There is a small difference between the crc-itu-t + bitrev and
	 * the crc-ccitt crc calculation. In the latter method the 2 bytes
	 * will be swapped, use swab16 to convert the crc to the correct
	 * value.
	 */
	crc = swab16(crc);

	return fw_crc == crc;
}

static int rt2800usb_check_firmware(struct rt2x00_dev *rt2x00dev,
				    const u8 *data, const size_t len)
{
	u16 chipset = (rt2x00_rev(&rt2x00dev->chip) >> 16) & 0xffff;
	size_t offset = 0;

	/*
	 * Firmware files:
	 * There are 2 variations of the rt2870 firmware.
	 * a) size: 4kb
	 * b) size: 8kb
	 * Note that (b) contains 2 seperate firmware blobs of 4k
	 * within the file. The first blob is the same firmware as (a),
	 * but the second blob is for the additional chipsets.
	 */
	if (len != 4096 && len != 8192)
		return FW_BAD_LENGTH;

	/*
	 * Check if we need the upper 4kb firmware data or not.
	 */
	if ((len == 4096) &&
	    (chipset != 0x2860) &&
	    (chipset != 0x2872) &&
	    (chipset != 0x3070))
		return FW_BAD_VERSION;

	/*
	 * 8kb firmware files must be checked as if it were
	 * 2 seperate firmware files.
	 */
	while (offset < len) {
		if (!rt2800usb_check_crc(data + offset, 4096))
			return FW_BAD_CRC;

		offset += 4096;
	}

	return FW_OK;
}

static int rt2800usb_load_firmware(struct rt2x00_dev *rt2x00dev,
				   const u8 *data, const size_t len)
{
	unsigned int i;
	int status;
	u32 reg;
	u32 offset;
	u32 length;
	u16 chipset = (rt2x00_rev(&rt2x00dev->chip) >> 16) & 0xffff;

	/*
	 * Check which section of the firmware we need.
	 */
	if ((chipset == 0x2860) ||
	    (chipset == 0x2872) ||
	    (chipset == 0x3070)) {
		offset = 0;
		length = 4096;
	} else {
		offset = 4096;
		length = 4096;
	}

	/*
	 * Wait for stable hardware.
	 */
	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2800_register_read(rt2x00dev, MAC_CSR0, &reg);
		if (reg && reg != ~0)
			break;
		msleep(1);
	}

	if (i == REGISTER_BUSY_COUNT) {
		ERROR(rt2x00dev, "Unstable hardware.\n");
		return -EBUSY;
	}

	/*
	 * Write firmware to device.
	 */
	rt2x00usb_vendor_request_large_buff(rt2x00dev, USB_MULTI_WRITE,
					    USB_VENDOR_REQUEST_OUT,
					    FIRMWARE_IMAGE_BASE,
					    data + offset, length,
					    REGISTER_TIMEOUT32(length));

	rt2800_register_write(rt2x00dev, H2M_MAILBOX_CID, ~0);
	rt2800_register_write(rt2x00dev, H2M_MAILBOX_STATUS, ~0);

	/*
	 * Send firmware request to device to load firmware,
	 * we need to specify a long timeout time.
	 */
	status = rt2x00usb_vendor_request_sw(rt2x00dev, USB_DEVICE_MODE,
					     0, USB_MODE_FIRMWARE,
					     REGISTER_TIMEOUT_FIRMWARE);
	if (status < 0) {
		ERROR(rt2x00dev, "Failed to write Firmware to device.\n");
		return status;
	}

	msleep(10);
	rt2800_register_write(rt2x00dev, H2M_MAILBOX_CSR, 0);

	/*
	 * Send signal to firmware during boot time.
	 */
	rt2800_mcu_request(rt2x00dev, MCU_BOOT_SIGNAL, 0xff, 0, 0);

	if ((chipset == 0x3070) ||
	    (chipset == 0x3071) ||
	    (chipset == 0x3572)) {
		udelay(200);
		rt2800_mcu_request(rt2x00dev, MCU_CURRENT, 0, 0, 0);
		udelay(10);
	}

	/*
	 * Wait for device to stabilize.
	 */
	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2800_register_read(rt2x00dev, PBF_SYS_CTRL, &reg);
		if (rt2x00_get_field32(reg, PBF_SYS_CTRL_READY))
			break;
		msleep(1);
	}

	if (i == REGISTER_BUSY_COUNT) {
		ERROR(rt2x00dev, "PBF system register not ready.\n");
		return -EBUSY;
	}

	/*
	 * Initialize firmware.
	 */
	rt2800_register_write(rt2x00dev, H2M_BBP_AGENT, 0);
	rt2800_register_write(rt2x00dev, H2M_MAILBOX_CSR, 0);
	msleep(1);

	return 0;
}

/*
 * Device state switch handlers.
 */
static void rt2800usb_toggle_rx(struct rt2x00_dev *rt2x00dev,
				enum dev_state state)
{
	u32 reg;

	rt2800_register_read(rt2x00dev, MAC_SYS_CTRL, &reg);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_RX,
			   (state == STATE_RADIO_RX_ON) ||
			   (state == STATE_RADIO_RX_ON_LINK));
	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, reg);
}

static int rt2800usb_wait_wpdma_ready(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u32 reg;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2800_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
		if (!rt2x00_get_field32(reg, WPDMA_GLO_CFG_TX_DMA_BUSY) &&
		    !rt2x00_get_field32(reg, WPDMA_GLO_CFG_RX_DMA_BUSY))
			return 0;

		msleep(1);
	}

	ERROR(rt2x00dev, "WPDMA TX/RX busy, aborting.\n");
	return -EACCES;
}

static int rt2800usb_enable_radio(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	u16 word;

	/*
	 * Initialize all registers.
	 */
	if (unlikely(rt2800usb_wait_wpdma_ready(rt2x00dev) ||
		     rt2800_init_registers(rt2x00dev) ||
		     rt2800_init_bbp(rt2x00dev) ||
		     rt2800_init_rfcsr(rt2x00dev)))
		return -EIO;

	rt2800_register_read(rt2x00dev, MAC_SYS_CTRL, &reg);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_TX, 1);
	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, reg);

	udelay(50);

	rt2800_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_WRITEBACK_DONE, 1);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_RX_DMA, 1);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_TX_DMA, 1);
	rt2800_register_write(rt2x00dev, WPDMA_GLO_CFG, reg);


	rt2800_register_read(rt2x00dev, USB_DMA_CFG, &reg);
	rt2x00_set_field32(&reg, USB_DMA_CFG_PHY_CLEAR, 0);
	/* Don't use bulk in aggregation when working with USB 1.1 */
	rt2x00_set_field32(&reg, USB_DMA_CFG_RX_BULK_AGG_EN,
			   (rt2x00dev->rx->usb_maxpacket == 512));
	rt2x00_set_field32(&reg, USB_DMA_CFG_RX_BULK_AGG_TIMEOUT, 128);
	/*
	 * Total room for RX frames in kilobytes, PBF might still exceed
	 * this limit so reduce the number to prevent errors.
	 */
	rt2x00_set_field32(&reg, USB_DMA_CFG_RX_BULK_AGG_LIMIT,
			   ((RX_ENTRIES * DATA_FRAME_SIZE) / 1024) - 3);
	rt2x00_set_field32(&reg, USB_DMA_CFG_RX_BULK_EN, 1);
	rt2x00_set_field32(&reg, USB_DMA_CFG_TX_BULK_EN, 1);
	rt2800_register_write(rt2x00dev, USB_DMA_CFG, reg);

	rt2800_register_read(rt2x00dev, MAC_SYS_CTRL, &reg);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_TX, 1);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_RX, 1);
	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, reg);

	/*
	 * Initialize LED control
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED1, &word);
	rt2800_mcu_request(rt2x00dev, MCU_LED_1, 0xff,
			      word & 0xff, (word >> 8) & 0xff);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED2, &word);
	rt2800_mcu_request(rt2x00dev, MCU_LED_2, 0xff,
			      word & 0xff, (word >> 8) & 0xff);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED3, &word);
	rt2800_mcu_request(rt2x00dev, MCU_LED_3, 0xff,
			      word & 0xff, (word >> 8) & 0xff);

	return 0;
}

static void rt2800usb_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2800_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_TX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_RX_DMA, 0);
	rt2800_register_write(rt2x00dev, WPDMA_GLO_CFG, reg);

	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, 0);
	rt2800_register_write(rt2x00dev, PWR_PIN_CFG, 0);
	rt2800_register_write(rt2x00dev, TX_PIN_CFG, 0);

	/* Wait for DMA, ignore error */
	rt2800usb_wait_wpdma_ready(rt2x00dev);

	rt2x00usb_disable_radio(rt2x00dev);
}

static int rt2800usb_set_state(struct rt2x00_dev *rt2x00dev,
			       enum dev_state state)
{
	if (state == STATE_AWAKE)
		rt2800_mcu_request(rt2x00dev, MCU_WAKEUP, 0xff, 0, 0);
	else
		rt2800_mcu_request(rt2x00dev, MCU_SLEEP, 0xff, 0, 2);

	return 0;
}

static int rt2800usb_set_device_state(struct rt2x00_dev *rt2x00dev,
				      enum dev_state state)
{
	int retval = 0;

	switch (state) {
	case STATE_RADIO_ON:
		/*
		 * Before the radio can be enabled, the device first has
		 * to be woken up. After that it needs a bit of time
		 * to be fully awake and then the radio can be enabled.
		 */
		rt2800usb_set_state(rt2x00dev, STATE_AWAKE);
		msleep(1);
		retval = rt2800usb_enable_radio(rt2x00dev);
		break;
	case STATE_RADIO_OFF:
		/*
		 * After the radio has been disabled, the device should
		 * be put to sleep for powersaving.
		 */
		rt2800usb_disable_radio(rt2x00dev);
		rt2800usb_set_state(rt2x00dev, STATE_SLEEP);
		break;
	case STATE_RADIO_RX_ON:
	case STATE_RADIO_RX_ON_LINK:
	case STATE_RADIO_RX_OFF:
	case STATE_RADIO_RX_OFF_LINK:
		rt2800usb_toggle_rx(rt2x00dev, state);
		break;
	case STATE_RADIO_IRQ_ON:
	case STATE_RADIO_IRQ_OFF:
		/* No support, but no error either */
		break;
	case STATE_DEEP_SLEEP:
	case STATE_SLEEP:
	case STATE_STANDBY:
	case STATE_AWAKE:
		retval = rt2800usb_set_state(rt2x00dev, state);
		break;
	default:
		retval = -ENOTSUPP;
		break;
	}

	if (unlikely(retval))
		ERROR(rt2x00dev, "Device failed to enter state %d (%d).\n",
		      state, retval);

	return retval;
}

/*
 * TX descriptor initialization
 */
static void rt2800usb_write_tx_desc(struct rt2x00_dev *rt2x00dev,
				    struct sk_buff *skb,
				    struct txentry_desc *txdesc)
{
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(skb);
	__le32 *txi = skbdesc->desc;
	__le32 *txwi = &txi[TXINFO_DESC_SIZE / sizeof(__le32)];
	u32 word;

	/*
	 * Initialize TX Info descriptor
	 */
	rt2x00_desc_read(txwi, 0, &word);
	rt2x00_set_field32(&word, TXWI_W0_FRAG,
			   test_bit(ENTRY_TXD_MORE_FRAG, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W0_MIMO_PS, 0);
	rt2x00_set_field32(&word, TXWI_W0_CF_ACK, 0);
	rt2x00_set_field32(&word, TXWI_W0_TS,
			   test_bit(ENTRY_TXD_REQ_TIMESTAMP, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W0_AMPDU,
			   test_bit(ENTRY_TXD_HT_AMPDU, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W0_MPDU_DENSITY, txdesc->mpdu_density);
	rt2x00_set_field32(&word, TXWI_W0_TX_OP, txdesc->ifs);
	rt2x00_set_field32(&word, TXWI_W0_MCS, txdesc->mcs);
	rt2x00_set_field32(&word, TXWI_W0_BW,
			   test_bit(ENTRY_TXD_HT_BW_40, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W0_SHORT_GI,
			   test_bit(ENTRY_TXD_HT_SHORT_GI, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W0_STBC, txdesc->stbc);
	rt2x00_set_field32(&word, TXWI_W0_PHYMODE, txdesc->rate_mode);
	rt2x00_desc_write(txwi, 0, word);

	rt2x00_desc_read(txwi, 1, &word);
	rt2x00_set_field32(&word, TXWI_W1_ACK,
			   test_bit(ENTRY_TXD_ACK, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W1_NSEQ,
			   test_bit(ENTRY_TXD_GENERATE_SEQ, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W1_BW_WIN_SIZE, txdesc->ba_size);
	rt2x00_set_field32(&word, TXWI_W1_WIRELESS_CLI_ID,
			   test_bit(ENTRY_TXD_ENCRYPT, &txdesc->flags) ?
			   txdesc->key_idx : 0xff);
	rt2x00_set_field32(&word, TXWI_W1_MPDU_TOTAL_BYTE_COUNT,
			   skb->len - txdesc->l2pad);
	rt2x00_set_field32(&word, TXWI_W1_PACKETID,
			   skbdesc->entry->queue->qid + 1);
	rt2x00_desc_write(txwi, 1, word);

	/*
	 * Always write 0 to IV/EIV fields, hardware will insert the IV
	 * from the IVEIV register when TXINFO_W0_WIV is set to 0.
	 * When TXINFO_W0_WIV is set to 1 it will use the IV data
	 * from the descriptor. The TXWI_W1_WIRELESS_CLI_ID indicates which
	 * crypto entry in the registers should be used to encrypt the frame.
	 */
	_rt2x00_desc_write(txwi, 2, 0 /* skbdesc->iv[0] */);
	_rt2x00_desc_write(txwi, 3, 0 /* skbdesc->iv[1] */);

	/*
	 * Initialize TX descriptor
	 */
	rt2x00_desc_read(txi, 0, &word);
	rt2x00_set_field32(&word, TXINFO_W0_USB_DMA_TX_PKT_LEN,
			   skb->len + TXWI_DESC_SIZE);
	rt2x00_set_field32(&word, TXINFO_W0_WIV,
			   !test_bit(ENTRY_TXD_ENCRYPT_IV, &txdesc->flags));
	rt2x00_set_field32(&word, TXINFO_W0_QSEL, 2);
	rt2x00_set_field32(&word, TXINFO_W0_SW_USE_LAST_ROUND, 0);
	rt2x00_set_field32(&word, TXINFO_W0_USB_DMA_NEXT_VALID, 0);
	rt2x00_set_field32(&word, TXINFO_W0_USB_DMA_TX_BURST,
			   test_bit(ENTRY_TXD_BURST, &txdesc->flags));
	rt2x00_desc_write(txi, 0, word);
}

/*
 * TX data initialization
 */
static void rt2800usb_write_beacon(struct queue_entry *entry)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	unsigned int beacon_base;
	u32 reg;

	/*
	 * Add the descriptor in front of the skb.
	 */
	skb_push(entry->skb, entry->queue->desc_size);
	memcpy(entry->skb->data, skbdesc->desc, skbdesc->desc_len);
	skbdesc->desc = entry->skb->data;

	/*
	 * Disable beaconing while we are reloading the beacon data,
	 * otherwise we might be sending out invalid data.
	 */
	rt2800_register_read(rt2x00dev, BCN_TIME_CFG, &reg);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 0);
	rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);

	/*
	 * Write entire beacon with descriptor to register.
	 */
	beacon_base = HW_BEACON_OFFSET(entry->entry_idx);
	rt2x00usb_vendor_request_large_buff(rt2x00dev, USB_MULTI_WRITE,
					    USB_VENDOR_REQUEST_OUT, beacon_base,
					    entry->skb->data, entry->skb->len,
					    REGISTER_TIMEOUT32(entry->skb->len));

	/*
	 * Clean up the beacon skb.
	 */
	dev_kfree_skb(entry->skb);
	entry->skb = NULL;
}

static int rt2800usb_get_tx_data_len(struct queue_entry *entry)
{
	int length;

	/*
	 * The length _must_ include 4 bytes padding,
	 * it should always be multiple of 4,
	 * but it must _not_ be a multiple of the USB packet size.
	 */
	length = roundup(entry->skb->len + 4, 4);
	length += (4 * !(length % entry->queue->usb_maxpacket));

	return length;
}

static void rt2800usb_kick_tx_queue(struct rt2x00_dev *rt2x00dev,
				    const enum data_queue_qid queue)
{
	u32 reg;

	if (queue != QID_BEACON) {
		rt2x00usb_kick_tx_queue(rt2x00dev, queue);
		return;
	}

	rt2800_register_read(rt2x00dev, BCN_TIME_CFG, &reg);
	if (!rt2x00_get_field32(reg, BCN_TIME_CFG_BEACON_GEN)) {
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TSF_TICKING, 1);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TBTT_ENABLE, 1);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 1);
		rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);
	}
}

/*
 * RX control handlers
 */
static void rt2800usb_fill_rxdone(struct queue_entry *entry,
				  struct rxdone_entry_desc *rxdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	__le32 *rxd = (__le32 *)entry->skb->data;
	__le32 *rxwi;
	u32 rxd0;
	u32 rxwi0;
	u32 rxwi1;
	u32 rxwi2;
	u32 rxwi3;

	/*
	 * Copy descriptor to the skbdesc->desc buffer, making it safe from
	 * moving of frame data in rt2x00usb.
	 */
	memcpy(skbdesc->desc, rxd, skbdesc->desc_len);
	rxd = (__le32 *)skbdesc->desc;
	rxwi = &rxd[RXINFO_DESC_SIZE / sizeof(__le32)];

	/*
	 * It is now safe to read the descriptor on all architectures.
	 */
	rt2x00_desc_read(rxd, 0, &rxd0);
	rt2x00_desc_read(rxwi, 0, &rxwi0);
	rt2x00_desc_read(rxwi, 1, &rxwi1);
	rt2x00_desc_read(rxwi, 2, &rxwi2);
	rt2x00_desc_read(rxwi, 3, &rxwi3);

	if (rt2x00_get_field32(rxd0, RXD_W0_CRC_ERROR))
		rxdesc->flags |= RX_FLAG_FAILED_FCS_CRC;

	if (test_bit(CONFIG_SUPPORT_HW_CRYPTO, &rt2x00dev->flags)) {
		rxdesc->cipher = rt2x00_get_field32(rxwi0, RXWI_W0_UDF);
		rxdesc->cipher_status =
		    rt2x00_get_field32(rxd0, RXD_W0_CIPHER_ERROR);
	}

	if (rt2x00_get_field32(rxd0, RXD_W0_DECRYPTED)) {
		/*
		 * Hardware has stripped IV/EIV data from 802.11 frame during
		 * decryption. Unfortunately the descriptor doesn't contain
		 * any fields with the EIV/IV data either, so they can't
		 * be restored by rt2x00lib.
		 */
		rxdesc->flags |= RX_FLAG_IV_STRIPPED;

		if (rxdesc->cipher_status == RX_CRYPTO_SUCCESS)
			rxdesc->flags |= RX_FLAG_DECRYPTED;
		else if (rxdesc->cipher_status == RX_CRYPTO_FAIL_MIC)
			rxdesc->flags |= RX_FLAG_MMIC_ERROR;
	}

	if (rt2x00_get_field32(rxd0, RXD_W0_MY_BSS))
		rxdesc->dev_flags |= RXDONE_MY_BSS;

	if (rt2x00_get_field32(rxd0, RXD_W0_L2PAD)) {
		rxdesc->dev_flags |= RXDONE_L2PAD;
		skbdesc->flags |= SKBDESC_L2_PADDED;
	}

	if (rt2x00_get_field32(rxwi1, RXWI_W1_SHORT_GI))
		rxdesc->flags |= RX_FLAG_SHORT_GI;

	if (rt2x00_get_field32(rxwi1, RXWI_W1_BW))
		rxdesc->flags |= RX_FLAG_40MHZ;

	/*
	 * Detect RX rate, always use MCS as signal type.
	 */
	rxdesc->dev_flags |= RXDONE_SIGNAL_MCS;
	rxdesc->rate_mode = rt2x00_get_field32(rxwi1, RXWI_W1_PHYMODE);
	rxdesc->signal = rt2x00_get_field32(rxwi1, RXWI_W1_MCS);

	/*
	 * Mask of 0x8 bit to remove the short preamble flag.
	 */
	if (rxdesc->rate_mode == RATE_MODE_CCK)
		rxdesc->signal &= ~0x8;

	rxdesc->rssi =
	    (rt2x00_get_field32(rxwi2, RXWI_W2_RSSI0) +
	     rt2x00_get_field32(rxwi2, RXWI_W2_RSSI1)) / 2;

	rxdesc->noise =
	    (rt2x00_get_field32(rxwi3, RXWI_W3_SNR0) +
	     rt2x00_get_field32(rxwi3, RXWI_W3_SNR1)) / 2;

	rxdesc->size = rt2x00_get_field32(rxwi0, RXWI_W0_MPDU_TOTAL_BYTE_COUNT);

	/*
	 * Remove RXWI descriptor from start of buffer.
	 */
	skb_pull(entry->skb, skbdesc->desc_len);
	skb_trim(entry->skb, rxdesc->size);
}

/*
 * Device probe functions.
 */
static int rt2800usb_validate_eeprom(struct rt2x00_dev *rt2x00dev)
{
	u16 word;
	u8 *mac;
	u8 default_lna_gain;

	rt2x00usb_eeprom_read(rt2x00dev, rt2x00dev->eeprom, EEPROM_SIZE);

	/*
	 * Start validation of the data that has been read.
	 */
	mac = rt2x00_eeprom_addr(rt2x00dev, EEPROM_MAC_ADDR_0);
	if (!is_valid_ether_addr(mac)) {
		random_ether_addr(mac);
		EEPROM(rt2x00dev, "MAC: %pM\n", mac);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_ANTENNA, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_ANTENNA_RXPATH, 2);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_TXPATH, 1);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_RF_TYPE, RF2820);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_ANTENNA, word);
		EEPROM(rt2x00dev, "Antenna: 0x%04x\n", word);
	} else if (rt2x00_rev(&rt2x00dev->chip) < RT2883_VERSION) {
		/*
		 * There is a max of 2 RX streams for RT2870 series
		 */
		if (rt2x00_get_field16(word, EEPROM_ANTENNA_RXPATH) > 2)
			rt2x00_set_field16(&word, EEPROM_ANTENNA_RXPATH, 2);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_ANTENNA, word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_NIC_HW_RADIO, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_DYNAMIC_TX_AGC, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_EXTERNAL_LNA_BG, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_EXTERNAL_LNA_A, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CARDBUS_ACCEL, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_BW40M_SB_BG, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_BW40M_SB_A, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_WPS_PBC, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_BW40M_BG, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_BW40M_A, 0);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_NIC, word);
		EEPROM(rt2x00dev, "NIC: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_FREQ, &word);
	if ((word & 0x00ff) == 0x00ff) {
		rt2x00_set_field16(&word, EEPROM_FREQ_OFFSET, 0);
		rt2x00_set_field16(&word, EEPROM_FREQ_LED_MODE,
				   LED_MODE_TXRX_ACTIVITY);
		rt2x00_set_field16(&word, EEPROM_FREQ_LED_POLARITY, 0);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_FREQ, word);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_LED1, 0x5555);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_LED2, 0x2221);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_LED3, 0xa9f8);
		EEPROM(rt2x00dev, "Freq: 0x%04x\n", word);
	}

	/*
	 * During the LNA validation we are going to use
	 * lna0 as correct value. Note that EEPROM_LNA
	 * is never validated.
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_LNA, &word);
	default_lna_gain = rt2x00_get_field16(word, EEPROM_LNA_A0);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_BG, &word);
	if (abs(rt2x00_get_field16(word, EEPROM_RSSI_BG_OFFSET0)) > 10)
		rt2x00_set_field16(&word, EEPROM_RSSI_BG_OFFSET0, 0);
	if (abs(rt2x00_get_field16(word, EEPROM_RSSI_BG_OFFSET1)) > 10)
		rt2x00_set_field16(&word, EEPROM_RSSI_BG_OFFSET1, 0);
	rt2x00_eeprom_write(rt2x00dev, EEPROM_RSSI_BG, word);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_BG2, &word);
	if (abs(rt2x00_get_field16(word, EEPROM_RSSI_BG2_OFFSET2)) > 10)
		rt2x00_set_field16(&word, EEPROM_RSSI_BG2_OFFSET2, 0);
	if (rt2x00_get_field16(word, EEPROM_RSSI_BG2_LNA_A1) == 0x00 ||
	    rt2x00_get_field16(word, EEPROM_RSSI_BG2_LNA_A1) == 0xff)
		rt2x00_set_field16(&word, EEPROM_RSSI_BG2_LNA_A1,
				   default_lna_gain);
	rt2x00_eeprom_write(rt2x00dev, EEPROM_RSSI_BG2, word);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_A, &word);
	if (abs(rt2x00_get_field16(word, EEPROM_RSSI_A_OFFSET0)) > 10)
		rt2x00_set_field16(&word, EEPROM_RSSI_A_OFFSET0, 0);
	if (abs(rt2x00_get_field16(word, EEPROM_RSSI_A_OFFSET1)) > 10)
		rt2x00_set_field16(&word, EEPROM_RSSI_A_OFFSET1, 0);
	rt2x00_eeprom_write(rt2x00dev, EEPROM_RSSI_A, word);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_A2, &word);
	if (abs(rt2x00_get_field16(word, EEPROM_RSSI_A2_OFFSET2)) > 10)
		rt2x00_set_field16(&word, EEPROM_RSSI_A2_OFFSET2, 0);
	if (rt2x00_get_field16(word, EEPROM_RSSI_A2_LNA_A2) == 0x00 ||
	    rt2x00_get_field16(word, EEPROM_RSSI_A2_LNA_A2) == 0xff)
		rt2x00_set_field16(&word, EEPROM_RSSI_A2_LNA_A2,
				   default_lna_gain);
	rt2x00_eeprom_write(rt2x00dev, EEPROM_RSSI_A2, word);

	return 0;
}

static int rt2800usb_init_eeprom(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	u16 value;
	u16 eeprom;

	/*
	 * Read EEPROM word for configuration.
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_ANTENNA, &eeprom);

	/*
	 * Identify RF chipset.
	 */
	value = rt2x00_get_field16(eeprom, EEPROM_ANTENNA_RF_TYPE);
	rt2800_register_read(rt2x00dev, MAC_CSR0, &reg);
	rt2x00_set_chip(rt2x00dev, RT2870, value, reg);

	/*
	 * The check for rt2860 is not a typo, some rt2870 hardware
	 * identifies itself as rt2860 in the CSR register.
	 */
	if (!rt2x00_check_rev(&rt2x00dev->chip, 0xfff00000, 0x28600000) &&
	    !rt2x00_check_rev(&rt2x00dev->chip, 0xfff00000, 0x28700000) &&
	    !rt2x00_check_rev(&rt2x00dev->chip, 0xfff00000, 0x28800000) &&
	    !rt2x00_check_rev(&rt2x00dev->chip, 0xffff0000, 0x30700000)) {
		ERROR(rt2x00dev, "Invalid RT chipset detected.\n");
		return -ENODEV;
	}

	if (!rt2x00_rf(&rt2x00dev->chip, RF2820) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF2850) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF2720) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF2750) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF3020) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF2020)) {
		ERROR(rt2x00dev, "Invalid RF chipset detected.\n");
		return -ENODEV;
	}

	/*
	 * Identify default antenna configuration.
	 */
	rt2x00dev->default_ant.tx =
	    rt2x00_get_field16(eeprom, EEPROM_ANTENNA_TXPATH);
	rt2x00dev->default_ant.rx =
	    rt2x00_get_field16(eeprom, EEPROM_ANTENNA_RXPATH);

	/*
	 * Read frequency offset and RF programming sequence.
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_FREQ, &eeprom);
	rt2x00dev->freq_offset = rt2x00_get_field16(eeprom, EEPROM_FREQ_OFFSET);

	/*
	 * Read external LNA informations.
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC, &eeprom);

	if (rt2x00_get_field16(eeprom, EEPROM_NIC_EXTERNAL_LNA_A))
		__set_bit(CONFIG_EXTERNAL_LNA_A, &rt2x00dev->flags);
	if (rt2x00_get_field16(eeprom, EEPROM_NIC_EXTERNAL_LNA_BG))
		__set_bit(CONFIG_EXTERNAL_LNA_BG, &rt2x00dev->flags);

	/*
	 * Detect if this device has an hardware controlled radio.
	 */
	if (rt2x00_get_field16(eeprom, EEPROM_NIC_HW_RADIO))
		__set_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags);

	/*
	 * Store led settings, for correct led behaviour.
	 */
#ifdef CONFIG_RT2X00_LIB_LEDS
	rt2800_init_led(rt2x00dev, &rt2x00dev->led_radio, LED_TYPE_RADIO);
	rt2800_init_led(rt2x00dev, &rt2x00dev->led_assoc, LED_TYPE_ASSOC);
	rt2800_init_led(rt2x00dev, &rt2x00dev->led_qual, LED_TYPE_QUALITY);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_FREQ,
			   &rt2x00dev->led_mcu_reg);
#endif /* CONFIG_RT2X00_LIB_LEDS */

	return 0;
}

/*
 * RF value list for rt2870
 * Supports: 2.4 GHz (all) & 5.2 GHz (RF2850 & RF2750)
 */
static const struct rf_channel rf_vals[] = {
	{ 1,  0x18402ecc, 0x184c0786, 0x1816b455, 0x1800510b },
	{ 2,  0x18402ecc, 0x184c0786, 0x18168a55, 0x1800519f },
	{ 3,  0x18402ecc, 0x184c078a, 0x18168a55, 0x1800518b },
	{ 4,  0x18402ecc, 0x184c078a, 0x18168a55, 0x1800519f },
	{ 5,  0x18402ecc, 0x184c078e, 0x18168a55, 0x1800518b },
	{ 6,  0x18402ecc, 0x184c078e, 0x18168a55, 0x1800519f },
	{ 7,  0x18402ecc, 0x184c0792, 0x18168a55, 0x1800518b },
	{ 8,  0x18402ecc, 0x184c0792, 0x18168a55, 0x1800519f },
	{ 9,  0x18402ecc, 0x184c0796, 0x18168a55, 0x1800518b },
	{ 10, 0x18402ecc, 0x184c0796, 0x18168a55, 0x1800519f },
	{ 11, 0x18402ecc, 0x184c079a, 0x18168a55, 0x1800518b },
	{ 12, 0x18402ecc, 0x184c079a, 0x18168a55, 0x1800519f },
	{ 13, 0x18402ecc, 0x184c079e, 0x18168a55, 0x1800518b },
	{ 14, 0x18402ecc, 0x184c07a2, 0x18168a55, 0x18005193 },

	/* 802.11 UNI / HyperLan 2 */
	{ 36, 0x18402ecc, 0x184c099a, 0x18158a55, 0x180ed1a3 },
	{ 38, 0x18402ecc, 0x184c099e, 0x18158a55, 0x180ed193 },
	{ 40, 0x18402ec8, 0x184c0682, 0x18158a55, 0x180ed183 },
	{ 44, 0x18402ec8, 0x184c0682, 0x18158a55, 0x180ed1a3 },
	{ 46, 0x18402ec8, 0x184c0686, 0x18158a55, 0x180ed18b },
	{ 48, 0x18402ec8, 0x184c0686, 0x18158a55, 0x180ed19b },
	{ 52, 0x18402ec8, 0x184c068a, 0x18158a55, 0x180ed193 },
	{ 54, 0x18402ec8, 0x184c068a, 0x18158a55, 0x180ed1a3 },
	{ 56, 0x18402ec8, 0x184c068e, 0x18158a55, 0x180ed18b },
	{ 60, 0x18402ec8, 0x184c0692, 0x18158a55, 0x180ed183 },
	{ 62, 0x18402ec8, 0x184c0692, 0x18158a55, 0x180ed193 },
	{ 64, 0x18402ec8, 0x184c0692, 0x18158a55, 0x180ed1a3 },

	/* 802.11 HyperLan 2 */
	{ 100, 0x18402ec8, 0x184c06b2, 0x18178a55, 0x180ed783 },
	{ 102, 0x18402ec8, 0x184c06b2, 0x18578a55, 0x180ed793 },
	{ 104, 0x18402ec8, 0x185c06b2, 0x18578a55, 0x180ed1a3 },
	{ 108, 0x18402ecc, 0x185c0a32, 0x18578a55, 0x180ed193 },
	{ 110, 0x18402ecc, 0x184c0a36, 0x18178a55, 0x180ed183 },
	{ 112, 0x18402ecc, 0x184c0a36, 0x18178a55, 0x180ed19b },
	{ 116, 0x18402ecc, 0x184c0a3a, 0x18178a55, 0x180ed1a3 },
	{ 118, 0x18402ecc, 0x184c0a3e, 0x18178a55, 0x180ed193 },
	{ 120, 0x18402ec4, 0x184c0382, 0x18178a55, 0x180ed183 },
	{ 124, 0x18402ec4, 0x184c0382, 0x18178a55, 0x180ed193 },
	{ 126, 0x18402ec4, 0x184c0382, 0x18178a55, 0x180ed15b },
	{ 128, 0x18402ec4, 0x184c0382, 0x18178a55, 0x180ed1a3 },
	{ 132, 0x18402ec4, 0x184c0386, 0x18178a55, 0x180ed18b },
	{ 134, 0x18402ec4, 0x184c0386, 0x18178a55, 0x180ed193 },
	{ 136, 0x18402ec4, 0x184c0386, 0x18178a55, 0x180ed19b },
	{ 140, 0x18402ec4, 0x184c038a, 0x18178a55, 0x180ed183 },

	/* 802.11 UNII */
	{ 149, 0x18402ec4, 0x184c038a, 0x18178a55, 0x180ed1a7 },
	{ 151, 0x18402ec4, 0x184c038e, 0x18178a55, 0x180ed187 },
	{ 153, 0x18402ec4, 0x184c038e, 0x18178a55, 0x180ed18f },
	{ 157, 0x18402ec4, 0x184c038e, 0x18178a55, 0x180ed19f },
	{ 159, 0x18402ec4, 0x184c038e, 0x18178a55, 0x180ed1a7 },
	{ 161, 0x18402ec4, 0x184c0392, 0x18178a55, 0x180ed187 },
	{ 165, 0x18402ec4, 0x184c0392, 0x18178a55, 0x180ed197 },
	{ 167, 0x18402ec4, 0x184c03d2, 0x18179855, 0x1815531f },
	{ 169, 0x18402ec4, 0x184c03d2, 0x18179855, 0x18155327 },
	{ 171, 0x18402ec4, 0x184c03d6, 0x18179855, 0x18155307 },
	{ 173, 0x18402ec4, 0x184c03d6, 0x18179855, 0x1815530f },

	/* 802.11 Japan */
	{ 184, 0x15002ccc, 0x1500491e, 0x1509be55, 0x150c0a0b },
	{ 188, 0x15002ccc, 0x15004922, 0x1509be55, 0x150c0a13 },
	{ 192, 0x15002ccc, 0x15004926, 0x1509be55, 0x150c0a1b },
	{ 196, 0x15002ccc, 0x1500492a, 0x1509be55, 0x150c0a23 },
	{ 208, 0x15002ccc, 0x1500493a, 0x1509be55, 0x150c0a13 },
	{ 212, 0x15002ccc, 0x1500493e, 0x1509be55, 0x150c0a1b },
	{ 216, 0x15002ccc, 0x15004982, 0x1509be55, 0x150c0a23 },
};

/*
 * RF value list for rt3070
 * Supports: 2.4 GHz
 */
static const struct rf_channel rf_vals_3070[] = {
	{1,  241, 2, 2 },
	{2,  241, 2, 7 },
	{3,  242, 2, 2 },
	{4,  242, 2, 7 },
	{5,  243, 2, 2 },
	{6,  243, 2, 7 },
	{7,  244, 2, 2 },
	{8,  244, 2, 7 },
	{9,  245, 2, 2 },
	{10, 245, 2, 7 },
	{11, 246, 2, 2 },
	{12, 246, 2, 7 },
	{13, 247, 2, 2 },
	{14, 248, 2, 4 },
};

static int rt2800usb_probe_hw_mode(struct rt2x00_dev *rt2x00dev)
{
	struct hw_mode_spec *spec = &rt2x00dev->spec;
	struct channel_info *info;
	char *tx_power1;
	char *tx_power2;
	unsigned int i;
	u16 eeprom;

	/*
	 * Initialize all hw fields.
	 */
	rt2x00dev->hw->flags =
	    IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING |
	    IEEE80211_HW_SIGNAL_DBM |
	    IEEE80211_HW_SUPPORTS_PS |
	    IEEE80211_HW_PS_NULLFUNC_STACK;
	rt2x00dev->hw->extra_tx_headroom = TXINFO_DESC_SIZE + TXWI_DESC_SIZE;

	SET_IEEE80211_DEV(rt2x00dev->hw, rt2x00dev->dev);
	SET_IEEE80211_PERM_ADDR(rt2x00dev->hw,
				rt2x00_eeprom_addr(rt2x00dev,
						   EEPROM_MAC_ADDR_0));

	rt2x00_eeprom_read(rt2x00dev, EEPROM_ANTENNA, &eeprom);

	/*
	 * Initialize HT information.
	 */
	spec->ht.ht_supported = true;
	spec->ht.cap =
	    IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
	    IEEE80211_HT_CAP_GRN_FLD |
	    IEEE80211_HT_CAP_SGI_20 |
	    IEEE80211_HT_CAP_SGI_40 |
	    IEEE80211_HT_CAP_TX_STBC |
	    IEEE80211_HT_CAP_RX_STBC |
	    IEEE80211_HT_CAP_PSMP_SUPPORT;
	spec->ht.ampdu_factor = 3;
	spec->ht.ampdu_density = 4;
	spec->ht.mcs.tx_params =
	    IEEE80211_HT_MCS_TX_DEFINED |
	    IEEE80211_HT_MCS_TX_RX_DIFF |
	    ((rt2x00_get_field16(eeprom, EEPROM_ANTENNA_TXPATH) - 1) <<
		IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT);

	switch (rt2x00_get_field16(eeprom, EEPROM_ANTENNA_RXPATH)) {
	case 3:
		spec->ht.mcs.rx_mask[2] = 0xff;
	case 2:
		spec->ht.mcs.rx_mask[1] = 0xff;
	case 1:
		spec->ht.mcs.rx_mask[0] = 0xff;
		spec->ht.mcs.rx_mask[4] = 0x1; /* MCS32 */
		break;
	}

	/*
	 * Initialize hw_mode information.
	 */
	spec->supported_bands = SUPPORT_BAND_2GHZ;
	spec->supported_rates = SUPPORT_RATE_CCK | SUPPORT_RATE_OFDM;

	if (rt2x00_rf(&rt2x00dev->chip, RF2820) ||
	    rt2x00_rf(&rt2x00dev->chip, RF2720)) {
		spec->num_channels = 14;
		spec->channels = rf_vals;
	} else if (rt2x00_rf(&rt2x00dev->chip, RF2850) ||
		   rt2x00_rf(&rt2x00dev->chip, RF2750)) {
		spec->supported_bands |= SUPPORT_BAND_5GHZ;
		spec->num_channels = ARRAY_SIZE(rf_vals);
		spec->channels = rf_vals;
	} else if (rt2x00_rf(&rt2x00dev->chip, RF3020) ||
		   rt2x00_rf(&rt2x00dev->chip, RF2020)) {
		spec->num_channels = ARRAY_SIZE(rf_vals_3070);
		spec->channels = rf_vals_3070;
	}

	/*
	 * Create channel information array
	 */
	info = kzalloc(spec->num_channels * sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	spec->channels_info = info;

	tx_power1 = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_BG1);
	tx_power2 = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_BG2);

	for (i = 0; i < 14; i++) {
		info[i].tx_power1 = TXPOWER_G_FROM_DEV(tx_power1[i]);
		info[i].tx_power2 = TXPOWER_G_FROM_DEV(tx_power2[i]);
	}

	if (spec->num_channels > 14) {
		tx_power1 = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_A1);
		tx_power2 = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_A2);

		for (i = 14; i < spec->num_channels; i++) {
			info[i].tx_power1 = TXPOWER_A_FROM_DEV(tx_power1[i]);
			info[i].tx_power2 = TXPOWER_A_FROM_DEV(tx_power2[i]);
		}
	}

	return 0;
}

static const struct rt2800_ops rt2800usb_rt2800_ops = {
	.register_read		= rt2x00usb_register_read,
	.register_write		= rt2x00usb_register_write,
	.register_write_lock	= rt2x00usb_register_write_lock,

	.register_multiread	= rt2x00usb_register_multiread,
	.register_multiwrite	= rt2x00usb_register_multiwrite,

	.regbusy_read		= rt2x00usb_regbusy_read,
};

static int rt2800usb_probe_hw(struct rt2x00_dev *rt2x00dev)
{
	int retval;

	rt2x00dev->priv = (void *)&rt2800usb_rt2800_ops;

	/*
	 * Allocate eeprom data.
	 */
	retval = rt2800usb_validate_eeprom(rt2x00dev);
	if (retval)
		return retval;

	retval = rt2800usb_init_eeprom(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * Initialize hw specifications.
	 */
	retval = rt2800usb_probe_hw_mode(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * This device has multiple filters for control frames
	 * and has a separate filter for PS Poll frames.
	 */
	__set_bit(DRIVER_SUPPORT_CONTROL_FILTERS, &rt2x00dev->flags);
	__set_bit(DRIVER_SUPPORT_CONTROL_FILTER_PSPOLL, &rt2x00dev->flags);

	/*
	 * This device requires firmware.
	 */
	__set_bit(DRIVER_REQUIRE_FIRMWARE, &rt2x00dev->flags);
	__set_bit(DRIVER_REQUIRE_L2PAD, &rt2x00dev->flags);
	if (!modparam_nohwcrypt)
		__set_bit(CONFIG_SUPPORT_HW_CRYPTO, &rt2x00dev->flags);

	/*
	 * Set the rssi offset.
	 */
	rt2x00dev->rssi_offset = DEFAULT_RSSI_OFFSET;

	return 0;
}

static const struct rt2x00lib_ops rt2800usb_rt2x00_ops = {
	.probe_hw		= rt2800usb_probe_hw,
	.get_firmware_name	= rt2800usb_get_firmware_name,
	.check_firmware		= rt2800usb_check_firmware,
	.load_firmware		= rt2800usb_load_firmware,
	.initialize		= rt2x00usb_initialize,
	.uninitialize		= rt2x00usb_uninitialize,
	.clear_entry		= rt2x00usb_clear_entry,
	.set_device_state	= rt2800usb_set_device_state,
	.rfkill_poll		= rt2800_rfkill_poll,
	.link_stats		= rt2800_link_stats,
	.reset_tuner		= rt2800_reset_tuner,
	.link_tuner		= rt2800_link_tuner,
	.write_tx_desc		= rt2800usb_write_tx_desc,
	.write_tx_data		= rt2x00usb_write_tx_data,
	.write_beacon		= rt2800usb_write_beacon,
	.get_tx_data_len	= rt2800usb_get_tx_data_len,
	.kick_tx_queue		= rt2800usb_kick_tx_queue,
	.kill_tx_queue		= rt2x00usb_kill_tx_queue,
	.fill_rxdone		= rt2800usb_fill_rxdone,
	.config_shared_key	= rt2800_config_shared_key,
	.config_pairwise_key	= rt2800_config_pairwise_key,
	.config_filter		= rt2800_config_filter,
	.config_intf		= rt2800_config_intf,
	.config_erp		= rt2800_config_erp,
	.config_ant		= rt2800_config_ant,
	.config			= rt2800_config,
};

static const struct data_queue_desc rt2800usb_queue_rx = {
	.entry_num		= RX_ENTRIES,
	.data_size		= AGGREGATION_SIZE,
	.desc_size		= RXINFO_DESC_SIZE + RXWI_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_usb),
};

static const struct data_queue_desc rt2800usb_queue_tx = {
	.entry_num		= TX_ENTRIES,
	.data_size		= AGGREGATION_SIZE,
	.desc_size		= TXINFO_DESC_SIZE + TXWI_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_usb),
};

static const struct data_queue_desc rt2800usb_queue_bcn = {
	.entry_num		= 8 * BEACON_ENTRIES,
	.data_size		= MGMT_FRAME_SIZE,
	.desc_size		= TXINFO_DESC_SIZE + TXWI_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_usb),
};

static const struct rt2x00_ops rt2800usb_ops = {
	.name		= KBUILD_MODNAME,
	.max_sta_intf	= 1,
	.max_ap_intf	= 8,
	.eeprom_size	= EEPROM_SIZE,
	.rf_size	= RF_SIZE,
	.tx_queues	= NUM_TX_QUEUES,
	.rx		= &rt2800usb_queue_rx,
	.tx		= &rt2800usb_queue_tx,
	.bcn		= &rt2800usb_queue_bcn,
	.lib		= &rt2800usb_rt2x00_ops,
	.hw		= &rt2800_mac80211_ops,
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	.debugfs	= &rt2800_rt2x00debug,
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */
};

/*
 * rt2800usb module information.
 */
static struct usb_device_id rt2800usb_device_table[] = {
	/* Abocom */
	{ USB_DEVICE(0x07b8, 0x2870), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07b8, 0x2770), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07b8, 0x3070), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07b8, 0x3071), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07b8, 0x3072), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1482, 0x3c09), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* AirTies */
	{ USB_DEVICE(0x1eda, 0x2310), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Amigo */
	{ USB_DEVICE(0x0e0b, 0x9031), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0e0b, 0x9041), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Amit */
	{ USB_DEVICE(0x15c5, 0x0008), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* ASUS */
	{ USB_DEVICE(0x0b05, 0x1731), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0b05, 0x1732), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0b05, 0x1742), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0b05, 0x1760), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0b05, 0x1761), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* AzureWave */
	{ USB_DEVICE(0x13d3, 0x3247), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x13d3, 0x3262), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x13d3, 0x3273), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x13d3, 0x3284), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Belkin */
	{ USB_DEVICE(0x050d, 0x8053), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x050d, 0x805c), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x050d, 0x815c), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x050d, 0x825a), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Buffalo */
	{ USB_DEVICE(0x0411, 0x00e8), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0411, 0x012e), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Conceptronic */
	{ USB_DEVICE(0x14b2, 0x3c06), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c07), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c08), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c09), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c11), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c12), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c23), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c25), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c27), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c28), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Corega */
	{ USB_DEVICE(0x07aa, 0x002f), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07aa, 0x003c), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07aa, 0x003f), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x18c5, 0x0008), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x18c5, 0x0012), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* D-Link */
	{ USB_DEVICE(0x07d1, 0x3c09), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c0a), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c0b), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c0d), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c0e), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c0f), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c11), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c13), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Edimax */
	{ USB_DEVICE(0x7392, 0x7711), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x7392, 0x7717), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x7392, 0x7718), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Encore */
	{ USB_DEVICE(0x203d, 0x1480), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* EnGenius */
	{ USB_DEVICE(0X1740, 0x9701), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1740, 0x9702), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1740, 0x9703), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1740, 0x9705), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1740, 0x9706), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1740, 0x9801), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Gemtek */
	{ USB_DEVICE(0x15a9, 0x0010), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Gigabyte */
	{ USB_DEVICE(0x1044, 0x800b), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1044, 0x800c), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1044, 0x800d), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Hawking */
	{ USB_DEVICE(0x0e66, 0x0001), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0e66, 0x0003), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0e66, 0x0009), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0e66, 0x000b), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* I-O DATA */
	{ USB_DEVICE(0x04bb, 0x0945), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* LevelOne */
	{ USB_DEVICE(0x1740, 0x0605), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1740, 0x0615), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Linksys */
	{ USB_DEVICE(0x1737, 0x0070), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1737, 0x0071), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1737, 0x0077), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Logitec */
	{ USB_DEVICE(0x0789, 0x0162), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0789, 0x0163), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0789, 0x0164), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Motorola */
	{ USB_DEVICE(0x100d, 0x9031), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x100d, 0x9032), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Ovislink */
	{ USB_DEVICE(0x1b75, 0x3072), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Pegatron */
	{ USB_DEVICE(0x1d4d, 0x0002), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1d4d, 0x000c), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1d4d, 0x000e), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Philips */
	{ USB_DEVICE(0x0471, 0x200f), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Planex */
	{ USB_DEVICE(0x2019, 0xed06), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x2019, 0xab24), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x2019, 0xab25), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Qcom */
	{ USB_DEVICE(0x18e8, 0x6259), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Quanta */
	{ USB_DEVICE(0x1a32, 0x0304), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Ralink */
	{ USB_DEVICE(0x0db0, 0x3820), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0db0, 0x6899), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x148f, 0x2070), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x148f, 0x2770), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x148f, 0x2870), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x148f, 0x3070), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x148f, 0x3071), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x148f, 0x3072), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x148f, 0x3572), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Samsung */
	{ USB_DEVICE(0x04e8, 0x2018), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Siemens */
	{ USB_DEVICE(0x129b, 0x1828), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Sitecom */
	{ USB_DEVICE(0x0df6, 0x0017), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x002b), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x002c), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x002d), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x0039), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x003b), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x003c), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x003d), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x003e), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x003f), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x0040), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x0042), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* SMC */
	{ USB_DEVICE(0x083a, 0x6618), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0x7511), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0x7512), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0x7522), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0x8522), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0xa512), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0xa618), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0xb522), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0xc522), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Sparklan */
	{ USB_DEVICE(0x15a9, 0x0006), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Sweex */
	{ USB_DEVICE(0x177f, 0x0153), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x177f, 0x0302), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x177f, 0x0313), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* U-Media*/
	{ USB_DEVICE(0x157e, 0x300e), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* ZCOM */
	{ USB_DEVICE(0x0cde, 0x0022), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0cde, 0x0025), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Zinwell */
	{ USB_DEVICE(0x5a57, 0x0280), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x5a57, 0x0282), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x5a57, 0x0283), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x5a57, 0x5257), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Zyxel */
	{ USB_DEVICE(0x0586, 0x3416), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0586, 0x341a), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ 0, }
};

MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("Ralink RT2800 USB Wireless LAN driver.");
MODULE_SUPPORTED_DEVICE("Ralink RT2870 USB chipset based cards");
MODULE_DEVICE_TABLE(usb, rt2800usb_device_table);
MODULE_FIRMWARE(FIRMWARE_RT2870);
MODULE_LICENSE("GPL");

static struct usb_driver rt2800usb_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= rt2800usb_device_table,
	.probe		= rt2x00usb_probe,
	.disconnect	= rt2x00usb_disconnect,
	.suspend	= rt2x00usb_suspend,
	.resume		= rt2x00usb_resume,
};

static int __init rt2800usb_init(void)
{
	return usb_register(&rt2800usb_driver);
}

static void __exit rt2800usb_exit(void)
{
	usb_deregister(&rt2800usb_driver);
}

module_init(rt2800usb_init);
module_exit(rt2800usb_exit);

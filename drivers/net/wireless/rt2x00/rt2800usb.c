/*
	Copyright (C) 2009 Ivo van Doorn <IvDoorn@gmail.com>
	Copyright (C) 2009 Mattias Nissler <mattias.nissler@gmx.de>
	Copyright (C) 2009 Felix Fietkau <nbd@openwrt.org>
	Copyright (C) 2009 Xose Vazquez Perez <xose.vazquez@gmail.com>
	Copyright (C) 2009 Axel Kollhofer <rain_maker@root-forum.org>
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
	    !rt2x00_rt(rt2x00dev, RT2860) &&
	    !rt2x00_rt(rt2x00dev, RT2872) &&
	    !rt2x00_rt(rt2x00dev, RT3070))
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

	/*
	 * Check which section of the firmware we need.
	 */
	if (rt2x00_rt(rt2x00dev, RT2860) ||
	    rt2x00_rt(rt2x00dev, RT2872) ||
	    rt2x00_rt(rt2x00dev, RT3070)) {
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

	if (rt2x00_rt(rt2x00dev, RT3070) ||
	    rt2x00_rt(rt2x00dev, RT3071) ||
	    rt2x00_rt(rt2x00dev, RT3572)) {
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

static int rt2800usb_enable_radio(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	u16 word;

	/*
	 * Initialize all registers.
	 */
	if (unlikely(rt2800_wait_wpdma_ready(rt2x00dev) ||
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
	rt2x00_set_field32(&reg, USB_DMA_CFG_RX_BULK_AGG_EN, 0);
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
	rt2800_wait_wpdma_ready(rt2x00dev);

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
	__le32 *rxi = (__le32 *)entry->skb->data;
	__le32 *rxwi;
	__le32 *rxd;
	u32 rxi0;
	u32 rxwi0;
	u32 rxwi1;
	u32 rxwi2;
	u32 rxwi3;
	u32 rxd0;
	int rx_pkt_len;

	/*
	 * RX frame format is :
	 * | RXINFO | RXWI | header | L2 pad | payload | pad | RXD | USB pad |
	 *          |<------------ rx_pkt_len -------------->|
	 */
	rt2x00_desc_read(rxi, 0, &rxi0);
	rx_pkt_len = rt2x00_get_field32(rxi0, RXINFO_W0_USB_DMA_RX_PKT_LEN);

	rxwi = (__le32 *)(entry->skb->data + RXINFO_DESC_SIZE);

	/*
	 * FIXME : we need to check for rx_pkt_len validity
	 */
	rxd = (__le32 *)(entry->skb->data + RXINFO_DESC_SIZE + rx_pkt_len);

	/*
	 * Copy descriptor to the skbdesc->desc buffer, making it safe from
	 * moving of frame data in rt2x00usb.
	 */
	memcpy(skbdesc->desc, rxi, skbdesc->desc_len);

	/*
	 * It is now safe to read the descriptor on all architectures.
	 */
	rt2x00_desc_read(rxwi, 0, &rxwi0);
	rt2x00_desc_read(rxwi, 1, &rxwi1);
	rt2x00_desc_read(rxwi, 2, &rxwi2);
	rt2x00_desc_read(rxwi, 3, &rxwi3);
	rt2x00_desc_read(rxd, 0, &rxd0);

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

	if (rt2x00_get_field32(rxd0, RXD_W0_L2PAD))
		rxdesc->dev_flags |= RXDONE_L2PAD;

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
}

/*
 * Device probe functions.
 */
static int rt2800usb_validate_eeprom(struct rt2x00_dev *rt2x00dev)
{
	if (rt2800_efuse_detect(rt2x00dev))
		rt2800_read_eeprom_efuse(rt2x00dev);
	else
		rt2x00usb_eeprom_read(rt2x00dev, rt2x00dev->eeprom,
				      EEPROM_SIZE);

	return rt2800_validate_eeprom(rt2x00dev);
}

static const struct rt2800_ops rt2800usb_rt2800_ops = {
	.register_read		= rt2x00usb_register_read,
	.register_read_lock	= rt2x00usb_register_read_lock,
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

	retval = rt2800_init_eeprom(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * Initialize hw specifications.
	 */
	retval = rt2800_probe_hw_mode(rt2x00dev);
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
	.name			= KBUILD_MODNAME,
	.max_sta_intf		= 1,
	.max_ap_intf		= 8,
	.eeprom_size		= EEPROM_SIZE,
	.rf_size		= RF_SIZE,
	.tx_queues		= NUM_TX_QUEUES,
	.extra_tx_headroom	= TXINFO_DESC_SIZE + TXWI_DESC_SIZE,
	.rx			= &rt2800usb_queue_rx,
	.tx			= &rt2800usb_queue_tx,
	.bcn			= &rt2800usb_queue_bcn,
	.lib			= &rt2800usb_rt2x00_ops,
	.hw			= &rt2800_mac80211_ops,
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	.debugfs		= &rt2800_rt2x00debug,
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */
};

/*
 * rt2800usb module information.
 */
static struct usb_device_id rt2800usb_device_table[] = {
	/* Abocom */
	{ USB_DEVICE(0x07b8, 0x2870), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07b8, 0x2770), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1482, 0x3c09), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Amit */
	{ USB_DEVICE(0x15c5, 0x0008), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Askey */
	{ USB_DEVICE(0x1690, 0x0740), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* ASUS */
	{ USB_DEVICE(0x0b05, 0x1731), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0b05, 0x1732), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0b05, 0x1742), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* AzureWave */
	{ USB_DEVICE(0x13d3, 0x3247), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Belkin */
	{ USB_DEVICE(0x050d, 0x8053), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x050d, 0x805c), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x050d, 0x815c), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Buffalo */
	{ USB_DEVICE(0x0411, 0x00e8), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Conceptronic */
	{ USB_DEVICE(0x14b2, 0x3c06), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c07), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c09), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c23), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c25), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c27), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c28), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Corega */
	{ USB_DEVICE(0x07aa, 0x002f), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07aa, 0x003c), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07aa, 0x003f), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* D-Link */
	{ USB_DEVICE(0x07d1, 0x3c09), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c11), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Edimax */
	{ USB_DEVICE(0x7392, 0x7717), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x7392, 0x7718), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* EnGenius */
	{ USB_DEVICE(0X1740, 0x9701), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1740, 0x9702), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Gigabyte */
	{ USB_DEVICE(0x1044, 0x800b), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Hawking */
	{ USB_DEVICE(0x0e66, 0x0001), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0e66, 0x0003), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Linksys */
	{ USB_DEVICE(0x1737, 0x0070), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1737, 0x0071), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Logitec */
	{ USB_DEVICE(0x0789, 0x0162), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0789, 0x0163), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0789, 0x0164), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Motorola */
	{ USB_DEVICE(0x100d, 0x9031), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* MSI */
	{ USB_DEVICE(0x0db0, 0x6899), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Philips */
	{ USB_DEVICE(0x0471, 0x200f), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Planex */
	{ USB_DEVICE(0x2019, 0xed06), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Ralink */
	{ USB_DEVICE(0x148f, 0x2770), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x148f, 0x2870), USB_DEVICE_DATA(&rt2800usb_ops) },
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
	{ USB_DEVICE(0x0df6, 0x003f), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* SMC */
	{ USB_DEVICE(0x083a, 0x6618), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0x7512), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0x7522), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0x8522), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0xa618), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0xb522), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Sparklan */
	{ USB_DEVICE(0x15a9, 0x0006), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Sweex */
	{ USB_DEVICE(0x177f, 0x0302), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* U-Media*/
	{ USB_DEVICE(0x157e, 0x300e), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* ZCOM */
	{ USB_DEVICE(0x0cde, 0x0022), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0cde, 0x0025), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Zinwell */
	{ USB_DEVICE(0x5a57, 0x0280), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x5a57, 0x0282), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Zyxel */
	{ USB_DEVICE(0x0586, 0x3416), USB_DEVICE_DATA(&rt2800usb_ops) },
#ifdef CONFIG_RT2800USB_RT30XX
	/* Abocom */
	{ USB_DEVICE(0x07b8, 0x3070), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07b8, 0x3071), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07b8, 0x3072), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* AirTies */
	{ USB_DEVICE(0x1eda, 0x2310), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* AzureWave */
	{ USB_DEVICE(0x13d3, 0x3273), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Conceptronic */
	{ USB_DEVICE(0x14b2, 0x3c12), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Corega */
	{ USB_DEVICE(0x18c5, 0x0012), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* D-Link */
	{ USB_DEVICE(0x07d1, 0x3c0a), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c0d), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c0e), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c0f), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Edimax */
	{ USB_DEVICE(0x7392, 0x7711), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Encore */
	{ USB_DEVICE(0x203d, 0x1480), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* EnGenius */
	{ USB_DEVICE(0x1740, 0x9703), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1740, 0x9705), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1740, 0x9706), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Gigabyte */
	{ USB_DEVICE(0x1044, 0x800d), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* I-O DATA */
	{ USB_DEVICE(0x04bb, 0x0945), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* MSI */
	{ USB_DEVICE(0x0db0, 0x3820), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Pegatron */
	{ USB_DEVICE(0x1d4d, 0x000c), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1d4d, 0x000e), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Planex */
	{ USB_DEVICE(0x2019, 0xab25), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Quanta */
	{ USB_DEVICE(0x1a32, 0x0304), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Ralink */
	{ USB_DEVICE(0x148f, 0x2070), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x148f, 0x3070), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x148f, 0x3071), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x148f, 0x3072), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Sitecom */
	{ USB_DEVICE(0x0df6, 0x003e), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x0042), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* SMC */
	{ USB_DEVICE(0x083a, 0x7511), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Zinwell */
	{ USB_DEVICE(0x5a57, 0x0283), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x5a57, 0x5257), USB_DEVICE_DATA(&rt2800usb_ops) },
#endif
#ifdef CONFIG_RT2800USB_RT35XX
	/* Askey */
	{ USB_DEVICE(0x1690, 0x0744), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Cisco */
	{ USB_DEVICE(0x167b, 0x4001), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* EnGenius */
	{ USB_DEVICE(0x1740, 0x9801), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* I-O DATA */
	{ USB_DEVICE(0x04bb, 0x0944), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Ralink */
	{ USB_DEVICE(0x148f, 0x3370), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x148f, 0x3572), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x148f, 0x8070), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Sitecom */
	{ USB_DEVICE(0x0df6, 0x0041), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Zinwell */
	{ USB_DEVICE(0x5a57, 0x0284), USB_DEVICE_DATA(&rt2800usb_ops) },
#endif
#ifdef CONFIG_RT2800USB_UNKNOWN
	/*
	 * Unclear what kind of devices these are (they aren't supported by the
	 * vendor driver).
	 */
	/* Allwin */
	{ USB_DEVICE(0x8516, 0x2070), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x8516, 0x2770), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x8516, 0x2870), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x8516, 0x3070), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x8516, 0x3071), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x8516, 0x3072), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x8516, 0x3572), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Amigo */
	{ USB_DEVICE(0x0e0b, 0x9031), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0e0b, 0x9041), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Askey */
	{ USB_DEVICE(0x0930, 0x0a07), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* ASUS */
	{ USB_DEVICE(0x0b05, 0x1760), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0b05, 0x1761), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0b05, 0x1784), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0b05, 0x1790), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1761, 0x0b05), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* AzureWave */
	{ USB_DEVICE(0x13d3, 0x3262), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x13d3, 0x3284), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x13d3, 0x3305), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Belkin */
	{ USB_DEVICE(0x050d, 0x825a), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Buffalo */
	{ USB_DEVICE(0x0411, 0x012e), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0411, 0x0148), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0411, 0x0150), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0411, 0x015d), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Conceptronic */
	{ USB_DEVICE(0x14b2, 0x3c08), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x14b2, 0x3c11), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Corega */
	{ USB_DEVICE(0x07aa, 0x0041), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07aa, 0x0042), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x18c5, 0x0008), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* D-Link */
	{ USB_DEVICE(0x07d1, 0x3c0b), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c13), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c15), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c16), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Encore */
	{ USB_DEVICE(0x203d, 0x14a1), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x203d, 0x14a9), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* EnGenius */
	{ USB_DEVICE(0x1740, 0x9707), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1740, 0x9708), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1740, 0x9709), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Gemtek */
	{ USB_DEVICE(0x15a9, 0x0010), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Gigabyte */
	{ USB_DEVICE(0x1044, 0x800c), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Hawking */
	{ USB_DEVICE(0x0e66, 0x0009), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0e66, 0x000b), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* I-O DATA */
	{ USB_DEVICE(0x04bb, 0x0947), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x04bb, 0x0948), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* LevelOne */
	{ USB_DEVICE(0x1740, 0x0605), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1740, 0x0615), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Linksys */
	{ USB_DEVICE(0x1737, 0x0077), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1737, 0x0078), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1737, 0x0079), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Motorola */
	{ USB_DEVICE(0x100d, 0x9032), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* MSI */
	{ USB_DEVICE(0x0db0, 0x3821), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0db0, 0x3822), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0db0, 0x3870), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0db0, 0x3871), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0db0, 0x821a), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0db0, 0x822a), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0db0, 0x870a), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0db0, 0x871a), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0db0, 0x899a), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Ovislink */
	{ USB_DEVICE(0x1b75, 0x3072), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Para */
	{ USB_DEVICE(0x20b8, 0x8888), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Pegatron */
	{ USB_DEVICE(0x05a6, 0x0101), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1d4d, 0x0002), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x1d4d, 0x0010), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Planex */
	{ USB_DEVICE(0x2019, 0xab24), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Qcom */
	{ USB_DEVICE(0x18e8, 0x6259), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Sitecom */
	{ USB_DEVICE(0x0df6, 0x003b), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x003c), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x003d), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x0040), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x0047), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x0048), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x004a), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x0df6, 0x004d), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* SMC */
	{ USB_DEVICE(0x083a, 0xa512), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0xa701), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0xa702), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0xc522), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x083a, 0xd522), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Sweex */
	{ USB_DEVICE(0x177f, 0x0153), USB_DEVICE_DATA(&rt2800usb_ops) },
	{ USB_DEVICE(0x177f, 0x0313), USB_DEVICE_DATA(&rt2800usb_ops) },
	/* Zyxel */
	{ USB_DEVICE(0x0586, 0x341a), USB_DEVICE_DATA(&rt2800usb_ops) },
#endif
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

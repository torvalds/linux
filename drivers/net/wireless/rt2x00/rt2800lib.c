/*
	Copyright (C) 2010 Willow Garage <http://www.willowgarage.com>
	Copyright (C) 2010 Ivo van Doorn <IvDoorn@gmail.com>
	Copyright (C) 2009 Bartlomiej Zolnierkiewicz <bzolnier@gmail.com>
	Copyright (C) 2009 Gertjan van Wingerde <gwingerde@gmail.com>

	Based on the original rt2800pci.c and rt2800usb.c.
	  Copyright (C) 2009 Alban Browaeys <prahal@yahoo.com>
	  Copyright (C) 2009 Felix Fietkau <nbd@openwrt.org>
	  Copyright (C) 2009 Luis Correia <luis.f.correia@gmail.com>
	  Copyright (C) 2009 Mattias Nissler <mattias.nissler@gmx.de>
	  Copyright (C) 2009 Mark Asselstine <asselsm@gmail.com>
	  Copyright (C) 2009 Xose Vazquez Perez <xose.vazquez@gmail.com>
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
	Module: rt2800lib
	Abstract: rt2800 generic device routines.
 */

#include <linux/crc-ccitt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "rt2x00.h"
#include "rt2800lib.h"
#include "rt2800.h"

/*
 * Register access.
 * All access to the CSR registers will go through the methods
 * rt2800_register_read and rt2800_register_write.
 * BBP and RF register require indirect register access,
 * and use the CSR registers BBPCSR and RFCSR to achieve this.
 * These indirect registers work with busy bits,
 * and we will try maximal REGISTER_BUSY_COUNT times to access
 * the register while taking a REGISTER_BUSY_DELAY us delay
 * between each attampt. When the busy bit is still set at that time,
 * the access attempt is considered to have failed,
 * and we will print an error.
 * The _lock versions must be used if you already hold the csr_mutex
 */
#define WAIT_FOR_BBP(__dev, __reg) \
	rt2800_regbusy_read((__dev), BBP_CSR_CFG, BBP_CSR_CFG_BUSY, (__reg))
#define WAIT_FOR_RFCSR(__dev, __reg) \
	rt2800_regbusy_read((__dev), RF_CSR_CFG, RF_CSR_CFG_BUSY, (__reg))
#define WAIT_FOR_RF(__dev, __reg) \
	rt2800_regbusy_read((__dev), RF_CSR_CFG0, RF_CSR_CFG0_BUSY, (__reg))
#define WAIT_FOR_MCU(__dev, __reg) \
	rt2800_regbusy_read((__dev), H2M_MAILBOX_CSR, \
			    H2M_MAILBOX_CSR_OWNER, (__reg))

static inline bool rt2800_is_305x_soc(struct rt2x00_dev *rt2x00dev)
{
	/* check for rt2872 on SoC */
	if (!rt2x00_is_soc(rt2x00dev) ||
	    !rt2x00_rt(rt2x00dev, RT2872))
		return false;

	/* we know for sure that these rf chipsets are used on rt305x boards */
	if (rt2x00_rf(rt2x00dev, RF3020) ||
	    rt2x00_rf(rt2x00dev, RF3021) ||
	    rt2x00_rf(rt2x00dev, RF3022))
		return true;

	NOTICE(rt2x00dev, "Unknown RF chipset on rt305x\n");
	return false;
}

static void rt2800_bbp_write(struct rt2x00_dev *rt2x00dev,
			     const unsigned int word, const u8 value)
{
	u32 reg;

	mutex_lock(&rt2x00dev->csr_mutex);

	/*
	 * Wait until the BBP becomes available, afterwards we
	 * can safely write the new data into the register.
	 */
	if (WAIT_FOR_BBP(rt2x00dev, &reg)) {
		reg = 0;
		rt2x00_set_field32(&reg, BBP_CSR_CFG_VALUE, value);
		rt2x00_set_field32(&reg, BBP_CSR_CFG_REGNUM, word);
		rt2x00_set_field32(&reg, BBP_CSR_CFG_BUSY, 1);
		rt2x00_set_field32(&reg, BBP_CSR_CFG_READ_CONTROL, 0);
		rt2x00_set_field32(&reg, BBP_CSR_CFG_BBP_RW_MODE, 1);

		rt2800_register_write_lock(rt2x00dev, BBP_CSR_CFG, reg);
	}

	mutex_unlock(&rt2x00dev->csr_mutex);
}

static void rt2800_bbp_read(struct rt2x00_dev *rt2x00dev,
			    const unsigned int word, u8 *value)
{
	u32 reg;

	mutex_lock(&rt2x00dev->csr_mutex);

	/*
	 * Wait until the BBP becomes available, afterwards we
	 * can safely write the read request into the register.
	 * After the data has been written, we wait until hardware
	 * returns the correct value, if at any time the register
	 * doesn't become available in time, reg will be 0xffffffff
	 * which means we return 0xff to the caller.
	 */
	if (WAIT_FOR_BBP(rt2x00dev, &reg)) {
		reg = 0;
		rt2x00_set_field32(&reg, BBP_CSR_CFG_REGNUM, word);
		rt2x00_set_field32(&reg, BBP_CSR_CFG_BUSY, 1);
		rt2x00_set_field32(&reg, BBP_CSR_CFG_READ_CONTROL, 1);
		rt2x00_set_field32(&reg, BBP_CSR_CFG_BBP_RW_MODE, 1);

		rt2800_register_write_lock(rt2x00dev, BBP_CSR_CFG, reg);

		WAIT_FOR_BBP(rt2x00dev, &reg);
	}

	*value = rt2x00_get_field32(reg, BBP_CSR_CFG_VALUE);

	mutex_unlock(&rt2x00dev->csr_mutex);
}

static void rt2800_rfcsr_write(struct rt2x00_dev *rt2x00dev,
			       const unsigned int word, const u8 value)
{
	u32 reg;

	mutex_lock(&rt2x00dev->csr_mutex);

	/*
	 * Wait until the RFCSR becomes available, afterwards we
	 * can safely write the new data into the register.
	 */
	if (WAIT_FOR_RFCSR(rt2x00dev, &reg)) {
		reg = 0;
		rt2x00_set_field32(&reg, RF_CSR_CFG_DATA, value);
		rt2x00_set_field32(&reg, RF_CSR_CFG_REGNUM, word);
		rt2x00_set_field32(&reg, RF_CSR_CFG_WRITE, 1);
		rt2x00_set_field32(&reg, RF_CSR_CFG_BUSY, 1);

		rt2800_register_write_lock(rt2x00dev, RF_CSR_CFG, reg);
	}

	mutex_unlock(&rt2x00dev->csr_mutex);
}

static void rt2800_rfcsr_read(struct rt2x00_dev *rt2x00dev,
			      const unsigned int word, u8 *value)
{
	u32 reg;

	mutex_lock(&rt2x00dev->csr_mutex);

	/*
	 * Wait until the RFCSR becomes available, afterwards we
	 * can safely write the read request into the register.
	 * After the data has been written, we wait until hardware
	 * returns the correct value, if at any time the register
	 * doesn't become available in time, reg will be 0xffffffff
	 * which means we return 0xff to the caller.
	 */
	if (WAIT_FOR_RFCSR(rt2x00dev, &reg)) {
		reg = 0;
		rt2x00_set_field32(&reg, RF_CSR_CFG_REGNUM, word);
		rt2x00_set_field32(&reg, RF_CSR_CFG_WRITE, 0);
		rt2x00_set_field32(&reg, RF_CSR_CFG_BUSY, 1);

		rt2800_register_write_lock(rt2x00dev, RF_CSR_CFG, reg);

		WAIT_FOR_RFCSR(rt2x00dev, &reg);
	}

	*value = rt2x00_get_field32(reg, RF_CSR_CFG_DATA);

	mutex_unlock(&rt2x00dev->csr_mutex);
}

static void rt2800_rf_write(struct rt2x00_dev *rt2x00dev,
			    const unsigned int word, const u32 value)
{
	u32 reg;

	mutex_lock(&rt2x00dev->csr_mutex);

	/*
	 * Wait until the RF becomes available, afterwards we
	 * can safely write the new data into the register.
	 */
	if (WAIT_FOR_RF(rt2x00dev, &reg)) {
		reg = 0;
		rt2x00_set_field32(&reg, RF_CSR_CFG0_REG_VALUE_BW, value);
		rt2x00_set_field32(&reg, RF_CSR_CFG0_STANDBYMODE, 0);
		rt2x00_set_field32(&reg, RF_CSR_CFG0_SEL, 0);
		rt2x00_set_field32(&reg, RF_CSR_CFG0_BUSY, 1);

		rt2800_register_write_lock(rt2x00dev, RF_CSR_CFG0, reg);
		rt2x00_rf_write(rt2x00dev, word, value);
	}

	mutex_unlock(&rt2x00dev->csr_mutex);
}

void rt2800_mcu_request(struct rt2x00_dev *rt2x00dev,
			const u8 command, const u8 token,
			const u8 arg0, const u8 arg1)
{
	u32 reg;

	/*
	 * SOC devices don't support MCU requests.
	 */
	if (rt2x00_is_soc(rt2x00dev))
		return;

	mutex_lock(&rt2x00dev->csr_mutex);

	/*
	 * Wait until the MCU becomes available, afterwards we
	 * can safely write the new data into the register.
	 */
	if (WAIT_FOR_MCU(rt2x00dev, &reg)) {
		rt2x00_set_field32(&reg, H2M_MAILBOX_CSR_OWNER, 1);
		rt2x00_set_field32(&reg, H2M_MAILBOX_CSR_CMD_TOKEN, token);
		rt2x00_set_field32(&reg, H2M_MAILBOX_CSR_ARG0, arg0);
		rt2x00_set_field32(&reg, H2M_MAILBOX_CSR_ARG1, arg1);
		rt2800_register_write_lock(rt2x00dev, H2M_MAILBOX_CSR, reg);

		reg = 0;
		rt2x00_set_field32(&reg, HOST_CMD_CSR_HOST_COMMAND, command);
		rt2800_register_write_lock(rt2x00dev, HOST_CMD_CSR, reg);
	}

	mutex_unlock(&rt2x00dev->csr_mutex);
}
EXPORT_SYMBOL_GPL(rt2800_mcu_request);

int rt2800_wait_csr_ready(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i = 0;
	u32 reg;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2800_register_read(rt2x00dev, MAC_CSR0, &reg);
		if (reg && reg != ~0)
			return 0;
		msleep(1);
	}

	ERROR(rt2x00dev, "Unstable hardware.\n");
	return -EBUSY;
}
EXPORT_SYMBOL_GPL(rt2800_wait_csr_ready);

int rt2800_wait_wpdma_ready(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u32 reg;

	/*
	 * Some devices are really slow to respond here. Wait a whole second
	 * before timing out.
	 */
	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2800_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
		if (!rt2x00_get_field32(reg, WPDMA_GLO_CFG_TX_DMA_BUSY) &&
		    !rt2x00_get_field32(reg, WPDMA_GLO_CFG_RX_DMA_BUSY))
			return 0;

		msleep(10);
	}

	ERROR(rt2x00dev, "WPDMA TX/RX busy, aborting.\n");
	return -EACCES;
}
EXPORT_SYMBOL_GPL(rt2800_wait_wpdma_ready);

static bool rt2800_check_firmware_crc(const u8 *data, const size_t len)
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

int rt2800_check_firmware(struct rt2x00_dev *rt2x00dev,
			  const u8 *data, const size_t len)
{
	size_t offset = 0;
	size_t fw_len;
	bool multiple;

	/*
	 * PCI(e) & SOC devices require firmware with a length
	 * of 8kb. USB devices require firmware files with a length
	 * of 4kb. Certain USB chipsets however require different firmware,
	 * which Ralink only provides attached to the original firmware
	 * file. Thus for USB devices, firmware files have a length
	 * which is a multiple of 4kb.
	 */
	if (rt2x00_is_usb(rt2x00dev)) {
		fw_len = 4096;
		multiple = true;
	} else {
		fw_len = 8192;
		multiple = true;
	}

	/*
	 * Validate the firmware length
	 */
	if (len != fw_len && (!multiple || (len % fw_len) != 0))
		return FW_BAD_LENGTH;

	/*
	 * Check if the chipset requires one of the upper parts
	 * of the firmware.
	 */
	if (rt2x00_is_usb(rt2x00dev) &&
	    !rt2x00_rt(rt2x00dev, RT2860) &&
	    !rt2x00_rt(rt2x00dev, RT2872) &&
	    !rt2x00_rt(rt2x00dev, RT3070) &&
	    ((len / fw_len) == 1))
		return FW_BAD_VERSION;

	/*
	 * 8kb firmware files must be checked as if it were
	 * 2 separate firmware files.
	 */
	while (offset < len) {
		if (!rt2800_check_firmware_crc(data + offset, fw_len))
			return FW_BAD_CRC;

		offset += fw_len;
	}

	return FW_OK;
}
EXPORT_SYMBOL_GPL(rt2800_check_firmware);

int rt2800_load_firmware(struct rt2x00_dev *rt2x00dev,
			 const u8 *data, const size_t len)
{
	unsigned int i;
	u32 reg;

	/*
	 * If driver doesn't wake up firmware here,
	 * rt2800_load_firmware will hang forever when interface is up again.
	 */
	rt2800_register_write(rt2x00dev, AUTOWAKEUP_CFG, 0x00000000);

	/*
	 * Wait for stable hardware.
	 */
	if (rt2800_wait_csr_ready(rt2x00dev))
		return -EBUSY;

	if (rt2x00_is_pci(rt2x00dev)) {
		if (rt2x00_rt(rt2x00dev, RT3572) ||
		    rt2x00_rt(rt2x00dev, RT5390)) {
			rt2800_register_read(rt2x00dev, AUX_CTRL, &reg);
			rt2x00_set_field32(&reg, AUX_CTRL_FORCE_PCIE_CLK, 1);
			rt2x00_set_field32(&reg, AUX_CTRL_WAKE_PCIE_EN, 1);
			rt2800_register_write(rt2x00dev, AUX_CTRL, reg);
		}
		rt2800_register_write(rt2x00dev, PWR_PIN_CFG, 0x00000002);
	}

	/*
	 * Disable DMA, will be reenabled later when enabling
	 * the radio.
	 */
	rt2800_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_TX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_DMA_BUSY, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_RX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_RX_DMA_BUSY, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_WRITEBACK_DONE, 1);
	rt2800_register_write(rt2x00dev, WPDMA_GLO_CFG, reg);

	/*
	 * Write firmware to the device.
	 */
	rt2800_drv_write_firmware(rt2x00dev, data, len);

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
EXPORT_SYMBOL_GPL(rt2800_load_firmware);

void rt2800_write_tx_data(struct queue_entry *entry,
			  struct txentry_desc *txdesc)
{
	__le32 *txwi = rt2800_drv_get_txwi(entry);
	u32 word;

	/*
	 * Initialize TX Info descriptor
	 */
	rt2x00_desc_read(txwi, 0, &word);
	rt2x00_set_field32(&word, TXWI_W0_FRAG,
			   test_bit(ENTRY_TXD_MORE_FRAG, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W0_MIMO_PS,
			   test_bit(ENTRY_TXD_HT_MIMO_PS, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W0_CF_ACK, 0);
	rt2x00_set_field32(&word, TXWI_W0_TS,
			   test_bit(ENTRY_TXD_REQ_TIMESTAMP, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W0_AMPDU,
			   test_bit(ENTRY_TXD_HT_AMPDU, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W0_MPDU_DENSITY,
			   txdesc->u.ht.mpdu_density);
	rt2x00_set_field32(&word, TXWI_W0_TX_OP, txdesc->u.ht.txop);
	rt2x00_set_field32(&word, TXWI_W0_MCS, txdesc->u.ht.mcs);
	rt2x00_set_field32(&word, TXWI_W0_BW,
			   test_bit(ENTRY_TXD_HT_BW_40, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W0_SHORT_GI,
			   test_bit(ENTRY_TXD_HT_SHORT_GI, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W0_STBC, txdesc->u.ht.stbc);
	rt2x00_set_field32(&word, TXWI_W0_PHYMODE, txdesc->rate_mode);
	rt2x00_desc_write(txwi, 0, word);

	rt2x00_desc_read(txwi, 1, &word);
	rt2x00_set_field32(&word, TXWI_W1_ACK,
			   test_bit(ENTRY_TXD_ACK, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W1_NSEQ,
			   test_bit(ENTRY_TXD_GENERATE_SEQ, &txdesc->flags));
	rt2x00_set_field32(&word, TXWI_W1_BW_WIN_SIZE, txdesc->u.ht.ba_size);
	rt2x00_set_field32(&word, TXWI_W1_WIRELESS_CLI_ID,
			   test_bit(ENTRY_TXD_ENCRYPT, &txdesc->flags) ?
			   txdesc->key_idx : txdesc->u.ht.wcid);
	rt2x00_set_field32(&word, TXWI_W1_MPDU_TOTAL_BYTE_COUNT,
			   txdesc->length);
	rt2x00_set_field32(&word, TXWI_W1_PACKETID_QUEUE, entry->queue->qid);
	rt2x00_set_field32(&word, TXWI_W1_PACKETID_ENTRY, (entry->entry_idx % 3) + 1);
	rt2x00_desc_write(txwi, 1, word);

	/*
	 * Always write 0 to IV/EIV fields, hardware will insert the IV
	 * from the IVEIV register when TXD_W3_WIV is set to 0.
	 * When TXD_W3_WIV is set to 1 it will use the IV data
	 * from the descriptor. The TXWI_W1_WIRELESS_CLI_ID indicates which
	 * crypto entry in the registers should be used to encrypt the frame.
	 */
	_rt2x00_desc_write(txwi, 2, 0 /* skbdesc->iv[0] */);
	_rt2x00_desc_write(txwi, 3, 0 /* skbdesc->iv[1] */);
}
EXPORT_SYMBOL_GPL(rt2800_write_tx_data);

static int rt2800_agc_to_rssi(struct rt2x00_dev *rt2x00dev, u32 rxwi_w2)
{
	int rssi0 = rt2x00_get_field32(rxwi_w2, RXWI_W2_RSSI0);
	int rssi1 = rt2x00_get_field32(rxwi_w2, RXWI_W2_RSSI1);
	int rssi2 = rt2x00_get_field32(rxwi_w2, RXWI_W2_RSSI2);
	u16 eeprom;
	u8 offset0;
	u8 offset1;
	u8 offset2;

	if (rt2x00dev->curr_band == IEEE80211_BAND_2GHZ) {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_BG, &eeprom);
		offset0 = rt2x00_get_field16(eeprom, EEPROM_RSSI_BG_OFFSET0);
		offset1 = rt2x00_get_field16(eeprom, EEPROM_RSSI_BG_OFFSET1);
		rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_BG2, &eeprom);
		offset2 = rt2x00_get_field16(eeprom, EEPROM_RSSI_BG2_OFFSET2);
	} else {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_A, &eeprom);
		offset0 = rt2x00_get_field16(eeprom, EEPROM_RSSI_A_OFFSET0);
		offset1 = rt2x00_get_field16(eeprom, EEPROM_RSSI_A_OFFSET1);
		rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_A2, &eeprom);
		offset2 = rt2x00_get_field16(eeprom, EEPROM_RSSI_A2_OFFSET2);
	}

	/*
	 * Convert the value from the descriptor into the RSSI value
	 * If the value in the descriptor is 0, it is considered invalid
	 * and the default (extremely low) rssi value is assumed
	 */
	rssi0 = (rssi0) ? (-12 - offset0 - rt2x00dev->lna_gain - rssi0) : -128;
	rssi1 = (rssi1) ? (-12 - offset1 - rt2x00dev->lna_gain - rssi1) : -128;
	rssi2 = (rssi2) ? (-12 - offset2 - rt2x00dev->lna_gain - rssi2) : -128;

	/*
	 * mac80211 only accepts a single RSSI value. Calculating the
	 * average doesn't deliver a fair answer either since -60:-60 would
	 * be considered equally good as -50:-70 while the second is the one
	 * which gives less energy...
	 */
	rssi0 = max(rssi0, rssi1);
	return max(rssi0, rssi2);
}

void rt2800_process_rxwi(struct queue_entry *entry,
			 struct rxdone_entry_desc *rxdesc)
{
	__le32 *rxwi = (__le32 *) entry->skb->data;
	u32 word;

	rt2x00_desc_read(rxwi, 0, &word);

	rxdesc->cipher = rt2x00_get_field32(word, RXWI_W0_UDF);
	rxdesc->size = rt2x00_get_field32(word, RXWI_W0_MPDU_TOTAL_BYTE_COUNT);

	rt2x00_desc_read(rxwi, 1, &word);

	if (rt2x00_get_field32(word, RXWI_W1_SHORT_GI))
		rxdesc->flags |= RX_FLAG_SHORT_GI;

	if (rt2x00_get_field32(word, RXWI_W1_BW))
		rxdesc->flags |= RX_FLAG_40MHZ;

	/*
	 * Detect RX rate, always use MCS as signal type.
	 */
	rxdesc->dev_flags |= RXDONE_SIGNAL_MCS;
	rxdesc->signal = rt2x00_get_field32(word, RXWI_W1_MCS);
	rxdesc->rate_mode = rt2x00_get_field32(word, RXWI_W1_PHYMODE);

	/*
	 * Mask of 0x8 bit to remove the short preamble flag.
	 */
	if (rxdesc->rate_mode == RATE_MODE_CCK)
		rxdesc->signal &= ~0x8;

	rt2x00_desc_read(rxwi, 2, &word);

	/*
	 * Convert descriptor AGC value to RSSI value.
	 */
	rxdesc->rssi = rt2800_agc_to_rssi(entry->queue->rt2x00dev, word);

	/*
	 * Remove RXWI descriptor from start of buffer.
	 */
	skb_pull(entry->skb, RXWI_DESC_SIZE);
}
EXPORT_SYMBOL_GPL(rt2800_process_rxwi);

void rt2800_txdone_entry(struct queue_entry *entry, u32 status, __le32 *txwi)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	struct txdone_entry_desc txdesc;
	u32 word;
	u16 mcs, real_mcs;
	int aggr, ampdu;

	/*
	 * Obtain the status about this packet.
	 */
	txdesc.flags = 0;
	rt2x00_desc_read(txwi, 0, &word);

	mcs = rt2x00_get_field32(word, TXWI_W0_MCS);
	ampdu = rt2x00_get_field32(word, TXWI_W0_AMPDU);

	real_mcs = rt2x00_get_field32(status, TX_STA_FIFO_MCS);
	aggr = rt2x00_get_field32(status, TX_STA_FIFO_TX_AGGRE);

	/*
	 * If a frame was meant to be sent as a single non-aggregated MPDU
	 * but ended up in an aggregate the used tx rate doesn't correlate
	 * with the one specified in the TXWI as the whole aggregate is sent
	 * with the same rate.
	 *
	 * For example: two frames are sent to rt2x00, the first one sets
	 * AMPDU=1 and requests MCS7 whereas the second frame sets AMDPU=0
	 * and requests MCS15. If the hw aggregates both frames into one
	 * AMDPU the tx status for both frames will contain MCS7 although
	 * the frame was sent successfully.
	 *
	 * Hence, replace the requested rate with the real tx rate to not
	 * confuse the rate control algortihm by providing clearly wrong
	 * data.
	 */
	if (unlikely(aggr == 1 && ampdu == 0 && real_mcs != mcs)) {
		skbdesc->tx_rate_idx = real_mcs;
		mcs = real_mcs;
	}

	if (aggr == 1 || ampdu == 1)
		__set_bit(TXDONE_AMPDU, &txdesc.flags);

	/*
	 * Ralink has a retry mechanism using a global fallback
	 * table. We setup this fallback table to try the immediate
	 * lower rate for all rates. In the TX_STA_FIFO, the MCS field
	 * always contains the MCS used for the last transmission, be
	 * it successful or not.
	 */
	if (rt2x00_get_field32(status, TX_STA_FIFO_TX_SUCCESS)) {
		/*
		 * Transmission succeeded. The number of retries is
		 * mcs - real_mcs
		 */
		__set_bit(TXDONE_SUCCESS, &txdesc.flags);
		txdesc.retry = ((mcs > real_mcs) ? mcs - real_mcs : 0);
	} else {
		/*
		 * Transmission failed. The number of retries is
		 * always 7 in this case (for a total number of 8
		 * frames sent).
		 */
		__set_bit(TXDONE_FAILURE, &txdesc.flags);
		txdesc.retry = rt2x00dev->long_retry;
	}

	/*
	 * the frame was retried at least once
	 * -> hw used fallback rates
	 */
	if (txdesc.retry)
		__set_bit(TXDONE_FALLBACK, &txdesc.flags);

	rt2x00lib_txdone(entry, &txdesc);
}
EXPORT_SYMBOL_GPL(rt2800_txdone_entry);

void rt2800_write_beacon(struct queue_entry *entry, struct txentry_desc *txdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	unsigned int beacon_base;
	unsigned int padding_len;
	u32 orig_reg, reg;

	/*
	 * Disable beaconing while we are reloading the beacon data,
	 * otherwise we might be sending out invalid data.
	 */
	rt2800_register_read(rt2x00dev, BCN_TIME_CFG, &reg);
	orig_reg = reg;
	rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 0);
	rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);

	/*
	 * Add space for the TXWI in front of the skb.
	 */
	memset(skb_push(entry->skb, TXWI_DESC_SIZE), 0, TXWI_DESC_SIZE);

	/*
	 * Register descriptor details in skb frame descriptor.
	 */
	skbdesc->flags |= SKBDESC_DESC_IN_SKB;
	skbdesc->desc = entry->skb->data;
	skbdesc->desc_len = TXWI_DESC_SIZE;

	/*
	 * Add the TXWI for the beacon to the skb.
	 */
	rt2800_write_tx_data(entry, txdesc);

	/*
	 * Dump beacon to userspace through debugfs.
	 */
	rt2x00debug_dump_frame(rt2x00dev, DUMP_FRAME_BEACON, entry->skb);

	/*
	 * Write entire beacon with TXWI and padding to register.
	 */
	padding_len = roundup(entry->skb->len, 4) - entry->skb->len;
	if (padding_len && skb_pad(entry->skb, padding_len)) {
		ERROR(rt2x00dev, "Failure padding beacon, aborting\n");
		/* skb freed by skb_pad() on failure */
		entry->skb = NULL;
		rt2800_register_write(rt2x00dev, BCN_TIME_CFG, orig_reg);
		return;
	}

	beacon_base = HW_BEACON_OFFSET(entry->entry_idx);
	rt2800_register_multiwrite(rt2x00dev, beacon_base, entry->skb->data,
				   entry->skb->len + padding_len);

	/*
	 * Enable beaconing again.
	 */
	rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 1);
	rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);

	/*
	 * Clean up beacon skb.
	 */
	dev_kfree_skb_any(entry->skb);
	entry->skb = NULL;
}
EXPORT_SYMBOL_GPL(rt2800_write_beacon);

static inline void rt2800_clear_beacon_register(struct rt2x00_dev *rt2x00dev,
						unsigned int beacon_base)
{
	int i;

	/*
	 * For the Beacon base registers we only need to clear
	 * the whole TXWI which (when set to 0) will invalidate
	 * the entire beacon.
	 */
	for (i = 0; i < TXWI_DESC_SIZE; i += sizeof(__le32))
		rt2800_register_write(rt2x00dev, beacon_base + i, 0);
}

void rt2800_clear_beacon(struct queue_entry *entry)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	u32 reg;

	/*
	 * Disable beaconing while we are reloading the beacon data,
	 * otherwise we might be sending out invalid data.
	 */
	rt2800_register_read(rt2x00dev, BCN_TIME_CFG, &reg);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 0);
	rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);

	/*
	 * Clear beacon.
	 */
	rt2800_clear_beacon_register(rt2x00dev,
				     HW_BEACON_OFFSET(entry->entry_idx));

	/*
	 * Enabled beaconing again.
	 */
	rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 1);
	rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);
}
EXPORT_SYMBOL_GPL(rt2800_clear_beacon);

#ifdef CONFIG_RT2X00_LIB_DEBUGFS
const struct rt2x00debug rt2800_rt2x00debug = {
	.owner	= THIS_MODULE,
	.csr	= {
		.read		= rt2800_register_read,
		.write		= rt2800_register_write,
		.flags		= RT2X00DEBUGFS_OFFSET,
		.word_base	= CSR_REG_BASE,
		.word_size	= sizeof(u32),
		.word_count	= CSR_REG_SIZE / sizeof(u32),
	},
	.eeprom	= {
		.read		= rt2x00_eeprom_read,
		.write		= rt2x00_eeprom_write,
		.word_base	= EEPROM_BASE,
		.word_size	= sizeof(u16),
		.word_count	= EEPROM_SIZE / sizeof(u16),
	},
	.bbp	= {
		.read		= rt2800_bbp_read,
		.write		= rt2800_bbp_write,
		.word_base	= BBP_BASE,
		.word_size	= sizeof(u8),
		.word_count	= BBP_SIZE / sizeof(u8),
	},
	.rf	= {
		.read		= rt2x00_rf_read,
		.write		= rt2800_rf_write,
		.word_base	= RF_BASE,
		.word_size	= sizeof(u32),
		.word_count	= RF_SIZE / sizeof(u32),
	},
};
EXPORT_SYMBOL_GPL(rt2800_rt2x00debug);
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */

int rt2800_rfkill_poll(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2800_register_read(rt2x00dev, GPIO_CTRL_CFG, &reg);
	return rt2x00_get_field32(reg, GPIO_CTRL_CFG_BIT2);
}
EXPORT_SYMBOL_GPL(rt2800_rfkill_poll);

#ifdef CONFIG_RT2X00_LIB_LEDS
static void rt2800_brightness_set(struct led_classdev *led_cdev,
				  enum led_brightness brightness)
{
	struct rt2x00_led *led =
	    container_of(led_cdev, struct rt2x00_led, led_dev);
	unsigned int enabled = brightness != LED_OFF;
	unsigned int bg_mode =
	    (enabled && led->rt2x00dev->curr_band == IEEE80211_BAND_2GHZ);
	unsigned int polarity =
		rt2x00_get_field16(led->rt2x00dev->led_mcu_reg,
				   EEPROM_FREQ_LED_POLARITY);
	unsigned int ledmode =
		rt2x00_get_field16(led->rt2x00dev->led_mcu_reg,
				   EEPROM_FREQ_LED_MODE);
	u32 reg;

	/* Check for SoC (SOC devices don't support MCU requests) */
	if (rt2x00_is_soc(led->rt2x00dev)) {
		rt2800_register_read(led->rt2x00dev, LED_CFG, &reg);

		/* Set LED Polarity */
		rt2x00_set_field32(&reg, LED_CFG_LED_POLAR, polarity);

		/* Set LED Mode */
		if (led->type == LED_TYPE_RADIO) {
			rt2x00_set_field32(&reg, LED_CFG_G_LED_MODE,
					   enabled ? 3 : 0);
		} else if (led->type == LED_TYPE_ASSOC) {
			rt2x00_set_field32(&reg, LED_CFG_Y_LED_MODE,
					   enabled ? 3 : 0);
		} else if (led->type == LED_TYPE_QUALITY) {
			rt2x00_set_field32(&reg, LED_CFG_R_LED_MODE,
					   enabled ? 3 : 0);
		}

		rt2800_register_write(led->rt2x00dev, LED_CFG, reg);

	} else {
		if (led->type == LED_TYPE_RADIO) {
			rt2800_mcu_request(led->rt2x00dev, MCU_LED, 0xff, ledmode,
					      enabled ? 0x20 : 0);
		} else if (led->type == LED_TYPE_ASSOC) {
			rt2800_mcu_request(led->rt2x00dev, MCU_LED, 0xff, ledmode,
					      enabled ? (bg_mode ? 0x60 : 0xa0) : 0x20);
		} else if (led->type == LED_TYPE_QUALITY) {
			/*
			 * The brightness is divided into 6 levels (0 - 5),
			 * The specs tell us the following levels:
			 *	0, 1 ,3, 7, 15, 31
			 * to determine the level in a simple way we can simply
			 * work with bitshifting:
			 *	(1 << level) - 1
			 */
			rt2800_mcu_request(led->rt2x00dev, MCU_LED_STRENGTH, 0xff,
					      (1 << brightness / (LED_FULL / 6)) - 1,
					      polarity);
		}
	}
}

static void rt2800_init_led(struct rt2x00_dev *rt2x00dev,
		     struct rt2x00_led *led, enum led_type type)
{
	led->rt2x00dev = rt2x00dev;
	led->type = type;
	led->led_dev.brightness_set = rt2800_brightness_set;
	led->flags = LED_INITIALIZED;
}
#endif /* CONFIG_RT2X00_LIB_LEDS */

/*
 * Configuration handlers.
 */
static void rt2800_config_wcid(struct rt2x00_dev *rt2x00dev,
			       const u8 *address,
			       int wcid)
{
	struct mac_wcid_entry wcid_entry;
	u32 offset;

	offset = MAC_WCID_ENTRY(wcid);

	memset(&wcid_entry, 0xff, sizeof(wcid_entry));
	if (address)
		memcpy(wcid_entry.mac, address, ETH_ALEN);

	rt2800_register_multiwrite(rt2x00dev, offset,
				      &wcid_entry, sizeof(wcid_entry));
}

static void rt2800_delete_wcid_attr(struct rt2x00_dev *rt2x00dev, int wcid)
{
	u32 offset;
	offset = MAC_WCID_ATTR_ENTRY(wcid);
	rt2800_register_write(rt2x00dev, offset, 0);
}

static void rt2800_config_wcid_attr_bssidx(struct rt2x00_dev *rt2x00dev,
					   int wcid, u32 bssidx)
{
	u32 offset = MAC_WCID_ATTR_ENTRY(wcid);
	u32 reg;

	/*
	 * The BSS Idx numbers is split in a main value of 3 bits,
	 * and a extended field for adding one additional bit to the value.
	 */
	rt2800_register_read(rt2x00dev, offset, &reg);
	rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_BSS_IDX, (bssidx & 0x7));
	rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_BSS_IDX_EXT,
			   (bssidx & 0x8) >> 3);
	rt2800_register_write(rt2x00dev, offset, reg);
}

static void rt2800_config_wcid_attr_cipher(struct rt2x00_dev *rt2x00dev,
					   struct rt2x00lib_crypto *crypto,
					   struct ieee80211_key_conf *key)
{
	struct mac_iveiv_entry iveiv_entry;
	u32 offset;
	u32 reg;

	offset = MAC_WCID_ATTR_ENTRY(key->hw_key_idx);

	if (crypto->cmd == SET_KEY) {
		rt2800_register_read(rt2x00dev, offset, &reg);
		rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_KEYTAB,
				   !!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE));
		/*
		 * Both the cipher as the BSS Idx numbers are split in a main
		 * value of 3 bits, and a extended field for adding one additional
		 * bit to the value.
		 */
		rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_CIPHER,
				   (crypto->cipher & 0x7));
		rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_CIPHER_EXT,
				   (crypto->cipher & 0x8) >> 3);
		rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_RX_WIUDF, crypto->cipher);
		rt2800_register_write(rt2x00dev, offset, reg);
	} else {
		/* Delete the cipher without touching the bssidx */
		rt2800_register_read(rt2x00dev, offset, &reg);
		rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_KEYTAB, 0);
		rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_CIPHER, 0);
		rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_CIPHER_EXT, 0);
		rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_RX_WIUDF, 0);
		rt2800_register_write(rt2x00dev, offset, reg);
	}

	offset = MAC_IVEIV_ENTRY(key->hw_key_idx);

	memset(&iveiv_entry, 0, sizeof(iveiv_entry));
	if ((crypto->cipher == CIPHER_TKIP) ||
	    (crypto->cipher == CIPHER_TKIP_NO_MIC) ||
	    (crypto->cipher == CIPHER_AES))
		iveiv_entry.iv[3] |= 0x20;
	iveiv_entry.iv[3] |= key->keyidx << 6;
	rt2800_register_multiwrite(rt2x00dev, offset,
				      &iveiv_entry, sizeof(iveiv_entry));
}

int rt2800_config_shared_key(struct rt2x00_dev *rt2x00dev,
			     struct rt2x00lib_crypto *crypto,
			     struct ieee80211_key_conf *key)
{
	struct hw_key_entry key_entry;
	struct rt2x00_field32 field;
	u32 offset;
	u32 reg;

	if (crypto->cmd == SET_KEY) {
		key->hw_key_idx = (4 * crypto->bssidx) + key->keyidx;

		memcpy(key_entry.key, crypto->key,
		       sizeof(key_entry.key));
		memcpy(key_entry.tx_mic, crypto->tx_mic,
		       sizeof(key_entry.tx_mic));
		memcpy(key_entry.rx_mic, crypto->rx_mic,
		       sizeof(key_entry.rx_mic));

		offset = SHARED_KEY_ENTRY(key->hw_key_idx);
		rt2800_register_multiwrite(rt2x00dev, offset,
					      &key_entry, sizeof(key_entry));
	}

	/*
	 * The cipher types are stored over multiple registers
	 * starting with SHARED_KEY_MODE_BASE each word will have
	 * 32 bits and contains the cipher types for 2 bssidx each.
	 * Using the correct defines correctly will cause overhead,
	 * so just calculate the correct offset.
	 */
	field.bit_offset = 4 * (key->hw_key_idx % 8);
	field.bit_mask = 0x7 << field.bit_offset;

	offset = SHARED_KEY_MODE_ENTRY(key->hw_key_idx / 8);

	rt2800_register_read(rt2x00dev, offset, &reg);
	rt2x00_set_field32(&reg, field,
			   (crypto->cmd == SET_KEY) * crypto->cipher);
	rt2800_register_write(rt2x00dev, offset, reg);

	/*
	 * Update WCID information
	 */
	rt2800_config_wcid(rt2x00dev, crypto->address, key->hw_key_idx);
	rt2800_config_wcid_attr_bssidx(rt2x00dev, key->hw_key_idx,
				       crypto->bssidx);
	rt2800_config_wcid_attr_cipher(rt2x00dev, crypto, key);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_config_shared_key);

static inline int rt2800_find_wcid(struct rt2x00_dev *rt2x00dev)
{
	struct mac_wcid_entry wcid_entry;
	int idx;
	u32 offset;

	/*
	 * Search for the first free WCID entry and return the corresponding
	 * index.
	 *
	 * Make sure the WCID starts _after_ the last possible shared key
	 * entry (>32).
	 *
	 * Since parts of the pairwise key table might be shared with
	 * the beacon frame buffers 6 & 7 we should only write into the
	 * first 222 entries.
	 */
	for (idx = 33; idx <= 222; idx++) {
		offset = MAC_WCID_ENTRY(idx);
		rt2800_register_multiread(rt2x00dev, offset, &wcid_entry,
					  sizeof(wcid_entry));
		if (is_broadcast_ether_addr(wcid_entry.mac))
			return idx;
	}

	/*
	 * Use -1 to indicate that we don't have any more space in the WCID
	 * table.
	 */
	return -1;
}

int rt2800_config_pairwise_key(struct rt2x00_dev *rt2x00dev,
			       struct rt2x00lib_crypto *crypto,
			       struct ieee80211_key_conf *key)
{
	struct hw_key_entry key_entry;
	u32 offset;

	if (crypto->cmd == SET_KEY) {
		/*
		 * Allow key configuration only for STAs that are
		 * known by the hw.
		 */
		if (crypto->wcid < 0)
			return -ENOSPC;
		key->hw_key_idx = crypto->wcid;

		memcpy(key_entry.key, crypto->key,
		       sizeof(key_entry.key));
		memcpy(key_entry.tx_mic, crypto->tx_mic,
		       sizeof(key_entry.tx_mic));
		memcpy(key_entry.rx_mic, crypto->rx_mic,
		       sizeof(key_entry.rx_mic));

		offset = PAIRWISE_KEY_ENTRY(key->hw_key_idx);
		rt2800_register_multiwrite(rt2x00dev, offset,
					      &key_entry, sizeof(key_entry));
	}

	/*
	 * Update WCID information
	 */
	rt2800_config_wcid_attr_cipher(rt2x00dev, crypto, key);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_config_pairwise_key);

int rt2800_sta_add(struct rt2x00_dev *rt2x00dev, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta)
{
	int wcid;
	struct rt2x00_sta *sta_priv = sta_to_rt2x00_sta(sta);

	/*
	 * Find next free WCID.
	 */
	wcid = rt2800_find_wcid(rt2x00dev);

	/*
	 * Store selected wcid even if it is invalid so that we can
	 * later decide if the STA is uploaded into the hw.
	 */
	sta_priv->wcid = wcid;

	/*
	 * No space left in the device, however, we can still communicate
	 * with the STA -> No error.
	 */
	if (wcid < 0)
		return 0;

	/*
	 * Clean up WCID attributes and write STA address to the device.
	 */
	rt2800_delete_wcid_attr(rt2x00dev, wcid);
	rt2800_config_wcid(rt2x00dev, sta->addr, wcid);
	rt2800_config_wcid_attr_bssidx(rt2x00dev, wcid,
				       rt2x00lib_get_bssidx(rt2x00dev, vif));
	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_sta_add);

int rt2800_sta_remove(struct rt2x00_dev *rt2x00dev, int wcid)
{
	/*
	 * Remove WCID entry, no need to clean the attributes as they will
	 * get renewed when the WCID is reused.
	 */
	rt2800_config_wcid(rt2x00dev, NULL, wcid);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_sta_remove);

void rt2800_config_filter(struct rt2x00_dev *rt2x00dev,
			  const unsigned int filter_flags)
{
	u32 reg;

	/*
	 * Start configuration steps.
	 * Note that the version error will always be dropped
	 * and broadcast frames will always be accepted since
	 * there is no filter for it at this time.
	 */
	rt2800_register_read(rt2x00dev, RX_FILTER_CFG, &reg);
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_CRC_ERROR,
			   !(filter_flags & FIF_FCSFAIL));
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_PHY_ERROR,
			   !(filter_flags & FIF_PLCPFAIL));
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_NOT_TO_ME,
			   !(filter_flags & FIF_PROMISC_IN_BSS));
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_NOT_MY_BSSD, 0);
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_VER_ERROR, 1);
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_MULTICAST,
			   !(filter_flags & FIF_ALLMULTI));
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_BROADCAST, 0);
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_DUPLICATE, 1);
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_CF_END_ACK,
			   !(filter_flags & FIF_CONTROL));
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_CF_END,
			   !(filter_flags & FIF_CONTROL));
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_ACK,
			   !(filter_flags & FIF_CONTROL));
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_CTS,
			   !(filter_flags & FIF_CONTROL));
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_RTS,
			   !(filter_flags & FIF_CONTROL));
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_PSPOLL,
			   !(filter_flags & FIF_PSPOLL));
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_BA,
			   !(filter_flags & FIF_CONTROL));
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_BAR,
			   !(filter_flags & FIF_CONTROL));
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_CNTL,
			   !(filter_flags & FIF_CONTROL));
	rt2800_register_write(rt2x00dev, RX_FILTER_CFG, reg);
}
EXPORT_SYMBOL_GPL(rt2800_config_filter);

void rt2800_config_intf(struct rt2x00_dev *rt2x00dev, struct rt2x00_intf *intf,
			struct rt2x00intf_conf *conf, const unsigned int flags)
{
	u32 reg;
	bool update_bssid = false;

	if (flags & CONFIG_UPDATE_TYPE) {
		/*
		 * Enable synchronisation.
		 */
		rt2800_register_read(rt2x00dev, BCN_TIME_CFG, &reg);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TSF_SYNC, conf->sync);
		rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);

		if (conf->sync == TSF_SYNC_AP_NONE) {
			/*
			 * Tune beacon queue transmit parameters for AP mode
			 */
			rt2800_register_read(rt2x00dev, TBTT_SYNC_CFG, &reg);
			rt2x00_set_field32(&reg, TBTT_SYNC_CFG_BCN_CWMIN, 0);
			rt2x00_set_field32(&reg, TBTT_SYNC_CFG_BCN_AIFSN, 1);
			rt2x00_set_field32(&reg, TBTT_SYNC_CFG_BCN_EXP_WIN, 32);
			rt2x00_set_field32(&reg, TBTT_SYNC_CFG_TBTT_ADJUST, 0);
			rt2800_register_write(rt2x00dev, TBTT_SYNC_CFG, reg);
		} else {
			rt2800_register_read(rt2x00dev, TBTT_SYNC_CFG, &reg);
			rt2x00_set_field32(&reg, TBTT_SYNC_CFG_BCN_CWMIN, 4);
			rt2x00_set_field32(&reg, TBTT_SYNC_CFG_BCN_AIFSN, 2);
			rt2x00_set_field32(&reg, TBTT_SYNC_CFG_BCN_EXP_WIN, 32);
			rt2x00_set_field32(&reg, TBTT_SYNC_CFG_TBTT_ADJUST, 16);
			rt2800_register_write(rt2x00dev, TBTT_SYNC_CFG, reg);
		}
	}

	if (flags & CONFIG_UPDATE_MAC) {
		if (flags & CONFIG_UPDATE_TYPE &&
		    conf->sync == TSF_SYNC_AP_NONE) {
			/*
			 * The BSSID register has to be set to our own mac
			 * address in AP mode.
			 */
			memcpy(conf->bssid, conf->mac, sizeof(conf->mac));
			update_bssid = true;
		}

		if (!is_zero_ether_addr((const u8 *)conf->mac)) {
			reg = le32_to_cpu(conf->mac[1]);
			rt2x00_set_field32(&reg, MAC_ADDR_DW1_UNICAST_TO_ME_MASK, 0xff);
			conf->mac[1] = cpu_to_le32(reg);
		}

		rt2800_register_multiwrite(rt2x00dev, MAC_ADDR_DW0,
					      conf->mac, sizeof(conf->mac));
	}

	if ((flags & CONFIG_UPDATE_BSSID) || update_bssid) {
		if (!is_zero_ether_addr((const u8 *)conf->bssid)) {
			reg = le32_to_cpu(conf->bssid[1]);
			rt2x00_set_field32(&reg, MAC_BSSID_DW1_BSS_ID_MASK, 3);
			rt2x00_set_field32(&reg, MAC_BSSID_DW1_BSS_BCN_NUM, 7);
			conf->bssid[1] = cpu_to_le32(reg);
		}

		rt2800_register_multiwrite(rt2x00dev, MAC_BSSID_DW0,
					      conf->bssid, sizeof(conf->bssid));
	}
}
EXPORT_SYMBOL_GPL(rt2800_config_intf);

static void rt2800_config_ht_opmode(struct rt2x00_dev *rt2x00dev,
				    struct rt2x00lib_erp *erp)
{
	bool any_sta_nongf = !!(erp->ht_opmode &
				IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
	u8 protection = erp->ht_opmode & IEEE80211_HT_OP_MODE_PROTECTION;
	u8 mm20_mode, mm40_mode, gf20_mode, gf40_mode;
	u16 mm20_rate, mm40_rate, gf20_rate, gf40_rate;
	u32 reg;

	/* default protection rate for HT20: OFDM 24M */
	mm20_rate = gf20_rate = 0x4004;

	/* default protection rate for HT40: duplicate OFDM 24M */
	mm40_rate = gf40_rate = 0x4084;

	switch (protection) {
	case IEEE80211_HT_OP_MODE_PROTECTION_NONE:
		/*
		 * All STAs in this BSS are HT20/40 but there might be
		 * STAs not supporting greenfield mode.
		 * => Disable protection for HT transmissions.
		 */
		mm20_mode = mm40_mode = gf20_mode = gf40_mode = 0;

		break;
	case IEEE80211_HT_OP_MODE_PROTECTION_20MHZ:
		/*
		 * All STAs in this BSS are HT20 or HT20/40 but there
		 * might be STAs not supporting greenfield mode.
		 * => Protect all HT40 transmissions.
		 */
		mm20_mode = gf20_mode = 0;
		mm40_mode = gf40_mode = 2;

		break;
	case IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER:
		/*
		 * Nonmember protection:
		 * According to 802.11n we _should_ protect all
		 * HT transmissions (but we don't have to).
		 *
		 * But if cts_protection is enabled we _shall_ protect
		 * all HT transmissions using a CCK rate.
		 *
		 * And if any station is non GF we _shall_ protect
		 * GF transmissions.
		 *
		 * We decide to protect everything
		 * -> fall through to mixed mode.
		 */
	case IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED:
		/*
		 * Legacy STAs are present
		 * => Protect all HT transmissions.
		 */
		mm20_mode = mm40_mode = gf20_mode = gf40_mode = 2;

		/*
		 * If erp protection is needed we have to protect HT
		 * transmissions with CCK 11M long preamble.
		 */
		if (erp->cts_protection) {
			/* don't duplicate RTS/CTS in CCK mode */
			mm20_rate = mm40_rate = 0x0003;
			gf20_rate = gf40_rate = 0x0003;
		}
		break;
	}

	/* check for STAs not supporting greenfield mode */
	if (any_sta_nongf)
		gf20_mode = gf40_mode = 2;

	/* Update HT protection config */
	rt2800_register_read(rt2x00dev, MM20_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_PROTECT_RATE, mm20_rate);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_PROTECT_CTRL, mm20_mode);
	rt2800_register_write(rt2x00dev, MM20_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, MM40_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_PROTECT_RATE, mm40_rate);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_PROTECT_CTRL, mm40_mode);
	rt2800_register_write(rt2x00dev, MM40_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, GF20_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_PROTECT_RATE, gf20_rate);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_PROTECT_CTRL, gf20_mode);
	rt2800_register_write(rt2x00dev, GF20_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, GF40_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_PROTECT_RATE, gf40_rate);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_PROTECT_CTRL, gf40_mode);
	rt2800_register_write(rt2x00dev, GF40_PROT_CFG, reg);
}

void rt2800_config_erp(struct rt2x00_dev *rt2x00dev, struct rt2x00lib_erp *erp,
		       u32 changed)
{
	u32 reg;

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		rt2800_register_read(rt2x00dev, AUTO_RSP_CFG, &reg);
		rt2x00_set_field32(&reg, AUTO_RSP_CFG_BAC_ACK_POLICY,
				   !!erp->short_preamble);
		rt2x00_set_field32(&reg, AUTO_RSP_CFG_AR_PREAMBLE,
				   !!erp->short_preamble);
		rt2800_register_write(rt2x00dev, AUTO_RSP_CFG, reg);
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		rt2800_register_read(rt2x00dev, OFDM_PROT_CFG, &reg);
		rt2x00_set_field32(&reg, OFDM_PROT_CFG_PROTECT_CTRL,
				   erp->cts_protection ? 2 : 0);
		rt2800_register_write(rt2x00dev, OFDM_PROT_CFG, reg);
	}

	if (changed & BSS_CHANGED_BASIC_RATES) {
		rt2800_register_write(rt2x00dev, LEGACY_BASIC_RATE,
					 erp->basic_rates);
		rt2800_register_write(rt2x00dev, HT_BASIC_RATE, 0x00008003);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		rt2800_register_read(rt2x00dev, BKOFF_SLOT_CFG, &reg);
		rt2x00_set_field32(&reg, BKOFF_SLOT_CFG_SLOT_TIME,
				   erp->slot_time);
		rt2800_register_write(rt2x00dev, BKOFF_SLOT_CFG, reg);

		rt2800_register_read(rt2x00dev, XIFS_TIME_CFG, &reg);
		rt2x00_set_field32(&reg, XIFS_TIME_CFG_EIFS, erp->eifs);
		rt2800_register_write(rt2x00dev, XIFS_TIME_CFG, reg);
	}

	if (changed & BSS_CHANGED_BEACON_INT) {
		rt2800_register_read(rt2x00dev, BCN_TIME_CFG, &reg);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_INTERVAL,
				   erp->beacon_int * 16);
		rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);
	}

	if (changed & BSS_CHANGED_HT)
		rt2800_config_ht_opmode(rt2x00dev, erp);
}
EXPORT_SYMBOL_GPL(rt2800_config_erp);

static void rt2800_config_3572bt_ant(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	u16 eeprom;
	u8 led_ctrl, led_g_mode, led_r_mode;

	rt2800_register_read(rt2x00dev, GPIO_SWITCH, &reg);
	if (rt2x00dev->curr_band == IEEE80211_BAND_5GHZ) {
		rt2x00_set_field32(&reg, GPIO_SWITCH_0, 1);
		rt2x00_set_field32(&reg, GPIO_SWITCH_1, 1);
	} else {
		rt2x00_set_field32(&reg, GPIO_SWITCH_0, 0);
		rt2x00_set_field32(&reg, GPIO_SWITCH_1, 0);
	}
	rt2800_register_write(rt2x00dev, GPIO_SWITCH, reg);

	rt2800_register_read(rt2x00dev, LED_CFG, &reg);
	led_g_mode = rt2x00_get_field32(reg, LED_CFG_LED_POLAR) ? 3 : 0;
	led_r_mode = rt2x00_get_field32(reg, LED_CFG_LED_POLAR) ? 0 : 3;
	if (led_g_mode != rt2x00_get_field32(reg, LED_CFG_G_LED_MODE) ||
	    led_r_mode != rt2x00_get_field32(reg, LED_CFG_R_LED_MODE)) {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_FREQ, &eeprom);
		led_ctrl = rt2x00_get_field16(eeprom, EEPROM_FREQ_LED_MODE);
		if (led_ctrl == 0 || led_ctrl > 0x40) {
			rt2x00_set_field32(&reg, LED_CFG_G_LED_MODE, led_g_mode);
			rt2x00_set_field32(&reg, LED_CFG_R_LED_MODE, led_r_mode);
			rt2800_register_write(rt2x00dev, LED_CFG, reg);
		} else {
			rt2800_mcu_request(rt2x00dev, MCU_BAND_SELECT, 0xff,
					   (led_g_mode << 2) | led_r_mode, 1);
		}
	}
}

static void rt2800_set_ant_diversity(struct rt2x00_dev *rt2x00dev,
				     enum antenna ant)
{
	u32 reg;
	u8 eesk_pin = (ant == ANTENNA_A) ? 1 : 0;
	u8 gpio_bit3 = (ant == ANTENNA_A) ? 0 : 1;

	if (rt2x00_is_pci(rt2x00dev)) {
		rt2800_register_read(rt2x00dev, E2PROM_CSR, &reg);
		rt2x00_set_field32(&reg, E2PROM_CSR_DATA_CLOCK, eesk_pin);
		rt2800_register_write(rt2x00dev, E2PROM_CSR, reg);
	} else if (rt2x00_is_usb(rt2x00dev))
		rt2800_mcu_request(rt2x00dev, MCU_ANT_SELECT, 0xff,
				   eesk_pin, 0);

	rt2800_register_read(rt2x00dev, GPIO_CTRL_CFG, &reg);
	rt2x00_set_field32(&reg, GPIO_CTRL_CFG_GPIOD_BIT3, 0);
	rt2x00_set_field32(&reg, GPIO_CTRL_CFG_BIT3, gpio_bit3);
	rt2800_register_write(rt2x00dev, GPIO_CTRL_CFG, reg);
}

void rt2800_config_ant(struct rt2x00_dev *rt2x00dev, struct antenna_setup *ant)
{
	u8 r1;
	u8 r3;
	u16 eeprom;

	rt2800_bbp_read(rt2x00dev, 1, &r1);
	rt2800_bbp_read(rt2x00dev, 3, &r3);

	if (rt2x00_rt(rt2x00dev, RT3572) &&
	    test_bit(CAPABILITY_BT_COEXIST, &rt2x00dev->cap_flags))
		rt2800_config_3572bt_ant(rt2x00dev);

	/*
	 * Configure the TX antenna.
	 */
	switch (ant->tx_chain_num) {
	case 1:
		rt2x00_set_field8(&r1, BBP1_TX_ANTENNA, 0);
		break;
	case 2:
		if (rt2x00_rt(rt2x00dev, RT3572) &&
		    test_bit(CAPABILITY_BT_COEXIST, &rt2x00dev->cap_flags))
			rt2x00_set_field8(&r1, BBP1_TX_ANTENNA, 1);
		else
			rt2x00_set_field8(&r1, BBP1_TX_ANTENNA, 2);
		break;
	case 3:
		rt2x00_set_field8(&r1, BBP1_TX_ANTENNA, 0);
		break;
	}

	/*
	 * Configure the RX antenna.
	 */
	switch (ant->rx_chain_num) {
	case 1:
		if (rt2x00_rt(rt2x00dev, RT3070) ||
		    rt2x00_rt(rt2x00dev, RT3090) ||
		    rt2x00_rt(rt2x00dev, RT3390)) {
			rt2x00_eeprom_read(rt2x00dev,
					   EEPROM_NIC_CONF1, &eeprom);
			if (rt2x00_get_field16(eeprom,
						EEPROM_NIC_CONF1_ANT_DIVERSITY))
				rt2800_set_ant_diversity(rt2x00dev,
						rt2x00dev->default_ant.rx);
		}
		rt2x00_set_field8(&r3, BBP3_RX_ANTENNA, 0);
		break;
	case 2:
		if (rt2x00_rt(rt2x00dev, RT3572) &&
		    test_bit(CAPABILITY_BT_COEXIST, &rt2x00dev->cap_flags)) {
			rt2x00_set_field8(&r3, BBP3_RX_ADC, 1);
			rt2x00_set_field8(&r3, BBP3_RX_ANTENNA,
				rt2x00dev->curr_band == IEEE80211_BAND_5GHZ);
			rt2800_set_ant_diversity(rt2x00dev, ANTENNA_B);
		} else {
			rt2x00_set_field8(&r3, BBP3_RX_ANTENNA, 1);
		}
		break;
	case 3:
		rt2x00_set_field8(&r3, BBP3_RX_ANTENNA, 2);
		break;
	}

	rt2800_bbp_write(rt2x00dev, 3, r3);
	rt2800_bbp_write(rt2x00dev, 1, r1);
}
EXPORT_SYMBOL_GPL(rt2800_config_ant);

static void rt2800_config_lna_gain(struct rt2x00_dev *rt2x00dev,
				   struct rt2x00lib_conf *libconf)
{
	u16 eeprom;
	short lna_gain;

	if (libconf->rf.channel <= 14) {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_LNA, &eeprom);
		lna_gain = rt2x00_get_field16(eeprom, EEPROM_LNA_BG);
	} else if (libconf->rf.channel <= 64) {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_LNA, &eeprom);
		lna_gain = rt2x00_get_field16(eeprom, EEPROM_LNA_A0);
	} else if (libconf->rf.channel <= 128) {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_BG2, &eeprom);
		lna_gain = rt2x00_get_field16(eeprom, EEPROM_RSSI_BG2_LNA_A1);
	} else {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_A2, &eeprom);
		lna_gain = rt2x00_get_field16(eeprom, EEPROM_RSSI_A2_LNA_A2);
	}

	rt2x00dev->lna_gain = lna_gain;
}

static void rt2800_config_channel_rf2xxx(struct rt2x00_dev *rt2x00dev,
					 struct ieee80211_conf *conf,
					 struct rf_channel *rf,
					 struct channel_info *info)
{
	rt2x00_set_field32(&rf->rf4, RF4_FREQ_OFFSET, rt2x00dev->freq_offset);

	if (rt2x00dev->default_ant.tx_chain_num == 1)
		rt2x00_set_field32(&rf->rf2, RF2_ANTENNA_TX1, 1);

	if (rt2x00dev->default_ant.rx_chain_num == 1) {
		rt2x00_set_field32(&rf->rf2, RF2_ANTENNA_RX1, 1);
		rt2x00_set_field32(&rf->rf2, RF2_ANTENNA_RX2, 1);
	} else if (rt2x00dev->default_ant.rx_chain_num == 2)
		rt2x00_set_field32(&rf->rf2, RF2_ANTENNA_RX2, 1);

	if (rf->channel > 14) {
		/*
		 * When TX power is below 0, we should increase it by 7 to
		 * make it a positive value (Minimum value is -7).
		 * However this means that values between 0 and 7 have
		 * double meaning, and we should set a 7DBm boost flag.
		 */
		rt2x00_set_field32(&rf->rf3, RF3_TXPOWER_A_7DBM_BOOST,
				   (info->default_power1 >= 0));

		if (info->default_power1 < 0)
			info->default_power1 += 7;

		rt2x00_set_field32(&rf->rf3, RF3_TXPOWER_A, info->default_power1);

		rt2x00_set_field32(&rf->rf4, RF4_TXPOWER_A_7DBM_BOOST,
				   (info->default_power2 >= 0));

		if (info->default_power2 < 0)
			info->default_power2 += 7;

		rt2x00_set_field32(&rf->rf4, RF4_TXPOWER_A, info->default_power2);
	} else {
		rt2x00_set_field32(&rf->rf3, RF3_TXPOWER_G, info->default_power1);
		rt2x00_set_field32(&rf->rf4, RF4_TXPOWER_G, info->default_power2);
	}

	rt2x00_set_field32(&rf->rf4, RF4_HT40, conf_is_ht40(conf));

	rt2800_rf_write(rt2x00dev, 1, rf->rf1);
	rt2800_rf_write(rt2x00dev, 2, rf->rf2);
	rt2800_rf_write(rt2x00dev, 3, rf->rf3 & ~0x00000004);
	rt2800_rf_write(rt2x00dev, 4, rf->rf4);

	udelay(200);

	rt2800_rf_write(rt2x00dev, 1, rf->rf1);
	rt2800_rf_write(rt2x00dev, 2, rf->rf2);
	rt2800_rf_write(rt2x00dev, 3, rf->rf3 | 0x00000004);
	rt2800_rf_write(rt2x00dev, 4, rf->rf4);

	udelay(200);

	rt2800_rf_write(rt2x00dev, 1, rf->rf1);
	rt2800_rf_write(rt2x00dev, 2, rf->rf2);
	rt2800_rf_write(rt2x00dev, 3, rf->rf3 & ~0x00000004);
	rt2800_rf_write(rt2x00dev, 4, rf->rf4);
}

static void rt2800_config_channel_rf3xxx(struct rt2x00_dev *rt2x00dev,
					 struct ieee80211_conf *conf,
					 struct rf_channel *rf,
					 struct channel_info *info)
{
	u8 rfcsr;

	rt2800_rfcsr_write(rt2x00dev, 2, rf->rf1);
	rt2800_rfcsr_write(rt2x00dev, 3, rf->rf3);

	rt2800_rfcsr_read(rt2x00dev, 6, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR6_R1, rf->rf2);
	rt2800_rfcsr_write(rt2x00dev, 6, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 12, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR12_TX_POWER, info->default_power1);
	rt2800_rfcsr_write(rt2x00dev, 12, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 13, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR13_TX_POWER, info->default_power2);
	rt2800_rfcsr_write(rt2x00dev, 13, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 23, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR23_FREQ_OFFSET, rt2x00dev->freq_offset);
	rt2800_rfcsr_write(rt2x00dev, 23, rfcsr);

	rt2800_rfcsr_write(rt2x00dev, 24,
			      rt2x00dev->calibration[conf_is_ht40(conf)]);

	rt2800_rfcsr_read(rt2x00dev, 7, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR7_RF_TUNING, 1);
	rt2800_rfcsr_write(rt2x00dev, 7, rfcsr);
}

static void rt2800_config_channel_rf3052(struct rt2x00_dev *rt2x00dev,
					 struct ieee80211_conf *conf,
					 struct rf_channel *rf,
					 struct channel_info *info)
{
	u8 rfcsr;
	u32 reg;

	if (rf->channel <= 14) {
		rt2800_bbp_write(rt2x00dev, 25, 0x15);
		rt2800_bbp_write(rt2x00dev, 26, 0x85);
	} else {
		rt2800_bbp_write(rt2x00dev, 25, 0x09);
		rt2800_bbp_write(rt2x00dev, 26, 0xff);
	}

	rt2800_rfcsr_write(rt2x00dev, 2, rf->rf1);
	rt2800_rfcsr_write(rt2x00dev, 3, rf->rf3);

	rt2800_rfcsr_read(rt2x00dev, 6, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR6_R1, rf->rf2);
	if (rf->channel <= 14)
		rt2x00_set_field8(&rfcsr, RFCSR6_TXDIV, 2);
	else
		rt2x00_set_field8(&rfcsr, RFCSR6_TXDIV, 1);
	rt2800_rfcsr_write(rt2x00dev, 6, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 5, &rfcsr);
	if (rf->channel <= 14)
		rt2x00_set_field8(&rfcsr, RFCSR5_R1, 1);
	else
		rt2x00_set_field8(&rfcsr, RFCSR5_R1, 2);
	rt2800_rfcsr_write(rt2x00dev, 5, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 12, &rfcsr);
	if (rf->channel <= 14) {
		rt2x00_set_field8(&rfcsr, RFCSR12_DR0, 3);
		rt2x00_set_field8(&rfcsr, RFCSR12_TX_POWER,
				(info->default_power1 & 0x3) |
				((info->default_power1 & 0xC) << 1));
	} else {
		rt2x00_set_field8(&rfcsr, RFCSR12_DR0, 7);
		rt2x00_set_field8(&rfcsr, RFCSR12_TX_POWER,
				(info->default_power1 & 0x3) |
				((info->default_power1 & 0xC) << 1));
	}
	rt2800_rfcsr_write(rt2x00dev, 12, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 13, &rfcsr);
	if (rf->channel <= 14) {
		rt2x00_set_field8(&rfcsr, RFCSR13_DR0, 3);
		rt2x00_set_field8(&rfcsr, RFCSR13_TX_POWER,
				(info->default_power2 & 0x3) |
				((info->default_power2 & 0xC) << 1));
	} else {
		rt2x00_set_field8(&rfcsr, RFCSR13_DR0, 7);
		rt2x00_set_field8(&rfcsr, RFCSR13_TX_POWER,
				(info->default_power2 & 0x3) |
				((info->default_power2 & 0xC) << 1));
	}
	rt2800_rfcsr_write(rt2x00dev, 13, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 1, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR1_RF_BLOCK_EN, 1);
	rt2x00_set_field8(&rfcsr, RFCSR1_RX0_PD, 0);
	rt2x00_set_field8(&rfcsr, RFCSR1_TX0_PD, 0);
	rt2x00_set_field8(&rfcsr, RFCSR1_RX1_PD, 0);
	rt2x00_set_field8(&rfcsr, RFCSR1_TX1_PD, 0);
	if (test_bit(CAPABILITY_BT_COEXIST, &rt2x00dev->cap_flags)) {
		if (rf->channel <= 14) {
			rt2x00_set_field8(&rfcsr, RFCSR1_RX0_PD, 1);
			rt2x00_set_field8(&rfcsr, RFCSR1_TX0_PD, 1);
		}
		rt2x00_set_field8(&rfcsr, RFCSR1_RX2_PD, 1);
		rt2x00_set_field8(&rfcsr, RFCSR1_TX2_PD, 1);
	} else {
		switch (rt2x00dev->default_ant.tx_chain_num) {
		case 1:
			rt2x00_set_field8(&rfcsr, RFCSR1_TX1_PD, 1);
		case 2:
			rt2x00_set_field8(&rfcsr, RFCSR1_TX2_PD, 1);
			break;
		}

		switch (rt2x00dev->default_ant.rx_chain_num) {
		case 1:
			rt2x00_set_field8(&rfcsr, RFCSR1_RX1_PD, 1);
		case 2:
			rt2x00_set_field8(&rfcsr, RFCSR1_RX2_PD, 1);
			break;
		}
	}
	rt2800_rfcsr_write(rt2x00dev, 1, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 23, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR23_FREQ_OFFSET, rt2x00dev->freq_offset);
	rt2800_rfcsr_write(rt2x00dev, 23, rfcsr);

	rt2800_rfcsr_write(rt2x00dev, 24,
			      rt2x00dev->calibration[conf_is_ht40(conf)]);
	rt2800_rfcsr_write(rt2x00dev, 31,
			      rt2x00dev->calibration[conf_is_ht40(conf)]);

	if (rf->channel <= 14) {
		rt2800_rfcsr_write(rt2x00dev, 7, 0xd8);
		rt2800_rfcsr_write(rt2x00dev, 9, 0xc3);
		rt2800_rfcsr_write(rt2x00dev, 10, 0xf1);
		rt2800_rfcsr_write(rt2x00dev, 11, 0xb9);
		rt2800_rfcsr_write(rt2x00dev, 15, 0x53);
		rt2800_rfcsr_write(rt2x00dev, 16, 0x4c);
		rt2800_rfcsr_write(rt2x00dev, 17, 0x23);
		rt2800_rfcsr_write(rt2x00dev, 19, 0x93);
		rt2800_rfcsr_write(rt2x00dev, 20, 0xb3);
		rt2800_rfcsr_write(rt2x00dev, 25, 0x15);
		rt2800_rfcsr_write(rt2x00dev, 26, 0x85);
		rt2800_rfcsr_write(rt2x00dev, 27, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 29, 0x9b);
	} else {
		rt2800_rfcsr_write(rt2x00dev, 7, 0x14);
		rt2800_rfcsr_write(rt2x00dev, 9, 0xc0);
		rt2800_rfcsr_write(rt2x00dev, 10, 0xf1);
		rt2800_rfcsr_write(rt2x00dev, 11, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 15, 0x43);
		rt2800_rfcsr_write(rt2x00dev, 16, 0x7a);
		rt2800_rfcsr_write(rt2x00dev, 17, 0x23);
		if (rf->channel <= 64) {
			rt2800_rfcsr_write(rt2x00dev, 19, 0xb7);
			rt2800_rfcsr_write(rt2x00dev, 20, 0xf6);
			rt2800_rfcsr_write(rt2x00dev, 25, 0x3d);
		} else if (rf->channel <= 128) {
			rt2800_rfcsr_write(rt2x00dev, 19, 0x74);
			rt2800_rfcsr_write(rt2x00dev, 20, 0xf4);
			rt2800_rfcsr_write(rt2x00dev, 25, 0x01);
		} else {
			rt2800_rfcsr_write(rt2x00dev, 19, 0x72);
			rt2800_rfcsr_write(rt2x00dev, 20, 0xf3);
			rt2800_rfcsr_write(rt2x00dev, 25, 0x01);
		}
		rt2800_rfcsr_write(rt2x00dev, 26, 0x87);
		rt2800_rfcsr_write(rt2x00dev, 27, 0x01);
		rt2800_rfcsr_write(rt2x00dev, 29, 0x9f);
	}

	rt2800_register_read(rt2x00dev, GPIO_CTRL_CFG, &reg);
	rt2x00_set_field32(&reg, GPIO_CTRL_CFG_GPIOD_BIT7, 0);
	if (rf->channel <= 14)
		rt2x00_set_field32(&reg, GPIO_CTRL_CFG_BIT7, 1);
	else
		rt2x00_set_field32(&reg, GPIO_CTRL_CFG_BIT7, 0);
	rt2800_register_write(rt2x00dev, GPIO_CTRL_CFG, reg);

	rt2800_rfcsr_read(rt2x00dev, 7, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR7_RF_TUNING, 1);
	rt2800_rfcsr_write(rt2x00dev, 7, rfcsr);
}

#define RT5390_POWER_BOUND     0x27
#define RT5390_FREQ_OFFSET_BOUND       0x5f

static void rt2800_config_channel_rf53xx(struct rt2x00_dev *rt2x00dev,
					 struct ieee80211_conf *conf,
					 struct rf_channel *rf,
					 struct channel_info *info)
{
	u8 rfcsr;

	rt2800_rfcsr_write(rt2x00dev, 8, rf->rf1);
	rt2800_rfcsr_write(rt2x00dev, 9, rf->rf3);
	rt2800_rfcsr_read(rt2x00dev, 11, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR11_R, rf->rf2);
	rt2800_rfcsr_write(rt2x00dev, 11, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 49, &rfcsr);
	if (info->default_power1 > RT5390_POWER_BOUND)
		rt2x00_set_field8(&rfcsr, RFCSR49_TX, RT5390_POWER_BOUND);
	else
		rt2x00_set_field8(&rfcsr, RFCSR49_TX, info->default_power1);
	rt2800_rfcsr_write(rt2x00dev, 49, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 1, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR1_RF_BLOCK_EN, 1);
	rt2x00_set_field8(&rfcsr, RFCSR1_PLL_PD, 1);
	rt2x00_set_field8(&rfcsr, RFCSR1_RX0_PD, 1);
	rt2x00_set_field8(&rfcsr, RFCSR1_TX0_PD, 1);
	rt2800_rfcsr_write(rt2x00dev, 1, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 17, &rfcsr);
	if (rt2x00dev->freq_offset > RT5390_FREQ_OFFSET_BOUND)
		rt2x00_set_field8(&rfcsr, RFCSR17_CODE,
				  RT5390_FREQ_OFFSET_BOUND);
	else
		rt2x00_set_field8(&rfcsr, RFCSR17_CODE, rt2x00dev->freq_offset);
	rt2800_rfcsr_write(rt2x00dev, 17, rfcsr);

	if (rf->channel <= 14) {
		int idx = rf->channel-1;

		if (test_bit(CAPABILITY_BT_COEXIST, &rt2x00dev->cap_flags)) {
			if (rt2x00_rt_rev_gte(rt2x00dev, RT5390, REV_RT5390F)) {
				/* r55/r59 value array of channel 1~14 */
				static const char r55_bt_rev[] = {0x83, 0x83,
					0x83, 0x73, 0x73, 0x63, 0x53, 0x53,
					0x53, 0x43, 0x43, 0x43, 0x43, 0x43};
				static const char r59_bt_rev[] = {0x0e, 0x0e,
					0x0e, 0x0e, 0x0e, 0x0b, 0x0a, 0x09,
					0x07, 0x07, 0x07, 0x07, 0x07, 0x07};

				rt2800_rfcsr_write(rt2x00dev, 55,
						   r55_bt_rev[idx]);
				rt2800_rfcsr_write(rt2x00dev, 59,
						   r59_bt_rev[idx]);
			} else {
				static const char r59_bt[] = {0x8b, 0x8b, 0x8b,
					0x8b, 0x8b, 0x8b, 0x8b, 0x8a, 0x89,
					0x88, 0x88, 0x86, 0x85, 0x84};

				rt2800_rfcsr_write(rt2x00dev, 59, r59_bt[idx]);
			}
		} else {
			if (rt2x00_rt_rev_gte(rt2x00dev, RT5390, REV_RT5390F)) {
				static const char r55_nonbt_rev[] = {0x23, 0x23,
					0x23, 0x23, 0x13, 0x13, 0x03, 0x03,
					0x03, 0x03, 0x03, 0x03, 0x03, 0x03};
				static const char r59_nonbt_rev[] = {0x07, 0x07,
					0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
					0x07, 0x07, 0x06, 0x05, 0x04, 0x04};

				rt2800_rfcsr_write(rt2x00dev, 55,
						   r55_nonbt_rev[idx]);
				rt2800_rfcsr_write(rt2x00dev, 59,
						   r59_nonbt_rev[idx]);
			} else if (rt2x00_rt(rt2x00dev, RT5390)) {
				static const char r59_non_bt[] = {0x8f, 0x8f,
					0x8f, 0x8f, 0x8f, 0x8f, 0x8f, 0x8d,
					0x8a, 0x88, 0x88, 0x87, 0x87, 0x86};

				rt2800_rfcsr_write(rt2x00dev, 59,
						   r59_non_bt[idx]);
			}
		}
	}

	rt2800_rfcsr_read(rt2x00dev, 30, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR30_TX_H20M, 0);
	rt2x00_set_field8(&rfcsr, RFCSR30_RX_H20M, 0);
	rt2800_rfcsr_write(rt2x00dev, 30, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 3, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR30_RF_CALIBRATION, 1);
	rt2800_rfcsr_write(rt2x00dev, 3, rfcsr);
}

static void rt2800_config_channel(struct rt2x00_dev *rt2x00dev,
				  struct ieee80211_conf *conf,
				  struct rf_channel *rf,
				  struct channel_info *info)
{
	u32 reg;
	unsigned int tx_pin;
	u8 bbp;

	if (rf->channel <= 14) {
		info->default_power1 = TXPOWER_G_TO_DEV(info->default_power1);
		info->default_power2 = TXPOWER_G_TO_DEV(info->default_power2);
	} else {
		info->default_power1 = TXPOWER_A_TO_DEV(info->default_power1);
		info->default_power2 = TXPOWER_A_TO_DEV(info->default_power2);
	}

	switch (rt2x00dev->chip.rf) {
	case RF2020:
	case RF3020:
	case RF3021:
	case RF3022:
	case RF3320:
		rt2800_config_channel_rf3xxx(rt2x00dev, conf, rf, info);
		break;
	case RF3052:
		rt2800_config_channel_rf3052(rt2x00dev, conf, rf, info);
		break;
	case RF5370:
	case RF5390:
		rt2800_config_channel_rf53xx(rt2x00dev, conf, rf, info);
		break;
	default:
		rt2800_config_channel_rf2xxx(rt2x00dev, conf, rf, info);
	}

	/*
	 * Change BBP settings
	 */
	rt2800_bbp_write(rt2x00dev, 62, 0x37 - rt2x00dev->lna_gain);
	rt2800_bbp_write(rt2x00dev, 63, 0x37 - rt2x00dev->lna_gain);
	rt2800_bbp_write(rt2x00dev, 64, 0x37 - rt2x00dev->lna_gain);
	rt2800_bbp_write(rt2x00dev, 86, 0);

	if (rf->channel <= 14) {
		if (!rt2x00_rt(rt2x00dev, RT5390)) {
			if (test_bit(CAPABILITY_EXTERNAL_LNA_BG,
				     &rt2x00dev->cap_flags)) {
				rt2800_bbp_write(rt2x00dev, 82, 0x62);
				rt2800_bbp_write(rt2x00dev, 75, 0x46);
			} else {
				rt2800_bbp_write(rt2x00dev, 82, 0x84);
				rt2800_bbp_write(rt2x00dev, 75, 0x50);
			}
		}
	} else {
		if (rt2x00_rt(rt2x00dev, RT3572))
			rt2800_bbp_write(rt2x00dev, 82, 0x94);
		else
			rt2800_bbp_write(rt2x00dev, 82, 0xf2);

		if (test_bit(CAPABILITY_EXTERNAL_LNA_A, &rt2x00dev->cap_flags))
			rt2800_bbp_write(rt2x00dev, 75, 0x46);
		else
			rt2800_bbp_write(rt2x00dev, 75, 0x50);
	}

	rt2800_register_read(rt2x00dev, TX_BAND_CFG, &reg);
	rt2x00_set_field32(&reg, TX_BAND_CFG_HT40_MINUS, conf_is_ht40_minus(conf));
	rt2x00_set_field32(&reg, TX_BAND_CFG_A, rf->channel > 14);
	rt2x00_set_field32(&reg, TX_BAND_CFG_BG, rf->channel <= 14);
	rt2800_register_write(rt2x00dev, TX_BAND_CFG, reg);

	if (rt2x00_rt(rt2x00dev, RT3572))
		rt2800_rfcsr_write(rt2x00dev, 8, 0);

	tx_pin = 0;

	/* Turn on unused PA or LNA when not using 1T or 1R */
	if (rt2x00dev->default_ant.tx_chain_num == 2) {
		rt2x00_set_field32(&tx_pin, TX_PIN_CFG_PA_PE_A1_EN,
				   rf->channel > 14);
		rt2x00_set_field32(&tx_pin, TX_PIN_CFG_PA_PE_G1_EN,
				   rf->channel <= 14);
	}

	/* Turn on unused PA or LNA when not using 1T or 1R */
	if (rt2x00dev->default_ant.rx_chain_num == 2) {
		rt2x00_set_field32(&tx_pin, TX_PIN_CFG_LNA_PE_A1_EN, 1);
		rt2x00_set_field32(&tx_pin, TX_PIN_CFG_LNA_PE_G1_EN, 1);
	}

	rt2x00_set_field32(&tx_pin, TX_PIN_CFG_LNA_PE_A0_EN, 1);
	rt2x00_set_field32(&tx_pin, TX_PIN_CFG_LNA_PE_G0_EN, 1);
	rt2x00_set_field32(&tx_pin, TX_PIN_CFG_RFTR_EN, 1);
	rt2x00_set_field32(&tx_pin, TX_PIN_CFG_TRSW_EN, 1);
	if (test_bit(CAPABILITY_BT_COEXIST, &rt2x00dev->cap_flags))
		rt2x00_set_field32(&tx_pin, TX_PIN_CFG_PA_PE_G0_EN, 1);
	else
		rt2x00_set_field32(&tx_pin, TX_PIN_CFG_PA_PE_G0_EN,
				   rf->channel <= 14);
	rt2x00_set_field32(&tx_pin, TX_PIN_CFG_PA_PE_A0_EN, rf->channel > 14);

	rt2800_register_write(rt2x00dev, TX_PIN_CFG, tx_pin);

	if (rt2x00_rt(rt2x00dev, RT3572))
		rt2800_rfcsr_write(rt2x00dev, 8, 0x80);

	rt2800_bbp_read(rt2x00dev, 4, &bbp);
	rt2x00_set_field8(&bbp, BBP4_BANDWIDTH, 2 * conf_is_ht40(conf));
	rt2800_bbp_write(rt2x00dev, 4, bbp);

	rt2800_bbp_read(rt2x00dev, 3, &bbp);
	rt2x00_set_field8(&bbp, BBP3_HT40_MINUS, conf_is_ht40_minus(conf));
	rt2800_bbp_write(rt2x00dev, 3, bbp);

	if (rt2x00_rt_rev(rt2x00dev, RT2860, REV_RT2860C)) {
		if (conf_is_ht40(conf)) {
			rt2800_bbp_write(rt2x00dev, 69, 0x1a);
			rt2800_bbp_write(rt2x00dev, 70, 0x0a);
			rt2800_bbp_write(rt2x00dev, 73, 0x16);
		} else {
			rt2800_bbp_write(rt2x00dev, 69, 0x16);
			rt2800_bbp_write(rt2x00dev, 70, 0x08);
			rt2800_bbp_write(rt2x00dev, 73, 0x11);
		}
	}

	msleep(1);

	/*
	 * Clear channel statistic counters
	 */
	rt2800_register_read(rt2x00dev, CH_IDLE_STA, &reg);
	rt2800_register_read(rt2x00dev, CH_BUSY_STA, &reg);
	rt2800_register_read(rt2x00dev, CH_BUSY_STA_SEC, &reg);
}

static int rt2800_get_gain_calibration_delta(struct rt2x00_dev *rt2x00dev)
{
	u8 tssi_bounds[9];
	u8 current_tssi;
	u16 eeprom;
	u8 step;
	int i;

	/*
	 * Read TSSI boundaries for temperature compensation from
	 * the EEPROM.
	 *
	 * Array idx               0    1    2    3    4    5    6    7    8
	 * Matching Delta value   -4   -3   -2   -1    0   +1   +2   +3   +4
	 * Example TSSI bounds  0xF0 0xD0 0xB5 0xA0 0x88 0x45 0x25 0x15 0x00
	 */
	if (rt2x00dev->curr_band == IEEE80211_BAND_2GHZ) {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_TSSI_BOUND_BG1, &eeprom);
		tssi_bounds[0] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_BG1_MINUS4);
		tssi_bounds[1] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_BG1_MINUS3);

		rt2x00_eeprom_read(rt2x00dev, EEPROM_TSSI_BOUND_BG2, &eeprom);
		tssi_bounds[2] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_BG2_MINUS2);
		tssi_bounds[3] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_BG2_MINUS1);

		rt2x00_eeprom_read(rt2x00dev, EEPROM_TSSI_BOUND_BG3, &eeprom);
		tssi_bounds[4] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_BG3_REF);
		tssi_bounds[5] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_BG3_PLUS1);

		rt2x00_eeprom_read(rt2x00dev, EEPROM_TSSI_BOUND_BG4, &eeprom);
		tssi_bounds[6] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_BG4_PLUS2);
		tssi_bounds[7] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_BG4_PLUS3);

		rt2x00_eeprom_read(rt2x00dev, EEPROM_TSSI_BOUND_BG5, &eeprom);
		tssi_bounds[8] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_BG5_PLUS4);

		step = rt2x00_get_field16(eeprom,
					  EEPROM_TSSI_BOUND_BG5_AGC_STEP);
	} else {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_TSSI_BOUND_A1, &eeprom);
		tssi_bounds[0] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_A1_MINUS4);
		tssi_bounds[1] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_A1_MINUS3);

		rt2x00_eeprom_read(rt2x00dev, EEPROM_TSSI_BOUND_A2, &eeprom);
		tssi_bounds[2] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_A2_MINUS2);
		tssi_bounds[3] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_A2_MINUS1);

		rt2x00_eeprom_read(rt2x00dev, EEPROM_TSSI_BOUND_A3, &eeprom);
		tssi_bounds[4] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_A3_REF);
		tssi_bounds[5] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_A3_PLUS1);

		rt2x00_eeprom_read(rt2x00dev, EEPROM_TSSI_BOUND_A4, &eeprom);
		tssi_bounds[6] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_A4_PLUS2);
		tssi_bounds[7] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_A4_PLUS3);

		rt2x00_eeprom_read(rt2x00dev, EEPROM_TSSI_BOUND_A5, &eeprom);
		tssi_bounds[8] = rt2x00_get_field16(eeprom,
					EEPROM_TSSI_BOUND_A5_PLUS4);

		step = rt2x00_get_field16(eeprom,
					  EEPROM_TSSI_BOUND_A5_AGC_STEP);
	}

	/*
	 * Check if temperature compensation is supported.
	 */
	if (tssi_bounds[4] == 0xff)
		return 0;

	/*
	 * Read current TSSI (BBP 49).
	 */
	rt2800_bbp_read(rt2x00dev, 49, &current_tssi);

	/*
	 * Compare TSSI value (BBP49) with the compensation boundaries
	 * from the EEPROM and increase or decrease tx power.
	 */
	for (i = 0; i <= 3; i++) {
		if (current_tssi > tssi_bounds[i])
			break;
	}

	if (i == 4) {
		for (i = 8; i >= 5; i--) {
			if (current_tssi < tssi_bounds[i])
				break;
		}
	}

	return (i - 4) * step;
}

static int rt2800_get_txpower_bw_comp(struct rt2x00_dev *rt2x00dev,
				      enum ieee80211_band band)
{
	u16 eeprom;
	u8 comp_en;
	u8 comp_type;
	int comp_value = 0;

	rt2x00_eeprom_read(rt2x00dev, EEPROM_TXPOWER_DELTA, &eeprom);

	/*
	 * HT40 compensation not required.
	 */
	if (eeprom == 0xffff ||
	    !test_bit(CONFIG_CHANNEL_HT40, &rt2x00dev->flags))
		return 0;

	if (band == IEEE80211_BAND_2GHZ) {
		comp_en = rt2x00_get_field16(eeprom,
				 EEPROM_TXPOWER_DELTA_ENABLE_2G);
		if (comp_en) {
			comp_type = rt2x00_get_field16(eeprom,
					   EEPROM_TXPOWER_DELTA_TYPE_2G);
			comp_value = rt2x00_get_field16(eeprom,
					    EEPROM_TXPOWER_DELTA_VALUE_2G);
			if (!comp_type)
				comp_value = -comp_value;
		}
	} else {
		comp_en = rt2x00_get_field16(eeprom,
				 EEPROM_TXPOWER_DELTA_ENABLE_5G);
		if (comp_en) {
			comp_type = rt2x00_get_field16(eeprom,
					   EEPROM_TXPOWER_DELTA_TYPE_5G);
			comp_value = rt2x00_get_field16(eeprom,
					    EEPROM_TXPOWER_DELTA_VALUE_5G);
			if (!comp_type)
				comp_value = -comp_value;
		}
	}

	return comp_value;
}

static u8 rt2800_compensate_txpower(struct rt2x00_dev *rt2x00dev, int is_rate_b,
				   enum ieee80211_band band, int power_level,
				   u8 txpower, int delta)
{
	u32 reg;
	u16 eeprom;
	u8 criterion;
	u8 eirp_txpower;
	u8 eirp_txpower_criterion;
	u8 reg_limit;

	if (!((band == IEEE80211_BAND_5GHZ) && is_rate_b))
		return txpower;

	if (test_bit(CAPABILITY_POWER_LIMIT, &rt2x00dev->cap_flags)) {
		/*
		 * Check if eirp txpower exceed txpower_limit.
		 * We use OFDM 6M as criterion and its eirp txpower
		 * is stored at EEPROM_EIRP_MAX_TX_POWER.
		 * .11b data rate need add additional 4dbm
		 * when calculating eirp txpower.
		 */
		rt2800_register_read(rt2x00dev, TX_PWR_CFG_0, &reg);
		criterion = rt2x00_get_field32(reg, TX_PWR_CFG_0_6MBS);

		rt2x00_eeprom_read(rt2x00dev,
				   EEPROM_EIRP_MAX_TX_POWER, &eeprom);

		if (band == IEEE80211_BAND_2GHZ)
			eirp_txpower_criterion = rt2x00_get_field16(eeprom,
						 EEPROM_EIRP_MAX_TX_POWER_2GHZ);
		else
			eirp_txpower_criterion = rt2x00_get_field16(eeprom,
						 EEPROM_EIRP_MAX_TX_POWER_5GHZ);

		eirp_txpower = eirp_txpower_criterion + (txpower - criterion) +
			       (is_rate_b ? 4 : 0) + delta;

		reg_limit = (eirp_txpower > power_level) ?
					(eirp_txpower - power_level) : 0;
	} else
		reg_limit = 0;

	return txpower + delta - reg_limit;
}

static void rt2800_config_txpower(struct rt2x00_dev *rt2x00dev,
				  enum ieee80211_band band,
				  int power_level)
{
	u8 txpower;
	u16 eeprom;
	int i, is_rate_b;
	u32 reg;
	u8 r1;
	u32 offset;
	int delta;

	/*
	 * Calculate HT40 compensation delta
	 */
	delta = rt2800_get_txpower_bw_comp(rt2x00dev, band);

	/*
	 * calculate temperature compensation delta
	 */
	delta += rt2800_get_gain_calibration_delta(rt2x00dev);

	/*
	 * set to normal bbp tx power control mode: +/- 0dBm
	 */
	rt2800_bbp_read(rt2x00dev, 1, &r1);
	rt2x00_set_field8(&r1, BBP1_TX_POWER_CTRL, 0);
	rt2800_bbp_write(rt2x00dev, 1, r1);
	offset = TX_PWR_CFG_0;

	for (i = 0; i < EEPROM_TXPOWER_BYRATE_SIZE; i += 2) {
		/* just to be safe */
		if (offset > TX_PWR_CFG_4)
			break;

		rt2800_register_read(rt2x00dev, offset, &reg);

		/* read the next four txpower values */
		rt2x00_eeprom_read(rt2x00dev, EEPROM_TXPOWER_BYRATE + i,
				   &eeprom);

		is_rate_b = i ? 0 : 1;
		/*
		 * TX_PWR_CFG_0: 1MBS, TX_PWR_CFG_1: 24MBS,
		 * TX_PWR_CFG_2: MCS4, TX_PWR_CFG_3: MCS12,
		 * TX_PWR_CFG_4: unknown
		 */
		txpower = rt2x00_get_field16(eeprom,
					     EEPROM_TXPOWER_BYRATE_RATE0);
		txpower = rt2800_compensate_txpower(rt2x00dev, is_rate_b, band,
					     power_level, txpower, delta);
		rt2x00_set_field32(&reg, TX_PWR_CFG_RATE0, txpower);

		/*
		 * TX_PWR_CFG_0: 2MBS, TX_PWR_CFG_1: 36MBS,
		 * TX_PWR_CFG_2: MCS5, TX_PWR_CFG_3: MCS13,
		 * TX_PWR_CFG_4: unknown
		 */
		txpower = rt2x00_get_field16(eeprom,
					     EEPROM_TXPOWER_BYRATE_RATE1);
		txpower = rt2800_compensate_txpower(rt2x00dev, is_rate_b, band,
					     power_level, txpower, delta);
		rt2x00_set_field32(&reg, TX_PWR_CFG_RATE1, txpower);

		/*
		 * TX_PWR_CFG_0: 5.5MBS, TX_PWR_CFG_1: 48MBS,
		 * TX_PWR_CFG_2: MCS6,  TX_PWR_CFG_3: MCS14,
		 * TX_PWR_CFG_4: unknown
		 */
		txpower = rt2x00_get_field16(eeprom,
					     EEPROM_TXPOWER_BYRATE_RATE2);
		txpower = rt2800_compensate_txpower(rt2x00dev, is_rate_b, band,
					     power_level, txpower, delta);
		rt2x00_set_field32(&reg, TX_PWR_CFG_RATE2, txpower);

		/*
		 * TX_PWR_CFG_0: 11MBS, TX_PWR_CFG_1: 54MBS,
		 * TX_PWR_CFG_2: MCS7,  TX_PWR_CFG_3: MCS15,
		 * TX_PWR_CFG_4: unknown
		 */
		txpower = rt2x00_get_field16(eeprom,
					     EEPROM_TXPOWER_BYRATE_RATE3);
		txpower = rt2800_compensate_txpower(rt2x00dev, is_rate_b, band,
					     power_level, txpower, delta);
		rt2x00_set_field32(&reg, TX_PWR_CFG_RATE3, txpower);

		/* read the next four txpower values */
		rt2x00_eeprom_read(rt2x00dev, EEPROM_TXPOWER_BYRATE + i + 1,
				   &eeprom);

		is_rate_b = 0;
		/*
		 * TX_PWR_CFG_0: 6MBS, TX_PWR_CFG_1: MCS0,
		 * TX_PWR_CFG_2: MCS8, TX_PWR_CFG_3: unknown,
		 * TX_PWR_CFG_4: unknown
		 */
		txpower = rt2x00_get_field16(eeprom,
					     EEPROM_TXPOWER_BYRATE_RATE0);
		txpower = rt2800_compensate_txpower(rt2x00dev, is_rate_b, band,
					     power_level, txpower, delta);
		rt2x00_set_field32(&reg, TX_PWR_CFG_RATE4, txpower);

		/*
		 * TX_PWR_CFG_0: 9MBS, TX_PWR_CFG_1: MCS1,
		 * TX_PWR_CFG_2: MCS9, TX_PWR_CFG_3: unknown,
		 * TX_PWR_CFG_4: unknown
		 */
		txpower = rt2x00_get_field16(eeprom,
					     EEPROM_TXPOWER_BYRATE_RATE1);
		txpower = rt2800_compensate_txpower(rt2x00dev, is_rate_b, band,
					     power_level, txpower, delta);
		rt2x00_set_field32(&reg, TX_PWR_CFG_RATE5, txpower);

		/*
		 * TX_PWR_CFG_0: 12MBS, TX_PWR_CFG_1: MCS2,
		 * TX_PWR_CFG_2: MCS10, TX_PWR_CFG_3: unknown,
		 * TX_PWR_CFG_4: unknown
		 */
		txpower = rt2x00_get_field16(eeprom,
					     EEPROM_TXPOWER_BYRATE_RATE2);
		txpower = rt2800_compensate_txpower(rt2x00dev, is_rate_b, band,
					     power_level, txpower, delta);
		rt2x00_set_field32(&reg, TX_PWR_CFG_RATE6, txpower);

		/*
		 * TX_PWR_CFG_0: 18MBS, TX_PWR_CFG_1: MCS3,
		 * TX_PWR_CFG_2: MCS11, TX_PWR_CFG_3: unknown,
		 * TX_PWR_CFG_4: unknown
		 */
		txpower = rt2x00_get_field16(eeprom,
					     EEPROM_TXPOWER_BYRATE_RATE3);
		txpower = rt2800_compensate_txpower(rt2x00dev, is_rate_b, band,
					     power_level, txpower, delta);
		rt2x00_set_field32(&reg, TX_PWR_CFG_RATE7, txpower);

		rt2800_register_write(rt2x00dev, offset, reg);

		/* next TX_PWR_CFG register */
		offset += 4;
	}
}

void rt2800_gain_calibration(struct rt2x00_dev *rt2x00dev)
{
	rt2800_config_txpower(rt2x00dev, rt2x00dev->curr_band,
			      rt2x00dev->tx_power);
}
EXPORT_SYMBOL_GPL(rt2800_gain_calibration);

static void rt2800_config_retry_limit(struct rt2x00_dev *rt2x00dev,
				      struct rt2x00lib_conf *libconf)
{
	u32 reg;

	rt2800_register_read(rt2x00dev, TX_RTY_CFG, &reg);
	rt2x00_set_field32(&reg, TX_RTY_CFG_SHORT_RTY_LIMIT,
			   libconf->conf->short_frame_max_tx_count);
	rt2x00_set_field32(&reg, TX_RTY_CFG_LONG_RTY_LIMIT,
			   libconf->conf->long_frame_max_tx_count);
	rt2800_register_write(rt2x00dev, TX_RTY_CFG, reg);
}

static void rt2800_config_ps(struct rt2x00_dev *rt2x00dev,
			     struct rt2x00lib_conf *libconf)
{
	enum dev_state state =
	    (libconf->conf->flags & IEEE80211_CONF_PS) ?
		STATE_SLEEP : STATE_AWAKE;
	u32 reg;

	if (state == STATE_SLEEP) {
		rt2800_register_write(rt2x00dev, AUTOWAKEUP_CFG, 0);

		rt2800_register_read(rt2x00dev, AUTOWAKEUP_CFG, &reg);
		rt2x00_set_field32(&reg, AUTOWAKEUP_CFG_AUTO_LEAD_TIME, 5);
		rt2x00_set_field32(&reg, AUTOWAKEUP_CFG_TBCN_BEFORE_WAKE,
				   libconf->conf->listen_interval - 1);
		rt2x00_set_field32(&reg, AUTOWAKEUP_CFG_AUTOWAKE, 1);
		rt2800_register_write(rt2x00dev, AUTOWAKEUP_CFG, reg);

		rt2x00dev->ops->lib->set_device_state(rt2x00dev, state);
	} else {
		rt2800_register_read(rt2x00dev, AUTOWAKEUP_CFG, &reg);
		rt2x00_set_field32(&reg, AUTOWAKEUP_CFG_AUTO_LEAD_TIME, 0);
		rt2x00_set_field32(&reg, AUTOWAKEUP_CFG_TBCN_BEFORE_WAKE, 0);
		rt2x00_set_field32(&reg, AUTOWAKEUP_CFG_AUTOWAKE, 0);
		rt2800_register_write(rt2x00dev, AUTOWAKEUP_CFG, reg);

		rt2x00dev->ops->lib->set_device_state(rt2x00dev, state);
	}
}

void rt2800_config(struct rt2x00_dev *rt2x00dev,
		   struct rt2x00lib_conf *libconf,
		   const unsigned int flags)
{
	/* Always recalculate LNA gain before changing configuration */
	rt2800_config_lna_gain(rt2x00dev, libconf);

	if (flags & IEEE80211_CONF_CHANGE_CHANNEL) {
		rt2800_config_channel(rt2x00dev, libconf->conf,
				      &libconf->rf, &libconf->channel);
		rt2800_config_txpower(rt2x00dev, libconf->conf->channel->band,
				      libconf->conf->power_level);
	}
	if (flags & IEEE80211_CONF_CHANGE_POWER)
		rt2800_config_txpower(rt2x00dev, libconf->conf->channel->band,
				      libconf->conf->power_level);
	if (flags & IEEE80211_CONF_CHANGE_RETRY_LIMITS)
		rt2800_config_retry_limit(rt2x00dev, libconf);
	if (flags & IEEE80211_CONF_CHANGE_PS)
		rt2800_config_ps(rt2x00dev, libconf);
}
EXPORT_SYMBOL_GPL(rt2800_config);

/*
 * Link tuning
 */
void rt2800_link_stats(struct rt2x00_dev *rt2x00dev, struct link_qual *qual)
{
	u32 reg;

	/*
	 * Update FCS error count from register.
	 */
	rt2800_register_read(rt2x00dev, RX_STA_CNT0, &reg);
	qual->rx_failed = rt2x00_get_field32(reg, RX_STA_CNT0_CRC_ERR);
}
EXPORT_SYMBOL_GPL(rt2800_link_stats);

static u8 rt2800_get_default_vgc(struct rt2x00_dev *rt2x00dev)
{
	if (rt2x00dev->curr_band == IEEE80211_BAND_2GHZ) {
		if (rt2x00_rt(rt2x00dev, RT3070) ||
		    rt2x00_rt(rt2x00dev, RT3071) ||
		    rt2x00_rt(rt2x00dev, RT3090) ||
		    rt2x00_rt(rt2x00dev, RT3390) ||
		    rt2x00_rt(rt2x00dev, RT5390))
			return 0x1c + (2 * rt2x00dev->lna_gain);
		else
			return 0x2e + rt2x00dev->lna_gain;
	}

	if (!test_bit(CONFIG_CHANNEL_HT40, &rt2x00dev->flags))
		return 0x32 + (rt2x00dev->lna_gain * 5) / 3;
	else
		return 0x3a + (rt2x00dev->lna_gain * 5) / 3;
}

static inline void rt2800_set_vgc(struct rt2x00_dev *rt2x00dev,
				  struct link_qual *qual, u8 vgc_level)
{
	if (qual->vgc_level != vgc_level) {
		rt2800_bbp_write(rt2x00dev, 66, vgc_level);
		qual->vgc_level = vgc_level;
		qual->vgc_level_reg = vgc_level;
	}
}

void rt2800_reset_tuner(struct rt2x00_dev *rt2x00dev, struct link_qual *qual)
{
	rt2800_set_vgc(rt2x00dev, qual, rt2800_get_default_vgc(rt2x00dev));
}
EXPORT_SYMBOL_GPL(rt2800_reset_tuner);

void rt2800_link_tuner(struct rt2x00_dev *rt2x00dev, struct link_qual *qual,
		       const u32 count)
{
	if (rt2x00_rt_rev(rt2x00dev, RT2860, REV_RT2860C))
		return;

	/*
	 * When RSSI is better then -80 increase VGC level with 0x10
	 */
	rt2800_set_vgc(rt2x00dev, qual,
		       rt2800_get_default_vgc(rt2x00dev) +
		       ((qual->rssi > -80) * 0x10));
}
EXPORT_SYMBOL_GPL(rt2800_link_tuner);

/*
 * Initialization functions.
 */
static int rt2800_init_registers(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	u16 eeprom;
	unsigned int i;
	int ret;

	rt2800_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_TX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_DMA_BUSY, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_RX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_RX_DMA_BUSY, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_WRITEBACK_DONE, 1);
	rt2800_register_write(rt2x00dev, WPDMA_GLO_CFG, reg);

	ret = rt2800_drv_init_registers(rt2x00dev);
	if (ret)
		return ret;

	rt2800_register_read(rt2x00dev, BCN_OFFSET0, &reg);
	rt2x00_set_field32(&reg, BCN_OFFSET0_BCN0, 0xe0); /* 0x3800 */
	rt2x00_set_field32(&reg, BCN_OFFSET0_BCN1, 0xe8); /* 0x3a00 */
	rt2x00_set_field32(&reg, BCN_OFFSET0_BCN2, 0xf0); /* 0x3c00 */
	rt2x00_set_field32(&reg, BCN_OFFSET0_BCN3, 0xf8); /* 0x3e00 */
	rt2800_register_write(rt2x00dev, BCN_OFFSET0, reg);

	rt2800_register_read(rt2x00dev, BCN_OFFSET1, &reg);
	rt2x00_set_field32(&reg, BCN_OFFSET1_BCN4, 0xc8); /* 0x3200 */
	rt2x00_set_field32(&reg, BCN_OFFSET1_BCN5, 0xd0); /* 0x3400 */
	rt2x00_set_field32(&reg, BCN_OFFSET1_BCN6, 0x77); /* 0x1dc0 */
	rt2x00_set_field32(&reg, BCN_OFFSET1_BCN7, 0x6f); /* 0x1bc0 */
	rt2800_register_write(rt2x00dev, BCN_OFFSET1, reg);

	rt2800_register_write(rt2x00dev, LEGACY_BASIC_RATE, 0x0000013f);
	rt2800_register_write(rt2x00dev, HT_BASIC_RATE, 0x00008003);

	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, 0x00000000);

	rt2800_register_read(rt2x00dev, BCN_TIME_CFG, &reg);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_INTERVAL, 1600);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_TSF_TICKING, 0);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_TSF_SYNC, 0);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_TBTT_ENABLE, 0);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 0);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_TX_TIME_COMPENSATE, 0);
	rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);

	rt2800_config_filter(rt2x00dev, FIF_ALLMULTI);

	rt2800_register_read(rt2x00dev, BKOFF_SLOT_CFG, &reg);
	rt2x00_set_field32(&reg, BKOFF_SLOT_CFG_SLOT_TIME, 9);
	rt2x00_set_field32(&reg, BKOFF_SLOT_CFG_CC_DELAY_TIME, 2);
	rt2800_register_write(rt2x00dev, BKOFF_SLOT_CFG, reg);

	if (rt2x00_rt(rt2x00dev, RT3071) ||
	    rt2x00_rt(rt2x00dev, RT3090) ||
	    rt2x00_rt(rt2x00dev, RT3390)) {
		rt2800_register_write(rt2x00dev, TX_SW_CFG0, 0x00000400);
		rt2800_register_write(rt2x00dev, TX_SW_CFG1, 0x00000000);
		if (rt2x00_rt_rev_lt(rt2x00dev, RT3071, REV_RT3071E) ||
		    rt2x00_rt_rev_lt(rt2x00dev, RT3090, REV_RT3090E) ||
		    rt2x00_rt_rev_lt(rt2x00dev, RT3390, REV_RT3390E)) {
			rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC_CONF1, &eeprom);
			if (rt2x00_get_field16(eeprom, EEPROM_NIC_CONF1_DAC_TEST))
				rt2800_register_write(rt2x00dev, TX_SW_CFG2,
						      0x0000002c);
			else
				rt2800_register_write(rt2x00dev, TX_SW_CFG2,
						      0x0000000f);
		} else {
			rt2800_register_write(rt2x00dev, TX_SW_CFG2, 0x00000000);
		}
	} else if (rt2x00_rt(rt2x00dev, RT3070)) {
		rt2800_register_write(rt2x00dev, TX_SW_CFG0, 0x00000400);

		if (rt2x00_rt_rev_lt(rt2x00dev, RT3070, REV_RT3070F)) {
			rt2800_register_write(rt2x00dev, TX_SW_CFG1, 0x00000000);
			rt2800_register_write(rt2x00dev, TX_SW_CFG2, 0x0000002c);
		} else {
			rt2800_register_write(rt2x00dev, TX_SW_CFG1, 0x00080606);
			rt2800_register_write(rt2x00dev, TX_SW_CFG2, 0x00000000);
		}
	} else if (rt2800_is_305x_soc(rt2x00dev)) {
		rt2800_register_write(rt2x00dev, TX_SW_CFG0, 0x00000400);
		rt2800_register_write(rt2x00dev, TX_SW_CFG1, 0x00000000);
		rt2800_register_write(rt2x00dev, TX_SW_CFG2, 0x00000030);
	} else if (rt2x00_rt(rt2x00dev, RT3572)) {
		rt2800_register_write(rt2x00dev, TX_SW_CFG0, 0x00000400);
		rt2800_register_write(rt2x00dev, TX_SW_CFG1, 0x00080606);
	} else if (rt2x00_rt(rt2x00dev, RT5390)) {
		rt2800_register_write(rt2x00dev, TX_SW_CFG0, 0x00000404);
		rt2800_register_write(rt2x00dev, TX_SW_CFG1, 0x00080606);
		rt2800_register_write(rt2x00dev, TX_SW_CFG2, 0x00000000);
	} else {
		rt2800_register_write(rt2x00dev, TX_SW_CFG0, 0x00000000);
		rt2800_register_write(rt2x00dev, TX_SW_CFG1, 0x00080606);
	}

	rt2800_register_read(rt2x00dev, TX_LINK_CFG, &reg);
	rt2x00_set_field32(&reg, TX_LINK_CFG_REMOTE_MFB_LIFETIME, 32);
	rt2x00_set_field32(&reg, TX_LINK_CFG_MFB_ENABLE, 0);
	rt2x00_set_field32(&reg, TX_LINK_CFG_REMOTE_UMFS_ENABLE, 0);
	rt2x00_set_field32(&reg, TX_LINK_CFG_TX_MRQ_EN, 0);
	rt2x00_set_field32(&reg, TX_LINK_CFG_TX_RDG_EN, 0);
	rt2x00_set_field32(&reg, TX_LINK_CFG_TX_CF_ACK_EN, 1);
	rt2x00_set_field32(&reg, TX_LINK_CFG_REMOTE_MFB, 0);
	rt2x00_set_field32(&reg, TX_LINK_CFG_REMOTE_MFS, 0);
	rt2800_register_write(rt2x00dev, TX_LINK_CFG, reg);

	rt2800_register_read(rt2x00dev, TX_TIMEOUT_CFG, &reg);
	rt2x00_set_field32(&reg, TX_TIMEOUT_CFG_MPDU_LIFETIME, 9);
	rt2x00_set_field32(&reg, TX_TIMEOUT_CFG_RX_ACK_TIMEOUT, 32);
	rt2x00_set_field32(&reg, TX_TIMEOUT_CFG_TX_OP_TIMEOUT, 10);
	rt2800_register_write(rt2x00dev, TX_TIMEOUT_CFG, reg);

	rt2800_register_read(rt2x00dev, MAX_LEN_CFG, &reg);
	rt2x00_set_field32(&reg, MAX_LEN_CFG_MAX_MPDU, AGGREGATION_SIZE);
	if (rt2x00_rt_rev_gte(rt2x00dev, RT2872, REV_RT2872E) ||
	    rt2x00_rt(rt2x00dev, RT2883) ||
	    rt2x00_rt_rev_lt(rt2x00dev, RT3070, REV_RT3070E))
		rt2x00_set_field32(&reg, MAX_LEN_CFG_MAX_PSDU, 2);
	else
		rt2x00_set_field32(&reg, MAX_LEN_CFG_MAX_PSDU, 1);
	rt2x00_set_field32(&reg, MAX_LEN_CFG_MIN_PSDU, 0);
	rt2x00_set_field32(&reg, MAX_LEN_CFG_MIN_MPDU, 0);
	rt2800_register_write(rt2x00dev, MAX_LEN_CFG, reg);

	rt2800_register_read(rt2x00dev, LED_CFG, &reg);
	rt2x00_set_field32(&reg, LED_CFG_ON_PERIOD, 70);
	rt2x00_set_field32(&reg, LED_CFG_OFF_PERIOD, 30);
	rt2x00_set_field32(&reg, LED_CFG_SLOW_BLINK_PERIOD, 3);
	rt2x00_set_field32(&reg, LED_CFG_R_LED_MODE, 3);
	rt2x00_set_field32(&reg, LED_CFG_G_LED_MODE, 3);
	rt2x00_set_field32(&reg, LED_CFG_Y_LED_MODE, 3);
	rt2x00_set_field32(&reg, LED_CFG_LED_POLAR, 1);
	rt2800_register_write(rt2x00dev, LED_CFG, reg);

	rt2800_register_write(rt2x00dev, PBF_MAX_PCNT, 0x1f3fbf9f);

	rt2800_register_read(rt2x00dev, TX_RTY_CFG, &reg);
	rt2x00_set_field32(&reg, TX_RTY_CFG_SHORT_RTY_LIMIT, 15);
	rt2x00_set_field32(&reg, TX_RTY_CFG_LONG_RTY_LIMIT, 31);
	rt2x00_set_field32(&reg, TX_RTY_CFG_LONG_RTY_THRE, 2000);
	rt2x00_set_field32(&reg, TX_RTY_CFG_NON_AGG_RTY_MODE, 0);
	rt2x00_set_field32(&reg, TX_RTY_CFG_AGG_RTY_MODE, 0);
	rt2x00_set_field32(&reg, TX_RTY_CFG_TX_AUTO_FB_ENABLE, 1);
	rt2800_register_write(rt2x00dev, TX_RTY_CFG, reg);

	rt2800_register_read(rt2x00dev, AUTO_RSP_CFG, &reg);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_AUTORESPONDER, 1);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_BAC_ACK_POLICY, 1);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_CTS_40_MMODE, 0);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_CTS_40_MREF, 0);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_AR_PREAMBLE, 1);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_DUAL_CTS_EN, 0);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_ACK_CTS_PSM_BIT, 0);
	rt2800_register_write(rt2x00dev, AUTO_RSP_CFG, reg);

	rt2800_register_read(rt2x00dev, CCK_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_PROTECT_RATE, 3);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_PROTECT_CTRL, 0);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_PROTECT_NAV_SHORT, 1);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_TX_OP_ALLOW_CCK, 1);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_TX_OP_ALLOW_OFDM, 1);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_TX_OP_ALLOW_MM20, 1);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_TX_OP_ALLOW_MM40, 0);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_TX_OP_ALLOW_GF20, 1);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_TX_OP_ALLOW_GF40, 0);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_RTS_TH_EN, 1);
	rt2800_register_write(rt2x00dev, CCK_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, OFDM_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_PROTECT_RATE, 3);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_PROTECT_CTRL, 0);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_PROTECT_NAV_SHORT, 1);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_TX_OP_ALLOW_CCK, 1);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_TX_OP_ALLOW_OFDM, 1);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_TX_OP_ALLOW_MM20, 1);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_TX_OP_ALLOW_MM40, 0);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_TX_OP_ALLOW_GF20, 1);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_TX_OP_ALLOW_GF40, 0);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_RTS_TH_EN, 1);
	rt2800_register_write(rt2x00dev, OFDM_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, MM20_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_PROTECT_RATE, 0x4004);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_PROTECT_CTRL, 0);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_PROTECT_NAV_SHORT, 1);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_TX_OP_ALLOW_CCK, 1);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_TX_OP_ALLOW_OFDM, 1);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_TX_OP_ALLOW_MM20, 1);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_TX_OP_ALLOW_MM40, 0);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_TX_OP_ALLOW_GF20, 1);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_TX_OP_ALLOW_GF40, 0);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_RTS_TH_EN, 0);
	rt2800_register_write(rt2x00dev, MM20_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, MM40_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_PROTECT_RATE, 0x4084);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_PROTECT_CTRL, 0);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_PROTECT_NAV_SHORT, 1);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_TX_OP_ALLOW_CCK, 1);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_TX_OP_ALLOW_OFDM, 1);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_TX_OP_ALLOW_MM20, 1);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_TX_OP_ALLOW_MM40, 1);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_TX_OP_ALLOW_GF20, 1);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_TX_OP_ALLOW_GF40, 1);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_RTS_TH_EN, 0);
	rt2800_register_write(rt2x00dev, MM40_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, GF20_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_PROTECT_RATE, 0x4004);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_PROTECT_CTRL, 0);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_PROTECT_NAV_SHORT, 1);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_TX_OP_ALLOW_CCK, 1);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_TX_OP_ALLOW_OFDM, 1);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_TX_OP_ALLOW_MM20, 1);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_TX_OP_ALLOW_MM40, 0);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_TX_OP_ALLOW_GF20, 1);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_TX_OP_ALLOW_GF40, 0);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_RTS_TH_EN, 0);
	rt2800_register_write(rt2x00dev, GF20_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, GF40_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_PROTECT_RATE, 0x4084);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_PROTECT_CTRL, 0);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_PROTECT_NAV_SHORT, 1);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_TX_OP_ALLOW_CCK, 1);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_TX_OP_ALLOW_OFDM, 1);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_TX_OP_ALLOW_MM20, 1);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_TX_OP_ALLOW_MM40, 1);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_TX_OP_ALLOW_GF20, 1);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_TX_OP_ALLOW_GF40, 1);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_RTS_TH_EN, 0);
	rt2800_register_write(rt2x00dev, GF40_PROT_CFG, reg);

	if (rt2x00_is_usb(rt2x00dev)) {
		rt2800_register_write(rt2x00dev, PBF_CFG, 0xf40006);

		rt2800_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
		rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_TX_DMA, 0);
		rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_DMA_BUSY, 0);
		rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_RX_DMA, 0);
		rt2x00_set_field32(&reg, WPDMA_GLO_CFG_RX_DMA_BUSY, 0);
		rt2x00_set_field32(&reg, WPDMA_GLO_CFG_WP_DMA_BURST_SIZE, 3);
		rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_WRITEBACK_DONE, 0);
		rt2x00_set_field32(&reg, WPDMA_GLO_CFG_BIG_ENDIAN, 0);
		rt2x00_set_field32(&reg, WPDMA_GLO_CFG_RX_HDR_SCATTER, 0);
		rt2x00_set_field32(&reg, WPDMA_GLO_CFG_HDR_SEG_LEN, 0);
		rt2800_register_write(rt2x00dev, WPDMA_GLO_CFG, reg);
	}

	/*
	 * The legacy driver also sets TXOP_CTRL_CFG_RESERVED_TRUN_EN to 1
	 * although it is reserved.
	 */
	rt2800_register_read(rt2x00dev, TXOP_CTRL_CFG, &reg);
	rt2x00_set_field32(&reg, TXOP_CTRL_CFG_TIMEOUT_TRUN_EN, 1);
	rt2x00_set_field32(&reg, TXOP_CTRL_CFG_AC_TRUN_EN, 1);
	rt2x00_set_field32(&reg, TXOP_CTRL_CFG_TXRATEGRP_TRUN_EN, 1);
	rt2x00_set_field32(&reg, TXOP_CTRL_CFG_USER_MODE_TRUN_EN, 1);
	rt2x00_set_field32(&reg, TXOP_CTRL_CFG_MIMO_PS_TRUN_EN, 1);
	rt2x00_set_field32(&reg, TXOP_CTRL_CFG_RESERVED_TRUN_EN, 1);
	rt2x00_set_field32(&reg, TXOP_CTRL_CFG_LSIG_TXOP_EN, 0);
	rt2x00_set_field32(&reg, TXOP_CTRL_CFG_EXT_CCA_EN, 0);
	rt2x00_set_field32(&reg, TXOP_CTRL_CFG_EXT_CCA_DLY, 88);
	rt2x00_set_field32(&reg, TXOP_CTRL_CFG_EXT_CWMIN, 0);
	rt2800_register_write(rt2x00dev, TXOP_CTRL_CFG, reg);

	rt2800_register_write(rt2x00dev, TXOP_HLDR_ET, 0x00000002);

	rt2800_register_read(rt2x00dev, TX_RTS_CFG, &reg);
	rt2x00_set_field32(&reg, TX_RTS_CFG_AUTO_RTS_RETRY_LIMIT, 32);
	rt2x00_set_field32(&reg, TX_RTS_CFG_RTS_THRES,
			   IEEE80211_MAX_RTS_THRESHOLD);
	rt2x00_set_field32(&reg, TX_RTS_CFG_RTS_FBK_EN, 0);
	rt2800_register_write(rt2x00dev, TX_RTS_CFG, reg);

	rt2800_register_write(rt2x00dev, EXP_ACK_TIME, 0x002400ca);

	/*
	 * Usually the CCK SIFS time should be set to 10 and the OFDM SIFS
	 * time should be set to 16. However, the original Ralink driver uses
	 * 16 for both and indeed using a value of 10 for CCK SIFS results in
	 * connection problems with 11g + CTS protection. Hence, use the same
	 * defaults as the Ralink driver: 16 for both, CCK and OFDM SIFS.
	 */
	rt2800_register_read(rt2x00dev, XIFS_TIME_CFG, &reg);
	rt2x00_set_field32(&reg, XIFS_TIME_CFG_CCKM_SIFS_TIME, 16);
	rt2x00_set_field32(&reg, XIFS_TIME_CFG_OFDM_SIFS_TIME, 16);
	rt2x00_set_field32(&reg, XIFS_TIME_CFG_OFDM_XIFS_TIME, 4);
	rt2x00_set_field32(&reg, XIFS_TIME_CFG_EIFS, 314);
	rt2x00_set_field32(&reg, XIFS_TIME_CFG_BB_RXEND_ENABLE, 1);
	rt2800_register_write(rt2x00dev, XIFS_TIME_CFG, reg);

	rt2800_register_write(rt2x00dev, PWR_PIN_CFG, 0x00000003);

	/*
	 * ASIC will keep garbage value after boot, clear encryption keys.
	 */
	for (i = 0; i < 4; i++)
		rt2800_register_write(rt2x00dev,
					 SHARED_KEY_MODE_ENTRY(i), 0);

	for (i = 0; i < 256; i++) {
		rt2800_config_wcid(rt2x00dev, NULL, i);
		rt2800_delete_wcid_attr(rt2x00dev, i);
		rt2800_register_write(rt2x00dev, MAC_IVEIV_ENTRY(i), 0);
	}

	/*
	 * Clear all beacons
	 */
	rt2800_clear_beacon_register(rt2x00dev, HW_BEACON_BASE0);
	rt2800_clear_beacon_register(rt2x00dev, HW_BEACON_BASE1);
	rt2800_clear_beacon_register(rt2x00dev, HW_BEACON_BASE2);
	rt2800_clear_beacon_register(rt2x00dev, HW_BEACON_BASE3);
	rt2800_clear_beacon_register(rt2x00dev, HW_BEACON_BASE4);
	rt2800_clear_beacon_register(rt2x00dev, HW_BEACON_BASE5);
	rt2800_clear_beacon_register(rt2x00dev, HW_BEACON_BASE6);
	rt2800_clear_beacon_register(rt2x00dev, HW_BEACON_BASE7);

	if (rt2x00_is_usb(rt2x00dev)) {
		rt2800_register_read(rt2x00dev, US_CYC_CNT, &reg);
		rt2x00_set_field32(&reg, US_CYC_CNT_CLOCK_CYCLE, 30);
		rt2800_register_write(rt2x00dev, US_CYC_CNT, reg);
	} else if (rt2x00_is_pcie(rt2x00dev)) {
		rt2800_register_read(rt2x00dev, US_CYC_CNT, &reg);
		rt2x00_set_field32(&reg, US_CYC_CNT_CLOCK_CYCLE, 125);
		rt2800_register_write(rt2x00dev, US_CYC_CNT, reg);
	}

	rt2800_register_read(rt2x00dev, HT_FBK_CFG0, &reg);
	rt2x00_set_field32(&reg, HT_FBK_CFG0_HTMCS0FBK, 0);
	rt2x00_set_field32(&reg, HT_FBK_CFG0_HTMCS1FBK, 0);
	rt2x00_set_field32(&reg, HT_FBK_CFG0_HTMCS2FBK, 1);
	rt2x00_set_field32(&reg, HT_FBK_CFG0_HTMCS3FBK, 2);
	rt2x00_set_field32(&reg, HT_FBK_CFG0_HTMCS4FBK, 3);
	rt2x00_set_field32(&reg, HT_FBK_CFG0_HTMCS5FBK, 4);
	rt2x00_set_field32(&reg, HT_FBK_CFG0_HTMCS6FBK, 5);
	rt2x00_set_field32(&reg, HT_FBK_CFG0_HTMCS7FBK, 6);
	rt2800_register_write(rt2x00dev, HT_FBK_CFG0, reg);

	rt2800_register_read(rt2x00dev, HT_FBK_CFG1, &reg);
	rt2x00_set_field32(&reg, HT_FBK_CFG1_HTMCS8FBK, 8);
	rt2x00_set_field32(&reg, HT_FBK_CFG1_HTMCS9FBK, 8);
	rt2x00_set_field32(&reg, HT_FBK_CFG1_HTMCS10FBK, 9);
	rt2x00_set_field32(&reg, HT_FBK_CFG1_HTMCS11FBK, 10);
	rt2x00_set_field32(&reg, HT_FBK_CFG1_HTMCS12FBK, 11);
	rt2x00_set_field32(&reg, HT_FBK_CFG1_HTMCS13FBK, 12);
	rt2x00_set_field32(&reg, HT_FBK_CFG1_HTMCS14FBK, 13);
	rt2x00_set_field32(&reg, HT_FBK_CFG1_HTMCS15FBK, 14);
	rt2800_register_write(rt2x00dev, HT_FBK_CFG1, reg);

	rt2800_register_read(rt2x00dev, LG_FBK_CFG0, &reg);
	rt2x00_set_field32(&reg, LG_FBK_CFG0_OFDMMCS0FBK, 8);
	rt2x00_set_field32(&reg, LG_FBK_CFG0_OFDMMCS1FBK, 8);
	rt2x00_set_field32(&reg, LG_FBK_CFG0_OFDMMCS2FBK, 9);
	rt2x00_set_field32(&reg, LG_FBK_CFG0_OFDMMCS3FBK, 10);
	rt2x00_set_field32(&reg, LG_FBK_CFG0_OFDMMCS4FBK, 11);
	rt2x00_set_field32(&reg, LG_FBK_CFG0_OFDMMCS5FBK, 12);
	rt2x00_set_field32(&reg, LG_FBK_CFG0_OFDMMCS6FBK, 13);
	rt2x00_set_field32(&reg, LG_FBK_CFG0_OFDMMCS7FBK, 14);
	rt2800_register_write(rt2x00dev, LG_FBK_CFG0, reg);

	rt2800_register_read(rt2x00dev, LG_FBK_CFG1, &reg);
	rt2x00_set_field32(&reg, LG_FBK_CFG0_CCKMCS0FBK, 0);
	rt2x00_set_field32(&reg, LG_FBK_CFG0_CCKMCS1FBK, 0);
	rt2x00_set_field32(&reg, LG_FBK_CFG0_CCKMCS2FBK, 1);
	rt2x00_set_field32(&reg, LG_FBK_CFG0_CCKMCS3FBK, 2);
	rt2800_register_write(rt2x00dev, LG_FBK_CFG1, reg);

	/*
	 * Do not force the BA window size, we use the TXWI to set it
	 */
	rt2800_register_read(rt2x00dev, AMPDU_BA_WINSIZE, &reg);
	rt2x00_set_field32(&reg, AMPDU_BA_WINSIZE_FORCE_WINSIZE_ENABLE, 0);
	rt2x00_set_field32(&reg, AMPDU_BA_WINSIZE_FORCE_WINSIZE, 0);
	rt2800_register_write(rt2x00dev, AMPDU_BA_WINSIZE, reg);

	/*
	 * We must clear the error counters.
	 * These registers are cleared on read,
	 * so we may pass a useless variable to store the value.
	 */
	rt2800_register_read(rt2x00dev, RX_STA_CNT0, &reg);
	rt2800_register_read(rt2x00dev, RX_STA_CNT1, &reg);
	rt2800_register_read(rt2x00dev, RX_STA_CNT2, &reg);
	rt2800_register_read(rt2x00dev, TX_STA_CNT0, &reg);
	rt2800_register_read(rt2x00dev, TX_STA_CNT1, &reg);
	rt2800_register_read(rt2x00dev, TX_STA_CNT2, &reg);

	/*
	 * Setup leadtime for pre tbtt interrupt to 6ms
	 */
	rt2800_register_read(rt2x00dev, INT_TIMER_CFG, &reg);
	rt2x00_set_field32(&reg, INT_TIMER_CFG_PRE_TBTT_TIMER, 6 << 4);
	rt2800_register_write(rt2x00dev, INT_TIMER_CFG, reg);

	/*
	 * Set up channel statistics timer
	 */
	rt2800_register_read(rt2x00dev, CH_TIME_CFG, &reg);
	rt2x00_set_field32(&reg, CH_TIME_CFG_EIFS_BUSY, 1);
	rt2x00_set_field32(&reg, CH_TIME_CFG_NAV_BUSY, 1);
	rt2x00_set_field32(&reg, CH_TIME_CFG_RX_BUSY, 1);
	rt2x00_set_field32(&reg, CH_TIME_CFG_TX_BUSY, 1);
	rt2x00_set_field32(&reg, CH_TIME_CFG_TMR_EN, 1);
	rt2800_register_write(rt2x00dev, CH_TIME_CFG, reg);

	return 0;
}

static int rt2800_wait_bbp_rf_ready(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u32 reg;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2800_register_read(rt2x00dev, MAC_STATUS_CFG, &reg);
		if (!rt2x00_get_field32(reg, MAC_STATUS_CFG_BBP_RF_BUSY))
			return 0;

		udelay(REGISTER_BUSY_DELAY);
	}

	ERROR(rt2x00dev, "BBP/RF register access failed, aborting.\n");
	return -EACCES;
}

static int rt2800_wait_bbp_ready(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u8 value;

	/*
	 * BBP was enabled after firmware was loaded,
	 * but we need to reactivate it now.
	 */
	rt2800_register_write(rt2x00dev, H2M_BBP_AGENT, 0);
	rt2800_register_write(rt2x00dev, H2M_MAILBOX_CSR, 0);
	msleep(1);

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2800_bbp_read(rt2x00dev, 0, &value);
		if ((value != 0xff) && (value != 0x00))
			return 0;
		udelay(REGISTER_BUSY_DELAY);
	}

	ERROR(rt2x00dev, "BBP register access failed, aborting.\n");
	return -EACCES;
}

static int rt2800_init_bbp(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u16 eeprom;
	u8 reg_id;
	u8 value;

	if (unlikely(rt2800_wait_bbp_rf_ready(rt2x00dev) ||
		     rt2800_wait_bbp_ready(rt2x00dev)))
		return -EACCES;

	if (rt2x00_rt(rt2x00dev, RT5390)) {
		rt2800_bbp_read(rt2x00dev, 4, &value);
		rt2x00_set_field8(&value, BBP4_MAC_IF_CTRL, 1);
		rt2800_bbp_write(rt2x00dev, 4, value);
	}

	if (rt2800_is_305x_soc(rt2x00dev) ||
	    rt2x00_rt(rt2x00dev, RT3572) ||
	    rt2x00_rt(rt2x00dev, RT5390))
		rt2800_bbp_write(rt2x00dev, 31, 0x08);

	rt2800_bbp_write(rt2x00dev, 65, 0x2c);
	rt2800_bbp_write(rt2x00dev, 66, 0x38);

	if (rt2x00_rt(rt2x00dev, RT5390))
		rt2800_bbp_write(rt2x00dev, 68, 0x0b);

	if (rt2x00_rt_rev(rt2x00dev, RT2860, REV_RT2860C)) {
		rt2800_bbp_write(rt2x00dev, 69, 0x16);
		rt2800_bbp_write(rt2x00dev, 73, 0x12);
	} else if (rt2x00_rt(rt2x00dev, RT5390)) {
		rt2800_bbp_write(rt2x00dev, 69, 0x12);
		rt2800_bbp_write(rt2x00dev, 73, 0x13);
		rt2800_bbp_write(rt2x00dev, 75, 0x46);
		rt2800_bbp_write(rt2x00dev, 76, 0x28);
		rt2800_bbp_write(rt2x00dev, 77, 0x59);
	} else {
		rt2800_bbp_write(rt2x00dev, 69, 0x12);
		rt2800_bbp_write(rt2x00dev, 73, 0x10);
	}

	rt2800_bbp_write(rt2x00dev, 70, 0x0a);

	if (rt2x00_rt(rt2x00dev, RT3070) ||
	    rt2x00_rt(rt2x00dev, RT3071) ||
	    rt2x00_rt(rt2x00dev, RT3090) ||
	    rt2x00_rt(rt2x00dev, RT3390) ||
	    rt2x00_rt(rt2x00dev, RT3572) ||
	    rt2x00_rt(rt2x00dev, RT5390)) {
		rt2800_bbp_write(rt2x00dev, 79, 0x13);
		rt2800_bbp_write(rt2x00dev, 80, 0x05);
		rt2800_bbp_write(rt2x00dev, 81, 0x33);
	} else if (rt2800_is_305x_soc(rt2x00dev)) {
		rt2800_bbp_write(rt2x00dev, 78, 0x0e);
		rt2800_bbp_write(rt2x00dev, 80, 0x08);
	} else {
		rt2800_bbp_write(rt2x00dev, 81, 0x37);
	}

	rt2800_bbp_write(rt2x00dev, 82, 0x62);
	if (rt2x00_rt(rt2x00dev, RT5390))
		rt2800_bbp_write(rt2x00dev, 83, 0x7a);
	else
		rt2800_bbp_write(rt2x00dev, 83, 0x6a);

	if (rt2x00_rt_rev(rt2x00dev, RT2860, REV_RT2860D))
		rt2800_bbp_write(rt2x00dev, 84, 0x19);
	else if (rt2x00_rt(rt2x00dev, RT5390))
		rt2800_bbp_write(rt2x00dev, 84, 0x9a);
	else
		rt2800_bbp_write(rt2x00dev, 84, 0x99);

	if (rt2x00_rt(rt2x00dev, RT5390))
		rt2800_bbp_write(rt2x00dev, 86, 0x38);
	else
		rt2800_bbp_write(rt2x00dev, 86, 0x00);

	rt2800_bbp_write(rt2x00dev, 91, 0x04);

	if (rt2x00_rt(rt2x00dev, RT5390))
		rt2800_bbp_write(rt2x00dev, 92, 0x02);
	else
		rt2800_bbp_write(rt2x00dev, 92, 0x00);

	if (rt2x00_rt_rev_gte(rt2x00dev, RT3070, REV_RT3070F) ||
	    rt2x00_rt_rev_gte(rt2x00dev, RT3071, REV_RT3071E) ||
	    rt2x00_rt_rev_gte(rt2x00dev, RT3090, REV_RT3090E) ||
	    rt2x00_rt_rev_gte(rt2x00dev, RT3390, REV_RT3390E) ||
	    rt2x00_rt(rt2x00dev, RT3572) ||
	    rt2x00_rt(rt2x00dev, RT5390) ||
	    rt2800_is_305x_soc(rt2x00dev))
		rt2800_bbp_write(rt2x00dev, 103, 0xc0);
	else
		rt2800_bbp_write(rt2x00dev, 103, 0x00);

	if (rt2x00_rt(rt2x00dev, RT5390))
		rt2800_bbp_write(rt2x00dev, 104, 0x92);

	if (rt2800_is_305x_soc(rt2x00dev))
		rt2800_bbp_write(rt2x00dev, 105, 0x01);
	else if (rt2x00_rt(rt2x00dev, RT5390))
		rt2800_bbp_write(rt2x00dev, 105, 0x3c);
	else
		rt2800_bbp_write(rt2x00dev, 105, 0x05);

	if (rt2x00_rt(rt2x00dev, RT5390))
		rt2800_bbp_write(rt2x00dev, 106, 0x03);
	else
		rt2800_bbp_write(rt2x00dev, 106, 0x35);

	if (rt2x00_rt(rt2x00dev, RT5390))
		rt2800_bbp_write(rt2x00dev, 128, 0x12);

	if (rt2x00_rt(rt2x00dev, RT3071) ||
	    rt2x00_rt(rt2x00dev, RT3090) ||
	    rt2x00_rt(rt2x00dev, RT3390) ||
	    rt2x00_rt(rt2x00dev, RT3572) ||
	    rt2x00_rt(rt2x00dev, RT5390)) {
		rt2800_bbp_read(rt2x00dev, 138, &value);

		rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC_CONF0, &eeprom);
		if (rt2x00_get_field16(eeprom, EEPROM_NIC_CONF0_TXPATH) == 1)
			value |= 0x20;
		if (rt2x00_get_field16(eeprom, EEPROM_NIC_CONF0_RXPATH) == 1)
			value &= ~0x02;

		rt2800_bbp_write(rt2x00dev, 138, value);
	}

	if (rt2x00_rt(rt2x00dev, RT5390)) {
		int ant, div_mode;

		rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC_CONF1, &eeprom);
		div_mode = rt2x00_get_field16(eeprom,
					      EEPROM_NIC_CONF1_ANT_DIVERSITY);
		ant = (div_mode == 3) ? 1 : 0;

		/* check if this is a Bluetooth combo card */
		if (test_bit(CAPABILITY_BT_COEXIST, &rt2x00dev->cap_flags)) {
			u32 reg;

			rt2800_register_read(rt2x00dev, GPIO_CTRL_CFG, &reg);
			rt2x00_set_field32(&reg, GPIO_CTRL_CFG_GPIOD_BIT3, 0);
			rt2x00_set_field32(&reg, GPIO_CTRL_CFG_GPIOD_BIT6, 0);
			rt2x00_set_field32(&reg, GPIO_CTRL_CFG_BIT3, 0);
			rt2x00_set_field32(&reg, GPIO_CTRL_CFG_BIT6, 0);
			if (ant == 0)
				rt2x00_set_field32(&reg, GPIO_CTRL_CFG_BIT3, 1);
			else if (ant == 1)
				rt2x00_set_field32(&reg, GPIO_CTRL_CFG_BIT6, 1);
			rt2800_register_write(rt2x00dev, GPIO_CTRL_CFG, reg);
		}

		rt2800_bbp_read(rt2x00dev, 152, &value);
		if (ant == 0)
			rt2x00_set_field8(&value, BBP152_RX_DEFAULT_ANT, 1);
		else
			rt2x00_set_field8(&value, BBP152_RX_DEFAULT_ANT, 0);
		rt2800_bbp_write(rt2x00dev, 152, value);

		/* Init frequency calibration */
		rt2800_bbp_write(rt2x00dev, 142, 1);
		rt2800_bbp_write(rt2x00dev, 143, 57);
	}

	for (i = 0; i < EEPROM_BBP_SIZE; i++) {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_BBP_START + i, &eeprom);

		if (eeprom != 0xffff && eeprom != 0x0000) {
			reg_id = rt2x00_get_field16(eeprom, EEPROM_BBP_REG_ID);
			value = rt2x00_get_field16(eeprom, EEPROM_BBP_VALUE);
			rt2800_bbp_write(rt2x00dev, reg_id, value);
		}
	}

	return 0;
}

static u8 rt2800_init_rx_filter(struct rt2x00_dev *rt2x00dev,
				bool bw40, u8 rfcsr24, u8 filter_target)
{
	unsigned int i;
	u8 bbp;
	u8 rfcsr;
	u8 passband;
	u8 stopband;
	u8 overtuned = 0;

	rt2800_rfcsr_write(rt2x00dev, 24, rfcsr24);

	rt2800_bbp_read(rt2x00dev, 4, &bbp);
	rt2x00_set_field8(&bbp, BBP4_BANDWIDTH, 2 * bw40);
	rt2800_bbp_write(rt2x00dev, 4, bbp);

	rt2800_rfcsr_read(rt2x00dev, 31, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR31_RX_H20M, bw40);
	rt2800_rfcsr_write(rt2x00dev, 31, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 22, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR22_BASEBAND_LOOPBACK, 1);
	rt2800_rfcsr_write(rt2x00dev, 22, rfcsr);

	/*
	 * Set power & frequency of passband test tone
	 */
	rt2800_bbp_write(rt2x00dev, 24, 0);

	for (i = 0; i < 100; i++) {
		rt2800_bbp_write(rt2x00dev, 25, 0x90);
		msleep(1);

		rt2800_bbp_read(rt2x00dev, 55, &passband);
		if (passband)
			break;
	}

	/*
	 * Set power & frequency of stopband test tone
	 */
	rt2800_bbp_write(rt2x00dev, 24, 0x06);

	for (i = 0; i < 100; i++) {
		rt2800_bbp_write(rt2x00dev, 25, 0x90);
		msleep(1);

		rt2800_bbp_read(rt2x00dev, 55, &stopband);

		if ((passband - stopband) <= filter_target) {
			rfcsr24++;
			overtuned += ((passband - stopband) == filter_target);
		} else
			break;

		rt2800_rfcsr_write(rt2x00dev, 24, rfcsr24);
	}

	rfcsr24 -= !!overtuned;

	rt2800_rfcsr_write(rt2x00dev, 24, rfcsr24);
	return rfcsr24;
}

static int rt2800_init_rfcsr(struct rt2x00_dev *rt2x00dev)
{
	u8 rfcsr;
	u8 bbp;
	u32 reg;
	u16 eeprom;

	if (!rt2x00_rt(rt2x00dev, RT3070) &&
	    !rt2x00_rt(rt2x00dev, RT3071) &&
	    !rt2x00_rt(rt2x00dev, RT3090) &&
	    !rt2x00_rt(rt2x00dev, RT3390) &&
	    !rt2x00_rt(rt2x00dev, RT3572) &&
	    !rt2x00_rt(rt2x00dev, RT5390) &&
	    !rt2800_is_305x_soc(rt2x00dev))
		return 0;

	/*
	 * Init RF calibration.
	 */
	if (rt2x00_rt(rt2x00dev, RT5390)) {
		rt2800_rfcsr_read(rt2x00dev, 2, &rfcsr);
		rt2x00_set_field8(&rfcsr, RFCSR2_RESCAL_EN, 1);
		rt2800_rfcsr_write(rt2x00dev, 2, rfcsr);
		msleep(1);
		rt2x00_set_field8(&rfcsr, RFCSR2_RESCAL_EN, 0);
		rt2800_rfcsr_write(rt2x00dev, 2, rfcsr);
	} else {
		rt2800_rfcsr_read(rt2x00dev, 30, &rfcsr);
		rt2x00_set_field8(&rfcsr, RFCSR30_RF_CALIBRATION, 1);
		rt2800_rfcsr_write(rt2x00dev, 30, rfcsr);
		msleep(1);
		rt2x00_set_field8(&rfcsr, RFCSR30_RF_CALIBRATION, 0);
		rt2800_rfcsr_write(rt2x00dev, 30, rfcsr);
	}

	if (rt2x00_rt(rt2x00dev, RT3070) ||
	    rt2x00_rt(rt2x00dev, RT3071) ||
	    rt2x00_rt(rt2x00dev, RT3090)) {
		rt2800_rfcsr_write(rt2x00dev, 4, 0x40);
		rt2800_rfcsr_write(rt2x00dev, 5, 0x03);
		rt2800_rfcsr_write(rt2x00dev, 6, 0x02);
		rt2800_rfcsr_write(rt2x00dev, 7, 0x60);
		rt2800_rfcsr_write(rt2x00dev, 9, 0x0f);
		rt2800_rfcsr_write(rt2x00dev, 10, 0x41);
		rt2800_rfcsr_write(rt2x00dev, 11, 0x21);
		rt2800_rfcsr_write(rt2x00dev, 12, 0x7b);
		rt2800_rfcsr_write(rt2x00dev, 14, 0x90);
		rt2800_rfcsr_write(rt2x00dev, 15, 0x58);
		rt2800_rfcsr_write(rt2x00dev, 16, 0xb3);
		rt2800_rfcsr_write(rt2x00dev, 17, 0x92);
		rt2800_rfcsr_write(rt2x00dev, 18, 0x2c);
		rt2800_rfcsr_write(rt2x00dev, 19, 0x02);
		rt2800_rfcsr_write(rt2x00dev, 20, 0xba);
		rt2800_rfcsr_write(rt2x00dev, 21, 0xdb);
		rt2800_rfcsr_write(rt2x00dev, 24, 0x16);
		rt2800_rfcsr_write(rt2x00dev, 25, 0x01);
		rt2800_rfcsr_write(rt2x00dev, 29, 0x1f);
	} else if (rt2x00_rt(rt2x00dev, RT3390)) {
		rt2800_rfcsr_write(rt2x00dev, 0, 0xa0);
		rt2800_rfcsr_write(rt2x00dev, 1, 0xe1);
		rt2800_rfcsr_write(rt2x00dev, 2, 0xf1);
		rt2800_rfcsr_write(rt2x00dev, 3, 0x62);
		rt2800_rfcsr_write(rt2x00dev, 4, 0x40);
		rt2800_rfcsr_write(rt2x00dev, 5, 0x8b);
		rt2800_rfcsr_write(rt2x00dev, 6, 0x42);
		rt2800_rfcsr_write(rt2x00dev, 7, 0x34);
		rt2800_rfcsr_write(rt2x00dev, 8, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 9, 0xc0);
		rt2800_rfcsr_write(rt2x00dev, 10, 0x61);
		rt2800_rfcsr_write(rt2x00dev, 11, 0x21);
		rt2800_rfcsr_write(rt2x00dev, 12, 0x3b);
		rt2800_rfcsr_write(rt2x00dev, 13, 0xe0);
		rt2800_rfcsr_write(rt2x00dev, 14, 0x90);
		rt2800_rfcsr_write(rt2x00dev, 15, 0x53);
		rt2800_rfcsr_write(rt2x00dev, 16, 0xe0);
		rt2800_rfcsr_write(rt2x00dev, 17, 0x94);
		rt2800_rfcsr_write(rt2x00dev, 18, 0x5c);
		rt2800_rfcsr_write(rt2x00dev, 19, 0x4a);
		rt2800_rfcsr_write(rt2x00dev, 20, 0xb2);
		rt2800_rfcsr_write(rt2x00dev, 21, 0xf6);
		rt2800_rfcsr_write(rt2x00dev, 22, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 23, 0x14);
		rt2800_rfcsr_write(rt2x00dev, 24, 0x08);
		rt2800_rfcsr_write(rt2x00dev, 25, 0x3d);
		rt2800_rfcsr_write(rt2x00dev, 26, 0x85);
		rt2800_rfcsr_write(rt2x00dev, 27, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 28, 0x41);
		rt2800_rfcsr_write(rt2x00dev, 29, 0x8f);
		rt2800_rfcsr_write(rt2x00dev, 30, 0x20);
		rt2800_rfcsr_write(rt2x00dev, 31, 0x0f);
	} else if (rt2x00_rt(rt2x00dev, RT3572)) {
		rt2800_rfcsr_write(rt2x00dev, 0, 0x70);
		rt2800_rfcsr_write(rt2x00dev, 1, 0x81);
		rt2800_rfcsr_write(rt2x00dev, 2, 0xf1);
		rt2800_rfcsr_write(rt2x00dev, 3, 0x02);
		rt2800_rfcsr_write(rt2x00dev, 4, 0x4c);
		rt2800_rfcsr_write(rt2x00dev, 5, 0x05);
		rt2800_rfcsr_write(rt2x00dev, 6, 0x4a);
		rt2800_rfcsr_write(rt2x00dev, 7, 0xd8);
		rt2800_rfcsr_write(rt2x00dev, 9, 0xc3);
		rt2800_rfcsr_write(rt2x00dev, 10, 0xf1);
		rt2800_rfcsr_write(rt2x00dev, 11, 0xb9);
		rt2800_rfcsr_write(rt2x00dev, 12, 0x70);
		rt2800_rfcsr_write(rt2x00dev, 13, 0x65);
		rt2800_rfcsr_write(rt2x00dev, 14, 0xa0);
		rt2800_rfcsr_write(rt2x00dev, 15, 0x53);
		rt2800_rfcsr_write(rt2x00dev, 16, 0x4c);
		rt2800_rfcsr_write(rt2x00dev, 17, 0x23);
		rt2800_rfcsr_write(rt2x00dev, 18, 0xac);
		rt2800_rfcsr_write(rt2x00dev, 19, 0x93);
		rt2800_rfcsr_write(rt2x00dev, 20, 0xb3);
		rt2800_rfcsr_write(rt2x00dev, 21, 0xd0);
		rt2800_rfcsr_write(rt2x00dev, 22, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 23, 0x3c);
		rt2800_rfcsr_write(rt2x00dev, 24, 0x16);
		rt2800_rfcsr_write(rt2x00dev, 25, 0x15);
		rt2800_rfcsr_write(rt2x00dev, 26, 0x85);
		rt2800_rfcsr_write(rt2x00dev, 27, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 28, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 29, 0x9b);
		rt2800_rfcsr_write(rt2x00dev, 30, 0x09);
		rt2800_rfcsr_write(rt2x00dev, 31, 0x10);
	} else if (rt2800_is_305x_soc(rt2x00dev)) {
		rt2800_rfcsr_write(rt2x00dev, 0, 0x50);
		rt2800_rfcsr_write(rt2x00dev, 1, 0x01);
		rt2800_rfcsr_write(rt2x00dev, 2, 0xf7);
		rt2800_rfcsr_write(rt2x00dev, 3, 0x75);
		rt2800_rfcsr_write(rt2x00dev, 4, 0x40);
		rt2800_rfcsr_write(rt2x00dev, 5, 0x03);
		rt2800_rfcsr_write(rt2x00dev, 6, 0x02);
		rt2800_rfcsr_write(rt2x00dev, 7, 0x50);
		rt2800_rfcsr_write(rt2x00dev, 8, 0x39);
		rt2800_rfcsr_write(rt2x00dev, 9, 0x0f);
		rt2800_rfcsr_write(rt2x00dev, 10, 0x60);
		rt2800_rfcsr_write(rt2x00dev, 11, 0x21);
		rt2800_rfcsr_write(rt2x00dev, 12, 0x75);
		rt2800_rfcsr_write(rt2x00dev, 13, 0x75);
		rt2800_rfcsr_write(rt2x00dev, 14, 0x90);
		rt2800_rfcsr_write(rt2x00dev, 15, 0x58);
		rt2800_rfcsr_write(rt2x00dev, 16, 0xb3);
		rt2800_rfcsr_write(rt2x00dev, 17, 0x92);
		rt2800_rfcsr_write(rt2x00dev, 18, 0x2c);
		rt2800_rfcsr_write(rt2x00dev, 19, 0x02);
		rt2800_rfcsr_write(rt2x00dev, 20, 0xba);
		rt2800_rfcsr_write(rt2x00dev, 21, 0xdb);
		rt2800_rfcsr_write(rt2x00dev, 22, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 23, 0x31);
		rt2800_rfcsr_write(rt2x00dev, 24, 0x08);
		rt2800_rfcsr_write(rt2x00dev, 25, 0x01);
		rt2800_rfcsr_write(rt2x00dev, 26, 0x25);
		rt2800_rfcsr_write(rt2x00dev, 27, 0x23);
		rt2800_rfcsr_write(rt2x00dev, 28, 0x13);
		rt2800_rfcsr_write(rt2x00dev, 29, 0x83);
		rt2800_rfcsr_write(rt2x00dev, 30, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 31, 0x00);
		return 0;
	} else if (rt2x00_rt(rt2x00dev, RT5390)) {
		rt2800_rfcsr_write(rt2x00dev, 1, 0x0f);
		rt2800_rfcsr_write(rt2x00dev, 2, 0x80);
		rt2800_rfcsr_write(rt2x00dev, 3, 0x88);
		rt2800_rfcsr_write(rt2x00dev, 5, 0x10);
		if (rt2x00_rt_rev_gte(rt2x00dev, RT5390, REV_RT5390F))
			rt2800_rfcsr_write(rt2x00dev, 6, 0xe0);
		else
			rt2800_rfcsr_write(rt2x00dev, 6, 0xa0);
		rt2800_rfcsr_write(rt2x00dev, 7, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 10, 0x53);
		rt2800_rfcsr_write(rt2x00dev, 11, 0x4a);
		rt2800_rfcsr_write(rt2x00dev, 12, 0xc6);
		rt2800_rfcsr_write(rt2x00dev, 13, 0x9f);
		rt2800_rfcsr_write(rt2x00dev, 14, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 15, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 16, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 18, 0x03);
		rt2800_rfcsr_write(rt2x00dev, 19, 0x00);

		rt2800_rfcsr_write(rt2x00dev, 20, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 21, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 22, 0x20);
		rt2800_rfcsr_write(rt2x00dev, 23, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 24, 0x00);
		if (rt2x00_rt_rev_gte(rt2x00dev, RT5390, REV_RT5390F))
			rt2800_rfcsr_write(rt2x00dev, 25, 0x80);
		else
			rt2800_rfcsr_write(rt2x00dev, 25, 0xc0);
		rt2800_rfcsr_write(rt2x00dev, 26, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 27, 0x09);
		rt2800_rfcsr_write(rt2x00dev, 28, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 29, 0x10);

		rt2800_rfcsr_write(rt2x00dev, 30, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 31, 0x80);
		rt2800_rfcsr_write(rt2x00dev, 32, 0x80);
		rt2800_rfcsr_write(rt2x00dev, 33, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 34, 0x07);
		rt2800_rfcsr_write(rt2x00dev, 35, 0x12);
		rt2800_rfcsr_write(rt2x00dev, 36, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 37, 0x08);
		rt2800_rfcsr_write(rt2x00dev, 38, 0x85);
		rt2800_rfcsr_write(rt2x00dev, 39, 0x1b);

		if (rt2x00_rt_rev_gte(rt2x00dev, RT5390, REV_RT5390F))
			rt2800_rfcsr_write(rt2x00dev, 40, 0x0b);
		else
			rt2800_rfcsr_write(rt2x00dev, 40, 0x4b);
		rt2800_rfcsr_write(rt2x00dev, 41, 0xbb);
		rt2800_rfcsr_write(rt2x00dev, 42, 0xd2);
		rt2800_rfcsr_write(rt2x00dev, 43, 0x9a);
		rt2800_rfcsr_write(rt2x00dev, 44, 0x0e);
		rt2800_rfcsr_write(rt2x00dev, 45, 0xa2);
		if (rt2x00_rt_rev_gte(rt2x00dev, RT5390, REV_RT5390F))
			rt2800_rfcsr_write(rt2x00dev, 46, 0x73);
		else
			rt2800_rfcsr_write(rt2x00dev, 46, 0x7b);
		rt2800_rfcsr_write(rt2x00dev, 47, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 48, 0x10);
		rt2800_rfcsr_write(rt2x00dev, 49, 0x94);

		rt2800_rfcsr_write(rt2x00dev, 52, 0x38);
		if (rt2x00_rt_rev_gte(rt2x00dev, RT5390, REV_RT5390F))
			rt2800_rfcsr_write(rt2x00dev, 53, 0x00);
		else
			rt2800_rfcsr_write(rt2x00dev, 53, 0x84);
		rt2800_rfcsr_write(rt2x00dev, 54, 0x78);
		rt2800_rfcsr_write(rt2x00dev, 55, 0x44);
		rt2800_rfcsr_write(rt2x00dev, 56, 0x22);
		rt2800_rfcsr_write(rt2x00dev, 57, 0x80);
		rt2800_rfcsr_write(rt2x00dev, 58, 0x7f);
		rt2800_rfcsr_write(rt2x00dev, 59, 0x63);

		rt2800_rfcsr_write(rt2x00dev, 60, 0x45);
		if (rt2x00_rt_rev_gte(rt2x00dev, RT5390, REV_RT5390F))
			rt2800_rfcsr_write(rt2x00dev, 61, 0xd1);
		else
			rt2800_rfcsr_write(rt2x00dev, 61, 0xdd);
		rt2800_rfcsr_write(rt2x00dev, 62, 0x00);
		rt2800_rfcsr_write(rt2x00dev, 63, 0x00);
	}

	if (rt2x00_rt_rev_lt(rt2x00dev, RT3070, REV_RT3070F)) {
		rt2800_register_read(rt2x00dev, LDO_CFG0, &reg);
		rt2x00_set_field32(&reg, LDO_CFG0_BGSEL, 1);
		rt2x00_set_field32(&reg, LDO_CFG0_LDO_CORE_VLEVEL, 3);
		rt2800_register_write(rt2x00dev, LDO_CFG0, reg);
	} else if (rt2x00_rt(rt2x00dev, RT3071) ||
		   rt2x00_rt(rt2x00dev, RT3090)) {
		rt2800_rfcsr_write(rt2x00dev, 31, 0x14);

		rt2800_rfcsr_read(rt2x00dev, 6, &rfcsr);
		rt2x00_set_field8(&rfcsr, RFCSR6_R2, 1);
		rt2800_rfcsr_write(rt2x00dev, 6, rfcsr);

		rt2800_register_read(rt2x00dev, LDO_CFG0, &reg);
		rt2x00_set_field32(&reg, LDO_CFG0_BGSEL, 1);
		if (rt2x00_rt_rev_lt(rt2x00dev, RT3071, REV_RT3071E) ||
		    rt2x00_rt_rev_lt(rt2x00dev, RT3090, REV_RT3090E)) {
			rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC_CONF1, &eeprom);
			if (rt2x00_get_field16(eeprom, EEPROM_NIC_CONF1_DAC_TEST))
				rt2x00_set_field32(&reg, LDO_CFG0_LDO_CORE_VLEVEL, 3);
			else
				rt2x00_set_field32(&reg, LDO_CFG0_LDO_CORE_VLEVEL, 0);
		}
		rt2800_register_write(rt2x00dev, LDO_CFG0, reg);

		rt2800_register_read(rt2x00dev, GPIO_SWITCH, &reg);
		rt2x00_set_field32(&reg, GPIO_SWITCH_5, 0);
		rt2800_register_write(rt2x00dev, GPIO_SWITCH, reg);
	} else if (rt2x00_rt(rt2x00dev, RT3390)) {
		rt2800_register_read(rt2x00dev, GPIO_SWITCH, &reg);
		rt2x00_set_field32(&reg, GPIO_SWITCH_5, 0);
		rt2800_register_write(rt2x00dev, GPIO_SWITCH, reg);
	} else if (rt2x00_rt(rt2x00dev, RT3572)) {
		rt2800_rfcsr_read(rt2x00dev, 6, &rfcsr);
		rt2x00_set_field8(&rfcsr, RFCSR6_R2, 1);
		rt2800_rfcsr_write(rt2x00dev, 6, rfcsr);

		rt2800_register_read(rt2x00dev, LDO_CFG0, &reg);
		rt2x00_set_field32(&reg, LDO_CFG0_LDO_CORE_VLEVEL, 3);
		rt2x00_set_field32(&reg, LDO_CFG0_BGSEL, 1);
		rt2800_register_write(rt2x00dev, LDO_CFG0, reg);
		msleep(1);
		rt2800_register_read(rt2x00dev, LDO_CFG0, &reg);
		rt2x00_set_field32(&reg, LDO_CFG0_BGSEL, 1);
		rt2800_register_write(rt2x00dev, LDO_CFG0, reg);
	}

	/*
	 * Set RX Filter calibration for 20MHz and 40MHz
	 */
	if (rt2x00_rt(rt2x00dev, RT3070)) {
		rt2x00dev->calibration[0] =
			rt2800_init_rx_filter(rt2x00dev, false, 0x07, 0x16);
		rt2x00dev->calibration[1] =
			rt2800_init_rx_filter(rt2x00dev, true, 0x27, 0x19);
	} else if (rt2x00_rt(rt2x00dev, RT3071) ||
		   rt2x00_rt(rt2x00dev, RT3090) ||
		   rt2x00_rt(rt2x00dev, RT3390) ||
		   rt2x00_rt(rt2x00dev, RT3572)) {
		rt2x00dev->calibration[0] =
			rt2800_init_rx_filter(rt2x00dev, false, 0x07, 0x13);
		rt2x00dev->calibration[1] =
			rt2800_init_rx_filter(rt2x00dev, true, 0x27, 0x15);
	}

	if (!rt2x00_rt(rt2x00dev, RT5390)) {
		/*
		 * Set back to initial state
		 */
		rt2800_bbp_write(rt2x00dev, 24, 0);

		rt2800_rfcsr_read(rt2x00dev, 22, &rfcsr);
		rt2x00_set_field8(&rfcsr, RFCSR22_BASEBAND_LOOPBACK, 0);
		rt2800_rfcsr_write(rt2x00dev, 22, rfcsr);

		/*
		 * Set BBP back to BW20
		 */
		rt2800_bbp_read(rt2x00dev, 4, &bbp);
		rt2x00_set_field8(&bbp, BBP4_BANDWIDTH, 0);
		rt2800_bbp_write(rt2x00dev, 4, bbp);
	}

	if (rt2x00_rt_rev_lt(rt2x00dev, RT3070, REV_RT3070F) ||
	    rt2x00_rt_rev_lt(rt2x00dev, RT3071, REV_RT3071E) ||
	    rt2x00_rt_rev_lt(rt2x00dev, RT3090, REV_RT3090E) ||
	    rt2x00_rt_rev_lt(rt2x00dev, RT3390, REV_RT3390E))
		rt2800_rfcsr_write(rt2x00dev, 27, 0x03);

	rt2800_register_read(rt2x00dev, OPT_14_CSR, &reg);
	rt2x00_set_field32(&reg, OPT_14_CSR_BIT0, 1);
	rt2800_register_write(rt2x00dev, OPT_14_CSR, reg);

	if (!rt2x00_rt(rt2x00dev, RT5390)) {
		rt2800_rfcsr_read(rt2x00dev, 17, &rfcsr);
		rt2x00_set_field8(&rfcsr, RFCSR17_TX_LO1_EN, 0);
		if (rt2x00_rt(rt2x00dev, RT3070) ||
		    rt2x00_rt_rev_lt(rt2x00dev, RT3071, REV_RT3071E) ||
		    rt2x00_rt_rev_lt(rt2x00dev, RT3090, REV_RT3090E) ||
		    rt2x00_rt_rev_lt(rt2x00dev, RT3390, REV_RT3390E)) {
			if (!test_bit(CAPABILITY_EXTERNAL_LNA_BG,
				      &rt2x00dev->cap_flags))
				rt2x00_set_field8(&rfcsr, RFCSR17_R, 1);
		}
		rt2x00_eeprom_read(rt2x00dev, EEPROM_TXMIXER_GAIN_BG, &eeprom);
		if (rt2x00_get_field16(eeprom, EEPROM_TXMIXER_GAIN_BG_VAL) >= 1)
			rt2x00_set_field8(&rfcsr, RFCSR17_TXMIXER_GAIN,
					rt2x00_get_field16(eeprom,
						EEPROM_TXMIXER_GAIN_BG_VAL));
		rt2800_rfcsr_write(rt2x00dev, 17, rfcsr);
	}

	if (rt2x00_rt(rt2x00dev, RT3090)) {
		rt2800_bbp_read(rt2x00dev, 138, &bbp);

		/*  Turn off unused DAC1 and ADC1 to reduce power consumption */
		rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC_CONF0, &eeprom);
		if (rt2x00_get_field16(eeprom, EEPROM_NIC_CONF0_RXPATH) == 1)
			rt2x00_set_field8(&bbp, BBP138_RX_ADC1, 0);
		if (rt2x00_get_field16(eeprom, EEPROM_NIC_CONF0_TXPATH) == 1)
			rt2x00_set_field8(&bbp, BBP138_TX_DAC1, 1);

		rt2800_bbp_write(rt2x00dev, 138, bbp);
	}

	if (rt2x00_rt(rt2x00dev, RT3071) ||
	    rt2x00_rt(rt2x00dev, RT3090) ||
	    rt2x00_rt(rt2x00dev, RT3390)) {
		rt2800_rfcsr_read(rt2x00dev, 1, &rfcsr);
		rt2x00_set_field8(&rfcsr, RFCSR1_RF_BLOCK_EN, 1);
		rt2x00_set_field8(&rfcsr, RFCSR1_RX0_PD, 0);
		rt2x00_set_field8(&rfcsr, RFCSR1_TX0_PD, 0);
		rt2x00_set_field8(&rfcsr, RFCSR1_RX1_PD, 1);
		rt2x00_set_field8(&rfcsr, RFCSR1_TX1_PD, 1);
		rt2800_rfcsr_write(rt2x00dev, 1, rfcsr);

		rt2800_rfcsr_read(rt2x00dev, 15, &rfcsr);
		rt2x00_set_field8(&rfcsr, RFCSR15_TX_LO2_EN, 0);
		rt2800_rfcsr_write(rt2x00dev, 15, rfcsr);

		rt2800_rfcsr_read(rt2x00dev, 20, &rfcsr);
		rt2x00_set_field8(&rfcsr, RFCSR20_RX_LO1_EN, 0);
		rt2800_rfcsr_write(rt2x00dev, 20, rfcsr);

		rt2800_rfcsr_read(rt2x00dev, 21, &rfcsr);
		rt2x00_set_field8(&rfcsr, RFCSR21_RX_LO2_EN, 0);
		rt2800_rfcsr_write(rt2x00dev, 21, rfcsr);
	}

	if (rt2x00_rt(rt2x00dev, RT3070)) {
		rt2800_rfcsr_read(rt2x00dev, 27, &rfcsr);
		if (rt2x00_rt_rev_lt(rt2x00dev, RT3070, REV_RT3070F))
			rt2x00_set_field8(&rfcsr, RFCSR27_R1, 3);
		else
			rt2x00_set_field8(&rfcsr, RFCSR27_R1, 0);
		rt2x00_set_field8(&rfcsr, RFCSR27_R2, 0);
		rt2x00_set_field8(&rfcsr, RFCSR27_R3, 0);
		rt2x00_set_field8(&rfcsr, RFCSR27_R4, 0);
		rt2800_rfcsr_write(rt2x00dev, 27, rfcsr);
	}

	if (rt2x00_rt(rt2x00dev, RT5390)) {
		rt2800_rfcsr_read(rt2x00dev, 38, &rfcsr);
		rt2x00_set_field8(&rfcsr, RFCSR38_RX_LO1_EN, 0);
		rt2800_rfcsr_write(rt2x00dev, 38, rfcsr);

		rt2800_rfcsr_read(rt2x00dev, 39, &rfcsr);
		rt2x00_set_field8(&rfcsr, RFCSR39_RX_LO2_EN, 0);
		rt2800_rfcsr_write(rt2x00dev, 39, rfcsr);

		rt2800_rfcsr_read(rt2x00dev, 30, &rfcsr);
		rt2x00_set_field8(&rfcsr, RFCSR30_RX_VCM, 2);
		rt2800_rfcsr_write(rt2x00dev, 30, rfcsr);
	}

	return 0;
}

int rt2800_enable_radio(struct rt2x00_dev *rt2x00dev)
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

	/*
	 * Send signal to firmware during boot time.
	 */
	rt2800_mcu_request(rt2x00dev, MCU_BOOT_SIGNAL, 0, 0, 0);

	if (rt2x00_is_usb(rt2x00dev) &&
	    (rt2x00_rt(rt2x00dev, RT3070) ||
	     rt2x00_rt(rt2x00dev, RT3071) ||
	     rt2x00_rt(rt2x00dev, RT3572))) {
		udelay(200);
		rt2800_mcu_request(rt2x00dev, MCU_CURRENT, 0, 0, 0);
		udelay(10);
	}

	/*
	 * Enable RX.
	 */
	rt2800_register_read(rt2x00dev, MAC_SYS_CTRL, &reg);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_TX, 1);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_RX, 0);
	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, reg);

	udelay(50);

	rt2800_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_TX_DMA, 1);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_RX_DMA, 1);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_WP_DMA_BURST_SIZE, 2);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_WRITEBACK_DONE, 1);
	rt2800_register_write(rt2x00dev, WPDMA_GLO_CFG, reg);

	rt2800_register_read(rt2x00dev, MAC_SYS_CTRL, &reg);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_TX, 1);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_RX, 1);
	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, reg);

	/*
	 * Initialize LED control
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED_AG_CONF, &word);
	rt2800_mcu_request(rt2x00dev, MCU_LED_AG_CONF, 0xff,
			   word & 0xff, (word >> 8) & 0xff);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED_ACT_CONF, &word);
	rt2800_mcu_request(rt2x00dev, MCU_LED_ACT_CONF, 0xff,
			   word & 0xff, (word >> 8) & 0xff);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED_POLARITY, &word);
	rt2800_mcu_request(rt2x00dev, MCU_LED_LED_POLARITY, 0xff,
			   word & 0xff, (word >> 8) & 0xff);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_enable_radio);

void rt2800_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2800_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_TX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_RX_DMA, 0);
	rt2800_register_write(rt2x00dev, WPDMA_GLO_CFG, reg);

	/* Wait for DMA, ignore error */
	rt2800_wait_wpdma_ready(rt2x00dev);

	rt2800_register_read(rt2x00dev, MAC_SYS_CTRL, &reg);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_TX, 0);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_RX, 0);
	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, reg);
}
EXPORT_SYMBOL_GPL(rt2800_disable_radio);

int rt2800_efuse_detect(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2800_register_read(rt2x00dev, EFUSE_CTRL, &reg);

	return rt2x00_get_field32(reg, EFUSE_CTRL_PRESENT);
}
EXPORT_SYMBOL_GPL(rt2800_efuse_detect);

static void rt2800_efuse_read(struct rt2x00_dev *rt2x00dev, unsigned int i)
{
	u32 reg;

	mutex_lock(&rt2x00dev->csr_mutex);

	rt2800_register_read_lock(rt2x00dev, EFUSE_CTRL, &reg);
	rt2x00_set_field32(&reg, EFUSE_CTRL_ADDRESS_IN, i);
	rt2x00_set_field32(&reg, EFUSE_CTRL_MODE, 0);
	rt2x00_set_field32(&reg, EFUSE_CTRL_KICK, 1);
	rt2800_register_write_lock(rt2x00dev, EFUSE_CTRL, reg);

	/* Wait until the EEPROM has been loaded */
	rt2800_regbusy_read(rt2x00dev, EFUSE_CTRL, EFUSE_CTRL_KICK, &reg);

	/* Apparently the data is read from end to start */
	rt2800_register_read_lock(rt2x00dev, EFUSE_DATA3, &reg);
	/* The returned value is in CPU order, but eeprom is le */
	*(u32 *)&rt2x00dev->eeprom[i] = cpu_to_le32(reg);
	rt2800_register_read_lock(rt2x00dev, EFUSE_DATA2, &reg);
	*(u32 *)&rt2x00dev->eeprom[i + 2] = cpu_to_le32(reg);
	rt2800_register_read_lock(rt2x00dev, EFUSE_DATA1, &reg);
	*(u32 *)&rt2x00dev->eeprom[i + 4] = cpu_to_le32(reg);
	rt2800_register_read_lock(rt2x00dev, EFUSE_DATA0, &reg);
	*(u32 *)&rt2x00dev->eeprom[i + 6] = cpu_to_le32(reg);

	mutex_unlock(&rt2x00dev->csr_mutex);
}

void rt2800_read_eeprom_efuse(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;

	for (i = 0; i < EEPROM_SIZE / sizeof(u16); i += 8)
		rt2800_efuse_read(rt2x00dev, i);
}
EXPORT_SYMBOL_GPL(rt2800_read_eeprom_efuse);

int rt2800_validate_eeprom(struct rt2x00_dev *rt2x00dev)
{
	u16 word;
	u8 *mac;
	u8 default_lna_gain;

	/*
	 * Start validation of the data that has been read.
	 */
	mac = rt2x00_eeprom_addr(rt2x00dev, EEPROM_MAC_ADDR_0);
	if (!is_valid_ether_addr(mac)) {
		random_ether_addr(mac);
		EEPROM(rt2x00dev, "MAC: %pM\n", mac);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC_CONF0, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_NIC_CONF0_RXPATH, 2);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF0_TXPATH, 1);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF0_RF_TYPE, RF2820);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_NIC_CONF0, word);
		EEPROM(rt2x00dev, "Antenna: 0x%04x\n", word);
	} else if (rt2x00_rt(rt2x00dev, RT2860) ||
		   rt2x00_rt(rt2x00dev, RT2872)) {
		/*
		 * There is a max of 2 RX streams for RT28x0 series
		 */
		if (rt2x00_get_field16(word, EEPROM_NIC_CONF0_RXPATH) > 2)
			rt2x00_set_field16(&word, EEPROM_NIC_CONF0_RXPATH, 2);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_NIC_CONF0, word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC_CONF1, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_HW_RADIO, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_EXTERNAL_TX_ALC, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_EXTERNAL_LNA_2G, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_EXTERNAL_LNA_5G, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_CARDBUS_ACCEL, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_BW40M_SB_2G, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_BW40M_SB_5G, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_WPS_PBC, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_BW40M_2G, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_BW40M_5G, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_BROADBAND_EXT_LNA, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_ANT_DIVERSITY, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_INTERNAL_TX_ALC, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_BT_COEXIST, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CONF1_DAC_TEST, 0);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_NIC_CONF1, word);
		EEPROM(rt2x00dev, "NIC: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_FREQ, &word);
	if ((word & 0x00ff) == 0x00ff) {
		rt2x00_set_field16(&word, EEPROM_FREQ_OFFSET, 0);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_FREQ, word);
		EEPROM(rt2x00dev, "Freq: 0x%04x\n", word);
	}
	if ((word & 0xff00) == 0xff00) {
		rt2x00_set_field16(&word, EEPROM_FREQ_LED_MODE,
				   LED_MODE_TXRX_ACTIVITY);
		rt2x00_set_field16(&word, EEPROM_FREQ_LED_POLARITY, 0);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_FREQ, word);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_LED_AG_CONF, 0x5555);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_LED_ACT_CONF, 0x2221);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_LED_POLARITY, 0xa9f8);
		EEPROM(rt2x00dev, "Led Mode: 0x%04x\n", word);
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
EXPORT_SYMBOL_GPL(rt2800_validate_eeprom);

int rt2800_init_eeprom(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	u16 value;
	u16 eeprom;

	/*
	 * Read EEPROM word for configuration.
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC_CONF0, &eeprom);

	/*
	 * Identify RF chipset by EEPROM value
	 * RT28xx/RT30xx: defined in "EEPROM_NIC_CONF0_RF_TYPE" field
	 * RT53xx: defined in "EEPROM_CHIP_ID" field
	 */
	rt2800_register_read(rt2x00dev, MAC_CSR0, &reg);
	if (rt2x00_get_field32(reg, MAC_CSR0_CHIPSET) == RT5390)
		rt2x00_eeprom_read(rt2x00dev, EEPROM_CHIP_ID, &value);
	else
		value = rt2x00_get_field16(eeprom, EEPROM_NIC_CONF0_RF_TYPE);

	rt2x00_set_chip(rt2x00dev, rt2x00_get_field32(reg, MAC_CSR0_CHIPSET),
			value, rt2x00_get_field32(reg, MAC_CSR0_REVISION));

	switch (rt2x00dev->chip.rt) {
	case RT2860:
	case RT2872:
	case RT2883:
	case RT3070:
	case RT3071:
	case RT3090:
	case RT3390:
	case RT3572:
	case RT5390:
		break;
	default:
		ERROR(rt2x00dev, "Invalid RT chipset detected.\n");
		return -ENODEV;
	}

	switch (rt2x00dev->chip.rf) {
	case RF2820:
	case RF2850:
	case RF2720:
	case RF2750:
	case RF3020:
	case RF2020:
	case RF3021:
	case RF3022:
	case RF3052:
	case RF3320:
	case RF5370:
	case RF5390:
		break;
	default:
		ERROR(rt2x00dev, "Invalid RF chipset 0x%x detected.\n",
		      rt2x00dev->chip.rf);
		return -ENODEV;
	}

	/*
	 * Identify default antenna configuration.
	 */
	rt2x00dev->default_ant.tx_chain_num =
	    rt2x00_get_field16(eeprom, EEPROM_NIC_CONF0_TXPATH);
	rt2x00dev->default_ant.rx_chain_num =
	    rt2x00_get_field16(eeprom, EEPROM_NIC_CONF0_RXPATH);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC_CONF1, &eeprom);

	if (rt2x00_rt(rt2x00dev, RT3070) ||
	    rt2x00_rt(rt2x00dev, RT3090) ||
	    rt2x00_rt(rt2x00dev, RT3390)) {
		value = rt2x00_get_field16(eeprom,
				EEPROM_NIC_CONF1_ANT_DIVERSITY);
		switch (value) {
		case 0:
		case 1:
		case 2:
			rt2x00dev->default_ant.tx = ANTENNA_A;
			rt2x00dev->default_ant.rx = ANTENNA_A;
			break;
		case 3:
			rt2x00dev->default_ant.tx = ANTENNA_A;
			rt2x00dev->default_ant.rx = ANTENNA_B;
			break;
		}
	} else {
		rt2x00dev->default_ant.tx = ANTENNA_A;
		rt2x00dev->default_ant.rx = ANTENNA_A;
	}

	/*
	 * Determine external LNA informations.
	 */
	if (rt2x00_get_field16(eeprom, EEPROM_NIC_CONF1_EXTERNAL_LNA_5G))
		__set_bit(CAPABILITY_EXTERNAL_LNA_A, &rt2x00dev->cap_flags);
	if (rt2x00_get_field16(eeprom, EEPROM_NIC_CONF1_EXTERNAL_LNA_2G))
		__set_bit(CAPABILITY_EXTERNAL_LNA_BG, &rt2x00dev->cap_flags);

	/*
	 * Detect if this device has an hardware controlled radio.
	 */
	if (rt2x00_get_field16(eeprom, EEPROM_NIC_CONF1_HW_RADIO))
		__set_bit(CAPABILITY_HW_BUTTON, &rt2x00dev->cap_flags);

	/*
	 * Detect if this device has Bluetooth co-existence.
	 */
	if (rt2x00_get_field16(eeprom, EEPROM_NIC_CONF1_BT_COEXIST))
		__set_bit(CAPABILITY_BT_COEXIST, &rt2x00dev->cap_flags);

	/*
	 * Read frequency offset and RF programming sequence.
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_FREQ, &eeprom);
	rt2x00dev->freq_offset = rt2x00_get_field16(eeprom, EEPROM_FREQ_OFFSET);

	/*
	 * Store led settings, for correct led behaviour.
	 */
#ifdef CONFIG_RT2X00_LIB_LEDS
	rt2800_init_led(rt2x00dev, &rt2x00dev->led_radio, LED_TYPE_RADIO);
	rt2800_init_led(rt2x00dev, &rt2x00dev->led_assoc, LED_TYPE_ASSOC);
	rt2800_init_led(rt2x00dev, &rt2x00dev->led_qual, LED_TYPE_QUALITY);

	rt2x00dev->led_mcu_reg = eeprom;
#endif /* CONFIG_RT2X00_LIB_LEDS */

	/*
	 * Check if support EIRP tx power limit feature.
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_EIRP_MAX_TX_POWER, &eeprom);

	if (rt2x00_get_field16(eeprom, EEPROM_EIRP_MAX_TX_POWER_2GHZ) <
					EIRP_MAX_TX_POWER_LIMIT)
		__set_bit(CAPABILITY_POWER_LIMIT, &rt2x00dev->cap_flags);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_init_eeprom);

/*
 * RF value list for rt28xx
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
 * RF value list for rt3xxx
 * Supports: 2.4 GHz (all) & 5.2 GHz (RF3052)
 */
static const struct rf_channel rf_vals_3x[] = {
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

	/* 802.11 UNI / HyperLan 2 */
	{36, 0x56, 0, 4},
	{38, 0x56, 0, 6},
	{40, 0x56, 0, 8},
	{44, 0x57, 0, 0},
	{46, 0x57, 0, 2},
	{48, 0x57, 0, 4},
	{52, 0x57, 0, 8},
	{54, 0x57, 0, 10},
	{56, 0x58, 0, 0},
	{60, 0x58, 0, 4},
	{62, 0x58, 0, 6},
	{64, 0x58, 0, 8},

	/* 802.11 HyperLan 2 */
	{100, 0x5b, 0, 8},
	{102, 0x5b, 0, 10},
	{104, 0x5c, 0, 0},
	{108, 0x5c, 0, 4},
	{110, 0x5c, 0, 6},
	{112, 0x5c, 0, 8},
	{116, 0x5d, 0, 0},
	{118, 0x5d, 0, 2},
	{120, 0x5d, 0, 4},
	{124, 0x5d, 0, 8},
	{126, 0x5d, 0, 10},
	{128, 0x5e, 0, 0},
	{132, 0x5e, 0, 4},
	{134, 0x5e, 0, 6},
	{136, 0x5e, 0, 8},
	{140, 0x5f, 0, 0},

	/* 802.11 UNII */
	{149, 0x5f, 0, 9},
	{151, 0x5f, 0, 11},
	{153, 0x60, 0, 1},
	{157, 0x60, 0, 5},
	{159, 0x60, 0, 7},
	{161, 0x60, 0, 9},
	{165, 0x61, 0, 1},
	{167, 0x61, 0, 3},
	{169, 0x61, 0, 5},
	{171, 0x61, 0, 7},
	{173, 0x61, 0, 9},
};

int rt2800_probe_hw_mode(struct rt2x00_dev *rt2x00dev)
{
	struct hw_mode_spec *spec = &rt2x00dev->spec;
	struct channel_info *info;
	char *default_power1;
	char *default_power2;
	unsigned int i;
	u16 eeprom;

	/*
	 * Disable powersaving as default on PCI devices.
	 */
	if (rt2x00_is_pci(rt2x00dev) || rt2x00_is_soc(rt2x00dev))
		rt2x00dev->hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	/*
	 * Initialize all hw fields.
	 */
	rt2x00dev->hw->flags =
	    IEEE80211_HW_SIGNAL_DBM |
	    IEEE80211_HW_SUPPORTS_PS |
	    IEEE80211_HW_PS_NULLFUNC_STACK |
	    IEEE80211_HW_AMPDU_AGGREGATION;
	/*
	 * Don't set IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING for USB devices
	 * unless we are capable of sending the buffered frames out after the
	 * DTIM transmission using rt2x00lib_beacondone. This will send out
	 * multicast and broadcast traffic immediately instead of buffering it
	 * infinitly and thus dropping it after some time.
	 */
	if (!rt2x00_is_usb(rt2x00dev))
		rt2x00dev->hw->flags |=
			IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING;

	SET_IEEE80211_DEV(rt2x00dev->hw, rt2x00dev->dev);
	SET_IEEE80211_PERM_ADDR(rt2x00dev->hw,
				rt2x00_eeprom_addr(rt2x00dev,
						   EEPROM_MAC_ADDR_0));

	/*
	 * As rt2800 has a global fallback table we cannot specify
	 * more then one tx rate per frame but since the hw will
	 * try several rates (based on the fallback table) we should
	 * initialize max_report_rates to the maximum number of rates
	 * we are going to try. Otherwise mac80211 will truncate our
	 * reported tx rates and the rc algortihm will end up with
	 * incorrect data.
	 */
	rt2x00dev->hw->max_rates = 1;
	rt2x00dev->hw->max_report_rates = 7;
	rt2x00dev->hw->max_rate_tries = 1;

	rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC_CONF0, &eeprom);

	/*
	 * Initialize hw_mode information.
	 */
	spec->supported_bands = SUPPORT_BAND_2GHZ;
	spec->supported_rates = SUPPORT_RATE_CCK | SUPPORT_RATE_OFDM;

	if (rt2x00_rf(rt2x00dev, RF2820) ||
	    rt2x00_rf(rt2x00dev, RF2720)) {
		spec->num_channels = 14;
		spec->channels = rf_vals;
	} else if (rt2x00_rf(rt2x00dev, RF2850) ||
		   rt2x00_rf(rt2x00dev, RF2750)) {
		spec->supported_bands |= SUPPORT_BAND_5GHZ;
		spec->num_channels = ARRAY_SIZE(rf_vals);
		spec->channels = rf_vals;
	} else if (rt2x00_rf(rt2x00dev, RF3020) ||
		   rt2x00_rf(rt2x00dev, RF2020) ||
		   rt2x00_rf(rt2x00dev, RF3021) ||
		   rt2x00_rf(rt2x00dev, RF3022) ||
		   rt2x00_rf(rt2x00dev, RF3320) ||
		   rt2x00_rf(rt2x00dev, RF5370) ||
		   rt2x00_rf(rt2x00dev, RF5390)) {
		spec->num_channels = 14;
		spec->channels = rf_vals_3x;
	} else if (rt2x00_rf(rt2x00dev, RF3052)) {
		spec->supported_bands |= SUPPORT_BAND_5GHZ;
		spec->num_channels = ARRAY_SIZE(rf_vals_3x);
		spec->channels = rf_vals_3x;
	}

	/*
	 * Initialize HT information.
	 */
	if (!rt2x00_rf(rt2x00dev, RF2020))
		spec->ht.ht_supported = true;
	else
		spec->ht.ht_supported = false;

	spec->ht.cap =
	    IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
	    IEEE80211_HT_CAP_GRN_FLD |
	    IEEE80211_HT_CAP_SGI_20 |
	    IEEE80211_HT_CAP_SGI_40;

	if (rt2x00_get_field16(eeprom, EEPROM_NIC_CONF0_TXPATH) >= 2)
		spec->ht.cap |= IEEE80211_HT_CAP_TX_STBC;

	spec->ht.cap |=
	    rt2x00_get_field16(eeprom, EEPROM_NIC_CONF0_RXPATH) <<
		IEEE80211_HT_CAP_RX_STBC_SHIFT;

	spec->ht.ampdu_factor = 3;
	spec->ht.ampdu_density = 4;
	spec->ht.mcs.tx_params =
	    IEEE80211_HT_MCS_TX_DEFINED |
	    IEEE80211_HT_MCS_TX_RX_DIFF |
	    ((rt2x00_get_field16(eeprom, EEPROM_NIC_CONF0_TXPATH) - 1) <<
		IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT);

	switch (rt2x00_get_field16(eeprom, EEPROM_NIC_CONF0_RXPATH)) {
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
	 * Create channel information array
	 */
	info = kcalloc(spec->num_channels, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	spec->channels_info = info;

	default_power1 = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_BG1);
	default_power2 = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_BG2);

	for (i = 0; i < 14; i++) {
		info[i].default_power1 = default_power1[i];
		info[i].default_power2 = default_power2[i];
	}

	if (spec->num_channels > 14) {
		default_power1 = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_A1);
		default_power2 = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_A2);

		for (i = 14; i < spec->num_channels; i++) {
			info[i].default_power1 = default_power1[i];
			info[i].default_power2 = default_power2[i];
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_probe_hw_mode);

/*
 * IEEE80211 stack callback functions.
 */
void rt2800_get_tkip_seq(struct ieee80211_hw *hw, u8 hw_key_idx, u32 *iv32,
			 u16 *iv16)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct mac_iveiv_entry iveiv_entry;
	u32 offset;

	offset = MAC_IVEIV_ENTRY(hw_key_idx);
	rt2800_register_multiread(rt2x00dev, offset,
				      &iveiv_entry, sizeof(iveiv_entry));

	memcpy(iv16, &iveiv_entry.iv[0], sizeof(*iv16));
	memcpy(iv32, &iveiv_entry.iv[4], sizeof(*iv32));
}
EXPORT_SYMBOL_GPL(rt2800_get_tkip_seq);

int rt2800_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	u32 reg;
	bool enabled = (value < IEEE80211_MAX_RTS_THRESHOLD);

	rt2800_register_read(rt2x00dev, TX_RTS_CFG, &reg);
	rt2x00_set_field32(&reg, TX_RTS_CFG_RTS_THRES, value);
	rt2800_register_write(rt2x00dev, TX_RTS_CFG, reg);

	rt2800_register_read(rt2x00dev, CCK_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_RTS_TH_EN, enabled);
	rt2800_register_write(rt2x00dev, CCK_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, OFDM_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_RTS_TH_EN, enabled);
	rt2800_register_write(rt2x00dev, OFDM_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, MM20_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_RTS_TH_EN, enabled);
	rt2800_register_write(rt2x00dev, MM20_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, MM40_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_RTS_TH_EN, enabled);
	rt2800_register_write(rt2x00dev, MM40_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, GF20_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_RTS_TH_EN, enabled);
	rt2800_register_write(rt2x00dev, GF20_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, GF40_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_RTS_TH_EN, enabled);
	rt2800_register_write(rt2x00dev, GF40_PROT_CFG, reg);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_set_rts_threshold);

int rt2800_conf_tx(struct ieee80211_hw *hw,
		   struct ieee80211_vif *vif, u16 queue_idx,
		   const struct ieee80211_tx_queue_params *params)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct data_queue *queue;
	struct rt2x00_field32 field;
	int retval;
	u32 reg;
	u32 offset;

	/*
	 * First pass the configuration through rt2x00lib, that will
	 * update the queue settings and validate the input. After that
	 * we are free to update the registers based on the value
	 * in the queue parameter.
	 */
	retval = rt2x00mac_conf_tx(hw, vif, queue_idx, params);
	if (retval)
		return retval;

	/*
	 * We only need to perform additional register initialization
	 * for WMM queues/
	 */
	if (queue_idx >= 4)
		return 0;

	queue = rt2x00queue_get_tx_queue(rt2x00dev, queue_idx);

	/* Update WMM TXOP register */
	offset = WMM_TXOP0_CFG + (sizeof(u32) * (!!(queue_idx & 2)));
	field.bit_offset = (queue_idx & 1) * 16;
	field.bit_mask = 0xffff << field.bit_offset;

	rt2800_register_read(rt2x00dev, offset, &reg);
	rt2x00_set_field32(&reg, field, queue->txop);
	rt2800_register_write(rt2x00dev, offset, reg);

	/* Update WMM registers */
	field.bit_offset = queue_idx * 4;
	field.bit_mask = 0xf << field.bit_offset;

	rt2800_register_read(rt2x00dev, WMM_AIFSN_CFG, &reg);
	rt2x00_set_field32(&reg, field, queue->aifs);
	rt2800_register_write(rt2x00dev, WMM_AIFSN_CFG, reg);

	rt2800_register_read(rt2x00dev, WMM_CWMIN_CFG, &reg);
	rt2x00_set_field32(&reg, field, queue->cw_min);
	rt2800_register_write(rt2x00dev, WMM_CWMIN_CFG, reg);

	rt2800_register_read(rt2x00dev, WMM_CWMAX_CFG, &reg);
	rt2x00_set_field32(&reg, field, queue->cw_max);
	rt2800_register_write(rt2x00dev, WMM_CWMAX_CFG, reg);

	/* Update EDCA registers */
	offset = EDCA_AC0_CFG + (sizeof(u32) * queue_idx);

	rt2800_register_read(rt2x00dev, offset, &reg);
	rt2x00_set_field32(&reg, EDCA_AC0_CFG_TX_OP, queue->txop);
	rt2x00_set_field32(&reg, EDCA_AC0_CFG_AIFSN, queue->aifs);
	rt2x00_set_field32(&reg, EDCA_AC0_CFG_CWMIN, queue->cw_min);
	rt2x00_set_field32(&reg, EDCA_AC0_CFG_CWMAX, queue->cw_max);
	rt2800_register_write(rt2x00dev, offset, reg);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_conf_tx);

u64 rt2800_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	u64 tsf;
	u32 reg;

	rt2800_register_read(rt2x00dev, TSF_TIMER_DW1, &reg);
	tsf = (u64) rt2x00_get_field32(reg, TSF_TIMER_DW1_HIGH_WORD) << 32;
	rt2800_register_read(rt2x00dev, TSF_TIMER_DW0, &reg);
	tsf |= rt2x00_get_field32(reg, TSF_TIMER_DW0_LOW_WORD);

	return tsf;
}
EXPORT_SYMBOL_GPL(rt2800_get_tsf);

int rt2800_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			enum ieee80211_ampdu_mlme_action action,
			struct ieee80211_sta *sta, u16 tid, u16 *ssn,
			u8 buf_size)
{
	struct rt2x00_sta *sta_priv = (struct rt2x00_sta *)sta->drv_priv;
	int ret = 0;

	/*
	 * Don't allow aggregation for stations the hardware isn't aware
	 * of because tx status reports for frames to an unknown station
	 * always contain wcid=255 and thus we can't distinguish between
	 * multiple stations which leads to unwanted situations when the
	 * hw reorders frames due to aggregation.
	 */
	if (sta_priv->wcid < 0)
		return 1;

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
	case IEEE80211_AMPDU_RX_STOP:
		/*
		 * The hw itself takes care of setting up BlockAck mechanisms.
		 * So, we only have to allow mac80211 to nagotiate a BlockAck
		 * agreement. Once that is done, the hw will BlockAck incoming
		 * AMPDUs without further setup.
		 */
		break;
	case IEEE80211_AMPDU_TX_START:
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP:
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		break;
	default:
		WARNING((struct rt2x00_dev *)hw->priv, "Unknown AMPDU action\n");
	}

	return ret;
}
EXPORT_SYMBOL_GPL(rt2800_ampdu_action);

int rt2800_get_survey(struct ieee80211_hw *hw, int idx,
		      struct survey_info *survey)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	u32 idle, busy, busy_ext;

	if (idx != 0)
		return -ENOENT;

	survey->channel = conf->channel;

	rt2800_register_read(rt2x00dev, CH_IDLE_STA, &idle);
	rt2800_register_read(rt2x00dev, CH_BUSY_STA, &busy);
	rt2800_register_read(rt2x00dev, CH_BUSY_STA_SEC, &busy_ext);

	if (idle || busy) {
		survey->filled = SURVEY_INFO_CHANNEL_TIME |
				 SURVEY_INFO_CHANNEL_TIME_BUSY |
				 SURVEY_INFO_CHANNEL_TIME_EXT_BUSY;

		survey->channel_time = (idle + busy) / 1000;
		survey->channel_time_busy = busy / 1000;
		survey->channel_time_ext_busy = busy_ext / 1000;
	}

	if (!(hw->conf.flags & IEEE80211_CONF_OFFCHANNEL))
		survey->filled |= SURVEY_INFO_IN_USE;

	return 0;

}
EXPORT_SYMBOL_GPL(rt2800_get_survey);

MODULE_AUTHOR(DRV_PROJECT ", Bartlomiej Zolnierkiewicz");
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("Ralink RT2800 library");
MODULE_LICENSE("GPL");

/*
	Copyright (C) 2009 Bartlomiej Zolnierkiewicz <bzolnier@gmail.com>
	Copyright (C) 2009 Gertjan van Wingerde <gwingerde@gmail.com>

	Based on the original rt2800pci.c and rt2800usb.c.
	  Copyright (C) 2009 Ivo van Doorn <IvDoorn@gmail.com>
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

#include <linux/kernel.h>
#include <linux/module.h>

#include "rt2x00.h"
#if defined(CONFIG_RT2800USB) || defined(CONFIG_RT2800USB_MODULE)
#include "rt2x00usb.h"
#endif
#include "rt2800lib.h"
#include "rt2800.h"
#include "rt2800usb.h"

MODULE_AUTHOR("Bartlomiej Zolnierkiewicz");
MODULE_DESCRIPTION("rt2800 library");
MODULE_LICENSE("GPL");

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
		if (rt2x00_intf_is_pci(rt2x00dev))
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
		if (rt2x00_intf_is_pci(rt2x00dev))
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
	 * RT2880 and RT3052 don't support MCU requests.
	 */
	if (rt2x00_rt(&rt2x00dev->chip, RT2880) ||
	    rt2x00_rt(&rt2x00dev->chip, RT3052))
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

static int rt2800_blink_set(struct led_classdev *led_cdev,
			    unsigned long *delay_on, unsigned long *delay_off)
{
	struct rt2x00_led *led =
	    container_of(led_cdev, struct rt2x00_led, led_dev);
	u32 reg;

	rt2800_register_read(led->rt2x00dev, LED_CFG, &reg);
	rt2x00_set_field32(&reg, LED_CFG_ON_PERIOD, *delay_on);
	rt2x00_set_field32(&reg, LED_CFG_OFF_PERIOD, *delay_off);
	rt2x00_set_field32(&reg, LED_CFG_SLOW_BLINK_PERIOD, 3);
	rt2x00_set_field32(&reg, LED_CFG_R_LED_MODE, 3);
	rt2x00_set_field32(&reg, LED_CFG_G_LED_MODE, 3);
	rt2x00_set_field32(&reg, LED_CFG_Y_LED_MODE, 3);
	rt2x00_set_field32(&reg, LED_CFG_LED_POLAR, 1);
	rt2800_register_write(led->rt2x00dev, LED_CFG, reg);

	return 0;
}

void rt2800_init_led(struct rt2x00_dev *rt2x00dev,
		     struct rt2x00_led *led, enum led_type type)
{
	led->rt2x00dev = rt2x00dev;
	led->type = type;
	led->led_dev.brightness_set = rt2800_brightness_set;
	led->led_dev.blink_set = rt2800_blink_set;
	led->flags = LED_INITIALIZED;
}
EXPORT_SYMBOL_GPL(rt2800_init_led);
#endif /* CONFIG_RT2X00_LIB_LEDS */

/*
 * Configuration handlers.
 */
static void rt2800_config_wcid_attr(struct rt2x00_dev *rt2x00dev,
				    struct rt2x00lib_crypto *crypto,
				    struct ieee80211_key_conf *key)
{
	struct mac_wcid_entry wcid_entry;
	struct mac_iveiv_entry iveiv_entry;
	u32 offset;
	u32 reg;

	offset = MAC_WCID_ATTR_ENTRY(key->hw_key_idx);

	rt2800_register_read(rt2x00dev, offset, &reg);
	rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_KEYTAB,
			   !!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE));
	rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_CIPHER,
			   (crypto->cmd == SET_KEY) * crypto->cipher);
	rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_BSS_IDX,
			   (crypto->cmd == SET_KEY) * crypto->bssidx);
	rt2x00_set_field32(&reg, MAC_WCID_ATTRIBUTE_RX_WIUDF, crypto->cipher);
	rt2800_register_write(rt2x00dev, offset, reg);

	offset = MAC_IVEIV_ENTRY(key->hw_key_idx);

	memset(&iveiv_entry, 0, sizeof(iveiv_entry));
	if ((crypto->cipher == CIPHER_TKIP) ||
	    (crypto->cipher == CIPHER_TKIP_NO_MIC) ||
	    (crypto->cipher == CIPHER_AES))
		iveiv_entry.iv[3] |= 0x20;
	iveiv_entry.iv[3] |= key->keyidx << 6;
	rt2800_register_multiwrite(rt2x00dev, offset,
				      &iveiv_entry, sizeof(iveiv_entry));

	offset = MAC_WCID_ENTRY(key->hw_key_idx);

	memset(&wcid_entry, 0, sizeof(wcid_entry));
	if (crypto->cmd == SET_KEY)
		memcpy(&wcid_entry, crypto->address, ETH_ALEN);
	rt2800_register_multiwrite(rt2x00dev, offset,
				      &wcid_entry, sizeof(wcid_entry));
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
	rt2800_config_wcid_attr(rt2x00dev, crypto, key);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_config_shared_key);

int rt2800_config_pairwise_key(struct rt2x00_dev *rt2x00dev,
			       struct rt2x00lib_crypto *crypto,
			       struct ieee80211_key_conf *key)
{
	struct hw_key_entry key_entry;
	u32 offset;

	if (crypto->cmd == SET_KEY) {
		/*
		 * 1 pairwise key is possible per AID, this means that the AID
		 * equals our hw_key_idx. Make sure the WCID starts _after_ the
		 * last possible shared key entry.
		 */
		if (crypto->aid > (256 - 32))
			return -ENOSPC;

		key->hw_key_idx = 32 + crypto->aid;

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
	rt2800_config_wcid_attr(rt2x00dev, crypto, key);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_config_pairwise_key);

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
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_BA, 1);
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_BAR, 0);
	rt2x00_set_field32(&reg, RX_FILTER_CFG_DROP_CNTL,
			   !(filter_flags & FIF_CONTROL));
	rt2800_register_write(rt2x00dev, RX_FILTER_CFG, reg);
}
EXPORT_SYMBOL_GPL(rt2800_config_filter);

void rt2800_config_intf(struct rt2x00_dev *rt2x00dev, struct rt2x00_intf *intf,
			struct rt2x00intf_conf *conf, const unsigned int flags)
{
	unsigned int beacon_base;
	u32 reg;

	if (flags & CONFIG_UPDATE_TYPE) {
		/*
		 * Clear current synchronisation setup.
		 * For the Beacon base registers we only need to clear
		 * the first byte since that byte contains the VALID and OWNER
		 * bits which (when set to 0) will invalidate the entire beacon.
		 */
		beacon_base = HW_BEACON_OFFSET(intf->beacon->entry_idx);
		rt2800_register_write(rt2x00dev, beacon_base, 0);

		/*
		 * Enable synchronisation.
		 */
		rt2800_register_read(rt2x00dev, BCN_TIME_CFG, &reg);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TSF_TICKING, 1);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TSF_SYNC, conf->sync);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TBTT_ENABLE,
				   (conf->sync == TSF_SYNC_BEACON));
		rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);
	}

	if (flags & CONFIG_UPDATE_MAC) {
		reg = le32_to_cpu(conf->mac[1]);
		rt2x00_set_field32(&reg, MAC_ADDR_DW1_UNICAST_TO_ME_MASK, 0xff);
		conf->mac[1] = cpu_to_le32(reg);

		rt2800_register_multiwrite(rt2x00dev, MAC_ADDR_DW0,
					      conf->mac, sizeof(conf->mac));
	}

	if (flags & CONFIG_UPDATE_BSSID) {
		reg = le32_to_cpu(conf->bssid[1]);
		rt2x00_set_field32(&reg, MAC_BSSID_DW1_BSS_ID_MASK, 0);
		rt2x00_set_field32(&reg, MAC_BSSID_DW1_BSS_BCN_NUM, 0);
		conf->bssid[1] = cpu_to_le32(reg);

		rt2800_register_multiwrite(rt2x00dev, MAC_BSSID_DW0,
					      conf->bssid, sizeof(conf->bssid));
	}
}
EXPORT_SYMBOL_GPL(rt2800_config_intf);

void rt2800_config_erp(struct rt2x00_dev *rt2x00dev, struct rt2x00lib_erp *erp)
{
	u32 reg;

	rt2800_register_read(rt2x00dev, TX_TIMEOUT_CFG, &reg);
	rt2x00_set_field32(&reg, TX_TIMEOUT_CFG_RX_ACK_TIMEOUT, 0x20);
	rt2800_register_write(rt2x00dev, TX_TIMEOUT_CFG, reg);

	rt2800_register_read(rt2x00dev, AUTO_RSP_CFG, &reg);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_BAC_ACK_POLICY,
			   !!erp->short_preamble);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_AR_PREAMBLE,
			   !!erp->short_preamble);
	rt2800_register_write(rt2x00dev, AUTO_RSP_CFG, reg);

	rt2800_register_read(rt2x00dev, OFDM_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_PROTECT_CTRL,
			   erp->cts_protection ? 2 : 0);
	rt2800_register_write(rt2x00dev, OFDM_PROT_CFG, reg);

	rt2800_register_write(rt2x00dev, LEGACY_BASIC_RATE,
				 erp->basic_rates);
	rt2800_register_write(rt2x00dev, HT_BASIC_RATE, 0x00008003);

	rt2800_register_read(rt2x00dev, BKOFF_SLOT_CFG, &reg);
	rt2x00_set_field32(&reg, BKOFF_SLOT_CFG_SLOT_TIME, erp->slot_time);
	rt2x00_set_field32(&reg, BKOFF_SLOT_CFG_CC_DELAY_TIME, 2);
	rt2800_register_write(rt2x00dev, BKOFF_SLOT_CFG, reg);

	rt2800_register_read(rt2x00dev, XIFS_TIME_CFG, &reg);
	rt2x00_set_field32(&reg, XIFS_TIME_CFG_CCKM_SIFS_TIME, erp->sifs);
	rt2x00_set_field32(&reg, XIFS_TIME_CFG_OFDM_SIFS_TIME, erp->sifs);
	rt2x00_set_field32(&reg, XIFS_TIME_CFG_OFDM_XIFS_TIME, 4);
	rt2x00_set_field32(&reg, XIFS_TIME_CFG_EIFS, erp->eifs);
	rt2x00_set_field32(&reg, XIFS_TIME_CFG_BB_RXEND_ENABLE, 1);
	rt2800_register_write(rt2x00dev, XIFS_TIME_CFG, reg);

	rt2800_register_read(rt2x00dev, BCN_TIME_CFG, &reg);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_INTERVAL,
			   erp->beacon_int * 16);
	rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);
}
EXPORT_SYMBOL_GPL(rt2800_config_erp);

void rt2800_config_ant(struct rt2x00_dev *rt2x00dev, struct antenna_setup *ant)
{
	u8 r1;
	u8 r3;

	rt2800_bbp_read(rt2x00dev, 1, &r1);
	rt2800_bbp_read(rt2x00dev, 3, &r3);

	/*
	 * Configure the TX antenna.
	 */
	switch ((int)ant->tx) {
	case 1:
		rt2x00_set_field8(&r1, BBP1_TX_ANTENNA, 0);
		if (rt2x00_intf_is_pci(rt2x00dev))
			rt2x00_set_field8(&r3, BBP3_RX_ANTENNA, 0);
		break;
	case 2:
		rt2x00_set_field8(&r1, BBP1_TX_ANTENNA, 2);
		break;
	case 3:
		/* Do nothing */
		break;
	}

	/*
	 * Configure the RX antenna.
	 */
	switch ((int)ant->rx) {
	case 1:
		rt2x00_set_field8(&r3, BBP3_RX_ANTENNA, 0);
		break;
	case 2:
		rt2x00_set_field8(&r3, BBP3_RX_ANTENNA, 1);
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

static void rt2800_config_channel_rt2x(struct rt2x00_dev *rt2x00dev,
				       struct ieee80211_conf *conf,
				       struct rf_channel *rf,
				       struct channel_info *info)
{
	rt2x00_set_field32(&rf->rf4, RF4_FREQ_OFFSET, rt2x00dev->freq_offset);

	if (rt2x00dev->default_ant.tx == 1)
		rt2x00_set_field32(&rf->rf2, RF2_ANTENNA_TX1, 1);

	if (rt2x00dev->default_ant.rx == 1) {
		rt2x00_set_field32(&rf->rf2, RF2_ANTENNA_RX1, 1);
		rt2x00_set_field32(&rf->rf2, RF2_ANTENNA_RX2, 1);
	} else if (rt2x00dev->default_ant.rx == 2)
		rt2x00_set_field32(&rf->rf2, RF2_ANTENNA_RX2, 1);

	if (rf->channel > 14) {
		/*
		 * When TX power is below 0, we should increase it by 7 to
		 * make it a positive value (Minumum value is -7).
		 * However this means that values between 0 and 7 have
		 * double meaning, and we should set a 7DBm boost flag.
		 */
		rt2x00_set_field32(&rf->rf3, RF3_TXPOWER_A_7DBM_BOOST,
				   (info->tx_power1 >= 0));

		if (info->tx_power1 < 0)
			info->tx_power1 += 7;

		rt2x00_set_field32(&rf->rf3, RF3_TXPOWER_A,
				   TXPOWER_A_TO_DEV(info->tx_power1));

		rt2x00_set_field32(&rf->rf4, RF4_TXPOWER_A_7DBM_BOOST,
				   (info->tx_power2 >= 0));

		if (info->tx_power2 < 0)
			info->tx_power2 += 7;

		rt2x00_set_field32(&rf->rf4, RF4_TXPOWER_A,
				   TXPOWER_A_TO_DEV(info->tx_power2));
	} else {
		rt2x00_set_field32(&rf->rf3, RF3_TXPOWER_G,
				   TXPOWER_G_TO_DEV(info->tx_power1));
		rt2x00_set_field32(&rf->rf4, RF4_TXPOWER_G,
				   TXPOWER_G_TO_DEV(info->tx_power2));
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

static void rt2800_config_channel_rt3x(struct rt2x00_dev *rt2x00dev,
				       struct ieee80211_conf *conf,
				       struct rf_channel *rf,
				       struct channel_info *info)
{
	u8 rfcsr;

	rt2800_rfcsr_write(rt2x00dev, 2, rf->rf1);
	rt2800_rfcsr_write(rt2x00dev, 3, rf->rf3);

	rt2800_rfcsr_read(rt2x00dev, 6, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR6_R, rf->rf2);
	rt2800_rfcsr_write(rt2x00dev, 6, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 12, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR12_TX_POWER,
			  TXPOWER_G_TO_DEV(info->tx_power1));
	rt2800_rfcsr_write(rt2x00dev, 12, rfcsr);

	rt2800_rfcsr_read(rt2x00dev, 23, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR23_FREQ_OFFSET, rt2x00dev->freq_offset);
	rt2800_rfcsr_write(rt2x00dev, 23, rfcsr);

	rt2800_rfcsr_write(rt2x00dev, 24,
			      rt2x00dev->calibration[conf_is_ht40(conf)]);

	rt2800_rfcsr_read(rt2x00dev, 23, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR7_RF_TUNING, 1);
	rt2800_rfcsr_write(rt2x00dev, 23, rfcsr);
}

static void rt2800_config_channel(struct rt2x00_dev *rt2x00dev,
				  struct ieee80211_conf *conf,
				  struct rf_channel *rf,
				  struct channel_info *info)
{
	u32 reg;
	unsigned int tx_pin;
	u8 bbp;

	if ((rt2x00_rt(&rt2x00dev->chip, RT3070) ||
	     rt2x00_rt(&rt2x00dev->chip, RT3090)) &&
	    (rt2x00_rf(&rt2x00dev->chip, RF2020) ||
	     rt2x00_rf(&rt2x00dev->chip, RF3020) ||
	     rt2x00_rf(&rt2x00dev->chip, RF3021) ||
	     rt2x00_rf(&rt2x00dev->chip, RF3022)))
		rt2800_config_channel_rt3x(rt2x00dev, conf, rf, info);
	else
		rt2800_config_channel_rt2x(rt2x00dev, conf, rf, info);

	/*
	 * Change BBP settings
	 */
	rt2800_bbp_write(rt2x00dev, 62, 0x37 - rt2x00dev->lna_gain);
	rt2800_bbp_write(rt2x00dev, 63, 0x37 - rt2x00dev->lna_gain);
	rt2800_bbp_write(rt2x00dev, 64, 0x37 - rt2x00dev->lna_gain);
	rt2800_bbp_write(rt2x00dev, 86, 0);

	if (rf->channel <= 14) {
		if (test_bit(CONFIG_EXTERNAL_LNA_BG, &rt2x00dev->flags)) {
			rt2800_bbp_write(rt2x00dev, 82, 0x62);
			rt2800_bbp_write(rt2x00dev, 75, 0x46);
		} else {
			rt2800_bbp_write(rt2x00dev, 82, 0x84);
			rt2800_bbp_write(rt2x00dev, 75, 0x50);
		}
	} else {
		rt2800_bbp_write(rt2x00dev, 82, 0xf2);

		if (test_bit(CONFIG_EXTERNAL_LNA_A, &rt2x00dev->flags))
			rt2800_bbp_write(rt2x00dev, 75, 0x46);
		else
			rt2800_bbp_write(rt2x00dev, 75, 0x50);
	}

	rt2800_register_read(rt2x00dev, TX_BAND_CFG, &reg);
	rt2x00_set_field32(&reg, TX_BAND_CFG_HT40_PLUS, conf_is_ht40_plus(conf));
	rt2x00_set_field32(&reg, TX_BAND_CFG_A, rf->channel > 14);
	rt2x00_set_field32(&reg, TX_BAND_CFG_BG, rf->channel <= 14);
	rt2800_register_write(rt2x00dev, TX_BAND_CFG, reg);

	tx_pin = 0;

	/* Turn on unused PA or LNA when not using 1T or 1R */
	if (rt2x00dev->default_ant.tx != 1) {
		rt2x00_set_field32(&tx_pin, TX_PIN_CFG_PA_PE_A1_EN, 1);
		rt2x00_set_field32(&tx_pin, TX_PIN_CFG_PA_PE_G1_EN, 1);
	}

	/* Turn on unused PA or LNA when not using 1T or 1R */
	if (rt2x00dev->default_ant.rx != 1) {
		rt2x00_set_field32(&tx_pin, TX_PIN_CFG_LNA_PE_A1_EN, 1);
		rt2x00_set_field32(&tx_pin, TX_PIN_CFG_LNA_PE_G1_EN, 1);
	}

	rt2x00_set_field32(&tx_pin, TX_PIN_CFG_LNA_PE_A0_EN, 1);
	rt2x00_set_field32(&tx_pin, TX_PIN_CFG_LNA_PE_G0_EN, 1);
	rt2x00_set_field32(&tx_pin, TX_PIN_CFG_RFTR_EN, 1);
	rt2x00_set_field32(&tx_pin, TX_PIN_CFG_TRSW_EN, 1);
	rt2x00_set_field32(&tx_pin, TX_PIN_CFG_PA_PE_G0_EN, rf->channel <= 14);
	rt2x00_set_field32(&tx_pin, TX_PIN_CFG_PA_PE_A0_EN, rf->channel > 14);

	rt2800_register_write(rt2x00dev, TX_PIN_CFG, tx_pin);

	rt2800_bbp_read(rt2x00dev, 4, &bbp);
	rt2x00_set_field8(&bbp, BBP4_BANDWIDTH, 2 * conf_is_ht40(conf));
	rt2800_bbp_write(rt2x00dev, 4, bbp);

	rt2800_bbp_read(rt2x00dev, 3, &bbp);
	rt2x00_set_field8(&bbp, BBP3_HT40_PLUS, conf_is_ht40_plus(conf));
	rt2800_bbp_write(rt2x00dev, 3, bbp);

	if (rt2x00_rev(&rt2x00dev->chip) == RT2860C_VERSION) {
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
}

static void rt2800_config_txpower(struct rt2x00_dev *rt2x00dev,
				  const int txpower)
{
	u32 reg;
	u32 value = TXPOWER_G_TO_DEV(txpower);
	u8 r1;

	rt2800_bbp_read(rt2x00dev, 1, &r1);
	rt2x00_set_field8(&reg, BBP1_TX_POWER, 0);
	rt2800_bbp_write(rt2x00dev, 1, r1);

	rt2800_register_read(rt2x00dev, TX_PWR_CFG_0, &reg);
	rt2x00_set_field32(&reg, TX_PWR_CFG_0_1MBS, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_0_2MBS, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_0_55MBS, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_0_11MBS, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_0_6MBS, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_0_9MBS, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_0_12MBS, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_0_18MBS, value);
	rt2800_register_write(rt2x00dev, TX_PWR_CFG_0, reg);

	rt2800_register_read(rt2x00dev, TX_PWR_CFG_1, &reg);
	rt2x00_set_field32(&reg, TX_PWR_CFG_1_24MBS, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_1_36MBS, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_1_48MBS, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_1_54MBS, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_1_MCS0, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_1_MCS1, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_1_MCS2, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_1_MCS3, value);
	rt2800_register_write(rt2x00dev, TX_PWR_CFG_1, reg);

	rt2800_register_read(rt2x00dev, TX_PWR_CFG_2, &reg);
	rt2x00_set_field32(&reg, TX_PWR_CFG_2_MCS4, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_2_MCS5, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_2_MCS6, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_2_MCS7, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_2_MCS8, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_2_MCS9, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_2_MCS10, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_2_MCS11, value);
	rt2800_register_write(rt2x00dev, TX_PWR_CFG_2, reg);

	rt2800_register_read(rt2x00dev, TX_PWR_CFG_3, &reg);
	rt2x00_set_field32(&reg, TX_PWR_CFG_3_MCS12, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_3_MCS13, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_3_MCS14, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_3_MCS15, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_3_UKNOWN1, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_3_UKNOWN2, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_3_UKNOWN3, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_3_UKNOWN4, value);
	rt2800_register_write(rt2x00dev, TX_PWR_CFG_3, reg);

	rt2800_register_read(rt2x00dev, TX_PWR_CFG_4, &reg);
	rt2x00_set_field32(&reg, TX_PWR_CFG_4_UKNOWN5, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_4_UKNOWN6, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_4_UKNOWN7, value);
	rt2x00_set_field32(&reg, TX_PWR_CFG_4_UKNOWN8, value);
	rt2800_register_write(rt2x00dev, TX_PWR_CFG_4, reg);
}

static void rt2800_config_retry_limit(struct rt2x00_dev *rt2x00dev,
				      struct rt2x00lib_conf *libconf)
{
	u32 reg;

	rt2800_register_read(rt2x00dev, TX_RTY_CFG, &reg);
	rt2x00_set_field32(&reg, TX_RTY_CFG_SHORT_RTY_LIMIT,
			   libconf->conf->short_frame_max_tx_count);
	rt2x00_set_field32(&reg, TX_RTY_CFG_LONG_RTY_LIMIT,
			   libconf->conf->long_frame_max_tx_count);
	rt2x00_set_field32(&reg, TX_RTY_CFG_LONG_RTY_THRE, 2000);
	rt2x00_set_field32(&reg, TX_RTY_CFG_NON_AGG_RTY_MODE, 0);
	rt2x00_set_field32(&reg, TX_RTY_CFG_AGG_RTY_MODE, 0);
	rt2x00_set_field32(&reg, TX_RTY_CFG_TX_AUTO_FB_ENABLE, 1);
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
		rt2x00dev->ops->lib->set_device_state(rt2x00dev, state);

		rt2800_register_read(rt2x00dev, AUTOWAKEUP_CFG, &reg);
		rt2x00_set_field32(&reg, AUTOWAKEUP_CFG_AUTO_LEAD_TIME, 0);
		rt2x00_set_field32(&reg, AUTOWAKEUP_CFG_TBCN_BEFORE_WAKE, 0);
		rt2x00_set_field32(&reg, AUTOWAKEUP_CFG_AUTOWAKE, 0);
		rt2800_register_write(rt2x00dev, AUTOWAKEUP_CFG, reg);
	}
}

void rt2800_config(struct rt2x00_dev *rt2x00dev,
		   struct rt2x00lib_conf *libconf,
		   const unsigned int flags)
{
	/* Always recalculate LNA gain before changing configuration */
	rt2800_config_lna_gain(rt2x00dev, libconf);

	if (flags & IEEE80211_CONF_CHANGE_CHANNEL)
		rt2800_config_channel(rt2x00dev, libconf->conf,
				      &libconf->rf, &libconf->channel);
	if (flags & IEEE80211_CONF_CHANGE_POWER)
		rt2800_config_txpower(rt2x00dev, libconf->conf->power_level);
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
		if (rt2x00_intf_is_usb(rt2x00dev) &&
		    rt2x00_rev(&rt2x00dev->chip) == RT3070_VERSION)
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
	if (rt2x00_rev(&rt2x00dev->chip) == RT2860C_VERSION)
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
int rt2800_init_registers(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	unsigned int i;

	if (rt2x00_intf_is_usb(rt2x00dev)) {
		/*
		 * Wait until BBP and RF are ready.
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

		rt2800_register_read(rt2x00dev, PBF_SYS_CTRL, &reg);
		rt2800_register_write(rt2x00dev, PBF_SYS_CTRL,
				      reg & ~0x00002000);
	} else if (rt2x00_intf_is_pci(rt2x00dev))
		rt2800_register_write(rt2x00dev, PWR_PIN_CFG, 0x00000003);

	rt2800_register_read(rt2x00dev, MAC_SYS_CTRL, &reg);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_RESET_CSR, 1);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_RESET_BBP, 1);
	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, reg);

	if (rt2x00_intf_is_usb(rt2x00dev)) {
		rt2800_register_write(rt2x00dev, USB_DMA_CFG, 0x00000000);
#if defined(CONFIG_RT2800USB) || defined(CONFIG_RT2800USB_MODULE)
		rt2x00usb_vendor_request_sw(rt2x00dev, USB_DEVICE_MODE, 0,
					    USB_MODE_RESET, REGISTER_TIMEOUT);
#endif
	}

	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, 0x00000000);

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
	rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_INTERVAL, 0);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_TSF_TICKING, 0);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_TSF_SYNC, 0);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_TBTT_ENABLE, 0);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 0);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_TX_TIME_COMPENSATE, 0);
	rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);

	if (rt2x00_intf_is_usb(rt2x00dev) &&
	    rt2x00_rev(&rt2x00dev->chip) == RT3070_VERSION) {
		rt2800_register_write(rt2x00dev, TX_SW_CFG0, 0x00000400);
		rt2800_register_write(rt2x00dev, TX_SW_CFG1, 0x00000000);
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
	rt2x00_set_field32(&reg, TX_TIMEOUT_CFG_TX_OP_TIMEOUT, 10);
	rt2800_register_write(rt2x00dev, TX_TIMEOUT_CFG, reg);

	rt2800_register_read(rt2x00dev, MAX_LEN_CFG, &reg);
	rt2x00_set_field32(&reg, MAX_LEN_CFG_MAX_MPDU, AGGREGATION_SIZE);
	if (rt2x00_rev(&rt2x00dev->chip) >= RT2880E_VERSION &&
	    rt2x00_rev(&rt2x00dev->chip) < RT3070_VERSION)
		rt2x00_set_field32(&reg, MAX_LEN_CFG_MAX_PSDU, 2);
	else
		rt2x00_set_field32(&reg, MAX_LEN_CFG_MAX_PSDU, 1);
	rt2x00_set_field32(&reg, MAX_LEN_CFG_MIN_PSDU, 0);
	rt2x00_set_field32(&reg, MAX_LEN_CFG_MIN_MPDU, 0);
	rt2800_register_write(rt2x00dev, MAX_LEN_CFG, reg);

	rt2800_register_write(rt2x00dev, PBF_MAX_PCNT, 0x1f3fbf9f);

	rt2800_register_read(rt2x00dev, AUTO_RSP_CFG, &reg);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_AUTORESPONDER, 1);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_CTS_40_MMODE, 0);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_CTS_40_MREF, 0);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_DUAL_CTS_EN, 0);
	rt2x00_set_field32(&reg, AUTO_RSP_CFG_ACK_CTS_PSM_BIT, 0);
	rt2800_register_write(rt2x00dev, AUTO_RSP_CFG, reg);

	rt2800_register_read(rt2x00dev, CCK_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_PROTECT_RATE, 8);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_PROTECT_CTRL, 0);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_PROTECT_NAV, 1);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_TX_OP_ALLOW_CCK, 1);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_TX_OP_ALLOW_OFDM, 1);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_TX_OP_ALLOW_MM20, 1);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_TX_OP_ALLOW_MM40, 1);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_TX_OP_ALLOW_GF20, 1);
	rt2x00_set_field32(&reg, CCK_PROT_CFG_TX_OP_ALLOW_GF40, 1);
	rt2800_register_write(rt2x00dev, CCK_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, OFDM_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_PROTECT_RATE, 8);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_PROTECT_CTRL, 0);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_PROTECT_NAV, 1);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_TX_OP_ALLOW_CCK, 1);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_TX_OP_ALLOW_OFDM, 1);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_TX_OP_ALLOW_MM20, 1);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_TX_OP_ALLOW_MM40, 1);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_TX_OP_ALLOW_GF20, 1);
	rt2x00_set_field32(&reg, OFDM_PROT_CFG_TX_OP_ALLOW_GF40, 1);
	rt2800_register_write(rt2x00dev, OFDM_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, MM20_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_PROTECT_RATE, 0x4004);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_PROTECT_CTRL, 0);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_PROTECT_NAV, 1);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_TX_OP_ALLOW_CCK, 1);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_TX_OP_ALLOW_OFDM, 1);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_TX_OP_ALLOW_MM20, 1);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_TX_OP_ALLOW_MM40, 0);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_TX_OP_ALLOW_GF20, 1);
	rt2x00_set_field32(&reg, MM20_PROT_CFG_TX_OP_ALLOW_GF40, 0);
	rt2800_register_write(rt2x00dev, MM20_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, MM40_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_PROTECT_RATE, 0x4084);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_PROTECT_CTRL, 0);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_PROTECT_NAV, 1);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_TX_OP_ALLOW_CCK, 1);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_TX_OP_ALLOW_OFDM, 1);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_TX_OP_ALLOW_MM20, 1);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_TX_OP_ALLOW_MM40, 1);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_TX_OP_ALLOW_GF20, 1);
	rt2x00_set_field32(&reg, MM40_PROT_CFG_TX_OP_ALLOW_GF40, 1);
	rt2800_register_write(rt2x00dev, MM40_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, GF20_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_PROTECT_RATE, 0x4004);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_PROTECT_CTRL, 0);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_PROTECT_NAV, 1);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_TX_OP_ALLOW_CCK, 1);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_TX_OP_ALLOW_OFDM, 1);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_TX_OP_ALLOW_MM20, 1);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_TX_OP_ALLOW_MM40, 0);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_TX_OP_ALLOW_GF20, 1);
	rt2x00_set_field32(&reg, GF20_PROT_CFG_TX_OP_ALLOW_GF40, 0);
	rt2800_register_write(rt2x00dev, GF20_PROT_CFG, reg);

	rt2800_register_read(rt2x00dev, GF40_PROT_CFG, &reg);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_PROTECT_RATE, 0x4084);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_PROTECT_CTRL, 0);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_PROTECT_NAV, 1);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_TX_OP_ALLOW_CCK, 1);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_TX_OP_ALLOW_OFDM, 1);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_TX_OP_ALLOW_MM20, 1);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_TX_OP_ALLOW_MM40, 1);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_TX_OP_ALLOW_GF20, 1);
	rt2x00_set_field32(&reg, GF40_PROT_CFG_TX_OP_ALLOW_GF40, 1);
	rt2800_register_write(rt2x00dev, GF40_PROT_CFG, reg);

	if (rt2x00_intf_is_usb(rt2x00dev)) {
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

	rt2800_register_write(rt2x00dev, TXOP_CTRL_CFG, 0x0000583f);
	rt2800_register_write(rt2x00dev, TXOP_HLDR_ET, 0x00000002);

	rt2800_register_read(rt2x00dev, TX_RTS_CFG, &reg);
	rt2x00_set_field32(&reg, TX_RTS_CFG_AUTO_RTS_RETRY_LIMIT, 32);
	rt2x00_set_field32(&reg, TX_RTS_CFG_RTS_THRES,
			   IEEE80211_MAX_RTS_THRESHOLD);
	rt2x00_set_field32(&reg, TX_RTS_CFG_RTS_FBK_EN, 0);
	rt2800_register_write(rt2x00dev, TX_RTS_CFG, reg);

	rt2800_register_write(rt2x00dev, EXP_ACK_TIME, 0x002400ca);
	rt2800_register_write(rt2x00dev, PWR_PIN_CFG, 0x00000003);

	/*
	 * ASIC will keep garbage value after boot, clear encryption keys.
	 */
	for (i = 0; i < 4; i++)
		rt2800_register_write(rt2x00dev,
					 SHARED_KEY_MODE_ENTRY(i), 0);

	for (i = 0; i < 256; i++) {
		u32 wcid[2] = { 0xffffffff, 0x00ffffff };
		rt2800_register_multiwrite(rt2x00dev, MAC_WCID_ENTRY(i),
					      wcid, sizeof(wcid));

		rt2800_register_write(rt2x00dev, MAC_WCID_ATTR_ENTRY(i), 1);
		rt2800_register_write(rt2x00dev, MAC_IVEIV_ENTRY(i), 0);
	}

	/*
	 * Clear all beacons
	 * For the Beacon base registers we only need to clear
	 * the first byte since that byte contains the VALID and OWNER
	 * bits which (when set to 0) will invalidate the entire beacon.
	 */
	rt2800_register_write(rt2x00dev, HW_BEACON_BASE0, 0);
	rt2800_register_write(rt2x00dev, HW_BEACON_BASE1, 0);
	rt2800_register_write(rt2x00dev, HW_BEACON_BASE2, 0);
	rt2800_register_write(rt2x00dev, HW_BEACON_BASE3, 0);
	rt2800_register_write(rt2x00dev, HW_BEACON_BASE4, 0);
	rt2800_register_write(rt2x00dev, HW_BEACON_BASE5, 0);
	rt2800_register_write(rt2x00dev, HW_BEACON_BASE6, 0);
	rt2800_register_write(rt2x00dev, HW_BEACON_BASE7, 0);

	if (rt2x00_intf_is_usb(rt2x00dev)) {
		rt2800_register_read(rt2x00dev, USB_CYC_CFG, &reg);
		rt2x00_set_field32(&reg, USB_CYC_CFG_CLOCK_CYCLE, 30);
		rt2800_register_write(rt2x00dev, USB_CYC_CFG, reg);
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

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_init_registers);

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

int rt2800_init_bbp(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u16 eeprom;
	u8 reg_id;
	u8 value;

	if (unlikely(rt2800_wait_bbp_rf_ready(rt2x00dev) ||
		     rt2800_wait_bbp_ready(rt2x00dev)))
		return -EACCES;

	rt2800_bbp_write(rt2x00dev, 65, 0x2c);
	rt2800_bbp_write(rt2x00dev, 66, 0x38);
	rt2800_bbp_write(rt2x00dev, 69, 0x12);
	rt2800_bbp_write(rt2x00dev, 70, 0x0a);
	rt2800_bbp_write(rt2x00dev, 73, 0x10);
	rt2800_bbp_write(rt2x00dev, 81, 0x37);
	rt2800_bbp_write(rt2x00dev, 82, 0x62);
	rt2800_bbp_write(rt2x00dev, 83, 0x6a);
	rt2800_bbp_write(rt2x00dev, 84, 0x99);
	rt2800_bbp_write(rt2x00dev, 86, 0x00);
	rt2800_bbp_write(rt2x00dev, 91, 0x04);
	rt2800_bbp_write(rt2x00dev, 92, 0x00);
	rt2800_bbp_write(rt2x00dev, 103, 0x00);
	rt2800_bbp_write(rt2x00dev, 105, 0x05);

	if (rt2x00_rev(&rt2x00dev->chip) == RT2860C_VERSION) {
		rt2800_bbp_write(rt2x00dev, 69, 0x16);
		rt2800_bbp_write(rt2x00dev, 73, 0x12);
	}

	if (rt2x00_rev(&rt2x00dev->chip) > RT2860D_VERSION)
		rt2800_bbp_write(rt2x00dev, 84, 0x19);

	if (rt2x00_intf_is_usb(rt2x00dev) &&
	    rt2x00_rev(&rt2x00dev->chip) == RT3070_VERSION) {
		rt2800_bbp_write(rt2x00dev, 70, 0x0a);
		rt2800_bbp_write(rt2x00dev, 84, 0x99);
		rt2800_bbp_write(rt2x00dev, 105, 0x05);
	}

	if (rt2x00_rt(&rt2x00dev->chip, RT3052)) {
		rt2800_bbp_write(rt2x00dev, 31, 0x08);
		rt2800_bbp_write(rt2x00dev, 78, 0x0e);
		rt2800_bbp_write(rt2x00dev, 80, 0x08);
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
EXPORT_SYMBOL_GPL(rt2800_init_bbp);

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

int rt2800_init_rfcsr(struct rt2x00_dev *rt2x00dev)
{
	u8 rfcsr;
	u8 bbp;

	if (rt2x00_intf_is_usb(rt2x00dev) &&
	    rt2x00_rev(&rt2x00dev->chip) != RT3070_VERSION)
		return 0;

	if (rt2x00_intf_is_pci(rt2x00dev)) {
		if (!rt2x00_rf(&rt2x00dev->chip, RF3020) &&
		    !rt2x00_rf(&rt2x00dev->chip, RF3021) &&
		    !rt2x00_rf(&rt2x00dev->chip, RF3022))
			return 0;
	}

	/*
	 * Init RF calibration.
	 */
	rt2800_rfcsr_read(rt2x00dev, 30, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR30_RF_CALIBRATION, 1);
	rt2800_rfcsr_write(rt2x00dev, 30, rfcsr);
	msleep(1);
	rt2x00_set_field8(&rfcsr, RFCSR30_RF_CALIBRATION, 0);
	rt2800_rfcsr_write(rt2x00dev, 30, rfcsr);

	if (rt2x00_intf_is_usb(rt2x00dev)) {
		rt2800_rfcsr_write(rt2x00dev, 4, 0x40);
		rt2800_rfcsr_write(rt2x00dev, 5, 0x03);
		rt2800_rfcsr_write(rt2x00dev, 6, 0x02);
		rt2800_rfcsr_write(rt2x00dev, 7, 0x70);
		rt2800_rfcsr_write(rt2x00dev, 9, 0x0f);
		rt2800_rfcsr_write(rt2x00dev, 10, 0x71);
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
		rt2800_rfcsr_write(rt2x00dev, 27, 0x03);
		rt2800_rfcsr_write(rt2x00dev, 29, 0x1f);
	} else if (rt2x00_intf_is_pci(rt2x00dev)) {
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
	}

	/*
	 * Set RX Filter calibration for 20MHz and 40MHz
	 */
	rt2x00dev->calibration[0] =
	    rt2800_init_rx_filter(rt2x00dev, false, 0x07, 0x16);
	rt2x00dev->calibration[1] =
	    rt2800_init_rx_filter(rt2x00dev, true, 0x27, 0x19);

	/*
	 * Set back to initial state
	 */
	rt2800_bbp_write(rt2x00dev, 24, 0);

	rt2800_rfcsr_read(rt2x00dev, 22, &rfcsr);
	rt2x00_set_field8(&rfcsr, RFCSR22_BASEBAND_LOOPBACK, 0);
	rt2800_rfcsr_write(rt2x00dev, 22, rfcsr);

	/*
	 * set BBP back to BW20
	 */
	rt2800_bbp_read(rt2x00dev, 4, &bbp);
	rt2x00_set_field8(&bbp, BBP4_BANDWIDTH, 0);
	rt2800_bbp_write(rt2x00dev, 4, bbp);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_init_rfcsr);

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
	rt2800_register_read_lock(rt2x00dev, EFUSE_DATA3,
					(u32 *)&rt2x00dev->eeprom[i]);
	rt2800_register_read_lock(rt2x00dev, EFUSE_DATA2,
					(u32 *)&rt2x00dev->eeprom[i + 2]);
	rt2800_register_read_lock(rt2x00dev, EFUSE_DATA1,
					(u32 *)&rt2x00dev->eeprom[i + 4]);
	rt2800_register_read_lock(rt2x00dev, EFUSE_DATA0,
					(u32 *)&rt2x00dev->eeprom[i + 6]);

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

	rt2x00_eeprom_read(rt2x00dev, EEPROM_ANTENNA, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_ANTENNA_RXPATH, 2);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_TXPATH, 1);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_RF_TYPE, RF2820);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_ANTENNA, word);
		EEPROM(rt2x00dev, "Antenna: 0x%04x\n", word);
	} else if (rt2x00_rev(&rt2x00dev->chip) < RT2883_VERSION) {
		/*
		 * There is a max of 2 RX streams for RT28x0 series
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
EXPORT_SYMBOL_GPL(rt2800_validate_eeprom);

int rt2800_init_eeprom(struct rt2x00_dev *rt2x00dev)
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

	rt2x00_set_chip_rf(rt2x00dev, value, reg);

	if (rt2x00_intf_is_usb(rt2x00dev)) {
		struct rt2x00_chip *chip = &rt2x00dev->chip;

		/*
		 * The check for rt2860 is not a typo, some rt2870 hardware
		 * identifies itself as rt2860 in the CSR register.
		 */
		if (rt2x00_check_rev(chip, 0xfff00000, 0x28600000) ||
		    rt2x00_check_rev(chip, 0xfff00000, 0x28700000) ||
		    rt2x00_check_rev(chip, 0xfff00000, 0x28800000)) {
			rt2x00_set_chip_rt(rt2x00dev, RT2870);
		} else if (rt2x00_check_rev(chip, 0xffff0000, 0x30700000)) {
			rt2x00_set_chip_rt(rt2x00dev, RT3070);
		} else {
			ERROR(rt2x00dev, "Invalid RT chipset detected.\n");
			return -ENODEV;
		}
	}
	rt2x00_print_chip(rt2x00dev);

	if (!rt2x00_rf(&rt2x00dev->chip, RF2820) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF2850) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF2720) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF2750) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF3020) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF2020) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF3021) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF3022)) {
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

	rt2x00_eeprom_read(rt2x00dev, EEPROM_FREQ, &rt2x00dev->led_mcu_reg);
#endif /* CONFIG_RT2X00_LIB_LEDS */

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800_init_eeprom);

/*
 * RF value list for rt28x0
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
static const struct rf_channel rf_vals_302x[] = {
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

int rt2800_probe_hw_mode(struct rt2x00_dev *rt2x00dev)
{
	struct rt2x00_chip *chip = &rt2x00dev->chip;
	struct hw_mode_spec *spec = &rt2x00dev->spec;
	struct channel_info *info;
	char *tx_power1;
	char *tx_power2;
	unsigned int i;
	u16 eeprom;

	/*
	 * Disable powersaving as default on PCI devices.
	 */
	if (rt2x00_intf_is_pci(rt2x00dev))
		rt2x00dev->hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	/*
	 * Initialize all hw fields.
	 */
	rt2x00dev->hw->flags =
	    IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING |
	    IEEE80211_HW_SIGNAL_DBM |
	    IEEE80211_HW_SUPPORTS_PS |
	    IEEE80211_HW_PS_NULLFUNC_STACK;

	SET_IEEE80211_DEV(rt2x00dev->hw, rt2x00dev->dev);
	SET_IEEE80211_PERM_ADDR(rt2x00dev->hw,
				rt2x00_eeprom_addr(rt2x00dev,
						   EEPROM_MAC_ADDR_0));

	rt2x00_eeprom_read(rt2x00dev, EEPROM_ANTENNA, &eeprom);

	/*
	 * Initialize hw_mode information.
	 */
	spec->supported_bands = SUPPORT_BAND_2GHZ;
	spec->supported_rates = SUPPORT_RATE_CCK | SUPPORT_RATE_OFDM;

	if (rt2x00_rf(chip, RF2820) ||
	    rt2x00_rf(chip, RF2720) ||
	    (rt2x00_intf_is_pci(rt2x00dev) && rt2x00_rf(chip, RF3052))) {
		spec->num_channels = 14;
		spec->channels = rf_vals;
	} else if (rt2x00_rf(chip, RF2850) || rt2x00_rf(chip, RF2750)) {
		spec->supported_bands |= SUPPORT_BAND_5GHZ;
		spec->num_channels = ARRAY_SIZE(rf_vals);
		spec->channels = rf_vals;
	} else if (rt2x00_rf(chip, RF3020) ||
		   rt2x00_rf(chip, RF2020) ||
		   rt2x00_rf(chip, RF3021) ||
		   rt2x00_rf(chip, RF3022)) {
		spec->num_channels = ARRAY_SIZE(rf_vals_302x);
		spec->channels = rf_vals_302x;
	}

	/*
	 * Initialize HT information.
	 */
	if (!rt2x00_rf(chip, RF2020))
		spec->ht.ht_supported = true;
	else
		spec->ht.ht_supported = false;

	spec->ht.cap =
	    IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
	    IEEE80211_HT_CAP_GRN_FLD |
	    IEEE80211_HT_CAP_SGI_20 |
	    IEEE80211_HT_CAP_SGI_40 |
	    IEEE80211_HT_CAP_TX_STBC |
	    IEEE80211_HT_CAP_RX_STBC;
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
EXPORT_SYMBOL_GPL(rt2800_probe_hw_mode);

/*
 * IEEE80211 stack callback functions.
 */
static void rt2800_get_tkip_seq(struct ieee80211_hw *hw, u8 hw_key_idx,
				u32 *iv32, u16 *iv16)
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

static int rt2800_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
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

static int rt2800_conf_tx(struct ieee80211_hw *hw, u16 queue_idx,
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
	retval = rt2x00mac_conf_tx(hw, queue_idx, params);
	if (retval)
		return retval;

	/*
	 * We only need to perform additional register initialization
	 * for WMM queues/
	 */
	if (queue_idx >= 4)
		return 0;

	queue = rt2x00queue_get_queue(rt2x00dev, queue_idx);

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

static u64 rt2800_get_tsf(struct ieee80211_hw *hw)
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

const struct ieee80211_ops rt2800_mac80211_ops = {
	.tx			= rt2x00mac_tx,
	.start			= rt2x00mac_start,
	.stop			= rt2x00mac_stop,
	.add_interface		= rt2x00mac_add_interface,
	.remove_interface	= rt2x00mac_remove_interface,
	.config			= rt2x00mac_config,
	.configure_filter	= rt2x00mac_configure_filter,
	.set_tim		= rt2x00mac_set_tim,
	.set_key		= rt2x00mac_set_key,
	.get_stats		= rt2x00mac_get_stats,
	.get_tkip_seq		= rt2800_get_tkip_seq,
	.set_rts_threshold	= rt2800_set_rts_threshold,
	.bss_info_changed	= rt2x00mac_bss_info_changed,
	.conf_tx		= rt2800_conf_tx,
	.get_tx_stats		= rt2x00mac_get_tx_stats,
	.get_tsf		= rt2800_get_tsf,
	.rfkill_poll		= rt2x00mac_rfkill_poll,
};
EXPORT_SYMBOL_GPL(rt2800_mac80211_ops);

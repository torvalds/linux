/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
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
	Module: rt73usb
	Abstract: rt73usb device specific routines.
	Supported chipsets: rt2571W & rt2671.
 */

#include <linux/crc-itu-t.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "rt2x00.h"
#include "rt2x00usb.h"
#include "rt73usb.h"

/*
 * Allow hardware encryption to be disabled.
 */
static int modparam_nohwcrypt = 0;
module_param_named(nohwcrypt, modparam_nohwcrypt, bool, S_IRUGO);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption.");

/*
 * Register access.
 * All access to the CSR registers will go through the methods
 * rt2x00usb_register_read and rt2x00usb_register_write.
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
	rt2x00usb_regbusy_read((__dev), PHY_CSR3, PHY_CSR3_BUSY, (__reg))
#define WAIT_FOR_RF(__dev, __reg) \
	rt2x00usb_regbusy_read((__dev), PHY_CSR4, PHY_CSR4_BUSY, (__reg))

static void rt73usb_bbp_write(struct rt2x00_dev *rt2x00dev,
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
		rt2x00_set_field32(&reg, PHY_CSR3_VALUE, value);
		rt2x00_set_field32(&reg, PHY_CSR3_REGNUM, word);
		rt2x00_set_field32(&reg, PHY_CSR3_BUSY, 1);
		rt2x00_set_field32(&reg, PHY_CSR3_READ_CONTROL, 0);

		rt2x00usb_register_write_lock(rt2x00dev, PHY_CSR3, reg);
	}

	mutex_unlock(&rt2x00dev->csr_mutex);
}

static void rt73usb_bbp_read(struct rt2x00_dev *rt2x00dev,
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
		rt2x00_set_field32(&reg, PHY_CSR3_REGNUM, word);
		rt2x00_set_field32(&reg, PHY_CSR3_BUSY, 1);
		rt2x00_set_field32(&reg, PHY_CSR3_READ_CONTROL, 1);

		rt2x00usb_register_write_lock(rt2x00dev, PHY_CSR3, reg);

		WAIT_FOR_BBP(rt2x00dev, &reg);
	}

	*value = rt2x00_get_field32(reg, PHY_CSR3_VALUE);

	mutex_unlock(&rt2x00dev->csr_mutex);
}

static void rt73usb_rf_write(struct rt2x00_dev *rt2x00dev,
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
		rt2x00_set_field32(&reg, PHY_CSR4_VALUE, value);
		/*
		 * RF5225 and RF2527 contain 21 bits per RF register value,
		 * all others contain 20 bits.
		 */
		rt2x00_set_field32(&reg, PHY_CSR4_NUMBER_OF_BITS,
				   20 + (rt2x00_rf(rt2x00dev, RF5225) ||
					 rt2x00_rf(rt2x00dev, RF2527)));
		rt2x00_set_field32(&reg, PHY_CSR4_IF_SELECT, 0);
		rt2x00_set_field32(&reg, PHY_CSR4_BUSY, 1);

		rt2x00usb_register_write_lock(rt2x00dev, PHY_CSR4, reg);
		rt2x00_rf_write(rt2x00dev, word, value);
	}

	mutex_unlock(&rt2x00dev->csr_mutex);
}

#ifdef CONFIG_RT2X00_LIB_DEBUGFS
static const struct rt2x00debug rt73usb_rt2x00debug = {
	.owner	= THIS_MODULE,
	.csr	= {
		.read		= rt2x00usb_register_read,
		.write		= rt2x00usb_register_write,
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
		.read		= rt73usb_bbp_read,
		.write		= rt73usb_bbp_write,
		.word_base	= BBP_BASE,
		.word_size	= sizeof(u8),
		.word_count	= BBP_SIZE / sizeof(u8),
	},
	.rf	= {
		.read		= rt2x00_rf_read,
		.write		= rt73usb_rf_write,
		.word_base	= RF_BASE,
		.word_size	= sizeof(u32),
		.word_count	= RF_SIZE / sizeof(u32),
	},
};
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */

static int rt73usb_rfkill_poll(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2x00usb_register_read(rt2x00dev, MAC_CSR13, &reg);
	return rt2x00_get_field32(reg, MAC_CSR13_BIT7);
}

#ifdef CONFIG_RT2X00_LIB_LEDS
static void rt73usb_brightness_set(struct led_classdev *led_cdev,
				   enum led_brightness brightness)
{
	struct rt2x00_led *led =
	   container_of(led_cdev, struct rt2x00_led, led_dev);
	unsigned int enabled = brightness != LED_OFF;
	unsigned int a_mode =
	    (enabled && led->rt2x00dev->curr_band == IEEE80211_BAND_5GHZ);
	unsigned int bg_mode =
	    (enabled && led->rt2x00dev->curr_band == IEEE80211_BAND_2GHZ);

	if (led->type == LED_TYPE_RADIO) {
		rt2x00_set_field16(&led->rt2x00dev->led_mcu_reg,
				   MCU_LEDCS_RADIO_STATUS, enabled);

		rt2x00usb_vendor_request_sw(led->rt2x00dev, USB_LED_CONTROL,
					    0, led->rt2x00dev->led_mcu_reg,
					    REGISTER_TIMEOUT);
	} else if (led->type == LED_TYPE_ASSOC) {
		rt2x00_set_field16(&led->rt2x00dev->led_mcu_reg,
				   MCU_LEDCS_LINK_BG_STATUS, bg_mode);
		rt2x00_set_field16(&led->rt2x00dev->led_mcu_reg,
				   MCU_LEDCS_LINK_A_STATUS, a_mode);

		rt2x00usb_vendor_request_sw(led->rt2x00dev, USB_LED_CONTROL,
					    0, led->rt2x00dev->led_mcu_reg,
					    REGISTER_TIMEOUT);
	} else if (led->type == LED_TYPE_QUALITY) {
		/*
		 * The brightness is divided into 6 levels (0 - 5),
		 * this means we need to convert the brightness
		 * argument into the matching level within that range.
		 */
		rt2x00usb_vendor_request_sw(led->rt2x00dev, USB_LED_CONTROL,
					    brightness / (LED_FULL / 6),
					    led->rt2x00dev->led_mcu_reg,
					    REGISTER_TIMEOUT);
	}
}

static int rt73usb_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on,
			     unsigned long *delay_off)
{
	struct rt2x00_led *led =
	    container_of(led_cdev, struct rt2x00_led, led_dev);
	u32 reg;

	rt2x00usb_register_read(led->rt2x00dev, MAC_CSR14, &reg);
	rt2x00_set_field32(&reg, MAC_CSR14_ON_PERIOD, *delay_on);
	rt2x00_set_field32(&reg, MAC_CSR14_OFF_PERIOD, *delay_off);
	rt2x00usb_register_write(led->rt2x00dev, MAC_CSR14, reg);

	return 0;
}

static void rt73usb_init_led(struct rt2x00_dev *rt2x00dev,
			     struct rt2x00_led *led,
			     enum led_type type)
{
	led->rt2x00dev = rt2x00dev;
	led->type = type;
	led->led_dev.brightness_set = rt73usb_brightness_set;
	led->led_dev.blink_set = rt73usb_blink_set;
	led->flags = LED_INITIALIZED;
}
#endif /* CONFIG_RT2X00_LIB_LEDS */

/*
 * Configuration handlers.
 */
static int rt73usb_config_shared_key(struct rt2x00_dev *rt2x00dev,
				     struct rt2x00lib_crypto *crypto,
				     struct ieee80211_key_conf *key)
{
	struct hw_key_entry key_entry;
	struct rt2x00_field32 field;
	int timeout;
	u32 mask;
	u32 reg;

	if (crypto->cmd == SET_KEY) {
		/*
		 * rt2x00lib can't determine the correct free
		 * key_idx for shared keys. We have 1 register
		 * with key valid bits. The goal is simple, read
		 * the register, if that is full we have no slots
		 * left.
		 * Note that each BSS is allowed to have up to 4
		 * shared keys, so put a mask over the allowed
		 * entries.
		 */
		mask = (0xf << crypto->bssidx);

		rt2x00usb_register_read(rt2x00dev, SEC_CSR0, &reg);
		reg &= mask;

		if (reg && reg == mask)
			return -ENOSPC;

		key->hw_key_idx += reg ? ffz(reg) : 0;

		/*
		 * Upload key to hardware
		 */
		memcpy(key_entry.key, crypto->key,
		       sizeof(key_entry.key));
		memcpy(key_entry.tx_mic, crypto->tx_mic,
		       sizeof(key_entry.tx_mic));
		memcpy(key_entry.rx_mic, crypto->rx_mic,
		       sizeof(key_entry.rx_mic));

		reg = SHARED_KEY_ENTRY(key->hw_key_idx);
		timeout = REGISTER_TIMEOUT32(sizeof(key_entry));
		rt2x00usb_vendor_request_large_buff(rt2x00dev, USB_MULTI_WRITE,
						    USB_VENDOR_REQUEST_OUT, reg,
						    &key_entry,
						    sizeof(key_entry),
						    timeout);

		/*
		 * The cipher types are stored over 2 registers.
		 * bssidx 0 and 1 keys are stored in SEC_CSR1 and
		 * bssidx 1 and 2 keys are stored in SEC_CSR5.
		 * Using the correct defines correctly will cause overhead,
		 * so just calculate the correct offset.
		 */
		if (key->hw_key_idx < 8) {
			field.bit_offset = (3 * key->hw_key_idx);
			field.bit_mask = 0x7 << field.bit_offset;

			rt2x00usb_register_read(rt2x00dev, SEC_CSR1, &reg);
			rt2x00_set_field32(&reg, field, crypto->cipher);
			rt2x00usb_register_write(rt2x00dev, SEC_CSR1, reg);
		} else {
			field.bit_offset = (3 * (key->hw_key_idx - 8));
			field.bit_mask = 0x7 << field.bit_offset;

			rt2x00usb_register_read(rt2x00dev, SEC_CSR5, &reg);
			rt2x00_set_field32(&reg, field, crypto->cipher);
			rt2x00usb_register_write(rt2x00dev, SEC_CSR5, reg);
		}

		/*
		 * The driver does not support the IV/EIV generation
		 * in hardware. However it doesn't support the IV/EIV
		 * inside the ieee80211 frame either, but requires it
		 * to be provided seperately for the descriptor.
		 * rt2x00lib will cut the IV/EIV data out of all frames
		 * given to us by mac80211, but we must tell mac80211
		 * to generate the IV/EIV data.
		 */
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
	}

	/*
	 * SEC_CSR0 contains only single-bit fields to indicate
	 * a particular key is valid. Because using the FIELD32()
	 * defines directly will cause a lot of overhead we use
	 * a calculation to determine the correct bit directly.
	 */
	mask = 1 << key->hw_key_idx;

	rt2x00usb_register_read(rt2x00dev, SEC_CSR0, &reg);
	if (crypto->cmd == SET_KEY)
		reg |= mask;
	else if (crypto->cmd == DISABLE_KEY)
		reg &= ~mask;
	rt2x00usb_register_write(rt2x00dev, SEC_CSR0, reg);

	return 0;
}

static int rt73usb_config_pairwise_key(struct rt2x00_dev *rt2x00dev,
				       struct rt2x00lib_crypto *crypto,
				       struct ieee80211_key_conf *key)
{
	struct hw_pairwise_ta_entry addr_entry;
	struct hw_key_entry key_entry;
	int timeout;
	u32 mask;
	u32 reg;

	if (crypto->cmd == SET_KEY) {
		/*
		 * rt2x00lib can't determine the correct free
		 * key_idx for pairwise keys. We have 2 registers
		 * with key valid bits. The goal is simple, read
		 * the first register, if that is full move to
		 * the next register.
		 * When both registers are full, we drop the key,
		 * otherwise we use the first invalid entry.
		 */
		rt2x00usb_register_read(rt2x00dev, SEC_CSR2, &reg);
		if (reg && reg == ~0) {
			key->hw_key_idx = 32;
			rt2x00usb_register_read(rt2x00dev, SEC_CSR3, &reg);
			if (reg && reg == ~0)
				return -ENOSPC;
		}

		key->hw_key_idx += reg ? ffz(reg) : 0;

		/*
		 * Upload key to hardware
		 */
		memcpy(key_entry.key, crypto->key,
		       sizeof(key_entry.key));
		memcpy(key_entry.tx_mic, crypto->tx_mic,
		       sizeof(key_entry.tx_mic));
		memcpy(key_entry.rx_mic, crypto->rx_mic,
		       sizeof(key_entry.rx_mic));

		reg = PAIRWISE_KEY_ENTRY(key->hw_key_idx);
		timeout = REGISTER_TIMEOUT32(sizeof(key_entry));
		rt2x00usb_vendor_request_large_buff(rt2x00dev, USB_MULTI_WRITE,
						    USB_VENDOR_REQUEST_OUT, reg,
						    &key_entry,
						    sizeof(key_entry),
						    timeout);

		/*
		 * Send the address and cipher type to the hardware register.
		 * This data fits within the CSR cache size, so we can use
		 * rt2x00usb_register_multiwrite() directly.
		 */
		memset(&addr_entry, 0, sizeof(addr_entry));
		memcpy(&addr_entry, crypto->address, ETH_ALEN);
		addr_entry.cipher = crypto->cipher;

		reg = PAIRWISE_TA_ENTRY(key->hw_key_idx);
		rt2x00usb_register_multiwrite(rt2x00dev, reg,
					    &addr_entry, sizeof(addr_entry));

		/*
		 * Enable pairwise lookup table for given BSS idx,
		 * without this received frames will not be decrypted
		 * by the hardware.
		 */
		rt2x00usb_register_read(rt2x00dev, SEC_CSR4, &reg);
		reg |= (1 << crypto->bssidx);
		rt2x00usb_register_write(rt2x00dev, SEC_CSR4, reg);

		/*
		 * The driver does not support the IV/EIV generation
		 * in hardware. However it doesn't support the IV/EIV
		 * inside the ieee80211 frame either, but requires it
		 * to be provided seperately for the descriptor.
		 * rt2x00lib will cut the IV/EIV data out of all frames
		 * given to us by mac80211, but we must tell mac80211
		 * to generate the IV/EIV data.
		 */
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
	}

	/*
	 * SEC_CSR2 and SEC_CSR3 contain only single-bit fields to indicate
	 * a particular key is valid. Because using the FIELD32()
	 * defines directly will cause a lot of overhead we use
	 * a calculation to determine the correct bit directly.
	 */
	if (key->hw_key_idx < 32) {
		mask = 1 << key->hw_key_idx;

		rt2x00usb_register_read(rt2x00dev, SEC_CSR2, &reg);
		if (crypto->cmd == SET_KEY)
			reg |= mask;
		else if (crypto->cmd == DISABLE_KEY)
			reg &= ~mask;
		rt2x00usb_register_write(rt2x00dev, SEC_CSR2, reg);
	} else {
		mask = 1 << (key->hw_key_idx - 32);

		rt2x00usb_register_read(rt2x00dev, SEC_CSR3, &reg);
		if (crypto->cmd == SET_KEY)
			reg |= mask;
		else if (crypto->cmd == DISABLE_KEY)
			reg &= ~mask;
		rt2x00usb_register_write(rt2x00dev, SEC_CSR3, reg);
	}

	return 0;
}

static void rt73usb_config_filter(struct rt2x00_dev *rt2x00dev,
				  const unsigned int filter_flags)
{
	u32 reg;

	/*
	 * Start configuration steps.
	 * Note that the version error will always be dropped
	 * and broadcast frames will always be accepted since
	 * there is no filter for it at this time.
	 */
	rt2x00usb_register_read(rt2x00dev, TXRX_CSR0, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_CRC,
			   !(filter_flags & FIF_FCSFAIL));
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_PHYSICAL,
			   !(filter_flags & FIF_PLCPFAIL));
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_CONTROL,
			   !(filter_flags & (FIF_CONTROL | FIF_PSPOLL)));
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_NOT_TO_ME,
			   !(filter_flags & FIF_PROMISC_IN_BSS));
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_TO_DS,
			   !(filter_flags & FIF_PROMISC_IN_BSS) &&
			   !rt2x00dev->intf_ap_count);
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_VERSION_ERROR, 1);
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_MULTICAST,
			   !(filter_flags & FIF_ALLMULTI));
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_BROADCAST, 0);
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_ACK_CTS,
			   !(filter_flags & FIF_CONTROL));
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR0, reg);
}

static void rt73usb_config_intf(struct rt2x00_dev *rt2x00dev,
				struct rt2x00_intf *intf,
				struct rt2x00intf_conf *conf,
				const unsigned int flags)
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
		rt2x00usb_register_write(rt2x00dev, beacon_base, 0);

		/*
		 * Enable synchronisation.
		 */
		rt2x00usb_register_read(rt2x00dev, TXRX_CSR9, &reg);
		rt2x00_set_field32(&reg, TXRX_CSR9_TSF_TICKING, 1);
		rt2x00_set_field32(&reg, TXRX_CSR9_TSF_SYNC, conf->sync);
		rt2x00_set_field32(&reg, TXRX_CSR9_TBTT_ENABLE, 1);
		rt2x00usb_register_write(rt2x00dev, TXRX_CSR9, reg);
	}

	if (flags & CONFIG_UPDATE_MAC) {
		reg = le32_to_cpu(conf->mac[1]);
		rt2x00_set_field32(&reg, MAC_CSR3_UNICAST_TO_ME_MASK, 0xff);
		conf->mac[1] = cpu_to_le32(reg);

		rt2x00usb_register_multiwrite(rt2x00dev, MAC_CSR2,
					    conf->mac, sizeof(conf->mac));
	}

	if (flags & CONFIG_UPDATE_BSSID) {
		reg = le32_to_cpu(conf->bssid[1]);
		rt2x00_set_field32(&reg, MAC_CSR5_BSS_ID_MASK, 3);
		conf->bssid[1] = cpu_to_le32(reg);

		rt2x00usb_register_multiwrite(rt2x00dev, MAC_CSR4,
					    conf->bssid, sizeof(conf->bssid));
	}
}

static void rt73usb_config_erp(struct rt2x00_dev *rt2x00dev,
			       struct rt2x00lib_erp *erp)
{
	u32 reg;

	rt2x00usb_register_read(rt2x00dev, TXRX_CSR0, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR0_RX_ACK_TIMEOUT, 0x32);
	rt2x00_set_field32(&reg, TXRX_CSR0_TSF_OFFSET, IEEE80211_HEADER);
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR0, reg);

	rt2x00usb_register_read(rt2x00dev, TXRX_CSR4, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR4_AUTORESPOND_ENABLE, 1);
	rt2x00_set_field32(&reg, TXRX_CSR4_AUTORESPOND_PREAMBLE,
			   !!erp->short_preamble);
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR4, reg);

	rt2x00usb_register_write(rt2x00dev, TXRX_CSR5, erp->basic_rates);

	rt2x00usb_register_read(rt2x00dev, TXRX_CSR9, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR9_BEACON_INTERVAL,
			   erp->beacon_int * 16);
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR9, reg);

	rt2x00usb_register_read(rt2x00dev, MAC_CSR9, &reg);
	rt2x00_set_field32(&reg, MAC_CSR9_SLOT_TIME, erp->slot_time);
	rt2x00usb_register_write(rt2x00dev, MAC_CSR9, reg);

	rt2x00usb_register_read(rt2x00dev, MAC_CSR8, &reg);
	rt2x00_set_field32(&reg, MAC_CSR8_SIFS, erp->sifs);
	rt2x00_set_field32(&reg, MAC_CSR8_SIFS_AFTER_RX_OFDM, 3);
	rt2x00_set_field32(&reg, MAC_CSR8_EIFS, erp->eifs);
	rt2x00usb_register_write(rt2x00dev, MAC_CSR8, reg);
}

static void rt73usb_config_antenna_5x(struct rt2x00_dev *rt2x00dev,
				      struct antenna_setup *ant)
{
	u8 r3;
	u8 r4;
	u8 r77;
	u8 temp;

	rt73usb_bbp_read(rt2x00dev, 3, &r3);
	rt73usb_bbp_read(rt2x00dev, 4, &r4);
	rt73usb_bbp_read(rt2x00dev, 77, &r77);

	rt2x00_set_field8(&r3, BBP_R3_SMART_MODE, 0);

	/*
	 * Configure the RX antenna.
	 */
	switch (ant->rx) {
	case ANTENNA_HW_DIVERSITY:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA_CONTROL, 2);
		temp = !test_bit(CONFIG_FRAME_TYPE, &rt2x00dev->flags)
		       && (rt2x00dev->curr_band != IEEE80211_BAND_5GHZ);
		rt2x00_set_field8(&r4, BBP_R4_RX_FRAME_END, temp);
		break;
	case ANTENNA_A:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA_CONTROL, 1);
		rt2x00_set_field8(&r4, BBP_R4_RX_FRAME_END, 0);
		if (rt2x00dev->curr_band == IEEE80211_BAND_5GHZ)
			rt2x00_set_field8(&r77, BBP_R77_RX_ANTENNA, 0);
		else
			rt2x00_set_field8(&r77, BBP_R77_RX_ANTENNA, 3);
		break;
	case ANTENNA_B:
	default:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA_CONTROL, 1);
		rt2x00_set_field8(&r4, BBP_R4_RX_FRAME_END, 0);
		if (rt2x00dev->curr_band == IEEE80211_BAND_5GHZ)
			rt2x00_set_field8(&r77, BBP_R77_RX_ANTENNA, 3);
		else
			rt2x00_set_field8(&r77, BBP_R77_RX_ANTENNA, 0);
		break;
	}

	rt73usb_bbp_write(rt2x00dev, 77, r77);
	rt73usb_bbp_write(rt2x00dev, 3, r3);
	rt73usb_bbp_write(rt2x00dev, 4, r4);
}

static void rt73usb_config_antenna_2x(struct rt2x00_dev *rt2x00dev,
				      struct antenna_setup *ant)
{
	u8 r3;
	u8 r4;
	u8 r77;

	rt73usb_bbp_read(rt2x00dev, 3, &r3);
	rt73usb_bbp_read(rt2x00dev, 4, &r4);
	rt73usb_bbp_read(rt2x00dev, 77, &r77);

	rt2x00_set_field8(&r3, BBP_R3_SMART_MODE, 0);
	rt2x00_set_field8(&r4, BBP_R4_RX_FRAME_END,
			  !test_bit(CONFIG_FRAME_TYPE, &rt2x00dev->flags));

	/*
	 * Configure the RX antenna.
	 */
	switch (ant->rx) {
	case ANTENNA_HW_DIVERSITY:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA_CONTROL, 2);
		break;
	case ANTENNA_A:
		rt2x00_set_field8(&r77, BBP_R77_RX_ANTENNA, 3);
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA_CONTROL, 1);
		break;
	case ANTENNA_B:
	default:
		rt2x00_set_field8(&r77, BBP_R77_RX_ANTENNA, 0);
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA_CONTROL, 1);
		break;
	}

	rt73usb_bbp_write(rt2x00dev, 77, r77);
	rt73usb_bbp_write(rt2x00dev, 3, r3);
	rt73usb_bbp_write(rt2x00dev, 4, r4);
}

struct antenna_sel {
	u8 word;
	/*
	 * value[0] -> non-LNA
	 * value[1] -> LNA
	 */
	u8 value[2];
};

static const struct antenna_sel antenna_sel_a[] = {
	{ 96,  { 0x58, 0x78 } },
	{ 104, { 0x38, 0x48 } },
	{ 75,  { 0xfe, 0x80 } },
	{ 86,  { 0xfe, 0x80 } },
	{ 88,  { 0xfe, 0x80 } },
	{ 35,  { 0x60, 0x60 } },
	{ 97,  { 0x58, 0x58 } },
	{ 98,  { 0x58, 0x58 } },
};

static const struct antenna_sel antenna_sel_bg[] = {
	{ 96,  { 0x48, 0x68 } },
	{ 104, { 0x2c, 0x3c } },
	{ 75,  { 0xfe, 0x80 } },
	{ 86,  { 0xfe, 0x80 } },
	{ 88,  { 0xfe, 0x80 } },
	{ 35,  { 0x50, 0x50 } },
	{ 97,  { 0x48, 0x48 } },
	{ 98,  { 0x48, 0x48 } },
};

static void rt73usb_config_ant(struct rt2x00_dev *rt2x00dev,
			       struct antenna_setup *ant)
{
	const struct antenna_sel *sel;
	unsigned int lna;
	unsigned int i;
	u32 reg;

	/*
	 * We should never come here because rt2x00lib is supposed
	 * to catch this and send us the correct antenna explicitely.
	 */
	BUG_ON(ant->rx == ANTENNA_SW_DIVERSITY ||
	       ant->tx == ANTENNA_SW_DIVERSITY);

	if (rt2x00dev->curr_band == IEEE80211_BAND_5GHZ) {
		sel = antenna_sel_a;
		lna = test_bit(CONFIG_EXTERNAL_LNA_A, &rt2x00dev->flags);
	} else {
		sel = antenna_sel_bg;
		lna = test_bit(CONFIG_EXTERNAL_LNA_BG, &rt2x00dev->flags);
	}

	for (i = 0; i < ARRAY_SIZE(antenna_sel_a); i++)
		rt73usb_bbp_write(rt2x00dev, sel[i].word, sel[i].value[lna]);

	rt2x00usb_register_read(rt2x00dev, PHY_CSR0, &reg);

	rt2x00_set_field32(&reg, PHY_CSR0_PA_PE_BG,
			   (rt2x00dev->curr_band == IEEE80211_BAND_2GHZ));
	rt2x00_set_field32(&reg, PHY_CSR0_PA_PE_A,
			   (rt2x00dev->curr_band == IEEE80211_BAND_5GHZ));

	rt2x00usb_register_write(rt2x00dev, PHY_CSR0, reg);

	if (rt2x00_rf(rt2x00dev, RF5226) || rt2x00_rf(rt2x00dev, RF5225))
		rt73usb_config_antenna_5x(rt2x00dev, ant);
	else if (rt2x00_rf(rt2x00dev, RF2528) || rt2x00_rf(rt2x00dev, RF2527))
		rt73usb_config_antenna_2x(rt2x00dev, ant);
}

static void rt73usb_config_lna_gain(struct rt2x00_dev *rt2x00dev,
				    struct rt2x00lib_conf *libconf)
{
	u16 eeprom;
	short lna_gain = 0;

	if (libconf->conf->channel->band == IEEE80211_BAND_2GHZ) {
		if (test_bit(CONFIG_EXTERNAL_LNA_BG, &rt2x00dev->flags))
			lna_gain += 14;

		rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_OFFSET_BG, &eeprom);
		lna_gain -= rt2x00_get_field16(eeprom, EEPROM_RSSI_OFFSET_BG_1);
	} else {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_OFFSET_A, &eeprom);
		lna_gain -= rt2x00_get_field16(eeprom, EEPROM_RSSI_OFFSET_A_1);
	}

	rt2x00dev->lna_gain = lna_gain;
}

static void rt73usb_config_channel(struct rt2x00_dev *rt2x00dev,
				   struct rf_channel *rf, const int txpower)
{
	u8 r3;
	u8 r94;
	u8 smart;

	rt2x00_set_field32(&rf->rf3, RF3_TXPOWER, TXPOWER_TO_DEV(txpower));
	rt2x00_set_field32(&rf->rf4, RF4_FREQ_OFFSET, rt2x00dev->freq_offset);

	smart = !(rt2x00_rf(rt2x00dev, RF5225) || rt2x00_rf(rt2x00dev, RF2527));

	rt73usb_bbp_read(rt2x00dev, 3, &r3);
	rt2x00_set_field8(&r3, BBP_R3_SMART_MODE, smart);
	rt73usb_bbp_write(rt2x00dev, 3, r3);

	r94 = 6;
	if (txpower > MAX_TXPOWER && txpower <= (MAX_TXPOWER + r94))
		r94 += txpower - MAX_TXPOWER;
	else if (txpower < MIN_TXPOWER && txpower >= (MIN_TXPOWER - r94))
		r94 += txpower;
	rt73usb_bbp_write(rt2x00dev, 94, r94);

	rt73usb_rf_write(rt2x00dev, 1, rf->rf1);
	rt73usb_rf_write(rt2x00dev, 2, rf->rf2);
	rt73usb_rf_write(rt2x00dev, 3, rf->rf3 & ~0x00000004);
	rt73usb_rf_write(rt2x00dev, 4, rf->rf4);

	rt73usb_rf_write(rt2x00dev, 1, rf->rf1);
	rt73usb_rf_write(rt2x00dev, 2, rf->rf2);
	rt73usb_rf_write(rt2x00dev, 3, rf->rf3 | 0x00000004);
	rt73usb_rf_write(rt2x00dev, 4, rf->rf4);

	rt73usb_rf_write(rt2x00dev, 1, rf->rf1);
	rt73usb_rf_write(rt2x00dev, 2, rf->rf2);
	rt73usb_rf_write(rt2x00dev, 3, rf->rf3 & ~0x00000004);
	rt73usb_rf_write(rt2x00dev, 4, rf->rf4);

	udelay(10);
}

static void rt73usb_config_txpower(struct rt2x00_dev *rt2x00dev,
				   const int txpower)
{
	struct rf_channel rf;

	rt2x00_rf_read(rt2x00dev, 1, &rf.rf1);
	rt2x00_rf_read(rt2x00dev, 2, &rf.rf2);
	rt2x00_rf_read(rt2x00dev, 3, &rf.rf3);
	rt2x00_rf_read(rt2x00dev, 4, &rf.rf4);

	rt73usb_config_channel(rt2x00dev, &rf, txpower);
}

static void rt73usb_config_retry_limit(struct rt2x00_dev *rt2x00dev,
				       struct rt2x00lib_conf *libconf)
{
	u32 reg;

	rt2x00usb_register_read(rt2x00dev, TXRX_CSR4, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR4_LONG_RETRY_LIMIT,
			   libconf->conf->long_frame_max_tx_count);
	rt2x00_set_field32(&reg, TXRX_CSR4_SHORT_RETRY_LIMIT,
			   libconf->conf->short_frame_max_tx_count);
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR4, reg);
}

static void rt73usb_config_ps(struct rt2x00_dev *rt2x00dev,
				struct rt2x00lib_conf *libconf)
{
	enum dev_state state =
	    (libconf->conf->flags & IEEE80211_CONF_PS) ?
		STATE_SLEEP : STATE_AWAKE;
	u32 reg;

	if (state == STATE_SLEEP) {
		rt2x00usb_register_read(rt2x00dev, MAC_CSR11, &reg);
		rt2x00_set_field32(&reg, MAC_CSR11_DELAY_AFTER_TBCN,
				   rt2x00dev->beacon_int - 10);
		rt2x00_set_field32(&reg, MAC_CSR11_TBCN_BEFORE_WAKEUP,
				   libconf->conf->listen_interval - 1);
		rt2x00_set_field32(&reg, MAC_CSR11_WAKEUP_LATENCY, 5);

		/* We must first disable autowake before it can be enabled */
		rt2x00_set_field32(&reg, MAC_CSR11_AUTOWAKE, 0);
		rt2x00usb_register_write(rt2x00dev, MAC_CSR11, reg);

		rt2x00_set_field32(&reg, MAC_CSR11_AUTOWAKE, 1);
		rt2x00usb_register_write(rt2x00dev, MAC_CSR11, reg);

		rt2x00usb_vendor_request_sw(rt2x00dev, USB_DEVICE_MODE, 0,
					    USB_MODE_SLEEP, REGISTER_TIMEOUT);
	} else {
		rt2x00usb_vendor_request_sw(rt2x00dev, USB_DEVICE_MODE, 0,
					    USB_MODE_WAKEUP, REGISTER_TIMEOUT);

		rt2x00usb_register_read(rt2x00dev, MAC_CSR11, &reg);
		rt2x00_set_field32(&reg, MAC_CSR11_DELAY_AFTER_TBCN, 0);
		rt2x00_set_field32(&reg, MAC_CSR11_TBCN_BEFORE_WAKEUP, 0);
		rt2x00_set_field32(&reg, MAC_CSR11_AUTOWAKE, 0);
		rt2x00_set_field32(&reg, MAC_CSR11_WAKEUP_LATENCY, 0);
		rt2x00usb_register_write(rt2x00dev, MAC_CSR11, reg);
	}
}

static void rt73usb_config(struct rt2x00_dev *rt2x00dev,
			   struct rt2x00lib_conf *libconf,
			   const unsigned int flags)
{
	/* Always recalculate LNA gain before changing configuration */
	rt73usb_config_lna_gain(rt2x00dev, libconf);

	if (flags & IEEE80211_CONF_CHANGE_CHANNEL)
		rt73usb_config_channel(rt2x00dev, &libconf->rf,
				       libconf->conf->power_level);
	if ((flags & IEEE80211_CONF_CHANGE_POWER) &&
	    !(flags & IEEE80211_CONF_CHANGE_CHANNEL))
		rt73usb_config_txpower(rt2x00dev, libconf->conf->power_level);
	if (flags & IEEE80211_CONF_CHANGE_RETRY_LIMITS)
		rt73usb_config_retry_limit(rt2x00dev, libconf);
	if (flags & IEEE80211_CONF_CHANGE_PS)
		rt73usb_config_ps(rt2x00dev, libconf);
}

/*
 * Link tuning
 */
static void rt73usb_link_stats(struct rt2x00_dev *rt2x00dev,
			       struct link_qual *qual)
{
	u32 reg;

	/*
	 * Update FCS error count from register.
	 */
	rt2x00usb_register_read(rt2x00dev, STA_CSR0, &reg);
	qual->rx_failed = rt2x00_get_field32(reg, STA_CSR0_FCS_ERROR);

	/*
	 * Update False CCA count from register.
	 */
	rt2x00usb_register_read(rt2x00dev, STA_CSR1, &reg);
	qual->false_cca = rt2x00_get_field32(reg, STA_CSR1_FALSE_CCA_ERROR);
}

static inline void rt73usb_set_vgc(struct rt2x00_dev *rt2x00dev,
				   struct link_qual *qual, u8 vgc_level)
{
	if (qual->vgc_level != vgc_level) {
		rt73usb_bbp_write(rt2x00dev, 17, vgc_level);
		qual->vgc_level = vgc_level;
		qual->vgc_level_reg = vgc_level;
	}
}

static void rt73usb_reset_tuner(struct rt2x00_dev *rt2x00dev,
				struct link_qual *qual)
{
	rt73usb_set_vgc(rt2x00dev, qual, 0x20);
}

static void rt73usb_link_tuner(struct rt2x00_dev *rt2x00dev,
			       struct link_qual *qual, const u32 count)
{
	u8 up_bound;
	u8 low_bound;

	/*
	 * Determine r17 bounds.
	 */
	if (rt2x00dev->rx_status.band == IEEE80211_BAND_5GHZ) {
		low_bound = 0x28;
		up_bound = 0x48;

		if (test_bit(CONFIG_EXTERNAL_LNA_A, &rt2x00dev->flags)) {
			low_bound += 0x10;
			up_bound += 0x10;
		}
	} else {
		if (qual->rssi > -82) {
			low_bound = 0x1c;
			up_bound = 0x40;
		} else if (qual->rssi > -84) {
			low_bound = 0x1c;
			up_bound = 0x20;
		} else {
			low_bound = 0x1c;
			up_bound = 0x1c;
		}

		if (test_bit(CONFIG_EXTERNAL_LNA_BG, &rt2x00dev->flags)) {
			low_bound += 0x14;
			up_bound += 0x10;
		}
	}

	/*
	 * If we are not associated, we should go straight to the
	 * dynamic CCA tuning.
	 */
	if (!rt2x00dev->intf_associated)
		goto dynamic_cca_tune;

	/*
	 * Special big-R17 for very short distance
	 */
	if (qual->rssi > -35) {
		rt73usb_set_vgc(rt2x00dev, qual, 0x60);
		return;
	}

	/*
	 * Special big-R17 for short distance
	 */
	if (qual->rssi >= -58) {
		rt73usb_set_vgc(rt2x00dev, qual, up_bound);
		return;
	}

	/*
	 * Special big-R17 for middle-short distance
	 */
	if (qual->rssi >= -66) {
		rt73usb_set_vgc(rt2x00dev, qual, low_bound + 0x10);
		return;
	}

	/*
	 * Special mid-R17 for middle distance
	 */
	if (qual->rssi >= -74) {
		rt73usb_set_vgc(rt2x00dev, qual, low_bound + 0x08);
		return;
	}

	/*
	 * Special case: Change up_bound based on the rssi.
	 * Lower up_bound when rssi is weaker then -74 dBm.
	 */
	up_bound -= 2 * (-74 - qual->rssi);
	if (low_bound > up_bound)
		up_bound = low_bound;

	if (qual->vgc_level > up_bound) {
		rt73usb_set_vgc(rt2x00dev, qual, up_bound);
		return;
	}

dynamic_cca_tune:

	/*
	 * r17 does not yet exceed upper limit, continue and base
	 * the r17 tuning on the false CCA count.
	 */
	if ((qual->false_cca > 512) && (qual->vgc_level < up_bound))
		rt73usb_set_vgc(rt2x00dev, qual,
				min_t(u8, qual->vgc_level + 4, up_bound));
	else if ((qual->false_cca < 100) && (qual->vgc_level > low_bound))
		rt73usb_set_vgc(rt2x00dev, qual,
				max_t(u8, qual->vgc_level - 4, low_bound));
}

/*
 * Firmware functions
 */
static char *rt73usb_get_firmware_name(struct rt2x00_dev *rt2x00dev)
{
	return FIRMWARE_RT2571;
}

static int rt73usb_check_firmware(struct rt2x00_dev *rt2x00dev,
				  const u8 *data, const size_t len)
{
	u16 fw_crc;
	u16 crc;

	/*
	 * Only support 2kb firmware files.
	 */
	if (len != 2048)
		return FW_BAD_LENGTH;

	/*
	 * The last 2 bytes in the firmware array are the crc checksum itself,
	 * this means that we should never pass those 2 bytes to the crc
	 * algorithm.
	 */
	fw_crc = (data[len - 2] << 8 | data[len - 1]);

	/*
	 * Use the crc itu-t algorithm.
	 */
	crc = crc_itu_t(0, data, len - 2);
	crc = crc_itu_t_byte(crc, 0);
	crc = crc_itu_t_byte(crc, 0);

	return (fw_crc == crc) ? FW_OK : FW_BAD_CRC;
}

static int rt73usb_load_firmware(struct rt2x00_dev *rt2x00dev,
				 const u8 *data, const size_t len)
{
	unsigned int i;
	int status;
	u32 reg;

	/*
	 * Wait for stable hardware.
	 */
	for (i = 0; i < 100; i++) {
		rt2x00usb_register_read(rt2x00dev, MAC_CSR0, &reg);
		if (reg)
			break;
		msleep(1);
	}

	if (!reg) {
		ERROR(rt2x00dev, "Unstable hardware.\n");
		return -EBUSY;
	}

	/*
	 * Write firmware to device.
	 */
	rt2x00usb_vendor_request_large_buff(rt2x00dev, USB_MULTI_WRITE,
					    USB_VENDOR_REQUEST_OUT,
					    FIRMWARE_IMAGE_BASE,
					    data, len,
					    REGISTER_TIMEOUT32(len));

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

	return 0;
}

/*
 * Initialization functions.
 */
static int rt73usb_init_registers(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2x00usb_register_read(rt2x00dev, TXRX_CSR0, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR0_AUTO_TX_SEQ, 1);
	rt2x00_set_field32(&reg, TXRX_CSR0_DISABLE_RX, 0);
	rt2x00_set_field32(&reg, TXRX_CSR0_TX_WITHOUT_WAITING, 0);
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR0, reg);

	rt2x00usb_register_read(rt2x00dev, TXRX_CSR1, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID0, 47); /* CCK Signal */
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID0_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID1, 30); /* Rssi */
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID1_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID2, 42); /* OFDM Rate */
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID2_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID3, 30); /* Rssi */
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID3_VALID, 1);
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR1, reg);

	/*
	 * CCK TXD BBP registers
	 */
	rt2x00usb_register_read(rt2x00dev, TXRX_CSR2, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID0, 13);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID0_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID1, 12);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID1_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID2, 11);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID2_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID3, 10);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID3_VALID, 1);
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR2, reg);

	/*
	 * OFDM TXD BBP registers
	 */
	rt2x00usb_register_read(rt2x00dev, TXRX_CSR3, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR3_BBP_ID0, 7);
	rt2x00_set_field32(&reg, TXRX_CSR3_BBP_ID0_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR3_BBP_ID1, 6);
	rt2x00_set_field32(&reg, TXRX_CSR3_BBP_ID1_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR3_BBP_ID2, 5);
	rt2x00_set_field32(&reg, TXRX_CSR3_BBP_ID2_VALID, 1);
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR3, reg);

	rt2x00usb_register_read(rt2x00dev, TXRX_CSR7, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR7_ACK_CTS_6MBS, 59);
	rt2x00_set_field32(&reg, TXRX_CSR7_ACK_CTS_9MBS, 53);
	rt2x00_set_field32(&reg, TXRX_CSR7_ACK_CTS_12MBS, 49);
	rt2x00_set_field32(&reg, TXRX_CSR7_ACK_CTS_18MBS, 46);
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR7, reg);

	rt2x00usb_register_read(rt2x00dev, TXRX_CSR8, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR8_ACK_CTS_24MBS, 44);
	rt2x00_set_field32(&reg, TXRX_CSR8_ACK_CTS_36MBS, 42);
	rt2x00_set_field32(&reg, TXRX_CSR8_ACK_CTS_48MBS, 42);
	rt2x00_set_field32(&reg, TXRX_CSR8_ACK_CTS_54MBS, 42);
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR8, reg);

	rt2x00usb_register_read(rt2x00dev, TXRX_CSR9, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR9_BEACON_INTERVAL, 0);
	rt2x00_set_field32(&reg, TXRX_CSR9_TSF_TICKING, 0);
	rt2x00_set_field32(&reg, TXRX_CSR9_TSF_SYNC, 0);
	rt2x00_set_field32(&reg, TXRX_CSR9_TBTT_ENABLE, 0);
	rt2x00_set_field32(&reg, TXRX_CSR9_BEACON_GEN, 0);
	rt2x00_set_field32(&reg, TXRX_CSR9_TIMESTAMP_COMPENSATE, 0);
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR9, reg);

	rt2x00usb_register_write(rt2x00dev, TXRX_CSR15, 0x0000000f);

	rt2x00usb_register_read(rt2x00dev, MAC_CSR6, &reg);
	rt2x00_set_field32(&reg, MAC_CSR6_MAX_FRAME_UNIT, 0xfff);
	rt2x00usb_register_write(rt2x00dev, MAC_CSR6, reg);

	rt2x00usb_register_write(rt2x00dev, MAC_CSR10, 0x00000718);

	if (rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_AWAKE))
		return -EBUSY;

	rt2x00usb_register_write(rt2x00dev, MAC_CSR13, 0x00007f00);

	/*
	 * Invalidate all Shared Keys (SEC_CSR0),
	 * and clear the Shared key Cipher algorithms (SEC_CSR1 & SEC_CSR5)
	 */
	rt2x00usb_register_write(rt2x00dev, SEC_CSR0, 0x00000000);
	rt2x00usb_register_write(rt2x00dev, SEC_CSR1, 0x00000000);
	rt2x00usb_register_write(rt2x00dev, SEC_CSR5, 0x00000000);

	reg = 0x000023b0;
	if (rt2x00_rf(rt2x00dev, RF5225) || rt2x00_rf(rt2x00dev, RF2527))
		rt2x00_set_field32(&reg, PHY_CSR1_RF_RPI, 1);
	rt2x00usb_register_write(rt2x00dev, PHY_CSR1, reg);

	rt2x00usb_register_write(rt2x00dev, PHY_CSR5, 0x00040a06);
	rt2x00usb_register_write(rt2x00dev, PHY_CSR6, 0x00080606);
	rt2x00usb_register_write(rt2x00dev, PHY_CSR7, 0x00000408);

	rt2x00usb_register_read(rt2x00dev, MAC_CSR9, &reg);
	rt2x00_set_field32(&reg, MAC_CSR9_CW_SELECT, 0);
	rt2x00usb_register_write(rt2x00dev, MAC_CSR9, reg);

	/*
	 * Clear all beacons
	 * For the Beacon base registers we only need to clear
	 * the first byte since that byte contains the VALID and OWNER
	 * bits which (when set to 0) will invalidate the entire beacon.
	 */
	rt2x00usb_register_write(rt2x00dev, HW_BEACON_BASE0, 0);
	rt2x00usb_register_write(rt2x00dev, HW_BEACON_BASE1, 0);
	rt2x00usb_register_write(rt2x00dev, HW_BEACON_BASE2, 0);
	rt2x00usb_register_write(rt2x00dev, HW_BEACON_BASE3, 0);

	/*
	 * We must clear the error counters.
	 * These registers are cleared on read,
	 * so we may pass a useless variable to store the value.
	 */
	rt2x00usb_register_read(rt2x00dev, STA_CSR0, &reg);
	rt2x00usb_register_read(rt2x00dev, STA_CSR1, &reg);
	rt2x00usb_register_read(rt2x00dev, STA_CSR2, &reg);

	/*
	 * Reset MAC and BBP registers.
	 */
	rt2x00usb_register_read(rt2x00dev, MAC_CSR1, &reg);
	rt2x00_set_field32(&reg, MAC_CSR1_SOFT_RESET, 1);
	rt2x00_set_field32(&reg, MAC_CSR1_BBP_RESET, 1);
	rt2x00usb_register_write(rt2x00dev, MAC_CSR1, reg);

	rt2x00usb_register_read(rt2x00dev, MAC_CSR1, &reg);
	rt2x00_set_field32(&reg, MAC_CSR1_SOFT_RESET, 0);
	rt2x00_set_field32(&reg, MAC_CSR1_BBP_RESET, 0);
	rt2x00usb_register_write(rt2x00dev, MAC_CSR1, reg);

	rt2x00usb_register_read(rt2x00dev, MAC_CSR1, &reg);
	rt2x00_set_field32(&reg, MAC_CSR1_HOST_READY, 1);
	rt2x00usb_register_write(rt2x00dev, MAC_CSR1, reg);

	return 0;
}

static int rt73usb_wait_bbp_ready(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u8 value;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt73usb_bbp_read(rt2x00dev, 0, &value);
		if ((value != 0xff) && (value != 0x00))
			return 0;
		udelay(REGISTER_BUSY_DELAY);
	}

	ERROR(rt2x00dev, "BBP register access failed, aborting.\n");
	return -EACCES;
}

static int rt73usb_init_bbp(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u16 eeprom;
	u8 reg_id;
	u8 value;

	if (unlikely(rt73usb_wait_bbp_ready(rt2x00dev)))
		return -EACCES;

	rt73usb_bbp_write(rt2x00dev, 3, 0x80);
	rt73usb_bbp_write(rt2x00dev, 15, 0x30);
	rt73usb_bbp_write(rt2x00dev, 21, 0xc8);
	rt73usb_bbp_write(rt2x00dev, 22, 0x38);
	rt73usb_bbp_write(rt2x00dev, 23, 0x06);
	rt73usb_bbp_write(rt2x00dev, 24, 0xfe);
	rt73usb_bbp_write(rt2x00dev, 25, 0x0a);
	rt73usb_bbp_write(rt2x00dev, 26, 0x0d);
	rt73usb_bbp_write(rt2x00dev, 32, 0x0b);
	rt73usb_bbp_write(rt2x00dev, 34, 0x12);
	rt73usb_bbp_write(rt2x00dev, 37, 0x07);
	rt73usb_bbp_write(rt2x00dev, 39, 0xf8);
	rt73usb_bbp_write(rt2x00dev, 41, 0x60);
	rt73usb_bbp_write(rt2x00dev, 53, 0x10);
	rt73usb_bbp_write(rt2x00dev, 54, 0x18);
	rt73usb_bbp_write(rt2x00dev, 60, 0x10);
	rt73usb_bbp_write(rt2x00dev, 61, 0x04);
	rt73usb_bbp_write(rt2x00dev, 62, 0x04);
	rt73usb_bbp_write(rt2x00dev, 75, 0xfe);
	rt73usb_bbp_write(rt2x00dev, 86, 0xfe);
	rt73usb_bbp_write(rt2x00dev, 88, 0xfe);
	rt73usb_bbp_write(rt2x00dev, 90, 0x0f);
	rt73usb_bbp_write(rt2x00dev, 99, 0x00);
	rt73usb_bbp_write(rt2x00dev, 102, 0x16);
	rt73usb_bbp_write(rt2x00dev, 107, 0x04);

	for (i = 0; i < EEPROM_BBP_SIZE; i++) {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_BBP_START + i, &eeprom);

		if (eeprom != 0xffff && eeprom != 0x0000) {
			reg_id = rt2x00_get_field16(eeprom, EEPROM_BBP_REG_ID);
			value = rt2x00_get_field16(eeprom, EEPROM_BBP_VALUE);
			rt73usb_bbp_write(rt2x00dev, reg_id, value);
		}
	}

	return 0;
}

/*
 * Device state switch handlers.
 */
static void rt73usb_toggle_rx(struct rt2x00_dev *rt2x00dev,
			      enum dev_state state)
{
	u32 reg;

	rt2x00usb_register_read(rt2x00dev, TXRX_CSR0, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR0_DISABLE_RX,
			   (state == STATE_RADIO_RX_OFF) ||
			   (state == STATE_RADIO_RX_OFF_LINK));
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR0, reg);
}

static int rt73usb_enable_radio(struct rt2x00_dev *rt2x00dev)
{
	/*
	 * Initialize all registers.
	 */
	if (unlikely(rt73usb_init_registers(rt2x00dev) ||
		     rt73usb_init_bbp(rt2x00dev)))
		return -EIO;

	return 0;
}

static void rt73usb_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	rt2x00usb_register_write(rt2x00dev, MAC_CSR10, 0x00001818);

	/*
	 * Disable synchronisation.
	 */
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR9, 0);

	rt2x00usb_disable_radio(rt2x00dev);
}

static int rt73usb_set_state(struct rt2x00_dev *rt2x00dev, enum dev_state state)
{
	u32 reg;
	unsigned int i;
	char put_to_sleep;

	put_to_sleep = (state != STATE_AWAKE);

	rt2x00usb_register_read(rt2x00dev, MAC_CSR12, &reg);
	rt2x00_set_field32(&reg, MAC_CSR12_FORCE_WAKEUP, !put_to_sleep);
	rt2x00_set_field32(&reg, MAC_CSR12_PUT_TO_SLEEP, put_to_sleep);
	rt2x00usb_register_write(rt2x00dev, MAC_CSR12, reg);

	/*
	 * Device is not guaranteed to be in the requested state yet.
	 * We must wait until the register indicates that the
	 * device has entered the correct state.
	 */
	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2x00usb_register_read(rt2x00dev, MAC_CSR12, &reg);
		state = rt2x00_get_field32(reg, MAC_CSR12_BBP_CURRENT_STATE);
		if (state == !put_to_sleep)
			return 0;
		msleep(10);
	}

	return -EBUSY;
}

static int rt73usb_set_device_state(struct rt2x00_dev *rt2x00dev,
				    enum dev_state state)
{
	int retval = 0;

	switch (state) {
	case STATE_RADIO_ON:
		retval = rt73usb_enable_radio(rt2x00dev);
		break;
	case STATE_RADIO_OFF:
		rt73usb_disable_radio(rt2x00dev);
		break;
	case STATE_RADIO_RX_ON:
	case STATE_RADIO_RX_ON_LINK:
	case STATE_RADIO_RX_OFF:
	case STATE_RADIO_RX_OFF_LINK:
		rt73usb_toggle_rx(rt2x00dev, state);
		break;
	case STATE_RADIO_IRQ_ON:
	case STATE_RADIO_IRQ_OFF:
		/* No support, but no error either */
		break;
	case STATE_DEEP_SLEEP:
	case STATE_SLEEP:
	case STATE_STANDBY:
	case STATE_AWAKE:
		retval = rt73usb_set_state(rt2x00dev, state);
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
static void rt73usb_write_tx_desc(struct rt2x00_dev *rt2x00dev,
				  struct sk_buff *skb,
				  struct txentry_desc *txdesc)
{
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(skb);
	__le32 *txd = skbdesc->desc;
	u32 word;

	/*
	 * Start writing the descriptor words.
	 */
	rt2x00_desc_read(txd, 1, &word);
	rt2x00_set_field32(&word, TXD_W1_HOST_Q_ID, txdesc->queue);
	rt2x00_set_field32(&word, TXD_W1_AIFSN, txdesc->aifs);
	rt2x00_set_field32(&word, TXD_W1_CWMIN, txdesc->cw_min);
	rt2x00_set_field32(&word, TXD_W1_CWMAX, txdesc->cw_max);
	rt2x00_set_field32(&word, TXD_W1_IV_OFFSET, txdesc->iv_offset);
	rt2x00_set_field32(&word, TXD_W1_HW_SEQUENCE,
			   test_bit(ENTRY_TXD_GENERATE_SEQ, &txdesc->flags));
	rt2x00_desc_write(txd, 1, word);

	rt2x00_desc_read(txd, 2, &word);
	rt2x00_set_field32(&word, TXD_W2_PLCP_SIGNAL, txdesc->signal);
	rt2x00_set_field32(&word, TXD_W2_PLCP_SERVICE, txdesc->service);
	rt2x00_set_field32(&word, TXD_W2_PLCP_LENGTH_LOW, txdesc->length_low);
	rt2x00_set_field32(&word, TXD_W2_PLCP_LENGTH_HIGH, txdesc->length_high);
	rt2x00_desc_write(txd, 2, word);

	if (test_bit(ENTRY_TXD_ENCRYPT, &txdesc->flags)) {
		_rt2x00_desc_write(txd, 3, skbdesc->iv[0]);
		_rt2x00_desc_write(txd, 4, skbdesc->iv[1]);
	}

	rt2x00_desc_read(txd, 5, &word);
	rt2x00_set_field32(&word, TXD_W5_TX_POWER,
			   TXPOWER_TO_DEV(rt2x00dev->tx_power));
	rt2x00_set_field32(&word, TXD_W5_WAITING_DMA_DONE_INT, 1);
	rt2x00_desc_write(txd, 5, word);

	rt2x00_desc_read(txd, 0, &word);
	rt2x00_set_field32(&word, TXD_W0_BURST,
			   test_bit(ENTRY_TXD_BURST, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_VALID, 1);
	rt2x00_set_field32(&word, TXD_W0_MORE_FRAG,
			   test_bit(ENTRY_TXD_MORE_FRAG, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_ACK,
			   test_bit(ENTRY_TXD_ACK, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_TIMESTAMP,
			   test_bit(ENTRY_TXD_REQ_TIMESTAMP, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_OFDM,
			   (txdesc->rate_mode == RATE_MODE_OFDM));
	rt2x00_set_field32(&word, TXD_W0_IFS, txdesc->ifs);
	rt2x00_set_field32(&word, TXD_W0_RETRY_MODE,
			   test_bit(ENTRY_TXD_RETRY_MODE, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_TKIP_MIC,
			   test_bit(ENTRY_TXD_ENCRYPT_MMIC, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_KEY_TABLE,
			   test_bit(ENTRY_TXD_ENCRYPT_PAIRWISE, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_KEY_INDEX, txdesc->key_idx);
	rt2x00_set_field32(&word, TXD_W0_DATABYTE_COUNT, skb->len);
	rt2x00_set_field32(&word, TXD_W0_BURST2,
			   test_bit(ENTRY_TXD_BURST, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_CIPHER_ALG, txdesc->cipher);
	rt2x00_desc_write(txd, 0, word);
}

/*
 * TX data initialization
 */
static void rt73usb_write_beacon(struct queue_entry *entry)
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
	rt2x00usb_register_read(rt2x00dev, TXRX_CSR9, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR9_BEACON_GEN, 0);
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR9, reg);

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

static int rt73usb_get_tx_data_len(struct queue_entry *entry)
{
	int length;

	/*
	 * The length _must_ be a multiple of 4,
	 * but it must _not_ be a multiple of the USB packet size.
	 */
	length = roundup(entry->skb->len, 4);
	length += (4 * !(length % entry->queue->usb_maxpacket));

	return length;
}

static void rt73usb_kick_tx_queue(struct rt2x00_dev *rt2x00dev,
				  const enum data_queue_qid queue)
{
	u32 reg;

	if (queue != QID_BEACON) {
		rt2x00usb_kick_tx_queue(rt2x00dev, queue);
		return;
	}

	/*
	 * For Wi-Fi faily generated beacons between participating stations.
	 * Set TBTT phase adaptive adjustment step to 8us (default 16us)
	 */
	rt2x00usb_register_write(rt2x00dev, TXRX_CSR10, 0x00001008);

	rt2x00usb_register_read(rt2x00dev, TXRX_CSR9, &reg);
	if (!rt2x00_get_field32(reg, TXRX_CSR9_BEACON_GEN)) {
		rt2x00_set_field32(&reg, TXRX_CSR9_TSF_TICKING, 1);
		rt2x00_set_field32(&reg, TXRX_CSR9_TBTT_ENABLE, 1);
		rt2x00_set_field32(&reg, TXRX_CSR9_BEACON_GEN, 1);
		rt2x00usb_register_write(rt2x00dev, TXRX_CSR9, reg);
	}
}

/*
 * RX control handlers
 */
static int rt73usb_agc_to_rssi(struct rt2x00_dev *rt2x00dev, int rxd_w1)
{
	u8 offset = rt2x00dev->lna_gain;
	u8 lna;

	lna = rt2x00_get_field32(rxd_w1, RXD_W1_RSSI_LNA);
	switch (lna) {
	case 3:
		offset += 90;
		break;
	case 2:
		offset += 74;
		break;
	case 1:
		offset += 64;
		break;
	default:
		return 0;
	}

	if (rt2x00dev->rx_status.band == IEEE80211_BAND_5GHZ) {
		if (test_bit(CONFIG_EXTERNAL_LNA_A, &rt2x00dev->flags)) {
			if (lna == 3 || lna == 2)
				offset += 10;
		} else {
			if (lna == 3)
				offset += 6;
			else if (lna == 2)
				offset += 8;
		}
	}

	return rt2x00_get_field32(rxd_w1, RXD_W1_RSSI_AGC) * 2 - offset;
}

static void rt73usb_fill_rxdone(struct queue_entry *entry,
				struct rxdone_entry_desc *rxdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	__le32 *rxd = (__le32 *)entry->skb->data;
	u32 word0;
	u32 word1;

	/*
	 * Copy descriptor to the skbdesc->desc buffer, making it safe from moving of
	 * frame data in rt2x00usb.
	 */
	memcpy(skbdesc->desc, rxd, skbdesc->desc_len);
	rxd = (__le32 *)skbdesc->desc;

	/*
	 * It is now safe to read the descriptor on all architectures.
	 */
	rt2x00_desc_read(rxd, 0, &word0);
	rt2x00_desc_read(rxd, 1, &word1);

	if (rt2x00_get_field32(word0, RXD_W0_CRC_ERROR))
		rxdesc->flags |= RX_FLAG_FAILED_FCS_CRC;

	if (test_bit(CONFIG_SUPPORT_HW_CRYPTO, &rt2x00dev->flags)) {
		rxdesc->cipher =
		    rt2x00_get_field32(word0, RXD_W0_CIPHER_ALG);
		rxdesc->cipher_status =
		    rt2x00_get_field32(word0, RXD_W0_CIPHER_ERROR);
	}

	if (rxdesc->cipher != CIPHER_NONE) {
		_rt2x00_desc_read(rxd, 2, &rxdesc->iv[0]);
		_rt2x00_desc_read(rxd, 3, &rxdesc->iv[1]);
		rxdesc->dev_flags |= RXDONE_CRYPTO_IV;

		_rt2x00_desc_read(rxd, 4, &rxdesc->icv);
		rxdesc->dev_flags |= RXDONE_CRYPTO_ICV;

		/*
		 * Hardware has stripped IV/EIV data from 802.11 frame during
		 * decryption. It has provided the data seperately but rt2x00lib
		 * should decide if it should be reinserted.
		 */
		rxdesc->flags |= RX_FLAG_IV_STRIPPED;

		/*
		 * FIXME: Legacy driver indicates that the frame does
		 * contain the Michael Mic. Unfortunately, in rt2x00
		 * the MIC seems to be missing completely...
		 */
		rxdesc->flags |= RX_FLAG_MMIC_STRIPPED;

		if (rxdesc->cipher_status == RX_CRYPTO_SUCCESS)
			rxdesc->flags |= RX_FLAG_DECRYPTED;
		else if (rxdesc->cipher_status == RX_CRYPTO_FAIL_MIC)
			rxdesc->flags |= RX_FLAG_MMIC_ERROR;
	}

	/*
	 * Obtain the status about this packet.
	 * When frame was received with an OFDM bitrate,
	 * the signal is the PLCP value. If it was received with
	 * a CCK bitrate the signal is the rate in 100kbit/s.
	 */
	rxdesc->signal = rt2x00_get_field32(word1, RXD_W1_SIGNAL);
	rxdesc->rssi = rt73usb_agc_to_rssi(rt2x00dev, word1);
	rxdesc->size = rt2x00_get_field32(word0, RXD_W0_DATABYTE_COUNT);

	if (rt2x00_get_field32(word0, RXD_W0_OFDM))
		rxdesc->dev_flags |= RXDONE_SIGNAL_PLCP;
	else
		rxdesc->dev_flags |= RXDONE_SIGNAL_BITRATE;
	if (rt2x00_get_field32(word0, RXD_W0_MY_BSS))
		rxdesc->dev_flags |= RXDONE_MY_BSS;

	/*
	 * Set skb pointers, and update frame information.
	 */
	skb_pull(entry->skb, entry->queue->desc_size);
	skb_trim(entry->skb, rxdesc->size);
}

/*
 * Device probe functions.
 */
static int rt73usb_validate_eeprom(struct rt2x00_dev *rt2x00dev)
{
	u16 word;
	u8 *mac;
	s8 value;

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
		rt2x00_set_field16(&word, EEPROM_ANTENNA_NUM, 2);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_TX_DEFAULT,
				   ANTENNA_B);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_RX_DEFAULT,
				   ANTENNA_B);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_FRAME_TYPE, 0);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_DYN_TXAGC, 0);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_HARDWARE_RADIO, 0);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_RF_TYPE, RF5226);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_ANTENNA, word);
		EEPROM(rt2x00dev, "Antenna: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_NIC_EXTERNAL_LNA, 0);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_NIC, word);
		EEPROM(rt2x00dev, "NIC: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_LED_POLARITY_RDY_G, 0);
		rt2x00_set_field16(&word, EEPROM_LED_POLARITY_RDY_A, 0);
		rt2x00_set_field16(&word, EEPROM_LED_POLARITY_ACT, 0);
		rt2x00_set_field16(&word, EEPROM_LED_POLARITY_GPIO_0, 0);
		rt2x00_set_field16(&word, EEPROM_LED_POLARITY_GPIO_1, 0);
		rt2x00_set_field16(&word, EEPROM_LED_POLARITY_GPIO_2, 0);
		rt2x00_set_field16(&word, EEPROM_LED_POLARITY_GPIO_3, 0);
		rt2x00_set_field16(&word, EEPROM_LED_POLARITY_GPIO_4, 0);
		rt2x00_set_field16(&word, EEPROM_LED_LED_MODE,
				   LED_MODE_DEFAULT);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_LED, word);
		EEPROM(rt2x00dev, "Led: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_FREQ, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_FREQ_OFFSET, 0);
		rt2x00_set_field16(&word, EEPROM_FREQ_SEQ, 0);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_FREQ, word);
		EEPROM(rt2x00dev, "Freq: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_OFFSET_BG, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_RSSI_OFFSET_BG_1, 0);
		rt2x00_set_field16(&word, EEPROM_RSSI_OFFSET_BG_2, 0);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_RSSI_OFFSET_BG, word);
		EEPROM(rt2x00dev, "RSSI OFFSET BG: 0x%04x\n", word);
	} else {
		value = rt2x00_get_field16(word, EEPROM_RSSI_OFFSET_BG_1);
		if (value < -10 || value > 10)
			rt2x00_set_field16(&word, EEPROM_RSSI_OFFSET_BG_1, 0);
		value = rt2x00_get_field16(word, EEPROM_RSSI_OFFSET_BG_2);
		if (value < -10 || value > 10)
			rt2x00_set_field16(&word, EEPROM_RSSI_OFFSET_BG_2, 0);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_RSSI_OFFSET_BG, word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_OFFSET_A, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_RSSI_OFFSET_A_1, 0);
		rt2x00_set_field16(&word, EEPROM_RSSI_OFFSET_A_2, 0);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_RSSI_OFFSET_A, word);
		EEPROM(rt2x00dev, "RSSI OFFSET A: 0x%04x\n", word);
	} else {
		value = rt2x00_get_field16(word, EEPROM_RSSI_OFFSET_A_1);
		if (value < -10 || value > 10)
			rt2x00_set_field16(&word, EEPROM_RSSI_OFFSET_A_1, 0);
		value = rt2x00_get_field16(word, EEPROM_RSSI_OFFSET_A_2);
		if (value < -10 || value > 10)
			rt2x00_set_field16(&word, EEPROM_RSSI_OFFSET_A_2, 0);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_RSSI_OFFSET_A, word);
	}

	return 0;
}

static int rt73usb_init_eeprom(struct rt2x00_dev *rt2x00dev)
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
	rt2x00usb_register_read(rt2x00dev, MAC_CSR0, &reg);
	rt2x00_set_chip(rt2x00dev, rt2x00_get_field32(reg, MAC_CSR0_CHIPSET),
			value, rt2x00_get_field32(reg, MAC_CSR0_REVISION));

	if (!rt2x00_rt(rt2x00dev, RT2573) || (rt2x00_rev(rt2x00dev) == 0)) {
		ERROR(rt2x00dev, "Invalid RT chipset detected.\n");
		return -ENODEV;
	}

	if (!rt2x00_rf(rt2x00dev, RF5226) &&
	    !rt2x00_rf(rt2x00dev, RF2528) &&
	    !rt2x00_rf(rt2x00dev, RF5225) &&
	    !rt2x00_rf(rt2x00dev, RF2527)) {
		ERROR(rt2x00dev, "Invalid RF chipset detected.\n");
		return -ENODEV;
	}

	/*
	 * Identify default antenna configuration.
	 */
	rt2x00dev->default_ant.tx =
	    rt2x00_get_field16(eeprom, EEPROM_ANTENNA_TX_DEFAULT);
	rt2x00dev->default_ant.rx =
	    rt2x00_get_field16(eeprom, EEPROM_ANTENNA_RX_DEFAULT);

	/*
	 * Read the Frame type.
	 */
	if (rt2x00_get_field16(eeprom, EEPROM_ANTENNA_FRAME_TYPE))
		__set_bit(CONFIG_FRAME_TYPE, &rt2x00dev->flags);

	/*
	 * Detect if this device has an hardware controlled radio.
	 */
	if (rt2x00_get_field16(eeprom, EEPROM_ANTENNA_HARDWARE_RADIO))
		__set_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags);

	/*
	 * Read frequency offset.
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_FREQ, &eeprom);
	rt2x00dev->freq_offset = rt2x00_get_field16(eeprom, EEPROM_FREQ_OFFSET);

	/*
	 * Read external LNA informations.
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC, &eeprom);

	if (rt2x00_get_field16(eeprom, EEPROM_NIC_EXTERNAL_LNA)) {
		__set_bit(CONFIG_EXTERNAL_LNA_A, &rt2x00dev->flags);
		__set_bit(CONFIG_EXTERNAL_LNA_BG, &rt2x00dev->flags);
	}

	/*
	 * Store led settings, for correct led behaviour.
	 */
#ifdef CONFIG_RT2X00_LIB_LEDS
	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED, &eeprom);

	rt73usb_init_led(rt2x00dev, &rt2x00dev->led_radio, LED_TYPE_RADIO);
	rt73usb_init_led(rt2x00dev, &rt2x00dev->led_assoc, LED_TYPE_ASSOC);
	if (value == LED_MODE_SIGNAL_STRENGTH)
		rt73usb_init_led(rt2x00dev, &rt2x00dev->led_qual,
				 LED_TYPE_QUALITY);

	rt2x00_set_field16(&rt2x00dev->led_mcu_reg, MCU_LEDCS_LED_MODE, value);
	rt2x00_set_field16(&rt2x00dev->led_mcu_reg, MCU_LEDCS_POLARITY_GPIO_0,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_GPIO_0));
	rt2x00_set_field16(&rt2x00dev->led_mcu_reg, MCU_LEDCS_POLARITY_GPIO_1,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_GPIO_1));
	rt2x00_set_field16(&rt2x00dev->led_mcu_reg, MCU_LEDCS_POLARITY_GPIO_2,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_GPIO_2));
	rt2x00_set_field16(&rt2x00dev->led_mcu_reg, MCU_LEDCS_POLARITY_GPIO_3,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_GPIO_3));
	rt2x00_set_field16(&rt2x00dev->led_mcu_reg, MCU_LEDCS_POLARITY_GPIO_4,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_GPIO_4));
	rt2x00_set_field16(&rt2x00dev->led_mcu_reg, MCU_LEDCS_POLARITY_ACT,
			   rt2x00_get_field16(eeprom, EEPROM_LED_POLARITY_ACT));
	rt2x00_set_field16(&rt2x00dev->led_mcu_reg, MCU_LEDCS_POLARITY_READY_BG,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_RDY_G));
	rt2x00_set_field16(&rt2x00dev->led_mcu_reg, MCU_LEDCS_POLARITY_READY_A,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_RDY_A));
#endif /* CONFIG_RT2X00_LIB_LEDS */

	return 0;
}

/*
 * RF value list for RF2528
 * Supports: 2.4 GHz
 */
static const struct rf_channel rf_vals_bg_2528[] = {
	{ 1,  0x00002c0c, 0x00000786, 0x00068255, 0x000fea0b },
	{ 2,  0x00002c0c, 0x00000786, 0x00068255, 0x000fea1f },
	{ 3,  0x00002c0c, 0x0000078a, 0x00068255, 0x000fea0b },
	{ 4,  0x00002c0c, 0x0000078a, 0x00068255, 0x000fea1f },
	{ 5,  0x00002c0c, 0x0000078e, 0x00068255, 0x000fea0b },
	{ 6,  0x00002c0c, 0x0000078e, 0x00068255, 0x000fea1f },
	{ 7,  0x00002c0c, 0x00000792, 0x00068255, 0x000fea0b },
	{ 8,  0x00002c0c, 0x00000792, 0x00068255, 0x000fea1f },
	{ 9,  0x00002c0c, 0x00000796, 0x00068255, 0x000fea0b },
	{ 10, 0x00002c0c, 0x00000796, 0x00068255, 0x000fea1f },
	{ 11, 0x00002c0c, 0x0000079a, 0x00068255, 0x000fea0b },
	{ 12, 0x00002c0c, 0x0000079a, 0x00068255, 0x000fea1f },
	{ 13, 0x00002c0c, 0x0000079e, 0x00068255, 0x000fea0b },
	{ 14, 0x00002c0c, 0x000007a2, 0x00068255, 0x000fea13 },
};

/*
 * RF value list for RF5226
 * Supports: 2.4 GHz & 5.2 GHz
 */
static const struct rf_channel rf_vals_5226[] = {
	{ 1,  0x00002c0c, 0x00000786, 0x00068255, 0x000fea0b },
	{ 2,  0x00002c0c, 0x00000786, 0x00068255, 0x000fea1f },
	{ 3,  0x00002c0c, 0x0000078a, 0x00068255, 0x000fea0b },
	{ 4,  0x00002c0c, 0x0000078a, 0x00068255, 0x000fea1f },
	{ 5,  0x00002c0c, 0x0000078e, 0x00068255, 0x000fea0b },
	{ 6,  0x00002c0c, 0x0000078e, 0x00068255, 0x000fea1f },
	{ 7,  0x00002c0c, 0x00000792, 0x00068255, 0x000fea0b },
	{ 8,  0x00002c0c, 0x00000792, 0x00068255, 0x000fea1f },
	{ 9,  0x00002c0c, 0x00000796, 0x00068255, 0x000fea0b },
	{ 10, 0x00002c0c, 0x00000796, 0x00068255, 0x000fea1f },
	{ 11, 0x00002c0c, 0x0000079a, 0x00068255, 0x000fea0b },
	{ 12, 0x00002c0c, 0x0000079a, 0x00068255, 0x000fea1f },
	{ 13, 0x00002c0c, 0x0000079e, 0x00068255, 0x000fea0b },
	{ 14, 0x00002c0c, 0x000007a2, 0x00068255, 0x000fea13 },

	/* 802.11 UNI / HyperLan 2 */
	{ 36, 0x00002c0c, 0x0000099a, 0x00098255, 0x000fea23 },
	{ 40, 0x00002c0c, 0x000009a2, 0x00098255, 0x000fea03 },
	{ 44, 0x00002c0c, 0x000009a6, 0x00098255, 0x000fea0b },
	{ 48, 0x00002c0c, 0x000009aa, 0x00098255, 0x000fea13 },
	{ 52, 0x00002c0c, 0x000009ae, 0x00098255, 0x000fea1b },
	{ 56, 0x00002c0c, 0x000009b2, 0x00098255, 0x000fea23 },
	{ 60, 0x00002c0c, 0x000009ba, 0x00098255, 0x000fea03 },
	{ 64, 0x00002c0c, 0x000009be, 0x00098255, 0x000fea0b },

	/* 802.11 HyperLan 2 */
	{ 100, 0x00002c0c, 0x00000a2a, 0x000b8255, 0x000fea03 },
	{ 104, 0x00002c0c, 0x00000a2e, 0x000b8255, 0x000fea0b },
	{ 108, 0x00002c0c, 0x00000a32, 0x000b8255, 0x000fea13 },
	{ 112, 0x00002c0c, 0x00000a36, 0x000b8255, 0x000fea1b },
	{ 116, 0x00002c0c, 0x00000a3a, 0x000b8255, 0x000fea23 },
	{ 120, 0x00002c0c, 0x00000a82, 0x000b8255, 0x000fea03 },
	{ 124, 0x00002c0c, 0x00000a86, 0x000b8255, 0x000fea0b },
	{ 128, 0x00002c0c, 0x00000a8a, 0x000b8255, 0x000fea13 },
	{ 132, 0x00002c0c, 0x00000a8e, 0x000b8255, 0x000fea1b },
	{ 136, 0x00002c0c, 0x00000a92, 0x000b8255, 0x000fea23 },

	/* 802.11 UNII */
	{ 140, 0x00002c0c, 0x00000a9a, 0x000b8255, 0x000fea03 },
	{ 149, 0x00002c0c, 0x00000aa2, 0x000b8255, 0x000fea1f },
	{ 153, 0x00002c0c, 0x00000aa6, 0x000b8255, 0x000fea27 },
	{ 157, 0x00002c0c, 0x00000aae, 0x000b8255, 0x000fea07 },
	{ 161, 0x00002c0c, 0x00000ab2, 0x000b8255, 0x000fea0f },
	{ 165, 0x00002c0c, 0x00000ab6, 0x000b8255, 0x000fea17 },

	/* MMAC(Japan)J52 ch 34,38,42,46 */
	{ 34, 0x00002c0c, 0x0008099a, 0x000da255, 0x000d3a0b },
	{ 38, 0x00002c0c, 0x0008099e, 0x000da255, 0x000d3a13 },
	{ 42, 0x00002c0c, 0x000809a2, 0x000da255, 0x000d3a1b },
	{ 46, 0x00002c0c, 0x000809a6, 0x000da255, 0x000d3a23 },
};

/*
 * RF value list for RF5225 & RF2527
 * Supports: 2.4 GHz & 5.2 GHz
 */
static const struct rf_channel rf_vals_5225_2527[] = {
	{ 1,  0x00002ccc, 0x00004786, 0x00068455, 0x000ffa0b },
	{ 2,  0x00002ccc, 0x00004786, 0x00068455, 0x000ffa1f },
	{ 3,  0x00002ccc, 0x0000478a, 0x00068455, 0x000ffa0b },
	{ 4,  0x00002ccc, 0x0000478a, 0x00068455, 0x000ffa1f },
	{ 5,  0x00002ccc, 0x0000478e, 0x00068455, 0x000ffa0b },
	{ 6,  0x00002ccc, 0x0000478e, 0x00068455, 0x000ffa1f },
	{ 7,  0x00002ccc, 0x00004792, 0x00068455, 0x000ffa0b },
	{ 8,  0x00002ccc, 0x00004792, 0x00068455, 0x000ffa1f },
	{ 9,  0x00002ccc, 0x00004796, 0x00068455, 0x000ffa0b },
	{ 10, 0x00002ccc, 0x00004796, 0x00068455, 0x000ffa1f },
	{ 11, 0x00002ccc, 0x0000479a, 0x00068455, 0x000ffa0b },
	{ 12, 0x00002ccc, 0x0000479a, 0x00068455, 0x000ffa1f },
	{ 13, 0x00002ccc, 0x0000479e, 0x00068455, 0x000ffa0b },
	{ 14, 0x00002ccc, 0x000047a2, 0x00068455, 0x000ffa13 },

	/* 802.11 UNI / HyperLan 2 */
	{ 36, 0x00002ccc, 0x0000499a, 0x0009be55, 0x000ffa23 },
	{ 40, 0x00002ccc, 0x000049a2, 0x0009be55, 0x000ffa03 },
	{ 44, 0x00002ccc, 0x000049a6, 0x0009be55, 0x000ffa0b },
	{ 48, 0x00002ccc, 0x000049aa, 0x0009be55, 0x000ffa13 },
	{ 52, 0x00002ccc, 0x000049ae, 0x0009ae55, 0x000ffa1b },
	{ 56, 0x00002ccc, 0x000049b2, 0x0009ae55, 0x000ffa23 },
	{ 60, 0x00002ccc, 0x000049ba, 0x0009ae55, 0x000ffa03 },
	{ 64, 0x00002ccc, 0x000049be, 0x0009ae55, 0x000ffa0b },

	/* 802.11 HyperLan 2 */
	{ 100, 0x00002ccc, 0x00004a2a, 0x000bae55, 0x000ffa03 },
	{ 104, 0x00002ccc, 0x00004a2e, 0x000bae55, 0x000ffa0b },
	{ 108, 0x00002ccc, 0x00004a32, 0x000bae55, 0x000ffa13 },
	{ 112, 0x00002ccc, 0x00004a36, 0x000bae55, 0x000ffa1b },
	{ 116, 0x00002ccc, 0x00004a3a, 0x000bbe55, 0x000ffa23 },
	{ 120, 0x00002ccc, 0x00004a82, 0x000bbe55, 0x000ffa03 },
	{ 124, 0x00002ccc, 0x00004a86, 0x000bbe55, 0x000ffa0b },
	{ 128, 0x00002ccc, 0x00004a8a, 0x000bbe55, 0x000ffa13 },
	{ 132, 0x00002ccc, 0x00004a8e, 0x000bbe55, 0x000ffa1b },
	{ 136, 0x00002ccc, 0x00004a92, 0x000bbe55, 0x000ffa23 },

	/* 802.11 UNII */
	{ 140, 0x00002ccc, 0x00004a9a, 0x000bbe55, 0x000ffa03 },
	{ 149, 0x00002ccc, 0x00004aa2, 0x000bbe55, 0x000ffa1f },
	{ 153, 0x00002ccc, 0x00004aa6, 0x000bbe55, 0x000ffa27 },
	{ 157, 0x00002ccc, 0x00004aae, 0x000bbe55, 0x000ffa07 },
	{ 161, 0x00002ccc, 0x00004ab2, 0x000bbe55, 0x000ffa0f },
	{ 165, 0x00002ccc, 0x00004ab6, 0x000bbe55, 0x000ffa17 },

	/* MMAC(Japan)J52 ch 34,38,42,46 */
	{ 34, 0x00002ccc, 0x0000499a, 0x0009be55, 0x000ffa0b },
	{ 38, 0x00002ccc, 0x0000499e, 0x0009be55, 0x000ffa13 },
	{ 42, 0x00002ccc, 0x000049a2, 0x0009be55, 0x000ffa1b },
	{ 46, 0x00002ccc, 0x000049a6, 0x0009be55, 0x000ffa23 },
};


static int rt73usb_probe_hw_mode(struct rt2x00_dev *rt2x00dev)
{
	struct hw_mode_spec *spec = &rt2x00dev->spec;
	struct channel_info *info;
	char *tx_power;
	unsigned int i;

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

	/*
	 * Initialize hw_mode information.
	 */
	spec->supported_bands = SUPPORT_BAND_2GHZ;
	spec->supported_rates = SUPPORT_RATE_CCK | SUPPORT_RATE_OFDM;

	if (rt2x00_rf(rt2x00dev, RF2528)) {
		spec->num_channels = ARRAY_SIZE(rf_vals_bg_2528);
		spec->channels = rf_vals_bg_2528;
	} else if (rt2x00_rf(rt2x00dev, RF5226)) {
		spec->supported_bands |= SUPPORT_BAND_5GHZ;
		spec->num_channels = ARRAY_SIZE(rf_vals_5226);
		spec->channels = rf_vals_5226;
	} else if (rt2x00_rf(rt2x00dev, RF2527)) {
		spec->num_channels = 14;
		spec->channels = rf_vals_5225_2527;
	} else if (rt2x00_rf(rt2x00dev, RF5225)) {
		spec->supported_bands |= SUPPORT_BAND_5GHZ;
		spec->num_channels = ARRAY_SIZE(rf_vals_5225_2527);
		spec->channels = rf_vals_5225_2527;
	}

	/*
	 * Create channel information array
	 */
	info = kzalloc(spec->num_channels * sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	spec->channels_info = info;

	tx_power = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_G_START);
	for (i = 0; i < 14; i++)
		info[i].tx_power1 = TXPOWER_FROM_DEV(tx_power[i]);

	if (spec->num_channels > 14) {
		tx_power = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_A_START);
		for (i = 14; i < spec->num_channels; i++)
			info[i].tx_power1 = TXPOWER_FROM_DEV(tx_power[i]);
	}

	return 0;
}

static int rt73usb_probe_hw(struct rt2x00_dev *rt2x00dev)
{
	int retval;

	/*
	 * Allocate eeprom data.
	 */
	retval = rt73usb_validate_eeprom(rt2x00dev);
	if (retval)
		return retval;

	retval = rt73usb_init_eeprom(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * Initialize hw specifications.
	 */
	retval = rt73usb_probe_hw_mode(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * This device has multiple filters for control frames,
	 * but has no a separate filter for PS Poll frames.
	 */
	__set_bit(DRIVER_SUPPORT_CONTROL_FILTERS, &rt2x00dev->flags);

	/*
	 * This device requires firmware.
	 */
	__set_bit(DRIVER_REQUIRE_FIRMWARE, &rt2x00dev->flags);
	if (!modparam_nohwcrypt)
		__set_bit(CONFIG_SUPPORT_HW_CRYPTO, &rt2x00dev->flags);

	/*
	 * Set the rssi offset.
	 */
	rt2x00dev->rssi_offset = DEFAULT_RSSI_OFFSET;

	return 0;
}

/*
 * IEEE80211 stack callback functions.
 */
static int rt73usb_conf_tx(struct ieee80211_hw *hw, u16 queue_idx,
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
	offset = AC_TXOP_CSR0 + (sizeof(u32) * (!!(queue_idx & 2)));
	field.bit_offset = (queue_idx & 1) * 16;
	field.bit_mask = 0xffff << field.bit_offset;

	rt2x00usb_register_read(rt2x00dev, offset, &reg);
	rt2x00_set_field32(&reg, field, queue->txop);
	rt2x00usb_register_write(rt2x00dev, offset, reg);

	/* Update WMM registers */
	field.bit_offset = queue_idx * 4;
	field.bit_mask = 0xf << field.bit_offset;

	rt2x00usb_register_read(rt2x00dev, AIFSN_CSR, &reg);
	rt2x00_set_field32(&reg, field, queue->aifs);
	rt2x00usb_register_write(rt2x00dev, AIFSN_CSR, reg);

	rt2x00usb_register_read(rt2x00dev, CWMIN_CSR, &reg);
	rt2x00_set_field32(&reg, field, queue->cw_min);
	rt2x00usb_register_write(rt2x00dev, CWMIN_CSR, reg);

	rt2x00usb_register_read(rt2x00dev, CWMAX_CSR, &reg);
	rt2x00_set_field32(&reg, field, queue->cw_max);
	rt2x00usb_register_write(rt2x00dev, CWMAX_CSR, reg);

	return 0;
}

static u64 rt73usb_get_tsf(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	u64 tsf;
	u32 reg;

	rt2x00usb_register_read(rt2x00dev, TXRX_CSR13, &reg);
	tsf = (u64) rt2x00_get_field32(reg, TXRX_CSR13_HIGH_TSFTIMER) << 32;
	rt2x00usb_register_read(rt2x00dev, TXRX_CSR12, &reg);
	tsf |= rt2x00_get_field32(reg, TXRX_CSR12_LOW_TSFTIMER);

	return tsf;
}

static const struct ieee80211_ops rt73usb_mac80211_ops = {
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
	.bss_info_changed	= rt2x00mac_bss_info_changed,
	.conf_tx		= rt73usb_conf_tx,
	.get_tsf		= rt73usb_get_tsf,
	.rfkill_poll		= rt2x00mac_rfkill_poll,
};

static const struct rt2x00lib_ops rt73usb_rt2x00_ops = {
	.probe_hw		= rt73usb_probe_hw,
	.get_firmware_name	= rt73usb_get_firmware_name,
	.check_firmware		= rt73usb_check_firmware,
	.load_firmware		= rt73usb_load_firmware,
	.initialize		= rt2x00usb_initialize,
	.uninitialize		= rt2x00usb_uninitialize,
	.clear_entry		= rt2x00usb_clear_entry,
	.set_device_state	= rt73usb_set_device_state,
	.rfkill_poll		= rt73usb_rfkill_poll,
	.link_stats		= rt73usb_link_stats,
	.reset_tuner		= rt73usb_reset_tuner,
	.link_tuner		= rt73usb_link_tuner,
	.write_tx_desc		= rt73usb_write_tx_desc,
	.write_tx_data		= rt2x00usb_write_tx_data,
	.write_beacon		= rt73usb_write_beacon,
	.get_tx_data_len	= rt73usb_get_tx_data_len,
	.kick_tx_queue		= rt73usb_kick_tx_queue,
	.kill_tx_queue		= rt2x00usb_kill_tx_queue,
	.fill_rxdone		= rt73usb_fill_rxdone,
	.config_shared_key	= rt73usb_config_shared_key,
	.config_pairwise_key	= rt73usb_config_pairwise_key,
	.config_filter		= rt73usb_config_filter,
	.config_intf		= rt73usb_config_intf,
	.config_erp		= rt73usb_config_erp,
	.config_ant		= rt73usb_config_ant,
	.config			= rt73usb_config,
};

static const struct data_queue_desc rt73usb_queue_rx = {
	.entry_num		= RX_ENTRIES,
	.data_size		= DATA_FRAME_SIZE,
	.desc_size		= RXD_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_usb),
};

static const struct data_queue_desc rt73usb_queue_tx = {
	.entry_num		= TX_ENTRIES,
	.data_size		= DATA_FRAME_SIZE,
	.desc_size		= TXD_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_usb),
};

static const struct data_queue_desc rt73usb_queue_bcn = {
	.entry_num		= 4 * BEACON_ENTRIES,
	.data_size		= MGMT_FRAME_SIZE,
	.desc_size		= TXINFO_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_usb),
};

static const struct rt2x00_ops rt73usb_ops = {
	.name			= KBUILD_MODNAME,
	.max_sta_intf		= 1,
	.max_ap_intf		= 4,
	.eeprom_size		= EEPROM_SIZE,
	.rf_size		= RF_SIZE,
	.tx_queues		= NUM_TX_QUEUES,
	.extra_tx_headroom	= TXD_DESC_SIZE,
	.rx			= &rt73usb_queue_rx,
	.tx			= &rt73usb_queue_tx,
	.bcn			= &rt73usb_queue_bcn,
	.lib			= &rt73usb_rt2x00_ops,
	.hw			= &rt73usb_mac80211_ops,
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	.debugfs		= &rt73usb_rt2x00debug,
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */
};

/*
 * rt73usb module information.
 */
static struct usb_device_id rt73usb_device_table[] = {
	/* AboCom */
	{ USB_DEVICE(0x07b8, 0xb21b), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x07b8, 0xb21c), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x07b8, 0xb21d), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x07b8, 0xb21e), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x07b8, 0xb21f), USB_DEVICE_DATA(&rt73usb_ops) },
	/* AL */
	{ USB_DEVICE(0x14b2, 0x3c10), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Amigo */
	{ USB_DEVICE(0x148f, 0x9021), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0eb0, 0x9021), USB_DEVICE_DATA(&rt73usb_ops) },
	/* AMIT  */
	{ USB_DEVICE(0x18c5, 0x0002), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Askey */
	{ USB_DEVICE(0x1690, 0x0722), USB_DEVICE_DATA(&rt73usb_ops) },
	/* ASUS */
	{ USB_DEVICE(0x0b05, 0x1723), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0b05, 0x1724), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Belkin */
	{ USB_DEVICE(0x050d, 0x7050), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x050d, 0x705a), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x050d, 0x905b), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x050d, 0x905c), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Billionton */
	{ USB_DEVICE(0x1631, 0xc019), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x08dd, 0x0120), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Buffalo */
	{ USB_DEVICE(0x0411, 0x00d8), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0411, 0x00d9), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0411, 0x00f4), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0411, 0x0116), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0411, 0x0119), USB_DEVICE_DATA(&rt73usb_ops) },
	/* CNet */
	{ USB_DEVICE(0x1371, 0x9022), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x1371, 0x9032), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Conceptronic */
	{ USB_DEVICE(0x14b2, 0x3c22), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Corega */
	{ USB_DEVICE(0x07aa, 0x002e), USB_DEVICE_DATA(&rt73usb_ops) },
	/* D-Link */
	{ USB_DEVICE(0x07d1, 0x3c03), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c04), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c06), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x07d1, 0x3c07), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Edimax */
	{ USB_DEVICE(0x7392, 0x7318), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x7392, 0x7618), USB_DEVICE_DATA(&rt73usb_ops) },
	/* EnGenius */
	{ USB_DEVICE(0x1740, 0x3701), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Gemtek */
	{ USB_DEVICE(0x15a9, 0x0004), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Gigabyte */
	{ USB_DEVICE(0x1044, 0x8008), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x1044, 0x800a), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Huawei-3Com */
	{ USB_DEVICE(0x1472, 0x0009), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Hercules */
	{ USB_DEVICE(0x06f8, 0xe002), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x06f8, 0xe010), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x06f8, 0xe020), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Linksys */
	{ USB_DEVICE(0x13b1, 0x0020), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x13b1, 0x0023), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x13b1, 0x0028), USB_DEVICE_DATA(&rt73usb_ops) },
	/* MSI */
	{ USB_DEVICE(0x0db0, 0x4600), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0db0, 0x6877), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0db0, 0x6874), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0db0, 0xa861), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0db0, 0xa874), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Ovislink */
	{ USB_DEVICE(0x1b75, 0x7318), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Ralink */
	{ USB_DEVICE(0x04bb, 0x093d), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x148f, 0x2573), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x148f, 0x2671), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Qcom */
	{ USB_DEVICE(0x18e8, 0x6196), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x18e8, 0x6229), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x18e8, 0x6238), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Samsung */
	{ USB_DEVICE(0x04e8, 0x4471), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Senao */
	{ USB_DEVICE(0x1740, 0x7100), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Sitecom */
	{ USB_DEVICE(0x0df6, 0x0024), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0df6, 0x0027), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0df6, 0x002f), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0df6, 0x90ac), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x0df6, 0x9712), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Surecom */
	{ USB_DEVICE(0x0769, 0x31f3), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Tilgin */
	{ USB_DEVICE(0x6933, 0x5001), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Philips */
	{ USB_DEVICE(0x0471, 0x200a), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Planex */
	{ USB_DEVICE(0x2019, 0xab01), USB_DEVICE_DATA(&rt73usb_ops) },
	{ USB_DEVICE(0x2019, 0xab50), USB_DEVICE_DATA(&rt73usb_ops) },
	/* WideTell */
	{ USB_DEVICE(0x7167, 0x3840), USB_DEVICE_DATA(&rt73usb_ops) },
	/* Zcom */
	{ USB_DEVICE(0x0cde, 0x001c), USB_DEVICE_DATA(&rt73usb_ops) },
	/* ZyXEL */
	{ USB_DEVICE(0x0586, 0x3415), USB_DEVICE_DATA(&rt73usb_ops) },
	{ 0, }
};

MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("Ralink RT73 USB Wireless LAN driver.");
MODULE_SUPPORTED_DEVICE("Ralink RT2571W & RT2671 USB chipset based cards");
MODULE_DEVICE_TABLE(usb, rt73usb_device_table);
MODULE_FIRMWARE(FIRMWARE_RT2571);
MODULE_LICENSE("GPL");

static struct usb_driver rt73usb_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= rt73usb_device_table,
	.probe		= rt2x00usb_probe,
	.disconnect	= rt2x00usb_disconnect,
	.suspend	= rt2x00usb_suspend,
	.resume		= rt2x00usb_resume,
};

static int __init rt73usb_init(void)
{
	return usb_register(&rt73usb_driver);
}

static void __exit rt73usb_exit(void)
{
	usb_deregister(&rt73usb_driver);
}

module_init(rt73usb_init);
module_exit(rt73usb_exit);

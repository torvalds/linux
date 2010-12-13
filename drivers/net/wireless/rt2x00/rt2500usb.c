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
	Module: rt2500usb
	Abstract: rt2500usb device specific routines.
	Supported chipsets: RT2570.
 */

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "rt2x00.h"
#include "rt2x00usb.h"
#include "rt2500usb.h"

/*
 * Allow hardware encryption to be disabled.
 */
static int modparam_nohwcrypt;
module_param_named(nohwcrypt, modparam_nohwcrypt, bool, S_IRUGO);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption.");

/*
 * Register access.
 * All access to the CSR registers will go through the methods
 * rt2500usb_register_read and rt2500usb_register_write.
 * BBP and RF register require indirect register access,
 * and use the CSR registers BBPCSR and RFCSR to achieve this.
 * These indirect registers work with busy bits,
 * and we will try maximal REGISTER_BUSY_COUNT times to access
 * the register while taking a REGISTER_BUSY_DELAY us delay
 * between each attampt. When the busy bit is still set at that time,
 * the access attempt is considered to have failed,
 * and we will print an error.
 * If the csr_mutex is already held then the _lock variants must
 * be used instead.
 */
static inline void rt2500usb_register_read(struct rt2x00_dev *rt2x00dev,
					   const unsigned int offset,
					   u16 *value)
{
	__le16 reg;
	rt2x00usb_vendor_request_buff(rt2x00dev, USB_MULTI_READ,
				      USB_VENDOR_REQUEST_IN, offset,
				      &reg, sizeof(reg), REGISTER_TIMEOUT);
	*value = le16_to_cpu(reg);
}

static inline void rt2500usb_register_read_lock(struct rt2x00_dev *rt2x00dev,
						const unsigned int offset,
						u16 *value)
{
	__le16 reg;
	rt2x00usb_vendor_req_buff_lock(rt2x00dev, USB_MULTI_READ,
				       USB_VENDOR_REQUEST_IN, offset,
				       &reg, sizeof(reg), REGISTER_TIMEOUT);
	*value = le16_to_cpu(reg);
}

static inline void rt2500usb_register_multiread(struct rt2x00_dev *rt2x00dev,
						const unsigned int offset,
						void *value, const u16 length)
{
	rt2x00usb_vendor_request_buff(rt2x00dev, USB_MULTI_READ,
				      USB_VENDOR_REQUEST_IN, offset,
				      value, length,
				      REGISTER_TIMEOUT16(length));
}

static inline void rt2500usb_register_write(struct rt2x00_dev *rt2x00dev,
					    const unsigned int offset,
					    u16 value)
{
	__le16 reg = cpu_to_le16(value);
	rt2x00usb_vendor_request_buff(rt2x00dev, USB_MULTI_WRITE,
				      USB_VENDOR_REQUEST_OUT, offset,
				      &reg, sizeof(reg), REGISTER_TIMEOUT);
}

static inline void rt2500usb_register_write_lock(struct rt2x00_dev *rt2x00dev,
						 const unsigned int offset,
						 u16 value)
{
	__le16 reg = cpu_to_le16(value);
	rt2x00usb_vendor_req_buff_lock(rt2x00dev, USB_MULTI_WRITE,
				       USB_VENDOR_REQUEST_OUT, offset,
				       &reg, sizeof(reg), REGISTER_TIMEOUT);
}

static inline void rt2500usb_register_multiwrite(struct rt2x00_dev *rt2x00dev,
						 const unsigned int offset,
						 void *value, const u16 length)
{
	rt2x00usb_vendor_request_buff(rt2x00dev, USB_MULTI_WRITE,
				      USB_VENDOR_REQUEST_OUT, offset,
				      value, length,
				      REGISTER_TIMEOUT16(length));
}

static int rt2500usb_regbusy_read(struct rt2x00_dev *rt2x00dev,
				  const unsigned int offset,
				  struct rt2x00_field16 field,
				  u16 *reg)
{
	unsigned int i;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2500usb_register_read_lock(rt2x00dev, offset, reg);
		if (!rt2x00_get_field16(*reg, field))
			return 1;
		udelay(REGISTER_BUSY_DELAY);
	}

	ERROR(rt2x00dev, "Indirect register access failed: "
	      "offset=0x%.08x, value=0x%.08x\n", offset, *reg);
	*reg = ~0;

	return 0;
}

#define WAIT_FOR_BBP(__dev, __reg) \
	rt2500usb_regbusy_read((__dev), PHY_CSR8, PHY_CSR8_BUSY, (__reg))
#define WAIT_FOR_RF(__dev, __reg) \
	rt2500usb_regbusy_read((__dev), PHY_CSR10, PHY_CSR10_RF_BUSY, (__reg))

static void rt2500usb_bbp_write(struct rt2x00_dev *rt2x00dev,
				const unsigned int word, const u8 value)
{
	u16 reg;

	mutex_lock(&rt2x00dev->csr_mutex);

	/*
	 * Wait until the BBP becomes available, afterwards we
	 * can safely write the new data into the register.
	 */
	if (WAIT_FOR_BBP(rt2x00dev, &reg)) {
		reg = 0;
		rt2x00_set_field16(&reg, PHY_CSR7_DATA, value);
		rt2x00_set_field16(&reg, PHY_CSR7_REG_ID, word);
		rt2x00_set_field16(&reg, PHY_CSR7_READ_CONTROL, 0);

		rt2500usb_register_write_lock(rt2x00dev, PHY_CSR7, reg);
	}

	mutex_unlock(&rt2x00dev->csr_mutex);
}

static void rt2500usb_bbp_read(struct rt2x00_dev *rt2x00dev,
			       const unsigned int word, u8 *value)
{
	u16 reg;

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
		rt2x00_set_field16(&reg, PHY_CSR7_REG_ID, word);
		rt2x00_set_field16(&reg, PHY_CSR7_READ_CONTROL, 1);

		rt2500usb_register_write_lock(rt2x00dev, PHY_CSR7, reg);

		if (WAIT_FOR_BBP(rt2x00dev, &reg))
			rt2500usb_register_read_lock(rt2x00dev, PHY_CSR7, &reg);
	}

	*value = rt2x00_get_field16(reg, PHY_CSR7_DATA);

	mutex_unlock(&rt2x00dev->csr_mutex);
}

static void rt2500usb_rf_write(struct rt2x00_dev *rt2x00dev,
			       const unsigned int word, const u32 value)
{
	u16 reg;

	mutex_lock(&rt2x00dev->csr_mutex);

	/*
	 * Wait until the RF becomes available, afterwards we
	 * can safely write the new data into the register.
	 */
	if (WAIT_FOR_RF(rt2x00dev, &reg)) {
		reg = 0;
		rt2x00_set_field16(&reg, PHY_CSR9_RF_VALUE, value);
		rt2500usb_register_write_lock(rt2x00dev, PHY_CSR9, reg);

		reg = 0;
		rt2x00_set_field16(&reg, PHY_CSR10_RF_VALUE, value >> 16);
		rt2x00_set_field16(&reg, PHY_CSR10_RF_NUMBER_OF_BITS, 20);
		rt2x00_set_field16(&reg, PHY_CSR10_RF_IF_SELECT, 0);
		rt2x00_set_field16(&reg, PHY_CSR10_RF_BUSY, 1);

		rt2500usb_register_write_lock(rt2x00dev, PHY_CSR10, reg);
		rt2x00_rf_write(rt2x00dev, word, value);
	}

	mutex_unlock(&rt2x00dev->csr_mutex);
}

#ifdef CONFIG_RT2X00_LIB_DEBUGFS
static void _rt2500usb_register_read(struct rt2x00_dev *rt2x00dev,
				     const unsigned int offset,
				     u32 *value)
{
	rt2500usb_register_read(rt2x00dev, offset, (u16 *)value);
}

static void _rt2500usb_register_write(struct rt2x00_dev *rt2x00dev,
				      const unsigned int offset,
				      u32 value)
{
	rt2500usb_register_write(rt2x00dev, offset, value);
}

static const struct rt2x00debug rt2500usb_rt2x00debug = {
	.owner	= THIS_MODULE,
	.csr	= {
		.read		= _rt2500usb_register_read,
		.write		= _rt2500usb_register_write,
		.flags		= RT2X00DEBUGFS_OFFSET,
		.word_base	= CSR_REG_BASE,
		.word_size	= sizeof(u16),
		.word_count	= CSR_REG_SIZE / sizeof(u16),
	},
	.eeprom	= {
		.read		= rt2x00_eeprom_read,
		.write		= rt2x00_eeprom_write,
		.word_base	= EEPROM_BASE,
		.word_size	= sizeof(u16),
		.word_count	= EEPROM_SIZE / sizeof(u16),
	},
	.bbp	= {
		.read		= rt2500usb_bbp_read,
		.write		= rt2500usb_bbp_write,
		.word_base	= BBP_BASE,
		.word_size	= sizeof(u8),
		.word_count	= BBP_SIZE / sizeof(u8),
	},
	.rf	= {
		.read		= rt2x00_rf_read,
		.write		= rt2500usb_rf_write,
		.word_base	= RF_BASE,
		.word_size	= sizeof(u32),
		.word_count	= RF_SIZE / sizeof(u32),
	},
};
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */

static int rt2500usb_rfkill_poll(struct rt2x00_dev *rt2x00dev)
{
	u16 reg;

	rt2500usb_register_read(rt2x00dev, MAC_CSR19, &reg);
	return rt2x00_get_field32(reg, MAC_CSR19_BIT7);
}

#ifdef CONFIG_RT2X00_LIB_LEDS
static void rt2500usb_brightness_set(struct led_classdev *led_cdev,
				     enum led_brightness brightness)
{
	struct rt2x00_led *led =
	    container_of(led_cdev, struct rt2x00_led, led_dev);
	unsigned int enabled = brightness != LED_OFF;
	u16 reg;

	rt2500usb_register_read(led->rt2x00dev, MAC_CSR20, &reg);

	if (led->type == LED_TYPE_RADIO || led->type == LED_TYPE_ASSOC)
		rt2x00_set_field16(&reg, MAC_CSR20_LINK, enabled);
	else if (led->type == LED_TYPE_ACTIVITY)
		rt2x00_set_field16(&reg, MAC_CSR20_ACTIVITY, enabled);

	rt2500usb_register_write(led->rt2x00dev, MAC_CSR20, reg);
}

static int rt2500usb_blink_set(struct led_classdev *led_cdev,
			       unsigned long *delay_on,
			       unsigned long *delay_off)
{
	struct rt2x00_led *led =
	    container_of(led_cdev, struct rt2x00_led, led_dev);
	u16 reg;

	rt2500usb_register_read(led->rt2x00dev, MAC_CSR21, &reg);
	rt2x00_set_field16(&reg, MAC_CSR21_ON_PERIOD, *delay_on);
	rt2x00_set_field16(&reg, MAC_CSR21_OFF_PERIOD, *delay_off);
	rt2500usb_register_write(led->rt2x00dev, MAC_CSR21, reg);

	return 0;
}

static void rt2500usb_init_led(struct rt2x00_dev *rt2x00dev,
			       struct rt2x00_led *led,
			       enum led_type type)
{
	led->rt2x00dev = rt2x00dev;
	led->type = type;
	led->led_dev.brightness_set = rt2500usb_brightness_set;
	led->led_dev.blink_set = rt2500usb_blink_set;
	led->flags = LED_INITIALIZED;
}
#endif /* CONFIG_RT2X00_LIB_LEDS */

/*
 * Configuration handlers.
 */

/*
 * rt2500usb does not differentiate between shared and pairwise
 * keys, so we should use the same function for both key types.
 */
static int rt2500usb_config_key(struct rt2x00_dev *rt2x00dev,
				struct rt2x00lib_crypto *crypto,
				struct ieee80211_key_conf *key)
{
	u32 mask;
	u16 reg;
	enum cipher curr_cipher;

	if (crypto->cmd == SET_KEY) {
		/*
		 * Disallow to set WEP key other than with index 0,
		 * it is known that not work at least on some hardware.
		 * SW crypto will be used in that case.
		 */
		if ((key->cipher == WLAN_CIPHER_SUITE_WEP40 ||
		     key->cipher == WLAN_CIPHER_SUITE_WEP104) &&
		    key->keyidx != 0)
			return -EOPNOTSUPP;

		/*
		 * Pairwise key will always be entry 0, but this
		 * could collide with a shared key on the same
		 * position...
		 */
		mask = TXRX_CSR0_KEY_ID.bit_mask;

		rt2500usb_register_read(rt2x00dev, TXRX_CSR0, &reg);
		curr_cipher = rt2x00_get_field16(reg, TXRX_CSR0_ALGORITHM);
		reg &= mask;

		if (reg && reg == mask)
			return -ENOSPC;

		reg = rt2x00_get_field16(reg, TXRX_CSR0_KEY_ID);

		key->hw_key_idx += reg ? ffz(reg) : 0;
		/*
		 * Hardware requires that all keys use the same cipher
		 * (e.g. TKIP-only, AES-only, but not TKIP+AES).
		 * If this is not the first key, compare the cipher with the
		 * first one and fall back to SW crypto if not the same.
		 */
		if (key->hw_key_idx > 0 && crypto->cipher != curr_cipher)
			return -EOPNOTSUPP;

		rt2500usb_register_multiwrite(rt2x00dev, KEY_ENTRY(key->hw_key_idx),
					      crypto->key, sizeof(crypto->key));

		/*
		 * The driver does not support the IV/EIV generation
		 * in hardware. However it demands the data to be provided
		 * both separately as well as inside the frame.
		 * We already provided the CONFIG_CRYPTO_COPY_IV to rt2x00lib
		 * to ensure rt2x00lib will not strip the data from the
		 * frame after the copy, now we must tell mac80211
		 * to generate the IV/EIV data.
		 */
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
	}

	/*
	 * TXRX_CSR0_KEY_ID contains only single-bit fields to indicate
	 * a particular key is valid.
	 */
	rt2500usb_register_read(rt2x00dev, TXRX_CSR0, &reg);
	rt2x00_set_field16(&reg, TXRX_CSR0_ALGORITHM, crypto->cipher);
	rt2x00_set_field16(&reg, TXRX_CSR0_IV_OFFSET, IEEE80211_HEADER);

	mask = rt2x00_get_field16(reg, TXRX_CSR0_KEY_ID);
	if (crypto->cmd == SET_KEY)
		mask |= 1 << key->hw_key_idx;
	else if (crypto->cmd == DISABLE_KEY)
		mask &= ~(1 << key->hw_key_idx);
	rt2x00_set_field16(&reg, TXRX_CSR0_KEY_ID, mask);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR0, reg);

	return 0;
}

static void rt2500usb_config_filter(struct rt2x00_dev *rt2x00dev,
				    const unsigned int filter_flags)
{
	u16 reg;

	/*
	 * Start configuration steps.
	 * Note that the version error will always be dropped
	 * and broadcast frames will always be accepted since
	 * there is no filter for it at this time.
	 */
	rt2500usb_register_read(rt2x00dev, TXRX_CSR2, &reg);
	rt2x00_set_field16(&reg, TXRX_CSR2_DROP_CRC,
			   !(filter_flags & FIF_FCSFAIL));
	rt2x00_set_field16(&reg, TXRX_CSR2_DROP_PHYSICAL,
			   !(filter_flags & FIF_PLCPFAIL));
	rt2x00_set_field16(&reg, TXRX_CSR2_DROP_CONTROL,
			   !(filter_flags & FIF_CONTROL));
	rt2x00_set_field16(&reg, TXRX_CSR2_DROP_NOT_TO_ME,
			   !(filter_flags & FIF_PROMISC_IN_BSS));
	rt2x00_set_field16(&reg, TXRX_CSR2_DROP_TODS,
			   !(filter_flags & FIF_PROMISC_IN_BSS) &&
			   !rt2x00dev->intf_ap_count);
	rt2x00_set_field16(&reg, TXRX_CSR2_DROP_VERSION_ERROR, 1);
	rt2x00_set_field16(&reg, TXRX_CSR2_DROP_MULTICAST,
			   !(filter_flags & FIF_ALLMULTI));
	rt2x00_set_field16(&reg, TXRX_CSR2_DROP_BROADCAST, 0);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR2, reg);
}

static void rt2500usb_config_intf(struct rt2x00_dev *rt2x00dev,
				  struct rt2x00_intf *intf,
				  struct rt2x00intf_conf *conf,
				  const unsigned int flags)
{
	unsigned int bcn_preload;
	u16 reg;

	if (flags & CONFIG_UPDATE_TYPE) {
		/*
		 * Enable beacon config
		 */
		bcn_preload = PREAMBLE + GET_DURATION(IEEE80211_HEADER, 20);
		rt2500usb_register_read(rt2x00dev, TXRX_CSR20, &reg);
		rt2x00_set_field16(&reg, TXRX_CSR20_OFFSET, bcn_preload >> 6);
		rt2x00_set_field16(&reg, TXRX_CSR20_BCN_EXPECT_WINDOW,
				   2 * (conf->type != NL80211_IFTYPE_STATION));
		rt2500usb_register_write(rt2x00dev, TXRX_CSR20, reg);

		/*
		 * Enable synchronisation.
		 */
		rt2500usb_register_read(rt2x00dev, TXRX_CSR18, &reg);
		rt2x00_set_field16(&reg, TXRX_CSR18_OFFSET, 0);
		rt2500usb_register_write(rt2x00dev, TXRX_CSR18, reg);

		rt2500usb_register_read(rt2x00dev, TXRX_CSR19, &reg);
		rt2x00_set_field16(&reg, TXRX_CSR19_TSF_COUNT, 1);
		rt2x00_set_field16(&reg, TXRX_CSR19_TSF_SYNC, conf->sync);
		rt2x00_set_field16(&reg, TXRX_CSR19_TBCN, 1);
		rt2500usb_register_write(rt2x00dev, TXRX_CSR19, reg);
	}

	if (flags & CONFIG_UPDATE_MAC)
		rt2500usb_register_multiwrite(rt2x00dev, MAC_CSR2, conf->mac,
					      (3 * sizeof(__le16)));

	if (flags & CONFIG_UPDATE_BSSID)
		rt2500usb_register_multiwrite(rt2x00dev, MAC_CSR5, conf->bssid,
					      (3 * sizeof(__le16)));
}

static void rt2500usb_config_erp(struct rt2x00_dev *rt2x00dev,
				 struct rt2x00lib_erp *erp,
				 u32 changed)
{
	u16 reg;

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		rt2500usb_register_read(rt2x00dev, TXRX_CSR10, &reg);
		rt2x00_set_field16(&reg, TXRX_CSR10_AUTORESPOND_PREAMBLE,
				   !!erp->short_preamble);
		rt2500usb_register_write(rt2x00dev, TXRX_CSR10, reg);
	}

	if (changed & BSS_CHANGED_BASIC_RATES)
		rt2500usb_register_write(rt2x00dev, TXRX_CSR11,
					 erp->basic_rates);

	if (changed & BSS_CHANGED_BEACON_INT) {
		rt2500usb_register_read(rt2x00dev, TXRX_CSR18, &reg);
		rt2x00_set_field16(&reg, TXRX_CSR18_INTERVAL,
				   erp->beacon_int * 4);
		rt2500usb_register_write(rt2x00dev, TXRX_CSR18, reg);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		rt2500usb_register_write(rt2x00dev, MAC_CSR10, erp->slot_time);
		rt2500usb_register_write(rt2x00dev, MAC_CSR11, erp->sifs);
		rt2500usb_register_write(rt2x00dev, MAC_CSR12, erp->eifs);
	}
}

static void rt2500usb_config_ant(struct rt2x00_dev *rt2x00dev,
				 struct antenna_setup *ant)
{
	u8 r2;
	u8 r14;
	u16 csr5;
	u16 csr6;

	/*
	 * We should never come here because rt2x00lib is supposed
	 * to catch this and send us the correct antenna explicitely.
	 */
	BUG_ON(ant->rx == ANTENNA_SW_DIVERSITY ||
	       ant->tx == ANTENNA_SW_DIVERSITY);

	rt2500usb_bbp_read(rt2x00dev, 2, &r2);
	rt2500usb_bbp_read(rt2x00dev, 14, &r14);
	rt2500usb_register_read(rt2x00dev, PHY_CSR5, &csr5);
	rt2500usb_register_read(rt2x00dev, PHY_CSR6, &csr6);

	/*
	 * Configure the TX antenna.
	 */
	switch (ant->tx) {
	case ANTENNA_HW_DIVERSITY:
		rt2x00_set_field8(&r2, BBP_R2_TX_ANTENNA, 1);
		rt2x00_set_field16(&csr5, PHY_CSR5_CCK, 1);
		rt2x00_set_field16(&csr6, PHY_CSR6_OFDM, 1);
		break;
	case ANTENNA_A:
		rt2x00_set_field8(&r2, BBP_R2_TX_ANTENNA, 0);
		rt2x00_set_field16(&csr5, PHY_CSR5_CCK, 0);
		rt2x00_set_field16(&csr6, PHY_CSR6_OFDM, 0);
		break;
	case ANTENNA_B:
	default:
		rt2x00_set_field8(&r2, BBP_R2_TX_ANTENNA, 2);
		rt2x00_set_field16(&csr5, PHY_CSR5_CCK, 2);
		rt2x00_set_field16(&csr6, PHY_CSR6_OFDM, 2);
		break;
	}

	/*
	 * Configure the RX antenna.
	 */
	switch (ant->rx) {
	case ANTENNA_HW_DIVERSITY:
		rt2x00_set_field8(&r14, BBP_R14_RX_ANTENNA, 1);
		break;
	case ANTENNA_A:
		rt2x00_set_field8(&r14, BBP_R14_RX_ANTENNA, 0);
		break;
	case ANTENNA_B:
	default:
		rt2x00_set_field8(&r14, BBP_R14_RX_ANTENNA, 2);
		break;
	}

	/*
	 * RT2525E and RT5222 need to flip TX I/Q
	 */
	if (rt2x00_rf(rt2x00dev, RF2525E) || rt2x00_rf(rt2x00dev, RF5222)) {
		rt2x00_set_field8(&r2, BBP_R2_TX_IQ_FLIP, 1);
		rt2x00_set_field16(&csr5, PHY_CSR5_CCK_FLIP, 1);
		rt2x00_set_field16(&csr6, PHY_CSR6_OFDM_FLIP, 1);

		/*
		 * RT2525E does not need RX I/Q Flip.
		 */
		if (rt2x00_rf(rt2x00dev, RF2525E))
			rt2x00_set_field8(&r14, BBP_R14_RX_IQ_FLIP, 0);
	} else {
		rt2x00_set_field16(&csr5, PHY_CSR5_CCK_FLIP, 0);
		rt2x00_set_field16(&csr6, PHY_CSR6_OFDM_FLIP, 0);
	}

	rt2500usb_bbp_write(rt2x00dev, 2, r2);
	rt2500usb_bbp_write(rt2x00dev, 14, r14);
	rt2500usb_register_write(rt2x00dev, PHY_CSR5, csr5);
	rt2500usb_register_write(rt2x00dev, PHY_CSR6, csr6);
}

static void rt2500usb_config_channel(struct rt2x00_dev *rt2x00dev,
				     struct rf_channel *rf, const int txpower)
{
	/*
	 * Set TXpower.
	 */
	rt2x00_set_field32(&rf->rf3, RF3_TXPOWER, TXPOWER_TO_DEV(txpower));

	/*
	 * For RT2525E we should first set the channel to half band higher.
	 */
	if (rt2x00_rf(rt2x00dev, RF2525E)) {
		static const u32 vals[] = {
			0x000008aa, 0x000008ae, 0x000008ae, 0x000008b2,
			0x000008b2, 0x000008b6, 0x000008b6, 0x000008ba,
			0x000008ba, 0x000008be, 0x000008b7, 0x00000902,
			0x00000902, 0x00000906
		};

		rt2500usb_rf_write(rt2x00dev, 2, vals[rf->channel - 1]);
		if (rf->rf4)
			rt2500usb_rf_write(rt2x00dev, 4, rf->rf4);
	}

	rt2500usb_rf_write(rt2x00dev, 1, rf->rf1);
	rt2500usb_rf_write(rt2x00dev, 2, rf->rf2);
	rt2500usb_rf_write(rt2x00dev, 3, rf->rf3);
	if (rf->rf4)
		rt2500usb_rf_write(rt2x00dev, 4, rf->rf4);
}

static void rt2500usb_config_txpower(struct rt2x00_dev *rt2x00dev,
				     const int txpower)
{
	u32 rf3;

	rt2x00_rf_read(rt2x00dev, 3, &rf3);
	rt2x00_set_field32(&rf3, RF3_TXPOWER, TXPOWER_TO_DEV(txpower));
	rt2500usb_rf_write(rt2x00dev, 3, rf3);
}

static void rt2500usb_config_ps(struct rt2x00_dev *rt2x00dev,
				struct rt2x00lib_conf *libconf)
{
	enum dev_state state =
	    (libconf->conf->flags & IEEE80211_CONF_PS) ?
		STATE_SLEEP : STATE_AWAKE;
	u16 reg;

	if (state == STATE_SLEEP) {
		rt2500usb_register_read(rt2x00dev, MAC_CSR18, &reg);
		rt2x00_set_field16(&reg, MAC_CSR18_DELAY_AFTER_BEACON,
				   rt2x00dev->beacon_int - 20);
		rt2x00_set_field16(&reg, MAC_CSR18_BEACONS_BEFORE_WAKEUP,
				   libconf->conf->listen_interval - 1);

		/* We must first disable autowake before it can be enabled */
		rt2x00_set_field16(&reg, MAC_CSR18_AUTO_WAKE, 0);
		rt2500usb_register_write(rt2x00dev, MAC_CSR18, reg);

		rt2x00_set_field16(&reg, MAC_CSR18_AUTO_WAKE, 1);
		rt2500usb_register_write(rt2x00dev, MAC_CSR18, reg);
	} else {
		rt2500usb_register_read(rt2x00dev, MAC_CSR18, &reg);
		rt2x00_set_field16(&reg, MAC_CSR18_AUTO_WAKE, 0);
		rt2500usb_register_write(rt2x00dev, MAC_CSR18, reg);
	}

	rt2x00dev->ops->lib->set_device_state(rt2x00dev, state);
}

static void rt2500usb_config(struct rt2x00_dev *rt2x00dev,
			     struct rt2x00lib_conf *libconf,
			     const unsigned int flags)
{
	if (flags & IEEE80211_CONF_CHANGE_CHANNEL)
		rt2500usb_config_channel(rt2x00dev, &libconf->rf,
					 libconf->conf->power_level);
	if ((flags & IEEE80211_CONF_CHANGE_POWER) &&
	    !(flags & IEEE80211_CONF_CHANGE_CHANNEL))
		rt2500usb_config_txpower(rt2x00dev,
					 libconf->conf->power_level);
	if (flags & IEEE80211_CONF_CHANGE_PS)
		rt2500usb_config_ps(rt2x00dev, libconf);
}

/*
 * Link tuning
 */
static void rt2500usb_link_stats(struct rt2x00_dev *rt2x00dev,
				 struct link_qual *qual)
{
	u16 reg;

	/*
	 * Update FCS error count from register.
	 */
	rt2500usb_register_read(rt2x00dev, STA_CSR0, &reg);
	qual->rx_failed = rt2x00_get_field16(reg, STA_CSR0_FCS_ERROR);

	/*
	 * Update False CCA count from register.
	 */
	rt2500usb_register_read(rt2x00dev, STA_CSR3, &reg);
	qual->false_cca = rt2x00_get_field16(reg, STA_CSR3_FALSE_CCA_ERROR);
}

static void rt2500usb_reset_tuner(struct rt2x00_dev *rt2x00dev,
				  struct link_qual *qual)
{
	u16 eeprom;
	u16 value;

	rt2x00_eeprom_read(rt2x00dev, EEPROM_BBPTUNE_R24, &eeprom);
	value = rt2x00_get_field16(eeprom, EEPROM_BBPTUNE_R24_LOW);
	rt2500usb_bbp_write(rt2x00dev, 24, value);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_BBPTUNE_R25, &eeprom);
	value = rt2x00_get_field16(eeprom, EEPROM_BBPTUNE_R25_LOW);
	rt2500usb_bbp_write(rt2x00dev, 25, value);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_BBPTUNE_R61, &eeprom);
	value = rt2x00_get_field16(eeprom, EEPROM_BBPTUNE_R61_LOW);
	rt2500usb_bbp_write(rt2x00dev, 61, value);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_BBPTUNE_VGC, &eeprom);
	value = rt2x00_get_field16(eeprom, EEPROM_BBPTUNE_VGCUPPER);
	rt2500usb_bbp_write(rt2x00dev, 17, value);

	qual->vgc_level = value;
}

/*
 * Queue handlers.
 */
static void rt2500usb_start_queue(struct data_queue *queue)
{
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	u16 reg;

	switch (queue->qid) {
	case QID_RX:
		rt2500usb_register_read(rt2x00dev, TXRX_CSR2, &reg);
		rt2x00_set_field16(&reg, TXRX_CSR2_DISABLE_RX, 0);
		rt2500usb_register_write(rt2x00dev, TXRX_CSR2, reg);
		break;
	case QID_BEACON:
		rt2500usb_register_read(rt2x00dev, TXRX_CSR19, &reg);
		rt2x00_set_field16(&reg, TXRX_CSR19_TSF_COUNT, 1);
		rt2x00_set_field16(&reg, TXRX_CSR19_TBCN, 1);
		rt2x00_set_field16(&reg, TXRX_CSR19_BEACON_GEN, 1);
		rt2500usb_register_write(rt2x00dev, TXRX_CSR19, reg);
		break;
	default:
		break;
	}
}

static void rt2500usb_stop_queue(struct data_queue *queue)
{
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	u16 reg;

	switch (queue->qid) {
	case QID_RX:
		rt2500usb_register_read(rt2x00dev, TXRX_CSR2, &reg);
		rt2x00_set_field16(&reg, TXRX_CSR2_DISABLE_RX, 1);
		rt2500usb_register_write(rt2x00dev, TXRX_CSR2, reg);
		break;
	case QID_BEACON:
		rt2500usb_register_read(rt2x00dev, TXRX_CSR19, &reg);
		rt2x00_set_field16(&reg, TXRX_CSR19_TSF_COUNT, 0);
		rt2x00_set_field16(&reg, TXRX_CSR19_TBCN, 0);
		rt2x00_set_field16(&reg, TXRX_CSR19_BEACON_GEN, 0);
		rt2500usb_register_write(rt2x00dev, TXRX_CSR19, reg);
		break;
	default:
		break;
	}

	rt2x00usb_stop_queue(queue);
}

/*
 * Initialization functions.
 */
static int rt2500usb_init_registers(struct rt2x00_dev *rt2x00dev)
{
	u16 reg;

	rt2x00usb_vendor_request_sw(rt2x00dev, USB_DEVICE_MODE, 0x0001,
				    USB_MODE_TEST, REGISTER_TIMEOUT);
	rt2x00usb_vendor_request_sw(rt2x00dev, USB_SINGLE_WRITE, 0x0308,
				    0x00f0, REGISTER_TIMEOUT);

	rt2500usb_register_read(rt2x00dev, TXRX_CSR2, &reg);
	rt2x00_set_field16(&reg, TXRX_CSR2_DISABLE_RX, 1);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR2, reg);

	rt2500usb_register_write(rt2x00dev, MAC_CSR13, 0x1111);
	rt2500usb_register_write(rt2x00dev, MAC_CSR14, 0x1e11);

	rt2500usb_register_read(rt2x00dev, MAC_CSR1, &reg);
	rt2x00_set_field16(&reg, MAC_CSR1_SOFT_RESET, 1);
	rt2x00_set_field16(&reg, MAC_CSR1_BBP_RESET, 1);
	rt2x00_set_field16(&reg, MAC_CSR1_HOST_READY, 0);
	rt2500usb_register_write(rt2x00dev, MAC_CSR1, reg);

	rt2500usb_register_read(rt2x00dev, MAC_CSR1, &reg);
	rt2x00_set_field16(&reg, MAC_CSR1_SOFT_RESET, 0);
	rt2x00_set_field16(&reg, MAC_CSR1_BBP_RESET, 0);
	rt2x00_set_field16(&reg, MAC_CSR1_HOST_READY, 0);
	rt2500usb_register_write(rt2x00dev, MAC_CSR1, reg);

	rt2500usb_register_read(rt2x00dev, TXRX_CSR5, &reg);
	rt2x00_set_field16(&reg, TXRX_CSR5_BBP_ID0, 13);
	rt2x00_set_field16(&reg, TXRX_CSR5_BBP_ID0_VALID, 1);
	rt2x00_set_field16(&reg, TXRX_CSR5_BBP_ID1, 12);
	rt2x00_set_field16(&reg, TXRX_CSR5_BBP_ID1_VALID, 1);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR5, reg);

	rt2500usb_register_read(rt2x00dev, TXRX_CSR6, &reg);
	rt2x00_set_field16(&reg, TXRX_CSR6_BBP_ID0, 10);
	rt2x00_set_field16(&reg, TXRX_CSR6_BBP_ID0_VALID, 1);
	rt2x00_set_field16(&reg, TXRX_CSR6_BBP_ID1, 11);
	rt2x00_set_field16(&reg, TXRX_CSR6_BBP_ID1_VALID, 1);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR6, reg);

	rt2500usb_register_read(rt2x00dev, TXRX_CSR7, &reg);
	rt2x00_set_field16(&reg, TXRX_CSR7_BBP_ID0, 7);
	rt2x00_set_field16(&reg, TXRX_CSR7_BBP_ID0_VALID, 1);
	rt2x00_set_field16(&reg, TXRX_CSR7_BBP_ID1, 6);
	rt2x00_set_field16(&reg, TXRX_CSR7_BBP_ID1_VALID, 1);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR7, reg);

	rt2500usb_register_read(rt2x00dev, TXRX_CSR8, &reg);
	rt2x00_set_field16(&reg, TXRX_CSR8_BBP_ID0, 5);
	rt2x00_set_field16(&reg, TXRX_CSR8_BBP_ID0_VALID, 1);
	rt2x00_set_field16(&reg, TXRX_CSR8_BBP_ID1, 0);
	rt2x00_set_field16(&reg, TXRX_CSR8_BBP_ID1_VALID, 0);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR8, reg);

	rt2500usb_register_read(rt2x00dev, TXRX_CSR19, &reg);
	rt2x00_set_field16(&reg, TXRX_CSR19_TSF_COUNT, 0);
	rt2x00_set_field16(&reg, TXRX_CSR19_TSF_SYNC, 0);
	rt2x00_set_field16(&reg, TXRX_CSR19_TBCN, 0);
	rt2x00_set_field16(&reg, TXRX_CSR19_BEACON_GEN, 0);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR19, reg);

	rt2500usb_register_write(rt2x00dev, TXRX_CSR21, 0xe78f);
	rt2500usb_register_write(rt2x00dev, MAC_CSR9, 0xff1d);

	if (rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_AWAKE))
		return -EBUSY;

	rt2500usb_register_read(rt2x00dev, MAC_CSR1, &reg);
	rt2x00_set_field16(&reg, MAC_CSR1_SOFT_RESET, 0);
	rt2x00_set_field16(&reg, MAC_CSR1_BBP_RESET, 0);
	rt2x00_set_field16(&reg, MAC_CSR1_HOST_READY, 1);
	rt2500usb_register_write(rt2x00dev, MAC_CSR1, reg);

	if (rt2x00_rev(rt2x00dev) >= RT2570_VERSION_C) {
		rt2500usb_register_read(rt2x00dev, PHY_CSR2, &reg);
		rt2x00_set_field16(&reg, PHY_CSR2_LNA, 0);
	} else {
		reg = 0;
		rt2x00_set_field16(&reg, PHY_CSR2_LNA, 1);
		rt2x00_set_field16(&reg, PHY_CSR2_LNA_MODE, 3);
	}
	rt2500usb_register_write(rt2x00dev, PHY_CSR2, reg);

	rt2500usb_register_write(rt2x00dev, MAC_CSR11, 0x0002);
	rt2500usb_register_write(rt2x00dev, MAC_CSR22, 0x0053);
	rt2500usb_register_write(rt2x00dev, MAC_CSR15, 0x01ee);
	rt2500usb_register_write(rt2x00dev, MAC_CSR16, 0x0000);

	rt2500usb_register_read(rt2x00dev, MAC_CSR8, &reg);
	rt2x00_set_field16(&reg, MAC_CSR8_MAX_FRAME_UNIT,
			   rt2x00dev->rx->data_size);
	rt2500usb_register_write(rt2x00dev, MAC_CSR8, reg);

	rt2500usb_register_read(rt2x00dev, TXRX_CSR0, &reg);
	rt2x00_set_field16(&reg, TXRX_CSR0_ALGORITHM, CIPHER_NONE);
	rt2x00_set_field16(&reg, TXRX_CSR0_IV_OFFSET, IEEE80211_HEADER);
	rt2x00_set_field16(&reg, TXRX_CSR0_KEY_ID, 0);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR0, reg);

	rt2500usb_register_read(rt2x00dev, MAC_CSR18, &reg);
	rt2x00_set_field16(&reg, MAC_CSR18_DELAY_AFTER_BEACON, 90);
	rt2500usb_register_write(rt2x00dev, MAC_CSR18, reg);

	rt2500usb_register_read(rt2x00dev, PHY_CSR4, &reg);
	rt2x00_set_field16(&reg, PHY_CSR4_LOW_RF_LE, 1);
	rt2500usb_register_write(rt2x00dev, PHY_CSR4, reg);

	rt2500usb_register_read(rt2x00dev, TXRX_CSR1, &reg);
	rt2x00_set_field16(&reg, TXRX_CSR1_AUTO_SEQUENCE, 1);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR1, reg);

	return 0;
}

static int rt2500usb_wait_bbp_ready(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u8 value;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2500usb_bbp_read(rt2x00dev, 0, &value);
		if ((value != 0xff) && (value != 0x00))
			return 0;
		udelay(REGISTER_BUSY_DELAY);
	}

	ERROR(rt2x00dev, "BBP register access failed, aborting.\n");
	return -EACCES;
}

static int rt2500usb_init_bbp(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u16 eeprom;
	u8 value;
	u8 reg_id;

	if (unlikely(rt2500usb_wait_bbp_ready(rt2x00dev)))
		return -EACCES;

	rt2500usb_bbp_write(rt2x00dev, 3, 0x02);
	rt2500usb_bbp_write(rt2x00dev, 4, 0x19);
	rt2500usb_bbp_write(rt2x00dev, 14, 0x1c);
	rt2500usb_bbp_write(rt2x00dev, 15, 0x30);
	rt2500usb_bbp_write(rt2x00dev, 16, 0xac);
	rt2500usb_bbp_write(rt2x00dev, 18, 0x18);
	rt2500usb_bbp_write(rt2x00dev, 19, 0xff);
	rt2500usb_bbp_write(rt2x00dev, 20, 0x1e);
	rt2500usb_bbp_write(rt2x00dev, 21, 0x08);
	rt2500usb_bbp_write(rt2x00dev, 22, 0x08);
	rt2500usb_bbp_write(rt2x00dev, 23, 0x08);
	rt2500usb_bbp_write(rt2x00dev, 24, 0x80);
	rt2500usb_bbp_write(rt2x00dev, 25, 0x50);
	rt2500usb_bbp_write(rt2x00dev, 26, 0x08);
	rt2500usb_bbp_write(rt2x00dev, 27, 0x23);
	rt2500usb_bbp_write(rt2x00dev, 30, 0x10);
	rt2500usb_bbp_write(rt2x00dev, 31, 0x2b);
	rt2500usb_bbp_write(rt2x00dev, 32, 0xb9);
	rt2500usb_bbp_write(rt2x00dev, 34, 0x12);
	rt2500usb_bbp_write(rt2x00dev, 35, 0x50);
	rt2500usb_bbp_write(rt2x00dev, 39, 0xc4);
	rt2500usb_bbp_write(rt2x00dev, 40, 0x02);
	rt2500usb_bbp_write(rt2x00dev, 41, 0x60);
	rt2500usb_bbp_write(rt2x00dev, 53, 0x10);
	rt2500usb_bbp_write(rt2x00dev, 54, 0x18);
	rt2500usb_bbp_write(rt2x00dev, 56, 0x08);
	rt2500usb_bbp_write(rt2x00dev, 57, 0x10);
	rt2500usb_bbp_write(rt2x00dev, 58, 0x08);
	rt2500usb_bbp_write(rt2x00dev, 61, 0x60);
	rt2500usb_bbp_write(rt2x00dev, 62, 0x10);
	rt2500usb_bbp_write(rt2x00dev, 75, 0xff);

	for (i = 0; i < EEPROM_BBP_SIZE; i++) {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_BBP_START + i, &eeprom);

		if (eeprom != 0xffff && eeprom != 0x0000) {
			reg_id = rt2x00_get_field16(eeprom, EEPROM_BBP_REG_ID);
			value = rt2x00_get_field16(eeprom, EEPROM_BBP_VALUE);
			rt2500usb_bbp_write(rt2x00dev, reg_id, value);
		}
	}

	return 0;
}

/*
 * Device state switch handlers.
 */
static int rt2500usb_enable_radio(struct rt2x00_dev *rt2x00dev)
{
	/*
	 * Initialize all registers.
	 */
	if (unlikely(rt2500usb_init_registers(rt2x00dev) ||
		     rt2500usb_init_bbp(rt2x00dev)))
		return -EIO;

	return 0;
}

static void rt2500usb_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	rt2500usb_register_write(rt2x00dev, MAC_CSR13, 0x2121);
	rt2500usb_register_write(rt2x00dev, MAC_CSR14, 0x2121);

	/*
	 * Disable synchronisation.
	 */
	rt2500usb_register_write(rt2x00dev, TXRX_CSR19, 0);

	rt2x00usb_disable_radio(rt2x00dev);
}

static int rt2500usb_set_state(struct rt2x00_dev *rt2x00dev,
			       enum dev_state state)
{
	u16 reg;
	u16 reg2;
	unsigned int i;
	char put_to_sleep;
	char bbp_state;
	char rf_state;

	put_to_sleep = (state != STATE_AWAKE);

	reg = 0;
	rt2x00_set_field16(&reg, MAC_CSR17_BBP_DESIRE_STATE, state);
	rt2x00_set_field16(&reg, MAC_CSR17_RF_DESIRE_STATE, state);
	rt2x00_set_field16(&reg, MAC_CSR17_PUT_TO_SLEEP, put_to_sleep);
	rt2500usb_register_write(rt2x00dev, MAC_CSR17, reg);
	rt2x00_set_field16(&reg, MAC_CSR17_SET_STATE, 1);
	rt2500usb_register_write(rt2x00dev, MAC_CSR17, reg);

	/*
	 * Device is not guaranteed to be in the requested state yet.
	 * We must wait until the register indicates that the
	 * device has entered the correct state.
	 */
	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2500usb_register_read(rt2x00dev, MAC_CSR17, &reg2);
		bbp_state = rt2x00_get_field16(reg2, MAC_CSR17_BBP_CURR_STATE);
		rf_state = rt2x00_get_field16(reg2, MAC_CSR17_RF_CURR_STATE);
		if (bbp_state == state && rf_state == state)
			return 0;
		rt2500usb_register_write(rt2x00dev, MAC_CSR17, reg);
		msleep(30);
	}

	return -EBUSY;
}

static int rt2500usb_set_device_state(struct rt2x00_dev *rt2x00dev,
				      enum dev_state state)
{
	int retval = 0;

	switch (state) {
	case STATE_RADIO_ON:
		retval = rt2500usb_enable_radio(rt2x00dev);
		break;
	case STATE_RADIO_OFF:
		rt2500usb_disable_radio(rt2x00dev);
		break;
	case STATE_RADIO_RX_ON:
		rt2500usb_start_queue(rt2x00dev->rx);
		break;
	case STATE_RADIO_RX_OFF:
		rt2500usb_stop_queue(rt2x00dev->rx);
		break;
	case STATE_RADIO_IRQ_ON:
	case STATE_RADIO_IRQ_ON_ISR:
	case STATE_RADIO_IRQ_OFF:
	case STATE_RADIO_IRQ_OFF_ISR:
		/* No support, but no error either */
		break;
	case STATE_DEEP_SLEEP:
	case STATE_SLEEP:
	case STATE_STANDBY:
	case STATE_AWAKE:
		retval = rt2500usb_set_state(rt2x00dev, state);
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
static void rt2500usb_write_tx_desc(struct queue_entry *entry,
				    struct txentry_desc *txdesc)
{
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	__le32 *txd = (__le32 *) entry->skb->data;
	u32 word;

	/*
	 * Start writing the descriptor words.
	 */
	rt2x00_desc_read(txd, 0, &word);
	rt2x00_set_field32(&word, TXD_W0_RETRY_LIMIT, txdesc->retry_limit);
	rt2x00_set_field32(&word, TXD_W0_MORE_FRAG,
			   test_bit(ENTRY_TXD_MORE_FRAG, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_ACK,
			   test_bit(ENTRY_TXD_ACK, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_TIMESTAMP,
			   test_bit(ENTRY_TXD_REQ_TIMESTAMP, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_OFDM,
			   (txdesc->rate_mode == RATE_MODE_OFDM));
	rt2x00_set_field32(&word, TXD_W0_NEW_SEQ,
			   test_bit(ENTRY_TXD_FIRST_FRAGMENT, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_IFS, txdesc->ifs);
	rt2x00_set_field32(&word, TXD_W0_DATABYTE_COUNT, txdesc->length);
	rt2x00_set_field32(&word, TXD_W0_CIPHER, !!txdesc->cipher);
	rt2x00_set_field32(&word, TXD_W0_KEY_ID, txdesc->key_idx);
	rt2x00_desc_write(txd, 0, word);

	rt2x00_desc_read(txd, 1, &word);
	rt2x00_set_field32(&word, TXD_W1_IV_OFFSET, txdesc->iv_offset);
	rt2x00_set_field32(&word, TXD_W1_AIFS, entry->queue->aifs);
	rt2x00_set_field32(&word, TXD_W1_CWMIN, entry->queue->cw_min);
	rt2x00_set_field32(&word, TXD_W1_CWMAX, entry->queue->cw_max);
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

	/*
	 * Register descriptor details in skb frame descriptor.
	 */
	skbdesc->flags |= SKBDESC_DESC_IN_SKB;
	skbdesc->desc = txd;
	skbdesc->desc_len = TXD_DESC_SIZE;
}

/*
 * TX data initialization
 */
static void rt2500usb_beacondone(struct urb *urb);

static void rt2500usb_write_beacon(struct queue_entry *entry,
				   struct txentry_desc *txdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct usb_device *usb_dev = to_usb_device_intf(rt2x00dev->dev);
	struct queue_entry_priv_usb_bcn *bcn_priv = entry->priv_data;
	int pipe = usb_sndbulkpipe(usb_dev, entry->queue->usb_endpoint);
	int length;
	u16 reg, reg0;

	/*
	 * Disable beaconing while we are reloading the beacon data,
	 * otherwise we might be sending out invalid data.
	 */
	rt2500usb_register_read(rt2x00dev, TXRX_CSR19, &reg);
	rt2x00_set_field16(&reg, TXRX_CSR19_BEACON_GEN, 0);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR19, reg);

	/*
	 * Add space for the descriptor in front of the skb.
	 */
	skb_push(entry->skb, TXD_DESC_SIZE);
	memset(entry->skb->data, 0, TXD_DESC_SIZE);

	/*
	 * Write the TX descriptor for the beacon.
	 */
	rt2500usb_write_tx_desc(entry, txdesc);

	/*
	 * Dump beacon to userspace through debugfs.
	 */
	rt2x00debug_dump_frame(rt2x00dev, DUMP_FRAME_BEACON, entry->skb);

	/*
	 * USB devices cannot blindly pass the skb->len as the
	 * length of the data to usb_fill_bulk_urb. Pass the skb
	 * to the driver to determine what the length should be.
	 */
	length = rt2x00dev->ops->lib->get_tx_data_len(entry);

	usb_fill_bulk_urb(bcn_priv->urb, usb_dev, pipe,
			  entry->skb->data, length, rt2500usb_beacondone,
			  entry);

	/*
	 * Second we need to create the guardian byte.
	 * We only need a single byte, so lets recycle
	 * the 'flags' field we are not using for beacons.
	 */
	bcn_priv->guardian_data = 0;
	usb_fill_bulk_urb(bcn_priv->guardian_urb, usb_dev, pipe,
			  &bcn_priv->guardian_data, 1, rt2500usb_beacondone,
			  entry);

	/*
	 * Send out the guardian byte.
	 */
	usb_submit_urb(bcn_priv->guardian_urb, GFP_ATOMIC);

	/*
	 * Enable beaconing again.
	 */
	rt2x00_set_field16(&reg, TXRX_CSR19_TSF_COUNT, 1);
	rt2x00_set_field16(&reg, TXRX_CSR19_TBCN, 1);
	reg0 = reg;
	rt2x00_set_field16(&reg, TXRX_CSR19_BEACON_GEN, 1);
	/*
	 * Beacon generation will fail initially.
	 * To prevent this we need to change the TXRX_CSR19
	 * register several times (reg0 is the same as reg
	 * except for TXRX_CSR19_BEACON_GEN, which is 0 in reg0
	 * and 1 in reg).
	 */
	rt2500usb_register_write(rt2x00dev, TXRX_CSR19, reg);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR19, reg0);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR19, reg);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR19, reg0);
	rt2500usb_register_write(rt2x00dev, TXRX_CSR19, reg);
}

static int rt2500usb_get_tx_data_len(struct queue_entry *entry)
{
	int length;

	/*
	 * The length _must_ be a multiple of 2,
	 * but it must _not_ be a multiple of the USB packet size.
	 */
	length = roundup(entry->skb->len, 2);
	length += (2 * !(length % entry->queue->usb_maxpacket));

	return length;
}

/*
 * RX control handlers
 */
static void rt2500usb_fill_rxdone(struct queue_entry *entry,
				  struct rxdone_entry_desc *rxdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct queue_entry_priv_usb *entry_priv = entry->priv_data;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	__le32 *rxd =
	    (__le32 *)(entry->skb->data +
		       (entry_priv->urb->actual_length -
			entry->queue->desc_size));
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
	if (rt2x00_get_field32(word0, RXD_W0_PHYSICAL_ERROR))
		rxdesc->flags |= RX_FLAG_FAILED_PLCP_CRC;

	rxdesc->cipher = rt2x00_get_field32(word0, RXD_W0_CIPHER);
	if (rt2x00_get_field32(word0, RXD_W0_CIPHER_ERROR))
		rxdesc->cipher_status = RX_CRYPTO_FAIL_KEY;

	if (rxdesc->cipher != CIPHER_NONE) {
		_rt2x00_desc_read(rxd, 2, &rxdesc->iv[0]);
		_rt2x00_desc_read(rxd, 3, &rxdesc->iv[1]);
		rxdesc->dev_flags |= RXDONE_CRYPTO_IV;

		/* ICV is located at the end of frame */

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
	rxdesc->rssi =
	    rt2x00_get_field32(word1, RXD_W1_RSSI) - rt2x00dev->rssi_offset;
	rxdesc->size = rt2x00_get_field32(word0, RXD_W0_DATABYTE_COUNT);

	if (rt2x00_get_field32(word0, RXD_W0_OFDM))
		rxdesc->dev_flags |= RXDONE_SIGNAL_PLCP;
	else
		rxdesc->dev_flags |= RXDONE_SIGNAL_BITRATE;
	if (rt2x00_get_field32(word0, RXD_W0_MY_BSS))
		rxdesc->dev_flags |= RXDONE_MY_BSS;

	/*
	 * Adjust the skb memory window to the frame boundaries.
	 */
	skb_trim(entry->skb, rxdesc->size);
}

/*
 * Interrupt functions.
 */
static void rt2500usb_beacondone(struct urb *urb)
{
	struct queue_entry *entry = (struct queue_entry *)urb->context;
	struct queue_entry_priv_usb_bcn *bcn_priv = entry->priv_data;

	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &entry->queue->rt2x00dev->flags))
		return;

	/*
	 * Check if this was the guardian beacon,
	 * if that was the case we need to send the real beacon now.
	 * Otherwise we should free the sk_buffer, the device
	 * should be doing the rest of the work now.
	 */
	if (bcn_priv->guardian_urb == urb) {
		usb_submit_urb(bcn_priv->urb, GFP_ATOMIC);
	} else if (bcn_priv->urb == urb) {
		dev_kfree_skb(entry->skb);
		entry->skb = NULL;
	}
}

/*
 * Device probe functions.
 */
static int rt2500usb_validate_eeprom(struct rt2x00_dev *rt2x00dev)
{
	u16 word;
	u8 *mac;
	u8 bbp;

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
				   ANTENNA_SW_DIVERSITY);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_RX_DEFAULT,
				   ANTENNA_SW_DIVERSITY);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_LED_MODE,
				   LED_MODE_DEFAULT);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_DYN_TXAGC, 0);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_HARDWARE_RADIO, 0);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_RF_TYPE, RF2522);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_ANTENNA, word);
		EEPROM(rt2x00dev, "Antenna: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_NIC_CARDBUS_ACCEL, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_DYN_BBP_TUNE, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CCK_TX_POWER, 0);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_NIC, word);
		EEPROM(rt2x00dev, "NIC: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_CALIBRATE_OFFSET, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_CALIBRATE_OFFSET_RSSI,
				   DEFAULT_RSSI_OFFSET);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_CALIBRATE_OFFSET, word);
		EEPROM(rt2x00dev, "Calibrate offset: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_BBPTUNE, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_BBPTUNE_THRESHOLD, 45);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_BBPTUNE, word);
		EEPROM(rt2x00dev, "BBPtune: 0x%04x\n", word);
	}

	/*
	 * Switch lower vgc bound to current BBP R17 value,
	 * lower the value a bit for better quality.
	 */
	rt2500usb_bbp_read(rt2x00dev, 17, &bbp);
	bbp -= 6;

	rt2x00_eeprom_read(rt2x00dev, EEPROM_BBPTUNE_VGC, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_BBPTUNE_VGCUPPER, 0x40);
		rt2x00_set_field16(&word, EEPROM_BBPTUNE_VGCLOWER, bbp);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_BBPTUNE_VGC, word);
		EEPROM(rt2x00dev, "BBPtune vgc: 0x%04x\n", word);
	} else {
		rt2x00_set_field16(&word, EEPROM_BBPTUNE_VGCLOWER, bbp);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_BBPTUNE_VGC, word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_BBPTUNE_R17, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_BBPTUNE_R17_LOW, 0x48);
		rt2x00_set_field16(&word, EEPROM_BBPTUNE_R17_HIGH, 0x41);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_BBPTUNE_R17, word);
		EEPROM(rt2x00dev, "BBPtune r17: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_BBPTUNE_R24, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_BBPTUNE_R24_LOW, 0x40);
		rt2x00_set_field16(&word, EEPROM_BBPTUNE_R24_HIGH, 0x80);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_BBPTUNE_R24, word);
		EEPROM(rt2x00dev, "BBPtune r24: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_BBPTUNE_R25, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_BBPTUNE_R25_LOW, 0x40);
		rt2x00_set_field16(&word, EEPROM_BBPTUNE_R25_HIGH, 0x50);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_BBPTUNE_R25, word);
		EEPROM(rt2x00dev, "BBPtune r25: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_BBPTUNE_R61, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_BBPTUNE_R61_LOW, 0x60);
		rt2x00_set_field16(&word, EEPROM_BBPTUNE_R61_HIGH, 0x6d);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_BBPTUNE_R61, word);
		EEPROM(rt2x00dev, "BBPtune r61: 0x%04x\n", word);
	}

	return 0;
}

static int rt2500usb_init_eeprom(struct rt2x00_dev *rt2x00dev)
{
	u16 reg;
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
	rt2500usb_register_read(rt2x00dev, MAC_CSR0, &reg);
	rt2x00_set_chip(rt2x00dev, RT2570, value, reg);

	if (((reg & 0xfff0) != 0) || ((reg & 0x0000000f) == 0)) {
		ERROR(rt2x00dev, "Invalid RT chipset detected.\n");
		return -ENODEV;
	}

	if (!rt2x00_rf(rt2x00dev, RF2522) &&
	    !rt2x00_rf(rt2x00dev, RF2523) &&
	    !rt2x00_rf(rt2x00dev, RF2524) &&
	    !rt2x00_rf(rt2x00dev, RF2525) &&
	    !rt2x00_rf(rt2x00dev, RF2525E) &&
	    !rt2x00_rf(rt2x00dev, RF5222)) {
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
	 * When the eeprom indicates SW_DIVERSITY use HW_DIVERSITY instead.
	 * I am not 100% sure about this, but the legacy drivers do not
	 * indicate antenna swapping in software is required when
	 * diversity is enabled.
	 */
	if (rt2x00dev->default_ant.tx == ANTENNA_SW_DIVERSITY)
		rt2x00dev->default_ant.tx = ANTENNA_HW_DIVERSITY;
	if (rt2x00dev->default_ant.rx == ANTENNA_SW_DIVERSITY)
		rt2x00dev->default_ant.rx = ANTENNA_HW_DIVERSITY;

	/*
	 * Store led mode, for correct led behaviour.
	 */
#ifdef CONFIG_RT2X00_LIB_LEDS
	value = rt2x00_get_field16(eeprom, EEPROM_ANTENNA_LED_MODE);

	rt2500usb_init_led(rt2x00dev, &rt2x00dev->led_radio, LED_TYPE_RADIO);
	if (value == LED_MODE_TXRX_ACTIVITY ||
	    value == LED_MODE_DEFAULT ||
	    value == LED_MODE_ASUS)
		rt2500usb_init_led(rt2x00dev, &rt2x00dev->led_qual,
				   LED_TYPE_ACTIVITY);
#endif /* CONFIG_RT2X00_LIB_LEDS */

	/*
	 * Detect if this device has an hardware controlled radio.
	 */
	if (rt2x00_get_field16(eeprom, EEPROM_ANTENNA_HARDWARE_RADIO))
		__set_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags);

	/*
	 * Read the RSSI <-> dBm offset information.
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_CALIBRATE_OFFSET, &eeprom);
	rt2x00dev->rssi_offset =
	    rt2x00_get_field16(eeprom, EEPROM_CALIBRATE_OFFSET_RSSI);

	return 0;
}

/*
 * RF value list for RF2522
 * Supports: 2.4 GHz
 */
static const struct rf_channel rf_vals_bg_2522[] = {
	{ 1,  0x00002050, 0x000c1fda, 0x00000101, 0 },
	{ 2,  0x00002050, 0x000c1fee, 0x00000101, 0 },
	{ 3,  0x00002050, 0x000c2002, 0x00000101, 0 },
	{ 4,  0x00002050, 0x000c2016, 0x00000101, 0 },
	{ 5,  0x00002050, 0x000c202a, 0x00000101, 0 },
	{ 6,  0x00002050, 0x000c203e, 0x00000101, 0 },
	{ 7,  0x00002050, 0x000c2052, 0x00000101, 0 },
	{ 8,  0x00002050, 0x000c2066, 0x00000101, 0 },
	{ 9,  0x00002050, 0x000c207a, 0x00000101, 0 },
	{ 10, 0x00002050, 0x000c208e, 0x00000101, 0 },
	{ 11, 0x00002050, 0x000c20a2, 0x00000101, 0 },
	{ 12, 0x00002050, 0x000c20b6, 0x00000101, 0 },
	{ 13, 0x00002050, 0x000c20ca, 0x00000101, 0 },
	{ 14, 0x00002050, 0x000c20fa, 0x00000101, 0 },
};

/*
 * RF value list for RF2523
 * Supports: 2.4 GHz
 */
static const struct rf_channel rf_vals_bg_2523[] = {
	{ 1,  0x00022010, 0x00000c9e, 0x000e0111, 0x00000a1b },
	{ 2,  0x00022010, 0x00000ca2, 0x000e0111, 0x00000a1b },
	{ 3,  0x00022010, 0x00000ca6, 0x000e0111, 0x00000a1b },
	{ 4,  0x00022010, 0x00000caa, 0x000e0111, 0x00000a1b },
	{ 5,  0x00022010, 0x00000cae, 0x000e0111, 0x00000a1b },
	{ 6,  0x00022010, 0x00000cb2, 0x000e0111, 0x00000a1b },
	{ 7,  0x00022010, 0x00000cb6, 0x000e0111, 0x00000a1b },
	{ 8,  0x00022010, 0x00000cba, 0x000e0111, 0x00000a1b },
	{ 9,  0x00022010, 0x00000cbe, 0x000e0111, 0x00000a1b },
	{ 10, 0x00022010, 0x00000d02, 0x000e0111, 0x00000a1b },
	{ 11, 0x00022010, 0x00000d06, 0x000e0111, 0x00000a1b },
	{ 12, 0x00022010, 0x00000d0a, 0x000e0111, 0x00000a1b },
	{ 13, 0x00022010, 0x00000d0e, 0x000e0111, 0x00000a1b },
	{ 14, 0x00022010, 0x00000d1a, 0x000e0111, 0x00000a03 },
};

/*
 * RF value list for RF2524
 * Supports: 2.4 GHz
 */
static const struct rf_channel rf_vals_bg_2524[] = {
	{ 1,  0x00032020, 0x00000c9e, 0x00000101, 0x00000a1b },
	{ 2,  0x00032020, 0x00000ca2, 0x00000101, 0x00000a1b },
	{ 3,  0x00032020, 0x00000ca6, 0x00000101, 0x00000a1b },
	{ 4,  0x00032020, 0x00000caa, 0x00000101, 0x00000a1b },
	{ 5,  0x00032020, 0x00000cae, 0x00000101, 0x00000a1b },
	{ 6,  0x00032020, 0x00000cb2, 0x00000101, 0x00000a1b },
	{ 7,  0x00032020, 0x00000cb6, 0x00000101, 0x00000a1b },
	{ 8,  0x00032020, 0x00000cba, 0x00000101, 0x00000a1b },
	{ 9,  0x00032020, 0x00000cbe, 0x00000101, 0x00000a1b },
	{ 10, 0x00032020, 0x00000d02, 0x00000101, 0x00000a1b },
	{ 11, 0x00032020, 0x00000d06, 0x00000101, 0x00000a1b },
	{ 12, 0x00032020, 0x00000d0a, 0x00000101, 0x00000a1b },
	{ 13, 0x00032020, 0x00000d0e, 0x00000101, 0x00000a1b },
	{ 14, 0x00032020, 0x00000d1a, 0x00000101, 0x00000a03 },
};

/*
 * RF value list for RF2525
 * Supports: 2.4 GHz
 */
static const struct rf_channel rf_vals_bg_2525[] = {
	{ 1,  0x00022020, 0x00080c9e, 0x00060111, 0x00000a1b },
	{ 2,  0x00022020, 0x00080ca2, 0x00060111, 0x00000a1b },
	{ 3,  0x00022020, 0x00080ca6, 0x00060111, 0x00000a1b },
	{ 4,  0x00022020, 0x00080caa, 0x00060111, 0x00000a1b },
	{ 5,  0x00022020, 0x00080cae, 0x00060111, 0x00000a1b },
	{ 6,  0x00022020, 0x00080cb2, 0x00060111, 0x00000a1b },
	{ 7,  0x00022020, 0x00080cb6, 0x00060111, 0x00000a1b },
	{ 8,  0x00022020, 0x00080cba, 0x00060111, 0x00000a1b },
	{ 9,  0x00022020, 0x00080cbe, 0x00060111, 0x00000a1b },
	{ 10, 0x00022020, 0x00080d02, 0x00060111, 0x00000a1b },
	{ 11, 0x00022020, 0x00080d06, 0x00060111, 0x00000a1b },
	{ 12, 0x00022020, 0x00080d0a, 0x00060111, 0x00000a1b },
	{ 13, 0x00022020, 0x00080d0e, 0x00060111, 0x00000a1b },
	{ 14, 0x00022020, 0x00080d1a, 0x00060111, 0x00000a03 },
};

/*
 * RF value list for RF2525e
 * Supports: 2.4 GHz
 */
static const struct rf_channel rf_vals_bg_2525e[] = {
	{ 1,  0x00022010, 0x0000089a, 0x00060111, 0x00000e1b },
	{ 2,  0x00022010, 0x0000089e, 0x00060111, 0x00000e07 },
	{ 3,  0x00022010, 0x0000089e, 0x00060111, 0x00000e1b },
	{ 4,  0x00022010, 0x000008a2, 0x00060111, 0x00000e07 },
	{ 5,  0x00022010, 0x000008a2, 0x00060111, 0x00000e1b },
	{ 6,  0x00022010, 0x000008a6, 0x00060111, 0x00000e07 },
	{ 7,  0x00022010, 0x000008a6, 0x00060111, 0x00000e1b },
	{ 8,  0x00022010, 0x000008aa, 0x00060111, 0x00000e07 },
	{ 9,  0x00022010, 0x000008aa, 0x00060111, 0x00000e1b },
	{ 10, 0x00022010, 0x000008ae, 0x00060111, 0x00000e07 },
	{ 11, 0x00022010, 0x000008ae, 0x00060111, 0x00000e1b },
	{ 12, 0x00022010, 0x000008b2, 0x00060111, 0x00000e07 },
	{ 13, 0x00022010, 0x000008b2, 0x00060111, 0x00000e1b },
	{ 14, 0x00022010, 0x000008b6, 0x00060111, 0x00000e23 },
};

/*
 * RF value list for RF5222
 * Supports: 2.4 GHz & 5.2 GHz
 */
static const struct rf_channel rf_vals_5222[] = {
	{ 1,  0x00022020, 0x00001136, 0x00000101, 0x00000a0b },
	{ 2,  0x00022020, 0x0000113a, 0x00000101, 0x00000a0b },
	{ 3,  0x00022020, 0x0000113e, 0x00000101, 0x00000a0b },
	{ 4,  0x00022020, 0x00001182, 0x00000101, 0x00000a0b },
	{ 5,  0x00022020, 0x00001186, 0x00000101, 0x00000a0b },
	{ 6,  0x00022020, 0x0000118a, 0x00000101, 0x00000a0b },
	{ 7,  0x00022020, 0x0000118e, 0x00000101, 0x00000a0b },
	{ 8,  0x00022020, 0x00001192, 0x00000101, 0x00000a0b },
	{ 9,  0x00022020, 0x00001196, 0x00000101, 0x00000a0b },
	{ 10, 0x00022020, 0x0000119a, 0x00000101, 0x00000a0b },
	{ 11, 0x00022020, 0x0000119e, 0x00000101, 0x00000a0b },
	{ 12, 0x00022020, 0x000011a2, 0x00000101, 0x00000a0b },
	{ 13, 0x00022020, 0x000011a6, 0x00000101, 0x00000a0b },
	{ 14, 0x00022020, 0x000011ae, 0x00000101, 0x00000a1b },

	/* 802.11 UNI / HyperLan 2 */
	{ 36, 0x00022010, 0x00018896, 0x00000101, 0x00000a1f },
	{ 40, 0x00022010, 0x0001889a, 0x00000101, 0x00000a1f },
	{ 44, 0x00022010, 0x0001889e, 0x00000101, 0x00000a1f },
	{ 48, 0x00022010, 0x000188a2, 0x00000101, 0x00000a1f },
	{ 52, 0x00022010, 0x000188a6, 0x00000101, 0x00000a1f },
	{ 66, 0x00022010, 0x000188aa, 0x00000101, 0x00000a1f },
	{ 60, 0x00022010, 0x000188ae, 0x00000101, 0x00000a1f },
	{ 64, 0x00022010, 0x000188b2, 0x00000101, 0x00000a1f },

	/* 802.11 HyperLan 2 */
	{ 100, 0x00022010, 0x00008802, 0x00000101, 0x00000a0f },
	{ 104, 0x00022010, 0x00008806, 0x00000101, 0x00000a0f },
	{ 108, 0x00022010, 0x0000880a, 0x00000101, 0x00000a0f },
	{ 112, 0x00022010, 0x0000880e, 0x00000101, 0x00000a0f },
	{ 116, 0x00022010, 0x00008812, 0x00000101, 0x00000a0f },
	{ 120, 0x00022010, 0x00008816, 0x00000101, 0x00000a0f },
	{ 124, 0x00022010, 0x0000881a, 0x00000101, 0x00000a0f },
	{ 128, 0x00022010, 0x0000881e, 0x00000101, 0x00000a0f },
	{ 132, 0x00022010, 0x00008822, 0x00000101, 0x00000a0f },
	{ 136, 0x00022010, 0x00008826, 0x00000101, 0x00000a0f },

	/* 802.11 UNII */
	{ 140, 0x00022010, 0x0000882a, 0x00000101, 0x00000a0f },
	{ 149, 0x00022020, 0x000090a6, 0x00000101, 0x00000a07 },
	{ 153, 0x00022020, 0x000090ae, 0x00000101, 0x00000a07 },
	{ 157, 0x00022020, 0x000090b6, 0x00000101, 0x00000a07 },
	{ 161, 0x00022020, 0x000090be, 0x00000101, 0x00000a07 },
};

static int rt2500usb_probe_hw_mode(struct rt2x00_dev *rt2x00dev)
{
	struct hw_mode_spec *spec = &rt2x00dev->spec;
	struct channel_info *info;
	char *tx_power;
	unsigned int i;

	/*
	 * Initialize all hw fields.
	 *
	 * Don't set IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING unless we are
	 * capable of sending the buffered frames out after the DTIM
	 * transmission using rt2x00lib_beacondone. This will send out
	 * multicast and broadcast traffic immediately instead of buffering it
	 * infinitly and thus dropping it after some time.
	 */
	rt2x00dev->hw->flags =
	    IEEE80211_HW_RX_INCLUDES_FCS |
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

	if (rt2x00_rf(rt2x00dev, RF2522)) {
		spec->num_channels = ARRAY_SIZE(rf_vals_bg_2522);
		spec->channels = rf_vals_bg_2522;
	} else if (rt2x00_rf(rt2x00dev, RF2523)) {
		spec->num_channels = ARRAY_SIZE(rf_vals_bg_2523);
		spec->channels = rf_vals_bg_2523;
	} else if (rt2x00_rf(rt2x00dev, RF2524)) {
		spec->num_channels = ARRAY_SIZE(rf_vals_bg_2524);
		spec->channels = rf_vals_bg_2524;
	} else if (rt2x00_rf(rt2x00dev, RF2525)) {
		spec->num_channels = ARRAY_SIZE(rf_vals_bg_2525);
		spec->channels = rf_vals_bg_2525;
	} else if (rt2x00_rf(rt2x00dev, RF2525E)) {
		spec->num_channels = ARRAY_SIZE(rf_vals_bg_2525e);
		spec->channels = rf_vals_bg_2525e;
	} else if (rt2x00_rf(rt2x00dev, RF5222)) {
		spec->supported_bands |= SUPPORT_BAND_5GHZ;
		spec->num_channels = ARRAY_SIZE(rf_vals_5222);
		spec->channels = rf_vals_5222;
	}

	/*
	 * Create channel information array
	 */
	info = kcalloc(spec->num_channels, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	spec->channels_info = info;

	tx_power = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_START);
	for (i = 0; i < 14; i++) {
		info[i].max_power = MAX_TXPOWER;
		info[i].default_power1 = TXPOWER_FROM_DEV(tx_power[i]);
	}

	if (spec->num_channels > 14) {
		for (i = 14; i < spec->num_channels; i++) {
			info[i].max_power = MAX_TXPOWER;
			info[i].default_power1 = DEFAULT_TXPOWER;
		}
	}

	return 0;
}

static int rt2500usb_probe_hw(struct rt2x00_dev *rt2x00dev)
{
	int retval;

	/*
	 * Allocate eeprom data.
	 */
	retval = rt2500usb_validate_eeprom(rt2x00dev);
	if (retval)
		return retval;

	retval = rt2500usb_init_eeprom(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * Initialize hw specifications.
	 */
	retval = rt2500usb_probe_hw_mode(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * This device requires the atim queue
	 */
	__set_bit(DRIVER_REQUIRE_ATIM_QUEUE, &rt2x00dev->flags);
	__set_bit(DRIVER_REQUIRE_BEACON_GUARD, &rt2x00dev->flags);
	if (!modparam_nohwcrypt) {
		__set_bit(CONFIG_SUPPORT_HW_CRYPTO, &rt2x00dev->flags);
		__set_bit(DRIVER_REQUIRE_COPY_IV, &rt2x00dev->flags);
	}
	__set_bit(DRIVER_SUPPORT_WATCHDOG, &rt2x00dev->flags);

	/*
	 * Set the rssi offset.
	 */
	rt2x00dev->rssi_offset = DEFAULT_RSSI_OFFSET;

	return 0;
}

static const struct ieee80211_ops rt2500usb_mac80211_ops = {
	.tx			= rt2x00mac_tx,
	.start			= rt2x00mac_start,
	.stop			= rt2x00mac_stop,
	.add_interface		= rt2x00mac_add_interface,
	.remove_interface	= rt2x00mac_remove_interface,
	.config			= rt2x00mac_config,
	.configure_filter	= rt2x00mac_configure_filter,
	.set_tim		= rt2x00mac_set_tim,
	.set_key		= rt2x00mac_set_key,
	.sw_scan_start		= rt2x00mac_sw_scan_start,
	.sw_scan_complete	= rt2x00mac_sw_scan_complete,
	.get_stats		= rt2x00mac_get_stats,
	.bss_info_changed	= rt2x00mac_bss_info_changed,
	.conf_tx		= rt2x00mac_conf_tx,
	.rfkill_poll		= rt2x00mac_rfkill_poll,
	.flush			= rt2x00mac_flush,
};

static const struct rt2x00lib_ops rt2500usb_rt2x00_ops = {
	.probe_hw		= rt2500usb_probe_hw,
	.initialize		= rt2x00usb_initialize,
	.uninitialize		= rt2x00usb_uninitialize,
	.clear_entry		= rt2x00usb_clear_entry,
	.set_device_state	= rt2500usb_set_device_state,
	.rfkill_poll		= rt2500usb_rfkill_poll,
	.link_stats		= rt2500usb_link_stats,
	.reset_tuner		= rt2500usb_reset_tuner,
	.watchdog		= rt2x00usb_watchdog,
	.write_tx_desc		= rt2500usb_write_tx_desc,
	.write_beacon		= rt2500usb_write_beacon,
	.get_tx_data_len	= rt2500usb_get_tx_data_len,
	.kick_tx_queue		= rt2x00usb_kick_tx_queue,
	.kill_tx_queue		= rt2500usb_stop_queue,
	.fill_rxdone		= rt2500usb_fill_rxdone,
	.config_shared_key	= rt2500usb_config_key,
	.config_pairwise_key	= rt2500usb_config_key,
	.config_filter		= rt2500usb_config_filter,
	.config_intf		= rt2500usb_config_intf,
	.config_erp		= rt2500usb_config_erp,
	.config_ant		= rt2500usb_config_ant,
	.config			= rt2500usb_config,
};

static const struct data_queue_desc rt2500usb_queue_rx = {
	.entry_num		= 32,
	.data_size		= DATA_FRAME_SIZE,
	.desc_size		= RXD_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_usb),
};

static const struct data_queue_desc rt2500usb_queue_tx = {
	.entry_num		= 32,
	.data_size		= DATA_FRAME_SIZE,
	.desc_size		= TXD_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_usb),
};

static const struct data_queue_desc rt2500usb_queue_bcn = {
	.entry_num		= 1,
	.data_size		= MGMT_FRAME_SIZE,
	.desc_size		= TXD_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_usb_bcn),
};

static const struct data_queue_desc rt2500usb_queue_atim = {
	.entry_num		= 8,
	.data_size		= DATA_FRAME_SIZE,
	.desc_size		= TXD_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_usb),
};

static const struct rt2x00_ops rt2500usb_ops = {
	.name			= KBUILD_MODNAME,
	.max_sta_intf		= 1,
	.max_ap_intf		= 1,
	.eeprom_size		= EEPROM_SIZE,
	.rf_size		= RF_SIZE,
	.tx_queues		= NUM_TX_QUEUES,
	.extra_tx_headroom	= TXD_DESC_SIZE,
	.rx			= &rt2500usb_queue_rx,
	.tx			= &rt2500usb_queue_tx,
	.bcn			= &rt2500usb_queue_bcn,
	.atim			= &rt2500usb_queue_atim,
	.lib			= &rt2500usb_rt2x00_ops,
	.hw			= &rt2500usb_mac80211_ops,
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	.debugfs		= &rt2500usb_rt2x00debug,
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */
};

/*
 * rt2500usb module information.
 */
static struct usb_device_id rt2500usb_device_table[] = {
	/* ASUS */
	{ USB_DEVICE(0x0b05, 0x1706), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x0b05, 0x1707), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* Belkin */
	{ USB_DEVICE(0x050d, 0x7050), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x050d, 0x7051), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x050d, 0x705a), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* Cisco Systems */
	{ USB_DEVICE(0x13b1, 0x000d), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x13b1, 0x0011), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x13b1, 0x001a), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* CNet */
	{ USB_DEVICE(0x1371, 0x9022), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* Conceptronic */
	{ USB_DEVICE(0x14b2, 0x3c02), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* D-LINK */
	{ USB_DEVICE(0x2001, 0x3c00), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* Gigabyte */
	{ USB_DEVICE(0x1044, 0x8001), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x1044, 0x8007), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* Hercules */
	{ USB_DEVICE(0x06f8, 0xe000), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* Melco */
	{ USB_DEVICE(0x0411, 0x005e), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x0411, 0x0066), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x0411, 0x0067), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x0411, 0x008b), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x0411, 0x0097), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* MSI */
	{ USB_DEVICE(0x0db0, 0x6861), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x0db0, 0x6865), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x0db0, 0x6869), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* Ralink */
	{ USB_DEVICE(0x148f, 0x1706), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x148f, 0x2570), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x148f, 0x2573), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ USB_DEVICE(0x148f, 0x9020), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* Sagem */
	{ USB_DEVICE(0x079b, 0x004b), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* Siemens */
	{ USB_DEVICE(0x0681, 0x3c06), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* SMC */
	{ USB_DEVICE(0x0707, 0xee13), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* Spairon */
	{ USB_DEVICE(0x114b, 0x0110), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* SURECOM */
	{ USB_DEVICE(0x0769, 0x11f3), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* Trust */
	{ USB_DEVICE(0x0eb0, 0x9020), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* VTech */
	{ USB_DEVICE(0x0f88, 0x3012), USB_DEVICE_DATA(&rt2500usb_ops) },
	/* Zinwell */
	{ USB_DEVICE(0x5a57, 0x0260), USB_DEVICE_DATA(&rt2500usb_ops) },
	{ 0, }
};

MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("Ralink RT2500 USB Wireless LAN driver.");
MODULE_SUPPORTED_DEVICE("Ralink RT2570 USB chipset based cards");
MODULE_DEVICE_TABLE(usb, rt2500usb_device_table);
MODULE_LICENSE("GPL");

static struct usb_driver rt2500usb_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= rt2500usb_device_table,
	.probe		= rt2x00usb_probe,
	.disconnect	= rt2x00usb_disconnect,
	.suspend	= rt2x00usb_suspend,
	.resume		= rt2x00usb_resume,
};

static int __init rt2500usb_init(void)
{
	return usb_register(&rt2500usb_driver);
}

static void __exit rt2500usb_exit(void)
{
	usb_deregister(&rt2500usb_driver);
}

module_init(rt2500usb_init);
module_exit(rt2500usb_exit);

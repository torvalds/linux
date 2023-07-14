// SPDX-License-Identifier: GPL-2.0-or-later
/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
	<http://rt2x00.serialmonkey.com>

 */

/*
	Module: rt2400pci
	Abstract: rt2400pci device specific routines.
	Supported chipsets: RT2460.
 */

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/eeprom_93cx6.h>
#include <linux/slab.h>

#include "rt2x00.h"
#include "rt2x00mmio.h"
#include "rt2x00pci.h"
#include "rt2400pci.h"

/*
 * Register access.
 * All access to the CSR registers will go through the methods
 * rt2x00mmio_register_read and rt2x00mmio_register_write.
 * BBP and RF register require indirect register access,
 * and use the CSR registers BBPCSR and RFCSR to achieve this.
 * These indirect registers work with busy bits,
 * and we will try maximal REGISTER_BUSY_COUNT times to access
 * the register while taking a REGISTER_BUSY_DELAY us delay
 * between each attempt. When the busy bit is still set at that time,
 * the access attempt is considered to have failed,
 * and we will print an error.
 */
#define WAIT_FOR_BBP(__dev, __reg) \
	rt2x00mmio_regbusy_read((__dev), BBPCSR, BBPCSR_BUSY, (__reg))
#define WAIT_FOR_RF(__dev, __reg) \
	rt2x00mmio_regbusy_read((__dev), RFCSR, RFCSR_BUSY, (__reg))

static void rt2400pci_bbp_write(struct rt2x00_dev *rt2x00dev,
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
		rt2x00_set_field32(&reg, BBPCSR_VALUE, value);
		rt2x00_set_field32(&reg, BBPCSR_REGNUM, word);
		rt2x00_set_field32(&reg, BBPCSR_BUSY, 1);
		rt2x00_set_field32(&reg, BBPCSR_WRITE_CONTROL, 1);

		rt2x00mmio_register_write(rt2x00dev, BBPCSR, reg);
	}

	mutex_unlock(&rt2x00dev->csr_mutex);
}

static u8 rt2400pci_bbp_read(struct rt2x00_dev *rt2x00dev,
			     const unsigned int word)
{
	u32 reg;
	u8 value;

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
		rt2x00_set_field32(&reg, BBPCSR_REGNUM, word);
		rt2x00_set_field32(&reg, BBPCSR_BUSY, 1);
		rt2x00_set_field32(&reg, BBPCSR_WRITE_CONTROL, 0);

		rt2x00mmio_register_write(rt2x00dev, BBPCSR, reg);

		WAIT_FOR_BBP(rt2x00dev, &reg);
	}

	value = rt2x00_get_field32(reg, BBPCSR_VALUE);

	mutex_unlock(&rt2x00dev->csr_mutex);

	return value;
}

static void rt2400pci_rf_write(struct rt2x00_dev *rt2x00dev,
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
		rt2x00_set_field32(&reg, RFCSR_VALUE, value);
		rt2x00_set_field32(&reg, RFCSR_NUMBER_OF_BITS, 20);
		rt2x00_set_field32(&reg, RFCSR_IF_SELECT, 0);
		rt2x00_set_field32(&reg, RFCSR_BUSY, 1);

		rt2x00mmio_register_write(rt2x00dev, RFCSR, reg);
		rt2x00_rf_write(rt2x00dev, word, value);
	}

	mutex_unlock(&rt2x00dev->csr_mutex);
}

static void rt2400pci_eepromregister_read(struct eeprom_93cx6 *eeprom)
{
	struct rt2x00_dev *rt2x00dev = eeprom->data;
	u32 reg;

	reg = rt2x00mmio_register_read(rt2x00dev, CSR21);

	eeprom->reg_data_in = !!rt2x00_get_field32(reg, CSR21_EEPROM_DATA_IN);
	eeprom->reg_data_out = !!rt2x00_get_field32(reg, CSR21_EEPROM_DATA_OUT);
	eeprom->reg_data_clock =
	    !!rt2x00_get_field32(reg, CSR21_EEPROM_DATA_CLOCK);
	eeprom->reg_chip_select =
	    !!rt2x00_get_field32(reg, CSR21_EEPROM_CHIP_SELECT);
}

static void rt2400pci_eepromregister_write(struct eeprom_93cx6 *eeprom)
{
	struct rt2x00_dev *rt2x00dev = eeprom->data;
	u32 reg = 0;

	rt2x00_set_field32(&reg, CSR21_EEPROM_DATA_IN, !!eeprom->reg_data_in);
	rt2x00_set_field32(&reg, CSR21_EEPROM_DATA_OUT, !!eeprom->reg_data_out);
	rt2x00_set_field32(&reg, CSR21_EEPROM_DATA_CLOCK,
			   !!eeprom->reg_data_clock);
	rt2x00_set_field32(&reg, CSR21_EEPROM_CHIP_SELECT,
			   !!eeprom->reg_chip_select);

	rt2x00mmio_register_write(rt2x00dev, CSR21, reg);
}

#ifdef CONFIG_RT2X00_LIB_DEBUGFS
static const struct rt2x00debug rt2400pci_rt2x00debug = {
	.owner	= THIS_MODULE,
	.csr	= {
		.read		= rt2x00mmio_register_read,
		.write		= rt2x00mmio_register_write,
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
		.read		= rt2400pci_bbp_read,
		.write		= rt2400pci_bbp_write,
		.word_base	= BBP_BASE,
		.word_size	= sizeof(u8),
		.word_count	= BBP_SIZE / sizeof(u8),
	},
	.rf	= {
		.read		= rt2x00_rf_read,
		.write		= rt2400pci_rf_write,
		.word_base	= RF_BASE,
		.word_size	= sizeof(u32),
		.word_count	= RF_SIZE / sizeof(u32),
	},
};
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */

static int rt2400pci_rfkill_poll(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	reg = rt2x00mmio_register_read(rt2x00dev, GPIOCSR);
	return rt2x00_get_field32(reg, GPIOCSR_VAL0);
}

#ifdef CONFIG_RT2X00_LIB_LEDS
static void rt2400pci_brightness_set(struct led_classdev *led_cdev,
				     enum led_brightness brightness)
{
	struct rt2x00_led *led =
	    container_of(led_cdev, struct rt2x00_led, led_dev);
	unsigned int enabled = brightness != LED_OFF;
	u32 reg;

	reg = rt2x00mmio_register_read(led->rt2x00dev, LEDCSR);

	if (led->type == LED_TYPE_RADIO || led->type == LED_TYPE_ASSOC)
		rt2x00_set_field32(&reg, LEDCSR_LINK, enabled);
	else if (led->type == LED_TYPE_ACTIVITY)
		rt2x00_set_field32(&reg, LEDCSR_ACTIVITY, enabled);

	rt2x00mmio_register_write(led->rt2x00dev, LEDCSR, reg);
}

static int rt2400pci_blink_set(struct led_classdev *led_cdev,
			       unsigned long *delay_on,
			       unsigned long *delay_off)
{
	struct rt2x00_led *led =
	    container_of(led_cdev, struct rt2x00_led, led_dev);
	u32 reg;

	reg = rt2x00mmio_register_read(led->rt2x00dev, LEDCSR);
	rt2x00_set_field32(&reg, LEDCSR_ON_PERIOD, *delay_on);
	rt2x00_set_field32(&reg, LEDCSR_OFF_PERIOD, *delay_off);
	rt2x00mmio_register_write(led->rt2x00dev, LEDCSR, reg);

	return 0;
}

static void rt2400pci_init_led(struct rt2x00_dev *rt2x00dev,
			       struct rt2x00_led *led,
			       enum led_type type)
{
	led->rt2x00dev = rt2x00dev;
	led->type = type;
	led->led_dev.brightness_set = rt2400pci_brightness_set;
	led->led_dev.blink_set = rt2400pci_blink_set;
	led->flags = LED_INITIALIZED;
}
#endif /* CONFIG_RT2X00_LIB_LEDS */

/*
 * Configuration handlers.
 */
static void rt2400pci_config_filter(struct rt2x00_dev *rt2x00dev,
				    const unsigned int filter_flags)
{
	u32 reg;

	/*
	 * Start configuration steps.
	 * Note that the version error will always be dropped
	 * since there is no filter for it at this time.
	 */
	reg = rt2x00mmio_register_read(rt2x00dev, RXCSR0);
	rt2x00_set_field32(&reg, RXCSR0_DROP_CRC,
			   !(filter_flags & FIF_FCSFAIL));
	rt2x00_set_field32(&reg, RXCSR0_DROP_PHYSICAL,
			   !(filter_flags & FIF_PLCPFAIL));
	rt2x00_set_field32(&reg, RXCSR0_DROP_CONTROL,
			   !(filter_flags & FIF_CONTROL));
	rt2x00_set_field32(&reg, RXCSR0_DROP_NOT_TO_ME,
			   !test_bit(CONFIG_MONITORING, &rt2x00dev->flags));
	rt2x00_set_field32(&reg, RXCSR0_DROP_TODS,
			   !test_bit(CONFIG_MONITORING, &rt2x00dev->flags) &&
			   !rt2x00dev->intf_ap_count);
	rt2x00_set_field32(&reg, RXCSR0_DROP_VERSION_ERROR, 1);
	rt2x00mmio_register_write(rt2x00dev, RXCSR0, reg);
}

static void rt2400pci_config_intf(struct rt2x00_dev *rt2x00dev,
				  struct rt2x00_intf *intf,
				  struct rt2x00intf_conf *conf,
				  const unsigned int flags)
{
	unsigned int bcn_preload;
	u32 reg;

	if (flags & CONFIG_UPDATE_TYPE) {
		/*
		 * Enable beacon config
		 */
		bcn_preload = PREAMBLE + GET_DURATION(IEEE80211_HEADER, 20);
		reg = rt2x00mmio_register_read(rt2x00dev, BCNCSR1);
		rt2x00_set_field32(&reg, BCNCSR1_PRELOAD, bcn_preload);
		rt2x00mmio_register_write(rt2x00dev, BCNCSR1, reg);

		/*
		 * Enable synchronisation.
		 */
		reg = rt2x00mmio_register_read(rt2x00dev, CSR14);
		rt2x00_set_field32(&reg, CSR14_TSF_SYNC, conf->sync);
		rt2x00mmio_register_write(rt2x00dev, CSR14, reg);
	}

	if (flags & CONFIG_UPDATE_MAC)
		rt2x00mmio_register_multiwrite(rt2x00dev, CSR3,
					       conf->mac, sizeof(conf->mac));

	if (flags & CONFIG_UPDATE_BSSID)
		rt2x00mmio_register_multiwrite(rt2x00dev, CSR5,
					       conf->bssid,
					       sizeof(conf->bssid));
}

static void rt2400pci_config_erp(struct rt2x00_dev *rt2x00dev,
				 struct rt2x00lib_erp *erp,
				 u32 changed)
{
	int preamble_mask;
	u32 reg;

	/*
	 * When short preamble is enabled, we should set bit 0x08
	 */
	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		preamble_mask = erp->short_preamble << 3;

		reg = rt2x00mmio_register_read(rt2x00dev, TXCSR1);
		rt2x00_set_field32(&reg, TXCSR1_ACK_TIMEOUT, 0x1ff);
		rt2x00_set_field32(&reg, TXCSR1_ACK_CONSUME_TIME, 0x13a);
		rt2x00_set_field32(&reg, TXCSR1_TSF_OFFSET, IEEE80211_HEADER);
		rt2x00_set_field32(&reg, TXCSR1_AUTORESPONDER, 1);
		rt2x00mmio_register_write(rt2x00dev, TXCSR1, reg);

		reg = rt2x00mmio_register_read(rt2x00dev, ARCSR2);
		rt2x00_set_field32(&reg, ARCSR2_SIGNAL, 0x00);
		rt2x00_set_field32(&reg, ARCSR2_SERVICE, 0x04);
		rt2x00_set_field32(&reg, ARCSR2_LENGTH,
				   GET_DURATION(ACK_SIZE, 10));
		rt2x00mmio_register_write(rt2x00dev, ARCSR2, reg);

		reg = rt2x00mmio_register_read(rt2x00dev, ARCSR3);
		rt2x00_set_field32(&reg, ARCSR3_SIGNAL, 0x01 | preamble_mask);
		rt2x00_set_field32(&reg, ARCSR3_SERVICE, 0x04);
		rt2x00_set_field32(&reg, ARCSR2_LENGTH,
				   GET_DURATION(ACK_SIZE, 20));
		rt2x00mmio_register_write(rt2x00dev, ARCSR3, reg);

		reg = rt2x00mmio_register_read(rt2x00dev, ARCSR4);
		rt2x00_set_field32(&reg, ARCSR4_SIGNAL, 0x02 | preamble_mask);
		rt2x00_set_field32(&reg, ARCSR4_SERVICE, 0x04);
		rt2x00_set_field32(&reg, ARCSR2_LENGTH,
				   GET_DURATION(ACK_SIZE, 55));
		rt2x00mmio_register_write(rt2x00dev, ARCSR4, reg);

		reg = rt2x00mmio_register_read(rt2x00dev, ARCSR5);
		rt2x00_set_field32(&reg, ARCSR5_SIGNAL, 0x03 | preamble_mask);
		rt2x00_set_field32(&reg, ARCSR5_SERVICE, 0x84);
		rt2x00_set_field32(&reg, ARCSR2_LENGTH,
				   GET_DURATION(ACK_SIZE, 110));
		rt2x00mmio_register_write(rt2x00dev, ARCSR5, reg);
	}

	if (changed & BSS_CHANGED_BASIC_RATES)
		rt2x00mmio_register_write(rt2x00dev, ARCSR1, erp->basic_rates);

	if (changed & BSS_CHANGED_ERP_SLOT) {
		reg = rt2x00mmio_register_read(rt2x00dev, CSR11);
		rt2x00_set_field32(&reg, CSR11_SLOT_TIME, erp->slot_time);
		rt2x00mmio_register_write(rt2x00dev, CSR11, reg);

		reg = rt2x00mmio_register_read(rt2x00dev, CSR18);
		rt2x00_set_field32(&reg, CSR18_SIFS, erp->sifs);
		rt2x00_set_field32(&reg, CSR18_PIFS, erp->pifs);
		rt2x00mmio_register_write(rt2x00dev, CSR18, reg);

		reg = rt2x00mmio_register_read(rt2x00dev, CSR19);
		rt2x00_set_field32(&reg, CSR19_DIFS, erp->difs);
		rt2x00_set_field32(&reg, CSR19_EIFS, erp->eifs);
		rt2x00mmio_register_write(rt2x00dev, CSR19, reg);
	}

	if (changed & BSS_CHANGED_BEACON_INT) {
		reg = rt2x00mmio_register_read(rt2x00dev, CSR12);
		rt2x00_set_field32(&reg, CSR12_BEACON_INTERVAL,
				   erp->beacon_int * 16);
		rt2x00_set_field32(&reg, CSR12_CFP_MAX_DURATION,
				   erp->beacon_int * 16);
		rt2x00mmio_register_write(rt2x00dev, CSR12, reg);
	}
}

static void rt2400pci_config_ant(struct rt2x00_dev *rt2x00dev,
				 struct antenna_setup *ant)
{
	u8 r1;
	u8 r4;

	/*
	 * We should never come here because rt2x00lib is supposed
	 * to catch this and send us the correct antenna explicitely.
	 */
	BUG_ON(ant->rx == ANTENNA_SW_DIVERSITY ||
	       ant->tx == ANTENNA_SW_DIVERSITY);

	r4 = rt2400pci_bbp_read(rt2x00dev, 4);
	r1 = rt2400pci_bbp_read(rt2x00dev, 1);

	/*
	 * Configure the TX antenna.
	 */
	switch (ant->tx) {
	case ANTENNA_HW_DIVERSITY:
		rt2x00_set_field8(&r1, BBP_R1_TX_ANTENNA, 1);
		break;
	case ANTENNA_A:
		rt2x00_set_field8(&r1, BBP_R1_TX_ANTENNA, 0);
		break;
	case ANTENNA_B:
	default:
		rt2x00_set_field8(&r1, BBP_R1_TX_ANTENNA, 2);
		break;
	}

	/*
	 * Configure the RX antenna.
	 */
	switch (ant->rx) {
	case ANTENNA_HW_DIVERSITY:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 1);
		break;
	case ANTENNA_A:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 0);
		break;
	case ANTENNA_B:
	default:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 2);
		break;
	}

	rt2400pci_bbp_write(rt2x00dev, 4, r4);
	rt2400pci_bbp_write(rt2x00dev, 1, r1);
}

static void rt2400pci_config_channel(struct rt2x00_dev *rt2x00dev,
				     struct rf_channel *rf)
{
	/*
	 * Switch on tuning bits.
	 */
	rt2x00_set_field32(&rf->rf1, RF1_TUNER, 1);
	rt2x00_set_field32(&rf->rf3, RF3_TUNER, 1);

	rt2400pci_rf_write(rt2x00dev, 1, rf->rf1);
	rt2400pci_rf_write(rt2x00dev, 2, rf->rf2);
	rt2400pci_rf_write(rt2x00dev, 3, rf->rf3);

	/*
	 * RF2420 chipset don't need any additional actions.
	 */
	if (rt2x00_rf(rt2x00dev, RF2420))
		return;

	/*
	 * For the RT2421 chipsets we need to write an invalid
	 * reference clock rate to activate auto_tune.
	 * After that we set the value back to the correct channel.
	 */
	rt2400pci_rf_write(rt2x00dev, 1, rf->rf1);
	rt2400pci_rf_write(rt2x00dev, 2, 0x000c2a32);
	rt2400pci_rf_write(rt2x00dev, 3, rf->rf3);

	msleep(1);

	rt2400pci_rf_write(rt2x00dev, 1, rf->rf1);
	rt2400pci_rf_write(rt2x00dev, 2, rf->rf2);
	rt2400pci_rf_write(rt2x00dev, 3, rf->rf3);

	msleep(1);

	/*
	 * Switch off tuning bits.
	 */
	rt2x00_set_field32(&rf->rf1, RF1_TUNER, 0);
	rt2x00_set_field32(&rf->rf3, RF3_TUNER, 0);

	rt2400pci_rf_write(rt2x00dev, 1, rf->rf1);
	rt2400pci_rf_write(rt2x00dev, 3, rf->rf3);

	/*
	 * Clear false CRC during channel switch.
	 */
	rf->rf1 = rt2x00mmio_register_read(rt2x00dev, CNT0);
}

static void rt2400pci_config_txpower(struct rt2x00_dev *rt2x00dev, int txpower)
{
	rt2400pci_bbp_write(rt2x00dev, 3, TXPOWER_TO_DEV(txpower));
}

static void rt2400pci_config_retry_limit(struct rt2x00_dev *rt2x00dev,
					 struct rt2x00lib_conf *libconf)
{
	u32 reg;

	reg = rt2x00mmio_register_read(rt2x00dev, CSR11);
	rt2x00_set_field32(&reg, CSR11_LONG_RETRY,
			   libconf->conf->long_frame_max_tx_count);
	rt2x00_set_field32(&reg, CSR11_SHORT_RETRY,
			   libconf->conf->short_frame_max_tx_count);
	rt2x00mmio_register_write(rt2x00dev, CSR11, reg);
}

static void rt2400pci_config_ps(struct rt2x00_dev *rt2x00dev,
				struct rt2x00lib_conf *libconf)
{
	enum dev_state state =
	    (libconf->conf->flags & IEEE80211_CONF_PS) ?
		STATE_SLEEP : STATE_AWAKE;
	u32 reg;

	if (state == STATE_SLEEP) {
		reg = rt2x00mmio_register_read(rt2x00dev, CSR20);
		rt2x00_set_field32(&reg, CSR20_DELAY_AFTER_TBCN,
				   (rt2x00dev->beacon_int - 20) * 16);
		rt2x00_set_field32(&reg, CSR20_TBCN_BEFORE_WAKEUP,
				   libconf->conf->listen_interval - 1);

		/* We must first disable autowake before it can be enabled */
		rt2x00_set_field32(&reg, CSR20_AUTOWAKE, 0);
		rt2x00mmio_register_write(rt2x00dev, CSR20, reg);

		rt2x00_set_field32(&reg, CSR20_AUTOWAKE, 1);
		rt2x00mmio_register_write(rt2x00dev, CSR20, reg);
	} else {
		reg = rt2x00mmio_register_read(rt2x00dev, CSR20);
		rt2x00_set_field32(&reg, CSR20_AUTOWAKE, 0);
		rt2x00mmio_register_write(rt2x00dev, CSR20, reg);
	}

	rt2x00dev->ops->lib->set_device_state(rt2x00dev, state);
}

static void rt2400pci_config(struct rt2x00_dev *rt2x00dev,
			     struct rt2x00lib_conf *libconf,
			     const unsigned int flags)
{
	if (flags & IEEE80211_CONF_CHANGE_CHANNEL)
		rt2400pci_config_channel(rt2x00dev, &libconf->rf);
	if (flags & IEEE80211_CONF_CHANGE_POWER)
		rt2400pci_config_txpower(rt2x00dev,
					 libconf->conf->power_level);
	if (flags & IEEE80211_CONF_CHANGE_RETRY_LIMITS)
		rt2400pci_config_retry_limit(rt2x00dev, libconf);
	if (flags & IEEE80211_CONF_CHANGE_PS)
		rt2400pci_config_ps(rt2x00dev, libconf);
}

static void rt2400pci_config_cw(struct rt2x00_dev *rt2x00dev,
				const int cw_min, const int cw_max)
{
	u32 reg;

	reg = rt2x00mmio_register_read(rt2x00dev, CSR11);
	rt2x00_set_field32(&reg, CSR11_CWMIN, cw_min);
	rt2x00_set_field32(&reg, CSR11_CWMAX, cw_max);
	rt2x00mmio_register_write(rt2x00dev, CSR11, reg);
}

/*
 * Link tuning
 */
static void rt2400pci_link_stats(struct rt2x00_dev *rt2x00dev,
				 struct link_qual *qual)
{
	u32 reg;
	u8 bbp;

	/*
	 * Update FCS error count from register.
	 */
	reg = rt2x00mmio_register_read(rt2x00dev, CNT0);
	qual->rx_failed = rt2x00_get_field32(reg, CNT0_FCS_ERROR);

	/*
	 * Update False CCA count from register.
	 */
	bbp = rt2400pci_bbp_read(rt2x00dev, 39);
	qual->false_cca = bbp;
}

static inline void rt2400pci_set_vgc(struct rt2x00_dev *rt2x00dev,
				     struct link_qual *qual, u8 vgc_level)
{
	if (qual->vgc_level_reg != vgc_level) {
		rt2400pci_bbp_write(rt2x00dev, 13, vgc_level);
		qual->vgc_level = vgc_level;
		qual->vgc_level_reg = vgc_level;
	}
}

static void rt2400pci_reset_tuner(struct rt2x00_dev *rt2x00dev,
				  struct link_qual *qual)
{
	rt2400pci_set_vgc(rt2x00dev, qual, 0x08);
}

static void rt2400pci_link_tuner(struct rt2x00_dev *rt2x00dev,
				 struct link_qual *qual, const u32 count)
{
	/*
	 * The link tuner should not run longer then 60 seconds,
	 * and should run once every 2 seconds.
	 */
	if (count > 60 || !(count & 1))
		return;

	/*
	 * Base r13 link tuning on the false cca count.
	 */
	if ((qual->false_cca > 512) && (qual->vgc_level < 0x20))
		rt2400pci_set_vgc(rt2x00dev, qual, ++qual->vgc_level);
	else if ((qual->false_cca < 100) && (qual->vgc_level > 0x08))
		rt2400pci_set_vgc(rt2x00dev, qual, --qual->vgc_level);
}

/*
 * Queue handlers.
 */
static void rt2400pci_start_queue(struct data_queue *queue)
{
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	u32 reg;

	switch (queue->qid) {
	case QID_RX:
		reg = rt2x00mmio_register_read(rt2x00dev, RXCSR0);
		rt2x00_set_field32(&reg, RXCSR0_DISABLE_RX, 0);
		rt2x00mmio_register_write(rt2x00dev, RXCSR0, reg);
		break;
	case QID_BEACON:
		reg = rt2x00mmio_register_read(rt2x00dev, CSR14);
		rt2x00_set_field32(&reg, CSR14_TSF_COUNT, 1);
		rt2x00_set_field32(&reg, CSR14_TBCN, 1);
		rt2x00_set_field32(&reg, CSR14_BEACON_GEN, 1);
		rt2x00mmio_register_write(rt2x00dev, CSR14, reg);
		break;
	default:
		break;
	}
}

static void rt2400pci_kick_queue(struct data_queue *queue)
{
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	u32 reg;

	switch (queue->qid) {
	case QID_AC_VO:
		reg = rt2x00mmio_register_read(rt2x00dev, TXCSR0);
		rt2x00_set_field32(&reg, TXCSR0_KICK_PRIO, 1);
		rt2x00mmio_register_write(rt2x00dev, TXCSR0, reg);
		break;
	case QID_AC_VI:
		reg = rt2x00mmio_register_read(rt2x00dev, TXCSR0);
		rt2x00_set_field32(&reg, TXCSR0_KICK_TX, 1);
		rt2x00mmio_register_write(rt2x00dev, TXCSR0, reg);
		break;
	case QID_ATIM:
		reg = rt2x00mmio_register_read(rt2x00dev, TXCSR0);
		rt2x00_set_field32(&reg, TXCSR0_KICK_ATIM, 1);
		rt2x00mmio_register_write(rt2x00dev, TXCSR0, reg);
		break;
	default:
		break;
	}
}

static void rt2400pci_stop_queue(struct data_queue *queue)
{
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	u32 reg;

	switch (queue->qid) {
	case QID_AC_VO:
	case QID_AC_VI:
	case QID_ATIM:
		reg = rt2x00mmio_register_read(rt2x00dev, TXCSR0);
		rt2x00_set_field32(&reg, TXCSR0_ABORT, 1);
		rt2x00mmio_register_write(rt2x00dev, TXCSR0, reg);
		break;
	case QID_RX:
		reg = rt2x00mmio_register_read(rt2x00dev, RXCSR0);
		rt2x00_set_field32(&reg, RXCSR0_DISABLE_RX, 1);
		rt2x00mmio_register_write(rt2x00dev, RXCSR0, reg);
		break;
	case QID_BEACON:
		reg = rt2x00mmio_register_read(rt2x00dev, CSR14);
		rt2x00_set_field32(&reg, CSR14_TSF_COUNT, 0);
		rt2x00_set_field32(&reg, CSR14_TBCN, 0);
		rt2x00_set_field32(&reg, CSR14_BEACON_GEN, 0);
		rt2x00mmio_register_write(rt2x00dev, CSR14, reg);

		/*
		 * Wait for possibly running tbtt tasklets.
		 */
		tasklet_kill(&rt2x00dev->tbtt_tasklet);
		break;
	default:
		break;
	}
}

/*
 * Initialization functions.
 */
static bool rt2400pci_get_entry_state(struct queue_entry *entry)
{
	struct queue_entry_priv_mmio *entry_priv = entry->priv_data;
	u32 word;

	if (entry->queue->qid == QID_RX) {
		word = rt2x00_desc_read(entry_priv->desc, 0);

		return rt2x00_get_field32(word, RXD_W0_OWNER_NIC);
	} else {
		word = rt2x00_desc_read(entry_priv->desc, 0);

		return (rt2x00_get_field32(word, TXD_W0_OWNER_NIC) ||
		        rt2x00_get_field32(word, TXD_W0_VALID));
	}
}

static void rt2400pci_clear_entry(struct queue_entry *entry)
{
	struct queue_entry_priv_mmio *entry_priv = entry->priv_data;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	u32 word;

	if (entry->queue->qid == QID_RX) {
		word = rt2x00_desc_read(entry_priv->desc, 2);
		rt2x00_set_field32(&word, RXD_W2_BUFFER_LENGTH, entry->skb->len);
		rt2x00_desc_write(entry_priv->desc, 2, word);

		word = rt2x00_desc_read(entry_priv->desc, 1);
		rt2x00_set_field32(&word, RXD_W1_BUFFER_ADDRESS, skbdesc->skb_dma);
		rt2x00_desc_write(entry_priv->desc, 1, word);

		word = rt2x00_desc_read(entry_priv->desc, 0);
		rt2x00_set_field32(&word, RXD_W0_OWNER_NIC, 1);
		rt2x00_desc_write(entry_priv->desc, 0, word);
	} else {
		word = rt2x00_desc_read(entry_priv->desc, 0);
		rt2x00_set_field32(&word, TXD_W0_VALID, 0);
		rt2x00_set_field32(&word, TXD_W0_OWNER_NIC, 0);
		rt2x00_desc_write(entry_priv->desc, 0, word);
	}
}

static int rt2400pci_init_queues(struct rt2x00_dev *rt2x00dev)
{
	struct queue_entry_priv_mmio *entry_priv;
	u32 reg;

	/*
	 * Initialize registers.
	 */
	reg = rt2x00mmio_register_read(rt2x00dev, TXCSR2);
	rt2x00_set_field32(&reg, TXCSR2_TXD_SIZE, rt2x00dev->tx[0].desc_size);
	rt2x00_set_field32(&reg, TXCSR2_NUM_TXD, rt2x00dev->tx[1].limit);
	rt2x00_set_field32(&reg, TXCSR2_NUM_ATIM, rt2x00dev->atim->limit);
	rt2x00_set_field32(&reg, TXCSR2_NUM_PRIO, rt2x00dev->tx[0].limit);
	rt2x00mmio_register_write(rt2x00dev, TXCSR2, reg);

	entry_priv = rt2x00dev->tx[1].entries[0].priv_data;
	reg = rt2x00mmio_register_read(rt2x00dev, TXCSR3);
	rt2x00_set_field32(&reg, TXCSR3_TX_RING_REGISTER,
			   entry_priv->desc_dma);
	rt2x00mmio_register_write(rt2x00dev, TXCSR3, reg);

	entry_priv = rt2x00dev->tx[0].entries[0].priv_data;
	reg = rt2x00mmio_register_read(rt2x00dev, TXCSR5);
	rt2x00_set_field32(&reg, TXCSR5_PRIO_RING_REGISTER,
			   entry_priv->desc_dma);
	rt2x00mmio_register_write(rt2x00dev, TXCSR5, reg);

	entry_priv = rt2x00dev->atim->entries[0].priv_data;
	reg = rt2x00mmio_register_read(rt2x00dev, TXCSR4);
	rt2x00_set_field32(&reg, TXCSR4_ATIM_RING_REGISTER,
			   entry_priv->desc_dma);
	rt2x00mmio_register_write(rt2x00dev, TXCSR4, reg);

	entry_priv = rt2x00dev->bcn->entries[0].priv_data;
	reg = rt2x00mmio_register_read(rt2x00dev, TXCSR6);
	rt2x00_set_field32(&reg, TXCSR6_BEACON_RING_REGISTER,
			   entry_priv->desc_dma);
	rt2x00mmio_register_write(rt2x00dev, TXCSR6, reg);

	reg = rt2x00mmio_register_read(rt2x00dev, RXCSR1);
	rt2x00_set_field32(&reg, RXCSR1_RXD_SIZE, rt2x00dev->rx->desc_size);
	rt2x00_set_field32(&reg, RXCSR1_NUM_RXD, rt2x00dev->rx->limit);
	rt2x00mmio_register_write(rt2x00dev, RXCSR1, reg);

	entry_priv = rt2x00dev->rx->entries[0].priv_data;
	reg = rt2x00mmio_register_read(rt2x00dev, RXCSR2);
	rt2x00_set_field32(&reg, RXCSR2_RX_RING_REGISTER,
			   entry_priv->desc_dma);
	rt2x00mmio_register_write(rt2x00dev, RXCSR2, reg);

	return 0;
}

static int rt2400pci_init_registers(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2x00mmio_register_write(rt2x00dev, PSCSR0, 0x00020002);
	rt2x00mmio_register_write(rt2x00dev, PSCSR1, 0x00000002);
	rt2x00mmio_register_write(rt2x00dev, PSCSR2, 0x00023f20);
	rt2x00mmio_register_write(rt2x00dev, PSCSR3, 0x00000002);

	reg = rt2x00mmio_register_read(rt2x00dev, TIMECSR);
	rt2x00_set_field32(&reg, TIMECSR_US_COUNT, 33);
	rt2x00_set_field32(&reg, TIMECSR_US_64_COUNT, 63);
	rt2x00_set_field32(&reg, TIMECSR_BEACON_EXPECT, 0);
	rt2x00mmio_register_write(rt2x00dev, TIMECSR, reg);

	reg = rt2x00mmio_register_read(rt2x00dev, CSR9);
	rt2x00_set_field32(&reg, CSR9_MAX_FRAME_UNIT,
			   (rt2x00dev->rx->data_size / 128));
	rt2x00mmio_register_write(rt2x00dev, CSR9, reg);

	reg = rt2x00mmio_register_read(rt2x00dev, CSR14);
	rt2x00_set_field32(&reg, CSR14_TSF_COUNT, 0);
	rt2x00_set_field32(&reg, CSR14_TSF_SYNC, 0);
	rt2x00_set_field32(&reg, CSR14_TBCN, 0);
	rt2x00_set_field32(&reg, CSR14_TCFP, 0);
	rt2x00_set_field32(&reg, CSR14_TATIMW, 0);
	rt2x00_set_field32(&reg, CSR14_BEACON_GEN, 0);
	rt2x00_set_field32(&reg, CSR14_CFP_COUNT_PRELOAD, 0);
	rt2x00_set_field32(&reg, CSR14_TBCM_PRELOAD, 0);
	rt2x00mmio_register_write(rt2x00dev, CSR14, reg);

	rt2x00mmio_register_write(rt2x00dev, CNT3, 0x3f080000);

	reg = rt2x00mmio_register_read(rt2x00dev, ARCSR0);
	rt2x00_set_field32(&reg, ARCSR0_AR_BBP_DATA0, 133);
	rt2x00_set_field32(&reg, ARCSR0_AR_BBP_ID0, 134);
	rt2x00_set_field32(&reg, ARCSR0_AR_BBP_DATA1, 136);
	rt2x00_set_field32(&reg, ARCSR0_AR_BBP_ID1, 135);
	rt2x00mmio_register_write(rt2x00dev, ARCSR0, reg);

	reg = rt2x00mmio_register_read(rt2x00dev, RXCSR3);
	rt2x00_set_field32(&reg, RXCSR3_BBP_ID0, 3); /* Tx power.*/
	rt2x00_set_field32(&reg, RXCSR3_BBP_ID0_VALID, 1);
	rt2x00_set_field32(&reg, RXCSR3_BBP_ID1, 32); /* Signal */
	rt2x00_set_field32(&reg, RXCSR3_BBP_ID1_VALID, 1);
	rt2x00_set_field32(&reg, RXCSR3_BBP_ID2, 36); /* Rssi */
	rt2x00_set_field32(&reg, RXCSR3_BBP_ID2_VALID, 1);
	rt2x00mmio_register_write(rt2x00dev, RXCSR3, reg);

	rt2x00mmio_register_write(rt2x00dev, PWRCSR0, 0x3f3b3100);

	if (rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_AWAKE))
		return -EBUSY;

	rt2x00mmio_register_write(rt2x00dev, MACCSR0, 0x00217223);
	rt2x00mmio_register_write(rt2x00dev, MACCSR1, 0x00235518);

	reg = rt2x00mmio_register_read(rt2x00dev, MACCSR2);
	rt2x00_set_field32(&reg, MACCSR2_DELAY, 64);
	rt2x00mmio_register_write(rt2x00dev, MACCSR2, reg);

	reg = rt2x00mmio_register_read(rt2x00dev, RALINKCSR);
	rt2x00_set_field32(&reg, RALINKCSR_AR_BBP_DATA0, 17);
	rt2x00_set_field32(&reg, RALINKCSR_AR_BBP_ID0, 154);
	rt2x00_set_field32(&reg, RALINKCSR_AR_BBP_DATA1, 0);
	rt2x00_set_field32(&reg, RALINKCSR_AR_BBP_ID1, 154);
	rt2x00mmio_register_write(rt2x00dev, RALINKCSR, reg);

	reg = rt2x00mmio_register_read(rt2x00dev, CSR1);
	rt2x00_set_field32(&reg, CSR1_SOFT_RESET, 1);
	rt2x00_set_field32(&reg, CSR1_BBP_RESET, 0);
	rt2x00_set_field32(&reg, CSR1_HOST_READY, 0);
	rt2x00mmio_register_write(rt2x00dev, CSR1, reg);

	reg = rt2x00mmio_register_read(rt2x00dev, CSR1);
	rt2x00_set_field32(&reg, CSR1_SOFT_RESET, 0);
	rt2x00_set_field32(&reg, CSR1_HOST_READY, 1);
	rt2x00mmio_register_write(rt2x00dev, CSR1, reg);

	/*
	 * We must clear the FCS and FIFO error count.
	 * These registers are cleared on read,
	 * so we may pass a useless variable to store the value.
	 */
	reg = rt2x00mmio_register_read(rt2x00dev, CNT0);
	reg = rt2x00mmio_register_read(rt2x00dev, CNT4);

	return 0;
}

static int rt2400pci_wait_bbp_ready(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u8 value;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		value = rt2400pci_bbp_read(rt2x00dev, 0);
		if ((value != 0xff) && (value != 0x00))
			return 0;
		udelay(REGISTER_BUSY_DELAY);
	}

	rt2x00_err(rt2x00dev, "BBP register access failed, aborting\n");
	return -EACCES;
}

static int rt2400pci_init_bbp(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u16 eeprom;
	u8 reg_id;
	u8 value;

	if (unlikely(rt2400pci_wait_bbp_ready(rt2x00dev)))
		return -EACCES;

	rt2400pci_bbp_write(rt2x00dev, 1, 0x00);
	rt2400pci_bbp_write(rt2x00dev, 3, 0x27);
	rt2400pci_bbp_write(rt2x00dev, 4, 0x08);
	rt2400pci_bbp_write(rt2x00dev, 10, 0x0f);
	rt2400pci_bbp_write(rt2x00dev, 15, 0x72);
	rt2400pci_bbp_write(rt2x00dev, 16, 0x74);
	rt2400pci_bbp_write(rt2x00dev, 17, 0x20);
	rt2400pci_bbp_write(rt2x00dev, 18, 0x72);
	rt2400pci_bbp_write(rt2x00dev, 19, 0x0b);
	rt2400pci_bbp_write(rt2x00dev, 20, 0x00);
	rt2400pci_bbp_write(rt2x00dev, 28, 0x11);
	rt2400pci_bbp_write(rt2x00dev, 29, 0x04);
	rt2400pci_bbp_write(rt2x00dev, 30, 0x21);
	rt2400pci_bbp_write(rt2x00dev, 31, 0x00);

	for (i = 0; i < EEPROM_BBP_SIZE; i++) {
		eeprom = rt2x00_eeprom_read(rt2x00dev, EEPROM_BBP_START + i);

		if (eeprom != 0xffff && eeprom != 0x0000) {
			reg_id = rt2x00_get_field16(eeprom, EEPROM_BBP_REG_ID);
			value = rt2x00_get_field16(eeprom, EEPROM_BBP_VALUE);
			rt2400pci_bbp_write(rt2x00dev, reg_id, value);
		}
	}

	return 0;
}

/*
 * Device state switch handlers.
 */
static void rt2400pci_toggle_irq(struct rt2x00_dev *rt2x00dev,
				 enum dev_state state)
{
	int mask = (state == STATE_RADIO_IRQ_OFF);
	u32 reg;
	unsigned long flags;

	/*
	 * When interrupts are being enabled, the interrupt registers
	 * should clear the register to assure a clean state.
	 */
	if (state == STATE_RADIO_IRQ_ON) {
		reg = rt2x00mmio_register_read(rt2x00dev, CSR7);
		rt2x00mmio_register_write(rt2x00dev, CSR7, reg);
	}

	/*
	 * Only toggle the interrupts bits we are going to use.
	 * Non-checked interrupt bits are disabled by default.
	 */
	spin_lock_irqsave(&rt2x00dev->irqmask_lock, flags);

	reg = rt2x00mmio_register_read(rt2x00dev, CSR8);
	rt2x00_set_field32(&reg, CSR8_TBCN_EXPIRE, mask);
	rt2x00_set_field32(&reg, CSR8_TXDONE_TXRING, mask);
	rt2x00_set_field32(&reg, CSR8_TXDONE_ATIMRING, mask);
	rt2x00_set_field32(&reg, CSR8_TXDONE_PRIORING, mask);
	rt2x00_set_field32(&reg, CSR8_RXDONE, mask);
	rt2x00mmio_register_write(rt2x00dev, CSR8, reg);

	spin_unlock_irqrestore(&rt2x00dev->irqmask_lock, flags);

	if (state == STATE_RADIO_IRQ_OFF) {
		/*
		 * Ensure that all tasklets are finished before
		 * disabling the interrupts.
		 */
		tasklet_kill(&rt2x00dev->txstatus_tasklet);
		tasklet_kill(&rt2x00dev->rxdone_tasklet);
		tasklet_kill(&rt2x00dev->tbtt_tasklet);
	}
}

static int rt2400pci_enable_radio(struct rt2x00_dev *rt2x00dev)
{
	/*
	 * Initialize all registers.
	 */
	if (unlikely(rt2400pci_init_queues(rt2x00dev) ||
		     rt2400pci_init_registers(rt2x00dev) ||
		     rt2400pci_init_bbp(rt2x00dev)))
		return -EIO;

	return 0;
}

static void rt2400pci_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	/*
	 * Disable power
	 */
	rt2x00mmio_register_write(rt2x00dev, PWRCSR0, 0);
}

static int rt2400pci_set_state(struct rt2x00_dev *rt2x00dev,
			       enum dev_state state)
{
	u32 reg, reg2;
	unsigned int i;
	bool put_to_sleep;
	u8 bbp_state;
	u8 rf_state;

	put_to_sleep = (state != STATE_AWAKE);

	reg = rt2x00mmio_register_read(rt2x00dev, PWRCSR1);
	rt2x00_set_field32(&reg, PWRCSR1_SET_STATE, 1);
	rt2x00_set_field32(&reg, PWRCSR1_BBP_DESIRE_STATE, state);
	rt2x00_set_field32(&reg, PWRCSR1_RF_DESIRE_STATE, state);
	rt2x00_set_field32(&reg, PWRCSR1_PUT_TO_SLEEP, put_to_sleep);
	rt2x00mmio_register_write(rt2x00dev, PWRCSR1, reg);

	/*
	 * Device is not guaranteed to be in the requested state yet.
	 * We must wait until the register indicates that the
	 * device has entered the correct state.
	 */
	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		reg2 = rt2x00mmio_register_read(rt2x00dev, PWRCSR1);
		bbp_state = rt2x00_get_field32(reg2, PWRCSR1_BBP_CURR_STATE);
		rf_state = rt2x00_get_field32(reg2, PWRCSR1_RF_CURR_STATE);
		if (bbp_state == state && rf_state == state)
			return 0;
		rt2x00mmio_register_write(rt2x00dev, PWRCSR1, reg);
		msleep(10);
	}

	return -EBUSY;
}

static int rt2400pci_set_device_state(struct rt2x00_dev *rt2x00dev,
				      enum dev_state state)
{
	int retval = 0;

	switch (state) {
	case STATE_RADIO_ON:
		retval = rt2400pci_enable_radio(rt2x00dev);
		break;
	case STATE_RADIO_OFF:
		rt2400pci_disable_radio(rt2x00dev);
		break;
	case STATE_RADIO_IRQ_ON:
	case STATE_RADIO_IRQ_OFF:
		rt2400pci_toggle_irq(rt2x00dev, state);
		break;
	case STATE_DEEP_SLEEP:
	case STATE_SLEEP:
	case STATE_STANDBY:
	case STATE_AWAKE:
		retval = rt2400pci_set_state(rt2x00dev, state);
		break;
	default:
		retval = -ENOTSUPP;
		break;
	}

	if (unlikely(retval))
		rt2x00_err(rt2x00dev, "Device failed to enter state %d (%d)\n",
			   state, retval);

	return retval;
}

/*
 * TX descriptor initialization
 */
static void rt2400pci_write_tx_desc(struct queue_entry *entry,
				    struct txentry_desc *txdesc)
{
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	struct queue_entry_priv_mmio *entry_priv = entry->priv_data;
	__le32 *txd = entry_priv->desc;
	u32 word;

	/*
	 * Start writing the descriptor words.
	 */
	word = rt2x00_desc_read(txd, 1);
	rt2x00_set_field32(&word, TXD_W1_BUFFER_ADDRESS, skbdesc->skb_dma);
	rt2x00_desc_write(txd, 1, word);

	word = rt2x00_desc_read(txd, 2);
	rt2x00_set_field32(&word, TXD_W2_BUFFER_LENGTH, txdesc->length);
	rt2x00_set_field32(&word, TXD_W2_DATABYTE_COUNT, txdesc->length);
	rt2x00_desc_write(txd, 2, word);

	word = rt2x00_desc_read(txd, 3);
	rt2x00_set_field32(&word, TXD_W3_PLCP_SIGNAL, txdesc->u.plcp.signal);
	rt2x00_set_field32(&word, TXD_W3_PLCP_SIGNAL_REGNUM, 5);
	rt2x00_set_field32(&word, TXD_W3_PLCP_SIGNAL_BUSY, 1);
	rt2x00_set_field32(&word, TXD_W3_PLCP_SERVICE, txdesc->u.plcp.service);
	rt2x00_set_field32(&word, TXD_W3_PLCP_SERVICE_REGNUM, 6);
	rt2x00_set_field32(&word, TXD_W3_PLCP_SERVICE_BUSY, 1);
	rt2x00_desc_write(txd, 3, word);

	word = rt2x00_desc_read(txd, 4);
	rt2x00_set_field32(&word, TXD_W4_PLCP_LENGTH_LOW,
			   txdesc->u.plcp.length_low);
	rt2x00_set_field32(&word, TXD_W3_PLCP_LENGTH_LOW_REGNUM, 8);
	rt2x00_set_field32(&word, TXD_W3_PLCP_LENGTH_LOW_BUSY, 1);
	rt2x00_set_field32(&word, TXD_W4_PLCP_LENGTH_HIGH,
			   txdesc->u.plcp.length_high);
	rt2x00_set_field32(&word, TXD_W3_PLCP_LENGTH_HIGH_REGNUM, 7);
	rt2x00_set_field32(&word, TXD_W3_PLCP_LENGTH_HIGH_BUSY, 1);
	rt2x00_desc_write(txd, 4, word);

	/*
	 * Writing TXD word 0 must the last to prevent a race condition with
	 * the device, whereby the device may take hold of the TXD before we
	 * finished updating it.
	 */
	word = rt2x00_desc_read(txd, 0);
	rt2x00_set_field32(&word, TXD_W0_OWNER_NIC, 1);
	rt2x00_set_field32(&word, TXD_W0_VALID, 1);
	rt2x00_set_field32(&word, TXD_W0_MORE_FRAG,
			   test_bit(ENTRY_TXD_MORE_FRAG, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_ACK,
			   test_bit(ENTRY_TXD_ACK, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_TIMESTAMP,
			   test_bit(ENTRY_TXD_REQ_TIMESTAMP, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_RTS,
			   test_bit(ENTRY_TXD_RTS_FRAME, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_IFS, txdesc->u.plcp.ifs);
	rt2x00_set_field32(&word, TXD_W0_RETRY_MODE,
			   test_bit(ENTRY_TXD_RETRY_MODE, &txdesc->flags));
	rt2x00_desc_write(txd, 0, word);

	/*
	 * Register descriptor details in skb frame descriptor.
	 */
	skbdesc->desc = txd;
	skbdesc->desc_len = TXD_DESC_SIZE;
}

/*
 * TX data initialization
 */
static void rt2400pci_write_beacon(struct queue_entry *entry,
				   struct txentry_desc *txdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	u32 reg;

	/*
	 * Disable beaconing while we are reloading the beacon data,
	 * otherwise we might be sending out invalid data.
	 */
	reg = rt2x00mmio_register_read(rt2x00dev, CSR14);
	rt2x00_set_field32(&reg, CSR14_BEACON_GEN, 0);
	rt2x00mmio_register_write(rt2x00dev, CSR14, reg);

	if (rt2x00queue_map_txskb(entry)) {
		rt2x00_err(rt2x00dev, "Fail to map beacon, aborting\n");
		goto out;
	}
	/*
	 * Enable beaconing again.
	 */
	rt2x00_set_field32(&reg, CSR14_BEACON_GEN, 1);
	/*
	 * Write the TX descriptor for the beacon.
	 */
	rt2400pci_write_tx_desc(entry, txdesc);

	/*
	 * Dump beacon to userspace through debugfs.
	 */
	rt2x00debug_dump_frame(rt2x00dev, DUMP_FRAME_BEACON, entry);
out:
	/*
	 * Enable beaconing again.
	 */
	rt2x00_set_field32(&reg, CSR14_BEACON_GEN, 1);
	rt2x00mmio_register_write(rt2x00dev, CSR14, reg);
}

/*
 * RX control handlers
 */
static void rt2400pci_fill_rxdone(struct queue_entry *entry,
				  struct rxdone_entry_desc *rxdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct queue_entry_priv_mmio *entry_priv = entry->priv_data;
	u32 word0;
	u32 word2;
	u32 word3;
	u32 word4;
	u64 tsf;
	u32 rx_low;
	u32 rx_high;

	word0 = rt2x00_desc_read(entry_priv->desc, 0);
	word2 = rt2x00_desc_read(entry_priv->desc, 2);
	word3 = rt2x00_desc_read(entry_priv->desc, 3);
	word4 = rt2x00_desc_read(entry_priv->desc, 4);

	if (rt2x00_get_field32(word0, RXD_W0_CRC_ERROR))
		rxdesc->flags |= RX_FLAG_FAILED_FCS_CRC;
	if (rt2x00_get_field32(word0, RXD_W0_PHYSICAL_ERROR))
		rxdesc->flags |= RX_FLAG_FAILED_PLCP_CRC;

	/*
	 * We only get the lower 32bits from the timestamp,
	 * to get the full 64bits we must complement it with
	 * the timestamp from get_tsf().
	 * Note that when a wraparound of the lower 32bits
	 * has occurred between the frame arrival and the get_tsf()
	 * call, we must decrease the higher 32bits with 1 to get
	 * to correct value.
	 */
	tsf = rt2x00dev->ops->hw->get_tsf(rt2x00dev->hw, NULL);
	rx_low = rt2x00_get_field32(word4, RXD_W4_RX_END_TIME);
	rx_high = upper_32_bits(tsf);

	if ((u32)tsf <= rx_low)
		rx_high--;

	/*
	 * Obtain the status about this packet.
	 * The signal is the PLCP value, and needs to be stripped
	 * of the preamble bit (0x08).
	 */
	rxdesc->timestamp = ((u64)rx_high << 32) | rx_low;
	rxdesc->signal = rt2x00_get_field32(word2, RXD_W2_SIGNAL) & ~0x08;
	rxdesc->rssi = rt2x00_get_field32(word3, RXD_W3_RSSI) -
	    entry->queue->rt2x00dev->rssi_offset;
	rxdesc->size = rt2x00_get_field32(word0, RXD_W0_DATABYTE_COUNT);

	rxdesc->dev_flags |= RXDONE_SIGNAL_PLCP;
	if (rt2x00_get_field32(word0, RXD_W0_MY_BSS))
		rxdesc->dev_flags |= RXDONE_MY_BSS;
}

/*
 * Interrupt functions.
 */
static void rt2400pci_txdone(struct rt2x00_dev *rt2x00dev,
			     const enum data_queue_qid queue_idx)
{
	struct data_queue *queue = rt2x00queue_get_tx_queue(rt2x00dev, queue_idx);
	struct queue_entry_priv_mmio *entry_priv;
	struct queue_entry *entry;
	struct txdone_entry_desc txdesc;
	u32 word;

	while (!rt2x00queue_empty(queue)) {
		entry = rt2x00queue_get_entry(queue, Q_INDEX_DONE);
		entry_priv = entry->priv_data;
		word = rt2x00_desc_read(entry_priv->desc, 0);

		if (rt2x00_get_field32(word, TXD_W0_OWNER_NIC) ||
		    !rt2x00_get_field32(word, TXD_W0_VALID))
			break;

		/*
		 * Obtain the status about this packet.
		 */
		txdesc.flags = 0;
		switch (rt2x00_get_field32(word, TXD_W0_RESULT)) {
		case 0: /* Success */
		case 1: /* Success with retry */
			__set_bit(TXDONE_SUCCESS, &txdesc.flags);
			break;
		case 2: /* Failure, excessive retries */
			__set_bit(TXDONE_EXCESSIVE_RETRY, &txdesc.flags);
			fallthrough;	/* this is a failed frame! */
		default: /* Failure */
			__set_bit(TXDONE_FAILURE, &txdesc.flags);
		}
		txdesc.retry = rt2x00_get_field32(word, TXD_W0_RETRY_COUNT);

		rt2x00lib_txdone(entry, &txdesc);
	}
}

static inline void rt2400pci_enable_interrupt(struct rt2x00_dev *rt2x00dev,
					      struct rt2x00_field32 irq_field)
{
	u32 reg;

	/*
	 * Enable a single interrupt. The interrupt mask register
	 * access needs locking.
	 */
	spin_lock_irq(&rt2x00dev->irqmask_lock);

	reg = rt2x00mmio_register_read(rt2x00dev, CSR8);
	rt2x00_set_field32(&reg, irq_field, 0);
	rt2x00mmio_register_write(rt2x00dev, CSR8, reg);

	spin_unlock_irq(&rt2x00dev->irqmask_lock);
}

static void rt2400pci_txstatus_tasklet(struct tasklet_struct *t)
{
	struct rt2x00_dev *rt2x00dev = from_tasklet(rt2x00dev, t,
						    txstatus_tasklet);
	u32 reg;

	/*
	 * Handle all tx queues.
	 */
	rt2400pci_txdone(rt2x00dev, QID_ATIM);
	rt2400pci_txdone(rt2x00dev, QID_AC_VO);
	rt2400pci_txdone(rt2x00dev, QID_AC_VI);

	/*
	 * Enable all TXDONE interrupts again.
	 */
	if (test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags)) {
		spin_lock_irq(&rt2x00dev->irqmask_lock);

		reg = rt2x00mmio_register_read(rt2x00dev, CSR8);
		rt2x00_set_field32(&reg, CSR8_TXDONE_TXRING, 0);
		rt2x00_set_field32(&reg, CSR8_TXDONE_ATIMRING, 0);
		rt2x00_set_field32(&reg, CSR8_TXDONE_PRIORING, 0);
		rt2x00mmio_register_write(rt2x00dev, CSR8, reg);

		spin_unlock_irq(&rt2x00dev->irqmask_lock);
	}
}

static void rt2400pci_tbtt_tasklet(struct tasklet_struct *t)
{
	struct rt2x00_dev *rt2x00dev = from_tasklet(rt2x00dev, t, tbtt_tasklet);
	rt2x00lib_beacondone(rt2x00dev);
	if (test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2400pci_enable_interrupt(rt2x00dev, CSR8_TBCN_EXPIRE);
}

static void rt2400pci_rxdone_tasklet(struct tasklet_struct *t)
{
	struct rt2x00_dev *rt2x00dev = from_tasklet(rt2x00dev, t,
						    rxdone_tasklet);
	if (rt2x00mmio_rxdone(rt2x00dev))
		tasklet_schedule(&rt2x00dev->rxdone_tasklet);
	else if (test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2400pci_enable_interrupt(rt2x00dev, CSR8_RXDONE);
}

static irqreturn_t rt2400pci_interrupt(int irq, void *dev_instance)
{
	struct rt2x00_dev *rt2x00dev = dev_instance;
	u32 reg, mask;

	/*
	 * Get the interrupt sources & saved to local variable.
	 * Write register value back to clear pending interrupts.
	 */
	reg = rt2x00mmio_register_read(rt2x00dev, CSR7);
	rt2x00mmio_register_write(rt2x00dev, CSR7, reg);

	if (!reg)
		return IRQ_NONE;

	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return IRQ_HANDLED;

	mask = reg;

	/*
	 * Schedule tasklets for interrupt handling.
	 */
	if (rt2x00_get_field32(reg, CSR7_TBCN_EXPIRE))
		tasklet_hi_schedule(&rt2x00dev->tbtt_tasklet);

	if (rt2x00_get_field32(reg, CSR7_RXDONE))
		tasklet_schedule(&rt2x00dev->rxdone_tasklet);

	if (rt2x00_get_field32(reg, CSR7_TXDONE_ATIMRING) ||
	    rt2x00_get_field32(reg, CSR7_TXDONE_PRIORING) ||
	    rt2x00_get_field32(reg, CSR7_TXDONE_TXRING)) {
		tasklet_schedule(&rt2x00dev->txstatus_tasklet);
		/*
		 * Mask out all txdone interrupts.
		 */
		rt2x00_set_field32(&mask, CSR8_TXDONE_TXRING, 1);
		rt2x00_set_field32(&mask, CSR8_TXDONE_ATIMRING, 1);
		rt2x00_set_field32(&mask, CSR8_TXDONE_PRIORING, 1);
	}

	/*
	 * Disable all interrupts for which a tasklet was scheduled right now,
	 * the tasklet will reenable the appropriate interrupts.
	 */
	spin_lock(&rt2x00dev->irqmask_lock);

	reg = rt2x00mmio_register_read(rt2x00dev, CSR8);
	reg |= mask;
	rt2x00mmio_register_write(rt2x00dev, CSR8, reg);

	spin_unlock(&rt2x00dev->irqmask_lock);



	return IRQ_HANDLED;
}

/*
 * Device probe functions.
 */
static int rt2400pci_validate_eeprom(struct rt2x00_dev *rt2x00dev)
{
	struct eeprom_93cx6 eeprom;
	u32 reg;
	u16 word;
	u8 *mac;

	reg = rt2x00mmio_register_read(rt2x00dev, CSR21);

	eeprom.data = rt2x00dev;
	eeprom.register_read = rt2400pci_eepromregister_read;
	eeprom.register_write = rt2400pci_eepromregister_write;
	eeprom.width = rt2x00_get_field32(reg, CSR21_TYPE_93C46) ?
	    PCI_EEPROM_WIDTH_93C46 : PCI_EEPROM_WIDTH_93C66;
	eeprom.reg_data_in = 0;
	eeprom.reg_data_out = 0;
	eeprom.reg_data_clock = 0;
	eeprom.reg_chip_select = 0;

	eeprom_93cx6_multiread(&eeprom, EEPROM_BASE, rt2x00dev->eeprom,
			       EEPROM_SIZE / sizeof(u16));

	/*
	 * Start validation of the data that has been read.
	 */
	mac = rt2x00_eeprom_addr(rt2x00dev, EEPROM_MAC_ADDR_0);
	rt2x00lib_set_mac_address(rt2x00dev, mac);

	word = rt2x00_eeprom_read(rt2x00dev, EEPROM_ANTENNA);
	if (word == 0xffff) {
		rt2x00_err(rt2x00dev, "Invalid EEPROM data detected\n");
		return -EINVAL;
	}

	return 0;
}

static int rt2400pci_init_eeprom(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	u16 value;
	u16 eeprom;

	/*
	 * Read EEPROM word for configuration.
	 */
	eeprom = rt2x00_eeprom_read(rt2x00dev, EEPROM_ANTENNA);

	/*
	 * Identify RF chipset.
	 */
	value = rt2x00_get_field16(eeprom, EEPROM_ANTENNA_RF_TYPE);
	reg = rt2x00mmio_register_read(rt2x00dev, CSR0);
	rt2x00_set_chip(rt2x00dev, RT2460, value,
			rt2x00_get_field32(reg, CSR0_REVISION));

	if (!rt2x00_rf(rt2x00dev, RF2420) && !rt2x00_rf(rt2x00dev, RF2421)) {
		rt2x00_err(rt2x00dev, "Invalid RF chipset detected\n");
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

	rt2400pci_init_led(rt2x00dev, &rt2x00dev->led_radio, LED_TYPE_RADIO);
	if (value == LED_MODE_TXRX_ACTIVITY ||
	    value == LED_MODE_DEFAULT ||
	    value == LED_MODE_ASUS)
		rt2400pci_init_led(rt2x00dev, &rt2x00dev->led_qual,
				   LED_TYPE_ACTIVITY);
#endif /* CONFIG_RT2X00_LIB_LEDS */

	/*
	 * Detect if this device has an hardware controlled radio.
	 */
	if (rt2x00_get_field16(eeprom, EEPROM_ANTENNA_HARDWARE_RADIO))
		__set_bit(CAPABILITY_HW_BUTTON, &rt2x00dev->cap_flags);

	/*
	 * Check if the BBP tuning should be enabled.
	 */
	if (rt2x00_get_field16(eeprom, EEPROM_ANTENNA_RX_AGCVGC_TUNING))
		__set_bit(CAPABILITY_LINK_TUNING, &rt2x00dev->cap_flags);

	return 0;
}

/*
 * RF value list for RF2420 & RF2421
 * Supports: 2.4 GHz
 */
static const struct rf_channel rf_vals_b[] = {
	{ 1,  0x00022058, 0x000c1fda, 0x00000101, 0 },
	{ 2,  0x00022058, 0x000c1fee, 0x00000101, 0 },
	{ 3,  0x00022058, 0x000c2002, 0x00000101, 0 },
	{ 4,  0x00022058, 0x000c2016, 0x00000101, 0 },
	{ 5,  0x00022058, 0x000c202a, 0x00000101, 0 },
	{ 6,  0x00022058, 0x000c203e, 0x00000101, 0 },
	{ 7,  0x00022058, 0x000c2052, 0x00000101, 0 },
	{ 8,  0x00022058, 0x000c2066, 0x00000101, 0 },
	{ 9,  0x00022058, 0x000c207a, 0x00000101, 0 },
	{ 10, 0x00022058, 0x000c208e, 0x00000101, 0 },
	{ 11, 0x00022058, 0x000c20a2, 0x00000101, 0 },
	{ 12, 0x00022058, 0x000c20b6, 0x00000101, 0 },
	{ 13, 0x00022058, 0x000c20ca, 0x00000101, 0 },
	{ 14, 0x00022058, 0x000c20fa, 0x00000101, 0 },
};

static int rt2400pci_probe_hw_mode(struct rt2x00_dev *rt2x00dev)
{
	struct hw_mode_spec *spec = &rt2x00dev->spec;
	struct channel_info *info;
	u8 *tx_power;
	unsigned int i;

	/*
	 * Initialize all hw fields.
	 */
	ieee80211_hw_set(rt2x00dev->hw, PS_NULLFUNC_STACK);
	ieee80211_hw_set(rt2x00dev->hw, SUPPORTS_PS);
	ieee80211_hw_set(rt2x00dev->hw, HOST_BROADCAST_PS_BUFFERING);
	ieee80211_hw_set(rt2x00dev->hw, SIGNAL_DBM);

	SET_IEEE80211_DEV(rt2x00dev->hw, rt2x00dev->dev);
	SET_IEEE80211_PERM_ADDR(rt2x00dev->hw,
				rt2x00_eeprom_addr(rt2x00dev,
						   EEPROM_MAC_ADDR_0));

	/*
	 * Initialize hw_mode information.
	 */
	spec->supported_bands = SUPPORT_BAND_2GHZ;
	spec->supported_rates = SUPPORT_RATE_CCK;

	spec->num_channels = ARRAY_SIZE(rf_vals_b);
	spec->channels = rf_vals_b;

	/*
	 * Create channel information array
	 */
	info = kcalloc(spec->num_channels, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	spec->channels_info = info;

	tx_power = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_START);
	for (i = 0; i < 14; i++) {
		info[i].max_power = TXPOWER_FROM_DEV(MAX_TXPOWER);
		info[i].default_power1 = TXPOWER_FROM_DEV(tx_power[i]);
	}

	return 0;
}

static int rt2400pci_probe_hw(struct rt2x00_dev *rt2x00dev)
{
	int retval;
	u32 reg;

	/*
	 * Allocate eeprom data.
	 */
	retval = rt2400pci_validate_eeprom(rt2x00dev);
	if (retval)
		return retval;

	retval = rt2400pci_init_eeprom(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * Enable rfkill polling by setting GPIO direction of the
	 * rfkill switch GPIO pin correctly.
	 */
	reg = rt2x00mmio_register_read(rt2x00dev, GPIOCSR);
	rt2x00_set_field32(&reg, GPIOCSR_DIR0, 1);
	rt2x00mmio_register_write(rt2x00dev, GPIOCSR, reg);

	/*
	 * Initialize hw specifications.
	 */
	retval = rt2400pci_probe_hw_mode(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * This device requires the atim queue and DMA-mapped skbs.
	 */
	__set_bit(REQUIRE_ATIM_QUEUE, &rt2x00dev->cap_flags);
	__set_bit(REQUIRE_DMA, &rt2x00dev->cap_flags);
	__set_bit(REQUIRE_SW_SEQNO, &rt2x00dev->cap_flags);

	/*
	 * Set the rssi offset.
	 */
	rt2x00dev->rssi_offset = DEFAULT_RSSI_OFFSET;

	return 0;
}

/*
 * IEEE80211 stack callback functions.
 */
static int rt2400pci_conf_tx(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     unsigned int link_id, u16 queue,
			     const struct ieee80211_tx_queue_params *params)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	/*
	 * We don't support variating cw_min and cw_max variables
	 * per queue. So by default we only configure the TX queue,
	 * and ignore all other configurations.
	 */
	if (queue != 0)
		return -EINVAL;

	if (rt2x00mac_conf_tx(hw, vif, link_id, queue, params))
		return -EINVAL;

	/*
	 * Write configuration to register.
	 */
	rt2400pci_config_cw(rt2x00dev,
			    rt2x00dev->tx->cw_min, rt2x00dev->tx->cw_max);

	return 0;
}

static u64 rt2400pci_get_tsf(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	u64 tsf;
	u32 reg;

	reg = rt2x00mmio_register_read(rt2x00dev, CSR17);
	tsf = (u64) rt2x00_get_field32(reg, CSR17_HIGH_TSFTIMER) << 32;
	reg = rt2x00mmio_register_read(rt2x00dev, CSR16);
	tsf |= rt2x00_get_field32(reg, CSR16_LOW_TSFTIMER);

	return tsf;
}

static int rt2400pci_tx_last_beacon(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	u32 reg;

	reg = rt2x00mmio_register_read(rt2x00dev, CSR15);
	return rt2x00_get_field32(reg, CSR15_BEACON_SENT);
}

static const struct ieee80211_ops rt2400pci_mac80211_ops = {
	.tx			= rt2x00mac_tx,
	.wake_tx_queue		= ieee80211_handle_wake_tx_queue,
	.start			= rt2x00mac_start,
	.stop			= rt2x00mac_stop,
	.add_interface		= rt2x00mac_add_interface,
	.remove_interface	= rt2x00mac_remove_interface,
	.config			= rt2x00mac_config,
	.configure_filter	= rt2x00mac_configure_filter,
	.sw_scan_start		= rt2x00mac_sw_scan_start,
	.sw_scan_complete	= rt2x00mac_sw_scan_complete,
	.get_stats		= rt2x00mac_get_stats,
	.bss_info_changed	= rt2x00mac_bss_info_changed,
	.conf_tx		= rt2400pci_conf_tx,
	.get_tsf		= rt2400pci_get_tsf,
	.tx_last_beacon		= rt2400pci_tx_last_beacon,
	.rfkill_poll		= rt2x00mac_rfkill_poll,
	.flush			= rt2x00mac_flush,
	.set_antenna		= rt2x00mac_set_antenna,
	.get_antenna		= rt2x00mac_get_antenna,
	.get_ringparam		= rt2x00mac_get_ringparam,
	.tx_frames_pending	= rt2x00mac_tx_frames_pending,
};

static const struct rt2x00lib_ops rt2400pci_rt2x00_ops = {
	.irq_handler		= rt2400pci_interrupt,
	.txstatus_tasklet	= rt2400pci_txstatus_tasklet,
	.tbtt_tasklet		= rt2400pci_tbtt_tasklet,
	.rxdone_tasklet		= rt2400pci_rxdone_tasklet,
	.probe_hw		= rt2400pci_probe_hw,
	.initialize		= rt2x00mmio_initialize,
	.uninitialize		= rt2x00mmio_uninitialize,
	.get_entry_state	= rt2400pci_get_entry_state,
	.clear_entry		= rt2400pci_clear_entry,
	.set_device_state	= rt2400pci_set_device_state,
	.rfkill_poll		= rt2400pci_rfkill_poll,
	.link_stats		= rt2400pci_link_stats,
	.reset_tuner		= rt2400pci_reset_tuner,
	.link_tuner		= rt2400pci_link_tuner,
	.start_queue		= rt2400pci_start_queue,
	.kick_queue		= rt2400pci_kick_queue,
	.stop_queue		= rt2400pci_stop_queue,
	.flush_queue		= rt2x00mmio_flush_queue,
	.write_tx_desc		= rt2400pci_write_tx_desc,
	.write_beacon		= rt2400pci_write_beacon,
	.fill_rxdone		= rt2400pci_fill_rxdone,
	.config_filter		= rt2400pci_config_filter,
	.config_intf		= rt2400pci_config_intf,
	.config_erp		= rt2400pci_config_erp,
	.config_ant		= rt2400pci_config_ant,
	.config			= rt2400pci_config,
};

static void rt2400pci_queue_init(struct data_queue *queue)
{
	switch (queue->qid) {
	case QID_RX:
		queue->limit = 24;
		queue->data_size = DATA_FRAME_SIZE;
		queue->desc_size = RXD_DESC_SIZE;
		queue->priv_size = sizeof(struct queue_entry_priv_mmio);
		break;

	case QID_AC_VO:
	case QID_AC_VI:
	case QID_AC_BE:
	case QID_AC_BK:
		queue->limit = 24;
		queue->data_size = DATA_FRAME_SIZE;
		queue->desc_size = TXD_DESC_SIZE;
		queue->priv_size = sizeof(struct queue_entry_priv_mmio);
		break;

	case QID_BEACON:
		queue->limit = 1;
		queue->data_size = MGMT_FRAME_SIZE;
		queue->desc_size = TXD_DESC_SIZE;
		queue->priv_size = sizeof(struct queue_entry_priv_mmio);
		break;

	case QID_ATIM:
		queue->limit = 8;
		queue->data_size = DATA_FRAME_SIZE;
		queue->desc_size = TXD_DESC_SIZE;
		queue->priv_size = sizeof(struct queue_entry_priv_mmio);
		break;

	default:
		BUG();
		break;
	}
}

static const struct rt2x00_ops rt2400pci_ops = {
	.name			= KBUILD_MODNAME,
	.max_ap_intf		= 1,
	.eeprom_size		= EEPROM_SIZE,
	.rf_size		= RF_SIZE,
	.tx_queues		= NUM_TX_QUEUES,
	.queue_init		= rt2400pci_queue_init,
	.lib			= &rt2400pci_rt2x00_ops,
	.hw			= &rt2400pci_mac80211_ops,
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	.debugfs		= &rt2400pci_rt2x00debug,
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */
};

/*
 * RT2400pci module information.
 */
static const struct pci_device_id rt2400pci_device_table[] = {
	{ PCI_DEVICE(0x1814, 0x0101) },
	{ 0, }
};


MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("Ralink RT2400 PCI & PCMCIA Wireless LAN driver.");
MODULE_DEVICE_TABLE(pci, rt2400pci_device_table);
MODULE_LICENSE("GPL");

static int rt2400pci_probe(struct pci_dev *pci_dev,
			   const struct pci_device_id *id)
{
	return rt2x00pci_probe(pci_dev, &rt2400pci_ops);
}

static struct pci_driver rt2400pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= rt2400pci_device_table,
	.probe		= rt2400pci_probe,
	.remove		= rt2x00pci_remove,
	.driver.pm	= &rt2x00pci_pm_ops,
};

module_pci_driver(rt2400pci_driver);

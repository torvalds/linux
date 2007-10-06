/*
	Copyright (C) 2004 - 2007 rt2x00 SourceForge Project
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
	Module: rt2400pci
	Abstract: rt2400pci device specific routines.
	Supported chipsets: RT2460.
 */

/*
 * Set enviroment defines for rt2x00.h
 */
#define DRV_NAME "rt2400pci"

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/eeprom_93cx6.h>

#include "rt2x00.h"
#include "rt2x00pci.h"
#include "rt2400pci.h"

/*
 * Register access.
 * All access to the CSR registers will go through the methods
 * rt2x00pci_register_read and rt2x00pci_register_write.
 * BBP and RF register require indirect register access,
 * and use the CSR registers BBPCSR and RFCSR to achieve this.
 * These indirect registers work with busy bits,
 * and we will try maximal REGISTER_BUSY_COUNT times to access
 * the register while taking a REGISTER_BUSY_DELAY us delay
 * between each attampt. When the busy bit is still set at that time,
 * the access attempt is considered to have failed,
 * and we will print an error.
 */
static u32 rt2400pci_bbp_check(const struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	unsigned int i;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2x00pci_register_read(rt2x00dev, BBPCSR, &reg);
		if (!rt2x00_get_field32(reg, BBPCSR_BUSY))
			break;
		udelay(REGISTER_BUSY_DELAY);
	}

	return reg;
}

static void rt2400pci_bbp_write(const struct rt2x00_dev *rt2x00dev,
				const unsigned int word, const u8 value)
{
	u32 reg;

	/*
	 * Wait until the BBP becomes ready.
	 */
	reg = rt2400pci_bbp_check(rt2x00dev);
	if (rt2x00_get_field32(reg, BBPCSR_BUSY)) {
		ERROR(rt2x00dev, "BBPCSR register busy. Write failed.\n");
		return;
	}

	/*
	 * Write the data into the BBP.
	 */
	reg = 0;
	rt2x00_set_field32(&reg, BBPCSR_VALUE, value);
	rt2x00_set_field32(&reg, BBPCSR_REGNUM, word);
	rt2x00_set_field32(&reg, BBPCSR_BUSY, 1);
	rt2x00_set_field32(&reg, BBPCSR_WRITE_CONTROL, 1);

	rt2x00pci_register_write(rt2x00dev, BBPCSR, reg);
}

static void rt2400pci_bbp_read(const struct rt2x00_dev *rt2x00dev,
			       const unsigned int word, u8 *value)
{
	u32 reg;

	/*
	 * Wait until the BBP becomes ready.
	 */
	reg = rt2400pci_bbp_check(rt2x00dev);
	if (rt2x00_get_field32(reg, BBPCSR_BUSY)) {
		ERROR(rt2x00dev, "BBPCSR register busy. Read failed.\n");
		return;
	}

	/*
	 * Write the request into the BBP.
	 */
	reg = 0;
	rt2x00_set_field32(&reg, BBPCSR_REGNUM, word);
	rt2x00_set_field32(&reg, BBPCSR_BUSY, 1);
	rt2x00_set_field32(&reg, BBPCSR_WRITE_CONTROL, 0);

	rt2x00pci_register_write(rt2x00dev, BBPCSR, reg);

	/*
	 * Wait until the BBP becomes ready.
	 */
	reg = rt2400pci_bbp_check(rt2x00dev);
	if (rt2x00_get_field32(reg, BBPCSR_BUSY)) {
		ERROR(rt2x00dev, "BBPCSR register busy. Read failed.\n");
		*value = 0xff;
		return;
	}

	*value = rt2x00_get_field32(reg, BBPCSR_VALUE);
}

static void rt2400pci_rf_write(const struct rt2x00_dev *rt2x00dev,
			       const unsigned int word, const u32 value)
{
	u32 reg;
	unsigned int i;

	if (!word)
		return;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2x00pci_register_read(rt2x00dev, RFCSR, &reg);
		if (!rt2x00_get_field32(reg, RFCSR_BUSY))
			goto rf_write;
		udelay(REGISTER_BUSY_DELAY);
	}

	ERROR(rt2x00dev, "RFCSR register busy. Write failed.\n");
	return;

rf_write:
	reg = 0;
	rt2x00_set_field32(&reg, RFCSR_VALUE, value);
	rt2x00_set_field32(&reg, RFCSR_NUMBER_OF_BITS, 20);
	rt2x00_set_field32(&reg, RFCSR_IF_SELECT, 0);
	rt2x00_set_field32(&reg, RFCSR_BUSY, 1);

	rt2x00pci_register_write(rt2x00dev, RFCSR, reg);
	rt2x00_rf_write(rt2x00dev, word, value);
}

static void rt2400pci_eepromregister_read(struct eeprom_93cx6 *eeprom)
{
	struct rt2x00_dev *rt2x00dev = eeprom->data;
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, CSR21, &reg);

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

	rt2x00pci_register_write(rt2x00dev, CSR21, reg);
}

#ifdef CONFIG_RT2X00_LIB_DEBUGFS
#define CSR_OFFSET(__word)	( CSR_REG_BASE + ((__word) * sizeof(u32)) )

static void rt2400pci_read_csr(const struct rt2x00_dev *rt2x00dev,
			       const unsigned int word, u32 *data)
{
	rt2x00pci_register_read(rt2x00dev, CSR_OFFSET(word), data);
}

static void rt2400pci_write_csr(const struct rt2x00_dev *rt2x00dev,
				const unsigned int word, u32 data)
{
	rt2x00pci_register_write(rt2x00dev, CSR_OFFSET(word), data);
}

static const struct rt2x00debug rt2400pci_rt2x00debug = {
	.owner	= THIS_MODULE,
	.csr	= {
		.read		= rt2400pci_read_csr,
		.write		= rt2400pci_write_csr,
		.word_size	= sizeof(u32),
		.word_count	= CSR_REG_SIZE / sizeof(u32),
	},
	.eeprom	= {
		.read		= rt2x00_eeprom_read,
		.write		= rt2x00_eeprom_write,
		.word_size	= sizeof(u16),
		.word_count	= EEPROM_SIZE / sizeof(u16),
	},
	.bbp	= {
		.read		= rt2400pci_bbp_read,
		.write		= rt2400pci_bbp_write,
		.word_size	= sizeof(u8),
		.word_count	= BBP_SIZE / sizeof(u8),
	},
	.rf	= {
		.read		= rt2x00_rf_read,
		.write		= rt2400pci_rf_write,
		.word_size	= sizeof(u32),
		.word_count	= RF_SIZE / sizeof(u32),
	},
};
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */

#ifdef CONFIG_RT2400PCI_RFKILL
static int rt2400pci_rfkill_poll(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, GPIOCSR, &reg);
	return rt2x00_get_field32(reg, GPIOCSR_BIT0);
}
#else
#define rt2400pci_rfkill_poll	NULL
#endif /* CONFIG_RT2400PCI_RFKILL */

/*
 * Configuration handlers.
 */
static void rt2400pci_config_mac_addr(struct rt2x00_dev *rt2x00dev,
				      __le32 *mac)
{
	rt2x00pci_register_multiwrite(rt2x00dev, CSR3, mac,
				      (2 * sizeof(__le32)));
}

static void rt2400pci_config_bssid(struct rt2x00_dev *rt2x00dev,
				   __le32 *bssid)
{
	rt2x00pci_register_multiwrite(rt2x00dev, CSR5, bssid,
				      (2 * sizeof(__le32)));
}

static void rt2400pci_config_type(struct rt2x00_dev *rt2x00dev, const int type,
				  const int tsf_sync)
{
	u32 reg;

	rt2x00pci_register_write(rt2x00dev, CSR14, 0);

	/*
	 * Enable beacon config
	 */
	rt2x00pci_register_read(rt2x00dev, BCNCSR1, &reg);
	rt2x00_set_field32(&reg, BCNCSR1_PRELOAD,
			   PREAMBLE + get_duration(IEEE80211_HEADER, 20));
	rt2x00pci_register_write(rt2x00dev, BCNCSR1, reg);

	/*
	 * Enable synchronisation.
	 */
	rt2x00pci_register_read(rt2x00dev, CSR14, &reg);
	rt2x00_set_field32(&reg, CSR14_TSF_COUNT, 1);
	rt2x00_set_field32(&reg, CSR14_TBCN, 1);
	rt2x00_set_field32(&reg, CSR14_BEACON_GEN, 0);
	rt2x00_set_field32(&reg, CSR14_TSF_SYNC, tsf_sync);
	rt2x00pci_register_write(rt2x00dev, CSR14, reg);
}

static void rt2400pci_config_preamble(struct rt2x00_dev *rt2x00dev,
				      const int short_preamble,
				      const int ack_timeout,
				      const int ack_consume_time)
{
	int preamble_mask;
	u32 reg;

	/*
	 * When short preamble is enabled, we should set bit 0x08
	 */
	preamble_mask = short_preamble << 3;

	rt2x00pci_register_read(rt2x00dev, TXCSR1, &reg);
	rt2x00_set_field32(&reg, TXCSR1_ACK_TIMEOUT, ack_timeout);
	rt2x00_set_field32(&reg, TXCSR1_ACK_CONSUME_TIME, ack_consume_time);
	rt2x00pci_register_write(rt2x00dev, TXCSR1, reg);

	rt2x00pci_register_read(rt2x00dev, ARCSR2, &reg);
	rt2x00_set_field32(&reg, ARCSR2_SIGNAL, 0x00 | preamble_mask);
	rt2x00_set_field32(&reg, ARCSR2_SERVICE, 0x04);
	rt2x00_set_field32(&reg, ARCSR2_LENGTH, get_duration(ACK_SIZE, 10));
	rt2x00pci_register_write(rt2x00dev, ARCSR2, reg);

	rt2x00pci_register_read(rt2x00dev, ARCSR3, &reg);
	rt2x00_set_field32(&reg, ARCSR3_SIGNAL, 0x01 | preamble_mask);
	rt2x00_set_field32(&reg, ARCSR3_SERVICE, 0x04);
	rt2x00_set_field32(&reg, ARCSR2_LENGTH, get_duration(ACK_SIZE, 20));
	rt2x00pci_register_write(rt2x00dev, ARCSR3, reg);

	rt2x00pci_register_read(rt2x00dev, ARCSR4, &reg);
	rt2x00_set_field32(&reg, ARCSR4_SIGNAL, 0x02 | preamble_mask);
	rt2x00_set_field32(&reg, ARCSR4_SERVICE, 0x04);
	rt2x00_set_field32(&reg, ARCSR2_LENGTH, get_duration(ACK_SIZE, 55));
	rt2x00pci_register_write(rt2x00dev, ARCSR4, reg);

	rt2x00pci_register_read(rt2x00dev, ARCSR5, &reg);
	rt2x00_set_field32(&reg, ARCSR5_SIGNAL, 0x03 | preamble_mask);
	rt2x00_set_field32(&reg, ARCSR5_SERVICE, 0x84);
	rt2x00_set_field32(&reg, ARCSR2_LENGTH, get_duration(ACK_SIZE, 110));
	rt2x00pci_register_write(rt2x00dev, ARCSR5, reg);
}

static void rt2400pci_config_phymode(struct rt2x00_dev *rt2x00dev,
				     const int basic_rate_mask)
{
	rt2x00pci_register_write(rt2x00dev, ARCSR1, basic_rate_mask);
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
	if (rt2x00_rf(&rt2x00dev->chip, RF2420))
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
	rt2x00pci_register_read(rt2x00dev, CNT0, &rf->rf1);
}

static void rt2400pci_config_txpower(struct rt2x00_dev *rt2x00dev, int txpower)
{
	rt2400pci_bbp_write(rt2x00dev, 3, TXPOWER_TO_DEV(txpower));
}

static void rt2400pci_config_antenna(struct rt2x00_dev *rt2x00dev,
				     int antenna_tx, int antenna_rx)
{
	u8 r1;
	u8 r4;

	rt2400pci_bbp_read(rt2x00dev, 4, &r4);
	rt2400pci_bbp_read(rt2x00dev, 1, &r1);

	/*
	 * Configure the TX antenna.
	 */
	switch (antenna_tx) {
	case ANTENNA_SW_DIVERSITY:
	case ANTENNA_HW_DIVERSITY:
		rt2x00_set_field8(&r1, BBP_R1_TX_ANTENNA, 1);
		break;
	case ANTENNA_A:
		rt2x00_set_field8(&r1, BBP_R1_TX_ANTENNA, 0);
		break;
	case ANTENNA_B:
		rt2x00_set_field8(&r1, BBP_R1_TX_ANTENNA, 2);
		break;
	}

	/*
	 * Configure the RX antenna.
	 */
	switch (antenna_rx) {
	case ANTENNA_SW_DIVERSITY:
	case ANTENNA_HW_DIVERSITY:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 1);
		break;
	case ANTENNA_A:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 0);
		break;
	case ANTENNA_B:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 2);
		break;
	}

	rt2400pci_bbp_write(rt2x00dev, 4, r4);
	rt2400pci_bbp_write(rt2x00dev, 1, r1);
}

static void rt2400pci_config_duration(struct rt2x00_dev *rt2x00dev,
				      struct rt2x00lib_conf *libconf)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, CSR11, &reg);
	rt2x00_set_field32(&reg, CSR11_SLOT_TIME, libconf->slot_time);
	rt2x00pci_register_write(rt2x00dev, CSR11, reg);

	rt2x00pci_register_read(rt2x00dev, CSR18, &reg);
	rt2x00_set_field32(&reg, CSR18_SIFS, libconf->sifs);
	rt2x00_set_field32(&reg, CSR18_PIFS, libconf->pifs);
	rt2x00pci_register_write(rt2x00dev, CSR18, reg);

	rt2x00pci_register_read(rt2x00dev, CSR19, &reg);
	rt2x00_set_field32(&reg, CSR19_DIFS, libconf->difs);
	rt2x00_set_field32(&reg, CSR19_EIFS, libconf->eifs);
	rt2x00pci_register_write(rt2x00dev, CSR19, reg);

	rt2x00pci_register_read(rt2x00dev, TXCSR1, &reg);
	rt2x00_set_field32(&reg, TXCSR1_TSF_OFFSET, IEEE80211_HEADER);
	rt2x00_set_field32(&reg, TXCSR1_AUTORESPONDER, 1);
	rt2x00pci_register_write(rt2x00dev, TXCSR1, reg);

	rt2x00pci_register_read(rt2x00dev, CSR12, &reg);
	rt2x00_set_field32(&reg, CSR12_BEACON_INTERVAL,
			   libconf->conf->beacon_int * 16);
	rt2x00_set_field32(&reg, CSR12_CFP_MAX_DURATION,
			   libconf->conf->beacon_int * 16);
	rt2x00pci_register_write(rt2x00dev, CSR12, reg);
}

static void rt2400pci_config(struct rt2x00_dev *rt2x00dev,
			     const unsigned int flags,
			     struct rt2x00lib_conf *libconf)
{
	if (flags & CONFIG_UPDATE_PHYMODE)
		rt2400pci_config_phymode(rt2x00dev, libconf->basic_rates);
	if (flags & CONFIG_UPDATE_CHANNEL)
		rt2400pci_config_channel(rt2x00dev, &libconf->rf);
	if (flags & CONFIG_UPDATE_TXPOWER)
		rt2400pci_config_txpower(rt2x00dev,
					 libconf->conf->power_level);
	if (flags & CONFIG_UPDATE_ANTENNA)
		rt2400pci_config_antenna(rt2x00dev,
					 libconf->conf->antenna_sel_tx,
					 libconf->conf->antenna_sel_rx);
	if (flags & (CONFIG_UPDATE_SLOT_TIME | CONFIG_UPDATE_BEACON_INT))
		rt2400pci_config_duration(rt2x00dev, libconf);
}

static void rt2400pci_config_cw(struct rt2x00_dev *rt2x00dev,
				struct ieee80211_tx_queue_params *params)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, CSR11, &reg);
	rt2x00_set_field32(&reg, CSR11_CWMIN, params->cw_min);
	rt2x00_set_field32(&reg, CSR11_CWMAX, params->cw_max);
	rt2x00pci_register_write(rt2x00dev, CSR11, reg);
}

/*
 * LED functions.
 */
static void rt2400pci_enable_led(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, LEDCSR, &reg);

	rt2x00_set_field32(&reg, LEDCSR_ON_PERIOD, 70);
	rt2x00_set_field32(&reg, LEDCSR_OFF_PERIOD, 30);

	if (rt2x00dev->led_mode == LED_MODE_TXRX_ACTIVITY) {
		rt2x00_set_field32(&reg, LEDCSR_LINK, 1);
		rt2x00_set_field32(&reg, LEDCSR_ACTIVITY, 0);
	} else if (rt2x00dev->led_mode == LED_MODE_ASUS) {
		rt2x00_set_field32(&reg, LEDCSR_LINK, 0);
		rt2x00_set_field32(&reg, LEDCSR_ACTIVITY, 1);
	} else {
		rt2x00_set_field32(&reg, LEDCSR_LINK, 1);
		rt2x00_set_field32(&reg, LEDCSR_ACTIVITY, 1);
	}

	rt2x00pci_register_write(rt2x00dev, LEDCSR, reg);
}

static void rt2400pci_disable_led(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, LEDCSR, &reg);
	rt2x00_set_field32(&reg, LEDCSR_LINK, 0);
	rt2x00_set_field32(&reg, LEDCSR_ACTIVITY, 0);
	rt2x00pci_register_write(rt2x00dev, LEDCSR, reg);
}

/*
 * Link tuning
 */
static void rt2400pci_link_stats(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	u8 bbp;

	/*
	 * Update FCS error count from register.
	 */
	rt2x00pci_register_read(rt2x00dev, CNT0, &reg);
	rt2x00dev->link.rx_failed = rt2x00_get_field32(reg, CNT0_FCS_ERROR);

	/*
	 * Update False CCA count from register.
	 */
	rt2400pci_bbp_read(rt2x00dev, 39, &bbp);
	rt2x00dev->link.false_cca = bbp;
}

static void rt2400pci_reset_tuner(struct rt2x00_dev *rt2x00dev)
{
	rt2400pci_bbp_write(rt2x00dev, 13, 0x08);
	rt2x00dev->link.vgc_level = 0x08;
}

static void rt2400pci_link_tuner(struct rt2x00_dev *rt2x00dev)
{
	u8 reg;

	/*
	 * The link tuner should not run longer then 60 seconds,
	 * and should run once every 2 seconds.
	 */
	if (rt2x00dev->link.count > 60 || !(rt2x00dev->link.count & 1))
		return;

	/*
	 * Base r13 link tuning on the false cca count.
	 */
	rt2400pci_bbp_read(rt2x00dev, 13, &reg);

	if (rt2x00dev->link.false_cca > 512 && reg < 0x20) {
		rt2400pci_bbp_write(rt2x00dev, 13, ++reg);
		rt2x00dev->link.vgc_level = reg;
	} else if (rt2x00dev->link.false_cca < 100 && reg > 0x08) {
		rt2400pci_bbp_write(rt2x00dev, 13, --reg);
		rt2x00dev->link.vgc_level = reg;
	}
}

/*
 * Initialization functions.
 */
static void rt2400pci_init_rxring(struct rt2x00_dev *rt2x00dev)
{
	struct data_ring *ring = rt2x00dev->rx;
	struct data_desc *rxd;
	unsigned int i;
	u32 word;

	memset(ring->data_addr, 0x00, rt2x00_get_ring_size(ring));

	for (i = 0; i < ring->stats.limit; i++) {
		rxd = ring->entry[i].priv;

		rt2x00_desc_read(rxd, 2, &word);
		rt2x00_set_field32(&word, RXD_W2_BUFFER_LENGTH,
				   ring->data_size);
		rt2x00_desc_write(rxd, 2, word);

		rt2x00_desc_read(rxd, 1, &word);
		rt2x00_set_field32(&word, RXD_W1_BUFFER_ADDRESS,
				   ring->entry[i].data_dma);
		rt2x00_desc_write(rxd, 1, word);

		rt2x00_desc_read(rxd, 0, &word);
		rt2x00_set_field32(&word, RXD_W0_OWNER_NIC, 1);
		rt2x00_desc_write(rxd, 0, word);
	}

	rt2x00_ring_index_clear(rt2x00dev->rx);
}

static void rt2400pci_init_txring(struct rt2x00_dev *rt2x00dev, const int queue)
{
	struct data_ring *ring = rt2x00lib_get_ring(rt2x00dev, queue);
	struct data_desc *txd;
	unsigned int i;
	u32 word;

	memset(ring->data_addr, 0x00, rt2x00_get_ring_size(ring));

	for (i = 0; i < ring->stats.limit; i++) {
		txd = ring->entry[i].priv;

		rt2x00_desc_read(txd, 1, &word);
		rt2x00_set_field32(&word, TXD_W1_BUFFER_ADDRESS,
				   ring->entry[i].data_dma);
		rt2x00_desc_write(txd, 1, word);

		rt2x00_desc_read(txd, 2, &word);
		rt2x00_set_field32(&word, TXD_W2_BUFFER_LENGTH,
				   ring->data_size);
		rt2x00_desc_write(txd, 2, word);

		rt2x00_desc_read(txd, 0, &word);
		rt2x00_set_field32(&word, TXD_W0_VALID, 0);
		rt2x00_set_field32(&word, TXD_W0_OWNER_NIC, 0);
		rt2x00_desc_write(txd, 0, word);
	}

	rt2x00_ring_index_clear(ring);
}

static int rt2400pci_init_rings(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	/*
	 * Initialize rings.
	 */
	rt2400pci_init_rxring(rt2x00dev);
	rt2400pci_init_txring(rt2x00dev, IEEE80211_TX_QUEUE_DATA0);
	rt2400pci_init_txring(rt2x00dev, IEEE80211_TX_QUEUE_DATA1);
	rt2400pci_init_txring(rt2x00dev, IEEE80211_TX_QUEUE_AFTER_BEACON);
	rt2400pci_init_txring(rt2x00dev, IEEE80211_TX_QUEUE_BEACON);

	/*
	 * Initialize registers.
	 */
	rt2x00pci_register_read(rt2x00dev, TXCSR2, &reg);
	rt2x00_set_field32(&reg, TXCSR2_TXD_SIZE,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA0].desc_size);
	rt2x00_set_field32(&reg, TXCSR2_NUM_TXD,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA1].stats.limit);
	rt2x00_set_field32(&reg, TXCSR2_NUM_ATIM,
			   rt2x00dev->bcn[1].stats.limit);
	rt2x00_set_field32(&reg, TXCSR2_NUM_PRIO,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA0].stats.limit);
	rt2x00pci_register_write(rt2x00dev, TXCSR2, reg);

	rt2x00pci_register_read(rt2x00dev, TXCSR3, &reg);
	rt2x00_set_field32(&reg, TXCSR3_TX_RING_REGISTER,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA1].data_dma);
	rt2x00pci_register_write(rt2x00dev, TXCSR3, reg);

	rt2x00pci_register_read(rt2x00dev, TXCSR5, &reg);
	rt2x00_set_field32(&reg, TXCSR5_PRIO_RING_REGISTER,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA0].data_dma);
	rt2x00pci_register_write(rt2x00dev, TXCSR5, reg);

	rt2x00pci_register_read(rt2x00dev, TXCSR4, &reg);
	rt2x00_set_field32(&reg, TXCSR4_ATIM_RING_REGISTER,
			   rt2x00dev->bcn[1].data_dma);
	rt2x00pci_register_write(rt2x00dev, TXCSR4, reg);

	rt2x00pci_register_read(rt2x00dev, TXCSR6, &reg);
	rt2x00_set_field32(&reg, TXCSR6_BEACON_RING_REGISTER,
			   rt2x00dev->bcn[0].data_dma);
	rt2x00pci_register_write(rt2x00dev, TXCSR6, reg);

	rt2x00pci_register_read(rt2x00dev, RXCSR1, &reg);
	rt2x00_set_field32(&reg, RXCSR1_RXD_SIZE, rt2x00dev->rx->desc_size);
	rt2x00_set_field32(&reg, RXCSR1_NUM_RXD, rt2x00dev->rx->stats.limit);
	rt2x00pci_register_write(rt2x00dev, RXCSR1, reg);

	rt2x00pci_register_read(rt2x00dev, RXCSR2, &reg);
	rt2x00_set_field32(&reg, RXCSR2_RX_RING_REGISTER,
			   rt2x00dev->rx->data_dma);
	rt2x00pci_register_write(rt2x00dev, RXCSR2, reg);

	return 0;
}

static int rt2400pci_init_registers(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2x00pci_register_write(rt2x00dev, PSCSR0, 0x00020002);
	rt2x00pci_register_write(rt2x00dev, PSCSR1, 0x00000002);
	rt2x00pci_register_write(rt2x00dev, PSCSR2, 0x00023f20);
	rt2x00pci_register_write(rt2x00dev, PSCSR3, 0x00000002);

	rt2x00pci_register_read(rt2x00dev, TIMECSR, &reg);
	rt2x00_set_field32(&reg, TIMECSR_US_COUNT, 33);
	rt2x00_set_field32(&reg, TIMECSR_US_64_COUNT, 63);
	rt2x00_set_field32(&reg, TIMECSR_BEACON_EXPECT, 0);
	rt2x00pci_register_write(rt2x00dev, TIMECSR, reg);

	rt2x00pci_register_read(rt2x00dev, CSR9, &reg);
	rt2x00_set_field32(&reg, CSR9_MAX_FRAME_UNIT,
			   (rt2x00dev->rx->data_size / 128));
	rt2x00pci_register_write(rt2x00dev, CSR9, reg);

	rt2x00pci_register_write(rt2x00dev, CNT3, 0x3f080000);

	rt2x00pci_register_read(rt2x00dev, ARCSR0, &reg);
	rt2x00_set_field32(&reg, ARCSR0_AR_BBP_DATA0, 133);
	rt2x00_set_field32(&reg, ARCSR0_AR_BBP_ID0, 134);
	rt2x00_set_field32(&reg, ARCSR0_AR_BBP_DATA1, 136);
	rt2x00_set_field32(&reg, ARCSR0_AR_BBP_ID1, 135);
	rt2x00pci_register_write(rt2x00dev, ARCSR0, reg);

	rt2x00pci_register_read(rt2x00dev, RXCSR3, &reg);
	rt2x00_set_field32(&reg, RXCSR3_BBP_ID0, 3); /* Tx power.*/
	rt2x00_set_field32(&reg, RXCSR3_BBP_ID0_VALID, 1);
	rt2x00_set_field32(&reg, RXCSR3_BBP_ID1, 32); /* Signal */
	rt2x00_set_field32(&reg, RXCSR3_BBP_ID1_VALID, 1);
	rt2x00_set_field32(&reg, RXCSR3_BBP_ID2, 36); /* Rssi */
	rt2x00_set_field32(&reg, RXCSR3_BBP_ID2_VALID, 1);
	rt2x00pci_register_write(rt2x00dev, RXCSR3, reg);

	rt2x00pci_register_write(rt2x00dev, PWRCSR0, 0x3f3b3100);

	if (rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_AWAKE))
		return -EBUSY;

	rt2x00pci_register_write(rt2x00dev, MACCSR0, 0x00217223);
	rt2x00pci_register_write(rt2x00dev, MACCSR1, 0x00235518);

	rt2x00pci_register_read(rt2x00dev, MACCSR2, &reg);
	rt2x00_set_field32(&reg, MACCSR2_DELAY, 64);
	rt2x00pci_register_write(rt2x00dev, MACCSR2, reg);

	rt2x00pci_register_read(rt2x00dev, RALINKCSR, &reg);
	rt2x00_set_field32(&reg, RALINKCSR_AR_BBP_DATA0, 17);
	rt2x00_set_field32(&reg, RALINKCSR_AR_BBP_ID0, 154);
	rt2x00_set_field32(&reg, RALINKCSR_AR_BBP_DATA1, 0);
	rt2x00_set_field32(&reg, RALINKCSR_AR_BBP_ID1, 154);
	rt2x00pci_register_write(rt2x00dev, RALINKCSR, reg);

	rt2x00pci_register_read(rt2x00dev, CSR1, &reg);
	rt2x00_set_field32(&reg, CSR1_SOFT_RESET, 1);
	rt2x00_set_field32(&reg, CSR1_BBP_RESET, 0);
	rt2x00_set_field32(&reg, CSR1_HOST_READY, 0);
	rt2x00pci_register_write(rt2x00dev, CSR1, reg);

	rt2x00pci_register_read(rt2x00dev, CSR1, &reg);
	rt2x00_set_field32(&reg, CSR1_SOFT_RESET, 0);
	rt2x00_set_field32(&reg, CSR1_HOST_READY, 1);
	rt2x00pci_register_write(rt2x00dev, CSR1, reg);

	/*
	 * We must clear the FCS and FIFO error count.
	 * These registers are cleared on read,
	 * so we may pass a useless variable to store the value.
	 */
	rt2x00pci_register_read(rt2x00dev, CNT0, &reg);
	rt2x00pci_register_read(rt2x00dev, CNT4, &reg);

	return 0;
}

static int rt2400pci_init_bbp(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u16 eeprom;
	u8 reg_id;
	u8 value;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2400pci_bbp_read(rt2x00dev, 0, &value);
		if ((value != 0xff) && (value != 0x00))
			goto continue_csr_init;
		NOTICE(rt2x00dev, "Waiting for BBP register.\n");
		udelay(REGISTER_BUSY_DELAY);
	}

	ERROR(rt2x00dev, "BBP register access failed, aborting.\n");
	return -EACCES;

continue_csr_init:
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

	DEBUG(rt2x00dev, "Start initialization from EEPROM...\n");
	for (i = 0; i < EEPROM_BBP_SIZE; i++) {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_BBP_START + i, &eeprom);

		if (eeprom != 0xffff && eeprom != 0x0000) {
			reg_id = rt2x00_get_field16(eeprom, EEPROM_BBP_REG_ID);
			value = rt2x00_get_field16(eeprom, EEPROM_BBP_VALUE);
			DEBUG(rt2x00dev, "BBP: 0x%02x, value: 0x%02x.\n",
			      reg_id, value);
			rt2400pci_bbp_write(rt2x00dev, reg_id, value);
		}
	}
	DEBUG(rt2x00dev, "...End initialization from EEPROM.\n");

	return 0;
}

/*
 * Device state switch handlers.
 */
static void rt2400pci_toggle_rx(struct rt2x00_dev *rt2x00dev,
				enum dev_state state)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, RXCSR0, &reg);
	rt2x00_set_field32(&reg, RXCSR0_DISABLE_RX,
			   state == STATE_RADIO_RX_OFF);
	rt2x00pci_register_write(rt2x00dev, RXCSR0, reg);
}

static void rt2400pci_toggle_irq(struct rt2x00_dev *rt2x00dev,
				 enum dev_state state)
{
	int mask = (state == STATE_RADIO_IRQ_OFF);
	u32 reg;

	/*
	 * When interrupts are being enabled, the interrupt registers
	 * should clear the register to assure a clean state.
	 */
	if (state == STATE_RADIO_IRQ_ON) {
		rt2x00pci_register_read(rt2x00dev, CSR7, &reg);
		rt2x00pci_register_write(rt2x00dev, CSR7, reg);
	}

	/*
	 * Only toggle the interrupts bits we are going to use.
	 * Non-checked interrupt bits are disabled by default.
	 */
	rt2x00pci_register_read(rt2x00dev, CSR8, &reg);
	rt2x00_set_field32(&reg, CSR8_TBCN_EXPIRE, mask);
	rt2x00_set_field32(&reg, CSR8_TXDONE_TXRING, mask);
	rt2x00_set_field32(&reg, CSR8_TXDONE_ATIMRING, mask);
	rt2x00_set_field32(&reg, CSR8_TXDONE_PRIORING, mask);
	rt2x00_set_field32(&reg, CSR8_RXDONE, mask);
	rt2x00pci_register_write(rt2x00dev, CSR8, reg);
}

static int rt2400pci_enable_radio(struct rt2x00_dev *rt2x00dev)
{
	/*
	 * Initialize all registers.
	 */
	if (rt2400pci_init_rings(rt2x00dev) ||
	    rt2400pci_init_registers(rt2x00dev) ||
	    rt2400pci_init_bbp(rt2x00dev)) {
		ERROR(rt2x00dev, "Register initialization failed.\n");
		return -EIO;
	}

	/*
	 * Enable interrupts.
	 */
	rt2400pci_toggle_irq(rt2x00dev, STATE_RADIO_IRQ_ON);

	/*
	 * Enable LED
	 */
	rt2400pci_enable_led(rt2x00dev);

	return 0;
}

static void rt2400pci_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	/*
	 * Disable LED
	 */
	rt2400pci_disable_led(rt2x00dev);

	rt2x00pci_register_write(rt2x00dev, PWRCSR0, 0);

	/*
	 * Disable synchronisation.
	 */
	rt2x00pci_register_write(rt2x00dev, CSR14, 0);

	/*
	 * Cancel RX and TX.
	 */
	rt2x00pci_register_read(rt2x00dev, TXCSR0, &reg);
	rt2x00_set_field32(&reg, TXCSR0_ABORT, 1);
	rt2x00pci_register_write(rt2x00dev, TXCSR0, reg);

	/*
	 * Disable interrupts.
	 */
	rt2400pci_toggle_irq(rt2x00dev, STATE_RADIO_IRQ_OFF);
}

static int rt2400pci_set_state(struct rt2x00_dev *rt2x00dev,
			       enum dev_state state)
{
	u32 reg;
	unsigned int i;
	char put_to_sleep;
	char bbp_state;
	char rf_state;

	put_to_sleep = (state != STATE_AWAKE);

	rt2x00pci_register_read(rt2x00dev, PWRCSR1, &reg);
	rt2x00_set_field32(&reg, PWRCSR1_SET_STATE, 1);
	rt2x00_set_field32(&reg, PWRCSR1_BBP_DESIRE_STATE, state);
	rt2x00_set_field32(&reg, PWRCSR1_RF_DESIRE_STATE, state);
	rt2x00_set_field32(&reg, PWRCSR1_PUT_TO_SLEEP, put_to_sleep);
	rt2x00pci_register_write(rt2x00dev, PWRCSR1, reg);

	/*
	 * Device is not guaranteed to be in the requested state yet.
	 * We must wait until the register indicates that the
	 * device has entered the correct state.
	 */
	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2x00pci_register_read(rt2x00dev, PWRCSR1, &reg);
		bbp_state = rt2x00_get_field32(reg, PWRCSR1_BBP_CURR_STATE);
		rf_state = rt2x00_get_field32(reg, PWRCSR1_RF_CURR_STATE);
		if (bbp_state == state && rf_state == state)
			return 0;
		msleep(10);
	}

	NOTICE(rt2x00dev, "Device failed to enter state %d, "
	       "current device state: bbp %d and rf %d.\n",
	       state, bbp_state, rf_state);

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
	case STATE_RADIO_RX_ON:
	case STATE_RADIO_RX_OFF:
		rt2400pci_toggle_rx(rt2x00dev, state);
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

	return retval;
}

/*
 * TX descriptor initialization
 */
static void rt2400pci_write_tx_desc(struct rt2x00_dev *rt2x00dev,
				    struct data_desc *txd,
				    struct txdata_entry_desc *desc,
				    struct ieee80211_hdr *ieee80211hdr,
				    unsigned int length,
				    struct ieee80211_tx_control *control)
{
	u32 word;
	u32 signal = 0;
	u32 service = 0;
	u32 length_high = 0;
	u32 length_low = 0;

	/*
	 * The PLCP values should be treated as if they
	 * were BBP values.
	 */
	rt2x00_set_field32(&signal, BBPCSR_VALUE, desc->signal);
	rt2x00_set_field32(&signal, BBPCSR_REGNUM, 5);
	rt2x00_set_field32(&signal, BBPCSR_BUSY, 1);

	rt2x00_set_field32(&service, BBPCSR_VALUE, desc->service);
	rt2x00_set_field32(&service, BBPCSR_REGNUM, 6);
	rt2x00_set_field32(&service, BBPCSR_BUSY, 1);

	rt2x00_set_field32(&length_high, BBPCSR_VALUE, desc->length_high);
	rt2x00_set_field32(&length_high, BBPCSR_REGNUM, 7);
	rt2x00_set_field32(&length_high, BBPCSR_BUSY, 1);

	rt2x00_set_field32(&length_low, BBPCSR_VALUE, desc->length_low);
	rt2x00_set_field32(&length_low, BBPCSR_REGNUM, 8);
	rt2x00_set_field32(&length_low, BBPCSR_BUSY, 1);

	/*
	 * Start writing the descriptor words.
	 */
	rt2x00_desc_read(txd, 2, &word);
	rt2x00_set_field32(&word, TXD_W2_DATABYTE_COUNT, length);
	rt2x00_desc_write(txd, 2, word);

	rt2x00_desc_read(txd, 3, &word);
	rt2x00_set_field32(&word, TXD_W3_PLCP_SIGNAL, signal);
	rt2x00_set_field32(&word, TXD_W3_PLCP_SERVICE, service);
	rt2x00_desc_write(txd, 3, word);

	rt2x00_desc_read(txd, 4, &word);
	rt2x00_set_field32(&word, TXD_W4_PLCP_LENGTH_LOW, length_low);
	rt2x00_set_field32(&word, TXD_W4_PLCP_LENGTH_HIGH, length_high);
	rt2x00_desc_write(txd, 4, word);

	rt2x00_desc_read(txd, 0, &word);
	rt2x00_set_field32(&word, TXD_W0_OWNER_NIC, 1);
	rt2x00_set_field32(&word, TXD_W0_VALID, 1);
	rt2x00_set_field32(&word, TXD_W0_MORE_FRAG,
			   test_bit(ENTRY_TXD_MORE_FRAG, &desc->flags));
	rt2x00_set_field32(&word, TXD_W0_ACK,
			   !(control->flags & IEEE80211_TXCTL_NO_ACK));
	rt2x00_set_field32(&word, TXD_W0_TIMESTAMP,
			   test_bit(ENTRY_TXD_REQ_TIMESTAMP, &desc->flags));
	rt2x00_set_field32(&word, TXD_W0_RTS,
			   test_bit(ENTRY_TXD_RTS_FRAME, &desc->flags));
	rt2x00_set_field32(&word, TXD_W0_IFS, desc->ifs);
	rt2x00_set_field32(&word, TXD_W0_RETRY_MODE,
			   !!(control->flags &
			      IEEE80211_TXCTL_LONG_RETRY_LIMIT));
	rt2x00_desc_write(txd, 0, word);
}

/*
 * TX data initialization
 */
static void rt2400pci_kick_tx_queue(struct rt2x00_dev *rt2x00dev,
				    unsigned int queue)
{
	u32 reg;

	if (queue == IEEE80211_TX_QUEUE_BEACON) {
		rt2x00pci_register_read(rt2x00dev, CSR14, &reg);
		if (!rt2x00_get_field32(reg, CSR14_BEACON_GEN)) {
			rt2x00_set_field32(&reg, CSR14_BEACON_GEN, 1);
			rt2x00pci_register_write(rt2x00dev, CSR14, reg);
		}
		return;
	}

	rt2x00pci_register_read(rt2x00dev, TXCSR0, &reg);
	if (queue == IEEE80211_TX_QUEUE_DATA0)
		rt2x00_set_field32(&reg, TXCSR0_KICK_PRIO, 1);
	else if (queue == IEEE80211_TX_QUEUE_DATA1)
		rt2x00_set_field32(&reg, TXCSR0_KICK_TX, 1);
	else if (queue == IEEE80211_TX_QUEUE_AFTER_BEACON)
		rt2x00_set_field32(&reg, TXCSR0_KICK_ATIM, 1);
	rt2x00pci_register_write(rt2x00dev, TXCSR0, reg);
}

/*
 * RX control handlers
 */
static void rt2400pci_fill_rxdone(struct data_entry *entry,
				  struct rxdata_entry_desc *desc)
{
	struct data_desc *rxd = entry->priv;
	u32 word0;
	u32 word2;

	rt2x00_desc_read(rxd, 0, &word0);
	rt2x00_desc_read(rxd, 2, &word2);

	desc->flags = 0;
	if (rt2x00_get_field32(word0, RXD_W0_CRC_ERROR))
		desc->flags |= RX_FLAG_FAILED_FCS_CRC;
	if (rt2x00_get_field32(word0, RXD_W0_PHYSICAL_ERROR))
		desc->flags |= RX_FLAG_FAILED_PLCP_CRC;

	/*
	 * Obtain the status about this packet.
	 */
	desc->signal = rt2x00_get_field32(word2, RXD_W2_SIGNAL);
	desc->rssi = rt2x00_get_field32(word2, RXD_W2_RSSI) -
	    entry->ring->rt2x00dev->rssi_offset;
	desc->ofdm = 0;
	desc->size = rt2x00_get_field32(word0, RXD_W0_DATABYTE_COUNT);
}

/*
 * Interrupt functions.
 */
static void rt2400pci_txdone(struct rt2x00_dev *rt2x00dev, const int queue)
{
	struct data_ring *ring = rt2x00lib_get_ring(rt2x00dev, queue);
	struct data_entry *entry;
	struct data_desc *txd;
	u32 word;
	int tx_status;
	int retry;

	while (!rt2x00_ring_empty(ring)) {
		entry = rt2x00_get_data_entry_done(ring);
		txd = entry->priv;
		rt2x00_desc_read(txd, 0, &word);

		if (rt2x00_get_field32(word, TXD_W0_OWNER_NIC) ||
		    !rt2x00_get_field32(word, TXD_W0_VALID))
			break;

		/*
		 * Obtain the status about this packet.
		 */
		tx_status = rt2x00_get_field32(word, TXD_W0_RESULT);
		retry = rt2x00_get_field32(word, TXD_W0_RETRY_COUNT);

		rt2x00lib_txdone(entry, tx_status, retry);

		/*
		 * Make this entry available for reuse.
		 */
		entry->flags = 0;
		rt2x00_set_field32(&word, TXD_W0_VALID, 0);
		rt2x00_desc_write(txd, 0, word);
		rt2x00_ring_index_done_inc(ring);
	}

	/*
	 * If the data ring was full before the txdone handler
	 * we must make sure the packet queue in the mac80211 stack
	 * is reenabled when the txdone handler has finished.
	 */
	entry = ring->entry;
	if (!rt2x00_ring_full(ring))
		ieee80211_wake_queue(rt2x00dev->hw,
				     entry->tx_status.control.queue);
}

static irqreturn_t rt2400pci_interrupt(int irq, void *dev_instance)
{
	struct rt2x00_dev *rt2x00dev = dev_instance;
	u32 reg;

	/*
	 * Get the interrupt sources & saved to local variable.
	 * Write register value back to clear pending interrupts.
	 */
	rt2x00pci_register_read(rt2x00dev, CSR7, &reg);
	rt2x00pci_register_write(rt2x00dev, CSR7, reg);

	if (!reg)
		return IRQ_NONE;

	if (!test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags))
		return IRQ_HANDLED;

	/*
	 * Handle interrupts, walk through all bits
	 * and run the tasks, the bits are checked in order of
	 * priority.
	 */

	/*
	 * 1 - Beacon timer expired interrupt.
	 */
	if (rt2x00_get_field32(reg, CSR7_TBCN_EXPIRE))
		rt2x00lib_beacondone(rt2x00dev);

	/*
	 * 2 - Rx ring done interrupt.
	 */
	if (rt2x00_get_field32(reg, CSR7_RXDONE))
		rt2x00pci_rxdone(rt2x00dev);

	/*
	 * 3 - Atim ring transmit done interrupt.
	 */
	if (rt2x00_get_field32(reg, CSR7_TXDONE_ATIMRING))
		rt2400pci_txdone(rt2x00dev, IEEE80211_TX_QUEUE_AFTER_BEACON);

	/*
	 * 4 - Priority ring transmit done interrupt.
	 */
	if (rt2x00_get_field32(reg, CSR7_TXDONE_PRIORING))
		rt2400pci_txdone(rt2x00dev, IEEE80211_TX_QUEUE_DATA0);

	/*
	 * 5 - Tx ring transmit done interrupt.
	 */
	if (rt2x00_get_field32(reg, CSR7_TXDONE_TXRING))
		rt2400pci_txdone(rt2x00dev, IEEE80211_TX_QUEUE_DATA1);

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

	rt2x00pci_register_read(rt2x00dev, CSR21, &reg);

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
	if (!is_valid_ether_addr(mac)) {
		DECLARE_MAC_BUF(macbuf);

		random_ether_addr(mac);
		EEPROM(rt2x00dev, "MAC: %s\n", print_mac(macbuf, mac));
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_ANTENNA, &word);
	if (word == 0xffff) {
		ERROR(rt2x00dev, "Invalid EEPROM data detected.\n");
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
	rt2x00_eeprom_read(rt2x00dev, EEPROM_ANTENNA, &eeprom);

	/*
	 * Identify RF chipset.
	 */
	value = rt2x00_get_field16(eeprom, EEPROM_ANTENNA_RF_TYPE);
	rt2x00pci_register_read(rt2x00dev, CSR0, &reg);
	rt2x00_set_chip(rt2x00dev, RT2460, value, reg);

	if (!rt2x00_rf(&rt2x00dev->chip, RF2420) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF2421)) {
		ERROR(rt2x00dev, "Invalid RF chipset detected.\n");
		return -ENODEV;
	}

	/*
	 * Identify default antenna configuration.
	 */
	rt2x00dev->hw->conf.antenna_sel_tx =
	    rt2x00_get_field16(eeprom, EEPROM_ANTENNA_TX_DEFAULT);
	rt2x00dev->hw->conf.antenna_sel_rx =
	    rt2x00_get_field16(eeprom, EEPROM_ANTENNA_RX_DEFAULT);

	/*
	 * Store led mode, for correct led behaviour.
	 */
	rt2x00dev->led_mode =
	    rt2x00_get_field16(eeprom, EEPROM_ANTENNA_LED_MODE);

	/*
	 * Detect if this device has an hardware controlled radio.
	 */
#ifdef CONFIG_RT2400PCI_RFKILL
	if (rt2x00_get_field16(eeprom, EEPROM_ANTENNA_HARDWARE_RADIO))
		__set_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags);
#endif /* CONFIG_RT2400PCI_RFKILL */

	/*
	 * Check if the BBP tuning should be enabled.
	 */
	if (!rt2x00_get_field16(eeprom, EEPROM_ANTENNA_RX_AGCVGC_TUNING))
		__set_bit(CONFIG_DISABLE_LINK_TUNING, &rt2x00dev->flags);

	return 0;
}

/*
 * RF value list for RF2420 & RF2421
 * Supports: 2.4 GHz
 */
static const struct rf_channel rf_vals_bg[] = {
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

static void rt2400pci_probe_hw_mode(struct rt2x00_dev *rt2x00dev)
{
	struct hw_mode_spec *spec = &rt2x00dev->spec;
	u8 *txpower;
	unsigned int i;

	/*
	 * Initialize all hw fields.
	 */
	rt2x00dev->hw->flags = IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING;
	rt2x00dev->hw->extra_tx_headroom = 0;
	rt2x00dev->hw->max_signal = MAX_SIGNAL;
	rt2x00dev->hw->max_rssi = MAX_RX_SSI;
	rt2x00dev->hw->queues = 2;

	SET_IEEE80211_DEV(rt2x00dev->hw, &rt2x00dev_pci(rt2x00dev)->dev);
	SET_IEEE80211_PERM_ADDR(rt2x00dev->hw,
				rt2x00_eeprom_addr(rt2x00dev,
						   EEPROM_MAC_ADDR_0));

	/*
	 * Convert tx_power array in eeprom.
	 */
	txpower = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_START);
	for (i = 0; i < 14; i++)
		txpower[i] = TXPOWER_FROM_DEV(txpower[i]);

	/*
	 * Initialize hw_mode information.
	 */
	spec->num_modes = 1;
	spec->num_rates = 4;
	spec->tx_power_a = NULL;
	spec->tx_power_bg = txpower;
	spec->tx_power_default = DEFAULT_TXPOWER;

	spec->num_channels = ARRAY_SIZE(rf_vals_bg);
	spec->channels = rf_vals_bg;
}

static int rt2400pci_probe_hw(struct rt2x00_dev *rt2x00dev)
{
	int retval;

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
	 * Initialize hw specifications.
	 */
	rt2400pci_probe_hw_mode(rt2x00dev);

	/*
	 * This device requires the beacon ring
	 */
	__set_bit(DRIVER_REQUIRE_BEACON_RING, &rt2x00dev->flags);

	/*
	 * Set the rssi offset.
	 */
	rt2x00dev->rssi_offset = DEFAULT_RSSI_OFFSET;

	return 0;
}

/*
 * IEEE80211 stack callback functions.
 */
static void rt2400pci_configure_filter(struct ieee80211_hw *hw,
				       unsigned int changed_flags,
				       unsigned int *total_flags,
				       int mc_count,
				       struct dev_addr_list *mc_list)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct interface *intf = &rt2x00dev->interface;
	u32 reg;

	/*
	 * Mask off any flags we are going to ignore from
	 * the total_flags field.
	 */
	*total_flags &=
	    FIF_ALLMULTI |
	    FIF_FCSFAIL |
	    FIF_PLCPFAIL |
	    FIF_CONTROL |
	    FIF_OTHER_BSS |
	    FIF_PROMISC_IN_BSS;

	/*
	 * Apply some rules to the filters:
	 * - Some filters imply different filters to be set.
	 * - Some things we can't filter out at all.
	 * - Some filters are set based on interface type.
	 */
	*total_flags |= FIF_ALLMULTI;
	if (*total_flags & FIF_OTHER_BSS ||
	    *total_flags & FIF_PROMISC_IN_BSS)
		*total_flags |= FIF_PROMISC_IN_BSS | FIF_OTHER_BSS;
	if (is_interface_type(intf, IEEE80211_IF_TYPE_AP))
		*total_flags |= FIF_PROMISC_IN_BSS;

	/*
	 * Check if there is any work left for us.
	 */
	if (intf->filter == *total_flags)
		return;
	intf->filter = *total_flags;

	/*
	 * Start configuration steps.
	 * Note that the version error will always be dropped
	 * since there is no filter for it at this time.
	 */
	rt2x00pci_register_read(rt2x00dev, RXCSR0, &reg);
	rt2x00_set_field32(&reg, RXCSR0_DROP_CRC,
			   !(*total_flags & FIF_FCSFAIL));
	rt2x00_set_field32(&reg, RXCSR0_DROP_PHYSICAL,
			   !(*total_flags & FIF_PLCPFAIL));
	rt2x00_set_field32(&reg, RXCSR0_DROP_CONTROL,
			   !(*total_flags & FIF_CONTROL));
	rt2x00_set_field32(&reg, RXCSR0_DROP_NOT_TO_ME,
			   !(*total_flags & FIF_PROMISC_IN_BSS));
	rt2x00_set_field32(&reg, RXCSR0_DROP_TODS,
			   !(*total_flags & FIF_PROMISC_IN_BSS));
	rt2x00_set_field32(&reg, RXCSR0_DROP_VERSION_ERROR, 1);
	rt2x00pci_register_write(rt2x00dev, RXCSR0, reg);
}

static int rt2400pci_set_retry_limit(struct ieee80211_hw *hw,
				     u32 short_retry, u32 long_retry)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, CSR11, &reg);
	rt2x00_set_field32(&reg, CSR11_LONG_RETRY, long_retry);
	rt2x00_set_field32(&reg, CSR11_SHORT_RETRY, short_retry);
	rt2x00pci_register_write(rt2x00dev, CSR11, reg);

	return 0;
}

static int rt2400pci_conf_tx(struct ieee80211_hw *hw,
			     int queue,
			     const struct ieee80211_tx_queue_params *params)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	/*
	 * We don't support variating cw_min and cw_max variables
	 * per queue. So by default we only configure the TX queue,
	 * and ignore all other configurations.
	 */
	if (queue != IEEE80211_TX_QUEUE_DATA0)
		return -EINVAL;

	if (rt2x00mac_conf_tx(hw, queue, params))
		return -EINVAL;

	/*
	 * Write configuration to register.
	 */
	rt2400pci_config_cw(rt2x00dev, &rt2x00dev->tx->tx_params);

	return 0;
}

static u64 rt2400pci_get_tsf(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	u64 tsf;
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, CSR17, &reg);
	tsf = (u64) rt2x00_get_field32(reg, CSR17_HIGH_TSFTIMER) << 32;
	rt2x00pci_register_read(rt2x00dev, CSR16, &reg);
	tsf |= rt2x00_get_field32(reg, CSR16_LOW_TSFTIMER);

	return tsf;
}

static void rt2400pci_reset_tsf(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	rt2x00pci_register_write(rt2x00dev, CSR16, 0);
	rt2x00pci_register_write(rt2x00dev, CSR17, 0);
}

static int rt2400pci_tx_last_beacon(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, CSR15, &reg);
	return rt2x00_get_field32(reg, CSR15_BEACON_SENT);
}

static const struct ieee80211_ops rt2400pci_mac80211_ops = {
	.tx			= rt2x00mac_tx,
	.start			= rt2x00mac_start,
	.stop			= rt2x00mac_stop,
	.add_interface		= rt2x00mac_add_interface,
	.remove_interface	= rt2x00mac_remove_interface,
	.config			= rt2x00mac_config,
	.config_interface	= rt2x00mac_config_interface,
	.configure_filter	= rt2400pci_configure_filter,
	.get_stats		= rt2x00mac_get_stats,
	.set_retry_limit	= rt2400pci_set_retry_limit,
	.erp_ie_changed		= rt2x00mac_erp_ie_changed,
	.conf_tx		= rt2400pci_conf_tx,
	.get_tx_stats		= rt2x00mac_get_tx_stats,
	.get_tsf		= rt2400pci_get_tsf,
	.reset_tsf		= rt2400pci_reset_tsf,
	.beacon_update		= rt2x00pci_beacon_update,
	.tx_last_beacon		= rt2400pci_tx_last_beacon,
};

static const struct rt2x00lib_ops rt2400pci_rt2x00_ops = {
	.irq_handler		= rt2400pci_interrupt,
	.probe_hw		= rt2400pci_probe_hw,
	.initialize		= rt2x00pci_initialize,
	.uninitialize		= rt2x00pci_uninitialize,
	.set_device_state	= rt2400pci_set_device_state,
	.rfkill_poll		= rt2400pci_rfkill_poll,
	.link_stats		= rt2400pci_link_stats,
	.reset_tuner		= rt2400pci_reset_tuner,
	.link_tuner		= rt2400pci_link_tuner,
	.write_tx_desc		= rt2400pci_write_tx_desc,
	.write_tx_data		= rt2x00pci_write_tx_data,
	.kick_tx_queue		= rt2400pci_kick_tx_queue,
	.fill_rxdone		= rt2400pci_fill_rxdone,
	.config_mac_addr	= rt2400pci_config_mac_addr,
	.config_bssid		= rt2400pci_config_bssid,
	.config_type		= rt2400pci_config_type,
	.config_preamble	= rt2400pci_config_preamble,
	.config			= rt2400pci_config,
};

static const struct rt2x00_ops rt2400pci_ops = {
	.name		= DRV_NAME,
	.rxd_size	= RXD_DESC_SIZE,
	.txd_size	= TXD_DESC_SIZE,
	.eeprom_size	= EEPROM_SIZE,
	.rf_size	= RF_SIZE,
	.lib		= &rt2400pci_rt2x00_ops,
	.hw		= &rt2400pci_mac80211_ops,
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	.debugfs	= &rt2400pci_rt2x00debug,
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */
};

/*
 * RT2400pci module information.
 */
static struct pci_device_id rt2400pci_device_table[] = {
	{ PCI_DEVICE(0x1814, 0x0101), PCI_DEVICE_DATA(&rt2400pci_ops) },
	{ 0, }
};

MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("Ralink RT2400 PCI & PCMCIA Wireless LAN driver.");
MODULE_SUPPORTED_DEVICE("Ralink RT2460 PCI & PCMCIA chipset based cards");
MODULE_DEVICE_TABLE(pci, rt2400pci_device_table);
MODULE_LICENSE("GPL");

static struct pci_driver rt2400pci_driver = {
	.name		= DRV_NAME,
	.id_table	= rt2400pci_device_table,
	.probe		= rt2x00pci_probe,
	.remove		= __devexit_p(rt2x00pci_remove),
	.suspend	= rt2x00pci_suspend,
	.resume		= rt2x00pci_resume,
};

static int __init rt2400pci_init(void)
{
	return pci_register_driver(&rt2400pci_driver);
}

static void __exit rt2400pci_exit(void)
{
	pci_unregister_driver(&rt2400pci_driver);
}

module_init(rt2400pci_init);
module_exit(rt2400pci_exit);

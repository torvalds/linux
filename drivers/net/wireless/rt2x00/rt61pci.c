/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
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
	Module: rt61pci
	Abstract: rt61pci device specific routines.
	Supported chipsets: RT2561, RT2561s, RT2661.
 */

#include <linux/crc-itu-t.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/eeprom_93cx6.h>

#include "rt2x00.h"
#include "rt2x00pci.h"
#include "rt61pci.h"

/*
 * Register access.
 * BBP and RF register require indirect register access,
 * and use the CSR registers PHY_CSR3 and PHY_CSR4 to achieve this.
 * These indirect registers work with busy bits,
 * and we will try maximal REGISTER_BUSY_COUNT times to access
 * the register while taking a REGISTER_BUSY_DELAY us delay
 * between each attampt. When the busy bit is still set at that time,
 * the access attempt is considered to have failed,
 * and we will print an error.
 */
static u32 rt61pci_bbp_check(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	unsigned int i;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2x00pci_register_read(rt2x00dev, PHY_CSR3, &reg);
		if (!rt2x00_get_field32(reg, PHY_CSR3_BUSY))
			break;
		udelay(REGISTER_BUSY_DELAY);
	}

	return reg;
}

static void rt61pci_bbp_write(struct rt2x00_dev *rt2x00dev,
			      const unsigned int word, const u8 value)
{
	u32 reg;

	/*
	 * Wait until the BBP becomes ready.
	 */
	reg = rt61pci_bbp_check(rt2x00dev);
	if (rt2x00_get_field32(reg, PHY_CSR3_BUSY)) {
		ERROR(rt2x00dev, "PHY_CSR3 register busy. Write failed.\n");
		return;
	}

	/*
	 * Write the data into the BBP.
	 */
	reg = 0;
	rt2x00_set_field32(&reg, PHY_CSR3_VALUE, value);
	rt2x00_set_field32(&reg, PHY_CSR3_REGNUM, word);
	rt2x00_set_field32(&reg, PHY_CSR3_BUSY, 1);
	rt2x00_set_field32(&reg, PHY_CSR3_READ_CONTROL, 0);

	rt2x00pci_register_write(rt2x00dev, PHY_CSR3, reg);
}

static void rt61pci_bbp_read(struct rt2x00_dev *rt2x00dev,
			     const unsigned int word, u8 *value)
{
	u32 reg;

	/*
	 * Wait until the BBP becomes ready.
	 */
	reg = rt61pci_bbp_check(rt2x00dev);
	if (rt2x00_get_field32(reg, PHY_CSR3_BUSY)) {
		ERROR(rt2x00dev, "PHY_CSR3 register busy. Read failed.\n");
		return;
	}

	/*
	 * Write the request into the BBP.
	 */
	reg = 0;
	rt2x00_set_field32(&reg, PHY_CSR3_REGNUM, word);
	rt2x00_set_field32(&reg, PHY_CSR3_BUSY, 1);
	rt2x00_set_field32(&reg, PHY_CSR3_READ_CONTROL, 1);

	rt2x00pci_register_write(rt2x00dev, PHY_CSR3, reg);

	/*
	 * Wait until the BBP becomes ready.
	 */
	reg = rt61pci_bbp_check(rt2x00dev);
	if (rt2x00_get_field32(reg, PHY_CSR3_BUSY)) {
		ERROR(rt2x00dev, "PHY_CSR3 register busy. Read failed.\n");
		*value = 0xff;
		return;
	}

	*value = rt2x00_get_field32(reg, PHY_CSR3_VALUE);
}

static void rt61pci_rf_write(struct rt2x00_dev *rt2x00dev,
			     const unsigned int word, const u32 value)
{
	u32 reg;
	unsigned int i;

	if (!word)
		return;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2x00pci_register_read(rt2x00dev, PHY_CSR4, &reg);
		if (!rt2x00_get_field32(reg, PHY_CSR4_BUSY))
			goto rf_write;
		udelay(REGISTER_BUSY_DELAY);
	}

	ERROR(rt2x00dev, "PHY_CSR4 register busy. Write failed.\n");
	return;

rf_write:
	reg = 0;
	rt2x00_set_field32(&reg, PHY_CSR4_VALUE, value);
	rt2x00_set_field32(&reg, PHY_CSR4_NUMBER_OF_BITS, 21);
	rt2x00_set_field32(&reg, PHY_CSR4_IF_SELECT, 0);
	rt2x00_set_field32(&reg, PHY_CSR4_BUSY, 1);

	rt2x00pci_register_write(rt2x00dev, PHY_CSR4, reg);
	rt2x00_rf_write(rt2x00dev, word, value);
}

#ifdef CONFIG_RT61PCI_LEDS
/*
 * This function is only called from rt61pci_led_brightness()
 * make gcc happy by placing this function inside the
 * same ifdef statement as the caller.
 */
static void rt61pci_mcu_request(struct rt2x00_dev *rt2x00dev,
				const u8 command, const u8 token,
				const u8 arg0, const u8 arg1)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, H2M_MAILBOX_CSR, &reg);

	if (rt2x00_get_field32(reg, H2M_MAILBOX_CSR_OWNER)) {
		ERROR(rt2x00dev, "mcu request error. "
		      "Request 0x%02x failed for token 0x%02x.\n",
		      command, token);
		return;
	}

	rt2x00_set_field32(&reg, H2M_MAILBOX_CSR_OWNER, 1);
	rt2x00_set_field32(&reg, H2M_MAILBOX_CSR_CMD_TOKEN, token);
	rt2x00_set_field32(&reg, H2M_MAILBOX_CSR_ARG0, arg0);
	rt2x00_set_field32(&reg, H2M_MAILBOX_CSR_ARG1, arg1);
	rt2x00pci_register_write(rt2x00dev, H2M_MAILBOX_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, HOST_CMD_CSR, &reg);
	rt2x00_set_field32(&reg, HOST_CMD_CSR_HOST_COMMAND, command);
	rt2x00_set_field32(&reg, HOST_CMD_CSR_INTERRUPT_MCU, 1);
	rt2x00pci_register_write(rt2x00dev, HOST_CMD_CSR, reg);
}
#endif /* CONFIG_RT61PCI_LEDS */

static void rt61pci_eepromregister_read(struct eeprom_93cx6 *eeprom)
{
	struct rt2x00_dev *rt2x00dev = eeprom->data;
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, E2PROM_CSR, &reg);

	eeprom->reg_data_in = !!rt2x00_get_field32(reg, E2PROM_CSR_DATA_IN);
	eeprom->reg_data_out = !!rt2x00_get_field32(reg, E2PROM_CSR_DATA_OUT);
	eeprom->reg_data_clock =
	    !!rt2x00_get_field32(reg, E2PROM_CSR_DATA_CLOCK);
	eeprom->reg_chip_select =
	    !!rt2x00_get_field32(reg, E2PROM_CSR_CHIP_SELECT);
}

static void rt61pci_eepromregister_write(struct eeprom_93cx6 *eeprom)
{
	struct rt2x00_dev *rt2x00dev = eeprom->data;
	u32 reg = 0;

	rt2x00_set_field32(&reg, E2PROM_CSR_DATA_IN, !!eeprom->reg_data_in);
	rt2x00_set_field32(&reg, E2PROM_CSR_DATA_OUT, !!eeprom->reg_data_out);
	rt2x00_set_field32(&reg, E2PROM_CSR_DATA_CLOCK,
			   !!eeprom->reg_data_clock);
	rt2x00_set_field32(&reg, E2PROM_CSR_CHIP_SELECT,
			   !!eeprom->reg_chip_select);

	rt2x00pci_register_write(rt2x00dev, E2PROM_CSR, reg);
}

#ifdef CONFIG_RT2X00_LIB_DEBUGFS
#define CSR_OFFSET(__word)	( CSR_REG_BASE + ((__word) * sizeof(u32)) )

static void rt61pci_read_csr(struct rt2x00_dev *rt2x00dev,
			     const unsigned int word, u32 *data)
{
	rt2x00pci_register_read(rt2x00dev, CSR_OFFSET(word), data);
}

static void rt61pci_write_csr(struct rt2x00_dev *rt2x00dev,
			      const unsigned int word, u32 data)
{
	rt2x00pci_register_write(rt2x00dev, CSR_OFFSET(word), data);
}

static const struct rt2x00debug rt61pci_rt2x00debug = {
	.owner	= THIS_MODULE,
	.csr	= {
		.read		= rt61pci_read_csr,
		.write		= rt61pci_write_csr,
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
		.read		= rt61pci_bbp_read,
		.write		= rt61pci_bbp_write,
		.word_size	= sizeof(u8),
		.word_count	= BBP_SIZE / sizeof(u8),
	},
	.rf	= {
		.read		= rt2x00_rf_read,
		.write		= rt61pci_rf_write,
		.word_size	= sizeof(u32),
		.word_count	= RF_SIZE / sizeof(u32),
	},
};
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */

#ifdef CONFIG_RT61PCI_RFKILL
static int rt61pci_rfkill_poll(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, MAC_CSR13, &reg);
	return rt2x00_get_field32(reg, MAC_CSR13_BIT5);
}
#else
#define rt61pci_rfkill_poll	NULL
#endif /* CONFIG_RT61PCI_RFKILL */

#ifdef CONFIG_RT61PCI_LEDS
static void rt61pci_brightness_set(struct led_classdev *led_cdev,
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

		rt61pci_mcu_request(led->rt2x00dev, MCU_LED, 0xff,
				    (led->rt2x00dev->led_mcu_reg & 0xff),
				    ((led->rt2x00dev->led_mcu_reg >> 8)));
	} else if (led->type == LED_TYPE_ASSOC) {
		rt2x00_set_field16(&led->rt2x00dev->led_mcu_reg,
				   MCU_LEDCS_LINK_BG_STATUS, bg_mode);
		rt2x00_set_field16(&led->rt2x00dev->led_mcu_reg,
				   MCU_LEDCS_LINK_A_STATUS, a_mode);

		rt61pci_mcu_request(led->rt2x00dev, MCU_LED, 0xff,
				    (led->rt2x00dev->led_mcu_reg & 0xff),
				    ((led->rt2x00dev->led_mcu_reg >> 8)));
	} else if (led->type == LED_TYPE_QUALITY) {
		/*
		 * The brightness is divided into 6 levels (0 - 5),
		 * this means we need to convert the brightness
		 * argument into the matching level within that range.
		 */
		rt61pci_mcu_request(led->rt2x00dev, MCU_LED_STRENGTH, 0xff,
				    brightness / (LED_FULL / 6), 0);
	}
}

static int rt61pci_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on,
			     unsigned long *delay_off)
{
	struct rt2x00_led *led =
	    container_of(led_cdev, struct rt2x00_led, led_dev);
	u32 reg;

	rt2x00pci_register_read(led->rt2x00dev, MAC_CSR14, &reg);
	rt2x00_set_field32(&reg, MAC_CSR14_ON_PERIOD, *delay_on);
	rt2x00_set_field32(&reg, MAC_CSR14_OFF_PERIOD, *delay_off);
	rt2x00pci_register_write(led->rt2x00dev, MAC_CSR14, reg);

	return 0;
}
#endif /* CONFIG_RT61PCI_LEDS */

/*
 * Configuration handlers.
 */
static void rt61pci_config_filter(struct rt2x00_dev *rt2x00dev,
				  const unsigned int filter_flags)
{
	u32 reg;

	/*
	 * Start configuration steps.
	 * Note that the version error will always be dropped
	 * and broadcast frames will always be accepted since
	 * there is no filter for it at this time.
	 */
	rt2x00pci_register_read(rt2x00dev, TXRX_CSR0, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_CRC,
			   !(filter_flags & FIF_FCSFAIL));
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_PHYSICAL,
			   !(filter_flags & FIF_PLCPFAIL));
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_CONTROL,
			   !(filter_flags & FIF_CONTROL));
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
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR0, reg);
}

static void rt61pci_config_intf(struct rt2x00_dev *rt2x00dev,
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
		rt2x00pci_register_write(rt2x00dev, beacon_base, 0);

		/*
		 * Enable synchronisation.
		 */
		rt2x00pci_register_read(rt2x00dev, TXRX_CSR9, &reg);
		rt2x00_set_field32(&reg, TXRX_CSR9_TSF_TICKING, 1);
		rt2x00_set_field32(&reg, TXRX_CSR9_TSF_SYNC, conf->sync);
		rt2x00_set_field32(&reg, TXRX_CSR9_TBTT_ENABLE, 1);
		rt2x00pci_register_write(rt2x00dev, TXRX_CSR9, reg);
	}

	if (flags & CONFIG_UPDATE_MAC) {
		reg = le32_to_cpu(conf->mac[1]);
		rt2x00_set_field32(&reg, MAC_CSR3_UNICAST_TO_ME_MASK, 0xff);
		conf->mac[1] = cpu_to_le32(reg);

		rt2x00pci_register_multiwrite(rt2x00dev, MAC_CSR2,
					      conf->mac, sizeof(conf->mac));
	}

	if (flags & CONFIG_UPDATE_BSSID) {
		reg = le32_to_cpu(conf->bssid[1]);
		rt2x00_set_field32(&reg, MAC_CSR5_BSS_ID_MASK, 3);
		conf->bssid[1] = cpu_to_le32(reg);

		rt2x00pci_register_multiwrite(rt2x00dev, MAC_CSR4,
					      conf->bssid, sizeof(conf->bssid));
	}
}

static void rt61pci_config_erp(struct rt2x00_dev *rt2x00dev,
			       struct rt2x00lib_erp *erp)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR0, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR0_RX_ACK_TIMEOUT, erp->ack_timeout);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR0, reg);

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR4, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR4_AUTORESPOND_PREAMBLE,
			   !!erp->short_preamble);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR4, reg);
}

static void rt61pci_config_phymode(struct rt2x00_dev *rt2x00dev,
				   const int basic_rate_mask)
{
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR5, basic_rate_mask);
}

static void rt61pci_config_channel(struct rt2x00_dev *rt2x00dev,
				   struct rf_channel *rf, const int txpower)
{
	u8 r3;
	u8 r94;
	u8 smart;

	rt2x00_set_field32(&rf->rf3, RF3_TXPOWER, TXPOWER_TO_DEV(txpower));
	rt2x00_set_field32(&rf->rf4, RF4_FREQ_OFFSET, rt2x00dev->freq_offset);

	smart = !(rt2x00_rf(&rt2x00dev->chip, RF5225) ||
		  rt2x00_rf(&rt2x00dev->chip, RF2527));

	rt61pci_bbp_read(rt2x00dev, 3, &r3);
	rt2x00_set_field8(&r3, BBP_R3_SMART_MODE, smart);
	rt61pci_bbp_write(rt2x00dev, 3, r3);

	r94 = 6;
	if (txpower > MAX_TXPOWER && txpower <= (MAX_TXPOWER + r94))
		r94 += txpower - MAX_TXPOWER;
	else if (txpower < MIN_TXPOWER && txpower >= (MIN_TXPOWER - r94))
		r94 += txpower;
	rt61pci_bbp_write(rt2x00dev, 94, r94);

	rt61pci_rf_write(rt2x00dev, 1, rf->rf1);
	rt61pci_rf_write(rt2x00dev, 2, rf->rf2);
	rt61pci_rf_write(rt2x00dev, 3, rf->rf3 & ~0x00000004);
	rt61pci_rf_write(rt2x00dev, 4, rf->rf4);

	udelay(200);

	rt61pci_rf_write(rt2x00dev, 1, rf->rf1);
	rt61pci_rf_write(rt2x00dev, 2, rf->rf2);
	rt61pci_rf_write(rt2x00dev, 3, rf->rf3 | 0x00000004);
	rt61pci_rf_write(rt2x00dev, 4, rf->rf4);

	udelay(200);

	rt61pci_rf_write(rt2x00dev, 1, rf->rf1);
	rt61pci_rf_write(rt2x00dev, 2, rf->rf2);
	rt61pci_rf_write(rt2x00dev, 3, rf->rf3 & ~0x00000004);
	rt61pci_rf_write(rt2x00dev, 4, rf->rf4);

	msleep(1);
}

static void rt61pci_config_txpower(struct rt2x00_dev *rt2x00dev,
				   const int txpower)
{
	struct rf_channel rf;

	rt2x00_rf_read(rt2x00dev, 1, &rf.rf1);
	rt2x00_rf_read(rt2x00dev, 2, &rf.rf2);
	rt2x00_rf_read(rt2x00dev, 3, &rf.rf3);
	rt2x00_rf_read(rt2x00dev, 4, &rf.rf4);

	rt61pci_config_channel(rt2x00dev, &rf, txpower);
}

static void rt61pci_config_antenna_5x(struct rt2x00_dev *rt2x00dev,
				      struct antenna_setup *ant)
{
	u8 r3;
	u8 r4;
	u8 r77;

	rt61pci_bbp_read(rt2x00dev, 3, &r3);
	rt61pci_bbp_read(rt2x00dev, 4, &r4);
	rt61pci_bbp_read(rt2x00dev, 77, &r77);

	rt2x00_set_field8(&r3, BBP_R3_SMART_MODE,
			  rt2x00_rf(&rt2x00dev->chip, RF5325));

	/*
	 * Configure the RX antenna.
	 */
	switch (ant->rx) {
	case ANTENNA_HW_DIVERSITY:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA_CONTROL, 2);
		rt2x00_set_field8(&r4, BBP_R4_RX_FRAME_END,
				  (rt2x00dev->curr_band != IEEE80211_BAND_5GHZ));
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

	rt61pci_bbp_write(rt2x00dev, 77, r77);
	rt61pci_bbp_write(rt2x00dev, 3, r3);
	rt61pci_bbp_write(rt2x00dev, 4, r4);
}

static void rt61pci_config_antenna_2x(struct rt2x00_dev *rt2x00dev,
				      struct antenna_setup *ant)
{
	u8 r3;
	u8 r4;
	u8 r77;

	rt61pci_bbp_read(rt2x00dev, 3, &r3);
	rt61pci_bbp_read(rt2x00dev, 4, &r4);
	rt61pci_bbp_read(rt2x00dev, 77, &r77);

	rt2x00_set_field8(&r3, BBP_R3_SMART_MODE,
			  rt2x00_rf(&rt2x00dev->chip, RF2529));
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
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA_CONTROL, 1);
		rt2x00_set_field8(&r77, BBP_R77_RX_ANTENNA, 3);
		break;
	case ANTENNA_B:
	default:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA_CONTROL, 1);
		rt2x00_set_field8(&r77, BBP_R77_RX_ANTENNA, 0);
		break;
	}

	rt61pci_bbp_write(rt2x00dev, 77, r77);
	rt61pci_bbp_write(rt2x00dev, 3, r3);
	rt61pci_bbp_write(rt2x00dev, 4, r4);
}

static void rt61pci_config_antenna_2529_rx(struct rt2x00_dev *rt2x00dev,
					   const int p1, const int p2)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, MAC_CSR13, &reg);

	rt2x00_set_field32(&reg, MAC_CSR13_BIT4, p1);
	rt2x00_set_field32(&reg, MAC_CSR13_BIT12, 0);

	rt2x00_set_field32(&reg, MAC_CSR13_BIT3, !p2);
	rt2x00_set_field32(&reg, MAC_CSR13_BIT11, 0);

	rt2x00pci_register_write(rt2x00dev, MAC_CSR13, reg);
}

static void rt61pci_config_antenna_2529(struct rt2x00_dev *rt2x00dev,
					struct antenna_setup *ant)
{
	u8 r3;
	u8 r4;
	u8 r77;

	rt61pci_bbp_read(rt2x00dev, 3, &r3);
	rt61pci_bbp_read(rt2x00dev, 4, &r4);
	rt61pci_bbp_read(rt2x00dev, 77, &r77);

	/*
	 * Configure the RX antenna.
	 */
	switch (ant->rx) {
	case ANTENNA_A:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA_CONTROL, 1);
		rt2x00_set_field8(&r77, BBP_R77_RX_ANTENNA, 0);
		rt61pci_config_antenna_2529_rx(rt2x00dev, 0, 0);
		break;
	case ANTENNA_HW_DIVERSITY:
		/*
		 * FIXME: Antenna selection for the rf 2529 is very confusing
		 * in the legacy driver. Just default to antenna B until the
		 * legacy code can be properly translated into rt2x00 code.
		 */
	case ANTENNA_B:
	default:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA_CONTROL, 1);
		rt2x00_set_field8(&r77, BBP_R77_RX_ANTENNA, 3);
		rt61pci_config_antenna_2529_rx(rt2x00dev, 1, 1);
		break;
	}

	rt61pci_bbp_write(rt2x00dev, 77, r77);
	rt61pci_bbp_write(rt2x00dev, 3, r3);
	rt61pci_bbp_write(rt2x00dev, 4, r4);
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

static void rt61pci_config_antenna(struct rt2x00_dev *rt2x00dev,
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
		rt61pci_bbp_write(rt2x00dev, sel[i].word, sel[i].value[lna]);

	rt2x00pci_register_read(rt2x00dev, PHY_CSR0, &reg);

	rt2x00_set_field32(&reg, PHY_CSR0_PA_PE_BG,
			   rt2x00dev->curr_band == IEEE80211_BAND_2GHZ);
	rt2x00_set_field32(&reg, PHY_CSR0_PA_PE_A,
			   rt2x00dev->curr_band == IEEE80211_BAND_5GHZ);

	rt2x00pci_register_write(rt2x00dev, PHY_CSR0, reg);

	if (rt2x00_rf(&rt2x00dev->chip, RF5225) ||
	    rt2x00_rf(&rt2x00dev->chip, RF5325))
		rt61pci_config_antenna_5x(rt2x00dev, ant);
	else if (rt2x00_rf(&rt2x00dev->chip, RF2527))
		rt61pci_config_antenna_2x(rt2x00dev, ant);
	else if (rt2x00_rf(&rt2x00dev->chip, RF2529)) {
		if (test_bit(CONFIG_DOUBLE_ANTENNA, &rt2x00dev->flags))
			rt61pci_config_antenna_2x(rt2x00dev, ant);
		else
			rt61pci_config_antenna_2529(rt2x00dev, ant);
	}
}

static void rt61pci_config_duration(struct rt2x00_dev *rt2x00dev,
				    struct rt2x00lib_conf *libconf)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, MAC_CSR9, &reg);
	rt2x00_set_field32(&reg, MAC_CSR9_SLOT_TIME, libconf->slot_time);
	rt2x00pci_register_write(rt2x00dev, MAC_CSR9, reg);

	rt2x00pci_register_read(rt2x00dev, MAC_CSR8, &reg);
	rt2x00_set_field32(&reg, MAC_CSR8_SIFS, libconf->sifs);
	rt2x00_set_field32(&reg, MAC_CSR8_SIFS_AFTER_RX_OFDM, 3);
	rt2x00_set_field32(&reg, MAC_CSR8_EIFS, libconf->eifs);
	rt2x00pci_register_write(rt2x00dev, MAC_CSR8, reg);

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR0, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR0_TSF_OFFSET, IEEE80211_HEADER);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR0, reg);

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR4, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR4_AUTORESPOND_ENABLE, 1);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR4, reg);

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR9, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR9_BEACON_INTERVAL,
			   libconf->conf->beacon_int * 16);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR9, reg);
}

static void rt61pci_config(struct rt2x00_dev *rt2x00dev,
			   struct rt2x00lib_conf *libconf,
			   const unsigned int flags)
{
	if (flags & CONFIG_UPDATE_PHYMODE)
		rt61pci_config_phymode(rt2x00dev, libconf->basic_rates);
	if (flags & CONFIG_UPDATE_CHANNEL)
		rt61pci_config_channel(rt2x00dev, &libconf->rf,
				       libconf->conf->power_level);
	if ((flags & CONFIG_UPDATE_TXPOWER) && !(flags & CONFIG_UPDATE_CHANNEL))
		rt61pci_config_txpower(rt2x00dev, libconf->conf->power_level);
	if (flags & CONFIG_UPDATE_ANTENNA)
		rt61pci_config_antenna(rt2x00dev, &libconf->ant);
	if (flags & (CONFIG_UPDATE_SLOT_TIME | CONFIG_UPDATE_BEACON_INT))
		rt61pci_config_duration(rt2x00dev, libconf);
}

/*
 * Link tuning
 */
static void rt61pci_link_stats(struct rt2x00_dev *rt2x00dev,
			       struct link_qual *qual)
{
	u32 reg;

	/*
	 * Update FCS error count from register.
	 */
	rt2x00pci_register_read(rt2x00dev, STA_CSR0, &reg);
	qual->rx_failed = rt2x00_get_field32(reg, STA_CSR0_FCS_ERROR);

	/*
	 * Update False CCA count from register.
	 */
	rt2x00pci_register_read(rt2x00dev, STA_CSR1, &reg);
	qual->false_cca = rt2x00_get_field32(reg, STA_CSR1_FALSE_CCA_ERROR);
}

static void rt61pci_reset_tuner(struct rt2x00_dev *rt2x00dev)
{
	rt61pci_bbp_write(rt2x00dev, 17, 0x20);
	rt2x00dev->link.vgc_level = 0x20;
}

static void rt61pci_link_tuner(struct rt2x00_dev *rt2x00dev)
{
	int rssi = rt2x00_get_link_rssi(&rt2x00dev->link);
	u8 r17;
	u8 up_bound;
	u8 low_bound;

	rt61pci_bbp_read(rt2x00dev, 17, &r17);

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
		low_bound = 0x20;
		up_bound = 0x40;
		if (test_bit(CONFIG_EXTERNAL_LNA_BG, &rt2x00dev->flags)) {
			low_bound += 0x10;
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
	if (rssi >= -35) {
		if (r17 != 0x60)
			rt61pci_bbp_write(rt2x00dev, 17, 0x60);
		return;
	}

	/*
	 * Special big-R17 for short distance
	 */
	if (rssi >= -58) {
		if (r17 != up_bound)
			rt61pci_bbp_write(rt2x00dev, 17, up_bound);
		return;
	}

	/*
	 * Special big-R17 for middle-short distance
	 */
	if (rssi >= -66) {
		low_bound += 0x10;
		if (r17 != low_bound)
			rt61pci_bbp_write(rt2x00dev, 17, low_bound);
		return;
	}

	/*
	 * Special mid-R17 for middle distance
	 */
	if (rssi >= -74) {
		low_bound += 0x08;
		if (r17 != low_bound)
			rt61pci_bbp_write(rt2x00dev, 17, low_bound);
		return;
	}

	/*
	 * Special case: Change up_bound based on the rssi.
	 * Lower up_bound when rssi is weaker then -74 dBm.
	 */
	up_bound -= 2 * (-74 - rssi);
	if (low_bound > up_bound)
		up_bound = low_bound;

	if (r17 > up_bound) {
		rt61pci_bbp_write(rt2x00dev, 17, up_bound);
		return;
	}

dynamic_cca_tune:

	/*
	 * r17 does not yet exceed upper limit, continue and base
	 * the r17 tuning on the false CCA count.
	 */
	if (rt2x00dev->link.qual.false_cca > 512 && r17 < up_bound) {
		if (++r17 > up_bound)
			r17 = up_bound;
		rt61pci_bbp_write(rt2x00dev, 17, r17);
	} else if (rt2x00dev->link.qual.false_cca < 100 && r17 > low_bound) {
		if (--r17 < low_bound)
			r17 = low_bound;
		rt61pci_bbp_write(rt2x00dev, 17, r17);
	}
}

/*
 * Firmware functions
 */
static char *rt61pci_get_firmware_name(struct rt2x00_dev *rt2x00dev)
{
	char *fw_name;

	switch (rt2x00dev->chip.rt) {
	case RT2561:
		fw_name = FIRMWARE_RT2561;
		break;
	case RT2561s:
		fw_name = FIRMWARE_RT2561s;
		break;
	case RT2661:
		fw_name = FIRMWARE_RT2661;
		break;
	default:
		fw_name = NULL;
		break;
	}

	return fw_name;
}

static u16 rt61pci_get_firmware_crc(void *data, const size_t len)
{
	u16 crc;

	/*
	 * Use the crc itu-t algorithm.
	 * The last 2 bytes in the firmware array are the crc checksum itself,
	 * this means that we should never pass those 2 bytes to the crc
	 * algorithm.
	 */
	crc = crc_itu_t(0, data, len - 2);
	crc = crc_itu_t_byte(crc, 0);
	crc = crc_itu_t_byte(crc, 0);

	return crc;
}

static int rt61pci_load_firmware(struct rt2x00_dev *rt2x00dev, void *data,
				 const size_t len)
{
	int i;
	u32 reg;

	/*
	 * Wait for stable hardware.
	 */
	for (i = 0; i < 100; i++) {
		rt2x00pci_register_read(rt2x00dev, MAC_CSR0, &reg);
		if (reg)
			break;
		msleep(1);
	}

	if (!reg) {
		ERROR(rt2x00dev, "Unstable hardware.\n");
		return -EBUSY;
	}

	/*
	 * Prepare MCU and mailbox for firmware loading.
	 */
	reg = 0;
	rt2x00_set_field32(&reg, MCU_CNTL_CSR_RESET, 1);
	rt2x00pci_register_write(rt2x00dev, MCU_CNTL_CSR, reg);
	rt2x00pci_register_write(rt2x00dev, M2H_CMD_DONE_CSR, 0xffffffff);
	rt2x00pci_register_write(rt2x00dev, H2M_MAILBOX_CSR, 0);
	rt2x00pci_register_write(rt2x00dev, HOST_CMD_CSR, 0);

	/*
	 * Write firmware to device.
	 */
	reg = 0;
	rt2x00_set_field32(&reg, MCU_CNTL_CSR_RESET, 1);
	rt2x00_set_field32(&reg, MCU_CNTL_CSR_SELECT_BANK, 1);
	rt2x00pci_register_write(rt2x00dev, MCU_CNTL_CSR, reg);

	rt2x00pci_register_multiwrite(rt2x00dev, FIRMWARE_IMAGE_BASE,
				      data, len);

	rt2x00_set_field32(&reg, MCU_CNTL_CSR_SELECT_BANK, 0);
	rt2x00pci_register_write(rt2x00dev, MCU_CNTL_CSR, reg);

	rt2x00_set_field32(&reg, MCU_CNTL_CSR_RESET, 0);
	rt2x00pci_register_write(rt2x00dev, MCU_CNTL_CSR, reg);

	for (i = 0; i < 100; i++) {
		rt2x00pci_register_read(rt2x00dev, MCU_CNTL_CSR, &reg);
		if (rt2x00_get_field32(reg, MCU_CNTL_CSR_READY))
			break;
		msleep(1);
	}

	if (i == 100) {
		ERROR(rt2x00dev, "MCU Control register not ready.\n");
		return -EBUSY;
	}

	/*
	 * Reset MAC and BBP registers.
	 */
	reg = 0;
	rt2x00_set_field32(&reg, MAC_CSR1_SOFT_RESET, 1);
	rt2x00_set_field32(&reg, MAC_CSR1_BBP_RESET, 1);
	rt2x00pci_register_write(rt2x00dev, MAC_CSR1, reg);

	rt2x00pci_register_read(rt2x00dev, MAC_CSR1, &reg);
	rt2x00_set_field32(&reg, MAC_CSR1_SOFT_RESET, 0);
	rt2x00_set_field32(&reg, MAC_CSR1_BBP_RESET, 0);
	rt2x00pci_register_write(rt2x00dev, MAC_CSR1, reg);

	rt2x00pci_register_read(rt2x00dev, MAC_CSR1, &reg);
	rt2x00_set_field32(&reg, MAC_CSR1_HOST_READY, 1);
	rt2x00pci_register_write(rt2x00dev, MAC_CSR1, reg);

	return 0;
}

/*
 * Initialization functions.
 */
static void rt61pci_init_rxentry(struct rt2x00_dev *rt2x00dev,
				 struct queue_entry *entry)
{
	struct queue_entry_priv_pci_rx *priv_rx = entry->priv_data;
	u32 word;

	rt2x00_desc_read(priv_rx->desc, 5, &word);
	rt2x00_set_field32(&word, RXD_W5_BUFFER_PHYSICAL_ADDRESS,
			   priv_rx->data_dma);
	rt2x00_desc_write(priv_rx->desc, 5, word);

	rt2x00_desc_read(priv_rx->desc, 0, &word);
	rt2x00_set_field32(&word, RXD_W0_OWNER_NIC, 1);
	rt2x00_desc_write(priv_rx->desc, 0, word);
}

static void rt61pci_init_txentry(struct rt2x00_dev *rt2x00dev,
				 struct queue_entry *entry)
{
	struct queue_entry_priv_pci_tx *priv_tx = entry->priv_data;
	u32 word;

	rt2x00_desc_read(priv_tx->desc, 1, &word);
	rt2x00_set_field32(&word, TXD_W1_BUFFER_COUNT, 1);
	rt2x00_desc_write(priv_tx->desc, 1, word);

	rt2x00_desc_read(priv_tx->desc, 5, &word);
	rt2x00_set_field32(&word, TXD_W5_PID_TYPE, entry->queue->qid);
	rt2x00_set_field32(&word, TXD_W5_PID_SUBTYPE, entry->entry_idx);
	rt2x00_desc_write(priv_tx->desc, 5, word);

	rt2x00_desc_read(priv_tx->desc, 6, &word);
	rt2x00_set_field32(&word, TXD_W6_BUFFER_PHYSICAL_ADDRESS,
			   priv_tx->data_dma);
	rt2x00_desc_write(priv_tx->desc, 6, word);

	rt2x00_desc_read(priv_tx->desc, 0, &word);
	rt2x00_set_field32(&word, TXD_W0_VALID, 0);
	rt2x00_set_field32(&word, TXD_W0_OWNER_NIC, 0);
	rt2x00_desc_write(priv_tx->desc, 0, word);
}

static int rt61pci_init_queues(struct rt2x00_dev *rt2x00dev)
{
	struct queue_entry_priv_pci_rx *priv_rx;
	struct queue_entry_priv_pci_tx *priv_tx;
	u32 reg;

	/*
	 * Initialize registers.
	 */
	rt2x00pci_register_read(rt2x00dev, TX_RING_CSR0, &reg);
	rt2x00_set_field32(&reg, TX_RING_CSR0_AC0_RING_SIZE,
			   rt2x00dev->tx[0].limit);
	rt2x00_set_field32(&reg, TX_RING_CSR0_AC1_RING_SIZE,
			   rt2x00dev->tx[1].limit);
	rt2x00_set_field32(&reg, TX_RING_CSR0_AC2_RING_SIZE,
			   rt2x00dev->tx[2].limit);
	rt2x00_set_field32(&reg, TX_RING_CSR0_AC3_RING_SIZE,
			   rt2x00dev->tx[3].limit);
	rt2x00pci_register_write(rt2x00dev, TX_RING_CSR0, reg);

	rt2x00pci_register_read(rt2x00dev, TX_RING_CSR1, &reg);
	rt2x00_set_field32(&reg, TX_RING_CSR1_TXD_SIZE,
			   rt2x00dev->tx[0].desc_size / 4);
	rt2x00pci_register_write(rt2x00dev, TX_RING_CSR1, reg);

	priv_tx = rt2x00dev->tx[0].entries[0].priv_data;
	rt2x00pci_register_read(rt2x00dev, AC0_BASE_CSR, &reg);
	rt2x00_set_field32(&reg, AC0_BASE_CSR_RING_REGISTER,
			   priv_tx->desc_dma);
	rt2x00pci_register_write(rt2x00dev, AC0_BASE_CSR, reg);

	priv_tx = rt2x00dev->tx[1].entries[0].priv_data;
	rt2x00pci_register_read(rt2x00dev, AC1_BASE_CSR, &reg);
	rt2x00_set_field32(&reg, AC1_BASE_CSR_RING_REGISTER,
			   priv_tx->desc_dma);
	rt2x00pci_register_write(rt2x00dev, AC1_BASE_CSR, reg);

	priv_tx = rt2x00dev->tx[2].entries[0].priv_data;
	rt2x00pci_register_read(rt2x00dev, AC2_BASE_CSR, &reg);
	rt2x00_set_field32(&reg, AC2_BASE_CSR_RING_REGISTER,
			   priv_tx->desc_dma);
	rt2x00pci_register_write(rt2x00dev, AC2_BASE_CSR, reg);

	priv_tx = rt2x00dev->tx[3].entries[0].priv_data;
	rt2x00pci_register_read(rt2x00dev, AC3_BASE_CSR, &reg);
	rt2x00_set_field32(&reg, AC3_BASE_CSR_RING_REGISTER,
			   priv_tx->desc_dma);
	rt2x00pci_register_write(rt2x00dev, AC3_BASE_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, RX_RING_CSR, &reg);
	rt2x00_set_field32(&reg, RX_RING_CSR_RING_SIZE, rt2x00dev->rx->limit);
	rt2x00_set_field32(&reg, RX_RING_CSR_RXD_SIZE,
			   rt2x00dev->rx->desc_size / 4);
	rt2x00_set_field32(&reg, RX_RING_CSR_RXD_WRITEBACK_SIZE, 4);
	rt2x00pci_register_write(rt2x00dev, RX_RING_CSR, reg);

	priv_rx = rt2x00dev->rx->entries[0].priv_data;
	rt2x00pci_register_read(rt2x00dev, RX_BASE_CSR, &reg);
	rt2x00_set_field32(&reg, RX_BASE_CSR_RING_REGISTER,
			   priv_rx->desc_dma);
	rt2x00pci_register_write(rt2x00dev, RX_BASE_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, TX_DMA_DST_CSR, &reg);
	rt2x00_set_field32(&reg, TX_DMA_DST_CSR_DEST_AC0, 2);
	rt2x00_set_field32(&reg, TX_DMA_DST_CSR_DEST_AC1, 2);
	rt2x00_set_field32(&reg, TX_DMA_DST_CSR_DEST_AC2, 2);
	rt2x00_set_field32(&reg, TX_DMA_DST_CSR_DEST_AC3, 2);
	rt2x00pci_register_write(rt2x00dev, TX_DMA_DST_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, LOAD_TX_RING_CSR, &reg);
	rt2x00_set_field32(&reg, LOAD_TX_RING_CSR_LOAD_TXD_AC0, 1);
	rt2x00_set_field32(&reg, LOAD_TX_RING_CSR_LOAD_TXD_AC1, 1);
	rt2x00_set_field32(&reg, LOAD_TX_RING_CSR_LOAD_TXD_AC2, 1);
	rt2x00_set_field32(&reg, LOAD_TX_RING_CSR_LOAD_TXD_AC3, 1);
	rt2x00pci_register_write(rt2x00dev, LOAD_TX_RING_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, RX_CNTL_CSR, &reg);
	rt2x00_set_field32(&reg, RX_CNTL_CSR_LOAD_RXD, 1);
	rt2x00pci_register_write(rt2x00dev, RX_CNTL_CSR, reg);

	return 0;
}

static int rt61pci_init_registers(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR0, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR0_AUTO_TX_SEQ, 1);
	rt2x00_set_field32(&reg, TXRX_CSR0_DISABLE_RX, 0);
	rt2x00_set_field32(&reg, TXRX_CSR0_TX_WITHOUT_WAITING, 0);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR0, reg);

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR1, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID0, 47); /* CCK Signal */
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID0_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID1, 30); /* Rssi */
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID1_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID2, 42); /* OFDM Rate */
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID2_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID3, 30); /* Rssi */
	rt2x00_set_field32(&reg, TXRX_CSR1_BBP_ID3_VALID, 1);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR1, reg);

	/*
	 * CCK TXD BBP registers
	 */
	rt2x00pci_register_read(rt2x00dev, TXRX_CSR2, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID0, 13);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID0_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID1, 12);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID1_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID2, 11);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID2_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID3, 10);
	rt2x00_set_field32(&reg, TXRX_CSR2_BBP_ID3_VALID, 1);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR2, reg);

	/*
	 * OFDM TXD BBP registers
	 */
	rt2x00pci_register_read(rt2x00dev, TXRX_CSR3, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR3_BBP_ID0, 7);
	rt2x00_set_field32(&reg, TXRX_CSR3_BBP_ID0_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR3_BBP_ID1, 6);
	rt2x00_set_field32(&reg, TXRX_CSR3_BBP_ID1_VALID, 1);
	rt2x00_set_field32(&reg, TXRX_CSR3_BBP_ID2, 5);
	rt2x00_set_field32(&reg, TXRX_CSR3_BBP_ID2_VALID, 1);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR3, reg);

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR7, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR7_ACK_CTS_6MBS, 59);
	rt2x00_set_field32(&reg, TXRX_CSR7_ACK_CTS_9MBS, 53);
	rt2x00_set_field32(&reg, TXRX_CSR7_ACK_CTS_12MBS, 49);
	rt2x00_set_field32(&reg, TXRX_CSR7_ACK_CTS_18MBS, 46);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR7, reg);

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR8, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR8_ACK_CTS_24MBS, 44);
	rt2x00_set_field32(&reg, TXRX_CSR8_ACK_CTS_36MBS, 42);
	rt2x00_set_field32(&reg, TXRX_CSR8_ACK_CTS_48MBS, 42);
	rt2x00_set_field32(&reg, TXRX_CSR8_ACK_CTS_54MBS, 42);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR8, reg);

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR9, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR9_BEACON_INTERVAL, 0);
	rt2x00_set_field32(&reg, TXRX_CSR9_TSF_TICKING, 0);
	rt2x00_set_field32(&reg, TXRX_CSR9_TSF_SYNC, 0);
	rt2x00_set_field32(&reg, TXRX_CSR9_TBTT_ENABLE, 0);
	rt2x00_set_field32(&reg, TXRX_CSR9_BEACON_GEN, 0);
	rt2x00_set_field32(&reg, TXRX_CSR9_TIMESTAMP_COMPENSATE, 0);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR9, reg);

	rt2x00pci_register_write(rt2x00dev, TXRX_CSR15, 0x0000000f);

	rt2x00pci_register_write(rt2x00dev, MAC_CSR6, 0x00000fff);

	rt2x00pci_register_read(rt2x00dev, MAC_CSR9, &reg);
	rt2x00_set_field32(&reg, MAC_CSR9_CW_SELECT, 0);
	rt2x00pci_register_write(rt2x00dev, MAC_CSR9, reg);

	rt2x00pci_register_write(rt2x00dev, MAC_CSR10, 0x0000071c);

	if (rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_AWAKE))
		return -EBUSY;

	rt2x00pci_register_write(rt2x00dev, MAC_CSR13, 0x0000e000);

	/*
	 * Invalidate all Shared Keys (SEC_CSR0),
	 * and clear the Shared key Cipher algorithms (SEC_CSR1 & SEC_CSR5)
	 */
	rt2x00pci_register_write(rt2x00dev, SEC_CSR0, 0x00000000);
	rt2x00pci_register_write(rt2x00dev, SEC_CSR1, 0x00000000);
	rt2x00pci_register_write(rt2x00dev, SEC_CSR5, 0x00000000);

	rt2x00pci_register_write(rt2x00dev, PHY_CSR1, 0x000023b0);
	rt2x00pci_register_write(rt2x00dev, PHY_CSR5, 0x060a100c);
	rt2x00pci_register_write(rt2x00dev, PHY_CSR6, 0x00080606);
	rt2x00pci_register_write(rt2x00dev, PHY_CSR7, 0x00000a08);

	rt2x00pci_register_write(rt2x00dev, PCI_CFG_CSR, 0x28ca4404);

	rt2x00pci_register_write(rt2x00dev, TEST_MODE_CSR, 0x00000200);

	rt2x00pci_register_write(rt2x00dev, M2H_CMD_DONE_CSR, 0xffffffff);

	rt2x00pci_register_read(rt2x00dev, AC_TXOP_CSR0, &reg);
	rt2x00_set_field32(&reg, AC_TXOP_CSR0_AC0_TX_OP, 0);
	rt2x00_set_field32(&reg, AC_TXOP_CSR0_AC1_TX_OP, 0);
	rt2x00pci_register_write(rt2x00dev, AC_TXOP_CSR0, reg);

	rt2x00pci_register_read(rt2x00dev, AC_TXOP_CSR1, &reg);
	rt2x00_set_field32(&reg, AC_TXOP_CSR1_AC2_TX_OP, 192);
	rt2x00_set_field32(&reg, AC_TXOP_CSR1_AC3_TX_OP, 48);
	rt2x00pci_register_write(rt2x00dev, AC_TXOP_CSR1, reg);

	/*
	 * Clear all beacons
	 * For the Beacon base registers we only need to clear
	 * the first byte since that byte contains the VALID and OWNER
	 * bits which (when set to 0) will invalidate the entire beacon.
	 */
	rt2x00pci_register_write(rt2x00dev, HW_BEACON_BASE0, 0);
	rt2x00pci_register_write(rt2x00dev, HW_BEACON_BASE1, 0);
	rt2x00pci_register_write(rt2x00dev, HW_BEACON_BASE2, 0);
	rt2x00pci_register_write(rt2x00dev, HW_BEACON_BASE3, 0);

	/*
	 * We must clear the error counters.
	 * These registers are cleared on read,
	 * so we may pass a useless variable to store the value.
	 */
	rt2x00pci_register_read(rt2x00dev, STA_CSR0, &reg);
	rt2x00pci_register_read(rt2x00dev, STA_CSR1, &reg);
	rt2x00pci_register_read(rt2x00dev, STA_CSR2, &reg);

	/*
	 * Reset MAC and BBP registers.
	 */
	rt2x00pci_register_read(rt2x00dev, MAC_CSR1, &reg);
	rt2x00_set_field32(&reg, MAC_CSR1_SOFT_RESET, 1);
	rt2x00_set_field32(&reg, MAC_CSR1_BBP_RESET, 1);
	rt2x00pci_register_write(rt2x00dev, MAC_CSR1, reg);

	rt2x00pci_register_read(rt2x00dev, MAC_CSR1, &reg);
	rt2x00_set_field32(&reg, MAC_CSR1_SOFT_RESET, 0);
	rt2x00_set_field32(&reg, MAC_CSR1_BBP_RESET, 0);
	rt2x00pci_register_write(rt2x00dev, MAC_CSR1, reg);

	rt2x00pci_register_read(rt2x00dev, MAC_CSR1, &reg);
	rt2x00_set_field32(&reg, MAC_CSR1_HOST_READY, 1);
	rt2x00pci_register_write(rt2x00dev, MAC_CSR1, reg);

	return 0;
}

static int rt61pci_init_bbp(struct rt2x00_dev *rt2x00dev)
{
	unsigned int i;
	u16 eeprom;
	u8 reg_id;
	u8 value;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt61pci_bbp_read(rt2x00dev, 0, &value);
		if ((value != 0xff) && (value != 0x00))
			goto continue_csr_init;
		NOTICE(rt2x00dev, "Waiting for BBP register.\n");
		udelay(REGISTER_BUSY_DELAY);
	}

	ERROR(rt2x00dev, "BBP register access failed, aborting.\n");
	return -EACCES;

continue_csr_init:
	rt61pci_bbp_write(rt2x00dev, 3, 0x00);
	rt61pci_bbp_write(rt2x00dev, 15, 0x30);
	rt61pci_bbp_write(rt2x00dev, 21, 0xc8);
	rt61pci_bbp_write(rt2x00dev, 22, 0x38);
	rt61pci_bbp_write(rt2x00dev, 23, 0x06);
	rt61pci_bbp_write(rt2x00dev, 24, 0xfe);
	rt61pci_bbp_write(rt2x00dev, 25, 0x0a);
	rt61pci_bbp_write(rt2x00dev, 26, 0x0d);
	rt61pci_bbp_write(rt2x00dev, 34, 0x12);
	rt61pci_bbp_write(rt2x00dev, 37, 0x07);
	rt61pci_bbp_write(rt2x00dev, 39, 0xf8);
	rt61pci_bbp_write(rt2x00dev, 41, 0x60);
	rt61pci_bbp_write(rt2x00dev, 53, 0x10);
	rt61pci_bbp_write(rt2x00dev, 54, 0x18);
	rt61pci_bbp_write(rt2x00dev, 60, 0x10);
	rt61pci_bbp_write(rt2x00dev, 61, 0x04);
	rt61pci_bbp_write(rt2x00dev, 62, 0x04);
	rt61pci_bbp_write(rt2x00dev, 75, 0xfe);
	rt61pci_bbp_write(rt2x00dev, 86, 0xfe);
	rt61pci_bbp_write(rt2x00dev, 88, 0xfe);
	rt61pci_bbp_write(rt2x00dev, 90, 0x0f);
	rt61pci_bbp_write(rt2x00dev, 99, 0x00);
	rt61pci_bbp_write(rt2x00dev, 102, 0x16);
	rt61pci_bbp_write(rt2x00dev, 107, 0x04);

	for (i = 0; i < EEPROM_BBP_SIZE; i++) {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_BBP_START + i, &eeprom);

		if (eeprom != 0xffff && eeprom != 0x0000) {
			reg_id = rt2x00_get_field16(eeprom, EEPROM_BBP_REG_ID);
			value = rt2x00_get_field16(eeprom, EEPROM_BBP_VALUE);
			rt61pci_bbp_write(rt2x00dev, reg_id, value);
		}
	}

	return 0;
}

/*
 * Device state switch handlers.
 */
static void rt61pci_toggle_rx(struct rt2x00_dev *rt2x00dev,
			      enum dev_state state)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR0, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR0_DISABLE_RX,
			   state == STATE_RADIO_RX_OFF);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR0, reg);
}

static void rt61pci_toggle_irq(struct rt2x00_dev *rt2x00dev,
			       enum dev_state state)
{
	int mask = (state == STATE_RADIO_IRQ_OFF);
	u32 reg;

	/*
	 * When interrupts are being enabled, the interrupt registers
	 * should clear the register to assure a clean state.
	 */
	if (state == STATE_RADIO_IRQ_ON) {
		rt2x00pci_register_read(rt2x00dev, INT_SOURCE_CSR, &reg);
		rt2x00pci_register_write(rt2x00dev, INT_SOURCE_CSR, reg);

		rt2x00pci_register_read(rt2x00dev, MCU_INT_SOURCE_CSR, &reg);
		rt2x00pci_register_write(rt2x00dev, MCU_INT_SOURCE_CSR, reg);
	}

	/*
	 * Only toggle the interrupts bits we are going to use.
	 * Non-checked interrupt bits are disabled by default.
	 */
	rt2x00pci_register_read(rt2x00dev, INT_MASK_CSR, &reg);
	rt2x00_set_field32(&reg, INT_MASK_CSR_TXDONE, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_RXDONE, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_ENABLE_MITIGATION, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_MITIGATION_PERIOD, 0xff);
	rt2x00pci_register_write(rt2x00dev, INT_MASK_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, MCU_INT_MASK_CSR, &reg);
	rt2x00_set_field32(&reg, MCU_INT_MASK_CSR_0, mask);
	rt2x00_set_field32(&reg, MCU_INT_MASK_CSR_1, mask);
	rt2x00_set_field32(&reg, MCU_INT_MASK_CSR_2, mask);
	rt2x00_set_field32(&reg, MCU_INT_MASK_CSR_3, mask);
	rt2x00_set_field32(&reg, MCU_INT_MASK_CSR_4, mask);
	rt2x00_set_field32(&reg, MCU_INT_MASK_CSR_5, mask);
	rt2x00_set_field32(&reg, MCU_INT_MASK_CSR_6, mask);
	rt2x00_set_field32(&reg, MCU_INT_MASK_CSR_7, mask);
	rt2x00pci_register_write(rt2x00dev, MCU_INT_MASK_CSR, reg);
}

static int rt61pci_enable_radio(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	/*
	 * Initialize all registers.
	 */
	if (rt61pci_init_queues(rt2x00dev) ||
	    rt61pci_init_registers(rt2x00dev) ||
	    rt61pci_init_bbp(rt2x00dev)) {
		ERROR(rt2x00dev, "Register initialization failed.\n");
		return -EIO;
	}

	/*
	 * Enable interrupts.
	 */
	rt61pci_toggle_irq(rt2x00dev, STATE_RADIO_IRQ_ON);

	/*
	 * Enable RX.
	 */
	rt2x00pci_register_read(rt2x00dev, RX_CNTL_CSR, &reg);
	rt2x00_set_field32(&reg, RX_CNTL_CSR_ENABLE_RX_DMA, 1);
	rt2x00pci_register_write(rt2x00dev, RX_CNTL_CSR, reg);

	return 0;
}

static void rt61pci_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2x00pci_register_write(rt2x00dev, MAC_CSR10, 0x00001818);

	/*
	 * Disable synchronisation.
	 */
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR9, 0);

	/*
	 * Cancel RX and TX.
	 */
	rt2x00pci_register_read(rt2x00dev, TX_CNTL_CSR, &reg);
	rt2x00_set_field32(&reg, TX_CNTL_CSR_ABORT_TX_AC0, 1);
	rt2x00_set_field32(&reg, TX_CNTL_CSR_ABORT_TX_AC1, 1);
	rt2x00_set_field32(&reg, TX_CNTL_CSR_ABORT_TX_AC2, 1);
	rt2x00_set_field32(&reg, TX_CNTL_CSR_ABORT_TX_AC3, 1);
	rt2x00pci_register_write(rt2x00dev, TX_CNTL_CSR, reg);

	/*
	 * Disable interrupts.
	 */
	rt61pci_toggle_irq(rt2x00dev, STATE_RADIO_IRQ_OFF);
}

static int rt61pci_set_state(struct rt2x00_dev *rt2x00dev, enum dev_state state)
{
	u32 reg;
	unsigned int i;
	char put_to_sleep;
	char current_state;

	put_to_sleep = (state != STATE_AWAKE);

	rt2x00pci_register_read(rt2x00dev, MAC_CSR12, &reg);
	rt2x00_set_field32(&reg, MAC_CSR12_FORCE_WAKEUP, !put_to_sleep);
	rt2x00_set_field32(&reg, MAC_CSR12_PUT_TO_SLEEP, put_to_sleep);
	rt2x00pci_register_write(rt2x00dev, MAC_CSR12, reg);

	/*
	 * Device is not guaranteed to be in the requested state yet.
	 * We must wait until the register indicates that the
	 * device has entered the correct state.
	 */
	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2x00pci_register_read(rt2x00dev, MAC_CSR12, &reg);
		current_state =
		    rt2x00_get_field32(reg, MAC_CSR12_BBP_CURRENT_STATE);
		if (current_state == !put_to_sleep)
			return 0;
		msleep(10);
	}

	NOTICE(rt2x00dev, "Device failed to enter state %d, "
	       "current device state %d.\n", !put_to_sleep, current_state);

	return -EBUSY;
}

static int rt61pci_set_device_state(struct rt2x00_dev *rt2x00dev,
				    enum dev_state state)
{
	int retval = 0;

	switch (state) {
	case STATE_RADIO_ON:
		retval = rt61pci_enable_radio(rt2x00dev);
		break;
	case STATE_RADIO_OFF:
		rt61pci_disable_radio(rt2x00dev);
		break;
	case STATE_RADIO_RX_ON:
	case STATE_RADIO_RX_ON_LINK:
		rt61pci_toggle_rx(rt2x00dev, STATE_RADIO_RX_ON);
		break;
	case STATE_RADIO_RX_OFF:
	case STATE_RADIO_RX_OFF_LINK:
		rt61pci_toggle_rx(rt2x00dev, STATE_RADIO_RX_OFF);
		break;
	case STATE_DEEP_SLEEP:
	case STATE_SLEEP:
	case STATE_STANDBY:
	case STATE_AWAKE:
		retval = rt61pci_set_state(rt2x00dev, state);
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
static void rt61pci_write_tx_desc(struct rt2x00_dev *rt2x00dev,
				    struct sk_buff *skb,
				    struct txentry_desc *txdesc,
				    struct ieee80211_tx_control *control)
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
	rt2x00_set_field32(&word, TXD_W1_IV_OFFSET, IEEE80211_HEADER);
	rt2x00_set_field32(&word, TXD_W1_HW_SEQUENCE, 1);
	rt2x00_desc_write(txd, 1, word);

	rt2x00_desc_read(txd, 2, &word);
	rt2x00_set_field32(&word, TXD_W2_PLCP_SIGNAL, txdesc->signal);
	rt2x00_set_field32(&word, TXD_W2_PLCP_SERVICE, txdesc->service);
	rt2x00_set_field32(&word, TXD_W2_PLCP_LENGTH_LOW, txdesc->length_low);
	rt2x00_set_field32(&word, TXD_W2_PLCP_LENGTH_HIGH, txdesc->length_high);
	rt2x00_desc_write(txd, 2, word);

	rt2x00_desc_read(txd, 5, &word);
	rt2x00_set_field32(&word, TXD_W5_TX_POWER,
			   TXPOWER_TO_DEV(rt2x00dev->tx_power));
	rt2x00_set_field32(&word, TXD_W5_WAITING_DMA_DONE_INT, 1);
	rt2x00_desc_write(txd, 5, word);

	if (skbdesc->desc_len > TXINFO_SIZE) {
		rt2x00_desc_read(txd, 11, &word);
		rt2x00_set_field32(&word, TXD_W11_BUFFER_LENGTH0, skbdesc->data_len);
		rt2x00_desc_write(txd, 11, word);
	}

	rt2x00_desc_read(txd, 0, &word);
	rt2x00_set_field32(&word, TXD_W0_OWNER_NIC, 1);
	rt2x00_set_field32(&word, TXD_W0_VALID, 1);
	rt2x00_set_field32(&word, TXD_W0_MORE_FRAG,
			   test_bit(ENTRY_TXD_MORE_FRAG, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_ACK,
			   test_bit(ENTRY_TXD_ACK, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_TIMESTAMP,
			   test_bit(ENTRY_TXD_REQ_TIMESTAMP, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_OFDM,
			   test_bit(ENTRY_TXD_OFDM_RATE, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_IFS, txdesc->ifs);
	rt2x00_set_field32(&word, TXD_W0_RETRY_MODE,
			   !!(control->flags &
			      IEEE80211_TXCTL_LONG_RETRY_LIMIT));
	rt2x00_set_field32(&word, TXD_W0_TKIP_MIC, 0);
	rt2x00_set_field32(&word, TXD_W0_DATABYTE_COUNT, skbdesc->data_len);
	rt2x00_set_field32(&word, TXD_W0_BURST,
			   test_bit(ENTRY_TXD_BURST, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W0_CIPHER_ALG, CIPHER_NONE);
	rt2x00_desc_write(txd, 0, word);
}

/*
 * TX data initialization
 */
static void rt61pci_kick_tx_queue(struct rt2x00_dev *rt2x00dev,
				  const unsigned int queue)
{
	u32 reg;

	if (queue == RT2X00_BCN_QUEUE_BEACON) {
		/*
		 * For Wi-Fi faily generated beacons between participating
		 * stations. Set TBTT phase adaptive adjustment step to 8us.
		 */
		rt2x00pci_register_write(rt2x00dev, TXRX_CSR10, 0x00001008);

		rt2x00pci_register_read(rt2x00dev, TXRX_CSR9, &reg);
		if (!rt2x00_get_field32(reg, TXRX_CSR9_BEACON_GEN)) {
			rt2x00_set_field32(&reg, TXRX_CSR9_TSF_TICKING, 1);
			rt2x00_set_field32(&reg, TXRX_CSR9_TBTT_ENABLE, 1);
			rt2x00_set_field32(&reg, TXRX_CSR9_BEACON_GEN, 1);
			rt2x00pci_register_write(rt2x00dev, TXRX_CSR9, reg);
		}
		return;
	}

	rt2x00pci_register_read(rt2x00dev, TX_CNTL_CSR, &reg);
	rt2x00_set_field32(&reg, TX_CNTL_CSR_KICK_TX_AC0,
			   (queue == IEEE80211_TX_QUEUE_DATA0));
	rt2x00_set_field32(&reg, TX_CNTL_CSR_KICK_TX_AC1,
			   (queue == IEEE80211_TX_QUEUE_DATA1));
	rt2x00_set_field32(&reg, TX_CNTL_CSR_KICK_TX_AC2,
			   (queue == IEEE80211_TX_QUEUE_DATA2));
	rt2x00_set_field32(&reg, TX_CNTL_CSR_KICK_TX_AC3,
			   (queue == IEEE80211_TX_QUEUE_DATA3));
	rt2x00pci_register_write(rt2x00dev, TX_CNTL_CSR, reg);
}

/*
 * RX control handlers
 */
static int rt61pci_agc_to_rssi(struct rt2x00_dev *rt2x00dev, int rxd_w1)
{
	u16 eeprom;
	u8 offset;
	u8 lna;

	lna = rt2x00_get_field32(rxd_w1, RXD_W1_RSSI_LNA);
	switch (lna) {
	case 3:
		offset = 90;
		break;
	case 2:
		offset = 74;
		break;
	case 1:
		offset = 64;
		break;
	default:
		return 0;
	}

	if (rt2x00dev->rx_status.band == IEEE80211_BAND_5GHZ) {
		if (test_bit(CONFIG_EXTERNAL_LNA_A, &rt2x00dev->flags))
			offset += 14;

		if (lna == 3 || lna == 2)
			offset += 10;

		rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_OFFSET_A, &eeprom);
		offset -= rt2x00_get_field16(eeprom, EEPROM_RSSI_OFFSET_A_1);
	} else {
		if (test_bit(CONFIG_EXTERNAL_LNA_BG, &rt2x00dev->flags))
			offset += 14;

		rt2x00_eeprom_read(rt2x00dev, EEPROM_RSSI_OFFSET_BG, &eeprom);
		offset -= rt2x00_get_field16(eeprom, EEPROM_RSSI_OFFSET_BG_1);
	}

	return rt2x00_get_field32(rxd_w1, RXD_W1_RSSI_AGC) * 2 - offset;
}

static void rt61pci_fill_rxdone(struct queue_entry *entry,
			        struct rxdone_entry_desc *rxdesc)
{
	struct queue_entry_priv_pci_rx *priv_rx = entry->priv_data;
	u32 word0;
	u32 word1;

	rt2x00_desc_read(priv_rx->desc, 0, &word0);
	rt2x00_desc_read(priv_rx->desc, 1, &word1);

	rxdesc->flags = 0;
	if (rt2x00_get_field32(word0, RXD_W0_CRC_ERROR))
		rxdesc->flags |= RX_FLAG_FAILED_FCS_CRC;

	/*
	 * Obtain the status about this packet.
	 * When frame was received with an OFDM bitrate,
	 * the signal is the PLCP value. If it was received with
	 * a CCK bitrate the signal is the rate in 100kbit/s.
	 */
	rxdesc->signal = rt2x00_get_field32(word1, RXD_W1_SIGNAL);
	rxdesc->rssi = rt61pci_agc_to_rssi(entry->queue->rt2x00dev, word1);
	rxdesc->size = rt2x00_get_field32(word0, RXD_W0_DATABYTE_COUNT);

	rxdesc->dev_flags = 0;
	if (rt2x00_get_field32(word0, RXD_W0_OFDM))
		rxdesc->dev_flags |= RXDONE_SIGNAL_PLCP;
	if (rt2x00_get_field32(word0, RXD_W0_MY_BSS))
		rxdesc->dev_flags |= RXDONE_MY_BSS;
}

/*
 * Interrupt functions.
 */
static void rt61pci_txdone(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	struct queue_entry *entry;
	struct queue_entry *entry_done;
	struct queue_entry_priv_pci_tx *priv_tx;
	struct txdone_entry_desc txdesc;
	u32 word;
	u32 reg;
	u32 old_reg;
	int type;
	int index;

	/*
	 * During each loop we will compare the freshly read
	 * STA_CSR4 register value with the value read from
	 * the previous loop. If the 2 values are equal then
	 * we should stop processing because the chance it
	 * quite big that the device has been unplugged and
	 * we risk going into an endless loop.
	 */
	old_reg = 0;

	while (1) {
		rt2x00pci_register_read(rt2x00dev, STA_CSR4, &reg);
		if (!rt2x00_get_field32(reg, STA_CSR4_VALID))
			break;

		if (old_reg == reg)
			break;
		old_reg = reg;

		/*
		 * Skip this entry when it contains an invalid
		 * queue identication number.
		 */
		type = rt2x00_get_field32(reg, STA_CSR4_PID_TYPE);
		queue = rt2x00queue_get_queue(rt2x00dev, type);
		if (unlikely(!queue))
			continue;

		/*
		 * Skip this entry when it contains an invalid
		 * index number.
		 */
		index = rt2x00_get_field32(reg, STA_CSR4_PID_SUBTYPE);
		if (unlikely(index >= queue->limit))
			continue;

		entry = &queue->entries[index];
		priv_tx = entry->priv_data;
		rt2x00_desc_read(priv_tx->desc, 0, &word);

		if (rt2x00_get_field32(word, TXD_W0_OWNER_NIC) ||
		    !rt2x00_get_field32(word, TXD_W0_VALID))
			return;

		entry_done = rt2x00queue_get_entry(queue, Q_INDEX_DONE);
		while (entry != entry_done) {
			/* Catch up.
			 * Just report any entries we missed as failed.
			 */
			WARNING(rt2x00dev,
				"TX status report missed for entry %d\n",
				entry_done->entry_idx);

			txdesc.status = TX_FAIL_OTHER;
			txdesc.retry = 0;

			rt2x00pci_txdone(rt2x00dev, entry_done, &txdesc);
			entry_done = rt2x00queue_get_entry(queue, Q_INDEX_DONE);
		}

		/*
		 * Obtain the status about this packet.
		 */
		txdesc.status = rt2x00_get_field32(reg, STA_CSR4_TX_RESULT);
		txdesc.retry = rt2x00_get_field32(reg, STA_CSR4_RETRY_COUNT);

		rt2x00pci_txdone(rt2x00dev, entry, &txdesc);
	}
}

static irqreturn_t rt61pci_interrupt(int irq, void *dev_instance)
{
	struct rt2x00_dev *rt2x00dev = dev_instance;
	u32 reg_mcu;
	u32 reg;

	/*
	 * Get the interrupt sources & saved to local variable.
	 * Write register value back to clear pending interrupts.
	 */
	rt2x00pci_register_read(rt2x00dev, MCU_INT_SOURCE_CSR, &reg_mcu);
	rt2x00pci_register_write(rt2x00dev, MCU_INT_SOURCE_CSR, reg_mcu);

	rt2x00pci_register_read(rt2x00dev, INT_SOURCE_CSR, &reg);
	rt2x00pci_register_write(rt2x00dev, INT_SOURCE_CSR, reg);

	if (!reg && !reg_mcu)
		return IRQ_NONE;

	if (!test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags))
		return IRQ_HANDLED;

	/*
	 * Handle interrupts, walk through all bits
	 * and run the tasks, the bits are checked in order of
	 * priority.
	 */

	/*
	 * 1 - Rx ring done interrupt.
	 */
	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_RXDONE))
		rt2x00pci_rxdone(rt2x00dev);

	/*
	 * 2 - Tx ring done interrupt.
	 */
	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_TXDONE))
		rt61pci_txdone(rt2x00dev);

	/*
	 * 3 - Handle MCU command done.
	 */
	if (reg_mcu)
		rt2x00pci_register_write(rt2x00dev,
					 M2H_CMD_DONE_CSR, 0xffffffff);

	return IRQ_HANDLED;
}

/*
 * Device probe functions.
 */
static int rt61pci_validate_eeprom(struct rt2x00_dev *rt2x00dev)
{
	struct eeprom_93cx6 eeprom;
	u32 reg;
	u16 word;
	u8 *mac;
	s8 value;

	rt2x00pci_register_read(rt2x00dev, E2PROM_CSR, &reg);

	eeprom.data = rt2x00dev;
	eeprom.register_read = rt61pci_eepromregister_read;
	eeprom.register_write = rt61pci_eepromregister_write;
	eeprom.width = rt2x00_get_field32(reg, E2PROM_CSR_TYPE_93C46) ?
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
		rt2x00_set_field16(&word, EEPROM_ANTENNA_NUM, 2);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_TX_DEFAULT,
				   ANTENNA_B);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_RX_DEFAULT,
				   ANTENNA_B);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_FRAME_TYPE, 0);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_DYN_TXAGC, 0);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_HARDWARE_RADIO, 0);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_RF_TYPE, RF5225);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_ANTENNA, word);
		EEPROM(rt2x00dev, "Antenna: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC, &word);
	if (word == 0xffff) {
		rt2x00_set_field16(&word, EEPROM_NIC_ENABLE_DIVERSITY, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_TX_DIVERSITY, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_TX_RX_FIXED, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_EXTERNAL_LNA_BG, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_CARDBUS_ACCEL, 0);
		rt2x00_set_field16(&word, EEPROM_NIC_EXTERNAL_LNA_A, 0);
		rt2x00_eeprom_write(rt2x00dev, EEPROM_NIC, word);
		EEPROM(rt2x00dev, "NIC: 0x%04x\n", word);
	}

	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED, &word);
	if (word == 0xffff) {
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

static int rt61pci_init_eeprom(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	u16 value;
	u16 eeprom;
	u16 device;

	/*
	 * Read EEPROM word for configuration.
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_ANTENNA, &eeprom);

	/*
	 * Identify RF chipset.
	 * To determine the RT chip we have to read the
	 * PCI header of the device.
	 */
	pci_read_config_word(rt2x00dev_pci(rt2x00dev),
			     PCI_CONFIG_HEADER_DEVICE, &device);
	value = rt2x00_get_field16(eeprom, EEPROM_ANTENNA_RF_TYPE);
	rt2x00pci_register_read(rt2x00dev, MAC_CSR0, &reg);
	rt2x00_set_chip(rt2x00dev, device, value, reg);

	if (!rt2x00_rf(&rt2x00dev->chip, RF5225) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF5325) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF2527) &&
	    !rt2x00_rf(&rt2x00dev->chip, RF2529)) {
		ERROR(rt2x00dev, "Invalid RF chipset detected.\n");
		return -ENODEV;
	}

	/*
	 * Determine number of antenna's.
	 */
	if (rt2x00_get_field16(eeprom, EEPROM_ANTENNA_NUM) == 2)
		__set_bit(CONFIG_DOUBLE_ANTENNA, &rt2x00dev->flags);

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
#ifdef CONFIG_RT61PCI_RFKILL
	if (rt2x00_get_field16(eeprom, EEPROM_ANTENNA_HARDWARE_RADIO))
		__set_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags);
#endif /* CONFIG_RT61PCI_RFKILL */

	/*
	 * Read frequency offset and RF programming sequence.
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_FREQ, &eeprom);
	if (rt2x00_get_field16(eeprom, EEPROM_FREQ_SEQ))
		__set_bit(CONFIG_RF_SEQUENCE, &rt2x00dev->flags);

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
	 * When working with a RF2529 chip without double antenna
	 * the antenna settings should be gathered from the NIC
	 * eeprom word.
	 */
	if (rt2x00_rf(&rt2x00dev->chip, RF2529) &&
	    !test_bit(CONFIG_DOUBLE_ANTENNA, &rt2x00dev->flags)) {
		switch (rt2x00_get_field16(eeprom, EEPROM_NIC_TX_RX_FIXED)) {
		case 0:
			rt2x00dev->default_ant.tx = ANTENNA_B;
			rt2x00dev->default_ant.rx = ANTENNA_A;
			break;
		case 1:
			rt2x00dev->default_ant.tx = ANTENNA_B;
			rt2x00dev->default_ant.rx = ANTENNA_B;
			break;
		case 2:
			rt2x00dev->default_ant.tx = ANTENNA_A;
			rt2x00dev->default_ant.rx = ANTENNA_A;
			break;
		case 3:
			rt2x00dev->default_ant.tx = ANTENNA_A;
			rt2x00dev->default_ant.rx = ANTENNA_B;
			break;
		}

		if (rt2x00_get_field16(eeprom, EEPROM_NIC_TX_DIVERSITY))
			rt2x00dev->default_ant.tx = ANTENNA_SW_DIVERSITY;
		if (rt2x00_get_field16(eeprom, EEPROM_NIC_ENABLE_DIVERSITY))
			rt2x00dev->default_ant.rx = ANTENNA_SW_DIVERSITY;
	}

	/*
	 * Store led settings, for correct led behaviour.
	 * If the eeprom value is invalid,
	 * switch to default led mode.
	 */
#ifdef CONFIG_RT61PCI_LEDS
	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED, &eeprom);
	value = rt2x00_get_field16(eeprom, EEPROM_LED_LED_MODE);

	rt2x00dev->led_radio.rt2x00dev = rt2x00dev;
	rt2x00dev->led_radio.type = LED_TYPE_RADIO;
	rt2x00dev->led_radio.led_dev.brightness_set =
	    rt61pci_brightness_set;
	rt2x00dev->led_radio.led_dev.blink_set =
	    rt61pci_blink_set;
	rt2x00dev->led_radio.flags = LED_INITIALIZED;

	rt2x00dev->led_assoc.rt2x00dev = rt2x00dev;
	rt2x00dev->led_assoc.type = LED_TYPE_ASSOC;
	rt2x00dev->led_assoc.led_dev.brightness_set =
	    rt61pci_brightness_set;
	rt2x00dev->led_assoc.led_dev.blink_set =
	    rt61pci_blink_set;
	rt2x00dev->led_assoc.flags = LED_INITIALIZED;

	if (value == LED_MODE_SIGNAL_STRENGTH) {
		rt2x00dev->led_qual.rt2x00dev = rt2x00dev;
		rt2x00dev->led_qual.type = LED_TYPE_QUALITY;
		rt2x00dev->led_qual.led_dev.brightness_set =
		    rt61pci_brightness_set;
		rt2x00dev->led_qual.led_dev.blink_set =
		    rt61pci_blink_set;
		rt2x00dev->led_qual.flags = LED_INITIALIZED;
	}

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
#endif /* CONFIG_RT61PCI_LEDS */

	return 0;
}

/*
 * RF value list for RF5225 & RF5325
 * Supports: 2.4 GHz & 5.2 GHz, rf_sequence disabled
 */
static const struct rf_channel rf_vals_noseq[] = {
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

/*
 * RF value list for RF5225 & RF5325
 * Supports: 2.4 GHz & 5.2 GHz, rf_sequence enabled
 */
static const struct rf_channel rf_vals_seq[] = {
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
	{ 36, 0x00002cd4, 0x0004481a, 0x00098455, 0x000c0a03 },
	{ 40, 0x00002cd0, 0x00044682, 0x00098455, 0x000c0a03 },
	{ 44, 0x00002cd0, 0x00044686, 0x00098455, 0x000c0a1b },
	{ 48, 0x00002cd0, 0x0004468e, 0x00098655, 0x000c0a0b },
	{ 52, 0x00002cd0, 0x00044692, 0x00098855, 0x000c0a23 },
	{ 56, 0x00002cd0, 0x0004469a, 0x00098c55, 0x000c0a13 },
	{ 60, 0x00002cd0, 0x000446a2, 0x00098e55, 0x000c0a03 },
	{ 64, 0x00002cd0, 0x000446a6, 0x00099255, 0x000c0a1b },

	/* 802.11 HyperLan 2 */
	{ 100, 0x00002cd4, 0x0004489a, 0x000b9855, 0x000c0a03 },
	{ 104, 0x00002cd4, 0x000448a2, 0x000b9855, 0x000c0a03 },
	{ 108, 0x00002cd4, 0x000448aa, 0x000b9855, 0x000c0a03 },
	{ 112, 0x00002cd4, 0x000448b2, 0x000b9a55, 0x000c0a03 },
	{ 116, 0x00002cd4, 0x000448ba, 0x000b9a55, 0x000c0a03 },
	{ 120, 0x00002cd0, 0x00044702, 0x000b9a55, 0x000c0a03 },
	{ 124, 0x00002cd0, 0x00044706, 0x000b9a55, 0x000c0a1b },
	{ 128, 0x00002cd0, 0x0004470e, 0x000b9c55, 0x000c0a0b },
	{ 132, 0x00002cd0, 0x00044712, 0x000b9c55, 0x000c0a23 },
	{ 136, 0x00002cd0, 0x0004471a, 0x000b9e55, 0x000c0a13 },

	/* 802.11 UNII */
	{ 140, 0x00002cd0, 0x00044722, 0x000b9e55, 0x000c0a03 },
	{ 149, 0x00002cd0, 0x0004472e, 0x000ba255, 0x000c0a1b },
	{ 153, 0x00002cd0, 0x00044736, 0x000ba255, 0x000c0a0b },
	{ 157, 0x00002cd4, 0x0004490a, 0x000ba255, 0x000c0a17 },
	{ 161, 0x00002cd4, 0x00044912, 0x000ba255, 0x000c0a17 },
	{ 165, 0x00002cd4, 0x0004491a, 0x000ba255, 0x000c0a17 },

	/* MMAC(Japan)J52 ch 34,38,42,46 */
	{ 34, 0x00002ccc, 0x0000499a, 0x0009be55, 0x000c0a0b },
	{ 38, 0x00002ccc, 0x0000499e, 0x0009be55, 0x000c0a13 },
	{ 42, 0x00002ccc, 0x000049a2, 0x0009be55, 0x000c0a1b },
	{ 46, 0x00002ccc, 0x000049a6, 0x0009be55, 0x000c0a23 },
};

static void rt61pci_probe_hw_mode(struct rt2x00_dev *rt2x00dev)
{
	struct hw_mode_spec *spec = &rt2x00dev->spec;
	u8 *txpower;
	unsigned int i;

	/*
	 * Initialize all hw fields.
	 */
	rt2x00dev->hw->flags =
	    IEEE80211_HW_HOST_GEN_BEACON_TEMPLATE |
	    IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING;
	rt2x00dev->hw->extra_tx_headroom = 0;
	rt2x00dev->hw->max_signal = MAX_SIGNAL;
	rt2x00dev->hw->max_rssi = MAX_RX_SSI;
	rt2x00dev->hw->queues = 4;

	SET_IEEE80211_DEV(rt2x00dev->hw, &rt2x00dev_pci(rt2x00dev)->dev);
	SET_IEEE80211_PERM_ADDR(rt2x00dev->hw,
				rt2x00_eeprom_addr(rt2x00dev,
						   EEPROM_MAC_ADDR_0));

	/*
	 * Convert tx_power array in eeprom.
	 */
	txpower = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_G_START);
	for (i = 0; i < 14; i++)
		txpower[i] = TXPOWER_FROM_DEV(txpower[i]);

	/*
	 * Initialize hw_mode information.
	 */
	spec->supported_bands = SUPPORT_BAND_2GHZ;
	spec->supported_rates = SUPPORT_RATE_CCK | SUPPORT_RATE_OFDM;
	spec->tx_power_a = NULL;
	spec->tx_power_bg = txpower;
	spec->tx_power_default = DEFAULT_TXPOWER;

	if (!test_bit(CONFIG_RF_SEQUENCE, &rt2x00dev->flags)) {
		spec->num_channels = 14;
		spec->channels = rf_vals_noseq;
	} else {
		spec->num_channels = 14;
		spec->channels = rf_vals_seq;
	}

	if (rt2x00_rf(&rt2x00dev->chip, RF5225) ||
	    rt2x00_rf(&rt2x00dev->chip, RF5325)) {
		spec->supported_bands |= SUPPORT_BAND_5GHZ;
		spec->num_channels = ARRAY_SIZE(rf_vals_seq);

		txpower = rt2x00_eeprom_addr(rt2x00dev, EEPROM_TXPOWER_A_START);
		for (i = 0; i < 14; i++)
			txpower[i] = TXPOWER_FROM_DEV(txpower[i]);

		spec->tx_power_a = txpower;
	}
}

static int rt61pci_probe_hw(struct rt2x00_dev *rt2x00dev)
{
	int retval;

	/*
	 * Allocate eeprom data.
	 */
	retval = rt61pci_validate_eeprom(rt2x00dev);
	if (retval)
		return retval;

	retval = rt61pci_init_eeprom(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * Initialize hw specifications.
	 */
	rt61pci_probe_hw_mode(rt2x00dev);

	/*
	 * This device requires firmware.
	 */
	__set_bit(DRIVER_REQUIRE_FIRMWARE, &rt2x00dev->flags);

	/*
	 * Set the rssi offset.
	 */
	rt2x00dev->rssi_offset = DEFAULT_RSSI_OFFSET;

	return 0;
}

/*
 * IEEE80211 stack callback functions.
 */
static int rt61pci_set_retry_limit(struct ieee80211_hw *hw,
				   u32 short_retry, u32 long_retry)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR4, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR4_LONG_RETRY_LIMIT, long_retry);
	rt2x00_set_field32(&reg, TXRX_CSR4_SHORT_RETRY_LIMIT, short_retry);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR4, reg);

	return 0;
}

static u64 rt61pci_get_tsf(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	u64 tsf;
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR13, &reg);
	tsf = (u64) rt2x00_get_field32(reg, TXRX_CSR13_HIGH_TSFTIMER) << 32;
	rt2x00pci_register_read(rt2x00dev, TXRX_CSR12, &reg);
	tsf |= rt2x00_get_field32(reg, TXRX_CSR12_LOW_TSFTIMER);

	return tsf;
}

static int rt61pci_beacon_update(struct ieee80211_hw *hw, struct sk_buff *skb,
			  struct ieee80211_tx_control *control)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct rt2x00_intf *intf = vif_to_intf(control->vif);
	struct queue_entry_priv_pci_tx *priv_tx;
	struct skb_frame_desc *skbdesc;
	unsigned int beacon_base;
	u32 reg;

	if (unlikely(!intf->beacon))
		return -ENOBUFS;

	priv_tx = intf->beacon->priv_data;
	memset(priv_tx->desc, 0, intf->beacon->queue->desc_size);

	/*
	 * Fill in skb descriptor
	 */
	skbdesc = get_skb_frame_desc(skb);
	memset(skbdesc, 0, sizeof(*skbdesc));
	skbdesc->flags |= FRAME_DESC_DRIVER_GENERATED;
	skbdesc->data = skb->data;
	skbdesc->data_len = skb->len;
	skbdesc->desc = priv_tx->desc;
	skbdesc->desc_len = intf->beacon->queue->desc_size;
	skbdesc->entry = intf->beacon;

	/*
	 * Disable beaconing while we are reloading the beacon data,
	 * otherwise we might be sending out invalid data.
	 */
	rt2x00pci_register_read(rt2x00dev, TXRX_CSR9, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR9_TSF_TICKING, 0);
	rt2x00_set_field32(&reg, TXRX_CSR9_TBTT_ENABLE, 0);
	rt2x00_set_field32(&reg, TXRX_CSR9_BEACON_GEN, 0);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR9, reg);

	/*
	 * mac80211 doesn't provide the control->queue variable
	 * for beacons. Set our own queue identification so
	 * it can be used during descriptor initialization.
	 */
	control->queue = RT2X00_BCN_QUEUE_BEACON;
	rt2x00lib_write_tx_desc(rt2x00dev, skb, control);

	/*
	 * Write entire beacon with descriptor to register,
	 * and kick the beacon generator.
	 */
	beacon_base = HW_BEACON_OFFSET(intf->beacon->entry_idx);
	rt2x00pci_register_multiwrite(rt2x00dev, beacon_base,
				      skbdesc->desc, skbdesc->desc_len);
	rt2x00pci_register_multiwrite(rt2x00dev,
				      beacon_base + skbdesc->desc_len,
				      skbdesc->data, skbdesc->data_len);
	rt61pci_kick_tx_queue(rt2x00dev, control->queue);

	return 0;
}

static const struct ieee80211_ops rt61pci_mac80211_ops = {
	.tx			= rt2x00mac_tx,
	.start			= rt2x00mac_start,
	.stop			= rt2x00mac_stop,
	.add_interface		= rt2x00mac_add_interface,
	.remove_interface	= rt2x00mac_remove_interface,
	.config			= rt2x00mac_config,
	.config_interface	= rt2x00mac_config_interface,
	.configure_filter	= rt2x00mac_configure_filter,
	.get_stats		= rt2x00mac_get_stats,
	.set_retry_limit	= rt61pci_set_retry_limit,
	.bss_info_changed	= rt2x00mac_bss_info_changed,
	.conf_tx		= rt2x00mac_conf_tx,
	.get_tx_stats		= rt2x00mac_get_tx_stats,
	.get_tsf		= rt61pci_get_tsf,
	.beacon_update		= rt61pci_beacon_update,
};

static const struct rt2x00lib_ops rt61pci_rt2x00_ops = {
	.irq_handler		= rt61pci_interrupt,
	.probe_hw		= rt61pci_probe_hw,
	.get_firmware_name	= rt61pci_get_firmware_name,
	.get_firmware_crc	= rt61pci_get_firmware_crc,
	.load_firmware		= rt61pci_load_firmware,
	.initialize		= rt2x00pci_initialize,
	.uninitialize		= rt2x00pci_uninitialize,
	.init_rxentry		= rt61pci_init_rxentry,
	.init_txentry		= rt61pci_init_txentry,
	.set_device_state	= rt61pci_set_device_state,
	.rfkill_poll		= rt61pci_rfkill_poll,
	.link_stats		= rt61pci_link_stats,
	.reset_tuner		= rt61pci_reset_tuner,
	.link_tuner		= rt61pci_link_tuner,
	.write_tx_desc		= rt61pci_write_tx_desc,
	.write_tx_data		= rt2x00pci_write_tx_data,
	.kick_tx_queue		= rt61pci_kick_tx_queue,
	.fill_rxdone		= rt61pci_fill_rxdone,
	.config_filter		= rt61pci_config_filter,
	.config_intf		= rt61pci_config_intf,
	.config_erp		= rt61pci_config_erp,
	.config			= rt61pci_config,
};

static const struct data_queue_desc rt61pci_queue_rx = {
	.entry_num		= RX_ENTRIES,
	.data_size		= DATA_FRAME_SIZE,
	.desc_size		= RXD_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_pci_rx),
};

static const struct data_queue_desc rt61pci_queue_tx = {
	.entry_num		= TX_ENTRIES,
	.data_size		= DATA_FRAME_SIZE,
	.desc_size		= TXD_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_pci_tx),
};

static const struct data_queue_desc rt61pci_queue_bcn = {
	.entry_num		= 4 * BEACON_ENTRIES,
	.data_size		= 0, /* No DMA required for beacons */
	.desc_size		= TXINFO_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_pci_tx),
};

static const struct rt2x00_ops rt61pci_ops = {
	.name		= KBUILD_MODNAME,
	.max_sta_intf	= 1,
	.max_ap_intf	= 4,
	.eeprom_size	= EEPROM_SIZE,
	.rf_size	= RF_SIZE,
	.rx		= &rt61pci_queue_rx,
	.tx		= &rt61pci_queue_tx,
	.bcn		= &rt61pci_queue_bcn,
	.lib		= &rt61pci_rt2x00_ops,
	.hw		= &rt61pci_mac80211_ops,
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	.debugfs	= &rt61pci_rt2x00debug,
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */
};

/*
 * RT61pci module information.
 */
static struct pci_device_id rt61pci_device_table[] = {
	/* RT2561s */
	{ PCI_DEVICE(0x1814, 0x0301), PCI_DEVICE_DATA(&rt61pci_ops) },
	/* RT2561 v2 */
	{ PCI_DEVICE(0x1814, 0x0302), PCI_DEVICE_DATA(&rt61pci_ops) },
	/* RT2661 */
	{ PCI_DEVICE(0x1814, 0x0401), PCI_DEVICE_DATA(&rt61pci_ops) },
	{ 0, }
};

MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("Ralink RT61 PCI & PCMCIA Wireless LAN driver.");
MODULE_SUPPORTED_DEVICE("Ralink RT2561, RT2561s & RT2661 "
			"PCI & PCMCIA chipset based cards");
MODULE_DEVICE_TABLE(pci, rt61pci_device_table);
MODULE_FIRMWARE(FIRMWARE_RT2561);
MODULE_FIRMWARE(FIRMWARE_RT2561s);
MODULE_FIRMWARE(FIRMWARE_RT2661);
MODULE_LICENSE("GPL");

static struct pci_driver rt61pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= rt61pci_device_table,
	.probe		= rt2x00pci_probe,
	.remove		= __devexit_p(rt2x00pci_remove),
	.suspend	= rt2x00pci_suspend,
	.resume		= rt2x00pci_resume,
};

static int __init rt61pci_init(void)
{
	return pci_register_driver(&rt61pci_driver);
}

static void __exit rt61pci_exit(void)
{
	pci_unregister_driver(&rt61pci_driver);
}

module_init(rt61pci_init);
module_exit(rt61pci_exit);

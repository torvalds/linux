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
	Module: rt61pci
	Abstract: rt61pci device specific routines.
	Supported chipsets: RT2561, RT2561s, RT2661.
 */

/*
 * Set enviroment defines for rt2x00.h
 */
#define DRV_NAME "rt61pci"

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
static u32 rt61pci_bbp_check(const struct rt2x00_dev *rt2x00dev)
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

static void rt61pci_bbp_write(const struct rt2x00_dev *rt2x00dev,
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

static void rt61pci_bbp_read(const struct rt2x00_dev *rt2x00dev,
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

static void rt61pci_rf_write(const struct rt2x00_dev *rt2x00dev,
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

static void rt61pci_mcu_request(const struct rt2x00_dev *rt2x00dev,
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

static void rt61pci_read_csr(const struct rt2x00_dev *rt2x00dev,
			     const unsigned int word, u32 *data)
{
	rt2x00pci_register_read(rt2x00dev, CSR_OFFSET(word), data);
}

static void rt61pci_write_csr(const struct rt2x00_dev *rt2x00dev,
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
	return rt2x00_get_field32(reg, MAC_CSR13_BIT5);;
}
#else
#define rt61pci_rfkill_poll	NULL
#endif /* CONFIG_RT61PCI_RFKILL */

/*
 * Configuration handlers.
 */
static void rt61pci_config_mac_addr(struct rt2x00_dev *rt2x00dev, __le32 *mac)
{
	u32 tmp;

	tmp = le32_to_cpu(mac[1]);
	rt2x00_set_field32(&tmp, MAC_CSR3_UNICAST_TO_ME_MASK, 0xff);
	mac[1] = cpu_to_le32(tmp);

	rt2x00pci_register_multiwrite(rt2x00dev, MAC_CSR2, mac,
				      (2 * sizeof(__le32)));
}

static void rt61pci_config_bssid(struct rt2x00_dev *rt2x00dev, __le32 *bssid)
{
	u32 tmp;

	tmp = le32_to_cpu(bssid[1]);
	rt2x00_set_field32(&tmp, MAC_CSR5_BSS_ID_MASK, 3);
	bssid[1] = cpu_to_le32(tmp);

	rt2x00pci_register_multiwrite(rt2x00dev, MAC_CSR4, bssid,
				      (2 * sizeof(__le32)));
}

static void rt61pci_config_type(struct rt2x00_dev *rt2x00dev, const int type,
				const int tsf_sync)
{
	u32 reg;

	/*
	 * Clear current synchronisation setup.
	 * For the Beacon base registers we only need to clear
	 * the first byte since that byte contains the VALID and OWNER
	 * bits which (when set to 0) will invalidate the entire beacon.
	 */
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR9, 0);
	rt2x00pci_register_write(rt2x00dev, HW_BEACON_BASE0, 0);
	rt2x00pci_register_write(rt2x00dev, HW_BEACON_BASE1, 0);
	rt2x00pci_register_write(rt2x00dev, HW_BEACON_BASE2, 0);
	rt2x00pci_register_write(rt2x00dev, HW_BEACON_BASE3, 0);

	/*
	 * Enable synchronisation.
	 */
	rt2x00pci_register_read(rt2x00dev, TXRX_CSR9, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR9_TSF_TICKING, 1);
	rt2x00_set_field32(&reg, TXRX_CSR9_TBTT_ENABLE, 1);
	rt2x00_set_field32(&reg, TXRX_CSR9_BEACON_GEN, 0);
	rt2x00_set_field32(&reg, TXRX_CSR9_TSF_SYNC, tsf_sync);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR9, reg);
}

static void rt61pci_config_preamble(struct rt2x00_dev *rt2x00dev,
				    const int short_preamble,
				    const int ack_timeout,
				    const int ack_consume_time)
{
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR0, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR0_RX_ACK_TIMEOUT, ack_timeout);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR0, reg);

	rt2x00pci_register_read(rt2x00dev, TXRX_CSR4, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR4_AUTORESPOND_PREAMBLE,
			   !!short_preamble);
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
				      const int antenna_tx,
				      const int antenna_rx)
{
	u8 r3;
	u8 r4;
	u8 r77;

	rt61pci_bbp_read(rt2x00dev, 3, &r3);
	rt61pci_bbp_read(rt2x00dev, 4, &r4);
	rt61pci_bbp_read(rt2x00dev, 77, &r77);

	rt2x00_set_field8(&r3, BBP_R3_SMART_MODE,
			  !rt2x00_rf(&rt2x00dev->chip, RF5225));

	switch (antenna_rx) {
	case ANTENNA_SW_DIVERSITY:
	case ANTENNA_HW_DIVERSITY:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 2);
		rt2x00_set_field8(&r4, BBP_R4_RX_FRAME_END,
				  !!(rt2x00dev->curr_hwmode != HWMODE_A));
		break;
	case ANTENNA_A:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 1);
		rt2x00_set_field8(&r4, BBP_R4_RX_FRAME_END, 0);

		if (rt2x00dev->curr_hwmode == HWMODE_A)
			rt2x00_set_field8(&r77, BBP_R77_PAIR, 0);
		else
			rt2x00_set_field8(&r77, BBP_R77_PAIR, 3);
		break;
	case ANTENNA_B:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 1);
		rt2x00_set_field8(&r4, BBP_R4_RX_FRAME_END, 0);

		if (rt2x00dev->curr_hwmode == HWMODE_A)
			rt2x00_set_field8(&r77, BBP_R77_PAIR, 3);
		else
			rt2x00_set_field8(&r77, BBP_R77_PAIR, 0);
		break;
	}

	rt61pci_bbp_write(rt2x00dev, 77, r77);
	rt61pci_bbp_write(rt2x00dev, 3, r3);
	rt61pci_bbp_write(rt2x00dev, 4, r4);
}

static void rt61pci_config_antenna_2x(struct rt2x00_dev *rt2x00dev,
				      const int antenna_tx,
				      const int antenna_rx)
{
	u8 r3;
	u8 r4;
	u8 r77;

	rt61pci_bbp_read(rt2x00dev, 3, &r3);
	rt61pci_bbp_read(rt2x00dev, 4, &r4);
	rt61pci_bbp_read(rt2x00dev, 77, &r77);

	rt2x00_set_field8(&r3, BBP_R3_SMART_MODE,
			  !rt2x00_rf(&rt2x00dev->chip, RF2527));
	rt2x00_set_field8(&r4, BBP_R4_RX_FRAME_END,
			  !test_bit(CONFIG_FRAME_TYPE, &rt2x00dev->flags));

	switch (antenna_rx) {
	case ANTENNA_SW_DIVERSITY:
	case ANTENNA_HW_DIVERSITY:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 2);
		break;
	case ANTENNA_A:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 1);
		rt2x00_set_field8(&r77, BBP_R77_PAIR, 3);
		break;
	case ANTENNA_B:
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 1);
		rt2x00_set_field8(&r77, BBP_R77_PAIR, 0);
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

	if (p1 != 0xff) {
		rt2x00_set_field32(&reg, MAC_CSR13_BIT4, !!p1);
		rt2x00_set_field32(&reg, MAC_CSR13_BIT12, 0);
		rt2x00pci_register_write(rt2x00dev, MAC_CSR13, reg);
	}
	if (p2 != 0xff) {
		rt2x00_set_field32(&reg, MAC_CSR13_BIT3, !p2);
		rt2x00_set_field32(&reg, MAC_CSR13_BIT11, 0);
		rt2x00pci_register_write(rt2x00dev, MAC_CSR13, reg);
	}
}

static void rt61pci_config_antenna_2529(struct rt2x00_dev *rt2x00dev,
					const int antenna_tx,
					const int antenna_rx)
{
	u16 eeprom;
	u8 r3;
	u8 r4;
	u8 r77;

	rt61pci_bbp_read(rt2x00dev, 3, &r3);
	rt61pci_bbp_read(rt2x00dev, 4, &r4);
	rt61pci_bbp_read(rt2x00dev, 77, &r77);
	rt2x00_eeprom_read(rt2x00dev, EEPROM_NIC, &eeprom);

	rt2x00_set_field8(&r3, BBP_R3_SMART_MODE, 0);

	if (rt2x00_get_field16(eeprom, EEPROM_NIC_ENABLE_DIVERSITY) &&
	    rt2x00_get_field16(eeprom, EEPROM_NIC_TX_DIVERSITY)) {
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 2);
		rt2x00_set_field8(&r4, BBP_R4_RX_FRAME_END, 1);
		rt61pci_config_antenna_2529_rx(rt2x00dev, 0, 1);
	} else if (rt2x00_get_field16(eeprom, EEPROM_NIC_ENABLE_DIVERSITY)) {
		if (rt2x00_get_field16(eeprom, EEPROM_NIC_TX_RX_FIXED) >= 2) {
			rt2x00_set_field8(&r77, BBP_R77_PAIR, 3);
			rt61pci_bbp_write(rt2x00dev, 77, r77);
		}
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 1);
		rt61pci_config_antenna_2529_rx(rt2x00dev, 1, 1);
	} else if (!rt2x00_get_field16(eeprom, EEPROM_NIC_ENABLE_DIVERSITY) &&
		   rt2x00_get_field16(eeprom, EEPROM_NIC_TX_DIVERSITY)) {
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 2);
		rt2x00_set_field8(&r4, BBP_R4_RX_FRAME_END, 0);

		switch (rt2x00_get_field16(eeprom, EEPROM_NIC_TX_RX_FIXED)) {
		case 0:
			rt61pci_config_antenna_2529_rx(rt2x00dev, 0, 1);
			break;
		case 1:
			rt61pci_config_antenna_2529_rx(rt2x00dev, 1, 0);
			break;
		case 2:
			rt61pci_config_antenna_2529_rx(rt2x00dev, 0, 0);
			break;
		case 3:
			rt61pci_config_antenna_2529_rx(rt2x00dev, 1, 1);
			break;
		}
	} else if (!rt2x00_get_field16(eeprom, EEPROM_NIC_ENABLE_DIVERSITY) &&
		   !rt2x00_get_field16(eeprom, EEPROM_NIC_TX_DIVERSITY)) {
		rt2x00_set_field8(&r4, BBP_R4_RX_ANTENNA, 1);
		rt2x00_set_field8(&r4, BBP_R4_RX_FRAME_END, 0);

		switch (rt2x00_get_field16(eeprom, EEPROM_NIC_TX_RX_FIXED)) {
		case 0:
			rt2x00_set_field8(&r77, BBP_R77_PAIR, 0);
			rt61pci_bbp_write(rt2x00dev, 77, r77);
			rt61pci_config_antenna_2529_rx(rt2x00dev, 0, 1);
			break;
		case 1:
			rt2x00_set_field8(&r77, BBP_R77_PAIR, 0);
			rt61pci_bbp_write(rt2x00dev, 77, r77);
			rt61pci_config_antenna_2529_rx(rt2x00dev, 1, 0);
			break;
		case 2:
			rt2x00_set_field8(&r77, BBP_R77_PAIR, 3);
			rt61pci_bbp_write(rt2x00dev, 77, r77);
			rt61pci_config_antenna_2529_rx(rt2x00dev, 0, 0);
			break;
		case 3:
			rt2x00_set_field8(&r77, BBP_R77_PAIR, 3);
			rt61pci_bbp_write(rt2x00dev, 77, r77);
			rt61pci_config_antenna_2529_rx(rt2x00dev, 1, 1);
			break;
		}
	}

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
				   const int antenna_tx, const int antenna_rx)
{
	const struct antenna_sel *sel;
	unsigned int lna;
	unsigned int i;
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, PHY_CSR0, &reg);

	if (rt2x00dev->curr_hwmode == HWMODE_A) {
		sel = antenna_sel_a;
		lna = test_bit(CONFIG_EXTERNAL_LNA_A, &rt2x00dev->flags);

		rt2x00_set_field32(&reg, PHY_CSR0_PA_PE_BG, 0);
		rt2x00_set_field32(&reg, PHY_CSR0_PA_PE_A, 1);
	} else {
		sel = antenna_sel_bg;
		lna = test_bit(CONFIG_EXTERNAL_LNA_BG, &rt2x00dev->flags);

		rt2x00_set_field32(&reg, PHY_CSR0_PA_PE_BG, 1);
		rt2x00_set_field32(&reg, PHY_CSR0_PA_PE_A, 0);
	}

	for (i = 0; i < ARRAY_SIZE(antenna_sel_a); i++)
		rt61pci_bbp_write(rt2x00dev, sel[i].word, sel[i].value[lna]);

	rt2x00pci_register_write(rt2x00dev, PHY_CSR0, reg);

	if (rt2x00_rf(&rt2x00dev->chip, RF5225) ||
	    rt2x00_rf(&rt2x00dev->chip, RF5325))
		rt61pci_config_antenna_5x(rt2x00dev, antenna_tx, antenna_rx);
	else if (rt2x00_rf(&rt2x00dev->chip, RF2527))
		rt61pci_config_antenna_2x(rt2x00dev, antenna_tx, antenna_rx);
	else if (rt2x00_rf(&rt2x00dev->chip, RF2529)) {
		if (test_bit(CONFIG_DOUBLE_ANTENNA, &rt2x00dev->flags))
			rt61pci_config_antenna_2x(rt2x00dev, antenna_tx,
						  antenna_rx);
		else
			rt61pci_config_antenna_2529(rt2x00dev, antenna_tx,
						    antenna_rx);
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
			   const unsigned int flags,
			   struct rt2x00lib_conf *libconf)
{
	if (flags & CONFIG_UPDATE_PHYMODE)
		rt61pci_config_phymode(rt2x00dev, libconf->basic_rates);
	if (flags & CONFIG_UPDATE_CHANNEL)
		rt61pci_config_channel(rt2x00dev, &libconf->rf,
				       libconf->conf->power_level);
	if ((flags & CONFIG_UPDATE_TXPOWER) && !(flags & CONFIG_UPDATE_CHANNEL))
		rt61pci_config_txpower(rt2x00dev, libconf->conf->power_level);
	if (flags & CONFIG_UPDATE_ANTENNA)
		rt61pci_config_antenna(rt2x00dev, libconf->conf->antenna_sel_tx,
				       libconf->conf->antenna_sel_rx);
	if (flags & (CONFIG_UPDATE_SLOT_TIME | CONFIG_UPDATE_BEACON_INT))
		rt61pci_config_duration(rt2x00dev, libconf);
}

/*
 * LED functions.
 */
static void rt61pci_enable_led(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	u16 led_reg;
	u8 arg0;
	u8 arg1;

	rt2x00pci_register_read(rt2x00dev, MAC_CSR14, &reg);
	rt2x00_set_field32(&reg, MAC_CSR14_ON_PERIOD, 70);
	rt2x00_set_field32(&reg, MAC_CSR14_OFF_PERIOD, 30);
	rt2x00pci_register_write(rt2x00dev, MAC_CSR14, reg);

	led_reg = rt2x00dev->led_reg;
	rt2x00_set_field16(&led_reg, MCU_LEDCS_RADIO_STATUS, 1);
	if (rt2x00dev->rx_status.phymode == MODE_IEEE80211A)
		rt2x00_set_field16(&led_reg, MCU_LEDCS_LINK_A_STATUS, 1);
	else
		rt2x00_set_field16(&led_reg, MCU_LEDCS_LINK_BG_STATUS, 1);

	arg0 = led_reg & 0xff;
	arg1 = (led_reg >> 8) & 0xff;

	rt61pci_mcu_request(rt2x00dev, MCU_LED, 0xff, arg0, arg1);
}

static void rt61pci_disable_led(struct rt2x00_dev *rt2x00dev)
{
	u16 led_reg;
	u8 arg0;
	u8 arg1;

	led_reg = rt2x00dev->led_reg;
	rt2x00_set_field16(&led_reg, MCU_LEDCS_RADIO_STATUS, 0);
	rt2x00_set_field16(&led_reg, MCU_LEDCS_LINK_BG_STATUS, 0);
	rt2x00_set_field16(&led_reg, MCU_LEDCS_LINK_A_STATUS, 0);

	arg0 = led_reg & 0xff;
	arg1 = (led_reg >> 8) & 0xff;

	rt61pci_mcu_request(rt2x00dev, MCU_LED, 0xff, arg0, arg1);
}

static void rt61pci_activity_led(struct rt2x00_dev *rt2x00dev, int rssi)
{
	u8 led;

	if (rt2x00dev->led_mode != LED_MODE_SIGNAL_STRENGTH)
		return;

	/*
	 * Led handling requires a positive value for the rssi,
	 * to do that correctly we need to add the correction.
	 */
	rssi += rt2x00dev->rssi_offset;

	if (rssi <= 30)
		led = 0;
	else if (rssi <= 39)
		led = 1;
	else if (rssi <= 49)
		led = 2;
	else if (rssi <= 53)
		led = 3;
	else if (rssi <= 63)
		led = 4;
	else
		led = 5;

	rt61pci_mcu_request(rt2x00dev, MCU_LED_STRENGTH, 0xff, led, 0);
}

/*
 * Link tuning
 */
static void rt61pci_link_stats(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	/*
	 * Update FCS error count from register.
	 */
	rt2x00pci_register_read(rt2x00dev, STA_CSR0, &reg);
	rt2x00dev->link.rx_failed = rt2x00_get_field32(reg, STA_CSR0_FCS_ERROR);

	/*
	 * Update False CCA count from register.
	 */
	rt2x00pci_register_read(rt2x00dev, STA_CSR1, &reg);
	rt2x00dev->link.false_cca =
	    rt2x00_get_field32(reg, STA_CSR1_FALSE_CCA_ERROR);
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

	/*
	 * Update Led strength
	 */
	rt61pci_activity_led(rt2x00dev, rssi);

	rt61pci_bbp_read(rt2x00dev, 17, &r17);

	/*
	 * Determine r17 bounds.
	 */
	if (rt2x00dev->rx_status.phymode == MODE_IEEE80211A) {
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

	/*
	 * r17 does not yet exceed upper limit, continue and base
	 * the r17 tuning on the false CCA count.
	 */
	if (rt2x00dev->link.false_cca > 512 && r17 < up_bound) {
		if (++r17 > up_bound)
			r17 = up_bound;
		rt61pci_bbp_write(rt2x00dev, 17, r17);
	} else if (rt2x00dev->link.false_cca < 100 && r17 > low_bound) {
		if (--r17 < low_bound)
			r17 = low_bound;
		rt61pci_bbp_write(rt2x00dev, 17, r17);
	}
}

/*
 * Firmware name function.
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

/*
 * Initialization functions.
 */
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

static void rt61pci_init_rxring(struct rt2x00_dev *rt2x00dev)
{
	struct data_ring *ring = rt2x00dev->rx;
	struct data_desc *rxd;
	unsigned int i;
	u32 word;

	memset(ring->data_addr, 0x00, rt2x00_get_ring_size(ring));

	for (i = 0; i < ring->stats.limit; i++) {
		rxd = ring->entry[i].priv;

		rt2x00_desc_read(rxd, 5, &word);
		rt2x00_set_field32(&word, RXD_W5_BUFFER_PHYSICAL_ADDRESS,
				   ring->entry[i].data_dma);
		rt2x00_desc_write(rxd, 5, word);

		rt2x00_desc_read(rxd, 0, &word);
		rt2x00_set_field32(&word, RXD_W0_OWNER_NIC, 1);
		rt2x00_desc_write(rxd, 0, word);
	}

	rt2x00_ring_index_clear(rt2x00dev->rx);
}

static void rt61pci_init_txring(struct rt2x00_dev *rt2x00dev, const int queue)
{
	struct data_ring *ring = rt2x00lib_get_ring(rt2x00dev, queue);
	struct data_desc *txd;
	unsigned int i;
	u32 word;

	memset(ring->data_addr, 0x00, rt2x00_get_ring_size(ring));

	for (i = 0; i < ring->stats.limit; i++) {
		txd = ring->entry[i].priv;

		rt2x00_desc_read(txd, 1, &word);
		rt2x00_set_field32(&word, TXD_W1_BUFFER_COUNT, 1);
		rt2x00_desc_write(txd, 1, word);

		rt2x00_desc_read(txd, 5, &word);
		rt2x00_set_field32(&word, TXD_W5_PID_TYPE, queue);
		rt2x00_set_field32(&word, TXD_W5_PID_SUBTYPE, i);
		rt2x00_desc_write(txd, 5, word);

		rt2x00_desc_read(txd, 6, &word);
		rt2x00_set_field32(&word, TXD_W6_BUFFER_PHYSICAL_ADDRESS,
				   ring->entry[i].data_dma);
		rt2x00_desc_write(txd, 6, word);

		rt2x00_desc_read(txd, 0, &word);
		rt2x00_set_field32(&word, TXD_W0_VALID, 0);
		rt2x00_set_field32(&word, TXD_W0_OWNER_NIC, 0);
		rt2x00_desc_write(txd, 0, word);
	}

	rt2x00_ring_index_clear(ring);
}

static int rt61pci_init_rings(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	/*
	 * Initialize rings.
	 */
	rt61pci_init_rxring(rt2x00dev);
	rt61pci_init_txring(rt2x00dev, IEEE80211_TX_QUEUE_DATA0);
	rt61pci_init_txring(rt2x00dev, IEEE80211_TX_QUEUE_DATA1);
	rt61pci_init_txring(rt2x00dev, IEEE80211_TX_QUEUE_DATA2);
	rt61pci_init_txring(rt2x00dev, IEEE80211_TX_QUEUE_DATA3);
	rt61pci_init_txring(rt2x00dev, IEEE80211_TX_QUEUE_DATA4);

	/*
	 * Initialize registers.
	 */
	rt2x00pci_register_read(rt2x00dev, TX_RING_CSR0, &reg);
	rt2x00_set_field32(&reg, TX_RING_CSR0_AC0_RING_SIZE,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA0].stats.limit);
	rt2x00_set_field32(&reg, TX_RING_CSR0_AC1_RING_SIZE,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA1].stats.limit);
	rt2x00_set_field32(&reg, TX_RING_CSR0_AC2_RING_SIZE,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA2].stats.limit);
	rt2x00_set_field32(&reg, TX_RING_CSR0_AC3_RING_SIZE,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA3].stats.limit);
	rt2x00pci_register_write(rt2x00dev, TX_RING_CSR0, reg);

	rt2x00pci_register_read(rt2x00dev, TX_RING_CSR1, &reg);
	rt2x00_set_field32(&reg, TX_RING_CSR1_MGMT_RING_SIZE,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA4].stats.limit);
	rt2x00_set_field32(&reg, TX_RING_CSR1_TXD_SIZE,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA0].desc_size /
			   4);
	rt2x00pci_register_write(rt2x00dev, TX_RING_CSR1, reg);

	rt2x00pci_register_read(rt2x00dev, AC0_BASE_CSR, &reg);
	rt2x00_set_field32(&reg, AC0_BASE_CSR_RING_REGISTER,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA0].data_dma);
	rt2x00pci_register_write(rt2x00dev, AC0_BASE_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, AC1_BASE_CSR, &reg);
	rt2x00_set_field32(&reg, AC1_BASE_CSR_RING_REGISTER,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA1].data_dma);
	rt2x00pci_register_write(rt2x00dev, AC1_BASE_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, AC2_BASE_CSR, &reg);
	rt2x00_set_field32(&reg, AC2_BASE_CSR_RING_REGISTER,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA2].data_dma);
	rt2x00pci_register_write(rt2x00dev, AC2_BASE_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, AC3_BASE_CSR, &reg);
	rt2x00_set_field32(&reg, AC3_BASE_CSR_RING_REGISTER,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA3].data_dma);
	rt2x00pci_register_write(rt2x00dev, AC3_BASE_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, MGMT_BASE_CSR, &reg);
	rt2x00_set_field32(&reg, MGMT_BASE_CSR_RING_REGISTER,
			   rt2x00dev->tx[IEEE80211_TX_QUEUE_DATA4].data_dma);
	rt2x00pci_register_write(rt2x00dev, MGMT_BASE_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, RX_RING_CSR, &reg);
	rt2x00_set_field32(&reg, RX_RING_CSR_RING_SIZE,
			   rt2x00dev->rx->stats.limit);
	rt2x00_set_field32(&reg, RX_RING_CSR_RXD_SIZE,
			   rt2x00dev->rx->desc_size / 4);
	rt2x00_set_field32(&reg, RX_RING_CSR_RXD_WRITEBACK_SIZE, 4);
	rt2x00pci_register_write(rt2x00dev, RX_RING_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, RX_BASE_CSR, &reg);
	rt2x00_set_field32(&reg, RX_BASE_CSR_RING_REGISTER,
			   rt2x00dev->rx->data_dma);
	rt2x00pci_register_write(rt2x00dev, RX_BASE_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, TX_DMA_DST_CSR, &reg);
	rt2x00_set_field32(&reg, TX_DMA_DST_CSR_DEST_AC0, 2);
	rt2x00_set_field32(&reg, TX_DMA_DST_CSR_DEST_AC1, 2);
	rt2x00_set_field32(&reg, TX_DMA_DST_CSR_DEST_AC2, 2);
	rt2x00_set_field32(&reg, TX_DMA_DST_CSR_DEST_AC3, 2);
	rt2x00_set_field32(&reg, TX_DMA_DST_CSR_DEST_MGMT, 0);
	rt2x00pci_register_write(rt2x00dev, TX_DMA_DST_CSR, reg);

	rt2x00pci_register_read(rt2x00dev, LOAD_TX_RING_CSR, &reg);
	rt2x00_set_field32(&reg, LOAD_TX_RING_CSR_LOAD_TXD_AC0, 1);
	rt2x00_set_field32(&reg, LOAD_TX_RING_CSR_LOAD_TXD_AC1, 1);
	rt2x00_set_field32(&reg, LOAD_TX_RING_CSR_LOAD_TXD_AC2, 1);
	rt2x00_set_field32(&reg, LOAD_TX_RING_CSR_LOAD_TXD_AC3, 1);
	rt2x00_set_field32(&reg, LOAD_TX_RING_CSR_LOAD_TXD_MGMT, 1);
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

	DEBUG(rt2x00dev, "Start initialization from EEPROM...\n");
	for (i = 0; i < EEPROM_BBP_SIZE; i++) {
		rt2x00_eeprom_read(rt2x00dev, EEPROM_BBP_START + i, &eeprom);

		if (eeprom != 0xffff && eeprom != 0x0000) {
			reg_id = rt2x00_get_field16(eeprom, EEPROM_BBP_REG_ID);
			value = rt2x00_get_field16(eeprom, EEPROM_BBP_VALUE);
			DEBUG(rt2x00dev, "BBP: 0x%02x, value: 0x%02x.\n",
			      reg_id, value);
			rt61pci_bbp_write(rt2x00dev, reg_id, value);
		}
	}
	DEBUG(rt2x00dev, "...End initialization from EEPROM.\n");

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
	if (rt61pci_init_rings(rt2x00dev) ||
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

	/*
	 * Enable LED
	 */
	rt61pci_enable_led(rt2x00dev);

	return 0;
}

static void rt61pci_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	/*
	 * Disable LED
	 */
	rt61pci_disable_led(rt2x00dev);

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
	rt2x00_set_field32(&reg, TX_CNTL_CSR_ABORT_TX_MGMT, 1);
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
	case STATE_RADIO_RX_OFF:
		rt61pci_toggle_rx(rt2x00dev, state);
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
				  struct data_desc *txd,
				  struct txdata_entry_desc *desc,
				  struct ieee80211_hdr *ieee80211hdr,
				  unsigned int length,
				  struct ieee80211_tx_control *control)
{
	u32 word;

	/*
	 * Start writing the descriptor words.
	 */
	rt2x00_desc_read(txd, 1, &word);
	rt2x00_set_field32(&word, TXD_W1_HOST_Q_ID, desc->queue);
	rt2x00_set_field32(&word, TXD_W1_AIFSN, desc->aifs);
	rt2x00_set_field32(&word, TXD_W1_CWMIN, desc->cw_min);
	rt2x00_set_field32(&word, TXD_W1_CWMAX, desc->cw_max);
	rt2x00_set_field32(&word, TXD_W1_IV_OFFSET, IEEE80211_HEADER);
	rt2x00_set_field32(&word, TXD_W1_HW_SEQUENCE, 1);
	rt2x00_desc_write(txd, 1, word);

	rt2x00_desc_read(txd, 2, &word);
	rt2x00_set_field32(&word, TXD_W2_PLCP_SIGNAL, desc->signal);
	rt2x00_set_field32(&word, TXD_W2_PLCP_SERVICE, desc->service);
	rt2x00_set_field32(&word, TXD_W2_PLCP_LENGTH_LOW, desc->length_low);
	rt2x00_set_field32(&word, TXD_W2_PLCP_LENGTH_HIGH, desc->length_high);
	rt2x00_desc_write(txd, 2, word);

	rt2x00_desc_read(txd, 5, &word);
	rt2x00_set_field32(&word, TXD_W5_TX_POWER,
			   TXPOWER_TO_DEV(control->power_level));
	rt2x00_set_field32(&word, TXD_W5_WAITING_DMA_DONE_INT, 1);
	rt2x00_desc_write(txd, 5, word);

	rt2x00_desc_read(txd, 11, &word);
	rt2x00_set_field32(&word, TXD_W11_BUFFER_LENGTH0, length);
	rt2x00_desc_write(txd, 11, word);

	rt2x00_desc_read(txd, 0, &word);
	rt2x00_set_field32(&word, TXD_W0_OWNER_NIC, 1);
	rt2x00_set_field32(&word, TXD_W0_VALID, 1);
	rt2x00_set_field32(&word, TXD_W0_MORE_FRAG,
			   test_bit(ENTRY_TXD_MORE_FRAG, &desc->flags));
	rt2x00_set_field32(&word, TXD_W0_ACK,
			   !(control->flags & IEEE80211_TXCTL_NO_ACK));
	rt2x00_set_field32(&word, TXD_W0_TIMESTAMP,
			   test_bit(ENTRY_TXD_REQ_TIMESTAMP, &desc->flags));
	rt2x00_set_field32(&word, TXD_W0_OFDM,
			   test_bit(ENTRY_TXD_OFDM_RATE, &desc->flags));
	rt2x00_set_field32(&word, TXD_W0_IFS, desc->ifs);
	rt2x00_set_field32(&word, TXD_W0_RETRY_MODE,
			   !!(control->flags &
			      IEEE80211_TXCTL_LONG_RETRY_LIMIT));
	rt2x00_set_field32(&word, TXD_W0_TKIP_MIC, 0);
	rt2x00_set_field32(&word, TXD_W0_DATABYTE_COUNT, length);
	rt2x00_set_field32(&word, TXD_W0_BURST,
			   test_bit(ENTRY_TXD_BURST, &desc->flags));
	rt2x00_set_field32(&word, TXD_W0_CIPHER_ALG, CIPHER_NONE);
	rt2x00_desc_write(txd, 0, word);
}

/*
 * TX data initialization
 */
static void rt61pci_kick_tx_queue(struct rt2x00_dev *rt2x00dev,
				  unsigned int queue)
{
	u32 reg;

	if (queue == IEEE80211_TX_QUEUE_BEACON) {
		/*
		 * For Wi-Fi faily generated beacons between participating
		 * stations. Set TBTT phase adaptive adjustment step to 8us.
		 */
		rt2x00pci_register_write(rt2x00dev, TXRX_CSR10, 0x00001008);

		rt2x00pci_register_read(rt2x00dev, TXRX_CSR9, &reg);
		if (!rt2x00_get_field32(reg, TXRX_CSR9_BEACON_GEN)) {
			rt2x00_set_field32(&reg, TXRX_CSR9_BEACON_GEN, 1);
			rt2x00pci_register_write(rt2x00dev, TXRX_CSR9, reg);
		}
		return;
	}

	rt2x00pci_register_read(rt2x00dev, TX_CNTL_CSR, &reg);
	if (queue == IEEE80211_TX_QUEUE_DATA0)
		rt2x00_set_field32(&reg, TX_CNTL_CSR_KICK_TX_AC0, 1);
	else if (queue == IEEE80211_TX_QUEUE_DATA1)
		rt2x00_set_field32(&reg, TX_CNTL_CSR_KICK_TX_AC1, 1);
	else if (queue == IEEE80211_TX_QUEUE_DATA2)
		rt2x00_set_field32(&reg, TX_CNTL_CSR_KICK_TX_AC2, 1);
	else if (queue == IEEE80211_TX_QUEUE_DATA3)
		rt2x00_set_field32(&reg, TX_CNTL_CSR_KICK_TX_AC3, 1);
	else if (queue == IEEE80211_TX_QUEUE_DATA4)
		rt2x00_set_field32(&reg, TX_CNTL_CSR_KICK_TX_MGMT, 1);
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

	if (rt2x00dev->rx_status.phymode == MODE_IEEE80211A) {
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

static void rt61pci_fill_rxdone(struct data_entry *entry,
			        struct rxdata_entry_desc *desc)
{
	struct data_desc *rxd = entry->priv;
	u32 word0;
	u32 word1;

	rt2x00_desc_read(rxd, 0, &word0);
	rt2x00_desc_read(rxd, 1, &word1);

	desc->flags = 0;
	if (rt2x00_get_field32(word0, RXD_W0_CRC_ERROR))
		desc->flags |= RX_FLAG_FAILED_FCS_CRC;

	/*
	 * Obtain the status about this packet.
	 */
	desc->signal = rt2x00_get_field32(word1, RXD_W1_SIGNAL);
	desc->rssi = rt61pci_agc_to_rssi(entry->ring->rt2x00dev, word1);
	desc->ofdm = rt2x00_get_field32(word0, RXD_W0_OFDM);
	desc->size = rt2x00_get_field32(word0, RXD_W0_DATABYTE_COUNT);

	return;
}

/*
 * Interrupt functions.
 */
static void rt61pci_txdone(struct rt2x00_dev *rt2x00dev)
{
	struct data_ring *ring;
	struct data_entry *entry;
	struct data_entry *entry_done;
	struct data_desc *txd;
	u32 word;
	u32 reg;
	u32 old_reg;
	int type;
	int index;
	int tx_status;
	int retry;

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
		 * ring identication number.
		 */
		type = rt2x00_get_field32(reg, STA_CSR4_PID_TYPE);
		ring = rt2x00lib_get_ring(rt2x00dev, type);
		if (unlikely(!ring))
			continue;

		/*
		 * Skip this entry when it contains an invalid
		 * index number.
		 */
		index = rt2x00_get_field32(reg, STA_CSR4_PID_SUBTYPE);
		if (unlikely(index >= ring->stats.limit))
			continue;

		entry = &ring->entry[index];
		txd = entry->priv;
		rt2x00_desc_read(txd, 0, &word);

		if (rt2x00_get_field32(word, TXD_W0_OWNER_NIC) ||
		    !rt2x00_get_field32(word, TXD_W0_VALID))
			return;

		entry_done = rt2x00_get_data_entry_done(ring);
		while (entry != entry_done) {
			/* Catch up. Just report any entries we missed as
			 * failed. */
			WARNING(rt2x00dev,
				"TX status report missed for entry %p\n",
				entry_done);
			rt2x00lib_txdone(entry_done, TX_FAIL_OTHER, 0);
			entry_done = rt2x00_get_data_entry_done(ring);
		}

		/*
		 * Obtain the status about this packet.
		 */
		tx_status = rt2x00_get_field32(reg, STA_CSR4_TX_RESULT);
		retry = rt2x00_get_field32(reg, STA_CSR4_RETRY_COUNT);

		rt2x00lib_txdone(entry, tx_status, retry);

		/*
		 * Make this entry available for reuse.
		 */
		entry->flags = 0;
		rt2x00_set_field32(&word, TXD_W0_VALID, 0);
		rt2x00_desc_write(txd, 0, word);
		rt2x00_ring_index_done_inc(entry->ring);

		/*
		 * If the data ring was full before the txdone handler
		 * we must make sure the packet queue in the mac80211 stack
		 * is reenabled when the txdone handler has finished.
		 */
		if (!rt2x00_ring_full(ring))
			ieee80211_wake_queue(rt2x00dev->hw,
					     entry->tx_status.control.queue);
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
		rt2x00_set_field16(&word, EEPROM_ANTENNA_TX_DEFAULT, 2);
		rt2x00_set_field16(&word, EEPROM_ANTENNA_RX_DEFAULT, 2);
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
		EEPROM(rt2x00dev, "RSSI OFFSET BG: 0x%04x\n", word);
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
	 * Identify default antenna configuration.
	 */
	rt2x00dev->hw->conf.antenna_sel_tx =
	    rt2x00_get_field16(eeprom, EEPROM_ANTENNA_TX_DEFAULT);
	rt2x00dev->hw->conf.antenna_sel_rx =
	    rt2x00_get_field16(eeprom, EEPROM_ANTENNA_RX_DEFAULT);

	/*
	 * Read the Frame type.
	 */
	if (rt2x00_get_field16(eeprom, EEPROM_ANTENNA_FRAME_TYPE))
		__set_bit(CONFIG_FRAME_TYPE, &rt2x00dev->flags);

	/*
	 * Determine number of antenna's.
	 */
	if (rt2x00_get_field16(eeprom, EEPROM_ANTENNA_NUM) == 2)
		__set_bit(CONFIG_DOUBLE_ANTENNA, &rt2x00dev->flags);

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
	 * Store led settings, for correct led behaviour.
	 * If the eeprom value is invalid,
	 * switch to default led mode.
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED, &eeprom);

	rt2x00dev->led_mode = rt2x00_get_field16(eeprom, EEPROM_LED_LED_MODE);

	rt2x00_set_field16(&rt2x00dev->led_reg, MCU_LEDCS_LED_MODE,
			   rt2x00dev->led_mode);
	rt2x00_set_field16(&rt2x00dev->led_reg, MCU_LEDCS_POLARITY_GPIO_0,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_GPIO_0));
	rt2x00_set_field16(&rt2x00dev->led_reg, MCU_LEDCS_POLARITY_GPIO_1,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_GPIO_1));
	rt2x00_set_field16(&rt2x00dev->led_reg, MCU_LEDCS_POLARITY_GPIO_2,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_GPIO_2));
	rt2x00_set_field16(&rt2x00dev->led_reg, MCU_LEDCS_POLARITY_GPIO_3,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_GPIO_3));
	rt2x00_set_field16(&rt2x00dev->led_reg, MCU_LEDCS_POLARITY_GPIO_4,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_GPIO_4));
	rt2x00_set_field16(&rt2x00dev->led_reg, MCU_LEDCS_POLARITY_ACT,
			   rt2x00_get_field16(eeprom, EEPROM_LED_POLARITY_ACT));
	rt2x00_set_field16(&rt2x00dev->led_reg, MCU_LEDCS_POLARITY_READY_BG,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_RDY_G));
	rt2x00_set_field16(&rt2x00dev->led_reg, MCU_LEDCS_POLARITY_READY_A,
			   rt2x00_get_field16(eeprom,
					      EEPROM_LED_POLARITY_RDY_A));

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
	rt2x00dev->hw->queues = 5;

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
	spec->num_modes = 2;
	spec->num_rates = 12;
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
		spec->num_modes = 3;
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
	 * This device requires firmware
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
static void rt61pci_configure_filter(struct ieee80211_hw *hw,
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
	if (mc_count)
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
	 * and broadcast frames will always be accepted since
	 * there is no filter for it at this time.
	 */
	rt2x00pci_register_read(rt2x00dev, TXRX_CSR0, &reg);
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_CRC,
			   !(*total_flags & FIF_FCSFAIL));
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_PHYSICAL,
			   !(*total_flags & FIF_PLCPFAIL));
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_CONTROL,
			   !(*total_flags & FIF_CONTROL));
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_NOT_TO_ME,
			   !(*total_flags & FIF_PROMISC_IN_BSS));
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_TO_DS,
			   !(*total_flags & FIF_PROMISC_IN_BSS));
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_VERSION_ERROR, 1);
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_MULTICAST,
			   !(*total_flags & FIF_ALLMULTI));
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_BORADCAST, 0);
	rt2x00_set_field32(&reg, TXRX_CSR0_DROP_ACK_CTS, 1);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR0, reg);
}

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

static void rt61pci_reset_tsf(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	rt2x00pci_register_write(rt2x00dev, TXRX_CSR12, 0);
	rt2x00pci_register_write(rt2x00dev, TXRX_CSR13, 0);
}

static int rt61pci_beacon_update(struct ieee80211_hw *hw, struct sk_buff *skb,
			  struct ieee80211_tx_control *control)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	/*
	 * Just in case the ieee80211 doesn't set this,
	 * but we need this queue set for the descriptor
	 * initialization.
	 */
	control->queue = IEEE80211_TX_QUEUE_BEACON;

	/*
	 * We need to append the descriptor in front of the
	 * beacon frame.
	 */
	if (skb_headroom(skb) < TXD_DESC_SIZE) {
		if (pskb_expand_head(skb, TXD_DESC_SIZE, 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			return -ENOMEM;
		}
	}

	/*
	 * First we create the beacon.
	 */
	skb_push(skb, TXD_DESC_SIZE);
	memset(skb->data, 0, TXD_DESC_SIZE);

	rt2x00lib_write_tx_desc(rt2x00dev, (struct data_desc *)skb->data,
				(struct ieee80211_hdr *)(skb->data +
							 TXD_DESC_SIZE),
				skb->len - TXD_DESC_SIZE, control);

	/*
	 * Write entire beacon with descriptor to register,
	 * and kick the beacon generator.
	 */
	rt2x00pci_register_multiwrite(rt2x00dev, HW_BEACON_BASE0,
				      skb->data, skb->len);
	rt61pci_kick_tx_queue(rt2x00dev, IEEE80211_TX_QUEUE_BEACON);

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
	.configure_filter	= rt61pci_configure_filter,
	.get_stats		= rt2x00mac_get_stats,
	.set_retry_limit	= rt61pci_set_retry_limit,
	.erp_ie_changed		= rt2x00mac_erp_ie_changed,
	.conf_tx		= rt2x00mac_conf_tx,
	.get_tx_stats		= rt2x00mac_get_tx_stats,
	.get_tsf		= rt61pci_get_tsf,
	.reset_tsf		= rt61pci_reset_tsf,
	.beacon_update		= rt61pci_beacon_update,
};

static const struct rt2x00lib_ops rt61pci_rt2x00_ops = {
	.irq_handler		= rt61pci_interrupt,
	.probe_hw		= rt61pci_probe_hw,
	.get_firmware_name	= rt61pci_get_firmware_name,
	.load_firmware		= rt61pci_load_firmware,
	.initialize		= rt2x00pci_initialize,
	.uninitialize		= rt2x00pci_uninitialize,
	.set_device_state	= rt61pci_set_device_state,
	.rfkill_poll		= rt61pci_rfkill_poll,
	.link_stats		= rt61pci_link_stats,
	.reset_tuner		= rt61pci_reset_tuner,
	.link_tuner		= rt61pci_link_tuner,
	.write_tx_desc		= rt61pci_write_tx_desc,
	.write_tx_data		= rt2x00pci_write_tx_data,
	.kick_tx_queue		= rt61pci_kick_tx_queue,
	.fill_rxdone		= rt61pci_fill_rxdone,
	.config_mac_addr	= rt61pci_config_mac_addr,
	.config_bssid		= rt61pci_config_bssid,
	.config_type		= rt61pci_config_type,
	.config_preamble	= rt61pci_config_preamble,
	.config			= rt61pci_config,
};

static const struct rt2x00_ops rt61pci_ops = {
	.name		= DRV_NAME,
	.rxd_size	= RXD_DESC_SIZE,
	.txd_size	= TXD_DESC_SIZE,
	.eeprom_size	= EEPROM_SIZE,
	.rf_size	= RF_SIZE,
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
	.name		= DRV_NAME,
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

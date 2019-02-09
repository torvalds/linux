/*
	Copyright (C) 2009 - 2010 Ivo van Doorn <IvDoorn@gmail.com>
	Copyright (C) 2009 Alban Browaeys <prahal@yahoo.com>
	Copyright (C) 2009 Felix Fietkau <nbd@openwrt.org>
	Copyright (C) 2009 Luis Correia <luis.f.correia@gmail.com>
	Copyright (C) 2009 Mattias Nissler <mattias.nissler@gmx.de>
	Copyright (C) 2009 Mark Asselstine <asselsm@gmail.com>
	Copyright (C) 2009 Xose Vazquez Perez <xose.vazquez@gmail.com>
	Copyright (C) 2009 Bart Zolnierkiewicz <bzolnier@gmail.com>
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
	along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
	Module: rt2800pci
	Abstract: rt2800pci device specific routines.
	Supported chipsets: RT2800E & RT2800ED.
 */

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/eeprom_93cx6.h>

#include "rt2x00.h"
#include "rt2x00mmio.h"
#include "rt2x00pci.h"
#include "rt2800lib.h"
#include "rt2800mmio.h"
#include "rt2800.h"
#include "rt2800pci.h"

/*
 * Allow hardware encryption to be disabled.
 */
static bool modparam_nohwcrypt = false;
module_param_named(nohwcrypt, modparam_nohwcrypt, bool, 0444);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption.");

static bool rt2800pci_hwcrypt_disabled(struct rt2x00_dev *rt2x00dev)
{
	return modparam_nohwcrypt;
}

static void rt2800pci_mcu_status(struct rt2x00_dev *rt2x00dev, const u8 token)
{
	unsigned int i;
	u32 reg;

	/*
	 * SOC devices don't support MCU requests.
	 */
	if (rt2x00_is_soc(rt2x00dev))
		return;

	for (i = 0; i < 200; i++) {
		reg = rt2x00mmio_register_read(rt2x00dev, H2M_MAILBOX_CID);

		if ((rt2x00_get_field32(reg, H2M_MAILBOX_CID_CMD0) == token) ||
		    (rt2x00_get_field32(reg, H2M_MAILBOX_CID_CMD1) == token) ||
		    (rt2x00_get_field32(reg, H2M_MAILBOX_CID_CMD2) == token) ||
		    (rt2x00_get_field32(reg, H2M_MAILBOX_CID_CMD3) == token))
			break;

		udelay(REGISTER_BUSY_DELAY);
	}

	if (i == 200)
		rt2x00_err(rt2x00dev, "MCU request failed, no response from hardware\n");

	rt2x00mmio_register_write(rt2x00dev, H2M_MAILBOX_STATUS, ~0);
	rt2x00mmio_register_write(rt2x00dev, H2M_MAILBOX_CID, ~0);
}

static void rt2800pci_eepromregister_read(struct eeprom_93cx6 *eeprom)
{
	struct rt2x00_dev *rt2x00dev = eeprom->data;
	u32 reg;

	reg = rt2x00mmio_register_read(rt2x00dev, E2PROM_CSR);

	eeprom->reg_data_in = !!rt2x00_get_field32(reg, E2PROM_CSR_DATA_IN);
	eeprom->reg_data_out = !!rt2x00_get_field32(reg, E2PROM_CSR_DATA_OUT);
	eeprom->reg_data_clock =
	    !!rt2x00_get_field32(reg, E2PROM_CSR_DATA_CLOCK);
	eeprom->reg_chip_select =
	    !!rt2x00_get_field32(reg, E2PROM_CSR_CHIP_SELECT);
}

static void rt2800pci_eepromregister_write(struct eeprom_93cx6 *eeprom)
{
	struct rt2x00_dev *rt2x00dev = eeprom->data;
	u32 reg = 0;

	rt2x00_set_field32(&reg, E2PROM_CSR_DATA_IN, !!eeprom->reg_data_in);
	rt2x00_set_field32(&reg, E2PROM_CSR_DATA_OUT, !!eeprom->reg_data_out);
	rt2x00_set_field32(&reg, E2PROM_CSR_DATA_CLOCK,
			   !!eeprom->reg_data_clock);
	rt2x00_set_field32(&reg, E2PROM_CSR_CHIP_SELECT,
			   !!eeprom->reg_chip_select);

	rt2x00mmio_register_write(rt2x00dev, E2PROM_CSR, reg);
}

static int rt2800pci_read_eeprom_pci(struct rt2x00_dev *rt2x00dev)
{
	struct eeprom_93cx6 eeprom;
	u32 reg;

	reg = rt2x00mmio_register_read(rt2x00dev, E2PROM_CSR);

	eeprom.data = rt2x00dev;
	eeprom.register_read = rt2800pci_eepromregister_read;
	eeprom.register_write = rt2800pci_eepromregister_write;
	switch (rt2x00_get_field32(reg, E2PROM_CSR_TYPE))
	{
	case 0:
		eeprom.width = PCI_EEPROM_WIDTH_93C46;
		break;
	case 1:
		eeprom.width = PCI_EEPROM_WIDTH_93C66;
		break;
	default:
		eeprom.width = PCI_EEPROM_WIDTH_93C86;
		break;
	}
	eeprom.reg_data_in = 0;
	eeprom.reg_data_out = 0;
	eeprom.reg_data_clock = 0;
	eeprom.reg_chip_select = 0;

	eeprom_93cx6_multiread(&eeprom, EEPROM_BASE, rt2x00dev->eeprom,
			       EEPROM_SIZE / sizeof(u16));

	return 0;
}

static int rt2800pci_efuse_detect(struct rt2x00_dev *rt2x00dev)
{
	return rt2800_efuse_detect(rt2x00dev);
}

static inline int rt2800pci_read_eeprom_efuse(struct rt2x00_dev *rt2x00dev)
{
	return rt2800_read_eeprom_efuse(rt2x00dev);
}

/*
 * Firmware functions
 */
static char *rt2800pci_get_firmware_name(struct rt2x00_dev *rt2x00dev)
{
	/*
	 * Chip rt3290 use specific 4KB firmware named rt3290.bin.
	 */
	if (rt2x00_rt(rt2x00dev, RT3290))
		return FIRMWARE_RT3290;
	else
		return FIRMWARE_RT2860;
}

static int rt2800pci_write_firmware(struct rt2x00_dev *rt2x00dev,
				    const u8 *data, const size_t len)
{
	u32 reg;

	/*
	 * enable Host program ram write selection
	 */
	reg = 0;
	rt2x00_set_field32(&reg, PBF_SYS_CTRL_HOST_RAM_WRITE, 1);
	rt2x00mmio_register_write(rt2x00dev, PBF_SYS_CTRL, reg);

	/*
	 * Write firmware to device.
	 */
	rt2x00mmio_register_multiwrite(rt2x00dev, FIRMWARE_IMAGE_BASE,
				       data, len);

	rt2x00mmio_register_write(rt2x00dev, PBF_SYS_CTRL, 0x00000);
	rt2x00mmio_register_write(rt2x00dev, PBF_SYS_CTRL, 0x00001);

	rt2x00mmio_register_write(rt2x00dev, H2M_BBP_AGENT, 0);
	rt2x00mmio_register_write(rt2x00dev, H2M_MAILBOX_CSR, 0);

	return 0;
}

/*
 * Device state switch handlers.
 */
static int rt2800pci_enable_radio(struct rt2x00_dev *rt2x00dev)
{
	int retval;

	retval = rt2800mmio_enable_radio(rt2x00dev);
	if (retval)
		return retval;

	/* After resume MCU_BOOT_SIGNAL will trash these. */
	rt2x00mmio_register_write(rt2x00dev, H2M_MAILBOX_STATUS, ~0);
	rt2x00mmio_register_write(rt2x00dev, H2M_MAILBOX_CID, ~0);

	rt2800_mcu_request(rt2x00dev, MCU_SLEEP, TOKEN_RADIO_OFF, 0xff, 0x02);
	rt2800pci_mcu_status(rt2x00dev, TOKEN_RADIO_OFF);

	rt2800_mcu_request(rt2x00dev, MCU_WAKEUP, TOKEN_WAKEUP, 0, 0);
	rt2800pci_mcu_status(rt2x00dev, TOKEN_WAKEUP);

	return retval;
}

static int rt2800pci_set_state(struct rt2x00_dev *rt2x00dev,
			       enum dev_state state)
{
	if (state == STATE_AWAKE) {
		rt2800_mcu_request(rt2x00dev, MCU_WAKEUP, TOKEN_WAKEUP,
				   0, 0x02);
		rt2800pci_mcu_status(rt2x00dev, TOKEN_WAKEUP);
	} else if (state == STATE_SLEEP) {
		rt2x00mmio_register_write(rt2x00dev, H2M_MAILBOX_STATUS,
					  0xffffffff);
		rt2x00mmio_register_write(rt2x00dev, H2M_MAILBOX_CID,
					  0xffffffff);
		rt2800_mcu_request(rt2x00dev, MCU_SLEEP, TOKEN_SLEEP,
				   0xff, 0x01);
	}

	return 0;
}

static int rt2800pci_set_device_state(struct rt2x00_dev *rt2x00dev,
				      enum dev_state state)
{
	int retval = 0;

	switch (state) {
	case STATE_RADIO_ON:
		retval = rt2800pci_enable_radio(rt2x00dev);
		break;
	case STATE_RADIO_OFF:
		/*
		 * After the radio has been disabled, the device should
		 * be put to sleep for powersaving.
		 */
		rt2800pci_set_state(rt2x00dev, STATE_SLEEP);
		break;
	case STATE_RADIO_IRQ_ON:
	case STATE_RADIO_IRQ_OFF:
		rt2800mmio_toggle_irq(rt2x00dev, state);
		break;
	case STATE_DEEP_SLEEP:
	case STATE_SLEEP:
	case STATE_STANDBY:
	case STATE_AWAKE:
		retval = rt2800pci_set_state(rt2x00dev, state);
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
 * Device probe functions.
 */
static int rt2800pci_read_eeprom(struct rt2x00_dev *rt2x00dev)
{
	int retval;

	if (rt2800pci_efuse_detect(rt2x00dev))
		retval = rt2800pci_read_eeprom_efuse(rt2x00dev);
	else
		retval = rt2800pci_read_eeprom_pci(rt2x00dev);

	return retval;
}

static const struct ieee80211_ops rt2800pci_mac80211_ops = {
	.tx			= rt2x00mac_tx,
	.start			= rt2x00mac_start,
	.stop			= rt2x00mac_stop,
	.add_interface		= rt2x00mac_add_interface,
	.remove_interface	= rt2x00mac_remove_interface,
	.config			= rt2x00mac_config,
	.configure_filter	= rt2x00mac_configure_filter,
	.set_key		= rt2x00mac_set_key,
	.sw_scan_start		= rt2x00mac_sw_scan_start,
	.sw_scan_complete	= rt2x00mac_sw_scan_complete,
	.get_stats		= rt2x00mac_get_stats,
	.get_key_seq		= rt2800_get_key_seq,
	.set_rts_threshold	= rt2800_set_rts_threshold,
	.sta_add		= rt2800_sta_add,
	.sta_remove		= rt2800_sta_remove,
	.bss_info_changed	= rt2x00mac_bss_info_changed,
	.conf_tx		= rt2800_conf_tx,
	.get_tsf		= rt2800_get_tsf,
	.rfkill_poll		= rt2x00mac_rfkill_poll,
	.ampdu_action		= rt2800_ampdu_action,
	.flush			= rt2x00mac_flush,
	.get_survey		= rt2800_get_survey,
	.get_ringparam		= rt2x00mac_get_ringparam,
	.tx_frames_pending	= rt2x00mac_tx_frames_pending,
};

static const struct rt2800_ops rt2800pci_rt2800_ops = {
	.register_read		= rt2x00mmio_register_read,
	.register_read_lock	= rt2x00mmio_register_read, /* same for PCI */
	.register_write		= rt2x00mmio_register_write,
	.register_write_lock	= rt2x00mmio_register_write, /* same for PCI */
	.register_multiread	= rt2x00mmio_register_multiread,
	.register_multiwrite	= rt2x00mmio_register_multiwrite,
	.regbusy_read		= rt2x00mmio_regbusy_read,
	.read_eeprom		= rt2800pci_read_eeprom,
	.hwcrypt_disabled	= rt2800pci_hwcrypt_disabled,
	.drv_write_firmware	= rt2800pci_write_firmware,
	.drv_init_registers	= rt2800mmio_init_registers,
	.drv_get_txwi		= rt2800mmio_get_txwi,
};

static const struct rt2x00lib_ops rt2800pci_rt2x00_ops = {
	.irq_handler		= rt2800mmio_interrupt,
	.txstatus_tasklet	= rt2800mmio_txstatus_tasklet,
	.pretbtt_tasklet	= rt2800mmio_pretbtt_tasklet,
	.tbtt_tasklet		= rt2800mmio_tbtt_tasklet,
	.rxdone_tasklet		= rt2800mmio_rxdone_tasklet,
	.autowake_tasklet	= rt2800mmio_autowake_tasklet,
	.probe_hw		= rt2800_probe_hw,
	.get_firmware_name	= rt2800pci_get_firmware_name,
	.check_firmware		= rt2800_check_firmware,
	.load_firmware		= rt2800_load_firmware,
	.initialize		= rt2x00mmio_initialize,
	.uninitialize		= rt2x00mmio_uninitialize,
	.get_entry_state	= rt2800mmio_get_entry_state,
	.clear_entry		= rt2800mmio_clear_entry,
	.set_device_state	= rt2800pci_set_device_state,
	.rfkill_poll		= rt2800_rfkill_poll,
	.link_stats		= rt2800_link_stats,
	.reset_tuner		= rt2800_reset_tuner,
	.link_tuner		= rt2800_link_tuner,
	.gain_calibration	= rt2800_gain_calibration,
	.vco_calibration	= rt2800_vco_calibration,
	.start_queue		= rt2800mmio_start_queue,
	.kick_queue		= rt2800mmio_kick_queue,
	.stop_queue		= rt2800mmio_stop_queue,
	.flush_queue		= rt2x00mmio_flush_queue,
	.write_tx_desc		= rt2800mmio_write_tx_desc,
	.write_tx_data		= rt2800_write_tx_data,
	.write_beacon		= rt2800_write_beacon,
	.clear_beacon		= rt2800_clear_beacon,
	.fill_rxdone		= rt2800mmio_fill_rxdone,
	.config_shared_key	= rt2800_config_shared_key,
	.config_pairwise_key	= rt2800_config_pairwise_key,
	.config_filter		= rt2800_config_filter,
	.config_intf		= rt2800_config_intf,
	.config_erp		= rt2800_config_erp,
	.config_ant		= rt2800_config_ant,
	.config			= rt2800_config,
};

static const struct rt2x00_ops rt2800pci_ops = {
	.name			= KBUILD_MODNAME,
	.drv_data_size		= sizeof(struct rt2800_drv_data),
	.max_ap_intf		= 8,
	.eeprom_size		= EEPROM_SIZE,
	.rf_size		= RF_SIZE,
	.tx_queues		= NUM_TX_QUEUES,
	.queue_init		= rt2800mmio_queue_init,
	.lib			= &rt2800pci_rt2x00_ops,
	.drv			= &rt2800pci_rt2800_ops,
	.hw			= &rt2800pci_mac80211_ops,
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	.debugfs		= &rt2800_rt2x00debug,
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */
};

/*
 * RT2800pci module information.
 */
static const struct pci_device_id rt2800pci_device_table[] = {
	{ PCI_DEVICE(0x1814, 0x0601) },
	{ PCI_DEVICE(0x1814, 0x0681) },
	{ PCI_DEVICE(0x1814, 0x0701) },
	{ PCI_DEVICE(0x1814, 0x0781) },
	{ PCI_DEVICE(0x1814, 0x3090) },
	{ PCI_DEVICE(0x1814, 0x3091) },
	{ PCI_DEVICE(0x1814, 0x3092) },
	{ PCI_DEVICE(0x1432, 0x7708) },
	{ PCI_DEVICE(0x1432, 0x7727) },
	{ PCI_DEVICE(0x1432, 0x7728) },
	{ PCI_DEVICE(0x1432, 0x7738) },
	{ PCI_DEVICE(0x1432, 0x7748) },
	{ PCI_DEVICE(0x1432, 0x7758) },
	{ PCI_DEVICE(0x1432, 0x7768) },
	{ PCI_DEVICE(0x1462, 0x891a) },
	{ PCI_DEVICE(0x1a3b, 0x1059) },
#ifdef CONFIG_RT2800PCI_RT3290
	{ PCI_DEVICE(0x1814, 0x3290) },
#endif
#ifdef CONFIG_RT2800PCI_RT33XX
	{ PCI_DEVICE(0x1814, 0x3390) },
#endif
#ifdef CONFIG_RT2800PCI_RT35XX
	{ PCI_DEVICE(0x1432, 0x7711) },
	{ PCI_DEVICE(0x1432, 0x7722) },
	{ PCI_DEVICE(0x1814, 0x3060) },
	{ PCI_DEVICE(0x1814, 0x3062) },
	{ PCI_DEVICE(0x1814, 0x3562) },
	{ PCI_DEVICE(0x1814, 0x3592) },
	{ PCI_DEVICE(0x1814, 0x3593) },
	{ PCI_DEVICE(0x1814, 0x359f) },
#endif
#ifdef CONFIG_RT2800PCI_RT53XX
	{ PCI_DEVICE(0x1814, 0x5360) },
	{ PCI_DEVICE(0x1814, 0x5362) },
	{ PCI_DEVICE(0x1814, 0x5390) },
	{ PCI_DEVICE(0x1814, 0x5392) },
	{ PCI_DEVICE(0x1814, 0x539a) },
	{ PCI_DEVICE(0x1814, 0x539b) },
	{ PCI_DEVICE(0x1814, 0x539f) },
#endif
	{ 0, }
};

MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("Ralink RT2800 PCI & PCMCIA Wireless LAN driver.");
MODULE_SUPPORTED_DEVICE("Ralink RT2860 PCI & PCMCIA chipset based cards");
MODULE_FIRMWARE(FIRMWARE_RT2860);
MODULE_DEVICE_TABLE(pci, rt2800pci_device_table);
MODULE_LICENSE("GPL");

static int rt2800pci_probe(struct pci_dev *pci_dev,
			   const struct pci_device_id *id)
{
	return rt2x00pci_probe(pci_dev, &rt2800pci_ops);
}

static struct pci_driver rt2800pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= rt2800pci_device_table,
	.probe		= rt2800pci_probe,
	.remove		= rt2x00pci_remove,
	.suspend	= rt2x00pci_suspend,
	.resume		= rt2x00pci_resume,
};

module_pci_driver(rt2800pci_driver);

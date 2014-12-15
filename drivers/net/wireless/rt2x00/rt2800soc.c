/*	Copyright (C) 2009 - 2010 Ivo van Doorn <IvDoorn@gmail.com>
 *	Copyright (C) 2009 Alban Browaeys <prahal@yahoo.com>
 *	Copyright (C) 2009 Felix Fietkau <nbd@openwrt.org>
 *	Copyright (C) 2009 Luis Correia <luis.f.correia@gmail.com>
 *	Copyright (C) 2009 Mattias Nissler <mattias.nissler@gmx.de>
 *	Copyright (C) 2009 Mark Asselstine <asselsm@gmail.com>
 *	Copyright (C) 2009 Xose Vazquez Perez <xose.vazquez@gmail.com>
 *	Copyright (C) 2009 Bart Zolnierkiewicz <bzolnier@gmail.com>
 *	<http://rt2x00.serialmonkey.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*	Module: rt2800soc
 *	Abstract: rt2800 WiSoC specific routines.
 */

#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "rt2x00.h"
#include "rt2x00mmio.h"
#include "rt2x00soc.h"
#include "rt2800.h"
#include "rt2800lib.h"
#include "rt2800mmio.h"

/* Allow hardware encryption to be disabled. */
static bool modparam_nohwcrypt;
module_param_named(nohwcrypt, modparam_nohwcrypt, bool, S_IRUGO);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption.");

static bool rt2800soc_hwcrypt_disabled(struct rt2x00_dev *rt2x00dev)
{
	return modparam_nohwcrypt;
}

static void rt2800soc_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	rt2800_disable_radio(rt2x00dev);
	rt2x00mmio_register_write(rt2x00dev, PWR_PIN_CFG, 0);
	rt2x00mmio_register_write(rt2x00dev, TX_PIN_CFG, 0);
}

static int rt2800soc_set_device_state(struct rt2x00_dev *rt2x00dev,
				      enum dev_state state)
{
	int retval = 0;

	switch (state) {
	case STATE_RADIO_ON:
		retval = rt2800mmio_enable_radio(rt2x00dev);
		break;

	case STATE_RADIO_OFF:
		rt2800soc_disable_radio(rt2x00dev);
		break;

	case STATE_RADIO_IRQ_ON:
	case STATE_RADIO_IRQ_OFF:
		rt2800mmio_toggle_irq(rt2x00dev, state);
		break;

	case STATE_DEEP_SLEEP:
	case STATE_SLEEP:
	case STATE_STANDBY:
	case STATE_AWAKE:
		/* These states are not supported, but don't report an error */
		retval = 0;
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

static int rt2800soc_read_eeprom(struct rt2x00_dev *rt2x00dev)
{
	void __iomem *base_addr = ioremap(0x1F040000, EEPROM_SIZE);

	if (!base_addr)
		return -ENOMEM;

	memcpy_fromio(rt2x00dev->eeprom, base_addr, EEPROM_SIZE);

	iounmap(base_addr);
	return 0;
}

/* Firmware functions */
static char *rt2800soc_get_firmware_name(struct rt2x00_dev *rt2x00dev)
{
	WARN_ON_ONCE(1);
	return NULL;
}

static int rt2800soc_load_firmware(struct rt2x00_dev *rt2x00dev,
				   const u8 *data, const size_t len)
{
	WARN_ON_ONCE(1);
	return 0;
}

static int rt2800soc_check_firmware(struct rt2x00_dev *rt2x00dev,
				    const u8 *data, const size_t len)
{
	WARN_ON_ONCE(1);
	return 0;
}

static int rt2800soc_write_firmware(struct rt2x00_dev *rt2x00dev,
				    const u8 *data, const size_t len)
{
	WARN_ON_ONCE(1);
	return 0;
}

static const struct ieee80211_ops rt2800soc_mac80211_ops = {
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
	.get_tkip_seq		= rt2800_get_tkip_seq,
	.set_rts_threshold	= rt2800_set_rts_threshold,
	.sta_add		= rt2x00mac_sta_add,
	.sta_remove		= rt2x00mac_sta_remove,
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

static const struct rt2800_ops rt2800soc_rt2800_ops = {
	.register_read		= rt2x00mmio_register_read,
	.register_read_lock	= rt2x00mmio_register_read, /* same for SoCs */
	.register_write		= rt2x00mmio_register_write,
	.register_write_lock	= rt2x00mmio_register_write, /* same for SoCs */
	.register_multiread	= rt2x00mmio_register_multiread,
	.register_multiwrite	= rt2x00mmio_register_multiwrite,
	.regbusy_read		= rt2x00mmio_regbusy_read,
	.read_eeprom		= rt2800soc_read_eeprom,
	.hwcrypt_disabled	= rt2800soc_hwcrypt_disabled,
	.drv_write_firmware	= rt2800soc_write_firmware,
	.drv_init_registers	= rt2800mmio_init_registers,
	.drv_get_txwi		= rt2800mmio_get_txwi,
};

static const struct rt2x00lib_ops rt2800soc_rt2x00_ops = {
	.irq_handler		= rt2800mmio_interrupt,
	.txstatus_tasklet	= rt2800mmio_txstatus_tasklet,
	.pretbtt_tasklet	= rt2800mmio_pretbtt_tasklet,
	.tbtt_tasklet		= rt2800mmio_tbtt_tasklet,
	.rxdone_tasklet		= rt2800mmio_rxdone_tasklet,
	.autowake_tasklet	= rt2800mmio_autowake_tasklet,
	.probe_hw		= rt2800_probe_hw,
	.get_firmware_name	= rt2800soc_get_firmware_name,
	.check_firmware		= rt2800soc_check_firmware,
	.load_firmware		= rt2800soc_load_firmware,
	.initialize		= rt2x00mmio_initialize,
	.uninitialize		= rt2x00mmio_uninitialize,
	.get_entry_state	= rt2800mmio_get_entry_state,
	.clear_entry		= rt2800mmio_clear_entry,
	.set_device_state	= rt2800soc_set_device_state,
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
	.sta_add		= rt2800_sta_add,
	.sta_remove		= rt2800_sta_remove,
};

static const struct rt2x00_ops rt2800soc_ops = {
	.name			= KBUILD_MODNAME,
	.drv_data_size		= sizeof(struct rt2800_drv_data),
	.max_ap_intf		= 8,
	.eeprom_size		= EEPROM_SIZE,
	.rf_size		= RF_SIZE,
	.tx_queues		= NUM_TX_QUEUES,
	.queue_init		= rt2800mmio_queue_init,
	.lib			= &rt2800soc_rt2x00_ops,
	.drv			= &rt2800soc_rt2800_ops,
	.hw			= &rt2800soc_mac80211_ops,
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	.debugfs		= &rt2800_rt2x00debug,
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */
};

static int rt2800soc_probe(struct platform_device *pdev)
{
	return rt2x00soc_probe(pdev, &rt2800soc_ops);
}

static struct platform_driver rt2800soc_driver = {
	.driver		= {
		.name		= "rt2800_wmac",
		.mod_name	= KBUILD_MODNAME,
	},
	.probe		= rt2800soc_probe,
	.remove		= rt2x00soc_remove,
	.suspend	= rt2x00soc_suspend,
	.resume		= rt2x00soc_resume,
};

module_platform_driver(rt2800soc_driver);

MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("Ralink WiSoC Wireless LAN driver.");
MODULE_LICENSE("GPL");

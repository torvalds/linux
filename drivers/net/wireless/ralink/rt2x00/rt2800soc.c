// SPDX-License-Identifier: GPL-2.0-or-later
/*	Copyright (C) 2009 - 2010 Ivo van Doorn <IvDoorn@gmail.com>
 *	Copyright (C) 2009 Alban Browaeys <prahal@yahoo.com>
 *	Copyright (C) 2009 Felix Fietkau <nbd@openwrt.org>
 *	Copyright (C) 2009 Luis Correia <luis.f.correia@gmail.com>
 *	Copyright (C) 2009 Mattias Nissler <mattias.nissler@gmx.de>
 *	Copyright (C) 2009 Mark Asselstine <asselsm@gmail.com>
 *	Copyright (C) 2009 Xose Vazquez Perez <xose.vazquez@gmail.com>
 *	Copyright (C) 2009 Bart Zolnierkiewicz <bzolnier@gmail.com>
 *	<http://rt2x00.serialmonkey.com>
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
#include "rt2800.h"
#include "rt2800lib.h"
#include "rt2800mmio.h"

/* Allow hardware encryption to be disabled. */
static bool modparam_nohwcrypt;
module_param_named(nohwcrypt, modparam_nohwcrypt, bool, 0444);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption.");

static bool rt2800soc_hwcrypt_disabled(struct rt2x00_dev *rt2x00dev)
{
	return modparam_nohwcrypt;
}

static void rt2800soc_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2800_disable_radio(rt2x00dev);
	rt2x00mmio_register_write(rt2x00dev, PWR_PIN_CFG, 0);

	reg = 0;
	if (rt2x00_rt(rt2x00dev, RT3883))
		rt2x00_set_field32(&reg, TX_PIN_CFG_RFTR_EN, 1);

	rt2x00mmio_register_write(rt2x00dev, TX_PIN_CFG, reg);
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

static void rt2x00soc_free_reg(struct rt2x00_dev *rt2x00dev)
{
	kfree(rt2x00dev->rf);
	rt2x00dev->rf = NULL;

	kfree(rt2x00dev->eeprom);
	rt2x00dev->eeprom = NULL;

	iounmap(rt2x00dev->csr.base);
}

static int rt2x00soc_alloc_reg(struct rt2x00_dev *rt2x00dev)
{
	struct platform_device *pdev = to_platform_device(rt2x00dev->dev);
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	rt2x00dev->csr.base = ioremap(res->start, resource_size(res));
	if (!rt2x00dev->csr.base)
		return -ENOMEM;

	rt2x00dev->eeprom = kzalloc(rt2x00dev->ops->eeprom_size, GFP_KERNEL);
	if (!rt2x00dev->eeprom)
		goto exit;

	rt2x00dev->rf = kzalloc(rt2x00dev->ops->rf_size, GFP_KERNEL);
	if (!rt2x00dev->rf)
		goto exit;

	return 0;

exit:
	rt2x00_probe_err("Failed to allocate registers\n");
	rt2x00soc_free_reg(rt2x00dev);

	return -ENOMEM;
}

static int rt2x00soc_probe(struct platform_device *pdev, const struct rt2x00_ops *ops)
{
	struct ieee80211_hw *hw;
	struct rt2x00_dev *rt2x00dev;
	int retval;

	hw = ieee80211_alloc_hw(sizeof(struct rt2x00_dev), ops->hw);
	if (!hw) {
		rt2x00_probe_err("Failed to allocate hardware\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, hw);

	rt2x00dev = hw->priv;
	rt2x00dev->dev = &pdev->dev;
	rt2x00dev->ops = ops;
	rt2x00dev->hw = hw;
	rt2x00dev->irq = platform_get_irq(pdev, 0);
	rt2x00dev->name = pdev->dev.driver->name;

	rt2x00dev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(rt2x00dev->clk))
		rt2x00dev->clk = NULL;

	rt2x00_set_chip_intf(rt2x00dev, RT2X00_CHIP_INTF_SOC);

	retval = rt2x00soc_alloc_reg(rt2x00dev);
	if (retval)
		goto exit_free_device;

	retval = rt2x00lib_probe_dev(rt2x00dev);
	if (retval)
		goto exit_free_reg;

	return 0;

exit_free_reg:
	rt2x00soc_free_reg(rt2x00dev);

exit_free_device:
	ieee80211_free_hw(hw);

	return retval;
}

static void rt2x00soc_remove(struct platform_device *pdev)
{
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	/*
	 * Free all allocated data.
	 */
	rt2x00lib_remove_dev(rt2x00dev);
	rt2x00soc_free_reg(rt2x00dev);
	ieee80211_free_hw(hw);
}

#ifdef CONFIG_PM
static int rt2x00soc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	return rt2x00lib_suspend(rt2x00dev);
}

static int rt2x00soc_resume(struct platform_device *pdev)
{
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	return rt2x00lib_resume(rt2x00dev);
}
#endif /* CONFIG_PM */

static const struct ieee80211_ops rt2800soc_mac80211_ops = {
	.add_chanctx = ieee80211_emulate_add_chanctx,
	.remove_chanctx = ieee80211_emulate_remove_chanctx,
	.change_chanctx = ieee80211_emulate_change_chanctx,
	.switch_vif_chanctx = ieee80211_emulate_switch_vif_chanctx,
	.tx			= rt2x00mac_tx,
	.wake_tx_queue		= ieee80211_handle_wake_tx_queue,
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
	.reconfig_complete	= rt2x00mac_reconfig_complete,
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
	.drv_get_dma_done	= rt2800mmio_get_dma_done,
};

static const struct rt2x00lib_ops rt2800soc_rt2x00_ops = {
	.irq_handler		= rt2800mmio_interrupt,
	.txstatus_tasklet	= rt2800mmio_txstatus_tasklet,
	.pretbtt_tasklet	= rt2800mmio_pretbtt_tasklet,
	.tbtt_tasklet		= rt2800mmio_tbtt_tasklet,
	.rxdone_tasklet		= rt2800mmio_rxdone_tasklet,
	.autowake_tasklet	= rt2800mmio_autowake_tasklet,
	.probe_hw		= rt2800mmio_probe_hw,
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
	.watchdog		= rt2800_watchdog,
	.start_queue		= rt2800mmio_start_queue,
	.kick_queue		= rt2800mmio_kick_queue,
	.stop_queue		= rt2800mmio_stop_queue,
	.flush_queue		= rt2800mmio_flush_queue,
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
	.pre_reset_hw		= rt2800_pre_reset_hw,
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

static const struct of_device_id rt2880_wmac_match[] = {
	{ .compatible = "ralink,rt2880-wifi" },
	{},
};
MODULE_DEVICE_TABLE(of, rt2880_wmac_match);

static struct platform_driver rt2800soc_driver = {
	.driver		= {
		.name		= "rt2800_wmac",
		.of_match_table = rt2880_wmac_match,
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

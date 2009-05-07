/*
 * Intel Wireless WiMAX Connection 2400m
 * Linux driver model glue for the SDIO device, reset & fw upload
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation <linux-wimax@intel.com>
 * Dirk Brandewie <dirk.j.brandewie@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 * Yanir Lubetkin <yanirx.lubetkin@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * See i2400m-sdio.h for a general description of this driver.
 *
 * This file implements driver model glue, and hook ups for the
 * generic driver to implement the bus-specific functions (device
 * communication setup/tear down, firmware upload and resetting).
 *
 * ROADMAP
 *
 * i2400m_probe()
 *   alloc_netdev()
 *     i2400ms_netdev_setup()
 *       i2400ms_init()
 *       i2400m_netdev_setup()
 *   i2400ms_enable_function()
 *   i2400m_setup()
 *
 * i2400m_remove()
 *     i2400m_release()
 *     free_netdev(net_dev)
 *
 * i2400ms_bus_reset()            Called by i2400m->bus_reset
 *   __i2400ms_reset()
 *     __i2400ms_send_barker()
 *
 * i2400ms_bus_dev_start()        Called by i2400m_dev_start() [who is
 *   i2400ms_tx_setup()           called by i2400m_setup()]
 *   i2400ms_rx_setup()
 *
 * i2400ms_bus_dev_stop()         Called by i2400m_dev_stop() [who is
 *   i2400ms_rx_release()         is called by i2400m_release()]
 *   i2400ms_tx_release()
 *
 */

#include <linux/debugfs.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include "i2400m-sdio.h"
#include <linux/wimax/i2400m.h>

#define D_SUBMODULE main
#include "sdio-debug-levels.h"

/* IOE WiMAX function timeout in seconds */
static int ioe_timeout = 2;
module_param(ioe_timeout, int, 0);

/* Our firmware file name list */
static const char *i2400ms_bus_fw_names[] = {
#define I2400MS_FW_FILE_NAME "i2400m-fw-sdio-1.3.sbcf"
	I2400MS_FW_FILE_NAME,
	NULL
};


static const struct i2400m_poke_table i2400ms_pokes[] = {
	I2400M_FW_POKE(0x6BE260, 0x00000088),
	I2400M_FW_POKE(0x080550, 0x00000005),
	I2400M_FW_POKE(0xAE0000, 0x00000000),
	I2400M_FW_POKE(0x000000, 0x00000000), /* MUST be 0 terminated or bad
					       * things will happen */
};

/*
 * Enable the SDIO function
 *
 * Tries to enable the SDIO function; might fail if it is still not
 * ready (in some hardware, the SDIO WiMAX function is only enabled
 * when we ask it to explicitly doing). Tries until a timeout is
 * reached.
 *
 * The reverse of this is...sdio_disable_function()
 *
 * Returns: 0 if the SDIO function was enabled, < 0 errno code on
 *     error (-ENODEV when it was unable to enable the function).
 */
static
int i2400ms_enable_function(struct sdio_func *func)
{
	u64 timeout;
	int err;
	struct device *dev = &func->dev;

	d_fnstart(3, dev, "(func %p)\n", func);
	/* Setup timeout (FIXME: This needs to read the CIS table to
	 * get a real timeout) and then wait for the device to signal
	 * it is ready */
	timeout = get_jiffies_64() + ioe_timeout * HZ;
	err = -ENODEV;
	while (err != 0 && time_before64(get_jiffies_64(), timeout)) {
		sdio_claim_host(func);
		err = sdio_enable_func(func);
		if (0 == err) {
			sdio_release_host(func);
			d_printf(2, dev, "SDIO function enabled\n");
			goto function_enabled;
		}
		d_printf(2, dev, "SDIO function failed to enable: %d\n", err);
		sdio_disable_func(func);
		sdio_release_host(func);
		msleep(I2400MS_INIT_SLEEP_INTERVAL);
	}
	/* If timed out, device is not there yet -- get -ENODEV so
	 * the device driver core will retry later on. */
	if (err == -ETIME) {
		dev_err(dev, "Can't enable WiMAX function; "
			" has the function been enabled?\n");
		err = -ENODEV;
	}
function_enabled:
	d_fnend(3, dev, "(func %p) = %d\n", func, err);
	return err;
}


/*
 * Setup driver resources needed to communicate with the device
 *
 * The fw needs some time to settle, and it was just uploaded,
 * so give it a break first. I'd prefer to just wait for the device to
 * send something, but seems the poking we do to enable SDIO stuff
 * interferes with it, so just give it a break before starting...
 */
static
int i2400ms_bus_dev_start(struct i2400m *i2400m)
{
	int result;
	struct i2400ms *i2400ms = container_of(i2400m, struct i2400ms, i2400m);
	struct sdio_func *func = i2400ms->func;
	struct device *dev = &func->dev;

	d_fnstart(3, dev, "(i2400m %p)\n", i2400m);
	msleep(200);
	result = i2400ms_tx_setup(i2400ms);
	if (result < 0)
		goto error_tx_setup;
	d_fnend(3, dev, "(i2400m %p) = %d\n", i2400m, result);
	return result;

error_tx_setup:
	i2400ms_tx_release(i2400ms);
	d_fnend(3, dev, "(i2400m %p) = void\n", i2400m);
	return result;
}


static
void i2400ms_bus_dev_stop(struct i2400m *i2400m)
{
	struct i2400ms *i2400ms = container_of(i2400m, struct i2400ms, i2400m);
	struct sdio_func *func = i2400ms->func;
	struct device *dev = &func->dev;

	d_fnstart(3, dev, "(i2400m %p)\n", i2400m);
	i2400ms_tx_release(i2400ms);
	d_fnend(3, dev, "(i2400m %p) = void\n", i2400m);
}


/*
 * Sends a barker buffer to the device
 *
 * This helper will allocate a kmalloced buffer and use it to transmit
 * (then free it). Reason for this is that the SDIO host controller
 * expects alignment (unknown exactly which) which the stack won't
 * really provide and certain arches/host-controller combinations
 * cannot use stack/vmalloc/text areas for DMA transfers.
 */
static
int __i2400ms_send_barker(struct i2400ms *i2400ms,
			  const __le32 *barker, size_t barker_size)
{
	int  ret;
	struct sdio_func *func = i2400ms->func;
	struct device *dev = &func->dev;
	void *buffer;

	ret = -ENOMEM;
	buffer = kmalloc(I2400MS_BLK_SIZE, GFP_KERNEL);
	if (buffer == NULL)
		goto error_kzalloc;

	memcpy(buffer, barker, barker_size);
	sdio_claim_host(func);
	ret = sdio_memcpy_toio(func, 0, buffer, I2400MS_BLK_SIZE);
	sdio_release_host(func);

	if (ret < 0)
		d_printf(0, dev, "E: barker error: %d\n", ret);

	kfree(buffer);
error_kzalloc:
	return ret;
}


/*
 * Reset a device at different levels (warm, cold or bus)
 *
 * @i2400ms: device descriptor
 * @reset_type: soft, warm or bus reset (I2400M_RT_WARM/SOFT/BUS)
 *
 * FIXME: not tested -- need to confirm expected effects
 *
 * Warm and cold resets get an SDIO reset if they fail (unimplemented)
 *
 * Warm reset:
 *
 * The device will be fully reset internally, but won't be
 * disconnected from the USB bus (so no reenumeration will
 * happen). Firmware upload will be neccessary.
 *
 * The device will send a reboot barker in the notification endpoint
 * that will trigger the driver to reinitialize the state
 * automatically from notif.c:i2400m_notification_grok() into
 * i2400m_dev_bootstrap_delayed().
 *
 * Cold and bus (USB) reset:
 *
 * The device will be fully reset internally, disconnected from the
 * USB bus an a reenumeration will happen. Firmware upload will be
 * neccessary. Thus, we don't do any locking or struct
 * reinitialization, as we are going to be fully disconnected and
 * reenumerated.
 *
 * Note we need to return -ENODEV if a warm reset was requested and we
 * had to resort to a bus reset. See i2400m_op_reset(), wimax_reset()
 * and wimax_dev->op_reset.
 *
 * WARNING: no driver state saved/fixed
 */
static
int i2400ms_bus_reset(struct i2400m *i2400m, enum i2400m_reset_type rt)
{
	int result = 0;
	struct i2400ms *i2400ms =
		container_of(i2400m, struct i2400ms, i2400m);
	struct device *dev = i2400m_dev(i2400m);
	static const __le32 i2400m_WARM_BOOT_BARKER[4] = {
		cpu_to_le32(I2400M_WARM_RESET_BARKER),
		cpu_to_le32(I2400M_WARM_RESET_BARKER),
		cpu_to_le32(I2400M_WARM_RESET_BARKER),
		cpu_to_le32(I2400M_WARM_RESET_BARKER),
	};
	static const __le32 i2400m_COLD_BOOT_BARKER[4] = {
		cpu_to_le32(I2400M_COLD_RESET_BARKER),
		cpu_to_le32(I2400M_COLD_RESET_BARKER),
		cpu_to_le32(I2400M_COLD_RESET_BARKER),
		cpu_to_le32(I2400M_COLD_RESET_BARKER),
	};

	if (rt == I2400M_RT_WARM)
		result = __i2400ms_send_barker(i2400ms, i2400m_WARM_BOOT_BARKER,
					       sizeof(i2400m_WARM_BOOT_BARKER));
	else if (rt == I2400M_RT_COLD)
		result = __i2400ms_send_barker(i2400ms, i2400m_COLD_BOOT_BARKER,
					       sizeof(i2400m_COLD_BOOT_BARKER));
	else if (rt == I2400M_RT_BUS) {
do_bus_reset:
		/* call netif_tx_disable() before sending IOE disable,
		 * so that all the tx from network layer are stopped
		 * while IOE is being reset. Make sure it is called
		 * only after register_netdev() was issued.
		 */
		if (i2400m->wimax_dev.net_dev->reg_state == NETREG_REGISTERED)
			netif_tx_disable(i2400m->wimax_dev.net_dev);

		i2400ms_rx_release(i2400ms);
		sdio_claim_host(i2400ms->func);
		sdio_disable_func(i2400ms->func);
		sdio_release_host(i2400ms->func);

		/* Wait for the device to settle */
		msleep(40);

		result = i2400ms_enable_function(i2400ms->func);
		if (result >= 0)
			i2400ms_rx_setup(i2400ms);
	} else
		BUG();
	if (result < 0 && rt != I2400M_RT_BUS) {
		dev_err(dev, "%s reset failed (%d); trying SDIO reset\n",
			rt == I2400M_RT_WARM ? "warm" : "cold", result);
		rt = I2400M_RT_BUS;
		goto do_bus_reset;
	}
	return result;
}


static
void i2400ms_netdev_setup(struct net_device *net_dev)
{
	struct i2400m *i2400m = net_dev_to_i2400m(net_dev);
	struct i2400ms *i2400ms = container_of(i2400m, struct i2400ms, i2400m);
	i2400ms_init(i2400ms);
	i2400m_netdev_setup(net_dev);
}


/*
 * Debug levels control; see debug.h
 */
struct d_level D_LEVEL[] = {
	D_SUBMODULE_DEFINE(main),
	D_SUBMODULE_DEFINE(tx),
	D_SUBMODULE_DEFINE(rx),
	D_SUBMODULE_DEFINE(fw),
};
size_t D_LEVEL_SIZE = ARRAY_SIZE(D_LEVEL);


#define __debugfs_register(prefix, name, parent)			\
do {									\
	result = d_level_register_debugfs(prefix, name, parent);	\
	if (result < 0)							\
		goto error;						\
} while (0)


static
int i2400ms_debugfs_add(struct i2400ms *i2400ms)
{
	int result;
	struct dentry *dentry = i2400ms->i2400m.wimax_dev.debugfs_dentry;

	dentry = debugfs_create_dir("i2400m-usb", dentry);
	result = PTR_ERR(dentry);
	if (IS_ERR(dentry)) {
		if (result == -ENODEV)
			result = 0;	/* No debugfs support */
		goto error;
	}
	i2400ms->debugfs_dentry = dentry;
	__debugfs_register("dl_", main, dentry);
	__debugfs_register("dl_", tx, dentry);
	__debugfs_register("dl_", rx, dentry);
	__debugfs_register("dl_", fw, dentry);

	return 0;

error:
	debugfs_remove_recursive(i2400ms->debugfs_dentry);
	return result;
}


/*
 * Probe a i2400m interface and register it
 *
 * @func:    SDIO function
 * @id:      SDIO device ID
 * @returns: 0 if ok, < 0 errno code on error.
 *
 * Alloc a net device, initialize the bus-specific details and then
 * calls the bus-generic initialization routine. That will register
 * the wimax and netdev devices, upload the firmware [using
 * _bus_bm_*()], call _bus_dev_start() to finalize the setup of the
 * communication with the device and then will start to talk to it to
 * finnish setting it up.
 *
 * Initialization is tricky; some instances of the hw are packed with
 * others in a way that requires a third driver that enables the WiMAX
 * function. In those cases, we can't enable the SDIO function and
 * we'll return with -ENODEV. When the driver that enables the WiMAX
 * function does its thing, it has to do a bus_rescan_devices() on the
 * SDIO bus so this driver is called again to enumerate the WiMAX
 * function.
 */
static
int i2400ms_probe(struct sdio_func *func,
		  const struct sdio_device_id *id)
{
	int result;
	struct net_device *net_dev;
	struct device *dev = &func->dev;
	struct i2400m *i2400m;
	struct i2400ms *i2400ms;

	/* Allocate instance [calls i2400m_netdev_setup() on it]. */
	result = -ENOMEM;
	net_dev = alloc_netdev(sizeof(*i2400ms), "wmx%d",
			       i2400ms_netdev_setup);
	if (net_dev == NULL) {
		dev_err(dev, "no memory for network device instance\n");
		goto error_alloc_netdev;
	}
	SET_NETDEV_DEV(net_dev, dev);
	i2400m = net_dev_to_i2400m(net_dev);
	i2400ms = container_of(i2400m, struct i2400ms, i2400m);
	i2400m->wimax_dev.net_dev = net_dev;
	i2400ms->func = func;
	sdio_set_drvdata(func, i2400ms);

	i2400m->bus_tx_block_size = I2400MS_BLK_SIZE;
	i2400m->bus_pl_size_max = I2400MS_PL_SIZE_MAX;
	i2400m->bus_dev_start = i2400ms_bus_dev_start;
	i2400m->bus_dev_stop = i2400ms_bus_dev_stop;
	i2400m->bus_tx_kick = i2400ms_bus_tx_kick;
	i2400m->bus_reset = i2400ms_bus_reset;
	/* The iwmc3200-wimax sometimes requires the driver to try
	 * hard when we paint it into a corner. */
	i2400m->bus_bm_retries = I3200_BOOT_RETRIES;
	i2400m->bus_bm_cmd_send = i2400ms_bus_bm_cmd_send;
	i2400m->bus_bm_wait_for_ack = i2400ms_bus_bm_wait_for_ack;
	i2400m->bus_fw_names = i2400ms_bus_fw_names;
	i2400m->bus_bm_mac_addr_impaired = 1;
	i2400m->bus_bm_pokes_table = &i2400ms_pokes[0];

	sdio_claim_host(func);
	result = sdio_set_block_size(func, I2400MS_BLK_SIZE);
	sdio_release_host(func);
	if (result < 0) {
		dev_err(dev, "Failed to set block size: %d\n", result);
		goto error_set_blk_size;
	}

	result = i2400ms_enable_function(i2400ms->func);
	if (result < 0) {
		dev_err(dev, "Cannot enable SDIO function: %d\n", result);
		goto error_func_enable;
	}

	result = i2400ms_rx_setup(i2400ms);
	if (result < 0)
		goto error_rx_setup;

	result = i2400m_setup(i2400m, I2400M_BRI_NO_REBOOT);
	if (result < 0) {
		dev_err(dev, "cannot setup device: %d\n", result);
		goto error_setup;
	}

	result = i2400ms_debugfs_add(i2400ms);
	if (result < 0) {
		dev_err(dev, "cannot create SDIO debugfs: %d\n",
			result);
		goto error_debugfs_add;
	}
	return 0;

error_debugfs_add:
	i2400m_release(i2400m);
error_setup:
	i2400ms_rx_release(i2400ms);
error_rx_setup:
	sdio_claim_host(func);
	sdio_disable_func(func);
	sdio_release_host(func);
error_func_enable:
error_set_blk_size:
	sdio_set_drvdata(func, NULL);
	free_netdev(net_dev);
error_alloc_netdev:
	return result;
}


static
void i2400ms_remove(struct sdio_func *func)
{
	struct device *dev = &func->dev;
	struct i2400ms *i2400ms = sdio_get_drvdata(func);
	struct i2400m *i2400m = &i2400ms->i2400m;
	struct net_device *net_dev = i2400m->wimax_dev.net_dev;

	d_fnstart(3, dev, "SDIO func %p\n", func);
	debugfs_remove_recursive(i2400ms->debugfs_dentry);
	i2400ms_rx_release(i2400ms);
	i2400m_release(i2400m);
	sdio_set_drvdata(func, NULL);
	sdio_claim_host(func);
	sdio_disable_func(func);
	sdio_release_host(func);
	free_netdev(net_dev);
	d_fnend(3, dev, "SDIO func %p\n", func);
}

enum {
	I2400MS_INTEL_VID = 0x89,
};

static
const struct sdio_device_id i2400ms_sdio_ids[] = {
	/* Intel: i2400m WiMAX over SDIO */
	{ SDIO_DEVICE(I2400MS_INTEL_VID, 0x1402) },
	{ }, 			/* end: all zeroes */
};
MODULE_DEVICE_TABLE(sdio, i2400ms_sdio_ids);


static
struct sdio_driver i2400m_sdio_driver = {
	.name		= KBUILD_MODNAME,
	.probe		= i2400ms_probe,
	.remove		= i2400ms_remove,
	.id_table	= i2400ms_sdio_ids,
};


static
int __init i2400ms_driver_init(void)
{
	return sdio_register_driver(&i2400m_sdio_driver);
}
module_init(i2400ms_driver_init);


static
void __exit i2400ms_driver_exit(void)
{
	flush_scheduled_work();	/* for the stuff we schedule */
	sdio_unregister_driver(&i2400m_sdio_driver);
}
module_exit(i2400ms_driver_exit);


MODULE_AUTHOR("Intel Corporation <linux-wimax@intel.com>");
MODULE_DESCRIPTION("Intel 2400M WiMAX networking for SDIO");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(I2400MS_FW_FILE_NAME);

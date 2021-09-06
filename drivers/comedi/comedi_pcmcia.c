// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi_pcmcia.c
 * Comedi PCMCIA driver specific functions.
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 */

#include <linux/module.h>
#include <linux/kernel.h>

#include "comedi_pcmcia.h"

/**
 * comedi_to_pcmcia_dev() - Return PCMCIA device attached to COMEDI device
 * @dev: COMEDI device.
 *
 * Assuming @dev->hw_dev is non-%NULL, it is assumed to be pointing to a
 * a &struct device embedded in a &struct pcmcia_device.
 *
 * Return: Attached PCMCIA device if @dev->hw_dev is non-%NULL.
 * Return %NULL if @dev->hw_dev is %NULL.
 */
struct pcmcia_device *comedi_to_pcmcia_dev(struct comedi_device *dev)
{
	return dev->hw_dev ? to_pcmcia_dev(dev->hw_dev) : NULL;
}
EXPORT_SYMBOL_GPL(comedi_to_pcmcia_dev);

static int comedi_pcmcia_conf_check(struct pcmcia_device *link,
				    void *priv_data)
{
	if (link->config_index == 0)
		return -EINVAL;

	return pcmcia_request_io(link);
}

/**
 * comedi_pcmcia_enable() - Request the regions and enable the PCMCIA device
 * @dev: COMEDI device.
 * @conf_check: Optional callback to check each configuration option of the
 *	PCMCIA device and request I/O regions.
 *
 * Assuming @dev->hw_dev is non-%NULL, it is assumed to be pointing to a a
 * &struct device embedded in a &struct pcmcia_device.  The comedi PCMCIA
 * driver needs to set the 'config_flags' member in the &struct pcmcia_device,
 * as appropriate for that driver, before calling this function in order to
 * allow pcmcia_loop_config() to do its internal autoconfiguration.
 *
 * If @conf_check is %NULL it is set to a default function.  If is
 * passed to pcmcia_loop_config() and should return %0 if the configuration
 * is valid and I/O regions requested successfully, otherwise it should return
 * a negative error value.  The default function returns -%EINVAL if the
 * 'config_index' member is %0, otherwise it calls pcmcia_request_io() and
 * returns the result.
 *
 * If the above configuration check passes, pcmcia_enable_device() is called
 * to set up and activate the PCMCIA device.
 *
 * If this function returns an error, comedi_pcmcia_disable() should be called
 * to release requested resources.
 *
 * Return:
 *	0 on success,
 *	-%ENODEV id @dev->hw_dev is %NULL,
 *	a negative error number from pcmcia_loop_config() if it fails,
 *	or a negative error number from pcmcia_enable_device() if it fails.
 */
int comedi_pcmcia_enable(struct comedi_device *dev,
			 int (*conf_check)(struct pcmcia_device *p_dev,
					   void *priv_data))
{
	struct pcmcia_device *link = comedi_to_pcmcia_dev(dev);
	int ret;

	if (!link)
		return -ENODEV;

	if (!conf_check)
		conf_check = comedi_pcmcia_conf_check;

	ret = pcmcia_loop_config(link, conf_check, NULL);
	if (ret)
		return ret;

	return pcmcia_enable_device(link);
}
EXPORT_SYMBOL_GPL(comedi_pcmcia_enable);

/**
 * comedi_pcmcia_disable() - Disable the PCMCIA device and release the regions
 * @dev: COMEDI device.
 *
 * Assuming @dev->hw_dev is non-%NULL, it is assumed to be pointing to a
 * a &struct device embedded in a &struct pcmcia_device.  Call
 * pcmcia_disable_device() to disable and clean up the PCMCIA device.
 */
void comedi_pcmcia_disable(struct comedi_device *dev)
{
	struct pcmcia_device *link = comedi_to_pcmcia_dev(dev);

	if (link)
		pcmcia_disable_device(link);
}
EXPORT_SYMBOL_GPL(comedi_pcmcia_disable);

/**
 * comedi_pcmcia_auto_config() - Configure/probe a PCMCIA COMEDI device
 * @link: PCMCIA device.
 * @driver: Registered COMEDI driver.
 *
 * Typically called from the pcmcia_driver (*probe) function.  Auto-configure
 * a COMEDI device, using a pointer to the &struct device embedded in *@link
 * as the hardware device.  The @driver's "auto_attach" handler may call
 * comedi_to_pcmcia_dev() on the passed in COMEDI device to recover @link.
 *
 * Return: The result of calling comedi_auto_config() (0 on success, or a
 * negative error number on failure).
 */
int comedi_pcmcia_auto_config(struct pcmcia_device *link,
			      struct comedi_driver *driver)
{
	return comedi_auto_config(&link->dev, driver, 0);
}
EXPORT_SYMBOL_GPL(comedi_pcmcia_auto_config);

/**
 * comedi_pcmcia_auto_unconfig() - Unconfigure/remove a PCMCIA COMEDI device
 * @link: PCMCIA device.
 *
 * Typically called from the pcmcia_driver (*remove) function.
 * Auto-unconfigure a COMEDI device attached to this PCMCIA device, using a
 * pointer to the &struct device embedded in *@link as the hardware device.
 * The COMEDI driver's "detach" handler will be called during unconfiguration
 * of the COMEDI device.
 *
 * Note that the COMEDI device may have already been unconfigured using the
 * %COMEDI_DEVCONFIG ioctl, in which case this attempt to unconfigure it
 * again should be ignored.
 */
void comedi_pcmcia_auto_unconfig(struct pcmcia_device *link)
{
	comedi_auto_unconfig(&link->dev);
}
EXPORT_SYMBOL_GPL(comedi_pcmcia_auto_unconfig);

/**
 * comedi_pcmcia_driver_register() - Register a PCMCIA COMEDI driver
 * @comedi_driver: COMEDI driver to be registered.
 * @pcmcia_driver: PCMCIA driver to be registered.
 *
 * This function is used for the module_init() of PCMCIA COMEDI driver modules
 * to register the COMEDI driver and the PCMCIA driver.  Do not call it
 * directly, use the module_comedi_pcmcia_driver() helper macro instead.
 *
 * Return: 0 on success, or a negative error number on failure.
 */
int comedi_pcmcia_driver_register(struct comedi_driver *comedi_driver,
				  struct pcmcia_driver *pcmcia_driver)
{
	int ret;

	ret = comedi_driver_register(comedi_driver);
	if (ret < 0)
		return ret;

	ret = pcmcia_register_driver(pcmcia_driver);
	if (ret < 0) {
		comedi_driver_unregister(comedi_driver);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(comedi_pcmcia_driver_register);

/**
 * comedi_pcmcia_driver_unregister() - Unregister a PCMCIA COMEDI driver
 * @comedi_driver: COMEDI driver to be registered.
 * @pcmcia_driver: PCMCIA driver to be registered.
 *
 * This function is called from the module_exit() of PCMCIA COMEDI driver
 * modules to unregister the PCMCIA driver and the COMEDI driver.  Do not call
 * it directly, use the module_comedi_pcmcia_driver() helper macro instead.
 */
void comedi_pcmcia_driver_unregister(struct comedi_driver *comedi_driver,
				     struct pcmcia_driver *pcmcia_driver)
{
	pcmcia_unregister_driver(pcmcia_driver);
	comedi_driver_unregister(comedi_driver);
}
EXPORT_SYMBOL_GPL(comedi_pcmcia_driver_unregister);

static int __init comedi_pcmcia_init(void)
{
	return 0;
}
module_init(comedi_pcmcia_init);

static void __exit comedi_pcmcia_exit(void)
{
}
module_exit(comedi_pcmcia_exit);

MODULE_AUTHOR("https://www.comedi.org");
MODULE_DESCRIPTION("Comedi PCMCIA interface module");
MODULE_LICENSE("GPL");

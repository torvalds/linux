/*
 * comedi_pcmcia.c
 * Comedi PCMCIA driver specific functions.
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#include "comedidev.h"

/**
 * comedi_to_pcmcia_dev() - comedi_device pointer to pcmcia_device pointer.
 * @dev: comedi_device struct
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
 * comedi_pcmcia_enable() - Request the regions and enable the PCMCIA device.
 * @dev: comedi_device struct
 * @conf_check: optional callback to check the pcmcia_device configuration
 *
 * The comedi PCMCIA driver needs to set the link->config_flags, as
 * appropriate for that driver, before calling this function in order
 * to allow pcmcia_loop_config() to do its internal autoconfiguration.
 */
int comedi_pcmcia_enable(struct comedi_device *dev,
			 int (*conf_check)(struct pcmcia_device *, void *))
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
 * comedi_pcmcia_disable() - Disable the PCMCIA device and release the regions.
 * @dev: comedi_device struct
 */
void comedi_pcmcia_disable(struct comedi_device *dev)
{
	struct pcmcia_device *link = comedi_to_pcmcia_dev(dev);

	if (link)
		pcmcia_disable_device(link);
}
EXPORT_SYMBOL_GPL(comedi_pcmcia_disable);

/**
 * comedi_pcmcia_auto_config() - Configure/probe a comedi PCMCIA driver.
 * @link: pcmcia_device struct
 * @driver: comedi_driver struct
 *
 * Typically called from the pcmcia_driver (*probe) function.
 */
int comedi_pcmcia_auto_config(struct pcmcia_device *link,
			      struct comedi_driver *driver)
{
	return comedi_auto_config(&link->dev, driver, 0);
}
EXPORT_SYMBOL_GPL(comedi_pcmcia_auto_config);

/**
 * comedi_pcmcia_auto_unconfig() - Unconfigure/remove a comedi PCMCIA driver.
 * @link: pcmcia_device struct
 *
 * Typically called from the pcmcia_driver (*remove) function.
 */
void comedi_pcmcia_auto_unconfig(struct pcmcia_device *link)
{
	comedi_auto_unconfig(&link->dev);
}
EXPORT_SYMBOL_GPL(comedi_pcmcia_auto_unconfig);

/**
 * comedi_pcmcia_driver_register() - Register a comedi PCMCIA driver.
 * @comedi_driver: comedi_driver struct
 * @pcmcia_driver: pcmcia_driver struct
 *
 * This function is used for the module_init() of comedi USB drivers.
 * Do not call it directly, use the module_comedi_pcmcia_driver() helper
 * macro instead.
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
 * comedi_pcmcia_driver_unregister() - Unregister a comedi PCMCIA driver.
 * @comedi_driver: comedi_driver struct
 * @pcmcia_driver: pcmcia_driver struct
 *
 * This function is used for the module_exit() of comedi PCMCIA drivers.
 * Do not call it directly, use the module_comedi_pcmcia_driver() helper
 * macro instead.
 */
void comedi_pcmcia_driver_unregister(struct comedi_driver *comedi_driver,
				     struct pcmcia_driver *pcmcia_driver)
{
	pcmcia_unregister_driver(pcmcia_driver);
	comedi_driver_unregister(comedi_driver);
}
EXPORT_SYMBOL_GPL(comedi_pcmcia_driver_unregister);

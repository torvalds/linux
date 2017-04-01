/*
 * comedi_pcmcia.h
 * header file for Comedi PCMCIA drivers
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

#ifndef _COMEDI_PCMCIA_H
#define _COMEDI_PCMCIA_H

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#include "comedidev.h"

struct pcmcia_device *comedi_to_pcmcia_dev(struct comedi_device *dev);

int comedi_pcmcia_enable(struct comedi_device *dev,
			 int (*conf_check)(struct pcmcia_device *p_dev,
					   void *priv_data));
void comedi_pcmcia_disable(struct comedi_device *dev);

int comedi_pcmcia_auto_config(struct pcmcia_device *link,
			      struct comedi_driver *driver);
void comedi_pcmcia_auto_unconfig(struct pcmcia_device *link);

int comedi_pcmcia_driver_register(struct comedi_driver *comedi_driver,
				  struct pcmcia_driver *pcmcia_driver);
void comedi_pcmcia_driver_unregister(struct comedi_driver *comedi_driver,
				     struct pcmcia_driver *pcmcia_driver);

/**
 * module_comedi_pcmcia_driver() - Helper macro for registering a comedi
 * PCMCIA driver
 * @__comedi_driver: comedi_driver struct
 * @__pcmcia_driver: pcmcia_driver struct
 *
 * Helper macro for comedi PCMCIA drivers which do not do anything special
 * in module init/exit. This eliminates a lot of boilerplate. Each
 * module may only use this macro once, and calling it replaces
 * module_init() and module_exit()
 */
#define module_comedi_pcmcia_driver(__comedi_driver, __pcmcia_driver) \
	module_driver(__comedi_driver, comedi_pcmcia_driver_register, \
			comedi_pcmcia_driver_unregister, &(__pcmcia_driver))

#endif /* _COMEDI_PCMCIA_H */

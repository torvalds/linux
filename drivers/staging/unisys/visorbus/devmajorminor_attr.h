/* devmajorminor_attr.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __DEVMAJORMINOR_ATTR_H__
#define __DEVMAJORMINOR_ATTR_H__

#include "visorbus.h"		/* just to get visor_device declaration */
#include "timskmod.h"

int register_devmajorminor_attributes(struct visor_device *dev);
void unregister_devmajorminor_attributes(struct visor_device *dev);
int devmajorminor_create_file(struct visor_device *dev, const char *nam,
			      int major, int minor);
void devmajorminor_remove_file(struct visor_device *dev, int slot);
void devmajorminor_remove_all_files(struct visor_device *dev);

#endif

/* businst_attr.h
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

#ifndef __BUSINST_H__
#define __BUSINST_H__

#include "visorbus_private.h"	/* just to get visorbus_devdata declaration */
#include "timskmod.h"

struct businst_attribute {
	struct attribute attr;
	 ssize_t (*show)(struct visorbus_devdata*, char *buf);
	 ssize_t (*store)(struct visorbus_devdata*, const char *buf,
			  size_t count);
};

ssize_t businst_attr_show(struct kobject *kobj,
			  struct attribute *attr, char *buf);
ssize_t businst_attr_store(struct kobject *kobj, struct attribute *attr,
			   const char *buf, size_t count);
int businst_create_file(struct visorbus_devdata *bus,
			struct businst_attribute *attr);
void businst_remove_file(struct visorbus_devdata *bus,
			 struct businst_attribute *attr);

#endif

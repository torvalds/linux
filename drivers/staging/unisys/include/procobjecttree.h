/* procobjecttree.h
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

/** @file *********************************************************************
 *
 *  This describes the interfaces necessary for creating a tree of types,
 *  objects, and properties in /proc.
 *
 ******************************************************************************
 */

#ifndef __PROCOBJECTTREE_H__
#define __PROCOBJECTTREE_H__

#include "uniklog.h"
#include "timskmod.h"

/* These are opaque structures to users.
 * Fields are declared only in the implementation .c files.
 */
typedef struct MYPROCOBJECT_Tag MYPROCOBJECT;
typedef struct MYPROCTYPE_Tag   MYPROCTYPE;

MYPROCOBJECT *visor_proc_CreateObject(MYPROCTYPE *type, const char *name,
				      void *context);
void          visor_proc_DestroyObject(MYPROCOBJECT *obj);
MYPROCTYPE   *visor_proc_CreateType(struct proc_dir_entry *procRootDir,
				    const char **name,
				    const char **propertyNames,
				    void (*show_property)(struct seq_file *,
							  void *, int));
void          visor_proc_DestroyType(MYPROCTYPE *type);

#endif

/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* \file ssi_sysfs.h
 * ARM CryptoCell sysfs APIs
 */

#ifndef __SSI_SYSFS_H__
#define __SSI_SYSFS_H__

#include <asm/timex.h>

/* forward declaration */
struct ssi_drvdata;

int ssi_sysfs_init(struct kobject *sys_dev_obj, struct ssi_drvdata *drvdata);
void ssi_sysfs_fini(void);

#endif /*__SSI_SYSFS_H__*/

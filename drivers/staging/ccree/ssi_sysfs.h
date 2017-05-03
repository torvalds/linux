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
   ARM CryptoCell sysfs APIs
 */

#ifndef __SSI_SYSFS_H__
#define __SSI_SYSFS_H__

#include <asm/timex.h>

/* forward declaration */
struct ssi_drvdata;

enum stat_phase {
	STAT_PHASE_0 = 0,
	STAT_PHASE_1,
	STAT_PHASE_2,
	STAT_PHASE_3,
	STAT_PHASE_4,
	STAT_PHASE_5,
	STAT_PHASE_6,
	MAX_STAT_PHASES,
};
enum stat_op {
	STAT_OP_TYPE_NULL = 0,
	STAT_OP_TYPE_ENCODE,
	STAT_OP_TYPE_DECODE,
	STAT_OP_TYPE_SETKEY,
	STAT_OP_TYPE_GENERIC,
	MAX_STAT_OP_TYPES,
};

int ssi_sysfs_init(struct kobject *sys_dev_obj, struct ssi_drvdata *drvdata);
void ssi_sysfs_fini(void);
void update_host_stat(unsigned int op_type, unsigned int phase, cycles_t result);
void update_cc_stat(unsigned int op_type, unsigned int phase, unsigned int elapsed_cycles);
void display_all_stat_db(void);

#endif /*__SSI_SYSFS_H__*/

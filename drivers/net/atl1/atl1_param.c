/*
 * Copyright(c) 2005 - 2006 Attansic Corporation. All rights reserved.
 * Copyright(c) 2006 Chris Snook <csnook@redhat.com>
 * Copyright(c) 2006 Jay Cliburn <jcliburn@gmail.com>
 *
 * Derived from Intel e1000 driver
 * Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/moduleparam.h>
#include "atl1.h"

/*
 * This is the only thing that needs to be changed to adjust the
 * maximum number of ports that the driver can manage.
 */
#define ATL1_MAX_NIC 4

#define OPTION_UNSET    -1
#define OPTION_DISABLED 0
#define OPTION_ENABLED  1

#define ATL1_PARAM_INIT { [0 ... ATL1_MAX_NIC] = OPTION_UNSET }

/*
 * Interrupt Moderate Timer in units of 2 us
 *
 * Valid Range: 10-65535
 *
 * Default Value: 100 (200us)
 */
static int __devinitdata int_mod_timer[ATL1_MAX_NIC+1] = ATL1_PARAM_INIT;
static int num_int_mod_timer = 0;
module_param_array_named(int_mod_timer, int_mod_timer, int, &num_int_mod_timer, 0);
MODULE_PARM_DESC(int_mod_timer, "Interrupt moderator timer");

/*
 * flash_vendor
 *
 * Valid Range: 0-2
 *
 * 0 - Atmel
 * 1 - SST
 * 2 - ST
 *
 * Default Value: 0
 */
static int __devinitdata flash_vendor[ATL1_MAX_NIC+1] = ATL1_PARAM_INIT;
static int num_flash_vendor = 0;
module_param_array_named(flash_vendor, flash_vendor, int, &num_flash_vendor, 0);
MODULE_PARM_DESC(flash_vendor, "SPI flash vendor");

#define DEFAULT_INT_MOD_CNT	100	/* 200us */
#define MAX_INT_MOD_CNT		65000
#define MIN_INT_MOD_CNT		50

#define FLASH_VENDOR_DEFAULT	0
#define FLASH_VENDOR_MIN	0
#define FLASH_VENDOR_MAX	2

struct atl1_option {
	enum { enable_option, range_option, list_option } type;
	char *name;
	char *err;
	int def;
	union {
		struct {	/* range_option info */
			int min;
			int max;
		} r;
		struct {	/* list_option info */
			int nr;
			struct atl1_opt_list {
				int i;
				char *str;
			} *p;
		} l;
	} arg;
};

static int __devinit atl1_validate_option(int *value, struct atl1_option *opt)
{
	if (*value == OPTION_UNSET) {
		*value = opt->def;
		return 0;
	}

	switch (opt->type) {
	case enable_option:
		switch (*value) {
		case OPTION_ENABLED:
			printk(KERN_INFO "%s: %s Enabled\n", atl1_driver_name,
				opt->name);
			return 0;
		case OPTION_DISABLED:
			printk(KERN_INFO "%s: %s Disabled\n", atl1_driver_name,
				opt->name);
			return 0;
		}
		break;
	case range_option:
		if (*value >= opt->arg.r.min && *value <= opt->arg.r.max) {
			printk(KERN_INFO "%s: %s set to %i\n",
				atl1_driver_name, opt->name, *value);
			return 0;
		}
		break;
	case list_option:{
			int i;
			struct atl1_opt_list *ent;

			for (i = 0; i < opt->arg.l.nr; i++) {
				ent = &opt->arg.l.p[i];
				if (*value == ent->i) {
					if (ent->str[0] != '\0')
						printk(KERN_INFO "%s: %s\n",
						       atl1_driver_name, ent->str);
					return 0;
				}
			}
		}
		break;

	default:
		break;
	}

	printk(KERN_INFO "%s: invalid %s specified (%i) %s\n",
	       atl1_driver_name, opt->name, *value, opt->err);
	*value = opt->def;
	return -1;
}

/*
 * atl1_check_options - Range Checking for Command Line Parameters
 * @adapter: board private structure
 *
 * This routine checks all command line parameters for valid user
 * input.  If an invalid value is given, or if no user specified
 * value exists, a default value is used.  The final value is stored
 * in a variable in the adapter structure.
 */
void __devinit atl1_check_options(struct atl1_adapter *adapter)
{
	int bd = adapter->bd_number;
	if (bd >= ATL1_MAX_NIC) {
		printk(KERN_NOTICE "%s: warning: no configuration for board #%i\n",
			atl1_driver_name, bd);
		printk(KERN_NOTICE "%s: using defaults for all values\n",
			atl1_driver_name);
	}
	{			/* Interrupt Moderate Timer */
		struct atl1_option opt = {
			.type = range_option,
			.name = "Interrupt Moderator Timer",
			.err = "using default of "
				__MODULE_STRING(DEFAULT_INT_MOD_CNT),
			.def = DEFAULT_INT_MOD_CNT,
			.arg = {.r =
				{.min = MIN_INT_MOD_CNT,.max = MAX_INT_MOD_CNT}}
		};
		int val;
		if (num_int_mod_timer > bd) {
			val = int_mod_timer[bd];
			atl1_validate_option(&val, &opt);
			adapter->imt = (u16) val;
		} else
			adapter->imt = (u16) (opt.def);
	}

	{			/* Flash Vendor */
		struct atl1_option opt = {
			.type = range_option,
			.name = "SPI Flash Vendor",
			.err = "using default of "
				__MODULE_STRING(FLASH_VENDOR_DEFAULT),
			.def = DEFAULT_INT_MOD_CNT,
			.arg = {.r =
				{.min = FLASH_VENDOR_MIN,.max =
				 FLASH_VENDOR_MAX}}
		};
		int val;
		if (num_flash_vendor > bd) {
			val = flash_vendor[bd];
			atl1_validate_option(&val, &opt);
			adapter->hw.flash_vendor = (u8) val;
		} else
			adapter->hw.flash_vendor = (u8) (opt.def);
	}
}

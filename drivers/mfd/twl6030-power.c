/*
 * Handling for Resource Mapping for TWL6030 Family of chips
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *	Nishanth Menon
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.

 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/pm.h>
#include <linux/i2c/twl.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/string.h>

#include <asm/mach-types.h>

#define VREG_GRP		0
#define MSK_TRANSITION_APP_SHIFT	0x5

static u8 dev_on_group;

/**
 * struct twl6030_resource_map - describe the resource mapping for TWL6030
 * @name:	name of the resource
 * @res_id:	resource ID
 * @base_addr: base address for TWL6030
 * @base_addr: type of the Resources Assignment register for TWL6032
 * base_addr = 0 for PREQx_RES_ASS_A register
 * base_addr = 1 for PREQx_RES_ASS_B register
 * base_addr = 2 for PREQx_RES_ASS_C register
 * @group: which device group can control this resource?
 * @mask: unused for TWL6030
 * @mask: bit mask of the resource in PREQx_RES_ASS_x registers for TWL6032
 */
struct twl6030_resource_map {
	char *name;
	u8 res_id;
	u8 base_addr;
	u8 group;
	u8 mask;
};

#define TWL6030_RES_DATA(ID, NAME, BASE_ADDR, GROUP) \
	{.res_id = ID, .name = NAME, .base_addr = BASE_ADDR,\
	.group = GROUP, .mask = 0,}

#define TWL6032_RES_DATA(ID, NAME, BASE_ADDR, GROUP, MASK) \
	{.res_id = ID, .name = NAME, .base_addr = BASE_ADDR,\
	.group = GROUP, .mask = MASK,}

/* list of all s/w modifiable resources in TWL6030 */
static __initdata struct twl6030_resource_map twl6030_res_map[] = {
	TWL6030_RES_DATA(RES_V1V29, "V1V29", 0x40, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_V1V8, "V1V8", 0x46, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_V2V1, "V2V1", 0x4c, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_VDD1, "CORE1", 0x52, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_VDD2, "CORE2", 0x58, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_VDD3, "CORE3", 0x5e, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_VMEM, "VMEM", 0x64, DEV_GRP_P1),
	/* VANA cannot be modified */
	TWL6030_RES_DATA(RES_VUAX1, "VUAX1", 0x84, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_VAUX2, "VAUX2", 0x88, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_VAUX3, "VAUX3", 0x8c, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_VCXIO, "VCXIO", 0x90, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_VDAC, "VDAC", 0x94, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_VMMC1, "VMMC", 0x98, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_VPP, "VPP", 0x9c, DEV_GRP_P1),
	/* VRTC cannot be modified */
	TWL6030_RES_DATA(RES_VUSBCP, "VUSB", 0xa0, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_VSIM, "VSIM", 0xa4, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_REGEN, "REGEN1", 0xad, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_REGEN2, "REGEN2", 0xb0, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_SYSEN, "SYSEN", 0xb3, DEV_GRP_P1),
	/* NRES_PWRON cannot be modified */
	/* 32KCLKAO cannot be modified */
	TWL6030_RES_DATA(RES_32KCLKG, "32KCLKG", 0xbc, DEV_GRP_P1),
	TWL6030_RES_DATA(RES_32KCLKAUDIO, "32KCLKAUDIO", 0xbf, DEV_GRP_P1),
	/* BIAS cannot be modified */
	/* VBATMIN_HI cannot be modified */
	/* RC6MHZ cannot be modified */
	/* TEMP cannot be modified */
};

/* list of all s/w modifiable resources in TWL6032 */
static __initdata struct twl6030_resource_map twl6032_res_map[] = {
	/* PREQx_RES_ASS_A register resources */
	TWL6032_RES_DATA(RES_LDOUSB, "VUSB", 0, DEV_GRP_P1, BIT(5)),
	TWL6032_RES_DATA(RES_SMPS5, "SMPS5", 0, DEV_GRP_P1, BIT(4)),
	TWL6032_RES_DATA(RES_SMPS5, "SMPS4", 0, DEV_GRP_P1, BIT(3)),
	TWL6032_RES_DATA(RES_SMPS5, "SMPS3", 0, DEV_GRP_P1, BIT(2)),
	TWL6032_RES_DATA(RES_SMPS5, "SMPS2", 0, DEV_GRP_P1, BIT(1)),
	TWL6032_RES_DATA(RES_SMPS5, "SMPS1", 0, DEV_GRP_P1, BIT(0)),
	/* PREQx_RES_ASS_B register resources */
	TWL6032_RES_DATA(RES_LDOLN, "LDOLN", 1, DEV_GRP_P1, BIT(7)),
	TWL6032_RES_DATA(RES_LDO7, "LDO7", 1, DEV_GRP_P1, BIT(6)),
	TWL6032_RES_DATA(RES_LDO6, "LDO6", 1, DEV_GRP_P1, BIT(5)),
	TWL6032_RES_DATA(RES_LDO5, "LDO5", 1, DEV_GRP_P1, BIT(4)),
	TWL6032_RES_DATA(RES_LDO4, "LDO4", 1, DEV_GRP_P1, BIT(3)),
	TWL6032_RES_DATA(RES_LDO3, "LDO3", 1, DEV_GRP_P1, BIT(2)),
	TWL6032_RES_DATA(RES_LDO2, "LDO2", 1, DEV_GRP_P1, BIT(1)),
	TWL6032_RES_DATA(RES_LDO1, "LDO1", 1, DEV_GRP_P1, BIT(0)),
	/* PREQx_RES_ASS_C register resources */
	TWL6032_RES_DATA(RES_VSYSMIN_HI, "VSYSMIN_HI", 2, DEV_GRP_P1, BIT(5)),
	TWL6032_RES_DATA(RES_32KCLKG, "32KCLKG", 2, DEV_GRP_P1, BIT(4)),
	TWL6032_RES_DATA(RES_32KCLKAUDIO, "32KCLKAUDIO", 2, DEV_GRP_P1, BIT(3)),
	TWL6032_RES_DATA(RES_SYSEN, "SYSEN", 2, DEV_GRP_P1, BIT(2)),
	TWL6032_RES_DATA(RES_REGEN2, "REGEN2", 2, DEV_GRP_P1, BIT(1)),
	TWL6032_RES_DATA(RES_REGEN, "REGEN1", 2, DEV_GRP_P1, BIT(0)),
};

static struct twl4030_system_config twl6030_sys_config[] = {
	{.name = "DEV_ON", .group =  DEV_GRP_P1,},
};

/* Actual power groups that TWL understands */
#define P3_GRP_6030	BIT(2)		/* secondary processor, modem, etc */
#define P2_GRP_6030	BIT(1)		/* "peripherals" */
#define P1_GRP_6030	BIT(0)		/* CPU/Linux */

static __init void twl6030_process_system_config(void)
{
	u8 grp;
	int r;
	bool i = false;

	struct twl4030_system_config *sys_config;
	sys_config = twl6030_sys_config;

	while (sys_config && sys_config->name) {
		if (!strcmp(sys_config->name, "DEV_ON")) {
			dev_on_group = sys_config->group;
			i = true;
			break;
		}
		sys_config++;
	}
	if (!i)
		pr_err("%s: Couldn't find DEV_ON resource configuration!"
			" MOD & CON group would be kept active.\n", __func__);

	if (dev_on_group) {
		r = twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &grp,
				TWL6030_PHOENIX_DEV_ON);
		if (r) {
			pr_err("%s: Error(%d) reading  {addr=0x%02x}",
				__func__, r, TWL6030_PHOENIX_DEV_ON);
			/*
			 * On error resetting to 0, so that all the process
			 * groups are kept active.
			 */
			dev_on_group = 0;
		} else {
			/*
			 * Unmapped processor groups are disabled by writing
			 * 1 to corresponding group in DEV_ON.
			 */
			grp |= (dev_on_group & DEV_GRP_P1) ? 0 : P1_GRP_6030;
			grp |= (dev_on_group & DEV_GRP_P2) ? 0 : P2_GRP_6030;
			grp |= (dev_on_group & DEV_GRP_P3) ? 0 : P3_GRP_6030;
			dev_on_group = grp;
		}

		/*
		 *  unmask PREQ transition Executes ACT2SLP and SLP2ACT sleep
		 *   sequence
		 */
		r = twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &grp,
				TWL6030_PM_MASTER_MSK_TRANSITION);
		if (r) {
			pr_err("%s: Error (%d) reading"
				" TWL6030_MSK_TRANSITION\n", __func__, r);
			return;
		}

		grp &= (dev_on_group << MSK_TRANSITION_APP_SHIFT);

		r = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, grp,
					TWL6030_PM_MASTER_MSK_TRANSITION);
		if (r)
			pr_err("%s: Error (%d) writing to"
				" TWL6030_MSK_TRANSITION\n", __func__, r);
	}
}

#define DEV_GRP_P1_OFFSET	1
#define DEV_GRP_P2_OFFSET	4
#define DEV_GRP_P3_OFFSET	7

static __init void twl6030_program_map(unsigned long features)
{
	struct twl6030_resource_map *res;
	int r, i;

	if (features & TWL6032_SUBCLASS) {
		/**
		 * mask[0] = 0 for twl_i2c_write
		 * mask[1]-mask[3]: PREQ1_RES_ASS_A - PREQ1_RES_ASS_C
		 * mask[4]-mask[6]: PREQ2_RES_ASS_A - PREQ2_RES_ASS_C
		 * mask[7]-mask[9]: PREQ3_RES_ASS_A - PREQ3_RES_ASS_C
		 */
		u8 mask[10];

		res = twl6032_res_map;
		memset(&mask[0], 0, 10);

		for (i = 0; i < ARRAY_SIZE(twl6032_res_map); i++) {
			/* map back from generic device id to TWL6032 mask */
			mask[DEV_GRP_P1_OFFSET + res->base_addr] |= \
				(res->group & DEV_GRP_P1) ? res->mask : 0;
			mask[DEV_GRP_P2_OFFSET + res->base_addr] |= \
				(res->group & DEV_GRP_P2) ? res->mask : 0;
			mask[DEV_GRP_P3_OFFSET + res->base_addr] |= \
				(res->group & DEV_GRP_P3) ? res->mask : 0;
			res++;
		}

		r = twl_i2c_write(TWL6030_MODULE_ID0, &mask[0],
			TWL6032_PREQ1_RES_ASS_A, 9);

		if (r)
			pr_err("%s: Error(%d) programming TWL6032 PREQ "
				"Assignment Registers {start addr=0xd7}\n",
				__func__, r);
	} else {
		res = twl6030_res_map;
		for (i = 0; i < ARRAY_SIZE(twl6030_res_map); i++) {
			u8 grp = 0;

			/* map back from generic device id to TWL6030 ID */
			grp |= (res->group & DEV_GRP_P1) ? P1_GRP_6030 : 0;
			grp |= (res->group & DEV_GRP_P2) ? P2_GRP_6030 : 0;
			grp |= (res->group & DEV_GRP_P3) ? P3_GRP_6030 : 0;

			r = twl_i2c_write_u8(TWL6030_MODULE_ID0, res->group,
					     res->base_addr);
			if (r)
				pr_err("%s: Error(%d) programming map %s {"
					"addr=0x%02x},grp=0x%02X\n", __func__,
					r, res->name, res->base_addr,
					res->group);
			res++;
		}
	}
}

static __init void twl6030_update_system_map
			(struct twl4030_system_config *sys_list)
{
	int i;
	struct twl4030_system_config *sys_res;

	while (sys_list && sys_list->name)  {
		sys_res = twl6030_sys_config;
		for (i = 0; i < ARRAY_SIZE(twl6030_sys_config); i++) {
			if (!strcmp(sys_res->name, sys_list->name))
				sys_res->group = sys_list->group &
					(DEV_GRP_P1 | DEV_GRP_P2 | DEV_GRP_P3);
			sys_res++;
		}
		sys_list++;
	}
}

static __init void twl6030_update_map(struct twl4030_resconfig *res_list, \
					unsigned long features)
{
	int i, res_idx = 0;
	struct twl6030_resource_map *res;
	struct twl6030_resource_map *cur_twl6030_res = twl6030_res_map;
	int twl6030_res_cnt = ARRAY_SIZE(twl6030_res_map);

	if (features & TWL6032_SUBCLASS) {
		cur_twl6030_res = twl6032_res_map;
		twl6030_res_cnt = ARRAY_SIZE(twl6032_res_map);
	}

	while (res_list->resource != TWL4030_RESCONFIG_UNDEF) {
		res = cur_twl6030_res;
		for (i = 0; i < twl6030_res_cnt; i++) {
			if (res->res_id == res_list->resource) {
				res->group = res_list->devgroup &
				    (DEV_GRP_P1 | DEV_GRP_P2 | DEV_GRP_P3);
				break;
			}
			res++;
		}

		if (i == twl6030_res_cnt) {
			pr_err("%s: in platform_data resource index %d, cannot"
			       " find match for resource 0x%02x. NO Update!\n",
			       __func__, res_idx, res_list->resource);
		}
		res_list++;
		res_idx++;
	}
}


static int twl6030_power_notifier_cb(struct notifier_block *notifier,
					unsigned long pm_event,  void *unused)
{
	int r = 0;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		r = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, dev_on_group,
				TWL6030_PHOENIX_DEV_ON);
		if (r)
			pr_err("%s: Error(%d) programming {addr=0x%02x}",
				__func__, r, TWL6030_PHOENIX_DEV_ON);
		break;
	}

	return notifier_from_errno(r);
}

static struct notifier_block twl6030_power_pm_notifier = {
	.notifier_call = twl6030_power_notifier_cb,
};

/**
 * twl6030_power_init() - Update the power map to reflect connectivity of board
 * @power_data:	power resource map to update (OPTIONAL) - use this if a resource
 *		is used by other devices other than APP (DEV_GRP_P1)
 */
void __init twl6030_power_init(struct twl4030_power_data *power_data, \
					unsigned long features)
{
	int r;

	if (power_data && (!power_data->resource_config &&
					!power_data->sys_config)) {
		pr_err("%s: power data from platform without configuration!\n",
		       __func__);
		return;
	}

	if (power_data && power_data->resource_config)
		twl6030_update_map(power_data->resource_config, features);

	if (power_data && power_data->sys_config)
		twl6030_update_system_map(power_data->sys_config);

	twl6030_process_system_config();

	twl6030_program_map(features);

	r = register_pm_notifier(&twl6030_power_pm_notifier);
	if (r)
		pr_err("%s: twl6030 power registration failed!\n", __func__);

	return;
}

/*
 *
 * Handle TWL4030 Power initialization
 *
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2006 Texas Instruments, Inc
 *
 * Written by 	Kalle Jokiniemi
 *		Peter De Schrijver <peter.de-schrijver@nokia.com>
 * Several fixes by Amit Kucheria <amit.kucheria@verdurent.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/pm.h>
#include <linux/mfd/twl.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <asm/mach-types.h>

static u8 twl4030_start_script_address = 0x2b;

/* Register bits for P1, P2 and P3_SW_EVENTS */
#define PWR_STOPON_PRWON	BIT(6)
#define PWR_STOPON_SYSEN	BIT(5)
#define PWR_ENABLE_WARMRESET	BIT(4)
#define PWR_LVL_WAKEUP		BIT(3)
#define PWR_DEVACT		BIT(2)
#define PWR_DEVSLP		BIT(1)
#define PWR_DEVOFF		BIT(0)

/* Register bits for CFG_P1_TRANSITION (also for P2 and P3) */
#define STARTON_SWBUG		BIT(7)	/* Start on watchdog */
#define STARTON_VBUS		BIT(5)	/* Start on VBUS */
#define STARTON_VBAT		BIT(4)	/* Start on battery insert */
#define STARTON_RTC		BIT(3)	/* Start on RTC */
#define STARTON_USB		BIT(2)	/* Start on USB host */
#define STARTON_CHG		BIT(1)	/* Start on charger */
#define STARTON_PWON		BIT(0)	/* Start on PWRON button */

#define SEQ_OFFSYNC		(1 << 0)

#define PHY_TO_OFF_PM_MASTER(p)		(p - 0x36)
#define PHY_TO_OFF_PM_RECEIVER(p)	(p - 0x5b)

/* resource - hfclk */
#define R_HFCLKOUT_DEV_GRP 	PHY_TO_OFF_PM_RECEIVER(0xe6)

/* PM events */
#define R_P1_SW_EVENTS		PHY_TO_OFF_PM_MASTER(0x46)
#define R_P2_SW_EVENTS		PHY_TO_OFF_PM_MASTER(0x47)
#define R_P3_SW_EVENTS		PHY_TO_OFF_PM_MASTER(0x48)
#define R_CFG_P1_TRANSITION	PHY_TO_OFF_PM_MASTER(0x36)
#define R_CFG_P2_TRANSITION	PHY_TO_OFF_PM_MASTER(0x37)
#define R_CFG_P3_TRANSITION	PHY_TO_OFF_PM_MASTER(0x38)

#define END_OF_SCRIPT		0x3f

#define R_SEQ_ADD_A2S		PHY_TO_OFF_PM_MASTER(0x55)
#define R_SEQ_ADD_S2A12		PHY_TO_OFF_PM_MASTER(0x56)
#define	R_SEQ_ADD_S2A3		PHY_TO_OFF_PM_MASTER(0x57)
#define	R_SEQ_ADD_WARM		PHY_TO_OFF_PM_MASTER(0x58)
#define R_MEMORY_ADDRESS	PHY_TO_OFF_PM_MASTER(0x59)
#define R_MEMORY_DATA		PHY_TO_OFF_PM_MASTER(0x5a)

/* resource configuration registers
   <RESOURCE>_DEV_GRP   at address 'n+0'
   <RESOURCE>_TYPE      at address 'n+1'
   <RESOURCE>_REMAP     at address 'n+2'
   <RESOURCE>_DEDICATED at address 'n+3'
*/
#define DEV_GRP_OFFSET		0
#define TYPE_OFFSET		1
#define REMAP_OFFSET		2
#define DEDICATED_OFFSET	3

/* Bit positions in the registers */

/* <RESOURCE>_DEV_GRP */
#define DEV_GRP_SHIFT		5
#define DEV_GRP_MASK		(7 << DEV_GRP_SHIFT)

/* <RESOURCE>_TYPE */
#define TYPE_SHIFT		0
#define TYPE_MASK		(7 << TYPE_SHIFT)
#define TYPE2_SHIFT		3
#define TYPE2_MASK		(3 << TYPE2_SHIFT)

/* <RESOURCE>_REMAP */
#define SLEEP_STATE_SHIFT	0
#define SLEEP_STATE_MASK	(0xf << SLEEP_STATE_SHIFT)
#define OFF_STATE_SHIFT		4
#define OFF_STATE_MASK		(0xf << OFF_STATE_SHIFT)

static u8 res_config_addrs[] = {
	[RES_VAUX1]	= 0x17,
	[RES_VAUX2]	= 0x1b,
	[RES_VAUX3]	= 0x1f,
	[RES_VAUX4]	= 0x23,
	[RES_VMMC1]	= 0x27,
	[RES_VMMC2]	= 0x2b,
	[RES_VPLL1]	= 0x2f,
	[RES_VPLL2]	= 0x33,
	[RES_VSIM]	= 0x37,
	[RES_VDAC]	= 0x3b,
	[RES_VINTANA1]	= 0x3f,
	[RES_VINTANA2]	= 0x43,
	[RES_VINTDIG]	= 0x47,
	[RES_VIO]	= 0x4b,
	[RES_VDD1]	= 0x55,
	[RES_VDD2]	= 0x63,
	[RES_VUSB_1V5]	= 0x71,
	[RES_VUSB_1V8]	= 0x74,
	[RES_VUSB_3V1]	= 0x77,
	[RES_VUSBCP]	= 0x7a,
	[RES_REGEN]	= 0x7f,
	[RES_NRES_PWRON] = 0x82,
	[RES_CLKEN]	= 0x85,
	[RES_SYSEN]	= 0x88,
	[RES_HFCLKOUT]	= 0x8b,
	[RES_32KCLKOUT]	= 0x8e,
	[RES_RESET]	= 0x91,
	[RES_MAIN_REF]	= 0x94,
};

/*
 * Usable values for .remap_sleep and .remap_off
 * Based on table "5.3.3 Resource Operating modes"
 */
enum {
	TWL_REMAP_OFF = 0,
	TWL_REMAP_SLEEP = 8,
	TWL_REMAP_ACTIVE = 9,
};

/*
 * Macros to configure the PM register states for various resources.
 * Note that we can make MSG_SINGULAR etc private to this driver once
 * omap3 has been made DT only.
 */
#define TWL_DFLT_DELAY		2	/* typically 2 32 KiHz cycles */
#define TWL_DEV_GRP_P123	(DEV_GRP_P1 | DEV_GRP_P2 | DEV_GRP_P3)
#define TWL_RESOURCE_SET(res, state)					\
	{ MSG_SINGULAR(DEV_GRP_NULL, (res), (state)), TWL_DFLT_DELAY }
#define TWL_RESOURCE_ON(res)	TWL_RESOURCE_SET(res, RES_STATE_ACTIVE)
#define TWL_RESOURCE_OFF(res)	TWL_RESOURCE_SET(res, RES_STATE_OFF)
#define TWL_RESOURCE_RESET(res)	TWL_RESOURCE_SET(res, RES_STATE_WRST)
/*
 * It seems that type1 and type2 is just the resource init order
 * number for the type1 and type2 group.
 */
#define TWL_RESOURCE_SET_ACTIVE(res, state)			       	\
	{ MSG_SINGULAR(DEV_GRP_NULL, (res), RES_STATE_ACTIVE), (state) }
#define TWL_RESOURCE_GROUP_RESET(group, type1, type2)			\
	{ MSG_BROADCAST(DEV_GRP_NULL, (group), (type1), (type2),	\
		RES_STATE_WRST), TWL_DFLT_DELAY }
#define TWL_RESOURCE_GROUP_SLEEP(group, type, type2)			\
	{ MSG_BROADCAST(DEV_GRP_NULL, (group), (type), (type2),		\
		RES_STATE_SLEEP), TWL_DFLT_DELAY }
#define TWL_RESOURCE_GROUP_ACTIVE(group, type, type2)			\
	{ MSG_BROADCAST(DEV_GRP_NULL, (group), (type), (type2),		\
		RES_STATE_ACTIVE), TWL_DFLT_DELAY }
#define TWL_REMAP_SLEEP(res, devgrp, typ, typ2)				\
	{ .resource = (res), .devgroup = (devgrp),			\
	  .type = (typ), .type2 = (typ2),				\
	  .remap_off = TWL_REMAP_OFF,					\
	  .remap_sleep = TWL_REMAP_SLEEP, }
#define TWL_REMAP_OFF(res, devgrp, typ, typ2)				\
	{ .resource = (res), .devgroup = (devgrp),			\
	  .type = (typ), .type2 = (typ2),				\
	  .remap_off = TWL_REMAP_OFF, .remap_sleep = TWL_REMAP_OFF, }

static int twl4030_write_script_byte(u8 address, u8 byte)
{
	int err;

	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, address, R_MEMORY_ADDRESS);
	if (err)
		goto out;
	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, byte, R_MEMORY_DATA);
out:
	return err;
}

static int twl4030_write_script_ins(u8 address, u16 pmb_message,
					   u8 delay, u8 next)
{
	int err;

	address *= 4;
	err = twl4030_write_script_byte(address++, pmb_message >> 8);
	if (err)
		goto out;
	err = twl4030_write_script_byte(address++, pmb_message & 0xff);
	if (err)
		goto out;
	err = twl4030_write_script_byte(address++, delay);
	if (err)
		goto out;
	err = twl4030_write_script_byte(address++, next);
out:
	return err;
}

static int twl4030_write_script(u8 address, struct twl4030_ins *script,
				       int len)
{
	int err = -EINVAL;

	for (; len; len--, address++, script++) {
		if (len == 1) {
			err = twl4030_write_script_ins(address,
						script->pmb_message,
						script->delay,
						END_OF_SCRIPT);
			if (err)
				break;
		} else {
			err = twl4030_write_script_ins(address,
						script->pmb_message,
						script->delay,
						address + 1);
			if (err)
				break;
		}
	}
	return err;
}

static int twl4030_config_wakeup3_sequence(u8 address)
{
	int err;
	u8 data;

	/* Set SLEEP to ACTIVE SEQ address for P3 */
	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, address, R_SEQ_ADD_S2A3);
	if (err)
		goto out;

	/* P3 LVL_WAKEUP should be on LEVEL */
	err = twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &data, R_P3_SW_EVENTS);
	if (err)
		goto out;
	data |= PWR_LVL_WAKEUP;
	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, data, R_P3_SW_EVENTS);
out:
	if (err)
		pr_err("TWL4030 wakeup sequence for P3 config error\n");
	return err;
}

static int
twl4030_config_wakeup12_sequence(const struct twl4030_power_data *pdata,
				 u8 address)
{
	int err = 0;
	u8 data;

	/* Set SLEEP to ACTIVE SEQ address for P1 and P2 */
	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, address, R_SEQ_ADD_S2A12);
	if (err)
		goto out;

	/* P1/P2 LVL_WAKEUP should be on LEVEL */
	err = twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &data, R_P1_SW_EVENTS);
	if (err)
		goto out;

	data |= PWR_LVL_WAKEUP;
	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, data, R_P1_SW_EVENTS);
	if (err)
		goto out;

	err = twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &data, R_P2_SW_EVENTS);
	if (err)
		goto out;

	data |= PWR_LVL_WAKEUP;
	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, data, R_P2_SW_EVENTS);
	if (err)
		goto out;

	if (pdata->ac_charger_quirk || machine_is_omap_3430sdp() ||
	    machine_is_omap_ldp()) {
		/* Disabling AC charger effect on sleep-active transitions */
		err = twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &data,
				      R_CFG_P1_TRANSITION);
		if (err)
			goto out;
		data &= ~STARTON_CHG;
		err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, data,
				       R_CFG_P1_TRANSITION);
		if (err)
			goto out;
	}

out:
	if (err)
		pr_err("TWL4030 wakeup sequence for P1 and P2" \
			"config error\n");
	return err;
}

static int twl4030_config_sleep_sequence(u8 address)
{
	int err;

	/* Set ACTIVE to SLEEP SEQ address in T2 memory*/
	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, address, R_SEQ_ADD_A2S);

	if (err)
		pr_err("TWL4030 sleep sequence config error\n");

	return err;
}

static int twl4030_config_warmreset_sequence(u8 address)
{
	int err;
	u8 rd_data;

	/* Set WARM RESET SEQ address for P1 */
	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, address, R_SEQ_ADD_WARM);
	if (err)
		goto out;

	/* P1/P2/P3 enable WARMRESET */
	err = twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &rd_data, R_P1_SW_EVENTS);
	if (err)
		goto out;

	rd_data |= PWR_ENABLE_WARMRESET;
	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, rd_data, R_P1_SW_EVENTS);
	if (err)
		goto out;

	err = twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &rd_data, R_P2_SW_EVENTS);
	if (err)
		goto out;

	rd_data |= PWR_ENABLE_WARMRESET;
	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, rd_data, R_P2_SW_EVENTS);
	if (err)
		goto out;

	err = twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &rd_data, R_P3_SW_EVENTS);
	if (err)
		goto out;

	rd_data |= PWR_ENABLE_WARMRESET;
	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, rd_data, R_P3_SW_EVENTS);
out:
	if (err)
		pr_err("TWL4030 warmreset seq config error\n");
	return err;
}

static int twl4030_configure_resource(struct twl4030_resconfig *rconfig)
{
	int rconfig_addr;
	int err;
	u8 type;
	u8 grp;
	u8 remap;

	if (rconfig->resource > TOTAL_RESOURCES) {
		pr_err("TWL4030 Resource %d does not exist\n",
			rconfig->resource);
		return -EINVAL;
	}

	rconfig_addr = res_config_addrs[rconfig->resource];

	/* Set resource group */
	err = twl_i2c_read_u8(TWL_MODULE_PM_RECEIVER, &grp,
			      rconfig_addr + DEV_GRP_OFFSET);
	if (err) {
		pr_err("TWL4030 Resource %d group could not be read\n",
			rconfig->resource);
		return err;
	}

	if (rconfig->devgroup != TWL4030_RESCONFIG_UNDEF) {
		grp &= ~DEV_GRP_MASK;
		grp |= rconfig->devgroup << DEV_GRP_SHIFT;
		err = twl_i2c_write_u8(TWL_MODULE_PM_RECEIVER,
				       grp, rconfig_addr + DEV_GRP_OFFSET);
		if (err < 0) {
			pr_err("TWL4030 failed to program devgroup\n");
			return err;
		}
	}

	/* Set resource types */
	err = twl_i2c_read_u8(TWL_MODULE_PM_RECEIVER, &type,
				rconfig_addr + TYPE_OFFSET);
	if (err < 0) {
		pr_err("TWL4030 Resource %d type could not be read\n",
			rconfig->resource);
		return err;
	}

	if (rconfig->type != TWL4030_RESCONFIG_UNDEF) {
		type &= ~TYPE_MASK;
		type |= rconfig->type << TYPE_SHIFT;
	}

	if (rconfig->type2 != TWL4030_RESCONFIG_UNDEF) {
		type &= ~TYPE2_MASK;
		type |= rconfig->type2 << TYPE2_SHIFT;
	}

	err = twl_i2c_write_u8(TWL_MODULE_PM_RECEIVER,
				type, rconfig_addr + TYPE_OFFSET);
	if (err < 0) {
		pr_err("TWL4030 failed to program resource type\n");
		return err;
	}

	/* Set remap states */
	err = twl_i2c_read_u8(TWL_MODULE_PM_RECEIVER, &remap,
			      rconfig_addr + REMAP_OFFSET);
	if (err < 0) {
		pr_err("TWL4030 Resource %d remap could not be read\n",
			rconfig->resource);
		return err;
	}

	if (rconfig->remap_off != TWL4030_RESCONFIG_UNDEF) {
		remap &= ~OFF_STATE_MASK;
		remap |= rconfig->remap_off << OFF_STATE_SHIFT;
	}

	if (rconfig->remap_sleep != TWL4030_RESCONFIG_UNDEF) {
		remap &= ~SLEEP_STATE_MASK;
		remap |= rconfig->remap_sleep << SLEEP_STATE_SHIFT;
	}

	err = twl_i2c_write_u8(TWL_MODULE_PM_RECEIVER,
			       remap,
			       rconfig_addr + REMAP_OFFSET);
	if (err < 0) {
		pr_err("TWL4030 failed to program remap\n");
		return err;
	}

	return 0;
}

static int load_twl4030_script(const struct twl4030_power_data *pdata,
			       struct twl4030_script *tscript,
			       u8 address)
{
	int err;
	static int order;

	/* Make sure the script isn't going beyond last valid address (0x3f) */
	if ((address + tscript->size) > END_OF_SCRIPT) {
		pr_err("TWL4030 scripts too big error\n");
		return -EINVAL;
	}

	err = twl4030_write_script(address, tscript->script, tscript->size);
	if (err)
		goto out;

	if (tscript->flags & TWL4030_WRST_SCRIPT) {
		err = twl4030_config_warmreset_sequence(address);
		if (err)
			goto out;
	}
	if (tscript->flags & TWL4030_WAKEUP12_SCRIPT) {
		/* Reset any existing sleep script to avoid hangs on reboot */
		err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, END_OF_SCRIPT,
				       R_SEQ_ADD_A2S);
		if (err)
			goto out;

		err = twl4030_config_wakeup12_sequence(pdata, address);
		if (err)
			goto out;
		order = 1;
	}
	if (tscript->flags & TWL4030_WAKEUP3_SCRIPT) {
		err = twl4030_config_wakeup3_sequence(address);
		if (err)
			goto out;
	}
	if (tscript->flags & TWL4030_SLEEP_SCRIPT) {
		if (!order)
			pr_warn("TWL4030: Bad order of scripts (sleep script before wakeup) Leads to boot failure on some boards\n");
		err = twl4030_config_sleep_sequence(address);
	}
out:
	return err;
}

int twl4030_remove_script(u8 flags)
{
	int err = 0;

	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, TWL4030_PM_MASTER_KEY_CFG1,
			       TWL4030_PM_MASTER_PROTECT_KEY);
	if (err) {
		pr_err("twl4030: unable to unlock PROTECT_KEY\n");
		return err;
	}

	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, TWL4030_PM_MASTER_KEY_CFG2,
			       TWL4030_PM_MASTER_PROTECT_KEY);
	if (err) {
		pr_err("twl4030: unable to unlock PROTECT_KEY\n");
		return err;
	}

	if (flags & TWL4030_WRST_SCRIPT) {
		err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, END_OF_SCRIPT,
				       R_SEQ_ADD_WARM);
		if (err)
			return err;
	}
	if (flags & TWL4030_WAKEUP12_SCRIPT) {
		err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, END_OF_SCRIPT,
				       R_SEQ_ADD_S2A12);
		if (err)
			return err;
	}
	if (flags & TWL4030_WAKEUP3_SCRIPT) {
		err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, END_OF_SCRIPT,
				       R_SEQ_ADD_S2A3);
		if (err)
			return err;
	}
	if (flags & TWL4030_SLEEP_SCRIPT) {
		err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, END_OF_SCRIPT,
				       R_SEQ_ADD_A2S);
		if (err)
			return err;
	}

	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, 0,
			       TWL4030_PM_MASTER_PROTECT_KEY);
	if (err)
		pr_err("TWL4030 Unable to relock registers\n");

	return err;
}

static int
twl4030_power_configure_scripts(const struct twl4030_power_data *pdata)
{
	int err;
	int i;
	u8 address = twl4030_start_script_address;

	for (i = 0; i < pdata->num; i++) {
		err = load_twl4030_script(pdata, pdata->scripts[i], address);
		if (err)
			return err;
		address += pdata->scripts[i]->size;
	}

	return 0;
}

static void twl4030_patch_rconfig(struct twl4030_resconfig *common,
				  struct twl4030_resconfig *board)
{
	while (common->resource) {
		struct twl4030_resconfig *b = board;

		while (b->resource) {
			if (b->resource == common->resource) {
				*common = *b;
				break;
			}
			b++;
		}
		common++;
	}
}

static int
twl4030_power_configure_resources(const struct twl4030_power_data *pdata)
{
	struct twl4030_resconfig *resconfig = pdata->resource_config;
	struct twl4030_resconfig *boardconf = pdata->board_config;
	int err;

	if (resconfig) {
		if (boardconf)
			twl4030_patch_rconfig(resconfig, boardconf);

		while (resconfig->resource) {
			err = twl4030_configure_resource(resconfig);
			if (err)
				return err;
			resconfig++;
		}
	}

	return 0;
}

static int twl4030_starton_mask_and_set(u8 bitmask, u8 bitvalues)
{
	u8 regs[3] = { TWL4030_PM_MASTER_CFG_P1_TRANSITION,
		       TWL4030_PM_MASTER_CFG_P2_TRANSITION,
		       TWL4030_PM_MASTER_CFG_P3_TRANSITION, };
	u8 val;
	int i, err;

	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, TWL4030_PM_MASTER_KEY_CFG1,
			       TWL4030_PM_MASTER_PROTECT_KEY);
	if (err)
		goto relock;
	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER,
			       TWL4030_PM_MASTER_KEY_CFG2,
			       TWL4030_PM_MASTER_PROTECT_KEY);
	if (err)
		goto relock;

	for (i = 0; i < sizeof(regs); i++) {
		err = twl_i2c_read_u8(TWL_MODULE_PM_MASTER,
				      &val, regs[i]);
		if (err)
			break;
		val = (~bitmask & val) | (bitmask & bitvalues);
		err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER,
				       val, regs[i]);
		if (err)
			break;
	}

	if (err)
		pr_err("TWL4030 Register access failed: %i\n", err);

relock:
	return twl_i2c_write_u8(TWL_MODULE_PM_MASTER, 0,
				TWL4030_PM_MASTER_PROTECT_KEY);
}

/*
 * In master mode, start the power off sequence.
 * After a successful execution, TWL shuts down the power to the SoC
 * and all peripherals connected to it.
 */
void twl4030_power_off(void)
{
	int err;

	/* Disable start on charger or VBUS as it can break poweroff */
	err = twl4030_starton_mask_and_set(STARTON_VBUS | STARTON_CHG, 0);
	if (err)
		pr_err("TWL4030 Unable to configure start-up\n");

	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, PWR_DEVOFF,
			       TWL4030_PM_MASTER_P1_SW_EVENTS);
	if (err)
		pr_err("TWL4030 Unable to power off\n");
}

static bool twl4030_power_use_poweroff(const struct twl4030_power_data *pdata,
					struct device_node *node)
{
	if (pdata && pdata->use_poweroff)
		return true;

	if (of_property_read_bool(node, "ti,system-power-controller"))
		return true;

	if (of_property_read_bool(node, "ti,use_poweroff"))
		return true;

	return false;
}

#ifdef CONFIG_OF

/* Generic warm reset configuration for omap3 */

static struct twl4030_ins omap3_wrst_seq[] = {
	TWL_RESOURCE_OFF(RES_NRES_PWRON),
	TWL_RESOURCE_OFF(RES_RESET),
	TWL_RESOURCE_RESET(RES_MAIN_REF),
	TWL_RESOURCE_GROUP_RESET(RES_GRP_ALL, RES_TYPE_R0, RES_TYPE2_R2),
	TWL_RESOURCE_RESET(RES_VUSB_3V1),
	TWL_RESOURCE_RESET(RES_VMMC1),
	TWL_RESOURCE_GROUP_RESET(RES_GRP_ALL, RES_TYPE_R0, RES_TYPE2_R1),
	TWL_RESOURCE_GROUP_RESET(RES_GRP_RC, RES_TYPE_ALL, RES_TYPE2_R0),
	TWL_RESOURCE_ON(RES_RESET),
	TWL_RESOURCE_ON(RES_NRES_PWRON),
};

static struct twl4030_script omap3_wrst_script = {
	.script	= omap3_wrst_seq,
	.size	= ARRAY_SIZE(omap3_wrst_seq),
	.flags	= TWL4030_WRST_SCRIPT,
};

static struct twl4030_script *omap3_reset_scripts[] = {
	&omap3_wrst_script,
};

static struct twl4030_resconfig omap3_rconfig[] = {
	TWL_REMAP_SLEEP(RES_HFCLKOUT, DEV_GRP_P3, -1, -1),
	TWL_REMAP_SLEEP(RES_VDD1, DEV_GRP_P1, -1, -1),
	TWL_REMAP_SLEEP(RES_VDD2, DEV_GRP_P1, -1, -1),
	{ 0, 0 },
};

static struct twl4030_power_data omap3_reset = {
	.scripts		= omap3_reset_scripts,
	.num			= ARRAY_SIZE(omap3_reset_scripts),
	.resource_config	= omap3_rconfig,
};

/* Recommended generic default idle configuration for off-idle */

/* Broadcast message to put res to sleep */
static struct twl4030_ins omap3_idle_sleep_on_seq[] = {
	TWL_RESOURCE_GROUP_SLEEP(RES_GRP_ALL, RES_TYPE_ALL, 0),
};

static struct twl4030_script omap3_idle_sleep_on_script = {
	.script	= omap3_idle_sleep_on_seq,
	.size	= ARRAY_SIZE(omap3_idle_sleep_on_seq),
	.flags	= TWL4030_SLEEP_SCRIPT,
};

/* Broadcast message to put res to active */
static struct twl4030_ins omap3_idle_wakeup_p12_seq[] = {
	TWL_RESOURCE_GROUP_ACTIVE(RES_GRP_ALL, RES_TYPE_ALL, 0),
};

static struct twl4030_script omap3_idle_wakeup_p12_script = {
	.script	= omap3_idle_wakeup_p12_seq,
	.size	= ARRAY_SIZE(omap3_idle_wakeup_p12_seq),
	.flags	= TWL4030_WAKEUP12_SCRIPT,
};

/* Broadcast message to put res to active */
static struct twl4030_ins omap3_idle_wakeup_p3_seq[] = {
	TWL_RESOURCE_SET_ACTIVE(RES_CLKEN, 0x37),
	TWL_RESOURCE_GROUP_ACTIVE(RES_GRP_ALL, RES_TYPE_ALL, 0),
};

static struct twl4030_script omap3_idle_wakeup_p3_script = {
	.script	= omap3_idle_wakeup_p3_seq,
	.size	= ARRAY_SIZE(omap3_idle_wakeup_p3_seq),
	.flags	= TWL4030_WAKEUP3_SCRIPT,
};

static struct twl4030_script *omap3_idle_scripts[] = {
	&omap3_idle_wakeup_p12_script,
	&omap3_idle_wakeup_p3_script,
	&omap3_wrst_script,
	&omap3_idle_sleep_on_script,
};

/*
 * Recommended configuration based on "Recommended Sleep
 * Sequences for the Zoom Platform":
 * http://omappedia.com/wiki/File:Recommended_Sleep_Sequences_Zoom.pdf
 * Note that the type1 and type2 seem to be just the init order number
 * for type1 and type2 groups as specified in the document mentioned
 * above.
 */
static struct twl4030_resconfig omap3_idle_rconfig[] = {
	TWL_REMAP_SLEEP(RES_VAUX1, TWL4030_RESCONFIG_UNDEF, 0, 0),
	TWL_REMAP_SLEEP(RES_VAUX2, TWL4030_RESCONFIG_UNDEF, 0, 0),
	TWL_REMAP_SLEEP(RES_VAUX3, TWL4030_RESCONFIG_UNDEF, 0, 0),
	TWL_REMAP_SLEEP(RES_VAUX4, TWL4030_RESCONFIG_UNDEF, 0, 0),
	TWL_REMAP_SLEEP(RES_VMMC1, TWL4030_RESCONFIG_UNDEF, 0, 0),
	TWL_REMAP_SLEEP(RES_VMMC2, TWL4030_RESCONFIG_UNDEF, 0, 0),
	TWL_REMAP_OFF(RES_VPLL1, DEV_GRP_P1, 3, 1),
	TWL_REMAP_SLEEP(RES_VPLL2, DEV_GRP_P1, 0, 0),
	TWL_REMAP_SLEEP(RES_VSIM, TWL4030_RESCONFIG_UNDEF, 0, 0),
	TWL_REMAP_SLEEP(RES_VDAC, TWL4030_RESCONFIG_UNDEF, 0, 0),
	TWL_REMAP_SLEEP(RES_VINTANA1, TWL_DEV_GRP_P123, 1, 2),
	TWL_REMAP_SLEEP(RES_VINTANA2, TWL_DEV_GRP_P123, 0, 2),
	TWL_REMAP_SLEEP(RES_VINTDIG, TWL_DEV_GRP_P123, 1, 2),
	TWL_REMAP_SLEEP(RES_VIO, TWL_DEV_GRP_P123, 2, 2),
	TWL_REMAP_OFF(RES_VDD1, DEV_GRP_P1, 4, 1),
	TWL_REMAP_OFF(RES_VDD2, DEV_GRP_P1, 3, 1),
	TWL_REMAP_SLEEP(RES_VUSB_1V5, TWL4030_RESCONFIG_UNDEF, 0, 0),
	TWL_REMAP_SLEEP(RES_VUSB_1V8, TWL4030_RESCONFIG_UNDEF, 0, 0),
	TWL_REMAP_SLEEP(RES_VUSB_3V1, TWL_DEV_GRP_P123, 0, 0),
	/* Resource #20 USB charge pump skipped */
	TWL_REMAP_SLEEP(RES_REGEN, TWL_DEV_GRP_P123, 2, 1),
	TWL_REMAP_SLEEP(RES_NRES_PWRON, TWL_DEV_GRP_P123, 0, 1),
	TWL_REMAP_SLEEP(RES_CLKEN, TWL_DEV_GRP_P123, 3, 2),
	TWL_REMAP_SLEEP(RES_SYSEN, TWL_DEV_GRP_P123, 6, 1),
	TWL_REMAP_SLEEP(RES_HFCLKOUT, DEV_GRP_P3, 0, 2),
	TWL_REMAP_SLEEP(RES_32KCLKOUT, TWL_DEV_GRP_P123, 0, 0),
	TWL_REMAP_SLEEP(RES_RESET, TWL_DEV_GRP_P123, 6, 0),
	TWL_REMAP_SLEEP(RES_MAIN_REF, TWL_DEV_GRP_P123, 0, 0),
	{ /* Terminator */ },
};

static struct twl4030_power_data omap3_idle = {
	.scripts		= omap3_idle_scripts,
	.num			= ARRAY_SIZE(omap3_idle_scripts),
	.resource_config	= omap3_idle_rconfig,
};

/* Disable 32 KiHz oscillator during idle */
static struct twl4030_resconfig osc_off_rconfig[] = {
	TWL_REMAP_OFF(RES_CLKEN, DEV_GRP_P1 | DEV_GRP_P3, 3, 2),
	{ /* Terminator */ },
};

static struct twl4030_power_data osc_off_idle = {
	.scripts		= omap3_idle_scripts,
	.num			= ARRAY_SIZE(omap3_idle_scripts),
	.resource_config	= omap3_idle_rconfig,
	.board_config		= osc_off_rconfig,
};

static struct twl4030_power_data omap3_idle_ac_quirk = {
	.scripts		= omap3_idle_scripts,
	.num			= ARRAY_SIZE(omap3_idle_scripts),
	.resource_config	= omap3_idle_rconfig,
	.ac_charger_quirk	= true,
};

static struct twl4030_power_data omap3_idle_ac_quirk_osc_off = {
	.scripts		= omap3_idle_scripts,
	.num			= ARRAY_SIZE(omap3_idle_scripts),
	.resource_config	= omap3_idle_rconfig,
	.board_config		= osc_off_rconfig,
	.ac_charger_quirk	= true,
};

static const struct of_device_id twl4030_power_of_match[] = {
	{
		.compatible = "ti,twl4030-power",
	},
	{
		.compatible = "ti,twl4030-power-reset",
		.data = &omap3_reset,
	},
	{
		.compatible = "ti,twl4030-power-idle",
		.data = &omap3_idle,
	},
	{
		.compatible = "ti,twl4030-power-idle-osc-off",
		.data = &osc_off_idle,
	},
	{
		.compatible = "ti,twl4030-power-omap3-sdp",
		.data = &omap3_idle_ac_quirk,
	},
	{
		.compatible = "ti,twl4030-power-omap3-ldp",
		.data = &omap3_idle_ac_quirk_osc_off,
	},
	{
		.compatible = "ti,twl4030-power-omap3-evm",
		.data = &omap3_idle_ac_quirk,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, twl4030_power_of_match);
#endif	/* CONFIG_OF */

static int twl4030_power_probe(struct platform_device *pdev)
{
	const struct twl4030_power_data *pdata = dev_get_platdata(&pdev->dev);
	struct device_node *node = pdev->dev.of_node;
	const struct of_device_id *match;
	int err = 0;
	int err2 = 0;
	u8 val;

	if (!pdata && !node) {
		dev_err(&pdev->dev, "Platform data is missing\n");
		return -EINVAL;
	}

	err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, TWL4030_PM_MASTER_KEY_CFG1,
			       TWL4030_PM_MASTER_PROTECT_KEY);
	err |= twl_i2c_write_u8(TWL_MODULE_PM_MASTER,
			       TWL4030_PM_MASTER_KEY_CFG2,
			       TWL4030_PM_MASTER_PROTECT_KEY);

	if (err) {
		pr_err("TWL4030 Unable to unlock registers\n");
		return err;
	}

	match = of_match_device(of_match_ptr(twl4030_power_of_match),
				&pdev->dev);
	if (match && match->data)
		pdata = match->data;

	if (pdata) {
		err = twl4030_power_configure_scripts(pdata);
		if (err) {
			pr_err("TWL4030 failed to load scripts\n");
			goto relock;
		}
		err = twl4030_power_configure_resources(pdata);
		if (err) {
			pr_err("TWL4030 failed to configure resource\n");
			goto relock;
		}
	}

	/* Board has to be wired properly to use this feature */
	if (twl4030_power_use_poweroff(pdata, node) && !pm_power_off) {
		/* Default for SEQ_OFFSYNC is set, lets ensure this */
		err = twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &val,
				      TWL4030_PM_MASTER_CFG_P123_TRANSITION);
		if (err) {
			pr_warn("TWL4030 Unable to read registers\n");
		} else if (!(val & SEQ_OFFSYNC)) {
			val |= SEQ_OFFSYNC;
			err = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, val,
					TWL4030_PM_MASTER_CFG_P123_TRANSITION);
			if (err) {
				pr_err("TWL4030 Unable to setup SEQ_OFFSYNC\n");
				goto relock;
			}
		}

		pm_power_off = twl4030_power_off;
	}

relock:
	err2 = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, 0,
			       TWL4030_PM_MASTER_PROTECT_KEY);
	if (err2) {
		pr_err("TWL4030 Unable to relock registers\n");
		return err2;
	}

	return err;
}

static struct platform_driver twl4030_power_driver = {
	.driver = {
		.name	= "twl4030_power",
		.of_match_table = of_match_ptr(twl4030_power_of_match),
	},
	.probe		= twl4030_power_probe,
};

module_platform_driver(twl4030_power_driver);

MODULE_AUTHOR("Nokia Corporation");
MODULE_AUTHOR("Texas Instruments, Inc.");
MODULE_DESCRIPTION("Power management for TWL4030");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:twl4030_power");

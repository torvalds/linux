/*
 * drivers/power/axp_power/axp20-board.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/i2c.h>
#include <mach/irqs.h>
#include <linux/power_supply.h>
#include <linux/apm_bios.h>
#include <linux/apm-emulation.h>
#include <linux/mfd/axp-mfd.h>

#include "axp-cfg.h"
#include <plat/sys_config.h>


int pmu_used;
int pmu_twi_id;
int pmu_irq_id;
int pmu_twi_addr;
int pmu_battery_rdc;
int pmu_battery_cap;
int pmu_init_chgcur;
int pmu_suspend_chgcur;
int pmu_resume_chgcur;
int pmu_shutdown_chgcur;
int pmu_init_chgvol;
int pmu_init_chgend_rate;
int pmu_init_chg_enabled;
int pmu_init_adc_freq;
int pmu_init_adc_freqc;
int pmu_init_chg_pretime;
int pmu_init_chg_csttime;

int pmu_bat_para1;
int pmu_bat_para2;
int pmu_bat_para3;
int pmu_bat_para4;
int pmu_bat_para5;
int pmu_bat_para6;
int pmu_bat_para7;
int pmu_bat_para8;
int pmu_bat_para9;
int pmu_bat_para10;
int pmu_bat_para11;
int pmu_bat_para12;
int pmu_bat_para13;
int pmu_bat_para14;
int pmu_bat_para15;
int pmu_bat_para16;

int pmu_usbvol_limit;
int pmu_usbvol;
int pmu_usbcur_limit;
int pmu_usbcur;

int pmu_pwroff_vol;
int pmu_pwron_vol;

int dcdc2_vol;
int dcdc3_vol;
int ldo2_vol;
int ldo3_vol;
int ldo4_vol;

int pmu_pekoff_time;
int pmu_pekoff_en;
int pmu_peklong_time;
int pmu_pekon_time;
int pmu_pwrok_time;
int pmu_pwrnoe_time;
int pmu_intotp_en;

/* Reverse engineered partly from Platformx drivers */
enum axp_regls{

	vcc_ldo1,
	vcc_ldo2,
	vcc_ldo3,
	vcc_ldo4,
	vcc_ldo5,

	vcc_buck2,
	vcc_buck3,
	vcc_ldoio0,
};

/* The values of the various regulator constraints are obviously dependent
 * on exactly what is wired to each ldo.  Unfortunately this information is
 * not generally available.  More information has been requested from Xbow
 * but as of yet they haven't been forthcoming.
 *
 * Some of these are clearly Stargate 2 related (no way of plugging
 * in an lcd on the IM2 for example!).
 */

static struct regulator_consumer_supply ldo1_data[] = {
		{
			.supply = "axp20_rtc",
		},
	};


static struct regulator_consumer_supply ldo2_data[] = {
		{
			.supply = "axp20_analog/fm",
		},
	};

static struct regulator_consumer_supply ldo3_data[] = {
		{
			.supply = "axp20_pll",
		},
	};

static struct regulator_consumer_supply ldo4_data[] = {
		{
			.supply = "axp20_hdmi",
		},
	};

static struct regulator_consumer_supply ldoio0_data[] = {
		{
			.supply = "axp20_mic",
		},
	};


static struct regulator_consumer_supply buck2_data[] = {
		{
			.supply = "axp20_core",
		},
	};

static struct regulator_consumer_supply buck3_data[] = {
		{
			.supply = "axp20_ddr",
		},
	};



static struct regulator_init_data axp_regl_init_data[] = {
	[vcc_ldo1] = {
		.constraints = { /* board default 1.25V */
			.name = "axp20_ldo1",
			.min_uV =  AXP20LDO1 * 1000,
			.max_uV =  AXP20LDO1 * 1000,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo1_data),
		.consumer_supplies = ldo1_data,
	},
	[vcc_ldo2] = {
		.constraints = { /* board default 3.0V */
			.name = "axp20_ldo2",
			.min_uV = 1800000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
			.initial_state = PM_SUSPEND_STANDBY,
			.state_standby = {
				//.uV = ldo2_vol * 1000,
				.enabled = 1,
			}
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo2_data),
		.consumer_supplies = ldo2_data,
	},
	[vcc_ldo3] = {
		.constraints = {/* default is 1.8V */
			.name = "axp20_ldo3",
			.min_uV =  700 * 1000,
			.max_uV =  3500* 1000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
			.initial_state = PM_SUSPEND_STANDBY,
			.state_standby = {
				//.uV = ldo3_vol * 1000,
				.enabled = 1,
			}
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo3_data),
		.consumer_supplies = ldo3_data,
	},
	[vcc_ldo4] = {
		.constraints = {
			/* board default is 3.3V */
			.name = "axp20_ldo4",
			.min_uV = 1250000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
			.initial_state = PM_SUSPEND_STANDBY,
			.state_standby = {
				//.uV = ldo4_vol * 1000,
				.enabled = 1,
			}
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo4_data),
		.consumer_supplies = ldo4_data,
	},
	[vcc_buck2] = {
		.constraints = { /* default 1.24V */
			.name = "axp20_buck2",
			.min_uV = 700 * 1000,
			.max_uV = 2275 * 1000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
			.initial_state = PM_SUSPEND_STANDBY,
			.state_standby = {
				//.uV = dcdc2_vol * 1000,
				.enabled = 1,
			}
		},
		.num_consumer_supplies = ARRAY_SIZE(buck2_data),
		.consumer_supplies = buck2_data,
	},
	[vcc_buck3] = {
		.constraints = { /* default 2.5V */
			.name = "axp20_buck3",
			.min_uV = 700 * 1000,
			.max_uV = 3500 * 1000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
			.initial_state = PM_SUSPEND_STANDBY,
			.state_standby = {
				//.uV = dcdc3_vol * 1000,
				.enabled = 1,
			}
		},
		.num_consumer_supplies = ARRAY_SIZE(buck3_data),
		.consumer_supplies = buck3_data,
	},
	[vcc_ldoio0] = {
		.constraints = { /* default 2.5V */
			.name = "axp20_ldoio0",
			.min_uV = 1800 * 1000,
			.max_uV = 3300 * 1000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldoio0_data),
		.consumer_supplies = ldoio0_data,
	},
};

static struct axp_funcdev_info axp_regldevs[] = {
	{
		.name = "axp20-regulator",
		.id = AXP20_ID_LDO1,
		.platform_data = &axp_regl_init_data[vcc_ldo1],
	}, {
		.name = "axp20-regulator",
		.id = AXP20_ID_LDO2,
		.platform_data = &axp_regl_init_data[vcc_ldo2],
	}, {
		.name = "axp20-regulator",
		.id = AXP20_ID_LDO3,
		.platform_data = &axp_regl_init_data[vcc_ldo3],
	}, {
		.name = "axp20-regulator",
		.id = AXP20_ID_LDO4,
		.platform_data = &axp_regl_init_data[vcc_ldo4],
	}, {
		.name = "axp20-regulator",
		.id = AXP20_ID_BUCK2,
		.platform_data = &axp_regl_init_data[vcc_buck2],
	}, {
		.name = "axp20-regulator",
		.id = AXP20_ID_BUCK3,
		.platform_data = &axp_regl_init_data[vcc_buck3],
	}, {
		.name = "axp20-regulator",
		.id = AXP20_ID_LDOIO0,
		.platform_data = &axp_regl_init_data[vcc_ldoio0],
	},
};

static struct power_supply_info battery_data ={
		.name ="PTI PL336078",
		.technology = POWER_SUPPLY_TECHNOLOGY_LION,
		//.voltage_max_design = pmu_init_chgvol,
		//.voltage_min_design = pmu_pwroff_vol,
		//.energy_full_design = pmu_battery_cap,
		.use_for_apm = 1,
};


static struct axp_supply_init_data axp_sply_init_data = {
	.battery_info = &battery_data,
	//.chgcur = pmu_init_chgcur,
	//.chgvol = pmu_init_chgvol,
	//.chgend = pmu_init_chgend_rate,
	//.chgen = pmu_init_chg_enabled,
	//.sample_time = pmu_init_adc_freq,
	//.chgpretime = pmu_init_chg_pretime,
	//.chgcsttime = pmu_init_chg_csttime,
};

static struct axp_funcdev_info axp_splydev[]={
   	{
   		.name = "axp20-supplyer",
			.id = AXP20_ID_SUPPLY,
      .platform_data = &axp_sply_init_data,
    },
};

static struct axp_funcdev_info axp_gpiodev[]={
   	{   .name = "axp20-gpio",
   		.id = AXP20_ID_GPIO,
    },
};

static struct axp_platform_data axp_pdata = {
	.num_regl_devs = ARRAY_SIZE(axp_regldevs),
	.num_sply_devs = ARRAY_SIZE(axp_splydev),
	.num_gpio_devs = ARRAY_SIZE(axp_gpiodev),
	.regl_devs = axp_regldevs,
	.sply_devs = axp_splydev,
	.gpio_devs = axp_gpiodev,
	.gpio_base = 0,
};

static struct i2c_board_info __initdata axp_mfd_i2c_board_info[] = {
	{
		.type = "axp20_mfd",
		//.addr = pmu_twi_addr,
		.platform_data = &axp_pdata,
		//.irq = pmu_irq_id,
	},
};

static int __init axp_board_init(void)
{
		int ret;
    ret = script_parser_fetch("pmu_para", "pmu_used", &pmu_used, sizeof(int));
    if (ret)
    {
        printk("axp driver uning configuration failed(%d)\n", __LINE__);
        return -1;
    }
    if (pmu_used)
    {
        ret = script_parser_fetch("pmu_para", "pmu_twi_id", &pmu_twi_id, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_twi_id = AXP20_I2CBUS;
        }
        ret = script_parser_fetch("pmu_para", "pmu_irq_id", &pmu_irq_id, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_irq_id = AXP20_IRQNO;
        }
        ret = script_parser_fetch("pmu_para", "pmu_twi_addr", &pmu_twi_addr, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_twi_addr = AXP20_ADDR;
        }
        ret = script_parser_fetch("pmu_para", "pmu_battery_rdc", &pmu_battery_rdc, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_battery_rdc = BATRDC;
        }
        ret = script_parser_fetch("pmu_para", "pmu_battery_cap", &pmu_battery_cap, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_battery_cap = BATTERYCAP;
        }
        ret = script_parser_fetch("pmu_para", "pmu_init_chgcur", &pmu_init_chgcur, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_init_chgcur = INTCHGCUR / 1000;
        }
        pmu_init_chgcur = pmu_init_chgcur * 1000;
        ret = script_parser_fetch("pmu_para", "pmu_suspend_chgcur", &pmu_suspend_chgcur, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_suspend_chgcur = SUSCHGCUR / 1000;
        }
        pmu_suspend_chgcur = pmu_suspend_chgcur * 1000;
        ret = script_parser_fetch("pmu_para", "pmu_resume_chgcur", &pmu_resume_chgcur, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_resume_chgcur = RESCHGCUR / 1000;
        }
        pmu_resume_chgcur = pmu_resume_chgcur * 1000;
        ret = script_parser_fetch("pmu_para", "pmu_shutdown_chgcur", &pmu_shutdown_chgcur, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_shutdown_chgcur = CLSCHGCUR / 1000;
        }
        pmu_shutdown_chgcur = pmu_shutdown_chgcur * 1000;
        ret = script_parser_fetch("pmu_para", "pmu_init_chgvol", &pmu_init_chgvol, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_init_chgvol = INTCHGVOL / 1000;
        }
        pmu_init_chgvol = pmu_init_chgvol * 1000;
        ret = script_parser_fetch("pmu_para", "pmu_init_chgend_rate", &pmu_init_chgend_rate, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_init_chgend_rate = INTCHGENDRATE;
        }
        ret = script_parser_fetch("pmu_para", "pmu_init_chg_enabled", &pmu_init_chg_enabled, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_init_chg_enabled = INTCHGENABLED;
        }
        ret = script_parser_fetch("pmu_para", "pmu_init_adc_freq", &pmu_init_adc_freq, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_init_adc_freq = INTADCFREQ;
        }
        ret = script_parser_fetch("pmu_para", "pmu_init_adc_freqc", &pmu_init_adc_freqc, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_init_adc_freq = INTADCFREQC;
        }
        ret = script_parser_fetch("pmu_para", "pmu_init_chg_pretime", &pmu_init_chg_pretime, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_init_chg_pretime = INTCHGPRETIME;
        }
        ret = script_parser_fetch("pmu_para", "pmu_init_chg_csttime", &pmu_init_chg_csttime, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_init_chg_csttime = INTCHGCSTTIME;
        }

        ret = script_parser_fetch("pmu_para", "pmu_bat_para1", &pmu_bat_para1, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para1 = OCVREG0;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para2", &pmu_bat_para2, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para2 = OCVREG1;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para3", &pmu_bat_para3, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para3 = OCVREG2;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para4", &pmu_bat_para4, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para4 = OCVREG3;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para5", &pmu_bat_para5, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para5 = OCVREG4;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para6", &pmu_bat_para6, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para6 = OCVREG5;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para7", &pmu_bat_para7, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para7 = OCVREG6;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para8", &pmu_bat_para8, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para8 = OCVREG7;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para9", &pmu_bat_para9, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para9 = OCVREG8;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para10", &pmu_bat_para10, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para10 = OCVREG9;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para11", &pmu_bat_para11, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para11 = OCVREGA;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para12", &pmu_bat_para12, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para12 = OCVREGB;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para13", &pmu_bat_para13, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para13 = OCVREGC;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para14", &pmu_bat_para14, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para14 = OCVREGD;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para15", &pmu_bat_para15, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para15 = OCVREGE;
        }
        ret = script_parser_fetch("pmu_para", "pmu_bat_para16", &pmu_bat_para16, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_bat_para16 = OCVREGF;
        }

        ret = script_parser_fetch("pmu_para", "pmu_usbvol_limit", &pmu_usbvol_limit, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_usbvol_limit = 1;
        }
        ret = script_parser_fetch("pmu_para", "pmu_usbvol", &pmu_usbvol, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_usbvol = 4400;
        }
        ret = script_parser_fetch("pmu_para", "pmu_usbcur_limit", &pmu_usbcur_limit, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_usbcur_limit = 0;
        }
        ret = script_parser_fetch("pmu_para", "pmu_usbcur", &pmu_usbcur, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_usbcur = 900;
        }
        ret = script_parser_fetch("pmu_para", "pmu_pwroff_vol", &pmu_pwroff_vol, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_pwroff_vol = 3300;
        }
        ret = script_parser_fetch("pmu_para", "pmu_pwron_vol", &pmu_pwron_vol, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_pwron_vol = 2900;
        }

        ret = script_parser_fetch("target", "dcdc2_vol", &dcdc2_vol, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            dcdc2_vol = 1400;
        }
        ret = script_parser_fetch("target", "dcdc3_vol", &dcdc3_vol, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            dcdc3_vol = 1250;
        }
        ret = script_parser_fetch("target", "ldo2_vol", &ldo2_vol, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            ldo2_vol = 3000;
        }
        ret = script_parser_fetch("target", "ldo3_vol", &ldo3_vol, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            ldo3_vol = 2800;
        }
        ret = script_parser_fetch("target", "ldo4_vol", &ldo4_vol, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            ldo4_vol = 2800;
        }

				ret = script_parser_fetch("pmu_para", "pmu_pekoff_time", &pmu_pekoff_time, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_pekoff_time = 6000;
        }
        ret = script_parser_fetch("pmu_para", "pmu_pekoff_en", &pmu_pekoff_en, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_pekoff_en   = 1;
        }
        ret = script_parser_fetch("pmu_para", "pmu_peklong_time", &pmu_peklong_time, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_peklong_time = 1500;
        }
        ret = script_parser_fetch("pmu_para", "pmu_pwrok_time", &pmu_pwrok_time, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
           pmu_pwrok_time    = 64;
        }
        ret = script_parser_fetch("pmu_para", "pmu_pwrnoe_time", &pmu_pwrnoe_time, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_pwrnoe_time = 2000;
        }
        ret = script_parser_fetch("pmu_para", "pmu_intotp_en", &pmu_intotp_en, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_intotp_en = 1;
        }   
        ret = script_parser_fetch("pmu_para", "pmu_pekon_time", &pmu_pekon_time, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_pekon_time = 1000;
        }           

        axp_regl_init_data[1].constraints.state_standby.uV = ldo2_vol * 1000;
        axp_regl_init_data[2].constraints.state_standby.uV = ldo3_vol * 1000;
        axp_regl_init_data[3].constraints.state_standby.uV = ldo4_vol * 1000;
        axp_regl_init_data[5].constraints.state_standby.uV = dcdc2_vol * 1000;
        axp_regl_init_data[6].constraints.state_standby.uV = dcdc3_vol * 1000;
        battery_data.voltage_max_design = pmu_init_chgvol;
        battery_data.voltage_min_design = pmu_pwroff_vol;
        battery_data.energy_full_design = pmu_battery_cap;
        axp_sply_init_data.chgcur = pmu_init_chgcur;
        axp_sply_init_data.chgvol = pmu_init_chgvol;
        axp_sply_init_data.chgend = pmu_init_chgend_rate;
        axp_sply_init_data.chgen = pmu_init_chg_enabled;
        axp_sply_init_data.sample_time = pmu_init_adc_freq;
        axp_sply_init_data.chgpretime = pmu_init_chg_pretime;
        axp_sply_init_data.chgcsttime = pmu_init_chg_csttime;
        axp_mfd_i2c_board_info[0].addr = pmu_twi_addr;
        axp_mfd_i2c_board_info[0].irq = pmu_irq_id;

        return i2c_register_board_info(pmu_twi_id, axp_mfd_i2c_board_info,
				ARRAY_SIZE(axp_mfd_i2c_board_info));
    }
    else
        return -1;

}
fs_initcall(axp_board_init);

MODULE_DESCRIPTION("Krosspower axp board");
MODULE_AUTHOR("Donglu Zhang Krosspower");
MODULE_LICENSE("GPL");


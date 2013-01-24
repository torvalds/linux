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
#include <linux/input.h>
#include <linux/delay.h>
#include <mach/sys_config.h>
#include "axp-cfg.h"
//#include "axp-sply.h"



//int axp_usbcurflag = 0;
//int axp_usbvolflag= 0;

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

/*
int axp_usbvol(void)
{
	axp_usbvolflag = 1;
    return 0;
}
EXPORT_SYMBOL_GPL(axp_usbvol);

int axp_usbcur(void)
{
    axp_usbcurflag = 1;
    return 0;
}
EXPORT_SYMBOL_GPL(axp_usbcur);

int axp_usbvol_restore(void)
{
 	axp_usbvolflag = 0;
    return 0;
}
EXPORT_SYMBOL_GPL(axp_usbvol_restore);

int axp_usbcur_restore(void)
{
	axp_usbcurflag = 0;
    return 0;
}
EXPORT_SYMBOL_GPL(axp_usbcur_restore);
*/

/* Reverse engineered partly from Platformx drivers */
enum axp_regls{

	vcc_ldo1,
	vcc_ldo2,
	vcc_ldo3,
	vcc_ldo4,
	vcc_ldo5,
	vcc_ldo6,
	vcc_ldo7,

	vcc_dcdc1,
	vcc_dcdc2,
	vcc_dcdc3,
	vcc_dcdc4,
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
			.supply = "axp15_rtc",
		},
	};



static struct regulator_consumer_supply ldo2_data[] = {
		{
			.supply = "axp15_analog/fm",
		},
	};

static struct regulator_consumer_supply ldo3_data[] = {
		{
			.supply = "axp15_analog/fm1",
		},
	};

static struct regulator_consumer_supply ldo4_data[] = {
		{
			.supply = "axp15_analog/fm2",
		},
	};

static struct regulator_consumer_supply ldo5_data[] = {
		{
			.supply = "axp15_pll/sdram",
		},
	};

static struct regulator_consumer_supply ldo6_data[] = {
		{
			.supply = "axp15_pll/hdmi",
		},
	};

static struct regulator_consumer_supply ldo7_data[] = {
		{
			.supply = "axp15_mic",
		},
	};

static struct regulator_consumer_supply buck1_data[] = {
		{
			.supply = "axp15_io",
		}
	};

static struct regulator_consumer_supply buck2_data[] = {
		{
			.supply = "axp15_core",
		},
	};

static struct regulator_consumer_supply buck3_data[] = {
		{
			.supply = "axp15_ddr",
		},
	};

static struct regulator_consumer_supply buck4_data[] = {
		{
			.supply = "axp15_ddr2",
		},
	};

static struct regulator_init_data axp_regl_init_data[] = {
	[vcc_ldo1] = {
		.constraints = { /* board default 1.25V */
			.name = "axp15_ldo1",
			.min_uV =  2500000,
			.max_uV =  5000000,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo1_data),
		.consumer_supplies = ldo1_data,
	},
	[vcc_ldo2] = {
		.constraints = { /* board default 3.0V */
			.name = "axp15_ldo2",
			.min_uV = 1300000,
			.max_uV = 1800000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo2_data),
		.consumer_supplies = ldo2_data,

	},
	[vcc_ldo3] = {
		.constraints = {/* default is 1.8V */
			.name = "axp15_ldo3",
			.min_uV = 1200000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo3_data),
		.consumer_supplies = ldo3_data,

	},
	[vcc_ldo4] = {
		.constraints = {
			/* board default is 3.3V */
			.name = "axp15_ldo4",
			.min_uV = 1200000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo4_data),
		.consumer_supplies = ldo4_data,
	},
	[vcc_ldo5] = {
		.constraints = { /* default 3.3V */
			.name = "axp15_ldo5",
			.min_uV = 700000,
			.max_uV = 3500000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo5_data),
		.consumer_supplies = ldo5_data,
	},
	[vcc_ldo6] = {
		.constraints = { /* default 3.3V */
			.name = "axp15_ldo6",
			.min_uV = 700000,
			.max_uV = 3500000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo6_data),
		.consumer_supplies = ldo6_data,
	},
	[vcc_ldo7] = {
		.constraints = { /* default 3.3V */
			.name = "axp15_ldoIO0",
			.min_uV = 1800000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo7_data),
		.consumer_supplies = ldo7_data,
	},
	[vcc_dcdc1] = {
		.constraints = { /* default 3.3V */
			.name = "axp15_dcdc1",
			.min_uV = 1700000,
			.max_uV = 3500000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE ,
		},
		.num_consumer_supplies = ARRAY_SIZE(buck1_data),
		.consumer_supplies = buck1_data,
	},
	[vcc_dcdc2] = {
		.constraints = { /* default 1.24V */
			.name = "axp15_dcdc2",
			//.min_uV = DCDC2MIN * 1000,
			//.max_uV = DCDC2MAX * 1000,
			.min_uV = 700000,
			.max_uV = 2275000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(buck2_data),
		.consumer_supplies = buck2_data,
	},
	[vcc_dcdc3] = {
		.constraints = { /* default 2.5V */
			.name = "axp15_dcdc3",
			.min_uV = 700000,
			.max_uV = 3500000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(buck3_data),
		.consumer_supplies = buck3_data,
	},
	[vcc_dcdc4] = {
		.constraints = { /* default 2.5V */
			.name = "axp15_dcdc4",
			.min_uV = 700000,
			.max_uV = 3500000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(buck4_data),
		.consumer_supplies = buck4_data,
	},
};

static struct axp_funcdev_info axp_regldevs[] = {
	{
		.name = "axp15-regulator",
		.id = AXP15_ID_LDO0,
		.platform_data = &axp_regl_init_data[vcc_ldo1],
	}, {
		.name = "axp15-regulator",
		.id = AXP15_ID_LDO1,
		.platform_data = &axp_regl_init_data[vcc_ldo2],
	}, {
		.name = "axp15-regulator",
		.id = AXP15_ID_LDO2,
		.platform_data = &axp_regl_init_data[vcc_ldo3],
	}, {
		.name = "axp15-regulator",
		.id = AXP15_ID_LDO3,
		.platform_data = &axp_regl_init_data[vcc_ldo4],
	}, {
		.name = "axp15-regulator",
		.id = AXP15_ID_LDO4,
		.platform_data = &axp_regl_init_data[vcc_ldo5],
	}, {
		.name = "axp15-regulator",
		.id = AXP15_ID_LDO5,
		.platform_data = &axp_regl_init_data[vcc_ldo6],
	}, {
		.name = "axp15-regulator",
		.id = AXP15_ID_LDOIO0,
		.platform_data = &axp_regl_init_data[vcc_ldo7],
	}, {
		.name = "axp15-regulator",
		.id = AXP15_ID_DCDC1,
		.platform_data = &axp_regl_init_data[vcc_dcdc1],
	}, {
		.name = "axp15-regulator",
		.id = AXP15_ID_DCDC2,
		.platform_data = &axp_regl_init_data[vcc_dcdc2],
	}, {
		.name = "axp15-regulator",
		.id = AXP15_ID_DCDC3,
		.platform_data = &axp_regl_init_data[vcc_dcdc3],
	}, {
		.name = "axp15-regulator",
		.id = AXP15_ID_DCDC4,
		.platform_data = &axp_regl_init_data[vcc_dcdc4],
	},
};
static struct power_supply_info battery_data ={
		.name ="PTI PL336078",
		.technology = POWER_SUPPLY_TECHNOLOGY_LiFe,
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
   		.name = "axp15-supplyer",
			.id = AXP15_ID_SUPPLY,
      .platform_data = &axp_sply_init_data,
    },
};

static struct axp_funcdev_info axp_gpiodev[]={
   	{   .name = "axp15-gpio",
   		.id = AXP15_ID_GPIO,
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
		.type = "axp15_mfd",
		.addr = AXP15_ADDR,
		.platform_data = &axp_pdata,
		.irq = SW_INT_IRQNO_ENMI,
	},
};

static int __init axp_board_init(void)
{
		int ret;
    ret = script_parser_fetch("pmu_para", "pmu_used", &pmu_used, sizeof(int));
    if (ret)
    {
        printk("axp driver uning configuration failed(%d)\n", __LINE__);
        return ;
    }
    if (pmu_used)
    {
        ret = script_parser_fetch("pmu_para", "pmu_twi_id", &pmu_twi_id, sizeof(int));
		printk("line:%d,pmu_twi_id=%d\n",__LINE__,pmu_twi_id);
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_twi_id = AXP15_I2CBUS;
        }
        ret = script_parser_fetch("pmu_para", "pmu_irq_id", &pmu_irq_id, sizeof(int));
		printk("line:%d,pmu_irq_id=%d\n",__LINE__,pmu_irq_id);
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_irq_id = AXP15_IRQNO;
        }
        ret = script_parser_fetch("pmu_para", "pmu_twi_addr", &pmu_twi_addr, sizeof(int));
		printk("line:%d,pmu_twi_addr=%d\n",__LINE__,pmu_twi_addr);
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_twi_addr = AXP15_ADDR;
        }
        ret = script_parser_fetch("pmu_para", "pmu_battery_rdc", &pmu_battery_rdc, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
			//int BATRDC = 0;   
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
            pmu_usbcur_limit = 1;
        }
        ret = script_parser_fetch("pmu_para", "pmu_usbcur", &pmu_usbcur, sizeof(int));
        if (ret)
        {
            printk("axp driver uning configuration failed(%d)\n", __LINE__);
            pmu_usbcur = 0;
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
        axp_regl_init_data[1].constraints.state_standby.enabled = (ldo2_vol)?1:0;
        axp_regl_init_data[1].constraints.state_standby.disabled = (ldo2_vol)?0:1;
        axp_regl_init_data[2].constraints.state_standby.enabled = (ldo3_vol)?1:0;
        axp_regl_init_data[2].constraints.state_standby.disabled = (ldo3_vol)?0:1;
        axp_regl_init_data[3].constraints.state_standby.enabled = (ldo4_vol)?1:0;
        axp_regl_init_data[3].constraints.state_standby.disabled = (ldo4_vol)?0:1;
        axp_regl_init_data[5].constraints.state_standby.enabled = (dcdc2_vol)?1:0;
        axp_regl_init_data[5].constraints.state_standby.disabled = (dcdc2_vol)?0:1;
        axp_regl_init_data[6].constraints.state_standby.enabled = (dcdc3_vol)?1:0;
        axp_regl_init_data[6].constraints.state_standby.disabled = (dcdc3_vol)?0:1;
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

MODULE_DESCRIPTION("Axp board");
MODULE_AUTHOR("Kyle Cheung");
MODULE_LICENSE("GPL");


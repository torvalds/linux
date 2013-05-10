#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/i2c.h>
#include <mach/irqs.h>
#include <linux/power_supply.h>
#include <linux/mfd/axp-mfd.h>


#include "axp-cfg.h"


static struct platform_device virt[]={

	{
			.name = "reg-15-cs-ldo0",
			.id = -1,
			.dev		= {
				.platform_data = "axp15_ldo0",
			}

	},{
			.name = "reg-15-cs-rtcldo",
			.id = -1,
			.dev		= {
				.platform_data = "axp15_rtc",
			}
	},{
			.name = "reg-15-cs-aldo1",
			.id = -1,
			.dev		= {
				.platform_data = "axp15_analog/fm",
			}
 	},{
			.name = "reg-15-cs-aldo2",
			.id = -1,
			.dev		= {
				.platform_data = "axp15_analog/fm2",
			}
 	},{
			.name = "reg-15-cs-dldo1",
			.id = -1,
			.dev		= {
				.platform_data = "axp15_pll/sdram",
			}
	},{
			.name = "reg-15-cs-dldo2",
			.id = -1,
			.dev		= {
				.platform_data = "axp15_pll/hdmi",
			}
 	},{
			.name = "reg-15-cs-gpioldo",
			.id = -1,
			.dev		= {
				.platform_data = "axp15_gpio",
			}
 	},{
			.name = "reg-15-cs-buck1",
			.id = -1,
			.dev		= {
				.platform_data = "axp15_io",
			}
 	},{
			.name = "reg-15-cs-buck2",
			.id = -1,
			.dev		= {
				.platform_data = "axp15_core",
			}
 	},{
			.name = "reg-15-cs-buck3",
			.id = -1,
			.dev		= {
				.platform_data = "axp15_ddr",
			}
	},{
			.name = "reg-15-cs-buck4",
			.id = -1,
			.dev		= {
				.platform_data = "axp15_ddr2",
			}
	},{
			.name = "reg-15-cs-ldoio0",
			.id = -1,
			.dev		= {
				.platform_data = "axp15_mic",
			}
	},
};



 static int __init virtual_init(void)
{
	int j,ret;
	for (j = 0; j < ARRAY_SIZE(virt); j++){
 		ret =  platform_device_register(&virt[j]);
  		if (ret)
				goto creat_devices_failed;
	}

	return ret;

creat_devices_failed:
	while (j--)
		platform_device_register(&virt[j]);
	return ret;

}

module_init(virtual_init);

static void __exit virtual_exit(void)
{
	int j;
	for (j = ARRAY_SIZE(virt) - 1; j >= 0; j--){
		platform_device_unregister(&virt[j]);
	}
}
module_exit(virtual_exit);

MODULE_DESCRIPTION("Axp regulator test");
MODULE_AUTHOR("Kyle Cheung");
MODULE_LICENSE("GPL");
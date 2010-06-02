/*
 * Copyright (C) 2010 Motorola, Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/leds-ld-cpcap.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/spi/cpcap.h>
#include <linux/spi/cpcap-regbits.h>
#include <linux/spi/spi.h>

#include <mach/irqs.h>

#include "gpio-names.h"

static struct cpcap_device *cpcap_di;

static int cpcap_validity_reboot(struct notifier_block *this,
				 unsigned long code, void *cmd)
{
	int ret = -1;
	int result = NOTIFY_DONE;
	char *mode = cmd;

	dev_info(&(cpcap_di->spi->dev), "Saving power down reason.\n");

	if (code == SYS_RESTART) {
		if (mode != NULL && !strncmp("outofcharge", mode, 12)) {
			/* Set the outofcharge bit in the cpcap */
			ret = cpcap_regacc_write(cpcap_di, CPCAP_REG_VAL1,
						 CPCAP_BIT_OUT_CHARGE_ONLY,
						 CPCAP_BIT_OUT_CHARGE_ONLY);
			if (ret) {
				dev_err(&(cpcap_di->spi->dev),
					"outofcharge cpcap set failure.\n");
				result = NOTIFY_BAD;
			}
			/* Set the soft reset bit in the cpcap */
			cpcap_regacc_write(cpcap_di, CPCAP_REG_VAL1,
					   CPCAP_BIT_SOFT_RESET,
					   CPCAP_BIT_SOFT_RESET);
			if (ret) {
				dev_err(&(cpcap_di->spi->dev),
					"reset cpcap set failure.\n");
				result = NOTIFY_BAD;
			}
		}

		/* Check if we are starting recovery mode */
		if (mode != NULL && !strncmp("recovery", mode, 9)) {
			/* Set the fota (recovery mode) bit in the cpcap */
			ret = cpcap_regacc_write(cpcap_di, CPCAP_REG_VAL1,
				CPCAP_BIT_FOTA_MODE, CPCAP_BIT_FOTA_MODE);
			if (ret) {
				dev_err(&(cpcap_di->spi->dev),
					"Recovery cpcap set failure.\n");
				result = NOTIFY_BAD;
			}
		} else {
			/* Set the fota (recovery mode) bit in the cpcap */
			ret = cpcap_regacc_write(cpcap_di, CPCAP_REG_VAL1, 0,
						 CPCAP_BIT_FOTA_MODE);
			if (ret) {
				dev_err(&(cpcap_di->spi->dev),
					"Recovery cpcap clear failure.\n");
				result = NOTIFY_BAD;
			}
		}
		/* Check if we are going into fast boot mode */
		if (mode != NULL && !strncmp("bootloader", mode, 11)) {
			/* Set the bootmode bit in the cpcap */
			ret = cpcap_regacc_write(cpcap_di, CPCAP_REG_VAL1,
				CPCAP_BIT_BOOT_MODE, CPCAP_BIT_BOOT_MODE);
			if (ret) {
				dev_err(&(cpcap_di->spi->dev),
					"Boot mode cpcap set failure.\n");
				result = NOTIFY_BAD;
			}
		}
	} else {
		ret = cpcap_regacc_write(cpcap_di, CPCAP_REG_VAL1,
					 0,
					 CPCAP_BIT_OUT_CHARGE_ONLY);
		if (ret) {
			dev_err(&(cpcap_di->spi->dev),
				"outofcharge cpcap set failure.\n");
			result = NOTIFY_BAD;
		}

		/* Clear the soft reset bit in the cpcap */
		ret = cpcap_regacc_write(cpcap_di, CPCAP_REG_VAL1, 0,
					 CPCAP_BIT_SOFT_RESET);
		if (ret) {
			dev_err(&(cpcap_di->spi->dev),
				"SW Reset cpcap set failure.\n");
			result = NOTIFY_BAD;
		}
		/* Clear the fota (recovery mode) bit in the cpcap */
		ret = cpcap_regacc_write(cpcap_di, CPCAP_REG_VAL1, 0,
					 CPCAP_BIT_FOTA_MODE);
		if (ret) {
			dev_err(&(cpcap_di->spi->dev),
				"Recovery cpcap clear failure.\n");
			result = NOTIFY_BAD;
		}
	}

	/* Always clear the kpanic bit */
	ret = cpcap_regacc_write(cpcap_di, CPCAP_REG_VAL1,
				 0, CPCAP_BIT_AP_KERNEL_PANIC);
	if (ret) {
		dev_err(&(cpcap_di->spi->dev),
			"Clear kernel panic bit failure.\n");
		result = NOTIFY_BAD;
	}

	return result;
}
static struct notifier_block validity_reboot_notifier = {
	.notifier_call = cpcap_validity_reboot,
};

static int cpcap_validity_probe(struct platform_device *pdev)
{
	if (pdev->dev.platform_data == NULL) {
		dev_err(&pdev->dev, "no platform_data\n");
		return -EINVAL;
	}

	cpcap_di = pdev->dev.platform_data;

	cpcap_regacc_write(cpcap_di, CPCAP_REG_VAL1,
			   (CPCAP_BIT_AP_KERNEL_PANIC | CPCAP_BIT_SOFT_RESET),
			   (CPCAP_BIT_AP_KERNEL_PANIC | CPCAP_BIT_SOFT_RESET));

	register_reboot_notifier(&validity_reboot_notifier);

	return 0;
}

static int cpcap_validity_remove(struct platform_device *pdev)
{
	unregister_reboot_notifier(&validity_reboot_notifier);
	cpcap_di = NULL;

	return 0;
}

static struct platform_driver cpcap_validity_driver = {
	.probe = cpcap_validity_probe,
	.remove = cpcap_validity_remove,
	.driver = {
		.name = "cpcap_validity",
		.owner  = THIS_MODULE,
	},
};

static struct platform_device cpcap_validity_device = {
	.name   = "cpcap_validity",
	.id     = -1,
	.dev    = {
		.platform_data  = NULL,
	},
};

static struct platform_device cpcap_3mm5_device = {
	.name   = "cpcap_3mm5",
	.id     = -1,
	.dev    = {
		.platform_data  = NULL,
	},
};

static struct cpcap_whisper_pdata whisper_pdata = {
	.data_gpio = TEGRA_GPIO_PV4,
	.pwr_gpio  = TEGRA_GPIO_PT2,
	.uartmux   = 1,
};

static struct platform_device cpcap_whisper_device = {
	.name   = "cpcap_whisper",
	.id     = -1,
	.dev    = {
		.platform_data  = &whisper_pdata,
	},
};

static struct cpcap_led stingray_display_led = {
	.cpcap_register = CPCAP_REG_KLC,
	.cpcap_mask = 0x03FF,
	.on_val = 0x00F5,
	.off_val = 0x00F4,
	.cpcap_duty_cycle = 0x2A0,
	.cpcap_current = 0x0,
	.class_name = LD_DISP_BUTTON_DEV,
	.led_regulator = "sw5",
};

static struct platform_device cpcap_disp_button_led = {
	.name   = LD_CPCAP_LED_DRV,
	.id     = 1,
	.dev    = {
		.platform_data  = &stingray_display_led,
	},
};
static struct cpcap_led stingray_privacy_led ={
	.cpcap_register = CPCAP_REG_BLEDC,
	.cpcap_mask = 0x03FF,
	.on_val = 0x00F5,
	.off_val = 0x00F4,
	.cpcap_duty_cycle = 0x41,
	.cpcap_current = 0x0,
	.class_name = LD_PRIVACY_LED_DEV,
	.led_regulator = "sw5",
};

static struct platform_device cpcap_privacy_led = {
	.name   = LD_CPCAP_LED_DRV,
	.id     = 2,
	.dev    = {
		.platform_data  = &stingray_privacy_led,
	},
};
static struct platform_device *cpcap_devices[] = {
	&cpcap_validity_device,
	&cpcap_whisper_device,
	&cpcap_disp_button_led,
	&cpcap_privacy_led,
	&cpcap_3mm5_device,
};

struct cpcap_spi_init_data stingray_cpcap_spi_init[] = {
	{CPCAP_REG_ADCC1,     0x9000},
	{CPCAP_REG_ADCC2,     0x4136},
	{CPCAP_REG_USBC1,     0x1201},
	{CPCAP_REG_USBC3,     0x7DFB},
	{CPCAP_REG_OWDC,      0x0003},
};

unsigned short cpcap_regulator_mode_values[CPCAP_NUM_REGULATORS] = {
	[CPCAP_SW2]      = 0x0100,
	[CPCAP_SW4]      = 0x0100,
	[CPCAP_SW5]      = 0x0022,
	[CPCAP_VCAM]     = 0x0003,
	[CPCAP_VCSI]     = 0x0003,
	[CPCAP_VDAC]     = 0x0003,
	[CPCAP_VDIG]     = 0x0003,
	[CPCAP_VFUSE]    = 0x0080,
	[CPCAP_VHVIO]    = 0x0003,
	[CPCAP_VSDIO]    = 0x0003,
	[CPCAP_VPLL]     = 0x0042,
	[CPCAP_VRF1]     = 0x000C,
	[CPCAP_VRF2]     = 0x0003,
	[CPCAP_VRFREF]   = 0x0003,
	[CPCAP_VWLAN1]   = 0x0003,
	[CPCAP_VWLAN2]   = 0x000C,
	[CPCAP_VSIM]     = 0x0003,
	[CPCAP_VSIMCARD] = 0x1E00,
	[CPCAP_VVIB]     = 0x0001,
	[CPCAP_VUSB]     = 0x000C,
	[CPCAP_VAUDIO]   = 0x0006,
};

unsigned short cpcap_regulator_off_mode_values[CPCAP_NUM_REGULATORS] = {
	[CPCAP_SW2]      = 0x0000,
	[CPCAP_SW4]      = 0x0000,
	[CPCAP_SW5]      = 0x0000,
	[CPCAP_VCAM]     = 0x0000,
	[CPCAP_VCSI]     = 0x0000,
	[CPCAP_VDAC]     = 0x0000,
	[CPCAP_VDIG]     = 0x0000,
	[CPCAP_VFUSE]    = 0x0000,
	[CPCAP_VHVIO]    = 0x0000,
	[CPCAP_VSDIO]    = 0x0000,
	[CPCAP_VPLL]     = 0x0000,
	[CPCAP_VRF1]     = 0x0000,
	[CPCAP_VRF2]     = 0x0000,
	[CPCAP_VRFREF]   = 0x0000,
	[CPCAP_VWLAN1]   = 0x0000,
	[CPCAP_VWLAN2]   = 0x0000,
	[CPCAP_VSIM]     = 0x0000,
	[CPCAP_VSIMCARD] = 0x0000,
	[CPCAP_VVIB]     = 0x0000,
	[CPCAP_VUSB]     = 0x0000,
	[CPCAP_VAUDIO]   = 0x0000,
};

#define REGULATOR_CONSUMER(name, device) { .supply = name, .dev = device, }

struct regulator_consumer_supply cpcap_sw2_consumers[] = {
	REGULATOR_CONSUMER("sw2", NULL),
};

struct regulator_consumer_supply cpcap_sw4_consumers[] = {
	REGULATOR_CONSUMER("sw4", NULL),
};

struct regulator_consumer_supply cpcap_sw5_consumers[] = {
	REGULATOR_CONSUMER("sw5", NULL),
};

struct regulator_consumer_supply cpcap_vcam_consumers[] = {
	REGULATOR_CONSUMER("vcam", NULL /* cpcap_cam_device */),
};

struct regulator_consumer_supply cpcap_vhvio_consumers[] = {
	REGULATOR_CONSUMER("vhvio", NULL /* lighting_driver */),
#if 0
	REGULATOR_CONSUMER("vhvio", NULL /* lighting_driver */),
	REGULATOR_CONSUMER("vhvio", NULL /* magnetometer */),
	REGULATOR_CONSUMER("vhvio", NULL /* light sensor */),
	REGULATOR_CONSUMER("vhvio", NULL /* accelerometer */),
	REGULATOR_CONSUMER("vhvio", NULL /* display */),
#endif
};

struct regulator_consumer_supply cpcap_vsdio_consumers[] = {
	REGULATOR_CONSUMER("vsdio", NULL),
};

struct regulator_consumer_supply cpcap_vcsi_consumers[] = {
	REGULATOR_CONSUMER("vcsi", NULL),
};

struct regulator_consumer_supply cpcap_vwlan2_consumers[] = {
	REGULATOR_CONSUMER("vwlan2", NULL /* sd slot */),
};

struct regulator_consumer_supply cpcap_vvib_consumers[] = {
	REGULATOR_CONSUMER("vvib", NULL /* vibrator */),
};

struct regulator_consumer_supply cpcap_vusb_consumers[] = {
	REGULATOR_CONSUMER("vusb", &cpcap_whisper_device.dev),
};

struct regulator_consumer_supply cpcap_vaudio_consumers[] = {
	REGULATOR_CONSUMER("vaudio", NULL /* mic opamp */),
};

static struct regulator_init_data cpcap_regulator[CPCAP_NUM_REGULATORS] = {
	[CPCAP_SW2] = {
		.constraints = {
			.min_uV			= 1000000,
			.max_uV			= 1200000,
			.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE,
			.always_on		= 1,
		},
		.num_consumer_supplies	= ARRAY_SIZE(cpcap_sw2_consumers),
		.consumer_supplies	= cpcap_sw2_consumers,
	},
	[CPCAP_SW4] = {
		.constraints = {
			.min_uV			= 1000000,
			.max_uV			= 1200000,
			.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE,
			.always_on		= 1,
		},
		.num_consumer_supplies	= ARRAY_SIZE(cpcap_sw4_consumers),
		.consumer_supplies	= cpcap_sw4_consumers,
	},
	[CPCAP_SW5] = {
		.constraints = {
			.min_uV			= 5050000,
			.max_uV			= 5050000,
			.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
			.boot_on		= 1, /* Temporary for M1 */
			.apply_uV		= 1,
		},
		.num_consumer_supplies	= ARRAY_SIZE(cpcap_sw5_consumers),
		.consumer_supplies	= cpcap_sw5_consumers,
	},
	[CPCAP_VCAM] = {
		.constraints = {
			.min_uV			= 2900000,
			.max_uV			= 2900000,
			.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
			.apply_uV		= 1,

		},
		.num_consumer_supplies	= ARRAY_SIZE(cpcap_vcam_consumers),
		.consumer_supplies	= cpcap_vcam_consumers,
	},
	[CPCAP_VCSI] = {
		.constraints = {
			.min_uV			= 1200000,
			.max_uV			= 1200000,
			.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
			.boot_on		= 1,
			.apply_uV		= 1,
		},
		.num_consumer_supplies	= ARRAY_SIZE(cpcap_vcsi_consumers),
		.consumer_supplies	= cpcap_vcsi_consumers,
	},
	[CPCAP_VDAC] = {
		.constraints = {
			.min_uV			= 1800000,
			.max_uV			= 1800000,
			.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
			.apply_uV		= 1,
		},
	},
	[CPCAP_VDIG] = {
		.constraints = {
			.min_uV			= 1200000,
			.max_uV			= 1875000,
			.valid_ops_mask		= 0,
		},
	},
	[CPCAP_VFUSE] = {
		.constraints = {
			.min_uV			= 1500000,
			.max_uV			= 3150000,
			.valid_ops_mask		= (REGULATOR_CHANGE_VOLTAGE |
						   REGULATOR_CHANGE_STATUS),
		},
	},
	[CPCAP_VHVIO] = {
		.constraints = {
			.min_uV			= 2775000,
			.max_uV			= 2775000,
			.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
			.always_on		= 1,
			.apply_uV		= 1,
		},
		.num_consumer_supplies	= ARRAY_SIZE(cpcap_vhvio_consumers),
		.consumer_supplies	= cpcap_vhvio_consumers,
	},
	[CPCAP_VSDIO] = {
		.constraints = {
			.min_uV			= 3000000,
			.max_uV			= 3000000,
			.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
			.apply_uV		= 1,
		},
		.num_consumer_supplies	= ARRAY_SIZE(cpcap_vsdio_consumers),
		.consumer_supplies	= cpcap_vsdio_consumers,
	},
	[CPCAP_VPLL] = {
		.constraints = {
			.min_uV			= 1800000,
			.max_uV			= 1800000,
			.valid_ops_mask		= 0,
			.always_on		= 1,
			.apply_uV		= 1,
		},
	},
	[CPCAP_VRF1] = {
		.constraints = {
			.min_uV			= 2500000,
			.max_uV			= 2775000,
			.valid_ops_mask		= 0,
		},
	},
	[CPCAP_VRF2] = {
		.constraints = {
			.min_uV			= 2775000,
			.max_uV			= 2775000,
			.valid_ops_mask		= 0,
		},
	},
	[CPCAP_VRFREF] = {
		.constraints = {
			.min_uV			= 2500000,
			.max_uV			= 2775000,
			.valid_ops_mask		= 0,
		},
	},
	[CPCAP_VWLAN1] = {
		.constraints = {
			.min_uV			= 1800000,
			.max_uV			= 1900000,
			.valid_ops_mask		= 0,
		},
	},
	[CPCAP_VWLAN2] = {
		.constraints = {
			.min_uV			= 3300000,
			.max_uV			= 3300000,
			.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
			.apply_uV		= 1,
		},
		.num_consumer_supplies	= ARRAY_SIZE(cpcap_vwlan2_consumers),
		.consumer_supplies	= cpcap_vwlan2_consumers,
	},
	[CPCAP_VSIM] = {
		.constraints = {
			.min_uV			= 1800000,
			.max_uV			= 2900000,
			.valid_ops_mask		= 0,
		},
	},
	[CPCAP_VSIMCARD] = {
		.constraints = {
			.min_uV			= 1800000,
			.max_uV			= 2900000,
			.valid_ops_mask		= 0,
		},
	},
	[CPCAP_VVIB] = {
		.constraints = {
			.min_uV			= 1300000,
			.max_uV			= 3000000,
			.valid_ops_mask		= (REGULATOR_CHANGE_VOLTAGE |
						   REGULATOR_CHANGE_STATUS),
		},
		.num_consumer_supplies	= ARRAY_SIZE(cpcap_vvib_consumers),
		.consumer_supplies	= cpcap_vvib_consumers,
	},
	[CPCAP_VUSB] = {
		.constraints = {
			.min_uV			= 3300000,
			.max_uV			= 3300000,
			.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
			.apply_uV		= 1,
		},
		.num_consumer_supplies	= ARRAY_SIZE(cpcap_vusb_consumers),
		.consumer_supplies	= cpcap_vusb_consumers,
	},
	[CPCAP_VAUDIO] = {
		.constraints = {
			.min_uV			= 2775000,
			.max_uV			= 2775000,
			.valid_modes_mask	= (REGULATOR_MODE_NORMAL |
						   REGULATOR_MODE_STANDBY),
			.valid_ops_mask		= REGULATOR_CHANGE_MODE,
			.always_on		= 1,
			.apply_uV		= 1,
		},
		.num_consumer_supplies	= ARRAY_SIZE(cpcap_vaudio_consumers),
		.consumer_supplies	= cpcap_vaudio_consumers,
	},
};

static struct cpcap_adc_ato stingray_cpcap_adc_ato = {
	.ato_in = 0x0480,
	.atox_in = 0,
	.adc_ps_factor_in = 0x0200,
	.atox_ps_factor_in = 0,
	.ato_out = 0,
	.atox_out = 0,
	.adc_ps_factor_out = 0,
	.atox_ps_factor_out = 0,
};

static struct cpcap_platform_data stingray_cpcap_data = {
	.init = stingray_cpcap_spi_init,
	.init_len = ARRAY_SIZE(stingray_cpcap_spi_init),
	.regulator_mode_values = cpcap_regulator_mode_values,
	.regulator_off_mode_values = cpcap_regulator_off_mode_values,
	.regulator_init = cpcap_regulator,
	.adc_ato = &stingray_cpcap_adc_ato,
	.ac_changed = NULL,
	.batt_changed = NULL,
	.usb_changed = NULL,
};

static struct spi_board_info stingray_spi_board_info[] __initdata = {
	{
		.modalias = "cpcap",
		.bus_num = 1,
		.chip_select = 0,
		.mode = SPI_MODE_0,
		.max_speed_hz = 10000000,
		.controller_data = &stingray_cpcap_data,
		.irq = INT_EXTERNAL_PMU,
	},
};

int __init stingray_spi_init(void)
{
	int i;

	tegra_gpio_enable(TEGRA_GPIO_PT2);
	gpio_request(TEGRA_GPIO_PT2, "usb_host_pwr_en");
	gpio_direction_output(TEGRA_GPIO_PT2, 0);

	spi_register_board_info(stingray_spi_board_info,
				ARRAY_SIZE(stingray_spi_board_info));

	for (i = 0; i < ARRAY_SIZE(cpcap_devices); i++)
		cpcap_device_register(cpcap_devices[i]);

	(void) cpcap_driver_register(&cpcap_validity_driver);

	return 0;
}

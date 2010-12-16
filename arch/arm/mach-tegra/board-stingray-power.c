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
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/leds-ld-cpcap.h>
#include <linux/mdm6600_ctrl.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/max8649.h>
#include <linux/spi/cpcap.h>
#include <linux/spi/cpcap-regbits.h>
#include <linux/spi/spi.h>
#include <linux/l3g4200d.h>

#include <mach/gpio.h>
#include <mach/iomap.h>
#include <mach/irqs.h>

#include "board-stingray.h"
#include "gpio-names.h"

/* For the PWR + VOL UP reset, CPCAP can perform a hard or a soft reset. A hard
 * reset will reset the entire system, where a soft reset will reset only the
 * T20. Uncomment this line to use soft resets (should not be enabled on
 * production builds). */
/* #define ENABLE_SOFT_RESET_DEBUGGING */

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
	int err;

	if (pdev->dev.platform_data == NULL) {
		dev_err(&pdev->dev, "no platform_data\n");
		return -EINVAL;
	}

	cpcap_di = pdev->dev.platform_data;

	cpcap_regacc_write(cpcap_di, CPCAP_REG_VAL1,
			   (CPCAP_BIT_AP_KERNEL_PANIC | CPCAP_BIT_SOFT_RESET),
			   (CPCAP_BIT_AP_KERNEL_PANIC | CPCAP_BIT_SOFT_RESET));

	register_reboot_notifier(&validity_reboot_notifier);

	/* CORE_PWR_REQ is only properly connected on P1 hardware and later */
	if (stingray_revision() >= STINGRAY_REVISION_P1) {
		err = cpcap_uc_start(cpcap_di, CPCAP_MACRO_14);
		dev_info(&pdev->dev, "Started macro 14: %d\n", err);
	} else
		dev_info(&pdev->dev, "Not starting macro 14 (no hw support)\n");

	/* Enable workaround to allow soft resets to work */
	cpcap_regacc_write(cpcap_di, CPCAP_REG_PGC,
			   CPCAP_BIT_SYS_RST_MODE, CPCAP_BIT_SYS_RST_MODE);
	err = cpcap_uc_start(cpcap_di, CPCAP_MACRO_15);
	dev_info(&pdev->dev, "Started macro 15: %d\n", err);

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

static struct cpcap_led stingray_privacy_led ={
	.blink_able = 0,
	.cpcap_register = CPCAP_REG_BLEDC,
	.cpcap_reg_mask = 0x03FF,
	.cpcap_reg_period = 0x0000,
	.cpcap_reg_duty_cycle = 0x0038,
	.cpcap_reg_current = 0x0002,
	.class_name = LD_PRIVACY_LED_DEV,
	.led_regulator = "sw5_led2",
};

static struct platform_device cpcap_privacy_led = {
	.name   = LD_CPCAP_LED_DRV,
	.id     = 2,
	.dev    = {
		.platform_data  = &stingray_privacy_led,
	},
};

static struct cpcap_led stingray_notification_led ={
	.blink_able = 1,
	.cpcap_register = CPCAP_REG_ADLC,
	.cpcap_reg_mask = 0x7FFF,
	.cpcap_reg_period = 0x0000,
	.cpcap_reg_duty_cycle = 0x03F0,
	.cpcap_reg_current = 0x0008,
	.class_name = LD_NOTIF_LED_DEV,
	.led_regulator = "sw5_led3",
};

static struct platform_device cpcap_notification_led = {
	.name   = LD_CPCAP_LED_DRV,
	.id     = 3,
	.dev    = {
		.platform_data  = &stingray_notification_led,
	},
};

static struct platform_device *cpcap_devices[] = {
	&cpcap_validity_device,
	&cpcap_notification_led,
	&cpcap_privacy_led,
	&cpcap_3mm5_device,
};

struct cpcap_spi_init_data stingray_cpcap_spi_init[] = {
	{CPCAP_REG_S1C1,      0x0000},
	{CPCAP_REG_S1C2,      0x0000},
	{CPCAP_REG_S2C1,      0x4830},
	{CPCAP_REG_S2C2,      0x3030},
	{CPCAP_REG_S3C,       0x0439},
	{CPCAP_REG_S4C1,      0x4930},
	{CPCAP_REG_S4C2,      0x301C},
	{CPCAP_REG_S5C,       0x0000},
	{CPCAP_REG_S6C,       0x0000},
	{CPCAP_REG_VRF1C,     0x0000},
	{CPCAP_REG_VRF2C,     0x0000},
	{CPCAP_REG_VRFREFC,   0x0000},
	{CPCAP_REG_VAUDIOC,   0x0065},
	{CPCAP_REG_ADCC1,     0x9000},
	{CPCAP_REG_ADCC2,     0x4136},
	{CPCAP_REG_USBC1,     0x1201},
	{CPCAP_REG_USBC3,     0x7DFB},
	{CPCAP_REG_OWDC,      0x0003},
	{CPCAP_REG_ADLC,      0x0000},
};

unsigned short cpcap_regulator_mode_values[CPCAP_NUM_REGULATORS] = {
	[CPCAP_SW2]      = 0x0800,
	[CPCAP_SW4]      = 0x0900,
	[CPCAP_SW5]      = 0x0022,
	[CPCAP_VCAM]     = 0x0007,
	[CPCAP_VCSI]     = 0x0007,
	[CPCAP_VDAC]     = 0x0003,
	[CPCAP_VDIG]     = 0x0005,
	[CPCAP_VFUSE]    = 0x0080,
	[CPCAP_VHVIO]    = 0x0002,
	[CPCAP_VSDIO]    = 0x0002,
	[CPCAP_VPLL]     = 0x0001,
	[CPCAP_VRF1]     = 0x000C,
	[CPCAP_VRF2]     = 0x0003,
	[CPCAP_VRFREF]   = 0x0003,
	[CPCAP_VWLAN1]   = 0x0005,
	[CPCAP_VWLAN2]   = 0x0008,
	[CPCAP_VSIM]     = 0x0003,
	[CPCAP_VSIMCARD] = 0x1E00,
	[CPCAP_VVIB]     = 0x0001,
	[CPCAP_VUSB]     = 0x000C,
	[CPCAP_VAUDIO]   = 0x0004,
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

#define REGULATOR_CONSUMER(name, device) { .supply = name, .dev_name = device, }
#define REGULATOR_CONSUMER_BY_DEVICE(name, device) \
	{ .supply = name, .dev = device, }

struct regulator_consumer_supply cpcap_sw2_consumers[] = {
	REGULATOR_CONSUMER("sw2", NULL),
	REGULATOR_CONSUMER("vdd_core", NULL),
};

struct regulator_consumer_supply cpcap_sw4_consumers[] = {
	REGULATOR_CONSUMER("sw4", NULL),
	REGULATOR_CONSUMER("vdd_aon", NULL),
};

struct regulator_consumer_supply cpcap_sw5_consumers[] = {
	REGULATOR_CONSUMER_BY_DEVICE("sw5_led2", &cpcap_privacy_led.dev),
	REGULATOR_CONSUMER_BY_DEVICE("sw5_led3", &cpcap_notification_led.dev),
};

struct regulator_consumer_supply cpcap_vcam_consumers[] = {
	REGULATOR_CONSUMER("vcc", "2-000c" /* focuser */),
};

struct regulator_consumer_supply cpcap_vhvio_consumers[] = {
	REGULATOR_CONSUMER("vhvio", NULL /* lighting_driver */),
	REGULATOR_CONSUMER("vcc", "2-0068" /* gyro*/),
	REGULATOR_CONSUMER("vcc", "3-000c" /* magnetometer */),
	REGULATOR_CONSUMER("vcc", "0-0077" /* barometer */),
	REGULATOR_CONSUMER("vcc", "3-000f" /* accelerometer */),
};

struct regulator_consumer_supply cpcap_vcsi_consumers[] = {
	REGULATOR_CONSUMER("vcsi", "tegra_camera"),
};

struct regulator_consumer_supply cpcap_vusb_consumers[] = {
	REGULATOR_CONSUMER_BY_DEVICE("vusb", &cpcap_whisper_device.dev),
};

struct regulator_consumer_supply cpcap_vaudio_consumers[] = {
	REGULATOR_CONSUMER("vaudio", NULL /* mic opamp */),
};

struct regulator_consumer_supply cpcap_vdig_consumers[] = {
	REGULATOR_CONSUMER("vdig", NULL /* gps */),
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
			.min_uV			= 1875000,
			.max_uV			= 1875000,
			.valid_ops_mask		= 0,
			.always_on		= 1,
			.apply_uV		= 1,
		},
		.num_consumer_supplies	= ARRAY_SIZE(cpcap_vdig_consumers),
		.consumer_supplies	= cpcap_vdig_consumers,
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
			.valid_ops_mask		= 0,
			.always_on		= 1,
			.apply_uV		= 1,
		},
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
			.always_on		= 1,
		},
	},
	[CPCAP_VWLAN2] = {
		.constraints = {
			.min_uV			= 3300000,
			.max_uV			= 3300000,
			.valid_ops_mask		= 0,
			.always_on		= 1,
			.apply_uV		= 1,
		},
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
			.valid_ops_mask		= 0,
		},
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
	.hwcfg = {
		(CPCAP_HWCFG0_SEC_STBY_SW3 |
		 CPCAP_HWCFG0_SEC_STBY_SW4 |
		 CPCAP_HWCFG0_SEC_STBY_VAUDIO |
		 CPCAP_HWCFG0_SEC_STBY_VCAM |
		 CPCAP_HWCFG0_SEC_STBY_VCSI |
		 CPCAP_HWCFG0_SEC_STBY_VHVIO |
		 CPCAP_HWCFG0_SEC_STBY_VPLL |
		 CPCAP_HWCFG0_SEC_STBY_VSDIO),
		(CPCAP_HWCFG1_SEC_STBY_VWLAN1 |
		 CPCAP_HWCFG1_SEC_STBY_VWLAN2)}
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

struct regulator_consumer_supply max8649_consumers[] = {
	REGULATOR_CONSUMER("vdd_cpu", NULL /* cpu */),
};

struct regulator_init_data max8649_regulator_init_data[] = {
	{
		.constraints = {
			.min_uV			= 770000,
			.max_uV			= 1100000,
			.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE,
			.always_on		= 1,
		},
		.num_consumer_supplies	= ARRAY_SIZE(max8649_consumers),
		.consumer_supplies	= max8649_consumers,
	},
};

struct max8649_platform_data stingray_max8649_pdata = {
	.regulator = max8649_regulator_init_data,
	.mode = 1,
	.extclk = 0,
	.ramp_timing = MAX8649_RAMP_32MV,
	.ramp_down = 0,
};

static struct i2c_board_info __initdata stingray_i2c_bus4_power_info[] = {
	{
		I2C_BOARD_INFO("max8649", 0x60),
		.platform_data = &stingray_max8649_pdata,
	},
};

static struct mdm_ctrl_platform_data mdm_ctrl_platform_data = {
	.gpios[MDM_CTRL_GPIO_AP_STATUS_0] = {
		TEGRA_GPIO_PC1, MDM_GPIO_DIRECTION_OUT, 0, 0, "mdm_ap_status0"},
	.gpios[MDM_CTRL_GPIO_AP_STATUS_1] = {
		TEGRA_GPIO_PC6, MDM_GPIO_DIRECTION_OUT, 0, 0, "mdm_ap_status1"},
	.gpios[MDM_CTRL_GPIO_AP_STATUS_2] = {
		TEGRA_GPIO_PQ3, MDM_GPIO_DIRECTION_OUT, 0, 0, "mdm_ap_status2"},
	.gpios[MDM_CTRL_GPIO_BP_STATUS_0] = {
		TEGRA_GPIO_PK3, MDM_GPIO_DIRECTION_IN, 0, 0, "mdm_bp_status0"},
	.gpios[MDM_CTRL_GPIO_BP_STATUS_1] = {
		TEGRA_GPIO_PK4, MDM_GPIO_DIRECTION_IN, 0, 0, "mdm_bp_status1"},
	.gpios[MDM_CTRL_GPIO_BP_STATUS_2] = {
		TEGRA_GPIO_PK2, MDM_GPIO_DIRECTION_IN, 0, 0, "mdm_bp_status2"},
	.gpios[MDM_CTRL_GPIO_BP_RESOUT]   = {
		TEGRA_GPIO_PS4, MDM_GPIO_DIRECTION_IN, 0, 0, "mdm_bp_resout"},
	.gpios[MDM_CTRL_GPIO_BP_RESIN]    = {
		TEGRA_GPIO_PZ1, MDM_GPIO_DIRECTION_OUT, 0, 0, "mdm_bp_resin"},
	.gpios[MDM_CTRL_GPIO_BP_PWRON]    = {
		TEGRA_GPIO_PS6, MDM_GPIO_DIRECTION_OUT, 0, 0, "mdm_bp_pwr_on"},
	.cmd_gpios = {TEGRA_GPIO_PQ5, TEGRA_GPIO_PS5},
};

static struct platform_device mdm_ctrl_platform_device = {
	.name = MDM_CTRL_MODULE_NAME,
	.id = -1,
	.dev = {
		.platform_data = &mdm_ctrl_platform_data,
	},
};

int __init stingray_power_init(void)
{
	int i;
	unsigned long pmc_cntrl_0;
	int qbp_usb_hw_bypass_enabled = stingray_qbp_usb_hw_bypass_enabled();

	/* Enable CORE_PWR_REQ signal from T20. The signal must be enabled
	 * before the CPCAP uC firmware is started. */
	pmc_cntrl_0 = readl(IO_ADDRESS(TEGRA_PMC_BASE));
	pmc_cntrl_0 |= 0x00000200;
	writel(pmc_cntrl_0, IO_ADDRESS(TEGRA_PMC_BASE));

	if (stingray_revision() <= STINGRAY_REVISION_M1)
		stingray_max8649_pdata.mode = 3;

#ifdef ENABLE_SOFT_RESET_DEBUGGING
	/* Only P3 and later hardware supports CPCAP resetting the T20. */
	if (stingray_revision() >= STINGRAY_REVISION_P3)
		stingray_cpcap_data.hwcfg[1] |= CPCAP_HWCFG1_SOFT_RESET_HOST;
#endif

	tegra_gpio_enable(TEGRA_GPIO_PT2);
	gpio_request(TEGRA_GPIO_PT2, "usb_host_pwr_en");
	gpio_direction_output(TEGRA_GPIO_PT2, 0);

	spi_register_board_info(stingray_spi_board_info,
				ARRAY_SIZE(stingray_spi_board_info));

	for (i = 0; i < ARRAY_SIZE(cpcap_devices); i++)
		cpcap_device_register(cpcap_devices[i]);

	if (!qbp_usb_hw_bypass_enabled)
		cpcap_device_register(&cpcap_whisper_device);

	(void) cpcap_driver_register(&cpcap_validity_driver);

	i2c_register_board_info(3, stingray_i2c_bus4_power_info,
		ARRAY_SIZE(stingray_i2c_bus4_power_info));

	for (i = 0; i < MDM_CTRL_NUM_GPIOS; i++)
		tegra_gpio_enable(mdm_ctrl_platform_data.gpios[i].number);

	if (qbp_usb_hw_bypass_enabled) {
		/* The default AP status is "no bypass", so we must override it */
		mdm_ctrl_platform_data.gpios[MDM_CTRL_GPIO_AP_STATUS_0]. \
				default_value = 1;
		mdm_ctrl_platform_data.gpios[MDM_CTRL_GPIO_AP_STATUS_1]. \
				default_value = 0;
		mdm_ctrl_platform_data.gpios[MDM_CTRL_GPIO_AP_STATUS_2]. \
				default_value = 0;
	}

	platform_device_register(&mdm_ctrl_platform_device);

	return 0;
}

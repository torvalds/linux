// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for LattePanda Sigma EC.
 *
 * The LattePanda Sigma is an x86 SBC made by DFRobot with an ITE IT8613E
 * Embedded Controller that manages a CPU fan and thermal sensors.
 *
 * The BIOS declares the ACPI Embedded Controller (PNP0C09) with _STA
 * returning 0 and provides only stub ECRD/ECWT methods that return Zero
 * for all registers. Since the kernel's ACPI EC subsystem never initializes,
 * ec_read() is not available and direct port I/O to the standard ACPI EC
 * ports (0x62/0x66) is used instead.
 *
 * Because ACPI never initializes the EC, there is no concurrent firmware
 * access to these ports, and no ACPI Global Lock or namespace mutex is
 * required. The hwmon with_info API serializes all sysfs callbacks,
 * so no additional driver-level locking is needed.
 *
 * The EC register map was discovered by dumping all 256 registers,
 * identifying those that change in real-time, and validating by physically
 * stopping the fan and observing the RPM register drop to zero. The map
 * has been verified on BIOS version 5.27; other versions may differ.
 *
 * Copyright (c) 2026 Mariano Abad <weimaraner@gmail.com>
 */

#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define DRIVER_NAME	"lattepanda_sigma_ec"

/* EC I/O ports (standard ACPI EC interface) */
#define EC_DATA_PORT	0x62
#define EC_CMD_PORT	0x66	/* also status port */

/* EC commands */
#define EC_CMD_READ	0x80

/* EC status register bits */
#define EC_STATUS_OBF	0x01	/* Output Buffer Full */
#define EC_STATUS_IBF	0x02	/* Input Buffer Full */

/* EC register offsets for LattePanda Sigma (BIOS 5.27) */
#define EC_REG_FAN_RPM_HI	0x2E
#define EC_REG_FAN_RPM_LO	0x2F
#define EC_REG_TEMP_BOARD	0x60
#define EC_REG_TEMP_CPU		0x70
#define EC_REG_FAN_DUTY		0x93

/*
 * EC polling uses udelay() because the EC typically responds within a
 * few microseconds. The kernel's own ACPI EC driver (drivers/acpi/ec.c)
 * likewise uses udelay() for busy-polling with a per-poll delay of 550us.
 *
 * usleep_range() was tested but caused EC protocol failures: the EC
 * clears its status flags within microseconds, and sleeping for 50-100us
 * between polls allowed the flags to transition past the expected state.
 *
 * The worst-case total busy-wait of 25ms covers EC recovery after errors.
 * In practice the EC responds within 10us so the loop exits immediately.
 */
#define EC_TIMEOUT_US		25000
#define EC_POLL_US		1

static bool force;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force,
		 "Force loading on untested BIOS versions (default: false)");

static struct platform_device *lps_ec_pdev;

static int ec_wait_ibf_clear(void)
{
	int i;

	for (i = 0; i < EC_TIMEOUT_US; i++) {
		if (!(inb(EC_CMD_PORT) & EC_STATUS_IBF))
			return 0;
		udelay(EC_POLL_US);
	}
	return -ETIMEDOUT;
}

static int ec_wait_obf_set(void)
{
	int i;

	for (i = 0; i < EC_TIMEOUT_US; i++) {
		if (inb(EC_CMD_PORT) & EC_STATUS_OBF)
			return 0;
		udelay(EC_POLL_US);
	}
	return -ETIMEDOUT;
}

static int ec_read_reg(u8 reg, u8 *val)
{
	int ret;

	ret = ec_wait_ibf_clear();
	if (ret)
		return ret;

	outb(EC_CMD_READ, EC_CMD_PORT);

	ret = ec_wait_ibf_clear();
	if (ret)
		return ret;

	outb(reg, EC_DATA_PORT);

	ret = ec_wait_obf_set();
	if (ret)
		return ret;

	*val = inb(EC_DATA_PORT);
	return 0;
}

/*
 * Read a 16-bit big-endian value from two consecutive EC registers.
 *
 * The EC may update the register pair between reading the high and low
 * bytes, which could produce a corrupted value if the high byte rolls
 * over (e.g., 0x0100 -> 0x00FF read as 0x01FF). Guard against this by
 * re-reading the high byte after reading the low byte. If the high byte
 * changed, re-read the low byte to get a consistent pair.
 * See also lm90_read16() which uses the same approach.
 */
static int ec_read_reg16(u8 reg_hi, u8 reg_lo, u16 *val)
{
	int ret;
	u8 oldh, newh, lo;

	ret = ec_read_reg(reg_hi, &oldh);
	if (ret)
		return ret;

	ret = ec_read_reg(reg_lo, &lo);
	if (ret)
		return ret;

	ret = ec_read_reg(reg_hi, &newh);
	if (ret)
		return ret;

	if (oldh != newh) {
		ret = ec_read_reg(reg_lo, &lo);
		if (ret)
			return ret;
	}

	*val = ((u16)newh << 8) | lo;
	return 0;
}

static int
lps_ec_read_string(struct device *dev,
		   enum hwmon_sensor_types type,
		   u32 attr, int channel,
		   const char **str)
{
	switch (type) {
	case hwmon_fan:
		*str = "CPU Fan";
		return 0;
	case hwmon_temp:
		*str = channel == 0 ? "Board Temp" : "CPU Temp";
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
lps_ec_is_visible(const void *drvdata,
		  enum hwmon_sensor_types type,
		  u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:
		if (attr == hwmon_fan_input || attr == hwmon_fan_label)
			return 0444;
		break;
	case hwmon_temp:
		if (attr == hwmon_temp_input || attr == hwmon_temp_label)
			return 0444;
		break;
	default:
		break;
	}
	return 0;
}

static int
lps_ec_read(struct device *dev,
	    enum hwmon_sensor_types type,
	    u32 attr, int channel, long *val)
{
	u16 rpm;
	u8 v;
	int ret;

	switch (type) {
	case hwmon_fan:
		if (attr != hwmon_fan_input)
			return -EOPNOTSUPP;
		ret = ec_read_reg16(EC_REG_FAN_RPM_HI,
				    EC_REG_FAN_RPM_LO, &rpm);
		if (ret)
			return ret;
		*val = rpm;
		return 0;

	case hwmon_temp:
		if (attr != hwmon_temp_input)
			return -EOPNOTSUPP;
		ret = ec_read_reg(channel == 0 ? EC_REG_TEMP_BOARD
					       : EC_REG_TEMP_CPU,
				  &v);
		if (ret)
			return ret;
		/* EC reports unsigned 8-bit temperature in degrees Celsius */
		*val = (unsigned long)v * 1000;
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_channel_info * const lps_ec_info[] = {
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	NULL
};

static const struct hwmon_ops lps_ec_ops = {
	.is_visible = lps_ec_is_visible,
	.read = lps_ec_read,
	.read_string = lps_ec_read_string,
};

static const struct hwmon_chip_info lps_ec_chip_info = {
	.ops = &lps_ec_ops,
	.info = lps_ec_info,
};

static int lps_ec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *hwmon;
	u8 test;
	int ret;

	if (!devm_request_region(dev, EC_DATA_PORT, 1, DRIVER_NAME))
		return dev_err_probe(dev, -EBUSY,
				     "Failed to request EC data port 0x%x\n",
				     EC_DATA_PORT);

	if (!devm_request_region(dev, EC_CMD_PORT, 1, DRIVER_NAME))
		return dev_err_probe(dev, -EBUSY,
				     "Failed to request EC cmd port 0x%x\n",
				     EC_CMD_PORT);

	/* Sanity check: verify EC is responsive */
	ret = ec_read_reg(EC_REG_FAN_DUTY, &test);
	if (ret)
		return dev_err_probe(dev, ret,
				     "EC not responding on ports 0x%x/0x%x\n",
				     EC_DATA_PORT, EC_CMD_PORT);

	hwmon = devm_hwmon_device_register_with_info(dev, DRIVER_NAME, NULL,
						     &lps_ec_chip_info, NULL);
	if (IS_ERR(hwmon))
		return dev_err_probe(dev, PTR_ERR(hwmon),
				     "Failed to register hwmon device\n");

	dev_info(dev, "EC hwmon registered (fan duty: %u%%)\n", test);
	return 0;
}

/* DMI table with strict BIOS version match (override with force=1) */
static const struct dmi_system_id lps_ec_dmi_table[] = {
	{
		.ident = "LattePanda Sigma",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LattePanda"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LattePanda Sigma"),
			DMI_MATCH(DMI_BIOS_VERSION, "5.27"),
		},
	},
	{ }	/* terminator */
};
MODULE_DEVICE_TABLE(dmi, lps_ec_dmi_table);

/* Loose table (vendor + product only) for use with force=1 */
static const struct dmi_system_id lps_ec_dmi_table_force[] = {
	{
		.ident = "LattePanda Sigma",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LattePanda"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LattePanda Sigma"),
		},
	},
	{ }	/* terminator */
};

static struct platform_driver lps_ec_driver = {
	.probe	= lps_ec_probe,
	.driver	= {
		.name = DRIVER_NAME,
	},
};

static int __init lps_ec_init(void)
{
	int ret;

	if (!dmi_check_system(lps_ec_dmi_table)) {
		if (!force || !dmi_check_system(lps_ec_dmi_table_force))
			return -ENODEV;
		pr_warn("%s: BIOS version not verified, loading due to force=1\n",
			DRIVER_NAME);
	}

	ret = platform_driver_register(&lps_ec_driver);
	if (ret)
		return ret;

	lps_ec_pdev = platform_device_register_simple(DRIVER_NAME, -1,
						      NULL, 0);
	if (IS_ERR(lps_ec_pdev)) {
		platform_driver_unregister(&lps_ec_driver);
		return PTR_ERR(lps_ec_pdev);
	}

	return 0;
}

static void __exit lps_ec_exit(void)
{
	platform_device_unregister(lps_ec_pdev);
	platform_driver_unregister(&lps_ec_driver);
}

module_init(lps_ec_init);
module_exit(lps_ec_exit);

MODULE_AUTHOR("Mariano Abad <weimaraner@gmail.com>");
MODULE_DESCRIPTION("Hardware monitoring driver for LattePanda Sigma EC");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for Analog Devices MAX16545/MAX16550 and
 * Volterra VT7505 PMBus controllers.
 *
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pmbus.h>

#include "pmbus.h"

#define VT7505_MFR_CONFIG		0xd0
#define VT7505_CFG_OCP_S_FILT_MASK	GENMASK(15, 14)

#define VT7505_MFR_PEAK_VIN		0xd1
#define VT7505_MFR_PEAK_IOUT		0xd2
#define VT7505_MFR_PEAK_PIN		0xd3
#define VT7505_MFR_PEAK_TEMP		0xd4
#define VT7505_MFR_CLEAR_PEAKS		0xd5
#define VT7505_MFR_PEAK_VOUT		0xfd

#define VT7505_RLOAD_DEFAULT		4750
/* Largest RLOAD for which coeff * rload / 1000 still fits in s32. */
#define VT7505_RLOAD_MAX		283383959U

struct vt7505_chip_data {
	int temp_m;
	int temp_b;
	bool has_ocp_filter;
};

static const struct vt7505_chip_data max16545_data = {
	.temp_m = 205,
	.temp_b = 6545,
};

static const struct vt7505_chip_data max16550_data = {
	.temp_m = 199,
	.temp_b = 7046,
	.has_ocp_filter = true,
};

static const struct vt7505_chip_data vt7505_data = {
	.temp_m = 205,
	.temp_b = 6545,
	.has_ocp_filter = true,
};

static int vt7505_read_word_data(struct i2c_client *client, int page,
				 int phase, int reg)
{
	switch (reg) {
	case PMBUS_VIRT_READ_VIN_MAX:
		return pmbus_read_word_data(client, page, phase,
					    VT7505_MFR_PEAK_VIN);
	case PMBUS_VIRT_READ_IOUT_MAX:
		return pmbus_read_word_data(client, page, phase,
					    VT7505_MFR_PEAK_IOUT);
	case PMBUS_VIRT_READ_PIN_MAX:
		return pmbus_read_word_data(client, page, phase,
					    VT7505_MFR_PEAK_PIN);
	case PMBUS_VIRT_READ_TEMP_MAX:
		return pmbus_read_word_data(client, page, phase,
					    VT7505_MFR_PEAK_TEMP);
	case PMBUS_VIRT_READ_VOUT_MAX:
		return pmbus_read_word_data(client, page, phase,
					    VT7505_MFR_PEAK_VOUT);
	case PMBUS_VIRT_RESET_VIN_HISTORY:
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
	case PMBUS_VIRT_RESET_PIN_HISTORY:
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
		return 0;
	default:
		return -ENODATA;
	}
}

static int vt7505_write_word_data(struct i2c_client *client, int page,
				  int reg, u16 word)
{
	switch (reg) {
	/*
	 * A single reset command clears all peak values. CLEAR_PEAKS is a
	 * send-byte command; the device NAKs a word or byte-data write to it.
	 */
	case PMBUS_VIRT_RESET_VIN_HISTORY:
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
	case PMBUS_VIRT_RESET_PIN_HISTORY:
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
		return pmbus_write_byte(client, page,
					VT7505_MFR_CLEAR_PEAKS);
	default:
		return -ENODATA;
	}
}

/*
 * None of these controllers implement the standard PMBus WRITE_PROTECT
 * (0x10) register, so tell the core not to access it.
 */
static struct pmbus_platform_data vt7505_pdata = {
	.flags = PMBUS_NO_WRITE_PROTECT,
};

static int vt7505_set_ocp_filter(struct i2c_client *client)
{
	u32 ocp_us;
	u8 field;
	int ret;
	u16 word;

	if (of_property_read_u32(client->dev.of_node, "adi,ocp-severe-filter-us",
				 &ocp_us))
		return 0;

	switch (ocp_us) {
	case 0:
		field = 0;
		break;
	case 1:
		field = 1;
		break;
	case 2:
		field = 2;
		break;
	case 10:
		field = 3;
		break;
	default:
		return dev_err_probe(&client->dev, -EINVAL,
				     "invalid adi,ocp-severe-filter-us value %u\n",
				     ocp_us);
	}

	ret = i2c_smbus_read_word_data(client, VT7505_MFR_CONFIG);
	if (ret < 0)
		return dev_err_probe(&client->dev, ret,
				     "failed to read MFR_CONFIG\n");

	word = ret & ~VT7505_CFG_OCP_S_FILT_MASK;
	word |= FIELD_PREP(VT7505_CFG_OCP_S_FILT_MASK, field);

	ret = i2c_smbus_write_word_data(client, VT7505_MFR_CONFIG, word);
	if (ret < 0)
		return dev_err_probe(&client->dev, ret,
				     "failed to write MFR_CONFIG\n");

	return 0;
}

static void vt7505_set_m(int *m, u32 rload)
{
	u64 val = (u64)*m * rload;

	/* rload is range-checked in probe, so the result fits in int. */
	*m = DIV_ROUND_CLOSEST_ULL(val, 1000);
}

static int vt7505_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	const struct vt7505_chip_data *chip;
	struct pmbus_driver_info *info;
	u32 rload;
	int ret;

	chip = i2c_get_match_data(client);
	if (!chip)
		return -ENODEV;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	dev->platform_data = &vt7505_pdata;

	/*
	 * The m coefficient used in the direct-format current and power
	 * calculations depends on RLOAD, the external current-report resistor
	 * connected between the ILOAD pin and ground. Use the default value if
	 * none is specified.
	 */
	if (of_property_read_u32(dev->of_node, "adi,rload-ohms", &rload))
		rload = VT7505_RLOAD_DEFAULT;

	if (!rload || rload > VT7505_RLOAD_MAX)
		return dev_err_probe(dev, -EINVAL,
				     "adi,rload-ohms must be 1-%u\n",
				     VT7505_RLOAD_MAX);

	info->pages = 1;
	info->read_word_data = vt7505_read_word_data;
	info->write_word_data = vt7505_write_word_data;

	info->format[PSC_VOLTAGE_IN] = direct;
	info->format[PSC_VOLTAGE_OUT] = direct;
	info->format[PSC_CURRENT_IN] = direct;
	info->format[PSC_CURRENT_OUT] = direct;
	info->format[PSC_POWER] = direct;
	info->format[PSC_TEMPERATURE] = direct;

	/*
	 * Direct data format coefficients from the device datasheet ("PMBus
	 * Equation Parameters"). The current and power m coefficients scale
	 * with RLOAD; vt7505_set_m() applies the 1/1000 factor below, giving
	 * m = 3.824 * RLOAD for current and 0.895 * RLOAD for power. The
	 * temperature coefficients are chip specific (see the chip data).
	 */
	info->m[PSC_VOLTAGE_IN] = 7578;
	info->R[PSC_VOLTAGE_IN] = -2;
	info->m[PSC_VOLTAGE_OUT] = 7578;
	info->R[PSC_VOLTAGE_OUT] = -2;
	info->m[PSC_CURRENT_IN] = 3824;
	info->b[PSC_CURRENT_IN] = -4300;
	info->R[PSC_CURRENT_IN] = -3;
	info->m[PSC_CURRENT_OUT] = 3824;
	info->b[PSC_CURRENT_OUT] = -4300;
	info->R[PSC_CURRENT_OUT] = -3;
	info->m[PSC_POWER] = 895;
	info->b[PSC_POWER] = -9100;
	info->R[PSC_POWER] = -2;

	vt7505_set_m(&info->m[PSC_CURRENT_IN], rload);
	vt7505_set_m(&info->m[PSC_CURRENT_OUT], rload);
	vt7505_set_m(&info->m[PSC_POWER], rload);

	info->m[PSC_TEMPERATURE] = chip->temp_m;
	info->b[PSC_TEMPERATURE] = chip->temp_b;
	info->R[PSC_TEMPERATURE] = -2;

	info->func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT |
			PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
			PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
			PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP |
			PMBUS_HAVE_IIN | PMBUS_HAVE_PIN;

	/*
	 * The severe OCP deglitch filter is programmable on the MAX16550 and
	 * the VT7505, but fixed on the MAX16545.
	 */
	if (chip->has_ocp_filter) {
		ret = vt7505_set_ocp_filter(client);
		if (ret)
			return ret;
	}

	return pmbus_do_probe(client, info);
}

static const struct i2c_device_id vt7505_id[] = {
	{ .name = "max16545", .driver_data = (kernel_ulong_t)&max16545_data },
	{ .name = "max16550", .driver_data = (kernel_ulong_t)&max16550_data },
	{ .name = "vt7505", .driver_data = (kernel_ulong_t)&vt7505_data },
	{ }
};
MODULE_DEVICE_TABLE(i2c, vt7505_id);

static const struct of_device_id vt7505_of_match[] = {
	{ .compatible = "adi,max16545", .data = &max16545_data },
	{ .compatible = "adi,max16550", .data = &max16550_data },
	{ .compatible = "adi,vt7505", .data = &vt7505_data },
	{ }
};
MODULE_DEVICE_TABLE(of, vt7505_of_match);

static struct i2c_driver vt7505_driver = {
	.driver = {
		.name = "vt7505",
		.of_match_table = vt7505_of_match,
	},
	.probe = vt7505_probe,
	.id_table = vt7505_id,
};
module_i2c_driver(vt7505_driver);

MODULE_AUTHOR("Georgi Vlaev <gvlaev@juniper.net>");
MODULE_DESCRIPTION("PMBus driver for Analog Devices MAX16545/MAX16550 and Volterra VT7505");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");

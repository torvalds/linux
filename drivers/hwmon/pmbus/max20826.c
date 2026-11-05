// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hardware monitoring driver for Analog Devices MAX20826 PMBus device
 *
 * Copyright 2026 Analog Devices Inc.
 */
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/pmbus.h>
#include <linux/property.h>
#include <linux/regulator/driver.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#include "pmbus.h"

#define MAX20826_REG_RAIL_PHASE_CFG	0xB1
#define MAX20826_REG_CTRL_MISC		0xCB
#define MAX20826_OPL_EN_MSK		BIT(7)

#define MAX20826_REG_VOUT_RES		0xDB
#define MAX20826_VOUT_RES_MSK		BIT(7)

#define MAX20855B_REG_VOUT_VRM		0xD1
#define MAX20855B_REG_VOUT_VRM_MASK	BIT(4)

#define MAX20826_REG_C_MODEAB		0xDC
/* Byte 1 bits 7 and 6*/
#define MAX20826_C_MODEAB_MASK		GENMASK(15, 14)

#define MAX20826_REG_ADDR_MODE		0xEC
#define MAX20826_PAGE_MODE_MSK		BIT(7)
#define MAX20826_DIRECT_ADDR_MSK	GENMASK(6, 0)

#define MAX20826_REG_OVERRIDE		0xED
#define MAX20826_OVERRIDE_MASK		BIT(7)

#define MAX20826_REG_PHASE_DETECT	0xF3
#define MAX20826_REG_PHASE_READ		0xF4

#define MAX20826_REG_STATUS_MON		0xF9
#define MAX20826_PHASES_NUM_MASK	GENMASK(7, 3)

#define MAX20826_MAX_PAGES		2
#define MAX20826_MAX_PHASES		16
#define MAX20826_PHASES_PER_PAGE	8
#define MAX20826_INTF_PWMVID		1
#define MAX20826_INTF_AVSBUS		3

#define MAX20855B_PHASES_NUM_MASK	GENMASK(7, 4)
#define MAX20855B_MAX_PHASES		8
#define MAX20908_MAX_PHASES		8
#define MAX20912_MAX_PHASES		12
#define MAX20916_MAX_PHASES		16

struct max20826_chip_info {
	const char *vendor_bus_name;
	u8 max_phases;
	unsigned int phase_num_mask;
	u8 start_index_iin;
	u8 start_index_iout;
	bool is_reg_addr_mode_block;
	bool is_vout_direct;
	bool select_vrm;
	bool has_avsbus;
	bool has_opl;
	u8 (*count_phases)(const u8 *config, int page);
};

struct max20826 {
	const struct max20826_chip_info *chip_info;
	struct pmbus_driver_info info;
	struct i2c_client *client;
	/* RAIL-B direct mode */
	struct i2c_client *client_b;
	struct i2c_client *curr_client;
	struct gpio_desc *avren;
	struct gpio_desc *bvren;
	bool vendor_bus;
	bool high_speed[MAX20826_MAX_PAGES];
	bool on_off_ctrl[MAX20826_MAX_PAGES];
	bool opl_enabled[MAX20826_MAX_PAGES];
};

struct max20826_debugfs_entry {
	struct max20826 *st;
	u8 page;
};

static u8 __max20826_count_phases(const u8 *config, int page)
{
	if (page)
		return hweight8(config[4]);

	return hweight8(config[0]) + hweight8(config[1]) -
	       hweight8(config[4]);
}

static u8 __max20855b_count_phases(const u8 *config, int page)
{
	if (page)
		return hweight8(config[3] & 0x3F);

	return hweight8(config[0]) + hweight8(config[1] & 0x0F) -
	       hweight8(config[3] & 0x3F);
}

static u8 __max20908_count_phases(const u8 *config, int page)
{
	if (page)
		return hweight8(config[4]);

	return hweight8(config[0]) + hweight8(config[1] & 0xF0) -
	       hweight8(config[4]);
}

static u8 __max20912_count_phases(const u8 *config, int page)
{
	if (page)
		return hweight8(config[4]);

	return hweight8(config[0]) + hweight8(config[1] & 0xFC) -
	       hweight8(config[4]);
}

#define to_max20826(p)	container_of(p, struct max20826, info)

enum {
	RAIL_A,
	RAIL_B,
};

static const struct regulator_desc __maybe_unused max20826_reg_desc[] = {
	PMBUS_REGULATOR("vout", 0),
	PMBUS_REGULATOR("vout", 1),
};

static struct i2c_client *max20826_select_rail(struct max20826 *st,
					       int page, bool probing)
{
	int ret;

	/*
	 * If in direct mode and we want RAIL_B (page 1) just return client_b.
	 * Otherwise, set the proper page (if page mode) and return RAIL_A.
	 */
	if (st->client_b) {
		/* if 0xff just return the last client */
		if (page < 0)
			return st->curr_client;
		if (page)
			st->curr_client = st->client_b;
		else
			st->curr_client = st->client;

		return st->curr_client;
	}

	if (!probing)
		ret = pmbus_set_page(st->client, page, 0xff);
	else
		ret = i2c_smbus_write_byte_data(st->client, PMBUS_PAGE, page);
	if (ret < 0)
		return ERR_PTR(ret);

	return st->client;
}

static int max20826_rail_read_block_data(struct i2c_client *rail, u8 reg,
					 u8 *block, u8 min_len)
{
	int ret;

	ret = pmbus_read_smbus_i2c_block_data(rail, reg, block);
	if (ret < 0)
		return ret;
	if (ret < min_len)
		return -EIO;

	return ret;
}

static int __max20826_read_block_data(struct max20826 *st, int page, u8 reg,
				      u8 *block, u8 min_len)
{
	struct i2c_client *rail;

	rail = max20826_select_rail(st, page, false);
	if (IS_ERR(rail))
		return PTR_ERR(rail);

	return max20826_rail_read_block_data(rail, reg, block, min_len);
}

static int __max20826_read_byte_data(struct max20826 *st, int page, int reg)
{
	struct i2c_client *rail;

	rail = max20826_select_rail(st, page, false);
	if (IS_ERR(rail))
		return PTR_ERR(rail);

	return i2c_smbus_read_byte_data(rail, reg);
}

static int __max20826_write_byte_data(struct max20826 *st, int page, int reg,
				      u8 value)
{
	struct i2c_client *rail;

	rail = max20826_select_rail(st, page, false);
	if (IS_ERR(rail))
		return PTR_ERR(rail);

	return i2c_smbus_write_byte_data(rail, reg, value);
}

static int __max20826_read_word_data(struct max20826 *st, int page, int reg)
{
	struct i2c_client *rail;

	rail = max20826_select_rail(st, page, false);
	if (IS_ERR(rail))
		return PTR_ERR(rail);

	return i2c_smbus_read_word_data(rail, reg);
}

/*
 * @rail is the already selected rail client and is only given during probe,
 * where the PMBus core cannot be used yet. Otherwise, pass NULL and let the
 * core dispatch through our own read_byte_data()/write_byte_data() callbacks,
 * which take care of selecting the rail.
 */
static int max20826_set_high_speed(struct max20826 *st, struct i2c_client *rail,
				   u8 page, bool high_speed)
{
	u8 reg, mask, val;
	int ret;

	if (st->vendor_bus) {
		reg = MAX20826_REG_OVERRIDE;
		mask = MAX20826_OVERRIDE_MASK;
		val = high_speed ? 0 : FIELD_PREP(MAX20826_OVERRIDE_MASK, 1);
	} else {
		reg = PMBUS_OPERATION;
		mask = PB_OPERATION_CONTROL_V_SRC;
		val = high_speed ? FIELD_PREP(PB_OPERATION_CONTROL_V_SRC, MAX20826_INTF_AVSBUS) : 0;
	}

	if (!rail) {
		ret = pmbus_update_byte_data(st->client, page, reg, mask, val);
	} else {
		ret = i2c_smbus_read_byte_data(rail, reg);
		if (ret < 0)
			return ret;

		ret = i2c_smbus_write_byte_data(rail, reg, (ret & ~mask) | val);
	}

	if (ret)
		return ret;

	st->high_speed[page] = high_speed;

	return 0;
}

static int max20826_read_curr(struct max20826 *st, int page, int phase, int reg)
{
	u8 val_out[I2C_SMBUS_BLOCK_MAX], start_byte;
	struct i2c_client *rail;
	int ret;

	if (phase == 0xff)
		return __max20826_read_word_data(st, page, reg);

	/*
	 * On Rail_A phases are ascending (from 1) while on RAIL_B they
	 * are descending (from 16).
	 */
	if (!page)
		phase += 1;
	else
		phase = st->chip_info->max_phases - phase;

	/* phase detect and read are only available on RAIL_A */
	rail = max20826_select_rail(st, RAIL_A, false);
	if (IS_ERR(rail))
		return PTR_ERR(rail);

	ret = i2c_smbus_write_byte_data(rail, MAX20826_REG_PHASE_DETECT, phase);
	if (ret < 0)
		return ret;

	/*
	 * For MAX20826: Byte 2:3 is for phase IOUT 4:5 is for phase IIN.
	 * For other chips: Byte 0:1 is for phase IOUT 2:3 is for phase IIN.
	 */
	if (reg == PMBUS_READ_IIN)
		start_byte = st->chip_info->start_index_iin;
	else
		start_byte = st->chip_info->start_index_iout;

	ret = max20826_rail_read_block_data(rail, MAX20826_REG_PHASE_READ,
					    val_out, start_byte + 2);
	if (ret < 0)
		return ret;

	return get_unaligned_le16(&val_out[start_byte]);
}

static int max20826_read_word_data(struct i2c_client *client, int page,
				   int phase, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct max20826 *st = to_max20826(info);

	switch (reg) {
	case PMBUS_READ_IIN:
	case PMBUS_READ_IOUT:
		return max20826_read_curr(st, page, phase, reg);
	case PMBUS_IOUT_OC_FAULT_LIMIT:
		if (!st->chip_info->has_opl || !st->opl_enabled[page])
			return __max20826_read_word_data(st, page, reg);
		return -EIO;
	case PMBUS_POUT_OP_FAULT_LIMIT:
		/*
		 * If Over Power Limit is enabled, PMBUS_IOUT_OC_FAULT_LIMIT
		 * shows the power limit and hence we need to report it
		 * properly in Watts.
		 */
		if (!st->chip_info->has_opl)
			return __max20826_read_word_data(st, page, reg);

		if (st->opl_enabled[page])
			return __max20826_read_word_data(st, page,
							 PMBUS_IOUT_OC_FAULT_LIMIT);
		return -EIO;
	default:
		if (reg >= PMBUS_VIRT_BASE)
			return -EOPNOTSUPP;
		return __max20826_read_word_data(st, page, reg);
	}
}

static int max20826_write_word_data(struct i2c_client *client, int page,
				    int reg, u16 word)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct max20826 *st = to_max20826(info);
	struct i2c_client *rail;

	rail = max20826_select_rail(st, page, false);
	if (IS_ERR(rail))
		return PTR_ERR(rail);

	return i2c_smbus_write_word_data(rail, reg, word);
}

static int max20826_regulator_do_enable(struct max20826 *st,
					struct i2c_client *rail, u8 page,
					u8 byte)
{
	/*
	 * If AVSBus is enabled (bits 5 and 4 set) the device refuses to set bit
	 * 7 of the OPERATION register. Hence, to workaround this, we first
	 * clear the bits and then set them all together.
	 */
	if (st->chip_info->has_avsbus && !st->vendor_bus && st->high_speed[page] &&
	    PB_OPERATION_CONTROL_ON & byte) {
		u8 __byte = byte & ~(PB_OPERATION_CONTROL_V_SRC | PB_OPERATION_CONTROL_ON);
		int ret;

		ret = i2c_smbus_write_byte_data(rail, PMBUS_OPERATION, __byte);
		if (ret < 0)
			return ret;
	}

	return i2c_smbus_write_byte_data(rail, PMBUS_OPERATION, byte);
}

static int max20826_regulator_enable(struct max20826 *st, int page, u8 byte)
{
	struct i2c_client *rail;

	rail = max20826_select_rail(st, page, false);
	if (IS_ERR(rail))
		return PTR_ERR(rail);

	if (page)
		gpiod_set_value_cansleep(st->bvren, !!(PB_OPERATION_CONTROL_ON & byte));
	else
		gpiod_set_value_cansleep(st->avren, !!(PB_OPERATION_CONTROL_ON & byte));

	return max20826_regulator_do_enable(st, rail, page, byte);
}

static int max20826_write_byte_data(struct i2c_client *client, int page,
				    int reg, u8 byte)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct max20826 *st = to_max20826(info);

	switch (reg) {
	case PMBUS_OPERATION:
		return max20826_regulator_enable(st, page, byte);
	default:
		return __max20826_write_byte_data(st, page, reg, byte);
	}
}

static int max20826_write_byte(struct i2c_client *client, int page, u8 byte)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct max20826 *st = to_max20826(info);
	struct i2c_client *rail;

	rail = max20826_select_rail(st, page, false);
	if (IS_ERR(rail))
		return PTR_ERR(rail);

	return i2c_smbus_write_byte(rail, byte);
}

static int max20826_regulator_enabled(struct max20826 *st, int page)
{
	struct gpio_desc *gpio = page ? st->bvren : st->avren;
	int on, ret;

	if (gpio) {
		on = gpiod_get_value_cansleep(gpio);
		if (on < 0)
			return on;
	} else {
		/* If the gpios are not given, just assume it's on */
		on = 1;
	}

	ret = __max20826_read_byte_data(st, page, PMBUS_OPERATION);
	if (ret < 0)
		return ret;

	if (st->on_off_ctrl[page])
		on = (PB_OPERATION_CONTROL_ON & ret) && on;

	ret &= ~PB_OPERATION_CONTROL_ON;
	return ret | FIELD_PREP(PB_OPERATION_CONTROL_ON, on);
}

static int max20826_iout_status(struct max20826 *st, int page)
{
	int status;

	status = __max20826_read_byte_data(st, page, PMBUS_STATUS_IOUT);
	if (status < 0 || !st->chip_info->has_opl)
		return status;

	/*
	 * If Over power limit is on, the fault condition is still set on the OC
	 * bit
	 */
	if (st->opl_enabled[page] && (status & PB_IOUT_OC_FAULT))
		return status | PB_POUT_OP_FAULT;

	return status;
}

static int max20826_read_byte_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct max20826 *st = to_max20826(info);

	switch (reg) {
	case PMBUS_OPERATION:
		return max20826_regulator_enabled(st, page);
	case PMBUS_STATUS_IOUT:
		return max20826_iout_status(st, page);
	default:
		return __max20826_read_byte_data(st, page, reg);
	}
}

static int max20826_read_block_data(struct i2c_client *client, int page, u8 reg,
				    char *data_buf)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct max20826 *st = to_max20826(info);

	switch (reg) {
	case PMBUS_MFR_ID:
	case PMBUS_MFR_MODEL:
		return __max20826_read_block_data(st, page, reg, data_buf, 16);
	case PMBUS_MFR_REVISION:
		return __max20826_read_block_data(st, page, reg, data_buf, 2);
	case PMBUS_MFR_DATE:
		return __max20826_read_block_data(st, page, reg, data_buf, 8);
	default:
		return -EOPNOTSUPP;
	}
}

static struct pmbus_driver_info max20826_default_info = {
	.pages = 1,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		   PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		   PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP |
		   PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_PIN | PMBUS_HAVE_POUT |
		   PMBUS_PHASE_VIRTUAL,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.m[PSC_VOLTAGE_OUT] = 2,
	.R[PSC_VOLTAGE_OUT] = 3,
	.write_byte_data = max20826_write_byte_data,
	.write_byte = max20826_write_byte,
	.read_byte_data = max20826_read_byte_data,
	.read_word_data = max20826_read_word_data,
	.write_word_data = max20826_write_word_data,
	.read_block_data = max20826_read_block_data,
#if IS_ENABLED(CONFIG_SENSORS_MAX20826_REGULATOR)
	.num_regulators = 1,
	.reg_desc = max20826_reg_desc,
#endif
};

static int max20826_detect_addr_mode(struct max20826 *st)
{
	u8 val_buf[I2C_SMBUS_BLOCK_MAX];
	struct device *dev = &st->client->dev;
	int ret, val;

	/*
	 * The idea is that after a POR, we are in page0 in which case we can
	 * read MAX20826_REG_ADDR_MODE. If we fail to read
	 * MAX20826_REG_ADDR_MODE, it might be due to a soft reset or unbinding
	 * the device in which case the device could be left in page 1. Hence,
	 * let's just try to change to page 0 and error out if we can't.
	 *
	 * OTOH, if the addressing mode is direct, we should be able to
	 * read MAX20826_REG_ADDR_MODE. If not, we'll fail setting the page.
	 *
	 * REG_ADDR_MODE, MAX20855B has 6 bytes, other devices have 1. Byte 3 is
	 * the equivalent of the single byte of the others.
	 */
	if (st->chip_info->is_reg_addr_mode_block)
		val = max20826_rail_read_block_data(st->client,
						    MAX20826_REG_ADDR_MODE,
						    val_buf, 3);
	else
		val = i2c_smbus_read_byte_data(st->client, MAX20826_REG_ADDR_MODE);

	if (val < 0) {
		ret = i2c_smbus_write_byte_data(st->client, PMBUS_PAGE, 0);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "Failed to change to page 0\n");

		/* try again! */
		if (st->chip_info->is_reg_addr_mode_block)
			val = max20826_rail_read_block_data(st->client,
							    MAX20826_REG_ADDR_MODE,
							    val_buf, 3);
		else
			val = i2c_smbus_read_byte_data(st->client,
						       MAX20826_REG_ADDR_MODE);
		if (val < 0)
			return dev_err_probe(dev, val,
					     "Failed to read MAX20826_REG_ADDR_MODE\n");
	}

	if (st->chip_info->is_reg_addr_mode_block)
		val = val_buf[2];

	if (val & MAX20826_PAGE_MODE_MSK)
		return 0;

	/*
	 * If in direct mode, rail b will be accessible from the addr of RAIL_A
	 * +1
	 */
	st->client_b = devm_i2c_new_dummy_device(dev, st->client->adapter,
						 st->client->addr + 1);
	if (IS_ERR(st->client_b))
		return PTR_ERR(st->client_b);

	return 0;
}

static int max20826_set_vout_interface(struct max20826 *st,
				       struct i2c_client *rail, u8 page)
{
	static const char * const props[MAX20826_MAX_PAGES] = {
		"adi,rail-a-high-speed",
		"adi,rail-b-high-speed",
	};
	bool high_speed;
	int ret;

	/*
	 * Rail A 0xDC_1[7:6] sets the operating mode for both rails, so only look
	 * at it once.
	 */
	if (page == RAIL_A) {
		if (st->chip_info->has_avsbus) {
			ret = i2c_smbus_read_word_data(st->client,
						       MAX20826_REG_C_MODEAB);
			if (ret < 0)
				return ret;

			if (FIELD_GET(MAX20826_C_MODEAB_MASK, ret) == MAX20826_INTF_PWMVID)
				st->vendor_bus = true;
		} else {
			st->vendor_bus = true;
		}
	}

	high_speed = device_property_read_bool(&st->client->dev, props[page]);

	return max20826_set_high_speed(st, rail, page, high_speed);
}

static int max20826_detect_phases(struct max20826 *st, struct i2c_client *rail,
				  struct pmbus_driver_info *info, u8 page,
				  u8 expected)
{
	u8 status_mon[I2C_SMBUS_BLOCK_MAX];
	int ret, ret2 = 0, oper_save = -1;
	u8 n_phases;

	/*
	 * In order to detect the number of phases, we need to be regulating.
	 * It is also assumed we're already in the page we want to detect the
	 * phases.
	 *
	 * If the vendor high speed interface controls the output voltage, the
	 * enable state is not ours to control (it is given by that interface
	 * together with the AVREN/BVREN pins) and writing PMBUS_OPERATION is a
	 * no-op, so just assume the rail is already regulating.
	 */
	if (!st->vendor_bus || !st->high_speed[page]) {
		ret = i2c_smbus_read_byte_data(rail, PMBUS_OPERATION);
		if (ret < 0)
			return ret;

		if (!(ret & PB_OPERATION_CONTROL_ON)) {
			oper_save = ret;
			ret = max20826_regulator_do_enable(st, rail, page,
							   ret | PB_OPERATION_CONTROL_ON);
			if (ret < 0)
				return ret;
		}
	}

	ret = max20826_rail_read_block_data(rail, MAX20826_REG_STATUS_MON,
					    status_mon, 2);
	if (ret < 0)
		goto out_restore_oper;

	n_phases = field_get(st->chip_info->phase_num_mask, status_mon[1]);
	if (n_phases != expected) {
		ret = dev_err_probe(&st->client->dev, -EIO,
				    "Number of phases mismatch: expected=%u, detected=%u\n",
				    expected, n_phases);
		goto out_restore_oper;
	}

	info->phases[page] = n_phases;
	for (unsigned int phase = 0; phase < n_phases; phase++)
		info->pfunc[phase] = PMBUS_HAVE_IOUT | PMBUS_HAVE_IIN;

out_restore_oper:
	if (oper_save >= 0)
		ret2 = max20826_regulator_do_enable(st, rail, page, oper_save);

	return ret < 0 ? ret : ret2;
}

static int max20826_get_rail_config(struct max20826 *st,
				    struct i2c_client *rail, u8 page)
{
	u8 ctrl_misc[I2C_SMBUS_BLOCK_MAX];
	int ret;

	/*
	 * Check if we need to control PMBUS_OPERATION in addition to the
	 * CONTROL pin.
	 */
	ret = i2c_smbus_read_byte_data(rail, PMBUS_ON_OFF_CONFIG);
	if (ret < 0)
		return ret;

	st->on_off_ctrl[page] = !!(ret & PB_ON_OFF_CONFIG_OPERATION_REQ);

	/*
	 * See if Over Power Limit is enabled. This will impact how
	 * OC_FAULT_LIMIT is handled.
	 */
	if (st->chip_info->has_opl) {
		ret = max20826_rail_read_block_data(rail, MAX20826_REG_CTRL_MISC,
						    ctrl_misc, 1);
		if (ret < 0)
			return ret;

		st->opl_enabled[page] = !!(ctrl_misc[0] & MAX20826_OPL_EN_MSK);
	}

	return 0;
}

static int max20826_setup_vout_format(struct max20826 *st)
{
	struct pmbus_driver_info *info = &st->info;
	struct i2c_client *rail;
	int ret;

	if (st->chip_info->is_vout_direct) {
		u8 vout_res[I2C_SMBUS_BLOCK_MAX];

		rail = max20826_select_rail(st, RAIL_A, true);
		if (IS_ERR(rail))
			return PTR_ERR(rail);

		ret = max20826_rail_read_block_data(rail, MAX20826_REG_VOUT_RES,
						    vout_res, 2);
		if (ret < 0)
			return ret;

		/* check for 1 mv/LSB */
		if (MAX20826_VOUT_RES_MSK & vout_res[1])
			info->m[PSC_VOLTAGE_OUT] = 1;

		return 0;
	}

	st->info.format[PSC_VOLTAGE_OUT] = vid;
	for (unsigned int page = 0; page < info->pages; page++) {
		if (!st->chip_info->select_vrm) {
			/* only vr12 in this case */
			st->info.vrm_version[page] = vr12;
			continue;
		}

		rail = max20826_select_rail(st, page, true);
		if (IS_ERR(rail))
			return PTR_ERR(rail);

		ret = i2c_smbus_read_byte_data(rail, MAX20855B_REG_VOUT_VRM);
		if (ret < 0)
			return ret;

		if (ret & MAX20855B_REG_VOUT_VRM_MASK)
			st->info.vrm_version[page] = vr13;
		else
			st->info.vrm_version[page] = vr12;
	}

	return 0;
}

static int max20826_setup(struct max20826 *st)
{
	struct pmbus_driver_info *info = &st->info;
	u8 config[I2C_SMBUS_BLOCK_MAX], expected_phases;
	struct device *dev = &st->client->dev;
	struct i2c_client *rail;
	int ret;

	ret = max20826_detect_addr_mode(st);
	if (ret < 0)
		return ret;

	ret = max20826_set_vout_interface(st, st->client, RAIL_A);
	if (ret < 0)
		return ret;

	ret = max20826_rail_read_block_data(st->client,
					    MAX20826_REG_RAIL_PHASE_CFG,
					    config, 5);
	if (ret < 0)
		return ret;

	/*
	 * If we have phases in RAIL_B, we need to subtract them on config[1] as
	 * those bits are also set in case RAIL_B has phases.
	 */
	expected_phases = st->chip_info->count_phases(config, RAIL_A);
	ret = max20826_detect_phases(st, st->client, info, RAIL_A,
				     expected_phases);
	if (ret < 0)
		return ret;

	ret = max20826_get_rail_config(st, st->client, RAIL_A);
	if (ret < 0)
		return ret;

	/* Let's see if RAIL_B is present */
	rail = max20826_select_rail(st, RAIL_B, true);
	if (IS_ERR(rail))
		return PTR_ERR(rail);

	/* Let's see if there's something on RAIL_B */
	st->bvren = devm_gpiod_get_optional(dev, "bvren", GPIOD_OUT_HIGH);
	if (IS_ERR(st->bvren))
		return PTR_ERR(st->bvren);

	ret = max20826_set_vout_interface(st, rail, RAIL_B);
	if (ret < 0)
		return ret;

	expected_phases = st->chip_info->count_phases(config, RAIL_B);
	ret = max20826_detect_phases(st, rail, info, RAIL_B, expected_phases);
	if (ret < 0)
		return ret;

	if (info->phases[RAIL_B]) {
		info->pages = MAX20826_MAX_PAGES;
		info->func[RAIL_B] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
				     PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
				     PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP |
				     PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_PIN | PMBUS_HAVE_POUT |
				     PMBUS_PHASE_VIRTUAL;

		if (IS_ENABLED(CONFIG_SENSORS_MAX20826_REGULATOR))
			info->num_regulators = 2;

		ret = max20826_get_rail_config(st, rail, RAIL_B);
		if (ret < 0)
			return ret;
	}

	ret = max20826_setup_vout_format(st);
	if (ret < 0)
		return ret;

	if (info->pages == 1) {
		/*
		 * In this case we can be left in RAIL_B and given that we on
		 * only have one page, pmbus_set_page() will refuse to comeback
		 * to RAIL_A which is obviously problematic. Hence, let's just
		 * select RAIL_A here.
		 */
		rail = max20826_select_rail(st, RAIL_A, true);
		if (IS_ERR(rail))
			return PTR_ERR(rail);
	}

	return 0;
}

static int max20826_high_speed_en_get(void *arg, u64 *val)
{
	const struct max20826_debugfs_entry *entry = arg;

	*val = entry->st->high_speed[entry->page];

	return 0;
}

static int max20826_high_speed_en_set(void *arg, u64 val)
{
	const struct max20826_debugfs_entry *entry = arg;
	struct max20826 *st = entry->st;

	guard(pmbus_lock)(st->client);

	return max20826_set_high_speed(st, NULL, entry->page, !!val);
}
DEFINE_DEBUGFS_ATTRIBUTE(max20826_high_speed_en_fops,
			 max20826_high_speed_en_get,
			 max20826_high_speed_en_set, "%llu\n");

static int max20826_high_speed_bus_show(struct seq_file *s, void *data)
{
	struct device *dev = s->private;
	struct i2c_client *client = to_i2c_client(dev);
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct max20826 *st = to_max20826(info);

	seq_printf(s, "%s\n", st->vendor_bus ? st->chip_info->vendor_bus_name : "AVSBus");

	return 0;
}

static void max20826_init_debugfs(struct max20826 *st)
{
	struct device *dev = &st->client->dev;
	struct dentry *dir;

	dir = pmbus_get_debugfs_dir(st->client);
	if (!dir)
		return;

	for (unsigned int page = 0; page < st->info.pages; page++) {
		struct max20826_debugfs_entry *entry;
		char name[32];

		entry = devm_kzalloc(dev, sizeof(*entry), GFP_KERNEL);
		if (!entry)
			return;

		entry->st = st;
		entry->page = page;

		scnprintf(name, sizeof(name), "in%u_high_speed_en", page + 2);
		debugfs_create_file_unsafe(name, 0644, dir, entry,
					   &max20826_high_speed_en_fops);
	}

	debugfs_create_devm_seqfile(dev, "in_high_speed_bus", dir,
				    max20826_high_speed_bus_show);
}

static int max20826_probe(struct i2c_client *client)
{
	struct max20826 *st;
	int ret;

	st = devm_kzalloc(&client->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->client = client;
	memcpy(&st->info, &max20826_default_info, sizeof(st->info));

	st->chip_info = i2c_get_match_data(client);
	if (!st->chip_info)
		return -EINVAL;

	st->avren = devm_gpiod_get_optional(&client->dev, "avren", GPIOD_OUT_HIGH);
	if (IS_ERR(st->avren))
		return PTR_ERR(st->avren);

	ret = max20826_setup(st);
	if (ret)
		return ret;

	ret = pmbus_do_probe(client, &st->info);
	if (ret)
		return ret;

	max20826_init_debugfs(st);

	return 0;
}

static const struct max20826_chip_info chip_info_max20826 = {
	.vendor_bus_name = "Nvidia PWMVID",
	.max_phases = MAX20826_MAX_PHASES,
	.phase_num_mask = MAX20826_PHASES_NUM_MASK,
	.start_index_iin = 4,
	.start_index_iout = 2,
	.is_vout_direct = true,
	.has_avsbus = true,
	.has_opl = true,
	.count_phases = __max20826_count_phases,
};

static const struct max20826_chip_info chip_info_max20855b = {
	.vendor_bus_name = "Intel SVID",
	.max_phases = MAX20855B_MAX_PHASES,
	.phase_num_mask = MAX20855B_PHASES_NUM_MASK,
	.start_index_iin = 2,
	.start_index_iout = 0,
	.is_reg_addr_mode_block = true,
	.select_vrm = true,
	.count_phases = __max20855b_count_phases,
};

static const struct max20826_chip_info chip_info_max20908 = {
	.vendor_bus_name = "AMD SVI3",
	.max_phases = MAX20908_MAX_PHASES,
	.phase_num_mask = MAX20826_PHASES_NUM_MASK,
	.start_index_iin = 2,
	.start_index_iout = 0,
	.count_phases = __max20908_count_phases,
};

static const struct max20826_chip_info chip_info_max20912 = {
	.vendor_bus_name = "AMD SVI3",
	.max_phases = MAX20912_MAX_PHASES,
	.phase_num_mask = MAX20826_PHASES_NUM_MASK,
	.start_index_iin = 2,
	.start_index_iout = 0,
	.count_phases = __max20912_count_phases,
};

static const struct max20826_chip_info chip_info_max20916 = {
	.vendor_bus_name = "AMD SVI3",
	.max_phases = MAX20916_MAX_PHASES,
	.phase_num_mask = MAX20826_PHASES_NUM_MASK,
	.start_index_iin = 2,
	.start_index_iout = 0,
	.count_phases = __max20826_count_phases,
};

static const struct i2c_device_id max20826_id[] = {
	{ "max20826", (kernel_ulong_t)&chip_info_max20826 },
	{ "max20855b", (kernel_ulong_t)&chip_info_max20855b },
	{ "max20908", (kernel_ulong_t)&chip_info_max20908 },
	{ "max20912", (kernel_ulong_t)&chip_info_max20912 },
	{ "max20916", (kernel_ulong_t)&chip_info_max20916 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max20826_id);

static const struct of_device_id max20826_of_match[] = {
	{ .compatible = "adi,max20826", .data = &chip_info_max20826 },
	{ .compatible = "adi,max20855b", .data = &chip_info_max20855b },
	{ .compatible = "adi,max20908", .data = &chip_info_max20908 },
	{ .compatible = "adi,max20912", .data = &chip_info_max20912 },
	{ .compatible = "adi,max20916", .data = &chip_info_max20916 },
	{ }
};
MODULE_DEVICE_TABLE(of, max20826_of_match);

static struct i2c_driver max20826_driver = {
	.driver = {
		.name = "max20826",
		.of_match_table = max20826_of_match,
	},
	.probe = max20826_probe,
	.id_table = max20826_id,
};
module_i2c_driver(max20826_driver);

MODULE_AUTHOR("Nuno Sá <nuno.sa@analog.com>");
MODULE_DESCRIPTION("PMBus driver for MAX20826");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");

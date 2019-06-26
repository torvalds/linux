// SPDX-License-Identifier: GPL-2.0
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "mdio-i2c.h"
#include "sfp.h"
#include "swphy.h"

enum {
	GPIO_MODDEF0,
	GPIO_LOS,
	GPIO_TX_FAULT,
	GPIO_TX_DISABLE,
	GPIO_RATE_SELECT,
	GPIO_MAX,

	SFP_F_PRESENT = BIT(GPIO_MODDEF0),
	SFP_F_LOS = BIT(GPIO_LOS),
	SFP_F_TX_FAULT = BIT(GPIO_TX_FAULT),
	SFP_F_TX_DISABLE = BIT(GPIO_TX_DISABLE),
	SFP_F_RATE_SELECT = BIT(GPIO_RATE_SELECT),

	SFP_E_INSERT = 0,
	SFP_E_REMOVE,
	SFP_E_DEV_DOWN,
	SFP_E_DEV_UP,
	SFP_E_TX_FAULT,
	SFP_E_TX_CLEAR,
	SFP_E_LOS_HIGH,
	SFP_E_LOS_LOW,
	SFP_E_TIMEOUT,

	SFP_MOD_EMPTY = 0,
	SFP_MOD_PROBE,
	SFP_MOD_HPOWER,
	SFP_MOD_PRESENT,
	SFP_MOD_ERROR,

	SFP_DEV_DOWN = 0,
	SFP_DEV_UP,

	SFP_S_DOWN = 0,
	SFP_S_INIT,
	SFP_S_WAIT_LOS,
	SFP_S_LINK_UP,
	SFP_S_TX_FAULT,
	SFP_S_REINIT,
	SFP_S_TX_DISABLE,
};

static const char  * const mod_state_strings[] = {
	[SFP_MOD_EMPTY] = "empty",
	[SFP_MOD_PROBE] = "probe",
	[SFP_MOD_HPOWER] = "hpower",
	[SFP_MOD_PRESENT] = "present",
	[SFP_MOD_ERROR] = "error",
};

static const char *mod_state_to_str(unsigned short mod_state)
{
	if (mod_state >= ARRAY_SIZE(mod_state_strings))
		return "Unknown module state";
	return mod_state_strings[mod_state];
}

static const char * const dev_state_strings[] = {
	[SFP_DEV_DOWN] = "down",
	[SFP_DEV_UP] = "up",
};

static const char *dev_state_to_str(unsigned short dev_state)
{
	if (dev_state >= ARRAY_SIZE(dev_state_strings))
		return "Unknown device state";
	return dev_state_strings[dev_state];
}

static const char * const event_strings[] = {
	[SFP_E_INSERT] = "insert",
	[SFP_E_REMOVE] = "remove",
	[SFP_E_DEV_DOWN] = "dev_down",
	[SFP_E_DEV_UP] = "dev_up",
	[SFP_E_TX_FAULT] = "tx_fault",
	[SFP_E_TX_CLEAR] = "tx_clear",
	[SFP_E_LOS_HIGH] = "los_high",
	[SFP_E_LOS_LOW] = "los_low",
	[SFP_E_TIMEOUT] = "timeout",
};

static const char *event_to_str(unsigned short event)
{
	if (event >= ARRAY_SIZE(event_strings))
		return "Unknown event";
	return event_strings[event];
}

static const char * const sm_state_strings[] = {
	[SFP_S_DOWN] = "down",
	[SFP_S_INIT] = "init",
	[SFP_S_WAIT_LOS] = "wait_los",
	[SFP_S_LINK_UP] = "link_up",
	[SFP_S_TX_FAULT] = "tx_fault",
	[SFP_S_REINIT] = "reinit",
	[SFP_S_TX_DISABLE] = "rx_disable",
};

static const char *sm_state_to_str(unsigned short sm_state)
{
	if (sm_state >= ARRAY_SIZE(sm_state_strings))
		return "Unknown state";
	return sm_state_strings[sm_state];
}

static const char *gpio_of_names[] = {
	"mod-def0",
	"los",
	"tx-fault",
	"tx-disable",
	"rate-select0",
};

static const enum gpiod_flags gpio_flags[] = {
	GPIOD_IN,
	GPIOD_IN,
	GPIOD_IN,
	GPIOD_ASIS,
	GPIOD_ASIS,
};

#define T_INIT_JIFFIES	msecs_to_jiffies(300)
#define T_RESET_US	10
#define T_FAULT_RECOVER	msecs_to_jiffies(1000)

/* SFP module presence detection is poor: the three MOD DEF signals are
 * the same length on the PCB, which means it's possible for MOD DEF 0 to
 * connect before the I2C bus on MOD DEF 1/2.
 *
 * The SFP MSA specifies 300ms as t_init (the time taken for TX_FAULT to
 * be deasserted) but makes no mention of the earliest time before we can
 * access the I2C EEPROM.  However, Avago modules require 300ms.
 */
#define T_PROBE_INIT	msecs_to_jiffies(300)
#define T_HPOWER_LEVEL	msecs_to_jiffies(300)
#define T_PROBE_RETRY	msecs_to_jiffies(100)

/* SFP modules appear to always have their PHY configured for bus address
 * 0x56 (which with mdio-i2c, translates to a PHY address of 22).
 */
#define SFP_PHY_ADDR	22

/* Give this long for the PHY to reset. */
#define T_PHY_RESET_MS	50

struct sff_data {
	unsigned int gpios;
	bool (*module_supported)(const struct sfp_eeprom_id *id);
};

struct sfp {
	struct device *dev;
	struct i2c_adapter *i2c;
	struct mii_bus *i2c_mii;
	struct sfp_bus *sfp_bus;
	struct phy_device *mod_phy;
	const struct sff_data *type;
	u32 max_power_mW;

	unsigned int (*get_state)(struct sfp *);
	void (*set_state)(struct sfp *, unsigned int);
	int (*read)(struct sfp *, bool, u8, void *, size_t);
	int (*write)(struct sfp *, bool, u8, void *, size_t);

	struct gpio_desc *gpio[GPIO_MAX];

	bool attached;
	unsigned int state;
	struct delayed_work poll;
	struct delayed_work timeout;
	struct mutex sm_mutex;
	unsigned char sm_mod_state;
	unsigned char sm_dev_state;
	unsigned short sm_state;
	unsigned int sm_retries;

	struct sfp_eeprom_id id;
#if IS_ENABLED(CONFIG_HWMON)
	struct sfp_diag diag;
	struct device *hwmon_dev;
	char *hwmon_name;
#endif

};

static bool sff_module_supported(const struct sfp_eeprom_id *id)
{
	return id->base.phys_id == SFP_PHYS_ID_SFF &&
	       id->base.phys_ext_id == SFP_PHYS_EXT_ID_SFP;
}

static const struct sff_data sff_data = {
	.gpios = SFP_F_LOS | SFP_F_TX_FAULT | SFP_F_TX_DISABLE,
	.module_supported = sff_module_supported,
};

static bool sfp_module_supported(const struct sfp_eeprom_id *id)
{
	return id->base.phys_id == SFP_PHYS_ID_SFP &&
	       id->base.phys_ext_id == SFP_PHYS_EXT_ID_SFP;
}

static const struct sff_data sfp_data = {
	.gpios = SFP_F_PRESENT | SFP_F_LOS | SFP_F_TX_FAULT |
		 SFP_F_TX_DISABLE | SFP_F_RATE_SELECT,
	.module_supported = sfp_module_supported,
};

static const struct of_device_id sfp_of_match[] = {
	{ .compatible = "sff,sff", .data = &sff_data, },
	{ .compatible = "sff,sfp", .data = &sfp_data, },
	{ },
};
MODULE_DEVICE_TABLE(of, sfp_of_match);

static unsigned long poll_jiffies;

static unsigned int sfp_gpio_get_state(struct sfp *sfp)
{
	unsigned int i, state, v;

	for (i = state = 0; i < GPIO_MAX; i++) {
		if (gpio_flags[i] != GPIOD_IN || !sfp->gpio[i])
			continue;

		v = gpiod_get_value_cansleep(sfp->gpio[i]);
		if (v)
			state |= BIT(i);
	}

	return state;
}

static unsigned int sff_gpio_get_state(struct sfp *sfp)
{
	return sfp_gpio_get_state(sfp) | SFP_F_PRESENT;
}

static void sfp_gpio_set_state(struct sfp *sfp, unsigned int state)
{
	if (state & SFP_F_PRESENT) {
		/* If the module is present, drive the signals */
		if (sfp->gpio[GPIO_TX_DISABLE])
			gpiod_direction_output(sfp->gpio[GPIO_TX_DISABLE],
					       state & SFP_F_TX_DISABLE);
		if (state & SFP_F_RATE_SELECT)
			gpiod_direction_output(sfp->gpio[GPIO_RATE_SELECT],
					       state & SFP_F_RATE_SELECT);
	} else {
		/* Otherwise, let them float to the pull-ups */
		if (sfp->gpio[GPIO_TX_DISABLE])
			gpiod_direction_input(sfp->gpio[GPIO_TX_DISABLE]);
		if (state & SFP_F_RATE_SELECT)
			gpiod_direction_input(sfp->gpio[GPIO_RATE_SELECT]);
	}
}

static int sfp_i2c_read(struct sfp *sfp, bool a2, u8 dev_addr, void *buf,
			size_t len)
{
	struct i2c_msg msgs[2];
	u8 bus_addr = a2 ? 0x51 : 0x50;
	size_t this_len;
	int ret;

	msgs[0].addr = bus_addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &dev_addr;
	msgs[1].addr = bus_addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = buf;

	while (len) {
		this_len = len;
		if (this_len > 16)
			this_len = 16;

		msgs[1].len = this_len;

		ret = i2c_transfer(sfp->i2c, msgs, ARRAY_SIZE(msgs));
		if (ret < 0)
			return ret;

		if (ret != ARRAY_SIZE(msgs))
			break;

		msgs[1].buf += this_len;
		dev_addr += this_len;
		len -= this_len;
	}

	return msgs[1].buf - (u8 *)buf;
}

static int sfp_i2c_write(struct sfp *sfp, bool a2, u8 dev_addr, void *buf,
	size_t len)
{
	struct i2c_msg msgs[1];
	u8 bus_addr = a2 ? 0x51 : 0x50;
	int ret;

	msgs[0].addr = bus_addr;
	msgs[0].flags = 0;
	msgs[0].len = 1 + len;
	msgs[0].buf = kmalloc(1 + len, GFP_KERNEL);
	if (!msgs[0].buf)
		return -ENOMEM;

	msgs[0].buf[0] = dev_addr;
	memcpy(&msgs[0].buf[1], buf, len);

	ret = i2c_transfer(sfp->i2c, msgs, ARRAY_SIZE(msgs));

	kfree(msgs[0].buf);

	if (ret < 0)
		return ret;

	return ret == ARRAY_SIZE(msgs) ? len : 0;
}

static int sfp_i2c_configure(struct sfp *sfp, struct i2c_adapter *i2c)
{
	struct mii_bus *i2c_mii;
	int ret;

	if (!i2c_check_functionality(i2c, I2C_FUNC_I2C))
		return -EINVAL;

	sfp->i2c = i2c;
	sfp->read = sfp_i2c_read;
	sfp->write = sfp_i2c_write;

	i2c_mii = mdio_i2c_alloc(sfp->dev, i2c);
	if (IS_ERR(i2c_mii))
		return PTR_ERR(i2c_mii);

	i2c_mii->name = "SFP I2C Bus";
	i2c_mii->phy_mask = ~0;

	ret = mdiobus_register(i2c_mii);
	if (ret < 0) {
		mdiobus_free(i2c_mii);
		return ret;
	}

	sfp->i2c_mii = i2c_mii;

	return 0;
}

/* Interface */
static unsigned int sfp_get_state(struct sfp *sfp)
{
	return sfp->get_state(sfp);
}

static void sfp_set_state(struct sfp *sfp, unsigned int state)
{
	sfp->set_state(sfp, state);
}

static int sfp_read(struct sfp *sfp, bool a2, u8 addr, void *buf, size_t len)
{
	return sfp->read(sfp, a2, addr, buf, len);
}

static int sfp_write(struct sfp *sfp, bool a2, u8 addr, void *buf, size_t len)
{
	return sfp->write(sfp, a2, addr, buf, len);
}

static unsigned int sfp_check(void *buf, size_t len)
{
	u8 *p, check;

	for (p = buf, check = 0; len; p++, len--)
		check += *p;

	return check;
}

/* hwmon */
#if IS_ENABLED(CONFIG_HWMON)
static umode_t sfp_hwmon_is_visible(const void *data,
				    enum hwmon_sensor_types type,
				    u32 attr, int channel)
{
	const struct sfp *sfp = data;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_min_alarm:
		case hwmon_temp_max_alarm:
		case hwmon_temp_lcrit_alarm:
		case hwmon_temp_crit_alarm:
		case hwmon_temp_min:
		case hwmon_temp_max:
		case hwmon_temp_lcrit:
		case hwmon_temp_crit:
			if (!(sfp->id.ext.enhopts & SFP_ENHOPTS_ALARMWARN))
				return 0;
			/* fall through */
		case hwmon_temp_input:
			return 0444;
		default:
			return 0;
		}
	case hwmon_in:
		switch (attr) {
		case hwmon_in_min_alarm:
		case hwmon_in_max_alarm:
		case hwmon_in_lcrit_alarm:
		case hwmon_in_crit_alarm:
		case hwmon_in_min:
		case hwmon_in_max:
		case hwmon_in_lcrit:
		case hwmon_in_crit:
			if (!(sfp->id.ext.enhopts & SFP_ENHOPTS_ALARMWARN))
				return 0;
			/* fall through */
		case hwmon_in_input:
			return 0444;
		default:
			return 0;
		}
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_min_alarm:
		case hwmon_curr_max_alarm:
		case hwmon_curr_lcrit_alarm:
		case hwmon_curr_crit_alarm:
		case hwmon_curr_min:
		case hwmon_curr_max:
		case hwmon_curr_lcrit:
		case hwmon_curr_crit:
			if (!(sfp->id.ext.enhopts & SFP_ENHOPTS_ALARMWARN))
				return 0;
			/* fall through */
		case hwmon_curr_input:
			return 0444;
		default:
			return 0;
		}
	case hwmon_power:
		/* External calibration of receive power requires
		 * floating point arithmetic. Doing that in the kernel
		 * is not easy, so just skip it. If the module does
		 * not require external calibration, we can however
		 * show receiver power, since FP is then not needed.
		 */
		if (sfp->id.ext.diagmon & SFP_DIAGMON_EXT_CAL &&
		    channel == 1)
			return 0;
		switch (attr) {
		case hwmon_power_min_alarm:
		case hwmon_power_max_alarm:
		case hwmon_power_lcrit_alarm:
		case hwmon_power_crit_alarm:
		case hwmon_power_min:
		case hwmon_power_max:
		case hwmon_power_lcrit:
		case hwmon_power_crit:
			if (!(sfp->id.ext.enhopts & SFP_ENHOPTS_ALARMWARN))
				return 0;
			/* fall through */
		case hwmon_power_input:
			return 0444;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

static int sfp_hwmon_read_sensor(struct sfp *sfp, int reg, long *value)
{
	__be16 val;
	int err;

	err = sfp_read(sfp, true, reg, &val, sizeof(val));
	if (err < 0)
		return err;

	*value = be16_to_cpu(val);

	return 0;
}

static void sfp_hwmon_to_rx_power(long *value)
{
	*value = DIV_ROUND_CLOSEST(*value, 100);
}

static void sfp_hwmon_calibrate(struct sfp *sfp, unsigned int slope, int offset,
				long *value)
{
	if (sfp->id.ext.diagmon & SFP_DIAGMON_EXT_CAL)
		*value = DIV_ROUND_CLOSEST(*value * slope, 256) + offset;
}

static void sfp_hwmon_calibrate_temp(struct sfp *sfp, long *value)
{
	sfp_hwmon_calibrate(sfp, be16_to_cpu(sfp->diag.cal_t_slope),
			    be16_to_cpu(sfp->diag.cal_t_offset), value);

	if (*value >= 0x8000)
		*value -= 0x10000;

	*value = DIV_ROUND_CLOSEST(*value * 1000, 256);
}

static void sfp_hwmon_calibrate_vcc(struct sfp *sfp, long *value)
{
	sfp_hwmon_calibrate(sfp, be16_to_cpu(sfp->diag.cal_v_slope),
			    be16_to_cpu(sfp->diag.cal_v_offset), value);

	*value = DIV_ROUND_CLOSEST(*value, 10);
}

static void sfp_hwmon_calibrate_bias(struct sfp *sfp, long *value)
{
	sfp_hwmon_calibrate(sfp, be16_to_cpu(sfp->diag.cal_txi_slope),
			    be16_to_cpu(sfp->diag.cal_txi_offset), value);

	*value = DIV_ROUND_CLOSEST(*value, 500);
}

static void sfp_hwmon_calibrate_tx_power(struct sfp *sfp, long *value)
{
	sfp_hwmon_calibrate(sfp, be16_to_cpu(sfp->diag.cal_txpwr_slope),
			    be16_to_cpu(sfp->diag.cal_txpwr_offset), value);

	*value = DIV_ROUND_CLOSEST(*value, 10);
}

static int sfp_hwmon_read_temp(struct sfp *sfp, int reg, long *value)
{
	int err;

	err = sfp_hwmon_read_sensor(sfp, reg, value);
	if (err < 0)
		return err;

	sfp_hwmon_calibrate_temp(sfp, value);

	return 0;
}

static int sfp_hwmon_read_vcc(struct sfp *sfp, int reg, long *value)
{
	int err;

	err = sfp_hwmon_read_sensor(sfp, reg, value);
	if (err < 0)
		return err;

	sfp_hwmon_calibrate_vcc(sfp, value);

	return 0;
}

static int sfp_hwmon_read_bias(struct sfp *sfp, int reg, long *value)
{
	int err;

	err = sfp_hwmon_read_sensor(sfp, reg, value);
	if (err < 0)
		return err;

	sfp_hwmon_calibrate_bias(sfp, value);

	return 0;
}

static int sfp_hwmon_read_tx_power(struct sfp *sfp, int reg, long *value)
{
	int err;

	err = sfp_hwmon_read_sensor(sfp, reg, value);
	if (err < 0)
		return err;

	sfp_hwmon_calibrate_tx_power(sfp, value);

	return 0;
}

static int sfp_hwmon_read_rx_power(struct sfp *sfp, int reg, long *value)
{
	int err;

	err = sfp_hwmon_read_sensor(sfp, reg, value);
	if (err < 0)
		return err;

	sfp_hwmon_to_rx_power(value);

	return 0;
}

static int sfp_hwmon_temp(struct sfp *sfp, u32 attr, long *value)
{
	u8 status;
	int err;

	switch (attr) {
	case hwmon_temp_input:
		return sfp_hwmon_read_temp(sfp, SFP_TEMP, value);

	case hwmon_temp_lcrit:
		*value = be16_to_cpu(sfp->diag.temp_low_alarm);
		sfp_hwmon_calibrate_temp(sfp, value);
		return 0;

	case hwmon_temp_min:
		*value = be16_to_cpu(sfp->diag.temp_low_warn);
		sfp_hwmon_calibrate_temp(sfp, value);
		return 0;
	case hwmon_temp_max:
		*value = be16_to_cpu(sfp->diag.temp_high_warn);
		sfp_hwmon_calibrate_temp(sfp, value);
		return 0;

	case hwmon_temp_crit:
		*value = be16_to_cpu(sfp->diag.temp_high_alarm);
		sfp_hwmon_calibrate_temp(sfp, value);
		return 0;

	case hwmon_temp_lcrit_alarm:
		err = sfp_read(sfp, true, SFP_ALARM0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_ALARM0_TEMP_LOW);
		return 0;

	case hwmon_temp_min_alarm:
		err = sfp_read(sfp, true, SFP_WARN0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_WARN0_TEMP_LOW);
		return 0;

	case hwmon_temp_max_alarm:
		err = sfp_read(sfp, true, SFP_WARN0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_WARN0_TEMP_HIGH);
		return 0;

	case hwmon_temp_crit_alarm:
		err = sfp_read(sfp, true, SFP_ALARM0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_ALARM0_TEMP_HIGH);
		return 0;
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static int sfp_hwmon_vcc(struct sfp *sfp, u32 attr, long *value)
{
	u8 status;
	int err;

	switch (attr) {
	case hwmon_in_input:
		return sfp_hwmon_read_vcc(sfp, SFP_VCC, value);

	case hwmon_in_lcrit:
		*value = be16_to_cpu(sfp->diag.volt_low_alarm);
		sfp_hwmon_calibrate_vcc(sfp, value);
		return 0;

	case hwmon_in_min:
		*value = be16_to_cpu(sfp->diag.volt_low_warn);
		sfp_hwmon_calibrate_vcc(sfp, value);
		return 0;

	case hwmon_in_max:
		*value = be16_to_cpu(sfp->diag.volt_high_warn);
		sfp_hwmon_calibrate_vcc(sfp, value);
		return 0;

	case hwmon_in_crit:
		*value = be16_to_cpu(sfp->diag.volt_high_alarm);
		sfp_hwmon_calibrate_vcc(sfp, value);
		return 0;

	case hwmon_in_lcrit_alarm:
		err = sfp_read(sfp, true, SFP_ALARM0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_ALARM0_VCC_LOW);
		return 0;

	case hwmon_in_min_alarm:
		err = sfp_read(sfp, true, SFP_WARN0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_WARN0_VCC_LOW);
		return 0;

	case hwmon_in_max_alarm:
		err = sfp_read(sfp, true, SFP_WARN0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_WARN0_VCC_HIGH);
		return 0;

	case hwmon_in_crit_alarm:
		err = sfp_read(sfp, true, SFP_ALARM0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_ALARM0_VCC_HIGH);
		return 0;
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static int sfp_hwmon_bias(struct sfp *sfp, u32 attr, long *value)
{
	u8 status;
	int err;

	switch (attr) {
	case hwmon_curr_input:
		return sfp_hwmon_read_bias(sfp, SFP_TX_BIAS, value);

	case hwmon_curr_lcrit:
		*value = be16_to_cpu(sfp->diag.bias_low_alarm);
		sfp_hwmon_calibrate_bias(sfp, value);
		return 0;

	case hwmon_curr_min:
		*value = be16_to_cpu(sfp->diag.bias_low_warn);
		sfp_hwmon_calibrate_bias(sfp, value);
		return 0;

	case hwmon_curr_max:
		*value = be16_to_cpu(sfp->diag.bias_high_warn);
		sfp_hwmon_calibrate_bias(sfp, value);
		return 0;

	case hwmon_curr_crit:
		*value = be16_to_cpu(sfp->diag.bias_high_alarm);
		sfp_hwmon_calibrate_bias(sfp, value);
		return 0;

	case hwmon_curr_lcrit_alarm:
		err = sfp_read(sfp, true, SFP_ALARM0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_ALARM0_TX_BIAS_LOW);
		return 0;

	case hwmon_curr_min_alarm:
		err = sfp_read(sfp, true, SFP_WARN0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_WARN0_TX_BIAS_LOW);
		return 0;

	case hwmon_curr_max_alarm:
		err = sfp_read(sfp, true, SFP_WARN0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_WARN0_TX_BIAS_HIGH);
		return 0;

	case hwmon_curr_crit_alarm:
		err = sfp_read(sfp, true, SFP_ALARM0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_ALARM0_TX_BIAS_HIGH);
		return 0;
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static int sfp_hwmon_tx_power(struct sfp *sfp, u32 attr, long *value)
{
	u8 status;
	int err;

	switch (attr) {
	case hwmon_power_input:
		return sfp_hwmon_read_tx_power(sfp, SFP_TX_POWER, value);

	case hwmon_power_lcrit:
		*value = be16_to_cpu(sfp->diag.txpwr_low_alarm);
		sfp_hwmon_calibrate_tx_power(sfp, value);
		return 0;

	case hwmon_power_min:
		*value = be16_to_cpu(sfp->diag.txpwr_low_warn);
		sfp_hwmon_calibrate_tx_power(sfp, value);
		return 0;

	case hwmon_power_max:
		*value = be16_to_cpu(sfp->diag.txpwr_high_warn);
		sfp_hwmon_calibrate_tx_power(sfp, value);
		return 0;

	case hwmon_power_crit:
		*value = be16_to_cpu(sfp->diag.txpwr_high_alarm);
		sfp_hwmon_calibrate_tx_power(sfp, value);
		return 0;

	case hwmon_power_lcrit_alarm:
		err = sfp_read(sfp, true, SFP_ALARM0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_ALARM0_TXPWR_LOW);
		return 0;

	case hwmon_power_min_alarm:
		err = sfp_read(sfp, true, SFP_WARN0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_WARN0_TXPWR_LOW);
		return 0;

	case hwmon_power_max_alarm:
		err = sfp_read(sfp, true, SFP_WARN0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_WARN0_TXPWR_HIGH);
		return 0;

	case hwmon_power_crit_alarm:
		err = sfp_read(sfp, true, SFP_ALARM0, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_ALARM0_TXPWR_HIGH);
		return 0;
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static int sfp_hwmon_rx_power(struct sfp *sfp, u32 attr, long *value)
{
	u8 status;
	int err;

	switch (attr) {
	case hwmon_power_input:
		return sfp_hwmon_read_rx_power(sfp, SFP_RX_POWER, value);

	case hwmon_power_lcrit:
		*value = be16_to_cpu(sfp->diag.rxpwr_low_alarm);
		sfp_hwmon_to_rx_power(value);
		return 0;

	case hwmon_power_min:
		*value = be16_to_cpu(sfp->diag.rxpwr_low_warn);
		sfp_hwmon_to_rx_power(value);
		return 0;

	case hwmon_power_max:
		*value = be16_to_cpu(sfp->diag.rxpwr_high_warn);
		sfp_hwmon_to_rx_power(value);
		return 0;

	case hwmon_power_crit:
		*value = be16_to_cpu(sfp->diag.rxpwr_high_alarm);
		sfp_hwmon_to_rx_power(value);
		return 0;

	case hwmon_power_lcrit_alarm:
		err = sfp_read(sfp, true, SFP_ALARM1, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_ALARM1_RXPWR_LOW);
		return 0;

	case hwmon_power_min_alarm:
		err = sfp_read(sfp, true, SFP_WARN1, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_WARN1_RXPWR_LOW);
		return 0;

	case hwmon_power_max_alarm:
		err = sfp_read(sfp, true, SFP_WARN1, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_WARN1_RXPWR_HIGH);
		return 0;

	case hwmon_power_crit_alarm:
		err = sfp_read(sfp, true, SFP_ALARM1, &status, sizeof(status));
		if (err < 0)
			return err;

		*value = !!(status & SFP_ALARM1_RXPWR_HIGH);
		return 0;
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static int sfp_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long *value)
{
	struct sfp *sfp = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		return sfp_hwmon_temp(sfp, attr, value);
	case hwmon_in:
		return sfp_hwmon_vcc(sfp, attr, value);
	case hwmon_curr:
		return sfp_hwmon_bias(sfp, attr, value);
	case hwmon_power:
		switch (channel) {
		case 0:
			return sfp_hwmon_tx_power(sfp, attr, value);
		case 1:
			return sfp_hwmon_rx_power(sfp, attr, value);
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops sfp_hwmon_ops = {
	.is_visible = sfp_hwmon_is_visible,
	.read = sfp_hwmon_read,
};

static u32 sfp_hwmon_chip_config[] = {
	HWMON_C_REGISTER_TZ,
	0,
};

static const struct hwmon_channel_info sfp_hwmon_chip = {
	.type = hwmon_chip,
	.config = sfp_hwmon_chip_config,
};

static u32 sfp_hwmon_temp_config[] = {
	HWMON_T_INPUT |
	HWMON_T_MAX | HWMON_T_MIN |
	HWMON_T_MAX_ALARM | HWMON_T_MIN_ALARM |
	HWMON_T_CRIT | HWMON_T_LCRIT |
	HWMON_T_CRIT_ALARM | HWMON_T_LCRIT_ALARM,
	0,
};

static const struct hwmon_channel_info sfp_hwmon_temp_channel_info = {
	.type = hwmon_temp,
	.config = sfp_hwmon_temp_config,
};

static u32 sfp_hwmon_vcc_config[] = {
	HWMON_I_INPUT |
	HWMON_I_MAX | HWMON_I_MIN |
	HWMON_I_MAX_ALARM | HWMON_I_MIN_ALARM |
	HWMON_I_CRIT | HWMON_I_LCRIT |
	HWMON_I_CRIT_ALARM | HWMON_I_LCRIT_ALARM,
	0,
};

static const struct hwmon_channel_info sfp_hwmon_vcc_channel_info = {
	.type = hwmon_in,
	.config = sfp_hwmon_vcc_config,
};

static u32 sfp_hwmon_bias_config[] = {
	HWMON_C_INPUT |
	HWMON_C_MAX | HWMON_C_MIN |
	HWMON_C_MAX_ALARM | HWMON_C_MIN_ALARM |
	HWMON_C_CRIT | HWMON_C_LCRIT |
	HWMON_C_CRIT_ALARM | HWMON_C_LCRIT_ALARM,
	0,
};

static const struct hwmon_channel_info sfp_hwmon_bias_channel_info = {
	.type = hwmon_curr,
	.config = sfp_hwmon_bias_config,
};

static u32 sfp_hwmon_power_config[] = {
	/* Transmit power */
	HWMON_P_INPUT |
	HWMON_P_MAX | HWMON_P_MIN |
	HWMON_P_MAX_ALARM | HWMON_P_MIN_ALARM |
	HWMON_P_CRIT | HWMON_P_LCRIT |
	HWMON_P_CRIT_ALARM | HWMON_P_LCRIT_ALARM,
	/* Receive power */
	HWMON_P_INPUT |
	HWMON_P_MAX | HWMON_P_MIN |
	HWMON_P_MAX_ALARM | HWMON_P_MIN_ALARM |
	HWMON_P_CRIT | HWMON_P_LCRIT |
	HWMON_P_CRIT_ALARM | HWMON_P_LCRIT_ALARM,
	0,
};

static const struct hwmon_channel_info sfp_hwmon_power_channel_info = {
	.type = hwmon_power,
	.config = sfp_hwmon_power_config,
};

static const struct hwmon_channel_info *sfp_hwmon_info[] = {
	&sfp_hwmon_chip,
	&sfp_hwmon_vcc_channel_info,
	&sfp_hwmon_temp_channel_info,
	&sfp_hwmon_bias_channel_info,
	&sfp_hwmon_power_channel_info,
	NULL,
};

static const struct hwmon_chip_info sfp_hwmon_chip_info = {
	.ops = &sfp_hwmon_ops,
	.info = sfp_hwmon_info,
};

static int sfp_hwmon_insert(struct sfp *sfp)
{
	int err, i;

	if (sfp->id.ext.sff8472_compliance == SFP_SFF8472_COMPLIANCE_NONE)
		return 0;

	if (!(sfp->id.ext.diagmon & SFP_DIAGMON_DDM))
		return 0;

	if (sfp->id.ext.diagmon & SFP_DIAGMON_ADDRMODE)
		/* This driver in general does not support address
		 * change.
		 */
		return 0;

	err = sfp_read(sfp, true, 0, &sfp->diag, sizeof(sfp->diag));
	if (err < 0)
		return err;

	sfp->hwmon_name = kstrdup(dev_name(sfp->dev), GFP_KERNEL);
	if (!sfp->hwmon_name)
		return -ENODEV;

	for (i = 0; sfp->hwmon_name[i]; i++)
		if (hwmon_is_bad_char(sfp->hwmon_name[i]))
			sfp->hwmon_name[i] = '_';

	sfp->hwmon_dev = hwmon_device_register_with_info(sfp->dev,
							 sfp->hwmon_name, sfp,
							 &sfp_hwmon_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(sfp->hwmon_dev);
}

static void sfp_hwmon_remove(struct sfp *sfp)
{
	if (!IS_ERR_OR_NULL(sfp->hwmon_dev)) {
		hwmon_device_unregister(sfp->hwmon_dev);
		sfp->hwmon_dev = NULL;
		kfree(sfp->hwmon_name);
	}
}
#else
static int sfp_hwmon_insert(struct sfp *sfp)
{
	return 0;
}

static void sfp_hwmon_remove(struct sfp *sfp)
{
}
#endif

/* Helpers */
static void sfp_module_tx_disable(struct sfp *sfp)
{
	dev_dbg(sfp->dev, "tx disable %u -> %u\n",
		sfp->state & SFP_F_TX_DISABLE ? 1 : 0, 1);
	sfp->state |= SFP_F_TX_DISABLE;
	sfp_set_state(sfp, sfp->state);
}

static void sfp_module_tx_enable(struct sfp *sfp)
{
	dev_dbg(sfp->dev, "tx disable %u -> %u\n",
		sfp->state & SFP_F_TX_DISABLE ? 1 : 0, 0);
	sfp->state &= ~SFP_F_TX_DISABLE;
	sfp_set_state(sfp, sfp->state);
}

static void sfp_module_tx_fault_reset(struct sfp *sfp)
{
	unsigned int state = sfp->state;

	if (state & SFP_F_TX_DISABLE)
		return;

	sfp_set_state(sfp, state | SFP_F_TX_DISABLE);

	udelay(T_RESET_US);

	sfp_set_state(sfp, state);
}

/* SFP state machine */
static void sfp_sm_set_timer(struct sfp *sfp, unsigned int timeout)
{
	if (timeout)
		mod_delayed_work(system_power_efficient_wq, &sfp->timeout,
				 timeout);
	else
		cancel_delayed_work(&sfp->timeout);
}

static void sfp_sm_next(struct sfp *sfp, unsigned int state,
			unsigned int timeout)
{
	sfp->sm_state = state;
	sfp_sm_set_timer(sfp, timeout);
}

static void sfp_sm_ins_next(struct sfp *sfp, unsigned int state,
			    unsigned int timeout)
{
	sfp->sm_mod_state = state;
	sfp_sm_set_timer(sfp, timeout);
}

static void sfp_sm_phy_detach(struct sfp *sfp)
{
	phy_stop(sfp->mod_phy);
	sfp_remove_phy(sfp->sfp_bus);
	phy_device_remove(sfp->mod_phy);
	phy_device_free(sfp->mod_phy);
	sfp->mod_phy = NULL;
}

static void sfp_sm_probe_phy(struct sfp *sfp)
{
	struct phy_device *phy;
	int err;

	msleep(T_PHY_RESET_MS);

	phy = mdiobus_scan(sfp->i2c_mii, SFP_PHY_ADDR);
	if (phy == ERR_PTR(-ENODEV)) {
		dev_info(sfp->dev, "no PHY detected\n");
		return;
	}
	if (IS_ERR(phy)) {
		dev_err(sfp->dev, "mdiobus scan returned %ld\n", PTR_ERR(phy));
		return;
	}

	err = sfp_add_phy(sfp->sfp_bus, phy);
	if (err) {
		phy_device_remove(phy);
		phy_device_free(phy);
		dev_err(sfp->dev, "sfp_add_phy failed: %d\n", err);
		return;
	}

	sfp->mod_phy = phy;
	phy_start(phy);
}

static void sfp_sm_link_up(struct sfp *sfp)
{
	sfp_link_up(sfp->sfp_bus);
	sfp_sm_next(sfp, SFP_S_LINK_UP, 0);
}

static void sfp_sm_link_down(struct sfp *sfp)
{
	sfp_link_down(sfp->sfp_bus);
}

static void sfp_sm_link_check_los(struct sfp *sfp)
{
	unsigned int los = sfp->state & SFP_F_LOS;

	/* If neither SFP_OPTIONS_LOS_INVERTED nor SFP_OPTIONS_LOS_NORMAL
	 * are set, we assume that no LOS signal is available.
	 */
	if (sfp->id.ext.options & cpu_to_be16(SFP_OPTIONS_LOS_INVERTED))
		los ^= SFP_F_LOS;
	else if (!(sfp->id.ext.options & cpu_to_be16(SFP_OPTIONS_LOS_NORMAL)))
		los = 0;

	if (los)
		sfp_sm_next(sfp, SFP_S_WAIT_LOS, 0);
	else
		sfp_sm_link_up(sfp);
}

static bool sfp_los_event_active(struct sfp *sfp, unsigned int event)
{
	return (sfp->id.ext.options & cpu_to_be16(SFP_OPTIONS_LOS_INVERTED) &&
		event == SFP_E_LOS_LOW) ||
	       (sfp->id.ext.options & cpu_to_be16(SFP_OPTIONS_LOS_NORMAL) &&
		event == SFP_E_LOS_HIGH);
}

static bool sfp_los_event_inactive(struct sfp *sfp, unsigned int event)
{
	return (sfp->id.ext.options & cpu_to_be16(SFP_OPTIONS_LOS_INVERTED) &&
		event == SFP_E_LOS_HIGH) ||
	       (sfp->id.ext.options & cpu_to_be16(SFP_OPTIONS_LOS_NORMAL) &&
		event == SFP_E_LOS_LOW);
}

static void sfp_sm_fault(struct sfp *sfp, bool warn)
{
	if (sfp->sm_retries && !--sfp->sm_retries) {
		dev_err(sfp->dev,
			"module persistently indicates fault, disabling\n");
		sfp_sm_next(sfp, SFP_S_TX_DISABLE, 0);
	} else {
		if (warn)
			dev_err(sfp->dev, "module transmit fault indicated\n");

		sfp_sm_next(sfp, SFP_S_TX_FAULT, T_FAULT_RECOVER);
	}
}

static void sfp_sm_mod_init(struct sfp *sfp)
{
	sfp_module_tx_enable(sfp);

	/* Wait t_init before indicating that the link is up, provided the
	 * current state indicates no TX_FAULT.  If TX_FAULT clears before
	 * this time, that's fine too.
	 */
	sfp_sm_next(sfp, SFP_S_INIT, T_INIT_JIFFIES);
	sfp->sm_retries = 5;

	/* Setting the serdes link mode is guesswork: there's no
	 * field in the EEPROM which indicates what mode should
	 * be used.
	 *
	 * If it's a gigabit-only fiber module, it probably does
	 * not have a PHY, so switch to 802.3z negotiation mode.
	 * Otherwise, switch to SGMII mode (which is required to
	 * support non-gigabit speeds) and probe for a PHY.
	 */
	if (sfp->id.base.e1000_base_t ||
	    sfp->id.base.e100_base_lx ||
	    sfp->id.base.e100_base_fx)
		sfp_sm_probe_phy(sfp);
}

static int sfp_sm_mod_hpower(struct sfp *sfp)
{
	u32 power;
	u8 val;
	int err;

	power = 1000;
	if (sfp->id.ext.options & cpu_to_be16(SFP_OPTIONS_POWER_DECL))
		power = 1500;
	if (sfp->id.ext.options & cpu_to_be16(SFP_OPTIONS_HIGH_POWER_LEVEL))
		power = 2000;

	if (sfp->id.ext.sff8472_compliance == SFP_SFF8472_COMPLIANCE_NONE &&
	    (sfp->id.ext.diagmon & (SFP_DIAGMON_DDM | SFP_DIAGMON_ADDRMODE)) !=
	    SFP_DIAGMON_DDM) {
		/* The module appears not to implement bus address 0xa2,
		 * or requires an address change sequence, so assume that
		 * the module powers up in the indicated power mode.
		 */
		if (power > sfp->max_power_mW) {
			dev_err(sfp->dev,
				"Host does not support %u.%uW modules\n",
				power / 1000, (power / 100) % 10);
			return -EINVAL;
		}
		return 0;
	}

	if (power > sfp->max_power_mW) {
		dev_warn(sfp->dev,
			 "Host does not support %u.%uW modules, module left in power mode 1\n",
			 power / 1000, (power / 100) % 10);
		return 0;
	}

	if (power <= 1000)
		return 0;

	err = sfp_read(sfp, true, SFP_EXT_STATUS, &val, sizeof(val));
	if (err != sizeof(val)) {
		dev_err(sfp->dev, "Failed to read EEPROM: %d\n", err);
		err = -EAGAIN;
		goto err;
	}

	val |= BIT(0);

	err = sfp_write(sfp, true, SFP_EXT_STATUS, &val, sizeof(val));
	if (err != sizeof(val)) {
		dev_err(sfp->dev, "Failed to write EEPROM: %d\n", err);
		err = -EAGAIN;
		goto err;
	}

	dev_info(sfp->dev, "Module switched to %u.%uW power level\n",
		 power / 1000, (power / 100) % 10);
	return T_HPOWER_LEVEL;

err:
	return err;
}

static int sfp_sm_mod_probe(struct sfp *sfp)
{
	/* SFP module inserted - read I2C data */
	struct sfp_eeprom_id id;
	bool cotsworks;
	u8 check;
	int ret;

	ret = sfp_read(sfp, false, 0, &id, sizeof(id));
	if (ret < 0) {
		dev_err(sfp->dev, "failed to read EEPROM: %d\n", ret);
		return -EAGAIN;
	}

	if (ret != sizeof(id)) {
		dev_err(sfp->dev, "EEPROM short read: %d\n", ret);
		return -EAGAIN;
	}

	/* Cotsworks do not seem to update the checksums when they
	 * do the final programming with the final module part number,
	 * serial number and date code.
	 */
	cotsworks = !memcmp(id.base.vendor_name, "COTSWORKS       ", 16);

	/* Validate the checksum over the base structure */
	check = sfp_check(&id.base, sizeof(id.base) - 1);
	if (check != id.base.cc_base) {
		if (cotsworks) {
			dev_warn(sfp->dev,
				 "EEPROM base structure checksum failure (0x%02x != 0x%02x)\n",
				 check, id.base.cc_base);
		} else {
			dev_err(sfp->dev,
				"EEPROM base structure checksum failure: 0x%02x != 0x%02x\n",
				check, id.base.cc_base);
			print_hex_dump(KERN_ERR, "sfp EE: ", DUMP_PREFIX_OFFSET,
				       16, 1, &id, sizeof(id), true);
			return -EINVAL;
		}
	}

	check = sfp_check(&id.ext, sizeof(id.ext) - 1);
	if (check != id.ext.cc_ext) {
		if (cotsworks) {
			dev_warn(sfp->dev,
				 "EEPROM extended structure checksum failure (0x%02x != 0x%02x)\n",
				 check, id.ext.cc_ext);
		} else {
			dev_err(sfp->dev,
				"EEPROM extended structure checksum failure: 0x%02x != 0x%02x\n",
				check, id.ext.cc_ext);
			print_hex_dump(KERN_ERR, "sfp EE: ", DUMP_PREFIX_OFFSET,
				       16, 1, &id, sizeof(id), true);
			memset(&id.ext, 0, sizeof(id.ext));
		}
	}

	sfp->id = id;

	dev_info(sfp->dev, "module %.*s %.*s rev %.*s sn %.*s dc %.*s\n",
		 (int)sizeof(id.base.vendor_name), id.base.vendor_name,
		 (int)sizeof(id.base.vendor_pn), id.base.vendor_pn,
		 (int)sizeof(id.base.vendor_rev), id.base.vendor_rev,
		 (int)sizeof(id.ext.vendor_sn), id.ext.vendor_sn,
		 (int)sizeof(id.ext.datecode), id.ext.datecode);

	/* Check whether we support this module */
	if (!sfp->type->module_supported(&sfp->id)) {
		dev_err(sfp->dev,
			"module is not supported - phys id 0x%02x 0x%02x\n",
			sfp->id.base.phys_id, sfp->id.base.phys_ext_id);
		return -EINVAL;
	}

	/* If the module requires address swap mode, warn about it */
	if (sfp->id.ext.diagmon & SFP_DIAGMON_ADDRMODE)
		dev_warn(sfp->dev,
			 "module address swap to access page 0xA2 is not supported.\n");

	ret = sfp_hwmon_insert(sfp);
	if (ret < 0)
		return ret;

	ret = sfp_module_insert(sfp->sfp_bus, &sfp->id);
	if (ret < 0)
		return ret;

	return sfp_sm_mod_hpower(sfp);
}

static void sfp_sm_mod_remove(struct sfp *sfp)
{
	sfp_module_remove(sfp->sfp_bus);

	sfp_hwmon_remove(sfp);

	if (sfp->mod_phy)
		sfp_sm_phy_detach(sfp);

	sfp_module_tx_disable(sfp);

	memset(&sfp->id, 0, sizeof(sfp->id));

	dev_info(sfp->dev, "module removed\n");
}

static void sfp_sm_event(struct sfp *sfp, unsigned int event)
{
	mutex_lock(&sfp->sm_mutex);

	dev_dbg(sfp->dev, "SM: enter %s:%s:%s event %s\n",
		mod_state_to_str(sfp->sm_mod_state),
		dev_state_to_str(sfp->sm_dev_state),
		sm_state_to_str(sfp->sm_state),
		event_to_str(event));

	/* This state machine tracks the insert/remove state of
	 * the module, and handles probing the on-board EEPROM.
	 */
	switch (sfp->sm_mod_state) {
	default:
		if (event == SFP_E_INSERT && sfp->attached) {
			sfp_module_tx_disable(sfp);
			sfp_sm_ins_next(sfp, SFP_MOD_PROBE, T_PROBE_INIT);
		}
		break;

	case SFP_MOD_PROBE:
		if (event == SFP_E_REMOVE) {
			sfp_sm_ins_next(sfp, SFP_MOD_EMPTY, 0);
		} else if (event == SFP_E_TIMEOUT) {
			int val = sfp_sm_mod_probe(sfp);

			if (val == 0)
				sfp_sm_ins_next(sfp, SFP_MOD_PRESENT, 0);
			else if (val > 0)
				sfp_sm_ins_next(sfp, SFP_MOD_HPOWER, val);
			else if (val != -EAGAIN)
				sfp_sm_ins_next(sfp, SFP_MOD_ERROR, 0);
			else
				sfp_sm_set_timer(sfp, T_PROBE_RETRY);
		}
		break;

	case SFP_MOD_HPOWER:
		if (event == SFP_E_TIMEOUT) {
			sfp_sm_ins_next(sfp, SFP_MOD_PRESENT, 0);
			break;
		}
		/* fallthrough */
	case SFP_MOD_PRESENT:
	case SFP_MOD_ERROR:
		if (event == SFP_E_REMOVE) {
			sfp_sm_mod_remove(sfp);
			sfp_sm_ins_next(sfp, SFP_MOD_EMPTY, 0);
		}
		break;
	}

	/* This state machine tracks the netdev up/down state */
	switch (sfp->sm_dev_state) {
	default:
		if (event == SFP_E_DEV_UP)
			sfp->sm_dev_state = SFP_DEV_UP;
		break;

	case SFP_DEV_UP:
		if (event == SFP_E_DEV_DOWN) {
			/* If the module has a PHY, avoid raising TX disable
			 * as this resets the PHY. Otherwise, raise it to
			 * turn the laser off.
			 */
			if (!sfp->mod_phy)
				sfp_module_tx_disable(sfp);
			sfp->sm_dev_state = SFP_DEV_DOWN;
		}
		break;
	}

	/* Some events are global */
	if (sfp->sm_state != SFP_S_DOWN &&
	    (sfp->sm_mod_state != SFP_MOD_PRESENT ||
	     sfp->sm_dev_state != SFP_DEV_UP)) {
		if (sfp->sm_state == SFP_S_LINK_UP &&
		    sfp->sm_dev_state == SFP_DEV_UP)
			sfp_sm_link_down(sfp);
		if (sfp->mod_phy)
			sfp_sm_phy_detach(sfp);
		sfp_sm_next(sfp, SFP_S_DOWN, 0);
		mutex_unlock(&sfp->sm_mutex);
		return;
	}

	/* The main state machine */
	switch (sfp->sm_state) {
	case SFP_S_DOWN:
		if (sfp->sm_mod_state == SFP_MOD_PRESENT &&
		    sfp->sm_dev_state == SFP_DEV_UP)
			sfp_sm_mod_init(sfp);
		break;

	case SFP_S_INIT:
		if (event == SFP_E_TIMEOUT && sfp->state & SFP_F_TX_FAULT)
			sfp_sm_fault(sfp, true);
		else if (event == SFP_E_TIMEOUT || event == SFP_E_TX_CLEAR)
			sfp_sm_link_check_los(sfp);
		break;

	case SFP_S_WAIT_LOS:
		if (event == SFP_E_TX_FAULT)
			sfp_sm_fault(sfp, true);
		else if (sfp_los_event_inactive(sfp, event))
			sfp_sm_link_up(sfp);
		break;

	case SFP_S_LINK_UP:
		if (event == SFP_E_TX_FAULT) {
			sfp_sm_link_down(sfp);
			sfp_sm_fault(sfp, true);
		} else if (sfp_los_event_active(sfp, event)) {
			sfp_sm_link_down(sfp);
			sfp_sm_next(sfp, SFP_S_WAIT_LOS, 0);
		}
		break;

	case SFP_S_TX_FAULT:
		if (event == SFP_E_TIMEOUT) {
			sfp_module_tx_fault_reset(sfp);
			sfp_sm_next(sfp, SFP_S_REINIT, T_INIT_JIFFIES);
		}
		break;

	case SFP_S_REINIT:
		if (event == SFP_E_TIMEOUT && sfp->state & SFP_F_TX_FAULT) {
			sfp_sm_fault(sfp, false);
		} else if (event == SFP_E_TIMEOUT || event == SFP_E_TX_CLEAR) {
			dev_info(sfp->dev, "module transmit fault recovered\n");
			sfp_sm_link_check_los(sfp);
		}
		break;

	case SFP_S_TX_DISABLE:
		break;
	}

	dev_dbg(sfp->dev, "SM: exit %s:%s:%s\n",
		mod_state_to_str(sfp->sm_mod_state),
		dev_state_to_str(sfp->sm_dev_state),
		sm_state_to_str(sfp->sm_state));

	mutex_unlock(&sfp->sm_mutex);
}

static void sfp_attach(struct sfp *sfp)
{
	sfp->attached = true;
	if (sfp->state & SFP_F_PRESENT)
		sfp_sm_event(sfp, SFP_E_INSERT);
}

static void sfp_detach(struct sfp *sfp)
{
	sfp->attached = false;
	sfp_sm_event(sfp, SFP_E_REMOVE);
}

static void sfp_start(struct sfp *sfp)
{
	sfp_sm_event(sfp, SFP_E_DEV_UP);
}

static void sfp_stop(struct sfp *sfp)
{
	sfp_sm_event(sfp, SFP_E_DEV_DOWN);
}

static int sfp_module_info(struct sfp *sfp, struct ethtool_modinfo *modinfo)
{
	/* locking... and check module is present */

	if (sfp->id.ext.sff8472_compliance &&
	    !(sfp->id.ext.diagmon & SFP_DIAGMON_ADDRMODE)) {
		modinfo->type = ETH_MODULE_SFF_8472;
		modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
	} else {
		modinfo->type = ETH_MODULE_SFF_8079;
		modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
	}
	return 0;
}

static int sfp_module_eeprom(struct sfp *sfp, struct ethtool_eeprom *ee,
			     u8 *data)
{
	unsigned int first, last, len;
	int ret;

	if (ee->len == 0)
		return -EINVAL;

	first = ee->offset;
	last = ee->offset + ee->len;
	if (first < ETH_MODULE_SFF_8079_LEN) {
		len = min_t(unsigned int, last, ETH_MODULE_SFF_8079_LEN);
		len -= first;

		ret = sfp_read(sfp, false, first, data, len);
		if (ret < 0)
			return ret;

		first += len;
		data += len;
	}
	if (first < ETH_MODULE_SFF_8472_LEN && last > ETH_MODULE_SFF_8079_LEN) {
		len = min_t(unsigned int, last, ETH_MODULE_SFF_8472_LEN);
		len -= first;
		first -= ETH_MODULE_SFF_8079_LEN;

		ret = sfp_read(sfp, true, first, data, len);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static const struct sfp_socket_ops sfp_module_ops = {
	.attach = sfp_attach,
	.detach = sfp_detach,
	.start = sfp_start,
	.stop = sfp_stop,
	.module_info = sfp_module_info,
	.module_eeprom = sfp_module_eeprom,
};

static void sfp_timeout(struct work_struct *work)
{
	struct sfp *sfp = container_of(work, struct sfp, timeout.work);

	rtnl_lock();
	sfp_sm_event(sfp, SFP_E_TIMEOUT);
	rtnl_unlock();
}

static void sfp_check_state(struct sfp *sfp)
{
	unsigned int state, i, changed;

	state = sfp_get_state(sfp);
	changed = state ^ sfp->state;
	changed &= SFP_F_PRESENT | SFP_F_LOS | SFP_F_TX_FAULT;

	for (i = 0; i < GPIO_MAX; i++)
		if (changed & BIT(i))
			dev_dbg(sfp->dev, "%s %u -> %u\n", gpio_of_names[i],
				!!(sfp->state & BIT(i)), !!(state & BIT(i)));

	state |= sfp->state & (SFP_F_TX_DISABLE | SFP_F_RATE_SELECT);
	sfp->state = state;

	rtnl_lock();
	if (changed & SFP_F_PRESENT)
		sfp_sm_event(sfp, state & SFP_F_PRESENT ?
				SFP_E_INSERT : SFP_E_REMOVE);

	if (changed & SFP_F_TX_FAULT)
		sfp_sm_event(sfp, state & SFP_F_TX_FAULT ?
				SFP_E_TX_FAULT : SFP_E_TX_CLEAR);

	if (changed & SFP_F_LOS)
		sfp_sm_event(sfp, state & SFP_F_LOS ?
				SFP_E_LOS_HIGH : SFP_E_LOS_LOW);
	rtnl_unlock();
}

static irqreturn_t sfp_irq(int irq, void *data)
{
	struct sfp *sfp = data;

	sfp_check_state(sfp);

	return IRQ_HANDLED;
}

static void sfp_poll(struct work_struct *work)
{
	struct sfp *sfp = container_of(work, struct sfp, poll.work);

	sfp_check_state(sfp);
	mod_delayed_work(system_wq, &sfp->poll, poll_jiffies);
}

static struct sfp *sfp_alloc(struct device *dev)
{
	struct sfp *sfp;

	sfp = kzalloc(sizeof(*sfp), GFP_KERNEL);
	if (!sfp)
		return ERR_PTR(-ENOMEM);

	sfp->dev = dev;

	mutex_init(&sfp->sm_mutex);
	INIT_DELAYED_WORK(&sfp->poll, sfp_poll);
	INIT_DELAYED_WORK(&sfp->timeout, sfp_timeout);

	return sfp;
}

static void sfp_cleanup(void *data)
{
	struct sfp *sfp = data;

	cancel_delayed_work_sync(&sfp->poll);
	cancel_delayed_work_sync(&sfp->timeout);
	if (sfp->i2c_mii) {
		mdiobus_unregister(sfp->i2c_mii);
		mdiobus_free(sfp->i2c_mii);
	}
	if (sfp->i2c)
		i2c_put_adapter(sfp->i2c);
	kfree(sfp);
}

static int sfp_probe(struct platform_device *pdev)
{
	const struct sff_data *sff;
	struct sfp *sfp;
	bool poll = false;
	int irq, err, i;

	sfp = sfp_alloc(&pdev->dev);
	if (IS_ERR(sfp))
		return PTR_ERR(sfp);

	platform_set_drvdata(pdev, sfp);

	err = devm_add_action(sfp->dev, sfp_cleanup, sfp);
	if (err < 0)
		return err;

	sff = sfp->type = &sfp_data;

	if (pdev->dev.of_node) {
		struct device_node *node = pdev->dev.of_node;
		const struct of_device_id *id;
		struct i2c_adapter *i2c;
		struct device_node *np;

		id = of_match_node(sfp_of_match, node);
		if (WARN_ON(!id))
			return -EINVAL;

		sff = sfp->type = id->data;

		np = of_parse_phandle(node, "i2c-bus", 0);
		if (!np) {
			dev_err(sfp->dev, "missing 'i2c-bus' property\n");
			return -ENODEV;
		}

		i2c = of_find_i2c_adapter_by_node(np);
		of_node_put(np);
		if (!i2c)
			return -EPROBE_DEFER;

		err = sfp_i2c_configure(sfp, i2c);
		if (err < 0) {
			i2c_put_adapter(i2c);
			return err;
		}
	}

	for (i = 0; i < GPIO_MAX; i++)
		if (sff->gpios & BIT(i)) {
			sfp->gpio[i] = devm_gpiod_get_optional(sfp->dev,
					   gpio_of_names[i], gpio_flags[i]);
			if (IS_ERR(sfp->gpio[i]))
				return PTR_ERR(sfp->gpio[i]);
		}

	sfp->get_state = sfp_gpio_get_state;
	sfp->set_state = sfp_gpio_set_state;

	/* Modules that have no detect signal are always present */
	if (!(sfp->gpio[GPIO_MODDEF0]))
		sfp->get_state = sff_gpio_get_state;

	device_property_read_u32(&pdev->dev, "maximum-power-milliwatt",
				 &sfp->max_power_mW);
	if (!sfp->max_power_mW)
		sfp->max_power_mW = 1000;

	dev_info(sfp->dev, "Host maximum power %u.%uW\n",
		 sfp->max_power_mW / 1000, (sfp->max_power_mW / 100) % 10);

	/* Get the initial state, and always signal TX disable,
	 * since the network interface will not be up.
	 */
	sfp->state = sfp_get_state(sfp) | SFP_F_TX_DISABLE;

	if (sfp->gpio[GPIO_RATE_SELECT] &&
	    gpiod_get_value_cansleep(sfp->gpio[GPIO_RATE_SELECT]))
		sfp->state |= SFP_F_RATE_SELECT;
	sfp_set_state(sfp, sfp->state);
	sfp_module_tx_disable(sfp);

	for (i = 0; i < GPIO_MAX; i++) {
		if (gpio_flags[i] != GPIOD_IN || !sfp->gpio[i])
			continue;

		irq = gpiod_to_irq(sfp->gpio[i]);
		if (!irq) {
			poll = true;
			continue;
		}

		err = devm_request_threaded_irq(sfp->dev, irq, NULL, sfp_irq,
						IRQF_ONESHOT |
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING,
						dev_name(sfp->dev), sfp);
		if (err)
			poll = true;
	}

	if (poll)
		mod_delayed_work(system_wq, &sfp->poll, poll_jiffies);

	/* We could have an issue in cases no Tx disable pin is available or
	 * wired as modules using a laser as their light source will continue to
	 * be active when the fiber is removed. This could be a safety issue and
	 * we should at least warn the user about that.
	 */
	if (!sfp->gpio[GPIO_TX_DISABLE])
		dev_warn(sfp->dev,
			 "No tx_disable pin: SFP modules will always be emitting.\n");

	sfp->sfp_bus = sfp_register_socket(sfp->dev, sfp, &sfp_module_ops);
	if (!sfp->sfp_bus)
		return -ENOMEM;

	return 0;
}

static int sfp_remove(struct platform_device *pdev)
{
	struct sfp *sfp = platform_get_drvdata(pdev);

	sfp_unregister_socket(sfp->sfp_bus);

	return 0;
}

static struct platform_driver sfp_driver = {
	.probe = sfp_probe,
	.remove = sfp_remove,
	.driver = {
		.name = "sfp",
		.of_match_table = sfp_of_match,
	},
};

static int sfp_init(void)
{
	poll_jiffies = msecs_to_jiffies(100);

	return platform_driver_register(&sfp_driver);
}
module_init(sfp_init);

static void sfp_exit(void)
{
	platform_driver_unregister(&sfp_driver);
}
module_exit(sfp_exit);

MODULE_ALIAS("platform:sfp");
MODULE_AUTHOR("Russell King");
MODULE_LICENSE("GPL v2");

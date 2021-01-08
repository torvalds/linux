// SPDX-License-Identifier: GPL-2.0
#include <linux/acpi.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/mdio/mdio-i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

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
	SFP_E_DEV_ATTACH,
	SFP_E_DEV_DETACH,
	SFP_E_DEV_DOWN,
	SFP_E_DEV_UP,
	SFP_E_TX_FAULT,
	SFP_E_TX_CLEAR,
	SFP_E_LOS_HIGH,
	SFP_E_LOS_LOW,
	SFP_E_TIMEOUT,

	SFP_MOD_EMPTY = 0,
	SFP_MOD_ERROR,
	SFP_MOD_PROBE,
	SFP_MOD_WAITDEV,
	SFP_MOD_HPOWER,
	SFP_MOD_WAITPWR,
	SFP_MOD_PRESENT,

	SFP_DEV_DETACHED = 0,
	SFP_DEV_DOWN,
	SFP_DEV_UP,

	SFP_S_DOWN = 0,
	SFP_S_FAIL,
	SFP_S_WAIT,
	SFP_S_INIT,
	SFP_S_INIT_PHY,
	SFP_S_INIT_TX_FAULT,
	SFP_S_WAIT_LOS,
	SFP_S_LINK_UP,
	SFP_S_TX_FAULT,
	SFP_S_REINIT,
	SFP_S_TX_DISABLE,
};

static const char  * const mod_state_strings[] = {
	[SFP_MOD_EMPTY] = "empty",
	[SFP_MOD_ERROR] = "error",
	[SFP_MOD_PROBE] = "probe",
	[SFP_MOD_WAITDEV] = "waitdev",
	[SFP_MOD_HPOWER] = "hpower",
	[SFP_MOD_WAITPWR] = "waitpwr",
	[SFP_MOD_PRESENT] = "present",
};

static const char *mod_state_to_str(unsigned short mod_state)
{
	if (mod_state >= ARRAY_SIZE(mod_state_strings))
		return "Unknown module state";
	return mod_state_strings[mod_state];
}

static const char * const dev_state_strings[] = {
	[SFP_DEV_DETACHED] = "detached",
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
	[SFP_E_DEV_ATTACH] = "dev_attach",
	[SFP_E_DEV_DETACH] = "dev_detach",
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
	[SFP_S_FAIL] = "fail",
	[SFP_S_WAIT] = "wait",
	[SFP_S_INIT] = "init",
	[SFP_S_INIT_PHY] = "init_phy",
	[SFP_S_INIT_TX_FAULT] = "init_tx_fault",
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

/* t_start_up (SFF-8431) or t_init (SFF-8472) is the time required for a
 * non-cooled module to initialise its laser safety circuitry. We wait
 * an initial T_WAIT period before we check the tx fault to give any PHY
 * on board (for a copper SFP) time to initialise.
 */
#define T_WAIT			msecs_to_jiffies(50)
#define T_START_UP		msecs_to_jiffies(300)
#define T_START_UP_BAD_GPON	msecs_to_jiffies(60000)

/* t_reset is the time required to assert the TX_DISABLE signal to reset
 * an indicated TX_FAULT.
 */
#define T_RESET_US		10
#define T_FAULT_RECOVER		msecs_to_jiffies(1000)

/* N_FAULT_INIT is the number of recovery attempts at module initialisation
 * time. If the TX_FAULT signal is not deasserted after this number of
 * attempts at clearing it, we decide that the module is faulty.
 * N_FAULT is the same but after the module has initialised.
 */
#define N_FAULT_INIT		5
#define N_FAULT			5

/* T_PHY_RETRY is the time interval between attempts to probe the PHY.
 * R_PHY_RETRY is the number of attempts.
 */
#define T_PHY_RETRY		msecs_to_jiffies(50)
#define R_PHY_RETRY		12

/* SFP module presence detection is poor: the three MOD DEF signals are
 * the same length on the PCB, which means it's possible for MOD DEF 0 to
 * connect before the I2C bus on MOD DEF 1/2.
 *
 * The SFF-8472 specifies t_serial ("Time from power on until module is
 * ready for data transmission over the two wire serial bus.") as 300ms.
 */
#define T_SERIAL		msecs_to_jiffies(300)
#define T_HPOWER_LEVEL		msecs_to_jiffies(300)
#define T_PROBE_RETRY_INIT	msecs_to_jiffies(100)
#define R_PROBE_RETRY_INIT	10
#define T_PROBE_RETRY_SLOW	msecs_to_jiffies(5000)
#define R_PROBE_RETRY_SLOW	12

/* SFP modules appear to always have their PHY configured for bus address
 * 0x56 (which with mdio-i2c, translates to a PHY address of 22).
 */
#define SFP_PHY_ADDR	22

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
	size_t i2c_block_size;
	u32 max_power_mW;

	unsigned int (*get_state)(struct sfp *);
	void (*set_state)(struct sfp *, unsigned int);
	int (*read)(struct sfp *, bool, u8, void *, size_t);
	int (*write)(struct sfp *, bool, u8, void *, size_t);

	struct gpio_desc *gpio[GPIO_MAX];
	int gpio_irq[GPIO_MAX];

	bool need_poll;

	struct mutex st_mutex;			/* Protects state */
	unsigned int state_soft_mask;
	unsigned int state;
	struct delayed_work poll;
	struct delayed_work timeout;
	struct mutex sm_mutex;			/* Protects state machine */
	unsigned char sm_mod_state;
	unsigned char sm_mod_tries_init;
	unsigned char sm_mod_tries;
	unsigned char sm_dev_state;
	unsigned short sm_state;
	unsigned char sm_fault_retries;
	unsigned char sm_phy_retries;

	struct sfp_eeprom_id id;
	unsigned int module_power_mW;
	unsigned int module_t_start_up;

#if IS_ENABLED(CONFIG_HWMON)
	struct sfp_diag diag;
	struct delayed_work hwmon_probe;
	unsigned int hwmon_tries;
	struct device *hwmon_dev;
	char *hwmon_name;
#endif

};

static bool sff_module_supported(const struct sfp_eeprom_id *id)
{
	return id->base.phys_id == SFF8024_ID_SFF_8472 &&
	       id->base.phys_ext_id == SFP_PHYS_EXT_ID_SFP;
}

static const struct sff_data sff_data = {
	.gpios = SFP_F_LOS | SFP_F_TX_FAULT | SFP_F_TX_DISABLE,
	.module_supported = sff_module_supported,
};

static bool sfp_module_supported(const struct sfp_eeprom_id *id)
{
	return id->base.phys_id == SFF8024_ID_SFP &&
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
	size_t block_size;
	size_t this_len;
	u8 bus_addr;
	int ret;

	if (a2) {
		block_size = 16;
		bus_addr = 0x51;
	} else {
		block_size = sfp->i2c_block_size;
		bus_addr = 0x50;
	}

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
		if (this_len > block_size)
			this_len = block_size;

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
static int sfp_read(struct sfp *sfp, bool a2, u8 addr, void *buf, size_t len)
{
	return sfp->read(sfp, a2, addr, buf, len);
}

static int sfp_write(struct sfp *sfp, bool a2, u8 addr, void *buf, size_t len)
{
	return sfp->write(sfp, a2, addr, buf, len);
}

static unsigned int sfp_soft_get_state(struct sfp *sfp)
{
	unsigned int state = 0;
	u8 status;
	int ret;

	ret = sfp_read(sfp, true, SFP_STATUS, &status, sizeof(status));
	if (ret == sizeof(status)) {
		if (status & SFP_STATUS_RX_LOS)
			state |= SFP_F_LOS;
		if (status & SFP_STATUS_TX_FAULT)
			state |= SFP_F_TX_FAULT;
	} else {
		dev_err_ratelimited(sfp->dev,
				    "failed to read SFP soft status: %d\n",
				    ret);
		/* Preserve the current state */
		state = sfp->state;
	}

	return state & sfp->state_soft_mask;
}

static void sfp_soft_set_state(struct sfp *sfp, unsigned int state)
{
	u8 status;

	if (sfp_read(sfp, true, SFP_STATUS, &status, sizeof(status)) ==
		     sizeof(status)) {
		if (state & SFP_F_TX_DISABLE)
			status |= SFP_STATUS_TX_DISABLE_FORCE;
		else
			status &= ~SFP_STATUS_TX_DISABLE_FORCE;

		sfp_write(sfp, true, SFP_STATUS, &status, sizeof(status));
	}
}

static void sfp_soft_start_poll(struct sfp *sfp)
{
	const struct sfp_eeprom_id *id = &sfp->id;

	sfp->state_soft_mask = 0;
	if (id->ext.enhopts & SFP_ENHOPTS_SOFT_TX_DISABLE &&
	    !sfp->gpio[GPIO_TX_DISABLE])
		sfp->state_soft_mask |= SFP_F_TX_DISABLE;
	if (id->ext.enhopts & SFP_ENHOPTS_SOFT_TX_FAULT &&
	    !sfp->gpio[GPIO_TX_FAULT])
		sfp->state_soft_mask |= SFP_F_TX_FAULT;
	if (id->ext.enhopts & SFP_ENHOPTS_SOFT_RX_LOS &&
	    !sfp->gpio[GPIO_LOS])
		sfp->state_soft_mask |= SFP_F_LOS;

	if (sfp->state_soft_mask & (SFP_F_LOS | SFP_F_TX_FAULT) &&
	    !sfp->need_poll)
		mod_delayed_work(system_wq, &sfp->poll, poll_jiffies);
}

static void sfp_soft_stop_poll(struct sfp *sfp)
{
	sfp->state_soft_mask = 0;
}

static unsigned int sfp_get_state(struct sfp *sfp)
{
	unsigned int state = sfp->get_state(sfp);

	if (state & SFP_F_PRESENT &&
	    sfp->state_soft_mask & (SFP_F_LOS | SFP_F_TX_FAULT))
		state |= sfp_soft_get_state(sfp);

	return state;
}

static void sfp_set_state(struct sfp *sfp, unsigned int state)
{
	sfp->set_state(sfp, state);

	if (state & SFP_F_PRESENT &&
	    sfp->state_soft_mask & SFP_F_TX_DISABLE)
		sfp_soft_set_state(sfp, state);
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
			fallthrough;
		case hwmon_temp_input:
		case hwmon_temp_label:
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
			fallthrough;
		case hwmon_in_input:
		case hwmon_in_label:
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
			fallthrough;
		case hwmon_curr_input:
		case hwmon_curr_label:
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
			fallthrough;
		case hwmon_power_input:
		case hwmon_power_label:
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
	*value = DIV_ROUND_CLOSEST(*value, 10);
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

static const char *const sfp_hwmon_power_labels[] = {
	"TX_power",
	"RX_power",
};

static int sfp_hwmon_read_string(struct device *dev,
				 enum hwmon_sensor_types type,
				 u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_label:
			*str = "bias";
			return 0;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_label:
			*str = "temperature";
			return 0;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_in:
		switch (attr) {
		case hwmon_in_label:
			*str = "VCC";
			return 0;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_power:
		switch (attr) {
		case hwmon_power_label:
			*str = sfp_hwmon_power_labels[channel];
			return 0;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_ops sfp_hwmon_ops = {
	.is_visible = sfp_hwmon_is_visible,
	.read = sfp_hwmon_read,
	.read_string = sfp_hwmon_read_string,
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
	HWMON_T_CRIT_ALARM | HWMON_T_LCRIT_ALARM |
	HWMON_T_LABEL,
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
	HWMON_I_CRIT_ALARM | HWMON_I_LCRIT_ALARM |
	HWMON_I_LABEL,
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
	HWMON_C_CRIT_ALARM | HWMON_C_LCRIT_ALARM |
	HWMON_C_LABEL,
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
	HWMON_P_CRIT_ALARM | HWMON_P_LCRIT_ALARM |
	HWMON_P_LABEL,
	/* Receive power */
	HWMON_P_INPUT |
	HWMON_P_MAX | HWMON_P_MIN |
	HWMON_P_MAX_ALARM | HWMON_P_MIN_ALARM |
	HWMON_P_CRIT | HWMON_P_LCRIT |
	HWMON_P_CRIT_ALARM | HWMON_P_LCRIT_ALARM |
	HWMON_P_LABEL,
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

static void sfp_hwmon_probe(struct work_struct *work)
{
	struct sfp *sfp = container_of(work, struct sfp, hwmon_probe.work);
	int err, i;

	err = sfp_read(sfp, true, 0, &sfp->diag, sizeof(sfp->diag));
	if (err < 0) {
		if (sfp->hwmon_tries--) {
			mod_delayed_work(system_wq, &sfp->hwmon_probe,
					 T_PROBE_RETRY_SLOW);
		} else {
			dev_warn(sfp->dev, "hwmon probe failed: %d\n", err);
		}
		return;
	}

	sfp->hwmon_name = kstrdup(dev_name(sfp->dev), GFP_KERNEL);
	if (!sfp->hwmon_name) {
		dev_err(sfp->dev, "out of memory for hwmon name\n");
		return;
	}

	for (i = 0; sfp->hwmon_name[i]; i++)
		if (hwmon_is_bad_char(sfp->hwmon_name[i]))
			sfp->hwmon_name[i] = '_';

	sfp->hwmon_dev = hwmon_device_register_with_info(sfp->dev,
							 sfp->hwmon_name, sfp,
							 &sfp_hwmon_chip_info,
							 NULL);
	if (IS_ERR(sfp->hwmon_dev))
		dev_err(sfp->dev, "failed to register hwmon device: %ld\n",
			PTR_ERR(sfp->hwmon_dev));
}

static int sfp_hwmon_insert(struct sfp *sfp)
{
	if (sfp->id.ext.sff8472_compliance == SFP_SFF8472_COMPLIANCE_NONE)
		return 0;

	if (!(sfp->id.ext.diagmon & SFP_DIAGMON_DDM))
		return 0;

	if (sfp->id.ext.diagmon & SFP_DIAGMON_ADDRMODE)
		/* This driver in general does not support address
		 * change.
		 */
		return 0;

	mod_delayed_work(system_wq, &sfp->hwmon_probe, 1);
	sfp->hwmon_tries = R_PROBE_RETRY_SLOW;

	return 0;
}

static void sfp_hwmon_remove(struct sfp *sfp)
{
	cancel_delayed_work_sync(&sfp->hwmon_probe);
	if (!IS_ERR_OR_NULL(sfp->hwmon_dev)) {
		hwmon_device_unregister(sfp->hwmon_dev);
		sfp->hwmon_dev = NULL;
		kfree(sfp->hwmon_name);
	}
}

static int sfp_hwmon_init(struct sfp *sfp)
{
	INIT_DELAYED_WORK(&sfp->hwmon_probe, sfp_hwmon_probe);

	return 0;
}

static void sfp_hwmon_exit(struct sfp *sfp)
{
	cancel_delayed_work_sync(&sfp->hwmon_probe);
}
#else
static int sfp_hwmon_insert(struct sfp *sfp)
{
	return 0;
}

static void sfp_hwmon_remove(struct sfp *sfp)
{
}

static int sfp_hwmon_init(struct sfp *sfp)
{
	return 0;
}

static void sfp_hwmon_exit(struct sfp *sfp)
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

static void sfp_sm_mod_next(struct sfp *sfp, unsigned int state,
			    unsigned int timeout)
{
	sfp->sm_mod_state = state;
	sfp_sm_set_timer(sfp, timeout);
}

static void sfp_sm_phy_detach(struct sfp *sfp)
{
	sfp_remove_phy(sfp->sfp_bus);
	phy_device_remove(sfp->mod_phy);
	phy_device_free(sfp->mod_phy);
	sfp->mod_phy = NULL;
}

static int sfp_sm_probe_phy(struct sfp *sfp, bool is_c45)
{
	struct phy_device *phy;
	int err;

	phy = get_phy_device(sfp->i2c_mii, SFP_PHY_ADDR, is_c45);
	if (phy == ERR_PTR(-ENODEV))
		return PTR_ERR(phy);
	if (IS_ERR(phy)) {
		dev_err(sfp->dev, "mdiobus scan returned %ld\n", PTR_ERR(phy));
		return PTR_ERR(phy);
	}

	err = phy_device_register(phy);
	if (err) {
		phy_device_free(phy);
		dev_err(sfp->dev, "phy_device_register failed: %d\n", err);
		return err;
	}

	err = sfp_add_phy(sfp->sfp_bus, phy);
	if (err) {
		phy_device_remove(phy);
		phy_device_free(phy);
		dev_err(sfp->dev, "sfp_add_phy failed: %d\n", err);
		return err;
	}

	sfp->mod_phy = phy;

	return 0;
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

static void sfp_sm_fault(struct sfp *sfp, unsigned int next_state, bool warn)
{
	if (sfp->sm_fault_retries && !--sfp->sm_fault_retries) {
		dev_err(sfp->dev,
			"module persistently indicates fault, disabling\n");
		sfp_sm_next(sfp, SFP_S_TX_DISABLE, 0);
	} else {
		if (warn)
			dev_err(sfp->dev, "module transmit fault indicated\n");

		sfp_sm_next(sfp, next_state, T_FAULT_RECOVER);
	}
}

/* Probe a SFP for a PHY device if the module supports copper - the PHY
 * normally sits at I2C bus address 0x56, and may either be a clause 22
 * or clause 45 PHY.
 *
 * Clause 22 copper SFP modules normally operate in Cisco SGMII mode with
 * negotiation enabled, but some may be in 1000base-X - which is for the
 * PHY driver to determine.
 *
 * Clause 45 copper SFP+ modules (10G) appear to switch their interface
 * mode according to the negotiated line speed.
 */
static int sfp_sm_probe_for_phy(struct sfp *sfp)
{
	int err = 0;

	switch (sfp->id.base.extended_cc) {
	case SFF8024_ECC_10GBASE_T_SFI:
	case SFF8024_ECC_10GBASE_T_SR:
	case SFF8024_ECC_5GBASE_T:
	case SFF8024_ECC_2_5GBASE_T:
		err = sfp_sm_probe_phy(sfp, true);
		break;

	default:
		if (sfp->id.base.e1000_base_t)
			err = sfp_sm_probe_phy(sfp, false);
		break;
	}
	return err;
}

static int sfp_module_parse_power(struct sfp *sfp)
{
	u32 power_mW = 1000;

	if (sfp->id.ext.options & cpu_to_be16(SFP_OPTIONS_POWER_DECL))
		power_mW = 1500;
	if (sfp->id.ext.options & cpu_to_be16(SFP_OPTIONS_HIGH_POWER_LEVEL))
		power_mW = 2000;

	if (power_mW > sfp->max_power_mW) {
		/* Module power specification exceeds the allowed maximum. */
		if (sfp->id.ext.sff8472_compliance ==
			SFP_SFF8472_COMPLIANCE_NONE &&
		    !(sfp->id.ext.diagmon & SFP_DIAGMON_DDM)) {
			/* The module appears not to implement bus address
			 * 0xa2, so assume that the module powers up in the
			 * indicated mode.
			 */
			dev_err(sfp->dev,
				"Host does not support %u.%uW modules\n",
				power_mW / 1000, (power_mW / 100) % 10);
			return -EINVAL;
		} else {
			dev_warn(sfp->dev,
				 "Host does not support %u.%uW modules, module left in power mode 1\n",
				 power_mW / 1000, (power_mW / 100) % 10);
			return 0;
		}
	}

	/* If the module requires a higher power mode, but also requires
	 * an address change sequence, warn the user that the module may
	 * not be functional.
	 */
	if (sfp->id.ext.diagmon & SFP_DIAGMON_ADDRMODE && power_mW > 1000) {
		dev_warn(sfp->dev,
			 "Address Change Sequence not supported but module requires %u.%uW, module may not be functional\n",
			 power_mW / 1000, (power_mW / 100) % 10);
		return 0;
	}

	sfp->module_power_mW = power_mW;

	return 0;
}

static int sfp_sm_mod_hpower(struct sfp *sfp, bool enable)
{
	u8 val;
	int err;

	err = sfp_read(sfp, true, SFP_EXT_STATUS, &val, sizeof(val));
	if (err != sizeof(val)) {
		dev_err(sfp->dev, "Failed to read EEPROM: %d\n", err);
		return -EAGAIN;
	}

	/* DM7052 reports as a high power module, responds to reads (with
	 * all bytes 0xff) at 0x51 but does not accept writes.  In any case,
	 * if the bit is already set, we're already in high power mode.
	 */
	if (!!(val & BIT(0)) == enable)
		return 0;

	if (enable)
		val |= BIT(0);
	else
		val &= ~BIT(0);

	err = sfp_write(sfp, true, SFP_EXT_STATUS, &val, sizeof(val));
	if (err != sizeof(val)) {
		dev_err(sfp->dev, "Failed to write EEPROM: %d\n", err);
		return -EAGAIN;
	}

	if (enable)
		dev_info(sfp->dev, "Module switched to %u.%uW power level\n",
			 sfp->module_power_mW / 1000,
			 (sfp->module_power_mW / 100) % 10);

	return 0;
}

/* Some modules (Nokia 3FE46541AA) lock up if byte 0x51 is read as a
 * single read. Switch back to reading 16 byte blocks unless we have
 * a CarlitoxxPro module (rebranded VSOL V2801F). Even more annoyingly,
 * some VSOL V2801F have the vendor name changed to OEM.
 */
static int sfp_quirk_i2c_block_size(const struct sfp_eeprom_base *base)
{
	if (!memcmp(base->vendor_name, "VSOL            ", 16))
		return 1;
	if (!memcmp(base->vendor_name, "OEM             ", 16) &&
	    !memcmp(base->vendor_pn,   "V2801F          ", 16))
		return 1;

	/* Some modules can't cope with long reads */
	return 16;
}

static void sfp_quirks_base(struct sfp *sfp, const struct sfp_eeprom_base *base)
{
	sfp->i2c_block_size = sfp_quirk_i2c_block_size(base);
}

static int sfp_cotsworks_fixup_check(struct sfp *sfp, struct sfp_eeprom_id *id)
{
	u8 check;
	int err;

	if (id->base.phys_id != SFF8024_ID_SFF_8472 ||
	    id->base.phys_ext_id != SFP_PHYS_EXT_ID_SFP ||
	    id->base.connector != SFF8024_CONNECTOR_LC) {
		dev_warn(sfp->dev, "Rewriting fiber module EEPROM with corrected values\n");
		id->base.phys_id = SFF8024_ID_SFF_8472;
		id->base.phys_ext_id = SFP_PHYS_EXT_ID_SFP;
		id->base.connector = SFF8024_CONNECTOR_LC;
		err = sfp_write(sfp, false, SFP_PHYS_ID, &id->base, 3);
		if (err != 3) {
			dev_err(sfp->dev, "Failed to rewrite module EEPROM: %d\n", err);
			return err;
		}

		/* Cotsworks modules have been found to require a delay between write operations. */
		mdelay(50);

		/* Update base structure checksum */
		check = sfp_check(&id->base, sizeof(id->base) - 1);
		err = sfp_write(sfp, false, SFP_CC_BASE, &check, 1);
		if (err != 1) {
			dev_err(sfp->dev, "Failed to update base structure checksum in fiber module EEPROM: %d\n", err);
			return err;
		}
	}
	return 0;
}

static int sfp_sm_mod_probe(struct sfp *sfp, bool report)
{
	/* SFP module inserted - read I2C data */
	struct sfp_eeprom_id id;
	bool cotsworks_sfbg;
	bool cotsworks;
	u8 check;
	int ret;

	/* Some modules (CarlitoxxPro CPGOS03-0490) do not support multibyte
	 * reads from the EEPROM, so start by reading the base identifying
	 * information one byte at a time.
	 */
	sfp->i2c_block_size = 1;

	ret = sfp_read(sfp, false, 0, &id.base, sizeof(id.base));
	if (ret < 0) {
		if (report)
			dev_err(sfp->dev, "failed to read EEPROM: %d\n", ret);
		return -EAGAIN;
	}

	if (ret != sizeof(id.base)) {
		dev_err(sfp->dev, "EEPROM short read: %d\n", ret);
		return -EAGAIN;
	}

	/* Cotsworks do not seem to update the checksums when they
	 * do the final programming with the final module part number,
	 * serial number and date code.
	 */
	cotsworks = !memcmp(id.base.vendor_name, "COTSWORKS       ", 16);
	cotsworks_sfbg = !memcmp(id.base.vendor_pn, "SFBG", 4);

	/* Cotsworks SFF module EEPROM do not always have valid phys_id,
	 * phys_ext_id, and connector bytes.  Rewrite SFF EEPROM bytes if
	 * Cotsworks PN matches and bytes are not correct.
	 */
	if (cotsworks && cotsworks_sfbg) {
		ret = sfp_cotsworks_fixup_check(sfp, &id);
		if (ret < 0)
			return ret;
	}

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

	/* Apply any early module-specific quirks */
	sfp_quirks_base(sfp, &id.base);

	ret = sfp_read(sfp, false, SFP_CC_BASE + 1, &id.ext, sizeof(id.ext));
	if (ret < 0) {
		if (report)
			dev_err(sfp->dev, "failed to read EEPROM: %d\n", ret);
		return -EAGAIN;
	}

	if (ret != sizeof(id.ext)) {
		dev_err(sfp->dev, "EEPROM short read: %d\n", ret);
		return -EAGAIN;
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
	if (!sfp->type->module_supported(&id)) {
		dev_err(sfp->dev,
			"module is not supported - phys id 0x%02x 0x%02x\n",
			sfp->id.base.phys_id, sfp->id.base.phys_ext_id);
		return -EINVAL;
	}

	/* If the module requires address swap mode, warn about it */
	if (sfp->id.ext.diagmon & SFP_DIAGMON_ADDRMODE)
		dev_warn(sfp->dev,
			 "module address swap to access page 0xA2 is not supported.\n");

	/* Parse the module power requirement */
	ret = sfp_module_parse_power(sfp);
	if (ret < 0)
		return ret;

	if (!memcmp(id.base.vendor_name, "ALCATELLUCENT   ", 16) &&
	    !memcmp(id.base.vendor_pn, "3FE46541AA      ", 16))
		sfp->module_t_start_up = T_START_UP_BAD_GPON;
	else
		sfp->module_t_start_up = T_START_UP;

	return 0;
}

static void sfp_sm_mod_remove(struct sfp *sfp)
{
	if (sfp->sm_mod_state > SFP_MOD_WAITDEV)
		sfp_module_remove(sfp->sfp_bus);

	sfp_hwmon_remove(sfp);

	memset(&sfp->id, 0, sizeof(sfp->id));
	sfp->module_power_mW = 0;

	dev_info(sfp->dev, "module removed\n");
}

/* This state machine tracks the upstream's state */
static void sfp_sm_device(struct sfp *sfp, unsigned int event)
{
	switch (sfp->sm_dev_state) {
	default:
		if (event == SFP_E_DEV_ATTACH)
			sfp->sm_dev_state = SFP_DEV_DOWN;
		break;

	case SFP_DEV_DOWN:
		if (event == SFP_E_DEV_DETACH)
			sfp->sm_dev_state = SFP_DEV_DETACHED;
		else if (event == SFP_E_DEV_UP)
			sfp->sm_dev_state = SFP_DEV_UP;
		break;

	case SFP_DEV_UP:
		if (event == SFP_E_DEV_DETACH)
			sfp->sm_dev_state = SFP_DEV_DETACHED;
		else if (event == SFP_E_DEV_DOWN)
			sfp->sm_dev_state = SFP_DEV_DOWN;
		break;
	}
}

/* This state machine tracks the insert/remove state of the module, probes
 * the on-board EEPROM, and sets up the power level.
 */
static void sfp_sm_module(struct sfp *sfp, unsigned int event)
{
	int err;

	/* Handle remove event globally, it resets this state machine */
	if (event == SFP_E_REMOVE) {
		if (sfp->sm_mod_state > SFP_MOD_PROBE)
			sfp_sm_mod_remove(sfp);
		sfp_sm_mod_next(sfp, SFP_MOD_EMPTY, 0);
		return;
	}

	/* Handle device detach globally */
	if (sfp->sm_dev_state < SFP_DEV_DOWN &&
	    sfp->sm_mod_state > SFP_MOD_WAITDEV) {
		if (sfp->module_power_mW > 1000 &&
		    sfp->sm_mod_state > SFP_MOD_HPOWER)
			sfp_sm_mod_hpower(sfp, false);
		sfp_sm_mod_next(sfp, SFP_MOD_WAITDEV, 0);
		return;
	}

	switch (sfp->sm_mod_state) {
	default:
		if (event == SFP_E_INSERT) {
			sfp_sm_mod_next(sfp, SFP_MOD_PROBE, T_SERIAL);
			sfp->sm_mod_tries_init = R_PROBE_RETRY_INIT;
			sfp->sm_mod_tries = R_PROBE_RETRY_SLOW;
		}
		break;

	case SFP_MOD_PROBE:
		/* Wait for T_PROBE_INIT to time out */
		if (event != SFP_E_TIMEOUT)
			break;

		err = sfp_sm_mod_probe(sfp, sfp->sm_mod_tries == 1);
		if (err == -EAGAIN) {
			if (sfp->sm_mod_tries_init &&
			   --sfp->sm_mod_tries_init) {
				sfp_sm_set_timer(sfp, T_PROBE_RETRY_INIT);
				break;
			} else if (sfp->sm_mod_tries && --sfp->sm_mod_tries) {
				if (sfp->sm_mod_tries == R_PROBE_RETRY_SLOW - 1)
					dev_warn(sfp->dev,
						 "please wait, module slow to respond\n");
				sfp_sm_set_timer(sfp, T_PROBE_RETRY_SLOW);
				break;
			}
		}
		if (err < 0) {
			sfp_sm_mod_next(sfp, SFP_MOD_ERROR, 0);
			break;
		}

		err = sfp_hwmon_insert(sfp);
		if (err)
			dev_warn(sfp->dev, "hwmon probe failed: %d\n", err);

		sfp_sm_mod_next(sfp, SFP_MOD_WAITDEV, 0);
		fallthrough;
	case SFP_MOD_WAITDEV:
		/* Ensure that the device is attached before proceeding */
		if (sfp->sm_dev_state < SFP_DEV_DOWN)
			break;

		/* Report the module insertion to the upstream device */
		err = sfp_module_insert(sfp->sfp_bus, &sfp->id);
		if (err < 0) {
			sfp_sm_mod_next(sfp, SFP_MOD_ERROR, 0);
			break;
		}

		/* If this is a power level 1 module, we are done */
		if (sfp->module_power_mW <= 1000)
			goto insert;

		sfp_sm_mod_next(sfp, SFP_MOD_HPOWER, 0);
		fallthrough;
	case SFP_MOD_HPOWER:
		/* Enable high power mode */
		err = sfp_sm_mod_hpower(sfp, true);
		if (err < 0) {
			if (err != -EAGAIN) {
				sfp_module_remove(sfp->sfp_bus);
				sfp_sm_mod_next(sfp, SFP_MOD_ERROR, 0);
			} else {
				sfp_sm_set_timer(sfp, T_PROBE_RETRY_INIT);
			}
			break;
		}

		sfp_sm_mod_next(sfp, SFP_MOD_WAITPWR, T_HPOWER_LEVEL);
		break;

	case SFP_MOD_WAITPWR:
		/* Wait for T_HPOWER_LEVEL to time out */
		if (event != SFP_E_TIMEOUT)
			break;

	insert:
		sfp_sm_mod_next(sfp, SFP_MOD_PRESENT, 0);
		break;

	case SFP_MOD_PRESENT:
	case SFP_MOD_ERROR:
		break;
	}
}

static void sfp_sm_main(struct sfp *sfp, unsigned int event)
{
	unsigned long timeout;
	int ret;

	/* Some events are global */
	if (sfp->sm_state != SFP_S_DOWN &&
	    (sfp->sm_mod_state != SFP_MOD_PRESENT ||
	     sfp->sm_dev_state != SFP_DEV_UP)) {
		if (sfp->sm_state == SFP_S_LINK_UP &&
		    sfp->sm_dev_state == SFP_DEV_UP)
			sfp_sm_link_down(sfp);
		if (sfp->sm_state > SFP_S_INIT)
			sfp_module_stop(sfp->sfp_bus);
		if (sfp->mod_phy)
			sfp_sm_phy_detach(sfp);
		sfp_module_tx_disable(sfp);
		sfp_soft_stop_poll(sfp);
		sfp_sm_next(sfp, SFP_S_DOWN, 0);
		return;
	}

	/* The main state machine */
	switch (sfp->sm_state) {
	case SFP_S_DOWN:
		if (sfp->sm_mod_state != SFP_MOD_PRESENT ||
		    sfp->sm_dev_state != SFP_DEV_UP)
			break;

		if (!(sfp->id.ext.diagmon & SFP_DIAGMON_ADDRMODE))
			sfp_soft_start_poll(sfp);

		sfp_module_tx_enable(sfp);

		/* Initialise the fault clearance retries */
		sfp->sm_fault_retries = N_FAULT_INIT;

		/* We need to check the TX_FAULT state, which is not defined
		 * while TX_DISABLE is asserted. The earliest we want to do
		 * anything (such as probe for a PHY) is 50ms.
		 */
		sfp_sm_next(sfp, SFP_S_WAIT, T_WAIT);
		break;

	case SFP_S_WAIT:
		if (event != SFP_E_TIMEOUT)
			break;

		if (sfp->state & SFP_F_TX_FAULT) {
			/* Wait up to t_init (SFF-8472) or t_start_up (SFF-8431)
			 * from the TX_DISABLE deassertion for the module to
			 * initialise, which is indicated by TX_FAULT
			 * deasserting.
			 */
			timeout = sfp->module_t_start_up;
			if (timeout > T_WAIT)
				timeout -= T_WAIT;
			else
				timeout = 1;

			sfp_sm_next(sfp, SFP_S_INIT, timeout);
		} else {
			/* TX_FAULT is not asserted, assume the module has
			 * finished initialising.
			 */
			goto init_done;
		}
		break;

	case SFP_S_INIT:
		if (event == SFP_E_TIMEOUT && sfp->state & SFP_F_TX_FAULT) {
			/* TX_FAULT is still asserted after t_init or
			 * or t_start_up, so assume there is a fault.
			 */
			sfp_sm_fault(sfp, SFP_S_INIT_TX_FAULT,
				     sfp->sm_fault_retries == N_FAULT_INIT);
		} else if (event == SFP_E_TIMEOUT || event == SFP_E_TX_CLEAR) {
	init_done:
			sfp->sm_phy_retries = R_PHY_RETRY;
			goto phy_probe;
		}
		break;

	case SFP_S_INIT_PHY:
		if (event != SFP_E_TIMEOUT)
			break;
	phy_probe:
		/* TX_FAULT deasserted or we timed out with TX_FAULT
		 * clear.  Probe for the PHY and check the LOS state.
		 */
		ret = sfp_sm_probe_for_phy(sfp);
		if (ret == -ENODEV) {
			if (--sfp->sm_phy_retries) {
				sfp_sm_next(sfp, SFP_S_INIT_PHY, T_PHY_RETRY);
				break;
			} else {
				dev_info(sfp->dev, "no PHY detected\n");
			}
		} else if (ret) {
			sfp_sm_next(sfp, SFP_S_FAIL, 0);
			break;
		}
		if (sfp_module_start(sfp->sfp_bus)) {
			sfp_sm_next(sfp, SFP_S_FAIL, 0);
			break;
		}
		sfp_sm_link_check_los(sfp);

		/* Reset the fault retry count */
		sfp->sm_fault_retries = N_FAULT;
		break;

	case SFP_S_INIT_TX_FAULT:
		if (event == SFP_E_TIMEOUT) {
			sfp_module_tx_fault_reset(sfp);
			sfp_sm_next(sfp, SFP_S_INIT, sfp->module_t_start_up);
		}
		break;

	case SFP_S_WAIT_LOS:
		if (event == SFP_E_TX_FAULT)
			sfp_sm_fault(sfp, SFP_S_TX_FAULT, true);
		else if (sfp_los_event_inactive(sfp, event))
			sfp_sm_link_up(sfp);
		break;

	case SFP_S_LINK_UP:
		if (event == SFP_E_TX_FAULT) {
			sfp_sm_link_down(sfp);
			sfp_sm_fault(sfp, SFP_S_TX_FAULT, true);
		} else if (sfp_los_event_active(sfp, event)) {
			sfp_sm_link_down(sfp);
			sfp_sm_next(sfp, SFP_S_WAIT_LOS, 0);
		}
		break;

	case SFP_S_TX_FAULT:
		if (event == SFP_E_TIMEOUT) {
			sfp_module_tx_fault_reset(sfp);
			sfp_sm_next(sfp, SFP_S_REINIT, sfp->module_t_start_up);
		}
		break;

	case SFP_S_REINIT:
		if (event == SFP_E_TIMEOUT && sfp->state & SFP_F_TX_FAULT) {
			sfp_sm_fault(sfp, SFP_S_TX_FAULT, false);
		} else if (event == SFP_E_TIMEOUT || event == SFP_E_TX_CLEAR) {
			dev_info(sfp->dev, "module transmit fault recovered\n");
			sfp_sm_link_check_los(sfp);
		}
		break;

	case SFP_S_TX_DISABLE:
		break;
	}
}

static void sfp_sm_event(struct sfp *sfp, unsigned int event)
{
	mutex_lock(&sfp->sm_mutex);

	dev_dbg(sfp->dev, "SM: enter %s:%s:%s event %s\n",
		mod_state_to_str(sfp->sm_mod_state),
		dev_state_to_str(sfp->sm_dev_state),
		sm_state_to_str(sfp->sm_state),
		event_to_str(event));

	sfp_sm_device(sfp, event);
	sfp_sm_module(sfp, event);
	sfp_sm_main(sfp, event);

	dev_dbg(sfp->dev, "SM: exit %s:%s:%s\n",
		mod_state_to_str(sfp->sm_mod_state),
		dev_state_to_str(sfp->sm_dev_state),
		sm_state_to_str(sfp->sm_state));

	mutex_unlock(&sfp->sm_mutex);
}

static void sfp_attach(struct sfp *sfp)
{
	sfp_sm_event(sfp, SFP_E_DEV_ATTACH);
}

static void sfp_detach(struct sfp *sfp)
{
	sfp_sm_event(sfp, SFP_E_DEV_DETACH);
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

	mutex_lock(&sfp->st_mutex);
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
	mutex_unlock(&sfp->st_mutex);
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

	if (sfp->state_soft_mask & (SFP_F_LOS | SFP_F_TX_FAULT) ||
	    sfp->need_poll)
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
	mutex_init(&sfp->st_mutex);
	INIT_DELAYED_WORK(&sfp->poll, sfp_poll);
	INIT_DELAYED_WORK(&sfp->timeout, sfp_timeout);

	sfp_hwmon_init(sfp);

	return sfp;
}

static void sfp_cleanup(void *data)
{
	struct sfp *sfp = data;

	sfp_hwmon_exit(sfp);

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
	struct i2c_adapter *i2c;
	char *sfp_irq_name;
	struct sfp *sfp;
	int err, i;

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
	} else if (has_acpi_companion(&pdev->dev)) {
		struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
		struct fwnode_handle *fw = acpi_fwnode_handle(adev);
		struct fwnode_reference_args args;
		struct acpi_handle *acpi_handle;
		int ret;

		ret = acpi_node_get_property_reference(fw, "i2c-bus", 0, &args);
		if (ret || !is_acpi_device_node(args.fwnode)) {
			dev_err(&pdev->dev, "missing 'i2c-bus' property\n");
			return -ENODEV;
		}

		acpi_handle = ACPI_HANDLE_FWNODE(args.fwnode);
		i2c = i2c_acpi_find_adapter_by_handle(acpi_handle);
	} else {
		return -EINVAL;
	}

	if (!i2c)
		return -EPROBE_DEFER;

	err = sfp_i2c_configure(sfp, i2c);
	if (err < 0) {
		i2c_put_adapter(i2c);
		return err;
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
	if (sfp->state & SFP_F_PRESENT) {
		rtnl_lock();
		sfp_sm_event(sfp, SFP_E_INSERT);
		rtnl_unlock();
	}

	for (i = 0; i < GPIO_MAX; i++) {
		if (gpio_flags[i] != GPIOD_IN || !sfp->gpio[i])
			continue;

		sfp->gpio_irq[i] = gpiod_to_irq(sfp->gpio[i]);
		if (sfp->gpio_irq[i] < 0) {
			sfp->gpio_irq[i] = 0;
			sfp->need_poll = true;
			continue;
		}

		sfp_irq_name = devm_kasprintf(sfp->dev, GFP_KERNEL,
					      "%s-%s", dev_name(sfp->dev),
					      gpio_of_names[i]);

		if (!sfp_irq_name)
			return -ENOMEM;

		err = devm_request_threaded_irq(sfp->dev, sfp->gpio_irq[i],
						NULL, sfp_irq,
						IRQF_ONESHOT |
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING,
						sfp_irq_name, sfp);
		if (err) {
			sfp->gpio_irq[i] = 0;
			sfp->need_poll = true;
		}
	}

	if (sfp->need_poll)
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

	rtnl_lock();
	sfp_sm_event(sfp, SFP_E_REMOVE);
	rtnl_unlock();

	return 0;
}

static void sfp_shutdown(struct platform_device *pdev)
{
	struct sfp *sfp = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < GPIO_MAX; i++) {
		if (!sfp->gpio_irq[i])
			continue;

		devm_free_irq(sfp->dev, sfp->gpio_irq[i], sfp);
	}

	cancel_delayed_work_sync(&sfp->poll);
	cancel_delayed_work_sync(&sfp->timeout);
}

static struct platform_driver sfp_driver = {
	.probe = sfp_probe,
	.remove = sfp_remove,
	.shutdown = sfp_shutdown,
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

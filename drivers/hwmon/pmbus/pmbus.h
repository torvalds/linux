/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * pmbus.h - Common defines and structures for PMBus devices
 *
 * Copyright (c) 2010, 2011 Ericsson AB.
 * Copyright (c) 2012 Guenter Roeck
 */

#ifndef PMBUS_H
#define PMBUS_H

#include <linux/bitops.h>
#include <linux/regulator/driver.h>

/*
 * Registers
 */
enum pmbus_regs {
	PMBUS_PAGE			= 0x00,
	PMBUS_OPERATION			= 0x01,
	PMBUS_ON_OFF_CONFIG		= 0x02,
	PMBUS_CLEAR_FAULTS		= 0x03,
	PMBUS_PHASE			= 0x04,

	PMBUS_WRITE_PROTECT		= 0x10,

	PMBUS_CAPABILITY		= 0x19,
	PMBUS_QUERY			= 0x1A,

	PMBUS_VOUT_MODE			= 0x20,
	PMBUS_VOUT_COMMAND		= 0x21,
	PMBUS_VOUT_TRIM			= 0x22,
	PMBUS_VOUT_CAL_OFFSET		= 0x23,
	PMBUS_VOUT_MAX			= 0x24,
	PMBUS_VOUT_MARGIN_HIGH		= 0x25,
	PMBUS_VOUT_MARGIN_LOW		= 0x26,
	PMBUS_VOUT_TRANSITION_RATE	= 0x27,
	PMBUS_VOUT_DROOP		= 0x28,
	PMBUS_VOUT_SCALE_LOOP		= 0x29,
	PMBUS_VOUT_SCALE_MONITOR	= 0x2A,

	PMBUS_COEFFICIENTS		= 0x30,
	PMBUS_POUT_MAX			= 0x31,

	PMBUS_FAN_CONFIG_12		= 0x3A,
	PMBUS_FAN_COMMAND_1		= 0x3B,
	PMBUS_FAN_COMMAND_2		= 0x3C,
	PMBUS_FAN_CONFIG_34		= 0x3D,
	PMBUS_FAN_COMMAND_3		= 0x3E,
	PMBUS_FAN_COMMAND_4		= 0x3F,

	PMBUS_VOUT_OV_FAULT_LIMIT	= 0x40,
	PMBUS_VOUT_OV_FAULT_RESPONSE	= 0x41,
	PMBUS_VOUT_OV_WARN_LIMIT	= 0x42,
	PMBUS_VOUT_UV_WARN_LIMIT	= 0x43,
	PMBUS_VOUT_UV_FAULT_LIMIT	= 0x44,
	PMBUS_VOUT_UV_FAULT_RESPONSE	= 0x45,
	PMBUS_IOUT_OC_FAULT_LIMIT	= 0x46,
	PMBUS_IOUT_OC_FAULT_RESPONSE	= 0x47,
	PMBUS_IOUT_OC_LV_FAULT_LIMIT	= 0x48,
	PMBUS_IOUT_OC_LV_FAULT_RESPONSE	= 0x49,
	PMBUS_IOUT_OC_WARN_LIMIT	= 0x4A,
	PMBUS_IOUT_UC_FAULT_LIMIT	= 0x4B,
	PMBUS_IOUT_UC_FAULT_RESPONSE	= 0x4C,

	PMBUS_OT_FAULT_LIMIT		= 0x4F,
	PMBUS_OT_FAULT_RESPONSE		= 0x50,
	PMBUS_OT_WARN_LIMIT		= 0x51,
	PMBUS_UT_WARN_LIMIT		= 0x52,
	PMBUS_UT_FAULT_LIMIT		= 0x53,
	PMBUS_UT_FAULT_RESPONSE		= 0x54,
	PMBUS_VIN_OV_FAULT_LIMIT	= 0x55,
	PMBUS_VIN_OV_FAULT_RESPONSE	= 0x56,
	PMBUS_VIN_OV_WARN_LIMIT		= 0x57,
	PMBUS_VIN_UV_WARN_LIMIT		= 0x58,
	PMBUS_VIN_UV_FAULT_LIMIT	= 0x59,

	PMBUS_IIN_OC_FAULT_LIMIT	= 0x5B,
	PMBUS_IIN_OC_WARN_LIMIT		= 0x5D,

	PMBUS_POUT_OP_FAULT_LIMIT	= 0x68,
	PMBUS_POUT_OP_WARN_LIMIT	= 0x6A,
	PMBUS_PIN_OP_WARN_LIMIT		= 0x6B,

	PMBUS_STATUS_BYTE		= 0x78,
	PMBUS_STATUS_WORD		= 0x79,
	PMBUS_STATUS_VOUT		= 0x7A,
	PMBUS_STATUS_IOUT		= 0x7B,
	PMBUS_STATUS_INPUT		= 0x7C,
	PMBUS_STATUS_TEMPERATURE	= 0x7D,
	PMBUS_STATUS_CML		= 0x7E,
	PMBUS_STATUS_OTHER		= 0x7F,
	PMBUS_STATUS_MFR_SPECIFIC	= 0x80,
	PMBUS_STATUS_FAN_12		= 0x81,
	PMBUS_STATUS_FAN_34		= 0x82,

	PMBUS_READ_VIN			= 0x88,
	PMBUS_READ_IIN			= 0x89,
	PMBUS_READ_VCAP			= 0x8A,
	PMBUS_READ_VOUT			= 0x8B,
	PMBUS_READ_IOUT			= 0x8C,
	PMBUS_READ_TEMPERATURE_1	= 0x8D,
	PMBUS_READ_TEMPERATURE_2	= 0x8E,
	PMBUS_READ_TEMPERATURE_3	= 0x8F,
	PMBUS_READ_FAN_SPEED_1		= 0x90,
	PMBUS_READ_FAN_SPEED_2		= 0x91,
	PMBUS_READ_FAN_SPEED_3		= 0x92,
	PMBUS_READ_FAN_SPEED_4		= 0x93,
	PMBUS_READ_DUTY_CYCLE		= 0x94,
	PMBUS_READ_FREQUENCY		= 0x95,
	PMBUS_READ_POUT			= 0x96,
	PMBUS_READ_PIN			= 0x97,

	PMBUS_REVISION			= 0x98,
	PMBUS_MFR_ID			= 0x99,
	PMBUS_MFR_MODEL			= 0x9A,
	PMBUS_MFR_REVISION		= 0x9B,
	PMBUS_MFR_LOCATION		= 0x9C,
	PMBUS_MFR_DATE			= 0x9D,
	PMBUS_MFR_SERIAL		= 0x9E,

	PMBUS_MFR_VIN_MIN		= 0xA0,
	PMBUS_MFR_VIN_MAX		= 0xA1,
	PMBUS_MFR_IIN_MAX		= 0xA2,
	PMBUS_MFR_PIN_MAX		= 0xA3,
	PMBUS_MFR_VOUT_MIN		= 0xA4,
	PMBUS_MFR_VOUT_MAX		= 0xA5,
	PMBUS_MFR_IOUT_MAX		= 0xA6,
	PMBUS_MFR_POUT_MAX		= 0xA7,

	PMBUS_IC_DEVICE_ID		= 0xAD,
	PMBUS_IC_DEVICE_REV		= 0xAE,

	PMBUS_MFR_MAX_TEMP_1		= 0xC0,
	PMBUS_MFR_MAX_TEMP_2		= 0xC1,
	PMBUS_MFR_MAX_TEMP_3		= 0xC2,

/*
 * Virtual registers.
 * Useful to support attributes which are not supported by standard PMBus
 * registers but exist as manufacturer specific registers on individual chips.
 * Must be mapped to real registers in device specific code.
 *
 * Semantics:
 * Virtual registers are all word size.
 * READ registers are read-only; writes are either ignored or return an error.
 * RESET registers are read/write. Reading reset registers returns zero
 * (used for detection), writing any value causes the associated history to be
 * reset.
 * Virtual registers have to be handled in device specific driver code. Chip
 * driver code returns non-negative register values if a virtual register is
 * supported, or a negative error code if not. The chip driver may return
 * -ENODATA or any other error code in this case, though an error code other
 * than -ENODATA is handled more efficiently and thus preferred. Either case,
 * the calling PMBus core code will abort if the chip driver returns an error
 * code when reading or writing virtual registers.
 */
	PMBUS_VIRT_BASE			= 0x100,
	PMBUS_VIRT_READ_TEMP_AVG,
	PMBUS_VIRT_READ_TEMP_MIN,
	PMBUS_VIRT_READ_TEMP_MAX,
	PMBUS_VIRT_RESET_TEMP_HISTORY,
	PMBUS_VIRT_READ_VIN_AVG,
	PMBUS_VIRT_READ_VIN_MIN,
	PMBUS_VIRT_READ_VIN_MAX,
	PMBUS_VIRT_RESET_VIN_HISTORY,
	PMBUS_VIRT_READ_IIN_AVG,
	PMBUS_VIRT_READ_IIN_MIN,
	PMBUS_VIRT_READ_IIN_MAX,
	PMBUS_VIRT_RESET_IIN_HISTORY,
	PMBUS_VIRT_READ_PIN_AVG,
	PMBUS_VIRT_READ_PIN_MIN,
	PMBUS_VIRT_READ_PIN_MAX,
	PMBUS_VIRT_RESET_PIN_HISTORY,
	PMBUS_VIRT_READ_POUT_AVG,
	PMBUS_VIRT_READ_POUT_MIN,
	PMBUS_VIRT_READ_POUT_MAX,
	PMBUS_VIRT_RESET_POUT_HISTORY,
	PMBUS_VIRT_READ_VOUT_AVG,
	PMBUS_VIRT_READ_VOUT_MIN,
	PMBUS_VIRT_READ_VOUT_MAX,
	PMBUS_VIRT_RESET_VOUT_HISTORY,
	PMBUS_VIRT_READ_IOUT_AVG,
	PMBUS_VIRT_READ_IOUT_MIN,
	PMBUS_VIRT_READ_IOUT_MAX,
	PMBUS_VIRT_RESET_IOUT_HISTORY,
	PMBUS_VIRT_READ_TEMP2_AVG,
	PMBUS_VIRT_READ_TEMP2_MIN,
	PMBUS_VIRT_READ_TEMP2_MAX,
	PMBUS_VIRT_RESET_TEMP2_HISTORY,

	PMBUS_VIRT_READ_VMON,
	PMBUS_VIRT_VMON_UV_WARN_LIMIT,
	PMBUS_VIRT_VMON_OV_WARN_LIMIT,
	PMBUS_VIRT_VMON_UV_FAULT_LIMIT,
	PMBUS_VIRT_VMON_OV_FAULT_LIMIT,
	PMBUS_VIRT_STATUS_VMON,

	/*
	 * RPM and PWM Fan control
	 *
	 * Drivers wanting to expose PWM control must define the behaviour of
	 * PMBUS_VIRT_PWM_[1-4] and PMBUS_VIRT_PWM_ENABLE_[1-4] in the
	 * {read,write}_word_data callback.
	 *
	 * pmbus core provides a default implementation for
	 * PMBUS_VIRT_FAN_TARGET_[1-4].
	 *
	 * TARGET, PWM and PWM_ENABLE members must be defined sequentially;
	 * pmbus core uses the difference between the provided register and
	 * it's _1 counterpart to calculate the FAN/PWM ID.
	 */
	PMBUS_VIRT_FAN_TARGET_1,
	PMBUS_VIRT_FAN_TARGET_2,
	PMBUS_VIRT_FAN_TARGET_3,
	PMBUS_VIRT_FAN_TARGET_4,
	PMBUS_VIRT_PWM_1,
	PMBUS_VIRT_PWM_2,
	PMBUS_VIRT_PWM_3,
	PMBUS_VIRT_PWM_4,
	PMBUS_VIRT_PWM_ENABLE_1,
	PMBUS_VIRT_PWM_ENABLE_2,
	PMBUS_VIRT_PWM_ENABLE_3,
	PMBUS_VIRT_PWM_ENABLE_4,

	/* Samples for average
	 *
	 * Drivers wanting to expose functionality for changing the number of
	 * samples used for average values should implement support in
	 * {read,write}_word_data callback for either PMBUS_VIRT_SAMPLES if it
	 * applies to all types of measurements, or any number of specific
	 * PMBUS_VIRT_*_SAMPLES registers to allow for individual control.
	 */
	PMBUS_VIRT_SAMPLES,
	PMBUS_VIRT_IN_SAMPLES,
	PMBUS_VIRT_CURR_SAMPLES,
	PMBUS_VIRT_POWER_SAMPLES,
	PMBUS_VIRT_TEMP_SAMPLES,
};

/*
 * OPERATION
 */
#define PB_OPERATION_CONTROL_ON		BIT(7)

/*
 * WRITE_PROTECT
 */
#define PB_WP_ALL	BIT(7)	/* all but WRITE_PROTECT */
#define PB_WP_OP	BIT(6)	/* all but WP, OPERATION, PAGE */
#define PB_WP_VOUT	BIT(5)	/* all but WP, OPERATION, PAGE, VOUT, ON_OFF */

#define PB_WP_ANY	(PB_WP_ALL | PB_WP_OP | PB_WP_VOUT)

/*
 * CAPABILITY
 */
#define PB_CAPABILITY_SMBALERT		BIT(4)
#define PB_CAPABILITY_ERROR_CHECK	BIT(7)

/*
 * VOUT_MODE
 */
#define PB_VOUT_MODE_MODE_MASK		0xe0
#define PB_VOUT_MODE_PARAM_MASK		0x1f

#define PB_VOUT_MODE_LINEAR		0x00
#define PB_VOUT_MODE_VID		0x20
#define PB_VOUT_MODE_DIRECT		0x40

/*
 * Fan configuration
 */
#define PB_FAN_2_PULSE_MASK		(BIT(0) | BIT(1))
#define PB_FAN_2_RPM			BIT(2)
#define PB_FAN_2_INSTALLED		BIT(3)
#define PB_FAN_1_PULSE_MASK		(BIT(4) | BIT(5))
#define PB_FAN_1_RPM			BIT(6)
#define PB_FAN_1_INSTALLED		BIT(7)

enum pmbus_fan_mode { percent = 0, rpm };

/*
 * STATUS_BYTE, STATUS_WORD (lower)
 */
#define PB_STATUS_NONE_ABOVE		BIT(0)
#define PB_STATUS_CML			BIT(1)
#define PB_STATUS_TEMPERATURE		BIT(2)
#define PB_STATUS_VIN_UV		BIT(3)
#define PB_STATUS_IOUT_OC		BIT(4)
#define PB_STATUS_VOUT_OV		BIT(5)
#define PB_STATUS_OFF			BIT(6)
#define PB_STATUS_BUSY			BIT(7)

/*
 * STATUS_WORD (upper)
 */
#define PB_STATUS_UNKNOWN		BIT(8)
#define PB_STATUS_OTHER			BIT(9)
#define PB_STATUS_FANS			BIT(10)
#define PB_STATUS_POWER_GOOD_N		BIT(11)
#define PB_STATUS_WORD_MFR		BIT(12)
#define PB_STATUS_INPUT			BIT(13)
#define PB_STATUS_IOUT_POUT		BIT(14)
#define PB_STATUS_VOUT			BIT(15)

/*
 * STATUS_IOUT
 */
#define PB_POUT_OP_WARNING		BIT(0)
#define PB_POUT_OP_FAULT		BIT(1)
#define PB_POWER_LIMITING		BIT(2)
#define PB_CURRENT_SHARE_FAULT		BIT(3)
#define PB_IOUT_UC_FAULT		BIT(4)
#define PB_IOUT_OC_WARNING		BIT(5)
#define PB_IOUT_OC_LV_FAULT		BIT(6)
#define PB_IOUT_OC_FAULT		BIT(7)

/*
 * STATUS_VOUT, STATUS_INPUT
 */
#define PB_VOLTAGE_UV_FAULT		BIT(4)
#define PB_VOLTAGE_UV_WARNING		BIT(5)
#define PB_VOLTAGE_OV_WARNING		BIT(6)
#define PB_VOLTAGE_OV_FAULT		BIT(7)

/*
 * STATUS_INPUT
 */
#define PB_PIN_OP_WARNING		BIT(0)
#define PB_IIN_OC_WARNING		BIT(1)
#define PB_IIN_OC_FAULT			BIT(2)

/*
 * STATUS_TEMPERATURE
 */
#define PB_TEMP_UT_FAULT		BIT(4)
#define PB_TEMP_UT_WARNING		BIT(5)
#define PB_TEMP_OT_WARNING		BIT(6)
#define PB_TEMP_OT_FAULT		BIT(7)

/*
 * STATUS_FAN
 */
#define PB_FAN_AIRFLOW_WARNING		BIT(0)
#define PB_FAN_AIRFLOW_FAULT		BIT(1)
#define PB_FAN_FAN2_SPEED_OVERRIDE	BIT(2)
#define PB_FAN_FAN1_SPEED_OVERRIDE	BIT(3)
#define PB_FAN_FAN2_WARNING		BIT(4)
#define PB_FAN_FAN1_WARNING		BIT(5)
#define PB_FAN_FAN2_FAULT		BIT(6)
#define PB_FAN_FAN1_FAULT		BIT(7)

/*
 * CML_FAULT_STATUS
 */
#define PB_CML_FAULT_OTHER_MEM_LOGIC	BIT(0)
#define PB_CML_FAULT_OTHER_COMM		BIT(1)
#define PB_CML_FAULT_PROCESSOR		BIT(3)
#define PB_CML_FAULT_MEMORY		BIT(4)
#define PB_CML_FAULT_PACKET_ERROR	BIT(5)
#define PB_CML_FAULT_INVALID_DATA	BIT(6)
#define PB_CML_FAULT_INVALID_COMMAND	BIT(7)

enum pmbus_sensor_classes {
	PSC_VOLTAGE_IN = 0,
	PSC_VOLTAGE_OUT,
	PSC_CURRENT_IN,
	PSC_CURRENT_OUT,
	PSC_POWER,
	PSC_TEMPERATURE,
	PSC_FAN,
	PSC_PWM,
	PSC_NUM_CLASSES		/* Number of power sensor classes */
};

#define PMBUS_PAGES	32	/* Per PMBus specification */
#define PMBUS_PHASES	8	/* Maximum number of phases per page */

/* Functionality bit mask */
#define PMBUS_HAVE_VIN		BIT(0)
#define PMBUS_HAVE_VCAP		BIT(1)
#define PMBUS_HAVE_VOUT		BIT(2)
#define PMBUS_HAVE_IIN		BIT(3)
#define PMBUS_HAVE_IOUT		BIT(4)
#define PMBUS_HAVE_PIN		BIT(5)
#define PMBUS_HAVE_POUT		BIT(6)
#define PMBUS_HAVE_FAN12	BIT(7)
#define PMBUS_HAVE_FAN34	BIT(8)
#define PMBUS_HAVE_TEMP		BIT(9)
#define PMBUS_HAVE_TEMP2	BIT(10)
#define PMBUS_HAVE_TEMP3	BIT(11)
#define PMBUS_HAVE_STATUS_VOUT	BIT(12)
#define PMBUS_HAVE_STATUS_IOUT	BIT(13)
#define PMBUS_HAVE_STATUS_INPUT	BIT(14)
#define PMBUS_HAVE_STATUS_TEMP	BIT(15)
#define PMBUS_HAVE_STATUS_FAN12	BIT(16)
#define PMBUS_HAVE_STATUS_FAN34	BIT(17)
#define PMBUS_HAVE_VMON		BIT(18)
#define PMBUS_HAVE_STATUS_VMON	BIT(19)
#define PMBUS_HAVE_PWM12	BIT(20)
#define PMBUS_HAVE_PWM34	BIT(21)
#define PMBUS_HAVE_SAMPLES	BIT(22)

#define PMBUS_PHASE_VIRTUAL	BIT(30)	/* Phases on this page are virtual */
#define PMBUS_PAGE_VIRTUAL	BIT(31)	/* Page is virtual */

enum pmbus_data_format { linear = 0, direct, vid };
enum vrm_version { vr11 = 0, vr12, vr13, imvp9, amd625mv };

struct pmbus_driver_info {
	int pages;		/* Total number of pages */
	u8 phases[PMBUS_PAGES];	/* Number of phases per page */
	enum pmbus_data_format format[PSC_NUM_CLASSES];
	enum vrm_version vrm_version[PMBUS_PAGES]; /* vrm version per page */
	/*
	 * Support one set of coefficients for each sensor type
	 * Used for chips providing data in direct mode.
	 */
	int m[PSC_NUM_CLASSES];	/* mantissa for direct data format */
	int b[PSC_NUM_CLASSES];	/* offset */
	int R[PSC_NUM_CLASSES];	/* exponent */

	u32 func[PMBUS_PAGES];	/* Functionality, per page */
	u32 pfunc[PMBUS_PHASES];/* Functionality, per phase */
	/*
	 * The following functions map manufacturing specific register values
	 * to PMBus standard register values. Specify only if mapping is
	 * necessary.
	 * Functions return the register value (read) or zero (write) if
	 * successful. A return value of -ENODATA indicates that there is no
	 * manufacturer specific register, but that a standard PMBus register
	 * may exist. Any other negative return value indicates that the
	 * register does not exist, and that no attempt should be made to read
	 * the standard register.
	 */
	int (*read_byte_data)(struct i2c_client *client, int page, int reg);
	int (*read_word_data)(struct i2c_client *client, int page, int phase,
			      int reg);
	int (*write_word_data)(struct i2c_client *client, int page, int reg,
			       u16 word);
	int (*write_byte)(struct i2c_client *client, int page, u8 value);
	/*
	 * The identify function determines supported PMBus functionality.
	 * This function is only necessary if a chip driver supports multiple
	 * chips, and the chip functionality is not pre-determined.
	 */
	int (*identify)(struct i2c_client *client,
			struct pmbus_driver_info *info);

	/* Regulator functionality, if supported by this chip driver. */
	int num_regulators;
	const struct regulator_desc *reg_desc;

	/* custom attributes */
	const struct attribute_group **groups;
};

/* Regulator ops */

extern const struct regulator_ops pmbus_regulator_ops;

/* Macro for filling in array of struct regulator_desc */
#define PMBUS_REGULATOR(_name, _id)				\
	[_id] = {						\
		.name = (_name # _id),				\
		.id = (_id),					\
		.of_match = of_match_ptr(_name # _id),		\
		.regulators_node = of_match_ptr("regulators"),	\
		.ops = &pmbus_regulator_ops,			\
		.type = REGULATOR_VOLTAGE,			\
		.owner = THIS_MODULE,				\
	}

/* Function declarations */

void pmbus_clear_cache(struct i2c_client *client);
int pmbus_set_page(struct i2c_client *client, int page, int phase);
int pmbus_read_word_data(struct i2c_client *client, int page, int phase,
			 u8 reg);
int pmbus_write_word_data(struct i2c_client *client, int page, u8 reg,
			  u16 word);
int pmbus_read_byte_data(struct i2c_client *client, int page, u8 reg);
int pmbus_write_byte(struct i2c_client *client, int page, u8 value);
int pmbus_write_byte_data(struct i2c_client *client, int page, u8 reg,
			  u8 value);
int pmbus_update_byte_data(struct i2c_client *client, int page, u8 reg,
			   u8 mask, u8 value);
void pmbus_clear_faults(struct i2c_client *client);
bool pmbus_check_byte_register(struct i2c_client *client, int page, int reg);
bool pmbus_check_word_register(struct i2c_client *client, int page, int reg);
int pmbus_do_probe(struct i2c_client *client, struct pmbus_driver_info *info);
const struct pmbus_driver_info *pmbus_get_driver_info(struct i2c_client
						      *client);
int pmbus_get_fan_rate_device(struct i2c_client *client, int page, int id,
			      enum pmbus_fan_mode mode);
int pmbus_get_fan_rate_cached(struct i2c_client *client, int page, int id,
			      enum pmbus_fan_mode mode);
int pmbus_update_fan(struct i2c_client *client, int page, int id,
		     u8 config, u8 mask, u16 command);
struct dentry *pmbus_get_debugfs_dir(struct i2c_client *client);

#endif /* PMBUS_H */

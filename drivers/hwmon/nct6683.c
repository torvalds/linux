// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * nct6683 - Driver for the hardware monitoring functionality of
 *	     Nuvoton NCT6683D/NCT6686D/NCT6687D eSIO
 *
 * Copyright (C) 2013  Guenter Roeck <linux@roeck-us.net>
 *
 * Derived from nct6775 driver
 * Copyright (C) 2012, 2013  Guenter Roeck <linux@roeck-us.net>
 *
 * Supports the following chips:
 *
 * Chip        #vin    #fan    #pwm    #temp  chip ID
 * nct6683d     21(1)   16      8       32(1) 0xc730
 * nct6686d     21(1)   16      8       32(1) 0xd440
 * nct6687d     21(1)   16      8       32(1) 0xd590
 *
 * Notes:
 *	(1) Total number of vin and temp inputs is 32.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

enum kinds { nct6683, nct6686, nct6687 };

static bool force;
module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Set to one to enable support for unknown vendors");

static const char * const nct6683_device_names[] = {
	"nct6683",
	"nct6686",
	"nct6687",
};

static const char * const nct6683_chip_names[] = {
	"NCT6683D",
	"NCT6686D",
	"NCT6687D",
};

#define DRVNAME "nct6683"

/*
 * Super-I/O constants and functions
 */

#define NCT6683_LD_ACPI		0x0a
#define NCT6683_LD_HWM		0x0b
#define NCT6683_LD_VID		0x0d

#define SIO_REG_LDSEL		0x07	/* Logical device select */
#define SIO_REG_DEVID		0x20	/* Device ID (2 bytes) */
#define SIO_REG_ENABLE		0x30	/* Logical device enable */
#define SIO_REG_ADDR		0x60	/* Logical device address (2 bytes) */

#define SIO_NCT6681_ID		0xb270	/* for later */
#define SIO_NCT6683_ID		0xc730
#define SIO_NCT6686_ID		0xd440
#define SIO_NCT6687_ID		0xd590
#define SIO_ID_MASK		0xFFF0

static inline void
superio_outb(int ioreg, int reg, int val)
{
	outb(reg, ioreg);
	outb(val, ioreg + 1);
}

static inline int
superio_inb(int ioreg, int reg)
{
	outb(reg, ioreg);
	return inb(ioreg + 1);
}

static inline void
superio_select(int ioreg, int ld)
{
	outb(SIO_REG_LDSEL, ioreg);
	outb(ld, ioreg + 1);
}

static inline int
superio_enter(int ioreg)
{
	/*
	 * Try to reserve <ioreg> and <ioreg + 1> for exclusive access.
	 */
	if (!request_muxed_region(ioreg, 2, DRVNAME))
		return -EBUSY;

	outb(0x87, ioreg);
	outb(0x87, ioreg);

	return 0;
}

static inline void
superio_exit(int ioreg)
{
	outb(0xaa, ioreg);
	outb(0x02, ioreg);
	outb(0x02, ioreg + 1);
	release_region(ioreg, 2);
}

/*
 * ISA constants
 */

#define IOREGION_ALIGNMENT	(~7)
#define IOREGION_OFFSET		4	/* Use EC port 1 */
#define IOREGION_LENGTH		4

#define EC_PAGE_REG		0
#define EC_INDEX_REG		1
#define EC_DATA_REG		2
#define EC_EVENT_REG		3

/* Common and NCT6683 specific data */

#define NCT6683_NUM_REG_MON		32
#define NCT6683_NUM_REG_FAN		16
#define NCT6683_NUM_REG_PWM		8

#define NCT6683_REG_MON(x)		(0x100 + (x) * 2)
#define NCT6683_REG_FAN_RPM(x)		(0x140 + (x) * 2)
#define NCT6683_REG_PWM(x)		(0x160 + (x))
#define NCT6683_REG_PWM_WRITE(x)	(0xa28 + (x))

#define NCT6683_REG_MON_STS(x)		(0x174 + (x))
#define NCT6683_REG_IDLE(x)		(0x178 + (x))

#define NCT6683_REG_FAN_STS(x)		(0x17c + (x))
#define NCT6683_REG_FAN_ERRSTS		0x17e
#define NCT6683_REG_FAN_INITSTS		0x17f

#define NCT6683_HWM_CFG			0x180

#define NCT6683_REG_MON_CFG(x)		(0x1a0 + (x))
#define NCT6683_REG_FANIN_CFG(x)	(0x1c0 + (x))
#define NCT6683_REG_FANOUT_CFG(x)	(0x1d0 + (x))

#define NCT6683_REG_INTEL_TEMP_MAX(x)	(0x901 + (x) * 16)
#define NCT6683_REG_INTEL_TEMP_CRIT(x)	(0x90d + (x) * 16)

#define NCT6683_REG_TEMP_HYST(x)	(0x330 + (x))		/* 8 bit */
#define NCT6683_REG_TEMP_MAX(x)		(0x350 + (x))		/* 8 bit */
#define NCT6683_REG_MON_HIGH(x)		(0x370 + (x) * 2)	/* 8 bit */
#define NCT6683_REG_MON_LOW(x)		(0x371 + (x) * 2)	/* 8 bit */

#define NCT6683_REG_FAN_MIN(x)		(0x3b8 + (x) * 2)	/* 16 bit */

#define NCT6683_REG_FAN_CFG_CTRL	0xa01
#define NCT6683_FAN_CFG_REQ		0x80
#define NCT6683_FAN_CFG_DONE		0x40

#define NCT6683_REG_CUSTOMER_ID		0x602
#define NCT6683_CUSTOMER_ID_INTEL	0x805
#define NCT6683_CUSTOMER_ID_MITAC	0xa0e
#define NCT6683_CUSTOMER_ID_MSI		0x201
#define NCT6683_CUSTOMER_ID_ASROCK		0xe2c
#define NCT6683_CUSTOMER_ID_ASROCK2	0xe1b

#define NCT6683_REG_BUILD_YEAR		0x604
#define NCT6683_REG_BUILD_MONTH		0x605
#define NCT6683_REG_BUILD_DAY		0x606
#define NCT6683_REG_SERIAL		0x607
#define NCT6683_REG_VERSION_HI		0x608
#define NCT6683_REG_VERSION_LO		0x609

#define NCT6683_REG_CR_CASEOPEN		0xe8
#define NCT6683_CR_CASEOPEN_MASK	(1 << 7)

#define NCT6683_REG_CR_BEEP		0xe0
#define NCT6683_CR_BEEP_MASK		(1 << 6)

static const char *const nct6683_mon_label[] = {
	NULL,	/* disabled */
	"Local",
	"Diode 0 (curr)",
	"Diode 1 (curr)",
	"Diode 2 (curr)",
	"Diode 0 (volt)",
	"Diode 1 (volt)",
	"Diode 2 (volt)",
	"Thermistor 14",
	"Thermistor 15",
	"Thermistor 16",
	"Thermistor 0",
	"Thermistor 1",
	"Thermistor 2",
	"Thermistor 3",
	"Thermistor 4",
	"Thermistor 5",		/* 0x10 */
	"Thermistor 6",
	"Thermistor 7",
	"Thermistor 8",
	"Thermistor 9",
	"Thermistor 10",
	"Thermistor 11",
	"Thermistor 12",
	"Thermistor 13",
	NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	"PECI 0.0",		/* 0x20 */
	"PECI 1.0",
	"PECI 2.0",
	"PECI 3.0",
	"PECI 0.1",
	"PECI 1.1",
	"PECI 2.1",
	"PECI 3.1",
	"PECI DIMM 0",
	"PECI DIMM 1",
	"PECI DIMM 2",
	"PECI DIMM 3",
	NULL, NULL, NULL, NULL,
	"PCH CPU",		/* 0x30 */
	"PCH CHIP",
	"PCH CHIP CPU MAX",
	"PCH MCH",
	"PCH DIMM 0",
	"PCH DIMM 1",
	"PCH DIMM 2",
	"PCH DIMM 3",
	"SMBus 0",
	"SMBus 1",
	"SMBus 2",
	"SMBus 3",
	"SMBus 4",
	"SMBus 5",
	"DIMM 0",
	"DIMM 1",
	"DIMM 2",		/* 0x40 */
	"DIMM 3",
	"AMD TSI Addr 90h",
	"AMD TSI Addr 92h",
	"AMD TSI Addr 94h",
	"AMD TSI Addr 96h",
	"AMD TSI Addr 98h",
	"AMD TSI Addr 9ah",
	"AMD TSI Addr 9ch",
	"AMD TSI Addr 9dh",
	NULL, NULL, NULL, NULL, NULL, NULL,
	"Virtual 0",		/* 0x50 */
	"Virtual 1",
	"Virtual 2",
	"Virtual 3",
	"Virtual 4",
	"Virtual 5",
	"Virtual 6",
	"Virtual 7",
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	"VCC",			/* 0x60 voltage sensors */
	"VSB",
	"AVSB",
	"VTT",
	"VBAT",
	"VREF",
	"VIN0",
	"VIN1",
	"VIN2",
	"VIN3",
	"VIN4",
	"VIN5",
	"VIN6",
	"VIN7",
	"VIN8",
	"VIN9",
	"VIN10",
	"VIN11",
	"VIN12",
	"VIN13",
	"VIN14",
	"VIN15",
	"VIN16",
};

#define NUM_MON_LABELS		ARRAY_SIZE(nct6683_mon_label)
#define MON_VOLTAGE_START	0x60

/* ------------------------------------------------------- */

struct nct6683_data {
	int addr;		/* IO base of EC space */
	int sioreg;		/* SIO register */
	enum kinds kind;
	u16 customer_id;

	struct device *hwmon_dev;
	const struct attribute_group *groups[6];

	int temp_num;			/* number of temperature attributes */
	u8 temp_index[NCT6683_NUM_REG_MON];
	u8 temp_src[NCT6683_NUM_REG_MON];

	u8 in_num;			/* number of voltage attributes */
	u8 in_index[NCT6683_NUM_REG_MON];
	u8 in_src[NCT6683_NUM_REG_MON];

	struct mutex update_lock;	/* used to protect sensor updates */
	bool valid;			/* true if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	/* Voltage attribute values */
	u8 in[3][NCT6683_NUM_REG_MON];	/* [0]=in, [1]=in_max, [2]=in_min */

	/* Temperature attribute values */
	s16 temp_in[NCT6683_NUM_REG_MON];
	s8 temp[4][NCT6683_NUM_REG_MON];/* [0]=min, [1]=max, [2]=hyst,
					 * [3]=crit
					 */

	/* Fan attribute values */
	unsigned int rpm[NCT6683_NUM_REG_FAN];
	u16 fan_min[NCT6683_NUM_REG_FAN];
	u8 fanin_cfg[NCT6683_NUM_REG_FAN];
	u8 fanout_cfg[NCT6683_NUM_REG_FAN];
	u16 have_fan;			/* some fan inputs can be disabled */

	u8 have_pwm;
	u8 pwm[NCT6683_NUM_REG_PWM];

#ifdef CONFIG_PM
	/* Remember extra register values over suspend/resume */
	u8 hwm_cfg;
#endif
};

struct nct6683_sio_data {
	int sioreg;
	enum kinds kind;
};

struct sensor_device_template {
	struct device_attribute dev_attr;
	union {
		struct {
			u8 nr;
			u8 index;
		} s;
		int index;
	} u;
	bool s2;	/* true if both index and nr are used */
};

struct sensor_device_attr_u {
	union {
		struct sensor_device_attribute a1;
		struct sensor_device_attribute_2 a2;
	} u;
	char name[32];
};

#define __TEMPLATE_ATTR(_template, _mode, _show, _store) {	\
	.attr = {.name = _template, .mode = _mode },		\
	.show	= _show,					\
	.store	= _store,					\
}

#define SENSOR_DEVICE_TEMPLATE(_template, _mode, _show, _store, _index)	\
	{ .dev_attr = __TEMPLATE_ATTR(_template, _mode, _show, _store),	\
	  .u.index = _index,						\
	  .s2 = false }

#define SENSOR_DEVICE_TEMPLATE_2(_template, _mode, _show, _store,	\
				 _nr, _index)				\
	{ .dev_attr = __TEMPLATE_ATTR(_template, _mode, _show, _store),	\
	  .u.s.index = _index,						\
	  .u.s.nr = _nr,						\
	  .s2 = true }

#define SENSOR_TEMPLATE(_name, _template, _mode, _show, _store, _index)	\
static struct sensor_device_template sensor_dev_template_##_name	\
	= SENSOR_DEVICE_TEMPLATE(_template, _mode, _show, _store,	\
				 _index)

#define SENSOR_TEMPLATE_2(_name, _template, _mode, _show, _store,	\
			  _nr, _index)					\
static struct sensor_device_template sensor_dev_template_##_name	\
	= SENSOR_DEVICE_TEMPLATE_2(_template, _mode, _show, _store,	\
				 _nr, _index)

struct sensor_template_group {
	struct sensor_device_template **templates;
	umode_t (*is_visible)(struct kobject *, struct attribute *, int);
	int base;
};

static struct attribute_group *
nct6683_create_attr_group(struct device *dev,
			  const struct sensor_template_group *tg,
			  int repeat)
{
	struct sensor_device_attribute_2 *a2;
	struct sensor_device_attribute *a;
	struct sensor_device_template **t;
	struct sensor_device_attr_u *su;
	struct attribute_group *group;
	struct attribute **attrs;
	int i, j, count;

	if (repeat <= 0)
		return ERR_PTR(-EINVAL);

	t = tg->templates;
	for (count = 0; *t; t++, count++)
		;

	if (count == 0)
		return ERR_PTR(-EINVAL);

	group = devm_kzalloc(dev, sizeof(*group), GFP_KERNEL);
	if (group == NULL)
		return ERR_PTR(-ENOMEM);

	attrs = devm_kcalloc(dev, repeat * count + 1, sizeof(*attrs),
			     GFP_KERNEL);
	if (attrs == NULL)
		return ERR_PTR(-ENOMEM);

	su = devm_kzalloc(dev, array3_size(repeat, count, sizeof(*su)),
			  GFP_KERNEL);
	if (su == NULL)
		return ERR_PTR(-ENOMEM);

	group->attrs = attrs;
	group->is_visible = tg->is_visible;

	for (i = 0; i < repeat; i++) {
		t = tg->templates;
		for (j = 0; *t != NULL; j++) {
			snprintf(su->name, sizeof(su->name),
				 (*t)->dev_attr.attr.name, tg->base + i);
			if ((*t)->s2) {
				a2 = &su->u.a2;
				sysfs_attr_init(&a2->dev_attr.attr);
				a2->dev_attr.attr.name = su->name;
				a2->nr = (*t)->u.s.nr + i;
				a2->index = (*t)->u.s.index;
				a2->dev_attr.attr.mode =
				  (*t)->dev_attr.attr.mode;
				a2->dev_attr.show = (*t)->dev_attr.show;
				a2->dev_attr.store = (*t)->dev_attr.store;
				*attrs = &a2->dev_attr.attr;
			} else {
				a = &su->u.a1;
				sysfs_attr_init(&a->dev_attr.attr);
				a->dev_attr.attr.name = su->name;
				a->index = (*t)->u.index + i;
				a->dev_attr.attr.mode =
				  (*t)->dev_attr.attr.mode;
				a->dev_attr.show = (*t)->dev_attr.show;
				a->dev_attr.store = (*t)->dev_attr.store;
				*attrs = &a->dev_attr.attr;
			}
			attrs++;
			su++;
			t++;
		}
	}

	return group;
}

/* LSB is 16 mV, except for the following sources, where it is 32 mV */
#define MON_SRC_VCC	0x60
#define MON_SRC_VSB	0x61
#define MON_SRC_AVSB	0x62
#define MON_SRC_VBAT	0x64

static inline long in_from_reg(u16 reg, u8 src)
{
	int scale = 16;

	if (src == MON_SRC_VCC || src == MON_SRC_VSB || src == MON_SRC_AVSB ||
	    src == MON_SRC_VBAT)
		scale <<= 1;
	return reg * scale;
}

static u16 nct6683_read(struct nct6683_data *data, u16 reg)
{
	int res;

	outb_p(0xff, data->addr + EC_PAGE_REG);		/* unlock */
	outb_p(reg >> 8, data->addr + EC_PAGE_REG);
	outb_p(reg & 0xff, data->addr + EC_INDEX_REG);
	res = inb_p(data->addr + EC_DATA_REG);
	return res;
}

static u16 nct6683_read16(struct nct6683_data *data, u16 reg)
{
	return (nct6683_read(data, reg) << 8) | nct6683_read(data, reg + 1);
}

static void nct6683_write(struct nct6683_data *data, u16 reg, u16 value)
{
	outb_p(0xff, data->addr + EC_PAGE_REG);		/* unlock */
	outb_p(reg >> 8, data->addr + EC_PAGE_REG);
	outb_p(reg & 0xff, data->addr + EC_INDEX_REG);
	outb_p(value & 0xff, data->addr + EC_DATA_REG);
}

static int get_in_reg(struct nct6683_data *data, int nr, int index)
{
	int ch = data->in_index[index];
	int reg = -EINVAL;

	switch (nr) {
	case 0:
		reg = NCT6683_REG_MON(ch);
		break;
	case 1:
		if (data->customer_id != NCT6683_CUSTOMER_ID_INTEL)
			reg = NCT6683_REG_MON_LOW(ch);
		break;
	case 2:
		if (data->customer_id != NCT6683_CUSTOMER_ID_INTEL)
			reg = NCT6683_REG_MON_HIGH(ch);
		break;
	default:
		break;
	}
	return reg;
}

static int get_temp_reg(struct nct6683_data *data, int nr, int index)
{
	int ch = data->temp_index[index];
	int reg = -EINVAL;

	switch (data->customer_id) {
	case NCT6683_CUSTOMER_ID_INTEL:
		switch (nr) {
		default:
		case 1:	/* max */
			reg = NCT6683_REG_INTEL_TEMP_MAX(ch);
			break;
		case 3:	/* crit */
			reg = NCT6683_REG_INTEL_TEMP_CRIT(ch);
			break;
		}
		break;
	case NCT6683_CUSTOMER_ID_MITAC:
	default:
		switch (nr) {
		default:
		case 0:	/* min */
			reg = NCT6683_REG_MON_LOW(ch);
			break;
		case 1:	/* max */
			reg = NCT6683_REG_TEMP_MAX(ch);
			break;
		case 2:	/* hyst */
			reg = NCT6683_REG_TEMP_HYST(ch);
			break;
		case 3:	/* crit */
			reg = NCT6683_REG_MON_HIGH(ch);
			break;
		}
		break;
	}
	return reg;
}

static void nct6683_update_pwm(struct device *dev)
{
	struct nct6683_data *data = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < NCT6683_NUM_REG_PWM; i++) {
		if (!(data->have_pwm & (1 << i)))
			continue;
		data->pwm[i] = nct6683_read(data, NCT6683_REG_PWM(i));
	}
}

static struct nct6683_data *nct6683_update_device(struct device *dev)
{
	struct nct6683_data *data = dev_get_drvdata(dev);
	int i, j;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		/* Measured voltages and limits */
		for (i = 0; i < data->in_num; i++) {
			for (j = 0; j < 3; j++) {
				int reg = get_in_reg(data, j, i);

				if (reg >= 0)
					data->in[j][i] =
						nct6683_read(data, reg);
			}
		}

		/* Measured temperatures and limits */
		for (i = 0; i < data->temp_num; i++) {
			u8 ch = data->temp_index[i];

			data->temp_in[i] = nct6683_read16(data,
							  NCT6683_REG_MON(ch));
			for (j = 0; j < 4; j++) {
				int reg = get_temp_reg(data, j, i);

				if (reg >= 0)
					data->temp[j][i] =
						nct6683_read(data, reg);
			}
		}

		/* Measured fan speeds and limits */
		for (i = 0; i < ARRAY_SIZE(data->rpm); i++) {
			if (!(data->have_fan & (1 << i)))
				continue;

			data->rpm[i] = nct6683_read16(data,
						NCT6683_REG_FAN_RPM(i));
			data->fan_min[i] = nct6683_read16(data,
						NCT6683_REG_FAN_MIN(i));
		}

		nct6683_update_pwm(dev);

		data->last_updated = jiffies;
		data->valid = true;
	}

	mutex_unlock(&data->update_lock);
	return data;
}

/*
 * Sysfs callback functions
 */
static ssize_t
show_in_label(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	struct nct6683_data *data = nct6683_update_device(dev);
	int nr = sattr->index;

	return sprintf(buf, "%s\n", nct6683_mon_label[data->in_src[nr]]);
}

static ssize_t
show_in_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct6683_data *data = nct6683_update_device(dev);
	int index = sattr->index;
	int nr = sattr->nr;

	return sprintf(buf, "%ld\n",
		       in_from_reg(data->in[index][nr], data->in_index[index]));
}

static umode_t nct6683_in_is_visible(struct kobject *kobj,
				     struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nct6683_data *data = dev_get_drvdata(dev);
	int nr = index % 4;	/* attribute */

	/*
	 * Voltage limits exist for Intel boards,
	 * but register location and encoding is unknown
	 */
	if ((nr == 2 || nr == 3) &&
	    data->customer_id == NCT6683_CUSTOMER_ID_INTEL)
		return 0;

	return attr->mode;
}

SENSOR_TEMPLATE(in_label, "in%d_label", S_IRUGO, show_in_label, NULL, 0);
SENSOR_TEMPLATE_2(in_input, "in%d_input", S_IRUGO, show_in_reg, NULL, 0, 0);
SENSOR_TEMPLATE_2(in_min, "in%d_min", S_IRUGO, show_in_reg, NULL, 0, 1);
SENSOR_TEMPLATE_2(in_max, "in%d_max", S_IRUGO, show_in_reg, NULL, 0, 2);

static struct sensor_device_template *nct6683_attributes_in_template[] = {
	&sensor_dev_template_in_label,
	&sensor_dev_template_in_input,
	&sensor_dev_template_in_min,
	&sensor_dev_template_in_max,
	NULL
};

static const struct sensor_template_group nct6683_in_template_group = {
	.templates = nct6683_attributes_in_template,
	.is_visible = nct6683_in_is_visible,
};

static ssize_t
show_fan(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	struct nct6683_data *data = nct6683_update_device(dev);

	return sprintf(buf, "%d\n", data->rpm[sattr->index]);
}

static ssize_t
show_fan_min(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6683_data *data = nct6683_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;

	return sprintf(buf, "%d\n", data->fan_min[nr]);
}

static ssize_t
show_fan_pulses(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	struct nct6683_data *data = nct6683_update_device(dev);

	return sprintf(buf, "%d\n",
		       ((data->fanin_cfg[sattr->index] >> 5) & 0x03) + 1);
}

static umode_t nct6683_fan_is_visible(struct kobject *kobj,
				      struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nct6683_data *data = dev_get_drvdata(dev);
	int fan = index / 3;	/* fan index */
	int nr = index % 3;	/* attribute index */

	if (!(data->have_fan & (1 << fan)))
		return 0;

	/*
	 * Intel may have minimum fan speed limits,
	 * but register location and encoding are unknown.
	 */
	if (nr == 2 && data->customer_id == NCT6683_CUSTOMER_ID_INTEL)
		return 0;

	return attr->mode;
}

SENSOR_TEMPLATE(fan_input, "fan%d_input", S_IRUGO, show_fan, NULL, 0);
SENSOR_TEMPLATE(fan_pulses, "fan%d_pulses", S_IRUGO, show_fan_pulses, NULL, 0);
SENSOR_TEMPLATE(fan_min, "fan%d_min", S_IRUGO, show_fan_min, NULL, 0);

/*
 * nct6683_fan_is_visible uses the index into the following array
 * to determine if attributes should be created or not.
 * Any change in order or content must be matched.
 */
static struct sensor_device_template *nct6683_attributes_fan_template[] = {
	&sensor_dev_template_fan_input,
	&sensor_dev_template_fan_pulses,
	&sensor_dev_template_fan_min,
	NULL
};

static const struct sensor_template_group nct6683_fan_template_group = {
	.templates = nct6683_attributes_fan_template,
	.is_visible = nct6683_fan_is_visible,
	.base = 1,
};

static ssize_t
show_temp_label(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	struct nct6683_data *data = nct6683_update_device(dev);
	int nr = sattr->index;

	return sprintf(buf, "%s\n", nct6683_mon_label[data->temp_src[nr]]);
}

static ssize_t
show_temp8(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct6683_data *data = nct6683_update_device(dev);
	int index = sattr->index;
	int nr = sattr->nr;

	return sprintf(buf, "%d\n", data->temp[index][nr] * 1000);
}

static ssize_t
show_temp_hyst(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	struct nct6683_data *data = nct6683_update_device(dev);
	int nr = sattr->index;
	int temp = data->temp[1][nr] - data->temp[2][nr];

	return sprintf(buf, "%d\n", temp * 1000);
}

static ssize_t
show_temp16(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	struct nct6683_data *data = nct6683_update_device(dev);
	int index = sattr->index;

	return sprintf(buf, "%d\n", (data->temp_in[index] / 128) * 500);
}

/*
 * Temperature sensor type is determined by temperature source
 * and can not be modified.
 * 0x02..0x07: Thermal diode
 * 0x08..0x18: Thermistor
 * 0x20..0x2b: Intel PECI
 * 0x42..0x49: AMD TSI
 * Others are unspecified (not visible)
 */

static int get_temp_type(u8 src)
{
	if (src >= 0x02 && src <= 0x07)
		return 3;	/* thermal diode */
	else if (src >= 0x08 && src <= 0x18)
		return 4;	/* thermistor */
	else if (src >= 0x20 && src <= 0x2b)
		return 6;	/* PECI */
	else if (src >= 0x42 && src <= 0x49)
		return 5;

	return 0;
}

static ssize_t
show_temp_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6683_data *data = nct6683_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	return sprintf(buf, "%d\n", get_temp_type(data->temp_src[nr]));
}

static umode_t nct6683_temp_is_visible(struct kobject *kobj,
				       struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nct6683_data *data = dev_get_drvdata(dev);
	int temp = index / 7;	/* temp index */
	int nr = index % 7;	/* attribute index */

	/*
	 * Intel does not have low temperature limits or temperature hysteresis
	 * registers, or at least register location and encoding is unknown.
	 */
	if ((nr == 2 || nr == 4) &&
	    data->customer_id == NCT6683_CUSTOMER_ID_INTEL)
		return 0;

	if (nr == 6 && get_temp_type(data->temp_src[temp]) == 0)
		return 0;				/* type */

	return attr->mode;
}

SENSOR_TEMPLATE(temp_input, "temp%d_input", S_IRUGO, show_temp16, NULL, 0);
SENSOR_TEMPLATE(temp_label, "temp%d_label", S_IRUGO, show_temp_label, NULL, 0);
SENSOR_TEMPLATE_2(temp_min, "temp%d_min", S_IRUGO, show_temp8, NULL, 0, 0);
SENSOR_TEMPLATE_2(temp_max, "temp%d_max", S_IRUGO, show_temp8, NULL, 0, 1);
SENSOR_TEMPLATE(temp_max_hyst, "temp%d_max_hyst", S_IRUGO, show_temp_hyst, NULL,
		0);
SENSOR_TEMPLATE_2(temp_crit, "temp%d_crit", S_IRUGO, show_temp8, NULL, 0, 3);
SENSOR_TEMPLATE(temp_type, "temp%d_type", S_IRUGO, show_temp_type, NULL, 0);

/*
 * nct6683_temp_is_visible uses the index into the following array
 * to determine if attributes should be created or not.
 * Any change in order or content must be matched.
 */
static struct sensor_device_template *nct6683_attributes_temp_template[] = {
	&sensor_dev_template_temp_input,
	&sensor_dev_template_temp_label,
	&sensor_dev_template_temp_min,		/* 2 */
	&sensor_dev_template_temp_max,		/* 3 */
	&sensor_dev_template_temp_max_hyst,	/* 4 */
	&sensor_dev_template_temp_crit,		/* 5 */
	&sensor_dev_template_temp_type,		/* 6 */
	NULL
};

static const struct sensor_template_group nct6683_temp_template_group = {
	.templates = nct6683_attributes_temp_template,
	.is_visible = nct6683_temp_is_visible,
	.base = 1,
};

static ssize_t
show_pwm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6683_data *data = nct6683_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int index = sattr->index;

	return sprintf(buf, "%d\n", data->pwm[index]);
}

static ssize_t
store_pwm(struct device *dev, struct device_attribute *attr, const char *buf,
	  size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct6683_data *data = dev_get_drvdata(dev);
	int index = sattr->index;
	unsigned long val;

	if (kstrtoul(buf, 10, &val) || val > 255)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	nct6683_write(data, NCT6683_REG_FAN_CFG_CTRL, NCT6683_FAN_CFG_REQ);
	usleep_range(1000, 2000);
	nct6683_write(data, NCT6683_REG_PWM_WRITE(index), val);
	nct6683_write(data, NCT6683_REG_FAN_CFG_CTRL, NCT6683_FAN_CFG_DONE);
	mutex_unlock(&data->update_lock);

	return count;
}

SENSOR_TEMPLATE(pwm, "pwm%d", S_IRUGO, show_pwm, store_pwm, 0);

static umode_t nct6683_pwm_is_visible(struct kobject *kobj,
				      struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nct6683_data *data = dev_get_drvdata(dev);
	int pwm = index;	/* pwm index */

	if (!(data->have_pwm & (1 << pwm)))
		return 0;

	/* Only update pwm values for Mitac boards */
	if (data->customer_id == NCT6683_CUSTOMER_ID_MITAC)
		return attr->mode | S_IWUSR;

	return attr->mode;
}

static struct sensor_device_template *nct6683_attributes_pwm_template[] = {
	&sensor_dev_template_pwm,
	NULL
};

static const struct sensor_template_group nct6683_pwm_template_group = {
	.templates = nct6683_attributes_pwm_template,
	.is_visible = nct6683_pwm_is_visible,
	.base = 1,
};

static ssize_t
beep_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6683_data *data = dev_get_drvdata(dev);
	int ret;
	u8 reg;

	mutex_lock(&data->update_lock);

	ret = superio_enter(data->sioreg);
	if (ret)
		goto error;
	superio_select(data->sioreg, NCT6683_LD_HWM);
	reg = superio_inb(data->sioreg, NCT6683_REG_CR_BEEP);
	superio_exit(data->sioreg);

	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%u\n", !!(reg & NCT6683_CR_BEEP_MASK));

error:
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t
beep_enable_store(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct nct6683_data *data = dev_get_drvdata(dev);
	unsigned long val;
	u8 reg;
	int ret;

	if (kstrtoul(buf, 10, &val) || (val != 0 && val != 1))
		return -EINVAL;

	mutex_lock(&data->update_lock);

	ret = superio_enter(data->sioreg);
	if (ret) {
		count = ret;
		goto error;
	}

	superio_select(data->sioreg, NCT6683_LD_HWM);
	reg = superio_inb(data->sioreg, NCT6683_REG_CR_BEEP);
	if (val)
		reg |= NCT6683_CR_BEEP_MASK;
	else
		reg &= ~NCT6683_CR_BEEP_MASK;
	superio_outb(data->sioreg, NCT6683_REG_CR_BEEP, reg);
	superio_exit(data->sioreg);
error:
	mutex_unlock(&data->update_lock);
	return count;
}

/* Case open detection */

static ssize_t
intrusion0_alarm_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct nct6683_data *data = dev_get_drvdata(dev);
	int ret;
	u8 reg;

	mutex_lock(&data->update_lock);

	ret = superio_enter(data->sioreg);
	if (ret)
		goto error;
	superio_select(data->sioreg, NCT6683_LD_ACPI);
	reg = superio_inb(data->sioreg, NCT6683_REG_CR_CASEOPEN);
	superio_exit(data->sioreg);

	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%u\n", !(reg & NCT6683_CR_CASEOPEN_MASK));

error:
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t
intrusion0_alarm_store(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct nct6683_data *data = dev_get_drvdata(dev);
	unsigned long val;
	u8 reg;
	int ret;

	if (kstrtoul(buf, 10, &val) || val != 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	/*
	 * Use CR registers to clear caseopen status.
	 * Caseopen is activ low, clear by writing 1 into the register.
	 */

	ret = superio_enter(data->sioreg);
	if (ret) {
		count = ret;
		goto error;
	}

	superio_select(data->sioreg, NCT6683_LD_ACPI);
	reg = superio_inb(data->sioreg, NCT6683_REG_CR_CASEOPEN);
	reg |= NCT6683_CR_CASEOPEN_MASK;
	superio_outb(data->sioreg, NCT6683_REG_CR_CASEOPEN, reg);
	reg &= ~NCT6683_CR_CASEOPEN_MASK;
	superio_outb(data->sioreg, NCT6683_REG_CR_CASEOPEN, reg);
	superio_exit(data->sioreg);

	data->valid = false;	/* Force cache refresh */
error:
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR_RW(intrusion0_alarm);
static DEVICE_ATTR_RW(beep_enable);

static struct attribute *nct6683_attributes_other[] = {
	&dev_attr_intrusion0_alarm.attr,
	&dev_attr_beep_enable.attr,
	NULL
};

static const struct attribute_group nct6683_group_other = {
	.attrs = nct6683_attributes_other,
};

/* Get the monitoring functions started */
static inline void nct6683_init_device(struct nct6683_data *data)
{
	u8 tmp;

	/* Start hardware monitoring if needed */
	tmp = nct6683_read(data, NCT6683_HWM_CFG);
	if (!(tmp & 0x80))
		nct6683_write(data, NCT6683_HWM_CFG, tmp | 0x80);
}

/*
 * There are a total of 24 fan inputs. Each can be configured as input
 * or as output. A maximum of 16 inputs and 8 outputs is configurable.
 */
static void
nct6683_setup_fans(struct nct6683_data *data)
{
	int i;
	u8 reg;

	for (i = 0; i < NCT6683_NUM_REG_FAN; i++) {
		reg = nct6683_read(data, NCT6683_REG_FANIN_CFG(i));
		if (reg & 0x80)
			data->have_fan |= 1 << i;
		data->fanin_cfg[i] = reg;
	}
	for (i = 0; i < NCT6683_NUM_REG_PWM; i++) {
		reg = nct6683_read(data, NCT6683_REG_FANOUT_CFG(i));
		if (reg & 0x80)
			data->have_pwm |= 1 << i;
		data->fanout_cfg[i] = reg;
	}
}

/*
 * Translation from monitoring register to temperature and voltage attributes
 * ==========================================================================
 *
 * There are a total of 32 monitoring registers. Each can be assigned to either
 * a temperature or voltage monitoring source.
 * NCT6683_REG_MON_CFG(x) defines assignment for each monitoring source.
 *
 * Temperature and voltage attribute mapping is determined by walking through
 * the NCT6683_REG_MON_CFG registers. If the assigned source is
 * a temperature, temp_index[n] is set to the monitor register index, and
 * temp_src[n] is set to the temperature source. If the assigned source is
 * a voltage, the respective values are stored in in_index[] and in_src[],
 * respectively.
 */

static void nct6683_setup_sensors(struct nct6683_data *data)
{
	u8 reg;
	int i;

	data->temp_num = 0;
	data->in_num = 0;
	for (i = 0; i < NCT6683_NUM_REG_MON; i++) {
		reg = nct6683_read(data, NCT6683_REG_MON_CFG(i)) & 0x7f;
		/* Ignore invalid assignments */
		if (reg >= NUM_MON_LABELS)
			continue;
		/* Skip if disabled or reserved */
		if (nct6683_mon_label[reg] == NULL)
			continue;
		if (reg < MON_VOLTAGE_START) {
			data->temp_index[data->temp_num] = i;
			data->temp_src[data->temp_num] = reg;
			data->temp_num++;
		} else {
			data->in_index[data->in_num] = i;
			data->in_src[data->in_num] = reg;
			data->in_num++;
		}
	}
}

static int nct6683_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nct6683_sio_data *sio_data = dev->platform_data;
	struct attribute_group *group;
	struct nct6683_data *data;
	struct device *hwmon_dev;
	struct resource *res;
	int groups = 0;
	char build[16];

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!devm_request_region(dev, res->start, IOREGION_LENGTH, DRVNAME))
		return -EBUSY;

	data = devm_kzalloc(dev, sizeof(struct nct6683_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->kind = sio_data->kind;
	data->sioreg = sio_data->sioreg;
	data->addr = res->start;
	mutex_init(&data->update_lock);
	platform_set_drvdata(pdev, data);

	data->customer_id = nct6683_read16(data, NCT6683_REG_CUSTOMER_ID);

	/* By default only instantiate driver if the customer ID is known */
	switch (data->customer_id) {
	case NCT6683_CUSTOMER_ID_INTEL:
		break;
	case NCT6683_CUSTOMER_ID_MITAC:
		break;
	case NCT6683_CUSTOMER_ID_MSI:
		break;
	case NCT6683_CUSTOMER_ID_ASROCK:
		break;
	case NCT6683_CUSTOMER_ID_ASROCK2:
		break;
	default:
		if (!force)
			return -ENODEV;
	}

	nct6683_init_device(data);
	nct6683_setup_fans(data);
	nct6683_setup_sensors(data);

	/* Register sysfs hooks */

	if (data->have_pwm) {
		group = nct6683_create_attr_group(dev,
						  &nct6683_pwm_template_group,
						  fls(data->have_pwm));
		if (IS_ERR(group))
			return PTR_ERR(group);
		data->groups[groups++] = group;
	}

	if (data->in_num) {
		group = nct6683_create_attr_group(dev,
						  &nct6683_in_template_group,
						  data->in_num);
		if (IS_ERR(group))
			return PTR_ERR(group);
		data->groups[groups++] = group;
	}

	if (data->have_fan) {
		group = nct6683_create_attr_group(dev,
						  &nct6683_fan_template_group,
						  fls(data->have_fan));
		if (IS_ERR(group))
			return PTR_ERR(group);
		data->groups[groups++] = group;
	}

	if (data->temp_num) {
		group = nct6683_create_attr_group(dev,
						  &nct6683_temp_template_group,
						  data->temp_num);
		if (IS_ERR(group))
			return PTR_ERR(group);
		data->groups[groups++] = group;
	}
	data->groups[groups++] = &nct6683_group_other;

	if (data->customer_id == NCT6683_CUSTOMER_ID_INTEL)
		scnprintf(build, sizeof(build), "%02x/%02x/%02x",
			  nct6683_read(data, NCT6683_REG_BUILD_MONTH),
			  nct6683_read(data, NCT6683_REG_BUILD_DAY),
			  nct6683_read(data, NCT6683_REG_BUILD_YEAR));
	else
		scnprintf(build, sizeof(build), "%02d/%02d/%02d",
			  nct6683_read(data, NCT6683_REG_BUILD_MONTH),
			  nct6683_read(data, NCT6683_REG_BUILD_DAY),
			  nct6683_read(data, NCT6683_REG_BUILD_YEAR));

	dev_info(dev, "%s EC firmware version %d.%d build %s\n",
		 nct6683_chip_names[data->kind],
		 nct6683_read(data, NCT6683_REG_VERSION_HI),
		 nct6683_read(data, NCT6683_REG_VERSION_LO),
		 build);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev,
			nct6683_device_names[data->kind], data, data->groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

#ifdef CONFIG_PM
static int nct6683_suspend(struct device *dev)
{
	struct nct6683_data *data = nct6683_update_device(dev);

	mutex_lock(&data->update_lock);
	data->hwm_cfg = nct6683_read(data, NCT6683_HWM_CFG);
	mutex_unlock(&data->update_lock);

	return 0;
}

static int nct6683_resume(struct device *dev)
{
	struct nct6683_data *data = dev_get_drvdata(dev);

	mutex_lock(&data->update_lock);

	nct6683_write(data, NCT6683_HWM_CFG, data->hwm_cfg);

	/* Force re-reading all values */
	data->valid = false;
	mutex_unlock(&data->update_lock);

	return 0;
}

static const struct dev_pm_ops nct6683_dev_pm_ops = {
	.suspend = nct6683_suspend,
	.resume = nct6683_resume,
	.freeze = nct6683_suspend,
	.restore = nct6683_resume,
};

#define NCT6683_DEV_PM_OPS	(&nct6683_dev_pm_ops)
#else
#define NCT6683_DEV_PM_OPS	NULL
#endif /* CONFIG_PM */

static struct platform_driver nct6683_driver = {
	.driver = {
		.name	= DRVNAME,
		.pm	= NCT6683_DEV_PM_OPS,
	},
	.probe		= nct6683_probe,
};

static int __init nct6683_find(int sioaddr, struct nct6683_sio_data *sio_data)
{
	int addr;
	u16 val;
	int err;

	err = superio_enter(sioaddr);
	if (err)
		return err;

	val = (superio_inb(sioaddr, SIO_REG_DEVID) << 8)
	       | superio_inb(sioaddr, SIO_REG_DEVID + 1);

	switch (val & SIO_ID_MASK) {
	case SIO_NCT6683_ID:
		sio_data->kind = nct6683;
		break;
	case SIO_NCT6686_ID:
		sio_data->kind = nct6686;
		break;
	case SIO_NCT6687_ID:
		sio_data->kind = nct6687;
		break;
	default:
		if (val != 0xffff)
			pr_debug("unsupported chip ID: 0x%04x\n", val);
		goto fail;
	}

	/* We have a known chip, find the HWM I/O address */
	superio_select(sioaddr, NCT6683_LD_HWM);
	val = (superio_inb(sioaddr, SIO_REG_ADDR) << 8)
	    | superio_inb(sioaddr, SIO_REG_ADDR + 1);
	addr = val & IOREGION_ALIGNMENT;
	if (addr == 0) {
		pr_err("EC base I/O port unconfigured\n");
		goto fail;
	}

	/* Activate logical device if needed */
	val = superio_inb(sioaddr, SIO_REG_ENABLE);
	if (!(val & 0x01)) {
		pr_warn("Forcibly enabling EC access. Data may be unusable.\n");
		superio_outb(sioaddr, SIO_REG_ENABLE, val | 0x01);
	}

	superio_exit(sioaddr);
	pr_info("Found %s or compatible chip at %#x:%#x\n",
		nct6683_chip_names[sio_data->kind], sioaddr, addr);
	sio_data->sioreg = sioaddr;

	return addr;

fail:
	superio_exit(sioaddr);
	return -ENODEV;
}

/*
 * when Super-I/O functions move to a separate file, the Super-I/O
 * bus will manage the lifetime of the device and this module will only keep
 * track of the nct6683 driver. But since we use platform_device_alloc(), we
 * must keep track of the device
 */
static struct platform_device *pdev[2];

static int __init sensors_nct6683_init(void)
{
	struct nct6683_sio_data sio_data;
	int sioaddr[2] = { 0x2e, 0x4e };
	struct resource res;
	bool found = false;
	int address;
	int i, err;

	err = platform_driver_register(&nct6683_driver);
	if (err)
		return err;

	/*
	 * initialize sio_data->kind and sio_data->sioreg.
	 *
	 * when Super-I/O functions move to a separate file, the Super-I/O
	 * driver will probe 0x2e and 0x4e and auto-detect the presence of a
	 * nct6683 hardware monitor, and call probe()
	 */
	for (i = 0; i < ARRAY_SIZE(pdev); i++) {
		address = nct6683_find(sioaddr[i], &sio_data);
		if (address <= 0)
			continue;

		found = true;

		pdev[i] = platform_device_alloc(DRVNAME, address);
		if (!pdev[i]) {
			err = -ENOMEM;
			goto exit_device_unregister;
		}

		err = platform_device_add_data(pdev[i], &sio_data,
					       sizeof(struct nct6683_sio_data));
		if (err)
			goto exit_device_put;

		memset(&res, 0, sizeof(res));
		res.name = DRVNAME;
		res.start = address + IOREGION_OFFSET;
		res.end = address + IOREGION_OFFSET + IOREGION_LENGTH - 1;
		res.flags = IORESOURCE_IO;

		err = acpi_check_resource_conflict(&res);
		if (err) {
			platform_device_put(pdev[i]);
			pdev[i] = NULL;
			continue;
		}

		err = platform_device_add_resources(pdev[i], &res, 1);
		if (err)
			goto exit_device_put;

		/* platform_device_add calls probe() */
		err = platform_device_add(pdev[i]);
		if (err)
			goto exit_device_put;
	}
	if (!found) {
		err = -ENODEV;
		goto exit_unregister;
	}

	return 0;

exit_device_put:
	platform_device_put(pdev[i]);
exit_device_unregister:
	while (--i >= 0) {
		if (pdev[i])
			platform_device_unregister(pdev[i]);
	}
exit_unregister:
	platform_driver_unregister(&nct6683_driver);
	return err;
}

static void __exit sensors_nct6683_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pdev); i++) {
		if (pdev[i])
			platform_device_unregister(pdev[i]);
	}
	platform_driver_unregister(&nct6683_driver);
}

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("NCT6683D driver");
MODULE_LICENSE("GPL");

module_init(sensors_nct6683_init);
module_exit(sensors_nct6683_exit);

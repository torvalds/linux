// SPDX-License-Identifier: GPL-2.0+
/*
 * ADE7854/58/68/78 Polyphase Multifunction Energy Metering IC Driver
 *
 * Copyright 2010 Analog Devices Inc.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include "meter.h"
#include "ade7854.h"

static ssize_t ade7854_read_8bit(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	int ret;
	u32 val = 0;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = st->read_reg(dev, this_attr->address, &val, 8);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%u\n", val);
}

static ssize_t ade7854_read_16bit(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	int ret;
	u32 val = 0;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = st->read_reg(dev, this_attr->address, &val, 16);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%u\n", val);
}

static ssize_t ade7854_read_24bit(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	int ret;
	u32 val;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = st->read_reg(dev, this_attr->address, &val, 24);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%u\n", val);
}

static ssize_t ade7854_read_32bit(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	int ret;
	u32 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);

	ret = st->read_reg(dev, this_attr->address, &val, 32);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%u\n", val);
}

static ssize_t ade7854_write_8bit(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t len)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);

	int ret;
	u8 val;

	ret = kstrtou8(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = st->write_reg(dev, this_attr->address, val, 8);

error_ret:
	return ret ? ret : len;
}

static ssize_t ade7854_write_16bit(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t len)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);

	int ret;
	u16 val;

	ret = kstrtou16(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = st->write_reg(dev, this_attr->address, val, 16);

error_ret:
	return ret ? ret : len;
}

static ssize_t ade7854_write_24bit(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t len)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);

	int ret;
	u32 val;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = st->write_reg(dev, this_attr->address, val, 24);

error_ret:
	return ret ? ret : len;
}

static ssize_t ade7854_write_32bit(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t len)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);

	int ret;
	u32 val;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = st->write_reg(dev, this_attr->address, val, 32);

error_ret:
	return ret ? ret : len;
}

static int ade7854_reset(struct device *dev)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);
	u32 val;

	st->read_reg(dev, ADE7854_CONFIG, &val, 16);
	val |= BIT(7); /* Software Chip Reset */

	return st->write_reg(dev, ADE7854_CONFIG, val, 16);
}

static IIO_DEV_ATTR_AIGAIN(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_AIGAIN);
static IIO_DEV_ATTR_BIGAIN(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_BIGAIN);
static IIO_DEV_ATTR_CIGAIN(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_CIGAIN);
static IIO_DEV_ATTR_NIGAIN(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_NIGAIN);
static IIO_DEV_ATTR_AVGAIN(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_AVGAIN);
static IIO_DEV_ATTR_BVGAIN(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_BVGAIN);
static IIO_DEV_ATTR_CVGAIN(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_CVGAIN);
static IIO_DEV_ATTR_APPARENT_POWER_A_GAIN(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_AVAGAIN);
static IIO_DEV_ATTR_APPARENT_POWER_B_GAIN(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_BVAGAIN);
static IIO_DEV_ATTR_APPARENT_POWER_C_GAIN(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_CVAGAIN);
static IIO_DEV_ATTR_ACTIVE_POWER_A_OFFSET(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_AWATTOS);
static IIO_DEV_ATTR_ACTIVE_POWER_B_OFFSET(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_BWATTOS);
static IIO_DEV_ATTR_ACTIVE_POWER_C_OFFSET(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_CWATTOS);
static IIO_DEV_ATTR_REACTIVE_POWER_A_GAIN(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_AVARGAIN);
static IIO_DEV_ATTR_REACTIVE_POWER_B_GAIN(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_BVARGAIN);
static IIO_DEV_ATTR_REACTIVE_POWER_C_GAIN(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_CVARGAIN);
static IIO_DEV_ATTR_REACTIVE_POWER_A_OFFSET(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_AVAROS);
static IIO_DEV_ATTR_REACTIVE_POWER_B_OFFSET(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_BVAROS);
static IIO_DEV_ATTR_REACTIVE_POWER_C_OFFSET(0644,
		ade7854_read_24bit,
		ade7854_write_24bit,
		ADE7854_CVAROS);
static IIO_DEV_ATTR_VPEAK(0644,
		ade7854_read_32bit,
		ade7854_write_32bit,
		ADE7854_VPEAK);
static IIO_DEV_ATTR_IPEAK(0644,
		ade7854_read_32bit,
		ade7854_write_32bit,
		ADE7854_IPEAK);
static IIO_DEV_ATTR_APHCAL(0644,
		ade7854_read_16bit,
		ade7854_write_16bit,
		ADE7854_APHCAL);
static IIO_DEV_ATTR_BPHCAL(0644,
		ade7854_read_16bit,
		ade7854_write_16bit,
		ADE7854_BPHCAL);
static IIO_DEV_ATTR_CPHCAL(0644,
		ade7854_read_16bit,
		ade7854_write_16bit,
		ADE7854_CPHCAL);
static IIO_DEV_ATTR_CF1DEN(0644,
		ade7854_read_16bit,
		ade7854_write_16bit,
		ADE7854_CF1DEN);
static IIO_DEV_ATTR_CF2DEN(0644,
		ade7854_read_16bit,
		ade7854_write_16bit,
		ADE7854_CF2DEN);
static IIO_DEV_ATTR_CF3DEN(0644,
		ade7854_read_16bit,
		ade7854_write_16bit,
		ADE7854_CF3DEN);
static IIO_DEV_ATTR_LINECYC(0644,
		ade7854_read_16bit,
		ade7854_write_16bit,
		ADE7854_LINECYC);
static IIO_DEV_ATTR_SAGCYC(0644,
		ade7854_read_8bit,
		ade7854_write_8bit,
		ADE7854_SAGCYC);
static IIO_DEV_ATTR_CFCYC(0644,
		ade7854_read_8bit,
		ade7854_write_8bit,
		ADE7854_CFCYC);
static IIO_DEV_ATTR_PEAKCYC(0644,
		ade7854_read_8bit,
		ade7854_write_8bit,
		ADE7854_PEAKCYC);
static IIO_DEV_ATTR_CHKSUM(ade7854_read_24bit,
		ADE7854_CHECKSUM);
static IIO_DEV_ATTR_ANGLE0(ade7854_read_24bit,
		ADE7854_ANGLE0);
static IIO_DEV_ATTR_ANGLE1(ade7854_read_24bit,
		ADE7854_ANGLE1);
static IIO_DEV_ATTR_ANGLE2(ade7854_read_24bit,
		ADE7854_ANGLE2);
static IIO_DEV_ATTR_AIRMS(0444,
		ade7854_read_24bit,
		NULL,
		ADE7854_AIRMS);
static IIO_DEV_ATTR_BIRMS(0444,
		ade7854_read_24bit,
		NULL,
		ADE7854_BIRMS);
static IIO_DEV_ATTR_CIRMS(0444,
		ade7854_read_24bit,
		NULL,
		ADE7854_CIRMS);
static IIO_DEV_ATTR_NIRMS(0444,
		ade7854_read_24bit,
		NULL,
		ADE7854_NIRMS);
static IIO_DEV_ATTR_AVRMS(0444,
		ade7854_read_24bit,
		NULL,
		ADE7854_AVRMS);
static IIO_DEV_ATTR_BVRMS(0444,
		ade7854_read_24bit,
		NULL,
		ADE7854_BVRMS);
static IIO_DEV_ATTR_CVRMS(0444,
		ade7854_read_24bit,
		NULL,
		ADE7854_CVRMS);
static IIO_DEV_ATTR_AIRMSOS(0444,
		ade7854_read_16bit,
		ade7854_write_16bit,
		ADE7854_AIRMSOS);
static IIO_DEV_ATTR_BIRMSOS(0444,
		ade7854_read_16bit,
		ade7854_write_16bit,
		ADE7854_BIRMSOS);
static IIO_DEV_ATTR_CIRMSOS(0444,
		ade7854_read_16bit,
		ade7854_write_16bit,
		ADE7854_CIRMSOS);
static IIO_DEV_ATTR_AVRMSOS(0444,
		ade7854_read_16bit,
		ade7854_write_16bit,
		ADE7854_AVRMSOS);
static IIO_DEV_ATTR_BVRMSOS(0444,
		ade7854_read_16bit,
		ade7854_write_16bit,
		ADE7854_BVRMSOS);
static IIO_DEV_ATTR_CVRMSOS(0444,
		ade7854_read_16bit,
		ade7854_write_16bit,
		ADE7854_CVRMSOS);
static IIO_DEV_ATTR_VOLT_A(ade7854_read_24bit,
		ADE7854_VAWV);
static IIO_DEV_ATTR_VOLT_B(ade7854_read_24bit,
		ADE7854_VBWV);
static IIO_DEV_ATTR_VOLT_C(ade7854_read_24bit,
		ADE7854_VCWV);
static IIO_DEV_ATTR_CURRENT_A(ade7854_read_24bit,
		ADE7854_IAWV);
static IIO_DEV_ATTR_CURRENT_B(ade7854_read_24bit,
		ADE7854_IBWV);
static IIO_DEV_ATTR_CURRENT_C(ade7854_read_24bit,
		ADE7854_ICWV);
static IIO_DEV_ATTR_AWATTHR(ade7854_read_32bit,
		ADE7854_AWATTHR);
static IIO_DEV_ATTR_BWATTHR(ade7854_read_32bit,
		ADE7854_BWATTHR);
static IIO_DEV_ATTR_CWATTHR(ade7854_read_32bit,
		ADE7854_CWATTHR);
static IIO_DEV_ATTR_AFWATTHR(ade7854_read_32bit,
		ADE7854_AFWATTHR);
static IIO_DEV_ATTR_BFWATTHR(ade7854_read_32bit,
		ADE7854_BFWATTHR);
static IIO_DEV_ATTR_CFWATTHR(ade7854_read_32bit,
		ADE7854_CFWATTHR);
static IIO_DEV_ATTR_AVARHR(ade7854_read_32bit,
		ADE7854_AVARHR);
static IIO_DEV_ATTR_BVARHR(ade7854_read_32bit,
		ADE7854_BVARHR);
static IIO_DEV_ATTR_CVARHR(ade7854_read_32bit,
		ADE7854_CVARHR);
static IIO_DEV_ATTR_AVAHR(ade7854_read_32bit,
		ADE7854_AVAHR);
static IIO_DEV_ATTR_BVAHR(ade7854_read_32bit,
		ADE7854_BVAHR);
static IIO_DEV_ATTR_CVAHR(ade7854_read_32bit,
		ADE7854_CVAHR);

static int ade7854_set_irq(struct device *dev, bool enable)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);

	int ret;
	u32 irqen;

	ret = st->read_reg(dev, ADE7854_MASK0, &irqen, 32);
	if (ret < 0)
		return ret;

	if (enable)
		irqen |= BIT(17); /* 1: interrupt enabled when all periodical
				   * (at 8 kHz rate) DSP computations finish.
				   */
	else
		irqen &= ~BIT(17);

	return st->write_reg(dev, ADE7854_MASK0, irqen, 32);
}

static int ade7854_initial_setup(struct iio_dev *indio_dev)
{
	int ret;
	struct device *dev = &indio_dev->dev;

	/* Disable IRQ */
	ret = ade7854_set_irq(dev, false);
	if (ret) {
		dev_err(dev, "disable irq failed");
		goto err_ret;
	}

	ade7854_reset(dev);
	usleep_range(ADE7854_STARTUP_DELAY, ADE7854_STARTUP_DELAY + 100);

err_ret:
	return ret;
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("8000");

static IIO_CONST_ATTR(name, "ade7854");

static struct attribute *ade7854_attributes[] = {
	&iio_dev_attr_aigain.dev_attr.attr,
	&iio_dev_attr_bigain.dev_attr.attr,
	&iio_dev_attr_cigain.dev_attr.attr,
	&iio_dev_attr_nigain.dev_attr.attr,
	&iio_dev_attr_avgain.dev_attr.attr,
	&iio_dev_attr_bvgain.dev_attr.attr,
	&iio_dev_attr_cvgain.dev_attr.attr,
	&iio_dev_attr_linecyc.dev_attr.attr,
	&iio_dev_attr_sagcyc.dev_attr.attr,
	&iio_dev_attr_cfcyc.dev_attr.attr,
	&iio_dev_attr_peakcyc.dev_attr.attr,
	&iio_dev_attr_chksum.dev_attr.attr,
	&iio_dev_attr_apparent_power_a_gain.dev_attr.attr,
	&iio_dev_attr_apparent_power_b_gain.dev_attr.attr,
	&iio_dev_attr_apparent_power_c_gain.dev_attr.attr,
	&iio_dev_attr_active_power_a_offset.dev_attr.attr,
	&iio_dev_attr_active_power_b_offset.dev_attr.attr,
	&iio_dev_attr_active_power_c_offset.dev_attr.attr,
	&iio_dev_attr_reactive_power_a_gain.dev_attr.attr,
	&iio_dev_attr_reactive_power_b_gain.dev_attr.attr,
	&iio_dev_attr_reactive_power_c_gain.dev_attr.attr,
	&iio_dev_attr_reactive_power_a_offset.dev_attr.attr,
	&iio_dev_attr_reactive_power_b_offset.dev_attr.attr,
	&iio_dev_attr_reactive_power_c_offset.dev_attr.attr,
	&iio_dev_attr_awatthr.dev_attr.attr,
	&iio_dev_attr_bwatthr.dev_attr.attr,
	&iio_dev_attr_cwatthr.dev_attr.attr,
	&iio_dev_attr_afwatthr.dev_attr.attr,
	&iio_dev_attr_bfwatthr.dev_attr.attr,
	&iio_dev_attr_cfwatthr.dev_attr.attr,
	&iio_dev_attr_avarhr.dev_attr.attr,
	&iio_dev_attr_bvarhr.dev_attr.attr,
	&iio_dev_attr_cvarhr.dev_attr.attr,
	&iio_dev_attr_angle0.dev_attr.attr,
	&iio_dev_attr_angle1.dev_attr.attr,
	&iio_dev_attr_angle2.dev_attr.attr,
	&iio_dev_attr_avahr.dev_attr.attr,
	&iio_dev_attr_bvahr.dev_attr.attr,
	&iio_dev_attr_cvahr.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_const_attr_name.dev_attr.attr,
	&iio_dev_attr_vpeak.dev_attr.attr,
	&iio_dev_attr_ipeak.dev_attr.attr,
	&iio_dev_attr_aphcal.dev_attr.attr,
	&iio_dev_attr_bphcal.dev_attr.attr,
	&iio_dev_attr_cphcal.dev_attr.attr,
	&iio_dev_attr_cf1den.dev_attr.attr,
	&iio_dev_attr_cf2den.dev_attr.attr,
	&iio_dev_attr_cf3den.dev_attr.attr,
	&iio_dev_attr_airms.dev_attr.attr,
	&iio_dev_attr_birms.dev_attr.attr,
	&iio_dev_attr_cirms.dev_attr.attr,
	&iio_dev_attr_nirms.dev_attr.attr,
	&iio_dev_attr_avrms.dev_attr.attr,
	&iio_dev_attr_bvrms.dev_attr.attr,
	&iio_dev_attr_cvrms.dev_attr.attr,
	&iio_dev_attr_airmsos.dev_attr.attr,
	&iio_dev_attr_birmsos.dev_attr.attr,
	&iio_dev_attr_cirmsos.dev_attr.attr,
	&iio_dev_attr_avrmsos.dev_attr.attr,
	&iio_dev_attr_bvrmsos.dev_attr.attr,
	&iio_dev_attr_cvrmsos.dev_attr.attr,
	&iio_dev_attr_volt_a.dev_attr.attr,
	&iio_dev_attr_volt_b.dev_attr.attr,
	&iio_dev_attr_volt_c.dev_attr.attr,
	&iio_dev_attr_current_a.dev_attr.attr,
	&iio_dev_attr_current_b.dev_attr.attr,
	&iio_dev_attr_current_c.dev_attr.attr,
	NULL,
};

static const struct attribute_group ade7854_attribute_group = {
	.attrs = ade7854_attributes,
};

static const struct iio_info ade7854_info = {
	.attrs = &ade7854_attribute_group,
};

int ade7854_probe(struct iio_dev *indio_dev, struct device *dev)
{
	int ret;
	struct ade7854_state *st = iio_priv(indio_dev);
	/* setup the industrialio driver allocated elements */
	mutex_init(&st->buf_lock);

	indio_dev->dev.parent = dev;
	indio_dev->info = &ade7854_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return ret;

	/* Get the device into a sane initial state */
	return ade7854_initial_setup(indio_dev);
}
EXPORT_SYMBOL(ade7854_probe);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADE7854/58/68/78 Polyphase Energy Meter");
MODULE_LICENSE("GPL v2");

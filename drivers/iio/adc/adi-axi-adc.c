// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices Generic AXI ADC IP core
 * Link: https://wiki.analog.com/resources/fpga/docs/axi_adc_ip
 *
 * Copyright 2012-2020 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <linux/fpga/adi-axi-common.h>

#include <linux/iio/backend.h>
#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>

#include "ad7606_bus_iface.h"
/*
 * Register definitions:
 *   https://wiki.analog.com/resources/fpga/docs/axi_adc_ip#register_map
 */

/* ADC controls */

#define ADI_AXI_REG_RSTN			0x0040
#define   ADI_AXI_REG_RSTN_CE_N			BIT(2)
#define   ADI_AXI_REG_RSTN_MMCM_RSTN		BIT(1)
#define   ADI_AXI_REG_RSTN_RSTN			BIT(0)

#define ADI_AXI_ADC_REG_CONFIG			0x000c
#define   ADI_AXI_ADC_REG_CONFIG_CMOS_OR_LVDS_N	BIT(7)

#define ADI_AXI_ADC_REG_CTRL			0x0044
#define    ADI_AXI_ADC_CTRL_DDR_EDGESEL_MASK	BIT(1)

#define ADI_AXI_ADC_REG_CNTRL_3			0x004c
#define   AXI_AD485X_CNTRL_3_OS_EN_MSK		BIT(2)
#define   AXI_AD485X_CNTRL_3_PACKET_FORMAT_MSK	GENMASK(1, 0)
#define   AXI_AD485X_PACKET_FORMAT_20BIT	0x0
#define   AXI_AD485X_PACKET_FORMAT_24BIT	0x1
#define   AXI_AD485X_PACKET_FORMAT_32BIT	0x2

#define ADI_AXI_ADC_REG_DRP_STATUS		0x0074
#define   ADI_AXI_ADC_DRP_LOCKED		BIT(17)

/* ADC Channel controls */

#define ADI_AXI_REG_CHAN_CTRL(c)		(0x0400 + (c) * 0x40)
#define   ADI_AXI_REG_CHAN_CTRL_LB_OWR		BIT(11)
#define   ADI_AXI_REG_CHAN_CTRL_PN_SEL_OWR	BIT(10)
#define   ADI_AXI_REG_CHAN_CTRL_IQCOR_EN	BIT(9)
#define   ADI_AXI_REG_CHAN_CTRL_DCFILT_EN	BIT(8)
#define   ADI_AXI_REG_CHAN_CTRL_FMT_MASK	GENMASK(6, 4)
#define   ADI_AXI_REG_CHAN_CTRL_FMT_SIGNEXT	BIT(6)
#define   ADI_AXI_REG_CHAN_CTRL_FMT_TYPE	BIT(5)
#define   ADI_AXI_REG_CHAN_CTRL_FMT_EN		BIT(4)
#define   ADI_AXI_REG_CHAN_CTRL_PN_TYPE_OWR	BIT(1)
#define   ADI_AXI_REG_CHAN_CTRL_ENABLE		BIT(0)

#define ADI_AXI_ADC_REG_CHAN_STATUS(c)		(0x0404 + (c) * 0x40)
#define   ADI_AXI_ADC_CHAN_STAT_PN_MASK		GENMASK(2, 1)
/* out of sync */
#define   ADI_AXI_ADC_CHAN_STAT_PN_OOS		BIT(1)
/* spurious out of sync */
#define   ADI_AXI_ADC_CHAN_STAT_PN_ERR		BIT(2)

#define ADI_AXI_ADC_REG_CHAN_CTRL_3(c)		(0x0418 + (c) * 0x40)
#define   ADI_AXI_ADC_CHAN_PN_SEL_MASK		GENMASK(19, 16)

/* IO Delays */
#define ADI_AXI_ADC_REG_DELAY(l)		(0x0800 + (l) * 0x4)
#define   AXI_ADC_DELAY_CTRL_MASK		GENMASK(4, 0)

#define ADI_AXI_REG_CONFIG_WR			0x0080
#define ADI_AXI_REG_CONFIG_RD			0x0084
#define ADI_AXI_REG_CONFIG_CTRL			0x008c
#define   ADI_AXI_REG_CONFIG_CTRL_READ		0x03
#define   ADI_AXI_REG_CONFIG_CTRL_WRITE		0x01

#define ADI_AXI_ADC_MAX_IO_NUM_LANES		15

#define ADI_AXI_REG_CHAN_CTRL_DEFAULTS		\
	(ADI_AXI_REG_CHAN_CTRL_FMT_SIGNEXT |	\
	 ADI_AXI_REG_CHAN_CTRL_FMT_EN |		\
	 ADI_AXI_REG_CHAN_CTRL_ENABLE)

#define ADI_AXI_REG_READ_BIT			0x8000
#define ADI_AXI_REG_ADDRESS_MASK		0xff00
#define ADI_AXI_REG_VALUE_MASK			0x00ff

struct axi_adc_info {
	unsigned int version;
	const struct iio_backend_info *backend_info;
	bool has_child_nodes;
	const void *pdata;
	unsigned int pdata_sz;
};

struct adi_axi_adc_state {
	const struct axi_adc_info *info;
	struct regmap *regmap;
	struct device *dev;
	/* lock to protect multiple accesses to the device registers */
	struct mutex lock;
};

static int axi_adc_enable(struct iio_backend *back)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	unsigned int __val;
	int ret;

	guard(mutex)(&st->lock);
	ret = regmap_set_bits(st->regmap, ADI_AXI_REG_RSTN,
			      ADI_AXI_REG_RSTN_MMCM_RSTN);
	if (ret)
		return ret;

	/*
	 * Make sure the DRP (Dynamic Reconfiguration Port) is locked. Not all
	 * designs really use it but if they don't we still get the lock bit
	 * set. So let's do it all the time so the code is generic.
	 */
	ret = regmap_read_poll_timeout(st->regmap, ADI_AXI_ADC_REG_DRP_STATUS,
				       __val, __val & ADI_AXI_ADC_DRP_LOCKED,
				       100, 1000);
	if (ret)
		return ret;

	return regmap_set_bits(st->regmap, ADI_AXI_REG_RSTN,
			       ADI_AXI_REG_RSTN_RSTN | ADI_AXI_REG_RSTN_MMCM_RSTN);
}

static void axi_adc_disable(struct iio_backend *back)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	guard(mutex)(&st->lock);
	regmap_write(st->regmap, ADI_AXI_REG_RSTN, 0);
}

static int axi_adc_data_format_set(struct iio_backend *back, unsigned int chan,
				   const struct iio_backend_data_fmt *data)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	u32 val;

	if (!data->enable)
		return regmap_clear_bits(st->regmap,
					 ADI_AXI_REG_CHAN_CTRL(chan),
					 ADI_AXI_REG_CHAN_CTRL_FMT_EN);

	val = FIELD_PREP(ADI_AXI_REG_CHAN_CTRL_FMT_EN, true);
	if (data->sign_extend)
		val |= FIELD_PREP(ADI_AXI_REG_CHAN_CTRL_FMT_SIGNEXT, true);
	if (data->type == IIO_BACKEND_OFFSET_BINARY)
		val |= FIELD_PREP(ADI_AXI_REG_CHAN_CTRL_FMT_TYPE, true);

	return regmap_update_bits(st->regmap, ADI_AXI_REG_CHAN_CTRL(chan),
				  ADI_AXI_REG_CHAN_CTRL_FMT_MASK, val);
}

static int axi_adc_data_sample_trigger(struct iio_backend *back,
				       enum iio_backend_sample_trigger trigger)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	switch (trigger) {
	case IIO_BACKEND_SAMPLE_TRIGGER_EDGE_RISING:
		return regmap_clear_bits(st->regmap, ADI_AXI_ADC_REG_CTRL,
					 ADI_AXI_ADC_CTRL_DDR_EDGESEL_MASK);
	case IIO_BACKEND_SAMPLE_TRIGGER_EDGE_FALLING:
		return regmap_set_bits(st->regmap, ADI_AXI_ADC_REG_CTRL,
				       ADI_AXI_ADC_CTRL_DDR_EDGESEL_MASK);
	default:
		return -EINVAL;
	}
}

static int axi_adc_iodelays_set(struct iio_backend *back, unsigned int lane,
				unsigned int tap)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	int ret;
	u32 val;

	if (tap > FIELD_MAX(AXI_ADC_DELAY_CTRL_MASK))
		return -EINVAL;
	if (lane > ADI_AXI_ADC_MAX_IO_NUM_LANES)
		return -EINVAL;

	guard(mutex)(&st->lock);
	ret = regmap_write(st->regmap, ADI_AXI_ADC_REG_DELAY(lane), tap);
	if (ret)
		return ret;
	/*
	 * If readback is ~0, that means there are issues with the
	 * delay_clk.
	 */
	ret = regmap_read(st->regmap, ADI_AXI_ADC_REG_DELAY(lane), &val);
	if (ret)
		return ret;
	if (val == U32_MAX)
		return -EIO;

	return 0;
}

static int axi_adc_test_pattern_set(struct iio_backend *back,
				    unsigned int chan,
				    enum iio_backend_test_pattern pattern)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	switch (pattern) {
	case IIO_BACKEND_NO_TEST_PATTERN:
		/* nothing to do */
		return 0;
	case IIO_BACKEND_ADI_PRBS_9A:
		return regmap_update_bits(st->regmap, ADI_AXI_ADC_REG_CHAN_CTRL_3(chan),
					  ADI_AXI_ADC_CHAN_PN_SEL_MASK,
					  FIELD_PREP(ADI_AXI_ADC_CHAN_PN_SEL_MASK, 0));
	case IIO_BACKEND_ADI_PRBS_23A:
		return regmap_update_bits(st->regmap, ADI_AXI_ADC_REG_CHAN_CTRL_3(chan),
					  ADI_AXI_ADC_CHAN_PN_SEL_MASK,
					  FIELD_PREP(ADI_AXI_ADC_CHAN_PN_SEL_MASK, 1));
	default:
		return -EINVAL;
	}
}

static int axi_adc_read_chan_status(struct adi_axi_adc_state *st, unsigned int chan,
				    unsigned int *status)
{
	int ret;

	guard(mutex)(&st->lock);
	/* reset test bits by setting them */
	ret = regmap_write(st->regmap, ADI_AXI_ADC_REG_CHAN_STATUS(chan),
			   ADI_AXI_ADC_CHAN_STAT_PN_MASK);
	if (ret)
		return ret;

	/* let's give enough time to validate or erroring the incoming pattern */
	fsleep(1000);

	return regmap_read(st->regmap, ADI_AXI_ADC_REG_CHAN_STATUS(chan),
			   status);
}

static int axi_adc_chan_status(struct iio_backend *back, unsigned int chan,
			       bool *error)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	u32 val;
	int ret;

	ret = axi_adc_read_chan_status(st, chan, &val);
	if (ret)
		return ret;

	if (ADI_AXI_ADC_CHAN_STAT_PN_MASK & val)
		*error = true;
	else
		*error = false;

	return 0;
}

static int axi_adc_debugfs_print_chan_status(struct iio_backend *back,
					     unsigned int chan, char *buf,
					     size_t len)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	u32 val;
	int ret;

	ret = axi_adc_read_chan_status(st, chan, &val);
	if (ret)
		return ret;

	/*
	 * PN_ERR is cleared in case out of sync is set. Hence, no point in
	 * checking both bits.
	 */
	if (val & ADI_AXI_ADC_CHAN_STAT_PN_OOS)
		return scnprintf(buf, len, "CH%u: Out of Sync.\n", chan);
	if (val & ADI_AXI_ADC_CHAN_STAT_PN_ERR)
		return scnprintf(buf, len, "CH%u: Spurious Out of Sync.\n", chan);

	return scnprintf(buf, len, "CH%u: OK.\n", chan);
}

static int axi_adc_chan_enable(struct iio_backend *back, unsigned int chan)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	return regmap_set_bits(st->regmap, ADI_AXI_REG_CHAN_CTRL(chan),
			       ADI_AXI_REG_CHAN_CTRL_ENABLE);
}

static int axi_adc_chan_disable(struct iio_backend *back, unsigned int chan)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	return regmap_clear_bits(st->regmap, ADI_AXI_REG_CHAN_CTRL(chan),
				 ADI_AXI_REG_CHAN_CTRL_ENABLE);
}

static int axi_adc_interface_type_get(struct iio_backend *back,
				      enum iio_backend_interface_type *type)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	unsigned int val;
	int ret;

	ret = regmap_read(st->regmap, ADI_AXI_ADC_REG_CONFIG, &val);
	if (ret)
		return ret;

	if (val & ADI_AXI_ADC_REG_CONFIG_CMOS_OR_LVDS_N)
		*type = IIO_BACKEND_INTERFACE_SERIAL_CMOS;
	else
		*type = IIO_BACKEND_INTERFACE_SERIAL_LVDS;

	return 0;
}

static int axi_adc_ad485x_data_size_set(struct iio_backend *back,
					unsigned int size)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	unsigned int val;

	switch (size) {
	/*
	 * There are two different variants of the AXI AXI_AD485X IP block, a
	 * 16-bit and a 20-bit variant.
	 * The 0x0 value (AXI_AD485X_PACKET_FORMAT_20BIT) is corresponding also
	 * to the 16-bit variant of the IP block.
	 */
	case 16:
	case 20:
		val = AXI_AD485X_PACKET_FORMAT_20BIT;
		break;
	case 24:
		val = AXI_AD485X_PACKET_FORMAT_24BIT;
		break;
	/*
	 * The 0x2 (AXI_AD485X_PACKET_FORMAT_32BIT) corresponds only to the
	 * 20-bit variant of the IP block. Setting this value properly is
	 * ensured by the upper layers of the drivers calling the axi-adc
	 * functions.
	 * Also, for 16-bit IP block, the 0x2 (AXI_AD485X_PACKET_FORMAT_32BIT)
	 * value is handled as maximum size available which is 24-bit for this
	 * configuration.
	 */
	case 32:
		val = AXI_AD485X_PACKET_FORMAT_32BIT;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(st->regmap, ADI_AXI_ADC_REG_CNTRL_3,
				  AXI_AD485X_CNTRL_3_PACKET_FORMAT_MSK,
				  FIELD_PREP(AXI_AD485X_CNTRL_3_PACKET_FORMAT_MSK, val));
}

static int axi_adc_ad485x_oversampling_ratio_set(struct iio_backend *back,
					  unsigned int ratio)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	/* The current state of the function enables or disables the
	 * oversampling in REG_CNTRL_3 register. A ratio equal to 1 implies no
	 * oversampling, while a value greater than 1 implies oversampling being
	 * enabled.
	 */
	switch (ratio) {
	case 0:
		return -EINVAL;
	case 1:
		return regmap_clear_bits(st->regmap, ADI_AXI_ADC_REG_CNTRL_3,
					 AXI_AD485X_CNTRL_3_OS_EN_MSK);
	default:
		return regmap_set_bits(st->regmap, ADI_AXI_ADC_REG_CNTRL_3,
				       AXI_AD485X_CNTRL_3_OS_EN_MSK);
	}
}

static struct iio_buffer *axi_adc_request_buffer(struct iio_backend *back,
						 struct iio_dev *indio_dev)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	const char *dma_name;

	if (device_property_read_string(st->dev, "dma-names", &dma_name))
		dma_name = "rx";

	return iio_dmaengine_buffer_setup(st->dev, indio_dev, dma_name);
}

static int axi_adc_raw_write(struct iio_backend *back, u32 val)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	regmap_write(st->regmap, ADI_AXI_REG_CONFIG_WR, val);
	regmap_write(st->regmap, ADI_AXI_REG_CONFIG_CTRL,
		     ADI_AXI_REG_CONFIG_CTRL_WRITE);
	fsleep(100);
	regmap_write(st->regmap, ADI_AXI_REG_CONFIG_CTRL, 0x00);
	fsleep(100);

	return 0;
}

static int axi_adc_raw_read(struct iio_backend *back, u32 *val)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	regmap_write(st->regmap, ADI_AXI_REG_CONFIG_CTRL,
		     ADI_AXI_REG_CONFIG_CTRL_READ);
	fsleep(100);
	regmap_read(st->regmap, ADI_AXI_REG_CONFIG_RD, val);
	regmap_write(st->regmap, ADI_AXI_REG_CONFIG_CTRL, 0x00);
	fsleep(100);

	return 0;
}

static int ad7606_bus_reg_read(struct iio_backend *back, u32 reg, u32 *val)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	u32 addr, reg_val;

	guard(mutex)(&st->lock);

	/*
	 * The address is written on the highest weight byte, and the MSB set
	 * at 1 indicates a read operation.
	 */
	addr = FIELD_PREP(ADI_AXI_REG_ADDRESS_MASK, reg) | ADI_AXI_REG_READ_BIT;
	axi_adc_raw_write(back, addr);
	axi_adc_raw_read(back, &reg_val);

	*val = FIELD_GET(ADI_AXI_REG_VALUE_MASK, reg_val);

	/* Write 0x0 on the bus to get back to ADC mode */
	axi_adc_raw_write(back, 0);

	return 0;
}

static int ad7606_bus_reg_write(struct iio_backend *back, u32 reg, u32 val)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	u32 buf;

	guard(mutex)(&st->lock);

	/* Write any register to switch to register mode */
	axi_adc_raw_write(back, 0xaf00);

	buf = FIELD_PREP(ADI_AXI_REG_ADDRESS_MASK, reg) |
	      FIELD_PREP(ADI_AXI_REG_VALUE_MASK, val);
	axi_adc_raw_write(back, buf);

	/* Write 0x0 on the bus to get back to ADC mode */
	axi_adc_raw_write(back, 0);

	return 0;
}

static void axi_adc_free_buffer(struct iio_backend *back,
				struct iio_buffer *buffer)
{
	iio_dmaengine_buffer_teardown(buffer);
}

static int axi_adc_reg_access(struct iio_backend *back, unsigned int reg,
			      unsigned int writeval, unsigned int *readval)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	if (readval)
		return regmap_read(st->regmap, reg, readval);

	return regmap_write(st->regmap, reg, writeval);
}

static const struct regmap_config axi_adc_regmap_config = {
	.val_bits = 32,
	.reg_bits = 32,
	.reg_stride = 4,
};

static void axi_adc_child_remove(void *data)
{
	platform_device_unregister(data);
}

static int axi_adc_create_platform_device(struct adi_axi_adc_state *st,
					  struct fwnode_handle *child)
{
	struct platform_device_info pi = {
	    .parent = st->dev,
	    .name = fwnode_get_name(child),
	    .id = PLATFORM_DEVID_AUTO,
	    .fwnode = child,
	    .data = st->info->pdata,
	    .size_data = st->info->pdata_sz,
	};
	struct platform_device *pdev;
	int ret;

	pdev = platform_device_register_full(&pi);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	ret = devm_add_action_or_reset(st->dev, axi_adc_child_remove, pdev);
	if (ret)
		return ret;

	return 0;
}

static const struct iio_backend_ops adi_axi_adc_ops = {
	.enable = axi_adc_enable,
	.disable = axi_adc_disable,
	.data_format_set = axi_adc_data_format_set,
	.chan_enable = axi_adc_chan_enable,
	.chan_disable = axi_adc_chan_disable,
	.request_buffer = axi_adc_request_buffer,
	.free_buffer = axi_adc_free_buffer,
	.data_sample_trigger = axi_adc_data_sample_trigger,
	.iodelay_set = axi_adc_iodelays_set,
	.test_pattern_set = axi_adc_test_pattern_set,
	.chan_status = axi_adc_chan_status,
	.interface_type_get = axi_adc_interface_type_get,
	.debugfs_reg_access = iio_backend_debugfs_ptr(axi_adc_reg_access),
	.debugfs_print_chan_status = iio_backend_debugfs_ptr(axi_adc_debugfs_print_chan_status),
};

static const struct iio_backend_info adi_axi_adc_generic = {
	.name = "axi-adc",
	.ops = &adi_axi_adc_ops,
};

static const struct iio_backend_ops adi_ad485x_ops = {
	.enable = axi_adc_enable,
	.disable = axi_adc_disable,
	.data_format_set = axi_adc_data_format_set,
	.chan_enable = axi_adc_chan_enable,
	.chan_disable = axi_adc_chan_disable,
	.request_buffer = axi_adc_request_buffer,
	.free_buffer = axi_adc_free_buffer,
	.data_sample_trigger = axi_adc_data_sample_trigger,
	.iodelay_set = axi_adc_iodelays_set,
	.chan_status = axi_adc_chan_status,
	.interface_type_get = axi_adc_interface_type_get,
	.data_size_set = axi_adc_ad485x_data_size_set,
	.oversampling_ratio_set = axi_adc_ad485x_oversampling_ratio_set,
	.debugfs_reg_access = iio_backend_debugfs_ptr(axi_adc_reg_access),
	.debugfs_print_chan_status =
		iio_backend_debugfs_ptr(axi_adc_debugfs_print_chan_status),
};

static const struct iio_backend_info axi_ad485x = {
	.name = "axi-ad485x",
	.ops = &adi_ad485x_ops,
};

static int adi_axi_adc_probe(struct platform_device *pdev)
{
	struct adi_axi_adc_state *st;
	void __iomem *base;
	unsigned int ver;
	struct clk *clk;
	int ret;

	st = devm_kzalloc(&pdev->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	st->dev = &pdev->dev;
	st->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					   &axi_adc_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(&pdev->dev, PTR_ERR(st->regmap),
				     "failed to init register map\n");

	st->info = device_get_match_data(&pdev->dev);
	if (!st->info)
		return -ENODEV;

	clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(clk),
				     "failed to get clock\n");

	/*
	 * Force disable the core. Up to the frontend to enable us. And we can
	 * still read/write registers...
	 */
	ret = regmap_write(st->regmap, ADI_AXI_REG_RSTN, 0);
	if (ret)
		return ret;

	ret = regmap_read(st->regmap, ADI_AXI_REG_VERSION, &ver);
	if (ret)
		return ret;

	if (ADI_AXI_PCORE_VER_MAJOR(ver) !=
	    ADI_AXI_PCORE_VER_MAJOR(st->info->version)) {
		dev_err(&pdev->dev,
			"Major version mismatch. Expected %d.%.2d.%c, Reported %d.%.2d.%c\n",
			ADI_AXI_PCORE_VER_MAJOR(st->info->version),
			ADI_AXI_PCORE_VER_MINOR(st->info->version),
			ADI_AXI_PCORE_VER_PATCH(st->info->version),
			ADI_AXI_PCORE_VER_MAJOR(ver),
			ADI_AXI_PCORE_VER_MINOR(ver),
			ADI_AXI_PCORE_VER_PATCH(ver));
		return -ENODEV;
	}

	ret = devm_iio_backend_register(&pdev->dev, st->info->backend_info, st);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register iio backend\n");

	device_for_each_child_node_scoped(&pdev->dev, child) {
		int val;

		if (!st->info->has_child_nodes)
			return dev_err_probe(&pdev->dev, -EINVAL,
					     "invalid fdt axi-dac compatible.");

		/* Processing only reg 0 node */
		ret = fwnode_property_read_u32(child, "reg", &val);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "invalid reg property.");
		if (val != 0)
			return dev_err_probe(&pdev->dev, -EINVAL,
					     "invalid node address.");

		ret = axi_adc_create_platform_device(st, child);
		if (ret)
			return dev_err_probe(&pdev->dev, -EINVAL,
					     "cannot create device.");
	}

	dev_info(&pdev->dev, "AXI ADC IP core (%d.%.2d.%c) probed\n",
		 ADI_AXI_PCORE_VER_MAJOR(ver),
		 ADI_AXI_PCORE_VER_MINOR(ver),
		 ADI_AXI_PCORE_VER_PATCH(ver));

	return 0;
}

static const struct axi_adc_info adc_generic = {
	.version = ADI_AXI_PCORE_VER(10, 0, 'a'),
	.backend_info = &adi_axi_adc_generic,
};

static const struct axi_adc_info adi_axi_ad485x = {
	.version = ADI_AXI_PCORE_VER(10, 0, 'a'),
	.backend_info = &axi_ad485x,
};

static const struct ad7606_platform_data ad7606_pdata = {
	.bus_reg_read = ad7606_bus_reg_read,
	.bus_reg_write = ad7606_bus_reg_write,
};

static const struct axi_adc_info adc_ad7606 = {
	.version = ADI_AXI_PCORE_VER(10, 0, 'a'),
	.backend_info = &adi_axi_adc_generic,
	.pdata = &ad7606_pdata,
	.pdata_sz = sizeof(ad7606_pdata),
	.has_child_nodes = true,
};

/* Match table for of_platform binding */
static const struct of_device_id adi_axi_adc_of_match[] = {
	{ .compatible = "adi,axi-adc-10.0.a", .data = &adc_generic },
	{ .compatible = "adi,axi-ad485x", .data = &adi_axi_ad485x },
	{ .compatible = "adi,axi-ad7606x", .data = &adc_ad7606 },
	{ }
};
MODULE_DEVICE_TABLE(of, adi_axi_adc_of_match);

static struct platform_driver adi_axi_adc_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = adi_axi_adc_of_match,
	},
	.probe = adi_axi_adc_probe,
};
module_platform_driver(adi_axi_adc_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices Generic AXI ADC IP core driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_DMAENGINE_BUFFER");
MODULE_IMPORT_NS("IIO_BACKEND");

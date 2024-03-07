// SPDX-License-Identifier: GPL-2.0
/*
 * STMicroelectronics stts22h temperature driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2021 STMicroelectronics Inc.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/sysfs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/workqueue.h>
#include <linux/version.h>

#include "stts22h.h"

static const struct iio_chan_spec st_stts22h_channel[] = {
	{
		.type = IIO_TEMP,
		.address = ST_STTS22H_TEMP_L_OUT_ADDR,
		.modified = 1,
		.channel2 = IIO_MOD_TEMP_AMBIENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};
static const unsigned long st_stts22h_available_scan_masks[] = { 0x1, 0x0 };

static const struct st_stts22h_odr_table_entry {
	u8 size;
	struct st_stts22h_reg reg;
	struct st_stts22h_odr odr_avl[ST_STTS22H_ODR_LIST_SIZE];
} st_stts22h_odr_table = {
	.size = ST_STTS22H_ODR_LIST_SIZE,
	.reg = {
		.addr = ST_STTS22H_CTRL_ADDR,
		.mask = ST_STTS22H_AVG_MASK,
	},
	.odr_avl[0] = {  25, 0x00 },
	.odr_avl[1] = {  50, 0x01 },
	.odr_avl[2] = { 100, 0x02 },
	.odr_avl[3] = { 200, 0x03 },
};

static int st_stts22h_read(struct device *dev, u8 addr, int len, u8 *data)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msg[2];

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 1;
	msg[0].buf = &addr;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = data;

	return i2c_transfer(client->adapter, msg, 2);
}

static int st_stts22h_write(struct device *dev, u8 addr, int len,
			    const u8 *data)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msg;
	u8 send[4];

	if (len > ARRAY_SIZE(send))
		return -ENOMEM;

	send[0] = addr;
	memcpy(&send[1], data, len * sizeof(u8));

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = len + 1;
	msg.buf = send;

	return i2c_transfer(client->adapter, &msg, 1);
}

static inline int st_stts22h_write_with_mask(struct st_stts22h_data *data,
					     u8 addr, u8 mask, u8 val)
{
	int err;
	u8 read;

	mutex_lock(&data->lock);
	err = st_stts22h_read(data->dev, addr, sizeof(read), &read);
	if (err < 0) {
		dev_err(data->dev, "failed to read %02x register\n", addr);
		goto out;
	}

	read = (read & ~mask) | ((val << __ffs(mask)) & mask);

	err = st_stts22h_write(data->dev, addr, sizeof(read), &read);
	if (err < 0)
		dev_err(data->dev, "failed to write %02x register\n", addr);

out:
	mutex_unlock(&data->lock);

	return err;
}

static __maybe_unused int st_stts22h_reg_access(struct iio_dev *iio_dev,
						unsigned int reg,
						unsigned int writeval,
						unsigned int *readval)
{
	struct st_stts22h_data *data = iio_priv(iio_dev);
	int err;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	mutex_lock(&data->lock);
	if (readval == NULL)
		err = st_stts22h_write(data->dev, reg, 1, (u8 *)&writeval);
	else
		err = st_stts22h_read(data->dev, reg, 1, (u8 *)readval);
	mutex_unlock(&data->lock);

	iio_device_release_direct_mode(iio_dev);

	return (err < 0) ? err : 0;
}

static inline void st_stts22h_flush_works(struct st_stts22h_data *data)
{
	flush_workqueue(data->st_stts22h_workqueue);
}

static int st_stts22h_allocate_workqueue(struct st_stts22h_data *data)
{
	if (!data->st_stts22h_workqueue)
		data->st_stts22h_workqueue =
					create_workqueue(data->iio_devs->name);

	if (!data->st_stts22h_workqueue)
		return -ENOMEM;

	return 0;
}

static inline s64 st_stts22h_get_time_ns(struct st_stts22h_data *data)
{
	return iio_get_time_ns(data->iio_devs);
}

static ssize_t
st_stts22h_sysfs_sampling_frequency_avail(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	int i, len = 0;

	for (i = 0; i < st_stts22h_odr_table.size; i++) {
		if (!st_stts22h_odr_table.odr_avl[i].hz)
			continue;

		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				 st_stts22h_odr_table.odr_avl[i].hz);
	}

	buf[len - 1] = '\n';

	return len;
}

static int st_stts22h_get_odr_val(struct st_stts22h_data *data, int val,
				  u8 *odr)
{
	int i;

	for (i = 0; i < st_stts22h_odr_table.size; i++) {
		if (st_stts22h_odr_table.odr_avl[i].hz >= val)
			break;
	}

	if (i == st_stts22h_odr_table.size)
		return -EINVAL;

	*odr = st_stts22h_odr_table.odr_avl[i].hz;

	return i;
}

static int st_stts22h_set_odr(struct st_stts22h_data *data, u8 req_odr)
{
	int err, i;

	for (i = 0; i < st_stts22h_odr_table.size; i++) {
		if (st_stts22h_odr_table.odr_avl[i].hz >= req_odr)
			break;
	}

	if (i == st_stts22h_odr_table.size)
		return -EINVAL;

	err = st_stts22h_write_with_mask(data, st_stts22h_odr_table.reg.addr,
					 st_stts22h_odr_table.reg.mask,
					 st_stts22h_odr_table.odr_avl[i].val);

	return err < 0 ? err : 0;
}

static int st_stts22h_sensor_set_enable(struct st_stts22h_data *data, bool en)
{
	u8 odr = en ? data->odr : 0;
	int64_t newTime;
	int err;

	err = st_stts22h_set_odr(data, odr);
	if (err < 0)
		return err;

	err = st_stts22h_write_with_mask(data,
					 ST_STTS22H_CTRL_ADDR,
					 ST_STTS22H_FREERUN_MASK,
					 en);
	if (err < 0)
		return err;

	if (en) {
		newTime = HZ_TO_PERIOD_NSEC(odr);
		data->sensorktime = ktime_set(0, newTime);
		hrtimer_start(&data->hr_timer, data->sensorktime,
			      HRTIMER_MODE_REL);

	} else {
		cancel_work_sync(&data->iio_work);
		hrtimer_cancel(&data->hr_timer);
	}

	data->enable = en;

	return 0;
}

static int st_stts22h_read_oneshot(struct st_stts22h_data *data,
				   u8 addr, int *val)
{
	int err, delay;
	__le16 temp;

	err = st_stts22h_sensor_set_enable(data, true);
	if (err < 0)
		return err;

	delay = 2 * (1000000 / data->odr);
	usleep_range(delay, 2 * delay);

	err = st_stts22h_read(data->dev, addr, sizeof(temp), (u8 *)&temp);
	if (err < 0)
		return err;

	st_stts22h_sensor_set_enable(data, false);

	*val = (s16)le16_to_cpu(temp);

	return IIO_VAL_INT;
}

static int st_stts22h_read_raw(struct iio_dev *iio_dev,
			       struct iio_chan_spec const *ch,
			       int *val, int *val2, long mask)
{
	struct st_stts22h_data *data = iio_priv(iio_dev);
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = iio_device_claim_direct_mode(iio_dev);
		if (err)
			return err;

		err = st_stts22h_read_oneshot(data, ch->address, val);
		iio_device_release_direct_mode(iio_dev);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = (int)data->odr;
		err = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = 1000;
		*val2 = ST_STTS22H_GAIN;
		err = IIO_VAL_FRACTIONAL;
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int st_stts22h_write_raw(struct iio_dev *iio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	int err = -EINVAL;

	if (mask == IIO_CHAN_INFO_SAMP_FREQ) {
		struct st_stts22h_data *data = iio_priv(iio_dev);
		u8 odr;


		err = st_stts22h_get_odr_val(data, val, &odr);
		if (err < 0)
			return err;

		err = iio_device_claim_direct_mode(iio_dev);
		if (err)
			return err;

		data->odr = odr;
		iio_device_release_direct_mode(iio_dev);
	}

	return err < 0 ? err : 0;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_stts22h_sysfs_sampling_frequency_avail);
static struct attribute *st_stts22h_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_stts22h_attribute_group = {
	.attrs = st_stts22h_attributes,
};

static const struct iio_info st_stts22h_info = {
	.attrs = &st_stts22h_attribute_group,
	.read_raw = st_stts22h_read_raw,
	.write_raw = st_stts22h_write_raw,

#ifdef CONFIG_DEBUG_FS
	.debugfs_reg_access = &st_stts22h_reg_access,
#endif /* CONFIG_DEBUG_FS */
};

static int st_stts22h_check_whoami(struct st_stts22h_data *data)
{
	int err;
	u8 wai;

	err = st_stts22h_read(data->dev, ST_STTS22H_WHOAMI_ADDR, 1, &wai);
	if (err < 0)
		return err;

	if (wai != ST_STTS22H_WHOAMI_VAL) {
		dev_err(data->dev, "unsupported whoami [%02x]\n", wai);

		return -ENODEV;
	}

	return 0;
}

static int st_stts22h_init(struct st_stts22h_data *data)
{
	int err;

	/* reset cycle */
	err = st_stts22h_write_with_mask(data, ST_STTS22H_CTRL_ADDR,
					 ST_STTS22H_SW_RESET_MASK, 1);
	if (err < 0)
		return err;

	err = st_stts22h_write_with_mask(data, ST_STTS22H_CTRL_ADDR,
					 ST_STTS22H_SW_RESET_MASK, 0);
	if (err < 0)
		return err;

	/* enable bdu */
	err = st_stts22h_write_with_mask(data, ST_STTS22H_CTRL_ADDR,
					 ST_STTS22H_BDU_MASK, 1);
	if (err < 0)
		return err;

	/* enable register auto increments */
	err = st_stts22h_write_with_mask(data, ST_STTS22H_CTRL_ADDR,
					 ST_STTS22H_IF_ADD_INC_MASK, 1);

	return err < 0 ? err : 0;
}

static
enum hrtimer_restart st_stts22h_poll_function_read(struct hrtimer *timer)
{
	struct st_stts22h_data *data;

	data = container_of((struct hrtimer *)timer,
			    struct st_stts22h_data, hr_timer);

	data->timestamp = st_stts22h_get_time_ns(data);
	queue_work(data->st_stts22h_workqueue, &data->iio_work);

	return HRTIMER_NORESTART;
}

static void st_stts22h_report_event(struct st_stts22h_data *data, u8 *tmp)
{
	struct iio_dev *iio_dev = data->iio_devs;
	u8 iio_buf[ALIGN(ST_STTS22H_SAMPLE_SIZE, sizeof(s64)) + sizeof(s64)];

	memcpy(iio_buf, tmp, ST_STTS22H_SAMPLE_SIZE);
	iio_push_to_buffers_with_timestamp(iio_dev, iio_buf, data->timestamp);
}

static void st_stts22h_poll_function_work(struct work_struct *iio_work)
{
	struct st_stts22h_data *data;
	ktime_t tmpkt, ktdelta;
	__le16 temp;

	data = container_of((struct work_struct *)iio_work,
			     struct st_stts22h_data, iio_work);

	/* adjust new timeout */
	ktdelta = ktime_set(0, (st_stts22h_get_time_ns(data) -
				data->timestamp));

	/* avoid negative value in case of high ODRs */
	if (ktime_after(data->sensorktime, ktdelta))
		tmpkt = ktime_sub(data->sensorktime, ktdelta);
	else
		tmpkt = data->sensorktime;

	hrtimer_start(&data->hr_timer, tmpkt, HRTIMER_MODE_REL);

	st_stts22h_read(data->dev, ST_STTS22H_TEMP_L_OUT_ADDR,
			sizeof(temp), (u8 *)&temp);
	st_stts22h_report_event(data, (u8 *)&temp);
}

static int st_stts22h_preenable(struct iio_dev *iio_dev)
{
	struct st_stts22h_data *data = iio_priv(iio_dev);

	return st_stts22h_sensor_set_enable(data, true);
}

static int st_stts22h_postdisable(struct iio_dev *iio_dev)
{
	struct st_stts22h_data *data = iio_priv(iio_dev);

	return st_stts22h_sensor_set_enable(data, false);
}

static const struct iio_buffer_setup_ops st_stts22h_buffer_ops = {
	.preenable = st_stts22h_preenable,
	.postdisable = st_stts22h_postdisable,
};

static int st_stts22h_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct st_stts22h_data *data;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,13,0)
	struct iio_buffer *buffer;
#endif /* LINUX_VERSION_CODE */
	struct iio_dev *iio_dev;
	struct device *dev;
	int err;

	iio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!iio_dev)
		return -ENOMEM;

	data = iio_priv(iio_dev);
	i2c_set_clientdata(client, iio_dev);

	dev = &client->dev;
	data->dev = dev;
	dev_set_drvdata(dev, (void *)data);

	mutex_init(&data->lock);
	err = st_stts22h_check_whoami(data);
	if (err < 0)
		return err;

	err = st_stts22h_init(data);
	if (err < 0)
		return err;

	iio_dev->name = client->name;
	iio_dev->dev.parent = &client->dev;
	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->info = &st_stts22h_info;
	iio_dev->channels = st_stts22h_channel;
	iio_dev->num_channels = ARRAY_SIZE(st_stts22h_channel);
	iio_dev->available_scan_masks = st_stts22h_available_scan_masks;
	data->iio_devs = iio_dev;

	/* configure hrtimer */
	data->odr = st_stts22h_odr_table.odr_avl[0].hz;
	data->enable = false;
	hrtimer_init(&data->hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	data->hr_timer.function = &st_stts22h_poll_function_read;
	data->sensorktime = ktime_set(0, HZ_TO_PERIOD_NSEC(data->odr));
	INIT_WORK(&data->iio_work, st_stts22h_poll_function_work);

	err = st_stts22h_allocate_workqueue(data);
	if (err < 0)
		return err;

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
	err = devm_iio_kfifo_buffer_setup(data->dev, data->iio_devs,
					  &st_stts22h_buffer_ops);
	if (err)
		return err;
#elif KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
	err = devm_iio_kfifo_buffer_setup(data->dev, data->iio_devs,
					  INDIO_BUFFER_SOFTWARE,
					  &st_stts22h_buffer_ops);
	if (err)
		return err;
#else /* LINUX_VERSION_CODE */
	buffer = devm_iio_kfifo_allocate(data->dev);
	if (!buffer)
		return -ENOMEM;

	iio_device_attach_buffer(data->iio_devs, buffer);
	data->iio_devs->modes |= INDIO_BUFFER_SOFTWARE;
	data->iio_devs->setup_ops = &st_stts22h_buffer_ops;
#endif /* LINUX_VERSION_CODE */

	return devm_iio_device_register(data->dev, data->iio_devs);
}

#if KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE
static void st_stts22h_remove(struct i2c_client *client)
{
	struct st_stts22h_data *data = dev_get_drvdata(&client->dev);

	if (data->enable)
		st_stts22h_sensor_set_enable(data, false);

	st_stts22h_flush_works(data);
	destroy_workqueue(data->st_stts22h_workqueue);
	data->st_stts22h_workqueue = NULL;
}
#else /* LINUX_VERSION_CODE */
static int st_stts22h_remove(struct i2c_client *client)
{
	struct st_stts22h_data *data = dev_get_drvdata(&client->dev);
	int err = 0;

	if (data->enable)
		err = st_stts22h_sensor_set_enable(data, false);

	st_stts22h_flush_works(data);
	destroy_workqueue(data->st_stts22h_workqueue);
	data->st_stts22h_workqueue = NULL;

	return err;
}
#endif /* LINUX_VERSION_CODE */

static int __maybe_unused st_stts22h_suspend(struct device *dev)
{
	struct st_stts22h_data *data = dev_get_drvdata(dev);
	int err = 0;

	if (data->enable)
		err = st_stts22h_sensor_set_enable(data, false);

	return err;
}

static int __maybe_unused st_stts22h_resume(struct device *dev)
{
	struct st_stts22h_data *data = dev_get_drvdata(dev);
	int err = 0;

	if (data->enable)
		err = st_stts22h_sensor_set_enable(data, true);

	return err;
}

#ifdef CONFIG_PM
const struct dev_pm_ops st_stts22h_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_stts22h_suspend, st_stts22h_resume)
};
#endif /* CONFIG_PM */

static const struct of_device_id st_stts22h_of_match[] = {
	{
		.compatible = "st,stts22h",
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_stts22h_of_match);

static const struct i2c_device_id st_stts22h_id_table[] = {
	{ ST_STTS22H_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_stts22h_id_table);

static struct i2c_driver st_stts22h_driver = {
	.driver = {
		.name = "st_stts22h_i2c",
#ifdef CONFIG_PM
		.pm = &st_stts22h_pm_ops,
#endif /* CONFIG_PM */
		.of_match_table = of_match_ptr(st_stts22h_of_match),
	},
	.probe = st_stts22h_probe,
	.remove = st_stts22h_remove,
	.id_table = st_stts22h_id_table,
};
module_i2c_driver(st_stts22h_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("stts22h ST MEMS temperature sensor driver");
MODULE_LICENSE("GPL");

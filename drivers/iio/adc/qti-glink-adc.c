// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/iio/iio.h>
#include <linux/soc/qcom/pmic_glink.h>

#define MSG_OWNER_SMB_ADC			32784
#define MSG_TYPE_REQ_RESP			1
#define SMB_ADC_READ_REQ_OP			0x49
#define ADC_READ_WAIT_TIME_MS			1000

enum glink_adc_channel {
	GLINK_ADC_CHAN_IIN			= 1,
	GLINK_ADC_CHAN_ICHG			= 2,
	GLINK_ADC_CHAN_DIE_TEMP			= 3,
	GLINK_ADC_CHAN_MAX
};

enum smb_adc_read_status {
	SMB_ADC_READ_STATUS_SUCCESS		= 0,
	SMB_ADC_READ_STATUS_ERR_NO_OPCODE	= 0x100,
	SMB_ADC_READ_STATUS_ERR_FAILED		= 0x200,
	SMB_ADC_READ_STATUS_ERR_NO_PMIC		= 0x201,
	SMB_ADC_READ_STATUS_ERR_INVALID_PARAM	= 0x202,
};

struct smb_adc_read_req_msg {
	struct pmic_glink_hdr	hdr;
	u32			bus_id;
	u32			pmic_id;
	u32			chan;
};

struct smb_adc_read_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			bus_id;
	u32			pmic_id;
	u32			chan;
	u32			raw_data;
	u32			conv_data;
	u32			status;
};

struct glink_adc_dev {
	struct pmic_glink_client	*client;
	struct device			*dev;
	struct mutex			lock;
	struct completion		ack;
	struct iio_chan_spec		*iio_chans;
	unsigned int			nchannels;
	struct smb_adc_read_resp_msg	read_msg;
};

#define ADC_GLINK_CHAN(hwchan)		FIELD_GET(GENMASK(7, 0), hwchan)
#define ADC_GLINK_PMIC_ID(hwchan)	FIELD_GET(GENMASK(15, 8), hwchan)
#define ADC_GLINK_BUS_ID(hwchan)	FIELD_GET(GENMASK(31, 16), hwchan)

static void glink_adc_handle_read_resp(struct glink_adc_dev *adc,
				       struct smb_adc_read_resp_msg *read_resp,
				       size_t len)
{
	if (len != sizeof(*read_resp)) {
		dev_err(adc->dev, "Invalid read response, glink packet size=%zu\n",
			len);
		return;
	}

	memcpy(&adc->read_msg, read_resp, sizeof(adc->read_msg));

	complete(&adc->ack);
}

static int glink_adc_callback(void *priv, void *data, size_t len)
{
	struct glink_adc_dev *adc = priv;
	struct pmic_glink_hdr *hdr = data;

	dev_dbg(adc->dev, "owner: %u type: %u opcode: %#x len: %zu\n",
		hdr->owner, hdr->type, hdr->opcode, len);

	switch (hdr->opcode) {
	case SMB_ADC_READ_REQ_OP:
		glink_adc_handle_read_resp(adc, data, len);
		break;
	default:
		dev_err(adc->dev, "Unknown opcode %u\n", hdr->opcode);
		break;
	}

	return 0;
}

static int glink_adc_read_channel(struct glink_adc_dev *adc,
				  struct iio_chan_spec const *chan,
				  int *conv_data, int *raw_data)
{
	struct smb_adc_read_req_msg msg = {{0}};
	int ret;

	msg.hdr.owner = MSG_OWNER_SMB_ADC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = SMB_ADC_READ_REQ_OP;

	msg.bus_id = ADC_GLINK_BUS_ID(chan->channel);
	msg.pmic_id = ADC_GLINK_PMIC_ID(chan->channel);
	msg.chan = ADC_GLINK_CHAN(chan->channel);

	mutex_lock(&adc->lock);
	reinit_completion(&adc->ack);
	ret = pmic_glink_write(adc->client, &msg, sizeof(msg));
	if (ret)
		goto done;

	ret = wait_for_completion_timeout(&adc->ack,
				msecs_to_jiffies(ADC_READ_WAIT_TIME_MS));
	if (!ret) {
		dev_err(adc->dev, "Error, ADC conversion timed out\n");
		ret = -ETIMEDOUT;
		goto done;
	}

	if (adc->read_msg.status != SMB_ADC_READ_STATUS_SUCCESS) {
		dev_err(adc->dev, "glink ADC read failed, bus_id=%u, pmic_id=%u, chan=%u, ret=%u\n",
			adc->read_msg.bus_id, adc->read_msg.pmic_id,
			adc->read_msg.chan, adc->read_msg.status);
		ret = -EIO;
		goto done;
	}

	if (conv_data)
		*conv_data = adc->read_msg.conv_data;
	if (raw_data)
		*raw_data = adc->read_msg.raw_data;

	ret = 0;
done:
	mutex_unlock(&adc->lock);
	return ret;
}

static int glink_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct glink_adc_dev *adc = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = glink_adc_read_channel(adc, chan, val, NULL);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_RAW:
		ret = glink_adc_read_channel(adc, chan, NULL, val);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}

	return 0;
}

static int glink_adc_fwnode_xlate(struct iio_dev *indio_dev,
				const struct fwnode_reference_args *iiospec)
{
	int i;

	for (i = 0; i < indio_dev->num_channels; i++) {
		if (indio_dev->channels[i].channel == iiospec->args[0])
			return i;
	}

	return -EINVAL;
}

static const struct iio_info glink_adc_info = {
	.read_raw = glink_adc_read_raw,
	.fwnode_xlate = glink_adc_fwnode_xlate,
};

#define GLINK_ADC_CHAN(_name, _type)					\
	{								\
		.datasheet_name = _name,				\
		.type = _type,						\
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED)	\
				      | BIT(IIO_CHAN_INFO_RAW),		\
	}								\


static const struct iio_chan_spec glink_adc_channels[] = {
	[GLINK_ADC_CHAN_IIN]		= GLINK_ADC_CHAN("iin", IIO_CURRENT),
	[GLINK_ADC_CHAN_ICHG]		= GLINK_ADC_CHAN("ichg", IIO_CURRENT),
	[GLINK_ADC_CHAN_DIE_TEMP]	= GLINK_ADC_CHAN("die_temp", IIO_TEMP),
};

static int glink_adc_get_dt_channel_data(struct glink_adc_dev *adc,
					 struct fwnode_handle *fwnode,
					 struct iio_chan_spec *iio_chan)
{
	u32 reg, chan;
	int ret;

	ret = fwnode_property_read_u32(fwnode, "reg", &reg);
	if (ret < 0) {
		dev_err(adc->dev, "missing channel number %s, ret=%d\n",
			fwnode_get_name(fwnode), ret);
		return ret;
	}

	chan = ADC_GLINK_CHAN(reg);
	if (chan == 0 || chan >= GLINK_ADC_CHAN_MAX ||
	    !glink_adc_channels[chan].datasheet_name) {
		dev_err(adc->dev, "%s invalid channel number %u\n",
			fwnode_get_name(fwnode), chan);
		return -EINVAL;
	}

	iio_chan->channel = reg;
	iio_chan->type = glink_adc_channels[chan].type;
	iio_chan->datasheet_name = glink_adc_channels[chan].datasheet_name;
	fwnode_property_read_string(fwnode, "label", &iio_chan->datasheet_name);
	iio_chan->extend_name = iio_chan->datasheet_name;
	iio_chan->info_mask_separate
		= glink_adc_channels[chan].info_mask_separate;

	return 0;
}

static int glink_adc_get_dt_data(struct glink_adc_dev *adc)
{
	struct iio_chan_spec *iio_chan;
	struct fwnode_handle *child;
	int ret;

	adc->nchannels = device_get_child_node_count(adc->dev);
	if (!adc->nchannels) {
		dev_dbg(adc->dev, "no ADC channels specified\n");
		return -EINVAL;
	}

	adc->iio_chans = devm_kcalloc(adc->dev, adc->nchannels,
				      sizeof(*adc->iio_chans), GFP_KERNEL);
	if (!adc->iio_chans)
		return -ENOMEM;

	iio_chan = adc->iio_chans;
	device_for_each_child_node(adc->dev, child) {
		ret = glink_adc_get_dt_channel_data(adc, child, iio_chan);
		if (ret < 0) {
			fwnode_handle_put(child);
			return ret;
		}

		iio_chan++;
	}

	return 0;
}

static int glink_adc_probe(struct platform_device *pdev)
{
	struct glink_adc_dev *adc;
	struct iio_dev *indio_dev;
	struct pmic_glink_client_data client_data = { };
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->dev = &pdev->dev;
	mutex_init(&adc->lock);
	init_completion(&adc->ack);
	platform_set_drvdata(pdev, adc);

	client_data.id = MSG_OWNER_SMB_ADC;
	client_data.name = "adc";
	client_data.msg_cb = glink_adc_callback;
	client_data.priv = adc;

	ret = glink_adc_get_dt_data(adc);
	if (ret < 0)
		return ret;

	adc->client = pmic_glink_register_client(&pdev->dev, &client_data);
	if (IS_ERR(adc->client)) {
		ret = PTR_ERR(adc->client);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Error registering glink ADC, ret=%d\n",
				ret);
		return ret;
	}

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->name = pdev->name;
	indio_dev->info = &glink_adc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adc->iio_chans;
	indio_dev->num_channels = adc->nchannels;

	ret = devm_iio_device_register(&pdev->dev, indio_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "iio device registration failed, ret=%d\n",
			ret);
		goto fail;
	}

	return 0;

fail:
	pmic_glink_unregister_client(adc->client);
	return ret;
}

static int glink_adc_remove(struct platform_device *pdev)
{
	struct glink_adc_dev *adc = platform_get_drvdata(pdev);

	pmic_glink_unregister_client(adc->client);

	return 0;
}

static const struct of_device_id glink_adc_match_table[] = {
	{ .compatible = "qcom,glink-adc", },
	{}
};
MODULE_DEVICE_TABLE(of, glink_adc_match_table);

static struct platform_driver glink_adc_driver = {
	.driver = {
		.name = "glink_adc",
		.of_match_table = glink_adc_match_table,
	},
	.probe = glink_adc_probe,
	.remove = glink_adc_remove,
};
module_platform_driver(glink_adc_driver);

MODULE_DESCRIPTION("Glink ADC Driver");
MODULE_LICENSE("GPL");

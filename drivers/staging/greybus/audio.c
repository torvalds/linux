/*
 * Greybus audio driver
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <sound/simple_card.h>

#include "greybus.h"
#include "audio.h"


#define GB_AUDIO_DATA_DRIVER_NAME		"gb_audio_data"
#define GB_AUDIO_MGMT_DRIVER_NAME		"gb_audio_mgmt"

#define RT5647_I2C_ADAPTER_NR			6
#define RT5647_I2C_ADDR				0x1b

/*
 * gb_snd management functions
 */
static DEFINE_SPINLOCK(gb_snd_list_lock);
static LIST_HEAD(gb_snd_list);
static int device_count;

static struct gb_snd *gb_find_snd(int bundle_id)
{
	struct gb_snd *tmp, *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&gb_snd_list_lock, flags);
	list_for_each_entry(tmp, &gb_snd_list, list)
		if (tmp->gb_bundle_id == bundle_id) {
			ret = tmp;
			break;
		}
	spin_unlock_irqrestore(&gb_snd_list_lock, flags);
	return ret;
}

static struct gb_snd *gb_get_snd(int bundle_id)
{
	struct gb_snd *snd_dev;
	unsigned long flags;

	snd_dev = gb_find_snd(bundle_id);
	if (snd_dev)
		return snd_dev;

	snd_dev = kzalloc(sizeof(*snd_dev), GFP_KERNEL);
	if (!snd_dev)
		return NULL;

	spin_lock_init(&snd_dev->lock);
	snd_dev->device_count = device_count++;
	snd_dev->gb_bundle_id = bundle_id;
	spin_lock_irqsave(&gb_snd_list_lock, flags);
	list_add(&snd_dev->list, &gb_snd_list);
	spin_unlock_irqrestore(&gb_snd_list_lock, flags);
	return snd_dev;
}

static void gb_free_snd(struct gb_snd *snd)
{
	unsigned long flags;

	spin_lock_irqsave(&gb_snd_list_lock, flags);
	if (!snd->i2s_tx_connection &&
			!snd->mgmt_connection) {
		list_del(&snd->list);
		spin_unlock_irqrestore(&gb_snd_list_lock, flags);
		kfree(snd);
	} else {
		spin_unlock_irqrestore(&gb_snd_list_lock, flags);
	}
}




/*
 * This is the ASoC simple card binds the platform codec,
 * cpu-dai and codec-dai togheter
 */
struct gb_card_info_object {
	struct asoc_simple_card_info card_info;
	char codec_name[255];
	char platform_name[255];
	char dai_name[255];
};


static struct asoc_simple_card_info *setup_card_info(int device_count)
{
	struct gb_card_info_object *obj;

	obj = kzalloc(sizeof(struct gb_card_info_object), GFP_KERNEL);
	if (!obj)
		return NULL;

	obj->card_info.name		= "Greybus Audio Module";
	obj->card_info.card		= "gb-card";
	obj->card_info.codec		= obj->codec_name;
	obj->card_info.platform		= obj->platform_name;
	obj->card_info.cpu_dai.name	= obj->dai_name;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0)
	obj->card_info.cpu_dai.fmt	= SND_SOC_DAIFMT_CBM_CFM;
#endif
#if USE_RT5645
	obj->card_info.daifmt		= SND_SOC_DAIFMT_NB_NF |
					  SND_SOC_DAIFMT_I2S;
	sprintf(obj->codec_name, "rt5645.%d-%04x", RT5647_I2C_ADAPTER_NR,
		RT5647_I2C_ADDR);
	obj->card_info.codec_dai.name	= "rt5645-aif1";
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0)
	obj->card_info.codec_dai.fmt	= SND_SOC_DAIFMT_CBS_CFS;
#endif
	obj->card_info.codec_dai.sysclk	= 12288000;
#else
	sprintf(obj->codec_name, "spdif-dit");
	obj->card_info.codec_dai.name = "dit-hifi";
#endif
	sprintf(obj->platform_name, "gb-pcm-audio.%i", device_count);
	sprintf(obj->dai_name, "gb-dai-audio.%i", device_count);

	return &obj->card_info;
}

static void free_card_info(struct asoc_simple_card_info *ci)
{
	struct gb_card_info_object *obj;

	obj = container_of(ci, struct gb_card_info_object, card_info);
	kfree(obj);
}


/*
 * XXX this is sort of cruddy but I get warnings if
 * we don't have dev.release handler set.
 */
static void default_release(struct device *dev)
{
}

/*
 * GB connection hooks
 */
static int gb_i2s_transmitter_connection_init(struct gb_connection *connection)
{
	struct gb_snd *snd_dev;
	struct platform_device *codec, *dai;
	struct asoc_simple_card_info *simple_card;
#if USE_RT5645
	struct i2c_board_info rt5647_info;
	struct i2c_adapter *i2c_adap;
#endif
	unsigned long flags;
	int ret;

	snd_dev = gb_get_snd(connection->bundle->id);
	if (!snd_dev)
		return -ENOMEM;

	codec = platform_device_register_simple("spdif-dit", -1, NULL, 0);
	if (!codec) {
		ret = -ENOMEM;
		goto out;
	}

	dai = platform_device_register_simple("gb-pcm-audio", snd_dev->device_count, NULL, 0);
	if (!dai) {
		ret = -ENOMEM;
		goto out;
	}

	simple_card = setup_card_info(snd_dev->device_count);
	if (!simple_card) {
		ret = -ENOMEM;
		goto out;
	}

	spin_lock_irqsave(&snd_dev->lock, flags);
	snd_dev->card.name = "asoc-simple-card";
	snd_dev->card.id = snd_dev->device_count;
	snd_dev->card.dev.release = default_release; /* XXX - suspicious */

	snd_dev->cpu_dai.name = "gb-dai-audio";
	snd_dev->cpu_dai.id = snd_dev->device_count;
	snd_dev->cpu_dai.dev.release = default_release; /* XXX - suspicious */


	snd_dev->simple_card_info = simple_card;
	snd_dev->card.dev.platform_data = simple_card;

	snd_dev->codec = codec;
	snd_dev->i2s_tx_connection = connection;
	snd_dev->cpu_dai.dev.platform_data = snd_dev;
	snd_dev->i2s_tx_connection->private = snd_dev;
	spin_unlock_irqrestore(&snd_dev->lock, flags);

	ret = platform_device_register(&snd_dev->cpu_dai);
	if (ret) {
		pr_err("cpu_dai platform_device register failed\n");
		goto out_dai;
	}

	ret = platform_device_register(&snd_dev->card);
	if (ret) {
		pr_err("card platform_device register failed\n");
		goto out_card;
	}

	ret = gb_i2s_data_get_version(connection);
	if (ret) {
		pr_err("i2s data get_version() failed: %d\n", ret);
		goto out_get_ver;
	}

#if USE_RT5645
	rt5647_info.addr = RT5647_I2C_ADDR;
	strlcpy(rt5647_info.type, "rt5647", I2C_NAME_SIZE);

	i2c_adap = i2c_get_adapter(RT5647_I2C_ADAPTER_NR);
	if (!i2c_adap) {
		pr_err("codec unavailable\n");
		ret = -ENODEV;
		goto out_get_ver;
	}

	snd_dev->rt5647 = i2c_new_device(i2c_adap, &rt5647_info);
	if (!snd_dev->rt5647) {
		pr_err("can't create rt5647 i2c device\n");
		goto out_get_ver;
	}
#endif

	return 0;

out_get_ver:
	platform_device_unregister(&snd_dev->card);
out_card:
	platform_device_unregister(&snd_dev->cpu_dai);
out_dai:
	platform_device_unregister(codec);
out:
	gb_free_snd(snd_dev);
	return ret;
}

static void gb_i2s_transmitter_connection_exit(struct gb_connection *connection)
{
	struct gb_snd *snd_dev;

	snd_dev = (struct gb_snd *)connection->private;

#if USE_RT5645
	i2c_unregister_device(snd_dev->rt5647);
#endif

	platform_device_unregister(&snd_dev->card);
	platform_device_unregister(&snd_dev->cpu_dai);
	platform_device_unregister(snd_dev->codec);

	free_card_info(snd_dev->simple_card_info);
	snd_dev->i2s_tx_connection = NULL;
	gb_free_snd(snd_dev);
}

static int gb_i2s_mgmt_connection_init(struct gb_connection *connection)
{
	struct gb_snd *snd_dev;
	unsigned long flags;
	int ret;

	snd_dev = gb_get_snd(connection->bundle->id);
	if (!snd_dev)
		return -ENOMEM;

	spin_lock_irqsave(&snd_dev->lock, flags);
	snd_dev->mgmt_connection = connection;
	connection->private = snd_dev;
	spin_unlock_irqrestore(&snd_dev->lock, flags);

	ret = gb_i2s_mgmt_get_version(connection);
	if (ret) {
		pr_err("i2s mgmt get_version() failed: %d\n", ret);
		goto err_free_snd_dev;
	}

	ret = gb_i2s_mgmt_get_cfgs(snd_dev, connection);
	if (ret) {
		pr_err("can't get i2s configurations: %d\n", ret);
		goto err_free_snd_dev;
	}

	ret = gb_i2s_mgmt_set_samples_per_message(snd_dev->mgmt_connection,
						  CONFIG_SAMPLES_PER_MSG);
	if (ret) {
		pr_err("set_samples_per_msg failed: %d\n", ret);
		goto err_free_i2s_configs;
	}

	snd_dev->send_data_req_buf = kzalloc(SEND_DATA_BUF_LEN, GFP_KERNEL);

	if (!snd_dev->send_data_req_buf) {
		ret = -ENOMEM;
		goto err_free_i2s_configs;
	}

	return 0;

err_free_i2s_configs:
	gb_i2s_mgmt_free_cfgs(snd_dev);
err_free_snd_dev:
	gb_free_snd(snd_dev);
	return ret;
}

static void gb_i2s_mgmt_connection_exit(struct gb_connection *connection)
{
	struct gb_snd *snd_dev = (struct gb_snd *)connection->private;

	gb_i2s_mgmt_free_cfgs(snd_dev);

	kfree(snd_dev->send_data_req_buf);
	snd_dev->send_data_req_buf = NULL;

	snd_dev->mgmt_connection = NULL;
	gb_free_snd(snd_dev);
}

static int gb_i2s_mgmt_report_event_recv(u8 type, struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_i2s_mgmt_report_event_request *req = op->request->payload;
	char *event_name;

	if (type != GB_I2S_MGMT_TYPE_REPORT_EVENT) {
		dev_err(&connection->dev, "Invalid request type: %d\n",
			type);
		return -EINVAL;
	}

	if (op->request->payload_size < sizeof(*req)) {
		dev_err(&connection->dev, "Short request received: %zu, %zu\n",
			op->request->payload_size, sizeof(*req));
		return -EINVAL;
	}

	switch (req->event) {
	case GB_I2S_MGMT_EVENT_UNSPECIFIED:
		event_name = "UNSPECIFIED";
		break;
	case GB_I2S_MGMT_EVENT_HALT:
		/* XXX Should stop streaming now */
		event_name = "HALT";
		break;
	case GB_I2S_MGMT_EVENT_INTERNAL_ERROR:
		event_name = "INTERNAL_ERROR";
		break;
	case GB_I2S_MGMT_EVENT_PROTOCOL_ERROR:
		event_name = "PROTOCOL_ERROR";
		break;
	case GB_I2S_MGMT_EVENT_FAILURE:
		event_name = "FAILURE";
		break;
	case GB_I2S_MGMT_EVENT_OUT_OF_SEQUENCE:
		event_name = "OUT_OF_SEQUENCE";
		break;
	case GB_I2S_MGMT_EVENT_UNDERRUN:
		event_name = "UNDERRUN";
		break;
	case GB_I2S_MGMT_EVENT_OVERRUN:
		event_name = "OVERRUN";
		break;
	case GB_I2S_MGMT_EVENT_CLOCKING:
		event_name = "CLOCKING";
		break;
	case GB_I2S_MGMT_EVENT_DATA_LEN:
		event_name = "DATA_LEN";
		break;
	default:
		dev_warn(&connection->dev, "Unknown I2S Event received: %d\n",
			 req->event);
		return -EINVAL;
	}

	dev_warn(&connection->dev, "I2S Event received: %d - '%s'\n",
		 req->event, event_name);

	return 0;
}

static struct gb_protocol gb_i2s_receiver_protocol = {
	.name			= GB_AUDIO_DATA_DRIVER_NAME,
	.id			= GREYBUS_PROTOCOL_I2S_RECEIVER,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_i2s_transmitter_connection_init,
	.connection_exit	= gb_i2s_transmitter_connection_exit,
	.request_recv		= NULL,
};

static struct gb_protocol gb_i2s_mgmt_protocol = {
	.name			= GB_AUDIO_MGMT_DRIVER_NAME,
	.id			= GREYBUS_PROTOCOL_I2S_MGMT,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_i2s_mgmt_connection_init,
	.connection_exit	= gb_i2s_mgmt_connection_exit,
	.request_recv		= gb_i2s_mgmt_report_event_recv,
};


/*
 * This is the basic hook get things initialized and registered w/ gb
 */

int gb_audio_protocol_init(void)
{
	int err;

	err = gb_protocol_register(&gb_i2s_mgmt_protocol);
	if (err) {
		pr_err("Can't register i2s mgmt protocol driver: %d\n", -err);
		return err;
	}

	err = gb_protocol_register(&gb_i2s_receiver_protocol);
	if (err) {
		pr_err("Can't register Audio protocol driver: %d\n", -err);
		goto err_unregister_i2s_mgmt;
	}

	err = platform_driver_register(&gb_audio_plat_driver);
	if (err) {
		pr_err("Can't register platform driver: %d\n", -err);
		goto err_unregister_plat;
	}

	err = platform_driver_register(&gb_audio_pcm_driver);
	if (err) {
		pr_err("Can't register pcm driver: %d\n", -err);
		goto err_unregister_pcm;
	}

	return 0;

err_unregister_pcm:
	platform_driver_unregister(&gb_audio_plat_driver);
err_unregister_plat:
	gb_protocol_deregister(&gb_i2s_receiver_protocol);
err_unregister_i2s_mgmt:
	gb_protocol_deregister(&gb_i2s_mgmt_protocol);
	return err;
}

void gb_audio_protocol_exit(void)
{
	platform_driver_unregister(&gb_audio_pcm_driver);
	platform_driver_unregister(&gb_audio_plat_driver);
	gb_protocol_deregister(&gb_i2s_receiver_protocol);
	gb_protocol_deregister(&gb_i2s_mgmt_protocol);
}

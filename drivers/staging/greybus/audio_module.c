/*
 * Greybus audio driver
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/msm-dynamic-dailink.h>

#include "audio_codec.h"
#include "audio_apbridgea.h"
#include "audio_manager.h"

static DEFINE_MUTEX(gb_codec_list_lock);
static LIST_HEAD(gb_codec_list);

/*
 * gb_snd management functions
 */

static int gbaudio_codec_request_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_audio_streaming_event_request *req = op->request->payload;

	dev_warn(&connection->bundle->dev,
		 "Audio Event received: cport: %u, event: %u\n",
		 req->data_cport, req->event);

	return 0;
}

static int gbaudio_data_connection_request_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;

	dev_warn(&connection->bundle->dev, "Audio Event received\n");

	return 0;
}

static int gb_audio_add_mgmt_connection(struct gbaudio_module_info *gbmodule,
				struct greybus_descriptor_cport *cport_desc,
				struct gb_bundle *bundle)
{
	struct gb_connection *connection;

	/* Management Cport */
	if (gbmodule->mgmt_connection) {
		dev_err(&bundle->dev,
			"Can't have multiple Management connections\n");
		return -ENODEV;
	}

	connection = gb_connection_create(bundle, le16_to_cpu(cport_desc->id),
					  gbaudio_codec_request_handler);
	if (IS_ERR(connection))
		return PTR_ERR(connection);

	connection->private = gbmodule;
	gbmodule->mgmt_connection = connection;

	return 0;
}

static int gb_audio_add_data_connection(struct gbaudio_module_info *gbmodule,
				struct greybus_descriptor_cport *cport_desc,
				struct gb_bundle *bundle)
{
	struct gb_connection *connection;
	struct gbaudio_data_connection *dai;

	dai = devm_kzalloc(gbmodule->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai) {
		dev_err(gbmodule->dev, "DAI Malloc failure\n");
		return -ENOMEM;
	}

	connection = gb_connection_create_flags(bundle,
					le16_to_cpu(cport_desc->id),
					gbaudio_data_connection_request_handler,
					GB_CONNECTION_FLAG_CSD);
	if (IS_ERR(connection)) {
		devm_kfree(gbmodule->dev, dai);
		return PTR_ERR(connection);
	}

	connection->private = gbmodule;
	/* dai->name should be same as codec->dai_name */
	strlcpy(dai->name, "greybus-apb1", NAME_SIZE);
	dai->data_cport = connection->intf_cport_id;
	dai->connection = connection;
	list_add(&dai->list, &gbmodule->data_list);

	return 0;
}

/*
 * This is the basic hook get things initialized and registered w/ gb
 */

static int gb_audio_probe(struct gb_bundle *bundle,
			  const struct greybus_bundle_id *id)
{
	struct device *dev = &bundle->dev;
	struct gbaudio_module_info *gbmodule;
	struct greybus_descriptor_cport *cport_desc;
	struct gb_audio_manager_module_descriptor desc;
	struct gbaudio_data_connection *dai, *_dai;
	int ret, i;
	struct gb_audio_topology *topology;


	/* There should be at least one Management and one Data cport */
	if (bundle->num_cports < 2)
		return -ENODEV;

	mutex_lock(&gb_codec_list_lock);
	/*
	 * There can be only one Management connection and any number of data
	 * connections.
	 */
	gbmodule = devm_kzalloc(dev, sizeof(*gbmodule), GFP_KERNEL);
	if (!gbmodule) {
		mutex_unlock(&gb_codec_list_lock);
		return -ENOMEM;
	}

	gbmodule->num_data_connections = bundle->num_cports - 1;
	mutex_init(&gbmodule->lock);
	INIT_LIST_HEAD(&gbmodule->data_list);
	INIT_LIST_HEAD(&gbmodule->widget_list);
	INIT_LIST_HEAD(&gbmodule->ctl_list);
	INIT_LIST_HEAD(&gbmodule->widget_ctl_list);
	gbmodule->dev = dev;
	snprintf(gbmodule->name, NAME_SIZE, "%s.%s", dev->driver->name,
		 dev_name(dev));
	greybus_set_drvdata(bundle, gbmodule);

	/* Create all connections */
	for (i = 0; i < bundle->num_cports; i++) {
		cport_desc = &bundle->cport_desc[i];

		switch (cport_desc->protocol_id) {
		case GREYBUS_PROTOCOL_AUDIO_MGMT:
			ret = gb_audio_add_mgmt_connection(gbmodule, cport_desc,
							   bundle);
			if (ret)
				goto destroy_connections;
			break;
		case GREYBUS_PROTOCOL_AUDIO_DATA:
			ret = gb_audio_add_data_connection(gbmodule, cport_desc,
							   bundle);
			if (ret)
				goto destroy_connections;
			break;
		default:
			dev_err(dev, "Unsupported protocol: 0x%02x\n",
				cport_desc->protocol_id);
			ret = -ENODEV;
			goto destroy_connections;
		}
	}

	/* There must be a management cport */
	if (!gbmodule->mgmt_connection) {
		ret = -EINVAL;
		dev_err(dev, "Missing management connection\n");
		goto destroy_connections;
	}

	/* Initialize management connection */
	ret = gb_connection_enable(gbmodule->mgmt_connection);
	if (ret) {
		dev_err(dev, "%d: Error while enabling mgmt connection\n", ret);
		goto destroy_connections;
	}
	gbmodule->dev_id = gbmodule->mgmt_connection->intf->interface_id;

	/*
	 * FIXME: malloc for topology happens via audio_gb driver
	 * should be done within codec driver itself
	 */
	ret = gb_audio_gb_get_topology(gbmodule->mgmt_connection, &topology);
	if (ret) {
		dev_err(dev, "%d:Error while fetching topology\n", ret);
		goto disable_connection;
	}

	/* process topology data */
	ret = gbaudio_tplg_parse_data(gbmodule, topology);
	if (ret) {
		dev_err(dev, "%d:Error while parsing topology data\n",
			  ret);
		goto free_topology;
	}
	gbmodule->topology = topology;

	/* register module with gbcodec */
	ret = gbaudio_register_module(gbmodule);
	if (ret)
		goto release_topology;

	/* Initialize data connections */
	list_for_each_entry(dai, &gbmodule->data_list, list) {
		ret = gb_connection_enable(dai->connection);
		if (ret)
			goto disable_data_connection;
	}
	gbmodule->is_connected = 1;

	/* inform above layer for uevent */
	dev_dbg(dev, "Inform set_event:%d to above layer\n", 1);
	/* prepare for the audio manager */
	strlcpy(desc.name, gbmodule->name, GB_AUDIO_MANAGER_MODULE_NAME_LEN);
	desc.slot = 1; /* todo */
	desc.vid = 2; /* todo */
	desc.pid = 3; /* todo */
	desc.cport = gbmodule->dev_id;
	desc.devices = 0x2; /* todo */
	gbmodule->manager_id = gb_audio_manager_add(&desc);

	dev_dbg(dev, "Add GB Audio device:%s\n", gbmodule->name);
	mutex_unlock(&gb_codec_list_lock);

	return 0;

disable_data_connection:
	list_for_each_entry_safe(dai, _dai, &gbmodule->data_list, list)
		gb_connection_disable(dai->connection);
	gbaudio_unregister_module(gbmodule);

release_topology:
	gbaudio_tplg_release(gbmodule);
	gbmodule->topology = NULL;

free_topology:
	kfree(topology);

disable_connection:
	gb_connection_disable(gbmodule->mgmt_connection);

destroy_connections:
	list_for_each_entry_safe(dai, _dai, &gbmodule->data_list, list) {
		gb_connection_destroy(dai->connection);
		list_del(&dai->list);
		devm_kfree(dev, dai);
	}

	if (gbmodule->mgmt_connection)
		gb_connection_destroy(gbmodule->mgmt_connection);

	devm_kfree(dev, gbmodule);
	mutex_unlock(&gb_codec_list_lock);

	return ret;
}

static void gb_audio_disconnect(struct gb_bundle *bundle)
{
	struct gbaudio_module_info *gbmodule = greybus_get_drvdata(bundle);
	struct gbaudio_data_connection *dai, *_dai;

	mutex_lock(&gb_codec_list_lock);

	gbaudio_unregister_module(gbmodule);

	/* inform uevent to above layers */
	gb_audio_manager_remove(gbmodule->manager_id);

	gbaudio_tplg_release(gbmodule);
	gbmodule->topology = NULL;
	kfree(gbmodule->topology);
	gb_connection_disable(gbmodule->mgmt_connection);
	list_for_each_entry_safe(dai, _dai, &gbmodule->data_list, list) {
		gb_connection_disable(dai->connection);
		gb_connection_destroy(dai->connection);
		list_del(&dai->list);
		devm_kfree(gbmodule->dev, dai);
	}
	gb_connection_destroy(gbmodule->mgmt_connection);
	gbmodule->mgmt_connection = NULL;

	devm_kfree(&bundle->dev, gbmodule);
	mutex_unlock(&gb_codec_list_lock);
}

static const struct greybus_bundle_id gb_audio_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_AUDIO) },
	{ }
};
MODULE_DEVICE_TABLE(greybus, gb_audio_id_table);

static struct greybus_driver gb_audio_driver = {
	.name		= "gb-audio",
	.probe		= gb_audio_probe,
	.disconnect	= gb_audio_disconnect,
	.id_table	= gb_audio_id_table,
};
module_greybus_driver(gb_audio_driver);

MODULE_DESCRIPTION("Greybus Audio module driver");
MODULE_AUTHOR("Vaibhav Agarwal <vaibhav.agarwal@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gbaudio-module");

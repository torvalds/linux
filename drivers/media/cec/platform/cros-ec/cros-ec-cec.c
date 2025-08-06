// SPDX-License-Identifier: GPL-2.0+
/*
 * CEC driver for ChromeOS Embedded Controller
 *
 * Copyright (c) 2018 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/dmi.h>
#include <linux/pci.h>
#include <linux/cec.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <media/cec.h>
#include <media/cec-notifier.h>

#define DRV_NAME	"cros-ec-cec"

/**
 * struct cros_ec_cec_port - Driver data for a single EC CEC port
 *
 * @port_num: port number
 * @adap: CEC adapter
 * @notify: CEC notifier pointer
 * @rx_msg: storage for a received message
 * @cros_ec_cec: pointer to the parent struct
 */
struct cros_ec_cec_port {
	int port_num;
	struct cec_adapter *adap;
	struct cec_notifier *notify;
	struct cec_msg rx_msg;
	struct cros_ec_cec *cros_ec_cec;
};

/**
 * struct cros_ec_cec - Driver data for EC CEC
 *
 * @cros_ec: Pointer to EC device
 * @notifier: Notifier info for responding to EC events
 * @write_cmd_version: Highest supported version of EC_CMD_CEC_WRITE_MSG.
 * @num_ports: Number of CEC ports
 * @ports: Array of ports
 */
struct cros_ec_cec {
	struct cros_ec_device *cros_ec;
	struct notifier_block notifier;
	int write_cmd_version;
	int num_ports;
	struct cros_ec_cec_port *ports[EC_CEC_MAX_PORTS];
};

static void cros_ec_cec_received_message(struct cros_ec_cec_port *port,
					 uint8_t *msg, uint8_t len)
{
	if (len > CEC_MAX_MSG_SIZE)
		len = CEC_MAX_MSG_SIZE;

	port->rx_msg.len = len;
	memcpy(port->rx_msg.msg, msg, len);

	cec_received_msg(port->adap, &port->rx_msg);
}

static void handle_cec_message(struct cros_ec_cec *cros_ec_cec)
{
	struct cros_ec_device *cros_ec = cros_ec_cec->cros_ec;
	uint8_t *cec_message = cros_ec->event_data.data.cec_message;
	unsigned int len = cros_ec->event_size;
	struct cros_ec_cec_port *port;
	/*
	 * There are two ways of receiving CEC messages:
	 * 1. Old EC firmware which only supports one port sends the data in a
	 *    cec_message MKBP event.
	 * 2. New EC firmware which supports multiple ports uses
	 *    EC_MKBP_CEC_HAVE_DATA to notify that data is ready and
	 *    EC_CMD_CEC_READ_MSG to read it.
	 * Check that the EC only has one CEC port, and then we can assume the
	 * message is from port 0.
	 */
	if (cros_ec_cec->num_ports != 1) {
		dev_err(cros_ec->dev,
			"received cec_message on device with %d ports\n",
			cros_ec_cec->num_ports);
		return;
	}
	port = cros_ec_cec->ports[0];

	cros_ec_cec_received_message(port, cec_message, len);
}

static void cros_ec_cec_read_message(struct cros_ec_cec_port *port)
{
	struct cros_ec_device *cros_ec = port->cros_ec_cec->cros_ec;
	struct ec_params_cec_read params = {
		.port = port->port_num,
	};
	struct ec_response_cec_read response;
	int ret;

	ret = cros_ec_cmd(cros_ec, 0, EC_CMD_CEC_READ_MSG, &params,
			  sizeof(params), &response, sizeof(response));
	if (ret < 0) {
		dev_err(cros_ec->dev,
			"error reading CEC message on EC: %d\n", ret);
		return;
	}

	cros_ec_cec_received_message(port, response.msg, response.msg_len);
}

static void handle_cec_event(struct cros_ec_cec *cros_ec_cec)
{
	struct cros_ec_device *cros_ec = cros_ec_cec->cros_ec;
	uint32_t cec_events = cros_ec->event_data.data.cec_events;
	uint32_t port_num = EC_MKBP_EVENT_CEC_GET_PORT(cec_events);
	uint32_t events = EC_MKBP_EVENT_CEC_GET_EVENTS(cec_events);
	struct cros_ec_cec_port *port;

	if (port_num >= cros_ec_cec->num_ports) {
		dev_err(cros_ec->dev,
			"received CEC event for invalid port %d\n", port_num);
		return;
	}
	port = cros_ec_cec->ports[port_num];

	if (events & EC_MKBP_CEC_SEND_OK)
		cec_transmit_attempt_done(port->adap, CEC_TX_STATUS_OK);

	/* FW takes care of all retries, tell core to avoid more retries */
	if (events & EC_MKBP_CEC_SEND_FAILED)
		cec_transmit_attempt_done(port->adap,
					  CEC_TX_STATUS_MAX_RETRIES |
					  CEC_TX_STATUS_NACK);

	if (events & EC_MKBP_CEC_HAVE_DATA)
		cros_ec_cec_read_message(port);
}

static int cros_ec_cec_event(struct notifier_block *nb,
			     unsigned long queued_during_suspend,
			     void *_notify)
{
	struct cros_ec_cec *cros_ec_cec;
	struct cros_ec_device *cros_ec;

	cros_ec_cec = container_of(nb, struct cros_ec_cec, notifier);
	cros_ec = cros_ec_cec->cros_ec;

	if (cros_ec->event_data.event_type == EC_MKBP_EVENT_CEC_EVENT) {
		handle_cec_event(cros_ec_cec);
		return NOTIFY_OK;
	}

	if (cros_ec->event_data.event_type == EC_MKBP_EVENT_CEC_MESSAGE) {
		handle_cec_message(cros_ec_cec);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static int cros_ec_cec_set_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct cros_ec_cec_port *port = adap->priv;
	struct cros_ec_cec *cros_ec_cec = port->cros_ec_cec;
	struct cros_ec_device *cros_ec = cros_ec_cec->cros_ec;
	struct ec_params_cec_set params = {
		.cmd = CEC_CMD_LOGICAL_ADDRESS,
		.port = port->port_num,
		.val = logical_addr,
	};
	int ret;

	ret = cros_ec_cmd(cros_ec, 0, EC_CMD_CEC_SET, &params, sizeof(params),
			  NULL, 0);
	if (ret < 0) {
		dev_err(cros_ec->dev,
			"error setting CEC logical address on EC: %d\n", ret);
		return ret;
	}

	return 0;
}

static int cros_ec_cec_transmit(struct cec_adapter *adap, u8 attempts,
				u32 signal_free_time, struct cec_msg *cec_msg)
{
	struct cros_ec_cec_port *port = adap->priv;
	struct cros_ec_cec *cros_ec_cec = port->cros_ec_cec;
	struct cros_ec_device *cros_ec = cros_ec_cec->cros_ec;
	struct ec_params_cec_write params;
	struct ec_params_cec_write_v1 params_v1;
	int ret;

	if (cros_ec_cec->write_cmd_version == 0) {
		memcpy(params.msg, cec_msg->msg, cec_msg->len);
		ret = cros_ec_cmd(cros_ec, 0, EC_CMD_CEC_WRITE_MSG, &params,
				  cec_msg->len, NULL, 0);
	} else {
		params_v1.port = port->port_num;
		params_v1.msg_len = cec_msg->len;
		memcpy(params_v1.msg, cec_msg->msg, cec_msg->len);
		ret = cros_ec_cmd(cros_ec, cros_ec_cec->write_cmd_version,
				  EC_CMD_CEC_WRITE_MSG, &params_v1,
				  sizeof(params_v1), NULL, 0);
	}

	if (ret < 0) {
		dev_err(cros_ec->dev,
			"error writing CEC msg on EC: %d\n", ret);
		return ret;
	}

	return 0;
}

static int cros_ec_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct cros_ec_cec_port *port = adap->priv;
	struct cros_ec_cec *cros_ec_cec = port->cros_ec_cec;
	struct cros_ec_device *cros_ec = cros_ec_cec->cros_ec;
	struct ec_params_cec_set params = {
		.cmd = CEC_CMD_ENABLE,
		.port = port->port_num,
		.val = enable,
	};
	int ret;

	ret = cros_ec_cmd(cros_ec, 0, EC_CMD_CEC_SET, &params, sizeof(params),
			  NULL, 0);
	if (ret < 0) {
		dev_err(cros_ec->dev,
			"error %sabling CEC on EC: %d\n",
			(enable ? "en" : "dis"), ret);
		return ret;
	}

	return 0;
}

static const struct cec_adap_ops cros_ec_cec_ops = {
	.adap_enable = cros_ec_cec_adap_enable,
	.adap_log_addr = cros_ec_cec_set_log_addr,
	.adap_transmit = cros_ec_cec_transmit,
};

#ifdef CONFIG_PM_SLEEP
static int cros_ec_cec_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cros_ec_cec *cros_ec_cec = dev_get_drvdata(&pdev->dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(cros_ec_cec->cros_ec->irq);

	return 0;
}

static int cros_ec_cec_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cros_ec_cec *cros_ec_cec = dev_get_drvdata(&pdev->dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(cros_ec_cec->cros_ec->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(cros_ec_cec_pm_ops,
	cros_ec_cec_suspend, cros_ec_cec_resume);

#if IS_ENABLED(CONFIG_PCI) && IS_ENABLED(CONFIG_DMI)

/*
 * Specify the DRM device name handling the HDMI output and the HDMI connector
 * corresponding to each CEC port. The order of connectors must match the order
 * in the EC (first connector is EC port 0, ...), and the number of connectors
 * must match the number of ports in the EC (which can be queried using the
 * EC_CMD_CEC_PORT_COUNT host command).
 */

struct cec_dmi_match {
	const char *sys_vendor;
	const char *product_name;
	const char *devname;
	const char *const *conns;
};

static const char *const port_b_conns[] = { "Port B", NULL };
static const char *const port_db_conns[] = { "Port D", "Port B", NULL };
static const char *const port_ba_conns[] = { "Port B", "Port A", NULL };
static const char *const port_ab_conns[] = { "Port A", "Port B", NULL };
static const char *const port_d_conns[] = { "Port D", NULL };

static const struct cec_dmi_match cec_dmi_match_table[] = {
	/* Google Fizz */
	{ "Google", "Fizz", "0000:00:02.0", port_b_conns },
	/* Google Brask */
	{ "Google", "Brask", "0000:00:02.0", port_b_conns },
	/* Google Moli */
	{ "Google", "Moli", "0000:00:02.0", port_b_conns },
	/* Google Kinox */
	{ "Google", "Kinox", "0000:00:02.0", port_b_conns },
	/* Google Kuldax */
	{ "Google", "Kuldax", "0000:00:02.0", port_b_conns },
	/* Google Aurash */
	{ "Google", "Aurash", "0000:00:02.0", port_b_conns },
	/* Google Gladios */
	{ "Google", "Gladios", "0000:00:02.0", port_b_conns },
	/* Google Lisbon */
	{ "Google", "Lisbon", "0000:00:02.0", port_b_conns },
	/* Google Dibbi */
	{ "Google", "Dibbi", "0000:00:02.0", port_db_conns },
	/* Google Constitution */
	{ "Google", "Constitution", "0000:00:02.0", port_ba_conns },
	/* Google Boxy */
	{ "Google", "Boxy", "0000:00:02.0", port_d_conns },
	/* Google Taranza */
	{ "Google", "Taranza", "0000:00:02.0", port_db_conns },
	/* Google Dexi */
	{ "Google", "Dexi", "0000:00:02.0", port_db_conns },
	/* Google Dita */
	{ "Google", "Dita", "0000:00:02.0", port_db_conns },
	/* Google Dirks */
	{ "Google", "Dirks", "0000:00:02.0", port_ab_conns },
	/* Google Moxie */
	{ "Google", "Moxie", "0000:00:02.0", port_b_conns },
};

static struct device *cros_ec_cec_find_hdmi_dev(struct device *dev,
						const char * const **conns)
{
	int i;

	for (i = 0 ; i < ARRAY_SIZE(cec_dmi_match_table) ; ++i) {
		const struct cec_dmi_match *m = &cec_dmi_match_table[i];

		if (dmi_match(DMI_SYS_VENDOR, m->sys_vendor) &&
		    dmi_match(DMI_PRODUCT_NAME, m->product_name)) {
			struct device *d;

			/* Find the device, bail out if not yet registered */
			d = bus_find_device_by_name(&pci_bus_type, NULL,
						    m->devname);
			if (!d)
				return ERR_PTR(-EPROBE_DEFER);
			put_device(d);
			*conns = m->conns;
			return d;
		}
	}

	/* Hardware support must be added in the cec_dmi_match_table */
	dev_warn(dev, "CEC notifier not configured for this hardware\n");

	return ERR_PTR(-ENODEV);
}

#else

static struct device *cros_ec_cec_find_hdmi_dev(struct device *dev,
						const char * const **conns)
{
	return ERR_PTR(-ENODEV);
}

#endif

static int cros_ec_cec_get_num_ports(struct cros_ec_cec *cros_ec_cec)
{
	struct ec_response_cec_port_count response;
	int ret;

	ret = cros_ec_cmd(cros_ec_cec->cros_ec, 0, EC_CMD_CEC_PORT_COUNT, NULL,
			  0, &response, sizeof(response));
	if (ret < 0) {
		/*
		 * Old EC firmware only supports one port and does not support
		 * the port count command, so fall back to assuming one port.
		 */
		cros_ec_cec->num_ports = 1;
		return 0;
	}

	if (response.port_count == 0) {
		dev_err(cros_ec_cec->cros_ec->dev,
			"EC reports 0 CEC ports\n");
		return -ENODEV;
	}

	if (response.port_count > EC_CEC_MAX_PORTS) {
		dev_err(cros_ec_cec->cros_ec->dev,
			"EC reports too many ports: %d\n", response.port_count);
		return -EINVAL;
	}

	cros_ec_cec->num_ports = response.port_count;
	return 0;
}

static int cros_ec_cec_get_write_cmd_version(struct cros_ec_cec *cros_ec_cec)
{
	struct cros_ec_device *cros_ec = cros_ec_cec->cros_ec;
	struct ec_params_get_cmd_versions_v1 params = {
		.cmd = EC_CMD_CEC_WRITE_MSG,
	};
	struct ec_response_get_cmd_versions response;
	int ret;

	ret = cros_ec_cmd(cros_ec, 1, EC_CMD_GET_CMD_VERSIONS, &params,
			  sizeof(params), &response, sizeof(response));
	if (ret < 0) {
		dev_err(cros_ec->dev,
			"error getting CEC write command version: %d\n", ret);
		return ret;
	}

	if (response.version_mask & EC_VER_MASK(1)) {
		cros_ec_cec->write_cmd_version = 1;
	} else {
		if (cros_ec_cec->num_ports != 1) {
			dev_err(cros_ec->dev,
				"v0 write command only supports 1 port, %d reported\n",
				cros_ec_cec->num_ports);
			return -EINVAL;
		}
		cros_ec_cec->write_cmd_version = 0;
	}

	return 0;
}

static int cros_ec_cec_init_port(struct device *dev,
				 struct cros_ec_cec *cros_ec_cec,
				 int port_num, struct device *hdmi_dev,
				 const char * const *conns)
{
	struct cros_ec_cec_port *port;
	int ret;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->cros_ec_cec = cros_ec_cec;
	port->port_num = port_num;

	port->adap = cec_allocate_adapter(&cros_ec_cec_ops, port, DRV_NAME,
					  CEC_CAP_DEFAULTS |
					  CEC_CAP_CONNECTOR_INFO, 1);
	if (IS_ERR(port->adap))
		return PTR_ERR(port->adap);

	if (!conns[port_num]) {
		dev_err(dev, "no conn for port %d\n", port_num);
		ret = -ENODEV;
		goto out_probe_adapter;
	}

	port->notify = cec_notifier_cec_adap_register(hdmi_dev, conns[port_num],
						      port->adap);
	if (!port->notify) {
		ret = -ENOMEM;
		goto out_probe_adapter;
	}

	ret = cec_register_adapter(port->adap, dev);
	if (ret < 0)
		goto out_probe_notify;

	cros_ec_cec->ports[port_num] = port;

	return 0;

out_probe_notify:
	cec_notifier_cec_adap_unregister(port->notify, port->adap);
out_probe_adapter:
	cec_delete_adapter(port->adap);
	return ret;
}

static int cros_ec_cec_probe(struct platform_device *pdev)
{
	struct cros_ec_dev *ec_dev = dev_get_drvdata(pdev->dev.parent);
	struct cros_ec_device *cros_ec = ec_dev->ec_dev;
	struct cros_ec_cec *cros_ec_cec;
	struct cros_ec_cec_port *port;
	struct device *hdmi_dev;
	const char * const *conns = NULL;
	int ret;

	hdmi_dev = cros_ec_cec_find_hdmi_dev(&pdev->dev, &conns);
	if (IS_ERR(hdmi_dev))
		return PTR_ERR(hdmi_dev);

	cros_ec_cec = devm_kzalloc(&pdev->dev, sizeof(*cros_ec_cec),
				   GFP_KERNEL);
	if (!cros_ec_cec)
		return -ENOMEM;

	platform_set_drvdata(pdev, cros_ec_cec);
	cros_ec_cec->cros_ec = cros_ec;

	device_init_wakeup(&pdev->dev, 1);

	ret = cros_ec_cec_get_num_ports(cros_ec_cec);
	if (ret)
		return ret;

	ret = cros_ec_cec_get_write_cmd_version(cros_ec_cec);
	if (ret)
		return ret;

	for (int i = 0; i < cros_ec_cec->num_ports; i++) {
		ret = cros_ec_cec_init_port(&pdev->dev, cros_ec_cec, i,
					    hdmi_dev, conns);
		if (ret)
			goto unregister_ports;
	}

	/* Get CEC events from the EC. */
	cros_ec_cec->notifier.notifier_call = cros_ec_cec_event;
	ret = blocking_notifier_chain_register(&cros_ec->event_notifier,
					       &cros_ec_cec->notifier);
	if (ret) {
		dev_err(&pdev->dev, "failed to register notifier\n");
		goto unregister_ports;
	}

	return 0;

unregister_ports:
	/*
	 * Unregister any adapters which have been registered. We don't add the
	 * port to the array until the adapter has been registered successfully,
	 * so any non-NULL ports must have been registered.
	 */
	for (int i = 0; i < cros_ec_cec->num_ports; i++) {
		port = cros_ec_cec->ports[i];
		if (!port)
			break;
		cec_notifier_cec_adap_unregister(port->notify, port->adap);
		cec_unregister_adapter(port->adap);
	}
	return ret;
}

static void cros_ec_cec_remove(struct platform_device *pdev)
{
	struct cros_ec_cec *cros_ec_cec = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct cros_ec_cec_port *port;
	int ret;

	/*
	 * blocking_notifier_chain_unregister() only fails if the notifier isn't
	 * in the list. We know it was added to it by .probe(), so there should
	 * be no need for error checking. Be cautious and still check.
	 */
	ret = blocking_notifier_chain_unregister(
			&cros_ec_cec->cros_ec->event_notifier,
			&cros_ec_cec->notifier);
	if (ret)
		dev_err(dev, "failed to unregister notifier\n");

	for (int i = 0; i < cros_ec_cec->num_ports; i++) {
		port = cros_ec_cec->ports[i];
		cec_notifier_cec_adap_unregister(port->notify, port->adap);
		cec_unregister_adapter(port->adap);
	}
}

static const struct platform_device_id cros_ec_cec_id[] = {
	{ DRV_NAME, 0 },
	{}
};
MODULE_DEVICE_TABLE(platform, cros_ec_cec_id);

static struct platform_driver cros_ec_cec_driver = {
	.probe = cros_ec_cec_probe,
	.remove = cros_ec_cec_remove,
	.driver = {
		.name = DRV_NAME,
		.pm = &cros_ec_cec_pm_ops,
	},
	.id_table = cros_ec_cec_id,
};

module_platform_driver(cros_ec_cec_driver);

MODULE_DESCRIPTION("CEC driver for ChromeOS ECs");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/of_device.h>
#include <linux/soc/qcom/apr.h>
#include <linux/soc/qcom/pdr.h>
#include <linux/rpmsg.h>
#include <linux/of.h>

enum {
	PR_TYPE_APR = 0,
	PR_TYPE_GPR,
};

/* Some random values tbh which does not collide with static modules */
#define GPR_DYNAMIC_PORT_START	0x10000000
#define GPR_DYNAMIC_PORT_END	0x20000000

struct packet_router {
	struct rpmsg_endpoint *ch;
	struct device *dev;
	spinlock_t svcs_lock;
	spinlock_t rx_lock;
	struct idr svcs_idr;
	int dest_domain_id;
	int type;
	struct pdr_handle *pdr;
	struct workqueue_struct *rxwq;
	struct work_struct rx_work;
	struct list_head rx_list;
};

struct apr_rx_buf {
	struct list_head node;
	int len;
	uint8_t buf[] __counted_by(len);
};

/**
 * apr_send_pkt() - Send a apr message from apr device
 *
 * @adev: Pointer to previously registered apr device.
 * @pkt: Pointer to apr packet to send
 *
 * Return: Will be an negative on packet size on success.
 */
int apr_send_pkt(struct apr_device *adev, struct apr_pkt *pkt)
{
	struct packet_router *apr = dev_get_drvdata(adev->dev.parent);
	struct apr_hdr *hdr;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&adev->svc.lock, flags);

	hdr = &pkt->hdr;
	hdr->src_domain = APR_DOMAIN_APPS;
	hdr->src_svc = adev->svc.id;
	hdr->dest_domain = adev->domain_id;
	hdr->dest_svc = adev->svc.id;

	ret = rpmsg_trysend(apr->ch, pkt, hdr->pkt_size);
	spin_unlock_irqrestore(&adev->svc.lock, flags);

	return ret ? ret : hdr->pkt_size;
}
EXPORT_SYMBOL_GPL(apr_send_pkt);

void gpr_free_port(gpr_port_t *port)
{
	struct packet_router *gpr = port->pr;
	unsigned long flags;

	spin_lock_irqsave(&gpr->svcs_lock, flags);
	idr_remove(&gpr->svcs_idr, port->id);
	spin_unlock_irqrestore(&gpr->svcs_lock, flags);

	kfree(port);
}
EXPORT_SYMBOL_GPL(gpr_free_port);

gpr_port_t *gpr_alloc_port(struct apr_device *gdev, struct device *dev,
				gpr_port_cb cb,	void *priv)
{
	struct packet_router *pr = dev_get_drvdata(gdev->dev.parent);
	gpr_port_t *port;
	struct pkt_router_svc *svc;
	int id;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return ERR_PTR(-ENOMEM);

	svc = port;
	svc->callback = cb;
	svc->pr = pr;
	svc->priv = priv;
	svc->dev = dev;
	spin_lock_init(&svc->lock);

	spin_lock(&pr->svcs_lock);
	id = idr_alloc_cyclic(&pr->svcs_idr, svc, GPR_DYNAMIC_PORT_START,
			      GPR_DYNAMIC_PORT_END, GFP_ATOMIC);
	if (id < 0) {
		dev_err(dev, "Unable to allocate dynamic GPR src port\n");
		kfree(port);
		spin_unlock(&pr->svcs_lock);
		return ERR_PTR(id);
	}

	svc->id = id;
	spin_unlock(&pr->svcs_lock);

	return port;
}
EXPORT_SYMBOL_GPL(gpr_alloc_port);

static int pkt_router_send_svc_pkt(struct pkt_router_svc *svc, struct gpr_pkt *pkt)
{
	struct packet_router *pr = svc->pr;
	struct gpr_hdr *hdr;
	unsigned long flags;
	int ret;

	hdr = &pkt->hdr;

	spin_lock_irqsave(&svc->lock, flags);
	ret = rpmsg_trysend(pr->ch, pkt, hdr->pkt_size);
	spin_unlock_irqrestore(&svc->lock, flags);

	return ret ? ret : hdr->pkt_size;
}

int gpr_send_pkt(struct apr_device *gdev, struct gpr_pkt *pkt)
{
	return pkt_router_send_svc_pkt(&gdev->svc, pkt);
}
EXPORT_SYMBOL_GPL(gpr_send_pkt);

int gpr_send_port_pkt(gpr_port_t *port, struct gpr_pkt *pkt)
{
	return pkt_router_send_svc_pkt(port, pkt);
}
EXPORT_SYMBOL_GPL(gpr_send_port_pkt);

static void apr_dev_release(struct device *dev)
{
	struct apr_device *adev = to_apr_device(dev);

	kfree(adev);
}

static int apr_callback(struct rpmsg_device *rpdev, void *buf,
				  int len, void *priv, u32 addr)
{
	struct packet_router *apr = dev_get_drvdata(&rpdev->dev);
	struct apr_rx_buf *abuf;
	unsigned long flags;

	if (len <= APR_HDR_SIZE) {
		dev_err(apr->dev, "APR: Improper apr pkt received:%p %d\n",
			buf, len);
		return -EINVAL;
	}

	abuf = kzalloc(struct_size(abuf, buf, len), GFP_ATOMIC);
	if (!abuf)
		return -ENOMEM;

	abuf->len = len;
	memcpy(abuf->buf, buf, len);

	spin_lock_irqsave(&apr->rx_lock, flags);
	list_add_tail(&abuf->node, &apr->rx_list);
	spin_unlock_irqrestore(&apr->rx_lock, flags);

	queue_work(apr->rxwq, &apr->rx_work);

	return 0;
}

static int apr_do_rx_callback(struct packet_router *apr, struct apr_rx_buf *abuf)
{
	uint16_t hdr_size, msg_type, ver, svc_id;
	struct pkt_router_svc *svc;
	struct apr_device *adev;
	struct apr_driver *adrv = NULL;
	struct apr_resp_pkt resp;
	struct apr_hdr *hdr;
	unsigned long flags;
	void *buf = abuf->buf;
	int len = abuf->len;

	hdr = buf;
	ver = APR_HDR_FIELD_VER(hdr->hdr_field);
	if (ver > APR_PKT_VER + 1)
		return -EINVAL;

	hdr_size = APR_HDR_FIELD_SIZE_BYTES(hdr->hdr_field);
	if (hdr_size < APR_HDR_SIZE) {
		dev_err(apr->dev, "APR: Wrong hdr size:%d\n", hdr_size);
		return -EINVAL;
	}

	if (hdr->pkt_size < APR_HDR_SIZE || hdr->pkt_size != len) {
		dev_err(apr->dev, "APR: Wrong packet size\n");
		return -EINVAL;
	}

	msg_type = APR_HDR_FIELD_MT(hdr->hdr_field);
	if (msg_type >= APR_MSG_TYPE_MAX) {
		dev_err(apr->dev, "APR: Wrong message type: %d\n", msg_type);
		return -EINVAL;
	}

	if (hdr->src_domain >= APR_DOMAIN_MAX ||
			hdr->dest_domain >= APR_DOMAIN_MAX ||
			hdr->src_svc >= APR_SVC_MAX ||
			hdr->dest_svc >= APR_SVC_MAX) {
		dev_err(apr->dev, "APR: Wrong APR header\n");
		return -EINVAL;
	}

	svc_id = hdr->dest_svc;
	spin_lock_irqsave(&apr->svcs_lock, flags);
	svc = idr_find(&apr->svcs_idr, svc_id);
	if (svc && svc->dev->driver) {
		adev = svc_to_apr_device(svc);
		adrv = to_apr_driver(adev->dev.driver);
	}
	spin_unlock_irqrestore(&apr->svcs_lock, flags);

	if (!adrv || !adev) {
		dev_err(apr->dev, "APR: service is not registered (%d)\n",
			svc_id);
		return -EINVAL;
	}

	resp.hdr = *hdr;
	resp.payload_size = hdr->pkt_size - hdr_size;

	/*
	 * NOTE: hdr_size is not same as APR_HDR_SIZE as remote can include
	 * optional headers in to apr_hdr which should be ignored
	 */
	if (resp.payload_size > 0)
		resp.payload = buf + hdr_size;

	adrv->callback(adev, &resp);

	return 0;
}

static int gpr_do_rx_callback(struct packet_router *gpr, struct apr_rx_buf *abuf)
{
	uint16_t hdr_size, ver;
	struct pkt_router_svc *svc = NULL;
	struct gpr_resp_pkt resp;
	struct gpr_hdr *hdr;
	unsigned long flags;
	void *buf = abuf->buf;
	int len = abuf->len;

	hdr = buf;
	ver = hdr->version;
	if (ver > GPR_PKT_VER + 1)
		return -EINVAL;

	hdr_size = hdr->hdr_size;
	if (hdr_size < GPR_PKT_HEADER_WORD_SIZE) {
		dev_err(gpr->dev, "GPR: Wrong hdr size:%d\n", hdr_size);
		return -EINVAL;
	}

	if (hdr->pkt_size < GPR_PKT_HEADER_BYTE_SIZE || hdr->pkt_size != len) {
		dev_err(gpr->dev, "GPR: Wrong packet size\n");
		return -EINVAL;
	}

	resp.hdr = *hdr;
	resp.payload_size = hdr->pkt_size - (hdr_size * 4);

	/*
	 * NOTE: hdr_size is not same as GPR_HDR_SIZE as remote can include
	 * optional headers in to gpr_hdr which should be ignored
	 */
	if (resp.payload_size > 0)
		resp.payload = buf + (hdr_size *  4);


	spin_lock_irqsave(&gpr->svcs_lock, flags);
	svc = idr_find(&gpr->svcs_idr, hdr->dest_port);
	spin_unlock_irqrestore(&gpr->svcs_lock, flags);

	if (!svc) {
		dev_err(gpr->dev, "GPR: Port(%x) is not registered\n",
			hdr->dest_port);
		return -EINVAL;
	}

	if (svc->callback)
		svc->callback(&resp, svc->priv, 0);

	return 0;
}

static void apr_rxwq(struct work_struct *work)
{
	struct packet_router *apr = container_of(work, struct packet_router, rx_work);
	struct apr_rx_buf *abuf, *b;
	unsigned long flags;

	if (!list_empty(&apr->rx_list)) {
		list_for_each_entry_safe(abuf, b, &apr->rx_list, node) {
			switch (apr->type) {
			case PR_TYPE_APR:
				apr_do_rx_callback(apr, abuf);
				break;
			case PR_TYPE_GPR:
				gpr_do_rx_callback(apr, abuf);
				break;
			default:
				break;
			}
			spin_lock_irqsave(&apr->rx_lock, flags);
			list_del(&abuf->node);
			spin_unlock_irqrestore(&apr->rx_lock, flags);
			kfree(abuf);
		}
	}
}

static int apr_device_match(struct device *dev, struct device_driver *drv)
{
	struct apr_device *adev = to_apr_device(dev);
	struct apr_driver *adrv = to_apr_driver(drv);
	const struct apr_device_id *id = adrv->id_table;

	/* Attempt an OF style match first */
	if (of_driver_match_device(dev, drv))
		return 1;

	if (!id)
		return 0;

	while (id->domain_id != 0 || id->svc_id != 0) {
		if (id->domain_id == adev->domain_id &&
		    id->svc_id == adev->svc.id)
			return 1;
		id++;
	}

	return 0;
}

static int apr_device_probe(struct device *dev)
{
	struct apr_device *adev = to_apr_device(dev);
	struct apr_driver *adrv = to_apr_driver(dev->driver);
	int ret;

	ret = adrv->probe(adev);
	if (!ret)
		adev->svc.callback = adrv->gpr_callback;

	return ret;
}

static void apr_device_remove(struct device *dev)
{
	struct apr_device *adev = to_apr_device(dev);
	struct apr_driver *adrv = to_apr_driver(dev->driver);
	struct packet_router *apr = dev_get_drvdata(adev->dev.parent);

	if (adrv->remove)
		adrv->remove(adev);
	spin_lock(&apr->svcs_lock);
	idr_remove(&apr->svcs_idr, adev->svc.id);
	spin_unlock(&apr->svcs_lock);
}

static int apr_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct apr_device *adev = to_apr_device(dev);
	int ret;

	ret = of_device_uevent_modalias(dev, env);
	if (ret != -ENODEV)
		return ret;

	return add_uevent_var(env, "MODALIAS=apr:%s", adev->name);
}

const struct bus_type aprbus = {
	.name		= "aprbus",
	.match		= apr_device_match,
	.probe		= apr_device_probe,
	.uevent		= apr_uevent,
	.remove		= apr_device_remove,
};
EXPORT_SYMBOL_GPL(aprbus);

static int apr_add_device(struct device *dev, struct device_node *np,
			  u32 svc_id, u32 domain_id)
{
	struct packet_router *apr = dev_get_drvdata(dev);
	struct apr_device *adev = NULL;
	struct pkt_router_svc *svc;
	int ret;

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;

	adev->svc_id = svc_id;
	svc = &adev->svc;

	svc->id = svc_id;
	svc->pr = apr;
	svc->priv = adev;
	svc->dev = dev;
	spin_lock_init(&svc->lock);

	adev->domain_id = domain_id;

	if (np)
		snprintf(adev->name, APR_NAME_SIZE, "%pOFn", np);

	switch (apr->type) {
	case PR_TYPE_APR:
		dev_set_name(&adev->dev, "aprsvc:%s:%x:%x", adev->name,
			     domain_id, svc_id);
		break;
	case PR_TYPE_GPR:
		dev_set_name(&adev->dev, "gprsvc:%s:%x:%x", adev->name,
			     domain_id, svc_id);
		break;
	default:
		break;
	}

	adev->dev.bus = &aprbus;
	adev->dev.parent = dev;
	adev->dev.of_node = np;
	adev->dev.release = apr_dev_release;
	adev->dev.driver = NULL;

	spin_lock(&apr->svcs_lock);
	ret = idr_alloc(&apr->svcs_idr, svc, svc_id, svc_id + 1, GFP_ATOMIC);
	spin_unlock(&apr->svcs_lock);
	if (ret < 0) {
		dev_err(dev, "idr_alloc failed: %d\n", ret);
		goto out;
	}

	/* Protection domain is optional, it does not exist on older platforms */
	ret = of_property_read_string_index(np, "qcom,protection-domain",
					    1, &adev->service_path);
	if (ret < 0 && ret != -EINVAL) {
		dev_err(dev, "Failed to read second value of qcom,protection-domain\n");
		goto out;
	}

	dev_info(dev, "Adding APR/GPR dev: %s\n", dev_name(&adev->dev));

	ret = device_register(&adev->dev);
	if (ret) {
		dev_err(dev, "device_register failed: %d\n", ret);
		put_device(&adev->dev);
	}

out:
	return ret;
}

static int of_apr_add_pd_lookups(struct device *dev)
{
	const char *service_name, *service_path;
	struct packet_router *apr = dev_get_drvdata(dev);
	struct device_node *node;
	struct pdr_service *pds;
	int ret;

	for_each_child_of_node(dev->of_node, node) {
		ret = of_property_read_string_index(node, "qcom,protection-domain",
						    0, &service_name);
		if (ret < 0)
			continue;

		ret = of_property_read_string_index(node, "qcom,protection-domain",
						    1, &service_path);
		if (ret < 0) {
			dev_err(dev, "pdr service path missing: %d\n", ret);
			of_node_put(node);
			return ret;
		}

		pds = pdr_add_lookup(apr->pdr, service_name, service_path);
		if (IS_ERR(pds) && PTR_ERR(pds) != -EALREADY) {
			dev_err(dev, "pdr add lookup failed: %ld\n", PTR_ERR(pds));
			of_node_put(node);
			return PTR_ERR(pds);
		}
	}

	return 0;
}

static void of_register_apr_devices(struct device *dev, const char *svc_path)
{
	struct packet_router *apr = dev_get_drvdata(dev);
	struct device_node *node;
	const char *service_path;
	int ret;

	for_each_child_of_node(dev->of_node, node) {
		u32 svc_id;
		u32 domain_id;

		/*
		 * This function is called with svc_path NULL during
		 * apr_probe(), in which case we register any apr devices
		 * without a qcom,protection-domain specified.
		 *
		 * Then as the protection domains becomes available
		 * (if applicable) this function is again called, but with
		 * svc_path representing the service becoming available. In
		 * this case we register any apr devices with a matching
		 * qcom,protection-domain.
		 */

		ret = of_property_read_string_index(node, "qcom,protection-domain",
						    1, &service_path);
		if (svc_path) {
			/* skip APR services that are PD independent */
			if (ret)
				continue;

			/* skip APR services whose PD paths don't match */
			if (strcmp(service_path, svc_path))
				continue;
		} else {
			/* skip APR services whose PD lookups are registered */
			if (ret == 0)
				continue;
		}

		if (of_property_read_u32(node, "reg", &svc_id))
			continue;

		domain_id = apr->dest_domain_id;

		if (apr_add_device(dev, node, svc_id, domain_id))
			dev_err(dev, "Failed to add apr %d svc\n", svc_id);
	}
}

static int apr_remove_device(struct device *dev, void *svc_path)
{
	struct apr_device *adev = to_apr_device(dev);

	if (svc_path && adev->service_path) {
		if (!strcmp(adev->service_path, (char *)svc_path))
			device_unregister(&adev->dev);
	} else {
		device_unregister(&adev->dev);
	}

	return 0;
}

static void apr_pd_status(int state, char *svc_path, void *priv)
{
	struct packet_router *apr = (struct packet_router *)priv;

	switch (state) {
	case SERVREG_SERVICE_STATE_UP:
		of_register_apr_devices(apr->dev, svc_path);
		break;
	case SERVREG_SERVICE_STATE_DOWN:
		device_for_each_child(apr->dev, svc_path, apr_remove_device);
		break;
	}
}

static int apr_probe(struct rpmsg_device *rpdev)
{
	struct device *dev = &rpdev->dev;
	struct packet_router *apr;
	int ret;

	apr = devm_kzalloc(dev, sizeof(*apr), GFP_KERNEL);
	if (!apr)
		return -ENOMEM;

	ret = of_property_read_u32(dev->of_node, "qcom,domain", &apr->dest_domain_id);

	if (of_device_is_compatible(dev->of_node, "qcom,gpr")) {
		apr->type = PR_TYPE_GPR;
	} else {
		if (ret) /* try deprecated apr-domain property */
			ret = of_property_read_u32(dev->of_node, "qcom,apr-domain",
						   &apr->dest_domain_id);
		apr->type = PR_TYPE_APR;
	}

	if (ret) {
		dev_err(dev, "Domain ID not specified in DT\n");
		return ret;
	}

	dev_set_drvdata(dev, apr);
	apr->ch = rpdev->ept;
	apr->dev = dev;
	apr->rxwq = create_singlethread_workqueue("qcom_apr_rx");
	if (!apr->rxwq) {
		dev_err(apr->dev, "Failed to start Rx WQ\n");
		return -ENOMEM;
	}
	INIT_WORK(&apr->rx_work, apr_rxwq);

	apr->pdr = pdr_handle_alloc(apr_pd_status, apr);
	if (IS_ERR(apr->pdr)) {
		dev_err(dev, "Failed to init PDR handle\n");
		ret = PTR_ERR(apr->pdr);
		goto destroy_wq;
	}

	INIT_LIST_HEAD(&apr->rx_list);
	spin_lock_init(&apr->rx_lock);
	spin_lock_init(&apr->svcs_lock);
	idr_init(&apr->svcs_idr);

	ret = of_apr_add_pd_lookups(dev);
	if (ret)
		goto handle_release;

	of_register_apr_devices(dev, NULL);

	return 0;

handle_release:
	pdr_handle_release(apr->pdr);
destroy_wq:
	destroy_workqueue(apr->rxwq);
	return ret;
}

static void apr_remove(struct rpmsg_device *rpdev)
{
	struct packet_router *apr = dev_get_drvdata(&rpdev->dev);

	pdr_handle_release(apr->pdr);
	device_for_each_child(&rpdev->dev, NULL, apr_remove_device);
	destroy_workqueue(apr->rxwq);
}

/*
 * __apr_driver_register() - Client driver registration with aprbus
 *
 * @drv:Client driver to be associated with client-device.
 * @owner: owning module/driver
 *
 * This API will register the client driver with the aprbus
 * It is called from the driver's module-init function.
 */
int __apr_driver_register(struct apr_driver *drv, struct module *owner)
{
	drv->driver.bus = &aprbus;
	drv->driver.owner = owner;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__apr_driver_register);

/*
 * apr_driver_unregister() - Undo effect of apr_driver_register
 *
 * @drv: Client driver to be unregistered
 */
void apr_driver_unregister(struct apr_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(apr_driver_unregister);

static const struct of_device_id pkt_router_of_match[] = {
	{ .compatible = "qcom,apr"},
	{ .compatible = "qcom,apr-v2"},
	{ .compatible = "qcom,gpr"},
	{}
};
MODULE_DEVICE_TABLE(of, pkt_router_of_match);

static struct rpmsg_driver packet_router_driver = {
	.probe = apr_probe,
	.remove = apr_remove,
	.callback = apr_callback,
	.drv = {
		.name = "qcom,apr",
		.of_match_table = pkt_router_of_match,
	},
};

static int __init apr_init(void)
{
	int ret;

	ret = bus_register(&aprbus);
	if (!ret)
		ret = register_rpmsg_driver(&packet_router_driver);
	else
		bus_unregister(&aprbus);

	return ret;
}

static void __exit apr_exit(void)
{
	bus_unregister(&aprbus);
	unregister_rpmsg_driver(&packet_router_driver);
}

subsys_initcall(apr_init);
module_exit(apr_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm APR Bus");

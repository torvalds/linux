/* QLogic qedr NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/module.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_addr.h>
#include <linux/netdevice.h>
#include <linux/iommu.h>
#include <net/addrconf.h>
#include <linux/qed/qede_roce.h>
#include "qedr.h"

MODULE_DESCRIPTION("QLogic 40G/100G ROCE Driver");
MODULE_AUTHOR("QLogic Corporation");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(QEDR_MODULE_VERSION);

void qedr_ib_dispatch_event(struct qedr_dev *dev, u8 port_num,
			    enum ib_event_type type)
{
	struct ib_event ibev;

	ibev.device = &dev->ibdev;
	ibev.element.port_num = port_num;
	ibev.event = type;

	ib_dispatch_event(&ibev);
}

static enum rdma_link_layer qedr_link_layer(struct ib_device *device,
					    u8 port_num)
{
	return IB_LINK_LAYER_ETHERNET;
}

static int qedr_register_device(struct qedr_dev *dev)
{
	strlcpy(dev->ibdev.name, "qedr%d", IB_DEVICE_NAME_MAX);

	memcpy(dev->ibdev.node_desc, QEDR_NODE_DESC, sizeof(QEDR_NODE_DESC));
	dev->ibdev.owner = THIS_MODULE;

	dev->ibdev.get_link_layer = qedr_link_layer;

	return 0;
}

/* QEDR sysfs interface */
static ssize_t show_rev(struct device *device, struct device_attribute *attr,
			char *buf)
{
	struct qedr_dev *dev = dev_get_drvdata(device);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", dev->pdev->vendor);
}

static ssize_t show_hca_type(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", "HCA_TYPE_TO_SET");
}

static DEVICE_ATTR(hw_rev, S_IRUGO, show_rev, NULL);
static DEVICE_ATTR(hca_type, S_IRUGO, show_hca_type, NULL);

static struct device_attribute *qedr_attributes[] = {
	&dev_attr_hw_rev,
	&dev_attr_hca_type
};

static void qedr_remove_sysfiles(struct qedr_dev *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qedr_attributes); i++)
		device_remove_file(&dev->ibdev.dev, qedr_attributes[i]);
}

static void qedr_pci_set_atomic(struct qedr_dev *dev, struct pci_dev *pdev)
{
	struct pci_dev *bridge;
	u32 val;

	dev->atomic_cap = IB_ATOMIC_NONE;

	bridge = pdev->bus->self;
	if (!bridge)
		return;

	/* Check whether we are connected directly or via a switch */
	while (bridge && bridge->bus->parent) {
		DP_DEBUG(dev, QEDR_MSG_INIT,
			 "Device is not connected directly to root. bridge->bus->number=%d primary=%d\n",
			 bridge->bus->number, bridge->bus->primary);
		/* Need to check Atomic Op Routing Supported all the way to
		 * root complex.
		 */
		pcie_capability_read_dword(bridge, PCI_EXP_DEVCAP2, &val);
		if (!(val & PCI_EXP_DEVCAP2_ATOMIC_ROUTE)) {
			pcie_capability_clear_word(pdev,
						   PCI_EXP_DEVCTL2,
						   PCI_EXP_DEVCTL2_ATOMIC_REQ);
			return;
		}
		bridge = bridge->bus->parent->self;
	}
	bridge = pdev->bus->self;

	/* according to bridge capability */
	pcie_capability_read_dword(bridge, PCI_EXP_DEVCAP2, &val);
	if (val & PCI_EXP_DEVCAP2_ATOMIC_COMP64) {
		pcie_capability_set_word(pdev, PCI_EXP_DEVCTL2,
					 PCI_EXP_DEVCTL2_ATOMIC_REQ);
		dev->atomic_cap = IB_ATOMIC_GLOB;
	} else {
		pcie_capability_clear_word(pdev, PCI_EXP_DEVCTL2,
					   PCI_EXP_DEVCTL2_ATOMIC_REQ);
	}
}

static struct qedr_dev *qedr_add(struct qed_dev *cdev, struct pci_dev *pdev,
				 struct net_device *ndev)
{
	struct qedr_dev *dev;
	int rc = 0, i;

	dev = (struct qedr_dev *)ib_alloc_device(sizeof(*dev));
	if (!dev) {
		pr_err("Unable to allocate ib device\n");
		return NULL;
	}

	DP_DEBUG(dev, QEDR_MSG_INIT, "qedr add device called\n");

	dev->pdev = pdev;
	dev->ndev = ndev;
	dev->cdev = cdev;

	qedr_pci_set_atomic(dev, pdev);

	rc = qedr_register_device(dev);
	if (rc) {
		DP_ERR(dev, "Unable to allocate register device\n");
		goto init_err;
	}

	for (i = 0; i < ARRAY_SIZE(qedr_attributes); i++)
		if (device_create_file(&dev->ibdev.dev, qedr_attributes[i]))
			goto init_err;

	DP_DEBUG(dev, QEDR_MSG_INIT, "qedr driver loaded successfully\n");
	return dev;

init_err:
	ib_dealloc_device(&dev->ibdev);
	DP_ERR(dev, "qedr driver load failed rc=%d\n", rc);

	return NULL;
}

static void qedr_remove(struct qedr_dev *dev)
{
	/* First unregister with stack to stop all the active traffic
	 * of the registered clients.
	 */
	qedr_remove_sysfiles(dev);

	ib_dealloc_device(&dev->ibdev);
}

static int qedr_close(struct qedr_dev *dev)
{
	qedr_ib_dispatch_event(dev, 1, IB_EVENT_PORT_ERR);

	return 0;
}

static void qedr_shutdown(struct qedr_dev *dev)
{
	qedr_close(dev);
	qedr_remove(dev);
}

/* event handling via NIC driver ensures that all the NIC specific
 * initialization done before RoCE driver notifies
 * event to stack.
 */
static void qedr_notify(struct qedr_dev *dev, enum qede_roce_event event)
{
	switch (event) {
	case QEDE_UP:
		qedr_ib_dispatch_event(dev, 1, IB_EVENT_PORT_ACTIVE);
		break;
	case QEDE_DOWN:
		qedr_close(dev);
		break;
	case QEDE_CLOSE:
		qedr_shutdown(dev);
		break;
	case QEDE_CHANGE_ADDR:
		qedr_ib_dispatch_event(dev, 1, IB_EVENT_GID_CHANGE);
		break;
	default:
		pr_err("Event not supported\n");
	}
}

static struct qedr_driver qedr_drv = {
	.name = "qedr_driver",
	.add = qedr_add,
	.remove = qedr_remove,
	.notify = qedr_notify,
};

static int __init qedr_init_module(void)
{
	return qede_roce_register_driver(&qedr_drv);
}

static void __exit qedr_exit_module(void)
{
	qede_roce_unregister_driver(&qedr_drv);
}

module_init(qedr_init_module);
module_exit(qedr_exit_module);

/*
 * Keystone NetCP Core driver
 *
 * Copyright (C) 2014 Texas Instruments Incorporated
 * Authors:	Sandeep Nair <sandeep_n@ti.com>
 *		Sandeep Paulraj <s-paulraj@ti.com>
 *		Cyril Chemparathy <cyril@ti.com>
 *		Santosh Shilimkar <santosh.shilimkar@ti.com>
 *		Murali Karicheri <m-karicheri2@ti.com>
 *		Wingman Kwok <w-kwok2@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_net.h>
#include <linux/of_address.h>
#include <linux/if_vlan.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/soc/ti/knav_qmss.h>
#include <linux/soc/ti/knav_dma.h>

#include "netcp.h"

#define NETCP_SOP_OFFSET	(NET_IP_ALIGN + NET_SKB_PAD)
#define NETCP_NAPI_WEIGHT	64
#define NETCP_TX_TIMEOUT	(5 * HZ)
#define NETCP_PACKET_SIZE	(ETH_FRAME_LEN + ETH_FCS_LEN)
#define NETCP_MIN_PACKET_SIZE	ETH_ZLEN
#define NETCP_MAX_MCAST_ADDR	16

#define NETCP_EFUSE_REG_INDEX	0

#define NETCP_MOD_PROBE_SKIPPED	1
#define NETCP_MOD_PROBE_FAILED	2

#define NETCP_DEBUG (NETIF_MSG_HW	| NETIF_MSG_WOL		|	\
		    NETIF_MSG_DRV	| NETIF_MSG_LINK	|	\
		    NETIF_MSG_IFUP	| NETIF_MSG_INTR	|	\
		    NETIF_MSG_PROBE	| NETIF_MSG_TIMER	|	\
		    NETIF_MSG_IFDOWN	| NETIF_MSG_RX_ERR	|	\
		    NETIF_MSG_TX_ERR	| NETIF_MSG_TX_DONE	|	\
		    NETIF_MSG_PKTDATA	| NETIF_MSG_TX_QUEUED	|	\
		    NETIF_MSG_RX_STATUS)

#define NETCP_EFUSE_ADDR_SWAP	2

#define knav_queue_get_id(q)	knav_queue_device_control(q, \
				KNAV_QUEUE_GET_ID, (unsigned long)NULL)

#define knav_queue_enable_notify(q) knav_queue_device_control(q,	\
					KNAV_QUEUE_ENABLE_NOTIFY,	\
					(unsigned long)NULL)

#define knav_queue_disable_notify(q) knav_queue_device_control(q,	\
					KNAV_QUEUE_DISABLE_NOTIFY,	\
					(unsigned long)NULL)

#define knav_queue_get_count(q)	knav_queue_device_control(q, \
				KNAV_QUEUE_GET_COUNT, (unsigned long)NULL)

#define for_each_netcp_module(module)			\
	list_for_each_entry(module, &netcp_modules, module_list)

#define for_each_netcp_device_module(netcp_device, inst_modpriv) \
	list_for_each_entry(inst_modpriv, \
		&((netcp_device)->modpriv_head), inst_list)

#define for_each_module(netcp, intf_modpriv)			\
	list_for_each_entry(intf_modpriv, &netcp->module_head, intf_list)

/* Module management structures */
struct netcp_device {
	struct list_head	device_list;
	struct list_head	interface_head;
	struct list_head	modpriv_head;
	struct device		*device;
};

struct netcp_inst_modpriv {
	struct netcp_device	*netcp_device;
	struct netcp_module	*netcp_module;
	struct list_head	inst_list;
	void			*module_priv;
};

struct netcp_intf_modpriv {
	struct netcp_intf	*netcp_priv;
	struct netcp_module	*netcp_module;
	struct list_head	intf_list;
	void			*module_priv;
};

static LIST_HEAD(netcp_devices);
static LIST_HEAD(netcp_modules);
static DEFINE_MUTEX(netcp_modules_lock);

static int netcp_debug_level = -1;
module_param(netcp_debug_level, int, 0);
MODULE_PARM_DESC(netcp_debug_level, "Netcp debug level (NETIF_MSG bits) (0=none,...,16=all)");

/* Helper functions - Get/Set */
static void get_pkt_info(dma_addr_t *buff, u32 *buff_len, dma_addr_t *ndesc,
			 struct knav_dma_desc *desc)
{
	*buff_len = le32_to_cpu(desc->buff_len);
	*buff = le32_to_cpu(desc->buff);
	*ndesc = le32_to_cpu(desc->next_desc);
}

static u32 get_sw_data(int index, struct knav_dma_desc *desc)
{
	/* No Endian conversion needed as this data is untouched by hw */
	return desc->sw_data[index];
}

/* use these macros to get sw data */
#define GET_SW_DATA0(desc) get_sw_data(0, desc)
#define GET_SW_DATA1(desc) get_sw_data(1, desc)
#define GET_SW_DATA2(desc) get_sw_data(2, desc)
#define GET_SW_DATA3(desc) get_sw_data(3, desc)

static void get_org_pkt_info(dma_addr_t *buff, u32 *buff_len,
			     struct knav_dma_desc *desc)
{
	*buff = le32_to_cpu(desc->orig_buff);
	*buff_len = le32_to_cpu(desc->orig_len);
}

static void get_words(dma_addr_t *words, int num_words, __le32 *desc)
{
	int i;

	for (i = 0; i < num_words; i++)
		words[i] = le32_to_cpu(desc[i]);
}

static void set_pkt_info(dma_addr_t buff, u32 buff_len, u32 ndesc,
			 struct knav_dma_desc *desc)
{
	desc->buff_len = cpu_to_le32(buff_len);
	desc->buff = cpu_to_le32(buff);
	desc->next_desc = cpu_to_le32(ndesc);
}

static void set_desc_info(u32 desc_info, u32 pkt_info,
			  struct knav_dma_desc *desc)
{
	desc->desc_info = cpu_to_le32(desc_info);
	desc->packet_info = cpu_to_le32(pkt_info);
}

static void set_sw_data(int index, u32 data, struct knav_dma_desc *desc)
{
	/* No Endian conversion needed as this data is untouched by hw */
	desc->sw_data[index] = data;
}

/* use these macros to set sw data */
#define SET_SW_DATA0(data, desc) set_sw_data(0, data, desc)
#define SET_SW_DATA1(data, desc) set_sw_data(1, data, desc)
#define SET_SW_DATA2(data, desc) set_sw_data(2, data, desc)
#define SET_SW_DATA3(data, desc) set_sw_data(3, data, desc)

static void set_org_pkt_info(dma_addr_t buff, u32 buff_len,
			     struct knav_dma_desc *desc)
{
	desc->orig_buff = cpu_to_le32(buff);
	desc->orig_len = cpu_to_le32(buff_len);
}

static void set_words(u32 *words, int num_words, __le32 *desc)
{
	int i;

	for (i = 0; i < num_words; i++)
		desc[i] = cpu_to_le32(words[i]);
}

/* Read the e-fuse value as 32 bit values to be endian independent */
static int emac_arch_get_mac_addr(char *x, void __iomem *efuse_mac, u32 swap)
{
	unsigned int addr0, addr1;

	addr1 = readl(efuse_mac + 4);
	addr0 = readl(efuse_mac);

	switch (swap) {
	case NETCP_EFUSE_ADDR_SWAP:
		addr0 = addr1;
		addr1 = readl(efuse_mac);
		break;
	default:
		break;
	}

	x[0] = (addr1 & 0x0000ff00) >> 8;
	x[1] = addr1 & 0x000000ff;
	x[2] = (addr0 & 0xff000000) >> 24;
	x[3] = (addr0 & 0x00ff0000) >> 16;
	x[4] = (addr0 & 0x0000ff00) >> 8;
	x[5] = addr0 & 0x000000ff;

	return 0;
}

static const char *netcp_node_name(struct device_node *node)
{
	const char *name;

	if (of_property_read_string(node, "label", &name) < 0)
		name = node->name;
	if (!name)
		name = "unknown";
	return name;
}

/* Module management routines */
static int netcp_register_interface(struct netcp_intf *netcp)
{
	int ret;

	ret = register_netdev(netcp->ndev);
	if (!ret)
		netcp->netdev_registered = true;
	return ret;
}

static int netcp_module_probe(struct netcp_device *netcp_device,
			      struct netcp_module *module)
{
	struct device *dev = netcp_device->device;
	struct device_node *devices, *interface, *node = dev->of_node;
	struct device_node *child;
	struct netcp_inst_modpriv *inst_modpriv;
	struct netcp_intf *netcp_intf;
	struct netcp_module *tmp;
	bool primary_module_registered = false;
	int ret;

	/* Find this module in the sub-tree for this device */
	devices = of_get_child_by_name(node, "netcp-devices");
	if (!devices) {
		dev_err(dev, "could not find netcp-devices node\n");
		return NETCP_MOD_PROBE_SKIPPED;
	}

	for_each_available_child_of_node(devices, child) {
		const char *name = netcp_node_name(child);

		if (!strcasecmp(module->name, name))
			break;
	}

	of_node_put(devices);
	/* If module not used for this device, skip it */
	if (!child) {
		dev_warn(dev, "module(%s) not used for device\n", module->name);
		return NETCP_MOD_PROBE_SKIPPED;
	}

	inst_modpriv = devm_kzalloc(dev, sizeof(*inst_modpriv), GFP_KERNEL);
	if (!inst_modpriv) {
		of_node_put(child);
		return -ENOMEM;
	}

	inst_modpriv->netcp_device = netcp_device;
	inst_modpriv->netcp_module = module;
	list_add_tail(&inst_modpriv->inst_list, &netcp_device->modpriv_head);

	ret = module->probe(netcp_device, dev, child,
			    &inst_modpriv->module_priv);
	of_node_put(child);
	if (ret) {
		dev_err(dev, "Probe of module(%s) failed with %d\n",
			module->name, ret);
		list_del(&inst_modpriv->inst_list);
		devm_kfree(dev, inst_modpriv);
		return NETCP_MOD_PROBE_FAILED;
	}

	/* Attach modules only if the primary module is probed */
	for_each_netcp_module(tmp) {
		if (tmp->primary)
			primary_module_registered = true;
	}

	if (!primary_module_registered)
		return 0;

	/* Attach module to interfaces */
	list_for_each_entry(netcp_intf, &netcp_device->interface_head,
			    interface_list) {
		struct netcp_intf_modpriv *intf_modpriv;

		intf_modpriv = devm_kzalloc(dev, sizeof(*intf_modpriv),
					    GFP_KERNEL);
		if (!intf_modpriv)
			return -ENOMEM;

		interface = of_parse_phandle(netcp_intf->node_interface,
					     module->name, 0);

		if (!interface) {
			devm_kfree(dev, intf_modpriv);
			continue;
		}

		intf_modpriv->netcp_priv = netcp_intf;
		intf_modpriv->netcp_module = module;
		list_add_tail(&intf_modpriv->intf_list,
			      &netcp_intf->module_head);

		ret = module->attach(inst_modpriv->module_priv,
				     netcp_intf->ndev, interface,
				     &intf_modpriv->module_priv);
		of_node_put(interface);
		if (ret) {
			dev_dbg(dev, "Attach of module %s declined with %d\n",
				module->name, ret);
			list_del(&intf_modpriv->intf_list);
			devm_kfree(dev, intf_modpriv);
			continue;
		}
	}

	/* Now register the interface with netdev */
	list_for_each_entry(netcp_intf,
			    &netcp_device->interface_head,
			    interface_list) {
		/* If interface not registered then register now */
		if (!netcp_intf->netdev_registered) {
			ret = netcp_register_interface(netcp_intf);
			if (ret)
				return -ENODEV;
		}
	}
	return 0;
}

int netcp_register_module(struct netcp_module *module)
{
	struct netcp_device *netcp_device;
	struct netcp_module *tmp;
	int ret;

	if (!module->name) {
		WARN(1, "error registering netcp module: no name\n");
		return -EINVAL;
	}

	if (!module->probe) {
		WARN(1, "error registering netcp module: no probe\n");
		return -EINVAL;
	}

	mutex_lock(&netcp_modules_lock);

	for_each_netcp_module(tmp) {
		if (!strcasecmp(tmp->name, module->name)) {
			mutex_unlock(&netcp_modules_lock);
			return -EEXIST;
		}
	}
	list_add_tail(&module->module_list, &netcp_modules);

	list_for_each_entry(netcp_device, &netcp_devices, device_list) {
		ret = netcp_module_probe(netcp_device, module);
		if (ret < 0)
			goto fail;
	}
	mutex_unlock(&netcp_modules_lock);
	return 0;

fail:
	mutex_unlock(&netcp_modules_lock);
	netcp_unregister_module(module);
	return ret;
}
EXPORT_SYMBOL_GPL(netcp_register_module);

static void netcp_release_module(struct netcp_device *netcp_device,
				 struct netcp_module *module)
{
	struct netcp_inst_modpriv *inst_modpriv, *inst_tmp;
	struct netcp_intf *netcp_intf, *netcp_tmp;
	struct device *dev = netcp_device->device;

	/* Release the module from each interface */
	list_for_each_entry_safe(netcp_intf, netcp_tmp,
				 &netcp_device->interface_head,
				 interface_list) {
		struct netcp_intf_modpriv *intf_modpriv, *intf_tmp;

		list_for_each_entry_safe(intf_modpriv, intf_tmp,
					 &netcp_intf->module_head,
					 intf_list) {
			if (intf_modpriv->netcp_module == module) {
				module->release(intf_modpriv->module_priv);
				list_del(&intf_modpriv->intf_list);
				devm_kfree(dev, intf_modpriv);
				break;
			}
		}
	}

	/* Remove the module from each instance */
	list_for_each_entry_safe(inst_modpriv, inst_tmp,
				 &netcp_device->modpriv_head, inst_list) {
		if (inst_modpriv->netcp_module == module) {
			module->remove(netcp_device,
				       inst_modpriv->module_priv);
			list_del(&inst_modpriv->inst_list);
			devm_kfree(dev, inst_modpriv);
			break;
		}
	}
}

void netcp_unregister_module(struct netcp_module *module)
{
	struct netcp_device *netcp_device;
	struct netcp_module *module_tmp;

	mutex_lock(&netcp_modules_lock);

	list_for_each_entry(netcp_device, &netcp_devices, device_list) {
		netcp_release_module(netcp_device, module);
	}

	/* Remove the module from the module list */
	for_each_netcp_module(module_tmp) {
		if (module == module_tmp) {
			list_del(&module->module_list);
			break;
		}
	}

	mutex_unlock(&netcp_modules_lock);
}
EXPORT_SYMBOL_GPL(netcp_unregister_module);

void *netcp_module_get_intf_data(struct netcp_module *module,
				 struct netcp_intf *intf)
{
	struct netcp_intf_modpriv *intf_modpriv;

	list_for_each_entry(intf_modpriv, &intf->module_head, intf_list)
		if (intf_modpriv->netcp_module == module)
			return intf_modpriv->module_priv;
	return NULL;
}
EXPORT_SYMBOL_GPL(netcp_module_get_intf_data);

/* Module TX and RX Hook management */
struct netcp_hook_list {
	struct list_head	 list;
	netcp_hook_rtn		*hook_rtn;
	void			*hook_data;
	int			 order;
};

int netcp_register_txhook(struct netcp_intf *netcp_priv, int order,
			  netcp_hook_rtn *hook_rtn, void *hook_data)
{
	struct netcp_hook_list *entry;
	struct netcp_hook_list *next;
	unsigned long flags;

	entry = devm_kzalloc(netcp_priv->dev, sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->hook_rtn  = hook_rtn;
	entry->hook_data = hook_data;
	entry->order     = order;

	spin_lock_irqsave(&netcp_priv->lock, flags);
	list_for_each_entry(next, &netcp_priv->txhook_list_head, list) {
		if (next->order > order)
			break;
	}
	__list_add(&entry->list, next->list.prev, &next->list);
	spin_unlock_irqrestore(&netcp_priv->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(netcp_register_txhook);

int netcp_unregister_txhook(struct netcp_intf *netcp_priv, int order,
			    netcp_hook_rtn *hook_rtn, void *hook_data)
{
	struct netcp_hook_list *next, *n;
	unsigned long flags;

	spin_lock_irqsave(&netcp_priv->lock, flags);
	list_for_each_entry_safe(next, n, &netcp_priv->txhook_list_head, list) {
		if ((next->order     == order) &&
		    (next->hook_rtn  == hook_rtn) &&
		    (next->hook_data == hook_data)) {
			list_del(&next->list);
			spin_unlock_irqrestore(&netcp_priv->lock, flags);
			devm_kfree(netcp_priv->dev, next);
			return 0;
		}
	}
	spin_unlock_irqrestore(&netcp_priv->lock, flags);
	return -ENOENT;
}
EXPORT_SYMBOL_GPL(netcp_unregister_txhook);

int netcp_register_rxhook(struct netcp_intf *netcp_priv, int order,
			  netcp_hook_rtn *hook_rtn, void *hook_data)
{
	struct netcp_hook_list *entry;
	struct netcp_hook_list *next;
	unsigned long flags;

	entry = devm_kzalloc(netcp_priv->dev, sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->hook_rtn  = hook_rtn;
	entry->hook_data = hook_data;
	entry->order     = order;

	spin_lock_irqsave(&netcp_priv->lock, flags);
	list_for_each_entry(next, &netcp_priv->rxhook_list_head, list) {
		if (next->order > order)
			break;
	}
	__list_add(&entry->list, next->list.prev, &next->list);
	spin_unlock_irqrestore(&netcp_priv->lock, flags);

	return 0;
}

int netcp_unregister_rxhook(struct netcp_intf *netcp_priv, int order,
			    netcp_hook_rtn *hook_rtn, void *hook_data)
{
	struct netcp_hook_list *next, *n;
	unsigned long flags;

	spin_lock_irqsave(&netcp_priv->lock, flags);
	list_for_each_entry_safe(next, n, &netcp_priv->rxhook_list_head, list) {
		if ((next->order     == order) &&
		    (next->hook_rtn  == hook_rtn) &&
		    (next->hook_data == hook_data)) {
			list_del(&next->list);
			spin_unlock_irqrestore(&netcp_priv->lock, flags);
			devm_kfree(netcp_priv->dev, next);
			return 0;
		}
	}
	spin_unlock_irqrestore(&netcp_priv->lock, flags);

	return -ENOENT;
}

static void netcp_frag_free(bool is_frag, void *ptr)
{
	if (is_frag)
		skb_free_frag(ptr);
	else
		kfree(ptr);
}

static void netcp_free_rx_desc_chain(struct netcp_intf *netcp,
				     struct knav_dma_desc *desc)
{
	struct knav_dma_desc *ndesc;
	dma_addr_t dma_desc, dma_buf;
	unsigned int buf_len, dma_sz = sizeof(*ndesc);
	void *buf_ptr;
	u32 tmp;

	get_words(&dma_desc, 1, &desc->next_desc);

	while (dma_desc) {
		ndesc = knav_pool_desc_unmap(netcp->rx_pool, dma_desc, dma_sz);
		if (unlikely(!ndesc)) {
			dev_err(netcp->ndev_dev, "failed to unmap Rx desc\n");
			break;
		}
		get_pkt_info(&dma_buf, &tmp, &dma_desc, ndesc);
		/* warning!!!! We are retrieving the virtual ptr in the sw_data
		 * field as a 32bit value. Will not work on 64bit machines
		 */
		buf_ptr = (void *)GET_SW_DATA0(ndesc);
		buf_len = (int)GET_SW_DATA1(desc);
		dma_unmap_page(netcp->dev, dma_buf, PAGE_SIZE, DMA_FROM_DEVICE);
		__free_page(buf_ptr);
		knav_pool_desc_put(netcp->rx_pool, desc);
	}
	/* warning!!!! We are retrieving the virtual ptr in the sw_data
	 * field as a 32bit value. Will not work on 64bit machines
	 */
	buf_ptr = (void *)GET_SW_DATA0(desc);
	buf_len = (int)GET_SW_DATA1(desc);

	if (buf_ptr)
		netcp_frag_free(buf_len <= PAGE_SIZE, buf_ptr);
	knav_pool_desc_put(netcp->rx_pool, desc);
}

static void netcp_empty_rx_queue(struct netcp_intf *netcp)
{
	struct knav_dma_desc *desc;
	unsigned int dma_sz;
	dma_addr_t dma;

	for (; ;) {
		dma = knav_queue_pop(netcp->rx_queue, &dma_sz);
		if (!dma)
			break;

		desc = knav_pool_desc_unmap(netcp->rx_pool, dma, dma_sz);
		if (unlikely(!desc)) {
			dev_err(netcp->ndev_dev, "%s: failed to unmap Rx desc\n",
				__func__);
			netcp->ndev->stats.rx_errors++;
			continue;
		}
		netcp_free_rx_desc_chain(netcp, desc);
		netcp->ndev->stats.rx_dropped++;
	}
}

static int netcp_process_one_rx_packet(struct netcp_intf *netcp)
{
	unsigned int dma_sz, buf_len, org_buf_len;
	struct knav_dma_desc *desc, *ndesc;
	unsigned int pkt_sz = 0, accum_sz;
	struct netcp_hook_list *rx_hook;
	dma_addr_t dma_desc, dma_buff;
	struct netcp_packet p_info;
	struct sk_buff *skb;
	void *org_buf_ptr;

	dma_desc = knav_queue_pop(netcp->rx_queue, &dma_sz);
	if (!dma_desc)
		return -1;

	desc = knav_pool_desc_unmap(netcp->rx_pool, dma_desc, dma_sz);
	if (unlikely(!desc)) {
		dev_err(netcp->ndev_dev, "failed to unmap Rx desc\n");
		return 0;
	}

	get_pkt_info(&dma_buff, &buf_len, &dma_desc, desc);
	/* warning!!!! We are retrieving the virtual ptr in the sw_data
	 * field as a 32bit value. Will not work on 64bit machines
	 */
	org_buf_ptr = (void *)GET_SW_DATA0(desc);
	org_buf_len = (int)GET_SW_DATA1(desc);

	if (unlikely(!org_buf_ptr)) {
		dev_err(netcp->ndev_dev, "NULL bufptr in desc\n");
		goto free_desc;
	}

	pkt_sz &= KNAV_DMA_DESC_PKT_LEN_MASK;
	accum_sz = buf_len;
	dma_unmap_single(netcp->dev, dma_buff, buf_len, DMA_FROM_DEVICE);

	/* Build a new sk_buff for the primary buffer */
	skb = build_skb(org_buf_ptr, org_buf_len);
	if (unlikely(!skb)) {
		dev_err(netcp->ndev_dev, "build_skb() failed\n");
		goto free_desc;
	}

	/* update data, tail and len */
	skb_reserve(skb, NETCP_SOP_OFFSET);
	__skb_put(skb, buf_len);

	/* Fill in the page fragment list */
	while (dma_desc) {
		struct page *page;

		ndesc = knav_pool_desc_unmap(netcp->rx_pool, dma_desc, dma_sz);
		if (unlikely(!ndesc)) {
			dev_err(netcp->ndev_dev, "failed to unmap Rx desc\n");
			goto free_desc;
		}

		get_pkt_info(&dma_buff, &buf_len, &dma_desc, ndesc);
		/* warning!!!! We are retrieving the virtual ptr in the sw_data
		 * field as a 32bit value. Will not work on 64bit machines
		 */
		page = (struct page *)GET_SW_DATA0(desc);

		if (likely(dma_buff && buf_len && page)) {
			dma_unmap_page(netcp->dev, dma_buff, PAGE_SIZE,
				       DMA_FROM_DEVICE);
		} else {
			dev_err(netcp->ndev_dev, "Bad Rx desc dma_buff(%pad), len(%d), page(%p)\n",
				&dma_buff, buf_len, page);
			goto free_desc;
		}

		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page,
				offset_in_page(dma_buff), buf_len, PAGE_SIZE);
		accum_sz += buf_len;

		/* Free the descriptor */
		knav_pool_desc_put(netcp->rx_pool, ndesc);
	}

	/* Free the primary descriptor */
	knav_pool_desc_put(netcp->rx_pool, desc);

	/* check for packet len and warn */
	if (unlikely(pkt_sz != accum_sz))
		dev_dbg(netcp->ndev_dev, "mismatch in packet size(%d) & sum of fragments(%d)\n",
			pkt_sz, accum_sz);

	/* Remove ethernet FCS from the packet */
	__pskb_trim(skb, skb->len - ETH_FCS_LEN);

	/* Call each of the RX hooks */
	p_info.skb = skb;
	p_info.rxtstamp_complete = false;
	list_for_each_entry(rx_hook, &netcp->rxhook_list_head, list) {
		int ret;

		ret = rx_hook->hook_rtn(rx_hook->order, rx_hook->hook_data,
					&p_info);
		if (unlikely(ret)) {
			dev_err(netcp->ndev_dev, "RX hook %d failed: %d\n",
				rx_hook->order, ret);
			netcp->ndev->stats.rx_errors++;
			dev_kfree_skb(skb);
			return 0;
		}
	}

	netcp->ndev->stats.rx_packets++;
	netcp->ndev->stats.rx_bytes += skb->len;

	/* push skb up the stack */
	skb->protocol = eth_type_trans(skb, netcp->ndev);
	netif_receive_skb(skb);
	return 0;

free_desc:
	netcp_free_rx_desc_chain(netcp, desc);
	netcp->ndev->stats.rx_errors++;
	return 0;
}

static int netcp_process_rx_packets(struct netcp_intf *netcp,
				    unsigned int budget)
{
	int i;

	for (i = 0; (i < budget) && !netcp_process_one_rx_packet(netcp); i++)
		;
	return i;
}

/* Release descriptors and attached buffers from Rx FDQ */
static void netcp_free_rx_buf(struct netcp_intf *netcp, int fdq)
{
	struct knav_dma_desc *desc;
	unsigned int buf_len, dma_sz;
	dma_addr_t dma;
	void *buf_ptr;

	/* Allocate descriptor */
	while ((dma = knav_queue_pop(netcp->rx_fdq[fdq], &dma_sz))) {
		desc = knav_pool_desc_unmap(netcp->rx_pool, dma, dma_sz);
		if (unlikely(!desc)) {
			dev_err(netcp->ndev_dev, "failed to unmap Rx desc\n");
			continue;
		}

		get_org_pkt_info(&dma, &buf_len, desc);
		/* warning!!!! We are retrieving the virtual ptr in the sw_data
		 * field as a 32bit value. Will not work on 64bit machines
		 */
		buf_ptr = (void *)GET_SW_DATA0(desc);

		if (unlikely(!dma)) {
			dev_err(netcp->ndev_dev, "NULL orig_buff in desc\n");
			knav_pool_desc_put(netcp->rx_pool, desc);
			continue;
		}

		if (unlikely(!buf_ptr)) {
			dev_err(netcp->ndev_dev, "NULL bufptr in desc\n");
			knav_pool_desc_put(netcp->rx_pool, desc);
			continue;
		}

		if (fdq == 0) {
			dma_unmap_single(netcp->dev, dma, buf_len,
					 DMA_FROM_DEVICE);
			netcp_frag_free((buf_len <= PAGE_SIZE), buf_ptr);
		} else {
			dma_unmap_page(netcp->dev, dma, buf_len,
				       DMA_FROM_DEVICE);
			__free_page(buf_ptr);
		}

		knav_pool_desc_put(netcp->rx_pool, desc);
	}
}

static void netcp_rxpool_free(struct netcp_intf *netcp)
{
	int i;

	for (i = 0; i < KNAV_DMA_FDQ_PER_CHAN &&
	     !IS_ERR_OR_NULL(netcp->rx_fdq[i]); i++)
		netcp_free_rx_buf(netcp, i);

	if (knav_pool_count(netcp->rx_pool) != netcp->rx_pool_size)
		dev_err(netcp->ndev_dev, "Lost Rx (%d) descriptors\n",
			netcp->rx_pool_size - knav_pool_count(netcp->rx_pool));

	knav_pool_destroy(netcp->rx_pool);
	netcp->rx_pool = NULL;
}

static int netcp_allocate_rx_buf(struct netcp_intf *netcp, int fdq)
{
	struct knav_dma_desc *hwdesc;
	unsigned int buf_len, dma_sz;
	u32 desc_info, pkt_info;
	struct page *page;
	dma_addr_t dma;
	void *bufptr;
	u32 sw_data[2];

	/* Allocate descriptor */
	hwdesc = knav_pool_desc_get(netcp->rx_pool);
	if (IS_ERR_OR_NULL(hwdesc)) {
		dev_dbg(netcp->ndev_dev, "out of rx pool desc\n");
		return -ENOMEM;
	}

	if (likely(fdq == 0)) {
		unsigned int primary_buf_len;
		/* Allocate a primary receive queue entry */
		buf_len = NETCP_PACKET_SIZE + NETCP_SOP_OFFSET;
		primary_buf_len = SKB_DATA_ALIGN(buf_len) +
				SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

		bufptr = netdev_alloc_frag(primary_buf_len);
		sw_data[1] = primary_buf_len;

		if (unlikely(!bufptr)) {
			dev_warn_ratelimited(netcp->ndev_dev,
					     "Primary RX buffer alloc failed\n");
			goto fail;
		}
		dma = dma_map_single(netcp->dev, bufptr, buf_len,
				     DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(netcp->dev, dma)))
			goto fail;

		/* warning!!!! We are saving the virtual ptr in the sw_data
		 * field as a 32bit value. Will not work on 64bit machines
		 */
		sw_data[0] = (u32)bufptr;
	} else {
		/* Allocate a secondary receive queue entry */
		page = alloc_page(GFP_ATOMIC | GFP_DMA | __GFP_COLD);
		if (unlikely(!page)) {
			dev_warn_ratelimited(netcp->ndev_dev, "Secondary page alloc failed\n");
			goto fail;
		}
		buf_len = PAGE_SIZE;
		dma = dma_map_page(netcp->dev, page, 0, buf_len, DMA_TO_DEVICE);
		/* warning!!!! We are saving the virtual ptr in the sw_data
		 * field as a 32bit value. Will not work on 64bit machines
		 */
		sw_data[0] = (u32)page;
		sw_data[1] = 0;
	}

	desc_info =  KNAV_DMA_DESC_PS_INFO_IN_DESC;
	desc_info |= buf_len & KNAV_DMA_DESC_PKT_LEN_MASK;
	pkt_info =  KNAV_DMA_DESC_HAS_EPIB;
	pkt_info |= KNAV_DMA_NUM_PS_WORDS << KNAV_DMA_DESC_PSLEN_SHIFT;
	pkt_info |= (netcp->rx_queue_id & KNAV_DMA_DESC_RETQ_MASK) <<
		    KNAV_DMA_DESC_RETQ_SHIFT;
	set_org_pkt_info(dma, buf_len, hwdesc);
	SET_SW_DATA0(sw_data[0], hwdesc);
	SET_SW_DATA1(sw_data[1], hwdesc);
	set_desc_info(desc_info, pkt_info, hwdesc);

	/* Push to FDQs */
	knav_pool_desc_map(netcp->rx_pool, hwdesc, sizeof(*hwdesc), &dma,
			   &dma_sz);
	knav_queue_push(netcp->rx_fdq[fdq], dma, sizeof(*hwdesc), 0);
	return 0;

fail:
	knav_pool_desc_put(netcp->rx_pool, hwdesc);
	return -ENOMEM;
}

/* Refill Rx FDQ with descriptors & attached buffers */
static void netcp_rxpool_refill(struct netcp_intf *netcp)
{
	u32 fdq_deficit[KNAV_DMA_FDQ_PER_CHAN] = {0};
	int i, ret = 0;

	/* Calculate the FDQ deficit and refill */
	for (i = 0; i < KNAV_DMA_FDQ_PER_CHAN && netcp->rx_fdq[i]; i++) {
		fdq_deficit[i] = netcp->rx_queue_depths[i] -
				 knav_queue_get_count(netcp->rx_fdq[i]);

		while (fdq_deficit[i]-- && !ret)
			ret = netcp_allocate_rx_buf(netcp, i);
	} /* end for fdqs */
}

/* NAPI poll */
static int netcp_rx_poll(struct napi_struct *napi, int budget)
{
	struct netcp_intf *netcp = container_of(napi, struct netcp_intf,
						rx_napi);
	unsigned int packets;

	packets = netcp_process_rx_packets(netcp, budget);

	netcp_rxpool_refill(netcp);
	if (packets < budget) {
		napi_complete(&netcp->rx_napi);
		knav_queue_enable_notify(netcp->rx_queue);
	}

	return packets;
}

static void netcp_rx_notify(void *arg)
{
	struct netcp_intf *netcp = arg;

	knav_queue_disable_notify(netcp->rx_queue);
	napi_schedule(&netcp->rx_napi);
}

static void netcp_free_tx_desc_chain(struct netcp_intf *netcp,
				     struct knav_dma_desc *desc,
				     unsigned int desc_sz)
{
	struct knav_dma_desc *ndesc = desc;
	dma_addr_t dma_desc, dma_buf;
	unsigned int buf_len;

	while (ndesc) {
		get_pkt_info(&dma_buf, &buf_len, &dma_desc, ndesc);

		if (dma_buf && buf_len)
			dma_unmap_single(netcp->dev, dma_buf, buf_len,
					 DMA_TO_DEVICE);
		else
			dev_warn(netcp->ndev_dev, "bad Tx desc buf(%pad), len(%d)\n",
				 &dma_buf, buf_len);

		knav_pool_desc_put(netcp->tx_pool, ndesc);
		ndesc = NULL;
		if (dma_desc) {
			ndesc = knav_pool_desc_unmap(netcp->tx_pool, dma_desc,
						     desc_sz);
			if (!ndesc)
				dev_err(netcp->ndev_dev, "failed to unmap Tx desc\n");
		}
	}
}

static int netcp_process_tx_compl_packets(struct netcp_intf *netcp,
					  unsigned int budget)
{
	struct knav_dma_desc *desc;
	struct sk_buff *skb;
	unsigned int dma_sz;
	dma_addr_t dma;
	int pkts = 0;

	while (budget--) {
		dma = knav_queue_pop(netcp->tx_compl_q, &dma_sz);
		if (!dma)
			break;
		desc = knav_pool_desc_unmap(netcp->tx_pool, dma, dma_sz);
		if (unlikely(!desc)) {
			dev_err(netcp->ndev_dev, "failed to unmap Tx desc\n");
			netcp->ndev->stats.tx_errors++;
			continue;
		}

		/* warning!!!! We are retrieving the virtual ptr in the sw_data
		 * field as a 32bit value. Will not work on 64bit machines
		 */
		skb = (struct sk_buff *)GET_SW_DATA0(desc);
		netcp_free_tx_desc_chain(netcp, desc, dma_sz);
		if (!skb) {
			dev_err(netcp->ndev_dev, "No skb in Tx desc\n");
			netcp->ndev->stats.tx_errors++;
			continue;
		}

		if (netif_subqueue_stopped(netcp->ndev, skb) &&
		    netif_running(netcp->ndev) &&
		    (knav_pool_count(netcp->tx_pool) >
		    netcp->tx_resume_threshold)) {
			u16 subqueue = skb_get_queue_mapping(skb);

			netif_wake_subqueue(netcp->ndev, subqueue);
		}

		netcp->ndev->stats.tx_packets++;
		netcp->ndev->stats.tx_bytes += skb->len;
		dev_kfree_skb(skb);
		pkts++;
	}
	return pkts;
}

static int netcp_tx_poll(struct napi_struct *napi, int budget)
{
	int packets;
	struct netcp_intf *netcp = container_of(napi, struct netcp_intf,
						tx_napi);

	packets = netcp_process_tx_compl_packets(netcp, budget);
	if (packets < budget) {
		napi_complete(&netcp->tx_napi);
		knav_queue_enable_notify(netcp->tx_compl_q);
	}

	return packets;
}

static void netcp_tx_notify(void *arg)
{
	struct netcp_intf *netcp = arg;

	knav_queue_disable_notify(netcp->tx_compl_q);
	napi_schedule(&netcp->tx_napi);
}

static struct knav_dma_desc*
netcp_tx_map_skb(struct sk_buff *skb, struct netcp_intf *netcp)
{
	struct knav_dma_desc *desc, *ndesc, *pdesc;
	unsigned int pkt_len = skb_headlen(skb);
	struct device *dev = netcp->dev;
	dma_addr_t dma_addr;
	unsigned int dma_sz;
	int i;

	/* Map the linear buffer */
	dma_addr = dma_map_single(dev, skb->data, pkt_len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, dma_addr))) {
		dev_err(netcp->ndev_dev, "Failed to map skb buffer\n");
		return NULL;
	}

	desc = knav_pool_desc_get(netcp->tx_pool);
	if (IS_ERR_OR_NULL(desc)) {
		dev_err(netcp->ndev_dev, "out of TX desc\n");
		dma_unmap_single(dev, dma_addr, pkt_len, DMA_TO_DEVICE);
		return NULL;
	}

	set_pkt_info(dma_addr, pkt_len, 0, desc);
	if (skb_is_nonlinear(skb)) {
		prefetchw(skb_shinfo(skb));
	} else {
		desc->next_desc = 0;
		goto upd_pkt_len;
	}

	pdesc = desc;

	/* Handle the case where skb is fragmented in pages */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		struct page *page = skb_frag_page(frag);
		u32 page_offset = frag->page_offset;
		u32 buf_len = skb_frag_size(frag);
		dma_addr_t desc_dma;
		u32 desc_dma_32;
		u32 pkt_info;

		dma_addr = dma_map_page(dev, page, page_offset, buf_len,
					DMA_TO_DEVICE);
		if (unlikely(!dma_addr)) {
			dev_err(netcp->ndev_dev, "Failed to map skb page\n");
			goto free_descs;
		}

		ndesc = knav_pool_desc_get(netcp->tx_pool);
		if (IS_ERR_OR_NULL(ndesc)) {
			dev_err(netcp->ndev_dev, "out of TX desc for frags\n");
			dma_unmap_page(dev, dma_addr, buf_len, DMA_TO_DEVICE);
			goto free_descs;
		}

		desc_dma = knav_pool_desc_virt_to_dma(netcp->tx_pool, ndesc);
		pkt_info =
			(netcp->tx_compl_qid & KNAV_DMA_DESC_RETQ_MASK) <<
				KNAV_DMA_DESC_RETQ_SHIFT;
		set_pkt_info(dma_addr, buf_len, 0, ndesc);
		desc_dma_32 = (u32)desc_dma;
		set_words(&desc_dma_32, 1, &pdesc->next_desc);
		pkt_len += buf_len;
		if (pdesc != desc)
			knav_pool_desc_map(netcp->tx_pool, pdesc,
					   sizeof(*pdesc), &desc_dma, &dma_sz);
		pdesc = ndesc;
	}
	if (pdesc != desc)
		knav_pool_desc_map(netcp->tx_pool, pdesc, sizeof(*pdesc),
				   &dma_addr, &dma_sz);

	/* frag list based linkage is not supported for now. */
	if (skb_shinfo(skb)->frag_list) {
		dev_err_ratelimited(netcp->ndev_dev, "NETIF_F_FRAGLIST not supported\n");
		goto free_descs;
	}

upd_pkt_len:
	WARN_ON(pkt_len != skb->len);

	pkt_len &= KNAV_DMA_DESC_PKT_LEN_MASK;
	set_words(&pkt_len, 1, &desc->desc_info);
	return desc;

free_descs:
	netcp_free_tx_desc_chain(netcp, desc, sizeof(*desc));
	return NULL;
}

static int netcp_tx_submit_skb(struct netcp_intf *netcp,
			       struct sk_buff *skb,
			       struct knav_dma_desc *desc)
{
	struct netcp_tx_pipe *tx_pipe = NULL;
	struct netcp_hook_list *tx_hook;
	struct netcp_packet p_info;
	unsigned int dma_sz;
	dma_addr_t dma;
	u32 tmp = 0;
	int ret = 0;

	p_info.netcp = netcp;
	p_info.skb = skb;
	p_info.tx_pipe = NULL;
	p_info.psdata_len = 0;
	p_info.ts_context = NULL;
	p_info.txtstamp_complete = NULL;
	p_info.epib = desc->epib;
	p_info.psdata = (u32 __force *)desc->psdata;
	memset(p_info.epib, 0, KNAV_DMA_NUM_EPIB_WORDS * sizeof(__le32));

	/* Find out where to inject the packet for transmission */
	list_for_each_entry(tx_hook, &netcp->txhook_list_head, list) {
		ret = tx_hook->hook_rtn(tx_hook->order, tx_hook->hook_data,
					&p_info);
		if (unlikely(ret != 0)) {
			dev_err(netcp->ndev_dev, "TX hook %d rejected the packet with reason(%d)\n",
				tx_hook->order, ret);
			ret = (ret < 0) ? ret : NETDEV_TX_OK;
			goto out;
		}
	}

	/* Make sure some TX hook claimed the packet */
	tx_pipe = p_info.tx_pipe;
	if (!tx_pipe) {
		dev_err(netcp->ndev_dev, "No TX hook claimed the packet!\n");
		ret = -ENXIO;
		goto out;
	}

	/* update descriptor */
	if (p_info.psdata_len) {
		/* psdata points to both native-endian and device-endian data */
		__le32 *psdata = (void __force *)p_info.psdata;

		memmove(p_info.psdata, p_info.psdata + p_info.psdata_len,
			p_info.psdata_len);
		set_words(p_info.psdata, p_info.psdata_len, psdata);
		tmp |= (p_info.psdata_len & KNAV_DMA_DESC_PSLEN_MASK) <<
			KNAV_DMA_DESC_PSLEN_SHIFT;
	}

	tmp |= KNAV_DMA_DESC_HAS_EPIB |
		((netcp->tx_compl_qid & KNAV_DMA_DESC_RETQ_MASK) <<
		KNAV_DMA_DESC_RETQ_SHIFT);

	if (!(tx_pipe->flags & SWITCH_TO_PORT_IN_TAGINFO)) {
		tmp |= ((tx_pipe->switch_to_port & KNAV_DMA_DESC_PSFLAG_MASK) <<
			KNAV_DMA_DESC_PSFLAG_SHIFT);
	}

	set_words(&tmp, 1, &desc->packet_info);
	/* warning!!!! We are saving the virtual ptr in the sw_data
	 * field as a 32bit value. Will not work on 64bit machines
	 */
	SET_SW_DATA0((u32)skb, desc);

	if (tx_pipe->flags & SWITCH_TO_PORT_IN_TAGINFO) {
		tmp = tx_pipe->switch_to_port;
		set_words(&tmp, 1, &desc->tag_info);
	}

	/* submit packet descriptor */
	ret = knav_pool_desc_map(netcp->tx_pool, desc, sizeof(*desc), &dma,
				 &dma_sz);
	if (unlikely(ret)) {
		dev_err(netcp->ndev_dev, "%s() failed to map desc\n", __func__);
		ret = -ENOMEM;
		goto out;
	}
	skb_tx_timestamp(skb);
	knav_queue_push(tx_pipe->dma_queue, dma, dma_sz, 0);

out:
	return ret;
}

/* Submit the packet */
static int netcp_ndo_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	int subqueue = skb_get_queue_mapping(skb);
	struct knav_dma_desc *desc;
	int desc_count, ret = 0;

	if (unlikely(skb->len <= 0)) {
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	if (unlikely(skb->len < NETCP_MIN_PACKET_SIZE)) {
		ret = skb_padto(skb, NETCP_MIN_PACKET_SIZE);
		if (ret < 0) {
			/* If we get here, the skb has already been dropped */
			dev_warn(netcp->ndev_dev, "padding failed (%d), packet dropped\n",
				 ret);
			ndev->stats.tx_dropped++;
			return ret;
		}
		skb->len = NETCP_MIN_PACKET_SIZE;
	}

	desc = netcp_tx_map_skb(skb, netcp);
	if (unlikely(!desc)) {
		netif_stop_subqueue(ndev, subqueue);
		ret = -ENOBUFS;
		goto drop;
	}

	ret = netcp_tx_submit_skb(netcp, skb, desc);
	if (ret)
		goto drop;

	netif_trans_update(ndev);

	/* Check Tx pool count & stop subqueue if needed */
	desc_count = knav_pool_count(netcp->tx_pool);
	if (desc_count < netcp->tx_pause_threshold) {
		dev_dbg(netcp->ndev_dev, "pausing tx, count(%d)\n", desc_count);
		netif_stop_subqueue(ndev, subqueue);
	}
	return NETDEV_TX_OK;

drop:
	ndev->stats.tx_dropped++;
	if (desc)
		netcp_free_tx_desc_chain(netcp, desc, sizeof(*desc));
	dev_kfree_skb(skb);
	return ret;
}

int netcp_txpipe_close(struct netcp_tx_pipe *tx_pipe)
{
	if (tx_pipe->dma_channel) {
		knav_dma_close_channel(tx_pipe->dma_channel);
		tx_pipe->dma_channel = NULL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(netcp_txpipe_close);

int netcp_txpipe_open(struct netcp_tx_pipe *tx_pipe)
{
	struct device *dev = tx_pipe->netcp_device->device;
	struct knav_dma_cfg config;
	int ret = 0;
	u8 name[16];

	memset(&config, 0, sizeof(config));
	config.direction = DMA_MEM_TO_DEV;
	config.u.tx.filt_einfo = false;
	config.u.tx.filt_pswords = false;
	config.u.tx.priority = DMA_PRIO_MED_L;

	tx_pipe->dma_channel = knav_dma_open_channel(dev,
				tx_pipe->dma_chan_name, &config);
	if (IS_ERR_OR_NULL(tx_pipe->dma_channel)) {
		dev_err(dev, "failed opening tx chan(%s)\n",
			tx_pipe->dma_chan_name);
		goto err;
	}

	snprintf(name, sizeof(name), "tx-pipe-%s", dev_name(dev));
	tx_pipe->dma_queue = knav_queue_open(name, tx_pipe->dma_queue_id,
					     KNAV_QUEUE_SHARED);
	if (IS_ERR(tx_pipe->dma_queue)) {
		dev_err(dev, "Could not open DMA queue for channel \"%s\": %d\n",
			name, ret);
		ret = PTR_ERR(tx_pipe->dma_queue);
		goto err;
	}

	dev_dbg(dev, "opened tx pipe %s\n", name);
	return 0;

err:
	if (!IS_ERR_OR_NULL(tx_pipe->dma_channel))
		knav_dma_close_channel(tx_pipe->dma_channel);
	tx_pipe->dma_channel = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(netcp_txpipe_open);

int netcp_txpipe_init(struct netcp_tx_pipe *tx_pipe,
		      struct netcp_device *netcp_device,
		      const char *dma_chan_name, unsigned int dma_queue_id)
{
	memset(tx_pipe, 0, sizeof(*tx_pipe));
	tx_pipe->netcp_device = netcp_device;
	tx_pipe->dma_chan_name = dma_chan_name;
	tx_pipe->dma_queue_id = dma_queue_id;
	return 0;
}
EXPORT_SYMBOL_GPL(netcp_txpipe_init);

static struct netcp_addr *netcp_addr_find(struct netcp_intf *netcp,
					  const u8 *addr,
					  enum netcp_addr_type type)
{
	struct netcp_addr *naddr;

	list_for_each_entry(naddr, &netcp->addr_list, node) {
		if (naddr->type != type)
			continue;
		if (addr && memcmp(addr, naddr->addr, ETH_ALEN))
			continue;
		return naddr;
	}

	return NULL;
}

static struct netcp_addr *netcp_addr_add(struct netcp_intf *netcp,
					 const u8 *addr,
					 enum netcp_addr_type type)
{
	struct netcp_addr *naddr;

	naddr = devm_kmalloc(netcp->dev, sizeof(*naddr), GFP_ATOMIC);
	if (!naddr)
		return NULL;

	naddr->type = type;
	naddr->flags = 0;
	naddr->netcp = netcp;
	if (addr)
		ether_addr_copy(naddr->addr, addr);
	else
		eth_zero_addr(naddr->addr);
	list_add_tail(&naddr->node, &netcp->addr_list);

	return naddr;
}

static void netcp_addr_del(struct netcp_intf *netcp, struct netcp_addr *naddr)
{
	list_del(&naddr->node);
	devm_kfree(netcp->dev, naddr);
}

static void netcp_addr_clear_mark(struct netcp_intf *netcp)
{
	struct netcp_addr *naddr;

	list_for_each_entry(naddr, &netcp->addr_list, node)
		naddr->flags = 0;
}

static void netcp_addr_add_mark(struct netcp_intf *netcp, const u8 *addr,
				enum netcp_addr_type type)
{
	struct netcp_addr *naddr;

	naddr = netcp_addr_find(netcp, addr, type);
	if (naddr) {
		naddr->flags |= ADDR_VALID;
		return;
	}

	naddr = netcp_addr_add(netcp, addr, type);
	if (!WARN_ON(!naddr))
		naddr->flags |= ADDR_NEW;
}

static void netcp_addr_sweep_del(struct netcp_intf *netcp)
{
	struct netcp_addr *naddr, *tmp;
	struct netcp_intf_modpriv *priv;
	struct netcp_module *module;
	int error;

	list_for_each_entry_safe(naddr, tmp, &netcp->addr_list, node) {
		if (naddr->flags & (ADDR_VALID | ADDR_NEW))
			continue;
		dev_dbg(netcp->ndev_dev, "deleting address %pM, type %x\n",
			naddr->addr, naddr->type);
		for_each_module(netcp, priv) {
			module = priv->netcp_module;
			if (!module->del_addr)
				continue;
			error = module->del_addr(priv->module_priv,
						 naddr);
			WARN_ON(error);
		}
		netcp_addr_del(netcp, naddr);
	}
}

static void netcp_addr_sweep_add(struct netcp_intf *netcp)
{
	struct netcp_addr *naddr, *tmp;
	struct netcp_intf_modpriv *priv;
	struct netcp_module *module;
	int error;

	list_for_each_entry_safe(naddr, tmp, &netcp->addr_list, node) {
		if (!(naddr->flags & ADDR_NEW))
			continue;
		dev_dbg(netcp->ndev_dev, "adding address %pM, type %x\n",
			naddr->addr, naddr->type);

		for_each_module(netcp, priv) {
			module = priv->netcp_module;
			if (!module->add_addr)
				continue;
			error = module->add_addr(priv->module_priv, naddr);
			WARN_ON(error);
		}
	}
}

static void netcp_set_rx_mode(struct net_device *ndev)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct netdev_hw_addr *ndev_addr;
	bool promisc;

	promisc = (ndev->flags & IFF_PROMISC ||
		   ndev->flags & IFF_ALLMULTI ||
		   netdev_mc_count(ndev) > NETCP_MAX_MCAST_ADDR);

	spin_lock(&netcp->lock);
	/* first clear all marks */
	netcp_addr_clear_mark(netcp);

	/* next add new entries, mark existing ones */
	netcp_addr_add_mark(netcp, ndev->broadcast, ADDR_BCAST);
	for_each_dev_addr(ndev, ndev_addr)
		netcp_addr_add_mark(netcp, ndev_addr->addr, ADDR_DEV);
	netdev_for_each_uc_addr(ndev_addr, ndev)
		netcp_addr_add_mark(netcp, ndev_addr->addr, ADDR_UCAST);
	netdev_for_each_mc_addr(ndev_addr, ndev)
		netcp_addr_add_mark(netcp, ndev_addr->addr, ADDR_MCAST);

	if (promisc)
		netcp_addr_add_mark(netcp, NULL, ADDR_ANY);

	/* finally sweep and callout into modules */
	netcp_addr_sweep_del(netcp);
	netcp_addr_sweep_add(netcp);
	spin_unlock(&netcp->lock);
}

static void netcp_free_navigator_resources(struct netcp_intf *netcp)
{
	int i;

	if (netcp->rx_channel) {
		knav_dma_close_channel(netcp->rx_channel);
		netcp->rx_channel = NULL;
	}

	if (!IS_ERR_OR_NULL(netcp->rx_pool))
		netcp_rxpool_free(netcp);

	if (!IS_ERR_OR_NULL(netcp->rx_queue)) {
		knav_queue_close(netcp->rx_queue);
		netcp->rx_queue = NULL;
	}

	for (i = 0; i < KNAV_DMA_FDQ_PER_CHAN &&
	     !IS_ERR_OR_NULL(netcp->rx_fdq[i]) ; ++i) {
		knav_queue_close(netcp->rx_fdq[i]);
		netcp->rx_fdq[i] = NULL;
	}

	if (!IS_ERR_OR_NULL(netcp->tx_compl_q)) {
		knav_queue_close(netcp->tx_compl_q);
		netcp->tx_compl_q = NULL;
	}

	if (!IS_ERR_OR_NULL(netcp->tx_pool)) {
		knav_pool_destroy(netcp->tx_pool);
		netcp->tx_pool = NULL;
	}
}

static int netcp_setup_navigator_resources(struct net_device *ndev)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct knav_queue_notify_config notify_cfg;
	struct knav_dma_cfg config;
	u32 last_fdq = 0;
	u8 name[16];
	int ret;
	int i;

	/* Create Rx/Tx descriptor pools */
	snprintf(name, sizeof(name), "rx-pool-%s", ndev->name);
	netcp->rx_pool = knav_pool_create(name, netcp->rx_pool_size,
						netcp->rx_pool_region_id);
	if (IS_ERR_OR_NULL(netcp->rx_pool)) {
		dev_err(netcp->ndev_dev, "Couldn't create rx pool\n");
		ret = PTR_ERR(netcp->rx_pool);
		goto fail;
	}

	snprintf(name, sizeof(name), "tx-pool-%s", ndev->name);
	netcp->tx_pool = knav_pool_create(name, netcp->tx_pool_size,
						netcp->tx_pool_region_id);
	if (IS_ERR_OR_NULL(netcp->tx_pool)) {
		dev_err(netcp->ndev_dev, "Couldn't create tx pool\n");
		ret = PTR_ERR(netcp->tx_pool);
		goto fail;
	}

	/* open Tx completion queue */
	snprintf(name, sizeof(name), "tx-compl-%s", ndev->name);
	netcp->tx_compl_q = knav_queue_open(name, netcp->tx_compl_qid, 0);
	if (IS_ERR_OR_NULL(netcp->tx_compl_q)) {
		ret = PTR_ERR(netcp->tx_compl_q);
		goto fail;
	}
	netcp->tx_compl_qid = knav_queue_get_id(netcp->tx_compl_q);

	/* Set notification for Tx completion */
	notify_cfg.fn = netcp_tx_notify;
	notify_cfg.fn_arg = netcp;
	ret = knav_queue_device_control(netcp->tx_compl_q,
					KNAV_QUEUE_SET_NOTIFIER,
					(unsigned long)&notify_cfg);
	if (ret)
		goto fail;

	knav_queue_disable_notify(netcp->tx_compl_q);

	/* open Rx completion queue */
	snprintf(name, sizeof(name), "rx-compl-%s", ndev->name);
	netcp->rx_queue = knav_queue_open(name, netcp->rx_queue_id, 0);
	if (IS_ERR_OR_NULL(netcp->rx_queue)) {
		ret = PTR_ERR(netcp->rx_queue);
		goto fail;
	}
	netcp->rx_queue_id = knav_queue_get_id(netcp->rx_queue);

	/* Set notification for Rx completion */
	notify_cfg.fn = netcp_rx_notify;
	notify_cfg.fn_arg = netcp;
	ret = knav_queue_device_control(netcp->rx_queue,
					KNAV_QUEUE_SET_NOTIFIER,
					(unsigned long)&notify_cfg);
	if (ret)
		goto fail;

	knav_queue_disable_notify(netcp->rx_queue);

	/* open Rx FDQs */
	for (i = 0; i < KNAV_DMA_FDQ_PER_CHAN && netcp->rx_queue_depths[i];
	     ++i) {
		snprintf(name, sizeof(name), "rx-fdq-%s-%d", ndev->name, i);
		netcp->rx_fdq[i] = knav_queue_open(name, KNAV_QUEUE_GP, 0);
		if (IS_ERR_OR_NULL(netcp->rx_fdq[i])) {
			ret = PTR_ERR(netcp->rx_fdq[i]);
			goto fail;
		}
	}

	memset(&config, 0, sizeof(config));
	config.direction		= DMA_DEV_TO_MEM;
	config.u.rx.einfo_present	= true;
	config.u.rx.psinfo_present	= true;
	config.u.rx.err_mode		= DMA_DROP;
	config.u.rx.desc_type		= DMA_DESC_HOST;
	config.u.rx.psinfo_at_sop	= false;
	config.u.rx.sop_offset		= NETCP_SOP_OFFSET;
	config.u.rx.dst_q		= netcp->rx_queue_id;
	config.u.rx.thresh		= DMA_THRESH_NONE;

	for (i = 0; i < KNAV_DMA_FDQ_PER_CHAN; ++i) {
		if (netcp->rx_fdq[i])
			last_fdq = knav_queue_get_id(netcp->rx_fdq[i]);
		config.u.rx.fdq[i] = last_fdq;
	}

	netcp->rx_channel = knav_dma_open_channel(netcp->netcp_device->device,
					netcp->dma_chan_name, &config);
	if (IS_ERR_OR_NULL(netcp->rx_channel)) {
		dev_err(netcp->ndev_dev, "failed opening rx chan(%s\n",
			netcp->dma_chan_name);
		goto fail;
	}

	dev_dbg(netcp->ndev_dev, "opened RX channel: %p\n", netcp->rx_channel);
	return 0;

fail:
	netcp_free_navigator_resources(netcp);
	return ret;
}

/* Open the device */
static int netcp_ndo_open(struct net_device *ndev)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct netcp_intf_modpriv *intf_modpriv;
	struct netcp_module *module;
	int ret;

	netif_carrier_off(ndev);
	ret = netcp_setup_navigator_resources(ndev);
	if (ret) {
		dev_err(netcp->ndev_dev, "Failed to setup navigator resources\n");
		goto fail;
	}

	for_each_module(netcp, intf_modpriv) {
		module = intf_modpriv->netcp_module;
		if (module->open) {
			ret = module->open(intf_modpriv->module_priv, ndev);
			if (ret != 0) {
				dev_err(netcp->ndev_dev, "module open failed\n");
				goto fail_open;
			}
		}
	}

	napi_enable(&netcp->rx_napi);
	napi_enable(&netcp->tx_napi);
	knav_queue_enable_notify(netcp->tx_compl_q);
	knav_queue_enable_notify(netcp->rx_queue);
	netcp_rxpool_refill(netcp);
	netif_tx_wake_all_queues(ndev);
	dev_dbg(netcp->ndev_dev, "netcp device %s opened\n", ndev->name);
	return 0;

fail_open:
	for_each_module(netcp, intf_modpriv) {
		module = intf_modpriv->netcp_module;
		if (module->close)
			module->close(intf_modpriv->module_priv, ndev);
	}

fail:
	netcp_free_navigator_resources(netcp);
	return ret;
}

/* Close the device */
static int netcp_ndo_stop(struct net_device *ndev)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct netcp_intf_modpriv *intf_modpriv;
	struct netcp_module *module;
	int err = 0;

	netif_tx_stop_all_queues(ndev);
	netif_carrier_off(ndev);
	netcp_addr_clear_mark(netcp);
	netcp_addr_sweep_del(netcp);
	knav_queue_disable_notify(netcp->rx_queue);
	knav_queue_disable_notify(netcp->tx_compl_q);
	napi_disable(&netcp->rx_napi);
	napi_disable(&netcp->tx_napi);

	for_each_module(netcp, intf_modpriv) {
		module = intf_modpriv->netcp_module;
		if (module->close) {
			err = module->close(intf_modpriv->module_priv, ndev);
			if (err != 0)
				dev_err(netcp->ndev_dev, "Close failed\n");
		}
	}

	/* Recycle Rx descriptors from completion queue */
	netcp_empty_rx_queue(netcp);

	/* Recycle Tx descriptors from completion queue */
	netcp_process_tx_compl_packets(netcp, netcp->tx_pool_size);

	if (knav_pool_count(netcp->tx_pool) != netcp->tx_pool_size)
		dev_err(netcp->ndev_dev, "Lost (%d) Tx descs\n",
			netcp->tx_pool_size - knav_pool_count(netcp->tx_pool));

	netcp_free_navigator_resources(netcp);
	dev_dbg(netcp->ndev_dev, "netcp device %s stopped\n", ndev->name);
	return 0;
}

static int netcp_ndo_ioctl(struct net_device *ndev,
			   struct ifreq *req, int cmd)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct netcp_intf_modpriv *intf_modpriv;
	struct netcp_module *module;
	int ret = -1, err = -EOPNOTSUPP;

	if (!netif_running(ndev))
		return -EINVAL;

	for_each_module(netcp, intf_modpriv) {
		module = intf_modpriv->netcp_module;
		if (!module->ioctl)
			continue;

		err = module->ioctl(intf_modpriv->module_priv, req, cmd);
		if ((err < 0) && (err != -EOPNOTSUPP)) {
			ret = err;
			goto out;
		}
		if (err == 0)
			ret = err;
	}

out:
	return (ret == 0) ? 0 : err;
}

static void netcp_ndo_tx_timeout(struct net_device *ndev)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	unsigned int descs = knav_pool_count(netcp->tx_pool);

	dev_err(netcp->ndev_dev, "transmit timed out tx descs(%d)\n", descs);
	netcp_process_tx_compl_packets(netcp, netcp->tx_pool_size);
	netif_trans_update(ndev);
	netif_tx_wake_all_queues(ndev);
}

static int netcp_rx_add_vid(struct net_device *ndev, __be16 proto, u16 vid)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct netcp_intf_modpriv *intf_modpriv;
	struct netcp_module *module;
	unsigned long flags;
	int err = 0;

	dev_dbg(netcp->ndev_dev, "adding rx vlan id: %d\n", vid);

	spin_lock_irqsave(&netcp->lock, flags);
	for_each_module(netcp, intf_modpriv) {
		module = intf_modpriv->netcp_module;
		if ((module->add_vid) && (vid != 0)) {
			err = module->add_vid(intf_modpriv->module_priv, vid);
			if (err != 0) {
				dev_err(netcp->ndev_dev, "Could not add vlan id = %d\n",
					vid);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&netcp->lock, flags);

	return err;
}

static int netcp_rx_kill_vid(struct net_device *ndev, __be16 proto, u16 vid)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct netcp_intf_modpriv *intf_modpriv;
	struct netcp_module *module;
	unsigned long flags;
	int err = 0;

	dev_dbg(netcp->ndev_dev, "removing rx vlan id: %d\n", vid);

	spin_lock_irqsave(&netcp->lock, flags);
	for_each_module(netcp, intf_modpriv) {
		module = intf_modpriv->netcp_module;
		if (module->del_vid) {
			err = module->del_vid(intf_modpriv->module_priv, vid);
			if (err != 0) {
				dev_err(netcp->ndev_dev, "Could not delete vlan id = %d\n",
					vid);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&netcp->lock, flags);
	return err;
}

static u16 netcp_select_queue(struct net_device *dev, struct sk_buff *skb,
			      void *accel_priv,
			      select_queue_fallback_t fallback)
{
	return 0;
}

static int netcp_setup_tc(struct net_device *dev, u32 handle, __be16 proto,
			  struct tc_to_netdev *tc)
{
	int i;

	/* setup tc must be called under rtnl lock */
	ASSERT_RTNL();

	if (tc->type != TC_SETUP_MQPRIO)
		return -EINVAL;

	/* Sanity-check the number of traffic classes requested */
	if ((dev->real_num_tx_queues <= 1) ||
	    (dev->real_num_tx_queues < tc->tc))
		return -EINVAL;

	/* Configure traffic class to queue mappings */
	if (tc->tc) {
		netdev_set_num_tc(dev, tc->tc);
		for (i = 0; i < tc->tc; i++)
			netdev_set_tc_queue(dev, i, 1, i);
	} else {
		netdev_reset_tc(dev);
	}

	return 0;
}

static const struct net_device_ops netcp_netdev_ops = {
	.ndo_open		= netcp_ndo_open,
	.ndo_stop		= netcp_ndo_stop,
	.ndo_start_xmit		= netcp_ndo_start_xmit,
	.ndo_set_rx_mode	= netcp_set_rx_mode,
	.ndo_do_ioctl           = netcp_ndo_ioctl,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_vlan_rx_add_vid	= netcp_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= netcp_rx_kill_vid,
	.ndo_tx_timeout		= netcp_ndo_tx_timeout,
	.ndo_select_queue	= netcp_select_queue,
	.ndo_setup_tc		= netcp_setup_tc,
};

static int netcp_create_interface(struct netcp_device *netcp_device,
				  struct device_node *node_interface)
{
	struct device *dev = netcp_device->device;
	struct device_node *node = dev->of_node;
	struct netcp_intf *netcp;
	struct net_device *ndev;
	resource_size_t size;
	struct resource res;
	void __iomem *efuse = NULL;
	u32 efuse_mac = 0;
	const void *mac_addr;
	u8 efuse_mac_addr[6];
	u32 temp[2];
	int ret = 0;

	ndev = alloc_etherdev_mqs(sizeof(*netcp), 1, 1);
	if (!ndev) {
		dev_err(dev, "Error allocating netdev\n");
		return -ENOMEM;
	}

	ndev->features |= NETIF_F_SG;
	ndev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;
	ndev->hw_features = ndev->features;
	ndev->vlan_features |=  NETIF_F_SG;

	/* MTU range: 68 - 9486 */
	ndev->min_mtu = ETH_MIN_MTU;
	ndev->max_mtu = NETCP_MAX_FRAME_SIZE - (ETH_HLEN + ETH_FCS_LEN);

	netcp = netdev_priv(ndev);
	spin_lock_init(&netcp->lock);
	INIT_LIST_HEAD(&netcp->module_head);
	INIT_LIST_HEAD(&netcp->txhook_list_head);
	INIT_LIST_HEAD(&netcp->rxhook_list_head);
	INIT_LIST_HEAD(&netcp->addr_list);
	netcp->netcp_device = netcp_device;
	netcp->dev = netcp_device->device;
	netcp->ndev = ndev;
	netcp->ndev_dev  = &ndev->dev;
	netcp->msg_enable = netif_msg_init(netcp_debug_level, NETCP_DEBUG);
	netcp->tx_pause_threshold = MAX_SKB_FRAGS;
	netcp->tx_resume_threshold = netcp->tx_pause_threshold;
	netcp->node_interface = node_interface;

	ret = of_property_read_u32(node_interface, "efuse-mac", &efuse_mac);
	if (efuse_mac) {
		if (of_address_to_resource(node, NETCP_EFUSE_REG_INDEX, &res)) {
			dev_err(dev, "could not find efuse-mac reg resource\n");
			ret = -ENODEV;
			goto quit;
		}
		size = resource_size(&res);

		if (!devm_request_mem_region(dev, res.start, size,
					     dev_name(dev))) {
			dev_err(dev, "could not reserve resource\n");
			ret = -ENOMEM;
			goto quit;
		}

		efuse = devm_ioremap_nocache(dev, res.start, size);
		if (!efuse) {
			dev_err(dev, "could not map resource\n");
			devm_release_mem_region(dev, res.start, size);
			ret = -ENOMEM;
			goto quit;
		}

		emac_arch_get_mac_addr(efuse_mac_addr, efuse, efuse_mac);
		if (is_valid_ether_addr(efuse_mac_addr))
			ether_addr_copy(ndev->dev_addr, efuse_mac_addr);
		else
			random_ether_addr(ndev->dev_addr);

		devm_iounmap(dev, efuse);
		devm_release_mem_region(dev, res.start, size);
	} else {
		mac_addr = of_get_mac_address(node_interface);
		if (mac_addr)
			ether_addr_copy(ndev->dev_addr, mac_addr);
		else
			random_ether_addr(ndev->dev_addr);
	}

	ret = of_property_read_string(node_interface, "rx-channel",
				      &netcp->dma_chan_name);
	if (ret < 0) {
		dev_err(dev, "missing \"rx-channel\" parameter\n");
		ret = -ENODEV;
		goto quit;
	}

	ret = of_property_read_u32(node_interface, "rx-queue",
				   &netcp->rx_queue_id);
	if (ret < 0) {
		dev_warn(dev, "missing \"rx-queue\" parameter\n");
		netcp->rx_queue_id = KNAV_QUEUE_QPEND;
	}

	ret = of_property_read_u32_array(node_interface, "rx-queue-depth",
					 netcp->rx_queue_depths,
					 KNAV_DMA_FDQ_PER_CHAN);
	if (ret < 0) {
		dev_err(dev, "missing \"rx-queue-depth\" parameter\n");
		netcp->rx_queue_depths[0] = 128;
	}

	ret = of_property_read_u32_array(node_interface, "rx-pool", temp, 2);
	if (ret < 0) {
		dev_err(dev, "missing \"rx-pool\" parameter\n");
		ret = -ENODEV;
		goto quit;
	}
	netcp->rx_pool_size = temp[0];
	netcp->rx_pool_region_id = temp[1];

	ret = of_property_read_u32_array(node_interface, "tx-pool", temp, 2);
	if (ret < 0) {
		dev_err(dev, "missing \"tx-pool\" parameter\n");
		ret = -ENODEV;
		goto quit;
	}
	netcp->tx_pool_size = temp[0];
	netcp->tx_pool_region_id = temp[1];

	if (netcp->tx_pool_size < MAX_SKB_FRAGS) {
		dev_err(dev, "tx-pool size too small, must be atleast(%ld)\n",
			MAX_SKB_FRAGS);
		ret = -ENODEV;
		goto quit;
	}

	ret = of_property_read_u32(node_interface, "tx-completion-queue",
				   &netcp->tx_compl_qid);
	if (ret < 0) {
		dev_warn(dev, "missing \"tx-completion-queue\" parameter\n");
		netcp->tx_compl_qid = KNAV_QUEUE_QPEND;
	}

	/* NAPI register */
	netif_napi_add(ndev, &netcp->rx_napi, netcp_rx_poll, NETCP_NAPI_WEIGHT);
	netif_tx_napi_add(ndev, &netcp->tx_napi, netcp_tx_poll, NETCP_NAPI_WEIGHT);

	/* Register the network device */
	ndev->dev_id		= 0;
	ndev->watchdog_timeo	= NETCP_TX_TIMEOUT;
	ndev->netdev_ops	= &netcp_netdev_ops;
	SET_NETDEV_DEV(ndev, dev);

	list_add_tail(&netcp->interface_list, &netcp_device->interface_head);
	return 0;

quit:
	free_netdev(ndev);
	return ret;
}

static void netcp_delete_interface(struct netcp_device *netcp_device,
				   struct net_device *ndev)
{
	struct netcp_intf_modpriv *intf_modpriv, *tmp;
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct netcp_module *module;

	dev_dbg(netcp_device->device, "Removing interface \"%s\"\n",
		ndev->name);

	/* Notify each of the modules that the interface is going away */
	list_for_each_entry_safe(intf_modpriv, tmp, &netcp->module_head,
				 intf_list) {
		module = intf_modpriv->netcp_module;
		dev_dbg(netcp_device->device, "Releasing module \"%s\"\n",
			module->name);
		if (module->release)
			module->release(intf_modpriv->module_priv);
		list_del(&intf_modpriv->intf_list);
	}
	WARN(!list_empty(&netcp->module_head), "%s interface module list is not empty!\n",
	     ndev->name);

	list_del(&netcp->interface_list);

	of_node_put(netcp->node_interface);
	unregister_netdev(ndev);
	netif_napi_del(&netcp->rx_napi);
	free_netdev(ndev);
}

static int netcp_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct netcp_intf *netcp_intf, *netcp_tmp;
	struct device_node *child, *interfaces;
	struct netcp_device *netcp_device;
	struct device *dev = &pdev->dev;
	int ret;

	if (!node) {
		dev_err(dev, "could not find device info\n");
		return -ENODEV;
	}

	/* Allocate a new NETCP device instance */
	netcp_device = devm_kzalloc(dev, sizeof(*netcp_device), GFP_KERNEL);
	if (!netcp_device)
		return -ENOMEM;

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_err(dev, "Failed to enable NETCP power-domain\n");
		pm_runtime_disable(&pdev->dev);
		return ret;
	}

	/* Initialize the NETCP device instance */
	INIT_LIST_HEAD(&netcp_device->interface_head);
	INIT_LIST_HEAD(&netcp_device->modpriv_head);
	netcp_device->device = dev;
	platform_set_drvdata(pdev, netcp_device);

	/* create interfaces */
	interfaces = of_get_child_by_name(node, "netcp-interfaces");
	if (!interfaces) {
		dev_err(dev, "could not find netcp-interfaces node\n");
		ret = -ENODEV;
		goto probe_quit;
	}

	for_each_available_child_of_node(interfaces, child) {
		ret = netcp_create_interface(netcp_device, child);
		if (ret) {
			dev_err(dev, "could not create interface(%s)\n",
				child->name);
			goto probe_quit_interface;
		}
	}

	of_node_put(interfaces);

	/* Add the device instance to the list */
	list_add_tail(&netcp_device->device_list, &netcp_devices);

	return 0;

probe_quit_interface:
	list_for_each_entry_safe(netcp_intf, netcp_tmp,
				 &netcp_device->interface_head,
				 interface_list) {
		netcp_delete_interface(netcp_device, netcp_intf->ndev);
	}

	of_node_put(interfaces);

probe_quit:
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int netcp_remove(struct platform_device *pdev)
{
	struct netcp_device *netcp_device = platform_get_drvdata(pdev);
	struct netcp_intf *netcp_intf, *netcp_tmp;
	struct netcp_inst_modpriv *inst_modpriv, *tmp;
	struct netcp_module *module;

	list_for_each_entry_safe(inst_modpriv, tmp, &netcp_device->modpriv_head,
				 inst_list) {
		module = inst_modpriv->netcp_module;
		dev_dbg(&pdev->dev, "Removing module \"%s\"\n", module->name);
		module->remove(netcp_device, inst_modpriv->module_priv);
		list_del(&inst_modpriv->inst_list);
	}

	/* now that all modules are removed, clean up the interfaces */
	list_for_each_entry_safe(netcp_intf, netcp_tmp,
				 &netcp_device->interface_head,
				 interface_list) {
		netcp_delete_interface(netcp_device, netcp_intf->ndev);
	}

	WARN(!list_empty(&netcp_device->interface_head),
	     "%s interface list not empty!\n", pdev->name);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id of_match[] = {
	{ .compatible = "ti,netcp-1.0", },
	{},
};
MODULE_DEVICE_TABLE(of, of_match);

static struct platform_driver netcp_driver = {
	.driver = {
		.name		= "netcp-1.0",
		.of_match_table	= of_match,
	},
	.probe = netcp_probe,
	.remove = netcp_remove,
};
module_platform_driver(netcp_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI NETCP driver for Keystone SOCs");
MODULE_AUTHOR("Sandeep Nair <sandeep_n@ti.com");

/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2015 Quantenna Communications. All rights reserved. */

#ifndef QTNFMAC_BUS_H
#define QTNFMAC_BUS_H

#include <linux/netdevice.h>
#include <linux/workqueue.h>

#include "trans.h"
#include "core.h"

#define QTNF_MAX_MAC		3

#define HBM_FRAME_META_MAGIC_PATTERN_S	0xAB
#define HBM_FRAME_META_MAGIC_PATTERN_E	0xBA

struct qtnf_frame_meta_info {
	u8 magic_s;
	u8 ifidx;
	u8 macid;
	u8 magic_e;
} __packed;

enum qtnf_fw_state {
	QTNF_FW_STATE_DETACHED,
	QTNF_FW_STATE_BOOT_DONE,
	QTNF_FW_STATE_ACTIVE,
	QTNF_FW_STATE_RUNNING,
	QTNF_FW_STATE_DEAD,
};

struct qtnf_bus;

struct qtnf_bus_ops {
	/* mgmt methods */
	int (*preinit)(struct qtnf_bus *);
	void (*stop)(struct qtnf_bus *);

	/* control path methods */
	int (*control_tx)(struct qtnf_bus *, struct sk_buff *);

	/* data xfer methods */
	int (*data_tx)(struct qtnf_bus *bus, struct sk_buff *skb,
		       unsigned int macid, unsigned int vifid);
	void (*data_tx_timeout)(struct qtnf_bus *, struct net_device *);
	void (*data_tx_use_meta_set)(struct qtnf_bus *bus, bool use_meta);
	void (*data_rx_start)(struct qtnf_bus *);
	void (*data_rx_stop)(struct qtnf_bus *);
};

struct qtnf_bus {
	struct device *dev;
	enum qtnf_fw_state fw_state;
	u32 chip;
	u32 chiprev;
	struct qtnf_bus_ops *bus_ops;
	struct qtnf_wmac *mac[QTNF_MAX_MAC];
	struct qtnf_qlink_transport trans;
	struct qtnf_hw_info hw_info;
	struct napi_struct mux_napi;
	struct net_device mux_dev;
	struct workqueue_struct *workqueue;
	struct workqueue_struct *hprio_workqueue;
	struct work_struct fw_work;
	struct work_struct event_work;
	struct mutex bus_lock; /* lock during command/event processing */
	struct dentry *dbg_dir;
	struct notifier_block netdev_nb;
	u8 hw_id[ETH_ALEN];
	/* bus private data */
	char bus_priv[0] __aligned(sizeof(void *));
};

static inline bool qtnf_fw_is_up(struct qtnf_bus *bus)
{
	enum qtnf_fw_state state = bus->fw_state;

	return ((state == QTNF_FW_STATE_ACTIVE) ||
		(state == QTNF_FW_STATE_RUNNING));
}

static inline bool qtnf_fw_is_attached(struct qtnf_bus *bus)
{
	enum qtnf_fw_state state = bus->fw_state;

	return ((state == QTNF_FW_STATE_ACTIVE) ||
		(state == QTNF_FW_STATE_RUNNING) ||
		(state == QTNF_FW_STATE_DEAD));
}

static inline void *get_bus_priv(struct qtnf_bus *bus)
{
	if (WARN(!bus, "qtnfmac: invalid bus pointer"))
		return NULL;

	return &bus->bus_priv;
}

/* callback wrappers */

static inline int qtnf_bus_preinit(struct qtnf_bus *bus)
{
	if (!bus->bus_ops->preinit)
		return 0;
	return bus->bus_ops->preinit(bus);
}

static inline void qtnf_bus_stop(struct qtnf_bus *bus)
{
	if (!bus->bus_ops->stop)
		return;
	bus->bus_ops->stop(bus);
}

static inline int qtnf_bus_data_tx(struct qtnf_bus *bus, struct sk_buff *skb,
				   unsigned int macid, unsigned int vifid)
{
	return bus->bus_ops->data_tx(bus, skb, macid, vifid);
}

static inline void
qtnf_bus_data_tx_timeout(struct qtnf_bus *bus, struct net_device *ndev)
{
	return bus->bus_ops->data_tx_timeout(bus, ndev);
}

static inline int qtnf_bus_control_tx(struct qtnf_bus *bus, struct sk_buff *skb)
{
	return bus->bus_ops->control_tx(bus, skb);
}

static inline void qtnf_bus_data_rx_start(struct qtnf_bus *bus)
{
	return bus->bus_ops->data_rx_start(bus);
}

static inline void qtnf_bus_data_rx_stop(struct qtnf_bus *bus)
{
	return bus->bus_ops->data_rx_stop(bus);
}

static __always_inline void qtnf_bus_lock(struct qtnf_bus *bus)
{
	mutex_lock(&bus->bus_lock);
}

static __always_inline void qtnf_bus_unlock(struct qtnf_bus *bus)
{
	mutex_unlock(&bus->bus_lock);
}

/* interface functions from common layer */

int qtnf_core_attach(struct qtnf_bus *bus);
void qtnf_core_detach(struct qtnf_bus *bus);

#endif /* QTNFMAC_BUS_H */

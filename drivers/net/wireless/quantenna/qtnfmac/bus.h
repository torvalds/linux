/*
 * Copyright (c) 2015 Quantenna Communications
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef QTNFMAC_BUS_H
#define QTNFMAC_BUS_H

#include <linux/netdevice.h>
#include <linux/workqueue.h>

#include "trans.h"
#include "core.h"

#define QTNF_MAX_MAC		3

enum qtnf_fw_state {
	QTNF_FW_STATE_RESET,
	QTNF_FW_STATE_FW_DNLD_DONE,
	QTNF_FW_STATE_BOOT_DONE,
	QTNF_FW_STATE_ACTIVE,
	QTNF_FW_STATE_DETACHED,
	QTNF_FW_STATE_EP_DEAD,
};

struct qtnf_bus;

struct qtnf_bus_ops {
	/* mgmt methods */
	int (*preinit)(struct qtnf_bus *);
	void (*stop)(struct qtnf_bus *);

	/* control path methods */
	int (*control_tx)(struct qtnf_bus *, struct sk_buff *);

	/* data xfer methods */
	int (*data_tx)(struct qtnf_bus *, struct sk_buff *);
	void (*data_tx_timeout)(struct qtnf_bus *, struct net_device *);
	void (*data_rx_start)(struct qtnf_bus *);
	void (*data_rx_stop)(struct qtnf_bus *);
};

struct qtnf_bus {
	struct device *dev;
	enum qtnf_fw_state fw_state;
	u32 chip;
	u32 chiprev;
	const struct qtnf_bus_ops *bus_ops;
	struct qtnf_wmac *mac[QTNF_MAX_MAC];
	struct qtnf_qlink_transport trans;
	struct qtnf_hw_info hw_info;
	struct napi_struct mux_napi;
	struct net_device mux_dev;
	struct workqueue_struct *workqueue;
	struct work_struct fw_work;
	struct work_struct event_work;
	struct mutex bus_lock; /* lock during command/event processing */
	struct dentry *dbg_dir;
	/* bus private data */
	char bus_priv[0] __aligned(sizeof(void *));
};

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

static inline int qtnf_bus_data_tx(struct qtnf_bus *bus, struct sk_buff *skb)
{
	return bus->bus_ops->data_tx(bus, skb);
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
void qtnf_txflowblock(struct device *dev, bool state);
void qtnf_txcomplete(struct device *dev, struct sk_buff *txp, bool success);

#endif /* QTNFMAC_BUS_H */

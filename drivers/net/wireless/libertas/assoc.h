/* Copyright (C) 2006, Red Hat, Inc. */

#ifndef _WLAN_ASSOC_H_
#define _WLAN_ASSOC_H_

#include "dev.h"

void libertas_association_worker(struct work_struct *work);

struct assoc_request * wlan_get_association_request(wlan_adapter *adapter);

void libertas_sync_channel(struct work_struct *work);

#define ASSOC_DELAY (HZ / 2)
static inline void wlan_postpone_association_work(wlan_private *priv)
{
	if (priv->adapter->surpriseremoved)
		return;
	cancel_delayed_work(&priv->assoc_work);
	queue_delayed_work(priv->assoc_thread, &priv->assoc_work, ASSOC_DELAY);
}

static inline void wlan_cancel_association_work(wlan_private *priv)
{
	cancel_delayed_work(&priv->assoc_work);
	if (priv->adapter->pending_assoc_req) {
		kfree(priv->adapter->pending_assoc_req);
		priv->adapter->pending_assoc_req = NULL;
	}
}

#endif /* _WLAN_ASSOC_H */

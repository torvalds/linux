/* Copyright (C) 2006, Red Hat, Inc. */

#ifndef _WLAN_ASSOC_H_
#define _WLAN_ASSOC_H_

#include "dev.h"

void libertas_association_worker(struct work_struct *work);

struct assoc_request * wlan_get_association_request(wlan_adapter *adapter);

void libertas_sync_channel(struct work_struct *work);

#endif /* _WLAN_ASSOC_H */

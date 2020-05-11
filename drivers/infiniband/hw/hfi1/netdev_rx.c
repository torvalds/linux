// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright(c) 2020 Intel Corporation.
 *
 */

/*
 * This file contains HFI1 support for netdev RX functionality
 */

#include "sdma.h"
#include "verbs.h"
#include "netdev.h"
#include "hfi.h"

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <rdma/ib_verbs.h>

/**
 * hfi1_netdev_add_data - Registers data with unique identifier
 * to be requested later this is needed for VNIC and IPoIB VLANs
 * implementations.
 * This call is protected by mutex idr_lock.
 *
 * @dd: hfi1 dev data
 * @id: requested integer id up to INT_MAX
 * @data: data to be associated with index
 */
int hfi1_netdev_add_data(struct hfi1_devdata *dd, int id, void *data)
{
	struct hfi1_netdev_priv *priv = hfi1_netdev_priv(dd->dummy_netdev);

	return xa_insert(&priv->dev_tbl, id, data, GFP_NOWAIT);
}

/**
 * hfi1_netdev_remove_data - Removes data with previously given id.
 * Returns the reference to removed entry.
 *
 * @dd: hfi1 dev data
 * @id: requested integer id up to INT_MAX
 */
void *hfi1_netdev_remove_data(struct hfi1_devdata *dd, int id)
{
	struct hfi1_netdev_priv *priv = hfi1_netdev_priv(dd->dummy_netdev);

	return xa_erase(&priv->dev_tbl, id);
}

/**
 * hfi1_netdev_get_data - Gets data with given id
 *
 * @dd: hfi1 dev data
 * @id: requested integer id up to INT_MAX
 */
void *hfi1_netdev_get_data(struct hfi1_devdata *dd, int id)
{
	struct hfi1_netdev_priv *priv = hfi1_netdev_priv(dd->dummy_netdev);

	return xa_load(&priv->dev_tbl, id);
}

/**
 * hfi1_netdev_get_first_dat - Gets first entry with greater or equal id.
 *
 * @dd: hfi1 dev data
 * @id: requested integer id up to INT_MAX
 */
void *hfi1_netdev_get_first_data(struct hfi1_devdata *dd, int *start_id)
{
	struct hfi1_netdev_priv *priv = hfi1_netdev_priv(dd->dummy_netdev);
	unsigned long index = *start_id;
	void *ret;

	ret = xa_find(&priv->dev_tbl, &index, UINT_MAX, XA_PRESENT);
	*start_id = (int)index;
	return ret;
}

// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2021 Intel Corporation

#include <linux/export.h>
#include <linux/peci.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "internal.h"

/**
 * peci_request_alloc() - allocate &struct peci_requests
 * @device: PECI device to which request is going to be sent
 * @tx_len: TX length
 * @rx_len: RX length
 *
 * Return: A pointer to a newly allocated &struct peci_request on success or NULL otherwise.
 */
struct peci_request *peci_request_alloc(struct peci_device *device, u8 tx_len, u8 rx_len)
{
	struct peci_request *req;

	/*
	 * TX and RX buffers are fixed length members of peci_request, this is
	 * just a warn for developers to make sure to expand the buffers (or
	 * change the allocation method) if we go over the current limit.
	 */
	if (WARN_ON_ONCE(tx_len > PECI_REQUEST_MAX_BUF_SIZE || rx_len > PECI_REQUEST_MAX_BUF_SIZE))
		return NULL;
	/*
	 * PECI controllers that we are using now don't support DMA, this
	 * should be converted to DMA API once support for controllers that do
	 * allow it is added to avoid an extra copy.
	 */
	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return NULL;

	req->device = device;
	req->tx.len = tx_len;
	req->rx.len = rx_len;

	return req;
}
EXPORT_SYMBOL_NS_GPL(peci_request_alloc, PECI);

/**
 * peci_request_free() - free peci_request
 * @req: the PECI request to be freed
 */
void peci_request_free(struct peci_request *req)
{
	kfree(req);
}
EXPORT_SYMBOL_NS_GPL(peci_request_free, PECI);

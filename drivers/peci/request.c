// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2021 Intel Corporation

#include <linux/bug.h>
#include <linux/export.h>
#include <linux/peci.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <asm/unaligned.h>

#include "internal.h"

#define PECI_GET_DIB_CMD		0xf7
#define  PECI_GET_DIB_WR_LEN		1
#define  PECI_GET_DIB_RD_LEN		8

#define PECI_RDPKGCFG_CMD		0xa1
#define  PECI_RDPKGCFG_WR_LEN		5
#define  PECI_RDPKGCFG_RD_LEN_BASE	1
#define PECI_WRPKGCFG_CMD		0xa5
#define  PECI_WRPKGCFG_WR_LEN_BASE	6
#define  PECI_WRPKGCFG_RD_LEN		1

/* Device Specific Completion Code (CC) Definition */
#define PECI_CC_SUCCESS				0x40
#define PECI_CC_NEED_RETRY			0x80
#define PECI_CC_OUT_OF_RESOURCE			0x81
#define PECI_CC_UNAVAIL_RESOURCE		0x82
#define PECI_CC_INVALID_REQ			0x90
#define PECI_CC_MCA_ERROR			0x91
#define PECI_CC_CATASTROPHIC_MCA_ERROR		0x93
#define PECI_CC_FATAL_MCA_ERROR			0x94
#define PECI_CC_PARITY_ERR_GPSB_OR_PMSB		0x98
#define PECI_CC_PARITY_ERR_GPSB_OR_PMSB_IERR	0x9B
#define PECI_CC_PARITY_ERR_GPSB_OR_PMSB_MCA	0x9C

#define PECI_RETRY_BIT			BIT(0)

#define PECI_RETRY_TIMEOUT		msecs_to_jiffies(700)
#define PECI_RETRY_INTERVAL_MIN		msecs_to_jiffies(1)
#define PECI_RETRY_INTERVAL_MAX		msecs_to_jiffies(128)

static u8 peci_request_data_cc(struct peci_request *req)
{
	return req->rx.buf[0];
}

/**
 * peci_request_status() - return -errno based on PECI completion code
 * @req: the PECI request that contains response data with completion code
 *
 * It can't be used for Ping(), GetDIB() and GetTemp() - for those commands we
 * don't expect completion code in the response.
 *
 * Return: -errno
 */
int peci_request_status(struct peci_request *req)
{
	u8 cc = peci_request_data_cc(req);

	if (cc != PECI_CC_SUCCESS)
		dev_dbg(&req->device->dev, "ret: %#02x\n", cc);

	switch (cc) {
	case PECI_CC_SUCCESS:
		return 0;
	case PECI_CC_NEED_RETRY:
	case PECI_CC_OUT_OF_RESOURCE:
	case PECI_CC_UNAVAIL_RESOURCE:
		return -EAGAIN;
	case PECI_CC_INVALID_REQ:
		return -EINVAL;
	case PECI_CC_MCA_ERROR:
	case PECI_CC_CATASTROPHIC_MCA_ERROR:
	case PECI_CC_FATAL_MCA_ERROR:
	case PECI_CC_PARITY_ERR_GPSB_OR_PMSB:
	case PECI_CC_PARITY_ERR_GPSB_OR_PMSB_IERR:
	case PECI_CC_PARITY_ERR_GPSB_OR_PMSB_MCA:
		return -EIO;
	}

	WARN_ONCE(1, "Unknown PECI completion code: %#02x\n", cc);

	return -EIO;
}
EXPORT_SYMBOL_NS_GPL(peci_request_status, PECI);

static int peci_request_xfer(struct peci_request *req)
{
	struct peci_device *device = req->device;
	struct peci_controller *controller = to_peci_controller(device->dev.parent);
	int ret;

	mutex_lock(&controller->bus_lock);
	ret = controller->ops->xfer(controller, device->addr, req);
	mutex_unlock(&controller->bus_lock);

	return ret;
}

static int peci_request_xfer_retry(struct peci_request *req)
{
	long wait_interval = PECI_RETRY_INTERVAL_MIN;
	struct peci_device *device = req->device;
	struct peci_controller *controller = to_peci_controller(device->dev.parent);
	unsigned long start = jiffies;
	int ret;

	/* Don't try to use it for ping */
	if (WARN_ON(req->tx.len == 0))
		return 0;

	do {
		ret = peci_request_xfer(req);
		if (ret) {
			dev_dbg(&controller->dev, "xfer error: %d\n", ret);
			return ret;
		}

		if (peci_request_status(req) != -EAGAIN)
			return 0;

		/* Set the retry bit to indicate a retry attempt */
		req->tx.buf[1] |= PECI_RETRY_BIT;

		if (schedule_timeout_interruptible(wait_interval))
			return -ERESTARTSYS;

		wait_interval = min_t(long, wait_interval * 2, PECI_RETRY_INTERVAL_MAX);
	} while (time_before(jiffies, start + PECI_RETRY_TIMEOUT));

	dev_dbg(&controller->dev, "request timed out\n");

	return -ETIMEDOUT;
}

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

struct peci_request *peci_xfer_get_dib(struct peci_device *device)
{
	struct peci_request *req;
	int ret;

	req = peci_request_alloc(device, PECI_GET_DIB_WR_LEN, PECI_GET_DIB_RD_LEN);
	if (!req)
		return ERR_PTR(-ENOMEM);

	req->tx.buf[0] = PECI_GET_DIB_CMD;

	ret = peci_request_xfer(req);
	if (ret) {
		peci_request_free(req);
		return ERR_PTR(ret);
	}

	return req;
}
EXPORT_SYMBOL_NS_GPL(peci_xfer_get_dib, PECI);

static struct peci_request *
__pkg_cfg_read(struct peci_device *device, u8 index, u16 param, u8 len)
{
	struct peci_request *req;
	int ret;

	req = peci_request_alloc(device, PECI_RDPKGCFG_WR_LEN, PECI_RDPKGCFG_RD_LEN_BASE + len);
	if (!req)
		return ERR_PTR(-ENOMEM);

	req->tx.buf[0] = PECI_RDPKGCFG_CMD;
	req->tx.buf[1] = 0;
	req->tx.buf[2] = index;
	put_unaligned_le16(param, &req->tx.buf[3]);

	ret = peci_request_xfer_retry(req);
	if (ret) {
		peci_request_free(req);
		return ERR_PTR(ret);
	}

	return req;
}

u8 peci_request_data_readb(struct peci_request *req)
{
	return req->rx.buf[1];
}
EXPORT_SYMBOL_NS_GPL(peci_request_data_readb, PECI);

u16 peci_request_data_readw(struct peci_request *req)
{
	return get_unaligned_le16(&req->rx.buf[1]);
}
EXPORT_SYMBOL_NS_GPL(peci_request_data_readw, PECI);

u32 peci_request_data_readl(struct peci_request *req)
{
	return get_unaligned_le32(&req->rx.buf[1]);
}
EXPORT_SYMBOL_NS_GPL(peci_request_data_readl, PECI);

u64 peci_request_data_readq(struct peci_request *req)
{
	return get_unaligned_le64(&req->rx.buf[1]);
}
EXPORT_SYMBOL_NS_GPL(peci_request_data_readq, PECI);

u64 peci_request_dib_read(struct peci_request *req)
{
	return get_unaligned_le64(&req->rx.buf[0]);
}
EXPORT_SYMBOL_NS_GPL(peci_request_dib_read, PECI);

#define __read_pkg_config(x, type) \
struct peci_request *peci_xfer_pkg_cfg_##x(struct peci_device *device, u8 index, u16 param) \
{ \
	return __pkg_cfg_read(device, index, param, sizeof(type)); \
} \
EXPORT_SYMBOL_NS_GPL(peci_xfer_pkg_cfg_##x, PECI)

__read_pkg_config(readb, u8);
__read_pkg_config(readw, u16);
__read_pkg_config(readl, u32);
__read_pkg_config(readq, u64);

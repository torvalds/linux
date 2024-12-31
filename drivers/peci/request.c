// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2021 Intel Corporation

#include <linux/bug.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/peci.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/unaligned.h>

#include "internal.h"

#define PECI_GET_DIB_CMD		0xf7
#define  PECI_GET_DIB_WR_LEN		1
#define  PECI_GET_DIB_RD_LEN		8

#define PECI_GET_TEMP_CMD		0x01
#define  PECI_GET_TEMP_WR_LEN		1
#define  PECI_GET_TEMP_RD_LEN		2

#define PECI_RDPKGCFG_CMD		0xa1
#define  PECI_RDPKGCFG_WR_LEN		5
#define  PECI_RDPKGCFG_RD_LEN_BASE	1
#define PECI_WRPKGCFG_CMD		0xa5
#define  PECI_WRPKGCFG_WR_LEN_BASE	6
#define  PECI_WRPKGCFG_RD_LEN		1

#define PECI_RDIAMSR_CMD		0xb1
#define  PECI_RDIAMSR_WR_LEN		5
#define  PECI_RDIAMSR_RD_LEN		9
#define PECI_WRIAMSR_CMD		0xb5
#define PECI_RDIAMSREX_CMD		0xd1
#define  PECI_RDIAMSREX_WR_LEN		6
#define  PECI_RDIAMSREX_RD_LEN		9

#define PECI_RDPCICFG_CMD		0x61
#define  PECI_RDPCICFG_WR_LEN		6
#define  PECI_RDPCICFG_RD_LEN		5
#define  PECI_RDPCICFG_RD_LEN_MAX	24
#define PECI_WRPCICFG_CMD		0x65

#define PECI_RDPCICFGLOCAL_CMD			0xe1
#define  PECI_RDPCICFGLOCAL_WR_LEN		5
#define  PECI_RDPCICFGLOCAL_RD_LEN_BASE		1
#define PECI_WRPCICFGLOCAL_CMD			0xe5
#define  PECI_WRPCICFGLOCAL_WR_LEN_BASE		6
#define  PECI_WRPCICFGLOCAL_RD_LEN		1

#define PECI_ENDPTCFG_TYPE_LOCAL_PCI		0x03
#define PECI_ENDPTCFG_TYPE_PCI			0x04
#define PECI_ENDPTCFG_TYPE_MMIO			0x05
#define PECI_ENDPTCFG_ADDR_TYPE_PCI		0x04
#define PECI_ENDPTCFG_ADDR_TYPE_MMIO_D		0x05
#define PECI_ENDPTCFG_ADDR_TYPE_MMIO_Q		0x06
#define PECI_RDENDPTCFG_CMD			0xc1
#define  PECI_RDENDPTCFG_PCI_WR_LEN		12
#define  PECI_RDENDPTCFG_MMIO_WR_LEN_BASE	10
#define  PECI_RDENDPTCFG_MMIO_D_WR_LEN		14
#define  PECI_RDENDPTCFG_MMIO_Q_WR_LEN		18
#define  PECI_RDENDPTCFG_RD_LEN_BASE		1
#define PECI_WRENDPTCFG_CMD			0xc5
#define  PECI_WRENDPTCFG_PCI_WR_LEN_BASE	13
#define  PECI_WRENDPTCFG_MMIO_D_WR_LEN_BASE	15
#define  PECI_WRENDPTCFG_MMIO_Q_WR_LEN_BASE	19
#define  PECI_WRENDPTCFG_RD_LEN			1

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
EXPORT_SYMBOL_NS_GPL(peci_request_status, "PECI");

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
EXPORT_SYMBOL_NS_GPL(peci_request_alloc, "PECI");

/**
 * peci_request_free() - free peci_request
 * @req: the PECI request to be freed
 */
void peci_request_free(struct peci_request *req)
{
	kfree(req);
}
EXPORT_SYMBOL_NS_GPL(peci_request_free, "PECI");

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
EXPORT_SYMBOL_NS_GPL(peci_xfer_get_dib, "PECI");

struct peci_request *peci_xfer_get_temp(struct peci_device *device)
{
	struct peci_request *req;
	int ret;

	req = peci_request_alloc(device, PECI_GET_TEMP_WR_LEN, PECI_GET_TEMP_RD_LEN);
	if (!req)
		return ERR_PTR(-ENOMEM);

	req->tx.buf[0] = PECI_GET_TEMP_CMD;

	ret = peci_request_xfer(req);
	if (ret) {
		peci_request_free(req);
		return ERR_PTR(ret);
	}

	return req;
}
EXPORT_SYMBOL_NS_GPL(peci_xfer_get_temp, "PECI");

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

static u32 __get_pci_addr(u8 bus, u8 dev, u8 func, u16 reg)
{
	return reg | PCI_DEVID(bus, PCI_DEVFN(dev, func)) << 12;
}

static struct peci_request *
__pci_cfg_local_read(struct peci_device *device, u8 bus, u8 dev, u8 func, u16 reg, u8 len)
{
	struct peci_request *req;
	u32 pci_addr;
	int ret;

	req = peci_request_alloc(device, PECI_RDPCICFGLOCAL_WR_LEN,
				 PECI_RDPCICFGLOCAL_RD_LEN_BASE + len);
	if (!req)
		return ERR_PTR(-ENOMEM);

	pci_addr = __get_pci_addr(bus, dev, func, reg);

	req->tx.buf[0] = PECI_RDPCICFGLOCAL_CMD;
	req->tx.buf[1] = 0;
	put_unaligned_le24(pci_addr, &req->tx.buf[2]);

	ret = peci_request_xfer_retry(req);
	if (ret) {
		peci_request_free(req);
		return ERR_PTR(ret);
	}

	return req;
}

static struct peci_request *
__ep_pci_cfg_read(struct peci_device *device, u8 msg_type, u8 seg,
		  u8 bus, u8 dev, u8 func, u16 reg, u8 len)
{
	struct peci_request *req;
	u32 pci_addr;
	int ret;

	req = peci_request_alloc(device, PECI_RDENDPTCFG_PCI_WR_LEN,
				 PECI_RDENDPTCFG_RD_LEN_BASE + len);
	if (!req)
		return ERR_PTR(-ENOMEM);

	pci_addr = __get_pci_addr(bus, dev, func, reg);

	req->tx.buf[0] = PECI_RDENDPTCFG_CMD;
	req->tx.buf[1] = 0;
	req->tx.buf[2] = msg_type;
	req->tx.buf[3] = 0;
	req->tx.buf[4] = 0;
	req->tx.buf[5] = 0;
	req->tx.buf[6] = PECI_ENDPTCFG_ADDR_TYPE_PCI;
	req->tx.buf[7] = seg; /* PCI Segment */
	put_unaligned_le32(pci_addr, &req->tx.buf[8]);

	ret = peci_request_xfer_retry(req);
	if (ret) {
		peci_request_free(req);
		return ERR_PTR(ret);
	}

	return req;
}

static struct peci_request *
__ep_mmio_read(struct peci_device *device, u8 bar, u8 addr_type, u8 seg,
	       u8 bus, u8 dev, u8 func, u64 offset, u8 tx_len, u8 len)
{
	struct peci_request *req;
	int ret;

	req = peci_request_alloc(device, tx_len, PECI_RDENDPTCFG_RD_LEN_BASE + len);
	if (!req)
		return ERR_PTR(-ENOMEM);

	req->tx.buf[0] = PECI_RDENDPTCFG_CMD;
	req->tx.buf[1] = 0;
	req->tx.buf[2] = PECI_ENDPTCFG_TYPE_MMIO;
	req->tx.buf[3] = 0; /* Endpoint ID */
	req->tx.buf[4] = 0; /* Reserved */
	req->tx.buf[5] = bar;
	req->tx.buf[6] = addr_type;
	req->tx.buf[7] = seg; /* PCI Segment */
	req->tx.buf[8] = PCI_DEVFN(dev, func);
	req->tx.buf[9] = bus; /* PCI Bus */

	if (addr_type == PECI_ENDPTCFG_ADDR_TYPE_MMIO_D)
		put_unaligned_le32(offset, &req->tx.buf[10]);
	else
		put_unaligned_le64(offset, &req->tx.buf[10]);

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
EXPORT_SYMBOL_NS_GPL(peci_request_data_readb, "PECI");

u16 peci_request_data_readw(struct peci_request *req)
{
	return get_unaligned_le16(&req->rx.buf[1]);
}
EXPORT_SYMBOL_NS_GPL(peci_request_data_readw, "PECI");

u32 peci_request_data_readl(struct peci_request *req)
{
	return get_unaligned_le32(&req->rx.buf[1]);
}
EXPORT_SYMBOL_NS_GPL(peci_request_data_readl, "PECI");

u64 peci_request_data_readq(struct peci_request *req)
{
	return get_unaligned_le64(&req->rx.buf[1]);
}
EXPORT_SYMBOL_NS_GPL(peci_request_data_readq, "PECI");

u64 peci_request_dib_read(struct peci_request *req)
{
	return get_unaligned_le64(&req->rx.buf[0]);
}
EXPORT_SYMBOL_NS_GPL(peci_request_dib_read, "PECI");

s16 peci_request_temp_read(struct peci_request *req)
{
	return get_unaligned_le16(&req->rx.buf[0]);
}
EXPORT_SYMBOL_NS_GPL(peci_request_temp_read, "PECI");

#define __read_pkg_config(x, type) \
struct peci_request *peci_xfer_pkg_cfg_##x(struct peci_device *device, u8 index, u16 param) \
{ \
	return __pkg_cfg_read(device, index, param, sizeof(type)); \
} \
EXPORT_SYMBOL_NS_GPL(peci_xfer_pkg_cfg_##x, "PECI")

__read_pkg_config(readb, u8);
__read_pkg_config(readw, u16);
__read_pkg_config(readl, u32);
__read_pkg_config(readq, u64);

#define __read_pci_config_local(x, type) \
struct peci_request * \
peci_xfer_pci_cfg_local_##x(struct peci_device *device, u8 bus, u8 dev, u8 func, u16 reg) \
{ \
	return __pci_cfg_local_read(device, bus, dev, func, reg, sizeof(type)); \
} \
EXPORT_SYMBOL_NS_GPL(peci_xfer_pci_cfg_local_##x, "PECI")

__read_pci_config_local(readb, u8);
__read_pci_config_local(readw, u16);
__read_pci_config_local(readl, u32);

#define __read_ep_pci_config(x, msg_type, type) \
struct peci_request * \
peci_xfer_ep_pci_cfg_##x(struct peci_device *device, u8 seg, u8 bus, u8 dev, u8 func, u16 reg) \
{ \
	return __ep_pci_cfg_read(device, msg_type, seg, bus, dev, func, reg, sizeof(type)); \
} \
EXPORT_SYMBOL_NS_GPL(peci_xfer_ep_pci_cfg_##x, "PECI")

__read_ep_pci_config(local_readb, PECI_ENDPTCFG_TYPE_LOCAL_PCI, u8);
__read_ep_pci_config(local_readw, PECI_ENDPTCFG_TYPE_LOCAL_PCI, u16);
__read_ep_pci_config(local_readl, PECI_ENDPTCFG_TYPE_LOCAL_PCI, u32);
__read_ep_pci_config(readb, PECI_ENDPTCFG_TYPE_PCI, u8);
__read_ep_pci_config(readw, PECI_ENDPTCFG_TYPE_PCI, u16);
__read_ep_pci_config(readl, PECI_ENDPTCFG_TYPE_PCI, u32);

#define __read_ep_mmio(x, y, addr_type, type1, type2) \
struct peci_request *peci_xfer_ep_mmio##y##_##x(struct peci_device *device, u8 bar, u8 seg, \
					   u8 bus, u8 dev, u8 func, u64 offset) \
{ \
	return __ep_mmio_read(device, bar, addr_type, seg, bus, dev, func, \
			      offset, PECI_RDENDPTCFG_MMIO_WR_LEN_BASE + sizeof(type1), \
			      sizeof(type2)); \
} \
EXPORT_SYMBOL_NS_GPL(peci_xfer_ep_mmio##y##_##x, "PECI")

__read_ep_mmio(readl, 32, PECI_ENDPTCFG_ADDR_TYPE_MMIO_D, u32, u32);
__read_ep_mmio(readl, 64, PECI_ENDPTCFG_ADDR_TYPE_MMIO_Q, u64, u32);

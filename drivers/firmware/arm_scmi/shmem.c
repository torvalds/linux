// SPDX-License-Identifier: GPL-2.0
/*
 * For transport using shared mem structure.
 *
 * Copyright (C) 2019-2024 ARM Ltd.
 */

#include <linux/ktime.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/processor.h>
#include <linux/types.h>

#include <linux/bug.h>

#include "common.h"

#define SCMI_SHMEM_LAYOUT_OVERHEAD	24

/*
 * SCMI specification requires all parameters, message headers, return
 * arguments or any protocol data to be expressed in little endian
 * format only.
 */
struct scmi_shared_mem {
	__le32 reserved;
	__le32 channel_status;
#define SCMI_SHMEM_CHAN_STAT_CHANNEL_ERROR	BIT(1)
#define SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE	BIT(0)
	__le32 reserved1[2];
	__le32 flags;
#define SCMI_SHMEM_FLAG_INTR_ENABLED	BIT(0)
	__le32 length;
	__le32 msg_header;
	u8 msg_payload[];
};

static inline void shmem_memcpy_fromio32(void *to,
					 const void __iomem *from,
					 size_t count)
{
	WARN_ON(!IS_ALIGNED((unsigned long)from, 4) ||
		!IS_ALIGNED((unsigned long)to, 4) ||
		count % 4);

	__ioread32_copy(to, from, count / 4);
}

static inline void shmem_memcpy_toio32(void __iomem *to,
				       const void *from,
				       size_t count)
{
	WARN_ON(!IS_ALIGNED((unsigned long)to, 4) ||
		!IS_ALIGNED((unsigned long)from, 4) ||
		count % 4);

	__iowrite32_copy(to, from, count / 4);
}

static struct scmi_shmem_io_ops shmem_io_ops32 = {
	.fromio	= shmem_memcpy_fromio32,
	.toio	= shmem_memcpy_toio32,
};

/* Wrappers are needed for proper memcpy_{from,to}_io expansion by the
 * pre-processor.
 */
static inline void shmem_memcpy_fromio(void *to,
				       const void __iomem *from,
				       size_t count)
{
	memcpy_fromio(to, from, count);
}

static inline void shmem_memcpy_toio(void __iomem *to,
				     const void *from,
				     size_t count)
{
	memcpy_toio(to, from, count);
}

static struct scmi_shmem_io_ops shmem_io_ops_default = {
	.fromio = shmem_memcpy_fromio,
	.toio	= shmem_memcpy_toio,
};

static void shmem_tx_prepare(struct scmi_shared_mem __iomem *shmem,
			     struct scmi_xfer *xfer,
			     struct scmi_chan_info *cinfo,
			     shmem_copy_toio_t copy_toio)
{
	ktime_t stop;

	/*
	 * Ideally channel must be free by now unless OS timeout last
	 * request and platform continued to process the same, wait
	 * until it releases the shared memory, otherwise we may endup
	 * overwriting its response with new message payload or vice-versa.
	 * Giving up anyway after twice the expected channel timeout so as
	 * not to bail-out on intermittent issues where the platform is
	 * occasionally a bit slower to answer.
	 *
	 * Note that after a timeout is detected we bail-out and carry on but
	 * the transport functionality is probably permanently compromised:
	 * this is just to ease debugging and avoid complete hangs on boot
	 * due to a misbehaving SCMI firmware.
	 */
	stop = ktime_add_ms(ktime_get(), 2 * cinfo->rx_timeout_ms);
	spin_until_cond((ioread32(&shmem->channel_status) &
			 SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE) ||
			 ktime_after(ktime_get(), stop));
	if (!(ioread32(&shmem->channel_status) &
	      SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE)) {
		WARN_ON_ONCE(1);
		dev_err(cinfo->dev,
			"Timeout waiting for a free TX channel !\n");
		return;
	}

	/* Mark channel busy + clear error */
	iowrite32(0x0, &shmem->channel_status);
	iowrite32(xfer->hdr.poll_completion ? 0 : SCMI_SHMEM_FLAG_INTR_ENABLED,
		  &shmem->flags);
	iowrite32(sizeof(shmem->msg_header) + xfer->tx.len, &shmem->length);
	iowrite32(pack_scmi_header(&xfer->hdr), &shmem->msg_header);
	if (xfer->tx.buf)
		copy_toio(shmem->msg_payload, xfer->tx.buf, xfer->tx.len);
}

static u32 shmem_read_header(struct scmi_shared_mem __iomem *shmem)
{
	return ioread32(&shmem->msg_header);
}

static void shmem_fetch_response(struct scmi_shared_mem __iomem *shmem,
				 struct scmi_xfer *xfer,
				 shmem_copy_fromio_t copy_fromio)
{
	size_t len = ioread32(&shmem->length);

	xfer->hdr.status = ioread32(shmem->msg_payload);
	/* Skip the length of header and status in shmem area i.e 8 bytes */
	xfer->rx.len = min_t(size_t, xfer->rx.len, len > 8 ? len - 8 : 0);

	/* Take a copy to the rx buffer.. */
	copy_fromio(xfer->rx.buf, shmem->msg_payload + 4, xfer->rx.len);
}

static void shmem_fetch_notification(struct scmi_shared_mem __iomem *shmem,
				     size_t max_len, struct scmi_xfer *xfer,
				     shmem_copy_fromio_t copy_fromio)
{
	size_t len = ioread32(&shmem->length);

	/* Skip only the length of header in shmem area i.e 4 bytes */
	xfer->rx.len = min_t(size_t, max_len, len > 4 ? len - 4 : 0);

	/* Take a copy to the rx buffer.. */
	copy_fromio(xfer->rx.buf, shmem->msg_payload, xfer->rx.len);
}

static void shmem_clear_channel(struct scmi_shared_mem __iomem *shmem)
{
	iowrite32(SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE, &shmem->channel_status);
}

static bool shmem_poll_done(struct scmi_shared_mem __iomem *shmem,
			    struct scmi_xfer *xfer)
{
	u16 xfer_id;

	xfer_id = MSG_XTRACT_TOKEN(ioread32(&shmem->msg_header));

	if (xfer->hdr.seq != xfer_id)
		return false;

	return ioread32(&shmem->channel_status) &
		(SCMI_SHMEM_CHAN_STAT_CHANNEL_ERROR |
		 SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE);
}

static bool shmem_channel_free(struct scmi_shared_mem __iomem *shmem)
{
	return (ioread32(&shmem->channel_status) &
			SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE);
}

static bool shmem_channel_intr_enabled(struct scmi_shared_mem __iomem *shmem)
{
	return ioread32(&shmem->flags) & SCMI_SHMEM_FLAG_INTR_ENABLED;
}

static void __iomem *shmem_setup_iomap(struct scmi_chan_info *cinfo,
				       struct device *dev, bool tx,
				       struct resource *res,
				       struct scmi_shmem_io_ops **ops)
{
	struct device_node *shmem __free(device_node);
	const char *desc = tx ? "Tx" : "Rx";
	int ret, idx = tx ? 0 : 1;
	struct device *cdev = cinfo->dev;
	struct resource lres = {};
	resource_size_t size;
	void __iomem *addr;
	u32 reg_io_width;

	shmem = of_parse_phandle(cdev->of_node, "shmem", idx);
	if (!shmem)
		return IOMEM_ERR_PTR(-ENODEV);

	if (!of_device_is_compatible(shmem, "arm,scmi-shmem"))
		return IOMEM_ERR_PTR(-ENXIO);

	/* Use a local on-stack as a working area when not provided */
	if (!res)
		res = &lres;

	ret = of_address_to_resource(shmem, 0, res);
	if (ret) {
		dev_err(cdev, "failed to get SCMI %s shared memory\n", desc);
		return IOMEM_ERR_PTR(ret);
	}

	size = resource_size(res);
	if (cinfo->max_msg_size + SCMI_SHMEM_LAYOUT_OVERHEAD > size) {
		dev_err(dev, "misconfigured SCMI shared memory\n");
		return IOMEM_ERR_PTR(-ENOSPC);
	}

	addr = devm_ioremap(dev, res->start, size);
	if (!addr) {
		dev_err(dev, "failed to ioremap SCMI %s shared memory\n", desc);
		return IOMEM_ERR_PTR(-EADDRNOTAVAIL);
	}

	of_property_read_u32(shmem, "reg-io-width", &reg_io_width);
	switch (reg_io_width) {
	case 4:
		*ops = &shmem_io_ops32;
		break;
	default:
		*ops = &shmem_io_ops_default;
		break;
	}

	return addr;
}

static const struct scmi_shared_mem_operations scmi_shmem_ops = {
	.tx_prepare = shmem_tx_prepare,
	.read_header = shmem_read_header,
	.fetch_response = shmem_fetch_response,
	.fetch_notification = shmem_fetch_notification,
	.clear_channel = shmem_clear_channel,
	.poll_done = shmem_poll_done,
	.channel_free = shmem_channel_free,
	.channel_intr_enabled = shmem_channel_intr_enabled,
	.setup_iomap = shmem_setup_iomap,
};

const struct scmi_shared_mem_operations *scmi_shared_mem_operations_get(void)
{
	return &scmi_shmem_ops;
}

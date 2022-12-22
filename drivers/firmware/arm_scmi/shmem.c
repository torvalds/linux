// SPDX-License-Identifier: GPL-2.0
/*
 * For transport using shared mem structure.
 *
 * Copyright (C) 2019 ARM Ltd.
 */

#include <linux/io.h>
#include <linux/processor.h>
#include <linux/types.h>

#include "common.h"

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

void shmem_tx_prepare(struct scmi_shared_mem __iomem *shmem,
		      struct scmi_xfer *xfer)
{
	/*
	 * Ideally channel must be free by now unless OS timeout last
	 * request and platform continued to process the same, wait
	 * until it releases the shared memory, otherwise we may endup
	 * overwriting its response with new message payload or vice-versa
	 */
	spin_until_cond(ioread32(&shmem->channel_status) &
			SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE);
	/* Mark channel busy + clear error */
	iowrite32(0x0, &shmem->channel_status);
	iowrite32(xfer->hdr.poll_completion ? 0 : SCMI_SHMEM_FLAG_INTR_ENABLED,
		  &shmem->flags);
	iowrite32(sizeof(shmem->msg_header) + xfer->tx.len, &shmem->length);
	iowrite32(pack_scmi_header(&xfer->hdr), &shmem->msg_header);
	if (xfer->tx.buf)
		memcpy_toio(shmem->msg_payload, xfer->tx.buf, xfer->tx.len);
}

u32 shmem_read_header(struct scmi_shared_mem __iomem *shmem)
{
	return ioread32(&shmem->msg_header);
}

void shmem_fetch_response(struct scmi_shared_mem __iomem *shmem,
			  struct scmi_xfer *xfer)
{
	size_t len = ioread32(&shmem->length);

	xfer->hdr.status = ioread32(shmem->msg_payload);
	/* Skip the length of header and status in shmem area i.e 8 bytes */
	xfer->rx.len = min_t(size_t, xfer->rx.len, len > 8 ? len - 8 : 0);

	/* Take a copy to the rx buffer.. */
	memcpy_fromio(xfer->rx.buf, shmem->msg_payload + 4, xfer->rx.len);
}

void shmem_fetch_notification(struct scmi_shared_mem __iomem *shmem,
			      size_t max_len, struct scmi_xfer *xfer)
{
	/* Skip only the length of header in shmem area i.e 4 bytes */
	xfer->rx.len = min_t(size_t, max_len, ioread32(&shmem->length) - 4);

	/* Take a copy to the rx buffer.. */
	memcpy_fromio(xfer->rx.buf, shmem->msg_payload, xfer->rx.len);
}

void shmem_clear_channel(struct scmi_shared_mem __iomem *shmem)
{
	iowrite32(SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE, &shmem->channel_status);
}

bool shmem_poll_done(struct scmi_shared_mem __iomem *shmem,
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

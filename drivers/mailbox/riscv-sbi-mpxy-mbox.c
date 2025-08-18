// SPDX-License-Identifier: GPL-2.0
/*
 * RISC-V SBI Message Proxy (MPXY) mailbox controller driver
 *
 * Copyright (C) 2025 Ventana Micro Systems Inc.
 */

#include <linux/acpi.h>
#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/irqchip/riscv-imsic.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox/riscv-rpmi-message.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_irq.h>
#include <linux/percpu.h>
#include <linux/platform_device.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/sbi.h>

/* ====== SBI MPXY extension data structures ====== */

/* SBI MPXY MSI related channel attributes */
struct sbi_mpxy_msi_info {
	/* Lower 32-bits of the MSI target address */
	u32 msi_addr_lo;
	/* Upper 32-bits of the MSI target address */
	u32 msi_addr_hi;
	/* MSI data value */
	u32 msi_data;
};

/*
 * SBI MPXY standard channel attributes.
 *
 * NOTE: The sequence of attribute fields are as-per the
 * defined sequence in the attribute table in spec (or
 * as-per the enum sbi_mpxy_attribute_id).
 */
struct sbi_mpxy_channel_attrs {
	/* Message protocol ID */
	u32 msg_proto_id;
	/* Message protocol version */
	u32 msg_proto_version;
	/* Message protocol maximum message length */
	u32 msg_max_len;
	/* Message protocol message send timeout in microseconds */
	u32 msg_send_timeout;
	/* Message protocol message completion timeout in microseconds */
	u32 msg_completion_timeout;
	/* Bit array for channel capabilities */
	u32 capability;
	/* SSE event ID */
	u32 sse_event_id;
	/* MSI enable/disable control knob */
	u32 msi_control;
	/* Channel MSI info */
	struct sbi_mpxy_msi_info msi_info;
	/* Events state control */
	u32 events_state_ctrl;
};

/*
 * RPMI specific SBI MPXY channel attributes.
 *
 * NOTE: The sequence of attribute fields are as-per the
 * defined sequence in the attribute table in spec (or
 * as-per the enum sbi_mpxy_rpmi_attribute_id).
 */
struct sbi_mpxy_rpmi_channel_attrs {
	/* RPMI service group ID */
	u32 servicegroup_id;
	/* RPMI service group version */
	u32 servicegroup_version;
	/* RPMI implementation ID */
	u32 impl_id;
	/* RPMI implementation version */
	u32 impl_version;
};

/* SBI MPXY channel IDs data in shared memory */
struct sbi_mpxy_channel_ids_data {
	/* Remaining number of channel ids */
	__le32 remaining;
	/* Returned channel ids in current function call */
	__le32 returned;
	/* Returned channel id array */
	__le32 channel_array[];
};

/* SBI MPXY notification data in shared memory */
struct sbi_mpxy_notification_data {
	/* Remaining number of notification events */
	__le32 remaining;
	/* Number of notification events returned */
	__le32 returned;
	/* Number of notification events lost */
	__le32 lost;
	/* Reserved for future use */
	__le32 reserved;
	/* Returned channel id array */
	u8 events_data[];
};

/* ====== MPXY data structures & helper routines ====== */

/* MPXY Per-CPU or local context */
struct mpxy_local {
	/* Shared memory base address */
	void *shmem;
	/* Shared memory physical address */
	phys_addr_t shmem_phys_addr;
	/* Flag representing whether shared memory is active or not */
	bool shmem_active;
};

static DEFINE_PER_CPU(struct mpxy_local, mpxy_local);
static unsigned long mpxy_shmem_size;
static bool mpxy_shmem_init_done;

static int mpxy_get_channel_count(u32 *channel_count)
{
	struct mpxy_local *mpxy = this_cpu_ptr(&mpxy_local);
	struct sbi_mpxy_channel_ids_data *sdata = mpxy->shmem;
	u32 remaining, returned;
	struct sbiret sret;

	if (!mpxy->shmem_active)
		return -ENODEV;
	if (!channel_count)
		return -EINVAL;

	get_cpu();

	/* Get the remaining and returned fields to calculate total */
	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_GET_CHANNEL_IDS,
			 0, 0, 0, 0, 0, 0);
	if (sret.error)
		goto err_put_cpu;

	remaining = le32_to_cpu(sdata->remaining);
	returned = le32_to_cpu(sdata->returned);
	*channel_count = remaining + returned;

err_put_cpu:
	put_cpu();
	return sbi_err_map_linux_errno(sret.error);
}

static int mpxy_get_channel_ids(u32 channel_count, u32 *channel_ids)
{
	struct mpxy_local *mpxy = this_cpu_ptr(&mpxy_local);
	struct sbi_mpxy_channel_ids_data *sdata = mpxy->shmem;
	u32 remaining, returned, count, start_index = 0;
	struct sbiret sret;

	if (!mpxy->shmem_active)
		return -ENODEV;
	if (!channel_count || !channel_ids)
		return -EINVAL;

	get_cpu();

	do {
		sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_GET_CHANNEL_IDS,
				 start_index, 0, 0, 0, 0, 0);
		if (sret.error)
			goto err_put_cpu;

		remaining = le32_to_cpu(sdata->remaining);
		returned = le32_to_cpu(sdata->returned);

		count = returned < (channel_count - start_index) ?
			returned : (channel_count - start_index);
		memcpy_from_le32(&channel_ids[start_index], sdata->channel_array, count);
		start_index += count;
	} while (remaining && start_index < channel_count);

err_put_cpu:
	put_cpu();
	return sbi_err_map_linux_errno(sret.error);
}

static int mpxy_read_attrs(u32 channel_id, u32 base_attrid, u32 attr_count,
			   u32 *attrs_buf)
{
	struct mpxy_local *mpxy = this_cpu_ptr(&mpxy_local);
	struct sbiret sret;

	if (!mpxy->shmem_active)
		return -ENODEV;
	if (!attr_count || !attrs_buf)
		return -EINVAL;

	get_cpu();

	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_READ_ATTRS,
			 channel_id, base_attrid, attr_count, 0, 0, 0);
	if (sret.error)
		goto err_put_cpu;

	memcpy_from_le32(attrs_buf, (__le32 *)mpxy->shmem, attr_count);

err_put_cpu:
	put_cpu();
	return sbi_err_map_linux_errno(sret.error);
}

static int mpxy_write_attrs(u32 channel_id, u32 base_attrid, u32 attr_count,
			    u32 *attrs_buf)
{
	struct mpxy_local *mpxy = this_cpu_ptr(&mpxy_local);
	struct sbiret sret;

	if (!mpxy->shmem_active)
		return -ENODEV;
	if (!attr_count || !attrs_buf)
		return -EINVAL;

	get_cpu();

	memcpy_to_le32((__le32 *)mpxy->shmem, attrs_buf, attr_count);
	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_WRITE_ATTRS,
			 channel_id, base_attrid, attr_count, 0, 0, 0);

	put_cpu();
	return sbi_err_map_linux_errno(sret.error);
}

static int mpxy_send_message_with_resp(u32 channel_id, u32 msg_id,
				       void *tx, unsigned long tx_len,
				       void *rx, unsigned long max_rx_len,
				       unsigned long *rx_len)
{
	struct mpxy_local *mpxy = this_cpu_ptr(&mpxy_local);
	unsigned long rx_bytes;
	struct sbiret sret;

	if (!mpxy->shmem_active)
		return -ENODEV;
	if (!tx && tx_len)
		return -EINVAL;

	get_cpu();

	/* Message protocols allowed to have no data in messages */
	if (tx_len)
		memcpy(mpxy->shmem, tx, tx_len);

	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_SEND_MSG_WITH_RESP,
			 channel_id, msg_id, tx_len, 0, 0, 0);
	if (rx && !sret.error) {
		rx_bytes = sret.value;
		if (rx_bytes > max_rx_len) {
			put_cpu();
			return -ENOSPC;
		}

		memcpy(rx, mpxy->shmem, rx_bytes);
		if (rx_len)
			*rx_len = rx_bytes;
	}

	put_cpu();
	return sbi_err_map_linux_errno(sret.error);
}

static int mpxy_send_message_without_resp(u32 channel_id, u32 msg_id,
					  void *tx, unsigned long tx_len)
{
	struct mpxy_local *mpxy = this_cpu_ptr(&mpxy_local);
	struct sbiret sret;

	if (!mpxy->shmem_active)
		return -ENODEV;
	if (!tx && tx_len)
		return -EINVAL;

	get_cpu();

	/* Message protocols allowed to have no data in messages */
	if (tx_len)
		memcpy(mpxy->shmem, tx, tx_len);

	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_SEND_MSG_WITHOUT_RESP,
			 channel_id, msg_id, tx_len, 0, 0, 0);

	put_cpu();
	return sbi_err_map_linux_errno(sret.error);
}

static int mpxy_get_notifications(u32 channel_id,
				  struct sbi_mpxy_notification_data *notif_data,
				  unsigned long *events_data_len)
{
	struct mpxy_local *mpxy = this_cpu_ptr(&mpxy_local);
	struct sbiret sret;

	if (!mpxy->shmem_active)
		return -ENODEV;
	if (!notif_data || !events_data_len)
		return -EINVAL;

	get_cpu();

	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_GET_NOTIFICATION_EVENTS,
			 channel_id, 0, 0, 0, 0, 0);
	if (sret.error)
		goto err_put_cpu;

	memcpy(notif_data, mpxy->shmem, sret.value + 16);
	*events_data_len = sret.value;

err_put_cpu:
	put_cpu();
	return sbi_err_map_linux_errno(sret.error);
}

static int mpxy_get_shmem_size(unsigned long *shmem_size)
{
	struct sbiret sret;

	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_GET_SHMEM_SIZE,
			 0, 0, 0, 0, 0, 0);
	if (sret.error)
		return sbi_err_map_linux_errno(sret.error);
	if (shmem_size)
		*shmem_size = sret.value;
	return 0;
}

static int mpxy_setup_shmem(unsigned int cpu)
{
	struct page *shmem_page;
	struct mpxy_local *mpxy;
	struct sbiret sret;

	mpxy = per_cpu_ptr(&mpxy_local, cpu);
	if (mpxy->shmem_active)
		return 0;

	shmem_page = alloc_pages(GFP_KERNEL | __GFP_ZERO, get_order(mpxy_shmem_size));
	if (!shmem_page)
		return -ENOMEM;

	/*
	 * Linux setup of shmem is done in mpxy OVERWRITE mode.
	 * flags[1:0] = 00b
	 */
	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_SET_SHMEM,
			 page_to_phys(shmem_page), 0, 0, 0, 0, 0);
	if (sret.error) {
		free_pages((unsigned long)page_to_virt(shmem_page),
			   get_order(mpxy_shmem_size));
		return sbi_err_map_linux_errno(sret.error);
	}

	mpxy->shmem = page_to_virt(shmem_page);
	mpxy->shmem_phys_addr = page_to_phys(shmem_page);
	mpxy->shmem_active = true;

	return 0;
}

/* ====== MPXY mailbox data structures ====== */

/* MPXY mailbox channel */
struct mpxy_mbox_channel {
	struct mpxy_mbox *mbox;
	u32 channel_id;
	struct sbi_mpxy_channel_attrs attrs;
	struct sbi_mpxy_rpmi_channel_attrs rpmi_attrs;
	struct sbi_mpxy_notification_data *notif;
	u32 max_xfer_len;
	bool have_events_state;
	u32 msi_index;
	u32 msi_irq;
	bool started;
};

/* MPXY mailbox */
struct mpxy_mbox {
	struct device *dev;
	u32 channel_count;
	struct mpxy_mbox_channel *channels;
	u32 msi_count;
	struct mpxy_mbox_channel **msi_index_to_channel;
	struct mbox_controller controller;
};

/* ====== MPXY RPMI processing ====== */

static void mpxy_mbox_send_rpmi_data(struct mpxy_mbox_channel *mchan,
				     struct rpmi_mbox_message *msg)
{
	msg->error = 0;
	switch (msg->type) {
	case RPMI_MBOX_MSG_TYPE_GET_ATTRIBUTE:
		switch (msg->attr.id) {
		case RPMI_MBOX_ATTR_SPEC_VERSION:
			msg->attr.value = mchan->attrs.msg_proto_version;
			break;
		case RPMI_MBOX_ATTR_MAX_MSG_DATA_SIZE:
			msg->attr.value = mchan->max_xfer_len;
			break;
		case RPMI_MBOX_ATTR_SERVICEGROUP_ID:
			msg->attr.value = mchan->rpmi_attrs.servicegroup_id;
			break;
		case RPMI_MBOX_ATTR_SERVICEGROUP_VERSION:
			msg->attr.value = mchan->rpmi_attrs.servicegroup_version;
			break;
		case RPMI_MBOX_ATTR_IMPL_ID:
			msg->attr.value = mchan->rpmi_attrs.impl_id;
			break;
		case RPMI_MBOX_ATTR_IMPL_VERSION:
			msg->attr.value = mchan->rpmi_attrs.impl_version;
			break;
		default:
			msg->error = -EOPNOTSUPP;
			break;
		}
		break;
	case RPMI_MBOX_MSG_TYPE_SET_ATTRIBUTE:
		/* None of the RPMI linux mailbox attributes are writeable */
		msg->error = -EOPNOTSUPP;
		break;
	case RPMI_MBOX_MSG_TYPE_SEND_WITH_RESPONSE:
		if ((!msg->data.request && msg->data.request_len) ||
		    (msg->data.request && msg->data.request_len > mchan->max_xfer_len) ||
		    (!msg->data.response && msg->data.max_response_len)) {
			msg->error = -EINVAL;
			break;
		}
		if (!(mchan->attrs.capability & SBI_MPXY_CHAN_CAP_SEND_WITH_RESP)) {
			msg->error = -EIO;
			break;
		}
		msg->error = mpxy_send_message_with_resp(mchan->channel_id,
							 msg->data.service_id,
							 msg->data.request,
							 msg->data.request_len,
							 msg->data.response,
							 msg->data.max_response_len,
							 &msg->data.out_response_len);
		break;
	case RPMI_MBOX_MSG_TYPE_SEND_WITHOUT_RESPONSE:
		if ((!msg->data.request && msg->data.request_len) ||
		    (msg->data.request && msg->data.request_len > mchan->max_xfer_len)) {
			msg->error = -EINVAL;
			break;
		}
		if (!(mchan->attrs.capability & SBI_MPXY_CHAN_CAP_SEND_WITHOUT_RESP)) {
			msg->error = -EIO;
			break;
		}
		msg->error = mpxy_send_message_without_resp(mchan->channel_id,
							    msg->data.service_id,
							    msg->data.request,
							    msg->data.request_len);
		break;
	default:
		msg->error = -EOPNOTSUPP;
		break;
	}
}

static void mpxy_mbox_peek_rpmi_data(struct mbox_chan *chan,
				     struct mpxy_mbox_channel *mchan,
				     struct sbi_mpxy_notification_data *notif,
				     unsigned long events_data_len)
{
	struct rpmi_notification_event *event;
	struct rpmi_mbox_message msg;
	unsigned long pos = 0;

	while (pos < events_data_len && (events_data_len - pos) <= sizeof(*event)) {
		event = (struct rpmi_notification_event *)(notif->events_data + pos);

		msg.type = RPMI_MBOX_MSG_TYPE_NOTIFICATION_EVENT;
		msg.notif.event_datalen = le16_to_cpu(event->event_datalen);
		msg.notif.event_id = event->event_id;
		msg.notif.event_data = event->event_data;
		msg.error = 0;

		mbox_chan_received_data(chan, &msg);
		pos += sizeof(*event) + msg.notif.event_datalen;
	}
}

static int mpxy_mbox_read_rpmi_attrs(struct mpxy_mbox_channel *mchan)
{
	return mpxy_read_attrs(mchan->channel_id,
			       SBI_MPXY_ATTR_MSGPROTO_ATTR_START,
			       sizeof(mchan->rpmi_attrs) / sizeof(u32),
			       (u32 *)&mchan->rpmi_attrs);
}

/* ====== MPXY mailbox callbacks ====== */

static int mpxy_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct mpxy_mbox_channel *mchan = chan->con_priv;

	if (mchan->attrs.msg_proto_id == SBI_MPXY_MSGPROTO_RPMI_ID) {
		mpxy_mbox_send_rpmi_data(mchan, data);
		return 0;
	}

	return -EOPNOTSUPP;
}

static bool mpxy_mbox_peek_data(struct mbox_chan *chan)
{
	struct mpxy_mbox_channel *mchan = chan->con_priv;
	struct sbi_mpxy_notification_data *notif = mchan->notif;
	bool have_notifications = false;
	unsigned long data_len;
	int rc;

	if (!(mchan->attrs.capability & SBI_MPXY_CHAN_CAP_GET_NOTIFICATIONS))
		return false;

	do {
		rc = mpxy_get_notifications(mchan->channel_id, notif, &data_len);
		if (rc || !data_len)
			break;

		if (mchan->attrs.msg_proto_id == SBI_MPXY_MSGPROTO_RPMI_ID)
			mpxy_mbox_peek_rpmi_data(chan, mchan, notif, data_len);

		have_notifications = true;
	} while (1);

	return have_notifications;
}

static irqreturn_t mpxy_mbox_irq_thread(int irq, void *dev_id)
{
	mpxy_mbox_peek_data(dev_id);
	return IRQ_HANDLED;
}

static int mpxy_mbox_setup_msi(struct mbox_chan *chan,
			       struct mpxy_mbox_channel *mchan)
{
	struct device *dev = mchan->mbox->dev;
	int rc;

	/* Do nothing if MSI not supported */
	if (mchan->msi_irq == U32_MAX)
		return 0;

	/* Fail if MSI already enabled */
	if (mchan->attrs.msi_control)
		return -EALREADY;

	/* Request channel MSI handler */
	rc = request_threaded_irq(mchan->msi_irq, NULL, mpxy_mbox_irq_thread,
				  0, dev_name(dev), chan);
	if (rc) {
		dev_err(dev, "failed to request MPXY channel 0x%x IRQ\n",
			mchan->channel_id);
		return rc;
	}

	/* Enable channel MSI control */
	mchan->attrs.msi_control = 1;
	rc = mpxy_write_attrs(mchan->channel_id, SBI_MPXY_ATTR_MSI_CONTROL,
			      1, &mchan->attrs.msi_control);
	if (rc) {
		dev_err(dev, "enable MSI control failed for MPXY channel 0x%x\n",
			mchan->channel_id);
		mchan->attrs.msi_control = 0;
		free_irq(mchan->msi_irq, chan);
		return rc;
	}

	return 0;
}

static void mpxy_mbox_cleanup_msi(struct mbox_chan *chan,
				  struct mpxy_mbox_channel *mchan)
{
	struct device *dev = mchan->mbox->dev;
	int rc;

	/* Do nothing if MSI not supported */
	if (mchan->msi_irq == U32_MAX)
		return;

	/* Do nothing if MSI already disabled */
	if (!mchan->attrs.msi_control)
		return;

	/* Disable channel MSI control */
	mchan->attrs.msi_control = 0;
	rc = mpxy_write_attrs(mchan->channel_id, SBI_MPXY_ATTR_MSI_CONTROL,
			      1, &mchan->attrs.msi_control);
	if (rc) {
		dev_err(dev, "disable MSI control failed for MPXY channel 0x%x\n",
			mchan->channel_id);
	}

	/* Free channel MSI handler */
	free_irq(mchan->msi_irq, chan);
}

static int mpxy_mbox_setup_events(struct mpxy_mbox_channel *mchan)
{
	struct device *dev = mchan->mbox->dev;
	int rc;

	/* Do nothing if events state not supported */
	if (!mchan->have_events_state)
		return 0;

	/* Fail if events state already enabled */
	if (mchan->attrs.events_state_ctrl)
		return -EALREADY;

	/* Enable channel events state */
	mchan->attrs.events_state_ctrl = 1;
	rc = mpxy_write_attrs(mchan->channel_id, SBI_MPXY_ATTR_EVENTS_STATE_CONTROL,
			      1, &mchan->attrs.events_state_ctrl);
	if (rc) {
		dev_err(dev, "enable events state failed for MPXY channel 0x%x\n",
			mchan->channel_id);
		mchan->attrs.events_state_ctrl = 0;
		return rc;
	}

	return 0;
}

static void mpxy_mbox_cleanup_events(struct mpxy_mbox_channel *mchan)
{
	struct device *dev = mchan->mbox->dev;
	int rc;

	/* Do nothing if events state not supported */
	if (!mchan->have_events_state)
		return;

	/* Do nothing if events state already disabled */
	if (!mchan->attrs.events_state_ctrl)
		return;

	/* Disable channel events state */
	mchan->attrs.events_state_ctrl = 0;
	rc = mpxy_write_attrs(mchan->channel_id, SBI_MPXY_ATTR_EVENTS_STATE_CONTROL,
			      1, &mchan->attrs.events_state_ctrl);
	if (rc)
		dev_err(dev, "disable events state failed for MPXY channel 0x%x\n",
			mchan->channel_id);
}

static int mpxy_mbox_startup(struct mbox_chan *chan)
{
	struct mpxy_mbox_channel *mchan = chan->con_priv;
	int rc;

	if (mchan->started)
		return -EALREADY;

	/* Setup channel MSI */
	rc = mpxy_mbox_setup_msi(chan, mchan);
	if (rc)
		return rc;

	/* Setup channel notification events */
	rc = mpxy_mbox_setup_events(mchan);
	if (rc) {
		mpxy_mbox_cleanup_msi(chan, mchan);
		return rc;
	}

	/* Mark the channel as started */
	mchan->started = true;

	return 0;
}

static void mpxy_mbox_shutdown(struct mbox_chan *chan)
{
	struct mpxy_mbox_channel *mchan = chan->con_priv;

	if (!mchan->started)
		return;

	/* Mark the channel as stopped */
	mchan->started = false;

	/* Cleanup channel notification events */
	mpxy_mbox_cleanup_events(mchan);

	/* Cleanup channel MSI */
	mpxy_mbox_cleanup_msi(chan, mchan);
}

static const struct mbox_chan_ops mpxy_mbox_ops = {
	.send_data = mpxy_mbox_send_data,
	.peek_data = mpxy_mbox_peek_data,
	.startup = mpxy_mbox_startup,
	.shutdown = mpxy_mbox_shutdown,
};

/* ====== MPXY platform driver ===== */

static void mpxy_mbox_msi_write(struct msi_desc *desc, struct msi_msg *msg)
{
	struct device *dev = msi_desc_to_dev(desc);
	struct mpxy_mbox *mbox = dev_get_drvdata(dev);
	struct mpxy_mbox_channel *mchan;
	struct sbi_mpxy_msi_info *minfo;
	int rc;

	mchan = mbox->msi_index_to_channel[desc->msi_index];
	if (!mchan) {
		dev_warn(dev, "MPXY channel not available for MSI index %d\n",
			 desc->msi_index);
		return;
	}

	minfo = &mchan->attrs.msi_info;
	minfo->msi_addr_lo = msg->address_lo;
	minfo->msi_addr_hi = msg->address_hi;
	minfo->msi_data = msg->data;

	rc = mpxy_write_attrs(mchan->channel_id, SBI_MPXY_ATTR_MSI_ADDR_LO,
			      sizeof(*minfo) / sizeof(u32), (u32 *)minfo);
	if (rc) {
		dev_warn(dev, "failed to write MSI info for MPXY channel 0x%x\n",
			 mchan->channel_id);
	}
}

static struct mbox_chan *mpxy_mbox_fw_xlate(struct mbox_controller *ctlr,
					    const struct fwnode_reference_args *pa)
{
	struct mpxy_mbox *mbox = container_of(ctlr, struct mpxy_mbox, controller);
	struct mpxy_mbox_channel *mchan;
	u32 i;

	if (pa->nargs != 2)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < mbox->channel_count; i++) {
		mchan = &mbox->channels[i];
		if (mchan->channel_id == pa->args[0] &&
		    mchan->attrs.msg_proto_id == pa->args[1])
			return &mbox->controller.chans[i];
	}

	return ERR_PTR(-ENOENT);
}

static int mpxy_mbox_populate_channels(struct mpxy_mbox *mbox)
{
	u32 i, *channel_ids __free(kfree) = NULL;
	struct mpxy_mbox_channel *mchan;
	int rc;

	/* Find-out of number of channels */
	rc = mpxy_get_channel_count(&mbox->channel_count);
	if (rc)
		return dev_err_probe(mbox->dev, rc, "failed to get number of MPXY channels\n");
	if (!mbox->channel_count)
		return dev_err_probe(mbox->dev, -ENODEV, "no MPXY channels available\n");

	/* Allocate and fetch all channel IDs */
	channel_ids = kcalloc(mbox->channel_count, sizeof(*channel_ids), GFP_KERNEL);
	if (!channel_ids)
		return -ENOMEM;
	rc = mpxy_get_channel_ids(mbox->channel_count, channel_ids);
	if (rc)
		return dev_err_probe(mbox->dev, rc, "failed to get MPXY channel IDs\n");

	/* Populate all channels */
	mbox->channels = devm_kcalloc(mbox->dev, mbox->channel_count,
				      sizeof(*mbox->channels), GFP_KERNEL);
	if (!mbox->channels)
		return -ENOMEM;
	for (i = 0; i < mbox->channel_count; i++) {
		mchan = &mbox->channels[i];
		mchan->mbox = mbox;
		mchan->channel_id = channel_ids[i];

		rc = mpxy_read_attrs(mchan->channel_id, SBI_MPXY_ATTR_MSG_PROT_ID,
				     sizeof(mchan->attrs) / sizeof(u32),
				     (u32 *)&mchan->attrs);
		if (rc) {
			return dev_err_probe(mbox->dev, rc,
					     "MPXY channel 0x%x read attrs failed\n",
					     mchan->channel_id);
		}

		if (mchan->attrs.msg_proto_id == SBI_MPXY_MSGPROTO_RPMI_ID) {
			rc = mpxy_mbox_read_rpmi_attrs(mchan);
			if (rc) {
				return dev_err_probe(mbox->dev, rc,
						     "MPXY channel 0x%x read RPMI attrs failed\n",
						     mchan->channel_id);
			}
		}

		mchan->notif = devm_kzalloc(mbox->dev, mpxy_shmem_size, GFP_KERNEL);
		if (!mchan->notif)
			return -ENOMEM;

		mchan->max_xfer_len = min(mpxy_shmem_size, mchan->attrs.msg_max_len);

		if ((mchan->attrs.capability & SBI_MPXY_CHAN_CAP_GET_NOTIFICATIONS) &&
		    (mchan->attrs.capability & SBI_MPXY_CHAN_CAP_EVENTS_STATE))
			mchan->have_events_state = true;

		if ((mchan->attrs.capability & SBI_MPXY_CHAN_CAP_GET_NOTIFICATIONS) &&
		    (mchan->attrs.capability & SBI_MPXY_CHAN_CAP_MSI))
			mchan->msi_index = mbox->msi_count++;
		else
			mchan->msi_index = U32_MAX;
		mchan->msi_irq = U32_MAX;
	}

	return 0;
}

static int mpxy_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpxy_mbox_channel *mchan;
	struct mpxy_mbox *mbox;
	int msi_idx, rc;
	u32 i;

	/*
	 * Initialize MPXY shared memory only once. This also ensures
	 * that SBI MPXY mailbox is probed only once.
	 */
	if (mpxy_shmem_init_done) {
		dev_err(dev, "SBI MPXY mailbox already initialized\n");
		return -EALREADY;
	}

	/* Probe for SBI MPXY extension */
	if (sbi_spec_version < sbi_mk_version(1, 0) ||
	    sbi_probe_extension(SBI_EXT_MPXY) <= 0) {
		dev_info(dev, "SBI MPXY extension not available\n");
		return -ENODEV;
	}

	/* Find-out shared memory size */
	rc = mpxy_get_shmem_size(&mpxy_shmem_size);
	if (rc)
		return dev_err_probe(dev, rc, "failed to get MPXY shared memory size\n");

	/*
	 * Setup MPXY shared memory on each CPU
	 *
	 * Note: Don't cleanup MPXY shared memory upon CPU power-down
	 * because the RPMI System MSI irqchip driver needs it to be
	 * available when migrating IRQs in CPU power-down path.
	 */
	cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "riscv/sbi-mpxy-shmem",
			  mpxy_setup_shmem, NULL);

	/* Mark as MPXY shared memory initialization done */
	mpxy_shmem_init_done = true;

	/* Allocate mailbox instance */
	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;
	mbox->dev = dev;
	platform_set_drvdata(pdev, mbox);

	/* Populate mailbox channels */
	rc = mpxy_mbox_populate_channels(mbox);
	if (rc)
		return rc;

	/* Initialize mailbox controller */
	mbox->controller.txdone_irq = false;
	mbox->controller.txdone_poll = false;
	mbox->controller.ops = &mpxy_mbox_ops;
	mbox->controller.dev = dev;
	mbox->controller.num_chans = mbox->channel_count;
	mbox->controller.fw_xlate = mpxy_mbox_fw_xlate;
	mbox->controller.chans = devm_kcalloc(dev, mbox->channel_count,
					      sizeof(*mbox->controller.chans),
					      GFP_KERNEL);
	if (!mbox->controller.chans)
		return -ENOMEM;
	for (i = 0; i < mbox->channel_count; i++)
		mbox->controller.chans[i].con_priv = &mbox->channels[i];

	/* Setup MSIs for mailbox (if required) */
	if (mbox->msi_count) {
		/*
		 * The device MSI domain for platform devices on RISC-V architecture
		 * is only available after the MSI controller driver is probed so,
		 * explicitly configure here.
		 */
		if (!dev_get_msi_domain(dev)) {
			struct fwnode_handle *fwnode = dev_fwnode(dev);

			/*
			 * The device MSI domain for OF devices is only set at the
			 * time of populating/creating OF device. If the device MSI
			 * domain is discovered later after the OF device is created
			 * then we need to set it explicitly before using any platform
			 * MSI functions.
			 */
			if (is_of_node(fwnode)) {
				of_msi_configure(dev, dev_of_node(dev));
			} else if (is_acpi_device_node(fwnode)) {
				struct irq_domain *msi_domain;

				msi_domain = irq_find_matching_fwnode(imsic_acpi_get_fwnode(dev),
								      DOMAIN_BUS_PLATFORM_MSI);
				dev_set_msi_domain(dev, msi_domain);
			}

			if (!dev_get_msi_domain(dev))
				return -EPROBE_DEFER;
		}

		mbox->msi_index_to_channel = devm_kcalloc(dev, mbox->msi_count,
							  sizeof(*mbox->msi_index_to_channel),
							  GFP_KERNEL);
		if (!mbox->msi_index_to_channel)
			return -ENOMEM;

		for (msi_idx = 0; msi_idx < mbox->msi_count; msi_idx++) {
			for (i = 0; i < mbox->channel_count; i++) {
				mchan = &mbox->channels[i];
				if (mchan->msi_index == msi_idx) {
					mbox->msi_index_to_channel[msi_idx] = mchan;
					break;
				}
			}
		}

		rc = platform_device_msi_init_and_alloc_irqs(dev, mbox->msi_count,
							     mpxy_mbox_msi_write);
		if (rc) {
			return dev_err_probe(dev, rc, "Failed to allocate %d MSIs\n",
					     mbox->msi_count);
		}

		for (i = 0; i < mbox->channel_count; i++) {
			mchan = &mbox->channels[i];
			if (mchan->msi_index == U32_MAX)
				continue;
			mchan->msi_irq = msi_get_virq(dev, mchan->msi_index);
		}
	}

	/* Register mailbox controller */
	rc = devm_mbox_controller_register(dev, &mbox->controller);
	if (rc) {
		dev_err_probe(dev, rc, "Registering SBI MPXY mailbox failed\n");
		if (mbox->msi_count)
			platform_device_msi_free_irqs_all(dev);
		return rc;
	}

#ifdef CONFIG_ACPI
	struct acpi_device *adev = ACPI_COMPANION(dev);

	if (adev)
		acpi_dev_clear_dependencies(adev);
#endif

	dev_info(dev, "mailbox registered with %d channels\n",
		 mbox->channel_count);
	return 0;
}

static void mpxy_mbox_remove(struct platform_device *pdev)
{
	struct mpxy_mbox *mbox = platform_get_drvdata(pdev);

	if (mbox->msi_count)
		platform_device_msi_free_irqs_all(mbox->dev);
}

static const struct of_device_id mpxy_mbox_of_match[] = {
	{ .compatible = "riscv,sbi-mpxy-mbox" },
	{}
};
MODULE_DEVICE_TABLE(of, mpxy_mbox_of_match);

static const struct acpi_device_id mpxy_mbox_acpi_match[] = {
	{ "RSCV0005" },
	{}
};
MODULE_DEVICE_TABLE(acpi, mpxy_mbox_acpi_match);

static struct platform_driver mpxy_mbox_driver = {
	.driver = {
		.name = "riscv-sbi-mpxy-mbox",
		.of_match_table = mpxy_mbox_of_match,
		.acpi_match_table = mpxy_mbox_acpi_match,
	},
	.probe = mpxy_mbox_probe,
	.remove = mpxy_mbox_remove,
};
module_platform_driver(mpxy_mbox_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anup Patel <apatel@ventanamicro.com>");
MODULE_DESCRIPTION("RISC-V SBI MPXY mailbox controller driver");

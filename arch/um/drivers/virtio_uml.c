// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Virtio vhost-user driver
 *
 * Copyright(c) 2019 Intel Corporation
 *
 * This module allows virtio devices to be used over a vhost-user socket.
 *
 * Guest devices can be instantiated by kernel module or command line
 * parameters. One device will be created for each parameter. Syntax:
 *
 *		[virtio_uml.]device=<socket>:<virtio_id>[:<platform_id>]
 * where:
 *		<socket>	:= vhost-user socket path to connect
 *		<virtio_id>	:= virtio device id (as in virtio_ids.h)
 *		<platform_id>	:= (optional) platform device id
 *
 * example:
 *		virtio_uml.device=/var/uml.socket:1
 *
 * Based on Virtio MMIO driver by Pawel Moll, copyright 2011-2014, ARM Ltd.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>
#include <shared/as-layout.h>
#include <irq_kern.h>
#include <init.h>
#include <os.h>
#include "vhost_user.h"

/* Workaround due to a conflict between irq_user.h and irqreturn.h */
#ifdef IRQ_NONE
#undef IRQ_NONE
#endif

#define MAX_SUPPORTED_QUEUE_SIZE	256

#define to_virtio_uml_device(_vdev) \
	container_of(_vdev, struct virtio_uml_device, vdev)

struct virtio_uml_device {
	struct virtio_device vdev;
	struct platform_device *pdev;

	int sock, req_fd;
	u64 features;
	u64 protocol_features;
	u8 status;
};

struct virtio_uml_vq_info {
	int kick_fd, call_fd;
	char name[32];
};

extern unsigned long long physmem_size, highmem;

#define vu_err(vu_dev, ...)	dev_err(&(vu_dev)->pdev->dev, __VA_ARGS__)

/* Vhost-user protocol */

static int full_sendmsg_fds(int fd, const void *buf, unsigned int len,
			    const int *fds, unsigned int fds_num)
{
	int rc;

	do {
		rc = os_sendmsg_fds(fd, buf, len, fds, fds_num);
		if (rc > 0) {
			buf += rc;
			len -= rc;
			fds = NULL;
			fds_num = 0;
		}
	} while (len && (rc >= 0 || rc == -EINTR));

	if (rc < 0)
		return rc;
	return 0;
}

static int full_read(int fd, void *buf, int len)
{
	int rc;

	do {
		rc = os_read_file(fd, buf, len);
		if (rc > 0) {
			buf += rc;
			len -= rc;
		}
	} while (len && (rc > 0 || rc == -EINTR));

	if (rc < 0)
		return rc;
	if (rc == 0)
		return -ECONNRESET;
	return 0;
}

static int vhost_user_recv_header(int fd, struct vhost_user_msg *msg)
{
	return full_read(fd, msg, sizeof(msg->header));
}

static int vhost_user_recv(int fd, struct vhost_user_msg *msg,
			   size_t max_payload_size)
{
	size_t size;
	int rc = vhost_user_recv_header(fd, msg);

	if (rc)
		return rc;
	size = msg->header.size;
	if (size > max_payload_size)
		return -EPROTO;
	return full_read(fd, &msg->payload, size);
}

static int vhost_user_recv_resp(struct virtio_uml_device *vu_dev,
				struct vhost_user_msg *msg,
				size_t max_payload_size)
{
	int rc = vhost_user_recv(vu_dev->sock, msg, max_payload_size);

	if (rc)
		return rc;

	if (msg->header.flags != (VHOST_USER_FLAG_REPLY | VHOST_USER_VERSION))
		return -EPROTO;

	return 0;
}

static int vhost_user_recv_u64(struct virtio_uml_device *vu_dev,
			       u64 *value)
{
	struct vhost_user_msg msg;
	int rc = vhost_user_recv_resp(vu_dev, &msg,
				      sizeof(msg.payload.integer));

	if (rc)
		return rc;
	if (msg.header.size != sizeof(msg.payload.integer))
		return -EPROTO;
	*value = msg.payload.integer;
	return 0;
}

static int vhost_user_recv_req(struct virtio_uml_device *vu_dev,
			       struct vhost_user_msg *msg,
			       size_t max_payload_size)
{
	int rc = vhost_user_recv(vu_dev->req_fd, msg, max_payload_size);

	if (rc)
		return rc;

	if ((msg->header.flags & ~VHOST_USER_FLAG_NEED_REPLY) !=
			VHOST_USER_VERSION)
		return -EPROTO;

	return 0;
}

static int vhost_user_send(struct virtio_uml_device *vu_dev,
			   bool need_response, struct vhost_user_msg *msg,
			   int *fds, size_t num_fds)
{
	size_t size = sizeof(msg->header) + msg->header.size;
	bool request_ack;
	int rc;

	msg->header.flags |= VHOST_USER_VERSION;

	/*
	 * The need_response flag indicates that we already need a response,
	 * e.g. to read the features. In these cases, don't request an ACK as
	 * it is meaningless. Also request an ACK only if supported.
	 */
	request_ack = !need_response;
	if (!(vu_dev->protocol_features &
			BIT_ULL(VHOST_USER_PROTOCOL_F_REPLY_ACK)))
		request_ack = false;

	if (request_ack)
		msg->header.flags |= VHOST_USER_FLAG_NEED_REPLY;

	rc = full_sendmsg_fds(vu_dev->sock, msg, size, fds, num_fds);
	if (rc < 0)
		return rc;

	if (request_ack) {
		uint64_t status;

		rc = vhost_user_recv_u64(vu_dev, &status);
		if (rc)
			return rc;

		if (status) {
			vu_err(vu_dev, "slave reports error: %llu\n", status);
			return -EIO;
		}
	}

	return 0;
}

static int vhost_user_send_no_payload(struct virtio_uml_device *vu_dev,
				      bool need_response, u32 request)
{
	struct vhost_user_msg msg = {
		.header.request = request,
	};

	return vhost_user_send(vu_dev, need_response, &msg, NULL, 0);
}

static int vhost_user_send_no_payload_fd(struct virtio_uml_device *vu_dev,
					 u32 request, int fd)
{
	struct vhost_user_msg msg = {
		.header.request = request,
	};

	return vhost_user_send(vu_dev, false, &msg, &fd, 1);
}

static int vhost_user_send_u64(struct virtio_uml_device *vu_dev,
			       u32 request, u64 value)
{
	struct vhost_user_msg msg = {
		.header.request = request,
		.header.size = sizeof(msg.payload.integer),
		.payload.integer = value,
	};

	return vhost_user_send(vu_dev, false, &msg, NULL, 0);
}

static int vhost_user_set_owner(struct virtio_uml_device *vu_dev)
{
	return vhost_user_send_no_payload(vu_dev, false, VHOST_USER_SET_OWNER);
}

static int vhost_user_get_features(struct virtio_uml_device *vu_dev,
				   u64 *features)
{
	int rc = vhost_user_send_no_payload(vu_dev, true,
					    VHOST_USER_GET_FEATURES);

	if (rc)
		return rc;
	return vhost_user_recv_u64(vu_dev, features);
}

static int vhost_user_set_features(struct virtio_uml_device *vu_dev,
				   u64 features)
{
	return vhost_user_send_u64(vu_dev, VHOST_USER_SET_FEATURES, features);
}

static int vhost_user_get_protocol_features(struct virtio_uml_device *vu_dev,
					    u64 *protocol_features)
{
	int rc = vhost_user_send_no_payload(vu_dev, true,
			VHOST_USER_GET_PROTOCOL_FEATURES);

	if (rc)
		return rc;
	return vhost_user_recv_u64(vu_dev, protocol_features);
}

static int vhost_user_set_protocol_features(struct virtio_uml_device *vu_dev,
					    u64 protocol_features)
{
	return vhost_user_send_u64(vu_dev, VHOST_USER_SET_PROTOCOL_FEATURES,
				   protocol_features);
}

static void vhost_user_reply(struct virtio_uml_device *vu_dev,
			     struct vhost_user_msg *msg, int response)
{
	struct vhost_user_msg reply = {
		.payload.integer = response,
	};
	size_t size = sizeof(reply.header) + sizeof(reply.payload.integer);
	int rc;

	reply.header = msg->header;
	reply.header.flags &= ~VHOST_USER_FLAG_NEED_REPLY;
	reply.header.flags |= VHOST_USER_FLAG_REPLY;
	reply.header.size = sizeof(reply.payload.integer);

	rc = full_sendmsg_fds(vu_dev->req_fd, &reply, size, NULL, 0);

	if (rc)
		vu_err(vu_dev,
		       "sending reply to slave request failed: %d (size %zu)\n",
		       rc, size);
}

static irqreturn_t vu_req_interrupt(int irq, void *data)
{
	struct virtio_uml_device *vu_dev = data;
	int response = 1;
	struct {
		struct vhost_user_msg msg;
		u8 extra_payload[512];
	} msg;
	int rc;

	rc = vhost_user_recv_req(vu_dev, &msg.msg,
				 sizeof(msg.msg.payload) +
				 sizeof(msg.extra_payload));

	if (rc)
		return IRQ_NONE;

	switch (msg.msg.header.request) {
	case VHOST_USER_SLAVE_CONFIG_CHANGE_MSG:
		virtio_config_changed(&vu_dev->vdev);
		response = 0;
		break;
	case VHOST_USER_SLAVE_IOTLB_MSG:
		/* not supported - VIRTIO_F_IOMMU_PLATFORM */
	case VHOST_USER_SLAVE_VRING_HOST_NOTIFIER_MSG:
		/* not supported - VHOST_USER_PROTOCOL_F_HOST_NOTIFIER */
	default:
		vu_err(vu_dev, "unexpected slave request %d\n",
		       msg.msg.header.request);
	}

	if (msg.msg.header.flags & VHOST_USER_FLAG_NEED_REPLY)
		vhost_user_reply(vu_dev, &msg.msg, response);

	return IRQ_HANDLED;
}

static int vhost_user_init_slave_req(struct virtio_uml_device *vu_dev)
{
	int rc, req_fds[2];

	/* Use a pipe for slave req fd, SIGIO is not supported for eventfd */
	rc = os_pipe(req_fds, true, true);
	if (rc < 0)
		return rc;
	vu_dev->req_fd = req_fds[0];

	rc = um_request_irq(VIRTIO_IRQ, vu_dev->req_fd, IRQ_READ,
			    vu_req_interrupt, IRQF_SHARED,
			    vu_dev->pdev->name, vu_dev);
	if (rc)
		goto err_close;

	rc = vhost_user_send_no_payload_fd(vu_dev, VHOST_USER_SET_SLAVE_REQ_FD,
					   req_fds[1]);
	if (rc)
		goto err_free_irq;

	goto out;

err_free_irq:
	um_free_irq(VIRTIO_IRQ, vu_dev);
err_close:
	os_close_file(req_fds[0]);
out:
	/* Close unused write end of request fds */
	os_close_file(req_fds[1]);
	return rc;
}

static int vhost_user_init(struct virtio_uml_device *vu_dev)
{
	int rc = vhost_user_set_owner(vu_dev);

	if (rc)
		return rc;
	rc = vhost_user_get_features(vu_dev, &vu_dev->features);
	if (rc)
		return rc;

	if (vu_dev->features & BIT_ULL(VHOST_USER_F_PROTOCOL_FEATURES)) {
		rc = vhost_user_get_protocol_features(vu_dev,
				&vu_dev->protocol_features);
		if (rc)
			return rc;
		vu_dev->protocol_features &= VHOST_USER_SUPPORTED_PROTOCOL_F;
		rc = vhost_user_set_protocol_features(vu_dev,
				vu_dev->protocol_features);
		if (rc)
			return rc;
	}

	if (vu_dev->protocol_features &
			BIT_ULL(VHOST_USER_PROTOCOL_F_SLAVE_REQ)) {
		rc = vhost_user_init_slave_req(vu_dev);
		if (rc)
			return rc;
	}

	return 0;
}

static void vhost_user_get_config(struct virtio_uml_device *vu_dev,
				  u32 offset, void *buf, u32 len)
{
	u32 cfg_size = offset + len;
	struct vhost_user_msg *msg;
	size_t payload_size = sizeof(msg->payload.config) + cfg_size;
	size_t msg_size = sizeof(msg->header) + payload_size;
	int rc;

	if (!(vu_dev->protocol_features &
	      BIT_ULL(VHOST_USER_PROTOCOL_F_CONFIG)))
		return;

	msg = kzalloc(msg_size, GFP_KERNEL);
	if (!msg)
		return;
	msg->header.request = VHOST_USER_GET_CONFIG;
	msg->header.size = payload_size;
	msg->payload.config.offset = 0;
	msg->payload.config.size = cfg_size;

	rc = vhost_user_send(vu_dev, true, msg, NULL, 0);
	if (rc) {
		vu_err(vu_dev, "sending VHOST_USER_GET_CONFIG failed: %d\n",
		       rc);
		goto free;
	}

	rc = vhost_user_recv_resp(vu_dev, msg, msg_size);
	if (rc) {
		vu_err(vu_dev,
		       "receiving VHOST_USER_GET_CONFIG response failed: %d\n",
		       rc);
		goto free;
	}

	if (msg->header.size != payload_size ||
	    msg->payload.config.size != cfg_size) {
		rc = -EPROTO;
		vu_err(vu_dev,
		       "Invalid VHOST_USER_GET_CONFIG sizes (payload %d expected %zu, config %u expected %u)\n",
		       msg->header.size, payload_size,
		       msg->payload.config.size, cfg_size);
		goto free;
	}
	memcpy(buf, msg->payload.config.payload + offset, len);

free:
	kfree(msg);
}

static void vhost_user_set_config(struct virtio_uml_device *vu_dev,
				  u32 offset, const void *buf, u32 len)
{
	struct vhost_user_msg *msg;
	size_t payload_size = sizeof(msg->payload.config) + len;
	size_t msg_size = sizeof(msg->header) + payload_size;
	int rc;

	if (!(vu_dev->protocol_features &
	      BIT_ULL(VHOST_USER_PROTOCOL_F_CONFIG)))
		return;

	msg = kzalloc(msg_size, GFP_KERNEL);
	if (!msg)
		return;
	msg->header.request = VHOST_USER_SET_CONFIG;
	msg->header.size = payload_size;
	msg->payload.config.offset = offset;
	msg->payload.config.size = len;
	memcpy(msg->payload.config.payload, buf, len);

	rc = vhost_user_send(vu_dev, false, msg, NULL, 0);
	if (rc)
		vu_err(vu_dev, "sending VHOST_USER_SET_CONFIG failed: %d\n",
		       rc);

	kfree(msg);
}

static int vhost_user_init_mem_region(u64 addr, u64 size, int *fd_out,
				      struct vhost_user_mem_region *region_out)
{
	unsigned long long mem_offset;
	int rc = phys_mapping(addr, &mem_offset);

	if (WARN(rc < 0, "phys_mapping of 0x%llx returned %d\n", addr, rc))
		return -EFAULT;
	*fd_out = rc;
	region_out->guest_addr = addr;
	region_out->user_addr = addr;
	region_out->size = size;
	region_out->mmap_offset = mem_offset;

	/* Ensure mapping is valid for the entire region */
	rc = phys_mapping(addr + size - 1, &mem_offset);
	if (WARN(rc != *fd_out, "phys_mapping of 0x%llx failed: %d != %d\n",
		 addr + size - 1, rc, *fd_out))
		return -EFAULT;
	return 0;
}

static int vhost_user_set_mem_table(struct virtio_uml_device *vu_dev)
{
	struct vhost_user_msg msg = {
		.header.request = VHOST_USER_SET_MEM_TABLE,
		.header.size = sizeof(msg.payload.mem_regions),
		.payload.mem_regions.num = 1,
	};
	unsigned long reserved = uml_reserved - uml_physmem;
	int fds[2];
	int rc;

	/*
	 * This is a bit tricky, see also the comment with setup_physmem().
	 *
	 * Essentially, setup_physmem() uses a file to mmap() our physmem,
	 * but the code and data we *already* have is omitted. To us, this
	 * is no difference, since they both become part of our address
	 * space and memory consumption. To somebody looking in from the
	 * outside, however, it is different because the part of our memory
	 * consumption that's already part of the binary (code/data) is not
	 * mapped from the file, so it's not visible to another mmap from
	 * the file descriptor.
	 *
	 * Thus, don't advertise this space to the vhost-user slave. This
	 * means that the slave will likely abort or similar when we give
	 * it an address from the hidden range, since it's not marked as
	 * a valid address, but at least that way we detect the issue and
	 * don't just have the slave read an all-zeroes buffer from the
	 * shared memory file, or write something there that we can never
	 * see (depending on the direction of the virtqueue traffic.)
	 *
	 * Since we usually don't want to use .text for virtio buffers,
	 * this effectively means that you cannot use
	 *  1) global variables, which are in the .bss and not in the shm
	 *     file-backed memory
	 *  2) the stack in some processes, depending on where they have
	 *     their stack (or maybe only no interrupt stack?)
	 *
	 * The stack is already not typically valid for DMA, so this isn't
	 * much of a restriction, but global variables might be encountered.
	 *
	 * It might be possible to fix it by copying around the data that's
	 * between bss_start and where we map the file now, but it's not
	 * something that you typically encounter with virtio drivers, so
	 * it didn't seem worthwhile.
	 */
	rc = vhost_user_init_mem_region(reserved, physmem_size - reserved,
					&fds[0],
					&msg.payload.mem_regions.regions[0]);

	if (rc < 0)
		return rc;
	if (highmem) {
		msg.payload.mem_regions.num++;
		rc = vhost_user_init_mem_region(__pa(end_iomem), highmem,
				&fds[1], &msg.payload.mem_regions.regions[1]);
		if (rc < 0)
			return rc;
	}

	return vhost_user_send(vu_dev, false, &msg, fds,
			       msg.payload.mem_regions.num);
}

static int vhost_user_set_vring_state(struct virtio_uml_device *vu_dev,
				      u32 request, u32 index, u32 num)
{
	struct vhost_user_msg msg = {
		.header.request = request,
		.header.size = sizeof(msg.payload.vring_state),
		.payload.vring_state.index = index,
		.payload.vring_state.num = num,
	};

	return vhost_user_send(vu_dev, false, &msg, NULL, 0);
}

static int vhost_user_set_vring_num(struct virtio_uml_device *vu_dev,
				    u32 index, u32 num)
{
	return vhost_user_set_vring_state(vu_dev, VHOST_USER_SET_VRING_NUM,
					  index, num);
}

static int vhost_user_set_vring_base(struct virtio_uml_device *vu_dev,
				     u32 index, u32 offset)
{
	return vhost_user_set_vring_state(vu_dev, VHOST_USER_SET_VRING_BASE,
					  index, offset);
}

static int vhost_user_set_vring_addr(struct virtio_uml_device *vu_dev,
				     u32 index, u64 desc, u64 used, u64 avail,
				     u64 log)
{
	struct vhost_user_msg msg = {
		.header.request = VHOST_USER_SET_VRING_ADDR,
		.header.size = sizeof(msg.payload.vring_addr),
		.payload.vring_addr.index = index,
		.payload.vring_addr.desc = desc,
		.payload.vring_addr.used = used,
		.payload.vring_addr.avail = avail,
		.payload.vring_addr.log = log,
	};

	return vhost_user_send(vu_dev, false, &msg, NULL, 0);
}

static int vhost_user_set_vring_fd(struct virtio_uml_device *vu_dev,
				   u32 request, int index, int fd)
{
	struct vhost_user_msg msg = {
		.header.request = request,
		.header.size = sizeof(msg.payload.integer),
		.payload.integer = index,
	};

	if (index & ~VHOST_USER_VRING_INDEX_MASK)
		return -EINVAL;
	if (fd < 0) {
		msg.payload.integer |= VHOST_USER_VRING_POLL_MASK;
		return vhost_user_send(vu_dev, false, &msg, NULL, 0);
	}
	return vhost_user_send(vu_dev, false, &msg, &fd, 1);
}

static int vhost_user_set_vring_call(struct virtio_uml_device *vu_dev,
				     int index, int fd)
{
	return vhost_user_set_vring_fd(vu_dev, VHOST_USER_SET_VRING_CALL,
				       index, fd);
}

static int vhost_user_set_vring_kick(struct virtio_uml_device *vu_dev,
				     int index, int fd)
{
	return vhost_user_set_vring_fd(vu_dev, VHOST_USER_SET_VRING_KICK,
				       index, fd);
}

static int vhost_user_set_vring_enable(struct virtio_uml_device *vu_dev,
				       u32 index, bool enable)
{
	if (!(vu_dev->features & BIT_ULL(VHOST_USER_F_PROTOCOL_FEATURES)))
		return 0;

	return vhost_user_set_vring_state(vu_dev, VHOST_USER_SET_VRING_ENABLE,
					  index, enable);
}


/* Virtio interface */

static bool vu_notify(struct virtqueue *vq)
{
	struct virtio_uml_vq_info *info = vq->priv;
	const uint64_t n = 1;
	int rc;

	do {
		rc = os_write_file(info->kick_fd, &n, sizeof(n));
	} while (rc == -EINTR);
	return !WARN(rc != sizeof(n), "write returned %d\n", rc);
}

static irqreturn_t vu_interrupt(int irq, void *opaque)
{
	struct virtqueue *vq = opaque;
	struct virtio_uml_vq_info *info = vq->priv;
	uint64_t n;
	int rc;
	irqreturn_t ret = IRQ_NONE;

	do {
		rc = os_read_file(info->call_fd, &n, sizeof(n));
		if (rc == sizeof(n))
			ret |= vring_interrupt(irq, vq);
	} while (rc == sizeof(n) || rc == -EINTR);
	WARN(rc != -EAGAIN, "read returned %d\n", rc);
	return ret;
}


static void vu_get(struct virtio_device *vdev, unsigned offset,
		   void *buf, unsigned len)
{
	struct virtio_uml_device *vu_dev = to_virtio_uml_device(vdev);

	vhost_user_get_config(vu_dev, offset, buf, len);
}

static void vu_set(struct virtio_device *vdev, unsigned offset,
		   const void *buf, unsigned len)
{
	struct virtio_uml_device *vu_dev = to_virtio_uml_device(vdev);

	vhost_user_set_config(vu_dev, offset, buf, len);
}

static u8 vu_get_status(struct virtio_device *vdev)
{
	struct virtio_uml_device *vu_dev = to_virtio_uml_device(vdev);

	return vu_dev->status;
}

static void vu_set_status(struct virtio_device *vdev, u8 status)
{
	struct virtio_uml_device *vu_dev = to_virtio_uml_device(vdev);

	vu_dev->status = status;
}

static void vu_reset(struct virtio_device *vdev)
{
	struct virtio_uml_device *vu_dev = to_virtio_uml_device(vdev);

	vu_dev->status = 0;
}

static void vu_del_vq(struct virtqueue *vq)
{
	struct virtio_uml_vq_info *info = vq->priv;

	um_free_irq(VIRTIO_IRQ, vq);

	os_close_file(info->call_fd);
	os_close_file(info->kick_fd);

	vring_del_virtqueue(vq);
	kfree(info);
}

static void vu_del_vqs(struct virtio_device *vdev)
{
	struct virtio_uml_device *vu_dev = to_virtio_uml_device(vdev);
	struct virtqueue *vq, *n;
	u64 features;

	/* Note: reverse order as a workaround to a decoding bug in snabb */
	list_for_each_entry_reverse(vq, &vdev->vqs, list)
		WARN_ON(vhost_user_set_vring_enable(vu_dev, vq->index, false));

	/* Ensure previous messages have been processed */
	WARN_ON(vhost_user_get_features(vu_dev, &features));

	list_for_each_entry_safe(vq, n, &vdev->vqs, list)
		vu_del_vq(vq);
}

static int vu_setup_vq_call_fd(struct virtio_uml_device *vu_dev,
			       struct virtqueue *vq)
{
	struct virtio_uml_vq_info *info = vq->priv;
	int call_fds[2];
	int rc;

	/* Use a pipe for call fd, since SIGIO is not supported for eventfd */
	rc = os_pipe(call_fds, true, true);
	if (rc < 0)
		return rc;

	info->call_fd = call_fds[0];
	rc = um_request_irq(VIRTIO_IRQ, info->call_fd, IRQ_READ,
			    vu_interrupt, IRQF_SHARED, info->name, vq);
	if (rc)
		goto close_both;

	rc = vhost_user_set_vring_call(vu_dev, vq->index, call_fds[1]);
	if (rc)
		goto release_irq;

	goto out;

release_irq:
	um_free_irq(VIRTIO_IRQ, vq);
close_both:
	os_close_file(call_fds[0]);
out:
	/* Close (unused) write end of call fds */
	os_close_file(call_fds[1]);

	return rc;
}

static struct virtqueue *vu_setup_vq(struct virtio_device *vdev,
				     unsigned index, vq_callback_t *callback,
				     const char *name, bool ctx)
{
	struct virtio_uml_device *vu_dev = to_virtio_uml_device(vdev);
	struct platform_device *pdev = vu_dev->pdev;
	struct virtio_uml_vq_info *info;
	struct virtqueue *vq;
	int num = MAX_SUPPORTED_QUEUE_SIZE;
	int rc;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		rc = -ENOMEM;
		goto error_kzalloc;
	}
	snprintf(info->name, sizeof(info->name), "%s.%d-%s", pdev->name,
		 pdev->id, name);

	vq = vring_create_virtqueue(index, num, PAGE_SIZE, vdev, true, true,
				    ctx, vu_notify, callback, info->name);
	if (!vq) {
		rc = -ENOMEM;
		goto error_create;
	}
	vq->priv = info;
	num = virtqueue_get_vring_size(vq);

	rc = os_eventfd(0, 0);
	if (rc < 0)
		goto error_kick;
	info->kick_fd = rc;

	rc = vu_setup_vq_call_fd(vu_dev, vq);
	if (rc)
		goto error_call;

	rc = vhost_user_set_vring_num(vu_dev, index, num);
	if (rc)
		goto error_setup;

	rc = vhost_user_set_vring_base(vu_dev, index, 0);
	if (rc)
		goto error_setup;

	rc = vhost_user_set_vring_addr(vu_dev, index,
				       virtqueue_get_desc_addr(vq),
				       virtqueue_get_used_addr(vq),
				       virtqueue_get_avail_addr(vq),
				       (u64) -1);
	if (rc)
		goto error_setup;

	return vq;

error_setup:
	um_free_irq(VIRTIO_IRQ, vq);
	os_close_file(info->call_fd);
error_call:
	os_close_file(info->kick_fd);
error_kick:
	vring_del_virtqueue(vq);
error_create:
	kfree(info);
error_kzalloc:
	return ERR_PTR(rc);
}

static int vu_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		       struct virtqueue *vqs[], vq_callback_t *callbacks[],
		       const char * const names[], const bool *ctx,
		       struct irq_affinity *desc)
{
	struct virtio_uml_device *vu_dev = to_virtio_uml_device(vdev);
	int i, queue_idx = 0, rc;
	struct virtqueue *vq;

	rc = vhost_user_set_mem_table(vu_dev);
	if (rc)
		return rc;

	for (i = 0; i < nvqs; ++i) {
		if (!names[i]) {
			vqs[i] = NULL;
			continue;
		}

		vqs[i] = vu_setup_vq(vdev, queue_idx++, callbacks[i], names[i],
				     ctx ? ctx[i] : false);
		if (IS_ERR(vqs[i])) {
			rc = PTR_ERR(vqs[i]);
			goto error_setup;
		}
	}

	list_for_each_entry(vq, &vdev->vqs, list) {
		struct virtio_uml_vq_info *info = vq->priv;

		rc = vhost_user_set_vring_kick(vu_dev, vq->index,
					       info->kick_fd);
		if (rc)
			goto error_setup;

		rc = vhost_user_set_vring_enable(vu_dev, vq->index, true);
		if (rc)
			goto error_setup;
	}

	return 0;

error_setup:
	vu_del_vqs(vdev);
	return rc;
}

static u64 vu_get_features(struct virtio_device *vdev)
{
	struct virtio_uml_device *vu_dev = to_virtio_uml_device(vdev);

	return vu_dev->features;
}

static int vu_finalize_features(struct virtio_device *vdev)
{
	struct virtio_uml_device *vu_dev = to_virtio_uml_device(vdev);
	u64 supported = vdev->features & VHOST_USER_SUPPORTED_F;

	vring_transport_features(vdev);
	vu_dev->features = vdev->features | supported;

	return vhost_user_set_features(vu_dev, vu_dev->features);
}

static const char *vu_bus_name(struct virtio_device *vdev)
{
	struct virtio_uml_device *vu_dev = to_virtio_uml_device(vdev);

	return vu_dev->pdev->name;
}

static const struct virtio_config_ops virtio_uml_config_ops = {
	.get = vu_get,
	.set = vu_set,
	.get_status = vu_get_status,
	.set_status = vu_set_status,
	.reset = vu_reset,
	.find_vqs = vu_find_vqs,
	.del_vqs = vu_del_vqs,
	.get_features = vu_get_features,
	.finalize_features = vu_finalize_features,
	.bus_name = vu_bus_name,
};

static void virtio_uml_release_dev(struct device *d)
{
	struct virtio_device *vdev =
			container_of(d, struct virtio_device, dev);
	struct virtio_uml_device *vu_dev = to_virtio_uml_device(vdev);

	/* might not have been opened due to not negotiating the feature */
	if (vu_dev->req_fd >= 0) {
		um_free_irq(VIRTIO_IRQ, vu_dev);
		os_close_file(vu_dev->req_fd);
	}

	os_close_file(vu_dev->sock);
}

/* Platform device */

struct virtio_uml_platform_data {
	u32 virtio_device_id;
	const char *socket_path;
};

static int virtio_uml_probe(struct platform_device *pdev)
{
	struct virtio_uml_platform_data *pdata = pdev->dev.platform_data;
	struct virtio_uml_device *vu_dev;
	int rc;

	if (!pdata)
		return -EINVAL;

	vu_dev = devm_kzalloc(&pdev->dev, sizeof(*vu_dev), GFP_KERNEL);
	if (!vu_dev)
		return -ENOMEM;

	vu_dev->vdev.dev.parent = &pdev->dev;
	vu_dev->vdev.dev.release = virtio_uml_release_dev;
	vu_dev->vdev.config = &virtio_uml_config_ops;
	vu_dev->vdev.id.device = pdata->virtio_device_id;
	vu_dev->vdev.id.vendor = VIRTIO_DEV_ANY_ID;
	vu_dev->pdev = pdev;
	vu_dev->req_fd = -1;

	do {
		rc = os_connect_socket(pdata->socket_path);
	} while (rc == -EINTR);
	if (rc < 0)
		return rc;
	vu_dev->sock = rc;

	rc = vhost_user_init(vu_dev);
	if (rc)
		goto error_init;

	platform_set_drvdata(pdev, vu_dev);

	rc = register_virtio_device(&vu_dev->vdev);
	if (rc)
		put_device(&vu_dev->vdev.dev);
	return rc;

error_init:
	os_close_file(vu_dev->sock);
	return rc;
}

static int virtio_uml_remove(struct platform_device *pdev)
{
	struct virtio_uml_device *vu_dev = platform_get_drvdata(pdev);

	unregister_virtio_device(&vu_dev->vdev);
	return 0;
}

/* Command line device list */

static void vu_cmdline_release_dev(struct device *d)
{
}

static struct device vu_cmdline_parent = {
	.init_name = "virtio-uml-cmdline",
	.release = vu_cmdline_release_dev,
};

static bool vu_cmdline_parent_registered;
static int vu_cmdline_id;

static int vu_cmdline_set(const char *device, const struct kernel_param *kp)
{
	const char *ids = strchr(device, ':');
	unsigned int virtio_device_id;
	int processed, consumed, err;
	char *socket_path;
	struct virtio_uml_platform_data pdata;
	struct platform_device *pdev;

	if (!ids || ids == device)
		return -EINVAL;

	processed = sscanf(ids, ":%u%n:%d%n",
			   &virtio_device_id, &consumed,
			   &vu_cmdline_id, &consumed);

	if (processed < 1 || ids[consumed])
		return -EINVAL;

	if (!vu_cmdline_parent_registered) {
		err = device_register(&vu_cmdline_parent);
		if (err) {
			pr_err("Failed to register parent device!\n");
			put_device(&vu_cmdline_parent);
			return err;
		}
		vu_cmdline_parent_registered = true;
	}

	socket_path = kmemdup_nul(device, ids - device, GFP_KERNEL);
	if (!socket_path)
		return -ENOMEM;

	pdata.virtio_device_id = (u32) virtio_device_id;
	pdata.socket_path = socket_path;

	pr_info("Registering device virtio-uml.%d id=%d at %s\n",
		vu_cmdline_id, virtio_device_id, socket_path);

	pdev = platform_device_register_data(&vu_cmdline_parent, "virtio-uml",
					     vu_cmdline_id++, &pdata,
					     sizeof(pdata));
	err = PTR_ERR_OR_ZERO(pdev);
	if (err)
		goto free;
	return 0;

free:
	kfree(socket_path);
	return err;
}

static int vu_cmdline_get_device(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct virtio_uml_platform_data *pdata = pdev->dev.platform_data;
	char *buffer = data;
	unsigned int len = strlen(buffer);

	snprintf(buffer + len, PAGE_SIZE - len, "%s:%d:%d\n",
		 pdata->socket_path, pdata->virtio_device_id, pdev->id);
	return 0;
}

static int vu_cmdline_get(char *buffer, const struct kernel_param *kp)
{
	buffer[0] = '\0';
	if (vu_cmdline_parent_registered)
		device_for_each_child(&vu_cmdline_parent, buffer,
				      vu_cmdline_get_device);
	return strlen(buffer) + 1;
}

static const struct kernel_param_ops vu_cmdline_param_ops = {
	.set = vu_cmdline_set,
	.get = vu_cmdline_get,
};

device_param_cb(device, &vu_cmdline_param_ops, NULL, S_IRUSR);
__uml_help(vu_cmdline_param_ops,
"virtio_uml.device=<socket>:<virtio_id>[:<platform_id>]\n"
"    Configure a virtio device over a vhost-user socket.\n"
"    See virtio_ids.h for a list of possible virtio device id values.\n"
"    Optionally use a specific platform_device id.\n\n"
);


static int vu_unregister_cmdline_device(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct virtio_uml_platform_data *pdata = pdev->dev.platform_data;

	kfree(pdata->socket_path);
	platform_device_unregister(pdev);
	return 0;
}

static void vu_unregister_cmdline_devices(void)
{
	if (vu_cmdline_parent_registered) {
		device_for_each_child(&vu_cmdline_parent, NULL,
				      vu_unregister_cmdline_device);
		device_unregister(&vu_cmdline_parent);
		vu_cmdline_parent_registered = false;
	}
}

/* Platform driver */

static const struct of_device_id virtio_uml_match[] = {
	{ .compatible = "virtio,uml", },
	{ }
};
MODULE_DEVICE_TABLE(of, virtio_uml_match);

static struct platform_driver virtio_uml_driver = {
	.probe = virtio_uml_probe,
	.remove = virtio_uml_remove,
	.driver = {
		.name = "virtio-uml",
		.of_match_table = virtio_uml_match,
	},
};

static int __init virtio_uml_init(void)
{
	return platform_driver_register(&virtio_uml_driver);
}

static void __exit virtio_uml_exit(void)
{
	platform_driver_unregister(&virtio_uml_driver);
	vu_unregister_cmdline_devices();
}

module_init(virtio_uml_init);
module_exit(virtio_uml_exit);
__uml_exitcall(virtio_uml_exit);

MODULE_DESCRIPTION("UML driver for vhost-user virtio devices");
MODULE_LICENSE("GPL");

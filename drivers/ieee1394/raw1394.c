/*
 * IEEE 1394 for Linux
 *
 * Raw interface to the bus
 *
 * Copyright (C) 1999, 2000 Andreas E. Bombe
 *               2001, 2002 Manfred Weihs <weihs@ict.tuwien.ac.at>
 *                     2002 Christian Toegel <christian.toegel@gmx.at>
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 *
 *
 * Contributions:
 *
 * Manfred Weihs <weihs@ict.tuwien.ac.at>
 *        configuration ROM manipulation
 *        address range mapping
 *        adaptation for new (transparent) loopback mechanism
 *        sending of arbitrary async packets
 * Christian Toegel <christian.toegel@gmx.at>
 *        address range mapping
 *        lock64 request
 *        transmit physical packet
 *        busreset notification control (switch on/off)
 *        busreset with selection of type (short/long)
 *        request_reply
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/compat.h>

#include "csr1212.h"
#include "highlevel.h"
#include "hosts.h"
#include "ieee1394.h"
#include "ieee1394_core.h"
#include "ieee1394_hotplug.h"
#include "ieee1394_transactions.h"
#include "ieee1394_types.h"
#include "iso.h"
#include "nodemgr.h"
#include "raw1394.h"
#include "raw1394-private.h"

#define int2ptr(x) ((void __user *)(unsigned long)x)
#define ptr2int(x) ((u64)(unsigned long)(void __user *)x)

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
#define RAW1394_DEBUG
#endif

#ifdef RAW1394_DEBUG
#define DBGMSG(fmt, args...) \
printk(KERN_INFO "raw1394:" fmt "\n" , ## args)
#else
#define DBGMSG(fmt, args...) do {} while (0)
#endif

static LIST_HEAD(host_info_list);
static int host_count;
static DEFINE_SPINLOCK(host_info_lock);
static atomic_t internal_generation = ATOMIC_INIT(0);

static atomic_t iso_buffer_size;
static const int iso_buffer_max = 4 * 1024 * 1024;	/* 4 MB */

static struct hpsb_highlevel raw1394_highlevel;

static int arm_read(struct hpsb_host *host, int nodeid, quadlet_t * buffer,
		    u64 addr, size_t length, u16 flags);
static int arm_write(struct hpsb_host *host, int nodeid, int destid,
		     quadlet_t * data, u64 addr, size_t length, u16 flags);
static int arm_lock(struct hpsb_host *host, int nodeid, quadlet_t * store,
		    u64 addr, quadlet_t data, quadlet_t arg, int ext_tcode,
		    u16 flags);
static int arm_lock64(struct hpsb_host *host, int nodeid, octlet_t * store,
		      u64 addr, octlet_t data, octlet_t arg, int ext_tcode,
		      u16 flags);
static const struct hpsb_address_ops arm_ops = {
	.read = arm_read,
	.write = arm_write,
	.lock = arm_lock,
	.lock64 = arm_lock64,
};

static void queue_complete_cb(struct pending_request *req);

static struct pending_request *__alloc_pending_request(gfp_t flags)
{
	struct pending_request *req;

	req = kzalloc(sizeof(*req), flags);
	if (req)
		INIT_LIST_HEAD(&req->list);

	return req;
}

static inline struct pending_request *alloc_pending_request(void)
{
	return __alloc_pending_request(GFP_KERNEL);
}

static void free_pending_request(struct pending_request *req)
{
	if (req->ibs) {
		if (atomic_dec_and_test(&req->ibs->refcount)) {
			atomic_sub(req->ibs->data_size, &iso_buffer_size);
			kfree(req->ibs);
		}
	} else if (req->free_data) {
		kfree(req->data);
	}
	hpsb_free_packet(req->packet);
	kfree(req);
}

/* fi->reqlists_lock must be taken */
static void __queue_complete_req(struct pending_request *req)
{
	struct file_info *fi = req->file_info;

	list_move_tail(&req->list, &fi->req_complete);
 	wake_up(&fi->wait_complete);
}

static void queue_complete_req(struct pending_request *req)
{
	unsigned long flags;
	struct file_info *fi = req->file_info;

	spin_lock_irqsave(&fi->reqlists_lock, flags);
	__queue_complete_req(req);
	spin_unlock_irqrestore(&fi->reqlists_lock, flags);
}

static void queue_complete_cb(struct pending_request *req)
{
	struct hpsb_packet *packet = req->packet;
	int rcode = (packet->header[1] >> 12) & 0xf;

	switch (packet->ack_code) {
	case ACKX_NONE:
	case ACKX_SEND_ERROR:
		req->req.error = RAW1394_ERROR_SEND_ERROR;
		break;
	case ACKX_ABORTED:
		req->req.error = RAW1394_ERROR_ABORTED;
		break;
	case ACKX_TIMEOUT:
		req->req.error = RAW1394_ERROR_TIMEOUT;
		break;
	default:
		req->req.error = (packet->ack_code << 16) | rcode;
		break;
	}

	if (!((packet->ack_code == ACK_PENDING) && (rcode == RCODE_COMPLETE))) {
		req->req.length = 0;
	}

	if ((req->req.type == RAW1394_REQ_ASYNC_READ) ||
	    (req->req.type == RAW1394_REQ_ASYNC_WRITE) ||
	    (req->req.type == RAW1394_REQ_ASYNC_STREAM) ||
	    (req->req.type == RAW1394_REQ_LOCK) ||
	    (req->req.type == RAW1394_REQ_LOCK64))
		hpsb_free_tlabel(packet);

	queue_complete_req(req);
}

static void add_host(struct hpsb_host *host)
{
	struct host_info *hi;
	unsigned long flags;

	hi = kmalloc(sizeof(*hi), GFP_KERNEL);

	if (hi) {
		INIT_LIST_HEAD(&hi->list);
		hi->host = host;
		INIT_LIST_HEAD(&hi->file_info_list);

		spin_lock_irqsave(&host_info_lock, flags);
		list_add_tail(&hi->list, &host_info_list);
		host_count++;
		spin_unlock_irqrestore(&host_info_lock, flags);
	}

	atomic_inc(&internal_generation);
}

static struct host_info *find_host_info(struct hpsb_host *host)
{
	struct host_info *hi;

	list_for_each_entry(hi, &host_info_list, list)
	    if (hi->host == host)
		return hi;

	return NULL;
}

static void remove_host(struct hpsb_host *host)
{
	struct host_info *hi;
	unsigned long flags;

	spin_lock_irqsave(&host_info_lock, flags);
	hi = find_host_info(host);

	if (hi != NULL) {
		list_del(&hi->list);
		host_count--;
		/*
		   FIXME: address ranges should be removed
		   and fileinfo states should be initialized
		   (including setting generation to
		   internal-generation ...)
		 */
	}
	spin_unlock_irqrestore(&host_info_lock, flags);

	if (hi == NULL) {
		printk(KERN_ERR "raw1394: attempt to remove unknown host "
		       "0x%p\n", host);
		return;
	}

	kfree(hi);

	atomic_inc(&internal_generation);
}

static void host_reset(struct hpsb_host *host)
{
	unsigned long flags;
	struct host_info *hi;
	struct file_info *fi;
	struct pending_request *req;

	spin_lock_irqsave(&host_info_lock, flags);
	hi = find_host_info(host);

	if (hi != NULL) {
		list_for_each_entry(fi, &hi->file_info_list, list) {
			if (fi->notification == RAW1394_NOTIFY_ON) {
				req = __alloc_pending_request(GFP_ATOMIC);

				if (req != NULL) {
					req->file_info = fi;
					req->req.type = RAW1394_REQ_BUS_RESET;
					req->req.generation =
					    get_hpsb_generation(host);
					req->req.misc = (host->node_id << 16)
					    | host->node_count;
					if (fi->protocol_version > 3) {
						req->req.misc |=
						    (NODEID_TO_NODE
						     (host->irm_id)
						     << 8);
					}

					queue_complete_req(req);
				}
			}
		}
	}
	spin_unlock_irqrestore(&host_info_lock, flags);
}

static void fcp_request(struct hpsb_host *host, int nodeid, int direction,
			int cts, u8 * data, size_t length)
{
	unsigned long flags;
	struct host_info *hi;
	struct file_info *fi;
	struct pending_request *req, *req_next;
	struct iso_block_store *ibs = NULL;
	LIST_HEAD(reqs);

	if ((atomic_read(&iso_buffer_size) + length) > iso_buffer_max) {
		HPSB_INFO("dropped fcp request");
		return;
	}

	spin_lock_irqsave(&host_info_lock, flags);
	hi = find_host_info(host);

	if (hi != NULL) {
		list_for_each_entry(fi, &hi->file_info_list, list) {
			if (!fi->fcp_buffer)
				continue;

			req = __alloc_pending_request(GFP_ATOMIC);
			if (!req)
				break;

			if (!ibs) {
				ibs = kmalloc(sizeof(*ibs) + length,
					      GFP_ATOMIC);
				if (!ibs) {
					kfree(req);
					break;
				}

				atomic_add(length, &iso_buffer_size);
				atomic_set(&ibs->refcount, 0);
				ibs->data_size = length;
				memcpy(ibs->data, data, length);
			}

			atomic_inc(&ibs->refcount);

			req->file_info = fi;
			req->ibs = ibs;
			req->data = ibs->data;
			req->req.type = RAW1394_REQ_FCP_REQUEST;
			req->req.generation = get_hpsb_generation(host);
			req->req.misc = nodeid | (direction << 16);
			req->req.recvb = ptr2int(fi->fcp_buffer);
			req->req.length = length;

			list_add_tail(&req->list, &reqs);
		}
	}
	spin_unlock_irqrestore(&host_info_lock, flags);

	list_for_each_entry_safe(req, req_next, &reqs, list)
	    queue_complete_req(req);
}

#ifdef CONFIG_COMPAT
struct compat_raw1394_req {
	__u32 type;
	__s32 error;
	__u32 misc;

	__u32 generation;
	__u32 length;

	__u64 address;

	__u64 tag;

	__u64 sendb;
	__u64 recvb;
}
#if defined(CONFIG_X86_64) || defined(CONFIG_IA64)
__attribute__((packed))
#endif
;

static const char __user *raw1394_compat_write(const char __user *buf)
{
	struct compat_raw1394_req __user *cr = (typeof(cr)) buf;
	struct raw1394_request __user *r;

	r = compat_alloc_user_space(sizeof(struct raw1394_request));

#define C(x) __copy_in_user(&r->x, &cr->x, sizeof(r->x))

	if (copy_in_user(r, cr, sizeof(struct compat_raw1394_req)) ||
	    C(address) ||
	    C(tag) ||
	    C(sendb) ||
	    C(recvb))
		return (__force const char __user *)ERR_PTR(-EFAULT);

	return (const char __user *)r;
}
#undef C

#define P(x) __put_user(r->x, &cr->x)

static int
raw1394_compat_read(const char __user *buf, struct raw1394_request *r)
{
	struct compat_raw1394_req __user *cr = (typeof(cr)) buf;

	if (!access_ok(VERIFY_WRITE, cr, sizeof(struct compat_raw1394_req)) ||
	    P(type) ||
	    P(error) ||
	    P(misc) ||
	    P(generation) ||
	    P(length) ||
	    P(address) ||
	    P(tag) ||
	    P(sendb) ||
	    P(recvb))
		return -EFAULT;

	return sizeof(struct compat_raw1394_req);
}
#undef P

#endif

/* get next completed request  (caller must hold fi->reqlists_lock) */
static inline struct pending_request *__next_complete_req(struct file_info *fi)
{
	struct list_head *lh;
	struct pending_request *req = NULL;

	if (!list_empty(&fi->req_complete)) {
		lh = fi->req_complete.next;
		list_del(lh);
		req = list_entry(lh, struct pending_request, list);
	}
	return req;
}

/* atomically get next completed request */
static struct pending_request *next_complete_req(struct file_info *fi)
{
	unsigned long flags;
	struct pending_request *req;

	spin_lock_irqsave(&fi->reqlists_lock, flags);
	req = __next_complete_req(fi);
	spin_unlock_irqrestore(&fi->reqlists_lock, flags);
	return req;
}

static ssize_t raw1394_read(struct file *file, char __user * buffer,
			    size_t count, loff_t * offset_is_ignored)
{
	struct file_info *fi = (struct file_info *)file->private_data;
	struct pending_request *req;
	ssize_t ret;

#ifdef CONFIG_COMPAT
	if (count == sizeof(struct compat_raw1394_req)) {
		/* ok */
	} else
#endif
	if (count != sizeof(struct raw1394_request)) {
		return -EINVAL;
	}

	if (!access_ok(VERIFY_WRITE, buffer, count)) {
		return -EFAULT;
	}

	if (file->f_flags & O_NONBLOCK) {
		if (!(req = next_complete_req(fi)))
			return -EAGAIN;
	} else {
		/*
		 * NB: We call the macro wait_event_interruptible() with a
		 * condition argument with side effect.  This is only possible
		 * because the side effect does not occur until the condition
		 * became true, and wait_event_interruptible() won't evaluate
		 * the condition again after that.
		 */
		if (wait_event_interruptible(fi->wait_complete,
					     (req = next_complete_req(fi))))
			return -ERESTARTSYS;
	}

	if (req->req.length) {
		if (copy_to_user(int2ptr(req->req.recvb), req->data,
				 req->req.length)) {
			req->req.error = RAW1394_ERROR_MEMFAULT;
		}
	}

#ifdef CONFIG_COMPAT
	if (count == sizeof(struct compat_raw1394_req) &&
   	    sizeof(struct compat_raw1394_req) !=
			sizeof(struct raw1394_request)) {
		ret = raw1394_compat_read(buffer, &req->req);
	} else
#endif
	{
		if (copy_to_user(buffer, &req->req, sizeof(req->req))) {
			ret = -EFAULT;
			goto out;
		}
		ret = (ssize_t) sizeof(struct raw1394_request);
	}
      out:
	free_pending_request(req);
	return ret;
}

static int state_opened(struct file_info *fi, struct pending_request *req)
{
	if (req->req.type == RAW1394_REQ_INITIALIZE) {
		switch (req->req.misc) {
		case RAW1394_KERNELAPI_VERSION:
		case 3:
			fi->state = initialized;
			fi->protocol_version = req->req.misc;
			req->req.error = RAW1394_ERROR_NONE;
			req->req.generation = atomic_read(&internal_generation);
			break;

		default:
			req->req.error = RAW1394_ERROR_COMPAT;
			req->req.misc = RAW1394_KERNELAPI_VERSION;
		}
	} else {
		req->req.error = RAW1394_ERROR_STATE_ORDER;
	}

	req->req.length = 0;
	queue_complete_req(req);
	return 0;
}

static int state_initialized(struct file_info *fi, struct pending_request *req)
{
	unsigned long flags;
	struct host_info *hi;
	struct raw1394_khost_list *khl;

	if (req->req.generation != atomic_read(&internal_generation)) {
		req->req.error = RAW1394_ERROR_GENERATION;
		req->req.generation = atomic_read(&internal_generation);
		req->req.length = 0;
		queue_complete_req(req);
		return 0;
	}

	switch (req->req.type) {
	case RAW1394_REQ_LIST_CARDS:
		spin_lock_irqsave(&host_info_lock, flags);
		khl = kmalloc(sizeof(*khl) * host_count, GFP_ATOMIC);

		if (khl) {
			req->req.misc = host_count;
			req->data = (quadlet_t *) khl;

			list_for_each_entry(hi, &host_info_list, list) {
				khl->nodes = hi->host->node_count;
				strcpy(khl->name, hi->host->driver->name);
				khl++;
			}
		}
		spin_unlock_irqrestore(&host_info_lock, flags);

		if (khl) {
			req->req.error = RAW1394_ERROR_NONE;
			req->req.length = min(req->req.length,
					      (u32) (sizeof
						     (struct raw1394_khost_list)
						     * req->req.misc));
			req->free_data = 1;
		} else {
			return -ENOMEM;
		}
		break;

	case RAW1394_REQ_SET_CARD:
		spin_lock_irqsave(&host_info_lock, flags);
		if (req->req.misc >= host_count) {
			req->req.error = RAW1394_ERROR_INVALID_ARG;
			goto out_set_card;
		}
		list_for_each_entry(hi, &host_info_list, list)
			if (!req->req.misc--)
				break;
		get_device(&hi->host->device); /* FIXME handle failure case */
		list_add_tail(&fi->list, &hi->file_info_list);

		/* prevent unloading of the host's low-level driver */
		if (!try_module_get(hi->host->driver->owner)) {
			req->req.error = RAW1394_ERROR_ABORTED;
			goto out_set_card;
		}
		WARN_ON(fi->host);
		fi->host = hi->host;
		fi->state = connected;

		req->req.error = RAW1394_ERROR_NONE;
		req->req.generation = get_hpsb_generation(fi->host);
		req->req.misc = (fi->host->node_id << 16)
				| fi->host->node_count;
		if (fi->protocol_version > 3)
			req->req.misc |= NODEID_TO_NODE(fi->host->irm_id) << 8;
out_set_card:
		spin_unlock_irqrestore(&host_info_lock, flags);

		req->req.length = 0;
		break;

	default:
		req->req.error = RAW1394_ERROR_STATE_ORDER;
		req->req.length = 0;
		break;
	}

	queue_complete_req(req);
	return 0;
}

static void handle_fcp_listen(struct file_info *fi, struct pending_request *req)
{
	if (req->req.misc) {
		if (fi->fcp_buffer) {
			req->req.error = RAW1394_ERROR_ALREADY;
		} else {
			fi->fcp_buffer = int2ptr(req->req.recvb);
		}
	} else {
		if (!fi->fcp_buffer) {
			req->req.error = RAW1394_ERROR_ALREADY;
		} else {
			fi->fcp_buffer = NULL;
		}
	}

	req->req.length = 0;
	queue_complete_req(req);
}

static int handle_async_request(struct file_info *fi,
				struct pending_request *req, int node)
{
	unsigned long flags;
	struct hpsb_packet *packet = NULL;
	u64 addr = req->req.address & 0xffffffffffffULL;

	switch (req->req.type) {
	case RAW1394_REQ_ASYNC_READ:
		DBGMSG("read_request called");
		packet =
		    hpsb_make_readpacket(fi->host, node, addr, req->req.length);

		if (!packet)
			return -ENOMEM;

		if (req->req.length == 4)
			req->data = &packet->header[3];
		else
			req->data = packet->data;

		break;

	case RAW1394_REQ_ASYNC_WRITE:
		DBGMSG("write_request called");

		packet = hpsb_make_writepacket(fi->host, node, addr, NULL,
					       req->req.length);
		if (!packet)
			return -ENOMEM;

		if (req->req.length == 4) {
			if (copy_from_user
			    (&packet->header[3], int2ptr(req->req.sendb),
			     req->req.length))
				req->req.error = RAW1394_ERROR_MEMFAULT;
		} else {
			if (copy_from_user
			    (packet->data, int2ptr(req->req.sendb),
			     req->req.length))
				req->req.error = RAW1394_ERROR_MEMFAULT;
		}

		req->req.length = 0;
		break;

	case RAW1394_REQ_ASYNC_STREAM:
		DBGMSG("stream_request called");

		packet =
		    hpsb_make_streampacket(fi->host, NULL, req->req.length,
					   node & 0x3f /*channel */ ,
					   (req->req.misc >> 16) & 0x3,
					   req->req.misc & 0xf);
		if (!packet)
			return -ENOMEM;

		if (copy_from_user(packet->data, int2ptr(req->req.sendb),
				   req->req.length))
			req->req.error = RAW1394_ERROR_MEMFAULT;

		req->req.length = 0;
		break;

	case RAW1394_REQ_LOCK:
		DBGMSG("lock_request called");
		if ((req->req.misc == EXTCODE_FETCH_ADD)
		    || (req->req.misc == EXTCODE_LITTLE_ADD)) {
			if (req->req.length != 4) {
				req->req.error = RAW1394_ERROR_INVALID_ARG;
				break;
			}
		} else {
			if (req->req.length != 8) {
				req->req.error = RAW1394_ERROR_INVALID_ARG;
				break;
			}
		}

		packet = hpsb_make_lockpacket(fi->host, node, addr,
					      req->req.misc, NULL, 0);
		if (!packet)
			return -ENOMEM;

		if (copy_from_user(packet->data, int2ptr(req->req.sendb),
				   req->req.length)) {
			req->req.error = RAW1394_ERROR_MEMFAULT;
			break;
		}

		req->data = packet->data;
		req->req.length = 4;
		break;

	case RAW1394_REQ_LOCK64:
		DBGMSG("lock64_request called");
		if ((req->req.misc == EXTCODE_FETCH_ADD)
		    || (req->req.misc == EXTCODE_LITTLE_ADD)) {
			if (req->req.length != 8) {
				req->req.error = RAW1394_ERROR_INVALID_ARG;
				break;
			}
		} else {
			if (req->req.length != 16) {
				req->req.error = RAW1394_ERROR_INVALID_ARG;
				break;
			}
		}
		packet = hpsb_make_lock64packet(fi->host, node, addr,
						req->req.misc, NULL, 0);
		if (!packet)
			return -ENOMEM;

		if (copy_from_user(packet->data, int2ptr(req->req.sendb),
				   req->req.length)) {
			req->req.error = RAW1394_ERROR_MEMFAULT;
			break;
		}

		req->data = packet->data;
		req->req.length = 8;
		break;

	default:
		req->req.error = RAW1394_ERROR_STATE_ORDER;
	}

	req->packet = packet;

	if (req->req.error) {
		req->req.length = 0;
		queue_complete_req(req);
		return 0;
	}

	hpsb_set_packet_complete_task(packet,
				      (void (*)(void *))queue_complete_cb, req);

	spin_lock_irqsave(&fi->reqlists_lock, flags);
	list_add_tail(&req->list, &fi->req_pending);
	spin_unlock_irqrestore(&fi->reqlists_lock, flags);

	packet->generation = req->req.generation;

	if (hpsb_send_packet(packet) < 0) {
		req->req.error = RAW1394_ERROR_SEND_ERROR;
		req->req.length = 0;
		hpsb_free_tlabel(packet);
		queue_complete_req(req);
	}
	return 0;
}

static int handle_async_send(struct file_info *fi, struct pending_request *req)
{
	unsigned long flags;
	struct hpsb_packet *packet;
	int header_length = req->req.misc & 0xffff;
	int expect_response = req->req.misc >> 16;
	size_t data_size;

	if (header_length > req->req.length || header_length < 12 ||
	    header_length > FIELD_SIZEOF(struct hpsb_packet, header)) {
		req->req.error = RAW1394_ERROR_INVALID_ARG;
		req->req.length = 0;
		queue_complete_req(req);
		return 0;
	}

	data_size = req->req.length - header_length;
	packet = hpsb_alloc_packet(data_size);
	req->packet = packet;
	if (!packet)
		return -ENOMEM;

	if (copy_from_user(packet->header, int2ptr(req->req.sendb),
			   header_length)) {
		req->req.error = RAW1394_ERROR_MEMFAULT;
		req->req.length = 0;
		queue_complete_req(req);
		return 0;
	}

	if (copy_from_user
	    (packet->data, int2ptr(req->req.sendb) + header_length,
	     data_size)) {
		req->req.error = RAW1394_ERROR_MEMFAULT;
		req->req.length = 0;
		queue_complete_req(req);
		return 0;
	}

	packet->type = hpsb_async;
	packet->node_id = packet->header[0] >> 16;
	packet->tcode = (packet->header[0] >> 4) & 0xf;
	packet->tlabel = (packet->header[0] >> 10) & 0x3f;
	packet->host = fi->host;
	packet->expect_response = expect_response;
	packet->header_size = header_length;
	packet->data_size = data_size;

	req->req.length = 0;
	hpsb_set_packet_complete_task(packet,
				      (void (*)(void *))queue_complete_cb, req);

	spin_lock_irqsave(&fi->reqlists_lock, flags);
	list_add_tail(&req->list, &fi->req_pending);
	spin_unlock_irqrestore(&fi->reqlists_lock, flags);

	/* Update the generation of the packet just before sending. */
	packet->generation = req->req.generation;

	if (hpsb_send_packet(packet) < 0) {
		req->req.error = RAW1394_ERROR_SEND_ERROR;
		queue_complete_req(req);
	}

	return 0;
}

static int arm_read(struct hpsb_host *host, int nodeid, quadlet_t * buffer,
		    u64 addr, size_t length, u16 flags)
{
	unsigned long irqflags;
	struct pending_request *req;
	struct host_info *hi;
	struct file_info *fi = NULL;
	struct list_head *entry;
	struct arm_addr *arm_addr = NULL;
	struct arm_request *arm_req = NULL;
	struct arm_response *arm_resp = NULL;
	int found = 0, size = 0, rcode = -1;
	struct arm_request_response *arm_req_resp = NULL;

	DBGMSG("arm_read  called by node: %X "
	       "addr: %4.4x %8.8x length: %Zu", nodeid,
	       (u16) ((addr >> 32) & 0xFFFF), (u32) (addr & 0xFFFFFFFF),
	       length);
	spin_lock_irqsave(&host_info_lock, irqflags);
	hi = find_host_info(host);	/* search address-entry */
	if (hi != NULL) {
		list_for_each_entry(fi, &hi->file_info_list, list) {
			entry = fi->addr_list.next;
			while (entry != &(fi->addr_list)) {
				arm_addr =
				    list_entry(entry, struct arm_addr,
					       addr_list);
				if (((arm_addr->start) <= (addr))
				    && ((arm_addr->end) >= (addr + length))) {
					found = 1;
					break;
				}
				entry = entry->next;
			}
			if (found) {
				break;
			}
		}
	}
	rcode = -1;
	if (!found) {
		printk(KERN_ERR "raw1394: arm_read FAILED addr_entry not found"
		       " -> rcode_address_error\n");
		spin_unlock_irqrestore(&host_info_lock, irqflags);
		return (RCODE_ADDRESS_ERROR);
	} else {
		DBGMSG("arm_read addr_entry FOUND");
	}
	if (arm_addr->rec_length < length) {
		DBGMSG("arm_read blocklength too big -> rcode_data_error");
		rcode = RCODE_DATA_ERROR;	/* hardware error, data is unavailable */
	}
	if (rcode == -1) {
		if (arm_addr->access_rights & ARM_READ) {
			if (!(arm_addr->client_transactions & ARM_READ)) {
				memcpy(buffer,
				       (arm_addr->addr_space_buffer) + (addr -
									(arm_addr->
									 start)),
				       length);
				DBGMSG("arm_read -> (rcode_complete)");
				rcode = RCODE_COMPLETE;
			}
		} else {
			rcode = RCODE_TYPE_ERROR;	/* function not allowed */
			DBGMSG("arm_read -> rcode_type_error (access denied)");
		}
	}
	if (arm_addr->notification_options & ARM_READ) {
		DBGMSG("arm_read -> entering notification-section");
		req = __alloc_pending_request(GFP_ATOMIC);
		if (!req) {
			DBGMSG("arm_read -> rcode_conflict_error");
			spin_unlock_irqrestore(&host_info_lock, irqflags);
			return (RCODE_CONFLICT_ERROR);	/* A resource conflict was detected.
							   The request may be retried */
		}
		if (rcode == RCODE_COMPLETE) {
			size =
			    sizeof(struct arm_request) +
			    sizeof(struct arm_response) +
			    length * sizeof(byte_t) +
			    sizeof(struct arm_request_response);
		} else {
			size =
			    sizeof(struct arm_request) +
			    sizeof(struct arm_response) +
			    sizeof(struct arm_request_response);
		}
		req->data = kmalloc(size, GFP_ATOMIC);
		if (!(req->data)) {
			free_pending_request(req);
			DBGMSG("arm_read -> rcode_conflict_error");
			spin_unlock_irqrestore(&host_info_lock, irqflags);
			return (RCODE_CONFLICT_ERROR);	/* A resource conflict was detected.
							   The request may be retried */
		}
		req->free_data = 1;
		req->file_info = fi;
		req->req.type = RAW1394_REQ_ARM;
		req->req.generation = get_hpsb_generation(host);
		req->req.misc =
		    (((length << 16) & (0xFFFF0000)) | (ARM_READ & 0xFF));
		req->req.tag = arm_addr->arm_tag;
		req->req.recvb = arm_addr->recvb;
		req->req.length = size;
		arm_req_resp = (struct arm_request_response *)(req->data);
		arm_req = (struct arm_request *)((byte_t *) (req->data) +
						 (sizeof
						  (struct
						   arm_request_response)));
		arm_resp =
		    (struct arm_response *)((byte_t *) (arm_req) +
					    (sizeof(struct arm_request)));
		arm_req->buffer = NULL;
		arm_resp->buffer = NULL;
		if (rcode == RCODE_COMPLETE) {
			byte_t *buf =
			    (byte_t *) arm_resp + sizeof(struct arm_response);
			memcpy(buf,
			       (arm_addr->addr_space_buffer) + (addr -
								(arm_addr->
								 start)),
			       length);
			arm_resp->buffer =
			    int2ptr((arm_addr->recvb) +
				    sizeof(struct arm_request_response) +
				    sizeof(struct arm_request) +
				    sizeof(struct arm_response));
		}
		arm_resp->buffer_length =
		    (rcode == RCODE_COMPLETE) ? length : 0;
		arm_resp->response_code = rcode;
		arm_req->buffer_length = 0;
		arm_req->generation = req->req.generation;
		arm_req->extended_transaction_code = 0;
		arm_req->destination_offset = addr;
		arm_req->source_nodeid = nodeid;
		arm_req->destination_nodeid = host->node_id;
		arm_req->tlabel = (flags >> 10) & 0x3f;
		arm_req->tcode = (flags >> 4) & 0x0f;
		arm_req_resp->request = int2ptr((arm_addr->recvb) +
						sizeof(struct
						       arm_request_response));
		arm_req_resp->response =
		    int2ptr((arm_addr->recvb) +
			    sizeof(struct arm_request_response) +
			    sizeof(struct arm_request));
		queue_complete_req(req);
	}
	spin_unlock_irqrestore(&host_info_lock, irqflags);
	return (rcode);
}

static int arm_write(struct hpsb_host *host, int nodeid, int destid,
		     quadlet_t * data, u64 addr, size_t length, u16 flags)
{
	unsigned long irqflags;
	struct pending_request *req;
	struct host_info *hi;
	struct file_info *fi = NULL;
	struct list_head *entry;
	struct arm_addr *arm_addr = NULL;
	struct arm_request *arm_req = NULL;
	struct arm_response *arm_resp = NULL;
	int found = 0, size = 0, rcode = -1, length_conflict = 0;
	struct arm_request_response *arm_req_resp = NULL;

	DBGMSG("arm_write called by node: %X "
	       "addr: %4.4x %8.8x length: %Zu", nodeid,
	       (u16) ((addr >> 32) & 0xFFFF), (u32) (addr & 0xFFFFFFFF),
	       length);
	spin_lock_irqsave(&host_info_lock, irqflags);
	hi = find_host_info(host);	/* search address-entry */
	if (hi != NULL) {
		list_for_each_entry(fi, &hi->file_info_list, list) {
			entry = fi->addr_list.next;
			while (entry != &(fi->addr_list)) {
				arm_addr =
				    list_entry(entry, struct arm_addr,
					       addr_list);
				if (((arm_addr->start) <= (addr))
				    && ((arm_addr->end) >= (addr + length))) {
					found = 1;
					break;
				}
				entry = entry->next;
			}
			if (found) {
				break;
			}
		}
	}
	rcode = -1;
	if (!found) {
		printk(KERN_ERR "raw1394: arm_write FAILED addr_entry not found"
		       " -> rcode_address_error\n");
		spin_unlock_irqrestore(&host_info_lock, irqflags);
		return (RCODE_ADDRESS_ERROR);
	} else {
		DBGMSG("arm_write addr_entry FOUND");
	}
	if (arm_addr->rec_length < length) {
		DBGMSG("arm_write blocklength too big -> rcode_data_error");
		length_conflict = 1;
		rcode = RCODE_DATA_ERROR;	/* hardware error, data is unavailable */
	}
	if (rcode == -1) {
		if (arm_addr->access_rights & ARM_WRITE) {
			if (!(arm_addr->client_transactions & ARM_WRITE)) {
				memcpy((arm_addr->addr_space_buffer) +
				       (addr - (arm_addr->start)), data,
				       length);
				DBGMSG("arm_write -> (rcode_complete)");
				rcode = RCODE_COMPLETE;
			}
		} else {
			rcode = RCODE_TYPE_ERROR;	/* function not allowed */
			DBGMSG("arm_write -> rcode_type_error (access denied)");
		}
	}
	if (arm_addr->notification_options & ARM_WRITE) {
		DBGMSG("arm_write -> entering notification-section");
		req = __alloc_pending_request(GFP_ATOMIC);
		if (!req) {
			DBGMSG("arm_write -> rcode_conflict_error");
			spin_unlock_irqrestore(&host_info_lock, irqflags);
			return (RCODE_CONFLICT_ERROR);	/* A resource conflict was detected.
							   The request my be retried */
		}
		size =
		    sizeof(struct arm_request) + sizeof(struct arm_response) +
		    (length) * sizeof(byte_t) +
		    sizeof(struct arm_request_response);
		req->data = kmalloc(size, GFP_ATOMIC);
		if (!(req->data)) {
			free_pending_request(req);
			DBGMSG("arm_write -> rcode_conflict_error");
			spin_unlock_irqrestore(&host_info_lock, irqflags);
			return (RCODE_CONFLICT_ERROR);	/* A resource conflict was detected.
							   The request may be retried */
		}
		req->free_data = 1;
		req->file_info = fi;
		req->req.type = RAW1394_REQ_ARM;
		req->req.generation = get_hpsb_generation(host);
		req->req.misc =
		    (((length << 16) & (0xFFFF0000)) | (ARM_WRITE & 0xFF));
		req->req.tag = arm_addr->arm_tag;
		req->req.recvb = arm_addr->recvb;
		req->req.length = size;
		arm_req_resp = (struct arm_request_response *)(req->data);
		arm_req = (struct arm_request *)((byte_t *) (req->data) +
						 (sizeof
						  (struct
						   arm_request_response)));
		arm_resp =
		    (struct arm_response *)((byte_t *) (arm_req) +
					    (sizeof(struct arm_request)));
		arm_resp->buffer = NULL;
		memcpy((byte_t *) arm_resp + sizeof(struct arm_response),
		       data, length);
		arm_req->buffer = int2ptr((arm_addr->recvb) +
					  sizeof(struct arm_request_response) +
					  sizeof(struct arm_request) +
					  sizeof(struct arm_response));
		arm_req->buffer_length = length;
		arm_req->generation = req->req.generation;
		arm_req->extended_transaction_code = 0;
		arm_req->destination_offset = addr;
		arm_req->source_nodeid = nodeid;
		arm_req->destination_nodeid = destid;
		arm_req->tlabel = (flags >> 10) & 0x3f;
		arm_req->tcode = (flags >> 4) & 0x0f;
		arm_resp->buffer_length = 0;
		arm_resp->response_code = rcode;
		arm_req_resp->request = int2ptr((arm_addr->recvb) +
						sizeof(struct
						       arm_request_response));
		arm_req_resp->response =
		    int2ptr((arm_addr->recvb) +
			    sizeof(struct arm_request_response) +
			    sizeof(struct arm_request));
		queue_complete_req(req);
	}
	spin_unlock_irqrestore(&host_info_lock, irqflags);
	return (rcode);
}

static int arm_lock(struct hpsb_host *host, int nodeid, quadlet_t * store,
		    u64 addr, quadlet_t data, quadlet_t arg, int ext_tcode,
		    u16 flags)
{
	unsigned long irqflags;
	struct pending_request *req;
	struct host_info *hi;
	struct file_info *fi = NULL;
	struct list_head *entry;
	struct arm_addr *arm_addr = NULL;
	struct arm_request *arm_req = NULL;
	struct arm_response *arm_resp = NULL;
	int found = 0, size = 0, rcode = -1;
	quadlet_t old, new;
	struct arm_request_response *arm_req_resp = NULL;

	if (((ext_tcode & 0xFF) == EXTCODE_FETCH_ADD) ||
	    ((ext_tcode & 0xFF) == EXTCODE_LITTLE_ADD)) {
		DBGMSG("arm_lock  called by node: %X "
		       "addr: %4.4x %8.8x extcode: %2.2X data: %8.8X",
		       nodeid, (u16) ((addr >> 32) & 0xFFFF),
		       (u32) (addr & 0xFFFFFFFF), ext_tcode & 0xFF,
		       be32_to_cpu(data));
	} else {
		DBGMSG("arm_lock  called by node: %X "
		       "addr: %4.4x %8.8x extcode: %2.2X data: %8.8X arg: %8.8X",
		       nodeid, (u16) ((addr >> 32) & 0xFFFF),
		       (u32) (addr & 0xFFFFFFFF), ext_tcode & 0xFF,
		       be32_to_cpu(data), be32_to_cpu(arg));
	}
	spin_lock_irqsave(&host_info_lock, irqflags);
	hi = find_host_info(host);	/* search address-entry */
	if (hi != NULL) {
		list_for_each_entry(fi, &hi->file_info_list, list) {
			entry = fi->addr_list.next;
			while (entry != &(fi->addr_list)) {
				arm_addr =
				    list_entry(entry, struct arm_addr,
					       addr_list);
				if (((arm_addr->start) <= (addr))
				    && ((arm_addr->end) >=
					(addr + sizeof(*store)))) {
					found = 1;
					break;
				}
				entry = entry->next;
			}
			if (found) {
				break;
			}
		}
	}
	rcode = -1;
	if (!found) {
		printk(KERN_ERR "raw1394: arm_lock FAILED addr_entry not found"
		       " -> rcode_address_error\n");
		spin_unlock_irqrestore(&host_info_lock, irqflags);
		return (RCODE_ADDRESS_ERROR);
	} else {
		DBGMSG("arm_lock addr_entry FOUND");
	}
	if (rcode == -1) {
		if (arm_addr->access_rights & ARM_LOCK) {
			if (!(arm_addr->client_transactions & ARM_LOCK)) {
				memcpy(&old,
				       (arm_addr->addr_space_buffer) + (addr -
									(arm_addr->
									 start)),
				       sizeof(old));
				switch (ext_tcode) {
				case (EXTCODE_MASK_SWAP):
					new = data | (old & ~arg);
					break;
				case (EXTCODE_COMPARE_SWAP):
					if (old == arg) {
						new = data;
					} else {
						new = old;
					}
					break;
				case (EXTCODE_FETCH_ADD):
					new =
					    cpu_to_be32(be32_to_cpu(data) +
							be32_to_cpu(old));
					break;
				case (EXTCODE_LITTLE_ADD):
					new =
					    cpu_to_le32(le32_to_cpu(data) +
							le32_to_cpu(old));
					break;
				case (EXTCODE_BOUNDED_ADD):
					if (old != arg) {
						new =
						    cpu_to_be32(be32_to_cpu
								(data) +
								be32_to_cpu
								(old));
					} else {
						new = old;
					}
					break;
				case (EXTCODE_WRAP_ADD):
					if (old != arg) {
						new =
						    cpu_to_be32(be32_to_cpu
								(data) +
								be32_to_cpu
								(old));
					} else {
						new = data;
					}
					break;
				default:
					rcode = RCODE_TYPE_ERROR;	/* function not allowed */
					printk(KERN_ERR
					       "raw1394: arm_lock FAILED "
					       "ext_tcode not allowed -> rcode_type_error\n");
					break;
				}	/*switch */
				if (rcode == -1) {
					DBGMSG("arm_lock -> (rcode_complete)");
					rcode = RCODE_COMPLETE;
					memcpy(store, &old, sizeof(*store));
					memcpy((arm_addr->addr_space_buffer) +
					       (addr - (arm_addr->start)),
					       &new, sizeof(*store));
				}
			}
		} else {
			rcode = RCODE_TYPE_ERROR;	/* function not allowed */
			DBGMSG("arm_lock -> rcode_type_error (access denied)");
		}
	}
	if (arm_addr->notification_options & ARM_LOCK) {
		byte_t *buf1, *buf2;
		DBGMSG("arm_lock -> entering notification-section");
		req = __alloc_pending_request(GFP_ATOMIC);
		if (!req) {
			DBGMSG("arm_lock -> rcode_conflict_error");
			spin_unlock_irqrestore(&host_info_lock, irqflags);
			return (RCODE_CONFLICT_ERROR);	/* A resource conflict was detected.
							   The request may be retried */
		}
		size = sizeof(struct arm_request) + sizeof(struct arm_response) + 3 * sizeof(*store) + sizeof(struct arm_request_response);	/* maximum */
		req->data = kmalloc(size, GFP_ATOMIC);
		if (!(req->data)) {
			free_pending_request(req);
			DBGMSG("arm_lock -> rcode_conflict_error");
			spin_unlock_irqrestore(&host_info_lock, irqflags);
			return (RCODE_CONFLICT_ERROR);	/* A resource conflict was detected.
							   The request may be retried */
		}
		req->free_data = 1;
		arm_req_resp = (struct arm_request_response *)(req->data);
		arm_req = (struct arm_request *)((byte_t *) (req->data) +
						 (sizeof
						  (struct
						   arm_request_response)));
		arm_resp =
		    (struct arm_response *)((byte_t *) (arm_req) +
					    (sizeof(struct arm_request)));
		buf1 = (byte_t *) arm_resp + sizeof(struct arm_response);
		buf2 = buf1 + 2 * sizeof(*store);
		if ((ext_tcode == EXTCODE_FETCH_ADD) ||
		    (ext_tcode == EXTCODE_LITTLE_ADD)) {
			arm_req->buffer_length = sizeof(*store);
			memcpy(buf1, &data, sizeof(*store));

		} else {
			arm_req->buffer_length = 2 * sizeof(*store);
			memcpy(buf1, &arg, sizeof(*store));
			memcpy(buf1 + sizeof(*store), &data, sizeof(*store));
		}
		if (rcode == RCODE_COMPLETE) {
			arm_resp->buffer_length = sizeof(*store);
			memcpy(buf2, &old, sizeof(*store));
		} else {
			arm_resp->buffer_length = 0;
		}
		req->file_info = fi;
		req->req.type = RAW1394_REQ_ARM;
		req->req.generation = get_hpsb_generation(host);
		req->req.misc = ((((sizeof(*store)) << 16) & (0xFFFF0000)) |
				 (ARM_LOCK & 0xFF));
		req->req.tag = arm_addr->arm_tag;
		req->req.recvb = arm_addr->recvb;
		req->req.length = size;
		arm_req->generation = req->req.generation;
		arm_req->extended_transaction_code = ext_tcode;
		arm_req->destination_offset = addr;
		arm_req->source_nodeid = nodeid;
		arm_req->destination_nodeid = host->node_id;
		arm_req->tlabel = (flags >> 10) & 0x3f;
		arm_req->tcode = (flags >> 4) & 0x0f;
		arm_resp->response_code = rcode;
		arm_req_resp->request = int2ptr((arm_addr->recvb) +
						sizeof(struct
						       arm_request_response));
		arm_req_resp->response =
		    int2ptr((arm_addr->recvb) +
			    sizeof(struct arm_request_response) +
			    sizeof(struct arm_request));
		arm_req->buffer =
		    int2ptr((arm_addr->recvb) +
			    sizeof(struct arm_request_response) +
			    sizeof(struct arm_request) +
			    sizeof(struct arm_response));
		arm_resp->buffer =
		    int2ptr((arm_addr->recvb) +
			    sizeof(struct arm_request_response) +
			    sizeof(struct arm_request) +
			    sizeof(struct arm_response) + 2 * sizeof(*store));
		queue_complete_req(req);
	}
	spin_unlock_irqrestore(&host_info_lock, irqflags);
	return (rcode);
}

static int arm_lock64(struct hpsb_host *host, int nodeid, octlet_t * store,
		      u64 addr, octlet_t data, octlet_t arg, int ext_tcode,
		      u16 flags)
{
	unsigned long irqflags;
	struct pending_request *req;
	struct host_info *hi;
	struct file_info *fi = NULL;
	struct list_head *entry;
	struct arm_addr *arm_addr = NULL;
	struct arm_request *arm_req = NULL;
	struct arm_response *arm_resp = NULL;
	int found = 0, size = 0, rcode = -1;
	octlet_t old, new;
	struct arm_request_response *arm_req_resp = NULL;

	if (((ext_tcode & 0xFF) == EXTCODE_FETCH_ADD) ||
	    ((ext_tcode & 0xFF) == EXTCODE_LITTLE_ADD)) {
		DBGMSG("arm_lock64 called by node: %X "
		       "addr: %4.4x %8.8x extcode: %2.2X data: %8.8X %8.8X ",
		       nodeid, (u16) ((addr >> 32) & 0xFFFF),
		       (u32) (addr & 0xFFFFFFFF),
		       ext_tcode & 0xFF,
		       (u32) ((be64_to_cpu(data) >> 32) & 0xFFFFFFFF),
		       (u32) (be64_to_cpu(data) & 0xFFFFFFFF));
	} else {
		DBGMSG("arm_lock64 called by node: %X "
		       "addr: %4.4x %8.8x extcode: %2.2X data: %8.8X %8.8X arg: "
		       "%8.8X %8.8X ",
		       nodeid, (u16) ((addr >> 32) & 0xFFFF),
		       (u32) (addr & 0xFFFFFFFF),
		       ext_tcode & 0xFF,
		       (u32) ((be64_to_cpu(data) >> 32) & 0xFFFFFFFF),
		       (u32) (be64_to_cpu(data) & 0xFFFFFFFF),
		       (u32) ((be64_to_cpu(arg) >> 32) & 0xFFFFFFFF),
		       (u32) (be64_to_cpu(arg) & 0xFFFFFFFF));
	}
	spin_lock_irqsave(&host_info_lock, irqflags);
	hi = find_host_info(host);	/* search addressentry in file_info's for host */
	if (hi != NULL) {
		list_for_each_entry(fi, &hi->file_info_list, list) {
			entry = fi->addr_list.next;
			while (entry != &(fi->addr_list)) {
				arm_addr =
				    list_entry(entry, struct arm_addr,
					       addr_list);
				if (((arm_addr->start) <= (addr))
				    && ((arm_addr->end) >=
					(addr + sizeof(*store)))) {
					found = 1;
					break;
				}
				entry = entry->next;
			}
			if (found) {
				break;
			}
		}
	}
	rcode = -1;
	if (!found) {
		printk(KERN_ERR
		       "raw1394: arm_lock64 FAILED addr_entry not found"
		       " -> rcode_address_error\n");
		spin_unlock_irqrestore(&host_info_lock, irqflags);
		return (RCODE_ADDRESS_ERROR);
	} else {
		DBGMSG("arm_lock64 addr_entry FOUND");
	}
	if (rcode == -1) {
		if (arm_addr->access_rights & ARM_LOCK) {
			if (!(arm_addr->client_transactions & ARM_LOCK)) {
				memcpy(&old,
				       (arm_addr->addr_space_buffer) + (addr -
									(arm_addr->
									 start)),
				       sizeof(old));
				switch (ext_tcode) {
				case (EXTCODE_MASK_SWAP):
					new = data | (old & ~arg);
					break;
				case (EXTCODE_COMPARE_SWAP):
					if (old == arg) {
						new = data;
					} else {
						new = old;
					}
					break;
				case (EXTCODE_FETCH_ADD):
					new =
					    cpu_to_be64(be64_to_cpu(data) +
							be64_to_cpu(old));
					break;
				case (EXTCODE_LITTLE_ADD):
					new =
					    cpu_to_le64(le64_to_cpu(data) +
							le64_to_cpu(old));
					break;
				case (EXTCODE_BOUNDED_ADD):
					if (old != arg) {
						new =
						    cpu_to_be64(be64_to_cpu
								(data) +
								be64_to_cpu
								(old));
					} else {
						new = old;
					}
					break;
				case (EXTCODE_WRAP_ADD):
					if (old != arg) {
						new =
						    cpu_to_be64(be64_to_cpu
								(data) +
								be64_to_cpu
								(old));
					} else {
						new = data;
					}
					break;
				default:
					printk(KERN_ERR
					       "raw1394: arm_lock64 FAILED "
					       "ext_tcode not allowed -> rcode_type_error\n");
					rcode = RCODE_TYPE_ERROR;	/* function not allowed */
					break;
				}	/*switch */
				if (rcode == -1) {
					DBGMSG
					    ("arm_lock64 -> (rcode_complete)");
					rcode = RCODE_COMPLETE;
					memcpy(store, &old, sizeof(*store));
					memcpy((arm_addr->addr_space_buffer) +
					       (addr - (arm_addr->start)),
					       &new, sizeof(*store));
				}
			}
		} else {
			rcode = RCODE_TYPE_ERROR;	/* function not allowed */
			DBGMSG
			    ("arm_lock64 -> rcode_type_error (access denied)");
		}
	}
	if (arm_addr->notification_options & ARM_LOCK) {
		byte_t *buf1, *buf2;
		DBGMSG("arm_lock64 -> entering notification-section");
		req = __alloc_pending_request(GFP_ATOMIC);
		if (!req) {
			spin_unlock_irqrestore(&host_info_lock, irqflags);
			DBGMSG("arm_lock64 -> rcode_conflict_error");
			return (RCODE_CONFLICT_ERROR);	/* A resource conflict was detected.
							   The request may be retried */
		}
		size = sizeof(struct arm_request) + sizeof(struct arm_response) + 3 * sizeof(*store) + sizeof(struct arm_request_response);	/* maximum */
		req->data = kmalloc(size, GFP_ATOMIC);
		if (!(req->data)) {
			free_pending_request(req);
			spin_unlock_irqrestore(&host_info_lock, irqflags);
			DBGMSG("arm_lock64 -> rcode_conflict_error");
			return (RCODE_CONFLICT_ERROR);	/* A resource conflict was detected.
							   The request may be retried */
		}
		req->free_data = 1;
		arm_req_resp = (struct arm_request_response *)(req->data);
		arm_req = (struct arm_request *)((byte_t *) (req->data) +
						 (sizeof
						  (struct
						   arm_request_response)));
		arm_resp =
		    (struct arm_response *)((byte_t *) (arm_req) +
					    (sizeof(struct arm_request)));
		buf1 = (byte_t *) arm_resp + sizeof(struct arm_response);
		buf2 = buf1 + 2 * sizeof(*store);
		if ((ext_tcode == EXTCODE_FETCH_ADD) ||
		    (ext_tcode == EXTCODE_LITTLE_ADD)) {
			arm_req->buffer_length = sizeof(*store);
			memcpy(buf1, &data, sizeof(*store));

		} else {
			arm_req->buffer_length = 2 * sizeof(*store);
			memcpy(buf1, &arg, sizeof(*store));
			memcpy(buf1 + sizeof(*store), &data, sizeof(*store));
		}
		if (rcode == RCODE_COMPLETE) {
			arm_resp->buffer_length = sizeof(*store);
			memcpy(buf2, &old, sizeof(*store));
		} else {
			arm_resp->buffer_length = 0;
		}
		req->file_info = fi;
		req->req.type = RAW1394_REQ_ARM;
		req->req.generation = get_hpsb_generation(host);
		req->req.misc = ((((sizeof(*store)) << 16) & (0xFFFF0000)) |
				 (ARM_LOCK & 0xFF));
		req->req.tag = arm_addr->arm_tag;
		req->req.recvb = arm_addr->recvb;
		req->req.length = size;
		arm_req->generation = req->req.generation;
		arm_req->extended_transaction_code = ext_tcode;
		arm_req->destination_offset = addr;
		arm_req->source_nodeid = nodeid;
		arm_req->destination_nodeid = host->node_id;
		arm_req->tlabel = (flags >> 10) & 0x3f;
		arm_req->tcode = (flags >> 4) & 0x0f;
		arm_resp->response_code = rcode;
		arm_req_resp->request = int2ptr((arm_addr->recvb) +
						sizeof(struct
						       arm_request_response));
		arm_req_resp->response =
		    int2ptr((arm_addr->recvb) +
			    sizeof(struct arm_request_response) +
			    sizeof(struct arm_request));
		arm_req->buffer =
		    int2ptr((arm_addr->recvb) +
			    sizeof(struct arm_request_response) +
			    sizeof(struct arm_request) +
			    sizeof(struct arm_response));
		arm_resp->buffer =
		    int2ptr((arm_addr->recvb) +
			    sizeof(struct arm_request_response) +
			    sizeof(struct arm_request) +
			    sizeof(struct arm_response) + 2 * sizeof(*store));
		queue_complete_req(req);
	}
	spin_unlock_irqrestore(&host_info_lock, irqflags);
	return (rcode);
}

static int arm_register(struct file_info *fi, struct pending_request *req)
{
	int retval;
	struct arm_addr *addr;
	struct host_info *hi;
	struct file_info *fi_hlp = NULL;
	struct list_head *entry;
	struct arm_addr *arm_addr = NULL;
	int same_host, another_host;
	unsigned long flags;

	DBGMSG("arm_register called "
	       "addr(Offset): %8.8x %8.8x length: %u "
	       "rights: %2.2X notify: %2.2X "
	       "max_blk_len: %4.4X",
	       (u32) ((req->req.address >> 32) & 0xFFFF),
	       (u32) (req->req.address & 0xFFFFFFFF),
	       req->req.length, ((req->req.misc >> 8) & 0xFF),
	       (req->req.misc & 0xFF), ((req->req.misc >> 16) & 0xFFFF));
	/* check addressrange */
	if ((((req->req.address) & ~(0xFFFFFFFFFFFFULL)) != 0) ||
	    (((req->req.address + req->req.length) & ~(0xFFFFFFFFFFFFULL)) !=
	     0)) {
		req->req.length = 0;
		return (-EINVAL);
	}
	/* addr-list-entry for fileinfo */
	addr = kmalloc(sizeof(*addr), GFP_KERNEL);
	if (!addr) {
		req->req.length = 0;
		return (-ENOMEM);
	}
	/* allocation of addr_space_buffer */
	addr->addr_space_buffer = vmalloc(req->req.length);
	if (!(addr->addr_space_buffer)) {
		kfree(addr);
		req->req.length = 0;
		return (-ENOMEM);
	}
	/* initialization of addr_space_buffer */
	if ((req->req.sendb) == (unsigned long)NULL) {
		/* init: set 0 */
		memset(addr->addr_space_buffer, 0, req->req.length);
	} else {
		/* init: user -> kernel */
		if (copy_from_user
		    (addr->addr_space_buffer, int2ptr(req->req.sendb),
		     req->req.length)) {
			vfree(addr->addr_space_buffer);
			kfree(addr);
			return (-EFAULT);
		}
	}
	INIT_LIST_HEAD(&addr->addr_list);
	addr->arm_tag = req->req.tag;
	addr->start = req->req.address;
	addr->end = req->req.address + req->req.length;
	addr->access_rights = (u8) (req->req.misc & 0x0F);
	addr->notification_options = (u8) ((req->req.misc >> 4) & 0x0F);
	addr->client_transactions = (u8) ((req->req.misc >> 8) & 0x0F);
	addr->access_rights |= addr->client_transactions;
	addr->notification_options |= addr->client_transactions;
	addr->recvb = req->req.recvb;
	addr->rec_length = (u16) ((req->req.misc >> 16) & 0xFFFF);

	spin_lock_irqsave(&host_info_lock, flags);
	hi = find_host_info(fi->host);
	same_host = 0;
	another_host = 0;
	/* same host with address-entry containing same addressrange ? */
	list_for_each_entry(fi_hlp, &hi->file_info_list, list) {
		entry = fi_hlp->addr_list.next;
		while (entry != &(fi_hlp->addr_list)) {
			arm_addr =
			    list_entry(entry, struct arm_addr, addr_list);
			if ((arm_addr->start == addr->start)
			    && (arm_addr->end == addr->end)) {
				DBGMSG("same host ownes same "
				       "addressrange -> EALREADY");
				same_host = 1;
				break;
			}
			entry = entry->next;
		}
		if (same_host) {
			break;
		}
	}
	if (same_host) {
		/* addressrange occupied by same host */
		spin_unlock_irqrestore(&host_info_lock, flags);
		vfree(addr->addr_space_buffer);
		kfree(addr);
		return (-EALREADY);
	}
	/* another host with valid address-entry containing same addressrange */
	list_for_each_entry(hi, &host_info_list, list) {
		if (hi->host != fi->host) {
			list_for_each_entry(fi_hlp, &hi->file_info_list, list) {
				entry = fi_hlp->addr_list.next;
				while (entry != &(fi_hlp->addr_list)) {
					arm_addr =
					    list_entry(entry, struct arm_addr,
						       addr_list);
					if ((arm_addr->start == addr->start)
					    && (arm_addr->end == addr->end)) {
						DBGMSG
						    ("another host ownes same "
						     "addressrange");
						another_host = 1;
						break;
					}
					entry = entry->next;
				}
				if (another_host) {
					break;
				}
			}
		}
	}
	spin_unlock_irqrestore(&host_info_lock, flags);

	if (another_host) {
		DBGMSG("another hosts entry is valid -> SUCCESS");
		if (copy_to_user(int2ptr(req->req.recvb),
				 &addr->start, sizeof(u64))) {
			printk(KERN_ERR "raw1394: arm_register failed "
			       " address-range-entry is invalid -> EFAULT !!!\n");
			vfree(addr->addr_space_buffer);
			kfree(addr);
			return (-EFAULT);
		}
		free_pending_request(req);	/* immediate success or fail */
		/* INSERT ENTRY */
		spin_lock_irqsave(&host_info_lock, flags);
		list_add_tail(&addr->addr_list, &fi->addr_list);
		spin_unlock_irqrestore(&host_info_lock, flags);
		return 0;
	}
	retval =
	    hpsb_register_addrspace(&raw1394_highlevel, fi->host, &arm_ops,
				    req->req.address,
				    req->req.address + req->req.length);
	if (retval) {
		/* INSERT ENTRY */
		spin_lock_irqsave(&host_info_lock, flags);
		list_add_tail(&addr->addr_list, &fi->addr_list);
		spin_unlock_irqrestore(&host_info_lock, flags);
	} else {
		DBGMSG("arm_register failed errno: %d \n", retval);
		vfree(addr->addr_space_buffer);
		kfree(addr);
		return (-EALREADY);
	}
	free_pending_request(req);	/* immediate success or fail */
	return 0;
}

static int arm_unregister(struct file_info *fi, struct pending_request *req)
{
	int found = 0;
	int retval = 0;
	struct list_head *entry;
	struct arm_addr *addr = NULL;
	struct host_info *hi;
	struct file_info *fi_hlp = NULL;
	struct arm_addr *arm_addr = NULL;
	int another_host;
	unsigned long flags;

	DBGMSG("arm_Unregister called addr(Offset): "
	       "%8.8x %8.8x",
	       (u32) ((req->req.address >> 32) & 0xFFFF),
	       (u32) (req->req.address & 0xFFFFFFFF));
	spin_lock_irqsave(&host_info_lock, flags);
	/* get addr */
	entry = fi->addr_list.next;
	while (entry != &(fi->addr_list)) {
		addr = list_entry(entry, struct arm_addr, addr_list);
		if (addr->start == req->req.address) {
			found = 1;
			break;
		}
		entry = entry->next;
	}
	if (!found) {
		DBGMSG("arm_Unregister addr not found");
		spin_unlock_irqrestore(&host_info_lock, flags);
		return (-EINVAL);
	}
	DBGMSG("arm_Unregister addr found");
	another_host = 0;
	/* another host with valid address-entry containing
	   same addressrange */
	list_for_each_entry(hi, &host_info_list, list) {
		if (hi->host != fi->host) {
			list_for_each_entry(fi_hlp, &hi->file_info_list, list) {
				entry = fi_hlp->addr_list.next;
				while (entry != &(fi_hlp->addr_list)) {
					arm_addr = list_entry(entry,
							      struct arm_addr,
							      addr_list);
					if (arm_addr->start == addr->start) {
						DBGMSG("another host ownes "
						       "same addressrange");
						another_host = 1;
						break;
					}
					entry = entry->next;
				}
				if (another_host) {
					break;
				}
			}
		}
	}
	if (another_host) {
		DBGMSG("delete entry from list -> success");
		list_del(&addr->addr_list);
		spin_unlock_irqrestore(&host_info_lock, flags);
		vfree(addr->addr_space_buffer);
		kfree(addr);
		free_pending_request(req);	/* immediate success or fail */
		return 0;
	}
	retval =
	    hpsb_unregister_addrspace(&raw1394_highlevel, fi->host,
				      addr->start);
	if (!retval) {
		printk(KERN_ERR "raw1394: arm_Unregister failed -> EINVAL\n");
		spin_unlock_irqrestore(&host_info_lock, flags);
		return (-EINVAL);
	}
	DBGMSG("delete entry from list -> success");
	list_del(&addr->addr_list);
	spin_unlock_irqrestore(&host_info_lock, flags);
	vfree(addr->addr_space_buffer);
	kfree(addr);
	free_pending_request(req);	/* immediate success or fail */
	return 0;
}

/* Copy data from ARM buffer(s) to user buffer. */
static int arm_get_buf(struct file_info *fi, struct pending_request *req)
{
	struct arm_addr *arm_addr = NULL;
	unsigned long flags;
	unsigned long offset;

	struct list_head *entry;

	DBGMSG("arm_get_buf "
	       "addr(Offset): %04X %08X length: %u",
	       (u32) ((req->req.address >> 32) & 0xFFFF),
	       (u32) (req->req.address & 0xFFFFFFFF), (u32) req->req.length);

	spin_lock_irqsave(&host_info_lock, flags);
	entry = fi->addr_list.next;
	while (entry != &(fi->addr_list)) {
		arm_addr = list_entry(entry, struct arm_addr, addr_list);
		if ((arm_addr->start <= req->req.address) &&
		    (arm_addr->end > req->req.address)) {
			if (req->req.address + req->req.length <= arm_addr->end) {
				offset = req->req.address - arm_addr->start;
				spin_unlock_irqrestore(&host_info_lock, flags);

				DBGMSG
				    ("arm_get_buf copy_to_user( %08X, %p, %u )",
				     (u32) req->req.recvb,
				     arm_addr->addr_space_buffer + offset,
				     (u32) req->req.length);
				if (copy_to_user
				    (int2ptr(req->req.recvb),
				     arm_addr->addr_space_buffer + offset,
				     req->req.length))
					return (-EFAULT);

				/* We have to free the request, because we
				 * queue no response, and therefore nobody
				 * will free it. */
				free_pending_request(req);
				return 0;
			} else {
				DBGMSG("arm_get_buf request exceeded mapping");
				spin_unlock_irqrestore(&host_info_lock, flags);
				return (-EINVAL);
			}
		}
		entry = entry->next;
	}
	spin_unlock_irqrestore(&host_info_lock, flags);
	return (-EINVAL);
}

/* Copy data from user buffer to ARM buffer(s). */
static int arm_set_buf(struct file_info *fi, struct pending_request *req)
{
	struct arm_addr *arm_addr = NULL;
	unsigned long flags;
	unsigned long offset;

	struct list_head *entry;

	DBGMSG("arm_set_buf "
	       "addr(Offset): %04X %08X length: %u",
	       (u32) ((req->req.address >> 32) & 0xFFFF),
	       (u32) (req->req.address & 0xFFFFFFFF), (u32) req->req.length);

	spin_lock_irqsave(&host_info_lock, flags);
	entry = fi->addr_list.next;
	while (entry != &(fi->addr_list)) {
		arm_addr = list_entry(entry, struct arm_addr, addr_list);
		if ((arm_addr->start <= req->req.address) &&
		    (arm_addr->end > req->req.address)) {
			if (req->req.address + req->req.length <= arm_addr->end) {
				offset = req->req.address - arm_addr->start;
				spin_unlock_irqrestore(&host_info_lock, flags);

				DBGMSG
				    ("arm_set_buf copy_from_user( %p, %08X, %u )",
				     arm_addr->addr_space_buffer + offset,
				     (u32) req->req.sendb,
				     (u32) req->req.length);
				if (copy_from_user
				    (arm_addr->addr_space_buffer + offset,
				     int2ptr(req->req.sendb),
				     req->req.length))
					return (-EFAULT);

				/* We have to free the request, because we
				 * queue no response, and therefore nobody
				 * will free it. */
				free_pending_request(req);
				return 0;
			} else {
				DBGMSG("arm_set_buf request exceeded mapping");
				spin_unlock_irqrestore(&host_info_lock, flags);
				return (-EINVAL);
			}
		}
		entry = entry->next;
	}
	spin_unlock_irqrestore(&host_info_lock, flags);
	return (-EINVAL);
}

static int reset_notification(struct file_info *fi, struct pending_request *req)
{
	DBGMSG("reset_notification called - switch %s ",
	       (req->req.misc == RAW1394_NOTIFY_OFF) ? "OFF" : "ON");
	if ((req->req.misc == RAW1394_NOTIFY_OFF) ||
	    (req->req.misc == RAW1394_NOTIFY_ON)) {
		fi->notification = (u8) req->req.misc;
		free_pending_request(req);	/* we have to free the request, because we queue no response, and therefore nobody will free it */
		return 0;
	}
	/* error EINVAL (22) invalid argument */
	return (-EINVAL);
}

static int write_phypacket(struct file_info *fi, struct pending_request *req)
{
	struct hpsb_packet *packet = NULL;
	int retval = 0;
	quadlet_t data;
	unsigned long flags;

	data = be32_to_cpu((u32) req->req.sendb);
	DBGMSG("write_phypacket called - quadlet 0x%8.8x ", data);
	packet = hpsb_make_phypacket(fi->host, data);
	if (!packet)
		return -ENOMEM;
	req->req.length = 0;
	req->packet = packet;
	hpsb_set_packet_complete_task(packet,
				      (void (*)(void *))queue_complete_cb, req);
	spin_lock_irqsave(&fi->reqlists_lock, flags);
	list_add_tail(&req->list, &fi->req_pending);
	spin_unlock_irqrestore(&fi->reqlists_lock, flags);
	packet->generation = req->req.generation;
	retval = hpsb_send_packet(packet);
	DBGMSG("write_phypacket send_packet called => retval: %d ", retval);
	if (retval < 0) {
		req->req.error = RAW1394_ERROR_SEND_ERROR;
		req->req.length = 0;
		queue_complete_req(req);
	}
	return 0;
}

static int get_config_rom(struct file_info *fi, struct pending_request *req)
{
	int ret = 0;
	quadlet_t *data = kmalloc(req->req.length, GFP_KERNEL);
	int status;

	if (!data)
		return -ENOMEM;

	status =
	    csr1212_read(fi->host->csr.rom, CSR1212_CONFIG_ROM_SPACE_OFFSET,
			 data, req->req.length);
	if (copy_to_user(int2ptr(req->req.recvb), data, req->req.length))
		ret = -EFAULT;
	if (copy_to_user
	    (int2ptr(req->req.tag), &fi->host->csr.rom->cache_head->len,
	     sizeof(fi->host->csr.rom->cache_head->len)))
		ret = -EFAULT;
	if (copy_to_user(int2ptr(req->req.address), &fi->host->csr.generation,
			 sizeof(fi->host->csr.generation)))
		ret = -EFAULT;
	if (copy_to_user(int2ptr(req->req.sendb), &status, sizeof(status)))
		ret = -EFAULT;
	kfree(data);
	if (ret >= 0) {
		free_pending_request(req);	/* we have to free the request, because we queue no response, and therefore nobody will free it */
	}
	return ret;
}

static int update_config_rom(struct file_info *fi, struct pending_request *req)
{
	int ret = 0;
	quadlet_t *data = kmalloc(req->req.length, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	if (copy_from_user(data, int2ptr(req->req.sendb), req->req.length)) {
		ret = -EFAULT;
	} else {
		int status = hpsb_update_config_rom(fi->host,
						    data, req->req.length,
						    (unsigned char)req->req.
						    misc);
		if (copy_to_user
		    (int2ptr(req->req.recvb), &status, sizeof(status)))
			ret = -ENOMEM;
	}
	kfree(data);
	if (ret >= 0) {
		free_pending_request(req);	/* we have to free the request, because we queue no response, and therefore nobody will free it */
		fi->cfgrom_upd = 1;
	}
	return ret;
}

static int modify_config_rom(struct file_info *fi, struct pending_request *req)
{
	struct csr1212_keyval *kv;
	struct csr1212_csr_rom_cache *cache;
	struct csr1212_dentry *dentry;
	u32 dr;
	int ret = 0;

	if (req->req.misc == ~0) {
		if (req->req.length == 0)
			return -EINVAL;

		/* Find an unused slot */
		for (dr = 0;
		     dr < RAW1394_MAX_USER_CSR_DIRS && fi->csr1212_dirs[dr];
		     dr++) ;

		if (dr == RAW1394_MAX_USER_CSR_DIRS)
			return -ENOMEM;

		fi->csr1212_dirs[dr] =
		    csr1212_new_directory(CSR1212_KV_ID_VENDOR);
		if (!fi->csr1212_dirs[dr])
			return -ENOMEM;
	} else {
		dr = req->req.misc;
		if (!fi->csr1212_dirs[dr])
			return -EINVAL;

		/* Delete old stuff */
		for (dentry =
		     fi->csr1212_dirs[dr]->value.directory.dentries_head;
		     dentry; dentry = dentry->next) {
			csr1212_detach_keyval_from_directory(fi->host->csr.rom->
							     root_kv,
							     dentry->kv);
		}

		if (req->req.length == 0) {
			csr1212_release_keyval(fi->csr1212_dirs[dr]);
			fi->csr1212_dirs[dr] = NULL;

			hpsb_update_config_rom_image(fi->host);
			free_pending_request(req);
			return 0;
		}
	}

	cache = csr1212_rom_cache_malloc(0, req->req.length);
	if (!cache) {
		csr1212_release_keyval(fi->csr1212_dirs[dr]);
		fi->csr1212_dirs[dr] = NULL;
		return -ENOMEM;
	}

	cache->filled_head = kmalloc(sizeof(*cache->filled_head), GFP_KERNEL);
	if (!cache->filled_head) {
		csr1212_release_keyval(fi->csr1212_dirs[dr]);
		fi->csr1212_dirs[dr] = NULL;
		CSR1212_FREE(cache);
		return -ENOMEM;
	}
	cache->filled_tail = cache->filled_head;

	if (copy_from_user(cache->data, int2ptr(req->req.sendb),
			   req->req.length)) {
		csr1212_release_keyval(fi->csr1212_dirs[dr]);
		fi->csr1212_dirs[dr] = NULL;
		ret = -EFAULT;
	} else {
		cache->len = req->req.length;
		cache->filled_head->offset_start = 0;
		cache->filled_head->offset_end = cache->size - 1;

		cache->layout_head = cache->layout_tail = fi->csr1212_dirs[dr];

		ret = CSR1212_SUCCESS;
		/* parse all the items */
		for (kv = cache->layout_head; ret == CSR1212_SUCCESS && kv;
		     kv = kv->next) {
			ret = csr1212_parse_keyval(kv, cache);
		}

		/* attach top level items to the root directory */
		for (dentry =
		     fi->csr1212_dirs[dr]->value.directory.dentries_head;
		     ret == CSR1212_SUCCESS && dentry; dentry = dentry->next) {
			ret =
			    csr1212_attach_keyval_to_directory(fi->host->csr.
							       rom->root_kv,
							       dentry->kv);
		}

		if (ret == CSR1212_SUCCESS) {
			ret = hpsb_update_config_rom_image(fi->host);

			if (ret >= 0 && copy_to_user(int2ptr(req->req.recvb),
						     &dr, sizeof(dr))) {
				ret = -ENOMEM;
			}
		}
	}
	kfree(cache->filled_head);
	CSR1212_FREE(cache);

	if (ret >= 0) {
		/* we have to free the request, because we queue no response,
		 * and therefore nobody will free it */
		free_pending_request(req);
		return 0;
	} else {
		for (dentry =
		     fi->csr1212_dirs[dr]->value.directory.dentries_head;
		     dentry; dentry = dentry->next) {
			csr1212_detach_keyval_from_directory(fi->host->csr.rom->
							     root_kv,
							     dentry->kv);
		}
		csr1212_release_keyval(fi->csr1212_dirs[dr]);
		fi->csr1212_dirs[dr] = NULL;
		return ret;
	}
}

static int state_connected(struct file_info *fi, struct pending_request *req)
{
	int node = req->req.address >> 48;

	req->req.error = RAW1394_ERROR_NONE;

	switch (req->req.type) {

	case RAW1394_REQ_ECHO:
		queue_complete_req(req);
		return 0;

	case RAW1394_REQ_ARM_REGISTER:
		return arm_register(fi, req);

	case RAW1394_REQ_ARM_UNREGISTER:
		return arm_unregister(fi, req);

	case RAW1394_REQ_ARM_SET_BUF:
		return arm_set_buf(fi, req);

	case RAW1394_REQ_ARM_GET_BUF:
		return arm_get_buf(fi, req);

	case RAW1394_REQ_RESET_NOTIFY:
		return reset_notification(fi, req);

	case RAW1394_REQ_ISO_SEND:
	case RAW1394_REQ_ISO_LISTEN:
		printk(KERN_DEBUG "raw1394: old iso ABI has been removed\n");
		req->req.error = RAW1394_ERROR_COMPAT;
		req->req.misc = RAW1394_KERNELAPI_VERSION;
		queue_complete_req(req);
		return 0;

	case RAW1394_REQ_FCP_LISTEN:
		handle_fcp_listen(fi, req);
		return 0;

	case RAW1394_REQ_RESET_BUS:
		if (req->req.misc == RAW1394_LONG_RESET) {
			DBGMSG("busreset called (type: LONG)");
			hpsb_reset_bus(fi->host, LONG_RESET);
			free_pending_request(req);	/* we have to free the request, because we queue no response, and therefore nobody will free it */
			return 0;
		}
		if (req->req.misc == RAW1394_SHORT_RESET) {
			DBGMSG("busreset called (type: SHORT)");
			hpsb_reset_bus(fi->host, SHORT_RESET);
			free_pending_request(req);	/* we have to free the request, because we queue no response, and therefore nobody will free it */
			return 0;
		}
		/* error EINVAL (22) invalid argument */
		return (-EINVAL);
	case RAW1394_REQ_GET_ROM:
		return get_config_rom(fi, req);

	case RAW1394_REQ_UPDATE_ROM:
		return update_config_rom(fi, req);

	case RAW1394_REQ_MODIFY_ROM:
		return modify_config_rom(fi, req);
	}

	if (req->req.generation != get_hpsb_generation(fi->host)) {
		req->req.error = RAW1394_ERROR_GENERATION;
		req->req.generation = get_hpsb_generation(fi->host);
		req->req.length = 0;
		queue_complete_req(req);
		return 0;
	}

	switch (req->req.type) {
	case RAW1394_REQ_PHYPACKET:
		return write_phypacket(fi, req);
	case RAW1394_REQ_ASYNC_SEND:
		return handle_async_send(fi, req);
	}

	if (req->req.length == 0) {
		req->req.error = RAW1394_ERROR_INVALID_ARG;
		queue_complete_req(req);
		return 0;
	}

	return handle_async_request(fi, req, node);
}

static ssize_t raw1394_write(struct file *file, const char __user * buffer,
			     size_t count, loff_t * offset_is_ignored)
{
	struct file_info *fi = (struct file_info *)file->private_data;
	struct pending_request *req;
	ssize_t retval = -EBADFD;

#ifdef CONFIG_COMPAT
	if (count == sizeof(struct compat_raw1394_req) &&
   	    sizeof(struct compat_raw1394_req) !=
			sizeof(struct raw1394_request)) {
		buffer = raw1394_compat_write(buffer);
		if (IS_ERR((__force void *)buffer))
			return PTR_ERR((__force void *)buffer);
	} else
#endif
	if (count != sizeof(struct raw1394_request)) {
		return -EINVAL;
	}

	req = alloc_pending_request();
	if (req == NULL) {
		return -ENOMEM;
	}
	req->file_info = fi;

	if (copy_from_user(&req->req, buffer, sizeof(struct raw1394_request))) {
		free_pending_request(req);
		return -EFAULT;
	}

	if (!mutex_trylock(&fi->state_mutex)) {
		free_pending_request(req);
		return -EAGAIN;
	}

	switch (fi->state) {
	case opened:
		retval = state_opened(fi, req);
		break;

	case initialized:
		retval = state_initialized(fi, req);
		break;

	case connected:
		retval = state_connected(fi, req);
		break;
	}

	mutex_unlock(&fi->state_mutex);

	if (retval < 0) {
		free_pending_request(req);
	} else {
		BUG_ON(retval);
		retval = count;
	}

	return retval;
}

/* rawiso operations */

/* check if any RAW1394_REQ_RAWISO_ACTIVITY event is already in the
 * completion queue (reqlists_lock must be taken) */
static inline int __rawiso_event_in_queue(struct file_info *fi)
{
	struct pending_request *req;

	list_for_each_entry(req, &fi->req_complete, list)
	    if (req->req.type == RAW1394_REQ_RAWISO_ACTIVITY)
		return 1;

	return 0;
}

/* put a RAWISO_ACTIVITY event in the queue, if one isn't there already */
static void queue_rawiso_event(struct file_info *fi)
{
	unsigned long flags;

	spin_lock_irqsave(&fi->reqlists_lock, flags);

	/* only one ISO activity event may be in the queue */
	if (!__rawiso_event_in_queue(fi)) {
		struct pending_request *req =
		    __alloc_pending_request(GFP_ATOMIC);

		if (req) {
			req->file_info = fi;
			req->req.type = RAW1394_REQ_RAWISO_ACTIVITY;
			req->req.generation = get_hpsb_generation(fi->host);
			__queue_complete_req(req);
		} else {
			/* on allocation failure, signal an overflow */
			if (fi->iso_handle) {
				atomic_inc(&fi->iso_handle->overflows);
			}
		}
	}
	spin_unlock_irqrestore(&fi->reqlists_lock, flags);
}

static void rawiso_activity_cb(struct hpsb_iso *iso)
{
	unsigned long flags;
	struct host_info *hi;
	struct file_info *fi;

	spin_lock_irqsave(&host_info_lock, flags);
	hi = find_host_info(iso->host);

	if (hi != NULL) {
		list_for_each_entry(fi, &hi->file_info_list, list) {
			if (fi->iso_handle == iso)
				queue_rawiso_event(fi);
		}
	}

	spin_unlock_irqrestore(&host_info_lock, flags);
}

/* helper function - gather all the kernel iso status bits for returning to user-space */
static void raw1394_iso_fill_status(struct hpsb_iso *iso,
				    struct raw1394_iso_status *stat)
{
	int overflows = atomic_read(&iso->overflows);
	int skips = atomic_read(&iso->skips);

	stat->config.data_buf_size = iso->buf_size;
	stat->config.buf_packets = iso->buf_packets;
	stat->config.channel = iso->channel;
	stat->config.speed = iso->speed;
	stat->config.irq_interval = iso->irq_interval;
	stat->n_packets = hpsb_iso_n_ready(iso);
	stat->overflows = ((skips & 0xFFFF) << 16) | ((overflows & 0xFFFF));
	stat->xmit_cycle = iso->xmit_cycle;
}

static int raw1394_iso_xmit_init(struct file_info *fi, void __user * uaddr)
{
	struct raw1394_iso_status stat;

	if (!fi->host)
		return -EINVAL;

	if (copy_from_user(&stat, uaddr, sizeof(stat)))
		return -EFAULT;

	fi->iso_handle = hpsb_iso_xmit_init(fi->host,
					    stat.config.data_buf_size,
					    stat.config.buf_packets,
					    stat.config.channel,
					    stat.config.speed,
					    stat.config.irq_interval,
					    rawiso_activity_cb);
	if (!fi->iso_handle)
		return -ENOMEM;

	fi->iso_state = RAW1394_ISO_XMIT;

	raw1394_iso_fill_status(fi->iso_handle, &stat);
	if (copy_to_user(uaddr, &stat, sizeof(stat)))
		return -EFAULT;

	/* queue an event to get things started */
	rawiso_activity_cb(fi->iso_handle);

	return 0;
}

static int raw1394_iso_recv_init(struct file_info *fi, void __user * uaddr)
{
	struct raw1394_iso_status stat;

	if (!fi->host)
		return -EINVAL;

	if (copy_from_user(&stat, uaddr, sizeof(stat)))
		return -EFAULT;

	fi->iso_handle = hpsb_iso_recv_init(fi->host,
					    stat.config.data_buf_size,
					    stat.config.buf_packets,
					    stat.config.channel,
					    stat.config.dma_mode,
					    stat.config.irq_interval,
					    rawiso_activity_cb);
	if (!fi->iso_handle)
		return -ENOMEM;

	fi->iso_state = RAW1394_ISO_RECV;

	raw1394_iso_fill_status(fi->iso_handle, &stat);
	if (copy_to_user(uaddr, &stat, sizeof(stat)))
		return -EFAULT;
	return 0;
}

static int raw1394_iso_get_status(struct file_info *fi, void __user * uaddr)
{
	struct raw1394_iso_status stat;
	struct hpsb_iso *iso = fi->iso_handle;

	raw1394_iso_fill_status(fi->iso_handle, &stat);
	if (copy_to_user(uaddr, &stat, sizeof(stat)))
		return -EFAULT;

	/* reset overflow counter */
	atomic_set(&iso->overflows, 0);
	/* reset skip counter */
	atomic_set(&iso->skips, 0);

	return 0;
}

/* copy N packet_infos out of the ringbuffer into user-supplied array */
static int raw1394_iso_recv_packets(struct file_info *fi, void __user * uaddr)
{
	struct raw1394_iso_packets upackets;
	unsigned int packet = fi->iso_handle->first_packet;
	int i;

	if (copy_from_user(&upackets, uaddr, sizeof(upackets)))
		return -EFAULT;

	if (upackets.n_packets > hpsb_iso_n_ready(fi->iso_handle))
		return -EINVAL;

	/* ensure user-supplied buffer is accessible and big enough */
	if (!access_ok(VERIFY_WRITE, upackets.infos,
		       upackets.n_packets *
		       sizeof(struct raw1394_iso_packet_info)))
		return -EFAULT;

	/* copy the packet_infos out */
	for (i = 0; i < upackets.n_packets; i++) {
		if (__copy_to_user(&upackets.infos[i],
				   &fi->iso_handle->infos[packet],
				   sizeof(struct raw1394_iso_packet_info)))
			return -EFAULT;

		packet = (packet + 1) % fi->iso_handle->buf_packets;
	}

	return 0;
}

/* copy N packet_infos from user to ringbuffer, and queue them for transmission */
static int raw1394_iso_send_packets(struct file_info *fi, void __user * uaddr)
{
	struct raw1394_iso_packets upackets;
	int i, rv;

	if (copy_from_user(&upackets, uaddr, sizeof(upackets)))
		return -EFAULT;

	if (upackets.n_packets >= fi->iso_handle->buf_packets)
		return -EINVAL;

	if (upackets.n_packets >= hpsb_iso_n_ready(fi->iso_handle))
		return -EAGAIN;

	/* ensure user-supplied buffer is accessible and big enough */
	if (!access_ok(VERIFY_READ, upackets.infos,
		       upackets.n_packets *
		       sizeof(struct raw1394_iso_packet_info)))
		return -EFAULT;

	/* copy the infos structs in and queue the packets */
	for (i = 0; i < upackets.n_packets; i++) {
		struct raw1394_iso_packet_info info;

		if (__copy_from_user(&info, &upackets.infos[i],
				     sizeof(struct raw1394_iso_packet_info)))
			return -EFAULT;

		rv = hpsb_iso_xmit_queue_packet(fi->iso_handle, info.offset,
						info.len, info.tag, info.sy);
		if (rv)
			return rv;
	}

	return 0;
}

static void raw1394_iso_shutdown(struct file_info *fi)
{
	if (fi->iso_handle)
		hpsb_iso_shutdown(fi->iso_handle);

	fi->iso_handle = NULL;
	fi->iso_state = RAW1394_ISO_INACTIVE;
}

static int raw1394_read_cycle_timer(struct file_info *fi, void __user * uaddr)
{
	struct raw1394_cycle_timer ct;
	int err;

	err = hpsb_read_cycle_timer(fi->host, &ct.cycle_timer, &ct.local_time);
	if (!err)
		if (copy_to_user(uaddr, &ct, sizeof(ct)))
			err = -EFAULT;
	return err;
}

/* mmap the rawiso xmit/recv buffer */
static int raw1394_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct file_info *fi = file->private_data;
	int ret;

	if (!mutex_trylock(&fi->state_mutex))
		return -EAGAIN;

	if (fi->iso_state == RAW1394_ISO_INACTIVE)
		ret = -EINVAL;
	else
		ret = dma_region_mmap(&fi->iso_handle->data_buf, file, vma);

	mutex_unlock(&fi->state_mutex);

	return ret;
}

static long raw1394_ioctl_inactive(struct file_info *fi, unsigned int cmd,
				   void __user *argp)
{
	switch (cmd) {
	case RAW1394_IOC_ISO_XMIT_INIT:
		return raw1394_iso_xmit_init(fi, argp);
	case RAW1394_IOC_ISO_RECV_INIT:
		return raw1394_iso_recv_init(fi, argp);
	default:
		return -EINVAL;
	}
}

static long raw1394_ioctl_recv(struct file_info *fi, unsigned int cmd,
			       unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case RAW1394_IOC_ISO_RECV_START:{
			int args[3];

			if (copy_from_user(&args[0], argp, sizeof(args)))
				return -EFAULT;
			return hpsb_iso_recv_start(fi->iso_handle,
						   args[0], args[1], args[2]);
		}
	case RAW1394_IOC_ISO_XMIT_RECV_STOP:
		hpsb_iso_stop(fi->iso_handle);
		return 0;
	case RAW1394_IOC_ISO_RECV_LISTEN_CHANNEL:
		return hpsb_iso_recv_listen_channel(fi->iso_handle, arg);
	case RAW1394_IOC_ISO_RECV_UNLISTEN_CHANNEL:
		return hpsb_iso_recv_unlisten_channel(fi->iso_handle, arg);
	case RAW1394_IOC_ISO_RECV_SET_CHANNEL_MASK:{
			u64 mask;

			if (copy_from_user(&mask, argp, sizeof(mask)))
				return -EFAULT;
			return hpsb_iso_recv_set_channel_mask(fi->iso_handle,
							      mask);
		}
	case RAW1394_IOC_ISO_GET_STATUS:
		return raw1394_iso_get_status(fi, argp);
	case RAW1394_IOC_ISO_RECV_PACKETS:
		return raw1394_iso_recv_packets(fi, argp);
	case RAW1394_IOC_ISO_RECV_RELEASE_PACKETS:
		return hpsb_iso_recv_release_packets(fi->iso_handle, arg);
	case RAW1394_IOC_ISO_RECV_FLUSH:
		return hpsb_iso_recv_flush(fi->iso_handle);
	case RAW1394_IOC_ISO_SHUTDOWN:
		raw1394_iso_shutdown(fi);
		return 0;
	case RAW1394_IOC_ISO_QUEUE_ACTIVITY:
		queue_rawiso_event(fi);
		return 0;
	default:
		return -EINVAL;
	}
}

static long raw1394_ioctl_xmit(struct file_info *fi, unsigned int cmd,
			       void __user *argp)
{
	switch (cmd) {
	case RAW1394_IOC_ISO_XMIT_START:{
			int args[2];

			if (copy_from_user(&args[0], argp, sizeof(args)))
				return -EFAULT;
			return hpsb_iso_xmit_start(fi->iso_handle,
						   args[0], args[1]);
		}
	case RAW1394_IOC_ISO_XMIT_SYNC:
		return hpsb_iso_xmit_sync(fi->iso_handle);
	case RAW1394_IOC_ISO_XMIT_RECV_STOP:
		hpsb_iso_stop(fi->iso_handle);
		return 0;
	case RAW1394_IOC_ISO_GET_STATUS:
		return raw1394_iso_get_status(fi, argp);
	case RAW1394_IOC_ISO_XMIT_PACKETS:
		return raw1394_iso_send_packets(fi, argp);
	case RAW1394_IOC_ISO_SHUTDOWN:
		raw1394_iso_shutdown(fi);
		return 0;
	case RAW1394_IOC_ISO_QUEUE_ACTIVITY:
		queue_rawiso_event(fi);
		return 0;
	default:
		return -EINVAL;
	}
}

/* ioctl is only used for rawiso operations */
static long raw1394_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct file_info *fi = file->private_data;
	void __user *argp = (void __user *)arg;
	long ret;

	/* state-independent commands */
	switch(cmd) {
	case RAW1394_IOC_GET_CYCLE_TIMER:
		return raw1394_read_cycle_timer(fi, argp);
	default:
		break;
	}

	if (!mutex_trylock(&fi->state_mutex))
		return -EAGAIN;

	switch (fi->iso_state) {
	case RAW1394_ISO_INACTIVE:
		ret = raw1394_ioctl_inactive(fi, cmd, argp);
		break;
	case RAW1394_ISO_RECV:
		ret = raw1394_ioctl_recv(fi, cmd, arg);
		break;
	case RAW1394_ISO_XMIT:
		ret = raw1394_ioctl_xmit(fi, cmd, argp);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&fi->state_mutex);

	return ret;
}

#ifdef CONFIG_COMPAT
struct raw1394_iso_packets32 {
        __u32 n_packets;
        compat_uptr_t infos;
} __attribute__((packed));

struct raw1394_cycle_timer32 {
        __u32 cycle_timer;
        __u64 local_time;
}
#if defined(CONFIG_X86_64) || defined(CONFIG_IA64)
__attribute__((packed))
#endif
;

#define RAW1394_IOC_ISO_RECV_PACKETS32          \
        _IOW ('#', 0x25, struct raw1394_iso_packets32)
#define RAW1394_IOC_ISO_XMIT_PACKETS32          \
        _IOW ('#', 0x27, struct raw1394_iso_packets32)
#define RAW1394_IOC_GET_CYCLE_TIMER32           \
        _IOR ('#', 0x30, struct raw1394_cycle_timer32)

static long raw1394_iso_xmit_recv_packets32(struct file *file, unsigned int cmd,
                                          struct raw1394_iso_packets32 __user *arg)
{
	compat_uptr_t infos32;
	void __user *infos;
	long err = -EFAULT;
	struct raw1394_iso_packets __user *dst = compat_alloc_user_space(sizeof(struct raw1394_iso_packets));

	if (!copy_in_user(&dst->n_packets, &arg->n_packets, sizeof arg->n_packets) &&
	    !copy_from_user(&infos32, &arg->infos, sizeof infos32)) {
		infos = compat_ptr(infos32);
		if (!copy_to_user(&dst->infos, &infos, sizeof infos))
			err = raw1394_ioctl(file, cmd, (unsigned long)dst);
	}
	return err;
}

static long raw1394_read_cycle_timer32(struct file_info *fi, void __user * uaddr)
{
	struct raw1394_cycle_timer32 ct;
	int err;

	err = hpsb_read_cycle_timer(fi->host, &ct.cycle_timer, &ct.local_time);
	if (!err)
		if (copy_to_user(uaddr, &ct, sizeof(ct)))
			err = -EFAULT;
	return err;
}

static long raw1394_compat_ioctl(struct file *file,
				 unsigned int cmd, unsigned long arg)
{
	struct file_info *fi = file->private_data;
	void __user *argp = (void __user *)arg;
	long err;

	switch (cmd) {
	/* These requests have same format as long as 'int' has same size. */
	case RAW1394_IOC_ISO_RECV_INIT:
	case RAW1394_IOC_ISO_RECV_START:
	case RAW1394_IOC_ISO_RECV_LISTEN_CHANNEL:
	case RAW1394_IOC_ISO_RECV_UNLISTEN_CHANNEL:
	case RAW1394_IOC_ISO_RECV_SET_CHANNEL_MASK:
	case RAW1394_IOC_ISO_RECV_RELEASE_PACKETS:
	case RAW1394_IOC_ISO_RECV_FLUSH:
	case RAW1394_IOC_ISO_XMIT_RECV_STOP:
	case RAW1394_IOC_ISO_XMIT_INIT:
	case RAW1394_IOC_ISO_XMIT_START:
	case RAW1394_IOC_ISO_XMIT_SYNC:
	case RAW1394_IOC_ISO_GET_STATUS:
	case RAW1394_IOC_ISO_SHUTDOWN:
	case RAW1394_IOC_ISO_QUEUE_ACTIVITY:
		err = raw1394_ioctl(file, cmd, arg);
		break;
	/* These request have different format. */
	case RAW1394_IOC_ISO_RECV_PACKETS32:
		err = raw1394_iso_xmit_recv_packets32(file, RAW1394_IOC_ISO_RECV_PACKETS, argp);
		break;
	case RAW1394_IOC_ISO_XMIT_PACKETS32:
		err = raw1394_iso_xmit_recv_packets32(file, RAW1394_IOC_ISO_XMIT_PACKETS, argp);
		break;
	case RAW1394_IOC_GET_CYCLE_TIMER32:
		err = raw1394_read_cycle_timer32(fi, argp);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}
#endif

static unsigned int raw1394_poll(struct file *file, poll_table * pt)
{
	struct file_info *fi = file->private_data;
	unsigned int mask = POLLOUT | POLLWRNORM;
	unsigned long flags;

	poll_wait(file, &fi->wait_complete, pt);

	spin_lock_irqsave(&fi->reqlists_lock, flags);
	if (!list_empty(&fi->req_complete)) {
		mask |= POLLIN | POLLRDNORM;
	}
	spin_unlock_irqrestore(&fi->reqlists_lock, flags);

	return mask;
}

static int raw1394_open(struct inode *inode, struct file *file)
{
	struct file_info *fi;

	fi = kzalloc(sizeof(*fi), GFP_KERNEL);
	if (!fi)
		return -ENOMEM;

	fi->notification = (u8) RAW1394_NOTIFY_ON;	/* busreset notification */

	INIT_LIST_HEAD(&fi->list);
	mutex_init(&fi->state_mutex);
	fi->state = opened;
	INIT_LIST_HEAD(&fi->req_pending);
	INIT_LIST_HEAD(&fi->req_complete);
	spin_lock_init(&fi->reqlists_lock);
	init_waitqueue_head(&fi->wait_complete);
	INIT_LIST_HEAD(&fi->addr_list);

	file->private_data = fi;

	return nonseekable_open(inode, file);
}

static int raw1394_release(struct inode *inode, struct file *file)
{
	struct file_info *fi = file->private_data;
	struct list_head *lh;
	struct pending_request *req;
	int i, fail;
	int retval = 0;
	struct list_head *entry;
	struct arm_addr *addr = NULL;
	struct host_info *hi;
	struct file_info *fi_hlp = NULL;
	struct arm_addr *arm_addr = NULL;
	int another_host;
	int csr_mod = 0;
	unsigned long flags;

	if (fi->iso_state != RAW1394_ISO_INACTIVE)
		raw1394_iso_shutdown(fi);

	spin_lock_irqsave(&host_info_lock, flags);

	fail = 0;
	/* set address-entries invalid */

	while (!list_empty(&fi->addr_list)) {
		another_host = 0;
		lh = fi->addr_list.next;
		addr = list_entry(lh, struct arm_addr, addr_list);
		/* another host with valid address-entry containing
		   same addressrange? */
		list_for_each_entry(hi, &host_info_list, list) {
			if (hi->host != fi->host) {
				list_for_each_entry(fi_hlp, &hi->file_info_list,
						    list) {
					entry = fi_hlp->addr_list.next;
					while (entry != &(fi_hlp->addr_list)) {
						arm_addr = list_entry(entry, struct
								      arm_addr,
								      addr_list);
						if (arm_addr->start ==
						    addr->start) {
							DBGMSG
							    ("raw1394_release: "
							     "another host ownes "
							     "same addressrange");
							another_host = 1;
							break;
						}
						entry = entry->next;
					}
					if (another_host) {
						break;
					}
				}
			}
		}
		if (!another_host) {
			DBGMSG("raw1394_release: call hpsb_arm_unregister");
			retval =
			    hpsb_unregister_addrspace(&raw1394_highlevel,
						      fi->host, addr->start);
			if (!retval) {
				++fail;
				printk(KERN_ERR
				       "raw1394_release arm_Unregister failed\n");
			}
		}
		DBGMSG("raw1394_release: delete addr_entry from list");
		list_del(&addr->addr_list);
		vfree(addr->addr_space_buffer);
		kfree(addr);
	}			/* while */
	spin_unlock_irqrestore(&host_info_lock, flags);
	if (fail > 0) {
		printk(KERN_ERR "raw1394: during addr_list-release "
		       "error(s) occurred \n");
	}

	for (;;) {
		/* This locked section guarantees that neither
		 * complete nor pending requests exist once i!=0 */
		spin_lock_irqsave(&fi->reqlists_lock, flags);
		while ((req = __next_complete_req(fi)))
			free_pending_request(req);

		i = list_empty(&fi->req_pending);
		spin_unlock_irqrestore(&fi->reqlists_lock, flags);

		if (i)
			break;
		/*
		 * Sleep until more requests can be freed.
		 *
		 * NB: We call the macro wait_event() with a condition argument
		 * with side effect.  This is only possible because the side
		 * effect does not occur until the condition became true, and
		 * wait_event() won't evaluate the condition again after that.
		 */
		wait_event(fi->wait_complete, (req = next_complete_req(fi)));
		free_pending_request(req);
	}

	/* Remove any sub-trees left by user space programs */
	for (i = 0; i < RAW1394_MAX_USER_CSR_DIRS; i++) {
		struct csr1212_dentry *dentry;
		if (!fi->csr1212_dirs[i])
			continue;
		for (dentry =
		     fi->csr1212_dirs[i]->value.directory.dentries_head; dentry;
		     dentry = dentry->next) {
			csr1212_detach_keyval_from_directory(fi->host->csr.rom->
							     root_kv,
							     dentry->kv);
		}
		csr1212_release_keyval(fi->csr1212_dirs[i]);
		fi->csr1212_dirs[i] = NULL;
		csr_mod = 1;
	}

	if ((csr_mod || fi->cfgrom_upd)
	    && hpsb_update_config_rom_image(fi->host) < 0)
		HPSB_ERR
		    ("Failed to generate Configuration ROM image for host %d",
		     fi->host->id);

	if (fi->state == connected) {
		spin_lock_irqsave(&host_info_lock, flags);
		list_del(&fi->list);
		spin_unlock_irqrestore(&host_info_lock, flags);

		put_device(&fi->host->device);
	}

	spin_lock_irqsave(&host_info_lock, flags);
	if (fi->host)
		module_put(fi->host->driver->owner);
	spin_unlock_irqrestore(&host_info_lock, flags);

	kfree(fi);

	return 0;
}

/*** HOTPLUG STUFF **********************************************************/
/*
 * Export information about protocols/devices supported by this driver.
 */
#ifdef MODULE
static const struct ieee1394_device_id raw1394_id_table[] = {
	{
	 .match_flags = IEEE1394_MATCH_SPECIFIER_ID | IEEE1394_MATCH_VERSION,
	 .specifier_id = AVC_UNIT_SPEC_ID_ENTRY & 0xffffff,
	 .version = AVC_SW_VERSION_ENTRY & 0xffffff},
	{
	 .match_flags = IEEE1394_MATCH_SPECIFIER_ID | IEEE1394_MATCH_VERSION,
	 .specifier_id = CAMERA_UNIT_SPEC_ID_ENTRY & 0xffffff,
	 .version = CAMERA_SW_VERSION_ENTRY & 0xffffff},
	{
	 .match_flags = IEEE1394_MATCH_SPECIFIER_ID | IEEE1394_MATCH_VERSION,
	 .specifier_id = CAMERA_UNIT_SPEC_ID_ENTRY & 0xffffff,
	 .version = (CAMERA_SW_VERSION_ENTRY + 1) & 0xffffff},
	{
	 .match_flags = IEEE1394_MATCH_SPECIFIER_ID | IEEE1394_MATCH_VERSION,
	 .specifier_id = CAMERA_UNIT_SPEC_ID_ENTRY & 0xffffff,
	 .version = (CAMERA_SW_VERSION_ENTRY + 2) & 0xffffff},
	{}
};

MODULE_DEVICE_TABLE(ieee1394, raw1394_id_table);
#endif /* MODULE */

static struct hpsb_protocol_driver raw1394_driver = {
	.name = "raw1394",
};

/******************************************************************************/

static struct hpsb_highlevel raw1394_highlevel = {
	.name = RAW1394_DEVICE_NAME,
	.add_host = add_host,
	.remove_host = remove_host,
	.host_reset = host_reset,
	.fcp_request = fcp_request,
};

static struct cdev raw1394_cdev;
static const struct file_operations raw1394_fops = {
	.owner = THIS_MODULE,
	.read = raw1394_read,
	.write = raw1394_write,
	.mmap = raw1394_mmap,
	.unlocked_ioctl = raw1394_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = raw1394_compat_ioctl,
#endif
	.poll = raw1394_poll,
	.open = raw1394_open,
	.release = raw1394_release,
	.llseek = no_llseek,
};

static int __init init_raw1394(void)
{
	int ret = 0;

	hpsb_register_highlevel(&raw1394_highlevel);

	if (IS_ERR
	    (device_create(hpsb_protocol_class, NULL,
			   MKDEV(IEEE1394_MAJOR,
				 IEEE1394_MINOR_BLOCK_RAW1394 * 16),
			   NULL, RAW1394_DEVICE_NAME))) {
		ret = -EFAULT;
		goto out_unreg;
	}

	cdev_init(&raw1394_cdev, &raw1394_fops);
	raw1394_cdev.owner = THIS_MODULE;
	ret = cdev_add(&raw1394_cdev, IEEE1394_RAW1394_DEV, 1);
	if (ret) {
		HPSB_ERR("raw1394 failed to register minor device block");
		goto out_dev;
	}

	HPSB_INFO("raw1394: /dev/%s device initialized", RAW1394_DEVICE_NAME);

	ret = hpsb_register_protocol(&raw1394_driver);
	if (ret) {
		HPSB_ERR("raw1394: failed to register protocol");
		cdev_del(&raw1394_cdev);
		goto out_dev;
	}

	goto out;

      out_dev:
	device_destroy(hpsb_protocol_class,
		       MKDEV(IEEE1394_MAJOR,
			     IEEE1394_MINOR_BLOCK_RAW1394 * 16));
      out_unreg:
	hpsb_unregister_highlevel(&raw1394_highlevel);
      out:
	return ret;
}

static void __exit cleanup_raw1394(void)
{
	device_destroy(hpsb_protocol_class,
		       MKDEV(IEEE1394_MAJOR,
			     IEEE1394_MINOR_BLOCK_RAW1394 * 16));
	cdev_del(&raw1394_cdev);
	hpsb_unregister_highlevel(&raw1394_highlevel);
	hpsb_unregister_protocol(&raw1394_driver);
}

module_init(init_raw1394);
module_exit(cleanup_raw1394);
MODULE_LICENSE("GPL");

/*
 * Copyright (c) 2011-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _NVME_H
#define _NVME_H

#include <linux/nvme.h>
#include <linux/pci.h>
#include <linux/kref.h>
#include <linux/blk-mq.h>

enum {
	/*
	 * Driver internal status code for commands that were cancelled due
	 * to timeouts or controller shutdown.  The value is negative so
	 * that it a) doesn't overlap with the unsigned hardware error codes,
	 * and b) can easily be tested for.
	 */
	NVME_SC_CANCELLED		= -EINTR,
};

extern unsigned char nvme_io_timeout;
#define NVME_IO_TIMEOUT	(nvme_io_timeout * HZ)

extern unsigned char admin_timeout;
#define ADMIN_TIMEOUT	(admin_timeout * HZ)

extern unsigned char shutdown_timeout;
#define SHUTDOWN_TIMEOUT	(shutdown_timeout * HZ)

enum {
	NVME_NS_LBA		= 0,
	NVME_NS_LIGHTNVM	= 1,
};

/*
 * List of workarounds for devices that required behavior not specified in
 * the standard.
 */
enum nvme_quirks {
	/*
	 * Prefers I/O aligned to a stripe size specified in a vendor
	 * specific Identify field.
	 */
	NVME_QUIRK_STRIPE_SIZE			= (1 << 0),

	/*
	 * The controller doesn't handle Identify value others than 0 or 1
	 * correctly.
	 */
	NVME_QUIRK_IDENTIFY_CNS			= (1 << 1),

	/*
	 * The controller deterministically returns O's on reads to discarded
	 * logical blocks.
	 */
	NVME_QUIRK_DISCARD_ZEROES		= (1 << 2),
};

enum nvme_ctrl_state {
	NVME_CTRL_NEW,
	NVME_CTRL_LIVE,
	NVME_CTRL_RESETTING,
	NVME_CTRL_DELETING,
};

struct nvme_ctrl {
	enum nvme_ctrl_state state;
	spinlock_t lock;
	const struct nvme_ctrl_ops *ops;
	struct request_queue *admin_q;
	struct device *dev;
	struct kref kref;
	int instance;
	struct blk_mq_tag_set *tagset;
	struct list_head namespaces;
	struct mutex namespaces_mutex;
	struct device *device;	/* char device */
	struct list_head node;
	struct ida ns_ida;

	char name[12];
	char serial[20];
	char model[40];
	char firmware_rev[8];
	u16 cntlid;

	u32 ctrl_config;

	u32 page_size;
	u32 max_hw_sectors;
	u32 stripe_size;
	u16 oncs;
	u16 vid;
	atomic_t abort_limit;
	u8 event_limit;
	u8 vwc;
	u32 vs;
	bool subsystem;
	unsigned long quirks;
	struct work_struct scan_work;
	struct work_struct async_event_work;
};

/*
 * An NVM Express namespace is equivalent to a SCSI LUN
 */
struct nvme_ns {
	struct list_head list;

	struct nvme_ctrl *ctrl;
	struct request_queue *queue;
	struct gendisk *disk;
	struct kref kref;
	int instance;

	u8 eui[8];
	u8 uuid[16];

	unsigned ns_id;
	int lba_shift;
	u16 ms;
	bool ext;
	u8 pi_type;
	int type;
	unsigned long flags;

#define NVME_NS_REMOVING 0
#define NVME_NS_DEAD     1

	u64 mode_select_num_blocks;
	u32 mode_select_block_len;
};

struct nvme_ctrl_ops {
	struct module *module;
	int (*reg_read32)(struct nvme_ctrl *ctrl, u32 off, u32 *val);
	int (*reg_write32)(struct nvme_ctrl *ctrl, u32 off, u32 val);
	int (*reg_read64)(struct nvme_ctrl *ctrl, u32 off, u64 *val);
	int (*reset_ctrl)(struct nvme_ctrl *ctrl);
	void (*free_ctrl)(struct nvme_ctrl *ctrl);
	void (*post_scan)(struct nvme_ctrl *ctrl);
	void (*submit_async_event)(struct nvme_ctrl *ctrl, int aer_idx);
};

static inline bool nvme_ctrl_ready(struct nvme_ctrl *ctrl)
{
	u32 val = 0;

	if (ctrl->ops->reg_read32(ctrl, NVME_REG_CSTS, &val))
		return false;
	return val & NVME_CSTS_RDY;
}

static inline int nvme_reset_subsystem(struct nvme_ctrl *ctrl)
{
	if (!ctrl->subsystem)
		return -ENOTTY;
	return ctrl->ops->reg_write32(ctrl, NVME_REG_NSSR, 0x4E564D65);
}

static inline u64 nvme_block_nr(struct nvme_ns *ns, sector_t sector)
{
	return (sector >> (ns->lba_shift - 9));
}

static inline unsigned nvme_map_len(struct request *rq)
{
	if (rq->cmd_flags & REQ_DISCARD)
		return sizeof(struct nvme_dsm_range);
	else
		return blk_rq_bytes(rq);
}

static inline void nvme_cleanup_cmd(struct request *req)
{
	if (req->cmd_flags & REQ_DISCARD)
		kfree(req->completion_data);
}

static inline int nvme_error_status(u16 status)
{
	switch (status & 0x7ff) {
	case NVME_SC_SUCCESS:
		return 0;
	case NVME_SC_CAP_EXCEEDED:
		return -ENOSPC;
	default:
		return -EIO;
	}
}

static inline bool nvme_req_needs_retry(struct request *req, u16 status)
{
	return !(status & NVME_SC_DNR || blk_noretry_request(req)) &&
		(jiffies - req->start_time) < req->timeout;
}

bool nvme_change_ctrl_state(struct nvme_ctrl *ctrl,
		enum nvme_ctrl_state new_state);
int nvme_disable_ctrl(struct nvme_ctrl *ctrl, u64 cap);
int nvme_enable_ctrl(struct nvme_ctrl *ctrl, u64 cap);
int nvme_shutdown_ctrl(struct nvme_ctrl *ctrl);
int nvme_init_ctrl(struct nvme_ctrl *ctrl, struct device *dev,
		const struct nvme_ctrl_ops *ops, unsigned long quirks);
void nvme_uninit_ctrl(struct nvme_ctrl *ctrl);
void nvme_put_ctrl(struct nvme_ctrl *ctrl);
int nvme_init_identify(struct nvme_ctrl *ctrl);

void nvme_queue_scan(struct nvme_ctrl *ctrl);
void nvme_remove_namespaces(struct nvme_ctrl *ctrl);

#define NVME_NR_AERS	1
void nvme_complete_async_event(struct nvme_ctrl *ctrl,
		struct nvme_completion *cqe);
void nvme_queue_async_events(struct nvme_ctrl *ctrl);

void nvme_stop_queues(struct nvme_ctrl *ctrl);
void nvme_start_queues(struct nvme_ctrl *ctrl);
void nvme_kill_queues(struct nvme_ctrl *ctrl);

struct request *nvme_alloc_request(struct request_queue *q,
		struct nvme_command *cmd, unsigned int flags);
void nvme_requeue_req(struct request *req);
int nvme_setup_cmd(struct nvme_ns *ns, struct request *req,
		struct nvme_command *cmd);
int nvme_submit_sync_cmd(struct request_queue *q, struct nvme_command *cmd,
		void *buf, unsigned bufflen);
int __nvme_submit_sync_cmd(struct request_queue *q, struct nvme_command *cmd,
		struct nvme_completion *cqe, void *buffer, unsigned bufflen,
		unsigned timeout);
int nvme_submit_user_cmd(struct request_queue *q, struct nvme_command *cmd,
		void __user *ubuffer, unsigned bufflen, u32 *result,
		unsigned timeout);
int __nvme_submit_user_cmd(struct request_queue *q, struct nvme_command *cmd,
		void __user *ubuffer, unsigned bufflen,
		void __user *meta_buffer, unsigned meta_len, u32 meta_seed,
		u32 *result, unsigned timeout);
int nvme_identify_ctrl(struct nvme_ctrl *dev, struct nvme_id_ctrl **id);
int nvme_identify_ns(struct nvme_ctrl *dev, unsigned nsid,
		struct nvme_id_ns **id);
int nvme_get_log_page(struct nvme_ctrl *dev, struct nvme_smart_log **log);
int nvme_get_features(struct nvme_ctrl *dev, unsigned fid, unsigned nsid,
			dma_addr_t dma_addr, u32 *result);
int nvme_set_features(struct nvme_ctrl *dev, unsigned fid, unsigned dword11,
			dma_addr_t dma_addr, u32 *result);
int nvme_set_queue_count(struct nvme_ctrl *ctrl, int *count);

struct sg_io_hdr;

int nvme_sg_io(struct nvme_ns *ns, struct sg_io_hdr __user *u_hdr);
int nvme_sg_io32(struct nvme_ns *ns, unsigned long arg);
int nvme_sg_get_version_num(int __user *ip);

#ifdef CONFIG_NVM
int nvme_nvm_ns_supported(struct nvme_ns *ns, struct nvme_id_ns *id);
int nvme_nvm_register(struct request_queue *q, char *disk_name);
void nvme_nvm_unregister(struct request_queue *q, char *disk_name);
#else
static inline int nvme_nvm_register(struct request_queue *q, char *disk_name)
{
	return 0;
}

static inline void nvme_nvm_unregister(struct request_queue *q, char *disk_name) {};

static inline int nvme_nvm_ns_supported(struct nvme_ns *ns, struct nvme_id_ns *id)
{
	return 0;
}
#endif /* CONFIG_NVM */

int __init nvme_core_init(void);
void nvme_core_exit(void);

#endif /* _NVME_H */

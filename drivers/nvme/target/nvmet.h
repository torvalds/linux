/*
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
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

#ifndef _NVMET_H
#define _NVMET_H

#include <linux/dma-mapping.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/kref.h>
#include <linux/percpu-refcount.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/nvme.h>
#include <linux/configfs.h>
#include <linux/rcupdate.h>
#include <linux/blkdev.h>

#define NVMET_ASYNC_EVENTS		4
#define NVMET_ERROR_LOG_SLOTS		128

/* Helper Macros when NVMe error is NVME_SC_CONNECT_INVALID_PARAM
 * The 16 bit shift is to set IATTR bit to 1, which means offending
 * offset starts in the data section of connect()
 */
#define IPO_IATTR_CONNECT_DATA(x)	\
	(cpu_to_le32((1 << 16) | (offsetof(struct nvmf_connect_data, x))))
#define IPO_IATTR_CONNECT_SQE(x)	\
	(cpu_to_le32(offsetof(struct nvmf_connect_command, x)))

struct nvmet_ns {
	struct list_head	dev_link;
	struct percpu_ref	ref;
	struct block_device	*bdev;
	u32			nsid;
	u32			blksize_shift;
	loff_t			size;
	u8			nguid[16];

	bool			enabled;
	struct nvmet_subsys	*subsys;
	const char		*device_path;

	struct config_group	device_group;
	struct config_group	group;

	struct completion	disable_done;
};

static inline struct nvmet_ns *to_nvmet_ns(struct config_item *item)
{
	return container_of(to_config_group(item), struct nvmet_ns, group);
}

struct nvmet_cq {
	u16			qid;
	u16			size;
};

struct nvmet_sq {
	struct nvmet_ctrl	*ctrl;
	struct percpu_ref	ref;
	u16			qid;
	u16			size;
	struct completion	free_done;
};

/**
 * struct nvmet_port -	Common structure to keep port
 *				information for the target.
 * @entry:		List head for holding a list of these elements.
 * @disc_addr:		Address information is stored in a format defined
 *				for a discovery log page entry.
 * @group:		ConfigFS group for this element's folder.
 * @priv:		Private data for the transport.
 */
struct nvmet_port {
	struct list_head		entry;
	struct nvmf_disc_rsp_page_entry	disc_addr;
	struct config_group		group;
	struct config_group		subsys_group;
	struct list_head		subsystems;
	struct config_group		referrals_group;
	struct list_head		referrals;
	void				*priv;
	bool				enabled;
};

static inline struct nvmet_port *to_nvmet_port(struct config_item *item)
{
	return container_of(to_config_group(item), struct nvmet_port,
			group);
}

struct nvmet_ctrl {
	struct nvmet_subsys	*subsys;
	struct nvmet_cq		**cqs;
	struct nvmet_sq		**sqs;

	struct mutex		lock;
	u64			cap;
	u64			serial;
	u32			cc;
	u32			csts;

	u16			cntlid;
	u32			kato;

	struct nvmet_req	*async_event_cmds[NVMET_ASYNC_EVENTS];
	unsigned int		nr_async_event_cmds;
	struct list_head	async_events;
	struct work_struct	async_event_work;

	struct list_head	subsys_entry;
	struct kref		ref;
	struct delayed_work	ka_work;
	struct work_struct	fatal_err_work;

	struct nvmet_fabrics_ops *ops;

	char			subsysnqn[NVMF_NQN_FIELD_LEN];
	char			hostnqn[NVMF_NQN_FIELD_LEN];
};

struct nvmet_subsys {
	enum nvme_subsys_type	type;

	struct mutex		lock;
	struct kref		ref;

	struct list_head	namespaces;
	unsigned int		max_nsid;

	struct list_head	ctrls;
	struct ida		cntlid_ida;

	struct list_head	hosts;
	bool			allow_any_host;

	u16			max_qid;

	u64			ver;
	char			*subsysnqn;

	struct config_group	group;

	struct config_group	namespaces_group;
	struct config_group	allowed_hosts_group;
};

static inline struct nvmet_subsys *to_subsys(struct config_item *item)
{
	return container_of(to_config_group(item), struct nvmet_subsys, group);
}

static inline struct nvmet_subsys *namespaces_to_subsys(
		struct config_item *item)
{
	return container_of(to_config_group(item), struct nvmet_subsys,
			namespaces_group);
}

struct nvmet_host {
	struct config_group	group;
};

static inline struct nvmet_host *to_host(struct config_item *item)
{
	return container_of(to_config_group(item), struct nvmet_host, group);
}

static inline char *nvmet_host_name(struct nvmet_host *host)
{
	return config_item_name(&host->group.cg_item);
}

struct nvmet_host_link {
	struct list_head	entry;
	struct nvmet_host	*host;
};

struct nvmet_subsys_link {
	struct list_head	entry;
	struct nvmet_subsys	*subsys;
};

struct nvmet_req;
struct nvmet_fabrics_ops {
	struct module *owner;
	unsigned int type;
	unsigned int sqe_inline_size;
	unsigned int msdbd;
	bool has_keyed_sgls : 1;
	void (*queue_response)(struct nvmet_req *req);
	int (*add_port)(struct nvmet_port *port);
	void (*remove_port)(struct nvmet_port *port);
	void (*delete_ctrl)(struct nvmet_ctrl *ctrl);
};

#define NVMET_MAX_INLINE_BIOVEC	8

struct nvmet_req {
	struct nvme_command	*cmd;
	struct nvme_completion	*rsp;
	struct nvmet_sq		*sq;
	struct nvmet_cq		*cq;
	struct nvmet_ns		*ns;
	struct scatterlist	*sg;
	struct bio		inline_bio;
	struct bio_vec		inline_bvec[NVMET_MAX_INLINE_BIOVEC];
	int			sg_cnt;
	size_t			data_len;

	struct nvmet_port	*port;

	void (*execute)(struct nvmet_req *req);
	struct nvmet_fabrics_ops *ops;
};

static inline void nvmet_set_status(struct nvmet_req *req, u16 status)
{
	req->rsp->status = cpu_to_le16(status << 1);
}

static inline void nvmet_set_result(struct nvmet_req *req, u32 result)
{
	req->rsp->result.u32 = cpu_to_le32(result);
}

/*
 * NVMe command writes actually are DMA reads for us on the target side.
 */
static inline enum dma_data_direction
nvmet_data_dir(struct nvmet_req *req)
{
	return nvme_is_write(req->cmd) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
}

struct nvmet_async_event {
	struct list_head	entry;
	u8			event_type;
	u8			event_info;
	u8			log_page;
};

int nvmet_parse_connect_cmd(struct nvmet_req *req);
int nvmet_parse_io_cmd(struct nvmet_req *req);
int nvmet_parse_admin_cmd(struct nvmet_req *req);
int nvmet_parse_discovery_cmd(struct nvmet_req *req);
int nvmet_parse_fabrics_cmd(struct nvmet_req *req);

bool nvmet_req_init(struct nvmet_req *req, struct nvmet_cq *cq,
		struct nvmet_sq *sq, struct nvmet_fabrics_ops *ops);
void nvmet_req_complete(struct nvmet_req *req, u16 status);

void nvmet_cq_setup(struct nvmet_ctrl *ctrl, struct nvmet_cq *cq, u16 qid,
		u16 size);
void nvmet_sq_setup(struct nvmet_ctrl *ctrl, struct nvmet_sq *sq, u16 qid,
		u16 size);
void nvmet_sq_destroy(struct nvmet_sq *sq);
int nvmet_sq_init(struct nvmet_sq *sq);

void nvmet_ctrl_fatal_error(struct nvmet_ctrl *ctrl);

void nvmet_update_cc(struct nvmet_ctrl *ctrl, u32 new);
u16 nvmet_alloc_ctrl(const char *subsysnqn, const char *hostnqn,
		struct nvmet_req *req, u32 kato, struct nvmet_ctrl **ctrlp);
u16 nvmet_ctrl_find_get(const char *subsysnqn, const char *hostnqn, u16 cntlid,
		struct nvmet_req *req, struct nvmet_ctrl **ret);
void nvmet_ctrl_put(struct nvmet_ctrl *ctrl);

struct nvmet_subsys *nvmet_subsys_alloc(const char *subsysnqn,
		enum nvme_subsys_type type);
void nvmet_subsys_put(struct nvmet_subsys *subsys);

struct nvmet_ns *nvmet_find_namespace(struct nvmet_ctrl *ctrl, __le32 nsid);
void nvmet_put_namespace(struct nvmet_ns *ns);
int nvmet_ns_enable(struct nvmet_ns *ns);
void nvmet_ns_disable(struct nvmet_ns *ns);
struct nvmet_ns *nvmet_ns_alloc(struct nvmet_subsys *subsys, u32 nsid);
void nvmet_ns_free(struct nvmet_ns *ns);

int nvmet_register_transport(struct nvmet_fabrics_ops *ops);
void nvmet_unregister_transport(struct nvmet_fabrics_ops *ops);

int nvmet_enable_port(struct nvmet_port *port);
void nvmet_disable_port(struct nvmet_port *port);

void nvmet_referral_enable(struct nvmet_port *parent, struct nvmet_port *port);
void nvmet_referral_disable(struct nvmet_port *port);

u16 nvmet_copy_to_sgl(struct nvmet_req *req, off_t off, const void *buf,
		size_t len);
u16 nvmet_copy_from_sgl(struct nvmet_req *req, off_t off, void *buf,
		size_t len);

u32 nvmet_get_log_page_len(struct nvme_command *cmd);

#define NVMET_QUEUE_SIZE	1024
#define NVMET_NR_QUEUES		64
#define NVMET_MAX_CMD		NVMET_QUEUE_SIZE
#define NVMET_KAS		10
#define NVMET_DISC_KATO		120

int __init nvmet_init_configfs(void);
void __exit nvmet_exit_configfs(void);

int __init nvmet_init_discovery(void);
void nvmet_exit_discovery(void);

extern struct nvmet_subsys *nvmet_disc_subsys;
extern u64 nvmet_genctr;
extern struct rw_semaphore nvmet_config_sem;

bool nvmet_host_allowed(struct nvmet_req *req, struct nvmet_subsys *subsys,
		const char *hostnqn);

#endif /* _NVMET_H */

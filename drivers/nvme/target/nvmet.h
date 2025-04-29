/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
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
#include <linux/uuid.h>
#include <linux/nvme.h>
#include <linux/configfs.h>
#include <linux/rcupdate.h>
#include <linux/blkdev.h>
#include <linux/radix-tree.h>
#include <linux/t10-pi.h>
#include <linux/kfifo.h>

#define NVMET_DEFAULT_VS		NVME_VS(2, 1, 0)

#define NVMET_NS_ENABLED		XA_MARK_1
#define NVMET_ASYNC_EVENTS		4
#define NVMET_ERROR_LOG_SLOTS		128
#define NVMET_NO_ERROR_LOC		((u16)-1)
#define NVMET_DEFAULT_CTRL_MODEL	"Linux"
#define NVMET_MN_MAX_SIZE		40
#define NVMET_SN_MAX_SIZE		20
#define NVMET_FR_MAX_SIZE		8
#define NVMET_PR_LOG_QUEUE_SIZE		64

#define nvmet_for_each_ns(xa, index, entry) \
	xa_for_each(xa, index, entry)

#define nvmet_for_each_enabled_ns(xa, index, entry) \
	xa_for_each_marked(xa, index, entry, NVMET_NS_ENABLED)

/*
 * Supported optional AENs:
 */
#define NVMET_AEN_CFG_OPTIONAL \
	(NVME_AEN_CFG_NS_ATTR | NVME_AEN_CFG_ANA_CHANGE)
#define NVMET_DISC_AEN_CFG_OPTIONAL \
	(NVME_AEN_CFG_DISC_CHANGE)

/*
 * Plus mandatory SMART AENs (we'll never send them, but allow enabling them):
 */
#define NVMET_AEN_CFG_ALL \
	(NVME_SMART_CRIT_SPARE | NVME_SMART_CRIT_TEMPERATURE | \
	 NVME_SMART_CRIT_RELIABILITY | NVME_SMART_CRIT_MEDIA | \
	 NVME_SMART_CRIT_VOLATILE_MEMORY | NVMET_AEN_CFG_OPTIONAL)

/* Helper Macros when NVMe error is NVME_SC_CONNECT_INVALID_PARAM
 * The 16 bit shift is to set IATTR bit to 1, which means offending
 * offset starts in the data section of connect()
 */
#define IPO_IATTR_CONNECT_DATA(x)	\
	(cpu_to_le32((1 << 16) | (offsetof(struct nvmf_connect_data, x))))
#define IPO_IATTR_CONNECT_SQE(x)	\
	(cpu_to_le32(offsetof(struct nvmf_connect_command, x)))

struct nvmet_pr_registrant {
	u64			rkey;
	uuid_t			hostid;
	enum nvme_pr_type	rtype;
	struct list_head	entry;
	struct rcu_head		rcu;
};

struct nvmet_pr {
	bool			enable;
	unsigned long		notify_mask;
	atomic_t		generation;
	struct nvmet_pr_registrant __rcu *holder;
	/*
	 * During the execution of the reservation command, mutual
	 * exclusion is required throughout the process. However,
	 * while waiting asynchronously for the 'per controller
	 * percpu_ref' to complete before the 'preempt and abort'
	 * command finishes, a semaphore is needed to ensure mutual
	 * exclusion instead of a mutex.
	 */
	struct semaphore	pr_sem;
	struct list_head	registrant_list;
};

struct nvmet_pr_per_ctrl_ref {
	struct percpu_ref	ref;
	struct completion	free_done;
	struct completion	confirm_done;
	uuid_t			hostid;
};

struct nvmet_ns {
	struct percpu_ref	ref;
	struct file		*bdev_file;
	struct block_device	*bdev;
	struct file		*file;
	bool			readonly;
	u32			nsid;
	u32			blksize_shift;
	loff_t			size;
	u8			nguid[16];
	uuid_t			uuid;
	u32			anagrpid;

	bool			buffered_io;
	bool			enabled;
	struct nvmet_subsys	*subsys;
	const char		*device_path;

	struct config_group	device_group;
	struct config_group	group;

	struct completion	disable_done;
	mempool_t		*bvec_pool;

	struct pci_dev		*p2p_dev;
	int			use_p2pmem;
	int			pi_type;
	int			metadata_size;
	u8			csi;
	struct nvmet_pr		pr;
	struct xarray		pr_per_ctrl_refs;
};

static inline struct nvmet_ns *to_nvmet_ns(struct config_item *item)
{
	return container_of(to_config_group(item), struct nvmet_ns, group);
}

static inline struct device *nvmet_ns_dev(struct nvmet_ns *ns)
{
	return ns->bdev ? disk_to_dev(ns->bdev->bd_disk) : NULL;
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
	u32			sqhd;
	bool			sqhd_disabled;
#ifdef CONFIG_NVME_TARGET_AUTH
	bool			authenticated;
	struct delayed_work	auth_expired_work;
	u16			dhchap_tid;
	u8			dhchap_status;
	u8			dhchap_step;
	u8			*dhchap_c1;
	u8			*dhchap_c2;
	u32			dhchap_s1;
	u32			dhchap_s2;
	u8			*dhchap_skey;
	int			dhchap_skey_len;
#endif
#ifdef CONFIG_NVME_TARGET_TCP_TLS
	struct key		*tls_key;
#endif
	struct completion	free_done;
	struct completion	confirm_done;
};

struct nvmet_ana_group {
	struct config_group	group;
	struct nvmet_port	*port;
	u32			grpid;
};

static inline struct nvmet_ana_group *to_ana_group(struct config_item *item)
{
	return container_of(to_config_group(item), struct nvmet_ana_group,
			group);
}

/**
 * struct nvmet_port -	Common structure to keep port
 *				information for the target.
 * @entry:		Entry into referrals or transport list.
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
	struct list_head		global_entry;
	struct config_group		ana_groups_group;
	struct nvmet_ana_group		ana_default_group;
	enum nvme_ana_state		*ana_state;
	struct key			*keyring;
	void				*priv;
	bool				enabled;
	int				inline_data_size;
	int				max_queue_size;
	const struct nvmet_fabrics_ops	*tr_ops;
	bool				pi_enable;
};

static inline struct nvmet_port *to_nvmet_port(struct config_item *item)
{
	return container_of(to_config_group(item), struct nvmet_port,
			group);
}

static inline struct nvmet_port *ana_groups_to_port(
		struct config_item *item)
{
	return container_of(to_config_group(item), struct nvmet_port,
			ana_groups_group);
}

static inline u8 nvmet_port_disc_addr_treq_secure_channel(struct nvmet_port *port)
{
	return (port->disc_addr.treq & NVME_TREQ_SECURE_CHANNEL_MASK);
}

static inline bool nvmet_port_secure_channel_required(struct nvmet_port *port)
{
    return nvmet_port_disc_addr_treq_secure_channel(port) == NVMF_TREQ_REQUIRED;
}

struct nvmet_pr_log_mgr {
	struct mutex		lock;
	u64			lost_count;
	u64			counter;
	DECLARE_KFIFO(log_queue, struct nvme_pr_log, NVMET_PR_LOG_QUEUE_SIZE);
};

struct nvmet_ctrl {
	struct nvmet_subsys	*subsys;
	struct nvmet_sq		**sqs;

	void			*drvdata;

	bool			reset_tbkas;

	struct mutex		lock;
	u64			cap;
	u32			cc;
	u32			csts;

	uuid_t			hostid;
	u16			cntlid;
	u32			kato;

	struct nvmet_port	*port;

	u32			aen_enabled;
	unsigned long		aen_masked;
	struct nvmet_req	*async_event_cmds[NVMET_ASYNC_EVENTS];
	unsigned int		nr_async_event_cmds;
	struct list_head	async_events;
	struct work_struct	async_event_work;

	struct list_head	subsys_entry;
	struct kref		ref;
	struct delayed_work	ka_work;
	struct work_struct	fatal_err_work;

	const struct nvmet_fabrics_ops *ops;

	__le32			*changed_ns_list;
	u32			nr_changed_ns;

	char			subsysnqn[NVMF_NQN_FIELD_LEN];
	char			hostnqn[NVMF_NQN_FIELD_LEN];

	struct device		*p2p_client;
	struct radix_tree_root	p2p_ns_map;
#ifdef CONFIG_NVME_TARGET_DEBUGFS
	struct dentry		*debugfs_dir;
#endif
	spinlock_t		error_lock;
	u64			err_counter;
	struct nvme_error_slot	slots[NVMET_ERROR_LOG_SLOTS];
	bool			pi_support;
	bool			concat;
#ifdef CONFIG_NVME_TARGET_AUTH
	struct nvme_dhchap_key	*host_key;
	struct nvme_dhchap_key	*ctrl_key;
	u8			shash_id;
	struct crypto_kpp	*dh_tfm;
	u8			dh_gid;
	u8			*dh_key;
	size_t			dh_keysize;
#endif
#ifdef CONFIG_NVME_TARGET_TCP_TLS
	struct key		*tls_key;
#endif
	struct nvmet_pr_log_mgr pr_log_mgr;
};

struct nvmet_subsys {
	enum nvme_subsys_type	type;

	struct mutex		lock;
	struct kref		ref;

	struct xarray		namespaces;
	unsigned int		nr_namespaces;
	u32			max_nsid;
	u16			cntlid_min;
	u16			cntlid_max;

	struct list_head	ctrls;

	struct list_head	hosts;
	bool			allow_any_host;
#ifdef CONFIG_NVME_TARGET_DEBUGFS
	struct dentry		*debugfs_dir;
#endif
	u16			max_qid;

	u64			ver;
	char			serial[NVMET_SN_MAX_SIZE];
	bool			subsys_discovered;
	char			*subsysnqn;
	bool			pi_support;

	struct config_group	group;

	struct config_group	namespaces_group;
	struct config_group	allowed_hosts_group;

	u16			vendor_id;
	u16			subsys_vendor_id;
	char			*model_number;
	u32			ieee_oui;
	char			*firmware_rev;

#ifdef CONFIG_NVME_TARGET_PASSTHRU
	struct nvme_ctrl	*passthru_ctrl;
	char			*passthru_ctrl_path;
	struct config_group	passthru_group;
	unsigned int		admin_timeout;
	unsigned int		io_timeout;
	unsigned int		clear_ids;
#endif /* CONFIG_NVME_TARGET_PASSTHRU */

#ifdef CONFIG_BLK_DEV_ZONED
	u8			zasl;
#endif /* CONFIG_BLK_DEV_ZONED */
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
	u8			*dhchap_secret;
	u8			*dhchap_ctrl_secret;
	u8			dhchap_key_hash;
	u8			dhchap_ctrl_key_hash;
	u8			dhchap_hash_id;
	u8			dhchap_dhgroup_id;
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
	unsigned int msdbd;
	unsigned int flags;
#define NVMF_KEYED_SGLS			(1 << 0)
#define NVMF_METADATA_SUPPORTED		(1 << 1)
	void (*queue_response)(struct nvmet_req *req);
	int (*add_port)(struct nvmet_port *port);
	void (*remove_port)(struct nvmet_port *port);
	void (*delete_ctrl)(struct nvmet_ctrl *ctrl);
	void (*disc_traddr)(struct nvmet_req *req,
			struct nvmet_port *port, char *traddr);
	ssize_t (*host_traddr)(struct nvmet_ctrl *ctrl,
			char *traddr, size_t traddr_len);
	u16 (*install_queue)(struct nvmet_sq *nvme_sq);
	void (*discovery_chg)(struct nvmet_port *port);
	u8 (*get_mdts)(const struct nvmet_ctrl *ctrl);
	u16 (*get_max_queue_size)(const struct nvmet_ctrl *ctrl);

	/* Operations mandatory for PCI target controllers */
	u16 (*create_sq)(struct nvmet_ctrl *ctrl, u16 sqid, u16 flags,
			 u16 qsize, u64 prp1);
	u16 (*delete_sq)(struct nvmet_ctrl *ctrl, u16 sqid);
	u16 (*create_cq)(struct nvmet_ctrl *ctrl, u16 cqid, u16 flags,
			 u16 qsize, u64 prp1, u16 irq_vector);
	u16 (*delete_cq)(struct nvmet_ctrl *ctrl, u16 cqid);
	u16 (*set_feature)(const struct nvmet_ctrl *ctrl, u8 feat,
			   void *feat_data);
	u16 (*get_feature)(const struct nvmet_ctrl *ctrl, u8 feat,
			   void *feat_data);
};

#define NVMET_MAX_INLINE_BIOVEC	8
#define NVMET_MAX_INLINE_DATA_LEN NVMET_MAX_INLINE_BIOVEC * PAGE_SIZE

struct nvmet_req {
	struct nvme_command	*cmd;
	struct nvme_completion	*cqe;
	struct nvmet_sq		*sq;
	struct nvmet_cq		*cq;
	struct nvmet_ns		*ns;
	struct scatterlist	*sg;
	struct scatterlist	*metadata_sg;
	struct bio_vec		inline_bvec[NVMET_MAX_INLINE_BIOVEC];
	union {
		struct {
			struct bio      inline_bio;
		} b;
		struct {
			bool			mpool_alloc;
			struct kiocb            iocb;
			struct bio_vec          *bvec;
			struct work_struct      work;
		} f;
		struct {
			struct bio		inline_bio;
			struct request		*rq;
			struct work_struct      work;
			bool			use_workqueue;
		} p;
#ifdef CONFIG_BLK_DEV_ZONED
		struct {
			struct bio		inline_bio;
			struct work_struct	zmgmt_work;
		} z;
#endif /* CONFIG_BLK_DEV_ZONED */
		struct {
			struct work_struct	abort_work;
		} r;
	};
	int			sg_cnt;
	int			metadata_sg_cnt;
	/* data length as parsed from the SGL descriptor: */
	size_t			transfer_len;
	size_t			metadata_len;

	struct nvmet_port	*port;

	void (*execute)(struct nvmet_req *req);
	const struct nvmet_fabrics_ops *ops;

	struct pci_dev		*p2p_dev;
	struct device		*p2p_client;
	u16			error_loc;
	u64			error_slba;
	struct nvmet_pr_per_ctrl_ref *pc_ref;
};

#define NVMET_MAX_MPOOL_BVEC		16
extern struct kmem_cache *nvmet_bvec_cache;
extern struct workqueue_struct *buffered_io_wq;
extern struct workqueue_struct *zbd_wq;
extern struct workqueue_struct *nvmet_wq;

static inline void nvmet_set_result(struct nvmet_req *req, u32 result)
{
	req->cqe->result.u32 = cpu_to_le32(result);
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

static inline void nvmet_clear_aen_bit(struct nvmet_req *req, u32 bn)
{
	int rae = le32_to_cpu(req->cmd->common.cdw10) & 1 << 15;

	if (!rae)
		clear_bit(bn, &req->sq->ctrl->aen_masked);
}

static inline bool nvmet_aen_bit_disabled(struct nvmet_ctrl *ctrl, u32 bn)
{
	if (!(READ_ONCE(ctrl->aen_enabled) & (1 << bn)))
		return true;
	return test_and_set_bit(bn, &ctrl->aen_masked);
}

void nvmet_get_feat_kato(struct nvmet_req *req);
void nvmet_get_feat_async_event(struct nvmet_req *req);
u16 nvmet_set_feat_kato(struct nvmet_req *req);
u16 nvmet_set_feat_async_event(struct nvmet_req *req, u32 mask);
void nvmet_execute_async_event(struct nvmet_req *req);
void nvmet_start_keep_alive_timer(struct nvmet_ctrl *ctrl);
void nvmet_stop_keep_alive_timer(struct nvmet_ctrl *ctrl);

u16 nvmet_parse_connect_cmd(struct nvmet_req *req);
u32 nvmet_connect_cmd_data_len(struct nvmet_req *req);
void nvmet_bdev_set_limits(struct block_device *bdev, struct nvme_id_ns *id);
u16 nvmet_bdev_parse_io_cmd(struct nvmet_req *req);
u16 nvmet_file_parse_io_cmd(struct nvmet_req *req);
u16 nvmet_bdev_zns_parse_io_cmd(struct nvmet_req *req);
u32 nvmet_admin_cmd_data_len(struct nvmet_req *req);
u16 nvmet_parse_admin_cmd(struct nvmet_req *req);
u32 nvmet_discovery_cmd_data_len(struct nvmet_req *req);
u16 nvmet_parse_discovery_cmd(struct nvmet_req *req);
u16 nvmet_parse_fabrics_admin_cmd(struct nvmet_req *req);
u32 nvmet_fabrics_admin_cmd_data_len(struct nvmet_req *req);
u16 nvmet_parse_fabrics_io_cmd(struct nvmet_req *req);
u32 nvmet_fabrics_io_cmd_data_len(struct nvmet_req *req);

bool nvmet_req_init(struct nvmet_req *req, struct nvmet_cq *cq,
		struct nvmet_sq *sq, const struct nvmet_fabrics_ops *ops);
void nvmet_req_uninit(struct nvmet_req *req);
size_t nvmet_req_transfer_len(struct nvmet_req *req);
bool nvmet_check_transfer_len(struct nvmet_req *req, size_t len);
bool nvmet_check_data_len_lte(struct nvmet_req *req, size_t data_len);
void nvmet_req_complete(struct nvmet_req *req, u16 status);
int nvmet_req_alloc_sgls(struct nvmet_req *req);
void nvmet_req_free_sgls(struct nvmet_req *req);

void nvmet_execute_set_features(struct nvmet_req *req);
void nvmet_execute_get_features(struct nvmet_req *req);
void nvmet_execute_keep_alive(struct nvmet_req *req);

u16 nvmet_check_cqid(struct nvmet_ctrl *ctrl, u16 cqid);
void nvmet_cq_setup(struct nvmet_ctrl *ctrl, struct nvmet_cq *cq, u16 qid,
		u16 size);
u16 nvmet_cq_create(struct nvmet_ctrl *ctrl, struct nvmet_cq *cq, u16 qid,
		u16 size);
u16 nvmet_check_sqid(struct nvmet_ctrl *ctrl, u16 sqid, bool create);
void nvmet_sq_setup(struct nvmet_ctrl *ctrl, struct nvmet_sq *sq, u16 qid,
		u16 size);
u16 nvmet_sq_create(struct nvmet_ctrl *ctrl, struct nvmet_sq *sq, u16 qid,
		u16 size);
void nvmet_sq_destroy(struct nvmet_sq *sq);
int nvmet_sq_init(struct nvmet_sq *sq);

void nvmet_ctrl_fatal_error(struct nvmet_ctrl *ctrl);

void nvmet_update_cc(struct nvmet_ctrl *ctrl, u32 new);

struct nvmet_alloc_ctrl_args {
	struct nvmet_port	*port;
	struct nvmet_sq		*sq;
	char			*subsysnqn;
	char			*hostnqn;
	uuid_t			*hostid;
	const struct nvmet_fabrics_ops *ops;
	struct device		*p2p_client;
	u32			kato;
	__le32			result;
	u16			error_loc;
	u16			status;
};

struct nvmet_ctrl *nvmet_alloc_ctrl(struct nvmet_alloc_ctrl_args *args);
struct nvmet_ctrl *nvmet_ctrl_find_get(const char *subsysnqn,
				       const char *hostnqn, u16 cntlid,
				       struct nvmet_req *req);
void nvmet_ctrl_put(struct nvmet_ctrl *ctrl);
u16 nvmet_check_ctrl_status(struct nvmet_req *req);
ssize_t nvmet_ctrl_host_traddr(struct nvmet_ctrl *ctrl,
		char *traddr, size_t traddr_len);

struct nvmet_subsys *nvmet_subsys_alloc(const char *subsysnqn,
		enum nvme_subsys_type type);
void nvmet_subsys_put(struct nvmet_subsys *subsys);
void nvmet_subsys_del_ctrls(struct nvmet_subsys *subsys);

u16 nvmet_req_find_ns(struct nvmet_req *req);
void nvmet_put_namespace(struct nvmet_ns *ns);
int nvmet_ns_enable(struct nvmet_ns *ns);
void nvmet_ns_disable(struct nvmet_ns *ns);
struct nvmet_ns *nvmet_ns_alloc(struct nvmet_subsys *subsys, u32 nsid);
void nvmet_ns_free(struct nvmet_ns *ns);

void nvmet_send_ana_event(struct nvmet_subsys *subsys,
		struct nvmet_port *port);
void nvmet_port_send_ana_event(struct nvmet_port *port);

int nvmet_register_transport(const struct nvmet_fabrics_ops *ops);
void nvmet_unregister_transport(const struct nvmet_fabrics_ops *ops);

void nvmet_port_del_ctrls(struct nvmet_port *port,
			  struct nvmet_subsys *subsys);

int nvmet_enable_port(struct nvmet_port *port);
void nvmet_disable_port(struct nvmet_port *port);

void nvmet_referral_enable(struct nvmet_port *parent, struct nvmet_port *port);
void nvmet_referral_disable(struct nvmet_port *parent, struct nvmet_port *port);

u16 nvmet_copy_to_sgl(struct nvmet_req *req, off_t off, const void *buf,
		size_t len);
u16 nvmet_copy_from_sgl(struct nvmet_req *req, off_t off, void *buf,
		size_t len);
u16 nvmet_zero_sgl(struct nvmet_req *req, off_t off, size_t len);

u32 nvmet_get_log_page_len(struct nvme_command *cmd);
u64 nvmet_get_log_page_offset(struct nvme_command *cmd);

extern struct list_head *nvmet_ports;
void nvmet_port_disc_changed(struct nvmet_port *port,
		struct nvmet_subsys *subsys);
void nvmet_subsys_disc_changed(struct nvmet_subsys *subsys,
		struct nvmet_host *host);
void nvmet_add_async_event(struct nvmet_ctrl *ctrl, u8 event_type,
		u8 event_info, u8 log_page);

#define NVMET_MIN_QUEUE_SIZE	16
#define NVMET_MAX_QUEUE_SIZE	1024
#define NVMET_NR_QUEUES		128
#define NVMET_MAX_CMD(ctrl)	(NVME_CAP_MQES(ctrl->cap) + 1)

/*
 * Nice round number that makes a list of nsids fit into a page.
 * Should become tunable at some point in the future.
 */
#define NVMET_MAX_NAMESPACES	1024

/*
 * 0 is not a valid ANA group ID, so we start numbering at 1.
 *
 * ANA Group 1 exists without manual intervention, has namespaces assigned to it
 * by default, and is available in an optimized state through all ports.
 */
#define NVMET_MAX_ANAGRPS	128
#define NVMET_DEFAULT_ANA_GRPID	1

#define NVMET_KAS		10
#define NVMET_DISC_KATO_MS		120000

int __init nvmet_init_configfs(void);
void __exit nvmet_exit_configfs(void);

int __init nvmet_init_discovery(void);
void nvmet_exit_discovery(void);

extern struct nvmet_subsys *nvmet_disc_subsys;
extern struct rw_semaphore nvmet_config_sem;

extern u32 nvmet_ana_group_enabled[NVMET_MAX_ANAGRPS + 1];
extern u64 nvmet_ana_chgcnt;
extern struct rw_semaphore nvmet_ana_sem;

bool nvmet_host_allowed(struct nvmet_subsys *subsys, const char *hostnqn);

int nvmet_bdev_ns_enable(struct nvmet_ns *ns);
int nvmet_file_ns_enable(struct nvmet_ns *ns);
void nvmet_bdev_ns_disable(struct nvmet_ns *ns);
void nvmet_file_ns_disable(struct nvmet_ns *ns);
u16 nvmet_bdev_flush(struct nvmet_req *req);
u16 nvmet_file_flush(struct nvmet_req *req);
void nvmet_ns_changed(struct nvmet_subsys *subsys, u32 nsid);
void nvmet_bdev_ns_revalidate(struct nvmet_ns *ns);
void nvmet_file_ns_revalidate(struct nvmet_ns *ns);
bool nvmet_ns_revalidate(struct nvmet_ns *ns);
u16 blk_to_nvme_status(struct nvmet_req *req, blk_status_t blk_sts);

bool nvmet_bdev_zns_enable(struct nvmet_ns *ns);
void nvmet_execute_identify_ctrl_zns(struct nvmet_req *req);
void nvmet_execute_identify_ns_zns(struct nvmet_req *req);
void nvmet_bdev_execute_zone_mgmt_recv(struct nvmet_req *req);
void nvmet_bdev_execute_zone_mgmt_send(struct nvmet_req *req);
void nvmet_bdev_execute_zone_append(struct nvmet_req *req);

static inline u32 nvmet_rw_data_len(struct nvmet_req *req)
{
	return ((u32)le16_to_cpu(req->cmd->rw.length) + 1) <<
			req->ns->blksize_shift;
}

static inline u32 nvmet_rw_metadata_len(struct nvmet_req *req)
{
	if (!IS_ENABLED(CONFIG_BLK_DEV_INTEGRITY))
		return 0;
	return ((u32)le16_to_cpu(req->cmd->rw.length) + 1) *
			req->ns->metadata_size;
}

static inline u32 nvmet_dsm_len(struct nvmet_req *req)
{
	return (le32_to_cpu(req->cmd->dsm.nr) + 1) *
		sizeof(struct nvme_dsm_range);
}

static inline struct nvmet_subsys *nvmet_req_subsys(struct nvmet_req *req)
{
	return req->sq->ctrl->subsys;
}

static inline bool nvmet_is_disc_subsys(struct nvmet_subsys *subsys)
{
    return subsys->type != NVME_NQN_NVME;
}

static inline bool nvmet_is_pci_ctrl(struct nvmet_ctrl *ctrl)
{
	return ctrl->port->disc_addr.trtype == NVMF_TRTYPE_PCI;
}

#ifdef CONFIG_NVME_TARGET_PASSTHRU
void nvmet_passthru_subsys_free(struct nvmet_subsys *subsys);
int nvmet_passthru_ctrl_enable(struct nvmet_subsys *subsys);
void nvmet_passthru_ctrl_disable(struct nvmet_subsys *subsys);
u16 nvmet_parse_passthru_admin_cmd(struct nvmet_req *req);
u16 nvmet_parse_passthru_io_cmd(struct nvmet_req *req);
static inline bool nvmet_is_passthru_subsys(struct nvmet_subsys *subsys)
{
	return subsys->passthru_ctrl;
}
#else /* CONFIG_NVME_TARGET_PASSTHRU */
static inline void nvmet_passthru_subsys_free(struct nvmet_subsys *subsys)
{
}
static inline void nvmet_passthru_ctrl_disable(struct nvmet_subsys *subsys)
{
}
static inline u16 nvmet_parse_passthru_admin_cmd(struct nvmet_req *req)
{
	return 0;
}
static inline u16 nvmet_parse_passthru_io_cmd(struct nvmet_req *req)
{
	return 0;
}
static inline bool nvmet_is_passthru_subsys(struct nvmet_subsys *subsys)
{
	return NULL;
}
#endif /* CONFIG_NVME_TARGET_PASSTHRU */

static inline bool nvmet_is_passthru_req(struct nvmet_req *req)
{
	return nvmet_is_passthru_subsys(nvmet_req_subsys(req));
}

void nvmet_passthrough_override_cap(struct nvmet_ctrl *ctrl);

u16 errno_to_nvme_status(struct nvmet_req *req, int errno);
u16 nvmet_report_invalid_opcode(struct nvmet_req *req);

static inline bool nvmet_cc_en(u32 cc)
{
	return (cc & NVME_CC_ENABLE) >> NVME_CC_EN_SHIFT;
}

static inline u8 nvmet_cc_css(u32 cc)
{
	return (cc & NVME_CC_CSS_MASK) >> NVME_CC_CSS_SHIFT;
}

static inline u8 nvmet_cc_mps(u32 cc)
{
	return (cc & NVME_CC_MPS_MASK) >> NVME_CC_MPS_SHIFT;
}

static inline u8 nvmet_cc_ams(u32 cc)
{
	return (cc & NVME_CC_AMS_MASK) >> NVME_CC_AMS_SHIFT;
}

static inline u8 nvmet_cc_shn(u32 cc)
{
	return (cc & NVME_CC_SHN_MASK) >> NVME_CC_SHN_SHIFT;
}

static inline u8 nvmet_cc_iosqes(u32 cc)
{
	return (cc & NVME_CC_IOSQES_MASK) >> NVME_CC_IOSQES_SHIFT;
}

static inline u8 nvmet_cc_iocqes(u32 cc)
{
	return (cc & NVME_CC_IOCQES_MASK) >> NVME_CC_IOCQES_SHIFT;
}

/* Convert a 32-bit number to a 16-bit 0's based number */
static inline __le16 to0based(u32 a)
{
	return cpu_to_le16(clamp(a, 1U, 1U << 16) - 1);
}

static inline bool nvmet_ns_has_pi(struct nvmet_ns *ns)
{
	if (!IS_ENABLED(CONFIG_BLK_DEV_INTEGRITY))
		return false;
	return ns->pi_type && ns->metadata_size == sizeof(struct t10_pi_tuple);
}

static inline __le64 nvmet_sect_to_lba(struct nvmet_ns *ns, sector_t sect)
{
	return cpu_to_le64(sect >> (ns->blksize_shift - SECTOR_SHIFT));
}

static inline sector_t nvmet_lba_to_sect(struct nvmet_ns *ns, __le64 lba)
{
	return le64_to_cpu(lba) << (ns->blksize_shift - SECTOR_SHIFT);
}

static inline bool nvmet_use_inline_bvec(struct nvmet_req *req)
{
	return req->transfer_len <= NVMET_MAX_INLINE_DATA_LEN &&
	       req->sg_cnt <= NVMET_MAX_INLINE_BIOVEC;
}

static inline void nvmet_req_bio_put(struct nvmet_req *req, struct bio *bio)
{
	if (bio != &req->b.inline_bio)
		bio_put(bio);
}

#ifdef CONFIG_NVME_TARGET_TCP_TLS
static inline key_serial_t nvmet_queue_tls_keyid(struct nvmet_sq *sq)
{
	return sq->tls_key ? key_serial(sq->tls_key) : 0;
}
static inline void nvmet_sq_put_tls_key(struct nvmet_sq *sq)
{
	if (sq->tls_key) {
		key_put(sq->tls_key);
		sq->tls_key = NULL;
	}
}
#else
static inline key_serial_t nvmet_queue_tls_keyid(struct nvmet_sq *sq) { return 0; }
static inline void nvmet_sq_put_tls_key(struct nvmet_sq *sq) {}
#endif
#ifdef CONFIG_NVME_TARGET_AUTH
u32 nvmet_auth_send_data_len(struct nvmet_req *req);
void nvmet_execute_auth_send(struct nvmet_req *req);
u32 nvmet_auth_receive_data_len(struct nvmet_req *req);
void nvmet_execute_auth_receive(struct nvmet_req *req);
int nvmet_auth_set_key(struct nvmet_host *host, const char *secret,
		       bool set_ctrl);
int nvmet_auth_set_host_hash(struct nvmet_host *host, const char *hash);
u8 nvmet_setup_auth(struct nvmet_ctrl *ctrl, struct nvmet_sq *sq);
void nvmet_auth_sq_init(struct nvmet_sq *sq);
void nvmet_destroy_auth(struct nvmet_ctrl *ctrl);
void nvmet_auth_sq_free(struct nvmet_sq *sq);
int nvmet_setup_dhgroup(struct nvmet_ctrl *ctrl, u8 dhgroup_id);
bool nvmet_check_auth_status(struct nvmet_req *req);
int nvmet_auth_host_hash(struct nvmet_req *req, u8 *response,
			 unsigned int hash_len);
int nvmet_auth_ctrl_hash(struct nvmet_req *req, u8 *response,
			 unsigned int hash_len);
static inline bool nvmet_has_auth(struct nvmet_ctrl *ctrl, struct nvmet_sq *sq)
{
	return ctrl->host_key != NULL && !nvmet_queue_tls_keyid(sq);
}
int nvmet_auth_ctrl_exponential(struct nvmet_req *req,
				u8 *buf, int buf_size);
int nvmet_auth_ctrl_sesskey(struct nvmet_req *req,
			    u8 *buf, int buf_size);
void nvmet_auth_insert_psk(struct nvmet_sq *sq);
#else
static inline u8 nvmet_setup_auth(struct nvmet_ctrl *ctrl,
				  struct nvmet_sq *sq)
{
	return 0;
}
static inline void nvmet_auth_sq_init(struct nvmet_sq *sq)
{
}
static inline void nvmet_destroy_auth(struct nvmet_ctrl *ctrl) {};
static inline void nvmet_auth_sq_free(struct nvmet_sq *sq) {};
static inline bool nvmet_check_auth_status(struct nvmet_req *req)
{
	return true;
}
static inline bool nvmet_has_auth(struct nvmet_ctrl *ctrl,
				  struct nvmet_sq *sq)
{
	return false;
}
static inline const char *nvmet_dhchap_dhgroup_name(u8 dhgid) { return NULL; }
static inline void nvmet_auth_insert_psk(struct nvmet_sq *sq) {};
#endif

int nvmet_pr_init_ns(struct nvmet_ns *ns);
u16 nvmet_parse_pr_cmd(struct nvmet_req *req);
u16 nvmet_pr_check_cmd_access(struct nvmet_req *req);
int nvmet_ctrl_init_pr(struct nvmet_ctrl *ctrl);
void nvmet_ctrl_destroy_pr(struct nvmet_ctrl *ctrl);
void nvmet_pr_exit_ns(struct nvmet_ns *ns);
void nvmet_execute_get_log_page_resv(struct nvmet_req *req);
u16 nvmet_set_feat_resv_notif_mask(struct nvmet_req *req, u32 mask);
u16 nvmet_get_feat_resv_notif_mask(struct nvmet_req *req);
u16 nvmet_pr_get_ns_pc_ref(struct nvmet_req *req);
static inline void nvmet_pr_put_ns_pc_ref(struct nvmet_pr_per_ctrl_ref *pc_ref)
{
	percpu_ref_put(&pc_ref->ref);
}

/*
 * Data for the get_feature() and set_feature() operations of PCI target
 * controllers.
 */
struct nvmet_feat_irq_coalesce {
	u8		thr;
	u8		time;
};

struct nvmet_feat_irq_config {
	u16		iv;
	bool		cd;
};

struct nvmet_feat_arbitration {
	u8		hpw;
	u8		mpw;
	u8		lpw;
	u8		ab;
};

#endif /* _NVMET_H */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2011-2014, Intel Corporation.
 */

#ifndef _NVME_H
#define _NVME_H

#include <linux/nvme.h>
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/kref.h>
#include <linux/blk-mq.h>
#include <linux/sed-opal.h>
#include <linux/fault-inject.h>
#include <linux/rcupdate.h>
#include <linux/wait.h>
#include <linux/t10-pi.h>

#include <trace/events/block.h>

extern unsigned int nvme_io_timeout;
#define NVME_IO_TIMEOUT	(nvme_io_timeout * HZ)

extern unsigned int admin_timeout;
#define NVME_ADMIN_TIMEOUT	(admin_timeout * HZ)

#define NVME_DEFAULT_KATO	5

#ifdef CONFIG_ARCH_NO_SG_CHAIN
#define  NVME_INLINE_SG_CNT  0
#define  NVME_INLINE_METADATA_SG_CNT  0
#else
#define  NVME_INLINE_SG_CNT  2
#define  NVME_INLINE_METADATA_SG_CNT  1
#endif

/*
 * Default to a 4K page size, with the intention to update this
 * path in the future to accommodate architectures with differing
 * kernel and IO page sizes.
 */
#define NVME_CTRL_PAGE_SHIFT	12
#define NVME_CTRL_PAGE_SIZE	(1 << NVME_CTRL_PAGE_SHIFT)

extern struct workqueue_struct *nvme_wq;
extern struct workqueue_struct *nvme_reset_wq;
extern struct workqueue_struct *nvme_delete_wq;

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
	 * The controller deterministically returns O's on reads to
	 * logical blocks that deallocate was called on.
	 */
	NVME_QUIRK_DEALLOCATE_ZEROES		= (1 << 2),

	/*
	 * The controller needs a delay before starts checking the device
	 * readiness, which is done by reading the NVME_CSTS_RDY bit.
	 */
	NVME_QUIRK_DELAY_BEFORE_CHK_RDY		= (1 << 3),

	/*
	 * APST should not be used.
	 */
	NVME_QUIRK_NO_APST			= (1 << 4),

	/*
	 * The deepest sleep state should not be used.
	 */
	NVME_QUIRK_NO_DEEPEST_PS		= (1 << 5),

	/*
	 * Set MEDIUM priority on SQ creation
	 */
	NVME_QUIRK_MEDIUM_PRIO_SQ		= (1 << 7),

	/*
	 * Ignore device provided subnqn.
	 */
	NVME_QUIRK_IGNORE_DEV_SUBNQN		= (1 << 8),

	/*
	 * Broken Write Zeroes.
	 */
	NVME_QUIRK_DISABLE_WRITE_ZEROES		= (1 << 9),

	/*
	 * Force simple suspend/resume path.
	 */
	NVME_QUIRK_SIMPLE_SUSPEND		= (1 << 10),

	/*
	 * Use only one interrupt vector for all queues
	 */
	NVME_QUIRK_SINGLE_VECTOR		= (1 << 11),

	/*
	 * Use non-standard 128 bytes SQEs.
	 */
	NVME_QUIRK_128_BYTES_SQES		= (1 << 12),

	/*
	 * Prevent tag overlap between queues
	 */
	NVME_QUIRK_SHARED_TAGS                  = (1 << 13),

	/*
	 * Don't change the value of the temperature threshold feature
	 */
	NVME_QUIRK_NO_TEMP_THRESH_CHANGE	= (1 << 14),

	/*
	 * The controller doesn't handle the Identify Namespace
	 * Identification Descriptor list subcommand despite claiming
	 * NVMe 1.3 compliance.
	 */
	NVME_QUIRK_NO_NS_DESC_LIST		= (1 << 15),

	/*
	 * The controller does not properly handle DMA addresses over
	 * 48 bits.
	 */
	NVME_QUIRK_DMA_ADDRESS_BITS_48		= (1 << 16),

	/*
	 * The controller requires the command_id value be limited, so skip
	 * encoding the generation sequence number.
	 */
	NVME_QUIRK_SKIP_CID_GEN			= (1 << 17),

	/*
	 * Reports garbage in the namespace identifiers (eui64, nguid, uuid).
	 */
	NVME_QUIRK_BOGUS_NID			= (1 << 18),

	/*
	 * No temperature thresholds for channels other than 0 (Composite).
	 */
	NVME_QUIRK_NO_SECONDARY_TEMP_THRESH	= (1 << 19),
};

/*
 * Common request structure for NVMe passthrough.  All drivers must have
 * this structure as the first member of their request-private data.
 */
struct nvme_request {
	struct nvme_command	*cmd;
	union nvme_result	result;
	u8			genctr;
	u8			retries;
	u8			flags;
	u16			status;
#ifdef CONFIG_NVME_MULTIPATH
	unsigned long		start_time;
#endif
	struct nvme_ctrl	*ctrl;
};

/*
 * Mark a bio as coming in through the mpath node.
 */
#define REQ_NVME_MPATH		REQ_DRV

enum {
	NVME_REQ_CANCELLED		= (1 << 0),
	NVME_REQ_USERCMD		= (1 << 1),
	NVME_MPATH_IO_STATS		= (1 << 2),
};

static inline struct nvme_request *nvme_req(struct request *req)
{
	return blk_mq_rq_to_pdu(req);
}

static inline u16 nvme_req_qid(struct request *req)
{
	if (!req->q->queuedata)
		return 0;

	return req->mq_hctx->queue_num + 1;
}

/* The below value is the specific amount of delay needed before checking
 * readiness in case of the PCI_DEVICE(0x1c58, 0x0003), which needs the
 * NVME_QUIRK_DELAY_BEFORE_CHK_RDY quirk enabled. The value (in ms) was
 * found empirically.
 */
#define NVME_QUIRK_DELAY_AMOUNT		2300

/*
 * enum nvme_ctrl_state: Controller state
 *
 * @NVME_CTRL_NEW:		New controller just allocated, initial state
 * @NVME_CTRL_LIVE:		Controller is connected and I/O capable
 * @NVME_CTRL_RESETTING:	Controller is resetting (or scheduled reset)
 * @NVME_CTRL_CONNECTING:	Controller is disconnected, now connecting the
 *				transport
 * @NVME_CTRL_DELETING:		Controller is deleting (or scheduled deletion)
 * @NVME_CTRL_DELETING_NOIO:	Controller is deleting and I/O is not
 *				disabled/failed immediately. This state comes
 * 				after all async event processing took place and
 * 				before ns removal and the controller deletion
 * 				progress
 * @NVME_CTRL_DEAD:		Controller is non-present/unresponsive during
 *				shutdown or removal. In this case we forcibly
 *				kill all inflight I/O as they have no chance to
 *				complete
 */
enum nvme_ctrl_state {
	NVME_CTRL_NEW,
	NVME_CTRL_LIVE,
	NVME_CTRL_RESETTING,
	NVME_CTRL_CONNECTING,
	NVME_CTRL_DELETING,
	NVME_CTRL_DELETING_NOIO,
	NVME_CTRL_DEAD,
};

struct nvme_fault_inject {
#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS
	struct fault_attr attr;
	struct dentry *parent;
	bool dont_retry;	/* DNR, do not retry */
	u16 status;		/* status code */
#endif
};

enum nvme_ctrl_flags {
	NVME_CTRL_FAILFAST_EXPIRED	= 0,
	NVME_CTRL_ADMIN_Q_STOPPED	= 1,
	NVME_CTRL_STARTED_ONCE		= 2,
	NVME_CTRL_STOPPED		= 3,
	NVME_CTRL_SKIP_ID_CNS_CS	= 4,
};

struct nvme_ctrl {
	bool comp_seen;
	bool identified;
	enum nvme_ctrl_state state;
	spinlock_t lock;
	struct mutex scan_lock;
	const struct nvme_ctrl_ops *ops;
	struct request_queue *admin_q;
	struct request_queue *connect_q;
	struct request_queue *fabrics_q;
	struct device *dev;
	int instance;
	int numa_node;
	struct blk_mq_tag_set *tagset;
	struct blk_mq_tag_set *admin_tagset;
	struct list_head namespaces;
	struct rw_semaphore namespaces_rwsem;
	struct device ctrl_device;
	struct device *device;	/* char device */
#ifdef CONFIG_NVME_HWMON
	struct device *hwmon_device;
#endif
	struct cdev cdev;
	struct work_struct reset_work;
	struct work_struct delete_work;
	wait_queue_head_t state_wq;

	struct nvme_subsystem *subsys;
	struct list_head subsys_entry;

	struct opal_dev *opal_dev;

	char name[12];
	u16 cntlid;

	u16 mtfa;
	u32 ctrl_config;
	u32 queue_count;

	u64 cap;
	u32 max_hw_sectors;
	u32 max_segments;
	u32 max_integrity_segments;
	u32 max_discard_sectors;
	u32 max_discard_segments;
	u32 max_zeroes_sectors;
#ifdef CONFIG_BLK_DEV_ZONED
	u32 max_zone_append;
#endif
	u16 crdt[3];
	u16 oncs;
	u32 dmrsl;
	u16 oacs;
	u16 sqsize;
	u32 max_namespaces;
	atomic_t abort_limit;
	u8 vwc;
	u32 vs;
	u32 sgls;
	u16 kas;
	u8 npss;
	u8 apsta;
	u16 wctemp;
	u16 cctemp;
	u32 oaes;
	u32 aen_result;
	u32 ctratt;
	unsigned int shutdown_timeout;
	unsigned int kato;
	bool subsystem;
	unsigned long quirks;
	struct nvme_id_power_state psd[32];
	struct nvme_effects_log *effects;
	struct xarray cels;
	struct work_struct scan_work;
	struct work_struct async_event_work;
	struct delayed_work ka_work;
	struct delayed_work failfast_work;
	struct nvme_command ka_cmd;
	unsigned long ka_last_check_time;
	struct work_struct fw_act_work;
	unsigned long events;

#ifdef CONFIG_NVME_MULTIPATH
	/* asymmetric namespace access: */
	u8 anacap;
	u8 anatt;
	u32 anagrpmax;
	u32 nanagrpid;
	struct mutex ana_lock;
	struct nvme_ana_rsp_hdr *ana_log_buf;
	size_t ana_log_size;
	struct timer_list anatt_timer;
	struct work_struct ana_work;
#endif

#ifdef CONFIG_NVME_AUTH
	struct work_struct dhchap_auth_work;
	struct mutex dhchap_auth_mutex;
	struct nvme_dhchap_queue_context *dhchap_ctxs;
	struct nvme_dhchap_key *host_key;
	struct nvme_dhchap_key *ctrl_key;
	u16 transaction;
#endif

	/* Power saving configuration */
	u64 ps_max_latency_us;
	bool apst_enabled;

	/* PCIe only: */
	u16 hmmaxd;
	u32 hmpre;
	u32 hmmin;
	u32 hmminds;

	/* Fabrics only */
	u32 ioccsz;
	u32 iorcsz;
	u16 icdoff;
	u16 maxcmd;
	int nr_reconnects;
	unsigned long flags;
	struct nvmf_ctrl_options *opts;

	struct page *discard_page;
	unsigned long discard_page_busy;

	struct nvme_fault_inject fault_inject;

	enum nvme_ctrl_type cntrltype;
	enum nvme_dctype dctype;
};

enum nvme_iopolicy {
	NVME_IOPOLICY_NUMA,
	NVME_IOPOLICY_RR,
};

struct nvme_subsystem {
	int			instance;
	struct device		dev;
	/*
	 * Because we unregister the device on the last put we need
	 * a separate refcount.
	 */
	struct kref		ref;
	struct list_head	entry;
	struct mutex		lock;
	struct list_head	ctrls;
	struct list_head	nsheads;
	char			subnqn[NVMF_NQN_SIZE];
	char			serial[20];
	char			model[40];
	char			firmware_rev[8];
	u8			cmic;
	enum nvme_subsys_type	subtype;
	u16			vendor_id;
	u16			awupf;	/* 0's based awupf value. */
	struct ida		ns_ida;
#ifdef CONFIG_NVME_MULTIPATH
	enum nvme_iopolicy	iopolicy;
#endif
};

/*
 * Container structure for uniqueue namespace identifiers.
 */
struct nvme_ns_ids {
	u8	eui64[8];
	u8	nguid[16];
	uuid_t	uuid;
	u8	csi;
};

/*
 * Anchor structure for namespaces.  There is one for each namespace in a
 * NVMe subsystem that any of our controllers can see, and the namespace
 * structure for each controller is chained of it.  For private namespaces
 * there is a 1:1 relation to our namespace structures, that is ->list
 * only ever has a single entry for private namespaces.
 */
struct nvme_ns_head {
	struct list_head	list;
	struct srcu_struct      srcu;
	struct nvme_subsystem	*subsys;
	unsigned		ns_id;
	struct nvme_ns_ids	ids;
	struct list_head	entry;
	struct kref		ref;
	bool			shared;
	int			instance;
	struct nvme_effects_log *effects;

	struct cdev		cdev;
	struct device		cdev_device;

	struct gendisk		*disk;
#ifdef CONFIG_NVME_MULTIPATH
	struct bio_list		requeue_list;
	spinlock_t		requeue_lock;
	struct work_struct	requeue_work;
	struct mutex		lock;
	unsigned long		flags;
#define NVME_NSHEAD_DISK_LIVE	0
	struct nvme_ns __rcu	*current_path[];
#endif
};

static inline bool nvme_ns_head_multipath(struct nvme_ns_head *head)
{
	return IS_ENABLED(CONFIG_NVME_MULTIPATH) && head->disk;
}

enum nvme_ns_features {
	NVME_NS_EXT_LBAS = 1 << 0, /* support extended LBA format */
	NVME_NS_METADATA_SUPPORTED = 1 << 1, /* support getting generated md */
	NVME_NS_DEAC,		/* DEAC bit in Write Zeores supported */
};

struct nvme_ns {
	struct list_head list;

	struct nvme_ctrl *ctrl;
	struct request_queue *queue;
	struct gendisk *disk;
#ifdef CONFIG_NVME_MULTIPATH
	enum nvme_ana_state ana_state;
	u32 ana_grpid;
#endif
	struct list_head siblings;
	struct kref kref;
	struct nvme_ns_head *head;

	int lba_shift;
	u16 ms;
	u16 pi_size;
	u16 sgs;
	u32 sws;
	u8 pi_type;
	u8 guard_type;
#ifdef CONFIG_BLK_DEV_ZONED
	u64 zsze;
#endif
	unsigned long features;
	unsigned long flags;
#define NVME_NS_REMOVING	0
#define NVME_NS_ANA_PENDING	2
#define NVME_NS_FORCE_RO	3
#define NVME_NS_READY		4

	struct cdev		cdev;
	struct device		cdev_device;

	struct nvme_fault_inject fault_inject;

};

/* NVMe ns supports metadata actions by the controller (generate/strip) */
static inline bool nvme_ns_has_pi(struct nvme_ns *ns)
{
	return ns->pi_type && ns->ms == ns->pi_size;
}

struct nvme_ctrl_ops {
	const char *name;
	struct module *module;
	unsigned int flags;
#define NVME_F_FABRICS			(1 << 0)
#define NVME_F_METADATA_SUPPORTED	(1 << 1)
#define NVME_F_BLOCKING			(1 << 2)

	const struct attribute_group **dev_attr_groups;
	int (*reg_read32)(struct nvme_ctrl *ctrl, u32 off, u32 *val);
	int (*reg_write32)(struct nvme_ctrl *ctrl, u32 off, u32 val);
	int (*reg_read64)(struct nvme_ctrl *ctrl, u32 off, u64 *val);
	void (*free_ctrl)(struct nvme_ctrl *ctrl);
	void (*submit_async_event)(struct nvme_ctrl *ctrl);
	void (*delete_ctrl)(struct nvme_ctrl *ctrl);
	void (*stop_ctrl)(struct nvme_ctrl *ctrl);
	int (*get_address)(struct nvme_ctrl *ctrl, char *buf, int size);
	void (*print_device_info)(struct nvme_ctrl *ctrl);
	bool (*supports_pci_p2pdma)(struct nvme_ctrl *ctrl);
};

/*
 * nvme command_id is constructed as such:
 * | xxxx | xxxxxxxxxxxx |
 *   gen    request tag
 */
#define nvme_genctr_mask(gen)			(gen & 0xf)
#define nvme_cid_install_genctr(gen)		(nvme_genctr_mask(gen) << 12)
#define nvme_genctr_from_cid(cid)		((cid & 0xf000) >> 12)
#define nvme_tag_from_cid(cid)			(cid & 0xfff)

static inline u16 nvme_cid(struct request *rq)
{
	return nvme_cid_install_genctr(nvme_req(rq)->genctr) | rq->tag;
}

static inline struct request *nvme_find_rq(struct blk_mq_tags *tags,
		u16 command_id)
{
	u8 genctr = nvme_genctr_from_cid(command_id);
	u16 tag = nvme_tag_from_cid(command_id);
	struct request *rq;

	rq = blk_mq_tag_to_rq(tags, tag);
	if (unlikely(!rq)) {
		pr_err("could not locate request for tag %#x\n",
			tag);
		return NULL;
	}
	if (unlikely(nvme_genctr_mask(nvme_req(rq)->genctr) != genctr)) {
		dev_err(nvme_req(rq)->ctrl->device,
			"request %#x genctr mismatch (got %#x expected %#x)\n",
			tag, genctr, nvme_genctr_mask(nvme_req(rq)->genctr));
		return NULL;
	}
	return rq;
}

static inline struct request *nvme_cid_to_rq(struct blk_mq_tags *tags,
                u16 command_id)
{
	return blk_mq_tag_to_rq(tags, nvme_tag_from_cid(command_id));
}

/*
 * Return the length of the string without the space padding
 */
static inline int nvme_strlen(char *s, int len)
{
	while (s[len - 1] == ' ')
		len--;
	return len;
}

static inline void nvme_print_device_info(struct nvme_ctrl *ctrl)
{
	struct nvme_subsystem *subsys = ctrl->subsys;

	if (ctrl->ops->print_device_info) {
		ctrl->ops->print_device_info(ctrl);
		return;
	}

	dev_err(ctrl->device,
		"VID:%04x model:%.*s firmware:%.*s\n", subsys->vendor_id,
		nvme_strlen(subsys->model, sizeof(subsys->model)),
		subsys->model, nvme_strlen(subsys->firmware_rev,
					   sizeof(subsys->firmware_rev)),
		subsys->firmware_rev);
}

#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS
void nvme_fault_inject_init(struct nvme_fault_inject *fault_inj,
			    const char *dev_name);
void nvme_fault_inject_fini(struct nvme_fault_inject *fault_inject);
void nvme_should_fail(struct request *req);
#else
static inline void nvme_fault_inject_init(struct nvme_fault_inject *fault_inj,
					  const char *dev_name)
{
}
static inline void nvme_fault_inject_fini(struct nvme_fault_inject *fault_inj)
{
}
static inline void nvme_should_fail(struct request *req) {}
#endif

bool nvme_wait_reset(struct nvme_ctrl *ctrl);
int nvme_try_sched_reset(struct nvme_ctrl *ctrl);

static inline int nvme_reset_subsystem(struct nvme_ctrl *ctrl)
{
	int ret;

	if (!ctrl->subsystem)
		return -ENOTTY;
	if (!nvme_wait_reset(ctrl))
		return -EBUSY;

	ret = ctrl->ops->reg_write32(ctrl, NVME_REG_NSSR, 0x4E564D65);
	if (ret)
		return ret;

	return nvme_try_sched_reset(ctrl);
}

/*
 * Convert a 512B sector number to a device logical block number.
 */
static inline u64 nvme_sect_to_lba(struct nvme_ns *ns, sector_t sector)
{
	return sector >> (ns->lba_shift - SECTOR_SHIFT);
}

/*
 * Convert a device logical block number to a 512B sector number.
 */
static inline sector_t nvme_lba_to_sect(struct nvme_ns *ns, u64 lba)
{
	return lba << (ns->lba_shift - SECTOR_SHIFT);
}

/*
 * Convert byte length to nvme's 0-based num dwords
 */
static inline u32 nvme_bytes_to_numd(size_t len)
{
	return (len >> 2) - 1;
}

static inline bool nvme_is_ana_error(u16 status)
{
	switch (status & 0x7ff) {
	case NVME_SC_ANA_TRANSITION:
	case NVME_SC_ANA_INACCESSIBLE:
	case NVME_SC_ANA_PERSISTENT_LOSS:
		return true;
	default:
		return false;
	}
}

static inline bool nvme_is_path_error(u16 status)
{
	/* check for a status code type of 'path related status' */
	return (status & 0x700) == 0x300;
}

/*
 * Fill in the status and result information from the CQE, and then figure out
 * if blk-mq will need to use IPI magic to complete the request, and if yes do
 * so.  If not let the caller complete the request without an indirect function
 * call.
 */
static inline bool nvme_try_complete_req(struct request *req, __le16 status,
		union nvme_result result)
{
	struct nvme_request *rq = nvme_req(req);
	struct nvme_ctrl *ctrl = rq->ctrl;

	if (!(ctrl->quirks & NVME_QUIRK_SKIP_CID_GEN))
		rq->genctr++;

	rq->status = le16_to_cpu(status) >> 1;
	rq->result = result;
	/* inject error when permitted by fault injection framework */
	nvme_should_fail(req);
	if (unlikely(blk_should_fake_timeout(req->q)))
		return true;
	return blk_mq_complete_request_remote(req);
}

static inline void nvme_get_ctrl(struct nvme_ctrl *ctrl)
{
	get_device(ctrl->device);
}

static inline void nvme_put_ctrl(struct nvme_ctrl *ctrl)
{
	put_device(ctrl->device);
}

static inline bool nvme_is_aen_req(u16 qid, __u16 command_id)
{
	return !qid &&
		nvme_tag_from_cid(command_id) >= NVME_AQ_BLK_MQ_DEPTH;
}

void nvme_complete_rq(struct request *req);
void nvme_complete_batch_req(struct request *req);

static __always_inline void nvme_complete_batch(struct io_comp_batch *iob,
						void (*fn)(struct request *rq))
{
	struct request *req;

	rq_list_for_each(&iob->req_list, req) {
		fn(req);
		nvme_complete_batch_req(req);
	}
	blk_mq_end_request_batch(iob);
}

blk_status_t nvme_host_path_error(struct request *req);
bool nvme_cancel_request(struct request *req, void *data);
void nvme_cancel_tagset(struct nvme_ctrl *ctrl);
void nvme_cancel_admin_tagset(struct nvme_ctrl *ctrl);
bool nvme_change_ctrl_state(struct nvme_ctrl *ctrl,
		enum nvme_ctrl_state new_state);
int nvme_disable_ctrl(struct nvme_ctrl *ctrl, bool shutdown);
int nvme_enable_ctrl(struct nvme_ctrl *ctrl);
int nvme_init_ctrl(struct nvme_ctrl *ctrl, struct device *dev,
		const struct nvme_ctrl_ops *ops, unsigned long quirks);
void nvme_uninit_ctrl(struct nvme_ctrl *ctrl);
void nvme_start_ctrl(struct nvme_ctrl *ctrl);
void nvme_stop_ctrl(struct nvme_ctrl *ctrl);
int nvme_init_ctrl_finish(struct nvme_ctrl *ctrl, bool was_suspended);
int nvme_alloc_admin_tag_set(struct nvme_ctrl *ctrl, struct blk_mq_tag_set *set,
		const struct blk_mq_ops *ops, unsigned int cmd_size);
void nvme_remove_admin_tag_set(struct nvme_ctrl *ctrl);
int nvme_alloc_io_tag_set(struct nvme_ctrl *ctrl, struct blk_mq_tag_set *set,
		const struct blk_mq_ops *ops, unsigned int nr_maps,
		unsigned int cmd_size);
void nvme_remove_io_tag_set(struct nvme_ctrl *ctrl);

void nvme_remove_namespaces(struct nvme_ctrl *ctrl);

void nvme_complete_async_event(struct nvme_ctrl *ctrl, __le16 status,
		volatile union nvme_result *res);

void nvme_quiesce_io_queues(struct nvme_ctrl *ctrl);
void nvme_unquiesce_io_queues(struct nvme_ctrl *ctrl);
void nvme_quiesce_admin_queue(struct nvme_ctrl *ctrl);
void nvme_unquiesce_admin_queue(struct nvme_ctrl *ctrl);
void nvme_mark_namespaces_dead(struct nvme_ctrl *ctrl);
void nvme_sync_queues(struct nvme_ctrl *ctrl);
void nvme_sync_io_queues(struct nvme_ctrl *ctrl);
void nvme_unfreeze(struct nvme_ctrl *ctrl);
void nvme_wait_freeze(struct nvme_ctrl *ctrl);
int nvme_wait_freeze_timeout(struct nvme_ctrl *ctrl, long timeout);
void nvme_start_freeze(struct nvme_ctrl *ctrl);

static inline enum req_op nvme_req_op(struct nvme_command *cmd)
{
	return nvme_is_write(cmd) ? REQ_OP_DRV_OUT : REQ_OP_DRV_IN;
}

#define NVME_QID_ANY -1
void nvme_init_request(struct request *req, struct nvme_command *cmd);
void nvme_cleanup_cmd(struct request *req);
blk_status_t nvme_setup_cmd(struct nvme_ns *ns, struct request *req);
blk_status_t nvme_fail_nonready_command(struct nvme_ctrl *ctrl,
		struct request *req);
bool __nvme_check_ready(struct nvme_ctrl *ctrl, struct request *rq,
		bool queue_live);

static inline bool nvme_check_ready(struct nvme_ctrl *ctrl, struct request *rq,
		bool queue_live)
{
	if (likely(ctrl->state == NVME_CTRL_LIVE))
		return true;
	if (ctrl->ops->flags & NVME_F_FABRICS &&
	    ctrl->state == NVME_CTRL_DELETING)
		return queue_live;
	return __nvme_check_ready(ctrl, rq, queue_live);
}

/*
 * NSID shall be unique for all shared namespaces, or if at least one of the
 * following conditions is met:
 *   1. Namespace Management is supported by the controller
 *   2. ANA is supported by the controller
 *   3. NVM Set are supported by the controller
 *
 * In other case, private namespace are not required to report a unique NSID.
 */
static inline bool nvme_is_unique_nsid(struct nvme_ctrl *ctrl,
		struct nvme_ns_head *head)
{
	return head->shared ||
		(ctrl->oacs & NVME_CTRL_OACS_NS_MNGT_SUPP) ||
		(ctrl->subsys->cmic & NVME_CTRL_CMIC_ANA) ||
		(ctrl->ctratt & NVME_CTRL_CTRATT_NVM_SETS);
}

int nvme_submit_sync_cmd(struct request_queue *q, struct nvme_command *cmd,
		void *buf, unsigned bufflen);
int __nvme_submit_sync_cmd(struct request_queue *q, struct nvme_command *cmd,
		union nvme_result *result, void *buffer, unsigned bufflen,
		int qid, int at_head,
		blk_mq_req_flags_t flags);
int nvme_set_features(struct nvme_ctrl *dev, unsigned int fid,
		      unsigned int dword11, void *buffer, size_t buflen,
		      u32 *result);
int nvme_get_features(struct nvme_ctrl *dev, unsigned int fid,
		      unsigned int dword11, void *buffer, size_t buflen,
		      u32 *result);
int nvme_set_queue_count(struct nvme_ctrl *ctrl, int *count);
void nvme_stop_keep_alive(struct nvme_ctrl *ctrl);
int nvme_reset_ctrl(struct nvme_ctrl *ctrl);
int nvme_reset_ctrl_sync(struct nvme_ctrl *ctrl);
int nvme_delete_ctrl(struct nvme_ctrl *ctrl);
void nvme_queue_scan(struct nvme_ctrl *ctrl);
int nvme_get_log(struct nvme_ctrl *ctrl, u32 nsid, u8 log_page, u8 lsp, u8 csi,
		void *log, size_t size, u64 offset);
bool nvme_tryget_ns_head(struct nvme_ns_head *head);
void nvme_put_ns_head(struct nvme_ns_head *head);
int nvme_cdev_add(struct cdev *cdev, struct device *cdev_device,
		const struct file_operations *fops, struct module *owner);
void nvme_cdev_del(struct cdev *cdev, struct device *cdev_device);
int nvme_ioctl(struct block_device *bdev, blk_mode_t mode,
		unsigned int cmd, unsigned long arg);
long nvme_ns_chr_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int nvme_ns_head_ioctl(struct block_device *bdev, blk_mode_t mode,
		unsigned int cmd, unsigned long arg);
long nvme_ns_head_chr_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg);
long nvme_dev_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg);
int nvme_ns_chr_uring_cmd_iopoll(struct io_uring_cmd *ioucmd,
		struct io_comp_batch *iob, unsigned int poll_flags);
int nvme_ns_chr_uring_cmd(struct io_uring_cmd *ioucmd,
		unsigned int issue_flags);
int nvme_ns_head_chr_uring_cmd(struct io_uring_cmd *ioucmd,
		unsigned int issue_flags);
int nvme_getgeo(struct block_device *bdev, struct hd_geometry *geo);
int nvme_dev_uring_cmd(struct io_uring_cmd *ioucmd, unsigned int issue_flags);

extern const struct attribute_group *nvme_ns_id_attr_groups[];
extern const struct pr_ops nvme_pr_ops;
extern const struct block_device_operations nvme_ns_head_ops;
extern const struct attribute_group nvme_dev_attrs_group;
extern const struct attribute_group *nvme_subsys_attrs_groups[];
extern const struct attribute_group *nvme_dev_attr_groups[];
extern const struct block_device_operations nvme_bdev_ops;

void nvme_delete_ctrl_sync(struct nvme_ctrl *ctrl);
struct nvme_ns *nvme_find_path(struct nvme_ns_head *head);
#ifdef CONFIG_NVME_MULTIPATH
static inline bool nvme_ctrl_use_ana(struct nvme_ctrl *ctrl)
{
	return ctrl->ana_log_buf != NULL;
}

void nvme_mpath_unfreeze(struct nvme_subsystem *subsys);
void nvme_mpath_wait_freeze(struct nvme_subsystem *subsys);
void nvme_mpath_start_freeze(struct nvme_subsystem *subsys);
void nvme_mpath_default_iopolicy(struct nvme_subsystem *subsys);
void nvme_failover_req(struct request *req);
void nvme_kick_requeue_lists(struct nvme_ctrl *ctrl);
int nvme_mpath_alloc_disk(struct nvme_ctrl *ctrl,struct nvme_ns_head *head);
void nvme_mpath_add_disk(struct nvme_ns *ns, __le32 anagrpid);
void nvme_mpath_remove_disk(struct nvme_ns_head *head);
int nvme_mpath_init_identify(struct nvme_ctrl *ctrl, struct nvme_id_ctrl *id);
void nvme_mpath_init_ctrl(struct nvme_ctrl *ctrl);
void nvme_mpath_update(struct nvme_ctrl *ctrl);
void nvme_mpath_uninit(struct nvme_ctrl *ctrl);
void nvme_mpath_stop(struct nvme_ctrl *ctrl);
bool nvme_mpath_clear_current_path(struct nvme_ns *ns);
void nvme_mpath_revalidate_paths(struct nvme_ns *ns);
void nvme_mpath_clear_ctrl_paths(struct nvme_ctrl *ctrl);
void nvme_mpath_shutdown_disk(struct nvme_ns_head *head);
void nvme_mpath_start_request(struct request *rq);
void nvme_mpath_end_request(struct request *rq);

static inline void nvme_trace_bio_complete(struct request *req)
{
	struct nvme_ns *ns = req->q->queuedata;

	if ((req->cmd_flags & REQ_NVME_MPATH) && req->bio)
		trace_block_bio_complete(ns->head->disk->queue, req->bio);
}

extern bool multipath;
extern struct device_attribute dev_attr_ana_grpid;
extern struct device_attribute dev_attr_ana_state;
extern struct device_attribute subsys_attr_iopolicy;

#else
#define multipath false
static inline bool nvme_ctrl_use_ana(struct nvme_ctrl *ctrl)
{
	return false;
}
static inline void nvme_failover_req(struct request *req)
{
}
static inline void nvme_kick_requeue_lists(struct nvme_ctrl *ctrl)
{
}
static inline int nvme_mpath_alloc_disk(struct nvme_ctrl *ctrl,
		struct nvme_ns_head *head)
{
	return 0;
}
static inline void nvme_mpath_add_disk(struct nvme_ns *ns, __le32 anagrpid)
{
}
static inline void nvme_mpath_remove_disk(struct nvme_ns_head *head)
{
}
static inline bool nvme_mpath_clear_current_path(struct nvme_ns *ns)
{
	return false;
}
static inline void nvme_mpath_revalidate_paths(struct nvme_ns *ns)
{
}
static inline void nvme_mpath_clear_ctrl_paths(struct nvme_ctrl *ctrl)
{
}
static inline void nvme_mpath_shutdown_disk(struct nvme_ns_head *head)
{
}
static inline void nvme_trace_bio_complete(struct request *req)
{
}
static inline void nvme_mpath_init_ctrl(struct nvme_ctrl *ctrl)
{
}
static inline int nvme_mpath_init_identify(struct nvme_ctrl *ctrl,
		struct nvme_id_ctrl *id)
{
	if (ctrl->subsys->cmic & NVME_CTRL_CMIC_ANA)
		dev_warn(ctrl->device,
"Please enable CONFIG_NVME_MULTIPATH for full support of multi-port devices.\n");
	return 0;
}
static inline void nvme_mpath_update(struct nvme_ctrl *ctrl)
{
}
static inline void nvme_mpath_uninit(struct nvme_ctrl *ctrl)
{
}
static inline void nvme_mpath_stop(struct nvme_ctrl *ctrl)
{
}
static inline void nvme_mpath_unfreeze(struct nvme_subsystem *subsys)
{
}
static inline void nvme_mpath_wait_freeze(struct nvme_subsystem *subsys)
{
}
static inline void nvme_mpath_start_freeze(struct nvme_subsystem *subsys)
{
}
static inline void nvme_mpath_default_iopolicy(struct nvme_subsystem *subsys)
{
}
static inline void nvme_mpath_start_request(struct request *rq)
{
}
static inline void nvme_mpath_end_request(struct request *rq)
{
}
#endif /* CONFIG_NVME_MULTIPATH */

int nvme_revalidate_zones(struct nvme_ns *ns);
int nvme_ns_report_zones(struct nvme_ns *ns, sector_t sector,
		unsigned int nr_zones, report_zones_cb cb, void *data);
#ifdef CONFIG_BLK_DEV_ZONED
int nvme_update_zone_info(struct nvme_ns *ns, unsigned lbaf);
blk_status_t nvme_setup_zone_mgmt_send(struct nvme_ns *ns, struct request *req,
				       struct nvme_command *cmnd,
				       enum nvme_zone_mgmt_action action);
#else
static inline blk_status_t nvme_setup_zone_mgmt_send(struct nvme_ns *ns,
		struct request *req, struct nvme_command *cmnd,
		enum nvme_zone_mgmt_action action)
{
	return BLK_STS_NOTSUPP;
}

static inline int nvme_update_zone_info(struct nvme_ns *ns, unsigned lbaf)
{
	dev_warn(ns->ctrl->device,
		 "Please enable CONFIG_BLK_DEV_ZONED to support ZNS devices\n");
	return -EPROTONOSUPPORT;
}
#endif

static inline struct nvme_ns *nvme_get_ns_from_dev(struct device *dev)
{
	return dev_to_disk(dev)->private_data;
}

#ifdef CONFIG_NVME_HWMON
int nvme_hwmon_init(struct nvme_ctrl *ctrl);
void nvme_hwmon_exit(struct nvme_ctrl *ctrl);
#else
static inline int nvme_hwmon_init(struct nvme_ctrl *ctrl)
{
	return 0;
}

static inline void nvme_hwmon_exit(struct nvme_ctrl *ctrl)
{
}
#endif

static inline void nvme_start_request(struct request *rq)
{
	if (rq->cmd_flags & REQ_NVME_MPATH)
		nvme_mpath_start_request(rq);
	blk_mq_start_request(rq);
}

static inline bool nvme_ctrl_sgl_supported(struct nvme_ctrl *ctrl)
{
	return ctrl->sgls & ((1 << 0) | (1 << 1));
}

#ifdef CONFIG_NVME_AUTH
int __init nvme_init_auth(void);
void __exit nvme_exit_auth(void);
int nvme_auth_init_ctrl(struct nvme_ctrl *ctrl);
void nvme_auth_stop(struct nvme_ctrl *ctrl);
int nvme_auth_negotiate(struct nvme_ctrl *ctrl, int qid);
int nvme_auth_wait(struct nvme_ctrl *ctrl, int qid);
void nvme_auth_free(struct nvme_ctrl *ctrl);
#else
static inline int nvme_auth_init_ctrl(struct nvme_ctrl *ctrl)
{
	return 0;
}
static inline int __init nvme_init_auth(void)
{
	return 0;
}
static inline void __exit nvme_exit_auth(void)
{
}
static inline void nvme_auth_stop(struct nvme_ctrl *ctrl) {};
static inline int nvme_auth_negotiate(struct nvme_ctrl *ctrl, int qid)
{
	return -EPROTONOSUPPORT;
}
static inline int nvme_auth_wait(struct nvme_ctrl *ctrl, int qid)
{
	return NVME_SC_AUTH_REQUIRED;
}
static inline void nvme_auth_free(struct nvme_ctrl *ctrl) {};
#endif

u32 nvme_command_effects(struct nvme_ctrl *ctrl, struct nvme_ns *ns,
			 u8 opcode);
u32 nvme_passthru_start(struct nvme_ctrl *ctrl, struct nvme_ns *ns, u8 opcode);
int nvme_execute_rq(struct request *rq, bool at_head);
void nvme_passthru_end(struct nvme_ctrl *ctrl, struct nvme_ns *ns, u32 effects,
		       struct nvme_command *cmd, int status);
struct nvme_ctrl *nvme_ctrl_from_file(struct file *file);
struct nvme_ns *nvme_find_get_ns(struct nvme_ctrl *ctrl, unsigned nsid);
void nvme_put_ns(struct nvme_ns *ns);

static inline bool nvme_multi_css(struct nvme_ctrl *ctrl)
{
	return (ctrl->ctrl_config & NVME_CC_CSS_MASK) == NVME_CC_CSS_CSI;
}

#ifdef CONFIG_NVME_VERBOSE_ERRORS
const unsigned char *nvme_get_error_status_str(u16 status);
const unsigned char *nvme_get_opcode_str(u8 opcode);
const unsigned char *nvme_get_admin_opcode_str(u8 opcode);
const unsigned char *nvme_get_fabrics_opcode_str(u8 opcode);
#else /* CONFIG_NVME_VERBOSE_ERRORS */
static inline const unsigned char *nvme_get_error_status_str(u16 status)
{
	return "I/O Error";
}
static inline const unsigned char *nvme_get_opcode_str(u8 opcode)
{
	return "I/O Cmd";
}
static inline const unsigned char *nvme_get_admin_opcode_str(u8 opcode)
{
	return "Admin Cmd";
}

static inline const unsigned char *nvme_get_fabrics_opcode_str(u8 opcode)
{
	return "Fabrics Cmd";
}
#endif /* CONFIG_NVME_VERBOSE_ERRORS */

static inline const unsigned char *nvme_opcode_str(int qid, u8 opcode, u8 fctype)
{
	if (opcode == nvme_fabrics_command)
		return nvme_get_fabrics_opcode_str(fctype);
	return qid ? nvme_get_opcode_str(opcode) :
		nvme_get_admin_opcode_str(opcode);
}
#endif /* _NVME_H */

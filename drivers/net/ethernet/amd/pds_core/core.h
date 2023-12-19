/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#ifndef _PDSC_H_
#define _PDSC_H_

#include <linux/debugfs.h>
#include <net/devlink.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>
#include <linux/pds/pds_adminq.h>
#include <linux/pds/pds_intr.h>

#define PDSC_DRV_DESCRIPTION	"AMD/Pensando Core Driver"

#define PDSC_WATCHDOG_SECS	5
#define PDSC_QUEUE_NAME_MAX_SZ  16
#define PDSC_ADMINQ_MIN_LENGTH	16	/* must be a power of two */
#define PDSC_NOTIFYQ_LENGTH	64	/* must be a power of two */
#define PDSC_TEARDOWN_RECOVERY	false
#define PDSC_TEARDOWN_REMOVING	true
#define PDSC_SETUP_RECOVERY	false
#define PDSC_SETUP_INIT		true

struct pdsc_dev_bar {
	void __iomem *vaddr;
	phys_addr_t bus_addr;
	unsigned long len;
	int res_index;
};

struct pdsc;

struct pdsc_vf {
	struct pds_auxiliary_dev *padev;
	struct pdsc *vf;
	u16     index;
	__le16  vif_types[PDS_DEV_TYPE_MAX];
};

struct pdsc_devinfo {
	u8 asic_type;
	u8 asic_rev;
	char fw_version[PDS_CORE_DEVINFO_FWVERS_BUFLEN + 1];
	char serial_num[PDS_CORE_DEVINFO_SERIAL_BUFLEN + 1];
};

struct pdsc_queue {
	struct pdsc_q_info *info;
	u64 dbval;
	u16 head_idx;
	u16 tail_idx;
	u8 hw_type;
	unsigned int index;
	unsigned int num_descs;
	u64 dbell_count;
	u64 features;
	unsigned int type;
	unsigned int hw_index;
	union {
		void *base;
		struct pds_core_admin_cmd *adminq;
	};
	dma_addr_t base_pa;	/* must be page aligned */
	unsigned int desc_size;
	unsigned int pid;
	char name[PDSC_QUEUE_NAME_MAX_SZ];
};

#define PDSC_INTR_NAME_MAX_SZ		32

struct pdsc_intr_info {
	char name[PDSC_INTR_NAME_MAX_SZ];
	unsigned int index;
	unsigned int vector;
	void *data;
};

struct pdsc_cq_info {
	void *comp;
};

struct pdsc_buf_info {
	struct page *page;
	dma_addr_t dma_addr;
	u32 page_offset;
	u32 len;
};

struct pdsc_q_info {
	union {
		void *desc;
		struct pdsc_admin_cmd *adminq_desc;
	};
	unsigned int bytes;
	unsigned int nbufs;
	struct pdsc_buf_info bufs[PDS_CORE_MAX_FRAGS];
	struct pdsc_wait_context *wc;
	void *dest;
};

struct pdsc_cq {
	struct pdsc_cq_info *info;
	struct pdsc_queue *bound_q;
	struct pdsc_intr_info *bound_intr;
	u16 tail_idx;
	bool done_color;
	unsigned int num_descs;
	unsigned int desc_size;
	void *base;
	dma_addr_t base_pa;	/* must be page aligned */
} ____cacheline_aligned_in_smp;

struct pdsc_qcq {
	struct pdsc *pdsc;
	void *q_base;
	dma_addr_t q_base_pa;	/* might not be page aligned */
	void *cq_base;
	dma_addr_t cq_base_pa;	/* might not be page aligned */
	u32 q_size;
	u32 cq_size;
	bool armed;
	unsigned int flags;

	struct work_struct work;
	struct pdsc_queue q;
	struct pdsc_cq cq;
	int intx;

	u32 accum_work;
	struct dentry *dentry;
};

struct pdsc_viftype {
	char *name;
	bool supported;
	bool enabled;
	int dl_id;
	int vif_id;
	struct pds_auxiliary_dev *padev;
};

/* No state flags set means we are in a steady running state */
enum pdsc_state_flags {
	PDSC_S_FW_DEAD,		    /* stopped, wait on startup or recovery */
	PDSC_S_INITING_DRIVER,	    /* initial startup from probe */
	PDSC_S_STOPPING_DRIVER,	    /* driver remove */

	/* leave this as last */
	PDSC_S_STATE_SIZE
};

struct pdsc {
	struct pci_dev *pdev;
	struct dentry *dentry;
	struct device *dev;
	struct pdsc_dev_bar bars[PDS_CORE_BARS_MAX];
	struct pdsc_vf *vfs;
	int num_vfs;
	int vf_id;
	int hw_index;
	int uid;

	unsigned long state;
	u8 fw_status;
	u8 fw_generation;
	unsigned long last_fw_time;
	u32 last_hb;
	struct timer_list wdtimer;
	unsigned int wdtimer_period;
	struct work_struct health_work;
	struct devlink_health_reporter *fw_reporter;
	u32 fw_recoveries;

	struct pdsc_devinfo dev_info;
	struct pds_core_dev_identity dev_ident;
	unsigned int nintrs;
	struct pdsc_intr_info *intr_info;	/* array of nintrs elements */

	struct workqueue_struct *wq;

	unsigned int devcmd_timeout;
	struct mutex devcmd_lock;	/* lock for dev_cmd operations */
	struct mutex config_lock;	/* lock for configuration operations */
	spinlock_t adminq_lock;		/* lock for adminq operations */
	struct pds_core_dev_info_regs __iomem *info_regs;
	struct pds_core_dev_cmd_regs __iomem *cmd_regs;
	struct pds_core_intr __iomem *intr_ctrl;
	u64 __iomem *intr_status;
	u64 __iomem *db_pages;
	dma_addr_t phy_db_pages;
	u64 __iomem *kern_dbpage;

	struct pdsc_qcq adminqcq;
	struct pdsc_qcq notifyqcq;
	u64 last_eid;
	struct pdsc_viftype *viftype_status;
};

/** enum pds_core_dbell_bits - bitwise composition of dbell values.
 *
 * @PDS_CORE_DBELL_QID_MASK:	unshifted mask of valid queue id bits.
 * @PDS_CORE_DBELL_QID_SHIFT:	queue id shift amount in dbell value.
 * @PDS_CORE_DBELL_QID:		macro to build QID component of dbell value.
 *
 * @PDS_CORE_DBELL_RING_MASK:	unshifted mask of valid ring bits.
 * @PDS_CORE_DBELL_RING_SHIFT:	ring shift amount in dbell value.
 * @PDS_CORE_DBELL_RING:	macro to build ring component of dbell value.
 *
 * @PDS_CORE_DBELL_RING_0:	ring zero dbell component value.
 * @PDS_CORE_DBELL_RING_1:	ring one dbell component value.
 * @PDS_CORE_DBELL_RING_2:	ring two dbell component value.
 * @PDS_CORE_DBELL_RING_3:	ring three dbell component value.
 *
 * @PDS_CORE_DBELL_INDEX_MASK:	bit mask of valid index bits, no shift needed.
 */
enum pds_core_dbell_bits {
	PDS_CORE_DBELL_QID_MASK		= 0xffffff,
	PDS_CORE_DBELL_QID_SHIFT		= 24,

#define PDS_CORE_DBELL_QID(n) \
	(((u64)(n) & PDS_CORE_DBELL_QID_MASK) << PDS_CORE_DBELL_QID_SHIFT)

	PDS_CORE_DBELL_RING_MASK		= 0x7,
	PDS_CORE_DBELL_RING_SHIFT		= 16,

#define PDS_CORE_DBELL_RING(n) \
	(((u64)(n) & PDS_CORE_DBELL_RING_MASK) << PDS_CORE_DBELL_RING_SHIFT)

	PDS_CORE_DBELL_RING_0		= 0,
	PDS_CORE_DBELL_RING_1		= PDS_CORE_DBELL_RING(1),
	PDS_CORE_DBELL_RING_2		= PDS_CORE_DBELL_RING(2),
	PDS_CORE_DBELL_RING_3		= PDS_CORE_DBELL_RING(3),

	PDS_CORE_DBELL_INDEX_MASK		= 0xffff,
};

static inline void pds_core_dbell_ring(u64 __iomem *db_page,
				       enum pds_core_logical_qtype qtype,
				       u64 val)
{
	writeq(val, &db_page[qtype]);
}

int pdsc_fw_reporter_diagnose(struct devlink_health_reporter *reporter,
			      struct devlink_fmsg *fmsg,
			      struct netlink_ext_ack *extack);
int pdsc_dl_info_get(struct devlink *dl, struct devlink_info_req *req,
		     struct netlink_ext_ack *extack);
int pdsc_dl_flash_update(struct devlink *dl,
			 struct devlink_flash_update_params *params,
			 struct netlink_ext_ack *extack);
int pdsc_dl_enable_get(struct devlink *dl, u32 id,
		       struct devlink_param_gset_ctx *ctx);
int pdsc_dl_enable_set(struct devlink *dl, u32 id,
		       struct devlink_param_gset_ctx *ctx);
int pdsc_dl_enable_validate(struct devlink *dl, u32 id,
			    union devlink_param_value val,
			    struct netlink_ext_ack *extack);

void __iomem *pdsc_map_dbpage(struct pdsc *pdsc, int page_num);

void pdsc_debugfs_create(void);
void pdsc_debugfs_destroy(void);
void pdsc_debugfs_add_dev(struct pdsc *pdsc);
void pdsc_debugfs_del_dev(struct pdsc *pdsc);
void pdsc_debugfs_add_ident(struct pdsc *pdsc);
void pdsc_debugfs_add_viftype(struct pdsc *pdsc);
void pdsc_debugfs_add_irqs(struct pdsc *pdsc);
void pdsc_debugfs_add_qcq(struct pdsc *pdsc, struct pdsc_qcq *qcq);
void pdsc_debugfs_del_qcq(struct pdsc_qcq *qcq);

int pdsc_err_to_errno(enum pds_core_status_code code);
bool pdsc_is_fw_running(struct pdsc *pdsc);
bool pdsc_is_fw_good(struct pdsc *pdsc);
int pdsc_devcmd(struct pdsc *pdsc, union pds_core_dev_cmd *cmd,
		union pds_core_dev_comp *comp, int max_seconds);
int pdsc_devcmd_locked(struct pdsc *pdsc, union pds_core_dev_cmd *cmd,
		       union pds_core_dev_comp *comp, int max_seconds);
int pdsc_devcmd_init(struct pdsc *pdsc);
int pdsc_devcmd_reset(struct pdsc *pdsc);
int pdsc_dev_reinit(struct pdsc *pdsc);
int pdsc_dev_init(struct pdsc *pdsc);

void pdsc_reset_prepare(struct pci_dev *pdev);
void pdsc_reset_done(struct pci_dev *pdev);

int pdsc_intr_alloc(struct pdsc *pdsc, char *name,
		    irq_handler_t handler, void *data);
void pdsc_intr_free(struct pdsc *pdsc, int index);
void pdsc_qcq_free(struct pdsc *pdsc, struct pdsc_qcq *qcq);
int pdsc_qcq_alloc(struct pdsc *pdsc, unsigned int type, unsigned int index,
		   const char *name, unsigned int flags, unsigned int num_descs,
		   unsigned int desc_size, unsigned int cq_desc_size,
		   unsigned int pid, struct pdsc_qcq *qcq);
int pdsc_setup(struct pdsc *pdsc, bool init);
void pdsc_teardown(struct pdsc *pdsc, bool removing);
int pdsc_start(struct pdsc *pdsc);
void pdsc_stop(struct pdsc *pdsc);
void pdsc_health_thread(struct work_struct *work);

int pdsc_register_notify(struct notifier_block *nb);
void pdsc_unregister_notify(struct notifier_block *nb);
void pdsc_notify(unsigned long event, void *data);
int pdsc_auxbus_dev_add(struct pdsc *cf, struct pdsc *pf);
int pdsc_auxbus_dev_del(struct pdsc *cf, struct pdsc *pf);

void pdsc_process_adminq(struct pdsc_qcq *qcq);
void pdsc_work_thread(struct work_struct *work);
irqreturn_t pdsc_adminq_isr(int irq, void *data);

int pdsc_firmware_update(struct pdsc *pdsc, const struct firmware *fw,
			 struct netlink_ext_ack *extack);

void pdsc_fw_down(struct pdsc *pdsc);
void pdsc_fw_up(struct pdsc *pdsc);

#endif /* _PDSC_H_ */

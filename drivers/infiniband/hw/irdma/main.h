/* SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB */
/* Copyright (c) 2015 - 2021 Intel Corporation */
#ifndef IRDMA_MAIN_H
#define IRDMA_MAIN_H

#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/if_vlan.h>
#include <net/addrconf.h>
#include <net/netevent.h>
#include <net/tcp.h>
#include <net/ip6_route.h>
#include <net/flow.h>
#include <net/secure_seq.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/crc32c.h>
#include <linux/kthread.h>
#ifndef CONFIG_64BIT
#include <linux/io-64-nonatomic-lo-hi.h>
#endif
#include <linux/auxiliary_bus.h>
#include <linux/net/intel/iidc.h>
#include <crypto/hash.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_pack.h>
#include <rdma/rdma_cm.h>
#include <rdma/iw_cm.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_cache.h>
#include <rdma/uverbs_ioctl.h>
#include "osdep.h"
#include "defs.h"
#include "hmc.h"
#include "type.h"
#include "ws.h"
#include "protos.h"
#include "pble.h"
#include "cm.h"
#include <rdma/irdma-abi.h>
#include "verbs.h"
#include "user.h"
#include "puda.h"

extern struct auxiliary_driver i40iw_auxiliary_drv;

#define IRDMA_FW_VER_DEFAULT	2
#define IRDMA_HW_VER	        2

#define IRDMA_ARP_ADD		1
#define IRDMA_ARP_DELETE	2
#define IRDMA_ARP_RESOLVE	3

#define IRDMA_MACIP_ADD		1
#define IRDMA_MACIP_DELETE	2

#define IW_CCQ_SIZE	(IRDMA_CQP_SW_SQSIZE_2048 + 1)
#define IW_CEQ_SIZE	2048
#define IW_AEQ_SIZE	2048

#define RX_BUF_SIZE	(1536 + 8)
#define IW_REG0_SIZE	(4 * 1024)
#define IW_TX_TIMEOUT	(6 * HZ)
#define IW_FIRST_QPN	1

#define IW_SW_CONTEXT_ALIGN	1024

#define MAX_DPC_ITERATIONS	128

#define IRDMA_EVENT_TIMEOUT		50000
#define IRDMA_VCHNL_EVENT_TIMEOUT	100000
#define IRDMA_RST_TIMEOUT_HZ		4

#define	IRDMA_NO_QSET	0xffff

#define IW_CFG_FPM_QP_COUNT		32768
#define IRDMA_MAX_PAGES_PER_FMR		262144
#define IRDMA_MIN_PAGES_PER_FMR		1
#define IRDMA_CQP_COMPL_RQ_WQE_FLUSHED	2
#define IRDMA_CQP_COMPL_SQ_WQE_FLUSHED	3

#define IRDMA_Q_TYPE_PE_AEQ	0x80
#define IRDMA_Q_INVALID_IDX	0xffff
#define IRDMA_REM_ENDPOINT_TRK_QPID	3

#define IRDMA_DRV_OPT_ENA_MPA_VER_0		0x00000001
#define IRDMA_DRV_OPT_DISABLE_MPA_CRC		0x00000002
#define IRDMA_DRV_OPT_DISABLE_FIRST_WRITE	0x00000004
#define IRDMA_DRV_OPT_DISABLE_INTF		0x00000008
#define IRDMA_DRV_OPT_ENA_MSI			0x00000010
#define IRDMA_DRV_OPT_DUAL_LOGICAL_PORT		0x00000020
#define IRDMA_DRV_OPT_NO_INLINE_DATA		0x00000080
#define IRDMA_DRV_OPT_DISABLE_INT_MOD		0x00000100
#define IRDMA_DRV_OPT_DISABLE_VIRT_WQ		0x00000200
#define IRDMA_DRV_OPT_ENA_PAU			0x00000400
#define IRDMA_DRV_OPT_MCAST_LOGPORT_MAP		0x00000800

#define IW_HMC_OBJ_TYPE_NUM	ARRAY_SIZE(iw_hmc_obj_types)
#define IRDMA_ROCE_CWND_DEFAULT			0x400
#define IRDMA_ROCE_ACKCREDS_DEFAULT		0x1E

#define IRDMA_FLUSH_SQ		BIT(0)
#define IRDMA_FLUSH_RQ		BIT(1)
#define IRDMA_REFLUSH		BIT(2)
#define IRDMA_FLUSH_WAIT	BIT(3)

#define IRDMA_IRQ_NAME_STR_LEN (64)

enum init_completion_state {
	INVALID_STATE = 0,
	INITIAL_STATE,
	CQP_CREATED,
	HMC_OBJS_CREATED,
	HW_RSRC_INITIALIZED,
	CCQ_CREATED,
	CEQ0_CREATED, /* Last state of probe */
	ILQ_CREATED,
	IEQ_CREATED,
	CEQS_CREATED,
	PBLE_CHUNK_MEM,
	AEQ_CREATED,
	IP_ADDR_REGISTERED,  /* Last state of open */
};

struct irdma_rsrc_limits {
	u32 qplimit;
	u32 mrlimit;
	u32 cqlimit;
};

struct irdma_cqp_err_info {
	u16 maj;
	u16 min;
	const char *desc;
};

struct irdma_cqp_compl_info {
	u32 op_ret_val;
	u16 maj_err_code;
	u16 min_err_code;
	bool error;
	u8 op_code;
};

struct irdma_cqp_request {
	struct cqp_cmds_info info;
	wait_queue_head_t waitq;
	struct list_head list;
	refcount_t refcnt;
	void (*callback_fcn)(struct irdma_cqp_request *cqp_request);
	void *param;
	struct irdma_cqp_compl_info compl_info;
	bool waiting:1;
	bool request_done:1;
	bool dynamic:1;
};

struct irdma_cqp {
	struct irdma_sc_cqp sc_cqp;
	spinlock_t req_lock; /* protect CQP request list */
	spinlock_t compl_lock; /* protect CQP completion processing */
	wait_queue_head_t waitq;
	wait_queue_head_t remove_wq;
	struct irdma_dma_mem sq;
	struct irdma_dma_mem host_ctx;
	u64 *scratch_array;
	struct irdma_cqp_request *cqp_requests;
	struct list_head cqp_avail_reqs;
	struct list_head cqp_pending_reqs;
};

struct irdma_ccq {
	struct irdma_sc_cq sc_cq;
	struct irdma_dma_mem mem_cq;
	struct irdma_dma_mem shadow_area;
};

struct irdma_ceq {
	struct irdma_sc_ceq sc_ceq;
	struct irdma_dma_mem mem;
	u32 irq;
	u32 msix_idx;
	struct irdma_pci_f *rf;
	struct tasklet_struct dpc_tasklet;
	spinlock_t ce_lock; /* sync cq destroy with cq completion event notification */
};

struct irdma_aeq {
	struct irdma_sc_aeq sc_aeq;
	struct irdma_dma_mem mem;
	struct irdma_pble_alloc palloc;
	bool virtual_map;
};

struct irdma_arp_entry {
	u32 ip_addr[4];
	u8 mac_addr[ETH_ALEN];
};

struct irdma_msix_vector {
	u32 idx;
	u32 irq;
	u32 cpu_affinity;
	u32 ceq_id;
	cpumask_t mask;
	char name[IRDMA_IRQ_NAME_STR_LEN];
};

struct irdma_mc_table_info {
	u32 mgn;
	u32 dest_ip[4];
	bool lan_fwd:1;
	bool ipv4_valid:1;
};

struct mc_table_list {
	struct list_head list;
	struct irdma_mc_table_info mc_info;
	struct irdma_mcast_grp_info mc_grp_ctx;
};

struct irdma_qv_info {
	u32 v_idx; /* msix_vector */
	u16 ceq_idx;
	u16 aeq_idx;
	u8 itr_idx;
};

struct irdma_qvlist_info {
	u32 num_vectors;
	struct irdma_qv_info qv_info[1];
};

struct irdma_gen_ops {
	void (*request_reset)(struct irdma_pci_f *rf);
	int (*register_qset)(struct irdma_sc_vsi *vsi,
			     struct irdma_ws_node *tc_node);
	void (*unregister_qset)(struct irdma_sc_vsi *vsi,
				struct irdma_ws_node *tc_node);
};

struct irdma_pci_f {
	bool reset:1;
	bool rsrc_created:1;
	bool msix_shared:1;
	u8 rsrc_profile;
	u8 *hmc_info_mem;
	u8 *mem_rsrc;
	u8 rdma_ver;
	u8 rst_to;
	u8 pf_id;
	enum irdma_protocol_used protocol_used;
	u32 sd_type;
	u32 msix_count;
	u32 max_mr;
	u32 max_qp;
	u32 max_cq;
	u32 max_ah;
	u32 next_ah;
	u32 max_mcg;
	u32 next_mcg;
	u32 max_pd;
	u32 next_qp;
	u32 next_cq;
	u32 next_pd;
	u32 max_mr_size;
	u32 max_cqe;
	u32 mr_stagmask;
	u32 used_pds;
	u32 used_cqs;
	u32 used_mrs;
	u32 used_qps;
	u32 arp_table_size;
	u32 next_arp_index;
	u32 ceqs_count;
	u32 next_ws_node_id;
	u32 max_ws_node_id;
	u32 limits_sel;
	unsigned long *allocated_ws_nodes;
	unsigned long *allocated_qps;
	unsigned long *allocated_cqs;
	unsigned long *allocated_mrs;
	unsigned long *allocated_pds;
	unsigned long *allocated_mcgs;
	unsigned long *allocated_ahs;
	unsigned long *allocated_arps;
	enum init_completion_state init_state;
	struct irdma_sc_dev sc_dev;
	struct pci_dev *pcidev;
	void *cdev;
	struct irdma_hw hw;
	struct irdma_cqp cqp;
	struct irdma_ccq ccq;
	struct irdma_aeq aeq;
	struct irdma_ceq *ceqlist;
	struct irdma_hmc_pble_rsrc *pble_rsrc;
	struct irdma_arp_entry *arp_table;
	spinlock_t arp_lock; /*protect ARP table access*/
	spinlock_t rsrc_lock; /* protect HW resource array access */
	spinlock_t qptable_lock; /*protect QP table access*/
	struct irdma_qp **qp_table;
	spinlock_t qh_list_lock; /* protect mc_qht_list */
	struct mc_table_list mc_qht_list;
	struct irdma_msix_vector *iw_msixtbl;
	struct irdma_qvlist_info *iw_qvlist;
	struct tasklet_struct dpc_tasklet;
	struct msix_entry *msix_entries;
	struct irdma_dma_mem obj_mem;
	struct irdma_dma_mem obj_next;
	atomic_t vchnl_msgs;
	wait_queue_head_t vchnl_waitq;
	struct workqueue_struct *cqp_cmpl_wq;
	struct work_struct cqp_cmpl_work;
	struct irdma_sc_vsi default_vsi;
	void *back_fcn;
	struct irdma_gen_ops gen_ops;
	struct irdma_device *iwdev;
};

struct irdma_device {
	struct ib_device ibdev;
	struct irdma_pci_f *rf;
	struct net_device *netdev;
	struct workqueue_struct *cleanup_wq;
	struct irdma_sc_vsi vsi;
	struct irdma_cm_core cm_core;
	DECLARE_HASHTABLE(ah_hash_tbl, 8);
	struct mutex ah_tbl_lock; /* protect AH hash table access */
	u32 roce_cwnd;
	u32 roce_ackcreds;
	u32 vendor_id;
	u32 vendor_part_id;
	u32 push_mode;
	u32 rcv_wnd;
	u16 mac_ip_table_idx;
	u16 vsi_num;
	u8 rcv_wscale;
	u8 iw_status;
	bool roce_mode:1;
	bool roce_dcqcn_en:1;
	bool dcb_vlan_mode:1;
	bool iw_ooo:1;
	enum init_completion_state init_state;

	wait_queue_head_t suspend_wq;
};

static inline struct irdma_device *to_iwdev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct irdma_device, ibdev);
}

static inline struct irdma_ucontext *to_ucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct irdma_ucontext, ibucontext);
}

static inline struct irdma_user_mmap_entry *
to_irdma_mmap_entry(struct rdma_user_mmap_entry *rdma_entry)
{
	return container_of(rdma_entry, struct irdma_user_mmap_entry,
			    rdma_entry);
}

static inline struct irdma_pd *to_iwpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct irdma_pd, ibpd);
}

static inline struct irdma_ah *to_iwah(struct ib_ah *ibah)
{
	return container_of(ibah, struct irdma_ah, ibah);
}

static inline struct irdma_mr *to_iwmr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct irdma_mr, ibmr);
}

static inline struct irdma_mr *to_iwmw(struct ib_mw *ibmw)
{
	return container_of(ibmw, struct irdma_mr, ibmw);
}

static inline struct irdma_cq *to_iwcq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct irdma_cq, ibcq);
}

static inline struct irdma_qp *to_iwqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct irdma_qp, ibqp);
}

static inline struct irdma_pci_f *dev_to_rf(struct irdma_sc_dev *dev)
{
	return container_of(dev, struct irdma_pci_f, sc_dev);
}

/**
 * irdma_alloc_resource - allocate a resource
 * @iwdev: device pointer
 * @resource_array: resource bit array:
 * @max_resources: maximum resource number
 * @req_resources_num: Allocated resource number
 * @next: next free id
 **/
static inline int irdma_alloc_rsrc(struct irdma_pci_f *rf,
				   unsigned long *rsrc_array, u32 max_rsrc,
				   u32 *req_rsrc_num, u32 *next)
{
	u32 rsrc_num;
	unsigned long flags;

	spin_lock_irqsave(&rf->rsrc_lock, flags);
	rsrc_num = find_next_zero_bit(rsrc_array, max_rsrc, *next);
	if (rsrc_num >= max_rsrc) {
		rsrc_num = find_first_zero_bit(rsrc_array, max_rsrc);
		if (rsrc_num >= max_rsrc) {
			spin_unlock_irqrestore(&rf->rsrc_lock, flags);
			ibdev_dbg(&rf->iwdev->ibdev,
				  "ERR: resource [%d] allocation failed\n",
				  rsrc_num);
			return -EOVERFLOW;
		}
	}
	__set_bit(rsrc_num, rsrc_array);
	*next = rsrc_num + 1;
	if (*next == max_rsrc)
		*next = 0;
	*req_rsrc_num = rsrc_num;
	spin_unlock_irqrestore(&rf->rsrc_lock, flags);

	return 0;
}

/**
 * irdma_free_resource - free a resource
 * @iwdev: device pointer
 * @resource_array: resource array for the resource_num
 * @resource_num: resource number to free
 **/
static inline void irdma_free_rsrc(struct irdma_pci_f *rf,
				   unsigned long *rsrc_array, u32 rsrc_num)
{
	unsigned long flags;

	spin_lock_irqsave(&rf->rsrc_lock, flags);
	__clear_bit(rsrc_num, rsrc_array);
	spin_unlock_irqrestore(&rf->rsrc_lock, flags);
}

int irdma_ctrl_init_hw(struct irdma_pci_f *rf);
void irdma_ctrl_deinit_hw(struct irdma_pci_f *rf);
int irdma_rt_init_hw(struct irdma_device *iwdev,
		     struct irdma_l2params *l2params);
void irdma_rt_deinit_hw(struct irdma_device *iwdev);
void irdma_qp_add_ref(struct ib_qp *ibqp);
void irdma_qp_rem_ref(struct ib_qp *ibqp);
void irdma_free_lsmm_rsrc(struct irdma_qp *iwqp);
struct ib_qp *irdma_get_qp(struct ib_device *ibdev, int qpn);
void irdma_flush_wqes(struct irdma_qp *iwqp, u32 flush_mask);
void irdma_manage_arp_cache(struct irdma_pci_f *rf,
			    const unsigned char *mac_addr,
			    u32 *ip_addr, bool ipv4, u32 action);
struct irdma_apbvt_entry *irdma_add_apbvt(struct irdma_device *iwdev, u16 port);
void irdma_del_apbvt(struct irdma_device *iwdev,
		     struct irdma_apbvt_entry *entry);
struct irdma_cqp_request *irdma_alloc_and_get_cqp_request(struct irdma_cqp *cqp,
							  bool wait);
void irdma_free_cqp_request(struct irdma_cqp *cqp,
			    struct irdma_cqp_request *cqp_request);
void irdma_put_cqp_request(struct irdma_cqp *cqp,
			   struct irdma_cqp_request *cqp_request);
int irdma_alloc_local_mac_entry(struct irdma_pci_f *rf, u16 *mac_tbl_idx);
int irdma_add_local_mac_entry(struct irdma_pci_f *rf, const u8 *mac_addr, u16 idx);
void irdma_del_local_mac_entry(struct irdma_pci_f *rf, u16 idx);

u32 irdma_initialize_hw_rsrc(struct irdma_pci_f *rf);
void irdma_port_ibevent(struct irdma_device *iwdev);
void irdma_cm_disconn(struct irdma_qp *qp);

bool irdma_cqp_crit_err(struct irdma_sc_dev *dev, u8 cqp_cmd,
			u16 maj_err_code, u16 min_err_code);
int irdma_handle_cqp_op(struct irdma_pci_f *rf,
			struct irdma_cqp_request *cqp_request);

int irdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int attr_mask,
		    struct ib_udata *udata);
int irdma_modify_qp_roce(struct ib_qp *ibqp, struct ib_qp_attr *attr,
			 int attr_mask, struct ib_udata *udata);
void irdma_cq_wq_destroy(struct irdma_pci_f *rf, struct irdma_sc_cq *cq);

void irdma_cleanup_pending_cqp_op(struct irdma_pci_f *rf);
int irdma_hw_modify_qp(struct irdma_device *iwdev, struct irdma_qp *iwqp,
		       struct irdma_modify_qp_info *info, bool wait);
int irdma_qp_suspend_resume(struct irdma_sc_qp *qp, bool suspend);
int irdma_manage_qhash(struct irdma_device *iwdev, struct irdma_cm_info *cminfo,
		       enum irdma_quad_entry_type etype,
		       enum irdma_quad_hash_manage_type mtype, void *cmnode,
		       bool wait);
void irdma_receive_ilq(struct irdma_sc_vsi *vsi, struct irdma_puda_buf *rbuf);
void irdma_free_sqbuf(struct irdma_sc_vsi *vsi, void *bufp);
void irdma_free_qp_rsrc(struct irdma_qp *iwqp);
int irdma_setup_cm_core(struct irdma_device *iwdev, u8 ver);
void irdma_cleanup_cm_core(struct irdma_cm_core *cm_core);
void irdma_next_iw_state(struct irdma_qp *iwqp, u8 state, u8 del_hash, u8 term,
			 u8 term_len);
int irdma_send_syn(struct irdma_cm_node *cm_node, u32 sendack);
int irdma_send_reset(struct irdma_cm_node *cm_node);
struct irdma_cm_node *irdma_find_node(struct irdma_cm_core *cm_core,
				      u16 rem_port, u32 *rem_addr, u16 loc_port,
				      u32 *loc_addr, u16 vlan_id);
int irdma_hw_flush_wqes(struct irdma_pci_f *rf, struct irdma_sc_qp *qp,
			struct irdma_qp_flush_info *info, bool wait);
void irdma_gen_ae(struct irdma_pci_f *rf, struct irdma_sc_qp *qp,
		  struct irdma_gen_ae_info *info, bool wait);
void irdma_copy_ip_ntohl(u32 *dst, __be32 *src);
void irdma_copy_ip_htonl(__be32 *dst, u32 *src);
u16 irdma_get_vlan_ipv4(u32 *addr);
struct net_device *irdma_netdev_vlan_ipv6(u32 *addr, u16 *vlan_id, u8 *mac);
struct ib_mr *irdma_reg_phys_mr(struct ib_pd *ib_pd, u64 addr, u64 size,
				int acc, u64 *iova_start);
int irdma_upload_qp_context(struct irdma_qp *iwqp, bool freeze, bool raw);
void irdma_cqp_ce_handler(struct irdma_pci_f *rf, struct irdma_sc_cq *cq);
int irdma_ah_cqp_op(struct irdma_pci_f *rf, struct irdma_sc_ah *sc_ah, u8 cmd,
		    bool wait,
		    void (*callback_fcn)(struct irdma_cqp_request *cqp_request),
		    void *cb_param);
void irdma_gsi_ud_qp_ah_cb(struct irdma_cqp_request *cqp_request);
bool irdma_cq_empty(struct irdma_cq *iwcq);
int irdma_inetaddr_event(struct notifier_block *notifier, unsigned long event,
			 void *ptr);
int irdma_inet6addr_event(struct notifier_block *notifier, unsigned long event,
			  void *ptr);
int irdma_net_event(struct notifier_block *notifier, unsigned long event,
		    void *ptr);
int irdma_netdevice_event(struct notifier_block *notifier, unsigned long event,
			  void *ptr);
void irdma_add_ip(struct irdma_device *iwdev);
void cqp_compl_worker(struct work_struct *work);
#endif /* IRDMA_MAIN_H */

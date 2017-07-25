/*******************************************************************************
*
* Copyright (c) 2015-2016 Intel Corporation.  All rights reserved.
*
* This software is available to you under a choice of one of two
* licenses.  You may choose to be licensed under the terms of the GNU
* General Public License (GPL) Version 2, available from the file
* COPYING in the main directory of this source tree, or the
* OpenFabrics.org BSD license below:
*
*   Redistribution and use in source and binary forms, with or
*   without modification, are permitted provided that the following
*   conditions are met:
*
*    - Redistributions of source code must retain the above
*	copyright notice, this list of conditions and the following
*	disclaimer.
*
*    - Redistributions in binary form must reproduce the above
*	copyright notice, this list of conditions and the following
*	disclaimer in the documentation and/or other materials
*	provided with the distribution.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
* BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
*******************************************************************************/

#ifndef I40IW_IW_H
#define I40IW_IW_H
#include <linux/netdevice.h>
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
#include <rdma/ib_smi.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_pack.h>
#include <rdma/rdma_cm.h>
#include <rdma/iw_cm.h>
#include <crypto/hash.h>

#include "i40iw_status.h"
#include "i40iw_osdep.h"
#include "i40iw_d.h"
#include "i40iw_hmc.h"

#include <i40e_client.h>
#include "i40iw_type.h"
#include "i40iw_p.h"
#include "i40iw_ucontext.h"
#include "i40iw_pble.h"
#include "i40iw_verbs.h"
#include "i40iw_cm.h"
#include "i40iw_user.h"
#include "i40iw_puda.h"

#define I40IW_FW_VERSION  2
#define I40IW_HW_VERSION  2

#define I40IW_ARP_ADD     1
#define I40IW_ARP_DELETE  2
#define I40IW_ARP_RESOLVE 3

#define I40IW_MACIP_ADD     1
#define I40IW_MACIP_DELETE  2

#define IW_CCQ_SIZE         (I40IW_CQP_SW_SQSIZE_2048 + 1)
#define IW_CEQ_SIZE         2048
#define IW_AEQ_SIZE         2048

#define RX_BUF_SIZE            (1536 + 8)
#define IW_REG0_SIZE           (4 * 1024)
#define IW_TX_TIMEOUT          (6 * HZ)
#define IW_FIRST_QPN           1
#define IW_SW_CONTEXT_ALIGN    1024

#define MAX_DPC_ITERATIONS		128

#define I40IW_EVENT_TIMEOUT		100000
#define I40IW_VCHNL_EVENT_TIMEOUT	100000

#define	I40IW_NO_VLAN			0xffff
#define	I40IW_NO_QSET			0xffff

/* access to mcast filter list */
#define IW_ADD_MCAST false
#define IW_DEL_MCAST true

#define I40IW_DRV_OPT_ENABLE_MPA_VER_0     0x00000001
#define I40IW_DRV_OPT_DISABLE_MPA_CRC      0x00000002
#define I40IW_DRV_OPT_DISABLE_FIRST_WRITE  0x00000004
#define I40IW_DRV_OPT_DISABLE_INTF         0x00000008
#define I40IW_DRV_OPT_ENABLE_MSI           0x00000010
#define I40IW_DRV_OPT_DUAL_LOGICAL_PORT    0x00000020
#define I40IW_DRV_OPT_NO_INLINE_DATA       0x00000080
#define I40IW_DRV_OPT_DISABLE_INT_MOD      0x00000100
#define I40IW_DRV_OPT_DISABLE_VIRT_WQ      0x00000200
#define I40IW_DRV_OPT_ENABLE_PAU           0x00000400
#define I40IW_DRV_OPT_MCAST_LOGPORT_MAP    0x00000800

#define IW_HMC_OBJ_TYPE_NUM ARRAY_SIZE(iw_hmc_obj_types)
#define IW_CFG_FPM_QP_COUNT               32768
#define I40IW_MAX_PAGES_PER_FMR           512
#define I40IW_MIN_PAGES_PER_FMR           1
#define I40IW_CQP_COMPL_RQ_WQE_FLUSHED    2
#define I40IW_CQP_COMPL_SQ_WQE_FLUSHED    3
#define I40IW_CQP_COMPL_RQ_SQ_WQE_FLUSHED 4

#define I40IW_MTU_TO_MSS		40
#define I40IW_DEFAULT_MSS		1460

struct i40iw_cqp_compl_info {
	u32 op_ret_val;
	u16 maj_err_code;
	u16 min_err_code;
	bool error;
	u8 op_code;
};

#define i40iw_pr_err(fmt, args ...) pr_err("%s: "fmt, __func__, ## args)

#define i40iw_pr_info(fmt, args ...) pr_info("%s: " fmt, __func__, ## args)

#define i40iw_pr_warn(fmt, args ...) pr_warn("%s: " fmt, __func__, ## args)

struct i40iw_cqp_request {
	struct cqp_commands_info info;
	wait_queue_head_t waitq;
	struct list_head list;
	atomic_t refcount;
	void (*callback_fcn)(struct i40iw_cqp_request*, u32);
	void *param;
	struct i40iw_cqp_compl_info compl_info;
	bool waiting;
	bool request_done;
	bool dynamic;
};

struct i40iw_cqp {
	struct i40iw_sc_cqp sc_cqp;
	spinlock_t req_lock; /*cqp request list */
	wait_queue_head_t waitq;
	struct i40iw_dma_mem sq;
	struct i40iw_dma_mem host_ctx;
	u64 *scratch_array;
	struct i40iw_cqp_request *cqp_requests;
	struct list_head cqp_avail_reqs;
	struct list_head cqp_pending_reqs;
};

struct i40iw_device;

struct i40iw_ccq {
	struct i40iw_sc_cq sc_cq;
	spinlock_t lock; /* ccq control */
	wait_queue_head_t waitq;
	struct i40iw_dma_mem mem_cq;
	struct i40iw_dma_mem shadow_area;
};

struct i40iw_ceq {
	struct i40iw_sc_ceq sc_ceq;
	struct i40iw_dma_mem mem;
	u32 irq;
	u32 msix_idx;
	struct i40iw_device *iwdev;
	struct tasklet_struct dpc_tasklet;
};

struct i40iw_aeq {
	struct i40iw_sc_aeq sc_aeq;
	struct i40iw_dma_mem mem;
};

struct i40iw_arp_entry {
	u32 ip_addr[4];
	u8 mac_addr[ETH_ALEN];
};

enum init_completion_state {
	INVALID_STATE = 0,
	INITIAL_STATE,
	CQP_CREATED,
	HMC_OBJS_CREATED,
	PBLE_CHUNK_MEM,
	CCQ_CREATED,
	AEQ_CREATED,
	CEQ_CREATED,
	ILQ_CREATED,
	IEQ_CREATED,
	INET_NOTIFIER,
	IP_ADDR_REGISTERED,
	RDMA_DEV_REGISTERED
};

struct i40iw_msix_vector {
	u32 idx;
	u32 irq;
	u32 cpu_affinity;
	u32 ceq_id;
};

struct l2params_work {
	struct work_struct work;
	struct i40iw_device *iwdev;
	struct i40iw_l2params l2params;
};

#define I40IW_MSIX_TABLE_SIZE   65

struct virtchnl_work {
	struct work_struct work;
	union {
		struct i40iw_cqp_request *cqp_request;
		struct i40iw_virtchnl_work_info work_info;
	};
};

struct i40e_qvlist_info;

struct i40iw_device {
	struct i40iw_ib_device *iwibdev;
	struct net_device *netdev;
	wait_queue_head_t vchnl_waitq;
	struct i40iw_sc_dev sc_dev;
	struct i40iw_sc_vsi vsi;
	struct i40iw_handler *hdl;
	struct i40e_info *ldev;
	struct i40e_client *client;
	struct i40iw_hw hw;
	struct i40iw_cm_core cm_core;
	u8 *mem_resources;
	unsigned long *allocated_qps;
	unsigned long *allocated_cqs;
	unsigned long *allocated_mrs;
	unsigned long *allocated_pds;
	unsigned long *allocated_arps;
	struct i40iw_qp **qp_table;
	bool msix_shared;
	u32 msix_count;
	struct i40iw_msix_vector *iw_msixtbl;
	struct i40e_qvlist_info *iw_qvlist;

	struct i40iw_hmc_pble_rsrc *pble_rsrc;
	struct i40iw_arp_entry *arp_table;
	struct i40iw_cqp cqp;
	struct i40iw_ccq ccq;
	u32 ceqs_count;
	struct i40iw_ceq *ceqlist;
	struct i40iw_aeq aeq;
	u32 arp_table_size;
	u32 next_arp_index;
	spinlock_t resource_lock; /* hw resource access */
	spinlock_t qptable_lock;
	u32 vendor_id;
	u32 vendor_part_id;
	u32 of_device_registered;

	u32 device_cap_flags;
	unsigned long db_start;
	u8 resource_profile;
	u8 max_rdma_vfs;
	u8 max_enabled_vfs;
	u8 max_sge;
	u8 iw_status;
	u8 send_term_ok;
	bool push_mode;		/* Initialized from parameter passed to driver */

	/* x710 specific */
	struct mutex pbl_mutex;
	struct tasklet_struct dpc_tasklet;
	struct workqueue_struct *virtchnl_wq;
	struct virtchnl_work virtchnl_w[I40IW_MAX_PE_ENABLED_VF_COUNT];
	struct i40iw_dma_mem obj_mem;
	struct i40iw_dma_mem obj_next;
	u8 *hmc_info_mem;
	u32 sd_type;
	struct workqueue_struct *param_wq;
	atomic_t params_busy;
	enum init_completion_state init_state;
	u16 mac_ip_table_idx;
	atomic_t vchnl_msgs;
	u32 max_mr;
	u32 max_qp;
	u32 max_cq;
	u32 max_pd;
	u32 next_qp;
	u32 next_cq;
	u32 next_pd;
	u32 max_mr_size;
	u32 max_qp_wr;
	u32 max_cqe;
	u32 mr_stagmask;
	u32 mpa_version;
	bool dcb;
	bool closing;
	bool reset;
	u32 used_pds;
	u32 used_cqs;
	u32 used_mrs;
	u32 used_qps;
	wait_queue_head_t close_wq;
	atomic64_t use_count;
};

struct i40iw_ib_device {
	struct ib_device ibdev;
	struct i40iw_device *iwdev;
};

struct i40iw_handler {
	struct list_head list;
	struct i40e_client *client;
	struct i40iw_device device;
	struct i40e_info ldev;
};

/**
 * to_iwdev - get device
 * @ibdev: ib device
 **/
static inline struct i40iw_device *to_iwdev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct i40iw_ib_device, ibdev)->iwdev;
}

/**
 * to_ucontext - get user context
 * @ibucontext: ib user context
 **/
static inline struct i40iw_ucontext *to_ucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct i40iw_ucontext, ibucontext);
}

/**
 * to_iwpd - get protection domain
 * @ibpd: ib pd
 **/
static inline struct i40iw_pd *to_iwpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct i40iw_pd, ibpd);
}

/**
 * to_iwmr - get device memory region
 * @ibdev: ib memory region
 **/
static inline struct i40iw_mr *to_iwmr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct i40iw_mr, ibmr);
}

/**
 * to_iwmr_from_ibfmr - get device memory region
 * @ibfmr: ib fmr
 **/
static inline struct i40iw_mr *to_iwmr_from_ibfmr(struct ib_fmr *ibfmr)
{
	return container_of(ibfmr, struct i40iw_mr, ibfmr);
}

/**
 * to_iwmw - get device memory window
 * @ibmw: ib memory window
 **/
static inline struct i40iw_mr *to_iwmw(struct ib_mw *ibmw)
{
	return container_of(ibmw, struct i40iw_mr, ibmw);
}

/**
 * to_iwcq - get completion queue
 * @ibcq: ib cqdevice
 **/
static inline struct i40iw_cq *to_iwcq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct i40iw_cq, ibcq);
}

/**
 * to_iwqp - get device qp
 * @ibqp: ib qp
 **/
static inline struct i40iw_qp *to_iwqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct i40iw_qp, ibqp);
}

/* i40iw.c */
void i40iw_add_ref(struct ib_qp *);
void i40iw_rem_ref(struct ib_qp *);
struct ib_qp *i40iw_get_qp(struct ib_device *, int);

void i40iw_flush_wqes(struct i40iw_device *iwdev,
		      struct i40iw_qp *qp);

void i40iw_manage_arp_cache(struct i40iw_device *iwdev,
			    unsigned char *mac_addr,
			    u32 *ip_addr,
			    bool ipv4,
			    u32 action);

int i40iw_manage_apbvt(struct i40iw_device *iwdev,
		       u16 accel_local_port,
		       bool add_port);

struct i40iw_cqp_request *i40iw_get_cqp_request(struct i40iw_cqp *cqp, bool wait);
void i40iw_free_cqp_request(struct i40iw_cqp *cqp, struct i40iw_cqp_request *cqp_request);
void i40iw_put_cqp_request(struct i40iw_cqp *cqp, struct i40iw_cqp_request *cqp_request);

/**
 * i40iw_alloc_resource - allocate a resource
 * @iwdev: device pointer
 * @resource_array: resource bit array:
 * @max_resources: maximum resource number
 * @req_resources_num: Allocated resource number
 * @next: next free id
 **/
static inline int i40iw_alloc_resource(struct i40iw_device *iwdev,
				       unsigned long *resource_array,
				       u32 max_resources,
				       u32 *req_resource_num,
				       u32 *next)
{
	u32 resource_num;
	unsigned long flags;

	spin_lock_irqsave(&iwdev->resource_lock, flags);
	resource_num = find_next_zero_bit(resource_array, max_resources, *next);
	if (resource_num >= max_resources) {
		resource_num = find_first_zero_bit(resource_array, max_resources);
		if (resource_num >= max_resources) {
			spin_unlock_irqrestore(&iwdev->resource_lock, flags);
			return -EOVERFLOW;
		}
	}
	set_bit(resource_num, resource_array);
	*next = resource_num + 1;
	if (*next == max_resources)
		*next = 0;
	*req_resource_num = resource_num;
	spin_unlock_irqrestore(&iwdev->resource_lock, flags);

	return 0;
}

/**
 * i40iw_is_resource_allocated - detrmine if resource is
 * allocated
 * @iwdev: device pointer
 * @resource_array: resource array for the resource_num
 * @resource_num: resource number to check
 **/
static inline bool i40iw_is_resource_allocated(struct i40iw_device *iwdev,
					       unsigned long *resource_array,
					       u32 resource_num)
{
	bool bit_is_set;
	unsigned long flags;

	spin_lock_irqsave(&iwdev->resource_lock, flags);

	bit_is_set = test_bit(resource_num, resource_array);
	spin_unlock_irqrestore(&iwdev->resource_lock, flags);

	return bit_is_set;
}

/**
 * i40iw_free_resource - free a resource
 * @iwdev: device pointer
 * @resource_array: resource array for the resource_num
 * @resource_num: resource number to free
 **/
static inline void i40iw_free_resource(struct i40iw_device *iwdev,
				       unsigned long *resource_array,
				       u32 resource_num)
{
	unsigned long flags;

	spin_lock_irqsave(&iwdev->resource_lock, flags);
	clear_bit(resource_num, resource_array);
	spin_unlock_irqrestore(&iwdev->resource_lock, flags);
}

/**
 * to_iwhdl - Get the handler from the device pointer
 * @iwdev: device pointer
 **/
static inline struct i40iw_handler *to_iwhdl(struct i40iw_device *iw_dev)
{
	return container_of(iw_dev, struct i40iw_handler, device);
}

struct i40iw_handler *i40iw_find_netdev(struct net_device *netdev);

/**
 * iw_init_resources -
 */
u32 i40iw_initialize_hw_resources(struct i40iw_device *iwdev);

int i40iw_register_rdma_device(struct i40iw_device *iwdev);
void i40iw_port_ibevent(struct i40iw_device *iwdev);
void i40iw_cm_disconn(struct i40iw_qp *iwqp);
void i40iw_cm_disconn_worker(void *);
int mini_cm_recv_pkt(struct i40iw_cm_core *, struct i40iw_device *,
		     struct sk_buff *);

enum i40iw_status_code i40iw_handle_cqp_op(struct i40iw_device *iwdev,
					   struct i40iw_cqp_request *cqp_request);
enum i40iw_status_code i40iw_add_mac_addr(struct i40iw_device *iwdev,
					  u8 *mac_addr, u8 *mac_index);
int i40iw_modify_qp(struct ib_qp *, struct ib_qp_attr *, int, struct ib_udata *);
void i40iw_cq_wq_destroy(struct i40iw_device *iwdev, struct i40iw_sc_cq *cq);

void i40iw_cleanup_pending_cqp_op(struct i40iw_device *iwdev);
void i40iw_rem_pdusecount(struct i40iw_pd *iwpd, struct i40iw_device *iwdev);
void i40iw_add_pdusecount(struct i40iw_pd *iwpd);
void i40iw_rem_devusecount(struct i40iw_device *iwdev);
void i40iw_add_devusecount(struct i40iw_device *iwdev);
void i40iw_hw_modify_qp(struct i40iw_device *iwdev, struct i40iw_qp *iwqp,
			struct i40iw_modify_qp_info *info, bool wait);

void i40iw_qp_suspend_resume(struct i40iw_sc_dev *dev,
			     struct i40iw_sc_qp *qp,
			     bool suspend);
enum i40iw_status_code i40iw_manage_qhash(struct i40iw_device *iwdev,
					  struct i40iw_cm_info *cminfo,
					  enum i40iw_quad_entry_type etype,
					  enum i40iw_quad_hash_manage_type mtype,
					  void *cmnode,
					  bool wait);
void i40iw_receive_ilq(struct i40iw_sc_vsi *vsi, struct i40iw_puda_buf *rbuf);
void i40iw_free_sqbuf(struct i40iw_sc_vsi *vsi, void *bufp);
void i40iw_free_qp_resources(struct i40iw_device *iwdev,
			     struct i40iw_qp *iwqp,
			     u32 qp_num);
enum i40iw_status_code i40iw_obj_aligned_mem(struct i40iw_device *iwdev,
					     struct i40iw_dma_mem *memptr,
					     u32 size, u32 mask);

void i40iw_request_reset(struct i40iw_device *iwdev);
void i40iw_destroy_rdma_device(struct i40iw_ib_device *iwibdev);
void i40iw_setup_cm_core(struct i40iw_device *iwdev);
void i40iw_cleanup_cm_core(struct i40iw_cm_core *cm_core);
void i40iw_process_ceq(struct i40iw_device *, struct i40iw_ceq *iwceq);
void i40iw_process_aeq(struct i40iw_device *);
void i40iw_next_iw_state(struct i40iw_qp *iwqp,
			 u8 state, u8 del_hash,
			 u8 term, u8 term_len);
int i40iw_send_syn(struct i40iw_cm_node *cm_node, u32 sendack);
struct i40iw_cm_node *i40iw_find_node(struct i40iw_cm_core *cm_core,
				      u16 rem_port,
				      u32 *rem_addr,
				      u16 loc_port,
				      u32 *loc_addr,
				      bool add_refcnt);

enum i40iw_status_code i40iw_hw_flush_wqes(struct i40iw_device *iwdev,
					   struct i40iw_sc_qp *qp,
					   struct i40iw_qp_flush_info *info,
					   bool wait);

void i40iw_copy_ip_ntohl(u32 *dst, __be32 *src);
struct ib_mr *i40iw_reg_phys_mr(struct ib_pd *ib_pd,
				u64 addr,
				u64 size,
				int acc,
				u64 *iova_start);

int i40iw_inetaddr_event(struct notifier_block *notifier,
			 unsigned long event,
			 void *ptr);
int i40iw_inet6addr_event(struct notifier_block *notifier,
			  unsigned long event,
			  void *ptr);
int i40iw_net_event(struct notifier_block *notifier,
		    unsigned long event,
		    void *ptr);

#endif

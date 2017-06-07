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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/if_vlan.h>
#include <net/addrconf.h>

#include "i40iw.h"
#include "i40iw_register.h"
#include <net/netevent.h>
#define CLIENT_IW_INTERFACE_VERSION_MAJOR 0
#define CLIENT_IW_INTERFACE_VERSION_MINOR 01
#define CLIENT_IW_INTERFACE_VERSION_BUILD 00

#define DRV_VERSION_MAJOR 0
#define DRV_VERSION_MINOR 5
#define DRV_VERSION_BUILD 123
#define DRV_VERSION	__stringify(DRV_VERSION_MAJOR) "."		\
	__stringify(DRV_VERSION_MINOR) "." __stringify(DRV_VERSION_BUILD)

static int push_mode;
module_param(push_mode, int, 0644);
MODULE_PARM_DESC(push_mode, "Low latency mode: 0=disabled (default), 1=enabled)");

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug flags: 0=disabled (default), 0x7fffffff=all");

static int resource_profile;
module_param(resource_profile, int, 0644);
MODULE_PARM_DESC(resource_profile,
		 "Resource Profile: 0=no VF RDMA support (default), 1=Weighted VF, 2=Even Distribution");

static int max_rdma_vfs = 32;
module_param(max_rdma_vfs, int, 0644);
MODULE_PARM_DESC(max_rdma_vfs, "Maximum VF count: 0-32 32=default");
static int mpa_version = 2;
module_param(mpa_version, int, 0644);
MODULE_PARM_DESC(mpa_version, "MPA version to be used in MPA Req/Resp 1 or 2");

MODULE_AUTHOR("Intel Corporation, <e1000-rdma@lists.sourceforge.net>");
MODULE_DESCRIPTION("Intel(R) Ethernet Connection X722 iWARP RDMA Driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VERSION);

static struct i40e_client i40iw_client;
static char i40iw_client_name[I40E_CLIENT_STR_LENGTH] = "i40iw";

static LIST_HEAD(i40iw_handlers);
static spinlock_t i40iw_handler_lock;

static enum i40iw_status_code i40iw_virtchnl_send(struct i40iw_sc_dev *dev,
						  u32 vf_id, u8 *msg, u16 len);

static struct notifier_block i40iw_inetaddr_notifier = {
	.notifier_call = i40iw_inetaddr_event
};

static struct notifier_block i40iw_inetaddr6_notifier = {
	.notifier_call = i40iw_inet6addr_event
};

static struct notifier_block i40iw_net_notifier = {
	.notifier_call = i40iw_net_event
};

static atomic_t i40iw_notifiers_registered;

/**
 * i40iw_find_i40e_handler - find a handler given a client info
 * @ldev: pointer to a client info
 */
static struct i40iw_handler *i40iw_find_i40e_handler(struct i40e_info *ldev)
{
	struct i40iw_handler *hdl;
	unsigned long flags;

	spin_lock_irqsave(&i40iw_handler_lock, flags);
	list_for_each_entry(hdl, &i40iw_handlers, list) {
		if (hdl->ldev.netdev == ldev->netdev) {
			spin_unlock_irqrestore(&i40iw_handler_lock, flags);
			return hdl;
		}
	}
	spin_unlock_irqrestore(&i40iw_handler_lock, flags);
	return NULL;
}

/**
 * i40iw_find_netdev - find a handler given a netdev
 * @netdev: pointer to net_device
 */
struct i40iw_handler *i40iw_find_netdev(struct net_device *netdev)
{
	struct i40iw_handler *hdl;
	unsigned long flags;

	spin_lock_irqsave(&i40iw_handler_lock, flags);
	list_for_each_entry(hdl, &i40iw_handlers, list) {
		if (hdl->ldev.netdev == netdev) {
			spin_unlock_irqrestore(&i40iw_handler_lock, flags);
			return hdl;
		}
	}
	spin_unlock_irqrestore(&i40iw_handler_lock, flags);
	return NULL;
}

/**
 * i40iw_add_handler - add a handler to the list
 * @hdl: handler to be added to the handler list
 */
static void i40iw_add_handler(struct i40iw_handler *hdl)
{
	unsigned long flags;

	spin_lock_irqsave(&i40iw_handler_lock, flags);
	list_add(&hdl->list, &i40iw_handlers);
	spin_unlock_irqrestore(&i40iw_handler_lock, flags);
}

/**
 * i40iw_del_handler - delete a handler from the list
 * @hdl: handler to be deleted from the handler list
 */
static int i40iw_del_handler(struct i40iw_handler *hdl)
{
	unsigned long flags;

	spin_lock_irqsave(&i40iw_handler_lock, flags);
	list_del(&hdl->list);
	spin_unlock_irqrestore(&i40iw_handler_lock, flags);
	return 0;
}

/**
 * i40iw_enable_intr - set up device interrupts
 * @dev: hardware control device structure
 * @msix_id: id of the interrupt to be enabled
 */
static void i40iw_enable_intr(struct i40iw_sc_dev *dev, u32 msix_id)
{
	u32 val;

	val = I40E_PFINT_DYN_CTLN_INTENA_MASK |
		I40E_PFINT_DYN_CTLN_CLEARPBA_MASK |
		(3 << I40E_PFINT_DYN_CTLN_ITR_INDX_SHIFT);
	if (dev->is_pf)
		i40iw_wr32(dev->hw, I40E_PFINT_DYN_CTLN(msix_id - 1), val);
	else
		i40iw_wr32(dev->hw, I40E_VFINT_DYN_CTLN1(msix_id - 1), val);
}

/**
 * i40iw_dpc - tasklet for aeq and ceq 0
 * @data: iwarp device
 */
static void i40iw_dpc(unsigned long data)
{
	struct i40iw_device *iwdev = (struct i40iw_device *)data;

	if (iwdev->msix_shared)
		i40iw_process_ceq(iwdev, iwdev->ceqlist);
	i40iw_process_aeq(iwdev);
	i40iw_enable_intr(&iwdev->sc_dev, iwdev->iw_msixtbl[0].idx);
}

/**
 * i40iw_ceq_dpc - dpc handler for CEQ
 * @data: data points to CEQ
 */
static void i40iw_ceq_dpc(unsigned long data)
{
	struct i40iw_ceq *iwceq = (struct i40iw_ceq *)data;
	struct i40iw_device *iwdev = iwceq->iwdev;

	i40iw_process_ceq(iwdev, iwceq);
	i40iw_enable_intr(&iwdev->sc_dev, iwceq->msix_idx);
}

/**
 * i40iw_irq_handler - interrupt handler for aeq and ceq0
 * @irq: Interrupt request number
 * @data: iwarp device
 */
static irqreturn_t i40iw_irq_handler(int irq, void *data)
{
	struct i40iw_device *iwdev = (struct i40iw_device *)data;

	tasklet_schedule(&iwdev->dpc_tasklet);
	return IRQ_HANDLED;
}

/**
 * i40iw_destroy_cqp  - destroy control qp
 * @iwdev: iwarp device
 * @create_done: 1 if cqp create poll was success
 *
 * Issue destroy cqp request and
 * free the resources associated with the cqp
 */
static void i40iw_destroy_cqp(struct i40iw_device *iwdev, bool free_hwcqp)
{
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	struct i40iw_cqp *cqp = &iwdev->cqp;

	if (free_hwcqp)
		dev->cqp_ops->cqp_destroy(dev->cqp);

	i40iw_free_dma_mem(dev->hw, &cqp->sq);
	kfree(cqp->scratch_array);
	iwdev->cqp.scratch_array = NULL;

	kfree(cqp->cqp_requests);
	cqp->cqp_requests = NULL;
}

/**
 * i40iw_disable_irqs - disable device interrupts
 * @dev: hardware control device structure
 * @msic_vec: msix vector to disable irq
 * @dev_id: parameter to pass to free_irq (used during irq setup)
 *
 * The function is called when destroying aeq/ceq
 */
static void i40iw_disable_irq(struct i40iw_sc_dev *dev,
			      struct i40iw_msix_vector *msix_vec,
			      void *dev_id)
{
	if (dev->is_pf)
		i40iw_wr32(dev->hw, I40E_PFINT_DYN_CTLN(msix_vec->idx - 1), 0);
	else
		i40iw_wr32(dev->hw, I40E_VFINT_DYN_CTLN1(msix_vec->idx - 1), 0);
	irq_set_affinity_hint(msix_vec->irq, NULL);
	free_irq(msix_vec->irq, dev_id);
}

/**
 * i40iw_destroy_aeq - destroy aeq
 * @iwdev: iwarp device
 * @reset: true if called before reset
 *
 * Issue a destroy aeq request and
 * free the resources associated with the aeq
 * The function is called during driver unload
 */
static void i40iw_destroy_aeq(struct i40iw_device *iwdev, bool reset)
{
	enum i40iw_status_code status = I40IW_ERR_NOT_READY;
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	struct i40iw_aeq *aeq = &iwdev->aeq;

	if (!iwdev->msix_shared)
		i40iw_disable_irq(dev, iwdev->iw_msixtbl, (void *)iwdev);
	if (reset)
		goto exit;

	if (!dev->aeq_ops->aeq_destroy(&aeq->sc_aeq, 0, 1))
		status = dev->aeq_ops->aeq_destroy_done(&aeq->sc_aeq);
	if (status)
		i40iw_pr_err("destroy aeq failed %d\n", status);

exit:
	i40iw_free_dma_mem(dev->hw, &aeq->mem);
}

/**
 * i40iw_destroy_ceq - destroy ceq
 * @iwdev: iwarp device
 * @iwceq: ceq to be destroyed
 * @reset: true if called before reset
 *
 * Issue a destroy ceq request and
 * free the resources associated with the ceq
 */
static void i40iw_destroy_ceq(struct i40iw_device *iwdev,
			      struct i40iw_ceq *iwceq,
			      bool reset)
{
	enum i40iw_status_code status;
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;

	if (reset)
		goto exit;

	status = dev->ceq_ops->ceq_destroy(&iwceq->sc_ceq, 0, 1);
	if (status) {
		i40iw_pr_err("ceq destroy command failed %d\n", status);
		goto exit;
	}

	status = dev->ceq_ops->cceq_destroy_done(&iwceq->sc_ceq);
	if (status)
		i40iw_pr_err("ceq destroy completion failed %d\n", status);
exit:
	i40iw_free_dma_mem(dev->hw, &iwceq->mem);
}

/**
 * i40iw_dele_ceqs - destroy all ceq's
 * @iwdev: iwarp device
 * @reset: true if called before reset
 *
 * Go through all of the device ceq's and for each ceq
 * disable the ceq interrupt and destroy the ceq
 */
static void i40iw_dele_ceqs(struct i40iw_device *iwdev, bool reset)
{
	u32 i = 0;
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	struct i40iw_ceq *iwceq = iwdev->ceqlist;
	struct i40iw_msix_vector *msix_vec = iwdev->iw_msixtbl;

	if (iwdev->msix_shared) {
		i40iw_disable_irq(dev, msix_vec, (void *)iwdev);
		i40iw_destroy_ceq(iwdev, iwceq, reset);
		iwceq++;
		i++;
	}

	for (msix_vec++; i < iwdev->ceqs_count; i++, msix_vec++, iwceq++) {
		i40iw_disable_irq(dev, msix_vec, (void *)iwceq);
		i40iw_destroy_ceq(iwdev, iwceq, reset);
	}
}

/**
 * i40iw_destroy_ccq - destroy control cq
 * @iwdev: iwarp device
 * @reset: true if called before reset
 *
 * Issue destroy ccq request and
 * free the resources associated with the ccq
 */
static void i40iw_destroy_ccq(struct i40iw_device *iwdev, bool reset)
{
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	struct i40iw_ccq *ccq = &iwdev->ccq;
	enum i40iw_status_code status = 0;

	if (!reset)
		status = dev->ccq_ops->ccq_destroy(dev->ccq, 0, true);
	if (status)
		i40iw_pr_err("ccq destroy failed %d\n", status);
	i40iw_free_dma_mem(dev->hw, &ccq->mem_cq);
}

/* types of hmc objects */
static enum i40iw_hmc_rsrc_type iw_hmc_obj_types[] = {
	I40IW_HMC_IW_QP,
	I40IW_HMC_IW_CQ,
	I40IW_HMC_IW_HTE,
	I40IW_HMC_IW_ARP,
	I40IW_HMC_IW_APBVT_ENTRY,
	I40IW_HMC_IW_MR,
	I40IW_HMC_IW_XF,
	I40IW_HMC_IW_XFFL,
	I40IW_HMC_IW_Q1,
	I40IW_HMC_IW_Q1FL,
	I40IW_HMC_IW_TIMER,
};

/**
 * i40iw_close_hmc_objects_type - delete hmc objects of a given type
 * @iwdev: iwarp device
 * @obj_type: the hmc object type to be deleted
 * @is_pf: true if the function is PF otherwise false
 * @reset: true if called before reset
 */
static void i40iw_close_hmc_objects_type(struct i40iw_sc_dev *dev,
					 enum i40iw_hmc_rsrc_type obj_type,
					 struct i40iw_hmc_info *hmc_info,
					 bool is_pf,
					 bool reset)
{
	struct i40iw_hmc_del_obj_info info;

	memset(&info, 0, sizeof(info));
	info.hmc_info = hmc_info;
	info.rsrc_type = obj_type;
	info.count = hmc_info->hmc_obj[obj_type].cnt;
	info.is_pf = is_pf;
	if (dev->hmc_ops->del_hmc_object(dev, &info, reset))
		i40iw_pr_err("del obj of type %d failed\n", obj_type);
}

/**
 * i40iw_del_hmc_objects - remove all device hmc objects
 * @dev: iwarp device
 * @hmc_info: hmc_info to free
 * @is_pf: true if hmc_info belongs to PF, not vf nor allocated
 *	   by PF on behalf of VF
 * @reset: true if called before reset
 */
static void i40iw_del_hmc_objects(struct i40iw_sc_dev *dev,
				  struct i40iw_hmc_info *hmc_info,
				  bool is_pf,
				  bool reset)
{
	unsigned int i;

	for (i = 0; i < IW_HMC_OBJ_TYPE_NUM; i++)
		i40iw_close_hmc_objects_type(dev, iw_hmc_obj_types[i], hmc_info, is_pf, reset);
}

/**
 * i40iw_ceq_handler - interrupt handler for ceq
 * @data: ceq pointer
 */
static irqreturn_t i40iw_ceq_handler(int irq, void *data)
{
	struct i40iw_ceq *iwceq = (struct i40iw_ceq *)data;

	if (iwceq->irq != irq)
		i40iw_pr_err("expected irq = %d received irq = %d\n", iwceq->irq, irq);
	tasklet_schedule(&iwceq->dpc_tasklet);
	return IRQ_HANDLED;
}

/**
 * i40iw_create_hmc_obj_type - create hmc object of a given type
 * @dev: hardware control device structure
 * @info: information for the hmc object to create
 */
static enum i40iw_status_code i40iw_create_hmc_obj_type(struct i40iw_sc_dev *dev,
							struct i40iw_hmc_create_obj_info *info)
{
	return dev->hmc_ops->create_hmc_object(dev, info);
}

/**
 * i40iw_create_hmc_objs - create all hmc objects for the device
 * @iwdev: iwarp device
 * @is_pf: true if the function is PF otherwise false
 *
 * Create the device hmc objects and allocate hmc pages
 * Return 0 if successful, otherwise clean up and return error
 */
static enum i40iw_status_code i40iw_create_hmc_objs(struct i40iw_device *iwdev,
						    bool is_pf)
{
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	struct i40iw_hmc_create_obj_info info;
	enum i40iw_status_code status;
	int i;

	memset(&info, 0, sizeof(info));
	info.hmc_info = dev->hmc_info;
	info.is_pf = is_pf;
	info.entry_type = iwdev->sd_type;
	for (i = 0; i < IW_HMC_OBJ_TYPE_NUM; i++) {
		info.rsrc_type = iw_hmc_obj_types[i];
		info.count = dev->hmc_info->hmc_obj[info.rsrc_type].cnt;
		status = i40iw_create_hmc_obj_type(dev, &info);
		if (status) {
			i40iw_pr_err("create obj type %d status = %d\n",
				     iw_hmc_obj_types[i], status);
			break;
		}
	}
	if (!status)
		return (dev->cqp_misc_ops->static_hmc_pages_allocated(dev->cqp, 0,
								      dev->hmc_fn_id,
								      true, true));

	while (i) {
		i--;
		/* destroy the hmc objects of a given type */
		i40iw_close_hmc_objects_type(dev,
					     iw_hmc_obj_types[i],
					     dev->hmc_info,
					     is_pf,
					     false);
	}
	return status;
}

/**
 * i40iw_obj_aligned_mem - get aligned memory from device allocated memory
 * @iwdev: iwarp device
 * @memptr: points to the memory addresses
 * @size: size of memory needed
 * @mask: mask for the aligned memory
 *
 * Get aligned memory of the requested size and
 * update the memptr to point to the new aligned memory
 * Return 0 if successful, otherwise return no memory error
 */
enum i40iw_status_code i40iw_obj_aligned_mem(struct i40iw_device *iwdev,
					     struct i40iw_dma_mem *memptr,
					     u32 size,
					     u32 mask)
{
	unsigned long va, newva;
	unsigned long extra;

	va = (unsigned long)iwdev->obj_next.va;
	newva = va;
	if (mask)
		newva = ALIGN(va, (mask + 1));
	extra = newva - va;
	memptr->va = (u8 *)va + extra;
	memptr->pa = iwdev->obj_next.pa + extra;
	memptr->size = size;
	if ((memptr->va + size) > (iwdev->obj_mem.va + iwdev->obj_mem.size))
		return I40IW_ERR_NO_MEMORY;

	iwdev->obj_next.va = memptr->va + size;
	iwdev->obj_next.pa = memptr->pa + size;
	return 0;
}

/**
 * i40iw_create_cqp - create control qp
 * @iwdev: iwarp device
 *
 * Return 0, if the cqp and all the resources associated with it
 * are successfully created, otherwise return error
 */
static enum i40iw_status_code i40iw_create_cqp(struct i40iw_device *iwdev)
{
	enum i40iw_status_code status;
	u32 sqsize = I40IW_CQP_SW_SQSIZE_2048;
	struct i40iw_dma_mem mem;
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	struct i40iw_cqp_init_info cqp_init_info;
	struct i40iw_cqp *cqp = &iwdev->cqp;
	u16 maj_err, min_err;
	int i;

	cqp->cqp_requests = kcalloc(sqsize, sizeof(*cqp->cqp_requests), GFP_KERNEL);
	if (!cqp->cqp_requests)
		return I40IW_ERR_NO_MEMORY;
	cqp->scratch_array = kcalloc(sqsize, sizeof(*cqp->scratch_array), GFP_KERNEL);
	if (!cqp->scratch_array) {
		kfree(cqp->cqp_requests);
		return I40IW_ERR_NO_MEMORY;
	}
	dev->cqp = &cqp->sc_cqp;
	dev->cqp->dev = dev;
	memset(&cqp_init_info, 0, sizeof(cqp_init_info));
	status = i40iw_allocate_dma_mem(dev->hw, &cqp->sq,
					(sizeof(struct i40iw_cqp_sq_wqe) * sqsize),
					I40IW_CQP_ALIGNMENT);
	if (status)
		goto exit;
	status = i40iw_obj_aligned_mem(iwdev, &mem, sizeof(struct i40iw_cqp_ctx),
				       I40IW_HOST_CTX_ALIGNMENT_MASK);
	if (status)
		goto exit;
	dev->cqp->host_ctx_pa = mem.pa;
	dev->cqp->host_ctx = mem.va;
	/* populate the cqp init info */
	cqp_init_info.dev = dev;
	cqp_init_info.sq_size = sqsize;
	cqp_init_info.sq = cqp->sq.va;
	cqp_init_info.sq_pa = cqp->sq.pa;
	cqp_init_info.host_ctx_pa = mem.pa;
	cqp_init_info.host_ctx = mem.va;
	cqp_init_info.hmc_profile = iwdev->resource_profile;
	cqp_init_info.enabled_vf_count = iwdev->max_rdma_vfs;
	cqp_init_info.scratch_array = cqp->scratch_array;
	status = dev->cqp_ops->cqp_init(dev->cqp, &cqp_init_info);
	if (status) {
		i40iw_pr_err("cqp init status %d\n", status);
		goto exit;
	}
	status = dev->cqp_ops->cqp_create(dev->cqp, &maj_err, &min_err);
	if (status) {
		i40iw_pr_err("cqp create status %d maj_err %d min_err %d\n",
			     status, maj_err, min_err);
		goto exit;
	}
	spin_lock_init(&cqp->req_lock);
	INIT_LIST_HEAD(&cqp->cqp_avail_reqs);
	INIT_LIST_HEAD(&cqp->cqp_pending_reqs);
	/* init the waitq of the cqp_requests and add them to the list */
	for (i = 0; i < I40IW_CQP_SW_SQSIZE_2048; i++) {
		init_waitqueue_head(&cqp->cqp_requests[i].waitq);
		list_add_tail(&cqp->cqp_requests[i].list, &cqp->cqp_avail_reqs);
	}
	return 0;
exit:
	/* clean up the created resources */
	i40iw_destroy_cqp(iwdev, false);
	return status;
}

/**
 * i40iw_create_ccq - create control cq
 * @iwdev: iwarp device
 *
 * Return 0, if the ccq and the resources associated with it
 * are successfully created, otherwise return error
 */
static enum i40iw_status_code i40iw_create_ccq(struct i40iw_device *iwdev)
{
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	struct i40iw_dma_mem mem;
	enum i40iw_status_code status;
	struct i40iw_ccq_init_info info;
	struct i40iw_ccq *ccq = &iwdev->ccq;

	memset(&info, 0, sizeof(info));
	dev->ccq = &ccq->sc_cq;
	dev->ccq->dev = dev;
	info.dev = dev;
	ccq->shadow_area.size = sizeof(struct i40iw_cq_shadow_area);
	ccq->mem_cq.size = sizeof(struct i40iw_cqe) * IW_CCQ_SIZE;
	status = i40iw_allocate_dma_mem(dev->hw, &ccq->mem_cq,
					ccq->mem_cq.size, I40IW_CQ0_ALIGNMENT);
	if (status)
		goto exit;
	status = i40iw_obj_aligned_mem(iwdev, &mem, ccq->shadow_area.size,
				       I40IW_SHADOWAREA_MASK);
	if (status)
		goto exit;
	ccq->sc_cq.back_cq = (void *)ccq;
	/* populate the ccq init info */
	info.cq_base = ccq->mem_cq.va;
	info.cq_pa = ccq->mem_cq.pa;
	info.num_elem = IW_CCQ_SIZE;
	info.shadow_area = mem.va;
	info.shadow_area_pa = mem.pa;
	info.ceqe_mask = false;
	info.ceq_id_valid = true;
	info.shadow_read_threshold = 16;
	status = dev->ccq_ops->ccq_init(dev->ccq, &info);
	if (!status)
		status = dev->ccq_ops->ccq_create(dev->ccq, 0, true, true);
exit:
	if (status)
		i40iw_free_dma_mem(dev->hw, &ccq->mem_cq);
	return status;
}

/**
 * i40iw_configure_ceq_vector - set up the msix interrupt vector for ceq
 * @iwdev: iwarp device
 * @msix_vec: interrupt vector information
 * @iwceq: ceq associated with the vector
 * @ceq_id: the id number of the iwceq
 *
 * Allocate interrupt resources and enable irq handling
 * Return 0 if successful, otherwise return error
 */
static enum i40iw_status_code i40iw_configure_ceq_vector(struct i40iw_device *iwdev,
							 struct i40iw_ceq *iwceq,
							 u32 ceq_id,
							 struct i40iw_msix_vector *msix_vec)
{
	enum i40iw_status_code status;
	cpumask_t mask;

	if (iwdev->msix_shared && !ceq_id) {
		tasklet_init(&iwdev->dpc_tasklet, i40iw_dpc, (unsigned long)iwdev);
		status = request_irq(msix_vec->irq, i40iw_irq_handler, 0, "AEQCEQ", iwdev);
	} else {
		tasklet_init(&iwceq->dpc_tasklet, i40iw_ceq_dpc, (unsigned long)iwceq);
		status = request_irq(msix_vec->irq, i40iw_ceq_handler, 0, "CEQ", iwceq);
	}

	cpumask_clear(&mask);
	cpumask_set_cpu(msix_vec->cpu_affinity, &mask);
	irq_set_affinity_hint(msix_vec->irq, &mask);

	if (status) {
		i40iw_pr_err("ceq irq config fail\n");
		return I40IW_ERR_CONFIG;
	}
	msix_vec->ceq_id = ceq_id;

	return 0;
}

/**
 * i40iw_create_ceq - create completion event queue
 * @iwdev: iwarp device
 * @iwceq: pointer to the ceq resources to be created
 * @ceq_id: the id number of the iwceq
 *
 * Return 0, if the ceq and the resources associated with it
 * are successfully created, otherwise return error
 */
static enum i40iw_status_code i40iw_create_ceq(struct i40iw_device *iwdev,
					       struct i40iw_ceq *iwceq,
					       u32 ceq_id)
{
	enum i40iw_status_code status;
	struct i40iw_ceq_init_info info;
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	u64 scratch;

	memset(&info, 0, sizeof(info));
	info.ceq_id = ceq_id;
	iwceq->iwdev = iwdev;
	iwceq->mem.size = sizeof(struct i40iw_ceqe) *
		iwdev->sc_dev.hmc_info->hmc_obj[I40IW_HMC_IW_CQ].cnt;
	status = i40iw_allocate_dma_mem(dev->hw, &iwceq->mem, iwceq->mem.size,
					I40IW_CEQ_ALIGNMENT);
	if (status)
		goto exit;
	info.ceq_id = ceq_id;
	info.ceqe_base = iwceq->mem.va;
	info.ceqe_pa = iwceq->mem.pa;

	info.elem_cnt = iwdev->sc_dev.hmc_info->hmc_obj[I40IW_HMC_IW_CQ].cnt;
	iwceq->sc_ceq.ceq_id = ceq_id;
	info.dev = dev;
	scratch = (uintptr_t)&iwdev->cqp.sc_cqp;
	status = dev->ceq_ops->ceq_init(&iwceq->sc_ceq, &info);
	if (!status)
		status = dev->ceq_ops->cceq_create(&iwceq->sc_ceq, scratch);

exit:
	if (status)
		i40iw_free_dma_mem(dev->hw, &iwceq->mem);
	return status;
}

void i40iw_request_reset(struct i40iw_device *iwdev)
{
	struct i40e_info *ldev = iwdev->ldev;

	ldev->ops->request_reset(ldev, iwdev->client, 1);
}

/**
 * i40iw_setup_ceqs - manage the device ceq's and their interrupt resources
 * @iwdev: iwarp device
 * @ldev: i40e lan device
 *
 * Allocate a list for all device completion event queues
 * Create the ceq's and configure their msix interrupt vectors
 * Return 0, if at least one ceq is successfully set up, otherwise return error
 */
static enum i40iw_status_code i40iw_setup_ceqs(struct i40iw_device *iwdev,
					       struct i40e_info *ldev)
{
	u32 i;
	u32 ceq_id;
	struct i40iw_ceq *iwceq;
	struct i40iw_msix_vector *msix_vec;
	enum i40iw_status_code status = 0;
	u32 num_ceqs;

	if (ldev && ldev->ops && ldev->ops->setup_qvlist) {
		status = ldev->ops->setup_qvlist(ldev, &i40iw_client,
						 iwdev->iw_qvlist);
		if (status)
			goto exit;
	} else {
		status = I40IW_ERR_BAD_PTR;
		goto exit;
	}

	num_ceqs = min(iwdev->msix_count, iwdev->sc_dev.hmc_fpm_misc.max_ceqs);
	iwdev->ceqlist = kcalloc(num_ceqs, sizeof(*iwdev->ceqlist), GFP_KERNEL);
	if (!iwdev->ceqlist) {
		status = I40IW_ERR_NO_MEMORY;
		goto exit;
	}
	i = (iwdev->msix_shared) ? 0 : 1;
	for (ceq_id = 0; i < num_ceqs; i++, ceq_id++) {
		iwceq = &iwdev->ceqlist[ceq_id];
		status = i40iw_create_ceq(iwdev, iwceq, ceq_id);
		if (status) {
			i40iw_pr_err("create ceq status = %d\n", status);
			break;
		}

		msix_vec = &iwdev->iw_msixtbl[i];
		iwceq->irq = msix_vec->irq;
		iwceq->msix_idx = msix_vec->idx;
		status = i40iw_configure_ceq_vector(iwdev, iwceq, ceq_id, msix_vec);
		if (status) {
			i40iw_destroy_ceq(iwdev, iwceq, false);
			break;
		}
		i40iw_enable_intr(&iwdev->sc_dev, msix_vec->idx);
		iwdev->ceqs_count++;
	}

exit:
	if (status) {
		if (!iwdev->ceqs_count) {
			kfree(iwdev->ceqlist);
			iwdev->ceqlist = NULL;
		} else {
			status = 0;
		}
	}
	return status;
}

/**
 * i40iw_configure_aeq_vector - set up the msix vector for aeq
 * @iwdev: iwarp device
 *
 * Allocate interrupt resources and enable irq handling
 * Return 0 if successful, otherwise return error
 */
static enum i40iw_status_code i40iw_configure_aeq_vector(struct i40iw_device *iwdev)
{
	struct i40iw_msix_vector *msix_vec = iwdev->iw_msixtbl;
	u32 ret = 0;

	if (!iwdev->msix_shared) {
		tasklet_init(&iwdev->dpc_tasklet, i40iw_dpc, (unsigned long)iwdev);
		ret = request_irq(msix_vec->irq, i40iw_irq_handler, 0, "i40iw", iwdev);
	}
	if (ret) {
		i40iw_pr_err("aeq irq config fail\n");
		return I40IW_ERR_CONFIG;
	}

	return 0;
}

/**
 * i40iw_create_aeq - create async event queue
 * @iwdev: iwarp device
 *
 * Return 0, if the aeq and the resources associated with it
 * are successfully created, otherwise return error
 */
static enum i40iw_status_code i40iw_create_aeq(struct i40iw_device *iwdev)
{
	enum i40iw_status_code status;
	struct i40iw_aeq_init_info info;
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	struct i40iw_aeq *aeq = &iwdev->aeq;
	u64 scratch = 0;
	u32 aeq_size;

	aeq_size = 2 * iwdev->sc_dev.hmc_info->hmc_obj[I40IW_HMC_IW_QP].cnt +
		iwdev->sc_dev.hmc_info->hmc_obj[I40IW_HMC_IW_CQ].cnt;
	memset(&info, 0, sizeof(info));
	aeq->mem.size = sizeof(struct i40iw_sc_aeqe) * aeq_size;
	status = i40iw_allocate_dma_mem(dev->hw, &aeq->mem, aeq->mem.size,
					I40IW_AEQ_ALIGNMENT);
	if (status)
		goto exit;

	info.aeqe_base = aeq->mem.va;
	info.aeq_elem_pa = aeq->mem.pa;
	info.elem_cnt = aeq_size;
	info.dev = dev;
	status = dev->aeq_ops->aeq_init(&aeq->sc_aeq, &info);
	if (status)
		goto exit;
	status = dev->aeq_ops->aeq_create(&aeq->sc_aeq, scratch, 1);
	if (!status)
		status = dev->aeq_ops->aeq_create_done(&aeq->sc_aeq);
exit:
	if (status)
		i40iw_free_dma_mem(dev->hw, &aeq->mem);
	return status;
}

/**
 * i40iw_setup_aeq - set up the device aeq
 * @iwdev: iwarp device
 *
 * Create the aeq and configure its msix interrupt vector
 * Return 0 if successful, otherwise return error
 */
static enum i40iw_status_code i40iw_setup_aeq(struct i40iw_device *iwdev)
{
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	enum i40iw_status_code status;

	status = i40iw_create_aeq(iwdev);
	if (status)
		return status;

	status = i40iw_configure_aeq_vector(iwdev);
	if (status) {
		i40iw_destroy_aeq(iwdev, false);
		return status;
	}

	if (!iwdev->msix_shared)
		i40iw_enable_intr(dev, iwdev->iw_msixtbl[0].idx);
	return 0;
}

/**
 * i40iw_initialize_ilq - create iwarp local queue for cm
 * @iwdev: iwarp device
 *
 * Return 0 if successful, otherwise return error
 */
static enum i40iw_status_code i40iw_initialize_ilq(struct i40iw_device *iwdev)
{
	struct i40iw_puda_rsrc_info info;
	enum i40iw_status_code status;

	memset(&info, 0, sizeof(info));
	info.type = I40IW_PUDA_RSRC_TYPE_ILQ;
	info.cq_id = 1;
	info.qp_id = 0;
	info.count = 1;
	info.pd_id = 1;
	info.sq_size = 8192;
	info.rq_size = 8192;
	info.buf_size = 1024;
	info.tx_buf_cnt = 16384;
	info.receive = i40iw_receive_ilq;
	info.xmit_complete = i40iw_free_sqbuf;
	status = i40iw_puda_create_rsrc(&iwdev->vsi, &info);
	if (status)
		i40iw_pr_err("ilq create fail\n");
	return status;
}

/**
 * i40iw_initialize_ieq - create iwarp exception queue
 * @iwdev: iwarp device
 *
 * Return 0 if successful, otherwise return error
 */
static enum i40iw_status_code i40iw_initialize_ieq(struct i40iw_device *iwdev)
{
	struct i40iw_puda_rsrc_info info;
	enum i40iw_status_code status;

	memset(&info, 0, sizeof(info));
	info.type = I40IW_PUDA_RSRC_TYPE_IEQ;
	info.cq_id = 2;
	info.qp_id = iwdev->sc_dev.exception_lan_queue;
	info.count = 1;
	info.pd_id = 2;
	info.sq_size = 8192;
	info.rq_size = 8192;
	info.buf_size = 2048;
	info.tx_buf_cnt = 16384;
	status = i40iw_puda_create_rsrc(&iwdev->vsi, &info);
	if (status)
		i40iw_pr_err("ieq create fail\n");
	return status;
}

/**
 * i40iw_hmc_setup - create hmc objects for the device
 * @iwdev: iwarp device
 *
 * Set up the device private memory space for the number and size of
 * the hmc objects and create the objects
 * Return 0 if successful, otherwise return error
 */
static enum i40iw_status_code i40iw_hmc_setup(struct i40iw_device *iwdev)
{
	enum i40iw_status_code status;

	iwdev->sd_type = I40IW_SD_TYPE_DIRECT;
	status = i40iw_config_fpm_values(&iwdev->sc_dev, IW_CFG_FPM_QP_COUNT);
	if (status)
		goto exit;
	status = i40iw_create_hmc_objs(iwdev, true);
	if (status)
		goto exit;
	iwdev->init_state = HMC_OBJS_CREATED;
exit:
	return status;
}

/**
 * i40iw_del_init_mem - deallocate memory resources
 * @iwdev: iwarp device
 */
static void i40iw_del_init_mem(struct i40iw_device *iwdev)
{
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;

	i40iw_free_dma_mem(&iwdev->hw, &iwdev->obj_mem);
	kfree(dev->hmc_info->sd_table.sd_entry);
	dev->hmc_info->sd_table.sd_entry = NULL;
	kfree(iwdev->mem_resources);
	iwdev->mem_resources = NULL;
	kfree(iwdev->ceqlist);
	iwdev->ceqlist = NULL;
	kfree(iwdev->iw_msixtbl);
	iwdev->iw_msixtbl = NULL;
	kfree(iwdev->hmc_info_mem);
	iwdev->hmc_info_mem = NULL;
}

/**
 * i40iw_del_macip_entry - remove a mac ip address entry from the hw table
 * @iwdev: iwarp device
 * @idx: the index of the mac ip address to delete
 */
static void i40iw_del_macip_entry(struct i40iw_device *iwdev, u8 idx)
{
	struct i40iw_cqp *iwcqp = &iwdev->cqp;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;
	enum i40iw_status_code status = 0;

	cqp_request = i40iw_get_cqp_request(iwcqp, true);
	if (!cqp_request) {
		i40iw_pr_err("cqp_request memory failed\n");
		return;
	}
	cqp_info = &cqp_request->info;
	cqp_info->cqp_cmd = OP_DELETE_LOCAL_MAC_IPADDR_ENTRY;
	cqp_info->post_sq = 1;
	cqp_info->in.u.del_local_mac_ipaddr_entry.cqp = &iwcqp->sc_cqp;
	cqp_info->in.u.del_local_mac_ipaddr_entry.scratch = (uintptr_t)cqp_request;
	cqp_info->in.u.del_local_mac_ipaddr_entry.entry_idx = idx;
	cqp_info->in.u.del_local_mac_ipaddr_entry.ignore_ref_count = 0;
	status = i40iw_handle_cqp_op(iwdev, cqp_request);
	if (status)
		i40iw_pr_err("CQP-OP Del MAC Ip entry fail");
}

/**
 * i40iw_add_mac_ipaddr_entry - add a mac ip address entry to the hw table
 * @iwdev: iwarp device
 * @mac_addr: pointer to mac address
 * @idx: the index of the mac ip address to add
 */
static enum i40iw_status_code i40iw_add_mac_ipaddr_entry(struct i40iw_device *iwdev,
							 u8 *mac_addr,
							 u8 idx)
{
	struct i40iw_local_mac_ipaddr_entry_info *info;
	struct i40iw_cqp *iwcqp = &iwdev->cqp;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;
	enum i40iw_status_code status = 0;

	cqp_request = i40iw_get_cqp_request(iwcqp, true);
	if (!cqp_request) {
		i40iw_pr_err("cqp_request memory failed\n");
		return I40IW_ERR_NO_MEMORY;
	}

	cqp_info = &cqp_request->info;

	cqp_info->post_sq = 1;
	info = &cqp_info->in.u.add_local_mac_ipaddr_entry.info;
	ether_addr_copy(info->mac_addr, mac_addr);
	info->entry_idx = idx;
	cqp_info->in.u.add_local_mac_ipaddr_entry.scratch = (uintptr_t)cqp_request;
	cqp_info->cqp_cmd = OP_ADD_LOCAL_MAC_IPADDR_ENTRY;
	cqp_info->in.u.add_local_mac_ipaddr_entry.cqp = &iwcqp->sc_cqp;
	cqp_info->in.u.add_local_mac_ipaddr_entry.scratch = (uintptr_t)cqp_request;
	status = i40iw_handle_cqp_op(iwdev, cqp_request);
	if (status)
		i40iw_pr_err("CQP-OP Add MAC Ip entry fail");
	return status;
}

/**
 * i40iw_alloc_local_mac_ipaddr_entry - allocate a mac ip address entry
 * @iwdev: iwarp device
 * @mac_ip_tbl_idx: the index of the new mac ip address
 *
 * Allocate a mac ip address entry and update the mac_ip_tbl_idx
 * to hold the index of the newly created mac ip address
 * Return 0 if successful, otherwise return error
 */
static enum i40iw_status_code i40iw_alloc_local_mac_ipaddr_entry(struct i40iw_device *iwdev,
								 u16 *mac_ip_tbl_idx)
{
	struct i40iw_cqp *iwcqp = &iwdev->cqp;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;
	enum i40iw_status_code status = 0;

	cqp_request = i40iw_get_cqp_request(iwcqp, true);
	if (!cqp_request) {
		i40iw_pr_err("cqp_request memory failed\n");
		return I40IW_ERR_NO_MEMORY;
	}

	/* increment refcount, because we need the cqp request ret value */
	atomic_inc(&cqp_request->refcount);

	cqp_info = &cqp_request->info;
	cqp_info->cqp_cmd = OP_ALLOC_LOCAL_MAC_IPADDR_ENTRY;
	cqp_info->post_sq = 1;
	cqp_info->in.u.alloc_local_mac_ipaddr_entry.cqp = &iwcqp->sc_cqp;
	cqp_info->in.u.alloc_local_mac_ipaddr_entry.scratch = (uintptr_t)cqp_request;
	status = i40iw_handle_cqp_op(iwdev, cqp_request);
	if (!status)
		*mac_ip_tbl_idx = cqp_request->compl_info.op_ret_val;
	else
		i40iw_pr_err("CQP-OP Alloc MAC Ip entry fail");
	/* decrement refcount and free the cqp request, if no longer used */
	i40iw_put_cqp_request(iwcqp, cqp_request);
	return status;
}

/**
 * i40iw_alloc_set_mac_ipaddr - set up a mac ip address table entry
 * @iwdev: iwarp device
 * @macaddr: pointer to mac address
 *
 * Allocate a mac ip address entry and add it to the hw table
 * Return 0 if successful, otherwise return error
 */
static enum i40iw_status_code i40iw_alloc_set_mac_ipaddr(struct i40iw_device *iwdev,
							 u8 *macaddr)
{
	enum i40iw_status_code status;

	status = i40iw_alloc_local_mac_ipaddr_entry(iwdev, &iwdev->mac_ip_table_idx);
	if (!status) {
		status = i40iw_add_mac_ipaddr_entry(iwdev, macaddr,
						    (u8)iwdev->mac_ip_table_idx);
		if (status)
			i40iw_del_macip_entry(iwdev, (u8)iwdev->mac_ip_table_idx);
	}
	return status;
}

/**
 * i40iw_add_ipv6_addr - add ipv6 address to the hw arp table
 * @iwdev: iwarp device
 */
static void i40iw_add_ipv6_addr(struct i40iw_device *iwdev)
{
	struct net_device *ip_dev;
	struct inet6_dev *idev;
	struct inet6_ifaddr *ifp, *tmp;
	u32 local_ipaddr6[4];

	rcu_read_lock();
	for_each_netdev_rcu(&init_net, ip_dev) {
		if ((((rdma_vlan_dev_vlan_id(ip_dev) < 0xFFFF) &&
		      (rdma_vlan_dev_real_dev(ip_dev) == iwdev->netdev)) ||
		     (ip_dev == iwdev->netdev)) && (ip_dev->flags & IFF_UP)) {
			idev = __in6_dev_get(ip_dev);
			if (!idev) {
				i40iw_pr_err("ipv6 inet device not found\n");
				break;
			}
			list_for_each_entry_safe(ifp, tmp, &idev->addr_list, if_list) {
				i40iw_pr_info("IP=%pI6, vlan_id=%d, MAC=%pM\n", &ifp->addr,
					      rdma_vlan_dev_vlan_id(ip_dev), ip_dev->dev_addr);
				i40iw_copy_ip_ntohl(local_ipaddr6,
						    ifp->addr.in6_u.u6_addr32);
				i40iw_manage_arp_cache(iwdev,
						       ip_dev->dev_addr,
						       local_ipaddr6,
						       false,
						       I40IW_ARP_ADD);
			}
		}
	}
	rcu_read_unlock();
}

/**
 * i40iw_add_ipv4_addr - add ipv4 address to the hw arp table
 * @iwdev: iwarp device
 */
static void i40iw_add_ipv4_addr(struct i40iw_device *iwdev)
{
	struct net_device *dev;
	struct in_device *idev;
	bool got_lock = true;
	u32 ip_addr;

	if (!rtnl_trylock())
		got_lock = false;

	for_each_netdev(&init_net, dev) {
		if ((((rdma_vlan_dev_vlan_id(dev) < 0xFFFF) &&
		      (rdma_vlan_dev_real_dev(dev) == iwdev->netdev)) ||
		    (dev == iwdev->netdev)) && (dev->flags & IFF_UP)) {
			idev = in_dev_get(dev);
			for_ifa(idev) {
				i40iw_debug(&iwdev->sc_dev, I40IW_DEBUG_CM,
					    "IP=%pI4, vlan_id=%d, MAC=%pM\n", &ifa->ifa_address,
					     rdma_vlan_dev_vlan_id(dev), dev->dev_addr);

				ip_addr = ntohl(ifa->ifa_address);
				i40iw_manage_arp_cache(iwdev,
						       dev->dev_addr,
						       &ip_addr,
						       true,
						       I40IW_ARP_ADD);
			}
			endfor_ifa(idev);
			in_dev_put(idev);
		}
	}
	if (got_lock)
		rtnl_unlock();
}

/**
 * i40iw_add_mac_ip - add mac and ip addresses
 * @iwdev: iwarp device
 *
 * Create and add a mac ip address entry to the hw table and
 * ipv4/ipv6 addresses to the arp cache
 * Return 0 if successful, otherwise return error
 */
static enum i40iw_status_code i40iw_add_mac_ip(struct i40iw_device *iwdev)
{
	struct net_device *netdev = iwdev->netdev;
	enum i40iw_status_code status;

	status = i40iw_alloc_set_mac_ipaddr(iwdev, (u8 *)netdev->dev_addr);
	if (status)
		return status;
	i40iw_add_ipv4_addr(iwdev);
	i40iw_add_ipv6_addr(iwdev);
	return 0;
}

/**
 * i40iw_wait_pe_ready - Check if firmware is ready
 * @hw: provides access to registers
 */
static void i40iw_wait_pe_ready(struct i40iw_hw *hw)
{
	u32 statusfw;
	u32 statuscpu0;
	u32 statuscpu1;
	u32 statuscpu2;
	u32 retrycount = 0;

	do {
		statusfw = i40iw_rd32(hw, I40E_GLPE_FWLDSTATUS);
		i40iw_pr_info("[%04d] fm load status[x%04X]\n", __LINE__, statusfw);
		statuscpu0 = i40iw_rd32(hw, I40E_GLPE_CPUSTATUS0);
		i40iw_pr_info("[%04d] CSR_CQP status[x%04X]\n", __LINE__, statuscpu0);
		statuscpu1 = i40iw_rd32(hw, I40E_GLPE_CPUSTATUS1);
		i40iw_pr_info("[%04d] I40E_GLPE_CPUSTATUS1 status[x%04X]\n",
			      __LINE__, statuscpu1);
		statuscpu2 = i40iw_rd32(hw, I40E_GLPE_CPUSTATUS2);
		i40iw_pr_info("[%04d] I40E_GLPE_CPUSTATUS2 status[x%04X]\n",
			      __LINE__, statuscpu2);
		if ((statuscpu0 == 0x80) && (statuscpu1 == 0x80) && (statuscpu2 == 0x80))
			break;	/* SUCCESS */
		mdelay(1000);
		retrycount++;
	} while (retrycount < 14);
	i40iw_wr32(hw, 0xb4040, 0x4C104C5);
}

/**
 * i40iw_initialize_dev - initialize device
 * @iwdev: iwarp device
 * @ldev: lan device information
 *
 * Allocate memory for the hmc objects and initialize iwdev
 * Return 0 if successful, otherwise clean up the resources
 * and return error
 */
static enum i40iw_status_code i40iw_initialize_dev(struct i40iw_device *iwdev,
						   struct i40e_info *ldev)
{
	enum i40iw_status_code status;
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	struct i40iw_device_init_info info;
	struct i40iw_vsi_init_info vsi_info;
	struct i40iw_dma_mem mem;
	struct i40iw_l2params l2params;
	u32 size;
	struct i40iw_vsi_stats_info stats_info;
	u16 last_qset = I40IW_NO_QSET;
	u16 qset;
	u32 i;

	memset(&l2params, 0, sizeof(l2params));
	memset(&info, 0, sizeof(info));
	size = sizeof(struct i40iw_hmc_pble_rsrc) + sizeof(struct i40iw_hmc_info) +
				(sizeof(struct i40iw_hmc_obj_info) * I40IW_HMC_IW_MAX);
	iwdev->hmc_info_mem = kzalloc(size, GFP_KERNEL);
	if (!iwdev->hmc_info_mem)
		return I40IW_ERR_NO_MEMORY;

	iwdev->pble_rsrc = (struct i40iw_hmc_pble_rsrc *)iwdev->hmc_info_mem;
	dev->hmc_info = &iwdev->hw.hmc;
	dev->hmc_info->hmc_obj = (struct i40iw_hmc_obj_info *)(iwdev->pble_rsrc + 1);
	status = i40iw_obj_aligned_mem(iwdev, &mem, I40IW_QUERY_FPM_BUF_SIZE,
				       I40IW_FPM_QUERY_BUF_ALIGNMENT_MASK);
	if (status)
		goto error;
	info.fpm_query_buf_pa = mem.pa;
	info.fpm_query_buf = mem.va;
	status = i40iw_obj_aligned_mem(iwdev, &mem, I40IW_COMMIT_FPM_BUF_SIZE,
				       I40IW_FPM_COMMIT_BUF_ALIGNMENT_MASK);
	if (status)
		goto error;
	info.fpm_commit_buf_pa = mem.pa;
	info.fpm_commit_buf = mem.va;
	info.hmc_fn_id = ldev->fid;
	info.is_pf = (ldev->ftype) ? false : true;
	info.bar0 = ldev->hw_addr;
	info.hw = &iwdev->hw;
	info.debug_mask = debug;
	l2params.mss =
		(ldev->params.mtu) ? ldev->params.mtu - I40IW_MTU_TO_MSS : I40IW_DEFAULT_MSS;
	for (i = 0; i < I40E_CLIENT_MAX_USER_PRIORITY; i++) {
		qset = ldev->params.qos.prio_qos[i].qs_handle;
		l2params.qs_handle_list[i] = qset;
		if (last_qset == I40IW_NO_QSET)
			last_qset = qset;
		else if ((qset != last_qset) && (qset != I40IW_NO_QSET))
			iwdev->dcb = true;
	}
	i40iw_pr_info("DCB is set/clear = %d\n", iwdev->dcb);
	info.exception_lan_queue = 1;
	info.vchnl_send = i40iw_virtchnl_send;
	status = i40iw_device_init(&iwdev->sc_dev, &info);

	if (status)
		goto error;
	memset(&vsi_info, 0, sizeof(vsi_info));
	vsi_info.dev = &iwdev->sc_dev;
	vsi_info.back_vsi = (void *)iwdev;
	vsi_info.params = &l2params;
	i40iw_sc_vsi_init(&iwdev->vsi, &vsi_info);

	if (dev->is_pf) {
		memset(&stats_info, 0, sizeof(stats_info));
		stats_info.fcn_id = ldev->fid;
		stats_info.pestat = kzalloc(sizeof(*stats_info.pestat), GFP_KERNEL);
		if (!stats_info.pestat) {
			status = I40IW_ERR_NO_MEMORY;
			goto error;
		}
		stats_info.stats_initialize = true;
		if (stats_info.pestat)
			i40iw_vsi_stats_init(&iwdev->vsi, &stats_info);
	}
	return status;
error:
	kfree(iwdev->hmc_info_mem);
	iwdev->hmc_info_mem = NULL;
	return status;
}

/**
 * i40iw_register_notifiers - register tcp ip notifiers
 */
static void i40iw_register_notifiers(void)
{
	if (atomic_inc_return(&i40iw_notifiers_registered) == 1) {
		register_inetaddr_notifier(&i40iw_inetaddr_notifier);
		register_inet6addr_notifier(&i40iw_inetaddr6_notifier);
		register_netevent_notifier(&i40iw_net_notifier);
	}
}

/**
 * i40iw_save_msix_info - copy msix vector information to iwarp device
 * @iwdev: iwarp device
 * @ldev: lan device information
 *
 * Allocate iwdev msix table and copy the ldev msix info to the table
 * Return 0 if successful, otherwise return error
 */
static enum i40iw_status_code i40iw_save_msix_info(struct i40iw_device *iwdev,
						   struct i40e_info *ldev)
{
	struct i40e_qvlist_info *iw_qvlist;
	struct i40e_qv_info *iw_qvinfo;
	u32 ceq_idx;
	u32 i;
	u32 size;

	iwdev->msix_count = ldev->msix_count;

	size = sizeof(struct i40iw_msix_vector) * iwdev->msix_count;
	size += sizeof(struct i40e_qvlist_info);
	size +=  sizeof(struct i40e_qv_info) * iwdev->msix_count - 1;
	iwdev->iw_msixtbl = kzalloc(size, GFP_KERNEL);

	if (!iwdev->iw_msixtbl)
		return I40IW_ERR_NO_MEMORY;
	iwdev->iw_qvlist = (struct i40e_qvlist_info *)(&iwdev->iw_msixtbl[iwdev->msix_count]);
	iw_qvlist = iwdev->iw_qvlist;
	iw_qvinfo = iw_qvlist->qv_info;
	iw_qvlist->num_vectors = iwdev->msix_count;
	if (iwdev->msix_count <= num_online_cpus())
		iwdev->msix_shared = true;
	for (i = 0, ceq_idx = 0; i < iwdev->msix_count; i++, iw_qvinfo++) {
		iwdev->iw_msixtbl[i].idx = ldev->msix_entries[i].entry;
		iwdev->iw_msixtbl[i].irq = ldev->msix_entries[i].vector;
		iwdev->iw_msixtbl[i].cpu_affinity = ceq_idx;
		if (i == 0) {
			iw_qvinfo->aeq_idx = 0;
			if (iwdev->msix_shared)
				iw_qvinfo->ceq_idx = ceq_idx++;
			else
				iw_qvinfo->ceq_idx = I40E_QUEUE_INVALID_IDX;
		} else {
			iw_qvinfo->aeq_idx = I40E_QUEUE_INVALID_IDX;
			iw_qvinfo->ceq_idx = ceq_idx++;
		}
		iw_qvinfo->itr_idx = 3;
		iw_qvinfo->v_idx = iwdev->iw_msixtbl[i].idx;
	}
	return 0;
}

/**
 * i40iw_deinit_device - clean up the device resources
 * @iwdev: iwarp device
 * @reset: true if called before reset
 *
 * Destroy the ib device interface, remove the mac ip entry and ipv4/ipv6 addresses,
 * destroy the device queues and free the pble and the hmc objects
 */
static void i40iw_deinit_device(struct i40iw_device *iwdev, bool reset)
{
	struct i40e_info *ldev = iwdev->ldev;

	struct i40iw_sc_dev *dev = &iwdev->sc_dev;

	i40iw_pr_info("state = %d\n", iwdev->init_state);
	if (iwdev->param_wq)
		destroy_workqueue(iwdev->param_wq);

	switch (iwdev->init_state) {
	case RDMA_DEV_REGISTERED:
		iwdev->iw_status = 0;
		i40iw_port_ibevent(iwdev);
		i40iw_destroy_rdma_device(iwdev->iwibdev);
		/* fallthrough */
	case IP_ADDR_REGISTERED:
		if (!reset)
			i40iw_del_macip_entry(iwdev, (u8)iwdev->mac_ip_table_idx);
		/* fallthrough */
	case INET_NOTIFIER:
		if (!atomic_dec_return(&i40iw_notifiers_registered)) {
			unregister_netevent_notifier(&i40iw_net_notifier);
			unregister_inetaddr_notifier(&i40iw_inetaddr_notifier);
			unregister_inet6addr_notifier(&i40iw_inetaddr6_notifier);
		}
		/* fallthrough */
	case CEQ_CREATED:
		i40iw_dele_ceqs(iwdev, reset);
		/* fallthrough */
	case AEQ_CREATED:
		i40iw_destroy_aeq(iwdev, reset);
		/* fallthrough */
	case IEQ_CREATED:
		i40iw_puda_dele_resources(&iwdev->vsi, I40IW_PUDA_RSRC_TYPE_IEQ, reset);
		/* fallthrough */
	case ILQ_CREATED:
		i40iw_puda_dele_resources(&iwdev->vsi, I40IW_PUDA_RSRC_TYPE_ILQ, reset);
		/* fallthrough */
	case CCQ_CREATED:
		i40iw_destroy_ccq(iwdev, reset);
		/* fallthrough */
	case PBLE_CHUNK_MEM:
		i40iw_destroy_pble_pool(dev, iwdev->pble_rsrc);
		/* fallthrough */
	case HMC_OBJS_CREATED:
		i40iw_del_hmc_objects(dev, dev->hmc_info, true, reset);
		/* fallthrough */
	case CQP_CREATED:
		i40iw_destroy_cqp(iwdev, true);
		/* fallthrough */
	case INITIAL_STATE:
		i40iw_cleanup_cm_core(&iwdev->cm_core);
		if (iwdev->vsi.pestat) {
			i40iw_vsi_stats_free(&iwdev->vsi);
			kfree(iwdev->vsi.pestat);
		}
		i40iw_del_init_mem(iwdev);
		break;
	case INVALID_STATE:
		/* fallthrough */
	default:
		i40iw_pr_err("bad init_state = %d\n", iwdev->init_state);
		break;
	}

	i40iw_del_handler(i40iw_find_i40e_handler(ldev));
	kfree(iwdev->hdl);
}

/**
 * i40iw_setup_init_state - set up the initial device struct
 * @hdl: handler for iwarp device - one per instance
 * @ldev: lan device information
 * @client: iwarp client information, provided during registration
 *
 * Initialize the iwarp device and its hdl information
 * using the ldev and client information
 * Return 0 if successful, otherwise return error
 */
static enum i40iw_status_code i40iw_setup_init_state(struct i40iw_handler *hdl,
						     struct i40e_info *ldev,
						     struct i40e_client *client)
{
	struct i40iw_device *iwdev = &hdl->device;
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	enum i40iw_status_code status;

	memcpy(&hdl->ldev, ldev, sizeof(*ldev));
	if (resource_profile == 1)
		resource_profile = 2;

	iwdev->mpa_version = mpa_version;
	iwdev->resource_profile = (resource_profile < I40IW_HMC_PROFILE_EQUAL) ?
	    (u8)resource_profile + I40IW_HMC_PROFILE_DEFAULT :
	    I40IW_HMC_PROFILE_DEFAULT;
	iwdev->max_rdma_vfs =
		(iwdev->resource_profile != I40IW_HMC_PROFILE_DEFAULT) ?  max_rdma_vfs : 0;
	iwdev->max_enabled_vfs = iwdev->max_rdma_vfs;
	iwdev->netdev = ldev->netdev;
	hdl->client = client;
	if (!ldev->ftype)
		iwdev->db_start = pci_resource_start(ldev->pcidev, 0) + I40IW_DB_ADDR_OFFSET;
	else
		iwdev->db_start = pci_resource_start(ldev->pcidev, 0) + I40IW_VF_DB_ADDR_OFFSET;

	status = i40iw_save_msix_info(iwdev, ldev);
	if (status)
		goto exit;
	iwdev->hw.dev_context = (void *)ldev->pcidev;
	iwdev->hw.hw_addr = ldev->hw_addr;
	status = i40iw_allocate_dma_mem(&iwdev->hw,
					&iwdev->obj_mem, 8192, 4096);
	if (status)
		goto exit;
	iwdev->obj_next = iwdev->obj_mem;
	iwdev->push_mode = push_mode;

	init_waitqueue_head(&iwdev->vchnl_waitq);
	init_waitqueue_head(&dev->vf_reqs);
	init_waitqueue_head(&iwdev->close_wq);

	status = i40iw_initialize_dev(iwdev, ldev);
exit:
	if (status) {
		kfree(iwdev->iw_msixtbl);
		i40iw_free_dma_mem(dev->hw, &iwdev->obj_mem);
		iwdev->iw_msixtbl = NULL;
	}
	return status;
}

/**
 * i40iw_get_used_rsrc - determine resources used internally
 * @iwdev: iwarp device
 *
 * Called after internal allocations
 */
static void i40iw_get_used_rsrc(struct i40iw_device *iwdev)
{
	iwdev->used_pds = find_next_zero_bit(iwdev->allocated_pds, iwdev->max_pd, 0);
	iwdev->used_qps = find_next_zero_bit(iwdev->allocated_qps, iwdev->max_qp, 0);
	iwdev->used_cqs = find_next_zero_bit(iwdev->allocated_cqs, iwdev->max_cq, 0);
	iwdev->used_mrs = find_next_zero_bit(iwdev->allocated_mrs, iwdev->max_mr, 0);
}

/**
 * i40iw_open - client interface operation open for iwarp/uda device
 * @ldev: lan device information
 * @client: iwarp client information, provided during registration
 *
 * Called by the lan driver during the processing of client register
 * Create device resources, set up queues, pble and hmc objects and
 * register the device with the ib verbs interface
 * Return 0 if successful, otherwise return error
 */
static int i40iw_open(struct i40e_info *ldev, struct i40e_client *client)
{
	struct i40iw_device *iwdev;
	struct i40iw_sc_dev *dev;
	enum i40iw_status_code status;
	struct i40iw_handler *hdl;

	hdl = i40iw_find_netdev(ldev->netdev);
	if (hdl)
		return 0;

	hdl = kzalloc(sizeof(*hdl), GFP_KERNEL);
	if (!hdl)
		return -ENOMEM;
	iwdev = &hdl->device;
	iwdev->hdl = hdl;
	dev = &iwdev->sc_dev;
	i40iw_setup_cm_core(iwdev);

	dev->back_dev = (void *)iwdev;
	iwdev->ldev = &hdl->ldev;
	iwdev->client = client;
	mutex_init(&iwdev->pbl_mutex);
	i40iw_add_handler(hdl);

	do {
		status = i40iw_setup_init_state(hdl, ldev, client);
		if (status)
			break;
		iwdev->init_state = INITIAL_STATE;
		if (dev->is_pf)
			i40iw_wait_pe_ready(dev->hw);
		status = i40iw_create_cqp(iwdev);
		if (status)
			break;
		iwdev->init_state = CQP_CREATED;
		status = i40iw_hmc_setup(iwdev);
		if (status)
			break;
		status = i40iw_create_ccq(iwdev);
		if (status)
			break;
		iwdev->init_state = CCQ_CREATED;
		status = i40iw_initialize_ilq(iwdev);
		if (status)
			break;
		iwdev->init_state = ILQ_CREATED;
		status = i40iw_initialize_ieq(iwdev);
		if (status)
			break;
		iwdev->init_state = IEQ_CREATED;
		status = i40iw_setup_aeq(iwdev);
		if (status)
			break;
		iwdev->init_state = AEQ_CREATED;
		status = i40iw_setup_ceqs(iwdev, ldev);
		if (status)
			break;
		iwdev->init_state = CEQ_CREATED;
		status = i40iw_initialize_hw_resources(iwdev);
		if (status)
			break;
		i40iw_get_used_rsrc(iwdev);
		dev->ccq_ops->ccq_arm(dev->ccq);
		status = i40iw_hmc_init_pble(&iwdev->sc_dev, iwdev->pble_rsrc);
		if (status)
			break;
		iwdev->virtchnl_wq = alloc_ordered_workqueue("iwvch", WQ_MEM_RECLAIM);
		i40iw_register_notifiers();
		iwdev->init_state = INET_NOTIFIER;
		status = i40iw_add_mac_ip(iwdev);
		if (status)
			break;
		iwdev->init_state = IP_ADDR_REGISTERED;
		if (i40iw_register_rdma_device(iwdev)) {
			i40iw_pr_err("register rdma device fail\n");
			break;
		};

		iwdev->init_state = RDMA_DEV_REGISTERED;
		iwdev->iw_status = 1;
		i40iw_port_ibevent(iwdev);
		iwdev->param_wq = alloc_ordered_workqueue("l2params", WQ_MEM_RECLAIM);
		if(iwdev->param_wq == NULL)
			break;
		i40iw_pr_info("i40iw_open completed\n");
		return 0;
	} while (0);

	i40iw_pr_err("status = %d last completion = %d\n", status, iwdev->init_state);
	i40iw_deinit_device(iwdev, false);
	return -ERESTART;
}

/**
 * i40iw_l2params_worker - worker for l2 params change
 * @work: work pointer for l2 params
 */
static void i40iw_l2params_worker(struct work_struct *work)
{
	struct l2params_work *dwork =
	    container_of(work, struct l2params_work, work);
	struct i40iw_device *iwdev = dwork->iwdev;

	i40iw_change_l2params(&iwdev->vsi, &dwork->l2params);
	atomic_dec(&iwdev->params_busy);
	kfree(work);
}

/**
 * i40iw_l2param_change - handle qs handles for qos and mss change
 * @ldev: lan device information
 * @client: client for paramater change
 * @params: new parameters from L2
 */
static void i40iw_l2param_change(struct i40e_info *ldev, struct i40e_client *client,
				 struct i40e_params *params)
{
	struct i40iw_handler *hdl;
	struct i40iw_l2params *l2params;
	struct l2params_work *work;
	struct i40iw_device *iwdev;
	int i;

	hdl = i40iw_find_i40e_handler(ldev);
	if (!hdl)
		return;

	iwdev = &hdl->device;

	if (atomic_read(&iwdev->params_busy))
		return;


	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return;

	atomic_inc(&iwdev->params_busy);

	work->iwdev = iwdev;
	l2params = &work->l2params;
	for (i = 0; i < I40E_CLIENT_MAX_USER_PRIORITY; i++)
		l2params->qs_handle_list[i] = params->qos.prio_qos[i].qs_handle;

	l2params->mss = (params->mtu) ? params->mtu - I40IW_MTU_TO_MSS : iwdev->vsi.mss;

	INIT_WORK(&work->work, i40iw_l2params_worker);
	queue_work(iwdev->param_wq, &work->work);
}

/**
 * i40iw_close - client interface operation close for iwarp/uda device
 * @ldev: lan device information
 * @client: client to close
 *
 * Called by the lan driver during the processing of client unregister
 * Destroy and clean up the driver resources
 */
static void i40iw_close(struct i40e_info *ldev, struct i40e_client *client, bool reset)
{
	struct i40iw_device *iwdev;
	struct i40iw_handler *hdl;

	hdl = i40iw_find_i40e_handler(ldev);
	if (!hdl)
		return;

	iwdev = &hdl->device;
	iwdev->closing = true;

	i40iw_cm_disconnect_all(iwdev);
	destroy_workqueue(iwdev->virtchnl_wq);
	i40iw_deinit_device(iwdev, reset);
}

/**
 * i40iw_vf_reset - process VF reset
 * @ldev: lan device information
 * @client: client interface instance
 * @vf_id: virtual function id
 *
 * Called when a VF is reset by the PF
 * Destroy and clean up the VF resources
 */
static void i40iw_vf_reset(struct i40e_info *ldev, struct i40e_client *client, u32 vf_id)
{
	struct i40iw_handler *hdl;
	struct i40iw_sc_dev *dev;
	struct i40iw_hmc_fcn_info hmc_fcn_info;
	struct i40iw_virt_mem vf_dev_mem;
	struct i40iw_vfdev *tmp_vfdev;
	unsigned int i;
	unsigned long flags;
	struct i40iw_device *iwdev;

	hdl = i40iw_find_i40e_handler(ldev);
	if (!hdl)
		return;

	dev = &hdl->device.sc_dev;
	iwdev = (struct i40iw_device *)dev->back_dev;

	for (i = 0; i < I40IW_MAX_PE_ENABLED_VF_COUNT; i++) {
		if (!dev->vf_dev[i] || (dev->vf_dev[i]->vf_id != vf_id))
			continue;
		/* free all resources allocated on behalf of vf */
		tmp_vfdev = dev->vf_dev[i];
		spin_lock_irqsave(&iwdev->vsi.pestat->lock, flags);
		dev->vf_dev[i] = NULL;
		spin_unlock_irqrestore(&iwdev->vsi.pestat->lock, flags);
		i40iw_del_hmc_objects(dev, &tmp_vfdev->hmc_info, false, false);
		/* remove vf hmc function */
		memset(&hmc_fcn_info, 0, sizeof(hmc_fcn_info));
		hmc_fcn_info.vf_id = vf_id;
		hmc_fcn_info.iw_vf_idx = tmp_vfdev->iw_vf_idx;
		hmc_fcn_info.free_fcn = true;
		i40iw_cqp_manage_hmc_fcn_cmd(dev, &hmc_fcn_info);
		/* free vf_dev */
		vf_dev_mem.va = tmp_vfdev;
		vf_dev_mem.size = sizeof(struct i40iw_vfdev) +
					sizeof(struct i40iw_hmc_obj_info) * I40IW_HMC_IW_MAX;
		i40iw_free_virt_mem(dev->hw, &vf_dev_mem);
		break;
	}
}

/**
 * i40iw_vf_enable - enable a number of VFs
 * @ldev: lan device information
 * @client: client interface instance
 * @num_vfs: number of VFs for the PF
 *
 * Called when the number of VFs changes
 */
static void i40iw_vf_enable(struct i40e_info *ldev,
			    struct i40e_client *client,
			    u32 num_vfs)
{
	struct i40iw_handler *hdl;

	hdl = i40iw_find_i40e_handler(ldev);
	if (!hdl)
		return;

	if (num_vfs > I40IW_MAX_PE_ENABLED_VF_COUNT)
		hdl->device.max_enabled_vfs = I40IW_MAX_PE_ENABLED_VF_COUNT;
	else
		hdl->device.max_enabled_vfs = num_vfs;
}

/**
 * i40iw_vf_capable - check if VF capable
 * @ldev: lan device information
 * @client: client interface instance
 * @vf_id: virtual function id
 *
 * Return 1 if a VF slot is available or if VF is already RDMA enabled
 * Return 0 otherwise
 */
static int i40iw_vf_capable(struct i40e_info *ldev,
			    struct i40e_client *client,
			    u32 vf_id)
{
	struct i40iw_handler *hdl;
	struct i40iw_sc_dev *dev;
	unsigned int i;

	hdl = i40iw_find_i40e_handler(ldev);
	if (!hdl)
		return 0;

	dev = &hdl->device.sc_dev;

	for (i = 0; i < hdl->device.max_enabled_vfs; i++) {
		if (!dev->vf_dev[i] || (dev->vf_dev[i]->vf_id == vf_id))
			return 1;
	}

	return 0;
}

/**
 * i40iw_virtchnl_receive - receive a message through the virtual channel
 * @ldev: lan device information
 * @client: client interface instance
 * @vf_id: virtual function id associated with the message
 * @msg: message buffer pointer
 * @len: length of the message
 *
 * Invoke virtual channel receive operation for the given msg
 * Return 0 if successful, otherwise return error
 */
static int i40iw_virtchnl_receive(struct i40e_info *ldev,
				  struct i40e_client *client,
				  u32 vf_id,
				  u8 *msg,
				  u16 len)
{
	struct i40iw_handler *hdl;
	struct i40iw_sc_dev *dev;
	struct i40iw_device *iwdev;
	int ret_code = I40IW_NOT_SUPPORTED;

	if (!len || !msg)
		return I40IW_ERR_PARAM;

	hdl = i40iw_find_i40e_handler(ldev);
	if (!hdl)
		return I40IW_ERR_PARAM;

	dev = &hdl->device.sc_dev;
	iwdev = dev->back_dev;

	if (dev->vchnl_if.vchnl_recv) {
		ret_code = dev->vchnl_if.vchnl_recv(dev, vf_id, msg, len);
		if (!dev->is_pf) {
			atomic_dec(&iwdev->vchnl_msgs);
			wake_up(&iwdev->vchnl_waitq);
		}
	}
	return ret_code;
}

/**
 * i40iw_vf_clear_to_send - wait to send virtual channel message
 * @dev: iwarp device *
 * Wait for until virtual channel is clear
 * before sending the next message
 *
 * Returns false if error
 * Returns true if clear to send
 */
bool i40iw_vf_clear_to_send(struct i40iw_sc_dev *dev)
{
	struct i40iw_device *iwdev;
	wait_queue_t wait;

	iwdev = dev->back_dev;

	if (!wq_has_sleeper(&dev->vf_reqs) &&
	    (atomic_read(&iwdev->vchnl_msgs) == 0))
		return true; /* virtual channel is clear */

	init_wait(&wait);
	add_wait_queue_exclusive(&dev->vf_reqs, &wait);

	if (!wait_event_timeout(dev->vf_reqs,
				(atomic_read(&iwdev->vchnl_msgs) == 0),
				I40IW_VCHNL_EVENT_TIMEOUT))
		dev->vchnl_up = false;

	remove_wait_queue(&dev->vf_reqs, &wait);

	return dev->vchnl_up;
}

/**
 * i40iw_virtchnl_send - send a message through the virtual channel
 * @dev: iwarp device
 * @vf_id: virtual function id associated with the message
 * @msg: virtual channel message buffer pointer
 * @len: length of the message
 *
 * Invoke virtual channel send operation for the given msg
 * Return 0 if successful, otherwise return error
 */
static enum i40iw_status_code i40iw_virtchnl_send(struct i40iw_sc_dev *dev,
						  u32 vf_id,
						  u8 *msg,
						  u16 len)
{
	struct i40iw_device *iwdev;
	struct i40e_info *ldev;

	if (!dev || !dev->back_dev)
		return I40IW_ERR_BAD_PTR;

	iwdev = dev->back_dev;
	ldev = iwdev->ldev;

	if (ldev && ldev->ops && ldev->ops->virtchnl_send)
		return ldev->ops->virtchnl_send(ldev, &i40iw_client, vf_id, msg, len);
	return I40IW_ERR_BAD_PTR;
}

/* client interface functions */
static const struct i40e_client_ops i40e_ops = {
	.open = i40iw_open,
	.close = i40iw_close,
	.l2_param_change = i40iw_l2param_change,
	.virtchnl_receive = i40iw_virtchnl_receive,
	.vf_reset = i40iw_vf_reset,
	.vf_enable = i40iw_vf_enable,
	.vf_capable = i40iw_vf_capable
};

/**
 * i40iw_init_module - driver initialization function
 *
 * First function to call when the driver is loaded
 * Register the driver as i40e client and port mapper client
 */
static int __init i40iw_init_module(void)
{
	int ret;

	memset(&i40iw_client, 0, sizeof(i40iw_client));
	i40iw_client.version.major = CLIENT_IW_INTERFACE_VERSION_MAJOR;
	i40iw_client.version.minor = CLIENT_IW_INTERFACE_VERSION_MINOR;
	i40iw_client.version.build = CLIENT_IW_INTERFACE_VERSION_BUILD;
	i40iw_client.ops = &i40e_ops;
	memcpy(i40iw_client.name, i40iw_client_name, I40E_CLIENT_STR_LENGTH);
	i40iw_client.type = I40E_CLIENT_IWARP;
	spin_lock_init(&i40iw_handler_lock);
	ret = i40e_register_client(&i40iw_client);
	return ret;
}

/**
 * i40iw_exit_module - driver exit clean up function
 *
 * The function is called just before the driver is unloaded
 * Unregister the driver as i40e client and port mapper client
 */
static void __exit i40iw_exit_module(void)
{
	i40e_unregister_client(&i40iw_client);
}

module_init(i40iw_init_module);
module_exit(i40iw_exit_module);

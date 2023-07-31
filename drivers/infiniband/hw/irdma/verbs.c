// SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB
/* Copyright (c) 2015 - 2021 Intel Corporation */
#include "main.h"

/**
 * irdma_query_device - get device attributes
 * @ibdev: device pointer from stack
 * @props: returning device attributes
 * @udata: user data
 */
static int irdma_query_device(struct ib_device *ibdev,
			      struct ib_device_attr *props,
			      struct ib_udata *udata)
{
	struct irdma_device *iwdev = to_iwdev(ibdev);
	struct irdma_pci_f *rf = iwdev->rf;
	struct pci_dev *pcidev = iwdev->rf->pcidev;
	struct irdma_hw_attrs *hw_attrs = &rf->sc_dev.hw_attrs;

	if (udata->inlen || udata->outlen)
		return -EINVAL;

	memset(props, 0, sizeof(*props));
	addrconf_addr_eui48((u8 *)&props->sys_image_guid,
			    iwdev->netdev->dev_addr);
	props->fw_ver = (u64)irdma_fw_major_ver(&rf->sc_dev) << 32 |
			irdma_fw_minor_ver(&rf->sc_dev);
	props->device_cap_flags = IB_DEVICE_MEM_WINDOW |
				  IB_DEVICE_MEM_MGT_EXTENSIONS;
	props->kernel_cap_flags = IBK_LOCAL_DMA_LKEY;
	props->vendor_id = pcidev->vendor;
	props->vendor_part_id = pcidev->device;

	props->hw_ver = rf->pcidev->revision;
	props->page_size_cap = hw_attrs->page_size_cap;
	props->max_mr_size = hw_attrs->max_mr_size;
	props->max_qp = rf->max_qp - rf->used_qps;
	props->max_qp_wr = hw_attrs->max_qp_wr;
	props->max_send_sge = hw_attrs->uk_attrs.max_hw_wq_frags;
	props->max_recv_sge = hw_attrs->uk_attrs.max_hw_wq_frags;
	props->max_cq = rf->max_cq - rf->used_cqs;
	props->max_cqe = rf->max_cqe - 1;
	props->max_mr = rf->max_mr - rf->used_mrs;
	props->max_mw = props->max_mr;
	props->max_pd = rf->max_pd - rf->used_pds;
	props->max_sge_rd = hw_attrs->uk_attrs.max_hw_read_sges;
	props->max_qp_rd_atom = hw_attrs->max_hw_ird;
	props->max_qp_init_rd_atom = hw_attrs->max_hw_ord;
	if (rdma_protocol_roce(ibdev, 1)) {
		props->device_cap_flags |= IB_DEVICE_RC_RNR_NAK_GEN;
		props->max_pkeys = IRDMA_PKEY_TBL_SZ;
	}

	props->max_ah = rf->max_ah;
	props->max_mcast_grp = rf->max_mcg;
	props->max_mcast_qp_attach = IRDMA_MAX_MGS_PER_CTX;
	props->max_total_mcast_qp_attach = rf->max_qp * IRDMA_MAX_MGS_PER_CTX;
	props->max_fast_reg_page_list_len = IRDMA_MAX_PAGES_PER_FMR;
#define HCA_CLOCK_TIMESTAMP_MASK 0x1ffff
	if (hw_attrs->uk_attrs.hw_rev >= IRDMA_GEN_2)
		props->timestamp_mask = HCA_CLOCK_TIMESTAMP_MASK;

	return 0;
}

/**
 * irdma_query_port - get port attributes
 * @ibdev: device pointer from stack
 * @port: port number for query
 * @props: returning device attributes
 */
static int irdma_query_port(struct ib_device *ibdev, u32 port,
			    struct ib_port_attr *props)
{
	struct irdma_device *iwdev = to_iwdev(ibdev);
	struct net_device *netdev = iwdev->netdev;

	/* no need to zero out pros here. done by caller */

	props->max_mtu = IB_MTU_4096;
	props->active_mtu = ib_mtu_int_to_enum(netdev->mtu);
	props->lid = 1;
	props->lmc = 0;
	props->sm_lid = 0;
	props->sm_sl = 0;
	if (netif_carrier_ok(netdev) && netif_running(netdev)) {
		props->state = IB_PORT_ACTIVE;
		props->phys_state = IB_PORT_PHYS_STATE_LINK_UP;
	} else {
		props->state = IB_PORT_DOWN;
		props->phys_state = IB_PORT_PHYS_STATE_DISABLED;
	}

	ib_get_eth_speed(ibdev, port, &props->active_speed,
			 &props->active_width);

	if (rdma_protocol_roce(ibdev, 1)) {
		props->gid_tbl_len = 32;
		props->ip_gids = true;
		props->pkey_tbl_len = IRDMA_PKEY_TBL_SZ;
	} else {
		props->gid_tbl_len = 1;
	}
	props->qkey_viol_cntr = 0;
	props->port_cap_flags |= IB_PORT_CM_SUP | IB_PORT_REINIT_SUP;
	props->max_msg_sz = iwdev->rf->sc_dev.hw_attrs.max_hw_outbound_msg_size;

	return 0;
}

/**
 * irdma_disassociate_ucontext - Disassociate user context
 * @context: ib user context
 */
static void irdma_disassociate_ucontext(struct ib_ucontext *context)
{
}

static int irdma_mmap_legacy(struct irdma_ucontext *ucontext,
			     struct vm_area_struct *vma)
{
	u64 pfn;

	if (vma->vm_pgoff || vma->vm_end - vma->vm_start != PAGE_SIZE)
		return -EINVAL;

	vma->vm_private_data = ucontext;
	pfn = ((uintptr_t)ucontext->iwdev->rf->sc_dev.hw_regs[IRDMA_DB_ADDR_OFFSET] +
	       pci_resource_start(ucontext->iwdev->rf->pcidev, 0)) >> PAGE_SHIFT;

	return rdma_user_mmap_io(&ucontext->ibucontext, vma, pfn, PAGE_SIZE,
				 pgprot_noncached(vma->vm_page_prot), NULL);
}

static void irdma_mmap_free(struct rdma_user_mmap_entry *rdma_entry)
{
	struct irdma_user_mmap_entry *entry = to_irdma_mmap_entry(rdma_entry);

	kfree(entry);
}

static struct rdma_user_mmap_entry*
irdma_user_mmap_entry_insert(struct irdma_ucontext *ucontext, u64 bar_offset,
			     enum irdma_mmap_flag mmap_flag, u64 *mmap_offset)
{
	struct irdma_user_mmap_entry *entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	int ret;

	if (!entry)
		return NULL;

	entry->bar_offset = bar_offset;
	entry->mmap_flag = mmap_flag;

	ret = rdma_user_mmap_entry_insert(&ucontext->ibucontext,
					  &entry->rdma_entry, PAGE_SIZE);
	if (ret) {
		kfree(entry);
		return NULL;
	}
	*mmap_offset = rdma_user_mmap_get_offset(&entry->rdma_entry);

	return &entry->rdma_entry;
}

/**
 * irdma_mmap - user memory map
 * @context: context created during alloc
 * @vma: kernel info for user memory map
 */
static int irdma_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	struct rdma_user_mmap_entry *rdma_entry;
	struct irdma_user_mmap_entry *entry;
	struct irdma_ucontext *ucontext;
	u64 pfn;
	int ret;

	ucontext = to_ucontext(context);

	/* Legacy support for libi40iw with hard-coded mmap key */
	if (ucontext->legacy_mode)
		return irdma_mmap_legacy(ucontext, vma);

	rdma_entry = rdma_user_mmap_entry_get(&ucontext->ibucontext, vma);
	if (!rdma_entry) {
		ibdev_dbg(&ucontext->iwdev->ibdev,
			  "VERBS: pgoff[0x%lx] does not have valid entry\n",
			  vma->vm_pgoff);
		return -EINVAL;
	}

	entry = to_irdma_mmap_entry(rdma_entry);
	ibdev_dbg(&ucontext->iwdev->ibdev,
		  "VERBS: bar_offset [0x%llx] mmap_flag [%d]\n",
		  entry->bar_offset, entry->mmap_flag);

	pfn = (entry->bar_offset +
	      pci_resource_start(ucontext->iwdev->rf->pcidev, 0)) >> PAGE_SHIFT;

	switch (entry->mmap_flag) {
	case IRDMA_MMAP_IO_NC:
		ret = rdma_user_mmap_io(context, vma, pfn, PAGE_SIZE,
					pgprot_noncached(vma->vm_page_prot),
					rdma_entry);
		break;
	case IRDMA_MMAP_IO_WC:
		ret = rdma_user_mmap_io(context, vma, pfn, PAGE_SIZE,
					pgprot_writecombine(vma->vm_page_prot),
					rdma_entry);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		ibdev_dbg(&ucontext->iwdev->ibdev,
			  "VERBS: bar_offset [0x%llx] mmap_flag[%d] err[%d]\n",
			  entry->bar_offset, entry->mmap_flag, ret);
	rdma_user_mmap_entry_put(rdma_entry);

	return ret;
}

/**
 * irdma_alloc_push_page - allocate a push page for qp
 * @iwqp: qp pointer
 */
static void irdma_alloc_push_page(struct irdma_qp *iwqp)
{
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	struct irdma_device *iwdev = iwqp->iwdev;
	struct irdma_sc_qp *qp = &iwqp->sc_qp;
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(&iwdev->rf->cqp, true);
	if (!cqp_request)
		return;

	cqp_info = &cqp_request->info;
	cqp_info->cqp_cmd = IRDMA_OP_MANAGE_PUSH_PAGE;
	cqp_info->post_sq = 1;
	cqp_info->in.u.manage_push_page.info.push_idx = 0;
	cqp_info->in.u.manage_push_page.info.qs_handle =
		qp->vsi->qos[qp->user_pri].qs_handle;
	cqp_info->in.u.manage_push_page.info.free_page = 0;
	cqp_info->in.u.manage_push_page.info.push_page_type = 0;
	cqp_info->in.u.manage_push_page.cqp = &iwdev->rf->cqp.sc_cqp;
	cqp_info->in.u.manage_push_page.scratch = (uintptr_t)cqp_request;

	status = irdma_handle_cqp_op(iwdev->rf, cqp_request);
	if (!status && cqp_request->compl_info.op_ret_val <
	    iwdev->rf->sc_dev.hw_attrs.max_hw_device_pages) {
		qp->push_idx = cqp_request->compl_info.op_ret_val;
		qp->push_offset = 0;
	}

	irdma_put_cqp_request(&iwdev->rf->cqp, cqp_request);
}

/**
 * irdma_alloc_ucontext - Allocate the user context data structure
 * @uctx: uverbs context pointer
 * @udata: user data
 *
 * This keeps track of all objects associated with a particular
 * user-mode client.
 */
static int irdma_alloc_ucontext(struct ib_ucontext *uctx,
				struct ib_udata *udata)
{
#define IRDMA_ALLOC_UCTX_MIN_REQ_LEN offsetofend(struct irdma_alloc_ucontext_req, rsvd8)
#define IRDMA_ALLOC_UCTX_MIN_RESP_LEN offsetofend(struct irdma_alloc_ucontext_resp, rsvd)
	struct ib_device *ibdev = uctx->device;
	struct irdma_device *iwdev = to_iwdev(ibdev);
	struct irdma_alloc_ucontext_req req = {};
	struct irdma_alloc_ucontext_resp uresp = {};
	struct irdma_ucontext *ucontext = to_ucontext(uctx);
	struct irdma_uk_attrs *uk_attrs = &iwdev->rf->sc_dev.hw_attrs.uk_attrs;

	if (udata->inlen < IRDMA_ALLOC_UCTX_MIN_REQ_LEN ||
	    udata->outlen < IRDMA_ALLOC_UCTX_MIN_RESP_LEN)
		return -EINVAL;

	if (ib_copy_from_udata(&req, udata, min(sizeof(req), udata->inlen)))
		return -EINVAL;

	if (req.userspace_ver < 4 || req.userspace_ver > IRDMA_ABI_VER)
		goto ver_error;

	ucontext->iwdev = iwdev;
	ucontext->abi_ver = req.userspace_ver;

	if (req.comp_mask & IRDMA_ALLOC_UCTX_USE_RAW_ATTR)
		ucontext->use_raw_attrs = true;

	/* GEN_1 legacy support with libi40iw */
	if (udata->outlen == IRDMA_ALLOC_UCTX_MIN_RESP_LEN) {
		if (uk_attrs->hw_rev != IRDMA_GEN_1)
			return -EOPNOTSUPP;

		ucontext->legacy_mode = true;
		uresp.max_qps = iwdev->rf->max_qp;
		uresp.max_pds = iwdev->rf->sc_dev.hw_attrs.max_hw_pds;
		uresp.wq_size = iwdev->rf->sc_dev.hw_attrs.max_qp_wr * 2;
		uresp.kernel_ver = req.userspace_ver;
		if (ib_copy_to_udata(udata, &uresp,
				     min(sizeof(uresp), udata->outlen)))
			return -EFAULT;
	} else {
		u64 bar_off = (uintptr_t)iwdev->rf->sc_dev.hw_regs[IRDMA_DB_ADDR_OFFSET];

		ucontext->db_mmap_entry =
			irdma_user_mmap_entry_insert(ucontext, bar_off,
						     IRDMA_MMAP_IO_NC,
						     &uresp.db_mmap_key);
		if (!ucontext->db_mmap_entry)
			return -ENOMEM;

		uresp.kernel_ver = IRDMA_ABI_VER;
		uresp.feature_flags = uk_attrs->feature_flags;
		uresp.max_hw_wq_frags = uk_attrs->max_hw_wq_frags;
		uresp.max_hw_read_sges = uk_attrs->max_hw_read_sges;
		uresp.max_hw_inline = uk_attrs->max_hw_inline;
		uresp.max_hw_rq_quanta = uk_attrs->max_hw_rq_quanta;
		uresp.max_hw_wq_quanta = uk_attrs->max_hw_wq_quanta;
		uresp.max_hw_sq_chunk = uk_attrs->max_hw_sq_chunk;
		uresp.max_hw_cq_size = uk_attrs->max_hw_cq_size;
		uresp.min_hw_cq_size = uk_attrs->min_hw_cq_size;
		uresp.hw_rev = uk_attrs->hw_rev;
		uresp.comp_mask |= IRDMA_ALLOC_UCTX_USE_RAW_ATTR;
		uresp.min_hw_wq_size = uk_attrs->min_hw_wq_size;
		uresp.comp_mask |= IRDMA_ALLOC_UCTX_MIN_HW_WQ_SIZE;
		if (ib_copy_to_udata(udata, &uresp,
				     min(sizeof(uresp), udata->outlen))) {
			rdma_user_mmap_entry_remove(ucontext->db_mmap_entry);
			return -EFAULT;
		}
	}

	INIT_LIST_HEAD(&ucontext->cq_reg_mem_list);
	spin_lock_init(&ucontext->cq_reg_mem_list_lock);
	INIT_LIST_HEAD(&ucontext->qp_reg_mem_list);
	spin_lock_init(&ucontext->qp_reg_mem_list_lock);

	return 0;

ver_error:
	ibdev_err(&iwdev->ibdev,
		  "Invalid userspace driver version detected. Detected version %d, should be %d\n",
		  req.userspace_ver, IRDMA_ABI_VER);
	return -EINVAL;
}

/**
 * irdma_dealloc_ucontext - deallocate the user context data structure
 * @context: user context created during alloc
 */
static void irdma_dealloc_ucontext(struct ib_ucontext *context)
{
	struct irdma_ucontext *ucontext = to_ucontext(context);

	rdma_user_mmap_entry_remove(ucontext->db_mmap_entry);
}

/**
 * irdma_alloc_pd - allocate protection domain
 * @pd: PD pointer
 * @udata: user data
 */
static int irdma_alloc_pd(struct ib_pd *pd, struct ib_udata *udata)
{
#define IRDMA_ALLOC_PD_MIN_RESP_LEN offsetofend(struct irdma_alloc_pd_resp, rsvd)
	struct irdma_pd *iwpd = to_iwpd(pd);
	struct irdma_device *iwdev = to_iwdev(pd->device);
	struct irdma_sc_dev *dev = &iwdev->rf->sc_dev;
	struct irdma_pci_f *rf = iwdev->rf;
	struct irdma_alloc_pd_resp uresp = {};
	struct irdma_sc_pd *sc_pd;
	u32 pd_id = 0;
	int err;

	if (udata && udata->outlen < IRDMA_ALLOC_PD_MIN_RESP_LEN)
		return -EINVAL;

	err = irdma_alloc_rsrc(rf, rf->allocated_pds, rf->max_pd, &pd_id,
			       &rf->next_pd);
	if (err)
		return err;

	sc_pd = &iwpd->sc_pd;
	if (udata) {
		struct irdma_ucontext *ucontext =
			rdma_udata_to_drv_context(udata, struct irdma_ucontext,
						  ibucontext);
		irdma_sc_pd_init(dev, sc_pd, pd_id, ucontext->abi_ver);
		uresp.pd_id = pd_id;
		if (ib_copy_to_udata(udata, &uresp,
				     min(sizeof(uresp), udata->outlen))) {
			err = -EFAULT;
			goto error;
		}
	} else {
		irdma_sc_pd_init(dev, sc_pd, pd_id, IRDMA_ABI_VER);
	}

	return 0;
error:
	irdma_free_rsrc(rf, rf->allocated_pds, pd_id);

	return err;
}

/**
 * irdma_dealloc_pd - deallocate pd
 * @ibpd: ptr of pd to be deallocated
 * @udata: user data
 */
static int irdma_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct irdma_pd *iwpd = to_iwpd(ibpd);
	struct irdma_device *iwdev = to_iwdev(ibpd->device);

	irdma_free_rsrc(iwdev->rf, iwdev->rf->allocated_pds, iwpd->sc_pd.pd_id);

	return 0;
}

/**
 * irdma_get_pbl - Retrieve pbl from a list given a virtual
 * address
 * @va: user virtual address
 * @pbl_list: pbl list to search in (QP's or CQ's)
 */
static struct irdma_pbl *irdma_get_pbl(unsigned long va,
				       struct list_head *pbl_list)
{
	struct irdma_pbl *iwpbl;

	list_for_each_entry (iwpbl, pbl_list, list) {
		if (iwpbl->user_base == va) {
			list_del(&iwpbl->list);
			iwpbl->on_list = false;
			return iwpbl;
		}
	}

	return NULL;
}

/**
 * irdma_clean_cqes - clean cq entries for qp
 * @iwqp: qp ptr (user or kernel)
 * @iwcq: cq ptr
 */
static void irdma_clean_cqes(struct irdma_qp *iwqp, struct irdma_cq *iwcq)
{
	struct irdma_cq_uk *ukcq = &iwcq->sc_cq.cq_uk;
	unsigned long flags;

	spin_lock_irqsave(&iwcq->lock, flags);
	irdma_uk_clean_cq(&iwqp->sc_qp.qp_uk, ukcq);
	spin_unlock_irqrestore(&iwcq->lock, flags);
}

static void irdma_remove_push_mmap_entries(struct irdma_qp *iwqp)
{
	if (iwqp->push_db_mmap_entry) {
		rdma_user_mmap_entry_remove(iwqp->push_db_mmap_entry);
		iwqp->push_db_mmap_entry = NULL;
	}
	if (iwqp->push_wqe_mmap_entry) {
		rdma_user_mmap_entry_remove(iwqp->push_wqe_mmap_entry);
		iwqp->push_wqe_mmap_entry = NULL;
	}
}

static int irdma_setup_push_mmap_entries(struct irdma_ucontext *ucontext,
					 struct irdma_qp *iwqp,
					 u64 *push_wqe_mmap_key,
					 u64 *push_db_mmap_key)
{
	struct irdma_device *iwdev = ucontext->iwdev;
	u64 rsvd, bar_off;

	rsvd = IRDMA_PF_BAR_RSVD;
	bar_off = (uintptr_t)iwdev->rf->sc_dev.hw_regs[IRDMA_DB_ADDR_OFFSET];
	/* skip over db page */
	bar_off += IRDMA_HW_PAGE_SIZE;
	/* push wqe page */
	bar_off += rsvd + iwqp->sc_qp.push_idx * IRDMA_HW_PAGE_SIZE;
	iwqp->push_wqe_mmap_entry = irdma_user_mmap_entry_insert(ucontext,
					bar_off, IRDMA_MMAP_IO_WC,
					push_wqe_mmap_key);
	if (!iwqp->push_wqe_mmap_entry)
		return -ENOMEM;

	/* push doorbell page */
	bar_off += IRDMA_HW_PAGE_SIZE;
	iwqp->push_db_mmap_entry = irdma_user_mmap_entry_insert(ucontext,
					bar_off, IRDMA_MMAP_IO_NC,
					push_db_mmap_key);
	if (!iwqp->push_db_mmap_entry) {
		rdma_user_mmap_entry_remove(iwqp->push_wqe_mmap_entry);
		return -ENOMEM;
	}

	return 0;
}

/**
 * irdma_destroy_qp - destroy qp
 * @ibqp: qp's ib pointer also to get to device's qp address
 * @udata: user data
 */
static int irdma_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
{
	struct irdma_qp *iwqp = to_iwqp(ibqp);
	struct irdma_device *iwdev = iwqp->iwdev;

	iwqp->sc_qp.qp_uk.destroy_pending = true;

	if (iwqp->iwarp_state == IRDMA_QP_STATE_RTS)
		irdma_modify_qp_to_err(&iwqp->sc_qp);

	if (!iwqp->user_mode)
		cancel_delayed_work_sync(&iwqp->dwork_flush);

	if (!iwqp->user_mode) {
		if (iwqp->iwscq) {
			irdma_clean_cqes(iwqp, iwqp->iwscq);
			if (iwqp->iwrcq != iwqp->iwscq)
				irdma_clean_cqes(iwqp, iwqp->iwrcq);
		}
	}

	irdma_qp_rem_ref(&iwqp->ibqp);
	wait_for_completion(&iwqp->free_qp);
	irdma_free_lsmm_rsrc(iwqp);
	irdma_cqp_qp_destroy_cmd(&iwdev->rf->sc_dev, &iwqp->sc_qp);

	irdma_remove_push_mmap_entries(iwqp);
	irdma_free_qp_rsrc(iwqp);

	return 0;
}

/**
 * irdma_setup_virt_qp - setup for allocation of virtual qp
 * @iwdev: irdma device
 * @iwqp: qp ptr
 * @init_info: initialize info to return
 */
static void irdma_setup_virt_qp(struct irdma_device *iwdev,
			       struct irdma_qp *iwqp,
			       struct irdma_qp_init_info *init_info)
{
	struct irdma_pbl *iwpbl = iwqp->iwpbl;
	struct irdma_qp_mr *qpmr = &iwpbl->qp_mr;

	iwqp->page = qpmr->sq_page;
	init_info->shadow_area_pa = qpmr->shadow;
	if (iwpbl->pbl_allocated) {
		init_info->virtual_map = true;
		init_info->sq_pa = qpmr->sq_pbl.idx;
		init_info->rq_pa = qpmr->rq_pbl.idx;
	} else {
		init_info->sq_pa = qpmr->sq_pbl.addr;
		init_info->rq_pa = qpmr->rq_pbl.addr;
	}
}

/**
 * irdma_setup_umode_qp - setup sq and rq size in user mode qp
 * @iwdev: iwarp device
 * @iwqp: qp ptr (user or kernel)
 * @info: initialize info to return
 * @init_attr: Initial QP create attributes
 */
static int irdma_setup_umode_qp(struct ib_udata *udata,
				struct irdma_device *iwdev,
				struct irdma_qp *iwqp,
				struct irdma_qp_init_info *info,
				struct ib_qp_init_attr *init_attr)
{
	struct irdma_ucontext *ucontext = rdma_udata_to_drv_context(udata,
				struct irdma_ucontext, ibucontext);
	struct irdma_qp_uk_init_info *ukinfo = &info->qp_uk_init_info;
	struct irdma_create_qp_req req;
	unsigned long flags;
	int ret;

	ret = ib_copy_from_udata(&req, udata,
				 min(sizeof(req), udata->inlen));
	if (ret) {
		ibdev_dbg(&iwdev->ibdev, "VERBS: ib_copy_from_data fail\n");
		return ret;
	}

	iwqp->ctx_info.qp_compl_ctx = req.user_compl_ctx;
	iwqp->user_mode = 1;
	if (req.user_wqe_bufs) {
		info->qp_uk_init_info.legacy_mode = ucontext->legacy_mode;
		spin_lock_irqsave(&ucontext->qp_reg_mem_list_lock, flags);
		iwqp->iwpbl = irdma_get_pbl((unsigned long)req.user_wqe_bufs,
					    &ucontext->qp_reg_mem_list);
		spin_unlock_irqrestore(&ucontext->qp_reg_mem_list_lock, flags);

		if (!iwqp->iwpbl) {
			ret = -ENODATA;
			ibdev_dbg(&iwdev->ibdev, "VERBS: no pbl info\n");
			return ret;
		}
	}

	if (!ucontext->use_raw_attrs) {
		/**
		 * Maintain backward compat with older ABI which passes sq and
		 * rq depth in quanta in cap.max_send_wr and cap.max_recv_wr.
		 * There is no way to compute the correct value of
		 * iwqp->max_send_wr/max_recv_wr in the kernel.
		 */
		iwqp->max_send_wr = init_attr->cap.max_send_wr;
		iwqp->max_recv_wr = init_attr->cap.max_recv_wr;
		ukinfo->sq_size = init_attr->cap.max_send_wr;
		ukinfo->rq_size = init_attr->cap.max_recv_wr;
		irdma_uk_calc_shift_wq(ukinfo, &ukinfo->sq_shift,
				       &ukinfo->rq_shift);
	} else {
		ret = irdma_uk_calc_depth_shift_sq(ukinfo, &ukinfo->sq_depth,
						   &ukinfo->sq_shift);
		if (ret)
			return ret;

		ret = irdma_uk_calc_depth_shift_rq(ukinfo, &ukinfo->rq_depth,
						   &ukinfo->rq_shift);
		if (ret)
			return ret;

		iwqp->max_send_wr =
			(ukinfo->sq_depth - IRDMA_SQ_RSVD) >> ukinfo->sq_shift;
		iwqp->max_recv_wr =
			(ukinfo->rq_depth - IRDMA_RQ_RSVD) >> ukinfo->rq_shift;
		ukinfo->sq_size = ukinfo->sq_depth >> ukinfo->sq_shift;
		ukinfo->rq_size = ukinfo->rq_depth >> ukinfo->rq_shift;
	}

	irdma_setup_virt_qp(iwdev, iwqp, info);

	return 0;
}

/**
 * irdma_setup_kmode_qp - setup initialization for kernel mode qp
 * @iwdev: iwarp device
 * @iwqp: qp ptr (user or kernel)
 * @info: initialize info to return
 * @init_attr: Initial QP create attributes
 */
static int irdma_setup_kmode_qp(struct irdma_device *iwdev,
				struct irdma_qp *iwqp,
				struct irdma_qp_init_info *info,
				struct ib_qp_init_attr *init_attr)
{
	struct irdma_dma_mem *mem = &iwqp->kqp.dma_mem;
	u32 size;
	int status;
	struct irdma_qp_uk_init_info *ukinfo = &info->qp_uk_init_info;

	status = irdma_uk_calc_depth_shift_sq(ukinfo, &ukinfo->sq_depth,
					      &ukinfo->sq_shift);
	if (status)
		return status;

	status = irdma_uk_calc_depth_shift_rq(ukinfo, &ukinfo->rq_depth,
					      &ukinfo->rq_shift);
	if (status)
		return status;

	iwqp->kqp.sq_wrid_mem =
		kcalloc(ukinfo->sq_depth, sizeof(*iwqp->kqp.sq_wrid_mem), GFP_KERNEL);
	if (!iwqp->kqp.sq_wrid_mem)
		return -ENOMEM;

	iwqp->kqp.rq_wrid_mem =
		kcalloc(ukinfo->rq_depth, sizeof(*iwqp->kqp.rq_wrid_mem), GFP_KERNEL);

	if (!iwqp->kqp.rq_wrid_mem) {
		kfree(iwqp->kqp.sq_wrid_mem);
		iwqp->kqp.sq_wrid_mem = NULL;
		return -ENOMEM;
	}

	ukinfo->sq_wrtrk_array = iwqp->kqp.sq_wrid_mem;
	ukinfo->rq_wrid_array = iwqp->kqp.rq_wrid_mem;

	size = (ukinfo->sq_depth + ukinfo->rq_depth) * IRDMA_QP_WQE_MIN_SIZE;
	size += (IRDMA_SHADOW_AREA_SIZE << 3);

	mem->size = ALIGN(size, 256);
	mem->va = dma_alloc_coherent(iwdev->rf->hw.device, mem->size,
				     &mem->pa, GFP_KERNEL);
	if (!mem->va) {
		kfree(iwqp->kqp.sq_wrid_mem);
		iwqp->kqp.sq_wrid_mem = NULL;
		kfree(iwqp->kqp.rq_wrid_mem);
		iwqp->kqp.rq_wrid_mem = NULL;
		return -ENOMEM;
	}

	ukinfo->sq = mem->va;
	info->sq_pa = mem->pa;
	ukinfo->rq = &ukinfo->sq[ukinfo->sq_depth];
	info->rq_pa = info->sq_pa + (ukinfo->sq_depth * IRDMA_QP_WQE_MIN_SIZE);
	ukinfo->shadow_area = ukinfo->rq[ukinfo->rq_depth].elem;
	info->shadow_area_pa =
		info->rq_pa + (ukinfo->rq_depth * IRDMA_QP_WQE_MIN_SIZE);
	ukinfo->sq_size = ukinfo->sq_depth >> ukinfo->sq_shift;
	ukinfo->rq_size = ukinfo->rq_depth >> ukinfo->rq_shift;
	ukinfo->qp_id = iwqp->ibqp.qp_num;

	iwqp->max_send_wr = (ukinfo->sq_depth - IRDMA_SQ_RSVD) >> ukinfo->sq_shift;
	iwqp->max_recv_wr = (ukinfo->rq_depth - IRDMA_RQ_RSVD) >> ukinfo->rq_shift;
	init_attr->cap.max_send_wr = iwqp->max_send_wr;
	init_attr->cap.max_recv_wr = iwqp->max_recv_wr;

	return 0;
}

static int irdma_cqp_create_qp_cmd(struct irdma_qp *iwqp)
{
	struct irdma_pci_f *rf = iwqp->iwdev->rf;
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	struct irdma_create_qp_info *qp_info;
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	qp_info = &cqp_request->info.in.u.qp_create.info;
	memset(qp_info, 0, sizeof(*qp_info));
	qp_info->mac_valid = true;
	qp_info->cq_num_valid = true;
	qp_info->next_iwarp_state = IRDMA_QP_STATE_IDLE;

	cqp_info->cqp_cmd = IRDMA_OP_QP_CREATE;
	cqp_info->post_sq = 1;
	cqp_info->in.u.qp_create.qp = &iwqp->sc_qp;
	cqp_info->in.u.qp_create.scratch = (uintptr_t)cqp_request;
	status = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(&rf->cqp, cqp_request);

	return status;
}

static void irdma_roce_fill_and_set_qpctx_info(struct irdma_qp *iwqp,
					       struct irdma_qp_host_ctx_info *ctx_info)
{
	struct irdma_device *iwdev = iwqp->iwdev;
	struct irdma_sc_dev *dev = &iwdev->rf->sc_dev;
	struct irdma_roce_offload_info *roce_info;
	struct irdma_udp_offload_info *udp_info;

	udp_info = &iwqp->udp_info;
	udp_info->snd_mss = ib_mtu_enum_to_int(ib_mtu_int_to_enum(iwdev->vsi.mtu));
	udp_info->cwnd = iwdev->roce_cwnd;
	udp_info->rexmit_thresh = 2;
	udp_info->rnr_nak_thresh = 2;
	udp_info->src_port = 0xc000;
	udp_info->dst_port = ROCE_V2_UDP_DPORT;
	roce_info = &iwqp->roce_info;
	ether_addr_copy(roce_info->mac_addr, iwdev->netdev->dev_addr);

	roce_info->rd_en = true;
	roce_info->wr_rdresp_en = true;
	roce_info->bind_en = true;
	roce_info->dcqcn_en = false;
	roce_info->rtomin = 5;

	roce_info->ack_credits = iwdev->roce_ackcreds;
	roce_info->ird_size = dev->hw_attrs.max_hw_ird;
	roce_info->ord_size = dev->hw_attrs.max_hw_ord;

	if (!iwqp->user_mode) {
		roce_info->priv_mode_en = true;
		roce_info->fast_reg_en = true;
		roce_info->udprivcq_en = true;
	}
	roce_info->roce_tver = 0;

	ctx_info->roce_info = &iwqp->roce_info;
	ctx_info->udp_info = &iwqp->udp_info;
	irdma_sc_qp_setctx_roce(&iwqp->sc_qp, iwqp->host_ctx.va, ctx_info);
}

static void irdma_iw_fill_and_set_qpctx_info(struct irdma_qp *iwqp,
					     struct irdma_qp_host_ctx_info *ctx_info)
{
	struct irdma_device *iwdev = iwqp->iwdev;
	struct irdma_sc_dev *dev = &iwdev->rf->sc_dev;
	struct irdma_iwarp_offload_info *iwarp_info;

	iwarp_info = &iwqp->iwarp_info;
	ether_addr_copy(iwarp_info->mac_addr, iwdev->netdev->dev_addr);
	iwarp_info->rd_en = true;
	iwarp_info->wr_rdresp_en = true;
	iwarp_info->bind_en = true;
	iwarp_info->ecn_en = true;
	iwarp_info->rtomin = 5;

	if (dev->hw_attrs.uk_attrs.hw_rev >= IRDMA_GEN_2)
		iwarp_info->ib_rd_en = true;
	if (!iwqp->user_mode) {
		iwarp_info->priv_mode_en = true;
		iwarp_info->fast_reg_en = true;
	}
	iwarp_info->ddp_ver = 1;
	iwarp_info->rdmap_ver = 1;

	ctx_info->iwarp_info = &iwqp->iwarp_info;
	ctx_info->iwarp_info_valid = true;
	irdma_sc_qp_setctx(&iwqp->sc_qp, iwqp->host_ctx.va, ctx_info);
	ctx_info->iwarp_info_valid = false;
}

static int irdma_validate_qp_attrs(struct ib_qp_init_attr *init_attr,
				   struct irdma_device *iwdev)
{
	struct irdma_sc_dev *dev = &iwdev->rf->sc_dev;
	struct irdma_uk_attrs *uk_attrs = &dev->hw_attrs.uk_attrs;

	if (init_attr->create_flags)
		return -EOPNOTSUPP;

	if (init_attr->cap.max_inline_data > uk_attrs->max_hw_inline ||
	    init_attr->cap.max_send_sge > uk_attrs->max_hw_wq_frags ||
	    init_attr->cap.max_recv_sge > uk_attrs->max_hw_wq_frags)
		return -EINVAL;

	if (rdma_protocol_roce(&iwdev->ibdev, 1)) {
		if (init_attr->qp_type != IB_QPT_RC &&
		    init_attr->qp_type != IB_QPT_UD &&
		    init_attr->qp_type != IB_QPT_GSI)
			return -EOPNOTSUPP;
	} else {
		if (init_attr->qp_type != IB_QPT_RC)
			return -EOPNOTSUPP;
	}

	return 0;
}

static void irdma_flush_worker(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct irdma_qp *iwqp = container_of(dwork, struct irdma_qp, dwork_flush);

	irdma_generate_flush_completions(iwqp);
}

/**
 * irdma_create_qp - create qp
 * @ibqp: ptr of qp
 * @init_attr: attributes for qp
 * @udata: user data for create qp
 */
static int irdma_create_qp(struct ib_qp *ibqp,
			   struct ib_qp_init_attr *init_attr,
			   struct ib_udata *udata)
{
#define IRDMA_CREATE_QP_MIN_REQ_LEN offsetofend(struct irdma_create_qp_req, user_compl_ctx)
#define IRDMA_CREATE_QP_MIN_RESP_LEN offsetofend(struct irdma_create_qp_resp, rsvd)
	struct ib_pd *ibpd = ibqp->pd;
	struct irdma_pd *iwpd = to_iwpd(ibpd);
	struct irdma_device *iwdev = to_iwdev(ibpd->device);
	struct irdma_pci_f *rf = iwdev->rf;
	struct irdma_qp *iwqp = to_iwqp(ibqp);
	struct irdma_create_qp_resp uresp = {};
	u32 qp_num = 0;
	int err_code;
	struct irdma_sc_qp *qp;
	struct irdma_sc_dev *dev = &rf->sc_dev;
	struct irdma_uk_attrs *uk_attrs = &dev->hw_attrs.uk_attrs;
	struct irdma_qp_init_info init_info = {};
	struct irdma_qp_host_ctx_info *ctx_info;

	err_code = irdma_validate_qp_attrs(init_attr, iwdev);
	if (err_code)
		return err_code;

	if (udata && (udata->inlen < IRDMA_CREATE_QP_MIN_REQ_LEN ||
		      udata->outlen < IRDMA_CREATE_QP_MIN_RESP_LEN))
		return -EINVAL;

	init_info.vsi = &iwdev->vsi;
	init_info.qp_uk_init_info.uk_attrs = uk_attrs;
	init_info.qp_uk_init_info.sq_size = init_attr->cap.max_send_wr;
	init_info.qp_uk_init_info.rq_size = init_attr->cap.max_recv_wr;
	init_info.qp_uk_init_info.max_sq_frag_cnt = init_attr->cap.max_send_sge;
	init_info.qp_uk_init_info.max_rq_frag_cnt = init_attr->cap.max_recv_sge;
	init_info.qp_uk_init_info.max_inline_data = init_attr->cap.max_inline_data;

	qp = &iwqp->sc_qp;
	qp->qp_uk.back_qp = iwqp;
	qp->push_idx = IRDMA_INVALID_PUSH_PAGE_INDEX;

	iwqp->iwdev = iwdev;
	iwqp->q2_ctx_mem.size = ALIGN(IRDMA_Q2_BUF_SIZE + IRDMA_QP_CTX_SIZE,
				      256);
	iwqp->q2_ctx_mem.va = dma_alloc_coherent(dev->hw->device,
						 iwqp->q2_ctx_mem.size,
						 &iwqp->q2_ctx_mem.pa,
						 GFP_KERNEL);
	if (!iwqp->q2_ctx_mem.va)
		return -ENOMEM;

	init_info.q2 = iwqp->q2_ctx_mem.va;
	init_info.q2_pa = iwqp->q2_ctx_mem.pa;
	init_info.host_ctx = (__le64 *)(init_info.q2 + IRDMA_Q2_BUF_SIZE);
	init_info.host_ctx_pa = init_info.q2_pa + IRDMA_Q2_BUF_SIZE;

	if (init_attr->qp_type == IB_QPT_GSI)
		qp_num = 1;
	else
		err_code = irdma_alloc_rsrc(rf, rf->allocated_qps, rf->max_qp,
					    &qp_num, &rf->next_qp);
	if (err_code)
		goto error;

	iwqp->iwpd = iwpd;
	iwqp->ibqp.qp_num = qp_num;
	qp = &iwqp->sc_qp;
	iwqp->iwscq = to_iwcq(init_attr->send_cq);
	iwqp->iwrcq = to_iwcq(init_attr->recv_cq);
	iwqp->host_ctx.va = init_info.host_ctx;
	iwqp->host_ctx.pa = init_info.host_ctx_pa;
	iwqp->host_ctx.size = IRDMA_QP_CTX_SIZE;

	init_info.pd = &iwpd->sc_pd;
	init_info.qp_uk_init_info.qp_id = iwqp->ibqp.qp_num;
	if (!rdma_protocol_roce(&iwdev->ibdev, 1))
		init_info.qp_uk_init_info.first_sq_wq = 1;
	iwqp->ctx_info.qp_compl_ctx = (uintptr_t)qp;
	init_waitqueue_head(&iwqp->waitq);
	init_waitqueue_head(&iwqp->mod_qp_waitq);

	if (udata) {
		init_info.qp_uk_init_info.abi_ver = iwpd->sc_pd.abi_ver;
		err_code = irdma_setup_umode_qp(udata, iwdev, iwqp, &init_info,
						init_attr);
	} else {
		INIT_DELAYED_WORK(&iwqp->dwork_flush, irdma_flush_worker);
		init_info.qp_uk_init_info.abi_ver = IRDMA_ABI_VER;
		err_code = irdma_setup_kmode_qp(iwdev, iwqp, &init_info, init_attr);
	}

	if (err_code) {
		ibdev_dbg(&iwdev->ibdev, "VERBS: setup qp failed\n");
		goto error;
	}

	if (rdma_protocol_roce(&iwdev->ibdev, 1)) {
		if (init_attr->qp_type == IB_QPT_RC) {
			init_info.qp_uk_init_info.type = IRDMA_QP_TYPE_ROCE_RC;
			init_info.qp_uk_init_info.qp_caps = IRDMA_SEND_WITH_IMM |
							    IRDMA_WRITE_WITH_IMM |
							    IRDMA_ROCE;
		} else {
			init_info.qp_uk_init_info.type = IRDMA_QP_TYPE_ROCE_UD;
			init_info.qp_uk_init_info.qp_caps = IRDMA_SEND_WITH_IMM |
							    IRDMA_ROCE;
		}
	} else {
		init_info.qp_uk_init_info.type = IRDMA_QP_TYPE_IWARP;
		init_info.qp_uk_init_info.qp_caps = IRDMA_WRITE_WITH_IMM;
	}

	if (dev->hw_attrs.uk_attrs.hw_rev > IRDMA_GEN_1)
		init_info.qp_uk_init_info.qp_caps |= IRDMA_PUSH_MODE;

	err_code = irdma_sc_qp_init(qp, &init_info);
	if (err_code) {
		ibdev_dbg(&iwdev->ibdev, "VERBS: qp_init fail\n");
		goto error;
	}

	ctx_info = &iwqp->ctx_info;
	ctx_info->send_cq_num = iwqp->iwscq->sc_cq.cq_uk.cq_id;
	ctx_info->rcv_cq_num = iwqp->iwrcq->sc_cq.cq_uk.cq_id;

	if (rdma_protocol_roce(&iwdev->ibdev, 1))
		irdma_roce_fill_and_set_qpctx_info(iwqp, ctx_info);
	else
		irdma_iw_fill_and_set_qpctx_info(iwqp, ctx_info);

	err_code = irdma_cqp_create_qp_cmd(iwqp);
	if (err_code)
		goto error;

	refcount_set(&iwqp->refcnt, 1);
	spin_lock_init(&iwqp->lock);
	spin_lock_init(&iwqp->sc_qp.pfpdu.lock);
	iwqp->sig_all = init_attr->sq_sig_type == IB_SIGNAL_ALL_WR;
	rf->qp_table[qp_num] = iwqp;

	if (rdma_protocol_roce(&iwdev->ibdev, 1)) {
		if (dev->ws_add(&iwdev->vsi, 0)) {
			irdma_cqp_qp_destroy_cmd(&rf->sc_dev, &iwqp->sc_qp);
			err_code = -EINVAL;
			goto error;
		}

		irdma_qp_add_qos(&iwqp->sc_qp);
	}

	if (udata) {
		/* GEN_1 legacy support with libi40iw does not have expanded uresp struct */
		if (udata->outlen < sizeof(uresp)) {
			uresp.lsmm = 1;
			uresp.push_idx = IRDMA_INVALID_PUSH_PAGE_INDEX_GEN_1;
		} else {
			if (rdma_protocol_iwarp(&iwdev->ibdev, 1))
				uresp.lsmm = 1;
		}
		uresp.actual_sq_size = init_info.qp_uk_init_info.sq_size;
		uresp.actual_rq_size = init_info.qp_uk_init_info.rq_size;
		uresp.qp_id = qp_num;
		uresp.qp_caps = qp->qp_uk.qp_caps;

		err_code = ib_copy_to_udata(udata, &uresp,
					    min(sizeof(uresp), udata->outlen));
		if (err_code) {
			ibdev_dbg(&iwdev->ibdev, "VERBS: copy_to_udata failed\n");
			irdma_destroy_qp(&iwqp->ibqp, udata);
			return err_code;
		}
	}

	init_completion(&iwqp->free_qp);
	return 0;

error:
	irdma_free_qp_rsrc(iwqp);
	return err_code;
}

static int irdma_get_ib_acc_flags(struct irdma_qp *iwqp)
{
	int acc_flags = 0;

	if (rdma_protocol_roce(iwqp->ibqp.device, 1)) {
		if (iwqp->roce_info.wr_rdresp_en) {
			acc_flags |= IB_ACCESS_LOCAL_WRITE;
			acc_flags |= IB_ACCESS_REMOTE_WRITE;
		}
		if (iwqp->roce_info.rd_en)
			acc_flags |= IB_ACCESS_REMOTE_READ;
		if (iwqp->roce_info.bind_en)
			acc_flags |= IB_ACCESS_MW_BIND;
	} else {
		if (iwqp->iwarp_info.wr_rdresp_en) {
			acc_flags |= IB_ACCESS_LOCAL_WRITE;
			acc_flags |= IB_ACCESS_REMOTE_WRITE;
		}
		if (iwqp->iwarp_info.rd_en)
			acc_flags |= IB_ACCESS_REMOTE_READ;
		if (iwqp->iwarp_info.bind_en)
			acc_flags |= IB_ACCESS_MW_BIND;
	}
	return acc_flags;
}

/**
 * irdma_query_qp - query qp attributes
 * @ibqp: qp pointer
 * @attr: attributes pointer
 * @attr_mask: Not used
 * @init_attr: qp attributes to return
 */
static int irdma_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
			  int attr_mask, struct ib_qp_init_attr *init_attr)
{
	struct irdma_qp *iwqp = to_iwqp(ibqp);
	struct irdma_sc_qp *qp = &iwqp->sc_qp;

	memset(attr, 0, sizeof(*attr));
	memset(init_attr, 0, sizeof(*init_attr));

	attr->qp_state = iwqp->ibqp_state;
	attr->cur_qp_state = iwqp->ibqp_state;
	attr->cap.max_send_wr = iwqp->max_send_wr;
	attr->cap.max_recv_wr = iwqp->max_recv_wr;
	attr->cap.max_inline_data = qp->qp_uk.max_inline_data;
	attr->cap.max_send_sge = qp->qp_uk.max_sq_frag_cnt;
	attr->cap.max_recv_sge = qp->qp_uk.max_rq_frag_cnt;
	attr->qp_access_flags = irdma_get_ib_acc_flags(iwqp);
	attr->port_num = 1;
	if (rdma_protocol_roce(ibqp->device, 1)) {
		attr->path_mtu = ib_mtu_int_to_enum(iwqp->udp_info.snd_mss);
		attr->qkey = iwqp->roce_info.qkey;
		attr->rq_psn = iwqp->udp_info.epsn;
		attr->sq_psn = iwqp->udp_info.psn_nxt;
		attr->dest_qp_num = iwqp->roce_info.dest_qp;
		attr->pkey_index = iwqp->roce_info.p_key;
		attr->retry_cnt = iwqp->udp_info.rexmit_thresh;
		attr->rnr_retry = iwqp->udp_info.rnr_nak_thresh;
		attr->max_rd_atomic = iwqp->roce_info.ord_size;
		attr->max_dest_rd_atomic = iwqp->roce_info.ird_size;
	}

	init_attr->event_handler = iwqp->ibqp.event_handler;
	init_attr->qp_context = iwqp->ibqp.qp_context;
	init_attr->send_cq = iwqp->ibqp.send_cq;
	init_attr->recv_cq = iwqp->ibqp.recv_cq;
	init_attr->cap = attr->cap;

	return 0;
}

/**
 * irdma_query_pkey - Query partition key
 * @ibdev: device pointer from stack
 * @port: port number
 * @index: index of pkey
 * @pkey: pointer to store the pkey
 */
static int irdma_query_pkey(struct ib_device *ibdev, u32 port, u16 index,
			    u16 *pkey)
{
	if (index >= IRDMA_PKEY_TBL_SZ)
		return -EINVAL;

	*pkey = IRDMA_DEFAULT_PKEY;
	return 0;
}

static u8 irdma_roce_get_vlan_prio(const struct ib_gid_attr *attr, u8 prio)
{
	struct net_device *ndev;

	rcu_read_lock();
	ndev = rcu_dereference(attr->ndev);
	if (!ndev)
		goto exit;
	if (is_vlan_dev(ndev)) {
		u16 vlan_qos = vlan_dev_get_egress_qos_mask(ndev, prio);

		prio = (vlan_qos & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
	}
exit:
	rcu_read_unlock();
	return prio;
}

/**
 * irdma_modify_qp_roce - modify qp request
 * @ibqp: qp's pointer for modify
 * @attr: access attributes
 * @attr_mask: state mask
 * @udata: user data
 */
int irdma_modify_qp_roce(struct ib_qp *ibqp, struct ib_qp_attr *attr,
			 int attr_mask, struct ib_udata *udata)
{
#define IRDMA_MODIFY_QP_MIN_REQ_LEN offsetofend(struct irdma_modify_qp_req, rq_flush)
#define IRDMA_MODIFY_QP_MIN_RESP_LEN offsetofend(struct irdma_modify_qp_resp, push_valid)
	struct irdma_pd *iwpd = to_iwpd(ibqp->pd);
	struct irdma_qp *iwqp = to_iwqp(ibqp);
	struct irdma_device *iwdev = iwqp->iwdev;
	struct irdma_sc_dev *dev = &iwdev->rf->sc_dev;
	struct irdma_qp_host_ctx_info *ctx_info;
	struct irdma_roce_offload_info *roce_info;
	struct irdma_udp_offload_info *udp_info;
	struct irdma_modify_qp_info info = {};
	struct irdma_modify_qp_resp uresp = {};
	struct irdma_modify_qp_req ureq = {};
	unsigned long flags;
	u8 issue_modify_qp = 0;
	int ret = 0;

	ctx_info = &iwqp->ctx_info;
	roce_info = &iwqp->roce_info;
	udp_info = &iwqp->udp_info;

	if (udata) {
		/* udata inlen/outlen can be 0 when supporting legacy libi40iw */
		if ((udata->inlen && udata->inlen < IRDMA_MODIFY_QP_MIN_REQ_LEN) ||
		    (udata->outlen && udata->outlen < IRDMA_MODIFY_QP_MIN_RESP_LEN))
			return -EINVAL;
	}

	if (attr_mask & ~IB_QP_ATTR_STANDARD_BITS)
		return -EOPNOTSUPP;

	if (attr_mask & IB_QP_DEST_QPN)
		roce_info->dest_qp = attr->dest_qp_num;

	if (attr_mask & IB_QP_PKEY_INDEX) {
		ret = irdma_query_pkey(ibqp->device, 0, attr->pkey_index,
				       &roce_info->p_key);
		if (ret)
			return ret;
	}

	if (attr_mask & IB_QP_QKEY)
		roce_info->qkey = attr->qkey;

	if (attr_mask & IB_QP_PATH_MTU)
		udp_info->snd_mss = ib_mtu_enum_to_int(attr->path_mtu);

	if (attr_mask & IB_QP_SQ_PSN) {
		udp_info->psn_nxt = attr->sq_psn;
		udp_info->lsn =  0xffff;
		udp_info->psn_una = attr->sq_psn;
		udp_info->psn_max = attr->sq_psn;
	}

	if (attr_mask & IB_QP_RQ_PSN)
		udp_info->epsn = attr->rq_psn;

	if (attr_mask & IB_QP_RNR_RETRY)
		udp_info->rnr_nak_thresh = attr->rnr_retry;

	if (attr_mask & IB_QP_RETRY_CNT)
		udp_info->rexmit_thresh = attr->retry_cnt;

	ctx_info->roce_info->pd_id = iwpd->sc_pd.pd_id;

	if (attr_mask & IB_QP_AV) {
		struct irdma_av *av = &iwqp->roce_ah.av;
		const struct ib_gid_attr *sgid_attr =
				attr->ah_attr.grh.sgid_attr;
		u16 vlan_id = VLAN_N_VID;
		u32 local_ip[4];

		memset(&iwqp->roce_ah, 0, sizeof(iwqp->roce_ah));
		if (attr->ah_attr.ah_flags & IB_AH_GRH) {
			udp_info->ttl = attr->ah_attr.grh.hop_limit;
			udp_info->flow_label = attr->ah_attr.grh.flow_label;
			udp_info->tos = attr->ah_attr.grh.traffic_class;
			udp_info->src_port =
				rdma_get_udp_sport(udp_info->flow_label,
						   ibqp->qp_num,
						   roce_info->dest_qp);
			irdma_qp_rem_qos(&iwqp->sc_qp);
			dev->ws_remove(iwqp->sc_qp.vsi, ctx_info->user_pri);
			if (iwqp->sc_qp.vsi->dscp_mode)
				ctx_info->user_pri =
				iwqp->sc_qp.vsi->dscp_map[irdma_tos2dscp(udp_info->tos)];
			else
				ctx_info->user_pri = rt_tos2priority(udp_info->tos);
		}
		ret = rdma_read_gid_l2_fields(sgid_attr, &vlan_id,
					      ctx_info->roce_info->mac_addr);
		if (ret)
			return ret;
		ctx_info->user_pri = irdma_roce_get_vlan_prio(sgid_attr,
							      ctx_info->user_pri);
		if (dev->ws_add(iwqp->sc_qp.vsi, ctx_info->user_pri))
			return -ENOMEM;
		iwqp->sc_qp.user_pri = ctx_info->user_pri;
		irdma_qp_add_qos(&iwqp->sc_qp);

		if (vlan_id >= VLAN_N_VID && iwdev->dcb_vlan_mode)
			vlan_id = 0;
		if (vlan_id < VLAN_N_VID) {
			udp_info->insert_vlan_tag = true;
			udp_info->vlan_tag = vlan_id |
				ctx_info->user_pri << VLAN_PRIO_SHIFT;
		} else {
			udp_info->insert_vlan_tag = false;
		}

		av->attrs = attr->ah_attr;
		rdma_gid2ip((struct sockaddr *)&av->sgid_addr, &sgid_attr->gid);
		rdma_gid2ip((struct sockaddr *)&av->dgid_addr, &attr->ah_attr.grh.dgid);
		av->net_type = rdma_gid_attr_network_type(sgid_attr);
		if (av->net_type == RDMA_NETWORK_IPV6) {
			__be32 *daddr =
				av->dgid_addr.saddr_in6.sin6_addr.in6_u.u6_addr32;
			__be32 *saddr =
				av->sgid_addr.saddr_in6.sin6_addr.in6_u.u6_addr32;

			irdma_copy_ip_ntohl(&udp_info->dest_ip_addr[0], daddr);
			irdma_copy_ip_ntohl(&udp_info->local_ipaddr[0], saddr);

			udp_info->ipv4 = false;
			irdma_copy_ip_ntohl(local_ip, daddr);

		} else if (av->net_type == RDMA_NETWORK_IPV4) {
			__be32 saddr = av->sgid_addr.saddr_in.sin_addr.s_addr;
			__be32 daddr = av->dgid_addr.saddr_in.sin_addr.s_addr;

			local_ip[0] = ntohl(daddr);

			udp_info->ipv4 = true;
			udp_info->dest_ip_addr[0] = 0;
			udp_info->dest_ip_addr[1] = 0;
			udp_info->dest_ip_addr[2] = 0;
			udp_info->dest_ip_addr[3] = local_ip[0];

			udp_info->local_ipaddr[0] = 0;
			udp_info->local_ipaddr[1] = 0;
			udp_info->local_ipaddr[2] = 0;
			udp_info->local_ipaddr[3] = ntohl(saddr);
		}
		udp_info->arp_idx =
			irdma_add_arp(iwdev->rf, local_ip, udp_info->ipv4,
				      attr->ah_attr.roce.dmac);
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC) {
		if (attr->max_rd_atomic > dev->hw_attrs.max_hw_ord) {
			ibdev_err(&iwdev->ibdev,
				  "rd_atomic = %d, above max_hw_ord=%d\n",
				  attr->max_rd_atomic,
				  dev->hw_attrs.max_hw_ord);
			return -EINVAL;
		}
		if (attr->max_rd_atomic)
			roce_info->ord_size = attr->max_rd_atomic;
		info.ord_valid = true;
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC) {
		if (attr->max_dest_rd_atomic > dev->hw_attrs.max_hw_ird) {
			ibdev_err(&iwdev->ibdev,
				  "rd_atomic = %d, above max_hw_ird=%d\n",
				   attr->max_rd_atomic,
				   dev->hw_attrs.max_hw_ird);
			return -EINVAL;
		}
		if (attr->max_dest_rd_atomic)
			roce_info->ird_size = attr->max_dest_rd_atomic;
	}

	if (attr_mask & IB_QP_ACCESS_FLAGS) {
		if (attr->qp_access_flags & IB_ACCESS_LOCAL_WRITE)
			roce_info->wr_rdresp_en = true;
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_WRITE)
			roce_info->wr_rdresp_en = true;
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_READ)
			roce_info->rd_en = true;
	}

	wait_event(iwqp->mod_qp_waitq, !atomic_read(&iwqp->hw_mod_qp_pend));

	ibdev_dbg(&iwdev->ibdev,
		  "VERBS: caller: %pS qp_id=%d to_ibqpstate=%d ibqpstate=%d irdma_qpstate=%d attr_mask=0x%x\n",
		  __builtin_return_address(0), ibqp->qp_num, attr->qp_state,
		  iwqp->ibqp_state, iwqp->iwarp_state, attr_mask);

	spin_lock_irqsave(&iwqp->lock, flags);
	if (attr_mask & IB_QP_STATE) {
		if (!ib_modify_qp_is_ok(iwqp->ibqp_state, attr->qp_state,
					iwqp->ibqp.qp_type, attr_mask)) {
			ibdev_warn(&iwdev->ibdev, "modify_qp invalid for qp_id=%d, old_state=0x%x, new_state=0x%x\n",
				   iwqp->ibqp.qp_num, iwqp->ibqp_state,
				   attr->qp_state);
			ret = -EINVAL;
			goto exit;
		}
		info.curr_iwarp_state = iwqp->iwarp_state;

		switch (attr->qp_state) {
		case IB_QPS_INIT:
			if (iwqp->iwarp_state > IRDMA_QP_STATE_IDLE) {
				ret = -EINVAL;
				goto exit;
			}

			if (iwqp->iwarp_state == IRDMA_QP_STATE_INVALID) {
				info.next_iwarp_state = IRDMA_QP_STATE_IDLE;
				issue_modify_qp = 1;
			}
			break;
		case IB_QPS_RTR:
			if (iwqp->iwarp_state > IRDMA_QP_STATE_IDLE) {
				ret = -EINVAL;
				goto exit;
			}
			info.arp_cache_idx_valid = true;
			info.cq_num_valid = true;
			info.next_iwarp_state = IRDMA_QP_STATE_RTR;
			issue_modify_qp = 1;
			break;
		case IB_QPS_RTS:
			if (iwqp->ibqp_state < IB_QPS_RTR ||
			    iwqp->ibqp_state == IB_QPS_ERR) {
				ret = -EINVAL;
				goto exit;
			}

			info.arp_cache_idx_valid = true;
			info.cq_num_valid = true;
			info.ord_valid = true;
			info.next_iwarp_state = IRDMA_QP_STATE_RTS;
			issue_modify_qp = 1;
			if (iwdev->push_mode && udata &&
			    iwqp->sc_qp.push_idx == IRDMA_INVALID_PUSH_PAGE_INDEX &&
			    dev->hw_attrs.uk_attrs.hw_rev >= IRDMA_GEN_2) {
				spin_unlock_irqrestore(&iwqp->lock, flags);
				irdma_alloc_push_page(iwqp);
				spin_lock_irqsave(&iwqp->lock, flags);
			}
			break;
		case IB_QPS_SQD:
			if (iwqp->iwarp_state == IRDMA_QP_STATE_SQD)
				goto exit;

			if (iwqp->iwarp_state != IRDMA_QP_STATE_RTS) {
				ret = -EINVAL;
				goto exit;
			}

			info.next_iwarp_state = IRDMA_QP_STATE_SQD;
			issue_modify_qp = 1;
			break;
		case IB_QPS_SQE:
		case IB_QPS_ERR:
		case IB_QPS_RESET:
			if (iwqp->iwarp_state == IRDMA_QP_STATE_RTS) {
				spin_unlock_irqrestore(&iwqp->lock, flags);
				info.next_iwarp_state = IRDMA_QP_STATE_SQD;
				irdma_hw_modify_qp(iwdev, iwqp, &info, true);
				spin_lock_irqsave(&iwqp->lock, flags);
			}

			if (iwqp->iwarp_state == IRDMA_QP_STATE_ERROR) {
				spin_unlock_irqrestore(&iwqp->lock, flags);
				if (udata && udata->inlen) {
					if (ib_copy_from_udata(&ureq, udata,
					    min(sizeof(ureq), udata->inlen)))
						return -EINVAL;

					irdma_flush_wqes(iwqp,
					    (ureq.sq_flush ? IRDMA_FLUSH_SQ : 0) |
					    (ureq.rq_flush ? IRDMA_FLUSH_RQ : 0) |
					    IRDMA_REFLUSH);
				}
				return 0;
			}

			info.next_iwarp_state = IRDMA_QP_STATE_ERROR;
			issue_modify_qp = 1;
			break;
		default:
			ret = -EINVAL;
			goto exit;
		}

		iwqp->ibqp_state = attr->qp_state;
	}

	ctx_info->send_cq_num = iwqp->iwscq->sc_cq.cq_uk.cq_id;
	ctx_info->rcv_cq_num = iwqp->iwrcq->sc_cq.cq_uk.cq_id;
	irdma_sc_qp_setctx_roce(&iwqp->sc_qp, iwqp->host_ctx.va, ctx_info);
	spin_unlock_irqrestore(&iwqp->lock, flags);

	if (attr_mask & IB_QP_STATE) {
		if (issue_modify_qp) {
			ctx_info->rem_endpoint_idx = udp_info->arp_idx;
			if (irdma_hw_modify_qp(iwdev, iwqp, &info, true))
				return -EINVAL;
			spin_lock_irqsave(&iwqp->lock, flags);
			if (iwqp->iwarp_state == info.curr_iwarp_state) {
				iwqp->iwarp_state = info.next_iwarp_state;
				iwqp->ibqp_state = attr->qp_state;
			}
			if (iwqp->ibqp_state > IB_QPS_RTS &&
			    !iwqp->flush_issued) {
				spin_unlock_irqrestore(&iwqp->lock, flags);
				irdma_flush_wqes(iwqp, IRDMA_FLUSH_SQ |
						       IRDMA_FLUSH_RQ |
						       IRDMA_FLUSH_WAIT);
				iwqp->flush_issued = 1;
			} else {
				spin_unlock_irqrestore(&iwqp->lock, flags);
			}
		} else {
			iwqp->ibqp_state = attr->qp_state;
		}
		if (udata && udata->outlen && dev->hw_attrs.uk_attrs.hw_rev >= IRDMA_GEN_2) {
			struct irdma_ucontext *ucontext;

			ucontext = rdma_udata_to_drv_context(udata,
					struct irdma_ucontext, ibucontext);
			if (iwqp->sc_qp.push_idx != IRDMA_INVALID_PUSH_PAGE_INDEX &&
			    !iwqp->push_wqe_mmap_entry &&
			    !irdma_setup_push_mmap_entries(ucontext, iwqp,
				&uresp.push_wqe_mmap_key, &uresp.push_db_mmap_key)) {
				uresp.push_valid = 1;
				uresp.push_offset = iwqp->sc_qp.push_offset;
			}
			ret = ib_copy_to_udata(udata, &uresp, min(sizeof(uresp),
					       udata->outlen));
			if (ret) {
				irdma_remove_push_mmap_entries(iwqp);
				ibdev_dbg(&iwdev->ibdev,
					  "VERBS: copy_to_udata failed\n");
				return ret;
			}
		}
	}

	return 0;
exit:
	spin_unlock_irqrestore(&iwqp->lock, flags);

	return ret;
}

/**
 * irdma_modify_qp - modify qp request
 * @ibqp: qp's pointer for modify
 * @attr: access attributes
 * @attr_mask: state mask
 * @udata: user data
 */
int irdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int attr_mask,
		    struct ib_udata *udata)
{
#define IRDMA_MODIFY_QP_MIN_REQ_LEN offsetofend(struct irdma_modify_qp_req, rq_flush)
#define IRDMA_MODIFY_QP_MIN_RESP_LEN offsetofend(struct irdma_modify_qp_resp, push_valid)
	struct irdma_qp *iwqp = to_iwqp(ibqp);
	struct irdma_device *iwdev = iwqp->iwdev;
	struct irdma_sc_dev *dev = &iwdev->rf->sc_dev;
	struct irdma_qp_host_ctx_info *ctx_info;
	struct irdma_tcp_offload_info *tcp_info;
	struct irdma_iwarp_offload_info *offload_info;
	struct irdma_modify_qp_info info = {};
	struct irdma_modify_qp_resp uresp = {};
	struct irdma_modify_qp_req ureq = {};
	u8 issue_modify_qp = 0;
	u8 dont_wait = 0;
	int err;
	unsigned long flags;

	if (udata) {
		/* udata inlen/outlen can be 0 when supporting legacy libi40iw */
		if ((udata->inlen && udata->inlen < IRDMA_MODIFY_QP_MIN_REQ_LEN) ||
		    (udata->outlen && udata->outlen < IRDMA_MODIFY_QP_MIN_RESP_LEN))
			return -EINVAL;
	}

	if (attr_mask & ~IB_QP_ATTR_STANDARD_BITS)
		return -EOPNOTSUPP;

	ctx_info = &iwqp->ctx_info;
	offload_info = &iwqp->iwarp_info;
	tcp_info = &iwqp->tcp_info;
	wait_event(iwqp->mod_qp_waitq, !atomic_read(&iwqp->hw_mod_qp_pend));
	ibdev_dbg(&iwdev->ibdev,
		  "VERBS: caller: %pS qp_id=%d to_ibqpstate=%d ibqpstate=%d irdma_qpstate=%d last_aeq=%d hw_tcp_state=%d hw_iwarp_state=%d attr_mask=0x%x\n",
		  __builtin_return_address(0), ibqp->qp_num, attr->qp_state,
		  iwqp->ibqp_state, iwqp->iwarp_state, iwqp->last_aeq,
		  iwqp->hw_tcp_state, iwqp->hw_iwarp_state, attr_mask);

	spin_lock_irqsave(&iwqp->lock, flags);
	if (attr_mask & IB_QP_STATE) {
		info.curr_iwarp_state = iwqp->iwarp_state;
		switch (attr->qp_state) {
		case IB_QPS_INIT:
		case IB_QPS_RTR:
			if (iwqp->iwarp_state > IRDMA_QP_STATE_IDLE) {
				err = -EINVAL;
				goto exit;
			}

			if (iwqp->iwarp_state == IRDMA_QP_STATE_INVALID) {
				info.next_iwarp_state = IRDMA_QP_STATE_IDLE;
				issue_modify_qp = 1;
			}
			if (iwdev->push_mode && udata &&
			    iwqp->sc_qp.push_idx == IRDMA_INVALID_PUSH_PAGE_INDEX &&
			    dev->hw_attrs.uk_attrs.hw_rev >= IRDMA_GEN_2) {
				spin_unlock_irqrestore(&iwqp->lock, flags);
				irdma_alloc_push_page(iwqp);
				spin_lock_irqsave(&iwqp->lock, flags);
			}
			break;
		case IB_QPS_RTS:
			if (iwqp->iwarp_state > IRDMA_QP_STATE_RTS ||
			    !iwqp->cm_id) {
				err = -EINVAL;
				goto exit;
			}

			issue_modify_qp = 1;
			iwqp->hw_tcp_state = IRDMA_TCP_STATE_ESTABLISHED;
			iwqp->hte_added = 1;
			info.next_iwarp_state = IRDMA_QP_STATE_RTS;
			info.tcp_ctx_valid = true;
			info.ord_valid = true;
			info.arp_cache_idx_valid = true;
			info.cq_num_valid = true;
			break;
		case IB_QPS_SQD:
			if (iwqp->hw_iwarp_state > IRDMA_QP_STATE_RTS) {
				err = 0;
				goto exit;
			}

			if (iwqp->iwarp_state == IRDMA_QP_STATE_CLOSING ||
			    iwqp->iwarp_state < IRDMA_QP_STATE_RTS) {
				err = 0;
				goto exit;
			}

			if (iwqp->iwarp_state > IRDMA_QP_STATE_CLOSING) {
				err = -EINVAL;
				goto exit;
			}

			info.next_iwarp_state = IRDMA_QP_STATE_CLOSING;
			issue_modify_qp = 1;
			break;
		case IB_QPS_SQE:
			if (iwqp->iwarp_state >= IRDMA_QP_STATE_TERMINATE) {
				err = -EINVAL;
				goto exit;
			}

			info.next_iwarp_state = IRDMA_QP_STATE_TERMINATE;
			issue_modify_qp = 1;
			break;
		case IB_QPS_ERR:
		case IB_QPS_RESET:
			if (iwqp->iwarp_state == IRDMA_QP_STATE_ERROR) {
				spin_unlock_irqrestore(&iwqp->lock, flags);
				if (udata && udata->inlen) {
					if (ib_copy_from_udata(&ureq, udata,
					    min(sizeof(ureq), udata->inlen)))
						return -EINVAL;

					irdma_flush_wqes(iwqp,
					    (ureq.sq_flush ? IRDMA_FLUSH_SQ : 0) |
					    (ureq.rq_flush ? IRDMA_FLUSH_RQ : 0) |
					    IRDMA_REFLUSH);
				}
				return 0;
			}

			if (iwqp->sc_qp.term_flags) {
				spin_unlock_irqrestore(&iwqp->lock, flags);
				irdma_terminate_del_timer(&iwqp->sc_qp);
				spin_lock_irqsave(&iwqp->lock, flags);
			}
			info.next_iwarp_state = IRDMA_QP_STATE_ERROR;
			if (iwqp->hw_tcp_state > IRDMA_TCP_STATE_CLOSED &&
			    iwdev->iw_status &&
			    iwqp->hw_tcp_state != IRDMA_TCP_STATE_TIME_WAIT)
				info.reset_tcp_conn = true;
			else
				dont_wait = 1;

			issue_modify_qp = 1;
			info.next_iwarp_state = IRDMA_QP_STATE_ERROR;
			break;
		default:
			err = -EINVAL;
			goto exit;
		}

		iwqp->ibqp_state = attr->qp_state;
	}
	if (attr_mask & IB_QP_ACCESS_FLAGS) {
		ctx_info->iwarp_info_valid = true;
		if (attr->qp_access_flags & IB_ACCESS_LOCAL_WRITE)
			offload_info->wr_rdresp_en = true;
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_WRITE)
			offload_info->wr_rdresp_en = true;
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_READ)
			offload_info->rd_en = true;
	}

	if (ctx_info->iwarp_info_valid) {
		ctx_info->send_cq_num = iwqp->iwscq->sc_cq.cq_uk.cq_id;
		ctx_info->rcv_cq_num = iwqp->iwrcq->sc_cq.cq_uk.cq_id;
		irdma_sc_qp_setctx(&iwqp->sc_qp, iwqp->host_ctx.va, ctx_info);
	}
	spin_unlock_irqrestore(&iwqp->lock, flags);

	if (attr_mask & IB_QP_STATE) {
		if (issue_modify_qp) {
			ctx_info->rem_endpoint_idx = tcp_info->arp_idx;
			if (irdma_hw_modify_qp(iwdev, iwqp, &info, true))
				return -EINVAL;
		}

		spin_lock_irqsave(&iwqp->lock, flags);
		if (iwqp->iwarp_state == info.curr_iwarp_state) {
			iwqp->iwarp_state = info.next_iwarp_state;
			iwqp->ibqp_state = attr->qp_state;
		}
		spin_unlock_irqrestore(&iwqp->lock, flags);
	}

	if (issue_modify_qp && iwqp->ibqp_state > IB_QPS_RTS) {
		if (dont_wait) {
			if (iwqp->hw_tcp_state) {
				spin_lock_irqsave(&iwqp->lock, flags);
				iwqp->hw_tcp_state = IRDMA_TCP_STATE_CLOSED;
				iwqp->last_aeq = IRDMA_AE_RESET_SENT;
				spin_unlock_irqrestore(&iwqp->lock, flags);
			}
			irdma_cm_disconn(iwqp);
		} else {
			int close_timer_started;

			spin_lock_irqsave(&iwdev->cm_core.ht_lock, flags);

			if (iwqp->cm_node) {
				refcount_inc(&iwqp->cm_node->refcnt);
				spin_unlock_irqrestore(&iwdev->cm_core.ht_lock, flags);
				close_timer_started = atomic_inc_return(&iwqp->close_timer_started);
				if (iwqp->cm_id && close_timer_started == 1)
					irdma_schedule_cm_timer(iwqp->cm_node,
						(struct irdma_puda_buf *)iwqp,
						IRDMA_TIMER_TYPE_CLOSE, 1, 0);

				irdma_rem_ref_cm_node(iwqp->cm_node);
			} else {
				spin_unlock_irqrestore(&iwdev->cm_core.ht_lock, flags);
			}
		}
	}
	if (attr_mask & IB_QP_STATE && udata && udata->outlen &&
	    dev->hw_attrs.uk_attrs.hw_rev >= IRDMA_GEN_2) {
		struct irdma_ucontext *ucontext;

		ucontext = rdma_udata_to_drv_context(udata,
					struct irdma_ucontext, ibucontext);
		if (iwqp->sc_qp.push_idx != IRDMA_INVALID_PUSH_PAGE_INDEX &&
		    !iwqp->push_wqe_mmap_entry &&
		    !irdma_setup_push_mmap_entries(ucontext, iwqp,
			&uresp.push_wqe_mmap_key, &uresp.push_db_mmap_key)) {
			uresp.push_valid = 1;
			uresp.push_offset = iwqp->sc_qp.push_offset;
		}

		err = ib_copy_to_udata(udata, &uresp, min(sizeof(uresp),
				       udata->outlen));
		if (err) {
			irdma_remove_push_mmap_entries(iwqp);
			ibdev_dbg(&iwdev->ibdev,
				  "VERBS: copy_to_udata failed\n");
			return err;
		}
	}

	return 0;
exit:
	spin_unlock_irqrestore(&iwqp->lock, flags);

	return err;
}

/**
 * irdma_cq_free_rsrc - free up resources for cq
 * @rf: RDMA PCI function
 * @iwcq: cq ptr
 */
static void irdma_cq_free_rsrc(struct irdma_pci_f *rf, struct irdma_cq *iwcq)
{
	struct irdma_sc_cq *cq = &iwcq->sc_cq;

	if (!iwcq->user_mode) {
		dma_free_coherent(rf->sc_dev.hw->device, iwcq->kmem.size,
				  iwcq->kmem.va, iwcq->kmem.pa);
		iwcq->kmem.va = NULL;
		dma_free_coherent(rf->sc_dev.hw->device,
				  iwcq->kmem_shadow.size,
				  iwcq->kmem_shadow.va, iwcq->kmem_shadow.pa);
		iwcq->kmem_shadow.va = NULL;
	}

	irdma_free_rsrc(rf, rf->allocated_cqs, cq->cq_uk.cq_id);
}

/**
 * irdma_free_cqbuf - worker to free a cq buffer
 * @work: provides access to the cq buffer to free
 */
static void irdma_free_cqbuf(struct work_struct *work)
{
	struct irdma_cq_buf *cq_buf = container_of(work, struct irdma_cq_buf, work);

	dma_free_coherent(cq_buf->hw->device, cq_buf->kmem_buf.size,
			  cq_buf->kmem_buf.va, cq_buf->kmem_buf.pa);
	cq_buf->kmem_buf.va = NULL;
	kfree(cq_buf);
}

/**
 * irdma_process_resize_list - remove resized cq buffers from the resize_list
 * @iwcq: cq which owns the resize_list
 * @iwdev: irdma device
 * @lcqe_buf: the buffer where the last cqe is received
 */
static int irdma_process_resize_list(struct irdma_cq *iwcq,
				     struct irdma_device *iwdev,
				     struct irdma_cq_buf *lcqe_buf)
{
	struct list_head *tmp_node, *list_node;
	struct irdma_cq_buf *cq_buf;
	int cnt = 0;

	list_for_each_safe(list_node, tmp_node, &iwcq->resize_list) {
		cq_buf = list_entry(list_node, struct irdma_cq_buf, list);
		if (cq_buf == lcqe_buf)
			return cnt;

		list_del(&cq_buf->list);
		queue_work(iwdev->cleanup_wq, &cq_buf->work);
		cnt++;
	}

	return cnt;
}

/**
 * irdma_destroy_cq - destroy cq
 * @ib_cq: cq pointer
 * @udata: user data
 */
static int irdma_destroy_cq(struct ib_cq *ib_cq, struct ib_udata *udata)
{
	struct irdma_device *iwdev = to_iwdev(ib_cq->device);
	struct irdma_cq *iwcq = to_iwcq(ib_cq);
	struct irdma_sc_cq *cq = &iwcq->sc_cq;
	struct irdma_sc_dev *dev = cq->dev;
	struct irdma_sc_ceq *ceq = dev->ceq[cq->ceq_id];
	struct irdma_ceq *iwceq = container_of(ceq, struct irdma_ceq, sc_ceq);
	unsigned long flags;

	spin_lock_irqsave(&iwcq->lock, flags);
	if (!list_empty(&iwcq->cmpl_generated))
		irdma_remove_cmpls_list(iwcq);
	if (!list_empty(&iwcq->resize_list))
		irdma_process_resize_list(iwcq, iwdev, NULL);
	spin_unlock_irqrestore(&iwcq->lock, flags);

	irdma_cq_rem_ref(ib_cq);
	wait_for_completion(&iwcq->free_cq);

	irdma_cq_wq_destroy(iwdev->rf, cq);

	spin_lock_irqsave(&iwceq->ce_lock, flags);
	irdma_sc_cleanup_ceqes(cq, ceq);
	spin_unlock_irqrestore(&iwceq->ce_lock, flags);
	irdma_cq_free_rsrc(iwdev->rf, iwcq);

	return 0;
}

/**
 * irdma_resize_cq - resize cq
 * @ibcq: cq to be resized
 * @entries: desired cq size
 * @udata: user data
 */
static int irdma_resize_cq(struct ib_cq *ibcq, int entries,
			   struct ib_udata *udata)
{
#define IRDMA_RESIZE_CQ_MIN_REQ_LEN offsetofend(struct irdma_resize_cq_req, user_cq_buffer)
	struct irdma_cq *iwcq = to_iwcq(ibcq);
	struct irdma_sc_dev *dev = iwcq->sc_cq.dev;
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	struct irdma_modify_cq_info *m_info;
	struct irdma_modify_cq_info info = {};
	struct irdma_dma_mem kmem_buf;
	struct irdma_cq_mr *cqmr_buf;
	struct irdma_pbl *iwpbl_buf;
	struct irdma_device *iwdev;
	struct irdma_pci_f *rf;
	struct irdma_cq_buf *cq_buf = NULL;
	unsigned long flags;
	int ret;

	iwdev = to_iwdev(ibcq->device);
	rf = iwdev->rf;

	if (!(rf->sc_dev.hw_attrs.uk_attrs.feature_flags &
	    IRDMA_FEATURE_CQ_RESIZE))
		return -EOPNOTSUPP;

	if (udata && udata->inlen < IRDMA_RESIZE_CQ_MIN_REQ_LEN)
		return -EINVAL;

	if (entries > rf->max_cqe)
		return -EINVAL;

	if (!iwcq->user_mode) {
		entries++;
		if (rf->sc_dev.hw_attrs.uk_attrs.hw_rev >= IRDMA_GEN_2)
			entries *= 2;
	}

	info.cq_size = max(entries, 4);

	if (info.cq_size == iwcq->sc_cq.cq_uk.cq_size - 1)
		return 0;

	if (udata) {
		struct irdma_resize_cq_req req = {};
		struct irdma_ucontext *ucontext =
			rdma_udata_to_drv_context(udata, struct irdma_ucontext,
						  ibucontext);

		/* CQ resize not supported with legacy GEN_1 libi40iw */
		if (ucontext->legacy_mode)
			return -EOPNOTSUPP;

		if (ib_copy_from_udata(&req, udata,
				       min(sizeof(req), udata->inlen)))
			return -EINVAL;

		spin_lock_irqsave(&ucontext->cq_reg_mem_list_lock, flags);
		iwpbl_buf = irdma_get_pbl((unsigned long)req.user_cq_buffer,
					  &ucontext->cq_reg_mem_list);
		spin_unlock_irqrestore(&ucontext->cq_reg_mem_list_lock, flags);

		if (!iwpbl_buf)
			return -ENOMEM;

		cqmr_buf = &iwpbl_buf->cq_mr;
		if (iwpbl_buf->pbl_allocated) {
			info.virtual_map = true;
			info.pbl_chunk_size = 1;
			info.first_pm_pbl_idx = cqmr_buf->cq_pbl.idx;
		} else {
			info.cq_pa = cqmr_buf->cq_pbl.addr;
		}
	} else {
		/* Kmode CQ resize */
		int rsize;

		rsize = info.cq_size * sizeof(struct irdma_cqe);
		kmem_buf.size = ALIGN(round_up(rsize, 256), 256);
		kmem_buf.va = dma_alloc_coherent(dev->hw->device,
						 kmem_buf.size, &kmem_buf.pa,
						 GFP_KERNEL);
		if (!kmem_buf.va)
			return -ENOMEM;

		info.cq_base = kmem_buf.va;
		info.cq_pa = kmem_buf.pa;
		cq_buf = kzalloc(sizeof(*cq_buf), GFP_KERNEL);
		if (!cq_buf) {
			ret = -ENOMEM;
			goto error;
		}
	}

	cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp, true);
	if (!cqp_request) {
		ret = -ENOMEM;
		goto error;
	}

	info.shadow_read_threshold = iwcq->sc_cq.shadow_read_threshold;
	info.cq_resize = true;

	cqp_info = &cqp_request->info;
	m_info = &cqp_info->in.u.cq_modify.info;
	memcpy(m_info, &info, sizeof(*m_info));

	cqp_info->cqp_cmd = IRDMA_OP_CQ_MODIFY;
	cqp_info->in.u.cq_modify.cq = &iwcq->sc_cq;
	cqp_info->in.u.cq_modify.scratch = (uintptr_t)cqp_request;
	cqp_info->post_sq = 1;
	ret = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(&rf->cqp, cqp_request);
	if (ret)
		goto error;

	spin_lock_irqsave(&iwcq->lock, flags);
	if (cq_buf) {
		cq_buf->kmem_buf = iwcq->kmem;
		cq_buf->hw = dev->hw;
		memcpy(&cq_buf->cq_uk, &iwcq->sc_cq.cq_uk, sizeof(cq_buf->cq_uk));
		INIT_WORK(&cq_buf->work, irdma_free_cqbuf);
		list_add_tail(&cq_buf->list, &iwcq->resize_list);
		iwcq->kmem = kmem_buf;
	}

	irdma_sc_cq_resize(&iwcq->sc_cq, &info);
	ibcq->cqe = info.cq_size - 1;
	spin_unlock_irqrestore(&iwcq->lock, flags);

	return 0;
error:
	if (!udata) {
		dma_free_coherent(dev->hw->device, kmem_buf.size, kmem_buf.va,
				  kmem_buf.pa);
		kmem_buf.va = NULL;
	}
	kfree(cq_buf);

	return ret;
}

static inline int cq_validate_flags(u32 flags, u8 hw_rev)
{
	/* GEN1 does not support CQ create flags */
	if (hw_rev == IRDMA_GEN_1)
		return flags ? -EOPNOTSUPP : 0;

	return flags & ~IB_UVERBS_CQ_FLAGS_TIMESTAMP_COMPLETION ? -EOPNOTSUPP : 0;
}

/**
 * irdma_create_cq - create cq
 * @ibcq: CQ allocated
 * @attr: attributes for cq
 * @udata: user data
 */
static int irdma_create_cq(struct ib_cq *ibcq,
			   const struct ib_cq_init_attr *attr,
			   struct ib_udata *udata)
{
#define IRDMA_CREATE_CQ_MIN_REQ_LEN offsetofend(struct irdma_create_cq_req, user_cq_buf)
#define IRDMA_CREATE_CQ_MIN_RESP_LEN offsetofend(struct irdma_create_cq_resp, cq_size)
	struct ib_device *ibdev = ibcq->device;
	struct irdma_device *iwdev = to_iwdev(ibdev);
	struct irdma_pci_f *rf = iwdev->rf;
	struct irdma_cq *iwcq = to_iwcq(ibcq);
	u32 cq_num = 0;
	struct irdma_sc_cq *cq;
	struct irdma_sc_dev *dev = &rf->sc_dev;
	struct irdma_cq_init_info info = {};
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	struct irdma_cq_uk_init_info *ukinfo = &info.cq_uk_init_info;
	unsigned long flags;
	int err_code;
	int entries = attr->cqe;

	err_code = cq_validate_flags(attr->flags, dev->hw_attrs.uk_attrs.hw_rev);
	if (err_code)
		return err_code;

	if (udata && (udata->inlen < IRDMA_CREATE_CQ_MIN_REQ_LEN ||
		      udata->outlen < IRDMA_CREATE_CQ_MIN_RESP_LEN))
		return -EINVAL;

	err_code = irdma_alloc_rsrc(rf, rf->allocated_cqs, rf->max_cq, &cq_num,
				    &rf->next_cq);
	if (err_code)
		return err_code;

	cq = &iwcq->sc_cq;
	cq->back_cq = iwcq;
	refcount_set(&iwcq->refcnt, 1);
	spin_lock_init(&iwcq->lock);
	INIT_LIST_HEAD(&iwcq->resize_list);
	INIT_LIST_HEAD(&iwcq->cmpl_generated);
	info.dev = dev;
	ukinfo->cq_size = max(entries, 4);
	ukinfo->cq_id = cq_num;
	iwcq->ibcq.cqe = info.cq_uk_init_info.cq_size;
	if (attr->comp_vector < rf->ceqs_count)
		info.ceq_id = attr->comp_vector;
	info.ceq_id_valid = true;
	info.ceqe_mask = 1;
	info.type = IRDMA_CQ_TYPE_IWARP;
	info.vsi = &iwdev->vsi;

	if (udata) {
		struct irdma_ucontext *ucontext;
		struct irdma_create_cq_req req = {};
		struct irdma_cq_mr *cqmr;
		struct irdma_pbl *iwpbl;
		struct irdma_pbl *iwpbl_shadow;
		struct irdma_cq_mr *cqmr_shadow;

		iwcq->user_mode = true;
		ucontext =
			rdma_udata_to_drv_context(udata, struct irdma_ucontext,
						  ibucontext);
		if (ib_copy_from_udata(&req, udata,
				       min(sizeof(req), udata->inlen))) {
			err_code = -EFAULT;
			goto cq_free_rsrc;
		}

		spin_lock_irqsave(&ucontext->cq_reg_mem_list_lock, flags);
		iwpbl = irdma_get_pbl((unsigned long)req.user_cq_buf,
				      &ucontext->cq_reg_mem_list);
		spin_unlock_irqrestore(&ucontext->cq_reg_mem_list_lock, flags);
		if (!iwpbl) {
			err_code = -EPROTO;
			goto cq_free_rsrc;
		}

		iwcq->iwpbl = iwpbl;
		iwcq->cq_mem_size = 0;
		cqmr = &iwpbl->cq_mr;

		if (rf->sc_dev.hw_attrs.uk_attrs.feature_flags &
		    IRDMA_FEATURE_CQ_RESIZE && !ucontext->legacy_mode) {
			spin_lock_irqsave(&ucontext->cq_reg_mem_list_lock, flags);
			iwpbl_shadow = irdma_get_pbl(
					(unsigned long)req.user_shadow_area,
					&ucontext->cq_reg_mem_list);
			spin_unlock_irqrestore(&ucontext->cq_reg_mem_list_lock, flags);

			if (!iwpbl_shadow) {
				err_code = -EPROTO;
				goto cq_free_rsrc;
			}
			iwcq->iwpbl_shadow = iwpbl_shadow;
			cqmr_shadow = &iwpbl_shadow->cq_mr;
			info.shadow_area_pa = cqmr_shadow->cq_pbl.addr;
			cqmr->split = true;
		} else {
			info.shadow_area_pa = cqmr->shadow;
		}
		if (iwpbl->pbl_allocated) {
			info.virtual_map = true;
			info.pbl_chunk_size = 1;
			info.first_pm_pbl_idx = cqmr->cq_pbl.idx;
		} else {
			info.cq_base_pa = cqmr->cq_pbl.addr;
		}
	} else {
		/* Kmode allocations */
		int rsize;

		if (entries < 1 || entries > rf->max_cqe) {
			err_code = -EINVAL;
			goto cq_free_rsrc;
		}

		entries++;
		if (dev->hw_attrs.uk_attrs.hw_rev >= IRDMA_GEN_2)
			entries *= 2;
		ukinfo->cq_size = entries;

		rsize = info.cq_uk_init_info.cq_size * sizeof(struct irdma_cqe);
		iwcq->kmem.size = ALIGN(round_up(rsize, 256), 256);
		iwcq->kmem.va = dma_alloc_coherent(dev->hw->device,
						   iwcq->kmem.size,
						   &iwcq->kmem.pa, GFP_KERNEL);
		if (!iwcq->kmem.va) {
			err_code = -ENOMEM;
			goto cq_free_rsrc;
		}

		iwcq->kmem_shadow.size = ALIGN(IRDMA_SHADOW_AREA_SIZE << 3,
					       64);
		iwcq->kmem_shadow.va = dma_alloc_coherent(dev->hw->device,
							  iwcq->kmem_shadow.size,
							  &iwcq->kmem_shadow.pa,
							  GFP_KERNEL);
		if (!iwcq->kmem_shadow.va) {
			err_code = -ENOMEM;
			goto cq_free_rsrc;
		}
		info.shadow_area_pa = iwcq->kmem_shadow.pa;
		ukinfo->shadow_area = iwcq->kmem_shadow.va;
		ukinfo->cq_base = iwcq->kmem.va;
		info.cq_base_pa = iwcq->kmem.pa;
	}

	if (dev->hw_attrs.uk_attrs.hw_rev >= IRDMA_GEN_2)
		info.shadow_read_threshold = min(info.cq_uk_init_info.cq_size / 2,
						 (u32)IRDMA_MAX_CQ_READ_THRESH);

	if (irdma_sc_cq_init(cq, &info)) {
		ibdev_dbg(&iwdev->ibdev, "VERBS: init cq fail\n");
		err_code = -EPROTO;
		goto cq_free_rsrc;
	}

	cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp, true);
	if (!cqp_request) {
		err_code = -ENOMEM;
		goto cq_free_rsrc;
	}

	cqp_info = &cqp_request->info;
	cqp_info->cqp_cmd = IRDMA_OP_CQ_CREATE;
	cqp_info->post_sq = 1;
	cqp_info->in.u.cq_create.cq = cq;
	cqp_info->in.u.cq_create.check_overflow = true;
	cqp_info->in.u.cq_create.scratch = (uintptr_t)cqp_request;
	err_code = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(&rf->cqp, cqp_request);
	if (err_code)
		goto cq_free_rsrc;

	if (udata) {
		struct irdma_create_cq_resp resp = {};

		resp.cq_id = info.cq_uk_init_info.cq_id;
		resp.cq_size = info.cq_uk_init_info.cq_size;
		if (ib_copy_to_udata(udata, &resp,
				     min(sizeof(resp), udata->outlen))) {
			ibdev_dbg(&iwdev->ibdev,
				  "VERBS: copy to user data\n");
			err_code = -EPROTO;
			goto cq_destroy;
		}
	}
	rf->cq_table[cq_num] = iwcq;
	init_completion(&iwcq->free_cq);

	return 0;
cq_destroy:
	irdma_cq_wq_destroy(rf, cq);
cq_free_rsrc:
	irdma_cq_free_rsrc(rf, iwcq);

	return err_code;
}

/**
 * irdma_get_mr_access - get hw MR access permissions from IB access flags
 * @access: IB access flags
 */
static inline u16 irdma_get_mr_access(int access)
{
	u16 hw_access = 0;

	hw_access |= (access & IB_ACCESS_LOCAL_WRITE) ?
		     IRDMA_ACCESS_FLAGS_LOCALWRITE : 0;
	hw_access |= (access & IB_ACCESS_REMOTE_WRITE) ?
		     IRDMA_ACCESS_FLAGS_REMOTEWRITE : 0;
	hw_access |= (access & IB_ACCESS_REMOTE_READ) ?
		     IRDMA_ACCESS_FLAGS_REMOTEREAD : 0;
	hw_access |= (access & IB_ACCESS_MW_BIND) ?
		     IRDMA_ACCESS_FLAGS_BIND_WINDOW : 0;
	hw_access |= (access & IB_ZERO_BASED) ?
		     IRDMA_ACCESS_FLAGS_ZERO_BASED : 0;
	hw_access |= IRDMA_ACCESS_FLAGS_LOCALREAD;

	return hw_access;
}

/**
 * irdma_free_stag - free stag resource
 * @iwdev: irdma device
 * @stag: stag to free
 */
static void irdma_free_stag(struct irdma_device *iwdev, u32 stag)
{
	u32 stag_idx;

	stag_idx = (stag & iwdev->rf->mr_stagmask) >> IRDMA_CQPSQ_STAG_IDX_S;
	irdma_free_rsrc(iwdev->rf, iwdev->rf->allocated_mrs, stag_idx);
}

/**
 * irdma_create_stag - create random stag
 * @iwdev: irdma device
 */
static u32 irdma_create_stag(struct irdma_device *iwdev)
{
	u32 stag = 0;
	u32 stag_index = 0;
	u32 next_stag_index;
	u32 driver_key;
	u32 random;
	u8 consumer_key;
	int ret;

	get_random_bytes(&random, sizeof(random));
	consumer_key = (u8)random;

	driver_key = random & ~iwdev->rf->mr_stagmask;
	next_stag_index = (random & iwdev->rf->mr_stagmask) >> 8;
	next_stag_index %= iwdev->rf->max_mr;

	ret = irdma_alloc_rsrc(iwdev->rf, iwdev->rf->allocated_mrs,
			       iwdev->rf->max_mr, &stag_index,
			       &next_stag_index);
	if (ret)
		return stag;
	stag = stag_index << IRDMA_CQPSQ_STAG_IDX_S;
	stag |= driver_key;
	stag += (u32)consumer_key;

	return stag;
}

/**
 * irdma_next_pbl_addr - Get next pbl address
 * @pbl: pointer to a pble
 * @pinfo: info pointer
 * @idx: index
 */
static inline u64 *irdma_next_pbl_addr(u64 *pbl, struct irdma_pble_info **pinfo,
				       u32 *idx)
{
	*idx += 1;
	if (!(*pinfo) || *idx != (*pinfo)->cnt)
		return ++pbl;
	*idx = 0;
	(*pinfo)++;

	return (*pinfo)->addr;
}

/**
 * irdma_copy_user_pgaddrs - copy user page address to pble's os locally
 * @iwmr: iwmr for IB's user page addresses
 * @pbl: ple pointer to save 1 level or 0 level pble
 * @level: indicated level 0, 1 or 2
 */
static void irdma_copy_user_pgaddrs(struct irdma_mr *iwmr, u64 *pbl,
				    enum irdma_pble_level level)
{
	struct ib_umem *region = iwmr->region;
	struct irdma_pbl *iwpbl = &iwmr->iwpbl;
	struct irdma_pble_alloc *palloc = &iwpbl->pble_alloc;
	struct irdma_pble_info *pinfo;
	struct ib_block_iter biter;
	u32 idx = 0;
	u32 pbl_cnt = 0;

	pinfo = (level == PBLE_LEVEL_1) ? NULL : palloc->level2.leaf;

	if (iwmr->type == IRDMA_MEMREG_TYPE_QP)
		iwpbl->qp_mr.sq_page = sg_page(region->sgt_append.sgt.sgl);

	rdma_umem_for_each_dma_block(region, &biter, iwmr->page_size) {
		*pbl = rdma_block_iter_dma_address(&biter);
		if (++pbl_cnt == palloc->total_cnt)
			break;
		pbl = irdma_next_pbl_addr(pbl, &pinfo, &idx);
	}
}

/**
 * irdma_check_mem_contiguous - check if pbls stored in arr are contiguous
 * @arr: lvl1 pbl array
 * @npages: page count
 * @pg_size: page size
 *
 */
static bool irdma_check_mem_contiguous(u64 *arr, u32 npages, u32 pg_size)
{
	u32 pg_idx;

	for (pg_idx = 0; pg_idx < npages; pg_idx++) {
		if ((*arr + (pg_size * pg_idx)) != arr[pg_idx])
			return false;
	}

	return true;
}

/**
 * irdma_check_mr_contiguous - check if MR is physically contiguous
 * @palloc: pbl allocation struct
 * @pg_size: page size
 */
static bool irdma_check_mr_contiguous(struct irdma_pble_alloc *palloc,
				      u32 pg_size)
{
	struct irdma_pble_level2 *lvl2 = &palloc->level2;
	struct irdma_pble_info *leaf = lvl2->leaf;
	u64 *arr = NULL;
	u64 *start_addr = NULL;
	int i;
	bool ret;

	if (palloc->level == PBLE_LEVEL_1) {
		arr = palloc->level1.addr;
		ret = irdma_check_mem_contiguous(arr, palloc->total_cnt,
						 pg_size);
		return ret;
	}

	start_addr = leaf->addr;

	for (i = 0; i < lvl2->leaf_cnt; i++, leaf++) {
		arr = leaf->addr;
		if ((*start_addr + (i * pg_size * PBLE_PER_PAGE)) != *arr)
			return false;
		ret = irdma_check_mem_contiguous(arr, leaf->cnt, pg_size);
		if (!ret)
			return false;
	}

	return true;
}

/**
 * irdma_setup_pbles - copy user pg address to pble's
 * @rf: RDMA PCI function
 * @iwmr: mr pointer for this memory registration
 * @lvl: requested pble levels
 */
static int irdma_setup_pbles(struct irdma_pci_f *rf, struct irdma_mr *iwmr,
			     u8 lvl)
{
	struct irdma_pbl *iwpbl = &iwmr->iwpbl;
	struct irdma_pble_alloc *palloc = &iwpbl->pble_alloc;
	struct irdma_pble_info *pinfo;
	u64 *pbl;
	int status;
	enum irdma_pble_level level = PBLE_LEVEL_1;

	if (lvl) {
		status = irdma_get_pble(rf->pble_rsrc, palloc, iwmr->page_cnt,
					lvl);
		if (status)
			return status;

		iwpbl->pbl_allocated = true;
		level = palloc->level;
		pinfo = (level == PBLE_LEVEL_1) ? &palloc->level1 :
						  palloc->level2.leaf;
		pbl = pinfo->addr;
	} else {
		pbl = iwmr->pgaddrmem;
	}

	irdma_copy_user_pgaddrs(iwmr, pbl, level);

	if (lvl)
		iwmr->pgaddrmem[0] = *pbl;

	return 0;
}

/**
 * irdma_handle_q_mem - handle memory for qp and cq
 * @iwdev: irdma device
 * @req: information for q memory management
 * @iwpbl: pble struct
 * @lvl: pble level mask
 */
static int irdma_handle_q_mem(struct irdma_device *iwdev,
			      struct irdma_mem_reg_req *req,
			      struct irdma_pbl *iwpbl, u8 lvl)
{
	struct irdma_pble_alloc *palloc = &iwpbl->pble_alloc;
	struct irdma_mr *iwmr = iwpbl->iwmr;
	struct irdma_qp_mr *qpmr = &iwpbl->qp_mr;
	struct irdma_cq_mr *cqmr = &iwpbl->cq_mr;
	struct irdma_hmc_pble *hmc_p;
	u64 *arr = iwmr->pgaddrmem;
	u32 pg_size, total;
	int err = 0;
	bool ret = true;

	pg_size = iwmr->page_size;
	err = irdma_setup_pbles(iwdev->rf, iwmr, lvl);
	if (err)
		return err;

	if (lvl)
		arr = palloc->level1.addr;

	switch (iwmr->type) {
	case IRDMA_MEMREG_TYPE_QP:
		total = req->sq_pages + req->rq_pages;
		hmc_p = &qpmr->sq_pbl;
		qpmr->shadow = (dma_addr_t)arr[total];

		if (lvl) {
			ret = irdma_check_mem_contiguous(arr, req->sq_pages,
							 pg_size);
			if (ret)
				ret = irdma_check_mem_contiguous(&arr[req->sq_pages],
								 req->rq_pages,
								 pg_size);
		}

		if (!ret) {
			hmc_p->idx = palloc->level1.idx;
			hmc_p = &qpmr->rq_pbl;
			hmc_p->idx = palloc->level1.idx + req->sq_pages;
		} else {
			hmc_p->addr = arr[0];
			hmc_p = &qpmr->rq_pbl;
			hmc_p->addr = arr[req->sq_pages];
		}
		break;
	case IRDMA_MEMREG_TYPE_CQ:
		hmc_p = &cqmr->cq_pbl;

		if (!cqmr->split)
			cqmr->shadow = (dma_addr_t)arr[req->cq_pages];

		if (lvl)
			ret = irdma_check_mem_contiguous(arr, req->cq_pages,
							 pg_size);

		if (!ret)
			hmc_p->idx = palloc->level1.idx;
		else
			hmc_p->addr = arr[0];
	break;
	default:
		ibdev_dbg(&iwdev->ibdev, "VERBS: MR type error\n");
		err = -EINVAL;
	}

	if (lvl && ret) {
		irdma_free_pble(iwdev->rf->pble_rsrc, palloc);
		iwpbl->pbl_allocated = false;
	}

	return err;
}

/**
 * irdma_hw_alloc_mw - create the hw memory window
 * @iwdev: irdma device
 * @iwmr: pointer to memory window info
 */
static int irdma_hw_alloc_mw(struct irdma_device *iwdev, struct irdma_mr *iwmr)
{
	struct irdma_mw_alloc_info *info;
	struct irdma_pd *iwpd = to_iwpd(iwmr->ibmr.pd);
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(&iwdev->rf->cqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	info = &cqp_info->in.u.mw_alloc.info;
	memset(info, 0, sizeof(*info));
	if (iwmr->ibmw.type == IB_MW_TYPE_1)
		info->mw_wide = true;

	info->page_size = PAGE_SIZE;
	info->mw_stag_index = iwmr->stag >> IRDMA_CQPSQ_STAG_IDX_S;
	info->pd_id = iwpd->sc_pd.pd_id;
	info->remote_access = true;
	cqp_info->cqp_cmd = IRDMA_OP_MW_ALLOC;
	cqp_info->post_sq = 1;
	cqp_info->in.u.mw_alloc.dev = &iwdev->rf->sc_dev;
	cqp_info->in.u.mw_alloc.scratch = (uintptr_t)cqp_request;
	status = irdma_handle_cqp_op(iwdev->rf, cqp_request);
	irdma_put_cqp_request(&iwdev->rf->cqp, cqp_request);

	return status;
}

/**
 * irdma_alloc_mw - Allocate memory window
 * @ibmw: Memory Window
 * @udata: user data pointer
 */
static int irdma_alloc_mw(struct ib_mw *ibmw, struct ib_udata *udata)
{
	struct irdma_device *iwdev = to_iwdev(ibmw->device);
	struct irdma_mr *iwmr = to_iwmw(ibmw);
	int err_code;
	u32 stag;

	stag = irdma_create_stag(iwdev);
	if (!stag)
		return -ENOMEM;

	iwmr->stag = stag;
	ibmw->rkey = stag;

	err_code = irdma_hw_alloc_mw(iwdev, iwmr);
	if (err_code) {
		irdma_free_stag(iwdev, stag);
		return err_code;
	}

	return 0;
}

/**
 * irdma_dealloc_mw - Dealloc memory window
 * @ibmw: memory window structure.
 */
static int irdma_dealloc_mw(struct ib_mw *ibmw)
{
	struct ib_pd *ibpd = ibmw->pd;
	struct irdma_pd *iwpd = to_iwpd(ibpd);
	struct irdma_mr *iwmr = to_iwmr((struct ib_mr *)ibmw);
	struct irdma_device *iwdev = to_iwdev(ibmw->device);
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	struct irdma_dealloc_stag_info *info;

	cqp_request = irdma_alloc_and_get_cqp_request(&iwdev->rf->cqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	info = &cqp_info->in.u.dealloc_stag.info;
	memset(info, 0, sizeof(*info));
	info->pd_id = iwpd->sc_pd.pd_id;
	info->stag_idx = ibmw->rkey >> IRDMA_CQPSQ_STAG_IDX_S;
	info->mr = false;
	cqp_info->cqp_cmd = IRDMA_OP_DEALLOC_STAG;
	cqp_info->post_sq = 1;
	cqp_info->in.u.dealloc_stag.dev = &iwdev->rf->sc_dev;
	cqp_info->in.u.dealloc_stag.scratch = (uintptr_t)cqp_request;
	irdma_handle_cqp_op(iwdev->rf, cqp_request);
	irdma_put_cqp_request(&iwdev->rf->cqp, cqp_request);
	irdma_free_stag(iwdev, iwmr->stag);

	return 0;
}

/**
 * irdma_hw_alloc_stag - cqp command to allocate stag
 * @iwdev: irdma device
 * @iwmr: irdma mr pointer
 */
static int irdma_hw_alloc_stag(struct irdma_device *iwdev,
			       struct irdma_mr *iwmr)
{
	struct irdma_allocate_stag_info *info;
	struct irdma_pd *iwpd = to_iwpd(iwmr->ibmr.pd);
	int status;
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;

	cqp_request = irdma_alloc_and_get_cqp_request(&iwdev->rf->cqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	info = &cqp_info->in.u.alloc_stag.info;
	memset(info, 0, sizeof(*info));
	info->page_size = PAGE_SIZE;
	info->stag_idx = iwmr->stag >> IRDMA_CQPSQ_STAG_IDX_S;
	info->pd_id = iwpd->sc_pd.pd_id;
	info->total_len = iwmr->len;
	info->remote_access = true;
	cqp_info->cqp_cmd = IRDMA_OP_ALLOC_STAG;
	cqp_info->post_sq = 1;
	cqp_info->in.u.alloc_stag.dev = &iwdev->rf->sc_dev;
	cqp_info->in.u.alloc_stag.scratch = (uintptr_t)cqp_request;
	status = irdma_handle_cqp_op(iwdev->rf, cqp_request);
	irdma_put_cqp_request(&iwdev->rf->cqp, cqp_request);

	return status;
}

/**
 * irdma_alloc_mr - register stag for fast memory registration
 * @pd: ibpd pointer
 * @mr_type: memory for stag registrion
 * @max_num_sg: man number of pages
 */
static struct ib_mr *irdma_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
				    u32 max_num_sg)
{
	struct irdma_device *iwdev = to_iwdev(pd->device);
	struct irdma_pble_alloc *palloc;
	struct irdma_pbl *iwpbl;
	struct irdma_mr *iwmr;
	u32 stag;
	int err_code;

	iwmr = kzalloc(sizeof(*iwmr), GFP_KERNEL);
	if (!iwmr)
		return ERR_PTR(-ENOMEM);

	stag = irdma_create_stag(iwdev);
	if (!stag) {
		err_code = -ENOMEM;
		goto err;
	}

	iwmr->stag = stag;
	iwmr->ibmr.rkey = stag;
	iwmr->ibmr.lkey = stag;
	iwmr->ibmr.pd = pd;
	iwmr->ibmr.device = pd->device;
	iwpbl = &iwmr->iwpbl;
	iwpbl->iwmr = iwmr;
	iwmr->type = IRDMA_MEMREG_TYPE_MEM;
	palloc = &iwpbl->pble_alloc;
	iwmr->page_cnt = max_num_sg;
	err_code = irdma_get_pble(iwdev->rf->pble_rsrc, palloc, iwmr->page_cnt,
				  false);
	if (err_code)
		goto err_get_pble;

	err_code = irdma_hw_alloc_stag(iwdev, iwmr);
	if (err_code)
		goto err_alloc_stag;

	iwpbl->pbl_allocated = true;

	return &iwmr->ibmr;
err_alloc_stag:
	irdma_free_pble(iwdev->rf->pble_rsrc, palloc);
err_get_pble:
	irdma_free_stag(iwdev, stag);
err:
	kfree(iwmr);

	return ERR_PTR(err_code);
}

/**
 * irdma_set_page - populate pbl list for fmr
 * @ibmr: ib mem to access iwarp mr pointer
 * @addr: page dma address fro pbl list
 */
static int irdma_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct irdma_mr *iwmr = to_iwmr(ibmr);
	struct irdma_pbl *iwpbl = &iwmr->iwpbl;
	struct irdma_pble_alloc *palloc = &iwpbl->pble_alloc;
	u64 *pbl;

	if (unlikely(iwmr->npages == iwmr->page_cnt))
		return -ENOMEM;

	if (palloc->level == PBLE_LEVEL_2) {
		struct irdma_pble_info *palloc_info =
			palloc->level2.leaf + (iwmr->npages >> PBLE_512_SHIFT);

		palloc_info->addr[iwmr->npages & (PBLE_PER_PAGE - 1)] = addr;
	} else {
		pbl = palloc->level1.addr;
		pbl[iwmr->npages] = addr;
	}
	iwmr->npages++;

	return 0;
}

/**
 * irdma_map_mr_sg - map of sg list for fmr
 * @ibmr: ib mem to access iwarp mr pointer
 * @sg: scatter gather list
 * @sg_nents: number of sg pages
 * @sg_offset: scatter gather list for fmr
 */
static int irdma_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg,
			   int sg_nents, unsigned int *sg_offset)
{
	struct irdma_mr *iwmr = to_iwmr(ibmr);

	iwmr->npages = 0;

	return ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset, irdma_set_page);
}

/**
 * irdma_hwreg_mr - send cqp command for memory registration
 * @iwdev: irdma device
 * @iwmr: irdma mr pointer
 * @access: access for MR
 */
static int irdma_hwreg_mr(struct irdma_device *iwdev, struct irdma_mr *iwmr,
			  u16 access)
{
	struct irdma_pbl *iwpbl = &iwmr->iwpbl;
	struct irdma_reg_ns_stag_info *stag_info;
	struct irdma_pd *iwpd = to_iwpd(iwmr->ibmr.pd);
	struct irdma_pble_alloc *palloc = &iwpbl->pble_alloc;
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	int ret;

	cqp_request = irdma_alloc_and_get_cqp_request(&iwdev->rf->cqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	stag_info = &cqp_info->in.u.mr_reg_non_shared.info;
	memset(stag_info, 0, sizeof(*stag_info));
	stag_info->va = iwpbl->user_base;
	stag_info->stag_idx = iwmr->stag >> IRDMA_CQPSQ_STAG_IDX_S;
	stag_info->stag_key = (u8)iwmr->stag;
	stag_info->total_len = iwmr->len;
	stag_info->access_rights = irdma_get_mr_access(access);
	stag_info->pd_id = iwpd->sc_pd.pd_id;
	if (stag_info->access_rights & IRDMA_ACCESS_FLAGS_ZERO_BASED)
		stag_info->addr_type = IRDMA_ADDR_TYPE_ZERO_BASED;
	else
		stag_info->addr_type = IRDMA_ADDR_TYPE_VA_BASED;
	stag_info->page_size = iwmr->page_size;

	if (iwpbl->pbl_allocated) {
		if (palloc->level == PBLE_LEVEL_1) {
			stag_info->first_pm_pbl_index = palloc->level1.idx;
			stag_info->chunk_size = 1;
		} else {
			stag_info->first_pm_pbl_index = palloc->level2.root.idx;
			stag_info->chunk_size = 3;
		}
	} else {
		stag_info->reg_addr_pa = iwmr->pgaddrmem[0];
	}

	cqp_info->cqp_cmd = IRDMA_OP_MR_REG_NON_SHARED;
	cqp_info->post_sq = 1;
	cqp_info->in.u.mr_reg_non_shared.dev = &iwdev->rf->sc_dev;
	cqp_info->in.u.mr_reg_non_shared.scratch = (uintptr_t)cqp_request;
	ret = irdma_handle_cqp_op(iwdev->rf, cqp_request);
	irdma_put_cqp_request(&iwdev->rf->cqp, cqp_request);

	return ret;
}

static int irdma_reg_user_mr_type_mem(struct irdma_mr *iwmr, int access)
{
	struct irdma_device *iwdev = to_iwdev(iwmr->ibmr.device);
	struct irdma_pbl *iwpbl = &iwmr->iwpbl;
	u32 stag;
	u8 lvl;
	int err;

	lvl = iwmr->page_cnt != 1 ? PBLE_LEVEL_1 | PBLE_LEVEL_2 : PBLE_LEVEL_0;

	err = irdma_setup_pbles(iwdev->rf, iwmr, lvl);
	if (err)
		return err;

	if (lvl) {
		err = irdma_check_mr_contiguous(&iwpbl->pble_alloc,
						iwmr->page_size);
		if (err) {
			irdma_free_pble(iwdev->rf->pble_rsrc, &iwpbl->pble_alloc);
			iwpbl->pbl_allocated = false;
		}
	}

	stag = irdma_create_stag(iwdev);
	if (!stag) {
		err = -ENOMEM;
		goto free_pble;
	}

	iwmr->stag = stag;
	iwmr->ibmr.rkey = stag;
	iwmr->ibmr.lkey = stag;
	err = irdma_hwreg_mr(iwdev, iwmr, access);
	if (err)
		goto err_hwreg;

	return 0;

err_hwreg:
	irdma_free_stag(iwdev, stag);

free_pble:
	if (iwpbl->pble_alloc.level != PBLE_LEVEL_0 && iwpbl->pbl_allocated)
		irdma_free_pble(iwdev->rf->pble_rsrc, &iwpbl->pble_alloc);

	return err;
}

static struct irdma_mr *irdma_alloc_iwmr(struct ib_umem *region,
					 struct ib_pd *pd, u64 virt,
					 enum irdma_memreg_type reg_type)
{
	struct irdma_device *iwdev = to_iwdev(pd->device);
	struct irdma_pbl *iwpbl = NULL;
	struct irdma_mr *iwmr = NULL;
	unsigned long pgsz_bitmap;

	iwmr = kzalloc(sizeof(*iwmr), GFP_KERNEL);
	if (!iwmr)
		return ERR_PTR(-ENOMEM);

	iwpbl = &iwmr->iwpbl;
	iwpbl->iwmr = iwmr;
	iwmr->region = region;
	iwmr->ibmr.pd = pd;
	iwmr->ibmr.device = pd->device;
	iwmr->ibmr.iova = virt;
	iwmr->type = reg_type;

	pgsz_bitmap = (reg_type == IRDMA_MEMREG_TYPE_MEM) ?
		iwdev->rf->sc_dev.hw_attrs.page_size_cap : PAGE_SIZE;

	iwmr->page_size = ib_umem_find_best_pgsz(region, pgsz_bitmap, virt);
	if (unlikely(!iwmr->page_size)) {
		kfree(iwmr);
		return ERR_PTR(-EOPNOTSUPP);
	}

	iwmr->len = region->length;
	iwpbl->user_base = virt;
	iwmr->page_cnt = ib_umem_num_dma_blocks(region, iwmr->page_size);

	return iwmr;
}

static void irdma_free_iwmr(struct irdma_mr *iwmr)
{
	kfree(iwmr);
}

static int irdma_reg_user_mr_type_qp(struct irdma_mem_reg_req req,
				     struct ib_udata *udata,
				     struct irdma_mr *iwmr)
{
	struct irdma_device *iwdev = to_iwdev(iwmr->ibmr.device);
	struct irdma_pbl *iwpbl = &iwmr->iwpbl;
	struct irdma_ucontext *ucontext = NULL;
	unsigned long flags;
	u32 total;
	int err;
	u8 lvl;

	total = req.sq_pages + req.rq_pages + 1;
	if (total > iwmr->page_cnt)
		return -EINVAL;

	total = req.sq_pages + req.rq_pages;
	lvl = total > 2 ? PBLE_LEVEL_1 : PBLE_LEVEL_0;
	err = irdma_handle_q_mem(iwdev, &req, iwpbl, lvl);
	if (err)
		return err;

	ucontext = rdma_udata_to_drv_context(udata, struct irdma_ucontext,
					     ibucontext);
	spin_lock_irqsave(&ucontext->qp_reg_mem_list_lock, flags);
	list_add_tail(&iwpbl->list, &ucontext->qp_reg_mem_list);
	iwpbl->on_list = true;
	spin_unlock_irqrestore(&ucontext->qp_reg_mem_list_lock, flags);

	return 0;
}

static int irdma_reg_user_mr_type_cq(struct irdma_mem_reg_req req,
				     struct ib_udata *udata,
				     struct irdma_mr *iwmr)
{
	struct irdma_device *iwdev = to_iwdev(iwmr->ibmr.device);
	struct irdma_pbl *iwpbl = &iwmr->iwpbl;
	struct irdma_ucontext *ucontext = NULL;
	u8 shadow_pgcnt = 1;
	unsigned long flags;
	u32 total;
	int err;
	u8 lvl;

	if (iwdev->rf->sc_dev.hw_attrs.uk_attrs.feature_flags & IRDMA_FEATURE_CQ_RESIZE)
		shadow_pgcnt = 0;
	total = req.cq_pages + shadow_pgcnt;
	if (total > iwmr->page_cnt)
		return -EINVAL;

	lvl = req.cq_pages > 1 ? PBLE_LEVEL_1 : PBLE_LEVEL_0;
	err = irdma_handle_q_mem(iwdev, &req, iwpbl, lvl);
	if (err)
		return err;

	ucontext = rdma_udata_to_drv_context(udata, struct irdma_ucontext,
					     ibucontext);
	spin_lock_irqsave(&ucontext->cq_reg_mem_list_lock, flags);
	list_add_tail(&iwpbl->list, &ucontext->cq_reg_mem_list);
	iwpbl->on_list = true;
	spin_unlock_irqrestore(&ucontext->cq_reg_mem_list_lock, flags);

	return 0;
}

/**
 * irdma_reg_user_mr - Register a user memory region
 * @pd: ptr of pd
 * @start: virtual start address
 * @len: length of mr
 * @virt: virtual address
 * @access: access of mr
 * @udata: user data
 */
static struct ib_mr *irdma_reg_user_mr(struct ib_pd *pd, u64 start, u64 len,
				       u64 virt, int access,
				       struct ib_udata *udata)
{
#define IRDMA_MEM_REG_MIN_REQ_LEN offsetofend(struct irdma_mem_reg_req, sq_pages)
	struct irdma_device *iwdev = to_iwdev(pd->device);
	struct irdma_mem_reg_req req = {};
	struct ib_umem *region = NULL;
	struct irdma_mr *iwmr = NULL;
	int err;

	if (len > iwdev->rf->sc_dev.hw_attrs.max_mr_size)
		return ERR_PTR(-EINVAL);

	if (udata->inlen < IRDMA_MEM_REG_MIN_REQ_LEN)
		return ERR_PTR(-EINVAL);

	region = ib_umem_get(pd->device, start, len, access);

	if (IS_ERR(region)) {
		ibdev_dbg(&iwdev->ibdev,
			  "VERBS: Failed to create ib_umem region\n");
		return (struct ib_mr *)region;
	}

	if (ib_copy_from_udata(&req, udata, min(sizeof(req), udata->inlen))) {
		ib_umem_release(region);
		return ERR_PTR(-EFAULT);
	}

	iwmr = irdma_alloc_iwmr(region, pd, virt, req.reg_type);
	if (IS_ERR(iwmr)) {
		ib_umem_release(region);
		return (struct ib_mr *)iwmr;
	}

	switch (req.reg_type) {
	case IRDMA_MEMREG_TYPE_QP:
		err = irdma_reg_user_mr_type_qp(req, udata, iwmr);
		if (err)
			goto error;

		break;
	case IRDMA_MEMREG_TYPE_CQ:
		err = irdma_reg_user_mr_type_cq(req, udata, iwmr);
		if (err)
			goto error;
		break;
	case IRDMA_MEMREG_TYPE_MEM:
		err = irdma_reg_user_mr_type_mem(iwmr, access);
		if (err)
			goto error;

		break;
	default:
		err = -EINVAL;
		goto error;
	}

	return &iwmr->ibmr;
error:
	ib_umem_release(region);
	irdma_free_iwmr(iwmr);

	return ERR_PTR(err);
}

static struct ib_mr *irdma_reg_user_mr_dmabuf(struct ib_pd *pd, u64 start,
					      u64 len, u64 virt,
					      int fd, int access,
					      struct ib_udata *udata)
{
	struct irdma_device *iwdev = to_iwdev(pd->device);
	struct ib_umem_dmabuf *umem_dmabuf;
	struct irdma_mr *iwmr;
	int err;

	if (len > iwdev->rf->sc_dev.hw_attrs.max_mr_size)
		return ERR_PTR(-EINVAL);

	umem_dmabuf = ib_umem_dmabuf_get_pinned(pd->device, start, len, fd, access);
	if (IS_ERR(umem_dmabuf)) {
		err = PTR_ERR(umem_dmabuf);
		ibdev_dbg(&iwdev->ibdev, "Failed to get dmabuf umem[%d]\n", err);
		return ERR_PTR(err);
	}

	iwmr = irdma_alloc_iwmr(&umem_dmabuf->umem, pd, virt, IRDMA_MEMREG_TYPE_MEM);
	if (IS_ERR(iwmr)) {
		err = PTR_ERR(iwmr);
		goto err_release;
	}

	err = irdma_reg_user_mr_type_mem(iwmr, access);
	if (err)
		goto err_iwmr;

	return &iwmr->ibmr;

err_iwmr:
	irdma_free_iwmr(iwmr);

err_release:
	ib_umem_release(&umem_dmabuf->umem);

	return ERR_PTR(err);
}

/**
 * irdma_reg_phys_mr - register kernel physical memory
 * @pd: ibpd pointer
 * @addr: physical address of memory to register
 * @size: size of memory to register
 * @access: Access rights
 * @iova_start: start of virtual address for physical buffers
 */
struct ib_mr *irdma_reg_phys_mr(struct ib_pd *pd, u64 addr, u64 size, int access,
				u64 *iova_start)
{
	struct irdma_device *iwdev = to_iwdev(pd->device);
	struct irdma_pbl *iwpbl;
	struct irdma_mr *iwmr;
	u32 stag;
	int ret;

	iwmr = kzalloc(sizeof(*iwmr), GFP_KERNEL);
	if (!iwmr)
		return ERR_PTR(-ENOMEM);

	iwmr->ibmr.pd = pd;
	iwmr->ibmr.device = pd->device;
	iwpbl = &iwmr->iwpbl;
	iwpbl->iwmr = iwmr;
	iwmr->type = IRDMA_MEMREG_TYPE_MEM;
	iwpbl->user_base = *iova_start;
	stag = irdma_create_stag(iwdev);
	if (!stag) {
		ret = -ENOMEM;
		goto err;
	}

	iwmr->stag = stag;
	iwmr->ibmr.iova = *iova_start;
	iwmr->ibmr.rkey = stag;
	iwmr->ibmr.lkey = stag;
	iwmr->page_cnt = 1;
	iwmr->pgaddrmem[0] = addr;
	iwmr->len = size;
	iwmr->page_size = SZ_4K;
	ret = irdma_hwreg_mr(iwdev, iwmr, access);
	if (ret) {
		irdma_free_stag(iwdev, stag);
		goto err;
	}

	return &iwmr->ibmr;

err:
	kfree(iwmr);

	return ERR_PTR(ret);
}

/**
 * irdma_get_dma_mr - register physical mem
 * @pd: ptr of pd
 * @acc: access for memory
 */
static struct ib_mr *irdma_get_dma_mr(struct ib_pd *pd, int acc)
{
	u64 kva = 0;

	return irdma_reg_phys_mr(pd, 0, 0, acc, &kva);
}

/**
 * irdma_del_memlist - Deleting pbl list entries for CQ/QP
 * @iwmr: iwmr for IB's user page addresses
 * @ucontext: ptr to user context
 */
static void irdma_del_memlist(struct irdma_mr *iwmr,
			      struct irdma_ucontext *ucontext)
{
	struct irdma_pbl *iwpbl = &iwmr->iwpbl;
	unsigned long flags;

	switch (iwmr->type) {
	case IRDMA_MEMREG_TYPE_CQ:
		spin_lock_irqsave(&ucontext->cq_reg_mem_list_lock, flags);
		if (iwpbl->on_list) {
			iwpbl->on_list = false;
			list_del(&iwpbl->list);
		}
		spin_unlock_irqrestore(&ucontext->cq_reg_mem_list_lock, flags);
		break;
	case IRDMA_MEMREG_TYPE_QP:
		spin_lock_irqsave(&ucontext->qp_reg_mem_list_lock, flags);
		if (iwpbl->on_list) {
			iwpbl->on_list = false;
			list_del(&iwpbl->list);
		}
		spin_unlock_irqrestore(&ucontext->qp_reg_mem_list_lock, flags);
		break;
	default:
		break;
	}
}

/**
 * irdma_dereg_mr - deregister mr
 * @ib_mr: mr ptr for dereg
 * @udata: user data
 */
static int irdma_dereg_mr(struct ib_mr *ib_mr, struct ib_udata *udata)
{
	struct ib_pd *ibpd = ib_mr->pd;
	struct irdma_pd *iwpd = to_iwpd(ibpd);
	struct irdma_mr *iwmr = to_iwmr(ib_mr);
	struct irdma_device *iwdev = to_iwdev(ib_mr->device);
	struct irdma_dealloc_stag_info *info;
	struct irdma_pbl *iwpbl = &iwmr->iwpbl;
	struct irdma_pble_alloc *palloc = &iwpbl->pble_alloc;
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	int status;

	if (iwmr->type != IRDMA_MEMREG_TYPE_MEM) {
		if (iwmr->region) {
			struct irdma_ucontext *ucontext;

			ucontext = rdma_udata_to_drv_context(udata,
						struct irdma_ucontext,
						ibucontext);
			irdma_del_memlist(iwmr, ucontext);
		}
		goto done;
	}

	cqp_request = irdma_alloc_and_get_cqp_request(&iwdev->rf->cqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	info = &cqp_info->in.u.dealloc_stag.info;
	memset(info, 0, sizeof(*info));
	info->pd_id = iwpd->sc_pd.pd_id;
	info->stag_idx = ib_mr->rkey >> IRDMA_CQPSQ_STAG_IDX_S;
	info->mr = true;
	if (iwpbl->pbl_allocated)
		info->dealloc_pbl = true;

	cqp_info->cqp_cmd = IRDMA_OP_DEALLOC_STAG;
	cqp_info->post_sq = 1;
	cqp_info->in.u.dealloc_stag.dev = &iwdev->rf->sc_dev;
	cqp_info->in.u.dealloc_stag.scratch = (uintptr_t)cqp_request;
	status = irdma_handle_cqp_op(iwdev->rf, cqp_request);
	irdma_put_cqp_request(&iwdev->rf->cqp, cqp_request);
	if (status)
		return status;

	irdma_free_stag(iwdev, iwmr->stag);
done:
	if (iwpbl->pbl_allocated)
		irdma_free_pble(iwdev->rf->pble_rsrc, palloc);
	ib_umem_release(iwmr->region);
	kfree(iwmr);

	return 0;
}

/**
 * irdma_post_send -  kernel application wr
 * @ibqp: qp ptr for wr
 * @ib_wr: work request ptr
 * @bad_wr: return of bad wr if err
 */
static int irdma_post_send(struct ib_qp *ibqp,
			   const struct ib_send_wr *ib_wr,
			   const struct ib_send_wr **bad_wr)
{
	struct irdma_qp *iwqp;
	struct irdma_qp_uk *ukqp;
	struct irdma_sc_dev *dev;
	struct irdma_post_sq_info info;
	int err = 0;
	unsigned long flags;
	bool inv_stag;
	struct irdma_ah *ah;

	iwqp = to_iwqp(ibqp);
	ukqp = &iwqp->sc_qp.qp_uk;
	dev = &iwqp->iwdev->rf->sc_dev;

	spin_lock_irqsave(&iwqp->lock, flags);
	while (ib_wr) {
		memset(&info, 0, sizeof(info));
		inv_stag = false;
		info.wr_id = (ib_wr->wr_id);
		if ((ib_wr->send_flags & IB_SEND_SIGNALED) || iwqp->sig_all)
			info.signaled = true;
		if (ib_wr->send_flags & IB_SEND_FENCE)
			info.read_fence = true;
		switch (ib_wr->opcode) {
		case IB_WR_SEND_WITH_IMM:
			if (ukqp->qp_caps & IRDMA_SEND_WITH_IMM) {
				info.imm_data_valid = true;
				info.imm_data = ntohl(ib_wr->ex.imm_data);
			} else {
				err = -EINVAL;
				break;
			}
			fallthrough;
		case IB_WR_SEND:
		case IB_WR_SEND_WITH_INV:
			if (ib_wr->opcode == IB_WR_SEND ||
			    ib_wr->opcode == IB_WR_SEND_WITH_IMM) {
				if (ib_wr->send_flags & IB_SEND_SOLICITED)
					info.op_type = IRDMA_OP_TYPE_SEND_SOL;
				else
					info.op_type = IRDMA_OP_TYPE_SEND;
			} else {
				if (ib_wr->send_flags & IB_SEND_SOLICITED)
					info.op_type = IRDMA_OP_TYPE_SEND_SOL_INV;
				else
					info.op_type = IRDMA_OP_TYPE_SEND_INV;
				info.stag_to_inv = ib_wr->ex.invalidate_rkey;
			}

			info.op.send.num_sges = ib_wr->num_sge;
			info.op.send.sg_list = ib_wr->sg_list;
			if (iwqp->ibqp.qp_type == IB_QPT_UD ||
			    iwqp->ibqp.qp_type == IB_QPT_GSI) {
				ah = to_iwah(ud_wr(ib_wr)->ah);
				info.op.send.ah_id = ah->sc_ah.ah_info.ah_idx;
				info.op.send.qkey = ud_wr(ib_wr)->remote_qkey;
				info.op.send.dest_qp = ud_wr(ib_wr)->remote_qpn;
			}

			if (ib_wr->send_flags & IB_SEND_INLINE)
				err = irdma_uk_inline_send(ukqp, &info, false);
			else
				err = irdma_uk_send(ukqp, &info, false);
			break;
		case IB_WR_RDMA_WRITE_WITH_IMM:
			if (ukqp->qp_caps & IRDMA_WRITE_WITH_IMM) {
				info.imm_data_valid = true;
				info.imm_data = ntohl(ib_wr->ex.imm_data);
			} else {
				err = -EINVAL;
				break;
			}
			fallthrough;
		case IB_WR_RDMA_WRITE:
			if (ib_wr->send_flags & IB_SEND_SOLICITED)
				info.op_type = IRDMA_OP_TYPE_RDMA_WRITE_SOL;
			else
				info.op_type = IRDMA_OP_TYPE_RDMA_WRITE;

			info.op.rdma_write.num_lo_sges = ib_wr->num_sge;
			info.op.rdma_write.lo_sg_list = ib_wr->sg_list;
			info.op.rdma_write.rem_addr.addr =
				rdma_wr(ib_wr)->remote_addr;
			info.op.rdma_write.rem_addr.lkey = rdma_wr(ib_wr)->rkey;
			if (ib_wr->send_flags & IB_SEND_INLINE)
				err = irdma_uk_inline_rdma_write(ukqp, &info, false);
			else
				err = irdma_uk_rdma_write(ukqp, &info, false);
			break;
		case IB_WR_RDMA_READ_WITH_INV:
			inv_stag = true;
			fallthrough;
		case IB_WR_RDMA_READ:
			if (ib_wr->num_sge >
			    dev->hw_attrs.uk_attrs.max_hw_read_sges) {
				err = -EINVAL;
				break;
			}
			info.op_type = IRDMA_OP_TYPE_RDMA_READ;
			info.op.rdma_read.rem_addr.addr = rdma_wr(ib_wr)->remote_addr;
			info.op.rdma_read.rem_addr.lkey = rdma_wr(ib_wr)->rkey;
			info.op.rdma_read.lo_sg_list = (void *)ib_wr->sg_list;
			info.op.rdma_read.num_lo_sges = ib_wr->num_sge;
			err = irdma_uk_rdma_read(ukqp, &info, inv_stag, false);
			break;
		case IB_WR_LOCAL_INV:
			info.op_type = IRDMA_OP_TYPE_INV_STAG;
			info.local_fence = info.read_fence;
			info.op.inv_local_stag.target_stag = ib_wr->ex.invalidate_rkey;
			err = irdma_uk_stag_local_invalidate(ukqp, &info, true);
			break;
		case IB_WR_REG_MR: {
			struct irdma_mr *iwmr = to_iwmr(reg_wr(ib_wr)->mr);
			struct irdma_pble_alloc *palloc = &iwmr->iwpbl.pble_alloc;
			struct irdma_fast_reg_stag_info stag_info = {};

			stag_info.signaled = info.signaled;
			stag_info.read_fence = info.read_fence;
			stag_info.access_rights = irdma_get_mr_access(reg_wr(ib_wr)->access);
			stag_info.stag_key = reg_wr(ib_wr)->key & 0xff;
			stag_info.stag_idx = reg_wr(ib_wr)->key >> 8;
			stag_info.page_size = reg_wr(ib_wr)->mr->page_size;
			stag_info.wr_id = ib_wr->wr_id;
			stag_info.addr_type = IRDMA_ADDR_TYPE_VA_BASED;
			stag_info.va = (void *)(uintptr_t)iwmr->ibmr.iova;
			stag_info.total_len = iwmr->ibmr.length;
			stag_info.reg_addr_pa = *palloc->level1.addr;
			stag_info.first_pm_pbl_index = palloc->level1.idx;
			stag_info.local_fence = ib_wr->send_flags & IB_SEND_FENCE;
			if (iwmr->npages > IRDMA_MIN_PAGES_PER_FMR)
				stag_info.chunk_size = 1;
			err = irdma_sc_mr_fast_register(&iwqp->sc_qp, &stag_info,
							true);
			break;
		}
		default:
			err = -EINVAL;
			ibdev_dbg(&iwqp->iwdev->ibdev,
				  "VERBS: upost_send bad opcode = 0x%x\n",
				  ib_wr->opcode);
			break;
		}

		if (err)
			break;
		ib_wr = ib_wr->next;
	}

	if (!iwqp->flush_issued) {
		if (iwqp->hw_iwarp_state <= IRDMA_QP_STATE_RTS)
			irdma_uk_qp_post_wr(ukqp);
		spin_unlock_irqrestore(&iwqp->lock, flags);
	} else {
		spin_unlock_irqrestore(&iwqp->lock, flags);
		mod_delayed_work(iwqp->iwdev->cleanup_wq, &iwqp->dwork_flush,
				 msecs_to_jiffies(IRDMA_FLUSH_DELAY_MS));
	}
	if (err)
		*bad_wr = ib_wr;

	return err;
}

/**
 * irdma_post_recv - post receive wr for kernel application
 * @ibqp: ib qp pointer
 * @ib_wr: work request for receive
 * @bad_wr: bad wr caused an error
 */
static int irdma_post_recv(struct ib_qp *ibqp,
			   const struct ib_recv_wr *ib_wr,
			   const struct ib_recv_wr **bad_wr)
{
	struct irdma_qp *iwqp;
	struct irdma_qp_uk *ukqp;
	struct irdma_post_rq_info post_recv = {};
	unsigned long flags;
	int err = 0;

	iwqp = to_iwqp(ibqp);
	ukqp = &iwqp->sc_qp.qp_uk;

	spin_lock_irqsave(&iwqp->lock, flags);
	while (ib_wr) {
		post_recv.num_sges = ib_wr->num_sge;
		post_recv.wr_id = ib_wr->wr_id;
		post_recv.sg_list = ib_wr->sg_list;
		err = irdma_uk_post_receive(ukqp, &post_recv);
		if (err) {
			ibdev_dbg(&iwqp->iwdev->ibdev,
				  "VERBS: post_recv err %d\n", err);
			goto out;
		}

		ib_wr = ib_wr->next;
	}

out:
	spin_unlock_irqrestore(&iwqp->lock, flags);
	if (iwqp->flush_issued)
		mod_delayed_work(iwqp->iwdev->cleanup_wq, &iwqp->dwork_flush,
				 msecs_to_jiffies(IRDMA_FLUSH_DELAY_MS));

	if (err)
		*bad_wr = ib_wr;

	return err;
}

/**
 * irdma_flush_err_to_ib_wc_status - return change flush error code to IB status
 * @opcode: iwarp flush code
 */
static enum ib_wc_status irdma_flush_err_to_ib_wc_status(enum irdma_flush_opcode opcode)
{
	switch (opcode) {
	case FLUSH_PROT_ERR:
		return IB_WC_LOC_PROT_ERR;
	case FLUSH_REM_ACCESS_ERR:
		return IB_WC_REM_ACCESS_ERR;
	case FLUSH_LOC_QP_OP_ERR:
		return IB_WC_LOC_QP_OP_ERR;
	case FLUSH_REM_OP_ERR:
		return IB_WC_REM_OP_ERR;
	case FLUSH_LOC_LEN_ERR:
		return IB_WC_LOC_LEN_ERR;
	case FLUSH_GENERAL_ERR:
		return IB_WC_WR_FLUSH_ERR;
	case FLUSH_RETRY_EXC_ERR:
		return IB_WC_RETRY_EXC_ERR;
	case FLUSH_MW_BIND_ERR:
		return IB_WC_MW_BIND_ERR;
	case FLUSH_REM_INV_REQ_ERR:
		return IB_WC_REM_INV_REQ_ERR;
	case FLUSH_FATAL_ERR:
	default:
		return IB_WC_FATAL_ERR;
	}
}

/**
 * irdma_process_cqe - process cqe info
 * @entry: processed cqe
 * @cq_poll_info: cqe info
 */
static void irdma_process_cqe(struct ib_wc *entry,
			      struct irdma_cq_poll_info *cq_poll_info)
{
	struct irdma_sc_qp *qp;

	entry->wc_flags = 0;
	entry->pkey_index = 0;
	entry->wr_id = cq_poll_info->wr_id;

	qp = cq_poll_info->qp_handle;
	entry->qp = qp->qp_uk.back_qp;

	if (cq_poll_info->error) {
		entry->status = (cq_poll_info->comp_status == IRDMA_COMPL_STATUS_FLUSHED) ?
				irdma_flush_err_to_ib_wc_status(cq_poll_info->minor_err) : IB_WC_GENERAL_ERR;

		entry->vendor_err = cq_poll_info->major_err << 16 |
				    cq_poll_info->minor_err;
	} else {
		entry->status = IB_WC_SUCCESS;
		if (cq_poll_info->imm_valid) {
			entry->ex.imm_data = htonl(cq_poll_info->imm_data);
			entry->wc_flags |= IB_WC_WITH_IMM;
		}
		if (cq_poll_info->ud_smac_valid) {
			ether_addr_copy(entry->smac, cq_poll_info->ud_smac);
			entry->wc_flags |= IB_WC_WITH_SMAC;
		}

		if (cq_poll_info->ud_vlan_valid) {
			u16 vlan = cq_poll_info->ud_vlan & VLAN_VID_MASK;

			entry->sl = cq_poll_info->ud_vlan >> VLAN_PRIO_SHIFT;
			if (vlan) {
				entry->vlan_id = vlan;
				entry->wc_flags |= IB_WC_WITH_VLAN;
			}
		} else {
			entry->sl = 0;
		}
	}

	if (cq_poll_info->q_type == IRDMA_CQE_QTYPE_SQ) {
		set_ib_wc_op_sq(cq_poll_info, entry);
	} else {
		set_ib_wc_op_rq(cq_poll_info, entry,
				qp->qp_uk.qp_caps & IRDMA_SEND_WITH_IMM);
		if (qp->qp_uk.qp_type != IRDMA_QP_TYPE_ROCE_UD &&
		    cq_poll_info->stag_invalid_set) {
			entry->ex.invalidate_rkey = cq_poll_info->inv_stag;
			entry->wc_flags |= IB_WC_WITH_INVALIDATE;
		}
	}

	if (qp->qp_uk.qp_type == IRDMA_QP_TYPE_ROCE_UD) {
		entry->src_qp = cq_poll_info->ud_src_qpn;
		entry->slid = 0;
		entry->wc_flags |=
			(IB_WC_GRH | IB_WC_WITH_NETWORK_HDR_TYPE);
		entry->network_hdr_type = cq_poll_info->ipv4 ?
						  RDMA_NETWORK_IPV4 :
						  RDMA_NETWORK_IPV6;
	} else {
		entry->src_qp = cq_poll_info->qp_id;
	}

	entry->byte_len = cq_poll_info->bytes_xfered;
}

/**
 * irdma_poll_one - poll one entry of the CQ
 * @ukcq: ukcq to poll
 * @cur_cqe: current CQE info to be filled in
 * @entry: ibv_wc object to be filled for non-extended CQ or NULL for extended CQ
 *
 * Returns the internal irdma device error code or 0 on success
 */
static inline int irdma_poll_one(struct irdma_cq_uk *ukcq,
				 struct irdma_cq_poll_info *cur_cqe,
				 struct ib_wc *entry)
{
	int ret = irdma_uk_cq_poll_cmpl(ukcq, cur_cqe);

	if (ret)
		return ret;

	irdma_process_cqe(entry, cur_cqe);

	return 0;
}

/**
 * __irdma_poll_cq - poll cq for completion (kernel apps)
 * @iwcq: cq to poll
 * @num_entries: number of entries to poll
 * @entry: wr of a completed entry
 */
static int __irdma_poll_cq(struct irdma_cq *iwcq, int num_entries, struct ib_wc *entry)
{
	struct list_head *tmp_node, *list_node;
	struct irdma_cq_buf *last_buf = NULL;
	struct irdma_cq_poll_info *cur_cqe = &iwcq->cur_cqe;
	struct irdma_cq_buf *cq_buf;
	int ret;
	struct irdma_device *iwdev;
	struct irdma_cq_uk *ukcq;
	bool cq_new_cqe = false;
	int resized_bufs = 0;
	int npolled = 0;

	iwdev = to_iwdev(iwcq->ibcq.device);
	ukcq = &iwcq->sc_cq.cq_uk;

	/* go through the list of previously resized CQ buffers */
	list_for_each_safe(list_node, tmp_node, &iwcq->resize_list) {
		cq_buf = container_of(list_node, struct irdma_cq_buf, list);
		while (npolled < num_entries) {
			ret = irdma_poll_one(&cq_buf->cq_uk, cur_cqe, entry + npolled);
			if (!ret) {
				++npolled;
				cq_new_cqe = true;
				continue;
			}
			if (ret == -ENOENT)
				break;
			 /* QP using the CQ is destroyed. Skip reporting this CQE */
			if (ret == -EFAULT) {
				cq_new_cqe = true;
				continue;
			}
			goto error;
		}

		/* save the resized CQ buffer which received the last cqe */
		if (cq_new_cqe)
			last_buf = cq_buf;
		cq_new_cqe = false;
	}

	/* check the current CQ for new cqes */
	while (npolled < num_entries) {
		ret = irdma_poll_one(ukcq, cur_cqe, entry + npolled);
		if (ret == -ENOENT) {
			ret = irdma_generated_cmpls(iwcq, cur_cqe);
			if (!ret)
				irdma_process_cqe(entry + npolled, cur_cqe);
		}
		if (!ret) {
			++npolled;
			cq_new_cqe = true;
			continue;
		}

		if (ret == -ENOENT)
			break;
		/* QP using the CQ is destroyed. Skip reporting this CQE */
		if (ret == -EFAULT) {
			cq_new_cqe = true;
			continue;
		}
		goto error;
	}

	if (cq_new_cqe)
		/* all previous CQ resizes are complete */
		resized_bufs = irdma_process_resize_list(iwcq, iwdev, NULL);
	else if (last_buf)
		/* only CQ resizes up to the last_buf are complete */
		resized_bufs = irdma_process_resize_list(iwcq, iwdev, last_buf);
	if (resized_bufs)
		/* report to the HW the number of complete CQ resizes */
		irdma_uk_cq_set_resized_cnt(ukcq, resized_bufs);

	return npolled;
error:
	ibdev_dbg(&iwdev->ibdev, "%s: Error polling CQ, irdma_err: %d\n",
		  __func__, ret);

	return ret;
}

/**
 * irdma_poll_cq - poll cq for completion (kernel apps)
 * @ibcq: cq to poll
 * @num_entries: number of entries to poll
 * @entry: wr of a completed entry
 */
static int irdma_poll_cq(struct ib_cq *ibcq, int num_entries,
			 struct ib_wc *entry)
{
	struct irdma_cq *iwcq;
	unsigned long flags;
	int ret;

	iwcq = to_iwcq(ibcq);

	spin_lock_irqsave(&iwcq->lock, flags);
	ret = __irdma_poll_cq(iwcq, num_entries, entry);
	spin_unlock_irqrestore(&iwcq->lock, flags);

	return ret;
}

/**
 * irdma_req_notify_cq - arm cq kernel application
 * @ibcq: cq to arm
 * @notify_flags: notofication flags
 */
static int irdma_req_notify_cq(struct ib_cq *ibcq,
			       enum ib_cq_notify_flags notify_flags)
{
	struct irdma_cq *iwcq;
	struct irdma_cq_uk *ukcq;
	unsigned long flags;
	enum irdma_cmpl_notify cq_notify;
	bool promo_event = false;
	int ret = 0;

	cq_notify = notify_flags == IB_CQ_SOLICITED ?
		    IRDMA_CQ_COMPL_SOLICITED : IRDMA_CQ_COMPL_EVENT;
	iwcq = to_iwcq(ibcq);
	ukcq = &iwcq->sc_cq.cq_uk;

	spin_lock_irqsave(&iwcq->lock, flags);
	/* Only promote to arm the CQ for any event if the last arm event was solicited. */
	if (iwcq->last_notify == IRDMA_CQ_COMPL_SOLICITED && notify_flags != IB_CQ_SOLICITED)
		promo_event = true;

	if (!atomic_cmpxchg(&iwcq->armed, 0, 1) || promo_event) {
		iwcq->last_notify = cq_notify;
		irdma_uk_cq_request_notification(ukcq, cq_notify);
	}

	if ((notify_flags & IB_CQ_REPORT_MISSED_EVENTS) &&
	    (!irdma_cq_empty(iwcq) || !list_empty(&iwcq->cmpl_generated)))
		ret = 1;
	spin_unlock_irqrestore(&iwcq->lock, flags);

	return ret;
}

static int irdma_roce_port_immutable(struct ib_device *ibdev, u32 port_num,
				     struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	immutable->core_cap_flags = RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;
	err = ib_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->max_mad_size = IB_MGMT_MAD_SIZE;
	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;

	return 0;
}

static int irdma_iw_port_immutable(struct ib_device *ibdev, u32 port_num,
				   struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	immutable->core_cap_flags = RDMA_CORE_PORT_IWARP;
	err = ib_query_port(ibdev, port_num, &attr);
	if (err)
		return err;
	immutable->gid_tbl_len = attr.gid_tbl_len;

	return 0;
}

static const struct rdma_stat_desc irdma_hw_stat_names[] = {
	/* gen1 - 32-bit */
	[IRDMA_HW_STAT_INDEX_IP4RXDISCARD].name		= "ip4InDiscards",
	[IRDMA_HW_STAT_INDEX_IP4RXTRUNC].name		= "ip4InTruncatedPkts",
	[IRDMA_HW_STAT_INDEX_IP4TXNOROUTE].name		= "ip4OutNoRoutes",
	[IRDMA_HW_STAT_INDEX_IP6RXDISCARD].name		= "ip6InDiscards",
	[IRDMA_HW_STAT_INDEX_IP6RXTRUNC].name		= "ip6InTruncatedPkts",
	[IRDMA_HW_STAT_INDEX_IP6TXNOROUTE].name		= "ip6OutNoRoutes",
	[IRDMA_HW_STAT_INDEX_TCPRTXSEG].name		= "tcpRetransSegs",
	[IRDMA_HW_STAT_INDEX_TCPRXOPTERR].name		= "tcpInOptErrors",
	[IRDMA_HW_STAT_INDEX_TCPRXPROTOERR].name	= "tcpInProtoErrors",
	[IRDMA_HW_STAT_INDEX_RXVLANERR].name		= "rxVlanErrors",
	/* gen1 - 64-bit */
	[IRDMA_HW_STAT_INDEX_IP4RXOCTS].name		= "ip4InOctets",
	[IRDMA_HW_STAT_INDEX_IP4RXPKTS].name		= "ip4InPkts",
	[IRDMA_HW_STAT_INDEX_IP4RXFRAGS].name		= "ip4InReasmRqd",
	[IRDMA_HW_STAT_INDEX_IP4RXMCPKTS].name		= "ip4InMcastPkts",
	[IRDMA_HW_STAT_INDEX_IP4TXOCTS].name		= "ip4OutOctets",
	[IRDMA_HW_STAT_INDEX_IP4TXPKTS].name		= "ip4OutPkts",
	[IRDMA_HW_STAT_INDEX_IP4TXFRAGS].name		= "ip4OutSegRqd",
	[IRDMA_HW_STAT_INDEX_IP4TXMCPKTS].name		= "ip4OutMcastPkts",
	[IRDMA_HW_STAT_INDEX_IP6RXOCTS].name		= "ip6InOctets",
	[IRDMA_HW_STAT_INDEX_IP6RXPKTS].name		= "ip6InPkts",
	[IRDMA_HW_STAT_INDEX_IP6RXFRAGS].name		= "ip6InReasmRqd",
	[IRDMA_HW_STAT_INDEX_IP6RXMCPKTS].name		= "ip6InMcastPkts",
	[IRDMA_HW_STAT_INDEX_IP6TXOCTS].name		= "ip6OutOctets",
	[IRDMA_HW_STAT_INDEX_IP6TXPKTS].name		= "ip6OutPkts",
	[IRDMA_HW_STAT_INDEX_IP6TXFRAGS].name		= "ip6OutSegRqd",
	[IRDMA_HW_STAT_INDEX_IP6TXMCPKTS].name		= "ip6OutMcastPkts",
	[IRDMA_HW_STAT_INDEX_TCPRXSEGS].name		= "tcpInSegs",
	[IRDMA_HW_STAT_INDEX_TCPTXSEG].name		= "tcpOutSegs",
	[IRDMA_HW_STAT_INDEX_RDMARXRDS].name		= "iwInRdmaReads",
	[IRDMA_HW_STAT_INDEX_RDMARXSNDS].name		= "iwInRdmaSends",
	[IRDMA_HW_STAT_INDEX_RDMARXWRS].name		= "iwInRdmaWrites",
	[IRDMA_HW_STAT_INDEX_RDMATXRDS].name		= "iwOutRdmaReads",
	[IRDMA_HW_STAT_INDEX_RDMATXSNDS].name		= "iwOutRdmaSends",
	[IRDMA_HW_STAT_INDEX_RDMATXWRS].name		= "iwOutRdmaWrites",
	[IRDMA_HW_STAT_INDEX_RDMAVBND].name		= "iwRdmaBnd",
	[IRDMA_HW_STAT_INDEX_RDMAVINV].name		= "iwRdmaInv",

	/* gen2 - 32-bit */
	[IRDMA_HW_STAT_INDEX_RXRPCNPHANDLED].name	= "cnpHandled",
	[IRDMA_HW_STAT_INDEX_RXRPCNPIGNORED].name	= "cnpIgnored",
	[IRDMA_HW_STAT_INDEX_TXNPCNPSENT].name		= "cnpSent",
	/* gen2 - 64-bit */
	[IRDMA_HW_STAT_INDEX_IP4RXMCOCTS].name		= "ip4InMcastOctets",
	[IRDMA_HW_STAT_INDEX_IP4TXMCOCTS].name		= "ip4OutMcastOctets",
	[IRDMA_HW_STAT_INDEX_IP6RXMCOCTS].name		= "ip6InMcastOctets",
	[IRDMA_HW_STAT_INDEX_IP6TXMCOCTS].name		= "ip6OutMcastOctets",
	[IRDMA_HW_STAT_INDEX_UDPRXPKTS].name		= "RxUDP",
	[IRDMA_HW_STAT_INDEX_UDPTXPKTS].name		= "TxUDP",
	[IRDMA_HW_STAT_INDEX_RXNPECNMARKEDPKTS].name	= "RxECNMrkd",

};

static void irdma_get_dev_fw_str(struct ib_device *dev, char *str)
{
	struct irdma_device *iwdev = to_iwdev(dev);

	snprintf(str, IB_FW_VERSION_NAME_MAX, "%u.%u",
		 irdma_fw_major_ver(&iwdev->rf->sc_dev),
		 irdma_fw_minor_ver(&iwdev->rf->sc_dev));
}

/**
 * irdma_alloc_hw_port_stats - Allocate a hw stats structure
 * @ibdev: device pointer from stack
 * @port_num: port number
 */
static struct rdma_hw_stats *irdma_alloc_hw_port_stats(struct ib_device *ibdev,
						       u32 port_num)
{
	struct irdma_device *iwdev = to_iwdev(ibdev);
	struct irdma_sc_dev *dev = &iwdev->rf->sc_dev;

	int num_counters = dev->hw_attrs.max_stat_idx;
	unsigned long lifespan = RDMA_HW_STATS_DEFAULT_LIFESPAN;

	return rdma_alloc_hw_stats_struct(irdma_hw_stat_names, num_counters,
					  lifespan);
}

/**
 * irdma_get_hw_stats - Populates the rdma_hw_stats structure
 * @ibdev: device pointer from stack
 * @stats: stats pointer from stack
 * @port_num: port number
 * @index: which hw counter the stack is requesting we update
 */
static int irdma_get_hw_stats(struct ib_device *ibdev,
			      struct rdma_hw_stats *stats, u32 port_num,
			      int index)
{
	struct irdma_device *iwdev = to_iwdev(ibdev);
	struct irdma_dev_hw_stats *hw_stats = &iwdev->vsi.pestat->hw_stats;

	if (iwdev->rf->rdma_ver >= IRDMA_GEN_2)
		irdma_cqp_gather_stats_cmd(&iwdev->rf->sc_dev, iwdev->vsi.pestat, true);
	else
		irdma_cqp_gather_stats_gen1(&iwdev->rf->sc_dev, iwdev->vsi.pestat);

	memcpy(&stats->value[0], hw_stats, sizeof(u64) * stats->num_counters);

	return stats->num_counters;
}

/**
 * irdma_query_gid - Query port GID
 * @ibdev: device pointer from stack
 * @port: port number
 * @index: Entry index
 * @gid: Global ID
 */
static int irdma_query_gid(struct ib_device *ibdev, u32 port, int index,
			   union ib_gid *gid)
{
	struct irdma_device *iwdev = to_iwdev(ibdev);

	memset(gid->raw, 0, sizeof(gid->raw));
	ether_addr_copy(gid->raw, iwdev->netdev->dev_addr);

	return 0;
}

/**
 * mcast_list_add -  Add a new mcast item to list
 * @rf: RDMA PCI function
 * @new_elem: pointer to element to add
 */
static void mcast_list_add(struct irdma_pci_f *rf,
			   struct mc_table_list *new_elem)
{
	list_add(&new_elem->list, &rf->mc_qht_list.list);
}

/**
 * mcast_list_del - Remove an mcast item from list
 * @mc_qht_elem: pointer to mcast table list element
 */
static void mcast_list_del(struct mc_table_list *mc_qht_elem)
{
	if (mc_qht_elem)
		list_del(&mc_qht_elem->list);
}

/**
 * mcast_list_lookup_ip - Search mcast list for address
 * @rf: RDMA PCI function
 * @ip_mcast: pointer to mcast IP address
 */
static struct mc_table_list *mcast_list_lookup_ip(struct irdma_pci_f *rf,
						  u32 *ip_mcast)
{
	struct mc_table_list *mc_qht_el;
	struct list_head *pos, *q;

	list_for_each_safe (pos, q, &rf->mc_qht_list.list) {
		mc_qht_el = list_entry(pos, struct mc_table_list, list);
		if (!memcmp(mc_qht_el->mc_info.dest_ip, ip_mcast,
			    sizeof(mc_qht_el->mc_info.dest_ip)))
			return mc_qht_el;
	}

	return NULL;
}

/**
 * irdma_mcast_cqp_op - perform a mcast cqp operation
 * @iwdev: irdma device
 * @mc_grp_ctx: mcast group info
 * @op: operation
 *
 * returns error status
 */
static int irdma_mcast_cqp_op(struct irdma_device *iwdev,
			      struct irdma_mcast_grp_info *mc_grp_ctx, u8 op)
{
	struct cqp_cmds_info *cqp_info;
	struct irdma_cqp_request *cqp_request;
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(&iwdev->rf->cqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_request->info.in.u.mc_create.info = *mc_grp_ctx;
	cqp_info = &cqp_request->info;
	cqp_info->cqp_cmd = op;
	cqp_info->post_sq = 1;
	cqp_info->in.u.mc_create.scratch = (uintptr_t)cqp_request;
	cqp_info->in.u.mc_create.cqp = &iwdev->rf->cqp.sc_cqp;
	status = irdma_handle_cqp_op(iwdev->rf, cqp_request);
	irdma_put_cqp_request(&iwdev->rf->cqp, cqp_request);

	return status;
}

/**
 * irdma_mcast_mac - Get the multicast MAC for an IP address
 * @ip_addr: IPv4 or IPv6 address
 * @mac: pointer to result MAC address
 * @ipv4: flag indicating IPv4 or IPv6
 *
 */
void irdma_mcast_mac(u32 *ip_addr, u8 *mac, bool ipv4)
{
	u8 *ip = (u8 *)ip_addr;

	if (ipv4) {
		unsigned char mac4[ETH_ALEN] = {0x01, 0x00, 0x5E, 0x00,
						0x00, 0x00};

		mac4[3] = ip[2] & 0x7F;
		mac4[4] = ip[1];
		mac4[5] = ip[0];
		ether_addr_copy(mac, mac4);
	} else {
		unsigned char mac6[ETH_ALEN] = {0x33, 0x33, 0x00, 0x00,
						0x00, 0x00};

		mac6[2] = ip[3];
		mac6[3] = ip[2];
		mac6[4] = ip[1];
		mac6[5] = ip[0];
		ether_addr_copy(mac, mac6);
	}
}

/**
 * irdma_attach_mcast - attach a qp to a multicast group
 * @ibqp: ptr to qp
 * @ibgid: pointer to global ID
 * @lid: local ID
 *
 * returns error status
 */
static int irdma_attach_mcast(struct ib_qp *ibqp, union ib_gid *ibgid, u16 lid)
{
	struct irdma_qp *iwqp = to_iwqp(ibqp);
	struct irdma_device *iwdev = iwqp->iwdev;
	struct irdma_pci_f *rf = iwdev->rf;
	struct mc_table_list *mc_qht_elem;
	struct irdma_mcast_grp_ctx_entry_info mcg_info = {};
	unsigned long flags;
	u32 ip_addr[4] = {};
	u32 mgn;
	u32 no_mgs;
	int ret = 0;
	bool ipv4;
	u16 vlan_id;
	union irdma_sockaddr sgid_addr;
	unsigned char dmac[ETH_ALEN];

	rdma_gid2ip((struct sockaddr *)&sgid_addr, ibgid);

	if (!ipv6_addr_v4mapped((struct in6_addr *)ibgid)) {
		irdma_copy_ip_ntohl(ip_addr,
				    sgid_addr.saddr_in6.sin6_addr.in6_u.u6_addr32);
		irdma_get_vlan_mac_ipv6(ip_addr, &vlan_id, NULL);
		ipv4 = false;
		ibdev_dbg(&iwdev->ibdev,
			  "VERBS: qp_id=%d, IP6address=%pI6\n", ibqp->qp_num,
			  ip_addr);
		irdma_mcast_mac(ip_addr, dmac, false);
	} else {
		ip_addr[0] = ntohl(sgid_addr.saddr_in.sin_addr.s_addr);
		ipv4 = true;
		vlan_id = irdma_get_vlan_ipv4(ip_addr);
		irdma_mcast_mac(ip_addr, dmac, true);
		ibdev_dbg(&iwdev->ibdev,
			  "VERBS: qp_id=%d, IP4address=%pI4, MAC=%pM\n",
			  ibqp->qp_num, ip_addr, dmac);
	}

	spin_lock_irqsave(&rf->qh_list_lock, flags);
	mc_qht_elem = mcast_list_lookup_ip(rf, ip_addr);
	if (!mc_qht_elem) {
		struct irdma_dma_mem *dma_mem_mc;

		spin_unlock_irqrestore(&rf->qh_list_lock, flags);
		mc_qht_elem = kzalloc(sizeof(*mc_qht_elem), GFP_KERNEL);
		if (!mc_qht_elem)
			return -ENOMEM;

		mc_qht_elem->mc_info.ipv4_valid = ipv4;
		memcpy(mc_qht_elem->mc_info.dest_ip, ip_addr,
		       sizeof(mc_qht_elem->mc_info.dest_ip));
		ret = irdma_alloc_rsrc(rf, rf->allocated_mcgs, rf->max_mcg,
				       &mgn, &rf->next_mcg);
		if (ret) {
			kfree(mc_qht_elem);
			return -ENOMEM;
		}

		mc_qht_elem->mc_info.mgn = mgn;
		dma_mem_mc = &mc_qht_elem->mc_grp_ctx.dma_mem_mc;
		dma_mem_mc->size = ALIGN(sizeof(u64) * IRDMA_MAX_MGS_PER_CTX,
					 IRDMA_HW_PAGE_SIZE);
		dma_mem_mc->va = dma_alloc_coherent(rf->hw.device,
						    dma_mem_mc->size,
						    &dma_mem_mc->pa,
						    GFP_KERNEL);
		if (!dma_mem_mc->va) {
			irdma_free_rsrc(rf, rf->allocated_mcgs, mgn);
			kfree(mc_qht_elem);
			return -ENOMEM;
		}

		mc_qht_elem->mc_grp_ctx.mg_id = (u16)mgn;
		memcpy(mc_qht_elem->mc_grp_ctx.dest_ip_addr, ip_addr,
		       sizeof(mc_qht_elem->mc_grp_ctx.dest_ip_addr));
		mc_qht_elem->mc_grp_ctx.ipv4_valid = ipv4;
		mc_qht_elem->mc_grp_ctx.vlan_id = vlan_id;
		if (vlan_id < VLAN_N_VID)
			mc_qht_elem->mc_grp_ctx.vlan_valid = true;
		mc_qht_elem->mc_grp_ctx.hmc_fcn_id = iwdev->rf->sc_dev.hmc_fn_id;
		mc_qht_elem->mc_grp_ctx.qs_handle =
			iwqp->sc_qp.vsi->qos[iwqp->sc_qp.user_pri].qs_handle;
		ether_addr_copy(mc_qht_elem->mc_grp_ctx.dest_mac_addr, dmac);

		spin_lock_irqsave(&rf->qh_list_lock, flags);
		mcast_list_add(rf, mc_qht_elem);
	} else {
		if (mc_qht_elem->mc_grp_ctx.no_of_mgs ==
		    IRDMA_MAX_MGS_PER_CTX) {
			spin_unlock_irqrestore(&rf->qh_list_lock, flags);
			return -ENOMEM;
		}
	}

	mcg_info.qp_id = iwqp->ibqp.qp_num;
	no_mgs = mc_qht_elem->mc_grp_ctx.no_of_mgs;
	irdma_sc_add_mcast_grp(&mc_qht_elem->mc_grp_ctx, &mcg_info);
	spin_unlock_irqrestore(&rf->qh_list_lock, flags);

	/* Only if there is a change do we need to modify or create */
	if (!no_mgs) {
		ret = irdma_mcast_cqp_op(iwdev, &mc_qht_elem->mc_grp_ctx,
					 IRDMA_OP_MC_CREATE);
	} else if (no_mgs != mc_qht_elem->mc_grp_ctx.no_of_mgs) {
		ret = irdma_mcast_cqp_op(iwdev, &mc_qht_elem->mc_grp_ctx,
					 IRDMA_OP_MC_MODIFY);
	} else {
		return 0;
	}

	if (ret)
		goto error;

	return 0;

error:
	irdma_sc_del_mcast_grp(&mc_qht_elem->mc_grp_ctx, &mcg_info);
	if (!mc_qht_elem->mc_grp_ctx.no_of_mgs) {
		mcast_list_del(mc_qht_elem);
		dma_free_coherent(rf->hw.device,
				  mc_qht_elem->mc_grp_ctx.dma_mem_mc.size,
				  mc_qht_elem->mc_grp_ctx.dma_mem_mc.va,
				  mc_qht_elem->mc_grp_ctx.dma_mem_mc.pa);
		mc_qht_elem->mc_grp_ctx.dma_mem_mc.va = NULL;
		irdma_free_rsrc(rf, rf->allocated_mcgs,
				mc_qht_elem->mc_grp_ctx.mg_id);
		kfree(mc_qht_elem);
	}

	return ret;
}

/**
 * irdma_detach_mcast - detach a qp from a multicast group
 * @ibqp: ptr to qp
 * @ibgid: pointer to global ID
 * @lid: local ID
 *
 * returns error status
 */
static int irdma_detach_mcast(struct ib_qp *ibqp, union ib_gid *ibgid, u16 lid)
{
	struct irdma_qp *iwqp = to_iwqp(ibqp);
	struct irdma_device *iwdev = iwqp->iwdev;
	struct irdma_pci_f *rf = iwdev->rf;
	u32 ip_addr[4] = {};
	struct mc_table_list *mc_qht_elem;
	struct irdma_mcast_grp_ctx_entry_info mcg_info = {};
	int ret;
	unsigned long flags;
	union irdma_sockaddr sgid_addr;

	rdma_gid2ip((struct sockaddr *)&sgid_addr, ibgid);
	if (!ipv6_addr_v4mapped((struct in6_addr *)ibgid))
		irdma_copy_ip_ntohl(ip_addr,
				    sgid_addr.saddr_in6.sin6_addr.in6_u.u6_addr32);
	else
		ip_addr[0] = ntohl(sgid_addr.saddr_in.sin_addr.s_addr);

	spin_lock_irqsave(&rf->qh_list_lock, flags);
	mc_qht_elem = mcast_list_lookup_ip(rf, ip_addr);
	if (!mc_qht_elem) {
		spin_unlock_irqrestore(&rf->qh_list_lock, flags);
		ibdev_dbg(&iwdev->ibdev,
			  "VERBS: address not found MCG\n");
		return 0;
	}

	mcg_info.qp_id = iwqp->ibqp.qp_num;
	irdma_sc_del_mcast_grp(&mc_qht_elem->mc_grp_ctx, &mcg_info);
	if (!mc_qht_elem->mc_grp_ctx.no_of_mgs) {
		mcast_list_del(mc_qht_elem);
		spin_unlock_irqrestore(&rf->qh_list_lock, flags);
		ret = irdma_mcast_cqp_op(iwdev, &mc_qht_elem->mc_grp_ctx,
					 IRDMA_OP_MC_DESTROY);
		if (ret) {
			ibdev_dbg(&iwdev->ibdev,
				  "VERBS: failed MC_DESTROY MCG\n");
			spin_lock_irqsave(&rf->qh_list_lock, flags);
			mcast_list_add(rf, mc_qht_elem);
			spin_unlock_irqrestore(&rf->qh_list_lock, flags);
			return -EAGAIN;
		}

		dma_free_coherent(rf->hw.device,
				  mc_qht_elem->mc_grp_ctx.dma_mem_mc.size,
				  mc_qht_elem->mc_grp_ctx.dma_mem_mc.va,
				  mc_qht_elem->mc_grp_ctx.dma_mem_mc.pa);
		mc_qht_elem->mc_grp_ctx.dma_mem_mc.va = NULL;
		irdma_free_rsrc(rf, rf->allocated_mcgs,
				mc_qht_elem->mc_grp_ctx.mg_id);
		kfree(mc_qht_elem);
	} else {
		spin_unlock_irqrestore(&rf->qh_list_lock, flags);
		ret = irdma_mcast_cqp_op(iwdev, &mc_qht_elem->mc_grp_ctx,
					 IRDMA_OP_MC_MODIFY);
		if (ret) {
			ibdev_dbg(&iwdev->ibdev,
				  "VERBS: failed Modify MCG\n");
			return ret;
		}
	}

	return 0;
}

static int irdma_create_hw_ah(struct irdma_device *iwdev, struct irdma_ah *ah, bool sleep)
{
	struct irdma_pci_f *rf = iwdev->rf;
	int err;

	err = irdma_alloc_rsrc(rf, rf->allocated_ahs, rf->max_ah, &ah->sc_ah.ah_info.ah_idx,
			       &rf->next_ah);
	if (err)
		return err;

	err = irdma_ah_cqp_op(rf, &ah->sc_ah, IRDMA_OP_AH_CREATE, sleep,
			      irdma_gsi_ud_qp_ah_cb, &ah->sc_ah);

	if (err) {
		ibdev_dbg(&iwdev->ibdev, "VERBS: CQP-OP Create AH fail");
		goto err_ah_create;
	}

	if (!sleep) {
		int cnt = CQP_COMPL_WAIT_TIME_MS * CQP_TIMEOUT_THRESHOLD;

		do {
			irdma_cqp_ce_handler(rf, &rf->ccq.sc_cq);
			mdelay(1);
		} while (!ah->sc_ah.ah_info.ah_valid && --cnt);

		if (!cnt) {
			ibdev_dbg(&iwdev->ibdev, "VERBS: CQP create AH timed out");
			err = -ETIMEDOUT;
			goto err_ah_create;
		}
	}
	return 0;

err_ah_create:
	irdma_free_rsrc(iwdev->rf, iwdev->rf->allocated_ahs, ah->sc_ah.ah_info.ah_idx);

	return err;
}

static int irdma_setup_ah(struct ib_ah *ibah, struct rdma_ah_init_attr *attr)
{
	struct irdma_pd *pd = to_iwpd(ibah->pd);
	struct irdma_ah *ah = container_of(ibah, struct irdma_ah, ibah);
	struct rdma_ah_attr *ah_attr = attr->ah_attr;
	const struct ib_gid_attr *sgid_attr;
	struct irdma_device *iwdev = to_iwdev(ibah->pd->device);
	struct irdma_pci_f *rf = iwdev->rf;
	struct irdma_sc_ah *sc_ah;
	struct irdma_ah_info *ah_info;
	union irdma_sockaddr sgid_addr, dgid_addr;
	int err;
	u8 dmac[ETH_ALEN];

	ah->pd = pd;
	sc_ah = &ah->sc_ah;
	sc_ah->ah_info.vsi = &iwdev->vsi;
	irdma_sc_init_ah(&rf->sc_dev, sc_ah);
	ah->sgid_index = ah_attr->grh.sgid_index;
	sgid_attr = ah_attr->grh.sgid_attr;
	memcpy(&ah->dgid, &ah_attr->grh.dgid, sizeof(ah->dgid));
	rdma_gid2ip((struct sockaddr *)&sgid_addr, &sgid_attr->gid);
	rdma_gid2ip((struct sockaddr *)&dgid_addr, &ah_attr->grh.dgid);
	ah->av.attrs = *ah_attr;
	ah->av.net_type = rdma_gid_attr_network_type(sgid_attr);
	ah_info = &sc_ah->ah_info;
	ah_info->pd_idx = pd->sc_pd.pd_id;
	if (ah_attr->ah_flags & IB_AH_GRH) {
		ah_info->flow_label = ah_attr->grh.flow_label;
		ah_info->hop_ttl = ah_attr->grh.hop_limit;
		ah_info->tc_tos = ah_attr->grh.traffic_class;
	}

	ether_addr_copy(dmac, ah_attr->roce.dmac);
	if (ah->av.net_type == RDMA_NETWORK_IPV4) {
		ah_info->ipv4_valid = true;
		ah_info->dest_ip_addr[0] =
			ntohl(dgid_addr.saddr_in.sin_addr.s_addr);
		ah_info->src_ip_addr[0] =
			ntohl(sgid_addr.saddr_in.sin_addr.s_addr);
		ah_info->do_lpbk = irdma_ipv4_is_lpb(ah_info->src_ip_addr[0],
						     ah_info->dest_ip_addr[0]);
		if (ipv4_is_multicast(dgid_addr.saddr_in.sin_addr.s_addr)) {
			ah_info->do_lpbk = true;
			irdma_mcast_mac(ah_info->dest_ip_addr, dmac, true);
		}
	} else {
		irdma_copy_ip_ntohl(ah_info->dest_ip_addr,
				    dgid_addr.saddr_in6.sin6_addr.in6_u.u6_addr32);
		irdma_copy_ip_ntohl(ah_info->src_ip_addr,
				    sgid_addr.saddr_in6.sin6_addr.in6_u.u6_addr32);
		ah_info->do_lpbk = irdma_ipv6_is_lpb(ah_info->src_ip_addr,
						     ah_info->dest_ip_addr);
		if (rdma_is_multicast_addr(&dgid_addr.saddr_in6.sin6_addr)) {
			ah_info->do_lpbk = true;
			irdma_mcast_mac(ah_info->dest_ip_addr, dmac, false);
		}
	}

	err = rdma_read_gid_l2_fields(sgid_attr, &ah_info->vlan_tag,
				      ah_info->mac_addr);
	if (err)
		return err;

	ah_info->dst_arpindex = irdma_add_arp(iwdev->rf, ah_info->dest_ip_addr,
					      ah_info->ipv4_valid, dmac);

	if (ah_info->dst_arpindex == -1)
		return -EINVAL;

	if (ah_info->vlan_tag >= VLAN_N_VID && iwdev->dcb_vlan_mode)
		ah_info->vlan_tag = 0;

	if (ah_info->vlan_tag < VLAN_N_VID) {
		u8 prio = rt_tos2priority(ah_info->tc_tos);

		prio = irdma_roce_get_vlan_prio(sgid_attr, prio);

		ah_info->vlan_tag |= (u16)prio << VLAN_PRIO_SHIFT;
		ah_info->insert_vlan_tag = true;
	}

	return 0;
}

/**
 * irdma_ah_exists - Check for existing identical AH
 * @iwdev: irdma device
 * @new_ah: AH to check for
 *
 * returns true if AH is found, false if not found.
 */
static bool irdma_ah_exists(struct irdma_device *iwdev,
			    struct irdma_ah *new_ah)
{
	struct irdma_ah *ah;
	u32 key = new_ah->sc_ah.ah_info.dest_ip_addr[0] ^
		  new_ah->sc_ah.ah_info.dest_ip_addr[1] ^
		  new_ah->sc_ah.ah_info.dest_ip_addr[2] ^
		  new_ah->sc_ah.ah_info.dest_ip_addr[3];

	hash_for_each_possible(iwdev->ah_hash_tbl, ah, list, key) {
		/* Set ah_valid and ah_id the same so memcmp can work */
		new_ah->sc_ah.ah_info.ah_idx = ah->sc_ah.ah_info.ah_idx;
		new_ah->sc_ah.ah_info.ah_valid = ah->sc_ah.ah_info.ah_valid;
		if (!memcmp(&ah->sc_ah.ah_info, &new_ah->sc_ah.ah_info,
			    sizeof(ah->sc_ah.ah_info))) {
			refcount_inc(&ah->refcnt);
			new_ah->parent_ah = ah;
			return true;
		}
	}

	return false;
}

/**
 * irdma_destroy_ah - Destroy address handle
 * @ibah: pointer to address handle
 * @ah_flags: flags for sleepable
 */
static int irdma_destroy_ah(struct ib_ah *ibah, u32 ah_flags)
{
	struct irdma_device *iwdev = to_iwdev(ibah->device);
	struct irdma_ah *ah = to_iwah(ibah);

	if ((ah_flags & RDMA_DESTROY_AH_SLEEPABLE) && ah->parent_ah) {
		mutex_lock(&iwdev->ah_tbl_lock);
		if (!refcount_dec_and_test(&ah->parent_ah->refcnt)) {
			mutex_unlock(&iwdev->ah_tbl_lock);
			return 0;
		}
		hash_del(&ah->parent_ah->list);
		kfree(ah->parent_ah);
		mutex_unlock(&iwdev->ah_tbl_lock);
	}

	irdma_ah_cqp_op(iwdev->rf, &ah->sc_ah, IRDMA_OP_AH_DESTROY,
			false, NULL, ah);

	irdma_free_rsrc(iwdev->rf, iwdev->rf->allocated_ahs,
			ah->sc_ah.ah_info.ah_idx);

	return 0;
}

/**
 * irdma_create_user_ah - create user address handle
 * @ibah: address handle
 * @attr: address handle attributes
 * @udata: User data
 *
 * returns 0 on success, error otherwise
 */
static int irdma_create_user_ah(struct ib_ah *ibah,
				struct rdma_ah_init_attr *attr,
				struct ib_udata *udata)
{
#define IRDMA_CREATE_AH_MIN_RESP_LEN offsetofend(struct irdma_create_ah_resp, rsvd)
	struct irdma_ah *ah = container_of(ibah, struct irdma_ah, ibah);
	struct irdma_device *iwdev = to_iwdev(ibah->pd->device);
	struct irdma_create_ah_resp uresp;
	struct irdma_ah *parent_ah;
	int err;

	if (udata && udata->outlen < IRDMA_CREATE_AH_MIN_RESP_LEN)
		return -EINVAL;

	err = irdma_setup_ah(ibah, attr);
	if (err)
		return err;
	mutex_lock(&iwdev->ah_tbl_lock);
	if (!irdma_ah_exists(iwdev, ah)) {
		err = irdma_create_hw_ah(iwdev, ah, true);
		if (err) {
			mutex_unlock(&iwdev->ah_tbl_lock);
			return err;
		}
		/* Add new AH to list */
		parent_ah = kmemdup(ah, sizeof(*ah), GFP_KERNEL);
		if (parent_ah) {
			u32 key = parent_ah->sc_ah.ah_info.dest_ip_addr[0] ^
				  parent_ah->sc_ah.ah_info.dest_ip_addr[1] ^
				  parent_ah->sc_ah.ah_info.dest_ip_addr[2] ^
				  parent_ah->sc_ah.ah_info.dest_ip_addr[3];

			ah->parent_ah = parent_ah;
			hash_add(iwdev->ah_hash_tbl, &parent_ah->list, key);
			refcount_set(&parent_ah->refcnt, 1);
		}
	}
	mutex_unlock(&iwdev->ah_tbl_lock);

	uresp.ah_id = ah->sc_ah.ah_info.ah_idx;
	err = ib_copy_to_udata(udata, &uresp, min(sizeof(uresp), udata->outlen));
	if (err)
		irdma_destroy_ah(ibah, attr->flags);

	return err;
}

/**
 * irdma_create_ah - create address handle
 * @ibah: address handle
 * @attr: address handle attributes
 * @udata: NULL
 *
 * returns 0 on success, error otherwise
 */
static int irdma_create_ah(struct ib_ah *ibah, struct rdma_ah_init_attr *attr,
			   struct ib_udata *udata)
{
	struct irdma_ah *ah = container_of(ibah, struct irdma_ah, ibah);
	struct irdma_device *iwdev = to_iwdev(ibah->pd->device);
	int err;

	err = irdma_setup_ah(ibah, attr);
	if (err)
		return err;
	err = irdma_create_hw_ah(iwdev, ah, attr->flags & RDMA_CREATE_AH_SLEEPABLE);

	return err;
}

/**
 * irdma_query_ah - Query address handle
 * @ibah: pointer to address handle
 * @ah_attr: address handle attributes
 */
static int irdma_query_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr)
{
	struct irdma_ah *ah = to_iwah(ibah);

	memset(ah_attr, 0, sizeof(*ah_attr));
	if (ah->av.attrs.ah_flags & IB_AH_GRH) {
		ah_attr->ah_flags = IB_AH_GRH;
		ah_attr->grh.flow_label = ah->sc_ah.ah_info.flow_label;
		ah_attr->grh.traffic_class = ah->sc_ah.ah_info.tc_tos;
		ah_attr->grh.hop_limit = ah->sc_ah.ah_info.hop_ttl;
		ah_attr->grh.sgid_index = ah->sgid_index;
		memcpy(&ah_attr->grh.dgid, &ah->dgid,
		       sizeof(ah_attr->grh.dgid));
	}

	return 0;
}

static enum rdma_link_layer irdma_get_link_layer(struct ib_device *ibdev,
						 u32 port_num)
{
	return IB_LINK_LAYER_ETHERNET;
}

static const struct ib_device_ops irdma_roce_dev_ops = {
	.attach_mcast = irdma_attach_mcast,
	.create_ah = irdma_create_ah,
	.create_user_ah = irdma_create_user_ah,
	.destroy_ah = irdma_destroy_ah,
	.detach_mcast = irdma_detach_mcast,
	.get_link_layer = irdma_get_link_layer,
	.get_port_immutable = irdma_roce_port_immutable,
	.modify_qp = irdma_modify_qp_roce,
	.query_ah = irdma_query_ah,
	.query_pkey = irdma_query_pkey,
};

static const struct ib_device_ops irdma_iw_dev_ops = {
	.get_port_immutable = irdma_iw_port_immutable,
	.iw_accept = irdma_accept,
	.iw_add_ref = irdma_qp_add_ref,
	.iw_connect = irdma_connect,
	.iw_create_listen = irdma_create_listen,
	.iw_destroy_listen = irdma_destroy_listen,
	.iw_get_qp = irdma_get_qp,
	.iw_reject = irdma_reject,
	.iw_rem_ref = irdma_qp_rem_ref,
	.modify_qp = irdma_modify_qp,
	.query_gid = irdma_query_gid,
};

static const struct ib_device_ops irdma_dev_ops = {
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_IRDMA,
	.uverbs_abi_ver = IRDMA_ABI_VER,

	.alloc_hw_port_stats = irdma_alloc_hw_port_stats,
	.alloc_mr = irdma_alloc_mr,
	.alloc_mw = irdma_alloc_mw,
	.alloc_pd = irdma_alloc_pd,
	.alloc_ucontext = irdma_alloc_ucontext,
	.create_cq = irdma_create_cq,
	.create_qp = irdma_create_qp,
	.dealloc_driver = irdma_ib_dealloc_device,
	.dealloc_mw = irdma_dealloc_mw,
	.dealloc_pd = irdma_dealloc_pd,
	.dealloc_ucontext = irdma_dealloc_ucontext,
	.dereg_mr = irdma_dereg_mr,
	.destroy_cq = irdma_destroy_cq,
	.destroy_qp = irdma_destroy_qp,
	.disassociate_ucontext = irdma_disassociate_ucontext,
	.get_dev_fw_str = irdma_get_dev_fw_str,
	.get_dma_mr = irdma_get_dma_mr,
	.get_hw_stats = irdma_get_hw_stats,
	.map_mr_sg = irdma_map_mr_sg,
	.mmap = irdma_mmap,
	.mmap_free = irdma_mmap_free,
	.poll_cq = irdma_poll_cq,
	.post_recv = irdma_post_recv,
	.post_send = irdma_post_send,
	.query_device = irdma_query_device,
	.query_port = irdma_query_port,
	.query_qp = irdma_query_qp,
	.reg_user_mr = irdma_reg_user_mr,
	.reg_user_mr_dmabuf = irdma_reg_user_mr_dmabuf,
	.req_notify_cq = irdma_req_notify_cq,
	.resize_cq = irdma_resize_cq,
	INIT_RDMA_OBJ_SIZE(ib_pd, irdma_pd, ibpd),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, irdma_ucontext, ibucontext),
	INIT_RDMA_OBJ_SIZE(ib_ah, irdma_ah, ibah),
	INIT_RDMA_OBJ_SIZE(ib_cq, irdma_cq, ibcq),
	INIT_RDMA_OBJ_SIZE(ib_mw, irdma_mr, ibmw),
	INIT_RDMA_OBJ_SIZE(ib_qp, irdma_qp, ibqp),
};

/**
 * irdma_init_roce_device - initialization of roce rdma device
 * @iwdev: irdma device
 */
static void irdma_init_roce_device(struct irdma_device *iwdev)
{
	iwdev->ibdev.node_type = RDMA_NODE_IB_CA;
	addrconf_addr_eui48((u8 *)&iwdev->ibdev.node_guid,
			    iwdev->netdev->dev_addr);
	ib_set_device_ops(&iwdev->ibdev, &irdma_roce_dev_ops);
}

/**
 * irdma_init_iw_device - initialization of iwarp rdma device
 * @iwdev: irdma device
 */
static void irdma_init_iw_device(struct irdma_device *iwdev)
{
	struct net_device *netdev = iwdev->netdev;

	iwdev->ibdev.node_type = RDMA_NODE_RNIC;
	addrconf_addr_eui48((u8 *)&iwdev->ibdev.node_guid,
			    netdev->dev_addr);
	memcpy(iwdev->ibdev.iw_ifname, netdev->name,
	       sizeof(iwdev->ibdev.iw_ifname));
	ib_set_device_ops(&iwdev->ibdev, &irdma_iw_dev_ops);
}

/**
 * irdma_init_rdma_device - initialization of rdma device
 * @iwdev: irdma device
 */
static void irdma_init_rdma_device(struct irdma_device *iwdev)
{
	struct pci_dev *pcidev = iwdev->rf->pcidev;

	if (iwdev->roce_mode)
		irdma_init_roce_device(iwdev);
	else
		irdma_init_iw_device(iwdev);

	iwdev->ibdev.phys_port_cnt = 1;
	iwdev->ibdev.num_comp_vectors = iwdev->rf->ceqs_count;
	iwdev->ibdev.dev.parent = &pcidev->dev;
	ib_set_device_ops(&iwdev->ibdev, &irdma_dev_ops);
}

/**
 * irdma_port_ibevent - indicate port event
 * @iwdev: irdma device
 */
void irdma_port_ibevent(struct irdma_device *iwdev)
{
	struct ib_event event;

	event.device = &iwdev->ibdev;
	event.element.port_num = 1;
	event.event =
		iwdev->iw_status ? IB_EVENT_PORT_ACTIVE : IB_EVENT_PORT_ERR;
	ib_dispatch_event(&event);
}

/**
 * irdma_ib_unregister_device - unregister rdma device from IB
 * core
 * @iwdev: irdma device
 */
void irdma_ib_unregister_device(struct irdma_device *iwdev)
{
	iwdev->iw_status = 0;
	irdma_port_ibevent(iwdev);
	ib_unregister_device(&iwdev->ibdev);
}

/**
 * irdma_ib_register_device - register irdma device to IB core
 * @iwdev: irdma device
 */
int irdma_ib_register_device(struct irdma_device *iwdev)
{
	int ret;

	irdma_init_rdma_device(iwdev);

	ret = ib_device_set_netdev(&iwdev->ibdev, iwdev->netdev, 1);
	if (ret)
		goto error;
	dma_set_max_seg_size(iwdev->rf->hw.device, UINT_MAX);
	ret = ib_register_device(&iwdev->ibdev, "irdma%d", iwdev->rf->hw.device);
	if (ret)
		goto error;

	iwdev->iw_status = 1;
	irdma_port_ibevent(iwdev);

	return 0;

error:
	if (ret)
		ibdev_dbg(&iwdev->ibdev, "VERBS: Register RDMA device fail\n");

	return ret;
}

/**
 * irdma_ib_dealloc_device
 * @ibdev: ib device
 *
 * callback from ibdev dealloc_driver to deallocate resources
 * unber irdma device
 */
void irdma_ib_dealloc_device(struct ib_device *ibdev)
{
	struct irdma_device *iwdev = to_iwdev(ibdev);

	irdma_rt_deinit_hw(iwdev);
	irdma_ctrl_deinit_hw(iwdev->rf);
	kfree(iwdev->rf);
}

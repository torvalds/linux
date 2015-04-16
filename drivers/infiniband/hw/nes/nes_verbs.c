/*
 * Copyright (c) 2006 - 2011 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/random.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

#include <rdma/ib_verbs.h>
#include <rdma/iw_cm.h>
#include <rdma/ib_user_verbs.h>

#include "nes.h"

#include <rdma/ib_umem.h>

atomic_t mod_qp_timouts;
atomic_t qps_created;
atomic_t sw_qps_destroyed;

static void nes_unregister_ofa_device(struct nes_ib_device *nesibdev);

/**
 * nes_alloc_mw
 */
static struct ib_mw *nes_alloc_mw(struct ib_pd *ibpd, enum ib_mw_type type)
{
	struct nes_pd *nespd = to_nespd(ibpd);
	struct nes_vnic *nesvnic = to_nesvnic(ibpd->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_cqp_request *cqp_request;
	struct nes_mr *nesmr;
	struct ib_mw *ibmw;
	struct nes_hw_cqp_wqe *cqp_wqe;
	int ret;
	u32 stag;
	u32 stag_index = 0;
	u32 next_stag_index = 0;
	u32 driver_key = 0;
	u8 stag_key = 0;

	if (type != IB_MW_TYPE_1)
		return ERR_PTR(-EINVAL);

	get_random_bytes(&next_stag_index, sizeof(next_stag_index));
	stag_key = (u8)next_stag_index;

	driver_key = 0;

	next_stag_index >>= 8;
	next_stag_index %= nesadapter->max_mr;

	ret = nes_alloc_resource(nesadapter, nesadapter->allocated_mrs,
			nesadapter->max_mr, &stag_index, &next_stag_index, NES_RESOURCE_MW);
	if (ret) {
		return ERR_PTR(ret);
	}

	nesmr = kzalloc(sizeof(*nesmr), GFP_KERNEL);
	if (!nesmr) {
		nes_free_resource(nesadapter, nesadapter->allocated_mrs, stag_index);
		return ERR_PTR(-ENOMEM);
	}

	stag = stag_index << 8;
	stag |= driver_key;
	stag += (u32)stag_key;

	nes_debug(NES_DBG_MR, "Registering STag 0x%08X, index = 0x%08X\n",
			stag, stag_index);

	/* Register the region with the adapter */
	cqp_request = nes_get_cqp_request(nesdev);
	if (cqp_request == NULL) {
		kfree(nesmr);
		nes_free_resource(nesadapter, nesadapter->allocated_mrs, stag_index);
		return ERR_PTR(-ENOMEM);
	}

	cqp_request->waiting = 1;
	cqp_wqe = &cqp_request->cqp_wqe;

	cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] =
			cpu_to_le32( NES_CQP_ALLOCATE_STAG | NES_CQP_STAG_RIGHTS_REMOTE_READ |
			NES_CQP_STAG_RIGHTS_REMOTE_WRITE | NES_CQP_STAG_VA_TO |
			NES_CQP_STAG_REM_ACC_EN);

	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_LEN_HIGH_PD_IDX, (nespd->pd_id & 0x00007fff));
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_STAG_IDX, stag);

	atomic_set(&cqp_request->refcount, 2);
	nes_post_cqp_request(nesdev, cqp_request);

	/* Wait for CQP */
	ret = wait_event_timeout(cqp_request->waitq, (cqp_request->request_done != 0),
			NES_EVENT_TIMEOUT);
	nes_debug(NES_DBG_MR, "Register STag 0x%08X completed, wait_event_timeout ret = %u,"
			" CQP Major:Minor codes = 0x%04X:0x%04X.\n",
			stag, ret, cqp_request->major_code, cqp_request->minor_code);
	if ((!ret) || (cqp_request->major_code)) {
		nes_put_cqp_request(nesdev, cqp_request);
		kfree(nesmr);
		nes_free_resource(nesadapter, nesadapter->allocated_mrs, stag_index);
		if (!ret) {
			return ERR_PTR(-ETIME);
		} else {
			return ERR_PTR(-ENOMEM);
		}
	}
	nes_put_cqp_request(nesdev, cqp_request);

	nesmr->ibmw.rkey = stag;
	nesmr->mode = IWNES_MEMREG_TYPE_MW;
	ibmw = &nesmr->ibmw;
	nesmr->pbl_4k = 0;
	nesmr->pbls_used = 0;

	return ibmw;
}


/**
 * nes_dealloc_mw
 */
static int nes_dealloc_mw(struct ib_mw *ibmw)
{
	struct nes_mr *nesmr = to_nesmw(ibmw);
	struct nes_vnic *nesvnic = to_nesvnic(ibmw->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_cqp_request *cqp_request;
	int err = 0;
	int ret;

	/* Deallocate the window with the adapter */
	cqp_request = nes_get_cqp_request(nesdev);
	if (cqp_request == NULL) {
		nes_debug(NES_DBG_MR, "Failed to get a cqp_request.\n");
		return -ENOMEM;
	}
	cqp_request->waiting = 1;
	cqp_wqe = &cqp_request->cqp_wqe;
	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX, NES_CQP_DEALLOCATE_STAG);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_STAG_IDX, ibmw->rkey);

	atomic_set(&cqp_request->refcount, 2);
	nes_post_cqp_request(nesdev, cqp_request);

	/* Wait for CQP */
	nes_debug(NES_DBG_MR, "Waiting for deallocate STag 0x%08X to complete.\n",
			ibmw->rkey);
	ret = wait_event_timeout(cqp_request->waitq, (0 != cqp_request->request_done),
			NES_EVENT_TIMEOUT);
	nes_debug(NES_DBG_MR, "Deallocate STag completed, wait_event_timeout ret = %u,"
			" CQP Major:Minor codes = 0x%04X:0x%04X.\n",
			ret, cqp_request->major_code, cqp_request->minor_code);
	if (!ret)
		err = -ETIME;
	else if (cqp_request->major_code)
		err = -EIO;

	nes_put_cqp_request(nesdev, cqp_request);

	nes_free_resource(nesadapter, nesadapter->allocated_mrs,
			(ibmw->rkey & 0x0fffff00) >> 8);
	kfree(nesmr);

	return err;
}


/**
 * nes_bind_mw
 */
static int nes_bind_mw(struct ib_qp *ibqp, struct ib_mw *ibmw,
		struct ib_mw_bind *ibmw_bind)
{
	u64 u64temp;
	struct nes_vnic *nesvnic = to_nesvnic(ibqp->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	/* struct nes_mr *nesmr = to_nesmw(ibmw); */
	struct nes_qp *nesqp = to_nesqp(ibqp);
	struct nes_hw_qp_wqe *wqe;
	unsigned long flags = 0;
	u32 head;
	u32 wqe_misc = 0;
	u32 qsize;

	if (nesqp->ibqp_state > IB_QPS_RTS)
		return -EINVAL;

	spin_lock_irqsave(&nesqp->lock, flags);

	head = nesqp->hwqp.sq_head;
	qsize = nesqp->hwqp.sq_tail;

	/* Check for SQ overflow */
	if (((head + (2 * qsize) - nesqp->hwqp.sq_tail) % qsize) == (qsize - 1)) {
		spin_unlock_irqrestore(&nesqp->lock, flags);
		return -ENOMEM;
	}

	wqe = &nesqp->hwqp.sq_vbase[head];
	/* nes_debug(NES_DBG_MR, "processing sq wqe at %p, head = %u.\n", wqe, head); */
	nes_fill_init_qp_wqe(wqe, nesqp, head);
	u64temp = ibmw_bind->wr_id;
	set_wqe_64bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_COMP_SCRATCH_LOW_IDX, u64temp);
	wqe_misc = NES_IWARP_SQ_OP_BIND;

	wqe_misc |= NES_IWARP_SQ_WQE_LOCAL_FENCE;

	if (ibmw_bind->send_flags & IB_SEND_SIGNALED)
		wqe_misc |= NES_IWARP_SQ_WQE_SIGNALED_COMPL;

	if (ibmw_bind->bind_info.mw_access_flags & IB_ACCESS_REMOTE_WRITE)
		wqe_misc |= NES_CQP_STAG_RIGHTS_REMOTE_WRITE;
	if (ibmw_bind->bind_info.mw_access_flags & IB_ACCESS_REMOTE_READ)
		wqe_misc |= NES_CQP_STAG_RIGHTS_REMOTE_READ;

	set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_MISC_IDX, wqe_misc);
	set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_BIND_WQE_MR_IDX,
			    ibmw_bind->bind_info.mr->lkey);
	set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_BIND_WQE_MW_IDX, ibmw->rkey);
	set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_BIND_WQE_LENGTH_LOW_IDX,
			ibmw_bind->bind_info.length);
	wqe->wqe_words[NES_IWARP_SQ_BIND_WQE_LENGTH_HIGH_IDX] = 0;
	u64temp = (u64)ibmw_bind->bind_info.addr;
	set_wqe_64bit_value(wqe->wqe_words, NES_IWARP_SQ_BIND_WQE_VA_FBO_LOW_IDX, u64temp);

	head++;
	if (head >= qsize)
		head = 0;

	nesqp->hwqp.sq_head = head;
	barrier();

	nes_write32(nesdev->regs+NES_WQE_ALLOC,
			(1 << 24) | 0x00800000 | nesqp->hwqp.qp_id);

	spin_unlock_irqrestore(&nesqp->lock, flags);

	return 0;
}


/*
 * nes_alloc_fast_mr
 */
static int alloc_fast_reg_mr(struct nes_device *nesdev, struct nes_pd *nespd,
			     u32 stag, u32 page_count)
{
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_cqp_request *cqp_request;
	unsigned long flags;
	int ret;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	u32 opcode = 0;
	u16 major_code;
	u64 region_length = page_count * PAGE_SIZE;


	cqp_request = nes_get_cqp_request(nesdev);
	if (cqp_request == NULL) {
		nes_debug(NES_DBG_MR, "Failed to get a cqp_request.\n");
		return -ENOMEM;
	}
	nes_debug(NES_DBG_MR, "alloc_fast_reg_mr: page_count = %d, "
			      "region_length = %llu\n",
			      page_count, region_length);
	cqp_request->waiting = 1;
	cqp_wqe = &cqp_request->cqp_wqe;

	spin_lock_irqsave(&nesadapter->pbl_lock, flags);
	if (nesadapter->free_4kpbl > 0) {
		nesadapter->free_4kpbl--;
		spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
	} else {
		/* No 4kpbl's available: */
		spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
		nes_debug(NES_DBG_MR, "Out of Pbls\n");
		nes_free_cqp_request(nesdev, cqp_request);
		return -ENOMEM;
	}

	opcode = NES_CQP_ALLOCATE_STAG | NES_CQP_STAG_MR |
		 NES_CQP_STAG_PBL_BLK_SIZE | NES_CQP_STAG_VA_TO |
		 NES_CQP_STAG_REM_ACC_EN;
	/*
	 * The current OFED API does not support the zero based TO option.
	 * If added then need to changed the NES_CQP_STAG_VA* option.  Also,
	 * the API does not support that ability to have the MR set for local
	 * access only when created and not allow the SQ op to override. Given
	 * this the remote enable must be set here.
	 */

	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX, opcode);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_PBL_BLK_COUNT_IDX, 1);

	cqp_wqe->wqe_words[NES_CQP_STAG_WQE_LEN_HIGH_PD_IDX] =
			cpu_to_le32((u32)(region_length >> 8) & 0xff000000);
	cqp_wqe->wqe_words[NES_CQP_STAG_WQE_LEN_HIGH_PD_IDX] |=
			cpu_to_le32(nespd->pd_id & 0x00007fff);

	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_STAG_IDX, stag);
	set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_VA_LOW_IDX, 0);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_LEN_LOW_IDX, 0);
	set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_PA_LOW_IDX, 0);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_PBL_LEN_IDX, (page_count * 8));
	cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] |= cpu_to_le32(NES_CQP_STAG_PBL_BLK_SIZE);
	barrier();

	atomic_set(&cqp_request->refcount, 2);
	nes_post_cqp_request(nesdev, cqp_request);

	/* Wait for CQP */
	ret = wait_event_timeout(cqp_request->waitq,
				 (0 != cqp_request->request_done),
				 NES_EVENT_TIMEOUT);

	nes_debug(NES_DBG_MR, "Allocate STag 0x%08X completed, "
		  "wait_event_timeout ret = %u, CQP Major:Minor codes = "
		  "0x%04X:0x%04X.\n", stag, ret, cqp_request->major_code,
		  cqp_request->minor_code);
	major_code = cqp_request->major_code;
	nes_put_cqp_request(nesdev, cqp_request);

	if (!ret || major_code) {
		spin_lock_irqsave(&nesadapter->pbl_lock, flags);
		nesadapter->free_4kpbl++;
		spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
	}

	if (!ret)
		return -ETIME;
	else if (major_code)
		return -EIO;
	return 0;
}

/*
 * nes_alloc_fast_reg_mr
 */
static struct ib_mr *nes_alloc_fast_reg_mr(struct ib_pd *ibpd, int max_page_list_len)
{
	struct nes_pd *nespd = to_nespd(ibpd);
	struct nes_vnic *nesvnic = to_nesvnic(ibpd->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;

	u32 next_stag_index;
	u8 stag_key = 0;
	u32 driver_key = 0;
	int err = 0;
	u32 stag_index = 0;
	struct nes_mr *nesmr;
	u32 stag;
	int ret;
	struct ib_mr *ibmr;
/*
 * Note:  Set to always use a fixed length single page entry PBL.  This is to allow
 *	 for the fast_reg_mr operation to always know the size of the PBL.
 */
	if (max_page_list_len > (NES_4K_PBL_CHUNK_SIZE / sizeof(u64)))
		return ERR_PTR(-E2BIG);

	get_random_bytes(&next_stag_index, sizeof(next_stag_index));
	stag_key = (u8)next_stag_index;
	next_stag_index >>= 8;
	next_stag_index %= nesadapter->max_mr;

	err = nes_alloc_resource(nesadapter, nesadapter->allocated_mrs,
				 nesadapter->max_mr, &stag_index,
				 &next_stag_index, NES_RESOURCE_FAST_MR);
	if (err)
		return ERR_PTR(err);

	nesmr = kzalloc(sizeof(*nesmr), GFP_KERNEL);
	if (!nesmr) {
		nes_free_resource(nesadapter, nesadapter->allocated_mrs, stag_index);
		return ERR_PTR(-ENOMEM);
	}

	stag = stag_index << 8;
	stag |= driver_key;
	stag += (u32)stag_key;

	nes_debug(NES_DBG_MR, "Allocating STag 0x%08X index = 0x%08X\n",
		  stag, stag_index);

	ret = alloc_fast_reg_mr(nesdev, nespd, stag, max_page_list_len);

	if (ret == 0) {
		nesmr->ibmr.rkey = stag;
		nesmr->ibmr.lkey = stag;
		nesmr->mode = IWNES_MEMREG_TYPE_FMEM;
		ibmr = &nesmr->ibmr;
	} else {
		kfree(nesmr);
		nes_free_resource(nesadapter, nesadapter->allocated_mrs, stag_index);
		ibmr = ERR_PTR(-ENOMEM);
	}
	return ibmr;
}

/*
 * nes_alloc_fast_reg_page_list
 */
static struct ib_fast_reg_page_list *nes_alloc_fast_reg_page_list(
							struct ib_device *ibdev,
							int page_list_len)
{
	struct nes_vnic *nesvnic = to_nesvnic(ibdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct ib_fast_reg_page_list *pifrpl;
	struct nes_ib_fast_reg_page_list *pnesfrpl;

	if (page_list_len > (NES_4K_PBL_CHUNK_SIZE / sizeof(u64)))
		return ERR_PTR(-E2BIG);
	/*
	 * Allocate the ib_fast_reg_page_list structure, the
	 * nes_fast_bpl structure, and the PLB table.
	 */
	pnesfrpl = kmalloc(sizeof(struct nes_ib_fast_reg_page_list) +
			   page_list_len * sizeof(u64), GFP_KERNEL);

	if (!pnesfrpl)
		return ERR_PTR(-ENOMEM);

	pifrpl = &pnesfrpl->ibfrpl;
	pifrpl->page_list = &pnesfrpl->pbl;
	pifrpl->max_page_list_len = page_list_len;
	/*
	 * Allocate the WQE PBL
	 */
	pnesfrpl->nes_wqe_pbl.kva = pci_alloc_consistent(nesdev->pcidev,
							 page_list_len * sizeof(u64),
							 &pnesfrpl->nes_wqe_pbl.paddr);

	if (!pnesfrpl->nes_wqe_pbl.kva) {
		kfree(pnesfrpl);
		return ERR_PTR(-ENOMEM);
	}
	nes_debug(NES_DBG_MR, "nes_alloc_fast_reg_pbl: nes_frpl = %p, "
		  "ibfrpl = %p, ibfrpl.page_list = %p, pbl.kva = %p, "
		  "pbl.paddr = %llx\n", pnesfrpl, &pnesfrpl->ibfrpl,
		  pnesfrpl->ibfrpl.page_list, pnesfrpl->nes_wqe_pbl.kva,
		  (unsigned long long) pnesfrpl->nes_wqe_pbl.paddr);

	return pifrpl;
}

/*
 * nes_free_fast_reg_page_list
 */
static void nes_free_fast_reg_page_list(struct ib_fast_reg_page_list *pifrpl)
{
	struct nes_vnic *nesvnic = to_nesvnic(pifrpl->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_ib_fast_reg_page_list *pnesfrpl;

	pnesfrpl = container_of(pifrpl, struct nes_ib_fast_reg_page_list, ibfrpl);
	/*
	 * Free the WQE PBL.
	 */
	pci_free_consistent(nesdev->pcidev,
			    pifrpl->max_page_list_len * sizeof(u64),
			    pnesfrpl->nes_wqe_pbl.kva,
			    pnesfrpl->nes_wqe_pbl.paddr);
	/*
	 * Free the PBL structure
	 */
	kfree(pnesfrpl);
}

/**
 * nes_query_device
 */
static int nes_query_device(struct ib_device *ibdev, struct ib_device_attr *props)
{
	struct nes_vnic *nesvnic = to_nesvnic(ibdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_ib_device *nesibdev = nesvnic->nesibdev;

	memset(props, 0, sizeof(*props));
	memcpy(&props->sys_image_guid, nesvnic->netdev->dev_addr, 6);

	props->fw_ver = nesdev->nesadapter->firmware_version;
	props->device_cap_flags = nesdev->nesadapter->device_cap_flags;
	props->vendor_id = nesdev->nesadapter->vendor_id;
	props->vendor_part_id = nesdev->nesadapter->vendor_part_id;
	props->hw_ver = nesdev->nesadapter->hw_rev;
	props->max_mr_size = 0x80000000;
	props->max_qp = nesibdev->max_qp;
	props->max_qp_wr = nesdev->nesadapter->max_qp_wr - 2;
	props->max_sge = nesdev->nesadapter->max_sge;
	props->max_cq = nesibdev->max_cq;
	props->max_cqe = nesdev->nesadapter->max_cqe;
	props->max_mr = nesibdev->max_mr;
	props->max_mw = nesibdev->max_mr;
	props->max_pd = nesibdev->max_pd;
	props->max_sge_rd = 1;
	switch (nesdev->nesadapter->max_irrq_wr) {
		case 0:
			props->max_qp_rd_atom = 2;
			break;
		case 1:
			props->max_qp_rd_atom = 8;
			break;
		case 2:
			props->max_qp_rd_atom = 32;
			break;
		case 3:
			props->max_qp_rd_atom = 64;
			break;
		default:
			props->max_qp_rd_atom = 0;
	}
	props->max_qp_init_rd_atom = props->max_qp_rd_atom;
	props->atomic_cap = IB_ATOMIC_NONE;
	props->max_map_per_fmr = 1;

	return 0;
}


/**
 * nes_query_port
 */
static int nes_query_port(struct ib_device *ibdev, u8 port, struct ib_port_attr *props)
{
	struct nes_vnic *nesvnic = to_nesvnic(ibdev);
	struct net_device *netdev = nesvnic->netdev;

	memset(props, 0, sizeof(*props));

	props->max_mtu = IB_MTU_4096;

	if (netdev->mtu  >= 4096)
		props->active_mtu = IB_MTU_4096;
	else if (netdev->mtu  >= 2048)
		props->active_mtu = IB_MTU_2048;
	else if (netdev->mtu  >= 1024)
		props->active_mtu = IB_MTU_1024;
	else if (netdev->mtu  >= 512)
		props->active_mtu = IB_MTU_512;
	else
		props->active_mtu = IB_MTU_256;

	props->lid = 1;
	props->lmc = 0;
	props->sm_lid = 0;
	props->sm_sl = 0;
	if (netif_queue_stopped(netdev))
		props->state = IB_PORT_DOWN;
	else if (nesvnic->linkup)
		props->state = IB_PORT_ACTIVE;
	else
		props->state = IB_PORT_DOWN;
	props->phys_state = 0;
	props->port_cap_flags = IB_PORT_CM_SUP | IB_PORT_REINIT_SUP |
			IB_PORT_VENDOR_CLASS_SUP | IB_PORT_BOOT_MGMT_SUP;
	props->gid_tbl_len = 1;
	props->pkey_tbl_len = 1;
	props->qkey_viol_cntr = 0;
	props->active_width = IB_WIDTH_4X;
	props->active_speed = IB_SPEED_SDR;
	props->max_msg_sz = 0x80000000;

	return 0;
}


/**
 * nes_query_pkey
 */
static int nes_query_pkey(struct ib_device *ibdev, u8 port, u16 index, u16 *pkey)
{
	*pkey = 0;
	return 0;
}


/**
 * nes_query_gid
 */
static int nes_query_gid(struct ib_device *ibdev, u8 port,
		int index, union ib_gid *gid)
{
	struct nes_vnic *nesvnic = to_nesvnic(ibdev);

	memset(&(gid->raw[0]), 0, sizeof(gid->raw));
	memcpy(&(gid->raw[0]), nesvnic->netdev->dev_addr, 6);

	return 0;
}


/**
 * nes_alloc_ucontext - Allocate the user context data structure. This keeps track
 * of all objects associated with a particular user-mode client.
 */
static struct ib_ucontext *nes_alloc_ucontext(struct ib_device *ibdev,
		struct ib_udata *udata)
{
	struct nes_vnic *nesvnic = to_nesvnic(ibdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_alloc_ucontext_req req;
	struct nes_alloc_ucontext_resp uresp;
	struct nes_ucontext *nes_ucontext;
	struct nes_ib_device *nesibdev = nesvnic->nesibdev;


	if (ib_copy_from_udata(&req, udata, sizeof(struct nes_alloc_ucontext_req))) {
		printk(KERN_ERR PFX "Invalid structure size on allocate user context.\n");
		return ERR_PTR(-EINVAL);
	}

	if (req.userspace_ver != NES_ABI_USERSPACE_VER) {
		printk(KERN_ERR PFX "Invalid userspace driver version detected. Detected version %d, should be %d\n",
			req.userspace_ver, NES_ABI_USERSPACE_VER);
		return ERR_PTR(-EINVAL);
	}


	memset(&uresp, 0, sizeof uresp);

	uresp.max_qps = nesibdev->max_qp;
	uresp.max_pds = nesibdev->max_pd;
	uresp.wq_size = nesdev->nesadapter->max_qp_wr * 2;
	uresp.virtwq = nesadapter->virtwq;
	uresp.kernel_ver = NES_ABI_KERNEL_VER;

	nes_ucontext = kzalloc(sizeof *nes_ucontext, GFP_KERNEL);
	if (!nes_ucontext)
		return ERR_PTR(-ENOMEM);

	nes_ucontext->nesdev = nesdev;
	nes_ucontext->mmap_wq_offset = uresp.max_pds;
	nes_ucontext->mmap_cq_offset = nes_ucontext->mmap_wq_offset +
			((sizeof(struct nes_hw_qp_wqe) * uresp.max_qps * 2) + PAGE_SIZE-1) /
			PAGE_SIZE;


	if (ib_copy_to_udata(udata, &uresp, sizeof uresp)) {
		kfree(nes_ucontext);
		return ERR_PTR(-EFAULT);
	}

	INIT_LIST_HEAD(&nes_ucontext->cq_reg_mem_list);
	INIT_LIST_HEAD(&nes_ucontext->qp_reg_mem_list);
	atomic_set(&nes_ucontext->usecnt, 1);
	return &nes_ucontext->ibucontext;
}


/**
 * nes_dealloc_ucontext
 */
static int nes_dealloc_ucontext(struct ib_ucontext *context)
{
	/* struct nes_vnic *nesvnic = to_nesvnic(context->device); */
	/* struct nes_device *nesdev = nesvnic->nesdev; */
	struct nes_ucontext *nes_ucontext = to_nesucontext(context);

	if (!atomic_dec_and_test(&nes_ucontext->usecnt))
	  return 0;
	kfree(nes_ucontext);
	return 0;
}


/**
 * nes_mmap
 */
static int nes_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	unsigned long index;
	struct nes_vnic *nesvnic = to_nesvnic(context->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	/* struct nes_adapter *nesadapter = nesdev->nesadapter; */
	struct nes_ucontext *nes_ucontext;
	struct nes_qp *nesqp;

	nes_ucontext = to_nesucontext(context);


	if (vma->vm_pgoff >= nes_ucontext->mmap_wq_offset) {
		index = (vma->vm_pgoff - nes_ucontext->mmap_wq_offset) * PAGE_SIZE;
		index /= ((sizeof(struct nes_hw_qp_wqe) * nesdev->nesadapter->max_qp_wr * 2) +
				PAGE_SIZE-1) & (~(PAGE_SIZE-1));
		if (!test_bit(index, nes_ucontext->allocated_wqs)) {
			nes_debug(NES_DBG_MMAP, "wq %lu not allocated\n", index);
			return -EFAULT;
		}
		nesqp = nes_ucontext->mmap_nesqp[index];
		if (nesqp == NULL) {
			nes_debug(NES_DBG_MMAP, "wq %lu has a NULL QP base.\n", index);
			return -EFAULT;
		}
		if (remap_pfn_range(vma, vma->vm_start,
				virt_to_phys(nesqp->hwqp.sq_vbase) >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot)) {
			nes_debug(NES_DBG_MMAP, "remap_pfn_range failed.\n");
			return -EAGAIN;
		}
		vma->vm_private_data = nesqp;
		return 0;
	} else {
		index = vma->vm_pgoff;
		if (!test_bit(index, nes_ucontext->allocated_doorbells))
			return -EFAULT;

		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		if (io_remap_pfn_range(vma, vma->vm_start,
				(nesdev->doorbell_start +
				((nes_ucontext->mmap_db_index[index] - nesdev->base_doorbell_index) * 4096))
				>> PAGE_SHIFT, PAGE_SIZE, vma->vm_page_prot))
			return -EAGAIN;
		vma->vm_private_data = nes_ucontext;
		return 0;
	}

	return -ENOSYS;
}


/**
 * nes_alloc_pd
 */
static struct ib_pd *nes_alloc_pd(struct ib_device *ibdev,
		struct ib_ucontext *context, struct ib_udata *udata)
{
	struct nes_pd *nespd;
	struct nes_vnic *nesvnic = to_nesvnic(ibdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_ucontext *nesucontext;
	struct nes_alloc_pd_resp uresp;
	u32 pd_num = 0;
	int err;

	nes_debug(NES_DBG_PD, "nesvnic=%p, netdev=%p %s, ibdev=%p, context=%p, netdev refcnt=%u\n",
			nesvnic, nesdev->netdev[0], nesdev->netdev[0]->name, ibdev, context,
			netdev_refcnt_read(nesvnic->netdev));

	err = nes_alloc_resource(nesadapter, nesadapter->allocated_pds,
			nesadapter->max_pd, &pd_num, &nesadapter->next_pd, NES_RESOURCE_PD);
	if (err) {
		return ERR_PTR(err);
	}

	nespd = kzalloc(sizeof (struct nes_pd), GFP_KERNEL);
	if (!nespd) {
		nes_free_resource(nesadapter, nesadapter->allocated_pds, pd_num);
		return ERR_PTR(-ENOMEM);
	}

	nes_debug(NES_DBG_PD, "Allocating PD (%p) for ib device %s\n",
			nespd, nesvnic->nesibdev->ibdev.name);

	nespd->pd_id = (pd_num << (PAGE_SHIFT-12)) + nesadapter->base_pd;

	if (context) {
		nesucontext = to_nesucontext(context);
		nespd->mmap_db_index = find_next_zero_bit(nesucontext->allocated_doorbells,
				NES_MAX_USER_DB_REGIONS, nesucontext->first_free_db);
		nes_debug(NES_DBG_PD, "find_first_zero_biton doorbells returned %u, mapping pd_id %u.\n",
				nespd->mmap_db_index, nespd->pd_id);
		if (nespd->mmap_db_index >= NES_MAX_USER_DB_REGIONS) {
			nes_debug(NES_DBG_PD, "mmap_db_index > MAX\n");
			nes_free_resource(nesadapter, nesadapter->allocated_pds, pd_num);
			kfree(nespd);
			return ERR_PTR(-ENOMEM);
		}

		uresp.pd_id = nespd->pd_id;
		uresp.mmap_db_index = nespd->mmap_db_index;
		if (ib_copy_to_udata(udata, &uresp, sizeof (struct nes_alloc_pd_resp))) {
			nes_free_resource(nesadapter, nesadapter->allocated_pds, pd_num);
			kfree(nespd);
			return ERR_PTR(-EFAULT);
		}

		set_bit(nespd->mmap_db_index, nesucontext->allocated_doorbells);
		nesucontext->mmap_db_index[nespd->mmap_db_index] = nespd->pd_id;
		nesucontext->first_free_db = nespd->mmap_db_index + 1;
	}

	nes_debug(NES_DBG_PD, "PD%u structure located @%p.\n", nespd->pd_id, nespd);
	return &nespd->ibpd;
}


/**
 * nes_dealloc_pd
 */
static int nes_dealloc_pd(struct ib_pd *ibpd)
{
	struct nes_ucontext *nesucontext;
	struct nes_pd *nespd = to_nespd(ibpd);
	struct nes_vnic *nesvnic = to_nesvnic(ibpd->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;

	if ((ibpd->uobject) && (ibpd->uobject->context)) {
		nesucontext = to_nesucontext(ibpd->uobject->context);
		nes_debug(NES_DBG_PD, "Clearing bit %u from allocated doorbells\n",
				nespd->mmap_db_index);
		clear_bit(nespd->mmap_db_index, nesucontext->allocated_doorbells);
		nesucontext->mmap_db_index[nespd->mmap_db_index] = 0;
		if (nesucontext->first_free_db > nespd->mmap_db_index) {
			nesucontext->first_free_db = nespd->mmap_db_index;
		}
	}

	nes_debug(NES_DBG_PD, "Deallocating PD%u structure located @%p.\n",
			nespd->pd_id, nespd);
	nes_free_resource(nesadapter, nesadapter->allocated_pds,
			(nespd->pd_id-nesadapter->base_pd)>>(PAGE_SHIFT-12));
	kfree(nespd);

	return 0;
}


/**
 * nes_create_ah
 */
static struct ib_ah *nes_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr)
{
	return ERR_PTR(-ENOSYS);
}


/**
 * nes_destroy_ah
 */
static int nes_destroy_ah(struct ib_ah *ah)
{
	return -ENOSYS;
}


/**
 * nes_get_encoded_size
 */
static inline u8 nes_get_encoded_size(int *size)
{
	u8 encoded_size = 0;
	if (*size <= 32) {
		*size = 32;
		encoded_size = 1;
	} else if (*size <= 128) {
		*size = 128;
		encoded_size = 2;
	} else if (*size <= 512) {
		*size = 512;
		encoded_size = 3;
	}
	return (encoded_size);
}



/**
 * nes_setup_virt_qp
 */
static int nes_setup_virt_qp(struct nes_qp *nesqp, struct nes_pbl *nespbl,
		struct nes_vnic *nesvnic, int sq_size, int rq_size)
{
	unsigned long flags;
	void *mem;
	__le64 *pbl = NULL;
	__le64 *tpbl;
	__le64 *pblbuffer;
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	u32 pbl_entries;
	u8 rq_pbl_entries;
	u8 sq_pbl_entries;

	pbl_entries = nespbl->pbl_size >> 3;
	nes_debug(NES_DBG_QP, "Userspace PBL, pbl_size=%u, pbl_entries = %d pbl_vbase=%p, pbl_pbase=%lx\n",
			nespbl->pbl_size, pbl_entries,
			(void *)nespbl->pbl_vbase,
			(unsigned long) nespbl->pbl_pbase);
	pbl = (__le64 *) nespbl->pbl_vbase; /* points to first pbl entry */
	/* now lets set the sq_vbase as well as rq_vbase addrs we will assign */
	/* the first pbl to be fro the rq_vbase... */
	rq_pbl_entries = (rq_size * sizeof(struct nes_hw_qp_wqe)) >> 12;
	sq_pbl_entries = (sq_size * sizeof(struct nes_hw_qp_wqe)) >> 12;
	nesqp->hwqp.sq_pbase = (le32_to_cpu(((__le32 *)pbl)[0])) | ((u64)((le32_to_cpu(((__le32 *)pbl)[1]))) << 32);
	if (!nespbl->page) {
		nes_debug(NES_DBG_QP, "QP nespbl->page is NULL \n");
		kfree(nespbl);
		return -ENOMEM;
	}

	nesqp->hwqp.sq_vbase = kmap(nespbl->page);
	nesqp->page = nespbl->page;
	if (!nesqp->hwqp.sq_vbase) {
		nes_debug(NES_DBG_QP, "QP sq_vbase kmap failed\n");
		kfree(nespbl);
		return -ENOMEM;
	}

	/* Now to get to sq.. we need to calculate how many */
	/* PBL entries were used by the rq.. */
	pbl += sq_pbl_entries;
	nesqp->hwqp.rq_pbase = (le32_to_cpu(((__le32 *)pbl)[0])) | ((u64)((le32_to_cpu(((__le32 *)pbl)[1]))) << 32);
	/* nesqp->hwqp.rq_vbase = bus_to_virt(*pbl); */
	/*nesqp->hwqp.rq_vbase = phys_to_virt(*pbl); */

	nes_debug(NES_DBG_QP, "QP sq_vbase= %p sq_pbase=%lx rq_vbase=%p rq_pbase=%lx\n",
		  nesqp->hwqp.sq_vbase, (unsigned long) nesqp->hwqp.sq_pbase,
		  nesqp->hwqp.rq_vbase, (unsigned long) nesqp->hwqp.rq_pbase);
	spin_lock_irqsave(&nesadapter->pbl_lock, flags);
	if (!nesadapter->free_256pbl) {
		pci_free_consistent(nesdev->pcidev, nespbl->pbl_size, nespbl->pbl_vbase,
				nespbl->pbl_pbase);
		spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
		kunmap(nesqp->page);
		kfree(nespbl);
		return -ENOMEM;
	}
	nesadapter->free_256pbl--;
	spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);

	nesqp->pbl_vbase = pci_alloc_consistent(nesdev->pcidev, 256, &nesqp->pbl_pbase);
	pblbuffer = nesqp->pbl_vbase;
	if (!nesqp->pbl_vbase) {
		/* memory allocated during nes_reg_user_mr() */
		pci_free_consistent(nesdev->pcidev, nespbl->pbl_size, nespbl->pbl_vbase,
				    nespbl->pbl_pbase);
		kfree(nespbl);
		spin_lock_irqsave(&nesadapter->pbl_lock, flags);
		nesadapter->free_256pbl++;
		spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
		kunmap(nesqp->page);
		return -ENOMEM;
	}
	memset(nesqp->pbl_vbase, 0, 256);
	/* fill in the page address in the pbl buffer.. */
	tpbl = pblbuffer + 16;
	pbl = (__le64 *)nespbl->pbl_vbase;
	while (sq_pbl_entries--)
		*tpbl++ = *pbl++;
	tpbl = pblbuffer;
	while (rq_pbl_entries--)
		*tpbl++ = *pbl++;

	/* done with memory allocated during nes_reg_user_mr() */
	pci_free_consistent(nesdev->pcidev, nespbl->pbl_size, nespbl->pbl_vbase,
			    nespbl->pbl_pbase);
	kfree(nespbl);

	nesqp->qp_mem_size =
			max((u32)sizeof(struct nes_qp_context), ((u32)256)) + 256;     /* this is Q2 */
	/* Round up to a multiple of a page */
	nesqp->qp_mem_size += PAGE_SIZE - 1;
	nesqp->qp_mem_size &= ~(PAGE_SIZE - 1);

	mem = pci_alloc_consistent(nesdev->pcidev, nesqp->qp_mem_size,
			&nesqp->hwqp.q2_pbase);

	if (!mem) {
		pci_free_consistent(nesdev->pcidev, 256, nesqp->pbl_vbase, nesqp->pbl_pbase);
		nesqp->pbl_vbase = NULL;
		spin_lock_irqsave(&nesadapter->pbl_lock, flags);
		nesadapter->free_256pbl++;
		spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
		kunmap(nesqp->page);
		return -ENOMEM;
	}
	nesqp->sq_kmapped = 1;
	nesqp->hwqp.q2_vbase = mem;
	mem += 256;
	memset(nesqp->hwqp.q2_vbase, 0, 256);
	nesqp->nesqp_context = mem;
	memset(nesqp->nesqp_context, 0, sizeof(*nesqp->nesqp_context));
	nesqp->nesqp_context_pbase = nesqp->hwqp.q2_pbase + 256;

	return 0;
}


/**
 * nes_setup_mmap_qp
 */
static int nes_setup_mmap_qp(struct nes_qp *nesqp, struct nes_vnic *nesvnic,
		int sq_size, int rq_size)
{
	void *mem;
	struct nes_device *nesdev = nesvnic->nesdev;

	nesqp->qp_mem_size = (sizeof(struct nes_hw_qp_wqe) * sq_size) +
			(sizeof(struct nes_hw_qp_wqe) * rq_size) +
			max((u32)sizeof(struct nes_qp_context), ((u32)256)) +
			256; /* this is Q2 */
	/* Round up to a multiple of a page */
	nesqp->qp_mem_size += PAGE_SIZE - 1;
	nesqp->qp_mem_size &= ~(PAGE_SIZE - 1);

	mem = pci_alloc_consistent(nesdev->pcidev, nesqp->qp_mem_size,
			&nesqp->hwqp.sq_pbase);
	if (!mem)
		return -ENOMEM;
	nes_debug(NES_DBG_QP, "PCI consistent memory for "
			"host descriptor rings located @ %p (pa = 0x%08lX.) size = %u.\n",
			mem, (unsigned long)nesqp->hwqp.sq_pbase, nesqp->qp_mem_size);

	memset(mem, 0, nesqp->qp_mem_size);

	nesqp->hwqp.sq_vbase = mem;
	mem += sizeof(struct nes_hw_qp_wqe) * sq_size;

	nesqp->hwqp.rq_vbase = mem;
	nesqp->hwqp.rq_pbase = nesqp->hwqp.sq_pbase +
			sizeof(struct nes_hw_qp_wqe) * sq_size;
	mem += sizeof(struct nes_hw_qp_wqe) * rq_size;

	nesqp->hwqp.q2_vbase = mem;
	nesqp->hwqp.q2_pbase = nesqp->hwqp.rq_pbase +
			sizeof(struct nes_hw_qp_wqe) * rq_size;
	mem += 256;
	memset(nesqp->hwqp.q2_vbase, 0, 256);

	nesqp->nesqp_context = mem;
	nesqp->nesqp_context_pbase = nesqp->hwqp.q2_pbase + 256;
	memset(nesqp->nesqp_context, 0, sizeof(*nesqp->nesqp_context));
	return 0;
}


/**
 * nes_free_qp_mem() is to free up the qp's pci_alloc_consistent() memory.
 */
static inline void nes_free_qp_mem(struct nes_device *nesdev,
		struct nes_qp *nesqp, int virt_wqs)
{
	unsigned long flags;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	if (!virt_wqs) {
		pci_free_consistent(nesdev->pcidev, nesqp->qp_mem_size,
				nesqp->hwqp.sq_vbase, nesqp->hwqp.sq_pbase);
	}else {
		spin_lock_irqsave(&nesadapter->pbl_lock, flags);
		nesadapter->free_256pbl++;
		spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
		pci_free_consistent(nesdev->pcidev, nesqp->qp_mem_size, nesqp->hwqp.q2_vbase, nesqp->hwqp.q2_pbase);
		pci_free_consistent(nesdev->pcidev, 256, nesqp->pbl_vbase, nesqp->pbl_pbase );
		nesqp->pbl_vbase = NULL;
		if (nesqp->sq_kmapped) {
			nesqp->sq_kmapped = 0;
			kunmap(nesqp->page);
		}
	}
}


/**
 * nes_create_qp
 */
static struct ib_qp *nes_create_qp(struct ib_pd *ibpd,
		struct ib_qp_init_attr *init_attr, struct ib_udata *udata)
{
	u64 u64temp= 0;
	u64 u64nesqp = 0;
	struct nes_pd *nespd = to_nespd(ibpd);
	struct nes_vnic *nesvnic = to_nesvnic(ibpd->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_qp *nesqp;
	struct nes_cq *nescq;
	struct nes_ucontext *nes_ucontext;
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_cqp_request *cqp_request;
	struct nes_create_qp_req req;
	struct nes_create_qp_resp uresp;
	struct nes_pbl  *nespbl = NULL;
	u32 qp_num = 0;
	u32 opcode = 0;
	/* u32 counter = 0; */
	void *mem;
	unsigned long flags;
	int ret;
	int err;
	int virt_wqs = 0;
	int sq_size;
	int rq_size;
	u8 sq_encoded_size;
	u8 rq_encoded_size;
	/* int counter; */

	if (init_attr->create_flags)
		return ERR_PTR(-EINVAL);

	atomic_inc(&qps_created);
	switch (init_attr->qp_type) {
		case IB_QPT_RC:
			if (nes_drv_opt & NES_DRV_OPT_NO_INLINE_DATA) {
				init_attr->cap.max_inline_data = 0;
			} else {
				init_attr->cap.max_inline_data = 64;
			}
			sq_size = init_attr->cap.max_send_wr;
			rq_size = init_attr->cap.max_recv_wr;

			/* check if the encoded sizes are OK or not... */
			sq_encoded_size = nes_get_encoded_size(&sq_size);
			rq_encoded_size = nes_get_encoded_size(&rq_size);

			if ((!sq_encoded_size) || (!rq_encoded_size)) {
				nes_debug(NES_DBG_QP, "ERROR bad rq (%u) or sq (%u) size\n",
						rq_size, sq_size);
				return ERR_PTR(-EINVAL);
			}

			init_attr->cap.max_send_wr = sq_size -2;
			init_attr->cap.max_recv_wr = rq_size -1;
			nes_debug(NES_DBG_QP, "RQ size=%u, SQ Size=%u\n", rq_size, sq_size);

			ret = nes_alloc_resource(nesadapter, nesadapter->allocated_qps,
					nesadapter->max_qp, &qp_num, &nesadapter->next_qp, NES_RESOURCE_QP);
			if (ret) {
				return ERR_PTR(ret);
			}

			/* Need 512 (actually now 1024) byte alignment on this structure */
			mem = kzalloc(sizeof(*nesqp)+NES_SW_CONTEXT_ALIGN-1, GFP_KERNEL);
			if (!mem) {
				nes_free_resource(nesadapter, nesadapter->allocated_qps, qp_num);
				nes_debug(NES_DBG_QP, "Unable to allocate QP\n");
				return ERR_PTR(-ENOMEM);
			}
			u64nesqp = (unsigned long)mem;
			u64nesqp += ((u64)NES_SW_CONTEXT_ALIGN) - 1;
			u64temp = ((u64)NES_SW_CONTEXT_ALIGN) - 1;
			u64nesqp &= ~u64temp;
			nesqp = (struct nes_qp *)(unsigned long)u64nesqp;
			/* nes_debug(NES_DBG_QP, "nesqp=%p, allocated buffer=%p.  Rounded to closest %u\n",
					nesqp, mem, NES_SW_CONTEXT_ALIGN); */
			nesqp->allocated_buffer = mem;

			if (udata) {
				if (ib_copy_from_udata(&req, udata, sizeof(struct nes_create_qp_req))) {
					nes_free_resource(nesadapter, nesadapter->allocated_qps, qp_num);
					kfree(nesqp->allocated_buffer);
					nes_debug(NES_DBG_QP, "ib_copy_from_udata() Failed \n");
					return ERR_PTR(-EFAULT);
				}
				if (req.user_wqe_buffers) {
					virt_wqs = 1;
				}
				if (req.user_qp_buffer)
					nesqp->nesuqp_addr = req.user_qp_buffer;
				if ((ibpd->uobject) && (ibpd->uobject->context)) {
					nesqp->user_mode = 1;
					nes_ucontext = to_nesucontext(ibpd->uobject->context);
					if (virt_wqs) {
						err = 1;
						list_for_each_entry(nespbl, &nes_ucontext->qp_reg_mem_list, list) {
							if (nespbl->user_base == (unsigned long )req.user_wqe_buffers) {
								list_del(&nespbl->list);
								err = 0;
								nes_debug(NES_DBG_QP, "Found PBL for virtual QP. nespbl=%p. user_base=0x%lx\n",
									  nespbl, nespbl->user_base);
								break;
							}
						}
						if (err) {
							nes_debug(NES_DBG_QP, "Didn't Find PBL for virtual QP. address = %llx.\n",
								  (long long unsigned int)req.user_wqe_buffers);
							nes_free_resource(nesadapter, nesadapter->allocated_qps, qp_num);
							kfree(nesqp->allocated_buffer);
							return ERR_PTR(-EFAULT);
						}
					}

					nes_ucontext = to_nesucontext(ibpd->uobject->context);
					nesqp->mmap_sq_db_index =
						find_next_zero_bit(nes_ucontext->allocated_wqs,
								   NES_MAX_USER_WQ_REGIONS, nes_ucontext->first_free_wq);
					/* nes_debug(NES_DBG_QP, "find_first_zero_biton wqs returned %u\n",
							nespd->mmap_db_index); */
					if (nesqp->mmap_sq_db_index >= NES_MAX_USER_WQ_REGIONS) {
						nes_debug(NES_DBG_QP,
							  "db index > max user regions, failing create QP\n");
						nes_free_resource(nesadapter, nesadapter->allocated_qps, qp_num);
						if (virt_wqs) {
							pci_free_consistent(nesdev->pcidev, nespbl->pbl_size, nespbl->pbl_vbase,
									    nespbl->pbl_pbase);
							kfree(nespbl);
						}
						kfree(nesqp->allocated_buffer);
						return ERR_PTR(-ENOMEM);
					}
					set_bit(nesqp->mmap_sq_db_index, nes_ucontext->allocated_wqs);
					nes_ucontext->mmap_nesqp[nesqp->mmap_sq_db_index] = nesqp;
					nes_ucontext->first_free_wq = nesqp->mmap_sq_db_index + 1;
				} else {
					nes_free_resource(nesadapter, nesadapter->allocated_qps, qp_num);
					kfree(nesqp->allocated_buffer);
					return ERR_PTR(-EFAULT);
				}
			}
			err = (!virt_wqs) ? nes_setup_mmap_qp(nesqp, nesvnic, sq_size, rq_size) :
					nes_setup_virt_qp(nesqp, nespbl, nesvnic, sq_size, rq_size);
			if (err) {
				nes_debug(NES_DBG_QP,
					  "error geting qp mem code = %d\n", err);
				nes_free_resource(nesadapter, nesadapter->allocated_qps, qp_num);
				kfree(nesqp->allocated_buffer);
				return ERR_PTR(-ENOMEM);
			}

			nesqp->hwqp.sq_size = sq_size;
			nesqp->hwqp.sq_encoded_size = sq_encoded_size;
			nesqp->hwqp.sq_head = 1;
			nesqp->hwqp.rq_size = rq_size;
			nesqp->hwqp.rq_encoded_size = rq_encoded_size;
			/* nes_debug(NES_DBG_QP, "nesqp->nesqp_context_pbase = %p\n",
					(void *)nesqp->nesqp_context_pbase);
			*/
			nesqp->hwqp.qp_id = qp_num;
			nesqp->ibqp.qp_num = nesqp->hwqp.qp_id;
			nesqp->nespd = nespd;

			nescq = to_nescq(init_attr->send_cq);
			nesqp->nesscq = nescq;
			nescq = to_nescq(init_attr->recv_cq);
			nesqp->nesrcq = nescq;

			nesqp->nesqp_context->misc |= cpu_to_le32((u32)PCI_FUNC(nesdev->pcidev->devfn) <<
					NES_QPCONTEXT_MISC_PCI_FCN_SHIFT);
			nesqp->nesqp_context->misc |= cpu_to_le32((u32)nesqp->hwqp.rq_encoded_size <<
					NES_QPCONTEXT_MISC_RQ_SIZE_SHIFT);
			nesqp->nesqp_context->misc |= cpu_to_le32((u32)nesqp->hwqp.sq_encoded_size <<
					NES_QPCONTEXT_MISC_SQ_SIZE_SHIFT);
			if (!udata) {
				nesqp->nesqp_context->misc |= cpu_to_le32(NES_QPCONTEXT_MISC_PRIV_EN);
				nesqp->nesqp_context->misc |= cpu_to_le32(NES_QPCONTEXT_MISC_FAST_REGISTER_EN);
			}
			nesqp->nesqp_context->cqs = cpu_to_le32(nesqp->nesscq->hw_cq.cq_number +
					((u32)nesqp->nesrcq->hw_cq.cq_number << 16));
			u64temp = (u64)nesqp->hwqp.sq_pbase;
			nesqp->nesqp_context->sq_addr_low = cpu_to_le32((u32)u64temp);
			nesqp->nesqp_context->sq_addr_high = cpu_to_le32((u32)(u64temp >> 32));


			if (!virt_wqs) {
				u64temp = (u64)nesqp->hwqp.sq_pbase;
				nesqp->nesqp_context->sq_addr_low = cpu_to_le32((u32)u64temp);
				nesqp->nesqp_context->sq_addr_high = cpu_to_le32((u32)(u64temp >> 32));
				u64temp = (u64)nesqp->hwqp.rq_pbase;
				nesqp->nesqp_context->rq_addr_low = cpu_to_le32((u32)u64temp);
				nesqp->nesqp_context->rq_addr_high = cpu_to_le32((u32)(u64temp >> 32));
			} else {
				u64temp = (u64)nesqp->pbl_pbase;
				nesqp->nesqp_context->rq_addr_low = cpu_to_le32((u32)u64temp);
				nesqp->nesqp_context->rq_addr_high = cpu_to_le32((u32)(u64temp >> 32));
			}

			/* nes_debug(NES_DBG_QP, "next_qp_nic_index=%u, using nic_index=%d\n",
					nesvnic->next_qp_nic_index,
					nesvnic->qp_nic_index[nesvnic->next_qp_nic_index]); */
			spin_lock_irqsave(&nesdev->cqp.lock, flags);
			nesqp->nesqp_context->misc2 |= cpu_to_le32(
					(u32)nesvnic->qp_nic_index[nesvnic->next_qp_nic_index] <<
					NES_QPCONTEXT_MISC2_NIC_INDEX_SHIFT);
			nesvnic->next_qp_nic_index++;
			if ((nesvnic->next_qp_nic_index > 3) ||
					(nesvnic->qp_nic_index[nesvnic->next_qp_nic_index] == 0xf)) {
				nesvnic->next_qp_nic_index = 0;
			}
			spin_unlock_irqrestore(&nesdev->cqp.lock, flags);

			nesqp->nesqp_context->pd_index_wscale |= cpu_to_le32((u32)nesqp->nespd->pd_id << 16);
			u64temp = (u64)nesqp->hwqp.q2_pbase;
			nesqp->nesqp_context->q2_addr_low = cpu_to_le32((u32)u64temp);
			nesqp->nesqp_context->q2_addr_high = cpu_to_le32((u32)(u64temp >> 32));
			nesqp->nesqp_context->aeq_token_low =  cpu_to_le32((u32)((unsigned long)(nesqp)));
			nesqp->nesqp_context->aeq_token_high =  cpu_to_le32((u32)(upper_32_bits((unsigned long)(nesqp))));
			nesqp->nesqp_context->ird_ord_sizes = cpu_to_le32(NES_QPCONTEXT_ORDIRD_ALSMM |
					NES_QPCONTEXT_ORDIRD_AAH |
					((((u32)nesadapter->max_irrq_wr) <<
					NES_QPCONTEXT_ORDIRD_IRDSIZE_SHIFT) & NES_QPCONTEXT_ORDIRD_IRDSIZE_MASK));
			if (disable_mpa_crc) {
				nes_debug(NES_DBG_QP, "Disabling MPA crc checking due to module option.\n");
				nesqp->nesqp_context->ird_ord_sizes |= cpu_to_le32(NES_QPCONTEXT_ORDIRD_RNMC);
			}


			/* Create the QP */
			cqp_request = nes_get_cqp_request(nesdev);
			if (cqp_request == NULL) {
				nes_debug(NES_DBG_QP, "Failed to get a cqp_request\n");
				nes_free_resource(nesadapter, nesadapter->allocated_qps, qp_num);
				nes_free_qp_mem(nesdev, nesqp,virt_wqs);
				kfree(nesqp->allocated_buffer);
				return ERR_PTR(-ENOMEM);
			}
			cqp_request->waiting = 1;
			cqp_wqe = &cqp_request->cqp_wqe;

			if (!virt_wqs) {
				opcode = NES_CQP_CREATE_QP | NES_CQP_QP_TYPE_IWARP |
					NES_CQP_QP_IWARP_STATE_IDLE;
			} else {
				opcode = NES_CQP_CREATE_QP | NES_CQP_QP_TYPE_IWARP | NES_CQP_QP_VIRT_WQS |
					NES_CQP_QP_IWARP_STATE_IDLE;
			}
			opcode |= NES_CQP_QP_CQS_VALID;
			nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
			set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX, opcode);
			set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_ID_IDX, nesqp->hwqp.qp_id);

			u64temp = (u64)nesqp->nesqp_context_pbase;
			set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_QP_WQE_CONTEXT_LOW_IDX, u64temp);

			atomic_set(&cqp_request->refcount, 2);
			nes_post_cqp_request(nesdev, cqp_request);

			/* Wait for CQP */
			nes_debug(NES_DBG_QP, "Waiting for create iWARP QP%u to complete.\n",
					nesqp->hwqp.qp_id);
			ret = wait_event_timeout(cqp_request->waitq,
					(cqp_request->request_done != 0), NES_EVENT_TIMEOUT);
			nes_debug(NES_DBG_QP, "Create iwarp QP%u completed, wait_event_timeout ret=%u,"
					" nesdev->cqp_head = %u, nesdev->cqp.sq_tail = %u,"
					" CQP Major:Minor codes = 0x%04X:0x%04X.\n",
					nesqp->hwqp.qp_id, ret, nesdev->cqp.sq_head, nesdev->cqp.sq_tail,
					cqp_request->major_code, cqp_request->minor_code);
			if ((!ret) || (cqp_request->major_code)) {
				nes_put_cqp_request(nesdev, cqp_request);
				nes_free_resource(nesadapter, nesadapter->allocated_qps, qp_num);
				nes_free_qp_mem(nesdev, nesqp,virt_wqs);
				kfree(nesqp->allocated_buffer);
				if (!ret) {
					return ERR_PTR(-ETIME);
				} else {
					return ERR_PTR(-EIO);
				}
			}

			nes_put_cqp_request(nesdev, cqp_request);

			if (ibpd->uobject) {
				uresp.mmap_sq_db_index = nesqp->mmap_sq_db_index;
				uresp.mmap_rq_db_index = 0;
				uresp.actual_sq_size = sq_size;
				uresp.actual_rq_size = rq_size;
				uresp.qp_id = nesqp->hwqp.qp_id;
				uresp.nes_drv_opt = nes_drv_opt;
				if (ib_copy_to_udata(udata, &uresp, sizeof uresp)) {
					nes_free_resource(nesadapter, nesadapter->allocated_qps, qp_num);
					nes_free_qp_mem(nesdev, nesqp,virt_wqs);
					kfree(nesqp->allocated_buffer);
					return ERR_PTR(-EFAULT);
				}
			}

			nes_debug(NES_DBG_QP, "QP%u structure located @%p.Size = %u.\n",
					nesqp->hwqp.qp_id, nesqp, (u32)sizeof(*nesqp));
			spin_lock_init(&nesqp->lock);
			nes_add_ref(&nesqp->ibqp);
			break;
		default:
			nes_debug(NES_DBG_QP, "Invalid QP type: %d\n", init_attr->qp_type);
			return ERR_PTR(-EINVAL);
	}

	nesqp->sig_all = (init_attr->sq_sig_type == IB_SIGNAL_ALL_WR);
	init_timer(&nesqp->terminate_timer);
	nesqp->terminate_timer.function = nes_terminate_timeout;
	nesqp->terminate_timer.data = (unsigned long)nesqp;

	/* update the QP table */
	nesdev->nesadapter->qp_table[nesqp->hwqp.qp_id-NES_FIRST_QPN] = nesqp;
	nes_debug(NES_DBG_QP, "netdev refcnt=%u\n",
			netdev_refcnt_read(nesvnic->netdev));

	return &nesqp->ibqp;
}

/**
 * nes_clean_cq
 */
static void nes_clean_cq(struct nes_qp *nesqp, struct nes_cq *nescq)
{
	u32 cq_head;
	u32 lo;
	u32 hi;
	u64 u64temp;
	unsigned long flags = 0;

	spin_lock_irqsave(&nescq->lock, flags);

	cq_head = nescq->hw_cq.cq_head;
	while (le32_to_cpu(nescq->hw_cq.cq_vbase[cq_head].cqe_words[NES_CQE_OPCODE_IDX]) & NES_CQE_VALID) {
		rmb();
		lo = le32_to_cpu(nescq->hw_cq.cq_vbase[cq_head].cqe_words[NES_CQE_COMP_COMP_CTX_LOW_IDX]);
		hi = le32_to_cpu(nescq->hw_cq.cq_vbase[cq_head].cqe_words[NES_CQE_COMP_COMP_CTX_HIGH_IDX]);
		u64temp = (((u64)hi) << 32) | ((u64)lo);
		u64temp &= ~(NES_SW_CONTEXT_ALIGN-1);
		if (u64temp == (u64)(unsigned long)nesqp) {
			/* Zero the context value so cqe will be ignored */
			nescq->hw_cq.cq_vbase[cq_head].cqe_words[NES_CQE_COMP_COMP_CTX_LOW_IDX] = 0;
			nescq->hw_cq.cq_vbase[cq_head].cqe_words[NES_CQE_COMP_COMP_CTX_HIGH_IDX] = 0;
		}

		if (++cq_head >= nescq->hw_cq.cq_size)
			cq_head = 0;
	}

	spin_unlock_irqrestore(&nescq->lock, flags);
}


/**
 * nes_destroy_qp
 */
static int nes_destroy_qp(struct ib_qp *ibqp)
{
	struct nes_qp *nesqp = to_nesqp(ibqp);
	struct nes_ucontext *nes_ucontext;
	struct ib_qp_attr attr;
	struct iw_cm_id *cm_id;
	struct iw_cm_event cm_event;
	int ret = 0;

	atomic_inc(&sw_qps_destroyed);
	nesqp->destroyed = 1;

	/* Blow away the connection if it exists. */
	if (nesqp->ibqp_state >= IB_QPS_INIT && nesqp->ibqp_state <= IB_QPS_RTS) {
		/* if (nesqp->ibqp_state == IB_QPS_RTS) { */
		attr.qp_state = IB_QPS_ERR;
		nes_modify_qp(&nesqp->ibqp, &attr, IB_QP_STATE, NULL);
	}

	if (((nesqp->ibqp_state == IB_QPS_INIT) ||
			(nesqp->ibqp_state == IB_QPS_RTR)) && (nesqp->cm_id)) {
		cm_id = nesqp->cm_id;
		cm_event.event = IW_CM_EVENT_CONNECT_REPLY;
		cm_event.status = -ETIMEDOUT;
		cm_event.local_addr = cm_id->local_addr;
		cm_event.remote_addr = cm_id->remote_addr;
		cm_event.private_data = NULL;
		cm_event.private_data_len = 0;

		nes_debug(NES_DBG_QP, "Generating a CM Timeout Event for "
				"QP%u. cm_id = %p, refcount = %u. \n",
				nesqp->hwqp.qp_id, cm_id, atomic_read(&nesqp->refcount));

		cm_id->rem_ref(cm_id);
		ret = cm_id->event_handler(cm_id, &cm_event);
		if (ret)
			nes_debug(NES_DBG_QP, "OFA CM event_handler returned, ret=%d\n", ret);
	}

	if (nesqp->user_mode) {
		if ((ibqp->uobject)&&(ibqp->uobject->context)) {
			nes_ucontext = to_nesucontext(ibqp->uobject->context);
			clear_bit(nesqp->mmap_sq_db_index, nes_ucontext->allocated_wqs);
			nes_ucontext->mmap_nesqp[nesqp->mmap_sq_db_index] = NULL;
			if (nes_ucontext->first_free_wq > nesqp->mmap_sq_db_index) {
				nes_ucontext->first_free_wq = nesqp->mmap_sq_db_index;
			}
		}
		if (nesqp->pbl_pbase && nesqp->sq_kmapped) {
			nesqp->sq_kmapped = 0;
			kunmap(nesqp->page);
		}
	} else {
		/* Clean any pending completions from the cq(s) */
		if (nesqp->nesscq)
			nes_clean_cq(nesqp, nesqp->nesscq);

		if ((nesqp->nesrcq) && (nesqp->nesrcq != nesqp->nesscq))
			nes_clean_cq(nesqp, nesqp->nesrcq);
	}
	nes_rem_ref(&nesqp->ibqp);
	return 0;
}


/**
 * nes_create_cq
 */
static struct ib_cq *nes_create_cq(struct ib_device *ibdev, int entries,
		int comp_vector,
		struct ib_ucontext *context, struct ib_udata *udata)
{
	u64 u64temp;
	struct nes_vnic *nesvnic = to_nesvnic(ibdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_cq *nescq;
	struct nes_ucontext *nes_ucontext = NULL;
	struct nes_cqp_request *cqp_request;
	void *mem = NULL;
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_pbl *nespbl = NULL;
	struct nes_create_cq_req req;
	struct nes_create_cq_resp resp;
	u32 cq_num = 0;
	u32 opcode = 0;
	u32 pbl_entries = 1;
	int err;
	unsigned long flags;
	int ret;

	if (entries > nesadapter->max_cqe)
		return ERR_PTR(-EINVAL);

	err = nes_alloc_resource(nesadapter, nesadapter->allocated_cqs,
			nesadapter->max_cq, &cq_num, &nesadapter->next_cq, NES_RESOURCE_CQ);
	if (err) {
		return ERR_PTR(err);
	}

	nescq = kzalloc(sizeof(struct nes_cq), GFP_KERNEL);
	if (!nescq) {
		nes_free_resource(nesadapter, nesadapter->allocated_cqs, cq_num);
		nes_debug(NES_DBG_CQ, "Unable to allocate nes_cq struct\n");
		return ERR_PTR(-ENOMEM);
	}

	nescq->hw_cq.cq_size = max(entries + 1, 5);
	nescq->hw_cq.cq_number = cq_num;
	nescq->ibcq.cqe = nescq->hw_cq.cq_size - 1;


	if (context) {
		nes_ucontext = to_nesucontext(context);
		if (ib_copy_from_udata(&req, udata, sizeof (struct nes_create_cq_req))) {
			nes_free_resource(nesadapter, nesadapter->allocated_cqs, cq_num);
			kfree(nescq);
			return ERR_PTR(-EFAULT);
		}
		nesvnic->mcrq_ucontext = nes_ucontext;
		nes_ucontext->mcrqf = req.mcrqf;
		if (nes_ucontext->mcrqf) {
			if (nes_ucontext->mcrqf & 0x80000000)
				nescq->hw_cq.cq_number = nesvnic->nic.qp_id + 28 + 2 * ((nes_ucontext->mcrqf & 0xf) - 1);
			else if (nes_ucontext->mcrqf & 0x40000000)
				nescq->hw_cq.cq_number = nes_ucontext->mcrqf & 0xffff;
			else
				nescq->hw_cq.cq_number = nesvnic->mcrq_qp_id + nes_ucontext->mcrqf-1;
			nescq->mcrqf = nes_ucontext->mcrqf;
			nes_free_resource(nesadapter, nesadapter->allocated_cqs, cq_num);
		}
		nes_debug(NES_DBG_CQ, "CQ Virtual Address = %08lX, size = %u.\n",
				(unsigned long)req.user_cq_buffer, entries);
		err = 1;
		list_for_each_entry(nespbl, &nes_ucontext->cq_reg_mem_list, list) {
			if (nespbl->user_base == (unsigned long )req.user_cq_buffer) {
				list_del(&nespbl->list);
				err = 0;
				nes_debug(NES_DBG_CQ, "Found PBL for virtual CQ. nespbl=%p.\n",
						nespbl);
				break;
			}
		}
		if (err) {
			nes_free_resource(nesadapter, nesadapter->allocated_cqs, cq_num);
			kfree(nescq);
			return ERR_PTR(-EFAULT);
		}

		pbl_entries = nespbl->pbl_size >> 3;
		nescq->cq_mem_size = 0;
	} else {
		nescq->cq_mem_size = nescq->hw_cq.cq_size * sizeof(struct nes_hw_cqe);
		nes_debug(NES_DBG_CQ, "Attempting to allocate pci memory (%u entries, %u bytes) for CQ%u.\n",
				entries, nescq->cq_mem_size, nescq->hw_cq.cq_number);

		/* allocate the physical buffer space */
		mem = pci_zalloc_consistent(nesdev->pcidev, nescq->cq_mem_size,
					    &nescq->hw_cq.cq_pbase);
		if (!mem) {
			printk(KERN_ERR PFX "Unable to allocate pci memory for cq\n");
			nes_free_resource(nesadapter, nesadapter->allocated_cqs, cq_num);
			kfree(nescq);
			return ERR_PTR(-ENOMEM);
		}

		nescq->hw_cq.cq_vbase = mem;
		nescq->hw_cq.cq_head = 0;
		nes_debug(NES_DBG_CQ, "CQ%u virtual address @ %p, phys = 0x%08X\n",
				nescq->hw_cq.cq_number, nescq->hw_cq.cq_vbase,
				(u32)nescq->hw_cq.cq_pbase);
	}

	nescq->hw_cq.ce_handler = nes_iwarp_ce_handler;
	spin_lock_init(&nescq->lock);

	/* send CreateCQ request to CQP */
	cqp_request = nes_get_cqp_request(nesdev);
	if (cqp_request == NULL) {
		nes_debug(NES_DBG_CQ, "Failed to get a cqp_request.\n");
		if (!context)
			pci_free_consistent(nesdev->pcidev, nescq->cq_mem_size, mem,
					nescq->hw_cq.cq_pbase);
		else {
			pci_free_consistent(nesdev->pcidev, nespbl->pbl_size,
					    nespbl->pbl_vbase, nespbl->pbl_pbase);
			kfree(nespbl);
		}

		nes_free_resource(nesadapter, nesadapter->allocated_cqs, cq_num);
		kfree(nescq);
		return ERR_PTR(-ENOMEM);
	}
	cqp_request->waiting = 1;
	cqp_wqe = &cqp_request->cqp_wqe;

	opcode = NES_CQP_CREATE_CQ | NES_CQP_CQ_CEQ_VALID |
			NES_CQP_CQ_CHK_OVERFLOW |
			NES_CQP_CQ_CEQE_MASK | ((u32)nescq->hw_cq.cq_size << 16);

	spin_lock_irqsave(&nesadapter->pbl_lock, flags);

	if (pbl_entries != 1) {
		if (pbl_entries > 32) {
			/* use 4k pbl */
			nes_debug(NES_DBG_CQ, "pbl_entries=%u, use a 4k PBL\n", pbl_entries);
			if (nesadapter->free_4kpbl == 0) {
				spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
				nes_free_cqp_request(nesdev, cqp_request);
				if (!context)
					pci_free_consistent(nesdev->pcidev, nescq->cq_mem_size, mem,
							nescq->hw_cq.cq_pbase);
				else {
					pci_free_consistent(nesdev->pcidev, nespbl->pbl_size,
							    nespbl->pbl_vbase, nespbl->pbl_pbase);
					kfree(nespbl);
				}
				nes_free_resource(nesadapter, nesadapter->allocated_cqs, cq_num);
				kfree(nescq);
				return ERR_PTR(-ENOMEM);
			} else {
				opcode |= (NES_CQP_CQ_VIRT | NES_CQP_CQ_4KB_CHUNK);
				nescq->virtual_cq = 2;
				nesadapter->free_4kpbl--;
			}
		} else {
			/* use 256 byte pbl */
			nes_debug(NES_DBG_CQ, "pbl_entries=%u, use a 256 byte PBL\n", pbl_entries);
			if (nesadapter->free_256pbl == 0) {
				spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
				nes_free_cqp_request(nesdev, cqp_request);
				if (!context)
					pci_free_consistent(nesdev->pcidev, nescq->cq_mem_size, mem,
							nescq->hw_cq.cq_pbase);
				else {
					pci_free_consistent(nesdev->pcidev, nespbl->pbl_size,
							    nespbl->pbl_vbase, nespbl->pbl_pbase);
					kfree(nespbl);
				}
				nes_free_resource(nesadapter, nesadapter->allocated_cqs, cq_num);
				kfree(nescq);
				return ERR_PTR(-ENOMEM);
			} else {
				opcode |= NES_CQP_CQ_VIRT;
				nescq->virtual_cq = 1;
				nesadapter->free_256pbl--;
			}
		}
	}

	spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);

	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX, opcode);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_ID_IDX,
			(nescq->hw_cq.cq_number | ((u32)nesdev->ceq_index << 16)));

	if (context) {
		if (pbl_entries != 1)
			u64temp = (u64)nespbl->pbl_pbase;
		else
			u64temp	= le64_to_cpu(nespbl->pbl_vbase[0]);
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_CQ_WQE_DOORBELL_INDEX_HIGH_IDX,
				nes_ucontext->mmap_db_index[0]);
	} else {
		u64temp = (u64)nescq->hw_cq.cq_pbase;
		cqp_wqe->wqe_words[NES_CQP_CQ_WQE_DOORBELL_INDEX_HIGH_IDX] = 0;
	}
	set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_CQ_WQE_PBL_LOW_IDX, u64temp);
	cqp_wqe->wqe_words[NES_CQP_CQ_WQE_CQ_CONTEXT_HIGH_IDX] = 0;
	u64temp = (u64)(unsigned long)&nescq->hw_cq;
	cqp_wqe->wqe_words[NES_CQP_CQ_WQE_CQ_CONTEXT_LOW_IDX] =
			cpu_to_le32((u32)(u64temp >> 1));
	cqp_wqe->wqe_words[NES_CQP_CQ_WQE_CQ_CONTEXT_HIGH_IDX] =
			cpu_to_le32(((u32)((u64temp) >> 33)) & 0x7FFFFFFF);

	atomic_set(&cqp_request->refcount, 2);
	nes_post_cqp_request(nesdev, cqp_request);

	/* Wait for CQP */
	nes_debug(NES_DBG_CQ, "Waiting for create iWARP CQ%u to complete.\n",
			nescq->hw_cq.cq_number);
	ret = wait_event_timeout(cqp_request->waitq, (0 != cqp_request->request_done),
			NES_EVENT_TIMEOUT * 2);
	nes_debug(NES_DBG_CQ, "Create iWARP CQ%u completed, wait_event_timeout ret = %d.\n",
			nescq->hw_cq.cq_number, ret);
	if ((!ret) || (cqp_request->major_code)) {
		nes_put_cqp_request(nesdev, cqp_request);
		if (!context)
			pci_free_consistent(nesdev->pcidev, nescq->cq_mem_size, mem,
					nescq->hw_cq.cq_pbase);
		else {
			pci_free_consistent(nesdev->pcidev, nespbl->pbl_size,
					    nespbl->pbl_vbase, nespbl->pbl_pbase);
			kfree(nespbl);
		}
		nes_free_resource(nesadapter, nesadapter->allocated_cqs, cq_num);
		kfree(nescq);
		return ERR_PTR(-EIO);
	}
	nes_put_cqp_request(nesdev, cqp_request);

	if (context) {
		/* free the nespbl */
		pci_free_consistent(nesdev->pcidev, nespbl->pbl_size, nespbl->pbl_vbase,
				nespbl->pbl_pbase);
		kfree(nespbl);
		resp.cq_id = nescq->hw_cq.cq_number;
		resp.cq_size = nescq->hw_cq.cq_size;
		resp.mmap_db_index = 0;
		if (ib_copy_to_udata(udata, &resp, sizeof resp - sizeof resp.reserved)) {
			nes_free_resource(nesadapter, nesadapter->allocated_cqs, cq_num);
			kfree(nescq);
			return ERR_PTR(-EFAULT);
		}
	}

	return &nescq->ibcq;
}


/**
 * nes_destroy_cq
 */
static int nes_destroy_cq(struct ib_cq *ib_cq)
{
	struct nes_cq *nescq;
	struct nes_device *nesdev;
	struct nes_vnic *nesvnic;
	struct nes_adapter *nesadapter;
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_cqp_request *cqp_request;
	unsigned long flags;
	u32 opcode = 0;
	int ret;

	if (ib_cq == NULL)
		return 0;

	nescq = to_nescq(ib_cq);
	nesvnic = to_nesvnic(ib_cq->device);
	nesdev = nesvnic->nesdev;
	nesadapter = nesdev->nesadapter;

	nes_debug(NES_DBG_CQ, "Destroy CQ%u\n", nescq->hw_cq.cq_number);

	/* Send DestroyCQ request to CQP */
	cqp_request = nes_get_cqp_request(nesdev);
	if (cqp_request == NULL) {
		nes_debug(NES_DBG_CQ, "Failed to get a cqp_request.\n");
		return -ENOMEM;
	}
	cqp_request->waiting = 1;
	cqp_wqe = &cqp_request->cqp_wqe;
	opcode = NES_CQP_DESTROY_CQ | (nescq->hw_cq.cq_size << 16);
	spin_lock_irqsave(&nesadapter->pbl_lock, flags);
	if (nescq->virtual_cq == 1) {
		nesadapter->free_256pbl++;
		if (nesadapter->free_256pbl > nesadapter->max_256pbl) {
			printk(KERN_ERR PFX "%s: free 256B PBLs(%u) has exceeded the max(%u)\n",
					__func__, nesadapter->free_256pbl, nesadapter->max_256pbl);
		}
	} else if (nescq->virtual_cq == 2) {
		nesadapter->free_4kpbl++;
		if (nesadapter->free_4kpbl > nesadapter->max_4kpbl) {
			printk(KERN_ERR PFX "%s: free 4K PBLs(%u) has exceeded the max(%u)\n",
					__func__, nesadapter->free_4kpbl, nesadapter->max_4kpbl);
		}
		opcode |= NES_CQP_CQ_4KB_CHUNK;
	}

	spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);

	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX, opcode);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_ID_IDX,
		(nescq->hw_cq.cq_number | ((u32)PCI_FUNC(nesdev->pcidev->devfn) << 16)));
	if (!nescq->mcrqf)
		nes_free_resource(nesadapter, nesadapter->allocated_cqs, nescq->hw_cq.cq_number);

	atomic_set(&cqp_request->refcount, 2);
	nes_post_cqp_request(nesdev, cqp_request);

	/* Wait for CQP */
	nes_debug(NES_DBG_CQ, "Waiting for destroy iWARP CQ%u to complete.\n",
			nescq->hw_cq.cq_number);
	ret = wait_event_timeout(cqp_request->waitq, (0 != cqp_request->request_done),
			NES_EVENT_TIMEOUT);
	nes_debug(NES_DBG_CQ, "Destroy iWARP CQ%u completed, wait_event_timeout ret = %u,"
			" CQP Major:Minor codes = 0x%04X:0x%04X.\n",
			nescq->hw_cq.cq_number, ret, cqp_request->major_code,
			cqp_request->minor_code);
	if (!ret) {
		nes_debug(NES_DBG_CQ, "iWARP CQ%u destroy timeout expired\n",
					nescq->hw_cq.cq_number);
		ret = -ETIME;
	} else if (cqp_request->major_code) {
		nes_debug(NES_DBG_CQ, "iWARP CQ%u destroy failed\n",
					nescq->hw_cq.cq_number);
		ret = -EIO;
	} else {
		ret = 0;
	}
	nes_put_cqp_request(nesdev, cqp_request);

	if (nescq->cq_mem_size)
		pci_free_consistent(nesdev->pcidev, nescq->cq_mem_size,
				    nescq->hw_cq.cq_vbase, nescq->hw_cq.cq_pbase);
	kfree(nescq);

	return ret;
}

/**
 * root_256
 */
static u32 root_256(struct nes_device *nesdev,
		    struct nes_root_vpbl *root_vpbl,
		    struct nes_root_vpbl *new_root,
		    u16 pbl_count_4k)
{
	u64 leaf_pbl;
	int i, j, k;

	if (pbl_count_4k == 1) {
		new_root->pbl_vbase = pci_alloc_consistent(nesdev->pcidev,
						512, &new_root->pbl_pbase);

		if (new_root->pbl_vbase == NULL)
			return 0;

		leaf_pbl = (u64)root_vpbl->pbl_pbase;
		for (i = 0; i < 16; i++) {
			new_root->pbl_vbase[i].pa_low =
				cpu_to_le32((u32)leaf_pbl);
			new_root->pbl_vbase[i].pa_high =
				cpu_to_le32((u32)((((u64)leaf_pbl) >> 32)));
			leaf_pbl += 256;
		}
	} else {
		for (i = 3; i >= 0; i--) {
			j = i * 16;
			root_vpbl->pbl_vbase[j] = root_vpbl->pbl_vbase[i];
			leaf_pbl = le32_to_cpu(root_vpbl->pbl_vbase[j].pa_low) +
			    (((u64)le32_to_cpu(root_vpbl->pbl_vbase[j].pa_high))
				<< 32);
			for (k = 1; k < 16; k++) {
				leaf_pbl += 256;
				root_vpbl->pbl_vbase[j + k].pa_low =
						cpu_to_le32((u32)leaf_pbl);
				root_vpbl->pbl_vbase[j + k].pa_high =
				    cpu_to_le32((u32)((((u64)leaf_pbl) >> 32)));
			}
		}
	}

	return 1;
}


/**
 * nes_reg_mr
 */
static int nes_reg_mr(struct nes_device *nesdev, struct nes_pd *nespd,
		u32 stag, u64 region_length, struct nes_root_vpbl *root_vpbl,
		dma_addr_t single_buffer, u16 pbl_count_4k,
		u16 residual_page_count_4k, int acc, u64 *iova_start,
		u16 *actual_pbl_cnt, u8 *used_4k_pbls)
{
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_cqp_request *cqp_request;
	unsigned long flags;
	int ret;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	uint pg_cnt = 0;
	u16 pbl_count_256 = 0;
	u16 pbl_count = 0;
	u8  use_256_pbls = 0;
	u8  use_4k_pbls = 0;
	u16 use_two_level = (pbl_count_4k > 1) ? 1 : 0;
	struct nes_root_vpbl new_root = { 0, NULL, NULL };
	u32 opcode = 0;
	u16 major_code;

	/* Register the region with the adapter */
	cqp_request = nes_get_cqp_request(nesdev);
	if (cqp_request == NULL) {
		nes_debug(NES_DBG_MR, "Failed to get a cqp_request.\n");
		return -ENOMEM;
	}
	cqp_request->waiting = 1;
	cqp_wqe = &cqp_request->cqp_wqe;

	if (pbl_count_4k) {
		spin_lock_irqsave(&nesadapter->pbl_lock, flags);

		pg_cnt = ((pbl_count_4k - 1) * 512) + residual_page_count_4k;
		pbl_count_256 = (pg_cnt + 31) / 32;
		if (pg_cnt <= 32) {
			if (pbl_count_256 <= nesadapter->free_256pbl)
				use_256_pbls = 1;
			else if (pbl_count_4k <= nesadapter->free_4kpbl)
				use_4k_pbls = 1;
		} else if (pg_cnt <= 2048) {
			if (((pbl_count_4k + use_two_level) <= nesadapter->free_4kpbl) &&
			    (nesadapter->free_4kpbl > (nesadapter->max_4kpbl >> 1))) {
				use_4k_pbls = 1;
			} else if ((pbl_count_256 + 1) <= nesadapter->free_256pbl) {
				use_256_pbls = 1;
				use_two_level = 1;
			} else if ((pbl_count_4k + use_two_level) <= nesadapter->free_4kpbl) {
				use_4k_pbls = 1;
			}
		} else {
			if ((pbl_count_4k + 1) <= nesadapter->free_4kpbl)
				use_4k_pbls = 1;
		}

		if (use_256_pbls) {
			pbl_count = pbl_count_256;
			nesadapter->free_256pbl -= pbl_count + use_two_level;
		} else if (use_4k_pbls) {
			pbl_count =  pbl_count_4k;
			nesadapter->free_4kpbl -= pbl_count + use_two_level;
		} else {
			spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
			nes_debug(NES_DBG_MR, "Out of Pbls\n");
			nes_free_cqp_request(nesdev, cqp_request);
			return -ENOMEM;
		}

		spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
	}

	if (use_256_pbls && use_two_level) {
		if (root_256(nesdev, root_vpbl, &new_root, pbl_count_4k) == 1) {
			if (new_root.pbl_pbase != 0)
				root_vpbl = &new_root;
		} else {
			spin_lock_irqsave(&nesadapter->pbl_lock, flags);
			nesadapter->free_256pbl += pbl_count_256 + use_two_level;
			use_256_pbls = 0;

			if (pbl_count_4k == 1)
				use_two_level = 0;
			pbl_count = pbl_count_4k;

			if ((pbl_count_4k + use_two_level) <= nesadapter->free_4kpbl) {
				nesadapter->free_4kpbl -= pbl_count + use_two_level;
				use_4k_pbls = 1;
			}
			spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);

			if (use_4k_pbls == 0)
				return -ENOMEM;
		}
	}

	opcode = NES_CQP_REGISTER_STAG | NES_CQP_STAG_RIGHTS_LOCAL_READ |
					NES_CQP_STAG_VA_TO | NES_CQP_STAG_MR;
	if (acc & IB_ACCESS_LOCAL_WRITE)
		opcode |= NES_CQP_STAG_RIGHTS_LOCAL_WRITE;
	if (acc & IB_ACCESS_REMOTE_WRITE)
		opcode |= NES_CQP_STAG_RIGHTS_REMOTE_WRITE | NES_CQP_STAG_REM_ACC_EN;
	if (acc & IB_ACCESS_REMOTE_READ)
		opcode |= NES_CQP_STAG_RIGHTS_REMOTE_READ | NES_CQP_STAG_REM_ACC_EN;
	if (acc & IB_ACCESS_MW_BIND)
		opcode |= NES_CQP_STAG_RIGHTS_WINDOW_BIND | NES_CQP_STAG_REM_ACC_EN;

	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX, opcode);
	set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_VA_LOW_IDX, *iova_start);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_LEN_LOW_IDX, region_length);

	cqp_wqe->wqe_words[NES_CQP_STAG_WQE_LEN_HIGH_PD_IDX] =
			cpu_to_le32((u32)(region_length >> 8) & 0xff000000);
	cqp_wqe->wqe_words[NES_CQP_STAG_WQE_LEN_HIGH_PD_IDX] |=
			cpu_to_le32(nespd->pd_id & 0x00007fff);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_STAG_IDX, stag);

	if (pbl_count == 0) {
		set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_PA_LOW_IDX, single_buffer);
	} else {
		set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_PA_LOW_IDX, root_vpbl->pbl_pbase);
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_PBL_BLK_COUNT_IDX, pbl_count);
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_PBL_LEN_IDX, (pg_cnt * 8));

		if (use_4k_pbls)
			cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] |= cpu_to_le32(NES_CQP_STAG_PBL_BLK_SIZE);
	}
	barrier();

	atomic_set(&cqp_request->refcount, 2);
	nes_post_cqp_request(nesdev, cqp_request);

	/* Wait for CQP */
	ret = wait_event_timeout(cqp_request->waitq, (0 != cqp_request->request_done),
			NES_EVENT_TIMEOUT);
	nes_debug(NES_DBG_MR, "Register STag 0x%08X completed, wait_event_timeout ret = %u,"
			" CQP Major:Minor codes = 0x%04X:0x%04X.\n",
			stag, ret, cqp_request->major_code, cqp_request->minor_code);
	major_code = cqp_request->major_code;
	nes_put_cqp_request(nesdev, cqp_request);

	if ((!ret || major_code) && pbl_count != 0) {
		spin_lock_irqsave(&nesadapter->pbl_lock, flags);
		if (use_256_pbls)
			nesadapter->free_256pbl += pbl_count + use_two_level;
		else if (use_4k_pbls)
			nesadapter->free_4kpbl += pbl_count + use_two_level;
		spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
	}
	if (new_root.pbl_pbase)
		pci_free_consistent(nesdev->pcidev, 512, new_root.pbl_vbase,
				    new_root.pbl_pbase);

	if (!ret)
		return -ETIME;
	else if (major_code)
		return -EIO;

	*actual_pbl_cnt = pbl_count + use_two_level;
	*used_4k_pbls = use_4k_pbls;
	return 0;
}


/**
 * nes_reg_phys_mr
 */
static struct ib_mr *nes_reg_phys_mr(struct ib_pd *ib_pd,
		struct ib_phys_buf *buffer_list, int num_phys_buf, int acc,
		u64 * iova_start)
{
	u64 region_length;
	struct nes_pd *nespd = to_nespd(ib_pd);
	struct nes_vnic *nesvnic = to_nesvnic(ib_pd->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_mr *nesmr;
	struct ib_mr *ibmr;
	struct nes_vpbl vpbl;
	struct nes_root_vpbl root_vpbl;
	u32 stag;
	u32 i;
	unsigned long mask;
	u32 stag_index = 0;
	u32 next_stag_index = 0;
	u32 driver_key = 0;
	u32 root_pbl_index = 0;
	u32 cur_pbl_index = 0;
	int err = 0;
	int ret = 0;
	u16 pbl_count = 0;
	u8 single_page = 1;
	u8 stag_key = 0;

	region_length = 0;
	vpbl.pbl_vbase = NULL;
	root_vpbl.pbl_vbase = NULL;
	root_vpbl.pbl_pbase = 0;

	get_random_bytes(&next_stag_index, sizeof(next_stag_index));
	stag_key = (u8)next_stag_index;

	driver_key = 0;

	next_stag_index >>= 8;
	next_stag_index %= nesadapter->max_mr;
	if (num_phys_buf > (1024*512)) {
		return ERR_PTR(-E2BIG);
	}

	if ((buffer_list[0].addr ^ *iova_start) & ~PAGE_MASK)
		return ERR_PTR(-EINVAL);

	err = nes_alloc_resource(nesadapter, nesadapter->allocated_mrs, nesadapter->max_mr,
			&stag_index, &next_stag_index, NES_RESOURCE_PHYS_MR);
	if (err) {
		return ERR_PTR(err);
	}

	nesmr = kzalloc(sizeof(*nesmr), GFP_KERNEL);
	if (!nesmr) {
		nes_free_resource(nesadapter, nesadapter->allocated_mrs, stag_index);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < num_phys_buf; i++) {

		if ((i & 0x01FF) == 0) {
			if (root_pbl_index == 1) {
				/* Allocate the root PBL */
				root_vpbl.pbl_vbase = pci_alloc_consistent(nesdev->pcidev, 8192,
						&root_vpbl.pbl_pbase);
				nes_debug(NES_DBG_MR, "Allocating root PBL, va = %p, pa = 0x%08X\n",
						root_vpbl.pbl_vbase, (unsigned int)root_vpbl.pbl_pbase);
				if (!root_vpbl.pbl_vbase) {
					pci_free_consistent(nesdev->pcidev, 4096, vpbl.pbl_vbase,
							vpbl.pbl_pbase);
					nes_free_resource(nesadapter, nesadapter->allocated_mrs, stag_index);
					kfree(nesmr);
					return ERR_PTR(-ENOMEM);
				}
				root_vpbl.leaf_vpbl = kzalloc(sizeof(*root_vpbl.leaf_vpbl)*1024, GFP_KERNEL);
				if (!root_vpbl.leaf_vpbl) {
					pci_free_consistent(nesdev->pcidev, 8192, root_vpbl.pbl_vbase,
							root_vpbl.pbl_pbase);
					pci_free_consistent(nesdev->pcidev, 4096, vpbl.pbl_vbase,
							vpbl.pbl_pbase);
					nes_free_resource(nesadapter, nesadapter->allocated_mrs, stag_index);
					kfree(nesmr);
					return ERR_PTR(-ENOMEM);
				}
				root_vpbl.pbl_vbase[0].pa_low = cpu_to_le32((u32)vpbl.pbl_pbase);
				root_vpbl.pbl_vbase[0].pa_high =
						cpu_to_le32((u32)((((u64)vpbl.pbl_pbase) >> 32)));
				root_vpbl.leaf_vpbl[0] = vpbl;
			}
			/* Allocate a 4K buffer for the PBL */
			vpbl.pbl_vbase = pci_alloc_consistent(nesdev->pcidev, 4096,
					&vpbl.pbl_pbase);
			nes_debug(NES_DBG_MR, "Allocating leaf PBL, va = %p, pa = 0x%016lX\n",
					vpbl.pbl_vbase, (unsigned long)vpbl.pbl_pbase);
			if (!vpbl.pbl_vbase) {
				nes_free_resource(nesadapter, nesadapter->allocated_mrs, stag_index);
				ibmr = ERR_PTR(-ENOMEM);
				kfree(nesmr);
				goto reg_phys_err;
			}
			/* Fill in the root table */
			if (1 <= root_pbl_index) {
				root_vpbl.pbl_vbase[root_pbl_index].pa_low =
						cpu_to_le32((u32)vpbl.pbl_pbase);
				root_vpbl.pbl_vbase[root_pbl_index].pa_high =
						cpu_to_le32((u32)((((u64)vpbl.pbl_pbase) >> 32)));
				root_vpbl.leaf_vpbl[root_pbl_index] = vpbl;
			}
			root_pbl_index++;
			cur_pbl_index = 0;
		}

		mask = !buffer_list[i].size;
		if (i != 0)
			mask |= buffer_list[i].addr;
		if (i != num_phys_buf - 1)
			mask |= buffer_list[i].addr + buffer_list[i].size;

		if (mask & ~PAGE_MASK) {
			nes_free_resource(nesadapter, nesadapter->allocated_mrs, stag_index);
			nes_debug(NES_DBG_MR, "Invalid buffer addr or size\n");
			ibmr = ERR_PTR(-EINVAL);
			kfree(nesmr);
			goto reg_phys_err;
		}

		region_length += buffer_list[i].size;
		if ((i != 0) && (single_page)) {
			if ((buffer_list[i-1].addr+PAGE_SIZE) != buffer_list[i].addr)
				single_page = 0;
		}
		vpbl.pbl_vbase[cur_pbl_index].pa_low = cpu_to_le32((u32)buffer_list[i].addr & PAGE_MASK);
		vpbl.pbl_vbase[cur_pbl_index++].pa_high =
				cpu_to_le32((u32)((((u64)buffer_list[i].addr) >> 32)));
	}

	stag = stag_index << 8;
	stag |= driver_key;
	stag += (u32)stag_key;

	nes_debug(NES_DBG_MR, "Registering STag 0x%08X, VA = 0x%016lX,"
			" length = 0x%016lX, index = 0x%08X\n",
			stag, (unsigned long)*iova_start, (unsigned long)region_length, stag_index);

	/* Make the leaf PBL the root if only one PBL */
	if (root_pbl_index == 1) {
		root_vpbl.pbl_pbase = vpbl.pbl_pbase;
	}

	if (single_page) {
		pbl_count = 0;
	} else {
		pbl_count = root_pbl_index;
	}
	ret = nes_reg_mr(nesdev, nespd, stag, region_length, &root_vpbl,
			buffer_list[0].addr, pbl_count, (u16)cur_pbl_index, acc, iova_start,
			&nesmr->pbls_used, &nesmr->pbl_4k);

	if (ret == 0) {
		nesmr->ibmr.rkey = stag;
		nesmr->ibmr.lkey = stag;
		nesmr->mode = IWNES_MEMREG_TYPE_MEM;
		ibmr = &nesmr->ibmr;
	} else {
		kfree(nesmr);
		ibmr = ERR_PTR(-ENOMEM);
	}

	reg_phys_err:
	/* free the resources */
	if (root_pbl_index == 1) {
		/* single PBL case */
		pci_free_consistent(nesdev->pcidev, 4096, vpbl.pbl_vbase, vpbl.pbl_pbase);
	} else {
		for (i=0; i<root_pbl_index; i++) {
			pci_free_consistent(nesdev->pcidev, 4096, root_vpbl.leaf_vpbl[i].pbl_vbase,
					root_vpbl.leaf_vpbl[i].pbl_pbase);
		}
		kfree(root_vpbl.leaf_vpbl);
		pci_free_consistent(nesdev->pcidev, 8192, root_vpbl.pbl_vbase,
				root_vpbl.pbl_pbase);
	}

	return ibmr;
}


/**
 * nes_get_dma_mr
 */
static struct ib_mr *nes_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct ib_phys_buf bl;
	u64 kva = 0;

	nes_debug(NES_DBG_MR, "\n");

	bl.size = (u64)0xffffffffffULL;
	bl.addr = 0;
	return nes_reg_phys_mr(pd, &bl, 1, acc, &kva);
}


/**
 * nes_reg_user_mr
 */
static struct ib_mr *nes_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
		u64 virt, int acc, struct ib_udata *udata)
{
	u64 iova_start;
	__le64 *pbl;
	u64 region_length;
	dma_addr_t last_dma_addr = 0;
	dma_addr_t first_dma_addr = 0;
	struct nes_pd *nespd = to_nespd(pd);
	struct nes_vnic *nesvnic = to_nesvnic(pd->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct ib_mr *ibmr = ERR_PTR(-EINVAL);
	struct scatterlist *sg;
	struct nes_ucontext *nes_ucontext;
	struct nes_pbl *nespbl;
	struct nes_mr *nesmr;
	struct ib_umem *region;
	struct nes_mem_reg_req req;
	struct nes_vpbl vpbl;
	struct nes_root_vpbl root_vpbl;
	int entry, page_index;
	int page_count = 0;
	int err, pbl_depth = 0;
	int chunk_pages;
	int ret;
	u32 stag;
	u32 stag_index = 0;
	u32 next_stag_index;
	u32 driver_key;
	u32 root_pbl_index = 0;
	u32 cur_pbl_index = 0;
	u32 skip_pages;
	u16 pbl_count;
	u8 single_page = 1;
	u8 stag_key;
	int first_page = 1;

	region = ib_umem_get(pd->uobject->context, start, length, acc, 0);
	if (IS_ERR(region)) {
		return (struct ib_mr *)region;
	}

	nes_debug(NES_DBG_MR, "User base = 0x%lX, Virt base = 0x%lX, length = %u,"
			" offset = %u, page size = %u.\n",
			(unsigned long int)start, (unsigned long int)virt, (u32)length,
			ib_umem_offset(region), region->page_size);

	skip_pages = ((u32)ib_umem_offset(region)) >> 12;

	if (ib_copy_from_udata(&req, udata, sizeof(req))) {
		ib_umem_release(region);
		return ERR_PTR(-EFAULT);
	}
	nes_debug(NES_DBG_MR, "Memory Registration type = %08X.\n", req.reg_type);

	switch (req.reg_type) {
		case IWNES_MEMREG_TYPE_MEM:
			pbl_depth = 0;
			region_length = 0;
			vpbl.pbl_vbase = NULL;
			root_vpbl.pbl_vbase = NULL;
			root_vpbl.pbl_pbase = 0;

			get_random_bytes(&next_stag_index, sizeof(next_stag_index));
			stag_key = (u8)next_stag_index;

			driver_key = next_stag_index & 0x70000000;

			next_stag_index >>= 8;
			next_stag_index %= nesadapter->max_mr;

			err = nes_alloc_resource(nesadapter, nesadapter->allocated_mrs,
					nesadapter->max_mr, &stag_index, &next_stag_index, NES_RESOURCE_USER_MR);
			if (err) {
				ib_umem_release(region);
				return ERR_PTR(err);
			}

			nesmr = kzalloc(sizeof(*nesmr), GFP_KERNEL);
			if (!nesmr) {
				ib_umem_release(region);
				nes_free_resource(nesadapter, nesadapter->allocated_mrs, stag_index);
				return ERR_PTR(-ENOMEM);
			}
			nesmr->region = region;

			for_each_sg(region->sg_head.sgl, sg, region->nmap, entry) {
				if (sg_dma_address(sg) & ~PAGE_MASK) {
					ib_umem_release(region);
					nes_free_resource(nesadapter, nesadapter->allocated_mrs, stag_index);
					nes_debug(NES_DBG_MR, "Unaligned Memory Buffer: 0x%x\n",
						  (unsigned int) sg_dma_address(sg));
					ibmr = ERR_PTR(-EINVAL);
					kfree(nesmr);
					goto reg_user_mr_err;
				}

				if (!sg_dma_len(sg)) {
					ib_umem_release(region);
					nes_free_resource(nesadapter, nesadapter->allocated_mrs,
							  stag_index);
					nes_debug(NES_DBG_MR, "Invalid Buffer Size\n");
					ibmr = ERR_PTR(-EINVAL);
					kfree(nesmr);
					goto reg_user_mr_err;
				}

				region_length += sg_dma_len(sg);
				chunk_pages = sg_dma_len(sg) >> 12;
				region_length -= skip_pages << 12;
				for (page_index = skip_pages; page_index < chunk_pages; page_index++) {
					skip_pages = 0;
					if ((page_count != 0) && (page_count << 12) - (ib_umem_offset(region) & (4096 - 1)) >= region->length)
						goto enough_pages;
					if ((page_count&0x01FF) == 0) {
						if (page_count >= 1024 * 512) {
							ib_umem_release(region);
							nes_free_resource(nesadapter,
									  nesadapter->allocated_mrs, stag_index);
							kfree(nesmr);
							ibmr = ERR_PTR(-E2BIG);
							goto reg_user_mr_err;
						}
						if (root_pbl_index == 1) {
							root_vpbl.pbl_vbase = pci_alloc_consistent(nesdev->pcidev,
									8192, &root_vpbl.pbl_pbase);
							nes_debug(NES_DBG_MR, "Allocating root PBL, va = %p, pa = 0x%08X\n",
								  root_vpbl.pbl_vbase, (unsigned int)root_vpbl.pbl_pbase);
							if (!root_vpbl.pbl_vbase) {
								ib_umem_release(region);
								pci_free_consistent(nesdev->pcidev, 4096, vpbl.pbl_vbase,
										    vpbl.pbl_pbase);
								nes_free_resource(nesadapter, nesadapter->allocated_mrs,
										  stag_index);
								kfree(nesmr);
								ibmr = ERR_PTR(-ENOMEM);
								goto reg_user_mr_err;
							}
							root_vpbl.leaf_vpbl = kzalloc(sizeof(*root_vpbl.leaf_vpbl)*1024,
									GFP_KERNEL);
							if (!root_vpbl.leaf_vpbl) {
								ib_umem_release(region);
								pci_free_consistent(nesdev->pcidev, 8192, root_vpbl.pbl_vbase,
										    root_vpbl.pbl_pbase);
								pci_free_consistent(nesdev->pcidev, 4096, vpbl.pbl_vbase,
										    vpbl.pbl_pbase);
								nes_free_resource(nesadapter, nesadapter->allocated_mrs,
										  stag_index);
								kfree(nesmr);
								ibmr = ERR_PTR(-ENOMEM);
								goto reg_user_mr_err;
							}
							root_vpbl.pbl_vbase[0].pa_low =
									cpu_to_le32((u32)vpbl.pbl_pbase);
							root_vpbl.pbl_vbase[0].pa_high =
									cpu_to_le32((u32)((((u64)vpbl.pbl_pbase) >> 32)));
							root_vpbl.leaf_vpbl[0] = vpbl;
						}
						vpbl.pbl_vbase = pci_alloc_consistent(nesdev->pcidev, 4096,
								&vpbl.pbl_pbase);
						nes_debug(NES_DBG_MR, "Allocating leaf PBL, va = %p, pa = 0x%08X\n",
							  vpbl.pbl_vbase, (unsigned int)vpbl.pbl_pbase);
						if (!vpbl.pbl_vbase) {
							ib_umem_release(region);
							nes_free_resource(nesadapter, nesadapter->allocated_mrs, stag_index);
							ibmr = ERR_PTR(-ENOMEM);
							kfree(nesmr);
							goto reg_user_mr_err;
						}
						if (1 <= root_pbl_index) {
							root_vpbl.pbl_vbase[root_pbl_index].pa_low =
									cpu_to_le32((u32)vpbl.pbl_pbase);
							root_vpbl.pbl_vbase[root_pbl_index].pa_high =
									cpu_to_le32((u32)((((u64)vpbl.pbl_pbase)>>32)));
							root_vpbl.leaf_vpbl[root_pbl_index] = vpbl;
						}
						root_pbl_index++;
						cur_pbl_index = 0;
					}
					if (single_page) {
						if (page_count != 0) {
							if ((last_dma_addr+4096) !=
									(sg_dma_address(sg)+
									(page_index*4096)))
								single_page = 0;
							last_dma_addr = sg_dma_address(sg)+
									(page_index*4096);
						} else {
							first_dma_addr = sg_dma_address(sg)+
									(page_index*4096);
							last_dma_addr = first_dma_addr;
						}
					}

					vpbl.pbl_vbase[cur_pbl_index].pa_low =
							cpu_to_le32((u32)(sg_dma_address(sg)+
							(page_index*4096)));
					vpbl.pbl_vbase[cur_pbl_index].pa_high =
							cpu_to_le32((u32)((((u64)(sg_dma_address(sg)+
							(page_index*4096))) >> 32)));
					cur_pbl_index++;
					page_count++;
				}
			}

			enough_pages:
			nes_debug(NES_DBG_MR, "calculating stag, stag_index=0x%08x, driver_key=0x%08x,"
					" stag_key=0x%08x\n",
					stag_index, driver_key, stag_key);
			stag = stag_index << 8;
			stag |= driver_key;
			stag += (u32)stag_key;

			iova_start = virt;
			/* Make the leaf PBL the root if only one PBL */
			if (root_pbl_index == 1) {
				root_vpbl.pbl_pbase = vpbl.pbl_pbase;
			}

			if (single_page) {
				pbl_count = 0;
			} else {
				pbl_count = root_pbl_index;
				first_dma_addr = 0;
			}
			nes_debug(NES_DBG_MR, "Registering STag 0x%08X, VA = 0x%08X, length = 0x%08X,"
					" index = 0x%08X, region->length=0x%08llx, pbl_count = %u\n",
					stag, (unsigned int)iova_start,
					(unsigned int)region_length, stag_index,
					(unsigned long long)region->length, pbl_count);
			ret = nes_reg_mr(nesdev, nespd, stag, region->length, &root_vpbl,
					 first_dma_addr, pbl_count, (u16)cur_pbl_index, acc,
					 &iova_start, &nesmr->pbls_used, &nesmr->pbl_4k);

			nes_debug(NES_DBG_MR, "ret=%d\n", ret);

			if (ret == 0) {
				nesmr->ibmr.rkey = stag;
				nesmr->ibmr.lkey = stag;
				nesmr->mode = IWNES_MEMREG_TYPE_MEM;
				ibmr = &nesmr->ibmr;
			} else {
				ib_umem_release(region);
				kfree(nesmr);
				ibmr = ERR_PTR(-ENOMEM);
			}

			reg_user_mr_err:
			/* free the resources */
			if (root_pbl_index == 1) {
				pci_free_consistent(nesdev->pcidev, 4096, vpbl.pbl_vbase,
						vpbl.pbl_pbase);
			} else {
				for (page_index=0; page_index<root_pbl_index; page_index++) {
					pci_free_consistent(nesdev->pcidev, 4096,
							root_vpbl.leaf_vpbl[page_index].pbl_vbase,
							root_vpbl.leaf_vpbl[page_index].pbl_pbase);
				}
				kfree(root_vpbl.leaf_vpbl);
				pci_free_consistent(nesdev->pcidev, 8192, root_vpbl.pbl_vbase,
						root_vpbl.pbl_pbase);
			}

			nes_debug(NES_DBG_MR, "Leaving, ibmr=%p", ibmr);

			return ibmr;
		case IWNES_MEMREG_TYPE_QP:
		case IWNES_MEMREG_TYPE_CQ:
			if (!region->length) {
				nes_debug(NES_DBG_MR, "Unable to register zero length region for CQ\n");
				ib_umem_release(region);
				return ERR_PTR(-EINVAL);
			}
			nespbl = kzalloc(sizeof(*nespbl), GFP_KERNEL);
			if (!nespbl) {
				nes_debug(NES_DBG_MR, "Unable to allocate PBL\n");
				ib_umem_release(region);
				return ERR_PTR(-ENOMEM);
			}
			nesmr = kzalloc(sizeof(*nesmr), GFP_KERNEL);
			if (!nesmr) {
				ib_umem_release(region);
				kfree(nespbl);
				nes_debug(NES_DBG_MR, "Unable to allocate nesmr\n");
				return ERR_PTR(-ENOMEM);
			}
			nesmr->region = region;
			nes_ucontext = to_nesucontext(pd->uobject->context);
			pbl_depth = region->length >> 12;
			pbl_depth += (region->length & (4096-1)) ? 1 : 0;
			nespbl->pbl_size = pbl_depth*sizeof(u64);
			if (req.reg_type == IWNES_MEMREG_TYPE_QP) {
				nes_debug(NES_DBG_MR, "Attempting to allocate QP PBL memory");
			} else {
				nes_debug(NES_DBG_MR, "Attempting to allocate CP PBL memory");
			}

			nes_debug(NES_DBG_MR, " %u bytes, %u entries.\n",
					nespbl->pbl_size, pbl_depth);
			pbl = pci_alloc_consistent(nesdev->pcidev, nespbl->pbl_size,
					&nespbl->pbl_pbase);
			if (!pbl) {
				ib_umem_release(region);
				kfree(nesmr);
				kfree(nespbl);
				nes_debug(NES_DBG_MR, "Unable to allocate PBL memory\n");
				return ERR_PTR(-ENOMEM);
			}

			nespbl->pbl_vbase = (u64 *)pbl;
			nespbl->user_base = start;
			nes_debug(NES_DBG_MR, "Allocated PBL memory, %u bytes, pbl_pbase=%lx,"
					" pbl_vbase=%p user_base=0x%lx\n",
				  nespbl->pbl_size, (unsigned long) nespbl->pbl_pbase,
				  (void *) nespbl->pbl_vbase, nespbl->user_base);

			for_each_sg(region->sg_head.sgl, sg, region->nmap, entry) {
				chunk_pages = sg_dma_len(sg) >> 12;
				chunk_pages += (sg_dma_len(sg) & (4096-1)) ? 1 : 0;
				if (first_page) {
					nespbl->page = sg_page(sg);
					first_page = 0;
				}

				for (page_index = 0; page_index < chunk_pages; page_index++) {
					((__le32 *)pbl)[0] = cpu_to_le32((u32)
							(sg_dma_address(sg)+
							(page_index*4096)));
					((__le32 *)pbl)[1] = cpu_to_le32(((u64)
							(sg_dma_address(sg)+
							(page_index*4096)))>>32);
					nes_debug(NES_DBG_MR, "pbl=%p, *pbl=0x%016llx, 0x%08x%08x\n", pbl,
						  (unsigned long long)*pbl,
						  le32_to_cpu(((__le32 *)pbl)[1]), le32_to_cpu(((__le32 *)pbl)[0]));
					pbl++;
				}
			}

			if (req.reg_type == IWNES_MEMREG_TYPE_QP) {
				list_add_tail(&nespbl->list, &nes_ucontext->qp_reg_mem_list);
			} else {
				list_add_tail(&nespbl->list, &nes_ucontext->cq_reg_mem_list);
			}
			nesmr->ibmr.rkey = -1;
			nesmr->ibmr.lkey = -1;
			nesmr->mode = req.reg_type;
			return &nesmr->ibmr;
	}

	ib_umem_release(region);
	return ERR_PTR(-ENOSYS);
}


/**
 * nes_dereg_mr
 */
static int nes_dereg_mr(struct ib_mr *ib_mr)
{
	struct nes_mr *nesmr = to_nesmr(ib_mr);
	struct nes_vnic *nesvnic = to_nesvnic(ib_mr->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_cqp_request *cqp_request;
	unsigned long flags;
	int ret;
	u16 major_code;
	u16 minor_code;

	if (nesmr->region) {
		ib_umem_release(nesmr->region);
	}
	if (nesmr->mode != IWNES_MEMREG_TYPE_MEM) {
		kfree(nesmr);
		return 0;
	}

	/* Deallocate the region with the adapter */

	cqp_request = nes_get_cqp_request(nesdev);
	if (cqp_request == NULL) {
		nes_debug(NES_DBG_MR, "Failed to get a cqp_request.\n");
		return -ENOMEM;
	}
	cqp_request->waiting = 1;
	cqp_wqe = &cqp_request->cqp_wqe;

	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX,
			NES_CQP_DEALLOCATE_STAG | NES_CQP_STAG_VA_TO |
			NES_CQP_STAG_DEALLOC_PBLS | NES_CQP_STAG_MR);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_STAG_WQE_STAG_IDX, ib_mr->rkey);

	atomic_set(&cqp_request->refcount, 2);
	nes_post_cqp_request(nesdev, cqp_request);

	/* Wait for CQP */
	nes_debug(NES_DBG_MR, "Waiting for deallocate STag 0x%08X completed\n", ib_mr->rkey);
	ret = wait_event_timeout(cqp_request->waitq, (cqp_request->request_done != 0),
			NES_EVENT_TIMEOUT);
	nes_debug(NES_DBG_MR, "Deallocate STag 0x%08X completed, wait_event_timeout ret = %u,"
			" CQP Major:Minor codes = 0x%04X:0x%04X\n",
			ib_mr->rkey, ret, cqp_request->major_code, cqp_request->minor_code);

	major_code = cqp_request->major_code;
	minor_code = cqp_request->minor_code;

	nes_put_cqp_request(nesdev, cqp_request);

	if (!ret) {
		nes_debug(NES_DBG_MR, "Timeout waiting to destroy STag,"
				" ib_mr=%p, rkey = 0x%08X\n",
				ib_mr, ib_mr->rkey);
		return -ETIME;
	} else if (major_code) {
		nes_debug(NES_DBG_MR, "Error (0x%04X:0x%04X) while attempting"
				" to destroy STag, ib_mr=%p, rkey = 0x%08X\n",
				major_code, minor_code, ib_mr, ib_mr->rkey);
		return -EIO;
	}

	if (nesmr->pbls_used != 0) {
		spin_lock_irqsave(&nesadapter->pbl_lock, flags);
		if (nesmr->pbl_4k) {
			nesadapter->free_4kpbl += nesmr->pbls_used;
			if (nesadapter->free_4kpbl > nesadapter->max_4kpbl)
				printk(KERN_ERR PFX "free 4KB PBLs(%u) has "
					"exceeded the max(%u)\n",
					nesadapter->free_4kpbl,
					nesadapter->max_4kpbl);
		} else {
			nesadapter->free_256pbl += nesmr->pbls_used;
			if (nesadapter->free_256pbl > nesadapter->max_256pbl)
				printk(KERN_ERR PFX "free 256B PBLs(%u) has "
					"exceeded the max(%u)\n",
					nesadapter->free_256pbl,
					nesadapter->max_256pbl);
		}
		spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
	}
	nes_free_resource(nesadapter, nesadapter->allocated_mrs,
			(ib_mr->rkey & 0x0fffff00) >> 8);

	kfree(nesmr);

	return 0;
}


/**
 * show_rev
 */
static ssize_t show_rev(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct nes_ib_device *nesibdev =
			container_of(dev, struct nes_ib_device, ibdev.dev);
	struct nes_vnic *nesvnic = nesibdev->nesvnic;

	nes_debug(NES_DBG_INIT, "\n");
	return sprintf(buf, "%x\n", nesvnic->nesdev->nesadapter->hw_rev);
}


/**
 * show_fw_ver
 */
static ssize_t show_fw_ver(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct nes_ib_device *nesibdev =
			container_of(dev, struct nes_ib_device, ibdev.dev);
	struct nes_vnic *nesvnic = nesibdev->nesvnic;

	nes_debug(NES_DBG_INIT, "\n");
	return sprintf(buf, "%u.%u\n",
		(nesvnic->nesdev->nesadapter->firmware_version >> 16),
		(nesvnic->nesdev->nesadapter->firmware_version & 0x000000ff));
}


/**
 * show_hca
 */
static ssize_t show_hca(struct device *dev, struct device_attribute *attr,
		        char *buf)
{
	nes_debug(NES_DBG_INIT, "\n");
	return sprintf(buf, "NES020\n");
}


/**
 * show_board
 */
static ssize_t show_board(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	nes_debug(NES_DBG_INIT, "\n");
	return sprintf(buf, "%.*s\n", 32, "NES020 Board ID");
}


static DEVICE_ATTR(hw_rev, S_IRUGO, show_rev, NULL);
static DEVICE_ATTR(fw_ver, S_IRUGO, show_fw_ver, NULL);
static DEVICE_ATTR(hca_type, S_IRUGO, show_hca, NULL);
static DEVICE_ATTR(board_id, S_IRUGO, show_board, NULL);

static struct device_attribute *nes_dev_attributes[] = {
	&dev_attr_hw_rev,
	&dev_attr_fw_ver,
	&dev_attr_hca_type,
	&dev_attr_board_id
};


/**
 * nes_query_qp
 */
static int nes_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		int attr_mask, struct ib_qp_init_attr *init_attr)
{
	struct nes_qp *nesqp = to_nesqp(ibqp);

	nes_debug(NES_DBG_QP, "\n");

	attr->qp_access_flags = 0;
	attr->cap.max_send_wr = nesqp->hwqp.sq_size;
	attr->cap.max_recv_wr = nesqp->hwqp.rq_size;
	attr->cap.max_recv_sge = 1;
	if (nes_drv_opt & NES_DRV_OPT_NO_INLINE_DATA)
		attr->cap.max_inline_data = 0;
	else
		attr->cap.max_inline_data = 64;

	init_attr->event_handler = nesqp->ibqp.event_handler;
	init_attr->qp_context = nesqp->ibqp.qp_context;
	init_attr->send_cq = nesqp->ibqp.send_cq;
	init_attr->recv_cq = nesqp->ibqp.recv_cq;
	init_attr->srq = nesqp->ibqp.srq;
	init_attr->cap = attr->cap;

	return 0;
}


/**
 * nes_hw_modify_qp
 */
int nes_hw_modify_qp(struct nes_device *nesdev, struct nes_qp *nesqp,
		u32 next_iwarp_state, u32 termlen, u32 wait_completion)
{
	struct nes_hw_cqp_wqe *cqp_wqe;
	/* struct iw_cm_id *cm_id = nesqp->cm_id; */
	/* struct iw_cm_event cm_event; */
	struct nes_cqp_request *cqp_request;
	int ret;
	u16 major_code;

	nes_debug(NES_DBG_MOD_QP, "QP%u, refcount=%d\n",
			nesqp->hwqp.qp_id, atomic_read(&nesqp->refcount));

	cqp_request = nes_get_cqp_request(nesdev);
	if (cqp_request == NULL) {
		nes_debug(NES_DBG_MOD_QP, "Failed to get a cqp_request.\n");
		return -ENOMEM;
	}
	if (wait_completion) {
		cqp_request->waiting = 1;
	} else {
		cqp_request->waiting = 0;
	}
	cqp_wqe = &cqp_request->cqp_wqe;

	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX,
			NES_CQP_MODIFY_QP | NES_CQP_QP_TYPE_IWARP | next_iwarp_state);
	nes_debug(NES_DBG_MOD_QP, "using next_iwarp_state=%08x, wqe_words=%08x\n",
			next_iwarp_state, le32_to_cpu(cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX]));
	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_ID_IDX, nesqp->hwqp.qp_id);
	set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_QP_WQE_CONTEXT_LOW_IDX, (u64)nesqp->nesqp_context_pbase);

	/* If sending a terminate message, fill in the length (in words) */
	if (((next_iwarp_state & NES_CQP_QP_IWARP_STATE_MASK) == NES_CQP_QP_IWARP_STATE_TERMINATE) &&
	    !(next_iwarp_state & NES_CQP_QP_TERM_DONT_SEND_TERM_MSG)) {
		termlen = ((termlen + 3) >> 2) << NES_CQP_OP_TERMLEN_SHIFT;
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_QP_WQE_NEW_MSS_IDX, termlen);
	}

	atomic_set(&cqp_request->refcount, 2);
	nes_post_cqp_request(nesdev, cqp_request);

	/* Wait for CQP */
	if (wait_completion) {
		/* nes_debug(NES_DBG_MOD_QP, "Waiting for modify iWARP QP%u to complete.\n",
				nesqp->hwqp.qp_id); */
		ret = wait_event_timeout(cqp_request->waitq, (cqp_request->request_done != 0),
				NES_EVENT_TIMEOUT);
		nes_debug(NES_DBG_MOD_QP, "Modify iwarp QP%u completed, wait_event_timeout ret=%u, "
				"CQP Major:Minor codes = 0x%04X:0x%04X.\n",
				nesqp->hwqp.qp_id, ret, cqp_request->major_code, cqp_request->minor_code);
		major_code = cqp_request->major_code;
		if (major_code) {
			nes_debug(NES_DBG_MOD_QP, "Modify iwarp QP%u failed"
					"CQP Major:Minor codes = 0x%04X:0x%04X, intended next state = 0x%08X.\n",
					nesqp->hwqp.qp_id, cqp_request->major_code,
					cqp_request->minor_code, next_iwarp_state);
		}

		nes_put_cqp_request(nesdev, cqp_request);

		if (!ret)
			return -ETIME;
		else if (major_code)
			return -EIO;
		else
			return 0;
	} else {
		return 0;
	}
}


/**
 * nes_modify_qp
 */
int nes_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		int attr_mask, struct ib_udata *udata)
{
	struct nes_qp *nesqp = to_nesqp(ibqp);
	struct nes_vnic *nesvnic = to_nesvnic(ibqp->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	/* u32 cqp_head; */
	/* u32 counter; */
	u32 next_iwarp_state = 0;
	int err;
	unsigned long qplockflags;
	int ret;
	u16 original_last_aeq;
	u8 issue_modify_qp = 0;
	u8 dont_wait = 0;

	nes_debug(NES_DBG_MOD_QP, "QP%u: QP State=%u, cur QP State=%u,"
			" iwarp_state=0x%X, refcount=%d\n",
			nesqp->hwqp.qp_id, attr->qp_state, nesqp->ibqp_state,
			nesqp->iwarp_state, atomic_read(&nesqp->refcount));

	spin_lock_irqsave(&nesqp->lock, qplockflags);

	nes_debug(NES_DBG_MOD_QP, "QP%u: hw_iwarp_state=0x%X, hw_tcp_state=0x%X,"
			" QP Access Flags=0x%X, attr_mask = 0x%0x\n",
			nesqp->hwqp.qp_id, nesqp->hw_iwarp_state,
			nesqp->hw_tcp_state, attr->qp_access_flags, attr_mask);

	if (attr_mask & IB_QP_STATE) {
		switch (attr->qp_state) {
			case IB_QPS_INIT:
				nes_debug(NES_DBG_MOD_QP, "QP%u: new state = init\n",
						nesqp->hwqp.qp_id);
				if (nesqp->iwarp_state > (u32)NES_CQP_QP_IWARP_STATE_IDLE) {
					spin_unlock_irqrestore(&nesqp->lock, qplockflags);
					return -EINVAL;
				}
				next_iwarp_state = NES_CQP_QP_IWARP_STATE_IDLE;
				issue_modify_qp = 1;
				break;
			case IB_QPS_RTR:
				nes_debug(NES_DBG_MOD_QP, "QP%u: new state = rtr\n",
						nesqp->hwqp.qp_id);
				if (nesqp->iwarp_state>(u32)NES_CQP_QP_IWARP_STATE_IDLE) {
					spin_unlock_irqrestore(&nesqp->lock, qplockflags);
					return -EINVAL;
				}
				next_iwarp_state = NES_CQP_QP_IWARP_STATE_IDLE;
				issue_modify_qp = 1;
				break;
			case IB_QPS_RTS:
				nes_debug(NES_DBG_MOD_QP, "QP%u: new state = rts\n",
						nesqp->hwqp.qp_id);
				if (nesqp->iwarp_state>(u32)NES_CQP_QP_IWARP_STATE_RTS) {
					spin_unlock_irqrestore(&nesqp->lock, qplockflags);
					return -EINVAL;
				}
				if (nesqp->cm_id == NULL) {
					nes_debug(NES_DBG_MOD_QP, "QP%u: Failing attempt to move QP to RTS without a CM_ID. \n",
							nesqp->hwqp.qp_id );
					spin_unlock_irqrestore(&nesqp->lock, qplockflags);
					return -EINVAL;
				}
				next_iwarp_state = NES_CQP_QP_IWARP_STATE_RTS;
				if (nesqp->iwarp_state != NES_CQP_QP_IWARP_STATE_RTS)
					next_iwarp_state |= NES_CQP_QP_CONTEXT_VALID |
							NES_CQP_QP_ARP_VALID | NES_CQP_QP_ORD_VALID;
				issue_modify_qp = 1;
				nesqp->hw_tcp_state = NES_AEQE_TCP_STATE_ESTABLISHED;
				nesqp->hw_iwarp_state = NES_AEQE_IWARP_STATE_RTS;
				nesqp->hte_added = 1;
				break;
			case IB_QPS_SQD:
				issue_modify_qp = 1;
				nes_debug(NES_DBG_MOD_QP, "QP%u: new state=closing. SQ head=%u, SQ tail=%u\n",
						nesqp->hwqp.qp_id, nesqp->hwqp.sq_head, nesqp->hwqp.sq_tail);
				if (nesqp->iwarp_state == (u32)NES_CQP_QP_IWARP_STATE_CLOSING) {
					spin_unlock_irqrestore(&nesqp->lock, qplockflags);
					return 0;
				} else {
					if (nesqp->iwarp_state > (u32)NES_CQP_QP_IWARP_STATE_CLOSING) {
						nes_debug(NES_DBG_MOD_QP, "QP%u: State change to closing"
								" ignored due to current iWARP state\n",
								nesqp->hwqp.qp_id);
						spin_unlock_irqrestore(&nesqp->lock, qplockflags);
						return -EINVAL;
					}
					if (nesqp->hw_iwarp_state != NES_AEQE_IWARP_STATE_RTS) {
						nes_debug(NES_DBG_MOD_QP, "QP%u: State change to closing"
								" already done based on hw state.\n",
								nesqp->hwqp.qp_id);
						issue_modify_qp = 0;
					}
					switch (nesqp->hw_iwarp_state) {
						case NES_AEQE_IWARP_STATE_CLOSING:
							next_iwarp_state = NES_CQP_QP_IWARP_STATE_CLOSING;
							break;
						case NES_AEQE_IWARP_STATE_TERMINATE:
							next_iwarp_state = NES_CQP_QP_IWARP_STATE_TERMINATE;
							break;
						case NES_AEQE_IWARP_STATE_ERROR:
							next_iwarp_state = NES_CQP_QP_IWARP_STATE_ERROR;
							break;
						default:
							next_iwarp_state = NES_CQP_QP_IWARP_STATE_CLOSING;
							nesqp->hw_iwarp_state = NES_AEQE_IWARP_STATE_CLOSING;
							break;
					}
				}
				break;
			case IB_QPS_SQE:
				nes_debug(NES_DBG_MOD_QP, "QP%u: new state = terminate\n",
						nesqp->hwqp.qp_id);
				if (nesqp->iwarp_state>=(u32)NES_CQP_QP_IWARP_STATE_TERMINATE) {
					spin_unlock_irqrestore(&nesqp->lock, qplockflags);
					return -EINVAL;
				}
				/* next_iwarp_state = (NES_CQP_QP_IWARP_STATE_TERMINATE | 0x02000000); */
				next_iwarp_state = NES_CQP_QP_IWARP_STATE_TERMINATE;
				nesqp->hw_iwarp_state = NES_AEQE_IWARP_STATE_TERMINATE;
				issue_modify_qp = 1;
				break;
			case IB_QPS_ERR:
			case IB_QPS_RESET:
				if (nesqp->iwarp_state == (u32)NES_CQP_QP_IWARP_STATE_ERROR) {
					spin_unlock_irqrestore(&nesqp->lock, qplockflags);
					return -EINVAL;
				}
				nes_debug(NES_DBG_MOD_QP, "QP%u: new state = error\n",
						nesqp->hwqp.qp_id);
				if (nesqp->term_flags)
					del_timer(&nesqp->terminate_timer);

				next_iwarp_state = NES_CQP_QP_IWARP_STATE_ERROR;
				/* next_iwarp_state = (NES_CQP_QP_IWARP_STATE_TERMINATE | 0x02000000); */
					if (nesqp->hte_added) {
						nes_debug(NES_DBG_MOD_QP, "set CQP_QP_DEL_HTE\n");
						next_iwarp_state |= NES_CQP_QP_DEL_HTE;
						nesqp->hte_added = 0;
					}
				if ((nesqp->hw_tcp_state > NES_AEQE_TCP_STATE_CLOSED) &&
						(nesdev->iw_status) &&
						(nesqp->hw_tcp_state != NES_AEQE_TCP_STATE_TIME_WAIT)) {
					next_iwarp_state |= NES_CQP_QP_RESET;
				} else {
					nes_debug(NES_DBG_MOD_QP, "QP%u NOT setting NES_CQP_QP_RESET since TCP state = %u\n",
							nesqp->hwqp.qp_id, nesqp->hw_tcp_state);
					dont_wait = 1;
				}
				issue_modify_qp = 1;
				nesqp->hw_iwarp_state = NES_AEQE_IWARP_STATE_ERROR;
				break;
			default:
				spin_unlock_irqrestore(&nesqp->lock, qplockflags);
				return -EINVAL;
				break;
		}

		nesqp->ibqp_state = attr->qp_state;
		nesqp->iwarp_state = next_iwarp_state & NES_CQP_QP_IWARP_STATE_MASK;
		nes_debug(NES_DBG_MOD_QP, "Change nesqp->iwarp_state=%08x\n",
				nesqp->iwarp_state);
	}

	if (attr_mask & IB_QP_ACCESS_FLAGS) {
		if (attr->qp_access_flags & IB_ACCESS_LOCAL_WRITE) {
			nesqp->nesqp_context->misc |= cpu_to_le32(NES_QPCONTEXT_MISC_RDMA_WRITE_EN |
					NES_QPCONTEXT_MISC_RDMA_READ_EN);
			issue_modify_qp = 1;
		}
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_WRITE) {
			nesqp->nesqp_context->misc |= cpu_to_le32(NES_QPCONTEXT_MISC_RDMA_WRITE_EN);
			issue_modify_qp = 1;
		}
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_READ) {
			nesqp->nesqp_context->misc |= cpu_to_le32(NES_QPCONTEXT_MISC_RDMA_READ_EN);
			issue_modify_qp = 1;
		}
		if (attr->qp_access_flags & IB_ACCESS_MW_BIND) {
			nesqp->nesqp_context->misc |= cpu_to_le32(NES_QPCONTEXT_MISC_WBIND_EN);
			issue_modify_qp = 1;
		}

		if (nesqp->user_mode) {
			nesqp->nesqp_context->misc |= cpu_to_le32(NES_QPCONTEXT_MISC_RDMA_WRITE_EN |
					NES_QPCONTEXT_MISC_RDMA_READ_EN);
			issue_modify_qp = 1;
		}
	}

	original_last_aeq = nesqp->last_aeq;
	spin_unlock_irqrestore(&nesqp->lock, qplockflags);

	nes_debug(NES_DBG_MOD_QP, "issue_modify_qp=%u\n", issue_modify_qp);

	ret = 0;


	if (issue_modify_qp) {
		nes_debug(NES_DBG_MOD_QP, "call nes_hw_modify_qp\n");
		ret = nes_hw_modify_qp(nesdev, nesqp, next_iwarp_state, 0, 1);
		if (ret)
			nes_debug(NES_DBG_MOD_QP, "nes_hw_modify_qp (next_iwarp_state = 0x%08X)"
					" failed for QP%u.\n",
					next_iwarp_state, nesqp->hwqp.qp_id);

	}

	if ((issue_modify_qp) && (nesqp->ibqp_state > IB_QPS_RTS)) {
		nes_debug(NES_DBG_MOD_QP, "QP%u Issued ModifyQP refcount (%d),"
				" original_last_aeq = 0x%04X. last_aeq = 0x%04X.\n",
				nesqp->hwqp.qp_id, atomic_read(&nesqp->refcount),
				original_last_aeq, nesqp->last_aeq);
		if (!ret || original_last_aeq != NES_AEQE_AEID_RDMAP_ROE_BAD_LLP_CLOSE) {
			if (dont_wait) {
				if (nesqp->cm_id && nesqp->hw_tcp_state != 0) {
					nes_debug(NES_DBG_MOD_QP, "QP%u Queuing fake disconnect for QP refcount (%d),"
							" original_last_aeq = 0x%04X. last_aeq = 0x%04X.\n",
							nesqp->hwqp.qp_id, atomic_read(&nesqp->refcount),
							original_last_aeq, nesqp->last_aeq);
					/* this one is for the cm_disconnect thread */
					spin_lock_irqsave(&nesqp->lock, qplockflags);
					nesqp->hw_tcp_state = NES_AEQE_TCP_STATE_CLOSED;
					nesqp->last_aeq = NES_AEQE_AEID_RESET_SENT;
					spin_unlock_irqrestore(&nesqp->lock, qplockflags);
					nes_cm_disconn(nesqp);
				} else {
					nes_debug(NES_DBG_MOD_QP, "QP%u No fake disconnect, QP refcount=%d\n",
							nesqp->hwqp.qp_id, atomic_read(&nesqp->refcount));
				}
			} else {
				spin_lock_irqsave(&nesqp->lock, qplockflags);
				if (nesqp->cm_id) {
					/* These two are for the timer thread */
					if (atomic_inc_return(&nesqp->close_timer_started) == 1) {
						nesqp->cm_id->add_ref(nesqp->cm_id);
						nes_debug(NES_DBG_MOD_QP, "QP%u Not decrementing QP refcount (%d),"
								" need ae to finish up, original_last_aeq = 0x%04X."
								" last_aeq = 0x%04X, scheduling timer.\n",
								nesqp->hwqp.qp_id, atomic_read(&nesqp->refcount),
								original_last_aeq, nesqp->last_aeq);
						schedule_nes_timer(nesqp->cm_node, (struct sk_buff *) nesqp, NES_TIMER_TYPE_CLOSE, 1, 0);
					}
					spin_unlock_irqrestore(&nesqp->lock, qplockflags);
				} else {
					spin_unlock_irqrestore(&nesqp->lock, qplockflags);
					nes_debug(NES_DBG_MOD_QP, "QP%u Not decrementing QP refcount (%d),"
							" need ae to finish up, original_last_aeq = 0x%04X."
							" last_aeq = 0x%04X.\n",
							nesqp->hwqp.qp_id, atomic_read(&nesqp->refcount),
							original_last_aeq, nesqp->last_aeq);
				}
			}
		} else {
			nes_debug(NES_DBG_MOD_QP, "QP%u Decrementing QP refcount (%d), No ae to finish up,"
					" original_last_aeq = 0x%04X. last_aeq = 0x%04X.\n",
					nesqp->hwqp.qp_id, atomic_read(&nesqp->refcount),
					original_last_aeq, nesqp->last_aeq);
		}
	} else {
		nes_debug(NES_DBG_MOD_QP, "QP%u Decrementing QP refcount (%d), No ae to finish up,"
				" original_last_aeq = 0x%04X. last_aeq = 0x%04X.\n",
				nesqp->hwqp.qp_id, atomic_read(&nesqp->refcount),
				original_last_aeq, nesqp->last_aeq);
	}

	err = 0;

	nes_debug(NES_DBG_MOD_QP, "QP%u Leaving, refcount=%d\n",
			nesqp->hwqp.qp_id, atomic_read(&nesqp->refcount));

	return err;
}


/**
 * nes_muticast_attach
 */
static int nes_multicast_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	nes_debug(NES_DBG_INIT, "\n");
	return -ENOSYS;
}


/**
 * nes_multicast_detach
 */
static int nes_multicast_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	nes_debug(NES_DBG_INIT, "\n");
	return -ENOSYS;
}


/**
 * nes_process_mad
 */
static int nes_process_mad(struct ib_device *ibdev, int mad_flags,
		u8 port_num, struct ib_wc *in_wc, struct ib_grh *in_grh,
		struct ib_mad *in_mad, struct ib_mad *out_mad)
{
	nes_debug(NES_DBG_INIT, "\n");
	return -ENOSYS;
}

static inline void
fill_wqe_sg_send(struct nes_hw_qp_wqe *wqe, struct ib_send_wr *ib_wr, u32 uselkey)
{
	int sge_index;
	int total_payload_length = 0;
	for (sge_index = 0; sge_index < ib_wr->num_sge; sge_index++) {
		set_wqe_64bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_FRAG0_LOW_IDX+(sge_index*4),
			ib_wr->sg_list[sge_index].addr);
		set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_LENGTH0_IDX + (sge_index*4),
			ib_wr->sg_list[sge_index].length);
		if (uselkey)
			set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_STAG0_IDX + (sge_index*4),
						(ib_wr->sg_list[sge_index].lkey));
		else
			set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_STAG0_IDX + (sge_index*4), 0);

		total_payload_length += ib_wr->sg_list[sge_index].length;
	}
	nes_debug(NES_DBG_IW_TX, "UC UC UC, sending total_payload_length=%u \n",
			total_payload_length);
	set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_TOTAL_PAYLOAD_IDX,
				total_payload_length);
}

/**
 * nes_post_send
 */
static int nes_post_send(struct ib_qp *ibqp, struct ib_send_wr *ib_wr,
		struct ib_send_wr **bad_wr)
{
	u64 u64temp;
	unsigned long flags = 0;
	struct nes_vnic *nesvnic = to_nesvnic(ibqp->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_qp *nesqp = to_nesqp(ibqp);
	struct nes_hw_qp_wqe *wqe;
	int err = 0;
	u32 qsize = nesqp->hwqp.sq_size;
	u32 head;
	u32 wqe_misc = 0;
	u32 wqe_count = 0;
	u32 counter;

	if (nesqp->ibqp_state > IB_QPS_RTS) {
		err = -EINVAL;
		goto out;
	}

	spin_lock_irqsave(&nesqp->lock, flags);

	head = nesqp->hwqp.sq_head;

	while (ib_wr) {
		/* Check for QP error */
		if (nesqp->term_flags) {
			err = -EINVAL;
			break;
		}

		/* Check for SQ overflow */
		if (((head + (2 * qsize) - nesqp->hwqp.sq_tail) % qsize) == (qsize - 1)) {
			err = -ENOMEM;
			break;
		}

		wqe = &nesqp->hwqp.sq_vbase[head];
		/* nes_debug(NES_DBG_IW_TX, "processing sq wqe for QP%u at %p, head = %u.\n",
				nesqp->hwqp.qp_id, wqe, head); */
		nes_fill_init_qp_wqe(wqe, nesqp, head);
		u64temp = (u64)(ib_wr->wr_id);
		set_wqe_64bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_COMP_SCRATCH_LOW_IDX,
					u64temp);
		switch (ib_wr->opcode) {
		case IB_WR_SEND:
		case IB_WR_SEND_WITH_INV:
			if (IB_WR_SEND == ib_wr->opcode) {
				if (ib_wr->send_flags & IB_SEND_SOLICITED)
					wqe_misc = NES_IWARP_SQ_OP_SENDSE;
				else
					wqe_misc = NES_IWARP_SQ_OP_SEND;
			} else {
				if (ib_wr->send_flags & IB_SEND_SOLICITED)
					wqe_misc = NES_IWARP_SQ_OP_SENDSEINV;
				else
					wqe_misc = NES_IWARP_SQ_OP_SENDINV;

				set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_INV_STAG_LOW_IDX,
						    ib_wr->ex.invalidate_rkey);
			}

			if (ib_wr->num_sge > nesdev->nesadapter->max_sge) {
				err = -EINVAL;
				break;
			}

			if (ib_wr->send_flags & IB_SEND_FENCE)
				wqe_misc |= NES_IWARP_SQ_WQE_LOCAL_FENCE;

			if ((ib_wr->send_flags & IB_SEND_INLINE) &&
			    ((nes_drv_opt & NES_DRV_OPT_NO_INLINE_DATA) == 0) &&
			     (ib_wr->sg_list[0].length <= 64)) {
				memcpy(&wqe->wqe_words[NES_IWARP_SQ_WQE_IMM_DATA_START_IDX],
				       (void *)(unsigned long)ib_wr->sg_list[0].addr, ib_wr->sg_list[0].length);
				set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_TOTAL_PAYLOAD_IDX,
						    ib_wr->sg_list[0].length);
				wqe_misc |= NES_IWARP_SQ_WQE_IMM_DATA;
			} else {
				fill_wqe_sg_send(wqe, ib_wr, 1);
			}

			break;
		case IB_WR_RDMA_WRITE:
			wqe_misc = NES_IWARP_SQ_OP_RDMAW;
			if (ib_wr->num_sge > nesdev->nesadapter->max_sge) {
				nes_debug(NES_DBG_IW_TX, "Exceeded max sge, ib_wr=%u, max=%u\n",
					  ib_wr->num_sge, nesdev->nesadapter->max_sge);
				err = -EINVAL;
				break;
			}

			if (ib_wr->send_flags & IB_SEND_FENCE)
				wqe_misc |= NES_IWARP_SQ_WQE_LOCAL_FENCE;

			set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_RDMA_STAG_IDX,
					    ib_wr->wr.rdma.rkey);
			set_wqe_64bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_RDMA_TO_LOW_IDX,
					    ib_wr->wr.rdma.remote_addr);

			if ((ib_wr->send_flags & IB_SEND_INLINE) &&
			    ((nes_drv_opt & NES_DRV_OPT_NO_INLINE_DATA) == 0) &&
			     (ib_wr->sg_list[0].length <= 64)) {
				memcpy(&wqe->wqe_words[NES_IWARP_SQ_WQE_IMM_DATA_START_IDX],
				       (void *)(unsigned long)ib_wr->sg_list[0].addr, ib_wr->sg_list[0].length);
				set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_TOTAL_PAYLOAD_IDX,
						    ib_wr->sg_list[0].length);
				wqe_misc |= NES_IWARP_SQ_WQE_IMM_DATA;
			} else {
				fill_wqe_sg_send(wqe, ib_wr, 1);
			}

			wqe->wqe_words[NES_IWARP_SQ_WQE_RDMA_LENGTH_IDX] =
				wqe->wqe_words[NES_IWARP_SQ_WQE_TOTAL_PAYLOAD_IDX];
			break;
		case IB_WR_RDMA_READ:
		case IB_WR_RDMA_READ_WITH_INV:
			/* iWARP only supports 1 sge for RDMA reads */
			if (ib_wr->num_sge > 1) {
				nes_debug(NES_DBG_IW_TX, "Exceeded max sge, ib_wr=%u, max=1\n",
					  ib_wr->num_sge);
				err = -EINVAL;
				break;
			}
			if (ib_wr->opcode == IB_WR_RDMA_READ) {
				wqe_misc = NES_IWARP_SQ_OP_RDMAR;
			} else {
				wqe_misc = NES_IWARP_SQ_OP_RDMAR_LOCINV;
				set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_INV_STAG_LOW_IDX,
						    ib_wr->ex.invalidate_rkey);
			}

			set_wqe_64bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_RDMA_TO_LOW_IDX,
					    ib_wr->wr.rdma.remote_addr);
			set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_RDMA_STAG_IDX,
					    ib_wr->wr.rdma.rkey);
			set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_RDMA_LENGTH_IDX,
					    ib_wr->sg_list->length);
			set_wqe_64bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_FRAG0_LOW_IDX,
					    ib_wr->sg_list->addr);
			set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_STAG0_IDX,
					    ib_wr->sg_list->lkey);
			break;
		case IB_WR_LOCAL_INV:
			wqe_misc = NES_IWARP_SQ_OP_LOCINV;
			set_wqe_32bit_value(wqe->wqe_words,
					    NES_IWARP_SQ_LOCINV_WQE_INV_STAG_IDX,
					    ib_wr->ex.invalidate_rkey);
			break;
		case IB_WR_FAST_REG_MR:
		{
			int i;
			int flags = ib_wr->wr.fast_reg.access_flags;
			struct nes_ib_fast_reg_page_list *pnesfrpl =
				container_of(ib_wr->wr.fast_reg.page_list,
					     struct nes_ib_fast_reg_page_list,
					     ibfrpl);
			u64 *src_page_list = pnesfrpl->ibfrpl.page_list;
			u64 *dst_page_list = pnesfrpl->nes_wqe_pbl.kva;

			if (ib_wr->wr.fast_reg.page_list_len >
			    (NES_4K_PBL_CHUNK_SIZE / sizeof(u64))) {
				nes_debug(NES_DBG_IW_TX, "SQ_FMR: bad page_list_len\n");
				err = -EINVAL;
				break;
			}
			wqe_misc = NES_IWARP_SQ_OP_FAST_REG;
			set_wqe_64bit_value(wqe->wqe_words,
					    NES_IWARP_SQ_FMR_WQE_VA_FBO_LOW_IDX,
					    ib_wr->wr.fast_reg.iova_start);
			set_wqe_32bit_value(wqe->wqe_words,
					    NES_IWARP_SQ_FMR_WQE_LENGTH_LOW_IDX,
					    ib_wr->wr.fast_reg.length);
			set_wqe_32bit_value(wqe->wqe_words,
					    NES_IWARP_SQ_FMR_WQE_LENGTH_HIGH_IDX, 0);
			set_wqe_32bit_value(wqe->wqe_words,
					    NES_IWARP_SQ_FMR_WQE_MR_STAG_IDX,
					    ib_wr->wr.fast_reg.rkey);
			/* Set page size: */
			if (ib_wr->wr.fast_reg.page_shift == 12) {
				wqe_misc |= NES_IWARP_SQ_FMR_WQE_PAGE_SIZE_4K;
			} else if (ib_wr->wr.fast_reg.page_shift == 21) {
				wqe_misc |= NES_IWARP_SQ_FMR_WQE_PAGE_SIZE_2M;
			} else {
				nes_debug(NES_DBG_IW_TX, "Invalid page shift,"
					  " ib_wr=%u, max=1\n", ib_wr->num_sge);
				err = -EINVAL;
				break;
			}
			/* Set access_flags */
			wqe_misc |= NES_IWARP_SQ_FMR_WQE_RIGHTS_ENABLE_LOCAL_READ;
			if (flags & IB_ACCESS_LOCAL_WRITE)
				wqe_misc |= NES_IWARP_SQ_FMR_WQE_RIGHTS_ENABLE_LOCAL_WRITE;

			if (flags & IB_ACCESS_REMOTE_WRITE)
				wqe_misc |= NES_IWARP_SQ_FMR_WQE_RIGHTS_ENABLE_REMOTE_WRITE;

			if (flags & IB_ACCESS_REMOTE_READ)
				wqe_misc |= NES_IWARP_SQ_FMR_WQE_RIGHTS_ENABLE_REMOTE_READ;

			if (flags & IB_ACCESS_MW_BIND)
				wqe_misc |= NES_IWARP_SQ_FMR_WQE_RIGHTS_ENABLE_WINDOW_BIND;

			/* Fill in PBL info: */
			if (ib_wr->wr.fast_reg.page_list_len >
			    pnesfrpl->ibfrpl.max_page_list_len) {
				nes_debug(NES_DBG_IW_TX, "Invalid page list length,"
					  " ib_wr=%p, value=%u, max=%u\n",
					  ib_wr, ib_wr->wr.fast_reg.page_list_len,
					  pnesfrpl->ibfrpl.max_page_list_len);
				err = -EINVAL;
				break;
			}

			set_wqe_64bit_value(wqe->wqe_words,
					    NES_IWARP_SQ_FMR_WQE_PBL_ADDR_LOW_IDX,
					    pnesfrpl->nes_wqe_pbl.paddr);

			set_wqe_32bit_value(wqe->wqe_words,
					    NES_IWARP_SQ_FMR_WQE_PBL_LENGTH_IDX,
					    ib_wr->wr.fast_reg.page_list_len * 8);

			for (i = 0; i < ib_wr->wr.fast_reg.page_list_len; i++)
				dst_page_list[i] = cpu_to_le64(src_page_list[i]);

			nes_debug(NES_DBG_IW_TX, "SQ_FMR: iova_start: %llx, "
				  "length: %d, rkey: %0x, pgl_paddr: %llx, "
				  "page_list_len: %u, wqe_misc: %x\n",
				  (unsigned long long) ib_wr->wr.fast_reg.iova_start,
				  ib_wr->wr.fast_reg.length,
				  ib_wr->wr.fast_reg.rkey,
				  (unsigned long long) pnesfrpl->nes_wqe_pbl.paddr,
				  ib_wr->wr.fast_reg.page_list_len,
				  wqe_misc);
			break;
		}
		default:
			/* error */
			err = -EINVAL;
			break;
		}

		if (err)
			break;

		if ((ib_wr->send_flags & IB_SEND_SIGNALED) || nesqp->sig_all)
			wqe_misc |= NES_IWARP_SQ_WQE_SIGNALED_COMPL;

		wqe->wqe_words[NES_IWARP_SQ_WQE_MISC_IDX] = cpu_to_le32(wqe_misc);

		ib_wr = ib_wr->next;
		head++;
		wqe_count++;
		if (head >= qsize)
			head = 0;

	}

	nesqp->hwqp.sq_head = head;
	barrier();
	while (wqe_count) {
		counter = min(wqe_count, ((u32)255));
		wqe_count -= counter;
		nes_write32(nesdev->regs + NES_WQE_ALLOC,
				(counter << 24) | 0x00800000 | nesqp->hwqp.qp_id);
	}

	spin_unlock_irqrestore(&nesqp->lock, flags);

out:
	if (err)
		*bad_wr = ib_wr;
	return err;
}


/**
 * nes_post_recv
 */
static int nes_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *ib_wr,
		struct ib_recv_wr **bad_wr)
{
	u64 u64temp;
	unsigned long flags = 0;
	struct nes_vnic *nesvnic = to_nesvnic(ibqp->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_qp *nesqp = to_nesqp(ibqp);
	struct nes_hw_qp_wqe *wqe;
	int err = 0;
	int sge_index;
	u32 qsize = nesqp->hwqp.rq_size;
	u32 head;
	u32 wqe_count = 0;
	u32 counter;
	u32 total_payload_length;

	if (nesqp->ibqp_state > IB_QPS_RTS) {
		err = -EINVAL;
		goto out;
	}

	spin_lock_irqsave(&nesqp->lock, flags);

	head = nesqp->hwqp.rq_head;

	while (ib_wr) {
		/* Check for QP error */
		if (nesqp->term_flags) {
			err = -EINVAL;
			break;
		}

		if (ib_wr->num_sge > nesdev->nesadapter->max_sge) {
			err = -EINVAL;
			break;
		}
		/* Check for RQ overflow */
		if (((head + (2 * qsize) - nesqp->hwqp.rq_tail) % qsize) == (qsize - 1)) {
			err = -ENOMEM;
			break;
		}

		nes_debug(NES_DBG_IW_RX, "ibwr sge count = %u.\n", ib_wr->num_sge);
		wqe = &nesqp->hwqp.rq_vbase[head];

		/* nes_debug(NES_DBG_IW_RX, "QP%u:processing rq wqe at %p, head = %u.\n",
				nesqp->hwqp.qp_id, wqe, head); */
		nes_fill_init_qp_wqe(wqe, nesqp, head);
		u64temp = (u64)(ib_wr->wr_id);
		set_wqe_64bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_COMP_SCRATCH_LOW_IDX,
					u64temp);
		total_payload_length = 0;
		for (sge_index=0; sge_index < ib_wr->num_sge; sge_index++) {
			set_wqe_64bit_value(wqe->wqe_words, NES_IWARP_RQ_WQE_FRAG0_LOW_IDX+(sge_index*4),
					ib_wr->sg_list[sge_index].addr);
			set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_RQ_WQE_LENGTH0_IDX+(sge_index*4),
					ib_wr->sg_list[sge_index].length);
			set_wqe_32bit_value(wqe->wqe_words,NES_IWARP_RQ_WQE_STAG0_IDX+(sge_index*4),
					ib_wr->sg_list[sge_index].lkey);

			total_payload_length += ib_wr->sg_list[sge_index].length;
		}
		set_wqe_32bit_value(wqe->wqe_words, NES_IWARP_RQ_WQE_TOTAL_PAYLOAD_IDX,
					total_payload_length);

		ib_wr = ib_wr->next;
		head++;
		wqe_count++;
		if (head >= qsize)
			head = 0;
	}

	nesqp->hwqp.rq_head = head;
	barrier();
	while (wqe_count) {
		counter = min(wqe_count, ((u32)255));
		wqe_count -= counter;
		nes_write32(nesdev->regs+NES_WQE_ALLOC, (counter<<24) | nesqp->hwqp.qp_id);
	}

	spin_unlock_irqrestore(&nesqp->lock, flags);

out:
	if (err)
		*bad_wr = ib_wr;
	return err;
}


/**
 * nes_poll_cq
 */
static int nes_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *entry)
{
	u64 u64temp;
	u64 wrid;
	unsigned long flags = 0;
	struct nes_vnic *nesvnic = to_nesvnic(ibcq->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_cq *nescq = to_nescq(ibcq);
	struct nes_qp *nesqp;
	struct nes_hw_cqe cqe;
	u32 head;
	u32 wq_tail = 0;
	u32 cq_size;
	u32 cqe_count = 0;
	u32 wqe_index;
	u32 u32temp;
	u32 move_cq_head = 1;
	u32 err_code;

	nes_debug(NES_DBG_CQ, "\n");

	spin_lock_irqsave(&nescq->lock, flags);

	head = nescq->hw_cq.cq_head;
	cq_size = nescq->hw_cq.cq_size;

	while (cqe_count < num_entries) {
		if ((le32_to_cpu(nescq->hw_cq.cq_vbase[head].cqe_words[NES_CQE_OPCODE_IDX]) &
				NES_CQE_VALID) == 0)
			break;

		/*
		 * Make sure we read CQ entry contents *after*
		 * we've checked the valid bit.
		 */
		rmb();

		cqe = nescq->hw_cq.cq_vbase[head];
		u32temp = le32_to_cpu(cqe.cqe_words[NES_CQE_COMP_COMP_CTX_LOW_IDX]);
		wqe_index = u32temp & (nesdev->nesadapter->max_qp_wr - 1);
		u32temp &= ~(NES_SW_CONTEXT_ALIGN-1);
		/* parse CQE, get completion context from WQE (either rq or sq) */
		u64temp = (((u64)(le32_to_cpu(cqe.cqe_words[NES_CQE_COMP_COMP_CTX_HIGH_IDX])))<<32) |
				((u64)u32temp);

		if (u64temp) {
			nesqp = (struct nes_qp *)(unsigned long)u64temp;
			memset(entry, 0, sizeof *entry);
			if (cqe.cqe_words[NES_CQE_ERROR_CODE_IDX] == 0) {
				entry->status = IB_WC_SUCCESS;
			} else {
				err_code = le32_to_cpu(cqe.cqe_words[NES_CQE_ERROR_CODE_IDX]);
				if (NES_IWARP_CQE_MAJOR_DRV == (err_code >> 16)) {
					entry->status = err_code & 0x0000ffff;

					/* The rest of the cqe's will be marked as flushed */
					nescq->hw_cq.cq_vbase[head].cqe_words[NES_CQE_ERROR_CODE_IDX] =
						cpu_to_le32((NES_IWARP_CQE_MAJOR_FLUSH << 16) |
							    NES_IWARP_CQE_MINOR_FLUSH);
				} else
					entry->status = IB_WC_WR_FLUSH_ERR;
			}

			entry->qp = &nesqp->ibqp;
			entry->src_qp = nesqp->hwqp.qp_id;

			if (le32_to_cpu(cqe.cqe_words[NES_CQE_OPCODE_IDX]) & NES_CQE_SQ) {
				if (nesqp->skip_lsmm) {
					nesqp->skip_lsmm = 0;
					nesqp->hwqp.sq_tail++;
				}

				/* Working on a SQ Completion*/
				wrid = (((u64)(cpu_to_le32((u32)nesqp->hwqp.sq_vbase[wqe_index].
						wqe_words[NES_IWARP_SQ_WQE_COMP_SCRATCH_HIGH_IDX]))) << 32) |
						((u64)(cpu_to_le32((u32)nesqp->hwqp.sq_vbase[wqe_index].
						wqe_words[NES_IWARP_SQ_WQE_COMP_SCRATCH_LOW_IDX])));
				entry->byte_len = le32_to_cpu(nesqp->hwqp.sq_vbase[wqe_index].
						wqe_words[NES_IWARP_SQ_WQE_TOTAL_PAYLOAD_IDX]);

				switch (le32_to_cpu(nesqp->hwqp.sq_vbase[wqe_index].
						wqe_words[NES_IWARP_SQ_WQE_MISC_IDX]) & 0x3f) {
					case NES_IWARP_SQ_OP_RDMAW:
						nes_debug(NES_DBG_CQ, "Operation = RDMA WRITE.\n");
						entry->opcode = IB_WC_RDMA_WRITE;
						break;
					case NES_IWARP_SQ_OP_RDMAR:
						nes_debug(NES_DBG_CQ, "Operation = RDMA READ.\n");
						entry->opcode = IB_WC_RDMA_READ;
						entry->byte_len = le32_to_cpu(nesqp->hwqp.sq_vbase[wqe_index].
								wqe_words[NES_IWARP_SQ_WQE_RDMA_LENGTH_IDX]);
						break;
					case NES_IWARP_SQ_OP_SENDINV:
					case NES_IWARP_SQ_OP_SENDSEINV:
					case NES_IWARP_SQ_OP_SEND:
					case NES_IWARP_SQ_OP_SENDSE:
						nes_debug(NES_DBG_CQ, "Operation = Send.\n");
						entry->opcode = IB_WC_SEND;
						break;
					case NES_IWARP_SQ_OP_LOCINV:
						entry->opcode = IB_WC_LOCAL_INV;
						break;
					case NES_IWARP_SQ_OP_FAST_REG:
						entry->opcode = IB_WC_FAST_REG_MR;
						break;
				}

				nesqp->hwqp.sq_tail = (wqe_index+1)&(nesqp->hwqp.sq_size - 1);
				if ((entry->status != IB_WC_SUCCESS) && (nesqp->hwqp.sq_tail != nesqp->hwqp.sq_head)) {
					move_cq_head = 0;
					wq_tail = nesqp->hwqp.sq_tail;
				}
			} else {
				/* Working on a RQ Completion*/
				entry->byte_len = le32_to_cpu(cqe.cqe_words[NES_CQE_PAYLOAD_LENGTH_IDX]);
				wrid = ((u64)(le32_to_cpu(nesqp->hwqp.rq_vbase[wqe_index].wqe_words[NES_IWARP_RQ_WQE_COMP_SCRATCH_LOW_IDX]))) |
					((u64)(le32_to_cpu(nesqp->hwqp.rq_vbase[wqe_index].wqe_words[NES_IWARP_RQ_WQE_COMP_SCRATCH_HIGH_IDX]))<<32);
					entry->opcode = IB_WC_RECV;

				nesqp->hwqp.rq_tail = (wqe_index+1)&(nesqp->hwqp.rq_size - 1);
				if ((entry->status != IB_WC_SUCCESS) && (nesqp->hwqp.rq_tail != nesqp->hwqp.rq_head)) {
					move_cq_head = 0;
					wq_tail = nesqp->hwqp.rq_tail;
				}
			}

			entry->wr_id = wrid;
			entry++;
			cqe_count++;
		}

		if (move_cq_head) {
			nescq->hw_cq.cq_vbase[head].cqe_words[NES_CQE_OPCODE_IDX] = 0;
			if (++head >= cq_size)
				head = 0;
			nescq->polled_completions++;

			if ((nescq->polled_completions > (cq_size / 2)) ||
					(nescq->polled_completions == 255)) {
				nes_debug(NES_DBG_CQ, "CQ%u Issuing CQE Allocate since more than half of cqes"
					" are pending %u of %u.\n",
					nescq->hw_cq.cq_number, nescq->polled_completions, cq_size);
				nes_write32(nesdev->regs+NES_CQE_ALLOC,
					nescq->hw_cq.cq_number | (nescq->polled_completions << 16));
				nescq->polled_completions = 0;
			}
		} else {
			/* Update the wqe index and set status to flush */
			wqe_index = le32_to_cpu(cqe.cqe_words[NES_CQE_COMP_COMP_CTX_LOW_IDX]);
			wqe_index = (wqe_index & (~(nesdev->nesadapter->max_qp_wr - 1))) | wq_tail;
			nescq->hw_cq.cq_vbase[head].cqe_words[NES_CQE_COMP_COMP_CTX_LOW_IDX] =
				cpu_to_le32(wqe_index);
			move_cq_head = 1; /* ready for next pass */
		}
	}

	if (nescq->polled_completions) {
		nes_write32(nesdev->regs+NES_CQE_ALLOC,
				nescq->hw_cq.cq_number | (nescq->polled_completions << 16));
		nescq->polled_completions = 0;
	}

	nescq->hw_cq.cq_head = head;
	nes_debug(NES_DBG_CQ, "Reporting %u completions for CQ%u.\n",
			cqe_count, nescq->hw_cq.cq_number);

	spin_unlock_irqrestore(&nescq->lock, flags);

	return cqe_count;
}


/**
 * nes_req_notify_cq
 */
static int nes_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags notify_flags)
		{
	struct nes_vnic *nesvnic = to_nesvnic(ibcq->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_cq *nescq = to_nescq(ibcq);
	u32 cq_arm;

	nes_debug(NES_DBG_CQ, "Requesting notification for CQ%u.\n",
			nescq->hw_cq.cq_number);

	cq_arm = nescq->hw_cq.cq_number;
	if ((notify_flags & IB_CQ_SOLICITED_MASK) == IB_CQ_NEXT_COMP)
		cq_arm |= NES_CQE_ALLOC_NOTIFY_NEXT;
	else if ((notify_flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED)
		cq_arm |= NES_CQE_ALLOC_NOTIFY_SE;
	else
		return -EINVAL;

	nes_write32(nesdev->regs+NES_CQE_ALLOC, cq_arm);
	nes_read32(nesdev->regs+NES_CQE_ALLOC);

	return 0;
}


/**
 * nes_init_ofa_device
 */
struct nes_ib_device *nes_init_ofa_device(struct net_device *netdev)
{
	struct nes_ib_device *nesibdev;
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;

	nesibdev = (struct nes_ib_device *)ib_alloc_device(sizeof(struct nes_ib_device));
	if (nesibdev == NULL) {
		return NULL;
	}
	strlcpy(nesibdev->ibdev.name, "nes%d", IB_DEVICE_NAME_MAX);
	nesibdev->ibdev.owner = THIS_MODULE;

	nesibdev->ibdev.node_type = RDMA_NODE_RNIC;
	memset(&nesibdev->ibdev.node_guid, 0, sizeof(nesibdev->ibdev.node_guid));
	memcpy(&nesibdev->ibdev.node_guid, netdev->dev_addr, 6);

	nesibdev->ibdev.uverbs_cmd_mask =
			(1ull << IB_USER_VERBS_CMD_GET_CONTEXT) |
			(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE) |
			(1ull << IB_USER_VERBS_CMD_QUERY_PORT) |
			(1ull << IB_USER_VERBS_CMD_ALLOC_PD) |
			(1ull << IB_USER_VERBS_CMD_DEALLOC_PD) |
			(1ull << IB_USER_VERBS_CMD_REG_MR) |
			(1ull << IB_USER_VERBS_CMD_DEREG_MR) |
			(1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL) |
			(1ull << IB_USER_VERBS_CMD_CREATE_CQ) |
			(1ull << IB_USER_VERBS_CMD_DESTROY_CQ) |
			(1ull << IB_USER_VERBS_CMD_CREATE_AH) |
			(1ull << IB_USER_VERBS_CMD_DESTROY_AH) |
			(1ull << IB_USER_VERBS_CMD_REQ_NOTIFY_CQ) |
			(1ull << IB_USER_VERBS_CMD_CREATE_QP) |
			(1ull << IB_USER_VERBS_CMD_MODIFY_QP) |
			(1ull << IB_USER_VERBS_CMD_POLL_CQ) |
			(1ull << IB_USER_VERBS_CMD_DESTROY_QP) |
			(1ull << IB_USER_VERBS_CMD_ALLOC_MW) |
			(1ull << IB_USER_VERBS_CMD_BIND_MW) |
			(1ull << IB_USER_VERBS_CMD_DEALLOC_MW) |
			(1ull << IB_USER_VERBS_CMD_POST_RECV) |
			(1ull << IB_USER_VERBS_CMD_POST_SEND);

	nesibdev->ibdev.phys_port_cnt = 1;
	nesibdev->ibdev.num_comp_vectors = 1;
	nesibdev->ibdev.dma_device = &nesdev->pcidev->dev;
	nesibdev->ibdev.dev.parent = &nesdev->pcidev->dev;
	nesibdev->ibdev.query_device = nes_query_device;
	nesibdev->ibdev.query_port = nes_query_port;
	nesibdev->ibdev.query_pkey = nes_query_pkey;
	nesibdev->ibdev.query_gid = nes_query_gid;
	nesibdev->ibdev.alloc_ucontext = nes_alloc_ucontext;
	nesibdev->ibdev.dealloc_ucontext = nes_dealloc_ucontext;
	nesibdev->ibdev.mmap = nes_mmap;
	nesibdev->ibdev.alloc_pd = nes_alloc_pd;
	nesibdev->ibdev.dealloc_pd = nes_dealloc_pd;
	nesibdev->ibdev.create_ah = nes_create_ah;
	nesibdev->ibdev.destroy_ah = nes_destroy_ah;
	nesibdev->ibdev.create_qp = nes_create_qp;
	nesibdev->ibdev.modify_qp = nes_modify_qp;
	nesibdev->ibdev.query_qp = nes_query_qp;
	nesibdev->ibdev.destroy_qp = nes_destroy_qp;
	nesibdev->ibdev.create_cq = nes_create_cq;
	nesibdev->ibdev.destroy_cq = nes_destroy_cq;
	nesibdev->ibdev.poll_cq = nes_poll_cq;
	nesibdev->ibdev.get_dma_mr = nes_get_dma_mr;
	nesibdev->ibdev.reg_phys_mr = nes_reg_phys_mr;
	nesibdev->ibdev.reg_user_mr = nes_reg_user_mr;
	nesibdev->ibdev.dereg_mr = nes_dereg_mr;
	nesibdev->ibdev.alloc_mw = nes_alloc_mw;
	nesibdev->ibdev.dealloc_mw = nes_dealloc_mw;
	nesibdev->ibdev.bind_mw = nes_bind_mw;

	nesibdev->ibdev.alloc_fast_reg_mr = nes_alloc_fast_reg_mr;
	nesibdev->ibdev.alloc_fast_reg_page_list = nes_alloc_fast_reg_page_list;
	nesibdev->ibdev.free_fast_reg_page_list = nes_free_fast_reg_page_list;

	nesibdev->ibdev.attach_mcast = nes_multicast_attach;
	nesibdev->ibdev.detach_mcast = nes_multicast_detach;
	nesibdev->ibdev.process_mad = nes_process_mad;

	nesibdev->ibdev.req_notify_cq = nes_req_notify_cq;
	nesibdev->ibdev.post_send = nes_post_send;
	nesibdev->ibdev.post_recv = nes_post_recv;

	nesibdev->ibdev.iwcm = kzalloc(sizeof(*nesibdev->ibdev.iwcm), GFP_KERNEL);
	if (nesibdev->ibdev.iwcm == NULL) {
		ib_dealloc_device(&nesibdev->ibdev);
		return NULL;
	}
	nesibdev->ibdev.iwcm->add_ref = nes_add_ref;
	nesibdev->ibdev.iwcm->rem_ref = nes_rem_ref;
	nesibdev->ibdev.iwcm->get_qp = nes_get_qp;
	nesibdev->ibdev.iwcm->connect = nes_connect;
	nesibdev->ibdev.iwcm->accept = nes_accept;
	nesibdev->ibdev.iwcm->reject = nes_reject;
	nesibdev->ibdev.iwcm->create_listen = nes_create_listen;
	nesibdev->ibdev.iwcm->destroy_listen = nes_destroy_listen;

	return nesibdev;
}


/**
 * nes_handle_delayed_event
 */
static void nes_handle_delayed_event(unsigned long data)
{
	struct nes_vnic *nesvnic = (void *) data;

	if (nesvnic->delayed_event != nesvnic->last_dispatched_event) {
		struct ib_event event;

		event.device = &nesvnic->nesibdev->ibdev;
		if (!event.device)
			goto stop_timer;
		event.event = nesvnic->delayed_event;
		event.element.port_num = nesvnic->logical_port + 1;
		ib_dispatch_event(&event);
	}

stop_timer:
	nesvnic->event_timer.function = NULL;
}


void  nes_port_ibevent(struct nes_vnic *nesvnic)
{
	struct nes_ib_device *nesibdev = nesvnic->nesibdev;
	struct nes_device *nesdev = nesvnic->nesdev;
	struct ib_event event;
	event.device = &nesibdev->ibdev;
	event.element.port_num = nesvnic->logical_port + 1;
	event.event = nesdev->iw_status ? IB_EVENT_PORT_ACTIVE : IB_EVENT_PORT_ERR;

	if (!nesvnic->event_timer.function) {
		ib_dispatch_event(&event);
		nesvnic->last_dispatched_event = event.event;
		nesvnic->event_timer.function = nes_handle_delayed_event;
		nesvnic->event_timer.data = (unsigned long) nesvnic;
		nesvnic->event_timer.expires = jiffies + NES_EVENT_DELAY;
		add_timer(&nesvnic->event_timer);
	} else {
		mod_timer(&nesvnic->event_timer, jiffies + NES_EVENT_DELAY);
	}
	nesvnic->delayed_event = event.event;
}


/**
 * nes_destroy_ofa_device
 */
void nes_destroy_ofa_device(struct nes_ib_device *nesibdev)
{
	if (nesibdev == NULL)
		return;

	nes_unregister_ofa_device(nesibdev);

	kfree(nesibdev->ibdev.iwcm);
	ib_dealloc_device(&nesibdev->ibdev);
}


/**
 * nes_register_ofa_device
 */
int nes_register_ofa_device(struct nes_ib_device *nesibdev)
{
	struct nes_vnic *nesvnic = nesibdev->nesvnic;
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	int i, ret;

	ret = ib_register_device(&nesvnic->nesibdev->ibdev, NULL);
	if (ret) {
		return ret;
	}

	/* Get the resources allocated to this device */
	nesibdev->max_cq = (nesadapter->max_cq-NES_FIRST_QPN) / nesadapter->port_count;
	nesibdev->max_mr = nesadapter->max_mr / nesadapter->port_count;
	nesibdev->max_qp = (nesadapter->max_qp-NES_FIRST_QPN) / nesadapter->port_count;
	nesibdev->max_pd = nesadapter->max_pd / nesadapter->port_count;

	for (i = 0; i < ARRAY_SIZE(nes_dev_attributes); ++i) {
		ret = device_create_file(&nesibdev->ibdev.dev, nes_dev_attributes[i]);
		if (ret) {
			while (i > 0) {
				i--;
				device_remove_file(&nesibdev->ibdev.dev,
						   nes_dev_attributes[i]);
			}
			ib_unregister_device(&nesibdev->ibdev);
			return ret;
		}
	}

	nesvnic->of_device_registered = 1;

	return 0;
}


/**
 * nes_unregister_ofa_device
 */
static void nes_unregister_ofa_device(struct nes_ib_device *nesibdev)
{
	struct nes_vnic *nesvnic = nesibdev->nesvnic;
	int i;

	for (i = 0; i < ARRAY_SIZE(nes_dev_attributes); ++i) {
		device_remove_file(&nesibdev->ibdev.dev, nes_dev_attributes[i]);
	}

	if (nesvnic->of_device_registered) {
		ib_unregister_device(&nesibdev->ibdev);
	}

	nesvnic->of_device_registered = 0;
}

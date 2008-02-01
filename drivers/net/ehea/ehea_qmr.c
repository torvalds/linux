/*
 *  linux/drivers/net/ehea/ehea_qmr.c
 *
 *  eHEA ethernet device driver for IBM eServer System p
 *
 *  (C) Copyright IBM Corp. 2006
 *
 *  Authors:
 *       Christoph Raisch <raisch@de.ibm.com>
 *       Jan-Bernd Themann <themann@de.ibm.com>
 *       Thomas Klein <tklein@de.ibm.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/mm.h>
#include "ehea.h"
#include "ehea_phyp.h"
#include "ehea_qmr.h"


struct ehea_busmap ehea_bmap = { 0, 0, NULL };


static void *hw_qpageit_get_inc(struct hw_queue *queue)
{
	void *retvalue = hw_qeit_get(queue);

	queue->current_q_offset += queue->pagesize;
	if (queue->current_q_offset > queue->queue_length) {
		queue->current_q_offset -= queue->pagesize;
		retvalue = NULL;
	} else if (((u64) retvalue) & (EHEA_PAGESIZE-1)) {
		ehea_error("not on pageboundary");
		retvalue = NULL;
	}
	return retvalue;
}

static int hw_queue_ctor(struct hw_queue *queue, const u32 nr_of_pages,
			  const u32 pagesize, const u32 qe_size)
{
	int pages_per_kpage = PAGE_SIZE / pagesize;
	int i, k;

	if ((pagesize > PAGE_SIZE) || (!pages_per_kpage)) {
		ehea_error("pagesize conflict! kernel pagesize=%d, "
			   "ehea pagesize=%d", (int)PAGE_SIZE, (int)pagesize);
		return -EINVAL;
	}

	queue->queue_length = nr_of_pages * pagesize;
	queue->queue_pages = kmalloc(nr_of_pages * sizeof(void *), GFP_KERNEL);
	if (!queue->queue_pages) {
		ehea_error("no mem for queue_pages");
		return -ENOMEM;
	}

	/*
	 * allocate pages for queue:
	 * outer loop allocates whole kernel pages (page aligned) and
	 * inner loop divides a kernel page into smaller hea queue pages
	 */
	i = 0;
	while (i < nr_of_pages) {
		u8 *kpage = (u8 *)get_zeroed_page(GFP_KERNEL);
		if (!kpage)
			goto out_nomem;
		for (k = 0; k < pages_per_kpage && i < nr_of_pages; k++) {
			(queue->queue_pages)[i] = (struct ehea_page *)kpage;
			kpage += pagesize;
			i++;
		}
	}

	queue->current_q_offset = 0;
	queue->qe_size = qe_size;
	queue->pagesize = pagesize;
	queue->toggle_state = 1;

	return 0;
out_nomem:
	for (i = 0; i < nr_of_pages; i += pages_per_kpage) {
		if (!(queue->queue_pages)[i])
			break;
		free_page((unsigned long)(queue->queue_pages)[i]);
	}
	return -ENOMEM;
}

static void hw_queue_dtor(struct hw_queue *queue)
{
	int pages_per_kpage = PAGE_SIZE / queue->pagesize;
	int i, nr_pages;

	if (!queue || !queue->queue_pages)
		return;

	nr_pages = queue->queue_length / queue->pagesize;

	for (i = 0; i < nr_pages; i += pages_per_kpage)
		free_page((unsigned long)(queue->queue_pages)[i]);

	kfree(queue->queue_pages);
}

struct ehea_cq *ehea_create_cq(struct ehea_adapter *adapter,
			       int nr_of_cqe, u64 eq_handle, u32 cq_token)
{
	struct ehea_cq *cq;
	struct h_epa epa;
	u64 *cq_handle_ref, hret, rpage;
	u32 act_nr_of_entries, act_pages, counter;
	int ret;
	void *vpage;

	cq = kzalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq) {
		ehea_error("no mem for cq");
		goto out_nomem;
	}

	cq->attr.max_nr_of_cqes = nr_of_cqe;
	cq->attr.cq_token = cq_token;
	cq->attr.eq_handle = eq_handle;

	cq->adapter = adapter;

	cq_handle_ref = &cq->fw_handle;
	act_nr_of_entries = 0;
	act_pages = 0;

	hret = ehea_h_alloc_resource_cq(adapter->handle, &cq->attr,
					&cq->fw_handle, &cq->epas);
	if (hret != H_SUCCESS) {
		ehea_error("alloc_resource_cq failed");
		goto out_freemem;
	}

	ret = hw_queue_ctor(&cq->hw_queue, cq->attr.nr_pages,
			    EHEA_PAGESIZE, sizeof(struct ehea_cqe));
	if (ret)
		goto out_freeres;

	for (counter = 0; counter < cq->attr.nr_pages; counter++) {
		vpage = hw_qpageit_get_inc(&cq->hw_queue);
		if (!vpage) {
			ehea_error("hw_qpageit_get_inc failed");
			goto out_kill_hwq;
		}

		rpage = virt_to_abs(vpage);
		hret = ehea_h_register_rpage(adapter->handle,
					     0, EHEA_CQ_REGISTER_ORIG,
					     cq->fw_handle, rpage, 1);
		if (hret < H_SUCCESS) {
			ehea_error("register_rpage_cq failed ehea_cq=%p "
				   "hret=%lx counter=%i act_pages=%i",
				   cq, hret, counter, cq->attr.nr_pages);
			goto out_kill_hwq;
		}

		if (counter == (cq->attr.nr_pages - 1)) {
			vpage = hw_qpageit_get_inc(&cq->hw_queue);

			if ((hret != H_SUCCESS) || (vpage)) {
				ehea_error("registration of pages not "
					   "complete hret=%lx\n", hret);
				goto out_kill_hwq;
			}
		} else {
			if ((hret != H_PAGE_REGISTERED) || (!vpage)) {
				ehea_error("CQ: registration of page failed "
					   "hret=%lx\n", hret);
				goto out_kill_hwq;
			}
		}
	}

	hw_qeit_reset(&cq->hw_queue);
	epa = cq->epas.kernel;
	ehea_reset_cq_ep(cq);
	ehea_reset_cq_n1(cq);

	return cq;

out_kill_hwq:
	hw_queue_dtor(&cq->hw_queue);

out_freeres:
	ehea_h_free_resource(adapter->handle, cq->fw_handle, FORCE_FREE);

out_freemem:
	kfree(cq);

out_nomem:
	return NULL;
}

u64 ehea_destroy_cq_res(struct ehea_cq *cq, u64 force)
{
	u64 hret;
	u64 adapter_handle = cq->adapter->handle;

	/* deregister all previous registered pages */
	hret = ehea_h_free_resource(adapter_handle, cq->fw_handle, force);
	if (hret != H_SUCCESS)
		return hret;

	hw_queue_dtor(&cq->hw_queue);
	kfree(cq);

	return hret;
}

int ehea_destroy_cq(struct ehea_cq *cq)
{
	u64 hret;
	if (!cq)
		return 0;

	hcp_epas_dtor(&cq->epas);
	hret = ehea_destroy_cq_res(cq, NORMAL_FREE);
	if (hret == H_R_STATE) {
		ehea_error_data(cq->adapter, cq->fw_handle);
		hret = ehea_destroy_cq_res(cq, FORCE_FREE);
	}

	if (hret != H_SUCCESS) {
		ehea_error("destroy CQ failed");
		return -EIO;
	}

	return 0;
}

struct ehea_eq *ehea_create_eq(struct ehea_adapter *adapter,
			       const enum ehea_eq_type type,
			       const u32 max_nr_of_eqes, const u8 eqe_gen)
{
	int ret, i;
	u64 hret, rpage;
	void *vpage;
	struct ehea_eq *eq;

	eq = kzalloc(sizeof(*eq), GFP_KERNEL);
	if (!eq) {
		ehea_error("no mem for eq");
		return NULL;
	}

	eq->adapter = adapter;
	eq->attr.type = type;
	eq->attr.max_nr_of_eqes = max_nr_of_eqes;
	eq->attr.eqe_gen = eqe_gen;
	spin_lock_init(&eq->spinlock);

	hret = ehea_h_alloc_resource_eq(adapter->handle,
					&eq->attr, &eq->fw_handle);
	if (hret != H_SUCCESS) {
		ehea_error("alloc_resource_eq failed");
		goto out_freemem;
	}

	ret = hw_queue_ctor(&eq->hw_queue, eq->attr.nr_pages,
			    EHEA_PAGESIZE, sizeof(struct ehea_eqe));
	if (ret) {
		ehea_error("can't allocate eq pages");
		goto out_freeres;
	}

	for (i = 0; i < eq->attr.nr_pages; i++) {
		vpage = hw_qpageit_get_inc(&eq->hw_queue);
		if (!vpage) {
			ehea_error("hw_qpageit_get_inc failed");
			hret = H_RESOURCE;
			goto out_kill_hwq;
		}

		rpage = virt_to_abs(vpage);

		hret = ehea_h_register_rpage(adapter->handle, 0,
					     EHEA_EQ_REGISTER_ORIG,
					     eq->fw_handle, rpage, 1);

		if (i == (eq->attr.nr_pages - 1)) {
			/* last page */
			vpage = hw_qpageit_get_inc(&eq->hw_queue);
			if ((hret != H_SUCCESS) || (vpage))
				goto out_kill_hwq;

		} else {
			if ((hret != H_PAGE_REGISTERED) || (!vpage))
				goto out_kill_hwq;

		}
	}

	hw_qeit_reset(&eq->hw_queue);
	return eq;

out_kill_hwq:
	hw_queue_dtor(&eq->hw_queue);

out_freeres:
	ehea_h_free_resource(adapter->handle, eq->fw_handle, FORCE_FREE);

out_freemem:
	kfree(eq);
	return NULL;
}

struct ehea_eqe *ehea_poll_eq(struct ehea_eq *eq)
{
	struct ehea_eqe *eqe;
	unsigned long flags;

	spin_lock_irqsave(&eq->spinlock, flags);
	eqe = (struct ehea_eqe *)hw_eqit_eq_get_inc_valid(&eq->hw_queue);
	spin_unlock_irqrestore(&eq->spinlock, flags);

	return eqe;
}

u64 ehea_destroy_eq_res(struct ehea_eq *eq, u64 force)
{
	u64 hret;
	unsigned long flags;

	spin_lock_irqsave(&eq->spinlock, flags);

	hret = ehea_h_free_resource(eq->adapter->handle, eq->fw_handle, force);
	spin_unlock_irqrestore(&eq->spinlock, flags);

	if (hret != H_SUCCESS)
		return hret;

	hw_queue_dtor(&eq->hw_queue);
	kfree(eq);

	return hret;
}

int ehea_destroy_eq(struct ehea_eq *eq)
{
	u64 hret;
	if (!eq)
		return 0;

	hcp_epas_dtor(&eq->epas);

	hret = ehea_destroy_eq_res(eq, NORMAL_FREE);
	if (hret == H_R_STATE) {
		ehea_error_data(eq->adapter, eq->fw_handle);
		hret = ehea_destroy_eq_res(eq, FORCE_FREE);
	}

	if (hret != H_SUCCESS) {
		ehea_error("destroy EQ failed");
		return -EIO;
	}

	return 0;
}

/**
 * allocates memory for a queue and registers pages in phyp
 */
int ehea_qp_alloc_register(struct ehea_qp *qp, struct hw_queue *hw_queue,
			   int nr_pages, int wqe_size, int act_nr_sges,
			   struct ehea_adapter *adapter, int h_call_q_selector)
{
	u64 hret, rpage;
	int ret, cnt;
	void *vpage;

	ret = hw_queue_ctor(hw_queue, nr_pages, EHEA_PAGESIZE, wqe_size);
	if (ret)
		return ret;

	for (cnt = 0; cnt < nr_pages; cnt++) {
		vpage = hw_qpageit_get_inc(hw_queue);
		if (!vpage) {
			ehea_error("hw_qpageit_get_inc failed");
			goto out_kill_hwq;
		}
		rpage = virt_to_abs(vpage);
		hret = ehea_h_register_rpage(adapter->handle,
					     0, h_call_q_selector,
					     qp->fw_handle, rpage, 1);
		if (hret < H_SUCCESS) {
			ehea_error("register_rpage_qp failed");
			goto out_kill_hwq;
		}
	}
	hw_qeit_reset(hw_queue);
	return 0;

out_kill_hwq:
	hw_queue_dtor(hw_queue);
	return -EIO;
}

static inline u32 map_wqe_size(u8 wqe_enc_size)
{
	return 128 << wqe_enc_size;
}

struct ehea_qp *ehea_create_qp(struct ehea_adapter *adapter,
			       u32 pd, struct ehea_qp_init_attr *init_attr)
{
	int ret;
	u64 hret;
	struct ehea_qp *qp;
	u32 wqe_size_in_bytes_sq, wqe_size_in_bytes_rq1;
	u32 wqe_size_in_bytes_rq2, wqe_size_in_bytes_rq3;


	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp) {
		ehea_error("no mem for qp");
		return NULL;
	}

	qp->adapter = adapter;

	hret = ehea_h_alloc_resource_qp(adapter->handle, init_attr, pd,
					&qp->fw_handle, &qp->epas);
	if (hret != H_SUCCESS) {
		ehea_error("ehea_h_alloc_resource_qp failed");
		goto out_freemem;
	}

	wqe_size_in_bytes_sq = map_wqe_size(init_attr->act_wqe_size_enc_sq);
	wqe_size_in_bytes_rq1 = map_wqe_size(init_attr->act_wqe_size_enc_rq1);
	wqe_size_in_bytes_rq2 = map_wqe_size(init_attr->act_wqe_size_enc_rq2);
	wqe_size_in_bytes_rq3 = map_wqe_size(init_attr->act_wqe_size_enc_rq3);

	ret = ehea_qp_alloc_register(qp, &qp->hw_squeue, init_attr->nr_sq_pages,
				     wqe_size_in_bytes_sq,
				     init_attr->act_wqe_size_enc_sq, adapter,
				     0);
	if (ret) {
		ehea_error("can't register for sq ret=%x", ret);
		goto out_freeres;
	}

	ret = ehea_qp_alloc_register(qp, &qp->hw_rqueue1,
				     init_attr->nr_rq1_pages,
				     wqe_size_in_bytes_rq1,
				     init_attr->act_wqe_size_enc_rq1,
				     adapter, 1);
	if (ret) {
		ehea_error("can't register for rq1 ret=%x", ret);
		goto out_kill_hwsq;
	}

	if (init_attr->rq_count > 1) {
		ret = ehea_qp_alloc_register(qp, &qp->hw_rqueue2,
					     init_attr->nr_rq2_pages,
					     wqe_size_in_bytes_rq2,
					     init_attr->act_wqe_size_enc_rq2,
					     adapter, 2);
		if (ret) {
			ehea_error("can't register for rq2 ret=%x", ret);
			goto out_kill_hwr1q;
		}
	}

	if (init_attr->rq_count > 2) {
		ret = ehea_qp_alloc_register(qp, &qp->hw_rqueue3,
					     init_attr->nr_rq3_pages,
					     wqe_size_in_bytes_rq3,
					     init_attr->act_wqe_size_enc_rq3,
					     adapter, 3);
		if (ret) {
			ehea_error("can't register for rq3 ret=%x", ret);
			goto out_kill_hwr2q;
		}
	}

	qp->init_attr = *init_attr;

	return qp;

out_kill_hwr2q:
	hw_queue_dtor(&qp->hw_rqueue2);

out_kill_hwr1q:
	hw_queue_dtor(&qp->hw_rqueue1);

out_kill_hwsq:
	hw_queue_dtor(&qp->hw_squeue);

out_freeres:
	ehea_h_disable_and_get_hea(adapter->handle, qp->fw_handle);
	ehea_h_free_resource(adapter->handle, qp->fw_handle, FORCE_FREE);

out_freemem:
	kfree(qp);
	return NULL;
}

u64 ehea_destroy_qp_res(struct ehea_qp *qp, u64 force)
{
	u64 hret;
	struct ehea_qp_init_attr *qp_attr = &qp->init_attr;


	ehea_h_disable_and_get_hea(qp->adapter->handle, qp->fw_handle);
	hret = ehea_h_free_resource(qp->adapter->handle, qp->fw_handle, force);
	if (hret != H_SUCCESS)
		return hret;

	hw_queue_dtor(&qp->hw_squeue);
	hw_queue_dtor(&qp->hw_rqueue1);

	if (qp_attr->rq_count > 1)
		hw_queue_dtor(&qp->hw_rqueue2);
	if (qp_attr->rq_count > 2)
		hw_queue_dtor(&qp->hw_rqueue3);
	kfree(qp);

	return hret;
}

int ehea_destroy_qp(struct ehea_qp *qp)
{
	u64 hret;
	if (!qp)
		return 0;

	hcp_epas_dtor(&qp->epas);

	hret = ehea_destroy_qp_res(qp, NORMAL_FREE);
	if (hret == H_R_STATE) {
		ehea_error_data(qp->adapter, qp->fw_handle);
		hret = ehea_destroy_qp_res(qp, FORCE_FREE);
	}

	if (hret != H_SUCCESS) {
		ehea_error("destroy QP failed");
		return -EIO;
	}

	return 0;
}

int ehea_create_busmap(void)
{
	u64 vaddr = EHEA_BUSMAP_START;
	unsigned long high_section_index = 0;
	int i;

	/*
	 * Sections are not in ascending order -> Loop over all sections and
	 * find the highest PFN to compute the required map size.
	*/
	ehea_bmap.valid_sections = 0;

	for (i = 0; i < NR_MEM_SECTIONS; i++)
		if (valid_section_nr(i))
			high_section_index = i;

	ehea_bmap.entries = high_section_index + 1;
	ehea_bmap.vaddr = vmalloc(ehea_bmap.entries * sizeof(*ehea_bmap.vaddr));

	if (!ehea_bmap.vaddr)
		return -ENOMEM;

	for (i = 0 ; i < ehea_bmap.entries; i++) {
		unsigned long pfn = section_nr_to_pfn(i);

		if (pfn_valid(pfn)) {
			ehea_bmap.vaddr[i] = vaddr;
			vaddr += EHEA_SECTSIZE;
			ehea_bmap.valid_sections++;
		} else
			ehea_bmap.vaddr[i] = 0;
	}

	return 0;
}

void ehea_destroy_busmap(void)
{
	vfree(ehea_bmap.vaddr);
}

u64 ehea_map_vaddr(void *caddr)
{
	u64 mapped_addr;
	unsigned long index = __pa(caddr) >> SECTION_SIZE_BITS;

	if (likely(index < ehea_bmap.entries)) {
		mapped_addr = ehea_bmap.vaddr[index];
		if (likely(mapped_addr))
			mapped_addr |= (((unsigned long)caddr)
					& (EHEA_SECTSIZE - 1));
		else
			mapped_addr = -1;
	} else
		mapped_addr = -1;

	if (unlikely(mapped_addr == -1))
		if (!test_and_set_bit(__EHEA_STOP_XFER, &ehea_driver_flags))
			schedule_work(&ehea_rereg_mr_task);

	return mapped_addr;
}

int ehea_reg_kernel_mr(struct ehea_adapter *adapter, struct ehea_mr *mr)
{
	int ret;
	u64 *pt;
	void *pg;
	u64 hret, pt_abs, i, j, m, mr_len;
	u32 acc_ctrl = EHEA_MR_ACC_CTRL;

	mr_len = ehea_bmap.valid_sections * EHEA_SECTSIZE;

	pt =  kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!pt) {
		ehea_error("no mem");
		ret = -ENOMEM;
		goto out;
	}
	pt_abs = virt_to_abs(pt);

	hret = ehea_h_alloc_resource_mr(adapter->handle,
					EHEA_BUSMAP_START, mr_len,
					acc_ctrl, adapter->pd,
					&mr->handle, &mr->lkey);
	if (hret != H_SUCCESS) {
		ehea_error("alloc_resource_mr failed");
		ret = -EIO;
		goto out;
	}

	for (i = 0 ; i < ehea_bmap.entries; i++)
		if (ehea_bmap.vaddr[i]) {
			void *sectbase = __va(i << SECTION_SIZE_BITS);
			unsigned long k = 0;

			for (j = 0; j < (EHEA_PAGES_PER_SECTION /
					 EHEA_MAX_RPAGE); j++) {

				for (m = 0; m < EHEA_MAX_RPAGE; m++) {
					pg = sectbase + ((k++) * EHEA_PAGESIZE);
					pt[m] = virt_to_abs(pg);
				}

				hret = ehea_h_register_rpage_mr(adapter->handle,
								mr->handle,
								0, 0, pt_abs,
								EHEA_MAX_RPAGE);
				if ((hret != H_SUCCESS)
				    && (hret != H_PAGE_REGISTERED)) {
					ehea_h_free_resource(adapter->handle,
							     mr->handle,
							     FORCE_FREE);
					ehea_error("register_rpage_mr failed");
					ret = -EIO;
					goto out;
				}
			}
		}

	if (hret != H_SUCCESS) {
		ehea_h_free_resource(adapter->handle, mr->handle, FORCE_FREE);
		ehea_error("registering mr failed");
		ret = -EIO;
		goto out;
	}

	mr->vaddr = EHEA_BUSMAP_START;
	mr->adapter = adapter;
	ret = 0;
out:
	kfree(pt);
	return ret;
}

int ehea_rem_mr(struct ehea_mr *mr)
{
	u64 hret;

	if (!mr || !mr->adapter)
		return -EINVAL;

	hret = ehea_h_free_resource(mr->adapter->handle, mr->handle,
				    FORCE_FREE);
	if (hret != H_SUCCESS) {
		ehea_error("destroy MR failed");
		return -EIO;
	}

	return 0;
}

int ehea_gen_smr(struct ehea_adapter *adapter, struct ehea_mr *old_mr,
		 struct ehea_mr *shared_mr)
{
	u64 hret;

	hret = ehea_h_register_smr(adapter->handle, old_mr->handle,
				   old_mr->vaddr, EHEA_MR_ACC_CTRL,
				   adapter->pd, shared_mr);
	if (hret != H_SUCCESS)
		return -EIO;

	shared_mr->adapter = adapter;

	return 0;
}

void print_error_data(u64 *data)
{
	int length;
	u64 type = EHEA_BMASK_GET(ERROR_DATA_TYPE, data[2]);
	u64 resource = data[1];

	length = EHEA_BMASK_GET(ERROR_DATA_LENGTH, data[0]);

	if (length > EHEA_PAGESIZE)
		length = EHEA_PAGESIZE;

	if (type == 0x8) /* Queue Pair */
		ehea_error("QP (resource=%lX) state: AER=0x%lX, AERR=0x%lX, "
			   "port=%lX", resource, data[6], data[12], data[22]);

	if (type == 0x4) /* Completion Queue */
		ehea_error("CQ (resource=%lX) state: AER=0x%lX", resource,
			   data[6]);

	if (type == 0x3) /* Event Queue */
		ehea_error("EQ (resource=%lX) state: AER=0x%lX", resource,
			   data[6]);

	ehea_dump(data, length, "error data");
}

void ehea_error_data(struct ehea_adapter *adapter, u64 res_handle)
{
	unsigned long ret;
	u64 *rblock;

	rblock = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!rblock) {
		ehea_error("Cannot allocate rblock memory.");
		return;
	}

	ret = ehea_h_error_data(adapter->handle,
				res_handle,
				rblock);

	if (ret == H_R_STATE)
		ehea_error("No error data is available: %lX.", res_handle);
	else if (ret == H_SUCCESS)
		print_error_data(rblock);
	else
		ehea_error("Error data could not be fetched: %lX", res_handle);

	kfree(rblock);
}

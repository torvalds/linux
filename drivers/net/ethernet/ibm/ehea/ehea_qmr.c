/*
 *  linux/drivers/net/ethernet/ibm/ehea/ehea_qmr.c
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/slab.h>
#include "ehea.h"
#include "ehea_phyp.h"
#include "ehea_qmr.h"

static struct ehea_bmap *ehea_bmap;

static void *hw_qpageit_get_inc(struct hw_queue *queue)
{
	void *retvalue = hw_qeit_get(queue);

	queue->current_q_offset += queue->pagesize;
	if (queue->current_q_offset > queue->queue_length) {
		queue->current_q_offset -= queue->pagesize;
		retvalue = NULL;
	} else if (((u64) retvalue) & (EHEA_PAGESIZE-1)) {
		pr_err("not on pageboundary\n");
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
		pr_err("pagesize conflict! kernel pagesize=%d, ehea pagesize=%d\n",
		       (int)PAGE_SIZE, (int)pagesize);
		return -EINVAL;
	}

	queue->queue_length = nr_of_pages * pagesize;
	queue->queue_pages = kmalloc(nr_of_pages * sizeof(void *), GFP_KERNEL);
	if (!queue->queue_pages) {
		pr_err("no mem for queue_pages\n");
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
		pr_err("no mem for cq\n");
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
		pr_err("alloc_resource_cq failed\n");
		goto out_freemem;
	}

	ret = hw_queue_ctor(&cq->hw_queue, cq->attr.nr_pages,
			    EHEA_PAGESIZE, sizeof(struct ehea_cqe));
	if (ret)
		goto out_freeres;

	for (counter = 0; counter < cq->attr.nr_pages; counter++) {
		vpage = hw_qpageit_get_inc(&cq->hw_queue);
		if (!vpage) {
			pr_err("hw_qpageit_get_inc failed\n");
			goto out_kill_hwq;
		}

		rpage = __pa(vpage);
		hret = ehea_h_register_rpage(adapter->handle,
					     0, EHEA_CQ_REGISTER_ORIG,
					     cq->fw_handle, rpage, 1);
		if (hret < H_SUCCESS) {
			pr_err("register_rpage_cq failed ehea_cq=%p hret=%llx counter=%i act_pages=%i\n",
			       cq, hret, counter, cq->attr.nr_pages);
			goto out_kill_hwq;
		}

		if (counter == (cq->attr.nr_pages - 1)) {
			vpage = hw_qpageit_get_inc(&cq->hw_queue);

			if ((hret != H_SUCCESS) || (vpage)) {
				pr_err("registration of pages not complete hret=%llx\n",
				       hret);
				goto out_kill_hwq;
			}
		} else {
			if (hret != H_PAGE_REGISTERED) {
				pr_err("CQ: registration of page failed hret=%llx\n",
				       hret);
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

static u64 ehea_destroy_cq_res(struct ehea_cq *cq, u64 force)
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
	u64 hret, aer, aerr;
	if (!cq)
		return 0;

	hcp_epas_dtor(&cq->epas);
	hret = ehea_destroy_cq_res(cq, NORMAL_FREE);
	if (hret == H_R_STATE) {
		ehea_error_data(cq->adapter, cq->fw_handle, &aer, &aerr);
		hret = ehea_destroy_cq_res(cq, FORCE_FREE);
	}

	if (hret != H_SUCCESS) {
		pr_err("destroy CQ failed\n");
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
		pr_err("no mem for eq\n");
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
		pr_err("alloc_resource_eq failed\n");
		goto out_freemem;
	}

	ret = hw_queue_ctor(&eq->hw_queue, eq->attr.nr_pages,
			    EHEA_PAGESIZE, sizeof(struct ehea_eqe));
	if (ret) {
		pr_err("can't allocate eq pages\n");
		goto out_freeres;
	}

	for (i = 0; i < eq->attr.nr_pages; i++) {
		vpage = hw_qpageit_get_inc(&eq->hw_queue);
		if (!vpage) {
			pr_err("hw_qpageit_get_inc failed\n");
			hret = H_RESOURCE;
			goto out_kill_hwq;
		}

		rpage = __pa(vpage);

		hret = ehea_h_register_rpage(adapter->handle, 0,
					     EHEA_EQ_REGISTER_ORIG,
					     eq->fw_handle, rpage, 1);

		if (i == (eq->attr.nr_pages - 1)) {
			/* last page */
			vpage = hw_qpageit_get_inc(&eq->hw_queue);
			if ((hret != H_SUCCESS) || (vpage))
				goto out_kill_hwq;

		} else {
			if (hret != H_PAGE_REGISTERED)
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
	eqe = hw_eqit_eq_get_inc_valid(&eq->hw_queue);
	spin_unlock_irqrestore(&eq->spinlock, flags);

	return eqe;
}

static u64 ehea_destroy_eq_res(struct ehea_eq *eq, u64 force)
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
	u64 hret, aer, aerr;
	if (!eq)
		return 0;

	hcp_epas_dtor(&eq->epas);

	hret = ehea_destroy_eq_res(eq, NORMAL_FREE);
	if (hret == H_R_STATE) {
		ehea_error_data(eq->adapter, eq->fw_handle, &aer, &aerr);
		hret = ehea_destroy_eq_res(eq, FORCE_FREE);
	}

	if (hret != H_SUCCESS) {
		pr_err("destroy EQ failed\n");
		return -EIO;
	}

	return 0;
}

/* allocates memory for a queue and registers pages in phyp */
static int ehea_qp_alloc_register(struct ehea_qp *qp, struct hw_queue *hw_queue,
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
			pr_err("hw_qpageit_get_inc failed\n");
			goto out_kill_hwq;
		}
		rpage = __pa(vpage);
		hret = ehea_h_register_rpage(adapter->handle,
					     0, h_call_q_selector,
					     qp->fw_handle, rpage, 1);
		if (hret < H_SUCCESS) {
			pr_err("register_rpage_qp failed\n");
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
		pr_err("no mem for qp\n");
		return NULL;
	}

	qp->adapter = adapter;

	hret = ehea_h_alloc_resource_qp(adapter->handle, init_attr, pd,
					&qp->fw_handle, &qp->epas);
	if (hret != H_SUCCESS) {
		pr_err("ehea_h_alloc_resource_qp failed\n");
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
		pr_err("can't register for sq ret=%x\n", ret);
		goto out_freeres;
	}

	ret = ehea_qp_alloc_register(qp, &qp->hw_rqueue1,
				     init_attr->nr_rq1_pages,
				     wqe_size_in_bytes_rq1,
				     init_attr->act_wqe_size_enc_rq1,
				     adapter, 1);
	if (ret) {
		pr_err("can't register for rq1 ret=%x\n", ret);
		goto out_kill_hwsq;
	}

	if (init_attr->rq_count > 1) {
		ret = ehea_qp_alloc_register(qp, &qp->hw_rqueue2,
					     init_attr->nr_rq2_pages,
					     wqe_size_in_bytes_rq2,
					     init_attr->act_wqe_size_enc_rq2,
					     adapter, 2);
		if (ret) {
			pr_err("can't register for rq2 ret=%x\n", ret);
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
			pr_err("can't register for rq3 ret=%x\n", ret);
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

static u64 ehea_destroy_qp_res(struct ehea_qp *qp, u64 force)
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
	u64 hret, aer, aerr;
	if (!qp)
		return 0;

	hcp_epas_dtor(&qp->epas);

	hret = ehea_destroy_qp_res(qp, NORMAL_FREE);
	if (hret == H_R_STATE) {
		ehea_error_data(qp->adapter, qp->fw_handle, &aer, &aerr);
		hret = ehea_destroy_qp_res(qp, FORCE_FREE);
	}

	if (hret != H_SUCCESS) {
		pr_err("destroy QP failed\n");
		return -EIO;
	}

	return 0;
}

static inline int ehea_calc_index(unsigned long i, unsigned long s)
{
	return (i >> s) & EHEA_INDEX_MASK;
}

static inline int ehea_init_top_bmap(struct ehea_top_bmap *ehea_top_bmap,
				     int dir)
{
	if (!ehea_top_bmap->dir[dir]) {
		ehea_top_bmap->dir[dir] =
			kzalloc(sizeof(struct ehea_dir_bmap), GFP_KERNEL);
		if (!ehea_top_bmap->dir[dir])
			return -ENOMEM;
	}
	return 0;
}

static inline int ehea_init_bmap(struct ehea_bmap *ehea_bmap, int top, int dir)
{
	if (!ehea_bmap->top[top]) {
		ehea_bmap->top[top] =
			kzalloc(sizeof(struct ehea_top_bmap), GFP_KERNEL);
		if (!ehea_bmap->top[top])
			return -ENOMEM;
	}
	return ehea_init_top_bmap(ehea_bmap->top[top], dir);
}

static DEFINE_MUTEX(ehea_busmap_mutex);
static unsigned long ehea_mr_len;

#define EHEA_BUSMAP_ADD_SECT 1
#define EHEA_BUSMAP_REM_SECT 0

static void ehea_rebuild_busmap(void)
{
	u64 vaddr = EHEA_BUSMAP_START;
	int top, dir, idx;

	for (top = 0; top < EHEA_MAP_ENTRIES; top++) {
		struct ehea_top_bmap *ehea_top;
		int valid_dir_entries = 0;

		if (!ehea_bmap->top[top])
			continue;
		ehea_top = ehea_bmap->top[top];
		for (dir = 0; dir < EHEA_MAP_ENTRIES; dir++) {
			struct ehea_dir_bmap *ehea_dir;
			int valid_entries = 0;

			if (!ehea_top->dir[dir])
				continue;
			valid_dir_entries++;
			ehea_dir = ehea_top->dir[dir];
			for (idx = 0; idx < EHEA_MAP_ENTRIES; idx++) {
				if (!ehea_dir->ent[idx])
					continue;
				valid_entries++;
				ehea_dir->ent[idx] = vaddr;
				vaddr += EHEA_SECTSIZE;
			}
			if (!valid_entries) {
				ehea_top->dir[dir] = NULL;
				kfree(ehea_dir);
			}
		}
		if (!valid_dir_entries) {
			ehea_bmap->top[top] = NULL;
			kfree(ehea_top);
		}
	}
}

static int ehea_update_busmap(unsigned long pfn, unsigned long nr_pages, int add)
{
	unsigned long i, start_section, end_section;

	if (!nr_pages)
		return 0;

	if (!ehea_bmap) {
		ehea_bmap = kzalloc(sizeof(struct ehea_bmap), GFP_KERNEL);
		if (!ehea_bmap)
			return -ENOMEM;
	}

	start_section = (pfn * PAGE_SIZE) / EHEA_SECTSIZE;
	end_section = start_section + ((nr_pages * PAGE_SIZE) / EHEA_SECTSIZE);
	/* Mark entries as valid or invalid only; address is assigned later */
	for (i = start_section; i < end_section; i++) {
		u64 flag;
		int top = ehea_calc_index(i, EHEA_TOP_INDEX_SHIFT);
		int dir = ehea_calc_index(i, EHEA_DIR_INDEX_SHIFT);
		int idx = i & EHEA_INDEX_MASK;

		if (add) {
			int ret = ehea_init_bmap(ehea_bmap, top, dir);
			if (ret)
				return ret;
			flag = 1; /* valid */
			ehea_mr_len += EHEA_SECTSIZE;
		} else {
			if (!ehea_bmap->top[top])
				continue;
			if (!ehea_bmap->top[top]->dir[dir])
				continue;
			flag = 0; /* invalid */
			ehea_mr_len -= EHEA_SECTSIZE;
		}

		ehea_bmap->top[top]->dir[dir]->ent[idx] = flag;
	}
	ehea_rebuild_busmap(); /* Assign contiguous addresses for mr */
	return 0;
}

int ehea_add_sect_bmap(unsigned long pfn, unsigned long nr_pages)
{
	int ret;

	mutex_lock(&ehea_busmap_mutex);
	ret = ehea_update_busmap(pfn, nr_pages, EHEA_BUSMAP_ADD_SECT);
	mutex_unlock(&ehea_busmap_mutex);
	return ret;
}

int ehea_rem_sect_bmap(unsigned long pfn, unsigned long nr_pages)
{
	int ret;

	mutex_lock(&ehea_busmap_mutex);
	ret = ehea_update_busmap(pfn, nr_pages, EHEA_BUSMAP_REM_SECT);
	mutex_unlock(&ehea_busmap_mutex);
	return ret;
}

static int ehea_is_hugepage(unsigned long pfn)
{
	int page_order;

	if (pfn & EHEA_HUGEPAGE_PFN_MASK)
		return 0;

	page_order = compound_order(pfn_to_page(pfn));
	if (page_order + PAGE_SHIFT != EHEA_HUGEPAGESHIFT)
		return 0;

	return 1;
}

static int ehea_create_busmap_callback(unsigned long initial_pfn,
				       unsigned long total_nr_pages, void *arg)
{
	int ret;
	unsigned long pfn, start_pfn, end_pfn, nr_pages;

	if ((total_nr_pages * PAGE_SIZE) < EHEA_HUGEPAGE_SIZE)
		return ehea_update_busmap(initial_pfn, total_nr_pages,
					  EHEA_BUSMAP_ADD_SECT);

	/* Given chunk is >= 16GB -> check for hugepages */
	start_pfn = initial_pfn;
	end_pfn = initial_pfn + total_nr_pages;
	pfn = start_pfn;

	while (pfn < end_pfn) {
		if (ehea_is_hugepage(pfn)) {
			/* Add mem found in front of the hugepage */
			nr_pages = pfn - start_pfn;
			ret = ehea_update_busmap(start_pfn, nr_pages,
						 EHEA_BUSMAP_ADD_SECT);
			if (ret)
				return ret;

			/* Skip the hugepage */
			pfn += (EHEA_HUGEPAGE_SIZE / PAGE_SIZE);
			start_pfn = pfn;
		} else
			pfn += (EHEA_SECTSIZE / PAGE_SIZE);
	}

	/* Add mem found behind the hugepage(s)  */
	nr_pages = pfn - start_pfn;
	return ehea_update_busmap(start_pfn, nr_pages, EHEA_BUSMAP_ADD_SECT);
}

int ehea_create_busmap(void)
{
	int ret;

	mutex_lock(&ehea_busmap_mutex);
	ehea_mr_len = 0;
	ret = walk_system_ram_range(0, 1ULL << MAX_PHYSMEM_BITS, NULL,
				   ehea_create_busmap_callback);
	mutex_unlock(&ehea_busmap_mutex);
	return ret;
}

void ehea_destroy_busmap(void)
{
	int top, dir;
	mutex_lock(&ehea_busmap_mutex);
	if (!ehea_bmap)
		goto out_destroy;

	for (top = 0; top < EHEA_MAP_ENTRIES; top++) {
		if (!ehea_bmap->top[top])
			continue;

		for (dir = 0; dir < EHEA_MAP_ENTRIES; dir++) {
			if (!ehea_bmap->top[top]->dir[dir])
				continue;

			kfree(ehea_bmap->top[top]->dir[dir]);
		}

		kfree(ehea_bmap->top[top]);
	}

	kfree(ehea_bmap);
	ehea_bmap = NULL;
out_destroy:
	mutex_unlock(&ehea_busmap_mutex);
}

u64 ehea_map_vaddr(void *caddr)
{
	int top, dir, idx;
	unsigned long index, offset;

	if (!ehea_bmap)
		return EHEA_INVAL_ADDR;

	index = __pa(caddr) >> SECTION_SIZE_BITS;
	top = (index >> EHEA_TOP_INDEX_SHIFT) & EHEA_INDEX_MASK;
	if (!ehea_bmap->top[top])
		return EHEA_INVAL_ADDR;

	dir = (index >> EHEA_DIR_INDEX_SHIFT) & EHEA_INDEX_MASK;
	if (!ehea_bmap->top[top]->dir[dir])
		return EHEA_INVAL_ADDR;

	idx = index & EHEA_INDEX_MASK;
	if (!ehea_bmap->top[top]->dir[dir]->ent[idx])
		return EHEA_INVAL_ADDR;

	offset = (unsigned long)caddr & (EHEA_SECTSIZE - 1);
	return ehea_bmap->top[top]->dir[dir]->ent[idx] | offset;
}

static inline void *ehea_calc_sectbase(int top, int dir, int idx)
{
	unsigned long ret = idx;
	ret |= dir << EHEA_DIR_INDEX_SHIFT;
	ret |= top << EHEA_TOP_INDEX_SHIFT;
	return __va(ret << SECTION_SIZE_BITS);
}

static u64 ehea_reg_mr_section(int top, int dir, int idx, u64 *pt,
			       struct ehea_adapter *adapter,
			       struct ehea_mr *mr)
{
	void *pg;
	u64 j, m, hret;
	unsigned long k = 0;
	u64 pt_abs = __pa(pt);

	void *sectbase = ehea_calc_sectbase(top, dir, idx);

	for (j = 0; j < (EHEA_PAGES_PER_SECTION / EHEA_MAX_RPAGE); j++) {

		for (m = 0; m < EHEA_MAX_RPAGE; m++) {
			pg = sectbase + ((k++) * EHEA_PAGESIZE);
			pt[m] = __pa(pg);
		}
		hret = ehea_h_register_rpage_mr(adapter->handle, mr->handle, 0,
						0, pt_abs, EHEA_MAX_RPAGE);

		if ((hret != H_SUCCESS) &&
		    (hret != H_PAGE_REGISTERED)) {
			ehea_h_free_resource(adapter->handle, mr->handle,
					     FORCE_FREE);
			pr_err("register_rpage_mr failed\n");
			return hret;
		}
	}
	return hret;
}

static u64 ehea_reg_mr_sections(int top, int dir, u64 *pt,
				struct ehea_adapter *adapter,
				struct ehea_mr *mr)
{
	u64 hret = H_SUCCESS;
	int idx;

	for (idx = 0; idx < EHEA_MAP_ENTRIES; idx++) {
		if (!ehea_bmap->top[top]->dir[dir]->ent[idx])
			continue;

		hret = ehea_reg_mr_section(top, dir, idx, pt, adapter, mr);
		if ((hret != H_SUCCESS) && (hret != H_PAGE_REGISTERED))
			return hret;
	}
	return hret;
}

static u64 ehea_reg_mr_dir_sections(int top, u64 *pt,
				    struct ehea_adapter *adapter,
				    struct ehea_mr *mr)
{
	u64 hret = H_SUCCESS;
	int dir;

	for (dir = 0; dir < EHEA_MAP_ENTRIES; dir++) {
		if (!ehea_bmap->top[top]->dir[dir])
			continue;

		hret = ehea_reg_mr_sections(top, dir, pt, adapter, mr);
		if ((hret != H_SUCCESS) && (hret != H_PAGE_REGISTERED))
			return hret;
	}
	return hret;
}

int ehea_reg_kernel_mr(struct ehea_adapter *adapter, struct ehea_mr *mr)
{
	int ret;
	u64 *pt;
	u64 hret;
	u32 acc_ctrl = EHEA_MR_ACC_CTRL;

	unsigned long top;

	pt = (void *)get_zeroed_page(GFP_KERNEL);
	if (!pt) {
		pr_err("no mem\n");
		ret = -ENOMEM;
		goto out;
	}

	hret = ehea_h_alloc_resource_mr(adapter->handle, EHEA_BUSMAP_START,
					ehea_mr_len, acc_ctrl, adapter->pd,
					&mr->handle, &mr->lkey);

	if (hret != H_SUCCESS) {
		pr_err("alloc_resource_mr failed\n");
		ret = -EIO;
		goto out;
	}

	if (!ehea_bmap) {
		ehea_h_free_resource(adapter->handle, mr->handle, FORCE_FREE);
		pr_err("no busmap available\n");
		ret = -EIO;
		goto out;
	}

	for (top = 0; top < EHEA_MAP_ENTRIES; top++) {
		if (!ehea_bmap->top[top])
			continue;

		hret = ehea_reg_mr_dir_sections(top, pt, adapter, mr);
		if((hret != H_PAGE_REGISTERED) && (hret != H_SUCCESS))
			break;
	}

	if (hret != H_SUCCESS) {
		ehea_h_free_resource(adapter->handle, mr->handle, FORCE_FREE);
		pr_err("registering mr failed\n");
		ret = -EIO;
		goto out;
	}

	mr->vaddr = EHEA_BUSMAP_START;
	mr->adapter = adapter;
	ret = 0;
out:
	free_page((unsigned long)pt);
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
		pr_err("destroy MR failed\n");
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

static void print_error_data(u64 *data)
{
	int length;
	u64 type = EHEA_BMASK_GET(ERROR_DATA_TYPE, data[2]);
	u64 resource = data[1];

	length = EHEA_BMASK_GET(ERROR_DATA_LENGTH, data[0]);

	if (length > EHEA_PAGESIZE)
		length = EHEA_PAGESIZE;

	if (type == EHEA_AER_RESTYPE_QP)
		pr_err("QP (resource=%llX) state: AER=0x%llX, AERR=0x%llX, port=%llX\n",
		       resource, data[6], data[12], data[22]);
	else if (type == EHEA_AER_RESTYPE_CQ)
		pr_err("CQ (resource=%llX) state: AER=0x%llX\n",
		       resource, data[6]);
	else if (type == EHEA_AER_RESTYPE_EQ)
		pr_err("EQ (resource=%llX) state: AER=0x%llX\n",
		       resource, data[6]);

	ehea_dump(data, length, "error data");
}

u64 ehea_error_data(struct ehea_adapter *adapter, u64 res_handle,
		    u64 *aer, u64 *aerr)
{
	unsigned long ret;
	u64 *rblock;
	u64 type = 0;

	rblock = (void *)get_zeroed_page(GFP_KERNEL);
	if (!rblock) {
		pr_err("Cannot allocate rblock memory\n");
		goto out;
	}

	ret = ehea_h_error_data(adapter->handle, res_handle, rblock);

	if (ret == H_SUCCESS) {
		type = EHEA_BMASK_GET(ERROR_DATA_TYPE, rblock[2]);
		*aer = rblock[6];
		*aerr = rblock[12];
		print_error_data(rblock);
	} else if (ret == H_R_STATE) {
		pr_err("No error data available: %llX\n", res_handle);
	} else
		pr_err("Error data could not be fetched: %llX\n", res_handle);

	free_page((unsigned long)rblock);
out:
	return type;
}

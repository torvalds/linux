/*
 * cxgb3i_ddp.c: Chelsio S3xx iSCSI DDP Manager.
 *
 * Copyright (c) 2008 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 */

#include <linux/skbuff.h>
#include <linux/scatterlist.h>

/* from cxgb3 LLD */
#include "common.h"
#include "t3_cpl.h"
#include "t3cdev.h"
#include "cxgb3_ctl_defs.h"
#include "cxgb3_offload.h"
#include "firmware_exports.h"

#include "cxgb3i_ddp.h"

#define DRV_MODULE_NAME         "cxgb3i_ddp"
#define DRV_MODULE_VERSION      "1.0.0"
#define DRV_MODULE_RELDATE      "Dec. 1, 2008"

static char version[] =
	"Chelsio S3xx iSCSI DDP " DRV_MODULE_NAME
	" v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";

MODULE_AUTHOR("Karen Xie <kxie@chelsio.com>");
MODULE_DESCRIPTION("cxgb3i ddp pagepod manager");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

#define ddp_log_error(fmt...) printk(KERN_ERR "cxgb3i_ddp: ERR! " fmt)
#define ddp_log_warn(fmt...)  printk(KERN_WARNING "cxgb3i_ddp: WARN! " fmt)
#define ddp_log_info(fmt...)  printk(KERN_INFO "cxgb3i_ddp: " fmt)

#ifdef __DEBUG_CXGB3I_DDP__
#define ddp_log_debug(fmt, args...) \
	printk(KERN_INFO "cxgb3i_ddp: %s - " fmt, __func__ , ## args)
#else
#define ddp_log_debug(fmt...)
#endif

/*
 * iSCSI Direct Data Placement
 *
 * T3 h/w can directly place the iSCSI Data-In or Data-Out PDU's payload into
 * pre-posted final destination host-memory buffers based on the Initiator
 * Task Tag (ITT) in Data-In or Target Task Tag (TTT) in Data-Out PDUs.
 *
 * The host memory address is programmed into h/w in the format of pagepod
 * entries.
 * The location of the pagepod entry is encoded into ddp tag which is used or
 * is the base for ITT/TTT.
 */

#define DDP_PGIDX_MAX		4
#define DDP_THRESHOLD	2048
static unsigned char ddp_page_order[DDP_PGIDX_MAX] = {0, 1, 2, 4};
static unsigned char ddp_page_shift[DDP_PGIDX_MAX] = {12, 13, 14, 16};
static unsigned char page_idx = DDP_PGIDX_MAX;

static LIST_HEAD(cxgb3i_ddp_list);
static DEFINE_RWLOCK(cxgb3i_ddp_rwlock);

/*
 * functions to program the pagepod in h/w
 */
static inline void ulp_mem_io_set_hdr(struct sk_buff *skb, unsigned int addr)
{
	struct ulp_mem_io *req = (struct ulp_mem_io *)skb->head;

	req->wr.wr_lo = 0;
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_BYPASS));
	req->cmd_lock_addr = htonl(V_ULP_MEMIO_ADDR(addr >> 5) |
				   V_ULPTX_CMD(ULP_MEM_WRITE));
	req->len = htonl(V_ULP_MEMIO_DATA_LEN(PPOD_SIZE >> 5) |
			 V_ULPTX_NFLITS((PPOD_SIZE >> 3) + 1));
}

static int set_ddp_map(struct cxgb3i_ddp_info *ddp, struct pagepod_hdr *hdr,
		       unsigned int idx, unsigned int npods,
		       struct cxgb3i_gather_list *gl)
{
	unsigned int pm_addr = (idx << PPOD_SIZE_SHIFT) + ddp->llimit;
	int i;

	for (i = 0; i < npods; i++, idx++, pm_addr += PPOD_SIZE) {
		struct sk_buff *skb = ddp->gl_skb[idx];
		struct pagepod *ppod;
		int j, pidx;

		/* hold on to the skb until we clear the ddp mapping */
		skb_get(skb);

		ulp_mem_io_set_hdr(skb, pm_addr);
		ppod = (struct pagepod *)
		       (skb->head + sizeof(struct ulp_mem_io));
		memcpy(&(ppod->hdr), hdr, sizeof(struct pagepod));
		for (pidx = 4 * i, j = 0; j < 5; ++j, ++pidx)
			ppod->addr[j] = pidx < gl->nelem ?
				     cpu_to_be64(gl->phys_addr[pidx]) : 0UL;

		skb->priority = CPL_PRIORITY_CONTROL;
		cxgb3_ofld_send(ddp->tdev, skb);
	}
	return 0;
}

static int clear_ddp_map(struct cxgb3i_ddp_info *ddp, unsigned int idx,
			 unsigned int npods)
{
	unsigned int pm_addr = (idx << PPOD_SIZE_SHIFT) + ddp->llimit;
	int i;

	for (i = 0; i < npods; i++, idx++, pm_addr += PPOD_SIZE) {
		struct sk_buff *skb = ddp->gl_skb[idx];

		ddp->gl_skb[idx] = NULL;
		memset((skb->head + sizeof(struct ulp_mem_io)), 0, PPOD_SIZE);
		ulp_mem_io_set_hdr(skb, pm_addr);
		skb->priority = CPL_PRIORITY_CONTROL;
		cxgb3_ofld_send(ddp->tdev, skb);
	}
	return 0;
}

static inline int ddp_find_unused_entries(struct cxgb3i_ddp_info *ddp,
					  int start, int max, int count,
					  struct cxgb3i_gather_list *gl)
{
	unsigned int i, j;

	spin_lock(&ddp->map_lock);
	for (i = start; i <= max;) {
		for (j = 0; j < count; j++) {
			if (ddp->gl_map[i + j])
				break;
		}
		if (j == count) {
			for (j = 0; j < count; j++)
				ddp->gl_map[i + j] = gl;
			spin_unlock(&ddp->map_lock);
			return i;
		}
		i += j + 1;
	}
	spin_unlock(&ddp->map_lock);
	return -EBUSY;
}

static inline void ddp_unmark_entries(struct cxgb3i_ddp_info *ddp,
				      int start, int count)
{
	spin_lock(&ddp->map_lock);
	memset(&ddp->gl_map[start], 0,
	       count * sizeof(struct cxgb3i_gather_list *));
	spin_unlock(&ddp->map_lock);
}

static inline void ddp_free_gl_skb(struct cxgb3i_ddp_info *ddp,
				   int idx, int count)
{
	int i;

	for (i = 0; i < count; i++, idx++)
		if (ddp->gl_skb[idx]) {
			kfree_skb(ddp->gl_skb[idx]);
			ddp->gl_skb[idx] = NULL;
		}
}

static inline int ddp_alloc_gl_skb(struct cxgb3i_ddp_info *ddp, int idx,
				   int count, gfp_t gfp)
{
	int i;

	for (i = 0; i < count; i++) {
		struct sk_buff *skb = alloc_skb(sizeof(struct ulp_mem_io) +
						PPOD_SIZE, gfp);
		if (skb) {
			ddp->gl_skb[idx + i] = skb;
			skb_put(skb, sizeof(struct ulp_mem_io) + PPOD_SIZE);
		} else {
			ddp_free_gl_skb(ddp, idx, i);
			return -ENOMEM;
		}
	}
	return 0;
}

/**
 * cxgb3i_ddp_find_page_index - return ddp page index for a given page size.
 * @pgsz: page size
 * return the ddp page index, if no match is found return DDP_PGIDX_MAX.
 */
int cxgb3i_ddp_find_page_index(unsigned long pgsz)
{
	int i;

	for (i = 0; i < DDP_PGIDX_MAX; i++) {
		if (pgsz == (1UL << ddp_page_shift[i]))
			return i;
	}
	ddp_log_debug("ddp page size 0x%lx not supported.\n", pgsz);
	return DDP_PGIDX_MAX;
}
EXPORT_SYMBOL_GPL(cxgb3i_ddp_find_page_index);

static inline void ddp_gl_unmap(struct pci_dev *pdev,
				struct cxgb3i_gather_list *gl)
{
	int i;

	for (i = 0; i < gl->nelem; i++)
		pci_unmap_page(pdev, gl->phys_addr[i], PAGE_SIZE,
			       PCI_DMA_FROMDEVICE);
}

static inline int ddp_gl_map(struct pci_dev *pdev,
			     struct cxgb3i_gather_list *gl)
{
	int i;

	for (i = 0; i < gl->nelem; i++) {
		gl->phys_addr[i] = pci_map_page(pdev, gl->pages[i], 0,
						PAGE_SIZE,
						PCI_DMA_FROMDEVICE);
		if (unlikely(pci_dma_mapping_error(pdev, gl->phys_addr[i])))
			goto unmap;
	}

	return i;

unmap:
	if (i) {
		unsigned int nelem = gl->nelem;

		gl->nelem = i;
		ddp_gl_unmap(pdev, gl);
		gl->nelem = nelem;
	}
	return -ENOMEM;
}

/**
 * cxgb3i_ddp_make_gl - build ddp page buffer list
 * @xferlen: total buffer length
 * @sgl: page buffer scatter-gather list
 * @sgcnt: # of page buffers
 * @pdev: pci_dev, used for pci map
 * @gfp: allocation mode
 *
 * construct a ddp page buffer list from the scsi scattergather list.
 * coalesce buffers as much as possible, and obtain dma addresses for
 * each page.
 *
 * Return the cxgb3i_gather_list constructed from the page buffers if the
 * memory can be used for ddp. Return NULL otherwise.
 */
struct cxgb3i_gather_list *cxgb3i_ddp_make_gl(unsigned int xferlen,
					      struct scatterlist *sgl,
					      unsigned int sgcnt,
					      struct pci_dev *pdev,
					      gfp_t gfp)
{
	struct cxgb3i_gather_list *gl;
	struct scatterlist *sg = sgl;
	struct page *sgpage = sg_page(sg);
	unsigned int sglen = sg->length;
	unsigned int sgoffset = sg->offset;
	unsigned int npages = (xferlen + sgoffset + PAGE_SIZE - 1) >>
			      PAGE_SHIFT;
	int i = 1, j = 0;

	if (xferlen < DDP_THRESHOLD) {
		ddp_log_debug("xfer %u < threshold %u, no ddp.\n",
			      xferlen, DDP_THRESHOLD);
		return NULL;
	}

	gl = kzalloc(sizeof(struct cxgb3i_gather_list) +
		     npages * (sizeof(dma_addr_t) + sizeof(struct page *)),
		     gfp);
	if (!gl)
		return NULL;

	gl->pages = (struct page **)&gl->phys_addr[npages];
	gl->length = xferlen;
	gl->offset = sgoffset;
	gl->pages[0] = sgpage;

	sg = sg_next(sg);
	while (sg) {
		struct page *page = sg_page(sg);

		if (sgpage == page && sg->offset == sgoffset + sglen)
			sglen += sg->length;
		else {
			/* make sure the sgl is fit for ddp:
			 * each has the same page size, and
			 * all of the middle pages are used completely
			 */
			if ((j && sgoffset) ||
			    ((i != sgcnt - 1) &&
			     ((sglen + sgoffset) & ~PAGE_MASK)))
				goto error_out;

			j++;
			if (j == gl->nelem || sg->offset)
				goto error_out;
			gl->pages[j] = page;
			sglen = sg->length;
			sgoffset = sg->offset;
			sgpage = page;
		}
		i++;
		sg = sg_next(sg);
	}
	gl->nelem = ++j;

	if (ddp_gl_map(pdev, gl) < 0)
		goto error_out;

	return gl;

error_out:
	kfree(gl);
	return NULL;
}
EXPORT_SYMBOL_GPL(cxgb3i_ddp_make_gl);

/**
 * cxgb3i_ddp_release_gl - release a page buffer list
 * @gl: a ddp page buffer list
 * @pdev: pci_dev used for pci_unmap
 * free a ddp page buffer list resulted from cxgb3i_ddp_make_gl().
 */
void cxgb3i_ddp_release_gl(struct cxgb3i_gather_list *gl,
			   struct pci_dev *pdev)
{
	ddp_gl_unmap(pdev, gl);
	kfree(gl);
}
EXPORT_SYMBOL_GPL(cxgb3i_ddp_release_gl);

/**
 * cxgb3i_ddp_tag_reserve - set up ddp for a data transfer
 * @tdev: t3cdev adapter
 * @tid: connection id
 * @tformat: tag format
 * @tagp: the s/w tag, if ddp setup is successful, it will be updated with
 *	  ddp/hw tag
 * @gl: the page momory list
 * @gfp: allocation mode
 *
 * ddp setup for a given page buffer list and construct the ddp tag.
 * return 0 if success, < 0 otherwise.
 */
int cxgb3i_ddp_tag_reserve(struct t3cdev *tdev, unsigned int tid,
			   struct cxgb3i_tag_format *tformat, u32 *tagp,
			   struct cxgb3i_gather_list *gl, gfp_t gfp)
{
	struct cxgb3i_ddp_info *ddp = tdev->ulp_iscsi;
	struct pagepod_hdr hdr;
	unsigned int npods;
	int idx = -1, idx_max;
	int err = -ENOMEM;
	u32 sw_tag = *tagp;
	u32 tag;

	if (page_idx >= DDP_PGIDX_MAX || !ddp || !gl || !gl->nelem ||
		gl->length < DDP_THRESHOLD) {
		ddp_log_debug("pgidx %u, xfer %u/%u, NO ddp.\n",
			      page_idx, gl->length, DDP_THRESHOLD);
		return -EINVAL;
	}

	npods = (gl->nelem + PPOD_PAGES_MAX - 1) >> PPOD_PAGES_SHIFT;
	idx_max = ddp->nppods - npods + 1;

	if (ddp->idx_last == ddp->nppods)
		idx = ddp_find_unused_entries(ddp, 0, idx_max, npods, gl);
	else {
		idx = ddp_find_unused_entries(ddp, ddp->idx_last + 1,
					      idx_max, npods, gl);
		if (idx < 0 && ddp->idx_last >= npods)
			idx = ddp_find_unused_entries(ddp, 0,
						      ddp->idx_last - npods + 1,
						      npods, gl);
	}
	if (idx < 0) {
		ddp_log_debug("xferlen %u, gl %u, npods %u NO DDP.\n",
			      gl->length, gl->nelem, npods);
		return idx;
	}

	err = ddp_alloc_gl_skb(ddp, idx, npods, gfp);
	if (err < 0)
		goto unmark_entries;

	tag = cxgb3i_ddp_tag_base(tformat, sw_tag);
	tag |= idx << PPOD_IDX_SHIFT;

	hdr.rsvd = 0;
	hdr.vld_tid = htonl(F_PPOD_VALID | V_PPOD_TID(tid));
	hdr.pgsz_tag_clr = htonl(tag & ddp->rsvd_tag_mask);
	hdr.maxoffset = htonl(gl->length);
	hdr.pgoffset = htonl(gl->offset);

	err = set_ddp_map(ddp, &hdr, idx, npods, gl);
	if (err < 0)
		goto free_gl_skb;

	ddp->idx_last = idx;
	ddp_log_debug("xfer %u, gl %u,%u, tid 0x%x, 0x%x -> 0x%x(%u,%u).\n",
		      gl->length, gl->nelem, gl->offset, tid, sw_tag, tag,
		      idx, npods);
	*tagp = tag;
	return 0;

free_gl_skb:
	ddp_free_gl_skb(ddp, idx, npods);
unmark_entries:
	ddp_unmark_entries(ddp, idx, npods);
	return err;
}
EXPORT_SYMBOL_GPL(cxgb3i_ddp_tag_reserve);

/**
 * cxgb3i_ddp_tag_release - release a ddp tag
 * @tdev: t3cdev adapter
 * @tag: ddp tag
 * ddp cleanup for a given ddp tag and release all the resources held
 */
void cxgb3i_ddp_tag_release(struct t3cdev *tdev, u32 tag)
{
	struct cxgb3i_ddp_info *ddp = tdev->ulp_iscsi;
	u32 idx;

	if (!ddp) {
		ddp_log_error("release ddp tag 0x%x, ddp NULL.\n", tag);
		return;
	}

	idx = (tag >> PPOD_IDX_SHIFT) & ddp->idx_mask;
	if (idx < ddp->nppods) {
		struct cxgb3i_gather_list *gl = ddp->gl_map[idx];
		unsigned int npods;

		if (!gl) {
			ddp_log_error("release ddp 0x%x, idx 0x%x, gl NULL.\n",
				      tag, idx);
			return;
		}
		npods = (gl->nelem + PPOD_PAGES_MAX - 1) >> PPOD_PAGES_SHIFT;
		ddp_log_debug("ddp tag 0x%x, release idx 0x%x, npods %u.\n",
			      tag, idx, npods);
		clear_ddp_map(ddp, idx, npods);
		ddp_unmark_entries(ddp, idx, npods);
		cxgb3i_ddp_release_gl(gl, ddp->pdev);
	} else
		ddp_log_error("ddp tag 0x%x, idx 0x%x > max 0x%x.\n",
			      tag, idx, ddp->nppods);
}
EXPORT_SYMBOL_GPL(cxgb3i_ddp_tag_release);

static int setup_conn_pgidx(struct t3cdev *tdev, unsigned int tid, int pg_idx,
			    int reply)
{
	struct sk_buff *skb = alloc_skb(sizeof(struct cpl_set_tcb_field),
					GFP_KERNEL);
	struct cpl_set_tcb_field *req;
	u64 val = pg_idx < DDP_PGIDX_MAX ? pg_idx : 0;

	if (!skb)
		return -ENOMEM;

	/* set up ulp submode and page size */
	req = (struct cpl_set_tcb_field *)skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, tid));
	req->reply = V_NO_REPLY(reply ? 0 : 1);
	req->cpu_idx = 0;
	req->word = htons(31);
	req->mask = cpu_to_be64(0xF0000000);
	req->val = cpu_to_be64(val << 28);
	skb->priority = CPL_PRIORITY_CONTROL;

	cxgb3_ofld_send(tdev, skb);
	return 0;
}

/**
 * cxgb3i_setup_conn_host_pagesize - setup the conn.'s ddp page size
 * @tdev: t3cdev adapter
 * @tid: connection id
 * @reply: request reply from h/w
 * set up the ddp page size based on the host PAGE_SIZE for a connection
 * identified by tid
 */
int cxgb3i_setup_conn_host_pagesize(struct t3cdev *tdev, unsigned int tid,
				    int reply)
{
	return setup_conn_pgidx(tdev, tid, page_idx, reply);
}
EXPORT_SYMBOL_GPL(cxgb3i_setup_conn_host_pagesize);

/**
 * cxgb3i_setup_conn_pagesize - setup the conn.'s ddp page size
 * @tdev: t3cdev adapter
 * @tid: connection id
 * @reply: request reply from h/w
 * @pgsz: ddp page size
 * set up the ddp page size for a connection identified by tid
 */
int cxgb3i_setup_conn_pagesize(struct t3cdev *tdev, unsigned int tid,
				int reply, unsigned long pgsz)
{
	int pgidx = cxgb3i_ddp_find_page_index(pgsz);

	return setup_conn_pgidx(tdev, tid, pgidx, reply);
}
EXPORT_SYMBOL_GPL(cxgb3i_setup_conn_pagesize);

/**
 * cxgb3i_setup_conn_digest - setup conn. digest setting
 * @tdev: t3cdev adapter
 * @tid: connection id
 * @hcrc: header digest enabled
 * @dcrc: data digest enabled
 * @reply: request reply from h/w
 * set up the iscsi digest settings for a connection identified by tid
 */
int cxgb3i_setup_conn_digest(struct t3cdev *tdev, unsigned int tid,
			     int hcrc, int dcrc, int reply)
{
	struct sk_buff *skb = alloc_skb(sizeof(struct cpl_set_tcb_field),
					GFP_KERNEL);
	struct cpl_set_tcb_field *req;
	u64 val = (hcrc ? 1 : 0) | (dcrc ? 2 : 0);

	if (!skb)
		return -ENOMEM;

	/* set up ulp submode and page size */
	req = (struct cpl_set_tcb_field *)skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, tid));
	req->reply = V_NO_REPLY(reply ? 0 : 1);
	req->cpu_idx = 0;
	req->word = htons(31);
	req->mask = cpu_to_be64(0x0F000000);
	req->val = cpu_to_be64(val << 24);
	skb->priority = CPL_PRIORITY_CONTROL;

	cxgb3_ofld_send(tdev, skb);
	return 0;
}
EXPORT_SYMBOL_GPL(cxgb3i_setup_conn_digest);

static int ddp_init(struct t3cdev *tdev)
{
	struct cxgb3i_ddp_info *ddp;
	struct ulp_iscsi_info uinfo;
	unsigned int ppmax, bits;
	int i, err;
	static int vers_printed;

	if (!vers_printed) {
		printk(KERN_INFO "%s", version);
		vers_printed = 1;
	}

	err = tdev->ctl(tdev, ULP_ISCSI_GET_PARAMS, &uinfo);
	if (err < 0) {
		ddp_log_error("%s, failed to get iscsi param err=%d.\n",
				 tdev->name, err);
		return err;
	}

	ppmax = (uinfo.ulimit - uinfo.llimit + 1) >> PPOD_SIZE_SHIFT;
	bits = __ilog2_u32(ppmax) + 1;
	if (bits > PPOD_IDX_MAX_SIZE)
		bits = PPOD_IDX_MAX_SIZE;
	ppmax = (1 << (bits - 1)) - 1;

	ddp = cxgb3i_alloc_big_mem(sizeof(struct cxgb3i_ddp_info) +
				   ppmax *
					(sizeof(struct cxgb3i_gather_list *) +
					sizeof(struct sk_buff *)),
				   GFP_KERNEL);
	if (!ddp) {
		ddp_log_warn("%s unable to alloc ddp 0x%d, ddp disabled.\n",
			     tdev->name, ppmax);
		return 0;
	}
	ddp->gl_map = (struct cxgb3i_gather_list **)(ddp + 1);
	ddp->gl_skb = (struct sk_buff **)(((char *)ddp->gl_map) +
					  ppmax *
					  sizeof(struct cxgb3i_gather_list *));
	spin_lock_init(&ddp->map_lock);

	ddp->tdev = tdev;
	ddp->pdev = uinfo.pdev;
	ddp->max_txsz = min_t(unsigned int, uinfo.max_txsz, ULP2_MAX_PKT_SIZE);
	ddp->max_rxsz = min_t(unsigned int, uinfo.max_rxsz, ULP2_MAX_PKT_SIZE);
	ddp->llimit = uinfo.llimit;
	ddp->ulimit = uinfo.ulimit;
	ddp->nppods = ppmax;
	ddp->idx_last = ppmax;
	ddp->idx_bits = bits;
	ddp->idx_mask = (1 << bits) - 1;
	ddp->rsvd_tag_mask = (1 << (bits + PPOD_IDX_SHIFT)) - 1;

	uinfo.tagmask = ddp->idx_mask << PPOD_IDX_SHIFT;
	for (i = 0; i < DDP_PGIDX_MAX; i++)
		uinfo.pgsz_factor[i] = ddp_page_order[i];
	uinfo.ulimit = uinfo.llimit + (ppmax << PPOD_SIZE_SHIFT);

	err = tdev->ctl(tdev, ULP_ISCSI_SET_PARAMS, &uinfo);
	if (err < 0) {
		ddp_log_warn("%s unable to set iscsi param err=%d, "
			      "ddp disabled.\n", tdev->name, err);
		goto free_ddp_map;
	}

	tdev->ulp_iscsi = ddp;

	/* add to the list */
	write_lock(&cxgb3i_ddp_rwlock);
	list_add_tail(&ddp->list, &cxgb3i_ddp_list);
	write_unlock(&cxgb3i_ddp_rwlock);

	ddp_log_info("nppods %u (0x%x ~ 0x%x), bits %u, mask 0x%x,0x%x "
			"pkt %u,%u.\n",
			ppmax, ddp->llimit, ddp->ulimit, ddp->idx_bits,
			ddp->idx_mask, ddp->rsvd_tag_mask,
			ddp->max_txsz, ddp->max_rxsz);
	return 0;

free_ddp_map:
	cxgb3i_free_big_mem(ddp);
	return err;
}

/**
 * cxgb3i_adapter_ddp_init - initialize the adapter's ddp resource
 * @tdev: t3cdev adapter
 * @tformat: tag format
 * @txsz: max tx pkt size, filled in by this func.
 * @rxsz: max rx pkt size, filled in by this func.
 * initialize the ddp pagepod manager for a given adapter if needed and
 * setup the tag format for a given iscsi entity
 */
int cxgb3i_adapter_ddp_init(struct t3cdev *tdev,
			    struct cxgb3i_tag_format *tformat,
			    unsigned int *txsz, unsigned int *rxsz)
{
	struct cxgb3i_ddp_info *ddp;
	unsigned char idx_bits;

	if (!tformat)
		return -EINVAL;

	if (!tdev->ulp_iscsi) {
		int err = ddp_init(tdev);
		if (err < 0)
			return err;
	}
	ddp = (struct cxgb3i_ddp_info *)tdev->ulp_iscsi;

	idx_bits = 32 - tformat->sw_bits;
	tformat->rsvd_bits = ddp->idx_bits;
	tformat->rsvd_shift = PPOD_IDX_SHIFT;
	tformat->rsvd_mask = (1 << tformat->rsvd_bits) - 1;

	ddp_log_info("tag format: sw %u, rsvd %u,%u, mask 0x%x.\n",
		      tformat->sw_bits, tformat->rsvd_bits,
		      tformat->rsvd_shift, tformat->rsvd_mask);

	*txsz = ddp->max_txsz;
	*rxsz = ddp->max_rxsz;
	ddp_log_info("ddp max pkt size: %u, %u.\n",
		     ddp->max_txsz, ddp->max_rxsz);
	return 0;
}
EXPORT_SYMBOL_GPL(cxgb3i_adapter_ddp_init);

static void ddp_release(struct cxgb3i_ddp_info *ddp)
{
	int i = 0;
	struct t3cdev *tdev = ddp->tdev;

	tdev->ulp_iscsi = NULL;
	while (i < ddp->nppods) {
		struct cxgb3i_gather_list *gl = ddp->gl_map[i];
		if (gl) {
			int npods = (gl->nelem + PPOD_PAGES_MAX - 1)
				     >> PPOD_PAGES_SHIFT;

			kfree(gl);
			ddp_free_gl_skb(ddp, i, npods);
		} else
			i++;
	}
	cxgb3i_free_big_mem(ddp);
}

/**
 * cxgb3i_adapter_ddp_cleanup - release the adapter's ddp resource
 * @tdev: t3cdev adapter
 * release all the resource held by the ddp pagepod manager for a given
 * adapter if needed
 */
void cxgb3i_adapter_ddp_cleanup(struct t3cdev *tdev)
{
	struct cxgb3i_ddp_info *ddp;

	/* remove from the list */
	write_lock(&cxgb3i_ddp_rwlock);
	list_for_each_entry(ddp, &cxgb3i_ddp_list, list) {
		if (ddp->tdev == tdev) {
			list_del(&ddp->list);
			break;
		}
	}
	write_unlock(&cxgb3i_ddp_rwlock);

	if (ddp)
		ddp_release(ddp);
}
EXPORT_SYMBOL_GPL(cxgb3i_adapter_ddp_cleanup);

/**
 * cxgb3i_ddp_init_module - module init entry point
 * initialize any driver wide global data structures
 */
static int __init cxgb3i_ddp_init_module(void)
{
	page_idx = cxgb3i_ddp_find_page_index(PAGE_SIZE);
	ddp_log_info("system PAGE_SIZE %lu, ddp idx %u.\n",
		     PAGE_SIZE, page_idx);
	return 0;
}

/**
 * cxgb3i_ddp_exit_module - module cleanup/exit entry point
 * go through the ddp list and release any resource held.
 */
static void __exit cxgb3i_ddp_exit_module(void)
{
	struct cxgb3i_ddp_info *ddp;

	/* release all ddp manager if there is any */
	write_lock(&cxgb3i_ddp_rwlock);
	list_for_each_entry(ddp, &cxgb3i_ddp_list, list) {
		list_del(&ddp->list);
		ddp_release(ddp);
	}
	write_unlock(&cxgb3i_ddp_rwlock);
}

module_init(cxgb3i_ddp_init_module);
module_exit(cxgb3i_ddp_exit_module);

/*
 * Copyright (c) 2006, 2007, 2008, 2009, 2010 QLogic Corporation.
 * All rights reserved.
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
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
 */

#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/swap.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/uio.h>
#include <linux/jiffies.h>
#include <asm/pgtable.h>
#include <linux/delay.h>
#include <linux/export.h>

#include "qib.h"
#include "qib_common.h"
#include "qib_user_sdma.h"

static int qib_open(struct inode *, struct file *);
static int qib_close(struct inode *, struct file *);
static ssize_t qib_write(struct file *, const char __user *, size_t, loff_t *);
static ssize_t qib_aio_write(struct kiocb *, const struct iovec *,
			     unsigned long, loff_t);
static unsigned int qib_poll(struct file *, struct poll_table_struct *);
static int qib_mmapf(struct file *, struct vm_area_struct *);

static const struct file_operations qib_file_ops = {
	.owner = THIS_MODULE,
	.write = qib_write,
	.aio_write = qib_aio_write,
	.open = qib_open,
	.release = qib_close,
	.poll = qib_poll,
	.mmap = qib_mmapf,
	.llseek = noop_llseek,
};

/*
 * Convert kernel virtual addresses to physical addresses so they don't
 * potentially conflict with the chip addresses used as mmap offsets.
 * It doesn't really matter what mmap offset we use as long as we can
 * interpret it correctly.
 */
static u64 cvt_kvaddr(void *p)
{
	struct page *page;
	u64 paddr = 0;

	page = vmalloc_to_page(p);
	if (page)
		paddr = page_to_pfn(page) << PAGE_SHIFT;

	return paddr;
}

static int qib_get_base_info(struct file *fp, void __user *ubase,
			     size_t ubase_size)
{
	struct qib_ctxtdata *rcd = ctxt_fp(fp);
	int ret = 0;
	struct qib_base_info *kinfo = NULL;
	struct qib_devdata *dd = rcd->dd;
	struct qib_pportdata *ppd = rcd->ppd;
	unsigned subctxt_cnt;
	int shared, master;
	size_t sz;

	subctxt_cnt = rcd->subctxt_cnt;
	if (!subctxt_cnt) {
		shared = 0;
		master = 0;
		subctxt_cnt = 1;
	} else {
		shared = 1;
		master = !subctxt_fp(fp);
	}

	sz = sizeof(*kinfo);
	/* If context sharing is not requested, allow the old size structure */
	if (!shared)
		sz -= 7 * sizeof(u64);
	if (ubase_size < sz) {
		ret = -EINVAL;
		goto bail;
	}

	kinfo = kzalloc(sizeof(*kinfo), GFP_KERNEL);
	if (kinfo == NULL) {
		ret = -ENOMEM;
		goto bail;
	}

	ret = dd->f_get_base_info(rcd, kinfo);
	if (ret < 0)
		goto bail;

	kinfo->spi_rcvhdr_cnt = dd->rcvhdrcnt;
	kinfo->spi_rcvhdrent_size = dd->rcvhdrentsize;
	kinfo->spi_tidegrcnt = rcd->rcvegrcnt;
	kinfo->spi_rcv_egrbufsize = dd->rcvegrbufsize;
	/*
	 * have to mmap whole thing
	 */
	kinfo->spi_rcv_egrbuftotlen =
		rcd->rcvegrbuf_chunks * rcd->rcvegrbuf_size;
	kinfo->spi_rcv_egrperchunk = rcd->rcvegrbufs_perchunk;
	kinfo->spi_rcv_egrchunksize = kinfo->spi_rcv_egrbuftotlen /
		rcd->rcvegrbuf_chunks;
	kinfo->spi_tidcnt = dd->rcvtidcnt / subctxt_cnt;
	if (master)
		kinfo->spi_tidcnt += dd->rcvtidcnt % subctxt_cnt;
	/*
	 * for this use, may be cfgctxts summed over all chips that
	 * are are configured and present
	 */
	kinfo->spi_nctxts = dd->cfgctxts;
	/* unit (chip/board) our context is on */
	kinfo->spi_unit = dd->unit;
	kinfo->spi_port = ppd->port;
	/* for now, only a single page */
	kinfo->spi_tid_maxsize = PAGE_SIZE;

	/*
	 * Doing this per context, and based on the skip value, etc.  This has
	 * to be the actual buffer size, since the protocol code treats it
	 * as an array.
	 *
	 * These have to be set to user addresses in the user code via mmap.
	 * These values are used on return to user code for the mmap target
	 * addresses only.  For 32 bit, same 44 bit address problem, so use
	 * the physical address, not virtual.  Before 2.6.11, using the
	 * page_address() macro worked, but in 2.6.11, even that returns the
	 * full 64 bit address (upper bits all 1's).  So far, using the
	 * physical addresses (or chip offsets, for chip mapping) works, but
	 * no doubt some future kernel release will change that, and we'll be
	 * on to yet another method of dealing with this.
	 * Normally only one of rcvhdr_tailaddr or rhf_offset is useful
	 * since the chips with non-zero rhf_offset don't normally
	 * enable tail register updates to host memory, but for testing,
	 * both can be enabled and used.
	 */
	kinfo->spi_rcvhdr_base = (u64) rcd->rcvhdrq_phys;
	kinfo->spi_rcvhdr_tailaddr = (u64) rcd->rcvhdrqtailaddr_phys;
	kinfo->spi_rhf_offset = dd->rhf_offset;
	kinfo->spi_rcv_egrbufs = (u64) rcd->rcvegr_phys;
	kinfo->spi_pioavailaddr = (u64) dd->pioavailregs_phys;
	/* setup per-unit (not port) status area for user programs */
	kinfo->spi_status = (u64) kinfo->spi_pioavailaddr +
		(char *) ppd->statusp -
		(char *) dd->pioavailregs_dma;
	kinfo->spi_uregbase = (u64) dd->uregbase + dd->ureg_align * rcd->ctxt;
	if (!shared) {
		kinfo->spi_piocnt = rcd->piocnt;
		kinfo->spi_piobufbase = (u64) rcd->piobufs;
		kinfo->spi_sendbuf_status = cvt_kvaddr(rcd->user_event_mask);
	} else if (master) {
		kinfo->spi_piocnt = (rcd->piocnt / subctxt_cnt) +
				    (rcd->piocnt % subctxt_cnt);
		/* Master's PIO buffers are after all the slave's */
		kinfo->spi_piobufbase = (u64) rcd->piobufs +
			dd->palign *
			(rcd->piocnt - kinfo->spi_piocnt);
	} else {
		unsigned slave = subctxt_fp(fp) - 1;

		kinfo->spi_piocnt = rcd->piocnt / subctxt_cnt;
		kinfo->spi_piobufbase = (u64) rcd->piobufs +
			dd->palign * kinfo->spi_piocnt * slave;
	}

	if (shared) {
		kinfo->spi_sendbuf_status =
			cvt_kvaddr(&rcd->user_event_mask[subctxt_fp(fp)]);
		/* only spi_subctxt_* fields should be set in this block! */
		kinfo->spi_subctxt_uregbase = cvt_kvaddr(rcd->subctxt_uregbase);

		kinfo->spi_subctxt_rcvegrbuf =
			cvt_kvaddr(rcd->subctxt_rcvegrbuf);
		kinfo->spi_subctxt_rcvhdr_base =
			cvt_kvaddr(rcd->subctxt_rcvhdr_base);
	}

	/*
	 * All user buffers are 2KB buffers.  If we ever support
	 * giving 4KB buffers to user processes, this will need some
	 * work.  Can't use piobufbase directly, because it has
	 * both 2K and 4K buffer base values.
	 */
	kinfo->spi_pioindex = (kinfo->spi_piobufbase - dd->pio2k_bufbase) /
		dd->palign;
	kinfo->spi_pioalign = dd->palign;
	kinfo->spi_qpair = QIB_KD_QP;
	/*
	 * user mode PIO buffers are always 2KB, even when 4KB can
	 * be received, and sent via the kernel; this is ibmaxlen
	 * for 2K MTU.
	 */
	kinfo->spi_piosize = dd->piosize2k - 2 * sizeof(u32);
	kinfo->spi_mtu = ppd->ibmaxlen; /* maxlen, not ibmtu */
	kinfo->spi_ctxt = rcd->ctxt;
	kinfo->spi_subctxt = subctxt_fp(fp);
	kinfo->spi_sw_version = QIB_KERN_SWVERSION;
	kinfo->spi_sw_version |= 1U << 31; /* QLogic-built, not kernel.org */
	kinfo->spi_hw_version = dd->revision;

	if (master)
		kinfo->spi_runtime_flags |= QIB_RUNTIME_MASTER;

	sz = (ubase_size < sizeof(*kinfo)) ? ubase_size : sizeof(*kinfo);
	if (copy_to_user(ubase, kinfo, sz))
		ret = -EFAULT;
bail:
	kfree(kinfo);
	return ret;
}

/**
 * qib_tid_update - update a context TID
 * @rcd: the context
 * @fp: the qib device file
 * @ti: the TID information
 *
 * The new implementation as of Oct 2004 is that the driver assigns
 * the tid and returns it to the caller.   To reduce search time, we
 * keep a cursor for each context, walking the shadow tid array to find
 * one that's not in use.
 *
 * For now, if we can't allocate the full list, we fail, although
 * in the long run, we'll allocate as many as we can, and the
 * caller will deal with that by trying the remaining pages later.
 * That means that when we fail, we have to mark the tids as not in
 * use again, in our shadow copy.
 *
 * It's up to the caller to free the tids when they are done.
 * We'll unlock the pages as they free them.
 *
 * Also, right now we are locking one page at a time, but since
 * the intended use of this routine is for a single group of
 * virtually contiguous pages, that should change to improve
 * performance.
 */
static int qib_tid_update(struct qib_ctxtdata *rcd, struct file *fp,
			  const struct qib_tid_info *ti)
{
	int ret = 0, ntids;
	u32 tid, ctxttid, cnt, i, tidcnt, tidoff;
	u16 *tidlist;
	struct qib_devdata *dd = rcd->dd;
	u64 physaddr;
	unsigned long vaddr;
	u64 __iomem *tidbase;
	unsigned long tidmap[8];
	struct page **pagep = NULL;
	unsigned subctxt = subctxt_fp(fp);

	if (!dd->pageshadow) {
		ret = -ENOMEM;
		goto done;
	}

	cnt = ti->tidcnt;
	if (!cnt) {
		ret = -EFAULT;
		goto done;
	}
	ctxttid = rcd->ctxt * dd->rcvtidcnt;
	if (!rcd->subctxt_cnt) {
		tidcnt = dd->rcvtidcnt;
		tid = rcd->tidcursor;
		tidoff = 0;
	} else if (!subctxt) {
		tidcnt = (dd->rcvtidcnt / rcd->subctxt_cnt) +
			 (dd->rcvtidcnt % rcd->subctxt_cnt);
		tidoff = dd->rcvtidcnt - tidcnt;
		ctxttid += tidoff;
		tid = tidcursor_fp(fp);
	} else {
		tidcnt = dd->rcvtidcnt / rcd->subctxt_cnt;
		tidoff = tidcnt * (subctxt - 1);
		ctxttid += tidoff;
		tid = tidcursor_fp(fp);
	}
	if (cnt > tidcnt) {
		/* make sure it all fits in tid_pg_list */
		qib_devinfo(dd->pcidev, "Process tried to allocate %u "
			 "TIDs, only trying max (%u)\n", cnt, tidcnt);
		cnt = tidcnt;
	}
	pagep = (struct page **) rcd->tid_pg_list;
	tidlist = (u16 *) &pagep[dd->rcvtidcnt];
	pagep += tidoff;
	tidlist += tidoff;

	memset(tidmap, 0, sizeof(tidmap));
	/* before decrement; chip actual # */
	ntids = tidcnt;
	tidbase = (u64 __iomem *) (((char __iomem *) dd->kregbase) +
				   dd->rcvtidbase +
				   ctxttid * sizeof(*tidbase));

	/* virtual address of first page in transfer */
	vaddr = ti->tidvaddr;
	if (!access_ok(VERIFY_WRITE, (void __user *) vaddr,
		       cnt * PAGE_SIZE)) {
		ret = -EFAULT;
		goto done;
	}
	ret = qib_get_user_pages(vaddr, cnt, pagep);
	if (ret) {
		/*
		 * if (ret == -EBUSY)
		 * We can't continue because the pagep array won't be
		 * initialized. This should never happen,
		 * unless perhaps the user has mpin'ed the pages
		 * themselves.
		 */
		qib_devinfo(dd->pcidev,
			 "Failed to lock addr %p, %u pages: "
			 "errno %d\n", (void *) vaddr, cnt, -ret);
		goto done;
	}
	for (i = 0; i < cnt; i++, vaddr += PAGE_SIZE) {
		for (; ntids--; tid++) {
			if (tid == tidcnt)
				tid = 0;
			if (!dd->pageshadow[ctxttid + tid])
				break;
		}
		if (ntids < 0) {
			/*
			 * Oops, wrapped all the way through their TIDs,
			 * and didn't have enough free; see comments at
			 * start of routine
			 */
			i--;    /* last tidlist[i] not filled in */
			ret = -ENOMEM;
			break;
		}
		tidlist[i] = tid + tidoff;
		/* we "know" system pages and TID pages are same size */
		dd->pageshadow[ctxttid + tid] = pagep[i];
		dd->physshadow[ctxttid + tid] =
			qib_map_page(dd->pcidev, pagep[i], 0, PAGE_SIZE,
				     PCI_DMA_FROMDEVICE);
		/*
		 * don't need atomic or it's overhead
		 */
		__set_bit(tid, tidmap);
		physaddr = dd->physshadow[ctxttid + tid];
		/* PERFORMANCE: below should almost certainly be cached */
		dd->f_put_tid(dd, &tidbase[tid],
				  RCVHQ_RCV_TYPE_EXPECTED, physaddr);
		/*
		 * don't check this tid in qib_ctxtshadow, since we
		 * just filled it in; start with the next one.
		 */
		tid++;
	}

	if (ret) {
		u32 limit;
cleanup:
		/* jump here if copy out of updated info failed... */
		/* same code that's in qib_free_tid() */
		limit = sizeof(tidmap) * BITS_PER_BYTE;
		if (limit > tidcnt)
			/* just in case size changes in future */
			limit = tidcnt;
		tid = find_first_bit((const unsigned long *)tidmap, limit);
		for (; tid < limit; tid++) {
			if (!test_bit(tid, tidmap))
				continue;
			if (dd->pageshadow[ctxttid + tid]) {
				dma_addr_t phys;

				phys = dd->physshadow[ctxttid + tid];
				dd->physshadow[ctxttid + tid] = dd->tidinvalid;
				/* PERFORMANCE: below should almost certainly
				 * be cached
				 */
				dd->f_put_tid(dd, &tidbase[tid],
					      RCVHQ_RCV_TYPE_EXPECTED,
					      dd->tidinvalid);
				pci_unmap_page(dd->pcidev, phys, PAGE_SIZE,
					       PCI_DMA_FROMDEVICE);
				dd->pageshadow[ctxttid + tid] = NULL;
			}
		}
		qib_release_user_pages(pagep, cnt);
	} else {
		/*
		 * Copy the updated array, with qib_tid's filled in, back
		 * to user.  Since we did the copy in already, this "should
		 * never fail" If it does, we have to clean up...
		 */
		if (copy_to_user((void __user *)
				 (unsigned long) ti->tidlist,
				 tidlist, cnt * sizeof(*tidlist))) {
			ret = -EFAULT;
			goto cleanup;
		}
		if (copy_to_user((void __user *) (unsigned long) ti->tidmap,
				 tidmap, sizeof tidmap)) {
			ret = -EFAULT;
			goto cleanup;
		}
		if (tid == tidcnt)
			tid = 0;
		if (!rcd->subctxt_cnt)
			rcd->tidcursor = tid;
		else
			tidcursor_fp(fp) = tid;
	}

done:
	return ret;
}

/**
 * qib_tid_free - free a context TID
 * @rcd: the context
 * @subctxt: the subcontext
 * @ti: the TID info
 *
 * right now we are unlocking one page at a time, but since
 * the intended use of this routine is for a single group of
 * virtually contiguous pages, that should change to improve
 * performance.  We check that the TID is in range for this context
 * but otherwise don't check validity; if user has an error and
 * frees the wrong tid, it's only their own data that can thereby
 * be corrupted.  We do check that the TID was in use, for sanity
 * We always use our idea of the saved address, not the address that
 * they pass in to us.
 */
static int qib_tid_free(struct qib_ctxtdata *rcd, unsigned subctxt,
			const struct qib_tid_info *ti)
{
	int ret = 0;
	u32 tid, ctxttid, cnt, limit, tidcnt;
	struct qib_devdata *dd = rcd->dd;
	u64 __iomem *tidbase;
	unsigned long tidmap[8];

	if (!dd->pageshadow) {
		ret = -ENOMEM;
		goto done;
	}

	if (copy_from_user(tidmap, (void __user *)(unsigned long)ti->tidmap,
			   sizeof tidmap)) {
		ret = -EFAULT;
		goto done;
	}

	ctxttid = rcd->ctxt * dd->rcvtidcnt;
	if (!rcd->subctxt_cnt)
		tidcnt = dd->rcvtidcnt;
	else if (!subctxt) {
		tidcnt = (dd->rcvtidcnt / rcd->subctxt_cnt) +
			 (dd->rcvtidcnt % rcd->subctxt_cnt);
		ctxttid += dd->rcvtidcnt - tidcnt;
	} else {
		tidcnt = dd->rcvtidcnt / rcd->subctxt_cnt;
		ctxttid += tidcnt * (subctxt - 1);
	}
	tidbase = (u64 __iomem *) ((char __iomem *)(dd->kregbase) +
				   dd->rcvtidbase +
				   ctxttid * sizeof(*tidbase));

	limit = sizeof(tidmap) * BITS_PER_BYTE;
	if (limit > tidcnt)
		/* just in case size changes in future */
		limit = tidcnt;
	tid = find_first_bit(tidmap, limit);
	for (cnt = 0; tid < limit; tid++) {
		/*
		 * small optimization; if we detect a run of 3 or so without
		 * any set, use find_first_bit again.  That's mainly to
		 * accelerate the case where we wrapped, so we have some at
		 * the beginning, and some at the end, and a big gap
		 * in the middle.
		 */
		if (!test_bit(tid, tidmap))
			continue;
		cnt++;
		if (dd->pageshadow[ctxttid + tid]) {
			struct page *p;
			dma_addr_t phys;

			p = dd->pageshadow[ctxttid + tid];
			dd->pageshadow[ctxttid + tid] = NULL;
			phys = dd->physshadow[ctxttid + tid];
			dd->physshadow[ctxttid + tid] = dd->tidinvalid;
			/* PERFORMANCE: below should almost certainly be
			 * cached
			 */
			dd->f_put_tid(dd, &tidbase[tid],
				      RCVHQ_RCV_TYPE_EXPECTED, dd->tidinvalid);
			pci_unmap_page(dd->pcidev, phys, PAGE_SIZE,
				       PCI_DMA_FROMDEVICE);
			qib_release_user_pages(&p, 1);
		}
	}
done:
	return ret;
}

/**
 * qib_set_part_key - set a partition key
 * @rcd: the context
 * @key: the key
 *
 * We can have up to 4 active at a time (other than the default, which is
 * always allowed).  This is somewhat tricky, since multiple contexts may set
 * the same key, so we reference count them, and clean up at exit.  All 4
 * partition keys are packed into a single qlogic_ib register.  It's an
 * error for a process to set the same pkey multiple times.  We provide no
 * mechanism to de-allocate a pkey at this time, we may eventually need to
 * do that.  I've used the atomic operations, and no locking, and only make
 * a single pass through what's available.  This should be more than
 * adequate for some time. I'll think about spinlocks or the like if and as
 * it's necessary.
 */
static int qib_set_part_key(struct qib_ctxtdata *rcd, u16 key)
{
	struct qib_pportdata *ppd = rcd->ppd;
	int i, any = 0, pidx = -1;
	u16 lkey = key & 0x7FFF;
	int ret;

	if (lkey == (QIB_DEFAULT_P_KEY & 0x7FFF)) {
		/* nothing to do; this key always valid */
		ret = 0;
		goto bail;
	}

	if (!lkey) {
		ret = -EINVAL;
		goto bail;
	}

	/*
	 * Set the full membership bit, because it has to be
	 * set in the register or the packet, and it seems
	 * cleaner to set in the register than to force all
	 * callers to set it.
	 */
	key |= 0x8000;

	for (i = 0; i < ARRAY_SIZE(rcd->pkeys); i++) {
		if (!rcd->pkeys[i] && pidx == -1)
			pidx = i;
		if (rcd->pkeys[i] == key) {
			ret = -EEXIST;
			goto bail;
		}
	}
	if (pidx == -1) {
		ret = -EBUSY;
		goto bail;
	}
	for (any = i = 0; i < ARRAY_SIZE(ppd->pkeys); i++) {
		if (!ppd->pkeys[i]) {
			any++;
			continue;
		}
		if (ppd->pkeys[i] == key) {
			atomic_t *pkrefs = &ppd->pkeyrefs[i];

			if (atomic_inc_return(pkrefs) > 1) {
				rcd->pkeys[pidx] = key;
				ret = 0;
				goto bail;
			} else {
				/*
				 * lost race, decrement count, catch below
				 */
				atomic_dec(pkrefs);
				any++;
			}
		}
		if ((ppd->pkeys[i] & 0x7FFF) == lkey) {
			/*
			 * It makes no sense to have both the limited and
			 * full membership PKEY set at the same time since
			 * the unlimited one will disable the limited one.
			 */
			ret = -EEXIST;
			goto bail;
		}
	}
	if (!any) {
		ret = -EBUSY;
		goto bail;
	}
	for (any = i = 0; i < ARRAY_SIZE(ppd->pkeys); i++) {
		if (!ppd->pkeys[i] &&
		    atomic_inc_return(&ppd->pkeyrefs[i]) == 1) {
			rcd->pkeys[pidx] = key;
			ppd->pkeys[i] = key;
			(void) ppd->dd->f_set_ib_cfg(ppd, QIB_IB_CFG_PKEYS, 0);
			ret = 0;
			goto bail;
		}
	}
	ret = -EBUSY;

bail:
	return ret;
}

/**
 * qib_manage_rcvq - manage a context's receive queue
 * @rcd: the context
 * @subctxt: the subcontext
 * @start_stop: action to carry out
 *
 * start_stop == 0 disables receive on the context, for use in queue
 * overflow conditions.  start_stop==1 re-enables, to be used to
 * re-init the software copy of the head register
 */
static int qib_manage_rcvq(struct qib_ctxtdata *rcd, unsigned subctxt,
			   int start_stop)
{
	struct qib_devdata *dd = rcd->dd;
	unsigned int rcvctrl_op;

	if (subctxt)
		goto bail;
	/* atomically clear receive enable ctxt. */
	if (start_stop) {
		/*
		 * On enable, force in-memory copy of the tail register to
		 * 0, so that protocol code doesn't have to worry about
		 * whether or not the chip has yet updated the in-memory
		 * copy or not on return from the system call. The chip
		 * always resets it's tail register back to 0 on a
		 * transition from disabled to enabled.
		 */
		if (rcd->rcvhdrtail_kvaddr)
			qib_clear_rcvhdrtail(rcd);
		rcvctrl_op = QIB_RCVCTRL_CTXT_ENB;
	} else
		rcvctrl_op = QIB_RCVCTRL_CTXT_DIS;
	dd->f_rcvctrl(rcd->ppd, rcvctrl_op, rcd->ctxt);
	/* always; new head should be equal to new tail; see above */
bail:
	return 0;
}

static void qib_clean_part_key(struct qib_ctxtdata *rcd,
			       struct qib_devdata *dd)
{
	int i, j, pchanged = 0;
	u64 oldpkey;
	struct qib_pportdata *ppd = rcd->ppd;

	/* for debugging only */
	oldpkey = (u64) ppd->pkeys[0] |
		((u64) ppd->pkeys[1] << 16) |
		((u64) ppd->pkeys[2] << 32) |
		((u64) ppd->pkeys[3] << 48);

	for (i = 0; i < ARRAY_SIZE(rcd->pkeys); i++) {
		if (!rcd->pkeys[i])
			continue;
		for (j = 0; j < ARRAY_SIZE(ppd->pkeys); j++) {
			/* check for match independent of the global bit */
			if ((ppd->pkeys[j] & 0x7fff) !=
			    (rcd->pkeys[i] & 0x7fff))
				continue;
			if (atomic_dec_and_test(&ppd->pkeyrefs[j])) {
				ppd->pkeys[j] = 0;
				pchanged++;
			}
			break;
		}
		rcd->pkeys[i] = 0;
	}
	if (pchanged)
		(void) ppd->dd->f_set_ib_cfg(ppd, QIB_IB_CFG_PKEYS, 0);
}

/* common code for the mappings on dma_alloc_coherent mem */
static int qib_mmap_mem(struct vm_area_struct *vma, struct qib_ctxtdata *rcd,
			unsigned len, void *kvaddr, u32 write_ok, char *what)
{
	struct qib_devdata *dd = rcd->dd;
	unsigned long pfn;
	int ret;

	if ((vma->vm_end - vma->vm_start) > len) {
		qib_devinfo(dd->pcidev,
			 "FAIL on %s: len %lx > %x\n", what,
			 vma->vm_end - vma->vm_start, len);
		ret = -EFAULT;
		goto bail;
	}

	/*
	 * shared context user code requires rcvhdrq mapped r/w, others
	 * only allowed readonly mapping.
	 */
	if (!write_ok) {
		if (vma->vm_flags & VM_WRITE) {
			qib_devinfo(dd->pcidev,
				 "%s must be mapped readonly\n", what);
			ret = -EPERM;
			goto bail;
		}

		/* don't allow them to later change with mprotect */
		vma->vm_flags &= ~VM_MAYWRITE;
	}

	pfn = virt_to_phys(kvaddr) >> PAGE_SHIFT;
	ret = remap_pfn_range(vma, vma->vm_start, pfn,
			      len, vma->vm_page_prot);
	if (ret)
		qib_devinfo(dd->pcidev, "%s ctxt%u mmap of %lx, %x "
			 "bytes failed: %d\n", what, rcd->ctxt,
			 pfn, len, ret);
bail:
	return ret;
}

static int mmap_ureg(struct vm_area_struct *vma, struct qib_devdata *dd,
		     u64 ureg)
{
	unsigned long phys;
	unsigned long sz;
	int ret;

	/*
	 * This is real hardware, so use io_remap.  This is the mechanism
	 * for the user process to update the head registers for their ctxt
	 * in the chip.
	 */
	sz = dd->flags & QIB_HAS_HDRSUPP ? 2 * PAGE_SIZE : PAGE_SIZE;
	if ((vma->vm_end - vma->vm_start) > sz) {
		qib_devinfo(dd->pcidev, "FAIL mmap userreg: reqlen "
			 "%lx > PAGE\n", vma->vm_end - vma->vm_start);
		ret = -EFAULT;
	} else {
		phys = dd->physaddr + ureg;
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

		vma->vm_flags |= VM_DONTCOPY | VM_DONTEXPAND;
		ret = io_remap_pfn_range(vma, vma->vm_start,
					 phys >> PAGE_SHIFT,
					 vma->vm_end - vma->vm_start,
					 vma->vm_page_prot);
	}
	return ret;
}

static int mmap_piobufs(struct vm_area_struct *vma,
			struct qib_devdata *dd,
			struct qib_ctxtdata *rcd,
			unsigned piobufs, unsigned piocnt)
{
	unsigned long phys;
	int ret;

	/*
	 * When we map the PIO buffers in the chip, we want to map them as
	 * writeonly, no read possible; unfortunately, x86 doesn't allow
	 * for this in hardware, but we still prevent users from asking
	 * for it.
	 */
	if ((vma->vm_end - vma->vm_start) > (piocnt * dd->palign)) {
		qib_devinfo(dd->pcidev, "FAIL mmap piobufs: "
			 "reqlen %lx > PAGE\n",
			 vma->vm_end - vma->vm_start);
		ret = -EINVAL;
		goto bail;
	}

	phys = dd->physaddr + piobufs;

#if defined(__powerpc__)
	/* There isn't a generic way to specify writethrough mappings */
	pgprot_val(vma->vm_page_prot) |= _PAGE_NO_CACHE;
	pgprot_val(vma->vm_page_prot) |= _PAGE_WRITETHRU;
	pgprot_val(vma->vm_page_prot) &= ~_PAGE_GUARDED;
#endif

	/*
	 * don't allow them to later change to readable with mprotect (for when
	 * not initially mapped readable, as is normally the case)
	 */
	vma->vm_flags &= ~VM_MAYREAD;
	vma->vm_flags |= VM_DONTCOPY | VM_DONTEXPAND;

	if (qib_wc_pat)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	ret = io_remap_pfn_range(vma, vma->vm_start, phys >> PAGE_SHIFT,
				 vma->vm_end - vma->vm_start,
				 vma->vm_page_prot);
bail:
	return ret;
}

static int mmap_rcvegrbufs(struct vm_area_struct *vma,
			   struct qib_ctxtdata *rcd)
{
	struct qib_devdata *dd = rcd->dd;
	unsigned long start, size;
	size_t total_size, i;
	unsigned long pfn;
	int ret;

	size = rcd->rcvegrbuf_size;
	total_size = rcd->rcvegrbuf_chunks * size;
	if ((vma->vm_end - vma->vm_start) > total_size) {
		qib_devinfo(dd->pcidev, "FAIL on egr bufs: "
			 "reqlen %lx > actual %lx\n",
			 vma->vm_end - vma->vm_start,
			 (unsigned long) total_size);
		ret = -EINVAL;
		goto bail;
	}

	if (vma->vm_flags & VM_WRITE) {
		qib_devinfo(dd->pcidev, "Can't map eager buffers as "
			 "writable (flags=%lx)\n", vma->vm_flags);
		ret = -EPERM;
		goto bail;
	}
	/* don't allow them to later change to writeable with mprotect */
	vma->vm_flags &= ~VM_MAYWRITE;

	start = vma->vm_start;

	for (i = 0; i < rcd->rcvegrbuf_chunks; i++, start += size) {
		pfn = virt_to_phys(rcd->rcvegrbuf[i]) >> PAGE_SHIFT;
		ret = remap_pfn_range(vma, start, pfn, size,
				      vma->vm_page_prot);
		if (ret < 0)
			goto bail;
	}
	ret = 0;

bail:
	return ret;
}

/*
 * qib_file_vma_fault - handle a VMA page fault.
 */
static int qib_file_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *page;

	page = vmalloc_to_page((void *)(vmf->pgoff << PAGE_SHIFT));
	if (!page)
		return VM_FAULT_SIGBUS;

	get_page(page);
	vmf->page = page;

	return 0;
}

static struct vm_operations_struct qib_file_vm_ops = {
	.fault = qib_file_vma_fault,
};

static int mmap_kvaddr(struct vm_area_struct *vma, u64 pgaddr,
		       struct qib_ctxtdata *rcd, unsigned subctxt)
{
	struct qib_devdata *dd = rcd->dd;
	unsigned subctxt_cnt;
	unsigned long len;
	void *addr;
	size_t size;
	int ret = 0;

	subctxt_cnt = rcd->subctxt_cnt;
	size = rcd->rcvegrbuf_chunks * rcd->rcvegrbuf_size;

	/*
	 * Each process has all the subctxt uregbase, rcvhdrq, and
	 * rcvegrbufs mmapped - as an array for all the processes,
	 * and also separately for this process.
	 */
	if (pgaddr == cvt_kvaddr(rcd->subctxt_uregbase)) {
		addr = rcd->subctxt_uregbase;
		size = PAGE_SIZE * subctxt_cnt;
	} else if (pgaddr == cvt_kvaddr(rcd->subctxt_rcvhdr_base)) {
		addr = rcd->subctxt_rcvhdr_base;
		size = rcd->rcvhdrq_size * subctxt_cnt;
	} else if (pgaddr == cvt_kvaddr(rcd->subctxt_rcvegrbuf)) {
		addr = rcd->subctxt_rcvegrbuf;
		size *= subctxt_cnt;
	} else if (pgaddr == cvt_kvaddr(rcd->subctxt_uregbase +
					PAGE_SIZE * subctxt)) {
		addr = rcd->subctxt_uregbase + PAGE_SIZE * subctxt;
		size = PAGE_SIZE;
	} else if (pgaddr == cvt_kvaddr(rcd->subctxt_rcvhdr_base +
					rcd->rcvhdrq_size * subctxt)) {
		addr = rcd->subctxt_rcvhdr_base +
			rcd->rcvhdrq_size * subctxt;
		size = rcd->rcvhdrq_size;
	} else if (pgaddr == cvt_kvaddr(&rcd->user_event_mask[subctxt])) {
		addr = rcd->user_event_mask;
		size = PAGE_SIZE;
	} else if (pgaddr == cvt_kvaddr(rcd->subctxt_rcvegrbuf +
					size * subctxt)) {
		addr = rcd->subctxt_rcvegrbuf + size * subctxt;
		/* rcvegrbufs are read-only on the slave */
		if (vma->vm_flags & VM_WRITE) {
			qib_devinfo(dd->pcidev,
				 "Can't map eager buffers as "
				 "writable (flags=%lx)\n", vma->vm_flags);
			ret = -EPERM;
			goto bail;
		}
		/*
		 * Don't allow permission to later change to writeable
		 * with mprotect.
		 */
		vma->vm_flags &= ~VM_MAYWRITE;
	} else
		goto bail;
	len = vma->vm_end - vma->vm_start;
	if (len > size) {
		ret = -EINVAL;
		goto bail;
	}

	vma->vm_pgoff = (unsigned long) addr >> PAGE_SHIFT;
	vma->vm_ops = &qib_file_vm_ops;
	vma->vm_flags |= VM_RESERVED | VM_DONTEXPAND;
	ret = 1;

bail:
	return ret;
}

/**
 * qib_mmapf - mmap various structures into user space
 * @fp: the file pointer
 * @vma: the VM area
 *
 * We use this to have a shared buffer between the kernel and the user code
 * for the rcvhdr queue, egr buffers, and the per-context user regs and pio
 * buffers in the chip.  We have the open and close entries so we can bump
 * the ref count and keep the driver from being unloaded while still mapped.
 */
static int qib_mmapf(struct file *fp, struct vm_area_struct *vma)
{
	struct qib_ctxtdata *rcd;
	struct qib_devdata *dd;
	u64 pgaddr, ureg;
	unsigned piobufs, piocnt;
	int ret, match = 1;

	rcd = ctxt_fp(fp);
	if (!rcd || !(vma->vm_flags & VM_SHARED)) {
		ret = -EINVAL;
		goto bail;
	}
	dd = rcd->dd;

	/*
	 * This is the qib_do_user_init() code, mapping the shared buffers
	 * and per-context user registers into the user process. The address
	 * referred to by vm_pgoff is the file offset passed via mmap().
	 * For shared contexts, this is the kernel vmalloc() address of the
	 * pages to share with the master.
	 * For non-shared or master ctxts, this is a physical address.
	 * We only do one mmap for each space mapped.
	 */
	pgaddr = vma->vm_pgoff << PAGE_SHIFT;

	/*
	 * Check for 0 in case one of the allocations failed, but user
	 * called mmap anyway.
	 */
	if (!pgaddr)  {
		ret = -EINVAL;
		goto bail;
	}

	/*
	 * Physical addresses must fit in 40 bits for our hardware.
	 * Check for kernel virtual addresses first, anything else must
	 * match a HW or memory address.
	 */
	ret = mmap_kvaddr(vma, pgaddr, rcd, subctxt_fp(fp));
	if (ret) {
		if (ret > 0)
			ret = 0;
		goto bail;
	}

	ureg = dd->uregbase + dd->ureg_align * rcd->ctxt;
	if (!rcd->subctxt_cnt) {
		/* ctxt is not shared */
		piocnt = rcd->piocnt;
		piobufs = rcd->piobufs;
	} else if (!subctxt_fp(fp)) {
		/* caller is the master */
		piocnt = (rcd->piocnt / rcd->subctxt_cnt) +
			 (rcd->piocnt % rcd->subctxt_cnt);
		piobufs = rcd->piobufs +
			dd->palign * (rcd->piocnt - piocnt);
	} else {
		unsigned slave = subctxt_fp(fp) - 1;

		/* caller is a slave */
		piocnt = rcd->piocnt / rcd->subctxt_cnt;
		piobufs = rcd->piobufs + dd->palign * piocnt * slave;
	}

	if (pgaddr == ureg)
		ret = mmap_ureg(vma, dd, ureg);
	else if (pgaddr == piobufs)
		ret = mmap_piobufs(vma, dd, rcd, piobufs, piocnt);
	else if (pgaddr == dd->pioavailregs_phys)
		/* in-memory copy of pioavail registers */
		ret = qib_mmap_mem(vma, rcd, PAGE_SIZE,
				   (void *) dd->pioavailregs_dma, 0,
				   "pioavail registers");
	else if (pgaddr == rcd->rcvegr_phys)
		ret = mmap_rcvegrbufs(vma, rcd);
	else if (pgaddr == (u64) rcd->rcvhdrq_phys)
		/*
		 * The rcvhdrq itself; multiple pages, contiguous
		 * from an i/o perspective.  Shared contexts need
		 * to map r/w, so we allow writing.
		 */
		ret = qib_mmap_mem(vma, rcd, rcd->rcvhdrq_size,
				   rcd->rcvhdrq, 1, "rcvhdrq");
	else if (pgaddr == (u64) rcd->rcvhdrqtailaddr_phys)
		/* in-memory copy of rcvhdrq tail register */
		ret = qib_mmap_mem(vma, rcd, PAGE_SIZE,
				   rcd->rcvhdrtail_kvaddr, 0,
				   "rcvhdrq tail");
	else
		match = 0;
	if (!match)
		ret = -EINVAL;

	vma->vm_private_data = NULL;

	if (ret < 0)
		qib_devinfo(dd->pcidev,
			 "mmap Failure %d: off %llx len %lx\n",
			 -ret, (unsigned long long)pgaddr,
			 vma->vm_end - vma->vm_start);
bail:
	return ret;
}

static unsigned int qib_poll_urgent(struct qib_ctxtdata *rcd,
				    struct file *fp,
				    struct poll_table_struct *pt)
{
	struct qib_devdata *dd = rcd->dd;
	unsigned pollflag;

	poll_wait(fp, &rcd->wait, pt);

	spin_lock_irq(&dd->uctxt_lock);
	if (rcd->urgent != rcd->urgent_poll) {
		pollflag = POLLIN | POLLRDNORM;
		rcd->urgent_poll = rcd->urgent;
	} else {
		pollflag = 0;
		set_bit(QIB_CTXT_WAITING_URG, &rcd->flag);
	}
	spin_unlock_irq(&dd->uctxt_lock);

	return pollflag;
}

static unsigned int qib_poll_next(struct qib_ctxtdata *rcd,
				  struct file *fp,
				  struct poll_table_struct *pt)
{
	struct qib_devdata *dd = rcd->dd;
	unsigned pollflag;

	poll_wait(fp, &rcd->wait, pt);

	spin_lock_irq(&dd->uctxt_lock);
	if (dd->f_hdrqempty(rcd)) {
		set_bit(QIB_CTXT_WAITING_RCV, &rcd->flag);
		dd->f_rcvctrl(rcd->ppd, QIB_RCVCTRL_INTRAVAIL_ENB, rcd->ctxt);
		pollflag = 0;
	} else
		pollflag = POLLIN | POLLRDNORM;
	spin_unlock_irq(&dd->uctxt_lock);

	return pollflag;
}

static unsigned int qib_poll(struct file *fp, struct poll_table_struct *pt)
{
	struct qib_ctxtdata *rcd;
	unsigned pollflag;

	rcd = ctxt_fp(fp);
	if (!rcd)
		pollflag = POLLERR;
	else if (rcd->poll_type == QIB_POLL_TYPE_URGENT)
		pollflag = qib_poll_urgent(rcd, fp, pt);
	else  if (rcd->poll_type == QIB_POLL_TYPE_ANYRCV)
		pollflag = qib_poll_next(rcd, fp, pt);
	else /* invalid */
		pollflag = POLLERR;

	return pollflag;
}

/*
 * Check that userland and driver are compatible for subcontexts.
 */
static int qib_compatible_subctxts(int user_swmajor, int user_swminor)
{
	/* this code is written long-hand for clarity */
	if (QIB_USER_SWMAJOR != user_swmajor) {
		/* no promise of compatibility if major mismatch */
		return 0;
	}
	if (QIB_USER_SWMAJOR == 1) {
		switch (QIB_USER_SWMINOR) {
		case 0:
		case 1:
		case 2:
			/* no subctxt implementation so cannot be compatible */
			return 0;
		case 3:
			/* 3 is only compatible with itself */
			return user_swminor == 3;
		default:
			/* >= 4 are compatible (or are expected to be) */
			return user_swminor >= 4;
		}
	}
	/* make no promises yet for future major versions */
	return 0;
}

static int init_subctxts(struct qib_devdata *dd,
			 struct qib_ctxtdata *rcd,
			 const struct qib_user_info *uinfo)
{
	int ret = 0;
	unsigned num_subctxts;
	size_t size;

	/*
	 * If the user is requesting zero subctxts,
	 * skip the subctxt allocation.
	 */
	if (uinfo->spu_subctxt_cnt <= 0)
		goto bail;
	num_subctxts = uinfo->spu_subctxt_cnt;

	/* Check for subctxt compatibility */
	if (!qib_compatible_subctxts(uinfo->spu_userversion >> 16,
		uinfo->spu_userversion & 0xffff)) {
		qib_devinfo(dd->pcidev,
			 "Mismatched user version (%d.%d) and driver "
			 "version (%d.%d) while context sharing. Ensure "
			 "that driver and library are from the same "
			 "release.\n",
			 (int) (uinfo->spu_userversion >> 16),
			 (int) (uinfo->spu_userversion & 0xffff),
			 QIB_USER_SWMAJOR, QIB_USER_SWMINOR);
		goto bail;
	}
	if (num_subctxts > QLOGIC_IB_MAX_SUBCTXT) {
		ret = -EINVAL;
		goto bail;
	}

	rcd->subctxt_uregbase = vmalloc_user(PAGE_SIZE * num_subctxts);
	if (!rcd->subctxt_uregbase) {
		ret = -ENOMEM;
		goto bail;
	}
	/* Note: rcd->rcvhdrq_size isn't initialized yet. */
	size = ALIGN(dd->rcvhdrcnt * dd->rcvhdrentsize *
		     sizeof(u32), PAGE_SIZE) * num_subctxts;
	rcd->subctxt_rcvhdr_base = vmalloc_user(size);
	if (!rcd->subctxt_rcvhdr_base) {
		ret = -ENOMEM;
		goto bail_ureg;
	}

	rcd->subctxt_rcvegrbuf = vmalloc_user(rcd->rcvegrbuf_chunks *
					      rcd->rcvegrbuf_size *
					      num_subctxts);
	if (!rcd->subctxt_rcvegrbuf) {
		ret = -ENOMEM;
		goto bail_rhdr;
	}

	rcd->subctxt_cnt = uinfo->spu_subctxt_cnt;
	rcd->subctxt_id = uinfo->spu_subctxt_id;
	rcd->active_slaves = 1;
	rcd->redirect_seq_cnt = 1;
	set_bit(QIB_CTXT_MASTER_UNINIT, &rcd->flag);
	goto bail;

bail_rhdr:
	vfree(rcd->subctxt_rcvhdr_base);
bail_ureg:
	vfree(rcd->subctxt_uregbase);
	rcd->subctxt_uregbase = NULL;
bail:
	return ret;
}

static int setup_ctxt(struct qib_pportdata *ppd, int ctxt,
		      struct file *fp, const struct qib_user_info *uinfo)
{
	struct qib_devdata *dd = ppd->dd;
	struct qib_ctxtdata *rcd;
	void *ptmp = NULL;
	int ret;

	rcd = qib_create_ctxtdata(ppd, ctxt);

	/*
	 * Allocate memory for use in qib_tid_update() at open to
	 * reduce cost of expected send setup per message segment
	 */
	if (rcd)
		ptmp = kmalloc(dd->rcvtidcnt * sizeof(u16) +
			       dd->rcvtidcnt * sizeof(struct page **),
			       GFP_KERNEL);

	if (!rcd || !ptmp) {
		qib_dev_err(dd, "Unable to allocate ctxtdata "
			    "memory, failing open\n");
		ret = -ENOMEM;
		goto bailerr;
	}
	rcd->userversion = uinfo->spu_userversion;
	ret = init_subctxts(dd, rcd, uinfo);
	if (ret)
		goto bailerr;
	rcd->tid_pg_list = ptmp;
	rcd->pid = current->pid;
	init_waitqueue_head(&dd->rcd[ctxt]->wait);
	strlcpy(rcd->comm, current->comm, sizeof(rcd->comm));
	ctxt_fp(fp) = rcd;
	qib_stats.sps_ctxts++;
	dd->freectxts++;
	ret = 0;
	goto bail;

bailerr:
	dd->rcd[ctxt] = NULL;
	kfree(rcd);
	kfree(ptmp);
bail:
	return ret;
}

static inline int usable(struct qib_pportdata *ppd)
{
	struct qib_devdata *dd = ppd->dd;

	return dd && (dd->flags & QIB_PRESENT) && dd->kregbase && ppd->lid &&
		(ppd->lflags & QIBL_LINKACTIVE);
}

/*
 * Select a context on the given device, either using a requested port
 * or the port based on the context number.
 */
static int choose_port_ctxt(struct file *fp, struct qib_devdata *dd, u32 port,
			    const struct qib_user_info *uinfo)
{
	struct qib_pportdata *ppd = NULL;
	int ret, ctxt;

	if (port) {
		if (!usable(dd->pport + port - 1)) {
			ret = -ENETDOWN;
			goto done;
		} else
			ppd = dd->pport + port - 1;
	}
	for (ctxt = dd->first_user_ctxt; ctxt < dd->cfgctxts && dd->rcd[ctxt];
	     ctxt++)
		;
	if (ctxt == dd->cfgctxts) {
		ret = -EBUSY;
		goto done;
	}
	if (!ppd) {
		u32 pidx = ctxt % dd->num_pports;
		if (usable(dd->pport + pidx))
			ppd = dd->pport + pidx;
		else {
			for (pidx = 0; pidx < dd->num_pports && !ppd;
			     pidx++)
				if (usable(dd->pport + pidx))
					ppd = dd->pport + pidx;
		}
	}
	ret = ppd ? setup_ctxt(ppd, ctxt, fp, uinfo) : -ENETDOWN;
done:
	return ret;
}

static int find_free_ctxt(int unit, struct file *fp,
			  const struct qib_user_info *uinfo)
{
	struct qib_devdata *dd = qib_lookup(unit);
	int ret;

	if (!dd || (uinfo->spu_port && uinfo->spu_port > dd->num_pports))
		ret = -ENODEV;
	else
		ret = choose_port_ctxt(fp, dd, uinfo->spu_port, uinfo);

	return ret;
}

static int get_a_ctxt(struct file *fp, const struct qib_user_info *uinfo,
		      unsigned alg)
{
	struct qib_devdata *udd = NULL;
	int ret = 0, devmax, npresent, nup, ndev, dusable = 0, i;
	u32 port = uinfo->spu_port, ctxt;

	devmax = qib_count_units(&npresent, &nup);
	if (!npresent) {
		ret = -ENXIO;
		goto done;
	}
	if (nup == 0) {
		ret = -ENETDOWN;
		goto done;
	}

	if (alg == QIB_PORT_ALG_ACROSS) {
		unsigned inuse = ~0U;
		/* find device (with ACTIVE ports) with fewest ctxts in use */
		for (ndev = 0; ndev < devmax; ndev++) {
			struct qib_devdata *dd = qib_lookup(ndev);
			unsigned cused = 0, cfree = 0, pusable = 0;
			if (!dd)
				continue;
			if (port && port <= dd->num_pports &&
			    usable(dd->pport + port - 1))
				pusable = 1;
			else
				for (i = 0; i < dd->num_pports; i++)
					if (usable(dd->pport + i))
						pusable++;
			if (!pusable)
				continue;
			for (ctxt = dd->first_user_ctxt; ctxt < dd->cfgctxts;
			     ctxt++)
				if (dd->rcd[ctxt])
					cused++;
				else
					cfree++;
			if (pusable && cfree && cused < inuse) {
				udd = dd;
				inuse = cused;
			}
		}
		if (udd) {
			ret = choose_port_ctxt(fp, udd, port, uinfo);
			goto done;
		}
	} else {
		for (ndev = 0; ndev < devmax; ndev++) {
			struct qib_devdata *dd = qib_lookup(ndev);
			if (dd) {
				ret = choose_port_ctxt(fp, dd, port, uinfo);
				if (!ret)
					goto done;
				if (ret == -EBUSY)
					dusable++;
			}
		}
	}
	ret = dusable ? -EBUSY : -ENETDOWN;

done:
	return ret;
}

static int find_shared_ctxt(struct file *fp,
			    const struct qib_user_info *uinfo)
{
	int devmax, ndev, i;
	int ret = 0;

	devmax = qib_count_units(NULL, NULL);

	for (ndev = 0; ndev < devmax; ndev++) {
		struct qib_devdata *dd = qib_lookup(ndev);

		/* device portion of usable() */
		if (!(dd && (dd->flags & QIB_PRESENT) && dd->kregbase))
			continue;
		for (i = dd->first_user_ctxt; i < dd->cfgctxts; i++) {
			struct qib_ctxtdata *rcd = dd->rcd[i];

			/* Skip ctxts which are not yet open */
			if (!rcd || !rcd->cnt)
				continue;
			/* Skip ctxt if it doesn't match the requested one */
			if (rcd->subctxt_id != uinfo->spu_subctxt_id)
				continue;
			/* Verify the sharing process matches the master */
			if (rcd->subctxt_cnt != uinfo->spu_subctxt_cnt ||
			    rcd->userversion != uinfo->spu_userversion ||
			    rcd->cnt >= rcd->subctxt_cnt) {
				ret = -EINVAL;
				goto done;
			}
			ctxt_fp(fp) = rcd;
			subctxt_fp(fp) = rcd->cnt++;
			rcd->subpid[subctxt_fp(fp)] = current->pid;
			tidcursor_fp(fp) = 0;
			rcd->active_slaves |= 1 << subctxt_fp(fp);
			ret = 1;
			goto done;
		}
	}

done:
	return ret;
}

static int qib_open(struct inode *in, struct file *fp)
{
	/* The real work is performed later in qib_assign_ctxt() */
	fp->private_data = kzalloc(sizeof(struct qib_filedata), GFP_KERNEL);
	if (fp->private_data) /* no cpu affinity by default */
		((struct qib_filedata *)fp->private_data)->rec_cpu_num = -1;
	return fp->private_data ? 0 : -ENOMEM;
}

/*
 * Get ctxt early, so can set affinity prior to memory allocation.
 */
static int qib_assign_ctxt(struct file *fp, const struct qib_user_info *uinfo)
{
	int ret;
	int i_minor;
	unsigned swmajor, swminor, alg = QIB_PORT_ALG_ACROSS;

	/* Check to be sure we haven't already initialized this file */
	if (ctxt_fp(fp)) {
		ret = -EINVAL;
		goto done;
	}

	/* for now, if major version is different, bail */
	swmajor = uinfo->spu_userversion >> 16;
	if (swmajor != QIB_USER_SWMAJOR) {
		ret = -ENODEV;
		goto done;
	}

	swminor = uinfo->spu_userversion & 0xffff;

	if (swminor >= 11 && uinfo->spu_port_alg < QIB_PORT_ALG_COUNT)
		alg = uinfo->spu_port_alg;

	mutex_lock(&qib_mutex);

	if (qib_compatible_subctxts(swmajor, swminor) &&
	    uinfo->spu_subctxt_cnt) {
		ret = find_shared_ctxt(fp, uinfo);
		if (ret) {
			if (ret > 0)
				ret = 0;
			goto done_chk_sdma;
		}
	}

	i_minor = iminor(fp->f_dentry->d_inode) - QIB_USER_MINOR_BASE;
	if (i_minor)
		ret = find_free_ctxt(i_minor - 1, fp, uinfo);
	else
		ret = get_a_ctxt(fp, uinfo, alg);

done_chk_sdma:
	if (!ret) {
		struct qib_filedata *fd = fp->private_data;
		const struct qib_ctxtdata *rcd = fd->rcd;
		const struct qib_devdata *dd = rcd->dd;
		unsigned int weight;

		if (dd->flags & QIB_HAS_SEND_DMA) {
			fd->pq = qib_user_sdma_queue_create(&dd->pcidev->dev,
							    dd->unit,
							    rcd->ctxt,
							    fd->subctxt);
			if (!fd->pq)
				ret = -ENOMEM;
		}

		/*
		 * If process has NOT already set it's affinity, select and
		 * reserve a processor for it, as a rendezvous for all
		 * users of the driver.  If they don't actually later
		 * set affinity to this cpu, or set it to some other cpu,
		 * it just means that sooner or later we don't recommend
		 * a cpu, and let the scheduler do it's best.
		 */
		weight = cpumask_weight(tsk_cpus_allowed(current));
		if (!ret && weight >= qib_cpulist_count) {
			int cpu;
			cpu = find_first_zero_bit(qib_cpulist,
						  qib_cpulist_count);
			if (cpu != qib_cpulist_count) {
				__set_bit(cpu, qib_cpulist);
				fd->rec_cpu_num = cpu;
			}
		} else if (weight == 1 &&
			test_bit(cpumask_first(tsk_cpus_allowed(current)),
				 qib_cpulist))
			qib_devinfo(dd->pcidev, "%s PID %u affinity "
				    "set to cpu %d; already allocated\n",
				    current->comm, current->pid,
				    cpumask_first(tsk_cpus_allowed(current)));
	}

	mutex_unlock(&qib_mutex);

done:
	return ret;
}


static int qib_do_user_init(struct file *fp,
			    const struct qib_user_info *uinfo)
{
	int ret;
	struct qib_ctxtdata *rcd = ctxt_fp(fp);
	struct qib_devdata *dd;
	unsigned uctxt;

	/* Subctxts don't need to initialize anything since master did it. */
	if (subctxt_fp(fp)) {
		ret = wait_event_interruptible(rcd->wait,
			!test_bit(QIB_CTXT_MASTER_UNINIT, &rcd->flag));
		goto bail;
	}

	dd = rcd->dd;

	/* some ctxts may get extra buffers, calculate that here */
	uctxt = rcd->ctxt - dd->first_user_ctxt;
	if (uctxt < dd->ctxts_extrabuf) {
		rcd->piocnt = dd->pbufsctxt + 1;
		rcd->pio_base = rcd->piocnt * uctxt;
	} else {
		rcd->piocnt = dd->pbufsctxt;
		rcd->pio_base = rcd->piocnt * uctxt +
			dd->ctxts_extrabuf;
	}

	/*
	 * All user buffers are 2KB buffers.  If we ever support
	 * giving 4KB buffers to user processes, this will need some
	 * work.  Can't use piobufbase directly, because it has
	 * both 2K and 4K buffer base values.  So check and handle.
	 */
	if ((rcd->pio_base + rcd->piocnt) > dd->piobcnt2k) {
		if (rcd->pio_base >= dd->piobcnt2k) {
			qib_dev_err(dd,
				    "%u:ctxt%u: no 2KB buffers available\n",
				    dd->unit, rcd->ctxt);
			ret = -ENOBUFS;
			goto bail;
		}
		rcd->piocnt = dd->piobcnt2k - rcd->pio_base;
		qib_dev_err(dd, "Ctxt%u: would use 4KB bufs, using %u\n",
			    rcd->ctxt, rcd->piocnt);
	}

	rcd->piobufs = dd->pio2k_bufbase + rcd->pio_base * dd->palign;
	qib_chg_pioavailkernel(dd, rcd->pio_base, rcd->piocnt,
			       TXCHK_CHG_TYPE_USER, rcd);
	/*
	 * try to ensure that processes start up with consistent avail update
	 * for their own range, at least.   If system very quiet, it might
	 * have the in-memory copy out of date at startup for this range of
	 * buffers, when a context gets re-used.  Do after the chg_pioavail
	 * and before the rest of setup, so it's "almost certain" the dma
	 * will have occurred (can't 100% guarantee, but should be many
	 * decimals of 9s, with this ordering), given how much else happens
	 * after this.
	 */
	dd->f_sendctrl(dd->pport, QIB_SENDCTRL_AVAIL_BLIP);

	/*
	 * Now allocate the rcvhdr Q and eager TIDs; skip the TID
	 * array for time being.  If rcd->ctxt > chip-supported,
	 * we need to do extra stuff here to handle by handling overflow
	 * through ctxt 0, someday
	 */
	ret = qib_create_rcvhdrq(dd, rcd);
	if (!ret)
		ret = qib_setup_eagerbufs(rcd);
	if (ret)
		goto bail_pio;

	rcd->tidcursor = 0; /* start at beginning after open */

	/* initialize poll variables... */
	rcd->urgent = 0;
	rcd->urgent_poll = 0;

	/*
	 * Now enable the ctxt for receive.
	 * For chips that are set to DMA the tail register to memory
	 * when they change (and when the update bit transitions from
	 * 0 to 1.  So for those chips, we turn it off and then back on.
	 * This will (very briefly) affect any other open ctxts, but the
	 * duration is very short, and therefore isn't an issue.  We
	 * explicitly set the in-memory tail copy to 0 beforehand, so we
	 * don't have to wait to be sure the DMA update has happened
	 * (chip resets head/tail to 0 on transition to enable).
	 */
	if (rcd->rcvhdrtail_kvaddr)
		qib_clear_rcvhdrtail(rcd);

	dd->f_rcvctrl(rcd->ppd, QIB_RCVCTRL_CTXT_ENB | QIB_RCVCTRL_TIDFLOW_ENB,
		      rcd->ctxt);

	/* Notify any waiting slaves */
	if (rcd->subctxt_cnt) {
		clear_bit(QIB_CTXT_MASTER_UNINIT, &rcd->flag);
		wake_up(&rcd->wait);
	}
	return 0;

bail_pio:
	qib_chg_pioavailkernel(dd, rcd->pio_base, rcd->piocnt,
			       TXCHK_CHG_TYPE_KERN, rcd);
bail:
	return ret;
}

/**
 * unlock_exptid - unlock any expected TID entries context still had in use
 * @rcd: ctxt
 *
 * We don't actually update the chip here, because we do a bulk update
 * below, using f_clear_tids.
 */
static void unlock_expected_tids(struct qib_ctxtdata *rcd)
{
	struct qib_devdata *dd = rcd->dd;
	int ctxt_tidbase = rcd->ctxt * dd->rcvtidcnt;
	int i, cnt = 0, maxtid = ctxt_tidbase + dd->rcvtidcnt;

	for (i = ctxt_tidbase; i < maxtid; i++) {
		struct page *p = dd->pageshadow[i];
		dma_addr_t phys;

		if (!p)
			continue;

		phys = dd->physshadow[i];
		dd->physshadow[i] = dd->tidinvalid;
		dd->pageshadow[i] = NULL;
		pci_unmap_page(dd->pcidev, phys, PAGE_SIZE,
			       PCI_DMA_FROMDEVICE);
		qib_release_user_pages(&p, 1);
		cnt++;
	}
}

static int qib_close(struct inode *in, struct file *fp)
{
	int ret = 0;
	struct qib_filedata *fd;
	struct qib_ctxtdata *rcd;
	struct qib_devdata *dd;
	unsigned long flags;
	unsigned ctxt;
	pid_t pid;

	mutex_lock(&qib_mutex);

	fd = fp->private_data;
	fp->private_data = NULL;
	rcd = fd->rcd;
	if (!rcd) {
		mutex_unlock(&qib_mutex);
		goto bail;
	}

	dd = rcd->dd;

	/* ensure all pio buffer writes in progress are flushed */
	qib_flush_wc();

	/* drain user sdma queue */
	if (fd->pq) {
		qib_user_sdma_queue_drain(rcd->ppd, fd->pq);
		qib_user_sdma_queue_destroy(fd->pq);
	}

	if (fd->rec_cpu_num != -1)
		__clear_bit(fd->rec_cpu_num, qib_cpulist);

	if (--rcd->cnt) {
		/*
		 * XXX If the master closes the context before the slave(s),
		 * revoke the mmap for the eager receive queue so
		 * the slave(s) don't wait for receive data forever.
		 */
		rcd->active_slaves &= ~(1 << fd->subctxt);
		rcd->subpid[fd->subctxt] = 0;
		mutex_unlock(&qib_mutex);
		goto bail;
	}

	/* early; no interrupt users after this */
	spin_lock_irqsave(&dd->uctxt_lock, flags);
	ctxt = rcd->ctxt;
	dd->rcd[ctxt] = NULL;
	pid = rcd->pid;
	rcd->pid = 0;
	spin_unlock_irqrestore(&dd->uctxt_lock, flags);

	if (rcd->rcvwait_to || rcd->piowait_to ||
	    rcd->rcvnowait || rcd->pionowait) {
		rcd->rcvwait_to = 0;
		rcd->piowait_to = 0;
		rcd->rcvnowait = 0;
		rcd->pionowait = 0;
	}
	if (rcd->flag)
		rcd->flag = 0;

	if (dd->kregbase) {
		/* atomically clear receive enable ctxt and intr avail. */
		dd->f_rcvctrl(rcd->ppd, QIB_RCVCTRL_CTXT_DIS |
				  QIB_RCVCTRL_INTRAVAIL_DIS, ctxt);

		/* clean up the pkeys for this ctxt user */
		qib_clean_part_key(rcd, dd);
		qib_disarm_piobufs(dd, rcd->pio_base, rcd->piocnt);
		qib_chg_pioavailkernel(dd, rcd->pio_base,
				       rcd->piocnt, TXCHK_CHG_TYPE_KERN, NULL);

		dd->f_clear_tids(dd, rcd);

		if (dd->pageshadow)
			unlock_expected_tids(rcd);
		qib_stats.sps_ctxts--;
		dd->freectxts--;
	}

	mutex_unlock(&qib_mutex);
	qib_free_ctxtdata(dd, rcd); /* after releasing the mutex */

bail:
	kfree(fd);
	return ret;
}

static int qib_ctxt_info(struct file *fp, struct qib_ctxt_info __user *uinfo)
{
	struct qib_ctxt_info info;
	int ret;
	size_t sz;
	struct qib_ctxtdata *rcd = ctxt_fp(fp);
	struct qib_filedata *fd;

	fd = fp->private_data;

	info.num_active = qib_count_active_units();
	info.unit = rcd->dd->unit;
	info.port = rcd->ppd->port;
	info.ctxt = rcd->ctxt;
	info.subctxt =  subctxt_fp(fp);
	/* Number of user ctxts available for this device. */
	info.num_ctxts = rcd->dd->cfgctxts - rcd->dd->first_user_ctxt;
	info.num_subctxts = rcd->subctxt_cnt;
	info.rec_cpu = fd->rec_cpu_num;
	sz = sizeof(info);

	if (copy_to_user(uinfo, &info, sz)) {
		ret = -EFAULT;
		goto bail;
	}
	ret = 0;

bail:
	return ret;
}

static int qib_sdma_get_inflight(struct qib_user_sdma_queue *pq,
				 u32 __user *inflightp)
{
	const u32 val = qib_user_sdma_inflight_counter(pq);

	if (put_user(val, inflightp))
		return -EFAULT;

	return 0;
}

static int qib_sdma_get_complete(struct qib_pportdata *ppd,
				 struct qib_user_sdma_queue *pq,
				 u32 __user *completep)
{
	u32 val;
	int err;

	if (!pq)
		return -EINVAL;

	err = qib_user_sdma_make_progress(ppd, pq);
	if (err < 0)
		return err;

	val = qib_user_sdma_complete_counter(pq);
	if (put_user(val, completep))
		return -EFAULT;

	return 0;
}

static int disarm_req_delay(struct qib_ctxtdata *rcd)
{
	int ret = 0;

	if (!usable(rcd->ppd)) {
		int i;
		/*
		 * if link is down, or otherwise not usable, delay
		 * the caller up to 30 seconds, so we don't thrash
		 * in trying to get the chip back to ACTIVE, and
		 * set flag so they make the call again.
		 */
		if (rcd->user_event_mask) {
			/*
			 * subctxt_cnt is 0 if not shared, so do base
			 * separately, first, then remaining subctxt, if any
			 */
			set_bit(_QIB_EVENT_DISARM_BUFS_BIT,
				&rcd->user_event_mask[0]);
			for (i = 1; i < rcd->subctxt_cnt; i++)
				set_bit(_QIB_EVENT_DISARM_BUFS_BIT,
					&rcd->user_event_mask[i]);
		}
		for (i = 0; !usable(rcd->ppd) && i < 300; i++)
			msleep(100);
		ret = -ENETDOWN;
	}
	return ret;
}

/*
 * Find all user contexts in use, and set the specified bit in their
 * event mask.
 * See also find_ctxt() for a similar use, that is specific to send buffers.
 */
int qib_set_uevent_bits(struct qib_pportdata *ppd, const int evtbit)
{
	struct qib_ctxtdata *rcd;
	unsigned ctxt;
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&ppd->dd->uctxt_lock, flags);
	for (ctxt = ppd->dd->first_user_ctxt; ctxt < ppd->dd->cfgctxts;
	     ctxt++) {
		rcd = ppd->dd->rcd[ctxt];
		if (!rcd)
			continue;
		if (rcd->user_event_mask) {
			int i;
			/*
			 * subctxt_cnt is 0 if not shared, so do base
			 * separately, first, then remaining subctxt, if any
			 */
			set_bit(evtbit, &rcd->user_event_mask[0]);
			for (i = 1; i < rcd->subctxt_cnt; i++)
				set_bit(evtbit, &rcd->user_event_mask[i]);
		}
		ret = 1;
		break;
	}
	spin_unlock_irqrestore(&ppd->dd->uctxt_lock, flags);

	return ret;
}

/*
 * clear the event notifier events for this context.
 * For the DISARM_BUFS case, we also take action (this obsoletes
 * the older QIB_CMD_DISARM_BUFS, but we keep it for backwards
 * compatibility.
 * Other bits don't currently require actions, just atomically clear.
 * User process then performs actions appropriate to bit having been
 * set, if desired, and checks again in future.
 */
static int qib_user_event_ack(struct qib_ctxtdata *rcd, int subctxt,
			      unsigned long events)
{
	int ret = 0, i;

	for (i = 0; i <= _QIB_MAX_EVENT_BIT; i++) {
		if (!test_bit(i, &events))
			continue;
		if (i == _QIB_EVENT_DISARM_BUFS_BIT) {
			(void)qib_disarm_piobufs_ifneeded(rcd);
			ret = disarm_req_delay(rcd);
		} else
			clear_bit(i, &rcd->user_event_mask[subctxt]);
	}
	return ret;
}

static ssize_t qib_write(struct file *fp, const char __user *data,
			 size_t count, loff_t *off)
{
	const struct qib_cmd __user *ucmd;
	struct qib_ctxtdata *rcd;
	const void __user *src;
	size_t consumed, copy = 0;
	struct qib_cmd cmd;
	ssize_t ret = 0;
	void *dest;

	if (count < sizeof(cmd.type)) {
		ret = -EINVAL;
		goto bail;
	}

	ucmd = (const struct qib_cmd __user *) data;

	if (copy_from_user(&cmd.type, &ucmd->type, sizeof(cmd.type))) {
		ret = -EFAULT;
		goto bail;
	}

	consumed = sizeof(cmd.type);

	switch (cmd.type) {
	case QIB_CMD_ASSIGN_CTXT:
	case QIB_CMD_USER_INIT:
		copy = sizeof(cmd.cmd.user_info);
		dest = &cmd.cmd.user_info;
		src = &ucmd->cmd.user_info;
		break;

	case QIB_CMD_RECV_CTRL:
		copy = sizeof(cmd.cmd.recv_ctrl);
		dest = &cmd.cmd.recv_ctrl;
		src = &ucmd->cmd.recv_ctrl;
		break;

	case QIB_CMD_CTXT_INFO:
		copy = sizeof(cmd.cmd.ctxt_info);
		dest = &cmd.cmd.ctxt_info;
		src = &ucmd->cmd.ctxt_info;
		break;

	case QIB_CMD_TID_UPDATE:
	case QIB_CMD_TID_FREE:
		copy = sizeof(cmd.cmd.tid_info);
		dest = &cmd.cmd.tid_info;
		src = &ucmd->cmd.tid_info;
		break;

	case QIB_CMD_SET_PART_KEY:
		copy = sizeof(cmd.cmd.part_key);
		dest = &cmd.cmd.part_key;
		src = &ucmd->cmd.part_key;
		break;

	case QIB_CMD_DISARM_BUFS:
	case QIB_CMD_PIOAVAILUPD: /* force an update of PIOAvail reg */
		copy = 0;
		src = NULL;
		dest = NULL;
		break;

	case QIB_CMD_POLL_TYPE:
		copy = sizeof(cmd.cmd.poll_type);
		dest = &cmd.cmd.poll_type;
		src = &ucmd->cmd.poll_type;
		break;

	case QIB_CMD_ARMLAUNCH_CTRL:
		copy = sizeof(cmd.cmd.armlaunch_ctrl);
		dest = &cmd.cmd.armlaunch_ctrl;
		src = &ucmd->cmd.armlaunch_ctrl;
		break;

	case QIB_CMD_SDMA_INFLIGHT:
		copy = sizeof(cmd.cmd.sdma_inflight);
		dest = &cmd.cmd.sdma_inflight;
		src = &ucmd->cmd.sdma_inflight;
		break;

	case QIB_CMD_SDMA_COMPLETE:
		copy = sizeof(cmd.cmd.sdma_complete);
		dest = &cmd.cmd.sdma_complete;
		src = &ucmd->cmd.sdma_complete;
		break;

	case QIB_CMD_ACK_EVENT:
		copy = sizeof(cmd.cmd.event_mask);
		dest = &cmd.cmd.event_mask;
		src = &ucmd->cmd.event_mask;
		break;

	default:
		ret = -EINVAL;
		goto bail;
	}

	if (copy) {
		if ((count - consumed) < copy) {
			ret = -EINVAL;
			goto bail;
		}
		if (copy_from_user(dest, src, copy)) {
			ret = -EFAULT;
			goto bail;
		}
		consumed += copy;
	}

	rcd = ctxt_fp(fp);
	if (!rcd && cmd.type != QIB_CMD_ASSIGN_CTXT) {
		ret = -EINVAL;
		goto bail;
	}

	switch (cmd.type) {
	case QIB_CMD_ASSIGN_CTXT:
		ret = qib_assign_ctxt(fp, &cmd.cmd.user_info);
		if (ret)
			goto bail;
		break;

	case QIB_CMD_USER_INIT:
		ret = qib_do_user_init(fp, &cmd.cmd.user_info);
		if (ret)
			goto bail;
		ret = qib_get_base_info(fp, (void __user *) (unsigned long)
					cmd.cmd.user_info.spu_base_info,
					cmd.cmd.user_info.spu_base_info_size);
		break;

	case QIB_CMD_RECV_CTRL:
		ret = qib_manage_rcvq(rcd, subctxt_fp(fp), cmd.cmd.recv_ctrl);
		break;

	case QIB_CMD_CTXT_INFO:
		ret = qib_ctxt_info(fp, (struct qib_ctxt_info __user *)
				    (unsigned long) cmd.cmd.ctxt_info);
		break;

	case QIB_CMD_TID_UPDATE:
		ret = qib_tid_update(rcd, fp, &cmd.cmd.tid_info);
		break;

	case QIB_CMD_TID_FREE:
		ret = qib_tid_free(rcd, subctxt_fp(fp), &cmd.cmd.tid_info);
		break;

	case QIB_CMD_SET_PART_KEY:
		ret = qib_set_part_key(rcd, cmd.cmd.part_key);
		break;

	case QIB_CMD_DISARM_BUFS:
		(void)qib_disarm_piobufs_ifneeded(rcd);
		ret = disarm_req_delay(rcd);
		break;

	case QIB_CMD_PIOAVAILUPD:
		qib_force_pio_avail_update(rcd->dd);
		break;

	case QIB_CMD_POLL_TYPE:
		rcd->poll_type = cmd.cmd.poll_type;
		break;

	case QIB_CMD_ARMLAUNCH_CTRL:
		rcd->dd->f_set_armlaunch(rcd->dd, cmd.cmd.armlaunch_ctrl);
		break;

	case QIB_CMD_SDMA_INFLIGHT:
		ret = qib_sdma_get_inflight(user_sdma_queue_fp(fp),
					    (u32 __user *) (unsigned long)
					    cmd.cmd.sdma_inflight);
		break;

	case QIB_CMD_SDMA_COMPLETE:
		ret = qib_sdma_get_complete(rcd->ppd,
					    user_sdma_queue_fp(fp),
					    (u32 __user *) (unsigned long)
					    cmd.cmd.sdma_complete);
		break;

	case QIB_CMD_ACK_EVENT:
		ret = qib_user_event_ack(rcd, subctxt_fp(fp),
					 cmd.cmd.event_mask);
		break;
	}

	if (ret >= 0)
		ret = consumed;

bail:
	return ret;
}

static ssize_t qib_aio_write(struct kiocb *iocb, const struct iovec *iov,
			     unsigned long dim, loff_t off)
{
	struct qib_filedata *fp = iocb->ki_filp->private_data;
	struct qib_ctxtdata *rcd = ctxt_fp(iocb->ki_filp);
	struct qib_user_sdma_queue *pq = fp->pq;

	if (!dim || !pq)
		return -EINVAL;

	return qib_user_sdma_writev(rcd, pq, iov, dim);
}

static struct class *qib_class;
static dev_t qib_dev;

int qib_cdev_init(int minor, const char *name,
		  const struct file_operations *fops,
		  struct cdev **cdevp, struct device **devp)
{
	const dev_t dev = MKDEV(MAJOR(qib_dev), minor);
	struct cdev *cdev;
	struct device *device = NULL;
	int ret;

	cdev = cdev_alloc();
	if (!cdev) {
		printk(KERN_ERR QIB_DRV_NAME
		       ": Could not allocate cdev for minor %d, %s\n",
		       minor, name);
		ret = -ENOMEM;
		goto done;
	}

	cdev->owner = THIS_MODULE;
	cdev->ops = fops;
	kobject_set_name(&cdev->kobj, name);

	ret = cdev_add(cdev, dev, 1);
	if (ret < 0) {
		printk(KERN_ERR QIB_DRV_NAME
		       ": Could not add cdev for minor %d, %s (err %d)\n",
		       minor, name, -ret);
		goto err_cdev;
	}

	device = device_create(qib_class, NULL, dev, NULL, name);
	if (!IS_ERR(device))
		goto done;
	ret = PTR_ERR(device);
	device = NULL;
	printk(KERN_ERR QIB_DRV_NAME ": Could not create "
	       "device for minor %d, %s (err %d)\n",
	       minor, name, -ret);
err_cdev:
	cdev_del(cdev);
	cdev = NULL;
done:
	*cdevp = cdev;
	*devp = device;
	return ret;
}

void qib_cdev_cleanup(struct cdev **cdevp, struct device **devp)
{
	struct device *device = *devp;

	if (device) {
		device_unregister(device);
		*devp = NULL;
	}

	if (*cdevp) {
		cdev_del(*cdevp);
		*cdevp = NULL;
	}
}

static struct cdev *wildcard_cdev;
static struct device *wildcard_device;

int __init qib_dev_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&qib_dev, 0, QIB_NMINORS, QIB_DRV_NAME);
	if (ret < 0) {
		printk(KERN_ERR QIB_DRV_NAME ": Could not allocate "
		       "chrdev region (err %d)\n", -ret);
		goto done;
	}

	qib_class = class_create(THIS_MODULE, "ipath");
	if (IS_ERR(qib_class)) {
		ret = PTR_ERR(qib_class);
		printk(KERN_ERR QIB_DRV_NAME ": Could not create "
		       "device class (err %d)\n", -ret);
		unregister_chrdev_region(qib_dev, QIB_NMINORS);
	}

done:
	return ret;
}

void qib_dev_cleanup(void)
{
	if (qib_class) {
		class_destroy(qib_class);
		qib_class = NULL;
	}

	unregister_chrdev_region(qib_dev, QIB_NMINORS);
}

static atomic_t user_count = ATOMIC_INIT(0);

static void qib_user_remove(struct qib_devdata *dd)
{
	if (atomic_dec_return(&user_count) == 0)
		qib_cdev_cleanup(&wildcard_cdev, &wildcard_device);

	qib_cdev_cleanup(&dd->user_cdev, &dd->user_device);
}

static int qib_user_add(struct qib_devdata *dd)
{
	char name[10];
	int ret;

	if (atomic_inc_return(&user_count) == 1) {
		ret = qib_cdev_init(0, "ipath", &qib_file_ops,
				    &wildcard_cdev, &wildcard_device);
		if (ret)
			goto done;
	}

	snprintf(name, sizeof(name), "ipath%d", dd->unit);
	ret = qib_cdev_init(dd->unit + 1, name, &qib_file_ops,
			    &dd->user_cdev, &dd->user_device);
	if (ret)
		qib_user_remove(dd);
done:
	return ret;
}

/*
 * Create per-unit files in /dev
 */
int qib_device_create(struct qib_devdata *dd)
{
	int r, ret;

	r = qib_user_add(dd);
	ret = qib_diag_add(dd);
	if (r && !ret)
		ret = r;
	return ret;
}

/*
 * Remove per-unit files in /dev
 * void, core kernel returns no errors for this stuff
 */
void qib_device_remove(struct qib_devdata *dd)
{
	qib_user_remove(dd);
	qib_diag_remove(dd);
}

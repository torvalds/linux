/*
 * Copyright (c) 2006, 2007 QLogic Corporation. All rights reserved.
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
#include <asm/pgtable.h>

#include "ipath_kernel.h"
#include "ipath_common.h"

static int ipath_open(struct inode *, struct file *);
static int ipath_close(struct inode *, struct file *);
static ssize_t ipath_write(struct file *, const char __user *, size_t,
			   loff_t *);
static unsigned int ipath_poll(struct file *, struct poll_table_struct *);
static int ipath_mmap(struct file *, struct vm_area_struct *);

static const struct file_operations ipath_file_ops = {
	.owner = THIS_MODULE,
	.write = ipath_write,
	.open = ipath_open,
	.release = ipath_close,
	.poll = ipath_poll,
	.mmap = ipath_mmap
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

static int ipath_get_base_info(struct file *fp,
			       void __user *ubase, size_t ubase_size)
{
	struct ipath_portdata *pd = port_fp(fp);
	int ret = 0;
	struct ipath_base_info *kinfo = NULL;
	struct ipath_devdata *dd = pd->port_dd;
	unsigned subport_cnt;
	int shared, master;
	size_t sz;

	subport_cnt = pd->port_subport_cnt;
	if (!subport_cnt) {
		shared = 0;
		master = 0;
		subport_cnt = 1;
	} else {
		shared = 1;
		master = !subport_fp(fp);
	}

	sz = sizeof(*kinfo);
	/* If port sharing is not requested, allow the old size structure */
	if (!shared)
		sz -= 7 * sizeof(u64);
	if (ubase_size < sz) {
		ipath_cdbg(PROC,
			   "Base size %zu, need %zu (version mismatch?)\n",
			   ubase_size, sz);
		ret = -EINVAL;
		goto bail;
	}

	kinfo = kzalloc(sizeof(*kinfo), GFP_KERNEL);
	if (kinfo == NULL) {
		ret = -ENOMEM;
		goto bail;
	}

	ret = dd->ipath_f_get_base_info(pd, kinfo);
	if (ret < 0)
		goto bail;

	kinfo->spi_rcvhdr_cnt = dd->ipath_rcvhdrcnt;
	kinfo->spi_rcvhdrent_size = dd->ipath_rcvhdrentsize;
	kinfo->spi_tidegrcnt = dd->ipath_rcvegrcnt;
	kinfo->spi_rcv_egrbufsize = dd->ipath_rcvegrbufsize;
	/*
	 * have to mmap whole thing
	 */
	kinfo->spi_rcv_egrbuftotlen =
		pd->port_rcvegrbuf_chunks * pd->port_rcvegrbuf_size;
	kinfo->spi_rcv_egrperchunk = pd->port_rcvegrbufs_perchunk;
	kinfo->spi_rcv_egrchunksize = kinfo->spi_rcv_egrbuftotlen /
		pd->port_rcvegrbuf_chunks;
	kinfo->spi_tidcnt = dd->ipath_rcvtidcnt / subport_cnt;
	if (master)
		kinfo->spi_tidcnt += dd->ipath_rcvtidcnt % subport_cnt;
	/*
	 * for this use, may be ipath_cfgports summed over all chips that
	 * are are configured and present
	 */
	kinfo->spi_nports = dd->ipath_cfgports;
	/* unit (chip/board) our port is on */
	kinfo->spi_unit = dd->ipath_unit;
	/* for now, only a single page */
	kinfo->spi_tid_maxsize = PAGE_SIZE;

	/*
	 * Doing this per port, and based on the skip value, etc.  This has
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
	 */
	kinfo->spi_rcvhdr_base = (u64) pd->port_rcvhdrq_phys;
	kinfo->spi_rcvhdr_tailaddr = (u64) pd->port_rcvhdrqtailaddr_phys;
	kinfo->spi_rcv_egrbufs = (u64) pd->port_rcvegr_phys;
	kinfo->spi_pioavailaddr = (u64) dd->ipath_pioavailregs_phys;
	kinfo->spi_status = (u64) kinfo->spi_pioavailaddr +
		(void *) dd->ipath_statusp -
		(void *) dd->ipath_pioavailregs_dma;
	if (!shared) {
		kinfo->spi_piocnt = dd->ipath_pbufsport;
		kinfo->spi_piobufbase = (u64) pd->port_piobufs;
		kinfo->__spi_uregbase = (u64) dd->ipath_uregbase +
			dd->ipath_palign * pd->port_port;
	} else if (master) {
		kinfo->spi_piocnt = (dd->ipath_pbufsport / subport_cnt) +
				    (dd->ipath_pbufsport % subport_cnt);
		/* Master's PIO buffers are after all the slave's */
		kinfo->spi_piobufbase = (u64) pd->port_piobufs +
			dd->ipath_palign *
			(dd->ipath_pbufsport - kinfo->spi_piocnt);
	} else {
		unsigned slave = subport_fp(fp) - 1;

		kinfo->spi_piocnt = dd->ipath_pbufsport / subport_cnt;
		kinfo->spi_piobufbase = (u64) pd->port_piobufs +
			dd->ipath_palign * kinfo->spi_piocnt * slave;
	}
	if (shared) {
		kinfo->spi_port_uregbase = (u64) dd->ipath_uregbase +
			dd->ipath_palign * pd->port_port;
		kinfo->spi_port_rcvegrbuf = kinfo->spi_rcv_egrbufs;
		kinfo->spi_port_rcvhdr_base = kinfo->spi_rcvhdr_base;
		kinfo->spi_port_rcvhdr_tailaddr = kinfo->spi_rcvhdr_tailaddr;

		kinfo->__spi_uregbase = cvt_kvaddr(pd->subport_uregbase +
			PAGE_SIZE * subport_fp(fp));

		kinfo->spi_rcvhdr_base = cvt_kvaddr(pd->subport_rcvhdr_base +
			pd->port_rcvhdrq_size * subport_fp(fp));
		kinfo->spi_rcvhdr_tailaddr = 0;
		kinfo->spi_rcv_egrbufs = cvt_kvaddr(pd->subport_rcvegrbuf +
			pd->port_rcvegrbuf_chunks * pd->port_rcvegrbuf_size *
			subport_fp(fp));

		kinfo->spi_subport_uregbase =
			cvt_kvaddr(pd->subport_uregbase);
		kinfo->spi_subport_rcvegrbuf =
			cvt_kvaddr(pd->subport_rcvegrbuf);
		kinfo->spi_subport_rcvhdr_base =
			cvt_kvaddr(pd->subport_rcvhdr_base);
		ipath_cdbg(PROC, "port %u flags %x %llx %llx %llx\n",
			kinfo->spi_port, kinfo->spi_runtime_flags,
			(unsigned long long) kinfo->spi_subport_uregbase,
			(unsigned long long) kinfo->spi_subport_rcvegrbuf,
			(unsigned long long) kinfo->spi_subport_rcvhdr_base);
	}

	kinfo->spi_pioindex = (kinfo->spi_piobufbase - dd->ipath_piobufbase) /
		dd->ipath_palign;
	kinfo->spi_pioalign = dd->ipath_palign;

	kinfo->spi_qpair = IPATH_KD_QP;
	kinfo->spi_piosize = dd->ipath_ibmaxlen;
	kinfo->spi_mtu = dd->ipath_ibmaxlen;	/* maxlen, not ibmtu */
	kinfo->spi_port = pd->port_port;
	kinfo->spi_subport = subport_fp(fp);
	kinfo->spi_sw_version = IPATH_KERN_SWVERSION;
	kinfo->spi_hw_version = dd->ipath_revision;

	if (master) {
		kinfo->spi_runtime_flags |= IPATH_RUNTIME_MASTER;
	}

	sz = (ubase_size < sizeof(*kinfo)) ? ubase_size : sizeof(*kinfo);
	if (copy_to_user(ubase, kinfo, sz))
		ret = -EFAULT;

bail:
	kfree(kinfo);
	return ret;
}

/**
 * ipath_tid_update - update a port TID
 * @pd: the port
 * @fp: the ipath device file
 * @ti: the TID information
 *
 * The new implementation as of Oct 2004 is that the driver assigns
 * the tid and returns it to the caller.   To make it easier to
 * catch bugs, and to reduce search time, we keep a cursor for
 * each port, walking the shadow tid array to find one that's not
 * in use.
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
static int ipath_tid_update(struct ipath_portdata *pd, struct file *fp,
			    const struct ipath_tid_info *ti)
{
	int ret = 0, ntids;
	u32 tid, porttid, cnt, i, tidcnt, tidoff;
	u16 *tidlist;
	struct ipath_devdata *dd = pd->port_dd;
	u64 physaddr;
	unsigned long vaddr;
	u64 __iomem *tidbase;
	unsigned long tidmap[8];
	struct page **pagep = NULL;
	unsigned subport = subport_fp(fp);

	if (!dd->ipath_pageshadow) {
		ret = -ENOMEM;
		goto done;
	}

	cnt = ti->tidcnt;
	if (!cnt) {
		ipath_dbg("After copyin, tidcnt 0, tidlist %llx\n",
			  (unsigned long long) ti->tidlist);
		/*
		 * Should we treat as success?  likely a bug
		 */
		ret = -EFAULT;
		goto done;
	}
	porttid = pd->port_port * dd->ipath_rcvtidcnt;
	if (!pd->port_subport_cnt) {
		tidcnt = dd->ipath_rcvtidcnt;
		tid = pd->port_tidcursor;
		tidoff = 0;
	} else if (!subport) {
		tidcnt = (dd->ipath_rcvtidcnt / pd->port_subport_cnt) +
			 (dd->ipath_rcvtidcnt % pd->port_subport_cnt);
		tidoff = dd->ipath_rcvtidcnt - tidcnt;
		porttid += tidoff;
		tid = tidcursor_fp(fp);
	} else {
		tidcnt = dd->ipath_rcvtidcnt / pd->port_subport_cnt;
		tidoff = tidcnt * (subport - 1);
		porttid += tidoff;
		tid = tidcursor_fp(fp);
	}
	if (cnt > tidcnt) {
		/* make sure it all fits in port_tid_pg_list */
		dev_info(&dd->pcidev->dev, "Process tried to allocate %u "
			 "TIDs, only trying max (%u)\n", cnt, tidcnt);
		cnt = tidcnt;
	}
	pagep = &((struct page **) pd->port_tid_pg_list)[tidoff];
	tidlist = &((u16 *) &pagep[dd->ipath_rcvtidcnt])[tidoff];

	memset(tidmap, 0, sizeof(tidmap));
	/* before decrement; chip actual # */
	ntids = tidcnt;
	tidbase = (u64 __iomem *) (((char __iomem *) dd->ipath_kregbase) +
				   dd->ipath_rcvtidbase +
				   porttid * sizeof(*tidbase));

	ipath_cdbg(VERBOSE, "Port%u %u tids, cursor %u, tidbase %p\n",
		   pd->port_port, cnt, tid, tidbase);

	/* virtual address of first page in transfer */
	vaddr = ti->tidvaddr;
	if (!access_ok(VERIFY_WRITE, (void __user *) vaddr,
		       cnt * PAGE_SIZE)) {
		ipath_dbg("Fail vaddr %p, %u pages, !access_ok\n",
			  (void *)vaddr, cnt);
		ret = -EFAULT;
		goto done;
	}
	ret = ipath_get_user_pages(vaddr, cnt, pagep);
	if (ret) {
		if (ret == -EBUSY) {
			ipath_dbg("Failed to lock addr %p, %u pages "
				  "(already locked)\n",
				  (void *) vaddr, cnt);
			/*
			 * for now, continue, and see what happens but with
			 * the new implementation, this should never happen,
			 * unless perhaps the user has mpin'ed the pages
			 * themselves (something we need to test)
			 */
			ret = 0;
		} else {
			dev_info(&dd->pcidev->dev,
				 "Failed to lock addr %p, %u pages: "
				 "errno %d\n", (void *) vaddr, cnt, -ret);
			goto done;
		}
	}
	for (i = 0; i < cnt; i++, vaddr += PAGE_SIZE) {
		for (; ntids--; tid++) {
			if (tid == tidcnt)
				tid = 0;
			if (!dd->ipath_pageshadow[porttid + tid])
				break;
		}
		if (ntids < 0) {
			/*
			 * oops, wrapped all the way through their TIDs,
			 * and didn't have enough free; see comments at
			 * start of routine
			 */
			ipath_dbg("Not enough free TIDs for %u pages "
				  "(index %d), failing\n", cnt, i);
			i--;	/* last tidlist[i] not filled in */
			ret = -ENOMEM;
			break;
		}
		tidlist[i] = tid + tidoff;
		ipath_cdbg(VERBOSE, "Updating idx %u to TID %u, "
			   "vaddr %lx\n", i, tid + tidoff, vaddr);
		/* we "know" system pages and TID pages are same size */
		dd->ipath_pageshadow[porttid + tid] = pagep[i];
		dd->ipath_physshadow[porttid + tid] = ipath_map_page(
			dd->pcidev, pagep[i], 0, PAGE_SIZE,
			PCI_DMA_FROMDEVICE);
		/*
		 * don't need atomic or it's overhead
		 */
		__set_bit(tid, tidmap);
		physaddr = dd->ipath_physshadow[porttid + tid];
		ipath_stats.sps_pagelocks++;
		ipath_cdbg(VERBOSE,
			   "TID %u, vaddr %lx, physaddr %llx pgp %p\n",
			   tid, vaddr, (unsigned long long) physaddr,
			   pagep[i]);
		dd->ipath_f_put_tid(dd, &tidbase[tid], RCVHQ_RCV_TYPE_EXPECTED,
				    physaddr);
		/*
		 * don't check this tid in ipath_portshadow, since we
		 * just filled it in; start with the next one.
		 */
		tid++;
	}

	if (ret) {
		u32 limit;
	cleanup:
		/* jump here if copy out of updated info failed... */
		ipath_dbg("After failure (ret=%d), undo %d of %d entries\n",
			  -ret, i, cnt);
		/* same code that's in ipath_free_tid() */
		limit = sizeof(tidmap) * BITS_PER_BYTE;
		if (limit > tidcnt)
			/* just in case size changes in future */
			limit = tidcnt;
		tid = find_first_bit((const unsigned long *)tidmap, limit);
		for (; tid < limit; tid++) {
			if (!test_bit(tid, tidmap))
				continue;
			if (dd->ipath_pageshadow[porttid + tid]) {
				ipath_cdbg(VERBOSE, "Freeing TID %u\n",
					   tid);
				dd->ipath_f_put_tid(dd, &tidbase[tid],
						    RCVHQ_RCV_TYPE_EXPECTED,
						    dd->ipath_tidinvalid);
				pci_unmap_page(dd->pcidev,
					dd->ipath_physshadow[porttid + tid],
					PAGE_SIZE, PCI_DMA_FROMDEVICE);
				dd->ipath_pageshadow[porttid + tid] = NULL;
				ipath_stats.sps_pageunlocks++;
			}
		}
		ipath_release_user_pages(pagep, cnt);
	} else {
		/*
		 * Copy the updated array, with ipath_tid's filled in, back
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
		if (!pd->port_subport_cnt)
			pd->port_tidcursor = tid;
		else
			tidcursor_fp(fp) = tid;
	}

done:
	if (ret)
		ipath_dbg("Failed to map %u TID pages, failing with %d\n",
			  ti->tidcnt, -ret);
	return ret;
}

/**
 * ipath_tid_free - free a port TID
 * @pd: the port
 * @subport: the subport
 * @ti: the TID info
 *
 * right now we are unlocking one page at a time, but since
 * the intended use of this routine is for a single group of
 * virtually contiguous pages, that should change to improve
 * performance.  We check that the TID is in range for this port
 * but otherwise don't check validity; if user has an error and
 * frees the wrong tid, it's only their own data that can thereby
 * be corrupted.  We do check that the TID was in use, for sanity
 * We always use our idea of the saved address, not the address that
 * they pass in to us.
 */

static int ipath_tid_free(struct ipath_portdata *pd, unsigned subport,
			  const struct ipath_tid_info *ti)
{
	int ret = 0;
	u32 tid, porttid, cnt, limit, tidcnt;
	struct ipath_devdata *dd = pd->port_dd;
	u64 __iomem *tidbase;
	unsigned long tidmap[8];

	if (!dd->ipath_pageshadow) {
		ret = -ENOMEM;
		goto done;
	}

	if (copy_from_user(tidmap, (void __user *)(unsigned long)ti->tidmap,
			   sizeof tidmap)) {
		ret = -EFAULT;
		goto done;
	}

	porttid = pd->port_port * dd->ipath_rcvtidcnt;
	if (!pd->port_subport_cnt)
		tidcnt = dd->ipath_rcvtidcnt;
	else if (!subport) {
		tidcnt = (dd->ipath_rcvtidcnt / pd->port_subport_cnt) +
			 (dd->ipath_rcvtidcnt % pd->port_subport_cnt);
		porttid += dd->ipath_rcvtidcnt - tidcnt;
	} else {
		tidcnt = dd->ipath_rcvtidcnt / pd->port_subport_cnt;
		porttid += tidcnt * (subport - 1);
	}
	tidbase = (u64 __iomem *) ((char __iomem *)(dd->ipath_kregbase) +
				   dd->ipath_rcvtidbase +
				   porttid * sizeof(*tidbase));

	limit = sizeof(tidmap) * BITS_PER_BYTE;
	if (limit > tidcnt)
		/* just in case size changes in future */
		limit = tidcnt;
	tid = find_first_bit(tidmap, limit);
	ipath_cdbg(VERBOSE, "Port%u free %u tids; first bit (max=%d) "
		   "set is %d, porttid %u\n", pd->port_port, ti->tidcnt,
		   limit, tid, porttid);
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
		if (dd->ipath_pageshadow[porttid + tid]) {
			struct page *p;
			p = dd->ipath_pageshadow[porttid + tid];
			dd->ipath_pageshadow[porttid + tid] = NULL;
			ipath_cdbg(VERBOSE, "PID %u freeing TID %u\n",
				   pd->port_pid, tid);
			dd->ipath_f_put_tid(dd, &tidbase[tid],
					    RCVHQ_RCV_TYPE_EXPECTED,
					    dd->ipath_tidinvalid);
			pci_unmap_page(dd->pcidev,
				dd->ipath_physshadow[porttid + tid],
				PAGE_SIZE, PCI_DMA_FROMDEVICE);
			ipath_release_user_pages(&p, 1);
			ipath_stats.sps_pageunlocks++;
		} else
			ipath_dbg("Unused tid %u, ignoring\n", tid);
	}
	if (cnt != ti->tidcnt)
		ipath_dbg("passed in tidcnt %d, only %d bits set in map\n",
			  ti->tidcnt, cnt);
done:
	if (ret)
		ipath_dbg("Failed to unmap %u TID pages, failing with %d\n",
			  ti->tidcnt, -ret);
	return ret;
}

/**
 * ipath_set_part_key - set a partition key
 * @pd: the port
 * @key: the key
 *
 * We can have up to 4 active at a time (other than the default, which is
 * always allowed).  This is somewhat tricky, since multiple ports may set
 * the same key, so we reference count them, and clean up at exit.  All 4
 * partition keys are packed into a single infinipath register.  It's an
 * error for a process to set the same pkey multiple times.  We provide no
 * mechanism to de-allocate a pkey at this time, we may eventually need to
 * do that.  I've used the atomic operations, and no locking, and only make
 * a single pass through what's available.  This should be more than
 * adequate for some time. I'll think about spinlocks or the like if and as
 * it's necessary.
 */
static int ipath_set_part_key(struct ipath_portdata *pd, u16 key)
{
	struct ipath_devdata *dd = pd->port_dd;
	int i, any = 0, pidx = -1;
	u16 lkey = key & 0x7FFF;
	int ret;

	if (lkey == (IPATH_DEFAULT_P_KEY & 0x7FFF)) {
		/* nothing to do; this key always valid */
		ret = 0;
		goto bail;
	}

	ipath_cdbg(VERBOSE, "p%u try to set pkey %hx, current keys "
		   "%hx:%x %hx:%x %hx:%x %hx:%x\n",
		   pd->port_port, key, dd->ipath_pkeys[0],
		   atomic_read(&dd->ipath_pkeyrefs[0]), dd->ipath_pkeys[1],
		   atomic_read(&dd->ipath_pkeyrefs[1]), dd->ipath_pkeys[2],
		   atomic_read(&dd->ipath_pkeyrefs[2]), dd->ipath_pkeys[3],
		   atomic_read(&dd->ipath_pkeyrefs[3]));

	if (!lkey) {
		ipath_cdbg(PROC, "p%u tries to set key 0, not allowed\n",
			   pd->port_port);
		ret = -EINVAL;
		goto bail;
	}

	/*
	 * Set the full membership bit, because it has to be
	 * set in the register or the packet, and it seems
	 * cleaner to set in the register than to force all
	 * callers to set it. (see bug 4331)
	 */
	key |= 0x8000;

	for (i = 0; i < ARRAY_SIZE(pd->port_pkeys); i++) {
		if (!pd->port_pkeys[i] && pidx == -1)
			pidx = i;
		if (pd->port_pkeys[i] == key) {
			ipath_cdbg(VERBOSE, "p%u tries to set same pkey "
				   "(%x) more than once\n",
				   pd->port_port, key);
			ret = -EEXIST;
			goto bail;
		}
	}
	if (pidx == -1) {
		ipath_dbg("All pkeys for port %u already in use, "
			  "can't set %x\n", pd->port_port, key);
		ret = -EBUSY;
		goto bail;
	}
	for (any = i = 0; i < ARRAY_SIZE(dd->ipath_pkeys); i++) {
		if (!dd->ipath_pkeys[i]) {
			any++;
			continue;
		}
		if (dd->ipath_pkeys[i] == key) {
			atomic_t *pkrefs = &dd->ipath_pkeyrefs[i];

			if (atomic_inc_return(pkrefs) > 1) {
				pd->port_pkeys[pidx] = key;
				ipath_cdbg(VERBOSE, "p%u set key %x "
					   "matches #%d, count now %d\n",
					   pd->port_port, key, i,
					   atomic_read(pkrefs));
				ret = 0;
				goto bail;
			} else {
				/*
				 * lost race, decrement count, catch below
				 */
				atomic_dec(pkrefs);
				ipath_cdbg(VERBOSE, "Lost race, count was "
					   "0, after dec, it's %d\n",
					   atomic_read(pkrefs));
				any++;
			}
		}
		if ((dd->ipath_pkeys[i] & 0x7FFF) == lkey) {
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
		ipath_dbg("port %u, all pkeys already in use, "
			  "can't set %x\n", pd->port_port, key);
		ret = -EBUSY;
		goto bail;
	}
	for (any = i = 0; i < ARRAY_SIZE(dd->ipath_pkeys); i++) {
		if (!dd->ipath_pkeys[i] &&
		    atomic_inc_return(&dd->ipath_pkeyrefs[i]) == 1) {
			u64 pkey;

			/* for ipathstats, etc. */
			ipath_stats.sps_pkeys[i] = lkey;
			pd->port_pkeys[pidx] = dd->ipath_pkeys[i] = key;
			pkey =
				(u64) dd->ipath_pkeys[0] |
				((u64) dd->ipath_pkeys[1] << 16) |
				((u64) dd->ipath_pkeys[2] << 32) |
				((u64) dd->ipath_pkeys[3] << 48);
			ipath_cdbg(PROC, "p%u set key %x in #%d, "
				   "portidx %d, new pkey reg %llx\n",
				   pd->port_port, key, i, pidx,
				   (unsigned long long) pkey);
			ipath_write_kreg(
				dd, dd->ipath_kregs->kr_partitionkey, pkey);

			ret = 0;
			goto bail;
		}
	}
	ipath_dbg("port %u, all pkeys already in use 2nd pass, "
		  "can't set %x\n", pd->port_port, key);
	ret = -EBUSY;

bail:
	return ret;
}

/**
 * ipath_manage_rcvq - manage a port's receive queue
 * @pd: the port
 * @subport: the subport
 * @start_stop: action to carry out
 *
 * start_stop == 0 disables receive on the port, for use in queue
 * overflow conditions.  start_stop==1 re-enables, to be used to
 * re-init the software copy of the head register
 */
static int ipath_manage_rcvq(struct ipath_portdata *pd, unsigned subport,
			     int start_stop)
{
	struct ipath_devdata *dd = pd->port_dd;

	ipath_cdbg(PROC, "%sabling rcv for unit %u port %u:%u\n",
		   start_stop ? "en" : "dis", dd->ipath_unit,
		   pd->port_port, subport);
	if (subport)
		goto bail;
	/* atomically clear receive enable port. */
	if (start_stop) {
		/*
		 * On enable, force in-memory copy of the tail register to
		 * 0, so that protocol code doesn't have to worry about
		 * whether or not the chip has yet updated the in-memory
		 * copy or not on return from the system call. The chip
		 * always resets it's tail register back to 0 on a
		 * transition from disabled to enabled.  This could cause a
		 * problem if software was broken, and did the enable w/o
		 * the disable, but eventually the in-memory copy will be
		 * updated and correct itself, even in the face of software
		 * bugs.
		 */
		*(volatile u64 *)pd->port_rcvhdrtail_kvaddr = 0;
		set_bit(INFINIPATH_R_PORTENABLE_SHIFT + pd->port_port,
			&dd->ipath_rcvctrl);
	} else
		clear_bit(INFINIPATH_R_PORTENABLE_SHIFT + pd->port_port,
			  &dd->ipath_rcvctrl);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
			 dd->ipath_rcvctrl);
	/* now be sure chip saw it before we return */
	ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
	if (start_stop) {
		/*
		 * And try to be sure that tail reg update has happened too.
		 * This should in theory interlock with the RXE changes to
		 * the tail register.  Don't assign it to the tail register
		 * in memory copy, since we could overwrite an update by the
		 * chip if we did.
		 */
		ipath_read_ureg32(dd, ur_rcvhdrtail, pd->port_port);
	}
	/* always; new head should be equal to new tail; see above */
bail:
	return 0;
}

static void ipath_clean_part_key(struct ipath_portdata *pd,
				 struct ipath_devdata *dd)
{
	int i, j, pchanged = 0;
	u64 oldpkey;

	/* for debugging only */
	oldpkey = (u64) dd->ipath_pkeys[0] |
		((u64) dd->ipath_pkeys[1] << 16) |
		((u64) dd->ipath_pkeys[2] << 32) |
		((u64) dd->ipath_pkeys[3] << 48);

	for (i = 0; i < ARRAY_SIZE(pd->port_pkeys); i++) {
		if (!pd->port_pkeys[i])
			continue;
		ipath_cdbg(VERBOSE, "look for key[%d] %hx in pkeys\n", i,
			   pd->port_pkeys[i]);
		for (j = 0; j < ARRAY_SIZE(dd->ipath_pkeys); j++) {
			/* check for match independent of the global bit */
			if ((dd->ipath_pkeys[j] & 0x7fff) !=
			    (pd->port_pkeys[i] & 0x7fff))
				continue;
			if (atomic_dec_and_test(&dd->ipath_pkeyrefs[j])) {
				ipath_cdbg(VERBOSE, "p%u clear key "
					   "%x matches #%d\n",
					   pd->port_port,
					   pd->port_pkeys[i], j);
				ipath_stats.sps_pkeys[j] =
					dd->ipath_pkeys[j] = 0;
				pchanged++;
			}
			else ipath_cdbg(
				VERBOSE, "p%u key %x matches #%d, "
				"but ref still %d\n", pd->port_port,
				pd->port_pkeys[i], j,
				atomic_read(&dd->ipath_pkeyrefs[j]));
			break;
		}
		pd->port_pkeys[i] = 0;
	}
	if (pchanged) {
		u64 pkey = (u64) dd->ipath_pkeys[0] |
			((u64) dd->ipath_pkeys[1] << 16) |
			((u64) dd->ipath_pkeys[2] << 32) |
			((u64) dd->ipath_pkeys[3] << 48);
		ipath_cdbg(VERBOSE, "p%u old pkey reg %llx, "
			   "new pkey reg %llx\n", pd->port_port,
			   (unsigned long long) oldpkey,
			   (unsigned long long) pkey);
		ipath_write_kreg(dd, dd->ipath_kregs->kr_partitionkey,
				 pkey);
	}
}

/*
 * Initialize the port data with the receive buffer sizes
 * so this can be done while the master port is locked.
 * Otherwise, there is a race with a slave opening the port
 * and seeing these fields uninitialized.
 */
static void init_user_egr_sizes(struct ipath_portdata *pd)
{
	struct ipath_devdata *dd = pd->port_dd;
	unsigned egrperchunk, egrcnt, size;

	/*
	 * to avoid wasting a lot of memory, we allocate 32KB chunks of
	 * physically contiguous memory, advance through it until used up
	 * and then allocate more.  Of course, we need memory to store those
	 * extra pointers, now.  Started out with 256KB, but under heavy
	 * memory pressure (creating large files and then copying them over
	 * NFS while doing lots of MPI jobs), we hit some allocation
	 * failures, even though we can sleep...  (2.6.10) Still get
	 * failures at 64K.  32K is the lowest we can go without wasting
	 * additional memory.
	 */
	size = 0x8000;
	egrperchunk = size / dd->ipath_rcvegrbufsize;
	egrcnt = dd->ipath_rcvegrcnt;
	pd->port_rcvegrbuf_chunks = (egrcnt + egrperchunk - 1) / egrperchunk;
	pd->port_rcvegrbufs_perchunk = egrperchunk;
	pd->port_rcvegrbuf_size = size;
}

/**
 * ipath_create_user_egr - allocate eager TID buffers
 * @pd: the port to allocate TID buffers for
 *
 * This routine is now quite different for user and kernel, because
 * the kernel uses skb's, for the accelerated network performance
 * This is the user port version
 *
 * Allocate the eager TID buffers and program them into infinipath
 * They are no longer completely contiguous, we do multiple allocation
 * calls.
 */
static int ipath_create_user_egr(struct ipath_portdata *pd)
{
	struct ipath_devdata *dd = pd->port_dd;
	unsigned e, egrcnt, egrperchunk, chunk, egrsize, egroff;
	size_t size;
	int ret;
	gfp_t gfp_flags;

	/*
	 * GFP_USER, but without GFP_FS, so buffer cache can be
	 * coalesced (we hope); otherwise, even at order 4,
	 * heavy filesystem activity makes these fail, and we can
	 * use compound pages.
	 */
	gfp_flags = __GFP_WAIT | __GFP_IO | __GFP_COMP;

	egrcnt = dd->ipath_rcvegrcnt;
	/* TID number offset for this port */
	egroff = pd->port_port * egrcnt;
	egrsize = dd->ipath_rcvegrbufsize;
	ipath_cdbg(VERBOSE, "Allocating %d egr buffers, at egrtid "
		   "offset %x, egrsize %u\n", egrcnt, egroff, egrsize);

	chunk = pd->port_rcvegrbuf_chunks;
	egrperchunk = pd->port_rcvegrbufs_perchunk;
	size = pd->port_rcvegrbuf_size;
	pd->port_rcvegrbuf = kmalloc(chunk * sizeof(pd->port_rcvegrbuf[0]),
				     GFP_KERNEL);
	if (!pd->port_rcvegrbuf) {
		ret = -ENOMEM;
		goto bail;
	}
	pd->port_rcvegrbuf_phys =
		kmalloc(chunk * sizeof(pd->port_rcvegrbuf_phys[0]),
			GFP_KERNEL);
	if (!pd->port_rcvegrbuf_phys) {
		ret = -ENOMEM;
		goto bail_rcvegrbuf;
	}
	for (e = 0; e < pd->port_rcvegrbuf_chunks; e++) {

		pd->port_rcvegrbuf[e] = dma_alloc_coherent(
			&dd->pcidev->dev, size, &pd->port_rcvegrbuf_phys[e],
			gfp_flags);

		if (!pd->port_rcvegrbuf[e]) {
			ret = -ENOMEM;
			goto bail_rcvegrbuf_phys;
		}
	}

	pd->port_rcvegr_phys = pd->port_rcvegrbuf_phys[0];

	for (e = chunk = 0; chunk < pd->port_rcvegrbuf_chunks; chunk++) {
		dma_addr_t pa = pd->port_rcvegrbuf_phys[chunk];
		unsigned i;

		for (i = 0; e < egrcnt && i < egrperchunk; e++, i++) {
			dd->ipath_f_put_tid(dd, e + egroff +
					    (u64 __iomem *)
					    ((char __iomem *)
					     dd->ipath_kregbase +
					     dd->ipath_rcvegrbase),
					    RCVHQ_RCV_TYPE_EAGER, pa);
			pa += egrsize;
		}
		cond_resched();	/* don't hog the cpu */
	}

	ret = 0;
	goto bail;

bail_rcvegrbuf_phys:
	for (e = 0; e < pd->port_rcvegrbuf_chunks &&
		pd->port_rcvegrbuf[e]; e++) {
		dma_free_coherent(&dd->pcidev->dev, size,
				  pd->port_rcvegrbuf[e],
				  pd->port_rcvegrbuf_phys[e]);

	}
	kfree(pd->port_rcvegrbuf_phys);
	pd->port_rcvegrbuf_phys = NULL;
bail_rcvegrbuf:
	kfree(pd->port_rcvegrbuf);
	pd->port_rcvegrbuf = NULL;
bail:
	return ret;
}


/* common code for the mappings on dma_alloc_coherent mem */
static int ipath_mmap_mem(struct vm_area_struct *vma,
	struct ipath_portdata *pd, unsigned len, int write_ok,
	void *kvaddr, char *what)
{
	struct ipath_devdata *dd = pd->port_dd;
	unsigned long pfn;
	int ret;

	if ((vma->vm_end - vma->vm_start) > len) {
		dev_info(&dd->pcidev->dev,
		         "FAIL on %s: len %lx > %x\n", what,
			 vma->vm_end - vma->vm_start, len);
		ret = -EFAULT;
		goto bail;
	}

	if (!write_ok) {
		if (vma->vm_flags & VM_WRITE) {
			dev_info(&dd->pcidev->dev,
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
		dev_info(&dd->pcidev->dev, "%s port%u mmap of %lx, %x "
			 "bytes r%c failed: %d\n", what, pd->port_port,
			 pfn, len, write_ok?'w':'o', ret);
	else
		ipath_cdbg(VERBOSE, "%s port%u mmaped %lx, %x bytes "
			   "r%c\n", what, pd->port_port, pfn, len,
			   write_ok?'w':'o');
bail:
	return ret;
}

static int mmap_ureg(struct vm_area_struct *vma, struct ipath_devdata *dd,
		     u64 ureg)
{
	unsigned long phys;
	int ret;

	/*
	 * This is real hardware, so use io_remap.  This is the mechanism
	 * for the user process to update the head registers for their port
	 * in the chip.
	 */
	if ((vma->vm_end - vma->vm_start) > PAGE_SIZE) {
		dev_info(&dd->pcidev->dev, "FAIL mmap userreg: reqlen "
			 "%lx > PAGE\n", vma->vm_end - vma->vm_start);
		ret = -EFAULT;
	} else {
		phys = dd->ipath_physaddr + ureg;
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
			struct ipath_devdata *dd,
			struct ipath_portdata *pd,
			unsigned piobufs, unsigned piocnt)
{
	unsigned long phys;
	int ret;

	/*
	 * When we map the PIO buffers in the chip, we want to map them as
	 * writeonly, no read possible.   This prevents access to previous
	 * process data, and catches users who might try to read the i/o
	 * space due to a bug.
	 */
	if ((vma->vm_end - vma->vm_start) > (piocnt * dd->ipath_palign)) {
		dev_info(&dd->pcidev->dev, "FAIL mmap piobufs: "
			 "reqlen %lx > PAGE\n",
			 vma->vm_end - vma->vm_start);
		ret = -EINVAL;
		goto bail;
	}

	phys = dd->ipath_physaddr + piobufs;

	/*
	 * Don't mark this as non-cached, or we don't get the
	 * write combining behavior we want on the PIO buffers!
	 */

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

	ret = io_remap_pfn_range(vma, vma->vm_start, phys >> PAGE_SHIFT,
				 vma->vm_end - vma->vm_start,
				 vma->vm_page_prot);
bail:
	return ret;
}

static int mmap_rcvegrbufs(struct vm_area_struct *vma,
			   struct ipath_portdata *pd)
{
	struct ipath_devdata *dd = pd->port_dd;
	unsigned long start, size;
	size_t total_size, i;
	unsigned long pfn;
	int ret;

	size = pd->port_rcvegrbuf_size;
	total_size = pd->port_rcvegrbuf_chunks * size;
	if ((vma->vm_end - vma->vm_start) > total_size) {
		dev_info(&dd->pcidev->dev, "FAIL on egr bufs: "
			 "reqlen %lx > actual %lx\n",
			 vma->vm_end - vma->vm_start,
			 (unsigned long) total_size);
		ret = -EINVAL;
		goto bail;
	}

	if (vma->vm_flags & VM_WRITE) {
		dev_info(&dd->pcidev->dev, "Can't map eager buffers as "
			 "writable (flags=%lx)\n", vma->vm_flags);
		ret = -EPERM;
		goto bail;
	}
	/* don't allow them to later change to writeable with mprotect */
	vma->vm_flags &= ~VM_MAYWRITE;

	start = vma->vm_start;

	for (i = 0; i < pd->port_rcvegrbuf_chunks; i++, start += size) {
		pfn = virt_to_phys(pd->port_rcvegrbuf[i]) >> PAGE_SHIFT;
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
 * ipath_file_vma_nopage - handle a VMA page fault.
 */
static struct page *ipath_file_vma_nopage(struct vm_area_struct *vma,
					  unsigned long address, int *type)
{
	unsigned long offset = address - vma->vm_start;
	struct page *page = NOPAGE_SIGBUS;
	void *pageptr;

	/*
	 * Convert the vmalloc address into a struct page.
	 */
	pageptr = (void *)(offset + (vma->vm_pgoff << PAGE_SHIFT));
	page = vmalloc_to_page(pageptr);
	if (!page)
		goto out;

	/* Increment the reference count. */
	get_page(page);
	if (type)
		*type = VM_FAULT_MINOR;
out:
	return page;
}

static struct vm_operations_struct ipath_file_vm_ops = {
	.nopage = ipath_file_vma_nopage,
};

static int mmap_kvaddr(struct vm_area_struct *vma, u64 pgaddr,
		       struct ipath_portdata *pd, unsigned subport)
{
	unsigned long len;
	struct ipath_devdata *dd;
	void *addr;
	size_t size;
	int ret = 0;

	/* If the port is not shared, all addresses should be physical */
	if (!pd->port_subport_cnt)
		goto bail;

	dd = pd->port_dd;
	size = pd->port_rcvegrbuf_chunks * pd->port_rcvegrbuf_size;

	/*
	 * Each process has all the subport uregbase, rcvhdrq, and
	 * rcvegrbufs mmapped - as an array for all the processes,
	 * and also separately for this process.
	 */
	if (pgaddr == cvt_kvaddr(pd->subport_uregbase)) {
		addr = pd->subport_uregbase;
		size = PAGE_SIZE * pd->port_subport_cnt;
	} else if (pgaddr == cvt_kvaddr(pd->subport_rcvhdr_base)) {
		addr = pd->subport_rcvhdr_base;
		size = pd->port_rcvhdrq_size * pd->port_subport_cnt;
	} else if (pgaddr == cvt_kvaddr(pd->subport_rcvegrbuf)) {
		addr = pd->subport_rcvegrbuf;
		size *= pd->port_subport_cnt;
        } else if (pgaddr == cvt_kvaddr(pd->subport_uregbase +
                                        PAGE_SIZE * subport)) {
                addr = pd->subport_uregbase + PAGE_SIZE * subport;
                size = PAGE_SIZE;
        } else if (pgaddr == cvt_kvaddr(pd->subport_rcvhdr_base +
                                pd->port_rcvhdrq_size * subport)) {
                addr = pd->subport_rcvhdr_base +
                        pd->port_rcvhdrq_size * subport;
                size = pd->port_rcvhdrq_size;
        } else if (pgaddr == cvt_kvaddr(pd->subport_rcvegrbuf +
                               size * subport)) {
                addr = pd->subport_rcvegrbuf + size * subport;
                /* rcvegrbufs are read-only on the slave */
                if (vma->vm_flags & VM_WRITE) {
                        dev_info(&dd->pcidev->dev,
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
	} else {
		goto bail;
	}
	len = vma->vm_end - vma->vm_start;
	if (len > size) {
		ipath_cdbg(MM, "FAIL: reqlen %lx > %zx\n", len, size);
		ret = -EINVAL;
		goto bail;
	}

	vma->vm_pgoff = (unsigned long) addr >> PAGE_SHIFT;
	vma->vm_ops = &ipath_file_vm_ops;
	vma->vm_flags |= VM_RESERVED | VM_DONTEXPAND;
	ret = 1;

bail:
	return ret;
}

/**
 * ipath_mmap - mmap various structures into user space
 * @fp: the file pointer
 * @vma: the VM area
 *
 * We use this to have a shared buffer between the kernel and the user code
 * for the rcvhdr queue, egr buffers, and the per-port user regs and pio
 * buffers in the chip.  We have the open and close entries so we can bump
 * the ref count and keep the driver from being unloaded while still mapped.
 */
static int ipath_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct ipath_portdata *pd;
	struct ipath_devdata *dd;
	u64 pgaddr, ureg;
	unsigned piobufs, piocnt;
	int ret;

	pd = port_fp(fp);
	if (!pd) {
		ret = -EINVAL;
		goto bail;
	}
	dd = pd->port_dd;

	/*
	 * This is the ipath_do_user_init() code, mapping the shared buffers
	 * into the user process. The address referred to by vm_pgoff is the
	 * file offset passed via mmap().  For shared ports, this is the
	 * kernel vmalloc() address of the pages to share with the master.
	 * For non-shared or master ports, this is a physical address.
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

	ipath_cdbg(MM, "pgaddr %llx vm_start=%lx len %lx port %u:%u:%u\n",
		   (unsigned long long) pgaddr, vma->vm_start,
		   vma->vm_end - vma->vm_start, dd->ipath_unit,
		   pd->port_port, subport_fp(fp));

	/*
	 * Physical addresses must fit in 40 bits for our hardware.
	 * Check for kernel virtual addresses first, anything else must
	 * match a HW or memory address.
	 */
	ret = mmap_kvaddr(vma, pgaddr, pd, subport_fp(fp));
	if (ret) {
		if (ret > 0)
			ret = 0;
		goto bail;
	}

	ureg = dd->ipath_uregbase + dd->ipath_palign * pd->port_port;
	if (!pd->port_subport_cnt) {
		/* port is not shared */
		piocnt = dd->ipath_pbufsport;
		piobufs = pd->port_piobufs;
	} else if (!subport_fp(fp)) {
		/* caller is the master */
		piocnt = (dd->ipath_pbufsport / pd->port_subport_cnt) +
			 (dd->ipath_pbufsport % pd->port_subport_cnt);
		piobufs = pd->port_piobufs +
			dd->ipath_palign * (dd->ipath_pbufsport - piocnt);
	} else {
		unsigned slave = subport_fp(fp) - 1;

		/* caller is a slave */
		piocnt = dd->ipath_pbufsport / pd->port_subport_cnt;
		piobufs = pd->port_piobufs + dd->ipath_palign * piocnt * slave;
	}

	if (pgaddr == ureg)
		ret = mmap_ureg(vma, dd, ureg);
	else if (pgaddr == piobufs)
		ret = mmap_piobufs(vma, dd, pd, piobufs, piocnt);
	else if (pgaddr == dd->ipath_pioavailregs_phys)
		/* in-memory copy of pioavail registers */
		ret = ipath_mmap_mem(vma, pd, PAGE_SIZE, 0,
			      	     (void *) dd->ipath_pioavailregs_dma,
				     "pioavail registers");
	else if (pgaddr == pd->port_rcvegr_phys)
		ret = mmap_rcvegrbufs(vma, pd);
	else if (pgaddr == (u64) pd->port_rcvhdrq_phys)
		/*
		 * The rcvhdrq itself; readonly except on HT (so have
		 * to allow writable mapping), multiple pages, contiguous
		 * from an i/o perspective.
		 */
		ret = ipath_mmap_mem(vma, pd, pd->port_rcvhdrq_size, 1,
				     pd->port_rcvhdrq,
				     "rcvhdrq");
	else if (pgaddr == (u64) pd->port_rcvhdrqtailaddr_phys)
		/* in-memory copy of rcvhdrq tail register */
		ret = ipath_mmap_mem(vma, pd, PAGE_SIZE, 0,
				     pd->port_rcvhdrtail_kvaddr,
				     "rcvhdrq tail");
	else
		ret = -EINVAL;

	vma->vm_private_data = NULL;

	if (ret < 0)
		dev_info(&dd->pcidev->dev,
			 "Failure %d on off %llx len %lx\n",
			 -ret, (unsigned long long)pgaddr,
			 vma->vm_end - vma->vm_start);
bail:
	return ret;
}

static unsigned ipath_poll_hdrqfull(struct ipath_portdata *pd)
{
	unsigned pollflag = 0;

	if ((pd->poll_type & IPATH_POLL_TYPE_OVERFLOW) &&
	    pd->port_hdrqfull != pd->port_hdrqfull_poll) {
		pollflag |= POLLIN | POLLRDNORM;
		pd->port_hdrqfull_poll = pd->port_hdrqfull;
	}

	return pollflag;
}

static unsigned int ipath_poll_urgent(struct ipath_portdata *pd,
				      struct file *fp,
				      struct poll_table_struct *pt)
{
	unsigned pollflag = 0;
	struct ipath_devdata *dd;

	dd = pd->port_dd;

	/* variable access in ipath_poll_hdrqfull() needs this */
	rmb();
	pollflag = ipath_poll_hdrqfull(pd);

	if (pd->port_urgent != pd->port_urgent_poll) {
		pollflag |= POLLIN | POLLRDNORM;
		pd->port_urgent_poll = pd->port_urgent;
	}

	if (!pollflag) {
		/* this saves a spin_lock/unlock in interrupt handler... */
		set_bit(IPATH_PORT_WAITING_URG, &pd->port_flag);
		/* flush waiting flag so don't miss an event... */
		wmb();
		poll_wait(fp, &pd->port_wait, pt);
	}

	return pollflag;
}

static unsigned int ipath_poll_next(struct ipath_portdata *pd,
				    struct file *fp,
				    struct poll_table_struct *pt)
{
	u32 head;
	u32 tail;
	unsigned pollflag = 0;
	struct ipath_devdata *dd;

	dd = pd->port_dd;

	/* variable access in ipath_poll_hdrqfull() needs this */
	rmb();
	pollflag = ipath_poll_hdrqfull(pd);

	head = ipath_read_ureg32(dd, ur_rcvhdrhead, pd->port_port);
	tail = *(volatile u64 *)pd->port_rcvhdrtail_kvaddr;

	if (head != tail)
		pollflag |= POLLIN | POLLRDNORM;
	else {
		/* this saves a spin_lock/unlock in interrupt handler */
		set_bit(IPATH_PORT_WAITING_RCV, &pd->port_flag);
		/* flush waiting flag so we don't miss an event */
		wmb();

		set_bit(pd->port_port + INFINIPATH_R_INTRAVAIL_SHIFT,
			&dd->ipath_rcvctrl);

		ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
				 dd->ipath_rcvctrl);

		if (dd->ipath_rhdrhead_intr_off) /* arm rcv interrupt */
			ipath_write_ureg(dd, ur_rcvhdrhead,
					 dd->ipath_rhdrhead_intr_off | head,
					 pd->port_port);

		poll_wait(fp, &pd->port_wait, pt);
	}

	return pollflag;
}

static unsigned int ipath_poll(struct file *fp,
			       struct poll_table_struct *pt)
{
	struct ipath_portdata *pd;
	unsigned pollflag;

	pd = port_fp(fp);
	if (!pd)
		pollflag = 0;
	else if (pd->poll_type & IPATH_POLL_TYPE_URGENT)
		pollflag = ipath_poll_urgent(pd, fp, pt);
	else
		pollflag = ipath_poll_next(pd, fp, pt);

	return pollflag;
}

static int ipath_supports_subports(int user_swmajor, int user_swminor)
{
	/* no subport implementation prior to software version 1.3 */
	return (user_swmajor > 1) || (user_swminor >= 3);
}

static int ipath_compatible_subports(int user_swmajor, int user_swminor)
{
	/* this code is written long-hand for clarity */
	if (IPATH_USER_SWMAJOR != user_swmajor) {
		/* no promise of compatibility if major mismatch */
		return 0;
	}
	if (IPATH_USER_SWMAJOR == 1) {
		switch (IPATH_USER_SWMINOR) {
		case 0:
		case 1:
		case 2:
			/* no subport implementation so cannot be compatible */
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

static int init_subports(struct ipath_devdata *dd,
			 struct ipath_portdata *pd,
			 const struct ipath_user_info *uinfo)
{
	int ret = 0;
	unsigned num_subports;
	size_t size;

	/*
	 * If the user is requesting zero subports,
	 * skip the subport allocation.
	 */
	if (uinfo->spu_subport_cnt <= 0)
		goto bail;

	/* Self-consistency check for ipath_compatible_subports() */
	if (ipath_supports_subports(IPATH_USER_SWMAJOR, IPATH_USER_SWMINOR) &&
	    !ipath_compatible_subports(IPATH_USER_SWMAJOR,
				       IPATH_USER_SWMINOR)) {
		dev_info(&dd->pcidev->dev,
			 "Inconsistent ipath_compatible_subports()\n");
		goto bail;
	}

	/* Check for subport compatibility */
	if (!ipath_compatible_subports(uinfo->spu_userversion >> 16,
				       uinfo->spu_userversion & 0xffff)) {
		dev_info(&dd->pcidev->dev,
			 "Mismatched user version (%d.%d) and driver "
			 "version (%d.%d) while port sharing. Ensure "
                         "that driver and library are from the same "
                         "release.\n",
			 (int) (uinfo->spu_userversion >> 16),
                         (int) (uinfo->spu_userversion & 0xffff),
			 IPATH_USER_SWMAJOR,
	                 IPATH_USER_SWMINOR);
		goto bail;
	}
	if (uinfo->spu_subport_cnt > INFINIPATH_MAX_SUBPORT) {
		ret = -EINVAL;
		goto bail;
	}

	num_subports = uinfo->spu_subport_cnt;
	pd->subport_uregbase = vmalloc(PAGE_SIZE * num_subports);
	if (!pd->subport_uregbase) {
		ret = -ENOMEM;
		goto bail;
	}
	/* Note: pd->port_rcvhdrq_size isn't initialized yet. */
	size = ALIGN(dd->ipath_rcvhdrcnt * dd->ipath_rcvhdrentsize *
		     sizeof(u32), PAGE_SIZE) * num_subports;
	pd->subport_rcvhdr_base = vmalloc(size);
	if (!pd->subport_rcvhdr_base) {
		ret = -ENOMEM;
		goto bail_ureg;
	}

	pd->subport_rcvegrbuf = vmalloc(pd->port_rcvegrbuf_chunks *
					pd->port_rcvegrbuf_size *
					num_subports);
	if (!pd->subport_rcvegrbuf) {
		ret = -ENOMEM;
		goto bail_rhdr;
	}

	pd->port_subport_cnt = uinfo->spu_subport_cnt;
	pd->port_subport_id = uinfo->spu_subport_id;
	pd->active_slaves = 1;
	set_bit(IPATH_PORT_MASTER_UNINIT, &pd->port_flag);
	memset(pd->subport_uregbase, 0, PAGE_SIZE * num_subports);
	memset(pd->subport_rcvhdr_base, 0, size);
	memset(pd->subport_rcvegrbuf, 0, pd->port_rcvegrbuf_chunks *
				         pd->port_rcvegrbuf_size *
				         num_subports);
	goto bail;

bail_rhdr:
	vfree(pd->subport_rcvhdr_base);
bail_ureg:
	vfree(pd->subport_uregbase);
	pd->subport_uregbase = NULL;
bail:
	return ret;
}

static int try_alloc_port(struct ipath_devdata *dd, int port,
			  struct file *fp,
			  const struct ipath_user_info *uinfo)
{
	struct ipath_portdata *pd;
	int ret;

	if (!(pd = dd->ipath_pd[port])) {
		void *ptmp;

		pd = kzalloc(sizeof(struct ipath_portdata), GFP_KERNEL);

		/*
		 * Allocate memory for use in ipath_tid_update() just once
		 * at open, not per call.  Reduces cost of expected send
		 * setup.
		 */
		ptmp = kmalloc(dd->ipath_rcvtidcnt * sizeof(u16) +
			       dd->ipath_rcvtidcnt * sizeof(struct page **),
			       GFP_KERNEL);
		if (!pd || !ptmp) {
			ipath_dev_err(dd, "Unable to allocate portdata "
				      "memory, failing open\n");
			ret = -ENOMEM;
			kfree(pd);
			kfree(ptmp);
			goto bail;
		}
		dd->ipath_pd[port] = pd;
		dd->ipath_pd[port]->port_port = port;
		dd->ipath_pd[port]->port_dd = dd;
		dd->ipath_pd[port]->port_tid_pg_list = ptmp;
		init_waitqueue_head(&dd->ipath_pd[port]->port_wait);
	}
	if (!pd->port_cnt) {
		pd->userversion = uinfo->spu_userversion;
		init_user_egr_sizes(pd);
		if ((ret = init_subports(dd, pd, uinfo)) != 0)
			goto bail;
		ipath_cdbg(PROC, "%s[%u] opened unit:port %u:%u\n",
			   current->comm, current->pid, dd->ipath_unit,
			   port);
		pd->port_cnt = 1;
		port_fp(fp) = pd;
		pd->port_pid = current->pid;
		strncpy(pd->port_comm, current->comm, sizeof(pd->port_comm));
		ipath_stats.sps_ports++;
		ret = 0;
	} else
		ret = -EBUSY;

bail:
	return ret;
}

static inline int usable(struct ipath_devdata *dd)
{
	return dd &&
		(dd->ipath_flags & IPATH_PRESENT) &&
		dd->ipath_kregbase &&
		dd->ipath_lid &&
		!(dd->ipath_flags & (IPATH_LINKDOWN | IPATH_DISABLED
				     | IPATH_LINKUNK));
}

static int find_free_port(int unit, struct file *fp,
			  const struct ipath_user_info *uinfo)
{
	struct ipath_devdata *dd = ipath_lookup(unit);
	int ret, i;

	if (!dd) {
		ret = -ENODEV;
		goto bail;
	}

	if (!usable(dd)) {
		ret = -ENETDOWN;
		goto bail;
	}

	for (i = 1; i < dd->ipath_cfgports; i++) {
		ret = try_alloc_port(dd, i, fp, uinfo);
		if (ret != -EBUSY)
			goto bail;
	}
	ret = -EBUSY;

bail:
	return ret;
}

static int find_best_unit(struct file *fp,
			  const struct ipath_user_info *uinfo)
{
	int ret = 0, i, prefunit = -1, devmax;
	int maxofallports, npresent, nup;
	int ndev;

	devmax = ipath_count_units(&npresent, &nup, &maxofallports);

	/*
	 * This code is present to allow a knowledgeable person to
	 * specify the layout of processes to processors before opening
	 * this driver, and then we'll assign the process to the "closest"
	 * InfiniPath chip to that processor (we assume reasonable connectivity,
	 * for now).  This code assumes that if affinity has been set
	 * before this point, that at most one cpu is set; for now this
	 * is reasonable.  I check for both cpus_empty() and cpus_full(),
	 * in case some kernel variant sets none of the bits when no
	 * affinity is set.  2.6.11 and 12 kernels have all present
	 * cpus set.  Some day we'll have to fix it up further to handle
	 * a cpu subset.  This algorithm fails for two HT chips connected
	 * in tunnel fashion.  Eventually this needs real topology
	 * information.  There may be some issues with dual core numbering
	 * as well.  This needs more work prior to release.
	 */
	if (!cpus_empty(current->cpus_allowed) &&
	    !cpus_full(current->cpus_allowed)) {
		int ncpus = num_online_cpus(), curcpu = -1, nset = 0;
		for (i = 0; i < ncpus; i++)
			if (cpu_isset(i, current->cpus_allowed)) {
				ipath_cdbg(PROC, "%s[%u] affinity set for "
					   "cpu %d/%d\n", current->comm,
					   current->pid, i, ncpus);
				curcpu = i;
				nset++;
			}
		if (curcpu != -1 && nset != ncpus) {
			if (npresent) {
				prefunit = curcpu / (ncpus / npresent);
				ipath_cdbg(PROC,"%s[%u] %d chips, %d cpus, "
					  "%d cpus/chip, select unit %d\n",
					  current->comm, current->pid,
					  npresent, ncpus, ncpus / npresent,
					  prefunit);
			}
		}
	}

	/*
	 * user ports start at 1, kernel port is 0
	 * For now, we do round-robin access across all chips
	 */

	if (prefunit != -1)
		devmax = prefunit + 1;
recheck:
	for (i = 1; i < maxofallports; i++) {
		for (ndev = prefunit != -1 ? prefunit : 0; ndev < devmax;
		     ndev++) {
			struct ipath_devdata *dd = ipath_lookup(ndev);

			if (!usable(dd))
				continue; /* can't use this unit */
			if (i >= dd->ipath_cfgports)
				/*
				 * Maxed out on users of this unit. Try
				 * next.
				 */
				continue;
			ret = try_alloc_port(dd, i, fp, uinfo);
			if (!ret)
				goto done;
		}
	}

	if (npresent) {
		if (nup == 0) {
			ret = -ENETDOWN;
			ipath_dbg("No ports available (none initialized "
				  "and ready)\n");
		} else {
			if (prefunit > 0) {
				/* if started above 0, retry from 0 */
				ipath_cdbg(PROC,
					   "%s[%u] no ports on prefunit "
					   "%d, clear and re-check\n",
					   current->comm, current->pid,
					   prefunit);
				devmax = ipath_count_units(NULL, NULL,
							   NULL);
				prefunit = -1;
				goto recheck;
			}
			ret = -EBUSY;
			ipath_dbg("No ports available\n");
		}
	} else {
		ret = -ENXIO;
		ipath_dbg("No boards found\n");
	}

done:
	return ret;
}

static int find_shared_port(struct file *fp,
			    const struct ipath_user_info *uinfo)
{
	int devmax, ndev, i;
	int ret = 0;

	devmax = ipath_count_units(NULL, NULL, NULL);

	for (ndev = 0; ndev < devmax; ndev++) {
		struct ipath_devdata *dd = ipath_lookup(ndev);

		if (!dd)
			continue;
		for (i = 1; i < dd->ipath_cfgports; i++) {
			struct ipath_portdata *pd = dd->ipath_pd[i];

			/* Skip ports which are not yet open */
			if (!pd || !pd->port_cnt)
				continue;
			/* Skip port if it doesn't match the requested one */
			if (pd->port_subport_id != uinfo->spu_subport_id)
				continue;
			/* Verify the sharing process matches the master */
			if (pd->port_subport_cnt != uinfo->spu_subport_cnt ||
			    pd->userversion != uinfo->spu_userversion ||
			    pd->port_cnt >= pd->port_subport_cnt) {
				ret = -EINVAL;
				goto done;
			}
			port_fp(fp) = pd;
			subport_fp(fp) = pd->port_cnt++;
			tidcursor_fp(fp) = 0;
			pd->active_slaves |= 1 << subport_fp(fp);
			ipath_cdbg(PROC,
				   "%s[%u] %u sharing %s[%u] unit:port %u:%u\n",
				   current->comm, current->pid,
				   subport_fp(fp),
				   pd->port_comm, pd->port_pid,
				   dd->ipath_unit, pd->port_port);
			ret = 1;
			goto done;
		}
	}

done:
	return ret;
}

static int ipath_open(struct inode *in, struct file *fp)
{
	/* The real work is performed later in ipath_assign_port() */
	fp->private_data = kzalloc(sizeof(struct ipath_filedata), GFP_KERNEL);
	return fp->private_data ? 0 : -ENOMEM;
}

/* Get port early, so can set affinity prior to memory allocation */
static int ipath_assign_port(struct file *fp,
			      const struct ipath_user_info *uinfo)
{
	int ret;
	int i_minor;
	unsigned swmajor, swminor;

	/* Check to be sure we haven't already initialized this file */
	if (port_fp(fp)) {
		ret = -EINVAL;
		goto done;
	}

	/* for now, if major version is different, bail */
	swmajor = uinfo->spu_userversion >> 16;
	if (swmajor != IPATH_USER_SWMAJOR) {
		ipath_dbg("User major version %d not same as driver "
			  "major %d\n", uinfo->spu_userversion >> 16,
			  IPATH_USER_SWMAJOR);
		ret = -ENODEV;
		goto done;
	}

	swminor = uinfo->spu_userversion & 0xffff;
	if (swminor != IPATH_USER_SWMINOR)
		ipath_dbg("User minor version %d not same as driver "
			  "minor %d\n", swminor, IPATH_USER_SWMINOR);

	mutex_lock(&ipath_mutex);

	if (ipath_compatible_subports(swmajor, swminor) &&
	    uinfo->spu_subport_cnt &&
	    (ret = find_shared_port(fp, uinfo))) {
		mutex_unlock(&ipath_mutex);
		if (ret > 0)
			ret = 0;
		goto done;
	}

	i_minor = iminor(fp->f_path.dentry->d_inode) - IPATH_USER_MINOR_BASE;
	ipath_cdbg(VERBOSE, "open on dev %lx (minor %d)\n",
		   (long)fp->f_path.dentry->d_inode->i_rdev, i_minor);

	if (i_minor)
		ret = find_free_port(i_minor - 1, fp, uinfo);
	else
		ret = find_best_unit(fp, uinfo);

	mutex_unlock(&ipath_mutex);

done:
	return ret;
}


static int ipath_do_user_init(struct file *fp,
			      const struct ipath_user_info *uinfo)
{
	int ret;
	struct ipath_portdata *pd = port_fp(fp);
	struct ipath_devdata *dd;
	u32 head32;

	/* Subports don't need to initialize anything since master did it. */
	if (subport_fp(fp)) {
		ret = wait_event_interruptible(pd->port_wait,
			!test_bit(IPATH_PORT_MASTER_UNINIT, &pd->port_flag));
		goto done;
	}

	dd = pd->port_dd;

	if (uinfo->spu_rcvhdrsize) {
		ret = ipath_setrcvhdrsize(dd, uinfo->spu_rcvhdrsize);
		if (ret)
			goto done;
	}

	/* for now we do nothing with rcvhdrcnt: uinfo->spu_rcvhdrcnt */

	/* for right now, kernel piobufs are at end, so port 1 is at 0 */
	pd->port_piobufs = dd->ipath_piobufbase +
		dd->ipath_pbufsport * (pd->port_port - 1) * dd->ipath_palign;
	ipath_cdbg(VERBOSE, "Set base of piobufs for port %u to 0x%x\n",
		   pd->port_port, pd->port_piobufs);

	/*
	 * Now allocate the rcvhdr Q and eager TIDs; skip the TID
	 * array for time being.  If pd->port_port > chip-supported,
	 * we need to do extra stuff here to handle by handling overflow
	 * through port 0, someday
	 */
	ret = ipath_create_rcvhdrq(dd, pd);
	if (!ret)
		ret = ipath_create_user_egr(pd);
	if (ret)
		goto done;

	/*
	 * set the eager head register for this port to the current values
	 * of the tail pointers, since we don't know if they were
	 * updated on last use of the port.
	 */
	head32 = ipath_read_ureg32(dd, ur_rcvegrindextail, pd->port_port);
	ipath_write_ureg(dd, ur_rcvegrindexhead, head32, pd->port_port);
	dd->ipath_lastegrheads[pd->port_port] = -1;
	dd->ipath_lastrcvhdrqtails[pd->port_port] = -1;
	ipath_cdbg(VERBOSE, "Wrote port%d egrhead %x from tail regs\n",
		pd->port_port, head32);
	pd->port_tidcursor = 0;	/* start at beginning after open */

	/* initialize poll variables... */
	pd->port_urgent = 0;
	pd->port_urgent_poll = 0;
	pd->port_hdrqfull_poll = pd->port_hdrqfull;

	/*
	 * now enable the port; the tail registers will be written to memory
	 * by the chip as soon as it sees the write to
	 * dd->ipath_kregs->kr_rcvctrl.  The update only happens on
	 * transition from 0 to 1, so clear it first, then set it as part of
	 * enabling the port.  This will (very briefly) affect any other
	 * open ports, but it shouldn't be long enough to be an issue.
	 * We explictly set the in-memory copy to 0 beforehand, so we don't
	 * have to wait to be sure the DMA update has happened.
	 */
	*(volatile u64 *)pd->port_rcvhdrtail_kvaddr = 0ULL;
	set_bit(INFINIPATH_R_PORTENABLE_SHIFT + pd->port_port,
		&dd->ipath_rcvctrl);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
			 dd->ipath_rcvctrl & ~INFINIPATH_R_TAILUPD);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
			 dd->ipath_rcvctrl);
	/* Notify any waiting slaves */
	if (pd->port_subport_cnt) {
		clear_bit(IPATH_PORT_MASTER_UNINIT, &pd->port_flag);
		wake_up(&pd->port_wait);
	}
done:
	return ret;
}

/**
 * unlock_exptid - unlock any expected TID entries port still had in use
 * @pd: port
 *
 * We don't actually update the chip here, because we do a bulk update
 * below, using ipath_f_clear_tids.
 */
static void unlock_expected_tids(struct ipath_portdata *pd)
{
	struct ipath_devdata *dd = pd->port_dd;
	int port_tidbase = pd->port_port * dd->ipath_rcvtidcnt;
	int i, cnt = 0, maxtid = port_tidbase + dd->ipath_rcvtidcnt;

	ipath_cdbg(VERBOSE, "Port %u unlocking any locked expTID pages\n",
		   pd->port_port);
	for (i = port_tidbase; i < maxtid; i++) {
		if (!dd->ipath_pageshadow[i])
			continue;

		pci_unmap_page(dd->pcidev, dd->ipath_physshadow[i],
			PAGE_SIZE, PCI_DMA_FROMDEVICE);
		ipath_release_user_pages_on_close(&dd->ipath_pageshadow[i],
						  1);
		dd->ipath_pageshadow[i] = NULL;
		cnt++;
		ipath_stats.sps_pageunlocks++;
	}
	if (cnt)
		ipath_cdbg(VERBOSE, "Port %u locked %u expTID entries\n",
			   pd->port_port, cnt);

	if (ipath_stats.sps_pagelocks || ipath_stats.sps_pageunlocks)
		ipath_cdbg(VERBOSE, "%llu pages locked, %llu unlocked\n",
			   (unsigned long long) ipath_stats.sps_pagelocks,
			   (unsigned long long)
			   ipath_stats.sps_pageunlocks);
}

static int ipath_close(struct inode *in, struct file *fp)
{
	int ret = 0;
	struct ipath_filedata *fd;
	struct ipath_portdata *pd;
	struct ipath_devdata *dd;
	unsigned port;

	ipath_cdbg(VERBOSE, "close on dev %lx, private data %p\n",
		   (long)in->i_rdev, fp->private_data);

	mutex_lock(&ipath_mutex);

	fd = (struct ipath_filedata *) fp->private_data;
	fp->private_data = NULL;
	pd = fd->pd;
	if (!pd) {
		mutex_unlock(&ipath_mutex);
		goto bail;
	}
	if (--pd->port_cnt) {
		/*
		 * XXX If the master closes the port before the slave(s),
		 * revoke the mmap for the eager receive queue so
		 * the slave(s) don't wait for receive data forever.
		 */
		pd->active_slaves &= ~(1 << fd->subport);
		mutex_unlock(&ipath_mutex);
		goto bail;
	}
	port = pd->port_port;
	dd = pd->port_dd;

	if (pd->port_hdrqfull) {
		ipath_cdbg(PROC, "%s[%u] had %u rcvhdrqfull errors "
			   "during run\n", pd->port_comm, pd->port_pid,
			   pd->port_hdrqfull);
		pd->port_hdrqfull = 0;
	}

	if (pd->port_rcvwait_to || pd->port_piowait_to
	    || pd->port_rcvnowait || pd->port_pionowait) {
		ipath_cdbg(VERBOSE, "port%u, %u rcv, %u pio wait timeo; "
			   "%u rcv %u, pio already\n",
			   pd->port_port, pd->port_rcvwait_to,
			   pd->port_piowait_to, pd->port_rcvnowait,
			   pd->port_pionowait);
		pd->port_rcvwait_to = pd->port_piowait_to =
			pd->port_rcvnowait = pd->port_pionowait = 0;
	}
	if (pd->port_flag) {
		ipath_dbg("port %u port_flag still set to 0x%lx\n",
			  pd->port_port, pd->port_flag);
		pd->port_flag = 0;
	}

	if (dd->ipath_kregbase) {
		int i;
		/* atomically clear receive enable port and intr avail. */
		clear_bit(INFINIPATH_R_PORTENABLE_SHIFT + port,
			  &dd->ipath_rcvctrl);
		clear_bit(pd->port_port + INFINIPATH_R_INTRAVAIL_SHIFT,
			  &dd->ipath_rcvctrl);
		ipath_write_kreg( dd, dd->ipath_kregs->kr_rcvctrl,
			dd->ipath_rcvctrl);
		/* and read back from chip to be sure that nothing
		 * else is in flight when we do the rest */
		(void)ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);

		/* clean up the pkeys for this port user */
		ipath_clean_part_key(pd, dd);
		/*
		 * be paranoid, and never write 0's to these, just use an
		 * unused part of the port 0 tail page.  Of course,
		 * rcvhdraddr points to a large chunk of memory, so this
		 * could still trash things, but at least it won't trash
		 * page 0, and by disabling the port, it should stop "soon",
		 * even if a packet or two is in already in flight after we
		 * disabled the port.
		 */
		ipath_write_kreg_port(dd,
		        dd->ipath_kregs->kr_rcvhdrtailaddr, port,
			dd->ipath_dummy_hdrq_phys);
		ipath_write_kreg_port(dd, dd->ipath_kregs->kr_rcvhdraddr,
			pd->port_port, dd->ipath_dummy_hdrq_phys);

		i = dd->ipath_pbufsport * (port - 1);
		ipath_disarm_piobufs(dd, i, dd->ipath_pbufsport);

		dd->ipath_f_clear_tids(dd, pd->port_port);

		if (dd->ipath_pageshadow)
			unlock_expected_tids(pd);
		ipath_stats.sps_ports--;
		ipath_cdbg(PROC, "%s[%u] closed port %u:%u\n",
			   pd->port_comm, pd->port_pid,
			   dd->ipath_unit, port);
	}

	pd->port_pid = 0;
	dd->ipath_pd[pd->port_port] = NULL; /* before releasing mutex */
	mutex_unlock(&ipath_mutex);
	ipath_free_pddata(dd, pd); /* after releasing the mutex */

bail:
	kfree(fd);
	return ret;
}

static int ipath_port_info(struct ipath_portdata *pd, u16 subport,
			   struct ipath_port_info __user *uinfo)
{
	struct ipath_port_info info;
	int nup;
	int ret;
	size_t sz;

	(void) ipath_count_units(NULL, &nup, NULL);
	info.num_active = nup;
	info.unit = pd->port_dd->ipath_unit;
	info.port = pd->port_port;
	info.subport = subport;
	/* Don't return new fields if old library opened the port. */
	if (ipath_supports_subports(pd->userversion >> 16,
				    pd->userversion & 0xffff)) {
		/* Number of user ports available for this device. */
		info.num_ports = pd->port_dd->ipath_cfgports - 1;
		info.num_subports = pd->port_subport_cnt;
		sz = sizeof(info);
	} else
		sz = sizeof(info) - 2 * sizeof(u16);

	if (copy_to_user(uinfo, &info, sz)) {
		ret = -EFAULT;
		goto bail;
	}
	ret = 0;

bail:
	return ret;
}

static int ipath_get_slave_info(struct ipath_portdata *pd,
				void __user *slave_mask_addr)
{
	int ret = 0;

	if (copy_to_user(slave_mask_addr, &pd->active_slaves, sizeof(u32)))
		ret = -EFAULT;
	return ret;
}

static int ipath_force_pio_avail_update(struct ipath_devdata *dd)
{
	u64 reg = dd->ipath_sendctrl;

	clear_bit(IPATH_S_PIOBUFAVAILUPD, &reg);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl, reg);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl, dd->ipath_sendctrl);

	return 0;
}

static ssize_t ipath_write(struct file *fp, const char __user *data,
			   size_t count, loff_t *off)
{
	const struct ipath_cmd __user *ucmd;
	struct ipath_portdata *pd;
	const void __user *src;
	size_t consumed, copy;
	struct ipath_cmd cmd;
	ssize_t ret = 0;
	void *dest;

	if (count < sizeof(cmd.type)) {
		ret = -EINVAL;
		goto bail;
	}

	ucmd = (const struct ipath_cmd __user *) data;

	if (copy_from_user(&cmd.type, &ucmd->type, sizeof(cmd.type))) {
		ret = -EFAULT;
		goto bail;
	}

	consumed = sizeof(cmd.type);

	switch (cmd.type) {
	case IPATH_CMD_ASSIGN_PORT:
	case __IPATH_CMD_USER_INIT:
	case IPATH_CMD_USER_INIT:
		copy = sizeof(cmd.cmd.user_info);
		dest = &cmd.cmd.user_info;
		src = &ucmd->cmd.user_info;
		break;
	case IPATH_CMD_RECV_CTRL:
		copy = sizeof(cmd.cmd.recv_ctrl);
		dest = &cmd.cmd.recv_ctrl;
		src = &ucmd->cmd.recv_ctrl;
		break;
	case IPATH_CMD_PORT_INFO:
		copy = sizeof(cmd.cmd.port_info);
		dest = &cmd.cmd.port_info;
		src = &ucmd->cmd.port_info;
		break;
	case IPATH_CMD_TID_UPDATE:
	case IPATH_CMD_TID_FREE:
		copy = sizeof(cmd.cmd.tid_info);
		dest = &cmd.cmd.tid_info;
		src = &ucmd->cmd.tid_info;
		break;
	case IPATH_CMD_SET_PART_KEY:
		copy = sizeof(cmd.cmd.part_key);
		dest = &cmd.cmd.part_key;
		src = &ucmd->cmd.part_key;
		break;
	case __IPATH_CMD_SLAVE_INFO:
		copy = sizeof(cmd.cmd.slave_mask_addr);
		dest = &cmd.cmd.slave_mask_addr;
		src = &ucmd->cmd.slave_mask_addr;
		break;
	case IPATH_CMD_PIOAVAILUPD:	// force an update of PIOAvail reg
		copy = 0;
		src = NULL;
		dest = NULL;
		break;
	case IPATH_CMD_POLL_TYPE:
		copy = sizeof(cmd.cmd.poll_type);
		dest = &cmd.cmd.poll_type;
		src = &ucmd->cmd.poll_type;
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

	pd = port_fp(fp);
	if (!pd && cmd.type != __IPATH_CMD_USER_INIT &&
		cmd.type != IPATH_CMD_ASSIGN_PORT) {
		ret = -EINVAL;
		goto bail;
	}

	switch (cmd.type) {
	case IPATH_CMD_ASSIGN_PORT:
		ret = ipath_assign_port(fp, &cmd.cmd.user_info);
		if (ret)
			goto bail;
		break;
	case __IPATH_CMD_USER_INIT:
		/* backwards compatibility, get port first */
		ret = ipath_assign_port(fp, &cmd.cmd.user_info);
		if (ret)
			goto bail;
		/* and fall through to current version. */
	case IPATH_CMD_USER_INIT:
		ret = ipath_do_user_init(fp, &cmd.cmd.user_info);
		if (ret)
			goto bail;
		ret = ipath_get_base_info(
			fp, (void __user *) (unsigned long)
			cmd.cmd.user_info.spu_base_info,
			cmd.cmd.user_info.spu_base_info_size);
		break;
	case IPATH_CMD_RECV_CTRL:
		ret = ipath_manage_rcvq(pd, subport_fp(fp), cmd.cmd.recv_ctrl);
		break;
	case IPATH_CMD_PORT_INFO:
		ret = ipath_port_info(pd, subport_fp(fp),
				      (struct ipath_port_info __user *)
				      (unsigned long) cmd.cmd.port_info);
		break;
	case IPATH_CMD_TID_UPDATE:
		ret = ipath_tid_update(pd, fp, &cmd.cmd.tid_info);
		break;
	case IPATH_CMD_TID_FREE:
		ret = ipath_tid_free(pd, subport_fp(fp), &cmd.cmd.tid_info);
		break;
	case IPATH_CMD_SET_PART_KEY:
		ret = ipath_set_part_key(pd, cmd.cmd.part_key);
		break;
	case __IPATH_CMD_SLAVE_INFO:
		ret = ipath_get_slave_info(pd,
					   (void __user *) (unsigned long)
					   cmd.cmd.slave_mask_addr);
		break;
	case IPATH_CMD_PIOAVAILUPD:
		ret = ipath_force_pio_avail_update(pd->port_dd);
		break;
	case IPATH_CMD_POLL_TYPE:
		pd->poll_type = cmd.cmd.poll_type;
		break;
	}

	if (ret >= 0)
		ret = consumed;

bail:
	return ret;
}

static struct class *ipath_class;

static int init_cdev(int minor, char *name, const struct file_operations *fops,
		     struct cdev **cdevp, struct class_device **class_devp)
{
	const dev_t dev = MKDEV(IPATH_MAJOR, minor);
	struct cdev *cdev = NULL;
	struct class_device *class_dev = NULL;
	int ret;

	cdev = cdev_alloc();
	if (!cdev) {
		printk(KERN_ERR IPATH_DRV_NAME
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
		printk(KERN_ERR IPATH_DRV_NAME
		       ": Could not add cdev for minor %d, %s (err %d)\n",
		       minor, name, -ret);
		goto err_cdev;
	}

	class_dev = class_device_create(ipath_class, NULL, dev, NULL, name);

	if (IS_ERR(class_dev)) {
		ret = PTR_ERR(class_dev);
		printk(KERN_ERR IPATH_DRV_NAME ": Could not create "
		       "class_dev for minor %d, %s (err %d)\n",
		       minor, name, -ret);
		goto err_cdev;
	}

	goto done;

err_cdev:
	cdev_del(cdev);
	cdev = NULL;

done:
	if (ret >= 0) {
		*cdevp = cdev;
		*class_devp = class_dev;
	} else {
		*cdevp = NULL;
		*class_devp = NULL;
	}

	return ret;
}

int ipath_cdev_init(int minor, char *name, const struct file_operations *fops,
		    struct cdev **cdevp, struct class_device **class_devp)
{
	return init_cdev(minor, name, fops, cdevp, class_devp);
}

static void cleanup_cdev(struct cdev **cdevp,
			 struct class_device **class_devp)
{
	struct class_device *class_dev = *class_devp;

	if (class_dev) {
		class_device_unregister(class_dev);
		*class_devp = NULL;
	}

	if (*cdevp) {
		cdev_del(*cdevp);
		*cdevp = NULL;
	}
}

void ipath_cdev_cleanup(struct cdev **cdevp,
			struct class_device **class_devp)
{
	cleanup_cdev(cdevp, class_devp);
}

static struct cdev *wildcard_cdev;
static struct class_device *wildcard_class_dev;

static const dev_t dev = MKDEV(IPATH_MAJOR, 0);

static int user_init(void)
{
	int ret;

	ret = register_chrdev_region(dev, IPATH_NMINORS, IPATH_DRV_NAME);
	if (ret < 0) {
		printk(KERN_ERR IPATH_DRV_NAME ": Could not register "
		       "chrdev region (err %d)\n", -ret);
		goto done;
	}

	ipath_class = class_create(THIS_MODULE, IPATH_DRV_NAME);

	if (IS_ERR(ipath_class)) {
		ret = PTR_ERR(ipath_class);
		printk(KERN_ERR IPATH_DRV_NAME ": Could not create "
		       "device class (err %d)\n", -ret);
		goto bail;
	}

	goto done;
bail:
	unregister_chrdev_region(dev, IPATH_NMINORS);
done:
	return ret;
}

static void user_cleanup(void)
{
	if (ipath_class) {
		class_destroy(ipath_class);
		ipath_class = NULL;
	}

	unregister_chrdev_region(dev, IPATH_NMINORS);
}

static atomic_t user_count = ATOMIC_INIT(0);
static atomic_t user_setup = ATOMIC_INIT(0);

int ipath_user_add(struct ipath_devdata *dd)
{
	char name[10];
	int ret;

	if (atomic_inc_return(&user_count) == 1) {
		ret = user_init();
		if (ret < 0) {
			ipath_dev_err(dd, "Unable to set up user support: "
				      "error %d\n", -ret);
			goto bail;
		}
		ret = init_cdev(0, "ipath", &ipath_file_ops, &wildcard_cdev,
				&wildcard_class_dev);
		if (ret < 0) {
			ipath_dev_err(dd, "Could not create wildcard "
				      "minor: error %d\n", -ret);
			goto bail_user;
		}

		atomic_set(&user_setup, 1);
	}

	snprintf(name, sizeof(name), "ipath%d", dd->ipath_unit);

	ret = init_cdev(dd->ipath_unit + 1, name, &ipath_file_ops,
			&dd->user_cdev, &dd->user_class_dev);
	if (ret < 0)
		ipath_dev_err(dd, "Could not create user minor %d, %s\n",
			      dd->ipath_unit + 1, name);

	goto bail;

bail_user:
	user_cleanup();
bail:
	return ret;
}

void ipath_user_remove(struct ipath_devdata *dd)
{
	cleanup_cdev(&dd->user_cdev, &dd->user_class_dev);

	if (atomic_dec_return(&user_count) == 0) {
		if (atomic_read(&user_setup) == 0)
			goto bail;

		cleanup_cdev(&wildcard_cdev, &wildcard_class_dev);
		user_cleanup();

		atomic_set(&user_setup, 0);
	}
bail:
	return;
}

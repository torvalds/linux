/*
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
#include "ips_common.h"
#include "ipath_layer.h"

static int ipath_open(struct inode *, struct file *);
static int ipath_close(struct inode *, struct file *);
static ssize_t ipath_write(struct file *, const char __user *, size_t,
			   loff_t *);
static unsigned int ipath_poll(struct file *, struct poll_table_struct *);
static int ipath_mmap(struct file *, struct vm_area_struct *);

static struct file_operations ipath_file_ops = {
	.owner = THIS_MODULE,
	.write = ipath_write,
	.open = ipath_open,
	.release = ipath_close,
	.poll = ipath_poll,
	.mmap = ipath_mmap
};

static int ipath_get_base_info(struct ipath_portdata *pd,
			       void __user *ubase, size_t ubase_size)
{
	int ret = 0;
	struct ipath_base_info *kinfo = NULL;
	struct ipath_devdata *dd = pd->port_dd;

	if (ubase_size < sizeof(*kinfo)) {
		ipath_cdbg(PROC,
			   "Base size %lu, need %lu (version mismatch?)\n",
			   (unsigned long) ubase_size,
			   (unsigned long) sizeof(*kinfo));
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
	kinfo->spi_tidcnt = dd->ipath_rcvtidcnt;
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
	 * no doubt some future kernel release will chang that, and we'll be
	 * on to yet another method of dealing with this
	 */
	kinfo->spi_rcvhdr_base = (u64) pd->port_rcvhdrq_phys;
	kinfo->spi_rcv_egrbufs = (u64) pd->port_rcvegr_phys;
	kinfo->spi_pioavailaddr = (u64) dd->ipath_pioavailregs_phys;
	kinfo->spi_status = (u64) kinfo->spi_pioavailaddr +
		(void *) dd->ipath_statusp -
		(void *) dd->ipath_pioavailregs_dma;
	kinfo->spi_piobufbase = (u64) pd->port_piobufs;
	kinfo->__spi_uregbase =
		dd->ipath_uregbase + dd->ipath_palign * pd->port_port;

	kinfo->spi_pioindex = dd->ipath_pbufsport * (pd->port_port - 1);
	kinfo->spi_piocnt = dd->ipath_pbufsport;
	kinfo->spi_pioalign = dd->ipath_palign;

	kinfo->spi_qpair = IPATH_KD_QP;
	kinfo->spi_piosize = dd->ipath_ibmaxlen;
	kinfo->spi_mtu = dd->ipath_ibmaxlen;	/* maxlen, not ibmtu */
	kinfo->spi_port = pd->port_port;
	kinfo->spi_sw_version = IPATH_KERN_SWVERSION;
	kinfo->spi_hw_version = dd->ipath_revision;

	if (copy_to_user(ubase, kinfo, sizeof(*kinfo)))
		ret = -EFAULT;

bail:
	kfree(kinfo);
	return ret;
}

/**
 * ipath_tid_update - update a port TID
 * @pd: the port
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
static int ipath_tid_update(struct ipath_portdata *pd,
			    const struct ipath_tid_info *ti)
{
	int ret = 0, ntids;
	u32 tid, porttid, cnt, i, tidcnt;
	u16 *tidlist;
	struct ipath_devdata *dd = pd->port_dd;
	u64 physaddr;
	unsigned long vaddr;
	u64 __iomem *tidbase;
	unsigned long tidmap[8];
	struct page **pagep = NULL;

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
	tidcnt = dd->ipath_rcvtidcnt;
	if (cnt >= tidcnt) {
		/* make sure it all fits in port_tid_pg_list */
		dev_info(&dd->pcidev->dev, "Process tried to allocate %u "
			 "TIDs, only trying max (%u)\n", cnt, tidcnt);
		cnt = tidcnt;
	}
	pagep = (struct page **)pd->port_tid_pg_list;
	tidlist = (u16 *) (&pagep[cnt]);

	memset(tidmap, 0, sizeof(tidmap));
	tid = pd->port_tidcursor;
	/* before decrement; chip actual # */
	porttid = pd->port_port * tidcnt;
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
		tidlist[i] = tid;
		ipath_cdbg(VERBOSE, "Updating idx %u to TID %u, "
			   "vaddr %lx\n", i, tid, vaddr);
		/* we "know" system pages and TID pages are same size */
		dd->ipath_pageshadow[porttid + tid] = pagep[i];
		/*
		 * don't need atomic or it's overhead
		 */
		__set_bit(tid, tidmap);
		physaddr = page_to_phys(pagep[i]);
		ipath_stats.sps_pagelocks++;
		ipath_cdbg(VERBOSE,
			   "TID %u, vaddr %lx, physaddr %llx pgp %p\n",
			   tid, vaddr, (unsigned long long) physaddr,
			   pagep[i]);
		dd->ipath_f_put_tid(dd, &tidbase[tid], 1, physaddr);
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
				dd->ipath_f_put_tid(dd, &tidbase[tid], 1,
						    dd->ipath_tidinvalid);
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
		pd->port_tidcursor = tid;
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

static int ipath_tid_free(struct ipath_portdata *pd,
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
	tidbase = (u64 __iomem *) ((char __iomem *)(dd->ipath_kregbase) +
				   dd->ipath_rcvtidbase +
				   porttid * sizeof(*tidbase));

	tidcnt = dd->ipath_rcvtidcnt;
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
			ipath_cdbg(VERBOSE, "PID %u freeing TID %u\n",
				   pd->port_pid, tid);
			dd->ipath_f_put_tid(dd, &tidbase[tid], 1,
					    dd->ipath_tidinvalid);
			ipath_release_user_pages(
				&dd->ipath_pageshadow[porttid + tid], 1);
			dd->ipath_pageshadow[porttid + tid] = NULL;
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

	if (lkey == (IPS_DEFAULT_P_KEY & 0x7FFF)) {
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
 * @start_stop: action to carry out
 *
 * start_stop == 0 disables receive on the port, for use in queue
 * overflow conditions.  start_stop==1 re-enables, to be used to
 * re-init the software copy of the head register
 */
static int ipath_manage_rcvq(struct ipath_portdata *pd, int start_stop)
{
	struct ipath_devdata *dd = pd->port_dd;
	u64 tval;

	ipath_cdbg(PROC, "%sabling rcv for unit %u port %u\n",
		   start_stop ? "en" : "dis", dd->ipath_unit,
		   pd->port_port);
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
		*pd->port_rcvhdrtail_kvaddr = 0;
		set_bit(INFINIPATH_R_PORTENABLE_SHIFT + pd->port_port,
			&dd->ipath_rcvctrl);
	} else
		clear_bit(INFINIPATH_R_PORTENABLE_SHIFT + pd->port_port,
			  &dd->ipath_rcvctrl);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
			 dd->ipath_rcvctrl);
	/* now be sure chip saw it before we return */
	tval = ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
	if (start_stop) {
		/*
		 * And try to be sure that tail reg update has happened too.
		 * This should in theory interlock with the RXE changes to
		 * the tail register.  Don't assign it to the tail register
		 * in memory copy, since we could overwrite an update by the
		 * chip if we did.
		 */
		tval = ipath_read_ureg32(dd, ur_rcvhdrtail, pd->port_port);
	}
	/* always; new head should be equal to new tail; see above */
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
	unsigned e, egrcnt, alloced, egrperchunk, chunk, egrsize, egroff;
	size_t size;
	int ret;

	egrcnt = dd->ipath_rcvegrcnt;
	/* TID number offset for this port */
	egroff = pd->port_port * egrcnt;
	egrsize = dd->ipath_rcvegrbufsize;
	ipath_cdbg(VERBOSE, "Allocating %d egr buffers, at egrtid "
		   "offset %x, egrsize %u\n", egrcnt, egroff, egrsize);

	/*
	 * to avoid wasting a lot of memory, we allocate 32KB chunks of
	 * physically contiguous memory, advance through it until used up
	 * and then allocate more.  Of course, we need memory to store those
	 * extra pointers, now.  Started out with 256KB, but under heavy
	 * memory pressure (creating large files and then copying them over
	 * NFS while doing lots of MPI jobs), we hit some allocation
	 * failures, even though we can sleep...  (2.6.10) Still get
	 * failures at 64K.  32K is the lowest we can go without waiting
	 * more memory again.  It seems likely that the coalescing in
	 * free_pages, etc. still has issues (as it has had previously
	 * during 2.6.x development).
	 */
	size = 0x8000;
	alloced = ALIGN(egrsize * egrcnt, size);
	egrperchunk = size / egrsize;
	chunk = (egrcnt + egrperchunk - 1) / egrperchunk;
	pd->port_rcvegrbuf_chunks = chunk;
	pd->port_rcvegrbufs_perchunk = egrperchunk;
	pd->port_rcvegrbuf_size = size;
	pd->port_rcvegrbuf = vmalloc(chunk * sizeof(pd->port_rcvegrbuf[0]));
	if (!pd->port_rcvegrbuf) {
		ret = -ENOMEM;
		goto bail;
	}
	pd->port_rcvegrbuf_phys =
		vmalloc(chunk * sizeof(pd->port_rcvegrbuf_phys[0]));
	if (!pd->port_rcvegrbuf_phys) {
		ret = -ENOMEM;
		goto bail_rcvegrbuf;
	}
	for (e = 0; e < pd->port_rcvegrbuf_chunks; e++) {
		/*
		 * GFP_USER, but without GFP_FS, so buffer cache can be
		 * coalesced (we hope); otherwise, even at order 4,
		 * heavy filesystem activity makes these fail
		 */
		gfp_t gfp_flags = __GFP_WAIT | __GFP_IO | __GFP_COMP;

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
					     dd->ipath_rcvegrbase), 0, pa);
			pa += egrsize;
		}
		cond_resched();	/* don't hog the cpu */
	}

	ret = 0;
	goto bail;

bail_rcvegrbuf_phys:
	for (e = 0; e < pd->port_rcvegrbuf_chunks &&
		     pd->port_rcvegrbuf[e]; e++)
		dma_free_coherent(&dd->pcidev->dev, size,
				  pd->port_rcvegrbuf[e],
				  pd->port_rcvegrbuf_phys[e]);

	vfree(pd->port_rcvegrbuf_phys);
	pd->port_rcvegrbuf_phys = NULL;
bail_rcvegrbuf:
	vfree(pd->port_rcvegrbuf);
	pd->port_rcvegrbuf = NULL;
bail:
	return ret;
}

static int ipath_do_user_init(struct ipath_portdata *pd,
			      const struct ipath_user_info *uinfo)
{
	int ret = 0;
	struct ipath_devdata *dd = pd->port_dd;
	u64 physaddr, uaddr, off, atmp;
	struct page *pagep;
	u32 head32;
	u64 head;

	/* for now, if major version is different, bail */
	if ((uinfo->spu_userversion >> 16) != IPATH_USER_SWMAJOR) {
		dev_info(&dd->pcidev->dev,
			 "User major version %d not same as driver "
			 "major %d\n", uinfo->spu_userversion >> 16,
			 IPATH_USER_SWMAJOR);
		ret = -ENODEV;
		goto done;
	}

	if ((uinfo->spu_userversion & 0xffff) != IPATH_USER_SWMINOR)
		ipath_dbg("User minor version %d not same as driver "
			  "minor %d\n", uinfo->spu_userversion & 0xffff,
			  IPATH_USER_SWMINOR);

	if (uinfo->spu_rcvhdrsize) {
		ret = ipath_setrcvhdrsize(dd, uinfo->spu_rcvhdrsize);
		if (ret)
			goto done;
	}

	/* for now we do nothing with rcvhdrcnt: uinfo->spu_rcvhdrcnt */

	/* set up for the rcvhdr Q tail register writeback to user memory */
	if (!uinfo->spu_rcvhdraddr ||
	    !access_ok(VERIFY_WRITE, (u64 __user *) (unsigned long)
		       uinfo->spu_rcvhdraddr, sizeof(u64))) {
		ipath_dbg("Port %d rcvhdrtail addr %llx not valid\n",
			  pd->port_port,
			  (unsigned long long) uinfo->spu_rcvhdraddr);
		ret = -EINVAL;
		goto done;
	}

	off = offset_in_page(uinfo->spu_rcvhdraddr);
	uaddr = PAGE_MASK & (unsigned long) uinfo->spu_rcvhdraddr;
	ret = ipath_get_user_pages_nocopy(uaddr, &pagep);
	if (ret) {
		dev_info(&dd->pcidev->dev, "Failed to lookup and lock "
			 "address %llx for rcvhdrtail: errno %d\n",
			 (unsigned long long) uinfo->spu_rcvhdraddr, -ret);
		goto done;
	}
	ipath_stats.sps_pagelocks++;
	pd->port_rcvhdrtail_uaddr = uaddr;
	pd->port_rcvhdrtail_pagep = pagep;
	pd->port_rcvhdrtail_kvaddr =
		page_address(pagep);
	pd->port_rcvhdrtail_kvaddr += off;
	physaddr = page_to_phys(pagep) + off;
	ipath_cdbg(VERBOSE, "port %d user addr %llx hdrtailaddr, %llx "
		   "physical (off=%llx)\n",
		   pd->port_port,
		   (unsigned long long) uinfo->spu_rcvhdraddr,
		   (unsigned long long) physaddr, (unsigned long long) off);
	ipath_write_kreg_port(dd, dd->ipath_kregs->kr_rcvhdrtailaddr,
			      pd->port_port, physaddr);
	atmp = ipath_read_kreg64_port(dd,
				      dd->ipath_kregs->kr_rcvhdrtailaddr,
				      pd->port_port);
	if (physaddr != atmp) {
		ipath_dev_err(dd,
			      "Catastrophic software error, "
			      "RcvHdrTailAddr%u written as %llx, "
			      "read back as %llx\n", pd->port_port,
			      (unsigned long long) physaddr,
			      (unsigned long long) atmp);
		ret = -EINVAL;
		goto done;
	}

	/* for right now, kernel piobufs are at end, so port 1 is at 0 */
	pd->port_piobufs = dd->ipath_piobufbase +
		dd->ipath_pbufsport * (pd->port_port -
				       1) * dd->ipath_palign;
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
	/* enable receives now */
	/* atomically set enable bit for this port */
	set_bit(INFINIPATH_R_PORTENABLE_SHIFT + pd->port_port,
		&dd->ipath_rcvctrl);

	/*
	 * set the head registers for this port to the current values
	 * of the tail pointers, since we don't know if they were
	 * updated on last use of the port.
	 */
	head32 = ipath_read_ureg32(dd, ur_rcvhdrtail, pd->port_port);
	head = (u64) head32;
	ipath_write_ureg(dd, ur_rcvhdrhead, head, pd->port_port);
	head32 = ipath_read_ureg32(dd, ur_rcvegrindextail, pd->port_port);
	ipath_write_ureg(dd, ur_rcvegrindexhead, head32, pd->port_port);
	dd->ipath_lastegrheads[pd->port_port] = -1;
	dd->ipath_lastrcvhdrqtails[pd->port_port] = -1;
	ipath_cdbg(VERBOSE, "Wrote port%d head %llx, egrhead %x from "
		   "tail regs\n", pd->port_port,
		   (unsigned long long) head, head32);
	pd->port_tidcursor = 0;	/* start at beginning after open */
	/*
	 * now enable the port; the tail registers will be written to memory
	 * by the chip as soon as it sees the write to
	 * dd->ipath_kregs->kr_rcvctrl.  The update only happens on
	 * transition from 0 to 1, so clear it first, then set it as part of
	 * enabling the port.  This will (very briefly) affect any other
	 * open ports, but it shouldn't be long enough to be an issue.
	 */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
			 dd->ipath_rcvctrl & ~INFINIPATH_R_TAILUPD);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
			 dd->ipath_rcvctrl);

done:
	return ret;
}

static int mmap_ureg(struct vm_area_struct *vma, struct ipath_devdata *dd,
		     u64 ureg)
{
	unsigned long phys;
	int ret;

	/* it's the real hardware, so io_remap works */

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
			struct ipath_portdata *pd)
{
	unsigned long phys;
	int ret;

	/*
	 * When we map the PIO buffers, we want to map them as writeonly, no
	 * read possible.
	 */

	if ((vma->vm_end - vma->vm_start) >
	    (dd->ipath_pbufsport * dd->ipath_palign)) {
		dev_info(&dd->pcidev->dev, "FAIL mmap piobufs: "
			 "reqlen %lx > PAGE\n",
			 vma->vm_end - vma->vm_start);
		ret = -EFAULT;
		goto bail;
	}

	phys = dd->ipath_physaddr + pd->port_piobufs;
	/*
	 * Do *NOT* mark this as non-cached (PWT bit), or we don't get the
	 * write combining behavior we want on the PIO buffers!
	 * vma->vm_page_prot =
	 *        pgprot_noncached(vma->vm_page_prot);
	 */

	if (vma->vm_flags & VM_READ) {
		dev_info(&dd->pcidev->dev,
			 "Can't map piobufs as readable (flags=%lx)\n",
			 vma->vm_flags);
		ret = -EPERM;
		goto bail;
	}

	/* don't allow them to later change to readable with mprotect */

	vma->vm_flags &= ~VM_MAYWRITE;
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
	dma_addr_t *phys;
	int ret;

	if (!pd->port_rcvegrbuf) {
		ret = -EFAULT;
		goto bail;
	}

	size = pd->port_rcvegrbuf_size;
	total_size = pd->port_rcvegrbuf_chunks * size;
	if ((vma->vm_end - vma->vm_start) > total_size) {
		dev_info(&dd->pcidev->dev, "FAIL on egr bufs: "
			 "reqlen %lx > actual %lx\n",
			 vma->vm_end - vma->vm_start,
			 (unsigned long) total_size);
		ret = -EFAULT;
		goto bail;
	}

	if (vma->vm_flags & VM_WRITE) {
		dev_info(&dd->pcidev->dev, "Can't map eager buffers as "
			 "writable (flags=%lx)\n", vma->vm_flags);
		ret = -EPERM;
		goto bail;
	}

	start = vma->vm_start;
	phys = pd->port_rcvegrbuf_phys;

	/* don't allow them to later change to writeable with mprotect */
	vma->vm_flags &= ~VM_MAYWRITE;

	for (i = 0; i < pd->port_rcvegrbuf_chunks; i++, start += size) {
		ret = remap_pfn_range(vma, start, phys[i] >> PAGE_SHIFT,
				      size, vma->vm_page_prot);
		if (ret < 0)
			goto bail;
	}
	ret = 0;

bail:
	return ret;
}

static int mmap_rcvhdrq(struct vm_area_struct *vma,
			struct ipath_portdata *pd)
{
	struct ipath_devdata *dd = pd->port_dd;
	size_t total_size;
	int ret;

	/*
	 * kmalloc'ed memory, physically contiguous; this is from
	 * spi_rcvhdr_base; we allow user to map read-write so they can
	 * write hdrq entries to allow protocol code to directly poll
	 * whether a hdrq entry has been written.
	 */
	total_size = ALIGN(dd->ipath_rcvhdrcnt * dd->ipath_rcvhdrentsize *
			   sizeof(u32), PAGE_SIZE);
	if ((vma->vm_end - vma->vm_start) > total_size) {
		dev_info(&dd->pcidev->dev,
			 "FAIL on rcvhdrq: reqlen %lx > actual %lx\n",
			 vma->vm_end - vma->vm_start,
			 (unsigned long) total_size);
		ret = -EFAULT;
		goto bail;
	}

	ret = remap_pfn_range(vma, vma->vm_start,
			      pd->port_rcvhdrq_phys >> PAGE_SHIFT,
			      vma->vm_end - vma->vm_start,
			      vma->vm_page_prot);
bail:
	return ret;
}

static int mmap_pioavailregs(struct vm_area_struct *vma,
			     struct ipath_portdata *pd)
{
	struct ipath_devdata *dd = pd->port_dd;
	int ret;

	/*
	 * when we map the PIO bufferavail registers, we want to map them as
	 * readonly, no write possible.
	 *
	 * kmalloc'ed memory, physically contiguous, one page only, readonly
	 */

	if ((vma->vm_end - vma->vm_start) > PAGE_SIZE) {
		dev_info(&dd->pcidev->dev, "FAIL on pioavailregs_dma: "
			 "reqlen %lx > actual %lx\n",
			 vma->vm_end - vma->vm_start,
			 (unsigned long) PAGE_SIZE);
		ret = -EFAULT;
		goto bail;
	}

	if (vma->vm_flags & VM_WRITE) {
		dev_info(&dd->pcidev->dev,
			 "Can't map pioavailregs as writable (flags=%lx)\n",
			 vma->vm_flags);
		ret = -EPERM;
		goto bail;
	}

	/* don't allow them to later change with mprotect */
	vma->vm_flags &= ~VM_MAYWRITE;

	ret = remap_pfn_range(vma, vma->vm_start,
			      dd->ipath_pioavailregs_phys >> PAGE_SHIFT,
			      PAGE_SIZE, vma->vm_page_prot);
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
	int ret;

	pd = port_fp(fp);
	dd = pd->port_dd;
	/*
	 * This is the ipath_do_user_init() code, mapping the shared buffers
	 * into the user process. The address referred to by vm_pgoff is the
	 * virtual, not physical, address; we only do one mmap for each
	 * space mapped.
	 */
	pgaddr = vma->vm_pgoff << PAGE_SHIFT;

	/*
	 * note that ureg does *NOT* have the kregvirt as part of it, to be
	 * sure that for 32 bit programs, we don't end up trying to map a >
	 * 44 address.  Has to match ipath_get_base_info() code that sets
	 * __spi_uregbase
	 */

	ureg = dd->ipath_uregbase + dd->ipath_palign * pd->port_port;

	ipath_cdbg(MM, "ushare: pgaddr %llx vm_start=%lx, vmlen %lx\n",
		   (unsigned long long) pgaddr, vma->vm_start,
		   vma->vm_end - vma->vm_start);

	if (pgaddr == ureg)
		ret = mmap_ureg(vma, dd, ureg);
	else if (pgaddr == pd->port_piobufs)
		ret = mmap_piobufs(vma, dd, pd);
	else if (pgaddr == (u64) pd->port_rcvegr_phys)
		ret = mmap_rcvegrbufs(vma, pd);
	else if (pgaddr == (u64) pd->port_rcvhdrq_phys)
		ret = mmap_rcvhdrq(vma, pd);
	else if (pgaddr == dd->ipath_pioavailregs_phys)
		ret = mmap_pioavailregs(vma, pd);
	else
		ret = -EINVAL;

	vma->vm_private_data = NULL;

	if (ret < 0)
		dev_info(&dd->pcidev->dev,
			 "Failure %d on addr %lx, off %lx\n",
			 -ret, vma->vm_start, vma->vm_pgoff);

	return ret;
}

static unsigned int ipath_poll(struct file *fp,
			       struct poll_table_struct *pt)
{
	struct ipath_portdata *pd;
	u32 head, tail;
	int bit;
	struct ipath_devdata *dd;

	pd = port_fp(fp);
	dd = pd->port_dd;

	bit = pd->port_port + INFINIPATH_R_INTRAVAIL_SHIFT;
	set_bit(bit, &dd->ipath_rcvctrl);

	/*
	 * Before blocking, make sure that head is still == tail,
	 * reading from the chip, so we can be sure the interrupt
	 * enable has made it to the chip.  If not equal, disable
	 * interrupt again and return immediately.  This avoids races,
	 * and the overhead of the chip read doesn't matter much at
	 * this point, since we are waiting for something anyway.
	 */

	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
			 dd->ipath_rcvctrl);

	head = ipath_read_ureg32(dd, ur_rcvhdrhead, pd->port_port);
	tail = ipath_read_ureg32(dd, ur_rcvhdrtail, pd->port_port);

	if (tail == head) {
		set_bit(IPATH_PORT_WAITING_RCV, &pd->port_flag);
		if(dd->ipath_rhdrhead_intr_off) /* arm rcv interrupt */
			(void)ipath_write_ureg(dd, ur_rcvhdrhead,
					       dd->ipath_rhdrhead_intr_off
					       | head, pd->port_port);
		poll_wait(fp, &pd->port_wait, pt);

		if (test_bit(IPATH_PORT_WAITING_RCV, &pd->port_flag)) {
			/* timed out, no packets received */
			clear_bit(IPATH_PORT_WAITING_RCV, &pd->port_flag);
			pd->port_rcvwait_to++;
		}
	}
	else {
		/* it's already happened; don't do wait_event overhead */
		pd->port_rcvnowait++;
	}

	clear_bit(bit, &dd->ipath_rcvctrl);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
			 dd->ipath_rcvctrl);

	return 0;
}

static int try_alloc_port(struct ipath_devdata *dd, int port,
			  struct file *fp)
{
	int ret;

	if (!dd->ipath_pd[port]) {
		void *p, *ptmp;

		p = kzalloc(sizeof(struct ipath_portdata), GFP_KERNEL);

		/*
		 * Allocate memory for use in ipath_tid_update() just once
		 * at open, not per call.  Reduces cost of expected send
		 * setup.
		 */
		ptmp = kmalloc(dd->ipath_rcvtidcnt * sizeof(u16) +
			       dd->ipath_rcvtidcnt * sizeof(struct page **),
			       GFP_KERNEL);
		if (!p || !ptmp) {
			ipath_dev_err(dd, "Unable to allocate portdata "
				      "memory, failing open\n");
			ret = -ENOMEM;
			kfree(p);
			kfree(ptmp);
			goto bail;
		}
		dd->ipath_pd[port] = p;
		dd->ipath_pd[port]->port_port = port;
		dd->ipath_pd[port]->port_dd = dd;
		dd->ipath_pd[port]->port_tid_pg_list = ptmp;
		init_waitqueue_head(&dd->ipath_pd[port]->port_wait);
	}
	if (!dd->ipath_pd[port]->port_cnt) {
		dd->ipath_pd[port]->port_cnt = 1;
		fp->private_data = (void *) dd->ipath_pd[port];
		ipath_cdbg(PROC, "%s[%u] opened unit:port %u:%u\n",
			   current->comm, current->pid, dd->ipath_unit,
			   port);
		dd->ipath_pd[port]->port_pid = current->pid;
		strncpy(dd->ipath_pd[port]->port_comm, current->comm,
			sizeof(dd->ipath_pd[port]->port_comm));
		ipath_stats.sps_ports++;
		ret = 0;
		goto bail;
	}
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

static int find_free_port(int unit, struct file *fp)
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

	for (i = 0; i < dd->ipath_cfgports; i++) {
		ret = try_alloc_port(dd, i, fp);
		if (ret != -EBUSY)
			goto bail;
	}
	ret = -EBUSY;

bail:
	return ret;
}

static int find_best_unit(struct file *fp)
{
	int ret = 0, i, prefunit = -1, devmax;
	int maxofallports, npresent, nup;
	int ndev;

	(void) ipath_count_units(&npresent, &nup, &maxofallports);

	/*
	 * This code is present to allow a knowledgeable person to
	 * specify the layout of processes to processors before opening
	 * this driver, and then we'll assign the process to the "closest"
	 * HT-400 to that processor (we assume reasonable connectivity,
	 * for now).  This code assumes that if affinity has been set
	 * before this point, that at most one cpu is set; for now this
	 * is reasonable.  I check for both cpus_empty() and cpus_full(),
	 * in case some kernel variant sets none of the bits when no
	 * affinity is set.  2.6.11 and 12 kernels have all present
	 * cpus set.  Some day we'll have to fix it up further to handle
	 * a cpu subset.  This algorithm fails for two HT-400's connected
	 * in tunnel fashion.  Eventually this needs real topology
	 * information.  There may be some issues with dual core numbering
	 * as well.  This needs more work prior to release.
	 */
	if (!cpus_empty(current->cpus_allowed) &&
	    !cpus_full(current->cpus_allowed)) {
		int ncpus = num_online_cpus(), curcpu = -1;
		for (i = 0; i < ncpus; i++)
			if (cpu_isset(i, current->cpus_allowed)) {
				ipath_cdbg(PROC, "%s[%u] affinity set for "
					   "cpu %d\n", current->comm,
					   current->pid, i);
				curcpu = i;
			}
		if (curcpu != -1) {
			if (npresent) {
				prefunit = curcpu / (ncpus / npresent);
				ipath_dbg("%s[%u] %d chips, %d cpus, "
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
	else
		devmax = ipath_count_units(NULL, NULL, NULL);
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
			ret = try_alloc_port(dd, i, fp);
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

static int ipath_open(struct inode *in, struct file *fp)
{
	int ret, minor;

	mutex_lock(&ipath_mutex);

	minor = iminor(in);
	ipath_cdbg(VERBOSE, "open on dev %lx (minor %d)\n",
		   (long)in->i_rdev, minor);

	if (minor)
		ret = find_free_port(minor - 1, fp);
	else
		ret = find_best_unit(fp);

	mutex_unlock(&ipath_mutex);
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
	struct ipath_portdata *pd;
	struct ipath_devdata *dd;
	unsigned port;

	ipath_cdbg(VERBOSE, "close on dev %lx, private data %p\n",
		   (long)in->i_rdev, fp->private_data);

	mutex_lock(&ipath_mutex);

	pd = port_fp(fp);
	port = pd->port_port;
	fp->private_data = NULL;
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
		if (pd->port_rcvhdrtail_uaddr) {
			pd->port_rcvhdrtail_uaddr = 0;
			pd->port_rcvhdrtail_kvaddr = NULL;
			ipath_release_user_pages_on_close(
				&pd->port_rcvhdrtail_pagep, 1);
			pd->port_rcvhdrtail_pagep = NULL;
			ipath_stats.sps_pageunlocks++;
		}
		ipath_write_kreg_port(
			dd, dd->ipath_kregs->kr_rcvhdrtailaddr,
			port, 0ULL);
		ipath_write_kreg_port(
			dd, dd->ipath_kregs->kr_rcvhdraddr,
			pd->port_port, 0);

		/* clean up the pkeys for this port user */
		ipath_clean_part_key(pd, dd);

		if (port < dd->ipath_cfgports) {
			int i = dd->ipath_pbufsport * (port - 1);
			ipath_disarm_piobufs(dd, i, dd->ipath_pbufsport);

			/* atomically clear receive enable port. */
			clear_bit(INFINIPATH_R_PORTENABLE_SHIFT + port,
				  &dd->ipath_rcvctrl);
			ipath_write_kreg(
				dd,
				dd->ipath_kregs->kr_rcvctrl,
				dd->ipath_rcvctrl);

			if (dd->ipath_pageshadow)
				unlock_expected_tids(pd);
			ipath_stats.sps_ports--;
			ipath_cdbg(PROC, "%s[%u] closed port %u:%u\n",
				   pd->port_comm, pd->port_pid,
				   dd->ipath_unit, port);
		}
	}

	pd->port_cnt = 0;
	pd->port_pid = 0;

	dd->ipath_f_clear_tids(dd, pd->port_port);

	ipath_free_pddata(dd, pd->port_port, 0);

	mutex_unlock(&ipath_mutex);

	return ret;
}

static int ipath_port_info(struct ipath_portdata *pd,
			   struct ipath_port_info __user *uinfo)
{
	struct ipath_port_info info;
	int nup;
	int ret;

	(void) ipath_count_units(NULL, &nup, NULL);
	info.num_active = nup;
	info.unit = pd->port_dd->ipath_unit;
	info.port = pd->port_port;

	if (copy_to_user(uinfo, &info, sizeof(info))) {
		ret = -EFAULT;
		goto bail;
	}
	ret = 0;

bail:
	return ret;
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
	default:
		ret = -EINVAL;
		goto bail;
	}

	if ((count - consumed) < copy) {
		ret = -EINVAL;
		goto bail;
	}

	if (copy_from_user(dest, src, copy)) {
		ret = -EFAULT;
		goto bail;
	}

	consumed += copy;
	pd = port_fp(fp);

	switch (cmd.type) {
	case IPATH_CMD_USER_INIT:
		ret = ipath_do_user_init(pd, &cmd.cmd.user_info);
		if (ret < 0)
			goto bail;
		ret = ipath_get_base_info(
			pd, (void __user *) (unsigned long)
			cmd.cmd.user_info.spu_base_info,
			cmd.cmd.user_info.spu_base_info_size);
		break;
	case IPATH_CMD_RECV_CTRL:
		ret = ipath_manage_rcvq(pd, cmd.cmd.recv_ctrl);
		break;
	case IPATH_CMD_PORT_INFO:
		ret = ipath_port_info(pd,
				      (struct ipath_port_info __user *)
				      (unsigned long) cmd.cmd.port_info);
		break;
	case IPATH_CMD_TID_UPDATE:
		ret = ipath_tid_update(pd, &cmd.cmd.tid_info);
		break;
	case IPATH_CMD_TID_FREE:
		ret = ipath_tid_free(pd, &cmd.cmd.tid_info);
		break;
	case IPATH_CMD_SET_PART_KEY:
		ret = ipath_set_part_key(pd, cmd.cmd.part_key);
		break;
	}

	if (ret >= 0)
		ret = consumed;

bail:
	return ret;
}

static struct class *ipath_class;

static int init_cdev(int minor, char *name, struct file_operations *fops,
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

int ipath_cdev_init(int minor, char *name, struct file_operations *fops,
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
		ret = ipath_diag_init();
		if (ret < 0) {
			ipath_dev_err(dd, "Unable to set up diag support: "
				      "error %d\n", -ret);
			goto bail_sma;
		}

		ret = init_cdev(0, "ipath", &ipath_file_ops, &wildcard_cdev,
				&wildcard_class_dev);
		if (ret < 0) {
			ipath_dev_err(dd, "Could not create wildcard "
				      "minor: error %d\n", -ret);
			goto bail_diag;
		}

		atomic_set(&user_setup, 1);
	}

	snprintf(name, sizeof(name), "ipath%d", dd->ipath_unit);

	ret = init_cdev(dd->ipath_unit + 1, name, &ipath_file_ops,
			&dd->cdev, &dd->class_dev);
	if (ret < 0)
		ipath_dev_err(dd, "Could not create user minor %d, %s\n",
			      dd->ipath_unit + 1, name);

	goto bail;

bail_diag:
	ipath_diag_cleanup();
bail_sma:
	user_cleanup();
bail:
	return ret;
}

void ipath_user_del(struct ipath_devdata *dd)
{
	cleanup_cdev(&dd->cdev, &dd->class_dev);

	if (atomic_dec_return(&user_count) == 0) {
		if (atomic_read(&user_setup) == 0)
			goto bail;

		cleanup_cdev(&wildcard_cdev, &wildcard_class_dev);
		ipath_diag_cleanup();
		user_cleanup();

		atomic_set(&user_setup, 0);
	}
bail:
	return;
}

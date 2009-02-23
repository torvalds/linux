/*
 * Copyright (c) 2006, 2007, 2008 QLogic Corporation. All rights reserved.
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
#include <linux/netdevice.h>
#include <linux/vmalloc.h>

#include "ipath_kernel.h"
#include "ipath_common.h"

/*
 * min buffers we want to have per port, after driver
 */
#define IPATH_MIN_USER_PORT_BUFCNT 7

/*
 * Number of ports we are configured to use (to allow for more pio
 * buffers per port, etc.)  Zero means use chip value.
 */
static ushort ipath_cfgports;

module_param_named(cfgports, ipath_cfgports, ushort, S_IRUGO);
MODULE_PARM_DESC(cfgports, "Set max number of ports to use");

/*
 * Number of buffers reserved for driver (verbs and layered drivers.)
 * Initialized based on number of PIO buffers if not set via module interface.
 * The problem with this is that it's global, but we'll use different
 * numbers for different chip types.
 */
static ushort ipath_kpiobufs;

static int ipath_set_kpiobufs(const char *val, struct kernel_param *kp);

module_param_call(kpiobufs, ipath_set_kpiobufs, param_get_ushort,
		  &ipath_kpiobufs, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(kpiobufs, "Set number of PIO buffers for driver");

/**
 * create_port0_egr - allocate the eager TID buffers
 * @dd: the infinipath device
 *
 * This code is now quite different for user and kernel, because
 * the kernel uses skb's, for the accelerated network performance.
 * This is the kernel (port0) version.
 *
 * Allocate the eager TID buffers and program them into infinipath.
 * We use the network layer alloc_skb() allocator to allocate the
 * memory, and either use the buffers as is for things like verbs
 * packets, or pass the buffers up to the ipath layered driver and
 * thence the network layer, replacing them as we do so (see
 * ipath_rcv_layer()).
 */
static int create_port0_egr(struct ipath_devdata *dd)
{
	unsigned e, egrcnt;
	struct ipath_skbinfo *skbinfo;
	int ret;

	egrcnt = dd->ipath_p0_rcvegrcnt;

	skbinfo = vmalloc(sizeof(*dd->ipath_port0_skbinfo) * egrcnt);
	if (skbinfo == NULL) {
		ipath_dev_err(dd, "allocation error for eager TID "
			      "skb array\n");
		ret = -ENOMEM;
		goto bail;
	}
	for (e = 0; e < egrcnt; e++) {
		/*
		 * This is a bit tricky in that we allocate extra
		 * space for 2 bytes of the 14 byte ethernet header.
		 * These two bytes are passed in the ipath header so
		 * the rest of the data is word aligned.  We allocate
		 * 4 bytes so that the data buffer stays word aligned.
		 * See ipath_kreceive() for more details.
		 */
		skbinfo[e].skb = ipath_alloc_skb(dd, GFP_KERNEL);
		if (!skbinfo[e].skb) {
			ipath_dev_err(dd, "SKB allocation error for "
				      "eager TID %u\n", e);
			while (e != 0)
				dev_kfree_skb(skbinfo[--e].skb);
			vfree(skbinfo);
			ret = -ENOMEM;
			goto bail;
		}
	}
	/*
	 * After loop above, so we can test non-NULL to see if ready
	 * to use at receive, etc.
	 */
	dd->ipath_port0_skbinfo = skbinfo;

	for (e = 0; e < egrcnt; e++) {
		dd->ipath_port0_skbinfo[e].phys =
		  ipath_map_single(dd->pcidev,
				   dd->ipath_port0_skbinfo[e].skb->data,
				   dd->ipath_ibmaxlen, PCI_DMA_FROMDEVICE);
		dd->ipath_f_put_tid(dd, e + (u64 __iomem *)
				    ((char __iomem *) dd->ipath_kregbase +
				     dd->ipath_rcvegrbase),
				    RCVHQ_RCV_TYPE_EAGER,
				    dd->ipath_port0_skbinfo[e].phys);
	}

	ret = 0;

bail:
	return ret;
}

static int bringup_link(struct ipath_devdata *dd)
{
	u64 val, ibc;
	int ret = 0;

	/* hold IBC in reset */
	dd->ipath_control &= ~INFINIPATH_C_LINKENABLE;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_control,
			 dd->ipath_control);

	/*
	 * set initial max size pkt IBC will send, including ICRC; it's the
	 * PIO buffer size in dwords, less 1; also see ipath_set_mtu()
	 */
	val = (dd->ipath_ibmaxlen >> 2) + 1;
	ibc = val << dd->ibcc_mpl_shift;

	/* flowcontrolwatermark is in units of KBytes */
	ibc |= 0x5ULL << INFINIPATH_IBCC_FLOWCTRLWATERMARK_SHIFT;
	/*
	 * How often flowctrl sent.  More or less in usecs; balance against
	 * watermark value, so that in theory senders always get a flow
	 * control update in time to not let the IB link go idle.
	 */
	ibc |= 0x3ULL << INFINIPATH_IBCC_FLOWCTRLPERIOD_SHIFT;
	/* max error tolerance */
	ibc |= 0xfULL << INFINIPATH_IBCC_PHYERRTHRESHOLD_SHIFT;
	/* use "real" buffer space for */
	ibc |= 4ULL << INFINIPATH_IBCC_CREDITSCALE_SHIFT;
	/* IB credit flow control. */
	ibc |= 0xfULL << INFINIPATH_IBCC_OVERRUNTHRESHOLD_SHIFT;
	/* initially come up waiting for TS1, without sending anything. */
	dd->ipath_ibcctrl = ibc;
	/*
	 * Want to start out with both LINKCMD and LINKINITCMD in NOP
	 * (0 and 0).  Don't put linkinitcmd in ipath_ibcctrl, want that
	 * to stay a NOP. Flag that we are disabled, for the (unlikely)
	 * case that some recovery path is trying to bring the link up
	 * before we are ready.
	 */
	ibc |= INFINIPATH_IBCC_LINKINITCMD_DISABLE <<
		INFINIPATH_IBCC_LINKINITCMD_SHIFT;
	dd->ipath_flags |= IPATH_IB_LINK_DISABLED;
	ipath_cdbg(VERBOSE, "Writing 0x%llx to ibcctrl\n",
		   (unsigned long long) ibc);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_ibcctrl, ibc);

	// be sure chip saw it
	val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);

	ret = dd->ipath_f_bringup_serdes(dd);

	if (ret)
		dev_info(&dd->pcidev->dev, "Could not initialize SerDes, "
			 "not usable\n");
	else {
		/* enable IBC */
		dd->ipath_control |= INFINIPATH_C_LINKENABLE;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_control,
				 dd->ipath_control);
	}

	return ret;
}

static struct ipath_portdata *create_portdata0(struct ipath_devdata *dd)
{
	struct ipath_portdata *pd = NULL;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (pd) {
		pd->port_dd = dd;
		pd->port_cnt = 1;
		/* The port 0 pkey table is used by the layer interface. */
		pd->port_pkeys[0] = IPATH_DEFAULT_P_KEY;
		pd->port_seq_cnt = 1;
	}
	return pd;
}

static int init_chip_first(struct ipath_devdata *dd)
{
	struct ipath_portdata *pd;
	int ret = 0;
	u64 val;

	spin_lock_init(&dd->ipath_kernel_tid_lock);
	spin_lock_init(&dd->ipath_user_tid_lock);
	spin_lock_init(&dd->ipath_sendctrl_lock);
	spin_lock_init(&dd->ipath_uctxt_lock);
	spin_lock_init(&dd->ipath_sdma_lock);
	spin_lock_init(&dd->ipath_gpio_lock);
	spin_lock_init(&dd->ipath_eep_st_lock);
	spin_lock_init(&dd->ipath_sdepb_lock);
	mutex_init(&dd->ipath_eep_lock);

	/*
	 * skip cfgports stuff because we are not allocating memory,
	 * and we don't want problems if the portcnt changed due to
	 * cfgports.  We do still check and report a difference, if
	 * not same (should be impossible).
	 */
	dd->ipath_f_config_ports(dd, ipath_cfgports);
	if (!ipath_cfgports)
		dd->ipath_cfgports = dd->ipath_portcnt;
	else if (ipath_cfgports <= dd->ipath_portcnt) {
		dd->ipath_cfgports = ipath_cfgports;
		ipath_dbg("Configured to use %u ports out of %u in chip\n",
			  dd->ipath_cfgports, ipath_read_kreg32(dd,
			  dd->ipath_kregs->kr_portcnt));
	} else {
		dd->ipath_cfgports = dd->ipath_portcnt;
		ipath_dbg("Tried to configured to use %u ports; chip "
			  "only supports %u\n", ipath_cfgports,
			  ipath_read_kreg32(dd,
				  dd->ipath_kregs->kr_portcnt));
	}
	/*
	 * Allocate full portcnt array, rather than just cfgports, because
	 * cleanup iterates across all possible ports.
	 */
	dd->ipath_pd = kzalloc(sizeof(*dd->ipath_pd) * dd->ipath_portcnt,
			       GFP_KERNEL);

	if (!dd->ipath_pd) {
		ipath_dev_err(dd, "Unable to allocate portdata array, "
			      "failing\n");
		ret = -ENOMEM;
		goto done;
	}

	pd = create_portdata0(dd);
	if (!pd) {
		ipath_dev_err(dd, "Unable to allocate portdata for port "
			      "0, failing\n");
		ret = -ENOMEM;
		goto done;
	}
	dd->ipath_pd[0] = pd;

	dd->ipath_rcvtidcnt =
		ipath_read_kreg32(dd, dd->ipath_kregs->kr_rcvtidcnt);
	dd->ipath_rcvtidbase =
		ipath_read_kreg32(dd, dd->ipath_kregs->kr_rcvtidbase);
	dd->ipath_rcvegrcnt =
		ipath_read_kreg32(dd, dd->ipath_kregs->kr_rcvegrcnt);
	dd->ipath_rcvegrbase =
		ipath_read_kreg32(dd, dd->ipath_kregs->kr_rcvegrbase);
	dd->ipath_palign =
		ipath_read_kreg32(dd, dd->ipath_kregs->kr_pagealign);
	dd->ipath_piobufbase =
		ipath_read_kreg64(dd, dd->ipath_kregs->kr_sendpiobufbase);
	val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_sendpiosize);
	dd->ipath_piosize2k = val & ~0U;
	dd->ipath_piosize4k = val >> 32;
	if (dd->ipath_piosize4k == 0 && ipath_mtu4096)
		ipath_mtu4096 = 0; /* 4KB not supported by this chip */
	dd->ipath_ibmtu = ipath_mtu4096 ? 4096 : 2048;
	val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_sendpiobufcnt);
	dd->ipath_piobcnt2k = val & ~0U;
	dd->ipath_piobcnt4k = val >> 32;
	dd->ipath_pio2kbase =
		(u32 __iomem *) (((char __iomem *) dd->ipath_kregbase) +
				 (dd->ipath_piobufbase & 0xffffffff));
	if (dd->ipath_piobcnt4k) {
		dd->ipath_pio4kbase = (u32 __iomem *)
			(((char __iomem *) dd->ipath_kregbase) +
			 (dd->ipath_piobufbase >> 32));
		/*
		 * 4K buffers take 2 pages; we use roundup just to be
		 * paranoid; we calculate it once here, rather than on
		 * ever buf allocate
		 */
		dd->ipath_4kalign = ALIGN(dd->ipath_piosize4k,
					  dd->ipath_palign);
		ipath_dbg("%u 2k(%x) piobufs @ %p, %u 4k(%x) @ %p "
			  "(%x aligned)\n",
			  dd->ipath_piobcnt2k, dd->ipath_piosize2k,
			  dd->ipath_pio2kbase, dd->ipath_piobcnt4k,
			  dd->ipath_piosize4k, dd->ipath_pio4kbase,
			  dd->ipath_4kalign);
	}
	else ipath_dbg("%u 2k piobufs @ %p\n",
		       dd->ipath_piobcnt2k, dd->ipath_pio2kbase);

done:
	return ret;
}

/**
 * init_chip_reset - re-initialize after a reset, or enable
 * @dd: the infinipath device
 *
 * sanity check at least some of the values after reset, and
 * ensure no receive or transmit (explictly, in case reset
 * failed
 */
static int init_chip_reset(struct ipath_devdata *dd)
{
	u32 rtmp;
	int i;
	unsigned long flags;

	/*
	 * ensure chip does no sends or receives, tail updates, or
	 * pioavail updates while we re-initialize
	 */
	dd->ipath_rcvctrl &= ~(1ULL << dd->ipath_r_tailupd_shift);
	for (i = 0; i < dd->ipath_portcnt; i++) {
		clear_bit(dd->ipath_r_portenable_shift + i,
			  &dd->ipath_rcvctrl);
		clear_bit(dd->ipath_r_intravail_shift + i,
			  &dd->ipath_rcvctrl);
	}
	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
		dd->ipath_rcvctrl);

	spin_lock_irqsave(&dd->ipath_sendctrl_lock, flags);
	dd->ipath_sendctrl = 0U; /* no sdma, etc */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl, dd->ipath_sendctrl);
	ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
	spin_unlock_irqrestore(&dd->ipath_sendctrl_lock, flags);

	ipath_write_kreg(dd, dd->ipath_kregs->kr_control, 0ULL);

	rtmp = ipath_read_kreg32(dd, dd->ipath_kregs->kr_rcvtidcnt);
	if (rtmp != dd->ipath_rcvtidcnt)
		dev_info(&dd->pcidev->dev, "tidcnt was %u before "
			 "reset, now %u, using original\n",
			 dd->ipath_rcvtidcnt, rtmp);
	rtmp = ipath_read_kreg32(dd, dd->ipath_kregs->kr_rcvtidbase);
	if (rtmp != dd->ipath_rcvtidbase)
		dev_info(&dd->pcidev->dev, "tidbase was %u before "
			 "reset, now %u, using original\n",
			 dd->ipath_rcvtidbase, rtmp);
	rtmp = ipath_read_kreg32(dd, dd->ipath_kregs->kr_rcvegrcnt);
	if (rtmp != dd->ipath_rcvegrcnt)
		dev_info(&dd->pcidev->dev, "egrcnt was %u before "
			 "reset, now %u, using original\n",
			 dd->ipath_rcvegrcnt, rtmp);
	rtmp = ipath_read_kreg32(dd, dd->ipath_kregs->kr_rcvegrbase);
	if (rtmp != dd->ipath_rcvegrbase)
		dev_info(&dd->pcidev->dev, "egrbase was %u before "
			 "reset, now %u, using original\n",
			 dd->ipath_rcvegrbase, rtmp);

	return 0;
}

static int init_pioavailregs(struct ipath_devdata *dd)
{
	int ret;

	dd->ipath_pioavailregs_dma = dma_alloc_coherent(
		&dd->pcidev->dev, PAGE_SIZE, &dd->ipath_pioavailregs_phys,
		GFP_KERNEL);
	if (!dd->ipath_pioavailregs_dma) {
		ipath_dev_err(dd, "failed to allocate PIOavail reg area "
			      "in memory\n");
		ret = -ENOMEM;
		goto done;
	}

	/*
	 * we really want L2 cache aligned, but for current CPUs of
	 * interest, they are the same.
	 */
	dd->ipath_statusp = (u64 *)
		((char *)dd->ipath_pioavailregs_dma +
		 ((2 * L1_CACHE_BYTES +
		   dd->ipath_pioavregs * sizeof(u64)) & ~L1_CACHE_BYTES));
	/* copy the current value now that it's really allocated */
	*dd->ipath_statusp = dd->_ipath_status;
	/*
	 * setup buffer to hold freeze msg, accessible to apps,
	 * following statusp
	 */
	dd->ipath_freezemsg = (char *)&dd->ipath_statusp[1];
	/* and its length */
	dd->ipath_freezelen = L1_CACHE_BYTES - sizeof(dd->ipath_statusp[0]);

	ret = 0;

done:
	return ret;
}

/**
 * init_shadow_tids - allocate the shadow TID array
 * @dd: the infinipath device
 *
 * allocate the shadow TID array, so we can ipath_munlock previous
 * entries.  It may make more sense to move the pageshadow to the
 * port data structure, so we only allocate memory for ports actually
 * in use, since we at 8k per port, now.
 */
static void init_shadow_tids(struct ipath_devdata *dd)
{
	struct page **pages;
	dma_addr_t *addrs;

	pages = vmalloc(dd->ipath_cfgports * dd->ipath_rcvtidcnt *
			sizeof(struct page *));
	if (!pages) {
		ipath_dev_err(dd, "failed to allocate shadow page * "
			      "array, no expected sends!\n");
		dd->ipath_pageshadow = NULL;
		return;
	}

	addrs = vmalloc(dd->ipath_cfgports * dd->ipath_rcvtidcnt *
			sizeof(dma_addr_t));
	if (!addrs) {
		ipath_dev_err(dd, "failed to allocate shadow dma handle "
			      "array, no expected sends!\n");
		vfree(pages);
		dd->ipath_pageshadow = NULL;
		return;
	}

	memset(pages, 0, dd->ipath_cfgports * dd->ipath_rcvtidcnt *
	       sizeof(struct page *));

	dd->ipath_pageshadow = pages;
	dd->ipath_physshadow = addrs;
}

static void enable_chip(struct ipath_devdata *dd, int reinit)
{
	u32 val;
	u64 rcvmask;
	unsigned long flags;
	int i;

	if (!reinit)
		init_waitqueue_head(&ipath_state_wait);

	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
			 dd->ipath_rcvctrl);

	spin_lock_irqsave(&dd->ipath_sendctrl_lock, flags);
	/* Enable PIO send, and update of PIOavail regs to memory. */
	dd->ipath_sendctrl = INFINIPATH_S_PIOENABLE |
		INFINIPATH_S_PIOBUFAVAILUPD;

	/*
	 * Set the PIO avail update threshold to host memory
	 * on chips that support it.
	 */
	if (dd->ipath_pioupd_thresh)
		dd->ipath_sendctrl |= dd->ipath_pioupd_thresh
			<< INFINIPATH_S_UPDTHRESH_SHIFT;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl, dd->ipath_sendctrl);
	ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
	spin_unlock_irqrestore(&dd->ipath_sendctrl_lock, flags);

	/*
	 * Enable kernel ports' receive and receive interrupt.
	 * Other ports done as user opens and inits them.
	 */
	rcvmask = 1ULL;
	dd->ipath_rcvctrl |= (rcvmask << dd->ipath_r_portenable_shift) |
		(rcvmask << dd->ipath_r_intravail_shift);
	if (!(dd->ipath_flags & IPATH_NODMA_RTAIL))
		dd->ipath_rcvctrl |= (1ULL << dd->ipath_r_tailupd_shift);

	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvctrl,
			 dd->ipath_rcvctrl);

	/*
	 * now ready for use.  this should be cleared whenever we
	 * detect a reset, or initiate one.
	 */
	dd->ipath_flags |= IPATH_INITTED;

	/*
	 * Init our shadow copies of head from tail values,
	 * and write head values to match.
	 */
	val = ipath_read_ureg32(dd, ur_rcvegrindextail, 0);
	ipath_write_ureg(dd, ur_rcvegrindexhead, val, 0);

	/* Initialize so we interrupt on next packet received */
	ipath_write_ureg(dd, ur_rcvhdrhead,
			 dd->ipath_rhdrhead_intr_off |
			 dd->ipath_pd[0]->port_head, 0);

	/*
	 * by now pioavail updates to memory should have occurred, so
	 * copy them into our working/shadow registers; this is in
	 * case something went wrong with abort, but mostly to get the
	 * initial values of the generation bit correct.
	 */
	for (i = 0; i < dd->ipath_pioavregs; i++) {
		__le64 pioavail;

		/*
		 * Chip Errata bug 6641; even and odd qwords>3 are swapped.
		 */
		if (i > 3 && (dd->ipath_flags & IPATH_SWAP_PIOBUFS))
			pioavail = dd->ipath_pioavailregs_dma[i ^ 1];
		else
			pioavail = dd->ipath_pioavailregs_dma[i];
		/*
		 * don't need to worry about ipath_pioavailkernel here
		 * because we will call ipath_chg_pioavailkernel() later
		 * in initialization, to busy out buffers as needed
		 */
		dd->ipath_pioavailshadow[i] = le64_to_cpu(pioavail);
	}
	/* can get counters, stats, etc. */
	dd->ipath_flags |= IPATH_PRESENT;
}

static int init_housekeeping(struct ipath_devdata *dd, int reinit)
{
	char boardn[40];
	int ret = 0;

	/*
	 * have to clear shadow copies of registers at init that are
	 * not otherwise set here, or all kinds of bizarre things
	 * happen with driver on chip reset
	 */
	dd->ipath_rcvhdrsize = 0;

	/*
	 * Don't clear ipath_flags as 8bit mode was set before
	 * entering this func. However, we do set the linkstate to
	 * unknown, so we can watch for a transition.
	 * PRESENT is set because we want register reads to work,
	 * and the kernel infrastructure saw it in config space;
	 * We clear it if we have failures.
	 */
	dd->ipath_flags |= IPATH_LINKUNK | IPATH_PRESENT;
	dd->ipath_flags &= ~(IPATH_LINKACTIVE | IPATH_LINKARMED |
			     IPATH_LINKDOWN | IPATH_LINKINIT);

	ipath_cdbg(VERBOSE, "Try to read spc chip revision\n");
	dd->ipath_revision =
		ipath_read_kreg64(dd, dd->ipath_kregs->kr_revision);

	/*
	 * set up fundamental info we need to use the chip; we assume
	 * if the revision reg and these regs are OK, we don't need to
	 * special case the rest
	 */
	dd->ipath_sregbase =
		ipath_read_kreg32(dd, dd->ipath_kregs->kr_sendregbase);
	dd->ipath_cregbase =
		ipath_read_kreg32(dd, dd->ipath_kregs->kr_counterregbase);
	dd->ipath_uregbase =
		ipath_read_kreg32(dd, dd->ipath_kregs->kr_userregbase);
	ipath_cdbg(VERBOSE, "ipath_kregbase %p, sendbase %x usrbase %x, "
		   "cntrbase %x\n", dd->ipath_kregbase, dd->ipath_sregbase,
		   dd->ipath_uregbase, dd->ipath_cregbase);
	if ((dd->ipath_revision & 0xffffffff) == 0xffffffff
	    || (dd->ipath_sregbase & 0xffffffff) == 0xffffffff
	    || (dd->ipath_cregbase & 0xffffffff) == 0xffffffff
	    || (dd->ipath_uregbase & 0xffffffff) == 0xffffffff) {
		ipath_dev_err(dd, "Register read failures from chip, "
			      "giving up initialization\n");
		dd->ipath_flags &= ~IPATH_PRESENT;
		ret = -ENODEV;
		goto done;
	}


	/* clear diagctrl register, in case diags were running and crashed */
	ipath_write_kreg (dd, dd->ipath_kregs->kr_hwdiagctrl, 0);

	/* clear the initial reset flag, in case first driver load */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_errorclear,
			 INFINIPATH_E_RESET);

	ipath_cdbg(VERBOSE, "Revision %llx (PCI %x)\n",
		   (unsigned long long) dd->ipath_revision,
		   dd->ipath_pcirev);

	if (((dd->ipath_revision >> INFINIPATH_R_SOFTWARE_SHIFT) &
	     INFINIPATH_R_SOFTWARE_MASK) != IPATH_CHIP_SWVERSION) {
		ipath_dev_err(dd, "Driver only handles version %d, "
			      "chip swversion is %d (%llx), failng\n",
			      IPATH_CHIP_SWVERSION,
			      (int)(dd->ipath_revision >>
				    INFINIPATH_R_SOFTWARE_SHIFT) &
			      INFINIPATH_R_SOFTWARE_MASK,
			      (unsigned long long) dd->ipath_revision);
		ret = -ENOSYS;
		goto done;
	}
	dd->ipath_majrev = (u8) ((dd->ipath_revision >>
				  INFINIPATH_R_CHIPREVMAJOR_SHIFT) &
				 INFINIPATH_R_CHIPREVMAJOR_MASK);
	dd->ipath_minrev = (u8) ((dd->ipath_revision >>
				  INFINIPATH_R_CHIPREVMINOR_SHIFT) &
				 INFINIPATH_R_CHIPREVMINOR_MASK);
	dd->ipath_boardrev = (u8) ((dd->ipath_revision >>
				    INFINIPATH_R_BOARDID_SHIFT) &
				   INFINIPATH_R_BOARDID_MASK);

	ret = dd->ipath_f_get_boardname(dd, boardn, sizeof boardn);

	snprintf(dd->ipath_boardversion, sizeof(dd->ipath_boardversion),
		 "ChipABI %u.%u, %s, InfiniPath%u %u.%u, PCI %u, "
		 "SW Compat %u\n",
		 IPATH_CHIP_VERS_MAJ, IPATH_CHIP_VERS_MIN, boardn,
		 (unsigned)(dd->ipath_revision >> INFINIPATH_R_ARCH_SHIFT) &
		 INFINIPATH_R_ARCH_MASK,
		 dd->ipath_majrev, dd->ipath_minrev, dd->ipath_pcirev,
		 (unsigned)(dd->ipath_revision >>
			    INFINIPATH_R_SOFTWARE_SHIFT) &
		 INFINIPATH_R_SOFTWARE_MASK);

	ipath_dbg("%s", dd->ipath_boardversion);

	if (ret)
		goto done;

	if (reinit)
		ret = init_chip_reset(dd);
	else
		ret = init_chip_first(dd);

done:
	return ret;
}

static void verify_interrupt(unsigned long opaque)
{
	struct ipath_devdata *dd = (struct ipath_devdata *) opaque;

	if (!dd)
		return; /* being torn down */

	/*
	 * If we don't have any interrupts, let the user know and
	 * don't bother checking again.
	 */
	if (dd->ipath_int_counter == 0) {
		if (!dd->ipath_f_intr_fallback(dd))
			dev_err(&dd->pcidev->dev, "No interrupts detected, "
				"not usable.\n");
		else /* re-arm the timer to see if fallback works */
			mod_timer(&dd->ipath_intrchk_timer, jiffies + HZ/2);
	} else
		ipath_cdbg(VERBOSE, "%u interrupts at timer check\n",
			dd->ipath_int_counter);
}

/**
 * ipath_init_chip - do the actual initialization sequence on the chip
 * @dd: the infinipath device
 * @reinit: reinitializing, so don't allocate new memory
 *
 * Do the actual initialization sequence on the chip.  This is done
 * both from the init routine called from the PCI infrastructure, and
 * when we reset the chip, or detect that it was reset internally,
 * or it's administratively re-enabled.
 *
 * Memory allocation here and in called routines is only done in
 * the first case (reinit == 0).  We have to be careful, because even
 * without memory allocation, we need to re-write all the chip registers
 * TIDs, etc. after the reset or enable has completed.
 */
int ipath_init_chip(struct ipath_devdata *dd, int reinit)
{
	int ret = 0;
	u32 kpiobufs, defkbufs;
	u32 piobufs, uports;
	u64 val;
	struct ipath_portdata *pd;
	gfp_t gfp_flags = GFP_USER | __GFP_COMP;

	ret = init_housekeeping(dd, reinit);
	if (ret)
		goto done;

	/*
	 * we ignore most issues after reporting them, but have to specially
	 * handle hardware-disabled chips.
	 */
	if (ret == 2) {
		/* unique error, known to ipath_init_one */
		ret = -EPERM;
		goto done;
	}

	/*
	 * We could bump this to allow for full rcvegrcnt + rcvtidcnt,
	 * but then it no longer nicely fits power of two, and since
	 * we now use routines that backend onto __get_free_pages, the
	 * rest would be wasted.
	 */
	dd->ipath_rcvhdrcnt = max(dd->ipath_p0_rcvegrcnt, dd->ipath_rcvegrcnt);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvhdrcnt,
			 dd->ipath_rcvhdrcnt);

	/*
	 * Set up the shadow copies of the piobufavail registers,
	 * which we compare against the chip registers for now, and
	 * the in memory DMA'ed copies of the registers.  This has to
	 * be done early, before we calculate lastport, etc.
	 */
	piobufs = dd->ipath_piobcnt2k + dd->ipath_piobcnt4k;
	/*
	 * calc number of pioavail registers, and save it; we have 2
	 * bits per buffer.
	 */
	dd->ipath_pioavregs = ALIGN(piobufs, sizeof(u64) * BITS_PER_BYTE / 2)
		/ (sizeof(u64) * BITS_PER_BYTE / 2);
	uports = dd->ipath_cfgports ? dd->ipath_cfgports - 1 : 0;
	if (piobufs > 144)
		defkbufs = 32 + dd->ipath_pioreserved;
	else
		defkbufs = 16 + dd->ipath_pioreserved;

	if (ipath_kpiobufs && (ipath_kpiobufs +
		(uports * IPATH_MIN_USER_PORT_BUFCNT)) > piobufs) {
		int i = (int) piobufs -
			(int) (uports * IPATH_MIN_USER_PORT_BUFCNT);
		if (i < 1)
			i = 1;
		dev_info(&dd->pcidev->dev, "Allocating %d PIO bufs of "
			 "%d for kernel leaves too few for %d user ports "
			 "(%d each); using %u\n", ipath_kpiobufs,
			 piobufs, uports, IPATH_MIN_USER_PORT_BUFCNT, i);
		/*
		 * shouldn't change ipath_kpiobufs, because could be
		 * different for different devices...
		 */
		kpiobufs = i;
	} else if (ipath_kpiobufs)
		kpiobufs = ipath_kpiobufs;
	else
		kpiobufs = defkbufs;
	dd->ipath_lastport_piobuf = piobufs - kpiobufs;
	dd->ipath_pbufsport =
		uports ? dd->ipath_lastport_piobuf / uports : 0;
	/* if not an even divisor, some user ports get extra buffers */
	dd->ipath_ports_extrabuf = dd->ipath_lastport_piobuf -
		(dd->ipath_pbufsport * uports);
	if (dd->ipath_ports_extrabuf)
		ipath_dbg("%u pbufs/port leaves some unused, add 1 buffer to "
			"ports <= %u\n", dd->ipath_pbufsport,
			dd->ipath_ports_extrabuf);
	dd->ipath_lastpioindex = 0;
	dd->ipath_lastpioindexl = dd->ipath_piobcnt2k;
	/* ipath_pioavailshadow initialized earlier */
	ipath_cdbg(VERBOSE, "%d PIO bufs for kernel out of %d total %u "
		   "each for %u user ports\n", kpiobufs,
		   piobufs, dd->ipath_pbufsport, uports);
	ret = dd->ipath_f_early_init(dd);
	if (ret) {
		ipath_dev_err(dd, "Early initialization failure\n");
		goto done;
	}

	/*
	 * Early_init sets rcvhdrentsize and rcvhdrsize, so this must be
	 * done after early_init.
	 */
	dd->ipath_hdrqlast =
		dd->ipath_rcvhdrentsize * (dd->ipath_rcvhdrcnt - 1);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvhdrentsize,
			 dd->ipath_rcvhdrentsize);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvhdrsize,
			 dd->ipath_rcvhdrsize);

	if (!reinit) {
		ret = init_pioavailregs(dd);
		init_shadow_tids(dd);
		if (ret)
			goto done;
	}

	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendpioavailaddr,
			 dd->ipath_pioavailregs_phys);

	/*
	 * this is to detect s/w errors, which the h/w works around by
	 * ignoring the low 6 bits of address, if it wasn't aligned.
	 */
	val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_sendpioavailaddr);
	if (val != dd->ipath_pioavailregs_phys) {
		ipath_dev_err(dd, "Catastrophic software error, "
			      "SendPIOAvailAddr written as %lx, "
			      "read back as %llx\n",
			      (unsigned long) dd->ipath_pioavailregs_phys,
			      (unsigned long long) val);
		ret = -EINVAL;
		goto done;
	}

	ipath_write_kreg(dd, dd->ipath_kregs->kr_rcvbthqp, IPATH_KD_QP);

	/*
	 * make sure we are not in freeze, and PIO send enabled, so
	 * writes to pbc happen
	 */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrmask, 0ULL);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrclear,
			 ~0ULL&~INFINIPATH_HWE_MEMBISTFAILED);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_control, 0ULL);

	/*
	 * before error clears, since we expect serdes pll errors during
	 * this, the first time after reset
	 */
	if (bringup_link(dd)) {
		dev_info(&dd->pcidev->dev, "Failed to bringup IB link\n");
		ret = -ENETDOWN;
		goto done;
	}

	/*
	 * clear any "expected" hwerrs from reset and/or initialization
	 * clear any that aren't enabled (at least this once), and then
	 * set the enable mask
	 */
	dd->ipath_f_init_hwerrors(dd);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrclear,
			 ~0ULL&~INFINIPATH_HWE_MEMBISTFAILED);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrmask,
			 dd->ipath_hwerrmask);

	/* clear all */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_errorclear, -1LL);
	/* enable errors that are masked, at least this first time. */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_errormask,
			 ~dd->ipath_maskederrs);
	dd->ipath_maskederrs = 0; /* don't re-enable ignored in timer */
	dd->ipath_errormask =
		ipath_read_kreg64(dd, dd->ipath_kregs->kr_errormask);
	/* clear any interrupts up to this point (ints still not enabled) */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_intclear, -1LL);

	dd->ipath_f_tidtemplate(dd);

	/*
	 * Set up the port 0 (kernel) rcvhdr q and egr TIDs.  If doing
	 * re-init, the simplest way to handle this is to free
	 * existing, and re-allocate.
	 * Need to re-create rest of port 0 portdata as well.
	 */
	pd = dd->ipath_pd[0];
	if (reinit) {
		struct ipath_portdata *npd;

		/*
		 * Alloc and init new ipath_portdata for port0,
		 * Then free old pd. Could lead to fragmentation, but also
		 * makes later support for hot-swap easier.
		 */
		npd = create_portdata0(dd);
		if (npd) {
			ipath_free_pddata(dd, pd);
			dd->ipath_pd[0] = npd;
			pd = npd;
		} else {
			ipath_dev_err(dd, "Unable to allocate portdata"
				      " for port 0, failing\n");
			ret = -ENOMEM;
			goto done;
		}
	}
	ret = ipath_create_rcvhdrq(dd, pd);
	if (!ret)
		ret = create_port0_egr(dd);
	if (ret) {
		ipath_dev_err(dd, "failed to allocate kernel port's "
			      "rcvhdrq and/or egr bufs\n");
		goto done;
	}
	else
		enable_chip(dd, reinit);

	/* after enable_chip, so pioavailshadow setup */
	ipath_chg_pioavailkernel(dd, 0, piobufs, 1);

	/*
	 * Cancel any possible active sends from early driver load.
	 * Follows early_init because some chips have to initialize
	 * PIO buffers in early_init to avoid false parity errors.
	 * After enable and ipath_chg_pioavailkernel so we can safely
	 * enable pioavail updates and PIOENABLE; packets are now
	 * ready to go out.
	 */
	ipath_cancel_sends(dd, 1);

	if (!reinit) {
		/*
		 * Used when we close a port, for DMA already in flight
		 * at close.
		 */
		dd->ipath_dummy_hdrq = dma_alloc_coherent(
			&dd->pcidev->dev, dd->ipath_pd[0]->port_rcvhdrq_size,
			&dd->ipath_dummy_hdrq_phys,
			gfp_flags);
		if (!dd->ipath_dummy_hdrq) {
			dev_info(&dd->pcidev->dev,
				"Couldn't allocate 0x%lx bytes for dummy hdrq\n",
				dd->ipath_pd[0]->port_rcvhdrq_size);
			/* fallback to just 0'ing */
			dd->ipath_dummy_hdrq_phys = 0UL;
		}
	}

	/*
	 * cause retrigger of pending interrupts ignored during init,
	 * even if we had errors
	 */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_intclear, 0ULL);

	if (!dd->ipath_stats_timer_active) {
		/*
		 * first init, or after an admin disable/enable
		 * set up stats retrieval timer, even if we had errors
		 * in last portion of setup
		 */
		init_timer(&dd->ipath_stats_timer);
		dd->ipath_stats_timer.function = ipath_get_faststats;
		dd->ipath_stats_timer.data = (unsigned long) dd;
		/* every 5 seconds; */
		dd->ipath_stats_timer.expires = jiffies + 5 * HZ;
		/* takes ~16 seconds to overflow at full IB 4x bandwdith */
		add_timer(&dd->ipath_stats_timer);
		dd->ipath_stats_timer_active = 1;
	}

	/* Set up SendDMA if chip supports it */
	if (dd->ipath_flags & IPATH_HAS_SEND_DMA)
		ret = setup_sdma(dd);

	/* Set up HoL state */
	init_timer(&dd->ipath_hol_timer);
	dd->ipath_hol_timer.function = ipath_hol_event;
	dd->ipath_hol_timer.data = (unsigned long)dd;
	dd->ipath_hol_state = IPATH_HOL_UP;

done:
	if (!ret) {
		*dd->ipath_statusp |= IPATH_STATUS_CHIP_PRESENT;
		if (!dd->ipath_f_intrsetup(dd)) {
			/* now we can enable all interrupts from the chip */
			ipath_write_kreg(dd, dd->ipath_kregs->kr_intmask,
					 -1LL);
			/* force re-interrupt of any pending interrupts. */
			ipath_write_kreg(dd, dd->ipath_kregs->kr_intclear,
					 0ULL);
			/* chip is usable; mark it as initialized */
			*dd->ipath_statusp |= IPATH_STATUS_INITTED;

			/*
			 * setup to verify we get an interrupt, and fallback
			 * to an alternate if necessary and possible
			 */
			if (!reinit) {
				init_timer(&dd->ipath_intrchk_timer);
				dd->ipath_intrchk_timer.function =
					verify_interrupt;
				dd->ipath_intrchk_timer.data =
					(unsigned long) dd;
			}
			dd->ipath_intrchk_timer.expires = jiffies + HZ/2;
			add_timer(&dd->ipath_intrchk_timer);
		} else
			ipath_dev_err(dd, "No interrupts enabled, couldn't "
				      "setup interrupt address\n");

		if (dd->ipath_cfgports > ipath_stats.sps_nports)
			/*
			 * sps_nports is a global, so, we set it to
			 * the highest number of ports of any of the
			 * chips we find; we never decrement it, at
			 * least for now.  Since this might have changed
			 * over disable/enable or prior to reset, always
			 * do the check and potentially adjust.
			 */
			ipath_stats.sps_nports = dd->ipath_cfgports;
	} else
		ipath_dbg("Failed (%d) to initialize chip\n", ret);

	/* if ret is non-zero, we probably should do some cleanup
	   here... */
	return ret;
}

static int ipath_set_kpiobufs(const char *str, struct kernel_param *kp)
{
	struct ipath_devdata *dd;
	unsigned long flags;
	unsigned short val;
	int ret;

	ret = ipath_parse_ushort(str, &val);

	spin_lock_irqsave(&ipath_devs_lock, flags);

	if (ret < 0)
		goto bail;

	if (val == 0) {
		ret = -EINVAL;
		goto bail;
	}

	list_for_each_entry(dd, &ipath_dev_list, ipath_list) {
		if (dd->ipath_kregbase)
			continue;
		if (val > (dd->ipath_piobcnt2k + dd->ipath_piobcnt4k -
			   (dd->ipath_cfgports *
			    IPATH_MIN_USER_PORT_BUFCNT)))
		{
			ipath_dev_err(
				dd,
				"Allocating %d PIO bufs for kernel leaves "
				"too few for %d user ports (%d each)\n",
				val, dd->ipath_cfgports - 1,
				IPATH_MIN_USER_PORT_BUFCNT);
			ret = -EINVAL;
			goto bail;
		}
		dd->ipath_lastport_piobuf =
			dd->ipath_piobcnt2k + dd->ipath_piobcnt4k - val;
	}

	ipath_kpiobufs = val;
	ret = 0;
bail:
	spin_unlock_irqrestore(&ipath_devs_lock, flags);

	return ret;
}

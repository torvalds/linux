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

/*
 * These are the routines used by layered drivers, currently just the
 * layered ethernet driver and verbs layer.
 */

#include <linux/io.h>
#include <linux/pci.h>
#include <asm/byteorder.h>

#include "ipath_kernel.h"
#include "ips_common.h"
#include "ipath_layer.h"

/* Acquire before ipath_devs_lock. */
static DEFINE_MUTEX(ipath_layer_mutex);

u16 ipath_layer_rcv_opcode;
static int (*layer_intr)(void *, u32);
static int (*layer_rcv)(void *, void *, struct sk_buff *);
static int (*layer_rcv_lid)(void *, void *);
static int (*verbs_piobufavail)(void *);
static void (*verbs_rcv)(void *, void *, void *, u32);
int ipath_verbs_registered;

static void *(*layer_add_one)(int, struct ipath_devdata *);
static void (*layer_remove_one)(void *);
static void *(*verbs_add_one)(int, struct ipath_devdata *);
static void (*verbs_remove_one)(void *);
static void (*verbs_timer_cb)(void *);

int __ipath_layer_intr(struct ipath_devdata *dd, u32 arg)
{
	int ret = -ENODEV;

	if (dd->ipath_layer.l_arg && layer_intr)
		ret = layer_intr(dd->ipath_layer.l_arg, arg);

	return ret;
}

int ipath_layer_intr(struct ipath_devdata *dd, u32 arg)
{
	int ret;

	mutex_lock(&ipath_layer_mutex);

	ret = __ipath_layer_intr(dd, arg);

	mutex_unlock(&ipath_layer_mutex);

	return ret;
}

int __ipath_layer_rcv(struct ipath_devdata *dd, void *hdr,
		      struct sk_buff *skb)
{
	int ret = -ENODEV;

	if (dd->ipath_layer.l_arg && layer_rcv)
		ret = layer_rcv(dd->ipath_layer.l_arg, hdr, skb);

	return ret;
}

int __ipath_layer_rcv_lid(struct ipath_devdata *dd, void *hdr)
{
	int ret = -ENODEV;

	if (dd->ipath_layer.l_arg && layer_rcv_lid)
		ret = layer_rcv_lid(dd->ipath_layer.l_arg, hdr);

	return ret;
}

int __ipath_verbs_piobufavail(struct ipath_devdata *dd)
{
	int ret = -ENODEV;

	if (dd->verbs_layer.l_arg && verbs_piobufavail)
		ret = verbs_piobufavail(dd->verbs_layer.l_arg);

	return ret;
}

int __ipath_verbs_rcv(struct ipath_devdata *dd, void *rc, void *ebuf,
		      u32 tlen)
{
	int ret = -ENODEV;

	if (dd->verbs_layer.l_arg && verbs_rcv) {
		verbs_rcv(dd->verbs_layer.l_arg, rc, ebuf, tlen);
		ret = 0;
	}

	return ret;
}

int ipath_layer_set_linkstate(struct ipath_devdata *dd, u8 newstate)
{
	u32 lstate;
	int ret;

	switch (newstate) {
	case IPATH_IB_LINKDOWN:
		ipath_set_ib_lstate(dd, INFINIPATH_IBCC_LINKINITCMD_POLL <<
				    INFINIPATH_IBCC_LINKINITCMD_SHIFT);
		/* don't wait */
		ret = 0;
		goto bail;

	case IPATH_IB_LINKDOWN_SLEEP:
		ipath_set_ib_lstate(dd, INFINIPATH_IBCC_LINKINITCMD_SLEEP <<
				    INFINIPATH_IBCC_LINKINITCMD_SHIFT);
		/* don't wait */
		ret = 0;
		goto bail;

	case IPATH_IB_LINKDOWN_DISABLE:
		ipath_set_ib_lstate(dd,
				    INFINIPATH_IBCC_LINKINITCMD_DISABLE <<
				    INFINIPATH_IBCC_LINKINITCMD_SHIFT);
		/* don't wait */
		ret = 0;
		goto bail;

	case IPATH_IB_LINKINIT:
		if (dd->ipath_flags & IPATH_LINKINIT) {
			ret = 0;
			goto bail;
		}
		ipath_set_ib_lstate(dd, INFINIPATH_IBCC_LINKCMD_INIT <<
				    INFINIPATH_IBCC_LINKCMD_SHIFT);
		lstate = IPATH_LINKINIT;
		break;

	case IPATH_IB_LINKARM:
		if (dd->ipath_flags & IPATH_LINKARMED) {
			ret = 0;
			goto bail;
		}
		if (!(dd->ipath_flags &
		      (IPATH_LINKINIT | IPATH_LINKACTIVE))) {
			ret = -EINVAL;
			goto bail;
		}
		ipath_set_ib_lstate(dd, INFINIPATH_IBCC_LINKCMD_ARMED <<
				    INFINIPATH_IBCC_LINKCMD_SHIFT);
		/*
		 * Since the port can transition to ACTIVE by receiving
		 * a non VL 15 packet, wait for either state.
		 */
		lstate = IPATH_LINKARMED | IPATH_LINKACTIVE;
		break;

	case IPATH_IB_LINKACTIVE:
		if (dd->ipath_flags & IPATH_LINKACTIVE) {
			ret = 0;
			goto bail;
		}
		if (!(dd->ipath_flags & IPATH_LINKARMED)) {
			ret = -EINVAL;
			goto bail;
		}
		ipath_set_ib_lstate(dd, INFINIPATH_IBCC_LINKCMD_ACTIVE <<
				    INFINIPATH_IBCC_LINKCMD_SHIFT);
		lstate = IPATH_LINKACTIVE;
		break;

	default:
		ipath_dbg("Invalid linkstate 0x%x requested\n", newstate);
		ret = -EINVAL;
		goto bail;
	}
	ret = ipath_wait_linkstate(dd, lstate, 2000);

bail:
	return ret;
}

EXPORT_SYMBOL_GPL(ipath_layer_set_linkstate);

/**
 * ipath_layer_set_mtu - set the MTU
 * @dd: the infinipath device
 * @arg: the new MTU
 *
 * we can handle "any" incoming size, the issue here is whether we
 * need to restrict our outgoing size.   For now, we don't do any
 * sanity checking on this, and we don't deal with what happens to
 * programs that are already running when the size changes.
 * NOTE: changing the MTU will usually cause the IBC to go back to
 * link initialize (IPATH_IBSTATE_INIT) state...
 */
int ipath_layer_set_mtu(struct ipath_devdata *dd, u16 arg)
{
	u32 piosize;
	int changed = 0;
	int ret;

	/*
	 * mtu is IB data payload max.  It's the largest power of 2 less
	 * than piosize (or even larger, since it only really controls the
	 * largest we can receive; we can send the max of the mtu and
	 * piosize).  We check that it's one of the valid IB sizes.
	 */
	if (arg != 256 && arg != 512 && arg != 1024 && arg != 2048 &&
	    arg != 4096) {
		ipath_dbg("Trying to set invalid mtu %u, failing\n", arg);
		ret = -EINVAL;
		goto bail;
	}
	if (dd->ipath_ibmtu == arg) {
		ret = 0;	/* same as current */
		goto bail;
	}

	piosize = dd->ipath_ibmaxlen;
	dd->ipath_ibmtu = arg;

	if (arg >= (piosize - IPATH_PIO_MAXIBHDR)) {
		/* Only if it's not the initial value (or reset to it) */
		if (piosize != dd->ipath_init_ibmaxlen) {
			dd->ipath_ibmaxlen = piosize;
			changed = 1;
		}
	} else if ((arg + IPATH_PIO_MAXIBHDR) != dd->ipath_ibmaxlen) {
		piosize = arg + IPATH_PIO_MAXIBHDR;
		ipath_cdbg(VERBOSE, "ibmaxlen was 0x%x, setting to 0x%x "
			   "(mtu 0x%x)\n", dd->ipath_ibmaxlen, piosize,
			   arg);
		dd->ipath_ibmaxlen = piosize;
		changed = 1;
	}

	if (changed) {
		/*
		 * set the IBC maxpktlength to the size of our pio
		 * buffers in words
		 */
		u64 ibc = dd->ipath_ibcctrl;
		ibc &= ~(INFINIPATH_IBCC_MAXPKTLEN_MASK <<
			 INFINIPATH_IBCC_MAXPKTLEN_SHIFT);

		piosize = piosize - 2 * sizeof(u32);	/* ignore pbc */
		dd->ipath_ibmaxlen = piosize;
		piosize /= sizeof(u32);	/* in words */
		/*
		 * for ICRC, which we only send in diag test pkt mode, and
		 * we don't need to worry about that for mtu
		 */
		piosize += 1;

		ibc |= piosize << INFINIPATH_IBCC_MAXPKTLEN_SHIFT;
		dd->ipath_ibcctrl = ibc;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_ibcctrl,
				 dd->ipath_ibcctrl);
		dd->ipath_f_tidtemplate(dd);
	}

	ret = 0;

bail:
	return ret;
}

EXPORT_SYMBOL_GPL(ipath_layer_set_mtu);

int ipath_set_sps_lid(struct ipath_devdata *dd, u32 arg, u8 lmc)
{
	ipath_stats.sps_lid[dd->ipath_unit] = arg;
	dd->ipath_lid = arg;
	dd->ipath_lmc = lmc;

	mutex_lock(&ipath_layer_mutex);

	if (dd->ipath_layer.l_arg && layer_intr)
		layer_intr(dd->ipath_layer.l_arg, IPATH_LAYER_INT_LID);

	mutex_unlock(&ipath_layer_mutex);

	return 0;
}

EXPORT_SYMBOL_GPL(ipath_set_sps_lid);

int ipath_layer_set_guid(struct ipath_devdata *dd, __be64 guid)
{
	/* XXX - need to inform anyone who cares this just happened. */
	dd->ipath_guid = guid;
	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_set_guid);

__be64 ipath_layer_get_guid(struct ipath_devdata *dd)
{
	return dd->ipath_guid;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_guid);

u32 ipath_layer_get_nguid(struct ipath_devdata *dd)
{
	return dd->ipath_nguid;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_nguid);

int ipath_layer_query_device(struct ipath_devdata *dd, u32 * vendor,
			     u32 * boardrev, u32 * majrev, u32 * minrev)
{
	*vendor = dd->ipath_vendorid;
	*boardrev = dd->ipath_boardrev;
	*majrev = dd->ipath_majrev;
	*minrev = dd->ipath_minrev;

	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_query_device);

u32 ipath_layer_get_flags(struct ipath_devdata *dd)
{
	return dd->ipath_flags;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_flags);

struct device *ipath_layer_get_device(struct ipath_devdata *dd)
{
	return &dd->pcidev->dev;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_device);

u16 ipath_layer_get_deviceid(struct ipath_devdata *dd)
{
	return dd->ipath_deviceid;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_deviceid);

u64 ipath_layer_get_lastibcstat(struct ipath_devdata *dd)
{
	return dd->ipath_lastibcstat;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_lastibcstat);

u32 ipath_layer_get_ibmtu(struct ipath_devdata *dd)
{
	return dd->ipath_ibmtu;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_ibmtu);

void ipath_layer_add(struct ipath_devdata *dd)
{
	mutex_lock(&ipath_layer_mutex);

	if (layer_add_one)
		dd->ipath_layer.l_arg =
			layer_add_one(dd->ipath_unit, dd);

	if (verbs_add_one)
		dd->verbs_layer.l_arg =
			verbs_add_one(dd->ipath_unit, dd);

	mutex_unlock(&ipath_layer_mutex);
}

void ipath_layer_del(struct ipath_devdata *dd)
{
	mutex_lock(&ipath_layer_mutex);

	if (dd->ipath_layer.l_arg && layer_remove_one) {
		layer_remove_one(dd->ipath_layer.l_arg);
		dd->ipath_layer.l_arg = NULL;
	}

	if (dd->verbs_layer.l_arg && verbs_remove_one) {
		verbs_remove_one(dd->verbs_layer.l_arg);
		dd->verbs_layer.l_arg = NULL;
	}

	mutex_unlock(&ipath_layer_mutex);
}

int ipath_layer_register(void *(*l_add)(int, struct ipath_devdata *),
			 void (*l_remove)(void *),
			 int (*l_intr)(void *, u32),
			 int (*l_rcv)(void *, void *, struct sk_buff *),
			 u16 l_rcv_opcode,
			 int (*l_rcv_lid)(void *, void *))
{
	struct ipath_devdata *dd, *tmp;
	unsigned long flags;

	mutex_lock(&ipath_layer_mutex);

	layer_add_one = l_add;
	layer_remove_one = l_remove;
	layer_intr = l_intr;
	layer_rcv = l_rcv;
	layer_rcv_lid = l_rcv_lid;
	ipath_layer_rcv_opcode = l_rcv_opcode;

	spin_lock_irqsave(&ipath_devs_lock, flags);

	list_for_each_entry_safe(dd, tmp, &ipath_dev_list, ipath_list) {
		if (!(dd->ipath_flags & IPATH_INITTED))
			continue;

		if (dd->ipath_layer.l_arg)
			continue;

		if (!(*dd->ipath_statusp & IPATH_STATUS_SMA))
			*dd->ipath_statusp |= IPATH_STATUS_OIB_SMA;

		spin_unlock_irqrestore(&ipath_devs_lock, flags);
		dd->ipath_layer.l_arg = l_add(dd->ipath_unit, dd);
		spin_lock_irqsave(&ipath_devs_lock, flags);
	}

	spin_unlock_irqrestore(&ipath_devs_lock, flags);
	mutex_unlock(&ipath_layer_mutex);

	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_register);

void ipath_layer_unregister(void)
{
	struct ipath_devdata *dd, *tmp;
	unsigned long flags;

	mutex_lock(&ipath_layer_mutex);
	spin_lock_irqsave(&ipath_devs_lock, flags);

	list_for_each_entry_safe(dd, tmp, &ipath_dev_list, ipath_list) {
		if (dd->ipath_layer.l_arg && layer_remove_one) {
			spin_unlock_irqrestore(&ipath_devs_lock, flags);
			layer_remove_one(dd->ipath_layer.l_arg);
			spin_lock_irqsave(&ipath_devs_lock, flags);
			dd->ipath_layer.l_arg = NULL;
		}
	}

	spin_unlock_irqrestore(&ipath_devs_lock, flags);

	layer_add_one = NULL;
	layer_remove_one = NULL;
	layer_intr = NULL;
	layer_rcv = NULL;
	layer_rcv_lid = NULL;

	mutex_unlock(&ipath_layer_mutex);
}

EXPORT_SYMBOL_GPL(ipath_layer_unregister);

static void __ipath_verbs_timer(unsigned long arg)
{
	struct ipath_devdata *dd = (struct ipath_devdata *) arg;

	/*
	 * If port 0 receive packet interrupts are not available, or
	 * can be missed, poll the receive queue
	 */
	if (dd->ipath_flags & IPATH_POLL_RX_INTR)
		ipath_kreceive(dd);

	/* Handle verbs layer timeouts. */
	if (dd->verbs_layer.l_arg && verbs_timer_cb)
		verbs_timer_cb(dd->verbs_layer.l_arg);

	mod_timer(&dd->verbs_layer.l_timer, jiffies + 1);
}

/**
 * ipath_verbs_register - verbs layer registration
 * @l_piobufavail: callback for when PIO buffers become available
 * @l_rcv: callback for receiving a packet
 * @l_timer_cb: timer callback
 * @ipath_devdata: device data structure is put here
 */
int ipath_verbs_register(void *(*l_add)(int, struct ipath_devdata *),
			 void (*l_remove)(void *arg),
			 int (*l_piobufavail) (void *arg),
			 void (*l_rcv) (void *arg, void *rhdr,
					void *data, u32 tlen),
			 void (*l_timer_cb) (void *arg))
{
	struct ipath_devdata *dd, *tmp;
	unsigned long flags;

	mutex_lock(&ipath_layer_mutex);

	verbs_add_one = l_add;
	verbs_remove_one = l_remove;
	verbs_piobufavail = l_piobufavail;
	verbs_rcv = l_rcv;
	verbs_timer_cb = l_timer_cb;

	spin_lock_irqsave(&ipath_devs_lock, flags);

	list_for_each_entry_safe(dd, tmp, &ipath_dev_list, ipath_list) {
		if (!(dd->ipath_flags & IPATH_INITTED))
			continue;

		if (dd->verbs_layer.l_arg)
			continue;

		spin_unlock_irqrestore(&ipath_devs_lock, flags);
		dd->verbs_layer.l_arg = l_add(dd->ipath_unit, dd);
		spin_lock_irqsave(&ipath_devs_lock, flags);
	}

	spin_unlock_irqrestore(&ipath_devs_lock, flags);
	mutex_unlock(&ipath_layer_mutex);

	ipath_verbs_registered = 1;

	return 0;
}

EXPORT_SYMBOL_GPL(ipath_verbs_register);

void ipath_verbs_unregister(void)
{
	struct ipath_devdata *dd, *tmp;
	unsigned long flags;

	mutex_lock(&ipath_layer_mutex);
	spin_lock_irqsave(&ipath_devs_lock, flags);

	list_for_each_entry_safe(dd, tmp, &ipath_dev_list, ipath_list) {
		*dd->ipath_statusp &= ~IPATH_STATUS_OIB_SMA;

		if (dd->verbs_layer.l_arg && verbs_remove_one) {
			spin_unlock_irqrestore(&ipath_devs_lock, flags);
			verbs_remove_one(dd->verbs_layer.l_arg);
			spin_lock_irqsave(&ipath_devs_lock, flags);
			dd->verbs_layer.l_arg = NULL;
		}
	}

	spin_unlock_irqrestore(&ipath_devs_lock, flags);

	verbs_add_one = NULL;
	verbs_remove_one = NULL;
	verbs_piobufavail = NULL;
	verbs_rcv = NULL;
	verbs_timer_cb = NULL;

	mutex_unlock(&ipath_layer_mutex);
}

EXPORT_SYMBOL_GPL(ipath_verbs_unregister);

int ipath_layer_open(struct ipath_devdata *dd, u32 * pktmax)
{
	int ret;
	u32 intval = 0;

	mutex_lock(&ipath_layer_mutex);

	if (!dd->ipath_layer.l_arg) {
		ret = -EINVAL;
		goto bail;
	}

	ret = ipath_setrcvhdrsize(dd, NUM_OF_EXTRA_WORDS_IN_HEADER_QUEUE);

	if (ret < 0)
		goto bail;

	*pktmax = dd->ipath_ibmaxlen;

	if (*dd->ipath_statusp & IPATH_STATUS_IB_READY)
		intval |= IPATH_LAYER_INT_IF_UP;
	if (ipath_stats.sps_lid[dd->ipath_unit])
		intval |= IPATH_LAYER_INT_LID;
	if (ipath_stats.sps_mlid[dd->ipath_unit])
		intval |= IPATH_LAYER_INT_BCAST;
	/*
	 * do this on open, in case low level is already up and
	 * just layered driver was reloaded, etc.
	 */
	if (intval)
		layer_intr(dd->ipath_layer.l_arg, intval);

	ret = 0;
bail:
	mutex_unlock(&ipath_layer_mutex);

	return ret;
}

EXPORT_SYMBOL_GPL(ipath_layer_open);

u16 ipath_layer_get_lid(struct ipath_devdata *dd)
{
	return dd->ipath_lid;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_lid);

/**
 * ipath_layer_get_mac - get the MAC address
 * @dd: the infinipath device
 * @mac: the MAC is put here
 *
 * This is the EUID-64 OUI octets (top 3), then
 * skip the next 2 (which should both be zero or 0xff).
 * The returned MAC is in network order
 * mac points to at least 6 bytes of buffer
 * We assume that by the time the LID is set, that the GUID is as valid
 * as it's ever going to be, rather than adding yet another status bit.
 */

int ipath_layer_get_mac(struct ipath_devdata *dd, u8 * mac)
{
	u8 *guid;

	guid = (u8 *) &dd->ipath_guid;

	mac[0] = guid[0];
	mac[1] = guid[1];
	mac[2] = guid[2];
	mac[3] = guid[5];
	mac[4] = guid[6];
	mac[5] = guid[7];
	if ((guid[3] || guid[4]) && !(guid[3] == 0xff && guid[4] == 0xff))
		ipath_dbg("Warning, guid bytes 3 and 4 not 0 or 0xffff: "
			  "%x %x\n", guid[3], guid[4]);
	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_mac);

u16 ipath_layer_get_bcast(struct ipath_devdata *dd)
{
	return dd->ipath_mlid;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_bcast);

u32 ipath_layer_get_cr_errpkey(struct ipath_devdata *dd)
{
	return ipath_read_creg32(dd, dd->ipath_cregs->cr_errpkey);
}

EXPORT_SYMBOL_GPL(ipath_layer_get_cr_errpkey);

static void update_sge(struct ipath_sge_state *ss, u32 length)
{
	struct ipath_sge *sge = &ss->sge;

	sge->vaddr += length;
	sge->length -= length;
	sge->sge_length -= length;
	if (sge->sge_length == 0) {
		if (--ss->num_sge)
			*sge = *ss->sg_list++;
	} else if (sge->length == 0 && sge->mr != NULL) {
		if (++sge->n >= IPATH_SEGSZ) {
			if (++sge->m >= sge->mr->mapsz)
				return;
			sge->n = 0;
		}
		sge->vaddr = sge->mr->map[sge->m]->segs[sge->n].vaddr;
		sge->length = sge->mr->map[sge->m]->segs[sge->n].length;
	}
}

#ifdef __LITTLE_ENDIAN
static inline u32 get_upper_bits(u32 data, u32 shift)
{
	return data >> shift;
}

static inline u32 set_upper_bits(u32 data, u32 shift)
{
	return data << shift;
}

static inline u32 clear_upper_bytes(u32 data, u32 n, u32 off)
{
	data <<= ((sizeof(u32) - n) * BITS_PER_BYTE);
	data >>= ((sizeof(u32) - n - off) * BITS_PER_BYTE);
	return data;
}
#else
static inline u32 get_upper_bits(u32 data, u32 shift)
{
	return data << shift;
}

static inline u32 set_upper_bits(u32 data, u32 shift)
{
	return data >> shift;
}

static inline u32 clear_upper_bytes(u32 data, u32 n, u32 off)
{
	data >>= ((sizeof(u32) - n) * BITS_PER_BYTE);
	data <<= ((sizeof(u32) - n - off) * BITS_PER_BYTE);
	return data;
}
#endif

static void copy_io(u32 __iomem *piobuf, struct ipath_sge_state *ss,
		    u32 length)
{
	u32 extra = 0;
	u32 data = 0;
	u32 last;

	while (1) {
		u32 len = ss->sge.length;
		u32 off;

		BUG_ON(len == 0);
		if (len > length)
			len = length;
		if (len > ss->sge.sge_length)
			len = ss->sge.sge_length;
		/* If the source address is not aligned, try to align it. */
		off = (unsigned long)ss->sge.vaddr & (sizeof(u32) - 1);
		if (off) {
			u32 *addr = (u32 *)((unsigned long)ss->sge.vaddr &
					    ~(sizeof(u32) - 1));
			u32 v = get_upper_bits(*addr, off * BITS_PER_BYTE);
			u32 y;

			y = sizeof(u32) - off;
			if (len > y)
				len = y;
			if (len + extra >= sizeof(u32)) {
				data |= set_upper_bits(v, extra *
						       BITS_PER_BYTE);
				len = sizeof(u32) - extra;
				if (len == length) {
					last = data;
					break;
				}
				__raw_writel(data, piobuf);
				piobuf++;
				extra = 0;
				data = 0;
			} else {
				/* Clear unused upper bytes */
				data |= clear_upper_bytes(v, len, extra);
				if (len == length) {
					last = data;
					break;
				}
				extra += len;
			}
		} else if (extra) {
			/* Source address is aligned. */
			u32 *addr = (u32 *) ss->sge.vaddr;
			int shift = extra * BITS_PER_BYTE;
			int ushift = 32 - shift;
			u32 l = len;

			while (l >= sizeof(u32)) {
				u32 v = *addr;

				data |= set_upper_bits(v, shift);
				__raw_writel(data, piobuf);
				data = get_upper_bits(v, ushift);
				piobuf++;
				addr++;
				l -= sizeof(u32);
			}
			/*
			 * We still have 'extra' number of bytes leftover.
			 */
			if (l) {
				u32 v = *addr;

				if (l + extra >= sizeof(u32)) {
					data |= set_upper_bits(v, shift);
					len -= l + extra - sizeof(u32);
					if (len == length) {
						last = data;
						break;
					}
					__raw_writel(data, piobuf);
					piobuf++;
					extra = 0;
					data = 0;
				} else {
					/* Clear unused upper bytes */
					data |= clear_upper_bytes(v, l,
								  extra);
					if (len == length) {
						last = data;
						break;
					}
					extra += l;
				}
			} else if (len == length) {
				last = data;
				break;
			}
		} else if (len == length) {
			u32 w;

			/*
			 * Need to round up for the last dword in the
			 * packet.
			 */
			w = (len + 3) >> 2;
			__iowrite32_copy(piobuf, ss->sge.vaddr, w - 1);
			piobuf += w - 1;
			last = ((u32 *) ss->sge.vaddr)[w - 1];
			break;
		} else {
			u32 w = len >> 2;

			__iowrite32_copy(piobuf, ss->sge.vaddr, w);
			piobuf += w;

			extra = len & (sizeof(u32) - 1);
			if (extra) {
				u32 v = ((u32 *) ss->sge.vaddr)[w];

				/* Clear unused upper bytes */
				data = clear_upper_bytes(v, extra, 0);
			}
		}
		update_sge(ss, len);
		length -= len;
	}
	/* must flush early everything before trigger word */
	ipath_flush_wc();
	__raw_writel(last, piobuf);
	/* be sure trigger word is written */
	ipath_flush_wc();
	update_sge(ss, length);
}

/**
 * ipath_verbs_send - send a packet from the verbs layer
 * @dd: the infinipath device
 * @hdrwords: the number of works in the header
 * @hdr: the packet header
 * @len: the length of the packet in bytes
 * @ss: the SGE to send
 *
 * This is like ipath_sma_send_pkt() in that we need to be able to send
 * packets after the chip is initialized (MADs) but also like
 * ipath_layer_send_hdr() since its used by the verbs layer.
 */
int ipath_verbs_send(struct ipath_devdata *dd, u32 hdrwords,
		     u32 *hdr, u32 len, struct ipath_sge_state *ss)
{
	u32 __iomem *piobuf;
	u32 plen;
	int ret;

	/* +1 is for the qword padding of pbc */
	plen = hdrwords + ((len + 3) >> 2) + 1;
	if (unlikely((plen << 2) > dd->ipath_ibmaxlen)) {
		ipath_dbg("packet len 0x%x too long, failing\n", plen);
		ret = -EINVAL;
		goto bail;
	}

	/* Get a PIO buffer to use. */
	piobuf = ipath_getpiobuf(dd, NULL);
	if (unlikely(piobuf == NULL)) {
		ret = -EBUSY;
		goto bail;
	}

	/*
	 * Write len to control qword, no flags.
	 * We have to flush after the PBC for correctness on some cpus
	 * or WC buffer can be written out of order.
	 */
	writeq(plen, piobuf);
	ipath_flush_wc();
	piobuf += 2;
	if (len == 0) {
		/*
		 * If there is just the header portion, must flush before
		 * writing last word of header for correctness, and after
		 * the last header word (trigger word).
		 */
		__iowrite32_copy(piobuf, hdr, hdrwords - 1);
		ipath_flush_wc();
		__raw_writel(hdr[hdrwords - 1], piobuf + hdrwords - 1);
		ipath_flush_wc();
		ret = 0;
		goto bail;
	}

	__iowrite32_copy(piobuf, hdr, hdrwords);
	piobuf += hdrwords;

	/* The common case is aligned and contained in one segment. */
	if (likely(ss->num_sge == 1 && len <= ss->sge.length &&
		   !((unsigned long)ss->sge.vaddr & (sizeof(u32) - 1)))) {
		u32 w;

		/* Need to round up for the last dword in the packet. */
		w = (len + 3) >> 2;
		__iowrite32_copy(piobuf, ss->sge.vaddr, w - 1);
		/* must flush early everything before trigger word */
		ipath_flush_wc();
		__raw_writel(((u32 *) ss->sge.vaddr)[w - 1],
			     piobuf + w - 1);
		/* be sure trigger word is written */
		ipath_flush_wc();
		update_sge(ss, len);
		ret = 0;
		goto bail;
	}
	copy_io(piobuf, ss, len);
	ret = 0;

bail:
	return ret;
}

EXPORT_SYMBOL_GPL(ipath_verbs_send);

int ipath_layer_snapshot_counters(struct ipath_devdata *dd, u64 *swords,
				  u64 *rwords, u64 *spkts, u64 *rpkts,
				  u64 *xmit_wait)
{
	int ret;

	if (!(dd->ipath_flags & IPATH_INITTED)) {
		/* no hardware, freeze, etc. */
		ipath_dbg("unit %u not usable\n", dd->ipath_unit);
		ret = -EINVAL;
		goto bail;
	}
	*swords = ipath_snap_cntr(dd, dd->ipath_cregs->cr_wordsendcnt);
	*rwords = ipath_snap_cntr(dd, dd->ipath_cregs->cr_wordrcvcnt);
	*spkts = ipath_snap_cntr(dd, dd->ipath_cregs->cr_pktsendcnt);
	*rpkts = ipath_snap_cntr(dd, dd->ipath_cregs->cr_pktrcvcnt);
	*xmit_wait = ipath_snap_cntr(dd, dd->ipath_cregs->cr_sendstallcnt);

	ret = 0;

bail:
	return ret;
}

EXPORT_SYMBOL_GPL(ipath_layer_snapshot_counters);

/**
 * ipath_layer_get_counters - get various chip counters
 * @dd: the infinipath device
 * @cntrs: counters are placed here
 *
 * Return the counters needed by recv_pma_get_portcounters().
 */
int ipath_layer_get_counters(struct ipath_devdata *dd,
			      struct ipath_layer_counters *cntrs)
{
	int ret;

	if (!(dd->ipath_flags & IPATH_INITTED)) {
		/* no hardware, freeze, etc. */
		ipath_dbg("unit %u not usable\n", dd->ipath_unit);
		ret = -EINVAL;
		goto bail;
	}
	cntrs->symbol_error_counter =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_ibsymbolerrcnt);
	cntrs->link_error_recovery_counter =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_iblinkerrrecovcnt);
	cntrs->link_downed_counter =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_iblinkdowncnt);
	cntrs->port_rcv_errors =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_rxdroppktcnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_rcvovflcnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_portovflcnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_errrcvflowctrlcnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_err_rlencnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_invalidrlencnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_erricrccnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_errvcrccnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_errlpcrccnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_errlinkcnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_badformatcnt);
	cntrs->port_rcv_remphys_errors =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_rcvebpcnt);
	cntrs->port_xmit_discards =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_unsupvlcnt);
	cntrs->port_xmit_data =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_wordsendcnt);
	cntrs->port_rcv_data =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_wordrcvcnt);
	cntrs->port_xmit_packets =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_pktsendcnt);
	cntrs->port_rcv_packets =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_pktrcvcnt);

	ret = 0;

bail:
	return ret;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_counters);

int ipath_layer_want_buffer(struct ipath_devdata *dd)
{
	set_bit(IPATH_S_PIOINTBUFAVAIL, &dd->ipath_sendctrl);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl,
			 dd->ipath_sendctrl);

	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_want_buffer);

int ipath_layer_send_hdr(struct ipath_devdata *dd, struct ether_header *hdr)
{
	int ret = 0;
	u32 __iomem *piobuf;
	u32 plen, *uhdr;
	size_t count;
	__be16 vlsllnh;

	if (!(dd->ipath_flags & IPATH_RCVHDRSZ_SET)) {
		ipath_dbg("send while not open\n");
		ret = -EINVAL;
	} else
		if ((dd->ipath_flags & (IPATH_LINKUNK | IPATH_LINKDOWN)) ||
		    dd->ipath_lid == 0) {
			/*
			 * lid check is for when sma hasn't yet configured
			 */
			ret = -ENETDOWN;
			ipath_cdbg(VERBOSE, "send while not ready, "
				   "mylid=%u, flags=0x%x\n",
				   dd->ipath_lid, dd->ipath_flags);
		}

	vlsllnh = *((__be16 *) hdr);
	if (vlsllnh != htons(IPS_LRH_BTH)) {
		ipath_dbg("Warning: lrh[0] wrong (%x, not %x); "
			  "not sending\n", be16_to_cpu(vlsllnh),
			  IPS_LRH_BTH);
		ret = -EINVAL;
	}
	if (ret)
		goto done;

	/* Get a PIO buffer to use. */
	piobuf = ipath_getpiobuf(dd, NULL);
	if (piobuf == NULL) {
		ret = -EBUSY;
		goto done;
	}

	plen = (sizeof(*hdr) >> 2); /* actual length */
	ipath_cdbg(EPKT, "0x%x+1w pio %p\n", plen, piobuf);

	writeq(plen+1, piobuf); /* len (+1 for pad) to pbc, no flags */
	ipath_flush_wc();
	piobuf += 2;
	uhdr = (u32 *)hdr;
	count = plen-1; /* amount we can copy before trigger word */
	__iowrite32_copy(piobuf, uhdr, count);
	ipath_flush_wc();
	__raw_writel(uhdr[count], piobuf + count);
	ipath_flush_wc(); /* ensure it's sent, now */

	ipath_stats.sps_ether_spkts++;	/* ether packet sent */

done:
	return ret;
}

EXPORT_SYMBOL_GPL(ipath_layer_send_hdr);

int ipath_layer_set_piointbufavail_int(struct ipath_devdata *dd)
{
	set_bit(IPATH_S_PIOINTBUFAVAIL, &dd->ipath_sendctrl);

	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl,
			 dd->ipath_sendctrl);
	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_set_piointbufavail_int);

int ipath_layer_enable_timer(struct ipath_devdata *dd)
{
	/*
	 * HT-400 has a design flaw where the chip and kernel idea
	 * of the tail register don't always agree, and therefore we won't
	 * get an interrupt on the next packet received.
	 * If the board supports per packet receive interrupts, use it.
	 * Otherwise, the timer function periodically checks for packets
	 * to cover this case.
	 * Either way, the timer is needed for verbs layer related
	 * processing.
	 */
	if (dd->ipath_flags & IPATH_GPIO_INTR) {
		ipath_write_kreg(dd, dd->ipath_kregs->kr_debugportselect,
				 0x2074076542310ULL);
		/* Enable GPIO bit 2 interrupt */
		ipath_write_kreg(dd, dd->ipath_kregs->kr_gpio_mask,
				 (u64) (1 << 2));
	}

	init_timer(&dd->verbs_layer.l_timer);
	dd->verbs_layer.l_timer.function = __ipath_verbs_timer;
	dd->verbs_layer.l_timer.data = (unsigned long)dd;
	dd->verbs_layer.l_timer.expires = jiffies + 1;
	add_timer(&dd->verbs_layer.l_timer);

	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_enable_timer);

int ipath_layer_disable_timer(struct ipath_devdata *dd)
{
	/* Disable GPIO bit 2 interrupt */
	if (dd->ipath_flags & IPATH_GPIO_INTR)
		ipath_write_kreg(dd, dd->ipath_kregs->kr_gpio_mask, 0);

	del_timer_sync(&dd->verbs_layer.l_timer);

	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_disable_timer);

/**
 * ipath_layer_set_verbs_flags - set the verbs layer flags
 * @dd: the infinipath device
 * @flags: the flags to set
 */
int ipath_layer_set_verbs_flags(struct ipath_devdata *dd, unsigned flags)
{
	struct ipath_devdata *ss;
	unsigned long lflags;

	spin_lock_irqsave(&ipath_devs_lock, lflags);

	list_for_each_entry(ss, &ipath_dev_list, ipath_list) {
		if (!(ss->ipath_flags & IPATH_INITTED))
			continue;
		if ((flags & IPATH_VERBS_KERNEL_SMA) &&
		    !(*ss->ipath_statusp & IPATH_STATUS_SMA))
			*ss->ipath_statusp |= IPATH_STATUS_OIB_SMA;
		else
			*ss->ipath_statusp &= ~IPATH_STATUS_OIB_SMA;
	}

	spin_unlock_irqrestore(&ipath_devs_lock, lflags);

	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_set_verbs_flags);

/**
 * ipath_layer_get_npkeys - return the size of the PKEY table for port 0
 * @dd: the infinipath device
 */
unsigned ipath_layer_get_npkeys(struct ipath_devdata *dd)
{
	return ARRAY_SIZE(dd->ipath_pd[0]->port_pkeys);
}

EXPORT_SYMBOL_GPL(ipath_layer_get_npkeys);

/**
 * ipath_layer_get_pkey - return the indexed PKEY from the port 0 PKEY table
 * @dd: the infinipath device
 * @index: the PKEY index
 */
unsigned ipath_layer_get_pkey(struct ipath_devdata *dd, unsigned index)
{
	unsigned ret;

	if (index >= ARRAY_SIZE(dd->ipath_pd[0]->port_pkeys))
		ret = 0;
	else
		ret = dd->ipath_pd[0]->port_pkeys[index];

	return ret;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_pkey);

/**
 * ipath_layer_get_pkeys - return the PKEY table for port 0
 * @dd: the infinipath device
 * @pkeys: the pkey table is placed here
 */
int ipath_layer_get_pkeys(struct ipath_devdata *dd, u16 * pkeys)
{
	struct ipath_portdata *pd = dd->ipath_pd[0];

	memcpy(pkeys, pd->port_pkeys, sizeof(pd->port_pkeys));

	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_pkeys);

/**
 * rm_pkey - decrecment the reference count for the given PKEY
 * @dd: the infinipath device
 * @key: the PKEY index
 *
 * Return true if this was the last reference and the hardware table entry
 * needs to be changed.
 */
static int rm_pkey(struct ipath_devdata *dd, u16 key)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(dd->ipath_pkeys); i++) {
		if (dd->ipath_pkeys[i] != key)
			continue;
		if (atomic_dec_and_test(&dd->ipath_pkeyrefs[i])) {
			dd->ipath_pkeys[i] = 0;
			ret = 1;
			goto bail;
		}
		break;
	}

	ret = 0;

bail:
	return ret;
}

/**
 * add_pkey - add the given PKEY to the hardware table
 * @dd: the infinipath device
 * @key: the PKEY
 *
 * Return an error code if unable to add the entry, zero if no change,
 * or 1 if the hardware PKEY register needs to be updated.
 */
static int add_pkey(struct ipath_devdata *dd, u16 key)
{
	int i;
	u16 lkey = key & 0x7FFF;
	int any = 0;
	int ret;

	if (lkey == 0x7FFF) {
		ret = 0;
		goto bail;
	}

	/* Look for an empty slot or a matching PKEY. */
	for (i = 0; i < ARRAY_SIZE(dd->ipath_pkeys); i++) {
		if (!dd->ipath_pkeys[i]) {
			any++;
			continue;
		}
		/* If it matches exactly, try to increment the ref count */
		if (dd->ipath_pkeys[i] == key) {
			if (atomic_inc_return(&dd->ipath_pkeyrefs[i]) > 1) {
				ret = 0;
				goto bail;
			}
			/* Lost the race. Look for an empty slot below. */
			atomic_dec(&dd->ipath_pkeyrefs[i]);
			any++;
		}
		/*
		 * It makes no sense to have both the limited and unlimited
		 * PKEY set at the same time since the unlimited one will
		 * disable the limited one.
		 */
		if ((dd->ipath_pkeys[i] & 0x7FFF) == lkey) {
			ret = -EEXIST;
			goto bail;
		}
	}
	if (!any) {
		ret = -EBUSY;
		goto bail;
	}
	for (i = 0; i < ARRAY_SIZE(dd->ipath_pkeys); i++) {
		if (!dd->ipath_pkeys[i] &&
		    atomic_inc_return(&dd->ipath_pkeyrefs[i]) == 1) {
			/* for ipathstats, etc. */
			ipath_stats.sps_pkeys[i] = lkey;
			dd->ipath_pkeys[i] = key;
			ret = 1;
			goto bail;
		}
	}
	ret = -EBUSY;

bail:
	return ret;
}

/**
 * ipath_layer_set_pkeys - set the PKEY table for port 0
 * @dd: the infinipath device
 * @pkeys: the PKEY table
 */
int ipath_layer_set_pkeys(struct ipath_devdata *dd, u16 * pkeys)
{
	struct ipath_portdata *pd;
	int i;
	int changed = 0;

	pd = dd->ipath_pd[0];

	for (i = 0; i < ARRAY_SIZE(pd->port_pkeys); i++) {
		u16 key = pkeys[i];
		u16 okey = pd->port_pkeys[i];

		if (key == okey)
			continue;
		/*
		 * The value of this PKEY table entry is changing.
		 * Remove the old entry in the hardware's array of PKEYs.
		 */
		if (okey & 0x7FFF)
			changed |= rm_pkey(dd, okey);
		if (key & 0x7FFF) {
			int ret = add_pkey(dd, key);

			if (ret < 0)
				key = 0;
			else
				changed |= ret;
		}
		pd->port_pkeys[i] = key;
	}
	if (changed) {
		u64 pkey;

		pkey = (u64) dd->ipath_pkeys[0] |
			((u64) dd->ipath_pkeys[1] << 16) |
			((u64) dd->ipath_pkeys[2] << 32) |
			((u64) dd->ipath_pkeys[3] << 48);
		ipath_cdbg(VERBOSE, "p0 new pkey reg %llx\n",
			   (unsigned long long) pkey);
		ipath_write_kreg(dd, dd->ipath_kregs->kr_partitionkey,
				 pkey);
	}
	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_set_pkeys);

/**
 * ipath_layer_get_linkdowndefaultstate - get the default linkdown state
 * @dd: the infinipath device
 *
 * Returns zero if the default is POLL, 1 if the default is SLEEP.
 */
int ipath_layer_get_linkdowndefaultstate(struct ipath_devdata *dd)
{
	return !!(dd->ipath_ibcctrl & INFINIPATH_IBCC_LINKDOWNDEFAULTSTATE);
}

EXPORT_SYMBOL_GPL(ipath_layer_get_linkdowndefaultstate);

/**
 * ipath_layer_set_linkdowndefaultstate - set the default linkdown state
 * @dd: the infinipath device
 * @sleep: the new state
 *
 * Note that this will only take effect when the link state changes.
 */
int ipath_layer_set_linkdowndefaultstate(struct ipath_devdata *dd,
					 int sleep)
{
	if (sleep)
		dd->ipath_ibcctrl |= INFINIPATH_IBCC_LINKDOWNDEFAULTSTATE;
	else
		dd->ipath_ibcctrl &= ~INFINIPATH_IBCC_LINKDOWNDEFAULTSTATE;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_ibcctrl,
			 dd->ipath_ibcctrl);
	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_set_linkdowndefaultstate);

int ipath_layer_get_phyerrthreshold(struct ipath_devdata *dd)
{
	return (dd->ipath_ibcctrl >>
		INFINIPATH_IBCC_PHYERRTHRESHOLD_SHIFT) &
		INFINIPATH_IBCC_PHYERRTHRESHOLD_MASK;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_phyerrthreshold);

/**
 * ipath_layer_set_phyerrthreshold - set the physical error threshold
 * @dd: the infinipath device
 * @n: the new threshold
 *
 * Note that this will only take effect when the link state changes.
 */
int ipath_layer_set_phyerrthreshold(struct ipath_devdata *dd, unsigned n)
{
	unsigned v;

	v = (dd->ipath_ibcctrl >> INFINIPATH_IBCC_PHYERRTHRESHOLD_SHIFT) &
		INFINIPATH_IBCC_PHYERRTHRESHOLD_MASK;
	if (v != n) {
		dd->ipath_ibcctrl &=
			~(INFINIPATH_IBCC_PHYERRTHRESHOLD_MASK <<
			  INFINIPATH_IBCC_PHYERRTHRESHOLD_SHIFT);
		dd->ipath_ibcctrl |=
			(u64) n << INFINIPATH_IBCC_PHYERRTHRESHOLD_SHIFT;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_ibcctrl,
				 dd->ipath_ibcctrl);
	}
	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_set_phyerrthreshold);

int ipath_layer_get_overrunthreshold(struct ipath_devdata *dd)
{
	return (dd->ipath_ibcctrl >>
		INFINIPATH_IBCC_OVERRUNTHRESHOLD_SHIFT) &
		INFINIPATH_IBCC_OVERRUNTHRESHOLD_MASK;
}

EXPORT_SYMBOL_GPL(ipath_layer_get_overrunthreshold);

/**
 * ipath_layer_set_overrunthreshold - set the overrun threshold
 * @dd: the infinipath device
 * @n: the new threshold
 *
 * Note that this will only take effect when the link state changes.
 */
int ipath_layer_set_overrunthreshold(struct ipath_devdata *dd, unsigned n)
{
	unsigned v;

	v = (dd->ipath_ibcctrl >> INFINIPATH_IBCC_OVERRUNTHRESHOLD_SHIFT) &
		INFINIPATH_IBCC_OVERRUNTHRESHOLD_MASK;
	if (v != n) {
		dd->ipath_ibcctrl &=
			~(INFINIPATH_IBCC_OVERRUNTHRESHOLD_MASK <<
			  INFINIPATH_IBCC_OVERRUNTHRESHOLD_SHIFT);
		dd->ipath_ibcctrl |=
			(u64) n << INFINIPATH_IBCC_OVERRUNTHRESHOLD_SHIFT;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_ibcctrl,
				 dd->ipath_ibcctrl);
	}
	return 0;
}

EXPORT_SYMBOL_GPL(ipath_layer_set_overrunthreshold);

int ipath_layer_get_boardname(struct ipath_devdata *dd, char *name,
			      size_t namelen)
{
	return dd->ipath_f_get_boardname(dd, name, namelen);
}
EXPORT_SYMBOL_GPL(ipath_layer_get_boardname);

u32 ipath_layer_get_rcvhdrentsize(struct ipath_devdata *dd)
{
	return dd->ipath_rcvhdrentsize;
}
EXPORT_SYMBOL_GPL(ipath_layer_get_rcvhdrentsize);

/*
 * Copyright (c) 2006 QLogic, Inc. All rights reserved.
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
#include <asm/byteorder.h>

#include "ipath_kernel.h"
#include "ipath_layer.h"
#include "ipath_verbs.h"
#include "ipath_common.h"

/* Acquire before ipath_devs_lock. */
static DEFINE_MUTEX(ipath_layer_mutex);

u16 ipath_layer_rcv_opcode;

static int (*layer_intr)(void *, u32);
static int (*layer_rcv)(void *, void *, struct sk_buff *);
static int (*layer_rcv_lid)(void *, void *);

static void *(*layer_add_one)(int, struct ipath_devdata *);
static void (*layer_remove_one)(void *);

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

void ipath_layer_lid_changed(struct ipath_devdata *dd)
{
	mutex_lock(&ipath_layer_mutex);

	if (dd->ipath_layer.l_arg && layer_intr)
		layer_intr(dd->ipath_layer.l_arg, IPATH_LAYER_INT_LID);

	mutex_unlock(&ipath_layer_mutex);
}

void ipath_layer_add(struct ipath_devdata *dd)
{
	mutex_lock(&ipath_layer_mutex);

	if (layer_add_one)
		dd->ipath_layer.l_arg =
			layer_add_one(dd->ipath_unit, dd);

	mutex_unlock(&ipath_layer_mutex);
}

void ipath_layer_remove(struct ipath_devdata *dd)
{
	mutex_lock(&ipath_layer_mutex);

	if (dd->ipath_layer.l_arg && layer_remove_one) {
		layer_remove_one(dd->ipath_layer.l_arg);
		dd->ipath_layer.l_arg = NULL;
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

int ipath_layer_open(struct ipath_devdata *dd, u32 * pktmax)
{
	int ret;
	u32 intval = 0;

	mutex_lock(&ipath_layer_mutex);

	if (!dd->ipath_layer.l_arg) {
		ret = -EINVAL;
		goto bail;
	}

	ret = ipath_setrcvhdrsize(dd, IPATH_HEADER_QUEUE_WORDS);

	if (ret < 0)
		goto bail;

	*pktmax = dd->ipath_ibmaxlen;

	if (*dd->ipath_statusp & IPATH_STATUS_IB_READY)
		intval |= IPATH_LAYER_INT_IF_UP;
	if (dd->ipath_lid)
		intval |= IPATH_LAYER_INT_LID;
	if (dd->ipath_mlid)
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
	if (vlsllnh != htons(IPATH_LRH_BTH)) {
		ipath_dbg("Warning: lrh[0] wrong (%x, not %x); "
			  "not sending\n", be16_to_cpu(vlsllnh),
			  IPATH_LRH_BTH);
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

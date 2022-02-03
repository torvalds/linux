// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author:
 *  Zhigang.Wei <zhigang.wei@mediatek.com>
 *  Chunfeng.Yun <chunfeng.yun@mediatek.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "xhci.h"
#include "xhci-mtk.h"

#define SSP_BW_BOUNDARY	130000
#define SS_BW_BOUNDARY	51000
/* table 5-5. High-speed Isoc Transaction Limits in usb_20 spec */
#define HS_BW_BOUNDARY	6144
/* usb2 spec section11.18.1: at most 188 FS bytes per microframe */
#define FS_PAYLOAD_MAX 188
/*
 * max number of microframes for split transfer,
 * for fs isoc in : 1 ss + 1 idle + 7 cs
 */
#define TT_MICROFRAMES_MAX 9

#define DBG_BUF_EN	64

/* schedule error type */
#define ESCH_SS_Y6		1001
#define ESCH_SS_OVERLAP		1002
#define ESCH_CS_OVERFLOW	1003
#define ESCH_BW_OVERFLOW	1004
#define ESCH_FIXME		1005

/* mtk scheduler bitmasks */
#define EP_BPKTS(p)	((p) & 0x7f)
#define EP_BCSCOUNT(p)	(((p) & 0x7) << 8)
#define EP_BBM(p)	((p) << 11)
#define EP_BOFFSET(p)	((p) & 0x3fff)
#define EP_BREPEAT(p)	(((p) & 0x7fff) << 16)

static char *sch_error_string(int err_num)
{
	switch (err_num) {
	case ESCH_SS_Y6:
		return "Can't schedule Start-Split in Y6";
	case ESCH_SS_OVERLAP:
		return "Can't find a suitable Start-Split location";
	case ESCH_CS_OVERFLOW:
		return "The last Complete-Split is greater than 7";
	case ESCH_BW_OVERFLOW:
		return "Bandwidth exceeds the maximum limit";
	case ESCH_FIXME:
		return "FIXME, to be resolved";
	default:
		return "Unknown";
	}
}

static int is_fs_or_ls(enum usb_device_speed speed)
{
	return speed == USB_SPEED_FULL || speed == USB_SPEED_LOW;
}

static const char *
decode_ep(struct usb_host_endpoint *ep, enum usb_device_speed speed)
{
	static char buf[DBG_BUF_EN];
	struct usb_endpoint_descriptor *epd = &ep->desc;
	unsigned int interval;
	const char *unit;

	interval = usb_decode_interval(epd, speed);
	if (interval % 1000) {
		unit = "us";
	} else {
		unit = "ms";
		interval /= 1000;
	}

	snprintf(buf, DBG_BUF_EN, "%s ep%d%s %s, mpkt:%d, interval:%d/%d%s",
		 usb_speed_string(speed), usb_endpoint_num(epd),
		 usb_endpoint_dir_in(epd) ? "in" : "out",
		 usb_ep_type_string(usb_endpoint_type(epd)),
		 usb_endpoint_maxp(epd), epd->bInterval, interval, unit);

	return buf;
}

static u32 get_bw_boundary(enum usb_device_speed speed)
{
	u32 boundary;

	switch (speed) {
	case USB_SPEED_SUPER_PLUS:
		boundary = SSP_BW_BOUNDARY;
		break;
	case USB_SPEED_SUPER:
		boundary = SS_BW_BOUNDARY;
		break;
	default:
		boundary = HS_BW_BOUNDARY;
		break;
	}

	return boundary;
}

/*
* get the bandwidth domain which @ep belongs to.
*
* the bandwidth domain array is saved to @sch_array of struct xhci_hcd_mtk,
* each HS root port is treated as a single bandwidth domain,
* but each SS root port is treated as two bandwidth domains, one for IN eps,
* one for OUT eps.
* @real_port value is defined as follow according to xHCI spec:
* 1 for SSport0, ..., N+1 for SSportN, N+2 for HSport0, N+3 for HSport1, etc
* so the bandwidth domain array is organized as follow for simplification:
* SSport0-OUT, SSport0-IN, ..., SSportX-OUT, SSportX-IN, HSport0, ..., HSportY
*/
static struct mu3h_sch_bw_info *
get_bw_info(struct xhci_hcd_mtk *mtk, struct usb_device *udev,
	    struct usb_host_endpoint *ep)
{
	struct xhci_hcd *xhci = hcd_to_xhci(mtk->hcd);
	struct xhci_virt_device *virt_dev;
	int bw_index;

	virt_dev = xhci->devs[udev->slot_id];
	if (!virt_dev->real_port) {
		WARN_ONCE(1, "%s invalid real_port\n", dev_name(&udev->dev));
		return NULL;
	}

	if (udev->speed >= USB_SPEED_SUPER) {
		if (usb_endpoint_dir_out(&ep->desc))
			bw_index = (virt_dev->real_port - 1) * 2;
		else
			bw_index = (virt_dev->real_port - 1) * 2 + 1;
	} else {
		/* add one more for each SS port */
		bw_index = virt_dev->real_port + xhci->usb3_rhub.num_ports - 1;
	}

	return &mtk->sch_array[bw_index];
}

static u32 get_esit(struct xhci_ep_ctx *ep_ctx)
{
	u32 esit;

	esit = 1 << CTX_TO_EP_INTERVAL(le32_to_cpu(ep_ctx->ep_info));
	if (esit > XHCI_MTK_MAX_ESIT)
		esit = XHCI_MTK_MAX_ESIT;

	return esit;
}

static struct mu3h_sch_tt *find_tt(struct usb_device *udev)
{
	struct usb_tt *utt = udev->tt;
	struct mu3h_sch_tt *tt, **tt_index, **ptt;
	bool allocated_index = false;

	if (!utt)
		return NULL;	/* Not below a TT */

	/*
	 * Find/create our data structure.
	 * For hubs with a single TT, we get it directly.
	 * For hubs with multiple TTs, there's an extra level of pointers.
	 */
	tt_index = NULL;
	if (utt->multi) {
		tt_index = utt->hcpriv;
		if (!tt_index) {	/* Create the index array */
			tt_index = kcalloc(utt->hub->maxchild,
					sizeof(*tt_index), GFP_KERNEL);
			if (!tt_index)
				return ERR_PTR(-ENOMEM);
			utt->hcpriv = tt_index;
			allocated_index = true;
		}
		ptt = &tt_index[udev->ttport - 1];
	} else {
		ptt = (struct mu3h_sch_tt **) &utt->hcpriv;
	}

	tt = *ptt;
	if (!tt) {	/* Create the mu3h_sch_tt */
		tt = kzalloc(sizeof(*tt), GFP_KERNEL);
		if (!tt) {
			if (allocated_index) {
				utt->hcpriv = NULL;
				kfree(tt_index);
			}
			return ERR_PTR(-ENOMEM);
		}
		INIT_LIST_HEAD(&tt->ep_list);
		*ptt = tt;
	}

	return tt;
}

/* Release the TT above udev, if it's not in use */
static void drop_tt(struct usb_device *udev)
{
	struct usb_tt *utt = udev->tt;
	struct mu3h_sch_tt *tt, **tt_index, **ptt;
	int i, cnt;

	if (!utt || !utt->hcpriv)
		return;		/* Not below a TT, or never allocated */

	cnt = 0;
	if (utt->multi) {
		tt_index = utt->hcpriv;
		ptt = &tt_index[udev->ttport - 1];
		/*  How many entries are left in tt_index? */
		for (i = 0; i < utt->hub->maxchild; ++i)
			cnt += !!tt_index[i];
	} else {
		tt_index = NULL;
		ptt = (struct mu3h_sch_tt **)&utt->hcpriv;
	}

	tt = *ptt;
	if (!tt || !list_empty(&tt->ep_list))
		return;		/* never allocated , or still in use*/

	*ptt = NULL;
	kfree(tt);

	if (cnt == 1) {
		utt->hcpriv = NULL;
		kfree(tt_index);
	}
}

static struct mu3h_sch_ep_info *
create_sch_ep(struct xhci_hcd_mtk *mtk, struct usb_device *udev,
	      struct usb_host_endpoint *ep, struct xhci_ep_ctx *ep_ctx)
{
	struct mu3h_sch_ep_info *sch_ep;
	struct mu3h_sch_bw_info *bw_info;
	struct mu3h_sch_tt *tt = NULL;
	u32 len_bw_budget_table;
	size_t mem_size;

	bw_info = get_bw_info(mtk, udev, ep);
	if (!bw_info)
		return ERR_PTR(-ENODEV);

	if (is_fs_or_ls(udev->speed))
		len_bw_budget_table = TT_MICROFRAMES_MAX;
	else if ((udev->speed >= USB_SPEED_SUPER)
			&& usb_endpoint_xfer_isoc(&ep->desc))
		len_bw_budget_table = get_esit(ep_ctx);
	else
		len_bw_budget_table = 1;

	mem_size = sizeof(struct mu3h_sch_ep_info) +
			len_bw_budget_table * sizeof(u32);
	sch_ep = kzalloc(mem_size, GFP_KERNEL);
	if (!sch_ep)
		return ERR_PTR(-ENOMEM);

	if (is_fs_or_ls(udev->speed)) {
		tt = find_tt(udev);
		if (IS_ERR(tt)) {
			kfree(sch_ep);
			return ERR_PTR(-ENOMEM);
		}
	}

	sch_ep->bw_info = bw_info;
	sch_ep->sch_tt = tt;
	sch_ep->ep = ep;
	sch_ep->speed = udev->speed;
	INIT_LIST_HEAD(&sch_ep->endpoint);
	INIT_LIST_HEAD(&sch_ep->tt_endpoint);
	INIT_HLIST_NODE(&sch_ep->hentry);

	return sch_ep;
}

static void setup_sch_info(struct xhci_ep_ctx *ep_ctx,
			   struct mu3h_sch_ep_info *sch_ep)
{
	u32 ep_type;
	u32 maxpkt;
	u32 max_burst;
	u32 mult;
	u32 esit_pkts;
	u32 max_esit_payload;
	u32 *bwb_table = sch_ep->bw_budget_table;
	int i;

	ep_type = CTX_TO_EP_TYPE(le32_to_cpu(ep_ctx->ep_info2));
	maxpkt = MAX_PACKET_DECODED(le32_to_cpu(ep_ctx->ep_info2));
	max_burst = CTX_TO_MAX_BURST(le32_to_cpu(ep_ctx->ep_info2));
	mult = CTX_TO_EP_MULT(le32_to_cpu(ep_ctx->ep_info));
	max_esit_payload =
		(CTX_TO_MAX_ESIT_PAYLOAD_HI(
			le32_to_cpu(ep_ctx->ep_info)) << 16) |
		 CTX_TO_MAX_ESIT_PAYLOAD(le32_to_cpu(ep_ctx->tx_info));

	sch_ep->esit = get_esit(ep_ctx);
	sch_ep->num_esit = XHCI_MTK_MAX_ESIT / sch_ep->esit;
	sch_ep->ep_type = ep_type;
	sch_ep->maxpkt = maxpkt;
	sch_ep->offset = 0;
	sch_ep->burst_mode = 0;
	sch_ep->repeat = 0;

	if (sch_ep->speed == USB_SPEED_HIGH) {
		sch_ep->cs_count = 0;

		/*
		 * usb_20 spec section5.9
		 * a single microframe is enough for HS synchromous endpoints
		 * in a interval
		 */
		sch_ep->num_budget_microframes = 1;

		/*
		 * xHCI spec section6.2.3.4
		 * @max_burst is the number of additional transactions
		 * opportunities per microframe
		 */
		sch_ep->pkts = max_burst + 1;
		sch_ep->bw_cost_per_microframe = maxpkt * sch_ep->pkts;
		bwb_table[0] = sch_ep->bw_cost_per_microframe;
	} else if (sch_ep->speed >= USB_SPEED_SUPER) {
		/* usb3_r1 spec section4.4.7 & 4.4.8 */
		sch_ep->cs_count = 0;
		sch_ep->burst_mode = 1;
		/*
		 * some device's (d)wBytesPerInterval is set as 0,
		 * then max_esit_payload is 0, so evaluate esit_pkts from
		 * mult and burst
		 */
		esit_pkts = DIV_ROUND_UP(max_esit_payload, maxpkt);
		if (esit_pkts == 0)
			esit_pkts = (mult + 1) * (max_burst + 1);

		if (ep_type == INT_IN_EP || ep_type == INT_OUT_EP) {
			sch_ep->pkts = esit_pkts;
			sch_ep->num_budget_microframes = 1;
			bwb_table[0] = maxpkt * sch_ep->pkts;
		}

		if (ep_type == ISOC_IN_EP || ep_type == ISOC_OUT_EP) {

			if (sch_ep->esit == 1)
				sch_ep->pkts = esit_pkts;
			else if (esit_pkts <= sch_ep->esit)
				sch_ep->pkts = 1;
			else
				sch_ep->pkts = roundup_pow_of_two(esit_pkts)
					/ sch_ep->esit;

			sch_ep->num_budget_microframes =
				DIV_ROUND_UP(esit_pkts, sch_ep->pkts);

			sch_ep->repeat = !!(sch_ep->num_budget_microframes > 1);
			sch_ep->bw_cost_per_microframe = maxpkt * sch_ep->pkts;

			for (i = 0; i < sch_ep->num_budget_microframes - 1; i++)
				bwb_table[i] = sch_ep->bw_cost_per_microframe;

			/* last one <= bw_cost_per_microframe */
			bwb_table[i] = maxpkt * esit_pkts
				       - i * sch_ep->bw_cost_per_microframe;
		}
	} else if (is_fs_or_ls(sch_ep->speed)) {
		sch_ep->pkts = 1; /* at most one packet for each microframe */

		/*
		 * num_budget_microframes and cs_count will be updated when
		 * check TT for INT_OUT_EP, ISOC/INT_IN_EP type
		 */
		sch_ep->cs_count = DIV_ROUND_UP(maxpkt, FS_PAYLOAD_MAX);
		sch_ep->num_budget_microframes = sch_ep->cs_count;
		sch_ep->bw_cost_per_microframe =
			(maxpkt < FS_PAYLOAD_MAX) ? maxpkt : FS_PAYLOAD_MAX;

		/* init budget table */
		if (ep_type == ISOC_OUT_EP) {
			for (i = 0; i < sch_ep->num_budget_microframes; i++)
				bwb_table[i] =	sch_ep->bw_cost_per_microframe;
		} else if (ep_type == INT_OUT_EP) {
			/* only first one consumes bandwidth, others as zero */
			bwb_table[0] = sch_ep->bw_cost_per_microframe;
		} else { /* INT_IN_EP or ISOC_IN_EP */
			bwb_table[0] = 0; /* start split */
			bwb_table[1] = 0; /* idle */
			/*
			 * due to cs_count will be updated according to cs
			 * position, assign all remainder budget array
			 * elements as @bw_cost_per_microframe, but only first
			 * @num_budget_microframes elements will be used later
			 */
			for (i = 2; i < TT_MICROFRAMES_MAX; i++)
				bwb_table[i] =	sch_ep->bw_cost_per_microframe;
		}
	}
}

/* Get maximum bandwidth when we schedule at offset slot. */
static u32 get_max_bw(struct mu3h_sch_bw_info *sch_bw,
	struct mu3h_sch_ep_info *sch_ep, u32 offset)
{
	u32 max_bw = 0;
	u32 bw;
	int i, j, k;

	for (i = 0; i < sch_ep->num_esit; i++) {
		u32 base = offset + i * sch_ep->esit;

		for (j = 0; j < sch_ep->num_budget_microframes; j++) {
			k = XHCI_MTK_BW_INDEX(base + j);
			bw = sch_bw->bus_bw[k] + sch_ep->bw_budget_table[j];
			if (bw > max_bw)
				max_bw = bw;
		}
	}
	return max_bw;
}

static void update_bus_bw(struct mu3h_sch_bw_info *sch_bw,
	struct mu3h_sch_ep_info *sch_ep, bool used)
{
	u32 base;
	int i, j, k;

	for (i = 0; i < sch_ep->num_esit; i++) {
		base = sch_ep->offset + i * sch_ep->esit;
		for (j = 0; j < sch_ep->num_budget_microframes; j++) {
			k = XHCI_MTK_BW_INDEX(base + j);
			if (used)
				sch_bw->bus_bw[k] += sch_ep->bw_budget_table[j];
			else
				sch_bw->bus_bw[k] -= sch_ep->bw_budget_table[j];
		}
	}
}

static int check_fs_bus_bw(struct mu3h_sch_ep_info *sch_ep, int offset)
{
	struct mu3h_sch_tt *tt = sch_ep->sch_tt;
	u32 tmp;
	int base;
	int i, j, k;

	for (i = 0; i < sch_ep->num_esit; i++) {
		base = offset + i * sch_ep->esit;

		/*
		 * Compared with hs bus, no matter what ep type,
		 * the hub will always delay one uframe to send data
		 */
		for (j = 0; j < sch_ep->num_budget_microframes; j++) {
			k = XHCI_MTK_BW_INDEX(base + j);
			tmp = tt->fs_bus_bw[k] + sch_ep->bw_budget_table[j];
			if (tmp > FS_PAYLOAD_MAX)
				return -ESCH_BW_OVERFLOW;
		}
	}

	return 0;
}

static int check_sch_tt(struct mu3h_sch_ep_info *sch_ep, u32 offset)
{
	u32 extra_cs_count;
	u32 start_ss, last_ss;
	u32 start_cs, last_cs;

	if (!sch_ep->sch_tt)
		return 0;

	start_ss = offset % 8;

	if (sch_ep->ep_type == ISOC_OUT_EP) {
		last_ss = start_ss + sch_ep->cs_count - 1;

		/*
		 * usb_20 spec section11.18:
		 * must never schedule Start-Split in Y6
		 */
		if (!(start_ss == 7 || last_ss < 6))
			return -ESCH_SS_Y6;

	} else {
		u32 cs_count = DIV_ROUND_UP(sch_ep->maxpkt, FS_PAYLOAD_MAX);

		/*
		 * usb_20 spec section11.18:
		 * must never schedule Start-Split in Y6
		 */
		if (start_ss == 6)
			return -ESCH_SS_Y6;

		/* one uframe for ss + one uframe for idle */
		start_cs = (start_ss + 2) % 8;
		last_cs = start_cs + cs_count - 1;

		if (last_cs > 7)
			return -ESCH_CS_OVERFLOW;

		if (sch_ep->ep_type == ISOC_IN_EP)
			extra_cs_count = (last_cs == 7) ? 1 : 2;
		else /*  ep_type : INTR IN / INTR OUT */
			extra_cs_count = 1;

		cs_count += extra_cs_count;
		if (cs_count > 7)
			cs_count = 7; /* HW limit */

		sch_ep->cs_count = cs_count;
		/* one for ss, the other for idle */
		sch_ep->num_budget_microframes = cs_count + 2;

		/*
		 * if interval=1, maxp >752, num_budge_micoframe is larger
		 * than sch_ep->esit, will overstep boundary
		 */
		if (sch_ep->num_budget_microframes > sch_ep->esit)
			sch_ep->num_budget_microframes = sch_ep->esit;
	}

	return check_fs_bus_bw(sch_ep, offset);
}

static void update_sch_tt(struct mu3h_sch_ep_info *sch_ep, bool used)
{
	struct mu3h_sch_tt *tt = sch_ep->sch_tt;
	u32 base;
	int i, j, k;

	for (i = 0; i < sch_ep->num_esit; i++) {
		base = sch_ep->offset + i * sch_ep->esit;

		for (j = 0; j < sch_ep->num_budget_microframes; j++) {
			k = XHCI_MTK_BW_INDEX(base + j);
			if (used)
				tt->fs_bus_bw[k] += sch_ep->bw_budget_table[j];
			else
				tt->fs_bus_bw[k] -= sch_ep->bw_budget_table[j];
		}
	}

	if (used)
		list_add_tail(&sch_ep->tt_endpoint, &tt->ep_list);
	else
		list_del(&sch_ep->tt_endpoint);
}

static int load_ep_bw(struct mu3h_sch_bw_info *sch_bw,
		      struct mu3h_sch_ep_info *sch_ep, bool loaded)
{
	if (sch_ep->sch_tt)
		update_sch_tt(sch_ep, loaded);

	/* update bus bandwidth info */
	update_bus_bw(sch_bw, sch_ep, loaded);
	sch_ep->allocated = loaded;

	return 0;
}

static int check_sch_bw(struct mu3h_sch_ep_info *sch_ep)
{
	struct mu3h_sch_bw_info *sch_bw = sch_ep->bw_info;
	const u32 bw_boundary = get_bw_boundary(sch_ep->speed);
	u32 offset;
	u32 worst_bw;
	u32 min_bw = ~0;
	int min_index = -1;
	int ret = 0;

	/*
	 * Search through all possible schedule microframes.
	 * and find a microframe where its worst bandwidth is minimum.
	 */
	for (offset = 0; offset < sch_ep->esit; offset++) {
		ret = check_sch_tt(sch_ep, offset);
		if (ret)
			continue;

		worst_bw = get_max_bw(sch_bw, sch_ep, offset);
		if (worst_bw > bw_boundary)
			continue;

		if (min_bw > worst_bw) {
			min_bw = worst_bw;
			min_index = offset;
		}

		/* use first-fit for LS/FS */
		if (sch_ep->sch_tt && min_index >= 0)
			break;

		if (min_bw == 0)
			break;
	}

	if (min_index < 0)
		return ret ? ret : -ESCH_BW_OVERFLOW;

	sch_ep->offset = min_index;

	return load_ep_bw(sch_bw, sch_ep, true);
}

static void destroy_sch_ep(struct xhci_hcd_mtk *mtk, struct usb_device *udev,
			   struct mu3h_sch_ep_info *sch_ep)
{
	/* only release ep bw check passed by check_sch_bw() */
	if (sch_ep->allocated)
		load_ep_bw(sch_ep->bw_info, sch_ep, false);

	if (sch_ep->sch_tt)
		drop_tt(udev);

	list_del(&sch_ep->endpoint);
	hlist_del(&sch_ep->hentry);
	kfree(sch_ep);
}

static bool need_bw_sch(struct usb_device *udev,
			struct usb_host_endpoint *ep)
{
	bool has_tt = udev->tt && udev->tt->hub->parent;

	/* only for periodic endpoints */
	if (usb_endpoint_xfer_control(&ep->desc)
		|| usb_endpoint_xfer_bulk(&ep->desc))
		return false;

	/*
	 * for LS & FS periodic endpoints which its device is not behind
	 * a TT are also ignored, root-hub will schedule them directly,
	 * but need set @bpkts field of endpoint context to 1.
	 */
	if (is_fs_or_ls(udev->speed) && !has_tt)
		return false;

	/* skip endpoint with zero maxpkt */
	if (usb_endpoint_maxp(&ep->desc) == 0)
		return false;

	return true;
}

int xhci_mtk_sch_init(struct xhci_hcd_mtk *mtk)
{
	struct xhci_hcd *xhci = hcd_to_xhci(mtk->hcd);
	struct mu3h_sch_bw_info *sch_array;
	int num_usb_bus;

	/* ss IN and OUT are separated */
	num_usb_bus = xhci->usb3_rhub.num_ports * 2 + xhci->usb2_rhub.num_ports;

	sch_array = kcalloc(num_usb_bus, sizeof(*sch_array), GFP_KERNEL);
	if (sch_array == NULL)
		return -ENOMEM;

	mtk->sch_array = sch_array;

	INIT_LIST_HEAD(&mtk->bw_ep_chk_list);
	hash_init(mtk->sch_ep_hash);

	return 0;
}

void xhci_mtk_sch_exit(struct xhci_hcd_mtk *mtk)
{
	kfree(mtk->sch_array);
}

static int add_ep_quirk(struct usb_hcd *hcd, struct usb_device *udev,
			struct usb_host_endpoint *ep)
{
	struct xhci_hcd_mtk *mtk = hcd_to_mtk(hcd);
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct xhci_ep_ctx *ep_ctx;
	struct xhci_virt_device *virt_dev;
	struct mu3h_sch_ep_info *sch_ep;
	unsigned int ep_index;

	virt_dev = xhci->devs[udev->slot_id];
	ep_index = xhci_get_endpoint_index(&ep->desc);
	ep_ctx = xhci_get_ep_ctx(xhci, virt_dev->in_ctx, ep_index);

	if (!need_bw_sch(udev, ep)) {
		/*
		 * set @bpkts to 1 if it is LS or FS periodic endpoint, and its
		 * device does not connected through an external HS hub
		 */
		if (usb_endpoint_xfer_int(&ep->desc)
			|| usb_endpoint_xfer_isoc(&ep->desc))
			ep_ctx->reserved[0] = cpu_to_le32(EP_BPKTS(1));

		return 0;
	}

	xhci_dbg(xhci, "%s %s\n", __func__, decode_ep(ep, udev->speed));

	sch_ep = create_sch_ep(mtk, udev, ep, ep_ctx);
	if (IS_ERR_OR_NULL(sch_ep))
		return -ENOMEM;

	setup_sch_info(ep_ctx, sch_ep);

	list_add_tail(&sch_ep->endpoint, &mtk->bw_ep_chk_list);
	hash_add(mtk->sch_ep_hash, &sch_ep->hentry, (unsigned long)ep);

	return 0;
}

static void drop_ep_quirk(struct usb_hcd *hcd, struct usb_device *udev,
			  struct usb_host_endpoint *ep)
{
	struct xhci_hcd_mtk *mtk = hcd_to_mtk(hcd);
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct mu3h_sch_ep_info *sch_ep;
	struct hlist_node *hn;

	if (!need_bw_sch(udev, ep))
		return;

	xhci_dbg(xhci, "%s %s\n", __func__, decode_ep(ep, udev->speed));

	hash_for_each_possible_safe(mtk->sch_ep_hash, sch_ep,
				    hn, hentry, (unsigned long)ep) {
		if (sch_ep->ep == ep) {
			destroy_sch_ep(mtk, udev, sch_ep);
			break;
		}
	}
}

int xhci_mtk_check_bandwidth(struct usb_hcd *hcd, struct usb_device *udev)
{
	struct xhci_hcd_mtk *mtk = hcd_to_mtk(hcd);
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct xhci_virt_device *virt_dev = xhci->devs[udev->slot_id];
	struct mu3h_sch_ep_info *sch_ep;
	int ret;

	xhci_dbg(xhci, "%s() udev %s\n", __func__, dev_name(&udev->dev));

	list_for_each_entry(sch_ep, &mtk->bw_ep_chk_list, endpoint) {
		struct xhci_ep_ctx *ep_ctx;
		struct usb_host_endpoint *ep = sch_ep->ep;
		unsigned int ep_index = xhci_get_endpoint_index(&ep->desc);

		ret = check_sch_bw(sch_ep);
		if (ret) {
			xhci_err(xhci, "Not enough bandwidth! (%s)\n",
				 sch_error_string(-ret));
			return -ENOSPC;
		}

		ep_ctx = xhci_get_ep_ctx(xhci, virt_dev->in_ctx, ep_index);
		ep_ctx->reserved[0] = cpu_to_le32(EP_BPKTS(sch_ep->pkts)
			| EP_BCSCOUNT(sch_ep->cs_count)
			| EP_BBM(sch_ep->burst_mode));
		ep_ctx->reserved[1] = cpu_to_le32(EP_BOFFSET(sch_ep->offset)
			| EP_BREPEAT(sch_ep->repeat));

		xhci_dbg(xhci, " PKTS:%x, CSCOUNT:%x, BM:%x, OFFSET:%x, REPEAT:%x\n",
			sch_ep->pkts, sch_ep->cs_count, sch_ep->burst_mode,
			sch_ep->offset, sch_ep->repeat);
	}

	ret = xhci_check_bandwidth(hcd, udev);
	if (!ret)
		list_del_init(&mtk->bw_ep_chk_list);

	return ret;
}

void xhci_mtk_reset_bandwidth(struct usb_hcd *hcd, struct usb_device *udev)
{
	struct xhci_hcd_mtk *mtk = hcd_to_mtk(hcd);
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct mu3h_sch_ep_info *sch_ep, *tmp;

	xhci_dbg(xhci, "%s() udev %s\n", __func__, dev_name(&udev->dev));

	list_for_each_entry_safe(sch_ep, tmp, &mtk->bw_ep_chk_list, endpoint)
		destroy_sch_ep(mtk, udev, sch_ep);

	xhci_reset_bandwidth(hcd, udev);
}

int xhci_mtk_add_ep(struct usb_hcd *hcd, struct usb_device *udev,
		    struct usb_host_endpoint *ep)
{
	int ret;

	ret = xhci_add_endpoint(hcd, udev, ep);
	if (ret)
		return ret;

	if (ep->hcpriv)
		ret = add_ep_quirk(hcd, udev, ep);

	return ret;
}

int xhci_mtk_drop_ep(struct usb_hcd *hcd, struct usb_device *udev,
		     struct usb_host_endpoint *ep)
{
	int ret;

	ret = xhci_drop_endpoint(hcd, udev, ep);
	if (ret)
		return ret;

	if (ep->hcpriv)
		drop_ep_quirk(hcd, udev, ep);

	return 0;
}

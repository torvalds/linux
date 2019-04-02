// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2009 by Martin Fuzzey
 */

/* this file is part of imx21-hcd.c */

#ifdef CONFIG_DYNAMIC_DE
#define DE
#endif

#ifndef DE

static inline void create_de_files(struct imx21 *imx21) { }
static inline void remove_de_files(struct imx21 *imx21) { }
static inline void de_urb_submitted(struct imx21 *imx21, struct urb *urb) {}
static inline void de_urb_completed(struct imx21 *imx21, struct urb *urb,
	int status) {}
static inline void de_urb_unlinked(struct imx21 *imx21, struct urb *urb) {}
static inline void de_urb_queued_for_etd(struct imx21 *imx21,
	struct urb *urb) {}
static inline void de_urb_queued_for_dmem(struct imx21 *imx21,
	struct urb *urb) {}
static inline void de_etd_allocated(struct imx21 *imx21) {}
static inline void de_etd_freed(struct imx21 *imx21) {}
static inline void de_dmem_allocated(struct imx21 *imx21, int size) {}
static inline void de_dmem_freed(struct imx21 *imx21, int size) {}
static inline void de_isoc_submitted(struct imx21 *imx21,
	int frame, struct td *td) {}
static inline void de_isoc_completed(struct imx21 *imx21,
	int frame, struct td *td, int cc, int len) {}

#else

#include <linux/defs.h>
#include <linux/seq_file.h>

static const char *dir_labels[] = {
	"TD 0",
	"OUT",
	"IN",
	"TD 1"
};

static const char *speed_labels[] = {
	"Full",
	"Low"
};

static const char *format_labels[] = {
	"Control",
	"ISO",
	"Bulk",
	"Interrupt"
};

static inline struct de_stats *stats_for_urb(struct imx21 *imx21,
	struct urb *urb)
{
	return usb_pipeisoc(urb->pipe) ?
		&imx21->isoc_stats : &imx21->nonisoc_stats;
}

static void de_urb_submitted(struct imx21 *imx21, struct urb *urb)
{
	stats_for_urb(imx21, urb)->submitted++;
}

static void de_urb_completed(struct imx21 *imx21, struct urb *urb, int st)
{
	if (st)
		stats_for_urb(imx21, urb)->completed_failed++;
	else
		stats_for_urb(imx21, urb)->completed_ok++;
}

static void de_urb_unlinked(struct imx21 *imx21, struct urb *urb)
{
	stats_for_urb(imx21, urb)->unlinked++;
}

static void de_urb_queued_for_etd(struct imx21 *imx21, struct urb *urb)
{
	stats_for_urb(imx21, urb)->queue_etd++;
}

static void de_urb_queued_for_dmem(struct imx21 *imx21, struct urb *urb)
{
	stats_for_urb(imx21, urb)->queue_dmem++;
}

static inline void de_etd_allocated(struct imx21 *imx21)
{
	imx21->etd_usage.maximum = max(
			++(imx21->etd_usage.value),
			imx21->etd_usage.maximum);
}

static inline void de_etd_freed(struct imx21 *imx21)
{
	imx21->etd_usage.value--;
}

static inline void de_dmem_allocated(struct imx21 *imx21, int size)
{
	imx21->dmem_usage.value += size;
	imx21->dmem_usage.maximum = max(
			imx21->dmem_usage.value,
			imx21->dmem_usage.maximum);
}

static inline void de_dmem_freed(struct imx21 *imx21, int size)
{
	imx21->dmem_usage.value -= size;
}


static void de_isoc_submitted(struct imx21 *imx21,
	int frame, struct td *td)
{
	struct de_isoc_trace *trace = &imx21->isoc_trace[
		imx21->isoc_trace_index++];

	imx21->isoc_trace_index %= ARRAY_SIZE(imx21->isoc_trace);
	trace->schedule_frame = td->frame;
	trace->submit_frame = frame;
	trace->request_len = td->len;
	trace->td = td;
}

static inline void de_isoc_completed(struct imx21 *imx21,
	int frame, struct td *td, int cc, int len)
{
	struct de_isoc_trace *trace, *trace_failed;
	int i;
	int found = 0;

	trace = imx21->isoc_trace;
	for (i = 0; i < ARRAY_SIZE(imx21->isoc_trace); i++, trace++) {
		if (trace->td == td) {
			trace->done_frame = frame;
			trace->done_len = len;
			trace->cc = cc;
			trace->td = NULL;
			found = 1;
			break;
		}
	}

	if (found && cc) {
		trace_failed = &imx21->isoc_trace_failed[
					imx21->isoc_trace_index_failed++];

		imx21->isoc_trace_index_failed %= ARRAY_SIZE(
						imx21->isoc_trace_failed);
		*trace_failed = *trace;
	}
}


static char *format_ep(struct usb_host_endpoint *ep, char *buf, int bufsize)
{
	if (ep)
		snprintf(buf, bufsize, "ep_%02x (type:%02X kaddr:%p)",
			ep->desc.bEndpointAddress,
			usb_endpoint_type(&ep->desc),
			ep);
	else
		snprintf(buf, bufsize, "none");
	return buf;
}

static char *format_etd_dword0(u32 value, char *buf, int bufsize)
{
	snprintf(buf, bufsize,
		"addr=%d ep=%d dir=%s speed=%s format=%s halted=%d",
		value & 0x7F,
		(value >> DW0_ENDPNT) & 0x0F,
		dir_labels[(value >> DW0_DIRECT) & 0x03],
		speed_labels[(value >> DW0_SPEED) & 0x01],
		format_labels[(value >> DW0_FORMAT) & 0x03],
		(value >> DW0_HALTED) & 0x01);
	return buf;
}

static int de_status_show(struct seq_file *s, void *v)
{
	struct imx21 *imx21 = s->private;
	int etds_allocated = 0;
	int etds_sw_busy = 0;
	int etds_hw_busy = 0;
	int dmem_blocks = 0;
	int queued_for_etd = 0;
	int queued_for_dmem = 0;
	unsigned int dmem_bytes = 0;
	int i;
	struct etd_priv *etd;
	u32 etd_enable_mask;
	unsigned long flags;
	struct imx21_dmem_area *dmem;
	struct ep_priv *ep_priv;

	spin_lock_irqsave(&imx21->lock, flags);

	etd_enable_mask = readl(imx21->regs + USBH_ETDENSET);
	for (i = 0, etd = imx21->etd; i < USB_NUM_ETD; i++, etd++) {
		if (etd->alloc)
			etds_allocated++;
		if (etd->urb)
			etds_sw_busy++;
		if (etd_enable_mask & (1<<i))
			etds_hw_busy++;
	}

	list_for_each_entry(dmem, &imx21->dmem_list, list) {
		dmem_bytes += dmem->size;
		dmem_blocks++;
	}

	list_for_each_entry(ep_priv, &imx21->queue_for_etd, queue)
		queued_for_etd++;

	list_for_each_entry(etd, &imx21->queue_for_dmem, queue)
		queued_for_dmem++;

	spin_unlock_irqrestore(&imx21->lock, flags);

	seq_printf(s,
		"Frame: %d\n"
		"ETDs allocated: %d/%d (max=%d)\n"
		"ETDs in use sw: %d\n"
		"ETDs in use hw: %d\n"
		"DMEM allocated: %d/%d (max=%d)\n"
		"DMEM blocks: %d\n"
		"Queued waiting for ETD: %d\n"
		"Queued waiting for DMEM: %d\n",
		readl(imx21->regs + USBH_FRMNUB) & 0xFFFF,
		etds_allocated, USB_NUM_ETD, imx21->etd_usage.maximum,
		etds_sw_busy,
		etds_hw_busy,
		dmem_bytes, DMEM_SIZE, imx21->dmem_usage.maximum,
		dmem_blocks,
		queued_for_etd,
		queued_for_dmem);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(de_status);

static int de_dmem_show(struct seq_file *s, void *v)
{
	struct imx21 *imx21 = s->private;
	struct imx21_dmem_area *dmem;
	unsigned long flags;
	char ep_text[40];

	spin_lock_irqsave(&imx21->lock, flags);

	list_for_each_entry(dmem, &imx21->dmem_list, list)
		seq_printf(s,
			"%04X: size=0x%X "
			"ep=%s\n",
			dmem->offset, dmem->size,
			format_ep(dmem->ep, ep_text, sizeof(ep_text)));

	spin_unlock_irqrestore(&imx21->lock, flags);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(de_dmem);

static int de_etd_show(struct seq_file *s, void *v)
{
	struct imx21 *imx21 = s->private;
	struct etd_priv *etd;
	char buf[60];
	u32 dword;
	int i, j;
	unsigned long flags;

	spin_lock_irqsave(&imx21->lock, flags);

	for (i = 0, etd = imx21->etd; i < USB_NUM_ETD; i++, etd++) {
		int state = -1;
		struct urb_priv *urb_priv;
		if (etd->urb) {
			urb_priv = etd->urb->hcpriv;
			if (urb_priv)
				state = urb_priv->state;
		}

		seq_printf(s,
			"etd_num: %d\n"
			"ep: %s\n"
			"alloc: %d\n"
			"len: %d\n"
			"busy sw: %d\n"
			"busy hw: %d\n"
			"urb state: %d\n"
			"current urb: %p\n",

			i,
			format_ep(etd->ep, buf, sizeof(buf)),
			etd->alloc,
			etd->len,
			etd->urb != NULL,
			(readl(imx21->regs + USBH_ETDENSET) & (1 << i)) > 0,
			state,
			etd->urb);

		for (j = 0; j < 4; j++) {
			dword = etd_readl(imx21, i, j);
			switch (j) {
			case 0:
				format_etd_dword0(dword, buf, sizeof(buf));
				break;
			case 2:
				snprintf(buf, sizeof(buf),
					"cc=0X%02X", dword >> DW2_COMPCODE);
				break;
			default:
				*buf = 0;
				break;
			}
			seq_printf(s,
				"dword %d: submitted=%08X cur=%08X [%s]\n",
				j,
				etd->submitted_dwords[j],
				dword,
				buf);
		}
		seq_printf(s, "\n");
	}

	spin_unlock_irqrestore(&imx21->lock, flags);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(de_etd);

static void de_statistics_show_one(struct seq_file *s,
	const char *name, struct de_stats *stats)
{
	seq_printf(s, "%s:\n"
		"submitted URBs: %lu\n"
		"completed OK: %lu\n"
		"completed failed: %lu\n"
		"unlinked: %lu\n"
		"queued for ETD: %lu\n"
		"queued for DMEM: %lu\n\n",
		name,
		stats->submitted,
		stats->completed_ok,
		stats->completed_failed,
		stats->unlinked,
		stats->queue_etd,
		stats->queue_dmem);
}

static int de_statistics_show(struct seq_file *s, void *v)
{
	struct imx21 *imx21 = s->private;
	unsigned long flags;

	spin_lock_irqsave(&imx21->lock, flags);

	de_statistics_show_one(s, "nonisoc", &imx21->nonisoc_stats);
	de_statistics_show_one(s, "isoc", &imx21->isoc_stats);
	seq_printf(s, "unblock kludge triggers: %lu\n", imx21->de_unblocks);
	spin_unlock_irqrestore(&imx21->lock, flags);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(de_statistics);

static void de_isoc_show_one(struct seq_file *s,
	const char *name, int index, 	struct de_isoc_trace *trace)
{
	seq_printf(s, "%s %d:\n"
		"cc=0X%02X\n"
		"scheduled frame %d (%d)\n"
		"submitted frame %d (%d)\n"
		"completed frame %d (%d)\n"
		"requested length=%d\n"
		"completed length=%d\n\n",
		name, index,
		trace->cc,
		trace->schedule_frame, trace->schedule_frame & 0xFFFF,
		trace->submit_frame, trace->submit_frame & 0xFFFF,
		trace->done_frame, trace->done_frame & 0xFFFF,
		trace->request_len,
		trace->done_len);
}

static int de_isoc_show(struct seq_file *s, void *v)
{
	struct imx21 *imx21 = s->private;
	struct de_isoc_trace *trace;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&imx21->lock, flags);

	trace = imx21->isoc_trace_failed;
	for (i = 0; i < ARRAY_SIZE(imx21->isoc_trace_failed); i++, trace++)
		de_isoc_show_one(s, "isoc failed", i, trace);

	trace = imx21->isoc_trace;
	for (i = 0; i < ARRAY_SIZE(imx21->isoc_trace); i++, trace++)
		de_isoc_show_one(s, "isoc", i, trace);

	spin_unlock_irqrestore(&imx21->lock, flags);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(de_isoc);

static void create_de_files(struct imx21 *imx21)
{
	struct dentry *root;

	root = defs_create_dir(dev_name(imx21->dev), NULL);
	imx21->de_root = root;

	defs_create_file("status", S_IRUGO, root, imx21, &de_status_fops);
	defs_create_file("dmem", S_IRUGO, root, imx21, &de_dmem_fops);
	defs_create_file("etd", S_IRUGO, root, imx21, &de_etd_fops);
	defs_create_file("statistics", S_IRUGO, root, imx21,
			    &de_statistics_fops);
	defs_create_file("isoc", S_IRUGO, root, imx21, &de_isoc_fops);
}

static void remove_de_files(struct imx21 *imx21)
{
	defs_remove_recursive(imx21->de_root);
}

#endif


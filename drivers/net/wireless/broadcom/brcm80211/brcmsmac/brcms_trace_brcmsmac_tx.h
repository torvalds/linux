/*
 * Copyright (c) 2011 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if !defined(__TRACE_BRCMSMAC_TX_H) || defined(TRACE_HEADER_MULTI_READ)
#define __TRACE_BRCMSMAC_TX_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM brcmsmac_tx

TRACE_EVENT(brcms_txdesc,
	TP_PROTO(const struct device *dev,
		 void *txh, size_t txh_len),
	TP_ARGS(dev, txh, txh_len),
	TP_STRUCT__entry(
		__string(dev, dev_name(dev))
		__dynamic_array(u8, txh, txh_len)
	),
	TP_fast_assign(
		__assign_str(dev, dev_name(dev));
		memcpy(__get_dynamic_array(txh), txh, txh_len);
	),
	TP_printk("[%s] txdesc", __get_str(dev))
);

TRACE_EVENT(brcms_txstatus,
	TP_PROTO(const struct device *dev, u16 framelen, u16 frameid,
		 u16 status, u16 lasttxtime, u16 sequence, u16 phyerr,
		 u16 ackphyrxsh),
	TP_ARGS(dev, framelen, frameid, status, lasttxtime, sequence, phyerr,
		ackphyrxsh),
	TP_STRUCT__entry(
		__string(dev, dev_name(dev))
		__field(u16, framelen)
		__field(u16, frameid)
		__field(u16, status)
		__field(u16, lasttxtime)
		__field(u16, sequence)
		__field(u16, phyerr)
		__field(u16, ackphyrxsh)
	),
	TP_fast_assign(
		__assign_str(dev, dev_name(dev));
		__entry->framelen = framelen;
		__entry->frameid = frameid;
		__entry->status = status;
		__entry->lasttxtime = lasttxtime;
		__entry->sequence = sequence;
		__entry->phyerr = phyerr;
		__entry->ackphyrxsh = ackphyrxsh;
	),
	TP_printk("[%s] FrameId %#04x TxStatus %#04x LastTxTime %#04x "
		  "Seq %#04x PHYTxStatus %#04x RxAck %#04x",
		  __get_str(dev), __entry->frameid, __entry->status,
		  __entry->lasttxtime, __entry->sequence, __entry->phyerr,
		  __entry->ackphyrxsh)
);

TRACE_EVENT(brcms_ampdu_session,
	TP_PROTO(const struct device *dev, unsigned max_ampdu_len,
		 u16 max_ampdu_frames, u16 ampdu_len, u16 ampdu_frames,
		 u16 dma_len),
	TP_ARGS(dev, max_ampdu_len, max_ampdu_frames, ampdu_len, ampdu_frames,
		dma_len),
	TP_STRUCT__entry(
		__string(dev, dev_name(dev))
		__field(unsigned, max_ampdu_len)
		__field(u16, max_ampdu_frames)
		__field(u16, ampdu_len)
		__field(u16, ampdu_frames)
		__field(u16, dma_len)
	),
	TP_fast_assign(
		__assign_str(dev, dev_name(dev));
		__entry->max_ampdu_len = max_ampdu_len;
		__entry->max_ampdu_frames = max_ampdu_frames;
		__entry->ampdu_len = ampdu_len;
		__entry->ampdu_frames = ampdu_frames;
		__entry->dma_len = dma_len;
	),
	TP_printk("[%s] ampdu session max_len=%u max_frames=%u len=%u frames=%u dma_len=%u",
		  __get_str(dev), __entry->max_ampdu_len,
		  __entry->max_ampdu_frames, __entry->ampdu_len,
		  __entry->ampdu_frames, __entry->dma_len)
);
#endif /* __TRACE_BRCMSMAC_TX_H */

#ifdef CONFIG_BRCM_TRACING

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE brcms_trace_brcmsmac_tx
#include <trace/define_trace.h>

#endif /* CONFIG_BRCM_TRACING */

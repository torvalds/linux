/*
 * dvb_demux.c - DVB kernel demux API
 *
 * Copyright (C) 2000-2001 Ralph  Metzler <ralph@convergence.de>
 *		       & Marcus Metzler <marcus@convergence.de>
 *			 for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) "dvb_demux: " fmt

#include <linux/sched/signal.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/crc32.h>
#include <linux/uaccess.h>
#include <asm/div64.h>

#include <media/dvb_demux.h>

static int dvb_demux_tscheck;
module_param(dvb_demux_tscheck, int, 0644);
MODULE_PARM_DESC(dvb_demux_tscheck,
		"enable transport stream continuity and TEI check");

static int dvb_demux_speedcheck;
module_param(dvb_demux_speedcheck, int, 0644);
MODULE_PARM_DESC(dvb_demux_speedcheck,
		"enable transport stream speed check");

static int dvb_demux_feed_err_pkts = 1;
module_param(dvb_demux_feed_err_pkts, int, 0644);
MODULE_PARM_DESC(dvb_demux_feed_err_pkts,
		 "when set to 0, drop packets with the TEI bit set (1 by default)");

#define dprintk(fmt, arg...) \
	printk(KERN_DEBUG pr_fmt("%s: " fmt),  __func__, ##arg)

#define dprintk_tscheck(x...) do {			\
	if (dvb_demux_tscheck && printk_ratelimit())	\
		dprintk(x);				\
} while (0)

/******************************************************************************
 * static inlined helper functions
 ******************************************************************************/

static inline u16 section_length(const u8 *buf)
{
	return 3 + ((buf[1] & 0x0f) << 8) + buf[2];
}

static inline u16 ts_pid(const u8 *buf)
{
	return ((buf[1] & 0x1f) << 8) + buf[2];
}

static inline u8 payload(const u8 *tsp)
{
	if (!(tsp[3] & 0x10))	// no payload?
		return 0;

	if (tsp[3] & 0x20) {	// adaptation field?
		if (tsp[4] > 183)	// corrupted data?
			return 0;
		else
			return 184 - 1 - tsp[4];
	}

	return 184;
}

static u32 dvb_dmx_crc32(struct dvb_demux_feed *f, const u8 *src, size_t len)
{
	return (f->feed.sec.crc_val = crc32_be(f->feed.sec.crc_val, src, len));
}

static void dvb_dmx_memcopy(struct dvb_demux_feed *f, u8 *d, const u8 *s,
			    size_t len)
{
	memcpy(d, s, len);
}

/******************************************************************************
 * Software filter functions
 ******************************************************************************/

static inline int dvb_dmx_swfilter_payload(struct dvb_demux_feed *feed,
					   const u8 *buf)
{
	int count = payload(buf);
	int p;
#ifdef CONFIG_DVB_DEMUX_SECTION_LOSS_LOG
	int ccok;
	u8 cc;
#endif

	if (count == 0)
		return -1;

	p = 188 - count;

#ifdef CONFIG_DVB_DEMUX_SECTION_LOSS_LOG
	cc = buf[3] & 0x0f;
	ccok = ((feed->cc + 1) & 0x0f) == cc;
	feed->cc = cc;
	if (!ccok)
		dprintk("missed packet: %d instead of %d!\n",
			cc, (feed->cc + 1) & 0x0f);
#endif

	if (buf[1] & 0x40)	// PUSI ?
		feed->peslen = 0xfffa;

	feed->peslen += count;

	return feed->cb.ts(&buf[p], count, NULL, 0, &feed->feed.ts);
}

static int dvb_dmx_swfilter_sectionfilter(struct dvb_demux_feed *feed,
					  struct dvb_demux_filter *f)
{
	u8 neq = 0;
	int i;

	for (i = 0; i < DVB_DEMUX_MASK_MAX; i++) {
		u8 xor = f->filter.filter_value[i] ^ feed->feed.sec.secbuf[i];

		if (f->maskandmode[i] & xor)
			return 0;

		neq |= f->maskandnotmode[i] & xor;
	}

	if (f->doneq && !neq)
		return 0;

	return feed->cb.sec(feed->feed.sec.secbuf, feed->feed.sec.seclen,
			    NULL, 0, &f->filter);
}

static inline int dvb_dmx_swfilter_section_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct dvb_demux_filter *f = feed->filter;
	struct dmx_section_feed *sec = &feed->feed.sec;
	int section_syntax_indicator;

	if (!sec->is_filtering)
		return 0;

	if (!f)
		return 0;

	if (sec->check_crc) {
		section_syntax_indicator = ((sec->secbuf[1] & 0x80) != 0);
		if (section_syntax_indicator &&
		    demux->check_crc32(feed, sec->secbuf, sec->seclen))
			return -1;
	}

	do {
		if (dvb_dmx_swfilter_sectionfilter(feed, f) < 0)
			return -1;
	} while ((f = f->next) && sec->is_filtering);

	sec->seclen = 0;

	return 0;
}

static void dvb_dmx_swfilter_section_new(struct dvb_demux_feed *feed)
{
	struct dmx_section_feed *sec = &feed->feed.sec;

#ifdef CONFIG_DVB_DEMUX_SECTION_LOSS_LOG
	if (sec->secbufp < sec->tsfeedp) {
		int n = sec->tsfeedp - sec->secbufp;

		/*
		 * Section padding is done with 0xff bytes entirely.
		 * Due to speed reasons, we won't check all of them
		 * but just first and last.
		 */
		if (sec->secbuf[0] != 0xff || sec->secbuf[n - 1] != 0xff) {
			dprintk("section ts padding loss: %d/%d\n",
			       n, sec->tsfeedp);
			dprintk("pad data: %*ph\n", n, sec->secbuf);
		}
	}
#endif

	sec->tsfeedp = sec->secbufp = sec->seclen = 0;
	sec->secbuf = sec->secbuf_base;
}

/*
 * Losless Section Demux 1.4.1 by Emard
 * Valsecchi Patrick:
 *  - middle of section A  (no PUSI)
 *  - end of section A and start of section B
 *    (with PUSI pointing to the start of the second section)
 *
 *  In this case, without feed->pusi_seen you'll receive a garbage section
 *  consisting of the end of section A. Basically because tsfeedp
 *  is incemented and the use=0 condition is not raised
 *  when the second packet arrives.
 *
 * Fix:
 * when demux is started, let feed->pusi_seen = false to
 * prevent initial feeding of garbage from the end of
 * previous section. When you for the first time see PUSI=1
 * then set feed->pusi_seen = true
 */
static int dvb_dmx_swfilter_section_copy_dump(struct dvb_demux_feed *feed,
					      const u8 *buf, u8 len)
{
	struct dvb_demux *demux = feed->demux;
	struct dmx_section_feed *sec = &feed->feed.sec;
	u16 limit, seclen, n;

	if (sec->tsfeedp >= DMX_MAX_SECFEED_SIZE)
		return 0;

	if (sec->tsfeedp + len > DMX_MAX_SECFEED_SIZE) {
#ifdef CONFIG_DVB_DEMUX_SECTION_LOSS_LOG
		dprintk("section buffer full loss: %d/%d\n",
			sec->tsfeedp + len - DMX_MAX_SECFEED_SIZE,
			DMX_MAX_SECFEED_SIZE);
#endif
		len = DMX_MAX_SECFEED_SIZE - sec->tsfeedp;
	}

	if (len <= 0)
		return 0;

	demux->memcopy(feed, sec->secbuf_base + sec->tsfeedp, buf, len);
	sec->tsfeedp += len;

	/*
	 * Dump all the sections we can find in the data (Emard)
	 */
	limit = sec->tsfeedp;
	if (limit > DMX_MAX_SECFEED_SIZE)
		return -1;	/* internal error should never happen */

	/* to be sure always set secbuf */
	sec->secbuf = sec->secbuf_base + sec->secbufp;

	for (n = 0; sec->secbufp + 2 < limit; n++) {
		seclen = section_length(sec->secbuf);
		if (seclen <= 0 || seclen > DMX_MAX_SECTION_SIZE
		    || seclen + sec->secbufp > limit)
			return 0;
		sec->seclen = seclen;
		sec->crc_val = ~0;
		/* dump [secbuf .. secbuf+seclen) */
		if (feed->pusi_seen)
			dvb_dmx_swfilter_section_feed(feed);
#ifdef CONFIG_DVB_DEMUX_SECTION_LOSS_LOG
		else
			dprintk("pusi not seen, discarding section data\n");
#endif
		sec->secbufp += seclen;	/* secbufp and secbuf moving together is */
		sec->secbuf += seclen;	/* redundant but saves pointer arithmetic */
	}

	return 0;
}

static int dvb_dmx_swfilter_section_packet(struct dvb_demux_feed *feed,
					   const u8 *buf)
{
	u8 p, count;
	int ccok, dc_i = 0;
	u8 cc;

	count = payload(buf);

	if (count == 0)		/* count == 0 if no payload or out of range */
		return -1;

	p = 188 - count;	/* payload start */

	cc = buf[3] & 0x0f;
	ccok = ((feed->cc + 1) & 0x0f) == cc;
	feed->cc = cc;

	if (buf[3] & 0x20) {
		/* adaption field present, check for discontinuity_indicator */
		if ((buf[4] > 0) && (buf[5] & 0x80))
			dc_i = 1;
	}

	if (!ccok || dc_i) {
#ifdef CONFIG_DVB_DEMUX_SECTION_LOSS_LOG
		if (dc_i)
			dprintk("%d frame with disconnect indicator\n",
				cc);
		else
			dprintk("discontinuity: %d instead of %d. %d bytes lost\n",
				cc, (feed->cc + 1) & 0x0f, count + 4);
		/*
		 * those bytes under sume circumstances will again be reported
		 * in the following dvb_dmx_swfilter_section_new
		 */
#endif
		/*
		 * Discontinuity detected. Reset pusi_seen to
		 * stop feeding of suspicious data until next PUSI=1 arrives
		 *
		 * FIXME: does it make sense if the MPEG-TS is the one
		 *	reporting discontinuity?
		 */
		feed->pusi_seen = false;
		dvb_dmx_swfilter_section_new(feed);
	}

	if (buf[1] & 0x40) {
		/* PUSI=1 (is set), section boundary is here */
		if (count > 1 && buf[p] < count) {
			const u8 *before = &buf[p + 1];
			u8 before_len = buf[p];
			const u8 *after = &before[before_len];
			u8 after_len = count - 1 - before_len;

			dvb_dmx_swfilter_section_copy_dump(feed, before,
							   before_len);
			/* before start of new section, set pusi_seen */
			feed->pusi_seen = true;
			dvb_dmx_swfilter_section_new(feed);
			dvb_dmx_swfilter_section_copy_dump(feed, after,
							   after_len);
		}
#ifdef CONFIG_DVB_DEMUX_SECTION_LOSS_LOG
		else if (count > 0)
			dprintk("PUSI=1 but %d bytes lost\n", count);
#endif
	} else {
		/* PUSI=0 (is not set), no section boundary */
		dvb_dmx_swfilter_section_copy_dump(feed, &buf[p], count);
	}

	return 0;
}

static inline void dvb_dmx_swfilter_packet_type(struct dvb_demux_feed *feed,
						const u8 *buf)
{
	switch (feed->type) {
	case DMX_TYPE_TS:
		if (!feed->feed.ts.is_filtering)
			break;
		if (feed->ts_type & TS_PACKET) {
			if (feed->ts_type & TS_PAYLOAD_ONLY)
				dvb_dmx_swfilter_payload(feed, buf);
			else
				feed->cb.ts(buf, 188, NULL, 0, &feed->feed.ts);
		}
		/* Used only on full-featured devices */
		if (feed->ts_type & TS_DECODER)
			if (feed->demux->write_to_decoder)
				feed->demux->write_to_decoder(feed, buf, 188);
		break;

	case DMX_TYPE_SEC:
		if (!feed->feed.sec.is_filtering)
			break;
		if (dvb_dmx_swfilter_section_packet(feed, buf) < 0)
			feed->feed.sec.seclen = feed->feed.sec.secbufp = 0;
		break;

	default:
		break;
	}
}

#define DVR_FEED(f)							\
	(((f)->type == DMX_TYPE_TS) &&					\
	((f)->feed.ts.is_filtering) &&					\
	(((f)->ts_type & (TS_PACKET | TS_DEMUX)) == TS_PACKET))

static void dvb_dmx_swfilter_packet(struct dvb_demux *demux, const u8 *buf)
{
	struct dvb_demux_feed *feed;
	u16 pid = ts_pid(buf);
	int dvr_done = 0;

	if (dvb_demux_speedcheck) {
		ktime_t cur_time;
		u64 speed_bytes, speed_timedelta;

		demux->speed_pkts_cnt++;

		/* show speed every SPEED_PKTS_INTERVAL packets */
		if (!(demux->speed_pkts_cnt % SPEED_PKTS_INTERVAL)) {
			cur_time = ktime_get();

			if (ktime_to_ns(demux->speed_last_time) != 0) {
				speed_bytes = (u64)demux->speed_pkts_cnt
					* 188 * 8;
				/* convert to 1024 basis */
				speed_bytes = 1000 * div64_u64(speed_bytes,
						1024);
				speed_timedelta = ktime_ms_delta(cur_time,
							demux->speed_last_time);
				if (speed_timedelta)
					dprintk("TS speed %llu Kbits/sec \n",
						div64_u64(speed_bytes,
							  speed_timedelta));
			}

			demux->speed_last_time = cur_time;
			demux->speed_pkts_cnt = 0;
		}
	}

	if (buf[1] & 0x80) {
		dprintk_tscheck("TEI detected. PID=0x%x data1=0x%x\n",
				pid, buf[1]);
		/* data in this packet can't be trusted - drop it unless
		 * module option dvb_demux_feed_err_pkts is set */
		if (!dvb_demux_feed_err_pkts)
			return;
	} else /* if TEI bit is set, pid may be wrong- skip pkt counter */
		if (demux->cnt_storage && dvb_demux_tscheck) {
			/* check pkt counter */
			if (pid < MAX_PID) {
				if (buf[3] & 0x10)
					demux->cnt_storage[pid] =
						(demux->cnt_storage[pid] + 1) & 0xf;

				if ((buf[3] & 0xf) != demux->cnt_storage[pid]) {
					dprintk_tscheck("TS packet counter mismatch. PID=0x%x expected 0x%x got 0x%x\n",
							pid, demux->cnt_storage[pid],
							buf[3] & 0xf);
					demux->cnt_storage[pid] = buf[3] & 0xf;
				}
			}
			/* end check */
		}

	list_for_each_entry(feed, &demux->feed_list, list_head) {
		if ((feed->pid != pid) && (feed->pid != 0x2000))
			continue;

		/* copy each packet only once to the dvr device, even
		 * if a PID is in multiple filters (e.g. video + PCR) */
		if ((DVR_FEED(feed)) && (dvr_done++))
			continue;

		if (feed->pid == pid)
			dvb_dmx_swfilter_packet_type(feed, buf);
		else if (feed->pid == 0x2000)
			feed->cb.ts(buf, 188, NULL, 0, &feed->feed.ts);
	}
}

void dvb_dmx_swfilter_packets(struct dvb_demux *demux, const u8 *buf,
			      size_t count)
{
	unsigned long flags;

	spin_lock_irqsave(&demux->lock, flags);

	while (count--) {
		if (buf[0] == 0x47)
			dvb_dmx_swfilter_packet(demux, buf);
		buf += 188;
	}

	spin_unlock_irqrestore(&demux->lock, flags);
}

EXPORT_SYMBOL(dvb_dmx_swfilter_packets);

static inline int find_next_packet(const u8 *buf, int pos, size_t count,
				   const int pktsize)
{
	int start = pos, lost;

	while (pos < count) {
		if (buf[pos] == 0x47 ||
		    (pktsize == 204 && buf[pos] == 0xB8))
			break;
		pos++;
	}

	lost = pos - start;
	if (lost) {
		/* This garbage is part of a valid packet? */
		int backtrack = pos - pktsize;
		if (backtrack >= 0 && (buf[backtrack] == 0x47 ||
		    (pktsize == 204 && buf[backtrack] == 0xB8)))
			return backtrack;
	}

	return pos;
}

/* Filter all pktsize= 188 or 204 sized packets and skip garbage. */
static inline void _dvb_dmx_swfilter(struct dvb_demux *demux, const u8 *buf,
		size_t count, const int pktsize)
{
	int p = 0, i, j;
	const u8 *q;
	unsigned long flags;

	spin_lock_irqsave(&demux->lock, flags);

	if (demux->tsbufp) { /* tsbuf[0] is now 0x47. */
		i = demux->tsbufp;
		j = pktsize - i;
		if (count < j) {
			memcpy(&demux->tsbuf[i], buf, count);
			demux->tsbufp += count;
			goto bailout;
		}
		memcpy(&demux->tsbuf[i], buf, j);
		if (demux->tsbuf[0] == 0x47) /* double check */
			dvb_dmx_swfilter_packet(demux, demux->tsbuf);
		demux->tsbufp = 0;
		p += j;
	}

	while (1) {
		p = find_next_packet(buf, p, count, pktsize);
		if (p >= count)
			break;
		if (count - p < pktsize)
			break;

		q = &buf[p];

		if (pktsize == 204 && (*q == 0xB8)) {
			memcpy(demux->tsbuf, q, 188);
			demux->tsbuf[0] = 0x47;
			q = demux->tsbuf;
		}
		dvb_dmx_swfilter_packet(demux, q);
		p += pktsize;
	}

	i = count - p;
	if (i) {
		memcpy(demux->tsbuf, &buf[p], i);
		demux->tsbufp = i;
		if (pktsize == 204 && demux->tsbuf[0] == 0xB8)
			demux->tsbuf[0] = 0x47;
	}

bailout:
	spin_unlock_irqrestore(&demux->lock, flags);
}

void dvb_dmx_swfilter(struct dvb_demux *demux, const u8 *buf, size_t count)
{
	_dvb_dmx_swfilter(demux, buf, count, 188);
}
EXPORT_SYMBOL(dvb_dmx_swfilter);

void dvb_dmx_swfilter_204(struct dvb_demux *demux, const u8 *buf, size_t count)
{
	_dvb_dmx_swfilter(demux, buf, count, 204);
}
EXPORT_SYMBOL(dvb_dmx_swfilter_204);

void dvb_dmx_swfilter_raw(struct dvb_demux *demux, const u8 *buf, size_t count)
{
	unsigned long flags;

	spin_lock_irqsave(&demux->lock, flags);

	demux->feed->cb.ts(buf, count, NULL, 0, &demux->feed->feed.ts);

	spin_unlock_irqrestore(&demux->lock, flags);
}
EXPORT_SYMBOL(dvb_dmx_swfilter_raw);

static struct dvb_demux_filter *dvb_dmx_filter_alloc(struct dvb_demux *demux)
{
	int i;

	for (i = 0; i < demux->filternum; i++)
		if (demux->filter[i].state == DMX_STATE_FREE)
			break;

	if (i == demux->filternum)
		return NULL;

	demux->filter[i].state = DMX_STATE_ALLOCATED;

	return &demux->filter[i];
}

static struct dvb_demux_feed *dvb_dmx_feed_alloc(struct dvb_demux *demux)
{
	int i;

	for (i = 0; i < demux->feednum; i++)
		if (demux->feed[i].state == DMX_STATE_FREE)
			break;

	if (i == demux->feednum)
		return NULL;

	demux->feed[i].state = DMX_STATE_ALLOCATED;

	return &demux->feed[i];
}

static int dvb_demux_feed_find(struct dvb_demux_feed *feed)
{
	struct dvb_demux_feed *entry;

	list_for_each_entry(entry, &feed->demux->feed_list, list_head)
		if (entry == feed)
			return 1;

	return 0;
}

static void dvb_demux_feed_add(struct dvb_demux_feed *feed)
{
	spin_lock_irq(&feed->demux->lock);
	if (dvb_demux_feed_find(feed)) {
		pr_err("%s: feed already in list (type=%x state=%x pid=%x)\n",
		       __func__, feed->type, feed->state, feed->pid);
		goto out;
	}

	list_add(&feed->list_head, &feed->demux->feed_list);
out:
	spin_unlock_irq(&feed->demux->lock);
}

static void dvb_demux_feed_del(struct dvb_demux_feed *feed)
{
	spin_lock_irq(&feed->demux->lock);
	if (!(dvb_demux_feed_find(feed))) {
		pr_err("%s: feed not in list (type=%x state=%x pid=%x)\n",
		       __func__, feed->type, feed->state, feed->pid);
		goto out;
	}

	list_del(&feed->list_head);
out:
	spin_unlock_irq(&feed->demux->lock);
}

static int dmx_ts_feed_set(struct dmx_ts_feed *ts_feed, u16 pid, int ts_type,
			   enum dmx_ts_pes pes_type, ktime_t timeout)
{
	struct dvb_demux_feed *feed = (struct dvb_demux_feed *)ts_feed;
	struct dvb_demux *demux = feed->demux;

	if (pid > DMX_MAX_PID)
		return -EINVAL;

	if (mutex_lock_interruptible(&demux->mutex))
		return -ERESTARTSYS;

	if (ts_type & TS_DECODER) {
		if (pes_type >= DMX_PES_OTHER) {
			mutex_unlock(&demux->mutex);
			return -EINVAL;
		}

		if (demux->pesfilter[pes_type] &&
		    demux->pesfilter[pes_type] != feed) {
			mutex_unlock(&demux->mutex);
			return -EINVAL;
		}

		demux->pesfilter[pes_type] = feed;
		demux->pids[pes_type] = pid;
	}

	dvb_demux_feed_add(feed);

	feed->pid = pid;
	feed->timeout = timeout;
	feed->ts_type = ts_type;
	feed->pes_type = pes_type;

	feed->state = DMX_STATE_READY;
	mutex_unlock(&demux->mutex);

	return 0;
}

static int dmx_ts_feed_start_filtering(struct dmx_ts_feed *ts_feed)
{
	struct dvb_demux_feed *feed = (struct dvb_demux_feed *)ts_feed;
	struct dvb_demux *demux = feed->demux;
	int ret;

	if (mutex_lock_interruptible(&demux->mutex))
		return -ERESTARTSYS;

	if (feed->state != DMX_STATE_READY || feed->type != DMX_TYPE_TS) {
		mutex_unlock(&demux->mutex);
		return -EINVAL;
	}

	if (!demux->start_feed) {
		mutex_unlock(&demux->mutex);
		return -ENODEV;
	}

	if ((ret = demux->start_feed(feed)) < 0) {
		mutex_unlock(&demux->mutex);
		return ret;
	}

	spin_lock_irq(&demux->lock);
	ts_feed->is_filtering = 1;
	feed->state = DMX_STATE_GO;
	spin_unlock_irq(&demux->lock);
	mutex_unlock(&demux->mutex);

	return 0;
}

static int dmx_ts_feed_stop_filtering(struct dmx_ts_feed *ts_feed)
{
	struct dvb_demux_feed *feed = (struct dvb_demux_feed *)ts_feed;
	struct dvb_demux *demux = feed->demux;
	int ret;

	mutex_lock(&demux->mutex);

	if (feed->state < DMX_STATE_GO) {
		mutex_unlock(&demux->mutex);
		return -EINVAL;
	}

	if (!demux->stop_feed) {
		mutex_unlock(&demux->mutex);
		return -ENODEV;
	}

	ret = demux->stop_feed(feed);

	spin_lock_irq(&demux->lock);
	ts_feed->is_filtering = 0;
	feed->state = DMX_STATE_ALLOCATED;
	spin_unlock_irq(&demux->lock);
	mutex_unlock(&demux->mutex);

	return ret;
}

static int dvbdmx_allocate_ts_feed(struct dmx_demux *dmx,
				   struct dmx_ts_feed **ts_feed,
				   dmx_ts_cb callback)
{
	struct dvb_demux *demux = (struct dvb_demux *)dmx;
	struct dvb_demux_feed *feed;

	if (mutex_lock_interruptible(&demux->mutex))
		return -ERESTARTSYS;

	if (!(feed = dvb_dmx_feed_alloc(demux))) {
		mutex_unlock(&demux->mutex);
		return -EBUSY;
	}

	feed->type = DMX_TYPE_TS;
	feed->cb.ts = callback;
	feed->demux = demux;
	feed->pid = 0xffff;
	feed->peslen = 0xfffa;

	(*ts_feed) = &feed->feed.ts;
	(*ts_feed)->parent = dmx;
	(*ts_feed)->priv = NULL;
	(*ts_feed)->is_filtering = 0;
	(*ts_feed)->start_filtering = dmx_ts_feed_start_filtering;
	(*ts_feed)->stop_filtering = dmx_ts_feed_stop_filtering;
	(*ts_feed)->set = dmx_ts_feed_set;

	if (!(feed->filter = dvb_dmx_filter_alloc(demux))) {
		feed->state = DMX_STATE_FREE;
		mutex_unlock(&demux->mutex);
		return -EBUSY;
	}

	feed->filter->type = DMX_TYPE_TS;
	feed->filter->feed = feed;
	feed->filter->state = DMX_STATE_READY;

	mutex_unlock(&demux->mutex);

	return 0;
}

static int dvbdmx_release_ts_feed(struct dmx_demux *dmx,
				  struct dmx_ts_feed *ts_feed)
{
	struct dvb_demux *demux = (struct dvb_demux *)dmx;
	struct dvb_demux_feed *feed = (struct dvb_demux_feed *)ts_feed;

	mutex_lock(&demux->mutex);

	if (feed->state == DMX_STATE_FREE) {
		mutex_unlock(&demux->mutex);
		return -EINVAL;
	}

	feed->state = DMX_STATE_FREE;
	feed->filter->state = DMX_STATE_FREE;

	dvb_demux_feed_del(feed);

	feed->pid = 0xffff;

	if (feed->ts_type & TS_DECODER && feed->pes_type < DMX_PES_OTHER)
		demux->pesfilter[feed->pes_type] = NULL;

	mutex_unlock(&demux->mutex);
	return 0;
}

/******************************************************************************
 * dmx_section_feed API calls
 ******************************************************************************/

static int dmx_section_feed_allocate_filter(struct dmx_section_feed *feed,
					    struct dmx_section_filter **filter)
{
	struct dvb_demux_feed *dvbdmxfeed = (struct dvb_demux_feed *)feed;
	struct dvb_demux *dvbdemux = dvbdmxfeed->demux;
	struct dvb_demux_filter *dvbdmxfilter;

	if (mutex_lock_interruptible(&dvbdemux->mutex))
		return -ERESTARTSYS;

	dvbdmxfilter = dvb_dmx_filter_alloc(dvbdemux);
	if (!dvbdmxfilter) {
		mutex_unlock(&dvbdemux->mutex);
		return -EBUSY;
	}

	spin_lock_irq(&dvbdemux->lock);
	*filter = &dvbdmxfilter->filter;
	(*filter)->parent = feed;
	(*filter)->priv = NULL;
	dvbdmxfilter->feed = dvbdmxfeed;
	dvbdmxfilter->type = DMX_TYPE_SEC;
	dvbdmxfilter->state = DMX_STATE_READY;
	dvbdmxfilter->next = dvbdmxfeed->filter;
	dvbdmxfeed->filter = dvbdmxfilter;
	spin_unlock_irq(&dvbdemux->lock);

	mutex_unlock(&dvbdemux->mutex);
	return 0;
}

static int dmx_section_feed_set(struct dmx_section_feed *feed,
				u16 pid, int check_crc)
{
	struct dvb_demux_feed *dvbdmxfeed = (struct dvb_demux_feed *)feed;
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;

	if (pid > 0x1fff)
		return -EINVAL;

	if (mutex_lock_interruptible(&dvbdmx->mutex))
		return -ERESTARTSYS;

	dvb_demux_feed_add(dvbdmxfeed);

	dvbdmxfeed->pid = pid;
	dvbdmxfeed->feed.sec.check_crc = check_crc;

	dvbdmxfeed->state = DMX_STATE_READY;
	mutex_unlock(&dvbdmx->mutex);
	return 0;
}

static void prepare_secfilters(struct dvb_demux_feed *dvbdmxfeed)
{
	int i;
	struct dvb_demux_filter *f;
	struct dmx_section_filter *sf;
	u8 mask, mode, doneq;

	if (!(f = dvbdmxfeed->filter))
		return;
	do {
		sf = &f->filter;
		doneq = false;
		for (i = 0; i < DVB_DEMUX_MASK_MAX; i++) {
			mode = sf->filter_mode[i];
			mask = sf->filter_mask[i];
			f->maskandmode[i] = mask & mode;
			doneq |= f->maskandnotmode[i] = mask & ~mode;
		}
		f->doneq = doneq ? true : false;
	} while ((f = f->next));
}

static int dmx_section_feed_start_filtering(struct dmx_section_feed *feed)
{
	struct dvb_demux_feed *dvbdmxfeed = (struct dvb_demux_feed *)feed;
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	int ret;

	if (mutex_lock_interruptible(&dvbdmx->mutex))
		return -ERESTARTSYS;

	if (feed->is_filtering) {
		mutex_unlock(&dvbdmx->mutex);
		return -EBUSY;
	}

	if (!dvbdmxfeed->filter) {
		mutex_unlock(&dvbdmx->mutex);
		return -EINVAL;
	}

	dvbdmxfeed->feed.sec.tsfeedp = 0;
	dvbdmxfeed->feed.sec.secbuf = dvbdmxfeed->feed.sec.secbuf_base;
	dvbdmxfeed->feed.sec.secbufp = 0;
	dvbdmxfeed->feed.sec.seclen = 0;

	if (!dvbdmx->start_feed) {
		mutex_unlock(&dvbdmx->mutex);
		return -ENODEV;
	}

	prepare_secfilters(dvbdmxfeed);

	if ((ret = dvbdmx->start_feed(dvbdmxfeed)) < 0) {
		mutex_unlock(&dvbdmx->mutex);
		return ret;
	}

	spin_lock_irq(&dvbdmx->lock);
	feed->is_filtering = 1;
	dvbdmxfeed->state = DMX_STATE_GO;
	spin_unlock_irq(&dvbdmx->lock);

	mutex_unlock(&dvbdmx->mutex);
	return 0;
}

static int dmx_section_feed_stop_filtering(struct dmx_section_feed *feed)
{
	struct dvb_demux_feed *dvbdmxfeed = (struct dvb_demux_feed *)feed;
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	int ret;

	mutex_lock(&dvbdmx->mutex);

	if (!dvbdmx->stop_feed) {
		mutex_unlock(&dvbdmx->mutex);
		return -ENODEV;
	}

	ret = dvbdmx->stop_feed(dvbdmxfeed);

	spin_lock_irq(&dvbdmx->lock);
	dvbdmxfeed->state = DMX_STATE_READY;
	feed->is_filtering = 0;
	spin_unlock_irq(&dvbdmx->lock);

	mutex_unlock(&dvbdmx->mutex);
	return ret;
}

static int dmx_section_feed_release_filter(struct dmx_section_feed *feed,
					   struct dmx_section_filter *filter)
{
	struct dvb_demux_filter *dvbdmxfilter = (struct dvb_demux_filter *)filter, *f;
	struct dvb_demux_feed *dvbdmxfeed = (struct dvb_demux_feed *)feed;
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;

	mutex_lock(&dvbdmx->mutex);

	if (dvbdmxfilter->feed != dvbdmxfeed) {
		mutex_unlock(&dvbdmx->mutex);
		return -EINVAL;
	}

	if (feed->is_filtering) {
		/* release dvbdmx->mutex as far as it is
		   acquired by stop_filtering() itself */
		mutex_unlock(&dvbdmx->mutex);
		feed->stop_filtering(feed);
		mutex_lock(&dvbdmx->mutex);
	}

	spin_lock_irq(&dvbdmx->lock);
	f = dvbdmxfeed->filter;

	if (f == dvbdmxfilter) {
		dvbdmxfeed->filter = dvbdmxfilter->next;
	} else {
		while (f->next != dvbdmxfilter)
			f = f->next;
		f->next = f->next->next;
	}

	dvbdmxfilter->state = DMX_STATE_FREE;
	spin_unlock_irq(&dvbdmx->lock);
	mutex_unlock(&dvbdmx->mutex);
	return 0;
}

static int dvbdmx_allocate_section_feed(struct dmx_demux *demux,
					struct dmx_section_feed **feed,
					dmx_section_cb callback)
{
	struct dvb_demux *dvbdmx = (struct dvb_demux *)demux;
	struct dvb_demux_feed *dvbdmxfeed;

	if (mutex_lock_interruptible(&dvbdmx->mutex))
		return -ERESTARTSYS;

	if (!(dvbdmxfeed = dvb_dmx_feed_alloc(dvbdmx))) {
		mutex_unlock(&dvbdmx->mutex);
		return -EBUSY;
	}

	dvbdmxfeed->type = DMX_TYPE_SEC;
	dvbdmxfeed->cb.sec = callback;
	dvbdmxfeed->demux = dvbdmx;
	dvbdmxfeed->pid = 0xffff;
	dvbdmxfeed->feed.sec.secbuf = dvbdmxfeed->feed.sec.secbuf_base;
	dvbdmxfeed->feed.sec.secbufp = dvbdmxfeed->feed.sec.seclen = 0;
	dvbdmxfeed->feed.sec.tsfeedp = 0;
	dvbdmxfeed->filter = NULL;

	(*feed) = &dvbdmxfeed->feed.sec;
	(*feed)->is_filtering = 0;
	(*feed)->parent = demux;
	(*feed)->priv = NULL;

	(*feed)->set = dmx_section_feed_set;
	(*feed)->allocate_filter = dmx_section_feed_allocate_filter;
	(*feed)->start_filtering = dmx_section_feed_start_filtering;
	(*feed)->stop_filtering = dmx_section_feed_stop_filtering;
	(*feed)->release_filter = dmx_section_feed_release_filter;

	mutex_unlock(&dvbdmx->mutex);
	return 0;
}

static int dvbdmx_release_section_feed(struct dmx_demux *demux,
				       struct dmx_section_feed *feed)
{
	struct dvb_demux_feed *dvbdmxfeed = (struct dvb_demux_feed *)feed;
	struct dvb_demux *dvbdmx = (struct dvb_demux *)demux;

	mutex_lock(&dvbdmx->mutex);

	if (dvbdmxfeed->state == DMX_STATE_FREE) {
		mutex_unlock(&dvbdmx->mutex);
		return -EINVAL;
	}
	dvbdmxfeed->state = DMX_STATE_FREE;

	dvb_demux_feed_del(dvbdmxfeed);

	dvbdmxfeed->pid = 0xffff;

	mutex_unlock(&dvbdmx->mutex);
	return 0;
}

/******************************************************************************
 * dvb_demux kernel data API calls
 ******************************************************************************/

static int dvbdmx_open(struct dmx_demux *demux)
{
	struct dvb_demux *dvbdemux = (struct dvb_demux *)demux;

	if (dvbdemux->users >= MAX_DVB_DEMUX_USERS)
		return -EUSERS;

	dvbdemux->users++;
	return 0;
}

static int dvbdmx_close(struct dmx_demux *demux)
{
	struct dvb_demux *dvbdemux = (struct dvb_demux *)demux;

	if (dvbdemux->users == 0)
		return -ENODEV;

	dvbdemux->users--;
	//FIXME: release any unneeded resources if users==0
	return 0;
}

static int dvbdmx_write(struct dmx_demux *demux, const char __user *buf, size_t count)
{
	struct dvb_demux *dvbdemux = (struct dvb_demux *)demux;
	void *p;

	if ((!demux->frontend) || (demux->frontend->source != DMX_MEMORY_FE))
		return -EINVAL;

	p = memdup_user(buf, count);
	if (IS_ERR(p))
		return PTR_ERR(p);
	if (mutex_lock_interruptible(&dvbdemux->mutex)) {
		kfree(p);
		return -ERESTARTSYS;
	}
	dvb_dmx_swfilter(dvbdemux, p, count);
	kfree(p);
	mutex_unlock(&dvbdemux->mutex);

	if (signal_pending(current))
		return -EINTR;
	return count;
}

static int dvbdmx_add_frontend(struct dmx_demux *demux,
			       struct dmx_frontend *frontend)
{
	struct dvb_demux *dvbdemux = (struct dvb_demux *)demux;
	struct list_head *head = &dvbdemux->frontend_list;

	list_add(&(frontend->connectivity_list), head);

	return 0;
}

static int dvbdmx_remove_frontend(struct dmx_demux *demux,
				  struct dmx_frontend *frontend)
{
	struct dvb_demux *dvbdemux = (struct dvb_demux *)demux;
	struct list_head *pos, *n, *head = &dvbdemux->frontend_list;

	list_for_each_safe(pos, n, head) {
		if (DMX_FE_ENTRY(pos) == frontend) {
			list_del(pos);
			return 0;
		}
	}

	return -ENODEV;
}

static struct list_head *dvbdmx_get_frontends(struct dmx_demux *demux)
{
	struct dvb_demux *dvbdemux = (struct dvb_demux *)demux;

	if (list_empty(&dvbdemux->frontend_list))
		return NULL;

	return &dvbdemux->frontend_list;
}

static int dvbdmx_connect_frontend(struct dmx_demux *demux,
				   struct dmx_frontend *frontend)
{
	struct dvb_demux *dvbdemux = (struct dvb_demux *)demux;

	if (demux->frontend)
		return -EINVAL;

	mutex_lock(&dvbdemux->mutex);

	demux->frontend = frontend;
	mutex_unlock(&dvbdemux->mutex);
	return 0;
}

static int dvbdmx_disconnect_frontend(struct dmx_demux *demux)
{
	struct dvb_demux *dvbdemux = (struct dvb_demux *)demux;

	mutex_lock(&dvbdemux->mutex);

	demux->frontend = NULL;
	mutex_unlock(&dvbdemux->mutex);
	return 0;
}

static int dvbdmx_get_pes_pids(struct dmx_demux *demux, u16 * pids)
{
	struct dvb_demux *dvbdemux = (struct dvb_demux *)demux;

	memcpy(pids, dvbdemux->pids, 5 * sizeof(u16));
	return 0;
}

int dvb_dmx_init(struct dvb_demux *dvbdemux)
{
	int i;
	struct dmx_demux *dmx = &dvbdemux->dmx;

	dvbdemux->cnt_storage = NULL;
	dvbdemux->users = 0;
	dvbdemux->filter = vmalloc(dvbdemux->filternum * sizeof(struct dvb_demux_filter));

	if (!dvbdemux->filter)
		return -ENOMEM;

	dvbdemux->feed = vmalloc(dvbdemux->feednum * sizeof(struct dvb_demux_feed));
	if (!dvbdemux->feed) {
		vfree(dvbdemux->filter);
		dvbdemux->filter = NULL;
		return -ENOMEM;
	}
	for (i = 0; i < dvbdemux->filternum; i++) {
		dvbdemux->filter[i].state = DMX_STATE_FREE;
		dvbdemux->filter[i].index = i;
	}
	for (i = 0; i < dvbdemux->feednum; i++) {
		dvbdemux->feed[i].state = DMX_STATE_FREE;
		dvbdemux->feed[i].index = i;
	}

	dvbdemux->cnt_storage = vmalloc(MAX_PID + 1);
	if (!dvbdemux->cnt_storage)
		pr_warn("Couldn't allocate memory for TS/TEI check. Disabling it\n");

	INIT_LIST_HEAD(&dvbdemux->frontend_list);

	for (i = 0; i < DMX_PES_OTHER; i++) {
		dvbdemux->pesfilter[i] = NULL;
		dvbdemux->pids[i] = 0xffff;
	}

	INIT_LIST_HEAD(&dvbdemux->feed_list);

	dvbdemux->playing = 0;
	dvbdemux->recording = 0;
	dvbdemux->tsbufp = 0;

	if (!dvbdemux->check_crc32)
		dvbdemux->check_crc32 = dvb_dmx_crc32;

	if (!dvbdemux->memcopy)
		dvbdemux->memcopy = dvb_dmx_memcopy;

	dmx->frontend = NULL;
	dmx->priv = dvbdemux;
	dmx->open = dvbdmx_open;
	dmx->close = dvbdmx_close;
	dmx->write = dvbdmx_write;
	dmx->allocate_ts_feed = dvbdmx_allocate_ts_feed;
	dmx->release_ts_feed = dvbdmx_release_ts_feed;
	dmx->allocate_section_feed = dvbdmx_allocate_section_feed;
	dmx->release_section_feed = dvbdmx_release_section_feed;

	dmx->add_frontend = dvbdmx_add_frontend;
	dmx->remove_frontend = dvbdmx_remove_frontend;
	dmx->get_frontends = dvbdmx_get_frontends;
	dmx->connect_frontend = dvbdmx_connect_frontend;
	dmx->disconnect_frontend = dvbdmx_disconnect_frontend;
	dmx->get_pes_pids = dvbdmx_get_pes_pids;

	mutex_init(&dvbdemux->mutex);
	spin_lock_init(&dvbdemux->lock);

	return 0;
}

EXPORT_SYMBOL(dvb_dmx_init);

void dvb_dmx_release(struct dvb_demux *dvbdemux)
{
	vfree(dvbdemux->cnt_storage);
	vfree(dvbdemux->filter);
	vfree(dvbdemux->feed);
}

EXPORT_SYMBOL(dvb_dmx_release);

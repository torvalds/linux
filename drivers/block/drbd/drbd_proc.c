/*
   drbd_proc.c

   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2001-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 1999-2008, Philipp Reisner <philipp.reisner@linbit.com>.
   Copyright (C) 2002-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

   drbd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   drbd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with drbd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 */

#include <linux/module.h>

#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/drbd.h>
#include "drbd_int.h"

static int drbd_proc_open(struct inode *inode, struct file *file);
static int drbd_proc_release(struct inode *inode, struct file *file);


struct proc_dir_entry *drbd_proc;
const struct file_operations drbd_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= drbd_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= drbd_proc_release,
};

static void seq_printf_with_thousands_grouping(struct seq_file *seq, long v)
{
	/* v is in kB/sec. We don't expect TiByte/sec yet. */
	if (unlikely(v >= 1000000)) {
		/* cool: > GiByte/s */
		seq_printf(seq, "%ld,", v / 1000000);
		v %= 1000000;
		seq_printf(seq, "%03ld,%03ld", v/1000, v % 1000);
	} else if (likely(v >= 1000))
		seq_printf(seq, "%ld,%03ld", v/1000, v % 1000);
	else
		seq_printf(seq, "%ld", v);
}

static void drbd_get_syncer_progress(struct drbd_device *device,
		union drbd_dev_state state, unsigned long *rs_total,
		unsigned long *bits_left, unsigned int *per_mil_done)
{
	/* this is to break it at compile time when we change that, in case we
	 * want to support more than (1<<32) bits on a 32bit arch. */
	typecheck(unsigned long, device->rs_total);
	*rs_total = device->rs_total;

	/* note: both rs_total and rs_left are in bits, i.e. in
	 * units of BM_BLOCK_SIZE.
	 * for the percentage, we don't care. */

	if (state.conn == C_VERIFY_S || state.conn == C_VERIFY_T)
		*bits_left = device->ov_left;
	else
		*bits_left = drbd_bm_total_weight(device) - device->rs_failed;
	/* >> 10 to prevent overflow,
	 * +1 to prevent division by zero */
	if (*bits_left > *rs_total) {
		/* D'oh. Maybe a logic bug somewhere.  More likely just a race
		 * between state change and reset of rs_total.
		 */
		*bits_left = *rs_total;
		*per_mil_done = *rs_total ? 0 : 1000;
	} else {
		/* Make sure the division happens in long context.
		 * We allow up to one petabyte storage right now,
		 * at a granularity of 4k per bit that is 2**38 bits.
		 * After shift right and multiplication by 1000,
		 * this should still fit easily into a 32bit long,
		 * so we don't need a 64bit division on 32bit arch.
		 * Note: currently we don't support such large bitmaps on 32bit
		 * arch anyways, but no harm done to be prepared for it here.
		 */
		unsigned int shift = *rs_total > UINT_MAX ? 16 : 10;
		unsigned long left = *bits_left >> shift;
		unsigned long total = 1UL + (*rs_total >> shift);
		unsigned long tmp = 1000UL - left * 1000UL/total;
		*per_mil_done = tmp;
	}
}


/*lge
 * progress bars shamelessly adapted from driver/md/md.c
 * output looks like
 *	[=====>..............] 33.5% (23456/123456)
 *	finish: 2:20:20 speed: 6,345 (6,456) K/sec
 */
static void drbd_syncer_progress(struct drbd_device *device, struct seq_file *seq,
		union drbd_dev_state state)
{
	unsigned long db, dt, dbdt, rt, rs_total, rs_left;
	unsigned int res;
	int i, x, y;
	int stalled = 0;

	drbd_get_syncer_progress(device, state, &rs_total, &rs_left, &res);

	x = res/50;
	y = 20-x;
	seq_puts(seq, "\t[");
	for (i = 1; i < x; i++)
		seq_putc(seq, '=');
	seq_putc(seq, '>');
	for (i = 0; i < y; i++)
		seq_putc(seq, '.');
	seq_puts(seq, "] ");

	if (state.conn == C_VERIFY_S || state.conn == C_VERIFY_T)
		seq_puts(seq, "verified:");
	else
		seq_puts(seq, "sync'ed:");
	seq_printf(seq, "%3u.%u%% ", res / 10, res % 10);

	/* if more than a few GB, display in MB */
	if (rs_total > (4UL << (30 - BM_BLOCK_SHIFT)))
		seq_printf(seq, "(%lu/%lu)M",
			    (unsigned long) Bit2KB(rs_left >> 10),
			    (unsigned long) Bit2KB(rs_total >> 10));
	else
		seq_printf(seq, "(%lu/%lu)K",
			    (unsigned long) Bit2KB(rs_left),
			    (unsigned long) Bit2KB(rs_total));

	seq_puts(seq, "\n\t");

	/* see drivers/md/md.c
	 * We do not want to overflow, so the order of operands and
	 * the * 100 / 100 trick are important. We do a +1 to be
	 * safe against division by zero. We only estimate anyway.
	 *
	 * dt: time from mark until now
	 * db: blocks written from mark until now
	 * rt: remaining time
	 */
	/* Rolling marks. last_mark+1 may just now be modified.  last_mark+2 is
	 * at least (DRBD_SYNC_MARKS-2)*DRBD_SYNC_MARK_STEP old, and has at
	 * least DRBD_SYNC_MARK_STEP time before it will be modified. */
	/* ------------------------ ~18s average ------------------------ */
	i = (device->rs_last_mark + 2) % DRBD_SYNC_MARKS;
	dt = (jiffies - device->rs_mark_time[i]) / HZ;
	if (dt > 180)
		stalled = 1;

	if (!dt)
		dt++;
	db = device->rs_mark_left[i] - rs_left;
	rt = (dt * (rs_left / (db/100+1)))/100; /* seconds */

	seq_printf(seq, "finish: %lu:%02lu:%02lu",
		rt / 3600, (rt % 3600) / 60, rt % 60);

	dbdt = Bit2KB(db/dt);
	seq_puts(seq, " speed: ");
	seq_printf_with_thousands_grouping(seq, dbdt);
	seq_puts(seq, " (");
	/* ------------------------- ~3s average ------------------------ */
	if (drbd_proc_details >= 1) {
		/* this is what drbd_rs_should_slow_down() uses */
		i = (device->rs_last_mark + DRBD_SYNC_MARKS-1) % DRBD_SYNC_MARKS;
		dt = (jiffies - device->rs_mark_time[i]) / HZ;
		if (!dt)
			dt++;
		db = device->rs_mark_left[i] - rs_left;
		dbdt = Bit2KB(db/dt);
		seq_printf_with_thousands_grouping(seq, dbdt);
		seq_puts(seq, " -- ");
	}

	/* --------------------- long term average ---------------------- */
	/* mean speed since syncer started
	 * we do account for PausedSync periods */
	dt = (jiffies - device->rs_start - device->rs_paused) / HZ;
	if (dt == 0)
		dt = 1;
	db = rs_total - rs_left;
	dbdt = Bit2KB(db/dt);
	seq_printf_with_thousands_grouping(seq, dbdt);
	seq_putc(seq, ')');

	if (state.conn == C_SYNC_TARGET ||
	    state.conn == C_VERIFY_S) {
		seq_puts(seq, " want: ");
		seq_printf_with_thousands_grouping(seq, device->c_sync_rate);
	}
	seq_printf(seq, " K/sec%s\n", stalled ? " (stalled)" : "");

	if (drbd_proc_details >= 1) {
		/* 64 bit:
		 * we convert to sectors in the display below. */
		unsigned long bm_bits = drbd_bm_bits(device);
		unsigned long bit_pos;
		unsigned long long stop_sector = 0;
		if (state.conn == C_VERIFY_S ||
		    state.conn == C_VERIFY_T) {
			bit_pos = bm_bits - device->ov_left;
			if (verify_can_do_stop_sector(device))
				stop_sector = device->ov_stop_sector;
		} else
			bit_pos = device->bm_resync_fo;
		/* Total sectors may be slightly off for oddly
		 * sized devices. So what. */
		seq_printf(seq,
			"\t%3d%% sector pos: %llu/%llu",
			(int)(bit_pos / (bm_bits/100+1)),
			(unsigned long long)bit_pos * BM_SECT_PER_BIT,
			(unsigned long long)bm_bits * BM_SECT_PER_BIT);
		if (stop_sector != 0 && stop_sector != ULLONG_MAX)
			seq_printf(seq, " stop sector: %llu", stop_sector);
		seq_putc(seq, '\n');
	}
}

static int drbd_seq_show(struct seq_file *seq, void *v)
{
	int i, prev_i = -1;
	const char *sn;
	struct drbd_device *device;
	struct net_conf *nc;
	union drbd_dev_state state;
	char wp;

	static char write_ordering_chars[] = {
		[WO_NONE] = 'n',
		[WO_DRAIN_IO] = 'd',
		[WO_BDEV_FLUSH] = 'f',
	};

	seq_printf(seq, "version: " REL_VERSION " (api:%d/proto:%d-%d)\n%s\n",
		   API_VERSION, PRO_VERSION_MIN, PRO_VERSION_MAX, drbd_buildtag());

	/*
	  cs .. connection state
	  ro .. node role (local/remote)
	  ds .. disk state (local/remote)
	     protocol
	     various flags
	  ns .. network send
	  nr .. network receive
	  dw .. disk write
	  dr .. disk read
	  al .. activity log write count
	  bm .. bitmap update write count
	  pe .. pending (waiting for ack or data reply)
	  ua .. unack'd (still need to send ack or data reply)
	  ap .. application requests accepted, but not yet completed
	  ep .. number of epochs currently "on the fly", P_BARRIER_ACK pending
	  wo .. write ordering mode currently in use
	 oos .. known out-of-sync kB
	*/

	rcu_read_lock();
	idr_for_each_entry(&drbd_devices, device, i) {
		if (prev_i != i - 1)
			seq_putc(seq, '\n');
		prev_i = i;

		state = device->state;
		sn = drbd_conn_str(state.conn);

		if (state.conn == C_STANDALONE &&
		    state.disk == D_DISKLESS &&
		    state.role == R_SECONDARY) {
			seq_printf(seq, "%2d: cs:Unconfigured\n", i);
		} else {
			/* reset device->congestion_reason */
			bdi_rw_congested(device->rq_queue->backing_dev_info);

			nc = rcu_dereference(first_peer_device(device)->connection->net_conf);
			wp = nc ? nc->wire_protocol - DRBD_PROT_A + 'A' : ' ';
			seq_printf(seq,
			   "%2d: cs:%s ro:%s/%s ds:%s/%s %c %c%c%c%c%c%c\n"
			   "    ns:%u nr:%u dw:%u dr:%u al:%u bm:%u "
			   "lo:%d pe:%d ua:%d ap:%d ep:%d wo:%c",
			   i, sn,
			   drbd_role_str(state.role),
			   drbd_role_str(state.peer),
			   drbd_disk_str(state.disk),
			   drbd_disk_str(state.pdsk),
			   wp,
			   drbd_suspended(device) ? 's' : 'r',
			   state.aftr_isp ? 'a' : '-',
			   state.peer_isp ? 'p' : '-',
			   state.user_isp ? 'u' : '-',
			   device->congestion_reason ?: '-',
			   test_bit(AL_SUSPENDED, &device->flags) ? 's' : '-',
			   device->send_cnt/2,
			   device->recv_cnt/2,
			   device->writ_cnt/2,
			   device->read_cnt/2,
			   device->al_writ_cnt,
			   device->bm_writ_cnt,
			   atomic_read(&device->local_cnt),
			   atomic_read(&device->ap_pending_cnt) +
			   atomic_read(&device->rs_pending_cnt),
			   atomic_read(&device->unacked_cnt),
			   atomic_read(&device->ap_bio_cnt),
			   first_peer_device(device)->connection->epochs,
			   write_ordering_chars[device->resource->write_ordering]
			);
			seq_printf(seq, " oos:%llu\n",
				   Bit2KB((unsigned long long)
					   drbd_bm_total_weight(device)));
		}
		if (state.conn == C_SYNC_SOURCE ||
		    state.conn == C_SYNC_TARGET ||
		    state.conn == C_VERIFY_S ||
		    state.conn == C_VERIFY_T)
			drbd_syncer_progress(device, seq, state);

		if (drbd_proc_details >= 1 && get_ldev_if_state(device, D_FAILED)) {
			lc_seq_printf_stats(seq, device->resync);
			lc_seq_printf_stats(seq, device->act_log);
			put_ldev(device);
		}

		if (drbd_proc_details >= 2)
			seq_printf(seq, "\tblocked on activity log: %d\n", atomic_read(&device->ap_actlog_cnt));
	}
	rcu_read_unlock();

	return 0;
}

static int drbd_proc_open(struct inode *inode, struct file *file)
{
	int err;

	if (try_module_get(THIS_MODULE)) {
		err = single_open(file, drbd_seq_show, NULL);
		if (err)
			module_put(THIS_MODULE);
		return err;
	}
	return -ENODEV;
}

static int drbd_proc_release(struct inode *inode, struct file *file)
{
	module_put(THIS_MODULE);
	return single_release(inode, file);
}

/* PROC FS stuff end */

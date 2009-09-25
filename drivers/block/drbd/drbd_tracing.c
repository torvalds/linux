/*
   drbd_tracing.c

   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2003-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 2003-2008, Philipp Reisner <philipp.reisner@linbit.com>.
   Copyright (C) 2003-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

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
#include <linux/drbd.h>
#include <linux/ctype.h>
#include "drbd_int.h"
#include "drbd_tracing.h"
#include <linux/drbd_tag_magic.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Philipp Reisner, Lars Ellenberg");
MODULE_DESCRIPTION("DRBD tracepoint probes");
MODULE_PARM_DESC(trace_mask, "Bitmap of events to trace see drbd_tracing.c");
MODULE_PARM_DESC(trace_level, "Current tracing level (changeable in /sys)");
MODULE_PARM_DESC(trace_devs, "Bitmap of devices to trace (changeable in /sys)");

unsigned int trace_mask = 0;  /* Bitmap of events to trace */
int trace_level;              /* Current trace level */
int trace_devs;		      /* Bitmap of devices to trace */

module_param(trace_mask, uint, 0444);
module_param(trace_level, int, 0644);
module_param(trace_devs, int, 0644);

enum {
	TRACE_PACKET  = 0x0001,
	TRACE_RQ      = 0x0002,
	TRACE_UUID    = 0x0004,
	TRACE_RESYNC  = 0x0008,
	TRACE_EE      = 0x0010,
	TRACE_UNPLUG  = 0x0020,
	TRACE_NL      = 0x0040,
	TRACE_AL_EXT  = 0x0080,
	TRACE_INT_RQ  = 0x0100,
	TRACE_MD_IO   = 0x0200,
	TRACE_EPOCH   = 0x0400,
};

/* Buffer printing support
 * dbg_print_flags: used for Flags arg to drbd_print_buffer
 * - DBGPRINT_BUFFADDR; if set, each line starts with the
 *	 virtual address of the line being output. If clear,
 *	 each line starts with the offset from the beginning
 *	 of the buffer. */
enum dbg_print_flags {
    DBGPRINT_BUFFADDR = 0x0001,
};

/* Macro stuff */
static char *nl_packet_name(int packet_type)
{
/* Generate packet type strings */
#define NL_PACKET(name, number, fields) \
	[P_ ## name] = # name,
#define NL_INTEGER Argh!
#define NL_BIT Argh!
#define NL_INT64 Argh!
#define NL_STRING Argh!

	static char *nl_tag_name[P_nl_after_last_packet] = {
#include "linux/drbd_nl.h"
	};

	return (packet_type < sizeof(nl_tag_name)/sizeof(nl_tag_name[0])) ?
	    nl_tag_name[packet_type] : "*Unknown*";
}
/* /Macro stuff */

static inline int is_mdev_trace(struct drbd_conf *mdev, unsigned int level)
{
	return trace_level >= level && ((1 << mdev_to_minor(mdev)) & trace_devs);
}

static void probe_drbd_unplug(struct drbd_conf *mdev, char *msg)
{
	if (!is_mdev_trace(mdev, TRACE_LVL_ALWAYS))
		return;

	dev_info(DEV, "%s, ap_bio_count=%d\n", msg, atomic_read(&mdev->ap_bio_cnt));
}

static void probe_drbd_uuid(struct drbd_conf *mdev, enum drbd_uuid_index index)
{
	static char *uuid_str[UI_EXTENDED_SIZE] = {
		[UI_CURRENT] = "CURRENT",
		[UI_BITMAP] = "BITMAP",
		[UI_HISTORY_START] = "HISTORY_START",
		[UI_HISTORY_END] = "HISTORY_END",
		[UI_SIZE] = "SIZE",
		[UI_FLAGS] = "FLAGS",
	};

	if (!is_mdev_trace(mdev, TRACE_LVL_ALWAYS))
		return;

	if (index >= UI_EXTENDED_SIZE) {
		dev_warn(DEV, " uuid_index >= EXTENDED_SIZE\n");
		return;
	}

	dev_info(DEV, " uuid[%s] now %016llX\n",
		 uuid_str[index],
		 (unsigned long long)mdev->ldev->md.uuid[index]);
}

static void probe_drbd_md_io(struct drbd_conf *mdev, int rw,
			     struct drbd_backing_dev *bdev)
{
	if (!is_mdev_trace(mdev, TRACE_LVL_ALWAYS))
		return;

	dev_info(DEV, " %s metadata superblock now\n",
		 rw == READ ? "Reading" : "Writing");
}

static void probe_drbd_ee(struct drbd_conf *mdev, struct drbd_epoch_entry *e, char* msg)
{
	if (!is_mdev_trace(mdev, TRACE_LVL_ALWAYS))
		return;

	dev_info(DEV, "EE %s sec=%llus size=%u e=%p\n",
		 msg, (unsigned long long)e->sector, e->size, e);
}

static void probe_drbd_epoch(struct drbd_conf *mdev, struct drbd_epoch *epoch,
			     enum epoch_event ev)
{
	static char *epoch_event_str[] = {
		[EV_PUT] = "put",
		[EV_GOT_BARRIER_NR] = "got_barrier_nr",
		[EV_BARRIER_DONE] = "barrier_done",
		[EV_BECAME_LAST] = "became_last",
		[EV_TRACE_FLUSH] = "issuing_flush",
		[EV_TRACE_ADD_BARRIER] = "added_barrier",
		[EV_TRACE_SETTING_BI] = "just set barrier_in_next_epoch",
	};

	if (!is_mdev_trace(mdev, TRACE_LVL_ALWAYS))
		return;

	ev &= ~EV_CLEANUP;

	switch (ev) {
	case EV_TRACE_ALLOC:
		dev_info(DEV, "Allocate epoch %p/xxxx { } nr_epochs=%d\n", epoch, mdev->epochs);
		break;
	case EV_TRACE_FREE:
		dev_info(DEV, "Freeing epoch %p/%d { size=%d } nr_epochs=%d\n",
			 epoch, epoch->barrier_nr, atomic_read(&epoch->epoch_size),
			 mdev->epochs);
		break;
	default:
		dev_info(DEV, "Update epoch  %p/%d { size=%d active=%d %c%c n%c%c } ev=%s\n",
			 epoch, epoch->barrier_nr, atomic_read(&epoch->epoch_size),
			 atomic_read(&epoch->active),
			 test_bit(DE_HAVE_BARRIER_NUMBER, &epoch->flags) ? 'n' : '-',
			 test_bit(DE_CONTAINS_A_BARRIER, &epoch->flags) ? 'b' : '-',
			 test_bit(DE_BARRIER_IN_NEXT_EPOCH_ISSUED, &epoch->flags) ? 'i' : '-',
			 test_bit(DE_BARRIER_IN_NEXT_EPOCH_DONE, &epoch->flags) ? 'd' : '-',
			 epoch_event_str[ev]);
	}
}

static void probe_drbd_netlink(void *data, int is_req)
{
	struct cn_msg *msg = data;

	if (is_req) {
		struct drbd_nl_cfg_req *nlp = (struct drbd_nl_cfg_req *)msg->data;

		printk(KERN_INFO "drbd%d: "
			 "Netlink: << %s (%d) - seq: %x, ack: %x, len: %x\n",
			 nlp->drbd_minor,
			 nl_packet_name(nlp->packet_type),
			 nlp->packet_type,
			 msg->seq, msg->ack, msg->len);
	} else {
		struct drbd_nl_cfg_reply *nlp = (struct drbd_nl_cfg_reply *)msg->data;

		printk(KERN_INFO "drbd%d: "
		       "Netlink: >> %s (%d) - seq: %x, ack: %x, len: %x\n",
		       nlp->minor,
		       nlp->packet_type == P_nl_after_last_packet ?
		       "Empty-Reply" : nl_packet_name(nlp->packet_type),
		       nlp->packet_type,
		       msg->seq, msg->ack, msg->len);
	}
}

static void probe_drbd_actlog(struct drbd_conf *mdev, sector_t sector, char* msg)
{
	unsigned int enr = (sector >> (AL_EXTENT_SHIFT-9));

	if (!is_mdev_trace(mdev, TRACE_LVL_ALWAYS))
		return;

	dev_info(DEV, "%s (sec=%llus, al_enr=%u, rs_enr=%d)\n",
		 msg, (unsigned long long) sector, enr,
		 (int)BM_SECT_TO_EXT(sector));
}

/**
 * drbd_print_buffer() - Hexdump arbitrary binary data into a buffer
 * @prefix:	String is output at the beginning of each line output.
 * @flags:	Currently only defined flag: DBGPRINT_BUFFADDR; if set, each
 *		line starts with the virtual address of the line being
 *		output. If clear, each line starts with the offset from the
 *		beginning of the buffer.
 * @size:	Indicates the size of each entry in the buffer. Supported
 * 		values are sizeof(char), sizeof(short) and sizeof(int)
 * @buffer:	Start address of buffer
 * @buffer_va:	Virtual address of start of buffer (normally the same
 *		as Buffer, but having it separate allows it to hold
 *		file address for example)
 * @length:	length of buffer
 */
static void drbd_print_buffer(const char *prefix, unsigned int flags, int size,
			      const void *buffer, const void *buffer_va,
			      unsigned int length)

#define LINE_SIZE       16
#define LINE_ENTRIES    (int)(LINE_SIZE/size)
{
	const unsigned char *pstart;
	const unsigned char *pstart_va;
	const unsigned char *pend;
	char bytes_str[LINE_SIZE*3+8], ascii_str[LINE_SIZE+8];
	char *pbytes = bytes_str, *pascii = ascii_str;
	int  offset = 0;
	long sizemask;
	int  field_width;
	int  index;
	const unsigned char *pend_str;
	const unsigned char *p;
	int count;

	/* verify size parameter */
	if (size != sizeof(char) &&
	    size != sizeof(short) &&
	    size != sizeof(int)) {
		printk(KERN_DEBUG "drbd_print_buffer: "
			"ERROR invalid size %d\n", size);
		return;
	}

	sizemask = size-1;
	field_width = size*2;

	/* Adjust start/end to be on appropriate boundary for size */
	buffer = (const char *)((long)buffer & ~sizemask);
	pend   = (const unsigned char *)
		(((long)buffer + length + sizemask) & ~sizemask);

	if (flags & DBGPRINT_BUFFADDR) {
		/* Move start back to nearest multiple of line size,
		 * if printing address. This results in nicely formatted output
		 * with addresses being on line size (16) byte boundaries */
		pstart = (const unsigned char *)((long)buffer & ~(LINE_SIZE-1));
	} else {
		pstart = (const unsigned char *)buffer;
	}

	/* Set value of start VA to print if addresses asked for */
	pstart_va = (const unsigned char *)buffer_va
		 - ((const unsigned char *)buffer-pstart);

	/* Calculate end position to nicely align right hand side */
	pend_str = pstart + (((pend-pstart) + LINE_SIZE-1) & ~(LINE_SIZE-1));

	/* Init strings */
	*pbytes = *pascii = '\0';

	/* Start at beginning of first line */
	p = pstart;
	count = 0;

	while (p < pend_str) {
		if (p < (const unsigned char *)buffer || p >= pend) {
			/* Before start of buffer or after end- print spaces */
			pbytes += sprintf(pbytes, "%*c ", field_width, ' ');
			pascii += sprintf(pascii, "%*c", size, ' ');
			p += size;
		} else {
			/* Add hex and ascii to strings */
			int val;
			switch (size) {
			default:
			case 1:
				val = *(unsigned char *)p;
				break;
			case 2:
				val = *(unsigned short *)p;
				break;
			case 4:
				val = *(unsigned int *)p;
				break;
			}

			pbytes += sprintf(pbytes, "%0*x ", field_width, val);

			for (index = size; index; index--) {
				*pascii++ = isprint(*p) ? *p : '.';
				p++;
			}
		}

		count++;

		if (count == LINE_ENTRIES || p >= pend_str) {
			/* Null terminate and print record */
			*pascii = '\0';
			printk(KERN_DEBUG "%s%8.8lx: %*s|%*s|\n",
			       prefix,
			       (flags & DBGPRINT_BUFFADDR)
			       ? (long)pstart_va:(long)offset,
			       LINE_ENTRIES*(field_width+1), bytes_str,
			       LINE_SIZE, ascii_str);

			/* Move onto next line */
			pstart_va += (p-pstart);
			pstart = p;
			count  = 0;
			offset += LINE_SIZE;

			/* Re-init strings */
			pbytes = bytes_str;
			pascii = ascii_str;
			*pbytes = *pascii = '\0';
		}
	}
}

static void probe_drbd_resync(struct drbd_conf *mdev, int level, const char *fmt, va_list args)
{
	char str[256];

	if (!is_mdev_trace(mdev, level))
		return;

	if (vsnprintf(str, 256, fmt, args) >= 256)
		str[255] = 0;

	printk(KERN_INFO "%s %s: %s", dev_driver_string(disk_to_dev(mdev->vdisk)),
	       dev_name(disk_to_dev(mdev->vdisk)), str);
}

static void probe_drbd_bio(struct drbd_conf *mdev, const char *pfx, struct bio *bio, int complete,
			   struct drbd_request *r)
{
#if defined(CONFIG_LBDAF) || defined(CONFIG_LBD)
#define SECTOR_FORMAT "%Lx"
#else
#define SECTOR_FORMAT "%lx"
#endif
#define SECTOR_SHIFT 9

	unsigned long lowaddr = (unsigned long)(bio->bi_sector << SECTOR_SHIFT);
	char *faddr = (char *)(lowaddr);
	char rb[sizeof(void *)*2+6] = { 0, };
	struct bio_vec *bvec;
	int segno;

	const int rw = bio->bi_rw;
	const int biorw      = (rw & (RW_MASK|RWA_MASK));
	const int biobarrier = (rw & (1<<BIO_RW_BARRIER));
	const int biosync = (rw & ((1<<BIO_RW_UNPLUG) | (1<<BIO_RW_SYNCIO)));

	if (!is_mdev_trace(mdev, TRACE_LVL_ALWAYS))
		return;

	if (r)
		sprintf(rb, "Req:%p ", r);

	dev_info(DEV, "%s %s:%s%s%s Bio:%p %s- %soffset " SECTOR_FORMAT ", size %x\n",
		 complete ? "<<<" : ">>>",
		 pfx,
		 biorw == WRITE ? "Write" : "Read",
		 biobarrier ? " : B" : "",
		 biosync ? " : S" : "",
		 bio,
		 rb,
		 complete ? (bio_flagged(bio, BIO_UPTODATE) ? "Success, " : "Failed, ") : "",
		 bio->bi_sector << SECTOR_SHIFT,
		 bio->bi_size);

	if (trace_level >= TRACE_LVL_METRICS &&
	    ((biorw == WRITE) ^ complete)) {
		printk(KERN_DEBUG "  ind     page   offset   length\n");
		__bio_for_each_segment(bvec, bio, segno, 0) {
			printk(KERN_DEBUG "  [%d] %p %8.8x %8.8x\n", segno,
			       bvec->bv_page, bvec->bv_offset, bvec->bv_len);

			if (trace_level >= TRACE_LVL_ALL) {
				char *bvec_buf;
				unsigned long flags;

				bvec_buf = bvec_kmap_irq(bvec, &flags);

				drbd_print_buffer("    ", DBGPRINT_BUFFADDR, 1,
						  bvec_buf,
						  faddr,
						  (bvec->bv_len <= 0x80)
						  ? bvec->bv_len : 0x80);

				bvec_kunmap_irq(bvec_buf, &flags);

				if (bvec->bv_len > 0x40)
					printk(KERN_DEBUG "    ....\n");

				faddr += bvec->bv_len;
			}
		}
	}
}

static void probe_drbd_req(struct drbd_request *req, enum drbd_req_event what, char *msg)
{
	static const char *rq_event_names[] = {
		[created] = "created",
		[to_be_send] = "to_be_send",
		[to_be_submitted] = "to_be_submitted",
		[queue_for_net_write] = "queue_for_net_write",
		[queue_for_net_read] = "queue_for_net_read",
		[send_canceled] = "send_canceled",
		[send_failed] = "send_failed",
		[handed_over_to_network] = "handed_over_to_network",
		[connection_lost_while_pending] =
					"connection_lost_while_pending",
		[recv_acked_by_peer] = "recv_acked_by_peer",
		[write_acked_by_peer] = "write_acked_by_peer",
		[neg_acked] = "neg_acked",
		[conflict_discarded_by_peer] = "conflict_discarded_by_peer",
		[barrier_acked] = "barrier_acked",
		[data_received] = "data_received",
		[read_completed_with_error] = "read_completed_with_error",
		[read_ahead_completed_with_error] = "reada_completed_with_error",
		[write_completed_with_error] = "write_completed_with_error",
		[completed_ok] = "completed_ok",
	};

	struct drbd_conf *mdev = req->mdev;

	const int rw = (req->master_bio == NULL ||
			bio_data_dir(req->master_bio) == WRITE) ?
		'W' : 'R';
	const unsigned long s = req->rq_state;

	if (what != nothing) {
		dev_info(DEV, "__req_mod(%p %c ,%s)\n", req, rw, rq_event_names[what]);
	} else {
		dev_info(DEV, "%s %p %c L%c%c%cN%c%c%c%c%c %u (%llus +%u) %s\n",
			 msg, req, rw,
			 s & RQ_LOCAL_PENDING ? 'p' : '-',
			 s & RQ_LOCAL_COMPLETED ? 'c' : '-',
			 s & RQ_LOCAL_OK ? 'o' : '-',
			 s & RQ_NET_PENDING ? 'p' : '-',
			 s & RQ_NET_QUEUED ? 'q' : '-',
			 s & RQ_NET_SENT ? 's' : '-',
			 s & RQ_NET_DONE ? 'd' : '-',
			 s & RQ_NET_OK ? 'o' : '-',
			 req->epoch,
			 (unsigned long long)req->sector,
			 req->size,
			 drbd_conn_str(mdev->state.conn));
	}
}


#define drbd_peer_str drbd_role_str
#define drbd_pdsk_str drbd_disk_str

#define PSM(A)							\
do {								\
	if (mask.A) {						\
		int i = snprintf(p, len, " " #A "( %s )",	\
				 drbd_##A##_str(val.A));	\
		if (i >= len)					\
			return op;				\
		p += i;						\
		len -= i;					\
	}							\
} while (0)

static char *dump_st(char *p, int len, union drbd_state mask, union drbd_state val)
{
	char *op = p;
	*p = '\0';
	PSM(role);
	PSM(peer);
	PSM(conn);
	PSM(disk);
	PSM(pdsk);

	return op;
}

#define INFOP(fmt, args...) \
do { \
	if (trace_level >= TRACE_LVL_ALL) { \
		dev_info(DEV, "%s:%d: %s [%d] %s %s " fmt , \
		     file, line, current->comm, current->pid, \
		     sockname, recv ? "<<<" : ">>>" , \
		     ## args); \
	} else { \
		dev_info(DEV, "%s %s " fmt, sockname, \
		     recv ? "<<<" : ">>>" , \
		     ## args); \
	} \
} while (0)

static char *_dump_block_id(u64 block_id, char *buff)
{
	if (is_syncer_block_id(block_id))
		strcpy(buff, "SyncerId");
	else
		sprintf(buff, "%llx", (unsigned long long)block_id);

	return buff;
}

static void probe_drbd_packet(struct drbd_conf *mdev, struct socket *sock,
			      int recv, union p_polymorph *p, char *file, int line)
{
	char *sockname = sock == mdev->meta.socket ? "meta" : "data";
	int cmd = (recv == 2) ? p->header.command : be16_to_cpu(p->header.command);
	char tmp[300];
	union drbd_state m, v;

	switch (cmd) {
	case P_HAND_SHAKE:
		INFOP("%s (protocol %u-%u)\n", cmdname(cmd),
			be32_to_cpu(p->handshake.protocol_min),
			be32_to_cpu(p->handshake.protocol_max));
		break;

	case P_BITMAP: /* don't report this */
	case P_COMPRESSED_BITMAP: /* don't report this */
		break;

	case P_DATA:
		INFOP("%s (sector %llus, id %s, seq %u, f %x)\n", cmdname(cmd),
		      (unsigned long long)be64_to_cpu(p->data.sector),
		      _dump_block_id(p->data.block_id, tmp),
		      be32_to_cpu(p->data.seq_num),
		      be32_to_cpu(p->data.dp_flags)
			);
		break;

	case P_DATA_REPLY:
	case P_RS_DATA_REPLY:
		INFOP("%s (sector %llus, id %s)\n", cmdname(cmd),
		      (unsigned long long)be64_to_cpu(p->data.sector),
		      _dump_block_id(p->data.block_id, tmp)
			);
		break;

	case P_RECV_ACK:
	case P_WRITE_ACK:
	case P_RS_WRITE_ACK:
	case P_DISCARD_ACK:
	case P_NEG_ACK:
	case P_NEG_RS_DREPLY:
		INFOP("%s (sector %llus, size %u, id %s, seq %u)\n",
			cmdname(cmd),
		      (long long)be64_to_cpu(p->block_ack.sector),
		      be32_to_cpu(p->block_ack.blksize),
		      _dump_block_id(p->block_ack.block_id, tmp),
		      be32_to_cpu(p->block_ack.seq_num)
			);
		break;

	case P_DATA_REQUEST:
	case P_RS_DATA_REQUEST:
		INFOP("%s (sector %llus, size %u, id %s)\n", cmdname(cmd),
		      (long long)be64_to_cpu(p->block_req.sector),
		      be32_to_cpu(p->block_req.blksize),
		      _dump_block_id(p->block_req.block_id, tmp)
			);
		break;

	case P_BARRIER:
	case P_BARRIER_ACK:
		INFOP("%s (barrier %u)\n", cmdname(cmd), p->barrier.barrier);
		break;

	case P_SYNC_PARAM:
	case P_SYNC_PARAM89:
		INFOP("%s (rate %u, verify-alg \"%.64s\", csums-alg \"%.64s\")\n",
			cmdname(cmd), be32_to_cpu(p->rs_param_89.rate),
			p->rs_param_89.verify_alg, p->rs_param_89.csums_alg);
		break;

	case P_UUIDS:
		INFOP("%s Curr:%016llX, Bitmap:%016llX, "
		      "HisSt:%016llX, HisEnd:%016llX\n",
		      cmdname(cmd),
		      (unsigned long long)be64_to_cpu(p->uuids.uuid[UI_CURRENT]),
		      (unsigned long long)be64_to_cpu(p->uuids.uuid[UI_BITMAP]),
		      (unsigned long long)be64_to_cpu(p->uuids.uuid[UI_HISTORY_START]),
		      (unsigned long long)be64_to_cpu(p->uuids.uuid[UI_HISTORY_END]));
		break;

	case P_SIZES:
		INFOP("%s (d %lluMiB, u %lluMiB, c %lldMiB, "
		      "max bio %x, q order %x)\n",
		      cmdname(cmd),
		      (long long)(be64_to_cpu(p->sizes.d_size)>>(20-9)),
		      (long long)(be64_to_cpu(p->sizes.u_size)>>(20-9)),
		      (long long)(be64_to_cpu(p->sizes.c_size)>>(20-9)),
		      be32_to_cpu(p->sizes.max_segment_size),
		      be32_to_cpu(p->sizes.queue_order_type));
		break;

	case P_STATE:
		v.i = be32_to_cpu(p->state.state);
		m.i = 0xffffffff;
		dump_st(tmp, sizeof(tmp), m, v);
		INFOP("%s (s %x {%s})\n", cmdname(cmd), v.i, tmp);
		break;

	case P_STATE_CHG_REQ:
		m.i = be32_to_cpu(p->req_state.mask);
		v.i = be32_to_cpu(p->req_state.val);
		dump_st(tmp, sizeof(tmp), m, v);
		INFOP("%s (m %x v %x {%s})\n", cmdname(cmd), m.i, v.i, tmp);
		break;

	case P_STATE_CHG_REPLY:
		INFOP("%s (ret %x)\n", cmdname(cmd),
		      be32_to_cpu(p->req_state_reply.retcode));
		break;

	case P_PING:
	case P_PING_ACK:
		/*
		 * Dont trace pings at summary level
		 */
		if (trace_level < TRACE_LVL_ALL)
			break;
		/* fall through... */
	default:
		INFOP("%s (%u)\n", cmdname(cmd), cmd);
		break;
	}
}


static int __init drbd_trace_init(void)
{
	int ret;

	if (trace_mask & TRACE_UNPLUG) {
		ret = register_trace_drbd_unplug(probe_drbd_unplug);
		WARN_ON(ret);
	}
	if (trace_mask & TRACE_UUID) {
		ret = register_trace_drbd_uuid(probe_drbd_uuid);
		WARN_ON(ret);
	}
	if (trace_mask & TRACE_EE) {
		ret = register_trace_drbd_ee(probe_drbd_ee);
		WARN_ON(ret);
	}
	if (trace_mask & TRACE_PACKET) {
		ret = register_trace_drbd_packet(probe_drbd_packet);
		WARN_ON(ret);
	}
	if (trace_mask & TRACE_MD_IO) {
		ret = register_trace_drbd_md_io(probe_drbd_md_io);
		WARN_ON(ret);
	}
	if (trace_mask & TRACE_EPOCH) {
		ret = register_trace_drbd_epoch(probe_drbd_epoch);
		WARN_ON(ret);
	}
	if (trace_mask & TRACE_NL) {
		ret = register_trace_drbd_netlink(probe_drbd_netlink);
		WARN_ON(ret);
	}
	if (trace_mask & TRACE_AL_EXT) {
		ret = register_trace_drbd_actlog(probe_drbd_actlog);
		WARN_ON(ret);
	}
	if (trace_mask & TRACE_RQ) {
		ret = register_trace_drbd_bio(probe_drbd_bio);
		WARN_ON(ret);
	}
	if (trace_mask & TRACE_INT_RQ) {
		ret = register_trace_drbd_req(probe_drbd_req);
		WARN_ON(ret);
	}
	if (trace_mask & TRACE_RESYNC) {
		ret = register_trace__drbd_resync(probe_drbd_resync);
		WARN_ON(ret);
	}
	return 0;
}

module_init(drbd_trace_init);

static void __exit drbd_trace_exit(void)
{
	if (trace_mask & TRACE_UNPLUG)
		unregister_trace_drbd_unplug(probe_drbd_unplug);
	if (trace_mask & TRACE_UUID)
		unregister_trace_drbd_uuid(probe_drbd_uuid);
	if (trace_mask & TRACE_EE)
		unregister_trace_drbd_ee(probe_drbd_ee);
	if (trace_mask & TRACE_PACKET)
		unregister_trace_drbd_packet(probe_drbd_packet);
	if (trace_mask & TRACE_MD_IO)
		unregister_trace_drbd_md_io(probe_drbd_md_io);
	if (trace_mask & TRACE_EPOCH)
		unregister_trace_drbd_epoch(probe_drbd_epoch);
	if (trace_mask & TRACE_NL)
		unregister_trace_drbd_netlink(probe_drbd_netlink);
	if (trace_mask & TRACE_AL_EXT)
		unregister_trace_drbd_actlog(probe_drbd_actlog);
	if (trace_mask & TRACE_RQ)
		unregister_trace_drbd_bio(probe_drbd_bio);
	if (trace_mask & TRACE_INT_RQ)
		unregister_trace_drbd_req(probe_drbd_req);
	if (trace_mask & TRACE_RESYNC)
		unregister_trace__drbd_resync(probe_drbd_resync);

	tracepoint_synchronize_unregister();
}

module_exit(drbd_trace_exit);

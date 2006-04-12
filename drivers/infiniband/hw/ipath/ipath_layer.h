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

#ifndef _IPATH_LAYER_H
#define _IPATH_LAYER_H

/*
 * This header file is for symbols shared between the infinipath driver
 * and drivers layered upon it (such as ipath).
 */

struct sk_buff;
struct ipath_sge_state;
struct ipath_devdata;
struct ether_header;

struct ipath_layer_counters {
	u64 symbol_error_counter;
	u64 link_error_recovery_counter;
	u64 link_downed_counter;
	u64 port_rcv_errors;
	u64 port_rcv_remphys_errors;
	u64 port_xmit_discards;
	u64 port_xmit_data;
	u64 port_rcv_data;
	u64 port_xmit_packets;
	u64 port_rcv_packets;
};

/*
 * A segment is a linear region of low physical memory.
 * XXX Maybe we should use phys addr here and kmap()/kunmap().
 * Used by the verbs layer.
 */
struct ipath_seg {
	void *vaddr;
	size_t length;
};

/* The number of ipath_segs that fit in a page. */
#define IPATH_SEGSZ     (PAGE_SIZE / sizeof (struct ipath_seg))

struct ipath_segarray {
	struct ipath_seg segs[IPATH_SEGSZ];
};

struct ipath_mregion {
	u64 user_base;		/* User's address for this region */
	u64 iova;		/* IB start address of this region */
	size_t length;
	u32 lkey;
	u32 offset;		/* offset (bytes) to start of region */
	int access_flags;
	u32 max_segs;		/* number of ipath_segs in all the arrays */
	u32 mapsz;		/* size of the map array */
	struct ipath_segarray *map[0];	/* the segments */
};

/*
 * These keep track of the copy progress within a memory region.
 * Used by the verbs layer.
 */
struct ipath_sge {
	struct ipath_mregion *mr;
	void *vaddr;		/* current pointer into the segment */
	u32 sge_length;		/* length of the SGE */
	u32 length;		/* remaining length of the segment */
	u16 m;			/* current index: mr->map[m] */
	u16 n;			/* current index: mr->map[m]->segs[n] */
};

struct ipath_sge_state {
	struct ipath_sge *sg_list;	/* next SGE to be used if any */
	struct ipath_sge sge;	/* progress state for the current SGE */
	u8 num_sge;
};

int ipath_layer_register(void *(*l_add)(int, struct ipath_devdata *),
			 void (*l_remove)(void *),
			 int (*l_intr)(void *, u32),
			 int (*l_rcv)(void *, void *,
				      struct sk_buff *),
			 u16 rcv_opcode,
			 int (*l_rcv_lid)(void *, void *));
int ipath_verbs_register(void *(*l_add)(int, struct ipath_devdata *),
			 void (*l_remove)(void *arg),
			 int (*l_piobufavail)(void *arg),
			 void (*l_rcv)(void *arg, void *rhdr,
				       void *data, u32 tlen),
			 void (*l_timer_cb)(void *arg));
void ipath_layer_unregister(void);
void ipath_verbs_unregister(void);
int ipath_layer_open(struct ipath_devdata *, u32 * pktmax);
u16 ipath_layer_get_lid(struct ipath_devdata *dd);
int ipath_layer_get_mac(struct ipath_devdata *dd, u8 *);
u16 ipath_layer_get_bcast(struct ipath_devdata *dd);
u32 ipath_layer_get_cr_errpkey(struct ipath_devdata *dd);
int ipath_layer_set_linkstate(struct ipath_devdata *dd, u8 state);
int ipath_layer_set_mtu(struct ipath_devdata *, u16);
int ipath_set_sps_lid(struct ipath_devdata *, u32, u8);
int ipath_layer_send_hdr(struct ipath_devdata *dd,
			 struct ether_header *hdr);
int ipath_verbs_send(struct ipath_devdata *dd, u32 hdrwords,
		     u32 * hdr, u32 len, struct ipath_sge_state *ss);
int ipath_layer_set_piointbufavail_int(struct ipath_devdata *dd);
int ipath_layer_get_boardname(struct ipath_devdata *dd, char *name,
			      size_t namelen);
int ipath_layer_snapshot_counters(struct ipath_devdata *dd, u64 *swords,
				  u64 *rwords, u64 *spkts, u64 *rpkts,
				  u64 *xmit_wait);
int ipath_layer_get_counters(struct ipath_devdata *dd,
			     struct ipath_layer_counters *cntrs);
int ipath_layer_want_buffer(struct ipath_devdata *dd);
int ipath_layer_set_guid(struct ipath_devdata *, __be64 guid);
__be64 ipath_layer_get_guid(struct ipath_devdata *);
u32 ipath_layer_get_nguid(struct ipath_devdata *);
int ipath_layer_query_device(struct ipath_devdata *, u32 * vendor,
			     u32 * boardrev, u32 * majrev, u32 * minrev);
u32 ipath_layer_get_flags(struct ipath_devdata *dd);
struct device *ipath_layer_get_device(struct ipath_devdata *dd);
u16 ipath_layer_get_deviceid(struct ipath_devdata *dd);
u64 ipath_layer_get_lastibcstat(struct ipath_devdata *dd);
u32 ipath_layer_get_ibmtu(struct ipath_devdata *dd);
int ipath_layer_enable_timer(struct ipath_devdata *dd);
int ipath_layer_disable_timer(struct ipath_devdata *dd);
int ipath_layer_set_verbs_flags(struct ipath_devdata *dd, unsigned flags);
unsigned ipath_layer_get_npkeys(struct ipath_devdata *dd);
unsigned ipath_layer_get_pkey(struct ipath_devdata *dd, unsigned index);
int ipath_layer_get_pkeys(struct ipath_devdata *dd, u16 *pkeys);
int ipath_layer_set_pkeys(struct ipath_devdata *dd, u16 *pkeys);
int ipath_layer_get_linkdowndefaultstate(struct ipath_devdata *dd);
int ipath_layer_set_linkdowndefaultstate(struct ipath_devdata *dd,
					 int sleep);
int ipath_layer_get_phyerrthreshold(struct ipath_devdata *dd);
int ipath_layer_set_phyerrthreshold(struct ipath_devdata *dd, unsigned n);
int ipath_layer_get_overrunthreshold(struct ipath_devdata *dd);
int ipath_layer_set_overrunthreshold(struct ipath_devdata *dd, unsigned n);
u32 ipath_layer_get_rcvhdrentsize(struct ipath_devdata *dd);

/* ipath_ether interrupt values */
#define IPATH_LAYER_INT_IF_UP 0x2
#define IPATH_LAYER_INT_IF_DOWN 0x4
#define IPATH_LAYER_INT_LID 0x8
#define IPATH_LAYER_INT_SEND_CONTINUE 0x10
#define IPATH_LAYER_INT_BCAST 0x40

/* _verbs_layer.l_flags */
#define IPATH_VERBS_KERNEL_SMA 0x1

extern unsigned ipath_debug; /* debugging bit mask */

#endif				/* _IPATH_LAYER_H */

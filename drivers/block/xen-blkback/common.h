/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __XEN_BLKIF__BACKEND__COMMON_H__
#define __XEN_BLKIF__BACKEND__COMMON_H__

#include <linux/version.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/io.h>
#include <asm/setup.h>
#include <asm/pgalloc.h>
#include <asm/hypervisor.h>
#include <xen/grant_table.h>
#include <xen/xenbus.h>
#include <xen/interface/io/ring.h>
#include <xen/interface/io/blkif.h>
#include <xen/interface/io/protocols.h>

#define DRV_PFX "xen-blkback:"
#define DPRINTK(fmt, args...)				\
	pr_debug(DRV_PFX "(%s:%d) " fmt ".\n",	\
		 __func__, __LINE__, ##args)


/* Not a real protocol.  Used to generate ring structs which contain
 * the elements common to all protocols only.  This way we get a
 * compiler-checkable way to use common struct elements, so we can
 * avoid using switch(protocol) in a number of places.  */
struct blkif_common_request {
	char dummy;
};
struct blkif_common_response {
	char dummy;
};

/* i386 protocol version */
#pragma pack(push, 4)
struct blkif_x86_32_request {
	uint8_t        operation;    /* BLKIF_OP_???                         */
	uint8_t        nr_segments;  /* number of segments                   */
	blkif_vdev_t   handle;       /* only for read/write requests         */
	uint64_t       id;           /* private guest value, echoed in resp  */
	blkif_sector_t sector_number;/* start sector idx on disk (r/w only)  */
	struct blkif_request_segment seg[BLKIF_MAX_SEGMENTS_PER_REQUEST];
};
struct blkif_x86_32_response {
	uint64_t        id;              /* copied from request */
	uint8_t         operation;       /* copied from request */
	int16_t         status;          /* BLKIF_RSP_???       */
};
#pragma pack(pop)

/* x86_64 protocol version */
struct blkif_x86_64_request {
	uint8_t        operation;    /* BLKIF_OP_???                         */
	uint8_t        nr_segments;  /* number of segments                   */
	blkif_vdev_t   handle;       /* only for read/write requests         */
	uint64_t       __attribute__((__aligned__(8))) id;
	blkif_sector_t sector_number;/* start sector idx on disk (r/w only)  */
	struct blkif_request_segment seg[BLKIF_MAX_SEGMENTS_PER_REQUEST];
};
struct blkif_x86_64_response {
	uint64_t       __attribute__((__aligned__(8))) id;
	uint8_t         operation;       /* copied from request */
	int16_t         status;          /* BLKIF_RSP_???       */
};

DEFINE_RING_TYPES(blkif_common, struct blkif_common_request,
		  struct blkif_common_response);
DEFINE_RING_TYPES(blkif_x86_32, struct blkif_x86_32_request,
		  struct blkif_x86_32_response);
DEFINE_RING_TYPES(blkif_x86_64, struct blkif_x86_64_request,
		  struct blkif_x86_64_response);

union blkif_back_rings {
	struct blkif_back_ring        native;
	struct blkif_common_back_ring common;
	struct blkif_x86_32_back_ring x86_32;
	struct blkif_x86_64_back_ring x86_64;
};

enum blkif_protocol {
	BLKIF_PROTOCOL_NATIVE = 1,
	BLKIF_PROTOCOL_X86_32 = 2,
	BLKIF_PROTOCOL_X86_64 = 3,
};

struct xen_vbd {
	/* What the domain refers to this vbd as. */
	blkif_vdev_t		handle;
	/* Non-zero -> read-only */
	unsigned char		readonly;
	/* VDISK_xxx */
	unsigned char		type;
	/* phys device that this vbd maps to. */
	u32			pdevice;
	struct block_device	*bdev;
	/* Cached size parameter. */
	sector_t		size;
	bool			flush_support;
};

struct backend_info;

struct xen_blkif {
	/* Unique identifier for this interface. */
	domid_t			domid;
	unsigned int		handle;
	/* Physical parameters of the comms window. */
	unsigned int		irq;
	/* Comms information. */
	enum blkif_protocol	blk_protocol;
	union blkif_back_rings	blk_rings;
	struct vm_struct	*blk_ring_area;
	/* The VBD attached to this interface. */
	struct xen_vbd		vbd;
	/* Back pointer to the backend_info. */
	struct backend_info	*be;
	/* Private fields. */
	spinlock_t		blk_ring_lock;
	atomic_t		refcnt;

	wait_queue_head_t	wq;
	/* One thread per one blkif. */
	struct task_struct	*xenblkd;
	unsigned int		waiting_reqs;

	/* statistics */
	unsigned long		st_print;
	int			st_rd_req;
	int			st_wr_req;
	int			st_oo_req;
	int			st_f_req;
	int			st_rd_sect;
	int			st_wr_sect;

	wait_queue_head_t	waiting_to_free;

	grant_handle_t		shmem_handle;
	grant_ref_t		shmem_ref;
};


#define vbd_sz(_v)	((_v)->bdev->bd_part ? \
			 (_v)->bdev->bd_part->nr_sects : \
			  get_capacity((_v)->bdev->bd_disk))

#define xen_blkif_get(_b) (atomic_inc(&(_b)->refcnt))
#define xen_blkif_put(_b)				\
	do {						\
		if (atomic_dec_and_test(&(_b)->refcnt))	\
			wake_up(&(_b)->waiting_to_free);\
	} while (0)

struct phys_req {
	unsigned short		dev;
	unsigned short		nr_sects;
	struct block_device	*bdev;
	blkif_sector_t		sector_number;
};
int xen_blkif_interface_init(void);

int xen_blkif_xenbus_init(void);

irqreturn_t xen_blkif_be_int(int irq, void *dev_id);
int xen_blkif_schedule(void *arg);

int xen_blkbk_flush_diskcache(struct xenbus_transaction xbt,
			      struct backend_info *be, int state);

struct xenbus_device *xen_blkbk_xenbus(struct backend_info *be);

static inline void blkif_get_x86_32_req(struct blkif_request *dst,
					struct blkif_x86_32_request *src)
{
	int i, n = BLKIF_MAX_SEGMENTS_PER_REQUEST;
	dst->operation = src->operation;
	dst->nr_segments = src->nr_segments;
	dst->handle = src->handle;
	dst->id = src->id;
	dst->u.rw.sector_number = src->sector_number;
	barrier();
	if (n > dst->nr_segments)
		n = dst->nr_segments;
	for (i = 0; i < n; i++)
		dst->u.rw.seg[i] = src->seg[i];
}

static inline void blkif_get_x86_64_req(struct blkif_request *dst,
					struct blkif_x86_64_request *src)
{
	int i, n = BLKIF_MAX_SEGMENTS_PER_REQUEST;
	dst->operation = src->operation;
	dst->nr_segments = src->nr_segments;
	dst->handle = src->handle;
	dst->id = src->id;
	dst->u.rw.sector_number = src->sector_number;
	barrier();
	if (n > dst->nr_segments)
		n = dst->nr_segments;
	for (i = 0; i < n; i++)
		dst->u.rw.seg[i] = src->seg[i];
}

#endif /* __XEN_BLKIF__BACKEND__COMMON_H__ */

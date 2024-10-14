/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _FDMA_API_H_
#define _FDMA_API_H_

#include <linux/bits.h>
#include <linux/etherdevice.h>
#include <linux/types.h>

/* This provides a common set of functions and data structures for interacting
 * with the Frame DMA engine on multiple Microchip switchcores.
 *
 * Frame DMA DCB format:
 *
 * +---------------------------+
 * |         Next Ptr          |
 * +---------------------------+
 * |   Reserved  |    Info     |
 * +---------------------------+
 * |         Data0 Ptr         |
 * +---------------------------+
 * |   Reserved  |    Status0  |
 * +---------------------------+
 * |         Data1 Ptr         |
 * +---------------------------+
 * |   Reserved  |    Status1  |
 * +---------------------------+
 * |         Data2 Ptr         |
 * +---------------------------+
 * |   Reserved  |    Status2  |
 * |-------------|-------------|
 * |                           |
 * |                           |
 * |                           |
 * |                           |
 * |                           |
 * |---------------------------|
 * |         Data14 Ptr        |
 * +-------------|-------------+
 * |   Reserved  |    Status14 |
 * +-------------|-------------+
 *
 * The data pointers points to the actual frame data to be received or sent. The
 * addresses of the data pointers can, as of writing, be either a: DMA address,
 * physical address or mapped address.
 *
 */

#define FDMA_DCB_INFO_DATAL(x)		((x) & GENMASK(15, 0))
#define FDMA_DCB_INFO_TOKEN		BIT(17)
#define FDMA_DCB_INFO_INTR		BIT(18)
#define FDMA_DCB_INFO_SW(x)		(((x) << 24) & GENMASK(31, 24))

#define FDMA_DCB_STATUS_BLOCKL(x)	((x) & GENMASK(15, 0))
#define FDMA_DCB_STATUS_SOF		BIT(16)
#define FDMA_DCB_STATUS_EOF		BIT(17)
#define FDMA_DCB_STATUS_INTR		BIT(18)
#define FDMA_DCB_STATUS_DONE		BIT(19)
#define FDMA_DCB_STATUS_BLOCKO(x)	(((x) << 20) & GENMASK(31, 20))
#define FDMA_DCB_INVALID_DATA		0x1

#define FDMA_DB_MAX			15 /* Max number of DB's on Sparx5 */

struct fdma;

struct fdma_db {
	u64 dataptr;
	u64 status;
};

struct fdma_dcb {
	u64 nextptr;
	u64 info;
	struct fdma_db db[FDMA_DB_MAX];
};

struct fdma_ops {
	/* User-provided callback to set the dataptr */
	int (*dataptr_cb)(struct fdma *fdma, int dcb_idx, int db_idx, u64 *ptr);
	/* User-provided callback to set the nextptr */
	int (*nextptr_cb)(struct fdma *fdma, int dcb_idx, u64 *ptr);
};

struct fdma {
	void *priv;

	/* Virtual addresses */
	struct fdma_dcb *dcbs;
	struct fdma_dcb *last_dcb;

	/* DMA address */
	dma_addr_t dma;

	/* Size of DCB + DB memory */
	int size;

	/* Indexes used to access the next-to-be-used DCB or DB */
	int db_index;
	int dcb_index;

	/* Number of DCB's and DB's */
	u32 n_dcbs;
	u32 n_dbs;

	/* Size of DB's */
	u32 db_size;

	/* Channel id this FDMA object operates on */
	u32 channel_id;

	struct fdma_ops ops;
};

/* Advance the DCB index and wrap if required. */
static inline void fdma_dcb_advance(struct fdma *fdma)
{
	fdma->dcb_index++;
	if (fdma->dcb_index >= fdma->n_dcbs)
		fdma->dcb_index = 0;
}

/* Advance the DB index. */
static inline void fdma_db_advance(struct fdma *fdma)
{
	fdma->db_index++;
}

/* Reset the db index to zero. */
static inline void fdma_db_reset(struct fdma *fdma)
{
	fdma->db_index = 0;
}

/* Check if a DCB can be reused in case of multiple DB's per DCB. */
static inline bool fdma_dcb_is_reusable(struct fdma *fdma)
{
	return fdma->db_index != fdma->n_dbs;
}

/* Check if the FDMA has marked this DB as done. */
static inline bool fdma_db_is_done(struct fdma_db *db)
{
	return db->status & FDMA_DCB_STATUS_DONE;
}

/* Get the length of a DB. */
static inline int fdma_db_len_get(struct fdma_db *db)
{
	return FDMA_DCB_STATUS_BLOCKL(db->status);
}

/* Set the length of a DB. */
static inline void fdma_dcb_len_set(struct fdma_dcb *dcb, u32 len)
{
	dcb->info = FDMA_DCB_INFO_DATAL(len);
}

/* Get a DB by index. */
static inline struct fdma_db *fdma_db_get(struct fdma *fdma, int dcb_idx,
					  int db_idx)
{
	return &fdma->dcbs[dcb_idx].db[db_idx];
}

/* Get the next DB. */
static inline struct fdma_db *fdma_db_next_get(struct fdma *fdma)
{
	return fdma_db_get(fdma, fdma->dcb_index, fdma->db_index);
}

/* Get a DCB by index. */
static inline struct fdma_dcb *fdma_dcb_get(struct fdma *fdma, int dcb_idx)
{
	return &fdma->dcbs[dcb_idx];
}

/* Get the next DCB. */
static inline struct fdma_dcb *fdma_dcb_next_get(struct fdma *fdma)
{
	return fdma_dcb_get(fdma, fdma->dcb_index);
}

/* Check if the FDMA has frames ready for extraction. */
static inline bool fdma_has_frames(struct fdma *fdma)
{
	return fdma_db_is_done(fdma_db_next_get(fdma));
}

/* Get a nextptr by index */
static inline int fdma_nextptr_cb(struct fdma *fdma, int dcb_idx, u64 *nextptr)
{
	*nextptr = fdma->dma + (sizeof(struct fdma_dcb) * dcb_idx);
	return 0;
}

/* Get the DMA address of a dataptr, by index. This function is only applicable
 * if the dataptr addresses and DCB's are in contiguous memory and the driver
 * supports XDP.
 */
static inline u64 fdma_dataptr_get_contiguous(struct fdma *fdma, int dcb_idx,
					      int db_idx)
{
	return fdma->dma + (sizeof(struct fdma_dcb) * fdma->n_dcbs) +
	       (dcb_idx * fdma->n_dbs + db_idx) * fdma->db_size +
	       XDP_PACKET_HEADROOM;
}

/* Get the virtual address of a dataptr, by index. This function is only
 * applicable if the dataptr addresses and DCB's are in contiguous memory and
 * the driver supports XDP.
 */
static inline void *fdma_dataptr_virt_get_contiguous(struct fdma *fdma,
						     int dcb_idx, int db_idx)
{
	return (u8 *)fdma->dcbs + (sizeof(struct fdma_dcb) * fdma->n_dcbs) +
	       (dcb_idx * fdma->n_dbs + db_idx) * fdma->db_size +
	       XDP_PACKET_HEADROOM;
}

/* Check if this DCB is the last used DCB. */
static inline bool fdma_is_last(struct fdma *fdma, struct fdma_dcb *dcb)
{
	return dcb == fdma->last_dcb;
}

int fdma_dcbs_init(struct fdma *fdma, u64 info, u64 status);
int fdma_db_add(struct fdma *fdma, int dcb_idx, int db_idx, u64 status);
int fdma_dcb_add(struct fdma *fdma, int dcb_idx, u64 info, u64 status);
int __fdma_dcb_add(struct fdma *fdma, int dcb_idx, u64 info, u64 status,
		   int (*dcb_cb)(struct fdma *fdma, int dcb_idx, u64 *nextptr),
		   int (*db_cb)(struct fdma *fdma, int dcb_idx, int db_idx,
				u64 *dataptr));

int fdma_alloc_coherent(struct device *dev, struct fdma *fdma);
int fdma_alloc_phys(struct fdma *fdma);

void fdma_free_coherent(struct device *dev, struct fdma *fdma);
void fdma_free_phys(struct fdma *fdma);

u32 fdma_get_size(struct fdma *fdma);
u32 fdma_get_size_contiguous(struct fdma *fdma);

#endif

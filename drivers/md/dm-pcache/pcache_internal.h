/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _PCACHE_INTERNAL_H
#define _PCACHE_INTERNAL_H

#include <linux/delay.h>
#include <linux/crc32c.h>

#define pcache_err(fmt, ...)							\
	pr_err("dm-pcache: %s:%u " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define pcache_info(fmt, ...)							\
	pr_info("dm-pcache: %s:%u " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define pcache_debug(fmt, ...)							\
	pr_debug("dm-pcache: %s:%u " fmt, __func__, __LINE__, ##__VA_ARGS__)

#define PCACHE_KB			(1024ULL)
#define PCACHE_MB			(1024 * PCACHE_KB)

/* Maximum number of metadata indices */
#define PCACHE_META_INDEX_MAX		2

#define PCACHE_CRC_SEED			0x3B15A
/*
 * struct pcache_meta_header - PCACHE metadata header structure
 * @crc: CRC checksum for validating metadata integrity.
 * @seq: Sequence number to track metadata updates.
 * @version: Metadata version.
 * @res: Reserved space for future use.
 */
struct pcache_meta_header {
	__u32 crc;
	__u8  seq;
	__u8  version;
	__u16 res;
};

/*
 * pcache_meta_crc - Calculate CRC for the given metadata header.
 * @header: Pointer to the metadata header.
 * @meta_size: Size of the metadata structure.
 *
 * Returns the CRC checksum calculated by excluding the CRC field itself.
 */
static inline u32 pcache_meta_crc(struct pcache_meta_header *header, u32 meta_size)
{
	return crc32c(PCACHE_CRC_SEED, (void *)header + 4, meta_size - 4);
}

/*
 * pcache_meta_seq_after - Check if a sequence number is more recent, accounting for overflow.
 * @seq1: First sequence number.
 * @seq2: Second sequence number.
 *
 * Determines if @seq1 is more recent than @seq2 by calculating the signed
 * difference between them. This approach allows handling sequence number
 * overflow correctly because the difference wraps naturally, and any value
 * greater than zero indicates that @seq1 is "after" @seq2. This method
 * assumes 8-bit unsigned sequence numbers, where the difference wraps
 * around if seq1 overflows past seq2.
 *
 * Returns:
 *   - true if @seq1 is more recent than @seq2, indicating it comes "after"
 *   - false otherwise.
 */
static inline bool pcache_meta_seq_after(u8 seq1, u8 seq2)
{
	return (s8)(seq1 - seq2) > 0;
}

/*
 * pcache_meta_find_latest - Find the latest valid metadata.
 * @header: Pointer to the metadata header.
 * @meta_size: Size of each metadata block.
 *
 * Finds the latest valid metadata by checking sequence numbers. If a
 * valid entry with the highest sequence number is found, its pointer
 * is returned. Returns NULL if no valid metadata is found.
 */
static inline void __must_check *pcache_meta_find_latest(struct pcache_meta_header *header,
					u32 meta_size, u32 meta_max_size,
					void *meta_ret)
{
	struct pcache_meta_header *meta, *latest = NULL;
	u32 i, seq_latest = 0;
	void *meta_addr;

	meta = meta_ret;

	for (i = 0; i < PCACHE_META_INDEX_MAX; i++) {
		meta_addr = (void *)header + (i * meta_max_size);
		if (copy_mc_to_kernel(meta, meta_addr, meta_size)) {
			pcache_err("hardware memory error when copy meta");
			return ERR_PTR(-EIO);
		}

		/* Skip if CRC check fails, which means corrupted */
		if (meta->crc != pcache_meta_crc(meta, meta_size))
			continue;

		/* Update latest if a more recent sequence is found */
		if (!latest || pcache_meta_seq_after(meta->seq, seq_latest)) {
			seq_latest = meta->seq;
			latest = meta_addr;
		}
	}

	if (!latest)
		return NULL;

	if (copy_mc_to_kernel(meta_ret, latest, meta_size)) {
		pcache_err("hardware memory error");
		return ERR_PTR(-EIO);
	}

	return latest;
}

#endif /* _PCACHE_INTERNAL_H */

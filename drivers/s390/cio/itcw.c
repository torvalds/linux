// SPDX-License-Identifier: GPL-2.0
/*
 *  Functions for incremental construction of fcx enabled I/O control blocks.
 *
 *    Copyright IBM Corp. 2008
 *    Author(s): Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <asm/fcx.h>
#include <asm/itcw.h>

/*
 * struct itcw - incremental tcw helper data type
 *
 * This structure serves as a handle for the incremental construction of a
 * tcw and associated tccb, tsb, data tidaw-list plus an optional interrogate
 * tcw and associated data. The data structures are contained inside a single
 * contiguous buffer provided by the user.
 *
 * The itcw construction functions take care of overall data integrity:
 * - reset unused fields to zero
 * - fill in required pointers
 * - ensure required alignment for data structures
 * - prevent data structures to cross 4k-byte boundary where required
 * - calculate tccb-related length fields
 * - optionally provide ready-made interrogate tcw and associated structures
 *
 * Restrictions apply to the itcws created with these construction functions:
 * - tida only supported for data address, not for tccb
 * - only contiguous tidaw-lists (no ttic)
 * - total number of bytes required per itcw may not exceed 4k bytes
 * - either read or write operation (may not work with r=0 and w=0)
 *
 * Example:
 * struct itcw *itcw;
 * void *buffer;
 * size_t size;
 *
 * size = itcw_calc_size(1, 2, 0);
 * buffer = kmalloc(size, GFP_KERNEL | GFP_DMA);
 * if (!buffer)
 *	return -ENOMEM;
 * itcw = itcw_init(buffer, size, ITCW_OP_READ, 1, 2, 0);
 * if (IS_ERR(itcw))
 *	return PTR_ER(itcw);
 * itcw_add_dcw(itcw, 0x2, 0, NULL, 0, 72);
 * itcw_add_tidaw(itcw, 0, 0x30000, 20);
 * itcw_add_tidaw(itcw, 0, 0x40000, 52);
 * itcw_finalize(itcw);
 *
 */
struct itcw {
	struct tcw *tcw;
	struct tcw *intrg_tcw;
	int num_tidaws;
	int max_tidaws;
	int intrg_num_tidaws;
	int intrg_max_tidaws;
};

/**
 * itcw_get_tcw - return pointer to tcw associated with the itcw
 * @itcw: address of the itcw
 *
 * Return pointer to the tcw associated with the itcw.
 */
struct tcw *itcw_get_tcw(struct itcw *itcw)
{
	return itcw->tcw;
}
EXPORT_SYMBOL(itcw_get_tcw);

/**
 * itcw_calc_size - return the size of an itcw with the given parameters
 * @intrg: if non-zero, add an interrogate tcw
 * @max_tidaws: maximum number of tidaws to be used for data addressing or zero
 * if no tida is to be used.
 * @intrg_max_tidaws: maximum number of tidaws to be used for data addressing
 * by the interrogate tcw, if specified
 *
 * Calculate and return the number of bytes required to hold an itcw with the
 * given parameters and assuming tccbs with maximum size.
 *
 * Note that the resulting size also contains bytes needed for alignment
 * padding as well as padding to ensure that data structures don't cross a
 * 4k-boundary where required.
 */
size_t itcw_calc_size(int intrg, int max_tidaws, int intrg_max_tidaws)
{
	size_t len;
	int cross_count;

	/* Main data. */
	len = sizeof(struct itcw);
	len += /* TCW */ sizeof(struct tcw) + /* TCCB */ TCCB_MAX_SIZE +
	       /* TSB */ sizeof(struct tsb) +
	       /* TIDAL */ max_tidaws * sizeof(struct tidaw);
	/* Interrogate data. */
	if (intrg) {
		len += /* TCW */ sizeof(struct tcw) + /* TCCB */ TCCB_MAX_SIZE +
		       /* TSB */ sizeof(struct tsb) +
		       /* TIDAL */ intrg_max_tidaws * sizeof(struct tidaw);
	}

	/* Maximum required alignment padding. */
	len += /* Initial TCW */ 63 + /* Interrogate TCCB */ 7;

	/* TIDAW lists may not cross a 4k boundary. To cross a
	 * boundary we need to add a TTIC TIDAW. We need to reserve
	 * one additional TIDAW for a TTIC that we may need to add due
	 * to the placement of the data chunk in memory, and a further
	 * TIDAW for each page boundary that the TIDAW list may cross
	 * due to it's own size.
	 */
	if (max_tidaws) {
		cross_count = 1 + ((max_tidaws * sizeof(struct tidaw) - 1)
				   >> PAGE_SHIFT);
		len += cross_count * sizeof(struct tidaw);
	}
	if (intrg_max_tidaws) {
		cross_count = 1 + ((intrg_max_tidaws * sizeof(struct tidaw) - 1)
				   >> PAGE_SHIFT);
		len += cross_count * sizeof(struct tidaw);
	}
	return len;
}
EXPORT_SYMBOL(itcw_calc_size);

#define CROSS4K(x, l)	(((x) & ~4095) != ((x + l) & ~4095))

static inline void *fit_chunk(addr_t *start, addr_t end, size_t len,
			      int align, int check_4k)
{
	addr_t addr;

	addr = ALIGN(*start, align);
	if (check_4k && CROSS4K(addr, len)) {
		addr = ALIGN(addr, 4096);
		addr = ALIGN(addr, align);
	}
	if (addr + len > end)
		return ERR_PTR(-ENOSPC);
	*start = addr + len;
	return (void *) addr;
}

/**
 * itcw_init - initialize incremental tcw data structure
 * @buffer: address of buffer to use for data structures
 * @size: number of bytes in buffer
 * @op: %ITCW_OP_READ for a read operation tcw, %ITCW_OP_WRITE for a write
 * operation tcw
 * @intrg: if non-zero, add and initialize an interrogate tcw
 * @max_tidaws: maximum number of tidaws to be used for data addressing or zero
 * if no tida is to be used.
 * @intrg_max_tidaws: maximum number of tidaws to be used for data addressing
 * by the interrogate tcw, if specified
 *
 * Prepare the specified buffer to be used as an incremental tcw, i.e. a
 * helper data structure that can be used to construct a valid tcw by
 * successive calls to other helper functions. Note: the buffer needs to be
 * located below the 2G address limit. The resulting tcw has the following
 * restrictions:
 *  - no tccb tidal
 *  - input/output tidal is contiguous (no ttic)
 *  - total data should not exceed 4k
 *  - tcw specifies either read or write operation
 *
 * On success, return pointer to the resulting incremental tcw data structure,
 * ERR_PTR otherwise.
 */
struct itcw *itcw_init(void *buffer, size_t size, int op, int intrg,
		       int max_tidaws, int intrg_max_tidaws)
{
	struct itcw *itcw;
	void *chunk;
	addr_t start;
	addr_t end;
	int cross_count;

	/* Check for 2G limit. */
	start = (addr_t) buffer;
	end = start + size;
	if ((virt_to_phys(buffer) + size) > (1 << 31))
		return ERR_PTR(-EINVAL);
	memset(buffer, 0, size);
	/* ITCW. */
	chunk = fit_chunk(&start, end, sizeof(struct itcw), 1, 0);
	if (IS_ERR(chunk))
		return chunk;
	itcw = chunk;
	/* allow for TTIC tidaws that may be needed to cross a page boundary */
	cross_count = 0;
	if (max_tidaws)
		cross_count = 1 + ((max_tidaws * sizeof(struct tidaw) - 1)
				   >> PAGE_SHIFT);
	itcw->max_tidaws = max_tidaws + cross_count;
	cross_count = 0;
	if (intrg_max_tidaws)
		cross_count = 1 + ((intrg_max_tidaws * sizeof(struct tidaw) - 1)
				   >> PAGE_SHIFT);
	itcw->intrg_max_tidaws = intrg_max_tidaws + cross_count;
	/* Main TCW. */
	chunk = fit_chunk(&start, end, sizeof(struct tcw), 64, 0);
	if (IS_ERR(chunk))
		return chunk;
	itcw->tcw = chunk;
	tcw_init(itcw->tcw, (op == ITCW_OP_READ) ? 1 : 0,
		 (op == ITCW_OP_WRITE) ? 1 : 0);
	/* Interrogate TCW. */
	if (intrg) {
		chunk = fit_chunk(&start, end, sizeof(struct tcw), 64, 0);
		if (IS_ERR(chunk))
			return chunk;
		itcw->intrg_tcw = chunk;
		tcw_init(itcw->intrg_tcw, 1, 0);
		tcw_set_intrg(itcw->tcw, itcw->intrg_tcw);
	}
	/* Data TIDAL. */
	if (max_tidaws > 0) {
		chunk = fit_chunk(&start, end, sizeof(struct tidaw) *
				  itcw->max_tidaws, 16, 0);
		if (IS_ERR(chunk))
			return chunk;
		tcw_set_data(itcw->tcw, chunk, 1);
	}
	/* Interrogate data TIDAL. */
	if (intrg && (intrg_max_tidaws > 0)) {
		chunk = fit_chunk(&start, end, sizeof(struct tidaw) *
				  itcw->intrg_max_tidaws, 16, 0);
		if (IS_ERR(chunk))
			return chunk;
		tcw_set_data(itcw->intrg_tcw, chunk, 1);
	}
	/* TSB. */
	chunk = fit_chunk(&start, end, sizeof(struct tsb), 8, 0);
	if (IS_ERR(chunk))
		return chunk;
	tsb_init(chunk);
	tcw_set_tsb(itcw->tcw, chunk);
	/* Interrogate TSB. */
	if (intrg) {
		chunk = fit_chunk(&start, end, sizeof(struct tsb), 8, 0);
		if (IS_ERR(chunk))
			return chunk;
		tsb_init(chunk);
		tcw_set_tsb(itcw->intrg_tcw, chunk);
	}
	/* TCCB. */
	chunk = fit_chunk(&start, end, TCCB_MAX_SIZE, 8, 0);
	if (IS_ERR(chunk))
		return chunk;
	tccb_init(chunk, TCCB_MAX_SIZE, TCCB_SAC_DEFAULT);
	tcw_set_tccb(itcw->tcw, chunk);
	/* Interrogate TCCB. */
	if (intrg) {
		chunk = fit_chunk(&start, end, TCCB_MAX_SIZE, 8, 0);
		if (IS_ERR(chunk))
			return chunk;
		tccb_init(chunk, TCCB_MAX_SIZE, TCCB_SAC_INTRG);
		tcw_set_tccb(itcw->intrg_tcw, chunk);
		tccb_add_dcw(chunk, TCCB_MAX_SIZE, DCW_CMD_INTRG, 0, NULL,
			     sizeof(struct dcw_intrg_data), 0);
		tcw_finalize(itcw->intrg_tcw, 0);
	}
	return itcw;
}
EXPORT_SYMBOL(itcw_init);

/**
 * itcw_add_dcw - add a dcw to the itcw
 * @itcw: address of the itcw
 * @cmd: the dcw command
 * @flags: flags for the dcw
 * @cd: address of control data for this dcw or NULL if none is required
 * @cd_count: number of control data bytes for this dcw
 * @count: number of data bytes for this dcw
 *
 * Add a new dcw to the specified itcw by writing the dcw information specified
 * by @cmd, @flags, @cd, @cd_count and @count to the tca of the tccb. Return
 * a pointer to the newly added dcw on success or -%ENOSPC if the new dcw
 * would exceed the available space.
 *
 * Note: the tcal field of the tccb header will be updated to reflect added
 * content.
 */
struct dcw *itcw_add_dcw(struct itcw *itcw, u8 cmd, u8 flags, void *cd,
			 u8 cd_count, u32 count)
{
	return tccb_add_dcw(tcw_get_tccb(itcw->tcw), TCCB_MAX_SIZE, cmd,
			    flags, cd, cd_count, count);
}
EXPORT_SYMBOL(itcw_add_dcw);

/**
 * itcw_add_tidaw - add a tidaw to the itcw
 * @itcw: address of the itcw
 * @flags: flags for the new tidaw
 * @addr: address value for the new tidaw
 * @count: count value for the new tidaw
 *
 * Add a new tidaw to the input/output data tidaw-list of the specified itcw
 * (depending on the value of the r-flag and w-flag). Return a pointer to
 * the new tidaw on success or -%ENOSPC if the new tidaw would exceed the
 * available space.
 *
 * Note: TTIC tidaws are automatically added when needed, so explicitly calling
 * this interface with the TTIC flag is not supported. The last-tidaw flag
 * for the last tidaw in the list will be set by itcw_finalize.
 */
struct tidaw *itcw_add_tidaw(struct itcw *itcw, u8 flags, void *addr, u32 count)
{
	struct tidaw *following;

	if (itcw->num_tidaws >= itcw->max_tidaws)
		return ERR_PTR(-ENOSPC);
	/*
	 * Is the tidaw, which follows the one we are about to fill, on the next
	 * page? Then we have to insert a TTIC tidaw first, that points to the
	 * tidaw on the new page.
	 */
	following = ((struct tidaw *) tcw_get_data(itcw->tcw))
		+ itcw->num_tidaws + 1;
	if (itcw->num_tidaws && !((unsigned long) following & ~PAGE_MASK)) {
		tcw_add_tidaw(itcw->tcw, itcw->num_tidaws++,
			      TIDAW_FLAGS_TTIC, following, 0);
		if (itcw->num_tidaws >= itcw->max_tidaws)
			return ERR_PTR(-ENOSPC);
	}
	return tcw_add_tidaw(itcw->tcw, itcw->num_tidaws++, flags, addr, count);
}
EXPORT_SYMBOL(itcw_add_tidaw);

/**
 * itcw_set_data - set data address and tida flag of the itcw
 * @itcw: address of the itcw
 * @addr: the data address
 * @use_tidal: zero of the data address specifies a contiguous block of data,
 * non-zero if it specifies a list if tidaws.
 *
 * Set the input/output data address of the itcw (depending on the value of the
 * r-flag and w-flag). If @use_tidal is non-zero, the corresponding tida flag
 * is set as well.
 */
void itcw_set_data(struct itcw *itcw, void *addr, int use_tidal)
{
	tcw_set_data(itcw->tcw, addr, use_tidal);
}
EXPORT_SYMBOL(itcw_set_data);

/**
 * itcw_finalize - calculate length and count fields of the itcw
 * @itcw: address of the itcw
 *
 * Calculate tcw input-/output-count and tccbl fields and add a tcat the tccb.
 * In case input- or output-tida is used, the tidaw-list must be stored in
 * continuous storage (no ttic). The tcal field in the tccb must be
 * up-to-date.
 */
void itcw_finalize(struct itcw *itcw)
{
	tcw_finalize(itcw->tcw, itcw->num_tidaws);
}
EXPORT_SYMBOL(itcw_finalize);

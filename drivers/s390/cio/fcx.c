// SPDX-License-Identifier: GPL-2.0
/*
 *  Functions for assembling fcx enabled I/O control blocks.
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
#include "cio.h"

/**
 * tcw_get_intrg - return pointer to associated interrogate tcw
 * @tcw: pointer to the original tcw
 *
 * Return a pointer to the interrogate tcw associated with the specified tcw
 * or %NULL if there is no associated interrogate tcw.
 */
struct tcw *tcw_get_intrg(struct tcw *tcw)
{
	return dma32_to_virt(tcw->intrg);
}
EXPORT_SYMBOL(tcw_get_intrg);

/**
 * tcw_get_data - return pointer to input/output data associated with tcw
 * @tcw: pointer to the tcw
 *
 * Return the input or output data address specified in the tcw depending
 * on whether the r-bit or the w-bit is set. If neither bit is set, return
 * %NULL.
 */
void *tcw_get_data(struct tcw *tcw)
{
	if (tcw->r)
		return dma64_to_virt(tcw->input);
	if (tcw->w)
		return dma64_to_virt(tcw->output);
	return NULL;
}
EXPORT_SYMBOL(tcw_get_data);

/**
 * tcw_get_tccb - return pointer to tccb associated with tcw
 * @tcw: pointer to the tcw
 *
 * Return pointer to the tccb associated with this tcw.
 */
struct tccb *tcw_get_tccb(struct tcw *tcw)
{
	return dma64_to_virt(tcw->tccb);
}
EXPORT_SYMBOL(tcw_get_tccb);

/**
 * tcw_get_tsb - return pointer to tsb associated with tcw
 * @tcw: pointer to the tcw
 *
 * Return pointer to the tsb associated with this tcw.
 */
struct tsb *tcw_get_tsb(struct tcw *tcw)
{
	return dma64_to_virt(tcw->tsb);
}
EXPORT_SYMBOL(tcw_get_tsb);

/**
 * tcw_init - initialize tcw data structure
 * @tcw: pointer to the tcw to be initialized
 * @r: initial value of the r-bit
 * @w: initial value of the w-bit
 *
 * Initialize all fields of the specified tcw data structure with zero and
 * fill in the format, flags, r and w fields.
 */
void tcw_init(struct tcw *tcw, int r, int w)
{
	memset(tcw, 0, sizeof(struct tcw));
	tcw->format = TCW_FORMAT_DEFAULT;
	tcw->flags = TCW_FLAGS_TIDAW_FORMAT(TCW_TIDAW_FORMAT_DEFAULT);
	if (r)
		tcw->r = 1;
	if (w)
		tcw->w = 1;
}
EXPORT_SYMBOL(tcw_init);

static inline size_t tca_size(struct tccb *tccb)
{
	return tccb->tcah.tcal - 12;
}

static u32 calc_dcw_count(struct tccb *tccb)
{
	int offset;
	struct dcw *dcw;
	u32 count = 0;
	size_t size;

	size = tca_size(tccb);
	for (offset = 0; offset < size;) {
		dcw = (struct dcw *) &tccb->tca[offset];
		count += dcw->count;
		if (!(dcw->flags & DCW_FLAGS_CC))
			break;
		offset += sizeof(struct dcw) + ALIGN((int) dcw->cd_count, 4);
	}
	return count;
}

static u32 calc_cbc_size(struct tidaw *tidaw, int num)
{
	int i;
	u32 cbc_data;
	u32 cbc_count = 0;
	u64 data_count = 0;

	for (i = 0; i < num; i++) {
		if (tidaw[i].flags & TIDAW_FLAGS_LAST)
			break;
		/* TODO: find out if padding applies to total of data
		 * transferred or data transferred by this tidaw. Assumption:
		 * applies to total. */
		data_count += tidaw[i].count;
		if (tidaw[i].flags & TIDAW_FLAGS_INSERT_CBC) {
			cbc_data = 4 + ALIGN(data_count, 4) - data_count;
			cbc_count += cbc_data;
			data_count += cbc_data;
		}
	}
	return cbc_count;
}

/**
 * tcw_finalize - finalize tcw length fields and tidaw list
 * @tcw: pointer to the tcw
 * @num_tidaws: the number of tidaws used to address input/output data or zero
 * if no tida is used
 *
 * Calculate the input-/output-count and tccbl field in the tcw, add a
 * tcat the tccb and terminate the data tidaw list if used.
 *
 * Note: in case input- or output-tida is used, the tidaw-list must be stored
 * in contiguous storage (no ttic). The tcal field in the tccb must be
 * up-to-date.
 */
void tcw_finalize(struct tcw *tcw, int num_tidaws)
{
	struct tidaw *tidaw;
	struct tccb *tccb;
	struct tccb_tcat *tcat;
	u32 count;

	/* Terminate tidaw list. */
	tidaw = tcw_get_data(tcw);
	if (num_tidaws > 0)
		tidaw[num_tidaws - 1].flags |= TIDAW_FLAGS_LAST;
	/* Add tcat to tccb. */
	tccb = tcw_get_tccb(tcw);
	tcat = (struct tccb_tcat *) &tccb->tca[tca_size(tccb)];
	memset(tcat, 0, sizeof(*tcat));
	/* Calculate tcw input/output count and tcat transport count. */
	count = calc_dcw_count(tccb);
	if (tcw->w && (tcw->flags & TCW_FLAGS_OUTPUT_TIDA))
		count += calc_cbc_size(tidaw, num_tidaws);
	if (tcw->r)
		tcw->input_count = count;
	else if (tcw->w)
		tcw->output_count = count;
	tcat->count = ALIGN(count, 4) + 4;
	/* Calculate tccbl. */
	tcw->tccbl = (sizeof(struct tccb) + tca_size(tccb) +
		      sizeof(struct tccb_tcat) - 20) >> 2;
}
EXPORT_SYMBOL(tcw_finalize);

/**
 * tcw_set_intrg - set the interrogate tcw address of a tcw
 * @tcw: the tcw address
 * @intrg_tcw: the address of the interrogate tcw
 *
 * Set the address of the interrogate tcw in the specified tcw.
 */
void tcw_set_intrg(struct tcw *tcw, struct tcw *intrg_tcw)
{
	tcw->intrg = virt_to_dma32(intrg_tcw);
}
EXPORT_SYMBOL(tcw_set_intrg);

/**
 * tcw_set_data - set data address and tida flag of a tcw
 * @tcw: the tcw address
 * @data: the data address
 * @use_tidal: zero of the data address specifies a contiguous block of data,
 * non-zero if it specifies a list if tidaws.
 *
 * Set the input/output data address of a tcw (depending on the value of the
 * r-flag and w-flag). If @use_tidal is non-zero, the corresponding tida flag
 * is set as well.
 */
void tcw_set_data(struct tcw *tcw, void *data, int use_tidal)
{
	if (tcw->r) {
		tcw->input = virt_to_dma64(data);
		if (use_tidal)
			tcw->flags |= TCW_FLAGS_INPUT_TIDA;
	} else if (tcw->w) {
		tcw->output = virt_to_dma64(data);
		if (use_tidal)
			tcw->flags |= TCW_FLAGS_OUTPUT_TIDA;
	}
}
EXPORT_SYMBOL(tcw_set_data);

/**
 * tcw_set_tccb - set tccb address of a tcw
 * @tcw: the tcw address
 * @tccb: the tccb address
 *
 * Set the address of the tccb in the specified tcw.
 */
void tcw_set_tccb(struct tcw *tcw, struct tccb *tccb)
{
	tcw->tccb = virt_to_dma64(tccb);
}
EXPORT_SYMBOL(tcw_set_tccb);

/**
 * tcw_set_tsb - set tsb address of a tcw
 * @tcw: the tcw address
 * @tsb: the tsb address
 *
 * Set the address of the tsb in the specified tcw.
 */
void tcw_set_tsb(struct tcw *tcw, struct tsb *tsb)
{
	tcw->tsb = virt_to_dma64(tsb);
}
EXPORT_SYMBOL(tcw_set_tsb);

/**
 * tccb_init - initialize tccb
 * @tccb: the tccb address
 * @size: the maximum size of the tccb
 * @sac: the service-action-code to be user
 *
 * Initialize the header of the specified tccb by resetting all values to zero
 * and filling in defaults for format, sac and initial tcal fields.
 */
void tccb_init(struct tccb *tccb, size_t size, u32 sac)
{
	memset(tccb, 0, size);
	tccb->tcah.format = TCCB_FORMAT_DEFAULT;
	tccb->tcah.sac = sac;
	tccb->tcah.tcal = 12;
}
EXPORT_SYMBOL(tccb_init);

/**
 * tsb_init - initialize tsb
 * @tsb: the tsb address
 *
 * Initialize the specified tsb by resetting all values to zero.
 */
void tsb_init(struct tsb *tsb)
{
	memset(tsb, 0, sizeof(*tsb));
}
EXPORT_SYMBOL(tsb_init);

/**
 * tccb_add_dcw - add a dcw to the tccb
 * @tccb: the tccb address
 * @tccb_size: the maximum tccb size
 * @cmd: the dcw command
 * @flags: flags for the dcw
 * @cd: pointer to control data for this dcw or NULL if none is required
 * @cd_count: number of control data bytes for this dcw
 * @count: number of data bytes for this dcw
 *
 * Add a new dcw to the specified tccb by writing the dcw information specified
 * by @cmd, @flags, @cd, @cd_count and @count to the tca of the tccb. Return
 * a pointer to the newly added dcw on success or -%ENOSPC if the new dcw
 * would exceed the available space as defined by @tccb_size.
 *
 * Note: the tcal field of the tccb header will be updates to reflect added
 * content.
 */
struct dcw *tccb_add_dcw(struct tccb *tccb, size_t tccb_size, u8 cmd, u8 flags,
			 void *cd, u8 cd_count, u32 count)
{
	struct dcw *dcw;
	int size;
	int tca_offset;

	/* Check for space. */
	tca_offset = tca_size(tccb);
	size = ALIGN(sizeof(struct dcw) + cd_count, 4);
	if (sizeof(struct tccb_tcah) + tca_offset + size +
	    sizeof(struct tccb_tcat) > tccb_size)
		return ERR_PTR(-ENOSPC);
	/* Add dcw to tca. */
	dcw = (struct dcw *) &tccb->tca[tca_offset];
	memset(dcw, 0, size);
	dcw->cmd = cmd;
	dcw->flags = flags;
	dcw->count = count;
	dcw->cd_count = cd_count;
	if (cd)
		memcpy(&dcw->cd[0], cd, cd_count);
	tccb->tcah.tcal += size;
	return dcw;
}
EXPORT_SYMBOL(tccb_add_dcw);

/**
 * tcw_add_tidaw - add a tidaw to a tcw
 * @tcw: the tcw address
 * @num_tidaws: the current number of tidaws
 * @flags: flags for the new tidaw
 * @addr: address value for the new tidaw
 * @count: count value for the new tidaw
 *
 * Add a new tidaw to the input/output data tidaw-list of the specified tcw
 * (depending on the value of the r-flag and w-flag) and return a pointer to
 * the new tidaw.
 *
 * Note: the tidaw-list is assumed to be contiguous with no ttics. The caller
 * must ensure that there is enough space for the new tidaw. The last-tidaw
 * flag for the last tidaw in the list will be set by tcw_finalize.
 */
struct tidaw *tcw_add_tidaw(struct tcw *tcw, int num_tidaws, u8 flags,
			    void *addr, u32 count)
{
	struct tidaw *tidaw;

	/* Add tidaw to tidaw-list. */
	tidaw = ((struct tidaw *) tcw_get_data(tcw)) + num_tidaws;
	memset(tidaw, 0, sizeof(struct tidaw));
	tidaw->flags = flags;
	tidaw->count = count;
	tidaw->addr = virt_to_dma64(addr);
	return tidaw;
}
EXPORT_SYMBOL(tcw_add_tidaw);

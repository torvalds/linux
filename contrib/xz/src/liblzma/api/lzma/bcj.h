/**
 * \file        lzma/bcj.h
 * \brief       Branch/Call/Jump conversion filters
 */

/*
 * Author: Lasse Collin
 *
 * This file has been put into the public domain.
 * You can do whatever you want with this file.
 *
 * See ../lzma.h for information about liblzma as a whole.
 */

#ifndef LZMA_H_INTERNAL
#	error Never include this file directly. Use <lzma.h> instead.
#endif


/* Filter IDs for lzma_filter.id */

#define LZMA_FILTER_X86         LZMA_VLI_C(0x04)
	/**<
	 * Filter for x86 binaries
	 */

#define LZMA_FILTER_POWERPC     LZMA_VLI_C(0x05)
	/**<
	 * Filter for Big endian PowerPC binaries
	 */

#define LZMA_FILTER_IA64        LZMA_VLI_C(0x06)
	/**<
	 * Filter for IA-64 (Itanium) binaries.
	 */

#define LZMA_FILTER_ARM         LZMA_VLI_C(0x07)
	/**<
	 * Filter for ARM binaries.
	 */

#define LZMA_FILTER_ARMTHUMB    LZMA_VLI_C(0x08)
	/**<
	 * Filter for ARM-Thumb binaries.
	 */

#define LZMA_FILTER_SPARC       LZMA_VLI_C(0x09)
	/**<
	 * Filter for SPARC binaries.
	 */


/**
 * \brief       Options for BCJ filters
 *
 * The BCJ filters never change the size of the data. Specifying options
 * for them is optional: if pointer to options is NULL, default value is
 * used. You probably never need to specify options to BCJ filters, so just
 * set the options pointer to NULL and be happy.
 *
 * If options with non-default values have been specified when encoding,
 * the same options must also be specified when decoding.
 *
 * \note        At the moment, none of the BCJ filters support
 *              LZMA_SYNC_FLUSH. If LZMA_SYNC_FLUSH is specified,
 *              LZMA_OPTIONS_ERROR will be returned. If there is need,
 *              partial support for LZMA_SYNC_FLUSH can be added in future.
 *              Partial means that flushing would be possible only at
 *              offsets that are multiple of 2, 4, or 16 depending on
 *              the filter, except x86 which cannot be made to support
 *              LZMA_SYNC_FLUSH predictably.
 */
typedef struct {
	/**
	 * \brief       Start offset for conversions
	 *
	 * This setting is useful only when the same filter is used
	 * _separately_ for multiple sections of the same executable file,
	 * and the sections contain cross-section branch/call/jump
	 * instructions. In that case it is beneficial to set the start
	 * offset of the non-first sections so that the relative addresses
	 * of the cross-section branch/call/jump instructions will use the
	 * same absolute addresses as in the first section.
	 *
	 * When the pointer to options is NULL, the default value (zero)
	 * is used.
	 */
	uint32_t start_offset;

} lzma_options_bcj;

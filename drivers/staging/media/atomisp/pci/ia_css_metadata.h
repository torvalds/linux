/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_METADATA_H
#define __IA_CSS_METADATA_H

/* @file
 * This file contains structure for processing sensor metadata.
 */

#include <linux/build_bug.h>

#include <type_support.h>
#include "ia_css_types.h"
#include "ia_css_stream_format.h"

/* Metadata configuration. This data structure contains necessary info
 *  to process sensor metadata.
 */
struct ia_css_metadata_config {
	enum atomisp_input_format data_type; /** Data type of CSI-2 embedded
			data. The default value is ATOMISP_INPUT_FORMAT_EMBEDDED. For
			certain sensors, user can choose non-default data type for embedded
			data. */
	struct ia_css_resolution  resolution; /** Resolution */
};

struct ia_css_metadata_info {
	struct ia_css_resolution resolution; /** Resolution */
	u32                 stride;     /** Stride in bytes */
	u32                 size;       /** Total size in bytes */
};

struct ia_css_metadata {
	struct ia_css_metadata_info info;    /** Layout info */
	ia_css_ptr		    address; /** CSS virtual address */
	u32		    exp_id;
	/** Exposure ID, see ia_css_event_public.h for more detail */
};

#define SIZE_OF_IA_CSS_METADATA_STRUCT sizeof(struct ia_css_metadata)

static_assert(sizeof(struct ia_css_metadata) == SIZE_OF_IA_CSS_METADATA_STRUCT);

/* @brief Allocate a metadata buffer.
 * @param[in]   metadata_info Metadata info struct, contains details on metadata buffers.
 * @return      Pointer of metadata buffer or NULL (if error)
 *
 * This function allocates a metadata buffer according to the properties
 * specified in the metadata_info struct.
 */
struct ia_css_metadata *
ia_css_metadata_allocate(const struct ia_css_metadata_info *metadata_info);

/* @brief Free a metadata buffer.
 *
 * @param[in]	metadata	Pointer of metadata buffer.
 * @return	None
 *
 * This function frees a metadata buffer.
 */
void
ia_css_metadata_free(struct ia_css_metadata *metadata);

#endif /* __IA_CSS_METADATA_H */

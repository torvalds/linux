/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#ifndef __IA_CSS_TAGGER_COMMON_H__
#define __IA_CSS_TAGGER_COMMON_H__

#include <system_local.h>
#include <type_support.h>

/**
 * @brief The tagger's circular buffer.
 *
 * Should be one less than NUM_CONTINUOUS_FRAMES in sh_css_internal.h
 */
#define MAX_CB_ELEMS_FOR_TAGGER 14

/**
 * @brief Data structure for the tagger buffer element.
 */
typedef struct {
	u32 frame;	/* the frame value stored in the element */
	u32 param;	/* the param value stored in the element */
	u8 mark;	/* the mark on the element */
	u8 lock;	/* the lock on the element */
	u8 exp_id; /* exp_id of frame, for debugging only */
} ia_css_tagger_buf_sp_elem_t;

#endif /* __IA_CSS_TAGGER_COMMON_H__ */

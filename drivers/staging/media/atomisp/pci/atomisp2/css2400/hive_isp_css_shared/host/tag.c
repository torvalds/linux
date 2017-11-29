/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "tag.h"
#include <platform_support.h>	/* NULL */
#include <assert_support.h>
#include "tag_local.h"

/*
 * @brief	Creates the tag description from the given parameters.
 * @param[in]	num_captures
 * @param[in]	skip
 * @param[in]	offset
 * @param[out]	tag_descr
 */
void
sh_css_create_tag_descr(int num_captures,
			unsigned int skip,
			int offset,
			unsigned int exp_id,
			struct sh_css_tag_descr *tag_descr)
{
	assert(tag_descr != NULL);

	tag_descr->num_captures = num_captures;
	tag_descr->skip		= skip;
	tag_descr->offset	= offset;
	tag_descr->exp_id	= exp_id;
}

/*
 * @brief	Encodes the members of tag description into a 32-bit value.
 * @param[in]	tag		Pointer to the tag description
 * @return	(unsigned int)	Encoded 32-bit tag-info
 */
unsigned int
sh_css_encode_tag_descr(struct sh_css_tag_descr *tag)
{
	int num_captures;
	unsigned int num_captures_sign;
	unsigned int skip;
	int offset;
	unsigned int offset_sign;
	unsigned int exp_id;
	unsigned int encoded_tag;

	assert(tag != NULL);

	if (tag->num_captures < 0) {
		num_captures = -tag->num_captures;
		num_captures_sign = 1;
	} else {
		num_captures = tag->num_captures;
		num_captures_sign = 0;
	}
	skip = tag->skip;
	if (tag->offset < 0) {
		offset = -tag->offset;
		offset_sign = 1;
	} else {
		offset = tag->offset;
		offset_sign = 0;
	}
	exp_id = tag->exp_id;

	if (exp_id != 0)
	{
		/* we encode either an exp_id or capture data */
		assert((num_captures == 0) && (skip == 0) && (offset == 0));

		encoded_tag = TAG_EXP | (exp_id & 0xFF) << TAG_EXP_ID_SHIFT;
	}
	else
	{
		encoded_tag = TAG_CAP 
				| ((num_captures_sign & 0x00000001) << TAG_NUM_CAPTURES_SIGN_SHIFT)
				| ((offset_sign       & 0x00000001) << TAG_OFFSET_SIGN_SHIFT)
				| ((num_captures      & 0x000000FF) << TAG_NUM_CAPTURES_SHIFT)
				| ((skip              & 0x000000FF) << TAG_OFFSET_SHIFT)
				| ((offset            & 0x000000FF) << TAG_SKIP_SHIFT);

	}
	return encoded_tag;
}

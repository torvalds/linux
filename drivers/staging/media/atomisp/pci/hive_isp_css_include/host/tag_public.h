/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __TAG_PUBLIC_H_INCLUDED__
#define __TAG_PUBLIC_H_INCLUDED__

/**
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
			struct sh_css_tag_descr *tag_descr);

/**
 * @brief	Encodes the members of tag description into a 32-bit value.
 * @param[in]	tag		Pointer to the tag description
 * @return	(unsigned int)	Encoded 32-bit tag-info
 */
unsigned int
sh_css_encode_tag_descr(struct sh_css_tag_descr *tag);

#endif /* __TAG_PUBLIC_H_INCLUDED__ */

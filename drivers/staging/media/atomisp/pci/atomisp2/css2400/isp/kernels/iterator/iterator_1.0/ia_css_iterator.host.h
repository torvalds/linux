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

#ifndef __IA_CSS_ITERATOR_HOST_H
#define __IA_CSS_ITERATOR_HOST_H

#include "ia_css_frame_public.h"
#include "ia_css_binary.h"
#include "ia_css_err.h"
#include "ia_css_iterator_param.h"

void
ia_css_iterator_config(
	struct sh_css_isp_iterator_isp_config *to,
	const struct ia_css_iterator_configuration *from,
	unsigned size);

enum ia_css_err
ia_css_iterator_configure(
	const struct ia_css_binary *binary,
	const struct ia_css_frame_info *in_info);

#endif /* __IA_CSS_ITERATOR_HOST_H */

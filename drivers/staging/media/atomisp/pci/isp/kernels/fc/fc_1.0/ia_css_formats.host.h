/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_FORMATS_HOST_H
#define __IA_CSS_FORMATS_HOST_H

#include "ia_css_formats_types.h"
#include "ia_css_formats_param.h"

extern const struct ia_css_formats_config default_formats_config;

void
ia_css_formats_encode(
    struct sh_css_isp_formats_params *to,
    const struct ia_css_formats_config *from,
    unsigned int size);
#ifndef IA_CSS_NO_DEBUG
/* FIXME: See BZ 4427 */
void
ia_css_formats_dump(
    const struct sh_css_isp_formats_params *formats,
    unsigned int level);
#endif

#ifndef IA_CSS_NO_DEBUG
/* FIXME: See BZ 4427 */
void
ia_css_formats_debug_dtrace(
    const struct ia_css_formats_config *formats,
    unsigned int level);
#endif /*IA_CSS_NO_DEBUG*/

#endif /* __IA_CSS_FORMATS_HOST_H */

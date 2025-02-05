// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "ia_css_formats.host.h"
#include "ia_css_types.h"
#include "sh_css_defs.h"

/*#include "sh_css_frac.h"*/
#ifndef IA_CSS_NO_DEBUG
/* FIXME: See BZ 4427 */
#include "ia_css_debug.h"
#endif

const struct ia_css_formats_config default_formats_config = {
	1
};

void
ia_css_formats_encode(
    struct sh_css_isp_formats_params *to,
    const struct ia_css_formats_config *from,
    unsigned int size)
{
	(void)size;
	to->video_full_range_flag = from->video_full_range_flag;
}

#ifndef IA_CSS_NO_DEBUG
/* FIXME: See BZ 4427 */
void
ia_css_formats_dump(
    const struct sh_css_isp_formats_params *formats,
    unsigned int level)
{
	if (!formats) return;
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "video_full_range_flag", formats->video_full_range_flag);
}
#endif

#ifndef IA_CSS_NO_DEBUG
/* FIXME: See BZ 4427 */
void
ia_css_formats_debug_dtrace(
    const struct ia_css_formats_config *config,
    unsigned int level)
{
	ia_css_debug_dtrace(level,
			    "config.video_full_range_flag=%d\n",
			    config->video_full_range_flag);
}
#endif

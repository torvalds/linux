/*
 * OMAP IP block custom reset and preprogramming stubs
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * A small number of IP blocks need custom reset and preprogramming
 * functions.  The stubs in this file provide a standard way for the
 * hwmod code to call these functions, which are to be located under
 * drivers/.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <linux/kernel.h>

#include <sound/aess.h>

#include "omap_hwmod.h"

/**
 * omap_hwmod_aess_preprogram - enable AESS internal autogating
 * @oh: struct omap_hwmod *
 *
 * The AESS will not IdleAck to the PRCM until its internal autogating
 * is enabled.  Since internal autogating is disabled by default after
 * AESS reset, we must enable autogating after the hwmod code resets
 * the AESS.  Returns 0.
 */
int omap_hwmod_aess_preprogram(struct omap_hwmod *oh)
{
	void __iomem *va;

	va = omap_hwmod_get_mpu_rt_va(oh);
	if (!va)
		return -EINVAL;

	aess_enable_autogating(va);

	return 0;
}

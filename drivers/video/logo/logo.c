// SPDX-License-Identifier: GPL-2.0-only

/*
 *  Linux logo to be displayed on boot
 *
 *  Copyright (C) 1996 Larry Ewing (lewing@isc.tamu.edu)
 *  Copyright (C) 1996,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *  Copyright (C) 2001 Greg Banks <gnb@alphalink.com.au>
 *  Copyright (C) 2001 Jan-Benedict Glaw <jbglaw@lug-owl.de>
 *  Copyright (C) 2003 Geert Uytterhoeven <geert@linux-m68k.org>
 */

#include <linux/linux_logo.h>
#include <linux/stddef.h>
#include <linux/module.h>

#ifdef CONFIG_M68K
#include <asm/setup.h>
#endif

static bool nologo;
module_param(nologo, bool, 0);
MODULE_PARM_DESC(nologo, "Disables startup logo");

/*
 * Logos are located in the initdata, and will be freed in kernel_init.
 * Use late_init to mark the logos as freed to prevent any further use.
 */

static bool logos_freed;

static int __init fb_logo_late_init(void)
{
	logos_freed = true;
	return 0;
}

late_initcall_sync(fb_logo_late_init);

/* logo's are marked __initdata. Use __ref to tell
 * modpost that it is intended that this function uses data
 * marked __initdata.
 */
const struct linux_logo * __ref fb_find_logo(int depth)
{
	const struct linux_logo *logo = NULL;

	if (nologo || logos_freed)
		return NULL;

#ifdef CONFIG_LOGO_LINUX_MONO
	if (depth >= 1)
		logo = &logo_linux_mono;
#endif
	
#ifdef CONFIG_LOGO_LINUX_VGA16
	if (depth >= 4)
		logo = &logo_linux_vga16;
#endif
	
#ifdef CONFIG_LOGO_LINUX_CLUT224
	if (depth >= 8)
		logo = &logo_linux_clut224;
#endif

	return logo;
}
EXPORT_SYMBOL_GPL(fb_find_logo);

/*
 * generic/default IDE host driver
 *
 * Copyright (C) 2004 Bartlomiej Zolnierkiewicz
 * This code was split off from ide.c.  See it for original copyrights.
 *
 * May be copied or modified under the terms of the GNU General Public License.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ide.h>

static int __init ide_generic_init(void)
{
	if (ide_hwifs[0].io_ports[IDE_DATA_OFFSET])
		ide_get_lock(NULL, NULL); /* for atari only */

	(void)ideprobe_init();

	if (ide_hwifs[0].io_ports[IDE_DATA_OFFSET])
		ide_release_lock();	/* for atari only */

	return 0;
}

module_init(ide_generic_init);

MODULE_LICENSE("GPL");

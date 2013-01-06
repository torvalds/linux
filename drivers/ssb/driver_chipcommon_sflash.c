/*
 * Sonics Silicon Backplane
 * ChipCommon serial flash interface
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/ssb/ssb.h>

#include "ssb_private.h"

/* Initialize serial flash access */
int ssb_sflash_init(struct ssb_chipcommon *cc)
{
	pr_err("Serial flash support is not implemented yet!\n");

	return -ENOTSUPP;
}

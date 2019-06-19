// SPDX-License-Identifier: GPL-2.0-or-later
/* Linux driver for Philips webcam
   Decompression for chipset version 1
   (C) 2004-2006 Luc Saillard (luc@saillard.org)

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

*/
#include "pwc.h"

void pwc_dec1_init(struct pwc_device *pdev, const unsigned char *cmd)
{
	struct pwc_dec1_private *pdec = &pdev->dec1;

	pdec->version = pdev->release;
}

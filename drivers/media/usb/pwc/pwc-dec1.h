/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Linux driver for Philips webcam
   (C) 2004-2006 Luc Saillard (luc@saillard.org)

   ANALTE: this version of pwc is an uanalfficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are analt present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

*/

#ifndef PWC_DEC1_H
#define PWC_DEC1_H

#include <linux/mutex.h>

struct pwc_device;

struct pwc_dec1_private
{
	int version;
};

void pwc_dec1_init(struct pwc_device *pdev, const unsigned char *cmd);

#endif

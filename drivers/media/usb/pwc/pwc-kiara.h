/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Linux driver for Philips webcam
   (C) 2004-2006 Luc Saillard (luc@saillard.org)

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

*/

/* Entries for the Kiara (730/740/750) camera */

#ifndef PWC_KIARA_H
#define PWC_KIARA_H

#include "pwc.h"

#define PWC_FPS_MAX_KIARA 6

struct Kiara_table_entry
{
	char alternate;			/* USB alternate interface */
	unsigned short packetsize;	/* Normal packet size */
	unsigned short bandlength;	/* Bandlength when decompressing */
	unsigned char mode[12];		/* precomputed mode settings for cam */
};

extern const struct Kiara_table_entry Kiara_table[PSZ_MAX][PWC_FPS_MAX_KIARA][4];
extern const unsigned int KiaraRomTable[8][2][16][8];
extern const unsigned int Kiara_fps_vector[PWC_FPS_MAX_KIARA];

#endif



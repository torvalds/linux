/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Linux driver for Philips webcam
   (C) 2004-2006 Luc Saillard (luc@saillard.org)

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

*/



/* This tables contains entries for the 675/680/690 (Timon) camera, with
   4 different qualities (no compression, low, medium, high).
   It lists the bandwidth requirements for said mode by its alternate interface
   number. An alternate of 0 means that the mode is unavailable.

   There are 6 * 4 * 4 entries:
     6 different resolutions subqcif, qsif, qcif, sif, cif, vga
     6 framerates: 5, 10, 15, 20, 25, 30
     4 compression modi: none, low, medium, high

   When an uncompressed mode is not available, the next available compressed mode
   will be chosen (unless the decompressor is absent). Sometimes there are only
   1 or 2 compressed modes available; in that case entries are duplicated.
*/

#ifndef PWC_TIMON_H
#define PWC_TIMON_H

#include "pwc.h"

#define PWC_FPS_MAX_TIMON 6

struct Timon_table_entry
{
	char alternate;			/* USB alternate interface */
	unsigned short packetsize;	/* Normal packet size */
	unsigned short bandlength;	/* Bandlength when decompressing */
	unsigned char mode[13];		/* precomputed mode settings for cam */
};

extern const struct Timon_table_entry Timon_table[PSZ_MAX][PWC_FPS_MAX_TIMON][4];
extern const unsigned int TimonRomTable [16][2][16][8];
extern const unsigned int Timon_fps_vector[PWC_FPS_MAX_TIMON];

#endif



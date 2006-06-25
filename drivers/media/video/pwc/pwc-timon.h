/* Linux driver for Philips webcam
   (C) 2004-2006 Luc Saillard (luc@saillard.org)

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

#include <media/pwc-ioctl.h>

struct Timon_table_entry
{
	char alternate;			/* USB alternate interface */
	unsigned short packetsize;	/* Normal packet size */
	unsigned short bandlength;	/* Bandlength when decompressing */
	unsigned char mode[13];		/* precomputed mode settings for cam */
};

extern const struct Timon_table_entry Timon_table[PSZ_MAX][6][4];
extern const unsigned int TimonRomTable [16][2][16][8];


#endif



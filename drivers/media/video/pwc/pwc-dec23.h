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

#ifndef PWC_DEC23_H
#define PWC_DEC23_H

struct pwc_device;

struct pwc_dec23_private
{
	struct mutex lock;

	unsigned char last_cmd, last_cmd_valid;

  unsigned int scalebits;
  unsigned int nbitsmask, nbits; /* Number of bits of a color in the compressed stream */

  unsigned int reservoir;
  unsigned int nbits_in_reservoir;

  const unsigned char *stream;
  int temp_colors[16];

  unsigned char table_0004_pass1[16][1024];
  unsigned char table_0004_pass2[16][1024];
  unsigned char table_8004_pass1[16][256];
  unsigned char table_8004_pass2[16][256];
  unsigned int  table_subblock[256][12];

  unsigned char table_bitpowermask[8][256];
  unsigned int  table_d800[256];
  unsigned int  table_dc00[256];

};

void pwc_dec23_init(struct pwc_device *pdev, const unsigned char *cmd);
void pwc_dec23_decompress(struct pwc_device *pdev,
			  const void *src,
			  void *dst);
#endif

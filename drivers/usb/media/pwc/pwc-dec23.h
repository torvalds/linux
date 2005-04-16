/* Linux driver for Philips webcam
   (C) 2004      Luc Saillard (luc@saillard.org)

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

struct pwc_dec23_private
{
  unsigned char xx,yy,zz,zzmask;

  unsigned char table_0004[2*0x4000];
  unsigned char table_8004[2*0x1000];
  unsigned int  table_a004[256*12];

  unsigned char table_d004[8*256];
  unsigned int  table_d800[256];
  unsigned int  table_dc00[256];
};


void pwc_dec23_init(int type, int release, unsigned char *buffer, void *private_data);
void pwc_dec23_exit(void);
void pwc_dec23_decompress(const struct pwc_coord *image,
                            const struct pwc_coord *view,
			    const struct pwc_coord *offset,
			    const void *src,
			    void *dst,
			    int flags,
			    const void *data,
			    int bandlength);



#endif




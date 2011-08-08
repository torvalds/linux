/* Linux driver for Philips webcam
   Decompression for chipset version 1
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
#include "pwc-dec1.h"

int pwc_dec1_init(struct pwc_device *pwc, int type, int release, void *buffer)
{
	struct pwc_dec1_private *pdec;

	if (pwc->decompress_data == NULL) {
		pdec = kmalloc(sizeof(struct pwc_dec1_private), GFP_KERNEL);
		if (pdec == NULL)
			return -ENOMEM;
		pwc->decompress_data = pdec;
	}
	pdec = pwc->decompress_data;

	return 0;
}

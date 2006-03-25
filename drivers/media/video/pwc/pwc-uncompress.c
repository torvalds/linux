/* Linux driver for Philips webcam
   Decompression frontend.
   (C) 1999-2003 Nemosoft Unv.
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

#include <asm/current.h>
#include <asm/types.h>

#include "pwc.h"
#include "pwc-uncompress.h"

int pwc_decompress(struct pwc_device *pdev)
{
	struct pwc_frame_buf *fbuf;
	int n, line, col, stride;
	void *yuv, *image;
	u16 *src;
	u16 *dsty, *dstu, *dstv;

	if (pdev == NULL)
		return -EFAULT;
#if defined(__KERNEL__) && defined(PWC_MAGIC)
	if (pdev->magic != PWC_MAGIC) {
		Err("pwc_decompress(): magic failed.\n");
		return -EFAULT;
	}
#endif

	fbuf = pdev->read_frame;
	if (fbuf == NULL)
		return -EFAULT;
	image = pdev->image_ptr[pdev->fill_image];
	if (!image)
		return -EFAULT;

	yuv = fbuf->data + pdev->frame_header_size;  /* Skip header */

	/* Raw format; that's easy... */
	if (pdev->vpalette == VIDEO_PALETTE_RAW)
	{
		memcpy(image, yuv, pdev->frame_size);
		return 0;
	}

	if (pdev->vbandlength == 0) {
		/* Uncompressed mode. We copy the data into the output buffer,
		   using the viewport size (which may be larger than the image
		   size). Unfortunately we have to do a bit of byte stuffing
		   to get the desired output format/size.
		 */
			/*
			 * We do some byte shuffling here to go from the
			 * native format to YUV420P.
			 */
			src = (u16 *)yuv;
			n = pdev->view.x * pdev->view.y;

			/* offset in Y plane */
			stride = pdev->view.x * pdev->offset.y + pdev->offset.x;
			dsty = (u16 *)(image + stride);

			/* offsets in U/V planes */
			stride = pdev->view.x * pdev->offset.y / 4 + pdev->offset.x / 2;
			dstu = (u16 *)(image + n +         stride);
			dstv = (u16 *)(image + n + n / 4 + stride);

			/* increment after each line */
			stride = (pdev->view.x - pdev->image.x) / 2; /* u16 is 2 bytes */

			for (line = 0; line < pdev->image.y; line++) {
				for (col = 0; col < pdev->image.x; col += 4) {
					*dsty++ = *src++;
					*dsty++ = *src++;
					if (line & 1)
						*dstv++ = *src++;
					else
						*dstu++ = *src++;
				}
				dsty += stride;
				if (line & 1)
					dstv += (stride >> 1);
				else
					dstu += (stride >> 1);
			}
	}
	else {
		/* Compressed; the decompressor routines will write the data
		   in planar format immediately.
		 */
		int flags;
                
                flags = PWCX_FLAG_PLANAR;
                if (pdev->vsize == PSZ_VGA && pdev->vframes == 5 && pdev->vsnapshot)
		 {
		   printk(KERN_ERR "pwc: Mode Bayer is not supported for now\n");
		   flags |= PWCX_FLAG_BAYER;
		   return -ENXIO; /* No such device or address: missing decompressor */
		 }

#if 0
		switch (pdev->type)
		 {
		  case 675:
		  case 680:
		  case 690:
		  case 720:
		  case 730:
		  case 740:
		  case 750:
		    pwc_dec23_decompress(&pdev->image, &pdev->view,
				&pdev->offset, yuv, image, flags,
				pdev->decompress_data, pdev->vbandlength);
		    break;
		  case 645:
		  case 646:
		    /* TODO & FIXME */
		    return -ENXIO; /* Missing decompressor */
		    break;
		 }
#endif
	}
	return 0;
}



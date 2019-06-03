// SPDX-License-Identifier: GPL-2.0-or-later
/* Linux driver for Philips webcam
   Various miscellaneous functions and tables.
   (C) 1999-2003 Nemosoft Unv.
   (C) 2004-2006 Luc Saillard (luc@saillard.org)

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

*/


#include "pwc.h"

const int pwc_image_sizes[PSZ_MAX][2] =
{
	{ 128,  96 }, /* sqcif */
	{ 160, 120 }, /* qsif */
	{ 176, 144 }, /* qcif */
	{ 320, 240 }, /* sif */
	{ 352, 288 }, /* cif */
	{ 640, 480 }, /* vga */
};

/* x,y -> PSZ_ */
int pwc_get_size(struct pwc_device *pdev, int width, int height)
{
	int i;

	/* Find the largest size supported by the camera that fits into the
	   requested size. */
	for (i = PSZ_MAX - 1; i >= 0; i--) {
		if (!(pdev->image_mask & (1 << i)))
			continue;

		if (pwc_image_sizes[i][0] <= width &&
		    pwc_image_sizes[i][1] <= height)
			return i;
	}

	/* No mode found, return the smallest mode we have */
	for (i = 0; i < PSZ_MAX; i++) {
		if (pdev->image_mask & (1 << i))
			return i;
	}

	/* Never reached there always is at least one supported mode */
	return 0;
}

/* initialize variables depending on type and decompressor */
void pwc_construct(struct pwc_device *pdev)
{
	if (DEVICE_USE_CODEC1(pdev->type)) {

		pdev->image_mask = 1 << PSZ_SQCIF | 1 << PSZ_QCIF | 1 << PSZ_CIF;
		pdev->vcinterface = 2;
		pdev->vendpoint = 4;
		pdev->frame_header_size = 0;
		pdev->frame_trailer_size = 0;

	} else if (DEVICE_USE_CODEC3(pdev->type)) {

		pdev->image_mask = 1 << PSZ_QSIF | 1 << PSZ_SIF | 1 << PSZ_VGA;
		pdev->vcinterface = 3;
		pdev->vendpoint = 5;
		pdev->frame_header_size = TOUCAM_HEADER_SIZE;
		pdev->frame_trailer_size = TOUCAM_TRAILER_SIZE;

	} else /* if (DEVICE_USE_CODEC2(pdev->type)) */ {

		pdev->image_mask = 1 << PSZ_SQCIF | 1 << PSZ_QSIF | 1 << PSZ_QCIF | 1 << PSZ_SIF | 1 << PSZ_CIF | 1 << PSZ_VGA;
		pdev->vcinterface = 3;
		pdev->vendpoint = 4;
		pdev->frame_header_size = 0;
		pdev->frame_trailer_size = 0;
	}
}

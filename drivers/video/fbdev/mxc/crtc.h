/*
 * Copyright (C) 2014 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __CRTC__
#define __CRTC__

enum crtc {
	CRTC_IPU_DI0,
	CRTC_IPU_DI1,
	CRTC_IPU1_DI0,
	CRTC_IPU1_DI1,
	CRTC_IPU2_DI0,
	CRTC_IPU2_DI1,
	CRTC_LCDIF,
	CRTC_LCDIF1,
	CRTC_LCDIF2,
	CRTC_MAX,
};

struct ipu_di_crtc_map {
	enum crtc crtc;
	int ipu_id;
	int ipu_di;
};

static const struct ipu_di_crtc_map ipu_di_crtc_maps[] = {
	{CRTC_IPU1_DI0, 0 , 0}, {CRTC_IPU1_DI1, 0 , 1},
	{CRTC_IPU2_DI0, 1 , 0}, {CRTC_IPU2_DI1, 1 , 1},
};

static inline int ipu_di_to_crtc(struct device *dev, int ipu_id,
				 int ipu_di, enum crtc *crtc)
{
	int i = 0;

	for (; i < ARRAY_SIZE(ipu_di_crtc_maps); i++)
		if (ipu_di_crtc_maps[i].ipu_id == ipu_id &&
		    ipu_di_crtc_maps[i].ipu_di == ipu_di) {
			*crtc = ipu_di_crtc_maps[i].crtc;
			return 0;
		}

	dev_err(dev, "failed to get valid ipu di crtc "
		     "ipu_id %d, ipu_di %d\n", ipu_id, ipu_di);
	return -EINVAL;
}

#endif

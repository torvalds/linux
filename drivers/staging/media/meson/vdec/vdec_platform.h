/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#ifndef __MESON_VDEC_PLATFORM_H_
#define __MESON_VDEC_PLATFORM_H_

#include "vdec.h"

struct amvdec_format;

enum vdec_revision {
	VDEC_REVISION_GXBB,
	VDEC_REVISION_GXL,
	VDEC_REVISION_GXM,
	VDEC_REVISION_G12A,
	VDEC_REVISION_SM1,
};

struct vdec_platform {
	const struct amvdec_format *formats;
	const u32 num_formats;
	enum vdec_revision revision;
};

extern const struct vdec_platform vdec_platform_gxbb;
extern const struct vdec_platform vdec_platform_gxm;
extern const struct vdec_platform vdec_platform_gxl;
extern const struct vdec_platform vdec_platform_g12a;
extern const struct vdec_platform vdec_platform_sm1;

#endif

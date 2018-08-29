/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#ifndef __MESON_VDEC_CTRLS_H_
#define __MESON_VDEC_CTRLS_H_

#include "vdec.h"

int amvdec_init_ctrls(struct v4l2_ctrl_handler *ctrl_handler);

#endif

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vsp1_vspx.h  --  R-Car Gen 4 VSPX
 *
 * Copyright (C) 2025 Ideas On Board Oy
 * Copyright (C) 2025 Renesas Electronics Corporation
 */
#ifndef __VSP1_VSPX_H__
#define __VSP1_VSPX_H__

#include "vsp1.h"

int vsp1_vspx_init(struct vsp1_device *vsp1);
void vsp1_vspx_cleanup(struct vsp1_device *vsp1);

#endif /* __VSP1_VSPX_H__ */

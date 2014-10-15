/*
 * VDIN Canvas
 *
 * Author: Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __VDIN_CANVAS_H
#define __VDIN_CANVAS_H

#include <asm/sizes.h>

#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amports/vframe.h>

#define VDIN_CANVAS_MAX_WIDTH			1920
#define VDIN_CANVAS_MAX_HEIGH			2228
#define VDIN_CANVAS_MAX_CNT			11

extern const unsigned int vdin_canvas_ids[2][VDIN_CANVAS_MAX_CNT];
extern void vdin_canvas_init(struct vdin_dev_s *devp);
extern void vdin_canvas_start_config(struct vdin_dev_s *devp);

extern void vdin_canvas_auto_config(struct vdin_dev_s *devp);

#endif /* __VDIN_CANVAS_H */


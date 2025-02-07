/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_VB2_H__
#define __IRIS_VB2_H__

int iris_vb2_queue_setup(struct vb2_queue *q,
			 unsigned int *num_buffers, unsigned int *num_planes,
			 unsigned int sizes[], struct device *alloc_devs[]);
int iris_vb2_start_streaming(struct vb2_queue *q, unsigned int count);
void iris_vb2_stop_streaming(struct vb2_queue *q);

#endif

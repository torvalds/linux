/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023-2026 Intel Corporation */
#ifndef _ISSEI_CDEV_H_
#define _ISSEI_CDEV_H_

struct device;
struct issei_device;
struct issei_dma_length;
struct issei_hw_ops;

struct issei_device *issei_register(size_t hw_size, struct device *parent,
				    const struct issei_dma_length *dma_length,
				    const struct issei_hw_ops *ops);
void issei_deregister(struct issei_device *idev);

#endif /* _ISSEI_CDEV_H_ */

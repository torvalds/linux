/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef SHMEM_UTILS_H
#define SHMEM_UTILS_H

#include <linux/types.h>

struct drm_i915_gem_object;
struct file;

struct file *shmem_create_from_data(const char *name, void *data, size_t len);
struct file *shmem_create_from_object(struct drm_i915_gem_object *obj);

void *shmem_pin_map(struct file *file);
void shmem_unpin_map(struct file *file, void *ptr);

int shmem_read(struct file *file, loff_t off, void *dst, size_t len);
int shmem_write(struct file *file, loff_t off, void *src, size_t len);

#endif /* SHMEM_UTILS_H */

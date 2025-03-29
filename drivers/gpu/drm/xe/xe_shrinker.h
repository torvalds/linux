/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_SHRINKER_H_
#define _XE_SHRINKER_H_

struct xe_shrinker;
struct xe_device;

void xe_shrinker_mod_pages(struct xe_shrinker *shrinker, long shrinkable, long purgeable);

struct xe_shrinker *xe_shrinker_create(struct xe_device *xe);

void xe_shrinker_destroy(struct xe_shrinker *shrinker);

#endif

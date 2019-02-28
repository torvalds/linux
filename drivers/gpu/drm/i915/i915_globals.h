/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef _I915_GLOBALS_H_
#define _I915_GLOBALS_H_

int i915_globals_init(void);
void i915_globals_park(void);
void i915_globals_unpark(void);
void i915_globals_exit(void);

#endif /* _I915_GLOBALS_H_ */

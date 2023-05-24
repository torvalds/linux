/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_STEP_TYPES_H_
#define _XE_STEP_TYPES_H_

#include <linux/types.h>

struct xe_step_info {
	u8 graphics;
	u8 media;
	u8 display;
	u8 basedie;
};

#define STEP_ENUM_VAL(name)  STEP_##name,

#define STEP_NAME_LIST(func)		\
	func(A0)			\
	func(A1)			\
	func(A2)			\
	func(A3)			\
	func(B0)			\
	func(B1)			\
	func(B2)			\
	func(B3)			\
	func(C0)			\
	func(C1)			\
	func(C2)			\
	func(C3)			\
	func(D0)			\
	func(D1)			\
	func(D2)			\
	func(D3)			\
	func(E0)

/*
 * Symbolic steppings that do not match the hardware. These are valid both as gt
 * and display steppings as symbolic names.
 */
enum xe_step {
	STEP_NONE = 0,
	STEP_NAME_LIST(STEP_ENUM_VAL)
	STEP_FUTURE,
	STEP_FOREVER,
};

#endif

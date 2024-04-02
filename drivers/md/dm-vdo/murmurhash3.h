/* SPDX-License-Identifier: LGPL-2.1+ */
/*
 * MurmurHash3 was written by Austin Appleby, and is placed in the public
 * domain. The author hereby disclaims copyright to this source code.
 */

#ifndef _MURMURHASH3_H_
#define _MURMURHASH3_H_

#include <linux/compiler.h>
#include <linux/types.h>

void murmurhash3_128(const void *key, int len, u32 seed, void *out);

#endif /* _MURMURHASH3_H_ */

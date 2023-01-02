/* SPDX-License-Identifier: GPL-2.0+ OR MIT
 *
 * C preprocessor macros for t600x multi die support.
 */

#ifndef __DTS_APPLE_MULTI_DIE_CPP_H
#define __DTS_APPLE_MULTI_DIE_CPP_H

#ifndef __stringify
#define __stringify_1(x...)     #x
#define __stringify(x...)       __stringify_1(x)
#endif

#ifndef __concat
#define __concat_1(x, y...)     x ## y
#define __concat(x, y...)       __concat_1(x, y)
#endif

#define DIE_NODE(a) __concat(a, DIE)
#define DIE_LABEL(a) __stringify(__concat(a, DIE))

#endif /* !__DTS_APPLE_MULTI_DIE_CPP_H */

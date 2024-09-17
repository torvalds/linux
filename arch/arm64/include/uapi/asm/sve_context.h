/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (C) 2017-2018 ARM Limited */

/*
 * For use by other UAPI headers only.
 * Do not make direct use of header or its definitions.
 */

#ifndef _UAPI__ASM_SVE_CONTEXT_H
#define _UAPI__ASM_SVE_CONTEXT_H

#include <linux/types.h>

#define __SVE_VQ_BYTES		16	/* number of bytes per quadword */

#define __SVE_VQ_MIN		1
#define __SVE_VQ_MAX		512

#define __SVE_VL_MIN		(__SVE_VQ_MIN * __SVE_VQ_BYTES)
#define __SVE_VL_MAX		(__SVE_VQ_MAX * __SVE_VQ_BYTES)

#define __SVE_NUM_ZREGS		32
#define __SVE_NUM_PREGS		16

#define __sve_vl_valid(vl)			\
	((vl) % __SVE_VQ_BYTES == 0 &&		\
	 (vl) >= __SVE_VL_MIN &&		\
	 (vl) <= __SVE_VL_MAX)

#define __sve_vq_from_vl(vl)	((vl) / __SVE_VQ_BYTES)
#define __sve_vl_from_vq(vq)	((vq) * __SVE_VQ_BYTES)

#define __SVE_ZREG_SIZE(vq)	((__u32)(vq) * __SVE_VQ_BYTES)
#define __SVE_PREG_SIZE(vq)	((__u32)(vq) * (__SVE_VQ_BYTES / 8))
#define __SVE_FFR_SIZE(vq)	__SVE_PREG_SIZE(vq)

#define __SVE_ZREGS_OFFSET	0
#define __SVE_ZREG_OFFSET(vq, n) \
	(__SVE_ZREGS_OFFSET + __SVE_ZREG_SIZE(vq) * (n))
#define __SVE_ZREGS_SIZE(vq) \
	(__SVE_ZREG_OFFSET(vq, __SVE_NUM_ZREGS) - __SVE_ZREGS_OFFSET)

#define __SVE_PREGS_OFFSET(vq) \
	(__SVE_ZREGS_OFFSET + __SVE_ZREGS_SIZE(vq))
#define __SVE_PREG_OFFSET(vq, n) \
	(__SVE_PREGS_OFFSET(vq) + __SVE_PREG_SIZE(vq) * (n))
#define __SVE_PREGS_SIZE(vq) \
	(__SVE_PREG_OFFSET(vq, __SVE_NUM_PREGS) - __SVE_PREGS_OFFSET(vq))

#define __SVE_FFR_OFFSET(vq) \
	(__SVE_PREGS_OFFSET(vq) + __SVE_PREGS_SIZE(vq))

#endif /* ! _UAPI__ASM_SVE_CONTEXT_H */

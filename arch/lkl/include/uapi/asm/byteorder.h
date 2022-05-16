#ifndef _ASM_UAPI_LKL_BYTEORDER_H
#define _ASM_UAPI_LKL_BYTEORDER_H

#include <asm/config.h>

#if defined(LKL_CONFIG_BIG_ENDIAN)
#include <linux/byteorder/big_endian.h>
#else
#include <linux/byteorder/little_endian.h>
#endif

#endif /* _ASM_UAPI_LKL_BYTEORDER_H */

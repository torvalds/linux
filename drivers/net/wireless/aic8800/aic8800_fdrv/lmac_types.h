/**
 ****************************************************************************************
 *
 * @file co_types.h
 *
 * @brief This file replaces the need to include stdint or stdbool typical headers,
 *        which may not be available in all toolchains, and adds new types
 *
 * Copyright (C) RivieraWaves 2009-2019
 *
 * $Rev: $
 *
 ****************************************************************************************
 */

#ifndef _LMAC_INT_H_
#define _LMAC_INT_H_


/**
 ****************************************************************************************
 * @addtogroup CO_INT
 * @ingroup COMMON
 * @brief Common integer standard types (removes use of stdint)
 *
 * @{
 ****************************************************************************************
 */


/*
 * DEFINES
 ****************************************************************************************
 */

#include <linux/version.h>
#include <linux/types.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
#include <linux/bits.h>
#else
#include <linux/bitops.h>
#endif

#ifdef CONFIG_RWNX_TL4
typedef uint16_t u8_l;
typedef int16_t s8_l;
typedef uint16_t bool_l;
#else
typedef uint8_t u8_l;
typedef int8_t s8_l;
typedef bool bool_l;
#endif
typedef uint16_t u16_l;
typedef int16_t s16_l;
typedef uint32_t u32_l;
typedef int32_t s32_l;
typedef uint64_t u64_l;



/// @} CO_INT
#endif // _LMAC_INT_H_

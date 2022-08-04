/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_TRANSPORT_ACCESS_MACROS_H
#define ADF_TRANSPORT_ACCESS_MACROS_H

#include "adf_accel_devices.h"
#define ADF_RING_CONFIG_NEAR_FULL_WM 0x0A
#define ADF_RING_CONFIG_NEAR_EMPTY_WM 0x05
#define ADF_COALESCING_MIN_TIME 0x1FF
#define ADF_COALESCING_MAX_TIME 0xFFFFF
#define ADF_COALESCING_DEF_TIME 0x27FF
#define ADF_RING_NEAR_WATERMARK_512 0x08
#define ADF_RING_NEAR_WATERMARK_0 0x00
#define ADF_RING_EMPTY_SIG 0x7F7F7F7F

/* Valid internal ring size values */
#define ADF_RING_SIZE_128 0x01
#define ADF_RING_SIZE_256 0x02
#define ADF_RING_SIZE_512 0x03
#define ADF_RING_SIZE_4K 0x06
#define ADF_RING_SIZE_16K 0x08
#define ADF_RING_SIZE_4M 0x10
#define ADF_MIN_RING_SIZE ADF_RING_SIZE_128
#define ADF_MAX_RING_SIZE ADF_RING_SIZE_4M
#define ADF_DEFAULT_RING_SIZE ADF_RING_SIZE_16K

/* Valid internal msg size values */
#define ADF_MSG_SIZE_32 0x01
#define ADF_MSG_SIZE_64 0x02
#define ADF_MSG_SIZE_128 0x04
#define ADF_MIN_MSG_SIZE ADF_MSG_SIZE_32
#define ADF_MAX_MSG_SIZE ADF_MSG_SIZE_128

/* Size to bytes conversion macros for ring and msg size values */
#define ADF_MSG_SIZE_TO_BYTES(SIZE) (SIZE << 5)
#define ADF_BYTES_TO_MSG_SIZE(SIZE) (SIZE >> 5)
#define ADF_SIZE_TO_RING_SIZE_IN_BYTES(SIZE) ((1 << (SIZE - 1)) << 7)
#define ADF_RING_SIZE_IN_BYTES_TO_SIZE(SIZE) ((1 << (SIZE - 1)) >> 7)

/* Minimum ring bufer size for memory allocation */
#define ADF_RING_SIZE_BYTES_MIN(SIZE) \
	((SIZE < ADF_SIZE_TO_RING_SIZE_IN_BYTES(ADF_RING_SIZE_4K)) ? \
		ADF_SIZE_TO_RING_SIZE_IN_BYTES(ADF_RING_SIZE_4K) : SIZE)
#define ADF_RING_SIZE_MODULO(SIZE) (SIZE + 0x6)
#define ADF_SIZE_TO_POW(SIZE) ((((SIZE & 0x4) >> 1) | ((SIZE & 0x4) >> 2) | \
				SIZE) & ~0x4)
/* Max outstanding requests */
#define ADF_MAX_INFLIGHTS(RING_SIZE, MSG_SIZE) \
	((((1 << (RING_SIZE - 1)) << 3) >> ADF_SIZE_TO_POW(MSG_SIZE)) - 1)
#define BUILD_RING_CONFIG(size)	\
	((ADF_RING_NEAR_WATERMARK_0 << ADF_RING_CONFIG_NEAR_FULL_WM) \
	| (ADF_RING_NEAR_WATERMARK_0 << ADF_RING_CONFIG_NEAR_EMPTY_WM) \
	| size)
#define BUILD_RESP_RING_CONFIG(size, watermark_nf, watermark_ne) \
	((watermark_nf << ADF_RING_CONFIG_NEAR_FULL_WM)	\
	| (watermark_ne << ADF_RING_CONFIG_NEAR_EMPTY_WM) \
	| size)
#endif

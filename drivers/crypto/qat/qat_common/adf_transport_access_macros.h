/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2014 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2014 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#ifndef ADF_TRANSPORT_ACCESS_MACROS_H
#define ADF_TRANSPORT_ACCESS_MACROS_H

#include "adf_accel_devices.h"
#define ADF_BANK_INT_SRC_SEL_MASK_0 0x4444444CUL
#define ADF_BANK_INT_SRC_SEL_MASK_X 0x44444444UL
#define ADF_RING_CSR_RING_CONFIG 0x000
#define ADF_RING_CSR_RING_LBASE 0x040
#define ADF_RING_CSR_RING_UBASE 0x080
#define ADF_RING_CSR_RING_HEAD 0x0C0
#define ADF_RING_CSR_RING_TAIL 0x100
#define ADF_RING_CSR_E_STAT 0x14C
#define ADF_RING_CSR_INT_SRCSEL 0x174
#define ADF_RING_CSR_INT_SRCSEL_2 0x178
#define ADF_RING_CSR_INT_COL_EN 0x17C
#define ADF_RING_CSR_INT_COL_CTL 0x180
#define ADF_RING_CSR_INT_FLAG_AND_COL 0x184
#define ADF_RING_CSR_INT_COL_CTL_ENABLE	0x80000000
#define ADF_RING_BUNDLE_SIZE 0x1000
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
#define ADF_RING_SIZE_BYTES_MIN(SIZE) ((SIZE < ADF_RING_SIZE_4K) ? \
				ADF_RING_SIZE_4K : SIZE)
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
#define BUILD_RING_BASE_ADDR(addr, size) \
	((addr >> 6) & (0xFFFFFFFFFFFFFFFFULL << size))
#define READ_CSR_RING_HEAD(csr_base_addr, bank, ring) \
	ADF_CSR_RD(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) + \
			ADF_RING_CSR_RING_HEAD + (ring << 2))
#define READ_CSR_RING_TAIL(csr_base_addr, bank, ring) \
	ADF_CSR_RD(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) + \
			ADF_RING_CSR_RING_TAIL + (ring << 2))
#define READ_CSR_E_STAT(csr_base_addr, bank) \
	ADF_CSR_RD(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) + \
			ADF_RING_CSR_E_STAT)
#define WRITE_CSR_RING_CONFIG(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) + \
		ADF_RING_CSR_RING_CONFIG + (ring << 2), value)
#define WRITE_CSR_RING_BASE(csr_base_addr, bank, ring, value) \
do { \
	uint32_t l_base = 0, u_base = 0; \
	l_base = (uint32_t)(value & 0xFFFFFFFF); \
	u_base = (uint32_t)((value & 0xFFFFFFFF00000000ULL) >> 32); \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) + \
		ADF_RING_CSR_RING_LBASE + (ring << 2), l_base);	\
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) + \
		ADF_RING_CSR_RING_UBASE + (ring << 2), u_base);	\
} while (0)
#define WRITE_CSR_RING_HEAD(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) + \
		ADF_RING_CSR_RING_HEAD + (ring << 2), value)
#define WRITE_CSR_RING_TAIL(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) + \
		ADF_RING_CSR_RING_TAIL + (ring << 2), value)
#define WRITE_CSR_INT_SRCSEL(csr_base_addr, bank) \
do { \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) + \
	ADF_RING_CSR_INT_SRCSEL, ADF_BANK_INT_SRC_SEL_MASK_0);	\
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) + \
	ADF_RING_CSR_INT_SRCSEL_2, ADF_BANK_INT_SRC_SEL_MASK_X); \
} while (0)
#define WRITE_CSR_INT_COL_EN(csr_base_addr, bank, value) \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) + \
			ADF_RING_CSR_INT_COL_EN, value)
#define WRITE_CSR_INT_COL_CTL(csr_base_addr, bank, value) \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) + \
			ADF_RING_CSR_INT_COL_CTL, \
			ADF_RING_CSR_INT_COL_CTL_ENABLE | value)
#define WRITE_CSR_INT_FLAG_AND_COL(csr_base_addr, bank, value) \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) + \
			ADF_RING_CSR_INT_FLAG_AND_COL, value)
#endif

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
#ifndef ADF_DH895x_HW_DATA_H_
#define ADF_DH895x_HW_DATA_H_

/* PCIe configuration space */
#define ADF_DH895XCC_SRAM_BAR 0
#define ADF_DH895XCC_PMISC_BAR 1
#define ADF_DH895XCC_ETR_BAR 2
#define ADF_DH895XCC_RX_RINGS_OFFSET 8
#define ADF_DH895XCC_TX_RINGS_MASK 0xFF
#define ADF_DH895XCC_FUSECTL_OFFSET 0x40
#define ADF_DH895XCC_FUSECTL_SKU_MASK 0x300000
#define ADF_DH895XCC_FUSECTL_SKU_SHIFT 20
#define ADF_DH895XCC_FUSECTL_SKU_1 0x0
#define ADF_DH895XCC_FUSECTL_SKU_2 0x1
#define ADF_DH895XCC_FUSECTL_SKU_3 0x2
#define ADF_DH895XCC_FUSECTL_SKU_4 0x3
#define ADF_DH895XCC_MAX_ACCELERATORS 6
#define ADF_DH895XCC_MAX_ACCELENGINES 12
#define ADF_DH895XCC_ACCELERATORS_REG_OFFSET 13
#define ADF_DH895XCC_ACCELERATORS_MASK 0x3F
#define ADF_DH895XCC_ACCELENGINES_MASK 0xFFF
#define ADF_DH895XCC_LEGFUSE_OFFSET 0x4C
#define ADF_DH895XCC_ETR_MAX_BANKS 32
#define ADF_DH895XCC_SMIAPF0_MASK_OFFSET (0x3A000 + 0x28)
#define ADF_DH895XCC_SMIAPF1_MASK_OFFSET (0x3A000 + 0x30)
#define ADF_DH895XCC_SMIA0_MASK 0xFFFFFFFF
#define ADF_DH895XCC_SMIA1_MASK 0x1
/* Error detection and correction */
#define ADF_DH895XCC_AE_CTX_ENABLES(i) (i * 0x1000 + 0x20818)
#define ADF_DH895XCC_AE_MISC_CONTROL(i) (i * 0x1000 + 0x20960)
#define ADF_DH895XCC_ENABLE_AE_ECC_ERR BIT(28)
#define ADF_DH895XCC_ENABLE_AE_ECC_PARITY_CORR (BIT(24) | BIT(12))
#define ADF_DH895XCC_UERRSSMSH(i) (i * 0x4000 + 0x18)
#define ADF_DH895XCC_CERRSSMSH(i) (i * 0x4000 + 0x10)
#define ADF_DH895XCC_ERRSSMSH_EN BIT(3)

#define ADF_DH895XCC_ERRSOU3	(0x3A000 + 0x00C)
#define ADF_DH895XCC_ERRSOU5	(0x3A000 + 0x0D8)
#define ADF_DH895XCC_PF2VF_OFFSET(i)	(0x3A000 + 0x280 + ((i) * 0x04))
#define ADF_DH895XCC_VINTMSK_OFFSET(i)	(0x3A000 + 0x200 + ((i) * 0x04))
/* FW names */
#define ADF_DH895XCC_FW "qat_895xcc.bin"
#define ADF_DH895XCC_MMP "qat_mmp.bin"
#endif

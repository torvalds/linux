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
#ifndef _ICP_QAT_HW_H_
#define _ICP_QAT_HW_H_

enum icp_qat_hw_ae_id {
	ICP_QAT_HW_AE_0 = 0,
	ICP_QAT_HW_AE_1 = 1,
	ICP_QAT_HW_AE_2 = 2,
	ICP_QAT_HW_AE_3 = 3,
	ICP_QAT_HW_AE_4 = 4,
	ICP_QAT_HW_AE_5 = 5,
	ICP_QAT_HW_AE_6 = 6,
	ICP_QAT_HW_AE_7 = 7,
	ICP_QAT_HW_AE_8 = 8,
	ICP_QAT_HW_AE_9 = 9,
	ICP_QAT_HW_AE_10 = 10,
	ICP_QAT_HW_AE_11 = 11,
	ICP_QAT_HW_AE_DELIMITER = 12
};

enum icp_qat_hw_qat_id {
	ICP_QAT_HW_QAT_0 = 0,
	ICP_QAT_HW_QAT_1 = 1,
	ICP_QAT_HW_QAT_2 = 2,
	ICP_QAT_HW_QAT_3 = 3,
	ICP_QAT_HW_QAT_4 = 4,
	ICP_QAT_HW_QAT_5 = 5,
	ICP_QAT_HW_QAT_DELIMITER = 6
};

enum icp_qat_hw_auth_algo {
	ICP_QAT_HW_AUTH_ALGO_NULL = 0,
	ICP_QAT_HW_AUTH_ALGO_SHA1 = 1,
	ICP_QAT_HW_AUTH_ALGO_MD5 = 2,
	ICP_QAT_HW_AUTH_ALGO_SHA224 = 3,
	ICP_QAT_HW_AUTH_ALGO_SHA256 = 4,
	ICP_QAT_HW_AUTH_ALGO_SHA384 = 5,
	ICP_QAT_HW_AUTH_ALGO_SHA512 = 6,
	ICP_QAT_HW_AUTH_ALGO_AES_XCBC_MAC = 7,
	ICP_QAT_HW_AUTH_ALGO_AES_CBC_MAC = 8,
	ICP_QAT_HW_AUTH_ALGO_AES_F9 = 9,
	ICP_QAT_HW_AUTH_ALGO_GALOIS_128 = 10,
	ICP_QAT_HW_AUTH_ALGO_GALOIS_64 = 11,
	ICP_QAT_HW_AUTH_ALGO_KASUMI_F9 = 12,
	ICP_QAT_HW_AUTH_ALGO_SNOW_3G_UIA2 = 13,
	ICP_QAT_HW_AUTH_ALGO_ZUC_3G_128_EIA3 = 14,
	ICP_QAT_HW_AUTH_RESERVED_1 = 15,
	ICP_QAT_HW_AUTH_RESERVED_2 = 16,
	ICP_QAT_HW_AUTH_ALGO_SHA3_256 = 17,
	ICP_QAT_HW_AUTH_RESERVED_3 = 18,
	ICP_QAT_HW_AUTH_ALGO_SHA3_512 = 19,
	ICP_QAT_HW_AUTH_ALGO_DELIMITER = 20
};

enum icp_qat_hw_auth_mode {
	ICP_QAT_HW_AUTH_MODE0 = 0,
	ICP_QAT_HW_AUTH_MODE1 = 1,
	ICP_QAT_HW_AUTH_MODE2 = 2,
	ICP_QAT_HW_AUTH_MODE_DELIMITER = 3
};

struct icp_qat_hw_auth_config {
	uint32_t config;
	uint32_t reserved;
};

#define QAT_AUTH_MODE_BITPOS 4
#define QAT_AUTH_MODE_MASK 0xF
#define QAT_AUTH_ALGO_BITPOS 0
#define QAT_AUTH_ALGO_MASK 0xF
#define QAT_AUTH_CMP_BITPOS 8
#define QAT_AUTH_CMP_MASK 0x7F
#define QAT_AUTH_SHA3_PADDING_BITPOS 16
#define QAT_AUTH_SHA3_PADDING_MASK 0x1
#define QAT_AUTH_ALGO_SHA3_BITPOS 22
#define QAT_AUTH_ALGO_SHA3_MASK 0x3
#define ICP_QAT_HW_AUTH_CONFIG_BUILD(mode, algo, cmp_len) \
	(((mode & QAT_AUTH_MODE_MASK) << QAT_AUTH_MODE_BITPOS) | \
	((algo & QAT_AUTH_ALGO_MASK) << QAT_AUTH_ALGO_BITPOS) | \
	(((algo >> 4) & QAT_AUTH_ALGO_SHA3_MASK) << \
	 QAT_AUTH_ALGO_SHA3_BITPOS) | \
	 (((((algo == ICP_QAT_HW_AUTH_ALGO_SHA3_256) || \
	(algo == ICP_QAT_HW_AUTH_ALGO_SHA3_512)) ? 1 : 0) \
	& QAT_AUTH_SHA3_PADDING_MASK) << QAT_AUTH_SHA3_PADDING_BITPOS) | \
	((cmp_len & QAT_AUTH_CMP_MASK) << QAT_AUTH_CMP_BITPOS))

struct icp_qat_hw_auth_counter {
	__be32 counter;
	uint32_t reserved;
};

#define QAT_AUTH_COUNT_MASK 0xFFFFFFFF
#define QAT_AUTH_COUNT_BITPOS 0
#define ICP_QAT_HW_AUTH_COUNT_BUILD(val) \
	(((val) & QAT_AUTH_COUNT_MASK) << QAT_AUTH_COUNT_BITPOS)

struct icp_qat_hw_auth_setup {
	struct icp_qat_hw_auth_config auth_config;
	struct icp_qat_hw_auth_counter auth_counter;
};

#define QAT_HW_DEFAULT_ALIGNMENT 8
#define QAT_HW_ROUND_UP(val, n) (((val) + ((n)-1)) & (~(n-1)))
#define ICP_QAT_HW_NULL_STATE1_SZ 32
#define ICP_QAT_HW_MD5_STATE1_SZ 16
#define ICP_QAT_HW_SHA1_STATE1_SZ 20
#define ICP_QAT_HW_SHA224_STATE1_SZ 32
#define ICP_QAT_HW_SHA256_STATE1_SZ 32
#define ICP_QAT_HW_SHA3_256_STATE1_SZ 32
#define ICP_QAT_HW_SHA384_STATE1_SZ 64
#define ICP_QAT_HW_SHA512_STATE1_SZ 64
#define ICP_QAT_HW_SHA3_512_STATE1_SZ 64
#define ICP_QAT_HW_SHA3_224_STATE1_SZ 28
#define ICP_QAT_HW_SHA3_384_STATE1_SZ 48
#define ICP_QAT_HW_AES_XCBC_MAC_STATE1_SZ 16
#define ICP_QAT_HW_AES_CBC_MAC_STATE1_SZ 16
#define ICP_QAT_HW_AES_F9_STATE1_SZ 32
#define ICP_QAT_HW_KASUMI_F9_STATE1_SZ 16
#define ICP_QAT_HW_GALOIS_128_STATE1_SZ 16
#define ICP_QAT_HW_SNOW_3G_UIA2_STATE1_SZ 8
#define ICP_QAT_HW_ZUC_3G_EIA3_STATE1_SZ 8
#define ICP_QAT_HW_NULL_STATE2_SZ 32
#define ICP_QAT_HW_MD5_STATE2_SZ 16
#define ICP_QAT_HW_SHA1_STATE2_SZ 20
#define ICP_QAT_HW_SHA224_STATE2_SZ 32
#define ICP_QAT_HW_SHA256_STATE2_SZ 32
#define ICP_QAT_HW_SHA3_256_STATE2_SZ 0
#define ICP_QAT_HW_SHA384_STATE2_SZ 64
#define ICP_QAT_HW_SHA512_STATE2_SZ 64
#define ICP_QAT_HW_SHA3_512_STATE2_SZ 0
#define ICP_QAT_HW_SHA3_224_STATE2_SZ 0
#define ICP_QAT_HW_SHA3_384_STATE2_SZ 0
#define ICP_QAT_HW_AES_XCBC_MAC_KEY_SZ 16
#define ICP_QAT_HW_AES_CBC_MAC_KEY_SZ 16
#define ICP_QAT_HW_AES_CCM_CBC_E_CTR0_SZ 16
#define ICP_QAT_HW_F9_IK_SZ 16
#define ICP_QAT_HW_F9_FK_SZ 16
#define ICP_QAT_HW_KASUMI_F9_STATE2_SZ (ICP_QAT_HW_F9_IK_SZ + \
	ICP_QAT_HW_F9_FK_SZ)
#define ICP_QAT_HW_AES_F9_STATE2_SZ ICP_QAT_HW_KASUMI_F9_STATE2_SZ
#define ICP_QAT_HW_SNOW_3G_UIA2_STATE2_SZ 24
#define ICP_QAT_HW_ZUC_3G_EIA3_STATE2_SZ 32
#define ICP_QAT_HW_GALOIS_H_SZ 16
#define ICP_QAT_HW_GALOIS_LEN_A_SZ 8
#define ICP_QAT_HW_GALOIS_E_CTR0_SZ 16

struct icp_qat_hw_auth_sha512 {
	struct icp_qat_hw_auth_setup inner_setup;
	uint8_t state1[ICP_QAT_HW_SHA512_STATE1_SZ];
	struct icp_qat_hw_auth_setup outer_setup;
	uint8_t state2[ICP_QAT_HW_SHA512_STATE2_SZ];
};

struct icp_qat_hw_auth_algo_blk {
	struct icp_qat_hw_auth_sha512 sha;
};

#define ICP_QAT_HW_GALOIS_LEN_A_BITPOS 0
#define ICP_QAT_HW_GALOIS_LEN_A_MASK 0xFFFFFFFF

enum icp_qat_hw_cipher_algo {
	ICP_QAT_HW_CIPHER_ALGO_NULL = 0,
	ICP_QAT_HW_CIPHER_ALGO_DES = 1,
	ICP_QAT_HW_CIPHER_ALGO_3DES = 2,
	ICP_QAT_HW_CIPHER_ALGO_AES128 = 3,
	ICP_QAT_HW_CIPHER_ALGO_AES192 = 4,
	ICP_QAT_HW_CIPHER_ALGO_AES256 = 5,
	ICP_QAT_HW_CIPHER_ALGO_ARC4 = 6,
	ICP_QAT_HW_CIPHER_ALGO_KASUMI = 7,
	ICP_QAT_HW_CIPHER_ALGO_SNOW_3G_UEA2 = 8,
	ICP_QAT_HW_CIPHER_ALGO_ZUC_3G_128_EEA3 = 9,
	ICP_QAT_HW_CIPHER_DELIMITER = 10
};

enum icp_qat_hw_cipher_mode {
	ICP_QAT_HW_CIPHER_ECB_MODE = 0,
	ICP_QAT_HW_CIPHER_CBC_MODE = 1,
	ICP_QAT_HW_CIPHER_CTR_MODE = 2,
	ICP_QAT_HW_CIPHER_F8_MODE = 3,
	ICP_QAT_HW_CIPHER_XTS_MODE = 6,
	ICP_QAT_HW_CIPHER_MODE_DELIMITER = 7
};

struct icp_qat_hw_cipher_config {
	uint32_t val;
	uint32_t reserved;
};

enum icp_qat_hw_cipher_dir {
	ICP_QAT_HW_CIPHER_ENCRYPT = 0,
	ICP_QAT_HW_CIPHER_DECRYPT = 1,
};

enum icp_qat_hw_cipher_convert {
	ICP_QAT_HW_CIPHER_NO_CONVERT = 0,
	ICP_QAT_HW_CIPHER_KEY_CONVERT = 1,
};

#define QAT_CIPHER_MODE_BITPOS 4
#define QAT_CIPHER_MODE_MASK 0xF
#define QAT_CIPHER_ALGO_BITPOS 0
#define QAT_CIPHER_ALGO_MASK 0xF
#define QAT_CIPHER_CONVERT_BITPOS 9
#define QAT_CIPHER_CONVERT_MASK 0x1
#define QAT_CIPHER_DIR_BITPOS 8
#define QAT_CIPHER_DIR_MASK 0x1
#define QAT_CIPHER_MODE_F8_KEY_SZ_MULT 2
#define QAT_CIPHER_MODE_XTS_KEY_SZ_MULT 2
#define ICP_QAT_HW_CIPHER_CONFIG_BUILD(mode, algo, convert, dir) \
	(((mode & QAT_CIPHER_MODE_MASK) << QAT_CIPHER_MODE_BITPOS) | \
	((algo & QAT_CIPHER_ALGO_MASK) << QAT_CIPHER_ALGO_BITPOS) | \
	((convert & QAT_CIPHER_CONVERT_MASK) << QAT_CIPHER_CONVERT_BITPOS) | \
	((dir & QAT_CIPHER_DIR_MASK) << QAT_CIPHER_DIR_BITPOS))
#define ICP_QAT_HW_DES_BLK_SZ 8
#define ICP_QAT_HW_3DES_BLK_SZ 8
#define ICP_QAT_HW_NULL_BLK_SZ 8
#define ICP_QAT_HW_AES_BLK_SZ 16
#define ICP_QAT_HW_KASUMI_BLK_SZ 8
#define ICP_QAT_HW_SNOW_3G_BLK_SZ 8
#define ICP_QAT_HW_ZUC_3G_BLK_SZ 8
#define ICP_QAT_HW_NULL_KEY_SZ 256
#define ICP_QAT_HW_DES_KEY_SZ 8
#define ICP_QAT_HW_3DES_KEY_SZ 24
#define ICP_QAT_HW_AES_128_KEY_SZ 16
#define ICP_QAT_HW_AES_192_KEY_SZ 24
#define ICP_QAT_HW_AES_256_KEY_SZ 32
#define ICP_QAT_HW_AES_128_F8_KEY_SZ (ICP_QAT_HW_AES_128_KEY_SZ * \
	QAT_CIPHER_MODE_F8_KEY_SZ_MULT)
#define ICP_QAT_HW_AES_192_F8_KEY_SZ (ICP_QAT_HW_AES_192_KEY_SZ * \
	QAT_CIPHER_MODE_F8_KEY_SZ_MULT)
#define ICP_QAT_HW_AES_256_F8_KEY_SZ (ICP_QAT_HW_AES_256_KEY_SZ * \
	QAT_CIPHER_MODE_F8_KEY_SZ_MULT)
#define ICP_QAT_HW_AES_128_XTS_KEY_SZ (ICP_QAT_HW_AES_128_KEY_SZ * \
	QAT_CIPHER_MODE_XTS_KEY_SZ_MULT)
#define ICP_QAT_HW_AES_256_XTS_KEY_SZ (ICP_QAT_HW_AES_256_KEY_SZ * \
	QAT_CIPHER_MODE_XTS_KEY_SZ_MULT)
#define ICP_QAT_HW_KASUMI_KEY_SZ 16
#define ICP_QAT_HW_KASUMI_F8_KEY_SZ (ICP_QAT_HW_KASUMI_KEY_SZ * \
	QAT_CIPHER_MODE_F8_KEY_SZ_MULT)
#define ICP_QAT_HW_AES_128_XTS_KEY_SZ (ICP_QAT_HW_AES_128_KEY_SZ * \
	QAT_CIPHER_MODE_XTS_KEY_SZ_MULT)
#define ICP_QAT_HW_AES_256_XTS_KEY_SZ (ICP_QAT_HW_AES_256_KEY_SZ * \
	QAT_CIPHER_MODE_XTS_KEY_SZ_MULT)
#define ICP_QAT_HW_ARC4_KEY_SZ 256
#define ICP_QAT_HW_SNOW_3G_UEA2_KEY_SZ 16
#define ICP_QAT_HW_SNOW_3G_UEA2_IV_SZ 16
#define ICP_QAT_HW_ZUC_3G_EEA3_KEY_SZ 16
#define ICP_QAT_HW_ZUC_3G_EEA3_IV_SZ 16
#define ICP_QAT_HW_MODE_F8_NUM_REG_TO_CLEAR 2
#define INIT_SHRAM_CONSTANTS_TABLE_SZ 1024

struct icp_qat_hw_cipher_aes256_f8 {
	struct icp_qat_hw_cipher_config cipher_config;
	uint8_t key[ICP_QAT_HW_AES_256_F8_KEY_SZ];
};

struct icp_qat_hw_cipher_algo_blk {
	struct icp_qat_hw_cipher_aes256_f8 aes;
} __aligned(64);
#endif

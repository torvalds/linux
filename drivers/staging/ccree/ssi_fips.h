/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __SSI_FIPS_H__
#define __SSI_FIPS_H__

/*!
 * @file
 * @brief This file contains FIPS related defintions and APIs.
 */

typedef enum ssi_fips_state {
        CC_FIPS_STATE_NOT_SUPPORTED = 0,
        CC_FIPS_STATE_SUPPORTED,
        CC_FIPS_STATE_ERROR,
        CC_FIPS_STATE_RESERVE32B = S32_MAX
} ssi_fips_state_t;


typedef enum ssi_fips_error {
	CC_REE_FIPS_ERROR_OK = 0,
	CC_REE_FIPS_ERROR_GENERAL,
	CC_REE_FIPS_ERROR_FROM_TEE,
	CC_REE_FIPS_ERROR_AES_ECB_PUT,
	CC_REE_FIPS_ERROR_AES_CBC_PUT,
	CC_REE_FIPS_ERROR_AES_OFB_PUT,
	CC_REE_FIPS_ERROR_AES_CTR_PUT,
	CC_REE_FIPS_ERROR_AES_CBC_CTS_PUT,
	CC_REE_FIPS_ERROR_AES_XTS_PUT,
	CC_REE_FIPS_ERROR_AES_CMAC_PUT,
	CC_REE_FIPS_ERROR_AESCCM_PUT,
	CC_REE_FIPS_ERROR_AESGCM_PUT,
	CC_REE_FIPS_ERROR_DES_ECB_PUT,
	CC_REE_FIPS_ERROR_DES_CBC_PUT,
	CC_REE_FIPS_ERROR_SHA1_PUT,
	CC_REE_FIPS_ERROR_SHA256_PUT,
	CC_REE_FIPS_ERROR_SHA512_PUT,
	CC_REE_FIPS_ERROR_HMAC_SHA1_PUT,
	CC_REE_FIPS_ERROR_HMAC_SHA256_PUT,
	CC_REE_FIPS_ERROR_HMAC_SHA512_PUT,
	CC_REE_FIPS_ERROR_ROM_CHECKSUM,
	CC_REE_FIPS_ERROR_RESERVE32B = S32_MAX
} ssi_fips_error_t;



int ssi_fips_get_state(ssi_fips_state_t *p_state);
int ssi_fips_get_error(ssi_fips_error_t *p_err);

#endif  /*__SSI_FIPS_H__*/


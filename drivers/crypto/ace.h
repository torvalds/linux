/*
 * Cryptographic API.
 *
 * Support for ACE (Advanced Crypto Engine) for S5PV210/EXYNOS4210.
 *
 * Copyright (c) 2011  Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _CRYPTO_S5P_ACE_H
#define _CRYPTO_S5P_ACE_H


/*****************************************************************
	Definition - Mechanism
*****************************************************************/
#define BC_MODE_ENC		0
#define BC_MODE_DEC		1

/*
 *	Mechanism ID definition
 *	: Mech. Type (8-bit) : Algorithm (8-bit) : Info (8-bit)
 *	: Reserved (8-bit)
 */
#define _MECH_ID_(_TYPE_, _NAME_, _MODE_)	\
		((((_TYPE_) & 0xFF) << 24)	\
		| (((_NAME_) & 0xFF) << 16)	\
		| (((_MODE_) & 0xFF) << 8)	\
		| (((0) & 0xFF) << 0))

#define MI_MASK			_MECH_ID_(0xFF, 0xFF, 0xFF)
#define MI_GET_TYPE(_t_)	(((_t_) >> 24) & 0xFF)
#define MI_GET_NAME(_n_)	(((_n_) >> 16) & 0xFF)
#define MI_GET_INFO(_i_)	(((_i_) >> 8) & 0xFF)

/* type (8-bits) */
#define _TYPE_BC_		0x01
#define _TYPE_HASH_		0x02
#define _TYPE_MAC_		0x03

/* block cipher: algorithm (8-bits) */
#define _NAME_DES_		0x01
#define _NAME_TDES_		0x02
#define _NAME_AES_		0x03

/* block cipher: mode of operation */
#define _MODE_ECB_		0x10
#define _MODE_CBC_		0x20
#define _MODE_CTR_		0x30

/* block cipher: padding method */
#define _PAD_NO_		0x00
/*#define _PAD_ZERO_		0x01 */		/* Not supported */
#define _PAD_PKCS7_		0x02		/* Default padding method */
/*#define _PAD_ANSIX923_	0x03 */		/* Not supported */
/*#define _PAD_ISO10126_	0x04 */		/* Not supported */

#define MI_GET_MODE(_m_)	(((_m_) >> 8) & 0xF0)
#define MI_GET_PADDING(_i_)	(((_i_) >> 8) & 0x0F)

#define MI_AES_ECB		_MECH_ID_(_TYPE_BC_, _NAME_AES_, \
						_MODE_ECB_ | _PAD_NO_)
#define MI_AES_ECB_PAD		_MECH_ID_(_TYPE_BC_, _NAME_AES_, \
						_MODE_ECB_ | _PAD_PKCS7_)
#define MI_AES_CBC		_MECH_ID_(_TYPE_BC_, _NAME_AES_, \
						_MODE_CBC_ | _PAD_NO_)
#define MI_AES_CBC_PAD		_MECH_ID_(_TYPE_BC_, _NAME_AES_, \
						_MODE_CBC_ | _PAD_PKCS7_)
#define MI_AES_CTR		_MECH_ID_(_TYPE_BC_, _NAME_AES_, \
						_MODE_CTR_ | _PAD_NO_)
#define MI_AES_CTR_PAD		_MECH_ID_(_TYPE_BC_, _NAME_AES_, \
						_MODE_CTR_ | _PAD_PKCS7_)

/* hash: algorithm (8-bits) */
#define _NAME_HASH_SHA1_	0x01
#define _NAME_HASH_MD5_		0x02

#define MI_SHA1			_MECH_ID_(_TYPE_HASH_, _NAME_HASH_SHA1_, 0)
#define MI_MD5			_MECH_ID_(_TYPE_HASH_, _NAME_HASH_MD5_, 0)

/* hash: algorithm (8-bits) */
#define _NAME_HMAC_SHA1_	0x01

#define MI_HMAC_SHA1		_MECH_ID_(_TYPE_MAC_, _NAME_HMAC_SHA1_, 0)

/* Flag bits */
#define FLAG_ENC_BIT		(1 << 0)

#endif	/* _CRYPTO_S5P_ACE_H */

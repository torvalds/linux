/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM4 helper macros for Crypto Extensions
 * Copyright (C) 2022 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#define SM4_PREPARE(ptr)					\
	ld1		{v24.16b-v27.16b}, [ptr], #64;		\
	ld1		{v28.16b-v31.16b}, [ptr];

#define SM4_CRYPT_BLK_BE(b0)					\
	sm4e		b0.4s, v24.4s;				\
	sm4e		b0.4s, v25.4s;				\
	sm4e		b0.4s, v26.4s;				\
	sm4e		b0.4s, v27.4s;				\
	sm4e		b0.4s, v28.4s;				\
	sm4e		b0.4s, v29.4s;				\
	sm4e		b0.4s, v30.4s;				\
	sm4e		b0.4s, v31.4s;				\
	rev64		b0.4s, b0.4s;				\
	ext		b0.16b, b0.16b, b0.16b, #8;		\
	rev32		b0.16b, b0.16b;

#define SM4_CRYPT_BLK(b0)					\
	rev32		b0.16b, b0.16b;				\
	SM4_CRYPT_BLK_BE(b0);

#define SM4_CRYPT_BLK2_BE(b0, b1)				\
	sm4e		b0.4s, v24.4s;				\
	sm4e		b1.4s, v24.4s;				\
	sm4e		b0.4s, v25.4s;				\
	sm4e		b1.4s, v25.4s;				\
	sm4e		b0.4s, v26.4s;				\
	sm4e		b1.4s, v26.4s;				\
	sm4e		b0.4s, v27.4s;				\
	sm4e		b1.4s, v27.4s;				\
	sm4e		b0.4s, v28.4s;				\
	sm4e		b1.4s, v28.4s;				\
	sm4e		b0.4s, v29.4s;				\
	sm4e		b1.4s, v29.4s;				\
	sm4e		b0.4s, v30.4s;				\
	sm4e		b1.4s, v30.4s;				\
	sm4e		b0.4s, v31.4s;				\
	sm4e		b1.4s, v31.4s;				\
	rev64		b0.4s, b0.4s;				\
	rev64		b1.4s, b1.4s;				\
	ext		b0.16b, b0.16b, b0.16b, #8;		\
	ext		b1.16b, b1.16b, b1.16b, #8;		\
	rev32		b0.16b, b0.16b;				\
	rev32		b1.16b, b1.16b;				\

#define SM4_CRYPT_BLK2(b0, b1)					\
	rev32		b0.16b, b0.16b;				\
	rev32		b1.16b, b1.16b;				\
	SM4_CRYPT_BLK2_BE(b0, b1);

#define SM4_CRYPT_BLK4_BE(b0, b1, b2, b3)			\
	sm4e		b0.4s, v24.4s;				\
	sm4e		b1.4s, v24.4s;				\
	sm4e		b2.4s, v24.4s;				\
	sm4e		b3.4s, v24.4s;				\
	sm4e		b0.4s, v25.4s;				\
	sm4e		b1.4s, v25.4s;				\
	sm4e		b2.4s, v25.4s;				\
	sm4e		b3.4s, v25.4s;				\
	sm4e		b0.4s, v26.4s;				\
	sm4e		b1.4s, v26.4s;				\
	sm4e		b2.4s, v26.4s;				\
	sm4e		b3.4s, v26.4s;				\
	sm4e		b0.4s, v27.4s;				\
	sm4e		b1.4s, v27.4s;				\
	sm4e		b2.4s, v27.4s;				\
	sm4e		b3.4s, v27.4s;				\
	sm4e		b0.4s, v28.4s;				\
	sm4e		b1.4s, v28.4s;				\
	sm4e		b2.4s, v28.4s;				\
	sm4e		b3.4s, v28.4s;				\
	sm4e		b0.4s, v29.4s;				\
	sm4e		b1.4s, v29.4s;				\
	sm4e		b2.4s, v29.4s;				\
	sm4e		b3.4s, v29.4s;				\
	sm4e		b0.4s, v30.4s;				\
	sm4e		b1.4s, v30.4s;				\
	sm4e		b2.4s, v30.4s;				\
	sm4e		b3.4s, v30.4s;				\
	sm4e		b0.4s, v31.4s;				\
	sm4e		b1.4s, v31.4s;				\
	sm4e		b2.4s, v31.4s;				\
	sm4e		b3.4s, v31.4s;				\
	rev64		b0.4s, b0.4s;				\
	rev64		b1.4s, b1.4s;				\
	rev64		b2.4s, b2.4s;				\
	rev64		b3.4s, b3.4s;				\
	ext		b0.16b, b0.16b, b0.16b, #8;		\
	ext		b1.16b, b1.16b, b1.16b, #8;		\
	ext		b2.16b, b2.16b, b2.16b, #8;		\
	ext		b3.16b, b3.16b, b3.16b, #8;		\
	rev32		b0.16b, b0.16b;				\
	rev32		b1.16b, b1.16b;				\
	rev32		b2.16b, b2.16b;				\
	rev32		b3.16b, b3.16b;

#define SM4_CRYPT_BLK4(b0, b1, b2, b3)				\
	rev32		b0.16b, b0.16b;				\
	rev32		b1.16b, b1.16b;				\
	rev32		b2.16b, b2.16b;				\
	rev32		b3.16b, b3.16b;				\
	SM4_CRYPT_BLK4_BE(b0, b1, b2, b3);

#define SM4_CRYPT_BLK8_BE(b0, b1, b2, b3, b4, b5, b6, b7)	\
	sm4e		b0.4s, v24.4s;				\
	sm4e		b1.4s, v24.4s;				\
	sm4e		b2.4s, v24.4s;				\
	sm4e		b3.4s, v24.4s;				\
	sm4e		b4.4s, v24.4s;				\
	sm4e		b5.4s, v24.4s;				\
	sm4e		b6.4s, v24.4s;				\
	sm4e		b7.4s, v24.4s;				\
	sm4e		b0.4s, v25.4s;				\
	sm4e		b1.4s, v25.4s;				\
	sm4e		b2.4s, v25.4s;				\
	sm4e		b3.4s, v25.4s;				\
	sm4e		b4.4s, v25.4s;				\
	sm4e		b5.4s, v25.4s;				\
	sm4e		b6.4s, v25.4s;				\
	sm4e		b7.4s, v25.4s;				\
	sm4e		b0.4s, v26.4s;				\
	sm4e		b1.4s, v26.4s;				\
	sm4e		b2.4s, v26.4s;				\
	sm4e		b3.4s, v26.4s;				\
	sm4e		b4.4s, v26.4s;				\
	sm4e		b5.4s, v26.4s;				\
	sm4e		b6.4s, v26.4s;				\
	sm4e		b7.4s, v26.4s;				\
	sm4e		b0.4s, v27.4s;				\
	sm4e		b1.4s, v27.4s;				\
	sm4e		b2.4s, v27.4s;				\
	sm4e		b3.4s, v27.4s;				\
	sm4e		b4.4s, v27.4s;				\
	sm4e		b5.4s, v27.4s;				\
	sm4e		b6.4s, v27.4s;				\
	sm4e		b7.4s, v27.4s;				\
	sm4e		b0.4s, v28.4s;				\
	sm4e		b1.4s, v28.4s;				\
	sm4e		b2.4s, v28.4s;				\
	sm4e		b3.4s, v28.4s;				\
	sm4e		b4.4s, v28.4s;				\
	sm4e		b5.4s, v28.4s;				\
	sm4e		b6.4s, v28.4s;				\
	sm4e		b7.4s, v28.4s;				\
	sm4e		b0.4s, v29.4s;				\
	sm4e		b1.4s, v29.4s;				\
	sm4e		b2.4s, v29.4s;				\
	sm4e		b3.4s, v29.4s;				\
	sm4e		b4.4s, v29.4s;				\
	sm4e		b5.4s, v29.4s;				\
	sm4e		b6.4s, v29.4s;				\
	sm4e		b7.4s, v29.4s;				\
	sm4e		b0.4s, v30.4s;				\
	sm4e		b1.4s, v30.4s;				\
	sm4e		b2.4s, v30.4s;				\
	sm4e		b3.4s, v30.4s;				\
	sm4e		b4.4s, v30.4s;				\
	sm4e		b5.4s, v30.4s;				\
	sm4e		b6.4s, v30.4s;				\
	sm4e		b7.4s, v30.4s;				\
	sm4e		b0.4s, v31.4s;				\
	sm4e		b1.4s, v31.4s;				\
	sm4e		b2.4s, v31.4s;				\
	sm4e		b3.4s, v31.4s;				\
	sm4e		b4.4s, v31.4s;				\
	sm4e		b5.4s, v31.4s;				\
	sm4e		b6.4s, v31.4s;				\
	sm4e		b7.4s, v31.4s;				\
	rev64		b0.4s, b0.4s;				\
	rev64		b1.4s, b1.4s;				\
	rev64		b2.4s, b2.4s;				\
	rev64		b3.4s, b3.4s;				\
	rev64		b4.4s, b4.4s;				\
	rev64		b5.4s, b5.4s;				\
	rev64		b6.4s, b6.4s;				\
	rev64		b7.4s, b7.4s;				\
	ext		b0.16b, b0.16b, b0.16b, #8;		\
	ext		b1.16b, b1.16b, b1.16b, #8;		\
	ext		b2.16b, b2.16b, b2.16b, #8;		\
	ext		b3.16b, b3.16b, b3.16b, #8;		\
	ext		b4.16b, b4.16b, b4.16b, #8;		\
	ext		b5.16b, b5.16b, b5.16b, #8;		\
	ext		b6.16b, b6.16b, b6.16b, #8;		\
	ext		b7.16b, b7.16b, b7.16b, #8;		\
	rev32		b0.16b, b0.16b;				\
	rev32		b1.16b, b1.16b;				\
	rev32		b2.16b, b2.16b;				\
	rev32		b3.16b, b3.16b;				\
	rev32		b4.16b, b4.16b;				\
	rev32		b5.16b, b5.16b;				\
	rev32		b6.16b, b6.16b;				\
	rev32		b7.16b, b7.16b;

#define SM4_CRYPT_BLK8(b0, b1, b2, b3, b4, b5, b6, b7)		\
	rev32		b0.16b, b0.16b;				\
	rev32		b1.16b, b1.16b;				\
	rev32		b2.16b, b2.16b;				\
	rev32		b3.16b, b3.16b;				\
	rev32		b4.16b, b4.16b;				\
	rev32		b5.16b, b5.16b;				\
	rev32		b6.16b, b6.16b;				\
	rev32		b7.16b, b7.16b;				\
	SM4_CRYPT_BLK8_BE(b0, b1, b2, b3, b4, b5, b6, b7);

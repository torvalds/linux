/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Definitions of EC-RDSA Curve Parameters
 *
 * Copyright (c) 2019 Vitaly Chikunov <vt@altlinux.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef _CRYTO_ECRDSA_DEFS_H
#define _CRYTO_ECRDSA_DEFS_H

#include <crypto/internal/ecc.h>

#define ECRDSA_MAX_SIG_SIZE (2 * 512 / 8)
#define ECRDSA_MAX_DIGITS (512 / 64)

/*
 * EC-RDSA uses its own set of curves.
 *
 * cp256{a,b,c} curves first defined for GOST R 34.10-2001 in RFC 4357 (as
 * 256-bit {A,B,C}-ParamSet), but inherited for GOST R 34.10-2012 and
 * proposed for use in R 50.1.114-2016 and RFC 7836 as the 256-bit curves.
 */
/* OID_gostCPSignA 1.2.643.2.2.35.1 */
static u64 cp256a_g_x[] = {
	0x0000000000000001ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull, };
static u64 cp256a_g_y[] = {
	0x22ACC99C9E9F1E14ull, 0x35294F2DDF23E3B1ull,
	0x27DF505A453F2B76ull, 0x8D91E471E0989CDAull, };
static u64 cp256a_p[] = { /* p = 2^256 - 617 */
	0xFFFFFFFFFFFFFD97ull, 0xFFFFFFFFFFFFFFFFull,
	0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull };
static u64 cp256a_n[] = {
	0x45841B09B761B893ull, 0x6C611070995AD100ull,
	0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull };
static u64 cp256a_a[] = { /* a = p - 3 */
	0xFFFFFFFFFFFFFD94ull, 0xFFFFFFFFFFFFFFFFull,
	0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull };
static u64 cp256a_b[] = {
	0x00000000000000a6ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull };

static struct ecc_curve gost_cp256a = {
	.name = "cp256a",
	.g = {
		.x = cp256a_g_x,
		.y = cp256a_g_y,
		.ndigits = 256 / 64,
	},
	.p = cp256a_p,
	.n = cp256a_n,
	.a = cp256a_a,
	.b = cp256a_b
};

/* OID_gostCPSignB 1.2.643.2.2.35.2 */
static u64 cp256b_g_x[] = {
	0x0000000000000001ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull, };
static u64 cp256b_g_y[] = {
	0x744BF8D717717EFCull, 0xC545C9858D03ECFBull,
	0xB83D1C3EB2C070E5ull, 0x3FA8124359F96680ull, };
static u64 cp256b_p[] = { /* p = 2^255 + 3225 */
	0x0000000000000C99ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x8000000000000000ull, };
static u64 cp256b_n[] = {
	0xE497161BCC8A198Full, 0x5F700CFFF1A624E5ull,
	0x0000000000000001ull, 0x8000000000000000ull, };
static u64 cp256b_a[] = { /* a = p - 3 */
	0x0000000000000C96ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x8000000000000000ull, };
static u64 cp256b_b[] = {
	0x2F49D4CE7E1BBC8Bull, 0xE979259373FF2B18ull,
	0x66A7D3C25C3DF80Aull, 0x3E1AF419A269A5F8ull, };

static struct ecc_curve gost_cp256b = {
	.name = "cp256b",
	.g = {
		.x = cp256b_g_x,
		.y = cp256b_g_y,
		.ndigits = 256 / 64,
	},
	.p = cp256b_p,
	.n = cp256b_n,
	.a = cp256b_a,
	.b = cp256b_b
};

/* OID_gostCPSignC 1.2.643.2.2.35.3 */
static u64 cp256c_g_x[] = {
	0x0000000000000000ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull, };
static u64 cp256c_g_y[] = {
	0x366E550DFDB3BB67ull, 0x4D4DC440D4641A8Full,
	0x3CBF3783CD08C0EEull, 0x41ECE55743711A8Cull, };
static u64 cp256c_p[] = {
	0x7998F7B9022D759Bull, 0xCF846E86789051D3ull,
	0xAB1EC85E6B41C8AAull, 0x9B9F605F5A858107ull,
	/* pre-computed value for Barrett's reduction */
	0xedc283cdd217b5a2ull, 0xbac48fc06398ae59ull,
	0x405384d55f9f3b73ull, 0xa51f176161f1d734ull,
	0x0000000000000001ull, };
static u64 cp256c_n[] = {
	0xF02F3A6598980BB9ull, 0x582CA3511EDDFB74ull,
	0xAB1EC85E6B41C8AAull, 0x9B9F605F5A858107ull, };
static u64 cp256c_a[] = { /* a = p - 3 */
	0x7998F7B9022D7598ull, 0xCF846E86789051D3ull,
	0xAB1EC85E6B41C8AAull, 0x9B9F605F5A858107ull, };
static u64 cp256c_b[] = {
	0x000000000000805aull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull, };

static struct ecc_curve gost_cp256c = {
	.name = "cp256c",
	.g = {
		.x = cp256c_g_x,
		.y = cp256c_g_y,
		.ndigits = 256 / 64,
	},
	.p = cp256c_p,
	.n = cp256c_n,
	.a = cp256c_a,
	.b = cp256c_b
};

/* tc512{a,b} curves first recommended in 2013 and then standardized in
 * R 50.1.114-2016 and RFC 7836 for use with GOST R 34.10-2012 (as TC26
 * 512-bit ParamSet{A,B}).
 */
/* OID_gostTC26Sign512A 1.2.643.7.1.2.1.2.1 */
static u64 tc512a_g_x[] = {
	0x0000000000000003ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull, };
static u64 tc512a_g_y[] = {
	0x89A589CB5215F2A4ull, 0x8028FE5FC235F5B8ull,
	0x3D75E6A50E3A41E9ull, 0xDF1626BE4FD036E9ull,
	0x778064FDCBEFA921ull, 0xCE5E1C93ACF1ABC1ull,
	0xA61B8816E25450E6ull, 0x7503CFE87A836AE3ull, };
static u64 tc512a_p[] = { /* p = 2^512 - 569 */
	0xFFFFFFFFFFFFFDC7ull, 0xFFFFFFFFFFFFFFFFull,
	0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull,
	0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull,
	0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, };
static u64 tc512a_n[] = {
	0xCACDB1411F10B275ull, 0x9B4B38ABFAD2B85Dull,
	0x6FF22B8D4E056060ull, 0x27E69532F48D8911ull,
	0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull,
	0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, };
static u64 tc512a_a[] = { /* a = p - 3 */
	0xFFFFFFFFFFFFFDC4ull, 0xFFFFFFFFFFFFFFFFull,
	0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull,
	0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull,
	0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, };
static u64 tc512a_b[] = {
	0x503190785A71C760ull, 0x862EF9D4EBEE4761ull,
	0x4CB4574010DA90DDull, 0xEE3CB090F30D2761ull,
	0x79BD081CFD0B6265ull, 0x34B82574761CB0E8ull,
	0xC1BD0B2B6667F1DAull, 0xE8C2505DEDFC86DDull, };

static struct ecc_curve gost_tc512a = {
	.name = "tc512a",
	.g = {
		.x = tc512a_g_x,
		.y = tc512a_g_y,
		.ndigits = 512 / 64,
	},
	.p = tc512a_p,
	.n = tc512a_n,
	.a = tc512a_a,
	.b = tc512a_b
};

/* OID_gostTC26Sign512B 1.2.643.7.1.2.1.2.2 */
static u64 tc512b_g_x[] = {
	0x0000000000000002ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull, };
static u64 tc512b_g_y[] = {
	0x7E21340780FE41BDull, 0x28041055F94CEEECull,
	0x152CBCAAF8C03988ull, 0xDCB228FD1EDF4A39ull,
	0xBE6DD9E6C8EC7335ull, 0x3C123B697578C213ull,
	0x2C071E3647A8940Full, 0x1A8F7EDA389B094Cull, };
static u64 tc512b_p[] = { /* p = 2^511 + 111 */
	0x000000000000006Full, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x8000000000000000ull, };
static u64 tc512b_n[] = {
	0xC6346C54374F25BDull, 0x8B996712101BEA0Eull,
	0xACFDB77BD9D40CFAull, 0x49A1EC142565A545ull,
	0x0000000000000001ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x8000000000000000ull, };
static u64 tc512b_a[] = { /* a = p - 3 */
	0x000000000000006Cull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x0000000000000000ull,
	0x0000000000000000ull, 0x8000000000000000ull, };
static u64 tc512b_b[] = {
	0xFB8CCBC7C5140116ull, 0x50F78BEE1FA3106Eull,
	0x7F8B276FAD1AB69Cull, 0x3E965D2DB1416D21ull,
	0xBF85DC806C4B289Full, 0xB97C7D614AF138BCull,
	0x7E3E06CF6F5E2517ull, 0x687D1B459DC84145ull, };

static struct ecc_curve gost_tc512b = {
	.name = "tc512b",
	.g = {
		.x = tc512b_g_x,
		.y = tc512b_g_y,
		.ndigits = 512 / 64,
	},
	.p = tc512b_p,
	.n = tc512b_n,
	.a = tc512b_a,
	.b = tc512b_b
};

#endif

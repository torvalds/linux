/*	$NetBSD: t_inttypes.c,v 1.3 2013/10/19 17:44:37 christos Exp $	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <atf-c.h>

ATF_TC_WITHOUT_HEAD(int_fmtio);
ATF_TC_BODY(int_fmtio, tc)
{
	char buf[64];

	int8_t i8 = 0;
	int16_t i16 = 0;
	int32_t i32 = 0;
	int64_t i64 = 0;
	int_least8_t il8 = 0;
	int_least16_t il16 = 0;
	int_least32_t il32 = 0;
	int_least64_t il64 = 0;
	int_fast8_t if8 = 0;
	int_fast16_t if16 = 0;
	int_fast32_t if32 = 0;
	int_fast64_t if64 = 0;
	intmax_t im = 0;
	intptr_t ip = 0;
	uint8_t ui8 = 0;
	uint16_t ui16 = 0;
	uint32_t ui32 = 0;
	uint64_t ui64 = 0;
	uint_least8_t uil8 = 0;
	uint_least16_t uil16 = 0;
	uint_least32_t uil32 = 0;
	uint_least64_t uil64 = 0;
	uint_fast8_t uif8 = 0;
	uint_fast16_t uif16 = 0;
	uint_fast32_t uif32 = 0;
	uint_fast64_t uif64 = 0;
	uintmax_t uim = 0;
	uintptr_t uip = 0;

#define	PRINT(fmt, var) \
	snprintf(buf, sizeof(buf), "%" fmt, var)
#define	SCAN(fmt, var) \
	sscanf(buf, "%" fmt, &var)

	PRINT(PRId8, i8);
	PRINT(PRId16, i16);
	PRINT(PRId32, i32);
	PRINT(PRId64, i64);
	PRINT(PRIdLEAST8, il8);
	PRINT(PRIdLEAST16, il16);
	PRINT(PRIdLEAST32, il32);
	PRINT(PRIdLEAST64, il64);
	PRINT(PRIdFAST8, if8);
	PRINT(PRIdFAST16, if16);
	PRINT(PRIdFAST32, if32);
	PRINT(PRIdFAST64, if64);
	PRINT(PRIdMAX, im);
	PRINT(PRIdPTR, ip);

	PRINT(PRIi8, i8);
	PRINT(PRIi16, i16);
	PRINT(PRIi32, i32);
	PRINT(PRIi64, i64);
	PRINT(PRIiLEAST8, il8);
	PRINT(PRIiLEAST16, il16);
	PRINT(PRIiLEAST32, il32);
	PRINT(PRIiLEAST64, il64);
	PRINT(PRIiFAST8, if8);
	PRINT(PRIiFAST16, if16);
	PRINT(PRIiFAST32, if32);
	PRINT(PRIiFAST64, if64);
	PRINT(PRIiMAX, im);
	PRINT(PRIiPTR, ip);

	PRINT(PRIo8, ui8);
	PRINT(PRIo16, ui16);
	PRINT(PRIo32, ui32);
	PRINT(PRIo64, ui64);
	PRINT(PRIoLEAST8, uil8);
	PRINT(PRIoLEAST16, uil16);
	PRINT(PRIoLEAST32, uil32);
	PRINT(PRIoLEAST64, uil64);
	PRINT(PRIoFAST8, uif8);
	PRINT(PRIoFAST16, uif16);
	PRINT(PRIoFAST32, uif32);
	PRINT(PRIoFAST64, uif64);
	PRINT(PRIoMAX, uim);
	PRINT(PRIoPTR, uip);

	PRINT(PRIu8, ui8);
	PRINT(PRIu16, ui16);
	PRINT(PRIu32, ui32);
	PRINT(PRIu64, ui64);
	PRINT(PRIuLEAST8, uil8);
	PRINT(PRIuLEAST16, uil16);
	PRINT(PRIuLEAST32, uil32);
	PRINT(PRIuLEAST64, uil64);
	PRINT(PRIuFAST8, uif8);
	PRINT(PRIuFAST16, uif16);
	PRINT(PRIuFAST32, uif32);
	PRINT(PRIuFAST64, uif64);
	PRINT(PRIuMAX, uim);
	PRINT(PRIuPTR, uip);

	PRINT(PRIx8, ui8);
	PRINT(PRIx16, ui16);
	PRINT(PRIx32, ui32);
	PRINT(PRIx64, ui64);
	PRINT(PRIxLEAST8, uil8);
	PRINT(PRIxLEAST16, uil16);
	PRINT(PRIxLEAST32, uil32);
	PRINT(PRIxLEAST64, uil64);
	PRINT(PRIxFAST8, uif8);
	PRINT(PRIxFAST16, uif16);
	PRINT(PRIxFAST32, uif32);
	PRINT(PRIxFAST64, uif64);
	PRINT(PRIxMAX, uim);
	PRINT(PRIxPTR, uip);

	PRINT(PRIX8, ui8);
	PRINT(PRIX16, ui16);
	PRINT(PRIX32, ui32);
	PRINT(PRIX64, ui64);
	PRINT(PRIXLEAST8, uil8);
	PRINT(PRIXLEAST16, uil16);
	PRINT(PRIXLEAST32, uil32);
	PRINT(PRIXLEAST64, uil64);
	PRINT(PRIXFAST8, uif8);
	PRINT(PRIXFAST16, uif16);
	PRINT(PRIXFAST32, uif32);
	PRINT(PRIXFAST64, uif64);
	PRINT(PRIXMAX, uim);
	PRINT(PRIXPTR, uip);


	SCAN(SCNd8, i8);
	SCAN(SCNd16, i16);
	SCAN(SCNd32, i32);
	SCAN(SCNd64, i64);
	SCAN(SCNdLEAST8, il8);
	SCAN(SCNdLEAST16, il16);
	SCAN(SCNdLEAST32, il32);
	SCAN(SCNdLEAST64, il64);
	SCAN(SCNdFAST8, if8);
	SCAN(SCNdFAST16, if16);
	SCAN(SCNdFAST32, if32);
	SCAN(SCNdFAST64, if64);
	SCAN(SCNdMAX, im);
	SCAN(SCNdPTR, ip);

	SCAN(SCNi8, i8);
	SCAN(SCNi16, i16);
	SCAN(SCNi32, i32);
	SCAN(SCNi64, i64);
	SCAN(SCNiLEAST8, il8);
	SCAN(SCNiLEAST16, il16);
	SCAN(SCNiLEAST32, il32);
	SCAN(SCNiLEAST64, il64);
	SCAN(SCNiFAST8, if8);
	SCAN(SCNiFAST16, if16);
	SCAN(SCNiFAST32, if32);
	SCAN(SCNiFAST64, if64);
	SCAN(SCNiMAX, im);
	SCAN(SCNiPTR, ip);

	SCAN(SCNo8, ui8);
	SCAN(SCNo16, ui16);
	SCAN(SCNo32, ui32);
	SCAN(SCNo64, ui64);
	SCAN(SCNoLEAST8, uil8);
	SCAN(SCNoLEAST16, uil16);
	SCAN(SCNoLEAST32, uil32);
	SCAN(SCNoLEAST64, uil64);
	SCAN(SCNoFAST8, uif8);
	SCAN(SCNoFAST16, uif16);
	SCAN(SCNoFAST32, uif32);
	SCAN(SCNoFAST64, uif64);
	SCAN(SCNoMAX, uim);
	SCAN(SCNoPTR, uip);

	SCAN(SCNu8, ui8);
	SCAN(SCNu16, ui16);
	SCAN(SCNu32, ui32);
	SCAN(SCNu64, ui64);
	SCAN(SCNuLEAST8, uil8);
	SCAN(SCNuLEAST16, uil16);
	SCAN(SCNuLEAST32, uil32);
	SCAN(SCNuLEAST64, uil64);
	SCAN(SCNuFAST8, uif8);
	SCAN(SCNuFAST16, uif16);
	SCAN(SCNuFAST32, uif32);
	SCAN(SCNuFAST64, uif64);
	SCAN(SCNuMAX, uim);
	SCAN(SCNuPTR, uip);

	SCAN(SCNx8, ui8);
	SCAN(SCNx16, ui16);
	SCAN(SCNx32, ui32);
	SCAN(SCNx64, ui64);
	SCAN(SCNxLEAST8, uil8);
	SCAN(SCNxLEAST16, uil16);
	SCAN(SCNxLEAST32, uil32);
	SCAN(SCNxLEAST64, uil64);
	SCAN(SCNxFAST8, uif8);
	SCAN(SCNxFAST16, uif16);
	SCAN(SCNxFAST32, uif32);
	SCAN(SCNxFAST64, uif64);
	SCAN(SCNxMAX, uim);
	SCAN(SCNxPTR, uip);

#undef SCAN
#undef PRINT
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, int_fmtio);

	return atf_no_error();
}

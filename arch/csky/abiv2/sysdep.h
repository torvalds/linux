/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __SYSDEP_H
#define __SYSDEP_H

#ifdef __ASSEMBLER__

#if defined(__CK860__)
#define LABLE_ALIGN	\
	.balignw 16, 0x6c03

#define PRE_BNEZAD(R)

#define BNEZAD(R, L)	\
	bnezad	R, L
#else
#define LABLE_ALIGN	\
	.balignw 8, 0x6c03

#define PRE_BNEZAD(R)	\
	subi	R, 1

#define BNEZAD(R, L)	\
	bnez	R, L
#endif

#endif

#endif

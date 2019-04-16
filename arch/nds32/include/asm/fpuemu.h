/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2005-2018 Andes Technology Corporation */

#ifndef __ARCH_NDS32_FPUEMU_H
#define __ARCH_NDS32_FPUEMU_H

/*
 * single precision
 */

void fadds(void *ft, void *fa, void *fb);
void fsubs(void *ft, void *fa, void *fb);
void fmuls(void *ft, void *fa, void *fb);
void fdivs(void *ft, void *fa, void *fb);
void fs2d(void *ft, void *fa);
void fsqrts(void *ft, void *fa);
void fnegs(void *ft, void *fa);
int fcmps(void *ft, void *fa, void *fb, int cop);

/*
 * double precision
 */
void faddd(void *ft, void *fa, void *fb);
void fsubd(void *ft, void *fa, void *fb);
void fmuld(void *ft, void *fa, void *fb);
void fdivd(void *ft, void *fa, void *fb);
void fsqrtd(void *ft, void *fa);
void fd2s(void *ft, void *fa);
void fnegd(void *ft, void *fa);
int fcmpd(void *ft, void *fa, void *fb, int cop);

#endif /* __ARCH_NDS32_FPUEMU_H */

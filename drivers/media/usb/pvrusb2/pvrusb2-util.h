/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 */
#ifndef __PVRUSB2_UTIL_H
#define __PVRUSB2_UTIL_H

#define PVR2_DECOMPOSE_LE(t,i,d) \
    do {    \
	(t)[i] = (d) & 0xff;\
	(t)[i+1] = ((d) >> 8) & 0xff;\
	(t)[i+2] = ((d) >> 16) & 0xff;\
	(t)[i+3] = ((d) >> 24) & 0xff;\
    } while(0)

#define PVR2_DECOMPOSE_BE(t,i,d) \
    do {    \
	(t)[i+3] = (d) & 0xff;\
	(t)[i+2] = ((d) >> 8) & 0xff;\
	(t)[i+1] = ((d) >> 16) & 0xff;\
	(t)[i] = ((d) >> 24) & 0xff;\
    } while(0)

#define PVR2_COMPOSE_LE(t,i) \
    ((((u32)((t)[i+3])) << 24) | \
     (((u32)((t)[i+2])) << 16) | \
     (((u32)((t)[i+1])) << 8) | \
     ((u32)((t)[i])))

#define PVR2_COMPOSE_BE(t,i) \
    ((((u32)((t)[i])) << 24) | \
     (((u32)((t)[i+1])) << 16) | \
     (((u32)((t)[i+2])) << 8) | \
     ((u32)((t)[i+3])))


#endif /* __PVRUSB2_UTIL_H */

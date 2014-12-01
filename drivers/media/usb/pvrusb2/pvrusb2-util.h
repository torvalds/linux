/*
 *
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
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

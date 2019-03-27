/*	$NetBSD: pack_dev.h,v 1.7 2008/04/28 20:23:09 martin Exp $	*/

/*-
 * Copyright (c) 1998, 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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

#ifndef	_PACK_DEV_H
#define	_PACK_DEV_H

#ifdef __CYGWIN__
typedef	__dev32_t	portdev_t;
#else
typedef	dev_t		portdev_t;
#endif
typedef	portdev_t pack_t(int, u_long [], const char **);

pack_t	*pack_find(const char *);
pack_t	 pack_native;

#define	major_netbsd(x)		((int32_t)((((x) & 0x000fff00) >>  8)))
#define	minor_netbsd(x)		((int32_t)((((x) & 0xfff00000) >> 12) | \
					   (((x) & 0x000000ff) >>  0)))
#define	makedev_netbsd(x,y)	((dev_t)((((x) <<  8) & 0x000fff00) | \
					 (((y) << 12) & 0xfff00000) | \
					 (((y) <<  0) & 0x000000ff)))

#endif	/* _PACK_DEV_H */

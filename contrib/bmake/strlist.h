/*	$NetBSD: strlist.h,v 1.3 2009/01/16 21:15:34 dsl Exp $	*/

/*-
 * Copyright (c) 2008 - 2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David Laight.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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

#ifndef _STRLIST_H
#define _STRLIST_H

typedef struct {
    char          *si_str;
    unsigned int  si_info;
} strlist_item_t;

typedef struct {
    unsigned int    sl_num;
    unsigned int    sl_max;
    strlist_item_t  *sl_items;
} strlist_t;

void strlist_init(strlist_t *);
void strlist_clean(strlist_t *);
void strlist_add_str(strlist_t *, char *, unsigned int);

#define strlist_num(sl) ((sl)->sl_num)
#define strlist_str(sl, n)  ((sl)->sl_items[n].si_str)
#define strlist_info(sl, n)  ((sl)->sl_items[n].si_info)
#define strlist_set_info(sl, n, v)  ((void)((sl)->sl_items[n].si_info = (v)))

#define STRLIST_FOREACH(v, sl, index) \
    if ((sl)->sl_items != NULL) \
	for (index = 0; (v = strlist_str(sl, index)) != NULL; index++)

#endif /* _STRLIST_H */

/**************************************************************************
 *
 * Copyright (C) 2000-2008 Alacritech, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ALACRITECH, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ALACRITECH, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Alacritech, Inc.
 *
 **************************************************************************/

/*
 * FILENAME: sxg_os.h
 *
 * These are the Linux-specific definitions required for the SLICOSS
 * driver, which should allow for greater portability to other OSes.
 */
#ifndef _SLIC_OS_SPECIFIC_H_
#define _SLIC_OS_SPECIFIC_H_

#define FALSE	(0)
#define TRUE	(1)


typedef struct _LIST_ENTRY {
	struct _LIST_ENTRY *nle_flink;
	struct _LIST_ENTRY *nle_blink;
} list_entry, LIST_ENTRY, *PLIST_ENTRY;

#define InitializeListHead(l)                   \
        (l)->nle_flink = (l)->nle_blink = (l)

#define IsListEmpty(h)                          \
        ((h)->nle_flink == (h))

#define RemoveEntryList(e)                      \
        do {                                    \
                list_entry              *b;     \
                list_entry              *f;     \
                                                \
                f = (e)->nle_flink;             \
                b = (e)->nle_blink;             \
                b->nle_flink = f;               \
                f->nle_blink = b;               \
        } while (0)

/* These two have to be inlined since they return things. */

static __inline PLIST_ENTRY
RemoveHeadList(list_entry *l)
{
        list_entry              *f;
        list_entry              *e;

        e = l->nle_flink;
        f = e->nle_flink;
        l->nle_flink = f;
        f->nle_blink = l;

        return (e);
}

static __inline PLIST_ENTRY
RemoveTailList(list_entry *l)
{
        list_entry              *b;
        list_entry              *e;

        e = l->nle_blink;
        b = e->nle_blink;
        l->nle_blink = b;
        b->nle_flink = l;

        return (e);
}


#define InsertTailList(l, e)                    \
        do {                                    \
                list_entry              *b;     \
                                                \
                b = (l)->nle_blink;             \
                (e)->nle_flink = (l);           \
                (e)->nle_blink = b;             \
                b->nle_flink = (e);             \
                (l)->nle_blink = (e);           \
        } while (0)

#define InsertHeadList(l, e)                    \
        do {                                    \
                list_entry              *f;     \
                                                \
                f = (l)->nle_flink;             \
                (e)->nle_flink = f;             \
                (e)->nle_blink = l;             \
                f->nle_blink = (e);             \
                (l)->nle_flink = (e);           \
        } while (0)


#define ATK_DEBUG  1

#if ATK_DEBUG
#define SLIC_TIMESTAMP(value) {                                             \
        struct timeval  timev;                                              \
        do_gettimeofday(&timev);                                            \
        value = timev.tv_sec*1000000 + timev.tv_usec;                       \
}
#else
#define SLIC_TIMESTAMP(value)
#endif


/******************  SXG DEFINES  *****************************************/

#ifdef  ATKDBG
#define SXG_TIMESTAMP(value) {                                             \
        struct timeval  timev;                                              \
        do_gettimeofday(&timev);                                            \
        value = timev.tv_sec*1000000 + timev.tv_usec;                       \
}
#else
#define SXG_TIMESTAMP(value)
#endif

#define WRITE_REG(reg,value,flush)                  sxg_reg32_write((&reg), (value), (flush))
#define WRITE_REG64(a,reg,value,cpu)                sxg_reg64_write((a),(&reg),(value),(cpu))
#define READ_REG(reg,value)   (value) = readl((void __iomem *)(&reg))

#endif  /* _SLIC_OS_SPECIFIC_H_  */


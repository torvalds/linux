/*	$NetBSD: buf.c,v 1.25 2012/04/24 20:26:58 sjg Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1988, 1989 by Adam de Boor
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef MAKE_NATIVE
static char rcsid[] = "$NetBSD: buf.c,v 1.25 2012/04/24 20:26:58 sjg Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)buf.c	8.1 (Berkeley) 6/6/93";
#else
__RCSID("$NetBSD: buf.c,v 1.25 2012/04/24 20:26:58 sjg Exp $");
#endif
#endif /* not lint */
#endif

/*-
 * buf.c --
 *	Functions for automatically-expanded buffers.
 */

#include    "make.h"
#include    "buf.h"

#ifndef max
#define max(a,b)  ((a) > (b) ? (a) : (b))
#endif

#define BUF_DEF_SIZE	256 	/* Default buffer size */

/*-
 *-----------------------------------------------------------------------
 * Buf_Expand_1 --
 *	Extend buffer for single byte add.
 *
 *-----------------------------------------------------------------------
 */
void
Buf_Expand_1(Buffer *bp)
{
    bp->size += max(bp->size, 16);
    bp->buffer = bmake_realloc(bp->buffer, bp->size);
}

/*-
 *-----------------------------------------------------------------------
 * Buf_AddBytes --
 *	Add a number of bytes to the buffer.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Guess what?
 *
 *-----------------------------------------------------------------------
 */
void
Buf_AddBytes(Buffer *bp, int numBytes, const Byte *bytesPtr)
{
    int count = bp->count;
    Byte *ptr;

    if (__predict_false(count + numBytes >= bp->size)) {
	bp->size += max(bp->size, numBytes + 16);
	bp->buffer = bmake_realloc(bp->buffer, bp->size);
    }

    ptr = bp->buffer + count;
    bp->count = count + numBytes;
    ptr[numBytes] = 0;
    memcpy(ptr, bytesPtr, numBytes);
}

/*-
 *-----------------------------------------------------------------------
 * Buf_GetAll --
 *	Get all the available data at once.
 *
 * Results:
 *	A pointer to the data and the number of bytes available.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
Byte *
Buf_GetAll(Buffer *bp, int *numBytesPtr)
{

    if (numBytesPtr != NULL)
	*numBytesPtr = bp->count;

    return (bp->buffer);
}

/*-
 *-----------------------------------------------------------------------
 * Buf_Empty --
 *	Throw away bytes in a buffer.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The bytes are discarded.
 *
 *-----------------------------------------------------------------------
 */
void
Buf_Empty(Buffer *bp)
{

    bp->count = 0;
    *bp->buffer = 0;
}

/*-
 *-----------------------------------------------------------------------
 * Buf_Init --
 *	Initialize a buffer. If no initial size is given, a reasonable
 *	default is used.
 *
 * Input:
 *	size		Initial size for the buffer
 *
 * Results:
 *	A buffer to be given to other functions in this library.
 *
 * Side Effects:
 *	The buffer is created, the space allocated and pointers
 *	initialized.
 *
 *-----------------------------------------------------------------------
 */
void
Buf_Init(Buffer *bp, int size)
{
    if (size <= 0) {
	size = BUF_DEF_SIZE;
    }
    bp->size = size;
    bp->count = 0;
    bp->buffer = bmake_malloc(size);
    *bp->buffer = 0;
}

/*-
 *-----------------------------------------------------------------------
 * Buf_Destroy --
 *	Nuke a buffer and all its resources.
 *
 * Input:
 *	buf		Buffer to destroy
 *	freeData	TRUE if the data should be destroyed
 *
 * Results:
 *	Data buffer, NULL if freed
 *
 * Side Effects:
 *	The buffer is freed.
 *
 *-----------------------------------------------------------------------
 */
Byte *
Buf_Destroy(Buffer *buf, Boolean freeData)
{
    Byte *data;

    data = buf->buffer;
    if (freeData) {
	free(data);
	data = NULL;
    }

    buf->size = 0;
    buf->count = 0;
    buf->buffer = NULL;

    return data;
}


/*-
 *-----------------------------------------------------------------------
 * Buf_DestroyCompact --
 *	Nuke a buffer and return its data.
 *
 * Input:
 *	buf		Buffer to destroy
 *
 * Results:
 *	Data buffer
 *
 * Side Effects:
 *	If the buffer size is much greater than its content,
 *	a new buffer will be allocated and the old one freed.
 *
 *-----------------------------------------------------------------------
 */
#ifndef BUF_COMPACT_LIMIT
# define BUF_COMPACT_LIMIT 128          /* worthwhile saving */
#endif

Byte *
Buf_DestroyCompact(Buffer *buf)
{
#if BUF_COMPACT_LIMIT > 0
    Byte *data;

    if (buf->size - buf->count >= BUF_COMPACT_LIMIT) {
	/* We trust realloc to be smart */
	data = bmake_realloc(buf->buffer, buf->count + 1);
	if (data) {
	    data[buf->count] = 0;
	    Buf_Destroy(buf, FALSE);
	    return data;
	}
    }
#endif
    return Buf_Destroy(buf, FALSE);
}

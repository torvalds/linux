/*-
 * Copyright (c) 2016 Kai Wang
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "_libpe.h"

ELFTC_VCSID("$Id: libpe_buffer.c 3312 2016-01-10 09:23:51Z kaiwang27 $");

PE_SecBuf *
libpe_alloc_buffer(PE_Scn *ps, size_t sz)
{
	PE_SecBuf *sb;

	if ((sb = malloc(sizeof(PE_SecBuf))) == NULL) {
		errno = ENOMEM;
		return (NULL);
	}

	sb->sb_ps = ps;
	sb->sb_flags = 0;
	sb->sb_pb.pb_align = 1;
	sb->sb_pb.pb_off = 0;
	sb->sb_pb.pb_size = sz;
	if (sz > 0) {
		if ((sb->sb_pb.pb_buf = malloc(sz)) == NULL) {
			free(sb);
			errno = ENOMEM;
			return (NULL);
		}
		sb->sb_flags |= LIBPE_F_BUFFER_MALLOCED;
	} else
		sb->sb_pb.pb_buf = NULL;

	STAILQ_INSERT_TAIL(&ps->ps_b, sb, sb_next);

	return (sb);
}

void
libpe_release_buffer(PE_SecBuf *sb)
{
	PE_Scn *ps;

	assert(sb != NULL);

	ps = sb->sb_ps;

	STAILQ_REMOVE(&ps->ps_b, sb, _PE_SecBuf, sb_next);

	if (sb->sb_flags & LIBPE_F_BUFFER_MALLOCED)
		free(sb->sb_pb.pb_buf);

	free(sb);
}

static int
cmp_sb(PE_SecBuf *a, PE_SecBuf *b)
{

	if (a->sb_pb.pb_off < b->sb_pb.pb_off)
		return (-1);
	else if (a->sb_pb.pb_off == b->sb_pb.pb_off)
		return (0);
	else
		return (1);
}

static void
sort_buffers(PE_Scn *ps)
{

	if (STAILQ_EMPTY(&ps->ps_b))
		return;

	STAILQ_SORT(&ps->ps_b, _PE_SecBuf, sb_next, cmp_sb);
}

size_t
libpe_resync_buffers(PE_Scn *ps)
{
	PE_SecBuf *sb;
	PE_Buffer *pb;
	size_t sz;

	assert(ps->ps_flags & LIBPE_F_LOAD_SECTION);

	sort_buffers(ps);

	sz = 0;
	STAILQ_FOREACH(sb, &ps->ps_b, sb_next) {
		if (ps->ps_flags & PE_F_DIRTY)
			sb->sb_flags |= PE_F_DIRTY;

		pb = (PE_Buffer *) sb;
		if (pb->pb_align > ps->ps_falign)
			pb->pb_align = ps->ps_falign;
		if (pb->pb_buf == NULL || pb->pb_size == 0)
			continue;

		sz = roundup(sz, pb->pb_align);

		if (pb->pb_off != (off_t) sz) {
			pb->pb_off = sz;
			sb->sb_flags |= PE_F_DIRTY;
		}
		sz += pb->pb_size;
	}

	return (sz);
}

int
libpe_write_buffers(PE_Scn *ps)
{
	PE *pe;
	PE_SecBuf *sb;
	PE_Buffer *pb;
	off_t off;

	assert(ps->ps_flags & LIBPE_F_LOAD_SECTION);

	pe = ps->ps_pe;

	off = 0;
	STAILQ_FOREACH(sb, &ps->ps_b, sb_next) {
		pb = &sb->sb_pb;
		if (pb->pb_buf == NULL || pb->pb_size == 0)
			continue;

		if ((sb->sb_flags & PE_F_DIRTY) == 0) {
			assert((pe->pe_flags & LIBPE_F_SPECIAL_FILE) == 0);
			if (lseek(pe->pe_fd, (off_t) pb->pb_size, SEEK_CUR) <
			    0) {
				errno = EIO;
				return (-1);
			}
			goto next_buf;
		}

		if (pb->pb_off > off) {
			if (libpe_pad(pe, pb->pb_off - off) < 0)
				return (-1);
			off = pb->pb_off;
		}

		if (write(pe->pe_fd, pb->pb_buf, pb->pb_size) !=
		    (ssize_t) pb->pb_size) {
			errno = EIO;
			return (-1);
		}

	next_buf:
		off += pb->pb_size;
	}

	return (0);
}

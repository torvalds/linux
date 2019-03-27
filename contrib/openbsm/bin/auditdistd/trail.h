/*-
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	_AUDITDISTD_TRAIL_H_
#define	_AUDITDISTD_TRAIL_H_

#include <stdbool.h>
#include <unistd.h>	/* off_t */

#define	TRAIL_IDENTICAL	0
#define	TRAIL_RENAMED	1
#define	TRAIL_OLDER	2
#define	TRAIL_NEWER	3

struct trail;

struct trail *trail_new(const char *dirname, bool create);
void trail_free(struct trail *trail);
bool trail_is_not_terminated(const char *filename);
bool trail_is_crash_recovery(const char *filename);
void trail_start(struct trail *trail, const char *filename, off_t offset);
void trail_next(struct trail *trail);
void trail_close(struct trail *trail);
void trail_reset(struct trail *trail);
void trail_unlink(struct trail *trail, const char *filename);
bool trail_switch(struct trail *trail);
const char *trail_filename(const struct trail *trail);
int trail_filefd(const struct trail *trail);
int trail_dirfd(const struct trail *trail);
void trail_last(DIR *dirfp, char *filename, size_t filenamesize);
bool trail_validate_name(const char *srcname, const char *dstname);
int trail_name_compare(const char *name0, const char *name1);

#endif	/* !_AUDITDISTD_TRAIL_H_ */

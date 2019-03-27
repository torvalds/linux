/*-
 * Copyright (c) 2011 Michihiro NAKAJIMA
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __LIBARCHIVE_BUILD
#error This header is only to be used internally to libarchive.
#endif

#ifndef ARCHIVE_ENTRY_LOCALE_H_INCLUDED
#define	ARCHIVE_ENTRY_LOCALE_H_INCLUDED

struct archive_entry;
struct archive_string_conv;

/*
 * Utility functions to set and get entry attributes by translating
 * character-set. These are designed for use in format readers and writers.
 *
 * The return code and interface of these are quite different from other
 * functions for archive_entry defined in archive_entry.h.
 * Common return code are:
 *   Return 0 if the string conversion succeeded.
 *   Return -1 if the string conversion failed.
 */

#define archive_entry_gname_l	_archive_entry_gname_l
int _archive_entry_gname_l(struct archive_entry *,
    const char **, size_t *, struct archive_string_conv *);
#define archive_entry_hardlink_l	_archive_entry_hardlink_l
int _archive_entry_hardlink_l(struct archive_entry *,
    const char **, size_t *, struct archive_string_conv *);
#define archive_entry_pathname_l	_archive_entry_pathname_l
int _archive_entry_pathname_l(struct archive_entry *,
    const char **, size_t *, struct archive_string_conv *);
#define archive_entry_symlink_l	_archive_entry_symlink_l
int _archive_entry_symlink_l(struct archive_entry *,
    const char **, size_t *, struct archive_string_conv *);
#define archive_entry_uname_l	_archive_entry_uname_l
int _archive_entry_uname_l(struct archive_entry *,
    const char **, size_t *, struct archive_string_conv *);
#define archive_entry_acl_text_l _archive_entry_acl_text_l
int _archive_entry_acl_text_l(struct archive_entry *, int,
const char **, size_t *, struct archive_string_conv *) __LA_DEPRECATED;
#define archive_entry_acl_to_text_l _archive_entry_acl_to_text_l
char *_archive_entry_acl_to_text_l(struct archive_entry *, ssize_t *, int,
    struct archive_string_conv *);
#define archive_entry_acl_from_text_l _archive_entry_acl_from_text_l
int _archive_entry_acl_from_text_l(struct archive_entry *, const char* text,
    int type, struct archive_string_conv *);
#define archive_entry_copy_gname_l	_archive_entry_copy_gname_l
int _archive_entry_copy_gname_l(struct archive_entry *,
    const char *, size_t, struct archive_string_conv *);
#define archive_entry_copy_hardlink_l	_archive_entry_copy_hardlink_l
int _archive_entry_copy_hardlink_l(struct archive_entry *,
    const char *, size_t, struct archive_string_conv *);
#define archive_entry_copy_link_l	_archive_entry_copy_link_l
int _archive_entry_copy_link_l(struct archive_entry *,
    const char *, size_t, struct archive_string_conv *);
#define archive_entry_copy_pathname_l	_archive_entry_copy_pathname_l
int _archive_entry_copy_pathname_l(struct archive_entry *,
    const char *, size_t, struct archive_string_conv *);
#define archive_entry_copy_symlink_l	_archive_entry_copy_symlink_l
int _archive_entry_copy_symlink_l(struct archive_entry *,
    const char *, size_t, struct archive_string_conv *);
#define archive_entry_copy_uname_l	_archive_entry_copy_uname_l
int _archive_entry_copy_uname_l(struct archive_entry *,
    const char *, size_t, struct archive_string_conv *);

#endif /* ARCHIVE_ENTRY_LOCALE_H_INCLUDED */

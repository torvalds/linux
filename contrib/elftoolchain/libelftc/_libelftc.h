/*-
 * Copyright (c) 2009 Kai Wang
 * Copyright (c) 2007,2008 Hyogeol Lee <hyogeollee@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: _libelftc.h 3531 2017-06-05 05:08:43Z kaiwang27 $
 */

#ifndef	__LIBELFTC_H_
#define	__LIBELFTC_H_

#include <stdbool.h>

#include "_elftc.h"

struct _Elftc_Bfd_Target {
	const char	*bt_name;	/* target name. */
	unsigned int	 bt_type;	/* target type. */
	unsigned int	 bt_byteorder;	/* elf target byteorder. */
	unsigned int	 bt_elfclass;	/* elf target class (32/64bit). */
	unsigned int	 bt_machine;	/* elf target arch. */
	unsigned int	 bt_osabi;	/* elf target abi. */
};

extern struct _Elftc_Bfd_Target _libelftc_targets[];

/** @brief Dynamic vector data for string. */
struct vector_str {
	/** Current size */
	size_t		size;
	/** Total capacity */
	size_t		capacity;
	/** String array */
	char		**container;
};

#define BUFFER_GROWFACTOR	1.618

#define	ELFTC_FAILURE		0
#define	ELFTC_ISDIGIT(C) 	(isdigit((C) & 0xFF))
#define	ELFTC_SUCCESS		1

#define VECTOR_DEF_CAPACITY	8


#ifdef __cplusplus
extern "C" {
#endif
char	*cpp_demangle_ARM(const char *_org);
char	*cpp_demangle_gnu2(const char *_org);
char	*cpp_demangle_gnu3(const char *_org);
bool	is_cpp_mangled_ARM(const char *_org);
bool	is_cpp_mangled_gnu2(const char *_org);
bool	is_cpp_mangled_gnu3(const char *_org);
unsigned int	libelftc_hash_string(const char *);
void	vector_str_dest(struct vector_str *_vec);
int	vector_str_find(const struct vector_str *_vs, const char *_str,
    size_t _len);
char	*vector_str_get_flat(const struct vector_str *_vs, size_t *_len);
bool	vector_str_init(struct vector_str *_vs);
bool	vector_str_pop(struct vector_str *_vs);
bool	vector_str_push(struct vector_str *_vs, const char *_str,
    size_t _len);
bool	vector_str_push_vector(struct vector_str *_dst,
    struct vector_str *_org);
bool	vector_str_push_vector_head(struct vector_str *_dst,
    struct vector_str *_org);
char	*vector_str_substr(const struct vector_str *_vs, size_t _begin,
    size_t _end, size_t *_rlen);
#ifdef __cplusplus
}
#endif

#endif	/* __LIBELFTC_H */

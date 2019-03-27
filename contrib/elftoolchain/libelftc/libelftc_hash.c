/*-
 * Copyright (c) 2013, Joseph Koshy
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
 */

/*
 * An implementation of the Fowler-Noll-Vo hash function.
 *
 * References:
 * - http://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
 * - http://www.isthe.com/chongo/tech/comp/fnv/
 */

#include <sys/types.h>

#include <limits.h>

#include "_libelftc.h"

ELFTC_VCSID("$Id: libelftc_hash.c 2870 2013-01-07 10:38:43Z jkoshy $");

/*
 * Use the size of an 'int' to determine the magic numbers used by the
 * hash function.
 */

#if	INT_MAX == 2147483647UL
#define	FNV_PRIME		16777619UL
#define FNV_OFFSET		2166136261UL
#elif	INT_MAX == 18446744073709551615ULL
#define	FNV_PRIME		1099511628211ULL
#define	FNV_OFFSET		14695981039346656037ULL
#else
#error	sizeof(int) is unknown.
#endif

unsigned int
libelftc_hash_string(const char *s)
{
	char c;
	unsigned int hash;

	for (hash = FNV_OFFSET; (c = *s) != '\0'; s++) {
		hash ^= c;
		hash *= FNV_PRIME;
	}

	return (hash);
}

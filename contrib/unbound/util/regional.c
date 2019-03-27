/*
 * regional.c -- region based memory allocator.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 * 
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 * Regional allocator. Allocates small portions of of larger chunks.
 */

#include "config.h"
#include "util/log.h"
#include "util/regional.h"

#ifdef ALIGNMENT
#  undef ALIGNMENT
#endif
/** increase size until it fits alignment of s bytes */
#define ALIGN_UP(x, s)     (((x) + s - 1) & (~(s - 1)))
/** what size to align on; make sure a char* fits in it. */
#define ALIGNMENT          (sizeof(uint64_t))

/** Default reasonable size for chunks */
#define REGIONAL_CHUNK_SIZE         8192
#ifdef UNBOUND_ALLOC_NONREGIONAL
/** All objects allocated outside of chunks, for debug */
#define REGIONAL_LARGE_OBJECT_SIZE  0
#else
/** Default size for large objects - allocated outside of chunks. */
#define REGIONAL_LARGE_OBJECT_SIZE  2048
#endif

struct regional* 
regional_create(void)
{
	return regional_create_custom(REGIONAL_CHUNK_SIZE);
}

/** init regional struct with first block */
static void
regional_init(struct regional* r)
{
	size_t a = ALIGN_UP(sizeof(struct regional), ALIGNMENT);
	r->data = (char*)r + a;
	r->available = r->first_size - a;
	r->next = NULL;
	r->large_list = NULL;
	r->total_large = 0;
}

struct regional* 
regional_create_custom(size_t size)
{
	struct regional* r = (struct regional*)malloc(size);
	log_assert(sizeof(struct regional) <= size);
	if(!r) return NULL;
	r->first_size = size;
	regional_init(r);
	return r;
}

void 
regional_free_all(struct regional *r)
{
	char* p = r->next, *np;
	while(p) {
		np = *(char**)p;
		free(p);
		p = np;
	}
	p = r->large_list;
	while(p) {
		np = *(char**)p;
		free(p);
		p = np;
	}
	regional_init(r);
}

void 
regional_destroy(struct regional *r)
{
	if(!r) return;
	regional_free_all(r);
	free(r);
}

void *
regional_alloc(struct regional *r, size_t size)
{
	size_t a = ALIGN_UP(size, ALIGNMENT);
	void *s;
	/* large objects */
	if(a > REGIONAL_LARGE_OBJECT_SIZE) {
		s = malloc(ALIGNMENT + size);
		if(!s) return NULL;
		r->total_large += ALIGNMENT+size;
		*(char**)s = r->large_list;
		r->large_list = (char*)s;
		return (char*)s+ALIGNMENT;
	}
	/* create a new chunk */
	if(a > r->available) {
		s = malloc(REGIONAL_CHUNK_SIZE);
		if(!s) return NULL;
		*(char**)s = r->next;
		r->next = (char*)s;
		r->data = (char*)s + ALIGNMENT;
		r->available = REGIONAL_CHUNK_SIZE - ALIGNMENT;
	}
	/* put in this chunk */
	r->available -= a;
	s = r->data;
	r->data += a;
	return s;
}

void *
regional_alloc_init(struct regional* r, const void *init, size_t size)
{
	void *s = regional_alloc(r, size);
	if(!s) return NULL;
	memcpy(s, init, size);
	return s;
}

void *
regional_alloc_zero(struct regional *r, size_t size)
{
	void *s = regional_alloc(r, size);
	if(!s) return NULL;
	memset(s, 0, size);
	return s;
}

char *
regional_strdup(struct regional *r, const char *string)
{
	return (char*)regional_alloc_init(r, string, strlen(string)+1);
}

/**
 * reasonably slow, but stats and get_mem are not supposed to be fast
 * count the number of chunks in use
 */
static size_t
count_chunks(struct regional* r)
{
	size_t c = 1;
	char* p = r->next;
	while(p) {
		c++;
		p = *(char**)p;
	}
	return c;
}

/**
 * also reasonably slow, counts the number of large objects
 */
static size_t
count_large(struct regional* r)
{
	size_t c = 0;
	char* p = r->large_list;
	while(p) {
		c++;
		p = *(char**)p;
	}
	return c;
}

void 
regional_log_stats(struct regional *r)
{
	/* some basic assertions put here (non time critical code) */
	log_assert(ALIGNMENT >= sizeof(char*));
	log_assert(REGIONAL_CHUNK_SIZE > ALIGNMENT);
	log_assert(REGIONAL_CHUNK_SIZE-ALIGNMENT > REGIONAL_LARGE_OBJECT_SIZE);
	log_assert(REGIONAL_CHUNK_SIZE >= sizeof(struct regional));
	/* debug print */
	log_info("regional %u chunks, %u large",
		(unsigned)count_chunks(r), (unsigned)count_large(r));
}

size_t 
regional_get_mem(struct regional* r)
{
	return r->first_size + (count_chunks(r)-1)*REGIONAL_CHUNK_SIZE 
		+ r->total_large;
}

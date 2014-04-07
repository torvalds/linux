/*
 * Copyright (C) 2014 Sergey Senozhatsky.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ZCOMP_H_
#define _ZCOMP_H_

#include <linux/mutex.h>

struct zcomp_strm {
	/* compression/decompression buffer */
	void *buffer;
	/*
	 * The private data of the compression stream, only compression
	 * stream backend can touch this (e.g. compression algorithm
	 * working memory)
	 */
	void *private;
};

/* static compression backend */
struct zcomp_backend {
	int (*compress)(const unsigned char *src, unsigned char *dst,
			size_t *dst_len, void *private);

	int (*decompress)(const unsigned char *src, size_t src_len,
			unsigned char *dst);

	void *(*create)(void);
	void (*destroy)(void *private);

	const char *name;
};

/* dynamic per-device compression frontend */
struct zcomp {
	struct mutex strm_lock;
	struct zcomp_strm *zstrm;
	struct zcomp_backend *backend;
};

struct zcomp *zcomp_create(const char *comp);
void zcomp_destroy(struct zcomp *comp);

struct zcomp_strm *zcomp_strm_find(struct zcomp *comp);
void zcomp_strm_release(struct zcomp *comp, struct zcomp_strm *zstrm);

int zcomp_compress(struct zcomp *comp, struct zcomp_strm *zstrm,
		const unsigned char *src, size_t *dst_len);

int zcomp_decompress(struct zcomp *comp, const unsigned char *src,
		size_t src_len, unsigned char *dst);
#endif /* _ZCOMP_H_ */

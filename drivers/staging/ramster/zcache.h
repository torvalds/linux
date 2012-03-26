/*
 * zcache.h
 *
 * External zcache functions
 *
 * Copyright (c) 2009-2012, Dan Magenheimer, Oracle Corp.
 */

#ifndef _ZCACHE_H_
#define _ZCACHE_H_

extern int zcache_put(int, int, struct tmem_oid *, uint32_t,
			char *, size_t, bool, int);
extern int zcache_autocreate_pool(int, int, bool);
extern int zcache_get(int, int, struct tmem_oid *, uint32_t,
			char *, size_t *, bool, int);
extern int zcache_flush(int, int, struct tmem_oid *, uint32_t);
extern int zcache_flush_object(int, int, struct tmem_oid *);
extern int zcache_localify(int, struct tmem_oid *, uint32_t,
			char *, size_t, void *);

#endif /* _ZCACHE_H */

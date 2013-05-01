/*
 * zbud.h
 *
 * Copyright (c) 2010-2012, Dan Magenheimer, Oracle Corp.
 *
 */

#ifndef _ZBUD_H_
#define _ZBUD_H_

#include "tmem.h"

struct zbudref;

extern unsigned int zbud_max_buddy_size(void);
extern struct zbudref *zbud_match_prep(struct tmem_handle *th, bool eph,
						void *cdata, unsigned size);
extern struct zbudref *zbud_create_prep(struct tmem_handle *th, bool eph,
						void *cdata, unsigned size,
						struct page *newpage);
extern void zbud_create_finish(struct zbudref *, bool);
extern int zbud_decompress(struct page *, struct zbudref *, bool,
				void (*func)(char *, unsigned int, char *));
extern int zbud_copy_from_zbud(char *, struct zbudref *, size_t *, bool);
extern int zbud_copy_to_zbud(struct zbudref *, char *, bool);
extern struct page *zbud_free_and_delist(struct zbudref *, bool eph,
						unsigned int *, unsigned int *);
extern struct page *zbud_evict_pageframe_lru(unsigned int *, unsigned int *);
extern unsigned int zbud_make_zombie_lru(struct tmem_handle *, unsigned char **,
						unsigned int *, bool);
extern void zbud_init(void);

#endif /* _ZBUD_H_ */

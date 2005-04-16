/*
 * Copyright (C) 2004 Red Hat UK Ltd.
 *
 * This file is released under the GPL.
 */

#ifndef DM_BIO_LIST_H
#define DM_BIO_LIST_H

#include <linux/bio.h>

struct bio_list {
	struct bio *head;
	struct bio *tail;
};

static inline void bio_list_init(struct bio_list *bl)
{
	bl->head = bl->tail = NULL;
}

static inline void bio_list_add(struct bio_list *bl, struct bio *bio)
{
	bio->bi_next = NULL;

	if (bl->tail)
		bl->tail->bi_next = bio;
	else
		bl->head = bio;

	bl->tail = bio;
}

static inline void bio_list_merge(struct bio_list *bl, struct bio_list *bl2)
{
	if (bl->tail)
		bl->tail->bi_next = bl2->head;
	else
		bl->head = bl2->head;

	bl->tail = bl2->tail;
}

static inline struct bio *bio_list_pop(struct bio_list *bl)
{
	struct bio *bio = bl->head;

	if (bio) {
		bl->head = bl->head->bi_next;
		if (!bl->head)
			bl->tail = NULL;

		bio->bi_next = NULL;
	}

	return bio;
}

static inline struct bio *bio_list_get(struct bio_list *bl)
{
	struct bio *bio = bl->head;

	bl->head = bl->tail = NULL;

	return bio;
}

#endif

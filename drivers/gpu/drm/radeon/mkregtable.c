// SPDX-License-Identifier: GPL-2.0
/* utility to create the register check tables
 * this includes inlined list.h safe for userspace.
 *
 * Copyright 2009 Jerome Glisse
 * Copyright 2009 Red Hat Inc.
 *
 * Authors:
 * 	Jerome Glisse
 * 	Dave Airlie
 */

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <regex.h>
#include <libgen.h>

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:    the pointer to the member.
 * @type:   the type of the container struct this is embedded in.
 * @member: the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({          \
	const typeof(((type *)0)->member)*__mptr = (ptr);    \
		     (type *)((char *)__mptr - offsetof(type, member)); })

/*
 * Simple doubly linked list implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole lists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */

struct list_head {
	struct list_head *next, *prev;
};


static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
#ifndef CONFIG_DEBUG_LIST
static inline void __list_add(struct list_head *new,
			      struct list_head *prev, struct list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}
#else
extern void __list_add(struct list_head *new,
		       struct list_head *prev, struct list_head *next);
#endif

/**
 * list_add_tail - add a new entry
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}

/**
 * list_entry - get the struct for this entry
 * @ptr:	the &struct list_head pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_head within the struct.
 */
#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)

/**
 * list_for_each_entry	-	iterate over list of given type
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_for_each_entry(pos, head, member)				\
	for (pos = list_entry((head)->next, typeof(*pos), member);	\
	     &pos->member != (head); 	\
	     pos = list_entry(pos->member.next, typeof(*pos), member))

struct offset {
	struct list_head list;
	unsigned offset;
};

struct table {
	struct list_head offsets;
	unsigned offset_max;
	unsigned nentry;
	unsigned *table;
	char *gpu_prefix;
};

static struct offset *offset_new(unsigned o)
{
	struct offset *offset;

	offset = (struct offset *)malloc(sizeof(struct offset));
	if (offset) {
		INIT_LIST_HEAD(&offset->list);
		offset->offset = o;
	}
	return offset;
}

static void table_offset_add(struct table *t, struct offset *offset)
{
	list_add_tail(&offset->list, &t->offsets);
}

static void table_init(struct table *t)
{
	INIT_LIST_HEAD(&t->offsets);
	t->offset_max = 0;
	t->nentry = 0;
	t->table = NULL;
}

static void table_print(struct table *t)
{
	unsigned nlloop, i, j, n, c, id;

	nlloop = (t->nentry + 3) / 4;
	c = t->nentry;
	printf("static const unsigned %s_reg_safe_bm[%d] = {\n", t->gpu_prefix,
	       t->nentry);
	for (i = 0, id = 0; i < nlloop; i++) {
		n = 4;
		if (n > c)
			n = c;
		c -= n;
		for (j = 0; j < n; j++) {
			if (j == 0)
				printf("\t");
			else
				printf(" ");
			printf("0x%08X,", t->table[id++]);
		}
		printf("\n");
	}
	printf("};\n");
}

static int table_build(struct table *t)
{
	struct offset *offset;
	unsigned i, m;

	t->nentry = ((t->offset_max >> 2) + 31) / 32;
	t->table = (unsigned *)malloc(sizeof(unsigned) * t->nentry);
	if (t->table == NULL)
		return -1;
	memset(t->table, 0xff, sizeof(unsigned) * t->nentry);
	list_for_each_entry(offset, &t->offsets, list) {
		i = (offset->offset >> 2) / 32;
		m = (offset->offset >> 2) & 31;
		m = 1 << m;
		t->table[i] ^= m;
	}
	return 0;
}

static char gpu_name[10];
static int parser_auth(struct table *t, const char *filename)
{
	FILE *file;
	regex_t mask_rex;
	regmatch_t match[4];
	char buf[1024];
	size_t end;
	int len;
	int done = 0;
	int r;
	unsigned o;
	struct offset *offset;
	char last_reg_s[10];
	int last_reg;

	if (regcomp
	    (&mask_rex, "(0x[0-9a-fA-F]*) *([_a-zA-Z0-9]*)", REG_EXTENDED)) {
		fprintf(stderr, "Failed to compile regular expression\n");
		return -1;
	}
	file = fopen(filename, "r");
	if (file == NULL) {
		fprintf(stderr, "Failed to open: %s\n", filename);
		return -1;
	}
	fseek(file, 0, SEEK_END);
	end = ftell(file);
	fseek(file, 0, SEEK_SET);

	/* get header */
	if (fgets(buf, 1024, file) == NULL) {
		fclose(file);
		return -1;
	}

	/* first line will contain the last register
	 * and gpu name */
	sscanf(buf, "%9s %9s", gpu_name, last_reg_s);
	t->gpu_prefix = gpu_name;
	last_reg = strtol(last_reg_s, NULL, 16);

	do {
		if (fgets(buf, 1024, file) == NULL) {
			fclose(file);
			return -1;
		}
		len = strlen(buf);
		if (ftell(file) == end)
			done = 1;
		if (len) {
			r = regexec(&mask_rex, buf, 4, match, 0);
			if (r == REG_NOMATCH) {
			} else if (r) {
				fprintf(stderr,
					"Error matching regular expression %d in %s\n",
					r, filename);
				fclose(file);
				return -1;
			} else {
				buf[match[0].rm_eo] = 0;
				buf[match[1].rm_eo] = 0;
				buf[match[2].rm_eo] = 0;
				o = strtol(&buf[match[1].rm_so], NULL, 16);
				offset = offset_new(o);
				table_offset_add(t, offset);
				if (o > t->offset_max)
					t->offset_max = o;
			}
		}
	} while (!done);
	fclose(file);
	if (t->offset_max < last_reg)
		t->offset_max = last_reg;
	return table_build(t);
}

int main(int argc, char *argv[])
{
	struct table t;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <authfile>\n", argv[0]);
		exit(1);
	}
	table_init(&t);
	if (parser_auth(&t, argv[1])) {
		fprintf(stderr, "Failed to parse file %s\n", argv[1]);
		return -1;
	}
	table_print(&t);
	return 0;
}

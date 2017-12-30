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

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
	struct list_head name = LIST_HEAD_INIT(name)

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
 * list_add - add a new entry
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void list_add(struct list_head *new, struct list_head *head)
{
	__list_add(new, head, head->next);
}

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

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_del(struct list_head *prev, struct list_head *next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty() on entry does not return true after this, the entry is
 * in an undefined state.
 */
#ifndef CONFIG_DEBUG_LIST
static inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	entry->next = (void *)0xDEADBEEF;
	entry->prev = (void *)0xBEEFDEAD;
}
#else
extern void list_del(struct list_head *entry);
#endif

/**
 * list_replace - replace old entry by new one
 * @old : the element to be replaced
 * @new : the new element to insert
 *
 * If @old was empty, it will be overwritten.
 */
static inline void list_replace(struct list_head *old, struct list_head *new)
{
	new->next = old->next;
	new->next->prev = new;
	new->prev = old->prev;
	new->prev->next = new;
}

static inline void list_replace_init(struct list_head *old,
				     struct list_head *new)
{
	list_replace(old, new);
	INIT_LIST_HEAD(old);
}

/**
 * list_del_init - deletes entry from list and reinitialize it.
 * @entry: the element to delete from the list.
 */
static inline void list_del_init(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	INIT_LIST_HEAD(entry);
}

/**
 * list_move - delete from one list and add as another's head
 * @list: the entry to move
 * @head: the head that will precede our entry
 */
static inline void list_move(struct list_head *list, struct list_head *head)
{
	__list_del(list->prev, list->next);
	list_add(list, head);
}

/**
 * list_move_tail - delete from one list and add as another's tail
 * @list: the entry to move
 * @head: the head that will follow our entry
 */
static inline void list_move_tail(struct list_head *list,
				  struct list_head *head)
{
	__list_del(list->prev, list->next);
	list_add_tail(list, head);
}

/**
 * list_is_last - tests whether @list is the last entry in list @head
 * @list: the entry to test
 * @head: the head of the list
 */
static inline int list_is_last(const struct list_head *list,
			       const struct list_head *head)
{
	return list->next == head;
}

/**
 * list_empty - tests whether a list is empty
 * @head: the list to test.
 */
static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

/**
 * list_empty_careful - tests whether a list is empty and not being modified
 * @head: the list to test
 *
 * Description:
 * tests whether a list is empty _and_ checks that no other CPU might be
 * in the process of modifying either member (next or prev)
 *
 * NOTE: using list_empty_careful() without synchronization
 * can only be safe if the only activity that can happen
 * to the list entry is list_del_init(). Eg. it cannot be used
 * if another CPU could re-list_add() it.
 */
static inline int list_empty_careful(const struct list_head *head)
{
	struct list_head *next = head->next;
	return (next == head) && (next == head->prev);
}

/**
 * list_is_singular - tests whether a list has just one entry.
 * @head: the list to test.
 */
static inline int list_is_singular(const struct list_head *head)
{
	return !list_empty(head) && (head->next == head->prev);
}

static inline void __list_cut_position(struct list_head *list,
				       struct list_head *head,
				       struct list_head *entry)
{
	struct list_head *new_first = entry->next;
	list->next = head->next;
	list->next->prev = list;
	list->prev = entry;
	entry->next = list;
	head->next = new_first;
	new_first->prev = head;
}

/**
 * list_cut_position - cut a list into two
 * @list: a new list to add all removed entries
 * @head: a list with entries
 * @entry: an entry within head, could be the head itself
 *	and if so we won't cut the list
 *
 * This helper moves the initial part of @head, up to and
 * including @entry, from @head to @list. You should
 * pass on @entry an element you know is on @head. @list
 * should be an empty list or a list you do not care about
 * losing its data.
 *
 */
static inline void list_cut_position(struct list_head *list,
				     struct list_head *head,
				     struct list_head *entry)
{
	if (list_empty(head))
		return;
	if (list_is_singular(head) && (head->next != entry && head != entry))
		return;
	if (entry == head)
		INIT_LIST_HEAD(list);
	else
		__list_cut_position(list, head, entry);
}

static inline void __list_splice(const struct list_head *list,
				 struct list_head *prev, struct list_head *next)
{
	struct list_head *first = list->next;
	struct list_head *last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

/**
 * list_splice - join two lists, this is designed for stacks
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void list_splice(const struct list_head *list,
			       struct list_head *head)
{
	if (!list_empty(list))
		__list_splice(list, head, head->next);
}

/**
 * list_splice_tail - join two lists, each list being a queue
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void list_splice_tail(struct list_head *list,
				    struct list_head *head)
{
	if (!list_empty(list))
		__list_splice(list, head->prev, head);
}

/**
 * list_splice_init - join two lists and reinitialise the emptied list.
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 *
 * The list at @list is reinitialised
 */
static inline void list_splice_init(struct list_head *list,
				    struct list_head *head)
{
	if (!list_empty(list)) {
		__list_splice(list, head, head->next);
		INIT_LIST_HEAD(list);
	}
}

/**
 * list_splice_tail_init - join two lists and reinitialise the emptied list
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 *
 * Each of the lists is a queue.
 * The list at @list is reinitialised
 */
static inline void list_splice_tail_init(struct list_head *list,
					 struct list_head *head)
{
	if (!list_empty(list)) {
		__list_splice(list, head->prev, head);
		INIT_LIST_HEAD(list);
	}
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
 * list_first_entry - get the first element from a list
 * @ptr:	the list head to take the element from.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_head within the struct.
 *
 * Note, that list is expected to be not empty.
 */
#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

/**
 * list_for_each	-	iterate over a list
 * @pos:	the &struct list_head to use as a loop cursor.
 * @head:	the head for your list.
 */
#define list_for_each(pos, head) \
	for (pos = (head)->next; prefetch(pos->next), pos != (head); \
		pos = pos->next)

/**
 * list_for_each_prev	-	iterate over a list backwards
 * @pos:	the &struct list_head to use as a loop cursor.
 * @head:	the head for your list.
 */
#define list_for_each_prev(pos, head) \
	for (pos = (head)->prev; prefetch(pos->prev), pos != (head); \
		pos = pos->prev)

/**
 * list_for_each_safe - iterate over a list safe against removal of list entry
 * @pos:	the &struct list_head to use as a loop cursor.
 * @n:		another &struct list_head to use as temporary storage
 * @head:	the head for your list.
 */
#define list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

/**
 * list_for_each_prev_safe - iterate over a list backwards safe against removal of list entry
 * @pos:	the &struct list_head to use as a loop cursor.
 * @n:		another &struct list_head to use as temporary storage
 * @head:	the head for your list.
 */
#define list_for_each_prev_safe(pos, n, head) \
	for (pos = (head)->prev, n = pos->prev; \
	     prefetch(pos->prev), pos != (head); \
	     pos = n, n = pos->prev)

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

/**
 * list_for_each_entry_reverse - iterate backwards over list of given type.
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_for_each_entry_reverse(pos, head, member)			\
	for (pos = list_entry((head)->prev, typeof(*pos), member);	\
	     prefetch(pos->member.prev), &pos->member != (head); 	\
	     pos = list_entry(pos->member.prev, typeof(*pos), member))

/**
 * list_prepare_entry - prepare a pos entry for use in list_for_each_entry_continue()
 * @pos:	the type * to use as a start point
 * @head:	the head of the list
 * @member:	the name of the list_head within the struct.
 *
 * Prepares a pos entry for use as a start point in list_for_each_entry_continue().
 */
#define list_prepare_entry(pos, head, member) \
	((pos) ? : list_entry(head, typeof(*pos), member))

/**
 * list_for_each_entry_continue - continue iteration over list of given type
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Continue to iterate over list of given type, continuing after
 * the current position.
 */
#define list_for_each_entry_continue(pos, head, member) 		\
	for (pos = list_entry(pos->member.next, typeof(*pos), member);	\
	     prefetch(pos->member.next), &pos->member != (head);	\
	     pos = list_entry(pos->member.next, typeof(*pos), member))

/**
 * list_for_each_entry_continue_reverse - iterate backwards from the given point
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Start to iterate over list of given type backwards, continuing after
 * the current position.
 */
#define list_for_each_entry_continue_reverse(pos, head, member)		\
	for (pos = list_entry(pos->member.prev, typeof(*pos), member);	\
	     prefetch(pos->member.prev), &pos->member != (head);	\
	     pos = list_entry(pos->member.prev, typeof(*pos), member))

/**
 * list_for_each_entry_from - iterate over list of given type from the current point
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Iterate over list of given type, continuing from current position.
 */
#define list_for_each_entry_from(pos, head, member) 			\
	for (; prefetch(pos->member.next), &pos->member != (head);	\
	     pos = list_entry(pos->member.next, typeof(*pos), member))

/**
 * list_for_each_entry_safe - iterate over list of given type safe against removal of list entry
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_entry((head)->next, typeof(*pos), member),	\
		n = list_entry(pos->member.next, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = n, n = list_entry(n->member.next, typeof(*n), member))

/**
 * list_for_each_entry_safe_continue
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Iterate over list of given type, continuing after current point,
 * safe against removal of list entry.
 */
#define list_for_each_entry_safe_continue(pos, n, head, member) 		\
	for (pos = list_entry(pos->member.next, typeof(*pos), member), 		\
		n = list_entry(pos->member.next, typeof(*pos), member);		\
	     &pos->member != (head);						\
	     pos = n, n = list_entry(n->member.next, typeof(*n), member))

/**
 * list_for_each_entry_safe_from
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Iterate over list of given type from current point, safe against
 * removal of list entry.
 */
#define list_for_each_entry_safe_from(pos, n, head, member) 			\
	for (n = list_entry(pos->member.next, typeof(*pos), member);		\
	     &pos->member != (head);						\
	     pos = n, n = list_entry(n->member.next, typeof(*n), member))

/**
 * list_for_each_entry_safe_reverse
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Iterate backwards over list of given type, safe against removal
 * of list entry.
 */
#define list_for_each_entry_safe_reverse(pos, n, head, member)		\
	for (pos = list_entry((head)->prev, typeof(*pos), member),	\
		n = list_entry(pos->member.prev, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = n, n = list_entry(n->member.prev, typeof(*n), member))

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

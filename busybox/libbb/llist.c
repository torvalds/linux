/* vi: set sw=4 ts=4: */
/*
 * linked list helper functions.
 *
 * Copyright (C) 2003 Glenn McGrath
 * Copyright (C) 2005 Vladimir Oleynik
 * Copyright (C) 2005 Bernhard Reutner-Fischer
 * Copyright (C) 2006 Rob Landley <rob@landley.net>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* Add data to the start of the linked list.  */
void FAST_FUNC llist_add_to(llist_t **old_head, void *data)
{
	llist_t *new_head = xmalloc(sizeof(llist_t));

	new_head->data = data;
	new_head->link = *old_head;
	*old_head = new_head;
}

/* Add data to the end of the linked list.  */
void FAST_FUNC llist_add_to_end(llist_t **list_head, void *data)
{
	while (*list_head)
		list_head = &(*list_head)->link;
	*list_head = xzalloc(sizeof(llist_t));
	(*list_head)->data = data;
	/*(*list_head)->link = NULL;*/
}

/* Remove first element from the list and return it */
void* FAST_FUNC llist_pop(llist_t **head)
{
	void *data = NULL;
	llist_t *temp = *head;

	if (temp) {
		data = temp->data;
		*head = temp->link;
		free(temp);
	}
	return data;
}

/* Unlink arbitrary given element from the list */
void FAST_FUNC llist_unlink(llist_t **head, llist_t *elm)
{
	if (!elm)
		return;
	while (*head) {
		if (*head == elm) {
			*head = (*head)->link;
			break;
		}
		head = &(*head)->link;
	}
}

/* Recursively free all elements in the linked list.  If freeit != NULL
 * call it on each datum in the list */
void FAST_FUNC llist_free(llist_t *elm, void (*freeit)(void *data))
{
	while (elm) {
		void *data = llist_pop(&elm);

		if (freeit)
			freeit(data);
	}
}

/* Reverse list order. */
llist_t* FAST_FUNC llist_rev(llist_t *list)
{
	llist_t *rev = NULL;

	while (list) {
		llist_t *next = list->link;

		list->link = rev;
		rev = list;
		list = next;
	}
	return rev;
}

llist_t* FAST_FUNC llist_find_str(llist_t *list, const char *str)
{
	while (list) {
		if (strcmp(list->data, str) == 0)
			break;
		list = list->link;
	}
	return list;
}

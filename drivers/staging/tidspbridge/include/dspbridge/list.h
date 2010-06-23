/*
 * list.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Declarations of list management control structures and definitions
 * of inline list management functions.
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef LIST_
#define LIST_

#include <dspbridge/host_os.h>
#include <linux/list.h>

#define LST_IS_EMPTY(l)      list_empty(&(l)->head)

struct lst_list {
	struct list_head head;
};

/*
 *  ======== lst_first ========
 *  Purpose:
 *      Returns a pointer to the first element of the list, or NULL if the list
 *      is empty.
 *  Parameters:
 *      pList:  Pointer to list control structure.
 *  Returns:
 *      Pointer to first list element, or NULL.
 *  Requires:
 *      - LST initialized.
 *      - pList != NULL.
 *  Ensures:
 */
static inline struct list_head *lst_first(struct lst_list *pList)
{
	if (pList && !list_empty(&pList->head))
		return pList->head.next;
	return NULL;
}

/*
 *  ======== lst_get_head ========
 *  Purpose:
 *      Pops the head off the list and returns a pointer to it.
 *  Details:
 *      If the list is empty, returns NULL.
 *      Else, removes the element at the head of the list, making the next
 *      element the head of the list.
 *      The head is removed by making the tail element of the list point its
 *      "next" pointer at the next element after the head, and by making the
 *      "prev" pointer of the next element after the head point at the tail
 *      element.  So the next element after the head becomes the new head of
 *      the list.
 *  Parameters:
 *      pList:  Pointer to list control structure of list whose head
 *              element is to be removed
 *  Returns:
 *      Pointer to element that was at the head of the list (success)
 *      NULL          No elements in list
 *  Requires:
 *      - LST initialized.
 *      - pList != NULL.
 *  Ensures:
 *  Notes:
 *      Because the tail of the list points forward (its "next" pointer) to
 *      the head of the list, and the head of the list points backward (its
 *      "prev" pointer) to the tail of the list, this list is circular.
 */
static inline struct list_head *lst_get_head(struct lst_list *pList)
{
	struct list_head *elem_list;

	if (!pList || list_empty(&pList->head))
		return NULL;

	elem_list = pList->head.next;
	pList->head.next = elem_list->next;
	elem_list->next->prev = &pList->head;

	return elem_list;
}

/*
 *  ======== lst_init_elem ========
 *  Purpose:
 *      Initializes a list element to default (cleared) values
 *  Details:
 *  Parameters:
 *      elem_list:  Pointer to list element to be reset
 *  Returns:
 *  Requires:
 *      LST initialized.
 *  Ensures:
 *  Notes:
 *      This function must not be called to "reset" an element in the middle
 *      of a list chain -- that would break the chain.
 *
 */
static inline void lst_init_elem(struct list_head *elem_list)
{
	if (elem_list) {
		elem_list->next = NULL;
		elem_list->prev = NULL;
	}
}

/*
 *  ======== lst_insert_before ========
 *  Purpose:
 *     Insert the element before the existing element.
 *  Parameters:
 *      pList:          Pointer to list control structure.
 *      elem_list:          Pointer to element in list to insert.
 *      pElemExisting:  Pointer to existing list element.
 *  Returns:
 *  Requires:
 *      - LST initialized.
 *      - pList != NULL.
 *      - elem_list != NULL.
 *      - pElemExisting != NULL.
 *  Ensures:
 */
static inline void lst_insert_before(struct lst_list *pList,
				     struct list_head *elem_list,
				     struct list_head *pElemExisting)
{
	if (pList && elem_list && pElemExisting)
		list_add_tail(elem_list, pElemExisting);
}

/*
 *  ======== lst_next ========
 *  Purpose:
 *      Returns a pointer to the next element of the list, or NULL if the next
 *      element is the head of the list or the list is empty.
 *  Parameters:
 *      pList:      Pointer to list control structure.
 *      cur_elem:   Pointer to element in list to remove.
 *  Returns:
 *      Pointer to list element, or NULL.
 *  Requires:
 *      - LST initialized.
 *      - pList != NULL.
 *      - cur_elem != NULL.
 *  Ensures:
 */
static inline struct list_head *lst_next(struct lst_list *pList,
					 struct list_head *cur_elem)
{
	if (pList && !list_empty(&pList->head) && cur_elem &&
	    (cur_elem->next != &pList->head))
		return cur_elem->next;
	return NULL;
}

/*
 *  ======== lst_put_tail ========
 *  Purpose:
 *      Adds the specified element to the tail of the list
 *  Details:
 *      Sets new element's "prev" pointer to the address previously held by
 *      the head element's prev pointer.  This is the previous tail member of
 *      the list.
 *      Sets the new head's prev pointer to the address of the element.
 *      Sets next pointer of the previous tail member of the list to point to
 *      the new element (rather than the head, which it had been pointing at).
 *      Sets new element's next pointer to the address of the head element.
 *      Sets head's prev pointer to the address of the new element.
 *  Parameters:
 *      pList:  Pointer to list control structure to which *elem_list will be
 *              added
 *      elem_list:  Pointer to list element to be added
 *  Returns:
 *      Void
 *  Requires:
 *      *elem_list and *pList must both exist.
 *      LST initialized.
 *  Ensures:
 *  Notes:
 *      Because the tail is always "just before" the head of the list (the
 *      tail's "next" pointer points at the head of the list, and the head's
 *      "prev" pointer points at the tail of the list), the list is circular.
 */
static inline void lst_put_tail(struct lst_list *pList,
				struct list_head *elem_list)
{
	if (pList && elem_list)
		list_add_tail(elem_list, &pList->head);
}

/*
 *  ======== lst_remove_elem ========
 *  Purpose:
 *      Removes (unlinks) the given element from the list, if the list is not
 *      empty.  Does not free the list element.
 *  Parameters:
 *      pList:      Pointer to list control structure.
 *      cur_elem:   Pointer to element in list to remove.
 *  Returns:
 *  Requires:
 *      - LST initialized.
 *      - pList != NULL.
 *      - cur_elem != NULL.
 *  Ensures:
 */
static inline void lst_remove_elem(struct lst_list *pList,
				   struct list_head *cur_elem)
{
	if (pList && !list_empty(&pList->head) && cur_elem)
		list_del_init(cur_elem);
}

#endif /* LIST_ */

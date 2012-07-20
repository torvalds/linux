/*
 * Operating system kernel abstraction -- linked lists.
 *
 * Copyright (C) 2009-2010 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */

#include <stddef.h>
#include "list.h"

/**
 * Initialize an empty list.
 *
 * @ingroup list
 */
void os_list_init(struct os_list *list)
{
    list->head.next = list->head.prev = &list->head;
}

/**
 * Is the list empty?
 *
 * @return true iff the list contains no nodes.
 *
 * @ingroup list
 */
int os_list_empty(struct os_list *list)
{
    return list->head.next == &list->head;
}

static void os_list_add(struct os_list_node *prev, struct os_list_node *new,
                        struct os_list_node *next)
{
    next->prev = new;
    new->next  = next;
    new->prev  = prev;
    prev->next = new;
}

/**
 * Add a node to the tail of the list.
 *
 * @param list the list.
 * @param node the list node to add.
 *
 * @ingroup list
 */
void os_list_add_tail(struct os_list *list, struct os_list_node *node)
{
    os_list_add(list->head.prev, node, &list->head);
}

/**
 * Remove a node from a list.
 *
 * @param node the node to remove.
 *
 * @ingroup list
 */
void os_list_del(struct os_list_node *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;

    node->prev = node->next = NULL;
}

/**
 * The node at the head of the list.
 *
 * @param list the list.
 *
 * @return the node at the head of the list; or os_list_end() if the
 * list is empty.
 *
 * @ingroup list
 */
struct os_list_node *os_list_head(struct os_list *list)
{
    return list->head.next;
}

/**
 * The node marking the end of a list.
 *
 * @param list the list.
 *
 * @return the node that marks the end of the list.
 *
 * @ingroup list
 */
struct os_list_node *os_list_end(struct os_list *list)
{
    return &list->head;
}

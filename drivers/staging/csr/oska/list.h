/*
 * Operating system kernel abstraction -- linked lists.
 *
 * Copyright (C) 2009-2010 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_LIST_H
#define __OSKA_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup list Linked Lists
 *
 * Generic linked list implementations suitable for all platforms.
 *
 *   - Circular, doubly-linked list (struct os_list).
 */

/**
 * A list node.
 *
 * This list node structure should be the first field within any
 * structure that is to be stored in a list.
 *
 * @see struct os_list
 * @ingroup list
 */
struct os_list_node {
    /**
     * The pointer to the previous node in the list, or os_list_end()
     * if the end of the list has been reached.
     */
    struct os_list_node *prev;
    /**
     * The pointer to the next node in the list, or os_list_end() if
     * the end of the list has been reached.
     */
    struct os_list_node *next;
};

/**
 * A circular, doubly-linked list of nodes.
 *
 * Structures to be stored in a list should contains a struct
 * os_list_node as the \e first field.
 * \code
 *   struct foo {
 *      struct os_list_node node;
 *      int bar;
 *      ...
 *   };
 * \endcode
 * Going to/from a struct foo to a list node is then simple.
 * \code
 *   struct os_list_node *node;
 *   struct foo *foo;
 *   [...]
 *   node = &foo->node;
 *   foo = (struct foo *)node
 * \endcode
 * Lists must be initialized with os_list_init() before adding nodes
 * with os_list_add_tail().  The node at the head of the list is
 * obtained with os_list_head().  Nodes are removed from the list with
 * os_list_del().
 *
 * A list can be interated from the head to the tail using:
 * \code
 *   struct os_list_node *node;
 *   for (node = os_list_head(list); node != os_list_end(list); node = node->next) {
 *      struct foo *foo = (struct foo *)node;
 *      ...
 *   }
 * \endcode
 *
 * In the above loop, the current list node cannot be removed (with
 * os_list_del()).  If this is required use this form of loop:
 * \code
 *   struct os_list_node *node, *next;
 *   for (node = os_list_head(list), next = node->next;
 *        node != os_list_end(list);
 *        node = next, next = node->next) {
 *      struct foo *foo = (struct foo *)node;
 *      ...
 *      os_list_del(node);
 *      ...
 *   }
 * \endcode
 *
 * @ingroup list
 */
struct os_list {
    /**
     * @internal
     * The dummy node marking the end of the list.
     */
    struct os_list_node head;
};

void os_list_init(struct os_list *list);
int os_list_empty(struct os_list *list);
void os_list_add_tail(struct os_list *list, struct os_list_node *node);
void os_list_del(struct os_list_node *node);
struct os_list_node *os_list_head(struct os_list *list);
struct os_list_node *os_list_end(struct os_list *list);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* #ifndef __OSKA_LIST_H */

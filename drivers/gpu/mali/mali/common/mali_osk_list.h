/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_list.h
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef __MALI_OSK_LIST_H__
#define __MALI_OSK_LIST_H__

#ifdef __cplusplus
extern "C"
{
#endif

MALI_STATIC_INLINE void __mali_osk_list_add(_mali_osk_list_t *new_entry, _mali_osk_list_t *prev, _mali_osk_list_t *next)
{
    next->prev = new_entry;
    new_entry->next = next;
    new_entry->prev = prev;
    prev->next = new_entry;
}

MALI_STATIC_INLINE void __mali_osk_list_del(_mali_osk_list_t *prev, _mali_osk_list_t *next)
{
    next->prev = prev;
    prev->next = next;
}

/** @addtogroup _mali_osk_list
 * @{ */

/** Reference implementations of Doubly-linked Circular Lists are provided.
 * There is often no need to re-implement these.
 *
 * @note The implementation may differ subtly from any lists the OS provides.
 * For this reason, these lists should not be mixed with OS-specific lists
 * inside the OSK/UKK implementation. */

/** @brief Initialize a list element.
 *
 * All list elements must be initialized before use.
 *
 * Do not use on any list element that is present in a list without using
 * _mali_osk_list_del first, otherwise this will break the list.
 *
 * @param list the list element to initialize
 */
MALI_STATIC_INLINE void _mali_osk_list_init( _mali_osk_list_t *list )
{
    list->next = list;
    list->prev = list;
}

/** @brief Insert a single list element after an entry in a list
 *
 * As an example, if this is inserted to the head of a list, then this becomes
 * the first element of the list.
 *
 * Do not use to move list elements from one list to another, as it will break
 * the originating list.
 *
 *
 * @param newlist the list element to insert
 * @param list the list in which to insert. The new element will be the next
 * entry in this list
 */
MALI_STATIC_INLINE void _mali_osk_list_add( _mali_osk_list_t *new_entry, _mali_osk_list_t *list )
{
    __mali_osk_list_add(new_entry, list, list->next);
}

/** @brief Insert a single list element before an entry in a list
 *
 * As an example, if this is inserted to the head of a list, then this becomes
 * the last element of the list.
 *
 * Do not use to move list elements from one list to another, as it will break
 * the originating list.
 *
 * @param newlist the list element to insert
 * @param list the list in which to insert. The new element will be the previous
 * entry in this list
 */
MALI_STATIC_INLINE void _mali_osk_list_addtail( _mali_osk_list_t *new_entry, _mali_osk_list_t *list )
{
    __mali_osk_list_add(new_entry, list->prev, list);
}

/** @brief Remove a single element from a list
 *
 * The element will no longer be present in the list. The removed list element
 * will be uninitialized, and so should not be traversed. It must be
 * initialized before further use.
 *
 * @param list the list element to remove.
 */
MALI_STATIC_INLINE void _mali_osk_list_del( _mali_osk_list_t *list )
{
    __mali_osk_list_del(list->prev, list->next);
}

/** @brief Remove a single element from a list, and re-initialize it
 *
 * The element will no longer be present in the list. The removed list element
 * will initialized, and so can be used as normal.
 *
 * @param list the list element to remove and initialize.
 */
MALI_STATIC_INLINE void _mali_osk_list_delinit( _mali_osk_list_t *list )
{
    __mali_osk_list_del(list->prev, list->next);
    _mali_osk_list_init(list);
}

/** @brief Determine whether a list is empty.
 *
 * An empty list is one that contains a single element that points to itself.
 *
 * @param list the list to check.
 * @return non-zero if the list is empty, and zero otherwise.
 */
MALI_STATIC_INLINE int _mali_osk_list_empty( _mali_osk_list_t *list )
{
    return list->next == list;
}

/** @brief Move a list element from one list to another.
 *
 * The list element must be initialized.
 *
 * As an example, moving a list item to the head of a new list causes this item
 * to be the first element in the new list.
 *
 * @param move the list element to move
 * @param list the new list into which the element will be inserted, as the next
 * element in the list.
 */
MALI_STATIC_INLINE void _mali_osk_list_move( _mali_osk_list_t *move_entry, _mali_osk_list_t *list )
{
    __mali_osk_list_del(move_entry->prev, move_entry->next);
    _mali_osk_list_add(move_entry, list);
}

/** @brief Join two lists
 *
 * The list element must be initialized.
 *
 * Allows you to join a list into another list at a specific location
 *
 * @param list the new list to add
 * @param at the location in a list to add the new list into
 */
MALI_STATIC_INLINE void _mali_osk_list_splice( _mali_osk_list_t *list, _mali_osk_list_t *at )
{
    if (!_mali_osk_list_empty(list))
    {
        /* insert all items from 'list' after 'at'  */
        _mali_osk_list_t *first = list->next;
        _mali_osk_list_t *last = list->prev;
        _mali_osk_list_t *split = at->next;

        first->prev = at;
        at->next = first;

        last->next  = split;
        split->prev = last;
    }
}
/** @} */ /* end group _mali_osk_list */

#ifdef __cplusplus
}
#endif

#endif /* __MALI_OSK_LIST_H__ */

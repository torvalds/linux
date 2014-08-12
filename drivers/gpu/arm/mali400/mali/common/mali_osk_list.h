/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file mali_osk_list.h
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef __MALI_OSK_LIST_H__
#define __MALI_OSK_LIST_H__

#include "mali_osk.h"
#include "mali_kernel_common.h"

#ifdef __cplusplus
extern "C" {
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

/** @addtogroup _mali_osk_list OSK Doubly-Linked Circular Lists
 * @{ */

/** Reference implementations of Doubly-linked Circular Lists are provided.
 * There is often no need to re-implement these.
 *
 * @note The implementation may differ subtly from any lists the OS provides.
 * For this reason, these lists should not be mixed with OS-specific lists
 * inside the OSK/UKK implementation. */

/** @brief Initialize a list to be a head of an empty list
 * @param exp the list to initialize. */
#define _MALI_OSK_INIT_LIST_HEAD(exp) _mali_osk_list_init(exp)

/** @brief Define a list variable, which is uninitialized.
 * @param exp the name of the variable that the list will be defined as. */
#define _MALI_OSK_LIST_HEAD(exp) _mali_osk_list_t exp

/** @brief Define a list variable, which is initialized.
 * @param exp the name of the variable that the list will be defined as. */
#define _MALI_OSK_LIST_HEAD_STATIC_INIT(exp) _mali_osk_list_t exp = { &exp, &exp }

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
MALI_STATIC_INLINE mali_bool _mali_osk_list_empty( _mali_osk_list_t *list )
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

/** @brief Move an entire list
 *
 * The list element must be initialized.
 *
 * Allows you to move a list from one list head to another list head
 *
 * @param old_list The existing list head
 * @param new_list The new list head (must be an empty list)
 */
MALI_STATIC_INLINE void _mali_osk_list_move_list( _mali_osk_list_t *old_list, _mali_osk_list_t *new_list )
{
	MALI_DEBUG_ASSERT(_mali_osk_list_empty(new_list));
	if (!_mali_osk_list_empty(old_list)) {
		new_list->next = old_list->next;
		new_list->prev = old_list->prev;
		new_list->next->prev = new_list;
		new_list->prev->next = new_list;
		old_list->next = old_list;
		old_list->prev = old_list;
	}
}

/** @brief Find the containing structure of a list
 *
 * When traversing a list, this is used to recover the containing structure,
 * given that is contains a _mali_osk_list_t member.
 *
 * Each list must be of structures of one type, and must link the same members
 * together, otherwise it will not be possible to correctly recover the
 * sturctures that the lists link.
 *
 * @note no type or memory checking occurs to ensure that a structure does in
 * fact exist for the list entry, and that it is being recovered with respect
 * to the correct list member.
 *
 * @param ptr the pointer to the _mali_osk_list_t member in this structure
 * @param type the type of the structure that contains the member
 * @param member the member of the structure that ptr points to.
 * @return a pointer to a \a type object which contains the _mali_osk_list_t
 * \a member, as pointed to by the _mali_osk_list_t \a *ptr.
 */
#define _MALI_OSK_LIST_ENTRY(ptr, type, member) \
	_MALI_OSK_CONTAINER_OF(ptr, type, member)

/** @brief Enumerate a list safely
 *
 * With this macro, lists can be enumerated in a 'safe' manner. That is,
 * entries can be deleted from the list without causing an error during
 * enumeration. To achieve this, a 'temporary' pointer is required, which must
 * be provided to the macro.
 *
 * Use it like a 'for()', 'while()' or 'do()' construct, and so it must be
 * followed by a statement or compound-statement which will be executed for
 * each list entry.
 *
 * Upon loop completion, providing that an early out was not taken in the
 * loop body, then it is guaranteed that ptr->member == list, even if the loop
 * body never executed.
 *
 * @param ptr a pointer to an object of type 'type', which points to the
 * structure that contains the currently enumerated list entry.
 * @param tmp a pointer to an object of type 'type', which must not be used
 * inside the list-execution statement.
 * @param list a pointer to a _mali_osk_list_t, from which enumeration will
 * begin
 * @param type the type of the structure that contains the _mali_osk_list_t
 * member that is part of the list to be enumerated.
 * @param member the _mali_osk_list_t member of the structure that is part of
 * the list to be enumerated.
 */
#define _MALI_OSK_LIST_FOREACHENTRY(ptr, tmp, list, type, member)         \
	for (ptr = _MALI_OSK_LIST_ENTRY((list)->next, type, member),      \
	     tmp = _MALI_OSK_LIST_ENTRY(ptr->member.next, type, member);  \
	     &ptr->member != (list);                                      \
	     ptr = tmp,                                                   \
	     tmp = _MALI_OSK_LIST_ENTRY(tmp->member.next, type, member))

/** @brief Enumerate a list in reverse order safely
 *
 * This macro is identical to @ref _MALI_OSK_LIST_FOREACHENTRY, except that
 * entries are enumerated in reverse order.
 *
 * @param ptr a pointer to an object of type 'type', which points to the
 * structure that contains the currently enumerated list entry.
 * @param tmp a pointer to an object of type 'type', which must not be used
 * inside the list-execution statement.
 * @param list a pointer to a _mali_osk_list_t, from which enumeration will
 * begin
 * @param type the type of the structure that contains the _mali_osk_list_t
 * member that is part of the list to be enumerated.
 * @param member the _mali_osk_list_t member of the structure that is part of
 * the list to be enumerated.
 */
#define _MALI_OSK_LIST_FOREACHENTRY_REVERSE(ptr, tmp, list, type, member) \
	for (ptr = _MALI_OSK_LIST_ENTRY((list)->prev, type, member),      \
	     tmp = _MALI_OSK_LIST_ENTRY(ptr->member.prev, type, member);  \
	     &ptr->member != (list);                                      \
	     ptr = tmp,                                                   \
	     tmp = _MALI_OSK_LIST_ENTRY(tmp->member.prev, type, member))

/** @} */ /* end group _mali_osk_list */

#ifdef __cplusplus
}
#endif

#endif /* __MALI_OSK_LIST_H__ */

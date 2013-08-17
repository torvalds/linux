/*
 *
 * (C) COPYRIGHT 2008-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_osk_lists.h
 * Implementation of the OS abstraction layer for the kernel device driver.
 * Note that the OSK list implementation is copied from the CUTILS
 * doubly linked list (DLIST) implementation.
 */

#ifndef _OSK_LISTS_H_
#define _OSK_LISTS_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#include <osk/mali_osk_common.h>

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_osk_api
 * @{
 */

/**
 * @addtogroup osk_dlist Doubly-linked list
 * @{
 */
/**
 * @addtogroup osk_dlist_public Public
 * @{
 */
/**
 * @brief Item of a list
 *
 * @note Can be integrated inside a wider structure.
 */
typedef struct osk_dlist_item
{
	struct
	{
		struct osk_dlist_item *next; /**< @private */
		struct osk_dlist_item *prev; /**< @private */
	}oskp; /**< @private*/
}osk_dlist_item;

/**
 * @brief Doubly-linked list
 */
typedef struct osk_dlist
{
	struct
	{
		struct osk_dlist_item *front; /**< @private */
		struct osk_dlist_item *back; /**< @private */
	}oskp; /**< @private*/
}osk_dlist;

/**
 * @brief Test if @c container_ptr is the back of the list
 *
 * @param [in] container_ptr Pointer to the front of the container to test.
 * @param [in] attribute     Attribute of the container of type @c osk_dlist_item
 *
 * @note An assert is triggered if @a container_ptr is NULL.
 * @note If @c attribute is invalid then the behavior is undefined.
 *
 * @return Returns MALI_TRUE if @c container_ptr is the back of the list.
 */
#define OSK_DLIST_IS_BACK(container_ptr, attribute)\
	(NULL == (OSK_CHECK_PTR(container_ptr))->attribute.oskp.next)

/**
 * @brief Test if @c container_ptr is the front of the list
 *
 * @param [in] container_ptr Pointer to the front of the container to test.
 * @param [in] attribute     Attribute of the container of type @c osk_dlist_item
 *
 * @note An assert is triggered if @a container_ptr is NULL.
 * @note If @c attribute is invalid then the behavior is undefined.
 *
 * @return Returns MALI_TRUE if @c container_ptr is the front of the list.
 */
#define OSK_DLIST_IS_FRONT(container_ptr, attribute)\
	(NULL == (OSK_CHECK_PTR(container_ptr))->attribute.oskp.prev)

/**
 * @brief Test if @c container_ptr is valid
 *
 * @param [in] container_ptr Pointer to the front of the container to test.
 * @param [in] attribute     Attribute of the container of type @c osk_dlist_item
 *
 * @note If @c attribute is invalid then the behavior is undefined.
 *
 * @return Returns MALI_TRUE if @c container_ptr is valid or MALI_FALSE otherwise.
 */
#define OSK_DLIST_IS_VALID(container_ptr, attribute)\
		( NULL != (container_ptr) )

/**
 * @brief Return the next item in the list
 *
 * @param [in] container_ptr Pointer to an item of type @c type
 * @param [in] type          Type of the container
 * @param [in] attribute     Attribute of the container of type @c osk_dlist_item
 *
 * @return A pointer to the next container item, or @c NULL.

 * @note If this macro evaluates as null then the back of the list has been reached.
 * @note An assert is triggered if @a container_ptr is NULL.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */
#define OSK_DLIST_NEXT(container_ptr, type, attribute)\
	( OSK_DLIST_IS_BACK( container_ptr, attribute ) ?\
	NULL :CONTAINER_OF( (container_ptr)->attribute.oskp.next, type, attribute ) )

/**
 * @brief Return MALI_TRUE if the list is empty
 *
 * @param [in] osk_dlist_ptr Pointer to the @c osk_dlist to test.
 *
 * @note An assert is triggered if @a osk_dlist_ptr is NULL.
 *
 * @return Returns MALI_TRUE if @c osk_dlist_ptr is an empty list.
 */
#define OSK_DLIST_IS_EMPTY(osk_dlist_ptr)\
	(NULL == OSK_CHECK_PTR(osk_dlist_ptr)->oskp.front)

/**
 * @brief Return the previous item in the list
 *
 * @param [in] container_ptr Pointer to an item of type @c type
 * @param [in] type          Type of the container
 * @param [in] attribute     Attribute of the container of type @c osk_dlist_item
 *
 * @return A pointer to the previous container item, or @c NULL.

 * @note If this macro evaluates as null then the front of the list has been reached.
 * @note An assert is triggered if @a container_ptr is NULL.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */
#define OSK_DLIST_PREV(container_ptr, type, attribute)\
	( OSK_DLIST_IS_FRONT( container_ptr, attribute ) ?\
	NULL : CONTAINER_OF( (container_ptr)->attribute.oskp.prev, type, attribute) )

/**
 * @brief Return the front container of the list
 *
 * @param [in] osk_dlist_ptr Pointer to a list
 * @param [in] type             Type of the list container
 * @param [in] attribute        Attribute of the container of type @c osk_dlist_item
 *
 * @return A pointer to the front container item, or @c NULL.

 * @note If this macro evaluates as null then the list is empty.
 * @note An assert is triggered if @a osk_dlist_ptr is NULL.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */
#define OSK_DLIST_FRONT(osk_dlist_ptr, type, attribute)\
	( OSK_CHECK_PTR( osk_dlist_ptr )->oskp.front == NULL ?\
	NULL : CONTAINER_OF( (osk_dlist_ptr)->oskp.front, type, attribute ) )

/**
 * @brief Check whether or not @c container_ptr is a member of @c osk_dlist_ptr.
 *
 * @param [in] osk_dlist_ptr                Pointer to a list
 * @param [in] container_ptr                Pointer to the item to check.
 * @param [in] attribute                    Attribute of the container of type @c osk_dlist_item
 *
 * @return MALI_TRUE if @c container_ptr is a member of @c osk_dlist_ptr, MALI_FALSE if not.
 *
 * @note An assert is triggered if @c osk_dlist_ptr is NULL.
 * @note An assert is triggered if @c container_to_remove_ptr is NULL.
 * @note If @c attribute is invalid then the behavior is undefined.
 */
#define OSK_DLIST_MEMBER_OF(osk_dlist_ptr, container_ptr, attribute)\
		oskp_dlist_member_of(osk_dlist_ptr, &(OSK_CHECK_PTR(container_ptr))->attribute)

/**
 * @brief Return the back container of the list
 *
 * @param [in] osk_dlist_ptr Pointer to a list
 * @param [in] type             Type of the list container
 * @param [in] attribute        Attribute of the container of type @c osk_dlist_item
 *
 * @return A pointer to the back container item, or @c NULL.
 *
 * @note If this macro evaluates as null then the list is empty.
 * @note An assert is triggered if @a osk_dlist_ptr is NULL.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */
#define OSK_DLIST_BACK(osk_dlist_ptr, type, attribute)\
	( OSK_CHECK_PTR( osk_dlist_ptr )->oskp.back == NULL ?\
	NULL : 	CONTAINER_OF( (osk_dlist_ptr)->oskp.back, type, attribute) )

/**
 * @brief Initialize a list
 *
 * @param [out] osk_dlist_ptr Pointer to a osk_dlist
 *
 * @note An assert is triggered if @c osk_dlist_ptr is NULL.
 */
#define OSK_DLIST_INIT(osk_dlist_ptr)\
	do\
	{\
		OSK_CHECK_PTR(osk_dlist_ptr); \
		(osk_dlist_ptr)->oskp.front = NULL;	\
		(osk_dlist_ptr)->oskp.back = NULL;\
       	}while(MALI_FALSE)

/**
 * @brief Append @c container_to_insert_ptr at the back of @c osk_dlist_ptr
 *
 * The front and the back of the list are automatically adjusted.
 *
 * @param [in, out] osk_dlist_ptr        Pointer to a list
 * @param [in, out] container_to_insert_ptr Pointer to an item to insert of type @c type.
 * @param [in] type                         Type of the list container
 * @param [in] attribute                    Attribute of the container of type @c osk_dlist_item
 *
 * @note An assert is triggered if @c osk_dlist_ptr is NULL.
 * @note An assert is triggered if @c container_to_insert_ptr is NULL or if it already belongs to the list.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */
#define OSK_DLIST_PUSH_BACK(osk_dlist_ptr, container_to_insert_ptr, type, attribute)\
	OSK_DLIST_INSERT_BEFORE(osk_dlist_ptr, container_to_insert_ptr, NULL, type, attribute)

/**
 * @brief Insert @c container_to_insert_ptr at the front of @c osk_dlist_ptr
 *
 * The front and the back of the list are automatically adjusted.
 *
 * @param [in, out] osk_dlist_ptr        Pointer to a list
 * @param [in, out] container_to_insert_ptr Pointer to an item to insert of type @c type.
 * @param [in] type                         Type of the list container
 * @param [in] attribute                    Attribute of the container of type @c osk_dlist_item
 *
 * @note An assert is triggered if @c osk_dlist_ptr is NULL.
 * @note An assert is triggered if @c container_to_insert_ptr is NULL or if it already belongs to the list.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */
#define OSK_DLIST_PUSH_FRONT(osk_dlist_ptr, container_to_insert_ptr, type, attribute)\
	OSK_DLIST_INSERT_AFTER(osk_dlist_ptr, container_to_insert_ptr, NULL, type, attribute)

 /**
 * @brief Remove the back of @c osk_dlist_ptr and return the element just removed
 *
 * The front and the back of the list are automatically adjusted.
 *
 * @param [in, out] osk_dlist_ptr Pointer to a list
 * @param [in] type                  Type of the list container
 * @param [in] attribute             Attribute of the container of type @c osk_dlist_item
 *
 * @return A pointer to a container item.
 *
 * @note If @c OSK_DLIST_IS_VALID returns MALI_FALSE when testing the returned pointer then the list is empty
 * @note An assert is triggered if @c osk_dlist_ptr is NULL or empty.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */
#define OSK_DLIST_POP_BACK(osk_dlist_ptr, type, attribute, err)\
	CONTAINER_OF(\
		oskp_dlist_remove_debug(\
			osk_dlist_ptr, \
			&OSK_CHECK_PTR( OSK_DLIST_BACK(osk_dlist_ptr, type, attribute) )->attribute, &err), \
		type, \
		attribute)
 /**
 * @brief Remove the front of @c osk_dlist_ptr and return the element just removed
 *
 * The front and the back of the list are automatically adjusted.
 *
 * @note The list must contain at least one item.
 *
 * @param [in, out] osk_dlist_ptr Pointer to a list
 * @param [in] type                  Type of the list container
 * @param [in] attribute             Attribute of the container of type @c osk_dlist_item
 *
 * @return A pointer to a container item.
 *
 * @note An assert is triggered if @c osk_dlist_ptr is NULL or empty.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */
#define OSK_DLIST_POP_FRONT(osk_dlist_ptr, type, attribute, err)\
	CONTAINER_OF(\
		oskp_dlist_remove_debug(\
			osk_dlist_ptr, \
			&OSK_CHECK_PTR( OSK_DLIST_FRONT(osk_dlist_ptr, type, attribute) )->attribute, &err), \
		type, \
		attribute)

/**
 * @brief Append @c container_to_insert_ptr after @c container_pos_ptr in @c osk_dlist_ptr
 *
 * @note Insert the new element at the list front if @c container_pos_ptr is NULL.
 *
 * The front and the back of the list are automatically adjusted.
 *
 * @param [in, out] osk_dlist_ptr        Pointer to a list
 * @param [in, out] container_to_insert_ptr Pointer to an item to insert of type @c type.
 * @param [in, out] container_pos_ptr       Pointer to the item of type @c type after which inserting the new item.
 * @param [in] type                         Type of the list container
 * @param [in] attribute                    Attribute of the container of type @c osk_dlist_item
 *
 * @note An assert is triggered if @c osk_dlist_ptr is NULL.
 * @note An assert is triggered if @c container_pos_ptr is not NULL and not a member of the list.
 * @note An assert is triggered if @c container_to_insert_ptr is NULL or if it already belongs to the list.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */
#define OSK_DLIST_INSERT_AFTER(osk_dlist_ptr, container_to_insert_ptr, container_pos_ptr, type, attribute)\
		 oskp_dlist_insert_after(\
				osk_dlist_ptr, \
				&(OSK_CHECK_PTR(container_to_insert_ptr))->attribute,	\
				&((type*)container_pos_ptr)->attribute,	\
				container_pos_ptr)
/**
 * @brief Append @c container_to_insert_ptr before @c container_pos_ptr in @c osk_dlist_ptr
 *
 * @note Insert the new element at the list back if @c container_pos_ptr is NULL.
 *
 * The front and the back of the list are automatically adjusted.
 *
 * @param [in, out] osk_dlist_ptr        Pointer to a list
 * @param [in, out] container_to_insert_ptr Pointer to an item to insert of type @c type.
 * @param [in, out] container_pos_ptr       Pointer to the item of type @c type before which inserting the new item.
 * @param [in] type                         Type of the list container
 * @param [in] attribute                    Attribute of the container of type @c osk_dlist_item
 *
 * @note An assert is triggered if @c osk_dlist_ptr is NULL.
 * @note An assert is triggered if @c container_pos_ptr is not NULL and not a member of the list.
 * @note An assert is triggered if @c container_to_insert_ptr is NULL or if it already belongs to the list.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */

#define OSK_DLIST_INSERT_BEFORE(osk_dlist_ptr, container_to_insert_ptr, container_pos_ptr, type, attribute)\
		 oskp_dlist_insert_before(\
				osk_dlist_ptr, \
				&(OSK_CHECK_PTR(container_to_insert_ptr))->attribute,	\
				&((type*)container_pos_ptr)->attribute, \
				container_pos_ptr)

/**
 * @brief Remove an item container from a doubly-linked list and return a pointer to the element
 * which was next in the list.
 *
 * The front and the back of the list are automatically adjusted.
 *
 * @param [in, out] osk_dlist_ptr        Pointer to a list
 * @param [in, out] container_to_remove_ptr Pointer to an item to remove of type @c type.
 * @param [in] type                         Type of the list container
 * @param [in] attribute                    Attribute of the container of type @c osk_dlist_item
 *
 * @return A pointer to the item container that was immediately after the one
 *         removed from the list, or @c NULL.
 *
 * @note If this macro evaluates as null then the back of the list has been reached.
 * @note An assert is triggered if @c osk_dlist_ptr is NULL.
 * @note An assert is triggered if @c container_to_remove_ptr is NULL or if it doesn't belong to the list.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.

 * @pre @p osk_dlist_ptr must have been initialized by @ref OSK_DLIST_INIT.
 * @pre @p container_to_remove_ptr must be a member of list @p osk_dlist_ptr.
 * @post @p container_to_remove_ptr is no longer a member of list @p osk_dlist_ptr.
 *
 */

#define OSK_DLIST_REMOVE_AND_RETURN_NEXT(osk_dlist_ptr, container_to_remove_ptr, type, attribute)\
		( OSK_DLIST_IS_BACK( container_to_remove_ptr, attribute ) ?\
		  ( oskp_dlist_remove( osk_dlist_ptr, &( container_to_remove_ptr )->attribute ), NULL ) :\
		  CONTAINER_OF( oskp_dlist_remove_and_return_next( osk_dlist_ptr,\
		                &( container_to_remove_ptr )->attribute ),\
		                type,\
		                attribute ) )

/**
 * @brief Remove an item container from a doubly-linked list.
 *
 * The front and the back of the list are automatically adjusted.
 *
 * @param [in, out] osk_dlist_ptr        Pointer to a list
 * @param [in, out] container_to_remove_ptr Pointer to an item to remove of type @c type.
 * @param [in] attribute                    Attribute of the container of type @c osk_dlist_item
 *
 * @note An assert error is triggered if @c osk_dlist_ptr is NULL.
 * @note An assert error is triggered if @c container_to_remove_ptr is NULL or if it doesn't belong to the list.
 * @note If @c attribute is invalid then the behavior is undefined.
 *
 * @pre @p osk_dlist_ptr must have been initialized by @ref OSK_DLIST_INIT.
 * @pre @p container_to_remove_ptr must be a member of list @p osk_dlist_ptr.
 * @post @p container_to_remove_ptr is no longer a member of list @p osk_dlist_ptr.
 */
#define OSK_DLIST_REMOVE(osk_dlist_ptr, container_to_remove_ptr, attribute, error)\
	oskp_dlist_remove_item_debug(osk_dlist_ptr, &((OSK_CHECK_PTR(container_to_remove_ptr))->attribute), &error)

/**
 * @brief Remove an item container from a doubly-linked list and return a pointer to the element which was the
 * previous one in the list.
 *
 * The front and the back of the list are automatically adjusted.
 *
 * @param [in, out] osk_dlist_ptr        Pointer to a list
 * @param [in, out] container_to_remove_ptr Pointer to an item to remove of type @c type.
 * @param [in] type                         Type of the list container
 * @param [in] attribute                    Attribute of the container of type @c osk_dlist_item
 *
 * @return A pointer to the item container that was immediately before the one
 *         removed from the list, or @c NULL.
 *
 * @note If this macro evaluates as null then the front of the list has been reached.
 * @note An assert is triggered if @c osk_dlist_ptr is NULL.
 * @note An assert is triggered if @c container_to_remove_ptr is NULL or if it doesn't belong to the list.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 *
 * @pre @p osk_dlist_ptr must have been initialized by @ref OSK_DLIST_INIT.
 * @pre @p container_to_remove_ptr must be a member of list @p osk_dlist_ptr.
 * @post @p container_to_remove_ptr is no longer a member of list @p osk_dlist_ptr.
 */

#define OSK_DLIST_REMOVE_AND_RETURN_PREV(osk_dlist_ptr, container_to_remove_ptr, type, attribute)\
	( OSK_DLIST_IS_FRONT( container_to_remove_ptr, attribute ) ?\
	  ( oskp_dlist_remove( osk_dlist_ptr, &( container_to_remove_ptr )->attribute ), NULL ) :\
	  CONTAINER_OF( oskp_dlist_remove_and_return_prev( osk_dlist_ptr,\
			  	  	&( container_to_remove_ptr )->attribute ),\
					type,\
					attribute ) )


/**
 * @brief Remove and call the destructor function for every item in the list, walking from start to end.
 *
 * @param [in, out] osk_dlist_ptr Pointer to the list to empty
 * @param [in] type                  Type of the list container.
 * @param [in] attribute             Attribute of the container of type @c osk_dlist_item
 * @param [in] destructor_func       Destructor function called for every item present in the list.
 *
 * This function has to be of the form void func(type* item);
 *
 * @note An assert is triggered if @c osk_dlist_ptr is NULL.
 * @note An assert is triggered if @c destructor_func is NULL.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */
#define OSK_DLIST_EMPTY_LIST(osk_dlist_ptr, type, attribute, destructor_func)\
	do\
	{\
		type* oskp_it;\
		OSK_ASSERT(NULL != osk_dlist_ptr); \
		OSK_ASSERT(NULL != destructor_func); \
		oskp_it = OSK_DLIST_FRONT(osk_dlist_ptr, type, attribute);\
		while ( oskp_it != NULL )\
		{\
			type* to_delete = oskp_it;\
			oskp_it = OSK_DLIST_REMOVE_AND_RETURN_NEXT(osk_dlist_ptr, oskp_it, type, attribute);\
			destructor_func(to_delete);\
		}\
	}while(MALI_FALSE)

/**
 * @brief Remove and call the destructor function for every item in the list, walking from the end and to the front.
 *
 * @param [in, out] osk_dlist_ptr Pointer to the list to empty
 * @param [in] type                  Type of the list container.
 * @param [in] attribute             Attribute of the container of type @c osk_dlist_item
 * @param [in] destructor_func       Destructor function called for every item present in the list.
 *
 * This function has to be of the form void func(type* item);
 *
 * @note An assert is triggered if @c osk_dlist_ptr is NULL.
 * @note An assert is triggered if @c destructor_func is NULL.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */

#define OSK_DLIST_EMPTY_LIST_REVERSE(osk_dlist_ptr, type, attribute, destructor_func)\
	do\
	{\
		type* oskp_it;\
		OSK_ASSERT(NULL != osk_dlist_ptr); \
		OSK_ASSERT(NULL != destructor_func); \
		oskp_it = OSK_DLIST_BACK(osk_dlist_ptr, type, attribute);\
		while ( oskp_it != NULL )\
		{\
			type* to_delete = oskp_it;\
			oskp_it = OSK_DLIST_REMOVE_AND_RETURN_PREV(osk_dlist_ptr, oskp_it, type, attribute);\
			destructor_func(to_delete);\
		}\
	}while(MALI_FALSE)



/**
 * @brief Iterate forward through each container item of the given list
 *
 * @param [in, out] osk_dlist_ptr Pointer to a list
 * @param [in] type                  Container type of the list
 * @param [in] attribute             Attribute of the container of type @c osk_dlist_item
 * @param [out] container_iterator   Iterator variable of type "type*" to use to iterate through the list.
 *
 * @note An assert is triggered if @c osk_dlist_ptr is NULL.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */
#define OSK_DLIST_FOREACH(osk_dlist_ptr, type, attribute, container_iterator)\
	OSK_ASSERT(NULL != osk_dlist_ptr); \
	for(\
			container_iterator = OSK_DLIST_FRONT(osk_dlist_ptr, type, attribute);\
			NULL != container_iterator;	\
			container_iterator = OSK_DLIST_NEXT(container_iterator, type, attribute))

/**
 * @brief Reverse iterate through each container item of the given list
 *
 * @param [in, out] osk_dlist_ptr Pointer to a list
 * @param [in] type                  Container type of the list
 * @param [in] attribute             Attribute of the container of type @c osk_dlist_item
 * @param [out] container_iterator   Iterator variable of type "type*" to use to iterate through the list.
 *
 * @note An assert is triggered if @c osk_dlist_ptr is NULL.
 * @note If @c type or @c attribute is invalid then the behavior is undefined.
 */
#define OSK_DLIST_FOREACH_REVERSE(osk_dlist_ptr, type, attribute, container_iterator)\
	OSK_ASSERT(NULL != osk_dlist_ptr); \
	for(\
			container_iterator = OSK_DLIST_BACK(osk_dlist_ptr, type, attribute);\
			NULL != container_iterator;	\
			container_iterator = OSK_DLIST_PREV(container_iterator, type, attribute))

/**
 * @}
 */
/* End osk_dlist_public */
/**
 * @addtogroup osk_dlist_private Private
 * @{
 */

/**
 * @brief Insert a new item after an existing one.
 *
 * @param [in, out] list_ptr       Pointer to the list the new item is going to be added to.
 * @param [in, out] item_to_insert New item to insert in the list.
 * @param [in, out] position       Position after which to add the new item.
 * @param [in] previous            If this argument is NULL then @c position is ignored and the
 *                                 new item is added to the front.
 *
 * @note An assert is triggered if @c list_ptr is NULL.
 * @note An assert is triggered if @c previous is NULL and @c position is NULL.
 */
OSK_STATIC_INLINE void oskp_dlist_insert_after(osk_dlist * const list_ptr, osk_dlist_item * const item_to_insert,
	osk_dlist_item * const position, const void * previous);

/**
 * @brief Insert a new item before an existing one.
 *
 * @param [in, out] list_ptr       Pointer to the list the new item is going to be added to.
 * @param [in, out] item_to_insert New item to insert in the list.
 * @param [in, out] position       Position before which to add the new item.
 * @param [in] previous            If this argument is NULL then @c position is ignored and the new
 *                                 item is added to the back
 *
 * @note An assert is triggered if @c list_ptr is NULL.
 * @note An assert is triggered if @c previous is NULL and @c position is NULL.
 */

OSK_STATIC_INLINE void oskp_dlist_insert_before(osk_dlist * const list_ptr, osk_dlist_item* const item_to_insert,
	osk_dlist_item * const position, const void * previous);

/**
 * @brief Remove a given item from the list and return the item which was next in the list
 *
 * @param [in, out] list_ptr       List from which the item needs to be removed
 * @param [in, out] item_to_remove Item to remove from the list
 *
 * @return A pointer to the item which was next in the list. Return NULL if the back has just been removed.
 *
 * @note An assert is triggered if @c list_ptr is NULL.
 * @note An assert is triggered if @c item_to_remove is not a member of @c list_ptr

 */
OSK_STATIC_INLINE osk_dlist_item* oskp_dlist_remove_and_return_next(osk_dlist * const list_ptr,
	osk_dlist_item * const item_to_remove) CHECK_RESULT;

/**
 * @brief Remove a given item from the list and return the item which was before in the list
 *
 * @param [in, out] list_ptr       List from which the item needs to be removed
 * @param [in, out] item_to_remove Item to remove from the list
 *
 * @return A pointer to the item which was before in the list. Return NULL if the front has just been removed.
 *
 * @note An assert is triggered if @c list_ptr is NULL.
 * @note An assert is triggered if @c item_to_remove is not a member of @c list_ptr
 */
OSK_STATIC_INLINE osk_dlist_item* oskp_dlist_remove_and_return_prev(osk_dlist * const list_ptr,
	osk_dlist_item * const item_to_remove) CHECK_RESULT;

/**
 * @brief Remove a given item from the list and return it.
 *
 * @param [in, out] list_ptr       List from which the item needs to be removed
 * @param [in, out] item_to_remove Item to remove from the list
 *
 * @return A pointer to the item which has been removed from the list.
 *
 * @note An assert is triggered if @c list_ptr is NULL.
 * @note An assert is triggered if @c item_to_remove is not a member of @c list_ptr
 */

OSK_STATIC_INLINE osk_dlist_item* oskp_dlist_remove(osk_dlist * const list_ptr,
	osk_dlist_item * const item_to_remove);

OSK_STATIC_INLINE osk_dlist_item* oskp_dlist_remove_debug(osk_dlist * const list_ptr,
	osk_dlist_item * const item_to_remove, int *err);

/**
 * @brief Check that @c item  is a member of the @c list
 *
 * @param [in] list Metadata of the list
 * @param [in] item Item to check
 *
 * @note An assert error is triggered if @c list is NULL.
 *
 * @return MALI_TRUE if @c item is a member of @c list or MALI_FALSE otherwise.
 */
OSK_STATIC_INLINE mali_bool oskp_dlist_member_of(const osk_dlist* const list, const osk_dlist_item* const item) CHECK_RESULT;

/**
 * @brief remove @c item_to_remove from @c front
 *
 * @param [in, out] front List from which the item needs to be removed
 * @param [in, out] item_to_remove Item to remove from the list.
 *
 * @note An assert is triggered if @c list_ptr is NULL.
 * @note An assert is triggered if @c item_to_remove is not a member of @c list_ptr
 */
OSK_STATIC_INLINE void oskp_dlist_remove_item(osk_dlist* const front, osk_dlist_item* const item_to_remove);

OSK_STATIC_INLINE void oskp_dlist_remove_item_debug(osk_dlist* const front, osk_dlist_item* const item_to_remove, int *error);

/**
 * @}
 */
/* end osk_dlist_private */
/**
 * @}
 */
/* end osk_dlist group */

/**
 * @addtogroup osk_dlist Doubly-linked list
 * @{
 */
/**
 * @addtogroup osk_dlist_private Private
 * @{
 */

CHECK_RESULT OSK_STATIC_INLINE mali_bool oskp_dlist_member_of(const osk_dlist* const list, const osk_dlist_item* const item)
{
	mali_bool return_value = MALI_FALSE;
	const osk_dlist_item* it;

	OSK_ASSERT(NULL != list);

	it = list->oskp.front;
	while(NULL != it)
	{
		if(item == it)
		{
			return_value = MALI_TRUE;
			break;
		}

		it = it->oskp.next;
	}
	return return_value;
}

OSK_STATIC_INLINE void oskp_dlist_insert_before(osk_dlist * const front, osk_dlist_item * const item_to_insert,
	osk_dlist_item * const position, const void * previous)
{
	OSK_ASSERT(NULL != front);
	OSK_ASSERT(NULL != item_to_insert);
	OSK_ASSERT((NULL == previous) || (NULL != position));
	OSK_ASSERT(MALI_FALSE == oskp_dlist_member_of(front, item_to_insert));

	if(NULL == previous)
	{
		item_to_insert->oskp.prev = front->oskp.back;

		/*if there are some other items in the list, update their links.*/
		if(NULL != front->oskp.back)
		{
			front->oskp.back->oskp.next = item_to_insert;
		}
		item_to_insert->oskp.next = NULL;
		front->oskp.back = item_to_insert;
	}
	else
	{
		/* insertion at a position which is not the back*/
		OSK_ASSERT(MALI_FALSE != oskp_dlist_member_of(front, position));

		item_to_insert->oskp.prev = position->oskp.prev;
		item_to_insert->oskp.next = position;
		position->oskp.prev = item_to_insert;

		/*if there are some other items in the list, update their links.*/
		if(NULL != item_to_insert->oskp.prev)
		{
			item_to_insert->oskp.prev->oskp.next = item_to_insert;
		}

	}

	/* Did the element inserted became the new front */
	if(front->oskp.front == item_to_insert->oskp.next)
	{
		front->oskp.front = item_to_insert;
	}
}

OSK_STATIC_INLINE
void oskp_dlist_insert_after(osk_dlist * const front, osk_dlist_item * const item_to_insert,
	osk_dlist_item * const position, const void * previous)
{
	OSK_ASSERT(NULL != front);
	OSK_ASSERT(NULL != item_to_insert);
	OSK_ASSERT((NULL == previous) || (NULL != position));
	OSK_ASSERT(MALI_FALSE == oskp_dlist_member_of(front, item_to_insert));

	if(NULL == previous)
	{
		item_to_insert->oskp.next = front->oskp.front;

		/*if there are some other items in the list, update their links.*/
		if(NULL != front->oskp.front)
		{
			front->oskp.front->oskp.prev = item_to_insert;
		}
		item_to_insert->oskp.prev = NULL;
		front->oskp.front = item_to_insert;
	}
	else
	{
		/* insertion at a position which is not the front */
		OSK_ASSERT(MALI_FALSE != oskp_dlist_member_of(front, position));

		item_to_insert->oskp.next = position->oskp.next;
		item_to_insert->oskp.prev = position;
		position->oskp.next = item_to_insert;

		/*if the item has not been inserted at the back, then update the links of the next item*/
		if(NULL != item_to_insert->oskp.next)
		{
			item_to_insert->oskp.next->oskp.prev = item_to_insert;
		}
	}

	/* Is the item inserted the new back ?*/
	if(front->oskp.back == item_to_insert->oskp.prev)
	{
		front->oskp.back = item_to_insert;
	}
}

OSK_STATIC_INLINE
void oskp_dlist_remove_item_debug(osk_dlist* const front, osk_dlist_item* const item_to_remove, int *error)
{
	OSK_ASSERT(NULL != front);
	OSK_ASSERT(NULL != item_to_remove);
	OSK_ASSERT(MALI_TRUE == oskp_dlist_member_of(front, item_to_remove));

	/* if the item to remove is the current front*/
	if( front->oskp.front == item_to_remove )
	{
		/* then make the front point to the next item*/
		front->oskp.front = item_to_remove->oskp.next;
	}
	else
	{
		/* else just the previous item point to the next one*/
		item_to_remove->oskp.prev->oskp.next = item_to_remove->oskp.next;
	}

	/* if the item to remove is the current back*/
	if(front->oskp.back == item_to_remove)
	{
		/* then make the back point to the previous item*/
		front->oskp.back = item_to_remove->oskp.prev;
	}
	else
	{
		/* else just the next item point to the previous one*/
		if (!item_to_remove->oskp.next)
		{
			osk_dlist_item * item;
			int i;

			/* bad node, log the contents of the list */

			printk(KERN_ERR "Removing bad node %p, dumping list, forward walk\n", item_to_remove);
			item = front->oskp.front;
			i = 0;
			while (item)
			{
				printk(KERN_ERR "list item %d: %p <- %p -> %p\n", i, item->oskp.prev, item, item->oskp.next);
				item = item->oskp.next;
				i++;
			}

			printk(KERN_ERR "Removing bad node, dumping list, reverse walk\n");
			item = front->oskp.back;
			i = 0;
			while (item)
			{
				printk(KERN_ERR "list item %d: %p <- %p -> %p\n", i, item->oskp.prev, item, item->oskp.next);
				item = item->oskp.prev;
				i++;
			}
			*error = 1;
			return;
		}
		item_to_remove->oskp.next->oskp.prev = item_to_remove->oskp.prev;
	}

	*error = 0;
	item_to_remove->oskp.next = NULL;
	item_to_remove->oskp.prev = NULL;
}

OSK_STATIC_INLINE
void oskp_dlist_remove_item(osk_dlist* const front, osk_dlist_item* const item_to_remove)
{
	OSK_ASSERT(NULL != front);
	OSK_ASSERT(NULL != item_to_remove);
	OSK_ASSERT(MALI_TRUE == oskp_dlist_member_of(front, item_to_remove));

	/* if the item to remove is the current front*/
	if( front->oskp.front == item_to_remove )
	{
		/* then make the front point to the next item*/
		front->oskp.front = item_to_remove->oskp.next;
	}
	else
	{
		/* else just the previous item point to the next one*/
		item_to_remove->oskp.prev->oskp.next = item_to_remove->oskp.next;
	}

	/* if the item to remove is the current back*/
	if(front->oskp.back == item_to_remove)
	{
		/* then make the back point to the previous item*/
		front->oskp.back = item_to_remove->oskp.prev;
	}
	else
	{
		/* else just the next item point to the previous one*/
		if (!item_to_remove->oskp.next)
		{
			osk_dlist_item * item;
			int i;

			/* bad node, log the contents of the list */

			printk(KERN_ERR "Removing bad node %p, dumping list, forward walk\n", item_to_remove);
			item = front->oskp.front;
			i = 0;
			while (item)
			{
				printk(KERN_ERR "list item %d: %p <- %p -> %p\n", i, item->oskp.prev, item, item->oskp.next);
				item = item->oskp.next;
				i++;
			}

			printk(KERN_ERR "Removing bad node, dumping list, reverse walk\n");
			item = front->oskp.back;
			i = 0;
			while (item)
			{
				printk(KERN_ERR "list item %d: %p <- %p -> %p\n", i, item->oskp.prev, item, item->oskp.next);
				item = item->oskp.prev;
				i++;
			}

		}
		item_to_remove->oskp.next->oskp.prev = item_to_remove->oskp.prev;
	}

	item_to_remove->oskp.next = NULL;
	item_to_remove->oskp.prev = NULL;
}

OSK_STATIC_INLINE
osk_dlist_item* oskp_dlist_remove_debug(osk_dlist * const front, osk_dlist_item * const item_to_remove, int *err)
{
	oskp_dlist_remove_item_debug(front, item_to_remove, err);

	item_to_remove->oskp.next = NULL;
	item_to_remove->oskp.prev = NULL;

	return item_to_remove;
}

OSK_STATIC_INLINE
osk_dlist_item* oskp_dlist_remove(osk_dlist * const front, osk_dlist_item * const item_to_remove)
{
	oskp_dlist_remove_item(front, item_to_remove);

	item_to_remove->oskp.next = NULL;
	item_to_remove->oskp.prev = NULL;

	return item_to_remove;
}


CHECK_RESULT OSK_STATIC_INLINE
osk_dlist_item* oskp_dlist_remove_and_return_next(osk_dlist * const front,
		osk_dlist_item * const item_to_remove)
{
	osk_dlist_item *next;

	OSK_ASSERT(NULL != front);
	OSK_ASSERT(NULL != item_to_remove);

	next = item_to_remove->oskp.next;
	oskp_dlist_remove_item(front, item_to_remove);
	return next;
}

CHECK_RESULT OSK_STATIC_INLINE
osk_dlist_item* oskp_dlist_remove_and_return_prev(osk_dlist * const front,
		osk_dlist_item * const item_to_remove)
{
	osk_dlist_item *prev;

	OSK_ASSERT(NULL != front);
	OSK_ASSERT(NULL != item_to_remove);

	prev = item_to_remove->oskp.prev;
	oskp_dlist_remove_item(front, item_to_remove);
	return prev;
}

/**
 * @}
 */
/* end osk_dlist_private */

/**
 * @}
 */
/* end osk_dlist group */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

#ifdef __cplusplus
}
#endif

#endif /* _OSK_LISTS_H_ */

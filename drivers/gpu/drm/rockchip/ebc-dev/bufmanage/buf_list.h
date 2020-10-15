// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Zorro Liu <zorro.liu@rock-chips.com>
 */

#ifndef _BUF_LIST_H_
#define _BUF_LIST_H_

#define BUF_LIST_MAX_NUMBER 100

typedef struct buf_list_s {
	/** number of elements */
	int nb_elt;
	/** list node */
	int **array_elements;
	int maxelements;
} buf_list_t;

/** @brief initializes the list struct
  *
  * @param *li - pointer to list struct
  * @returns 0 on success, 1 on error
  */
int buf_list_init(buf_list_t **li, int maxelements);

/** @brief uninitializes the list struct
  *
  * @param *li - the list
  * @returns 0 on success, 1 on error
  */
int buf_list_uninit(buf_list_t *li);

/** @brief query if i'nth element exists
  *
  * @param *li - the list
  * @param i   - position
  * @returns 0 on success, 1 on error
  */
int buf_list_eol(buf_list_t *li, int i);

/** @brief return the element at position
  *
  * @param *li - the list
  * @param pos - position
  * @returns pointer to element on success, NULL on error.
  */
int *buf_list_get(buf_list_t *li, int pos);

/** @brief removes the element at position
  *
  * @param *li - the list
  * @param pos - position
  * @returns - on success, 1 on error
  */
int buf_list_remove(buf_list_t *li, int pos);

/** @brief adds the element at position
  *
  * @param *li - the list
  * @param *el - element
  * @param pos - position (-1 means the end)
  * @returns - on success, 1 on error
  */
int buf_list_add(buf_list_t *li, int *el, int pos);

/** @brief search the node at list, with the given compare function
  *
  * @param *list    - the list
  * @param *node    - node to be matched
  * @param cmp_func - compare function. compare function must return -1, 0, 1
		for less than, equal to, and greater than
  * @returns - on success, 1 on error
  */
int *buf_list_find(buf_list_t *list, int *node, int (*cmp_func)(int *, int *));

/** @brief return the position of node
  *
  * @param *list - the list
  * @param *node - element
  * @returns - position on success, -1 on error
  */
int buf_list_get_pos(buf_list_t *list, int *node);

/** @brief set the node element at a specified position
  *
  * @param *list - the list
  * @param *el - element
  * @pos pos - position
  * @returns - 1 on success, -1 on error
  */
int buf_list_set(buf_list_t *li, int *el, int pos);

#endif

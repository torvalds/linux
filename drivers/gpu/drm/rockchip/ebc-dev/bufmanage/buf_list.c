// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Zorro Liu <zorro.liu@rock-chips.com>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "buf_list.h"

#define IS_NULL(ptr) (NULL == ptr)

int buf_list_init(buf_list_t **li, int maxelements)
{
	(*li) = (buf_list_t *)kmalloc(sizeof(buf_list_t), GFP_KERNEL);
	if ((*li) == NULL)
		return -ENOMEM;

	(*li)->nb_elt = 0;
	(*li)->array_elements = NULL;
	(*li)->maxelements = maxelements;

	(*li)->array_elements = (int **)kmalloc(sizeof(int *) * maxelements, GFP_KERNEL);
	if ((*li)->array_elements == NULL) {
		kfree(*li);
		return -ENOMEM;
	}
	memset((*li)->array_elements, 0, (sizeof(int *) * maxelements));

	return 0;
}

int buf_list_uninit(buf_list_t *li)
{
	if (!(IS_NULL(li))) {
		if (!(IS_NULL(li->array_elements))) {
			memset(li->array_elements, 0, (sizeof(int *) * (li->maxelements)));
			kfree(li->array_elements);
			li->array_elements = NULL;
		}
		if (li)
			kfree(li);
	}

	return 0;
}

int buf_list_eol(buf_list_t *li, int i)
{
	if (IS_NULL(li) || IS_NULL(li->array_elements))
		return 1;

	if ((i >= 0) && (i < li->nb_elt))
		return 0;

	/* end of list */
	return 1;
}

int *buf_list_get(buf_list_t *li, int pos)
{
	if ((IS_NULL(li)) || (IS_NULL(li->array_elements)) || (pos < 0) || (pos >= li->nb_elt))
		/* element does not exist */
		return NULL;

	return li->array_elements[pos];
}

int buf_list_remove(buf_list_t *li, int pos)
{
	int i = 0;

	if ((IS_NULL(li)) || (IS_NULL(li->array_elements)) || (pos < 0) || (pos >= li->nb_elt))
		/* element does not exist */
		return -1;

	/* exist because nb_elt > 0 */
	i = pos;
	while (i < li->nb_elt - 1) {
		li->array_elements[i] = li->array_elements[i + 1];
		i++;
	}
	li->nb_elt--;

	return li->nb_elt;
}

int buf_list_add(buf_list_t *li, int *el, int pos)
{
	int i = 0;

	if ((IS_NULL(li)) || (IS_NULL(li->array_elements)))
		return -1;

	if ((pos < 0) || (pos >= li->nb_elt)) {
		/* insert at the end  */
		pos = li->nb_elt;
	} else {
		i = (li->nb_elt - 1);
		while (i >= pos) {
			li->array_elements[i + 1] = li->array_elements[i];
			i--;
		}
	}

	if (pos >= (li->maxelements))
		return -1;

	li->array_elements[pos] = el;
	li->nb_elt++;

	return li->nb_elt;
}

int *buf_list_find(buf_list_t *list, int *node, int (*cmp_func)(int *, int *))
{
	int pos = 0;
	void *tmp = NULL;

	if ((IS_NULL(list)) || (IS_NULL(list->array_elements)))
		return NULL;

	while (pos < list->nb_elt) /*(!buf_list_eol(list, pos))*/ {
		int *node_;
#if 1
		node_ = list->array_elements[pos];
#else
		node_ = buf_list_get(list, pos);
#endif
		if (cmp_func(node, node_) == 0) {
			tmp = node_;
			break;
		}
		pos++;
	}

	return tmp;
}

int buf_list_get_pos(buf_list_t *list, int *node)
{
	int pos = 0;

	if ((IS_NULL(list)) || (IS_NULL(list->array_elements)) || (list->nb_elt <= 0))
		return -1;

	/* exist because nb_elt > 0 */
	pos = 0;
	while (pos < list->nb_elt) {
		if ((int *)(list->array_elements[pos]) == node)
			return pos;
		pos++;
	}

	return -1;
}

int buf_list_set(buf_list_t *li, int *el, int pos)
{
	if ((IS_NULL(li)) || (IS_NULL(li->array_elements)) || (pos < 0) || (pos >= li->nb_elt))
		/* element does not exist */
		return -1;

	/* exist because nb_elt > 0 */
	li->array_elements[pos] = el;

	return 0;
}

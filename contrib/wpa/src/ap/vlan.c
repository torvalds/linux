/*
 * hostapd / VLAN definition
 * Copyright (c) 2016, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "ap/vlan.h"

/* compare the two arguments, NULL is treated as empty
 * return zero iff they are equal
 */
int vlan_compare(struct vlan_description *a, struct vlan_description *b)
{
	int i;
	const int a_empty = !a || !a->notempty;
	const int b_empty = !b || !b->notempty;

	if (a_empty && b_empty)
		return 0;
	if (a_empty || b_empty)
		return 1;
	if (a->untagged != b->untagged)
		return 1;
	for (i = 0; i < MAX_NUM_TAGGED_VLAN; i++) {
		if (a->tagged[i] != b->tagged[i])
			return 1;
	}
	return 0;
}

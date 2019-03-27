/*
 * hostapd / VLAN definition
 * Copyright (c) 2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef VLAN_H
#define VLAN_H

#define MAX_NUM_TAGGED_VLAN 32

struct vlan_description {
	int notempty; /* 0 : no vlan information present, 1: else */
	int untagged; /* >0 802.1q vid */
	int tagged[MAX_NUM_TAGGED_VLAN]; /* first k items, ascending order */
};

#ifndef CONFIG_NO_VLAN
int vlan_compare(struct vlan_description *a, struct vlan_description *b);
#else /* CONFIG_NO_VLAN */
static inline int
vlan_compare(struct vlan_description *a, struct vlan_description *b)
{
	return 0;
}
#endif /* CONFIG_NO_VLAN */

#endif /* VLAN_H */

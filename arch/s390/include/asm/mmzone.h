/*
 * NUMA support for s390
 *
 * Copyright IBM Corp. 2015
 */

#ifndef _ASM_S390_MMZONE_H
#define _ASM_S390_MMZONE_H

#ifdef CONFIG_NUMA

extern struct pglist_data *node_data[];
#define NODE_DATA(nid) (node_data[nid])

#endif /* CONFIG_NUMA */
#endif /* _ASM_S390_MMZONE_H */

#include <asm-generic/topology.h>

#ifndef _ASM_MICROBLAZE_TOPOLOGY_H
#define _ASM_MICROBLAZE_TOPOLOGY_H

struct device_node;
static inline int of_node_to_nid(struct device_node *device)
{
	return 0;
}
#endif /* _ASM_MICROBLAZE_TOPOLOGY_H */

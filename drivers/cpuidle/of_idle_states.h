#ifndef __OF_IDLE_STATES
#define __OF_IDLE_STATES

int __init of_init_idle_driver(struct cpuidle_driver *drv,
			       struct device_node *state_nodes[],
			       unsigned int start_idx,
			       bool init_nodes);
#endif

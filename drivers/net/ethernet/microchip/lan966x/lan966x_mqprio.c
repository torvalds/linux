// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"

int lan966x_mqprio_add(struct lan966x_port *port, u8 num_tc)
{
	u8 i;

	if (num_tc != NUM_PRIO_QUEUES) {
		netdev_err(port->dev, "Only %d traffic classes supported\n",
			   NUM_PRIO_QUEUES);
		return -EINVAL;
	}

	netdev_set_num_tc(port->dev, num_tc);

	for (i = 0; i < num_tc; ++i)
		netdev_set_tc_queue(port->dev, i, 1, i);

	return 0;
}

int lan966x_mqprio_del(struct lan966x_port *port)
{
	netdev_reset_tc(port->dev);

	return 0;
}

// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"
#include "vcap_api_client.h"

int lan966x_goto_port_add(struct lan966x_port *port,
			  struct flow_action_entry *act,
			  unsigned long goto_id,
			  struct netlink_ext_ack *extack)
{
	struct lan966x *lan966x = port->lan966x;
	int err;

	err = vcap_enable_lookups(lan966x->vcap_ctrl, port->dev,
				  act->chain_index, goto_id,
				  true);
	if (err == -EFAULT) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported goto chain");
		return -EOPNOTSUPP;
	}

	if (err == -EADDRINUSE) {
		NL_SET_ERR_MSG_MOD(extack, "VCAP already enabled");
		return -EOPNOTSUPP;
	}

	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Could not enable VCAP lookups");
		return err;
	}

	port->tc.goto_id = goto_id;

	return 0;
}

int lan966x_goto_port_del(struct lan966x_port *port,
			  unsigned long goto_id,
			  struct netlink_ext_ack *extack)
{
	struct lan966x *lan966x = port->lan966x;
	int err;

	err = vcap_enable_lookups(lan966x->vcap_ctrl, port->dev, 0,
				  goto_id, false);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Could not disable VCAP lookups");
		return err;
	}

	port->tc.goto_id = 0;

	return 0;
}

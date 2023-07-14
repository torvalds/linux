// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"
#include "vcap_api_client.h"

int lan966x_goto_port_add(struct lan966x_port *port,
			  int from_cid, int to_cid,
			  unsigned long goto_id,
			  struct netlink_ext_ack *extack)
{
	struct lan966x *lan966x = port->lan966x;
	int err;

	err = vcap_enable_lookups(lan966x->vcap_ctrl, port->dev,
				  from_cid, to_cid, goto_id,
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

	return 0;
}

int lan966x_goto_port_del(struct lan966x_port *port,
			  unsigned long goto_id,
			  struct netlink_ext_ack *extack)
{
	struct lan966x *lan966x = port->lan966x;
	int err;

	err = vcap_enable_lookups(lan966x->vcap_ctrl, port->dev, 0, 0,
				  goto_id, false);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Could not disable VCAP lookups");
		return err;
	}

	return 0;
}

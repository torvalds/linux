// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"
#include "vcap_api.h"

int lan966x_vcap_init(struct lan966x *lan966x)
{
	struct vcap_control *ctrl;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	lan966x->vcap_ctrl = ctrl;

	return 0;
}

void lan966x_vcap_deinit(struct lan966x *lan966x)
{
	struct vcap_control *ctrl;

	ctrl = lan966x->vcap_ctrl;
	if (!ctrl)
		return;

	kfree(ctrl);
}

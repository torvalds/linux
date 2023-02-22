// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Gerhard Engleder <gerhard@engleder-embedded.com> */

#include <linux/if_vlan.h>
#include <net/xdp_sock_drv.h>

#include "tsnep.h"

int tsnep_xdp_setup_prog(struct tsnep_adapter *adapter, struct bpf_prog *prog,
			 struct netlink_ext_ack *extack)
{
	struct bpf_prog *old_prog;

	old_prog = xchg(&adapter->xdp_prog, prog);
	if (old_prog)
		bpf_prog_put(old_prog);

	return 0;
}

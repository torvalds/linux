/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2020 Marvell International Ltd. All rights reserved. */

#ifndef __PRESTERA_DSA_H_
#define __PRESTERA_DSA_H_

#include <linux/types.h>

#define PRESTERA_DSA_HLEN	16

enum prestera_dsa_cmd {
	/* DSA command is "To CPU" */
	PRESTERA_DSA_CMD_TO_CPU = 0,

	/* DSA command is "From CPU" */
	PRESTERA_DSA_CMD_FROM_CPU,
};

struct prestera_dsa_vlan {
	u16 vid;
	u8 vpt;
	u8 cfi_bit;
	bool is_tagged;
};

struct prestera_dsa {
	struct prestera_dsa_vlan vlan;
	u32 hw_dev_num;
	u32 port_num;
};

int prestera_dsa_parse(struct prestera_dsa *dsa, const u8 *dsa_buf);
int prestera_dsa_build(const struct prestera_dsa *dsa, u8 *dsa_buf);

#endif /* _PRESTERA_DSA_H_ */

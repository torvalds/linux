/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * Copyright (c) 2026 Luminex Network Intelligence
 */
#ifndef _MV88E6XXX_TCFLOWER_H_
#define _MV88E6XXX_TCFLOWER_H_

int mv88e6xxx_cls_flower_add(struct dsa_switch *ds, int port,
			     struct flow_cls_offload *cls, bool ingress);
int mv88e6xxx_cls_flower_del(struct dsa_switch *ds, int port,
			     struct flow_cls_offload *cls, bool ingress);
void mv88e6xxx_flower_teardown(struct mv88e6xxx_chip *chip);
#endif

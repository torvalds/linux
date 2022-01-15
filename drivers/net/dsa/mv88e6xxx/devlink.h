/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Marvell 88E6xxx Switch devlink support. */

#ifndef _MV88E6XXX_DEVLINK_H
#define _MV88E6XXX_DEVLINK_H

int mv88e6xxx_setup_devlink_params(struct dsa_switch *ds);
void mv88e6xxx_teardown_devlink_params(struct dsa_switch *ds);
int mv88e6xxx_setup_devlink_resources(struct dsa_switch *ds);
int mv88e6xxx_devlink_param_get(struct dsa_switch *ds, u32 id,
				struct devlink_param_gset_ctx *ctx);
int mv88e6xxx_devlink_param_set(struct dsa_switch *ds, u32 id,
				struct devlink_param_gset_ctx *ctx);
int mv88e6xxx_setup_devlink_regions_global(struct dsa_switch *ds);
void mv88e6xxx_teardown_devlink_regions_global(struct dsa_switch *ds);
int mv88e6xxx_setup_devlink_regions_port(struct dsa_switch *ds, int port);
void mv88e6xxx_teardown_devlink_regions_port(struct dsa_switch *ds, int port);

int mv88e6xxx_devlink_info_get(struct dsa_switch *ds,
			       struct devlink_info_req *req,
			       struct netlink_ext_ack *extack);
#endif /* _MV88E6XXX_DEVLINK_H */

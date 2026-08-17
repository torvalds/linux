/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _RTL83XX_H
#define _RTL83XX_H

struct realtek_interface_info {
	int (*reg_read)(void *ctx, u32 reg, u32 *val);
	int (*reg_write)(void *ctx, u32 reg, u32 val);
};

void rtl83xx_lock(void *ctx);
void rtl83xx_unlock(void *ctx);
int rtl83xx_setup_user_mdio(struct dsa_switch *ds);
struct realtek_priv *
rtl83xx_probe(struct device *dev,
	      const struct realtek_interface_info *interface_info);
int rtl83xx_register_switch(struct realtek_priv *priv);
void rtl83xx_unregister_switch(struct realtek_priv *priv);
void rtl83xx_shutdown(struct realtek_priv *priv);
void rtl83xx_remove(struct realtek_priv *priv);
void rtl83xx_reset_assert(struct realtek_priv *priv);
void rtl83xx_reset_deassert(struct realtek_priv *priv);

int rtl83xx_port_bridge_join(struct dsa_switch *ds, int port,
			     struct dsa_bridge bridge,
			     bool *tx_forward_offload,
			     struct netlink_ext_ack *extack);
void rtl83xx_port_bridge_leave(struct dsa_switch *ds, int port,
			       struct dsa_bridge bridge);
int rtl83xx_port_bridge_flags(struct dsa_switch *ds, int port,
			      struct switchdev_brport_flags flags,
			      struct netlink_ext_ack *extack);
int rtl83xx_setup_port_flood_control(struct realtek_priv *priv, int port);

void rtl83xx_port_fast_age(struct dsa_switch *ds, int port);
int rtl83xx_port_fdb_add(struct dsa_switch *ds, int port,
			 const unsigned char *addr, u16 vid,
			 struct dsa_db db);
int rtl83xx_port_fdb_del(struct dsa_switch *ds, int port,
			 const unsigned char *addr, u16 vid,
			 struct dsa_db db);
int rtl83xx_port_fdb_dump(struct dsa_switch *ds, int port,
			  dsa_fdb_dump_cb_t *cb, void *data);
int rtl83xx_port_mdb_add(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_mdb *mdb,
			 struct dsa_db db);
int rtl83xx_port_mdb_del(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_mdb *mdb,
			 struct dsa_db db);

#endif /* _RTL83XX_H */

/* SPDX-License-Identifier: GPL-2.0 */
/* Microchip lan937x dev ops headers
 * Copyright (C) 2019-2022 Microchip Technology Inc.
 */

#ifndef __LAN937X_CFG_H
#define __LAN937X_CFG_H

int lan937x_reset_switch(struct ksz_device *dev);
int lan937x_setup(struct dsa_switch *ds);
void lan937x_teardown(struct dsa_switch *ds);
void lan937x_port_setup(struct ksz_device *dev, int port, bool cpu_port);
void lan937x_config_cpu_port(struct dsa_switch *ds);
int lan937x_switch_init(struct ksz_device *dev);
void lan937x_switch_exit(struct ksz_device *dev);
int lan937x_r_phy(struct ksz_device *dev, u16 addr, u16 reg, u16 *data);
int lan937x_w_phy(struct ksz_device *dev, u16 addr, u16 reg, u16 val);
int lan937x_change_mtu(struct ksz_device *dev, int port, int new_mtu);
void lan937x_phylink_get_caps(struct ksz_device *dev, int port,
			      struct phylink_config *config);
void lan937x_setup_rgmii_delay(struct ksz_device *dev, int port);
int lan937x_set_ageing_time(struct ksz_device *dev, unsigned int msecs);
#endif

/* SPDX-License-Identifier: GPL-2.0 */
/* Texas Instruments ICSSM Ethernet driver
 *
 * Copyright (C) 2018-2022 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#ifndef __NET_TI_PRUETH_H
#define __NET_TI_PRUETH_H

#include <linux/phy.h>
#include <linux/types.h>
#include <linux/pruss_driver.h>
#include <linux/remoteproc/pruss.h>

/* PRU Ethernet Type - Ethernet functionality (protocol
 * implemented) provided by the PRU firmware being loaded.
 */
enum pruss_ethtype {
	PRUSS_ETHTYPE_EMAC = 0,
	PRUSS_ETHTYPE_HSR,
	PRUSS_ETHTYPE_PRP,
	PRUSS_ETHTYPE_SWITCH,
	PRUSS_ETHTYPE_MAX,
};

/* In switch mode there are 3 real ports i.e. 3 mac addrs.
 * however Linux sees only the host side port. The other 2 ports
 * are the switch ports.
 * In emac mode there are 2 real ports i.e. 2 mac addrs.
 * Linux sees both the ports.
 */
enum prueth_port {
	PRUETH_PORT_HOST = 0,	/* host side port */
	PRUETH_PORT_MII0,	/* physical port MII 0 */
	PRUETH_PORT_MII1,	/* physical port MII 1 */
	PRUETH_PORT_INVALID,	/* Invalid prueth port */
};

enum prueth_mac {
	PRUETH_MAC0 = 0,
	PRUETH_MAC1,
	PRUETH_NUM_MACS,
	PRUETH_MAC_INVALID,
};

/**
 * struct prueth_firmware - PRU Ethernet FW data
 * @fw_name: firmware names of firmware to run on PRU
 */
struct prueth_firmware {
	const char *fw_name[PRUSS_ETHTYPE_MAX];
};

/**
 * struct prueth_private_data - PRU Ethernet private data
 * @fw_pru: firmware names to be used for PRUSS ethernet usecases
 */
struct prueth_private_data {
	const struct prueth_firmware fw_pru[PRUSS_NUM_PRUS];
};

/* data for each emac port */
struct prueth_emac {
	struct prueth *prueth;
	struct net_device *ndev;

	struct rproc *pru;
	struct phy_device *phydev;

	int link;
	int speed;
	int duplex;

	enum prueth_port port_id;
	const char *phy_id;
	u8 mac_addr[6];
	phy_interface_t phy_if;

	/* spin lock used to protect
	 * during link configuration
	 */
	spinlock_t lock;
};

struct prueth {
	struct device *dev;
	struct pruss *pruss;
	struct rproc *pru0, *pru1;

	const struct prueth_private_data *fw_data;
	struct prueth_fw_offsets *fw_offsets;

	struct device_node *eth_node[PRUETH_NUM_MACS];
	struct prueth_emac *emac[PRUETH_NUM_MACS];
	struct net_device *registered_netdevs[PRUETH_NUM_MACS];

	unsigned int eth_type;
};
#endif /* __NET_TI_PRUETH_H */

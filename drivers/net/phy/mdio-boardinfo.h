/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mdio-boardinfo.h - board info interface internal to the mdio_bus
 * component
 */

#ifndef __MDIO_BOARD_INFO_H
#define __MDIO_BOARD_INFO_H

struct mii_bus;
struct mdio_board_info;

void mdiobus_setup_mdiodev_from_board_info(struct mii_bus *bus,
					   int (*cb)
					   (struct mii_bus *bus,
					    struct mdio_board_info *bi));

#endif /* __MDIO_BOARD_INFO_H */

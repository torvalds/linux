/*
 * mdio-boardinfo.h - board info interface internal to the mdio_bus
 * component
 */

#ifndef __MDIO_BOARD_INFO_H
#define __MDIO_BOARD_INFO_H

#include <linux/phy.h>
#include <linux/mutex.h>

struct mdio_board_entry {
	struct list_head	list;
	struct mdio_board_info	board_info;
};

void mdiobus_setup_mdiodev_from_board_info(struct mii_bus *bus,
					   int (*cb)
					   (struct mii_bus *bus,
					    struct mdio_board_info *bi));

#endif /* __MDIO_BOARD_INFO_H */

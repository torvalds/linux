/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 */
#ifndef _SCIF_PEER_BUS_H_
#define _SCIF_PEER_BUS_H_

#include <linux/device.h>
#include <linux/mic_common.h>

/*
 * Peer devices show up as PCIe devices for the mgmt node but not the cards.
 * The mgmt node discovers all the cards on the PCIe bus and informs the other
 * cards about their peers. Upon notification of a peer a node adds a peer
 * device to the peer bus to maintain symmetry in the way devices are
 * discovered across all nodes in the SCIF network.
 */
/**
 * scif_peer_dev - representation of a peer SCIF device
 * @dev: underlying device
 * @dnode - The destination node which this device will communicate with.
 */
struct scif_peer_dev {
	struct device dev;
	u8 dnode;
};

/**
 * scif_peer_driver - operations for a scif_peer I/O driver
 * @driver: underlying device driver (populate name and owner).
 * @id_table: the ids serviced by this driver.
 * @probe: the function to call when a device is found.  Returns 0 or -errno.
 * @remove: the function to call when a device is removed.
 */
struct scif_peer_driver {
	struct device_driver driver;
	const struct scif_peer_dev_id *id_table;

	int (*probe)(struct scif_peer_dev *dev);
	void (*remove)(struct scif_peer_dev *dev);
};

struct scif_dev;

int scif_peer_register_driver(struct scif_peer_driver *driver);
void scif_peer_unregister_driver(struct scif_peer_driver *driver);

struct scif_peer_dev *scif_peer_register_device(struct scif_dev *sdev);
void scif_peer_unregister_device(struct scif_peer_dev *sdev);

int scif_peer_bus_init(void);
void scif_peer_bus_exit(void);
#endif /* _SCIF_PEER_BUS_H */

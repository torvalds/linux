// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/pci.h>
#include <linux/etherdevice.h>

#include "octep_config.h"
#include "octep_main.h"

/**
 * octep_clean_iqs()  - Clean Tx queues to shutdown the device.
 *
 * @oct: Octeon device private data structure.
 *
 * Free the buffers in Tx queue descriptors pending completion and
 * reset queue indices
 */
void octep_clean_iqs(struct octep_device *oct)
{
}

/**
 * octep_setup_iqs() - setup resources for all Tx queues.
 *
 * @oct: Octeon device private data structure.
 */
int octep_setup_iqs(struct octep_device *oct)
{
	return -1;
}

/**
 * octep_free_iqs() - Free resources of all Tx queues.
 *
 * @oct: Octeon device private data structure.
 */
void octep_free_iqs(struct octep_device *oct)
{
}

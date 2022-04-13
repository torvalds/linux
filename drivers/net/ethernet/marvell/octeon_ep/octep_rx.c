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
 * octep_setup_oqs() - setup resources for all Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
int octep_setup_oqs(struct octep_device *oct)
{
	return -1;
}

/**
 * octep_oq_dbell_init() - Initialize Rx queue doorbell.
 *
 * @oct: Octeon device private data structure.
 *
 * Write number of descriptors to Rx queue doorbell register.
 */
void octep_oq_dbell_init(struct octep_device *oct)
{
}

/**
 * octep_free_oqs() - Free resources of all Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
void octep_free_oqs(struct octep_device *oct)
{
}

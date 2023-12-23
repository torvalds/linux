// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) VF Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/pci.h>
#include <linux/etherdevice.h>

#include "octep_vf_config.h"
#include "octep_vf_main.h"

/**
 * octep_vf_clean_iqs()  - Clean Tx queues to shutdown the device.
 *
 * @oct: Octeon device private data structure.
 *
 * Free the buffers in Tx queue descriptors pending completion and
 * reset queue indices
 */
void octep_vf_clean_iqs(struct octep_vf_device *oct)
{
}

/**
 * octep_vf_setup_iqs() - setup resources for all Tx queues.
 *
 * @oct: Octeon device private data structure.
 */
int octep_vf_setup_iqs(struct octep_vf_device *oct)
{
	return -1;
}

/**
 * octep_vf_free_iqs() - Free resources of all Tx queues.
 *
 * @oct: Octeon device private data structure.
 */
void octep_vf_free_iqs(struct octep_vf_device *oct)
{
}

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
 * octep_vf_setup_oqs() - setup resources for all Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
int octep_vf_setup_oqs(struct octep_vf_device *oct)
{
	return -1;
}

/**
 * octep_vf_oq_dbell_init() - Initialize Rx queue doorbell.
 *
 * @oct: Octeon device private data structure.
 *
 * Write number of descriptors to Rx queue doorbell register.
 */
void octep_vf_oq_dbell_init(struct octep_vf_device *oct)
{
}

/**
 * octep_vf_free_oqs() - Free resources of all Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
void octep_vf_free_oqs(struct octep_vf_device *oct)
{
}

/*
 * Copyright (C) ST-Ericsson AB 2010
 * Contact: Sjur Brendeland / sjur.brandeland@stericsson.com
 * Author:  Amarnath Revanna / amarnath.bangalore.revanna@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":" fmt

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <mach/mbox-db5500.h>
#include <net/caif/caif_shm.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CAIF Shared Memory protocol driver");

#define MAX_SHM_INSTANCES	1

enum {
	MBX_ACC0,
	MBX_ACC1,
	MBX_DSP
};

static struct shmdev_layer shmdev_lyr[MAX_SHM_INSTANCES];

static unsigned int shm_start;
static unsigned int shm_size;

module_param(shm_size, uint  , 0440);
MODULE_PARM_DESC(shm_total_size, "Start of SHM shared memory");

module_param(shm_start, uint  , 0440);
MODULE_PARM_DESC(shm_total_start, "Total Size of SHM shared memory");

static int shmdev_send_msg(u32 dev_id, u32 mbx_msg)
{
	/* Always block until msg is written successfully */
	mbox_send(shmdev_lyr[dev_id].hmbx, mbx_msg, true);
	return 0;
}

static int shmdev_mbx_setup(void *pshmdrv_cb, struct shmdev_layer *pshm_dev,
							 void *pshm_drv)
{
	/*
	 * For UX5500, we have only 1 SHM instance which uses MBX0
	 * for communication with the peer modem
	 */
	pshm_dev->hmbx = mbox_setup(MBX_ACC0, pshmdrv_cb, pshm_drv);

	if (!pshm_dev->hmbx)
		return -ENODEV;
	else
		return 0;
}

static int __init caif_shmdev_init(void)
{
	int i, result;

	/* Loop is currently overkill, there is only one instance */
	for (i = 0; i < MAX_SHM_INSTANCES; i++) {

		shmdev_lyr[i].shm_base_addr = shm_start;
		shmdev_lyr[i].shm_total_sz = shm_size;

		if (((char *)shmdev_lyr[i].shm_base_addr == NULL)
			       || (shmdev_lyr[i].shm_total_sz <= 0))	{
			pr_warn("ERROR,"
				"Shared memory Address and/or Size incorrect"
				", Bailing out ...\n");
			result = -EINVAL;
			goto clean;
		}

		pr_info("SHM AREA (instance %d) STARTS"
			" AT %p\n", i, (char *)shmdev_lyr[i].shm_base_addr);

		shmdev_lyr[i].shm_id = i;
		shmdev_lyr[i].pshmdev_mbxsend = shmdev_send_msg;
		shmdev_lyr[i].pshmdev_mbxsetup = shmdev_mbx_setup;

		/*
		 * Finally, CAIF core module is called with details in place:
		 * 1. SHM base address
		 * 2. SHM size
		 * 3. MBX handle
		 */
		result = caif_shmcore_probe(&shmdev_lyr[i]);
		if (result) {
			pr_warn("ERROR[%d],"
				"Could not probe SHM core (instance %d)"
				" Bailing out ...\n", result, i);
			goto clean;
		}
	}

	return 0;

clean:
	/*
	 * For now, we assume that even if one instance of SHM fails, we bail
	 * out of the driver support completely. For this, we need to release
	 * any memory allocated and unregister any instance of SHM net device.
	 */
	for (i = 0; i < MAX_SHM_INSTANCES; i++) {
		if (shmdev_lyr[i].pshm_netdev)
			unregister_netdev(shmdev_lyr[i].pshm_netdev);
	}
	return result;
}

static void __exit caif_shmdev_exit(void)
{
	int i;

	for (i = 0; i < MAX_SHM_INSTANCES; i++) {
		caif_shmcore_remove(shmdev_lyr[i].pshm_netdev);
		kfree((void *)shmdev_lyr[i].shm_base_addr);
	}

}

module_init(caif_shmdev_init);
module_exit(caif_shmdev_exit);

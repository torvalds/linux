// SPDX-License-Identifier: GPL-2.0
/*
 *  IBM System z PNET ID Support
 *
 *    Copyright IBM Corp. 2018
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <asm/ccwgroup.h>
#include <asm/ccwdev.h>
#include <asm/pnet.h>
#include <asm/ebcdic.h>

#define PNETIDS_LEN		64	/* Total utility string length in bytes
					 * to cover up to 4 PNETIDs of 16 bytes
					 * for up to 4 device ports
					 */
#define MAX_PNETID_LEN		16	/* Max.length of a single port PNETID */
#define MAX_PNETID_PORTS	(PNETIDS_LEN / MAX_PNETID_LEN)
					/* Max. # of ports with a PNETID */

/*
 * Get the PNETIDs from a device.
 * s390 hardware supports the definition of a so-called Physical Network
 * Identifier (short PNETID) per network device port. These PNETIDs can be
 * used to identify network devices that are attached to the same physical
 * network (broadcast domain).
 *
 * The device can be
 * - a ccwgroup device with all bundled subchannels having the same PNETID
 * - a PCI attached network device
 *
 * Returns:
 * 0:		PNETIDs extracted from device.
 * -ENOMEM:	No memory to extract utility string.
 * -EOPNOTSUPP: Device type without utility string support
 */
static int pnet_ids_by_device(struct device *dev, u8 *pnetids)
{
	memset(pnetids, 0, PNETIDS_LEN);
	if (dev_is_ccwgroup(dev)) {
		struct ccwgroup_device *gdev = to_ccwgroupdev(dev);
		u8 *util_str;

		util_str = ccw_device_get_util_str(gdev->cdev[0], 0);
		if (!util_str)
			return -ENOMEM;
		memcpy(pnetids, util_str, PNETIDS_LEN);
		EBCASC(pnetids, PNETIDS_LEN);
		kfree(util_str);
		return 0;
	}
	if (dev_is_pci(dev)) {
		struct zpci_dev *zdev = to_zpci(to_pci_dev(dev));

		memcpy(pnetids, zdev->util_str, sizeof(zdev->util_str));
		EBCASC(pnetids, sizeof(zdev->util_str));
		return 0;
	}
	return -EOPNOTSUPP;
}

/*
 * Extract the pnetid for a device port.
 *
 * Return 0 if a pnetid is found and -ENOENT otherwise.
 */
int pnet_id_by_dev_port(struct device *dev, unsigned short port, u8 *pnetid)
{
	u8 pnetids[MAX_PNETID_PORTS][MAX_PNETID_LEN];
	static const u8 zero[MAX_PNETID_LEN] = { 0 };
	int rc = 0;

	if (!dev || port >= MAX_PNETID_PORTS)
		return -ENOENT;

	if (!pnet_ids_by_device(dev, (u8 *)pnetids) &&
	    memcmp(pnetids[port], zero, MAX_PNETID_LEN))
		memcpy(pnetid, pnetids[port], MAX_PNETID_LEN);
	else
		rc = -ENOENT;

	return rc;
}
EXPORT_SYMBOL_GPL(pnet_id_by_dev_port);

MODULE_DESCRIPTION("pnetid determination from utility strings");
MODULE_LICENSE("GPL");

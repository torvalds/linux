/*
 * Arch specific extensions to struct device
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_DEVICE_H
#define _ASM_MICROBLAZE_DEVICE_H

struct device_node;

struct dev_archdata {
};

struct pdev_archdata {
	u64 dma_mask;
};

#endif /* _ASM_MICROBLAZE_DEVICE_H */



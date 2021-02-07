/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ACRN_HSM_DRV_H
#define __ACRN_HSM_DRV_H

#include <linux/types.h>

#define ACRN_INVALID_VMID (0xffffU)

/**
 * struct acrn_vm - Properties of ACRN User VM.
 * @vmid:	User VM ID
 */
struct acrn_vm {
	u16	vmid;
};

#endif /* __ACRN_HSM_DRV_H */

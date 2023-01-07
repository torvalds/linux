// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <linux/gunyah/gh_rm_drv.h>
#include "reset.h"

/**
 * gh_arch_validate_vm_exited_notif: Validate the arch specific exit
 * reason and provide a generic reason for further use.
 * @buff_size: Size of the buffer containing the exit reason
 * @hdr_size: Size of the header
 * @vm_exited_payload: Struct of exit_reason
 *
 * If the exit reason is not valid or has an incorrect size, -EINVAL is
 * returned, 0 otherwise and also provides a generic reason for exit
 * which can be used by drivers.
 */
int gh_arch_validate_vm_exited_notif(size_t payload_size,
	struct gh_rm_notif_vm_exited_payload *vm_exited_payload)
{
	switch (vm_exited_payload->exit_type) {
	case GH_RM_VM_EXIT_TYPE_PSCI_SYSTEM_RESET2:
		if (payload_size !=
		    sizeof(*vm_exited_payload) + sizeof(struct gh_vm_exit_reason_psci_sys_reset2)) {
			pr_err("%s: Invalid size for type PSCI_SYSTEM_RESET2: %u\n",
			__func__, payload_size);
			return -EINVAL;
		}
		vm_exited_payload->exit_type = GH_RM_VM_EXIT_TYPE_SYSTEM_RESET;
		fallthrough;
	case GH_RM_VM_EXIT_TYPE_PSCI_SYSTEM_RESET:
		vm_exited_payload->exit_type = GH_RM_VM_EXIT_TYPE_SYSTEM_RESET;
		break;
	case GH_RM_VM_EXIT_TYPE_PSCI_SYSTEM_OFF:
		vm_exited_payload->exit_type = GH_RM_VM_EXIT_TYPE_SYSTEM_OFF;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(gh_arch_validate_vm_exited_notif);


/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 *
 */

#ifndef __RESET_H
#define __RESET_H

#define GH_RM_VM_EXIT_TYPE_PSCI_SYSTEM_OFF		1
#define GH_RM_VM_EXIT_TYPE_PSCI_SYSTEM_RESET	2
#define GH_RM_VM_EXIT_TYPE_PSCI_SYSTEM_RESET2	3

/* GH_RM_VM_EXIT_TYPE_PSCI_SYSTEM_RESET2 */
struct gh_vm_exit_reason_psci_sys_reset2 {
	u16 exit_flags;
	/* GH_PSCI_SYS_RESET2_EXIT_FLAG_* are bit representations.
	 * It follows similar flags model as that of VM_EXIT, but
	 * only if the vendor_reset field in the struct is set
	 */
#define GH_PSCI_SYS_RESET2_EXIT_FLAG_TYPE	0x1
#define GH_PSCI_SYS_RESET2_POWEROFF	0 /* Value at bit:0 */
#define GH_PSCI_SYS_RESET2_RESTART	1 /* Value at bit:0 */
#define GH_PSCI_SYS_RESET2_EXIT_FLAG_SYSTEM	0x2
#define GH_PSCI_SYS_RESET2_EXIT_FLAG_WARM	0x4
#define GH_PSCI_SYS_RESET2_EXIT_FLAG_DUMP	0x8

	u8 exit_code;
	/* Exit codes.
	 * It follows similar flags model as that of VM_EXIT, but
	 * only if the vendor_reset field in the struct is set
	 */
#define GH_PSCI_SYS_RESET2_CODE_NORMAL	0
#define GH_PSCI_SYS_RESET2_SOFTWARE_ERR	1
#define GH_PSCI_SYS_RESET2_BUS_ERR		2
#define GH_PSCI_SYS_RESET2_DEVICE_ERR	3

	u8 reserved:7;

	/* If the vendor_reset is set, the above flags and codes apply.
	 * Else, the entire exit_reason struct is 0, which qualifies as
	 * PSCI_SYSTEM_WARM_RESET. Hence, first check this field before
	 * checking others.
	 */
	u8 vendor_reset:1;
} __packed;

#endif

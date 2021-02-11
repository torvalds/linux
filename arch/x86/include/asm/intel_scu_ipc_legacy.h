/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_INTEL_SCU_IPC_LEGACY_H_
#define _ASM_X86_INTEL_SCU_IPC_LEGACY_H_

#include <linux/types.h>

#define IPCMSG_COLD_OFF		0x80	/* Only for Tangier */
#define IPCMSG_COLD_RESET	0xF1

/* Don't call these in new code - they will be removed eventually */

/* Issue commands to the SCU with or without data */
static inline int intel_scu_ipc_simple_command(int cmd, int sub)
{
	return intel_scu_ipc_dev_simple_command(NULL, cmd, sub);
}

#endif

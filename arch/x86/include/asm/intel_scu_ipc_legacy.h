/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_INTEL_SCU_IPC_LEGACY_H_
#define _ASM_X86_INTEL_SCU_IPC_LEGACY_H_

#include <linux/notifier.h>

#define IPCMSG_INDIRECT_READ	0x02
#define IPCMSG_INDIRECT_WRITE	0x05

#define IPCMSG_COLD_OFF		0x80	/* Only for Tangier */

#define IPCMSG_WARM_RESET	0xF0
#define IPCMSG_COLD_RESET	0xF1
#define IPCMSG_SOFT_RESET	0xF2
#define IPCMSG_COLD_BOOT	0xF3

#define IPCMSG_VRTC		0xFA	/* Set vRTC device */
/* Command id associated with message IPCMSG_VRTC */
#define IPC_CMD_VRTC_SETTIME      1	/* Set time */
#define IPC_CMD_VRTC_SETALARM     2	/* Set alarm */

/* Don't call these in new code - they will be removed eventually */

/* Read single register */
static inline int intel_scu_ipc_ioread8(u16 addr, u8 *data)
{
	return intel_scu_ipc_dev_ioread8(NULL, addr, data);
}

/* Read a vector */
static inline int intel_scu_ipc_readv(u16 *addr, u8 *data, int len)
{
	return intel_scu_ipc_dev_readv(NULL, addr, data, len);
}

/* Write single register */
static inline int intel_scu_ipc_iowrite8(u16 addr, u8 data)
{
	return intel_scu_ipc_dev_iowrite8(NULL, addr, data);
}

/* Write a vector */
static inline int intel_scu_ipc_writev(u16 *addr, u8 *data, int len)
{
	return intel_scu_ipc_dev_writev(NULL, addr, data, len);
}

/* Update single register based on the mask */
static inline int intel_scu_ipc_update_register(u16 addr, u8 data, u8 mask)
{
	return intel_scu_ipc_dev_update(NULL, addr, data, mask);
}

/* Issue commands to the SCU with or without data */
static inline int intel_scu_ipc_simple_command(int cmd, int sub)
{
	return intel_scu_ipc_dev_simple_command(NULL, cmd, sub);
}

static inline int intel_scu_ipc_command(int cmd, int sub, u32 *in, int inlen,
					u32 *out, int outlen)
{
	/* New API takes both inlen and outlen as bytes so convert here */
	size_t inbytes = inlen * sizeof(u32);
	size_t outbytes = outlen * sizeof(u32);

	return intel_scu_ipc_dev_command_with_size(NULL, cmd, sub, in, inbytes,
						   inlen, out, outbytes);
}

extern struct blocking_notifier_head intel_scu_notifier;

static inline void intel_scu_notifier_add(struct notifier_block *nb)
{
	blocking_notifier_chain_register(&intel_scu_notifier, nb);
}

static inline void intel_scu_notifier_remove(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&intel_scu_notifier, nb);
}

static inline int intel_scu_notifier_post(unsigned long v, void *p)
{
	return blocking_notifier_call_chain(&intel_scu_notifier, v, p);
}

#define		SCU_AVAILABLE		1
#define		SCU_DOWN		2

#endif

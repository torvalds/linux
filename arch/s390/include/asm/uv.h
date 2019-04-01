/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ultravisor Interfaces
 *
 * Copyright IBM Corp. 2019
 *
 * Author(s):
 *	Vasily Gorbik <gor@linux.ibm.com>
 *	Janosch Frank <frankja@linux.ibm.com>
 */
#ifndef _ASM_S390_UV_H
#define _ASM_S390_UV_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/bug.h>
#include <asm/page.h>

#define UVC_RC_EXECUTED		0x0001
#define UVC_RC_INV_CMD		0x0002
#define UVC_RC_INV_STATE	0x0003
#define UVC_RC_INV_LEN		0x0005
#define UVC_RC_NO_RESUME	0x0007

#define UVC_CMD_QUI			0x0001
#define UVC_CMD_SET_SHARED_ACCESS	0x1000
#define UVC_CMD_REMOVE_SHARED_ACCESS	0x1001

/* Bits in installed uv calls */
enum uv_cmds_inst {
	BIT_UVC_CMD_QUI = 0,
	BIT_UVC_CMD_SET_SHARED_ACCESS = 8,
	BIT_UVC_CMD_REMOVE_SHARED_ACCESS = 9,
};

struct uv_cb_header {
	u16 len;
	u16 cmd;	/* Command Code */
	u16 rc;		/* Response Code */
	u16 rrc;	/* Return Reason Code */
} __packed __aligned(8);

struct uv_cb_qui {
	struct uv_cb_header header;
	u64 reserved08;
	u64 inst_calls_list[4];
	u64 reserved30[15];
} __packed __aligned(8);

struct uv_cb_share {
	struct uv_cb_header header;
	u64 reserved08[3];
	u64 paddr;
	u64 reserved28;
} __packed __aligned(8);

static inline int uv_call(unsigned long r1, unsigned long r2)
{
	int cc;

	asm volatile(
		"0:	.insn rrf,0xB9A40000,%[r1],%[r2],0,0\n"
		"		brc	3,0b\n"
		"		ipm	%[cc]\n"
		"		srl	%[cc],28\n"
		: [cc] "=d" (cc)
		: [r1] "a" (r1), [r2] "a" (r2)
		: "memory", "cc");
	return cc;
}

#ifdef CONFIG_PROTECTED_VIRTUALIZATION_GUEST
extern int prot_virt_guest;

static inline int is_prot_virt_guest(void)
{
	return prot_virt_guest;
}

static inline int share(unsigned long addr, u16 cmd)
{
	struct uv_cb_share uvcb = {
		.header.cmd = cmd,
		.header.len = sizeof(uvcb),
		.paddr = addr
	};

	if (!is_prot_virt_guest())
		return -ENOTSUPP;
	/*
	 * Sharing is page wise, if we encounter addresses that are
	 * not page aligned, we assume something went wrong. If
	 * malloced structs are passed to this function, we could leak
	 * data to the hypervisor.
	 */
	BUG_ON(addr & ~PAGE_MASK);

	if (!uv_call(0, (u64)&uvcb))
		return 0;
	return -EINVAL;
}

/*
 * Guest 2 request to the Ultravisor to make a page shared with the
 * hypervisor for IO.
 *
 * @addr: Real or absolute address of the page to be shared
 */
static inline int uv_set_shared(unsigned long addr)
{
	return share(addr, UVC_CMD_SET_SHARED_ACCESS);
}

/*
 * Guest 2 request to the Ultravisor to make a page unshared.
 *
 * @addr: Real or absolute address of the page to be unshared
 */
static inline int uv_remove_shared(unsigned long addr)
{
	return share(addr, UVC_CMD_REMOVE_SHARED_ACCESS);
}

void uv_query_info(void);
#else
#define is_prot_virt_guest() 0
static inline int uv_set_shared(unsigned long addr) { return 0; }
static inline int uv_remove_shared(unsigned long addr) { return 0; }
static inline void uv_query_info(void) {}
#endif

#endif /* _ASM_S390_UV_H */

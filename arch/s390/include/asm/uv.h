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
#include <linux/sched.h>
#include <asm/page.h>
#include <asm/gmap.h>

#define UVC_RC_EXECUTED		0x0001
#define UVC_RC_INV_CMD		0x0002
#define UVC_RC_INV_STATE	0x0003
#define UVC_RC_INV_LEN		0x0005
#define UVC_RC_NO_RESUME	0x0007
#define UVC_RC_NEED_DESTROY	0x8000

#define UVC_CMD_QUI			0x0001
#define UVC_CMD_INIT_UV			0x000f
#define UVC_CMD_CREATE_SEC_CONF		0x0100
#define UVC_CMD_DESTROY_SEC_CONF	0x0101
#define UVC_CMD_CREATE_SEC_CPU		0x0120
#define UVC_CMD_DESTROY_SEC_CPU		0x0121
#define UVC_CMD_CONV_TO_SEC_STOR	0x0200
#define UVC_CMD_CONV_FROM_SEC_STOR	0x0201
#define UVC_CMD_DESTR_SEC_STOR		0x0202
#define UVC_CMD_SET_SEC_CONF_PARAMS	0x0300
#define UVC_CMD_UNPACK_IMG		0x0301
#define UVC_CMD_VERIFY_IMG		0x0302
#define UVC_CMD_CPU_RESET		0x0310
#define UVC_CMD_CPU_RESET_INITIAL	0x0311
#define UVC_CMD_PREPARE_RESET		0x0320
#define UVC_CMD_CPU_RESET_CLEAR		0x0321
#define UVC_CMD_CPU_SET_STATE		0x0330
#define UVC_CMD_SET_UNSHARE_ALL		0x0340
#define UVC_CMD_PIN_PAGE_SHARED		0x0341
#define UVC_CMD_UNPIN_PAGE_SHARED	0x0342
#define UVC_CMD_SET_SHARED_ACCESS	0x1000
#define UVC_CMD_REMOVE_SHARED_ACCESS	0x1001

/* Bits in installed uv calls */
enum uv_cmds_inst {
	BIT_UVC_CMD_QUI = 0,
	BIT_UVC_CMD_INIT_UV = 1,
	BIT_UVC_CMD_CREATE_SEC_CONF = 2,
	BIT_UVC_CMD_DESTROY_SEC_CONF = 3,
	BIT_UVC_CMD_CREATE_SEC_CPU = 4,
	BIT_UVC_CMD_DESTROY_SEC_CPU = 5,
	BIT_UVC_CMD_CONV_TO_SEC_STOR = 6,
	BIT_UVC_CMD_CONV_FROM_SEC_STOR = 7,
	BIT_UVC_CMD_SET_SHARED_ACCESS = 8,
	BIT_UVC_CMD_REMOVE_SHARED_ACCESS = 9,
	BIT_UVC_CMD_SET_SEC_PARMS = 11,
	BIT_UVC_CMD_UNPACK_IMG = 13,
	BIT_UVC_CMD_VERIFY_IMG = 14,
	BIT_UVC_CMD_CPU_RESET = 15,
	BIT_UVC_CMD_CPU_RESET_INITIAL = 16,
	BIT_UVC_CMD_CPU_SET_STATE = 17,
	BIT_UVC_CMD_PREPARE_RESET = 18,
	BIT_UVC_CMD_CPU_PERFORM_CLEAR_RESET = 19,
	BIT_UVC_CMD_UNSHARE_ALL = 20,
	BIT_UVC_CMD_PIN_PAGE_SHARED = 21,
	BIT_UVC_CMD_UNPIN_PAGE_SHARED = 22,
};

enum uv_feat_ind {
	BIT_UV_FEAT_MISC = 0,
};

struct uv_cb_header {
	u16 len;
	u16 cmd;	/* Command Code */
	u16 rc;		/* Response Code */
	u16 rrc;	/* Return Reason Code */
} __packed __aligned(8);

/* Query Ultravisor Information */
struct uv_cb_qui {
	struct uv_cb_header header;
	u64 reserved08;
	u64 inst_calls_list[4];
	u64 reserved30[2];
	u64 uv_base_stor_len;
	u64 reserved48;
	u64 conf_base_phys_stor_len;
	u64 conf_base_virt_stor_len;
	u64 conf_virt_var_stor_len;
	u64 cpu_stor_len;
	u32 reserved70[3];
	u32 max_num_sec_conf;
	u64 max_guest_stor_addr;
	u8  reserved88[158 - 136];
	u16 max_guest_cpu_id;
	u64 uv_feature_indications;
	u8  reserveda0[200 - 168];
} __packed __aligned(8);

/* Initialize Ultravisor */
struct uv_cb_init {
	struct uv_cb_header header;
	u64 reserved08[2];
	u64 stor_origin;
	u64 stor_len;
	u64 reserved28[4];
} __packed __aligned(8);

/* Create Guest Configuration */
struct uv_cb_cgc {
	struct uv_cb_header header;
	u64 reserved08[2];
	u64 guest_handle;
	u64 conf_base_stor_origin;
	u64 conf_virt_stor_origin;
	u64 reserved30;
	u64 guest_stor_origin;
	u64 guest_stor_len;
	u64 guest_sca;
	u64 guest_asce;
	u64 reserved58[5];
} __packed __aligned(8);

/* Create Secure CPU */
struct uv_cb_csc {
	struct uv_cb_header header;
	u64 reserved08[2];
	u64 cpu_handle;
	u64 guest_handle;
	u64 stor_origin;
	u8  reserved30[6];
	u16 num;
	u64 state_origin;
	u64 reserved40[4];
} __packed __aligned(8);

/* Convert to Secure */
struct uv_cb_cts {
	struct uv_cb_header header;
	u64 reserved08[2];
	u64 guest_handle;
	u64 gaddr;
} __packed __aligned(8);

/* Convert from Secure / Pin Page Shared */
struct uv_cb_cfs {
	struct uv_cb_header header;
	u64 reserved08[2];
	u64 paddr;
} __packed __aligned(8);

/* Set Secure Config Parameter */
struct uv_cb_ssc {
	struct uv_cb_header header;
	u64 reserved08[2];
	u64 guest_handle;
	u64 sec_header_origin;
	u32 sec_header_len;
	u32 reserved2c;
	u64 reserved30[4];
} __packed __aligned(8);

/* Unpack */
struct uv_cb_unp {
	struct uv_cb_header header;
	u64 reserved08[2];
	u64 guest_handle;
	u64 gaddr;
	u64 tweak[2];
	u64 reserved38[3];
} __packed __aligned(8);

#define PV_CPU_STATE_OPR	1
#define PV_CPU_STATE_STP	2
#define PV_CPU_STATE_CHKSTP	3
#define PV_CPU_STATE_OPR_LOAD	5

struct uv_cb_cpu_set_state {
	struct uv_cb_header header;
	u64 reserved08[2];
	u64 cpu_handle;
	u8  reserved20[7];
	u8  state;
	u64 reserved28[5];
};

/*
 * A common UV call struct for calls that take no payload
 * Examples:
 * Destroy cpu/config
 * Verify
 */
struct uv_cb_nodata {
	struct uv_cb_header header;
	u64 reserved08[2];
	u64 handle;
	u64 reserved20[4];
} __packed __aligned(8);

/* Set Shared Access */
struct uv_cb_share {
	struct uv_cb_header header;
	u64 reserved08[3];
	u64 paddr;
	u64 reserved28;
} __packed __aligned(8);

static inline int __uv_call(unsigned long r1, unsigned long r2)
{
	int cc;

	asm volatile(
		"	.insn rrf,0xB9A40000,%[r1],%[r2],0,0\n"
		"	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		: [cc] "=d" (cc)
		: [r1] "a" (r1), [r2] "a" (r2)
		: "memory", "cc");
	return cc;
}

static inline int uv_call(unsigned long r1, unsigned long r2)
{
	int cc;

	do {
		cc = __uv_call(r1, r2);
	} while (cc > 1);
	return cc;
}

/* Low level uv_call that avoids stalls for long running busy conditions  */
static inline int uv_call_sched(unsigned long r1, unsigned long r2)
{
	int cc;

	do {
		cc = __uv_call(r1, r2);
		cond_resched();
	} while (cc > 1);
	return cc;
}

/*
 * special variant of uv_call that only transports the cpu or guest
 * handle and the command, like destroy or verify.
 */
static inline int uv_cmd_nodata(u64 handle, u16 cmd, u16 *rc, u16 *rrc)
{
	struct uv_cb_nodata uvcb = {
		.header.cmd = cmd,
		.header.len = sizeof(uvcb),
		.handle = handle,
	};
	int cc;

	WARN(!handle, "No handle provided to Ultravisor call cmd %x\n", cmd);
	cc = uv_call_sched(0, (u64)&uvcb);
	*rc = uvcb.header.rc;
	*rrc = uvcb.header.rrc;
	return cc ? -EINVAL : 0;
}

struct uv_info {
	unsigned long inst_calls_list[4];
	unsigned long uv_base_stor_len;
	unsigned long guest_base_stor_len;
	unsigned long guest_virt_base_stor_len;
	unsigned long guest_virt_var_stor_len;
	unsigned long guest_cpu_stor_len;
	unsigned long max_sec_stor_addr;
	unsigned int max_num_sec_conf;
	unsigned short max_guest_cpu_id;
	unsigned long uv_feature_indications;
};

extern struct uv_info uv_info;

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
		return -EOPNOTSUPP;
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

#else
#define is_prot_virt_guest() 0
static inline int uv_set_shared(unsigned long addr) { return 0; }
static inline int uv_remove_shared(unsigned long addr) { return 0; }
#endif

#if IS_ENABLED(CONFIG_KVM)
extern int prot_virt_host;

static inline int is_prot_virt_host(void)
{
	return prot_virt_host;
}

int gmap_make_secure(struct gmap *gmap, unsigned long gaddr, void *uvcb);
int uv_destroy_page(unsigned long paddr);
int uv_convert_from_secure(unsigned long paddr);
int gmap_convert_to_secure(struct gmap *gmap, unsigned long gaddr);

void setup_uv(void);
void adjust_to_uv_max(unsigned long *vmax);
#else
#define is_prot_virt_host() 0
static inline void setup_uv(void) {}
static inline void adjust_to_uv_max(unsigned long *vmax) {}

static inline int uv_destroy_page(unsigned long paddr)
{
	return 0;
}

static inline int uv_convert_from_secure(unsigned long paddr)
{
	return 0;
}
#endif

#if defined(CONFIG_PROTECTED_VIRTUALIZATION_GUEST) || IS_ENABLED(CONFIG_KVM)
void uv_query_info(void);
#else
static inline void uv_query_info(void) {}
#endif

#endif /* _ASM_S390_UV_H */

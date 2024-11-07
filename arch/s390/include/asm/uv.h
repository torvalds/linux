/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ultravisor Interfaces
 *
 * Copyright IBM Corp. 2019, 2024
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
#include <asm/asm.h>

#define UVC_CC_OK	0
#define UVC_CC_ERROR	1
#define UVC_CC_BUSY	2
#define UVC_CC_PARTIAL	3

#define UVC_RC_EXECUTED		0x0001
#define UVC_RC_INV_CMD		0x0002
#define UVC_RC_INV_STATE	0x0003
#define UVC_RC_INV_LEN		0x0005
#define UVC_RC_NO_RESUME	0x0007
#define UVC_RC_MORE_DATA	0x0100
#define UVC_RC_NEED_DESTROY	0x8000

#define UVC_CMD_QUI			0x0001
#define UVC_CMD_QUERY_KEYS		0x0002
#define UVC_CMD_INIT_UV			0x000f
#define UVC_CMD_CREATE_SEC_CONF		0x0100
#define UVC_CMD_DESTROY_SEC_CONF	0x0101
#define UVC_CMD_DESTROY_SEC_CONF_FAST	0x0102
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
#define UVC_CMD_DUMP_INIT		0x0400
#define UVC_CMD_DUMP_CONF_STOR_STATE	0x0401
#define UVC_CMD_DUMP_CPU		0x0402
#define UVC_CMD_DUMP_COMPLETE		0x0403
#define UVC_CMD_SET_SHARED_ACCESS	0x1000
#define UVC_CMD_REMOVE_SHARED_ACCESS	0x1001
#define UVC_CMD_RETR_ATTEST		0x1020
#define UVC_CMD_ADD_SECRET		0x1031
#define UVC_CMD_LIST_SECRETS		0x1033
#define UVC_CMD_LOCK_SECRETS		0x1034
#define UVC_CMD_RETR_SECRET		0x1035

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
	BIT_UVC_CMD_DESTROY_SEC_CONF_FAST = 23,
	BIT_UVC_CMD_DUMP_INIT = 24,
	BIT_UVC_CMD_DUMP_CONFIG_STOR_STATE = 25,
	BIT_UVC_CMD_DUMP_CPU = 26,
	BIT_UVC_CMD_DUMP_COMPLETE = 27,
	BIT_UVC_CMD_RETR_ATTEST = 28,
	BIT_UVC_CMD_ADD_SECRET = 29,
	BIT_UVC_CMD_LIST_SECRETS = 30,
	BIT_UVC_CMD_LOCK_SECRETS = 31,
	BIT_UVC_CMD_RETR_SECRET = 33,
	BIT_UVC_CMD_QUERY_KEYS = 34,
};

enum uv_feat_ind {
	BIT_UV_FEAT_MISC = 0,
	BIT_UV_FEAT_AIV = 1,
	BIT_UV_FEAT_AP = 4,
	BIT_UV_FEAT_AP_INTR = 5,
};

struct uv_cb_header {
	u16 len;
	u16 cmd;	/* Command Code */
	u16 rc;		/* Response Code */
	u16 rrc;	/* Return Reason Code */
} __packed __aligned(8);

/* Query Ultravisor Information */
struct uv_cb_qui {
	struct uv_cb_header header;		/* 0x0000 */
	u64 reserved08;				/* 0x0008 */
	u64 inst_calls_list[4];			/* 0x0010 */
	u64 reserved30[2];			/* 0x0030 */
	u64 uv_base_stor_len;			/* 0x0040 */
	u64 reserved48;				/* 0x0048 */
	u64 conf_base_phys_stor_len;		/* 0x0050 */
	u64 conf_base_virt_stor_len;		/* 0x0058 */
	u64 conf_virt_var_stor_len;		/* 0x0060 */
	u64 cpu_stor_len;			/* 0x0068 */
	u32 reserved70[3];			/* 0x0070 */
	u32 max_num_sec_conf;			/* 0x007c */
	u64 max_guest_stor_addr;		/* 0x0080 */
	u8  reserved88[0x9e - 0x88];		/* 0x0088 */
	u16 max_guest_cpu_id;			/* 0x009e */
	u64 uv_feature_indications;		/* 0x00a0 */
	u64 reserveda8;				/* 0x00a8 */
	u64 supp_se_hdr_versions;		/* 0x00b0 */
	u64 supp_se_hdr_pcf;			/* 0x00b8 */
	u64 reservedc0;				/* 0x00c0 */
	u64 conf_dump_storage_state_len;	/* 0x00c8 */
	u64 conf_dump_finalize_len;		/* 0x00d0 */
	u64 reservedd8;				/* 0x00d8 */
	u64 supp_att_req_hdr_ver;		/* 0x00e0 */
	u64 supp_att_pflags;			/* 0x00e8 */
	u64 reservedf0;				/* 0x00f0 */
	u64 supp_add_secret_req_ver;		/* 0x00f8 */
	u64 supp_add_secret_pcf;		/* 0x0100 */
	u64 supp_secret_types;			/* 0x0108 */
	u16 max_assoc_secrets;			/* 0x0110 */
	u16 max_retr_secrets;			/* 0x0112 */
	u8 reserved114[0x120 - 0x114];		/* 0x0114 */
} __packed __aligned(8);

struct uv_key_hash {
	u64 dword[4];
} __packed __aligned(8);

#define UVC_QUERY_KEYS_IDX_HK		0
#define UVC_QUERY_KEYS_IDX_BACK_HK	1

/* Query Ultravisor Keys */
struct uv_cb_query_keys {
	struct uv_cb_header header;		/* 0x0000 */
	u64 reserved08[3];			/* 0x0008 */
	struct uv_key_hash key_hashes[15];	/* 0x0020 */
} __packed __aligned(8);
static_assert(sizeof(struct uv_cb_query_keys) == 0x200);

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
	u8  reserved30[6];
	union {
		struct {
			u16 : 14;
			u16 ap_instr_intr : 1;
			u16 ap_allow_instr : 1;
		};
		u16 raw;
	} flags;
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

/* Destroy Configuration Fast */
struct uv_cb_destroy_fast {
	struct uv_cb_header header;
	u64 reserved08[2];
	u64 handle;
	u64 reserved20[5];
} __packed __aligned(8);

/* Set Shared Access */
struct uv_cb_share {
	struct uv_cb_header header;
	u64 reserved08[3];
	u64 paddr;
	u64 reserved28;
} __packed __aligned(8);

/* Retrieve Attestation Measurement */
struct uv_cb_attest {
	struct uv_cb_header header;	/* 0x0000 */
	u64 reserved08[2];		/* 0x0008 */
	u64 arcb_addr;			/* 0x0018 */
	u64 cont_token;			/* 0x0020 */
	u8  reserved28[6];		/* 0x0028 */
	u16 user_data_len;		/* 0x002e */
	u8  user_data[256];		/* 0x0030 */
	u32 reserved130[3];		/* 0x0130 */
	u32 meas_len;			/* 0x013c */
	u64 meas_addr;			/* 0x0140 */
	u8  config_uid[16];		/* 0x0148 */
	u32 reserved158;		/* 0x0158 */
	u32 add_data_len;		/* 0x015c */
	u64 add_data_addr;		/* 0x0160 */
	u64 reserved168[4];		/* 0x0168 */
} __packed __aligned(8);

struct uv_cb_dump_cpu {
	struct uv_cb_header header;
	u64 reserved08[2];
	u64 cpu_handle;
	u64 dump_area_origin;
	u64 reserved28[5];
} __packed __aligned(8);

struct uv_cb_dump_stor_state {
	struct uv_cb_header header;
	u64 reserved08[2];
	u64 config_handle;
	u64 dump_area_origin;
	u64 gaddr;
	u64 reserved28[4];
} __packed __aligned(8);

struct uv_cb_dump_complete {
	struct uv_cb_header header;
	u64 reserved08[2];
	u64 config_handle;
	u64 dump_area_origin;
	u64 reserved30[5];
} __packed __aligned(8);

/*
 * A common UV call struct for pv guests that contains a single address
 * Examples:
 * Add Secret
 */
struct uv_cb_guest_addr {
	struct uv_cb_header header;
	u64 reserved08[3];
	u64 addr;
	u64 reserved28[4];
} __packed __aligned(8);

#define UVC_RC_RETR_SECR_BUF_SMALL	0x0109
#define UVC_RC_RETR_SECR_STORE_EMPTY	0x010f
#define UVC_RC_RETR_SECR_INV_IDX	0x0110
#define UVC_RC_RETR_SECR_INV_SECRET	0x0111

struct uv_cb_retr_secr {
	struct uv_cb_header header;
	u64 reserved08[2];
	u16 secret_idx;
	u16 reserved1a;
	u32 buf_size;
	u64 buf_addr;
	u64 reserved28[4];
}  __packed __aligned(8);

struct uv_cb_list_secrets {
	struct uv_cb_header header;
	u64 reserved08[2];
	u8  reserved18[6];
	u16 start_idx;
	u64 list_addr;
	u64 reserved28[4];
} __packed __aligned(8);

enum uv_secret_types {
	UV_SECRET_INVAL = 0x0,
	UV_SECRET_NULL = 0x1,
	UV_SECRET_ASSOCIATION = 0x2,
	UV_SECRET_PLAIN = 0x3,
	UV_SECRET_AES_128 = 0x4,
	UV_SECRET_AES_192 = 0x5,
	UV_SECRET_AES_256 = 0x6,
	UV_SECRET_AES_XTS_128 = 0x7,
	UV_SECRET_AES_XTS_256 = 0x8,
	UV_SECRET_HMAC_SHA_256 = 0x9,
	UV_SECRET_HMAC_SHA_512 = 0xa,
	/* 0x0b - 0x10 reserved */
	UV_SECRET_ECDSA_P256 = 0x11,
	UV_SECRET_ECDSA_P384 = 0x12,
	UV_SECRET_ECDSA_P521 = 0x13,
	UV_SECRET_ECDSA_ED25519 = 0x14,
	UV_SECRET_ECDSA_ED448 = 0x15,
};

/**
 * uv_secret_list_item_hdr - UV secret metadata.
 * @index: Index of the secret in the secret list.
 * @type: Type of the secret. See `enum uv_secret_types`.
 * @length: Length of the stored secret.
 */
struct uv_secret_list_item_hdr {
	u16 index;
	u16 type;
	u32 length;
} __packed __aligned(8);

#define UV_SECRET_ID_LEN 32
/**
 * uv_secret_list_item - UV secret entry.
 * @hdr: The metadata of this secret.
 * @id: The ID of this secret, not the secret itself.
 */
struct uv_secret_list_item {
	struct uv_secret_list_item_hdr hdr;
	u64 reserverd08;
	u8 id[UV_SECRET_ID_LEN];
} __packed __aligned(8);

/**
 * uv_secret_list - UV secret-metadata list.
 * @num_secr_stored: Number of secrets stored in this list.
 * @total_num_secrets: Number of secrets stored in the UV for this guest.
 * @next_secret_idx: positive number if there are more secrets available or zero.
 * @secrets: Up to 85 UV-secret metadata entries.
 */
struct uv_secret_list {
	u16 num_secr_stored;
	u16 total_num_secrets;
	u16 next_secret_idx;
	u16 reserved_06;
	u64 reserved_08;
	struct uv_secret_list_item secrets[85];
} __packed __aligned(8);
static_assert(sizeof(struct uv_secret_list) == PAGE_SIZE);

static inline int __uv_call(unsigned long r1, unsigned long r2)
{
	int cc;

	asm volatile(
		"	.insn	 rrf,0xb9a40000,%[r1],%[r2],0,0\n"
		CC_IPM(cc)
		: CC_OUT(cc, cc)
		: [r1] "a" (r1), [r2] "a" (r2)
		: CC_CLOBBER_LIST("memory"));
	return CC_TRANSFORM(cc);
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

/**
 * uv_list_secrets() - Do a List Secrets UVC.
 *
 * @buf: Buffer to write list into; size of one page.
 * @start_idx: The smallest index that should be included in the list.
 *		For the fist invocation use 0.
 * @rc: Pointer to store the return code or NULL.
 * @rrc: Pointer to store the return reason code or NULL.
 *
 * This function calls the List Secrets UVC. The result is written into `buf`,
 * that needs to be at least one page of writable memory.
 * `buf` consists of:
 * * %struct uv_secret_list_hdr
 * * %struct uv_secret_list_item (multiple)
 *
 * For `start_idx` use _0_ for the first call. If there are more secrets available
 * but could not fit into the page then `rc` is `UVC_RC_MORE_DATA`.
 * In this case use `uv_secret_list_hdr.next_secret_idx` for `start_idx`.
 *
 * Context: might sleep.
 *
 * Return: The UVC condition code.
 */
static inline int uv_list_secrets(struct uv_secret_list *buf, u16 start_idx,
				  u16 *rc, u16 *rrc)
{
	struct uv_cb_list_secrets uvcb = {
		.header.len = sizeof(uvcb),
		.header.cmd = UVC_CMD_LIST_SECRETS,
		.start_idx = start_idx,
		.list_addr = (u64)buf,
	};
	int cc = uv_call_sched(0, (u64)&uvcb);

	if (rc)
		*rc = uvcb.header.rc;
	if (rrc)
		*rrc = uvcb.header.rrc;

	return cc;
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
	unsigned long supp_se_hdr_ver;
	unsigned long supp_se_hdr_pcf;
	unsigned long conf_dump_storage_state_len;
	unsigned long conf_dump_finalize_len;
	unsigned long supp_att_req_hdr_ver;
	unsigned long supp_att_pflags;
	unsigned long supp_add_secret_req_ver;
	unsigned long supp_add_secret_pcf;
	unsigned long supp_secret_types;
	unsigned short max_assoc_secrets;
	unsigned short max_retr_secrets;
};

extern struct uv_info uv_info;

static inline bool uv_has_feature(u8 feature_bit)
{
	if (feature_bit >= sizeof(uv_info.uv_feature_indications) * 8)
		return false;
	return test_bit_inv(feature_bit, &uv_info.uv_feature_indications);
}

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
	pr_err("%s UVC failed (rc: 0x%x, rrc: 0x%x), possible hypervisor bug.\n",
	       uvcb.header.cmd == UVC_CMD_SET_SHARED_ACCESS ? "Share" : "Unshare",
	       uvcb.header.rc, uvcb.header.rrc);
	panic("System security cannot be guaranteed unless the system panics now.\n");
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

int uv_get_secret_metadata(const u8 secret_id[UV_SECRET_ID_LEN],
			   struct uv_secret_list_item_hdr *secret);
int uv_retrieve_secret(u16 secret_idx, u8 *buf, size_t buf_size);

extern int prot_virt_host;

static inline int is_prot_virt_host(void)
{
	return prot_virt_host;
}

int uv_pin_shared(unsigned long paddr);
int gmap_make_secure(struct gmap *gmap, unsigned long gaddr, void *uvcb);
int gmap_destroy_page(struct gmap *gmap, unsigned long gaddr);
int uv_destroy_folio(struct folio *folio);
int uv_destroy_pte(pte_t pte);
int uv_convert_from_secure_pte(pte_t pte);
int gmap_convert_to_secure(struct gmap *gmap, unsigned long gaddr);

void setup_uv(void);

#endif /* _ASM_S390_UV_H */

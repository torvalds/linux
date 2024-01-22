/* SPDX-License-Identifier: GPL-2.0 */
#ifndef S390_ISM_H
#define S390_ISM_H

#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/ism.h>
#include <net/smc.h>
#include <asm/pci_insn.h>

#define UTIL_STR_LEN	16

/*
 * Do not use the first word of the DMB bits to ensure 8 byte aligned access.
 */
#define ISM_DMB_WORD_OFFSET	1
#define ISM_DMB_BIT_OFFSET	(ISM_DMB_WORD_OFFSET * 32)

#define ISM_REG_SBA	0x1
#define ISM_REG_IEQ	0x2
#define ISM_READ_GID	0x3
#define ISM_ADD_VLAN_ID	0x4
#define ISM_DEL_VLAN_ID	0x5
#define ISM_SET_VLAN	0x6
#define ISM_RESET_VLAN	0x7
#define ISM_QUERY_INFO	0x8
#define ISM_QUERY_RGID	0x9
#define ISM_REG_DMB	0xA
#define ISM_UNREG_DMB	0xB
#define ISM_SIGNAL_IEQ	0xE
#define ISM_UNREG_SBA	0x11
#define ISM_UNREG_IEQ	0x12

struct ism_req_hdr {
	u32 cmd;
	u16 : 16;
	u16 len;
};

struct ism_resp_hdr {
	u32 cmd;
	u16 ret;
	u16 len;
};

union ism_reg_sba {
	struct {
		struct ism_req_hdr hdr;
		u64 sba;
	} request;
	struct {
		struct ism_resp_hdr hdr;
	} response;
} __aligned(16);

union ism_reg_ieq {
	struct {
		struct ism_req_hdr hdr;
		u64 ieq;
		u64 len;
	} request;
	struct {
		struct ism_resp_hdr hdr;
	} response;
} __aligned(16);

union ism_read_gid {
	struct {
		struct ism_req_hdr hdr;
	} request;
	struct {
		struct ism_resp_hdr hdr;
		u64 gid;
	} response;
} __aligned(16);

union ism_qi {
	struct {
		struct ism_req_hdr hdr;
	} request;
	struct {
		struct ism_resp_hdr hdr;
		u32 version;
		u32 max_len;
		u64 ism_state;
		u64 my_gid;
		u64 sba;
		u64 ieq;
		u32 ieq_len;
		u32 : 32;
		u32 dmbs_owned;
		u32 dmbs_used;
		u32 vlan_required;
		u32 vlan_nr_ids;
		u16 vlan_id[64];
	} response;
} __aligned(64);

union ism_query_rgid {
	struct {
		struct ism_req_hdr hdr;
		u64 rgid;
		u32 vlan_valid;
		u32 vlan_id;
	} request;
	struct {
		struct ism_resp_hdr hdr;
	} response;
} __aligned(16);

union ism_reg_dmb {
	struct {
		struct ism_req_hdr hdr;
		u64 dmb;
		u32 dmb_len;
		u32 sba_idx;
		u32 vlan_valid;
		u32 vlan_id;
		u64 rgid;
	} request;
	struct {
		struct ism_resp_hdr hdr;
		u64 dmb_tok;
	} response;
} __aligned(32);

union ism_sig_ieq {
	struct {
		struct ism_req_hdr hdr;
		u64 rgid;
		u32 trigger_irq;
		u32 event_code;
		u64 info;
	} request;
	struct {
		struct ism_resp_hdr hdr;
	} response;
} __aligned(32);

union ism_unreg_dmb {
	struct {
		struct ism_req_hdr hdr;
		u64 dmb_tok;
	} request;
	struct {
		struct ism_resp_hdr hdr;
	} response;
} __aligned(16);

union ism_cmd_simple {
	struct {
		struct ism_req_hdr hdr;
	} request;
	struct {
		struct ism_resp_hdr hdr;
	} response;
} __aligned(8);

union ism_set_vlan_id {
	struct {
		struct ism_req_hdr hdr;
		u64 vlan_id;
	} request;
	struct {
		struct ism_resp_hdr hdr;
	} response;
} __aligned(16);

struct ism_eq_header {
	u64 idx;
	u64 ieq_len;
	u64 entry_len;
	u64 : 64;
};

struct ism_eq {
	struct ism_eq_header header;
	struct ism_event entry[15];
};

struct ism_sba {
	u32 s : 1;	/* summary bit */
	u32 e : 1;	/* event bit */
	u32 : 30;
	u32 dmb_bits[ISM_NR_DMBS / 32];
	u32 reserved[3];
	u16 dmbe_mask[ISM_NR_DMBS];
};

#define ISM_CREATE_REQ(dmb, idx, sf, offset)		\
	((dmb) | (idx) << 24 | (sf) << 23 | (offset))

static inline void __ism_read_cmd(struct ism_dev *ism, void *data,
				  unsigned long offset, unsigned long len)
{
	struct zpci_dev *zdev = to_zpci(ism->pdev);
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 2, 8);

	while (len > 0) {
		__zpci_load(data, req, offset);
		offset += 8;
		data += 8;
		len -= 8;
	}
}

static inline void __ism_write_cmd(struct ism_dev *ism, void *data,
				   unsigned long offset, unsigned long len)
{
	struct zpci_dev *zdev = to_zpci(ism->pdev);
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 2, len);

	if (len)
		__zpci_store_block(data, req, offset);
}

static inline int __ism_move(struct ism_dev *ism, u64 dmb_req, void *data,
			     unsigned int size)
{
	struct zpci_dev *zdev = to_zpci(ism->pdev);
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, size);

	return __zpci_store_block(data, req, dmb_req);
}

#endif /* S390_ISM_H */

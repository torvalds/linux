#ifndef _ASM_S390_PCI_CLP_H
#define _ASM_S390_PCI_CLP_H

#include <asm/clp.h>

/*
 * Call Logical Processor - Command Codes
 */
#define CLP_LIST_PCI		0x0002
#define CLP_QUERY_PCI_FN	0x0003
#define CLP_QUERY_PCI_FNGRP	0x0004
#define CLP_SET_PCI_FN		0x0005

/* PCI function handle list entry */
struct clp_fh_list_entry {
	u16 device_id;
	u16 vendor_id;
	u32 config_state :  1;
	u32		 : 31;
	u32 fid;		/* PCI function id */
	u32 fh;			/* PCI function handle */
} __packed;

#define CLP_RC_SETPCIFN_FH	0x0101	/* Invalid PCI fn handle */
#define CLP_RC_SETPCIFN_FHOP	0x0102	/* Fn handle not valid for op */
#define CLP_RC_SETPCIFN_DMAAS	0x0103	/* Invalid DMA addr space */
#define CLP_RC_SETPCIFN_RES	0x0104	/* Insufficient resources */
#define CLP_RC_SETPCIFN_ALRDY	0x0105	/* Fn already in requested state */
#define CLP_RC_SETPCIFN_ERR	0x0106	/* Fn in permanent error state */
#define CLP_RC_SETPCIFN_RECPND	0x0107	/* Error recovery pending */
#define CLP_RC_SETPCIFN_BUSY	0x0108	/* Fn busy */
#define CLP_RC_LISTPCI_BADRT	0x010a	/* Resume token not recognized */
#define CLP_RC_QUERYPCIFG_PFGID	0x010b	/* Unrecognized PFGID */

/* request or response block header length */
#define LIST_PCI_HDR_LEN	32

/* Number of function handles fitting in response block */
#define CLP_FH_LIST_NR_ENTRIES				\
	((CLP_BLK_SIZE - 2 * LIST_PCI_HDR_LEN)		\
		/ sizeof(struct clp_fh_list_entry))

#define CLP_SET_ENABLE_PCI_FN	0	/* Yes, 0 enables it */
#define CLP_SET_DISABLE_PCI_FN	1	/* Yes, 1 disables it */

#define CLP_UTIL_STR_LEN	64
#define CLP_PFIP_NR_SEGMENTS	4

/* List PCI functions request */
struct clp_req_list_pci {
	struct clp_req_hdr hdr;
	u64 resume_token;
	u64 reserved2;
} __packed;

/* List PCI functions response */
struct clp_rsp_list_pci {
	struct clp_rsp_hdr hdr;
	u64 resume_token;
	u32 reserved2;
	u16 max_fn;
	u8 reserved3;
	u8 entry_size;
	struct clp_fh_list_entry fh_list[CLP_FH_LIST_NR_ENTRIES];
} __packed;

/* Query PCI function request */
struct clp_req_query_pci {
	struct clp_req_hdr hdr;
	u32 fh;				/* function handle */
	u32 reserved2;
	u64 reserved3;
} __packed;

/* Query PCI function response */
struct clp_rsp_query_pci {
	struct clp_rsp_hdr hdr;
	u16 vfn;			/* virtual fn number */
	u16			:  7;
	u16 util_str_avail	:  1;	/* utility string available? */
	u16 pfgid		:  8;	/* pci function group id */
	u32 fid;			/* pci function id */
	u8 bar_size[PCI_BAR_COUNT];
	u16 pchid;
	u32 bar[PCI_BAR_COUNT];
	u8 pfip[CLP_PFIP_NR_SEGMENTS];	/* pci function internal path */
	u32			: 24;
	u8 pft;				/* pci function type */
	u64 sdma;			/* start dma as */
	u64 edma;			/* end dma as */
	u32 reserved[11];
	u32 uid;			/* user defined id */
	u8 util_str[CLP_UTIL_STR_LEN];	/* utility string */
} __packed;

/* Query PCI function group request */
struct clp_req_query_pci_grp {
	struct clp_req_hdr hdr;
	u32 reserved2		: 24;
	u32 pfgid		:  8;	/* function group id */
	u32 reserved3;
	u64 reserved4;
} __packed;

/* Query PCI function group response */
struct clp_rsp_query_pci_grp {
	struct clp_rsp_hdr hdr;
	u16			:  4;
	u16 noi			: 12;	/* number of interrupts */
	u8 version;
	u8			:  6;
	u8 frame		:  1;
	u8 refresh		:  1;	/* TLB refresh mode */
	u16 reserved2;
	u16 mui;
	u64 reserved3;
	u64 dasm;			/* dma address space mask */
	u64 msia;			/* MSI address */
	u64 reserved4;
	u64 reserved5;
} __packed;

/* Set PCI function request */
struct clp_req_set_pci {
	struct clp_req_hdr hdr;
	u32 fh;				/* function handle */
	u16 reserved2;
	u8 oc;				/* operation controls */
	u8 ndas;			/* number of dma spaces */
	u64 reserved3;
} __packed;

/* Set PCI function response */
struct clp_rsp_set_pci {
	struct clp_rsp_hdr hdr;
	u32 fh;				/* function handle */
	u32 reserved3;
	u64 reserved4;
} __packed;

/* Combined request/response block structures used by clp insn */
struct clp_req_rsp_list_pci {
	struct clp_req_list_pci request;
	struct clp_rsp_list_pci response;
} __packed;

struct clp_req_rsp_set_pci {
	struct clp_req_set_pci request;
	struct clp_rsp_set_pci response;
} __packed;

struct clp_req_rsp_query_pci {
	struct clp_req_query_pci request;
	struct clp_rsp_query_pci response;
} __packed;

struct clp_req_rsp_query_pci_grp {
	struct clp_req_query_pci_grp request;
	struct clp_rsp_query_pci_grp response;
} __packed;

#endif

/*
 * Misc utility routines for accessing chip-specific features
 * of the BOOKER NCI (non coherent interconnect) based Broadcom chips.
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2020,
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties,
 * copied or duplicated in any form, in whole or in part, without
 * the prior written permission of Broadcom.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 */

#include <typedefs.h>
#include <hndsoc.h>
#include <sbchipc.h>
#include <pcicfg.h>
#include <pcie_core.h>
#include "siutils_priv.h"
#include <nci.h>
#include <bcmdevs.h>
#include <hndoobr.h>

#define NCI_BAD_REG			0xbbadd000u	/* Bad Register Address */
#define NCI_BAD_INDEX			-1		/* Bad Index */

#define OOBR_BASE_MASK			0x00001FFFu	/* Mask to get Base address of OOBR */
#define EROM1_BASE_MASK			0x00000FFFu	/* Mask to get Base address of EROM1 */

/* Core Info */
#define COREINFO_COREID_MASK		0x00000FFFu	/* Bit-11 to 0 */
#define COREINFO_REV_MASK		0x000FF000u	/* Core Rev Mask */
#define COREINFO_REV_SHIFT		12u		/* Bit-12 */
#define COREINFO_MFG_MASK		0x00F00000u	/* Core Mfg Mask */
#define COREINFO_MFG_SHIFT		20u		/* Bit-20 */
#define COREINFO_BPID_MASK		0x07000000u	/* 26-24 Gives Backplane ID */
#define COREINFO_BPID_SHIFT		24u		/* Bit:26-24 */
#define COREINFO_ISBP_MASK		0x08000000u	/* Is Backplane or Bridge */
#define COREINFO_ISBP_SHIFT		27u		/* Bit:27 */

/* Interface Config */
#define IC_IFACECNT_MASK		0x0000F000u	/* No of Interface Descriptor Mask */
#define IC_IFACECNT_SHIFT		12u		/* Bit-12 */
#define IC_IFACEOFFSET_MASK		0x00000FFFu	/* OFFSET for 1st Interface Descriptor */

/* DMP Reg Offset */
#define DMP_DMPCTRL_REG_OFFSET		8u

/* Interface Descriptor Masks */
#define ID_NODEPTR_MASK			0xFFFFFFF8u	/* Master/Slave Network Interface Addr */
#define ID_NODETYPE_MASK		0x00000007u	/* 0:Booker 1:IDM 1-0xf:Reserved */
#define ID_WORDOFFSET_MASK		0xF0000000u	/* WordOffset to next Iface Desc in EROM2 */
#define ID_WORDOFFSET_SHIFT		28u		/* WordOffset bits 31-28 */
#define ID_CORETYPE_MASK		0x08000000u	/* CORE belongs to OOBR(0) or EROM(1) */
#define ID_CORETYPE_SHIFT		27u		/* Bit-27 */
#define ID_MI_MASK			0x04000000u	/* 0: Slave Interface, 1:Master Interface */
#define ID_MI_SHIFT			26u		/* Bit-26 */
#define ID_NADDR_MASK			0x03000000u	/* No of Slave Address Regions */
#define ID_NADDR_SHIFT			24u		/* Bit:25-24 */
#define ID_BPID_MASK			0x00F00000u	/* Give Backplane ID */
#define ID_BPID_SHIFT			20u		/* Bit:20-23 */
#define ID_COREINFOPTR_MASK		0x00001FFFu	/* OOBR or EROM Offset */
#define ID_ENDMARKER			0xFFFFFFFFu	/* End of EROM Part 2 */

/* Slave Port Address Descriptor Masks */
#define SLAVEPORT_BASE_ADDR_MASK	0xFFFFFF00u	/* Bits 31:8 is the base address */
#define SLAVEPORT_BOUND_ADDR_MASK	0x00000040u	/* Addr is not 2^n and with bound addr */
#define SLAVEPORT_BOUND_ADDR_SHIFT	6u		/* Bit-6 */
#define SLAVEPORT_64BIT_ADDR_MASK	0x00000020u	/* 64-bit base and bound fields */
#define SLAVEPORT_64BIT_ADDR_SHIFT	5u		/* Bit-5 */
#define SLAVEPORT_ADDR_SIZE_MASK	0x0000001Fu	/* Address Size mask */
#define SLAVEPORT_ADDR_TYPE_BOUND	0x1u		/* Bound Addr */
#define SLAVEPORT_ADDR_TYPE_64		0x2u		/* 64-Bit Addr */
#define SLAVEPORT_ADDR_MIN_SHIFT	0x8u
/* Address space Size of the slave port */
#define SLAVEPORT_ADDR_SIZE(adesc)	(1u << ((adesc & SLAVEPORT_ADDR_SIZE_MASK) + \
			SLAVEPORT_ADDR_MIN_SHIFT))

#define GET_NEXT_EROM_ADDR(addr)	((uint32*)((uintptr)(addr) + 4u))

#define NCI_DEFAULT_CORE_UNIT		(0u)

/* Error Codes */
enum {
	NCI_OK				= 0,
	NCI_BACKPLANE_ID_MISMATCH	= -1,
	NCI_INVALID_EROM2PTR		= -2,
	NCI_WORDOFFSET_MISMATCH		= -3,
	NCI_NOMEM			= -4,
	NCI_MASTER_INVALID_ADDR		= -5
};

#define GET_OOBR_BASE(erom2base)	((erom2base) & ~OOBR_BASE_MASK)
#define GET_EROM1_BASE(erom2base)	((erom2base) & ~EROM1_BASE_MASK)
#define CORE_ID(core_info)		((core_info) & COREINFO_COREID_MASK)
#define GET_INFACECNT(iface_cfg)	(((iface_cfg) & IC_IFACECNT_MASK) >> IC_IFACECNT_SHIFT)
#define GET_NODEPTR(iface_desc_0)	((iface_desc_0) & ID_NODEPTR_MASK)
#define GET_NODETYPE(iface_desc_0)	((iface_desc_0) & ID_NODETYPE_MASK)
#define GET_WORDOFFSET(iface_desc_1)	(((iface_desc_1) & ID_WORDOFFSET_MASK) \
					>> ID_WORDOFFSET_SHIFT)
#define IS_MASTER(iface_desc_1)		(((iface_desc_1) & ID_MI_MASK) >> ID_MI_SHIFT)
#define GET_CORETYPE(iface_desc_1)	(((iface_desc_1) & ID_CORETYPE_MASK) >> ID_CORETYPE_SHIFT)
#define GET_NUM_ADDR_REG(iface_desc_1)	(((iface_desc_1) & ID_NADDR_MASK) >> ID_NADDR_SHIFT)
#define GET_COREOFFSET(iface_desc_1)	((iface_desc_1) & ID_COREINFOPTR_MASK)
#define ADDR_SIZE(sz)			((1u << ((sz) + 8u)) - 1u)

#define CORE_REV(core_info)		((core_info) & COREINFO_REV_MASK) >> COREINFO_REV_SHIFT
#define CORE_MFG(core_info)		((core_info) & COREINFO_MFG_MASK) >> COREINFO_MFG_SHIFT
#define COREINFO_BPID(core_info)	(((core_info) & COREINFO_BPID_MASK) >> COREINFO_BPID_SHIFT)
#define IS_BACKPLANE(core_info)		(((core_info) & COREINFO_ISBP_MASK) >> COREINFO_ISBP_SHIFT)
#define ID_BPID(iface_desc_1)		(((iface_desc_1) & ID_BPID_MASK) >> ID_BPID_SHIFT)
#define IS_BACKPLANE_ID_SAME(core_info, iface_desc_1) \
					(COREINFO_BPID((core_info)) == ID_BPID((iface_desc_1)))

#define NCI_WORD_SIZE			(4u)
#define PCI_ACCESS_SIZE			(4u)

#define NCI_ADDR2NUM(addr)		((uintptr)(addr))
#define NCI_ADD_NUM(addr, size)		(NCI_ADDR2NUM(addr) + (size))
#ifdef DONGLEBUILD
#define NCI_ADD_ADDR(addr, size)	((uint32*)REG_MAP(NCI_ADD_NUM((addr), (size)), 0u))
#else /* !DONGLEBUILD */
#define NCI_ADD_ADDR(addr, size)	((uint32*)(NCI_ADD_NUM((addr), (size))))
#endif /* DONGLEBUILD */
#define NCI_INC_ADDR(addr, size)	((addr) = NCI_ADD_ADDR((addr), (size)))

#define NODE_TYPE_BOOKER		0x0u
#define NODE_TYPE_NIC400		0x1u

#define BP_BOOKER			0x0u
#define BP_NIC400			0x1u
#define BP_APB1				0x2u
#define BP_APB2				0x3u
#define BP_CCI400			0x4u

#define PCIE_WRITE_SIZE			4u

static const char BACKPLANE_ID_NAME[][11] = {
	"BOOKER",
	"NIC400",
	"APB1",
	"APB2",
	"CCI400",
	"\0"
};

#define APB_INF(ifd)	((ID_BPID((ifd).iface_desc_1) == BP_APB1) || \
	(ID_BPID((ifd).iface_desc_1) == BP_APB2))
#define BOOKER_INF(ifd)	(ID_BPID((ifd).iface_desc_1) == BP_BOOKER)
#define NIC_INF(ifd)	(ID_BPID((ifd).iface_desc_1) == BP_NIC400)

/* BOOKER NCI LOG LEVEL */
#define NCI_LOG_LEVEL_ERROR		0x1u
#define NCI_LOG_LEVEL_TRACE		0x2u
#define NCI_LOG_LEVEL_INFO		0x4u
#define NCI_LOG_LEVEL_PRINT		0x8u

#ifndef NCI_DEFAULT_LOG_LEVEL
#define NCI_DEFAULT_LOG_LEVEL	(NCI_LOG_LEVEL_ERROR)
#endif /* NCI_DEFAULT_LOG_LEVEL */

uint32 nci_log_level = NCI_DEFAULT_LOG_LEVEL;

#ifdef DONGLEBUILD
#define NCI_ERROR(args) do { if (nci_log_level & NCI_LOG_LEVEL_ERROR) { printf args; } } while (0u)
#define NCI_TRACE(args) do { if (nci_log_level & NCI_LOG_LEVEL_TRACE) { printf args; } } while (0u)
#define NCI_INFO(args)  do { if (nci_log_level & NCI_LOG_LEVEL_INFO) { printf args; } } while (0u)
#define NCI_PRINT(args) do { if (nci_log_level & NCI_LOG_LEVEL_PRINT) { printf args; } } while (0u)
#else /* !DONGLEBUILD */
#define NCI_KERN_PRINT(...) printk(KERN_ERR __VA_ARGS__)
#define NCI_ERROR(args) do { if (nci_log_level & NCI_LOG_LEVEL_ERROR) \
		{ NCI_KERN_PRINT args; } } while (0u)
#define NCI_TRACE(args) do { if (nci_log_level & NCI_LOG_LEVEL_TRACE) \
		{ NCI_KERN_PRINT args; } } while (0u)
#define NCI_INFO(args)  do { if (nci_log_level & NCI_LOG_LEVEL_INFO) \
		{ NCI_KERN_PRINT args; } } while (0u)
#define NCI_PRINT(args)  do { if (nci_log_level & NCI_LOG_LEVEL_PRINT) \
			{ NCI_KERN_PRINT args; } } while (0u)
#endif /* DONGLEBUILD */

#define NCI_EROM_WORD_SIZEOF		4u
#define NCI_REGS_PER_CORE		2u

#define NCI_EROM1_LEN(erom2base)	(erom2base - GET_EROM1_BASE(erom2base))
#define NCI_NONOOBR_CORES(erom2base)	NCI_EROM1_LEN(erom2base) \
						/(NCI_REGS_PER_CORE * NCI_EROM_WORD_SIZEOF)

/* AXI ID to CoreID + unit mappings */
typedef struct nci_axi_to_coreidx {
	uint coreid;
	uint coreunit;
} nci_axi_to_coreidx_t;

static const nci_axi_to_coreidx_t axi2coreidx_4397[] = {
	{CC_CORE_ID, 0},	 /* 00 Chipcommon */
	{PCIE2_CORE_ID, 0},	/* 01 PCIe */
	{D11_CORE_ID, 0},	/* 02 D11 Main */
	{ARMCR4_CORE_ID, 0},	/* 03 ARM */
	{BT_CORE_ID, 0},	/* 04 BT AHB */
	{D11_CORE_ID, 1},	/* 05 D11 Aux */
	{D11_CORE_ID, 0},	/* 06 D11 Main l1 */
	{D11_CORE_ID, 1},	/* 07 D11 Aux  l1 */
	{D11_CORE_ID, 0},	/* 08 D11 Main l2 */
	{D11_CORE_ID, 1},	/* 09 D11 Aux  l2 */
	{NODEV_CORE_ID, 0},	/* 10 M2M DMA */
	{NODEV_CORE_ID, 0},	/* 11 unused */
	{NODEV_CORE_ID, 0},	/* 12 unused */
	{NODEV_CORE_ID, 0},	/* 13 unused */
	{NODEV_CORE_ID, 0},	/* 14 unused */
	{NODEV_CORE_ID, 0}	/* 15 unused */
};

typedef struct slave_port {
	uint32		adesc;		/**< Address Descriptor 0 */
	uint32		addrl;		/**< Lower Base */
	uint32		addrh;		/**< Upper Base */
	uint32		extaddrl;	/**< Lower Bound */
	uint32		extaddrh;	/**< Ubber Bound */
} slave_port_t;

typedef struct interface_desc {
	slave_port_t	*sp;		/**< Slave Port Addr 0-3 */

	uint32		iface_desc_0;	/**< Interface-0 Descriptor Word0 */
	/* If Node Type 0-Booker xMNI/xSNI address. If Node Type 1-DMP wrapper Address */
	uint32		node_ptr;	/**< Core's Node pointer */

	uint32		iface_desc_1;	/**< Interface Descriptor Word1 */
	uint8		num_addr_reg;	/**< Number of Slave Port Addr (Valid only if master=0) */
	uint8		coretype;	/**< Core Belongs to 0:OOBR 1:Without OOBR */
	uint8		master;		/**< 1:Master 0:Slave */

	uint8		node_type;	/**< 0:Booker , 1:IDM Wrapper, 2-0xf: Reserved */
} interface_desc_t;

typedef struct nci_cores {
	void		*regs;
	/* 2:0-Node type (0-booker,1-IDM Wrapper) 31:3-Interconnect registyer space */
	interface_desc_t *desc;		/**< Interface & Address Descriptors */
	/*
	 * 11:0-CoreID, 19:12-RevID 23:20-MFG 26:24-Backplane ID if
	 * bit 27 is 1 (Core is Backplane or Bridge )
	 */
	uint32		coreinfo;	/**< CoreInfo of each core */
	/*
	 * 11:0 - Offosewt of 1st Interface desc in EROM 15:12 - No.
	 * of interfaces attachedto this core
	 */
	uint32		iface_cfg;	/**< Interface config Reg */
	uint32		dmp_regs_off;	/**< DMP control & DMP status @ 0x48 from coreinfo */
	uint32		coreid;		/**< id of each core */
	uint8		coreunit;	/**< Unit differentiate same coreids */
	uint8		iface_cnt;	/**< no of Interface connected to each core */
	uint8		PAD[2u];
} nci_cores_t;

typedef struct nci_info {
	void		*osh;		/**< osl os handle */
	nci_cores_t	*cores;		/**< Cores Parsed */
	void		*pci_bar_addr;	/**< PCI BAR0 Window */
	uint32		cc_erom2base;	/**< Base of EROM2 from ChipCommon */
	uint32		*erom1base;	/**< Base of EROM1 */
	uint32		*erom2base;	/**< Base of EROM2 */
	uint32		*oobr_base;	/**< Base of OOBR */
	uint16		bustype;	/**< SI_BUS, PCI_BUS */
	uint8		max_cores;	/**< # Max cores indicated by Register */
	uint8		num_cores;	/**< # discovered cores */
	uint8		refcnt;		/**< Allocation reference count  */
	uint8		scan_done;	/**< Set to TRUE when erom scan is done. */
	uint8		PAD[2];
} nci_info_t;

#define NI_IDM_RESET_ENTRY 0x1
#define NI_IDM_RESET_EXIT  0x0

/* AXI Slave Network Interface registers */
typedef volatile struct asni_regs {
	uint32 node_type;	/* 0x000 */
	uint32 node_info;	/* 0x004 */
	uint32 secr_acc;	/* 0x008 */
	uint32 pmusela;		/* 0x00c */
	uint32 pmuselb;		/* 0x010 */
	uint32 PAD[11];
	uint32 node_feat;	/* 0x040 */
	uint32 bursplt;		/* 0x044 */
	uint32 addr_remap;	/* 0x048 */
	uint32 PAD[13];
	uint32 sildbg;		/* 0x080 */
	uint32 qosctl;		/* 0x084 */
	uint32 wdatthrs;	/* 0x088 */
	uint32 arqosovr;	/* 0x08c */
	uint32 awqosovr;	/* 0x090 */
	uint32 atqosot;		/* 0x094 */
	uint32 arqosot;		/* 0x098 */
	uint32 awqosot;		/* 0x09c */
	uint32 axqosot;		/* 0x0a0 */
	uint32 qosrdpk;		/* 0x0a4 */
	uint32 qosrdbur;	/* 0x0a8 */
	uint32 qosrdavg;	/* 0x0ac */
	uint32 qoswrpk;		/* 0x0b0 */
	uint32 qoswrbur;	/* 0x0b4 */
	uint32 qoswravg;	/* 0x0b8 */
	uint32 qoscompk;	/* 0x0bc */
	uint32 qoscombur;	/* 0x0c0 */
	uint32 qoscomavg;	/* 0x0c4 */
	uint32 qosrbbqv;	/* 0x0c8 */
	uint32 qoswrbqv;	/* 0x0cc */
	uint32 qoscombqv;	/* 0x0d0 */
	uint32 PAD[11];
	uint32 idm_device_id;	/* 0x100 */
	uint32 PAD[15];
	uint32 idm_reset_ctrl;	/* 0x140 */
} asni_regs_t;

/* AXI Master Network Interface registers */
typedef volatile struct amni_regs {
	uint32 node_type;		/* 0x000 */
	uint32 node_info;		/* 0x004 */
	uint32 secr_acc;		/* 0x008 */
	uint32 pmusela;			/* 0x00c */
	uint32 pmuselb;			/* 0x010 */
	uint32 PAD[11];
	uint32 node_feat;		/* 0x040 */
	uint32 PAD[15];
	uint32 sildbg;			/* 0x080 */
	uint32 qosacc;			/* 0x084 */
	uint32 PAD[26];
	uint32 interrupt_status;	/* 0x0f0 */
	uint32 interrupt_mask;		/* 0x0f4 */
	uint32 interrupt_status_ns;	/* 0x0f8 */
	uint32 interrupt_mask_ns;	/* 0x0FC */
	uint32 idm_device_id;		/* 0x100 */
	uint32 PAD[15];
	uint32 idm_reset_ctrl;		/* 0x140 */
} amni_regs_t;

#define NCI_SPINWAIT_TIMEOUT		(300u)

/* DMP/io control and DMP/io status */
typedef struct dmp_regs {
	uint32 dmpctrl;
	uint32 dmpstatus;
} dmp_regs_t;

#ifdef _RTE_
static nci_info_t *knci_info = NULL;
#endif /* _RTE_ */

static void nci_save_iface1_reg(interface_desc_t *desc, uint32 iface_desc_1);
static uint32* nci_save_slaveport_addr(nci_info_t *nci,
	interface_desc_t *desc, uint32 *erom2ptr);
static int nci_get_coreunit(nci_cores_t *cores, uint32 numcores, uint cid,
	uint32 iface_desc_1);
static nci_cores_t* nci_initial_parse(nci_info_t *nci, uint32 *erom2ptr, uint32 *core_idx);
static void _nci_setcoreidx_pcie_bus(si_t *sih, volatile void **regs, uint32 curmap,
	uint32 curwrap);
static volatile void *_nci_setcoreidx(si_t *sih, uint coreidx);
static uint32 _nci_get_curwrap(nci_info_t *nci, uint coreidx, uint wrapper_idx);
static uint32 nci_get_curwrap(nci_info_t *nci, uint coreidx);
static uint32 _nci_get_curmap(nci_info_t *nci, uint coreidx, uint slave_port_idx, uint base_idx);
static uint32 nci_get_curmap(nci_info_t *nci, uint coreidx);
static void _nci_core_reset(const si_t *sih, uint32 bits, uint32 resetbits);
#if defined (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC)
static void nci_reset_APB(const si_info_t *sii, aidmp_t *ai, int *ret,
	uint32 errlog_status, uint32 errlog_id);
static void nci_reset_axi_to(const si_info_t *sii, aidmp_t *ai);
#endif /* (AXI_TIMEOUTS) ||  (AXI_TIMEOUTS_NIC) */
static uint32 nci_find_numcores(si_t *sih);
#ifdef BOOKER_NIC400_INF
static int32 nci_find_first_wrapper_idx(nci_info_t *nci, uint32 coreidx);
#endif /* BOOKER_NIC400_INF */

/*
 * Description : This function will search for a CORE with matching 'core_id' and mismatching
 * 'wordoffset', if found then increments 'coreunit' by 1.
 */
/* TODO: Need to understand this. */
static int
BCMATTACHFN(nci_get_coreunit)(nci_cores_t *cores, uint32 numcores,
		uint core_id, uint32 iface_desc_1)
{
	uint32 core_idx;
	uint32 coreunit = NCI_DEFAULT_CORE_UNIT;

	for (core_idx = 0u; core_idx < numcores; core_idx++) {
		if ((cores[core_idx].coreid == core_id) &&
			(GET_COREOFFSET(cores[core_idx].desc->iface_desc_1) !=
				GET_COREOFFSET(iface_desc_1))) {
			coreunit = cores[core_idx].coreunit + 1;
		}
	}

	return coreunit;
}

/*
 * OOBR Region
	+-------------------------------+
	+				+
	+	OOBR with EROM Data	+
	+				+
	+-------------------------------+
	+				+
	+	EROM1			+
	+				+
	+-------------------------------+  --> ChipCommon.EROMBASE
	+				+
	+	EROM2			+
	+				+
	+-------------------------------+
*/

/**
 * Function : nci_init
 * Description : Malloc's memory related to 'nci_info_t' and its internal elements.
 *
 * @paramter[in]
 * @regs : This is a ChipCommon Regster
 * @bustype : Bus Connect Type
 *
 * Return : On Succes 'nci_info_t' data structure is returned as void,
 *	where all EROM parsed Cores are saved,
 *	using this all EROM Cores are Freed.
 *	On Failure 'NULL' is returned by printing ERROR messages
 */
void*
BCMATTACHFN(nci_init)(si_t *sih, chipcregs_t *cc, uint bustype)
{
	si_info_t *sii = SI_INFO(sih);
	nci_cores_t *cores;
	nci_info_t *nci = NULL;
	uint8 err_at = 0u;

#ifdef _RTE_
	if (knci_info) {
		knci_info->refcnt++;
		nci = knci_info;

		goto end;
	}
#endif /* _RTE_ */

	/* It is used only when NCI_ERROR is used */
	BCM_REFERENCE(err_at);

	if ((nci = MALLOCZ(sii->osh, sizeof(*nci))) == NULL) {
		err_at = 1u;
		goto end;
	}
	sii->nci_info = nci;

	nci->osh = sii->osh;
	nci->refcnt++;

	nci->cc_erom2base = R_REG(nci->osh, &cc->eromptr);
	nci->bustype = bustype;
	switch (nci->bustype) {
		case SI_BUS:
			nci->erom2base = (uint32*)REG_MAP(nci->cc_erom2base, 0u);
			nci->oobr_base = (uint32*)REG_MAP(GET_OOBR_BASE(nci->cc_erom2base), 0u);
			nci->erom1base = (uint32*)REG_MAP(GET_EROM1_BASE(nci->cc_erom2base), 0u);

			break;

		case PCI_BUS:
			/* Set wrappers address */
			sii->curwrap = (void *)((uintptr)cc + SI_CORE_SIZE);
			/* Set access window to Erom Base(For NCI, EROM starts with OOBR) */
			OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE,
				GET_EROM1_BASE(nci->cc_erom2base));
			nci->erom1base = (uint32*)((uintptr)cc);
			nci->erom2base = (uint32*)((uintptr)cc + NCI_EROM1_LEN(nci->cc_erom2base));

			break;

		default:
			err_at = 2u;
			ASSERT(0u);
			goto end;
	}

	nci->max_cores = nci_find_numcores(sih);
	if (!nci->max_cores) {
		err_at = 3u;
		goto end;
	}

	if ((cores = MALLOCZ(nci->osh, sizeof(*cores) * nci->max_cores)) == NULL) {
		err_at = 4u;
		goto end;
	}
	nci->cores = cores;

#ifdef _RTE_
	knci_info = nci;
#endif /* _RTE_ */

end:
	if (err_at) {
		NCI_ERROR(("nci_init: Failed err_at=%#x\n", err_at));
		nci_uninit(nci);
		nci = NULL;
	}

	return nci;
}

/**
 * Function : nci_uninit
 * Description : Free's memory related to 'nci_info_t' and its internal malloc'd elements.
 *
 * @paramter[in]
 * @nci : This is 'nci_info_t' data structure, where all EROM parsed Cores are saved, using this
 *	all EROM Cores are Freed.
 *
 * Return : void
 */
void
BCMATTACHFN(nci_uninit)(void *ctx)
{
	nci_info_t *nci = (nci_info_t *)ctx;
	uint8 core_idx, desc_idx;
	interface_desc_t *desc;
	nci_cores_t *cores;
	slave_port_t *sp;

	if (nci == NULL) {
		return;
	}

	nci->refcnt--;

#ifdef _RTE_
	if (nci->refcnt != 0) {
		return;
	}
#endif /* _RTE_ */

	cores = nci->cores;
	if (cores == NULL) {
		goto end;
	}

	for (core_idx = 0u; core_idx < nci->num_cores; core_idx++) {
		desc = cores[core_idx].desc;
		if (desc == NULL) {
			break;
		}

		for (desc_idx = 0u; desc_idx < cores[core_idx].iface_cnt; desc_idx++) {
			sp = desc[desc_idx].sp;
			if (sp) {
				MFREE(nci->osh, sp, (sizeof(*sp) * desc[desc_idx].num_addr_reg));
			}
		}
		MFREE(nci->osh, desc, (sizeof(*desc) * cores[core_idx].iface_cnt));
	}
	MFREE(nci->osh, cores, sizeof(*cores) * nci->max_cores);

end:

#ifdef _RTE_
	knci_info = NULL;
#endif /* _RTE_ */

	MFREE(nci->osh, nci, sizeof(*nci));
}

/**
 * Function : nci_save_iface1_reg
 * Description : Interface1 Descriptor is obtained from the Reg and saved in
 * Internal data structures 'nci->cores'.
 *
 * @paramter[in]
 * @desc : Descriptor of Core which needs to be updated with obatained Interface1 Descritpor.
 * @iface_desc_1 : Obatained Interface1 Descritpor.
 *
 * Return : void
 */
static void
BCMATTACHFN(nci_save_iface1_reg)(interface_desc_t *desc, uint32 iface_desc_1)
{
	BCM_REFERENCE(BACKPLANE_ID_NAME);

	desc->coretype = GET_CORETYPE(iface_desc_1);
	desc->master = IS_MASTER(iface_desc_1);

	desc->iface_desc_1 = iface_desc_1;
	desc->num_addr_reg = GET_NUM_ADDR_REG(iface_desc_1);
	if (desc->master) {
		if (desc->num_addr_reg) {
			NCI_ERROR(("nci_save_iface1_reg: Master NODEPTR Addresses is not zero "
				"i.e. %d\n", GET_NUM_ADDR_REG(iface_desc_1)));
			ASSERT(0u);
		}
	} else {
		/* SLAVE 'NumAddressRegion' one less than actual slave ports, so increment by 1 */
		desc->num_addr_reg++;
	}

	NCI_INFO(("\tnci_save_iface1_reg: %s InterfaceDesc:%#x WordOffset=%#x "
		"NoAddrReg=%#x %s_Offset=%#x BackplaneID=%s\n",
		desc->master?"Master":"Slave", desc->iface_desc_1,
		GET_WORDOFFSET(desc->iface_desc_1),
		desc->num_addr_reg, desc->coretype?"EROM1":"OOBR",
		GET_COREOFFSET(desc->iface_desc_1),
		BACKPLANE_ID_NAME[ID_BPID(desc->iface_desc_1)]));
}

/**
 * Function : nci_save_slaveport_addr
 * Description : All Slave Port Addr of Interface Descriptor are saved.
 *
 * @paramter[in]
 * @nci : This is 'nci_info_t' data structure, where all EROM parsed Cores are saved
 * @desc : Current Interface Descriptor.
 * @erom2ptr : Pointer to Address Descriptor0.
 *
 * Return : On Success, this function returns Erom2 Ptr to Next Interface Descriptor,
 *	On Failure, NULL is returned.
 */
static uint32*
BCMATTACHFN(nci_save_slaveport_addr)(nci_info_t *nci,
		interface_desc_t *desc, uint32 *erom2ptr)
{
	slave_port_t *sp;
	uint32 adesc;
	uint32 sz;
	uint32 addr_idx;

	/* Allocate 'NumAddressRegion' of Slave Port */
	if ((desc->sp = (slave_port_t *)MALLOCZ(
		nci->osh, (sizeof(*sp) * desc->num_addr_reg))) == NULL) {
		NCI_ERROR(("\tnci_save_slaveport_addr: Memory Allocation failed for Slave Port\n"));
		return NULL;
	}

	sp = desc->sp;
	/* Slave Port Addrs Desc */
	for (addr_idx = 0u; addr_idx < desc->num_addr_reg; addr_idx++) {
		adesc = R_REG(nci->osh, erom2ptr);
		NCI_INC_ADDR(erom2ptr, NCI_WORD_SIZE);
		sp[addr_idx].adesc = adesc;

		sp[addr_idx].addrl = adesc & SLAVEPORT_BASE_ADDR_MASK;
		if (adesc & SLAVEPORT_64BIT_ADDR_MASK) {
			sp[addr_idx].addrh = R_REG(nci->osh, erom2ptr);
			NCI_INC_ADDR(erom2ptr, NCI_WORD_SIZE);
			sp[addr_idx].extaddrl = R_REG(nci->osh, erom2ptr);
			NCI_INC_ADDR(erom2ptr, NCI_WORD_SIZE);
			sp[addr_idx].extaddrh = R_REG(nci->osh, erom2ptr);
			NCI_INC_ADDR(erom2ptr, NCI_WORD_SIZE);
			NCI_INFO(("\tnci_save_slaveport_addr: SlavePortAddr[%#x]:0x%08x al=0x%08x "
				"ah=0x%08x extal=0x%08x extah=0x%08x\n", addr_idx, adesc,
				sp[addr_idx].addrl, sp[addr_idx].addrh, sp[addr_idx].extaddrl,
				sp[addr_idx].extaddrh));
			}
		else if (adesc & SLAVEPORT_BOUND_ADDR_MASK) {
			sp[addr_idx].addrh = R_REG(nci->osh, erom2ptr);
			NCI_INC_ADDR(erom2ptr, NCI_WORD_SIZE);
			NCI_INFO(("\tnci_save_slaveport_addr: SlavePortAddr[%#x]:0x%08x al=0x%08x "
				"ah=0x%08x\n", addr_idx, adesc, sp[addr_idx].addrl,
				sp[addr_idx].addrh));
		} else {
			sz = adesc & SLAVEPORT_ADDR_SIZE_MASK;
			sp[addr_idx].addrh = sp[addr_idx].addrl + ADDR_SIZE(sz);
			NCI_INFO(("\tnci_save_slaveport_addr: SlavePortAddr[%#x]:0x%08x al=0x%08x "
				"ah=0x%08x sz=0x%08x\n", addr_idx, adesc, sp[addr_idx].addrl,
				sp[addr_idx].addrh, sz));
		}
	}

	return erom2ptr;
}

/**
 * Function : nci_initial_parse
 * Description : This function does
 *	1. Obtains OOBR/EROM1 pointer based on CoreType
 *	2. Analysis right CoreUnit for this 'core'
 *	3. Saves CoreInfo & Interface Config in Coresponding 'core'
 *
 * @paramter[in]
 * @nci : This is 'nci_info_t' data structure, where all EROM parsed Cores are saved.
 * @erom2ptr : Pointer to Interface Descriptor0.
 * @core_idx : New core index needs to be populated in this pointer.
 *
 * Return : On Success, this function returns 'core' where CoreInfo & Interface Config are saved.
 */
static nci_cores_t*
BCMATTACHFN(nci_initial_parse)(nci_info_t *nci, uint32 *erom2ptr, uint32 *core_idx)
{
	uint32 iface_desc_1;
	nci_cores_t *core;
	uint32 dmp_regs_off = 0u;
	uint32 iface_cfg = 0u;
	uint32 core_info;
	uint32 *ptr;
	uint coreid;

	iface_desc_1 = R_REG(nci->osh, erom2ptr);

	/* Get EROM1/OOBR Pointer based on CoreType */
	if (!GET_CORETYPE(iface_desc_1)) {
		if (nci->bustype == PCI_BUS) {
			OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE,
				GET_OOBR_BASE(nci->cc_erom2base));
			nci->oobr_base = (uint32*)((uintptr)nci->erom1base);
		}

		ptr = NCI_ADD_ADDR(nci->oobr_base, GET_COREOFFSET(iface_desc_1));
	} else {
		ptr = NCI_ADD_ADDR(nci->erom1base, GET_COREOFFSET(iface_desc_1));
	}
	dmp_regs_off = GET_COREOFFSET(iface_desc_1) + DMP_DMPCTRL_REG_OFFSET;

	core_info = R_REG(nci->osh, ptr);
	NCI_INC_ADDR(ptr, NCI_WORD_SIZE);
	iface_cfg = R_REG(nci->osh, ptr);

	*core_idx = nci->num_cores;
	core = &nci->cores[*core_idx];

	if (CORE_ID(core_info) < 0xFFu) {
		coreid = CORE_ID(core_info) | 0x800u;
	} else {
		coreid = CORE_ID(core_info);
	}

	/* Get coreunit from previous cores i.e. num_cores */
	core->coreunit = nci_get_coreunit(nci->cores, nci->num_cores,
		coreid, iface_desc_1);

	core->coreid = coreid;

	/* Increment the num_cores once proper coreunit is known */
	nci->num_cores++;

	NCI_INFO(("\n\nnci_initial_parse: core_idx:%d %s=%p \n",
		*core_idx, GET_CORETYPE(iface_desc_1)?"EROM1":"OOBR", ptr));

	/* Core Info Register */
	core->coreinfo = core_info;

	/* Save DMP register base address. */
	core->dmp_regs_off = dmp_regs_off;

	NCI_INFO(("\tnci_initial_parse: COREINFO:%#x CId:%#x CUnit=%#x CRev=%#x CMfg=%#x\n",
		core->coreinfo, core->coreid, core->coreunit, CORE_REV(core->coreinfo),
		CORE_MFG(core->coreinfo)));

	/* Interface Config Register */
	core->iface_cfg = iface_cfg;
	core->iface_cnt = GET_INFACECNT(iface_cfg);

	NCI_INFO(("\tnci_initial_parse: INTERFACE_CFG:%#x IfaceCnt=%#x IfaceOffset=%#x \n",
		iface_cfg, core->iface_cnt, iface_cfg & IC_IFACEOFFSET_MASK));

	/* For PCI_BUS case set back BAR0 Window to EROM1 Base */
	if (nci->bustype == PCI_BUS) {
		OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE,
			GET_EROM1_BASE(nci->cc_erom2base));
	}

	return core;
}

static uint32
BCMATTACHFN(nci_find_numcores)(si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	volatile hndoobr_reg_t *oobr_reg = NULL;
	uint32 orig_bar0_win1 = 0u;
	uint32 num_oobr_cores = 0u;
	uint32 num_nonoobr_cores = 0u;

	/* No of Non-OOBR Cores */
	num_nonoobr_cores = NCI_NONOOBR_CORES(nci->cc_erom2base);
	if (num_nonoobr_cores <= 0u) {
		NCI_ERROR(("nci_find_numcores: Invalid Number of non-OOBR cores %d\n",
			num_nonoobr_cores));
		goto fail;
	}

	/* No of OOBR Cores */
	switch (BUSTYPE(sih->bustype)) {
	case SI_BUS:
		oobr_reg = (volatile hndoobr_reg_t*)REG_MAP(GET_OOBR_BASE(nci->cc_erom2base),
				SI_CORE_SIZE);
		break;

	case PCI_BUS:
		/* Save Original Bar0 Win1 */
		orig_bar0_win1 = OSL_PCI_READ_CONFIG(nci->osh, PCI_BAR0_WIN,
			PCI_ACCESS_SIZE);

		OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE,
			GET_OOBR_BASE(nci->cc_erom2base));
		oobr_reg = (volatile hndoobr_reg_t*)sii->curmap;
		break;

	default:
		NCI_ERROR(("nci_find_numcores: Invalid bustype %d\n", BUSTYPE(sih->bustype)));
		ASSERT(0);
		goto fail;
	}

	num_oobr_cores = R_REG(nci->osh, &oobr_reg->capability) & OOBR_CAP_CORECNT_MASK;
	if (num_oobr_cores <= 0u) {
		NCI_ERROR(("nci_find_numcores: Invalid Number of OOBR cores %d\n", num_oobr_cores));
		goto fail;
	}

	/* Point back to original base */
	if (BUSTYPE(sih->bustype) == PCI_BUS) {
		OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE, orig_bar0_win1);
	}

	NCI_PRINT(("nci_find_numcores: Total Cores found %d\n",
		(num_oobr_cores + num_nonoobr_cores)));
	/* Total No of Cores */
	return (num_oobr_cores + num_nonoobr_cores);

fail:
	return 0u;
}

/**
 * Function : nci_scan
 * Description : Function parses EROM in BOOKER NCI Architecture and saves all inforamtion about
 *	Cores in 'nci_info_t' data structure.
 *
 * @paramter[in]
 * @nci : This is 'nci_info_t' data structure, where all EROM parsed Cores are saved.
 *
 * Return : On Success No of parsed Cores in EROM is returned,
 *	On Failure '0' is returned by printing ERROR messages
 *	in Console(If NCI_LOG_LEVEL is enabled).
 */
uint32
BCMATTACHFN(nci_scan)(si_t *sih)
{
	si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = (nci_info_t *)sii->nci_info;
	axi_wrapper_t * axi_wrapper = sii->axi_wrapper;
	uint32 *cur_iface_desc_1_ptr;
	nci_cores_t *core;
	interface_desc_t *desc;
	uint32 wordoffset = 0u;
	uint32 iface_desc_0;
	uint32 iface_desc_1;
	uint32 *erom2ptr;
	uint8 iface_idx;
	uint32 core_idx;
	int err = 0;

	/* If scan was finished already */
	if (nci->scan_done) {
		goto end;
	}

	erom2ptr = nci->erom2base;
	sii->axi_num_wrappers = 0;

	while (TRUE) {
		iface_desc_0 = R_REG(nci->osh, erom2ptr);
		if (iface_desc_0 == ID_ENDMARKER) {
			NCI_INFO(("\nnci_scan: Reached end of EROM2 with total cores=%d \n",
				nci->num_cores));
			break;
		}

		/* Save current Iface1 Addr for comparision */
		cur_iface_desc_1_ptr = GET_NEXT_EROM_ADDR(erom2ptr);

		/* Get CoreInfo, InterfaceCfg, CoreIdx */
		core = nci_initial_parse(nci, cur_iface_desc_1_ptr, &core_idx);

		core->desc = (interface_desc_t *)MALLOCZ(
			nci->osh, (sizeof(*(core->desc)) * core->iface_cnt));
		if (core->desc == NULL) {
			NCI_ERROR(("nci_scan: Mem Alloc failed for Iface and Addr "
				"Descriptor\n"));
			err = NCI_NOMEM;
			break;
		}

		for (iface_idx = 0u; iface_idx < core->iface_cnt; iface_idx++) {
			desc = &core->desc[iface_idx];

			iface_desc_0 = R_REG(nci->osh, erom2ptr);
			NCI_INC_ADDR(erom2ptr, NCI_WORD_SIZE);
			iface_desc_1 = R_REG(nci->osh, erom2ptr);
			NCI_INC_ADDR(erom2ptr, NCI_WORD_SIZE);

			/* Interface Descriptor Register */
			nci_save_iface1_reg(desc, iface_desc_1);
			if (desc->master && desc->num_addr_reg) {
				err = NCI_MASTER_INVALID_ADDR;
				goto end;
			}

			wordoffset = GET_WORDOFFSET(iface_desc_1);

			/* NodePointer Register */
			desc->iface_desc_0 = iface_desc_0;
			desc->node_ptr = GET_NODEPTR(iface_desc_0);
			desc->node_type = GET_NODETYPE(iface_desc_0);

			if (axi_wrapper && (sii->axi_num_wrappers < SI_MAX_AXI_WRAPPERS)) {
				axi_wrapper[sii->axi_num_wrappers].mfg = CORE_MFG(core->coreinfo);
				axi_wrapper[sii->axi_num_wrappers].cid = CORE_ID(core->coreinfo);
				axi_wrapper[sii->axi_num_wrappers].rev = CORE_REV(core->coreinfo);
				axi_wrapper[sii->axi_num_wrappers].wrapper_type = desc->master;
				axi_wrapper[sii->axi_num_wrappers].wrapper_addr = desc->node_ptr;
				sii->axi_num_wrappers++;
			}

			NCI_INFO(("nci_scan: %s NodePointer:%#x Type=%s NODEPTR=%#x \n",
				desc->master?"Master":"Slave", desc->iface_desc_0,
				desc->node_type?"NIC-400":"BOOKER", desc->node_ptr));

			/* Slave Port Addresses */
			if (!desc->master) {
				erom2ptr = nci_save_slaveport_addr(nci, desc, erom2ptr);
				if (erom2ptr == NULL) {
					NCI_ERROR(("nci_scan: Invalid EROM2PTR\n"));
					err = NCI_INVALID_EROM2PTR;
					goto end;
				}
			}

			/* Current loop ends with next iface_desc_0 */
		}

		if (wordoffset == 0u) {
			NCI_INFO(("\nnci_scan: EROM PARSING found END 'wordoffset=%#x' "
				"with total cores=%d \n", wordoffset, nci->num_cores));
			break;
		}
	}
	nci->scan_done = TRUE;

end:
	if (err) {
		NCI_ERROR(("nci_scan: Failed with Code %d\n", err));
		nci->num_cores = 0;
		ASSERT(0u);
	}

	return nci->num_cores;
}

/**
 * Function : nci_dump_erom
 * Description : Function dumps EROM from inforamtion cores in 'nci_info_t' data structure.
 *
 * @paramter[in]
 * @nci : This is 'nci_info_t' data structure, where all EROM parsed Cores are saved.
 *
 * Return : void
 */
void
BCMATTACHFN(nci_dump_erom)(void *ctx)
{
	nci_info_t *nci = (nci_info_t *)ctx;
	nci_cores_t *core;
	interface_desc_t *desc;
	slave_port_t *sp;
	uint32 core_idx, addr_idx, iface_idx;
	uint32 core_info;

	BCM_REFERENCE(core_info);

	NCI_INFO(("\nnci_dump_erom: -- EROM Dump --\n"));
	for (core_idx = 0u; core_idx < nci->num_cores; core_idx++) {
		core = &nci->cores[core_idx];

		/* Core Info Register */
		core_info = core->coreinfo;
		NCI_INFO(("\nnci_dump_erom: core_idx=%d COREINFO:%#x CId:%#x CUnit:%#x CRev=%#x "
			"CMfg=%#x\n", core_idx, core_info, CORE_ID(core_info), core->coreunit,
			CORE_REV(core_info), CORE_MFG(core_info)));

		/* Interface Config Register */
		NCI_INFO(("nci_dump_erom: IfaceCfg=%#x IfaceCnt=%#x \n",
			core->iface_cfg, core->iface_cnt));

		for (iface_idx = 0u; iface_idx < core->iface_cnt; iface_idx++) {
			desc = &core->desc[iface_idx];
			/* NodePointer Register */
			NCI_INFO(("nci_dump_erom: %s iface_desc_0 Master=%#x MASTER_WRAP=%#x "
				"Type=%s \n", desc->master?"Master":"Slave", desc->iface_desc_0,
				desc->node_ptr,
				(desc->node_type)?"NIC-400":"BOOKER"));

			/* Interface Descriptor Register */
			NCI_INFO(("nci_dump_erom: %s InterfaceDesc:%#x WOffset=%#x NoAddrReg=%#x "
				"%s_Offset=%#x\n", desc->master?"Master":"Slave",
				desc->iface_desc_1, GET_WORDOFFSET(desc->iface_desc_1),
				desc->num_addr_reg,
				desc->coretype?"EROM1":"OOBR", GET_COREOFFSET(desc->iface_desc_1)));

			/* Slave Port Addresses */
			sp = desc->sp;
			if (!sp) {
				continue;
			}
			for (addr_idx = 0u; addr_idx < desc->num_addr_reg; addr_idx++) {
				if (sp[addr_idx].extaddrl) {
					NCI_INFO(("nci_dump_erom: SlavePortAddr[%#x]: AddrDesc=%#x"
						" al=%#x ah=%#x  extal=%#x extah=%#x\n", addr_idx,
						sp[addr_idx].adesc, sp[addr_idx].addrl,
						sp[addr_idx].addrh, sp[addr_idx].extaddrl,
						sp[addr_idx].extaddrh));
				} else {
					NCI_INFO(("nci_dump_erom: SlavePortAddr[%#x]: AddrDesc=%#x"
						" al=%#x ah=%#x\n", addr_idx, sp[addr_idx].adesc,
						sp[addr_idx].addrl, sp[addr_idx].addrh));
				}
			}
		}
	}

	return;
}

/*
 * Switch to 'coreidx', issue a single arbitrary 32bit register mask & set operation,
 * switch back to the original core, and return the new value.
 */
uint
BCMPOSTTRAPFN(nci_corereg)(si_t *sih, uint coreidx, uint regoff, uint mask, uint val)
{
	uint origidx = 0;
	volatile uint32 *r = NULL;
	uint w;
	bcm_int_bitmask_t intr_val;
	bool fast = FALSE;
	si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	nci_cores_t *cores_info = &nci->cores[coreidx];

	NCI_TRACE(("nci_corereg coreidx %u regoff %u mask %u val %u\n",
		coreidx, regoff, mask, val));
	ASSERT(GOODIDX(coreidx, nci->num_cores));
	ASSERT(regoff < SI_CORE_SIZE);
	ASSERT((val & ~mask) == 0);

	if (coreidx >= SI_MAXCORES) {
		return 0;
	}

	if (BUSTYPE(sih->bustype) == SI_BUS) {
		/* If internal bus, we can always get at everything */
		uint32 curmap = nci_get_curmap(nci, coreidx);
		BCM_REFERENCE(curmap);

		fast = TRUE;
		/* map if does not exist */
		if (!cores_info->regs) {
			cores_info->regs = REG_MAP(curmap, SI_CORE_SIZE);
			ASSERT(GOODREGS(cores_info->regs));
		}
		r = (volatile uint32 *)((volatile uchar *)cores_info->regs + regoff);
	} else if (BUSTYPE(sih->bustype) == PCI_BUS) {
		/* If pci/pcie, we can get at pci/pcie regs and on newer cores to chipc */

		if ((cores_info->coreid == CC_CORE_ID) && SI_FAST(sii)) {
			/* Chipc registers are mapped at 12KB */

			fast = TRUE;
			r = (volatile uint32 *)((volatile char *)sii->curmap +
				PCI_16KB0_CCREGS_OFFSET + regoff);
		} else if (sii->pub.buscoreidx == coreidx) {
			/* pci registers are at either in the last 2KB of an 8KB window
			 * or, in pcie and pci rev 13 at 8KB
			 */
			fast = TRUE;
			if (SI_FAST(sii)) {
				r = (volatile uint32 *)((volatile char *)sii->curmap +
					PCI_16KB0_PCIREGS_OFFSET + regoff);
			} else {
				r = (volatile uint32 *)((volatile char *)sii->curmap +
					((regoff >= SBCONFIGOFF) ?
					PCI_BAR0_PCISBR_OFFSET : PCI_BAR0_PCIREGS_OFFSET) + regoff);
			}
		}
	}

	if (!fast) {
		INTR_OFF(sii, &intr_val);

		/* save current core index */
		origidx = si_coreidx(&sii->pub);

		/* switch core */
		r = (volatile uint32*)((volatile uchar*)nci_setcoreidx(&sii->pub, coreidx) +
			regoff);
	}
	ASSERT(r != NULL);

	/* mask and set */
	if (mask || val) {
		w = (R_REG(sii->osh, r) & ~mask) | val;
		W_REG(sii->osh, r, w);
	}

	/* readback */
	w = R_REG(sii->osh, r);

	if (!fast) {
		/* restore core index */
		if (origidx != coreidx) {
			nci_setcoreidx(&sii->pub, origidx);
		}
		INTR_RESTORE(sii, &intr_val);
	}

	return (w);
}

uint
nci_corereg_writeonly(si_t *sih, uint coreidx, uint regoff, uint mask, uint val)
{
	uint origidx = 0;
	volatile uint32 *r = NULL;
	uint w = 0;
	bcm_int_bitmask_t intr_val;
	bool fast = FALSE;
	si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	nci_cores_t *cores_info = &nci->cores[coreidx];

	NCI_TRACE(("nci_corereg_writeonly() coreidx %u regoff %u mask %u val %u\n",
		coreidx, regoff, mask, val));

	ASSERT(GOODIDX(coreidx, nci->num_cores));
	ASSERT(regoff < SI_CORE_SIZE);
	ASSERT((val & ~mask) == 0);

	if (coreidx >= SI_MAXCORES) {
		return 0;
	}

	if (BUSTYPE(sih->bustype) == SI_BUS) {
		/* If internal bus, we can always get at everything */
		uint32 curmap = nci_get_curmap(nci, coreidx);
		BCM_REFERENCE(curmap);
		fast = TRUE;
		/* map if does not exist */
		if (!cores_info->regs) {
			cores_info->regs = REG_MAP(curmap, SI_CORE_SIZE);
			ASSERT(GOODREGS(cores_info->regs));
		}
		r = (volatile uint32 *)((volatile uchar *)cores_info->regs + regoff);
	} else if (BUSTYPE(sih->bustype) == PCI_BUS) {
		/* If pci/pcie, we can get at pci/pcie regs and on newer cores to chipc */

		if ((cores_info->coreid == CC_CORE_ID) && SI_FAST(sii)) {
			/* Chipc registers are mapped at 12KB */

			fast = TRUE;
			r = (volatile uint32 *)((volatile char *)sii->curmap +
				PCI_16KB0_CCREGS_OFFSET + regoff);
		} else if (sii->pub.buscoreidx == coreidx) {
			/* pci registers are at either in the last 2KB of an 8KB window
			 * or, in pcie and pci rev 13 at 8KB
			 */
			fast = TRUE;
			if (SI_FAST(sii)) {
				r = (volatile uint32 *)((volatile char *)sii->curmap +
					PCI_16KB0_PCIREGS_OFFSET + regoff);
			} else {
				r = (volatile uint32 *)((volatile char *)sii->curmap +
					((regoff >= SBCONFIGOFF) ?
					PCI_BAR0_PCISBR_OFFSET : PCI_BAR0_PCIREGS_OFFSET) + regoff);
			}
		}
	}

	if (!fast) {
		INTR_OFF(sii, &intr_val);

		/* save current core index */
		origidx = si_coreidx(&sii->pub);

		/* switch core */
		r = (volatile uint32*) ((volatile uchar*) nci_setcoreidx(&sii->pub, coreidx) +
			regoff);
	}
	ASSERT(r != NULL);

	/* mask and set */
	if (mask || val) {
		w = (R_REG(sii->osh, r) & ~mask) | val;
		W_REG(sii->osh, r, w);
	}

	if (!fast) {
		/* restore core index */
		if (origidx != coreidx) {
			nci_setcoreidx(&sii->pub, origidx);
		}

		INTR_RESTORE(sii, &intr_val);
	}

	return (w);
}

/*
 * If there is no need for fiddling with interrupts or core switches (typically silicon
 * back plane registers, pci registers and chipcommon registers), this function
 * returns the register offset on this core to a mapped address. This address can
 * be used for W_REG/R_REG directly.
 *
 * For accessing registers that would need a core switch, this function will return
 * NULL.
 */
volatile uint32 *
nci_corereg_addr(si_t *sih, uint coreidx, uint regoff)
{
	volatile uint32 *r = NULL;
	bool fast = FALSE;
	si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	nci_cores_t *cores_info = &nci->cores[coreidx];

	NCI_TRACE(("nci_corereg_addr() coreidx %u regoff %u\n", coreidx, regoff));

	ASSERT(GOODIDX(coreidx, nci->num_cores));
	ASSERT(regoff < SI_CORE_SIZE);

	if (coreidx >= SI_MAXCORES) {
		return 0;
	}

	if (BUSTYPE(sih->bustype) == SI_BUS) {
		uint32 curmap = nci_get_curmap(nci, coreidx);
		BCM_REFERENCE(curmap);

		/* If internal bus, we can always get at everything */
		fast = TRUE;
		/* map if does not exist */
		if (!cores_info->regs) {
			cores_info->regs = REG_MAP(curmap, SI_CORE_SIZE);
			ASSERT(GOODREGS(cores_info->regs));
		}
		r = (volatile uint32 *)((volatile uchar *)cores_info->regs + regoff);

	} else if (BUSTYPE(sih->bustype) == PCI_BUS) {
		/* If pci/pcie, we can get at pci/pcie regs and on newer cores to chipc */

		if ((cores_info->coreid == CC_CORE_ID) && SI_FAST(sii)) {
			/* Chipc registers are mapped at 12KB */

			fast = TRUE;
			r = (volatile uint32 *)((volatile char *)sii->curmap +
				PCI_16KB0_CCREGS_OFFSET + regoff);
		} else if (sii->pub.buscoreidx == coreidx) {
			/* pci registers are at either in the last 2KB of an 8KB window
			 * or, in pcie and pci rev 13 at 8KB
			 */
			fast = TRUE;
			if (SI_FAST(sii)) {
				r = (volatile uint32 *)((volatile char *)sii->curmap +
					PCI_16KB0_PCIREGS_OFFSET + regoff);
			} else {
				r = (volatile uint32 *)((volatile char *)sii->curmap +
					((regoff >= SBCONFIGOFF) ?
					PCI_BAR0_PCISBR_OFFSET : PCI_BAR0_PCIREGS_OFFSET) + regoff);
			}
		}
	}

	if (!fast) {
		ASSERT(sii->curidx == coreidx);
		r = (volatile uint32*) ((volatile uchar*)sii->curmap + regoff);
	}

	return (r);
}

uint
BCMPOSTTRAPFN(nci_findcoreidx)(const si_t *sih, uint coreid, uint coreunit)
{
	si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	uint core_idx;

	NCI_TRACE(("nci_findcoreidx() coreid %u coreunit %u\n", coreid, coreunit));

	for (core_idx = 0; core_idx < nci->num_cores; core_idx++) {
		if ((nci->cores[core_idx].coreid == coreid) &&
			(nci->cores[core_idx].coreunit == coreunit)) {
			return core_idx;
		}
	}
	return BADIDX;
}

static uint32
_nci_get_slave_addr_size(nci_info_t *nci, uint coreidx, uint32 slave_port_idx, uint base_idx)
{
	uint32 size;
	uint32 add_desc;

	NCI_TRACE(("_nci_get_slave_addr_size() coreidx %u slave_port_idx %u base_idx %u\n",
		coreidx, slave_port_idx, base_idx));

	add_desc = nci->cores[coreidx].desc[slave_port_idx].sp[base_idx].adesc;

	size = add_desc & SLAVEPORT_ADDR_SIZE_MASK;
	return ADDR_SIZE(size);
}

static uint32
BCMPOSTTRAPFN(_nci_get_curmap)(nci_info_t *nci, uint coreidx, uint slave_port_idx, uint base_idx)
{
	/* TODO: Is handling of 64 bit addressing required */
	NCI_TRACE(("_nci_get_curmap coreidx %u slave_port_idx %u base_idx %u\n",
		coreidx, slave_port_idx, base_idx));
	return nci->cores[coreidx].desc[slave_port_idx].sp[base_idx].addrl;
}

/* Get the interface descriptor which is connected to APB and return its address */
static uint32
BCMPOSTTRAPFN(nci_get_curmap)(nci_info_t *nci, uint coreidx)
{
	nci_cores_t *core_info = &nci->cores[coreidx];
	uint32 iface_idx;

	NCI_TRACE(("nci_get_curmap coreidx %u\n", coreidx));
	for (iface_idx = 0; iface_idx < core_info->iface_cnt; iface_idx++) {
		NCI_TRACE(("nci_get_curmap iface_idx %u BP_ID %u master %u\n",
			iface_idx, ID_BPID(core_info->desc[iface_idx].iface_desc_1),
			IS_MASTER(core_info->desc[iface_idx].iface_desc_1)));

		/* If core is a Backplane or Bridge, then its slave port
		 * will give the pointer to access registers.
		 */
		if (!IS_MASTER(core_info->desc[iface_idx].iface_desc_1) &&
			(IS_BACKPLANE(core_info->coreinfo) ||
			APB_INF(core_info->desc[iface_idx]))) {
			return _nci_get_curmap(nci, coreidx, iface_idx, 0);
		}
	}

	/* no valid slave port address is found */
	return NCI_BAD_REG;
}

static uint32
BCMPOSTTRAPFN(_nci_get_curwrap)(nci_info_t *nci, uint coreidx, uint wrapper_idx)
{
	return nci->cores[coreidx].desc[wrapper_idx].node_ptr;
}

static uint32
BCMPOSTTRAPFN(nci_get_curwrap)(nci_info_t *nci, uint coreidx)
{
	nci_cores_t *core_info = &nci->cores[coreidx];
	uint32 iface_idx;
	NCI_TRACE(("nci_get_curwrap coreidx %u\n", coreidx));
	for (iface_idx = 0; iface_idx < core_info->iface_cnt; iface_idx++) {
		NCI_TRACE(("nci_get_curwrap iface_idx %u BP_ID %u master %u\n",
			iface_idx, ID_BPID(core_info->desc[iface_idx].iface_desc_1),
		IS_MASTER(core_info->desc[iface_idx].iface_desc_1)));
		if ((ID_BPID(core_info->desc[iface_idx].iface_desc_1) == BP_BOOKER) ||
			(ID_BPID(core_info->desc[iface_idx].iface_desc_1) == BP_NIC400)) {
			return _nci_get_curwrap(nci, coreidx, iface_idx);
		}
	}

	/* no valid master wrapper found */
	return NCI_BAD_REG;
}

static void
_nci_setcoreidx_pcie_bus(si_t *sih, volatile void **regs, uint32 curmap,
		uint32 curwrap)
{
	si_info_t *sii = SI_INFO(sih);

	*regs = sii->curmap;
	switch (sii->slice) {
	case 0: /* main/first slice */
		/* point bar0 window */
		OSL_PCI_WRITE_CONFIG(sii->osh, PCI_BAR0_WIN, PCIE_WRITE_SIZE, curmap);
		// TODO: why curwrap is zero i.e no master wrapper
		if (curwrap != 0) {
			if (PCIE_GEN2(sii)) {
				OSL_PCI_WRITE_CONFIG(sii->osh, PCIE2_BAR0_WIN2,
					PCIE_WRITE_SIZE, curwrap);
			} else {
				OSL_PCI_WRITE_CONFIG(sii->osh, PCI_BAR0_WIN2,
					PCIE_WRITE_SIZE, curwrap);
			}
		}
		break;
	case 1: /* aux/second slice */
		/* PCIE GEN2 only for other slices */
		if (!PCIE_GEN2(sii)) {
			/* other slices not supported */
			NCI_ERROR(("pci gen not supported for slice 1\n"));
			ASSERT(0);
			break;
		}

		/* 0x4000 - 0x4fff: enum space 0x5000 - 0x5fff: wrapper space */

		*regs = (volatile uint8 *)*regs + PCI_SEC_BAR0_WIN_OFFSET;
		sii->curwrap = (void *)((uintptr)*regs + SI_CORE_SIZE);

		/* point bar0 window */
		OSL_PCI_WRITE_CONFIG(sii->osh, PCIE2_BAR0_CORE2_WIN, PCIE_WRITE_SIZE,	curmap);
		OSL_PCI_WRITE_CONFIG(sii->osh, PCIE2_BAR0_CORE2_WIN2, PCIE_WRITE_SIZE, curwrap);
		break;

	case 2: /* scan/third slice */
		/* PCIE GEN2 only for other slices */
		if (!PCIE_GEN2(sii)) {
			/* other slices not supported */
			NCI_ERROR(("pci gen not supported for slice 1\n"));
			ASSERT(0);
			break;
		}
		/* 0x9000 - 0x9fff: enum space 0xa000 - 0xafff: wrapper space */
		*regs = (volatile uint8 *)*regs + PCI_SEC_BAR0_WIN_OFFSET;
		sii->curwrap = (void *)((uintptr)*regs + SI_CORE_SIZE);

		/* point bar0 window */
		nci_corereg(sih, sih->buscoreidx, PCIE_TER_BAR0_WIN, ~0, curmap);
		nci_corereg(sih, sih->buscoreidx, PCIE_TER_BAR0_WRAPPER, ~0, curwrap);
		break;
	default:
		ASSERT(0);
		break;
	}
}

static volatile void *
BCMPOSTTRAPFN(_nci_setcoreidx)(si_t *sih, uint coreidx)
{
	si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	nci_cores_t *cores_info = &nci->cores[coreidx];
	uint32 curmap, curwrap;
	volatile void *regs = NULL;

	NCI_TRACE(("_nci_setcoreidx coreidx %u\n", coreidx));
	if (!GOODIDX(coreidx, nci->num_cores)) {
		return (NULL);
	}
	/*
	 * If the user has provided an interrupt mask enabled function,
	 * then assert interrupts are disabled before switching the core.
	 */
	ASSERT((sii->intrsenabled_fn == NULL) ||
		!(*(sii)->intrsenabled_fn)((sii)->intr_arg));

	curmap = nci_get_curmap(nci, coreidx);
	curwrap = nci_get_curwrap(nci, coreidx);

	switch (BUSTYPE(sih->bustype)) {
	case SI_BUS:
		/* map if does not exist */
		if (!cores_info->regs) {
			cores_info->regs = REG_MAP(curmap, SI_CORE_SIZE);
			ASSERT(GOODREGS(cores_info->regs));
		}
		sii->curmap = regs = cores_info->regs;
		sii->curwrap = REG_MAP(curwrap, SI_CORE_SIZE);
		break;

	case PCI_BUS:
		_nci_setcoreidx_pcie_bus(sih, &regs, curmap, curwrap);
		break;

	default:
		NCI_ERROR(("_nci_stcoreidx Invalid bustype %d\n", BUSTYPE(sih->bustype)));
		break;
	}
	sii->curidx = coreidx;
	return regs;
}

volatile void *
BCMPOSTTRAPFN(nci_setcoreidx)(si_t *sih, uint coreidx)
{
	return _nci_setcoreidx(sih, coreidx);
}

volatile void *
BCMPOSTTRAPFN(nci_setcore)(si_t *sih, uint coreid, uint coreunit)
{
	si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	uint core_idx;

	NCI_TRACE(("nci_setcore coreidx %u coreunit %u\n", coreid, coreunit));
	core_idx = nci_findcoreidx(sih, coreid, coreunit);

	if (!GOODIDX(core_idx, nci->num_cores)) {
		return (NULL);
	}
	return nci_setcoreidx(sih, core_idx);
}

/* Get the value of the register at offset "offset" of currently configured core */
uint
BCMPOSTTRAPFN(nci_get_wrap_reg)(const si_t *sih, uint32 offset, uint32 mask, uint32 val)
{
	const si_info_t *sii = SI_INFO(sih);
	uint32 *addr = (uint32 *) ((uchar *)(sii->curwrap) + offset);
	NCI_TRACE(("nci_wrap_reg offset %u mask %u val %u\n", offset, mask, val));

	if (mask || val) {
		uint32 w = R_REG(sii->osh, addr);
		w &= ~mask;
		w |= val;
		W_REG(sii->osh, addr, w);
	}
	return (R_REG(sii->osh, addr));
}

uint
nci_corevendor(const si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;

	NCI_TRACE(("nci_corevendor coreidx %u\n", sii->curidx));
	return (nci->cores[sii->curidx].coreinfo & COREINFO_MFG_MASK) >> COREINFO_MFG_SHIFT;
}

uint
BCMPOSTTRAPFN(nci_corerev)(const si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	uint coreidx = sii->curidx;

	NCI_TRACE(("nci_corerev coreidx %u\n", coreidx));

	return (nci->cores[coreidx].coreinfo & COREINFO_REV_MASK) >> COREINFO_REV_SHIFT;
}

uint
nci_corerev_minor(const si_t *sih)
{
	return (nci_core_sflags(sih, 0, 0) >> SISF_MINORREV_D11_SHIFT) &
			SISF_MINORREV_D11_MASK;
}

uint
BCMPOSTTRAPFN(nci_coreid)(const si_t *sih, uint coreidx)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;

	NCI_TRACE(("nci_coreid coreidx %u\n", coreidx));
	return nci->cores[coreidx].coreid;
}

/** return total coreunit of coreid or zero if not found */
uint
BCMPOSTTRAPFN(nci_numcoreunits)(const si_t *sih, uint coreid)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	uint found = 0;
	uint i;

	NCI_TRACE(("nci_numcoreunits coreidx %u\n", coreid));

	for (i = 0; i < nci->num_cores; i++) {
		if (nci->cores[i].coreid == coreid) {
			found++;
		}
	}

	return found;
}

/* Return the address of the nth address space in the current core
 * Arguments:
 * sih : Pointer to struct si_t
 * spidx : slave port index
 * baidx : base address index
 */
uint32
nci_addr_space(const si_t *sih, uint spidx, uint baidx)
{
	const si_info_t *sii = SI_INFO(sih);
	uint cidx;

	NCI_TRACE(("nci_addr_space spidx %u baidx %u\n", spidx, baidx));
	cidx = sii->curidx;
	return _nci_get_curmap(sii->nci_info, cidx, spidx, baidx);
}

/* Return the size of the nth address space in the current core
* Arguments:
* sih : Pointer to struct si_t
* spidx : slave port index
* baidx : base address index
*/
uint32
nci_addr_space_size(const si_t *sih, uint spidx, uint baidx)
{
	const si_info_t *sii = SI_INFO(sih);
	uint cidx;

	NCI_TRACE(("nci_addr_space_size spidx %u baidx %u\n", spidx, baidx));

	cidx = sii->curidx;
	return _nci_get_slave_addr_size(sii->nci_info, cidx, spidx, baidx);
}

/*
 * Performs soft reset of attached device.
 * Writes have the following effect:
 * 0b1 Request attached device to enter reset.
 * Write is ignored if it occurs before soft reset exit has occurred.
 *
 * 0b0 Request attached device to exit reset.
 * Write is ignored if it occurs before soft reset entry has occurred.
 *
 * Software can poll this register to determine whether soft reset entry or exit has occurred,
 * using the following values:
 * 0b1 Indicates that the device is in reset.
 * 0b0 Indicates that the device is not in reset.
 *
 *
 * Note
 * The register value updates to reflect a request for reset entry or reset exit,
 * but the update can only occur after required internal conditions are met.
 * Until these conditions are met, a read to the register returns the old value.
 * For example, outstanding transactions currently being handled must complete before
 * the register value updates.
 *
 * To ensure reset propagation within the device,
 * it is the responsibility of software to allow enough cycles after
 * soft reset assertion is reflected in the reset control register
 * before exiting soft reset by triggering a write of 0b0.
 * If this responsibility is not met, the behavior is undefined or unpredictable.
 *
 * When the register value is 0b1,
 * the external soft reset pin that connects to the attached AXI master or slave
 * device is asserted, using the correct polarity of the reset pin.
 * When the register value is 0b0, the external softreset
 * pin that connects to the attached AXI master or slave device is deasserted,
 * using the correct polarity of the reset pin.
 */
static void
BCMPOSTTRAPFN(_nci_core_reset)(const si_t *sih, uint32 bits, uint32 resetbits)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	amni_regs_t *amni = (amni_regs_t *)(uintptr)sii->curwrap;
	volatile dmp_regs_t *io;
	volatile uint32* erom_base = 0u;
	uint32 orig_bar0_win1 = 0u;
	volatile uint32 dummy;
	volatile uint32 reg_read;
	uint32 dmp_write_value;

	/* Point to OOBR base */
	switch (BUSTYPE(sih->bustype)) {
	case SI_BUS:
		erom_base = (volatile uint32*)REG_MAP(GET_OOBR_BASE(nci->cc_erom2base),
			SI_CORE_SIZE);
		break;

	case PCI_BUS:
		/*
		 * Save Original Bar0 Win1. In nci, the io registers dmpctrl & dmpstatus
		 * registers are implemented in the EROM section. REF -
		 * https://docs.google.com/document/d/1HE7hAmvdoNFSnMI7MKQV1qVrFBZVsgLdNcILNOA2C8c
		 * This requires addition BAR0 windows mapping to erom section in chipcommon.
		 */
		orig_bar0_win1 = OSL_PCI_READ_CONFIG(nci->osh, PCI_BAR0_WIN,
			PCI_ACCESS_SIZE);

		OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE,
			GET_OOBR_BASE(nci->cc_erom2base));
		erom_base = (volatile uint32*)sii->curmap;
		break;

	default:
		NCI_ERROR(("_nci_core_reset Invalid bustype %d\n", BUSTYPE(sih->bustype)));
		break;
	}

	/* Point to DMP Control */
	io = (dmp_regs_t*)(NCI_ADD_ADDR(erom_base, nci->cores[sii->curidx].dmp_regs_off));

	NCI_TRACE(("_nci_core_reset reg 0x%p io %p\n", amni, io));

	/* Put core into reset */
	W_REG(nci->osh, &amni->idm_reset_ctrl, NI_IDM_RESET_ENTRY);

	/* poll for the reset to happen */
	while (TRUE) {
		/* Wait until reset is effective */
		SPINWAIT(((reg_read = R_REG(nci->osh, &amni->idm_reset_ctrl)) !=
			NI_IDM_RESET_ENTRY), NCI_SPINWAIT_TIMEOUT);

		if (reg_read == NI_IDM_RESET_ENTRY) {
			break;
		}
	}

	dmp_write_value = (bits | resetbits | SICF_FGC | SICF_CLOCK_EN);

	W_REG(nci->osh, &io->dmpctrl, dmp_write_value);

	/* poll for the dmp_reg write to happen */
	while (TRUE) {
		/* Wait until reset is effective */
		SPINWAIT(((reg_read = R_REG(nci->osh, &io->dmpctrl)) !=
			dmp_write_value), NCI_SPINWAIT_TIMEOUT);
		if (reg_read == dmp_write_value) {
			break;
		}
	}

	/* take core out of reset */
	W_REG(nci->osh, &amni->idm_reset_ctrl, 0u);

	/* poll for the core to come out of reset */
	while (TRUE) {
		/* Wait until reset is effected */
		SPINWAIT(((reg_read = R_REG(nci->osh, &amni->idm_reset_ctrl)) !=
			NI_IDM_RESET_EXIT), NCI_SPINWAIT_TIMEOUT);
		if (reg_read == NI_IDM_RESET_EXIT) {
			break;
		}
	}

	dmp_write_value = (bits | SICF_CLOCK_EN);
	W_REG(nci->osh, &io->dmpctrl, (bits | SICF_CLOCK_EN));
	/* poll for the core to come out of reset */
	while (TRUE) {
		SPINWAIT(((reg_read = R_REG(nci->osh, &io->dmpctrl)) !=
			dmp_write_value), NCI_SPINWAIT_TIMEOUT);
		if (reg_read == dmp_write_value) {
			break;
		}
	}

	dummy = R_REG(nci->osh, &io->dmpctrl);
	BCM_REFERENCE(dummy);

	/* Point back to original base */
	if (BUSTYPE(sih->bustype) == PCI_BUS) {
		OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE, orig_bar0_win1);
	}
}

/* reset and re-enable a core
 */
void
BCMPOSTTRAPFN(nci_core_reset)(const si_t *sih, uint32 bits, uint32 resetbits)
{
	const si_info_t *sii = SI_INFO(sih);
	int32 iface_idx = 0u;
	nci_info_t *nci = sii->nci_info;
	nci_cores_t *core = &nci->cores[sii->curidx];

	/* If Wrapper is of NIC400, then call AI functionality */
	for (iface_idx = core->iface_cnt-1; iface_idx >= 0; iface_idx--) {
		if (!(BOOKER_INF(core->desc[iface_idx]) || NIC_INF(core->desc[iface_idx]))) {
			continue;
		}
#ifdef BOOKER_NIC400_INF
		if (core->desc[iface_idx].node_type == NODE_TYPE_NIC400) {
			ai_core_reset_ext(sih, bits, resetbits);
		} else
#endif /* BOOKER_NIC400_INF */
		{
			_nci_core_reset(sih, bits, resetbits);
		}
	}
}

#ifdef BOOKER_NIC400_INF
static int32
BCMPOSTTRAPFN(nci_find_first_wrapper_idx)(nci_info_t *nci, uint32 coreidx)
{
	nci_cores_t *core_info = &nci->cores[coreidx];
	uint32 iface_idx;

	NCI_TRACE(("nci_find_first_wrapper_idx %u\n", coreidx));

	for (iface_idx = 0; iface_idx < core_info->iface_cnt; iface_idx++) {
		NCI_INFO(("nci_find_first_wrapper_idx: %u BP_ID %u master %u\n",
			iface_idx, ID_BPID(core_info->desc[iface_idx].iface_desc_1),
			IS_MASTER(core_info->desc[iface_idx].iface_desc_1)));

		if ((ID_BPID(core_info->desc[iface_idx].iface_desc_1) == BP_BOOKER) ||
			(ID_BPID(core_info->desc[iface_idx].iface_desc_1) == BP_NIC400)) {
			return iface_idx;
		}
	}

	/* no valid master wrapper found */
	return NCI_BAD_INDEX;
}
#endif /* BOOKER_NIC400_INF */

void
nci_core_disable(const si_t *sih, uint32 bits)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	uint32 reg_read;
	volatile dmp_regs_t *io = NULL;
	uint32 orig_bar0_win1 = 0u;
	uint32 dmp_write_value;
	amni_regs_t *amni = (amni_regs_t *)(uintptr)sii->curwrap;
	nci_cores_t *core = &nci->cores[sii->curidx];
	int32 iface_idx;

	NCI_TRACE(("nci_core_disable\n"));

	BCM_REFERENCE(core);
	BCM_REFERENCE(iface_idx);

#ifdef BOOKER_NIC400_INF
	iface_idx = nci_find_first_wrapper_idx(nci, sii->curidx);

	if (iface_idx < 0) {
		NCI_ERROR(("nci_core_disable: First Wrapper is not found\n"));
		ASSERT(0u);
		return;
	}

	/* If Wrapper is of NIC400, then call AI functionality */
	if (core->desc[iface_idx].master && (core->desc[iface_idx].node_type == NODE_TYPE_NIC400)) {
		return ai_core_disable(sih, bits);
	}
#endif /* BOOKER_NIC400_INF */

	ASSERT(GOODREGS(sii->curwrap));
	reg_read = R_REG(nci->osh, &amni->idm_reset_ctrl);

	/* if core is already in reset, just return */
	if (reg_read == NI_IDM_RESET_ENTRY) {
		return;
	}

	/* Put core into reset */
	W_REG(nci->osh, &amni->idm_reset_ctrl, NI_IDM_RESET_ENTRY);
	while (TRUE) {
		/* Wait until reset is effected */
		SPINWAIT(((reg_read = R_REG(nci->osh, &amni->idm_reset_ctrl)) !=
		NI_IDM_RESET_ENTRY), NCI_SPINWAIT_TIMEOUT);
		if (reg_read == NI_IDM_RESET_ENTRY) {
			break;
		}
	}

	/* Point to OOBR base */
	switch (BUSTYPE(sih->bustype)) {
	case SI_BUS:
		io = (volatile dmp_regs_t*)
			REG_MAP(GET_OOBR_BASE(nci->cc_erom2base), SI_CORE_SIZE);
		break;

	case PCI_BUS:
		/* Save Original Bar0 Win1 */
		orig_bar0_win1 =
			OSL_PCI_READ_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE);

		OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE,
			GET_OOBR_BASE(nci->cc_erom2base));
		io = (volatile dmp_regs_t*)sii->curmap;
		break;

	default:
		NCI_ERROR(("nci_core_disable Invalid bustype %d\n", BUSTYPE(sih->bustype)));
		break;

	}

	/* Point to DMP Control */
	io = (dmp_regs_t*)(NCI_ADD_ADDR(io, nci->cores[sii->curidx].dmp_regs_off));

	dmp_write_value = (bits | SICF_FGC | SICF_CLOCK_EN);
	W_REG(nci->osh, &io->dmpctrl, dmp_write_value);

	/* poll for the dmp_reg write to happen */
	while (TRUE) {
		/* Wait until reset is effected */
		SPINWAIT(((reg_read = R_REG(nci->osh, &io->dmpctrl)) != dmp_write_value),
		NCI_SPINWAIT_TIMEOUT);
		if (reg_read == dmp_write_value) {
			break;
		}
	}

	/* Point back to original base */
	if (BUSTYPE(sih->bustype) == PCI_BUS) {
		OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE, orig_bar0_win1);
	}
}

bool
BCMPOSTTRAPFN(nci_iscoreup)(const si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	amni_regs_t *ni = (amni_regs_t *)(uintptr)sii->curwrap;
	uint32 reset_ctrl;

#ifdef BOOKER_NIC400_INF
	nci_cores_t *core = &nci->cores[sii->curidx];
	int32 iface_idx = nci_find_first_wrapper_idx(nci, sii->curidx);

	if (iface_idx < 0) {
		NCI_ERROR(("nci_iscoreup: First Wrapper is not found\n"));
		ASSERT(0u);
		return FALSE;
	}

	/* If Wrapper is of NIC400, then call AI functionality */
	if (core->desc[iface_idx].master && (core->desc[iface_idx].node_type == NODE_TYPE_NIC400)) {
		return ai_iscoreup(sih);
	}
#endif /* BOOKER_NIC400_INF */

	NCI_TRACE(("nci_iscoreup\n"));
	reset_ctrl = R_REG(nci->osh, &ni->idm_reset_ctrl);

	return (reset_ctrl == NI_IDM_RESET_ENTRY) ? FALSE : TRUE;
}

/* TODO: OOB Router core is not available. Can be removed. */
uint
nci_intflag(si_t *sih)
{
	return 0;
}

uint
nci_flag(si_t *sih)
{
	/* TODO: will be implemented if required for NCI */
	return 0;
}

uint
nci_flag_alt(const si_t *sih)
{
	/* TODO: will be implemented if required for NCI */
	return 0;
}

void
BCMATTACHFN(nci_setint)(const si_t *sih, int siflag)
{
	BCM_REFERENCE(sih);
	BCM_REFERENCE(siflag);

	/* TODO: Figure out how to set interrupt mask in nci */
}

/* TODO: OOB Router core is not available. Can we remove or need an alternate implementation. */
uint32
nci_oobr_baseaddr(const si_t *sih, bool second)
{
	return 0;
}

uint
nci_coreunit(const si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	nci_cores_t *cores = nci->cores;
	uint idx;
	uint coreid;
	uint coreunit;
	uint i;

	coreunit = 0;

	idx = sii->curidx;

	ASSERT(GOODREGS(sii->curmap));
	coreid = nci_coreid(sih, sii->curidx);

	/* count the cores of our type */
	for (i = 0; i < idx; i++) {
		if (cores[i].coreid == coreid) {
			coreunit++;
		}
	}

	return (coreunit);
}

uint
nci_corelist(const si_t *sih, uint coreid[])
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	nci_cores_t *cores = nci->cores;
	uint32 i;

	for (i = 0; i < sii->numcores; i++) {
		coreid[i] = cores[i].coreid;
	}

	return (sii->numcores);
}

/* Return the number of address spaces in current core */
int
BCMATTACHFN(nci_numaddrspaces)(const si_t *sih)
{
	/* TODO: Either save it or parse the EROM on demand, currently hardcode 2 */
	BCM_REFERENCE(sih);

	return 2;
}

/* The value of wrap_pos should be greater than 0 */
/* wrapba, wrapba2 and wrapba3 */
uint32
nci_get_nth_wrapper(const si_t *sih, int32 wrap_pos)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	const nci_cores_t *core_info = &nci->cores[sii->curidx];
	uint32 iface_idx;
	uint32 addr = 0;

	ASSERT(wrap_pos >= 0);
	if (wrap_pos < 0) {
		return addr;
	}

	NCI_TRACE(("nci_get_curmap coreidx %u\n", sii->curidx));
	for (iface_idx = 0; iface_idx < core_info->iface_cnt; iface_idx++) {
	NCI_TRACE(("nci_get_curmap iface_idx %u BP_ID %u master %u\n",
		iface_idx, ID_BPID(core_info->desc[iface_idx].iface_desc_1),
		IS_MASTER(core_info->desc[iface_idx].iface_desc_1)));
		/* hack for core idx 8, coreidx without APB Backplane ID */
		if (!IS_MASTER(core_info->desc[iface_idx].iface_desc_1)) {
			continue;
		}
		/* TODO: Should the interface be only BOOKER or NIC is also fine. */
		if (GET_NODETYPE(core_info->desc[iface_idx].iface_desc_0) != NODE_TYPE_BOOKER) {
			continue;
		}
		/* Iterate till we do not get a wrapper at nth (wrap_pos) position */
		if (wrap_pos == 0) {
			break;
		}
		wrap_pos--;
	}
	if (iface_idx < core_info->iface_cnt) {
		addr = GET_NODEPTR(core_info->desc[iface_idx].iface_desc_0);
	}
	return addr;
}

/* Get slave port address of the 0th slave (csp2ba) */
uint32
nci_get_axi_addr(const si_t *sih, uint32 *size)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	const nci_cores_t *core_info = (const nci_cores_t *)&nci->cores[sii->curidx];
	uint32 iface_idx;
	uint32 addr = 0;

	NCI_TRACE(("nci_get_curmap coreidx %u\n", sii->curidx));
	for (iface_idx = 0; iface_idx < core_info->iface_cnt; iface_idx++) {
	NCI_TRACE(("nci_get_curmap iface_idx %u BP_ID %u master %u\n",
		iface_idx, ID_BPID(core_info->desc[iface_idx].iface_desc_1),
		IS_MASTER(core_info->desc[iface_idx].iface_desc_1)));
		if (IS_MASTER(core_info->desc[iface_idx].iface_desc_1)) {
			continue;
		}
		if ((ID_BPID(core_info->desc[iface_idx].iface_desc_1) == BP_BOOKER) ||
			(ID_BPID(core_info->desc[iface_idx].iface_desc_1) == BP_NIC400)) {
			break;
		}
	}
	if (iface_idx < core_info->iface_cnt) {
		/*
		 * TODO: Is there any case where we need to return the slave port address
		 * corresponding to index other than 0.
		 */
		if (&core_info->desc[iface_idx].sp[0] != NULL) {
			addr = core_info->desc[iface_idx].sp[0].addrl;
			if (size) {
				uint32 adesc = core_info->desc[iface_idx].sp[0].adesc;
				*size = SLAVEPORT_ADDR_SIZE(adesc);
			}
		 }
	}
	return addr;
}

/* spidx shouldbe the index of the slave port which we are expecting.
 * The value will vary from 0 to num_addr_reg.
 */
/* coresba and coresba2 */
uint32
nci_get_core_baaddr(const si_t *sih, uint32 *size, int32 baidx)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	const nci_cores_t *core_info = (const nci_cores_t *)&nci->cores[sii->curidx];
	uint32 iface_idx;
	uint32 addr = 0;

	NCI_TRACE(("nci_get_curmap coreidx %u\n", sii->curidx));
	for (iface_idx = 0; iface_idx < core_info->iface_cnt; iface_idx++) {
	NCI_TRACE(("nci_get_curmap iface_idx %u BP_ID %u master %u\n",
		iface_idx, ID_BPID(core_info->desc[iface_idx].iface_desc_1),
		IS_MASTER(core_info->desc[iface_idx].iface_desc_1)));
		/* hack for core idx 8, coreidx without APB Backplane ID */
		if (IS_MASTER(core_info->desc[iface_idx].iface_desc_1)) {
			continue;
		}
		if ((ID_BPID(core_info->desc[iface_idx].iface_desc_1) == BP_APB1) ||
			(ID_BPID(core_info->desc[iface_idx].iface_desc_1) == BP_APB2)) {
			break;
		}
	}
	if (iface_idx < core_info->iface_cnt) {
		/*
		 * TODO: Is there any case where we need to return the slave port address
		 * corresponding to index other than 0.
		 */
		if ((core_info->desc[iface_idx].num_addr_reg > baidx) &&
			(&core_info->desc[iface_idx].sp[baidx] != NULL)) {
			addr = core_info->desc[iface_idx].sp[baidx].addrl;
			if (size) {
				uint32 adesc = core_info->desc[iface_idx].sp[0].adesc;
				*size = SLAVEPORT_ADDR_SIZE(adesc);
			}
		 }
	}
	return addr;
}

uint32
nci_addrspace(const si_t *sih, uint spidx, uint baidx)
{
	if (spidx == CORE_SLAVE_PORT_0) {
		if (baidx == CORE_BASE_ADDR_0) {
			return nci_get_core_baaddr(sih, NULL, CORE_BASE_ADDR_0);
		} else if (baidx == CORE_BASE_ADDR_1) {
			return nci_get_core_baaddr(sih, NULL, CORE_BASE_ADDR_1);
		}
	} else if (spidx == CORE_SLAVE_PORT_1) {
		if (baidx == CORE_BASE_ADDR_0) {
			return nci_get_axi_addr(sih, NULL);
		}
	}

	SI_ERROR(("nci_addrspace: Need to parse the erom again to find %d base addr"
		" in %d slave port\n", baidx, spidx));

	return 0;
}

uint32
BCMATTACHFN(nci_addrspacesize)(const si_t *sih, uint spidx, uint baidx)
{
	uint32 size = 0;

	if (spidx == CORE_SLAVE_PORT_0) {
		if (baidx == CORE_BASE_ADDR_0) {
			nci_get_core_baaddr(sih, &size, CORE_BASE_ADDR_0);
			goto done;
		} else if (baidx == CORE_BASE_ADDR_1) {
			nci_get_core_baaddr(sih, &size, CORE_BASE_ADDR_1);
			goto done;
		}
	} else if (spidx == CORE_SLAVE_PORT_1) {
		if (baidx == CORE_BASE_ADDR_0) {
			nci_get_axi_addr(sih, &size);
			goto done;
		}
	}

	SI_ERROR(("nci_addrspacesize: Need to parse the erom again to find %d"
		" base addr in %d slave port\n", baidx, spidx));
done:
	return size;
}

uint32
BCMPOSTTRAPFN(nci_core_cflags)(const si_t *sih, uint32 mask, uint32 val)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	nci_cores_t *core = &nci->cores[sii->curidx];
	uint32 orig_bar0_win1 = 0;
	int32 iface_idx;
	uint32 w;

	BCM_REFERENCE(iface_idx);

	if ((core[sii->curidx].coreid) == PMU_CORE_ID) {
		NCI_ERROR(("nci_core_cflags: Accessing PMU DMP register (ioctrl)\n"));
		return 0;
	}

	ASSERT(GOODREGS(sii->curwrap));
	ASSERT((val & ~mask) == 0);

#ifdef BOOKER_NIC400_INF
	iface_idx = nci_find_first_wrapper_idx(nci, sii->curidx);
	if (iface_idx < 0) {
		NCI_ERROR(("nci_core_cflags: First Wrapper is not found\n"));
		ASSERT(0u);
		return 0u;
	}

	/* If Wrapper is of NIC400, then call AI functionality */
	if (core->desc[iface_idx].master && (core->desc[iface_idx].node_type == NODE_TYPE_NIC400)) {
		aidmp_t *ai = sii->curwrap;

		if (mask || val) {
			 w = ((R_REG(sii->osh, &ai->ioctrl) & ~mask) | val);
			W_REG(sii->osh, &ai->ioctrl, w);
		}
		return R_REG(sii->osh, &ai->ioctrl);
	} else
#endif /* BOOKER_NIC400_INF */
	{
		volatile dmp_regs_t *io = sii->curwrap;
		volatile uint32 reg_read;

		/* BOOKER */
		/* Point to OOBR base */
		switch (BUSTYPE(sih->bustype)) {
		case SI_BUS:
			io = (volatile dmp_regs_t*)
				REG_MAP(GET_OOBR_BASE(nci->cc_erom2base), SI_CORE_SIZE);
			break;

		case PCI_BUS:
			/* Save Original Bar0 Win1 */
			orig_bar0_win1 =
				OSL_PCI_READ_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE);

			OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE,
				GET_OOBR_BASE(nci->cc_erom2base));
			io = (volatile dmp_regs_t*)sii->curmap;
			break;

		default:
			NCI_ERROR(("nci_core_cflags Invalid bustype %d\n", BUSTYPE(sih->bustype)));
			break;

		}

		/* Point to DMP Control */
		io = (dmp_regs_t*)(NCI_ADD_ADDR(io, nci->cores[sii->curidx].dmp_regs_off));

		if (mask || val) {
			w = ((R_REG(sii->osh, &io->dmpctrl) & ~mask) | val);
			W_REG(sii->osh, &io->dmpctrl, w);
		}

		reg_read = R_REG(sii->osh, &io->dmpctrl);

		/* Point back to original base */
		if (BUSTYPE(sih->bustype) == PCI_BUS) {
			OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN,
				PCI_ACCESS_SIZE, orig_bar0_win1);
		}

		return reg_read;
	}
}

void
BCMPOSTTRAPFN(nci_core_cflags_wo)(const si_t *sih, uint32 mask, uint32 val)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	nci_cores_t *core = &nci->cores[sii->curidx];
	volatile dmp_regs_t *io = sii->curwrap;
	uint32 orig_bar0_win1 = 0;
	int32 iface_idx;
	uint32 w;

	BCM_REFERENCE(iface_idx);

	if ((core[sii->curidx].coreid) == PMU_CORE_ID) {
		NCI_ERROR(("nci_core_cflags: Accessing PMU DMP register (ioctrl)\n"));
		return;
	}

	ASSERT(GOODREGS(sii->curwrap));
	ASSERT((val & ~mask) == 0);

#ifdef BOOKER_NIC400_INF
	iface_idx = nci_find_first_wrapper_idx(nci, sii->curidx);
	if (iface_idx < 0) {
		NCI_ERROR(("nci_core_cflags_wo: First Wrapper is not found\n"));
		ASSERT(0u);
		return;
	}

	/* If Wrapper is of NIC400, then call AI functionality */
	if (core->desc[iface_idx].master && (core->desc[iface_idx].node_type == NODE_TYPE_NIC400)) {
		aidmp_t *ai = sii->curwrap;
		if (mask || val) {
			w = ((R_REG(sii->osh, &ai->ioctrl) & ~mask) | val);
			W_REG(sii->osh, &ai->ioctrl, w);
		}
	} else
#endif /* BOOKER_NIC400_INF */
	{
		/* BOOKER */
		/* Point to OOBR base */
		switch (BUSTYPE(sih->bustype)) {
		case SI_BUS:
			io = (volatile dmp_regs_t*)
				REG_MAP(GET_OOBR_BASE(nci->cc_erom2base), SI_CORE_SIZE);
			break;

		case PCI_BUS:
			/* Save Original Bar0 Win1 */
			orig_bar0_win1 =
				OSL_PCI_READ_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE);

			OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE,
				GET_OOBR_BASE(nci->cc_erom2base));
			io = (volatile dmp_regs_t*)sii->curmap;
			break;

		default:
			NCI_ERROR(("nci_core_cflags_wo Invalid bustype %d\n",
				BUSTYPE(sih->bustype)));
			break;
		}

		/* Point to DMP Control */
		io = (dmp_regs_t*)(NCI_ADD_ADDR(io, nci->cores[sii->curidx].dmp_regs_off));

		if (mask || val) {
			w = ((R_REG(sii->osh, &io->dmpctrl) & ~mask) | val);
			W_REG(sii->osh, &io->dmpctrl, w);
		}

		/* Point back to original base */
		if (BUSTYPE(sih->bustype) == PCI_BUS) {
			OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN,
				PCI_ACCESS_SIZE, orig_bar0_win1);
		}
	}
}

uint32
nci_core_sflags(const si_t *sih, uint32 mask, uint32 val)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	nci_cores_t *core = &nci->cores[sii->curidx];
	uint32 orig_bar0_win1 = 0;
	int32 iface_idx;
	uint32 w;

	BCM_REFERENCE(iface_idx);

	if ((core[sii->curidx].coreid) == PMU_CORE_ID) {
		NCI_ERROR(("nci_core_sflags: Accessing PMU DMP register (ioctrl)\n"));
		return 0;
	}

	ASSERT(GOODREGS(sii->curwrap));

	ASSERT((val & ~mask) == 0);
	ASSERT((mask & ~SISF_CORE_BITS) == 0);

#ifdef BOOKER_NIC400_INF
	iface_idx = nci_find_first_wrapper_idx(nci, sii->curidx);
	if (iface_idx < 0) {
		NCI_ERROR(("nci_core_sflags: First Wrapper is not found\n"));
		ASSERT(0u);
		return 0u;
	}

	/* If Wrapper is of NIC400, then call AI functionality */
	if (core->desc[iface_idx].master && (core->desc[iface_idx].node_type == NODE_TYPE_NIC400)) {
		aidmp_t *ai = sii->curwrap;
		if (mask || val) {
			w = ((R_REG(sii->osh, &ai->iostatus) & ~mask) | val);
			W_REG(sii->osh, &ai->iostatus, w);
		}

		return R_REG(sii->osh, &ai->iostatus);
	} else
#endif /* BOOKER_NIC400_INF */
	{
		volatile dmp_regs_t *io = sii->curwrap;
		volatile uint32 reg_read;

		/* BOOKER */
		/* Point to OOBR base */
		switch (BUSTYPE(sih->bustype)) {
		case SI_BUS:
			io = (volatile dmp_regs_t*)
				REG_MAP(GET_OOBR_BASE(nci->cc_erom2base), SI_CORE_SIZE);
			break;

		case PCI_BUS:
			/* Save Original Bar0 Win1 */
			orig_bar0_win1 =
				OSL_PCI_READ_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE);

			OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN, PCI_ACCESS_SIZE,
			GET_OOBR_BASE(nci->cc_erom2base));
			io = (volatile dmp_regs_t*)sii->curmap;
			break;

		default:
			NCI_ERROR(("nci_core_sflags Invalid bustype %d\n", BUSTYPE(sih->bustype)));
			return 0u;
		}

		/* Point to DMP Control */
		io = (dmp_regs_t*)(NCI_ADD_ADDR(io, nci->cores[sii->curidx].dmp_regs_off));

		if (mask || val) {
			w = ((R_REG(sii->osh, &io->dmpstatus) & ~mask) | val);
			W_REG(sii->osh, &io->dmpstatus, w);
		}

		reg_read = R_REG(sii->osh, &io->dmpstatus);

		/* Point back to original base */
		if (BUSTYPE(sih->bustype) == PCI_BUS) {
			OSL_PCI_WRITE_CONFIG(nci->osh, PCI_BAR0_WIN,
				PCI_ACCESS_SIZE, orig_bar0_win1);
		}

		return reg_read;
	}
}

/* TODO: Used only by host */
int
nci_backplane_access(si_t *sih, uint addr, uint size, uint *val, bool read)
{
	return 0;
}

int
nci_backplane_access_64(si_t *sih, uint addr, uint size, uint64 *val, bool read)
{
	return 0;
}

uint
nci_num_slaveports(const si_t *sih, uint coreidx)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	const nci_cores_t *core_info = (const nci_cores_t *)&nci->cores[coreidx];
	uint32 iface_idx;
	uint32 numports = 0;

	NCI_TRACE(("nci_get_curmap coreidx %u\n", coreidx));
	for (iface_idx = 0; iface_idx < core_info->iface_cnt; iface_idx++) {
	NCI_TRACE(("nci_get_curmap iface_idx %u BP_ID %u master %u\n",
		iface_idx, ID_BPID(core_info->desc[iface_idx].iface_desc_1),
		IS_MASTER(core_info->desc[iface_idx].iface_desc_1)));
		/* hack for core idx 8, coreidx without APB Backplane ID */
		if (IS_MASTER(core_info->desc[iface_idx].iface_desc_1)) {
			continue;
		}
		if ((ID_BPID(core_info->desc[iface_idx].iface_desc_1) == BP_APB1) ||
			(ID_BPID(core_info->desc[iface_idx].iface_desc_1) == BP_APB2)) {
			break;
		}
	}
	if (iface_idx < core_info->iface_cnt) {
		numports = core_info->desc[iface_idx].num_addr_reg;
	}
	return numports;
}

#if defined(BCMDBG) || defined(BCMDBG_DUMP) || defined(BCMDBG_PHYDUMP)
void
nci_dumpregs(const si_t *sih, struct bcmstrbuf *b)
{
	const si_info_t *sii = SI_INFO(sih);

	bcm_bprintf(b, "ChipNum:%x, ChipRev;%x, BusType:%x, BoardType:%x, BoardVendor:%x\n\n",
			sih->chip, sih->chiprev, sih->bustype, sih->boardtype, sih->boardvendor);
	BCM_REFERENCE(sii);
	/* TODO: Implement dump regs for nci. */
}
#endif  /* BCMDBG || BCMDBG_DUMP || BCMDBG_PHYDUMP */

#ifdef BCMDBG
static void
_nci_view(osl_t *osh, aidmp_t *ai, uint32 cid, uint32 addr, bool verbose)
{
	/* TODO: This is WIP and will be developed once the
	 * implementation is done based on the NCI.
	 */
}

void
nci_view(si_t *sih, bool verbose)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	const nci_cores_t *core_info = (const nci_cores_t *)nci->cores;
	osl_t *osh;
	/* TODO: We need to do the structure mapping correctly based on the BOOKER/NIC type */
	aidmp_t *ai;
	uint32 cid, addr;

	ai = sii->curwrap;
	osh = sii->osh;

	if ((core_info[sii->curidx].coreid) == PMU_CORE_ID) {
		SI_ERROR(("Cannot access pmu DMP\n"));
		return;
	}
	cid = core_info[sii->curidx].coreid;
	addr = nci_get_nth_wrapper(sih, 0u);
	_nci_view(osh, ai, cid, addr, verbose);
}

void
nci_viewall(si_t *sih, bool verbose)
{
	const si_info_t *sii = SI_INFO(sih);
	nci_info_t *nci = sii->nci_info;
	const nci_cores_t *core_info = (const nci_cores_t *)nci->cores;
	osl_t *osh;
	aidmp_t *ai;
	uint32 cid, addr;
	uint i;

	osh = sii->osh;
	for (i = 0; i < sii->numcores; i++) {
		nci_setcoreidx(sih, i);

		if ((core_info[i].coreid) == PMU_CORE_ID) {
			SI_ERROR(("Skipping pmu DMP\n"));
			continue;
		}
		ai = sii->curwrap;
		cid = core_info[i].coreid;
		addr = nci_get_nth_wrapper(sih, 0u);
		_nci_view(osh, ai, cid, addr, verbose);
	}
}
#endif /* BCMDBG */

uint32
nci_clear_backplane_to(si_t *sih)
{
	/* TODO: This is WIP and will be developed once the
	 * implementation is done based on the NCI.
	 */
	return 0;
}

#if defined (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC)
static bool g_disable_backplane_logs = FALSE;

static uint32 last_axi_error = AXI_WRAP_STS_NONE;
static uint32 last_axi_error_log_status = 0;
static uint32 last_axi_error_core = 0;
static uint32 last_axi_error_wrap = 0;
static uint32 last_axi_errlog_lo = 0;
static uint32 last_axi_errlog_hi = 0;
static uint32 last_axi_errlog_id = 0;

/* slave error is ignored, so account for those cases */
static uint32 si_ignore_errlog_cnt = 0;

static void
nci_reset_APB(const si_info_t *sii, aidmp_t *ai, int *ret,
		uint32 errlog_status, uint32 errlog_id)
{
	/* only reset APB Bridge on timeout (not slave error, or dec error) */
	switch (errlog_status & AIELS_ERROR_MASK) {
	case AIELS_SLAVE_ERR:
		NCI_PRINT(("AXI slave error\n"));
		*ret |= AXI_WRAP_STS_SLAVE_ERR;
		break;

	case AIELS_TIMEOUT:
		nci_reset_axi_to(sii, ai);
		*ret |= AXI_WRAP_STS_TIMEOUT;
		break;

	case AIELS_DECODE:
		NCI_PRINT(("AXI decode error\n"));
#ifdef USE_HOSTMEM
		/* Ignore known cases of CR4 prefetch abort bugs */
		if ((errlog_id & (BCM_AXI_ID_MASK | BCM_AXI_ACCESS_TYPE_MASK)) !=
				(BCM43xx_AXI_ACCESS_TYPE_PREFETCH | BCM43xx_CR4_AXI_ID))
#endif /* USE_HOSTMEM */
		{
			*ret |= AXI_WRAP_STS_DECODE_ERR;
		}
		break;
	default:
		ASSERT(0);	/* should be impossible */
	}
	if (errlog_status & AIELS_MULTIPLE_ERRORS) {
		NCI_PRINT(("Multiple AXI Errors\n"));
		/* Set multiple errors bit only if actual error is not ignored */
		if (*ret) {
			*ret |= AXI_WRAP_STS_MULTIPLE_ERRORS;
		}
	}
	return;
}
/*
 * API to clear the back plane timeout per core.
 * Caller may passs optional wrapper address. If present this will be used as
 * the wrapper base address. If wrapper base address is provided then caller
 * must provide the coreid also.
 * If both coreid and wrapper is zero, then err status of current bridge
 * will be verified.
 */

uint32
nci_clear_backplane_to_per_core(si_t *sih, uint coreid, uint coreunit, void *wrap)
{
	int ret = AXI_WRAP_STS_NONE;
	aidmp_t *ai = NULL;
	uint32 errlog_status = 0;
	const si_info_t *sii = SI_INFO(sih);
	uint32 errlog_lo = 0, errlog_hi = 0, errlog_id = 0, errlog_flags = 0;
	uint32 current_coreidx = si_coreidx(sih);
	uint32 target_coreidx = nci_findcoreidx(sih, coreid, coreunit);

#if defined(AXI_TIMEOUTS_NIC)
	si_axi_error_t * axi_error = sih->err_info ?
		&sih->err_info->axi_error[sih->err_info->count] : NULL;
#endif /* AXI_TIMEOUTS_NIC */
	bool restore_core = FALSE;

	if ((sii->axi_num_wrappers == 0) ||
#ifdef AXI_TIMEOUTS_NIC
		(!PCIE(sii)) ||
#endif /* AXI_TIMEOUTS_NIC */
		FALSE) {
		SI_VMSG(("nci_clear_backplane_to_per_core, axi_num_wrappers:%d, Is_PCIE:%d,"
			" BUS_TYPE:%d, ID:%x\n",
			sii->axi_num_wrappers, PCIE(sii),
			BUSTYPE(sii->pub.bustype), sii->pub.buscoretype));
		return AXI_WRAP_STS_NONE;
	}

	if (wrap != NULL) {
		ai = (aidmp_t *)wrap;
	} else if (coreid && (target_coreidx != current_coreidx)) {
		if (nci_setcoreidx(sih, target_coreidx) == NULL) {
			/* Unable to set the core */
			NCI_PRINT(("Set Code Failed: coreid:%x, unit:%d, target_coreidx:%d\n",
				coreid, coreunit, target_coreidx));
			errlog_lo = target_coreidx;
			ret = AXI_WRAP_STS_SET_CORE_FAIL;
			goto end;
		}
		restore_core = TRUE;
		ai = (aidmp_t *)si_wrapperregs(sih);
	} else {
		/* Read error status of current wrapper */
		ai = (aidmp_t *)si_wrapperregs(sih);

		/* Update CoreID to current Code ID */
		coreid = nci_coreid(sih, sii->curidx);
	}

	/* read error log status */
	errlog_status = R_REG(sii->osh, &ai->errlogstatus);

	if (errlog_status == ID32_INVALID) {
		/* Do not try to peek further */
		NCI_PRINT(("nci_clear_backplane_to_per_core, errlogstatus:%x - "
				"Slave Wrapper:%x\n", errlog_status, coreid));
		ret = AXI_WRAP_STS_WRAP_RD_ERR;
		errlog_lo = (uint32)(uintptr)&ai->errlogstatus;
		goto end;
	}

	if ((errlog_status & AIELS_ERROR_MASK) != 0) {
		uint32 tmp;
		uint32 count = 0;
		/* set ErrDone to clear the condition */
		W_REG(sii->osh, &ai->errlogdone, AIELD_ERRDONE_MASK);

		/* SPINWAIT on errlogstatus timeout status bits */
		while ((tmp = R_REG(sii->osh, &ai->errlogstatus)) & AIELS_ERROR_MASK) {

			if (tmp == ID32_INVALID) {
				NCI_PRINT(("nci_clear_backplane_to_per_core: prev errlogstatus:%x,"
							" errlogstatus:%x\n",
							errlog_status, tmp));
				ret = AXI_WRAP_STS_WRAP_RD_ERR;

				errlog_lo = (uint32)(uintptr)&ai->errlogstatus;
				goto end;
			}
			/*
			 * Clear again, to avoid getting stuck in the loop, if a new error
			 * is logged after we cleared the first timeout
			 */
			W_REG(sii->osh, &ai->errlogdone, AIELD_ERRDONE_MASK);

			count++;
			OSL_DELAY(10);
			if ((10 * count) > AI_REG_READ_TIMEOUT) {
				errlog_status = tmp;
				break;
			}
		}

		errlog_lo = R_REG(sii->osh, &ai->errlogaddrlo);
		errlog_hi = R_REG(sii->osh, &ai->errlogaddrhi);
		errlog_id = R_REG(sii->osh, &ai->errlogid);
		errlog_flags = R_REG(sii->osh, &ai->errlogflags);

		/* we are already in the error path, so OK to check for the  slave error */
		if (nci_ignore_errlog(sii, ai, errlog_lo, errlog_hi, errlog_id,	errlog_status)) {
			si_ignore_errlog_cnt++;
			goto end;
		}

		nci_reset_APB(sii, ai, &ret, errlog_status, errlog_id);

		NCI_PRINT(("\tCoreID: %x\n", coreid));
		NCI_PRINT(("\t errlog: lo 0x%08x, hi 0x%08x, id 0x%08x, flags 0x%08x"
					", status 0x%08x\n",
					errlog_lo, errlog_hi, errlog_id, errlog_flags,
					errlog_status));
	}

end:
	if (ret != AXI_WRAP_STS_NONE) {
		last_axi_error = ret;
		last_axi_error_log_status = errlog_status;
		last_axi_error_core = coreid;
		last_axi_error_wrap = (uint32)ai;
		last_axi_errlog_lo = errlog_lo;
		last_axi_errlog_hi = errlog_hi;
		last_axi_errlog_id = errlog_id;
	}

#if defined(AXI_TIMEOUTS_NIC)
	if (axi_error && (ret != AXI_WRAP_STS_NONE)) {
		axi_error->error = ret;
		axi_error->coreid = coreid;
		axi_error->errlog_lo = errlog_lo;
		axi_error->errlog_hi = errlog_hi;
		axi_error->errlog_id = errlog_id;
		axi_error->errlog_flags = errlog_flags;
		axi_error->errlog_status = errlog_status;
		sih->err_info->count++;

		if (sih->err_info->count == SI_MAX_ERRLOG_SIZE) {
			sih->err_info->count = SI_MAX_ERRLOG_SIZE - 1;
			NCI_PRINT(("AXI Error log overflow\n"));
		}
	}
#endif /* AXI_TIMEOUTS_NIC */

	if (restore_core) {
		if (nci_setcoreidx(sih, current_coreidx) == NULL) {
			/* Unable to set the core */
			return ID32_INVALID;
		}
	}
	return ret;
}

/* TODO: It needs to be handled based on BOOKER/NCI DMP. */
/* reset AXI timeout */
static void
nci_reset_axi_to(const si_info_t *sii, aidmp_t *ai)
{
	/* reset APB Bridge */
	OR_REG(sii->osh, &ai->resetctrl, AIRC_RESET);
	/* sync write */
	(void)R_REG(sii->osh, &ai->resetctrl);
	/* clear Reset bit */
	AND_REG(sii->osh, &ai->resetctrl, ~(AIRC_RESET));
	/* sync write */
	(void)R_REG(sii->osh, &ai->resetctrl);
	NCI_PRINT(("AXI timeout\n"));
	if (R_REG(sii->osh, &ai->resetctrl) & AIRC_RESET) {
		NCI_PRINT(("reset failed on wrapper %p\n", ai));
		g_disable_backplane_logs = TRUE;
	}
}

void
nci_wrapper_get_last_error(const si_t *sih, uint32 *error_status, uint32 *core, uint32 *lo,
	uint32 *hi, uint32 *id)
{
	*error_status = last_axi_error_log_status;
	*core = last_axi_error_core;
	*lo = last_axi_errlog_lo;
	*hi = last_axi_errlog_hi;
	*id = last_axi_errlog_id;
}

uint32
nci_get_axi_timeout_reg(void)
{
	return (GOODREGS(last_axi_errlog_lo) ? last_axi_errlog_lo : 0);
}
#endif /* AXI_TIMEOUTS || AXI_TIMEOUTS_NIC */

/* TODO: This function should be able to handle NIC as well as BOOKER */
bool
nci_ignore_errlog(const si_info_t *sii, const aidmp_t *ai,
	uint32 lo_addr, uint32 hi_addr, uint32 err_axi_id, uint32 errsts)
{
	uint32 ignore_errsts = AIELS_SLAVE_ERR;
	uint32 ignore_errsts_2 = 0;
	uint32 ignore_hi = BT_CC_SPROM_BADREG_HI;
	uint32 ignore_lo = BT_CC_SPROM_BADREG_LO;
	uint32 ignore_size = BT_CC_SPROM_BADREG_SIZE;
	bool address_check = TRUE;
	uint32 axi_id = 0;
	uint32 axi_id2 = 0;
	bool extd_axi_id_mask = FALSE;
	uint32 axi_id_mask;

	NCI_PRINT(("err check: core %p, error %d, axi id 0x%04x, addr(0x%08x:%08x)\n",
		ai, errsts, err_axi_id, hi_addr, lo_addr));

	/* ignore the BT slave errors if the errlog is to chipcommon addr 0x190 */
	switch (CHIPID(sii->pub.chip)) {
		case BCM4397_CHIP_GRPID:	/* TODO: Are these IDs same for 4397 as well? */
#ifdef BTOVERPCIE
			axi_id = BCM4378_BT_AXI_ID;
			/* For BT over PCIE, ignore any slave error from BT. */
			/* No need to check any address range */
			address_check = FALSE;
#endif /* BTOVERPCIE */
			axi_id2 = BCM4378_ARM_PREFETCH_AXI_ID;
			extd_axi_id_mask = TRUE;
			ignore_errsts_2 = AIELS_DECODE;
			break;
		default:
			return FALSE;
	}

	axi_id_mask = extd_axi_id_mask ? AI_ERRLOGID_AXI_ID_MASK_EXTD : AI_ERRLOGID_AXI_ID_MASK;

	/* AXI ID check */
	err_axi_id &= axi_id_mask;
	errsts &=  AIELS_ERROR_MASK;

	/* check the ignore error cases. 2 checks */
	if (!(((err_axi_id == axi_id) && (errsts == ignore_errsts)) ||
		((err_axi_id == axi_id2) && (errsts == ignore_errsts_2)))) {
		/* not the error ignore cases */
		return FALSE;

	}

	/* check the specific address checks now, if specified */
	if (address_check) {
		/* address range check */
		if ((hi_addr != ignore_hi) ||
			(lo_addr < ignore_lo) || (lo_addr >= (ignore_lo + ignore_size))) {
			return FALSE;
		}
	}

	NCI_PRINT(("err check: ignored\n"));
	return TRUE;
}

/* TODO: Check the CORE to AXI ID mapping for 4397 */
uint32
nci_findcoreidx_by_axiid(const si_t *sih, uint32 axiid)
{
	uint coreid = 0;
	uint coreunit = 0;
	const nci_axi_to_coreidx_t *axi2coreidx = NULL;
	switch (CHIPID(sih->chip)) {
	case BCM4397_CHIP_GRPID:
		axi2coreidx = axi2coreidx_4397;
		break;
	default:
		NCI_PRINT(("Chipid mapping not found\n"));
		break;
	}

	if (!axi2coreidx) {
		return (BADIDX);
	}

	coreid = axi2coreidx[axiid].coreid;
	coreunit = axi2coreidx[axiid].coreunit;

	return nci_findcoreidx(sih, coreid, coreunit);
}

void nci_coreaddrspaceX(const si_t *sih, uint asidx, uint32 *addr, uint32 *size)
{
	/* Adding just a wrapper. Will implement when required. */
}

/*
 * this is not declared as static const, although that is the right thing to do
 * reason being if declared as static const, compile/link process would that in
 * read only section...
 * currently this code/array is used to identify the registers which are dumped
 * during trap processing
 * and usually for the trap buffer, .rodata buffer is reused,  so for now just static
*/
/* TODO: Should we do another mapping for BOOKER and used correct one based on type of DMP. */
#ifdef DONGLEBUILD
static uint32 BCMPOST_TRAP_RODATA(wrapper_offsets_to_dump)[] = {
	OFFSETOF(aidmp_t, ioctrl),
	OFFSETOF(aidmp_t, iostatus),
	OFFSETOF(aidmp_t, resetctrl),
	OFFSETOF(aidmp_t, resetstatus),
	OFFSETOF(aidmp_t, resetreadid),
	OFFSETOF(aidmp_t, resetwriteid),
	OFFSETOF(aidmp_t, errlogctrl),
	OFFSETOF(aidmp_t, errlogdone),
	OFFSETOF(aidmp_t, errlogstatus),
	OFFSETOF(aidmp_t, errlogaddrlo),
	OFFSETOF(aidmp_t, errlogaddrhi),
	OFFSETOF(aidmp_t, errlogid),
	OFFSETOF(aidmp_t, errloguser),
	OFFSETOF(aidmp_t, errlogflags),
	OFFSETOF(aidmp_t, itipoobaout),
	OFFSETOF(aidmp_t, itipoobbout),
	OFFSETOF(aidmp_t, itipoobcout),
	OFFSETOF(aidmp_t, itipoobdout)
};

static uint32
BCMRAMFN(nci_get_sizeof_wrapper_offsets_to_dump)(void)
{
	return (sizeof(wrapper_offsets_to_dump));
}

static uint32
BCMRAMFN(nci_get_wrapper_base_addr)(uint32 **offset)
{
	uint32 arr_size = ARRAYSIZE(wrapper_offsets_to_dump);

	*offset = &wrapper_offsets_to_dump[0];
	return arr_size;
}

#ifdef UART_TRAP_DBG
/* TODO: Is br_wrapba populated for 4397 NCI? */
void
nci_dump_APB_Bridge_registers(const si_t *sih)
{
	aidmp_t *ai;
	const si_info_t *sii = SI_INFO(sih);

	ai = (aidmp_t *)sii->br_wrapba[0];
	printf("APB Bridge 0\n");
	printf("lo 0x%08x, hi 0x%08x, id 0x%08x, flags 0x%08x",
		R_REG(sii->osh, &ai->errlogaddrlo),
		R_REG(sii->osh, &ai->errlogaddrhi),
		R_REG(sii->osh, &ai->errlogid),
		R_REG(sii->osh, &ai->errlogflags));
	printf("\n status 0x%08x\n", R_REG(sii->osh, &ai->errlogstatus));
}
#endif /* UART_TRAP_DBG */

uint32
BCMATTACHFN(nci_wrapper_dump_buf_size)(const si_t *sih)
{
	uint32 buf_size = 0;
	uint32 wrapper_count = 0;
	const si_info_t *sii = SI_INFO(sih);

	wrapper_count = sii->axi_num_wrappers;
	if (wrapper_count == 0) {
		return 0;
	}

	/* cnt indicates how many registers, tag_id 0 will say these are address/value */
	/* address/value pairs */
	buf_size += 2 * (nci_get_sizeof_wrapper_offsets_to_dump() * wrapper_count);

	return buf_size;
}

uint32*
nci_wrapper_dump_binary_one(const si_info_t *sii, uint32 *p32, uint32 wrap_ba)
{
	uint i;
	uint32 *addr;
	uint32 arr_size;
	uint32 *offset_base;

	arr_size = nci_get_wrapper_base_addr(&offset_base);

	for (i = 0; i < arr_size; i++) {
		addr = (uint32 *)(wrap_ba + *(offset_base + i));
		*p32++ = (uint32)addr;
		*p32++ = R_REG(sii->osh, addr);
	}
	return p32;
}

uint32
nci_wrapper_dump_binary(const si_t *sih, uchar *p)
{
	uint32 *p32 = (uint32 *)p;
	uint32 i;
	const si_info_t *sii = SI_INFO(sih);

	for (i = 0; i < sii->axi_num_wrappers; i++) {
		p32 = nci_wrapper_dump_binary_one(sii, p32, sii->axi_wrapper[i].wrapper_addr);
	}
	return 0;
}

#if defined(ETD)
uint32
nci_wrapper_dump_last_timeout(const si_t *sih, uint32 *error, uint32 *core, uint32 *ba, uchar *p)
{
#if defined (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC)
	uint32 *p32;
	uint32 wrap_ba = last_axi_error_wrap;
	uint i;
	uint32 *addr;

	const si_info_t *sii = SI_INFO(sih);

	if (last_axi_error != AXI_WRAP_STS_NONE) {
		if (wrap_ba) {
			p32 = (uint32 *)p;
			uint32 arr_size;
			uint32 *offset_base;

			arr_size = nci_get_wrapper_base_addr(&offset_base);
			for (i = 0; i < arr_size; i++) {
				addr = (uint32 *)(wrap_ba + *(offset_base + i));
				*p32++ = R_REG(sii->osh, addr);
			}
		}
		*error = last_axi_error;
		*core = last_axi_error_core;
		*ba = wrap_ba;
	}
#else
	*error = 0;
	*core = 0;
	*ba = 0;
#endif /* AXI_TIMEOUTS || AXI_TIMEOUTS_NIC */
	return 0;
}
#endif /* ETD */

bool
nci_check_enable_backplane_log(const si_t *sih)
{
#if defined (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC)
	if (g_disable_backplane_logs) {
		return FALSE;
	}
	else {
		return TRUE;
	}
#else /*  (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC) */
	return FALSE;
#endif /*  (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC) */
}
#endif /* DONGLEBUILD */

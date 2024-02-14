/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 */

#ifndef __FSL_PAMU_H
#define __FSL_PAMU_H

#include <linux/iommu.h>
#include <linux/pci.h>

#include <asm/fsl_pamu_stash.h>

/* Bit Field macros
 *	v = bit field variable; m = mask, m##_SHIFT = shift, x = value to load
 */
#define set_bf(v, m, x)		(v = ((v) & ~(m)) | (((x) << m##_SHIFT) & (m)))
#define get_bf(v, m)		(((v) & (m)) >> m##_SHIFT)

/* PAMU CCSR space */
#define PAMU_PGC 0x00000000     /* Allows all peripheral accesses */
#define PAMU_PE 0x40000000      /* enable PAMU                    */

/* PAMU_OFFSET to the next pamu space in ccsr */
#define PAMU_OFFSET 0x1000

#define PAMU_MMAP_REGS_BASE 0

struct pamu_mmap_regs {
	u32 ppbah;
	u32 ppbal;
	u32 pplah;
	u32 pplal;
	u32 spbah;
	u32 spbal;
	u32 splah;
	u32 splal;
	u32 obah;
	u32 obal;
	u32 olah;
	u32 olal;
};

/* PAMU Error Registers */
#define PAMU_POES1 0x0040
#define PAMU_POES2 0x0044
#define PAMU_POEAH 0x0048
#define PAMU_POEAL 0x004C
#define PAMU_AVS1  0x0050
#define PAMU_AVS1_AV    0x1
#define PAMU_AVS1_OTV   0x6
#define PAMU_AVS1_APV   0x78
#define PAMU_AVS1_WAV   0x380
#define PAMU_AVS1_LAV   0x1c00
#define PAMU_AVS1_GCV   0x2000
#define PAMU_AVS1_PDV   0x4000
#define PAMU_AV_MASK    (PAMU_AVS1_AV | PAMU_AVS1_OTV | PAMU_AVS1_APV | PAMU_AVS1_WAV \
			 | PAMU_AVS1_LAV | PAMU_AVS1_GCV | PAMU_AVS1_PDV)
#define PAMU_AVS1_LIODN_SHIFT 16
#define PAMU_LAV_LIODN_NOT_IN_PPAACT 0x400

#define PAMU_AVS2  0x0054
#define PAMU_AVAH  0x0058
#define PAMU_AVAL  0x005C
#define PAMU_EECTL 0x0060
#define PAMU_EEDIS 0x0064
#define PAMU_EEINTEN 0x0068
#define PAMU_EEDET 0x006C
#define PAMU_EEATTR 0x0070
#define PAMU_EEAHI 0x0074
#define PAMU_EEALO 0x0078
#define PAMU_EEDHI 0X007C
#define PAMU_EEDLO 0x0080
#define PAMU_EECC  0x0084
#define PAMU_UDAD  0x0090

/* PAMU Revision Registers */
#define PAMU_PR1 0x0BF8
#define PAMU_PR2 0x0BFC

/* PAMU version mask */
#define PAMU_PR1_MASK 0xffff

/* PAMU Capabilities Registers */
#define PAMU_PC1 0x0C00
#define PAMU_PC2 0x0C04
#define PAMU_PC3 0x0C08
#define PAMU_PC4 0x0C0C

/* PAMU Control Register */
#define PAMU_PC 0x0C10

/* PAMU control defs */
#define PAMU_CONTROL 0x0C10
#define PAMU_PC_PGC 0x80000000  /* PAMU gate closed bit */
#define PAMU_PC_PE   0x40000000 /* PAMU enable bit */
#define PAMU_PC_SPCC 0x00000010 /* sPAACE cache enable */
#define PAMU_PC_PPCC 0x00000001 /* pPAACE cache enable */
#define PAMU_PC_OCE  0x00001000 /* OMT cache enable */

#define PAMU_PFA1 0x0C14
#define PAMU_PFA2 0x0C18

#define PAMU_PC2_MLIODN(X) ((X) >> 16)
#define PAMU_PC3_MWCE(X) (((X) >> 21) & 0xf)

/* PAMU Interrupt control and Status Register */
#define PAMU_PICS 0x0C1C
#define PAMU_ACCESS_VIOLATION_STAT   0x8
#define PAMU_ACCESS_VIOLATION_ENABLE 0x4

/* PAMU Debug Registers */
#define PAMU_PD1 0x0F00
#define PAMU_PD2 0x0F04
#define PAMU_PD3 0x0F08
#define PAMU_PD4 0x0F0C

#define PAACE_AP_PERMS_DENIED  0x0
#define PAACE_AP_PERMS_QUERY   0x1
#define PAACE_AP_PERMS_UPDATE  0x2
#define PAACE_AP_PERMS_ALL     0x3

#define PAACE_DD_TO_HOST       0x0
#define PAACE_DD_TO_IO         0x1
#define PAACE_PT_PRIMARY       0x0
#define PAACE_PT_SECONDARY     0x1
#define PAACE_V_INVALID        0x0
#define PAACE_V_VALID          0x1
#define PAACE_MW_SUBWINDOWS    0x1

#define PAACE_WSE_4K           0xB
#define PAACE_WSE_8K           0xC
#define PAACE_WSE_16K          0xD
#define PAACE_WSE_32K          0xE
#define PAACE_WSE_64K          0xF
#define PAACE_WSE_128K         0x10
#define PAACE_WSE_256K         0x11
#define PAACE_WSE_512K         0x12
#define PAACE_WSE_1M           0x13
#define PAACE_WSE_2M           0x14
#define PAACE_WSE_4M           0x15
#define PAACE_WSE_8M           0x16
#define PAACE_WSE_16M          0x17
#define PAACE_WSE_32M          0x18
#define PAACE_WSE_64M          0x19
#define PAACE_WSE_128M         0x1A
#define PAACE_WSE_256M         0x1B
#define PAACE_WSE_512M         0x1C
#define PAACE_WSE_1G           0x1D
#define PAACE_WSE_2G           0x1E
#define PAACE_WSE_4G           0x1F

#define PAACE_DID_PCI_EXPRESS_1 0x00
#define PAACE_DID_PCI_EXPRESS_2 0x01
#define PAACE_DID_PCI_EXPRESS_3 0x02
#define PAACE_DID_PCI_EXPRESS_4 0x03
#define PAACE_DID_LOCAL_BUS     0x04
#define PAACE_DID_SRIO          0x0C
#define PAACE_DID_MEM_1         0x10
#define PAACE_DID_MEM_2         0x11
#define PAACE_DID_MEM_3         0x12
#define PAACE_DID_MEM_4         0x13
#define PAACE_DID_MEM_1_2       0x14
#define PAACE_DID_MEM_3_4       0x15
#define PAACE_DID_MEM_1_4       0x16
#define PAACE_DID_BM_SW_PORTAL  0x18
#define PAACE_DID_PAMU          0x1C
#define PAACE_DID_CAAM          0x21
#define PAACE_DID_QM_SW_PORTAL  0x3C
#define PAACE_DID_CORE0_INST    0x80
#define PAACE_DID_CORE0_DATA    0x81
#define PAACE_DID_CORE1_INST    0x82
#define PAACE_DID_CORE1_DATA    0x83
#define PAACE_DID_CORE2_INST    0x84
#define PAACE_DID_CORE2_DATA    0x85
#define PAACE_DID_CORE3_INST    0x86
#define PAACE_DID_CORE3_DATA    0x87
#define PAACE_DID_CORE4_INST    0x88
#define PAACE_DID_CORE4_DATA    0x89
#define PAACE_DID_CORE5_INST    0x8A
#define PAACE_DID_CORE5_DATA    0x8B
#define PAACE_DID_CORE6_INST    0x8C
#define PAACE_DID_CORE6_DATA    0x8D
#define PAACE_DID_CORE7_INST    0x8E
#define PAACE_DID_CORE7_DATA    0x8F
#define PAACE_DID_BROADCAST     0xFF

#define PAACE_ATM_NO_XLATE      0x00
#define PAACE_ATM_WINDOW_XLATE  0x01
#define PAACE_ATM_PAGE_XLATE    0x02
#define PAACE_ATM_WIN_PG_XLATE  (PAACE_ATM_WINDOW_XLATE | PAACE_ATM_PAGE_XLATE)
#define PAACE_OTM_NO_XLATE      0x00
#define PAACE_OTM_IMMEDIATE     0x01
#define PAACE_OTM_INDEXED       0x02
#define PAACE_OTM_RESERVED      0x03

#define PAACE_M_COHERENCE_REQ   0x01

#define PAACE_PID_0             0x0
#define PAACE_PID_1             0x1
#define PAACE_PID_2             0x2
#define PAACE_PID_3             0x3
#define PAACE_PID_4             0x4
#define PAACE_PID_5             0x5
#define PAACE_PID_6             0x6
#define PAACE_PID_7             0x7

#define PAACE_TCEF_FORMAT0_8B   0x00
#define PAACE_TCEF_FORMAT1_RSVD 0x01
/*
 * Hard coded value for the PAACT size to accommodate
 * maximum LIODN value generated by u-boot.
 */
#define PAACE_NUMBER_ENTRIES    0x500
/* Hard coded value for the SPAACT size */
#define SPAACE_NUMBER_ENTRIES	0x800

#define	OME_NUMBER_ENTRIES      16

/* PAACE Bit Field Defines */
#define PPAACE_AF_WBAL			0xfffff000
#define PPAACE_AF_WBAL_SHIFT		12
#define PPAACE_AF_WSE			0x00000fc0
#define PPAACE_AF_WSE_SHIFT		6
#define PPAACE_AF_MW			0x00000020
#define PPAACE_AF_MW_SHIFT		5

#define SPAACE_AF_LIODN			0xffff0000
#define SPAACE_AF_LIODN_SHIFT		16

#define PAACE_AF_AP			0x00000018
#define PAACE_AF_AP_SHIFT		3
#define PAACE_AF_DD			0x00000004
#define PAACE_AF_DD_SHIFT		2
#define PAACE_AF_PT			0x00000002
#define PAACE_AF_PT_SHIFT		1
#define PAACE_AF_V			0x00000001
#define PAACE_AF_V_SHIFT		0

#define PAACE_DA_HOST_CR		0x80
#define PAACE_DA_HOST_CR_SHIFT		7

#define PAACE_IA_CID			0x00FF0000
#define PAACE_IA_CID_SHIFT		16
#define PAACE_IA_WCE			0x000000F0
#define PAACE_IA_WCE_SHIFT		4
#define PAACE_IA_ATM			0x0000000C
#define PAACE_IA_ATM_SHIFT		2
#define PAACE_IA_OTM			0x00000003
#define PAACE_IA_OTM_SHIFT		0

#define PAACE_WIN_TWBAL			0xfffff000
#define PAACE_WIN_TWBAL_SHIFT		12
#define PAACE_WIN_SWSE			0x00000fc0
#define PAACE_WIN_SWSE_SHIFT		6

/* PAMU Data Structures */
/* primary / secondary paact structure */
struct paace {
	/* PAACE Offset 0x00 */
	u32 wbah;				/* only valid for Primary PAACE */
	u32 addr_bitfields;		/* See P/S PAACE_AF_* */

	/* PAACE Offset 0x08 */
	/* Interpretation of first 32 bits dependent on DD above */
	union {
		struct {
			/* Destination ID, see PAACE_DID_* defines */
			u8 did;
			/* Partition ID */
			u8 pid;
			/* Snoop ID */
			u8 snpid;
			/* coherency_required : 1 reserved : 7 */
			u8 coherency_required; /* See PAACE_DA_* */
		} to_host;
		struct {
			/* Destination ID, see PAACE_DID_* defines */
			u8  did;
			u8  reserved1;
			u16 reserved2;
		} to_io;
	} domain_attr;

	/* Implementation attributes + window count + address & operation translation modes */
	u32 impl_attr;			/* See PAACE_IA_* */

	/* PAACE Offset 0x10 */
	/* Translated window base address */
	u32 twbah;
	u32 win_bitfields;			/* See PAACE_WIN_* */

	/* PAACE Offset 0x18 */
	/* first secondary paace entry */
	u32 fspi;				/* only valid for Primary PAACE */
	union {
		struct {
			u8 ioea;
			u8 moea;
			u8 ioeb;
			u8 moeb;
		} immed_ot;
		struct {
			u16 reserved;
			u16 omi;
		} index_ot;
	} op_encode;

	/* PAACE Offsets 0x20-0x38 */
	u32 reserved[8];			/* not currently implemented */
};

/* OME : Operation mapping entry
 * MOE : Mapped Operation Encodings
 * The operation mapping table is table containing operation mapping entries (OME).
 * The index of a particular OME is programmed in the PAACE entry for translation
 * in bound I/O operations corresponding to an LIODN. The OMT is used for translation
 * specifically in case of the indexed translation mode. Each OME contains a 128
 * byte mapped operation encoding (MOE), where each byte represents an MOE.
 */
#define NUM_MOE 128
struct ome {
	u8 moe[NUM_MOE];
} __packed;

#define PAACT_SIZE              (sizeof(struct paace) * PAACE_NUMBER_ENTRIES)
#define SPAACT_SIZE              (sizeof(struct paace) * SPAACE_NUMBER_ENTRIES)
#define OMT_SIZE                (sizeof(struct ome) * OME_NUMBER_ENTRIES)

#define PAMU_PAGE_SHIFT 12
#define PAMU_PAGE_SIZE  4096ULL

#define IOE_READ        0x00
#define IOE_READ_IDX    0x00
#define IOE_WRITE       0x81
#define IOE_WRITE_IDX   0x01
#define IOE_EREAD0      0x82    /* Enhanced read type 0 */
#define IOE_EREAD0_IDX  0x02    /* Enhanced read type 0 */
#define IOE_EWRITE0     0x83    /* Enhanced write type 0 */
#define IOE_EWRITE0_IDX 0x03    /* Enhanced write type 0 */
#define IOE_DIRECT0     0x84    /* Directive type 0 */
#define IOE_DIRECT0_IDX 0x04    /* Directive type 0 */
#define IOE_EREAD1      0x85    /* Enhanced read type 1 */
#define IOE_EREAD1_IDX  0x05    /* Enhanced read type 1 */
#define IOE_EWRITE1     0x86    /* Enhanced write type 1 */
#define IOE_EWRITE1_IDX 0x06    /* Enhanced write type 1 */
#define IOE_DIRECT1     0x87    /* Directive type 1 */
#define IOE_DIRECT1_IDX 0x07    /* Directive type 1 */
#define IOE_RAC         0x8c    /* Read with Atomic clear */
#define IOE_RAC_IDX     0x0c    /* Read with Atomic clear */
#define IOE_RAS         0x8d    /* Read with Atomic set */
#define IOE_RAS_IDX     0x0d    /* Read with Atomic set */
#define IOE_RAD         0x8e    /* Read with Atomic decrement */
#define IOE_RAD_IDX     0x0e    /* Read with Atomic decrement */
#define IOE_RAI         0x8f    /* Read with Atomic increment */
#define IOE_RAI_IDX     0x0f    /* Read with Atomic increment */

#define EOE_READ        0x00
#define EOE_WRITE       0x01
#define EOE_RAC         0x0c    /* Read with Atomic clear */
#define EOE_RAS         0x0d    /* Read with Atomic set */
#define EOE_RAD         0x0e    /* Read with Atomic decrement */
#define EOE_RAI         0x0f    /* Read with Atomic increment */
#define EOE_LDEC        0x10    /* Load external cache */
#define EOE_LDECL       0x11    /* Load external cache with stash lock */
#define EOE_LDECPE      0x12    /* Load external cache with preferred exclusive */
#define EOE_LDECPEL     0x13    /* Load external cache with preferred exclusive and lock */
#define EOE_LDECFE      0x14    /* Load external cache with forced exclusive */
#define EOE_LDECFEL     0x15    /* Load external cache with forced exclusive and lock */
#define EOE_RSA         0x16    /* Read with stash allocate */
#define EOE_RSAU        0x17    /* Read with stash allocate and unlock */
#define EOE_READI       0x18    /* Read with invalidate */
#define EOE_RWNITC      0x19    /* Read with no intention to cache */
#define EOE_WCI         0x1a    /* Write cache inhibited */
#define EOE_WWSA        0x1b    /* Write with stash allocate */
#define EOE_WWSAL       0x1c    /* Write with stash allocate and lock */
#define EOE_WWSAO       0x1d    /* Write with stash allocate only */
#define EOE_WWSAOL      0x1e    /* Write with stash allocate only and lock */
#define EOE_VALID       0x80

/* Function prototypes */
int pamu_domain_init(void);
int pamu_enable_liodn(int liodn);
int pamu_disable_liodn(int liodn);
int pamu_config_ppaace(int liodn, u32 omi, uint32_t stashid, int prot);

u32 get_stash_id(u32 stash_dest_hint, u32 vcpu);
void get_ome_index(u32 *omi_index, struct device *dev);
int  pamu_update_paace_stash(int liodn, u32 value);

#endif  /* __FSL_PAMU_H */

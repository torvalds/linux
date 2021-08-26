// SPDX-License-Identifier: GPL-2.0
/* SandyBridge-EP/IvyTown uncore support */
#include "uncore.h"
#include "uncore_discovery.h"

/* SNB-EP pci bus to socket mapping */
#define SNBEP_CPUNODEID			0x40
#define SNBEP_GIDNIDMAP			0x54

/* SNB-EP Box level control */
#define SNBEP_PMON_BOX_CTL_RST_CTRL	(1 << 0)
#define SNBEP_PMON_BOX_CTL_RST_CTRS	(1 << 1)
#define SNBEP_PMON_BOX_CTL_FRZ		(1 << 8)
#define SNBEP_PMON_BOX_CTL_FRZ_EN	(1 << 16)
#define SNBEP_PMON_BOX_CTL_INT		(SNBEP_PMON_BOX_CTL_RST_CTRL | \
					 SNBEP_PMON_BOX_CTL_RST_CTRS | \
					 SNBEP_PMON_BOX_CTL_FRZ_EN)
/* SNB-EP event control */
#define SNBEP_PMON_CTL_EV_SEL_MASK	0x000000ff
#define SNBEP_PMON_CTL_UMASK_MASK	0x0000ff00
#define SNBEP_PMON_CTL_RST		(1 << 17)
#define SNBEP_PMON_CTL_EDGE_DET		(1 << 18)
#define SNBEP_PMON_CTL_EV_SEL_EXT	(1 << 21)
#define SNBEP_PMON_CTL_EN		(1 << 22)
#define SNBEP_PMON_CTL_INVERT		(1 << 23)
#define SNBEP_PMON_CTL_TRESH_MASK	0xff000000
#define SNBEP_PMON_RAW_EVENT_MASK	(SNBEP_PMON_CTL_EV_SEL_MASK | \
					 SNBEP_PMON_CTL_UMASK_MASK | \
					 SNBEP_PMON_CTL_EDGE_DET | \
					 SNBEP_PMON_CTL_INVERT | \
					 SNBEP_PMON_CTL_TRESH_MASK)

/* SNB-EP Ubox event control */
#define SNBEP_U_MSR_PMON_CTL_TRESH_MASK		0x1f000000
#define SNBEP_U_MSR_PMON_RAW_EVENT_MASK		\
				(SNBEP_PMON_CTL_EV_SEL_MASK | \
				 SNBEP_PMON_CTL_UMASK_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_PMON_CTL_INVERT | \
				 SNBEP_U_MSR_PMON_CTL_TRESH_MASK)

#define SNBEP_CBO_PMON_CTL_TID_EN		(1 << 19)
#define SNBEP_CBO_MSR_PMON_RAW_EVENT_MASK	(SNBEP_PMON_RAW_EVENT_MASK | \
						 SNBEP_CBO_PMON_CTL_TID_EN)

/* SNB-EP PCU event control */
#define SNBEP_PCU_MSR_PMON_CTL_OCC_SEL_MASK	0x0000c000
#define SNBEP_PCU_MSR_PMON_CTL_TRESH_MASK	0x1f000000
#define SNBEP_PCU_MSR_PMON_CTL_OCC_INVERT	(1 << 30)
#define SNBEP_PCU_MSR_PMON_CTL_OCC_EDGE_DET	(1 << 31)
#define SNBEP_PCU_MSR_PMON_RAW_EVENT_MASK	\
				(SNBEP_PMON_CTL_EV_SEL_MASK | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_SEL_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_PMON_CTL_INVERT | \
				 SNBEP_PCU_MSR_PMON_CTL_TRESH_MASK | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_INVERT | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_EDGE_DET)

#define SNBEP_QPI_PCI_PMON_RAW_EVENT_MASK	\
				(SNBEP_PMON_RAW_EVENT_MASK | \
				 SNBEP_PMON_CTL_EV_SEL_EXT)

/* SNB-EP pci control register */
#define SNBEP_PCI_PMON_BOX_CTL			0xf4
#define SNBEP_PCI_PMON_CTL0			0xd8
/* SNB-EP pci counter register */
#define SNBEP_PCI_PMON_CTR0			0xa0

/* SNB-EP home agent register */
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0	0x40
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1	0x44
#define SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH	0x48
/* SNB-EP memory controller register */
#define SNBEP_MC_CHy_PCI_PMON_FIXED_CTL		0xf0
#define SNBEP_MC_CHy_PCI_PMON_FIXED_CTR		0xd0
/* SNB-EP QPI register */
#define SNBEP_Q_Py_PCI_PMON_PKT_MATCH0		0x228
#define SNBEP_Q_Py_PCI_PMON_PKT_MATCH1		0x22c
#define SNBEP_Q_Py_PCI_PMON_PKT_MASK0		0x238
#define SNBEP_Q_Py_PCI_PMON_PKT_MASK1		0x23c

/* SNB-EP Ubox register */
#define SNBEP_U_MSR_PMON_CTR0			0xc16
#define SNBEP_U_MSR_PMON_CTL0			0xc10

#define SNBEP_U_MSR_PMON_UCLK_FIXED_CTL		0xc08
#define SNBEP_U_MSR_PMON_UCLK_FIXED_CTR		0xc09

/* SNB-EP Cbo register */
#define SNBEP_C0_MSR_PMON_CTR0			0xd16
#define SNBEP_C0_MSR_PMON_CTL0			0xd10
#define SNBEP_C0_MSR_PMON_BOX_CTL		0xd04
#define SNBEP_C0_MSR_PMON_BOX_FILTER		0xd14
#define SNBEP_CBO_MSR_OFFSET			0x20

#define SNBEP_CB0_MSR_PMON_BOX_FILTER_TID	0x1f
#define SNBEP_CB0_MSR_PMON_BOX_FILTER_NID	0x3fc00
#define SNBEP_CB0_MSR_PMON_BOX_FILTER_STATE	0x7c0000
#define SNBEP_CB0_MSR_PMON_BOX_FILTER_OPC	0xff800000

#define SNBEP_CBO_EVENT_EXTRA_REG(e, m, i) {	\
	.event = (e),				\
	.msr = SNBEP_C0_MSR_PMON_BOX_FILTER,	\
	.config_mask = (m),			\
	.idx = (i)				\
}

/* SNB-EP PCU register */
#define SNBEP_PCU_MSR_PMON_CTR0			0xc36
#define SNBEP_PCU_MSR_PMON_CTL0			0xc30
#define SNBEP_PCU_MSR_PMON_BOX_CTL		0xc24
#define SNBEP_PCU_MSR_PMON_BOX_FILTER		0xc34
#define SNBEP_PCU_MSR_PMON_BOX_FILTER_MASK	0xffffffff
#define SNBEP_PCU_MSR_CORE_C3_CTR		0x3fc
#define SNBEP_PCU_MSR_CORE_C6_CTR		0x3fd

/* IVBEP event control */
#define IVBEP_PMON_BOX_CTL_INT		(SNBEP_PMON_BOX_CTL_RST_CTRL | \
					 SNBEP_PMON_BOX_CTL_RST_CTRS)
#define IVBEP_PMON_RAW_EVENT_MASK		(SNBEP_PMON_CTL_EV_SEL_MASK | \
					 SNBEP_PMON_CTL_UMASK_MASK | \
					 SNBEP_PMON_CTL_EDGE_DET | \
					 SNBEP_PMON_CTL_TRESH_MASK)
/* IVBEP Ubox */
#define IVBEP_U_MSR_PMON_GLOBAL_CTL		0xc00
#define IVBEP_U_PMON_GLOBAL_FRZ_ALL		(1 << 31)
#define IVBEP_U_PMON_GLOBAL_UNFRZ_ALL		(1 << 29)

#define IVBEP_U_MSR_PMON_RAW_EVENT_MASK	\
				(SNBEP_PMON_CTL_EV_SEL_MASK | \
				 SNBEP_PMON_CTL_UMASK_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_U_MSR_PMON_CTL_TRESH_MASK)
/* IVBEP Cbo */
#define IVBEP_CBO_MSR_PMON_RAW_EVENT_MASK		(IVBEP_PMON_RAW_EVENT_MASK | \
						 SNBEP_CBO_PMON_CTL_TID_EN)

#define IVBEP_CB0_MSR_PMON_BOX_FILTER_TID		(0x1fULL << 0)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_LINK	(0xfULL << 5)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_STATE	(0x3fULL << 17)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_NID		(0xffffULL << 32)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_OPC		(0x1ffULL << 52)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_C6		(0x1ULL << 61)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_NC		(0x1ULL << 62)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_ISOC	(0x1ULL << 63)

/* IVBEP home agent */
#define IVBEP_HA_PCI_PMON_CTL_Q_OCC_RST		(1 << 16)
#define IVBEP_HA_PCI_PMON_RAW_EVENT_MASK		\
				(IVBEP_PMON_RAW_EVENT_MASK | \
				 IVBEP_HA_PCI_PMON_CTL_Q_OCC_RST)
/* IVBEP PCU */
#define IVBEP_PCU_MSR_PMON_RAW_EVENT_MASK	\
				(SNBEP_PMON_CTL_EV_SEL_MASK | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_SEL_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_PCU_MSR_PMON_CTL_TRESH_MASK | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_INVERT | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_EDGE_DET)
/* IVBEP QPI */
#define IVBEP_QPI_PCI_PMON_RAW_EVENT_MASK	\
				(IVBEP_PMON_RAW_EVENT_MASK | \
				 SNBEP_PMON_CTL_EV_SEL_EXT)

#define __BITS_VALUE(x, i, n)  ((typeof(x))(((x) >> ((i) * (n))) & \
				((1ULL << (n)) - 1)))

/* Haswell-EP Ubox */
#define HSWEP_U_MSR_PMON_CTR0			0x709
#define HSWEP_U_MSR_PMON_CTL0			0x705
#define HSWEP_U_MSR_PMON_FILTER			0x707

#define HSWEP_U_MSR_PMON_UCLK_FIXED_CTL		0x703
#define HSWEP_U_MSR_PMON_UCLK_FIXED_CTR		0x704

#define HSWEP_U_MSR_PMON_BOX_FILTER_TID		(0x1 << 0)
#define HSWEP_U_MSR_PMON_BOX_FILTER_CID		(0x1fULL << 1)
#define HSWEP_U_MSR_PMON_BOX_FILTER_MASK \
					(HSWEP_U_MSR_PMON_BOX_FILTER_TID | \
					 HSWEP_U_MSR_PMON_BOX_FILTER_CID)

/* Haswell-EP CBo */
#define HSWEP_C0_MSR_PMON_CTR0			0xe08
#define HSWEP_C0_MSR_PMON_CTL0			0xe01
#define HSWEP_C0_MSR_PMON_BOX_CTL			0xe00
#define HSWEP_C0_MSR_PMON_BOX_FILTER0		0xe05
#define HSWEP_CBO_MSR_OFFSET			0x10


#define HSWEP_CB0_MSR_PMON_BOX_FILTER_TID		(0x3fULL << 0)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_LINK	(0xfULL << 6)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_STATE	(0x7fULL << 17)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_NID		(0xffffULL << 32)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_OPC		(0x1ffULL << 52)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_C6		(0x1ULL << 61)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_NC		(0x1ULL << 62)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_ISOC	(0x1ULL << 63)


/* Haswell-EP Sbox */
#define HSWEP_S0_MSR_PMON_CTR0			0x726
#define HSWEP_S0_MSR_PMON_CTL0			0x721
#define HSWEP_S0_MSR_PMON_BOX_CTL			0x720
#define HSWEP_SBOX_MSR_OFFSET			0xa
#define HSWEP_S_MSR_PMON_RAW_EVENT_MASK		(SNBEP_PMON_RAW_EVENT_MASK | \
						 SNBEP_CBO_PMON_CTL_TID_EN)

/* Haswell-EP PCU */
#define HSWEP_PCU_MSR_PMON_CTR0			0x717
#define HSWEP_PCU_MSR_PMON_CTL0			0x711
#define HSWEP_PCU_MSR_PMON_BOX_CTL		0x710
#define HSWEP_PCU_MSR_PMON_BOX_FILTER		0x715

/* KNL Ubox */
#define KNL_U_MSR_PMON_RAW_EVENT_MASK \
					(SNBEP_U_MSR_PMON_RAW_EVENT_MASK | \
						SNBEP_CBO_PMON_CTL_TID_EN)
/* KNL CHA */
#define KNL_CHA_MSR_OFFSET			0xc
#define KNL_CHA_MSR_PMON_CTL_QOR		(1 << 16)
#define KNL_CHA_MSR_PMON_RAW_EVENT_MASK \
					(SNBEP_CBO_MSR_PMON_RAW_EVENT_MASK | \
					 KNL_CHA_MSR_PMON_CTL_QOR)
#define KNL_CHA_MSR_PMON_BOX_FILTER_TID		0x1ff
#define KNL_CHA_MSR_PMON_BOX_FILTER_STATE	(7 << 18)
#define KNL_CHA_MSR_PMON_BOX_FILTER_OP		(0xfffffe2aULL << 32)
#define KNL_CHA_MSR_PMON_BOX_FILTER_REMOTE_NODE	(0x1ULL << 32)
#define KNL_CHA_MSR_PMON_BOX_FILTER_LOCAL_NODE	(0x1ULL << 33)
#define KNL_CHA_MSR_PMON_BOX_FILTER_NNC		(0x1ULL << 37)

/* KNL EDC/MC UCLK */
#define KNL_UCLK_MSR_PMON_CTR0_LOW		0x400
#define KNL_UCLK_MSR_PMON_CTL0			0x420
#define KNL_UCLK_MSR_PMON_BOX_CTL		0x430
#define KNL_UCLK_MSR_PMON_UCLK_FIXED_LOW	0x44c
#define KNL_UCLK_MSR_PMON_UCLK_FIXED_CTL	0x454
#define KNL_PMON_FIXED_CTL_EN			0x1

/* KNL EDC */
#define KNL_EDC0_ECLK_MSR_PMON_CTR0_LOW		0xa00
#define KNL_EDC0_ECLK_MSR_PMON_CTL0		0xa20
#define KNL_EDC0_ECLK_MSR_PMON_BOX_CTL		0xa30
#define KNL_EDC0_ECLK_MSR_PMON_ECLK_FIXED_LOW	0xa3c
#define KNL_EDC0_ECLK_MSR_PMON_ECLK_FIXED_CTL	0xa44

/* KNL MC */
#define KNL_MC0_CH0_MSR_PMON_CTR0_LOW		0xb00
#define KNL_MC0_CH0_MSR_PMON_CTL0		0xb20
#define KNL_MC0_CH0_MSR_PMON_BOX_CTL		0xb30
#define KNL_MC0_CH0_MSR_PMON_FIXED_LOW		0xb3c
#define KNL_MC0_CH0_MSR_PMON_FIXED_CTL		0xb44

/* KNL IRP */
#define KNL_IRP_PCI_PMON_BOX_CTL		0xf0
#define KNL_IRP_PCI_PMON_RAW_EVENT_MASK		(SNBEP_PMON_RAW_EVENT_MASK | \
						 KNL_CHA_MSR_PMON_CTL_QOR)
/* KNL PCU */
#define KNL_PCU_PMON_CTL_EV_SEL_MASK		0x0000007f
#define KNL_PCU_PMON_CTL_USE_OCC_CTR		(1 << 7)
#define KNL_PCU_MSR_PMON_CTL_TRESH_MASK		0x3f000000
#define KNL_PCU_MSR_PMON_RAW_EVENT_MASK	\
				(KNL_PCU_PMON_CTL_EV_SEL_MASK | \
				 KNL_PCU_PMON_CTL_USE_OCC_CTR | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_SEL_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_CBO_PMON_CTL_TID_EN | \
				 SNBEP_PMON_CTL_INVERT | \
				 KNL_PCU_MSR_PMON_CTL_TRESH_MASK | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_INVERT | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_EDGE_DET)

/* SKX pci bus to socket mapping */
#define SKX_CPUNODEID			0xc0
#define SKX_GIDNIDMAP			0xd4

/*
 * The CPU_BUS_NUMBER MSR returns the values of the respective CPUBUSNO CSR
 * that BIOS programmed. MSR has package scope.
 * |  Bit  |  Default  |  Description
 * | [63]  |    00h    | VALID - When set, indicates the CPU bus
 *                       numbers have been initialized. (RO)
 * |[62:48]|    ---    | Reserved
 * |[47:40]|    00h    | BUS_NUM_5 - Return the bus number BIOS assigned
 *                       CPUBUSNO(5). (RO)
 * |[39:32]|    00h    | BUS_NUM_4 - Return the bus number BIOS assigned
 *                       CPUBUSNO(4). (RO)
 * |[31:24]|    00h    | BUS_NUM_3 - Return the bus number BIOS assigned
 *                       CPUBUSNO(3). (RO)
 * |[23:16]|    00h    | BUS_NUM_2 - Return the bus number BIOS assigned
 *                       CPUBUSNO(2). (RO)
 * |[15:8] |    00h    | BUS_NUM_1 - Return the bus number BIOS assigned
 *                       CPUBUSNO(1). (RO)
 * | [7:0] |    00h    | BUS_NUM_0 - Return the bus number BIOS assigned
 *                       CPUBUSNO(0). (RO)
 */
#define SKX_MSR_CPU_BUS_NUMBER		0x300
#define SKX_MSR_CPU_BUS_VALID_BIT	(1ULL << 63)
#define BUS_NUM_STRIDE			8

/* SKX CHA */
#define SKX_CHA_MSR_PMON_BOX_FILTER_TID		(0x1ffULL << 0)
#define SKX_CHA_MSR_PMON_BOX_FILTER_LINK	(0xfULL << 9)
#define SKX_CHA_MSR_PMON_BOX_FILTER_STATE	(0x3ffULL << 17)
#define SKX_CHA_MSR_PMON_BOX_FILTER_REM		(0x1ULL << 32)
#define SKX_CHA_MSR_PMON_BOX_FILTER_LOC		(0x1ULL << 33)
#define SKX_CHA_MSR_PMON_BOX_FILTER_ALL_OPC	(0x1ULL << 35)
#define SKX_CHA_MSR_PMON_BOX_FILTER_NM		(0x1ULL << 36)
#define SKX_CHA_MSR_PMON_BOX_FILTER_NOT_NM	(0x1ULL << 37)
#define SKX_CHA_MSR_PMON_BOX_FILTER_OPC0	(0x3ffULL << 41)
#define SKX_CHA_MSR_PMON_BOX_FILTER_OPC1	(0x3ffULL << 51)
#define SKX_CHA_MSR_PMON_BOX_FILTER_C6		(0x1ULL << 61)
#define SKX_CHA_MSR_PMON_BOX_FILTER_NC		(0x1ULL << 62)
#define SKX_CHA_MSR_PMON_BOX_FILTER_ISOC	(0x1ULL << 63)

/* SKX IIO */
#define SKX_IIO0_MSR_PMON_CTL0		0xa48
#define SKX_IIO0_MSR_PMON_CTR0		0xa41
#define SKX_IIO0_MSR_PMON_BOX_CTL	0xa40
#define SKX_IIO_MSR_OFFSET		0x20

#define SKX_PMON_CTL_TRESH_MASK		(0xff << 24)
#define SKX_PMON_CTL_TRESH_MASK_EXT	(0xf)
#define SKX_PMON_CTL_CH_MASK		(0xff << 4)
#define SKX_PMON_CTL_FC_MASK		(0x7 << 12)
#define SKX_IIO_PMON_RAW_EVENT_MASK	(SNBEP_PMON_CTL_EV_SEL_MASK | \
					 SNBEP_PMON_CTL_UMASK_MASK | \
					 SNBEP_PMON_CTL_EDGE_DET | \
					 SNBEP_PMON_CTL_INVERT | \
					 SKX_PMON_CTL_TRESH_MASK)
#define SKX_IIO_PMON_RAW_EVENT_MASK_EXT	(SKX_PMON_CTL_TRESH_MASK_EXT | \
					 SKX_PMON_CTL_CH_MASK | \
					 SKX_PMON_CTL_FC_MASK)

/* SKX IRP */
#define SKX_IRP0_MSR_PMON_CTL0		0xa5b
#define SKX_IRP0_MSR_PMON_CTR0		0xa59
#define SKX_IRP0_MSR_PMON_BOX_CTL	0xa58
#define SKX_IRP_MSR_OFFSET		0x20

/* SKX UPI */
#define SKX_UPI_PCI_PMON_CTL0		0x350
#define SKX_UPI_PCI_PMON_CTR0		0x318
#define SKX_UPI_PCI_PMON_BOX_CTL	0x378
#define SKX_UPI_CTL_UMASK_EXT		0xffefff

/* SKX M2M */
#define SKX_M2M_PCI_PMON_CTL0		0x228
#define SKX_M2M_PCI_PMON_CTR0		0x200
#define SKX_M2M_PCI_PMON_BOX_CTL	0x258

/* Memory Map registers device ID */
#define SNR_ICX_MESH2IIO_MMAP_DID		0x9a2
#define SNR_ICX_SAD_CONTROL_CFG		0x3f4

/* Getting I/O stack id in SAD_COTROL_CFG notation */
#define SAD_CONTROL_STACK_ID(data)		(((data) >> 4) & 0x7)

/* SNR Ubox */
#define SNR_U_MSR_PMON_CTR0			0x1f98
#define SNR_U_MSR_PMON_CTL0			0x1f91
#define SNR_U_MSR_PMON_UCLK_FIXED_CTL		0x1f93
#define SNR_U_MSR_PMON_UCLK_FIXED_CTR		0x1f94

/* SNR CHA */
#define SNR_CHA_RAW_EVENT_MASK_EXT		0x3ffffff
#define SNR_CHA_MSR_PMON_CTL0			0x1c01
#define SNR_CHA_MSR_PMON_CTR0			0x1c08
#define SNR_CHA_MSR_PMON_BOX_CTL		0x1c00
#define SNR_C0_MSR_PMON_BOX_FILTER0		0x1c05


/* SNR IIO */
#define SNR_IIO_MSR_PMON_CTL0			0x1e08
#define SNR_IIO_MSR_PMON_CTR0			0x1e01
#define SNR_IIO_MSR_PMON_BOX_CTL		0x1e00
#define SNR_IIO_MSR_OFFSET			0x10
#define SNR_IIO_PMON_RAW_EVENT_MASK_EXT		0x7ffff

/* SNR IRP */
#define SNR_IRP0_MSR_PMON_CTL0			0x1ea8
#define SNR_IRP0_MSR_PMON_CTR0			0x1ea1
#define SNR_IRP0_MSR_PMON_BOX_CTL		0x1ea0
#define SNR_IRP_MSR_OFFSET			0x10

/* SNR M2PCIE */
#define SNR_M2PCIE_MSR_PMON_CTL0		0x1e58
#define SNR_M2PCIE_MSR_PMON_CTR0		0x1e51
#define SNR_M2PCIE_MSR_PMON_BOX_CTL		0x1e50
#define SNR_M2PCIE_MSR_OFFSET			0x10

/* SNR PCU */
#define SNR_PCU_MSR_PMON_CTL0			0x1ef1
#define SNR_PCU_MSR_PMON_CTR0			0x1ef8
#define SNR_PCU_MSR_PMON_BOX_CTL		0x1ef0
#define SNR_PCU_MSR_PMON_BOX_FILTER		0x1efc

/* SNR M2M */
#define SNR_M2M_PCI_PMON_CTL0			0x468
#define SNR_M2M_PCI_PMON_CTR0			0x440
#define SNR_M2M_PCI_PMON_BOX_CTL		0x438
#define SNR_M2M_PCI_PMON_UMASK_EXT		0xff

/* SNR PCIE3 */
#define SNR_PCIE3_PCI_PMON_CTL0			0x508
#define SNR_PCIE3_PCI_PMON_CTR0			0x4e8
#define SNR_PCIE3_PCI_PMON_BOX_CTL		0x4e0

/* SNR IMC */
#define SNR_IMC_MMIO_PMON_FIXED_CTL		0x54
#define SNR_IMC_MMIO_PMON_FIXED_CTR		0x38
#define SNR_IMC_MMIO_PMON_CTL0			0x40
#define SNR_IMC_MMIO_PMON_CTR0			0x8
#define SNR_IMC_MMIO_PMON_BOX_CTL		0x22800
#define SNR_IMC_MMIO_OFFSET			0x4000
#define SNR_IMC_MMIO_SIZE			0x4000
#define SNR_IMC_MMIO_BASE_OFFSET		0xd0
#define SNR_IMC_MMIO_BASE_MASK			0x1FFFFFFF
#define SNR_IMC_MMIO_MEM0_OFFSET		0xd8
#define SNR_IMC_MMIO_MEM0_MASK			0x7FF

/* ICX CHA */
#define ICX_C34_MSR_PMON_CTR0			0xb68
#define ICX_C34_MSR_PMON_CTL0			0xb61
#define ICX_C34_MSR_PMON_BOX_CTL		0xb60
#define ICX_C34_MSR_PMON_BOX_FILTER0		0xb65

/* ICX IIO */
#define ICX_IIO_MSR_PMON_CTL0			0xa58
#define ICX_IIO_MSR_PMON_CTR0			0xa51
#define ICX_IIO_MSR_PMON_BOX_CTL		0xa50

/* ICX IRP */
#define ICX_IRP0_MSR_PMON_CTL0			0xa4d
#define ICX_IRP0_MSR_PMON_CTR0			0xa4b
#define ICX_IRP0_MSR_PMON_BOX_CTL		0xa4a

/* ICX M2PCIE */
#define ICX_M2PCIE_MSR_PMON_CTL0		0xa46
#define ICX_M2PCIE_MSR_PMON_CTR0		0xa41
#define ICX_M2PCIE_MSR_PMON_BOX_CTL		0xa40

/* ICX UPI */
#define ICX_UPI_PCI_PMON_CTL0			0x350
#define ICX_UPI_PCI_PMON_CTR0			0x320
#define ICX_UPI_PCI_PMON_BOX_CTL		0x318
#define ICX_UPI_CTL_UMASK_EXT			0xffffff

/* ICX M3UPI*/
#define ICX_M3UPI_PCI_PMON_CTL0			0xd8
#define ICX_M3UPI_PCI_PMON_CTR0			0xa8
#define ICX_M3UPI_PCI_PMON_BOX_CTL		0xa0

/* ICX IMC */
#define ICX_NUMBER_IMC_CHN			3
#define ICX_IMC_MEM_STRIDE			0x4

/* SPR */
#define SPR_RAW_EVENT_MASK_EXT			0xffffff

/* SPR CHA */
#define SPR_CHA_PMON_CTL_TID_EN			(1 << 16)
#define SPR_CHA_PMON_EVENT_MASK			(SNBEP_PMON_RAW_EVENT_MASK | \
						 SPR_CHA_PMON_CTL_TID_EN)
#define SPR_CHA_PMON_BOX_FILTER_TID		0x3ff

#define SPR_C0_MSR_PMON_BOX_FILTER0		0x200e

DEFINE_UNCORE_FORMAT_ATTR(event, event, "config:0-7");
DEFINE_UNCORE_FORMAT_ATTR(event2, event, "config:0-6");
DEFINE_UNCORE_FORMAT_ATTR(event_ext, event, "config:0-7,21");
DEFINE_UNCORE_FORMAT_ATTR(use_occ_ctr, use_occ_ctr, "config:7");
DEFINE_UNCORE_FORMAT_ATTR(umask, umask, "config:8-15");
DEFINE_UNCORE_FORMAT_ATTR(umask_ext, umask, "config:8-15,32-43,45-55");
DEFINE_UNCORE_FORMAT_ATTR(umask_ext2, umask, "config:8-15,32-57");
DEFINE_UNCORE_FORMAT_ATTR(umask_ext3, umask, "config:8-15,32-39");
DEFINE_UNCORE_FORMAT_ATTR(umask_ext4, umask, "config:8-15,32-55");
DEFINE_UNCORE_FORMAT_ATTR(qor, qor, "config:16");
DEFINE_UNCORE_FORMAT_ATTR(edge, edge, "config:18");
DEFINE_UNCORE_FORMAT_ATTR(tid_en, tid_en, "config:19");
DEFINE_UNCORE_FORMAT_ATTR(tid_en2, tid_en, "config:16");
DEFINE_UNCORE_FORMAT_ATTR(inv, inv, "config:23");
DEFINE_UNCORE_FORMAT_ATTR(thresh9, thresh, "config:24-35");
DEFINE_UNCORE_FORMAT_ATTR(thresh8, thresh, "config:24-31");
DEFINE_UNCORE_FORMAT_ATTR(thresh6, thresh, "config:24-29");
DEFINE_UNCORE_FORMAT_ATTR(thresh5, thresh, "config:24-28");
DEFINE_UNCORE_FORMAT_ATTR(occ_sel, occ_sel, "config:14-15");
DEFINE_UNCORE_FORMAT_ATTR(occ_invert, occ_invert, "config:30");
DEFINE_UNCORE_FORMAT_ATTR(occ_edge, occ_edge, "config:14-51");
DEFINE_UNCORE_FORMAT_ATTR(occ_edge_det, occ_edge_det, "config:31");
DEFINE_UNCORE_FORMAT_ATTR(ch_mask, ch_mask, "config:36-43");
DEFINE_UNCORE_FORMAT_ATTR(ch_mask2, ch_mask, "config:36-47");
DEFINE_UNCORE_FORMAT_ATTR(fc_mask, fc_mask, "config:44-46");
DEFINE_UNCORE_FORMAT_ATTR(fc_mask2, fc_mask, "config:48-50");
DEFINE_UNCORE_FORMAT_ATTR(filter_tid, filter_tid, "config1:0-4");
DEFINE_UNCORE_FORMAT_ATTR(filter_tid2, filter_tid, "config1:0");
DEFINE_UNCORE_FORMAT_ATTR(filter_tid3, filter_tid, "config1:0-5");
DEFINE_UNCORE_FORMAT_ATTR(filter_tid4, filter_tid, "config1:0-8");
DEFINE_UNCORE_FORMAT_ATTR(filter_tid5, filter_tid, "config1:0-9");
DEFINE_UNCORE_FORMAT_ATTR(filter_cid, filter_cid, "config1:5");
DEFINE_UNCORE_FORMAT_ATTR(filter_link, filter_link, "config1:5-8");
DEFINE_UNCORE_FORMAT_ATTR(filter_link2, filter_link, "config1:6-8");
DEFINE_UNCORE_FORMAT_ATTR(filter_link3, filter_link, "config1:12");
DEFINE_UNCORE_FORMAT_ATTR(filter_nid, filter_nid, "config1:10-17");
DEFINE_UNCORE_FORMAT_ATTR(filter_nid2, filter_nid, "config1:32-47");
DEFINE_UNCORE_FORMAT_ATTR(filter_state, filter_state, "config1:18-22");
DEFINE_UNCORE_FORMAT_ATTR(filter_state2, filter_state, "config1:17-22");
DEFINE_UNCORE_FORMAT_ATTR(filter_state3, filter_state, "config1:17-23");
DEFINE_UNCORE_FORMAT_ATTR(filter_state4, filter_state, "config1:18-20");
DEFINE_UNCORE_FORMAT_ATTR(filter_state5, filter_state, "config1:17-26");
DEFINE_UNCORE_FORMAT_ATTR(filter_rem, filter_rem, "config1:32");
DEFINE_UNCORE_FORMAT_ATTR(filter_loc, filter_loc, "config1:33");
DEFINE_UNCORE_FORMAT_ATTR(filter_nm, filter_nm, "config1:36");
DEFINE_UNCORE_FORMAT_ATTR(filter_not_nm, filter_not_nm, "config1:37");
DEFINE_UNCORE_FORMAT_ATTR(filter_local, filter_local, "config1:33");
DEFINE_UNCORE_FORMAT_ATTR(filter_all_op, filter_all_op, "config1:35");
DEFINE_UNCORE_FORMAT_ATTR(filter_nnm, filter_nnm, "config1:37");
DEFINE_UNCORE_FORMAT_ATTR(filter_opc, filter_opc, "config1:23-31");
DEFINE_UNCORE_FORMAT_ATTR(filter_opc2, filter_opc, "config1:52-60");
DEFINE_UNCORE_FORMAT_ATTR(filter_opc3, filter_opc, "config1:41-60");
DEFINE_UNCORE_FORMAT_ATTR(filter_opc_0, filter_opc0, "config1:41-50");
DEFINE_UNCORE_FORMAT_ATTR(filter_opc_1, filter_opc1, "config1:51-60");
DEFINE_UNCORE_FORMAT_ATTR(filter_nc, filter_nc, "config1:62");
DEFINE_UNCORE_FORMAT_ATTR(filter_c6, filter_c6, "config1:61");
DEFINE_UNCORE_FORMAT_ATTR(filter_isoc, filter_isoc, "config1:63");
DEFINE_UNCORE_FORMAT_ATTR(filter_band0, filter_band0, "config1:0-7");
DEFINE_UNCORE_FORMAT_ATTR(filter_band1, filter_band1, "config1:8-15");
DEFINE_UNCORE_FORMAT_ATTR(filter_band2, filter_band2, "config1:16-23");
DEFINE_UNCORE_FORMAT_ATTR(filter_band3, filter_band3, "config1:24-31");
DEFINE_UNCORE_FORMAT_ATTR(match_rds, match_rds, "config1:48-51");
DEFINE_UNCORE_FORMAT_ATTR(match_rnid30, match_rnid30, "config1:32-35");
DEFINE_UNCORE_FORMAT_ATTR(match_rnid4, match_rnid4, "config1:31");
DEFINE_UNCORE_FORMAT_ATTR(match_dnid, match_dnid, "config1:13-17");
DEFINE_UNCORE_FORMAT_ATTR(match_mc, match_mc, "config1:9-12");
DEFINE_UNCORE_FORMAT_ATTR(match_opc, match_opc, "config1:5-8");
DEFINE_UNCORE_FORMAT_ATTR(match_vnw, match_vnw, "config1:3-4");
DEFINE_UNCORE_FORMAT_ATTR(match0, match0, "config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(match1, match1, "config1:32-63");
DEFINE_UNCORE_FORMAT_ATTR(mask_rds, mask_rds, "config2:48-51");
DEFINE_UNCORE_FORMAT_ATTR(mask_rnid30, mask_rnid30, "config2:32-35");
DEFINE_UNCORE_FORMAT_ATTR(mask_rnid4, mask_rnid4, "config2:31");
DEFINE_UNCORE_FORMAT_ATTR(mask_dnid, mask_dnid, "config2:13-17");
DEFINE_UNCORE_FORMAT_ATTR(mask_mc, mask_mc, "config2:9-12");
DEFINE_UNCORE_FORMAT_ATTR(mask_opc, mask_opc, "config2:5-8");
DEFINE_UNCORE_FORMAT_ATTR(mask_vnw, mask_vnw, "config2:3-4");
DEFINE_UNCORE_FORMAT_ATTR(mask0, mask0, "config2:0-31");
DEFINE_UNCORE_FORMAT_ATTR(mask1, mask1, "config2:32-63");

static void snbep_uncore_pci_disable_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);
	u32 config = 0;

	if (!pci_read_config_dword(pdev, box_ctl, &config)) {
		config |= SNBEP_PMON_BOX_CTL_FRZ;
		pci_write_config_dword(pdev, box_ctl, config);
	}
}

static void snbep_uncore_pci_enable_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);
	u32 config = 0;

	if (!pci_read_config_dword(pdev, box_ctl, &config)) {
		config &= ~SNBEP_PMON_BOX_CTL_FRZ;
		pci_write_config_dword(pdev, box_ctl, config);
	}
}

static void snbep_uncore_pci_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static void snbep_uncore_pci_disable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, hwc->config_base, hwc->config);
}

static u64 snbep_uncore_pci_read_counter(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;
	u64 count = 0;

	pci_read_config_dword(pdev, hwc->event_base, (u32 *)&count);
	pci_read_config_dword(pdev, hwc->event_base + 4, (u32 *)&count + 1);

	return count;
}

static void snbep_uncore_pci_init_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);

	pci_write_config_dword(pdev, box_ctl, SNBEP_PMON_BOX_CTL_INT);
}

static void snbep_uncore_msr_disable_box(struct intel_uncore_box *box)
{
	u64 config;
	unsigned msr;

	msr = uncore_msr_box_ctl(box);
	if (msr) {
		rdmsrl(msr, config);
		config |= SNBEP_PMON_BOX_CTL_FRZ;
		wrmsrl(msr, config);
	}
}

static void snbep_uncore_msr_enable_box(struct intel_uncore_box *box)
{
	u64 config;
	unsigned msr;

	msr = uncore_msr_box_ctl(box);
	if (msr) {
		rdmsrl(msr, config);
		config &= ~SNBEP_PMON_BOX_CTL_FRZ;
		wrmsrl(msr, config);
	}
}

static void snbep_uncore_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;

	if (reg1->idx != EXTRA_REG_NONE)
		wrmsrl(reg1->reg, uncore_shared_reg_config(box, 0));

	wrmsrl(hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static void snbep_uncore_msr_disable_event(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base, hwc->config);
}

static void snbep_uncore_msr_init_box(struct intel_uncore_box *box)
{
	unsigned msr = uncore_msr_box_ctl(box);

	if (msr)
		wrmsrl(msr, SNBEP_PMON_BOX_CTL_INT);
}

static struct attribute *snbep_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static struct attribute *snbep_uncore_ubox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	NULL,
};

static struct attribute *snbep_uncore_cbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid.attr,
	&format_attr_filter_nid.attr,
	&format_attr_filter_state.attr,
	&format_attr_filter_opc.attr,
	NULL,
};

static struct attribute *snbep_uncore_pcu_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_occ_sel.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	&format_attr_occ_invert.attr,
	&format_attr_occ_edge.attr,
	&format_attr_filter_band0.attr,
	&format_attr_filter_band1.attr,
	&format_attr_filter_band2.attr,
	&format_attr_filter_band3.attr,
	NULL,
};

static struct attribute *snbep_uncore_qpi_formats_attr[] = {
	&format_attr_event_ext.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_match_rds.attr,
	&format_attr_match_rnid30.attr,
	&format_attr_match_rnid4.attr,
	&format_attr_match_dnid.attr,
	&format_attr_match_mc.attr,
	&format_attr_match_opc.attr,
	&format_attr_match_vnw.attr,
	&format_attr_match0.attr,
	&format_attr_match1.attr,
	&format_attr_mask_rds.attr,
	&format_attr_mask_rnid30.attr,
	&format_attr_mask_rnid4.attr,
	&format_attr_mask_dnid.attr,
	&format_attr_mask_mc.attr,
	&format_attr_mask_opc.attr,
	&format_attr_mask_vnw.attr,
	&format_attr_mask0.attr,
	&format_attr_mask1.attr,
	NULL,
};

static struct uncore_event_desc snbep_uncore_imc_events[] = {
	INTEL_UNCORE_EVENT_DESC(clockticks,      "event=0xff,umask=0x00"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read,  "event=0x04,umask=0x03"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read.scale, "6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read.unit, "MiB"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write, "event=0x04,umask=0x0c"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write.scale, "6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write.unit, "MiB"),
	{ /* end: all zeroes */ },
};

static struct uncore_event_desc snbep_uncore_qpi_events[] = {
	INTEL_UNCORE_EVENT_DESC(clockticks,       "event=0x14"),
	INTEL_UNCORE_EVENT_DESC(txl_flits_active, "event=0x00,umask=0x06"),
	INTEL_UNCORE_EVENT_DESC(drs_data,         "event=0x102,umask=0x08"),
	INTEL_UNCORE_EVENT_DESC(ncb_data,         "event=0x103,umask=0x04"),
	{ /* end: all zeroes */ },
};

static const struct attribute_group snbep_uncore_format_group = {
	.name = "format",
	.attrs = snbep_uncore_formats_attr,
};

static const struct attribute_group snbep_uncore_ubox_format_group = {
	.name = "format",
	.attrs = snbep_uncore_ubox_formats_attr,
};

static const struct attribute_group snbep_uncore_cbox_format_group = {
	.name = "format",
	.attrs = snbep_uncore_cbox_formats_attr,
};

static const struct attribute_group snbep_uncore_pcu_format_group = {
	.name = "format",
	.attrs = snbep_uncore_pcu_formats_attr,
};

static const struct attribute_group snbep_uncore_qpi_format_group = {
	.name = "format",
	.attrs = snbep_uncore_qpi_formats_attr,
};

#define __SNBEP_UNCORE_MSR_OPS_COMMON_INIT()			\
	.disable_box	= snbep_uncore_msr_disable_box,		\
	.enable_box	= snbep_uncore_msr_enable_box,		\
	.disable_event	= snbep_uncore_msr_disable_event,	\
	.enable_event	= snbep_uncore_msr_enable_event,	\
	.read_counter	= uncore_msr_read_counter

#define SNBEP_UNCORE_MSR_OPS_COMMON_INIT()			\
	__SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),			\
	.init_box	= snbep_uncore_msr_init_box		\

static struct intel_uncore_ops snbep_uncore_msr_ops = {
	SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
};

#define SNBEP_UNCORE_PCI_OPS_COMMON_INIT()			\
	.init_box	= snbep_uncore_pci_init_box,		\
	.disable_box	= snbep_uncore_pci_disable_box,		\
	.enable_box	= snbep_uncore_pci_enable_box,		\
	.disable_event	= snbep_uncore_pci_disable_event,	\
	.read_counter	= snbep_uncore_pci_read_counter

static struct intel_uncore_ops snbep_uncore_pci_ops = {
	SNBEP_UNCORE_PCI_OPS_COMMON_INIT(),
	.enable_event	= snbep_uncore_pci_enable_event,	\
};

static struct event_constraint snbep_uncore_cbox_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x01, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x02, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x04, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x05, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x07, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x09, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x12, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x1b, 0xc),
	UNCORE_EVENT_CONSTRAINT(0x1c, 0xc),
	UNCORE_EVENT_CONSTRAINT(0x1d, 0xc),
	UNCORE_EVENT_CONSTRAINT(0x1e, 0xc),
	UNCORE_EVENT_CONSTRAINT(0x1f, 0xe),
	UNCORE_EVENT_CONSTRAINT(0x21, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x31, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x35, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x37, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x38, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x39, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x3b, 0x1),
	EVENT_CONSTRAINT_END
};

static struct event_constraint snbep_uncore_r2pcie_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x12, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x24, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	EVENT_CONSTRAINT_END
};

static struct event_constraint snbep_uncore_r3qpi_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x12, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x20, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x21, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x22, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x24, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x28, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x29, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2a, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2b, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2c, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2d, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2e, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2f, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x30, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x31, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x37, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x38, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x39, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type snbep_uncore_ubox = {
	.name		= "ubox",
	.num_counters   = 2,
	.num_boxes	= 1,
	.perf_ctr_bits	= 44,
	.fixed_ctr_bits	= 48,
	.perf_ctr	= SNBEP_U_MSR_PMON_CTR0,
	.event_ctl	= SNBEP_U_MSR_PMON_CTL0,
	.event_mask	= SNBEP_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr	= SNBEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl	= SNBEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.ops		= &snbep_uncore_msr_ops,
	.format_group	= &snbep_uncore_ubox_format_group,
};

static struct extra_reg snbep_uncore_cbox_extra_regs[] = {
	SNBEP_CBO_EVENT_EXTRA_REG(SNBEP_CBO_PMON_CTL_TID_EN,
				  SNBEP_CBO_PMON_CTL_TID_EN, 0x1),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0334, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4334, 0xffff, 0x6),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0534, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4534, 0xffff, 0x6),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0934, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4934, 0xffff, 0x6),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4134, 0xffff, 0x6),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0135, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0335, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4135, 0xffff, 0xa),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4335, 0xffff, 0xa),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4435, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4835, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a35, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5035, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0136, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0336, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4136, 0xffff, 0xa),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4336, 0xffff, 0xa),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4436, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4836, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a36, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4037, 0x40ff, 0x2),
	EVENT_EXTRA_END
};

static void snbep_cbox_put_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct intel_uncore_extra_reg *er = &box->shared_regs[0];
	int i;

	if (uncore_box_is_fake(box))
		return;

	for (i = 0; i < 5; i++) {
		if (reg1->alloc & (0x1 << i))
			atomic_sub(1 << (i * 6), &er->ref);
	}
	reg1->alloc = 0;
}

static struct event_constraint *
__snbep_cbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event,
			    u64 (*cbox_filter_mask)(int fields))
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct intel_uncore_extra_reg *er = &box->shared_regs[0];
	int i, alloc = 0;
	unsigned long flags;
	u64 mask;

	if (reg1->idx == EXTRA_REG_NONE)
		return NULL;

	raw_spin_lock_irqsave(&er->lock, flags);
	for (i = 0; i < 5; i++) {
		if (!(reg1->idx & (0x1 << i)))
			continue;
		if (!uncore_box_is_fake(box) && (reg1->alloc & (0x1 << i)))
			continue;

		mask = cbox_filter_mask(0x1 << i);
		if (!__BITS_VALUE(atomic_read(&er->ref), i, 6) ||
		    !((reg1->config ^ er->config) & mask)) {
			atomic_add(1 << (i * 6), &er->ref);
			er->config &= ~mask;
			er->config |= reg1->config & mask;
			alloc |= (0x1 << i);
		} else {
			break;
		}
	}
	raw_spin_unlock_irqrestore(&er->lock, flags);
	if (i < 5)
		goto fail;

	if (!uncore_box_is_fake(box))
		reg1->alloc |= alloc;

	return NULL;
fail:
	for (; i >= 0; i--) {
		if (alloc & (0x1 << i))
			atomic_sub(1 << (i * 6), &er->ref);
	}
	return &uncore_constraint_empty;
}

static u64 snbep_cbox_filter_mask(int fields)
{
	u64 mask = 0;

	if (fields & 0x1)
		mask |= SNBEP_CB0_MSR_PMON_BOX_FILTER_TID;
	if (fields & 0x2)
		mask |= SNBEP_CB0_MSR_PMON_BOX_FILTER_NID;
	if (fields & 0x4)
		mask |= SNBEP_CB0_MSR_PMON_BOX_FILTER_STATE;
	if (fields & 0x8)
		mask |= SNBEP_CB0_MSR_PMON_BOX_FILTER_OPC;

	return mask;
}

static struct event_constraint *
snbep_cbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	return __snbep_cbox_get_constraint(box, event, snbep_cbox_filter_mask);
}

static int snbep_cbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct extra_reg *er;
	int idx = 0;

	for (er = snbep_uncore_cbox_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		idx |= er->idx;
	}

	if (idx) {
		reg1->reg = SNBEP_C0_MSR_PMON_BOX_FILTER +
			SNBEP_CBO_MSR_OFFSET * box->pmu->pmu_idx;
		reg1->config = event->attr.config1 & snbep_cbox_filter_mask(idx);
		reg1->idx = idx;
	}
	return 0;
}

static struct intel_uncore_ops snbep_uncore_cbox_ops = {
	SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= snbep_cbox_hw_config,
	.get_constraint		= snbep_cbox_get_constraint,
	.put_constraint		= snbep_cbox_put_constraint,
};

static struct intel_uncore_type snbep_uncore_cbox = {
	.name			= "cbox",
	.num_counters		= 4,
	.num_boxes		= 8,
	.perf_ctr_bits		= 44,
	.event_ctl		= SNBEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= SNBEP_C0_MSR_PMON_CTR0,
	.event_mask		= SNBEP_CBO_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= SNBEP_CBO_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= snbep_uncore_cbox_constraints,
	.ops			= &snbep_uncore_cbox_ops,
	.format_group		= &snbep_uncore_cbox_format_group,
};

static u64 snbep_pcu_alter_er(struct perf_event *event, int new_idx, bool modify)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	u64 config = reg1->config;

	if (new_idx > reg1->idx)
		config <<= 8 * (new_idx - reg1->idx);
	else
		config >>= 8 * (reg1->idx - new_idx);

	if (modify) {
		hwc->config += new_idx - reg1->idx;
		reg1->config = config;
		reg1->idx = new_idx;
	}
	return config;
}

static struct event_constraint *
snbep_pcu_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct intel_uncore_extra_reg *er = &box->shared_regs[0];
	unsigned long flags;
	int idx = reg1->idx;
	u64 mask, config1 = reg1->config;
	bool ok = false;

	if (reg1->idx == EXTRA_REG_NONE ||
	    (!uncore_box_is_fake(box) && reg1->alloc))
		return NULL;
again:
	mask = 0xffULL << (idx * 8);
	raw_spin_lock_irqsave(&er->lock, flags);
	if (!__BITS_VALUE(atomic_read(&er->ref), idx, 8) ||
	    !((config1 ^ er->config) & mask)) {
		atomic_add(1 << (idx * 8), &er->ref);
		er->config &= ~mask;
		er->config |= config1 & mask;
		ok = true;
	}
	raw_spin_unlock_irqrestore(&er->lock, flags);

	if (!ok) {
		idx = (idx + 1) % 4;
		if (idx != reg1->idx) {
			config1 = snbep_pcu_alter_er(event, idx, false);
			goto again;
		}
		return &uncore_constraint_empty;
	}

	if (!uncore_box_is_fake(box)) {
		if (idx != reg1->idx)
			snbep_pcu_alter_er(event, idx, true);
		reg1->alloc = 1;
	}
	return NULL;
}

static void snbep_pcu_put_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct intel_uncore_extra_reg *er = &box->shared_regs[0];

	if (uncore_box_is_fake(box) || !reg1->alloc)
		return;

	atomic_sub(1 << (reg1->idx * 8), &er->ref);
	reg1->alloc = 0;
}

static int snbep_pcu_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	int ev_sel = hwc->config & SNBEP_PMON_CTL_EV_SEL_MASK;

	if (ev_sel >= 0xb && ev_sel <= 0xe) {
		reg1->reg = SNBEP_PCU_MSR_PMON_BOX_FILTER;
		reg1->idx = ev_sel - 0xb;
		reg1->config = event->attr.config1 & (0xff << (reg1->idx * 8));
	}
	return 0;
}

static struct intel_uncore_ops snbep_uncore_pcu_ops = {
	SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= snbep_pcu_hw_config,
	.get_constraint		= snbep_pcu_get_constraint,
	.put_constraint		= snbep_pcu_put_constraint,
};

static struct intel_uncore_type snbep_uncore_pcu = {
	.name			= "pcu",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCU_MSR_PMON_CTR0,
	.event_ctl		= SNBEP_PCU_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PCU_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCU_MSR_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &snbep_uncore_pcu_ops,
	.format_group		= &snbep_uncore_pcu_format_group,
};

static struct intel_uncore_type *snbep_msr_uncores[] = {
	&snbep_uncore_ubox,
	&snbep_uncore_cbox,
	&snbep_uncore_pcu,
	NULL,
};

void snbep_uncore_cpu_init(void)
{
	if (snbep_uncore_cbox.num_boxes > boot_cpu_data.x86_max_cores)
		snbep_uncore_cbox.num_boxes = boot_cpu_data.x86_max_cores;
	uncore_msr_uncores = snbep_msr_uncores;
}

enum {
	SNBEP_PCI_QPI_PORT0_FILTER,
	SNBEP_PCI_QPI_PORT1_FILTER,
	BDX_PCI_QPI_PORT2_FILTER,
};

static int snbep_qpi_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;

	if ((hwc->config & SNBEP_PMON_CTL_EV_SEL_MASK) == 0x38) {
		reg1->idx = 0;
		reg1->reg = SNBEP_Q_Py_PCI_PMON_PKT_MATCH0;
		reg1->config = event->attr.config1;
		reg2->reg = SNBEP_Q_Py_PCI_PMON_PKT_MASK0;
		reg2->config = event->attr.config2;
	}
	return 0;
}

static void snbep_qpi_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;

	if (reg1->idx != EXTRA_REG_NONE) {
		int idx = box->pmu->pmu_idx + SNBEP_PCI_QPI_PORT0_FILTER;
		int die = box->dieid;
		struct pci_dev *filter_pdev = uncore_extra_pci_dev[die].dev[idx];

		if (filter_pdev) {
			pci_write_config_dword(filter_pdev, reg1->reg,
						(u32)reg1->config);
			pci_write_config_dword(filter_pdev, reg1->reg + 4,
						(u32)(reg1->config >> 32));
			pci_write_config_dword(filter_pdev, reg2->reg,
						(u32)reg2->config);
			pci_write_config_dword(filter_pdev, reg2->reg + 4,
						(u32)(reg2->config >> 32));
		}
	}

	pci_write_config_dword(pdev, hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static struct intel_uncore_ops snbep_uncore_qpi_ops = {
	SNBEP_UNCORE_PCI_OPS_COMMON_INIT(),
	.enable_event		= snbep_qpi_enable_event,
	.hw_config		= snbep_qpi_hw_config,
	.get_constraint		= uncore_get_constraint,
	.put_constraint		= uncore_put_constraint,
};

#define SNBEP_UNCORE_PCI_COMMON_INIT()				\
	.perf_ctr	= SNBEP_PCI_PMON_CTR0,			\
	.event_ctl	= SNBEP_PCI_PMON_CTL0,			\
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,		\
	.box_ctl	= SNBEP_PCI_PMON_BOX_CTL,		\
	.ops		= &snbep_uncore_pci_ops,		\
	.format_group	= &snbep_uncore_format_group

static struct intel_uncore_type snbep_uncore_ha = {
	.name		= "ha",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type snbep_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 4,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTR,
	.fixed_ctl	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTL,
	.event_descs	= snbep_uncore_imc_events,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type snbep_uncore_qpi = {
	.name			= "qpi",
	.num_counters		= 4,
	.num_boxes		= 2,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCI_PMON_CTR0,
	.event_ctl		= SNBEP_PCI_PMON_CTL0,
	.event_mask		= SNBEP_QPI_PCI_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &snbep_uncore_qpi_ops,
	.event_descs		= snbep_uncore_qpi_events,
	.format_group		= &snbep_uncore_qpi_format_group,
};


static struct intel_uncore_type snbep_uncore_r2pcie = {
	.name		= "r2pcie",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 44,
	.constraints	= snbep_uncore_r2pcie_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type snbep_uncore_r3qpi = {
	.name		= "r3qpi",
	.num_counters   = 3,
	.num_boxes	= 2,
	.perf_ctr_bits	= 44,
	.constraints	= snbep_uncore_r3qpi_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

enum {
	SNBEP_PCI_UNCORE_HA,
	SNBEP_PCI_UNCORE_IMC,
	SNBEP_PCI_UNCORE_QPI,
	SNBEP_PCI_UNCORE_R2PCIE,
	SNBEP_PCI_UNCORE_R3QPI,
};

static struct intel_uncore_type *snbep_pci_uncores[] = {
	[SNBEP_PCI_UNCORE_HA]		= &snbep_uncore_ha,
	[SNBEP_PCI_UNCORE_IMC]		= &snbep_uncore_imc,
	[SNBEP_PCI_UNCORE_QPI]		= &snbep_uncore_qpi,
	[SNBEP_PCI_UNCORE_R2PCIE]	= &snbep_uncore_r2pcie,
	[SNBEP_PCI_UNCORE_R3QPI]	= &snbep_uncore_r3qpi,
	NULL,
};

static const struct pci_device_id snbep_uncore_pci_ids[] = {
	{ /* Home Agent */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_HA),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_HA, 0),
	},
	{ /* MC Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC0),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_IMC, 0),
	},
	{ /* MC Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC1),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_IMC, 1),
	},
	{ /* MC Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC2),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_IMC, 2),
	},
	{ /* MC Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC3),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_IMC, 3),
	},
	{ /* QPI Port 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_QPI0),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_QPI, 0),
	},
	{ /* QPI Port 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_QPI1),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_QPI, 1),
	},
	{ /* R2PCIe */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_R2PCIE),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_R2PCIE, 0),
	},
	{ /* R3QPI Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_R3QPI0),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_R3QPI, 0),
	},
	{ /* R3QPI Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_R3QPI1),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_R3QPI, 1),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x3c86),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT0_FILTER),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x3c96),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT1_FILTER),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver snbep_uncore_pci_driver = {
	.name		= "snbep_uncore",
	.id_table	= snbep_uncore_pci_ids,
};

#define NODE_ID_MASK	0x7

/*
 * build pci bus to socket mapping
 */
static int snbep_pci2phy_map_init(int devid, int nodeid_loc, int idmap_loc, bool reverse)
{
	struct pci_dev *ubox_dev = NULL;
	int i, bus, nodeid, segment, die_id;
	struct pci2phy_map *map;
	int err = 0;
	u32 config = 0;

	while (1) {
		/* find the UBOX device */
		ubox_dev = pci_get_device(PCI_VENDOR_ID_INTEL, devid, ubox_dev);
		if (!ubox_dev)
			break;
		bus = ubox_dev->bus->number;
		/*
		 * The nodeid and idmap registers only contain enough
		 * information to handle 8 nodes.  On systems with more
		 * than 8 nodes, we need to rely on NUMA information,
		 * filled in from BIOS supplied information, to determine
		 * the topology.
		 */
		if (nr_node_ids <= 8) {
			/* get the Node ID of the local register */
			err = pci_read_config_dword(ubox_dev, nodeid_loc, &config);
			if (err)
				break;
			nodeid = config & NODE_ID_MASK;
			/* get the Node ID mapping */
			err = pci_read_config_dword(ubox_dev, idmap_loc, &config);
			if (err)
				break;

			segment = pci_domain_nr(ubox_dev->bus);
			raw_spin_lock(&pci2phy_map_lock);
			map = __find_pci2phy_map(segment);
			if (!map) {
				raw_spin_unlock(&pci2phy_map_lock);
				err = -ENOMEM;
				break;
			}

			/*
			 * every three bits in the Node ID mapping register maps
			 * to a particular node.
			 */
			for (i = 0; i < 8; i++) {
				if (nodeid == ((config >> (3 * i)) & 0x7)) {
					if (topology_max_die_per_package() > 1)
						die_id = i;
					else
						die_id = topology_phys_to_logical_pkg(i);
					if (die_id < 0)
						die_id = -ENODEV;
					map->pbus_to_dieid[bus] = die_id;
					break;
				}
			}
			raw_spin_unlock(&pci2phy_map_lock);
		} else {
			int node = pcibus_to_node(ubox_dev->bus);
			int cpu;

			segment = pci_domain_nr(ubox_dev->bus);
			raw_spin_lock(&pci2phy_map_lock);
			map = __find_pci2phy_map(segment);
			if (!map) {
				raw_spin_unlock(&pci2phy_map_lock);
				err = -ENOMEM;
				break;
			}

			die_id = -1;
			for_each_cpu(cpu, cpumask_of_pcibus(ubox_dev->bus)) {
				struct cpuinfo_x86 *c = &cpu_data(cpu);

				if (c->initialized && cpu_to_node(cpu) == node) {
					map->pbus_to_dieid[bus] = die_id = c->logical_die_id;
					break;
				}
			}
			raw_spin_unlock(&pci2phy_map_lock);

			if (WARN_ON_ONCE(die_id == -1)) {
				err = -EINVAL;
				break;
			}
		}
	}

	if (!err) {
		/*
		 * For PCI bus with no UBOX device, find the next bus
		 * that has UBOX device and use its mapping.
		 */
		raw_spin_lock(&pci2phy_map_lock);
		list_for_each_entry(map, &pci2phy_map_head, list) {
			i = -1;
			if (reverse) {
				for (bus = 255; bus >= 0; bus--) {
					if (map->pbus_to_dieid[bus] != -1)
						i = map->pbus_to_dieid[bus];
					else
						map->pbus_to_dieid[bus] = i;
				}
			} else {
				for (bus = 0; bus <= 255; bus++) {
					if (map->pbus_to_dieid[bus] != -1)
						i = map->pbus_to_dieid[bus];
					else
						map->pbus_to_dieid[bus] = i;
				}
			}
		}
		raw_spin_unlock(&pci2phy_map_lock);
	}

	pci_dev_put(ubox_dev);

	return err ? pcibios_err_to_errno(err) : 0;
}

int snbep_uncore_pci_init(void)
{
	int ret = snbep_pci2phy_map_init(0x3ce0, SNBEP_CPUNODEID, SNBEP_GIDNIDMAP, true);
	if (ret)
		return ret;
	uncore_pci_uncores = snbep_pci_uncores;
	uncore_pci_driver = &snbep_uncore_pci_driver;
	return 0;
}
/* end of Sandy Bridge-EP uncore support */

/* IvyTown uncore support */
static void ivbep_uncore_msr_init_box(struct intel_uncore_box *box)
{
	unsigned msr = uncore_msr_box_ctl(box);
	if (msr)
		wrmsrl(msr, IVBEP_PMON_BOX_CTL_INT);
}

static void ivbep_uncore_pci_init_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;

	pci_write_config_dword(pdev, SNBEP_PCI_PMON_BOX_CTL, IVBEP_PMON_BOX_CTL_INT);
}

#define IVBEP_UNCORE_MSR_OPS_COMMON_INIT()			\
	.init_box	= ivbep_uncore_msr_init_box,		\
	.disable_box	= snbep_uncore_msr_disable_box,		\
	.enable_box	= snbep_uncore_msr_enable_box,		\
	.disable_event	= snbep_uncore_msr_disable_event,	\
	.enable_event	= snbep_uncore_msr_enable_event,	\
	.read_counter	= uncore_msr_read_counter

static struct intel_uncore_ops ivbep_uncore_msr_ops = {
	IVBEP_UNCORE_MSR_OPS_COMMON_INIT(),
};

static struct intel_uncore_ops ivbep_uncore_pci_ops = {
	.init_box	= ivbep_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= snbep_uncore_pci_disable_event,
	.enable_event	= snbep_uncore_pci_enable_event,
	.read_counter	= snbep_uncore_pci_read_counter,
};

#define IVBEP_UNCORE_PCI_COMMON_INIT()				\
	.perf_ctr	= SNBEP_PCI_PMON_CTR0,			\
	.event_ctl	= SNBEP_PCI_PMON_CTL0,			\
	.event_mask	= IVBEP_PMON_RAW_EVENT_MASK,		\
	.box_ctl	= SNBEP_PCI_PMON_BOX_CTL,		\
	.ops		= &ivbep_uncore_pci_ops,			\
	.format_group	= &ivbep_uncore_format_group

static struct attribute *ivbep_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static struct attribute *ivbep_uncore_ubox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	NULL,
};

static struct attribute *ivbep_uncore_cbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid.attr,
	&format_attr_filter_link.attr,
	&format_attr_filter_state2.attr,
	&format_attr_filter_nid2.attr,
	&format_attr_filter_opc2.attr,
	&format_attr_filter_nc.attr,
	&format_attr_filter_c6.attr,
	&format_attr_filter_isoc.attr,
	NULL,
};

static struct attribute *ivbep_uncore_pcu_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_occ_sel.attr,
	&format_attr_edge.attr,
	&format_attr_thresh5.attr,
	&format_attr_occ_invert.attr,
	&format_attr_occ_edge.attr,
	&format_attr_filter_band0.attr,
	&format_attr_filter_band1.attr,
	&format_attr_filter_band2.attr,
	&format_attr_filter_band3.attr,
	NULL,
};

static struct attribute *ivbep_uncore_qpi_formats_attr[] = {
	&format_attr_event_ext.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_thresh8.attr,
	&format_attr_match_rds.attr,
	&format_attr_match_rnid30.attr,
	&format_attr_match_rnid4.attr,
	&format_attr_match_dnid.attr,
	&format_attr_match_mc.attr,
	&format_attr_match_opc.attr,
	&format_attr_match_vnw.attr,
	&format_attr_match0.attr,
	&format_attr_match1.attr,
	&format_attr_mask_rds.attr,
	&format_attr_mask_rnid30.attr,
	&format_attr_mask_rnid4.attr,
	&format_attr_mask_dnid.attr,
	&format_attr_mask_mc.attr,
	&format_attr_mask_opc.attr,
	&format_attr_mask_vnw.attr,
	&format_attr_mask0.attr,
	&format_attr_mask1.attr,
	NULL,
};

static const struct attribute_group ivbep_uncore_format_group = {
	.name = "format",
	.attrs = ivbep_uncore_formats_attr,
};

static const struct attribute_group ivbep_uncore_ubox_format_group = {
	.name = "format",
	.attrs = ivbep_uncore_ubox_formats_attr,
};

static const struct attribute_group ivbep_uncore_cbox_format_group = {
	.name = "format",
	.attrs = ivbep_uncore_cbox_formats_attr,
};

static const struct attribute_group ivbep_uncore_pcu_format_group = {
	.name = "format",
	.attrs = ivbep_uncore_pcu_formats_attr,
};

static const struct attribute_group ivbep_uncore_qpi_format_group = {
	.name = "format",
	.attrs = ivbep_uncore_qpi_formats_attr,
};

static struct intel_uncore_type ivbep_uncore_ubox = {
	.name		= "ubox",
	.num_counters   = 2,
	.num_boxes	= 1,
	.perf_ctr_bits	= 44,
	.fixed_ctr_bits	= 48,
	.perf_ctr	= SNBEP_U_MSR_PMON_CTR0,
	.event_ctl	= SNBEP_U_MSR_PMON_CTL0,
	.event_mask	= IVBEP_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr	= SNBEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl	= SNBEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.ops		= &ivbep_uncore_msr_ops,
	.format_group	= &ivbep_uncore_ubox_format_group,
};

static struct extra_reg ivbep_uncore_cbox_extra_regs[] = {
	SNBEP_CBO_EVENT_EXTRA_REG(SNBEP_CBO_PMON_CTL_TID_EN,
				  SNBEP_CBO_PMON_CTL_TID_EN, 0x1),
	SNBEP_CBO_EVENT_EXTRA_REG(0x1031, 0x10ff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x1134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4134, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5134, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0334, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4334, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0534, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4534, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0934, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4934, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0135, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2135, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4135, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4335, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4435, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4835, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a35, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5035, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8135, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4136, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4336, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4436, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4836, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a36, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5036, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4037, 0x40ff, 0x8),
	EVENT_EXTRA_END
};

static u64 ivbep_cbox_filter_mask(int fields)
{
	u64 mask = 0;

	if (fields & 0x1)
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_TID;
	if (fields & 0x2)
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_LINK;
	if (fields & 0x4)
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_STATE;
	if (fields & 0x8)
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_NID;
	if (fields & 0x10) {
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_OPC;
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_NC;
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_C6;
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_ISOC;
	}

	return mask;
}

static struct event_constraint *
ivbep_cbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	return __snbep_cbox_get_constraint(box, event, ivbep_cbox_filter_mask);
}

static int ivbep_cbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct extra_reg *er;
	int idx = 0;

	for (er = ivbep_uncore_cbox_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		idx |= er->idx;
	}

	if (idx) {
		reg1->reg = SNBEP_C0_MSR_PMON_BOX_FILTER +
			SNBEP_CBO_MSR_OFFSET * box->pmu->pmu_idx;
		reg1->config = event->attr.config1 & ivbep_cbox_filter_mask(idx);
		reg1->idx = idx;
	}
	return 0;
}

static void ivbep_cbox_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;

	if (reg1->idx != EXTRA_REG_NONE) {
		u64 filter = uncore_shared_reg_config(box, 0);
		wrmsrl(reg1->reg, filter & 0xffffffff);
		wrmsrl(reg1->reg + 6, filter >> 32);
	}

	wrmsrl(hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static struct intel_uncore_ops ivbep_uncore_cbox_ops = {
	.init_box		= ivbep_uncore_msr_init_box,
	.disable_box		= snbep_uncore_msr_disable_box,
	.enable_box		= snbep_uncore_msr_enable_box,
	.disable_event		= snbep_uncore_msr_disable_event,
	.enable_event		= ivbep_cbox_enable_event,
	.read_counter		= uncore_msr_read_counter,
	.hw_config		= ivbep_cbox_hw_config,
	.get_constraint		= ivbep_cbox_get_constraint,
	.put_constraint		= snbep_cbox_put_constraint,
};

static struct intel_uncore_type ivbep_uncore_cbox = {
	.name			= "cbox",
	.num_counters		= 4,
	.num_boxes		= 15,
	.perf_ctr_bits		= 44,
	.event_ctl		= SNBEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= SNBEP_C0_MSR_PMON_CTR0,
	.event_mask		= IVBEP_CBO_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= SNBEP_CBO_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= snbep_uncore_cbox_constraints,
	.ops			= &ivbep_uncore_cbox_ops,
	.format_group		= &ivbep_uncore_cbox_format_group,
};

static struct intel_uncore_ops ivbep_uncore_pcu_ops = {
	IVBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= snbep_pcu_hw_config,
	.get_constraint		= snbep_pcu_get_constraint,
	.put_constraint		= snbep_pcu_put_constraint,
};

static struct intel_uncore_type ivbep_uncore_pcu = {
	.name			= "pcu",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCU_MSR_PMON_CTR0,
	.event_ctl		= SNBEP_PCU_MSR_PMON_CTL0,
	.event_mask		= IVBEP_PCU_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCU_MSR_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &ivbep_uncore_pcu_ops,
	.format_group		= &ivbep_uncore_pcu_format_group,
};

static struct intel_uncore_type *ivbep_msr_uncores[] = {
	&ivbep_uncore_ubox,
	&ivbep_uncore_cbox,
	&ivbep_uncore_pcu,
	NULL,
};

void ivbep_uncore_cpu_init(void)
{
	if (ivbep_uncore_cbox.num_boxes > boot_cpu_data.x86_max_cores)
		ivbep_uncore_cbox.num_boxes = boot_cpu_data.x86_max_cores;
	uncore_msr_uncores = ivbep_msr_uncores;
}

static struct intel_uncore_type ivbep_uncore_ha = {
	.name		= "ha",
	.num_counters   = 4,
	.num_boxes	= 2,
	.perf_ctr_bits	= 48,
	IVBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type ivbep_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 8,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTR,
	.fixed_ctl	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTL,
	.event_descs	= snbep_uncore_imc_events,
	IVBEP_UNCORE_PCI_COMMON_INIT(),
};

/* registers in IRP boxes are not properly aligned */
static unsigned ivbep_uncore_irp_ctls[] = {0xd8, 0xdc, 0xe0, 0xe4};
static unsigned ivbep_uncore_irp_ctrs[] = {0xa0, 0xb0, 0xb8, 0xc0};

static void ivbep_uncore_irp_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, ivbep_uncore_irp_ctls[hwc->idx],
			       hwc->config | SNBEP_PMON_CTL_EN);
}

static void ivbep_uncore_irp_disable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, ivbep_uncore_irp_ctls[hwc->idx], hwc->config);
}

static u64 ivbep_uncore_irp_read_counter(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;
	u64 count = 0;

	pci_read_config_dword(pdev, ivbep_uncore_irp_ctrs[hwc->idx], (u32 *)&count);
	pci_read_config_dword(pdev, ivbep_uncore_irp_ctrs[hwc->idx] + 4, (u32 *)&count + 1);

	return count;
}

static struct intel_uncore_ops ivbep_uncore_irp_ops = {
	.init_box	= ivbep_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= ivbep_uncore_irp_disable_event,
	.enable_event	= ivbep_uncore_irp_enable_event,
	.read_counter	= ivbep_uncore_irp_read_counter,
};

static struct intel_uncore_type ivbep_uncore_irp = {
	.name			= "irp",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.event_mask		= IVBEP_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.ops			= &ivbep_uncore_irp_ops,
	.format_group		= &ivbep_uncore_format_group,
};

static struct intel_uncore_ops ivbep_uncore_qpi_ops = {
	.init_box	= ivbep_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= snbep_uncore_pci_disable_event,
	.enable_event	= snbep_qpi_enable_event,
	.read_counter	= snbep_uncore_pci_read_counter,
	.hw_config	= snbep_qpi_hw_config,
	.get_constraint	= uncore_get_constraint,
	.put_constraint	= uncore_put_constraint,
};

static struct intel_uncore_type ivbep_uncore_qpi = {
	.name			= "qpi",
	.num_counters		= 4,
	.num_boxes		= 3,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCI_PMON_CTR0,
	.event_ctl		= SNBEP_PCI_PMON_CTL0,
	.event_mask		= IVBEP_QPI_PCI_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &ivbep_uncore_qpi_ops,
	.format_group		= &ivbep_uncore_qpi_format_group,
};

static struct intel_uncore_type ivbep_uncore_r2pcie = {
	.name		= "r2pcie",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 44,
	.constraints	= snbep_uncore_r2pcie_constraints,
	IVBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type ivbep_uncore_r3qpi = {
	.name		= "r3qpi",
	.num_counters   = 3,
	.num_boxes	= 2,
	.perf_ctr_bits	= 44,
	.constraints	= snbep_uncore_r3qpi_constraints,
	IVBEP_UNCORE_PCI_COMMON_INIT(),
};

enum {
	IVBEP_PCI_UNCORE_HA,
	IVBEP_PCI_UNCORE_IMC,
	IVBEP_PCI_UNCORE_IRP,
	IVBEP_PCI_UNCORE_QPI,
	IVBEP_PCI_UNCORE_R2PCIE,
	IVBEP_PCI_UNCORE_R3QPI,
};

static struct intel_uncore_type *ivbep_pci_uncores[] = {
	[IVBEP_PCI_UNCORE_HA]	= &ivbep_uncore_ha,
	[IVBEP_PCI_UNCORE_IMC]	= &ivbep_uncore_imc,
	[IVBEP_PCI_UNCORE_IRP]	= &ivbep_uncore_irp,
	[IVBEP_PCI_UNCORE_QPI]	= &ivbep_uncore_qpi,
	[IVBEP_PCI_UNCORE_R2PCIE]	= &ivbep_uncore_r2pcie,
	[IVBEP_PCI_UNCORE_R3QPI]	= &ivbep_uncore_r3qpi,
	NULL,
};

static const struct pci_device_id ivbep_uncore_pci_ids[] = {
	{ /* Home Agent 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe30),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_HA, 0),
	},
	{ /* Home Agent 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe38),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_HA, 1),
	},
	{ /* MC0 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xeb4),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 0),
	},
	{ /* MC0 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xeb5),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 1),
	},
	{ /* MC0 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xeb0),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 2),
	},
	{ /* MC0 Channel 4 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xeb1),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 3),
	},
	{ /* MC1 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xef4),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 4),
	},
	{ /* MC1 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xef5),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 5),
	},
	{ /* MC1 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xef0),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 6),
	},
	{ /* MC1 Channel 4 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xef1),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 7),
	},
	{ /* IRP */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe39),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IRP, 0),
	},
	{ /* QPI0 Port 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe32),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_QPI, 0),
	},
	{ /* QPI0 Port 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe33),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_QPI, 1),
	},
	{ /* QPI1 Port 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe3a),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_QPI, 2),
	},
	{ /* R2PCIe */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe34),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_R2PCIE, 0),
	},
	{ /* R3QPI0 Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe36),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_R3QPI, 0),
	},
	{ /* R3QPI0 Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe37),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_R3QPI, 1),
	},
	{ /* R3QPI1 Link 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe3e),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_R3QPI, 2),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe86),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT0_FILTER),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe96),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT1_FILTER),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver ivbep_uncore_pci_driver = {
	.name		= "ivbep_uncore",
	.id_table	= ivbep_uncore_pci_ids,
};

int ivbep_uncore_pci_init(void)
{
	int ret = snbep_pci2phy_map_init(0x0e1e, SNBEP_CPUNODEID, SNBEP_GIDNIDMAP, true);
	if (ret)
		return ret;
	uncore_pci_uncores = ivbep_pci_uncores;
	uncore_pci_driver = &ivbep_uncore_pci_driver;
	return 0;
}
/* end of IvyTown uncore support */

/* KNL uncore support */
static struct attribute *knl_uncore_ubox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	NULL,
};

static const struct attribute_group knl_uncore_ubox_format_group = {
	.name = "format",
	.attrs = knl_uncore_ubox_formats_attr,
};

static struct intel_uncore_type knl_uncore_ubox = {
	.name			= "ubox",
	.num_counters		= 2,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= HSWEP_U_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_U_MSR_PMON_CTL0,
	.event_mask		= KNL_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.ops			= &snbep_uncore_msr_ops,
	.format_group		= &knl_uncore_ubox_format_group,
};

static struct attribute *knl_uncore_cha_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_qor.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid4.attr,
	&format_attr_filter_link3.attr,
	&format_attr_filter_state4.attr,
	&format_attr_filter_local.attr,
	&format_attr_filter_all_op.attr,
	&format_attr_filter_nnm.attr,
	&format_attr_filter_opc3.attr,
	&format_attr_filter_nc.attr,
	&format_attr_filter_isoc.attr,
	NULL,
};

static const struct attribute_group knl_uncore_cha_format_group = {
	.name = "format",
	.attrs = knl_uncore_cha_formats_attr,
};

static struct event_constraint knl_uncore_cha_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x11, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x1f, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x1),
	EVENT_CONSTRAINT_END
};

static struct extra_reg knl_uncore_cha_extra_regs[] = {
	SNBEP_CBO_EVENT_EXTRA_REG(SNBEP_CBO_PMON_CTL_TID_EN,
				  SNBEP_CBO_PMON_CTL_TID_EN, 0x1),
	SNBEP_CBO_EVENT_EXTRA_REG(0x3d, 0xff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x35, 0xff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x36, 0xff, 0x4),
	EVENT_EXTRA_END
};

static u64 knl_cha_filter_mask(int fields)
{
	u64 mask = 0;

	if (fields & 0x1)
		mask |= KNL_CHA_MSR_PMON_BOX_FILTER_TID;
	if (fields & 0x2)
		mask |= KNL_CHA_MSR_PMON_BOX_FILTER_STATE;
	if (fields & 0x4)
		mask |= KNL_CHA_MSR_PMON_BOX_FILTER_OP;
	return mask;
}

static struct event_constraint *
knl_cha_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	return __snbep_cbox_get_constraint(box, event, knl_cha_filter_mask);
}

static int knl_cha_hw_config(struct intel_uncore_box *box,
			     struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct extra_reg *er;
	int idx = 0;

	for (er = knl_uncore_cha_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		idx |= er->idx;
	}

	if (idx) {
		reg1->reg = HSWEP_C0_MSR_PMON_BOX_FILTER0 +
			    KNL_CHA_MSR_OFFSET * box->pmu->pmu_idx;
		reg1->config = event->attr.config1 & knl_cha_filter_mask(idx);

		reg1->config |= KNL_CHA_MSR_PMON_BOX_FILTER_REMOTE_NODE;
		reg1->config |= KNL_CHA_MSR_PMON_BOX_FILTER_LOCAL_NODE;
		reg1->config |= KNL_CHA_MSR_PMON_BOX_FILTER_NNC;
		reg1->idx = idx;
	}
	return 0;
}

static void hswep_cbox_enable_event(struct intel_uncore_box *box,
				    struct perf_event *event);

static struct intel_uncore_ops knl_uncore_cha_ops = {
	.init_box		= snbep_uncore_msr_init_box,
	.disable_box		= snbep_uncore_msr_disable_box,
	.enable_box		= snbep_uncore_msr_enable_box,
	.disable_event		= snbep_uncore_msr_disable_event,
	.enable_event		= hswep_cbox_enable_event,
	.read_counter		= uncore_msr_read_counter,
	.hw_config		= knl_cha_hw_config,
	.get_constraint		= knl_cha_get_constraint,
	.put_constraint		= snbep_cbox_put_constraint,
};

static struct intel_uncore_type knl_uncore_cha = {
	.name			= "cha",
	.num_counters		= 4,
	.num_boxes		= 38,
	.perf_ctr_bits		= 48,
	.event_ctl		= HSWEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= HSWEP_C0_MSR_PMON_CTR0,
	.event_mask		= KNL_CHA_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= KNL_CHA_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= knl_uncore_cha_constraints,
	.ops			= &knl_uncore_cha_ops,
	.format_group		= &knl_uncore_cha_format_group,
};

static struct attribute *knl_uncore_pcu_formats_attr[] = {
	&format_attr_event2.attr,
	&format_attr_use_occ_ctr.attr,
	&format_attr_occ_sel.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh6.attr,
	&format_attr_occ_invert.attr,
	&format_attr_occ_edge_det.attr,
	NULL,
};

static const struct attribute_group knl_uncore_pcu_format_group = {
	.name = "format",
	.attrs = knl_uncore_pcu_formats_attr,
};

static struct intel_uncore_type knl_uncore_pcu = {
	.name			= "pcu",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= HSWEP_PCU_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_PCU_MSR_PMON_CTL0,
	.event_mask		= KNL_PCU_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_PCU_MSR_PMON_BOX_CTL,
	.ops			= &snbep_uncore_msr_ops,
	.format_group		= &knl_uncore_pcu_format_group,
};

static struct intel_uncore_type *knl_msr_uncores[] = {
	&knl_uncore_ubox,
	&knl_uncore_cha,
	&knl_uncore_pcu,
	NULL,
};

void knl_uncore_cpu_init(void)
{
	uncore_msr_uncores = knl_msr_uncores;
}

static void knl_uncore_imc_enable_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);

	pci_write_config_dword(pdev, box_ctl, 0);
}

static void knl_uncore_imc_enable_event(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	if ((event->attr.config & SNBEP_PMON_CTL_EV_SEL_MASK)
							== UNCORE_FIXED_EVENT)
		pci_write_config_dword(pdev, hwc->config_base,
				       hwc->config | KNL_PMON_FIXED_CTL_EN);
	else
		pci_write_config_dword(pdev, hwc->config_base,
				       hwc->config | SNBEP_PMON_CTL_EN);
}

static struct intel_uncore_ops knl_uncore_imc_ops = {
	.init_box	= snbep_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= knl_uncore_imc_enable_box,
	.read_counter	= snbep_uncore_pci_read_counter,
	.enable_event	= knl_uncore_imc_enable_event,
	.disable_event	= snbep_uncore_pci_disable_event,
};

static struct intel_uncore_type knl_uncore_imc_uclk = {
	.name			= "imc_uclk",
	.num_counters		= 4,
	.num_boxes		= 2,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= KNL_UCLK_MSR_PMON_CTR0_LOW,
	.event_ctl		= KNL_UCLK_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= KNL_UCLK_MSR_PMON_UCLK_FIXED_LOW,
	.fixed_ctl		= KNL_UCLK_MSR_PMON_UCLK_FIXED_CTL,
	.box_ctl		= KNL_UCLK_MSR_PMON_BOX_CTL,
	.ops			= &knl_uncore_imc_ops,
	.format_group		= &snbep_uncore_format_group,
};

static struct intel_uncore_type knl_uncore_imc_dclk = {
	.name			= "imc",
	.num_counters		= 4,
	.num_boxes		= 6,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= KNL_MC0_CH0_MSR_PMON_CTR0_LOW,
	.event_ctl		= KNL_MC0_CH0_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= KNL_MC0_CH0_MSR_PMON_FIXED_LOW,
	.fixed_ctl		= KNL_MC0_CH0_MSR_PMON_FIXED_CTL,
	.box_ctl		= KNL_MC0_CH0_MSR_PMON_BOX_CTL,
	.ops			= &knl_uncore_imc_ops,
	.format_group		= &snbep_uncore_format_group,
};

static struct intel_uncore_type knl_uncore_edc_uclk = {
	.name			= "edc_uclk",
	.num_counters		= 4,
	.num_boxes		= 8,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= KNL_UCLK_MSR_PMON_CTR0_LOW,
	.event_ctl		= KNL_UCLK_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= KNL_UCLK_MSR_PMON_UCLK_FIXED_LOW,
	.fixed_ctl		= KNL_UCLK_MSR_PMON_UCLK_FIXED_CTL,
	.box_ctl		= KNL_UCLK_MSR_PMON_BOX_CTL,
	.ops			= &knl_uncore_imc_ops,
	.format_group		= &snbep_uncore_format_group,
};

static struct intel_uncore_type knl_uncore_edc_eclk = {
	.name			= "edc_eclk",
	.num_counters		= 4,
	.num_boxes		= 8,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= KNL_EDC0_ECLK_MSR_PMON_CTR0_LOW,
	.event_ctl		= KNL_EDC0_ECLK_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= KNL_EDC0_ECLK_MSR_PMON_ECLK_FIXED_LOW,
	.fixed_ctl		= KNL_EDC0_ECLK_MSR_PMON_ECLK_FIXED_CTL,
	.box_ctl		= KNL_EDC0_ECLK_MSR_PMON_BOX_CTL,
	.ops			= &knl_uncore_imc_ops,
	.format_group		= &snbep_uncore_format_group,
};

static struct event_constraint knl_uncore_m2pcie_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type knl_uncore_m2pcie = {
	.name		= "m2pcie",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.constraints	= knl_uncore_m2pcie_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct attribute *knl_uncore_irp_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_qor.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static const struct attribute_group knl_uncore_irp_format_group = {
	.name = "format",
	.attrs = knl_uncore_irp_formats_attr,
};

static struct intel_uncore_type knl_uncore_irp = {
	.name			= "irp",
	.num_counters		= 2,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCI_PMON_CTR0,
	.event_ctl		= SNBEP_PCI_PMON_CTL0,
	.event_mask		= KNL_IRP_PCI_PMON_RAW_EVENT_MASK,
	.box_ctl		= KNL_IRP_PCI_PMON_BOX_CTL,
	.ops			= &snbep_uncore_pci_ops,
	.format_group		= &knl_uncore_irp_format_group,
};

enum {
	KNL_PCI_UNCORE_MC_UCLK,
	KNL_PCI_UNCORE_MC_DCLK,
	KNL_PCI_UNCORE_EDC_UCLK,
	KNL_PCI_UNCORE_EDC_ECLK,
	KNL_PCI_UNCORE_M2PCIE,
	KNL_PCI_UNCORE_IRP,
};

static struct intel_uncore_type *knl_pci_uncores[] = {
	[KNL_PCI_UNCORE_MC_UCLK]	= &knl_uncore_imc_uclk,
	[KNL_PCI_UNCORE_MC_DCLK]	= &knl_uncore_imc_dclk,
	[KNL_PCI_UNCORE_EDC_UCLK]	= &knl_uncore_edc_uclk,
	[KNL_PCI_UNCORE_EDC_ECLK]	= &knl_uncore_edc_eclk,
	[KNL_PCI_UNCORE_M2PCIE]		= &knl_uncore_m2pcie,
	[KNL_PCI_UNCORE_IRP]		= &knl_uncore_irp,
	NULL,
};

/*
 * KNL uses a common PCI device ID for multiple instances of an Uncore PMU
 * device type. prior to KNL, each instance of a PMU device type had a unique
 * device ID.
 *
 *	PCI Device ID	Uncore PMU Devices
 *	----------------------------------
 *	0x7841		MC0 UClk, MC1 UClk
 *	0x7843		MC0 DClk CH 0, MC0 DClk CH 1, MC0 DClk CH 2,
 *			MC1 DClk CH 0, MC1 DClk CH 1, MC1 DClk CH 2
 *	0x7833		EDC0 UClk, EDC1 UClk, EDC2 UClk, EDC3 UClk,
 *			EDC4 UClk, EDC5 UClk, EDC6 UClk, EDC7 UClk
 *	0x7835		EDC0 EClk, EDC1 EClk, EDC2 EClk, EDC3 EClk,
 *			EDC4 EClk, EDC5 EClk, EDC6 EClk, EDC7 EClk
 *	0x7817		M2PCIe
 *	0x7814		IRP
*/

static const struct pci_device_id knl_uncore_pci_ids[] = {
	{ /* MC0 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7841),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(10, 0, KNL_PCI_UNCORE_MC_UCLK, 0),
	},
	{ /* MC1 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7841),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(11, 0, KNL_PCI_UNCORE_MC_UCLK, 1),
	},
	{ /* MC0 DClk CH 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7843),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(8, 2, KNL_PCI_UNCORE_MC_DCLK, 0),
	},
	{ /* MC0 DClk CH 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7843),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(8, 3, KNL_PCI_UNCORE_MC_DCLK, 1),
	},
	{ /* MC0 DClk CH 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7843),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(8, 4, KNL_PCI_UNCORE_MC_DCLK, 2),
	},
	{ /* MC1 DClk CH 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7843),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(9, 2, KNL_PCI_UNCORE_MC_DCLK, 3),
	},
	{ /* MC1 DClk CH 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7843),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(9, 3, KNL_PCI_UNCORE_MC_DCLK, 4),
	},
	{ /* MC1 DClk CH 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7843),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(9, 4, KNL_PCI_UNCORE_MC_DCLK, 5),
	},
	{ /* EDC0 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(15, 0, KNL_PCI_UNCORE_EDC_UCLK, 0),
	},
	{ /* EDC1 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(16, 0, KNL_PCI_UNCORE_EDC_UCLK, 1),
	},
	{ /* EDC2 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(17, 0, KNL_PCI_UNCORE_EDC_UCLK, 2),
	},
	{ /* EDC3 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(18, 0, KNL_PCI_UNCORE_EDC_UCLK, 3),
	},
	{ /* EDC4 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(19, 0, KNL_PCI_UNCORE_EDC_UCLK, 4),
	},
	{ /* EDC5 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(20, 0, KNL_PCI_UNCORE_EDC_UCLK, 5),
	},
	{ /* EDC6 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(21, 0, KNL_PCI_UNCORE_EDC_UCLK, 6),
	},
	{ /* EDC7 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(22, 0, KNL_PCI_UNCORE_EDC_UCLK, 7),
	},
	{ /* EDC0 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(24, 2, KNL_PCI_UNCORE_EDC_ECLK, 0),
	},
	{ /* EDC1 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(25, 2, KNL_PCI_UNCORE_EDC_ECLK, 1),
	},
	{ /* EDC2 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(26, 2, KNL_PCI_UNCORE_EDC_ECLK, 2),
	},
	{ /* EDC3 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(27, 2, KNL_PCI_UNCORE_EDC_ECLK, 3),
	},
	{ /* EDC4 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(28, 2, KNL_PCI_UNCORE_EDC_ECLK, 4),
	},
	{ /* EDC5 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(29, 2, KNL_PCI_UNCORE_EDC_ECLK, 5),
	},
	{ /* EDC6 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(30, 2, KNL_PCI_UNCORE_EDC_ECLK, 6),
	},
	{ /* EDC7 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(31, 2, KNL_PCI_UNCORE_EDC_ECLK, 7),
	},
	{ /* M2PCIe */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7817),
		.driver_data = UNCORE_PCI_DEV_DATA(KNL_PCI_UNCORE_M2PCIE, 0),
	},
	{ /* IRP */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7814),
		.driver_data = UNCORE_PCI_DEV_DATA(KNL_PCI_UNCORE_IRP, 0),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver knl_uncore_pci_driver = {
	.name		= "knl_uncore",
	.id_table	= knl_uncore_pci_ids,
};

int knl_uncore_pci_init(void)
{
	int ret;

	/* All KNL PCI based PMON units are on the same PCI bus except IRP */
	ret = snb_pci2phy_map_init(0x7814); /* IRP */
	if (ret)
		return ret;
	ret = snb_pci2phy_map_init(0x7817); /* M2PCIe */
	if (ret)
		return ret;
	uncore_pci_uncores = knl_pci_uncores;
	uncore_pci_driver = &knl_uncore_pci_driver;
	return 0;
}

/* end of KNL uncore support */

/* Haswell-EP uncore support */
static struct attribute *hswep_uncore_ubox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	&format_attr_filter_tid2.attr,
	&format_attr_filter_cid.attr,
	NULL,
};

static const struct attribute_group hswep_uncore_ubox_format_group = {
	.name = "format",
	.attrs = hswep_uncore_ubox_formats_attr,
};

static int hswep_ubox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	reg1->reg = HSWEP_U_MSR_PMON_FILTER;
	reg1->config = event->attr.config1 & HSWEP_U_MSR_PMON_BOX_FILTER_MASK;
	reg1->idx = 0;
	return 0;
}

static struct intel_uncore_ops hswep_uncore_ubox_ops = {
	SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= hswep_ubox_hw_config,
	.get_constraint		= uncore_get_constraint,
	.put_constraint		= uncore_put_constraint,
};

static struct intel_uncore_type hswep_uncore_ubox = {
	.name			= "ubox",
	.num_counters		= 2,
	.num_boxes		= 1,
	.perf_ctr_bits		= 44,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= HSWEP_U_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_U_MSR_PMON_CTL0,
	.event_mask		= SNBEP_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.num_shared_regs	= 1,
	.ops			= &hswep_uncore_ubox_ops,
	.format_group		= &hswep_uncore_ubox_format_group,
};

static struct attribute *hswep_uncore_cbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid3.attr,
	&format_attr_filter_link2.attr,
	&format_attr_filter_state3.attr,
	&format_attr_filter_nid2.attr,
	&format_attr_filter_opc2.attr,
	&format_attr_filter_nc.attr,
	&format_attr_filter_c6.attr,
	&format_attr_filter_isoc.attr,
	NULL,
};

static const struct attribute_group hswep_uncore_cbox_format_group = {
	.name = "format",
	.attrs = hswep_uncore_cbox_formats_attr,
};

static struct event_constraint hswep_uncore_cbox_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x01, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x09, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x38, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x3b, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x3e, 0x1),
	EVENT_CONSTRAINT_END
};

static struct extra_reg hswep_uncore_cbox_extra_regs[] = {
	SNBEP_CBO_EVENT_EXTRA_REG(SNBEP_CBO_PMON_CTL_TID_EN,
				  SNBEP_CBO_PMON_CTL_TID_EN, 0x1),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0334, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0534, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0934, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x1134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4037, 0x40ff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4028, 0x40ff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4032, 0x40ff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4029, 0x40ff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4033, 0x40ff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x402A, 0x40ff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0135, 0xffff, 0x12),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4135, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4435, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4835, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5035, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4335, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a35, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2135, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8135, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4136, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4436, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4836, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4336, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a36, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5036, 0xffff, 0x8),
	EVENT_EXTRA_END
};

static u64 hswep_cbox_filter_mask(int fields)
{
	u64 mask = 0;
	if (fields & 0x1)
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_TID;
	if (fields & 0x2)
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_LINK;
	if (fields & 0x4)
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_STATE;
	if (fields & 0x8)
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_NID;
	if (fields & 0x10) {
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_OPC;
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_NC;
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_C6;
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_ISOC;
	}
	return mask;
}

static struct event_constraint *
hswep_cbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	return __snbep_cbox_get_constraint(box, event, hswep_cbox_filter_mask);
}

static int hswep_cbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct extra_reg *er;
	int idx = 0;

	for (er = hswep_uncore_cbox_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		idx |= er->idx;
	}

	if (idx) {
		reg1->reg = HSWEP_C0_MSR_PMON_BOX_FILTER0 +
			    HSWEP_CBO_MSR_OFFSET * box->pmu->pmu_idx;
		reg1->config = event->attr.config1 & hswep_cbox_filter_mask(idx);
		reg1->idx = idx;
	}
	return 0;
}

static void hswep_cbox_enable_event(struct intel_uncore_box *box,
				  struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;

	if (reg1->idx != EXTRA_REG_NONE) {
		u64 filter = uncore_shared_reg_config(box, 0);
		wrmsrl(reg1->reg, filter & 0xffffffff);
		wrmsrl(reg1->reg + 1, filter >> 32);
	}

	wrmsrl(hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static struct intel_uncore_ops hswep_uncore_cbox_ops = {
	.init_box		= snbep_uncore_msr_init_box,
	.disable_box		= snbep_uncore_msr_disable_box,
	.enable_box		= snbep_uncore_msr_enable_box,
	.disable_event		= snbep_uncore_msr_disable_event,
	.enable_event		= hswep_cbox_enable_event,
	.read_counter		= uncore_msr_read_counter,
	.hw_config		= hswep_cbox_hw_config,
	.get_constraint		= hswep_cbox_get_constraint,
	.put_constraint		= snbep_cbox_put_constraint,
};

static struct intel_uncore_type hswep_uncore_cbox = {
	.name			= "cbox",
	.num_counters		= 4,
	.num_boxes		= 18,
	.perf_ctr_bits		= 48,
	.event_ctl		= HSWEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= HSWEP_C0_MSR_PMON_CTR0,
	.event_mask		= SNBEP_CBO_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= HSWEP_CBO_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= hswep_uncore_cbox_constraints,
	.ops			= &hswep_uncore_cbox_ops,
	.format_group		= &hswep_uncore_cbox_format_group,
};

/*
 * Write SBOX Initialization register bit by bit to avoid spurious #GPs
 */
static void hswep_uncore_sbox_msr_init_box(struct intel_uncore_box *box)
{
	unsigned msr = uncore_msr_box_ctl(box);

	if (msr) {
		u64 init = SNBEP_PMON_BOX_CTL_INT;
		u64 flags = 0;
		int i;

		for_each_set_bit(i, (unsigned long *)&init, 64) {
			flags |= (1ULL << i);
			wrmsrl(msr, flags);
		}
	}
}

static struct intel_uncore_ops hswep_uncore_sbox_msr_ops = {
	__SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.init_box		= hswep_uncore_sbox_msr_init_box
};

static struct attribute *hswep_uncore_sbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static const struct attribute_group hswep_uncore_sbox_format_group = {
	.name = "format",
	.attrs = hswep_uncore_sbox_formats_attr,
};

static struct intel_uncore_type hswep_uncore_sbox = {
	.name			= "sbox",
	.num_counters		= 4,
	.num_boxes		= 4,
	.perf_ctr_bits		= 44,
	.event_ctl		= HSWEP_S0_MSR_PMON_CTL0,
	.perf_ctr		= HSWEP_S0_MSR_PMON_CTR0,
	.event_mask		= HSWEP_S_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_S0_MSR_PMON_BOX_CTL,
	.msr_offset		= HSWEP_SBOX_MSR_OFFSET,
	.ops			= &hswep_uncore_sbox_msr_ops,
	.format_group		= &hswep_uncore_sbox_format_group,
};

static int hswep_pcu_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	int ev_sel = hwc->config & SNBEP_PMON_CTL_EV_SEL_MASK;

	if (ev_sel >= 0xb && ev_sel <= 0xe) {
		reg1->reg = HSWEP_PCU_MSR_PMON_BOX_FILTER;
		reg1->idx = ev_sel - 0xb;
		reg1->config = event->attr.config1 & (0xff << reg1->idx);
	}
	return 0;
}

static struct intel_uncore_ops hswep_uncore_pcu_ops = {
	SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= hswep_pcu_hw_config,
	.get_constraint		= snbep_pcu_get_constraint,
	.put_constraint		= snbep_pcu_put_constraint,
};

static struct intel_uncore_type hswep_uncore_pcu = {
	.name			= "pcu",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= HSWEP_PCU_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_PCU_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PCU_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_PCU_MSR_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &hswep_uncore_pcu_ops,
	.format_group		= &snbep_uncore_pcu_format_group,
};

static struct intel_uncore_type *hswep_msr_uncores[] = {
	&hswep_uncore_ubox,
	&hswep_uncore_cbox,
	&hswep_uncore_sbox,
	&hswep_uncore_pcu,
	NULL,
};

#define HSWEP_PCU_DID			0x2fc0
#define HSWEP_PCU_CAPID4_OFFET		0x94
#define hswep_get_chop(_cap)		(((_cap) >> 6) & 0x3)

static bool hswep_has_limit_sbox(unsigned int device)
{
	struct pci_dev *dev = pci_get_device(PCI_VENDOR_ID_INTEL, device, NULL);
	u32 capid4;

	if (!dev)
		return false;

	pci_read_config_dword(dev, HSWEP_PCU_CAPID4_OFFET, &capid4);
	if (!hswep_get_chop(capid4))
		return true;

	return false;
}

void hswep_uncore_cpu_init(void)
{
	if (hswep_uncore_cbox.num_boxes > boot_cpu_data.x86_max_cores)
		hswep_uncore_cbox.num_boxes = boot_cpu_data.x86_max_cores;

	/* Detect 6-8 core systems with only two SBOXes */
	if (hswep_has_limit_sbox(HSWEP_PCU_DID))
		hswep_uncore_sbox.num_boxes = 2;

	uncore_msr_uncores = hswep_msr_uncores;
}

static struct intel_uncore_type hswep_uncore_ha = {
	.name		= "ha",
	.num_counters   = 4,
	.num_boxes	= 2,
	.perf_ctr_bits	= 48,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct uncore_event_desc hswep_uncore_imc_events[] = {
	INTEL_UNCORE_EVENT_DESC(clockticks,      "event=0x00,umask=0x00"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read,  "event=0x04,umask=0x03"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read.scale, "6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read.unit, "MiB"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write, "event=0x04,umask=0x0c"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write.scale, "6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write.unit, "MiB"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_type hswep_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 8,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTR,
	.fixed_ctl	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTL,
	.event_descs	= hswep_uncore_imc_events,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static unsigned hswep_uncore_irp_ctrs[] = {0xa0, 0xa8, 0xb0, 0xb8};

static u64 hswep_uncore_irp_read_counter(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;
	u64 count = 0;

	pci_read_config_dword(pdev, hswep_uncore_irp_ctrs[hwc->idx], (u32 *)&count);
	pci_read_config_dword(pdev, hswep_uncore_irp_ctrs[hwc->idx] + 4, (u32 *)&count + 1);

	return count;
}

static struct intel_uncore_ops hswep_uncore_irp_ops = {
	.init_box	= snbep_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= ivbep_uncore_irp_disable_event,
	.enable_event	= ivbep_uncore_irp_enable_event,
	.read_counter	= hswep_uncore_irp_read_counter,
};

static struct intel_uncore_type hswep_uncore_irp = {
	.name			= "irp",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.ops			= &hswep_uncore_irp_ops,
	.format_group		= &snbep_uncore_format_group,
};

static struct intel_uncore_type hswep_uncore_qpi = {
	.name			= "qpi",
	.num_counters		= 4,
	.num_boxes		= 3,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCI_PMON_CTR0,
	.event_ctl		= SNBEP_PCI_PMON_CTL0,
	.event_mask		= SNBEP_QPI_PCI_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &snbep_uncore_qpi_ops,
	.format_group		= &snbep_uncore_qpi_format_group,
};

static struct event_constraint hswep_uncore_r2pcie_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x24, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x27, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x28, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x29, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2a, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x2b, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2c, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2d, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x35, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type hswep_uncore_r2pcie = {
	.name		= "r2pcie",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.constraints	= hswep_uncore_r2pcie_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct event_constraint hswep_uncore_r3qpi_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x01, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x07, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x08, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x09, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x0a, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x0e, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x12, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x14, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x15, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x1f, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x20, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x21, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x22, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x28, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x29, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2c, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2d, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2e, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2f, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x31, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x37, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x38, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x39, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type hswep_uncore_r3qpi = {
	.name		= "r3qpi",
	.num_counters   = 3,
	.num_boxes	= 3,
	.perf_ctr_bits	= 44,
	.constraints	= hswep_uncore_r3qpi_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

enum {
	HSWEP_PCI_UNCORE_HA,
	HSWEP_PCI_UNCORE_IMC,
	HSWEP_PCI_UNCORE_IRP,
	HSWEP_PCI_UNCORE_QPI,
	HSWEP_PCI_UNCORE_R2PCIE,
	HSWEP_PCI_UNCORE_R3QPI,
};

static struct intel_uncore_type *hswep_pci_uncores[] = {
	[HSWEP_PCI_UNCORE_HA]	= &hswep_uncore_ha,
	[HSWEP_PCI_UNCORE_IMC]	= &hswep_uncore_imc,
	[HSWEP_PCI_UNCORE_IRP]	= &hswep_uncore_irp,
	[HSWEP_PCI_UNCORE_QPI]	= &hswep_uncore_qpi,
	[HSWEP_PCI_UNCORE_R2PCIE]	= &hswep_uncore_r2pcie,
	[HSWEP_PCI_UNCORE_R3QPI]	= &hswep_uncore_r3qpi,
	NULL,
};

static const struct pci_device_id hswep_uncore_pci_ids[] = {
	{ /* Home Agent 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f30),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_HA, 0),
	},
	{ /* Home Agent 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f38),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_HA, 1),
	},
	{ /* MC0 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fb0),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 0),
	},
	{ /* MC0 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fb1),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 1),
	},
	{ /* MC0 Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fb4),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 2),
	},
	{ /* MC0 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fb5),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 3),
	},
	{ /* MC1 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fd0),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 4),
	},
	{ /* MC1 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fd1),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 5),
	},
	{ /* MC1 Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fd4),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 6),
	},
	{ /* MC1 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fd5),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 7),
	},
	{ /* IRP */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f39),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IRP, 0),
	},
	{ /* QPI0 Port 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f32),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_QPI, 0),
	},
	{ /* QPI0 Port 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f33),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_QPI, 1),
	},
	{ /* QPI1 Port 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f3a),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_QPI, 2),
	},
	{ /* R2PCIe */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f34),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_R2PCIE, 0),
	},
	{ /* R3QPI0 Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f36),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_R3QPI, 0),
	},
	{ /* R3QPI0 Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f37),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_R3QPI, 1),
	},
	{ /* R3QPI1 Link 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f3e),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_R3QPI, 2),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f86),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT0_FILTER),
	},
	{ /* QPI Port 1 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f96),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT1_FILTER),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver hswep_uncore_pci_driver = {
	.name		= "hswep_uncore",
	.id_table	= hswep_uncore_pci_ids,
};

int hswep_uncore_pci_init(void)
{
	int ret = snbep_pci2phy_map_init(0x2f1e, SNBEP_CPUNODEID, SNBEP_GIDNIDMAP, true);
	if (ret)
		return ret;
	uncore_pci_uncores = hswep_pci_uncores;
	uncore_pci_driver = &hswep_uncore_pci_driver;
	return 0;
}
/* end of Haswell-EP uncore support */

/* BDX uncore support */

static struct intel_uncore_type bdx_uncore_ubox = {
	.name			= "ubox",
	.num_counters		= 2,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= HSWEP_U_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_U_MSR_PMON_CTL0,
	.event_mask		= SNBEP_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.num_shared_regs	= 1,
	.ops			= &ivbep_uncore_msr_ops,
	.format_group		= &ivbep_uncore_ubox_format_group,
};

static struct event_constraint bdx_uncore_cbox_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x09, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x3e, 0x1),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type bdx_uncore_cbox = {
	.name			= "cbox",
	.num_counters		= 4,
	.num_boxes		= 24,
	.perf_ctr_bits		= 48,
	.event_ctl		= HSWEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= HSWEP_C0_MSR_PMON_CTR0,
	.event_mask		= SNBEP_CBO_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= HSWEP_CBO_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= bdx_uncore_cbox_constraints,
	.ops			= &hswep_uncore_cbox_ops,
	.format_group		= &hswep_uncore_cbox_format_group,
};

static struct intel_uncore_type bdx_uncore_sbox = {
	.name			= "sbox",
	.num_counters		= 4,
	.num_boxes		= 4,
	.perf_ctr_bits		= 48,
	.event_ctl		= HSWEP_S0_MSR_PMON_CTL0,
	.perf_ctr		= HSWEP_S0_MSR_PMON_CTR0,
	.event_mask		= HSWEP_S_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_S0_MSR_PMON_BOX_CTL,
	.msr_offset		= HSWEP_SBOX_MSR_OFFSET,
	.ops			= &hswep_uncore_sbox_msr_ops,
	.format_group		= &hswep_uncore_sbox_format_group,
};

#define BDX_MSR_UNCORE_SBOX	3

static struct intel_uncore_type *bdx_msr_uncores[] = {
	&bdx_uncore_ubox,
	&bdx_uncore_cbox,
	&hswep_uncore_pcu,
	&bdx_uncore_sbox,
	NULL,
};

/* Bit 7 'Use Occupancy' is not available for counter 0 on BDX */
static struct event_constraint bdx_uncore_pcu_constraints[] = {
	EVENT_CONSTRAINT(0x80, 0xe, 0x80),
	EVENT_CONSTRAINT_END
};

#define BDX_PCU_DID			0x6fc0

void bdx_uncore_cpu_init(void)
{
	if (bdx_uncore_cbox.num_boxes > boot_cpu_data.x86_max_cores)
		bdx_uncore_cbox.num_boxes = boot_cpu_data.x86_max_cores;
	uncore_msr_uncores = bdx_msr_uncores;

	/* Detect systems with no SBOXes */
	if ((boot_cpu_data.x86_model == 86) || hswep_has_limit_sbox(BDX_PCU_DID))
		uncore_msr_uncores[BDX_MSR_UNCORE_SBOX] = NULL;

	hswep_uncore_pcu.constraints = bdx_uncore_pcu_constraints;
}

static struct intel_uncore_type bdx_uncore_ha = {
	.name		= "ha",
	.num_counters   = 4,
	.num_boxes	= 2,
	.perf_ctr_bits	= 48,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type bdx_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 8,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTR,
	.fixed_ctl	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTL,
	.event_descs	= hswep_uncore_imc_events,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type bdx_uncore_irp = {
	.name			= "irp",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.ops			= &hswep_uncore_irp_ops,
	.format_group		= &snbep_uncore_format_group,
};

static struct intel_uncore_type bdx_uncore_qpi = {
	.name			= "qpi",
	.num_counters		= 4,
	.num_boxes		= 3,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCI_PMON_CTR0,
	.event_ctl		= SNBEP_PCI_PMON_CTL0,
	.event_mask		= SNBEP_QPI_PCI_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &snbep_uncore_qpi_ops,
	.format_group		= &snbep_uncore_qpi_format_group,
};

static struct event_constraint bdx_uncore_r2pcie_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x28, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2c, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2d, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type bdx_uncore_r2pcie = {
	.name		= "r2pcie",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.constraints	= bdx_uncore_r2pcie_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct event_constraint bdx_uncore_r3qpi_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x01, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x07, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x08, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x09, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x0a, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x0e, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x14, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x15, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x1f, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x20, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x21, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x22, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x28, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x29, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2c, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2d, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2e, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2f, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x37, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x38, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x39, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type bdx_uncore_r3qpi = {
	.name		= "r3qpi",
	.num_counters   = 3,
	.num_boxes	= 3,
	.perf_ctr_bits	= 48,
	.constraints	= bdx_uncore_r3qpi_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

enum {
	BDX_PCI_UNCORE_HA,
	BDX_PCI_UNCORE_IMC,
	BDX_PCI_UNCORE_IRP,
	BDX_PCI_UNCORE_QPI,
	BDX_PCI_UNCORE_R2PCIE,
	BDX_PCI_UNCORE_R3QPI,
};

static struct intel_uncore_type *bdx_pci_uncores[] = {
	[BDX_PCI_UNCORE_HA]	= &bdx_uncore_ha,
	[BDX_PCI_UNCORE_IMC]	= &bdx_uncore_imc,
	[BDX_PCI_UNCORE_IRP]	= &bdx_uncore_irp,
	[BDX_PCI_UNCORE_QPI]	= &bdx_uncore_qpi,
	[BDX_PCI_UNCORE_R2PCIE]	= &bdx_uncore_r2pcie,
	[BDX_PCI_UNCORE_R3QPI]	= &bdx_uncore_r3qpi,
	NULL,
};

static const struct pci_device_id bdx_uncore_pci_ids[] = {
	{ /* Home Agent 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f30),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_HA, 0),
	},
	{ /* Home Agent 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f38),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_HA, 1),
	},
	{ /* MC0 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fb0),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 0),
	},
	{ /* MC0 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fb1),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 1),
	},
	{ /* MC0 Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fb4),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 2),
	},
	{ /* MC0 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fb5),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 3),
	},
	{ /* MC1 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fd0),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 4),
	},
	{ /* MC1 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fd1),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 5),
	},
	{ /* MC1 Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fd4),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 6),
	},
	{ /* MC1 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fd5),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 7),
	},
	{ /* IRP */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f39),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IRP, 0),
	},
	{ /* QPI0 Port 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f32),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_QPI, 0),
	},
	{ /* QPI0 Port 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f33),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_QPI, 1),
	},
	{ /* QPI1 Port 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f3a),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_QPI, 2),
	},
	{ /* R2PCIe */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f34),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_R2PCIE, 0),
	},
	{ /* R3QPI0 Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f36),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_R3QPI, 0),
	},
	{ /* R3QPI0 Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f37),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_R3QPI, 1),
	},
	{ /* R3QPI1 Link 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f3e),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_R3QPI, 2),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f86),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT0_FILTER),
	},
	{ /* QPI Port 1 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f96),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT1_FILTER),
	},
	{ /* QPI Port 2 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f46),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   BDX_PCI_QPI_PORT2_FILTER),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver bdx_uncore_pci_driver = {
	.name		= "bdx_uncore",
	.id_table	= bdx_uncore_pci_ids,
};

int bdx_uncore_pci_init(void)
{
	int ret = snbep_pci2phy_map_init(0x6f1e, SNBEP_CPUNODEID, SNBEP_GIDNIDMAP, true);

	if (ret)
		return ret;
	uncore_pci_uncores = bdx_pci_uncores;
	uncore_pci_driver = &bdx_uncore_pci_driver;
	return 0;
}

/* end of BDX uncore support */

/* SKX uncore support */

static struct intel_uncore_type skx_uncore_ubox = {
	.name			= "ubox",
	.num_counters		= 2,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= HSWEP_U_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_U_MSR_PMON_CTL0,
	.event_mask		= SNBEP_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.ops			= &ivbep_uncore_msr_ops,
	.format_group		= &ivbep_uncore_ubox_format_group,
};

static struct attribute *skx_uncore_cha_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid4.attr,
	&format_attr_filter_state5.attr,
	&format_attr_filter_rem.attr,
	&format_attr_filter_loc.attr,
	&format_attr_filter_nm.attr,
	&format_attr_filter_all_op.attr,
	&format_attr_filter_not_nm.attr,
	&format_attr_filter_opc_0.attr,
	&format_attr_filter_opc_1.attr,
	&format_attr_filter_nc.attr,
	&format_attr_filter_isoc.attr,
	NULL,
};

static const struct attribute_group skx_uncore_chabox_format_group = {
	.name = "format",
	.attrs = skx_uncore_cha_formats_attr,
};

static struct event_constraint skx_uncore_chabox_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x11, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x1),
	EVENT_CONSTRAINT_END
};

static struct extra_reg skx_uncore_cha_extra_regs[] = {
	SNBEP_CBO_EVENT_EXTRA_REG(0x0334, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0534, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0934, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x1134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x3134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x9134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x35, 0xff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x36, 0xff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x38, 0xff, 0x3),
	EVENT_EXTRA_END
};

static u64 skx_cha_filter_mask(int fields)
{
	u64 mask = 0;

	if (fields & 0x1)
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_TID;
	if (fields & 0x2)
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_LINK;
	if (fields & 0x4)
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_STATE;
	if (fields & 0x8) {
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_REM;
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_LOC;
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_ALL_OPC;
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_NM;
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_NOT_NM;
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_OPC0;
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_OPC1;
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_NC;
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_ISOC;
	}
	return mask;
}

static struct event_constraint *
skx_cha_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	return __snbep_cbox_get_constraint(box, event, skx_cha_filter_mask);
}

static int skx_cha_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct extra_reg *er;
	int idx = 0;

	for (er = skx_uncore_cha_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		idx |= er->idx;
	}

	if (idx) {
		reg1->reg = HSWEP_C0_MSR_PMON_BOX_FILTER0 +
			    HSWEP_CBO_MSR_OFFSET * box->pmu->pmu_idx;
		reg1->config = event->attr.config1 & skx_cha_filter_mask(idx);
		reg1->idx = idx;
	}
	return 0;
}

static struct intel_uncore_ops skx_uncore_chabox_ops = {
	/* There is no frz_en for chabox ctl */
	.init_box		= ivbep_uncore_msr_init_box,
	.disable_box		= snbep_uncore_msr_disable_box,
	.enable_box		= snbep_uncore_msr_enable_box,
	.disable_event		= snbep_uncore_msr_disable_event,
	.enable_event		= hswep_cbox_enable_event,
	.read_counter		= uncore_msr_read_counter,
	.hw_config		= skx_cha_hw_config,
	.get_constraint		= skx_cha_get_constraint,
	.put_constraint		= snbep_cbox_put_constraint,
};

static struct intel_uncore_type skx_uncore_chabox = {
	.name			= "cha",
	.num_counters		= 4,
	.perf_ctr_bits		= 48,
	.event_ctl		= HSWEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= HSWEP_C0_MSR_PMON_CTR0,
	.event_mask		= HSWEP_S_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= HSWEP_CBO_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= skx_uncore_chabox_constraints,
	.ops			= &skx_uncore_chabox_ops,
	.format_group		= &skx_uncore_chabox_format_group,
};

static struct attribute *skx_uncore_iio_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh9.attr,
	&format_attr_ch_mask.attr,
	&format_attr_fc_mask.attr,
	NULL,
};

static const struct attribute_group skx_uncore_iio_format_group = {
	.name = "format",
	.attrs = skx_uncore_iio_formats_attr,
};

static struct event_constraint skx_uncore_iio_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x83, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x88, 0xc),
	UNCORE_EVENT_CONSTRAINT(0x95, 0xc),
	UNCORE_EVENT_CONSTRAINT(0xc0, 0xc),
	UNCORE_EVENT_CONSTRAINT(0xc5, 0xc),
	UNCORE_EVENT_CONSTRAINT(0xd4, 0xc),
	EVENT_CONSTRAINT_END
};

static void skx_iio_enable_event(struct intel_uncore_box *box,
				 struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static struct intel_uncore_ops skx_uncore_iio_ops = {
	.init_box		= ivbep_uncore_msr_init_box,
	.disable_box		= snbep_uncore_msr_disable_box,
	.enable_box		= snbep_uncore_msr_enable_box,
	.disable_event		= snbep_uncore_msr_disable_event,
	.enable_event		= skx_iio_enable_event,
	.read_counter		= uncore_msr_read_counter,
};

static inline u8 skx_iio_stack(struct intel_uncore_pmu *pmu, int die)
{
	return pmu->type->topology[die].configuration >>
	       (pmu->pmu_idx * BUS_NUM_STRIDE);
}

static umode_t
pmu_iio_mapping_visible(struct kobject *kobj, struct attribute *attr,
			 int die, int zero_bus_pmu)
{
	struct intel_uncore_pmu *pmu = dev_to_uncore_pmu(kobj_to_dev(kobj));

	return (!skx_iio_stack(pmu, die) && pmu->pmu_idx != zero_bus_pmu) ? 0 : attr->mode;
}

static umode_t
skx_iio_mapping_visible(struct kobject *kobj, struct attribute *attr, int die)
{
	/* Root bus 0x00 is valid only for pmu_idx = 0. */
	return pmu_iio_mapping_visible(kobj, attr, die, 0);
}

static ssize_t skx_iio_mapping_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct intel_uncore_pmu *pmu = dev_to_uncore_pmu(dev);
	struct dev_ext_attribute *ea = to_dev_ext_attribute(attr);
	long die = (long)ea->var;

	return sprintf(buf, "%04x:%02x\n", pmu->type->topology[die].segment,
					   skx_iio_stack(pmu, die));
}

static int skx_msr_cpu_bus_read(int cpu, u64 *topology)
{
	u64 msr_value;

	if (rdmsrl_on_cpu(cpu, SKX_MSR_CPU_BUS_NUMBER, &msr_value) ||
			!(msr_value & SKX_MSR_CPU_BUS_VALID_BIT))
		return -ENXIO;

	*topology = msr_value;

	return 0;
}

static int die_to_cpu(int die)
{
	int res = 0, cpu, current_die;
	/*
	 * Using cpus_read_lock() to ensure cpu is not going down between
	 * looking at cpu_online_mask.
	 */
	cpus_read_lock();
	for_each_online_cpu(cpu) {
		current_die = topology_logical_die_id(cpu);
		if (current_die == die) {
			res = cpu;
			break;
		}
	}
	cpus_read_unlock();
	return res;
}

static int skx_iio_get_topology(struct intel_uncore_type *type)
{
	int die, ret = -EPERM;

	type->topology = kcalloc(uncore_max_dies(), sizeof(*type->topology),
				 GFP_KERNEL);
	if (!type->topology)
		return -ENOMEM;

	for (die = 0; die < uncore_max_dies(); die++) {
		ret = skx_msr_cpu_bus_read(die_to_cpu(die),
					   &type->topology[die].configuration);
		if (ret)
			break;

		ret = uncore_die_to_segment(die);
		if (ret < 0)
			break;

		type->topology[die].segment = ret;
	}

	if (ret < 0) {
		kfree(type->topology);
		type->topology = NULL;
	}

	return ret;
}

static struct attribute_group skx_iio_mapping_group = {
	.is_visible	= skx_iio_mapping_visible,
};

static const struct attribute_group *skx_iio_attr_update[] = {
	&skx_iio_mapping_group,
	NULL,
};

static int
pmu_iio_set_mapping(struct intel_uncore_type *type, struct attribute_group *ag)
{
	char buf[64];
	int ret;
	long die = -1;
	struct attribute **attrs = NULL;
	struct dev_ext_attribute *eas = NULL;

	ret = type->get_topology(type);
	if (ret < 0)
		goto clear_attr_update;

	ret = -ENOMEM;

	/* One more for NULL. */
	attrs = kcalloc((uncore_max_dies() + 1), sizeof(*attrs), GFP_KERNEL);
	if (!attrs)
		goto clear_topology;

	eas = kcalloc(uncore_max_dies(), sizeof(*eas), GFP_KERNEL);
	if (!eas)
		goto clear_attrs;

	for (die = 0; die < uncore_max_dies(); die++) {
		sprintf(buf, "die%ld", die);
		sysfs_attr_init(&eas[die].attr.attr);
		eas[die].attr.attr.name = kstrdup(buf, GFP_KERNEL);
		if (!eas[die].attr.attr.name)
			goto err;
		eas[die].attr.attr.mode = 0444;
		eas[die].attr.show = skx_iio_mapping_show;
		eas[die].attr.store = NULL;
		eas[die].var = (void *)die;
		attrs[die] = &eas[die].attr.attr;
	}
	ag->attrs = attrs;

	return 0;
err:
	for (; die >= 0; die--)
		kfree(eas[die].attr.attr.name);
	kfree(eas);
clear_attrs:
	kfree(attrs);
clear_topology:
	kfree(type->topology);
clear_attr_update:
	type->attr_update = NULL;
	return ret;
}

static void
pmu_iio_cleanup_mapping(struct intel_uncore_type *type, struct attribute_group *ag)
{
	struct attribute **attr = ag->attrs;

	if (!attr)
		return;

	for (; *attr; attr++)
		kfree((*attr)->name);
	kfree(attr_to_ext_attr(*ag->attrs));
	kfree(ag->attrs);
	ag->attrs = NULL;
	kfree(type->topology);
}

static int skx_iio_set_mapping(struct intel_uncore_type *type)
{
	return pmu_iio_set_mapping(type, &skx_iio_mapping_group);
}

static void skx_iio_cleanup_mapping(struct intel_uncore_type *type)
{
	pmu_iio_cleanup_mapping(type, &skx_iio_mapping_group);
}

static struct intel_uncore_type skx_uncore_iio = {
	.name			= "iio",
	.num_counters		= 4,
	.num_boxes		= 6,
	.perf_ctr_bits		= 48,
	.event_ctl		= SKX_IIO0_MSR_PMON_CTL0,
	.perf_ctr		= SKX_IIO0_MSR_PMON_CTR0,
	.event_mask		= SKX_IIO_PMON_RAW_EVENT_MASK,
	.event_mask_ext		= SKX_IIO_PMON_RAW_EVENT_MASK_EXT,
	.box_ctl		= SKX_IIO0_MSR_PMON_BOX_CTL,
	.msr_offset		= SKX_IIO_MSR_OFFSET,
	.constraints		= skx_uncore_iio_constraints,
	.ops			= &skx_uncore_iio_ops,
	.format_group		= &skx_uncore_iio_format_group,
	.attr_update		= skx_iio_attr_update,
	.get_topology		= skx_iio_get_topology,
	.set_mapping		= skx_iio_set_mapping,
	.cleanup_mapping	= skx_iio_cleanup_mapping,
};

enum perf_uncore_iio_freerunning_type_id {
	SKX_IIO_MSR_IOCLK			= 0,
	SKX_IIO_MSR_BW				= 1,
	SKX_IIO_MSR_UTIL			= 2,

	SKX_IIO_FREERUNNING_TYPE_MAX,
};


static struct freerunning_counters skx_iio_freerunning[] = {
	[SKX_IIO_MSR_IOCLK]	= { 0xa45, 0x1, 0x20, 1, 36 },
	[SKX_IIO_MSR_BW]	= { 0xb00, 0x1, 0x10, 8, 36 },
	[SKX_IIO_MSR_UTIL]	= { 0xb08, 0x1, 0x10, 8, 36 },
};

static struct uncore_event_desc skx_uncore_iio_freerunning_events[] = {
	/* Free-Running IO CLOCKS Counter */
	INTEL_UNCORE_EVENT_DESC(ioclk,			"event=0xff,umask=0x10"),
	/* Free-Running IIO BANDWIDTH Counters */
	INTEL_UNCORE_EVENT_DESC(bw_in_port0,		"event=0xff,umask=0x20"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port0.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port0.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port1,		"event=0xff,umask=0x21"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port1.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port1.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port2,		"event=0xff,umask=0x22"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port2.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port2.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port3,		"event=0xff,umask=0x23"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port3.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port3.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port0,		"event=0xff,umask=0x24"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port0.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port0.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port1,		"event=0xff,umask=0x25"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port1.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port1.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port2,		"event=0xff,umask=0x26"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port2.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port2.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port3,		"event=0xff,umask=0x27"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port3.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port3.unit,	"MiB"),
	/* Free-running IIO UTILIZATION Counters */
	INTEL_UNCORE_EVENT_DESC(util_in_port0,		"event=0xff,umask=0x30"),
	INTEL_UNCORE_EVENT_DESC(util_out_port0,		"event=0xff,umask=0x31"),
	INTEL_UNCORE_EVENT_DESC(util_in_port1,		"event=0xff,umask=0x32"),
	INTEL_UNCORE_EVENT_DESC(util_out_port1,		"event=0xff,umask=0x33"),
	INTEL_UNCORE_EVENT_DESC(util_in_port2,		"event=0xff,umask=0x34"),
	INTEL_UNCORE_EVENT_DESC(util_out_port2,		"event=0xff,umask=0x35"),
	INTEL_UNCORE_EVENT_DESC(util_in_port3,		"event=0xff,umask=0x36"),
	INTEL_UNCORE_EVENT_DESC(util_out_port3,		"event=0xff,umask=0x37"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_ops skx_uncore_iio_freerunning_ops = {
	.read_counter		= uncore_msr_read_counter,
	.hw_config		= uncore_freerunning_hw_config,
};

static struct attribute *skx_uncore_iio_freerunning_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	NULL,
};

static const struct attribute_group skx_uncore_iio_freerunning_format_group = {
	.name = "format",
	.attrs = skx_uncore_iio_freerunning_formats_attr,
};

static struct intel_uncore_type skx_uncore_iio_free_running = {
	.name			= "iio_free_running",
	.num_counters		= 17,
	.num_boxes		= 6,
	.num_freerunning_types	= SKX_IIO_FREERUNNING_TYPE_MAX,
	.freerunning		= skx_iio_freerunning,
	.ops			= &skx_uncore_iio_freerunning_ops,
	.event_descs		= skx_uncore_iio_freerunning_events,
	.format_group		= &skx_uncore_iio_freerunning_format_group,
};

static struct attribute *skx_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static const struct attribute_group skx_uncore_format_group = {
	.name = "format",
	.attrs = skx_uncore_formats_attr,
};

static struct intel_uncore_type skx_uncore_irp = {
	.name			= "irp",
	.num_counters		= 2,
	.num_boxes		= 6,
	.perf_ctr_bits		= 48,
	.event_ctl		= SKX_IRP0_MSR_PMON_CTL0,
	.perf_ctr		= SKX_IRP0_MSR_PMON_CTR0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl		= SKX_IRP0_MSR_PMON_BOX_CTL,
	.msr_offset		= SKX_IRP_MSR_OFFSET,
	.ops			= &skx_uncore_iio_ops,
	.format_group		= &skx_uncore_format_group,
};

static struct attribute *skx_uncore_pcu_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_occ_invert.attr,
	&format_attr_occ_edge_det.attr,
	&format_attr_filter_band0.attr,
	&format_attr_filter_band1.attr,
	&format_attr_filter_band2.attr,
	&format_attr_filter_band3.attr,
	NULL,
};

static struct attribute_group skx_uncore_pcu_format_group = {
	.name = "format",
	.attrs = skx_uncore_pcu_formats_attr,
};

static struct intel_uncore_ops skx_uncore_pcu_ops = {
	IVBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= hswep_pcu_hw_config,
	.get_constraint		= snbep_pcu_get_constraint,
	.put_constraint		= snbep_pcu_put_constraint,
};

static struct intel_uncore_type skx_uncore_pcu = {
	.name			= "pcu",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= HSWEP_PCU_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_PCU_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PCU_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_PCU_MSR_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &skx_uncore_pcu_ops,
	.format_group		= &skx_uncore_pcu_format_group,
};

static struct intel_uncore_type *skx_msr_uncores[] = {
	&skx_uncore_ubox,
	&skx_uncore_chabox,
	&skx_uncore_iio,
	&skx_uncore_iio_free_running,
	&skx_uncore_irp,
	&skx_uncore_pcu,
	NULL,
};

/*
 * To determine the number of CHAs, it should read bits 27:0 in the CAPID6
 * register which located at Device 30, Function 3, Offset 0x9C. PCI ID 0x2083.
 */
#define SKX_CAPID6		0x9c
#define SKX_CHA_BIT_MASK	GENMASK(27, 0)

static int skx_count_chabox(void)
{
	struct pci_dev *dev = NULL;
	u32 val = 0;

	dev = pci_get_device(PCI_VENDOR_ID_INTEL, 0x2083, dev);
	if (!dev)
		goto out;

	pci_read_config_dword(dev, SKX_CAPID6, &val);
	val &= SKX_CHA_BIT_MASK;
out:
	pci_dev_put(dev);
	return hweight32(val);
}

void skx_uncore_cpu_init(void)
{
	skx_uncore_chabox.num_boxes = skx_count_chabox();
	uncore_msr_uncores = skx_msr_uncores;
}

static struct intel_uncore_type skx_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 6,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTR,
	.fixed_ctl	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTL,
	.event_descs	= hswep_uncore_imc_events,
	.perf_ctr	= SNBEP_PCI_PMON_CTR0,
	.event_ctl	= SNBEP_PCI_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl	= SNBEP_PCI_PMON_BOX_CTL,
	.ops		= &ivbep_uncore_pci_ops,
	.format_group	= &skx_uncore_format_group,
};

static struct attribute *skx_upi_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask_ext.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static const struct attribute_group skx_upi_uncore_format_group = {
	.name = "format",
	.attrs = skx_upi_uncore_formats_attr,
};

static void skx_upi_uncore_pci_init_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;

	__set_bit(UNCORE_BOX_FLAG_CTL_OFFS8, &box->flags);
	pci_write_config_dword(pdev, SKX_UPI_PCI_PMON_BOX_CTL, IVBEP_PMON_BOX_CTL_INT);
}

static struct intel_uncore_ops skx_upi_uncore_pci_ops = {
	.init_box	= skx_upi_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= snbep_uncore_pci_disable_event,
	.enable_event	= snbep_uncore_pci_enable_event,
	.read_counter	= snbep_uncore_pci_read_counter,
};

static struct intel_uncore_type skx_uncore_upi = {
	.name		= "upi",
	.num_counters   = 4,
	.num_boxes	= 3,
	.perf_ctr_bits	= 48,
	.perf_ctr	= SKX_UPI_PCI_PMON_CTR0,
	.event_ctl	= SKX_UPI_PCI_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.event_mask_ext = SKX_UPI_CTL_UMASK_EXT,
	.box_ctl	= SKX_UPI_PCI_PMON_BOX_CTL,
	.ops		= &skx_upi_uncore_pci_ops,
	.format_group	= &skx_upi_uncore_format_group,
};

static void skx_m2m_uncore_pci_init_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;

	__set_bit(UNCORE_BOX_FLAG_CTL_OFFS8, &box->flags);
	pci_write_config_dword(pdev, SKX_M2M_PCI_PMON_BOX_CTL, IVBEP_PMON_BOX_CTL_INT);
}

static struct intel_uncore_ops skx_m2m_uncore_pci_ops = {
	.init_box	= skx_m2m_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= snbep_uncore_pci_disable_event,
	.enable_event	= snbep_uncore_pci_enable_event,
	.read_counter	= snbep_uncore_pci_read_counter,
};

static struct intel_uncore_type skx_uncore_m2m = {
	.name		= "m2m",
	.num_counters   = 4,
	.num_boxes	= 2,
	.perf_ctr_bits	= 48,
	.perf_ctr	= SKX_M2M_PCI_PMON_CTR0,
	.event_ctl	= SKX_M2M_PCI_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl	= SKX_M2M_PCI_PMON_BOX_CTL,
	.ops		= &skx_m2m_uncore_pci_ops,
	.format_group	= &skx_uncore_format_group,
};

static struct event_constraint skx_uncore_m2pcie_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type skx_uncore_m2pcie = {
	.name		= "m2pcie",
	.num_counters   = 4,
	.num_boxes	= 4,
	.perf_ctr_bits	= 48,
	.constraints	= skx_uncore_m2pcie_constraints,
	.perf_ctr	= SNBEP_PCI_PMON_CTR0,
	.event_ctl	= SNBEP_PCI_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl	= SNBEP_PCI_PMON_BOX_CTL,
	.ops		= &ivbep_uncore_pci_ops,
	.format_group	= &skx_uncore_format_group,
};

static struct event_constraint skx_uncore_m3upi_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x1d, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x1e, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x40, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x4e, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x4f, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x50, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x51, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x52, 0x7),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type skx_uncore_m3upi = {
	.name		= "m3upi",
	.num_counters   = 3,
	.num_boxes	= 3,
	.perf_ctr_bits	= 48,
	.constraints	= skx_uncore_m3upi_constraints,
	.perf_ctr	= SNBEP_PCI_PMON_CTR0,
	.event_ctl	= SNBEP_PCI_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl	= SNBEP_PCI_PMON_BOX_CTL,
	.ops		= &ivbep_uncore_pci_ops,
	.format_group	= &skx_uncore_format_group,
};

enum {
	SKX_PCI_UNCORE_IMC,
	SKX_PCI_UNCORE_M2M,
	SKX_PCI_UNCORE_UPI,
	SKX_PCI_UNCORE_M2PCIE,
	SKX_PCI_UNCORE_M3UPI,
};

static struct intel_uncore_type *skx_pci_uncores[] = {
	[SKX_PCI_UNCORE_IMC]	= &skx_uncore_imc,
	[SKX_PCI_UNCORE_M2M]	= &skx_uncore_m2m,
	[SKX_PCI_UNCORE_UPI]	= &skx_uncore_upi,
	[SKX_PCI_UNCORE_M2PCIE]	= &skx_uncore_m2pcie,
	[SKX_PCI_UNCORE_M3UPI]	= &skx_uncore_m3upi,
	NULL,
};

static const struct pci_device_id skx_uncore_pci_ids[] = {
	{ /* MC0 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2042),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(10, 2, SKX_PCI_UNCORE_IMC, 0),
	},
	{ /* MC0 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2046),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(10, 6, SKX_PCI_UNCORE_IMC, 1),
	},
	{ /* MC0 Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x204a),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(11, 2, SKX_PCI_UNCORE_IMC, 2),
	},
	{ /* MC1 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2042),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(12, 2, SKX_PCI_UNCORE_IMC, 3),
	},
	{ /* MC1 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2046),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(12, 6, SKX_PCI_UNCORE_IMC, 4),
	},
	{ /* MC1 Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x204a),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(13, 2, SKX_PCI_UNCORE_IMC, 5),
	},
	{ /* M2M0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2066),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(8, 0, SKX_PCI_UNCORE_M2M, 0),
	},
	{ /* M2M1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2066),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(9, 0, SKX_PCI_UNCORE_M2M, 1),
	},
	{ /* UPI0 Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2058),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(14, 0, SKX_PCI_UNCORE_UPI, 0),
	},
	{ /* UPI0 Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2058),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(15, 0, SKX_PCI_UNCORE_UPI, 1),
	},
	{ /* UPI1 Link 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2058),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(16, 0, SKX_PCI_UNCORE_UPI, 2),
	},
	{ /* M2PCIe 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2088),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(21, 1, SKX_PCI_UNCORE_M2PCIE, 0),
	},
	{ /* M2PCIe 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2088),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(22, 1, SKX_PCI_UNCORE_M2PCIE, 1),
	},
	{ /* M2PCIe 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2088),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(23, 1, SKX_PCI_UNCORE_M2PCIE, 2),
	},
	{ /* M2PCIe 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2088),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(21, 5, SKX_PCI_UNCORE_M2PCIE, 3),
	},
	{ /* M3UPI0 Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x204D),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(18, 1, SKX_PCI_UNCORE_M3UPI, 0),
	},
	{ /* M3UPI0 Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x204E),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(18, 2, SKX_PCI_UNCORE_M3UPI, 1),
	},
	{ /* M3UPI1 Link 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x204D),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(18, 5, SKX_PCI_UNCORE_M3UPI, 2),
	},
	{ /* end: all zeroes */ }
};


static struct pci_driver skx_uncore_pci_driver = {
	.name		= "skx_uncore",
	.id_table	= skx_uncore_pci_ids,
};

int skx_uncore_pci_init(void)
{
	/* need to double check pci address */
	int ret = snbep_pci2phy_map_init(0x2014, SKX_CPUNODEID, SKX_GIDNIDMAP, false);

	if (ret)
		return ret;

	uncore_pci_uncores = skx_pci_uncores;
	uncore_pci_driver = &skx_uncore_pci_driver;
	return 0;
}

/* end of SKX uncore support */

/* SNR uncore support */

static struct intel_uncore_type snr_uncore_ubox = {
	.name			= "ubox",
	.num_counters		= 2,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= SNR_U_MSR_PMON_CTR0,
	.event_ctl		= SNR_U_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= SNR_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl		= SNR_U_MSR_PMON_UCLK_FIXED_CTL,
	.ops			= &ivbep_uncore_msr_ops,
	.format_group		= &ivbep_uncore_format_group,
};

static struct attribute *snr_uncore_cha_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask_ext2.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid5.attr,
	NULL,
};
static const struct attribute_group snr_uncore_chabox_format_group = {
	.name = "format",
	.attrs = snr_uncore_cha_formats_attr,
};

static int snr_cha_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;

	reg1->reg = SNR_C0_MSR_PMON_BOX_FILTER0 +
		    box->pmu->type->msr_offset * box->pmu->pmu_idx;
	reg1->config = event->attr.config1 & SKX_CHA_MSR_PMON_BOX_FILTER_TID;
	reg1->idx = 0;

	return 0;
}

static void snr_cha_enable_event(struct intel_uncore_box *box,
				   struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;

	if (reg1->idx != EXTRA_REG_NONE)
		wrmsrl(reg1->reg, reg1->config);

	wrmsrl(hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static struct intel_uncore_ops snr_uncore_chabox_ops = {
	.init_box		= ivbep_uncore_msr_init_box,
	.disable_box		= snbep_uncore_msr_disable_box,
	.enable_box		= snbep_uncore_msr_enable_box,
	.disable_event		= snbep_uncore_msr_disable_event,
	.enable_event		= snr_cha_enable_event,
	.read_counter		= uncore_msr_read_counter,
	.hw_config		= snr_cha_hw_config,
};

static struct intel_uncore_type snr_uncore_chabox = {
	.name			= "cha",
	.num_counters		= 4,
	.num_boxes		= 6,
	.perf_ctr_bits		= 48,
	.event_ctl		= SNR_CHA_MSR_PMON_CTL0,
	.perf_ctr		= SNR_CHA_MSR_PMON_CTR0,
	.box_ctl		= SNR_CHA_MSR_PMON_BOX_CTL,
	.msr_offset		= HSWEP_CBO_MSR_OFFSET,
	.event_mask		= HSWEP_S_MSR_PMON_RAW_EVENT_MASK,
	.event_mask_ext		= SNR_CHA_RAW_EVENT_MASK_EXT,
	.ops			= &snr_uncore_chabox_ops,
	.format_group		= &snr_uncore_chabox_format_group,
};

static struct attribute *snr_uncore_iio_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh9.attr,
	&format_attr_ch_mask2.attr,
	&format_attr_fc_mask2.attr,
	NULL,
};

static const struct attribute_group snr_uncore_iio_format_group = {
	.name = "format",
	.attrs = snr_uncore_iio_formats_attr,
};

static umode_t
snr_iio_mapping_visible(struct kobject *kobj, struct attribute *attr, int die)
{
	/* Root bus 0x00 is valid only for pmu_idx = 1. */
	return pmu_iio_mapping_visible(kobj, attr, die, 1);
}

static struct attribute_group snr_iio_mapping_group = {
	.is_visible	= snr_iio_mapping_visible,
};

static const struct attribute_group *snr_iio_attr_update[] = {
	&snr_iio_mapping_group,
	NULL,
};

static int sad_cfg_iio_topology(struct intel_uncore_type *type, u8 *sad_pmon_mapping)
{
	u32 sad_cfg;
	int die, stack_id, ret = -EPERM;
	struct pci_dev *dev = NULL;

	type->topology = kcalloc(uncore_max_dies(), sizeof(*type->topology),
				 GFP_KERNEL);
	if (!type->topology)
		return -ENOMEM;

	while ((dev = pci_get_device(PCI_VENDOR_ID_INTEL, SNR_ICX_MESH2IIO_MMAP_DID, dev))) {
		ret = pci_read_config_dword(dev, SNR_ICX_SAD_CONTROL_CFG, &sad_cfg);
		if (ret) {
			ret = pcibios_err_to_errno(ret);
			break;
		}

		die = uncore_pcibus_to_dieid(dev->bus);
		stack_id = SAD_CONTROL_STACK_ID(sad_cfg);
		if (die < 0 || stack_id >= type->num_boxes) {
			ret = -EPERM;
			break;
		}

		/* Convert stack id from SAD_CONTROL to PMON notation. */
		stack_id = sad_pmon_mapping[stack_id];

		((u8 *)&(type->topology[die].configuration))[stack_id] = dev->bus->number;
		type->topology[die].segment = pci_domain_nr(dev->bus);
	}

	if (ret) {
		kfree(type->topology);
		type->topology = NULL;
	}

	return ret;
}

/*
 * SNR has a static mapping of stack IDs from SAD_CONTROL_CFG notation to PMON
 */
enum {
	SNR_QAT_PMON_ID,
	SNR_CBDMA_DMI_PMON_ID,
	SNR_NIS_PMON_ID,
	SNR_DLB_PMON_ID,
	SNR_PCIE_GEN3_PMON_ID
};

static u8 snr_sad_pmon_mapping[] = {
	SNR_CBDMA_DMI_PMON_ID,
	SNR_PCIE_GEN3_PMON_ID,
	SNR_DLB_PMON_ID,
	SNR_NIS_PMON_ID,
	SNR_QAT_PMON_ID
};

static int snr_iio_get_topology(struct intel_uncore_type *type)
{
	return sad_cfg_iio_topology(type, snr_sad_pmon_mapping);
}

static int snr_iio_set_mapping(struct intel_uncore_type *type)
{
	return pmu_iio_set_mapping(type, &snr_iio_mapping_group);
}

static void snr_iio_cleanup_mapping(struct intel_uncore_type *type)
{
	pmu_iio_cleanup_mapping(type, &snr_iio_mapping_group);
}

static struct intel_uncore_type snr_uncore_iio = {
	.name			= "iio",
	.num_counters		= 4,
	.num_boxes		= 5,
	.perf_ctr_bits		= 48,
	.event_ctl		= SNR_IIO_MSR_PMON_CTL0,
	.perf_ctr		= SNR_IIO_MSR_PMON_CTR0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.event_mask_ext		= SNR_IIO_PMON_RAW_EVENT_MASK_EXT,
	.box_ctl		= SNR_IIO_MSR_PMON_BOX_CTL,
	.msr_offset		= SNR_IIO_MSR_OFFSET,
	.ops			= &ivbep_uncore_msr_ops,
	.format_group		= &snr_uncore_iio_format_group,
	.attr_update		= snr_iio_attr_update,
	.get_topology		= snr_iio_get_topology,
	.set_mapping		= snr_iio_set_mapping,
	.cleanup_mapping	= snr_iio_cleanup_mapping,
};

static struct intel_uncore_type snr_uncore_irp = {
	.name			= "irp",
	.num_counters		= 2,
	.num_boxes		= 5,
	.perf_ctr_bits		= 48,
	.event_ctl		= SNR_IRP0_MSR_PMON_CTL0,
	.perf_ctr		= SNR_IRP0_MSR_PMON_CTR0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNR_IRP0_MSR_PMON_BOX_CTL,
	.msr_offset		= SNR_IRP_MSR_OFFSET,
	.ops			= &ivbep_uncore_msr_ops,
	.format_group		= &ivbep_uncore_format_group,
};

static struct intel_uncore_type snr_uncore_m2pcie = {
	.name		= "m2pcie",
	.num_counters	= 4,
	.num_boxes	= 5,
	.perf_ctr_bits	= 48,
	.event_ctl	= SNR_M2PCIE_MSR_PMON_CTL0,
	.perf_ctr	= SNR_M2PCIE_MSR_PMON_CTR0,
	.box_ctl	= SNR_M2PCIE_MSR_PMON_BOX_CTL,
	.msr_offset	= SNR_M2PCIE_MSR_OFFSET,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.ops		= &ivbep_uncore_msr_ops,
	.format_group	= &ivbep_uncore_format_group,
};

static int snr_pcu_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	int ev_sel = hwc->config & SNBEP_PMON_CTL_EV_SEL_MASK;

	if (ev_sel >= 0xb && ev_sel <= 0xe) {
		reg1->reg = SNR_PCU_MSR_PMON_BOX_FILTER;
		reg1->idx = ev_sel - 0xb;
		reg1->config = event->attr.config1 & (0xff << reg1->idx);
	}
	return 0;
}

static struct intel_uncore_ops snr_uncore_pcu_ops = {
	IVBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= snr_pcu_hw_config,
	.get_constraint		= snbep_pcu_get_constraint,
	.put_constraint		= snbep_pcu_put_constraint,
};

static struct intel_uncore_type snr_uncore_pcu = {
	.name			= "pcu",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNR_PCU_MSR_PMON_CTR0,
	.event_ctl		= SNR_PCU_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNR_PCU_MSR_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &snr_uncore_pcu_ops,
	.format_group		= &skx_uncore_pcu_format_group,
};

enum perf_uncore_snr_iio_freerunning_type_id {
	SNR_IIO_MSR_IOCLK,
	SNR_IIO_MSR_BW_IN,

	SNR_IIO_FREERUNNING_TYPE_MAX,
};

static struct freerunning_counters snr_iio_freerunning[] = {
	[SNR_IIO_MSR_IOCLK]	= { 0x1eac, 0x1, 0x10, 1, 48 },
	[SNR_IIO_MSR_BW_IN]	= { 0x1f00, 0x1, 0x10, 8, 48 },
};

static struct uncore_event_desc snr_uncore_iio_freerunning_events[] = {
	/* Free-Running IIO CLOCKS Counter */
	INTEL_UNCORE_EVENT_DESC(ioclk,			"event=0xff,umask=0x10"),
	/* Free-Running IIO BANDWIDTH IN Counters */
	INTEL_UNCORE_EVENT_DESC(bw_in_port0,		"event=0xff,umask=0x20"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port0.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port0.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port1,		"event=0xff,umask=0x21"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port1.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port1.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port2,		"event=0xff,umask=0x22"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port2.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port2.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port3,		"event=0xff,umask=0x23"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port3.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port3.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port4,		"event=0xff,umask=0x24"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port4.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port4.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port5,		"event=0xff,umask=0x25"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port5.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port5.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port6,		"event=0xff,umask=0x26"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port6.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port6.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port7,		"event=0xff,umask=0x27"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port7.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port7.unit,	"MiB"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_type snr_uncore_iio_free_running = {
	.name			= "iio_free_running",
	.num_counters		= 9,
	.num_boxes		= 5,
	.num_freerunning_types	= SNR_IIO_FREERUNNING_TYPE_MAX,
	.freerunning		= snr_iio_freerunning,
	.ops			= &skx_uncore_iio_freerunning_ops,
	.event_descs		= snr_uncore_iio_freerunning_events,
	.format_group		= &skx_uncore_iio_freerunning_format_group,
};

static struct intel_uncore_type *snr_msr_uncores[] = {
	&snr_uncore_ubox,
	&snr_uncore_chabox,
	&snr_uncore_iio,
	&snr_uncore_irp,
	&snr_uncore_m2pcie,
	&snr_uncore_pcu,
	&snr_uncore_iio_free_running,
	NULL,
};

void snr_uncore_cpu_init(void)
{
	uncore_msr_uncores = snr_msr_uncores;
}

static void snr_m2m_uncore_pci_init_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);

	__set_bit(UNCORE_BOX_FLAG_CTL_OFFS8, &box->flags);
	pci_write_config_dword(pdev, box_ctl, IVBEP_PMON_BOX_CTL_INT);
}

static struct intel_uncore_ops snr_m2m_uncore_pci_ops = {
	.init_box	= snr_m2m_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= snbep_uncore_pci_disable_event,
	.enable_event	= snbep_uncore_pci_enable_event,
	.read_counter	= snbep_uncore_pci_read_counter,
};

static struct attribute *snr_m2m_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask_ext3.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static const struct attribute_group snr_m2m_uncore_format_group = {
	.name = "format",
	.attrs = snr_m2m_uncore_formats_attr,
};

static struct intel_uncore_type snr_uncore_m2m = {
	.name		= "m2m",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.perf_ctr	= SNR_M2M_PCI_PMON_CTR0,
	.event_ctl	= SNR_M2M_PCI_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.event_mask_ext	= SNR_M2M_PCI_PMON_UMASK_EXT,
	.box_ctl	= SNR_M2M_PCI_PMON_BOX_CTL,
	.ops		= &snr_m2m_uncore_pci_ops,
	.format_group	= &snr_m2m_uncore_format_group,
};

static void snr_uncore_pci_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, hwc->config_base, (u32)(hwc->config | SNBEP_PMON_CTL_EN));
	pci_write_config_dword(pdev, hwc->config_base + 4, (u32)(hwc->config >> 32));
}

static struct intel_uncore_ops snr_pcie3_uncore_pci_ops = {
	.init_box	= snr_m2m_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= snbep_uncore_pci_disable_event,
	.enable_event	= snr_uncore_pci_enable_event,
	.read_counter	= snbep_uncore_pci_read_counter,
};

static struct intel_uncore_type snr_uncore_pcie3 = {
	.name		= "pcie3",
	.num_counters	= 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.perf_ctr	= SNR_PCIE3_PCI_PMON_CTR0,
	.event_ctl	= SNR_PCIE3_PCI_PMON_CTL0,
	.event_mask	= SKX_IIO_PMON_RAW_EVENT_MASK,
	.event_mask_ext	= SKX_IIO_PMON_RAW_EVENT_MASK_EXT,
	.box_ctl	= SNR_PCIE3_PCI_PMON_BOX_CTL,
	.ops		= &snr_pcie3_uncore_pci_ops,
	.format_group	= &skx_uncore_iio_format_group,
};

enum {
	SNR_PCI_UNCORE_M2M,
	SNR_PCI_UNCORE_PCIE3,
};

static struct intel_uncore_type *snr_pci_uncores[] = {
	[SNR_PCI_UNCORE_M2M]		= &snr_uncore_m2m,
	[SNR_PCI_UNCORE_PCIE3]		= &snr_uncore_pcie3,
	NULL,
};

static const struct pci_device_id snr_uncore_pci_ids[] = {
	{ /* M2M */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x344a),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(12, 0, SNR_PCI_UNCORE_M2M, 0),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver snr_uncore_pci_driver = {
	.name		= "snr_uncore",
	.id_table	= snr_uncore_pci_ids,
};

static const struct pci_device_id snr_uncore_pci_sub_ids[] = {
	{ /* PCIe3 RP */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x334a),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(4, 0, SNR_PCI_UNCORE_PCIE3, 0),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver snr_uncore_pci_sub_driver = {
	.name		= "snr_uncore_sub",
	.id_table	= snr_uncore_pci_sub_ids,
};

int snr_uncore_pci_init(void)
{
	/* SNR UBOX DID */
	int ret = snbep_pci2phy_map_init(0x3460, SKX_CPUNODEID,
					 SKX_GIDNIDMAP, true);

	if (ret)
		return ret;

	uncore_pci_uncores = snr_pci_uncores;
	uncore_pci_driver = &snr_uncore_pci_driver;
	uncore_pci_sub_driver = &snr_uncore_pci_sub_driver;
	return 0;
}

#define SNR_MC_DEVICE_ID	0x3451

static struct pci_dev *snr_uncore_get_mc_dev(unsigned int device, int id)
{
	struct pci_dev *mc_dev = NULL;
	int pkg;

	while (1) {
		mc_dev = pci_get_device(PCI_VENDOR_ID_INTEL, device, mc_dev);
		if (!mc_dev)
			break;
		pkg = uncore_pcibus_to_dieid(mc_dev->bus);
		if (pkg == id)
			break;
	}
	return mc_dev;
}

static int snr_uncore_mmio_map(struct intel_uncore_box *box,
			       unsigned int box_ctl, int mem_offset,
			       unsigned int device)
{
	struct pci_dev *pdev = snr_uncore_get_mc_dev(device, box->dieid);
	struct intel_uncore_type *type = box->pmu->type;
	resource_size_t addr;
	u32 pci_dword;

	if (!pdev)
		return -ENODEV;

	pci_read_config_dword(pdev, SNR_IMC_MMIO_BASE_OFFSET, &pci_dword);
	addr = ((resource_size_t)pci_dword & SNR_IMC_MMIO_BASE_MASK) << 23;

	pci_read_config_dword(pdev, mem_offset, &pci_dword);
	addr |= (pci_dword & SNR_IMC_MMIO_MEM0_MASK) << 12;

	addr += box_ctl;

	box->io_addr = ioremap(addr, type->mmio_map_size);
	if (!box->io_addr) {
		pr_warn("perf uncore: Failed to ioremap for %s.\n", type->name);
		return -EINVAL;
	}

	return 0;
}

static void __snr_uncore_mmio_init_box(struct intel_uncore_box *box,
				       unsigned int box_ctl, int mem_offset,
				       unsigned int device)
{
	if (!snr_uncore_mmio_map(box, box_ctl, mem_offset, device))
		writel(IVBEP_PMON_BOX_CTL_INT, box->io_addr);
}

static void snr_uncore_mmio_init_box(struct intel_uncore_box *box)
{
	__snr_uncore_mmio_init_box(box, uncore_mmio_box_ctl(box),
				   SNR_IMC_MMIO_MEM0_OFFSET,
				   SNR_MC_DEVICE_ID);
}

static void snr_uncore_mmio_disable_box(struct intel_uncore_box *box)
{
	u32 config;

	if (!box->io_addr)
		return;

	config = readl(box->io_addr);
	config |= SNBEP_PMON_BOX_CTL_FRZ;
	writel(config, box->io_addr);
}

static void snr_uncore_mmio_enable_box(struct intel_uncore_box *box)
{
	u32 config;

	if (!box->io_addr)
		return;

	config = readl(box->io_addr);
	config &= ~SNBEP_PMON_BOX_CTL_FRZ;
	writel(config, box->io_addr);
}

static void snr_uncore_mmio_enable_event(struct intel_uncore_box *box,
					   struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (!box->io_addr)
		return;

	if (!uncore_mmio_is_valid_offset(box, hwc->config_base))
		return;

	writel(hwc->config | SNBEP_PMON_CTL_EN,
	       box->io_addr + hwc->config_base);
}

static void snr_uncore_mmio_disable_event(struct intel_uncore_box *box,
					    struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (!box->io_addr)
		return;

	if (!uncore_mmio_is_valid_offset(box, hwc->config_base))
		return;

	writel(hwc->config, box->io_addr + hwc->config_base);
}

static struct intel_uncore_ops snr_uncore_mmio_ops = {
	.init_box	= snr_uncore_mmio_init_box,
	.exit_box	= uncore_mmio_exit_box,
	.disable_box	= snr_uncore_mmio_disable_box,
	.enable_box	= snr_uncore_mmio_enable_box,
	.disable_event	= snr_uncore_mmio_disable_event,
	.enable_event	= snr_uncore_mmio_enable_event,
	.read_counter	= uncore_mmio_read_counter,
};

static struct uncore_event_desc snr_uncore_imc_events[] = {
	INTEL_UNCORE_EVENT_DESC(clockticks,      "event=0x00,umask=0x00"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read,  "event=0x04,umask=0x0f"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read.scale, "6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read.unit, "MiB"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write, "event=0x04,umask=0x30"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write.scale, "6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write.unit, "MiB"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_type snr_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 2,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNR_IMC_MMIO_PMON_FIXED_CTR,
	.fixed_ctl	= SNR_IMC_MMIO_PMON_FIXED_CTL,
	.event_descs	= snr_uncore_imc_events,
	.perf_ctr	= SNR_IMC_MMIO_PMON_CTR0,
	.event_ctl	= SNR_IMC_MMIO_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl	= SNR_IMC_MMIO_PMON_BOX_CTL,
	.mmio_offset	= SNR_IMC_MMIO_OFFSET,
	.mmio_map_size	= SNR_IMC_MMIO_SIZE,
	.ops		= &snr_uncore_mmio_ops,
	.format_group	= &skx_uncore_format_group,
};

enum perf_uncore_snr_imc_freerunning_type_id {
	SNR_IMC_DCLK,
	SNR_IMC_DDR,

	SNR_IMC_FREERUNNING_TYPE_MAX,
};

static struct freerunning_counters snr_imc_freerunning[] = {
	[SNR_IMC_DCLK]	= { 0x22b0, 0x0, 0, 1, 48 },
	[SNR_IMC_DDR]	= { 0x2290, 0x8, 0, 2, 48 },
};

static struct uncore_event_desc snr_uncore_imc_freerunning_events[] = {
	INTEL_UNCORE_EVENT_DESC(dclk,		"event=0xff,umask=0x10"),

	INTEL_UNCORE_EVENT_DESC(read,		"event=0xff,umask=0x20"),
	INTEL_UNCORE_EVENT_DESC(read.scale,	"6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(read.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(write,		"event=0xff,umask=0x21"),
	INTEL_UNCORE_EVENT_DESC(write.scale,	"6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(write.unit,	"MiB"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_ops snr_uncore_imc_freerunning_ops = {
	.init_box	= snr_uncore_mmio_init_box,
	.exit_box	= uncore_mmio_exit_box,
	.read_counter	= uncore_mmio_read_counter,
	.hw_config	= uncore_freerunning_hw_config,
};

static struct intel_uncore_type snr_uncore_imc_free_running = {
	.name			= "imc_free_running",
	.num_counters		= 3,
	.num_boxes		= 1,
	.num_freerunning_types	= SNR_IMC_FREERUNNING_TYPE_MAX,
	.mmio_map_size		= SNR_IMC_MMIO_SIZE,
	.freerunning		= snr_imc_freerunning,
	.ops			= &snr_uncore_imc_freerunning_ops,
	.event_descs		= snr_uncore_imc_freerunning_events,
	.format_group		= &skx_uncore_iio_freerunning_format_group,
};

static struct intel_uncore_type *snr_mmio_uncores[] = {
	&snr_uncore_imc,
	&snr_uncore_imc_free_running,
	NULL,
};

void snr_uncore_mmio_init(void)
{
	uncore_mmio_uncores = snr_mmio_uncores;
}

/* end of SNR uncore support */

/* ICX uncore support */

static unsigned icx_cha_msr_offsets[] = {
	0x2a0, 0x2ae, 0x2bc, 0x2ca, 0x2d8, 0x2e6, 0x2f4, 0x302, 0x310,
	0x31e, 0x32c, 0x33a, 0x348, 0x356, 0x364, 0x372, 0x380, 0x38e,
	0x3aa, 0x3b8, 0x3c6, 0x3d4, 0x3e2, 0x3f0, 0x3fe, 0x40c, 0x41a,
	0x428, 0x436, 0x444, 0x452, 0x460, 0x46e, 0x47c, 0x0,   0xe,
	0x1c,  0x2a,  0x38,  0x46,
};

static int icx_cha_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	bool tie_en = !!(event->hw.config & SNBEP_CBO_PMON_CTL_TID_EN);

	if (tie_en) {
		reg1->reg = ICX_C34_MSR_PMON_BOX_FILTER0 +
			    icx_cha_msr_offsets[box->pmu->pmu_idx];
		reg1->config = event->attr.config1 & SKX_CHA_MSR_PMON_BOX_FILTER_TID;
		reg1->idx = 0;
	}

	return 0;
}

static struct intel_uncore_ops icx_uncore_chabox_ops = {
	.init_box		= ivbep_uncore_msr_init_box,
	.disable_box		= snbep_uncore_msr_disable_box,
	.enable_box		= snbep_uncore_msr_enable_box,
	.disable_event		= snbep_uncore_msr_disable_event,
	.enable_event		= snr_cha_enable_event,
	.read_counter		= uncore_msr_read_counter,
	.hw_config		= icx_cha_hw_config,
};

static struct intel_uncore_type icx_uncore_chabox = {
	.name			= "cha",
	.num_counters		= 4,
	.perf_ctr_bits		= 48,
	.event_ctl		= ICX_C34_MSR_PMON_CTL0,
	.perf_ctr		= ICX_C34_MSR_PMON_CTR0,
	.box_ctl		= ICX_C34_MSR_PMON_BOX_CTL,
	.msr_offsets		= icx_cha_msr_offsets,
	.event_mask		= HSWEP_S_MSR_PMON_RAW_EVENT_MASK,
	.event_mask_ext		= SNR_CHA_RAW_EVENT_MASK_EXT,
	.constraints		= skx_uncore_chabox_constraints,
	.ops			= &icx_uncore_chabox_ops,
	.format_group		= &snr_uncore_chabox_format_group,
};

static unsigned icx_msr_offsets[] = {
	0x0, 0x20, 0x40, 0x90, 0xb0, 0xd0,
};

static struct event_constraint icx_uncore_iio_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x02, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x03, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x83, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x88, 0xc),
	UNCORE_EVENT_CONSTRAINT(0xc0, 0xc),
	UNCORE_EVENT_CONSTRAINT(0xc5, 0xc),
	UNCORE_EVENT_CONSTRAINT(0xd5, 0xc),
	EVENT_CONSTRAINT_END
};

static umode_t
icx_iio_mapping_visible(struct kobject *kobj, struct attribute *attr, int die)
{
	/* Root bus 0x00 is valid only for pmu_idx = 5. */
	return pmu_iio_mapping_visible(kobj, attr, die, 5);
}

static struct attribute_group icx_iio_mapping_group = {
	.is_visible	= icx_iio_mapping_visible,
};

static const struct attribute_group *icx_iio_attr_update[] = {
	&icx_iio_mapping_group,
	NULL,
};

/*
 * ICX has a static mapping of stack IDs from SAD_CONTROL_CFG notation to PMON
 */
enum {
	ICX_PCIE1_PMON_ID,
	ICX_PCIE2_PMON_ID,
	ICX_PCIE3_PMON_ID,
	ICX_PCIE4_PMON_ID,
	ICX_PCIE5_PMON_ID,
	ICX_CBDMA_DMI_PMON_ID
};

static u8 icx_sad_pmon_mapping[] = {
	ICX_CBDMA_DMI_PMON_ID,
	ICX_PCIE1_PMON_ID,
	ICX_PCIE2_PMON_ID,
	ICX_PCIE3_PMON_ID,
	ICX_PCIE4_PMON_ID,
	ICX_PCIE5_PMON_ID,
};

static int icx_iio_get_topology(struct intel_uncore_type *type)
{
	return sad_cfg_iio_topology(type, icx_sad_pmon_mapping);
}

static int icx_iio_set_mapping(struct intel_uncore_type *type)
{
	return pmu_iio_set_mapping(type, &icx_iio_mapping_group);
}

static void icx_iio_cleanup_mapping(struct intel_uncore_type *type)
{
	pmu_iio_cleanup_mapping(type, &icx_iio_mapping_group);
}

static struct intel_uncore_type icx_uncore_iio = {
	.name			= "iio",
	.num_counters		= 4,
	.num_boxes		= 6,
	.perf_ctr_bits		= 48,
	.event_ctl		= ICX_IIO_MSR_PMON_CTL0,
	.perf_ctr		= ICX_IIO_MSR_PMON_CTR0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.event_mask_ext		= SNR_IIO_PMON_RAW_EVENT_MASK_EXT,
	.box_ctl		= ICX_IIO_MSR_PMON_BOX_CTL,
	.msr_offsets		= icx_msr_offsets,
	.constraints		= icx_uncore_iio_constraints,
	.ops			= &skx_uncore_iio_ops,
	.format_group		= &snr_uncore_iio_format_group,
	.attr_update		= icx_iio_attr_update,
	.get_topology		= icx_iio_get_topology,
	.set_mapping		= icx_iio_set_mapping,
	.cleanup_mapping	= icx_iio_cleanup_mapping,
};

static struct intel_uncore_type icx_uncore_irp = {
	.name			= "irp",
	.num_counters		= 2,
	.num_boxes		= 6,
	.perf_ctr_bits		= 48,
	.event_ctl		= ICX_IRP0_MSR_PMON_CTL0,
	.perf_ctr		= ICX_IRP0_MSR_PMON_CTR0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl		= ICX_IRP0_MSR_PMON_BOX_CTL,
	.msr_offsets		= icx_msr_offsets,
	.ops			= &ivbep_uncore_msr_ops,
	.format_group		= &ivbep_uncore_format_group,
};

static struct event_constraint icx_uncore_m2pcie_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x14, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2d, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type icx_uncore_m2pcie = {
	.name		= "m2pcie",
	.num_counters	= 4,
	.num_boxes	= 6,
	.perf_ctr_bits	= 48,
	.event_ctl	= ICX_M2PCIE_MSR_PMON_CTL0,
	.perf_ctr	= ICX_M2PCIE_MSR_PMON_CTR0,
	.box_ctl	= ICX_M2PCIE_MSR_PMON_BOX_CTL,
	.msr_offsets	= icx_msr_offsets,
	.constraints	= icx_uncore_m2pcie_constraints,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.ops		= &ivbep_uncore_msr_ops,
	.format_group	= &ivbep_uncore_format_group,
};

enum perf_uncore_icx_iio_freerunning_type_id {
	ICX_IIO_MSR_IOCLK,
	ICX_IIO_MSR_BW_IN,

	ICX_IIO_FREERUNNING_TYPE_MAX,
};

static unsigned icx_iio_clk_freerunning_box_offsets[] = {
	0x0, 0x20, 0x40, 0x90, 0xb0, 0xd0,
};

static unsigned icx_iio_bw_freerunning_box_offsets[] = {
	0x0, 0x10, 0x20, 0x90, 0xa0, 0xb0,
};

static struct freerunning_counters icx_iio_freerunning[] = {
	[ICX_IIO_MSR_IOCLK]	= { 0xa55, 0x1, 0x20, 1, 48, icx_iio_clk_freerunning_box_offsets },
	[ICX_IIO_MSR_BW_IN]	= { 0xaa0, 0x1, 0x10, 8, 48, icx_iio_bw_freerunning_box_offsets },
};

static struct uncore_event_desc icx_uncore_iio_freerunning_events[] = {
	/* Free-Running IIO CLOCKS Counter */
	INTEL_UNCORE_EVENT_DESC(ioclk,			"event=0xff,umask=0x10"),
	/* Free-Running IIO BANDWIDTH IN Counters */
	INTEL_UNCORE_EVENT_DESC(bw_in_port0,		"event=0xff,umask=0x20"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port0.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port0.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port1,		"event=0xff,umask=0x21"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port1.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port1.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port2,		"event=0xff,umask=0x22"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port2.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port2.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port3,		"event=0xff,umask=0x23"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port3.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port3.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port4,		"event=0xff,umask=0x24"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port4.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port4.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port5,		"event=0xff,umask=0x25"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port5.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port5.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port6,		"event=0xff,umask=0x26"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port6.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port6.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port7,		"event=0xff,umask=0x27"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port7.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port7.unit,	"MiB"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_type icx_uncore_iio_free_running = {
	.name			= "iio_free_running",
	.num_counters		= 9,
	.num_boxes		= 6,
	.num_freerunning_types	= ICX_IIO_FREERUNNING_TYPE_MAX,
	.freerunning		= icx_iio_freerunning,
	.ops			= &skx_uncore_iio_freerunning_ops,
	.event_descs		= icx_uncore_iio_freerunning_events,
	.format_group		= &skx_uncore_iio_freerunning_format_group,
};

static struct intel_uncore_type *icx_msr_uncores[] = {
	&skx_uncore_ubox,
	&icx_uncore_chabox,
	&icx_uncore_iio,
	&icx_uncore_irp,
	&icx_uncore_m2pcie,
	&skx_uncore_pcu,
	&icx_uncore_iio_free_running,
	NULL,
};

/*
 * To determine the number of CHAs, it should read CAPID6(Low) and CAPID7 (High)
 * registers which located at Device 30, Function 3
 */
#define ICX_CAPID6		0x9c
#define ICX_CAPID7		0xa0

static u64 icx_count_chabox(void)
{
	struct pci_dev *dev = NULL;
	u64 caps = 0;

	dev = pci_get_device(PCI_VENDOR_ID_INTEL, 0x345b, dev);
	if (!dev)
		goto out;

	pci_read_config_dword(dev, ICX_CAPID6, (u32 *)&caps);
	pci_read_config_dword(dev, ICX_CAPID7, (u32 *)&caps + 1);
out:
	pci_dev_put(dev);
	return hweight64(caps);
}

void icx_uncore_cpu_init(void)
{
	u64 num_boxes = icx_count_chabox();

	if (WARN_ON(num_boxes > ARRAY_SIZE(icx_cha_msr_offsets)))
		return;
	icx_uncore_chabox.num_boxes = num_boxes;
	uncore_msr_uncores = icx_msr_uncores;
}

static struct intel_uncore_type icx_uncore_m2m = {
	.name		= "m2m",
	.num_counters   = 4,
	.num_boxes	= 4,
	.perf_ctr_bits	= 48,
	.perf_ctr	= SNR_M2M_PCI_PMON_CTR0,
	.event_ctl	= SNR_M2M_PCI_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.event_mask_ext	= SNR_M2M_PCI_PMON_UMASK_EXT,
	.box_ctl	= SNR_M2M_PCI_PMON_BOX_CTL,
	.ops		= &snr_m2m_uncore_pci_ops,
	.format_group	= &snr_m2m_uncore_format_group,
};

static struct attribute *icx_upi_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask_ext4.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static const struct attribute_group icx_upi_uncore_format_group = {
	.name = "format",
	.attrs = icx_upi_uncore_formats_attr,
};

static struct intel_uncore_type icx_uncore_upi = {
	.name		= "upi",
	.num_counters   = 4,
	.num_boxes	= 3,
	.perf_ctr_bits	= 48,
	.perf_ctr	= ICX_UPI_PCI_PMON_CTR0,
	.event_ctl	= ICX_UPI_PCI_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.event_mask_ext = ICX_UPI_CTL_UMASK_EXT,
	.box_ctl	= ICX_UPI_PCI_PMON_BOX_CTL,
	.ops		= &skx_upi_uncore_pci_ops,
	.format_group	= &icx_upi_uncore_format_group,
};

static struct event_constraint icx_uncore_m3upi_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x1c, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x1d, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x1e, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x1f, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x40, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x4e, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x4f, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x50, 0x7),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type icx_uncore_m3upi = {
	.name		= "m3upi",
	.num_counters   = 4,
	.num_boxes	= 3,
	.perf_ctr_bits	= 48,
	.perf_ctr	= ICX_M3UPI_PCI_PMON_CTR0,
	.event_ctl	= ICX_M3UPI_PCI_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl	= ICX_M3UPI_PCI_PMON_BOX_CTL,
	.constraints	= icx_uncore_m3upi_constraints,
	.ops		= &ivbep_uncore_pci_ops,
	.format_group	= &skx_uncore_format_group,
};

enum {
	ICX_PCI_UNCORE_M2M,
	ICX_PCI_UNCORE_UPI,
	ICX_PCI_UNCORE_M3UPI,
};

static struct intel_uncore_type *icx_pci_uncores[] = {
	[ICX_PCI_UNCORE_M2M]		= &icx_uncore_m2m,
	[ICX_PCI_UNCORE_UPI]		= &icx_uncore_upi,
	[ICX_PCI_UNCORE_M3UPI]		= &icx_uncore_m3upi,
	NULL,
};

static const struct pci_device_id icx_uncore_pci_ids[] = {
	{ /* M2M 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x344a),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(12, 0, ICX_PCI_UNCORE_M2M, 0),
	},
	{ /* M2M 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x344a),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(13, 0, ICX_PCI_UNCORE_M2M, 1),
	},
	{ /* M2M 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x344a),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(14, 0, ICX_PCI_UNCORE_M2M, 2),
	},
	{ /* M2M 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x344a),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(15, 0, ICX_PCI_UNCORE_M2M, 3),
	},
	{ /* UPI Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x3441),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(2, 1, ICX_PCI_UNCORE_UPI, 0),
	},
	{ /* UPI Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x3441),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(3, 1, ICX_PCI_UNCORE_UPI, 1),
	},
	{ /* UPI Link 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x3441),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(4, 1, ICX_PCI_UNCORE_UPI, 2),
	},
	{ /* M3UPI Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x3446),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(5, 1, ICX_PCI_UNCORE_M3UPI, 0),
	},
	{ /* M3UPI Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x3446),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(6, 1, ICX_PCI_UNCORE_M3UPI, 1),
	},
	{ /* M3UPI Link 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x3446),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(7, 1, ICX_PCI_UNCORE_M3UPI, 2),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver icx_uncore_pci_driver = {
	.name		= "icx_uncore",
	.id_table	= icx_uncore_pci_ids,
};

int icx_uncore_pci_init(void)
{
	/* ICX UBOX DID */
	int ret = snbep_pci2phy_map_init(0x3450, SKX_CPUNODEID,
					 SKX_GIDNIDMAP, true);

	if (ret)
		return ret;

	uncore_pci_uncores = icx_pci_uncores;
	uncore_pci_driver = &icx_uncore_pci_driver;
	return 0;
}

static void icx_uncore_imc_init_box(struct intel_uncore_box *box)
{
	unsigned int box_ctl = box->pmu->type->box_ctl +
			       box->pmu->type->mmio_offset * (box->pmu->pmu_idx % ICX_NUMBER_IMC_CHN);
	int mem_offset = (box->pmu->pmu_idx / ICX_NUMBER_IMC_CHN) * ICX_IMC_MEM_STRIDE +
			 SNR_IMC_MMIO_MEM0_OFFSET;

	__snr_uncore_mmio_init_box(box, box_ctl, mem_offset,
				   SNR_MC_DEVICE_ID);
}

static struct intel_uncore_ops icx_uncore_mmio_ops = {
	.init_box	= icx_uncore_imc_init_box,
	.exit_box	= uncore_mmio_exit_box,
	.disable_box	= snr_uncore_mmio_disable_box,
	.enable_box	= snr_uncore_mmio_enable_box,
	.disable_event	= snr_uncore_mmio_disable_event,
	.enable_event	= snr_uncore_mmio_enable_event,
	.read_counter	= uncore_mmio_read_counter,
};

static struct intel_uncore_type icx_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 12,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNR_IMC_MMIO_PMON_FIXED_CTR,
	.fixed_ctl	= SNR_IMC_MMIO_PMON_FIXED_CTL,
	.event_descs	= hswep_uncore_imc_events,
	.perf_ctr	= SNR_IMC_MMIO_PMON_CTR0,
	.event_ctl	= SNR_IMC_MMIO_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl	= SNR_IMC_MMIO_PMON_BOX_CTL,
	.mmio_offset	= SNR_IMC_MMIO_OFFSET,
	.mmio_map_size	= SNR_IMC_MMIO_SIZE,
	.ops		= &icx_uncore_mmio_ops,
	.format_group	= &skx_uncore_format_group,
};

enum perf_uncore_icx_imc_freerunning_type_id {
	ICX_IMC_DCLK,
	ICX_IMC_DDR,
	ICX_IMC_DDRT,

	ICX_IMC_FREERUNNING_TYPE_MAX,
};

static struct freerunning_counters icx_imc_freerunning[] = {
	[ICX_IMC_DCLK]	= { 0x22b0, 0x0, 0, 1, 48 },
	[ICX_IMC_DDR]	= { 0x2290, 0x8, 0, 2, 48 },
	[ICX_IMC_DDRT]	= { 0x22a0, 0x8, 0, 2, 48 },
};

static struct uncore_event_desc icx_uncore_imc_freerunning_events[] = {
	INTEL_UNCORE_EVENT_DESC(dclk,			"event=0xff,umask=0x10"),

	INTEL_UNCORE_EVENT_DESC(read,			"event=0xff,umask=0x20"),
	INTEL_UNCORE_EVENT_DESC(read.scale,		"6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(read.unit,		"MiB"),
	INTEL_UNCORE_EVENT_DESC(write,			"event=0xff,umask=0x21"),
	INTEL_UNCORE_EVENT_DESC(write.scale,		"6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(write.unit,		"MiB"),

	INTEL_UNCORE_EVENT_DESC(ddrt_read,		"event=0xff,umask=0x30"),
	INTEL_UNCORE_EVENT_DESC(ddrt_read.scale,	"6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(ddrt_read.unit,		"MiB"),
	INTEL_UNCORE_EVENT_DESC(ddrt_write,		"event=0xff,umask=0x31"),
	INTEL_UNCORE_EVENT_DESC(ddrt_write.scale,	"6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(ddrt_write.unit,	"MiB"),
	{ /* end: all zeroes */ },
};

static void icx_uncore_imc_freerunning_init_box(struct intel_uncore_box *box)
{
	int mem_offset = box->pmu->pmu_idx * ICX_IMC_MEM_STRIDE +
			 SNR_IMC_MMIO_MEM0_OFFSET;

	snr_uncore_mmio_map(box, uncore_mmio_box_ctl(box),
			    mem_offset, SNR_MC_DEVICE_ID);
}

static struct intel_uncore_ops icx_uncore_imc_freerunning_ops = {
	.init_box	= icx_uncore_imc_freerunning_init_box,
	.exit_box	= uncore_mmio_exit_box,
	.read_counter	= uncore_mmio_read_counter,
	.hw_config	= uncore_freerunning_hw_config,
};

static struct intel_uncore_type icx_uncore_imc_free_running = {
	.name			= "imc_free_running",
	.num_counters		= 5,
	.num_boxes		= 4,
	.num_freerunning_types	= ICX_IMC_FREERUNNING_TYPE_MAX,
	.mmio_map_size		= SNR_IMC_MMIO_SIZE,
	.freerunning		= icx_imc_freerunning,
	.ops			= &icx_uncore_imc_freerunning_ops,
	.event_descs		= icx_uncore_imc_freerunning_events,
	.format_group		= &skx_uncore_iio_freerunning_format_group,
};

static struct intel_uncore_type *icx_mmio_uncores[] = {
	&icx_uncore_imc,
	&icx_uncore_imc_free_running,
	NULL,
};

void icx_uncore_mmio_init(void)
{
	uncore_mmio_uncores = icx_mmio_uncores;
}

/* end of ICX uncore support */

/* SPR uncore support */

static void spr_uncore_msr_enable_event(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;

	if (reg1->idx != EXTRA_REG_NONE)
		wrmsrl(reg1->reg, reg1->config);

	wrmsrl(hwc->config_base, hwc->config);
}

static void spr_uncore_msr_disable_event(struct intel_uncore_box *box,
					 struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;

	if (reg1->idx != EXTRA_REG_NONE)
		wrmsrl(reg1->reg, 0);

	wrmsrl(hwc->config_base, 0);
}

static int spr_cha_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	bool tie_en = !!(event->hw.config & SPR_CHA_PMON_CTL_TID_EN);
	struct intel_uncore_type *type = box->pmu->type;

	if (tie_en) {
		reg1->reg = SPR_C0_MSR_PMON_BOX_FILTER0 +
			    HSWEP_CBO_MSR_OFFSET * type->box_ids[box->pmu->pmu_idx];
		reg1->config = event->attr.config1 & SPR_CHA_PMON_BOX_FILTER_TID;
		reg1->idx = 0;
	}

	return 0;
}

static struct intel_uncore_ops spr_uncore_chabox_ops = {
	.init_box		= intel_generic_uncore_msr_init_box,
	.disable_box		= intel_generic_uncore_msr_disable_box,
	.enable_box		= intel_generic_uncore_msr_enable_box,
	.disable_event		= spr_uncore_msr_disable_event,
	.enable_event		= spr_uncore_msr_enable_event,
	.read_counter		= uncore_msr_read_counter,
	.hw_config		= spr_cha_hw_config,
	.get_constraint		= uncore_get_constraint,
	.put_constraint		= uncore_put_constraint,
};

static struct attribute *spr_uncore_cha_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask_ext4.attr,
	&format_attr_tid_en2.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid5.attr,
	NULL,
};
static const struct attribute_group spr_uncore_chabox_format_group = {
	.name = "format",
	.attrs = spr_uncore_cha_formats_attr,
};

static ssize_t alias_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct intel_uncore_pmu *pmu = dev_to_uncore_pmu(dev);
	char pmu_name[UNCORE_PMU_NAME_LEN];

	uncore_get_alias_name(pmu_name, pmu);
	return sysfs_emit(buf, "%s\n", pmu_name);
}

static DEVICE_ATTR_RO(alias);

static struct attribute *uncore_alias_attrs[] = {
	&dev_attr_alias.attr,
	NULL
};

ATTRIBUTE_GROUPS(uncore_alias);

static struct intel_uncore_type spr_uncore_chabox = {
	.name			= "cha",
	.event_mask		= SPR_CHA_PMON_EVENT_MASK,
	.event_mask_ext		= SPR_RAW_EVENT_MASK_EXT,
	.num_shared_regs	= 1,
	.constraints		= skx_uncore_chabox_constraints,
	.ops			= &spr_uncore_chabox_ops,
	.format_group		= &spr_uncore_chabox_format_group,
	.attr_update		= uncore_alias_groups,
};

static struct intel_uncore_type spr_uncore_iio = {
	.name			= "iio",
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.event_mask_ext		= SNR_IIO_PMON_RAW_EVENT_MASK_EXT,
	.format_group		= &snr_uncore_iio_format_group,
	.attr_update		= uncore_alias_groups,
};

static struct attribute *spr_uncore_raw_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask_ext4.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static const struct attribute_group spr_uncore_raw_format_group = {
	.name			= "format",
	.attrs			= spr_uncore_raw_formats_attr,
};

#define SPR_UNCORE_COMMON_FORMAT()				\
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,	\
	.event_mask_ext		= SPR_RAW_EVENT_MASK_EXT,	\
	.format_group		= &spr_uncore_raw_format_group,	\
	.attr_update		= uncore_alias_groups

static struct intel_uncore_type spr_uncore_irp = {
	SPR_UNCORE_COMMON_FORMAT(),
	.name			= "irp",

};

static struct intel_uncore_type spr_uncore_m2pcie = {
	SPR_UNCORE_COMMON_FORMAT(),
	.name			= "m2pcie",
};

static struct intel_uncore_type spr_uncore_pcu = {
	.name			= "pcu",
	.attr_update		= uncore_alias_groups,
};

static void spr_uncore_mmio_enable_event(struct intel_uncore_box *box,
					 struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (!box->io_addr)
		return;

	if (uncore_pmc_fixed(hwc->idx))
		writel(SNBEP_PMON_CTL_EN, box->io_addr + hwc->config_base);
	else
		writel(hwc->config, box->io_addr + hwc->config_base);
}

static struct intel_uncore_ops spr_uncore_mmio_ops = {
	.init_box		= intel_generic_uncore_mmio_init_box,
	.exit_box		= uncore_mmio_exit_box,
	.disable_box		= intel_generic_uncore_mmio_disable_box,
	.enable_box		= intel_generic_uncore_mmio_enable_box,
	.disable_event		= intel_generic_uncore_mmio_disable_event,
	.enable_event		= spr_uncore_mmio_enable_event,
	.read_counter		= uncore_mmio_read_counter,
};

static struct intel_uncore_type spr_uncore_imc = {
	SPR_UNCORE_COMMON_FORMAT(),
	.name			= "imc",
	.fixed_ctr_bits		= 48,
	.fixed_ctr		= SNR_IMC_MMIO_PMON_FIXED_CTR,
	.fixed_ctl		= SNR_IMC_MMIO_PMON_FIXED_CTL,
	.ops			= &spr_uncore_mmio_ops,
};

static void spr_uncore_pci_enable_event(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, hwc->config_base + 4, (u32)(hwc->config >> 32));
	pci_write_config_dword(pdev, hwc->config_base, (u32)hwc->config);
}

static struct intel_uncore_ops spr_uncore_pci_ops = {
	.init_box		= intel_generic_uncore_pci_init_box,
	.disable_box		= intel_generic_uncore_pci_disable_box,
	.enable_box		= intel_generic_uncore_pci_enable_box,
	.disable_event		= intel_generic_uncore_pci_disable_event,
	.enable_event		= spr_uncore_pci_enable_event,
	.read_counter		= intel_generic_uncore_pci_read_counter,
};

#define SPR_UNCORE_PCI_COMMON_FORMAT()			\
	SPR_UNCORE_COMMON_FORMAT(),			\
	.ops			= &spr_uncore_pci_ops

static struct intel_uncore_type spr_uncore_m2m = {
	SPR_UNCORE_PCI_COMMON_FORMAT(),
	.name			= "m2m",
};

static struct intel_uncore_type spr_uncore_upi = {
	SPR_UNCORE_PCI_COMMON_FORMAT(),
	.name			= "upi",
};

static struct intel_uncore_type spr_uncore_m3upi = {
	SPR_UNCORE_PCI_COMMON_FORMAT(),
	.name			= "m3upi",
};

static struct intel_uncore_type spr_uncore_mdf = {
	SPR_UNCORE_COMMON_FORMAT(),
	.name			= "mdf",
};

#define UNCORE_SPR_NUM_UNCORE_TYPES		12
#define UNCORE_SPR_IIO				1
#define UNCORE_SPR_IMC				6

static struct intel_uncore_type *spr_uncores[UNCORE_SPR_NUM_UNCORE_TYPES] = {
	&spr_uncore_chabox,
	&spr_uncore_iio,
	&spr_uncore_irp,
	&spr_uncore_m2pcie,
	&spr_uncore_pcu,
	NULL,
	&spr_uncore_imc,
	&spr_uncore_m2m,
	&spr_uncore_upi,
	&spr_uncore_m3upi,
	NULL,
	&spr_uncore_mdf,
};

enum perf_uncore_spr_iio_freerunning_type_id {
	SPR_IIO_MSR_IOCLK,
	SPR_IIO_MSR_BW_IN,
	SPR_IIO_MSR_BW_OUT,

	SPR_IIO_FREERUNNING_TYPE_MAX,
};

static struct freerunning_counters spr_iio_freerunning[] = {
	[SPR_IIO_MSR_IOCLK]	= { 0x340e, 0x1, 0x10, 1, 48 },
	[SPR_IIO_MSR_BW_IN]	= { 0x3800, 0x1, 0x10, 8, 48 },
	[SPR_IIO_MSR_BW_OUT]	= { 0x3808, 0x1, 0x10, 8, 48 },
};

static struct uncore_event_desc spr_uncore_iio_freerunning_events[] = {
	/* Free-Running IIO CLOCKS Counter */
	INTEL_UNCORE_EVENT_DESC(ioclk,			"event=0xff,umask=0x10"),
	/* Free-Running IIO BANDWIDTH IN Counters */
	INTEL_UNCORE_EVENT_DESC(bw_in_port0,		"event=0xff,umask=0x20"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port0.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port0.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port1,		"event=0xff,umask=0x21"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port1.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port1.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port2,		"event=0xff,umask=0x22"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port2.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port2.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port3,		"event=0xff,umask=0x23"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port3.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port3.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port4,		"event=0xff,umask=0x24"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port4.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port4.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port5,		"event=0xff,umask=0x25"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port5.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port5.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port6,		"event=0xff,umask=0x26"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port6.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port6.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port7,		"event=0xff,umask=0x27"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port7.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_in_port7.unit,	"MiB"),
	/* Free-Running IIO BANDWIDTH OUT Counters */
	INTEL_UNCORE_EVENT_DESC(bw_out_port0,		"event=0xff,umask=0x30"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port0.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port0.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port1,		"event=0xff,umask=0x31"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port1.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port1.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port2,		"event=0xff,umask=0x32"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port2.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port2.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port3,		"event=0xff,umask=0x33"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port3.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port3.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port4,		"event=0xff,umask=0x34"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port4.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port4.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port5,		"event=0xff,umask=0x35"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port5.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port5.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port6,		"event=0xff,umask=0x36"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port6.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port6.unit,	"MiB"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port7,		"event=0xff,umask=0x37"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port7.scale,	"3.814697266e-6"),
	INTEL_UNCORE_EVENT_DESC(bw_out_port7.unit,	"MiB"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_type spr_uncore_iio_free_running = {
	.name			= "iio_free_running",
	.num_counters		= 17,
	.num_freerunning_types	= SPR_IIO_FREERUNNING_TYPE_MAX,
	.freerunning		= spr_iio_freerunning,
	.ops			= &skx_uncore_iio_freerunning_ops,
	.event_descs		= spr_uncore_iio_freerunning_events,
	.format_group		= &skx_uncore_iio_freerunning_format_group,
};

enum perf_uncore_spr_imc_freerunning_type_id {
	SPR_IMC_DCLK,
	SPR_IMC_PQ_CYCLES,

	SPR_IMC_FREERUNNING_TYPE_MAX,
};

static struct freerunning_counters spr_imc_freerunning[] = {
	[SPR_IMC_DCLK]		= { 0x22b0, 0x0, 0, 1, 48 },
	[SPR_IMC_PQ_CYCLES]	= { 0x2318, 0x8, 0, 2, 48 },
};

static struct uncore_event_desc spr_uncore_imc_freerunning_events[] = {
	INTEL_UNCORE_EVENT_DESC(dclk,			"event=0xff,umask=0x10"),

	INTEL_UNCORE_EVENT_DESC(rpq_cycles,		"event=0xff,umask=0x20"),
	INTEL_UNCORE_EVENT_DESC(wpq_cycles,		"event=0xff,umask=0x21"),
	{ /* end: all zeroes */ },
};

#define SPR_MC_DEVICE_ID	0x3251

static void spr_uncore_imc_freerunning_init_box(struct intel_uncore_box *box)
{
	int mem_offset = box->pmu->pmu_idx * ICX_IMC_MEM_STRIDE + SNR_IMC_MMIO_MEM0_OFFSET;

	snr_uncore_mmio_map(box, uncore_mmio_box_ctl(box),
			    mem_offset, SPR_MC_DEVICE_ID);
}

static struct intel_uncore_ops spr_uncore_imc_freerunning_ops = {
	.init_box	= spr_uncore_imc_freerunning_init_box,
	.exit_box	= uncore_mmio_exit_box,
	.read_counter	= uncore_mmio_read_counter,
	.hw_config	= uncore_freerunning_hw_config,
};

static struct intel_uncore_type spr_uncore_imc_free_running = {
	.name			= "imc_free_running",
	.num_counters		= 3,
	.mmio_map_size		= SNR_IMC_MMIO_SIZE,
	.num_freerunning_types	= SPR_IMC_FREERUNNING_TYPE_MAX,
	.freerunning		= spr_imc_freerunning,
	.ops			= &spr_uncore_imc_freerunning_ops,
	.event_descs		= spr_uncore_imc_freerunning_events,
	.format_group		= &skx_uncore_iio_freerunning_format_group,
};

#define UNCORE_SPR_MSR_EXTRA_UNCORES		1
#define UNCORE_SPR_MMIO_EXTRA_UNCORES		1

static struct intel_uncore_type *spr_msr_uncores[UNCORE_SPR_MSR_EXTRA_UNCORES] = {
	&spr_uncore_iio_free_running,
};

static struct intel_uncore_type *spr_mmio_uncores[UNCORE_SPR_MMIO_EXTRA_UNCORES] = {
	&spr_uncore_imc_free_running,
};

static void uncore_type_customized_copy(struct intel_uncore_type *to_type,
					struct intel_uncore_type *from_type)
{
	if (!to_type || !from_type)
		return;

	if (from_type->name)
		to_type->name = from_type->name;
	if (from_type->fixed_ctr_bits)
		to_type->fixed_ctr_bits = from_type->fixed_ctr_bits;
	if (from_type->event_mask)
		to_type->event_mask = from_type->event_mask;
	if (from_type->event_mask_ext)
		to_type->event_mask_ext = from_type->event_mask_ext;
	if (from_type->fixed_ctr)
		to_type->fixed_ctr = from_type->fixed_ctr;
	if (from_type->fixed_ctl)
		to_type->fixed_ctl = from_type->fixed_ctl;
	if (from_type->fixed_ctr_bits)
		to_type->fixed_ctr_bits = from_type->fixed_ctr_bits;
	if (from_type->num_shared_regs)
		to_type->num_shared_regs = from_type->num_shared_regs;
	if (from_type->constraints)
		to_type->constraints = from_type->constraints;
	if (from_type->ops)
		to_type->ops = from_type->ops;
	if (from_type->event_descs)
		to_type->event_descs = from_type->event_descs;
	if (from_type->format_group)
		to_type->format_group = from_type->format_group;
	if (from_type->attr_update)
		to_type->attr_update = from_type->attr_update;
}

static struct intel_uncore_type **
uncore_get_uncores(enum uncore_access_type type_id, int num_extra,
		    struct intel_uncore_type **extra)
{
	struct intel_uncore_type **types, **start_types;
	int i;

	start_types = types = intel_uncore_generic_init_uncores(type_id, num_extra);

	/* Only copy the customized features */
	for (; *types; types++) {
		if ((*types)->type_id >= UNCORE_SPR_NUM_UNCORE_TYPES)
			continue;
		uncore_type_customized_copy(*types, spr_uncores[(*types)->type_id]);
	}

	for (i = 0; i < num_extra; i++, types++)
		*types = extra[i];

	return start_types;
}

static struct intel_uncore_type *
uncore_find_type_by_id(struct intel_uncore_type **types, int type_id)
{
	for (; *types; types++) {
		if (type_id == (*types)->type_id)
			return *types;
	}

	return NULL;
}

static int uncore_type_max_boxes(struct intel_uncore_type **types,
				 int type_id)
{
	struct intel_uncore_type *type;
	int i, max = 0;

	type = uncore_find_type_by_id(types, type_id);
	if (!type)
		return 0;

	for (i = 0; i < type->num_boxes; i++) {
		if (type->box_ids[i] > max)
			max = type->box_ids[i];
	}

	return max + 1;
}

void spr_uncore_cpu_init(void)
{
	uncore_msr_uncores = uncore_get_uncores(UNCORE_ACCESS_MSR,
						UNCORE_SPR_MSR_EXTRA_UNCORES,
						spr_msr_uncores);

	spr_uncore_iio_free_running.num_boxes = uncore_type_max_boxes(uncore_msr_uncores, UNCORE_SPR_IIO);
}

int spr_uncore_pci_init(void)
{
	uncore_pci_uncores = uncore_get_uncores(UNCORE_ACCESS_PCI, 0, NULL);
	return 0;
}

void spr_uncore_mmio_init(void)
{
	int ret = snbep_pci2phy_map_init(0x3250, SKX_CPUNODEID, SKX_GIDNIDMAP, true);

	if (ret)
		uncore_mmio_uncores = uncore_get_uncores(UNCORE_ACCESS_MMIO, 0, NULL);
	else {
		uncore_mmio_uncores = uncore_get_uncores(UNCORE_ACCESS_MMIO,
							 UNCORE_SPR_MMIO_EXTRA_UNCORES,
							 spr_mmio_uncores);

		spr_uncore_imc_free_running.num_boxes = uncore_type_max_boxes(uncore_mmio_uncores, UNCORE_SPR_IMC) / 2;
	}
}

/* end of SPR uncore support */

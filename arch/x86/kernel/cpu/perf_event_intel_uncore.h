#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/perf_event.h>
#include "perf_event.h"

#define UNCORE_PMU_NAME_LEN		32
#define UNCORE_PMU_HRTIMER_INTERVAL	(60LL * NSEC_PER_SEC)
#define UNCORE_SNB_IMC_HRTIMER_INTERVAL (5ULL * NSEC_PER_SEC)

#define UNCORE_FIXED_EVENT		0xff
#define UNCORE_PMC_IDX_MAX_GENERIC	8
#define UNCORE_PMC_IDX_FIXED		UNCORE_PMC_IDX_MAX_GENERIC
#define UNCORE_PMC_IDX_MAX		(UNCORE_PMC_IDX_FIXED + 1)

#define UNCORE_PCI_DEV_DATA(type, idx)	((type << 8) | idx)
#define UNCORE_PCI_DEV_TYPE(data)	((data >> 8) & 0xff)
#define UNCORE_PCI_DEV_IDX(data)	(data & 0xff)
#define UNCORE_EXTRA_PCI_DEV		0xff
#define UNCORE_EXTRA_PCI_DEV_MAX	2

/* support up to 8 sockets */
#define UNCORE_SOCKET_MAX		8

#define UNCORE_EVENT_CONSTRAINT(c, n) EVENT_CONSTRAINT(c, n, 0xff)

/* SNB event control */
#define SNB_UNC_CTL_EV_SEL_MASK			0x000000ff
#define SNB_UNC_CTL_UMASK_MASK			0x0000ff00
#define SNB_UNC_CTL_EDGE_DET			(1 << 18)
#define SNB_UNC_CTL_EN				(1 << 22)
#define SNB_UNC_CTL_INVERT			(1 << 23)
#define SNB_UNC_CTL_CMASK_MASK			0x1f000000
#define NHM_UNC_CTL_CMASK_MASK			0xff000000
#define NHM_UNC_FIXED_CTR_CTL_EN		(1 << 0)

#define SNB_UNC_RAW_EVENT_MASK			(SNB_UNC_CTL_EV_SEL_MASK | \
						 SNB_UNC_CTL_UMASK_MASK | \
						 SNB_UNC_CTL_EDGE_DET | \
						 SNB_UNC_CTL_INVERT | \
						 SNB_UNC_CTL_CMASK_MASK)

#define NHM_UNC_RAW_EVENT_MASK			(SNB_UNC_CTL_EV_SEL_MASK | \
						 SNB_UNC_CTL_UMASK_MASK | \
						 SNB_UNC_CTL_EDGE_DET | \
						 SNB_UNC_CTL_INVERT | \
						 NHM_UNC_CTL_CMASK_MASK)

/* SNB global control register */
#define SNB_UNC_PERF_GLOBAL_CTL                 0x391
#define SNB_UNC_FIXED_CTR_CTRL                  0x394
#define SNB_UNC_FIXED_CTR                       0x395

/* SNB uncore global control */
#define SNB_UNC_GLOBAL_CTL_CORE_ALL             ((1 << 4) - 1)
#define SNB_UNC_GLOBAL_CTL_EN                   (1 << 29)

/* SNB Cbo register */
#define SNB_UNC_CBO_0_PERFEVTSEL0               0x700
#define SNB_UNC_CBO_0_PER_CTR0                  0x706
#define SNB_UNC_CBO_MSR_OFFSET                  0x10

/* NHM global control register */
#define NHM_UNC_PERF_GLOBAL_CTL                 0x391
#define NHM_UNC_FIXED_CTR                       0x394
#define NHM_UNC_FIXED_CTR_CTRL                  0x395

/* NHM uncore global control */
#define NHM_UNC_GLOBAL_CTL_EN_PC_ALL            ((1ULL << 8) - 1)
#define NHM_UNC_GLOBAL_CTL_EN_FC                (1ULL << 32)

/* NHM uncore register */
#define NHM_UNC_PERFEVTSEL0                     0x3c0
#define NHM_UNC_UNCORE_PMC0                     0x3b0

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
				 SNBEP_PMON_CTL_EV_SEL_EXT | \
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

/* IVT event control */
#define IVT_PMON_BOX_CTL_INT		(SNBEP_PMON_BOX_CTL_RST_CTRL | \
					 SNBEP_PMON_BOX_CTL_RST_CTRS)
#define IVT_PMON_RAW_EVENT_MASK		(SNBEP_PMON_CTL_EV_SEL_MASK | \
					 SNBEP_PMON_CTL_UMASK_MASK | \
					 SNBEP_PMON_CTL_EDGE_DET | \
					 SNBEP_PMON_CTL_TRESH_MASK)
/* IVT Ubox */
#define IVT_U_MSR_PMON_GLOBAL_CTL		0xc00
#define IVT_U_PMON_GLOBAL_FRZ_ALL		(1 << 31)
#define IVT_U_PMON_GLOBAL_UNFRZ_ALL		(1 << 29)

#define IVT_U_MSR_PMON_RAW_EVENT_MASK	\
				(SNBEP_PMON_CTL_EV_SEL_MASK | \
				 SNBEP_PMON_CTL_UMASK_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_U_MSR_PMON_CTL_TRESH_MASK)
/* IVT Cbo */
#define IVT_CBO_MSR_PMON_RAW_EVENT_MASK		(IVT_PMON_RAW_EVENT_MASK | \
						 SNBEP_CBO_PMON_CTL_TID_EN)

#define IVT_CB0_MSR_PMON_BOX_FILTER_TID		(0x1fULL << 0)
#define IVT_CB0_MSR_PMON_BOX_FILTER_LINK	(0xfULL << 5)
#define IVT_CB0_MSR_PMON_BOX_FILTER_STATE	(0x3fULL << 17)
#define IVT_CB0_MSR_PMON_BOX_FILTER_NID		(0xffffULL << 32)
#define IVT_CB0_MSR_PMON_BOX_FILTER_OPC		(0x1ffULL << 52)
#define IVT_CB0_MSR_PMON_BOX_FILTER_C6		(0x1ULL << 61)
#define IVT_CB0_MSR_PMON_BOX_FILTER_NC		(0x1ULL << 62)
#define IVT_CB0_MSR_PMON_BOX_FILTER_IOSC	(0x1ULL << 63)

/* IVT home agent */
#define IVT_HA_PCI_PMON_CTL_Q_OCC_RST		(1 << 16)
#define IVT_HA_PCI_PMON_RAW_EVENT_MASK		\
				(IVT_PMON_RAW_EVENT_MASK | \
				 IVT_HA_PCI_PMON_CTL_Q_OCC_RST)
/* IVT PCU */
#define IVT_PCU_MSR_PMON_RAW_EVENT_MASK	\
				(SNBEP_PMON_CTL_EV_SEL_MASK | \
				 SNBEP_PMON_CTL_EV_SEL_EXT | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_SEL_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_PCU_MSR_PMON_CTL_TRESH_MASK | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_INVERT | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_EDGE_DET)
/* IVT QPI */
#define IVT_QPI_PCI_PMON_RAW_EVENT_MASK	\
				(IVT_PMON_RAW_EVENT_MASK | \
				 SNBEP_PMON_CTL_EV_SEL_EXT)

/* NHM-EX event control */
#define NHMEX_PMON_CTL_EV_SEL_MASK	0x000000ff
#define NHMEX_PMON_CTL_UMASK_MASK	0x0000ff00
#define NHMEX_PMON_CTL_EN_BIT0		(1 << 0)
#define NHMEX_PMON_CTL_EDGE_DET		(1 << 18)
#define NHMEX_PMON_CTL_PMI_EN		(1 << 20)
#define NHMEX_PMON_CTL_EN_BIT22		(1 << 22)
#define NHMEX_PMON_CTL_INVERT		(1 << 23)
#define NHMEX_PMON_CTL_TRESH_MASK	0xff000000
#define NHMEX_PMON_RAW_EVENT_MASK	(NHMEX_PMON_CTL_EV_SEL_MASK | \
					 NHMEX_PMON_CTL_UMASK_MASK | \
					 NHMEX_PMON_CTL_EDGE_DET | \
					 NHMEX_PMON_CTL_INVERT | \
					 NHMEX_PMON_CTL_TRESH_MASK)

/* NHM-EX Ubox */
#define NHMEX_U_MSR_PMON_GLOBAL_CTL		0xc00
#define NHMEX_U_MSR_PMON_CTR			0xc11
#define NHMEX_U_MSR_PMON_EV_SEL			0xc10

#define NHMEX_U_PMON_GLOBAL_EN			(1 << 0)
#define NHMEX_U_PMON_GLOBAL_PMI_CORE_SEL	0x0000001e
#define NHMEX_U_PMON_GLOBAL_EN_ALL		(1 << 28)
#define NHMEX_U_PMON_GLOBAL_RST_ALL		(1 << 29)
#define NHMEX_U_PMON_GLOBAL_FRZ_ALL		(1 << 31)

#define NHMEX_U_PMON_RAW_EVENT_MASK		\
		(NHMEX_PMON_CTL_EV_SEL_MASK |	\
		 NHMEX_PMON_CTL_EDGE_DET)

/* NHM-EX Cbox */
#define NHMEX_C0_MSR_PMON_GLOBAL_CTL		0xd00
#define NHMEX_C0_MSR_PMON_CTR0			0xd11
#define NHMEX_C0_MSR_PMON_EV_SEL0		0xd10
#define NHMEX_C_MSR_OFFSET			0x20

/* NHM-EX Bbox */
#define NHMEX_B0_MSR_PMON_GLOBAL_CTL		0xc20
#define NHMEX_B0_MSR_PMON_CTR0			0xc31
#define NHMEX_B0_MSR_PMON_CTL0			0xc30
#define NHMEX_B_MSR_OFFSET			0x40
#define NHMEX_B0_MSR_MATCH			0xe45
#define NHMEX_B0_MSR_MASK			0xe46
#define NHMEX_B1_MSR_MATCH			0xe4d
#define NHMEX_B1_MSR_MASK			0xe4e

#define NHMEX_B_PMON_CTL_EN			(1 << 0)
#define NHMEX_B_PMON_CTL_EV_SEL_SHIFT		1
#define NHMEX_B_PMON_CTL_EV_SEL_MASK		\
		(0x1f << NHMEX_B_PMON_CTL_EV_SEL_SHIFT)
#define NHMEX_B_PMON_CTR_SHIFT		6
#define NHMEX_B_PMON_CTR_MASK		\
		(0x3 << NHMEX_B_PMON_CTR_SHIFT)
#define NHMEX_B_PMON_RAW_EVENT_MASK		\
		(NHMEX_B_PMON_CTL_EV_SEL_MASK | \
		 NHMEX_B_PMON_CTR_MASK)

/* NHM-EX Sbox */
#define NHMEX_S0_MSR_PMON_GLOBAL_CTL		0xc40
#define NHMEX_S0_MSR_PMON_CTR0			0xc51
#define NHMEX_S0_MSR_PMON_CTL0			0xc50
#define NHMEX_S_MSR_OFFSET			0x80
#define NHMEX_S0_MSR_MM_CFG			0xe48
#define NHMEX_S0_MSR_MATCH			0xe49
#define NHMEX_S0_MSR_MASK			0xe4a
#define NHMEX_S1_MSR_MM_CFG			0xe58
#define NHMEX_S1_MSR_MATCH			0xe59
#define NHMEX_S1_MSR_MASK			0xe5a

#define NHMEX_S_PMON_MM_CFG_EN			(0x1ULL << 63)
#define NHMEX_S_EVENT_TO_R_PROG_EV		0

/* NHM-EX Mbox */
#define NHMEX_M0_MSR_GLOBAL_CTL			0xca0
#define NHMEX_M0_MSR_PMU_DSP			0xca5
#define NHMEX_M0_MSR_PMU_ISS			0xca6
#define NHMEX_M0_MSR_PMU_MAP			0xca7
#define NHMEX_M0_MSR_PMU_MSC_THR		0xca8
#define NHMEX_M0_MSR_PMU_PGT			0xca9
#define NHMEX_M0_MSR_PMU_PLD			0xcaa
#define NHMEX_M0_MSR_PMU_ZDP_CTL_FVC		0xcab
#define NHMEX_M0_MSR_PMU_CTL0			0xcb0
#define NHMEX_M0_MSR_PMU_CNT0			0xcb1
#define NHMEX_M_MSR_OFFSET			0x40
#define NHMEX_M0_MSR_PMU_MM_CFG			0xe54
#define NHMEX_M1_MSR_PMU_MM_CFG			0xe5c

#define NHMEX_M_PMON_MM_CFG_EN			(1ULL << 63)
#define NHMEX_M_PMON_ADDR_MATCH_MASK		0x3ffffffffULL
#define NHMEX_M_PMON_ADDR_MASK_MASK		0x7ffffffULL
#define NHMEX_M_PMON_ADDR_MASK_SHIFT		34

#define NHMEX_M_PMON_CTL_EN			(1 << 0)
#define NHMEX_M_PMON_CTL_PMI_EN			(1 << 1)
#define NHMEX_M_PMON_CTL_COUNT_MODE_SHIFT	2
#define NHMEX_M_PMON_CTL_COUNT_MODE_MASK	\
	(0x3 << NHMEX_M_PMON_CTL_COUNT_MODE_SHIFT)
#define NHMEX_M_PMON_CTL_STORAGE_MODE_SHIFT	4
#define NHMEX_M_PMON_CTL_STORAGE_MODE_MASK	\
	(0x3 << NHMEX_M_PMON_CTL_STORAGE_MODE_SHIFT)
#define NHMEX_M_PMON_CTL_WRAP_MODE		(1 << 6)
#define NHMEX_M_PMON_CTL_FLAG_MODE		(1 << 7)
#define NHMEX_M_PMON_CTL_INC_SEL_SHIFT		9
#define NHMEX_M_PMON_CTL_INC_SEL_MASK		\
	(0x1f << NHMEX_M_PMON_CTL_INC_SEL_SHIFT)
#define NHMEX_M_PMON_CTL_SET_FLAG_SEL_SHIFT	19
#define NHMEX_M_PMON_CTL_SET_FLAG_SEL_MASK	\
	(0x7 << NHMEX_M_PMON_CTL_SET_FLAG_SEL_SHIFT)
#define NHMEX_M_PMON_RAW_EVENT_MASK			\
		(NHMEX_M_PMON_CTL_COUNT_MODE_MASK |	\
		 NHMEX_M_PMON_CTL_STORAGE_MODE_MASK |	\
		 NHMEX_M_PMON_CTL_WRAP_MODE |		\
		 NHMEX_M_PMON_CTL_FLAG_MODE |		\
		 NHMEX_M_PMON_CTL_INC_SEL_MASK |	\
		 NHMEX_M_PMON_CTL_SET_FLAG_SEL_MASK)

#define NHMEX_M_PMON_ZDP_CTL_FVC_MASK		(((1 << 11) - 1) | (1 << 23))
#define NHMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(n)	(0x7ULL << (11 + 3 * (n)))

#define WSMEX_M_PMON_ZDP_CTL_FVC_MASK		(((1 << 12) - 1) | (1 << 24))
#define WSMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(n)	(0x7ULL << (12 + 3 * (n)))

/*
 * use the 9~13 bits to select event If the 7th bit is not set,
 * otherwise use the 19~21 bits to select event.
 */
#define MBOX_INC_SEL(x) ((x) << NHMEX_M_PMON_CTL_INC_SEL_SHIFT)
#define MBOX_SET_FLAG_SEL(x) (((x) << NHMEX_M_PMON_CTL_SET_FLAG_SEL_SHIFT) | \
				NHMEX_M_PMON_CTL_FLAG_MODE)
#define MBOX_INC_SEL_MASK (NHMEX_M_PMON_CTL_INC_SEL_MASK | \
			   NHMEX_M_PMON_CTL_FLAG_MODE)
#define MBOX_SET_FLAG_SEL_MASK (NHMEX_M_PMON_CTL_SET_FLAG_SEL_MASK | \
				NHMEX_M_PMON_CTL_FLAG_MODE)
#define MBOX_INC_SEL_EXTAR_REG(c, r) \
		EVENT_EXTRA_REG(MBOX_INC_SEL(c), NHMEX_M0_MSR_PMU_##r, \
				MBOX_INC_SEL_MASK, (u64)-1, NHMEX_M_##r)
#define MBOX_SET_FLAG_SEL_EXTRA_REG(c, r) \
		EVENT_EXTRA_REG(MBOX_SET_FLAG_SEL(c), NHMEX_M0_MSR_PMU_##r, \
				MBOX_SET_FLAG_SEL_MASK, \
				(u64)-1, NHMEX_M_##r)

/* NHM-EX Rbox */
#define NHMEX_R_MSR_GLOBAL_CTL			0xe00
#define NHMEX_R_MSR_PMON_CTL0			0xe10
#define NHMEX_R_MSR_PMON_CNT0			0xe11
#define NHMEX_R_MSR_OFFSET			0x20

#define NHMEX_R_MSR_PORTN_QLX_CFG(n)		\
		((n) < 4 ? (0xe0c + (n)) : (0xe2c + (n) - 4))
#define NHMEX_R_MSR_PORTN_IPERF_CFG0(n)		(0xe04 + (n))
#define NHMEX_R_MSR_PORTN_IPERF_CFG1(n)		(0xe24 + (n))
#define NHMEX_R_MSR_PORTN_XBR_OFFSET(n)		\
		(((n) < 4 ? 0 : 0x10) + (n) * 4)
#define NHMEX_R_MSR_PORTN_XBR_SET1_MM_CFG(n)	\
		(0xe60 + NHMEX_R_MSR_PORTN_XBR_OFFSET(n))
#define NHMEX_R_MSR_PORTN_XBR_SET1_MATCH(n)	\
		(NHMEX_R_MSR_PORTN_XBR_SET1_MM_CFG(n) + 1)
#define NHMEX_R_MSR_PORTN_XBR_SET1_MASK(n)	\
		(NHMEX_R_MSR_PORTN_XBR_SET1_MM_CFG(n) + 2)
#define NHMEX_R_MSR_PORTN_XBR_SET2_MM_CFG(n)	\
		(0xe70 + NHMEX_R_MSR_PORTN_XBR_OFFSET(n))
#define NHMEX_R_MSR_PORTN_XBR_SET2_MATCH(n)	\
		(NHMEX_R_MSR_PORTN_XBR_SET2_MM_CFG(n) + 1)
#define NHMEX_R_MSR_PORTN_XBR_SET2_MASK(n)	\
		(NHMEX_R_MSR_PORTN_XBR_SET2_MM_CFG(n) + 2)

#define NHMEX_R_PMON_CTL_EN			(1 << 0)
#define NHMEX_R_PMON_CTL_EV_SEL_SHIFT		1
#define NHMEX_R_PMON_CTL_EV_SEL_MASK		\
		(0x1f << NHMEX_R_PMON_CTL_EV_SEL_SHIFT)
#define NHMEX_R_PMON_CTL_PMI_EN			(1 << 6)
#define NHMEX_R_PMON_RAW_EVENT_MASK		NHMEX_R_PMON_CTL_EV_SEL_MASK

/* NHM-EX Wbox */
#define NHMEX_W_MSR_GLOBAL_CTL			0xc80
#define NHMEX_W_MSR_PMON_CNT0			0xc90
#define NHMEX_W_MSR_PMON_EVT_SEL0		0xc91
#define NHMEX_W_MSR_PMON_FIXED_CTR		0x394
#define NHMEX_W_MSR_PMON_FIXED_CTL		0x395

#define NHMEX_W_PMON_GLOBAL_FIXED_EN		(1ULL << 31)

struct intel_uncore_ops;
struct intel_uncore_pmu;
struct intel_uncore_box;
struct uncore_event_desc;

struct intel_uncore_type {
	const char *name;
	int num_counters;
	int num_boxes;
	int perf_ctr_bits;
	int fixed_ctr_bits;
	unsigned perf_ctr;
	unsigned event_ctl;
	unsigned event_mask;
	unsigned fixed_ctr;
	unsigned fixed_ctl;
	unsigned box_ctl;
	unsigned msr_offset;
	unsigned num_shared_regs:8;
	unsigned single_fixed:1;
	unsigned pair_ctr_ctl:1;
	unsigned *msr_offsets;
	struct event_constraint unconstrainted;
	struct event_constraint *constraints;
	struct intel_uncore_pmu *pmus;
	struct intel_uncore_ops *ops;
	struct uncore_event_desc *event_descs;
	const struct attribute_group *attr_groups[4];
	struct pmu *pmu; /* for custom pmu ops */
};

#define pmu_group attr_groups[0]
#define format_group attr_groups[1]
#define events_group attr_groups[2]

struct intel_uncore_ops {
	void (*init_box)(struct intel_uncore_box *);
	void (*disable_box)(struct intel_uncore_box *);
	void (*enable_box)(struct intel_uncore_box *);
	void (*disable_event)(struct intel_uncore_box *, struct perf_event *);
	void (*enable_event)(struct intel_uncore_box *, struct perf_event *);
	u64 (*read_counter)(struct intel_uncore_box *, struct perf_event *);
	int (*hw_config)(struct intel_uncore_box *, struct perf_event *);
	struct event_constraint *(*get_constraint)(struct intel_uncore_box *,
						   struct perf_event *);
	void (*put_constraint)(struct intel_uncore_box *, struct perf_event *);
};

struct intel_uncore_pmu {
	struct pmu pmu;
	char name[UNCORE_PMU_NAME_LEN];
	int pmu_idx;
	int func_id;
	struct intel_uncore_type *type;
	struct intel_uncore_box ** __percpu box;
	struct list_head box_list;
};

struct intel_uncore_extra_reg {
	raw_spinlock_t lock;
	u64 config, config1, config2;
	atomic_t ref;
};

struct intel_uncore_box {
	int phys_id;
	int n_active;	/* number of active events */
	int n_events;
	int cpu;	/* cpu to collect events */
	unsigned long flags;
	atomic_t refcnt;
	struct perf_event *events[UNCORE_PMC_IDX_MAX];
	struct perf_event *event_list[UNCORE_PMC_IDX_MAX];
	unsigned long active_mask[BITS_TO_LONGS(UNCORE_PMC_IDX_MAX)];
	u64 tags[UNCORE_PMC_IDX_MAX];
	struct pci_dev *pci_dev;
	struct intel_uncore_pmu *pmu;
	u64 hrtimer_duration; /* hrtimer timeout for this box */
	struct hrtimer hrtimer;
	struct list_head list;
	struct list_head active_list;
	void *io_addr;
	struct intel_uncore_extra_reg shared_regs[0];
};

#define UNCORE_BOX_FLAG_INITIATED	0

struct uncore_event_desc {
	struct kobj_attribute attr;
	const char *config;
};

#define INTEL_UNCORE_EVENT_DESC(_name, _config)			\
{								\
	.attr	= __ATTR(_name, 0444, uncore_event_show, NULL),	\
	.config	= _config,					\
}

#define DEFINE_UNCORE_FORMAT_ATTR(_var, _name, _format)			\
static ssize_t __uncore_##_var##_show(struct kobject *kobj,		\
				struct kobj_attribute *attr,		\
				char *page)				\
{									\
	BUILD_BUG_ON(sizeof(_format) >= PAGE_SIZE);			\
	return sprintf(page, _format "\n");				\
}									\
static struct kobj_attribute format_attr_##_var =			\
	__ATTR(_name, 0444, __uncore_##_var##_show, NULL)


static ssize_t uncore_event_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct uncore_event_desc *event =
		container_of(attr, struct uncore_event_desc, attr);
	return sprintf(buf, "%s", event->config);
}

static inline unsigned uncore_pci_box_ctl(struct intel_uncore_box *box)
{
	return box->pmu->type->box_ctl;
}

static inline unsigned uncore_pci_fixed_ctl(struct intel_uncore_box *box)
{
	return box->pmu->type->fixed_ctl;
}

static inline unsigned uncore_pci_fixed_ctr(struct intel_uncore_box *box)
{
	return box->pmu->type->fixed_ctr;
}

static inline
unsigned uncore_pci_event_ctl(struct intel_uncore_box *box, int idx)
{
	return idx * 4 + box->pmu->type->event_ctl;
}

static inline
unsigned uncore_pci_perf_ctr(struct intel_uncore_box *box, int idx)
{
	return idx * 8 + box->pmu->type->perf_ctr;
}

static inline unsigned uncore_msr_box_offset(struct intel_uncore_box *box)
{
	struct intel_uncore_pmu *pmu = box->pmu;
	return pmu->type->msr_offsets ?
		pmu->type->msr_offsets[pmu->pmu_idx] :
		pmu->type->msr_offset * pmu->pmu_idx;
}

static inline unsigned uncore_msr_box_ctl(struct intel_uncore_box *box)
{
	if (!box->pmu->type->box_ctl)
		return 0;
	return box->pmu->type->box_ctl + uncore_msr_box_offset(box);
}

static inline unsigned uncore_msr_fixed_ctl(struct intel_uncore_box *box)
{
	if (!box->pmu->type->fixed_ctl)
		return 0;
	return box->pmu->type->fixed_ctl + uncore_msr_box_offset(box);
}

static inline unsigned uncore_msr_fixed_ctr(struct intel_uncore_box *box)
{
	return box->pmu->type->fixed_ctr + uncore_msr_box_offset(box);
}

static inline
unsigned uncore_msr_event_ctl(struct intel_uncore_box *box, int idx)
{
	return box->pmu->type->event_ctl +
		(box->pmu->type->pair_ctr_ctl ? 2 * idx : idx) +
		uncore_msr_box_offset(box);
}

static inline
unsigned uncore_msr_perf_ctr(struct intel_uncore_box *box, int idx)
{
	return box->pmu->type->perf_ctr +
		(box->pmu->type->pair_ctr_ctl ? 2 * idx : idx) +
		uncore_msr_box_offset(box);
}

static inline
unsigned uncore_fixed_ctl(struct intel_uncore_box *box)
{
	if (box->pci_dev)
		return uncore_pci_fixed_ctl(box);
	else
		return uncore_msr_fixed_ctl(box);
}

static inline
unsigned uncore_fixed_ctr(struct intel_uncore_box *box)
{
	if (box->pci_dev)
		return uncore_pci_fixed_ctr(box);
	else
		return uncore_msr_fixed_ctr(box);
}

static inline
unsigned uncore_event_ctl(struct intel_uncore_box *box, int idx)
{
	if (box->pci_dev)
		return uncore_pci_event_ctl(box, idx);
	else
		return uncore_msr_event_ctl(box, idx);
}

static inline
unsigned uncore_perf_ctr(struct intel_uncore_box *box, int idx)
{
	if (box->pci_dev)
		return uncore_pci_perf_ctr(box, idx);
	else
		return uncore_msr_perf_ctr(box, idx);
}

static inline int uncore_perf_ctr_bits(struct intel_uncore_box *box)
{
	return box->pmu->type->perf_ctr_bits;
}

static inline int uncore_fixed_ctr_bits(struct intel_uncore_box *box)
{
	return box->pmu->type->fixed_ctr_bits;
}

static inline int uncore_num_counters(struct intel_uncore_box *box)
{
	return box->pmu->type->num_counters;
}

static inline void uncore_disable_box(struct intel_uncore_box *box)
{
	if (box->pmu->type->ops->disable_box)
		box->pmu->type->ops->disable_box(box);
}

static inline void uncore_enable_box(struct intel_uncore_box *box)
{
	if (box->pmu->type->ops->enable_box)
		box->pmu->type->ops->enable_box(box);
}

static inline void uncore_disable_event(struct intel_uncore_box *box,
				struct perf_event *event)
{
	box->pmu->type->ops->disable_event(box, event);
}

static inline void uncore_enable_event(struct intel_uncore_box *box,
				struct perf_event *event)
{
	box->pmu->type->ops->enable_event(box, event);
}

static inline u64 uncore_read_counter(struct intel_uncore_box *box,
				struct perf_event *event)
{
	return box->pmu->type->ops->read_counter(box, event);
}

static inline void uncore_box_init(struct intel_uncore_box *box)
{
	if (!test_and_set_bit(UNCORE_BOX_FLAG_INITIATED, &box->flags)) {
		if (box->pmu->type->ops->init_box)
			box->pmu->type->ops->init_box(box);
	}
}

static inline bool uncore_box_is_fake(struct intel_uncore_box *box)
{
	return (box->phys_id < 0);
}

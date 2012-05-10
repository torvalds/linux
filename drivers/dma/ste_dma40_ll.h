/*
 * Copyright (C) ST-Ericsson SA 2007-2010
 * Author: Per Friden <per.friden@stericsson.com> for ST-Ericsson SA
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson SA
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef STE_DMA40_LL_H
#define STE_DMA40_LL_H

#define D40_DREG_PCBASE		0x400
#define D40_DREG_PCDELTA	(8 * 4)
#define D40_LLI_ALIGN		16 /* LLI alignment must be 16 bytes. */

#define D40_LCPA_CHAN_SIZE 32
#define D40_LCPA_CHAN_DST_DELTA 16

#define D40_TYPE_TO_GROUP(type) (type / 16)
#define D40_TYPE_TO_EVENT(type) (type % 16)
#define D40_GROUP_SIZE 8
#define D40_PHYS_TO_GROUP(phys) ((phys & (D40_GROUP_SIZE - 1)) / 2)

/* Most bits of the CFG register are the same in log as in phy mode */
#define D40_SREG_CFG_MST_POS		15
#define D40_SREG_CFG_TIM_POS		14
#define D40_SREG_CFG_EIM_POS		13
#define D40_SREG_CFG_LOG_INCR_POS	12
#define D40_SREG_CFG_PHY_PEN_POS	12
#define D40_SREG_CFG_PSIZE_POS		10
#define D40_SREG_CFG_ESIZE_POS		 8
#define D40_SREG_CFG_PRI_POS		 7
#define D40_SREG_CFG_LBE_POS		 6
#define D40_SREG_CFG_LOG_GIM_POS	 5
#define D40_SREG_CFG_LOG_MFU_POS	 4
#define D40_SREG_CFG_PHY_TM_POS		 4
#define D40_SREG_CFG_PHY_EVTL_POS	 0


/* Standard channel parameters - basic mode (element register) */
#define D40_SREG_ELEM_PHY_ECNT_POS	16
#define D40_SREG_ELEM_PHY_EIDX_POS	 0

#define D40_SREG_ELEM_PHY_ECNT_MASK	(0xFFFF << D40_SREG_ELEM_PHY_ECNT_POS)

/* Standard channel parameters - basic mode (Link register) */
#define D40_SREG_LNK_PHY_TCP_POS	0
#define D40_SREG_LNK_PHY_LMP_POS	1
#define D40_SREG_LNK_PHY_PRE_POS	2
/*
 * Source  destination link address. Contains the
 * 29-bit byte word aligned address of the reload area.
 */
#define D40_SREG_LNK_PHYS_LNK_MASK	0xFFFFFFF8UL

/* Standard basic channel logical mode */

/* Element register */
#define D40_SREG_ELEM_LOG_ECNT_POS	16
#define D40_SREG_ELEM_LOG_LIDX_POS	 8
#define D40_SREG_ELEM_LOG_LOS_POS	 1
#define D40_SREG_ELEM_LOG_TCP_POS	 0

#define D40_SREG_ELEM_LOG_LIDX_MASK	(0xFF << D40_SREG_ELEM_LOG_LIDX_POS)

/* Link register */
#define D40_EVENTLINE_POS(i)		(2 * i)
#define D40_EVENTLINE_MASK(i)		(0x3 << D40_EVENTLINE_POS(i))

/* Standard basic channel logical params in memory */

/* LCSP0 */
#define D40_MEM_LCSP0_ECNT_POS		16
#define D40_MEM_LCSP0_SPTR_POS		 0

#define D40_MEM_LCSP0_ECNT_MASK		(0xFFFF << D40_MEM_LCSP0_ECNT_POS)
#define D40_MEM_LCSP0_SPTR_MASK		(0xFFFF << D40_MEM_LCSP0_SPTR_POS)

/* LCSP1 */
#define D40_MEM_LCSP1_SPTR_POS		16
#define D40_MEM_LCSP1_SCFG_MST_POS	15
#define D40_MEM_LCSP1_SCFG_TIM_POS	14
#define D40_MEM_LCSP1_SCFG_EIM_POS	13
#define D40_MEM_LCSP1_SCFG_INCR_POS	12
#define D40_MEM_LCSP1_SCFG_PSIZE_POS	10
#define D40_MEM_LCSP1_SCFG_ESIZE_POS	 8
#define D40_MEM_LCSP1_SLOS_POS		 1
#define D40_MEM_LCSP1_STCP_POS		 0

#define D40_MEM_LCSP1_SPTR_MASK		(0xFFFF << D40_MEM_LCSP1_SPTR_POS)
#define D40_MEM_LCSP1_SCFG_TIM_MASK	(0x1 << D40_MEM_LCSP1_SCFG_TIM_POS)
#define D40_MEM_LCSP1_SCFG_INCR_MASK	(0x1 << D40_MEM_LCSP1_SCFG_INCR_POS)
#define D40_MEM_LCSP1_SCFG_PSIZE_MASK	(0x3 << D40_MEM_LCSP1_SCFG_PSIZE_POS)
#define D40_MEM_LCSP1_SLOS_MASK		(0x7F << D40_MEM_LCSP1_SLOS_POS)
#define D40_MEM_LCSP1_STCP_MASK		(0x1 << D40_MEM_LCSP1_STCP_POS)

/* LCSP2 */
#define D40_MEM_LCSP2_ECNT_POS		16

#define D40_MEM_LCSP2_ECNT_MASK		(0xFFFF << D40_MEM_LCSP2_ECNT_POS)

/* LCSP3 */
#define D40_MEM_LCSP3_DCFG_MST_POS	15
#define D40_MEM_LCSP3_DCFG_TIM_POS	14
#define D40_MEM_LCSP3_DCFG_EIM_POS	13
#define D40_MEM_LCSP3_DCFG_INCR_POS	12
#define D40_MEM_LCSP3_DCFG_PSIZE_POS	10
#define D40_MEM_LCSP3_DCFG_ESIZE_POS	 8
#define D40_MEM_LCSP3_DLOS_POS		 1
#define D40_MEM_LCSP3_DTCP_POS		 0

#define D40_MEM_LCSP3_DLOS_MASK		(0x7F << D40_MEM_LCSP3_DLOS_POS)
#define D40_MEM_LCSP3_DTCP_MASK		(0x1 << D40_MEM_LCSP3_DTCP_POS)


/* Standard channel parameter register offsets */
#define D40_CHAN_REG_SSCFG	0x00
#define D40_CHAN_REG_SSELT	0x04
#define D40_CHAN_REG_SSPTR	0x08
#define D40_CHAN_REG_SSLNK	0x0C
#define D40_CHAN_REG_SDCFG	0x10
#define D40_CHAN_REG_SDELT	0x14
#define D40_CHAN_REG_SDPTR	0x18
#define D40_CHAN_REG_SDLNK	0x1C

/* DMA Register Offsets */
#define D40_DREG_GCC		0x000
#define D40_DREG_GCC_ENA	0x1
/* This assumes that there are only 4 event groups */
#define D40_DREG_GCC_ENABLE_ALL	0xff01
#define D40_DREG_GCC_EVTGRP_POS 8
#define D40_DREG_GCC_SRC 0
#define D40_DREG_GCC_DST 1
#define D40_DREG_GCC_EVTGRP_ENA(x, y) \
	(1 << (D40_DREG_GCC_EVTGRP_POS + 2 * x + y))

#define D40_DREG_PRTYP		0x004
#define D40_DREG_PRSME		0x008
#define D40_DREG_PRSMO		0x00C
#define D40_DREG_PRMSE		0x010
#define D40_DREG_PRMSO		0x014
#define D40_DREG_PRMOE		0x018
#define D40_DREG_PRMOO		0x01C
#define D40_DREG_PRMO_PCHAN_BASIC		0x1
#define D40_DREG_PRMO_PCHAN_MODULO		0x2
#define D40_DREG_PRMO_PCHAN_DOUBLE_DST		0x3
#define D40_DREG_PRMO_LCHAN_SRC_PHY_DST_LOG	0x1
#define D40_DREG_PRMO_LCHAN_SRC_LOG_DST_PHY	0x2
#define D40_DREG_PRMO_LCHAN_SRC_LOG_DST_LOG	0x3

#define D40_DREG_LCPA		0x020
#define D40_DREG_LCLA		0x024
#define D40_DREG_ACTIVE		0x050
#define D40_DREG_ACTIVO		0x054
#define D40_DREG_FSEB1		0x058
#define D40_DREG_FSEB2		0x05C
#define D40_DREG_PCMIS		0x060
#define D40_DREG_PCICR		0x064
#define D40_DREG_PCTIS		0x068
#define D40_DREG_PCEIS		0x06C
#define D40_DREG_LCMIS0		0x080
#define D40_DREG_LCMIS1		0x084
#define D40_DREG_LCMIS2		0x088
#define D40_DREG_LCMIS3		0x08C
#define D40_DREG_LCICR0		0x090
#define D40_DREG_LCICR1		0x094
#define D40_DREG_LCICR2		0x098
#define D40_DREG_LCICR3		0x09C
#define D40_DREG_LCTIS0		0x0A0
#define D40_DREG_LCTIS1		0x0A4
#define D40_DREG_LCTIS2		0x0A8
#define D40_DREG_LCTIS3		0x0AC
#define D40_DREG_LCEIS0		0x0B0
#define D40_DREG_LCEIS1		0x0B4
#define D40_DREG_LCEIS2		0x0B8
#define D40_DREG_LCEIS3		0x0BC
#define D40_DREG_PSEG1		0x110
#define D40_DREG_PSEG2		0x114
#define D40_DREG_PSEG3		0x118
#define D40_DREG_PSEG4		0x11C
#define D40_DREG_PCEG1		0x120
#define D40_DREG_PCEG2		0x124
#define D40_DREG_PCEG3		0x128
#define D40_DREG_PCEG4		0x12C
#define D40_DREG_RSEG1		0x130
#define D40_DREG_RSEG2		0x134
#define D40_DREG_RSEG3		0x138
#define D40_DREG_RSEG4		0x13C
#define D40_DREG_RCEG1		0x140
#define D40_DREG_RCEG2		0x144
#define D40_DREG_RCEG3		0x148
#define D40_DREG_RCEG4		0x14C
#define D40_DREG_STFU		0xFC8
#define D40_DREG_ICFG		0xFCC
#define D40_DREG_PERIPHID0	0xFE0
#define D40_DREG_PERIPHID1	0xFE4
#define D40_DREG_PERIPHID2	0xFE8
#define D40_DREG_PERIPHID3	0xFEC
#define D40_DREG_CELLID0	0xFF0
#define D40_DREG_CELLID1	0xFF4
#define D40_DREG_CELLID2	0xFF8
#define D40_DREG_CELLID3	0xFFC

/* LLI related structures */

/**
 * struct d40_phy_lli - The basic configration register for each physical
 * channel.
 *
 * @reg_cfg: The configuration register.
 * @reg_elt: The element register.
 * @reg_ptr: The pointer register.
 * @reg_lnk: The link register.
 *
 * These registers are set up for both physical and logical transfers
 * Note that the bit in each register means differently in logical and
 * physical(standard) mode.
 *
 * This struct must be 16 bytes aligned, and only contain physical registers
 * since it will be directly accessed by the DMA.
 */
struct d40_phy_lli {
	u32 reg_cfg;
	u32 reg_elt;
	u32 reg_ptr;
	u32 reg_lnk;
};

/**
 * struct d40_phy_lli_bidir - struct for a transfer.
 *
 * @src: Register settings for src channel.
 * @dst: Register settings for dst channel.
 *
 * All DMA transfers have a source and a destination.
 */

struct d40_phy_lli_bidir {
	struct d40_phy_lli	*src;
	struct d40_phy_lli	*dst;
};


/**
 * struct d40_log_lli - logical lli configuration
 *
 * @lcsp02: Either maps to register lcsp0 if src or lcsp2 if dst.
 * @lcsp13: Either maps to register lcsp1 if src or lcsp3 if dst.
 *
 * This struct must be 8 bytes aligned since it will be accessed directy by
 * the DMA. Never add any none hw mapped registers to this struct.
 */

struct d40_log_lli {
	u32 lcsp02;
	u32 lcsp13;
};

/**
 * struct d40_log_lli_bidir - For both src and dst
 *
 * @src: pointer to src lli configuration.
 * @dst: pointer to dst lli configuration.
 *
 * You always have a src and a dst when doing DMA transfers.
 */

struct d40_log_lli_bidir {
	struct d40_log_lli *src;
	struct d40_log_lli *dst;
};

/**
 * struct d40_log_lli_full - LCPA layout
 *
 * @lcsp0: Logical Channel Standard Param 0 - Src.
 * @lcsp1: Logical Channel Standard Param 1 - Src.
 * @lcsp2: Logical Channel Standard Param 2 - Dst.
 * @lcsp3: Logical Channel Standard Param 3 - Dst.
 *
 * This struct maps to LCPA physical memory layout. Must map to
 * the hw.
 */
struct d40_log_lli_full {
	u32 lcsp0;
	u32 lcsp1;
	u32 lcsp2;
	u32 lcsp3;
};

/**
 * struct d40_def_lcsp - Default LCSP1 and LCSP3 settings
 *
 * @lcsp3: The default configuration for dst.
 * @lcsp1: The default configuration for src.
 */
struct d40_def_lcsp {
	u32 lcsp3;
	u32 lcsp1;
};

/* Physical channels */

enum d40_lli_flags {
	LLI_ADDR_INC	= 1 << 0,
	LLI_TERM_INT	= 1 << 1,
	LLI_CYCLIC	= 1 << 2,
	LLI_LAST_LINK	= 1 << 3,
};

void d40_phy_cfg(struct stedma40_chan_cfg *cfg,
		 u32 *src_cfg,
		 u32 *dst_cfg,
		 bool is_log);

void d40_log_cfg(struct stedma40_chan_cfg *cfg,
		 u32 *lcsp1,
		 u32 *lcsp2);

int d40_phy_sg_to_lli(struct scatterlist *sg,
		      int sg_len,
		      dma_addr_t target,
		      struct d40_phy_lli *lli,
		      dma_addr_t lli_phys,
		      u32 reg_cfg,
		      struct stedma40_half_channel_info *info,
		      struct stedma40_half_channel_info *otherinfo,
		      unsigned long flags);

/* Logical channels */

int d40_log_sg_to_lli(struct scatterlist *sg,
		      int sg_len,
		      dma_addr_t dev_addr,
		      struct d40_log_lli *lli_sg,
		      u32 lcsp13, /* src or dst*/
		      u32 data_width1, u32 data_width2);

void d40_log_lli_lcpa_write(struct d40_log_lli_full *lcpa,
			    struct d40_log_lli *lli_dst,
			    struct d40_log_lli *lli_src,
			    int next, unsigned int flags);

void d40_log_lli_lcla_write(struct d40_log_lli *lcla,
			    struct d40_log_lli *lli_dst,
			    struct d40_log_lli *lli_src,
			    int next, unsigned int flags);

#endif /* STE_DMA40_LLI_H */

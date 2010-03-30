/*
 * driver/dma/ste_dma40_ll.h
 *
 * Copyright (C) ST-Ericsson 2007-2010
 * License terms: GNU General Public License (GPL) version 2
 * Author: Per Friden <per.friden@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 */
#ifndef STE_DMA40_LL_H
#define STE_DMA40_LL_H

#define D40_DREG_PCBASE		0x400
#define D40_DREG_PCDELTA	(8 * 4)
#define D40_LLI_ALIGN		16 /* LLI alignment must be 16 bytes. */

#define D40_TYPE_TO_GROUP(type) (type / 16)
#define D40_TYPE_TO_EVENT(type) (type % 16)

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
#define D40_DEACTIVATE_EVENTLINE	0x0
#define D40_ACTIVATE_EVENTLINE		0x1
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
#define D40_DREG_PRTYP		0x004
#define D40_DREG_PRSME		0x008
#define D40_DREG_PRSMO		0x00C
#define D40_DREG_PRMSE		0x010
#define D40_DREG_PRMSO		0x014
#define D40_DREG_PRMOE		0x018
#define D40_DREG_PRMOO		0x01C
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
 * @dst_addr: Physical destination address.
 * @src_addr: Physical source address.
 *
 * All DMA transfers have a source and a destination.
 */

struct d40_phy_lli_bidir {
	struct d40_phy_lli	*src;
	struct d40_phy_lli	*dst;
	dma_addr_t		 dst_addr;
	dma_addr_t		 src_addr;
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

/**
 * struct d40_lcla_elem - Info for one LCA element.
 *
 * @src_id: logical channel src id
 * @dst_id: logical channel dst id
 * @src: LCPA formated src parameters
 * @dst: LCPA formated dst parameters
 *
 */
struct d40_lcla_elem {
	int			src_id;
	int			dst_id;
	struct d40_log_lli     *src;
	struct d40_log_lli     *dst;
};

/* Physical channels */

void d40_phy_cfg(struct stedma40_chan_cfg *cfg,
		 u32 *src_cfg, u32 *dst_cfg, bool is_log);

void d40_log_cfg(struct stedma40_chan_cfg *cfg,
		 u32 *lcsp1, u32 *lcsp2);

int d40_phy_sg_to_lli(struct scatterlist *sg,
		      int sg_len,
		      dma_addr_t target,
		      struct d40_phy_lli *lli,
		      dma_addr_t lli_phys,
		      u32 reg_cfg,
		      u32 data_width,
		      int psize,
		      bool term_int);

int d40_phy_fill_lli(struct d40_phy_lli *lli,
		     dma_addr_t data,
		     u32 data_size,
		     int psize,
		     dma_addr_t next_lli,
		     u32 reg_cfg,
		     bool term_int,
		     u32 data_width,
		     bool is_device);

void d40_phy_lli_write(void __iomem *virtbase,
		       u32 phy_chan_num,
		       struct d40_phy_lli *lli_dst,
		       struct d40_phy_lli *lli_src);

/* Logical channels */

void d40_log_fill_lli(struct d40_log_lli *lli,
		      dma_addr_t data, u32 data_size,
		      u32 lli_next_off, u32 reg_cfg,
		      u32 data_width,
		      bool term_int, bool addr_inc);

int d40_log_sg_to_dev(struct d40_lcla_elem *lcla,
		      struct scatterlist *sg,
		      int sg_len,
		      struct d40_log_lli_bidir *lli,
		      struct d40_def_lcsp *lcsp,
		      u32 src_data_width,
		      u32 dst_data_width,
		      enum dma_data_direction direction,
		      bool term_int, dma_addr_t dev_addr, int max_len,
		      int llis_per_log);

void d40_log_lli_write(struct d40_log_lli_full *lcpa,
		       struct d40_log_lli *lcla_src,
		       struct d40_log_lli *lcla_dst,
		       struct d40_log_lli *lli_dst,
		       struct d40_log_lli *lli_src,
		       int llis_per_log);

int d40_log_sg_to_lli(int lcla_id,
		      struct scatterlist *sg,
		      int sg_len,
		      struct d40_log_lli *lli_sg,
		      u32 lcsp13, /* src or dst*/
		      u32 data_width,
		      bool term_int, int max_len, int llis_per_log);

#endif /* STE_DMA40_LLI_H */

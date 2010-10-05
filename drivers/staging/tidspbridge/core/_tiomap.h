/*
 * _tiomap.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Definitions and types private to this Bridge driver.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _TIOMAP_
#define _TIOMAP_

#include <plat/powerdomain.h>
#include <plat/clockdomain.h>
#include <mach-omap2/prm-regbits-34xx.h>
#include <mach-omap2/cm-regbits-34xx.h>
#include <plat/iommu.h>
#include <plat/iovmm.h>
#include <dspbridge/devdefs.h>
#include <hw_defs.h>
#include <dspbridge/dspioctl.h>	/* for bridge_ioctl_extproc defn */
#include <dspbridge/sync.h>
#include <dspbridge/clk.h>

struct map_l4_peripheral {
	u32 phys_addr;
	u32 dsp_virt_addr;
};

#define ARM_MAILBOX_START               0xfffcf000
#define ARM_MAILBOX_LENGTH              0x800

/* New Registers in OMAP3.1 */

#define TESTBLOCK_ID_START              0xfffed400
#define TESTBLOCK_ID_LENGTH             0xff

/* ID Returned by OMAP1510 */
#define TBC_ID_VALUE                    0xB47002F

#define SPACE_LENGTH                    0x2000
#define API_CLKM_DPLL_DMA               0xfffec000
#define ARM_INTERRUPT_OFFSET            0xb00

#define BIOS24XX

#define L4_PERIPHERAL_NULL          0x0
#define DSPVA_PERIPHERAL_NULL       0x0

#define MAX_LOCK_TLB_ENTRIES 15

#define L4_PERIPHERAL_PRM        0x48306000	/*PRM L4 Peripheral */
#define DSPVA_PERIPHERAL_PRM     0x1181e000
#define L4_PERIPHERAL_SCM        0x48002000	/*SCM L4 Peripheral */
#define DSPVA_PERIPHERAL_SCM     0x1181f000
#define L4_PERIPHERAL_MMU        0x5D000000	/*MMU L4 Peripheral */
#define DSPVA_PERIPHERAL_MMU     0x11820000
#define L4_PERIPHERAL_CM        0x48004000	/* Core L4, Clock Management */
#define DSPVA_PERIPHERAL_CM     0x1181c000
#define L4_PERIPHERAL_PER        0x48005000	/*  PER */
#define DSPVA_PERIPHERAL_PER     0x1181d000

#define L4_PERIPHERAL_GPIO1       0x48310000
#define DSPVA_PERIPHERAL_GPIO1    0x11809000
#define L4_PERIPHERAL_GPIO2       0x49050000
#define DSPVA_PERIPHERAL_GPIO2    0x1180a000
#define L4_PERIPHERAL_GPIO3       0x49052000
#define DSPVA_PERIPHERAL_GPIO3    0x1180b000
#define L4_PERIPHERAL_GPIO4       0x49054000
#define DSPVA_PERIPHERAL_GPIO4    0x1180c000
#define L4_PERIPHERAL_GPIO5       0x49056000
#define DSPVA_PERIPHERAL_GPIO5    0x1180d000

#define L4_PERIPHERAL_IVA2WDT      0x49030000
#define DSPVA_PERIPHERAL_IVA2WDT   0x1180e000

#define L4_PERIPHERAL_DISPLAY     0x48050000
#define DSPVA_PERIPHERAL_DISPLAY  0x1180f000

#define L4_PERIPHERAL_SSI         0x48058000
#define DSPVA_PERIPHERAL_SSI      0x11804000
#define L4_PERIPHERAL_GDD         0x48059000
#define DSPVA_PERIPHERAL_GDD      0x11805000
#define L4_PERIPHERAL_SS1         0x4805a000
#define DSPVA_PERIPHERAL_SS1      0x11806000
#define L4_PERIPHERAL_SS2         0x4805b000
#define DSPVA_PERIPHERAL_SS2      0x11807000

#define L4_PERIPHERAL_CAMERA      0x480BC000
#define DSPVA_PERIPHERAL_CAMERA   0x11819000

#define L4_PERIPHERAL_SDMA        0x48056000
#define DSPVA_PERIPHERAL_SDMA     0x11810000	/* 0x1181d000 conflict w/ PER */

#define L4_PERIPHERAL_UART1             0x4806a000
#define DSPVA_PERIPHERAL_UART1          0x11811000
#define L4_PERIPHERAL_UART2             0x4806c000
#define DSPVA_PERIPHERAL_UART2          0x11812000
#define L4_PERIPHERAL_UART3             0x49020000
#define DSPVA_PERIPHERAL_UART3    0x11813000

#define L4_PERIPHERAL_MCBSP1      0x48074000
#define DSPVA_PERIPHERAL_MCBSP1   0x11814000
#define L4_PERIPHERAL_MCBSP2      0x49022000
#define DSPVA_PERIPHERAL_MCBSP2   0x11815000
#define L4_PERIPHERAL_MCBSP3      0x49024000
#define DSPVA_PERIPHERAL_MCBSP3   0x11816000
#define L4_PERIPHERAL_MCBSP4      0x49026000
#define DSPVA_PERIPHERAL_MCBSP4   0x11817000
#define L4_PERIPHERAL_MCBSP5      0x48096000
#define DSPVA_PERIPHERAL_MCBSP5   0x11818000

#define L4_PERIPHERAL_GPTIMER5    0x49038000
#define DSPVA_PERIPHERAL_GPTIMER5 0x11800000
#define L4_PERIPHERAL_GPTIMER6    0x4903a000
#define DSPVA_PERIPHERAL_GPTIMER6 0x11801000
#define L4_PERIPHERAL_GPTIMER7    0x4903c000
#define DSPVA_PERIPHERAL_GPTIMER7 0x11802000
#define L4_PERIPHERAL_GPTIMER8    0x4903e000
#define DSPVA_PERIPHERAL_GPTIMER8 0x11803000

#define L4_PERIPHERAL_SPI1      0x48098000
#define DSPVA_PERIPHERAL_SPI1   0x1181a000
#define L4_PERIPHERAL_SPI2      0x4809a000
#define DSPVA_PERIPHERAL_SPI2   0x1181b000

#define L4_PERIPHERAL_MBOX        0x48094000
#define DSPVA_PERIPHERAL_MBOX     0x11808000

#define PM_GRPSEL_BASE 			0x48307000
#define DSPVA_GRPSEL_BASE 		0x11821000

#define L4_PERIPHERAL_SIDETONE_MCBSP2        0x49028000
#define DSPVA_PERIPHERAL_SIDETONE_MCBSP2 0x11824000
#define L4_PERIPHERAL_SIDETONE_MCBSP3        0x4902a000
#define DSPVA_PERIPHERAL_SIDETONE_MCBSP3 0x11825000

/* define a static array with L4 mappings */
static const struct map_l4_peripheral l4_peripheral_table[] = {
	{L4_PERIPHERAL_MBOX, DSPVA_PERIPHERAL_MBOX},
	{L4_PERIPHERAL_SCM, DSPVA_PERIPHERAL_SCM},
	{L4_PERIPHERAL_MMU, DSPVA_PERIPHERAL_MMU},
	{L4_PERIPHERAL_GPTIMER5, DSPVA_PERIPHERAL_GPTIMER5},
	{L4_PERIPHERAL_GPTIMER6, DSPVA_PERIPHERAL_GPTIMER6},
	{L4_PERIPHERAL_GPTIMER7, DSPVA_PERIPHERAL_GPTIMER7},
	{L4_PERIPHERAL_GPTIMER8, DSPVA_PERIPHERAL_GPTIMER8},
	{L4_PERIPHERAL_GPIO1, DSPVA_PERIPHERAL_GPIO1},
	{L4_PERIPHERAL_GPIO2, DSPVA_PERIPHERAL_GPIO2},
	{L4_PERIPHERAL_GPIO3, DSPVA_PERIPHERAL_GPIO3},
	{L4_PERIPHERAL_GPIO4, DSPVA_PERIPHERAL_GPIO4},
	{L4_PERIPHERAL_GPIO5, DSPVA_PERIPHERAL_GPIO5},
	{L4_PERIPHERAL_IVA2WDT, DSPVA_PERIPHERAL_IVA2WDT},
	{L4_PERIPHERAL_DISPLAY, DSPVA_PERIPHERAL_DISPLAY},
	{L4_PERIPHERAL_SSI, DSPVA_PERIPHERAL_SSI},
	{L4_PERIPHERAL_GDD, DSPVA_PERIPHERAL_GDD},
	{L4_PERIPHERAL_SS1, DSPVA_PERIPHERAL_SS1},
	{L4_PERIPHERAL_SS2, DSPVA_PERIPHERAL_SS2},
	{L4_PERIPHERAL_UART1, DSPVA_PERIPHERAL_UART1},
	{L4_PERIPHERAL_UART2, DSPVA_PERIPHERAL_UART2},
	{L4_PERIPHERAL_UART3, DSPVA_PERIPHERAL_UART3},
	{L4_PERIPHERAL_MCBSP1, DSPVA_PERIPHERAL_MCBSP1},
	{L4_PERIPHERAL_MCBSP2, DSPVA_PERIPHERAL_MCBSP2},
	{L4_PERIPHERAL_MCBSP3, DSPVA_PERIPHERAL_MCBSP3},
	{L4_PERIPHERAL_MCBSP4, DSPVA_PERIPHERAL_MCBSP4},
	{L4_PERIPHERAL_MCBSP5, DSPVA_PERIPHERAL_MCBSP5},
	{L4_PERIPHERAL_CAMERA, DSPVA_PERIPHERAL_CAMERA},
	{L4_PERIPHERAL_SPI1, DSPVA_PERIPHERAL_SPI1},
	{L4_PERIPHERAL_SPI2, DSPVA_PERIPHERAL_SPI2},
	{L4_PERIPHERAL_PRM, DSPVA_PERIPHERAL_PRM},
	{L4_PERIPHERAL_CM, DSPVA_PERIPHERAL_CM},
	{L4_PERIPHERAL_PER, DSPVA_PERIPHERAL_PER},
	{PM_GRPSEL_BASE, DSPVA_GRPSEL_BASE},
	{L4_PERIPHERAL_SIDETONE_MCBSP2, DSPVA_PERIPHERAL_SIDETONE_MCBSP2},
	{L4_PERIPHERAL_SIDETONE_MCBSP3, DSPVA_PERIPHERAL_SIDETONE_MCBSP3},
	{L4_PERIPHERAL_NULL, DSPVA_PERIPHERAL_NULL}
};

/*
 *   15         10                  0
 *   ---------------------------------
 *  |0|0|1|0|0|0|c|c|c|i|i|i|i|i|i|i|
 *  ---------------------------------
 *  |  (class)  | (module specific) |
 *
 *  where  c -> Externel Clock Command: Clk & Autoidle Disable/Enable
 *  i -> External Clock ID Timers 5,6,7,8, McBSP1,2 and WDT3
 */

/* MBX_PM_CLK_IDMASK: DSP External clock id mask. */
#define MBX_PM_CLK_IDMASK   0x7F

/* MBX_PM_CLK_CMDSHIFT: DSP External clock command shift. */
#define MBX_PM_CLK_CMDSHIFT 7

/* MBX_PM_CLK_CMDMASK: DSP External clock command mask. */
#define MBX_PM_CLK_CMDMASK 7

/* MBX_PM_MAX_RESOURCES: CORE 1 Clock resources. */
#define MBX_CORE1_RESOURCES 7

/* MBX_PM_MAX_RESOURCES: CORE 2 Clock Resources. */
#define MBX_CORE2_RESOURCES 1

/* MBX_PM_MAX_RESOURCES: TOTAL Clock Reosurces. */
#define MBX_PM_MAX_RESOURCES 11

/*  Power Management Commands */
#define BPWR_DISABLE_CLOCK	0
#define BPWR_ENABLE_CLOCK	1

/* OMAP242x specific resources */
enum bpwr_ext_clock_id {
	BPWR_GP_TIMER5 = 0x10,
	BPWR_GP_TIMER6,
	BPWR_GP_TIMER7,
	BPWR_GP_TIMER8,
	BPWR_WD_TIMER3,
	BPWR_MCBSP1,
	BPWR_MCBSP2,
	BPWR_MCBSP3,
	BPWR_MCBSP4,
	BPWR_MCBSP5,
	BPWR_SSI = 0x20
};

static const u32 bpwr_clkid[] = {
	(u32) BPWR_GP_TIMER5,
	(u32) BPWR_GP_TIMER6,
	(u32) BPWR_GP_TIMER7,
	(u32) BPWR_GP_TIMER8,
	(u32) BPWR_WD_TIMER3,
	(u32) BPWR_MCBSP1,
	(u32) BPWR_MCBSP2,
	(u32) BPWR_MCBSP3,
	(u32) BPWR_MCBSP4,
	(u32) BPWR_MCBSP5,
	(u32) BPWR_SSI
};

struct bpwr_clk_t {
	u32 clk_id;
	enum dsp_clk_id clk;
};

static const struct bpwr_clk_t bpwr_clks[] = {
	{(u32) BPWR_GP_TIMER5, DSP_CLK_GPT5},
	{(u32) BPWR_GP_TIMER6, DSP_CLK_GPT6},
	{(u32) BPWR_GP_TIMER7, DSP_CLK_GPT7},
	{(u32) BPWR_GP_TIMER8, DSP_CLK_GPT8},
	{(u32) BPWR_WD_TIMER3, DSP_CLK_WDT3},
	{(u32) BPWR_MCBSP1, DSP_CLK_MCBSP1},
	{(u32) BPWR_MCBSP2, DSP_CLK_MCBSP2},
	{(u32) BPWR_MCBSP3, DSP_CLK_MCBSP3},
	{(u32) BPWR_MCBSP4, DSP_CLK_MCBSP4},
	{(u32) BPWR_MCBSP5, DSP_CLK_MCBSP5},
	{(u32) BPWR_SSI, DSP_CLK_SSI}
};

/* Interrupt Register Offsets */
#define INTH_IT_REG_OFFSET              0x00	/* Interrupt register offset */
#define INTH_MASK_IT_REG_OFFSET         0x04	/* Mask Interrupt reg offset */

#define   DSP_MAILBOX1_INT              10
/*
 *  Bit definition of  Interrupt  Level  Registers
 */

/* Mail Box defines */
#define MB_ARM2DSP1_REG_OFFSET          0x00

#define MB_ARM2DSP1B_REG_OFFSET         0x04

#define MB_DSP2ARM1B_REG_OFFSET         0x0C

#define MB_ARM2DSP1_FLAG_REG_OFFSET     0x18

#define MB_ARM2DSP_FLAG                 0x0001

#define MBOX_ARM2DSP HW_MBOX_ID0
#define MBOX_DSP2ARM HW_MBOX_ID1
#define MBOX_ARM HW_MBOX_U0_ARM
#define MBOX_DSP HW_MBOX_U1_DSP1

#define ENABLE                          true
#define DISABLE                         false

#define HIGH_LEVEL                      true
#define LOW_LEVEL                       false

/* Macro's */
#define CLEAR_BIT(reg, mask)             (reg &= ~mask)
#define SET_BIT(reg, mask)               (reg |= mask)

#define SET_GROUP_BITS16(reg, position, width, value) \
	do {\
		reg &= ~((0xFFFF >> (16 - (width))) << (position)) ; \
		reg |= ((value & (0xFFFF >> (16 - (width)))) << (position)); \
	} while (0);

#define CLEAR_BIT_INDEX(reg, index)   (reg &= ~(1 << (index)))

struct shm_segs {
	u32 seg0_da;
	u32 seg0_pa;
	u32 seg0_va;
	u32 seg0_size;
	u32 seg1_da;
	u32 seg1_pa;
	u32 seg1_va;
	u32 seg1_size;
};


/* This Bridge driver's device context: */
struct bridge_dev_context {
	struct dev_object *hdev_obj;	/* Handle to Bridge device object. */
	u32 dw_dsp_base_addr;	/* Arm's API to DSP virt base addr */
	/*
	 * DSP External memory prog address as seen virtually by the OS on
	 * the host side.
	 */
	u32 dw_dsp_ext_base_addr;	/* See the comment above */
	u32 dw_api_reg_base;	/* API mem map'd registers */
	void __iomem *dw_dsp_mmu_base;	/* DSP MMU Mapped registers */
	u32 dw_api_clk_base;	/* CLK Registers */
	u32 dw_dsp_clk_m2_base;	/* DSP Clock Module m2 */
	u32 dw_public_rhea;	/* Pub Rhea */
	u32 dw_int_addr;	/* MB INTR reg */
	u32 dw_tc_endianism;	/* TC Endianism register */
	u32 dw_test_base;	/* DSP MMU Mapped registers */
	u32 dw_self_loop;	/* Pointer to the selfloop */
	u32 dw_dsp_start_add;	/* API Boot vector */
	u32 dw_internal_size;	/* Internal memory size */

	struct omap_mbox *mbox;		/* Mail box handle */
	struct iommu *dsp_mmu;      /* iommu for iva2 handler */
	struct shm_segs sh_s;
	struct cfg_hostres *resources;	/* Host Resources */

	/*
	 * Processor specific info is set when prog loaded and read from DCD.
	 * [See bridge_dev_ctrl()]  PROC info contains DSP-MMU TLB entries.
	 */
	/* DMMU TLB entries */
	struct bridge_ioctl_extproc atlb_entry[BRDIOCTL_NUMOFMMUTLB];
	u32 dw_brd_state;       /* Last known board state. */

	/* TC Settings */
	bool tc_word_swap_on;	/* Traffic Controller Word Swap */
	struct pg_table_attrs *pt_attrs;
	u32 dsp_per_clks;
};

/*
 * If dsp_debug is true, do not branch to the DSP entry
 * point and wait for DSP to boot.
 */
extern s32 dsp_debug;

/*
 *  ======== sm_interrupt_dsp ========
 *  Purpose:
 *      Set interrupt value & send an interrupt to the DSP processor(s).
 *      This is typicaly used when mailbox interrupt mechanisms allow data
 *      to be associated with interrupt such as for OMAP's CMD/DATA regs.
 *  Parameters:
 *      dev_context:    Handle to Bridge driver defined device info.
 *      mb_val:         Value associated with interrupt(e.g. mailbox value).
 *  Returns:
 *      0:        Interrupt sent;
 *      else:           Unable to send interrupt.
 *  Requires:
 *  Ensures:
 */
int sm_interrupt_dsp(struct bridge_dev_context *dev_context, u16 mb_val);

/**
 * user_to_dsp_map() - maps user to dsp virtual address
 * @mmu:	Pointer to iommu handle.
 * @uva:		Virtual user space address.
 * @da		DSP address
 * @size		Buffer size to map.
 * @usr_pgs	struct page array pointer where the user pages will be stored
 *
 * This function maps a user space buffer into DSP virtual address.
 *
 */
u32 user_to_dsp_map(struct iommu *mmu, u32 uva, u32 da, u32 size,
						struct page **usr_pgs);

/**
 * user_to_dsp_unmap() - unmaps DSP virtual buffer.
 * @mmu:	Pointer to iommu handle.
 * @da		DSP address
 *
 * This function unmaps a user space buffer into DSP virtual address.
 *
 */
int user_to_dsp_unmap(struct iommu *mmu, u32 da);

#endif /* _TIOMAP_ */

/*
 *  Copyright © 2014-2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef VC4_REGS_H
#define VC4_REGS_H

#include <linux/bitops.h>

#define VC4_MASK(high, low) ((u32)GENMASK(high, low))
/* Using the GNU statement expression extension */
#define VC4_SET_FIELD(value, field)					\
	({								\
		uint32_t fieldval = (value) << field##_SHIFT;		\
		WARN_ON((fieldval & ~field##_MASK) != 0);		\
		fieldval & field##_MASK;				\
	 })

#define VC4_GET_FIELD(word, field) (((word) & field##_MASK) >>		\
				    field##_SHIFT)

#define V3D_IDENT0   0x00000
# define V3D_EXPECTED_IDENT0 \
	((2 << 24) | \
	('V' << 0) | \
	('3' << 8) | \
	 ('D' << 16))

#define V3D_IDENT1   0x00004
/* Multiples of 1kb */
# define V3D_IDENT1_VPM_SIZE_MASK                      VC4_MASK(31, 28)
# define V3D_IDENT1_VPM_SIZE_SHIFT                     28
# define V3D_IDENT1_NSEM_MASK                          VC4_MASK(23, 16)
# define V3D_IDENT1_NSEM_SHIFT                         16
# define V3D_IDENT1_TUPS_MASK                          VC4_MASK(15, 12)
# define V3D_IDENT1_TUPS_SHIFT                         12
# define V3D_IDENT1_QUPS_MASK                          VC4_MASK(11, 8)
# define V3D_IDENT1_QUPS_SHIFT                         8
# define V3D_IDENT1_NSLC_MASK                          VC4_MASK(7, 4)
# define V3D_IDENT1_NSLC_SHIFT                         4
# define V3D_IDENT1_REV_MASK                           VC4_MASK(3, 0)
# define V3D_IDENT1_REV_SHIFT                          0

#define V3D_IDENT2   0x00008
#define V3D_SCRATCH  0x00010
#define V3D_L2CACTL  0x00020
# define V3D_L2CACTL_L2CCLR                            BIT(2)
# define V3D_L2CACTL_L2CDIS                            BIT(1)
# define V3D_L2CACTL_L2CENA                            BIT(0)

#define V3D_SLCACTL  0x00024
# define V3D_SLCACTL_T1CC_MASK                         VC4_MASK(27, 24)
# define V3D_SLCACTL_T1CC_SHIFT                        24
# define V3D_SLCACTL_T0CC_MASK                         VC4_MASK(19, 16)
# define V3D_SLCACTL_T0CC_SHIFT                        16
# define V3D_SLCACTL_UCC_MASK                          VC4_MASK(11, 8)
# define V3D_SLCACTL_UCC_SHIFT                         8
# define V3D_SLCACTL_ICC_MASK                          VC4_MASK(3, 0)
# define V3D_SLCACTL_ICC_SHIFT                         0

#define V3D_INTCTL   0x00030
#define V3D_INTENA   0x00034
#define V3D_INTDIS   0x00038
# define V3D_INT_SPILLUSE                              BIT(3)
# define V3D_INT_OUTOMEM                               BIT(2)
# define V3D_INT_FLDONE                                BIT(1)
# define V3D_INT_FRDONE                                BIT(0)

#define V3D_CT0CS    0x00100
#define V3D_CT1CS    0x00104
#define V3D_CTNCS(n) (V3D_CT0CS + 4 * n)
# define V3D_CTRSTA      BIT(15)
# define V3D_CTSEMA      BIT(12)
# define V3D_CTRTSD      BIT(8)
# define V3D_CTRUN       BIT(5)
# define V3D_CTSUBS      BIT(4)
# define V3D_CTERR       BIT(3)
# define V3D_CTMODE      BIT(0)

#define V3D_CT0EA    0x00108
#define V3D_CT1EA    0x0010c
#define V3D_CTNEA(n) (V3D_CT0EA + 4 * (n))
#define V3D_CT0CA    0x00110
#define V3D_CT1CA    0x00114
#define V3D_CTNCA(n) (V3D_CT0CA + 4 * (n))
#define V3D_CT00RA0  0x00118
#define V3D_CT01RA0  0x0011c
#define V3D_CTNRA0(n) (V3D_CT00RA0 + 4 * (n))
#define V3D_CT0LC    0x00120
#define V3D_CT1LC    0x00124
#define V3D_CTNLC(n) (V3D_CT0LC + 4 * (n))
#define V3D_CT0PC    0x00128
#define V3D_CT1PC    0x0012c
#define V3D_CTNPC(n) (V3D_CT0PC + 4 * (n))

#define V3D_PCS      0x00130
# define V3D_BMOOM       BIT(8)
# define V3D_RMBUSY      BIT(3)
# define V3D_RMACTIVE    BIT(2)
# define V3D_BMBUSY      BIT(1)
# define V3D_BMACTIVE    BIT(0)

#define V3D_BFC      0x00134
#define V3D_RFC      0x00138
#define V3D_BPCA     0x00300
#define V3D_BPCS     0x00304
#define V3D_BPOA     0x00308
#define V3D_BPOS     0x0030c
#define V3D_BXCF     0x00310
#define V3D_SQRSV0   0x00410
#define V3D_SQRSV1   0x00414
#define V3D_SQCNTL   0x00418
#define V3D_SRQPC    0x00430
#define V3D_SRQUA    0x00434
#define V3D_SRQUL    0x00438
#define V3D_SRQCS    0x0043c
#define V3D_VPACNTL  0x00500
#define V3D_VPMBASE  0x00504
#define V3D_PCTRC    0x00670
#define V3D_PCTRE    0x00674
#define V3D_PCTR0    0x00680
#define V3D_PCTRS0   0x00684
#define V3D_PCTR1    0x00688
#define V3D_PCTRS1   0x0068c
#define V3D_PCTR2    0x00690
#define V3D_PCTRS2   0x00694
#define V3D_PCTR3    0x00698
#define V3D_PCTRS3   0x0069c
#define V3D_PCTR4    0x006a0
#define V3D_PCTRS4   0x006a4
#define V3D_PCTR5    0x006a8
#define V3D_PCTRS5   0x006ac
#define V3D_PCTR6    0x006b0
#define V3D_PCTRS6   0x006b4
#define V3D_PCTR7    0x006b8
#define V3D_PCTRS7   0x006bc
#define V3D_PCTR8    0x006c0
#define V3D_PCTRS8   0x006c4
#define V3D_PCTR9    0x006c8
#define V3D_PCTRS9   0x006cc
#define V3D_PCTR10   0x006d0
#define V3D_PCTRS10  0x006d4
#define V3D_PCTR11   0x006d8
#define V3D_PCTRS11  0x006dc
#define V3D_PCTR12   0x006e0
#define V3D_PCTRS12  0x006e4
#define V3D_PCTR13   0x006e8
#define V3D_PCTRS13  0x006ec
#define V3D_PCTR14   0x006f0
#define V3D_PCTRS14  0x006f4
#define V3D_PCTR15   0x006f8
#define V3D_PCTRS15  0x006fc
#define V3D_DBGE     0x00f00
#define V3D_FDBGO    0x00f04
#define V3D_FDBGB    0x00f08
#define V3D_FDBGR    0x00f0c
#define V3D_FDBGS    0x00f10
#define V3D_ERRSTAT  0x00f20

#define PV_CONTROL				0x00
# define PV_CONTROL_FORMAT_MASK			VC4_MASK(23, 21)
# define PV_CONTROL_FORMAT_SHIFT		21
# define PV_CONTROL_FORMAT_24			0
# define PV_CONTROL_FORMAT_DSIV_16		1
# define PV_CONTROL_FORMAT_DSIC_16		2
# define PV_CONTROL_FORMAT_DSIV_18		3
# define PV_CONTROL_FORMAT_DSIV_24		4

# define PV_CONTROL_FIFO_LEVEL_MASK		VC4_MASK(20, 15)
# define PV_CONTROL_FIFO_LEVEL_SHIFT		15
# define PV_CONTROL_CLR_AT_START		BIT(14)
# define PV_CONTROL_TRIGGER_UNDERFLOW		BIT(13)
# define PV_CONTROL_WAIT_HSTART			BIT(12)
# define PV_CONTROL_PIXEL_REP_MASK		VC4_MASK(5, 4)
# define PV_CONTROL_PIXEL_REP_SHIFT		4
# define PV_CONTROL_CLK_SELECT_DSI		0
# define PV_CONTROL_CLK_SELECT_DPI_SMI_HDMI	1
# define PV_CONTROL_CLK_SELECT_VEC		2
# define PV_CONTROL_CLK_SELECT_MASK		VC4_MASK(3, 2)
# define PV_CONTROL_CLK_SELECT_SHIFT		2
# define PV_CONTROL_FIFO_CLR			BIT(1)
# define PV_CONTROL_EN				BIT(0)

#define PV_V_CONTROL				0x04
# define PV_VCONTROL_ODD_DELAY_MASK		VC4_MASK(22, 6)
# define PV_VCONTROL_ODD_DELAY_SHIFT		6
# define PV_VCONTROL_ODD_FIRST			BIT(5)
# define PV_VCONTROL_INTERLACE			BIT(4)
# define PV_VCONTROL_DSI			BIT(3)
# define PV_VCONTROL_COMMAND			BIT(2)
# define PV_VCONTROL_CONTINUOUS			BIT(1)
# define PV_VCONTROL_VIDEN			BIT(0)

#define PV_VSYNCD_EVEN				0x08

#define PV_HORZA				0x0c
# define PV_HORZA_HBP_MASK			VC4_MASK(31, 16)
# define PV_HORZA_HBP_SHIFT			16
# define PV_HORZA_HSYNC_MASK			VC4_MASK(15, 0)
# define PV_HORZA_HSYNC_SHIFT			0

#define PV_HORZB				0x10
# define PV_HORZB_HFP_MASK			VC4_MASK(31, 16)
# define PV_HORZB_HFP_SHIFT			16
# define PV_HORZB_HACTIVE_MASK			VC4_MASK(15, 0)
# define PV_HORZB_HACTIVE_SHIFT			0

#define PV_VERTA				0x14
# define PV_VERTA_VBP_MASK			VC4_MASK(31, 16)
# define PV_VERTA_VBP_SHIFT			16
# define PV_VERTA_VSYNC_MASK			VC4_MASK(15, 0)
# define PV_VERTA_VSYNC_SHIFT			0

#define PV_VERTB				0x18
# define PV_VERTB_VFP_MASK			VC4_MASK(31, 16)
# define PV_VERTB_VFP_SHIFT			16
# define PV_VERTB_VACTIVE_MASK			VC4_MASK(15, 0)
# define PV_VERTB_VACTIVE_SHIFT			0

#define PV_VERTA_EVEN				0x1c
#define PV_VERTB_EVEN				0x20

#define PV_INTEN				0x24
#define PV_INTSTAT				0x28
# define PV_INT_VID_IDLE			BIT(9)
# define PV_INT_VFP_END				BIT(8)
# define PV_INT_VFP_START			BIT(7)
# define PV_INT_VACT_START			BIT(6)
# define PV_INT_VBP_START			BIT(5)
# define PV_INT_VSYNC_START			BIT(4)
# define PV_INT_HFP_START			BIT(3)
# define PV_INT_HACT_START			BIT(2)
# define PV_INT_HBP_START			BIT(1)
# define PV_INT_HSYNC_START			BIT(0)

#define PV_STAT					0x2c

#define PV_HACT_ACT				0x30

#define SCALER_DISPCTRL                         0x00000000
/* Global register for clock gating the HVS */
# define SCALER_DISPCTRL_ENABLE			BIT(31)
# define SCALER_DISPCTRL_DSP2EISLUR		BIT(15)
# define SCALER_DISPCTRL_DSP1EISLUR		BIT(14)
# define SCALER_DISPCTRL_DSP3_MUX_MASK		VC4_MASK(19, 18)
# define SCALER_DISPCTRL_DSP3_MUX_SHIFT		18

/* Enables Display 0 short line and underrun contribution to
 * SCALER_DISPSTAT_IRQDISP0.  Note that short frame contributions are
 * always enabled.
 */
# define SCALER_DISPCTRL_DSP0EISLUR		BIT(13)
# define SCALER_DISPCTRL_DSP2EIEOLN		BIT(12)
# define SCALER_DISPCTRL_DSP2EIEOF		BIT(11)
# define SCALER_DISPCTRL_DSP1EIEOLN		BIT(10)
# define SCALER_DISPCTRL_DSP1EIEOF		BIT(9)
/* Enables Display 0 end-of-line-N contribution to
 * SCALER_DISPSTAT_IRQDISP0
 */
# define SCALER_DISPCTRL_DSP0EIEOLN		BIT(8)
/* Enables Display 0 EOF contribution to SCALER_DISPSTAT_IRQDISP0 */
# define SCALER_DISPCTRL_DSP0EIEOF		BIT(7)

# define SCALER_DISPCTRL_SLVRDEIRQ		BIT(6)
# define SCALER_DISPCTRL_SLVWREIRQ		BIT(5)
# define SCALER_DISPCTRL_DMAEIRQ		BIT(4)
# define SCALER_DISPCTRL_DISP2EIRQ		BIT(3)
# define SCALER_DISPCTRL_DISP1EIRQ		BIT(2)
/* Enables interrupt generation on the enabled EOF/EOLN/EISLUR
 * bits and short frames..
 */
# define SCALER_DISPCTRL_DISP0EIRQ		BIT(1)
/* Enables interrupt generation on scaler profiler interrupt. */
# define SCALER_DISPCTRL_SCLEIRQ		BIT(0)

#define SCALER_DISPSTAT                         0x00000004
# define SCALER_DISPSTAT_COBLOW2		BIT(29)
# define SCALER_DISPSTAT_EOLN2			BIT(28)
# define SCALER_DISPSTAT_ESFRAME2		BIT(27)
# define SCALER_DISPSTAT_ESLINE2		BIT(26)
# define SCALER_DISPSTAT_EUFLOW2		BIT(25)
# define SCALER_DISPSTAT_EOF2			BIT(24)

# define SCALER_DISPSTAT_COBLOW1		BIT(21)
# define SCALER_DISPSTAT_EOLN1			BIT(20)
# define SCALER_DISPSTAT_ESFRAME1		BIT(19)
# define SCALER_DISPSTAT_ESLINE1		BIT(18)
# define SCALER_DISPSTAT_EUFLOW1		BIT(17)
# define SCALER_DISPSTAT_EOF1			BIT(16)

# define SCALER_DISPSTAT_RESP_MASK		VC4_MASK(15, 14)
# define SCALER_DISPSTAT_RESP_SHIFT		14
# define SCALER_DISPSTAT_RESP_OKAY		0
# define SCALER_DISPSTAT_RESP_EXOKAY		1
# define SCALER_DISPSTAT_RESP_SLVERR		2
# define SCALER_DISPSTAT_RESP_DECERR		3

# define SCALER_DISPSTAT_COBLOW0		BIT(13)
/* Set when the DISPEOLN line is done compositing. */
# define SCALER_DISPSTAT_EOLN0			BIT(12)
/* Set when VSTART is seen but there are still pixels in the current
 * output line.
 */
# define SCALER_DISPSTAT_ESFRAME0		BIT(11)
/* Set when HSTART is seen but there are still pixels in the current
 * output line.
 */
# define SCALER_DISPSTAT_ESLINE0		BIT(10)
/* Set when the the downstream tries to read from the display FIFO
 * while it's empty.
 */
# define SCALER_DISPSTAT_EUFLOW0		BIT(9)
/* Set when the display mode changes from RUN to EOF */
# define SCALER_DISPSTAT_EOF0			BIT(8)

/* Set on AXI invalid DMA ID error. */
# define SCALER_DISPSTAT_DMA_ERROR		BIT(7)
/* Set on AXI slave read decode error */
# define SCALER_DISPSTAT_IRQSLVRD		BIT(6)
/* Set on AXI slave write decode error */
# define SCALER_DISPSTAT_IRQSLVWR		BIT(5)
/* Set when SCALER_DISPSTAT_DMA_ERROR is set, or
 * SCALER_DISPSTAT_RESP_ERROR is not SCALER_DISPSTAT_RESP_OKAY.
 */
# define SCALER_DISPSTAT_IRQDMA			BIT(4)
# define SCALER_DISPSTAT_IRQDISP2		BIT(3)
# define SCALER_DISPSTAT_IRQDISP1		BIT(2)
/* Set when any of the EOF/EOLN/ESFRAME/ESLINE bits are set and their
 * corresponding interrupt bit is enabled in DISPCTRL.
 */
# define SCALER_DISPSTAT_IRQDISP0		BIT(1)
/* On read, the profiler interrupt.  On write, clear *all* interrupt bits. */
# define SCALER_DISPSTAT_IRQSCL			BIT(0)

#define SCALER_DISPID                           0x00000008
#define SCALER_DISPECTRL                        0x0000000c
#define SCALER_DISPPROF                         0x00000010
#define SCALER_DISPDITHER                       0x00000014
#define SCALER_DISPEOLN                         0x00000018
#define SCALER_DISPLIST0                        0x00000020
#define SCALER_DISPLIST1                        0x00000024
#define SCALER_DISPLIST2                        0x00000028
#define SCALER_DISPLSTAT                        0x0000002c
#define SCALER_DISPLISTX(x)			(SCALER_DISPLIST0 +	\
						 (x) * (SCALER_DISPLIST1 - \
							SCALER_DISPLIST0))

#define SCALER_DISPLACT0                        0x00000030
#define SCALER_DISPLACT1                        0x00000034
#define SCALER_DISPLACT2                        0x00000038
#define SCALER_DISPLACTX(x)			(SCALER_DISPLACT0 +	\
						 (x) * (SCALER_DISPLACT1 - \
							SCALER_DISPLACT0))

#define SCALER_DISPCTRL0                        0x00000040
# define SCALER_DISPCTRLX_ENABLE		BIT(31)
# define SCALER_DISPCTRLX_RESET			BIT(30)
# define SCALER_DISPCTRLX_WIDTH_MASK		VC4_MASK(23, 12)
# define SCALER_DISPCTRLX_WIDTH_SHIFT		12
# define SCALER_DISPCTRLX_HEIGHT_MASK		VC4_MASK(11, 0)
# define SCALER_DISPCTRLX_HEIGHT_SHIFT		0

#define SCALER_DISPBKGND0                       0x00000044
# define SCALER_DISPBKGND_AUTOHS		BIT(31)
# define SCALER_DISPBKGND_INTERLACE		BIT(30)
# define SCALER_DISPBKGND_GAMMA			BIT(29)
# define SCALER_DISPBKGND_TESTMODE_MASK		VC4_MASK(28, 25)
# define SCALER_DISPBKGND_TESTMODE_SHIFT	25
/* Enables filling the scaler line with the RGB value in the low 24
 * bits before compositing.  Costs cycles, so should be skipped if
 * opaque display planes will cover everything.
 */
# define SCALER_DISPBKGND_FILL			BIT(24)

#define SCALER_DISPSTAT0                        0x00000048
# define SCALER_DISPSTATX_MODE_MASK		VC4_MASK(31, 30)
# define SCALER_DISPSTATX_MODE_SHIFT		30
# define SCALER_DISPSTATX_MODE_DISABLED		0
# define SCALER_DISPSTATX_MODE_INIT		1
# define SCALER_DISPSTATX_MODE_RUN		2
# define SCALER_DISPSTATX_MODE_EOF		3
# define SCALER_DISPSTATX_FULL			BIT(29)
# define SCALER_DISPSTATX_EMPTY			BIT(28)
# define SCALER_DISPSTATX_FRAME_COUNT_MASK	VC4_MASK(17, 12)
# define SCALER_DISPSTATX_FRAME_COUNT_SHIFT	12
# define SCALER_DISPSTATX_LINE_MASK		VC4_MASK(11, 0)
# define SCALER_DISPSTATX_LINE_SHIFT		0

#define SCALER_DISPBASE0                        0x0000004c
/* Last pixel in the COB (display FIFO memory) allocated to this HVS
 * channel.  Must be 4-pixel aligned (and thus 4 pixels less than the
 * next COB base).
 */
# define SCALER_DISPBASEX_TOP_MASK		VC4_MASK(31, 16)
# define SCALER_DISPBASEX_TOP_SHIFT		16
/* First pixel in the COB (display FIFO memory) allocated to this HVS
 * channel.  Must be 4-pixel aligned.
 */
# define SCALER_DISPBASEX_BASE_MASK		VC4_MASK(15, 0)
# define SCALER_DISPBASEX_BASE_SHIFT		0

#define SCALER_DISPCTRL1                        0x00000050
#define SCALER_DISPBKGND1                       0x00000054
#define SCALER_DISPBKGNDX(x)			(SCALER_DISPBKGND0 +        \
						 (x) * (SCALER_DISPBKGND1 - \
							SCALER_DISPBKGND0))
#define SCALER_DISPSTAT1                        0x00000058
#define SCALER_DISPSTATX(x)			(SCALER_DISPSTAT0 +        \
						 (x) * (SCALER_DISPSTAT1 - \
							SCALER_DISPSTAT0))
#define SCALER_DISPBASE1                        0x0000005c
#define SCALER_DISPBASEX(x)			(SCALER_DISPBASE0 +        \
						 (x) * (SCALER_DISPBASE1 - \
							SCALER_DISPBASE0))
#define SCALER_DISPCTRL2                        0x00000060
#define SCALER_DISPCTRLX(x)			(SCALER_DISPCTRL0 +        \
						 (x) * (SCALER_DISPCTRL1 - \
							SCALER_DISPCTRL0))
#define SCALER_DISPBKGND2                       0x00000064
#define SCALER_DISPSTAT2                        0x00000068
#define SCALER_DISPBASE2                        0x0000006c
#define SCALER_DISPALPHA2                       0x00000070
#define SCALER_GAMADDR                          0x00000078
# define SCALER_GAMADDR_AUTOINC			BIT(31)
/* Enables all gamma ramp SRAMs, not just those of CRTCs with gamma
 * enabled.
 */
# define SCALER_GAMADDR_SRAMENB			BIT(30)

#define SCALER_GAMDATA                          0x000000e0
#define SCALER_DLIST_START                      0x00002000
#define SCALER_DLIST_SIZE                       0x00004000

#define VC4_HDMI_CORE_REV			0x000

#define VC4_HDMI_SW_RESET_CONTROL		0x004
# define VC4_HDMI_SW_RESET_FORMAT_DETECT	BIT(1)
# define VC4_HDMI_SW_RESET_HDMI			BIT(0)

#define VC4_HDMI_HOTPLUG_INT			0x008

#define VC4_HDMI_HOTPLUG			0x00c
# define VC4_HDMI_HOTPLUG_CONNECTED		BIT(0)

/* 3 bits per field, where each field maps from that corresponding MAI
 * bus channel to the given HDMI channel.
 */
#define VC4_HDMI_MAI_CHANNEL_MAP		0x090

#define VC4_HDMI_MAI_CONFIG			0x094
# define VC4_HDMI_MAI_CONFIG_FORMAT_REVERSE		BIT(27)
# define VC4_HDMI_MAI_CONFIG_BIT_REVERSE		BIT(26)
# define VC4_HDMI_MAI_CHANNEL_MASK_MASK			VC4_MASK(15, 0)
# define VC4_HDMI_MAI_CHANNEL_MASK_SHIFT		0

/* Last received format word on the MAI bus. */
#define VC4_HDMI_MAI_FORMAT			0x098

#define VC4_HDMI_AUDIO_PACKET_CONFIG		0x09c
# define VC4_HDMI_AUDIO_PACKET_ZERO_DATA_ON_SAMPLE_FLAT		BIT(29)
# define VC4_HDMI_AUDIO_PACKET_ZERO_DATA_ON_INACTIVE_CHANNELS	BIT(24)
# define VC4_HDMI_AUDIO_PACKET_FORCE_SAMPLE_PRESENT		BIT(19)
# define VC4_HDMI_AUDIO_PACKET_FORCE_B_FRAME			BIT(18)
# define VC4_HDMI_AUDIO_PACKET_B_FRAME_IDENTIFIER_MASK		VC4_MASK(13, 10)
# define VC4_HDMI_AUDIO_PACKET_B_FRAME_IDENTIFIER_SHIFT		10
/* If set, then multichannel, otherwise 2 channel. */
# define VC4_HDMI_AUDIO_PACKET_AUDIO_LAYOUT			BIT(9)
/* If set, then AUDIO_LAYOUT overrides audio_cea_mask */
# define VC4_HDMI_AUDIO_PACKET_FORCE_AUDIO_LAYOUT		BIT(8)
# define VC4_HDMI_AUDIO_PACKET_CEA_MASK_MASK			VC4_MASK(7, 0)
# define VC4_HDMI_AUDIO_PACKET_CEA_MASK_SHIFT			0

#define VC4_HDMI_RAM_PACKET_CONFIG		0x0a0
# define VC4_HDMI_RAM_PACKET_ENABLE		BIT(16)

#define VC4_HDMI_RAM_PACKET_STATUS		0x0a4

#define VC4_HDMI_CRP_CFG			0x0a8
/* When set, the CTS_PERIOD counts based on MAI bus sync pulse instead
 * of pixel clock.
 */
# define VC4_HDMI_CRP_USE_MAI_BUS_SYNC_FOR_CTS	BIT(26)
/* When set, no CRP packets will be sent. */
# define VC4_HDMI_CRP_CFG_DISABLE		BIT(25)
/* If set, generates CTS values based on N, audio clock, and video
 * clock.  N must be divisible by 128.
 */
# define VC4_HDMI_CRP_CFG_EXTERNAL_CTS_EN	BIT(24)
# define VC4_HDMI_CRP_CFG_N_MASK		VC4_MASK(19, 0)
# define VC4_HDMI_CRP_CFG_N_SHIFT		0

/* 20-bit fields containing CTS values to be transmitted if !EXTERNAL_CTS_EN */
#define VC4_HDMI_CTS_0				0x0ac
#define VC4_HDMI_CTS_1				0x0b0
/* 20-bit fields containing number of clocks to send CTS0/1 before
 * switching to the other one.
 */
#define VC4_HDMI_CTS_PERIOD_0			0x0b4
#define VC4_HDMI_CTS_PERIOD_1			0x0b8

#define VC4_HDMI_HORZA				0x0c4
# define VC4_HDMI_HORZA_VPOS			BIT(14)
# define VC4_HDMI_HORZA_HPOS			BIT(13)
/* Horizontal active pixels (hdisplay). */
# define VC4_HDMI_HORZA_HAP_MASK		VC4_MASK(12, 0)
# define VC4_HDMI_HORZA_HAP_SHIFT		0

#define VC4_HDMI_HORZB				0x0c8
/* Horizontal pack porch (htotal - hsync_end). */
# define VC4_HDMI_HORZB_HBP_MASK		VC4_MASK(29, 20)
# define VC4_HDMI_HORZB_HBP_SHIFT		20
/* Horizontal sync pulse (hsync_end - hsync_start). */
# define VC4_HDMI_HORZB_HSP_MASK		VC4_MASK(19, 10)
# define VC4_HDMI_HORZB_HSP_SHIFT		10
/* Horizontal front porch (hsync_start - hdisplay). */
# define VC4_HDMI_HORZB_HFP_MASK		VC4_MASK(9, 0)
# define VC4_HDMI_HORZB_HFP_SHIFT		0

#define VC4_HDMI_FIFO_CTL			0x05c
# define VC4_HDMI_FIFO_CTL_RECENTER_DONE	BIT(14)
# define VC4_HDMI_FIFO_CTL_USE_EMPTY		BIT(13)
# define VC4_HDMI_FIFO_CTL_ON_VB		BIT(7)
# define VC4_HDMI_FIFO_CTL_RECENTER		BIT(6)
# define VC4_HDMI_FIFO_CTL_FIFO_RESET		BIT(5)
# define VC4_HDMI_FIFO_CTL_USE_PLL_LOCK		BIT(4)
# define VC4_HDMI_FIFO_CTL_INV_CLK_XFR		BIT(3)
# define VC4_HDMI_FIFO_CTL_CAPTURE_PTR		BIT(2)
# define VC4_HDMI_FIFO_CTL_USE_FULL		BIT(1)
# define VC4_HDMI_FIFO_CTL_MASTER_SLAVE_N	BIT(0)
# define VC4_HDMI_FIFO_VALID_WRITE_MASK		0xefff

#define VC4_HDMI_SCHEDULER_CONTROL		0x0c0
# define VC4_HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT BIT(15)
# define VC4_HDMI_SCHEDULER_CONTROL_IGNORE_VSYNC_PREDICTS BIT(5)
# define VC4_HDMI_SCHEDULER_CONTROL_VERT_ALWAYS_KEEPOUT	BIT(3)
# define VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE	BIT(1)
# define VC4_HDMI_SCHEDULER_CONTROL_MODE_HDMI	BIT(0)

#define VC4_HDMI_VERTA0				0x0cc
#define VC4_HDMI_VERTA1				0x0d4
/* Vertical sync pulse (vsync_end - vsync_start). */
# define VC4_HDMI_VERTA_VSP_MASK		VC4_MASK(24, 20)
# define VC4_HDMI_VERTA_VSP_SHIFT		20
/* Vertical front porch (vsync_start - vdisplay). */
# define VC4_HDMI_VERTA_VFP_MASK		VC4_MASK(19, 13)
# define VC4_HDMI_VERTA_VFP_SHIFT		13
/* Vertical active lines (vdisplay). */
# define VC4_HDMI_VERTA_VAL_MASK		VC4_MASK(12, 0)
# define VC4_HDMI_VERTA_VAL_SHIFT		0

#define VC4_HDMI_VERTB0				0x0d0
#define VC4_HDMI_VERTB1				0x0d8
/* Vertical sync pulse offset (for interlaced) */
# define VC4_HDMI_VERTB_VSPO_MASK		VC4_MASK(21, 9)
# define VC4_HDMI_VERTB_VSPO_SHIFT		9
/* Vertical pack porch (vtotal - vsync_end). */
# define VC4_HDMI_VERTB_VBP_MASK		VC4_MASK(8, 0)
# define VC4_HDMI_VERTB_VBP_SHIFT		0

#define VC4_HDMI_CEC_CNTRL_1			0x0e8
/* Set when the transmission has ended. */
# define VC4_HDMI_CEC_TX_EOM			BIT(31)
/* If set, transmission was acked on the 1st or 2nd attempt (only one
 * retry is attempted).  If in continuous mode, this means TX needs to
 * be filled if !TX_EOM.
 */
# define VC4_HDMI_CEC_TX_STATUS_GOOD		BIT(30)
# define VC4_HDMI_CEC_RX_EOM			BIT(29)
# define VC4_HDMI_CEC_RX_STATUS_GOOD		BIT(28)
/* Number of bytes received for the message. */
# define VC4_HDMI_CEC_REC_WRD_CNT_MASK		VC4_MASK(27, 24)
# define VC4_HDMI_CEC_REC_WRD_CNT_SHIFT		24
/* Sets continuous receive mode.  Generates interrupt after each 8
 * bytes to signal that RX_DATA should be consumed, and at RX_EOM.
 *
 * If disabled, maximum 16 bytes will be received (including header),
 * and interrupt at RX_EOM.  Later bytes will be acked but not put
 * into the RX_DATA.
 */
# define VC4_HDMI_CEC_RX_CONTINUE		BIT(23)
# define VC4_HDMI_CEC_TX_CONTINUE		BIT(22)
/* Set this after a CEC interrupt. */
# define VC4_HDMI_CEC_CLEAR_RECEIVE_OFF		BIT(21)
/* Starts a TX.  Will wait for appropriate idel time before CEC
 * activity. Must be cleared in between transmits.
 */
# define VC4_HDMI_CEC_START_XMIT_BEGIN		BIT(20)
# define VC4_HDMI_CEC_MESSAGE_LENGTH_MASK	VC4_MASK(19, 16)
# define VC4_HDMI_CEC_MESSAGE_LENGTH_SHIFT	16
/* Device's CEC address */
# define VC4_HDMI_CEC_ADDR_MASK			VC4_MASK(15, 12)
# define VC4_HDMI_CEC_ADDR_SHIFT		12
/* Divides off of HSM clock to generate CEC bit clock. */
/* With the current defaults the CEC bit clock is 40 kHz = 25 usec */
# define VC4_HDMI_CEC_DIV_CLK_CNT_MASK		VC4_MASK(11, 0)
# define VC4_HDMI_CEC_DIV_CLK_CNT_SHIFT		0

/* Set these fields to how many bit clock cycles get to that many
 * microseconds.
 */
#define VC4_HDMI_CEC_CNTRL_2			0x0ec
# define VC4_HDMI_CEC_CNT_TO_1500_US_MASK	VC4_MASK(30, 24)
# define VC4_HDMI_CEC_CNT_TO_1500_US_SHIFT	24
# define VC4_HDMI_CEC_CNT_TO_1300_US_MASK	VC4_MASK(23, 17)
# define VC4_HDMI_CEC_CNT_TO_1300_US_SHIFT	17
# define VC4_HDMI_CEC_CNT_TO_800_US_MASK	VC4_MASK(16, 11)
# define VC4_HDMI_CEC_CNT_TO_800_US_SHIFT	11
# define VC4_HDMI_CEC_CNT_TO_600_US_MASK	VC4_MASK(10, 5)
# define VC4_HDMI_CEC_CNT_TO_600_US_SHIFT	5
# define VC4_HDMI_CEC_CNT_TO_400_US_MASK	VC4_MASK(4, 0)
# define VC4_HDMI_CEC_CNT_TO_400_US_SHIFT	0

#define VC4_HDMI_CEC_CNTRL_3			0x0f0
# define VC4_HDMI_CEC_CNT_TO_2750_US_MASK	VC4_MASK(31, 24)
# define VC4_HDMI_CEC_CNT_TO_2750_US_SHIFT	24
# define VC4_HDMI_CEC_CNT_TO_2400_US_MASK	VC4_MASK(23, 16)
# define VC4_HDMI_CEC_CNT_TO_2400_US_SHIFT	16
# define VC4_HDMI_CEC_CNT_TO_2050_US_MASK	VC4_MASK(15, 8)
# define VC4_HDMI_CEC_CNT_TO_2050_US_SHIFT	8
# define VC4_HDMI_CEC_CNT_TO_1700_US_MASK	VC4_MASK(7, 0)
# define VC4_HDMI_CEC_CNT_TO_1700_US_SHIFT	0

#define VC4_HDMI_CEC_CNTRL_4			0x0f4
# define VC4_HDMI_CEC_CNT_TO_4300_US_MASK	VC4_MASK(31, 24)
# define VC4_HDMI_CEC_CNT_TO_4300_US_SHIFT	24
# define VC4_HDMI_CEC_CNT_TO_3900_US_MASK	VC4_MASK(23, 16)
# define VC4_HDMI_CEC_CNT_TO_3900_US_SHIFT	16
# define VC4_HDMI_CEC_CNT_TO_3600_US_MASK	VC4_MASK(15, 8)
# define VC4_HDMI_CEC_CNT_TO_3600_US_SHIFT	8
# define VC4_HDMI_CEC_CNT_TO_3500_US_MASK	VC4_MASK(7, 0)
# define VC4_HDMI_CEC_CNT_TO_3500_US_SHIFT	0

#define VC4_HDMI_CEC_CNTRL_5			0x0f8
# define VC4_HDMI_CEC_TX_SW_RESET		BIT(27)
# define VC4_HDMI_CEC_RX_SW_RESET		BIT(26)
# define VC4_HDMI_CEC_PAD_SW_RESET		BIT(25)
# define VC4_HDMI_CEC_MUX_TP_OUT_CEC		BIT(24)
# define VC4_HDMI_CEC_RX_CEC_INT		BIT(23)
# define VC4_HDMI_CEC_CLK_PRELOAD_MASK		VC4_MASK(22, 16)
# define VC4_HDMI_CEC_CLK_PRELOAD_SHIFT		16
# define VC4_HDMI_CEC_CNT_TO_4700_US_MASK	VC4_MASK(15, 8)
# define VC4_HDMI_CEC_CNT_TO_4700_US_SHIFT	8
# define VC4_HDMI_CEC_CNT_TO_4500_US_MASK	VC4_MASK(7, 0)
# define VC4_HDMI_CEC_CNT_TO_4500_US_SHIFT	0

/* Transmit data, first byte is low byte of the 32-bit reg.  MSB of
 * each byte transmitted first.
 */
#define VC4_HDMI_CEC_TX_DATA_1			0x0fc
#define VC4_HDMI_CEC_TX_DATA_2			0x100
#define VC4_HDMI_CEC_TX_DATA_3			0x104
#define VC4_HDMI_CEC_TX_DATA_4			0x108
#define VC4_HDMI_CEC_RX_DATA_1			0x10c
#define VC4_HDMI_CEC_RX_DATA_2			0x110
#define VC4_HDMI_CEC_RX_DATA_3			0x114
#define VC4_HDMI_CEC_RX_DATA_4			0x118

#define VC4_HDMI_TX_PHY_RESET_CTL		0x2c0

#define VC4_HDMI_TX_PHY_CTL0			0x2c4
# define VC4_HDMI_TX_PHY_RNG_PWRDN		BIT(25)

/* Interrupt status bits */
#define VC4_HDMI_CPU_STATUS			0x340
#define VC4_HDMI_CPU_SET			0x344
#define VC4_HDMI_CPU_CLEAR			0x348
# define VC4_HDMI_CPU_CEC			BIT(6)
# define VC4_HDMI_CPU_HOTPLUG			BIT(0)

#define VC4_HDMI_CPU_MASK_STATUS		0x34c
#define VC4_HDMI_CPU_MASK_SET			0x350
#define VC4_HDMI_CPU_MASK_CLEAR			0x354

#define VC4_HDMI_GCP(x)				(0x400 + ((x) * 0x4))
#define VC4_HDMI_RAM_PACKET(x)			(0x400 + ((x) * 0x24))
#define VC4_HDMI_PACKET_STRIDE			0x24

#define VC4_HD_M_CTL				0x00c
/* Debug: Current receive value on the CEC pad. */
# define VC4_HD_CECRXD				BIT(9)
/* Debug: Override CEC output to 0. */
# define VC4_HD_CECOVR				BIT(8)
# define VC4_HD_M_REGISTER_FILE_STANDBY		(3 << 6)
# define VC4_HD_M_RAM_STANDBY			(3 << 4)
# define VC4_HD_M_SW_RST			BIT(2)
# define VC4_HD_M_ENABLE			BIT(0)

#define VC4_HD_MAI_CTL				0x014
/* Set when audio stream is received at a slower rate than the
 * sampling period, so MAI fifo goes empty.  Write 1 to clear.
 */
# define VC4_HD_MAI_CTL_DLATE			BIT(15)
# define VC4_HD_MAI_CTL_BUSY			BIT(14)
# define VC4_HD_MAI_CTL_CHALIGN			BIT(13)
# define VC4_HD_MAI_CTL_WHOLSMP			BIT(12)
# define VC4_HD_MAI_CTL_FULL			BIT(11)
# define VC4_HD_MAI_CTL_EMPTY			BIT(10)
# define VC4_HD_MAI_CTL_FLUSH			BIT(9)
/* If set, MAI bus generates SPDIF (bit 31) parity instead of passing
 * through.
 */
# define VC4_HD_MAI_CTL_PAREN			BIT(8)
# define VC4_HD_MAI_CTL_CHNUM_MASK		VC4_MASK(7, 4)
# define VC4_HD_MAI_CTL_CHNUM_SHIFT		4
# define VC4_HD_MAI_CTL_ENABLE			BIT(3)
/* Underflow error status bit, write 1 to clear. */
# define VC4_HD_MAI_CTL_ERRORE			BIT(2)
/* Overflow error status bit, write 1 to clear. */
# define VC4_HD_MAI_CTL_ERRORF			BIT(1)
/* Single-shot reset bit.  Read value is undefined. */
# define VC4_HD_MAI_CTL_RESET			BIT(0)

#define VC4_HD_MAI_THR				0x018
# define VC4_HD_MAI_THR_PANICHIGH_MASK		VC4_MASK(29, 24)
# define VC4_HD_MAI_THR_PANICHIGH_SHIFT		24
# define VC4_HD_MAI_THR_PANICLOW_MASK		VC4_MASK(21, 16)
# define VC4_HD_MAI_THR_PANICLOW_SHIFT		16
# define VC4_HD_MAI_THR_DREQHIGH_MASK		VC4_MASK(13, 8)
# define VC4_HD_MAI_THR_DREQHIGH_SHIFT		8
# define VC4_HD_MAI_THR_DREQLOW_MASK		VC4_MASK(5, 0)
# define VC4_HD_MAI_THR_DREQLOW_SHIFT		0

/* Format header to be placed on the MAI data. Unused. */
#define VC4_HD_MAI_FMT				0x01c

/* Register for DMAing in audio data to be transported over the MAI
 * bus to the Falcon core.
 */
#define VC4_HD_MAI_DATA				0x020

/* Divider from HDMI HSM clock to MAI serial clock.  Sampling period
 * converges to N / (M + 1) cycles.
 */
#define VC4_HD_MAI_SMP				0x02c
# define VC4_HD_MAI_SMP_N_MASK			VC4_MASK(31, 8)
# define VC4_HD_MAI_SMP_N_SHIFT			8
# define VC4_HD_MAI_SMP_M_MASK			VC4_MASK(7, 0)
# define VC4_HD_MAI_SMP_M_SHIFT			0

#define VC4_HD_VID_CTL				0x038
# define VC4_HD_VID_CTL_ENABLE			BIT(31)
# define VC4_HD_VID_CTL_UNDERFLOW_ENABLE	BIT(30)
# define VC4_HD_VID_CTL_FRAME_COUNTER_RESET	BIT(29)
# define VC4_HD_VID_CTL_VSYNC_LOW		BIT(28)
# define VC4_HD_VID_CTL_HSYNC_LOW		BIT(27)

#define VC4_HD_CSC_CTL				0x040
# define VC4_HD_CSC_CTL_ORDER_MASK		VC4_MASK(7, 5)
# define VC4_HD_CSC_CTL_ORDER_SHIFT		5
# define VC4_HD_CSC_CTL_ORDER_RGB		0
# define VC4_HD_CSC_CTL_ORDER_BGR		1
# define VC4_HD_CSC_CTL_ORDER_BRG		2
# define VC4_HD_CSC_CTL_ORDER_GRB		3
# define VC4_HD_CSC_CTL_ORDER_GBR		4
# define VC4_HD_CSC_CTL_ORDER_RBG		5
# define VC4_HD_CSC_CTL_PADMSB			BIT(4)
# define VC4_HD_CSC_CTL_MODE_MASK		VC4_MASK(3, 2)
# define VC4_HD_CSC_CTL_MODE_SHIFT		2
# define VC4_HD_CSC_CTL_MODE_RGB_TO_SD_YPRPB	0
# define VC4_HD_CSC_CTL_MODE_RGB_TO_HD_YPRPB	1
# define VC4_HD_CSC_CTL_MODE_CUSTOM		3
# define VC4_HD_CSC_CTL_RGB2YCC			BIT(1)
# define VC4_HD_CSC_CTL_ENABLE			BIT(0)

#define VC4_HD_CSC_12_11			0x044
#define VC4_HD_CSC_14_13			0x048
#define VC4_HD_CSC_22_21			0x04c
#define VC4_HD_CSC_24_23			0x050
#define VC4_HD_CSC_32_31			0x054
#define VC4_HD_CSC_34_33			0x058

#define VC4_HD_FRAME_COUNT			0x068

/* HVS display list information. */
#define HVS_BOOTLOADER_DLIST_END                32

enum hvs_pixel_format {
	/* 8bpp */
	HVS_PIXEL_FORMAT_RGB332 = 0,
	/* 16bpp */
	HVS_PIXEL_FORMAT_RGBA4444 = 1,
	HVS_PIXEL_FORMAT_RGB555 = 2,
	HVS_PIXEL_FORMAT_RGBA5551 = 3,
	HVS_PIXEL_FORMAT_RGB565 = 4,
	/* 24bpp */
	HVS_PIXEL_FORMAT_RGB888 = 5,
	HVS_PIXEL_FORMAT_RGBA6666 = 6,
	/* 32bpp */
	HVS_PIXEL_FORMAT_RGBA8888 = 7,

	HVS_PIXEL_FORMAT_YCBCR_YUV420_3PLANE = 8,
	HVS_PIXEL_FORMAT_YCBCR_YUV420_2PLANE = 9,
	HVS_PIXEL_FORMAT_YCBCR_YUV422_3PLANE = 10,
	HVS_PIXEL_FORMAT_YCBCR_YUV422_2PLANE = 11,
};

/* Note: the LSB is the rightmost character shown.  Only valid for
 * HVS_PIXEL_FORMAT_RGB8888, not RGB888.
 */
#define HVS_PIXEL_ORDER_RGBA			0
#define HVS_PIXEL_ORDER_BGRA			1
#define HVS_PIXEL_ORDER_ARGB			2
#define HVS_PIXEL_ORDER_ABGR			3

#define HVS_PIXEL_ORDER_XBRG			0
#define HVS_PIXEL_ORDER_XRBG			1
#define HVS_PIXEL_ORDER_XRGB			2
#define HVS_PIXEL_ORDER_XBGR			3

#define HVS_PIXEL_ORDER_XYCBCR			0
#define HVS_PIXEL_ORDER_XYCRCB			1
#define HVS_PIXEL_ORDER_YXCBCR			2
#define HVS_PIXEL_ORDER_YXCRCB			3

#define SCALER_CTL0_END				BIT(31)
#define SCALER_CTL0_VALID			BIT(30)

#define SCALER_CTL0_SIZE_MASK			VC4_MASK(29, 24)
#define SCALER_CTL0_SIZE_SHIFT			24

#define SCALER_CTL0_TILING_MASK			VC4_MASK(21, 20)
#define SCALER_CTL0_TILING_SHIFT		20
#define SCALER_CTL0_TILING_LINEAR		0
#define SCALER_CTL0_TILING_64B			1
#define SCALER_CTL0_TILING_128B			2
#define SCALER_CTL0_TILING_256B_OR_T		3

#define SCALER_CTL0_HFLIP                       BIT(16)
#define SCALER_CTL0_VFLIP                       BIT(15)

#define SCALER_CTL0_ORDER_MASK			VC4_MASK(14, 13)
#define SCALER_CTL0_ORDER_SHIFT			13

#define SCALER_CTL0_SCL1_MASK			VC4_MASK(10, 8)
#define SCALER_CTL0_SCL1_SHIFT			8

#define SCALER_CTL0_SCL0_MASK			VC4_MASK(7, 5)
#define SCALER_CTL0_SCL0_SHIFT			5

#define SCALER_CTL0_SCL_H_PPF_V_PPF		0
#define SCALER_CTL0_SCL_H_TPZ_V_PPF		1
#define SCALER_CTL0_SCL_H_PPF_V_TPZ		2
#define SCALER_CTL0_SCL_H_TPZ_V_TPZ		3
#define SCALER_CTL0_SCL_H_PPF_V_NONE		4
#define SCALER_CTL0_SCL_H_NONE_V_PPF		5
#define SCALER_CTL0_SCL_H_NONE_V_TPZ		6
#define SCALER_CTL0_SCL_H_TPZ_V_NONE		7

/* Set to indicate no scaling. */
#define SCALER_CTL0_UNITY			BIT(4)

#define SCALER_CTL0_PIXEL_FORMAT_MASK		VC4_MASK(3, 0)
#define SCALER_CTL0_PIXEL_FORMAT_SHIFT		0

#define SCALER_POS0_FIXED_ALPHA_MASK		VC4_MASK(31, 24)
#define SCALER_POS0_FIXED_ALPHA_SHIFT		24

#define SCALER_POS0_START_Y_MASK		VC4_MASK(23, 12)
#define SCALER_POS0_START_Y_SHIFT		12

#define SCALER_POS0_START_X_MASK		VC4_MASK(11, 0)
#define SCALER_POS0_START_X_SHIFT		0

#define SCALER_POS1_SCL_HEIGHT_MASK		VC4_MASK(27, 16)
#define SCALER_POS1_SCL_HEIGHT_SHIFT		16

#define SCALER_POS1_SCL_WIDTH_MASK		VC4_MASK(11, 0)
#define SCALER_POS1_SCL_WIDTH_SHIFT		0

#define SCALER_POS2_ALPHA_MODE_MASK		VC4_MASK(31, 30)
#define SCALER_POS2_ALPHA_MODE_SHIFT		30
#define SCALER_POS2_ALPHA_MODE_PIPELINE		0
#define SCALER_POS2_ALPHA_MODE_FIXED		1
#define SCALER_POS2_ALPHA_MODE_FIXED_NONZERO	2
#define SCALER_POS2_ALPHA_MODE_FIXED_OVER_0x07	3

#define SCALER_POS2_HEIGHT_MASK			VC4_MASK(27, 16)
#define SCALER_POS2_HEIGHT_SHIFT		16

#define SCALER_POS2_WIDTH_MASK			VC4_MASK(11, 0)
#define SCALER_POS2_WIDTH_SHIFT			0

/* Color Space Conversion words.  Some values are S2.8 signed
 * integers, except that the 2 integer bits map as {0x0: 0, 0x1: 1,
 * 0x2: 2, 0x3: -1}
 */
/* bottom 8 bits of S2.8 contribution of Cr to Blue */
#define SCALER_CSC0_COEF_CR_BLU_MASK		VC4_MASK(31, 24)
#define SCALER_CSC0_COEF_CR_BLU_SHIFT		24
/* Signed offset to apply to Y before CSC. (Y' = Y + YY_OFS) */
#define SCALER_CSC0_COEF_YY_OFS_MASK		VC4_MASK(23, 16)
#define SCALER_CSC0_COEF_YY_OFS_SHIFT		16
/* Signed offset to apply to CB before CSC (Cb' = Cb - 128 + CB_OFS). */
#define SCALER_CSC0_COEF_CB_OFS_MASK		VC4_MASK(15, 8)
#define SCALER_CSC0_COEF_CB_OFS_SHIFT		8
/* Signed offset to apply to CB before CSC (Cr' = Cr - 128 + CR_OFS). */
#define SCALER_CSC0_COEF_CR_OFS_MASK		VC4_MASK(7, 0)
#define SCALER_CSC0_COEF_CR_OFS_SHIFT		0
#define SCALER_CSC0_ITR_R_601_5			0x00f00000
#define SCALER_CSC0_ITR_R_709_3			0x00f00000
#define SCALER_CSC0_JPEG_JFIF			0x00000000

/* S2.8 contribution of Cb to Green */
#define SCALER_CSC1_COEF_CB_GRN_MASK		VC4_MASK(31, 22)
#define SCALER_CSC1_COEF_CB_GRN_SHIFT		22
/* S2.8 contribution of Cr to Green */
#define SCALER_CSC1_COEF_CR_GRN_MASK		VC4_MASK(21, 12)
#define SCALER_CSC1_COEF_CR_GRN_SHIFT		12
/* S2.8 contribution of Y to all of RGB */
#define SCALER_CSC1_COEF_YY_ALL_MASK		VC4_MASK(11, 2)
#define SCALER_CSC1_COEF_YY_ALL_SHIFT		2
/* top 2 bits of S2.8 contribution of Cr to Blue */
#define SCALER_CSC1_COEF_CR_BLU_MASK		VC4_MASK(1, 0)
#define SCALER_CSC1_COEF_CR_BLU_SHIFT		0
#define SCALER_CSC1_ITR_R_601_5			0xe73304a8
#define SCALER_CSC1_ITR_R_709_3			0xf2b784a8
#define SCALER_CSC1_JPEG_JFIF			0xea34a400

/* S2.8 contribution of Cb to Red */
#define SCALER_CSC2_COEF_CB_RED_MASK		VC4_MASK(29, 20)
#define SCALER_CSC2_COEF_CB_RED_SHIFT		20
/* S2.8 contribution of Cr to Red */
#define SCALER_CSC2_COEF_CR_RED_MASK		VC4_MASK(19, 10)
#define SCALER_CSC2_COEF_CR_RED_SHIFT		10
/* S2.8 contribution of Cb to Blue */
#define SCALER_CSC2_COEF_CB_BLU_MASK		VC4_MASK(19, 10)
#define SCALER_CSC2_COEF_CB_BLU_SHIFT		10
#define SCALER_CSC2_ITR_R_601_5			0x00066204
#define SCALER_CSC2_ITR_R_709_3			0x00072a1c
#define SCALER_CSC2_JPEG_JFIF			0x000599c5

#define SCALER_TPZ0_VERT_RECALC			BIT(31)
#define SCALER_TPZ0_SCALE_MASK			VC4_MASK(28, 8)
#define SCALER_TPZ0_SCALE_SHIFT			8
#define SCALER_TPZ0_IPHASE_MASK			VC4_MASK(7, 0)
#define SCALER_TPZ0_IPHASE_SHIFT		0
#define SCALER_TPZ1_RECIP_MASK			VC4_MASK(15, 0)
#define SCALER_TPZ1_RECIP_SHIFT			0

/* Skips interpolating coefficients to 64 phases, so just 8 are used.
 * Required for nearest neighbor.
 */
#define SCALER_PPF_NOINTERP			BIT(31)
/* Replaes the highest valued coefficient with one that makes all 4
 * sum to unity.
 */
#define SCALER_PPF_AGC				BIT(30)
#define SCALER_PPF_SCALE_MASK			VC4_MASK(24, 8)
#define SCALER_PPF_SCALE_SHIFT			8
#define SCALER_PPF_IPHASE_MASK			VC4_MASK(6, 0)
#define SCALER_PPF_IPHASE_SHIFT			0

#define SCALER_PPF_KERNEL_OFFSET_MASK		VC4_MASK(13, 0)
#define SCALER_PPF_KERNEL_OFFSET_SHIFT		0
#define SCALER_PPF_KERNEL_UNCACHED		BIT(31)

/* PITCH0/1/2 fields for raster. */
#define SCALER_SRC_PITCH_MASK			VC4_MASK(15, 0)
#define SCALER_SRC_PITCH_SHIFT			0

/* PITCH0 fields for T-tiled. */
#define SCALER_PITCH0_TILE_WIDTH_L_MASK		VC4_MASK(22, 16)
#define SCALER_PITCH0_TILE_WIDTH_L_SHIFT	16
#define SCALER_PITCH0_TILE_LINE_DIR		BIT(15)
#define SCALER_PITCH0_TILE_INITIAL_LINE_DIR	BIT(14)
/* Y offset within a tile. */
#define SCALER_PITCH0_TILE_Y_OFFSET_MASK	VC4_MASK(13, 7)
#define SCALER_PITCH0_TILE_Y_OFFSET_SHIFT	7
#define SCALER_PITCH0_TILE_WIDTH_R_MASK		VC4_MASK(6, 0)
#define SCALER_PITCH0_TILE_WIDTH_R_SHIFT	0

#endif /* VC4_REGS_H */

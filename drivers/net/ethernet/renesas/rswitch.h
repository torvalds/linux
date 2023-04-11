/* SPDX-License-Identifier: GPL-2.0 */
/* Renesas Ethernet Switch device driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#ifndef __RSWITCH_H__
#define __RSWITCH_H__

#include <linux/platform_device.h>
#include "rcar_gen4_ptp.h"

#define RSWITCH_MAX_NUM_QUEUES	128

#define RSWITCH_NUM_PORTS	3
#define rswitch_for_each_enabled_port(priv, i)		\
	for (i = 0; i < RSWITCH_NUM_PORTS; i++)		\
		if (priv->rdev[i]->disabled)		\
			continue;			\
		else

#define rswitch_for_each_enabled_port_continue_reverse(priv, i)	\
	for (i--; i >= 0; i--)					\
		if (priv->rdev[i]->disabled)			\
			continue;				\
		else

#define TX_RING_SIZE		1024
#define RX_RING_SIZE		1024
#define TS_RING_SIZE		(TX_RING_SIZE * RSWITCH_NUM_PORTS)

#define PKT_BUF_SZ		1584
#define RSWITCH_ALIGN		128
#define RSWITCH_MAX_CTAG_PCP	7

#define RSWITCH_TIMEOUT_US	100000

#define RSWITCH_TOP_OFFSET	0x00008000
#define RSWITCH_COMA_OFFSET	0x00009000
#define RSWITCH_ETHA_OFFSET	0x0000a000	/* with RMAC */
#define RSWITCH_ETHA_SIZE	0x00002000	/* with RMAC */
#define RSWITCH_GWCA0_OFFSET	0x00010000
#define RSWITCH_GWCA1_OFFSET	0x00012000

/* TODO: hardcoded ETHA/GWCA settings for now */
#define GWCA_IRQ_RESOURCE_NAME	"gwca0_rxtx%d"
#define GWCA_IRQ_NAME		"rswitch: gwca0_rxtx%d"
#define GWCA_NUM_IRQS		8
#define GWCA_INDEX		0
#define AGENT_INDEX_GWCA	3
#define GWRO			RSWITCH_GWCA0_OFFSET

#define GWCA_TS_IRQ_RESOURCE_NAME	"gwca0_rxts0"
#define GWCA_TS_IRQ_NAME		"rswitch: gwca0_rxts0"
#define GWCA_TS_IRQ_BIT			BIT(0)

#define FWRO	0
#define TPRO	RSWITCH_TOP_OFFSET
#define CARO	RSWITCH_COMA_OFFSET
#define TARO	0
#define RMRO	0x1000
enum rswitch_reg {
	FWGC		= FWRO + 0x0000,
	FWTTC0		= FWRO + 0x0010,
	FWTTC1		= FWRO + 0x0014,
	FWLBMC		= FWRO + 0x0018,
	FWCEPTC		= FWRO + 0x0020,
	FWCEPRC0	= FWRO + 0x0024,
	FWCEPRC1	= FWRO + 0x0028,
	FWCEPRC2	= FWRO + 0x002c,
	FWCLPTC		= FWRO + 0x0030,
	FWCLPRC		= FWRO + 0x0034,
	FWCMPTC		= FWRO + 0x0040,
	FWEMPTC		= FWRO + 0x0044,
	FWSDMPTC	= FWRO + 0x0050,
	FWSDMPVC	= FWRO + 0x0054,
	FWLBWMC0	= FWRO + 0x0080,
	FWPC00		= FWRO + 0x0100,
	FWPC10		= FWRO + 0x0104,
	FWPC20		= FWRO + 0x0108,
	FWCTGC00	= FWRO + 0x0400,
	FWCTGC10	= FWRO + 0x0404,
	FWCTTC00	= FWRO + 0x0408,
	FWCTTC10	= FWRO + 0x040c,
	FWCTTC200	= FWRO + 0x0410,
	FWCTSC00	= FWRO + 0x0420,
	FWCTSC10	= FWRO + 0x0424,
	FWCTSC20	= FWRO + 0x0428,
	FWCTSC30	= FWRO + 0x042c,
	FWCTSC40	= FWRO + 0x0430,
	FWTWBFC0	= FWRO + 0x1000,
	FWTWBFVC0	= FWRO + 0x1004,
	FWTHBFC0	= FWRO + 0x1400,
	FWTHBFV0C0	= FWRO + 0x1404,
	FWTHBFV1C0	= FWRO + 0x1408,
	FWFOBFC0	= FWRO + 0x1800,
	FWFOBFV0C0	= FWRO + 0x1804,
	FWFOBFV1C0	= FWRO + 0x1808,
	FWRFC0		= FWRO + 0x1c00,
	FWRFVC0		= FWRO + 0x1c04,
	FWCFC0		= FWRO + 0x2000,
	FWCFMC00	= FWRO + 0x2004,
	FWIP4SC		= FWRO + 0x4008,
	FWIP6SC		= FWRO + 0x4018,
	FWIP6OC		= FWRO + 0x401c,
	FWL2SC		= FWRO + 0x4020,
	FWSFHEC		= FWRO + 0x4030,
	FWSHCR0		= FWRO + 0x4040,
	FWSHCR1		= FWRO + 0x4044,
	FWSHCR2		= FWRO + 0x4048,
	FWSHCR3		= FWRO + 0x404c,
	FWSHCR4		= FWRO + 0x4050,
	FWSHCR5		= FWRO + 0x4054,
	FWSHCR6		= FWRO + 0x4058,
	FWSHCR7		= FWRO + 0x405c,
	FWSHCR8		= FWRO + 0x4060,
	FWSHCR9		= FWRO + 0x4064,
	FWSHCR10	= FWRO + 0x4068,
	FWSHCR11	= FWRO + 0x406c,
	FWSHCR12	= FWRO + 0x4070,
	FWSHCR13	= FWRO + 0x4074,
	FWSHCRR		= FWRO + 0x4078,
	FWLTHHEC	= FWRO + 0x4090,
	FWLTHHC		= FWRO + 0x4094,
	FWLTHTL0	= FWRO + 0x40a0,
	FWLTHTL1	= FWRO + 0x40a4,
	FWLTHTL2	= FWRO + 0x40a8,
	FWLTHTL3	= FWRO + 0x40ac,
	FWLTHTL4	= FWRO + 0x40b0,
	FWLTHTL5	= FWRO + 0x40b4,
	FWLTHTL6	= FWRO + 0x40b8,
	FWLTHTL7	= FWRO + 0x40bc,
	FWLTHTL80	= FWRO + 0x40c0,
	FWLTHTL9	= FWRO + 0x40d0,
	FWLTHTLR	= FWRO + 0x40d4,
	FWLTHTIM	= FWRO + 0x40e0,
	FWLTHTEM	= FWRO + 0x40e4,
	FWLTHTS0	= FWRO + 0x4100,
	FWLTHTS1	= FWRO + 0x4104,
	FWLTHTS2	= FWRO + 0x4108,
	FWLTHTS3	= FWRO + 0x410c,
	FWLTHTS4	= FWRO + 0x4110,
	FWLTHTSR0	= FWRO + 0x4120,
	FWLTHTSR1	= FWRO + 0x4124,
	FWLTHTSR2	= FWRO + 0x4128,
	FWLTHTSR3	= FWRO + 0x412c,
	FWLTHTSR40	= FWRO + 0x4130,
	FWLTHTSR5	= FWRO + 0x4140,
	FWLTHTR		= FWRO + 0x4150,
	FWLTHTRR0	= FWRO + 0x4154,
	FWLTHTRR1	= FWRO + 0x4158,
	FWLTHTRR2	= FWRO + 0x415c,
	FWLTHTRR3	= FWRO + 0x4160,
	FWLTHTRR4	= FWRO + 0x4164,
	FWLTHTRR5	= FWRO + 0x4168,
	FWLTHTRR6	= FWRO + 0x416c,
	FWLTHTRR7	= FWRO + 0x4170,
	FWLTHTRR8	= FWRO + 0x4174,
	FWLTHTRR9	= FWRO + 0x4180,
	FWLTHTRR10	= FWRO + 0x4190,
	FWIPHEC		= FWRO + 0x4214,
	FWIPHC		= FWRO + 0x4218,
	FWIPTL0		= FWRO + 0x4220,
	FWIPTL1		= FWRO + 0x4224,
	FWIPTL2		= FWRO + 0x4228,
	FWIPTL3		= FWRO + 0x422c,
	FWIPTL4		= FWRO + 0x4230,
	FWIPTL5		= FWRO + 0x4234,
	FWIPTL6		= FWRO + 0x4238,
	FWIPTL7		= FWRO + 0x4240,
	FWIPTL8		= FWRO + 0x4250,
	FWIPTLR		= FWRO + 0x4254,
	FWIPTIM		= FWRO + 0x4260,
	FWIPTEM		= FWRO + 0x4264,
	FWIPTS0		= FWRO + 0x4270,
	FWIPTS1		= FWRO + 0x4274,
	FWIPTS2		= FWRO + 0x4278,
	FWIPTS3		= FWRO + 0x427c,
	FWIPTS4		= FWRO + 0x4280,
	FWIPTSR0	= FWRO + 0x4284,
	FWIPTSR1	= FWRO + 0x4288,
	FWIPTSR2	= FWRO + 0x428c,
	FWIPTSR3	= FWRO + 0x4290,
	FWIPTSR4	= FWRO + 0x42a0,
	FWIPTR		= FWRO + 0x42b0,
	FWIPTRR0	= FWRO + 0x42b4,
	FWIPTRR1	= FWRO + 0x42b8,
	FWIPTRR2	= FWRO + 0x42bc,
	FWIPTRR3	= FWRO + 0x42c0,
	FWIPTRR4	= FWRO + 0x42c4,
	FWIPTRR5	= FWRO + 0x42c8,
	FWIPTRR6	= FWRO + 0x42cc,
	FWIPTRR7	= FWRO + 0x42d0,
	FWIPTRR8	= FWRO + 0x42e0,
	FWIPTRR9	= FWRO + 0x42f0,
	FWIPHLEC	= FWRO + 0x4300,
	FWIPAGUSPC	= FWRO + 0x4500,
	FWIPAGC		= FWRO + 0x4504,
	FWIPAGM0	= FWRO + 0x4510,
	FWIPAGM1	= FWRO + 0x4514,
	FWIPAGM2	= FWRO + 0x4518,
	FWIPAGM3	= FWRO + 0x451c,
	FWIPAGM4	= FWRO + 0x4520,
	FWMACHEC	= FWRO + 0x4620,
	FWMACHC		= FWRO + 0x4624,
	FWMACTL0	= FWRO + 0x4630,
	FWMACTL1	= FWRO + 0x4634,
	FWMACTL2	= FWRO + 0x4638,
	FWMACTL3	= FWRO + 0x463c,
	FWMACTL4	= FWRO + 0x4640,
	FWMACTL5	= FWRO + 0x4650,
	FWMACTLR	= FWRO + 0x4654,
	FWMACTIM	= FWRO + 0x4660,
	FWMACTEM	= FWRO + 0x4664,
	FWMACTS0	= FWRO + 0x4670,
	FWMACTS1	= FWRO + 0x4674,
	FWMACTSR0	= FWRO + 0x4678,
	FWMACTSR1	= FWRO + 0x467c,
	FWMACTSR2	= FWRO + 0x4680,
	FWMACTSR3	= FWRO + 0x4690,
	FWMACTR		= FWRO + 0x46a0,
	FWMACTRR0	= FWRO + 0x46a4,
	FWMACTRR1	= FWRO + 0x46a8,
	FWMACTRR2	= FWRO + 0x46ac,
	FWMACTRR3	= FWRO + 0x46b0,
	FWMACTRR4	= FWRO + 0x46b4,
	FWMACTRR5	= FWRO + 0x46c0,
	FWMACTRR6	= FWRO + 0x46d0,
	FWMACHLEC	= FWRO + 0x4700,
	FWMACAGUSPC	= FWRO + 0x4880,
	FWMACAGC	= FWRO + 0x4884,
	FWMACAGM0	= FWRO + 0x4888,
	FWMACAGM1	= FWRO + 0x488c,
	FWVLANTEC	= FWRO + 0x4900,
	FWVLANTL0	= FWRO + 0x4910,
	FWVLANTL1	= FWRO + 0x4914,
	FWVLANTL2	= FWRO + 0x4918,
	FWVLANTL3	= FWRO + 0x4920,
	FWVLANTL4	= FWRO + 0x4930,
	FWVLANTLR	= FWRO + 0x4934,
	FWVLANTIM	= FWRO + 0x4940,
	FWVLANTEM	= FWRO + 0x4944,
	FWVLANTS	= FWRO + 0x4950,
	FWVLANTSR0	= FWRO + 0x4954,
	FWVLANTSR1	= FWRO + 0x4958,
	FWVLANTSR2	= FWRO + 0x4960,
	FWVLANTSR3	= FWRO + 0x4970,
	FWPBFC0		= FWRO + 0x4a00,
	FWPBFCSDC00	= FWRO + 0x4a04,
	FWL23URL0	= FWRO + 0x4e00,
	FWL23URL1	= FWRO + 0x4e04,
	FWL23URL2	= FWRO + 0x4e08,
	FWL23URL3	= FWRO + 0x4e0c,
	FWL23URLR	= FWRO + 0x4e10,
	FWL23UTIM	= FWRO + 0x4e20,
	FWL23URR	= FWRO + 0x4e30,
	FWL23URRR0	= FWRO + 0x4e34,
	FWL23URRR1	= FWRO + 0x4e38,
	FWL23URRR2	= FWRO + 0x4e3c,
	FWL23URRR3	= FWRO + 0x4e40,
	FWL23URMC0	= FWRO + 0x4f00,
	FWPMFGC0	= FWRO + 0x5000,
	FWPGFC0		= FWRO + 0x5100,
	FWPGFIGSC0	= FWRO + 0x5104,
	FWPGFENC0	= FWRO + 0x5108,
	FWPGFENM0	= FWRO + 0x510c,
	FWPGFCSTC00	= FWRO + 0x5110,
	FWPGFCSTC10	= FWRO + 0x5114,
	FWPGFCSTM00	= FWRO + 0x5118,
	FWPGFCSTM10	= FWRO + 0x511c,
	FWPGFCTC0	= FWRO + 0x5120,
	FWPGFCTM0	= FWRO + 0x5124,
	FWPGFHCC0	= FWRO + 0x5128,
	FWPGFSM0	= FWRO + 0x512c,
	FWPGFGC0	= FWRO + 0x5130,
	FWPGFGL0	= FWRO + 0x5500,
	FWPGFGL1	= FWRO + 0x5504,
	FWPGFGLR	= FWRO + 0x5518,
	FWPGFGR		= FWRO + 0x5510,
	FWPGFGRR0	= FWRO + 0x5514,
	FWPGFGRR1	= FWRO + 0x5518,
	FWPGFRIM	= FWRO + 0x5520,
	FWPMTRFC0	= FWRO + 0x5600,
	FWPMTRCBSC0	= FWRO + 0x5604,
	FWPMTRC0RC0	= FWRO + 0x5608,
	FWPMTREBSC0	= FWRO + 0x560c,
	FWPMTREIRC0	= FWRO + 0x5610,
	FWPMTRFM0	= FWRO + 0x5614,
	FWFTL0		= FWRO + 0x6000,
	FWFTL1		= FWRO + 0x6004,
	FWFTLR		= FWRO + 0x6008,
	FWFTOC		= FWRO + 0x6010,
	FWFTOPC		= FWRO + 0x6014,
	FWFTIM		= FWRO + 0x6020,
	FWFTR		= FWRO + 0x6030,
	FWFTRR0		= FWRO + 0x6034,
	FWFTRR1		= FWRO + 0x6038,
	FWFTRR2		= FWRO + 0x603c,
	FWSEQNGC0	= FWRO + 0x6100,
	FWSEQNGM0	= FWRO + 0x6104,
	FWSEQNRC	= FWRO + 0x6200,
	FWCTFDCN0	= FWRO + 0x6300,
	FWLTHFDCN0	= FWRO + 0x6304,
	FWIPFDCN0	= FWRO + 0x6308,
	FWLTWFDCN0	= FWRO + 0x630c,
	FWPBFDCN0	= FWRO + 0x6310,
	FWMHLCN0	= FWRO + 0x6314,
	FWIHLCN0	= FWRO + 0x6318,
	FWICRDCN0	= FWRO + 0x6500,
	FWWMRDCN0	= FWRO + 0x6504,
	FWCTRDCN0	= FWRO + 0x6508,
	FWLTHRDCN0	= FWRO + 0x650c,
	FWIPRDCN0	= FWRO + 0x6510,
	FWLTWRDCN0	= FWRO + 0x6514,
	FWPBRDCN0	= FWRO + 0x6518,
	FWPMFDCN0	= FWRO + 0x6700,
	FWPGFDCN0	= FWRO + 0x6780,
	FWPMGDCN0	= FWRO + 0x6800,
	FWPMYDCN0	= FWRO + 0x6804,
	FWPMRDCN0	= FWRO + 0x6808,
	FWFRPPCN0	= FWRO + 0x6a00,
	FWFRDPCN0	= FWRO + 0x6a04,
	FWEIS00		= FWRO + 0x7900,
	FWEIE00		= FWRO + 0x7904,
	FWEID00		= FWRO + 0x7908,
	FWEIS1		= FWRO + 0x7a00,
	FWEIE1		= FWRO + 0x7a04,
	FWEID1		= FWRO + 0x7a08,
	FWEIS2		= FWRO + 0x7a10,
	FWEIE2		= FWRO + 0x7a14,
	FWEID2		= FWRO + 0x7a18,
	FWEIS3		= FWRO + 0x7a20,
	FWEIE3		= FWRO + 0x7a24,
	FWEID3		= FWRO + 0x7a28,
	FWEIS4		= FWRO + 0x7a30,
	FWEIE4		= FWRO + 0x7a34,
	FWEID4		= FWRO + 0x7a38,
	FWEIS5		= FWRO + 0x7a40,
	FWEIE5		= FWRO + 0x7a44,
	FWEID5		= FWRO + 0x7a48,
	FWEIS60		= FWRO + 0x7a50,
	FWEIE60		= FWRO + 0x7a54,
	FWEID60		= FWRO + 0x7a58,
	FWEIS61		= FWRO + 0x7a60,
	FWEIE61		= FWRO + 0x7a64,
	FWEID61		= FWRO + 0x7a68,
	FWEIS62		= FWRO + 0x7a70,
	FWEIE62		= FWRO + 0x7a74,
	FWEID62		= FWRO + 0x7a78,
	FWEIS63		= FWRO + 0x7a80,
	FWEIE63		= FWRO + 0x7a84,
	FWEID63		= FWRO + 0x7a88,
	FWEIS70		= FWRO + 0x7a90,
	FWEIE70		= FWRO + 0x7A94,
	FWEID70		= FWRO + 0x7a98,
	FWEIS71		= FWRO + 0x7aa0,
	FWEIE71		= FWRO + 0x7aa4,
	FWEID71		= FWRO + 0x7aa8,
	FWEIS72		= FWRO + 0x7ab0,
	FWEIE72		= FWRO + 0x7ab4,
	FWEID72		= FWRO + 0x7ab8,
	FWEIS73		= FWRO + 0x7ac0,
	FWEIE73		= FWRO + 0x7ac4,
	FWEID73		= FWRO + 0x7ac8,
	FWEIS80		= FWRO + 0x7ad0,
	FWEIE80		= FWRO + 0x7ad4,
	FWEID80		= FWRO + 0x7ad8,
	FWEIS81		= FWRO + 0x7ae0,
	FWEIE81		= FWRO + 0x7ae4,
	FWEID81		= FWRO + 0x7ae8,
	FWEIS82		= FWRO + 0x7af0,
	FWEIE82		= FWRO + 0x7af4,
	FWEID82		= FWRO + 0x7af8,
	FWEIS83		= FWRO + 0x7b00,
	FWEIE83		= FWRO + 0x7b04,
	FWEID83		= FWRO + 0x7b08,
	FWMIS0		= FWRO + 0x7c00,
	FWMIE0		= FWRO + 0x7c04,
	FWMID0		= FWRO + 0x7c08,
	FWSCR0		= FWRO + 0x7d00,
	FWSCR1		= FWRO + 0x7d04,
	FWSCR2		= FWRO + 0x7d08,
	FWSCR3		= FWRO + 0x7d0c,
	FWSCR4		= FWRO + 0x7d10,
	FWSCR5		= FWRO + 0x7d14,
	FWSCR6		= FWRO + 0x7d18,
	FWSCR7		= FWRO + 0x7d1c,
	FWSCR8		= FWRO + 0x7d20,
	FWSCR9		= FWRO + 0x7d24,
	FWSCR10		= FWRO + 0x7d28,
	FWSCR11		= FWRO + 0x7d2c,
	FWSCR12		= FWRO + 0x7d30,
	FWSCR13		= FWRO + 0x7d34,
	FWSCR14		= FWRO + 0x7d38,
	FWSCR15		= FWRO + 0x7d3c,
	FWSCR16		= FWRO + 0x7d40,
	FWSCR17		= FWRO + 0x7d44,
	FWSCR18		= FWRO + 0x7d48,
	FWSCR19		= FWRO + 0x7d4c,
	FWSCR20		= FWRO + 0x7d50,
	FWSCR21		= FWRO + 0x7d54,
	FWSCR22		= FWRO + 0x7d58,
	FWSCR23		= FWRO + 0x7d5c,
	FWSCR24		= FWRO + 0x7d60,
	FWSCR25		= FWRO + 0x7d64,
	FWSCR26		= FWRO + 0x7d68,
	FWSCR27		= FWRO + 0x7d6c,
	FWSCR28		= FWRO + 0x7d70,
	FWSCR29		= FWRO + 0x7d74,
	FWSCR30		= FWRO + 0x7d78,
	FWSCR31		= FWRO + 0x7d7c,
	FWSCR32		= FWRO + 0x7d80,
	FWSCR33		= FWRO + 0x7d84,
	FWSCR34		= FWRO + 0x7d88,
	FWSCR35		= FWRO + 0x7d8c,
	FWSCR36		= FWRO + 0x7d90,
	FWSCR37		= FWRO + 0x7d94,
	FWSCR38		= FWRO + 0x7d98,
	FWSCR39		= FWRO + 0x7d9c,
	FWSCR40		= FWRO + 0x7da0,
	FWSCR41		= FWRO + 0x7da4,
	FWSCR42		= FWRO + 0x7da8,
	FWSCR43		= FWRO + 0x7dac,
	FWSCR44		= FWRO + 0x7db0,
	FWSCR45		= FWRO + 0x7db4,
	FWSCR46		= FWRO + 0x7db8,

	TPEMIMC0	= TPRO + 0x0000,
	TPEMIMC1	= TPRO + 0x0004,
	TPEMIMC2	= TPRO + 0x0008,
	TPEMIMC3	= TPRO + 0x000c,
	TPEMIMC4	= TPRO + 0x0010,
	TPEMIMC5	= TPRO + 0x0014,
	TPEMIMC60	= TPRO + 0x0080,
	TPEMIMC70	= TPRO + 0x0100,
	TSIM		= TPRO + 0x0700,
	TFIM		= TPRO + 0x0704,
	TCIM		= TPRO + 0x0708,
	TGIM0		= TPRO + 0x0710,
	TGIM1		= TPRO + 0x0714,
	TEIM0		= TPRO + 0x0720,
	TEIM1		= TPRO + 0x0724,
	TEIM2		= TPRO + 0x0728,

	RIPV		= CARO + 0x0000,
	RRC		= CARO + 0x0004,
	RCEC		= CARO + 0x0008,
	RCDC		= CARO + 0x000c,
	RSSIS		= CARO + 0x0010,
	RSSIE		= CARO + 0x0014,
	RSSID		= CARO + 0x0018,
	CABPIBWMC	= CARO + 0x0020,
	CABPWMLC	= CARO + 0x0040,
	CABPPFLC0	= CARO + 0x0050,
	CABPPWMLC0	= CARO + 0x0060,
	CABPPPFLC00	= CARO + 0x00a0,
	CABPULC		= CARO + 0x0100,
	CABPIRM		= CARO + 0x0140,
	CABPPCM		= CARO + 0x0144,
	CABPLCM		= CARO + 0x0148,
	CABPCPM		= CARO + 0x0180,
	CABPMCPM	= CARO + 0x0200,
	CARDNM		= CARO + 0x0280,
	CARDMNM		= CARO + 0x0284,
	CARDCN		= CARO + 0x0290,
	CAEIS0		= CARO + 0x0300,
	CAEIE0		= CARO + 0x0304,
	CAEID0		= CARO + 0x0308,
	CAEIS1		= CARO + 0x0310,
	CAEIE1		= CARO + 0x0314,
	CAEID1		= CARO + 0x0318,
	CAMIS0		= CARO + 0x0340,
	CAMIE0		= CARO + 0x0344,
	CAMID0		= CARO + 0x0348,
	CAMIS1		= CARO + 0x0350,
	CAMIE1		= CARO + 0x0354,
	CAMID1		= CARO + 0x0358,
	CASCR		= CARO + 0x0380,

	EAMC		= TARO + 0x0000,
	EAMS		= TARO + 0x0004,
	EAIRC		= TARO + 0x0010,
	EATDQSC		= TARO + 0x0014,
	EATDQC		= TARO + 0x0018,
	EATDQAC		= TARO + 0x001c,
	EATPEC		= TARO + 0x0020,
	EATMFSC0	= TARO + 0x0040,
	EATDQDC0	= TARO + 0x0060,
	EATDQM0		= TARO + 0x0080,
	EATDQMLM0	= TARO + 0x00a0,
	EACTQC		= TARO + 0x0100,
	EACTDQDC	= TARO + 0x0104,
	EACTDQM		= TARO + 0x0108,
	EACTDQMLM	= TARO + 0x010c,
	EAVCC		= TARO + 0x0130,
	EAVTC		= TARO + 0x0134,
	EATTFC		= TARO + 0x0138,
	EACAEC		= TARO + 0x0200,
	EACC		= TARO + 0x0204,
	EACAIVC0	= TARO + 0x0220,
	EACAULC0	= TARO + 0x0240,
	EACOEM		= TARO + 0x0260,
	EACOIVM0	= TARO + 0x0280,
	EACOULM0	= TARO + 0x02a0,
	EACGSM		= TARO + 0x02c0,
	EATASC		= TARO + 0x0300,
	EATASENC0	= TARO + 0x0320,
	EATASCTENC	= TARO + 0x0340,
	EATASENM0	= TARO + 0x0360,
	EATASCTENM	= TARO + 0x0380,
	EATASCSTC0	= TARO + 0x03a0,
	EATASCSTC1	= TARO + 0x03a4,
	EATASCSTM0	= TARO + 0x03a8,
	EATASCSTM1	= TARO + 0x03ac,
	EATASCTC	= TARO + 0x03b0,
	EATASCTM	= TARO + 0x03b4,
	EATASGL0	= TARO + 0x03c0,
	EATASGL1	= TARO + 0x03c4,
	EATASGLR	= TARO + 0x03c8,
	EATASGR		= TARO + 0x03d0,
	EATASGRR	= TARO + 0x03d4,
	EATASHCC	= TARO + 0x03e0,
	EATASRIRM	= TARO + 0x03e4,
	EATASSM		= TARO + 0x03e8,
	EAUSMFSECN	= TARO + 0x0400,
	EATFECN		= TARO + 0x0404,
	EAFSECN		= TARO + 0x0408,
	EADQOECN	= TARO + 0x040c,
	EADQSECN	= TARO + 0x0410,
	EACKSECN	= TARO + 0x0414,
	EAEIS0		= TARO + 0x0500,
	EAEIE0		= TARO + 0x0504,
	EAEID0		= TARO + 0x0508,
	EAEIS1		= TARO + 0x0510,
	EAEIE1		= TARO + 0x0514,
	EAEID1		= TARO + 0x0518,
	EAEIS2		= TARO + 0x0520,
	EAEIE2		= TARO + 0x0524,
	EAEID2		= TARO + 0x0528,
	EASCR		= TARO + 0x0580,

	MPSM		= RMRO + 0x0000,
	MPIC		= RMRO + 0x0004,
	MPIM		= RMRO + 0x0008,
	MIOC		= RMRO + 0x0010,
	MIOM		= RMRO + 0x0014,
	MXMS		= RMRO + 0x0018,
	MTFFC		= RMRO + 0x0020,
	MTPFC		= RMRO + 0x0024,
	MTPFC2		= RMRO + 0x0028,
	MTPFC30		= RMRO + 0x0030,
	MTATC0		= RMRO + 0x0050,
	MTIM		= RMRO + 0x0060,
	MRGC		= RMRO + 0x0080,
	MRMAC0		= RMRO + 0x0084,
	MRMAC1		= RMRO + 0x0088,
	MRAFC		= RMRO + 0x008c,
	MRSCE		= RMRO + 0x0090,
	MRSCP		= RMRO + 0x0094,
	MRSCC		= RMRO + 0x0098,
	MRFSCE		= RMRO + 0x009c,
	MRFSCP		= RMRO + 0x00a0,
	MTRC		= RMRO + 0x00a4,
	MRIM		= RMRO + 0x00a8,
	MRPFM		= RMRO + 0x00ac,
	MPFC0		= RMRO + 0x0100,
	MLVC		= RMRO + 0x0180,
	MEEEC		= RMRO + 0x0184,
	MLBC		= RMRO + 0x0188,
	MXGMIIC		= RMRO + 0x0190,
	MPCH		= RMRO + 0x0194,
	MANC		= RMRO + 0x0198,
	MANM		= RMRO + 0x019c,
	MPLCA1		= RMRO + 0x01a0,
	MPLCA2		= RMRO + 0x01a4,
	MPLCA3		= RMRO + 0x01a8,
	MPLCA4		= RMRO + 0x01ac,
	MPLCAM		= RMRO + 0x01b0,
	MHDC1		= RMRO + 0x01c0,
	MHDC2		= RMRO + 0x01c4,
	MEIS		= RMRO + 0x0200,
	MEIE		= RMRO + 0x0204,
	MEID		= RMRO + 0x0208,
	MMIS0		= RMRO + 0x0210,
	MMIE0		= RMRO + 0x0214,
	MMID0		= RMRO + 0x0218,
	MMIS1		= RMRO + 0x0220,
	MMIE1		= RMRO + 0x0224,
	MMID1		= RMRO + 0x0228,
	MMIS2		= RMRO + 0x0230,
	MMIE2		= RMRO + 0x0234,
	MMID2		= RMRO + 0x0238,
	MMPFTCT		= RMRO + 0x0300,
	MAPFTCT		= RMRO + 0x0304,
	MPFRCT		= RMRO + 0x0308,
	MFCICT		= RMRO + 0x030c,
	MEEECT		= RMRO + 0x0310,
	MMPCFTCT0	= RMRO + 0x0320,
	MAPCFTCT0	= RMRO + 0x0330,
	MPCFRCT0	= RMRO + 0x0340,
	MHDCC		= RMRO + 0x0350,
	MROVFC		= RMRO + 0x0354,
	MRHCRCEC	= RMRO + 0x0358,
	MRXBCE		= RMRO + 0x0400,
	MRXBCP		= RMRO + 0x0404,
	MRGFCE		= RMRO + 0x0408,
	MRGFCP		= RMRO + 0x040c,
	MRBFC		= RMRO + 0x0410,
	MRMFC		= RMRO + 0x0414,
	MRUFC		= RMRO + 0x0418,
	MRPEFC		= RMRO + 0x041c,
	MRNEFC		= RMRO + 0x0420,
	MRFMEFC		= RMRO + 0x0424,
	MRFFMEFC	= RMRO + 0x0428,
	MRCFCEFC	= RMRO + 0x042c,
	MRFCEFC		= RMRO + 0x0430,
	MRRCFEFC	= RMRO + 0x0434,
	MRUEFC		= RMRO + 0x043c,
	MROEFC		= RMRO + 0x0440,
	MRBOEC		= RMRO + 0x0444,
	MTXBCE		= RMRO + 0x0500,
	MTXBCP		= RMRO + 0x0504,
	MTGFCE		= RMRO + 0x0508,
	MTGFCP		= RMRO + 0x050c,
	MTBFC		= RMRO + 0x0510,
	MTMFC		= RMRO + 0x0514,
	MTUFC		= RMRO + 0x0518,
	MTEFC		= RMRO + 0x051c,

	GWMC		= GWRO + 0x0000,
	GWMS		= GWRO + 0x0004,
	GWIRC		= GWRO + 0x0010,
	GWRDQSC		= GWRO + 0x0014,
	GWRDQC		= GWRO + 0x0018,
	GWRDQAC		= GWRO + 0x001c,
	GWRGC		= GWRO + 0x0020,
	GWRMFSC0	= GWRO + 0x0040,
	GWRDQDC0	= GWRO + 0x0060,
	GWRDQM0		= GWRO + 0x0080,
	GWRDQMLM0	= GWRO + 0x00a0,
	GWMTIRM		= GWRO + 0x0100,
	GWMSTLS		= GWRO + 0x0104,
	GWMSTLR		= GWRO + 0x0108,
	GWMSTSS		= GWRO + 0x010c,
	GWMSTSR		= GWRO + 0x0110,
	GWMAC0		= GWRO + 0x0120,
	GWMAC1		= GWRO + 0x0124,
	GWVCC		= GWRO + 0x0130,
	GWVTC		= GWRO + 0x0134,
	GWTTFC		= GWRO + 0x0138,
	GWTDCAC00	= GWRO + 0x0140,
	GWTDCAC10	= GWRO + 0x0144,
	GWTSDCC0	= GWRO + 0x0160,
	GWTNM		= GWRO + 0x0180,
	GWTMNM		= GWRO + 0x0184,
	GWAC		= GWRO + 0x0190,
	GWDCBAC0	= GWRO + 0x0194,
	GWDCBAC1	= GWRO + 0x0198,
	GWIICBSC	= GWRO + 0x019c,
	GWMDNC		= GWRO + 0x01a0,
	GWTRC0		= GWRO + 0x0200,
	GWTPC0		= GWRO + 0x0300,
	GWARIRM		= GWRO + 0x0380,
	GWDCC0		= GWRO + 0x0400,
	GWAARSS		= GWRO + 0x0800,
	GWAARSR0	= GWRO + 0x0804,
	GWAARSR1	= GWRO + 0x0808,
	GWIDAUAS0	= GWRO + 0x0840,
	GWIDASM0	= GWRO + 0x0880,
	GWIDASAM00	= GWRO + 0x0900,
	GWIDASAM10	= GWRO + 0x0904,
	GWIDACAM00	= GWRO + 0x0980,
	GWIDACAM10	= GWRO + 0x0984,
	GWGRLC		= GWRO + 0x0a00,
	GWGRLULC	= GWRO + 0x0a04,
	GWRLIVC0	= GWRO + 0x0a80,
	GWRLULC0	= GWRO + 0x0a84,
	GWIDPC		= GWRO + 0x0b00,
	GWIDC0		= GWRO + 0x0c00,
	GWDIS0		= GWRO + 0x1100,
	GWDIE0		= GWRO + 0x1104,
	GWDID0		= GWRO + 0x1108,
	GWTSDIS		= GWRO + 0x1180,
	GWTSDIE		= GWRO + 0x1184,
	GWTSDID		= GWRO + 0x1188,
	GWEIS0		= GWRO + 0x1190,
	GWEIE0		= GWRO + 0x1194,
	GWEID0		= GWRO + 0x1198,
	GWEIS1		= GWRO + 0x11a0,
	GWEIE1		= GWRO + 0x11a4,
	GWEID1		= GWRO + 0x11a8,
	GWEIS20		= GWRO + 0x1200,
	GWEIE20		= GWRO + 0x1204,
	GWEID20		= GWRO + 0x1208,
	GWEIS3		= GWRO + 0x1280,
	GWEIE3		= GWRO + 0x1284,
	GWEID3		= GWRO + 0x1288,
	GWEIS4		= GWRO + 0x1290,
	GWEIE4		= GWRO + 0x1294,
	GWEID4		= GWRO + 0x1298,
	GWEIS5		= GWRO + 0x12a0,
	GWEIE5		= GWRO + 0x12a4,
	GWEID5		= GWRO + 0x12a8,
	GWSCR0		= GWRO + 0x1800,
	GWSCR1		= GWRO + 0x1900,
};

/* ETHA/RMAC */
enum rswitch_etha_mode {
	EAMC_OPC_RESET,
	EAMC_OPC_DISABLE,
	EAMC_OPC_CONFIG,
	EAMC_OPC_OPERATION,
};

#define EAMS_OPS_MASK		EAMC_OPC_OPERATION

#define EAVCC_VEM_SC_TAG	(0x3 << 16)

#define MPIC_PIS_MII		0x00
#define MPIC_PIS_GMII		0x02
#define MPIC_PIS_XGMII		0x04
#define MPIC_LSC_SHIFT		3
#define MPIC_LSC_100M		(1 << MPIC_LSC_SHIFT)
#define MPIC_LSC_1G		(2 << MPIC_LSC_SHIFT)
#define MPIC_LSC_2_5G		(3 << MPIC_LSC_SHIFT)

#define MDIO_READ_C45		0x03
#define MDIO_WRITE_C45		0x01

#define MPSM_PSME		BIT(0)
#define MPSM_MFF_C45		BIT(2)
#define MPSM_PRD_SHIFT		16
#define MPSM_PRD_MASK		GENMASK(31, MPSM_PRD_SHIFT)

/* Completion flags */
#define MMIS1_PAACS             BIT(2) /* Address */
#define MMIS1_PWACS             BIT(1) /* Write */
#define MMIS1_PRACS             BIT(0) /* Read */
#define MMIS1_CLEAR_FLAGS       0xf

#define MPIC_PSMCS_SHIFT	16
#define MPIC_PSMCS_MASK		GENMASK(22, MPIC_PSMCS_SHIFT)
#define MPIC_PSMCS(val)		((val) << MPIC_PSMCS_SHIFT)

#define MPIC_PSMHT_SHIFT	24
#define MPIC_PSMHT_MASK		GENMASK(26, MPIC_PSMHT_SHIFT)
#define MPIC_PSMHT(val)		((val) << MPIC_PSMHT_SHIFT)

#define MLVC_PLV		BIT(16)

/* GWCA */
enum rswitch_gwca_mode {
	GWMC_OPC_RESET,
	GWMC_OPC_DISABLE,
	GWMC_OPC_CONFIG,
	GWMC_OPC_OPERATION,
};

#define GWMS_OPS_MASK		GWMC_OPC_OPERATION

#define GWMTIRM_MTIOG		BIT(0)
#define GWMTIRM_MTR		BIT(1)

#define GWVCC_VEM_SC_TAG	(0x3 << 16)

#define GWARIRM_ARIOG		BIT(0)
#define GWARIRM_ARR		BIT(1)

#define GWDCC_BALR		BIT(24)
#define GWDCC_DQT		BIT(11)
#define GWDCC_ETS		BIT(9)
#define GWDCC_EDE		BIT(8)

#define GWTRC(queue)		(GWTRC0 + (queue) / 32 * 4)
#define GWDCC_OFFS(queue)	(GWDCC0 + (queue) * 4)

#define GWDIS(i)		(GWDIS0 + (i) * 0x10)
#define GWDIE(i)		(GWDIE0 + (i) * 0x10)
#define GWDID(i)		(GWDID0 + (i) * 0x10)

/* COMA */
#define RRC_RR			BIT(0)
#define RRC_RR_CLR		0
#define	RCEC_ACE_DEFAULT	(BIT(0) | BIT(AGENT_INDEX_GWCA))
#define RCEC_RCE		BIT(16)
#define RCDC_RCD		BIT(16)

#define CABPIRM_BPIOG		BIT(0)
#define CABPIRM_BPR		BIT(1)

/* MFWD */
#define FWPC0_LTHTA		BIT(0)
#define FWPC0_IP4UE		BIT(3)
#define FWPC0_IP4TE		BIT(4)
#define FWPC0_IP4OE		BIT(5)
#define FWPC0_L2SE		BIT(9)
#define FWPC0_IP4EA		BIT(10)
#define FWPC0_IPDSA		BIT(12)
#define FWPC0_IPHLA		BIT(18)
#define FWPC0_MACSDA		BIT(20)
#define FWPC0_MACHLA		BIT(26)
#define FWPC0_MACHMA		BIT(27)
#define FWPC0_VLANSA		BIT(28)

#define FWPC0(i)		(FWPC00 + (i) * 0x10)
#define FWPC0_DEFAULT		(FWPC0_LTHTA | FWPC0_IP4UE | FWPC0_IP4TE | \
				 FWPC0_IP4OE | FWPC0_L2SE | FWPC0_IP4EA | \
				 FWPC0_IPDSA | FWPC0_IPHLA | FWPC0_MACSDA | \
				 FWPC0_MACHLA |	FWPC0_MACHMA | FWPC0_VLANSA)
#define FWPC1(i)		(FWPC10 + (i) * 0x10)
#define FWPC1_DDE		BIT(0)

#define	FWPBFC(i)		(FWPBFC0 + (i) * 0x10)

#define FWPBFCSDC(j, i)         (FWPBFCSDC00 + (i) * 0x10 + (j) * 0x04)

/* TOP */
#define TPEMIMC7(queue)		(TPEMIMC70 + (queue) * 4)

/* Descriptors */
enum RX_DS_CC_BIT {
	RX_DS	= 0x0fff, /* Data size */
	RX_TR	= 0x1000, /* Truncation indication */
	RX_EI	= 0x2000, /* Error indication */
	RX_PS	= 0xc000, /* Padding selection */
};

enum TX_DS_TAGL_BIT {
	TX_DS	= 0x0fff, /* Data size */
	TX_TAGL	= 0xf000, /* Frame tag LSBs */
};

enum DIE_DT {
	/* Frame data */
	DT_FSINGLE	= 0x80,
	DT_FSTART	= 0x90,
	DT_FMID		= 0xa0,
	DT_FEND		= 0xb0,

	/* Chain control */
	DT_LEMPTY	= 0xc0,
	DT_EEMPTY	= 0xd0,
	DT_LINKFIX	= 0x00,
	DT_LINK		= 0xe0,
	DT_EOS		= 0xf0,
	/* HW/SW arbitration */
	DT_FEMPTY	= 0x40,
	DT_FEMPTY_IS	= 0x10,
	DT_FEMPTY_IC	= 0x20,
	DT_FEMPTY_ND	= 0x30,
	DT_FEMPTY_START	= 0x50,
	DT_FEMPTY_MID	= 0x60,
	DT_FEMPTY_END	= 0x70,

	DT_MASK		= 0xf0,
	DIE		= 0x08,	/* Descriptor Interrupt Enable */
};

/* Both transmission and reception */
#define INFO1_FMT		BIT(2)
#define INFO1_TXC		BIT(3)

/* For transmission */
#define INFO1_TSUN(val)		((u64)(val) << 8ULL)
#define INFO1_CSD0(index)	((u64)(index) << 32ULL)
#define INFO1_CSD1(index)	((u64)(index) << 40ULL)
#define INFO1_DV(port_vector)	((u64)(port_vector) << 48ULL)

/* For reception */
#define INFO1_SPN(port)		((u64)(port) << 36ULL)

/* For timestamp descriptor in dptrl (Byte 4 to 7) */
#define TS_DESC_TSUN(dptrl)	((dptrl) & GENMASK(7, 0))
#define TS_DESC_SPN(dptrl)	(((dptrl) & GENMASK(10, 8)) >> 8)
#define TS_DESC_DPN(dptrl)	(((dptrl) & GENMASK(17, 16)) >> 16)
#define TS_DESC_TN(dptrl)	((dptrl) & BIT(24))

struct rswitch_desc {
	__le16 info_ds;	/* Descriptor size */
	u8 die_dt;	/* Descriptor interrupt enable and type */
	__u8  dptrh;	/* Descriptor pointer MSB */
	__le32 dptrl;	/* Descriptor pointer LSW */
} __packed;

struct rswitch_ts_desc {
	struct rswitch_desc desc;
	__le32 ts_nsec;
	__le32 ts_sec;
} __packed;

struct rswitch_ext_desc {
	struct rswitch_desc desc;
	__le64 info1;
} __packed;

struct rswitch_ext_ts_desc {
	struct rswitch_desc desc;
	__le64 info1;
	__le32 ts_nsec;
	__le32 ts_sec;
} __packed;

struct rswitch_etha {
	int index;
	void __iomem *addr;
	void __iomem *coma_addr;
	bool external_phy;
	struct mii_bus *mii;
	phy_interface_t phy_interface;
	u8 mac_addr[MAX_ADDR_LEN];
	int link;
	int speed;

	/* This hardware could not be initialized twice so that marked
	 * this flag to avoid multiple initialization.
	 */
	bool operated;
};

/* The datasheet said descriptor "chain" and/or "queue". For consistency of
 * name, this driver calls "queue".
 */
struct rswitch_gwca_queue {
	union {
		struct rswitch_ext_desc *tx_ring;
		struct rswitch_ext_ts_desc *rx_ring;
		struct rswitch_ts_desc *ts_ring;
	};

	/* Common */
	dma_addr_t ring_dma;
	int ring_size;
	int cur;
	int dirty;

	/* For [rt]_ring */
	int index;
	bool dir_tx;
	struct sk_buff **skbs;
	struct net_device *ndev;	/* queue to ndev for irq */
};

struct rswitch_gwca_ts_info {
	struct sk_buff *skb;
	struct list_head list;

	int port;
	u8 tag;
};

#define RSWITCH_NUM_IRQ_REGS	(RSWITCH_MAX_NUM_QUEUES / BITS_PER_TYPE(u32))
struct rswitch_gwca {
	int index;
	struct rswitch_desc *linkfix_table;
	dma_addr_t linkfix_table_dma;
	u32 linkfix_table_size;
	struct rswitch_gwca_queue *queues;
	int num_queues;
	struct rswitch_gwca_queue ts_queue;
	struct list_head ts_info_list;
	DECLARE_BITMAP(used, RSWITCH_MAX_NUM_QUEUES);
	u32 tx_irq_bits[RSWITCH_NUM_IRQ_REGS];
	u32 rx_irq_bits[RSWITCH_NUM_IRQ_REGS];
	int speed;
};

#define NUM_QUEUES_PER_NDEV	2
struct rswitch_device {
	struct rswitch_private *priv;
	struct net_device *ndev;
	struct napi_struct napi;
	void __iomem *addr;
	struct rswitch_gwca_queue *tx_queue;
	struct rswitch_gwca_queue *rx_queue;
	u8 ts_tag;
	bool disabled;

	int port;
	struct rswitch_etha *etha;
	struct device_node *np_port;
	struct phy *serdes;
};

struct rswitch_mfwd_mac_table_entry {
	int queue_index;
	unsigned char addr[MAX_ADDR_LEN];
};

struct rswitch_mfwd {
	struct rswitch_mac_table_entry *mac_table_entries;
	int num_mac_table_entries;
};

struct rswitch_private {
	struct platform_device *pdev;
	void __iomem *addr;
	struct rcar_gen4_ptp_private *ptp_priv;

	struct rswitch_device *rdev[RSWITCH_NUM_PORTS];
	DECLARE_BITMAP(opened_ports, RSWITCH_NUM_PORTS);

	struct rswitch_gwca gwca;
	struct rswitch_etha etha[RSWITCH_NUM_PORTS];
	struct rswitch_mfwd mfwd;

	bool gwca_halt;
};

#endif	/* #ifndef __RSWITCH_H__ */

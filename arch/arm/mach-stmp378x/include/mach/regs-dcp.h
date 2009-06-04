/*
 * stmp378x: DCP register definitions
 *
 * Copyright (c) 2008 Freescale Semiconductor
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#define REGS_DCP_BASE	(STMP3XXX_REGS_BASE + 0x28000)
#define REGS_DCP_PHYS	0x80028000
#define REGS_DCP_SIZE	0x2000

#define HW_DCP_CTRL		0x0
#define BM_DCP_CTRL_CHANNEL_INTERRUPT_ENABLE	0x000000FF
#define BP_DCP_CTRL_CHANNEL_INTERRUPT_ENABLE	0
#define BM_DCP_CTRL_ENABLE_CONTEXT_CACHING	0x00400000
#define BM_DCP_CTRL_GATHER_RESIDUAL_WRITES	0x00800000
#define BM_DCP_CTRL_CLKGATE	0x40000000
#define BM_DCP_CTRL_SFTRST	0x80000000

#define HW_DCP_STAT		0x10
#define BM_DCP_STAT_IRQ		0x0000000F
#define BP_DCP_STAT_IRQ		0

#define HW_DCP_CHANNELCTRL	0x20
#define BM_DCP_CHANNELCTRL_ENABLE_CHANNEL	0x000000FF
#define BP_DCP_CHANNELCTRL_ENABLE_CHANNEL	0

#define HW_DCP_CONTEXT		0x50
#define BM_DCP_PACKET1_INTERRUPT	0x00000001
#define BP_DCP_PACKET1_INTERRUPT	0
#define BM_DCP_PACKET1_DECR_SEMAPHORE	0x00000002
#define BM_DCP_PACKET1_CHAIN	0x00000004
#define BM_DCP_PACKET1_CHAIN_CONTIGUOUS	0x00000008
#define BM_DCP_PACKET1_ENABLE_CIPHER	0x00000020
#define BM_DCP_PACKET1_ENABLE_HASH	0x00000040
#define BM_DCP_PACKET1_CIPHER_ENCRYPT	0x00000100
#define BM_DCP_PACKET1_CIPHER_INIT	0x00000200
#define BM_DCP_PACKET1_OTP_KEY	0x00000400
#define BM_DCP_PACKET1_PAYLOAD_KEY	0x00000800
#define BM_DCP_PACKET1_HASH_INIT	0x00001000
#define BM_DCP_PACKET1_HASH_TERM	0x00002000
#define BM_DCP_PACKET2_CIPHER_SELECT	0x0000000F
#define BP_DCP_PACKET2_CIPHER_SELECT	0
#define BM_DCP_PACKET2_CIPHER_MODE	0x000000F0
#define BP_DCP_PACKET2_CIPHER_MODE	4
#define BM_DCP_PACKET2_KEY_SELECT	0x0000FF00
#define BP_DCP_PACKET2_KEY_SELECT	8
#define BM_DCP_PACKET2_HASH_SELECT	0x000F0000
#define BP_DCP_PACKET2_HASH_SELECT	16
#define BM_DCP_PACKET2_CIPHER_CFG	0xFF000000
#define BP_DCP_PACKET2_CIPHER_CFG	24

#define HW_DCP_CH0CMDPTR	(0x100 + 0 * 0x40)
#define HW_DCP_CH1CMDPTR	(0x100 + 1 * 0x40)
#define HW_DCP_CH2CMDPTR	(0x100 + 2 * 0x40)
#define HW_DCP_CH3CMDPTR	(0x100 + 3 * 0x40)

#define HW_DCP_CHnCMDPTR	0x100

#define HW_DCP_CH0SEMA		(0x110 + 0 * 0x40)
#define HW_DCP_CH1SEMA		(0x110 + 1 * 0x40)
#define HW_DCP_CH2SEMA		(0x110 + 2 * 0x40)
#define HW_DCP_CH3SEMA		(0x110 + 3 * 0x40)

#define HW_DCP_CHnSEMA		0x110
#define BM_DCP_CHnSEMA_INCREMENT	0x000000FF
#define BP_DCP_CHnSEMA_INCREMENT	0

#define HW_DCP_CH0STAT		(0x120 + 0 * 0x40)
#define HW_DCP_CH1STAT		(0x120 + 1 * 0x40)
#define HW_DCP_CH2STAT		(0x120 + 2 * 0x40)
#define HW_DCP_CH3STAT		(0x120 + 3 * 0x40)

#define HW_DCP_CHnSTAT		0x120

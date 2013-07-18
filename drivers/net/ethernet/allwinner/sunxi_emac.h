/*
 * drivers/net/sunxi/sunxi_emac.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*
 * sunxi ethernet mac
 */

#ifndef _SUNXI_EMAC_H_
#define _SUNXI_EMAC_H_

/*   registers define  */
/*  SUNXI_EMAC register  */
#define SUNXI_EMAC_BASE		0x01C0B000

#define SUNXI_EMAC_CTL_REG             (0x00)
#define SUNXI_EMAC_TX_MODE_REG         (0x04)
#define SUNXI_EMAC_TX_FLOW_REG         (0x08)
#define SUNXI_EMAC_TX_CTL0_REG         (0x0C)
#define SUNXI_EMAC_TX_CTL1_REG         (0x10)
#define SUNXI_EMAC_TX_INS_REG          (0x14)
#define SUNXI_EMAC_TX_PL0_REG          (0x18)
#define SUNXI_EMAC_TX_PL1_REG          (0x1C)
#define SUNXI_EMAC_TX_STA_REG          (0x20)
#define SUNXI_EMAC_TX_IO_DATA_REG      (0x24)
#define SUNXI_EMAC_TX_IO_DATA1_REG     (0x28)
#define SUNXI_EMAC_TX_TSVL0_REG        (0x2C)
#define SUNXI_EMAC_TX_TSVH0_REG        (0x30)
#define SUNXI_EMAC_TX_TSVL1_REG        (0x34)
#define SUNXI_EMAC_TX_TSVH1_REG        (0x38)
#define SUNXI_EMAC_RX_CTL_REG          (0x3C)
#define SUNXI_EMAC_RX_HASH0_REG        (0x40)
#define SUNXI_EMAC_RX_HASH1_REG        (0x44)
#define SUNXI_EMAC_RX_STA_REG          (0x48)
#define SUNXI_EMAC_RX_IO_DATA_REG      (0x4C)
#define SUNXI_EMAC_RX_FBC_REG          (0x50)
#define SUNXI_EMAC_INT_CTL_REG         (0x54)
#define SUNXI_EMAC_INT_STA_REG         (0x58)
#define SUNXI_EMAC_MAC_CTL0_REG        (0x5C)
#define SUNXI_EMAC_MAC_CTL1_REG        (0x60)
#define SUNXI_EMAC_MAC_IPGT_REG        (0x64)
#define SUNXI_EMAC_MAC_IPGR_REG        (0x68)
#define SUNXI_EMAC_MAC_CLRT_REG        (0x6C)
#define SUNXI_EMAC_MAC_MAXF_REG        (0x70)
#define SUNXI_EMAC_MAC_SUPP_REG        (0x74)
#define SUNXI_EMAC_MAC_TEST_REG        (0x78)
#define SUNXI_EMAC_MAC_MCFG_REG        (0x7C)
#define SUNXI_EMAC_MAC_MCMD_REG        (0x80)
#define SUNXI_EMAC_MAC_MADR_REG        (0x84)
#define SUNXI_EMAC_MAC_MWTD_REG        (0x88)
#define SUNXI_EMAC_MAC_MRDD_REG        (0x8C)
#define SUNXI_EMAC_MAC_MIND_REG        (0x90)
#define SUNXI_EMAC_MAC_SSRR_REG        (0x94)
#define SUNXI_EMAC_MAC_A0_REG          (0x98)
#define SUNXI_EMAC_MAC_A1_REG          (0x9C)
#define SUNXI_EMAC_MAC_A2_REG          (0xA0)
#define SUNXI_EMAC_SAFX_L_REG0         (0xA4)
#define SUNXI_EMAC_SAFX_H_REG0         (0xA8)
#define SUNXI_EMAC_SAFX_L_REG1         (0xAC)
#define SUNXI_EMAC_SAFX_H_REG1         (0xB0)
#define SUNXI_EMAC_SAFX_L_REG2         (0xB4)
#define SUNXI_EMAC_SAFX_H_REG2         (0xB8)
#define SUNXI_EMAC_SAFX_L_REG3         (0xBC)
#define SUNXI_EMAC_SAFX_H_REG3         (0xC0)

/*  PIO register  */
#define PA_CFG0_REG        	(0x00)
#define PD_CFG0_REG		(0x6c)

//SRAMC register
#define SRAMC_BASE		0x01c00000
#define SRAMC_CFG_REG		(0x04)


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~registers define~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


//set up PHY
#define PHY_AUTO_NEGOTIOATION  1  // 0: Normal     1: Auto(default)
#define PHY_SPEED              1  // 0: 10M        1: 100M(default)
#define SUNXI_EMAC_MAC_FULL          1  //0: Half duplex   1: Full duplex(default)

//set up SUNXI_EMAC TX
#define SUNXI_EMAC_TX_TM          0  //0: CPU           1: DMA(default)
#define SUNXI_EMAC_TX_AB_M        1  //0: Disable       1: Aborted frame enable(default)

//set up SUNXI_EMAC RX
#define SUNXI_EMAC_RX_TM          0  //0: CPU           1: DMA(default)
#define SUNXI_EMAC_RX_DRQ_MODE    0  //0: DRQ asserted  1: DRQ automatically(default)

#define SUNXI_EMAC_RX_PA          0  //0: Normal(default)        1: Pass all Frames
#define SUNXI_EMAC_RX_PCF         0  //0: Normal(default)        1: Pass Control Frames
#define SUNXI_EMAC_RX_PCRCE       0  //0: Normal(default)        1: Pass Frames with CRC Error
#define SUNXI_EMAC_RX_POR         1  //0: Normal                 1: Pass Frames length out of range(default)
#define SUNXI_EMAC_RX_PLE         0	 //0: Normal(default)        1: Pass Frames with Length Error
#define SUNXI_EMAC_RX_UCAD        1  //0: Not accept             1: Accept unicast Packets(default)
#define SUNXI_EMAC_RX_DAF         1  //0: Normal(default)        1: DA Filtering
#define SUNXI_EMAC_RX_MCO         1  //0: Not accept             1: Accept multicast Packets(default)
#define SUNXI_EMAC_RX_MHF         0  //0: Disable(default)       1: Enable Hash filter
#define SUNXI_EMAC_RX_BCO		    1  //0: Not accept             1: Accept Broadcast Packets(default)
#define SUNXI_EMAC_RX_SAF         0  //0: Disable(default)       1: Enable SA Filtering
#define SUNXI_EMAC_RX_SAIF        0  //0: Normal(default)        1: Inverse Filtering

//set up MAC
#define SUNXI_EMAC_MAC_TFC        1  //0: Disable                1: Enable Transmit Flow Control(default)
#define SUNXI_EMAC_MAC_RFC        1  //0: Disable                1: Enable Receive Flow Control(default)



#define SUNXI_EMAC_MAC_FLC        1  //0: Disable                1: Enable MAC Frame Length Checking(default)
#define SUNXI_EMAC_MAC_HF         0  //0: Disable(default)       1: Enable Huge Frame
#define SUNXI_EMAC_MAC_DCRC       0  //0: Disable(default)       1: Enable MAC Delayed CRC
#define SUNXI_EMAC_MAC_CRC        1  //0: Disable                1: Enable MAC CRC(default)
#define SUNXI_EMAC_MAC_PC         1  //0: Disable                1: Enable MAC PAD Short frames(default)
#define SUNXI_EMAC_MAC_VC         0  //0: Disable(default)       1: Enable MAC PAD Short frames and append CRC
#define SUNXI_EMAC_MAC_ADP        0  //0: Disable(default)       1: Enable MAC auto detect Short frames
#define SUNXI_EMAC_MAC_PRE        0  //0: Disable(default)       1: Enable
#define SUNXI_EMAC_MAC_LPE        0  //0: Disable(default)       1: Enable
#define SUNXI_EMAC_MAC_NB         0  //0: Disable(default)       1: Enable no back off
#define SUNXI_EMAC_MAC_BNB        0  //0: Disable(default)       1: Enable
#define SUNXI_EMAC_MAC_ED         0  //0: Disable(default)       1: Enable

#if SUNXI_EMAC_MAC_FULL
  #define SUNXI_EMAC_MAC_IPGT   0x15
#else
  #define SUNXI_EMAC_MAC_IPGT   0x12
#endif

#define SUNXI_EMAC_MAC_NBTB_IPG1   0xC
#define SUNXI_EMAC_MAC_NBTB_IPG2   0x12

#define SUNXI_EMAC_MAC_CW   0x37
#define SUNXI_EMAC_MAC_RM   0xF

#define SUNXI_EMAC_MAC_MFL  0x0600

#define SUNXI_EMAC_MAP1

//define receive status
#define SUNXI_EMAC_CRCERR		(1<<4)
#define SUNXI_EMAC_LENERR		(3<<5)

#endif /* _SUNXI_EMAC_H_ */

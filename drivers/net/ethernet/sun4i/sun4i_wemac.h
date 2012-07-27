/*
 * drivers/net/sun4i/sun4i_wemac.h
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
 * wemac Ethernet
 */

#ifndef _WEMACX_H_
#define _WEMACX_H_

/*   registers define  */
/*  EMAC register  */
#define EMAC_BASE		0x01C0B000

#define EMAC_CTL_REG             (0x00)
#define EMAC_TX_MODE_REG         (0x04)
#define EMAC_TX_FLOW_REG         (0x08)
#define EMAC_TX_CTL0_REG         (0x0C)
#define EMAC_TX_CTL1_REG         (0x10)
#define EMAC_TX_INS_REG          (0x14)
#define EMAC_TX_PL0_REG          (0x18)
#define EMAC_TX_PL1_REG          (0x1C)
#define EMAC_TX_STA_REG          (0x20)
#define EMAC_TX_IO_DATA_REG      (0x24)
#define EMAC_TX_IO_DATA1_REG     (0x28)
#define EMAC_TX_TSVL0_REG        (0x2C)
#define EMAC_TX_TSVH0_REG        (0x30)
#define EMAC_TX_TSVL1_REG        (0x34)
#define EMAC_TX_TSVH1_REG        (0x38)
#define EMAC_RX_CTL_REG          (0x3C)
#define EMAC_RX_HASH0_REG        (0x40)
#define EMAC_RX_HASH1_REG        (0x44)
#define EMAC_RX_STA_REG          (0x48)
#define EMAC_RX_IO_DATA_REG      (0x4C)
#define EMAC_RX_FBC_REG          (0x50)
#define EMAC_INT_CTL_REG         (0x54)
#define EMAC_INT_STA_REG         (0x58)
#define EMAC_MAC_CTL0_REG        (0x5C)
#define EMAC_MAC_CTL1_REG        (0x60)
#define EMAC_MAC_IPGT_REG        (0x64)
#define EMAC_MAC_IPGR_REG        (0x68)
#define EMAC_MAC_CLRT_REG        (0x6C)
#define EMAC_MAC_MAXF_REG        (0x70)
#define EMAC_MAC_SUPP_REG        (0x74)
#define EMAC_MAC_TEST_REG        (0x78)
#define EMAC_MAC_MCFG_REG        (0x7C)
#define EMAC_MAC_MCMD_REG        (0x80)
#define EMAC_MAC_MADR_REG        (0x84)
#define EMAC_MAC_MWTD_REG        (0x88)
#define EMAC_MAC_MRDD_REG        (0x8C)
#define EMAC_MAC_MIND_REG        (0x90)
#define EMAC_MAC_SSRR_REG        (0x94)
#define EMAC_MAC_A0_REG          (0x98)
#define EMAC_MAC_A1_REG          (0x9C)
#define EMAC_MAC_A2_REG          (0xA0)
#define EMAC_SAFX_L_REG0         (0xA4)
#define EMAC_SAFX_H_REG0         (0xA8)
#define EMAC_SAFX_L_REG1         (0xAC)
#define EMAC_SAFX_H_REG1         (0xB0)
#define EMAC_SAFX_L_REG2         (0xB4)
#define EMAC_SAFX_H_REG2         (0xB8)
#define EMAC_SAFX_L_REG3         (0xBC)
#define EMAC_SAFX_H_REG3         (0xC0)

/*  PIO register  */
//#define PIO_BASE            	0x01c20800

#define PA_CFG0_REG        	(0x00)
#define PA_CFG1_REG        	(0x04)
#define PA_CFG2_REG        	(0x08)
#define PA_DAT_REG         	(0x0c)
#define PA_DRV0_REG        	(0x10)
#define PA_DRV1_REG        	(0x14)
#define PA_PULLUP0_REG     	(0x18)
#define PA_PULLUP1_REG     	(0x1c)
#define PB_CFG0_REG        	(0x20)
#define PB_CFG1_REG        	(0x24)
#define PB_CFG2_REG        	(0x28)
#define PB_DAT_REG         	(0x2c)
#define PB_DRV0_REG        	(0x30)
#define PB_DRV1_REG        	(0x34)
#define PB_PULLUP0_REG     	(0x38)
#define PB_PULLUP1_REG     	(0x3c)
#define PC_CFG0_REG        	(0x40)
#define PC_CFG1_REG        	(0x44)
#define PC_CFG2_REG        	(0x48)
#define PC_DAT_REG         	(0x4c)
#define PC_DRV0_REG        	(0x50)
#define PC_DRV1_REG        	(0x54)
#define PC_PULLUP0_REG     	(0x58)
#define PC_PULLUP1_REG     	(0x5c)
#define PD_CFG0_REG        	(0x60)
#define PD_CFG1_REG        	(0x64)
#define PD_CFG2_REG        	(0x68)
#define PD_CFG3_REG        	(0x6c)
#define PD_DAT_REG         	(0x70)
#define PD_DRV0_REG        	(0x74)
#define PD_DRV1_REG        	(0x78)
#define PD_PULLUP0_REG     	(0x7c)
#define PD_PULLUP1_REG     	(0x80)
#define PE_CFG0_REG        	(0x84)
#define PE_CFG1_REG        	(0x88)
#define PE_DAT_REG         	(0x8c)
#define PE_DRV_REG         	(0x90)
#define PE_PULLUP_REG      	(0x94)
#define PF_DRV_REG         	(0x98)
#define PF_PULLUP_REG      	(0x9c)
#define PD_INT_REG         	(0xa0)


//CCM register
#define CCM_BASE           	0x01c20000

#define CCM_AC320_MACC_REG 	(0x00)
#define CCM_AUDIO_HOSC_REG 	(0x04)
#define CCM_AHB_APB_REG    	(0x08)
#define CCM_AHB_GATING_REG 	(0x0c)
#define CCM_APB_GATING_REG 	(0x10)
#define CCM_NFC_MS_REG     	(0x14)
#define CCM_SD01_REG       	(0x18)
#define CCM_SD23_REG	    	(0x1c)
#define CCM_DRAM_PLL_REG   	(0x20)
#define CCM_DE_REG         	(0x24)
#define CCM_LCD_MACC_REG   	(0x28)
#define CCM_TV_CSI_REG     	(0x2c)
#define CCM_VIDEO_PLL_REG  	(0x30)
#define CCM_IR_CLK_REG     	(0x34)
#define CCM_AUDIO_CLK_REG  	(0x38)
#define CCM_TS_CLK_REG   	(0x3c)
#define CCM_AVS_USB_CLK_REG 	(0x40)
#define CCM_PID_CLK_REG       	(0xd0)
#define CCM_WAKEUP_PENDING_REG	(0xd4)


//SRAMC register
#define SRAMC_BASE		0x01c00000
#define SRAMC_CFG_REG		(0x04)


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~registers define~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


//set up PHY
#define PHY_AUTO_NEGOTIOATION  1  // 0: Normal     1: Auto(default)
#define PHY_SPEED              1  // 0: 10M        1: 100M(default)
#define EMAC_MAC_FULL          1  //0: Half duplex   1: Full duplex(default)

//set up EMAC TX
#define EMAC_TX_TM          0  //0: CPU           1: DMA(default)
#define EMAC_TX_AB_M        1  //0: Disable       1: Aborted frame enable(default)

//set up EMAC RX
#define EMAC_RX_TM          0  //0: CPU           1: DMA(default)
#define EMAC_RX_DRQ_MODE    0  //0: DRQ asserted  1: DRQ automatically(default)

#define EMAC_RX_PA          0  //0: Normal(default)        1: Pass all Frames
#define EMAC_RX_PCF         0  //0: Normal(default)        1: Pass Control Frames
#define EMAC_RX_PCRCE       0  //0: Normal(default)        1: Pass Frames with CRC Error
#define EMAC_RX_POR         1  //0: Normal                 1: Pass Frames length out of range(default)
#define EMAC_RX_PLE         0	 //0: Normal(default)        1: Pass Frames with Length Error
#define EMAC_RX_UCAD        1  //0: Not accept             1: Accept unicast Packets(default)
#define EMAC_RX_DAF         1  //0: Normal(default)        1: DA Filtering
#define EMAC_RX_MCO         1  //0: Not accept             1: Accept multicast Packets(default)
#define EMAC_RX_MHF         0  //0: Disable(default)       1: Enable Hash filter
#define EMAC_RX_BCO		    1  //0: Not accept             1: Accept Broadcast Packets(default)
#define EMAC_RX_SAF         0  //0: Disable(default)       1: Enable SA Filtering
#define EMAC_RX_SAIF        0  //0: Normal(default)        1: Inverse Filtering

//set up MAC
#define EMAC_MAC_TFC        1  //0: Disable                1: Enable Transmit Flow Control(default)
#define EMAC_MAC_RFC        1  //0: Disable                1: Enable Receive Flow Control(default)



#define EMAC_MAC_FLC        1  //0: Disable                1: Enable MAC Frame Length Checking(default)
#define EMAC_MAC_HF         0  //0: Disable(default)       1: Enable Huge Frame
#define EMAC_MAC_DCRC       0  //0: Disable(default)       1: Enable MAC Delayed CRC
#define EMAC_MAC_CRC        1  //0: Disable                1: Enable MAC CRC(default)
#define EMAC_MAC_PC         1  //0: Disable                1: Enable MAC PAD Short frames(default)
#define EMAC_MAC_VC         0  //0: Disable(default)       1: Enable MAC PAD Short frames and append CRC
#define EMAC_MAC_ADP        0  //0: Disable(default)       1: Enable MAC auto detect Short frames
#define EMAC_MAC_PRE        0  //0: Disable(default)       1: Enable
#define EMAC_MAC_LPE        0  //0: Disable(default)       1: Enable
#define EMAC_MAC_NB         0  //0: Disable(default)       1: Enable no back off
#define EMAC_MAC_BNB        0  //0: Disable(default)       1: Enable
#define EMAC_MAC_ED         0  //0: Disable(default)       1: Enable

#if EMAC_MAC_FULL
  #define EMAC_MAC_IPGT   0x15
#else
  #define EMAC_MAC_IPGT   0x12
#endif

#define EMAC_MAC_NBTB_IPG1   0xC
#define EMAC_MAC_NBTB_IPG2   0x12

#define EMAC_MAC_CW   0x37
#define EMAC_MAC_RM   0xF

#define EMAC_MAC_MFL  0x0600

#define EMAC_MAP1

//define receive status
#define EMAC_CRCERR		(1<<4)
#define EMAC_LENERR		(3<<5)

#define WEMAC_PLATF_8BITONLY    (0x0001)
#define WEMAC_PLATF_16BITONLY   (0x0002)
#define WEMAC_PLATF_32BITONLY   (0x0004)
#define WEMAC_PLATF_EXT_PHY     (0x0008)
#define WEMAC_PLATF_NO_EEPROM   (0x0010)
#define WEMAC_PLATF_SIMPLE_PHY (0x0020)  /* Use NSR to find LinkStatus */

/* platfrom data for platfrom device structure's platfrom_data field */

struct wemac_plat_data {
        unsigned int    flags;
        unsigned char   dev_addr[6];

        /* allow replacement IO routines */

        void    (*inblk)(void __iomem *reg, void *data, int len);
        void    (*outblk)(void __iomem *reg, void *data, int len);
        void    (*dumpblk)(void __iomem *reg, int len);
};


#endif /* _WEMACX_H_ */


/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __GSPI_OPS_H__
#define __GSPI_OPS_H__

/* follwing defination is based on
 * GSPI spec of RTL8723, we temp
 * suppose that it will be the same
 * for diff chips of GSPI, if not
 * we should move it to HAL folder */
#define SPI_LOCAL_DOMAIN 				0x0
#define WLAN_IOREG_DOMAIN 			0x8
#define FW_FIFO_DOMAIN 				0x4
#define TX_HIQ_DOMAIN 					0xc
#define TX_MIQ_DOMAIN 					0xd
#define TX_LOQ_DOMAIN 					0xe
#define RX_RXFIFO_DOMAIN 				0x1f

//IO Bus domain address mapping
#define DEFUALT_OFFSET					0x0
#define SPI_LOCAL_OFFSET    				0x10250000
#define WLAN_IOREG_OFFSET   			0x10260000
#define FW_FIFO_OFFSET 	    			0x10270000
#define TX_HIQ_OFFSET	    				0x10310000
#define TX_MIQ_OFFSET					0x1032000
#define TX_LOQ_OFFSET					0x10330000
#define RX_RXOFF_OFFSET	    			0x10340000

//SPI Local registers
#define SPI_REG_TX_CTRL					0x0000 // SPI Tx Control
#define SPI_REG_STATUS_RECOVERY		0x0004
#define SPI_REG_INT_TIMEOUT		   	0x0006
#define SPI_REG_HIMR					0x0014 // SPI Host Interrupt Mask
#define SPI_REG_HISR					0x0018 // SPI Host Interrupt Service Routine
#define SPI_REG_RX0_REQ_LEN			0x001C // RXDMA Request Length
#define SPI_REG_FREE_TXPG				0x0020 // Free Tx Buffer Page
#define SPI_REG_HCPWM1					0x0024 // HCI Current Power Mode 1
#define SPI_REG_HCPWM2					0x0026 // HCI Current Power Mode 2
#define SPI_REG_HTSFR_INFO				0x0030 // HTSF Informaion
#define SPI_REG_HRPWM1					0x0080 // HCI Request Power Mode 1
#define SPI_REG_HRPWM2					0x0082 // HCI Request Power Mode 2
#define SPI_REG_HPS_CLKR				0x0084 // HCI Power Save Clock
#define SPI_REG_HSUS_CTRL				0x0086 // SPI HCI Suspend Control
#define SPI_REG_HIMR_ON				0x0090 //SPI Host Extension Interrupt Mask Always
#define SPI_REG_HISR_ON				0x0091 //SPI Host Extension Interrupt Status Always
#define SPI_REG_CFG						0x00F0 //SPI Configuration Register

#define SPI_TX_CTRL 	               			(SPI_REG_TX_CTRL  |SPI_LOCAL_OFFSET)
#define SPI_STATUS_RECOVERY		   	(SPI_REG_STATUS_RECOVERY  |SPI_LOCAL_OFFSET)
#define SPI_INT_TIMEOUT		   	   		(SPI_REG_INT_TIMEOUT  |SPI_LOCAL_OFFSET)
#define SPI_HIMR 	                  			(SPI_REG_HIMR |SPI_LOCAL_OFFSET)
#define SPI_HISR 	                   			(SPI_REG_HISR |SPI_LOCAL_OFFSET)
#define SPI_RX0_REQ_LEN_1_BYTE 	   	(SPI_REG_RX0_REQ_LEN |SPI_LOCAL_OFFSET)
#define SPI_FREE_TXPG 	               		(SPI_REG_FREE_TXPG |SPI_LOCAL_OFFSET)

#define	SPI_HIMR_DISABLED				0

//SPI HIMR MASK diff with SDIO
#define SPI_HISR_RX_REQUEST    			BIT(0)
#define SPI_HISR_AVAL					BIT(1)
#define SPI_HISR_TXERR					BIT(2)
#define SPI_HISR_RXERR					BIT(3)
#define SPI_HISR_TXFOVW				BIT(4)
#define SPI_HISR_RXFOVW				BIT(5)
#define SPI_HISR_TXBCNOK				BIT(6)
#define SPI_HISR_TXBCNERR				BIT(7)
#define SPI_HISR_BCNERLY_INT			BIT(16)
#define SPI_HISR_ATIMEND				BIT(17)
#define SPI_HISR_ATIMEND_E				BIT(18)
#define SPI_HISR_CTWEND				BIT(19)
#define SPI_HISR_C2HCMD				BIT(20)
#define SPI_HISR_CPWM1					BIT(21)
#define SPI_HISR_CPWM2					BIT(22)
#define SPI_HISR_HSISR_IND				BIT(23)
#define SPI_HISR_GTINT3_IND				BIT(24)
#define SPI_HISR_GTINT4_IND				BIT(25)
#define SPI_HISR_PSTIMEOUT				BIT(26)
#define SPI_HISR_OCPINT					BIT(27)
#define SPI_HISR_TSF_BIT32_TOGGLE		BIT(29)

#define MASK_SPI_HISR_CLEAR		(SPI_HISR_TXERR |\
									SPI_HISR_RXERR |\
									SPI_HISR_TXFOVW |\
									SPI_HISR_RXFOVW |\
									SPI_HISR_TXBCNOK |\
									SPI_HISR_TXBCNERR |\
									SPI_HISR_C2HCMD |\
									SPI_HISR_CPWM1 |\
									SPI_HISR_CPWM2 |\
									SPI_HISR_HSISR_IND |\
									SPI_HISR_GTINT3_IND |\
									SPI_HISR_GTINT4_IND |\
									SPI_HISR_PSTIMEOUT |\
									SPI_HISR_OCPINT)

#define REG_LEN_FORMAT(pcmd, x) 			SET_BITS_TO_LE_4BYTE(pcmd, 0, 8, x)//(x<<(unsigned int)24)
#define REG_ADDR_FORMAT(pcmd,x) 			SET_BITS_TO_LE_4BYTE(pcmd, 8, 16, x)//(x<<(unsigned int)16)
#define REG_DOMAIN_ID_FORMAT(pcmd,x) 		SET_BITS_TO_LE_4BYTE(pcmd, 24, 5, x)//(x<<(unsigned int)0)
#define REG_FUN_FORMAT(pcmd,x) 			SET_BITS_TO_LE_4BYTE(pcmd, 29, 2, x)//(x<<(unsigned int)5)
#define REG_RW_FORMAT(pcmd,x) 				SET_BITS_TO_LE_4BYTE(pcmd, 31, 1, x)//(x<<(unsigned int)7)

#define FIFO_LEN_FORMAT(pcmd, x) 			SET_BITS_TO_LE_4BYTE(pcmd, 0, 16, x)//(x<<(unsigned int)24)
//#define FIFO_ADDR_FORMAT(pcmd,x) 			SET_BITS_TO_LE_4BYTE(pcmd, 8, 16, x)//(x<<(unsigned int)16)
#define FIFO_DOMAIN_ID_FORMAT(pcmd,x) 	SET_BITS_TO_LE_4BYTE(pcmd, 24, 5, x)//(x<<(unsigned int)0)
#define FIFO_FUN_FORMAT(pcmd,x) 			SET_BITS_TO_LE_4BYTE(pcmd, 29, 2, x)//(x<<(unsigned int)5)
#define FIFO_RW_FORMAT(pcmd,x) 			SET_BITS_TO_LE_4BYTE(pcmd, 31, 1, x)//(x<<(unsigned int)7)


//get status dword0
#define GET_STATUS_PUB_PAGE_NUM(status)		LE_BITS_TO_4BYTE(status, 24, 8)
#define GET_STATUS_HI_PAGE_NUM(status)		LE_BITS_TO_4BYTE(status, 18, 6)
#define GET_STATUS_MID_PAGE_NUM(status)		LE_BITS_TO_4BYTE(status, 12, 6)
#define GET_STATUS_LOW_PAGE_NUM(status)		LE_BITS_TO_4BYTE(status, 6, 6)
#define GET_STATUS_HISR_HI6BIT(status)			LE_BITS_TO_4BYTE(status, 0, 6)

//get status dword1
#define GET_STATUS_HISR_MID8BIT(status)		LE_BITS_TO_4BYTE(status + 4, 24, 8)
#define GET_STATUS_HISR_LOW8BIT(status)		LE_BITS_TO_4BYTE(status + 4, 16, 8)
#define GET_STATUS_ERROR(status)		    		LE_BITS_TO_4BYTE(status + 4, 17, 1)
#define GET_STATUS_INT(status)		        		LE_BITS_TO_4BYTE(status + 4, 16, 1)
#define GET_STATUS_RX_LENGTH(status)			LE_BITS_TO_4BYTE(status + 4, 0, 16)


#define RXDESC_SIZE	24


struct spi_more_data {
	unsigned long more_data;
	unsigned long len;
};

#ifdef CONFIG_RTL8723A
void rtl8723as_set_hal_ops(PADAPTER padapter);
#define set_hal_ops rtl8723as_set_hal_ops
#endif

#ifdef CONFIG_RTL8188E
void rtl8188es_set_hal_ops(PADAPTER padapter);
#define set_hal_ops rtl8188es_set_hal_ops
#endif
extern void spi_set_chip_endian(PADAPTER padapter);
extern void spi_set_intf_ops(_adapter *padapter,struct _io_ops *pops);
extern void spi_set_chip_endian(PADAPTER padapter);
extern void InitInterrupt8723ASdio(PADAPTER padapter);
extern void InitSysInterrupt8723ASdio(PADAPTER padapter);
extern void EnableInterrupt8723ASdio(PADAPTER padapter);
extern void DisableInterrupt8723ASdio(PADAPTER padapter);
extern void spi_int_hdl(PADAPTER padapter);
extern u8 HalQueryTxBufferStatus8723ASdio(PADAPTER padapter);
extern void InitInterrupt8188ESdio(PADAPTER padapter);
extern void EnableInterrupt8188ESdio(PADAPTER padapter);
extern void DisableInterrupt8188ESdio(PADAPTER padapter);
#ifdef CONFIG_RTL8723B
extern void InitInterrupt8723BSdio(PADAPTER padapter);
extern void InitSysInterrupt8723BSdio(PADAPTER padapter);
extern void EnableInterrupt8723BSdio(PADAPTER padapter);
extern void DisableInterrupt8723BSdio(PADAPTER padapter);
extern u8 HalQueryTxBufferStatus8723BSdio(PADAPTER padapter);
#endif

#endif //__GSPI_OPS_H__

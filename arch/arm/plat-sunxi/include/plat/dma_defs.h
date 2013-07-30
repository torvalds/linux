/*
 * sunxi:hw des/src bit masks depending on sun4i/sun5i soc
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * huangxin <huangxin@allwinnertech.com>
 * (C) Copyright 2013
 * Alexsey Shestacov <wingrimen@gmail.com>
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
#ifndef __SUNXI_DMA_DEFS__
#define __SUNXI_DMA_DEFS__
/* DRQSRC and DRQDST are slightly different on sun4i and sun5i*/
#if defined CONFIG_ARCH_SUN4I
/*normal DMA Source*/
#define N_DRQSRC_IR0RX		0b00000
#define N_DRQSRC_IR1RX 		0b00001
#define N_DRQSRC_SPDIFRX	0b00010
#define N_DRQSRC_IISRX		0b00011
#define N_DRQSRC_AC97RX		0b00101
#define N_DRQSRC_UART0RX	0b01000
#define N_DRQSRC_UART1RX 	0b01001
#define N_DRQSRC_UART2RX	0b01010
#define N_DRQSRC_UART3RX	0b01011
#define N_DRQSRC_UART4RX	0b01100
#define N_DRQSRC_UART5RX	0b01101
#define N_DRQSRC_UART6RX	0b01110
#define N_DRQSRC_UART7RX	0b01111
#define N_DRQSRC_HDMIDDCRX	0b10000
#define N_DRQSRC_AUDIOCDAD	0b10011	//Audio Codec D/A
#define N_DRQSRC_SRAM		0b10101
#define N_DRQSRC_SDRAM		0b10110
#define N_DRQSRC_TPAD		0b10111	//TP A/D
#define N_DRQSRC_SPI0RX		0b11000
#define N_DRQSRC_SPI1RX		0b11001
#define N_DRQSRC_SPI2RX		0b11010
#define N_DRQSRC_SPI3RX		0b11011

/*normal DMA destination*/
#define N_DRQDST_IR0TX		0b00000
#define N_DRQDST_IR1TX 		0b00001
#define N_DRQDST_SPDIFTX	0b00010
#define N_DRQDST_IISTX		0b00011
#define N_DRQDST_AC97TX		0b00101
#define N_DRQDST_UART0TX	0b01000
#define N_DRQDST_UART1TX 	0b01001
#define N_DRQDST_UART2TX	0b01010
#define N_DRQDST_UART3TX	0b01011
#define N_DRQDST_UART4TX	0b01100
#define N_DRQDST_UART5TX	0b01101
#define N_DRQDST_UART6TX	0b01110
#define N_DRQDST_UART7TX	0b01111
#define N_DRQDST_HDMIDDCTX	0b10000	//HDMI DDC TX
#define N_DRQDST_AUDIOCDAD	0b10011	//Audio Codec D/A
#define N_DRQDST_SRAM		0b10101
#define N_DRQDST_SDRAM		0b10110
#define N_DRQDST_SPI0TX		0b11000
#define N_DRQDST_SPI1TX		0b11001
#define N_DRQDST_SPI2TX		0b11010
#define N_DRQDST_SPI3TX		0b11011

/*Dedicated DMA Source*/
#define D_DRQSRC_SRAM		0b00000//0x0 SRAM memory
#define D_DRQSRC_SDRAM		0b00001//0x1 SDRAM memory
#define D_DRQSRC_PATA		0b00010//0x2 PATA
#define D_DRQSRC_NAND 		0b00011//0x3 NAND Flash Controller(NFC)
#define D_DRQSRC_USB0 		0b00100//0x4 USB0
#define D_DRQSRC_EMACRX		0b00111//0x7 Ethernet MAC Rx
#define D_DRQSRC_SPI1RX		0b01001//0x9 SPI1 RX
#define D_DRQSRC_SECRX 		0b01011//0xB Security System Rx
#define D_DRQSRC_MS 		0b10111//0x17 Memory Stick Controller(MSC)
#define D_DRQSRC_SPI0RX		0b11011//0x1B SPI0 RX
#define D_DRQSRC_SPI2RX		0b11101//0x1D SPI2 RX
#define D_DRQSRC_SPI3RX		0b11111//0x1F SPI3 RX


/*Dedicated DMA Destination*/
#define D_DRQDST_SRAM		0b00000//0x0 SRAM memory
#define D_DRQDST_SDRAM		0b00001//0x1 SDRAM memory
#define D_DRQDST_PATA		0b00010//0x2 PATA
#define D_DRQDST_NAND 		0b00011//0x3 NAND Flash Controller(NFC)
#define D_DRQDST_USB0 		0b00100//0x4 USB0
#define D_DRQDST_EMACTX		0b00110//0x6 Ethernet MAC Rx
#define D_DRQDST_SPI1TX		0b01000//0x8 SPI1 RX
#define D_DRQDST_SECTX 		0b01010//0xA Security System Rx
#define D_DRQDST_TCON0 		0b01110//0xE TCON0
#define D_DRQDST_TCON1 		0b01111//0xF TCON1
#define D_DRQDST_MS		0b10111//0x17 Memory Stick Controller(MSC)
#define D_DRQDST_HDMIAUDIO	0b11000//0x18 HDMI Audio
#define D_DRQDST_SPI0TX		0b11010//0x1A SPI0 TX
#define D_DRQDST_SPI2TX		0b11100//0x1C SPI2 TX
#define D_DRQDST_SPI3TX		0b11110//0x1E SPI3 TX



#elif defined CONFIG_ARCH_SUN5I

/*normal DMA Source*/
#define N_DRQSRC_IRRX		0b00000
#define N_DRQSRC_SPDIFRX	0b00010
#define N_DRQSRC_IISRX		0b00011
#define N_DRQSRC_UART0RX	0b01000
#define N_DRQSRC_UART1RX 	0b01001
#define N_DRQSRC_UART2RX	0b01010
#define N_DRQSRC_UART3RX	0b01011
#define N_DRQSRC_HDMIDDCRX	0b10000
#define N_DRQSRC_AUDIOCDAD	0b10011	//Audio Codec D/A
#define N_DRQSRC_SRAM		0b10101
#define N_DRQSRC_SDRAM		0b10110
#define N_DRQSRC_TPAD		0b10111	//TP A/D
#define N_DRQSRC_SPI0RX		0b11000
#define N_DRQSRC_SPI1RX		0b11001
#define N_DRQSRC_SPI2RX		0b11010
#define N_DRQSRC_USBEP1		0b11011
#define N_DRQSRC_USBEP2		0b11100
#define N_DRQSRC_USBEP3		0b11101
#define N_DRQSRC_USBEP4		0b11110
#define N_DRQSRC_USBEP5		0b11111

/*normal DMA destination*/
#define N_DRQDST_IRTX		0b00000
#define N_DRQDST_SPDIFTX	0b00010
#define N_DRQDST_IISTX		0b00011
#define N_DRQDST_UART0TX	0b01000
#define N_DRQDST_UART1TX 	0b01001
#define N_DRQDST_UART2TX	0b01010
#define N_DRQDST_UART3TX	0b01011
#define N_DRQDST_HDMIDDCTX	0b10000//HDMI DDC TX
#define N_DRQDST_AUDIOCDAD	0b10011//Audio Codec D/A
#define N_DRQDST_SRAM		0b10101
#define N_DRQDST_SDRAM		0b10110
#define N_DRQDST_SPI0TX		0b11000
#define N_DRQDST_SPI1TX		0b11001
#define N_DRQDST_SPI2TX		0b11010
#define N_DRQDST_USBEP1		0b11011
#define N_DRQDST_USBEP2		0b11100
#define N_DRQDST_USBEP3		0b11101
#define N_DRQDST_USBEP4		0b11110
#define N_DRQDST_USBEP5		0b11111

/*Dedicated DMA Source*/
#define D_DRQSRC_SRAM		0b00000//0x0 SRAM memory
#define D_DRQSRC_SDRAM		0b00001//0x1 SDRAM memory
#define D_DRQSRC_NAND 		0b00011//0x3 NAND Flash Controller(NFC)
#define D_DRQSRC_USB0 		0b00100//0x4 USB0
#define D_DRQSRC_EMACRX		0b00111//0x7 Ethernet MAC Rx
#define D_DRQSRC_SPI1RX		0b01001//0x9 SPI1 RX
#define D_DRQSRC_SECRX 		0b01011//0xB Security System Rx
#define D_DRQSRC_MS 		0b10111//0x17 Memory Stick Controller(MSC)
#define D_DRQSRC_SPI0RX		0b11011//0x1B SPI0 RX
#define D_DRQSRC_SPI2RX		0b11101//0x1D SPI2 RX

/*Dedicated DMA Destination*/
#define D_DRQDST_SRAM		0b00000//0x0 SRAM memory
#define D_DRQDST_SDRAM		0b00001//0x1 SDRAM memory
#define D_DRQDST_NAND 		0b00011//0x3 NAND Flash Controller(NFC)
#define D_DRQDST_USB0 		0b00100//0x4 USB0
#define D_DRQDST_EMACTX		0b00110//0x6 Ethernet MAC Rx
#define D_DRQDST_SPI1TX		0b01000//0x8 SPI1 RX
#define D_DRQDST_SECTX 		0b01010//0xA Security System Tx
#define D_DRQDST_TCON0 		0b01110//0xE TCON0
#define D_DRQDST_MS			0b10111//0x17 Memory Stick Controller(MSC)
#define D_DRQDST_HDMIAUDIO	0b11000//0x18 HDMI Audio
#define D_DRQDST_SPI0TX		0b11010//0x1A SPI0 TX
#define D_DRQDST_SPI2TX		0b11100//0x1C SPI2 TX


#endif 


#if defined CONFIG_ARCH_SUN4I

enum drq_type {
		DRQ_TYPE_SRAM,
		DRQ_TYPE_SDRAM,
		DRQ_TYPE_PATA,
		DRQ_TYPE_NAND,
		DRQ_TYPE_USB0,
		DRQ_TYPE_EMAC,
		DRQ_TYPE_SPI1,
		DRQ_TYPE_SS,//Security System
		DRQ_TYPE_MS,//Memory Stick Control
		DRQ_TYPE_SPI0,
		DRQ_TYPE_SPI2,
		DRQ_TYPE_SPI3,
		DRQ_TYPE_TCON0,
		DRQ_TYPE_TCON1,
		DRQ_TYPE_HDMI,

		DRQ_TYPE_HDMIAUDIO,
		DRQ_TYPE_IR0,
		DRQ_TYPE_IR1,
		DRQ_TYPE_SPDIF,
		DRQ_TYPE_IIS,
		DRQ_TYPE_AC97,
		DRQ_TYPE_UART0,
		DRQ_TYPE_UART1,
		DRQ_TYPE_UART2,
		DRQ_TYPE_UART3,
		DRQ_TYPE_UART4,
		DRQ_TYPE_UART5,
		DRQ_TYPE_UART6,
		DRQ_TYPE_UART7,
		DRQ_TYPE_AUDIO,
		DRQ_TYPE_TPAD,
		DRQ_TYPE_MAX,
};

/* We use `virtual` dma channels to hide the fact we have only a limited
 * number of DMA channels, and not of all of them (dependant on the device)
 * can be attached to any DMA source. We therefore let the DMA core handle
 * the allocation of hardware channels to clients.
*/
enum sw_dma_ch {
	/*NDMA*/
	DMACH_NSPI0,
	DMACH_NSPI1,
	DMACH_NSPI2,
	DMACH_NSPI3,
	DMACH_NUART0,
	DMACH_NUART1,
	DMACH_NUART2,
	DMACH_NUART3,
	DMACH_NUART4,
	DMACH_NUART5,
	DMACH_NUART6,
	DMACH_NUART7,
	DMACH_NSRAM,
	DMACH_NSDRAM,
	DMACH_NTPAD,
	DMACH_NADDA_PLAY,//audio play
	DMACH_NADDA_CAPTURE,//audio capture
	DMACH_NIIS,
	DMACH_NIR0,
	DMACH_NIR1,
	DMACH_NSPDIF,
	DMACH_NAC97,
	DMACH_NHDMI,//HDMI
	/*DDMA*/
	DMACH_DSRAM,
	DMACH_DSDRAM,
	DMACH_DPATA,
	DMACH_DNAND,
	DMACH_DUSB0,
	DMACH_DEMACR,
	DMACH_DEMACT,
	DMACH_DSSR,
	DMACH_DSST,
	DMACH_TCON0,
	DMACH_TCON1,
	DMACH_HDMIAUDIO,//HDMIAUDIO
	DMACH_DMS,
	DMACH_DSPI0_TX,
	DMACH_DSPI0_RX,
	DMACH_DSPI1_TX,
	DMACH_DSPI1_RX,
	DMACH_DSPI2_TX,
	DMACH_DSPI2_RX,
	DMACH_DSPI3_TX,
	DMACH_DSPI3_RX,
	DMACH_MAX,/* 8 NDMAs, 8 DDMAs */

};
#elif defined CONFIG_ARCH_SUN5I
enum drq_type {	
		DRQ_TYPE_SRAM,
		DRQ_TYPE_SDRAM,		
		DRQ_TYPE_NAND,
		DRQ_TYPE_USB0,
		DRQ_TYPE_EMAC,
		DRQ_TYPE_SPI1,
		DRQ_TYPE_SS,//Security System 
		DRQ_TYPE_MS,//Memory Stick Control
		DRQ_TYPE_SPI0,
		DRQ_TYPE_SPI2,		
		DRQ_TYPE_TCON0,		
		DRQ_TYPE_HDMIAUDIO,
		
		DRQ_TYPE_HDMI,				
		DRQ_TYPE_IR,		
		DRQ_TYPE_SPDIF,
		DRQ_TYPE_IIS,		
		DRQ_TYPE_UART0,
		DRQ_TYPE_UART1,
		DRQ_TYPE_UART2,
		DRQ_TYPE_UART3,
		DRQ_TYPE_AUDIO,
		DRQ_TYPE_TPAD,	
		DRQ_TYPE_USBEP1,
		DRQ_TYPE_USBEP2,
		DRQ_TYPE_USBEP3,
		DRQ_TYPE_USBEP4,
		DRQ_TYPE_USBEP5,
		DRQ_TYPE_MAX,
};
/* We use `virtual` dma channels to hide the fact we have only a limited
 * number of DMA channels, and not of all of them (dependant on the device)
 * can be attached to any DMA source. We therefore let the DMA core handle
 * the allocation of hardware channels to clients.
*/
enum sw_dma_ch {
	/*NDMA*/
	DMACH_NSPI0,
	DMACH_NSPI1,
	DMACH_NSPI2,		
	DMACH_NUART0,
	DMACH_NUART1,
	DMACH_NUART2,
	DMACH_NUART3,	
	DMACH_NSRAM,
	DMACH_NSDRAM,
	DMACH_NTPAD,
	DMACH_NADDA_PLAY,//audio play
	DMACH_NADDA_CAPTURE,//audio capture
	DMACH_NIIS,
	DMACH_NIIS_CAPTURE,
	DMACH_NIR,	
	DMACH_NSPDIF,
	DMACH_NHDMI,//HDMI
	DMACH_NUSBEP1,
	DMACH_NUSBEP2,
	DMACH_NUSBEP3,
	DMACH_NUSBEP4,
	DMACH_NUSBEP5,
	/*DDMA*/	
	DMACH_DSRAM,
	DMACH_DSDRAM,	
	DMACH_DNAND,
	DMACH_DUSB0,
	DMACH_DEMACR,
	DMACH_DEMACT,
	DMACH_DSSR,
	DMACH_DSST,
	DMACH_TCON0,	
	DMACH_HDMIAUDIO,//HDMIAUDIO	
	DMACH_DMS,
	DMACH_DSPI0_TX,
	DMACH_DSPI0_RX,
	DMACH_DSPI1_TX,
	DMACH_DSPI1_RX,
	DMACH_DSPI2_TX,
	DMACH_DSPI2_RX,
	DMACH_MAX,
};
#endif

#endif 

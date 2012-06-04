/*
 * arch/arm/mach-sun5i/dma/dma_regs.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * huangxin <huangxin@allwinnertech.com>
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

#ifndef _DMA_REGS_
#define _DMA_REGS_

/* DMA */
#define SOFTWINNER_DMA_BASE             0x01c02000

/* DMA Register definitions */
#define SW_DMA_DIRQEN      		(0x0000)	//	DMA_IRQ_EN_REG(0x0000)
#define SW_DMA_DIRQPD      		(0x0004)	//	DMA_IRQ_PEND_STA_REG(0x0004)
#define SW_DMA_DCONF       		(0x00)		//	NDMA_CTRL_REG(0x100+N*0x20) and DDMA_CFG_REG(0x300+N*0x20) config
#define SW_DMA_DSRC        		(0x04)		//	NDMA_SRC_ADDR_REG(0x100+N*0x20+4) and DDMA_SRC_START_ADDR_REG(0x300+N*0x20+4)
#define SW_DMA_DDST        		(0x08)		//	NDMA_DEST_ADDR_REG(0x100+N*0x20+8) and DDMA_DEST_START_ADDR_REG(0x300+N*0x20+8)
#define SW_DMA_DCNT        		(0x0C)		//	NDMA_BC_REG(0x100+N*0x20+C) and DDMA_BC_REG(0x300+N*0x20+C)

/* For F23: DDMA parameter register */
#define SW_DMA_DCMBK       		(0x18)		//	DDMA_PARA_REG(0x300+N*0x20+0x18)

/* For F23: NDMA and DDMA */
#define SW_DCONF_LOADING	   	(1<<31)		// 	DMA Loading 				have used

/*NDMA Configuration Register*/
#define SW_NDMA_CONF_CONTI   	(1<<30)		// 	DMA Continuous Mode			have used
#define SW_NDMA_CONF_WAIT  		(7<<27)		// 	DMA Wait Status				not used yet
#define SW_NDMA_CONF_DSTDW 		(3<<25)		// 	destination data width		not used yet
#define SW_NDMA_CONF_DWBYTE    	(0<<25)		//	8-Bit						not used yet
#define SW_NDMA_CONF_DWHWORD   	(1<<25)		//	16-Bit						not used yet
#define SW_NDMA_CONF_DWWORD    	(2<<25)		//	32-Bit						not used yet
#define SW_NDMA_CONF_DSTBL 		(3<<23) 	//	destination burst lenght	not used yet
#define SW_NDMA_CONF_DSTBL0 	(0<<23)		//	1							not used yet
#define SW_NDMA_CONF_DSTBL1 	(1<<23)		//	4							not used yet
#define SW_NDMA_CONF_DSTBL2 	(2<<23)		//	8							not used yet
#define SW_NDMA_CONF_DSTSEC 	(1<<22)		//	DMA Destination Secutity	not used yet
#define SW_NDMA_CONF_DSTAT 		(1<<21)    	// 	destination address type	not used yet
#define SW_NDMA_CONF_DSTTP 		(31<<16)    //	destination DRQ type		not used yet

#define SW_NDMA_CONF_SRCDW 		(3<<9)		//	source data width			not used yet
#define SW_NDMA_CONF_SWBYTE    	(0<<9)		//	8-Bit						not used yet
#define SW_NDMA_CONF_SWHWORD   	(1<<9)		//	16-Bit						not used yet
#define SW_NDMA_CONF_SWWORD    	(2<<9)		//	32-Bit						not used yet
#define SW_NDMA_CONF_SRCBL 		(3<<7)		//	source burst lenght			not used yet
#define SW_NDMA_CONF_SRCBL0 	(0<<7)		//	1							not used yet
#define SW_NDMA_CONF_SRCBL1 	(1<<7)		//	4							not used yet
#define SW_NDMA_CONF_SRCBL2 	(2<<7)		//	8							not used yet
#define SW_NDMA_CONF_SRCSEC 	(1<<6)		//	DMA Source Secutity			not used yet
#define SW_NDMA_CONF_SRCAT 		(1<<5)		//	source address type			not used yet
#define SW_NDMA_CONF_SRCTP 		(31<<0)		//	normal source DRQ type		not used yet

/*DDMA Configuration Register*/
#define SW_DDMA_CONF_BUSY   	(1<<30)		// 	DMA BUSY Mode				not used yet
#define SW_DDMA_CONF_CONTI   	(1<<29)		// 	DMA Continuous Mode			have used
#define SW_DDMA_CONF_DSEC  		(1<<28)		// 	DMA Destination Security	not used yet
#define SW_DDMA_CONF_DSTDW 		(3<<25)		// 	destination data width		not used yet
#define SW_DDMA_CONF_DWBYTE    	(0<<25)		//	8-Bit						not used yet
#define SW_DDMA_CONF_DWHWORD   	(1<<25)		//	16-Bit						not used yet
#define SW_DDMA_CONF_DWWORD    	(2<<25)		//	32-Bit						not used yet
#define SW_DDMA_CONF_DSTBL 		(3<<23) 	//	destination burst lenght	not used yet
#define SW_DDMA_CONF_DSTBL0 	(0<<23)		//	1							not used yet
#define SW_DDMA_CONF_DSTBL1 	(1<<23)		//	4							not used yet
#define SW_DDMA_CONF_DSTBL2 	(2<<23)		//	8							not used yet
#define SW_DDMA_CONF_DSTADDR	(3<<21)    	// 	destination address type	not used yet
#define SW_DDMA_CONF_DSTADDR0	(0<<21)		//	Linear Mode					not used yet
#define SW_DDMA_CONF_DSTADDR1	(1<<21)		//	IO Mode						not used yet
#define SW_DDMA_CONF_DSTADDR2	(2<<21)		//	Horizontal Page Mode		not used yet
#define SW_DDMA_CONF_DSTADDR3	(3<<21)		//	Vertical Page Mode			not used yet
#define SW_DDMA_CONF_DSTTP 		(31<<16)    //	destination DRQ type		not used yet

#define SW_DDMA_CONF_BC			(1<<15)		//	BC mode select				not used yet
#define SW_DDMA_CONF_SSEC		(1<<12)		//	DMA Source Security			not used yet
#define SW_NDMA_CONF_SRCDW 		(3<<9)		//	source data width			not used yet
#define SW_NDMA_CONF_SWBYTE    	(0<<9)		//	8-Bit						not used yet
#define SW_NDMA_CONF_SWHWORD   	(1<<9)		//	16-Bit						not used yet
#define SW_NDMA_CONF_SWWORD    	(2<<9)		//	32-Bit						not used yet
#define SW_NDMA_CONF_SRCBL 		(3<<7)		//	source burst lenght			not used yet
#define SW_NDMA_CONF_SRCBL0 	(0<<7)		//	1							not used yet
#define SW_NDMA_CONF_SRCBL1 	(1<<7)		//	4							not used yet
#define SW_NDMA_CONF_SRCBL2 	(2<<7)		//	8							not used yet
#define SW_NDMA_CONF_SRCADDR 	(3<<5)		//	DMA Source Address Mode		not used yet
#define SW_NDMA_CONF_SRCADDR0 	(0<<5)		//	Linear Mode					not used yet
#define SW_NDMA_CONF_SRCADDR1 	(1<<5)		//	IO Mode						not used yet
#define SW_NDMA_CONF_SRCADDR2 	(2<<5)		//	Horizontal Page Mode		not used yet
#define SW_NDMA_CONF_SRCADDR3 	(3<<5)		//	Vertical Page Mode			not used yet
#define SW_NDMA_CONF_SRCTP 		(31<<0)		//	normal source DRQ type		not used yet

#endif    // #ifndef _DMA_REGS_

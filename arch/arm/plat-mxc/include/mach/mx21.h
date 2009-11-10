/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 * Copyright 2009 Holger Schurig, hs4233@mail.mn-solutions.de
 *
 * This contains i.MX21-specific hardware definitions. For those
 * hardware pieces that are common between i.MX21 and i.MX27, have a
 * look at mx2x.h.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#ifndef __ASM_ARCH_MXC_MX21_H__
#define __ASM_ARCH_MXC_MX21_H__

/* Memory regions and CS */
#define MX21_SDRAM_BASE_ADDR		0xc0000000
#define MX21_CSD1_BASE_ADDR		0xc4000000

#define MX21_CS0_BASE_ADDR		0xc8000000
#define MX21_CS1_BASE_ADDR		0xcc000000
#define MX21_CS2_BASE_ADDR		0xd0000000
#define MX21_CS3_BASE_ADDR		0xd1000000
#define MX21_CS4_BASE_ADDR		0xd2000000
#define MX21_PCMCIA_MEM_BASE_ADDR	0xd4000000
#define MX21_CS5_BASE_ADDR		0xdd000000

/* NAND, SDRAM, WEIM etc controllers */
#define MX21_X_MEMC_BASE_ADDR		0xdf000000
#define MX21_X_MEMC_BASE_ADDR_VIRT	0xf4200000
#define MX21_X_MEMC_SIZE		SZ_256K

#define MX21_SDRAMC_BASE_ADDR		(MX21_X_MEMC_BASE_ADDR + 0x0000)
#define MX21_EIM_BASE_ADDR		(MX21_X_MEMC_BASE_ADDR + 0x1000)
#define MX21_PCMCIA_CTL_BASE_ADDR	(MX21_X_MEMC_BASE_ADDR + 0x2000)
#define MX21_NFC_BASE_ADDR		(MX21_X_MEMC_BASE_ADDR + 0x3000)

#define MX21_IRAM_BASE_ADDR		0xffffe800	/* internal ram */

/* fixed interrupt numbers */
#define MX21_INT_FIRI		9
#define MX21_INT_BMI		30
#define MX21_INT_EMMAENC	49
#define MX21_INT_EMMADEC	50
#define MX21_INT_USBWKUP	53
#define MX21_INT_USBDMA		54
#define MX21_INT_USBHOST	55
#define MX21_INT_USBFUNC	56
#define MX21_INT_USBMNP		57
#define MX21_INT_USBCTRL	58
#define MX21_INT_USBCTRL	58

/* fixed DMA request numbers */
#define MX21_DMA_REQ_FIRI_RX	4
#define MX21_DMA_REQ_BMI_TX	28
#define MX21_DMA_REQ_BMI_RX	29

/* these should go away */
#define SDRAM_BASE_ADDR MX21_SDRAM_BASE_ADDR
#define CSD1_BASE_ADDR MX21_CSD1_BASE_ADDR
#define CS0_BASE_ADDR MX21_CS0_BASE_ADDR
#define CS1_BASE_ADDR MX21_CS1_BASE_ADDR
#define CS2_BASE_ADDR MX21_CS2_BASE_ADDR
#define CS3_BASE_ADDR MX21_CS3_BASE_ADDR
#define CS4_BASE_ADDR MX21_CS4_BASE_ADDR
#define PCMCIA_MEM_BASE_ADDR MX21_PCMCIA_MEM_BASE_ADDR
#define CS5_BASE_ADDR MX21_CS5_BASE_ADDR
#define X_MEMC_BASE_ADDR MX21_X_MEMC_BASE_ADDR
#define X_MEMC_BASE_ADDR_VIRT MX21_X_MEMC_BASE_ADDR_VIRT
#define X_MEMC_SIZE MX21_X_MEMC_SIZE
#define SDRAMC_BASE_ADDR MX21_SDRAMC_BASE_ADDR
#define EIM_BASE_ADDR MX21_EIM_BASE_ADDR
#define PCMCIA_CTL_BASE_ADDR MX21_PCMCIA_CTL_BASE_ADDR
#define NFC_BASE_ADDR MX21_NFC_BASE_ADDR
#define IRAM_BASE_ADDR MX21_IRAM_BASE_ADDR
#define MXC_INT_FIRI MX21_INT_FIRI
#define MXC_INT_BMI MX21_INT_BMI
#define MXC_INT_EMMAENC MX21_INT_EMMAENC
#define MXC_INT_EMMADEC MX21_INT_EMMADEC
#define MXC_INT_USBWKUP MX21_INT_USBWKUP
#define MXC_INT_USBDMA MX21_INT_USBDMA
#define MXC_INT_USBHOST MX21_INT_USBHOST
#define MXC_INT_USBFUNC MX21_INT_USBFUNC
#define MXC_INT_USBMNP MX21_INT_USBMNP
#define MXC_INT_USBCTRL MX21_INT_USBCTRL
#define MXC_INT_USBCTRL MX21_INT_USBCTRL
#define DMA_REQ_FIRI_RX MX21_DMA_REQ_FIRI_RX
#define DMA_REQ_BMI_TX MX21_DMA_REQ_BMI_TX
#define DMA_REQ_BMI_RX MX21_DMA_REQ_BMI_RX

#endif /* __ASM_ARCH_MXC_MX21_H__ */

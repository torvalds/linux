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
#define SDRAM_BASE_ADDR         0xC0000000
#define CSD1_BASE_ADDR          0xC4000000

#define CS0_BASE_ADDR           0xC8000000
#define CS1_BASE_ADDR           0xCC000000
#define CS2_BASE_ADDR           0xD0000000
#define CS3_BASE_ADDR           0xD1000000
#define CS4_BASE_ADDR           0xD2000000
#define CS5_BASE_ADDR           0xDD000000
#define PCMCIA_MEM_BASE_ADDR    0xD4000000

/* NAND, SDRAM, WEIM etc controllers */
#define X_MEMC_BASE_ADDR        0xDF000000
#define X_MEMC_BASE_ADDR_VIRT   0xF4200000
#define X_MEMC_SIZE             SZ_256K

#define SDRAMC_BASE_ADDR        (X_MEMC_BASE_ADDR + 0x0000)
#define EIM_BASE_ADDR           (X_MEMC_BASE_ADDR + 0x1000)
#define PCMCIA_CTL_BASE_ADDR    (X_MEMC_BASE_ADDR + 0x2000)
#define NFC_BASE_ADDR           (X_MEMC_BASE_ADDR + 0x3000)

#define IRAM_BASE_ADDR          0xFFFFE800	/* internal ram */

/* fixed interrupt numbers */
#define MXC_INT_USBCTRL         58
#define MXC_INT_USBCTRL         58
#define MXC_INT_USBMNP          57
#define MXC_INT_USBFUNC         56
#define MXC_INT_USBHOST         55
#define MXC_INT_USBDMA          54
#define MXC_INT_USBWKUP         53
#define MXC_INT_EMMADEC         50
#define MXC_INT_EMMAENC         49
#define MXC_INT_BMI             30
#define MXC_INT_FIRI            9

/* fixed DMA request numbers */
#define DMA_REQ_BMI_RX          29
#define DMA_REQ_BMI_TX          28
#define DMA_REQ_FIRI_RX         4

#endif /* __ASM_ARCH_MXC_MX21_H__ */

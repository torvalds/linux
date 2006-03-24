/*
 * PPC440EP definitions
 *
 * Wade Farnsworth <wfarnsworth@mvista.com>
 *
 * Copyright 2002 Roland Dreier
 * Copyright 2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifdef __KERNEL__
#ifndef __PPC_PLATFORMS_IBM440EP_H
#define __PPC_PLATFORMS_IBM440EP_H

#include <linux/config.h>
#include <asm/ibm44x.h>

/* UART */
#define PPC440EP_UART0_ADDR		0x0EF600300
#define PPC440EP_UART1_ADDR		0x0EF600400
#define PPC440EP_UART2_ADDR		0x0EF600500
#define PPC440EP_UART3_ADDR		0x0EF600600
#define UART0_INT			0
#define UART1_INT			1
#define UART2_INT			3
#define UART3_INT			4

/* Clock and Power Management */
#define IBM_CPM_IIC0		0x80000000	/* IIC interface */
#define IBM_CPM_IIC1		0x40000000	/* IIC interface */
#define IBM_CPM_PCI		0x20000000	/* PCI bridge */
#define IBM_CPM_USB1H		0x08000000	/* USB 1.1 Host */
#define IBM_CPM_FPU		0x04000000	/* floating point unit */
#define IBM_CPM_CPU		0x02000000	/* processor core */
#define IBM_CPM_DMA		0x01000000	/* DMA controller */
#define IBM_CPM_BGO		0x00800000	/* PLB to OPB bus arbiter */
#define IBM_CPM_BGI		0x00400000	/* OPB to PLB bridge */
#define IBM_CPM_EBC		0x00200000	/* External Bus Controller */
#define IBM_CPM_EBM		0x00100000	/* Ext Bus Master Interface */
#define IBM_CPM_DMC		0x00080000	/* SDRAM peripheral controller */
#define IBM_CPM_PLB4		0x00040000	/* PLB4 bus arbiter */
#define IBM_CPM_PLB4x3		0x00020000	/* PLB4 to PLB3 bridge controller */
#define IBM_CPM_PLB3x4		0x00010000	/* PLB3 to PLB4 bridge controller */
#define IBM_CPM_PLB3		0x00008000	/* PLB3 bus arbiter */
#define IBM_CPM_PPM		0x00002000	/* PLB Performance Monitor */
#define IBM_CPM_UIC1		0x00001000	/* Universal Interrupt Controller */
#define IBM_CPM_GPIO0		0x00000800	/* General Purpose IO (??) */
#define IBM_CPM_GPT		0x00000400	/* General Purpose Timers  */
#define IBM_CPM_UART0		0x00000200	/* serial port 0 */
#define IBM_CPM_UART1		0x00000100	/* serial port 1 */
#define IBM_CPM_UIC0		0x00000080	/* Universal Interrupt Controller */
#define IBM_CPM_TMRCLK		0x00000040	/* CPU timers */
#define IBM_CPM_EMAC0		0x00000020	/* ethernet port 0 */
#define IBM_CPM_EMAC1		0x00000010	/* ethernet port 1 */
#define IBM_CPM_UART2		0x00000008	/* serial port 2 */
#define IBM_CPM_UART3		0x00000004	/* serial port 3 */
#define IBM_CPM_USB2D		0x00000002	/* USB 2.0 Device */
#define IBM_CPM_USB2H		0x00000001	/* USB 2.0 Host */

#define DFLT_IBM4xx_PM		~(IBM_CPM_UIC0 | IBM_CPM_UIC1 | IBM_CPM_CPU \
				| IBM_CPM_EBC | IBM_CPM_BGO | IBM_CPM_FPU \
				| IBM_CPM_EBM | IBM_CPM_PLB4 | IBM_CPM_3x4 \
				| IBM_CPM_PLB3 | IBM_CPM_PLB4x3 \
				| IBM_CPM_EMAC0 | IBM_CPM_TMRCLK \
				| IBM_CPM_DMA | IBM_CPM_PCI | IBM_CPM_EMAC1)


#endif /* __PPC_PLATFORMS_IBM440EP_H */
#endif /* __KERNEL__ */

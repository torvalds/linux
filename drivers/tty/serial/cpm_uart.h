/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Driver for CPM (SCC/SMC) serial ports
 *
 *  Copyright (C) 2004 Freescale Semiconductor, Inc.
 *
 *  2006 (c) MontaVista Software, Inc.
 *	Vitaly Bordug <vbordug@ru.mvista.com>
 */
#ifndef CPM_UART_H
#define CPM_UART_H

#include <linux/platform_device.h>

struct gpio_desc;

#if defined(CONFIG_CPM2)
#include "asm/cpm2.h"
#elif defined(CONFIG_CPM1)
#include "asm/cpm1.h"
#endif

#define DPRAM_BASE	((u8 __iomem *)cpm_muram_addr(0))

#define SERIAL_CPM_MAJOR	204
#define SERIAL_CPM_MINOR	46

#define IS_SMC(pinfo)		(pinfo->flags & FLAG_SMC)
#define FLAG_SMC	0x00000002
#define FLAG_CONSOLE	0x00000001

#define UART_NR		6

#define RX_NUM_FIFO	4
#define RX_BUF_SIZE	32
#define TX_NUM_FIFO	4
#define TX_BUF_SIZE	32

#define GPIO_CTS	0
#define GPIO_RTS	1
#define GPIO_DCD	2
#define GPIO_DSR	3
#define GPIO_DTR	4
#define GPIO_RI		5

#define NUM_GPIOS	(GPIO_RI+1)

struct uart_cpm_port {
	struct uart_port	port;
	u16			rx_nrfifos;
	u16			rx_fifosize;
	u16			tx_nrfifos;
	u16			tx_fifosize;
	smc_t __iomem		*smcp;
	smc_uart_t __iomem	*smcup;
	scc_t __iomem		*sccp;
	scc_uart_t __iomem	*sccup;
	cbd_t __iomem		*rx_bd_base;
	cbd_t __iomem		*rx_cur;
	cbd_t __iomem		*tx_bd_base;
	cbd_t __iomem		*tx_cur;
	unsigned char		*tx_buf;
	unsigned char		*rx_buf;
	u32			flags;
	struct clk		*clk;
	u8			brg;
	uint			 dp_addr;
	void			*mem_addr;
	dma_addr_t		 dma_addr;
	u32			mem_size;
	/* wait on close if needed */
	int			wait_closing;
	/* value to combine with opcode to form cpm command */
	u32			command;
	struct gpio_desc	*gpios[NUM_GPIOS];
};

/*
   virtual to phys transtalion
*/
static inline unsigned long cpu2cpm_addr(void *addr,
                                         struct uart_cpm_port *pinfo)
{
	int offset;
	u32 val = (u32)addr;
	u32 mem = (u32)pinfo->mem_addr;
	/* sane check */
	if (likely(val >= mem && val < mem + pinfo->mem_size)) {
		offset = val - mem;
		return pinfo->dma_addr + offset;
	}
	/* something nasty happened */
	BUG();
	return 0;
}

static inline void *cpm2cpu_addr(unsigned long addr,
                                 struct uart_cpm_port *pinfo)
{
	int offset;
	u32 val = addr;
	u32 dma = (u32)pinfo->dma_addr;
	/* sane check */
	if (likely(val >= dma && val < dma + pinfo->mem_size)) {
		offset = val - dma;
		return pinfo->mem_addr + offset;
	}
	/* something nasty happened */
	BUG();
	return NULL;
}


#endif /* CPM_UART_H */

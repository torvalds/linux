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
#include <linux/fs_uart_pd.h>

struct gpio_desc;

#if defined(CONFIG_CPM2)
#include "cpm_uart_cpm2.h"
#elif defined(CONFIG_CPM1)
#include "cpm_uart_cpm1.h"
#endif

#define SERIAL_CPM_MAJOR	204
#define SERIAL_CPM_MINOR	46

#define IS_SMC(pinfo)		(pinfo->flags & FLAG_SMC)
#define IS_DISCARDING(pinfo)	(pinfo->flags & FLAG_DISCARDING)
#define FLAG_DISCARDING	0x00000004	/* when set, don't discard */
#define FLAG_SMC	0x00000002
#define FLAG_CONSOLE	0x00000001

#define UART_SMC1	fsid_smc1_uart
#define UART_SMC2	fsid_smc2_uart
#define UART_SCC1	fsid_scc1_uart
#define UART_SCC2	fsid_scc2_uart
#define UART_SCC3	fsid_scc3_uart
#define UART_SCC4	fsid_scc4_uart

#define UART_NR		fs_uart_nr

#define RX_NUM_FIFO	4
#define RX_BUF_SIZE	32
#define TX_NUM_FIFO	4
#define TX_BUF_SIZE	32

#define SCC_WAIT_CLOSING 100

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

extern int cpm_uart_nr;
extern struct uart_cpm_port cpm_uart_ports[UART_NR];

/* these are located in their respective files */
void cpm_line_cr_cmd(struct uart_cpm_port *port, int cmd);
void __iomem *cpm_uart_map_pram(struct uart_cpm_port *port,
				struct device_node *np);
void cpm_uart_unmap_pram(struct uart_cpm_port *port, void __iomem *pram);
int cpm_uart_init_portdesc(void);
int cpm_uart_allocbuf(struct uart_cpm_port *pinfo, unsigned int is_con);
void cpm_uart_freebuf(struct uart_cpm_port *pinfo);

void smc1_lineif(struct uart_cpm_port *pinfo);
void smc2_lineif(struct uart_cpm_port *pinfo);
void scc1_lineif(struct uart_cpm_port *pinfo);
void scc2_lineif(struct uart_cpm_port *pinfo);
void scc3_lineif(struct uart_cpm_port *pinfo);
void scc4_lineif(struct uart_cpm_port *pinfo);

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

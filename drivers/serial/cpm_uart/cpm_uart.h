/*
 *  linux/drivers/serial/cpm_uart.h
 *
 *  Driver for CPM (SCC/SMC) serial ports
 *
 *  Copyright (C) 2004 Freescale Semiconductor, Inc.
 *
 */
#ifndef CPM_UART_H
#define CPM_UART_H

#include <linux/config.h>

#if defined(CONFIG_CPM2)
#include "cpm_uart_cpm2.h"
#elif defined(CONFIG_8xx)
#include "cpm_uart_cpm1.h"
#endif

#define SERIAL_CPM_MAJOR	204
#define SERIAL_CPM_MINOR	46

#define IS_SMC(pinfo) 		(pinfo->flags & FLAG_SMC)
#define IS_DISCARDING(pinfo)	(pinfo->flags & FLAG_DISCARDING)
#define FLAG_DISCARDING	0x00000004	/* when set, don't discard */
#define FLAG_SMC	0x00000002
#define FLAG_CONSOLE	0x00000001

#define UART_SMC1	0
#define UART_SMC2	1
#define UART_SCC1	2
#define UART_SCC2	3
#define UART_SCC3	4
#define UART_SCC4	5

#define UART_NR	6

#define RX_NUM_FIFO	4
#define RX_BUF_SIZE	32
#define TX_NUM_FIFO	4
#define TX_BUF_SIZE	32

#define SCC_WAIT_CLOSING 100

struct uart_cpm_port {
	struct uart_port	port;
	u16			rx_nrfifos;
	u16			rx_fifosize;
	u16			tx_nrfifos;
	u16			tx_fifosize;
	smc_t			*smcp;
	smc_uart_t		*smcup;
	scc_t			*sccp;
	scc_uart_t		*sccup;
	volatile cbd_t		*rx_bd_base;
	volatile cbd_t		*rx_cur;
	volatile cbd_t		*tx_bd_base;
	volatile cbd_t		*tx_cur;
	unsigned char		*tx_buf;
	unsigned char		*rx_buf;
	u32			flags;
	void			(*set_lineif)(struct uart_cpm_port *);
	u8			brg;
	uint			 dp_addr;
	void			*mem_addr;
	dma_addr_t		 dma_addr;
	/* helpers */
	int			 baud;
	int			 bits;
	/* Keep track of 'odd' SMC2 wirings */
	int			is_portb;
	/* wait on close if needed */
	int 			wait_closing;
};

extern int cpm_uart_port_map[UART_NR];
extern int cpm_uart_nr;
extern struct uart_cpm_port cpm_uart_ports[UART_NR];

/* these are located in their respective files */
void cpm_line_cr_cmd(int line, int cmd);
int cpm_uart_init_portdesc(void);
int cpm_uart_allocbuf(struct uart_cpm_port *pinfo, unsigned int is_con);
void cpm_uart_freebuf(struct uart_cpm_port *pinfo);

void smc1_lineif(struct uart_cpm_port *pinfo);
void smc2_lineif(struct uart_cpm_port *pinfo);
void scc1_lineif(struct uart_cpm_port *pinfo);
void scc2_lineif(struct uart_cpm_port *pinfo);
void scc3_lineif(struct uart_cpm_port *pinfo);
void scc4_lineif(struct uart_cpm_port *pinfo);

#endif /* CPM_UART_H */

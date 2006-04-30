/*
 *  linux/drivers/serial/cpm_uart_cpm2.c
 *
 *  Driver for CPM (SCC/SMC) serial ports; CPM2 definitions
 *
 *  Maintainer: Kumar Gala (galak@kernel.crashing.org) (CPM2)
 *              Pantelis Antoniou (panto@intracom.gr) (CPM1)
 * 
 *  Copyright (C) 2004 Freescale Semiconductor, Inc.
 *            (C) 2004 Intracom, S.A.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/device.h>
#include <linux/bootmem.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <linux/serial_core.h>
#include <linux/kernel.h>

#include "cpm_uart.h"

/**************************************************************/

void cpm_line_cr_cmd(int line, int cmd)
{
	volatile cpm_cpm2_t *cp = cpmp;
	ulong val;

	switch (line) {
	case UART_SMC1:
		val = mk_cr_cmd(CPM_CR_SMC1_PAGE, CPM_CR_SMC1_SBLOCK, 0,
				cmd) | CPM_CR_FLG;
		break;
	case UART_SMC2:
		val = mk_cr_cmd(CPM_CR_SMC2_PAGE, CPM_CR_SMC2_SBLOCK, 0,
				cmd) | CPM_CR_FLG;
		break;
	case UART_SCC1:
		val = mk_cr_cmd(CPM_CR_SCC1_PAGE, CPM_CR_SCC1_SBLOCK, 0,
				cmd) | CPM_CR_FLG;
		break;
	case UART_SCC2:
		val = mk_cr_cmd(CPM_CR_SCC2_PAGE, CPM_CR_SCC2_SBLOCK, 0,
				cmd) | CPM_CR_FLG;
		break;
	case UART_SCC3:
		val = mk_cr_cmd(CPM_CR_SCC3_PAGE, CPM_CR_SCC3_SBLOCK, 0,
				cmd) | CPM_CR_FLG;
		break;
	case UART_SCC4:
		val = mk_cr_cmd(CPM_CR_SCC4_PAGE, CPM_CR_SCC4_SBLOCK, 0,
				cmd) | CPM_CR_FLG;
		break;
	default:
		return;

	}
	cp->cp_cpcr = val;
	while (cp->cp_cpcr & CPM_CR_FLG) ;
}

void smc1_lineif(struct uart_cpm_port *pinfo)
{
	volatile iop_cpm2_t *io = &cpm2_immr->im_ioport;

	/* SMC1 is only on port D */
	io->iop_ppard |= 0x00c00000;
	io->iop_pdird |= 0x00400000;
	io->iop_pdird &= ~0x00800000;
	io->iop_psord &= ~0x00c00000;

	/* Wire BRG1 to SMC1 */
	cpm2_immr->im_cpmux.cmx_smr &= 0x0f;
	pinfo->brg = 1;
}

void smc2_lineif(struct uart_cpm_port *pinfo)
{
	volatile iop_cpm2_t *io = &cpm2_immr->im_ioport;

	/* SMC2 is only on port A */
	io->iop_ppara |= 0x00c00000;
	io->iop_pdira |= 0x00400000;
	io->iop_pdira &= ~0x00800000;
	io->iop_psora &= ~0x00c00000;

	/* Wire BRG2 to SMC2 */
	cpm2_immr->im_cpmux.cmx_smr &= 0xf0;
	pinfo->brg = 2;
}

void scc1_lineif(struct uart_cpm_port *pinfo)
{
	volatile iop_cpm2_t *io = &cpm2_immr->im_ioport;

	/* Use Port D for SCC1 instead of other functions.  */
	io->iop_ppard |= 0x00000003;
	io->iop_psord &= ~0x00000001;	/* Rx */
	io->iop_psord |= 0x00000002;	/* Tx */
	io->iop_pdird &= ~0x00000001;	/* Rx */
	io->iop_pdird |= 0x00000002;	/* Tx */

	/* Wire BRG1 to SCC1 */
	cpm2_immr->im_cpmux.cmx_scr &= 0x00ffffff;
	cpm2_immr->im_cpmux.cmx_scr |= 0x00000000;
	pinfo->brg = 1;
}

void scc2_lineif(struct uart_cpm_port *pinfo)
{
	/*
	 * STx GP3 uses the SCC2 secondary option pin assignment
	 * which this driver doesn't account for in the static
	 * pin assignments. This kind of board specific info
	 * really has to get out of the driver so boards can
	 * be supported in a sane fashion.
	 */
#ifndef CONFIG_STX_GP3
	volatile iop_cpm2_t *io = &cpm2_immr->im_ioport;
	io->iop_pparb |= 0x008b0000;
	io->iop_pdirb |= 0x00880000;
	io->iop_psorb |= 0x00880000;
	io->iop_pdirb &= ~0x00030000;
	io->iop_psorb &= ~0x00030000;
#endif
	cpm2_immr->im_cpmux.cmx_scr &= 0xff00ffff;
	cpm2_immr->im_cpmux.cmx_scr |= 0x00090000;
	pinfo->brg = 2;
}

void scc3_lineif(struct uart_cpm_port *pinfo)
{
	volatile iop_cpm2_t *io = &cpm2_immr->im_ioport;
	io->iop_pparb |= 0x008b0000;
	io->iop_pdirb |= 0x00880000;
	io->iop_psorb |= 0x00880000;
	io->iop_pdirb &= ~0x00030000;
	io->iop_psorb &= ~0x00030000;
	cpm2_immr->im_cpmux.cmx_scr &= 0xffff00ff;
	cpm2_immr->im_cpmux.cmx_scr |= 0x00001200;
	pinfo->brg = 3;
}

void scc4_lineif(struct uart_cpm_port *pinfo)
{
	volatile iop_cpm2_t *io = &cpm2_immr->im_ioport;

	io->iop_ppard |= 0x00000600;
	io->iop_psord &= ~0x00000600;	/* Tx/Rx */
	io->iop_pdird &= ~0x00000200;	/* Rx */
	io->iop_pdird |= 0x00000400;	/* Tx */

	cpm2_immr->im_cpmux.cmx_scr &= 0xffffff00;
	cpm2_immr->im_cpmux.cmx_scr |= 0x0000001b;
	pinfo->brg = 4;
}

/*
 * Allocate DP-Ram and memory buffers. We need to allocate a transmit and 
 * receive buffer descriptors from dual port ram, and a character
 * buffer area from host mem. If we are allocating for the console we need
 * to do it from bootmem
 */
int cpm_uart_allocbuf(struct uart_cpm_port *pinfo, unsigned int is_con)
{
	int dpmemsz, memsz;
	u8 *dp_mem;
	uint dp_offset;
	u8 *mem_addr;
	dma_addr_t dma_addr = 0;

	pr_debug("CPM uart[%d]:allocbuf\n", pinfo->port.line);

	dpmemsz = sizeof(cbd_t) * (pinfo->rx_nrfifos + pinfo->tx_nrfifos);
	dp_offset = cpm_dpalloc(dpmemsz, 8);
	if (IS_DPERR(dp_offset)) {
		printk(KERN_ERR
		       "cpm_uart_cpm.c: could not allocate buffer descriptors\n");
		return -ENOMEM;
	}

	dp_mem = cpm_dpram_addr(dp_offset);

	memsz = L1_CACHE_ALIGN(pinfo->rx_nrfifos * pinfo->rx_fifosize) +
	    L1_CACHE_ALIGN(pinfo->tx_nrfifos * pinfo->tx_fifosize);
	if (is_con) {
		mem_addr = alloc_bootmem(memsz);
		dma_addr = mem_addr;
	}
	else
		mem_addr = dma_alloc_coherent(NULL, memsz, &dma_addr,
					      GFP_KERNEL);

	if (mem_addr == NULL) {
		cpm_dpfree(dp_offset);
		printk(KERN_ERR
		       "cpm_uart_cpm.c: could not allocate coherent memory\n");
		return -ENOMEM;
	}

	pinfo->dp_addr = dp_offset;
	pinfo->mem_addr = mem_addr;
	pinfo->dma_addr = dma_addr;
	pinfo->mem_size = memsz;

	pinfo->rx_buf = mem_addr;
	pinfo->tx_buf = pinfo->rx_buf + L1_CACHE_ALIGN(pinfo->rx_nrfifos
						       * pinfo->rx_fifosize);

	pinfo->rx_bd_base = (volatile cbd_t *)dp_mem;
	pinfo->tx_bd_base = pinfo->rx_bd_base + pinfo->rx_nrfifos;

	return 0;
}

void cpm_uart_freebuf(struct uart_cpm_port *pinfo)
{
	dma_free_coherent(NULL, L1_CACHE_ALIGN(pinfo->rx_nrfifos *
					       pinfo->rx_fifosize) +
			  L1_CACHE_ALIGN(pinfo->tx_nrfifos *
					 pinfo->tx_fifosize), pinfo->mem_addr,
			  pinfo->dma_addr);

	cpm_dpfree(pinfo->dp_addr);
}

/* Setup any dynamic params in the uart desc */
int cpm_uart_init_portdesc(void)
{
	pr_debug("CPM uart[-]:init portdesc\n");

	cpm_uart_nr = 0;
#ifdef CONFIG_SERIAL_CPM_SMC1
	cpm_uart_ports[UART_SMC1].smcp = (smc_t *) & cpm2_immr->im_smc[0];
	cpm_uart_ports[UART_SMC1].smcup =
	    (smc_uart_t *) & cpm2_immr->im_dprambase[PROFF_SMC1];
	*(u16 *)(&cpm2_immr->im_dprambase[PROFF_SMC1_BASE]) = PROFF_SMC1;
	cpm_uart_ports[UART_SMC1].port.mapbase =
	    (unsigned long)&cpm2_immr->im_smc[0];
	cpm_uart_ports[UART_SMC1].smcp->smc_smcm |= (SMCM_RX | SMCM_TX);
	cpm_uart_ports[UART_SMC1].smcp->smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);
	cpm_uart_ports[UART_SMC1].port.uartclk = (((bd_t *) __res)->bi_intfreq);
	cpm_uart_port_map[cpm_uart_nr++] = UART_SMC1;
#endif

#ifdef CONFIG_SERIAL_CPM_SMC2
	cpm_uart_ports[UART_SMC2].smcp = (smc_t *) & cpm2_immr->im_smc[1];
	cpm_uart_ports[UART_SMC2].smcup =
	    (smc_uart_t *) & cpm2_immr->im_dprambase[PROFF_SMC2];
	*(u16 *)(&cpm2_immr->im_dprambase[PROFF_SMC2_BASE]) = PROFF_SMC2;
	cpm_uart_ports[UART_SMC2].port.mapbase =
	    (unsigned long)&cpm2_immr->im_smc[1];
	cpm_uart_ports[UART_SMC2].smcp->smc_smcm |= (SMCM_RX | SMCM_TX);
	cpm_uart_ports[UART_SMC2].smcp->smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);
	cpm_uart_ports[UART_SMC2].port.uartclk = (((bd_t *) __res)->bi_intfreq);
	cpm_uart_port_map[cpm_uart_nr++] = UART_SMC2;
#endif

#ifdef CONFIG_SERIAL_CPM_SCC1
	cpm_uart_ports[UART_SCC1].sccp = (scc_t *) & cpm2_immr->im_scc[0];
	cpm_uart_ports[UART_SCC1].sccup =
	    (scc_uart_t *) & cpm2_immr->im_dprambase[PROFF_SCC1];
	cpm_uart_ports[UART_SCC1].port.mapbase =
	    (unsigned long)&cpm2_immr->im_scc[0];
	cpm_uart_ports[UART_SCC1].sccp->scc_sccm &=
	    ~(UART_SCCM_TX | UART_SCCM_RX);
	cpm_uart_ports[UART_SCC1].sccp->scc_gsmrl &=
	    ~(SCC_GSMRL_ENR | SCC_GSMRL_ENT);
	cpm_uart_ports[UART_SCC1].port.uartclk = (((bd_t *) __res)->bi_intfreq);
	cpm_uart_port_map[cpm_uart_nr++] = UART_SCC1;
#endif

#ifdef CONFIG_SERIAL_CPM_SCC2
	cpm_uart_ports[UART_SCC2].sccp = (scc_t *) & cpm2_immr->im_scc[1];
	cpm_uart_ports[UART_SCC2].sccup =
	    (scc_uart_t *) & cpm2_immr->im_dprambase[PROFF_SCC2];
	cpm_uart_ports[UART_SCC2].port.mapbase =
	    (unsigned long)&cpm2_immr->im_scc[1];
	cpm_uart_ports[UART_SCC2].sccp->scc_sccm &=
	    ~(UART_SCCM_TX | UART_SCCM_RX);
	cpm_uart_ports[UART_SCC2].sccp->scc_gsmrl &=
	    ~(SCC_GSMRL_ENR | SCC_GSMRL_ENT);
	cpm_uart_ports[UART_SCC2].port.uartclk = (((bd_t *) __res)->bi_intfreq);
	cpm_uart_port_map[cpm_uart_nr++] = UART_SCC2;
#endif

#ifdef CONFIG_SERIAL_CPM_SCC3
	cpm_uart_ports[UART_SCC3].sccp = (scc_t *) & cpm2_immr->im_scc[2];
	cpm_uart_ports[UART_SCC3].sccup =
	    (scc_uart_t *) & cpm2_immr->im_dprambase[PROFF_SCC3];
	cpm_uart_ports[UART_SCC3].port.mapbase =
	    (unsigned long)&cpm2_immr->im_scc[2];
	cpm_uart_ports[UART_SCC3].sccp->scc_sccm &=
	    ~(UART_SCCM_TX | UART_SCCM_RX);
	cpm_uart_ports[UART_SCC3].sccp->scc_gsmrl &=
	    ~(SCC_GSMRL_ENR | SCC_GSMRL_ENT);
	cpm_uart_ports[UART_SCC3].port.uartclk = (((bd_t *) __res)->bi_intfreq);
	cpm_uart_port_map[cpm_uart_nr++] = UART_SCC3;
#endif

#ifdef CONFIG_SERIAL_CPM_SCC4
	cpm_uart_ports[UART_SCC4].sccp = (scc_t *) & cpm2_immr->im_scc[3];
	cpm_uart_ports[UART_SCC4].sccup =
	    (scc_uart_t *) & cpm2_immr->im_dprambase[PROFF_SCC4];
	cpm_uart_ports[UART_SCC4].port.mapbase =
	    (unsigned long)&cpm2_immr->im_scc[3];
	cpm_uart_ports[UART_SCC4].sccp->scc_sccm &=
	    ~(UART_SCCM_TX | UART_SCCM_RX);
	cpm_uart_ports[UART_SCC4].sccp->scc_gsmrl &=
	    ~(SCC_GSMRL_ENR | SCC_GSMRL_ENT);
	cpm_uart_ports[UART_SCC4].port.uartclk = (((bd_t *) __res)->bi_intfreq);
	cpm_uart_port_map[cpm_uart_nr++] = UART_SCC4;
#endif

	return 0;
}

/*
 * linux/drivers/serial/cpm_uart_cpm1.h
 *
 * Driver for CPM (SCC/SMC) serial ports
 * 
 * definitions for cpm1
 *
 */

#ifndef CPM_UART_CPM1_H
#define CPM_UART_CPM1_H

#include <asm/commproc.h>

/* defines for IRQs */
#define SMC1_IRQ	(CPM_IRQ_OFFSET + CPMVEC_SMC1)
#define SMC2_IRQ	(CPM_IRQ_OFFSET + CPMVEC_SMC2)
#define SCC1_IRQ	(CPM_IRQ_OFFSET + CPMVEC_SCC1)
#define SCC2_IRQ	(CPM_IRQ_OFFSET + CPMVEC_SCC2)
#define SCC3_IRQ	(CPM_IRQ_OFFSET + CPMVEC_SCC3)
#define SCC4_IRQ	(CPM_IRQ_OFFSET + CPMVEC_SCC4)

/* the CPM address */
#define CPM_ADDR	IMAP_ADDR

static inline void cpm_set_brg(int brg, int baud)
{
	cpm_setbrg(brg, baud);
}

static inline void cpm_set_scc_fcr(volatile scc_uart_t * sup)
{
	sup->scc_genscc.scc_rfcr = SMC_EB;
	sup->scc_genscc.scc_tfcr = SMC_EB;
}

static inline void cpm_set_smc_fcr(volatile smc_uart_t * up)
{
	up->smc_rfcr = SMC_EB;
	up->smc_tfcr = SMC_EB;
}

#define DPRAM_BASE	((unsigned char *)&cpmp->cp_dpmem[0])

#endif

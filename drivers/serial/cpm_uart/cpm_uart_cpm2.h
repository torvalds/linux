/*
 * linux/drivers/serial/cpm_uart_cpm2.h
 *
 * Driver for CPM (SCC/SMC) serial ports
 * 
 * definitions for cpm2
 *
 */

#ifndef CPM_UART_CPM2_H
#define CPM_UART_CPM2_H

#include <asm/cpm2.h>

/* defines for IRQs */
#define SMC1_IRQ	SIU_INT_SMC1
#define SMC2_IRQ	SIU_INT_SMC2
#define SCC1_IRQ	SIU_INT_SCC1
#define SCC2_IRQ	SIU_INT_SCC2
#define SCC3_IRQ	SIU_INT_SCC3
#define SCC4_IRQ	SIU_INT_SCC4

/* the CPM address */
#define CPM_ADDR	CPM_MAP_ADDR

static inline void cpm_set_brg(int brg, int baud)
{
	cpm_setbrg(brg, baud);
}

static inline void cpm_set_scc_fcr(volatile scc_uart_t * sup)
{
	sup->scc_genscc.scc_rfcr = CPMFCR_GBL | CPMFCR_EB;
	sup->scc_genscc.scc_tfcr = CPMFCR_GBL | CPMFCR_EB;
}

static inline void cpm_set_smc_fcr(volatile smc_uart_t * up)
{
	up->smc_rfcr = CPMFCR_GBL | CPMFCR_EB;
	up->smc_tfcr = CPMFCR_GBL | CPMFCR_EB;
}

#define DPRAM_BASE	((unsigned char *)&cpm2_immr->im_dprambase[0])

#endif

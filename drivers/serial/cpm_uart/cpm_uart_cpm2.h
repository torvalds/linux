/*
 * linux/drivers/serial/cpm_uart/cpm_uart_cpm2.h
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
#ifndef CONFIG_PPC_CPM_NEW_BINDING
#define SMC1_IRQ	SIU_INT_SMC1
#define SMC2_IRQ	SIU_INT_SMC2
#define SCC1_IRQ	SIU_INT_SCC1
#define SCC2_IRQ	SIU_INT_SCC2
#define SCC3_IRQ	SIU_INT_SCC3
#define SCC4_IRQ	SIU_INT_SCC4
#endif

static inline void cpm_set_brg(int brg, int baud)
{
	cpm_setbrg(brg, baud);
}

static inline void cpm_set_scc_fcr(scc_uart_t __iomem *sup)
{
	out_8(&sup->scc_genscc.scc_rfcr, CPMFCR_GBL | CPMFCR_EB);
	out_8(&sup->scc_genscc.scc_tfcr, CPMFCR_GBL | CPMFCR_EB);
}

static inline void cpm_set_smc_fcr(smc_uart_t __iomem *up)
{
	out_8(&up->smc_rfcr, CPMFCR_GBL | CPMFCR_EB);
	out_8(&up->smc_tfcr, CPMFCR_GBL | CPMFCR_EB);
}

#define DPRAM_BASE	((u8 __iomem __force *)cpm_dpram_addr(0))

#endif

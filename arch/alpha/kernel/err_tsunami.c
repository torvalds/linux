// SPDX-License-Identifier: GPL-2.0
/* Machine check handling for Tsunami/Typhoon systems. */
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/core_tsunami.h>
#include <asm/smp.h>
#include <asm/err_common.h>
#include <asm/irq_regs.h>
#include "err_impl.h"
#include "proto.h"
#include "pci_impl.h"

static inline void
tsunami_pci_clr_err_1(tsunami_pchip *pchip)
{
	pchip->perror.csr;
	pchip->perror.csr = 0x040;
	mb();
	pchip->perror.csr;
}

static inline void
tsunami_pci_clr_err(void)
{
	tsunami_pci_clr_err_1(TSUNAMI_pchip0);

	/* TSUNAMI and TYPHOON can have 2, but might only have 1 (DS10) */
	if (TSUNAMI_cchip->csc.csr & 1L<<14)
		tsunami_pci_clr_err_1(TSUNAMI_pchip1);
}

void
tsunami_machine_check(unsigned long vector, unsigned long la_ptr)
{
	/* Clear error before any reporting.  */
	mb();
	mb();  /* magic */
	draina();
	tsunami_pci_clr_err();
	wrmces(0x7);
	mb();

	process_mcheck_info(vector, la_ptr, "TSUNAMI",
			    mcheck_expected(smp_processor_id()));
}

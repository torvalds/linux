/*
 * Copyright (C) 2010 Renesas Solutions Corp.
 *
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef __ASM_SH7372_H__
#define __ASM_SH7372_H__

/* DMA slave IDs */
enum {
	SHDMA_SLAVE_INVALID,
	SHDMA_SLAVE_SCIF0_TX,
	SHDMA_SLAVE_SCIF0_RX,
	SHDMA_SLAVE_SCIF1_TX,
	SHDMA_SLAVE_SCIF1_RX,
	SHDMA_SLAVE_SCIF2_TX,
	SHDMA_SLAVE_SCIF2_RX,
	SHDMA_SLAVE_SCIF3_TX,
	SHDMA_SLAVE_SCIF3_RX,
	SHDMA_SLAVE_SCIF4_TX,
	SHDMA_SLAVE_SCIF4_RX,
	SHDMA_SLAVE_SCIF5_TX,
	SHDMA_SLAVE_SCIF5_RX,
	SHDMA_SLAVE_SCIF6_TX,
	SHDMA_SLAVE_SCIF6_RX,
	SHDMA_SLAVE_FLCTL0_TX,
	SHDMA_SLAVE_FLCTL0_RX,
	SHDMA_SLAVE_FLCTL1_TX,
	SHDMA_SLAVE_FLCTL1_RX,
	SHDMA_SLAVE_SDHI0_RX,
	SHDMA_SLAVE_SDHI0_TX,
	SHDMA_SLAVE_SDHI1_RX,
	SHDMA_SLAVE_SDHI1_TX,
	SHDMA_SLAVE_SDHI2_RX,
	SHDMA_SLAVE_SDHI2_TX,
	SHDMA_SLAVE_FSIA_RX,
	SHDMA_SLAVE_FSIA_TX,
	SHDMA_SLAVE_MMCIF_RX,
	SHDMA_SLAVE_MMCIF_TX,
	SHDMA_SLAVE_USB0_TX,
	SHDMA_SLAVE_USB0_RX,
	SHDMA_SLAVE_USB1_TX,
	SHDMA_SLAVE_USB1_RX,
};

extern struct clk sh7372_extal1_clk;
extern struct clk sh7372_extal2_clk;
extern struct clk sh7372_dv_clki_clk;
extern struct clk sh7372_dv_clki_div2_clk;
extern struct clk sh7372_pllc2_clk;

extern void sh7372_init_irq(void);
extern void sh7372_map_io(void);
extern void sh7372_earlytimer_init(void);
extern void sh7372_add_early_devices(void);
extern void sh7372_add_standard_devices(void);
extern void sh7372_add_early_devices_dt(void);
extern void sh7372_add_standard_devices_dt(void);
extern void sh7372_clock_init(void);
extern void sh7372_pinmux_init(void);
extern void sh7372_pm_init(void);
extern void sh7372_resume_core_standby_sysc(void);
extern int  sh7372_do_idle_sysc(unsigned long sleep_mode);
extern void sh7372_intcs_suspend(void);
extern void sh7372_intcs_resume(void);
extern void sh7372_intca_suspend(void);
extern void sh7372_intca_resume(void);

extern unsigned long sh7372_cpu_resume;

#ifdef CONFIG_PM
extern void __init sh7372_init_pm_domains(void);
#else
static inline void sh7372_init_pm_domains(void) {}
#endif

extern void __init sh7372_pm_init_late(void);

#endif /* __ASM_SH7372_H__ */

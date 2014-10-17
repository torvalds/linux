#ifndef __ASM_SH73A0_H__
#define __ASM_SH73A0_H__

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
	SHDMA_SLAVE_SCIF7_TX,
	SHDMA_SLAVE_SCIF7_RX,
	SHDMA_SLAVE_SCIF8_TX,
	SHDMA_SLAVE_SCIF8_RX,
	SHDMA_SLAVE_SDHI0_TX,
	SHDMA_SLAVE_SDHI0_RX,
	SHDMA_SLAVE_SDHI1_TX,
	SHDMA_SLAVE_SDHI1_RX,
	SHDMA_SLAVE_SDHI2_TX,
	SHDMA_SLAVE_SDHI2_RX,
	SHDMA_SLAVE_MMCIF_TX,
	SHDMA_SLAVE_MMCIF_RX,
	SHDMA_SLAVE_FSI2A_TX,
	SHDMA_SLAVE_FSI2A_RX,
	SHDMA_SLAVE_FSI2B_TX,
	SHDMA_SLAVE_FSI2B_RX,
	SHDMA_SLAVE_FSI2C_TX,
	SHDMA_SLAVE_FSI2C_RX,
	SHDMA_SLAVE_FSI2D_RX,
};

/*
 *		SH73A0 IRQ LOCATION TABLE
 *
 * 416	-----------------------------------------
 *		IRQ0-IRQ15
 * 431	-----------------------------------------
 * ...
 * 448	-----------------------------------------
 *		sh73a0-intcs
 *		sh73a0-intca-irq-pins
 * 680	-----------------------------------------
 * ...
 * 700	-----------------------------------------
 *		sh73a0-pint0
 * 731	-----------------------------------------
 * 732	-----------------------------------------
 *		sh73a0-pint1
 * 739	-----------------------------------------
 * ...
 * 800	-----------------------------------------
 *		IRQ16-IRQ31
 * 815	-----------------------------------------
 * ...
 * 928	-----------------------------------------
 *		sh73a0-intca-irq-pins
 * 943	-----------------------------------------
 */

/* PINT interrupts are located at Linux IRQ 700 and up */
#define SH73A0_PINT0_IRQ(irq) ((irq) + 700)
#define SH73A0_PINT1_IRQ(irq) ((irq) + 732)

extern void sh73a0_init_irq(void);
extern void sh73a0_init_irq_dt(void);
extern void sh73a0_map_io(void);
extern void sh73a0_earlytimer_init(void);
extern void sh73a0_add_early_devices(void);
extern void sh73a0_add_standard_devices(void);
extern void sh73a0_add_standard_devices_dt(void);
extern void sh73a0_clock_init(void);
extern void sh73a0_pinmux_init(void);
extern void sh73a0_pm_init(void);
extern struct clk sh73a0_extal1_clk;
extern struct clk sh73a0_extal2_clk;
extern struct clk sh73a0_extcki_clk;
extern struct clk sh73a0_extalr_clk;
extern struct smp_operations sh73a0_smp_ops;

#endif /* __ASM_SH73A0_H__ */

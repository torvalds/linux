/*
 * arch/arm/mach-sa1100/include/mach/ide.h
 *
 * Copyright (c) 1998 Hugo Fiennes & Nicolas Pitre
 *
 * 18-aug-2000: Cleanup by Erik Mouw (J.A.K.Mouw@its.tudelft.nl)
 *              Get rid of the special ide_init_hwif_ports() functions
 *              and make a generalised function that can be used by all
 *              architectures.
 */

#include <asm/irq.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>

#error "This code is broken and needs update to match with current ide support"


/*
 * Set up a hw structure for a specified data port, control port and IRQ.
 * This should follow whatever the default interface uses.
 */
static inline void ide_init_hwif_ports(hw_regs_t *hw, unsigned long data_port,
				       unsigned long ctrl_port, int *irq)
{
	unsigned long reg = data_port;
	int i;
	int regincr = 1;

	/* The Empeg board has the first two address lines unused */
	if (machine_is_empeg())
		regincr = 1 << 2;

	/* The LART doesn't use A0 for IDE */
	if (machine_is_lart())
		regincr = 1 << 1;

	memset(hw, 0, sizeof(*hw));

	for (i = 0; i <= 7; i++) {
		hw->io_ports_array[i] = reg;
		reg += regincr;
	}

	hw->io_ports.ctl_addr = ctrl_port;

	if (irq)
		*irq = 0;
}

/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
static __inline__ void
ide_init_default_hwifs(void)
{
    if (machine_is_lart()) {
#ifdef CONFIG_SA1100_LART
        hw_regs_t hw;

        /* Enable GPIO as interrupt line */
        GPDR &= ~LART_GPIO_IDE;
	set_irq_type(LART_IRQ_IDE, IRQ_TYPE_EDGE_RISING);

        /* set PCMCIA interface timing */
        MECR = 0x00060006;

        /* init the interface */
	ide_init_hwif_ports(&hw, PCMCIA_IO_0_BASE + 0x0000, PCMCIA_IO_0_BASE + 0x1000, NULL);
        hw.irq = LART_IRQ_IDE;
        ide_register_hw(&hw);
#endif
    }
}

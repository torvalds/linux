/*
 * June 2006 steve.glendinning@smsc.com
 *
 * Polaris-specific resource declaration
 *
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/smsc911x.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/machvec.h>
#include <asm/heartbeat.h>
#include <cpu/gpio.h>
#include <mach-se/mach/se.h>

#define BCR2		(0xFFFFFF62)
#define WCR2		(0xFFFFFF66)
#define AREA5_WAIT_CTRL	(0x1C00)
#define WAIT_STATES_10	(0x7)

static struct resource smsc911x_resources[] = {
	[0] = {
		.name		= "smsc911x-memory",
		.start		= PA_EXT5,
		.end		= PA_EXT5 + 0x1fff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.name		= "smsc911x-irq",
		.start		= IRQ0_IRQ,
		.end		= IRQ0_IRQ,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc911x_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags		= SMSC911X_USE_32BIT,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct platform_device smsc911x_device = {
	.name		= "smsc911x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
	.resource	= smsc911x_resources,
	.dev = {
		.platform_data = &smsc911x_config,
	},
};

static unsigned char heartbeat_bit_pos[] = { 0, 1, 2, 3 };

static struct heartbeat_data heartbeat_data = {
	.bit_pos	= heartbeat_bit_pos,
	.nr_bits	= ARRAY_SIZE(heartbeat_bit_pos),
};

static struct resource heartbeat_resource = {
	.start	= PORT_PCDR,
	.end	= PORT_PCDR,
	.flags	= IORESOURCE_MEM | IORESOURCE_MEM_8BIT,
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.dev	= {
		.platform_data	= &heartbeat_data,
	},
	.num_resources	= 1,
	.resource	= &heartbeat_resource,
};

static struct platform_device *polaris_devices[] __initdata = {
	&smsc911x_device,
	&heartbeat_device,
};

static int __init polaris_initialise(void)
{
	u16 wcr, bcr_mask;

	printk(KERN_INFO "Configuring Polaris external bus\n");

	/* Configure area 5 with 2 wait states */
	wcr = __raw_readw(WCR2);
	wcr &= (~AREA5_WAIT_CTRL);
	wcr |= (WAIT_STATES_10 << 10);
	__raw_writew(wcr, WCR2);

	/* Configure area 5 for 32-bit access */
	bcr_mask = __raw_readw(BCR2);
	bcr_mask |= 1 << 10;
	__raw_writew(bcr_mask, BCR2);

	return platform_add_devices(polaris_devices,
				    ARRAY_SIZE(polaris_devices));
}
arch_initcall(polaris_initialise);

static struct ipr_data ipr_irq_table[] = {
	/* External IRQs */
	{ IRQ0_IRQ, 0,  0,  1, },	/* IRQ0 */
	{ IRQ1_IRQ, 0,  4,  1, },	/* IRQ1 */
};

static unsigned long ipr_offsets[] = {
	INTC_IPRC
};

static struct ipr_desc ipr_irq_desc = {
	.ipr_offsets	= ipr_offsets,
	.nr_offsets	= ARRAY_SIZE(ipr_offsets),

	.ipr_data	= ipr_irq_table,
	.nr_irqs	= ARRAY_SIZE(ipr_irq_table),
	.chip = {
		.name	= "sh7709-ext",
	},
};

static void __init init_polaris_irq(void)
{
	/* Disable all interrupts */
	__raw_writew(0, BCR_ILCRA);
	__raw_writew(0, BCR_ILCRB);
	__raw_writew(0, BCR_ILCRC);
	__raw_writew(0, BCR_ILCRD);
	__raw_writew(0, BCR_ILCRE);
	__raw_writew(0, BCR_ILCRF);
	__raw_writew(0, BCR_ILCRG);

	register_ipr_controller(&ipr_irq_desc);
}

static struct sh_machine_vector mv_polaris __initmv = {
	.mv_name		= "Polaris",
	.mv_nr_irqs		= 61,
	.mv_init_irq		= init_polaris_irq,
};

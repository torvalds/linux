/*
 * RM200 specific code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006,2007 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 *
 * i8259 parts ripped out of arch/mips/kernel/i8259.c
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/io.h>

#include <asm/sni.h>
#include <asm/time.h>
#include <asm/irq_cpu.h>

#define RM200_I8259A_IRQ_BASE 32

#define MEMPORT(_base,_irq)				\
	{						\
		.mapbase	= _base,		\
		.irq		= _irq,			\
		.uartclk	= 1843200,		\
		.iotype		= UPIO_MEM,		\
		.flags		= UPF_BOOT_AUTOCONF|UPF_IOREMAP, \
	}

static struct plat_serial8250_port rm200_data[] = {
	MEMPORT(0x160003f8, RM200_I8259A_IRQ_BASE + 4),
	MEMPORT(0x160002f8, RM200_I8259A_IRQ_BASE + 3),
	{ },
};

static struct platform_device rm200_serial8250_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= rm200_data,
	},
};

static struct resource rm200_ds1216_rsrc[] = {
        {
                .start = 0x1cd41ffc,
                .end   = 0x1cd41fff,
                .flags = IORESOURCE_MEM
        }
};

static struct platform_device rm200_ds1216_device = {
        .name           = "rtc-ds1216",
        .num_resources  = ARRAY_SIZE(rm200_ds1216_rsrc),
        .resource       = rm200_ds1216_rsrc
};

static struct resource snirm_82596_rm200_rsrc[] = {
	{
		.start = 0x18000000,
		.end   = 0x180fffff,
		.flags = IORESOURCE_MEM
	},
	{
		.start = 0x1b000000,
		.end   = 0x1b000004,
		.flags = IORESOURCE_MEM
	},
	{
		.start = 0x1ff00000,
		.end   = 0x1ff00020,
		.flags = IORESOURCE_MEM
	},
	{
		.start = 27,
		.end   = 27,
		.flags = IORESOURCE_IRQ
	},
	{
		.flags = 0x00
	}
};

static struct platform_device snirm_82596_rm200_pdev = {
	.name           = "snirm_82596",
	.num_resources  = ARRAY_SIZE(snirm_82596_rm200_rsrc),
	.resource       = snirm_82596_rm200_rsrc
};

static struct resource snirm_53c710_rm200_rsrc[] = {
	{
		.start = 0x19000000,
		.end   = 0x190fffff,
		.flags = IORESOURCE_MEM
	},
	{
		.start = 26,
		.end   = 26,
		.flags = IORESOURCE_IRQ
	}
};

static struct platform_device snirm_53c710_rm200_pdev = {
	.name           = "snirm_53c710",
	.num_resources  = ARRAY_SIZE(snirm_53c710_rm200_rsrc),
	.resource       = snirm_53c710_rm200_rsrc
};

static int __init snirm_setup_devinit(void)
{
	if (sni_brd_type == SNI_BRD_RM200) {
		platform_device_register(&rm200_serial8250_device);
		platform_device_register(&rm200_ds1216_device);
		platform_device_register(&snirm_82596_rm200_pdev);
		platform_device_register(&snirm_53c710_rm200_pdev);
		sni_eisa_root_init();
	}
	return 0;
}

device_initcall(snirm_setup_devinit);

/*
 * RM200 has an ISA and an EISA bus. The iSA bus is only used
 * for onboard devices and also has twi i8259 PICs. Since these
 * PICs are no accessible via inb/outb the following code uses
 * readb/writeb to access them
 */

DEFINE_SPINLOCK(sni_rm200_i8259A_lock);
#define PIC_CMD    0x00
#define PIC_IMR    0x01
#define PIC_ISR    PIC_CMD
#define PIC_POLL   PIC_ISR
#define PIC_OCW3   PIC_ISR

/* i8259A PIC related value */
#define PIC_CASCADE_IR		2
#define MASTER_ICW4_DEFAULT	0x01
#define SLAVE_ICW4_DEFAULT	0x01

/*
 * This contains the irq mask for both 8259A irq controllers,
 */
static unsigned int rm200_cached_irq_mask = 0xffff;
static __iomem u8 *rm200_pic_master;
static __iomem u8 *rm200_pic_slave;

#define cached_master_mask	(rm200_cached_irq_mask)
#define cached_slave_mask	(rm200_cached_irq_mask >> 8)

static void sni_rm200_disable_8259A_irq(unsigned int irq)
{
	unsigned int mask;
	unsigned long flags;

	irq -= RM200_I8259A_IRQ_BASE;
	mask = 1 << irq;
	spin_lock_irqsave(&sni_rm200_i8259A_lock, flags);
	rm200_cached_irq_mask |= mask;
	if (irq & 8)
		writeb(cached_slave_mask, rm200_pic_slave + PIC_IMR);
	else
		writeb(cached_master_mask, rm200_pic_master + PIC_IMR);
	spin_unlock_irqrestore(&sni_rm200_i8259A_lock, flags);
}

static void sni_rm200_enable_8259A_irq(unsigned int irq)
{
	unsigned int mask;
	unsigned long flags;

	irq -= RM200_I8259A_IRQ_BASE;
	mask = ~(1 << irq);
	spin_lock_irqsave(&sni_rm200_i8259A_lock, flags);
	rm200_cached_irq_mask &= mask;
	if (irq & 8)
		writeb(cached_slave_mask, rm200_pic_slave + PIC_IMR);
	else
		writeb(cached_master_mask, rm200_pic_master + PIC_IMR);
	spin_unlock_irqrestore(&sni_rm200_i8259A_lock, flags);
}

static inline int sni_rm200_i8259A_irq_real(unsigned int irq)
{
	int value;
	int irqmask = 1 << irq;

	if (irq < 8) {
		writeb(0x0B, rm200_pic_master + PIC_CMD);
		value = readb(rm200_pic_master + PIC_CMD) & irqmask;
		writeb(0x0A, rm200_pic_master + PIC_CMD);
		return value;
	}
	writeb(0x0B, rm200_pic_slave + PIC_CMD); /* ISR register */
	value = readb(rm200_pic_slave + PIC_CMD) & (irqmask >> 8);
	writeb(0x0A, rm200_pic_slave + PIC_CMD);
	return value;
}

/*
 * Careful! The 8259A is a fragile beast, it pretty
 * much _has_ to be done exactly like this (mask it
 * first, _then_ send the EOI, and the order of EOI
 * to the two 8259s is important!
 */
void sni_rm200_mask_and_ack_8259A(unsigned int irq)
{
	unsigned int irqmask;
	unsigned long flags;

	irq -= RM200_I8259A_IRQ_BASE;
	irqmask = 1 << irq;
	spin_lock_irqsave(&sni_rm200_i8259A_lock, flags);
	/*
	 * Lightweight spurious IRQ detection. We do not want
	 * to overdo spurious IRQ handling - it's usually a sign
	 * of hardware problems, so we only do the checks we can
	 * do without slowing down good hardware unnecessarily.
	 *
	 * Note that IRQ7 and IRQ15 (the two spurious IRQs
	 * usually resulting from the 8259A-1|2 PICs) occur
	 * even if the IRQ is masked in the 8259A. Thus we
	 * can check spurious 8259A IRQs without doing the
	 * quite slow i8259A_irq_real() call for every IRQ.
	 * This does not cover 100% of spurious interrupts,
	 * but should be enough to warn the user that there
	 * is something bad going on ...
	 */
	if (rm200_cached_irq_mask & irqmask)
		goto spurious_8259A_irq;
	rm200_cached_irq_mask |= irqmask;

handle_real_irq:
	if (irq & 8) {
		readb(rm200_pic_slave + PIC_IMR);
		writeb(cached_slave_mask, rm200_pic_slave + PIC_IMR);
		writeb(0x60+(irq & 7), rm200_pic_slave + PIC_CMD);
		writeb(0x60+PIC_CASCADE_IR, rm200_pic_master + PIC_CMD);
	} else {
		readb(rm200_pic_master + PIC_IMR);
		writeb(cached_master_mask, rm200_pic_master + PIC_IMR);
		writeb(0x60+irq, rm200_pic_master + PIC_CMD);
	}
	spin_unlock_irqrestore(&sni_rm200_i8259A_lock, flags);
	return;

spurious_8259A_irq:
	/*
	 * this is the slow path - should happen rarely.
	 */
	if (sni_rm200_i8259A_irq_real(irq))
		/*
		 * oops, the IRQ _is_ in service according to the
		 * 8259A - not spurious, go handle it.
		 */
		goto handle_real_irq;

	{
		static int spurious_irq_mask;
		/*
		 * At this point we can be sure the IRQ is spurious,
		 * lets ACK and report it. [once per IRQ]
		 */
		if (!(spurious_irq_mask & irqmask)) {
			printk(KERN_DEBUG
			       "spurious RM200 8259A interrupt: IRQ%d.\n", irq);
			spurious_irq_mask |= irqmask;
		}
		atomic_inc(&irq_err_count);
		/*
		 * Theoretically we do not have to handle this IRQ,
		 * but in Linux this does not cause problems and is
		 * simpler for us.
		 */
		goto handle_real_irq;
	}
}

static struct irq_chip sni_rm200_i8259A_chip = {
	.name		= "RM200-XT-PIC",
	.mask		= sni_rm200_disable_8259A_irq,
	.unmask		= sni_rm200_enable_8259A_irq,
	.mask_ack	= sni_rm200_mask_and_ack_8259A,
};

/*
 * Do the traditional i8259 interrupt polling thing.  This is for the few
 * cases where no better interrupt acknowledge method is available and we
 * absolutely must touch the i8259.
 */
static inline int sni_rm200_i8259_irq(void)
{
	int irq;

	spin_lock(&sni_rm200_i8259A_lock);

	/* Perform an interrupt acknowledge cycle on controller 1. */
	writeb(0x0C, rm200_pic_master + PIC_CMD);	/* prepare for poll */
	irq = readb(rm200_pic_master + PIC_CMD) & 7;
	if (irq == PIC_CASCADE_IR) {
		/*
		 * Interrupt is cascaded so perform interrupt
		 * acknowledge on controller 2.
		 */
		writeb(0x0C, rm200_pic_slave + PIC_CMD); /* prepare for poll */
		irq = (readb(rm200_pic_slave + PIC_CMD) & 7) + 8;
	}

	if (unlikely(irq == 7)) {
		/*
		 * This may be a spurious interrupt.
		 *
		 * Read the interrupt status register (ISR). If the most
		 * significant bit is not set then there is no valid
		 * interrupt.
		 */
		writeb(0x0B, rm200_pic_master + PIC_ISR); /* ISR register */
		if (~readb(rm200_pic_master + PIC_ISR) & 0x80)
			irq = -1;
	}

	spin_unlock(&sni_rm200_i8259A_lock);

	return likely(irq >= 0) ? irq + RM200_I8259A_IRQ_BASE : irq;
}

void sni_rm200_init_8259A(void)
{
	unsigned long flags;

	spin_lock_irqsave(&sni_rm200_i8259A_lock, flags);

	writeb(0xff, rm200_pic_master + PIC_IMR);
	writeb(0xff, rm200_pic_slave + PIC_IMR);

	writeb(0x11, rm200_pic_master + PIC_CMD);
	writeb(0, rm200_pic_master + PIC_IMR);
	writeb(1U << PIC_CASCADE_IR, rm200_pic_master + PIC_IMR);
	writeb(MASTER_ICW4_DEFAULT, rm200_pic_master + PIC_IMR);
	writeb(0x11, rm200_pic_slave + PIC_CMD);
	writeb(8, rm200_pic_slave + PIC_IMR);
	writeb(PIC_CASCADE_IR, rm200_pic_slave + PIC_IMR);
	writeb(SLAVE_ICW4_DEFAULT, rm200_pic_slave + PIC_IMR);
	udelay(100);		/* wait for 8259A to initialize */

	writeb(cached_master_mask, rm200_pic_master + PIC_IMR);
	writeb(cached_slave_mask, rm200_pic_slave + PIC_IMR);

	spin_unlock_irqrestore(&sni_rm200_i8259A_lock, flags);
}

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction sni_rm200_irq2 = {
	.handler = no_action,
	.name = "cascade",
};

static struct resource sni_rm200_pic1_resource = {
	.name = "onboard ISA pic1",
	.start = 0x16000020,
	.end = 0x16000023,
	.flags = IORESOURCE_BUSY
};

static struct resource sni_rm200_pic2_resource = {
	.name = "onboard ISA pic2",
	.start = 0x160000a0,
	.end = 0x160000a3,
	.flags = IORESOURCE_BUSY
};

/* ISA irq handler */
static irqreturn_t sni_rm200_i8259A_irq_handler(int dummy, void *p)
{
	int irq;

	irq = sni_rm200_i8259_irq();
	if (unlikely(irq < 0))
		return IRQ_NONE;

	do_IRQ(irq);
	return IRQ_HANDLED;
}

struct irqaction sni_rm200_i8259A_irq = {
	.handler = sni_rm200_i8259A_irq_handler,
	.name = "onboard ISA",
	.flags = IRQF_SHARED
};

void __init sni_rm200_i8259_irqs(void)
{
	int i;

	rm200_pic_master = ioremap_nocache(0x16000020, 4);
	if (!rm200_pic_master)
		return;
	rm200_pic_slave = ioremap_nocache(0x160000a0, 4);
	if (!rm200_pic_master) {
		iounmap(rm200_pic_master);
		return;
	}

	insert_resource(&iomem_resource, &sni_rm200_pic1_resource);
	insert_resource(&iomem_resource, &sni_rm200_pic2_resource);

	sni_rm200_init_8259A();

	for (i = RM200_I8259A_IRQ_BASE; i < RM200_I8259A_IRQ_BASE + 16; i++)
		set_irq_chip_and_handler(i, &sni_rm200_i8259A_chip,
					 handle_level_irq);

	setup_irq(RM200_I8259A_IRQ_BASE + PIC_CASCADE_IR, &sni_rm200_irq2);
}


#define SNI_RM200_INT_STAT_REG  CKSEG1ADDR(0xbc000000)
#define SNI_RM200_INT_ENA_REG   CKSEG1ADDR(0xbc080000)

#define SNI_RM200_INT_START  24
#define SNI_RM200_INT_END    28

static void enable_rm200_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq - SNI_RM200_INT_START);

	*(volatile u8 *)SNI_RM200_INT_ENA_REG &= ~mask;
}

void disable_rm200_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq - SNI_RM200_INT_START);

	*(volatile u8 *)SNI_RM200_INT_ENA_REG |= mask;
}

void end_rm200_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_rm200_irq(irq);
}

static struct irq_chip rm200_irq_type = {
	.typename = "RM200",
	.ack = disable_rm200_irq,
	.mask = disable_rm200_irq,
	.mask_ack = disable_rm200_irq,
	.unmask = enable_rm200_irq,
	.end = end_rm200_irq,
};

static void sni_rm200_hwint(void)
{
	u32 pending = read_c0_cause() & read_c0_status();
	u8 mask;
	u8 stat;
	int irq;

	if (pending & C_IRQ5)
		do_IRQ(MIPS_CPU_IRQ_BASE + 7);
	else if (pending & C_IRQ0) {
		clear_c0_status(IE_IRQ0);
		mask = *(volatile u8 *)SNI_RM200_INT_ENA_REG ^ 0x1f;
		stat = *(volatile u8 *)SNI_RM200_INT_STAT_REG ^ 0x14;
		irq = ffs(stat & mask & 0x1f);

		if (likely(irq > 0))
			do_IRQ(irq + SNI_RM200_INT_START - 1);
		set_c0_status(IE_IRQ0);
	}
}

void __init sni_rm200_irq_init(void)
{
	int i;

	* (volatile u8 *)SNI_RM200_INT_ENA_REG = 0x1f;

	sni_rm200_i8259_irqs();
	mips_cpu_irq_init();
	/* Actually we've got more interrupts to handle ...  */
	for (i = SNI_RM200_INT_START; i <= SNI_RM200_INT_END; i++)
		set_irq_chip_and_handler(i, &rm200_irq_type, handle_level_irq);
	sni_hwint = sni_rm200_hwint;
	change_c0_status(ST0_IM, IE_IRQ0);
	setup_irq(SNI_RM200_INT_START + 0, &sni_rm200_i8259A_irq);
	setup_irq(SNI_RM200_INT_START + 1, &sni_isa_irq);
}

void __init sni_rm200_init(void)
{
}

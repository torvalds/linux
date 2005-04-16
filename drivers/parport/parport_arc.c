/* Low-level parallel port routines for Archimedes onboard hardware
 *
 * Author: Phil Blundell <philb@gnu.org>
 */

/* This driver is for the parallel port hardware found on Acorn's old
 * range of Archimedes machines.  The A5000 and newer systems have PC-style
 * I/O hardware and should use the parport_pc driver instead.
 *
 * The Acorn printer port hardware is very simple.  There is a single 8-bit
 * write-only latch for the data port and control/status bits are handled
 * with various auxilliary input and output lines.  The port is not
 * bidirectional, does not support any modes other than SPP, and has only
 * a subset of the standard printer control lines connected.
 */

#include <linux/threads.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/parport.h>

#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/arch/oldlatches.h>
#include <asm/arch/irqs.h>

#define DATA_ADDRESS    0x3350010

/* This is equivalent to the above and only used for request_region. */
#define PORT_BASE       0x80000000 | ((DATA_ADDRESS - IO_BASE) >> 2)

/* The hardware can't read from the data latch, so we must use a soft
   copy. */
static unsigned char data_copy;

/* These are pretty simple. We know the irq is never shared and the
   kernel does all the magic that's required. */
static void arc_enable_irq(struct parport *p)
{
	enable_irq(p->irq);
}

static void arc_disable_irq(struct parport *p)
{
	disable_irq(p->irq);
}

static void arc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	parport_generic_irq(irq, (struct parport *) dev_id, regs);
}

static void arc_write_data(struct parport *p, unsigned char data)
{
	data_copy = data;
	outb_t(data, DATA_LATCH);
}

static unsigned char arc_read_data(struct parport *p)
{
	return data_copy;
}

static struct parport_operations parport_arc_ops = 
{
	.write_data	= arc_write_data,
	.read_data	= arc_read_data,

	.write_control	= arc_write_control,
	.read_control	= arc_read_control,
	.frob_control	= arc_frob_control,

	.read_status	= arc_read_status,

	.enable_irq	= arc_enable_irq,
	.disable_irq	= arc_disable_irq,

	.data_forward	= arc_data_forward,
	.data_reverse	= arc_data_reverse,

	.init_state	= arc_init_state,
	.save_state	= arc_save_state,
	.restore_state	= arc_restore_state,

	.epp_write_data	= parport_ieee1284_epp_write_data,
	.epp_read_data	= parport_ieee1284_epp_read_data,
	.epp_write_addr	= parport_ieee1284_epp_write_addr,
	.epp_read_addr	= parport_ieee1284_epp_read_addr,

	.ecp_write_data	= parport_ieee1284_ecp_write_data,
	.ecp_read_data	= parport_ieee1284_ecp_read_data,
	.ecp_write_addr	= parport_ieee1284_ecp_write_addr,
	
	.compat_write_data	= parport_ieee1284_write_compat,
	.nibble_read_data	= parport_ieee1284_read_nibble,
	.byte_read_data		= parport_ieee1284_read_byte,

	.owner		= THIS_MODULE,
};

/* --- Initialisation code -------------------------------- */

static int parport_arc_init(void)
{
	/* Archimedes hardware provides only one port, at a fixed address */
	struct parport *p;
	struct resource res;
	char *fake_name = "parport probe");

	res = request_region(PORT_BASE, 1, fake_name);
	if (res == NULL)
		return 0;

	p = parport_register_port (PORT_BASE, IRQ_PRINTERACK,
				   PARPORT_DMA_NONE, &parport_arc_ops);

	if (!p) {
		release_region(PORT_BASE, 1);
		return 0;
	}

	p->modes = PARPORT_MODE_ARCSPP;
	p->size = 1;
	rename_region(res, p->name);

	printk(KERN_INFO "%s: Archimedes on-board port, using irq %d\n",
	       p->irq);

	/* Tell the high-level drivers about the port. */
	parport_announce_port (p);

	return 1;
}

module_init(parport_arc_init)

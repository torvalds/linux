/* Low-level parallel-port routines for 8255-based PC-style hardware.
 * 
 * Authors: Phil Blundell <philb@gnu.org>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *	    Jose Renau <renau@acm.org>
 *          David Campbell <campbell@torque.net>
 *          Andrea Arcangeli
 *
 * based on work by Grant Guenther <grant@torque.net> and Phil Blundell.
 *
 * Cleaned up include files - Russell King <linux@arm.uk.linux.org>
 * DMA support - Bert De Jonghe <bert@sophis.be>
 * Many ECP bugs fixed.  Fred Barnes & Jamie Lokier, 1999
 * More PCI support now conditional on CONFIG_PCI, 03/2001, Paul G. 
 * Various hacks, Fred Barnes, 04/2001
 * Updated probing logic - Adam Belay <ambx1@neo.rr.com>
 */

/* This driver should work with any hardware that is broadly compatible
 * with that in the IBM PC.  This applies to the majority of integrated
 * I/O chipsets that are commonly available.  The expected register
 * layout is:
 *
 *	base+0		data
 *	base+1		status
 *	base+2		control
 *
 * In addition, there are some optional registers:
 *
 *	base+3		EPP address
 *	base+4		EPP data
 *	base+0x400	ECP config A
 *	base+0x401	ECP config B
 *	base+0x402	ECP control
 *
 * All registers are 8 bits wide and read/write.  If your hardware differs
 * only in register addresses (eg because your registers are on 32-bit
 * word boundaries) then you can alter the constants in parport_pc.h to
 * accommodate this.
 *
 * Note that the ECP registers may not start at offset 0x400 for PCI cards,
 * but rather will start at port->base_hi.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/pnp.h>
#include <linux/sysctl.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>

#include <linux/parport.h>
#include <linux/parport_pc.h>
#include <linux/via.h>
#include <asm/parport.h>

#define PARPORT_PC_MAX_PORTS PARPORT_MAX

#ifdef CONFIG_ISA_DMA_API
#define HAS_DMA
#endif

/* ECR modes */
#define ECR_SPP 00
#define ECR_PS2 01
#define ECR_PPF 02
#define ECR_ECP 03
#define ECR_EPP 04
#define ECR_VND 05
#define ECR_TST 06
#define ECR_CNF 07
#define ECR_MODE_MASK 0xe0
#define ECR_WRITE(p,v) frob_econtrol((p),0xff,(v))

#undef DEBUG

#ifdef DEBUG
#define DPRINTK  printk
#else
#define DPRINTK(stuff...)
#endif


#define NR_SUPERIOS 3
static struct superio_struct {	/* For Super-IO chips autodetection */
	int io;
	int irq;
	int dma;
} superios[NR_SUPERIOS] __devinitdata = { {0,},};

static int user_specified;
#if defined(CONFIG_PARPORT_PC_SUPERIO) || \
       (defined(CONFIG_PARPORT_1284) && defined(CONFIG_PARPORT_PC_FIFO))
static int verbose_probing;
#endif
static int pci_registered_parport;
static int pnp_registered_parport;

/* frob_control, but for ECR */
static void frob_econtrol (struct parport *pb, unsigned char m,
			   unsigned char v)
{
	unsigned char ectr = 0;

	if (m != 0xff)
		ectr = inb (ECONTROL (pb));

	DPRINTK (KERN_DEBUG "frob_econtrol(%02x,%02x): %02x -> %02x\n",
		m, v, ectr, (ectr & ~m) ^ v);

	outb ((ectr & ~m) ^ v, ECONTROL (pb));
}

static __inline__ void frob_set_mode (struct parport *p, int mode)
{
	frob_econtrol (p, ECR_MODE_MASK, mode << 5);
}

#ifdef CONFIG_PARPORT_PC_FIFO
/* Safely change the mode bits in the ECR 
   Returns:
	    0    : Success
	   -EBUSY: Could not drain FIFO in some finite amount of time,
		   mode not changed!
 */
static int change_mode(struct parport *p, int m)
{
	const struct parport_pc_private *priv = p->physport->private_data;
	unsigned char oecr;
	int mode;

	DPRINTK(KERN_INFO "parport change_mode ECP-ISA to mode 0x%02x\n",m);

	if (!priv->ecr) {
		printk (KERN_DEBUG "change_mode: but there's no ECR!\n");
		return 0;
	}

	/* Bits <7:5> contain the mode. */
	oecr = inb (ECONTROL (p));
	mode = (oecr >> 5) & 0x7;
	if (mode == m) return 0;

	if (mode >= 2 && !(priv->ctr & 0x20)) {
		/* This mode resets the FIFO, so we may
		 * have to wait for it to drain first. */
		unsigned long expire = jiffies + p->physport->cad->timeout;
		int counter;
		switch (mode) {
		case ECR_PPF: /* Parallel Port FIFO mode */
		case ECR_ECP: /* ECP Parallel Port mode */
			/* Busy wait for 200us */
			for (counter = 0; counter < 40; counter++) {
				if (inb (ECONTROL (p)) & 0x01)
					break;
				if (signal_pending (current)) break;
				udelay (5);
			}

			/* Poll slowly. */
			while (!(inb (ECONTROL (p)) & 0x01)) {
				if (time_after_eq (jiffies, expire))
					/* The FIFO is stuck. */
					return -EBUSY;
				schedule_timeout_interruptible(msecs_to_jiffies(10));
				if (signal_pending (current))
					break;
			}
		}
	}

	if (mode >= 2 && m >= 2) {
		/* We have to go through mode 001 */
		oecr &= ~(7 << 5);
		oecr |= ECR_PS2 << 5;
		ECR_WRITE (p, oecr);
	}

	/* Set the mode. */
	oecr &= ~(7 << 5);
	oecr |= m << 5;
	ECR_WRITE (p, oecr);
	return 0;
}

#ifdef CONFIG_PARPORT_1284
/* Find FIFO lossage; FIFO is reset */
#if 0
static int get_fifo_residue (struct parport *p)
{
	int residue;
	int cnfga;
	const struct parport_pc_private *priv = p->physport->private_data;

	/* Adjust for the contents of the FIFO. */
	for (residue = priv->fifo_depth; ; residue--) {
		if (inb (ECONTROL (p)) & 0x2)
				/* Full up. */
			break;

		outb (0, FIFO (p));
	}

	printk (KERN_DEBUG "%s: %d PWords were left in FIFO\n", p->name,
		residue);

	/* Reset the FIFO. */
	frob_set_mode (p, ECR_PS2);

	/* Now change to config mode and clean up. FIXME */
	frob_set_mode (p, ECR_CNF);
	cnfga = inb (CONFIGA (p));
	printk (KERN_DEBUG "%s: cnfgA contains 0x%02x\n", p->name, cnfga);

	if (!(cnfga & (1<<2))) {
		printk (KERN_DEBUG "%s: Accounting for extra byte\n", p->name);
		residue++;
	}

	/* Don't care about partial PWords until support is added for
	 * PWord != 1 byte. */

	/* Back to PS2 mode. */
	frob_set_mode (p, ECR_PS2);

	DPRINTK (KERN_DEBUG "*** get_fifo_residue: done residue collecting (ecr = 0x%2.2x)\n", inb (ECONTROL (p)));
	return residue;
}
#endif  /*  0 */
#endif /* IEEE 1284 support */
#endif /* FIFO support */

/*
 * Clear TIMEOUT BIT in EPP MODE
 *
 * This is also used in SPP detection.
 */
static int clear_epp_timeout(struct parport *pb)
{
	unsigned char r;

	if (!(parport_pc_read_status(pb) & 0x01))
		return 1;

	/* To clear timeout some chips require double read */
	parport_pc_read_status(pb);
	r = parport_pc_read_status(pb);
	outb (r | 0x01, STATUS (pb)); /* Some reset by writing 1 */
	outb (r & 0xfe, STATUS (pb)); /* Others by writing 0 */
	r = parport_pc_read_status(pb);

	return !(r & 0x01);
}

/*
 * Access functions.
 *
 * Most of these aren't static because they may be used by the
 * parport_xxx_yyy macros.  extern __inline__ versions of several
 * of these are in parport_pc.h.
 */

static irqreturn_t parport_pc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	parport_generic_irq(irq, (struct parport *) dev_id, regs);
	/* FIXME! Was it really ours? */
	return IRQ_HANDLED;
}

static void parport_pc_init_state(struct pardevice *dev, struct parport_state *s)
{
	s->u.pc.ctr = 0xc;
	if (dev->irq_func &&
	    dev->port->irq != PARPORT_IRQ_NONE)
		/* Set ackIntEn */
		s->u.pc.ctr |= 0x10;

	s->u.pc.ecr = 0x34; /* NetMos chip can cause problems 0x24;
			     * D.Gruszka VScom */
}

static void parport_pc_save_state(struct parport *p, struct parport_state *s)
{
	const struct parport_pc_private *priv = p->physport->private_data;
	s->u.pc.ctr = priv->ctr;
	if (priv->ecr)
		s->u.pc.ecr = inb (ECONTROL (p));
}

static void parport_pc_restore_state(struct parport *p, struct parport_state *s)
{
	struct parport_pc_private *priv = p->physport->private_data;
	register unsigned char c = s->u.pc.ctr & priv->ctr_writable;
	outb (c, CONTROL (p));
	priv->ctr = c;
	if (priv->ecr)
		ECR_WRITE (p, s->u.pc.ecr);
}

#ifdef CONFIG_PARPORT_1284
static size_t parport_pc_epp_read_data (struct parport *port, void *buf,
					size_t length, int flags)
{
	size_t got = 0;

	if (flags & PARPORT_W91284PIC) {
		unsigned char status;
		size_t left = length;

		/* use knowledge about data lines..:
		 *  nFault is 0 if there is at least 1 byte in the Warp's FIFO
		 *  pError is 1 if there are 16 bytes in the Warp's FIFO
		 */
		status = inb (STATUS (port));

		while (!(status & 0x08) && (got < length)) {
			if ((left >= 16) && (status & 0x20) && !(status & 0x08)) {
				/* can grab 16 bytes from warp fifo */
				if (!((long)buf & 0x03)) {
					insl (EPPDATA (port), buf, 4);
				} else {
					insb (EPPDATA (port), buf, 16);
				}
				buf += 16;
				got += 16;
				left -= 16;
			} else {
				/* grab single byte from the warp fifo */
				*((char *)buf) = inb (EPPDATA (port));
				buf++;
				got++;
				left--;
			}
			status = inb (STATUS (port));
			if (status & 0x01) {
				/* EPP timeout should never occur... */
				printk (KERN_DEBUG "%s: EPP timeout occurred while talking to "
					"w91284pic (should not have done)\n", port->name);
				clear_epp_timeout (port);
			}
		}
		return got;
	}
	if ((flags & PARPORT_EPP_FAST) && (length > 1)) {
		if (!(((long)buf | length) & 0x03)) {
			insl (EPPDATA (port), buf, (length >> 2));
		} else {
			insb (EPPDATA (port), buf, length);
		}
		if (inb (STATUS (port)) & 0x01) {
			clear_epp_timeout (port);
			return -EIO;
		}
		return length;
	}
	for (; got < length; got++) {
		*((char*)buf) = inb (EPPDATA(port));
		buf++;
		if (inb (STATUS (port)) & 0x01) {
			/* EPP timeout */
			clear_epp_timeout (port);
			break;
		}
	}

	return got;
}

static size_t parport_pc_epp_write_data (struct parport *port, const void *buf,
					 size_t length, int flags)
{
	size_t written = 0;

	if ((flags & PARPORT_EPP_FAST) && (length > 1)) {
		if (!(((long)buf | length) & 0x03)) {
			outsl (EPPDATA (port), buf, (length >> 2));
		} else {
			outsb (EPPDATA (port), buf, length);
		}
		if (inb (STATUS (port)) & 0x01) {
			clear_epp_timeout (port);
			return -EIO;
		}
		return length;
	}
	for (; written < length; written++) {
		outb (*((char*)buf), EPPDATA(port));
		buf++;
		if (inb (STATUS(port)) & 0x01) {
			clear_epp_timeout (port);
			break;
		}
	}

	return written;
}

static size_t parport_pc_epp_read_addr (struct parport *port, void *buf,
					size_t length, int flags)
{
	size_t got = 0;

	if ((flags & PARPORT_EPP_FAST) && (length > 1)) {
		insb (EPPADDR (port), buf, length);
		if (inb (STATUS (port)) & 0x01) {
			clear_epp_timeout (port);
			return -EIO;
		}
		return length;
	}
	for (; got < length; got++) {
		*((char*)buf) = inb (EPPADDR (port));
		buf++;
		if (inb (STATUS (port)) & 0x01) {
			clear_epp_timeout (port);
			break;
		}
	}

	return got;
}

static size_t parport_pc_epp_write_addr (struct parport *port,
					 const void *buf, size_t length,
					 int flags)
{
	size_t written = 0;

	if ((flags & PARPORT_EPP_FAST) && (length > 1)) {
		outsb (EPPADDR (port), buf, length);
		if (inb (STATUS (port)) & 0x01) {
			clear_epp_timeout (port);
			return -EIO;
		}
		return length;
	}
	for (; written < length; written++) {
		outb (*((char*)buf), EPPADDR (port));
		buf++;
		if (inb (STATUS (port)) & 0x01) {
			clear_epp_timeout (port);
			break;
		}
	}

	return written;
}

static size_t parport_pc_ecpepp_read_data (struct parport *port, void *buf,
					   size_t length, int flags)
{
	size_t got;

	frob_set_mode (port, ECR_EPP);
	parport_pc_data_reverse (port);
	parport_pc_write_control (port, 0x4);
	got = parport_pc_epp_read_data (port, buf, length, flags);
	frob_set_mode (port, ECR_PS2);

	return got;
}

static size_t parport_pc_ecpepp_write_data (struct parport *port,
					    const void *buf, size_t length,
					    int flags)
{
	size_t written;

	frob_set_mode (port, ECR_EPP);
	parport_pc_write_control (port, 0x4);
	parport_pc_data_forward (port);
	written = parport_pc_epp_write_data (port, buf, length, flags);
	frob_set_mode (port, ECR_PS2);

	return written;
}

static size_t parport_pc_ecpepp_read_addr (struct parport *port, void *buf,
					   size_t length, int flags)
{
	size_t got;

	frob_set_mode (port, ECR_EPP);
	parport_pc_data_reverse (port);
	parport_pc_write_control (port, 0x4);
	got = parport_pc_epp_read_addr (port, buf, length, flags);
	frob_set_mode (port, ECR_PS2);

	return got;
}

static size_t parport_pc_ecpepp_write_addr (struct parport *port,
					    const void *buf, size_t length,
					    int flags)
{
	size_t written;

	frob_set_mode (port, ECR_EPP);
	parport_pc_write_control (port, 0x4);
	parport_pc_data_forward (port);
	written = parport_pc_epp_write_addr (port, buf, length, flags);
	frob_set_mode (port, ECR_PS2);

	return written;
}
#endif /* IEEE 1284 support */

#ifdef CONFIG_PARPORT_PC_FIFO
static size_t parport_pc_fifo_write_block_pio (struct parport *port,
					       const void *buf, size_t length)
{
	int ret = 0;
	const unsigned char *bufp = buf;
	size_t left = length;
	unsigned long expire = jiffies + port->physport->cad->timeout;
	const int fifo = FIFO (port);
	int poll_for = 8; /* 80 usecs */
	const struct parport_pc_private *priv = port->physport->private_data;
	const int fifo_depth = priv->fifo_depth;

	port = port->physport;

	/* We don't want to be interrupted every character. */
	parport_pc_disable_irq (port);
	/* set nErrIntrEn and serviceIntr */
	frob_econtrol (port, (1<<4) | (1<<2), (1<<4) | (1<<2));

	/* Forward mode. */
	parport_pc_data_forward (port); /* Must be in PS2 mode */

	while (left) {
		unsigned char byte;
		unsigned char ecrval = inb (ECONTROL (port));
		int i = 0;

		if (need_resched() && time_before (jiffies, expire))
			/* Can't yield the port. */
			schedule ();

		/* Anyone else waiting for the port? */
		if (port->waithead) {
			printk (KERN_DEBUG "Somebody wants the port\n");
			break;
		}

		if (ecrval & 0x02) {
			/* FIFO is full. Wait for interrupt. */

			/* Clear serviceIntr */
			ECR_WRITE (port, ecrval & ~(1<<2));
		false_alarm:
			ret = parport_wait_event (port, HZ);
			if (ret < 0) break;
			ret = 0;
			if (!time_before (jiffies, expire)) {
				/* Timed out. */
				printk (KERN_DEBUG "FIFO write timed out\n");
				break;
			}
			ecrval = inb (ECONTROL (port));
			if (!(ecrval & (1<<2))) {
				if (need_resched() &&
				    time_before (jiffies, expire))
					schedule ();

				goto false_alarm;
			}

			continue;
		}

		/* Can't fail now. */
		expire = jiffies + port->cad->timeout;

	poll:
		if (signal_pending (current))
			break;

		if (ecrval & 0x01) {
			/* FIFO is empty. Blast it full. */
			const int n = left < fifo_depth ? left : fifo_depth;
			outsb (fifo, bufp, n);
			bufp += n;
			left -= n;

			/* Adjust the poll time. */
			if (i < (poll_for - 2)) poll_for--;
			continue;
		} else if (i++ < poll_for) {
			udelay (10);
			ecrval = inb (ECONTROL (port));
			goto poll;
		}

		/* Half-full (call me an optimist) */
		byte = *bufp++;
		outb (byte, fifo);
		left--;
        }

dump_parport_state ("leave fifo_write_block_pio", port);
	return length - left;
}

#ifdef HAS_DMA
static size_t parport_pc_fifo_write_block_dma (struct parport *port,
					       const void *buf, size_t length)
{
	int ret = 0;
	unsigned long dmaflag;
	size_t left = length;
	const struct parport_pc_private *priv = port->physport->private_data;
	dma_addr_t dma_addr, dma_handle;
	size_t maxlen = 0x10000; /* max 64k per DMA transfer */
	unsigned long start = (unsigned long) buf;
	unsigned long end = (unsigned long) buf + length - 1;

dump_parport_state ("enter fifo_write_block_dma", port);
	if (end < MAX_DMA_ADDRESS) {
		/* If it would cross a 64k boundary, cap it at the end. */
		if ((start ^ end) & ~0xffffUL)
			maxlen = 0x10000 - (start & 0xffff);

		dma_addr = dma_handle = pci_map_single(priv->dev, (void *)buf, length,
						       PCI_DMA_TODEVICE);
        } else {
		/* above 16 MB we use a bounce buffer as ISA-DMA is not possible */
		maxlen   = PAGE_SIZE;          /* sizeof(priv->dma_buf) */
		dma_addr = priv->dma_handle;
		dma_handle = 0;
	}

	port = port->physport;

	/* We don't want to be interrupted every character. */
	parport_pc_disable_irq (port);
	/* set nErrIntrEn and serviceIntr */
	frob_econtrol (port, (1<<4) | (1<<2), (1<<4) | (1<<2));

	/* Forward mode. */
	parport_pc_data_forward (port); /* Must be in PS2 mode */

	while (left) {
		unsigned long expire = jiffies + port->physport->cad->timeout;

		size_t count = left;

		if (count > maxlen)
			count = maxlen;

		if (!dma_handle)   /* bounce buffer ! */
			memcpy(priv->dma_buf, buf, count);

		dmaflag = claim_dma_lock();
		disable_dma(port->dma);
		clear_dma_ff(port->dma);
		set_dma_mode(port->dma, DMA_MODE_WRITE);
		set_dma_addr(port->dma, dma_addr);
		set_dma_count(port->dma, count);

		/* Set DMA mode */
		frob_econtrol (port, 1<<3, 1<<3);

		/* Clear serviceIntr */
		frob_econtrol (port, 1<<2, 0);

		enable_dma(port->dma);
		release_dma_lock(dmaflag);

		/* assume DMA will be successful */
		left -= count;
		buf  += count;
		if (dma_handle) dma_addr += count;

		/* Wait for interrupt. */
	false_alarm:
		ret = parport_wait_event (port, HZ);
		if (ret < 0) break;
		ret = 0;
		if (!time_before (jiffies, expire)) {
			/* Timed out. */
			printk (KERN_DEBUG "DMA write timed out\n");
			break;
		}
		/* Is serviceIntr set? */
		if (!(inb (ECONTROL (port)) & (1<<2))) {
			cond_resched();

			goto false_alarm;
		}

		dmaflag = claim_dma_lock();
		disable_dma(port->dma);
		clear_dma_ff(port->dma);
		count = get_dma_residue(port->dma);
		release_dma_lock(dmaflag);

		cond_resched(); /* Can't yield the port. */

		/* Anyone else waiting for the port? */
		if (port->waithead) {
			printk (KERN_DEBUG "Somebody wants the port\n");
			break;
		}

		/* update for possible DMA residue ! */
		buf  -= count;
		left += count;
		if (dma_handle) dma_addr -= count;
	}

	/* Maybe got here through break, so adjust for DMA residue! */
	dmaflag = claim_dma_lock();
	disable_dma(port->dma);
	clear_dma_ff(port->dma);
	left += get_dma_residue(port->dma);
	release_dma_lock(dmaflag);

	/* Turn off DMA mode */
	frob_econtrol (port, 1<<3, 0);
	
	if (dma_handle)
		pci_unmap_single(priv->dev, dma_handle, length, PCI_DMA_TODEVICE);

dump_parport_state ("leave fifo_write_block_dma", port);
	return length - left;
}
#endif

static inline size_t parport_pc_fifo_write_block(struct parport *port,
					       const void *buf, size_t length)
{
#ifdef HAS_DMA
	if (port->dma != PARPORT_DMA_NONE)
		return parport_pc_fifo_write_block_dma (port, buf, length);
#endif
	return parport_pc_fifo_write_block_pio (port, buf, length);
}

/* Parallel Port FIFO mode (ECP chipsets) */
static size_t parport_pc_compat_write_block_pio (struct parport *port,
						 const void *buf, size_t length,
						 int flags)
{
	size_t written;
	int r;
	unsigned long expire;
	const struct parport_pc_private *priv = port->physport->private_data;

	/* Special case: a timeout of zero means we cannot call schedule().
	 * Also if O_NONBLOCK is set then use the default implementation. */
	if (port->physport->cad->timeout <= PARPORT_INACTIVITY_O_NONBLOCK)
		return parport_ieee1284_write_compat (port, buf,
						      length, flags);

	/* Set up parallel port FIFO mode.*/
	parport_pc_data_forward (port); /* Must be in PS2 mode */
	parport_pc_frob_control (port, PARPORT_CONTROL_STROBE, 0);
	r = change_mode (port, ECR_PPF); /* Parallel port FIFO */
	if (r)  printk (KERN_DEBUG "%s: Warning change_mode ECR_PPF failed\n", port->name);

	port->physport->ieee1284.phase = IEEE1284_PH_FWD_DATA;

	/* Write the data to the FIFO. */
	written = parport_pc_fifo_write_block(port, buf, length);

	/* Finish up. */
	/* For some hardware we don't want to touch the mode until
	 * the FIFO is empty, so allow 4 seconds for each position
	 * in the fifo.
	 */
        expire = jiffies + (priv->fifo_depth * HZ * 4);
	do {
		/* Wait for the FIFO to empty */
		r = change_mode (port, ECR_PS2);
		if (r != -EBUSY) {
			break;
		}
	} while (time_before (jiffies, expire));
	if (r == -EBUSY) {

		printk (KERN_DEBUG "%s: FIFO is stuck\n", port->name);

		/* Prevent further data transfer. */
		frob_set_mode (port, ECR_TST);

		/* Adjust for the contents of the FIFO. */
		for (written -= priv->fifo_depth; ; written++) {
			if (inb (ECONTROL (port)) & 0x2) {
				/* Full up. */
				break;
			}
			outb (0, FIFO (port));
		}

		/* Reset the FIFO and return to PS2 mode. */
		frob_set_mode (port, ECR_PS2);
	}

	r = parport_wait_peripheral (port,
				     PARPORT_STATUS_BUSY,
				     PARPORT_STATUS_BUSY);
	if (r)
		printk (KERN_DEBUG
			"%s: BUSY timeout (%d) in compat_write_block_pio\n", 
			port->name, r);

	port->physport->ieee1284.phase = IEEE1284_PH_FWD_IDLE;

	return written;
}

/* ECP */
#ifdef CONFIG_PARPORT_1284
static size_t parport_pc_ecp_write_block_pio (struct parport *port,
					      const void *buf, size_t length,
					      int flags)
{
	size_t written;
	int r;
	unsigned long expire;
	const struct parport_pc_private *priv = port->physport->private_data;

	/* Special case: a timeout of zero means we cannot call schedule().
	 * Also if O_NONBLOCK is set then use the default implementation. */
	if (port->physport->cad->timeout <= PARPORT_INACTIVITY_O_NONBLOCK)
		return parport_ieee1284_ecp_write_data (port, buf,
							length, flags);

	/* Switch to forward mode if necessary. */
	if (port->physport->ieee1284.phase != IEEE1284_PH_FWD_IDLE) {
		/* Event 47: Set nInit high. */
		parport_frob_control (port,
				      PARPORT_CONTROL_INIT
				      | PARPORT_CONTROL_AUTOFD,
				      PARPORT_CONTROL_INIT
				      | PARPORT_CONTROL_AUTOFD);

		/* Event 49: PError goes high. */
		r = parport_wait_peripheral (port,
					     PARPORT_STATUS_PAPEROUT,
					     PARPORT_STATUS_PAPEROUT);
		if (r) {
			printk (KERN_DEBUG "%s: PError timeout (%d) "
				"in ecp_write_block_pio\n", port->name, r);
		}
	}

	/* Set up ECP parallel port mode.*/
	parport_pc_data_forward (port); /* Must be in PS2 mode */
	parport_pc_frob_control (port,
				 PARPORT_CONTROL_STROBE |
				 PARPORT_CONTROL_AUTOFD,
				 0);
	r = change_mode (port, ECR_ECP); /* ECP FIFO */
	if (r) printk (KERN_DEBUG "%s: Warning change_mode ECR_ECP failed\n", port->name);
	port->physport->ieee1284.phase = IEEE1284_PH_FWD_DATA;

	/* Write the data to the FIFO. */
	written = parport_pc_fifo_write_block(port, buf, length);

	/* Finish up. */
	/* For some hardware we don't want to touch the mode until
	 * the FIFO is empty, so allow 4 seconds for each position
	 * in the fifo.
	 */
	expire = jiffies + (priv->fifo_depth * (HZ * 4));
	do {
		/* Wait for the FIFO to empty */
		r = change_mode (port, ECR_PS2);
		if (r != -EBUSY) {
			break;
		}
	} while (time_before (jiffies, expire));
	if (r == -EBUSY) {

		printk (KERN_DEBUG "%s: FIFO is stuck\n", port->name);

		/* Prevent further data transfer. */
		frob_set_mode (port, ECR_TST);

		/* Adjust for the contents of the FIFO. */
		for (written -= priv->fifo_depth; ; written++) {
			if (inb (ECONTROL (port)) & 0x2) {
				/* Full up. */
				break;
			}
			outb (0, FIFO (port));
		}

		/* Reset the FIFO and return to PS2 mode. */
		frob_set_mode (port, ECR_PS2);

		/* Host transfer recovery. */
		parport_pc_data_reverse (port); /* Must be in PS2 mode */
		udelay (5);
		parport_frob_control (port, PARPORT_CONTROL_INIT, 0);
		r = parport_wait_peripheral (port, PARPORT_STATUS_PAPEROUT, 0);
		if (r)
			printk (KERN_DEBUG "%s: PE,1 timeout (%d) "
				"in ecp_write_block_pio\n", port->name, r);

		parport_frob_control (port,
				      PARPORT_CONTROL_INIT,
				      PARPORT_CONTROL_INIT);
		r = parport_wait_peripheral (port,
					     PARPORT_STATUS_PAPEROUT,
					     PARPORT_STATUS_PAPEROUT);
                if (r)
                        printk (KERN_DEBUG "%s: PE,2 timeout (%d) "
				"in ecp_write_block_pio\n", port->name, r);
	}

	r = parport_wait_peripheral (port,
				     PARPORT_STATUS_BUSY, 
				     PARPORT_STATUS_BUSY);
	if(r)
		printk (KERN_DEBUG
			"%s: BUSY timeout (%d) in ecp_write_block_pio\n",
			port->name, r);

	port->physport->ieee1284.phase = IEEE1284_PH_FWD_IDLE;

	return written;
}

#if 0
static size_t parport_pc_ecp_read_block_pio (struct parport *port,
					     void *buf, size_t length,
					     int flags)
{
	size_t left = length;
	size_t fifofull;
	int r;
	const int fifo = FIFO(port);
	const struct parport_pc_private *priv = port->physport->private_data;
	const int fifo_depth = priv->fifo_depth;
	char *bufp = buf;

	port = port->physport;
DPRINTK (KERN_DEBUG "parport_pc: parport_pc_ecp_read_block_pio\n");
dump_parport_state ("enter fcn", port);

	/* Special case: a timeout of zero means we cannot call schedule().
	 * Also if O_NONBLOCK is set then use the default implementation. */
	if (port->cad->timeout <= PARPORT_INACTIVITY_O_NONBLOCK)
		return parport_ieee1284_ecp_read_data (port, buf,
						       length, flags);

	if (port->ieee1284.mode == IEEE1284_MODE_ECPRLE) {
		/* If the peripheral is allowed to send RLE compressed
		 * data, it is possible for a byte to expand to 128
		 * bytes in the FIFO. */
		fifofull = 128;
	} else {
		fifofull = fifo_depth;
	}

	/* If the caller wants less than a full FIFO's worth of data,
	 * go through software emulation.  Otherwise we may have to throw
	 * away data. */
	if (length < fifofull)
		return parport_ieee1284_ecp_read_data (port, buf,
						       length, flags);

	if (port->ieee1284.phase != IEEE1284_PH_REV_IDLE) {
		/* change to reverse-idle phase (must be in forward-idle) */

		/* Event 38: Set nAutoFd low (also make sure nStrobe is high) */
		parport_frob_control (port,
				      PARPORT_CONTROL_AUTOFD
				      | PARPORT_CONTROL_STROBE,
				      PARPORT_CONTROL_AUTOFD);
		parport_pc_data_reverse (port); /* Must be in PS2 mode */
		udelay (5);
		/* Event 39: Set nInit low to initiate bus reversal */
		parport_frob_control (port,
				      PARPORT_CONTROL_INIT,
				      0);
		/* Event 40: Wait for  nAckReverse (PError) to go low */
		r = parport_wait_peripheral (port, PARPORT_STATUS_PAPEROUT, 0);
                if (r) {
                        printk (KERN_DEBUG "%s: PE timeout Event 40 (%d) "
				"in ecp_read_block_pio\n", port->name, r);
			return 0;
		}
	}

	/* Set up ECP FIFO mode.*/
/*	parport_pc_frob_control (port,
				 PARPORT_CONTROL_STROBE |
				 PARPORT_CONTROL_AUTOFD,
				 PARPORT_CONTROL_AUTOFD); */
	r = change_mode (port, ECR_ECP); /* ECP FIFO */
	if (r) printk (KERN_DEBUG "%s: Warning change_mode ECR_ECP failed\n", port->name);

	port->ieee1284.phase = IEEE1284_PH_REV_DATA;

	/* the first byte must be collected manually */
dump_parport_state ("pre 43", port);
	/* Event 43: Wait for nAck to go low */
	r = parport_wait_peripheral (port, PARPORT_STATUS_ACK, 0);
	if (r) {
		/* timed out while reading -- no data */
		printk (KERN_DEBUG "PIO read timed out (initial byte)\n");
		goto out_no_data;
	}
	/* read byte */
	*bufp++ = inb (DATA (port));
	left--;
dump_parport_state ("43-44", port);
	/* Event 44: nAutoFd (HostAck) goes high to acknowledge */
	parport_pc_frob_control (port,
				 PARPORT_CONTROL_AUTOFD,
				 0);
dump_parport_state ("pre 45", port);
	/* Event 45: Wait for nAck to go high */
/*	r = parport_wait_peripheral (port, PARPORT_STATUS_ACK, PARPORT_STATUS_ACK); */
dump_parport_state ("post 45", port);
r = 0;
	if (r) {
		/* timed out while waiting for peripheral to respond to ack */
		printk (KERN_DEBUG "ECP PIO read timed out (waiting for nAck)\n");

		/* keep hold of the byte we've got already */
		goto out_no_data;
	}
	/* Event 46: nAutoFd (HostAck) goes low to accept more data */
	parport_pc_frob_control (port,
				 PARPORT_CONTROL_AUTOFD,
				 PARPORT_CONTROL_AUTOFD);


dump_parport_state ("rev idle", port);
	/* Do the transfer. */
	while (left > fifofull) {
		int ret;
		unsigned long expire = jiffies + port->cad->timeout;
		unsigned char ecrval = inb (ECONTROL (port));

		if (need_resched() && time_before (jiffies, expire))
			/* Can't yield the port. */
			schedule ();

		/* At this point, the FIFO may already be full. In
                 * that case ECP is already holding back the
                 * peripheral (assuming proper design) with a delayed
                 * handshake.  Work fast to avoid a peripheral
                 * timeout.  */

		if (ecrval & 0x01) {
			/* FIFO is empty. Wait for interrupt. */
dump_parport_state ("FIFO empty", port);

			/* Anyone else waiting for the port? */
			if (port->waithead) {
				printk (KERN_DEBUG "Somebody wants the port\n");
				break;
			}

			/* Clear serviceIntr */
			ECR_WRITE (port, ecrval & ~(1<<2));
		false_alarm:
dump_parport_state ("waiting", port);
			ret = parport_wait_event (port, HZ);
DPRINTK (KERN_DEBUG "parport_wait_event returned %d\n", ret);
			if (ret < 0)
				break;
			ret = 0;
			if (!time_before (jiffies, expire)) {
				/* Timed out. */
dump_parport_state ("timeout", port);
				printk (KERN_DEBUG "PIO read timed out\n");
				break;
			}
			ecrval = inb (ECONTROL (port));
			if (!(ecrval & (1<<2))) {
				if (need_resched() &&
				    time_before (jiffies, expire)) {
					schedule ();
				}
				goto false_alarm;
			}

			/* Depending on how the FIFO threshold was
                         * set, how long interrupt service took, and
                         * how fast the peripheral is, we might be
                         * lucky and have a just filled FIFO. */
			continue;
		}

		if (ecrval & 0x02) {
			/* FIFO is full. */
dump_parport_state ("FIFO full", port);
			insb (fifo, bufp, fifo_depth);
			bufp += fifo_depth;
			left -= fifo_depth;
			continue;
		}

DPRINTK (KERN_DEBUG "*** ecp_read_block_pio: reading one byte from the FIFO\n");

		/* FIFO not filled.  We will cycle this loop for a while
                 * and either the peripheral will fill it faster,
                 * tripping a fast empty with insb, or we empty it. */
		*bufp++ = inb (fifo);
		left--;
	}

	/* scoop up anything left in the FIFO */
	while (left && !(inb (ECONTROL (port) & 0x01))) {
		*bufp++ = inb (fifo);
		left--;
	}

	port->ieee1284.phase = IEEE1284_PH_REV_IDLE;
dump_parport_state ("rev idle2", port);

out_no_data:

	/* Go to forward idle mode to shut the peripheral up (event 47). */
	parport_frob_control (port, PARPORT_CONTROL_INIT, PARPORT_CONTROL_INIT);

	/* event 49: PError goes high */
	r = parport_wait_peripheral (port,
				     PARPORT_STATUS_PAPEROUT,
				     PARPORT_STATUS_PAPEROUT);
	if (r) {
		printk (KERN_DEBUG
			"%s: PE timeout FWDIDLE (%d) in ecp_read_block_pio\n",
			port->name, r);
	}

	port->ieee1284.phase = IEEE1284_PH_FWD_IDLE;

	/* Finish up. */
	{
		int lost = get_fifo_residue (port);
		if (lost)
			/* Shouldn't happen with compliant peripherals. */
			printk (KERN_DEBUG "%s: DATA LOSS (%d bytes)!\n",
				port->name, lost);
	}

dump_parport_state ("fwd idle", port);
	return length - left;
}
#endif  /*  0  */
#endif /* IEEE 1284 support */
#endif /* Allowed to use FIFO/DMA */


/*
 *	******************************************
 *	INITIALISATION AND MODULE STUFF BELOW HERE
 *	******************************************
 */

/* GCC is not inlining extern inline function later overwriten to non-inline,
   so we use outlined_ variants here.  */
static const struct parport_operations parport_pc_ops =
{
	.write_data	= parport_pc_write_data,
	.read_data	= parport_pc_read_data,

	.write_control	= parport_pc_write_control,
	.read_control	= parport_pc_read_control,
	.frob_control	= parport_pc_frob_control,

	.read_status	= parport_pc_read_status,

	.enable_irq	= parport_pc_enable_irq,
	.disable_irq	= parport_pc_disable_irq,

	.data_forward	= parport_pc_data_forward,
	.data_reverse	= parport_pc_data_reverse,

	.init_state	= parport_pc_init_state,
	.save_state	= parport_pc_save_state,
	.restore_state	= parport_pc_restore_state,

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

#ifdef CONFIG_PARPORT_PC_SUPERIO
/* Super-IO chipset detection, Winbond, SMSC */
static void __devinit show_parconfig_smsc37c669(int io, int key)
{
	int cr1,cr4,cra,cr23,cr26,cr27,i=0;
	static const char *const modes[]={
		"SPP and Bidirectional (PS/2)",
		"EPP and SPP",
		"ECP",
		"ECP and EPP" };

	outb(key,io);
	outb(key,io);
	outb(1,io);
	cr1=inb(io+1);
	outb(4,io);
	cr4=inb(io+1);
	outb(0x0a,io);
	cra=inb(io+1);
	outb(0x23,io);
	cr23=inb(io+1);
	outb(0x26,io);
	cr26=inb(io+1);
	outb(0x27,io);
	cr27=inb(io+1);
	outb(0xaa,io);

	if (verbose_probing) {
		printk (KERN_INFO "SMSC 37c669 LPT Config: cr_1=0x%02x, 4=0x%02x, "
			"A=0x%2x, 23=0x%02x, 26=0x%02x, 27=0x%02x\n",
			cr1,cr4,cra,cr23,cr26,cr27);
		
		/* The documentation calls DMA and IRQ-Lines by letters, so
		   the board maker can/will wire them
		   appropriately/randomly...  G=reserved H=IDE-irq, */
		printk (KERN_INFO "SMSC LPT Config: io=0x%04x, irq=%c, dma=%c, "
			"fifo threshold=%d\n", cr23*4,
			(cr27 &0x0f) ? 'A'-1+(cr27 &0x0f): '-',
			(cr26 &0x0f) ? 'A'-1+(cr26 &0x0f): '-', cra & 0x0f);
		printk(KERN_INFO "SMSC LPT Config: enabled=%s power=%s\n",
		       (cr23*4 >=0x100) ?"yes":"no", (cr1 & 4) ? "yes" : "no");
		printk(KERN_INFO "SMSC LPT Config: Port mode=%s, EPP version =%s\n",
		       (cr1 & 0x08 ) ? "Standard mode only (SPP)" : modes[cr4 & 0x03], 
		       (cr4 & 0x40) ? "1.7" : "1.9");
	}
		
	/* Heuristics !  BIOS setup for this mainboard device limits
	   the choices to standard settings, i.e. io-address and IRQ
	   are related, however DMA can be 1 or 3, assume DMA_A=DMA1,
	   DMA_C=DMA3 (this is true e.g. for TYAN 1564D Tomcat IV) */
	if(cr23*4 >=0x100) { /* if active */
		while((superios[i].io!= 0) && (i<NR_SUPERIOS))
			i++;
		if(i==NR_SUPERIOS)
			printk(KERN_INFO "Super-IO: too many chips!\n");
		else {
			int d;
			switch (cr23*4) {
				case 0x3bc:
					superios[i].io = 0x3bc;
					superios[i].irq = 7;
					break;
				case 0x378:
					superios[i].io = 0x378;
					superios[i].irq = 7;
					break;
				case 0x278:
					superios[i].io = 0x278;
					superios[i].irq = 5;
			}
			d=(cr26 &0x0f);
			if((d==1) || (d==3)) 
				superios[i].dma= d;
			else
				superios[i].dma= PARPORT_DMA_NONE;
		}
 	}
}


static void __devinit show_parconfig_winbond(int io, int key)
{
	int cr30,cr60,cr61,cr70,cr74,crf0,i=0;
	static const char *const modes[] = {
		"Standard (SPP) and Bidirectional(PS/2)", /* 0 */
		"EPP-1.9 and SPP",
		"ECP",
		"ECP and EPP-1.9",
		"Standard (SPP)",
		"EPP-1.7 and SPP",		/* 5 */
		"undefined!",
		"ECP and EPP-1.7" };
	static char *const irqtypes[] = {
		"pulsed low, high-Z",
		"follows nACK" };
		
	/* The registers are called compatible-PnP because the
           register layout is modelled after ISA-PnP, the access
           method is just another ... */
	outb(key,io);
	outb(key,io);
	outb(0x07,io);   /* Register 7: Select Logical Device */
	outb(0x01,io+1); /* LD1 is Parallel Port */
	outb(0x30,io);
	cr30=inb(io+1);
	outb(0x60,io);
	cr60=inb(io+1);
	outb(0x61,io);
	cr61=inb(io+1);
	outb(0x70,io);
	cr70=inb(io+1);
	outb(0x74,io);
	cr74=inb(io+1);
	outb(0xf0,io);
	crf0=inb(io+1);
	outb(0xaa,io);

	if (verbose_probing) {
		printk(KERN_INFO "Winbond LPT Config: cr_30=%02x 60,61=%02x%02x "
		       "70=%02x 74=%02x, f0=%02x\n", cr30,cr60,cr61,cr70,cr74,crf0);
		printk(KERN_INFO "Winbond LPT Config: active=%s, io=0x%02x%02x irq=%d, ", 
		       (cr30 & 0x01) ? "yes":"no", cr60,cr61,cr70&0x0f );
		if ((cr74 & 0x07) > 3)
			printk("dma=none\n");
		else
			printk("dma=%d\n",cr74 & 0x07);
		printk(KERN_INFO "Winbond LPT Config: irqtype=%s, ECP fifo threshold=%d\n",
		       irqtypes[crf0>>7], (crf0>>3)&0x0f);
		printk(KERN_INFO "Winbond LPT Config: Port mode=%s\n", modes[crf0 & 0x07]);
	}

	if(cr30 & 0x01) { /* the settings can be interrogated later ... */
		while((superios[i].io!= 0) && (i<NR_SUPERIOS))
			i++;
		if(i==NR_SUPERIOS) 
			printk(KERN_INFO "Super-IO: too many chips!\n");
		else {
			superios[i].io = (cr60<<8)|cr61;
			superios[i].irq = cr70&0x0f;
			superios[i].dma = (((cr74 & 0x07) > 3) ?
					   PARPORT_DMA_NONE : (cr74 & 0x07));
		}
	}
}

static void __devinit decode_winbond(int efer, int key, int devid, int devrev, int oldid)
{
	const char *type = "unknown";
	int id,progif=2;

	if (devid == devrev)
		/* simple heuristics, we happened to read some
                   non-winbond register */
		return;

	id=(devid<<8) | devrev;

	/* Values are from public data sheets pdf files, I can just
           confirm 83977TF is correct :-) */
	if      (id == 0x9771) type="83977F/AF";
	else if (id == 0x9773) type="83977TF / SMSC 97w33x/97w34x";
	else if (id == 0x9774) type="83977ATF";
	else if ((id & ~0x0f) == 0x5270) type="83977CTF / SMSC 97w36x";
	else if ((id & ~0x0f) == 0x52f0) type="83977EF / SMSC 97w35x";
	else if ((id & ~0x0f) == 0x5210) type="83627";
	else if ((id & ~0x0f) == 0x6010) type="83697HF";
	else if ((oldid &0x0f ) == 0x0a) { type="83877F"; progif=1;}
	else if ((oldid &0x0f ) == 0x0b) { type="83877AF"; progif=1;}
	else if ((oldid &0x0f ) == 0x0c) { type="83877TF"; progif=1;}
	else if ((oldid &0x0f ) == 0x0d) { type="83877ATF"; progif=1;}
	else progif=0;

	if (verbose_probing)
		printk(KERN_INFO "Winbond chip at EFER=0x%x key=0x%02x "
		       "devid=%02x devrev=%02x oldid=%02x type=%s\n", 
		       efer, key, devid, devrev, oldid, type);

	if (progif == 2)
		show_parconfig_winbond(efer,key);
}

static void __devinit decode_smsc(int efer, int key, int devid, int devrev)
{
        const char *type = "unknown";
	void (*func)(int io, int key);
        int id;

        if (devid == devrev)
		/* simple heuristics, we happened to read some
                   non-smsc register */
		return;

	func=NULL;
        id=(devid<<8) | devrev;

	if	(id==0x0302) {type="37c669"; func=show_parconfig_smsc37c669;}
	else if	(id==0x6582) type="37c665IR";
	else if	(devid==0x65) type="37c665GT";
	else if	(devid==0x66) type="37c666GT";

	if (verbose_probing)
		printk(KERN_INFO "SMSC chip at EFER=0x%x "
		       "key=0x%02x devid=%02x devrev=%02x type=%s\n",
		       efer, key, devid, devrev, type);

	if (func)
		func(efer,key);
}


static void __devinit winbond_check(int io, int key)
{
	int devid,devrev,oldid,x_devid,x_devrev,x_oldid;

	if (!request_region(io, 3, __FUNCTION__))
		return;

	/* First probe without key */
	outb(0x20,io);
	x_devid=inb(io+1);
	outb(0x21,io);
	x_devrev=inb(io+1);
	outb(0x09,io);
	x_oldid=inb(io+1);

	outb(key,io);
	outb(key,io);     /* Write Magic Sequence to EFER, extended
                             funtion enable register */
	outb(0x20,io);    /* Write EFIR, extended function index register */
	devid=inb(io+1);  /* Read EFDR, extended function data register */
	outb(0x21,io);
	devrev=inb(io+1);
	outb(0x09,io);
	oldid=inb(io+1);
	outb(0xaa,io);    /* Magic Seal */

	if ((x_devid == devid) && (x_devrev == devrev) && (x_oldid == oldid))
		goto out; /* protection against false positives */

	decode_winbond(io,key,devid,devrev,oldid);
out:
	release_region(io, 3);
}

static void __devinit winbond_check2(int io,int key)
{
        int devid,devrev,oldid,x_devid,x_devrev,x_oldid;

	if (!request_region(io, 3, __FUNCTION__))
		return;

	/* First probe without the key */
	outb(0x20,io+2);
	x_devid=inb(io+2);
	outb(0x21,io+1);
	x_devrev=inb(io+2);
	outb(0x09,io+1);
	x_oldid=inb(io+2);

        outb(key,io);     /* Write Magic Byte to EFER, extended
                             funtion enable register */
        outb(0x20,io+2);  /* Write EFIR, extended function index register */
        devid=inb(io+2);  /* Read EFDR, extended function data register */
        outb(0x21,io+1);
        devrev=inb(io+2);
        outb(0x09,io+1);
        oldid=inb(io+2);
        outb(0xaa,io);    /* Magic Seal */

	if ((x_devid == devid) && (x_devrev == devrev) && (x_oldid == oldid))
		goto out; /* protection against false positives */

	decode_winbond(io,key,devid,devrev,oldid);
out:
	release_region(io, 3);
}

static void __devinit smsc_check(int io, int key)
{
        int id,rev,oldid,oldrev,x_id,x_rev,x_oldid,x_oldrev;

	if (!request_region(io, 3, __FUNCTION__))
		return;

	/* First probe without the key */
	outb(0x0d,io);
	x_oldid=inb(io+1);
	outb(0x0e,io);
	x_oldrev=inb(io+1);
	outb(0x20,io);
	x_id=inb(io+1);
	outb(0x21,io);
	x_rev=inb(io+1);

        outb(key,io);
        outb(key,io);     /* Write Magic Sequence to EFER, extended
                             funtion enable register */
        outb(0x0d,io);    /* Write EFIR, extended function index register */
        oldid=inb(io+1);  /* Read EFDR, extended function data register */
        outb(0x0e,io);
        oldrev=inb(io+1);
	outb(0x20,io);
	id=inb(io+1);
	outb(0x21,io);
	rev=inb(io+1);
        outb(0xaa,io);    /* Magic Seal */

	if ((x_id == id) && (x_oldrev == oldrev) &&
	    (x_oldid == oldid) && (x_rev == rev))
		goto out; /* protection against false positives */

        decode_smsc(io,key,oldid,oldrev);
out:
	release_region(io, 3);
}


static void __devinit detect_and_report_winbond (void)
{ 
	if (verbose_probing)
		printk(KERN_DEBUG "Winbond Super-IO detection, now testing ports 3F0,370,250,4E,2E ...\n");
	winbond_check(0x3f0,0x87);
	winbond_check(0x370,0x87);
	winbond_check(0x2e ,0x87);
	winbond_check(0x4e ,0x87);
	winbond_check(0x3f0,0x86);
	winbond_check2(0x250,0x88); 
	winbond_check2(0x250,0x89);
}

static void __devinit detect_and_report_smsc (void)
{
	if (verbose_probing)
		printk(KERN_DEBUG "SMSC Super-IO detection, now testing Ports 2F0, 370 ...\n");
	smsc_check(0x3f0,0x55);
	smsc_check(0x370,0x55);
	smsc_check(0x3f0,0x44);
	smsc_check(0x370,0x44);
}
#endif /* CONFIG_PARPORT_PC_SUPERIO */

static int __devinit get_superio_dma (struct parport *p)
{
	int i=0;
	while( (superios[i].io != p->base) && (i<NR_SUPERIOS))
		i++;
	if (i!=NR_SUPERIOS)
		return superios[i].dma;
	return PARPORT_DMA_NONE;
}

static int __devinit get_superio_irq (struct parport *p)
{
	int i=0;
        while( (superios[i].io != p->base) && (i<NR_SUPERIOS))
                i++;
        if (i!=NR_SUPERIOS)
                return superios[i].irq;
        return PARPORT_IRQ_NONE;
}
	

/* --- Mode detection ------------------------------------- */

/*
 * Checks for port existence, all ports support SPP MODE
 * Returns: 
 *         0           :  No parallel port at this address
 *  PARPORT_MODE_PCSPP :  SPP port detected 
 *                        (if the user specified an ioport himself,
 *                         this shall always be the case!)
 *
 */
static int __devinit parport_SPP_supported(struct parport *pb)
{
	unsigned char r, w;

	/*
	 * first clear an eventually pending EPP timeout 
	 * I (sailer@ife.ee.ethz.ch) have an SMSC chipset
	 * that does not even respond to SPP cycles if an EPP
	 * timeout is pending
	 */
	clear_epp_timeout(pb);

	/* Do a simple read-write test to make sure the port exists. */
	w = 0xc;
	outb (w, CONTROL (pb));

	/* Is there a control register that we can read from?  Some
	 * ports don't allow reads, so read_control just returns a
	 * software copy. Some ports _do_ allow reads, so bypass the
	 * software copy here.  In addition, some bits aren't
	 * writable. */
	r = inb (CONTROL (pb));
	if ((r & 0xf) == w) {
		w = 0xe;
		outb (w, CONTROL (pb));
		r = inb (CONTROL (pb));
		outb (0xc, CONTROL (pb));
		if ((r & 0xf) == w)
			return PARPORT_MODE_PCSPP;
	}

	if (user_specified)
		/* That didn't work, but the user thinks there's a
		 * port here. */
		printk (KERN_INFO "parport 0x%lx (WARNING): CTR: "
			"wrote 0x%02x, read 0x%02x\n", pb->base, w, r);

	/* Try the data register.  The data lines aren't tri-stated at
	 * this stage, so we expect back what we wrote. */
	w = 0xaa;
	parport_pc_write_data (pb, w);
	r = parport_pc_read_data (pb);
	if (r == w) {
		w = 0x55;
		parport_pc_write_data (pb, w);
		r = parport_pc_read_data (pb);
		if (r == w)
			return PARPORT_MODE_PCSPP;
	}

	if (user_specified) {
		/* Didn't work, but the user is convinced this is the
		 * place. */
		printk (KERN_INFO "parport 0x%lx (WARNING): DATA: "
			"wrote 0x%02x, read 0x%02x\n", pb->base, w, r);
		printk (KERN_INFO "parport 0x%lx: You gave this address, "
			"but there is probably no parallel port there!\n",
			pb->base);
	}

	/* It's possible that we can't read the control register or
	 * the data register.  In that case just believe the user. */
	if (user_specified)
		return PARPORT_MODE_PCSPP;

	return 0;
}

/* Check for ECR
 *
 * Old style XT ports alias io ports every 0x400, hence accessing ECR
 * on these cards actually accesses the CTR.
 *
 * Modern cards don't do this but reading from ECR will return 0xff
 * regardless of what is written here if the card does NOT support
 * ECP.
 *
 * We first check to see if ECR is the same as CTR.  If not, the low
 * two bits of ECR aren't writable, so we check by writing ECR and
 * reading it back to see if it's what we expect.
 */
static int __devinit parport_ECR_present(struct parport *pb)
{
	struct parport_pc_private *priv = pb->private_data;
	unsigned char r = 0xc;

	outb (r, CONTROL (pb));
	if ((inb (ECONTROL (pb)) & 0x3) == (r & 0x3)) {
		outb (r ^ 0x2, CONTROL (pb)); /* Toggle bit 1 */

		r = inb (CONTROL (pb));
		if ((inb (ECONTROL (pb)) & 0x2) == (r & 0x2))
			goto no_reg; /* Sure that no ECR register exists */
	}
	
	if ((inb (ECONTROL (pb)) & 0x3 ) != 0x1)
		goto no_reg;

	ECR_WRITE (pb, 0x34);
	if (inb (ECONTROL (pb)) != 0x35)
		goto no_reg;

	priv->ecr = 1;
	outb (0xc, CONTROL (pb));
	
	/* Go to mode 000 */
	frob_set_mode (pb, ECR_SPP);

	return 1;

 no_reg:
	outb (0xc, CONTROL (pb));
	return 0; 
}

#ifdef CONFIG_PARPORT_1284
/* Detect PS/2 support.
 *
 * Bit 5 (0x20) sets the PS/2 data direction; setting this high
 * allows us to read data from the data lines.  In theory we would get back
 * 0xff but any peripheral attached to the port may drag some or all of the
 * lines down to zero.  So if we get back anything that isn't the contents
 * of the data register we deem PS/2 support to be present. 
 *
 * Some SPP ports have "half PS/2" ability - you can't turn off the line
 * drivers, but an external peripheral with sufficiently beefy drivers of
 * its own can overpower them and assert its own levels onto the bus, from
 * where they can then be read back as normal.  Ports with this property
 * and the right type of device attached are likely to fail the SPP test,
 * (as they will appear to have stuck bits) and so the fact that they might
 * be misdetected here is rather academic. 
 */

static int __devinit parport_PS2_supported(struct parport *pb)
{
	int ok = 0;
  
	clear_epp_timeout(pb);

	/* try to tri-state the buffer */
	parport_pc_data_reverse (pb);
	
	parport_pc_write_data(pb, 0x55);
	if (parport_pc_read_data(pb) != 0x55) ok++;

	parport_pc_write_data(pb, 0xaa);
	if (parport_pc_read_data(pb) != 0xaa) ok++;

	/* cancel input mode */
	parport_pc_data_forward (pb);

	if (ok) {
		pb->modes |= PARPORT_MODE_TRISTATE;
	} else {
		struct parport_pc_private *priv = pb->private_data;
		priv->ctr_writable &= ~0x20;
	}

	return ok;
}

#ifdef CONFIG_PARPORT_PC_FIFO
static int __devinit parport_ECP_supported(struct parport *pb)
{
	int i;
	int config, configb;
	int pword;
	struct parport_pc_private *priv = pb->private_data;
	/* Translate ECP intrLine to ISA irq value */	
	static const int intrline[]= { 0, 7, 9, 10, 11, 14, 15, 5 }; 

	/* If there is no ECR, we have no hope of supporting ECP. */
	if (!priv->ecr)
		return 0;

	/* Find out FIFO depth */
	ECR_WRITE (pb, ECR_SPP << 5); /* Reset FIFO */
	ECR_WRITE (pb, ECR_TST << 5); /* TEST FIFO */
	for (i=0; i < 1024 && !(inb (ECONTROL (pb)) & 0x02); i++)
		outb (0xaa, FIFO (pb));

	/*
	 * Using LGS chipset it uses ECR register, but
	 * it doesn't support ECP or FIFO MODE
	 */
	if (i == 1024) {
		ECR_WRITE (pb, ECR_SPP << 5);
		return 0;
	}

	priv->fifo_depth = i;
	if (verbose_probing)
		printk (KERN_DEBUG "0x%lx: FIFO is %d bytes\n", pb->base, i);

	/* Find out writeIntrThreshold */
	frob_econtrol (pb, 1<<2, 1<<2);
	frob_econtrol (pb, 1<<2, 0);
	for (i = 1; i <= priv->fifo_depth; i++) {
		inb (FIFO (pb));
		udelay (50);
		if (inb (ECONTROL (pb)) & (1<<2))
			break;
	}

	if (i <= priv->fifo_depth) {
		if (verbose_probing)
			printk (KERN_DEBUG "0x%lx: writeIntrThreshold is %d\n",
				pb->base, i);
	} else
		/* Number of bytes we know we can write if we get an
                   interrupt. */
		i = 0;

	priv->writeIntrThreshold = i;

	/* Find out readIntrThreshold */
	frob_set_mode (pb, ECR_PS2); /* Reset FIFO and enable PS2 */
	parport_pc_data_reverse (pb); /* Must be in PS2 mode */
	frob_set_mode (pb, ECR_TST); /* Test FIFO */
	frob_econtrol (pb, 1<<2, 1<<2);
	frob_econtrol (pb, 1<<2, 0);
	for (i = 1; i <= priv->fifo_depth; i++) {
		outb (0xaa, FIFO (pb));
		if (inb (ECONTROL (pb)) & (1<<2))
			break;
	}

	if (i <= priv->fifo_depth) {
		if (verbose_probing)
			printk (KERN_INFO "0x%lx: readIntrThreshold is %d\n",
				pb->base, i);
	} else
		/* Number of bytes we can read if we get an interrupt. */
		i = 0;

	priv->readIntrThreshold = i;

	ECR_WRITE (pb, ECR_SPP << 5); /* Reset FIFO */
	ECR_WRITE (pb, 0xf4); /* Configuration mode */
	config = inb (CONFIGA (pb));
	pword = (config >> 4) & 0x7;
	switch (pword) {
	case 0:
		pword = 2;
		printk (KERN_WARNING "0x%lx: Unsupported pword size!\n",
			pb->base);
		break;
	case 2:
		pword = 4;
		printk (KERN_WARNING "0x%lx: Unsupported pword size!\n",
			pb->base);
		break;
	default:
		printk (KERN_WARNING "0x%lx: Unknown implementation ID\n",
			pb->base);
		/* Assume 1 */
	case 1:
		pword = 1;
	}
	priv->pword = pword;

	if (verbose_probing) {
		printk (KERN_DEBUG "0x%lx: PWord is %d bits\n", pb->base, 8 * pword);
		
		printk (KERN_DEBUG "0x%lx: Interrupts are ISA-%s\n", pb->base,
			config & 0x80 ? "Level" : "Pulses");

		configb = inb (CONFIGB (pb));
		printk (KERN_DEBUG "0x%lx: ECP port cfgA=0x%02x cfgB=0x%02x\n",
			pb->base, config, configb);
		printk (KERN_DEBUG "0x%lx: ECP settings irq=", pb->base);
		if ((configb >>3) & 0x07)
			printk("%d",intrline[(configb >>3) & 0x07]);
		else
			printk("<none or set by other means>");
		printk (" dma=");
		if( (configb & 0x03 ) == 0x00)
			printk("<none or set by other means>\n");
		else
			printk("%d\n",configb & 0x07);
	}

	/* Go back to mode 000 */
	frob_set_mode (pb, ECR_SPP);

	return 1;
}
#endif

static int __devinit parport_ECPPS2_supported(struct parport *pb)
{
	const struct parport_pc_private *priv = pb->private_data;
	int result;
	unsigned char oecr;

	if (!priv->ecr)
		return 0;

	oecr = inb (ECONTROL (pb));
	ECR_WRITE (pb, ECR_PS2 << 5);
	result = parport_PS2_supported(pb);
	ECR_WRITE (pb, oecr);
	return result;
}

/* EPP mode detection  */

static int __devinit parport_EPP_supported(struct parport *pb)
{
	const struct parport_pc_private *priv = pb->private_data;

	/*
	 * Theory:
	 *	Bit 0 of STR is the EPP timeout bit, this bit is 0
	 *	when EPP is possible and is set high when an EPP timeout
	 *	occurs (EPP uses the HALT line to stop the CPU while it does
	 *	the byte transfer, an EPP timeout occurs if the attached
	 *	device fails to respond after 10 micro seconds).
	 *
	 *	This bit is cleared by either reading it (National Semi)
	 *	or writing a 1 to the bit (SMC, UMC, WinBond), others ???
	 *	This bit is always high in non EPP modes.
	 */

	/* If EPP timeout bit clear then EPP available */
	if (!clear_epp_timeout(pb)) {
		return 0;  /* No way to clear timeout */
	}

	/* Check for Intel bug. */
	if (priv->ecr) {
		unsigned char i;
		for (i = 0x00; i < 0x80; i += 0x20) {
			ECR_WRITE (pb, i);
			if (clear_epp_timeout (pb)) {
				/* Phony EPP in ECP. */
				return 0;
			}
		}
	}

	pb->modes |= PARPORT_MODE_EPP;

	/* Set up access functions to use EPP hardware. */
	pb->ops->epp_read_data = parport_pc_epp_read_data;
	pb->ops->epp_write_data = parport_pc_epp_write_data;
	pb->ops->epp_read_addr = parport_pc_epp_read_addr;
	pb->ops->epp_write_addr = parport_pc_epp_write_addr;

	return 1;
}

static int __devinit parport_ECPEPP_supported(struct parport *pb)
{
	struct parport_pc_private *priv = pb->private_data;
	int result;
	unsigned char oecr;

	if (!priv->ecr) {
		return 0;
	}

	oecr = inb (ECONTROL (pb));
	/* Search for SMC style EPP+ECP mode */
	ECR_WRITE (pb, 0x80);
	outb (0x04, CONTROL (pb));
	result = parport_EPP_supported(pb);

	ECR_WRITE (pb, oecr);

	if (result) {
		/* Set up access functions to use ECP+EPP hardware. */
		pb->ops->epp_read_data = parport_pc_ecpepp_read_data;
		pb->ops->epp_write_data = parport_pc_ecpepp_write_data;
		pb->ops->epp_read_addr = parport_pc_ecpepp_read_addr;
		pb->ops->epp_write_addr = parport_pc_ecpepp_write_addr;
	}

	return result;
}

#else /* No IEEE 1284 support */

/* Don't bother probing for modes we know we won't use. */
static int __devinit parport_PS2_supported(struct parport *pb) { return 0; }
#ifdef CONFIG_PARPORT_PC_FIFO
static int __devinit parport_ECP_supported(struct parport *pb) { return 0; }
#endif
static int __devinit parport_EPP_supported(struct parport *pb) { return 0; }
static int __devinit parport_ECPEPP_supported(struct parport *pb){return 0;}
static int __devinit parport_ECPPS2_supported(struct parport *pb){return 0;}

#endif /* No IEEE 1284 support */

/* --- IRQ detection -------------------------------------- */

/* Only if supports ECP mode */
static int __devinit programmable_irq_support(struct parport *pb)
{
	int irq, intrLine;
	unsigned char oecr = inb (ECONTROL (pb));
	static const int lookup[8] = {
		PARPORT_IRQ_NONE, 7, 9, 10, 11, 14, 15, 5
	};

	ECR_WRITE (pb, ECR_CNF << 5); /* Configuration MODE */

	intrLine = (inb (CONFIGB (pb)) >> 3) & 0x07;
	irq = lookup[intrLine];

	ECR_WRITE (pb, oecr);
	return irq;
}

static int __devinit irq_probe_ECP(struct parport *pb)
{
	int i;
	unsigned long irqs;

	irqs = probe_irq_on();
		
	ECR_WRITE (pb, ECR_SPP << 5); /* Reset FIFO */
	ECR_WRITE (pb, (ECR_TST << 5) | 0x04);
	ECR_WRITE (pb, ECR_TST << 5);

	/* If Full FIFO sure that writeIntrThreshold is generated */
	for (i=0; i < 1024 && !(inb (ECONTROL (pb)) & 0x02) ; i++) 
		outb (0xaa, FIFO (pb));
		
	pb->irq = probe_irq_off(irqs);
	ECR_WRITE (pb, ECR_SPP << 5);

	if (pb->irq <= 0)
		pb->irq = PARPORT_IRQ_NONE;

	return pb->irq;
}

/*
 * This detection seems that only works in National Semiconductors
 * This doesn't work in SMC, LGS, and Winbond 
 */
static int __devinit irq_probe_EPP(struct parport *pb)
{
#ifndef ADVANCED_DETECT
	return PARPORT_IRQ_NONE;
#else
	int irqs;
	unsigned char oecr;

	if (pb->modes & PARPORT_MODE_PCECR)
		oecr = inb (ECONTROL (pb));

	irqs = probe_irq_on();

	if (pb->modes & PARPORT_MODE_PCECR)
		frob_econtrol (pb, 0x10, 0x10);
	
	clear_epp_timeout(pb);
	parport_pc_frob_control (pb, 0x20, 0x20);
	parport_pc_frob_control (pb, 0x10, 0x10);
	clear_epp_timeout(pb);

	/* Device isn't expecting an EPP read
	 * and generates an IRQ.
	 */
	parport_pc_read_epp(pb);
	udelay(20);

	pb->irq = probe_irq_off (irqs);
	if (pb->modes & PARPORT_MODE_PCECR)
		ECR_WRITE (pb, oecr);
	parport_pc_write_control(pb, 0xc);

	if (pb->irq <= 0)
		pb->irq = PARPORT_IRQ_NONE;

	return pb->irq;
#endif /* Advanced detection */
}

static int __devinit irq_probe_SPP(struct parport *pb)
{
	/* Don't even try to do this. */
	return PARPORT_IRQ_NONE;
}

/* We will attempt to share interrupt requests since other devices
 * such as sound cards and network cards seem to like using the
 * printer IRQs.
 *
 * When ECP is available we can autoprobe for IRQs.
 * NOTE: If we can autoprobe it, we can register the IRQ.
 */
static int __devinit parport_irq_probe(struct parport *pb)
{
	struct parport_pc_private *priv = pb->private_data;

	if (priv->ecr) {
		pb->irq = programmable_irq_support(pb);

		if (pb->irq == PARPORT_IRQ_NONE)
			pb->irq = irq_probe_ECP(pb);
	}

	if ((pb->irq == PARPORT_IRQ_NONE) && priv->ecr &&
	    (pb->modes & PARPORT_MODE_EPP))
		pb->irq = irq_probe_EPP(pb);

	clear_epp_timeout(pb);

	if (pb->irq == PARPORT_IRQ_NONE && (pb->modes & PARPORT_MODE_EPP))
		pb->irq = irq_probe_EPP(pb);

	clear_epp_timeout(pb);

	if (pb->irq == PARPORT_IRQ_NONE)
		pb->irq = irq_probe_SPP(pb);

	if (pb->irq == PARPORT_IRQ_NONE)
		pb->irq = get_superio_irq(pb);

	return pb->irq;
}

/* --- DMA detection -------------------------------------- */

/* Only if chipset conforms to ECP ISA Interface Standard */
static int __devinit programmable_dma_support (struct parport *p)
{
	unsigned char oecr = inb (ECONTROL (p));
	int dma;

	frob_set_mode (p, ECR_CNF);
	
	dma = inb (CONFIGB(p)) & 0x07;
	/* 000: Indicates jumpered 8-bit DMA if read-only.
	   100: Indicates jumpered 16-bit DMA if read-only. */
	if ((dma & 0x03) == 0)
		dma = PARPORT_DMA_NONE;

	ECR_WRITE (p, oecr);
	return dma;
}

static int __devinit parport_dma_probe (struct parport *p)
{
	const struct parport_pc_private *priv = p->private_data;
	if (priv->ecr)
		p->dma = programmable_dma_support(p); /* ask ECP chipset first */
	if (p->dma == PARPORT_DMA_NONE) {
		/* ask known Super-IO chips proper, although these
		   claim ECP compatible, some don't report their DMA
		   conforming to ECP standards */
		p->dma = get_superio_dma(p);
	}

	return p->dma;
}

/* --- Initialisation code -------------------------------- */

static LIST_HEAD(ports_list);
static DEFINE_SPINLOCK(ports_lock);

struct parport *parport_pc_probe_port (unsigned long int base,
				       unsigned long int base_hi,
				       int irq, int dma,
				       struct pci_dev *dev)
{
	struct parport_pc_private *priv;
	struct parport_operations *ops;
	struct parport *p;
	int probedirq = PARPORT_IRQ_NONE;
	struct resource *base_res;
	struct resource	*ECR_res = NULL;
	struct resource	*EPP_res = NULL;

	ops = kmalloc(sizeof (struct parport_operations), GFP_KERNEL);
	if (!ops)
		goto out1;

	priv = kmalloc (sizeof (struct parport_pc_private), GFP_KERNEL);
	if (!priv)
		goto out2;

	/* a misnomer, actually - it's allocate and reserve parport number */
	p = parport_register_port(base, irq, dma, ops);
	if (!p)
		goto out3;

	base_res = request_region(base, 3, p->name);
	if (!base_res)
		goto out4;

	memcpy(ops, &parport_pc_ops, sizeof (struct parport_operations));
	priv->ctr = 0xc;
	priv->ctr_writable = ~0x10;
	priv->ecr = 0;
	priv->fifo_depth = 0;
	priv->dma_buf = NULL;
	priv->dma_handle = 0;
	priv->dev = dev;
	INIT_LIST_HEAD(&priv->list);
	priv->port = p;
	p->base_hi = base_hi;
	p->modes = PARPORT_MODE_PCSPP | PARPORT_MODE_SAFEININT;
	p->private_data = priv;

	if (base_hi) {
		ECR_res = request_region(base_hi, 3, p->name);
		if (ECR_res)
			parport_ECR_present(p);
	}

	if (base != 0x3bc) {
		EPP_res = request_region(base+0x3, 5, p->name);
		if (EPP_res)
			if (!parport_EPP_supported(p))
				parport_ECPEPP_supported(p);
	}
	if (!parport_SPP_supported (p))
		/* No port. */
		goto out5;
	if (priv->ecr)
		parport_ECPPS2_supported(p);
	else
		parport_PS2_supported(p);

	p->size = (p->modes & PARPORT_MODE_EPP)?8:3;

	printk(KERN_INFO "%s: PC-style at 0x%lx", p->name, p->base);
	if (p->base_hi && priv->ecr)
		printk(" (0x%lx)", p->base_hi);
	if (p->irq == PARPORT_IRQ_AUTO) {
		p->irq = PARPORT_IRQ_NONE;
		parport_irq_probe(p);
	} else if (p->irq == PARPORT_IRQ_PROBEONLY) {
		p->irq = PARPORT_IRQ_NONE;
		parport_irq_probe(p);
		probedirq = p->irq;
		p->irq = PARPORT_IRQ_NONE;
	}
	if (p->irq != PARPORT_IRQ_NONE) {
		printk(", irq %d", p->irq);
		priv->ctr_writable |= 0x10;

		if (p->dma == PARPORT_DMA_AUTO) {
			p->dma = PARPORT_DMA_NONE;
			parport_dma_probe(p);
		}
	}
	if (p->dma == PARPORT_DMA_AUTO) /* To use DMA, giving the irq
                                           is mandatory (see above) */
		p->dma = PARPORT_DMA_NONE;

#ifdef CONFIG_PARPORT_PC_FIFO
	if (parport_ECP_supported(p) &&
	    p->dma != PARPORT_DMA_NOFIFO &&
	    priv->fifo_depth > 0 && p->irq != PARPORT_IRQ_NONE) {
		p->modes |= PARPORT_MODE_ECP | PARPORT_MODE_COMPAT;
		p->ops->compat_write_data = parport_pc_compat_write_block_pio;
#ifdef CONFIG_PARPORT_1284
		p->ops->ecp_write_data = parport_pc_ecp_write_block_pio;
		/* currently broken, but working on it.. (FB) */
		/* p->ops->ecp_read_data = parport_pc_ecp_read_block_pio; */
#endif /* IEEE 1284 support */
		if (p->dma != PARPORT_DMA_NONE) {
			printk(", dma %d", p->dma);
			p->modes |= PARPORT_MODE_DMA;
		}
		else printk(", using FIFO");
	}
	else
		/* We can't use the DMA channel after all. */
		p->dma = PARPORT_DMA_NONE;
#endif /* Allowed to use FIFO/DMA */

	printk(" [");
#define printmode(x) {if(p->modes&PARPORT_MODE_##x){printk("%s%s",f?",":"",#x);f++;}}
	{
		int f = 0;
		printmode(PCSPP);
		printmode(TRISTATE);
		printmode(COMPAT)
		printmode(EPP);
		printmode(ECP);
		printmode(DMA);
	}
#undef printmode
#ifndef CONFIG_PARPORT_1284
	printk ("(,...)");
#endif /* CONFIG_PARPORT_1284 */
	printk("]\n");
	if (probedirq != PARPORT_IRQ_NONE) 
		printk(KERN_INFO "%s: irq %d detected\n", p->name, probedirq);

	/* If No ECP release the ports grabbed above. */
	if (ECR_res && (p->modes & PARPORT_MODE_ECP) == 0) {
		release_region(base_hi, 3);
		ECR_res = NULL;
	}
	/* Likewise for EEP ports */
	if (EPP_res && (p->modes & PARPORT_MODE_EPP) == 0) {
		release_region(base+3, 5);
		EPP_res = NULL;
	}
	if (p->irq != PARPORT_IRQ_NONE) {
		if (request_irq (p->irq, parport_pc_interrupt,
				 0, p->name, p)) {
			printk (KERN_WARNING "%s: irq %d in use, "
				"resorting to polled operation\n",
				p->name, p->irq);
			p->irq = PARPORT_IRQ_NONE;
			p->dma = PARPORT_DMA_NONE;
		}

#ifdef CONFIG_PARPORT_PC_FIFO
#ifdef HAS_DMA
		if (p->dma != PARPORT_DMA_NONE) {
			if (request_dma (p->dma, p->name)) {
				printk (KERN_WARNING "%s: dma %d in use, "
					"resorting to PIO operation\n",
					p->name, p->dma);
				p->dma = PARPORT_DMA_NONE;
			} else {
				priv->dma_buf =
				  pci_alloc_consistent(priv->dev,
						       PAGE_SIZE,
						       &priv->dma_handle);
				if (! priv->dma_buf) {
					printk (KERN_WARNING "%s: "
						"cannot get buffer for DMA, "
						"resorting to PIO operation\n",
						p->name);
					free_dma(p->dma);
					p->dma = PARPORT_DMA_NONE;
				}
			}
		}
#endif
#endif
	}

	/* Done probing.  Now put the port into a sensible start-up state. */
	if (priv->ecr)
		/*
		 * Put the ECP detected port in PS2 mode.
		 * Do this also for ports that have ECR but don't do ECP.
		 */
		ECR_WRITE (p, 0x34);

	parport_pc_write_data(p, 0);
	parport_pc_data_forward (p);

	/* Now that we've told the sharing engine about the port, and
	   found out its characteristics, let the high-level drivers
	   know about it. */
	spin_lock(&ports_lock);
	list_add(&priv->list, &ports_list);
	spin_unlock(&ports_lock);
	parport_announce_port (p);

	return p;

out5:
	if (ECR_res)
		release_region(base_hi, 3);
	if (EPP_res)
		release_region(base+0x3, 5);
	release_region(base, 3);
out4:
	parport_put_port(p);
out3:
	kfree (priv);
out2:
	kfree (ops);
out1:
	return NULL;
}

EXPORT_SYMBOL (parport_pc_probe_port);

void parport_pc_unregister_port (struct parport *p)
{
	struct parport_pc_private *priv = p->private_data;
	struct parport_operations *ops = p->ops;

	parport_remove_port(p);
	spin_lock(&ports_lock);
	list_del_init(&priv->list);
	spin_unlock(&ports_lock);
#if defined(CONFIG_PARPORT_PC_FIFO) && defined(HAS_DMA)
	if (p->dma != PARPORT_DMA_NONE)
		free_dma(p->dma);
#endif
	if (p->irq != PARPORT_IRQ_NONE)
		free_irq(p->irq, p);
	release_region(p->base, 3);
	if (p->size > 3)
		release_region(p->base + 3, p->size - 3);
	if (p->modes & PARPORT_MODE_ECP)
		release_region(p->base_hi, 3);
#if defined(CONFIG_PARPORT_PC_FIFO) && defined(HAS_DMA)
	if (priv->dma_buf)
		pci_free_consistent(priv->dev, PAGE_SIZE,
				    priv->dma_buf,
				    priv->dma_handle);
#endif
	kfree (p->private_data);
	parport_put_port(p);
	kfree (ops); /* hope no-one cached it */
}

EXPORT_SYMBOL (parport_pc_unregister_port);

#ifdef CONFIG_PCI

/* ITE support maintained by Rich Liu <richliu@poorman.org> */
static int __devinit sio_ite_8872_probe (struct pci_dev *pdev, int autoirq,
					 int autodma,
					 const struct parport_pc_via_data *via)
{
	short inta_addr[6] = { 0x2A0, 0x2C0, 0x220, 0x240, 0x1E0 };
	struct resource *base_res;
	u32 ite8872set;
	u32 ite8872_lpt, ite8872_lpthi;
	u8 ite8872_irq, type;
	char *fake_name = "parport probe";
	int irq;
	int i;

	DPRINTK (KERN_DEBUG "sio_ite_8872_probe()\n");
	
	// make sure which one chip
	for(i = 0; i < 5; i++) {
		base_res = request_region(inta_addr[i], 0x8, fake_name);
		if (base_res) {
			int test;
			pci_write_config_dword (pdev, 0x60,
						0xe7000000 | inta_addr[i]);
			pci_write_config_dword (pdev, 0x78,
						0x00000000 | inta_addr[i]);
			test = inb (inta_addr[i]);
			if (test != 0xff) break;
			release_region(inta_addr[i], 0x8);
		}
	}
	if(i >= 5) {
		printk (KERN_INFO "parport_pc: cannot find ITE8872 INTA\n");
		return 0;
	}

	type = inb (inta_addr[i] + 0x18);
	type &= 0x0f;

	switch (type) {
	case 0x2:
		printk (KERN_INFO "parport_pc: ITE8871 found (1P)\n");
		ite8872set = 0x64200000;
		break;
	case 0xa:
		printk (KERN_INFO "parport_pc: ITE8875 found (1P)\n");
		ite8872set = 0x64200000;
		break;
	case 0xe:
		printk (KERN_INFO "parport_pc: ITE8872 found (2S1P)\n");
		ite8872set = 0x64e00000;
		break;
	case 0x6:
		printk (KERN_INFO "parport_pc: ITE8873 found (1S)\n");
		return 0;
	case 0x8:
		DPRINTK (KERN_DEBUG "parport_pc: ITE8874 found (2S)\n");
		return 0;
	default:
		printk (KERN_INFO "parport_pc: unknown ITE887x\n");
		printk (KERN_INFO "parport_pc: please mail 'lspci -nvv' "
			"output to Rich.Liu@ite.com.tw\n");
		return 0;
	}

	pci_read_config_byte (pdev, 0x3c, &ite8872_irq);
	pci_read_config_dword (pdev, 0x1c, &ite8872_lpt);
	ite8872_lpt &= 0x0000ff00;
	pci_read_config_dword (pdev, 0x20, &ite8872_lpthi);
	ite8872_lpthi &= 0x0000ff00;
	pci_write_config_dword (pdev, 0x6c, 0xe3000000 | ite8872_lpt);
	pci_write_config_dword (pdev, 0x70, 0xe3000000 | ite8872_lpthi);
	pci_write_config_dword (pdev, 0x80, (ite8872_lpthi<<16) | ite8872_lpt);
	// SET SPP&EPP , Parallel Port NO DMA , Enable All Function
	// SET Parallel IRQ
	pci_write_config_dword (pdev, 0x9c,
				ite8872set | (ite8872_irq * 0x11111));

	DPRINTK (KERN_DEBUG "ITE887x: The IRQ is %d.\n", ite8872_irq);
	DPRINTK (KERN_DEBUG "ITE887x: The PARALLEL I/O port is 0x%x.\n",
		 ite8872_lpt);
	DPRINTK (KERN_DEBUG "ITE887x: The PARALLEL I/O porthi is 0x%x.\n",
		 ite8872_lpthi);

	/* Let the user (or defaults) steer us away from interrupts */
	irq = ite8872_irq;
	if (autoirq != PARPORT_IRQ_AUTO)
		irq = PARPORT_IRQ_NONE;

	/*
	 * Release the resource so that parport_pc_probe_port can get it.
	 */
	release_resource(base_res);
	if (parport_pc_probe_port (ite8872_lpt, ite8872_lpthi,
				   irq, PARPORT_DMA_NONE, NULL)) {
		printk (KERN_INFO
			"parport_pc: ITE 8872 parallel port: io=0x%X",
			ite8872_lpt);
		if (irq != PARPORT_IRQ_NONE)
			printk (", irq=%d", irq);
		printk ("\n");
		return 1;
	}

	return 0;
}

/* VIA 8231 support by Pavel Fedin <sonic_amiga@rambler.ru>
   based on VIA 686a support code by Jeff Garzik <jgarzik@pobox.com> */
static int __devinitdata parport_init_mode = 0;

/* Data for two known VIA chips */
static struct parport_pc_via_data via_686a_data __devinitdata = {
	0x51,
	0x50,
	0x85,
	0x02,
	0xE2,
	0xF0,
	0xE6
};
static struct parport_pc_via_data via_8231_data __devinitdata = {
	0x45,
	0x44,
	0x50,
	0x04,
	0xF2,
	0xFA,
	0xF6
};

static int __devinit sio_via_probe (struct pci_dev *pdev, int autoirq,
				    int autodma,
				    const struct parport_pc_via_data *via)
{
	u8 tmp, tmp2, siofunc;
	u8 ppcontrol = 0;
	int dma, irq;
	unsigned port1, port2;
	unsigned have_epp = 0;

	printk(KERN_DEBUG "parport_pc: VIA 686A/8231 detected\n");

	switch(parport_init_mode)
	{
	case 1:
	    printk(KERN_DEBUG "parport_pc: setting SPP mode\n");
	    siofunc = VIA_FUNCTION_PARPORT_SPP;
	    break;
	case 2:
	    printk(KERN_DEBUG "parport_pc: setting PS/2 mode\n");
	    siofunc = VIA_FUNCTION_PARPORT_SPP;
	    ppcontrol = VIA_PARPORT_BIDIR;
	    break;
	case 3:
	    printk(KERN_DEBUG "parport_pc: setting EPP mode\n");
	    siofunc = VIA_FUNCTION_PARPORT_EPP;
	    ppcontrol = VIA_PARPORT_BIDIR;
	    have_epp = 1;
	    break;
	case 4:
	    printk(KERN_DEBUG "parport_pc: setting ECP mode\n");
	    siofunc = VIA_FUNCTION_PARPORT_ECP;
	    ppcontrol = VIA_PARPORT_BIDIR;
	    break;
	case 5:
	    printk(KERN_DEBUG "parport_pc: setting EPP+ECP mode\n");
	    siofunc = VIA_FUNCTION_PARPORT_ECP;
	    ppcontrol = VIA_PARPORT_BIDIR|VIA_PARPORT_ECPEPP;
	    have_epp = 1;
	    break;
	 default:
	    printk(KERN_DEBUG "parport_pc: probing current configuration\n");
	    siofunc = VIA_FUNCTION_PROBE;
	    break;
	}
	/*
	 * unlock super i/o configuration
	 */
	pci_read_config_byte(pdev, via->via_pci_superio_config_reg, &tmp);
	tmp |= via->via_pci_superio_config_data;
	pci_write_config_byte(pdev, via->via_pci_superio_config_reg, tmp);

	/* Bits 1-0: Parallel Port Mode / Enable */
	outb(via->viacfg_function, VIA_CONFIG_INDEX);
	tmp = inb (VIA_CONFIG_DATA);
	/* Bit 5: EPP+ECP enable; bit 7: PS/2 bidirectional port enable */
	outb(via->viacfg_parport_control, VIA_CONFIG_INDEX);
	tmp2 = inb (VIA_CONFIG_DATA);
	if (siofunc == VIA_FUNCTION_PROBE)
	{
	    siofunc = tmp & VIA_FUNCTION_PARPORT_DISABLE;
	    ppcontrol = tmp2;
	}
	else
	{
	    tmp &= ~VIA_FUNCTION_PARPORT_DISABLE;
	    tmp |= siofunc;
	    outb(via->viacfg_function, VIA_CONFIG_INDEX);
	    outb(tmp, VIA_CONFIG_DATA);
	    tmp2 &= ~(VIA_PARPORT_BIDIR|VIA_PARPORT_ECPEPP);
	    tmp2 |= ppcontrol;
	    outb(via->viacfg_parport_control, VIA_CONFIG_INDEX);
	    outb(tmp2, VIA_CONFIG_DATA);
	}
	
	/* Parallel Port I/O Base Address, bits 9-2 */
	outb(via->viacfg_parport_base, VIA_CONFIG_INDEX);
	port1 = inb(VIA_CONFIG_DATA) << 2;
	
	printk (KERN_DEBUG "parport_pc: Current parallel port base: 0x%X\n",port1);
	if ((port1 == 0x3BC) && have_epp)
	{
	    outb(via->viacfg_parport_base, VIA_CONFIG_INDEX);
	    outb((0x378 >> 2), VIA_CONFIG_DATA);
	    printk(KERN_DEBUG "parport_pc: Parallel port base changed to 0x378\n");
	    port1 = 0x378;
	}

	/*
	 * lock super i/o configuration
	 */
	pci_read_config_byte(pdev, via->via_pci_superio_config_reg, &tmp);
	tmp &= ~via->via_pci_superio_config_data;
	pci_write_config_byte(pdev, via->via_pci_superio_config_reg, tmp);

	if (siofunc == VIA_FUNCTION_PARPORT_DISABLE) {
		printk(KERN_INFO "parport_pc: VIA parallel port disabled in BIOS\n");
		return 0;
	}
	
	/* Bits 7-4: PnP Routing for Parallel Port IRQ */
	pci_read_config_byte(pdev, via->via_pci_parport_irq_reg, &tmp);
	irq = ((tmp & VIA_IRQCONTROL_PARALLEL) >> 4);

	if (siofunc == VIA_FUNCTION_PARPORT_ECP)
	{
	    /* Bits 3-2: PnP Routing for Parallel Port DMA */
	    pci_read_config_byte(pdev, via->via_pci_parport_dma_reg, &tmp);
	    dma = ((tmp & VIA_DMACONTROL_PARALLEL) >> 2);
	}
	else
	    /* if ECP not enabled, DMA is not enabled, assumed bogus 'dma' value */
	    dma = PARPORT_DMA_NONE;

	/* Let the user (or defaults) steer us away from interrupts and DMA */
	if (autoirq == PARPORT_IRQ_NONE) {
	    irq = PARPORT_IRQ_NONE;
	    dma = PARPORT_DMA_NONE;
	}
	if (autodma == PARPORT_DMA_NONE)
	    dma = PARPORT_DMA_NONE;

	switch (port1) {
	case 0x3bc: port2 = 0x7bc; break;
	case 0x378: port2 = 0x778; break;
	case 0x278: port2 = 0x678; break;
	default:
		printk(KERN_INFO "parport_pc: Weird VIA parport base 0x%X, ignoring\n",
			port1);
		return 0;
	}

	/* filter bogus IRQs */
	switch (irq) {
	case 0:
	case 2:
	case 8:
	case 13:
		irq = PARPORT_IRQ_NONE;
		break;

	default: /* do nothing */
		break;
	}

	/* finally, do the probe with values obtained */
	if (parport_pc_probe_port (port1, port2, irq, dma, NULL)) {
		printk (KERN_INFO
			"parport_pc: VIA parallel port: io=0x%X", port1);
		if (irq != PARPORT_IRQ_NONE)
			printk (", irq=%d", irq);
		if (dma != PARPORT_DMA_NONE)
			printk (", dma=%d", dma);
		printk ("\n");
		return 1;
	}
	
	printk(KERN_WARNING "parport_pc: Strange, can't probe VIA parallel port: io=0x%X, irq=%d, dma=%d\n",
		port1, irq, dma);
	return 0;
}


enum parport_pc_sio_types {
	sio_via_686a = 0,	/* Via VT82C686A motherboard Super I/O */
	sio_via_8231,		/* Via VT8231 south bridge integrated Super IO */
	sio_ite_8872,
	last_sio
};

/* each element directly indexed from enum list, above */
static struct parport_pc_superio {
	int (*probe) (struct pci_dev *pdev, int autoirq, int autodma,
		      const struct parport_pc_via_data *via);
	const struct parport_pc_via_data *via;
} parport_pc_superio_info[] __devinitdata = {
	{ sio_via_probe, &via_686a_data, },
	{ sio_via_probe, &via_8231_data, },
	{ sio_ite_8872_probe, NULL, },
};

enum parport_pc_pci_cards {
	siig_1p_10x = last_sio,
	siig_2p_10x,
	siig_1p_20x,
	siig_2p_20x,
	lava_parallel,
	lava_parallel_dual_a,
	lava_parallel_dual_b,
	boca_ioppar,
	plx_9050,
	timedia_4078a,
	timedia_4079h,
	timedia_4085h,
	timedia_4088a,
	timedia_4089a,
	timedia_4095a,
	timedia_4096a,
	timedia_4078u,
	timedia_4079a,
	timedia_4085u,
	timedia_4079r,
	timedia_4079s,
	timedia_4079d,
	timedia_4079e,
	timedia_4079f,
	timedia_9079a,
	timedia_9079b,
	timedia_9079c,
	timedia_4006a,
	timedia_4014,
	timedia_4008a,
	timedia_4018,
	timedia_9018a,
	syba_2p_epp,
	syba_1p_ecp,
	titan_010l,
	titan_1284p1,
	titan_1284p2,
	avlab_1p,
	avlab_2p,
	oxsemi_954,
	oxsemi_840,
	aks_0100,
	mobility_pp,
	netmos_9705,
	netmos_9715,
	netmos_9755,
	netmos_9805,
	netmos_9815,
};


/* each element directly indexed from enum list, above 
 * (but offset by last_sio) */
static struct parport_pc_pci {
	int numports;
	struct { /* BAR (base address registers) numbers in the config
                    space header */
		int lo;
		int hi; /* -1 if not there, >6 for offset-method (max
                           BAR is 6) */
	} addr[4];

	/* If set, this is called immediately after pci_enable_device.
	 * If it returns non-zero, no probing will take place and the
	 * ports will not be used. */
	int (*preinit_hook) (struct pci_dev *pdev, int autoirq, int autodma);

	/* If set, this is called after probing for ports.  If 'failed'
	 * is non-zero we couldn't use any of the ports. */
	void (*postinit_hook) (struct pci_dev *pdev, int failed);
} cards[] __devinitdata = {
	/* siig_1p_10x */		{ 1, { { 2, 3 }, } },
	/* siig_2p_10x */		{ 2, { { 2, 3 }, { 4, 5 }, } },
	/* siig_1p_20x */		{ 1, { { 0, 1 }, } },
	/* siig_2p_20x */		{ 2, { { 0, 1 }, { 2, 3 }, } },
	/* lava_parallel */		{ 1, { { 0, -1 }, } },
	/* lava_parallel_dual_a */	{ 1, { { 0, -1 }, } },
	/* lava_parallel_dual_b */	{ 1, { { 0, -1 }, } },
	/* boca_ioppar */		{ 1, { { 0, -1 }, } },
	/* plx_9050 */			{ 2, { { 4, -1 }, { 5, -1 }, } },
	/* timedia_4078a */		{ 1, { { 2, -1 }, } },
	/* timedia_4079h */             { 1, { { 2, 3 }, } },
	/* timedia_4085h */             { 2, { { 2, -1 }, { 4, -1 }, } },
	/* timedia_4088a */             { 2, { { 2, 3 }, { 4, 5 }, } },
	/* timedia_4089a */             { 2, { { 2, 3 }, { 4, 5 }, } },
	/* timedia_4095a */             { 2, { { 2, 3 }, { 4, 5 }, } },
	/* timedia_4096a */             { 2, { { 2, 3 }, { 4, 5 }, } },
	/* timedia_4078u */             { 1, { { 2, -1 }, } },
	/* timedia_4079a */             { 1, { { 2, 3 }, } },
	/* timedia_4085u */             { 2, { { 2, -1 }, { 4, -1 }, } },
	/* timedia_4079r */             { 1, { { 2, 3 }, } },
	/* timedia_4079s */             { 1, { { 2, 3 }, } },
	/* timedia_4079d */             { 1, { { 2, 3 }, } },
	/* timedia_4079e */             { 1, { { 2, 3 }, } },
	/* timedia_4079f */             { 1, { { 2, 3 }, } },
	/* timedia_9079a */             { 1, { { 2, 3 }, } },
	/* timedia_9079b */             { 1, { { 2, 3 }, } },
	/* timedia_9079c */             { 1, { { 2, 3 }, } },
	/* timedia_4006a */             { 1, { { 0, -1 }, } },
	/* timedia_4014  */             { 2, { { 0, -1 }, { 2, -1 }, } },
	/* timedia_4008a */             { 1, { { 0, 1 }, } },
	/* timedia_4018  */             { 2, { { 0, 1 }, { 2, 3 }, } },
	/* timedia_9018a */             { 2, { { 0, 1 }, { 2, 3 }, } },
					/* SYBA uses fixed offsets in
                                           a 1K io window */
	/* syba_2p_epp AP138B */	{ 2, { { 0, 0x078 }, { 0, 0x178 }, } },
	/* syba_1p_ecp W83787 */	{ 1, { { 0, 0x078 }, } },
	/* titan_010l */		{ 1, { { 3, -1 }, } },
	/* titan_1284p1 */              { 1, { { 0, 1 }, } },
	/* titan_1284p2 */		{ 2, { { 0, 1 }, { 2, 3 }, } },
	/* avlab_1p		*/	{ 1, { { 0, 1}, } },
	/* avlab_2p		*/	{ 2, { { 0, 1}, { 2, 3 },} },
	/* The Oxford Semi cards are unusual: 954 doesn't support ECP,
	 * and 840 locks up if you write 1 to bit 2! */
	/* oxsemi_954 */		{ 1, { { 0, -1 }, } },
	/* oxsemi_840 */		{ 1, { { 0, -1 }, } },
	/* aks_0100 */                  { 1, { { 0, -1 }, } },
	/* mobility_pp */		{ 1, { { 0, 1 }, } },
	/* netmos_9705 */               { 1, { { 0, -1 }, } }, /* untested */
        /* netmos_9715 */               { 2, { { 0, 1 }, { 2, 3 },} }, /* untested */
        /* netmos_9755 */               { 2, { { 0, 1 }, { 2, 3 },} }, /* untested */
	/* netmos_9805 */               { 1, { { 0, -1 }, } }, /* untested */
	/* netmos_9815 */               { 2, { { 0, -1 }, { 2, -1 }, } }, /* untested */
};

static const struct pci_device_id parport_pc_pci_tbl[] = {
	/* Super-IO onboard chips */
	{ 0x1106, 0x0686, PCI_ANY_ID, PCI_ANY_ID, 0, 0, sio_via_686a },
	{ 0x1106, 0x8231, PCI_ANY_ID, PCI_ANY_ID, 0, 0, sio_via_8231 },
	{ PCI_VENDOR_ID_ITE, PCI_DEVICE_ID_ITE_8872,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, sio_ite_8872 },

	/* PCI cards */
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1P_10x,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_1p_10x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2P_10x,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_2p_10x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1P_20x,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_1p_20x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2P_20x,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_2p_20x },
	{ PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_PARALLEL,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, lava_parallel },
	{ PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_DUAL_PAR_A,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, lava_parallel_dual_a },
	{ PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_DUAL_PAR_B,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, lava_parallel_dual_b },
	{ PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_BOCA_IOPPAR,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, boca_ioppar },
	{ PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
	  PCI_SUBVENDOR_ID_EXSYS, PCI_SUBDEVICE_ID_EXSYS_4014, 0,0, plx_9050 },
	/* PCI_VENDOR_ID_TIMEDIA/SUNIX has many differing cards ...*/
	{ 0x1409, 0x7168, 0x1409, 0x4078, 0, 0, timedia_4078a },
	{ 0x1409, 0x7168, 0x1409, 0x4079, 0, 0, timedia_4079h },
	{ 0x1409, 0x7168, 0x1409, 0x4085, 0, 0, timedia_4085h },
	{ 0x1409, 0x7168, 0x1409, 0x4088, 0, 0, timedia_4088a },
	{ 0x1409, 0x7168, 0x1409, 0x4089, 0, 0, timedia_4089a },
	{ 0x1409, 0x7168, 0x1409, 0x4095, 0, 0, timedia_4095a },
	{ 0x1409, 0x7168, 0x1409, 0x4096, 0, 0, timedia_4096a },
	{ 0x1409, 0x7168, 0x1409, 0x5078, 0, 0, timedia_4078u },
	{ 0x1409, 0x7168, 0x1409, 0x5079, 0, 0, timedia_4079a },
	{ 0x1409, 0x7168, 0x1409, 0x5085, 0, 0, timedia_4085u },
	{ 0x1409, 0x7168, 0x1409, 0x6079, 0, 0, timedia_4079r },
	{ 0x1409, 0x7168, 0x1409, 0x7079, 0, 0, timedia_4079s },
	{ 0x1409, 0x7168, 0x1409, 0x8079, 0, 0, timedia_4079d },
	{ 0x1409, 0x7168, 0x1409, 0x9079, 0, 0, timedia_4079e },
	{ 0x1409, 0x7168, 0x1409, 0xa079, 0, 0, timedia_4079f },
	{ 0x1409, 0x7168, 0x1409, 0xb079, 0, 0, timedia_9079a },
	{ 0x1409, 0x7168, 0x1409, 0xc079, 0, 0, timedia_9079b },
	{ 0x1409, 0x7168, 0x1409, 0xd079, 0, 0, timedia_9079c },
	{ 0x1409, 0x7268, 0x1409, 0x0101, 0, 0, timedia_4006a },
	{ 0x1409, 0x7268, 0x1409, 0x0102, 0, 0, timedia_4014 },
	{ 0x1409, 0x7268, 0x1409, 0x0103, 0, 0, timedia_4008a },
	{ 0x1409, 0x7268, 0x1409, 0x0104, 0, 0, timedia_4018 },
	{ 0x1409, 0x7268, 0x1409, 0x9018, 0, 0, timedia_9018a },
	{ 0x14f2, 0x0121, PCI_ANY_ID, PCI_ANY_ID, 0, 0, mobility_pp },
	{ PCI_VENDOR_ID_SYBA, PCI_DEVICE_ID_SYBA_2P_EPP,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, syba_2p_epp },
	{ PCI_VENDOR_ID_SYBA, PCI_DEVICE_ID_SYBA_1P_ECP,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, syba_1p_ecp },
	{ PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_010L,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, titan_010l },
	{ 0x9710, 0x9805, 0x1000, 0x0010, 0, 0, titan_1284p1 },
	{ 0x9710, 0x9815, 0x1000, 0x0020, 0, 0, titan_1284p2 },
	/* PCI_VENDOR_ID_AVLAB/Intek21 has another bunch of cards ...*/
	{ 0x14db, 0x2120, PCI_ANY_ID, PCI_ANY_ID, 0, 0, avlab_1p}, /* AFAVLAB_TK9902 */
	{ 0x14db, 0x2121, PCI_ANY_ID, PCI_ANY_ID, 0, 0, avlab_2p},
	{ PCI_VENDOR_ID_OXSEMI, PCI_DEVICE_ID_OXSEMI_16PCI954PP,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, oxsemi_954 },
	{ PCI_VENDOR_ID_OXSEMI, PCI_DEVICE_ID_OXSEMI_12PCI840,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, oxsemi_840 },
	{ PCI_VENDOR_ID_AKS, PCI_DEVICE_ID_AKS_ALADDINCARD,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, aks_0100 },
	/* NetMos communication controllers */
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9705,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, netmos_9705 },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9715,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, netmos_9715 },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9755,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, netmos_9755 },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9805,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, netmos_9805 },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9815,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, netmos_9815 },
	{ 0, } /* terminate list */
};
MODULE_DEVICE_TABLE(pci,parport_pc_pci_tbl);

struct pci_parport_data {
	int num;
	struct parport *ports[2];
};

static int parport_pc_pci_probe (struct pci_dev *dev,
					   const struct pci_device_id *id)
{
	int err, count, n, i = id->driver_data;
	struct pci_parport_data *data;

	if (i < last_sio)
		/* This is an onboard Super-IO and has already been probed */
		return 0;

	/* This is a PCI card */
	i -= last_sio;
	count = 0;
	if ((err = pci_enable_device (dev)) != 0)
		return err;

	data = kmalloc(sizeof(struct pci_parport_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (cards[i].preinit_hook &&
	    cards[i].preinit_hook (dev, PARPORT_IRQ_NONE, PARPORT_DMA_NONE)) {
		kfree(data);
		return -ENODEV;
	}

	for (n = 0; n < cards[i].numports; n++) {
		int lo = cards[i].addr[n].lo;
		int hi = cards[i].addr[n].hi;
		unsigned long io_lo, io_hi;
		io_lo = pci_resource_start (dev, lo);
		io_hi = 0;
		if ((hi >= 0) && (hi <= 6))
			io_hi = pci_resource_start (dev, hi);
		else if (hi > 6)
			io_lo += hi; /* Reinterpret the meaning of
                                        "hi" as an offset (see SYBA
                                        def.) */
		/* TODO: test if sharing interrupts works */
		printk (KERN_DEBUG "PCI parallel port detected: %04x:%04x, "
			"I/O at %#lx(%#lx)\n",
			parport_pc_pci_tbl[i + last_sio].vendor,
			parport_pc_pci_tbl[i + last_sio].device, io_lo, io_hi);
		data->ports[count] =
			parport_pc_probe_port (io_lo, io_hi, PARPORT_IRQ_NONE,
					       PARPORT_DMA_NONE, dev);
		if (data->ports[count])
			count++;
	}

	data->num = count;

	if (cards[i].postinit_hook)
		cards[i].postinit_hook (dev, count == 0);

	if (count) {
		pci_set_drvdata(dev, data);
		return 0;
	}

	kfree(data);

	return -ENODEV;
}

static void __devexit parport_pc_pci_remove(struct pci_dev *dev)
{
	struct pci_parport_data *data = pci_get_drvdata(dev);
	int i;

	pci_set_drvdata(dev, NULL);

	if (data) {
		for (i = data->num - 1; i >= 0; i--)
			parport_pc_unregister_port(data->ports[i]);

		kfree(data);
	}
}

static struct pci_driver parport_pc_pci_driver = {
	.name		= "parport_pc",
	.id_table	= parport_pc_pci_tbl,
	.probe		= parport_pc_pci_probe,
	.remove		= __devexit_p(parport_pc_pci_remove),
};

static int __init parport_pc_init_superio (int autoirq, int autodma)
{
	const struct pci_device_id *id;
	struct pci_dev *pdev = NULL;
	int ret = 0;

	for_each_pci_dev(pdev) {
		id = pci_match_id(parport_pc_pci_tbl, pdev);
		if (id == NULL || id->driver_data >= last_sio)
			continue;

		if (parport_pc_superio_info[id->driver_data].probe
			(pdev, autoirq, autodma,parport_pc_superio_info[id->driver_data].via)) {
			ret++;
		}
	}

	return ret; /* number of devices found */
}
#else
static struct pci_driver parport_pc_pci_driver;
static int __init parport_pc_init_superio(int autoirq, int autodma) {return 0;}
#endif /* CONFIG_PCI */


static const struct pnp_device_id parport_pc_pnp_tbl[] = {
	/* Standard LPT Printer Port */
	{.id = "PNP0400", .driver_data = 0},
	/* ECP Printer Port */
	{.id = "PNP0401", .driver_data = 0},
	{ }
};

MODULE_DEVICE_TABLE(pnp,parport_pc_pnp_tbl);

static int parport_pc_pnp_probe(struct pnp_dev *dev, const struct pnp_device_id *id)
{
	struct parport *pdata;
	unsigned long io_lo, io_hi;
	int dma, irq;

	if (pnp_port_valid(dev,0) &&
		!(pnp_port_flags(dev,0) & IORESOURCE_DISABLED)) {
		io_lo = pnp_port_start(dev,0);
	} else
		return -EINVAL;

	if (pnp_port_valid(dev,1) &&
		!(pnp_port_flags(dev,1) & IORESOURCE_DISABLED)) {
		io_hi = pnp_port_start(dev,1);
	} else
		io_hi = 0;

	if (pnp_irq_valid(dev,0) &&
		!(pnp_irq_flags(dev,0) & IORESOURCE_DISABLED)) {
		irq = pnp_irq(dev,0);
	} else
		irq = PARPORT_IRQ_NONE;

	if (pnp_dma_valid(dev,0) &&
		!(pnp_dma_flags(dev,0) & IORESOURCE_DISABLED)) {
		dma = pnp_dma(dev,0);
	} else
		dma = PARPORT_DMA_NONE;

	printk(KERN_INFO "parport: PnPBIOS parport detected.\n");
	if (!(pdata = parport_pc_probe_port (io_lo, io_hi, irq, dma, NULL)))
		return -ENODEV;

	pnp_set_drvdata(dev,pdata);
	return 0;
}

static void parport_pc_pnp_remove(struct pnp_dev *dev)
{
	struct parport *pdata = (struct parport *)pnp_get_drvdata(dev);
	if (!pdata)
		return;

	parport_pc_unregister_port(pdata);
}

/* we only need the pnp layer to activate the device, at least for now */
static struct pnp_driver parport_pc_pnp_driver = {
	.name		= "parport_pc",
	.id_table	= parport_pc_pnp_tbl,
	.probe		= parport_pc_pnp_probe,
	.remove		= parport_pc_pnp_remove,
};


/* This is called by parport_pc_find_nonpci_ports (in asm/parport.h) */
static int __devinit __attribute__((unused))
parport_pc_find_isa_ports (int autoirq, int autodma)
{
	int count = 0;

	if (parport_pc_probe_port(0x3bc, 0x7bc, autoirq, autodma, NULL))
		count++;
	if (parport_pc_probe_port(0x378, 0x778, autoirq, autodma, NULL))
		count++;
	if (parport_pc_probe_port(0x278, 0x678, autoirq, autodma, NULL))
		count++;

	return count;
}

/* This function is called by parport_pc_init if the user didn't
 * specify any ports to probe.  Its job is to find some ports.  Order
 * is important here -- we want ISA ports to be registered first,
 * followed by PCI cards (for least surprise), but before that we want
 * to do chipset-specific tests for some onboard ports that we know
 * about.
 *
 * autoirq is PARPORT_IRQ_NONE, PARPORT_IRQ_AUTO, or PARPORT_IRQ_PROBEONLY
 * autodma is PARPORT_DMA_NONE or PARPORT_DMA_AUTO
 */
static void __init parport_pc_find_ports (int autoirq, int autodma)
{
	int count = 0, err;

#ifdef CONFIG_PARPORT_PC_SUPERIO
	detect_and_report_winbond ();
	detect_and_report_smsc ();
#endif

	/* Onboard SuperIO chipsets that show themselves on the PCI bus. */
	count += parport_pc_init_superio (autoirq, autodma);

	/* PnP ports, skip detection if SuperIO already found them */
	if (!count) {
		err = pnp_register_driver (&parport_pc_pnp_driver);
		if (!err)
			pnp_registered_parport = 1;
	}

	/* ISA ports and whatever (see asm/parport.h). */
	parport_pc_find_nonpci_ports (autoirq, autodma);

	err = pci_register_driver (&parport_pc_pci_driver);
	if (!err)
		pci_registered_parport = 1;
}

/*
 *	Piles of crap below pretend to be a parser for module and kernel
 *	parameters.  Say "thank you" to whoever had come up with that
 *	syntax and keep in mind that code below is a cleaned up version.
 */

static int __initdata io[PARPORT_PC_MAX_PORTS+1] = { [0 ... PARPORT_PC_MAX_PORTS] = 0 };
static int __initdata io_hi[PARPORT_PC_MAX_PORTS+1] =
	{ [0 ... PARPORT_PC_MAX_PORTS] = PARPORT_IOHI_AUTO };
static int __initdata dmaval[PARPORT_PC_MAX_PORTS] = { [0 ... PARPORT_PC_MAX_PORTS-1] = PARPORT_DMA_NONE };
static int __initdata irqval[PARPORT_PC_MAX_PORTS] = { [0 ... PARPORT_PC_MAX_PORTS-1] = PARPORT_IRQ_PROBEONLY };

static int __init parport_parse_param(const char *s, int *val,
				int automatic, int none, int nofifo)
{
	if (!s)
		return 0;
	if (!strncmp(s, "auto", 4))
		*val = automatic;
	else if (!strncmp(s, "none", 4))
		*val = none;
	else if (nofifo && !strncmp(s, "nofifo", 4))
		*val = nofifo;
	else {
		char *ep;
		unsigned long r = simple_strtoul(s, &ep, 0);
		if (ep != s)
			*val = r;
		else {
			printk(KERN_ERR "parport: bad specifier `%s'\n", s);
			return -1;
		}
	}
	return 0;
}

static int __init parport_parse_irq(const char *irqstr, int *val)
{
	return parport_parse_param(irqstr, val, PARPORT_IRQ_AUTO,
				     PARPORT_IRQ_NONE, 0);
}

static int __init parport_parse_dma(const char *dmastr, int *val)
{
	return parport_parse_param(dmastr, val, PARPORT_DMA_AUTO,
				     PARPORT_DMA_NONE, PARPORT_DMA_NOFIFO);
}

#ifdef CONFIG_PCI
static int __init parport_init_mode_setup(char *str)
{
	printk(KERN_DEBUG "parport_pc.c: Specified parameter parport_init_mode=%s\n", str);

	if (!strcmp (str, "spp"))
		parport_init_mode=1;
	if (!strcmp (str, "ps2"))
		parport_init_mode=2;
	if (!strcmp (str, "epp"))
		parport_init_mode=3;
	if (!strcmp (str, "ecp"))
		parport_init_mode=4;
	if (!strcmp (str, "ecpepp"))
		parport_init_mode=5;
	return 1;
}
#endif

#ifdef MODULE
static const char *irq[PARPORT_PC_MAX_PORTS];
static const char *dma[PARPORT_PC_MAX_PORTS];

MODULE_PARM_DESC(io, "Base I/O address (SPP regs)");
module_param_array(io, int, NULL, 0);
MODULE_PARM_DESC(io_hi, "Base I/O address (ECR)");
module_param_array(io_hi, int, NULL, 0);
MODULE_PARM_DESC(irq, "IRQ line");
module_param_array(irq, charp, NULL, 0);
MODULE_PARM_DESC(dma, "DMA channel");
module_param_array(dma, charp, NULL, 0);
#if defined(CONFIG_PARPORT_PC_SUPERIO) || \
       (defined(CONFIG_PARPORT_1284) && defined(CONFIG_PARPORT_PC_FIFO))
MODULE_PARM_DESC(verbose_probing, "Log chit-chat during initialisation");
module_param(verbose_probing, int, 0644);
#endif
#ifdef CONFIG_PCI
static char *init_mode;
MODULE_PARM_DESC(init_mode, "Initialise mode for VIA VT8231 port (spp, ps2, epp, ecp or ecpepp)");
module_param(init_mode, charp, 0);
#endif

static int __init parse_parport_params(void)
{
	unsigned int i;
	int val;

#ifdef CONFIG_PCI
	if (init_mode)
		parport_init_mode_setup(init_mode);
#endif

	for (i = 0; i < PARPORT_PC_MAX_PORTS && io[i]; i++) {
		if (parport_parse_irq(irq[i], &val))
			return 1;
		irqval[i] = val;
		if (parport_parse_dma(dma[i], &val))
			return 1;
		dmaval[i] = val;
	}
	if (!io[0]) {
		/* The user can make us use any IRQs or DMAs we find. */
		if (irq[0] && !parport_parse_irq(irq[0], &val))
			switch (val) {
			case PARPORT_IRQ_NONE:
			case PARPORT_IRQ_AUTO:
				irqval[0] = val;
				break;
			default:
				printk (KERN_WARNING
					"parport_pc: irq specified "
					"without base address.  Use 'io=' "
					"to specify one\n");
			}

		if (dma[0] && !parport_parse_dma(dma[0], &val))
			switch (val) {
			case PARPORT_DMA_NONE:
			case PARPORT_DMA_AUTO:
				dmaval[0] = val;
				break;
			default:
				printk (KERN_WARNING
					"parport_pc: dma specified "
					"without base address.  Use 'io=' "
					"to specify one\n");
			}
	}
	return 0;
}

#else

static int parport_setup_ptr __initdata = 0;

/*
 * Acceptable parameters:
 *
 * parport=0
 * parport=auto
 * parport=0xBASE[,IRQ[,DMA]]
 *
 * IRQ/DMA may be numeric or 'auto' or 'none'
 */
static int __init parport_setup (char *str)
{
	char *endptr;
	char *sep;
	int val;

	if (!str || !*str || (*str == '0' && !*(str+1))) {
		/* Disable parport if "parport=0" in cmdline */
		io[0] = PARPORT_DISABLE;
		return 1;
	}

	if (!strncmp (str, "auto", 4)) {
		irqval[0] = PARPORT_IRQ_AUTO;
		dmaval[0] = PARPORT_DMA_AUTO;
		return 1;
	}

	val = simple_strtoul (str, &endptr, 0);
	if (endptr == str) {
		printk (KERN_WARNING "parport=%s not understood\n", str);
		return 1;
	}

	if (parport_setup_ptr == PARPORT_PC_MAX_PORTS) {
		printk(KERN_ERR "parport=%s ignored, too many ports\n", str);
		return 1;
	}

	io[parport_setup_ptr] = val;
	irqval[parport_setup_ptr] = PARPORT_IRQ_NONE;
	dmaval[parport_setup_ptr] = PARPORT_DMA_NONE;

	sep = strchr(str, ',');
	if (sep++) {
		if (parport_parse_irq(sep, &val))
			return 1;
		irqval[parport_setup_ptr] = val;
		sep = strchr(sep, ',');
		if (sep++) {
			if (parport_parse_dma(sep, &val))
				return 1;
			dmaval[parport_setup_ptr] = val;
		}
	}
	parport_setup_ptr++;
	return 1;
}

static int __init parse_parport_params(void)
{
	return io[0] == PARPORT_DISABLE;
}

__setup ("parport=", parport_setup);

/*
 * Acceptable parameters:
 *
 * parport_init_mode=[spp|ps2|epp|ecp|ecpepp]
 */
#ifdef CONFIG_PCI
__setup("parport_init_mode=",parport_init_mode_setup);
#endif
#endif

/* "Parser" ends here */

static int __init parport_pc_init(void)
{
	if (parse_parport_params())
		return -EINVAL;

	if (io[0]) {
		int i;
		/* Only probe the ports we were given. */
		user_specified = 1;
		for (i = 0; i < PARPORT_PC_MAX_PORTS; i++) {
			if (!io[i])
				break;
			if ((io_hi[i]) == PARPORT_IOHI_AUTO)
			       io_hi[i] = 0x400 + io[i];
			parport_pc_probe_port(io[i], io_hi[i],
						  irqval[i], dmaval[i], NULL);
		}
	} else
		parport_pc_find_ports (irqval[0], dmaval[0]);

	return 0;
}

static void __exit parport_pc_exit(void)
{
	if (pci_registered_parport)
		pci_unregister_driver (&parport_pc_pci_driver);
	if (pnp_registered_parport)
		pnp_unregister_driver (&parport_pc_pnp_driver);

	spin_lock(&ports_lock);
	while (!list_empty(&ports_list)) {
		struct parport_pc_private *priv;
		struct parport *port;
		priv = list_entry(ports_list.next,
				  struct parport_pc_private, list);
		port = priv->port;
		spin_unlock(&ports_lock);
		parport_pc_unregister_port(port);
		spin_lock(&ports_lock);
	}
	spin_unlock(&ports_lock);
}

MODULE_AUTHOR("Phil Blundell, Tim Waugh, others");
MODULE_DESCRIPTION("PC-style parallel port driver");
MODULE_LICENSE("GPL");
module_init(parport_pc_init)
module_exit(parport_pc_exit)

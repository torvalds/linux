/*
 * For License see notice in hfc_multi.c
 *
 * special IO and init functions for the embedded XHFC board
 * from Speech Design
 *
 */

#include <asm/8xx_immap.h>

/* Change this to the value used by your board */
#ifndef IMAP_ADDR
#define IMAP_ADDR	0xFFF00000
#endif

static void
#ifdef HFC_REGISTER_DEBUG
HFC_outb_embsd(struct hfc_multi *hc, u_char reg, u_char val,
	       const char *function, int line)
#else
	HFC_outb_embsd(struct hfc_multi *hc, u_char reg, u_char val)
#endif
{
	hc->immap->im_ioport.iop_padat |= PA_XHFC_A0;
	writeb(reg, hc->xhfc_memaddr);
	hc->immap->im_ioport.iop_padat &= ~(PA_XHFC_A0);
	writeb(val, hc->xhfc_memdata);
}
static u_char
#ifdef HFC_REGISTER_DEBUG
HFC_inb_embsd(struct hfc_multi *hc, u_char reg, const char *function, int line)
#else
	HFC_inb_embsd(struct hfc_multi *hc, u_char reg)
#endif
{
	hc->immap->im_ioport.iop_padat |= PA_XHFC_A0;
	writeb(reg, hc->xhfc_memaddr);
	hc->immap->im_ioport.iop_padat &= ~(PA_XHFC_A0);
	return readb(hc->xhfc_memdata);
}
static u_short
#ifdef HFC_REGISTER_DEBUG
HFC_inw_embsd(struct hfc_multi *hc, u_char reg, const char *function, int line)
#else
	HFC_inw_embsd(struct hfc_multi *hc, u_char reg)
#endif
{
	hc->immap->im_ioport.iop_padat |= PA_XHFC_A0;
	writeb(reg, hc->xhfc_memaddr);
	hc->immap->im_ioport.iop_padat &= ~(PA_XHFC_A0);
	return readb(hc->xhfc_memdata);
}
static void
#ifdef HFC_REGISTER_DEBUG
HFC_wait_embsd(struct hfc_multi *hc, const char *function, int line)
#else
	HFC_wait_embsd(struct hfc_multi *hc)
#endif
{
	hc->immap->im_ioport.iop_padat |= PA_XHFC_A0;
	writeb(R_STATUS, hc->xhfc_memaddr);
	hc->immap->im_ioport.iop_padat &= ~(PA_XHFC_A0);
	while (readb(hc->xhfc_memdata) & V_BUSY)
		cpu_relax();
}

/* write fifo data (EMBSD) */
void
write_fifo_embsd(struct hfc_multi *hc, u_char *data, int len)
{
	hc->immap->im_ioport.iop_padat |= PA_XHFC_A0;
	*hc->xhfc_memaddr = A_FIFO_DATA0;
	hc->immap->im_ioport.iop_padat &= ~(PA_XHFC_A0);
	while (len) {
		*hc->xhfc_memdata = *data;
		data++;
		len--;
	}
}

/* read fifo data (EMBSD) */
void
read_fifo_embsd(struct hfc_multi *hc, u_char *data, int len)
{
	hc->immap->im_ioport.iop_padat |= PA_XHFC_A0;
	*hc->xhfc_memaddr = A_FIFO_DATA0;
	hc->immap->im_ioport.iop_padat &= ~(PA_XHFC_A0);
	while (len) {
		*data = (u_char)(*hc->xhfc_memdata);
		data++;
		len--;
	}
}

static int
setup_embedded(struct hfc_multi *hc, struct hm_map *m)
{
	printk(KERN_INFO
	       "HFC-multi: card manufacturer: '%s' card name: '%s' clock: %s\n",
	       m->vendor_name, m->card_name, m->clock2 ? "double" : "normal");

	hc->pci_dev = NULL;
	if (m->clock2)
		test_and_set_bit(HFC_CHIP_CLOCK2, &hc->chip);

	hc->leds = m->leds;
	hc->ledstate = 0xAFFEAFFE;
	hc->opticalsupport = m->opticalsupport;

	hc->pci_iobase = 0;
	hc->pci_membase = 0;
	hc->xhfc_membase = NULL;
	hc->xhfc_memaddr = NULL;
	hc->xhfc_memdata = NULL;

	/* set memory access methods */
	if (m->io_mode) /* use mode from card config */
		hc->io_mode = m->io_mode;
	switch (hc->io_mode) {
	case HFC_IO_MODE_EMBSD:
		test_and_set_bit(HFC_CHIP_EMBSD, &hc->chip);
		hc->slots = 128; /* required */
		/* fall through */
		hc->HFC_outb = HFC_outb_embsd;
		hc->HFC_inb = HFC_inb_embsd;
		hc->HFC_inw = HFC_inw_embsd;
		hc->HFC_wait = HFC_wait_embsd;
		hc->read_fifo = read_fifo_embsd;
		hc->write_fifo = write_fifo_embsd;
		hc->xhfc_origmembase = XHFC_MEMBASE + XHFC_OFFSET * hc->id;
		hc->xhfc_membase = (u_char *)ioremap(hc->xhfc_origmembase,
						     XHFC_MEMSIZE);
		if (!hc->xhfc_membase) {
			printk(KERN_WARNING
			       "HFC-multi: failed to remap xhfc address space. "
			       "(internal error)\n");
			return -EIO;
		}
		hc->xhfc_memaddr = (u_long *)(hc->xhfc_membase + 4);
		hc->xhfc_memdata = (u_long *)(hc->xhfc_membase);
		printk(KERN_INFO
		       "HFC-multi: xhfc_membase:%#lx xhfc_origmembase:%#lx "
		       "xhfc_memaddr:%#lx xhfc_memdata:%#lx\n",
		       (u_long)hc->xhfc_membase, hc->xhfc_origmembase,
		       (u_long)hc->xhfc_memaddr, (u_long)hc->xhfc_memdata);
		break;
	default:
		printk(KERN_WARNING "HFC-multi: Invalid IO mode.\n");
		return -EIO;
	}

	/* Prepare the MPC8XX PortA 10 as output (address/data selector) */
	hc->immap = (struct immap *)(IMAP_ADDR);
	hc->immap->im_ioport.iop_papar &= ~(PA_XHFC_A0);
	hc->immap->im_ioport.iop_paodr &= ~(PA_XHFC_A0);
	hc->immap->im_ioport.iop_padir |=   PA_XHFC_A0;

	/* Prepare the MPC8xx PortB __X__ as input (ISDN__X__IRQ) */
	hc->pb_irqmsk = (PB_XHFC_IRQ1 << hc->id);
	hc->immap->im_cpm.cp_pbpar &= ~(hc->pb_irqmsk);
	hc->immap->im_cpm.cp_pbodr &= ~(hc->pb_irqmsk);
	hc->immap->im_cpm.cp_pbdir &= ~(hc->pb_irqmsk);

	/* At this point the needed config is done */
	/* fifos are still not enabled */
	return 0;
}

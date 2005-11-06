/*
 * $Id: hd64465_ss.c,v 1.7 2003/07/06 14:42:50 lethal Exp $
 *
 * Device driver for the PCMCIA controller module of the
 * Hitachi HD64465 handheld companion chip.
 *
 * Note that the HD64465 provides a very thin PCMCIA host bridge
 * layer, requiring a lot of the work of supporting cards to be
 * performed by the processor.  For example: mapping of card
 * interrupts to processor IRQs is done by IRQ demuxing software;
 * IO and memory mappings are fixed; setting voltages according
 * to card Voltage Select pins etc is done in software.
 *
 * Note also that this driver uses only the simple, fixed,
 * 16MB, 16-bit wide mappings to PCMCIA spaces defined by the
 * HD64465.  Larger mappings, smaller mappings, or mappings of
 * different width to the same socket, are all possible only by
 * involving the SH7750's MMU, which is considered unnecessary here.
 * The downside is that it may be possible for some drivers to
 * break because they need or expect 8-bit mappings.
 *
 * This driver currently supports only the following configuration:
 * SH7750 CPU, HD64465, TPS2206 voltage control chip.
 *
 * by Greg Banks <gbanks@pocketpenguins.com>
 * (c) 2000 PocketPenguins Inc
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <asm/errno.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/hd64465/hd64465.h>
#include <asm/hd64465/io.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/ss.h>
#include <pcmcia/bulkmem.h>
#include "cs_internal.h"

#define MODNAME "hd64465_ss"

/* #define HD64465_DEBUG 1 */

#if HD64465_DEBUG
#define DPRINTK(args...)	printk(MODNAME ": " args)
#else
#define DPRINTK(args...)
#endif

extern int hd64465_io_debug;
extern void * p3_ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags);
extern void p3_iounmap(void *addr);

/*============================================================*/

#define HS_IO_MAP_SIZE 	(64*1024)

typedef struct hs_socket_t
{
    unsigned int	number;
    u_int   	    	irq;
    u_long  	    	mem_base;
    void		*io_base;
    u_long  	    	mem_length;
    u_int   	    	ctrl_base;
    socket_state_t  	state;
    pccard_io_map     	io_maps[MAX_IO_WIN];
    pccard_mem_map  	mem_maps[MAX_WIN];
    struct pcmcia_socket	socket;
} hs_socket_t;



#define HS_MAX_SOCKETS 2
static hs_socket_t hs_sockets[HS_MAX_SOCKETS];

#define hs_in(sp, r)	    inb((sp)->ctrl_base + (r))
#define hs_out(sp, v, r)    outb(v, (sp)->ctrl_base + (r))


/* translate a boolean value to a bit in a register */
#define bool_to_regbit(sp, r, bi, bo)	    	\
    do {    	    	    	    	    	\
    	unsigned short v = hs_in(sp, r);    	\
	if (bo)     	    	    	    	\
	    v |= (bi);	    	    	    	\
	else	    	    	    	    	\
	    v &= ~(bi);     	    	    	\
	hs_out(sp, v, r);   	    	    	\
    } while(0)
    
/* register offsets from HD64465_REG_PCC[01]ISR */
#define ISR 	0x0
#define GCR 	0x2
#define CSCR 	0x4
#define CSCIER 	0x6
#define SCR 	0x8


/* Mask and values for CSCIER register */
#define IER_MASK    0x80
#define IER_ON	    0x3f    	/* interrupts on */
#define IER_OFF     0x00    	/* interrupts off */

/*============================================================*/

#if HD64465_DEBUG > 10

static void cis_hex_dump(const unsigned char *x, int len)
{
    	int i;
	
    	for (i=0 ; i<len ; i++)
	{
	    if (!(i & 0xf))
	    	printk("\n%08x", (unsigned)(x + i));
	    printk(" %02x", *(volatile unsigned short*)x);
	    x += 2;
	}
	printk("\n");
}

#endif
/*============================================================*/

/*
 * This code helps create the illusion that the IREQ line from
 * the PC card is mapped to one of the CPU's IRQ lines by the
 * host bridge hardware (which is how every host bridge *except*
 * the HD64465 works).  In particular, it supports enabling
 * and disabling the IREQ line by code which knows nothing
 * about the host bridge (e.g. device drivers, IDE code) using
 * the request_irq(), free_irq(), probe_irq_on() and probe_irq_off()
 * functions.  Also, it supports sharing the mapped IRQ with
 * real hardware IRQs from the -IRL0-3 lines.
 */

#define HS_NUM_MAPPED_IRQS  16	/* Limitation of the PCMCIA code */
static struct
{
    /* index is mapped irq number */
    hs_socket_t *sock;
    hw_irq_controller *old_handler;
} hs_mapped_irq[HS_NUM_MAPPED_IRQS];

static void hs_socket_enable_ireq(hs_socket_t *sp)
{
    	unsigned short cscier;
	
    	DPRINTK("hs_socket_enable_ireq(sock=%d)\n", sp->number);

    	cscier = hs_in(sp, CSCIER);
	cscier &= ~HD64465_PCCCSCIER_PIREQE_MASK;
    	cscier |= HD64465_PCCCSCIER_PIREQE_LEVEL;
	hs_out(sp, cscier, CSCIER);
}

static void hs_socket_disable_ireq(hs_socket_t *sp)
{
    	unsigned short cscier;
	
    	DPRINTK("hs_socket_disable_ireq(sock=%d)\n", sp->number);
	
    	cscier = hs_in(sp, CSCIER);
	cscier &= ~HD64465_PCCCSCIER_PIREQE_MASK;
	hs_out(sp, cscier, CSCIER);
}

static unsigned int hs_startup_irq(unsigned int irq)
{
	hs_socket_enable_ireq(hs_mapped_irq[irq].sock);
	hs_mapped_irq[irq].old_handler->startup(irq);
	return 0;
}

static void hs_shutdown_irq(unsigned int irq)
{
	hs_socket_disable_ireq(hs_mapped_irq[irq].sock);
	hs_mapped_irq[irq].old_handler->shutdown(irq);
}

static void hs_enable_irq(unsigned int irq)
{
	hs_socket_enable_ireq(hs_mapped_irq[irq].sock);
	hs_mapped_irq[irq].old_handler->enable(irq);
}

static void hs_disable_irq(unsigned int irq)
{
	hs_socket_disable_ireq(hs_mapped_irq[irq].sock);
	hs_mapped_irq[irq].old_handler->disable(irq);
}

extern struct hw_interrupt_type no_irq_type;

static void hs_mask_and_ack_irq(unsigned int irq)
{
	hs_socket_disable_ireq(hs_mapped_irq[irq].sock);
	/* ack_none() spuriously complains about an unexpected IRQ */
	if (hs_mapped_irq[irq].old_handler != &no_irq_type)
	    hs_mapped_irq[irq].old_handler->ack(irq);
}

static void hs_end_irq(unsigned int irq)
{
	hs_socket_enable_ireq(hs_mapped_irq[irq].sock);
	hs_mapped_irq[irq].old_handler->end(irq);
}


static struct hw_interrupt_type hd64465_ss_irq_type = {
	.typename	= "PCMCIA-IRQ",
	.startup	= hs_startup_irq,
	.shutdown	= hs_shutdown_irq,
	.enable		= hs_enable_irq,
	.disable	= hs_disable_irq,
	.ack		= hs_mask_and_ack_irq,
	.end		= hs_end_irq
};

/* 
 * This function should only ever be called with interrupts disabled.
 */
static void hs_map_irq(hs_socket_t *sp, unsigned int irq)
{
    	DPRINTK("hs_map_irq(sock=%d irq=%d)\n", sp->number, irq);
	
	if (irq >= HS_NUM_MAPPED_IRQS)
	    return;

    	hs_mapped_irq[irq].sock = sp;
	/* insert ourselves as the irq controller */
	hs_mapped_irq[irq].old_handler = irq_desc[irq].handler;
	irq_desc[irq].handler = &hd64465_ss_irq_type;
}


/* 
 * This function should only ever be called with interrupts disabled.
 */
static void hs_unmap_irq(hs_socket_t *sp, unsigned int irq)
{
    	DPRINTK("hs_unmap_irq(sock=%d irq=%d)\n", sp->number, irq);
	
	if (irq >= HS_NUM_MAPPED_IRQS)
	    return;
		
	/* restore the original irq controller */
	irq_desc[irq].handler = hs_mapped_irq[irq].old_handler;
}

/*============================================================*/


/*
 * Set Vpp and Vcc (in tenths of a Volt).  Does not
 * support the hi-Z state.
 *
 * Note, this assumes the board uses a TPS2206 chip to control
 * the Vcc and Vpp voltages to the hs_sockets.  If your board
 * uses the MIC2563 (also supported by the HD64465) then you
 * will have to modify this function.
 */
    	    	    	    	         /* 0V   3.3V  5.5V */
static const u_char hs_tps2206_avcc[3] = { 0x00, 0x04, 0x08 };
static const u_char hs_tps2206_bvcc[3] = { 0x00, 0x80, 0x40 };

static int hs_set_voltages(hs_socket_t *sp, int Vcc, int Vpp)
{
    	u_int psr;
	u_int vcci = 0;
	u_int sock = sp->number;
	
    	DPRINTK("hs_set_voltage(%d, %d, %d)\n", sock, Vcc, Vpp);

    	switch (Vcc)
	{
	case 0:  vcci = 0; break;
	case 33: vcci = 1; break;
	case 50: vcci = 2; break;
	default: return 0;
	}

    	/* Note: Vpp = 120 not supported -- Greg Banks */
	if (Vpp != 0 && Vpp != Vcc)
	    return 0;
	
	/* The PSR register holds 8 of the 9 bits which control
	 * the TPS2206 via its serial interface.
	 */
	psr = inw(HD64465_REG_PCCPSR);
	switch (sock)
	{
	case 0:
	    psr &= 0x0f;
	    psr |= hs_tps2206_avcc[vcci];
	    psr |= (Vpp == 0 ? 0x00 : 0x02);
	    break;
	case 1:
	    psr &= 0xf0;
	    psr |= hs_tps2206_bvcc[vcci];
	    psr |= (Vpp == 0 ? 0x00 : 0x20);
	    break;
	};
	outw(psr, HD64465_REG_PCCPSR);
	
	return 1;
}


/*============================================================*/

/*
 * Drive the RESET line to the card.
 */
static void hs_reset_socket(hs_socket_t *sp, int on)
{
    	unsigned short v;
	
	v = hs_in(sp, GCR);
	if (on)
	    v |= HD64465_PCCGCR_PCCR;
	else
	    v &= ~HD64465_PCCGCR_PCCR;
	hs_out(sp, v, GCR);
}

/*============================================================*/

static int hs_init(struct pcmcia_socket *s)
{
    	hs_socket_t *sp = container_of(s, struct hs_socket_t, socket);
	
    	DPRINTK("hs_init(%d)\n", sp->number);

	return 0;
}

/*============================================================*/


static int hs_get_status(struct pcmcia_socket *s, u_int *value)
{
    	hs_socket_t *sp = container_of(s, struct hs_socket_t, socket);
    	unsigned int isr;
	u_int status = 0;
	
	
	isr = hs_in(sp, ISR);

    	/* Card is seated and powered when *both* CD pins are low */
	if ((isr & HD64465_PCCISR_PCD_MASK) == 0)
    	{
	    status |= SS_DETECT;    /* card present */

	    switch (isr & HD64465_PCCISR_PBVD_MASK)
	    {
	    case HD64465_PCCISR_PBVD_BATGOOD:   
		break;
	    case HD64465_PCCISR_PBVD_BATWARN:
		status |= SS_BATWARN;
		break;
	    default:
		status |= SS_BATDEAD;
		break;
	    }

	    if (isr & HD64465_PCCISR_PREADY)
		status |= SS_READY;

	    if (isr & HD64465_PCCISR_PMWP)
		status |= SS_WRPROT;
		
	    /* Voltage Select pins interpreted as per Table 4-5 of the std.
	     * Assuming we have the TPS2206, the socket is a "Low Voltage
	     * key, 3.3V and 5V available, no X.XV available".
	     */
	    switch (isr & (HD64465_PCCISR_PVS2|HD64465_PCCISR_PVS1))
	    {
	    case HD64465_PCCISR_PVS1:
	    	printk(KERN_NOTICE MODNAME ": cannot handle X.XV card, ignored\n");
		status = 0;
	    	break;
	    case 0:
	    case HD64465_PCCISR_PVS2:
    	    	/* 3.3V */
    	    	status |= SS_3VCARD;
	    	break;
	    case HD64465_PCCISR_PVS2|HD64465_PCCISR_PVS1:
	    	/* 5V */
	    	break;
	    }
		
	    /* TODO: SS_POWERON */
	    /* TODO: SS_STSCHG */
    	}	
	
    	DPRINTK("hs_get_status(%d) = %x\n", sock, status);
	
	*value = status;
	return 0;
}

/*============================================================*/

static int hs_get_socket(struct pcmcia_socket *s, socket_state_t *state)
{
    	hs_socket_t *sp = container_of(s, struct hs_socket_t, socket);

    	DPRINTK("hs_get_socket(%d)\n", sock);
	
	*state = sp->state;
	return 0;
}

/*============================================================*/

static int hs_set_socket(struct pcmcia_socket *s, socket_state_t *state)
{
    	hs_socket_t *sp = container_of(s, struct hs_socket_t, socket);
    	u_long flags;
	u_int changed;
	unsigned short cscier;

    	DPRINTK("hs_set_socket(sock=%d, flags=%x, csc_mask=%x, Vcc=%d, Vpp=%d, io_irq=%d)\n",
	    sock, state->flags, state->csc_mask, state->Vcc, state->Vpp, state->io_irq);
	
	local_irq_save(flags);	/* Don't want interrupts happening here */

	if (state->Vpp != sp->state.Vpp ||
	    state->Vcc != sp->state.Vcc) {
	    if (!hs_set_voltages(sp, state->Vcc, state->Vpp)) {
	    	local_irq_restore(flags);
	    	return -EINVAL;
	    }
	}

/*    	hd64465_io_debug = 1; */
    	/*
	 * Handle changes in the Card Status Change mask,
	 * by propagating to the CSCR register
	 */	
	changed = sp->state.csc_mask ^ state->csc_mask;
	cscier = hs_in(sp, CSCIER);
	    
	if (changed & SS_DETECT) {
	    if (state->csc_mask & SS_DETECT)
		cscier |= HD64465_PCCCSCIER_PCDE;
	    else
		cscier &= ~HD64465_PCCCSCIER_PCDE;
	}

	if (changed & SS_READY) {
	    if (state->csc_mask & SS_READY)
		cscier |= HD64465_PCCCSCIER_PRE;
	    else
		cscier &= ~HD64465_PCCCSCIER_PRE;
	}

	if (changed & SS_BATDEAD) {
	    if (state->csc_mask & SS_BATDEAD)
		cscier |= HD64465_PCCCSCIER_PBDE;
	    else
		cscier &= ~HD64465_PCCCSCIER_PBDE;
	}

	if (changed & SS_BATWARN) {
	    if (state->csc_mask & SS_BATWARN)
		cscier |= HD64465_PCCCSCIER_PBWE;
	    else
		cscier &= ~HD64465_PCCCSCIER_PBWE;
	}

	if (changed & SS_STSCHG) {
	    if (state->csc_mask & SS_STSCHG)
		cscier |= HD64465_PCCCSCIER_PSCE;
	    else
		cscier &= ~HD64465_PCCCSCIER_PSCE;
	}

    	hs_out(sp, cscier, CSCIER);

	if (sp->state.io_irq && !state->io_irq)
	    hs_unmap_irq(sp, sp->state.io_irq);
	else if (!sp->state.io_irq && state->io_irq)
	    hs_map_irq(sp, state->io_irq);


    	/*
	 * Handle changes in the flags field,
	 * by propagating to config registers.
	 */	
	changed = sp->state.flags ^ state->flags;

	if (changed & SS_IOCARD) {
	    DPRINTK("card type: %s\n",
		    (state->flags & SS_IOCARD ? "i/o" : "memory" ));
	    bool_to_regbit(sp, GCR, HD64465_PCCGCR_PCCT,
		state->flags & SS_IOCARD);
	}

	if (changed & SS_RESET) {
	    DPRINTK("%s reset card\n",
		(state->flags & SS_RESET ? "start" : "stop"));
	    bool_to_regbit(sp, GCR, HD64465_PCCGCR_PCCR,
		state->flags & SS_RESET);
	}

	if (changed & SS_OUTPUT_ENA) {
	    DPRINTK("%sabling card output\n",
		(state->flags & SS_OUTPUT_ENA ? "en" : "dis"));
	    bool_to_regbit(sp, GCR, HD64465_PCCGCR_PDRV,
		state->flags & SS_OUTPUT_ENA);
	}

    	/* TODO: SS_SPKR_ENA */
	    
/*    	hd64465_io_debug = 0; */
	sp->state = *state;
	    
	local_irq_restore(flags);

#if HD64465_DEBUG > 10
	if (state->flags & SS_OUTPUT_ENA)   
	    cis_hex_dump((const unsigned char*)sp->mem_base, 0x100);
#endif
	return 0;
}

/*============================================================*/

static int hs_set_io_map(struct pcmcia_socket *s, struct pccard_io_map *io)
{
    	hs_socket_t *sp = container_of(s, struct hs_socket_t, socket);
	int map = io->map;
	int sock = sp->number;
	struct pccard_io_map *sio;
	pgprot_t prot;

    	DPRINTK("hs_set_io_map(sock=%d, map=%d, flags=0x%x, speed=%dns, start=%#lx, stop=%#lx)\n",
	    sock, map, io->flags, io->speed, io->start, io->stop);
	if (map >= MAX_IO_WIN)
	    return -EINVAL;
	sio = &sp->io_maps[map];

    	/* check for null changes */	
    	if (io->flags == sio->flags &&
	    io->start == sio->start &&
	    io->stop == sio->stop)
	    return 0;
	
	if (io->flags & MAP_AUTOSZ)
	    prot = PAGE_KERNEL_PCC(sock, _PAGE_PCC_IODYN);
	else if (io->flags & MAP_16BIT)
	    prot = PAGE_KERNEL_PCC(sock, _PAGE_PCC_IO16);
	else
	    prot = PAGE_KERNEL_PCC(sock, _PAGE_PCC_IO8);

	/* TODO: handle MAP_USE_WAIT */
	if (io->flags & MAP_USE_WAIT)
	    printk(KERN_INFO MODNAME ": MAP_USE_WAIT unimplemented\n");
	/* TODO: handle MAP_PREFETCH */
	if (io->flags & MAP_PREFETCH)
	    printk(KERN_INFO MODNAME ": MAP_PREFETCH unimplemented\n");
	/* TODO: handle MAP_WRPROT */
	if (io->flags & MAP_WRPROT)
	    printk(KERN_INFO MODNAME ": MAP_WRPROT unimplemented\n");
	/* TODO: handle MAP_0WS */
	if (io->flags & MAP_0WS)
	    printk(KERN_INFO MODNAME ": MAP_0WS unimplemented\n");

	if (io->flags & MAP_ACTIVE) {
	    unsigned long pstart, psize, paddrbase;
	    
	    paddrbase = virt_to_phys((void*)(sp->mem_base + 2 * HD64465_PCC_WINDOW));
	    pstart = io->start & PAGE_MASK;
	    psize = ((io->stop + PAGE_SIZE) & PAGE_MASK) - pstart;

    	    /*
	     * Change PTEs in only that portion of the mapping requested
	     * by the caller.  This means that most of the time, most of
	     * the PTEs in the io_vma will be unmapped and only the bottom
	     * page will be mapped.  But the code allows for weird cards
	     * that might want IO ports > 4K.
	     */
	    sp->io_base = p3_ioremap(paddrbase + pstart, psize, pgprot_val(prot));
	    
	    /*
	     * Change the mapping used by inb() outb() etc
	     */
	    hd64465_port_map(io->start,
		io->stop - io->start + 1,
	    	(unsigned long)sp->io_base + io->start, 0);
	} else {
	    hd64465_port_unmap(sio->start, sio->stop - sio->start + 1);
	    p3_iounmap(sp->io_base);
	}
	
	*sio = *io;
	return 0;
}

/*============================================================*/

static int hs_set_mem_map(struct pcmcia_socket *s, struct pccard_mem_map *mem)
{
    	hs_socket_t *sp = container_of(s, struct hs_socket_t, socket);
	struct pccard_mem_map *smem;
	int map = mem->map;
	unsigned long paddr;

#if 0
    	DPRINTK("hs_set_mem_map(sock=%d, map=%d, flags=0x%x, card_start=0x%08x)\n",
	    sock, map, mem->flags, mem->card_start);
#endif

	if (map >= MAX_WIN)
	    return -EINVAL;
	smem = &sp->mem_maps[map];
	
	paddr = sp->mem_base;	    	    /* base of Attribute mapping */
	if (!(mem->flags & MAP_ATTRIB))
	    paddr += HD64465_PCC_WINDOW;    /* base of Common mapping */
	paddr += mem->card_start;

    	/* Because we specified SS_CAP_STATIC_MAP, we are obliged
	 * at this time to report the system address corresponding
	 * to the card address requested.  This is how Socket Services
	 * queries our fixed mapping.  I wish this fact had been
	 * documented - Greg Banks.
	 */
    	mem->static_start = paddr;
	
	*smem = *mem;
	
    	return 0;
}

/* TODO: do we need to use the MMU to access Common memory ??? */

/*============================================================*/

/*
 * This function is registered with the HD64465 glue code to do a
 * secondary demux step on the PCMCIA interrupts.  It handles 
 * mapping the IREQ request from the card to a standard Linux
 * IRQ, as requested by SocketServices.
 */
static int hs_irq_demux(int irq, void *dev)
{
    	hs_socket_t *sp = (hs_socket_t *)dev;
	u_int cscr;
    	
    	DPRINTK("hs_irq_demux(irq=%d)\n", irq);

    	if (sp->state.io_irq &&
	    (cscr = hs_in(sp, CSCR)) & HD64465_PCCCSCR_PIREQ) {
	    cscr &= ~HD64465_PCCCSCR_PIREQ;
	    hs_out(sp, cscr, CSCR);
	    return sp->state.io_irq;
	}
	    
	return irq;
}

/*============================================================*/

/*
 * Interrupt handling routine.
 */
 
static irqreturn_t hs_interrupt(int irq, void *dev, struct pt_regs *regs)
{
    	hs_socket_t *sp = (hs_socket_t *)dev;
	u_int events = 0;
	u_int cscr;
	
	
	cscr = hs_in(sp, CSCR);
	
	DPRINTK("hs_interrupt, cscr=%04x\n", cscr);

	/* check for bus-related changes to be reported to Socket Services */
	if (cscr & HD64465_PCCCSCR_PCDC) {
	    /* double-check for a 16-bit card, as we don't support CardBus */
	    if ((hs_in(sp, ISR) & HD64465_PCCISR_PCD_MASK) != 0) {
	    	printk(KERN_NOTICE MODNAME
		    ": socket %d, card not a supported card type or not inserted correctly\n",
		    sp->number);
		/* Don't do the rest unless a card is present */
		cscr &= ~(HD64465_PCCCSCR_PCDC|
		    	  HD64465_PCCCSCR_PRC|
			  HD64465_PCCCSCR_PBW|
		    	  HD64465_PCCCSCR_PBD|
			  HD64465_PCCCSCR_PSC);
	    } else {
	    	cscr &= ~HD64465_PCCCSCR_PCDC;
		events |= SS_DETECT;    	/* card insertion or removal */
    	    }
	}
	if (cscr & HD64465_PCCCSCR_PRC) {
	    cscr &= ~HD64465_PCCCSCR_PRC;
	    events |= SS_READY;     	/* ready signal changed */
	}
	if (cscr & HD64465_PCCCSCR_PBW) {
	    cscr &= ~HD64465_PCCCSCR_PSC;
	    events |= SS_BATWARN;     	/* battery warning */
	}
	if (cscr & HD64465_PCCCSCR_PBD) {
	    cscr &= ~HD64465_PCCCSCR_PSC;
	    events |= SS_BATDEAD;     	/* battery dead */
	}
	if (cscr & HD64465_PCCCSCR_PSC) {
	    cscr &= ~HD64465_PCCCSCR_PSC;
	    events |= SS_STSCHG;     	/* STSCHG (status changed) signal */
	}
	
	if (cscr & HD64465_PCCCSCR_PIREQ) {
	    cscr &= ~HD64465_PCCCSCR_PIREQ;

    	    /* This should have been dealt with during irq demux */	    
	    printk(KERN_NOTICE MODNAME ": unexpected IREQ from card\n");
	}

	hs_out(sp, cscr, CSCR);

	if (events)
		pcmcia_parse_events(&sp->socket, events);

	return IRQ_HANDLED;
}

/*============================================================*/

static struct pccard_operations hs_operations = {
	.init			= hs_init,
	.get_status		= hs_get_status,
	.get_socket		= hs_get_socket,
	.set_socket		= hs_set_socket,
	.set_io_map		= hs_set_io_map,
	.set_mem_map		= hs_set_mem_map,
};

static int hs_init_socket(hs_socket_t *sp, int irq, unsigned long mem_base,
    	    unsigned int ctrl_base)
{
    	unsigned short v;
    	int i, err;

    	memset(sp, 0, sizeof(*sp));
	sp->irq = irq;
	sp->mem_base = mem_base;
	sp->mem_length = 4*HD64465_PCC_WINDOW;	/* 16MB */
	sp->ctrl_base = ctrl_base;
	
	for (i=0 ; i<MAX_IO_WIN ; i++)
	    sp->io_maps[i].map = i;
	for (i=0 ; i<MAX_WIN ; i++)
	    sp->mem_maps[i].map = i;
	
	hd64465_register_irq_demux(sp->irq, hs_irq_demux, sp);
	
    	if ((err = request_irq(sp->irq, hs_interrupt, SA_INTERRUPT, MODNAME, sp)) < 0)
	    return err;
    	if (request_mem_region(sp->mem_base, sp->mem_length, MODNAME) == 0) {
    	    sp->mem_base = 0;
	    return -ENOMEM;
	}


	/* According to section 3.2 of the PCMCIA standard, low-voltage
	 * capable cards must implement cold insertion, i.e. Vpp and
	 * Vcc set to 0 before card is inserted.
	 */
	/*hs_set_voltages(sp, 0, 0);*/
	
	/* hi-Z the outputs to the card and set 16MB map mode */
	v = hs_in(sp, GCR);
	v &= ~HD64465_PCCGCR_PCCT;  	/* memory-only card */
	hs_out(sp, v, GCR);

	v = hs_in(sp, GCR);
	v |= HD64465_PCCGCR_PDRV;   	/* enable outputs to card */
	hs_out(sp, v, GCR);

	v = hs_in(sp, GCR);
	v |= HD64465_PCCGCR_PMMOD; 	/* 16MB mapping mode */
	hs_out(sp, v, GCR);

	v = hs_in(sp, GCR);
	/* lowest 16MB of Common */
	v &= ~(HD64465_PCCGCR_PPA25|HD64465_PCCGCR_PPA24); 
	hs_out(sp, v, GCR);
	
	hs_reset_socket(sp, 1);

	printk(KERN_INFO "HD64465 PCMCIA bridge socket %d at 0x%08lx irq %d\n",
	    	i, sp->mem_base, sp->irq);

    	return 0;
}

static void hs_exit_socket(hs_socket_t *sp)
{
    	unsigned short cscier, gcr;
	unsigned long flags;
	
	local_irq_save(flags);

	/* turn off interrupts in hardware */
    	cscier = hs_in(sp, CSCIER);
	cscier = (cscier & IER_MASK) | IER_OFF;
    	hs_out(sp, cscier, CSCIER);
	
	/* hi-Z the outputs to the card */
    	gcr = hs_in(sp, GCR);
	gcr &= HD64465_PCCGCR_PDRV;
	hs_out(sp, gcr, GCR);

    	/* power the card down */
	hs_set_voltages(sp, 0, 0);

    	if (sp->mem_base != 0)
	    release_mem_region(sp->mem_base, sp->mem_length);
	if (sp->irq != 0) {
	    free_irq(sp->irq, hs_interrupt);
    	    hd64465_unregister_irq_demux(sp->irq);
	}

	local_irq_restore(flags);
}

static struct device_driver hd64465_driver = {
	.name = "hd64465-pcmcia",
	.bus = &platform_bus_type,
	.suspend = pcmcia_socket_dev_suspend,
	.resume = pcmcia_socket_dev_resume,
};

static struct platform_device hd64465_device = {
	.name = "hd64465-pcmcia",
	.id = 0,
};

static int __init init_hs(void)
{
	int i;
	unsigned short v;

/*	hd64465_io_debug = 1; */
	if (driver_register(&hd64465_driver))
		return -EINVAL;
	
	/* Wake both sockets out of STANDBY mode */
	/* TODO: wait 15ms */
	v = inw(HD64465_REG_SMSCR);
	v &= ~(HD64465_SMSCR_PC0ST|HD64465_SMSCR_PC1ST);
	outw(v, HD64465_REG_SMSCR);

	/* keep power controller out of shutdown mode */
	v = inb(HD64465_REG_PCC0SCR);
	v |= HD64465_PCCSCR_SHDN;
	outb(v, HD64465_REG_PCC0SCR);

    	/* use serial (TPS2206) power controller */
	v = inb(HD64465_REG_PCC0CSCR);
	v |= HD64465_PCCCSCR_PSWSEL;
	outb(v, HD64465_REG_PCC0CSCR);

	/*
	 * Setup hs_sockets[] structures and request system resources.
	 * TODO: on memory allocation failure, power down the socket
	 *       before quitting.
	 */
	for (i=0; i<HS_MAX_SOCKETS; i++) {
		hs_set_voltages(&hs_sockets[i], 0, 0);

		hs_sockets[i].socket.features |=  SS_CAP_PCCARD | SS_CAP_STATIC_MAP;      /* mappings are fixed in host memory */
		hs_sockets[i].socket.resource_ops = &pccard_static_ops;
		hs_sockets[i].socket.irq_mask =  0xffde;/*0xffff*/	    /* IRQs mapped in s/w so can do any, really */
		hs_sockets[i].socket.map_size = HD64465_PCC_WINDOW;     /* 16MB fixed window size */

		hs_sockets[i].socket.owner = THIS_MODULE;
		hs_sockets[i].socket.ss_entry = &hs_operations;
	}

	i = hs_init_socket(&hs_sockets[0],
	    HD64465_IRQ_PCMCIA0,
	    HD64465_PCC0_BASE,
	    HD64465_REG_PCC0ISR);
	if (i < 0) {
		unregister_driver(&hd64465_driver);
		return i;
	}
	i = hs_init_socket(&hs_sockets[1],
	    HD64465_IRQ_PCMCIA1,
	    HD64465_PCC1_BASE,
	    HD64465_REG_PCC1ISR);
	if (i < 0) {
		unregister_driver(&hd64465_driver);
		return i;
	}

/*	hd64465_io_debug = 0; */

	platform_device_register(&hd64465_device);

	for (i=0; i<HS_MAX_SOCKETS; i++) {
		unsigned int ret;
		hs_sockets[i].socket.dev.dev = &hd64465_device.dev;		
		hs_sockets[i].number = i;
		ret = pcmcia_register_socket(&hs_sockets[i].socket);
		if (ret && i)
			pcmcia_unregister_socket(&hs_sockets[0].socket);
	}

    	return 0;
}

static void __exit exit_hs(void)
{
	int i;

	for (i=0 ; i<HS_MAX_SOCKETS ; i++) {
		pcmcia_unregister_socket(&hs_sockets[i].socket);
		hs_exit_socket(&hs_sockets[i]);
	}

	platform_device_unregister(&hd64465_device);
	unregister_driver(&hd64465_driver);
}

module_init(init_hs);
module_exit(exit_hs);

/*============================================================*/
/*END*/

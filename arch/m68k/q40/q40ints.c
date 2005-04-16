/*
 * arch/m68k/q40/q40ints.c
 *
 * Copyright (C) 1999,2001 Richard Zidlicky
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * .. used to be loosely based on bvme6000ints.c
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/hardirq.h>

#include <asm/rtc.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>

#include <asm/q40_master.h>
#include <asm/q40ints.h>

/*
 * Q40 IRQs are defined as follows:
 *            3,4,5,6,7,10,11,14,15 : ISA dev IRQs
 *            16-31: reserved
 *            32   : keyboard int
 *            33   : frame int (50/200 Hz periodic timer)
 *            34   : sample int (10/20 KHz periodic timer)
 *
*/

extern int ints_inited;


irqreturn_t q40_irq2_handler (int, void *, struct pt_regs *fp);


static irqreturn_t q40_defhand (int irq, void *dev_id, struct pt_regs *fp);
static irqreturn_t default_handler(int lev, void *dev_id, struct pt_regs *regs);


#define DEVNAME_SIZE 24

static struct q40_irq_node {
	irqreturn_t	(*handler)(int, void *, struct pt_regs *);
	unsigned long	flags;
	void		*dev_id;
  /*        struct q40_irq_node *next;*/
        char	        devname[DEVNAME_SIZE];
	unsigned	count;
        unsigned short  state;
} irq_tab[Q40_IRQ_MAX+1];

short unsigned q40_ablecount[Q40_IRQ_MAX+1];

/*
 * void q40_init_IRQ (void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function is called during kernel startup to initialize
 * the q40 IRQ handling routines.
 */

static int disabled=0;

void q40_init_IRQ (void)
{
	int i;

	disabled=0;
	for (i = 0; i <= Q40_IRQ_MAX; i++) {
		irq_tab[i].handler = q40_defhand;
		irq_tab[i].flags = 0;
		irq_tab[i].dev_id = NULL;
		/*		irq_tab[i].next = NULL;*/
		irq_tab[i].devname[0] = 0;
		irq_tab[i].count = 0;
		irq_tab[i].state =0;
		q40_ablecount[i]=0;   /* all enabled */
	}

	/* setup handler for ISA ints */
	cpu_request_irq(IRQ2, q40_irq2_handler, 0, "q40 ISA and master chip",
			NULL);

	/* now enable some ints.. */
	master_outb(1,EXT_ENABLE_REG);  /* ISA IRQ 5-15 */

	/* make sure keyboard IRQ is disabled */
	master_outb(0,KEY_IRQ_ENABLE_REG);
}

int q40_request_irq(unsigned int irq,
		irqreturn_t (*handler)(int, void *, struct pt_regs *),
                unsigned long flags, const char *devname, void *dev_id)
{
  /*printk("q40_request_irq %d, %s\n",irq,devname);*/

	if (irq > Q40_IRQ_MAX || (irq>15 && irq<32)) {
		printk("%s: Incorrect IRQ %d from %s\n", __FUNCTION__, irq, devname);
		return -ENXIO;
	}

	/* test for ISA ints not implemented by HW */
	switch (irq)
	  {
	  case 1: case 2: case 8: case 9:
	  case 12: case 13:
	    printk("%s: ISA IRQ %d from %s not implemented by HW\n", __FUNCTION__, irq, devname);
	    return -ENXIO;
	  case 11:
	    printk("warning IRQ 10 and 11 not distinguishable\n");
	    irq=10;
	  default:
	    ;
	  }

	if (irq<Q40_IRQ_SAMPLE)
	  {
	    if (irq_tab[irq].dev_id != NULL)
		  {
		    printk("%s: IRQ %d from %s is not replaceable\n",
			   __FUNCTION__, irq, irq_tab[irq].devname);
		    return -EBUSY;
		  }
	    /*printk("IRQ %d set to handler %p\n",irq,handler);*/
	    if (dev_id==NULL)
		  {
		printk("WARNING: dev_id == NULL in request_irq\n");
		dev_id=(void*)1;
	      }
	    irq_tab[irq].handler = handler;
	    irq_tab[irq].flags   = flags;
	    irq_tab[irq].dev_id  = dev_id;
	    strlcpy(irq_tab[irq].devname,devname,sizeof(irq_tab[irq].devname));
	    irq_tab[irq].state = 0;
	    return 0;
	  }
	else {
	  /* Q40_IRQ_SAMPLE :somewhat special actions required here ..*/
	  cpu_request_irq(4, handler, flags, devname, dev_id);
	  cpu_request_irq(6, handler, flags, devname, dev_id);
	  return 0;
	}
}

void q40_free_irq(unsigned int irq, void *dev_id)
{
	if (irq > Q40_IRQ_MAX || (irq>15 && irq<32)) {
		printk("%s: Incorrect IRQ %d, dev_id %x \n", __FUNCTION__, irq, (unsigned)dev_id);
		return;
	}

	/* test for ISA ints not implemented by HW */
	switch (irq)
	  {
	  case 1: case 2: case 8: case 9:
	  case 12: case 13:
	    printk("%s: ISA IRQ %d from %x invalid\n", __FUNCTION__, irq, (unsigned)dev_id);
	    return;
	  case 11: irq=10;
	  default:
	    ;
	  }

	if (irq<Q40_IRQ_SAMPLE)
	  {
	    if (irq_tab[irq].dev_id != dev_id)
	      printk("%s: Removing probably wrong IRQ %d from %s\n",
		     __FUNCTION__, irq, irq_tab[irq].devname);

	    irq_tab[irq].handler = q40_defhand;
	    irq_tab[irq].flags   = 0;
	    irq_tab[irq].dev_id  = NULL;
	    /* irq_tab[irq].devname = NULL; */
	    /* do not reset state !! */
	  }
	else
	  { /* == Q40_IRQ_SAMPLE */
	    cpu_free_irq(4, dev_id);
	    cpu_free_irq(6, dev_id);
	  }
}


irqreturn_t q40_process_int (int level, struct pt_regs *fp)
{
  printk("unexpected interrupt vec=%x, pc=%lx, d0=%lx, d0_orig=%lx, d1=%lx, d2=%lx\n",
          level, fp->pc, fp->d0, fp->orig_d0, fp->d1, fp->d2);
  printk("\tIIRQ_REG = %x, EIRQ_REG = %x\n",master_inb(IIRQ_REG),master_inb(EIRQ_REG));
  return IRQ_HANDLED;
}

/*
 * this stuff doesn't really belong here..
*/

int ql_ticks;              /* 200Hz ticks since last jiffie */
static int sound_ticks;

#define SVOL 45

void q40_mksound(unsigned int hz, unsigned int ticks)
{
  /* for now ignore hz, except that hz==0 switches off sound */
  /* simply alternate the ampl (128-SVOL)-(128+SVOL)-..-.. at 200Hz */
  if (hz==0)
    {
      if (sound_ticks)
	sound_ticks=1;

      *DAC_LEFT=128;
      *DAC_RIGHT=128;

      return;
    }
  /* sound itself is done in q40_timer_int */
  if (sound_ticks == 0) sound_ticks=1000; /* pretty long beep */
  sound_ticks=ticks<<1;
}

static irqreturn_t (*q40_timer_routine)(int, void *, struct pt_regs *);

static irqreturn_t q40_timer_int (int irq, void * dev, struct pt_regs * regs)
{
    ql_ticks = ql_ticks ? 0 : 1;
    if (sound_ticks)
      {
	unsigned char sval=(sound_ticks & 1) ? 128-SVOL : 128+SVOL;
	sound_ticks--;
	*DAC_LEFT=sval;
	*DAC_RIGHT=sval;
      }

    if (!ql_ticks)
	q40_timer_routine(irq, dev, regs);
    return IRQ_HANDLED;
}

void q40_sched_init (irqreturn_t (*timer_routine)(int, void *, struct pt_regs *))
{
    int timer_irq;

    q40_timer_routine = timer_routine;
    timer_irq=Q40_IRQ_FRAME;

    if (request_irq(timer_irq, q40_timer_int, 0,
				"timer", q40_timer_int))
	panic ("Couldn't register timer int");

    master_outb(-1,FRAME_CLEAR_REG);
    master_outb( 1,FRAME_RATE_REG);
}


/*
 * tables to translate bits into IRQ numbers
 * it is a good idea to order the entries by priority
 *
*/

struct IRQ_TABLE{ unsigned mask; int irq ;};
#if 0
static struct IRQ_TABLE iirqs[]={
  {Q40_IRQ_FRAME_MASK,Q40_IRQ_FRAME},
  {Q40_IRQ_KEYB_MASK,Q40_IRQ_KEYBOARD},
  {0,0}};
#endif
static struct IRQ_TABLE eirqs[] = {
  { .mask = Q40_IRQ3_MASK,	.irq = 3 },	/* ser 1 */
  { .mask = Q40_IRQ4_MASK,	.irq = 4 },	/* ser 2 */
  { .mask = Q40_IRQ14_MASK,	.irq = 14 },	/* IDE 1 */
  { .mask = Q40_IRQ15_MASK,	.irq = 15 },	/* IDE 2 */
  { .mask = Q40_IRQ6_MASK,	.irq = 6 },	/* floppy, handled elsewhere */
  { .mask = Q40_IRQ7_MASK,	.irq = 7 },	/* par */
  { .mask = Q40_IRQ5_MASK,	.irq = 5 },
  { .mask = Q40_IRQ10_MASK,	.irq = 10 },
  {0,0}
};

/* complain only this many times about spurious ints : */
static int ccleirq=60;    /* ISA dev IRQ's*/
/*static int cclirq=60;*/     /* internal */

/* FIXME: add shared ints,mask,unmask,probing.... */

#define IRQ_INPROGRESS 1
/*static unsigned short saved_mask;*/
//static int do_tint=0;

#define DEBUG_Q40INT
/*#define IP_USE_DISABLE *//* would be nice, but crashes ???? */

static int mext_disabled=0;  /* ext irq disabled by master chip? */
static int aliased_irq=0;  /* how many times inside handler ?*/


/* got level 2 interrupt, dispatch to ISA or keyboard/timer IRQs */
irqreturn_t q40_irq2_handler (int vec, void *devname, struct pt_regs *fp)
{
  unsigned mir, mer;
  int irq,i;

//repeat:
  mir=master_inb(IIRQ_REG);
  if (mir&Q40_IRQ_FRAME_MASK) {
	  irq_tab[Q40_IRQ_FRAME].count++;
	  irq_tab[Q40_IRQ_FRAME].handler(Q40_IRQ_FRAME,irq_tab[Q40_IRQ_FRAME].dev_id,fp);
	  master_outb(-1,FRAME_CLEAR_REG);
  }
  if ((mir&Q40_IRQ_SER_MASK) || (mir&Q40_IRQ_EXT_MASK)) {
	  mer=master_inb(EIRQ_REG);
	  for (i=0; eirqs[i].mask; i++) {
		  if (mer&(eirqs[i].mask)) {
			  irq=eirqs[i].irq;
/*
 * There is a little mess wrt which IRQ really caused this irq request. The
 * main problem is that IIRQ_REG and EIRQ_REG reflect the state when they
 * are read - which is long after the request came in. In theory IRQs should
 * not just go away but they occassionally do
 */
			  if (irq>4 && irq<=15 && mext_disabled) {
				  /*aliased_irq++;*/
				  goto iirq;
			  }
			  if (irq_tab[irq].handler == q40_defhand ) {
				  printk("handler for IRQ %d not defined\n",irq);
				  continue; /* ignore uninited INTs :-( */
			  }
			  if ( irq_tab[irq].state & IRQ_INPROGRESS ) {
				  /* some handlers do local_irq_enable() for irq latency reasons, */
				  /* however reentering an active irq handler is not permitted */
#ifdef IP_USE_DISABLE
				  /* in theory this is the better way to do it because it still */
				  /* lets through eg the serial irqs, unfortunately it crashes */
				  disable_irq(irq);
				  disabled=1;
#else
				  /*printk("IRQ_INPROGRESS detected for irq %d, disabling - %s disabled\n",irq,disabled ? "already" : "not yet"); */
				  fp->sr = (((fp->sr) & (~0x700))+0x200);
				  disabled=1;
#endif
				  goto iirq;
			  }
			  irq_tab[irq].count++;
			  irq_tab[irq].state |= IRQ_INPROGRESS;
			  irq_tab[irq].handler(irq,irq_tab[irq].dev_id,fp);
			  irq_tab[irq].state &= ~IRQ_INPROGRESS;

			  /* naively enable everything, if that fails than    */
			  /* this function will be reentered immediately thus */
			  /* getting another chance to disable the IRQ        */

			  if ( disabled ) {
#ifdef IP_USE_DISABLE
				  if (irq>4){
					  disabled=0;
					  enable_irq(irq);}
#else
				  disabled=0;
				  /*printk("reenabling irq %d\n",irq); */
#endif
			  }
// used to do 'goto repeat;' here, this delayed bh processing too long
			  return IRQ_HANDLED;
		  }
	  }
	  if (mer && ccleirq>0 && !aliased_irq)
		  printk("ISA interrupt from unknown source? EIRQ_REG = %x\n",mer),ccleirq--;
  }
 iirq:
  mir=master_inb(IIRQ_REG);
  /* should test whether keyboard irq is really enabled, doing it in defhand */
  if (mir&Q40_IRQ_KEYB_MASK) {
	  irq_tab[Q40_IRQ_KEYBOARD].count++;
	  irq_tab[Q40_IRQ_KEYBOARD].handler(Q40_IRQ_KEYBOARD,irq_tab[Q40_IRQ_KEYBOARD].dev_id,fp);
  }
  return IRQ_HANDLED;
}

int show_q40_interrupts (struct seq_file *p, void *v)
{
	int i;

	for (i = 0; i <= Q40_IRQ_MAX; i++) {
		if (irq_tab[i].count)
		      seq_printf(p, "%sIRQ %02d: %8d  %s%s\n",
			      (i<=15) ? "ISA-" : "    " ,
			    i, irq_tab[i].count,
			    irq_tab[i].devname[0] ? irq_tab[i].devname : "?",
			    irq_tab[i].handler == q40_defhand ?
					" (now unassigned)" : "");
	}
	return 0;
}


static irqreturn_t q40_defhand (int irq, void *dev_id, struct pt_regs *fp)
{
        if (irq!=Q40_IRQ_KEYBOARD)
	     printk ("Unknown q40 interrupt %d\n", irq);
	else master_outb(-1,KEYBOARD_UNLOCK_REG);
	return IRQ_NONE;
}
static irqreturn_t default_handler(int lev, void *dev_id, struct pt_regs *regs)
{
	printk ("Uninitialised interrupt level %d\n", lev);
	return IRQ_NONE;
}

irqreturn_t (*q40_default_handler[SYS_IRQS])(int, void *, struct pt_regs *) = {
	 [0] = default_handler,
	 [1] = default_handler,
	 [2] = default_handler,
	 [3] = default_handler,
	 [4] = default_handler,
	 [5] = default_handler,
	 [6] = default_handler,
	 [7] = default_handler
};


void q40_enable_irq (unsigned int irq)
{
  if ( irq>=5 && irq<=15 )
  {
    mext_disabled--;
    if (mext_disabled>0)
	  printk("q40_enable_irq : nested disable/enable\n");
    if (mext_disabled==0)
    master_outb(1,EXT_ENABLE_REG);
    }
}


void q40_disable_irq (unsigned int irq)
{
  /* disable ISA iqs : only do something if the driver has been
   * verified to be Q40 "compatible" - right now IDE, NE2K
   * Any driver should not attempt to sleep across disable_irq !!
   */

  if ( irq>=5 && irq<=15 ) {
    master_outb(0,EXT_ENABLE_REG);
    mext_disabled++;
    if (mext_disabled>1) printk("disable_irq nesting count %d\n",mext_disabled);
  }
}

unsigned long q40_probe_irq_on (void)
{
  printk("irq probing not working - reconfigure the driver to avoid this\n");
  return -1;
}
int q40_probe_irq_off (unsigned long irqs)
{
  return -1;
}
/*
 * Local variables:
 * compile-command: "m68k-linux-gcc -D__KERNEL__ -I/home/rz/lx/linux-2.2.6/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -pipe -fno-strength-reduce -ffixed-a2 -m68040   -c -o q40ints.o q40ints.c"
 * End:
 */

// SPDX-License-Identifier: GPL-2.0-only
/* drivers/atm/idt77105.c - IDT77105 (PHY) driver */
 
/* Written 1999 by Greg Banks, NEC Australia <gnb@linuxfan.com>. Based on suni.c */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/atmdev.h>
#include <linux/sonet.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/capability.h>
#include <linux/atm_idt77105.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <asm/param.h>
#include <linux/uaccess.h>

#include "idt77105.h"

#undef GENERAL_DEBUG

#ifdef GENERAL_DEBUG
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif


struct idt77105_priv {
	struct idt77105_stats stats;    /* link diagnostics */
	struct atm_dev *dev;		/* device back-pointer */
	struct idt77105_priv *next;
        int loop_mode;
        unsigned char old_mcr;          /* storage of MCR reg while signal lost */
};

static DEFINE_SPINLOCK(idt77105_priv_lock);

#define PRIV(dev) ((struct idt77105_priv *) dev->phy_data)

#define PUT(val,reg) dev->ops->phy_put(dev,val,IDT77105_##reg)
#define GET(reg) dev->ops->phy_get(dev,IDT77105_##reg)

static void idt77105_stats_timer_func(struct timer_list *);
static void idt77105_restart_timer_func(struct timer_list *);


static DEFINE_TIMER(stats_timer, idt77105_stats_timer_func);
static DEFINE_TIMER(restart_timer, idt77105_restart_timer_func);
static int start_timer = 1;
static struct idt77105_priv *idt77105_all = NULL;

/*
 * Retrieve the value of one of the IDT77105's counters.
 * `counter' is one of the IDT77105_CTRSEL_* constants.
 */
static u16 get_counter(struct atm_dev *dev, int counter)
{
        u16 val;
        
        /* write the counter bit into PHY register 6 */
        PUT(counter, CTRSEL);
        /* read the low 8 bits from register 4 */
        val = GET(CTRLO);
        /* read the high 8 bits from register 5 */
        val |= GET(CTRHI)<<8;
        
        return val;
}

/*
 * Timer function called every second to gather statistics
 * from the 77105. This is done because the h/w registers
 * will overflow if not read at least once per second. The
 * kernel's stats are much higher precision. Also, having
 * a separate copy of the stats allows implementation of
 * an ioctl which gathers the stats *without* zero'ing them.
 */
static void idt77105_stats_timer_func(struct timer_list *unused)
{
	struct idt77105_priv *walk;
	struct atm_dev *dev;
	struct idt77105_stats *stats;

        DPRINTK("IDT77105 gathering statistics\n");
	for (walk = idt77105_all; walk; walk = walk->next) {
		dev = walk->dev;
                
		stats = &walk->stats;
                stats->symbol_errors += get_counter(dev, IDT77105_CTRSEL_SEC);
                stats->tx_cells += get_counter(dev, IDT77105_CTRSEL_TCC);
                stats->rx_cells += get_counter(dev, IDT77105_CTRSEL_RCC);
                stats->rx_hec_errors += get_counter(dev, IDT77105_CTRSEL_RHEC);
	}
        if (!start_timer) mod_timer(&stats_timer,jiffies+IDT77105_STATS_TIMER_PERIOD);
}


/*
 * A separate timer func which handles restarting PHY chips which
 * have had the cable re-inserted after being pulled out. This is
 * done by polling the Good Signal Bit in the Interrupt Status
 * register every 5 seconds. The other technique (checking Good
 * Signal Bit in the interrupt handler) cannot be used because PHY
 * interrupts need to be disabled when the cable is pulled out
 * to avoid lots of spurious cell error interrupts.
 */
static void idt77105_restart_timer_func(struct timer_list *unused)
{
	struct idt77105_priv *walk;
	struct atm_dev *dev;
        unsigned char istat;

        DPRINTK("IDT77105 checking for cable re-insertion\n");
	for (walk = idt77105_all; walk; walk = walk->next) {
		dev = walk->dev;
                
                if (dev->signal != ATM_PHY_SIG_LOST)
                    continue;
                    
                istat = GET(ISTAT); /* side effect: clears all interrupt status bits */
                if (istat & IDT77105_ISTAT_GOODSIG) {
                    /* Found signal again */
                    atm_dev_signal_change(dev, ATM_PHY_SIG_FOUND);
	            printk(KERN_NOTICE "%s(itf %d): signal detected again\n",
                        dev->type,dev->number);
                    /* flush the receive FIFO */
                    PUT( GET(DIAG) | IDT77105_DIAG_RFLUSH, DIAG);
                    /* re-enable interrupts */
	            PUT( walk->old_mcr ,MCR);
                }
	}
        if (!start_timer) mod_timer(&restart_timer,jiffies+IDT77105_RESTART_TIMER_PERIOD);
}


static int fetch_stats(struct atm_dev *dev,struct idt77105_stats __user *arg,int zero)
{
	unsigned long flags;
	struct idt77105_stats stats;

	spin_lock_irqsave(&idt77105_priv_lock, flags);
	memcpy(&stats, &PRIV(dev)->stats, sizeof(struct idt77105_stats));
	if (zero)
		memset(&PRIV(dev)->stats, 0, sizeof(struct idt77105_stats));
	spin_unlock_irqrestore(&idt77105_priv_lock, flags);
	if (arg == NULL)
		return 0;
	return copy_to_user(arg, &stats,
		    sizeof(struct idt77105_stats)) ? -EFAULT : 0;
}


static int set_loopback(struct atm_dev *dev,int mode)
{
	int diag;

	diag = GET(DIAG) & ~IDT77105_DIAG_LCMASK;
	switch (mode) {
		case ATM_LM_NONE:
			break;
		case ATM_LM_LOC_ATM:
			diag |= IDT77105_DIAG_LC_PHY_LOOPBACK;
			break;
		case ATM_LM_RMT_ATM:
			diag |= IDT77105_DIAG_LC_LINE_LOOPBACK;
			break;
		default:
			return -EINVAL;
	}
	PUT(diag,DIAG);
	printk(KERN_NOTICE "%s(%d) Loopback mode is: %s\n", dev->type,
	    dev->number,
	    (mode == ATM_LM_NONE ? "NONE" : 
	      (mode == ATM_LM_LOC_ATM ? "DIAG (local)" :
		(mode == IDT77105_DIAG_LC_LINE_LOOPBACK ? "LOOP (remote)" :
		  "unknown")))
		    );
	PRIV(dev)->loop_mode = mode;
	return 0;
}


static int idt77105_ioctl(struct atm_dev *dev,unsigned int cmd,void __user *arg)
{
        printk(KERN_NOTICE "%s(%d) idt77105_ioctl() called\n",dev->type,dev->number);
	switch (cmd) {
		case IDT77105_GETSTATZ:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			fallthrough;
		case IDT77105_GETSTAT:
			return fetch_stats(dev, arg, cmd == IDT77105_GETSTATZ);
		case ATM_SETLOOP:
			return set_loopback(dev,(int)(unsigned long) arg);
		case ATM_GETLOOP:
			return put_user(PRIV(dev)->loop_mode,(int __user *)arg) ?
			    -EFAULT : 0;
		case ATM_QUERYLOOP:
			return put_user(ATM_LM_LOC_ATM | ATM_LM_RMT_ATM,
			    (int __user *) arg) ? -EFAULT : 0;
		default:
			return -ENOIOCTLCMD;
	}
}



static void idt77105_int(struct atm_dev *dev)
{
        unsigned char istat;
        
        istat = GET(ISTAT); /* side effect: clears all interrupt status bits */
     
        DPRINTK("IDT77105 generated an interrupt, istat=%02x\n", (unsigned)istat);
                
        if (istat & IDT77105_ISTAT_RSCC) {
            /* Rx Signal Condition Change - line went up or down */
            if (istat & IDT77105_ISTAT_GOODSIG) {   /* signal detected again */
                /* This should not happen (restart timer does it) but JIC */
		atm_dev_signal_change(dev, ATM_PHY_SIG_FOUND);
            } else {    /* signal lost */
                /*
                 * Disable interrupts and stop all transmission and
                 * reception - the restart timer will restore these.
                 */
                PRIV(dev)->old_mcr = GET(MCR);
	        PUT(
                    (PRIV(dev)->old_mcr|
                    IDT77105_MCR_DREC|
                    IDT77105_MCR_DRIC|
                    IDT77105_MCR_HALTTX
                    ) & ~IDT77105_MCR_EIP, MCR);
		atm_dev_signal_change(dev, ATM_PHY_SIG_LOST);
	        printk(KERN_NOTICE "%s(itf %d): signal lost\n",
                    dev->type,dev->number);
            }
        }
        
        if (istat & IDT77105_ISTAT_RFO) {
            /* Rx FIFO Overrun -- perform a FIFO flush */
            PUT( GET(DIAG) | IDT77105_DIAG_RFLUSH, DIAG);
	    printk(KERN_NOTICE "%s(itf %d): receive FIFO overrun\n",
                dev->type,dev->number);
        }
#ifdef GENERAL_DEBUG
        if (istat & (IDT77105_ISTAT_HECERR | IDT77105_ISTAT_SCR |
                     IDT77105_ISTAT_RSE)) {
            /* normally don't care - just report in stats */
	    printk(KERN_NOTICE "%s(itf %d): received cell with error\n",
                dev->type,dev->number);
        }
#endif
}


static int idt77105_start(struct atm_dev *dev)
{
	unsigned long flags;

	if (!(dev->dev_data = kmalloc(sizeof(struct idt77105_priv),GFP_KERNEL)))
		return -ENOMEM;
	PRIV(dev)->dev = dev;
	spin_lock_irqsave(&idt77105_priv_lock, flags);
	PRIV(dev)->next = idt77105_all;
	idt77105_all = PRIV(dev);
	spin_unlock_irqrestore(&idt77105_priv_lock, flags);
	memset(&PRIV(dev)->stats,0,sizeof(struct idt77105_stats));
        
        /* initialise dev->signal from Good Signal Bit */
	atm_dev_signal_change(dev,
		GET(ISTAT) & IDT77105_ISTAT_GOODSIG ?
		ATM_PHY_SIG_FOUND : ATM_PHY_SIG_LOST);
	if (dev->signal == ATM_PHY_SIG_LOST)
		printk(KERN_WARNING "%s(itf %d): no signal\n",dev->type,
		    dev->number);

        /* initialise loop mode from hardware */
        switch ( GET(DIAG) & IDT77105_DIAG_LCMASK ) {
        case IDT77105_DIAG_LC_NORMAL:
            PRIV(dev)->loop_mode = ATM_LM_NONE;
            break;
        case IDT77105_DIAG_LC_PHY_LOOPBACK:
            PRIV(dev)->loop_mode = ATM_LM_LOC_ATM;
            break;
        case IDT77105_DIAG_LC_LINE_LOOPBACK:
            PRIV(dev)->loop_mode = ATM_LM_RMT_ATM;
            break;
        }
        
        /* enable interrupts, e.g. on loss of signal */
        PRIV(dev)->old_mcr = GET(MCR);
        if (dev->signal == ATM_PHY_SIG_FOUND) {
            PRIV(dev)->old_mcr |= IDT77105_MCR_EIP;
	    PUT(PRIV(dev)->old_mcr, MCR);
        }

                    
	idt77105_stats_timer_func(0); /* clear 77105 counters */
	(void) fetch_stats(dev,NULL,1); /* clear kernel counters */
        
	spin_lock_irqsave(&idt77105_priv_lock, flags);
	if (start_timer) {
		start_timer = 0;
                
		stats_timer.expires = jiffies+IDT77105_STATS_TIMER_PERIOD;
		add_timer(&stats_timer);
                
		restart_timer.expires = jiffies+IDT77105_RESTART_TIMER_PERIOD;
		add_timer(&restart_timer);
	}
	spin_unlock_irqrestore(&idt77105_priv_lock, flags);
	return 0;
}


static int idt77105_stop(struct atm_dev *dev)
{
	struct idt77105_priv *walk, *prev;

        DPRINTK("%s(itf %d): stopping IDT77105\n",dev->type,dev->number);
        
        /* disable interrupts */
	PUT( GET(MCR) & ~IDT77105_MCR_EIP, MCR );
        
        /* detach private struct from atm_dev & free */
	for (prev = NULL, walk = idt77105_all ;
             walk != NULL;
             prev = walk, walk = walk->next) {
            if (walk->dev == dev) {
                if (prev != NULL)
                    prev->next = walk->next;
                else
                    idt77105_all = walk->next;
	        dev->phy = NULL;
                dev->dev_data = NULL;
                kfree(walk);
                break;
            }
        }

	return 0;
}


static const struct atmphy_ops idt77105_ops = {
	.start = 	idt77105_start,
	.ioctl =	idt77105_ioctl,
	.interrupt =	idt77105_int,
	.stop =		idt77105_stop,
};


int idt77105_init(struct atm_dev *dev)
{
	dev->phy = &idt77105_ops;
	return 0;
}

EXPORT_SYMBOL(idt77105_init);

static void __exit idt77105_exit(void)
{
	/* turn off timers */
	del_timer_sync(&stats_timer);
	del_timer_sync(&restart_timer);
}

module_exit(idt77105_exit);

MODULE_LICENSE("GPL");

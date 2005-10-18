/****************************************************************************
* sdlamain.c	WANPIPE(tm) Multiprotocol WAN Link Driver.  Main module.
*
* Author:	Nenad Corbic	<ncorbic@sangoma.com>
*		Gideon Hack	
*
* Copyright:	(c) 1995-2000 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Dec 22, 2000  Nenad Corbic	Updated for 2.4.X kernels.
* 				Removed the polling routine.
* Nov 13, 2000  Nenad Corbic	Added hw probing on module load and dynamic
* 				device allocation. 
* Nov 7,  2000  Nenad Corbic	Fixed the Multi-Port PPP for kernels
*                               2.2.16 and above.
* Aug 2,  2000  Nenad Corbic	Block the Multi-Port PPP from running on
*  			        kernels 2.2.16 or greater.  The SyncPPP 
*  			        has changed.
* Jul 25, 2000  Nenad Corbic	Updated the Piggiback support for MultPPPP.
* Jul 13, 2000	Nenad Corbic	Added Multi-PPP support.
* Feb 02, 2000  Nenad Corbic    Fixed up piggyback probing and selection.
* Sep 23, 1999  Nenad Corbic    Added support for SMP
* Sep 13, 1999  Nenad Corbic	Each port is treated as a separate device.
* Jun 02, 1999  Gideon Hack     Added support for the S514 adapter.
*				Updates for Linux 2.2.X kernels.
* Sep 17, 1998	Jaspreet Singh	Updated for 2.1.121+ kernel
* Nov 28, 1997	Jaspreet Singh	Changed DRV_RELEASE to 1
* Nov 10, 1997	Jaspreet Singh	Changed sti() to restore_flags();
* Nov 06, 1997 	Jaspreet Singh	Changed DRV_VERSION to 4 and DRV_RELEASE to 0
* Oct 20, 1997 	Jaspreet Singh	Modified sdla_isr routine so that card->in_isr
*				assignments are taken out and placed in the
*				sdla_ppp.c, sdla_fr.c and sdla_x25.c isr
*				routines. Took out 'wandev->tx_int_enabled' and
*				replaced it with 'wandev->enable_tx_int'. 
* May 29, 1997	Jaspreet Singh	Flow Control Problem
*				added "wandev->tx_int_enabled=1" line in the
*				init module. This line initializes the flag for 
*				preventing Interrupt disabled with device set to
*				busy
* Jan 15, 1997	Gene Kozin	Version 3.1.0
*				 o added UDP management stuff
* Jan 02, 1997	Gene Kozin	Initial version.
*****************************************************************************/

#include <linux/config.h>	/* OS configuration options */
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/string.h>	/* inline memset(), etc. */
#include <linux/init.h>
#include <linux/slab.h>	/* kmalloc(), kfree() */
#include <linux/kernel.h>	/* printk(), and other useful stuff */
#include <linux/module.h>	/* support for loadable modules */
#include <linux/ioport.h>	/* request_region(), release_region() */
#include <linux/wanrouter.h>	/* WAN router definitions */
#include <linux/wanpipe.h>	/* WANPIPE common user API definitions */
#include <linux/rcupdate.h>

#include <linux/in.h>
#include <asm/io.h>		/* phys_to_virt() */
#include <linux/pci.h>
#include <linux/sdlapci.h>
#include <linux/if_wanpipe_common.h>

#include <asm/uaccess.h>	/* kernel <-> user copy */
#include <linux/inetdevice.h>

#include <linux/ip.h>
#include <net/route.h>
 
#define KMEM_SAFETYZONE 8


#ifndef CONFIG_WANPIPE_FR
  #define wpf_init(a,b) (-EPROTONOSUPPORT) 
#endif

#ifndef CONFIG_WANPIPE_CHDLC
 #define wpc_init(a,b) (-EPROTONOSUPPORT) 
#endif

#ifndef CONFIG_WANPIPE_X25
 #define wpx_init(a,b) (-EPROTONOSUPPORT) 
#endif
 
#ifndef CONFIG_WANPIPE_PPP
 #define wpp_init(a,b) (-EPROTONOSUPPORT) 
#endif

#ifndef CONFIG_WANPIPE_MULTPPP 
 #define wsppp_init(a,b) (-EPROTONOSUPPORT) 
#endif
 
 
/***********FOR DEBUGGING PURPOSES*********************************************
static void * dbg_kmalloc(unsigned int size, int prio, int line) {
	int i = 0;
	void * v = kmalloc(size+sizeof(unsigned int)+2*KMEM_SAFETYZONE*8,prio);
	char * c1 = v;	
	c1 += sizeof(unsigned int);
	*((unsigned int *)v) = size;

	for (i = 0; i < KMEM_SAFETYZONE; i++) {
		c1[0] = 'D'; c1[1] = 'E'; c1[2] = 'A'; c1[3] = 'D';
		c1[4] = 'B'; c1[5] = 'E'; c1[6] = 'E'; c1[7] = 'F';
		c1 += 8;
	}
	c1 += size;
	for (i = 0; i < KMEM_SAFETYZONE; i++) {
		c1[0] = 'M'; c1[1] = 'U'; c1[2] = 'N'; c1[3] = 'G';
		c1[4] = 'W'; c1[5] = 'A'; c1[6] = 'L'; c1[7] = 'L';
		c1 += 8;
	}
	v = ((char *)v) + sizeof(unsigned int) + KMEM_SAFETYZONE*8;
	printk(KERN_INFO "line %d  kmalloc(%d,%d) = %p\n",line,size,prio,v);
	return v;
}
static void dbg_kfree(void * v, int line) {
	unsigned int * sp = (unsigned int *)(((char *)v) - (sizeof(unsigned int) + KMEM_SAFETYZONE*8));
	unsigned int size = *sp;
	char * c1 = ((char *)v) - KMEM_SAFETYZONE*8;
	int i = 0;
	for (i = 0; i < KMEM_SAFETYZONE; i++) {
		if (   c1[0] != 'D' || c1[1] != 'E' || c1[2] != 'A' || c1[3] != 'D'
		    || c1[4] != 'B' || c1[5] != 'E' || c1[6] != 'E' || c1[7] != 'F') {
			printk(KERN_INFO "kmalloced block at %p has been corrupted (underrun)!\n",v);
			printk(KERN_INFO " %4x: %2x %2x %2x %2x %2x %2x %2x %2x\n", i*8,
			                c1[0],c1[1],c1[2],c1[3],c1[4],c1[5],c1[6],c1[7] );
		}
		c1 += 8;
	}
	c1 += size;
	for (i = 0; i < KMEM_SAFETYZONE; i++) {
		if (   c1[0] != 'M' || c1[1] != 'U' || c1[2] != 'N' || c1[3] != 'G'
		    || c1[4] != 'W' || c1[5] != 'A' || c1[6] != 'L' || c1[7] != 'L'
		   ) {
			printk(KERN_INFO "kmalloced block at %p has been corrupted (overrun):\n",v);
			printk(KERN_INFO " %4x: %2x %2x %2x %2x %2x %2x %2x %2x\n", i*8,
			                c1[0],c1[1],c1[2],c1[3],c1[4],c1[5],c1[6],c1[7] );
		}
		c1 += 8;
	}
	printk(KERN_INFO "line %d  kfree(%p)\n",line,v);
	v = ((char *)v) - (sizeof(unsigned int) + KMEM_SAFETYZONE*8);
	kfree(v);
}

#define kmalloc(x,y) dbg_kmalloc(x,y,__LINE__)
#define kfree(x) dbg_kfree(x,__LINE__)
******************************************************************************/



/****** Defines & Macros ****************************************************/

#ifdef	_DEBUG_
#define	STATIC
#else
#define	STATIC		static
#endif

#define	DRV_VERSION	5		/* version number */
#define	DRV_RELEASE	0		/* release (minor version) number */
#define	MAX_CARDS	16		/* max number of adapters */

#ifndef	CONFIG_WANPIPE_CARDS		/* configurable option */
#define	CONFIG_WANPIPE_CARDS 1
#endif

#define	CMD_OK		0		/* normal firmware return code */
#define	CMD_TIMEOUT	0xFF		/* firmware command timed out */
#define	MAX_CMD_RETRY	10		/* max number of firmware retries */
/****** Function Prototypes *************************************************/

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);
 
/* WAN link driver entry points */
static int setup(struct wan_device* wandev, wandev_conf_t* conf);
static int shutdown(struct wan_device* wandev);
static int ioctl(struct wan_device* wandev, unsigned cmd, unsigned long arg);

/* IOCTL handlers */
static int ioctl_dump	(sdla_t* card, sdla_dump_t* u_dump);
static int ioctl_exec	(sdla_t* card, sdla_exec_t* u_exec, int);

/* Miscellaneous functions */
STATIC irqreturn_t sdla_isr	(int irq, void* dev_id, struct pt_regs *regs);
static void release_hw  (sdla_t *card);

static int check_s508_conflicts (sdla_t* card,wandev_conf_t* conf, int*);
static int check_s514_conflicts (sdla_t* card,wandev_conf_t* conf, int*);


/****** Global Data **********************************************************
 * Note: All data must be explicitly initialized!!!
 */

/* private data */
static char drvname[]	= "wanpipe";
static char fullname[]	= "WANPIPE(tm) Multiprotocol Driver";
static char copyright[]	= "(c) 1995-2000 Sangoma Technologies Inc.";
static int ncards; 
static sdla_t* card_array;		/* adapter data space */

/* Wanpipe's own workqueue, used for all API's.
 * All protocol specific tasks will be inserted
 * into the "wanpipe_wq" workqueue. 

 * The kernel workqueue mechanism will execute
 * all pending tasks in the "wanpipe_wq" workqueue.
 */

struct workqueue_struct *wanpipe_wq;
DECLARE_WORK(wanpipe_work, NULL, NULL);

static int wanpipe_bh_critical;

/******* Kernel Loadable Module Entry Points ********************************/

/*============================================================================
 * Module 'insert' entry point.
 * o print announcement
 * o allocate adapter data space
 * o initialize static data
 * o register all cards with WAN router
 * o calibrate SDLA shared memory access delay.
 *
 * Return:	0	Ok
 *		< 0	error.
 * Context:	process
 */
 
static int __init wanpipe_init(void)
{
	int cnt, err = 0;

	printk(KERN_INFO "%s v%u.%u %s\n",
		fullname, DRV_VERSION, DRV_RELEASE, copyright);

	wanpipe_wq = create_workqueue("wanpipe_wq");
	if (!wanpipe_wq)
		return -ENOMEM;

	/* Probe for wanpipe cards and return the number found */
	printk(KERN_INFO "wanpipe: Probing for WANPIPE hardware.\n");
	ncards = wanpipe_hw_probe();
	if (ncards){
		printk(KERN_INFO "wanpipe: Allocating maximum %i devices: wanpipe%i - wanpipe%i.\n",ncards,1,ncards);
	}else{
		printk(KERN_INFO "wanpipe: No S514/S508 cards found, unloading modules!\n");
		destroy_workqueue(wanpipe_wq);
		return -ENODEV;
	}
	
	/* Verify number of cards and allocate adapter data space */
	card_array = kmalloc(sizeof(sdla_t) * ncards, GFP_KERNEL);
	if (card_array == NULL) {
		destroy_workqueue(wanpipe_wq);
		return -ENOMEM;
	}

	memset(card_array, 0, sizeof(sdla_t) * ncards);

	/* Register adapters with WAN router */
	for (cnt = 0; cnt < ncards; ++ cnt) {
		sdla_t* card = &card_array[cnt];
		struct wan_device* wandev = &card->wandev;

		card->next = NULL;
		sprintf(card->devname, "%s%d", drvname, cnt + 1);
		wandev->magic    = ROUTER_MAGIC;
		wandev->name     = card->devname;
		wandev->private  = card;
		wandev->enable_tx_int = 0;
		wandev->setup    = &setup;
		wandev->shutdown = &shutdown;
		wandev->ioctl    = &ioctl;
		err = register_wan_device(wandev);
		if (err) {
			printk(KERN_INFO
				"%s: %s registration failed with error %d!\n",
				drvname, card->devname, err);
			break;
		}
	}
	if (cnt){
		ncards = cnt;	/* adjust actual number of cards */
	}else {
		kfree(card_array);
		destroy_workqueue(wanpipe_wq);
		printk(KERN_INFO "IN Init Module: NO Cards registered\n");
		err = -ENODEV;
	}

	return err;
}

/*============================================================================
 * Module 'remove' entry point.
 * o unregister all adapters from the WAN router
 * o release all remaining system resources
 */
static void __exit wanpipe_cleanup(void)
{
	int i;

	if (!ncards)
		return;
		
	for (i = 0; i < ncards; ++i) {
		sdla_t* card = &card_array[i];
		unregister_wan_device(card->devname);
	}
	destroy_workqueue(wanpipe_wq);
	kfree(card_array);

	printk(KERN_INFO "\nwanpipe: WANPIPE Modules Unloaded.\n");
}

module_init(wanpipe_init);
module_exit(wanpipe_cleanup);

/******* WAN Device Driver Entry Points *************************************/

/*============================================================================
 * Setup/configure WAN link driver.
 * o check adapter state
 * o make sure firmware is present in configuration
 * o make sure I/O port and IRQ are specified
 * o make sure I/O region is available
 * o allocate interrupt vector
 * o setup SDLA hardware
 * o call appropriate routine to perform protocol-specific initialization
 * o mark I/O region as used
 * o if this is the first active card, then schedule background task
 *
 * This function is called when router handles ROUTER_SETUP IOCTL. The
 * configuration structure is in kernel memory (including extended data, if
 * any).
 */
 
static int setup(struct wan_device* wandev, wandev_conf_t* conf)
{
	sdla_t* card;
	int err = 0;
	int irq=0;

	/* Sanity checks */
	if ((wandev == NULL) || (wandev->private == NULL) || (conf == NULL)){
		printk(KERN_INFO 
		      "%s: Failed Sdlamain Setup wandev %u, card %u, conf %u !\n",
		      wandev->name,
		      (unsigned int)wandev,(unsigned int)wandev->private,
		      (unsigned int)conf); 
		return -EFAULT;
	}

	printk(KERN_INFO "%s: Starting WAN Setup\n", wandev->name);

	card = wandev->private;
	if (wandev->state != WAN_UNCONFIGURED){
		printk(KERN_INFO "%s: failed sdlamain setup, busy!\n",
			wandev->name);
		return -EBUSY;		/* already configured */
	}

	printk(KERN_INFO "\nProcessing WAN device %s...\n", wandev->name);

	/* Initialize the counters for each wandev 
	 * Used for counting number of times new_if and 
         * del_if get called.
	 */
	wandev->del_if_cnt = 0;
	wandev->new_if_cnt = 0;
	wandev->config_id  = conf->config_id;

	if (!conf->data_size || (conf->data == NULL)) {
		printk(KERN_INFO
			"%s: firmware not found in configuration data!\n",
			wandev->name);
		return -EINVAL;
	}

	/* Check for resource conflicts and setup the
	 * card for piggibacking if necessary */
	if(!conf->S514_CPU_no[0]) {
		if ((err=check_s508_conflicts(card,conf,&irq)) != 0){
			return err;
		}
	}else {
		if ((err=check_s514_conflicts(card,conf,&irq)) != 0){
			return err;
		}
	}

	/* If the current card has already been configured
         * or it's a piggyback card, do not try to allocate
         * resources.
	 */
	if (!card->wandev.piggyback && !card->configured){

		/* Configure hardware, load firmware, etc. */
		memset(&card->hw, 0, sizeof(sdlahw_t));

		/* for an S514 adapter, pass the CPU number and the slot number read */
		/* from 'router.conf' to the 'sdla_setup()' function via the 'port' */
		/* parameter */
		if (conf->S514_CPU_no[0]){

			card->hw.S514_cpu_no[0] = conf->S514_CPU_no[0];
			card->hw.S514_slot_no = conf->PCI_slot_no;
			card->hw.auto_pci_cfg = conf->auto_pci_cfg;

			if (card->hw.auto_pci_cfg == WANOPT_YES){
				printk(KERN_INFO "%s: Setting CPU to %c and Slot to Auto\n",
				card->devname, card->hw.S514_cpu_no[0]);
			}else{
				printk(KERN_INFO "%s: Setting CPU to %c and Slot to %i\n",
				card->devname, card->hw.S514_cpu_no[0], card->hw.S514_slot_no);
			}

		}else{
			/* 508 Card io port and irq initialization */
			card->hw.port = conf->ioport;
			card->hw.irq = (conf->irq == 9) ? 2 : conf->irq;
		}


		/* Compute the virtual address of the card in kernel space */
		if(conf->maddr){
			card->hw.dpmbase = phys_to_virt(conf->maddr);
		}else{	
			card->hw.dpmbase = (void *)conf->maddr;
		}
			
		card->hw.dpmsize = SDLA_WINDOWSIZE;
		
		/* set the adapter type if using an S514 adapter */
		card->hw.type = (conf->S514_CPU_no[0]) ? SDLA_S514 : conf->hw_opt[0]; 
		card->hw.pclk = conf->hw_opt[1];

		err = sdla_setup(&card->hw, conf->data, conf->data_size);
		if (err){
			printk(KERN_INFO "%s: Hardware setup Failed %i\n",
					card->devname,err);
			return err;
		}

	        if(card->hw.type != SDLA_S514)
			irq = (conf->irq == 2) ? 9 : conf->irq; /* IRQ2 -> IRQ9 */
		else
			irq = card->hw.irq;

		/* request an interrupt vector - note that interrupts may be shared */
		/* when using the S514 PCI adapter */
		
       		if(request_irq(irq, sdla_isr, 
		      (card->hw.type == SDLA_S514) ? SA_SHIRQ : 0, 
		       wandev->name, card)){

			printk(KERN_INFO "%s: Can't reserve IRQ %d!\n", wandev->name, irq);
			return -EINVAL;
		}

	}else{
		printk(KERN_INFO "%s: Card Configured %lu or Piggybacking %i!\n",
			wandev->name,card->configured,card->wandev.piggyback);
	} 


	if (!card->configured){

		/* Initialize the Spin lock */
		printk(KERN_INFO "%s: Initializing for SMP\n",wandev->name);

		/* Piggyback spin lock has already been initialized,
		 * in check_s514/s508_conflicts() */
		if (!card->wandev.piggyback){
			spin_lock_init(&card->wandev.lock);
		}
		
		/* Intialize WAN device data space */
		wandev->irq       = irq;
		wandev->dma       = 0;
		if(card->hw.type != SDLA_S514){ 
			wandev->ioport = card->hw.port;
		}else{
			wandev->S514_cpu_no[0] = card->hw.S514_cpu_no[0];
			wandev->S514_slot_no = card->hw.S514_slot_no;
		}
		wandev->maddr     = (unsigned long)card->hw.dpmbase;
		wandev->msize     = card->hw.dpmsize;
		wandev->hw_opt[0] = card->hw.type;
		wandev->hw_opt[1] = card->hw.pclk;
		wandev->hw_opt[2] = card->hw.memory;
		wandev->hw_opt[3] = card->hw.fwid;
	}

	/* Protocol-specific initialization */
	switch (card->hw.fwid) {

	case SFID_X25_502:
	case SFID_X25_508:
		printk(KERN_INFO "%s: Starting X.25 Protocol Init.\n",
				card->devname);
		err = wpx_init(card, conf);
		break;
	case SFID_FR502:
	case SFID_FR508:
		printk(KERN_INFO "%s: Starting Frame Relay Protocol Init.\n",
				card->devname);
		err = wpf_init(card, conf);
		break;
	case SFID_PPP502:
	case SFID_PPP508:
		printk(KERN_INFO "%s: Starting PPP Protocol Init.\n",
				card->devname);
		err = wpp_init(card, conf);
		break;
		
	case SFID_CHDLC508:
	case SFID_CHDLC514:
		if (conf->ft1){		
			printk(KERN_INFO "%s: Starting FT1 CSU/DSU Config Driver.\n",
				card->devname);
			err = wpft1_init(card, conf);
			break;
			
		}else if (conf->config_id == WANCONFIG_MPPP){
			printk(KERN_INFO "%s: Starting Multi-Port PPP Protocol Init.\n",
					card->devname);
			err = wsppp_init(card,conf);
			break;

		}else{
			printk(KERN_INFO "%s: Starting CHDLC Protocol Init.\n",
					card->devname);
			err = wpc_init(card, conf);
			break;
		}
	default:
		printk(KERN_INFO "%s: Error, Firmware is not supported %X %X!\n",
			wandev->name,card->hw.fwid,SFID_CHDLC508);
		err = -EPROTONOSUPPORT;
	}

	if (err != 0){
		if (err == -EPROTONOSUPPORT){
			printk(KERN_INFO 
				"%s: Error, Protocol selected has not been compiled!\n",
					card->devname);
			printk(KERN_INFO 
				"%s:        Re-configure the kernel and re-build the modules!\n",
					card->devname);
		}
		
		release_hw(card);
		wandev->state = WAN_UNCONFIGURED;
		return err;
	}


  	/* Reserve I/O region and schedule background task */
        if(card->hw.type != SDLA_S514 && !card->wandev.piggyback)
		if (!request_region(card->hw.port, card->hw.io_range, 
				wandev->name)) {
			printk(KERN_WARNING "port 0x%04x busy\n", card->hw.port);
			release_hw(card);
			wandev->state = WAN_UNCONFIGURED;
			return -EBUSY;
	  }

	/* Only use the polling routine for the X25 protocol */
	
	card->wandev.critical=0;
	return 0;
}

/*================================================================== 
 * configure_s508_card
 * 
 * For a S508 adapter, check for a possible configuration error in that
 * we are loading an adapter in the same IO port as a previously loaded S508
 * card.
 */ 

static int check_s508_conflicts (sdla_t* card,wandev_conf_t* conf, int *irq)
{
	unsigned long smp_flags;
	int i;
	
	if (conf->ioport <= 0) {
		printk(KERN_INFO
		"%s: can't configure without I/O port address!\n",
		card->wandev.name);
		return -EINVAL;
	}

	if (conf->irq <= 0) {
		printk(KERN_INFO "%s: can't configure without IRQ!\n",
		card->wandev.name);
		return -EINVAL;
	}

	if (test_bit(0,&card->configured))
		return 0;


	/* Check for already loaded card with the same IO port and IRQ 
	 * If found, copy its hardware configuration and use its
	 * resources (i.e. piggybacking)
	 */
	
	for (i = 0; i < ncards; i++) {
		sdla_t *nxt_card = &card_array[i];

		/* Skip the current card ptr */
		if (nxt_card == card)	
			continue;


		/* Find a card that is already configured with the
		 * same IO Port */
		if ((nxt_card->hw.type == SDLA_S508) &&
		    (nxt_card->hw.port == conf->ioport) && 
		    (nxt_card->next == NULL)){
			
			/* We found a card the card that has same configuration
			 * as us. This means, that we must setup this card in 
			 * piggibacking mode. However, only CHDLC and MPPP protocol
			 * support this setup */
		
			if ((conf->config_id == WANCONFIG_CHDLC || 
			     conf->config_id == WANCONFIG_MPPP) &&
			    (nxt_card->wandev.config_id == WANCONFIG_CHDLC || 
			     nxt_card->wandev.config_id == WANCONFIG_MPPP)){ 
				
				*irq = nxt_card->hw.irq;
				memcpy(&card->hw, &nxt_card->hw, sizeof(sdlahw_t));
			
				/* The master could already be running, we must
				 * set this as a critical area */
				lock_adapter_irq(&nxt_card->wandev.lock, &smp_flags);

				nxt_card->next = card;
				card->next = nxt_card;

				card->wandev.piggyback = WANOPT_YES;

				/* We must initialise the piggiback spin lock here
				 * since isr will try to lock card->next if it
				 * exists */
				spin_lock_init(&card->wandev.lock);
				
				unlock_adapter_irq(&nxt_card->wandev.lock, &smp_flags);
				break;
			}else{
				/* Trying to run piggibacking with a wrong protocol */
				printk(KERN_INFO "%s: ERROR: Resource busy, ioport: 0x%x\n"
						 "%s:        This protocol doesn't support\n"
						 "%s:        multi-port operation!\n",
						 card->devname,nxt_card->hw.port,
						 card->devname,card->devname);
				return -EEXIST;
			}
		}
	}
	

	/* Make sure I/O port region is available only if we are the
	 * master device.  If we are running in piggybacking mode, 
	 * we will use the resources of the master card. */
	if (!card->wandev.piggyback) {
		struct resource *rr =
			request_region(conf->ioport, SDLA_MAXIORANGE, "sdlamain");
		release_region(conf->ioport, SDLA_MAXIORANGE);

		if (!rr) {
			printk(KERN_INFO
				"%s: I/O region 0x%X - 0x%X is in use!\n",
				card->wandev.name, conf->ioport,
				conf->ioport + SDLA_MAXIORANGE - 1);
			return -EINVAL;
		}
	}

	return 0;
}

/*================================================================== 
 * configure_s514_card
 * 
 * For a S514 adapter, check for a possible configuration error in that
 * we are loading an adapter in the same slot as a previously loaded S514
 * card.
 */ 


static int check_s514_conflicts(sdla_t* card,wandev_conf_t* conf, int *irq)
{
	unsigned long smp_flags;
	int i;
	
	if (test_bit(0,&card->configured))
		return 0;

	
	/* Check for already loaded card with the same IO port and IRQ 
	 * If found, copy its hardware configuration and use its
	 * resources (i.e. piggybacking)
	 */

	for (i = 0; i < ncards; i ++) {
	
		sdla_t* nxt_card = &card_array[i];
		if(nxt_card == card)
			continue;
		
		if((nxt_card->hw.type == SDLA_S514) &&
		   (nxt_card->hw.S514_slot_no == conf->PCI_slot_no) &&
		   (nxt_card->hw.S514_cpu_no[0] == conf->S514_CPU_no[0])&&
		   (nxt_card->next == NULL)){


			if ((conf->config_id == WANCONFIG_CHDLC || 
			     conf->config_id == WANCONFIG_MPPP) &&
			    (nxt_card->wandev.config_id == WANCONFIG_CHDLC || 
			     nxt_card->wandev.config_id == WANCONFIG_MPPP)){ 
				
				*irq = nxt_card->hw.irq;
				memcpy(&card->hw, &nxt_card->hw, sizeof(sdlahw_t));
	
				/* The master could already be running, we must
				 * set this as a critical area */
				lock_adapter_irq(&nxt_card->wandev.lock,&smp_flags);
				nxt_card->next = card;
				card->next = nxt_card;

				card->wandev.piggyback = WANOPT_YES;

				/* We must initialise the piggiback spin lock here
				 * since isr will try to lock card->next if it
				 * exists */
				spin_lock_init(&card->wandev.lock);

				unlock_adapter_irq(&nxt_card->wandev.lock,&smp_flags);

			}else{
				/* Trying to run piggibacking with a wrong protocol */
				printk(KERN_INFO "%s: ERROR: Resource busy: CPU %c PCISLOT %i\n"
						 "%s:        This protocol doesn't support\n"
						 "%s:        multi-port operation!\n",
						 card->devname,
						 conf->S514_CPU_no[0],conf->PCI_slot_no,
						 card->devname,card->devname);
				return -EEXIST;
			}
		}
	}

	return 0;
}



/*============================================================================
 * Shut down WAN link driver. 
 * o shut down adapter hardware
 * o release system resources.
 *
 * This function is called by the router when device is being unregistered or
 * when it handles ROUTER_DOWN IOCTL.
 */
static int shutdown(struct wan_device* wandev)
{
	sdla_t *card;
	int err=0;
	
	/* sanity checks */
	if ((wandev == NULL) || (wandev->private == NULL)){
		return -EFAULT;
	}
		
	if (wandev->state == WAN_UNCONFIGURED){
		return 0;
	}

	card = wandev->private;

	if (card->tty_opt){
		if (card->tty_open){
			printk(KERN_INFO 
				"%s: Shutdown Failed: TTY is still open\n",
				  card->devname);
			return -EBUSY;
		}
	}
	
	wandev->state = WAN_UNCONFIGURED;

	set_bit(PERI_CRIT,(void*)&wandev->critical);
	
	/* In case of piggibacking, make sure that 
         * we never try to shutdown both devices at the same
         * time, because they depend on one another */
	
	if (card->disable_comm){
		card->disable_comm(card);
	}

	/* Release Resources */
	release_hw(card);

        /* only free the allocated I/O range if not an S514 adapter */
	if (wandev->hw_opt[0] != SDLA_S514 && !card->configured){
              	release_region(card->hw.port, card->hw.io_range);
	}

	if (!card->configured){
		memset(&card->hw, 0, sizeof(sdlahw_t));
	      	if (card->next){
			memset(&card->next->hw, 0, sizeof(sdlahw_t));
		}
	}
	

	clear_bit(PERI_CRIT,(void*)&wandev->critical);
	return err;
}

static void release_hw (sdla_t *card)
{
	sdla_t *nxt_card;

	
	/* Check if next device exists */
	if (card->next){
		nxt_card = card->next;
		/* If next device is down then release resources */
		if (nxt_card->wandev.state == WAN_UNCONFIGURED){
			if (card->wandev.piggyback){
				/* If this device is piggyback then use
                                 * information of the master device 
				 */
				printk(KERN_INFO "%s: Piggyback shutting down\n",card->devname);
				sdla_down(&card->next->hw);
       				free_irq(card->wandev.irq, card->next);
				card->configured = 0;
				card->next->configured = 0;
				card->wandev.piggyback = 0;
			}else{
				/* Master device shutting down */
				printk(KERN_INFO "%s: Master shutting down\n",card->devname);
				sdla_down(&card->hw);
				free_irq(card->wandev.irq, card);
				card->configured = 0;
				card->next->configured = 0;
			}
		}else{
			printk(KERN_INFO "%s: Device still running %i\n",
				nxt_card->devname,nxt_card->wandev.state);

			card->configured = 1;
		}
	}else{
		printk(KERN_INFO "%s: Master shutting down\n",card->devname);
		sdla_down(&card->hw);
       		free_irq(card->wandev.irq, card);
		card->configured = 0;
	}
	return;
}


/*============================================================================
 * Driver I/O control. 
 * o verify arguments
 * o perform requested action
 *
 * This function is called when router handles one of the reserved user
 * IOCTLs.  Note that 'arg' stil points to user address space.
 */
static int ioctl(struct wan_device* wandev, unsigned cmd, unsigned long arg)
{
	sdla_t* card;
	int err;

	/* sanity checks */
	if ((wandev == NULL) || (wandev->private == NULL))
		return -EFAULT;
	if (wandev->state == WAN_UNCONFIGURED)
		return -ENODEV;

	card = wandev->private;

	if(card->hw.type != SDLA_S514){
		disable_irq(card->hw.irq);
	}

	if (test_bit(SEND_CRIT, (void*)&wandev->critical)) {
		return -EAGAIN;
	}
	
	switch (cmd) {
	case WANPIPE_DUMP:
		err = ioctl_dump(wandev->private, (void*)arg);
		break;

	case WANPIPE_EXEC:
		err = ioctl_exec(wandev->private, (void*)arg, cmd);
		break;
	default:
		err = -EINVAL;
	}
 
	return err;
}

/****** Driver IOCTL Handlers ***********************************************/

/*============================================================================
 * Dump adapter memory to user buffer.
 * o verify request structure
 * o copy request structure to kernel data space
 * o verify length/offset
 * o verify user buffer
 * o copy adapter memory image to user buffer
 *
 * Note: when dumping memory, this routine switches curent dual-port memory
 *	 vector, so care must be taken to avoid racing conditions.
 */
static int ioctl_dump (sdla_t* card, sdla_dump_t* u_dump)
{
	sdla_dump_t dump;
	unsigned winsize;
	unsigned long oldvec;	/* DPM window vector */
	unsigned long smp_flags;
	int err = 0;

	if(copy_from_user((void*)&dump, (void*)u_dump, sizeof(sdla_dump_t)))
		return -EFAULT;
		
	if ((dump.magic != WANPIPE_MAGIC) ||
	    (dump.offset + dump.length > card->hw.memory))
		return -EINVAL;
	
	winsize = card->hw.dpmsize;

	if(card->hw.type != SDLA_S514) {

		lock_adapter_irq(&card->wandev.lock, &smp_flags);
		
                oldvec = card->hw.vector;
                while (dump.length) {
			/* current offset */				
                        unsigned pos = dump.offset % winsize;
			/* current vector */
                        unsigned long vec = dump.offset - pos;
                        unsigned len = (dump.length > (winsize - pos)) ?
                        	(winsize - pos) : dump.length;
			/* relocate window */
                        if (sdla_mapmem(&card->hw, vec) != 0) {
                                err = -EIO;
                                break;
                        }
			
                        if(copy_to_user((void *)dump.ptr,
                                (u8 *)card->hw.dpmbase + pos, len)){ 
				
				unlock_adapter_irq(&card->wandev.lock, &smp_flags);
				return -EFAULT;
			}

                        dump.length     -= len;
                        dump.offset     += len;
                        dump.ptr         = (char*)dump.ptr + len;
                }
		
                sdla_mapmem(&card->hw, oldvec);/* restore DPM window position */
		unlock_adapter_irq(&card->wandev.lock, &smp_flags);
        
	}else {

               if(copy_to_user((void *)dump.ptr,
			       (u8 *)card->hw.dpmbase + dump.offset, dump.length)){
			return -EFAULT;
		}
	}

	return err;
}

/*============================================================================
 * Execute adapter firmware command.
 * o verify request structure
 * o copy request structure to kernel data space
 * o call protocol-specific 'exec' function
 */
static int ioctl_exec (sdla_t* card, sdla_exec_t* u_exec, int cmd)
{
	sdla_exec_t exec;
	int err=0;

	if (card->exec == NULL && cmd == WANPIPE_EXEC){
		return -ENODEV;
	}

	if(copy_from_user((void*)&exec, (void*)u_exec, sizeof(sdla_exec_t)))
		return -EFAULT;

	if ((exec.magic != WANPIPE_MAGIC) || (exec.cmd == NULL))
		return -EINVAL;

	switch (cmd) {
		case WANPIPE_EXEC:	
			err = card->exec(card, exec.cmd, exec.data);
			break;
	}	
	return err;
}

/******* Miscellaneous ******************************************************/

/*============================================================================
 * SDLA Interrupt Service Routine.
 * o acknowledge SDLA hardware interrupt.
 * o call protocol-specific interrupt service routine, if any.
 */
STATIC irqreturn_t sdla_isr (int irq, void* dev_id, struct pt_regs *regs)
{
#define	card	((sdla_t*)dev_id)

	if(card->hw.type == SDLA_S514) {	/* handle interrrupt on S514 */
                u32 int_status;
                unsigned char CPU_no = card->hw.S514_cpu_no[0];
                unsigned char card_found_for_IRQ;
		u8 IRQ_count = 0;

		for(;;) {

			read_S514_int_stat(&card->hw, &int_status);

			/* check if the interrupt is for this device */
 			if(!((unsigned char)int_status &
				(IRQ_CPU_A | IRQ_CPU_B)))
                	        return IRQ_HANDLED;

			/* if the IRQ is for both CPUs on the same adapter, */
			/* then alter the interrupt status so as to handle */
			/* one CPU at a time */
			if(((unsigned char)int_status & (IRQ_CPU_A | IRQ_CPU_B))
				== (IRQ_CPU_A | IRQ_CPU_B)) {
				int_status &= (CPU_no == S514_CPU_A) ?
					~IRQ_CPU_B : ~IRQ_CPU_A;
			}
 
			card_found_for_IRQ = 0;

	             	/* check to see that the CPU number for this device */
			/* corresponds to the interrupt status read */
                	switch (CPU_no) {
                        	case S514_CPU_A:
                                	if((unsigned char)int_status &
						IRQ_CPU_A)
                                        card_found_for_IRQ = 1;
                                break;

	                        case S514_CPU_B:
        	                        if((unsigned char)int_status &
						IRQ_CPU_B)
                                        card_found_for_IRQ = 1;
                                break;
                	}

			/* exit if the interrupt is for another CPU on the */
			/* same IRQ */
			if(!card_found_for_IRQ)
				return IRQ_HANDLED;

       	 		if (!card || 
			   (card->wandev.state == WAN_UNCONFIGURED && !card->configured)){
					printk(KERN_INFO
						"Received IRQ %d for CPU #%c\n",
						irq, CPU_no);
					printk(KERN_INFO
						"IRQ for unconfigured adapter\n");
					S514_intack(&card->hw, int_status);
					return IRQ_HANDLED;
       			}

	        	if (card->in_isr) {
        	       		printk(KERN_INFO
					"%s: interrupt re-entrancy on IRQ %d\n",
                       			card->devname, card->wandev.irq);
				S514_intack(&card->hw, int_status);
 				return IRQ_HANDLED;
       			}

			spin_lock(&card->wandev.lock);
			if (card->next){
				spin_lock(&card->next->wandev.lock);
			}
				
	               	S514_intack(&card->hw, int_status);
       			if (card->isr)
				card->isr(card);

			if (card->next){
				spin_unlock(&card->next->wandev.lock);
			}
			spin_unlock(&card->wandev.lock);

			/* handle a maximum of two interrupts (one for each */
			/* CPU on the adapter) before returning */  
			if((++ IRQ_count) == 2)
				return IRQ_HANDLED;
		}
	}

	else {			/* handle interrupt on S508 adapter */

		if (!card || ((card->wandev.state == WAN_UNCONFIGURED) && !card->configured))
			return IRQ_HANDLED;

		if (card->in_isr) {
			printk(KERN_INFO
				"%s: interrupt re-entrancy on IRQ %d!\n",
				card->devname, card->wandev.irq);
			return IRQ_HANDLED;
		}

		spin_lock(&card->wandev.lock);
		if (card->next){
			spin_lock(&card->next->wandev.lock);
		}
	
		sdla_intack(&card->hw);
		if (card->isr)
			card->isr(card);
		
		if (card->next){
			spin_unlock(&card->next->wandev.lock);
		}
		spin_unlock(&card->wandev.lock);

	}
        return IRQ_HANDLED;
#undef	card
}

/*============================================================================
 * This routine is called by the protocol-specific modules when network
 * interface is being open.  The only reason we need this, is because we
 * have to call MOD_INC_USE_COUNT, but cannot include 'module.h' where it's
 * defined more than once into the same kernel module.
 */
void wanpipe_open (sdla_t* card)
{
	++card->open_cnt;
}

/*============================================================================
 * This routine is called by the protocol-specific modules when network
 * interface is being closed.  The only reason we need this, is because we
 * have to call MOD_DEC_USE_COUNT, but cannot include 'module.h' where it's
 * defined more than once into the same kernel module.
 */
void wanpipe_close (sdla_t* card)
{
	--card->open_cnt;
}

/*============================================================================
 * Set WAN device state.
 */
void wanpipe_set_state (sdla_t* card, int state)
{
	if (card->wandev.state != state) {
		switch (state) {
		case WAN_CONNECTED:
			printk (KERN_INFO "%s: link connected!\n",
				card->devname);
			break;

		case WAN_CONNECTING:
			printk (KERN_INFO "%s: link connecting...\n",
				card->devname);
			break;

		case WAN_DISCONNECTED:
			printk (KERN_INFO "%s: link disconnected!\n",
				card->devname);
			break;
		}
		card->wandev.state = state;
	}
	card->state_tick = jiffies;
}

sdla_t * wanpipe_find_card (char *name)
{
	int cnt;
	for (cnt = 0; cnt < ncards; ++ cnt) {
		sdla_t* card = &card_array[cnt];
		if (!strcmp(card->devname,name))
			return card;
	}
	return NULL;
}

sdla_t * wanpipe_find_card_num (int num)
{
	if (num < 1 || num > ncards)
		return NULL;	
	num--;
	return &card_array[num];
}

/*
 * @work_pointer:	work_struct to be done;
 * 			should already have PREPARE_WORK() or
 * 			  INIT_WORK() done on it by caller;
 */
void wanpipe_queue_work (struct work_struct *work_pointer)
{
	if (test_and_set_bit(1, (void*)&wanpipe_bh_critical))
		printk(KERN_INFO "CRITICAL IN QUEUING WORK\n");

	queue_work(wanpipe_wq, work_pointer);
	clear_bit(1,(void*)&wanpipe_bh_critical);
}

void wakeup_sk_bh(struct net_device *dev)
{
	wanpipe_common_t *chan = dev->priv;

	if (test_bit(0,&chan->common_critical))
		return;
	
	if (chan->sk && chan->tx_timer){
		chan->tx_timer->expires=jiffies+1;
		add_timer(chan->tx_timer);
	}
}

int change_dev_flags(struct net_device *dev, unsigned flags)
{
	struct ifreq if_info;
	mm_segment_t fs = get_fs();
	int err;

	memset(&if_info, 0, sizeof(if_info));
	strcpy(if_info.ifr_name, dev->name);
	if_info.ifr_flags = flags;	

	set_fs(get_ds());     /* get user space block */ 
	err = devinet_ioctl(SIOCSIFFLAGS, &if_info);
	set_fs(fs);

	return err;
}

unsigned long get_ip_address(struct net_device *dev, int option)
{
	
	struct in_ifaddr *ifaddr;
	struct in_device *in_dev;
	unsigned long addr = 0;

	rcu_read_lock();
	if ((in_dev = __in_dev_get_rcu(dev)) == NULL){
		goto out;
	}

	if ((ifaddr = in_dev->ifa_list)== NULL ){
		goto out;
	}
	
	switch (option){

	case WAN_LOCAL_IP:
		addr = ifaddr->ifa_local;
		break;
	
	case WAN_POINTOPOINT_IP:
		addr = ifaddr->ifa_address;
		break;	

	case WAN_NETMASK_IP:
		addr = ifaddr->ifa_mask;
		break;

	case WAN_BROADCAST_IP:
		addr = ifaddr->ifa_broadcast;
		break;
	default:
		break;
	}

out:
	rcu_read_unlock();
	return addr;
}	

void add_gateway(sdla_t *card, struct net_device *dev)
{
	mm_segment_t oldfs;
	struct rtentry route;
	int res;

	memset((char*)&route,0,sizeof(struct rtentry));

	((struct sockaddr_in *)
		&(route.rt_dst))->sin_addr.s_addr = 0;
	((struct sockaddr_in *)
		&(route.rt_dst))->sin_family = AF_INET;

	((struct sockaddr_in *)
		&(route.rt_genmask))->sin_addr.s_addr = 0;
	((struct sockaddr_in *) 
		&(route.rt_genmask)) ->sin_family = AF_INET;


	route.rt_flags = 0;  
	route.rt_dev = dev->name;

	oldfs = get_fs();
	set_fs(get_ds());
	res = ip_rt_ioctl(SIOCADDRT,&route);
	set_fs(oldfs);

	if (res == 0){
		printk(KERN_INFO "%s: Gateway added for %s\n",
			card->devname,dev->name);
	}

	return;
}

MODULE_LICENSE("GPL");

/****** End *********************************************************/

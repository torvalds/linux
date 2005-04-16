/*****************************************************************************
* wanpipe_multppp.c Multi-Port PPP driver module.
*
* Authors: 	Nenad Corbic <ncorbic@sangoma.com>
*
* Copyright:	(c) 1995-2001 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Dec 15 2000   Updated for 2.4.X kernel
* Nov 15 2000   Fixed the SyncPPP support for kernels 2.2.16 and higher.
*   		The pppstruct has changed.
* Jul 13 2000	Using the kernel Syncppp module on top of RAW Wanpipe CHDLC
*  		module.
*****************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>	/* printk(), and other useful stuff */
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/string.h>	/* inline memset(), etc. */
#include <linux/slab.h>	/* kmalloc(), kfree() */
#include <linux/wanrouter.h>	/* WAN router definitions */
#include <linux/wanpipe.h>	/* WANPIPE common user API definitions */
#include <linux/if_arp.h>	/* ARPHRD_* defines */

#include <linux/in.h>		/* sockaddr_in */
#include <linux/inet.h>	
#include <linux/if.h>
#include <asm/byteorder.h>	/* htons(), etc. */
#include <linux/sdlapci.h>
#include <asm/io.h>

#include <linux/sdla_chdlc.h>		/* CHDLC firmware API definitions */
#include <linux/sdla_asy.h>           	/* CHDLC (async) API definitions */

#include <linux/if_wanpipe_common.h>    /* Socket Driver common area */
#include <linux/if_wanpipe.h>		


#include <linux/inetdevice.h>
#include <asm/uaccess.h>

#include <net/syncppp.h>


/****** Defines & Macros ****************************************************/

#ifdef	_DEBUG_
#define	STATIC
#else
#define	STATIC		static
#endif

/* reasons for enabling the timer interrupt on the adapter */
#define TMR_INT_ENABLED_UDP   	0x01
#define TMR_INT_ENABLED_UPDATE	0x02
#define TMR_INT_ENABLED_CONFIG  0x04
 
#define	CHDLC_DFLT_DATA_LEN	1500		/* default MTU */
#define CHDLC_HDR_LEN		1

#define IFF_POINTTOPOINT 0x10

#define CHDLC_API 0x01

#define PORT(x)   (x == 0 ? "PRIMARY" : "SECONDARY" )
#define MAX_BH_BUFF	10

#define CRC_LENGTH 	2 
#define PPP_HEADER_LEN 	4
 
/******Data Structures*****************************************************/

/* This structure is placed in the private data area of the device structure.
 * The card structure used to occupy the private area but now the following 
 * structure will incorporate the card structure along with CHDLC specific data
 */

typedef struct chdlc_private_area
{
	void *if_ptr;				/* General Pointer used by SPPP */
	wanpipe_common_t common;
	sdla_t		*card;
	int 		TracingEnabled;		/* For enabling Tracing */
	unsigned long 	curr_trace_addr;	/* Used for Tracing */
	unsigned long 	start_trace_addr;
	unsigned long 	end_trace_addr;
	unsigned long 	base_addr_trace_buffer;
	unsigned long 	end_addr_trace_buffer;
	unsigned short 	number_trace_elements;
	unsigned  	available_buffer_space;
	unsigned long 	router_start_time;
	unsigned char 	route_status;
	unsigned char 	route_removed;
	unsigned long 	tick_counter;		/* For 5s timeout counter */
	unsigned long 	router_up_time;
        u32             IP_address;		/* IP addressing */
        u32             IP_netmask;
	unsigned char  mc;			/* Mulitcast support on/off */
	unsigned short udp_pkt_lgth;		/* udp packet processing */
	char udp_pkt_src;
	char udp_pkt_data[MAX_LGTH_UDP_MGNT_PKT];
	unsigned short timer_int_enabled;
	char update_comms_stats;		/* updating comms stats */

	//FIXME: add driver stats as per frame relay!

} chdlc_private_area_t;

/* Route Status options */
#define NO_ROUTE	0x00
#define ADD_ROUTE	0x01
#define ROUTE_ADDED	0x02
#define REMOVE_ROUTE	0x03


/* variable for keeping track of enabling/disabling FT1 monitor status */
static int rCount = 0;

/* variable for tracking how many interfaces to open for WANPIPE on the
   two ports */

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

/****** Function Prototypes *************************************************/
/* WAN link driver entry points. These are called by the WAN router module. */
static int update(struct wan_device* wandev);
static int new_if(struct wan_device* wandev, struct net_device* dev,
		  wanif_conf_t* conf);
static int del_if(struct wan_device* wandev, struct net_device* dev);

/* Network device interface */
static int if_init(struct net_device* dev);
static int if_open(struct net_device* dev);
static int if_close(struct net_device* dev);
static int if_send(struct sk_buff* skb, struct net_device* dev);
static struct net_device_stats* if_stats(struct net_device* dev);

static void if_tx_timeout(struct net_device *dev);

/* CHDLC Firmware interface functions */
static int chdlc_configure 	(sdla_t* card, void* data);
static int chdlc_comm_enable 	(sdla_t* card);
static int chdlc_comm_disable 	(sdla_t* card);
static int chdlc_read_version 	(sdla_t* card, char* str);
static int chdlc_set_intr_mode 	(sdla_t* card, unsigned mode);
static int chdlc_send (sdla_t* card, void* data, unsigned len);
static int chdlc_read_comm_err_stats (sdla_t* card);
static int chdlc_read_op_stats (sdla_t* card);
static int config_chdlc (sdla_t *card);


/* Miscellaneous CHDLC Functions */
static int set_chdlc_config (sdla_t* card);
static void init_chdlc_tx_rx_buff(sdla_t* card, struct net_device *dev);
static int chdlc_error (sdla_t *card, int err, CHDLC_MAILBOX_STRUCT *mb);
static int process_chdlc_exception(sdla_t *card);
static int process_global_exception(sdla_t *card);
static int update_comms_stats(sdla_t* card,
        chdlc_private_area_t* chdlc_priv_area);
static void port_set_state (sdla_t *card, int);

/* Interrupt handlers */
static void wsppp_isr (sdla_t* card);
static void rx_intr (sdla_t* card);
static void timer_intr(sdla_t *);

/* Miscellaneous functions */
static int reply_udp( unsigned char *data, unsigned int mbox_len );
static int intr_test( sdla_t* card);
static int udp_pkt_type( struct sk_buff *skb , sdla_t* card);
static int store_udp_mgmt_pkt(char udp_pkt_src, sdla_t* card,
			      struct sk_buff *skb, struct net_device* dev,
			      chdlc_private_area_t* chdlc_priv_area);
static int process_udp_mgmt_pkt(sdla_t* card, struct net_device* dev,  
				chdlc_private_area_t* chdlc_priv_area);
static unsigned short calc_checksum (char *, int);
static void s508_lock (sdla_t *card, unsigned long *smp_flags);
static void s508_unlock (sdla_t *card, unsigned long *smp_flags);
static void send_ppp_term_request(struct net_device *dev);


static int  Intr_test_counter;
/****** Public Functions ****************************************************/

/*============================================================================
 * Cisco HDLC protocol initialization routine.
 *
 * This routine is called by the main WANPIPE module during setup.  At this
 * point adapter is completely initialized and firmware is running.
 *  o read firmware version (to make sure it's alive)
 *  o configure adapter
 *  o initialize protocol-specific fields of the adapter data space.
 *
 * Return:	0	o.k.
 *		< 0	failure.
 */
int wsppp_init (sdla_t* card, wandev_conf_t* conf)
{
	unsigned char port_num;
	int err;
	unsigned long max_permitted_baud = 0;
	SHARED_MEMORY_INFO_STRUCT *flags;

	union
		{
		char str[80];
		} u;
	volatile CHDLC_MAILBOX_STRUCT* mb;
	CHDLC_MAILBOX_STRUCT* mb1;
	unsigned long timeout;

	/* Verify configuration ID */
	if (conf->config_id != WANCONFIG_MPPP) {
		printk(KERN_INFO "%s: invalid configuration ID %u!\n",
				  card->devname, conf->config_id);
		return -EINVAL;
	}

	/* Find out which Port to use */
	if ((conf->comm_port == WANOPT_PRI) || (conf->comm_port == WANOPT_SEC)){
		if (card->next){

			if (conf->comm_port != card->next->u.c.comm_port){
				card->u.c.comm_port = conf->comm_port;
			}else{
				printk(KERN_ERR "%s: ERROR - %s port used!\n",
        		        	card->wandev.name, PORT(conf->comm_port));
				return -EINVAL;
			}
		}else{
			card->u.c.comm_port = conf->comm_port;
		}
	}else{
		printk(KERN_ERR "%s: ERROR - Invalid Port Selected!\n",
                			card->wandev.name);
		return -EINVAL;
	}
	

	/* Initialize protocol-specific fields */
	if(card->hw.type != SDLA_S514){

		if (card->u.c.comm_port == WANOPT_PRI){	
			card->mbox  = (void *) card->hw.dpmbase;
		}else{
			card->mbox  = (void *) card->hw.dpmbase + 
				SEC_BASE_ADDR_MB_STRUCT - PRI_BASE_ADDR_MB_STRUCT;
		}	
	}else{ 
		/* for a S514 adapter, set a pointer to the actual mailbox in the */
		/* allocated virtual memory area */
		if (card->u.c.comm_port == WANOPT_PRI){
			card->mbox = (void *) card->hw.dpmbase + PRI_BASE_ADDR_MB_STRUCT;
		}else{
			card->mbox = (void *) card->hw.dpmbase + SEC_BASE_ADDR_MB_STRUCT;
		}	
	}

	mb = mb1 = card->mbox;

	if (!card->configured){

		/* The board will place an 'I' in the return code to indicate that it is
	   	ready to accept commands.  We expect this to be completed in less
           	than 1 second. */

		timeout = jiffies;
		while (mb->return_code != 'I')	/* Wait 1s for board to initialize */
			if ((jiffies - timeout) > 1*HZ) break;

		if (mb->return_code != 'I') {
			printk(KERN_INFO
				"%s: Initialization not completed by adapter\n",
				card->devname);
			printk(KERN_INFO "Please contact Sangoma representative.\n");
			return -EIO;
		}
	}

	/* Read firmware version.  Note that when adapter initializes, it
	 * clears the mailbox, so it may appear that the first command was
	 * executed successfully when in fact it was merely erased. To work
	 * around this, we execute the first command twice.
	 */

	if (chdlc_read_version(card, u.str))
		return -EIO;

	printk(KERN_INFO "%s: Running Raw CHDLC firmware v%s\n" 
			 "%s: for Multi-Port PPP protocol.\n",
			card->devname,u.str,card->devname); 

	card->isr			= &wsppp_isr;
	card->poll			= NULL;
	card->exec			= NULL;
	card->wandev.update		= &update;
 	card->wandev.new_if		= &new_if;
	card->wandev.del_if		= &del_if;
	card->wandev.udp_port   	= conf->udp_port;

	card->wandev.new_if_cnt = 0;

	/* reset the number of times the 'update()' proc has been called */
	card->u.c.update_call_count = 0;
	
	card->wandev.ttl = conf->ttl;
	card->wandev.interface = conf->interface; 

	if ((card->u.c.comm_port == WANOPT_SEC && conf->interface == WANOPT_V35)&&
	    card->hw.type != SDLA_S514){
		printk(KERN_INFO "%s: ERROR - V35 Interface not supported on S508 %s port \n",
			card->devname, PORT(card->u.c.comm_port));
		return -EIO;
	}


	card->wandev.clocking = conf->clocking;

	port_num = card->u.c.comm_port;

	/* Setup Port Bps */

	if(card->wandev.clocking) {
		if((port_num == WANOPT_PRI) || card->u.c.receive_only) {
			/* For Primary Port 0 */
               		max_permitted_baud =
				(card->hw.type == SDLA_S514) ?
				PRI_MAX_BAUD_RATE_S514 : 
				PRI_MAX_BAUD_RATE_S508;
		}
		else if(port_num == WANOPT_SEC) {
			/* For Secondary Port 1 */
                        max_permitted_baud =
                               (card->hw.type == SDLA_S514) ?
                                SEC_MAX_BAUD_RATE_S514 :
                                SEC_MAX_BAUD_RATE_S508;
                        }
  
			if(conf->bps > max_permitted_baud) {
				conf->bps = max_permitted_baud;
				printk(KERN_INFO "%s: Baud too high!\n",
					card->wandev.name);
 				printk(KERN_INFO "%s: Baud rate set to %lu bps\n", 
					card->wandev.name, max_permitted_baud);
			}
                             
			card->wandev.bps = conf->bps;
	}else{
        	card->wandev.bps = 0;
  	}

	/* Setup the Port MTU */
	if((port_num == WANOPT_PRI) || card->u.c.receive_only) {

		/* For Primary Port 0 */
		card->wandev.mtu =
			(conf->mtu >= MIN_LGTH_CHDLC_DATA_CFG) ?
			min_t(unsigned int, conf->mtu, PRI_MAX_NO_DATA_BYTES_IN_FRAME) :
			CHDLC_DFLT_DATA_LEN;
	} else if(port_num == WANOPT_SEC) { 
		/* For Secondary Port 1 */
		card->wandev.mtu =
			(conf->mtu >= MIN_LGTH_CHDLC_DATA_CFG) ?
			min_t(unsigned int, conf->mtu, SEC_MAX_NO_DATA_BYTES_IN_FRAME) :
			CHDLC_DFLT_DATA_LEN;
	}

	/* Add on a PPP Header */
	card->wandev.mtu += PPP_HEADER_LEN;

	/* Set up the interrupt status area */
	/* Read the CHDLC Configuration and obtain: 
	 *	Ptr to shared memory infor struct
         * Use this pointer to calculate the value of card->u.c.flags !
 	 */
	mb1->buffer_length = 0;
	mb1->command = READ_CHDLC_CONFIGURATION;
	err = sdla_exec(mb1) ? mb1->return_code : CMD_TIMEOUT;
	if(err != COMMAND_OK) {
		clear_bit(1, (void*)&card->wandev.critical);

                if(card->hw.type != SDLA_S514)
                	enable_irq(card->hw.irq);

		chdlc_error(card, err, mb1);
		return -EIO;
	}

	if(card->hw.type == SDLA_S514){
               	card->u.c.flags = (void *)(card->hw.dpmbase +
               		(((CHDLC_CONFIGURATION_STRUCT *)mb1->data)->
			ptr_shared_mem_info_struct));
        }else{
                card->u.c.flags = (void *)(card->hw.dpmbase +
                        (((CHDLC_CONFIGURATION_STRUCT *)mb1->data)->
			ptr_shared_mem_info_struct % SDLA_WINDOWSIZE));
	}
	
	flags = card->u.c.flags;
	
	/* This is for the ports link state */
	card->wandev.state = WAN_DUALPORT;
	card->u.c.state = WAN_DISCONNECTED;


	if (!card->wandev.piggyback){
		err = intr_test(card);

		if(err || (Intr_test_counter < MAX_INTR_TEST_COUNTER)) { 
			printk(KERN_ERR "%s: Interrupt test failed (%i)\n",
					card->devname, Intr_test_counter);
			printk(KERN_ERR "%s: Please choose another interrupt\n",
					card->devname);
			return  -EIO;
		}
			
		printk(KERN_INFO "%s: Interrupt test passed (%i)\n", 
				card->devname, Intr_test_counter);
	}


	if (chdlc_set_intr_mode(card, APP_INT_ON_TIMER)){
		printk (KERN_INFO "%s: Failed to set interrupt triggers!\n",
				card->devname);
		return -EIO;	
        }
	
	/* Mask the Timer interrupt */
	flags->interrupt_info_struct.interrupt_permission &= 
		~APP_INT_ON_TIMER;

	printk(KERN_INFO "\n");

	return 0;
}

/******* WAN Device Driver Entry Points *************************************/

/*============================================================================
 * Update device status & statistics
 * This procedure is called when updating the PROC file system and returns
 * various communications statistics. These statistics are accumulated from 3 
 * different locations:
 * 	1) The 'if_stats' recorded for the device.
 * 	2) Communication error statistics on the adapter.
 *      3) CHDLC operational statistics on the adapter.
 * The board level statistics are read during a timer interrupt. Note that we 
 * read the error and operational statistics during consecitive timer ticks so
 * as to minimize the time that we are inside the interrupt handler.
 *
 */
static int update(struct wan_device* wandev)
{
	sdla_t* card = wandev->private;
 	struct net_device* dev;
        volatile chdlc_private_area_t* chdlc_priv_area;
        SHARED_MEMORY_INFO_STRUCT *flags;
	unsigned long timeout;

	/* sanity checks */
	if((wandev == NULL) || (wandev->private == NULL))
		return -EFAULT;
	
	if(wandev->state == WAN_UNCONFIGURED)
		return -ENODEV;

	/* more sanity checks */
        if(!card->u.c.flags)
                return -ENODEV;

	if((dev=card->wandev.dev) == NULL)
		return -ENODEV;

	if((chdlc_priv_area=dev->priv) == NULL)
		return -ENODEV;

      	flags = card->u.c.flags;

       	if(chdlc_priv_area->update_comms_stats){
		return -EAGAIN;
	}
			
	/* we will need 2 timer interrupts to complete the */
	/* reading of the statistics */
	chdlc_priv_area->update_comms_stats = 2;
       	flags->interrupt_info_struct.interrupt_permission |= APP_INT_ON_TIMER;
	chdlc_priv_area->timer_int_enabled = TMR_INT_ENABLED_UPDATE;
  
	/* wait a maximum of 1 second for the statistics to be updated */ 
        timeout = jiffies;
        for(;;) {
		if(chdlc_priv_area->update_comms_stats == 0)
			break;
                if ((jiffies - timeout) > (1 * HZ)){
    			chdlc_priv_area->update_comms_stats = 0;
 			chdlc_priv_area->timer_int_enabled &=
				~TMR_INT_ENABLED_UPDATE; 
 			return -EAGAIN;
		}
        }

	return 0;
}


/*============================================================================
 * Create new logical channel.
 * This routine is called by the router when ROUTER_IFNEW IOCTL is being
 * handled.
 * o parse media- and hardware-specific configuration
 * o make sure that a new channel can be created
 * o allocate resources, if necessary
 * o prepare network device structure for registaration.
 *
 * Return:	0	o.k.
 *		< 0	failure (channel will not be created)
 */
static int new_if(struct wan_device* wandev, struct net_device* pdev,
		  wanif_conf_t* conf)
{

	struct ppp_device *pppdev = (struct ppp_device *)pdev;
	struct net_device *dev = NULL;
	struct sppp *sp;
	sdla_t* card = wandev->private;
	chdlc_private_area_t* chdlc_priv_area;
	
	if ((conf->name[0] == '\0') || (strlen(conf->name) > WAN_IFNAME_SZ)) {
		printk(KERN_INFO "%s: invalid interface name!\n",
			card->devname);
		return -EINVAL;
	}
		
	/* allocate and initialize private data */
	chdlc_priv_area = kmalloc(sizeof(chdlc_private_area_t), GFP_KERNEL);
	
	if(chdlc_priv_area == NULL) 
		return -ENOMEM;

	memset(chdlc_priv_area, 0, sizeof(chdlc_private_area_t));

	chdlc_priv_area->card = card; 

	/* initialize data */
	strcpy(card->u.c.if_name, conf->name);

	if(card->wandev.new_if_cnt > 0) {
                kfree(chdlc_priv_area);
		return -EEXIST;
	}

	card->wandev.new_if_cnt++;

	chdlc_priv_area->TracingEnabled = 0;

	//We don't need this any more
	chdlc_priv_area->route_status = NO_ROUTE;
	chdlc_priv_area->route_removed = 0;

	printk(KERN_INFO "%s: Firmware running in HDLC STREAMING Mode\n",
		wandev->name);
	
	/* Setup wanpipe as a router (WANPIPE) or as an API */
	if( strcmp(conf->usedby, "WANPIPE") == 0) {
		printk(KERN_INFO "%s: Driver running in WANPIPE mode!\n",
			wandev->name);
		card->u.c.usedby = WANPIPE;
	} else {
		printk(KERN_INFO 
			"%s: API Mode is not supported for SyncPPP!\n",
			wandev->name);
		kfree(chdlc_priv_area);
		return -EINVAL;
	}

	/* Get Multicast Information */
	chdlc_priv_area->mc = conf->mc;


	chdlc_priv_area->if_ptr = pppdev;

	/* prepare network device data space for registration */

	strcpy(dev->name,card->u.c.if_name);

	/* Attach PPP protocol layer to pppdev
	 * The sppp_attach() will initilize the dev structure
         * and setup ppp layer protocols.
         * All we have to do is to bind in:
         *        if_open(), if_close(), if_send() and get_stats() functions.
         */
	sppp_attach(pppdev);
	dev = pppdev->dev;
	sp = &pppdev->sppp;
	
	/* Enable PPP Debugging */
	// FIXME Fix this up somehow
	//sp->pp_flags |= PP_DEBUG; 	
	sp->pp_flags &= ~PP_CISCO;

	dev->init = &if_init;
	dev->priv = chdlc_priv_area;
	
	return 0;
}




/*============================================================================
 * Delete logical channel.
 */
static int del_if(struct wan_device* wandev, struct net_device* dev)
{
	chdlc_private_area_t *chdlc_priv_area = dev->priv;
	sdla_t *card = chdlc_priv_area->card;
	unsigned long smp_lock;
	
	/* Detach the PPP layer */
	printk(KERN_INFO "%s: Detaching SyncPPP Module from %s\n",
			wandev->name,dev->name);

	lock_adapter_irq(&wandev->lock,&smp_lock);

	sppp_detach(dev);
	chdlc_priv_area->if_ptr=NULL;
	
	chdlc_set_intr_mode(card, 0);
	if (card->u.c.comm_enabled)
		chdlc_comm_disable(card);
	unlock_adapter_irq(&wandev->lock,&smp_lock);
	
	port_set_state(card, WAN_DISCONNECTED);

	return 0;
}


/****** Network Device Interface ********************************************/

/*============================================================================
 * Initialize Linux network interface.
 *
 * This routine is called only once for each interface, during Linux network
 * interface registration.  Returning anything but zero will fail interface
 * registration.
 */
static int if_init(struct net_device* dev)
{
	chdlc_private_area_t* chdlc_priv_area = dev->priv;
	sdla_t* card = chdlc_priv_area->card;
	struct wan_device* wandev = &card->wandev;
	
	/* NOTE: Most of the dev initialization was
         *       done in sppp_attach(), called by new_if() 
         *       function. All we have to do here is
         *       to link four major routines below. 
         */

	/* Initialize device driver entry points */
	dev->open		= &if_open;
	dev->stop		= &if_close;
	dev->hard_start_xmit	= &if_send;
	dev->get_stats		= &if_stats;
	dev->tx_timeout		= &if_tx_timeout;
	dev->watchdog_timeo	= TX_TIMEOUT;


	/* Initialize hardware parameters */
	dev->irq	= wandev->irq;
	dev->dma	= wandev->dma;
	dev->base_addr	= wandev->ioport;
	dev->mem_start	= wandev->maddr;
	dev->mem_end	= wandev->maddr + wandev->msize - 1;

	/* Set transmit buffer queue length 
         * If we over fill this queue the packets will
         * be droped by the kernel.
         * sppp_attach() sets this to 10, but
         * 100 will give us more room at low speeds.
	 */
        dev->tx_queue_len = 100;
   
	return 0;
}


/*============================================================================
 * Handle transmit timeout event from netif watchdog
 */
static void if_tx_timeout(struct net_device *dev)
{
    	chdlc_private_area_t* chan = dev->priv;
	sdla_t *card = chan->card;
	
	/* If our device stays busy for at least 5 seconds then we will
	 * kick start the device by making dev->tbusy = 0.  We expect
	 * that our device never stays busy more than 5 seconds. So this                 
	 * is only used as a last resort.
	 */

	++card->wandev.stats.collisions;

	printk (KERN_INFO "%s: Transmit timed out on %s\n", card->devname,dev->name);
	netif_wake_queue (dev);
}


/*============================================================================
 * Open network interface.
 * o enable communications and interrupts.
 * o prevent module from unloading by incrementing use count
 *
 * Return 0 if O.k. or errno.
 */
static int if_open(struct net_device* dev)
{
	chdlc_private_area_t* chdlc_priv_area = dev->priv;
	sdla_t* card = chdlc_priv_area->card;
	struct timeval tv;
	SHARED_MEMORY_INFO_STRUCT *flags = card->u.c.flags;

	/* Only one open per interface is allowed */
	if (netif_running(dev))
		return -EBUSY;

	/* Start PPP Layer */
	if (sppp_open(dev)){
		return -EIO;
	}

	do_gettimeofday(&tv);
	chdlc_priv_area->router_start_time = tv.tv_sec;
 
	netif_start_queue(dev);
	
	wanpipe_open(card);

	chdlc_priv_area->timer_int_enabled |= TMR_INT_ENABLED_CONFIG;
	flags->interrupt_info_struct.interrupt_permission |= APP_INT_ON_TIMER;
	return 0;
}

/*============================================================================
 * Close network interface.
 * o if this is the last close, then disable communications and interrupts.
 * o reset flags.
 */
static int if_close(struct net_device* dev)
{
	chdlc_private_area_t* chdlc_priv_area = dev->priv;
	sdla_t* card = chdlc_priv_area->card;

	/* Stop the PPP Layer */
	sppp_close(dev);
	netif_stop_queue(dev);

	wanpipe_close(card);
	
	return 0;
}

/*============================================================================
 * Send a packet on a network interface.
 * o set tbusy flag (marks start of the transmission) to block a timer-based
 *   transmit from overlapping.
 * o check link state. If link is not up, then drop the packet.
 * o execute adapter send command.
 * o free socket buffer
 *
 * Return:	0	complete (socket buffer must be freed)
 *		non-0	packet may be re-transmitted (tbusy must be set)
 *
 * Notes:
 * 1. This routine is called either by the protocol stack or by the "net
 *    bottom half" (with interrupts enabled).
 * 2. Setting tbusy flag will inhibit further transmit requests from the
 *    protocol stack and can be used for flow control with protocol layer.
 */
static int if_send(struct sk_buff* skb, struct net_device* dev)
{
	chdlc_private_area_t *chdlc_priv_area = dev->priv;
	sdla_t *card = chdlc_priv_area->card;
	SHARED_MEMORY_INFO_STRUCT *flags = card->u.c.flags;
	INTERRUPT_INFORMATION_STRUCT *chdlc_int = &flags->interrupt_info_struct;
	int udp_type = 0;
	unsigned long smp_flags;
	int err=0;

	netif_stop_queue(dev);

	
	if (skb == NULL){
		/* If we get here, some higher layer thinks we've missed an
		 * tx-done interrupt.
		 */
		printk(KERN_INFO "%s: Received NULL skb buffer! interface %s got kicked!\n",
			card->devname, dev->name);

		netif_wake_queue(dev);
		return 0;
	}

   	if (ntohs(skb->protocol) != htons(PVC_PROT)){
		/* check the udp packet type */
		
		udp_type = udp_pkt_type(skb, card);
		if (udp_type == UDP_CPIPE_TYPE){
                        if(store_udp_mgmt_pkt(UDP_PKT_FRM_STACK, card, skb, dev,
                                chdlc_priv_area)){
				chdlc_int->interrupt_permission |=
					APP_INT_ON_TIMER;
			}
			netif_start_queue(dev);
			return 0;
		}
        }

	/* Lock the 508 Card: SMP is supported */
      	if(card->hw.type != SDLA_S514){
		s508_lock(card,&smp_flags);
	} 

    	if (test_and_set_bit(SEND_CRIT, (void*)&card->wandev.critical)){
	
		printk(KERN_INFO "%s: Critical in if_send: %lx\n",
					card->wandev.name,card->wandev.critical);
                ++card->wandev.stats.tx_dropped;
		netif_start_queue(dev);
		goto if_send_crit_exit;
	}

	if (card->wandev.state != WAN_CONNECTED){
		++card->wandev.stats.tx_dropped;
		netif_start_queue(dev);
		goto if_send_crit_exit;
	}
	
	if (chdlc_send(card, skb->data, skb->len)){
		netif_stop_queue(dev);

	}else{
		++card->wandev.stats.tx_packets;
       		card->wandev.stats.tx_bytes += skb->len;
		dev->trans_start = jiffies;
		netif_start_queue(dev);
	}	

if_send_crit_exit:
	if (!(err=netif_queue_stopped(dev))){
                dev_kfree_skb_any(skb);
	}else{
		chdlc_priv_area->tick_counter = jiffies;
		chdlc_int->interrupt_permission |= APP_INT_ON_TX_FRAME;
	}

	clear_bit(SEND_CRIT, (void*)&card->wandev.critical);
	if(card->hw.type != SDLA_S514){
		s508_unlock(card,&smp_flags);
	}

	return err;
}


/*============================================================================
 * Reply to UDP Management system.
 * Return length of reply.
 */
static int reply_udp( unsigned char *data, unsigned int mbox_len )
{

	unsigned short len, udp_length, temp, ip_length;
	unsigned long ip_temp;
	int even_bound = 0;
  	chdlc_udp_pkt_t *c_udp_pkt = (chdlc_udp_pkt_t *)data;
	 
	/* Set length of packet */
	len = sizeof(ip_pkt_t)+ 
	      sizeof(udp_pkt_t)+
	      sizeof(wp_mgmt_t)+
	      sizeof(cblock_t)+
	      sizeof(trace_info_t)+ 
	      mbox_len;

	/* fill in UDP reply */
	c_udp_pkt->wp_mgmt.request_reply = UDPMGMT_REPLY;
   
	/* fill in UDP length */
	udp_length = sizeof(udp_pkt_t)+ 
		     sizeof(wp_mgmt_t)+
		     sizeof(cblock_t)+
	             sizeof(trace_info_t)+
		     mbox_len; 

 	/* put it on an even boundary */
	if ( udp_length & 0x0001 ) {
		udp_length += 1;
		len += 1;
		even_bound = 1;
	}  

	temp = (udp_length<<8)|(udp_length>>8);
	c_udp_pkt->udp_pkt.udp_length = temp;
		 
	/* swap UDP ports */
	temp = c_udp_pkt->udp_pkt.udp_src_port;
	c_udp_pkt->udp_pkt.udp_src_port = 
			c_udp_pkt->udp_pkt.udp_dst_port; 
	c_udp_pkt->udp_pkt.udp_dst_port = temp;

	/* add UDP pseudo header */
	temp = 0x1100;
	*((unsigned short *)(c_udp_pkt->data+mbox_len+even_bound)) = temp;	
	temp = (udp_length<<8)|(udp_length>>8);
	*((unsigned short *)(c_udp_pkt->data+mbox_len+even_bound+2)) = temp;

		 
	/* calculate UDP checksum */
	c_udp_pkt->udp_pkt.udp_checksum = 0;
	c_udp_pkt->udp_pkt.udp_checksum = calc_checksum(&data[UDP_OFFSET],udp_length+UDP_OFFSET);

	/* fill in IP length */
	ip_length = len;
	temp = (ip_length<<8)|(ip_length>>8);
	c_udp_pkt->ip_pkt.total_length = temp;
  
	/* swap IP addresses */
	ip_temp = c_udp_pkt->ip_pkt.ip_src_address;
	c_udp_pkt->ip_pkt.ip_src_address = c_udp_pkt->ip_pkt.ip_dst_address;
	c_udp_pkt->ip_pkt.ip_dst_address = ip_temp;

	/* fill in IP checksum */
	c_udp_pkt->ip_pkt.hdr_checksum = 0;
	c_udp_pkt->ip_pkt.hdr_checksum = calc_checksum(data,sizeof(ip_pkt_t));

	return len;

} /* reply_udp */

unsigned short calc_checksum (char *data, int len)
{
	unsigned short temp; 
	unsigned long sum=0;
	int i;

	for( i = 0; i <len; i+=2 ) {
		memcpy(&temp,&data[i],2);
		sum += (unsigned long)temp;
	}

	while (sum >> 16 ) {
		sum = (sum & 0xffffUL) + (sum >> 16);
	}

	temp = (unsigned short)sum;
	temp = ~temp;

	if( temp == 0 ) 
		temp = 0xffff;

	return temp;	
}


/*============================================================================
 * Get ethernet-style interface statistics.
 * Return a pointer to struct enet_statistics.
 */
static struct net_device_stats* if_stats(struct net_device* dev)
{
	sdla_t *my_card;
	chdlc_private_area_t* chdlc_priv_area;

	/* Shutdown bug fix. In del_if() we kill
         * dev->priv pointer. This function, gets
         * called after del_if(), thus check
         * if pointer has been deleted */
	if ((chdlc_priv_area=dev->priv) == NULL)
		return NULL;

	my_card = chdlc_priv_area->card;
	return &my_card->wandev.stats; 
}


/****** Cisco HDLC Firmware Interface Functions *******************************/

/*============================================================================
 * Read firmware code version.
 *	Put code version as ASCII string in str. 
 */
static int chdlc_read_version (sdla_t* card, char* str)
{
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;
	int len;
	char err;
	mb->buffer_length = 0;
	mb->command = READ_CHDLC_CODE_VERSION;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;

	if(err != COMMAND_OK) {
		chdlc_error(card,err,mb);
	}
	else if (str) {  /* is not null */
		len = mb->buffer_length;
		memcpy(str, mb->data, len);
		str[len] = '\0';
	}
	return (err);
}

/*-----------------------------------------------------------------------------
 *  Configure CHDLC firmware.
 */
static int chdlc_configure (sdla_t* card, void* data)
{
	int err;
	CHDLC_MAILBOX_STRUCT *mailbox = card->mbox;
	int data_length = sizeof(CHDLC_CONFIGURATION_STRUCT);
	
	mailbox->buffer_length = data_length;  
	memcpy(mailbox->data, data, data_length);
	mailbox->command = SET_CHDLC_CONFIGURATION;
	err = sdla_exec(mailbox) ? mailbox->return_code : CMD_TIMEOUT;
	
	if (err != COMMAND_OK) chdlc_error (card, err, mailbox);
                           
	return err;
}


/*============================================================================
 * Set interrupt mode -- HDLC Version.
 */

static int chdlc_set_intr_mode (sdla_t* card, unsigned mode)
{
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;
	CHDLC_INT_TRIGGERS_STRUCT* int_data =
		 (CHDLC_INT_TRIGGERS_STRUCT *)mb->data;
	int err;

	int_data->CHDLC_interrupt_triggers 	= mode;
	int_data->IRQ				= card->hw.irq;
	int_data->interrupt_timer               = 1;
   
	mb->buffer_length = sizeof(CHDLC_INT_TRIGGERS_STRUCT);
	mb->command = SET_CHDLC_INTERRUPT_TRIGGERS;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	if (err != COMMAND_OK)
		chdlc_error (card, err, mb);
	return err;
}


/*============================================================================
 * Enable communications.
 */

static int chdlc_comm_enable (sdla_t* card)
{
	int err;
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;

	mb->buffer_length = 0;
	mb->command = ENABLE_CHDLC_COMMUNICATIONS;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	if (err != COMMAND_OK)
		chdlc_error(card, err, mb);
	else
		card->u.c.comm_enabled=1;

	return err;
}

/*============================================================================
 * Disable communications and Drop the Modem lines (DCD and RTS).
 */
static int chdlc_comm_disable (sdla_t* card)
{
	int err;
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;

	mb->buffer_length = 0;
	mb->command = DISABLE_CHDLC_COMMUNICATIONS;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	if (err != COMMAND_OK)
		chdlc_error(card,err,mb);

	return err;
}

/*============================================================================
 * Read communication error statistics.
 */
static int chdlc_read_comm_err_stats (sdla_t* card)
{
        int err;
        CHDLC_MAILBOX_STRUCT* mb = card->mbox;

        mb->buffer_length = 0;
        mb->command = READ_COMMS_ERROR_STATS;
        err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
        if (err != COMMAND_OK)
                chdlc_error(card,err,mb);
        return err;
}


/*============================================================================
 * Read CHDLC operational statistics.
 */
static int chdlc_read_op_stats (sdla_t* card)
{
        int err;
        CHDLC_MAILBOX_STRUCT* mb = card->mbox;

        mb->buffer_length = 0;
        mb->command = READ_CHDLC_OPERATIONAL_STATS;
        err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
        if (err != COMMAND_OK)
                chdlc_error(card,err,mb);
        return err;
}


/*============================================================================
 * Update communications error and general packet statistics.
 */
static int update_comms_stats(sdla_t* card,
	chdlc_private_area_t* chdlc_priv_area)
{
        CHDLC_MAILBOX_STRUCT* mb = card->mbox;
  	COMMS_ERROR_STATS_STRUCT* err_stats;
        CHDLC_OPERATIONAL_STATS_STRUCT *op_stats;

	/* on the first timer interrupt, read the comms error statistics */
	if(chdlc_priv_area->update_comms_stats == 2) {
		if(chdlc_read_comm_err_stats(card))
			return 1;
		err_stats = (COMMS_ERROR_STATS_STRUCT *)mb->data;
		card->wandev.stats.rx_over_errors = 
				err_stats->Rx_overrun_err_count;
		card->wandev.stats.rx_crc_errors = 
				err_stats->CRC_err_count;
		card->wandev.stats.rx_frame_errors = 
				err_stats->Rx_abort_count;
		card->wandev.stats.rx_fifo_errors = 
				err_stats->Rx_dis_pri_bfrs_full_count; 
		card->wandev.stats.rx_missed_errors =
				card->wandev.stats.rx_fifo_errors;
		card->wandev.stats.tx_aborted_errors =
				err_stats->sec_Tx_abort_count;
	}

        /* on the second timer interrupt, read the operational statistics */
	else {
        	if(chdlc_read_op_stats(card))
                	return 1;
		op_stats = (CHDLC_OPERATIONAL_STATS_STRUCT *)mb->data;
		card->wandev.stats.rx_length_errors =
			(op_stats->Rx_Data_discard_short_count +
			op_stats->Rx_Data_discard_long_count);
	}

	return 0;
}

/*============================================================================
 * Send packet.
 *	Return:	0 - o.k.
 *		1 - no transmit buffers available
 */
static int chdlc_send (sdla_t* card, void* data, unsigned len)
{
	CHDLC_DATA_TX_STATUS_EL_STRUCT *txbuf = card->u.c.txbuf;

	if (txbuf->opp_flag)
		return 1;
	
	sdla_poke(&card->hw, txbuf->ptr_data_bfr, data, len);

	txbuf->frame_length = len;
	txbuf->opp_flag = 1;		/* start transmission */
	
	/* Update transmit buffer control fields */
	card->u.c.txbuf = ++txbuf;

	if ((void*)txbuf > card->u.c.txbuf_last)
		card->u.c.txbuf = card->u.c.txbuf_base;

	return 0;
}

/****** Firmware Error Handler **********************************************/

/*============================================================================
 * Firmware error handler.
 *	This routine is called whenever firmware command returns non-zero
 *	return code.
 *
 * Return zero if previous command has to be cancelled.
 */
static int chdlc_error (sdla_t *card, int err, CHDLC_MAILBOX_STRUCT *mb)
{
	unsigned cmd = mb->command;

	switch (err) {

	case CMD_TIMEOUT:
		printk(KERN_ERR "%s: command 0x%02X timed out!\n",
			card->devname, cmd);
		break;

	case S514_BOTH_PORTS_SAME_CLK_MODE:
		if(cmd == SET_CHDLC_CONFIGURATION) {
			printk(KERN_INFO
			 "%s: Configure both ports for the same clock source\n",
				card->devname);
			break;
		}

	default:
		printk(KERN_INFO "%s: command 0x%02X returned 0x%02X!\n",
			card->devname, cmd, err);
	}

	return 0;
}

/****** Interrupt Handlers **************************************************/

/*============================================================================
 * Cisco HDLC interrupt service routine.
 */
STATIC void wsppp_isr (sdla_t* card)
{
	struct net_device* dev;
	SHARED_MEMORY_INFO_STRUCT* flags = NULL;
	int i;
	sdla_t *my_card;


	/* Check for which port the interrupt has been generated
	 * Since Secondary Port is piggybacking on the Primary
         * the check must be done here. 
	 */

	flags = card->u.c.flags;
	if (!flags->interrupt_info_struct.interrupt_type){
		/* Check for a second port (piggybacking) */
		if((my_card = card->next)){
			flags = my_card->u.c.flags;
			if (flags->interrupt_info_struct.interrupt_type){
				card = my_card;
				card->isr(card);
				return;
			}
		}
	}

	dev = card->wandev.dev;
	card->in_isr = 1;
	flags = card->u.c.flags;
		
	/* If we get an interrupt with no network device, stop the interrupts
	 * and issue an error */
	if ((!dev || !dev->priv) && flags->interrupt_info_struct.interrupt_type != 
	    	COMMAND_COMPLETE_APP_INT_PEND){
		goto isr_done;
	}

	
	/* if critical due to peripheral operations
	 * ie. update() or getstats() then reset the interrupt and
	 * wait for the board to retrigger.
	 */
	if(test_bit(PERI_CRIT, (void*)&card->wandev.critical)) {
		flags->interrupt_info_struct.
					interrupt_type = 0;
		goto isr_done;
	}


	/* On a 508 Card, if critical due to if_send 
         * Major Error !!!
	 */
	if(card->hw.type != SDLA_S514) {
		if(test_bit(0, (void*)&card->wandev.critical)) {
			printk(KERN_INFO "%s: Critical while in ISR: %lx\n",
				card->devname, card->wandev.critical);
			goto isr_done;
		}
	}

	switch(flags->interrupt_info_struct.interrupt_type) {

		case RX_APP_INT_PEND:	/* 0x01: receive interrupt */
			rx_intr(card);
			break;

		case TX_APP_INT_PEND:	/* 0x02: transmit interrupt */
			flags->interrupt_info_struct.interrupt_permission &=
				 ~APP_INT_ON_TX_FRAME;

			netif_wake_queue(dev);
			break;

		case COMMAND_COMPLETE_APP_INT_PEND:/* 0x04: cmd cplt */
			++ Intr_test_counter;
			break;

		case CHDLC_EXCEP_COND_APP_INT_PEND:	/* 0x20 */
			process_chdlc_exception(card);
			break;

		case GLOBAL_EXCEP_COND_APP_INT_PEND:
			process_global_exception(card);
			break;

		case TIMER_APP_INT_PEND:
			timer_intr(card);
			break;

		default:
			printk(KERN_INFO "%s: spurious interrupt 0x%02X!\n", 
				card->devname,
				flags->interrupt_info_struct.interrupt_type);
			printk(KERN_INFO "Code name: ");
			for(i = 0; i < 4; i ++)
				printk(KERN_INFO "%c",
					flags->global_info_struct.codename[i]); 
			printk(KERN_INFO "\nCode version: ");
			for(i = 0; i < 4; i ++)
				printk(KERN_INFO "%c", 
					flags->global_info_struct.codeversion[i]); 
			printk(KERN_INFO "\n");	
			break;
	}

isr_done:
	card->in_isr = 0;
	flags->interrupt_info_struct.interrupt_type = 0;
}

/*============================================================================
 * Receive interrupt handler.
 */
static void rx_intr (sdla_t* card)
{
	struct net_device *dev;
	chdlc_private_area_t *chdlc_priv_area;
	SHARED_MEMORY_INFO_STRUCT *flags = card->u.c.flags;
	CHDLC_DATA_RX_STATUS_EL_STRUCT *rxbuf = card->u.c.rxmb;
	struct sk_buff *skb;
	unsigned len;
	unsigned addr = rxbuf->ptr_data_bfr;
	void *buf;
	int i,udp_type;
	
	if (rxbuf->opp_flag != 0x01) {
		printk(KERN_INFO 
			"%s: corrupted Rx buffer @ 0x%X, flag = 0x%02X!\n", 
			card->devname, (unsigned)rxbuf, rxbuf->opp_flag);
                printk(KERN_INFO "Code name: ");
                for(i = 0; i < 4; i ++)
                        printk(KERN_INFO "%c",
                                flags->global_info_struct.codename[i]);
                printk(KERN_INFO "\nCode version: ");
                for(i = 0; i < 4; i ++)
                        printk(KERN_INFO "%c",
                                flags->global_info_struct.codeversion[i]);
                printk(KERN_INFO "\n");


		/* Bug Fix: Mar 6 2000
                 * If we get a corrupted mailbox, it measn that driver 
                 * is out of sync with the firmware. There is no recovery.
                 * If we don't turn off all interrupts for this card
                 * the machine will crash. 
                 */
		printk(KERN_INFO "%s: Critical router failure ...!!!\n", card->devname);
		printk(KERN_INFO "Please contact Sangoma Technologies !\n");
		chdlc_set_intr_mode(card,0);	
		return;
	}

	dev = card->wandev.dev;

	if (!dev){ 
		goto rx_exit;
	}
	
	if (!netif_running(dev)){
		goto rx_exit;
	}

	chdlc_priv_area = dev->priv;

	if (rxbuf->error_flag){	
		goto rx_exit;
	}
	/* Take off two CRC bytes */

	if (rxbuf->frame_length < 7 || rxbuf->frame_length > 1506 ){
		goto rx_exit;
	}	

	len = rxbuf->frame_length - CRC_LENGTH;

	/* Allocate socket buffer */
	skb = dev_alloc_skb(len);

	if (skb == NULL) {
		if (net_ratelimit()){
			printk(KERN_INFO "%s: no socket buffers available!\n",
						card->devname);
		}
		++card->wandev.stats.rx_dropped;
		goto rx_exit;
	}

	/* Copy data to the socket buffer */
	if((addr + len) > card->u.c.rx_top + 1) {
		unsigned tmp = card->u.c.rx_top - addr + 1;
		buf = skb_put(skb, tmp);
		sdla_peek(&card->hw, addr, buf, tmp);
		addr = card->u.c.rx_base;
		len -= tmp;
	}
		
	buf = skb_put(skb, len);
	sdla_peek(&card->hw, addr, buf, len);

	skb->protocol = htons(ETH_P_WAN_PPP);

	card->wandev.stats.rx_packets ++;
	card->wandev.stats.rx_bytes += skb->len;
	udp_type = udp_pkt_type( skb, card );

	if(udp_type == UDP_CPIPE_TYPE) {
		if(store_udp_mgmt_pkt(UDP_PKT_FRM_NETWORK,
   				      card, skb, dev, chdlc_priv_area)) {
     		        flags->interrupt_info_struct.
						interrupt_permission |= 
							APP_INT_ON_TIMER; 
		}
	}else{
               	/* Pass it up the protocol stack */
                skb->dev = dev;
                skb->mac.raw  = skb->data;
                netif_rx(skb);
                dev->last_rx = jiffies;
	}

rx_exit:
	/* Release buffer element and calculate a pointer to the next one */
	rxbuf->opp_flag = 0x00;
	card->u.c.rxmb = ++ rxbuf;
	if((void*)rxbuf > card->u.c.rxbuf_last){
		card->u.c.rxmb = card->u.c.rxbuf_base;
	}
}

/*============================================================================
 * Timer interrupt handler.
 * The timer interrupt is used for two purposes:
 *    1) Processing udp calls from 'cpipemon'.
 *    2) Reading board-level statistics for updating the proc file system.
 */
void timer_intr(sdla_t *card)
{
        struct net_device* dev;
        chdlc_private_area_t* chdlc_priv_area = NULL;
        SHARED_MEMORY_INFO_STRUCT* flags = NULL;

        dev = card->wandev.dev; 
        chdlc_priv_area = dev->priv;

	if (chdlc_priv_area->timer_int_enabled & TMR_INT_ENABLED_CONFIG) {
		if (!config_chdlc(card)){
			chdlc_priv_area->timer_int_enabled &= ~TMR_INT_ENABLED_CONFIG;
		}
	}
	
	/* process a udp call if pending */
       	if(chdlc_priv_area->timer_int_enabled & TMR_INT_ENABLED_UDP) {
               	process_udp_mgmt_pkt(card, dev,
                       chdlc_priv_area);
		chdlc_priv_area->timer_int_enabled &= ~TMR_INT_ENABLED_UDP;
        }
	

	/* read the communications statistics if required */
	if(chdlc_priv_area->timer_int_enabled & TMR_INT_ENABLED_UPDATE) {
		update_comms_stats(card, chdlc_priv_area);
                if(!(-- chdlc_priv_area->update_comms_stats)) {
			chdlc_priv_area->timer_int_enabled &= 
				~TMR_INT_ENABLED_UPDATE;
		}
        }

	/* only disable the timer interrupt if there are no udp or statistic */
	/* updates pending */
        if(!chdlc_priv_area->timer_int_enabled) {
                flags = card->u.c.flags;
                flags->interrupt_info_struct.interrupt_permission &=
                        ~APP_INT_ON_TIMER;
        }
}

/*------------------------------------------------------------------------------
  Miscellaneous Functions
	- set_chdlc_config() used to set configuration options on the board
------------------------------------------------------------------------------*/

static int set_chdlc_config(sdla_t* card)
{

	CHDLC_CONFIGURATION_STRUCT cfg;

	memset(&cfg, 0, sizeof(CHDLC_CONFIGURATION_STRUCT));

	if(card->wandev.clocking)
		cfg.baud_rate = card->wandev.bps;

	cfg.line_config_options = (card->wandev.interface == WANOPT_RS232) ?
		INTERFACE_LEVEL_RS232 : INTERFACE_LEVEL_V35;

	cfg.modem_config_options	= 0;
	//API OPTIONS
	cfg.CHDLC_API_options		= DISCARD_RX_ERROR_FRAMES;
	cfg.modem_status_timer		= 100;
	cfg.CHDLC_protocol_options	= HDLC_STREAMING_MODE;
	cfg.percent_data_buffer_for_Tx  = 50;
	cfg.CHDLC_statistics_options	= (CHDLC_TX_DATA_BYTE_COUNT_STAT |
		CHDLC_RX_DATA_BYTE_COUNT_STAT);
	cfg.max_CHDLC_data_field_length	= card->wandev.mtu;

	cfg.transmit_keepalive_timer	= 0;
	cfg.receive_keepalive_timer	= 0;
	cfg.keepalive_error_tolerance	= 0;
	cfg.SLARP_request_timer		= 0;

	cfg.IP_address		= 0;
	cfg.IP_netmask		= 0;
	
	return chdlc_configure(card, &cfg);
}

/*============================================================================
 * Process global exception condition
 */
static int process_global_exception(sdla_t *card)
{
	CHDLC_MAILBOX_STRUCT* mbox = card->mbox;
	int err;

	mbox->buffer_length = 0;
	mbox->command = READ_GLOBAL_EXCEPTION_CONDITION;
	err = sdla_exec(mbox) ? mbox->return_code : CMD_TIMEOUT;

	if(err != CMD_TIMEOUT ){
	
		switch(mbox->return_code) {
         
	      	case EXCEP_MODEM_STATUS_CHANGE:

			printk(KERN_INFO "%s: Modem status change\n",
				card->devname);

			switch(mbox->data[0] & (DCD_HIGH | CTS_HIGH)) {
				case (DCD_HIGH):
					printk(KERN_INFO "%s: DCD high, CTS low\n",card->devname);
					break;
				case (CTS_HIGH):
                                        printk(KERN_INFO "%s: DCD low, CTS high\n",card->devname);
                                        break;
                                case ((DCD_HIGH | CTS_HIGH)):
                                        printk(KERN_INFO "%s: DCD high, CTS high\n",card->devname);
                                        break;
				default:
                                        printk(KERN_INFO "%s: DCD low, CTS low\n",card->devname);
                                        break;
			}

			if (!(mbox->data[0] & DCD_HIGH) || !(mbox->data[0] & DCD_HIGH)){
				//printk(KERN_INFO "Sending TERM Request Manually !\n");
				send_ppp_term_request(card->wandev.dev);
			}	
			break;

                case EXCEP_TRC_DISABLED:
                        printk(KERN_INFO "%s: Line trace disabled\n",
				card->devname);
                        break;

		case EXCEP_IRQ_TIMEOUT:
			printk(KERN_INFO "%s: IRQ timeout occurred\n",
				card->devname); 
			break;

                default:
                        printk(KERN_INFO "%s: Global exception %x\n",
				card->devname, mbox->return_code);
                        break;
                }
	}
	return 0;
}


/*============================================================================
 * Process chdlc exception condition
 */
static int process_chdlc_exception(sdla_t *card)
{
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;
	int err;

	mb->buffer_length = 0;
	mb->command = READ_CHDLC_EXCEPTION_CONDITION;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	if(err != CMD_TIMEOUT) {
	
		switch (err) {

		case EXCEP_LINK_ACTIVE:
			port_set_state(card, WAN_CONNECTED);
			break;

		case EXCEP_LINK_INACTIVE_MODEM:
			port_set_state(card, WAN_DISCONNECTED);
			break;

		case EXCEP_LOOPBACK_CONDITION:
			printk(KERN_INFO "%s: Loopback Condition Detected.\n",
						card->devname);
			break;

		case NO_CHDLC_EXCEP_COND_TO_REPORT:
			printk(KERN_INFO "%s: No exceptions reported.\n",
						card->devname);
			break;
		default:
			printk(KERN_INFO "%s: Exception Condition %x!\n",
					card->devname,err);
			break;
		}

	}
	return 0;
}


/*=============================================================================
 * Store a UDP management packet for later processing.
 */

static int store_udp_mgmt_pkt(char udp_pkt_src, sdla_t* card,
			      struct sk_buff *skb, struct net_device* dev,
			      chdlc_private_area_t* chdlc_priv_area )
{
	int udp_pkt_stored = 0;

	if(!chdlc_priv_area->udp_pkt_lgth &&
	  (skb->len <= MAX_LGTH_UDP_MGNT_PKT)) {
        	chdlc_priv_area->udp_pkt_lgth = skb->len;
		chdlc_priv_area->udp_pkt_src = udp_pkt_src;
       		memcpy(chdlc_priv_area->udp_pkt_data, skb->data, skb->len);
		chdlc_priv_area->timer_int_enabled = TMR_INT_ENABLED_UDP;
		udp_pkt_stored = 1;
	}

	if(udp_pkt_src == UDP_PKT_FRM_STACK)
		dev_kfree_skb_any(skb);
	else
                dev_kfree_skb_any(skb);
	
	return(udp_pkt_stored);
}


/*=============================================================================
 * Process UDP management packet.
 */

static int process_udp_mgmt_pkt(sdla_t* card, struct net_device* dev,
				chdlc_private_area_t* chdlc_priv_area ) 
{
	unsigned char *buf;
	unsigned int frames, len;
	struct sk_buff *new_skb;
	unsigned short buffer_length, real_len;
	unsigned long data_ptr;
	unsigned data_length;
	int udp_mgmt_req_valid = 1;
	CHDLC_MAILBOX_STRUCT *mb = card->mbox;
	SHARED_MEMORY_INFO_STRUCT *flags = card->u.c.flags;
	chdlc_udp_pkt_t *chdlc_udp_pkt;
	struct timeval tv;
	int err;
	char ut_char;

	chdlc_udp_pkt = (chdlc_udp_pkt_t *) chdlc_priv_area->udp_pkt_data;

	if(chdlc_priv_area->udp_pkt_src == UDP_PKT_FRM_NETWORK) {

		switch(chdlc_udp_pkt->cblock.command) {
			case READ_GLOBAL_STATISTICS:
			case READ_MODEM_STATUS:  
			case READ_CHDLC_LINK_STATUS:
			case CPIPE_ROUTER_UP_TIME:
			case READ_COMMS_ERROR_STATS:
			case READ_CHDLC_OPERATIONAL_STATS:

			/* These two commands are executed for
			 * each request */
			case READ_CHDLC_CONFIGURATION:
			case READ_CHDLC_CODE_VERSION:
				udp_mgmt_req_valid = 1;
				break;
			default:
				udp_mgmt_req_valid = 0;
				break;
		} 
	}
	
  	if(!udp_mgmt_req_valid) {

		/* set length to 0 */
		chdlc_udp_pkt->cblock.buffer_length = 0;

    		/* set return code */
		chdlc_udp_pkt->cblock.return_code = 0xCD;

		if (net_ratelimit()){	
			printk(KERN_INFO 
			"%s: Warning, Illegal UDP command attempted from network: %x\n",
			card->devname,chdlc_udp_pkt->cblock.command);
		}

   	} else {
	   	unsigned long trace_status_cfg_addr = 0;
		TRACE_STATUS_EL_CFG_STRUCT trace_cfg_struct;
		TRACE_STATUS_ELEMENT_STRUCT trace_element_struct;

		switch(chdlc_udp_pkt->cblock.command) {

		case CPIPE_ENABLE_TRACING:
		     if (!chdlc_priv_area->TracingEnabled) {

			/* OPERATE_DATALINE_MONITOR */

			mb->buffer_length = sizeof(LINE_TRACE_CONFIG_STRUCT);
			mb->command = SET_TRACE_CONFIGURATION;

    			((LINE_TRACE_CONFIG_STRUCT *)mb->data)->
				trace_config = TRACE_ACTIVE;
			/* Trace delay mode is not used because it slows
			   down transfer and results in a standoff situation
			   when there is a lot of data */

			/* Configure the Trace based on user inputs */
			((LINE_TRACE_CONFIG_STRUCT *)mb->data)->trace_config |= 
					chdlc_udp_pkt->data[0];

			((LINE_TRACE_CONFIG_STRUCT *)mb->data)->
			   trace_deactivation_timer = 4000;


			err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
			if (err != COMMAND_OK) {
				chdlc_error(card,err,mb);
				card->TracingEnabled = 0;
				chdlc_udp_pkt->cblock.return_code = err;
				mb->buffer_length = 0;
				break;
	    		} 

			/* Get the base address of the trace element list */
			mb->buffer_length = 0;
			mb->command = READ_TRACE_CONFIGURATION;
			err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;

			if (err != COMMAND_OK) {
				chdlc_error(card,err,mb);
				chdlc_priv_area->TracingEnabled = 0;
				chdlc_udp_pkt->cblock.return_code = err;
				mb->buffer_length = 0;
				break;
	    		} 	

	   		trace_status_cfg_addr =((LINE_TRACE_CONFIG_STRUCT *)
				mb->data) -> ptr_trace_stat_el_cfg_struct;

			sdla_peek(&card->hw, trace_status_cfg_addr,
				 &trace_cfg_struct, sizeof(trace_cfg_struct));
		    
			chdlc_priv_area->start_trace_addr = trace_cfg_struct.
				base_addr_trace_status_elements;

			chdlc_priv_area->number_trace_elements = 
					trace_cfg_struct.number_trace_status_elements;

			chdlc_priv_area->end_trace_addr = (unsigned long)
					((TRACE_STATUS_ELEMENT_STRUCT *)
					 chdlc_priv_area->start_trace_addr + 
					 (chdlc_priv_area->number_trace_elements - 1));

			chdlc_priv_area->base_addr_trace_buffer = 
					trace_cfg_struct.base_addr_trace_buffer;

			chdlc_priv_area->end_addr_trace_buffer = 
					trace_cfg_struct.end_addr_trace_buffer;

		    	chdlc_priv_area->curr_trace_addr = 
					trace_cfg_struct.next_trace_element_to_use;

	    		chdlc_priv_area->available_buffer_space = 2000 - 
								  sizeof(ip_pkt_t) -
								  sizeof(udp_pkt_t) -
							      	  sizeof(wp_mgmt_t) -
								  sizeof(cblock_t) -
							          sizeof(trace_info_t);	
	       	     }
		     chdlc_udp_pkt->cblock.return_code = COMMAND_OK;
		     mb->buffer_length = 0;
	       	     chdlc_priv_area->TracingEnabled = 1;
	       	     break;
	   

		case CPIPE_DISABLE_TRACING:
		     if (chdlc_priv_area->TracingEnabled) {

			/* OPERATE_DATALINE_MONITOR */
			mb->buffer_length = sizeof(LINE_TRACE_CONFIG_STRUCT);
			mb->command = SET_TRACE_CONFIGURATION;
    			((LINE_TRACE_CONFIG_STRUCT *)mb->data)->
				trace_config = TRACE_INACTIVE;
			err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
		     }		

		     chdlc_priv_area->TracingEnabled = 0;
		     chdlc_udp_pkt->cblock.return_code = COMMAND_OK;
		     mb->buffer_length = 0;
		     break;
	   

		case CPIPE_GET_TRACE_INFO:

		     if (!chdlc_priv_area->TracingEnabled) {
			chdlc_udp_pkt->cblock.return_code = 1;
			mb->buffer_length = 0;
			break;
		     }

  		     chdlc_udp_pkt->trace_info.ismoredata = 0x00;
		     buffer_length = 0;	/* offset of packet already occupied */

		     for (frames=0; frames < chdlc_priv_area->number_trace_elements; frames++){

			trace_pkt_t *trace_pkt = (trace_pkt_t *)
				&chdlc_udp_pkt->data[buffer_length];

			sdla_peek(&card->hw, chdlc_priv_area->curr_trace_addr,
			   	  (unsigned char *)&trace_element_struct,
			   	  sizeof(TRACE_STATUS_ELEMENT_STRUCT));

     			if (trace_element_struct.opp_flag == 0x00) {
			 	break;
			}

			/* get pointer to real data */
			data_ptr = trace_element_struct.ptr_data_bfr;

			/* See if there is actual data on the trace buffer */
			if (data_ptr){
				data_length = trace_element_struct.trace_length;
			}else{
				data_length = 0;
				chdlc_udp_pkt->trace_info.ismoredata = 0x01;
			}
	
   			if( (chdlc_priv_area->available_buffer_space - buffer_length)
				< ( sizeof(trace_pkt_t) + data_length) ) {

                            /* indicate there are more frames on board & exit */
				chdlc_udp_pkt->trace_info.ismoredata = 0x01;
                               	break;
                         }

			trace_pkt->status = trace_element_struct.trace_type;

			trace_pkt->time_stamp =
				trace_element_struct.trace_time_stamp;

			trace_pkt->real_length =
				trace_element_struct.trace_length;

			/* see if we can fit the frame into the user buffer */
			real_len = trace_pkt->real_length;

			if (data_ptr == 0) {
			     	trace_pkt->data_avail = 0x00;
			} else {
				unsigned tmp = 0;

				/* get the data from circular buffer
				    must check for end of buffer */
			        trace_pkt->data_avail = 0x01;

				if ((data_ptr + real_len) >
					     chdlc_priv_area->end_addr_trace_buffer + 1){

				    	tmp = chdlc_priv_area->end_addr_trace_buffer - data_ptr + 1;
				    	sdla_peek(&card->hw, data_ptr,
					       	  trace_pkt->data,tmp);
				    	data_ptr = chdlc_priv_area->base_addr_trace_buffer;
				}
	
		        	sdla_peek(&card->hw, data_ptr,
					  &trace_pkt->data[tmp], real_len - tmp);
			}	

			/* zero the opp flag to show we got the frame */
			ut_char = 0x00;
			sdla_poke(&card->hw, chdlc_priv_area->curr_trace_addr, &ut_char, 1);

       			/* now move onto the next frame */
       			chdlc_priv_area->curr_trace_addr += sizeof(TRACE_STATUS_ELEMENT_STRUCT);

       			/* check if we went over the last address */
			if ( chdlc_priv_area->curr_trace_addr > chdlc_priv_area->end_trace_addr ) {
				chdlc_priv_area->curr_trace_addr = chdlc_priv_area->start_trace_addr;
       			}

            		if(trace_pkt->data_avail == 0x01) {
				buffer_length += real_len - 1;
			}
	 
	       	    	/* for the header */
	            	buffer_length += sizeof(trace_pkt_t);

		     }  /* For Loop */

		     if (frames == chdlc_priv_area->number_trace_elements){
			chdlc_udp_pkt->trace_info.ismoredata = 0x01;
	             }
 		     chdlc_udp_pkt->trace_info.num_frames = frames;
		 
    		     mb->buffer_length = buffer_length;
		     chdlc_udp_pkt->cblock.buffer_length = buffer_length; 
		 
		     chdlc_udp_pkt->cblock.return_code = COMMAND_OK; 
		     
		     break;


		case CPIPE_FT1_READ_STATUS:
			((unsigned char *)chdlc_udp_pkt->data )[0] =
				flags->FT1_info_struct.parallel_port_A_input;

			((unsigned char *)chdlc_udp_pkt->data )[1] =
				flags->FT1_info_struct.parallel_port_B_input;
				 
			chdlc_udp_pkt->cblock.return_code = COMMAND_OK;
			mb->buffer_length = 2;
			break;
		
		case CPIPE_ROUTER_UP_TIME:
			do_gettimeofday( &tv );
			chdlc_priv_area->router_up_time = tv.tv_sec - 
					chdlc_priv_area->router_start_time;
			*(unsigned long *)&chdlc_udp_pkt->data = 
					chdlc_priv_area->router_up_time;	
			mb->buffer_length = sizeof(unsigned long);
			break;

   		case FT1_MONITOR_STATUS_CTRL:
			/* Enable FT1 MONITOR STATUS */
	        	if ((chdlc_udp_pkt->data[0] & ENABLE_READ_FT1_STATUS) ||  
				(chdlc_udp_pkt->data[0] & ENABLE_READ_FT1_OP_STATS)) {
			
			     	if( rCount++ != 0 ) {
					chdlc_udp_pkt->cblock.
					return_code = COMMAND_OK;
					mb->buffer_length = 1;
		  			break;
		    	     	}
	      		}

	      		/* Disable FT1 MONITOR STATUS */
	      		if( chdlc_udp_pkt->data[0] == 0) {

	      	   	     	if( --rCount != 0) {
		  			chdlc_udp_pkt->cblock.
					return_code = COMMAND_OK;
					mb->buffer_length = 1;
		  			break;
	   	    	     	} 
	      		} 	
	
		default:
			/* it's a board command */
			mb->command = chdlc_udp_pkt->cblock.command;
			mb->buffer_length = chdlc_udp_pkt->cblock.buffer_length;
			if (mb->buffer_length) {
				memcpy(&mb->data, (unsigned char *) chdlc_udp_pkt->
							data, mb->buffer_length);
	      		} 
			/* run the command on the board */
			err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
			if (err != COMMAND_OK) {
				break;
			}

			/* copy the result back to our buffer */
	         	memcpy(&chdlc_udp_pkt->cblock, mb, sizeof(cblock_t)); 
			
			if (mb->buffer_length) {
	         		memcpy(&chdlc_udp_pkt->data, &mb->data, 
								mb->buffer_length); 
	      		}

		} /* end of switch */
     	} /* end of else */

     	/* Fill UDP TTL */
	chdlc_udp_pkt->ip_pkt.ttl = card->wandev.ttl; 

     	len = reply_udp(chdlc_priv_area->udp_pkt_data, mb->buffer_length);
	
     	if(chdlc_priv_area->udp_pkt_src == UDP_PKT_FRM_NETWORK) {
		if(!chdlc_send(card, chdlc_priv_area->udp_pkt_data, len)) {
			++ card->wandev.stats.tx_packets;
			card->wandev.stats.tx_bytes += len;
		}
	} else {	
	
		/* Pass it up the stack
    		   Allocate socket buffer */
		if ((new_skb = dev_alloc_skb(len)) != NULL) {
			/* copy data into new_skb */

 	    		buf = skb_put(new_skb, len);
  	    		memcpy(buf, chdlc_priv_area->udp_pkt_data, len);

            		/* Decapsulate pkt and pass it up the protocol stack */
	    		new_skb->protocol = htons(ETH_P_IP);
            		new_skb->dev = dev;
	    		new_skb->mac.raw  = new_skb->data;
	
			netif_rx(new_skb);
			dev->last_rx = jiffies;
		} else {
	    	
			printk(KERN_INFO "%s: no socket buffers available!\n",
					card->devname);
  		}
    	}
 
	chdlc_priv_area->udp_pkt_lgth = 0;
 	
	return 0;
}

/*============================================================================
 * Initialize Receive and Transmit Buffers.
 */

static void init_chdlc_tx_rx_buff(sdla_t* card, struct net_device *dev)
{
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;
	CHDLC_TX_STATUS_EL_CFG_STRUCT *tx_config;
	CHDLC_RX_STATUS_EL_CFG_STRUCT *rx_config;
	char err;
	
	mb->buffer_length = 0;
	mb->command = READ_CHDLC_CONFIGURATION;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;

	if(err != COMMAND_OK) {
		chdlc_error(card,err,mb);
		return;
	}

	if(card->hw.type == SDLA_S514) {
		tx_config = (CHDLC_TX_STATUS_EL_CFG_STRUCT *)(card->hw.dpmbase +
                (((CHDLC_CONFIGURATION_STRUCT *)mb->data)->
                            ptr_CHDLC_Tx_stat_el_cfg_struct));
        	rx_config = (CHDLC_RX_STATUS_EL_CFG_STRUCT *)(card->hw.dpmbase +
                (((CHDLC_CONFIGURATION_STRUCT *)mb->data)->
                            ptr_CHDLC_Rx_stat_el_cfg_struct));

       		/* Setup Head and Tails for buffers */
        	card->u.c.txbuf_base = (void *)(card->hw.dpmbase +
                tx_config->base_addr_Tx_status_elements);
        	card->u.c.txbuf_last = 
		(CHDLC_DATA_TX_STATUS_EL_STRUCT *)  
                card->u.c.txbuf_base +
		(tx_config->number_Tx_status_elements - 1);

        	card->u.c.rxbuf_base = (void *)(card->hw.dpmbase +
                rx_config->base_addr_Rx_status_elements);
        	card->u.c.rxbuf_last =
		(CHDLC_DATA_RX_STATUS_EL_STRUCT *)
                card->u.c.rxbuf_base +
		(rx_config->number_Rx_status_elements - 1);

 		/* Set up next pointer to be used */
        	card->u.c.txbuf = (void *)(card->hw.dpmbase +
                tx_config->next_Tx_status_element_to_use);
        	card->u.c.rxmb = (void *)(card->hw.dpmbase +
                rx_config->next_Rx_status_element_to_use);
	}
        else {
                tx_config = (CHDLC_TX_STATUS_EL_CFG_STRUCT *)(card->hw.dpmbase +
			(((CHDLC_CONFIGURATION_STRUCT *)mb->data)->
			ptr_CHDLC_Tx_stat_el_cfg_struct % SDLA_WINDOWSIZE));

                rx_config = (CHDLC_RX_STATUS_EL_CFG_STRUCT *)(card->hw.dpmbase +
			(((CHDLC_CONFIGURATION_STRUCT *)mb->data)->
			ptr_CHDLC_Rx_stat_el_cfg_struct % SDLA_WINDOWSIZE));

                /* Setup Head and Tails for buffers */
                card->u.c.txbuf_base = (void *)(card->hw.dpmbase +
		(tx_config->base_addr_Tx_status_elements % SDLA_WINDOWSIZE));
                card->u.c.txbuf_last =
		(CHDLC_DATA_TX_STATUS_EL_STRUCT *)card->u.c.txbuf_base
		+ (tx_config->number_Tx_status_elements - 1);
                card->u.c.rxbuf_base = (void *)(card->hw.dpmbase +
		(rx_config->base_addr_Rx_status_elements % SDLA_WINDOWSIZE));
                card->u.c.rxbuf_last = 
		(CHDLC_DATA_RX_STATUS_EL_STRUCT *)card->u.c.rxbuf_base
		+ (rx_config->number_Rx_status_elements - 1);

                 /* Set up next pointer to be used */
                card->u.c.txbuf = (void *)(card->hw.dpmbase +
		(tx_config->next_Tx_status_element_to_use % SDLA_WINDOWSIZE));
                card->u.c.rxmb = (void *)(card->hw.dpmbase +
		(rx_config->next_Rx_status_element_to_use % SDLA_WINDOWSIZE));
        }

        /* Setup Actual Buffer Start and end addresses */
        card->u.c.rx_base = rx_config->base_addr_Rx_buffer;
        card->u.c.rx_top  = rx_config->end_addr_Rx_buffer;

}

/*=============================================================================
 * Perform Interrupt Test by running READ_CHDLC_CODE_VERSION command MAX_INTR
 * _TEST_COUNTER times.
 */
static int intr_test( sdla_t* card)
{
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;
	int err,i;

	Intr_test_counter = 0;

	/* The critical flag is unset because during initialization (if_open) 
	 * we want the interrupts to be enabled so that when the wpc_isr is
	 * called it does not exit due to critical flag set.
	 */ 

	err = chdlc_set_intr_mode(card, APP_INT_ON_COMMAND_COMPLETE);

	if (err == CMD_OK) { 
		for (i = 0; i < MAX_INTR_TEST_COUNTER; i ++) {	
			mb->buffer_length  = 0;
			mb->command = READ_CHDLC_CODE_VERSION;
			err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
		}
	}
	else {
		return err;
	}

	err = chdlc_set_intr_mode(card, 0);

	if (err != CMD_OK)
		return err;

	return 0;
}

/*==============================================================================
 * Determine what type of UDP call it is. CPIPEAB ?
 */
static int udp_pkt_type(struct sk_buff *skb, sdla_t* card)
{
	 chdlc_udp_pkt_t *chdlc_udp_pkt = (chdlc_udp_pkt_t *)skb->data;

	if (!strncmp(chdlc_udp_pkt->wp_mgmt.signature,UDPMGMT_SIGNATURE,8) &&
	   (chdlc_udp_pkt->udp_pkt.udp_dst_port == ntohs(card->wandev.udp_port)) &&
	   (chdlc_udp_pkt->ip_pkt.protocol == UDPMGMT_UDP_PROTOCOL) &&
	   (chdlc_udp_pkt->wp_mgmt.request_reply == UDPMGMT_REQUEST)) {
		return UDP_CPIPE_TYPE;
	}
	else return UDP_INVALID_TYPE;
}

/*============================================================================
 * Set PORT state.
 */
static void port_set_state (sdla_t *card, int state)
{
	struct net_device *dev = card->wandev.dev;
	chdlc_private_area_t *chdlc_priv_area = dev->priv;

        if (card->u.c.state != state)
        {
                switch (state)
                {
                case WAN_CONNECTED:
                        printk (KERN_INFO "%s: HDLC link connected!\n",
                                card->devname);
                      break;

                case WAN_CONNECTING:
                        printk (KERN_INFO "%s: HDLC link connecting...\n",
                                card->devname);
                        break;

                case WAN_DISCONNECTED:
                        printk (KERN_INFO "%s: HDLC link disconnected!\n",
                                card->devname);
                        break;
                }

                card->wandev.state = card->u.c.state = state;
		chdlc_priv_area->common.state = state;
        }
}

void s508_lock (sdla_t *card, unsigned long *smp_flags)
{
	spin_lock_irqsave(&card->wandev.lock, *smp_flags);
        if (card->next){
		/* It is ok to use spin_lock here, since we
		 * already turned off interrupts */
        	spin_lock(&card->next->wandev.lock);
	}
}

void s508_unlock (sdla_t *card, unsigned long *smp_flags)
{
	if (card->next){
		spin_unlock(&card->next->wandev.lock);
	}
	spin_unlock_irqrestore(&card->wandev.lock, *smp_flags);
}



/*===========================================================================
 * config_chdlc
 *
 *	Configure the chdlc protocol and enable communications.		
 *
 *   	The if_open() function binds this function to the poll routine.
 *      Therefore, this function will run every time the chdlc interface
 *      is brought up. We cannot run this function from the if_open 
 *      because if_open does not have access to the remote IP address.
 *      
 *	If the communications are not enabled, proceed to configure
 *      the card and enable communications.
 *
 *      If the communications are enabled, it means that the interface
 *      was shutdown by ether the user or driver. In this case, we 
 *      have to check that the IP addresses have not changed.  If
 *      the IP addresses have changed, we have to reconfigure the firmware
 *      and update the changed IP addresses.  Otherwise, just exit.
 *
 */

static int config_chdlc (sdla_t *card)
{
	struct net_device *dev = card->wandev.dev;
	SHARED_MEMORY_INFO_STRUCT *flags = card->u.c.flags;

	if (card->u.c.comm_enabled){
		chdlc_comm_disable(card);
		port_set_state(card, WAN_DISCONNECTED);
	}

	if (set_chdlc_config(card)) {
		printk(KERN_INFO "%s: CHDLC Configuration Failed!\n",
				card->devname);
		return 0;
	}
	init_chdlc_tx_rx_buff(card, dev);

	/* Set interrupt mode and mask */
        if (chdlc_set_intr_mode(card, APP_INT_ON_RX_FRAME |
                		APP_INT_ON_GLOBAL_EXCEP_COND |
                		APP_INT_ON_TX_FRAME |
                		APP_INT_ON_CHDLC_EXCEP_COND | APP_INT_ON_TIMER)){
		printk (KERN_INFO "%s: Failed to set interrupt triggers!\n",
				card->devname);
		return 0;	
        }
	

	/* Mask the Transmit and Timer interrupt */
	flags->interrupt_info_struct.interrupt_permission &= 
		~(APP_INT_ON_TX_FRAME | APP_INT_ON_TIMER);


	if (chdlc_comm_enable(card) != 0) {
		printk(KERN_INFO "%s: Failed to enable chdlc communications!\n",
				card->devname);
		flags->interrupt_info_struct.interrupt_permission = 0;
		card->u.c.comm_enabled=0;
		chdlc_set_intr_mode(card,0);
		return 0;
	}

	/* Initialize Rx/Tx buffer control fields */
	port_set_state(card, WAN_CONNECTING);
	return 0; 
}


static void send_ppp_term_request(struct net_device *dev)
{
	struct sk_buff *new_skb;
	unsigned char *buf;

	if ((new_skb = dev_alloc_skb(8)) != NULL) {
		/* copy data into new_skb */

		buf = skb_put(new_skb, 8);
		sprintf(buf,"%c%c%c%c%c%c%c%c", 0xFF,0x03,0xC0,0x21,0x05,0x98,0x00,0x07);

		/* Decapsulate pkt and pass it up the protocol stack */
		new_skb->protocol = htons(ETH_P_WAN_PPP);
		new_skb->dev = dev;
		new_skb->mac.raw  = new_skb->data;

		netif_rx(new_skb);
		dev->last_rx = jiffies;
	}
}


MODULE_LICENSE("GPL");

/****** End ****************************************************************/

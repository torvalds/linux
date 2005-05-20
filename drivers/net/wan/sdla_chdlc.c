/*****************************************************************************
* sdla_chdlc.c	WANPIPE(tm) Multiprotocol WAN Link Driver. Cisco HDLC module.
*
* Authors: 	Nenad Corbic <ncorbic@sangoma.com>
*		Gideon Hack  
*
* Copyright:	(c) 1995-2001 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Feb 28, 2001  Nenad Corbic	Updated if_tx_timeout() routine for 
* 				2.4.X kernels.
* Jan 25, 2001  Nenad Corbic	Added a TTY Sync serial driver over the
* 				HDLC streaming protocol
* 				Added a TTY Async serial driver over the
* 				Async protocol.
* Dec 15, 2000  Nenad Corbic    Updated for 2.4.X Kernel support
* Nov 13, 2000  Nenad Corbic    Added true interface type encoding option.
* 				Tcpdump doesn't support CHDLC inteface
* 				types, to fix this "true type" option will set
* 				the interface type to RAW IP mode.
* Nov 07, 2000  Nenad Corbic	Added security features for UDP debugging:
*                               Deny all and specify allowed requests.
* Jun 20, 2000  Nenad Corbic	Fixed the API IP ERROR bug. Caused by the 
*                               latest update.
* May 09, 2000	Nenad Corbic	Option to bring down an interface
*                               upon disconnect.
* Mar 23, 2000  Nenad Corbic	Improved task queue, bh handling.
* Mar 16, 2000	Nenad Corbic	Fixed the SLARP Dynamic IP addressing.
* Mar 06, 2000  Nenad Corbic	Bug Fix: corrupted mbox recovery.
* Feb 10, 2000  Gideon Hack     Added ASYNC support.
* Feb 09, 2000  Nenad Corbic    Fixed two shutdown bugs in update() and
*                               if_stats() functions.
* Jan 24, 2000  Nenad Corbic    Fixed a startup wanpipe state racing,  
*                               condition between if_open and isr. 
* Jan 10, 2000  Nenad Corbic    Added new socket API support.
* Dev 15, 1999  Nenad Corbic    Fixed up header files for 2.0.X kernels
* Nov 20, 1999  Nenad Corbic 	Fixed zero length API bug.
* Sep 30, 1999  Nenad Corbic    Fixed dynamic IP and route setup.
* Sep 23, 1999  Nenad Corbic    Added SMP support, fixed tracing 
* Sep 13, 1999  Nenad Corbic	Split up Port 0 and 1 into separate devices.
* Jun 02, 1999  Gideon Hack     Added support for the S514 adapter.
* Oct 30, 1998	Jaspreet Singh	Added Support for CHDLC API (HDLC STREAMING).
* Oct 28, 1998	Jaspreet Singh	Added Support for Dual Port CHDLC.
* Aug 07, 1998	David Fong	Initial version.
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


#include <asm/uaccess.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>

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

/* TTY Includes */
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>


/****** Defines & Macros ****************************************************/

/* reasons for enabling the timer interrupt on the adapter */
#define TMR_INT_ENABLED_UDP   		0x01
#define TMR_INT_ENABLED_UPDATE		0x02
#define TMR_INT_ENABLED_CONFIG		0x10

#define MAX_IP_ERRORS	10

#define TTY_CHDLC_MAX_MTU	2000
#define	CHDLC_DFLT_DATA_LEN	1500		/* default MTU */
#define CHDLC_HDR_LEN		1

#define CHDLC_API 0x01

#define PORT(x)   (x == 0 ? "PRIMARY" : "SECONDARY" )
#define MAX_BH_BUFF	10

//#define PRINT_DEBUG
#ifdef PRINT_DEBUG
#define dbg_printk(format, a...) printk(format, ## a)
#else
#define dbg_printk(format, a...)
#endif  

/******Data Structures*****************************************************/

/* This structure is placed in the private data area of the device structure.
 * The card structure used to occupy the private area but now the following 
 * structure will incorporate the card structure along with CHDLC specific data
 */

typedef struct chdlc_private_area
{
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
	u32		ip_local;
	u32		ip_remote;
	u32 		ip_local_tmp;
	u32		ip_remote_tmp;
	u8		ip_error;
	u8		config_chdlc;
	u8 		config_chdlc_timeout;
	unsigned char  mc;			/* Mulitcast support on/off */
	unsigned short udp_pkt_lgth;		/* udp packet processing */
	char udp_pkt_src;
	char udp_pkt_data[MAX_LGTH_UDP_MGNT_PKT];
	unsigned short timer_int_enabled;
	char update_comms_stats;		/* updating comms stats */

	bh_data_t *bh_head;	  	  /* Circular buffer for chdlc_bh */
	unsigned long  tq_working;
	volatile int  bh_write;
	volatile int  bh_read;
	atomic_t  bh_buff_used;
	
	unsigned char interface_down;

	/* Polling work queue entry. Each interface
         * has its own work queue entry, which is used
         * to defer events from the interrupt */
	struct work_struct poll_work;
	struct timer_list poll_delay_timer;

	u8 gateway;
	u8 true_if_encoding;
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

/* Network device interface */
static int if_init(struct net_device* dev);
static int if_open(struct net_device* dev);
static int if_close(struct net_device* dev);
static int if_header(struct sk_buff* skb, struct net_device* dev,
		     unsigned short type, void* daddr, void* saddr,
		     unsigned len);

static int if_rebuild_hdr (struct sk_buff *skb);
static struct net_device_stats* if_stats(struct net_device* dev);
  
static int if_send(struct sk_buff* skb, struct net_device* dev);

/* CHDLC Firmware interface functions */
static int chdlc_configure 	(sdla_t* card, void* data);
static int chdlc_comm_enable 	(sdla_t* card);
static int chdlc_read_version 	(sdla_t* card, char* str);
static int chdlc_set_intr_mode 	(sdla_t* card, unsigned mode);
static int chdlc_send (sdla_t* card, void* data, unsigned len);
static int chdlc_read_comm_err_stats (sdla_t* card);
static int chdlc_read_op_stats (sdla_t* card);
static int chdlc_error (sdla_t *card, int err, CHDLC_MAILBOX_STRUCT *mb);


static int chdlc_disable_comm_shutdown (sdla_t *card);
static void if_tx_timeout(struct net_device *dev);

/* Miscellaneous CHDLC Functions */
static int set_chdlc_config (sdla_t* card);
static void init_chdlc_tx_rx_buff( sdla_t* card);
static int process_chdlc_exception(sdla_t *card);
static int process_global_exception(sdla_t *card);
static int update_comms_stats(sdla_t* card,
        chdlc_private_area_t* chdlc_priv_area);
static int configure_ip (sdla_t* card);
static int unconfigure_ip (sdla_t* card);
static void process_route(sdla_t *card);
static void port_set_state (sdla_t *card, int);
static int config_chdlc (sdla_t *card);
static void disable_comm (sdla_t *card);

static void trigger_chdlc_poll(struct net_device *dev);
static void chdlc_poll(struct net_device *dev);
static void chdlc_poll_delay (unsigned long dev_ptr);


/* Miscellaneous asynchronous interface Functions */
static int set_asy_config (sdla_t* card);
static int asy_comm_enable (sdla_t* card);

/* Interrupt handlers */
static void wpc_isr (sdla_t* card);
static void rx_intr (sdla_t* card);
static void timer_intr(sdla_t *);

/* Bottom half handlers */
static void chdlc_work(struct net_device *dev);
static int chdlc_work_cleanup(struct net_device *dev);
static int bh_enqueue(struct net_device *dev, struct sk_buff *skb);

/* Miscellaneous functions */
static int chk_bcast_mcast_addr(sdla_t* card, struct net_device* dev,
				struct sk_buff *skb);
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


static int  Intr_test_counter;

/* TTY Global Definitions */

#define NR_PORTS 4
#define WAN_TTY_MAJOR 226
#define WAN_TTY_MINOR 0

#define WAN_CARD(port) (tty_card_map[port])
#define MIN_PORT 0
#define MAX_PORT NR_PORTS-1 

#define CRC_LENGTH 2

static int wanpipe_tty_init(sdla_t *card);
static void wanpipe_tty_receive(sdla_t *, unsigned, unsigned int);
static void wanpipe_tty_trigger_poll(sdla_t *card);

static struct tty_driver serial_driver;
static int tty_init_cnt=0;

static struct serial_state rs_table[NR_PORTS];

static char tty_driver_mode=WANOPT_TTY_SYNC;

static char *opt_decode[] = {"NONE","CRTSCTS","XONXOFF-RX",
	  	             "CRTSCTS XONXOFF-RX","XONXOFF-TX",
		             "CRTSCTS XONXOFF-TX","CRTSCTS XONXOFF"};
static char *p_decode[] = {"NONE","ODD","EVEN"};

static void* tty_card_map[NR_PORTS] = {NULL,NULL,NULL,NULL};


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
int wpc_init (sdla_t* card, wandev_conf_t* conf)
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
	if (conf->config_id != WANCONFIG_CHDLC) {
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
				printk(KERN_INFO "%s: ERROR - %s port used!\n",
        		        	card->wandev.name, PORT(conf->comm_port));
				return -EINVAL;
			}
		}else{
			card->u.c.comm_port = conf->comm_port;
		}
	}else{
		printk(KERN_INFO "%s: ERROR - Invalid Port Selected!\n",
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

	printk(KERN_INFO "%s: Running Cisco HDLC firmware v%s\n",
		card->devname, u.str); 

	card->isr			= &wpc_isr;
	card->poll			= NULL;
	card->exec			= NULL;
	card->wandev.update		= &update;
 	card->wandev.new_if		= &new_if;
	card->wandev.del_if		= NULL;
	card->wandev.udp_port   	= conf->udp_port;
	card->disable_comm		= &disable_comm;
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

	/* in API mode, we can configure for "receive only" buffering */
	if(card->hw.type == SDLA_S514) {
		card->u.c.receive_only = conf->receive_only;
		if(conf->receive_only) {
			printk(KERN_INFO
				"%s: Configured for 'receive only' mode\n",
                                card->devname);
		}
	}

	/* Setup Port Bps */

	if(card->wandev.clocking) {
		if((port_num == WANOPT_PRI) || card->u.c.receive_only) {
			/* For Primary Port 0 */
               		max_permitted_baud =
				(card->hw.type == SDLA_S514) ?
				PRI_MAX_BAUD_RATE_S514 : 
				PRI_MAX_BAUD_RATE_S508;

		}else if(port_num == WANOPT_SEC) {
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

	/* Set up the interrupt status area */
	/* Read the CHDLC Configuration and obtain: 
	 *	Ptr to shared memory infor struct
         * Use this pointer to calculate the value of card->u.c.flags !
 	 */
	mb1->buffer_length = 0;
	mb1->command = READ_CHDLC_CONFIGURATION;
	err = sdla_exec(mb1) ? mb1->return_code : CMD_TIMEOUT;
	if(err != COMMAND_OK) {
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
		int err;

		/* Perform interrupt testing */
		err = intr_test(card);

		if(err || (Intr_test_counter < MAX_INTR_TEST_COUNTER)) { 
			printk(KERN_INFO "%s: Interrupt test failed (%i)\n",
					card->devname, Intr_test_counter);
			printk(KERN_INFO "%s: Please choose another interrupt\n",
					card->devname);
			return -EIO;
		}
		
		printk(KERN_INFO "%s: Interrupt test passed (%i)\n", 
				card->devname, Intr_test_counter);
		card->configured = 1;
	}

	if ((card->tty_opt=conf->tty) == WANOPT_YES){
		int err;
		card->tty_minor = conf->tty_minor;

		/* On ASYNC connections internal clocking 
		 * is mandatory */
		if ((card->u.c.async_mode = conf->tty_mode)){
			card->wandev.clocking = 1;
		}
		err=wanpipe_tty_init(card);
		if (err){
			return err;
		}
	}else{
	

		if (chdlc_set_intr_mode(card, APP_INT_ON_TIMER)){
			printk (KERN_INFO "%s: "
				"Failed to set interrupt triggers!\n",
				card->devname);
			return -EIO;	
        	}
	
		/* Mask the Timer interrupt */
		flags->interrupt_info_struct.interrupt_permission &= 
			~APP_INT_ON_TIMER;
	}

	/* If we are using CHDLC in backup mode, this flag will
	 * indicate not to look for IP addresses in config_chdlc()*/
	card->u.c.backup = conf->backup;
	
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

	if(test_bit(PERI_CRIT, (void*)&card->wandev.critical))
                return -EAGAIN;

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
static int new_if(struct wan_device* wandev, struct net_device* dev,
		  wanif_conf_t* conf)
{
	sdla_t* card = wandev->private;
	chdlc_private_area_t* chdlc_priv_area;


	printk(KERN_INFO "%s: Configuring Interface: %s\n",
			card->devname, conf->name);
 
	if ((conf->name[0] == '\0') || (strlen(conf->name) > WAN_IFNAME_SZ)) {
		printk(KERN_INFO "%s: Invalid interface name!\n",
			card->devname);
		return -EINVAL;
	}
		
	/* allocate and initialize private data */
	chdlc_priv_area = kmalloc(sizeof(chdlc_private_area_t), GFP_KERNEL);
	
	if(chdlc_priv_area == NULL) 
		return -ENOMEM;

	memset(chdlc_priv_area, 0, sizeof(chdlc_private_area_t));

	chdlc_priv_area->card = card; 
	chdlc_priv_area->common.sk = NULL;
	chdlc_priv_area->common.func = NULL;	

	/* initialize data */
	strcpy(card->u.c.if_name, conf->name);

	if(card->wandev.new_if_cnt > 0) {
                kfree(chdlc_priv_area);
		return -EEXIST;
	}

	card->wandev.new_if_cnt++;

	chdlc_priv_area->TracingEnabled = 0;
	chdlc_priv_area->route_status = NO_ROUTE;
	chdlc_priv_area->route_removed = 0;

	card->u.c.async_mode = conf->async_mode;
	
	/* setup for asynchronous mode */
	if(conf->async_mode) {
		printk(KERN_INFO "%s: Configuring for asynchronous mode\n",
			wandev->name);

		if(card->u.c.comm_port == WANOPT_PRI) {
			printk(KERN_INFO
				"%s:Asynchronous mode on secondary port only\n",
					wandev->name);
			kfree(chdlc_priv_area);
			return -EINVAL;
		}

	       	if(strcmp(conf->usedby, "WANPIPE") == 0) {
			printk(KERN_INFO
                                "%s: Running in WANIPE Async Mode\n",                                        			wandev->name);
			card->u.c.usedby = WANPIPE;
		}else{
			card->u.c.usedby = API;
		}

		if(!card->wandev.clocking) {
			printk(KERN_INFO
				"%s: Asynch. clocking must be 'Internal'\n",
				wandev->name);
			kfree(chdlc_priv_area);
			return -EINVAL;
		}

		if((card->wandev.bps < MIN_ASY_BAUD_RATE) ||
			(card->wandev.bps > MAX_ASY_BAUD_RATE)) {
			printk(KERN_INFO "%s: Selected baud rate is invalid.\n",
				wandev->name);
			printk(KERN_INFO "Must be between %u and %u bps.\n",
				MIN_ASY_BAUD_RATE, MAX_ASY_BAUD_RATE);
			kfree(chdlc_priv_area);
			return -EINVAL;
		}

		card->u.c.api_options = 0;
                if (conf->asy_data_trans == WANOPT_YES) {
                        card->u.c.api_options |= ASY_RX_DATA_TRANSPARENT;
                }
		
		card->u.c.protocol_options = 0;
		if (conf->rts_hs_for_receive == WANOPT_YES) {
			card->u.c.protocol_options |= ASY_RTS_HS_FOR_RX;
	        }
                if (conf->xon_xoff_hs_for_receive == WANOPT_YES) {
                        card->u.c.protocol_options |= ASY_XON_XOFF_HS_FOR_RX;
                }
                if (conf->xon_xoff_hs_for_transmit == WANOPT_YES) {
                        card->u.c.protocol_options |= ASY_XON_XOFF_HS_FOR_TX;
                }
                if (conf->dcd_hs_for_transmit == WANOPT_YES) {
                        card->u.c.protocol_options |= ASY_DCD_HS_FOR_TX;
                }
                if (conf->cts_hs_for_transmit == WANOPT_YES) {
                        card->u.c.protocol_options |= ASY_CTS_HS_FOR_TX;
                }

		card->u.c.tx_bits_per_char = conf->tx_bits_per_char;
                card->u.c.rx_bits_per_char = conf->rx_bits_per_char;
                card->u.c.stop_bits = conf->stop_bits;
		card->u.c.parity = conf->parity;
		card->u.c.break_timer = conf->break_timer;
		card->u.c.inter_char_timer = conf->inter_char_timer;
		card->u.c.rx_complete_length = conf->rx_complete_length;
		card->u.c.xon_char = conf->xon_char;

	} else {	/* setup for synchronous mode */

		card->u.c.protocol_options = 0;
		if (conf->ignore_dcd == WANOPT_YES){
			card->u.c.protocol_options |= IGNORE_DCD_FOR_LINK_STAT;
		}
		if (conf->ignore_cts == WANOPT_YES){
			card->u.c.protocol_options |= IGNORE_CTS_FOR_LINK_STAT;
		}

		if (conf->ignore_keepalive == WANOPT_YES) {
			card->u.c.protocol_options |=
				IGNORE_KPALV_FOR_LINK_STAT;
			card->u.c.kpalv_tx  = MIN_Tx_KPALV_TIMER; 
			card->u.c.kpalv_rx  = MIN_Rx_KPALV_TIMER; 
			card->u.c.kpalv_err = MIN_KPALV_ERR_TOL; 

		} else {   /* Do not ignore keepalives */
			card->u.c.kpalv_tx =
				((conf->keepalive_tx_tmr - MIN_Tx_KPALV_TIMER)
				>= 0) ?
	   			min_t(unsigned int, conf->keepalive_tx_tmr,MAX_Tx_KPALV_TIMER) :
				DEFAULT_Tx_KPALV_TIMER;

			card->u.c.kpalv_rx =
		   		((conf->keepalive_rx_tmr - MIN_Rx_KPALV_TIMER)
				>= 0) ?
	   			min_t(unsigned int, conf->keepalive_rx_tmr,MAX_Rx_KPALV_TIMER) :
				DEFAULT_Rx_KPALV_TIMER;

			card->u.c.kpalv_err =
		   		((conf->keepalive_err_margin-MIN_KPALV_ERR_TOL)
				>= 0) ?
	   			min_t(unsigned int, conf->keepalive_err_margin,
				MAX_KPALV_ERR_TOL) : 
	   			DEFAULT_KPALV_ERR_TOL;
		}

		/* Setup slarp timer to control delay between slarps */
		card->u.c.slarp_timer = 
			((conf->slarp_timer - MIN_SLARP_REQ_TIMER) >= 0) ?
			min_t(unsigned int, conf->slarp_timer, MAX_SLARP_REQ_TIMER) :
			DEFAULT_SLARP_REQ_TIMER;

		if (conf->hdlc_streaming == WANOPT_YES) {
			printk(KERN_INFO "%s: Enabling HDLC STREAMING Mode\n",
				wandev->name);
			card->u.c.protocol_options = HDLC_STREAMING_MODE;
		}

		if ((chdlc_priv_area->true_if_encoding = conf->true_if_encoding) == WANOPT_YES){
			printk(KERN_INFO 
				"%s: Enabling, true interface type encoding.\n",
				card->devname);
		}
		
        	/* Setup wanpipe as a router (WANPIPE) or as an API */
		if( strcmp(conf->usedby, "WANPIPE") == 0) {

			printk(KERN_INFO "%s: Running in WANPIPE mode!\n",
				wandev->name);
			card->u.c.usedby = WANPIPE;

			/* Option to bring down the interface when 
        		 * the link goes down */
			if (conf->if_down){
				set_bit(DYN_OPT_ON,&chdlc_priv_area->interface_down);
				printk(KERN_INFO 
				 "%s: Dynamic interface configuration enabled\n",
				   card->devname);
			} 

		} else if( strcmp(conf->usedby, "API") == 0) {
			card->u.c.usedby = API;
			printk(KERN_INFO "%s: Running in API mode !\n",
				wandev->name);
		}
	}

	/* Tells us that if this interface is a
         * gateway or not */
	if ((chdlc_priv_area->gateway = conf->gateway) == WANOPT_YES){
		printk(KERN_INFO "%s: Interface %s is set as a gateway.\n",
			card->devname,card->u.c.if_name);
	}

	/* Get Multicast Information */
	chdlc_priv_area->mc = conf->mc;

	/* prepare network device data space for registration */
	strcpy(dev->name,card->u.c.if_name);

	dev->init = &if_init;
	dev->priv = chdlc_priv_area;

	/* Initialize the polling work routine */
	INIT_WORK(&chdlc_priv_area->poll_work, (void*)(void*)chdlc_poll, dev);

	/* Initialize the polling delay timer */
	init_timer(&chdlc_priv_area->poll_delay_timer);
	chdlc_priv_area->poll_delay_timer.data = (unsigned long)dev;
	chdlc_priv_area->poll_delay_timer.function = chdlc_poll_delay;
	
	printk(KERN_INFO "\n");

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

	/* Initialize device driver entry points */
	dev->open		= &if_open;
	dev->stop		= &if_close;
	dev->hard_header	= &if_header;
	dev->rebuild_header	= &if_rebuild_hdr;
	dev->hard_start_xmit	= &if_send;
	dev->get_stats		= &if_stats;
	dev->tx_timeout		= &if_tx_timeout;
	dev->watchdog_timeo	= TX_TIMEOUT;
	
	/* Initialize media-specific parameters */
	dev->flags		|= IFF_POINTOPOINT;
	dev->flags		|= IFF_NOARP;

	/* Enable Mulitcasting if user selected */
	if (chdlc_priv_area->mc == WANOPT_YES){
		dev->flags 	|= IFF_MULTICAST;
	}
	
	if (chdlc_priv_area->true_if_encoding){
		dev->type	= ARPHRD_HDLC; /* This breaks the tcpdump */
	}else{
		dev->type	= ARPHRD_PPP;
	}
	
	dev->mtu		= card->wandev.mtu;
	/* for API usage, add the API header size to the requested MTU size */
	if(card->u.c.usedby == API) {
		dev->mtu += sizeof(api_tx_hdr_t);
	}
 
	dev->hard_header_len	= CHDLC_HDR_LEN;

	/* Initialize hardware parameters */
	dev->irq	= wandev->irq;
	dev->dma	= wandev->dma;
	dev->base_addr	= wandev->ioport;
	dev->mem_start	= wandev->maddr;
	dev->mem_end	= wandev->maddr + wandev->msize - 1;

	/* Set transmit buffer queue length 
	 * If too low packets will not be retransmitted 
         * by stack.
	 */
        dev->tx_queue_len = 100;
	SET_MODULE_OWNER(dev);
   
	return 0;
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
	int err = 0;

	/* Only one open per interface is allowed */

	if (netif_running(dev))
		return -EBUSY;

	/* Initialize the work queue entry */
	chdlc_priv_area->tq_working=0;

	INIT_WORK(&chdlc_priv_area->common.wanpipe_work,
			(void *)(void *)chdlc_work, dev);

	/* Allocate and initialize BH circular buffer */
	/* Add 1 to MAX_BH_BUFF so we don't have test with (MAX_BH_BUFF-1) */
	chdlc_priv_area->bh_head = kmalloc((sizeof(bh_data_t)*(MAX_BH_BUFF+1)),GFP_ATOMIC);
	memset(chdlc_priv_area->bh_head,0,(sizeof(bh_data_t)*(MAX_BH_BUFF+1)));
	atomic_set(&chdlc_priv_area->bh_buff_used, 0);
 
	do_gettimeofday(&tv);
	chdlc_priv_area->router_start_time = tv.tv_sec;

	netif_start_queue(dev);

	wanpipe_open(card);

	/* TTY is configured during wanpipe_set_termios
	 * call, not here */
	if (card->tty_opt)
		return err;
	
	set_bit(0,&chdlc_priv_area->config_chdlc);
	chdlc_priv_area->config_chdlc_timeout=jiffies;

	/* Start the CHDLC configuration after 1sec delay.
	 * This will give the interface initilization time
	 * to finish its configuration */
	mod_timer(&chdlc_priv_area->poll_delay_timer, jiffies + HZ);
	return err;
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

	if (chdlc_priv_area->bh_head){
		int i;
		struct sk_buff *skb;
	
		for (i=0; i<(MAX_BH_BUFF+1); i++){
			skb = ((bh_data_t *)&chdlc_priv_area->bh_head[i])->skb;
			if (skb != NULL){
                		dev_kfree_skb_any(skb);
			}
		}
		kfree(chdlc_priv_area->bh_head);
		chdlc_priv_area->bh_head=NULL;
	}

	netif_stop_queue(dev);
	wanpipe_close(card);
	del_timer(&chdlc_priv_area->poll_delay_timer);
	return 0;
}

static void disable_comm (sdla_t *card)
{
	SHARED_MEMORY_INFO_STRUCT *flags = card->u.c.flags;
	
	if (card->u.c.comm_enabled){
		chdlc_disable_comm_shutdown (card);
	}else{
		flags->interrupt_info_struct.interrupt_permission = 0;	
	}

	if (!tty_init_cnt)
		return;

	if (card->tty_opt){
		struct serial_state * state;
		if (!(--tty_init_cnt)){
			int e1;
			serial_driver.refcount=0;
			
			if ((e1 = tty_unregister_driver(&serial_driver)))
				printk("SERIAL: failed to unregister serial driver (%d)\n",
				       e1);
			printk(KERN_INFO "%s: Unregistering TTY Driver, Major %i\n",
					card->devname,WAN_TTY_MAJOR);
		}
		card->tty=NULL;
		tty_card_map[card->tty_minor]=NULL;
		state = &rs_table[card->tty_minor];
		memset(state, 0, sizeof(*state));
	}
	return;
}


/*============================================================================
 * Build media header.
 *
 * The trick here is to put packet type (Ethertype) into 'protocol' field of
 * the socket buffer, so that we don't forget it.  If packet type is not
 * supported, set skb->protocol to 0 and discard packet later.
 *
 * Return:	media header length.
 */
static int if_header(struct sk_buff* skb, struct net_device* dev,
		     unsigned short type, void* daddr, void* saddr,
		     unsigned len)
{
	skb->protocol = htons(type);

	return CHDLC_HDR_LEN;
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
 * Re-build media header.
 *
 * Return:	1	physical address resolved.
 *		0	physical address not resolved
 */
static int if_rebuild_hdr (struct sk_buff *skb)
{
	return 1;
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
		printk(KERN_INFO "%s: interface %s got kicked!\n",
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

		/* check to see if the source IP address is a broadcast or */
		/* multicast IP address */
                if(chk_bcast_mcast_addr(card, dev, skb)){
			++card->wandev.stats.tx_dropped;
			dev_kfree_skb_any(skb);
			netif_start_queue(dev);
			return 0;
		}
        }

	/* Lock the 508 Card: SMP is supported */
      	if(card->hw.type != SDLA_S514){
		s508_lock(card,&smp_flags);
	} 

    	if(test_and_set_bit(SEND_CRIT, (void*)&card->wandev.critical)) {
	
		printk(KERN_INFO "%s: Critical in if_send: %lx\n",
					card->wandev.name,card->wandev.critical);
                ++card->wandev.stats.tx_dropped;
		netif_start_queue(dev);
		goto if_send_exit_crit;
	}

	if(card->u.c.state != WAN_CONNECTED){
       		++card->wandev.stats.tx_dropped;
		netif_start_queue(dev);
		
	}else if(!skb->protocol){
        	++card->wandev.stats.tx_errors;
		netif_start_queue(dev);
		
	}else {
		void* data = skb->data;
		unsigned len = skb->len;
		unsigned char attr;

		/* If it's an API packet pull off the API
		 * header. Also check that the packet size
		 * is larger than the API header
	         */
		if (card->u.c.usedby == API){
			api_tx_hdr_t* api_tx_hdr;

			/* discard the frame if we are configured for */
			/* 'receive only' mode or if there is no data */
			if (card->u.c.receive_only ||
				(len <= sizeof(api_tx_hdr_t))) {
				
				++card->wandev.stats.tx_dropped;
				netif_start_queue(dev);
				goto if_send_exit_crit;
			}
				
			api_tx_hdr = (api_tx_hdr_t *)data;
			attr = api_tx_hdr->attr;
			data += sizeof(api_tx_hdr_t);
			len -= sizeof(api_tx_hdr_t);
		}

		if(chdlc_send(card, data, len)) {
			netif_stop_queue(dev);
		}else{
			++card->wandev.stats.tx_packets;
                        card->wandev.stats.tx_bytes += len;
			
			netif_start_queue(dev);
			
		 	dev->trans_start = jiffies;
		}	
	}

if_send_exit_crit:
	
	if (!(err=netif_queue_stopped(dev))) {
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
 * Check to see if the packet to be transmitted contains a broadcast or
 * multicast source IP address.
 */

static int chk_bcast_mcast_addr(sdla_t *card, struct net_device* dev,
				struct sk_buff *skb)
{
	u32 src_ip_addr;
        u32 broadcast_ip_addr = 0;
        struct in_device *in_dev;

        /* read the IP source address from the outgoing packet */
        src_ip_addr = *(u32 *)(skb->data + 12);

	/* read the IP broadcast address for the device */
        in_dev = dev->ip_ptr;
        if(in_dev != NULL) {
                struct in_ifaddr *ifa= in_dev->ifa_list;
                if(ifa != NULL)
                        broadcast_ip_addr = ifa->ifa_broadcast;
                else
                        return 0;
        }
 
        /* check if the IP Source Address is a Broadcast address */
        if((dev->flags & IFF_BROADCAST) && (src_ip_addr == broadcast_ip_addr)) {
                printk(KERN_INFO "%s: Broadcast Source Address silently discarded\n",
				card->devname);
                return 1;
        } 

        /* check if the IP Source Address is a Multicast address */
        if((ntohl(src_ip_addr) >= 0xE0000001) &&
		(ntohl(src_ip_addr) <= 0xFFFFFFFE)) {
                printk(KERN_INFO "%s: Multicast Source Address silently discarded\n",
				card->devname);
                return 1;
        }

        return 0;
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


/*===========================================================
 * chdlc_disable_comm_shutdown
 *
 * Shutdown() disables the communications. We must
 * have a sparate functions, because we must not
 * call chdlc_error() hander since the private
 * area has already been replaced */

static int chdlc_disable_comm_shutdown (sdla_t *card)
{
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;
	CHDLC_INT_TRIGGERS_STRUCT* int_data =
		 (CHDLC_INT_TRIGGERS_STRUCT *)mb->data;
	int err;

	/* Disable Interrutps */
	int_data->CHDLC_interrupt_triggers 	= 0;
	int_data->IRQ				= card->hw.irq;
	int_data->interrupt_timer               = 1;
   
	mb->buffer_length = sizeof(CHDLC_INT_TRIGGERS_STRUCT);
	mb->command = SET_CHDLC_INTERRUPT_TRIGGERS;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;

	/* Disable Communications */

	if (card->u.c.async_mode) {
		mb->command = DISABLE_ASY_COMMUNICATIONS;
	}else{
		mb->command = DISABLE_CHDLC_COMMUNICATIONS;
	}
	
	mb->buffer_length = 0;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	
	card->u.c.comm_enabled = 0;
	
	return 0;
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
		card->u.c.comm_enabled = 1;
	
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
		printk(KERN_INFO "%s: command 0x%02X timed out!\n",
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


/********** Bottom Half Handlers ********************************************/

/* NOTE: There is no API, BH support for Kernels lower than 2.2.X.
 *       DO NOT INSERT ANY CODE HERE, NOTICE THE 
 *       PREPROCESSOR STATEMENT ABOVE, UNLESS YOU KNOW WHAT YOU ARE
 *       DOING */

static void chdlc_work(struct net_device * dev)
{
	chdlc_private_area_t* chan = dev->priv;
	sdla_t *card = chan->card;
	struct sk_buff *skb;

	if (atomic_read(&chan->bh_buff_used) == 0){
		clear_bit(0, &chan->tq_working);
		return;
	}

	while (atomic_read(&chan->bh_buff_used)){

		skb  = ((bh_data_t *)&chan->bh_head[chan->bh_read])->skb;

		if (skb != NULL){

			if (chan->common.sk == NULL || chan->common.func == NULL){
				++card->wandev.stats.rx_dropped;
				dev_kfree_skb_any(skb);
				chdlc_work_cleanup(dev);
				continue;
			}

			if (chan->common.func(skb,dev,chan->common.sk) != 0){
				/* Sock full cannot send, queue us for another
                                 * try */
				atomic_set(&chan->common.receive_block,1);
				return;
			}else{
				chdlc_work_cleanup(dev);
			}
		}else{
			chdlc_work_cleanup(dev);
		}
	}	
	clear_bit(0, &chan->tq_working);

	return;
}

static int chdlc_work_cleanup(struct net_device *dev)
{
	chdlc_private_area_t* chan = dev->priv;

	((bh_data_t *)&chan->bh_head[chan->bh_read])->skb = NULL;

	if (chan->bh_read == MAX_BH_BUFF){
		chan->bh_read=0;
	}else{
		++chan->bh_read;	
	}

	atomic_dec(&chan->bh_buff_used);
	return 0;
}



static int bh_enqueue(struct net_device *dev, struct sk_buff *skb)
{
	/* Check for full */
	chdlc_private_area_t* chan = dev->priv;
	sdla_t *card = chan->card;

	if (atomic_read(&chan->bh_buff_used) == (MAX_BH_BUFF+1)){
		++card->wandev.stats.rx_dropped;
		dev_kfree_skb_any(skb);
		return 1; 
	}

	((bh_data_t *)&chan->bh_head[chan->bh_write])->skb = skb;

	if (chan->bh_write == MAX_BH_BUFF){
		chan->bh_write=0;
	}else{
		++chan->bh_write;
	}

	atomic_inc(&chan->bh_buff_used);

	return 0;
}

/* END OF API BH Support */


/****** Interrupt Handlers **************************************************/

/*============================================================================
 * Cisco HDLC interrupt service routine.
 */
static void wpc_isr (sdla_t* card)
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
		if ((my_card = card->next)){
			flags = my_card->u.c.flags;
			if (flags->interrupt_info_struct.interrupt_type){
				card = my_card;
				card->isr(card);
				return;
			}
		}
	}

	flags = card->u.c.flags;
	card->in_isr = 1;
	dev = card->wandev.dev;
	
	/* If we get an interrupt with no network device, stop the interrupts
	 * and issue an error */
	if (!card->tty_opt && !dev && 
	    flags->interrupt_info_struct.interrupt_type != 
	    	COMMAND_COMPLETE_APP_INT_PEND){

		goto isr_done;
	}
	
	/* if critical due to peripheral operations
	 * ie. update() or getstats() then reset the interrupt and
	 * wait for the board to retrigger.
	 */
	if(test_bit(PERI_CRIT, (void*)&card->wandev.critical)) {
		printk(KERN_INFO "ISR CRIT TO PERI\n");
		goto isr_done;
	}

	/* On a 508 Card, if critical due to if_send 
         * Major Error !!! */
	if(card->hw.type != SDLA_S514) {
		if(test_bit(SEND_CRIT, (void*)&card->wandev.critical)) {
			printk(KERN_INFO "%s: Critical while in ISR: %lx\n",
				card->devname, card->wandev.critical);
			card->in_isr = 0;
			flags->interrupt_info_struct.interrupt_type = 0;
			return;
		}
	}

	switch(flags->interrupt_info_struct.interrupt_type) {

	case RX_APP_INT_PEND:	/* 0x01: receive interrupt */
		rx_intr(card);
		break;

	case TX_APP_INT_PEND:	/* 0x02: transmit interrupt */
		flags->interrupt_info_struct.interrupt_permission &=
			 ~APP_INT_ON_TX_FRAME;

		if (card->tty_opt){
			wanpipe_tty_trigger_poll(card);
			break;
		}

		if (dev && netif_queue_stopped(dev)){
			if (card->u.c.usedby == API){
				netif_start_queue(dev);
				wakeup_sk_bh(dev);
			}else{
				netif_wake_queue(dev);
			}
		}
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
	return;
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

	len  = rxbuf->frame_length;

	if (card->tty_opt){

		if (rxbuf->error_flag){	
			goto rx_exit;
		}

		if (len <= CRC_LENGTH){
			goto rx_exit;
		}
		
		if (!card->u.c.async_mode){
			len -= CRC_LENGTH;
		}

		wanpipe_tty_receive(card,addr,len);
		goto rx_exit;
	}

	dev = card->wandev.dev;

	if (!dev){
		goto rx_exit;
	}

	if (!netif_running(dev))
		goto rx_exit;

	chdlc_priv_area = dev->priv;

	
	/* Allocate socket buffer */
	skb = dev_alloc_skb(len);

	if (skb == NULL) {
		printk(KERN_INFO "%s: no socket buffers available!\n",
					card->devname);
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

	skb->protocol = htons(ETH_P_IP);

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
	} else if(card->u.c.usedby == API) {

		api_rx_hdr_t* api_rx_hdr;
       		skb_push(skb, sizeof(api_rx_hdr_t));
                api_rx_hdr = (api_rx_hdr_t*)&skb->data[0x00];
		api_rx_hdr->error_flag = rxbuf->error_flag;
     		api_rx_hdr->time_stamp = rxbuf->time_stamp;

                skb->protocol = htons(PVC_PROT);
     		skb->mac.raw  = skb->data;
		skb->dev      = dev;
               	skb->pkt_type = WAN_PACKET_DATA;

		bh_enqueue(dev, skb);

		if (!test_and_set_bit(0,&chdlc_priv_area->tq_working))
			wanpipe_queue_work(&chdlc_priv_area->common.wanpipe_work);
	}else{
		/* FIXME: we should check to see if the received packet is a 
                          multicast packet so that we can increment the multicast 
                          statistic
                          ++ chdlc_priv_area->if_stats.multicast;
		*/
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

        if ((dev = card->wandev.dev)==NULL){
		flags = card->u.c.flags;
                flags->interrupt_info_struct.interrupt_permission &=
                        ~APP_INT_ON_TIMER;
		return;
	}
	
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

	if(card->wandev.clocking){
		cfg.baud_rate = card->wandev.bps;
	}
		
	cfg.line_config_options = (card->wandev.interface == WANOPT_RS232) ?
		INTERFACE_LEVEL_RS232 : INTERFACE_LEVEL_V35;

	cfg.modem_config_options	= 0;
	cfg.modem_status_timer		= 100;

	cfg.CHDLC_protocol_options	= card->u.c.protocol_options;

	if (card->tty_opt){
		cfg.CHDLC_API_options	= DISCARD_RX_ERROR_FRAMES;
	}
	
	cfg.percent_data_buffer_for_Tx  = (card->u.c.receive_only) ? 0 : 50;
	cfg.CHDLC_statistics_options	= (CHDLC_TX_DATA_BYTE_COUNT_STAT |
		CHDLC_RX_DATA_BYTE_COUNT_STAT);
	
	if (card->tty_opt){
		card->wandev.mtu = TTY_CHDLC_MAX_MTU;
	}
	cfg.max_CHDLC_data_field_length	= card->wandev.mtu;
	cfg.transmit_keepalive_timer	= card->u.c.kpalv_tx;
	cfg.receive_keepalive_timer	= card->u.c.kpalv_rx;
	cfg.keepalive_error_tolerance	= card->u.c.kpalv_err;
	cfg.SLARP_request_timer		= card->u.c.slarp_timer;

	if (cfg.SLARP_request_timer) {
		cfg.IP_address		= 0;
		cfg.IP_netmask		= 0;
		
	}else if (card->wandev.dev){
		struct net_device *dev = card->wandev.dev;
		chdlc_private_area_t *chdlc_priv_area = dev->priv;
		
                struct in_device *in_dev = dev->ip_ptr;

		if(in_dev != NULL) {
			struct in_ifaddr *ifa = in_dev->ifa_list;

			if (ifa != NULL ) {
				cfg.IP_address	= ntohl(ifa->ifa_local);
				cfg.IP_netmask	= ntohl(ifa->ifa_mask); 
				chdlc_priv_area->IP_address = ntohl(ifa->ifa_local);
				chdlc_priv_area->IP_netmask = ntohl(ifa->ifa_mask); 
			}
		}

		/* FIXME: We must re-think this message in next release
		if((cfg.IP_address & 0x000000FF) > 2) {
			printk(KERN_WARNING "\n");
	                printk(KERN_WARNING "  WARNING:%s configured with an\n",
				card->devname);
			printk(KERN_WARNING "  invalid local IP address.\n");
                        printk(KERN_WARNING "  Slarp pragmatics will fail.\n");
                        printk(KERN_WARNING "  IP address should be of the\n");
			printk(KERN_WARNING "  format A.B.C.1 or A.B.C.2.\n");
		}
		*/		
	}
	
	return chdlc_configure(card, &cfg);
}


/*-----------------------------------------------------------------------------
   set_asy_config() used to set asynchronous configuration options on the board
------------------------------------------------------------------------------*/

static int set_asy_config(sdla_t* card)
{

        ASY_CONFIGURATION_STRUCT cfg;
 	CHDLC_MAILBOX_STRUCT *mailbox = card->mbox;
	int err;

	memset(&cfg, 0, sizeof(ASY_CONFIGURATION_STRUCT));

	if(card->wandev.clocking)
		cfg.baud_rate = card->wandev.bps;

	cfg.line_config_options = (card->wandev.interface == WANOPT_RS232) ?
		INTERFACE_LEVEL_RS232 : INTERFACE_LEVEL_V35;

	cfg.modem_config_options	= 0;
	cfg.asy_API_options 		= card->u.c.api_options;
	cfg.asy_protocol_options	= card->u.c.protocol_options;
	cfg.Tx_bits_per_char		= card->u.c.tx_bits_per_char;
	cfg.Rx_bits_per_char		= card->u.c.rx_bits_per_char;
	cfg.stop_bits			= card->u.c.stop_bits;
	cfg.parity			= card->u.c.parity;
	cfg.break_timer			= card->u.c.break_timer;
	cfg.asy_Rx_inter_char_timer	= card->u.c.inter_char_timer; 
	cfg.asy_Rx_complete_length	= card->u.c.rx_complete_length; 
	cfg.XON_char			= card->u.c.xon_char;
	cfg.XOFF_char			= card->u.c.xoff_char;
	cfg.asy_statistics_options	= (CHDLC_TX_DATA_BYTE_COUNT_STAT |
		CHDLC_RX_DATA_BYTE_COUNT_STAT);

	mailbox->buffer_length = sizeof(ASY_CONFIGURATION_STRUCT);
	memcpy(mailbox->data, &cfg, mailbox->buffer_length);
	mailbox->command = SET_ASY_CONFIGURATION;
	err = sdla_exec(mailbox) ? mailbox->return_code : CMD_TIMEOUT;
	if (err != COMMAND_OK) 
		chdlc_error (card, err, mailbox);
	return err;
}

/*============================================================================
 * Enable asynchronous communications.
 */

static int asy_comm_enable (sdla_t* card)
{

	int err;
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;

	mb->buffer_length = 0;
	mb->command = ENABLE_ASY_COMMUNICATIONS;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	if (err != COMMAND_OK && card->wandev.dev)
		chdlc_error(card, err, mb);
	
	if (!err)
		card->u.c.comm_enabled = 1;

	return err;
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
			break;

                case EXCEP_TRC_DISABLED:
                        printk(KERN_INFO "%s: Line trace disabled\n",
				card->devname);
                        break;

		case EXCEP_IRQ_TIMEOUT:
			printk(KERN_INFO "%s: IRQ timeout occurred\n",
				card->devname); 
			break;

		case 0x17:
			if (card->tty_opt){
				if (card->tty && card->tty_open){ 
					printk(KERN_INFO 
						"%s: Modem Hangup Exception: Hanging Up!\n",
						card->devname);
					tty_hangup(card->tty);
				}
				break;
			}

			/* If TTY is not used just drop throught */
			
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
			trigger_chdlc_poll(card->wandev.dev);
			break;

		case EXCEP_LINK_INACTIVE_MODEM:
			port_set_state(card, WAN_DISCONNECTED);
			unconfigure_ip(card);
			trigger_chdlc_poll(card->wandev.dev);
			break;

		case EXCEP_LINK_INACTIVE_KPALV:
			port_set_state(card, WAN_DISCONNECTED);
			printk(KERN_INFO "%s: Keepalive timer expired.\n",
				 		card->devname);
			unconfigure_ip(card);
			trigger_chdlc_poll(card->wandev.dev);
			break;

		case EXCEP_IP_ADDRESS_DISCOVERED:
			if (configure_ip(card)) 
				return -1;
			break;

		case EXCEP_LOOPBACK_CONDITION:
			printk(KERN_INFO "%s: Loopback Condition Detected.\n",
						card->devname);
			break;

		case NO_CHDLC_EXCEP_COND_TO_REPORT:
			printk(KERN_INFO "%s: No exceptions reported.\n",
						card->devname);
			break;
		}

	}
	return 0;
}


/*============================================================================
 * Configure IP from SLARP negotiation
 * This adds dynamic routes when SLARP has provided valid addresses
 */

static int configure_ip (sdla_t* card)
{
	struct net_device *dev = card->wandev.dev;
        chdlc_private_area_t *chdlc_priv_area;
        char err;

	if (!dev)
		return 0;

	chdlc_priv_area = dev->priv;
	
	
        /* set to discover */
        if(card->u.c.slarp_timer != 0x00) {
		CHDLC_MAILBOX_STRUCT* mb = card->mbox;
		CHDLC_CONFIGURATION_STRUCT *cfg;

     		mb->buffer_length = 0;
		mb->command = READ_CHDLC_CONFIGURATION;
		err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	
		if(err != COMMAND_OK) {
			chdlc_error(card,err,mb);
			return -1;
		}

		cfg = (CHDLC_CONFIGURATION_STRUCT *)mb->data;
                chdlc_priv_area->IP_address = cfg->IP_address;
                chdlc_priv_area->IP_netmask = cfg->IP_netmask;

		/* Set flag to add route */
		chdlc_priv_area->route_status = ADD_ROUTE;

		/* The idea here is to add the route in the poll routine.
	   	This way, we aren't in interrupt context when adding routes */
		trigger_chdlc_poll(dev);
        }

	return 0;
}


/*============================================================================
 * Un-Configure IP negotiated by SLARP
 * This removes dynamic routes when the link becomes inactive.
 */

static int unconfigure_ip (sdla_t* card)
{
	struct net_device *dev = card->wandev.dev;
	chdlc_private_area_t *chdlc_priv_area;

	if (!dev)
		return 0;

	chdlc_priv_area= dev->priv;
	
	if (chdlc_priv_area->route_status == ROUTE_ADDED) {

		/* Note: If this function is called, the 
                 * port state has been DISCONNECTED.  This state
                 * change will trigger a poll_disconnected 
                 * function, that will check for this condition. 
		 */
		chdlc_priv_area->route_status = REMOVE_ROUTE;

	}
	return 0;
}

/*============================================================================
 * Routine to add/remove routes 
 * Called like a polling routine when Routes are flagged to be added/removed.
 */

static void process_route (sdla_t *card)
{
        struct net_device *dev = card->wandev.dev;
        unsigned char port_num;
        chdlc_private_area_t *chdlc_priv_area = NULL;
	u32 local_IP_addr = 0;
	u32 remote_IP_addr = 0;
	u32 IP_netmask, IP_addr;
        int err = 0;
	struct in_device *in_dev;
	mm_segment_t fs;
	struct ifreq if_info;
        struct sockaddr_in *if_data1, *if_data2;
	
        chdlc_priv_area = dev->priv;
        port_num = card->u.c.comm_port;

	/* Bug Fix Mar 16 2000
	 * AND the IP address to the Mask before checking
         * the last two bits. */

	if((chdlc_priv_area->route_status == ADD_ROUTE) &&
		((chdlc_priv_area->IP_address & ~chdlc_priv_area->IP_netmask) > 2)) {

		printk(KERN_INFO "%s: Dynamic route failure.\n",card->devname);

                if(card->u.c.slarp_timer) {
			u32 addr_net = htonl(chdlc_priv_area->IP_address);

			printk(KERN_INFO "%s: Bad IP address %u.%u.%u.%u received\n",
				card->devname,
			       NIPQUAD(addr_net));
                        printk(KERN_INFO "%s: from remote station.\n",
				card->devname);

                }else{ 
			u32 addr_net = htonl(chdlc_priv_area->IP_address);

                        printk(KERN_INFO "%s: Bad IP address %u.%u.%u.%u issued\n",
			       card->devname,
			       NIPQUAD(addr_net));
                        printk(KERN_INFO "%s: to remote station. Local\n",
				card->devname);
			printk(KERN_INFO "%s: IP address must be A.B.C.1\n",
				card->devname);
			printk(KERN_INFO "%s: or A.B.C.2.\n",card->devname);
		}

		/* remove the route due to the IP address error condition */
		chdlc_priv_area->route_status = REMOVE_ROUTE;
		err = 1;
   	}

	/* If we are removing a route with bad IP addressing, then use the */
	/* locally configured IP addresses */
        if((chdlc_priv_area->route_status == REMOVE_ROUTE) && err) {

 	        /* do not remove a bad route that has already been removed */
        	if(chdlc_priv_area->route_removed) {
	                return;
        	}

                in_dev = dev->ip_ptr;

                if(in_dev != NULL) {
                        struct in_ifaddr *ifa = in_dev->ifa_list;
                        if (ifa != NULL ) {
                                local_IP_addr = ifa->ifa_local;
                                IP_netmask  = ifa->ifa_mask;
                        }
                }
	}else{ 
       		/* According to Cisco HDLC, if the point-to-point address is
		   A.B.C.1, then we are the opposite (A.B.C.2), and vice-versa.
		*/
		IP_netmask = ntohl(chdlc_priv_area->IP_netmask);
	        remote_IP_addr = ntohl(chdlc_priv_area->IP_address);
	

		/* If Netmask is 255.255.255.255 the local address
                 * calculation will fail. Default it back to 255.255.255.0 */
		if (IP_netmask == 0xffffffff)
			IP_netmask &= 0x00ffffff;

		/* Bug Fix Mar 16 2000
		 * AND the Remote IP address with IP netmask, instead
                 * of static netmask of 255.255.255.0 */
        	local_IP_addr = (remote_IP_addr & IP_netmask) +
                	(~remote_IP_addr & ntohl(0x0003));

	        if(!card->u.c.slarp_timer) {
			IP_addr = local_IP_addr;
			local_IP_addr = remote_IP_addr;
			remote_IP_addr = IP_addr;
       		}
	}

        fs = get_fs();                  /* Save file system  */
        set_fs(get_ds());               /* Get user space block */

        /* Setup a structure for adding/removing routes */
        memset(&if_info, 0, sizeof(if_info));
        strcpy(if_info.ifr_name, dev->name);

	switch (chdlc_priv_area->route_status) {

	case ADD_ROUTE:

		if(!card->u.c.slarp_timer) {
			if_data2 = (struct sockaddr_in *)&if_info.ifr_dstaddr;
			if_data2->sin_addr.s_addr = remote_IP_addr;
			if_data2->sin_family = AF_INET;
			err = devinet_ioctl(SIOCSIFDSTADDR, &if_info);
		} else { 
			if_data1 = (struct sockaddr_in *)&if_info.ifr_addr;
			if_data1->sin_addr.s_addr = local_IP_addr;
			if_data1->sin_family = AF_INET;
			if(!(err = devinet_ioctl(SIOCSIFADDR, &if_info))){
				if_data2 = (struct sockaddr_in *)&if_info.ifr_dstaddr;
				if_data2->sin_addr.s_addr = remote_IP_addr;
				if_data2->sin_family = AF_INET;
				err = devinet_ioctl(SIOCSIFDSTADDR, &if_info);
			}
		}

               if(err) {
			printk(KERN_INFO "%s: Add route %u.%u.%u.%u failed (%d)\n", 
				card->devname, NIPQUAD(remote_IP_addr), err);
		} else {
			((chdlc_private_area_t *)dev->priv)->route_status = ROUTE_ADDED;
			printk(KERN_INFO "%s: Dynamic route added.\n",
				card->devname);
			printk(KERN_INFO "%s:    Local IP addr : %u.%u.%u.%u\n",
				card->devname, NIPQUAD(local_IP_addr));
			printk(KERN_INFO "%s:    Remote IP addr: %u.%u.%u.%u\n",
				card->devname, NIPQUAD(remote_IP_addr));
			chdlc_priv_area->route_removed = 0;
		}
		break;


	case REMOVE_ROUTE:
	
		/* Change the local ip address of the interface to 0.
		 * This will also delete the destination route.
		 */
		if(!card->u.c.slarp_timer) {
			if_data2 = (struct sockaddr_in *)&if_info.ifr_dstaddr;
			if_data2->sin_addr.s_addr = 0;
			if_data2->sin_family = AF_INET;
			err = devinet_ioctl(SIOCSIFDSTADDR, &if_info);
		} else {
			if_data1 = (struct sockaddr_in *)&if_info.ifr_addr;
			if_data1->sin_addr.s_addr = 0;
			if_data1->sin_family = AF_INET;
			err = devinet_ioctl(SIOCSIFADDR,&if_info);
		
		}
		if(err) {
			printk(KERN_INFO
				"%s: Remove route %u.%u.%u.%u failed, (err %d)\n",
					card->devname, NIPQUAD(remote_IP_addr),
					err);
		} else {
			((chdlc_private_area_t *)dev->priv)->route_status =
				NO_ROUTE;
                        printk(KERN_INFO "%s: Dynamic route removed: %u.%u.%u.%u\n",
                                        card->devname, NIPQUAD(local_IP_addr)); 
			chdlc_priv_area->route_removed = 1;
		}
		break;
	}

        set_fs(fs);                     /* Restore file system */

}


/*=============================================================================
 * Store a UDP management packet for later processing.
 */

static int store_udp_mgmt_pkt(char udp_pkt_src, sdla_t* card,
			      struct sk_buff *skb, struct net_device* dev,
			      chdlc_private_area_t* chdlc_priv_area)
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

	if(udp_pkt_src == UDP_PKT_FRM_STACK){
		dev_kfree_skb_any(skb);
	}else{
                dev_kfree_skb_any(skb);
	}
		
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

	if(chdlc_priv_area->udp_pkt_src == UDP_PKT_FRM_NETWORK){

		/* Only these commands are support for remote debugging.
		 * All others are not */
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
			chdlc_udp_pkt->cblock.buffer_length = 2;
			mb->buffer_length = 2;
			break;

		case CPIPE_ROUTER_UP_TIME:
			do_gettimeofday( &tv );
			chdlc_priv_area->router_up_time = tv.tv_sec - 
					chdlc_priv_area->router_start_time;
			*(unsigned long *)&chdlc_udp_pkt->data = 
					chdlc_priv_area->router_up_time;	
			mb->buffer_length = sizeof(unsigned long);
			chdlc_udp_pkt->cblock.buffer_length = sizeof(unsigned long);
			chdlc_udp_pkt->cblock.return_code = COMMAND_OK;
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
			goto dflt_1;

		default:
dflt_1:
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
	

     	if(chdlc_priv_area->udp_pkt_src == UDP_PKT_FRM_NETWORK){

		/* Must check if we interrupted if_send() routine. The
		 * tx buffers might be used. If so drop the packet */
	   	if (!test_bit(SEND_CRIT,&card->wandev.critical)) {
		
			if(!chdlc_send(card, chdlc_priv_area->udp_pkt_data, len)) {
				++ card->wandev.stats.tx_packets;
				card->wandev.stats.tx_bytes += len;
			}
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

static void init_chdlc_tx_rx_buff( sdla_t* card)
{
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;
	CHDLC_TX_STATUS_EL_CFG_STRUCT *tx_config;
	CHDLC_RX_STATUS_EL_CFG_STRUCT *rx_config;
	char err;
	
	mb->buffer_length = 0;
	mb->command = READ_CHDLC_CONFIGURATION;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;

	if(err != COMMAND_OK) {
		if (card->wandev.dev){
			chdlc_error(card,err,mb);
		}
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
	
	err = chdlc_set_intr_mode(card, APP_INT_ON_COMMAND_COMPLETE);

	if (err == CMD_OK) { 
		for (i = 0; i < MAX_INTR_TEST_COUNTER; i ++) {	
			mb->buffer_length  = 0;
			mb->command = READ_CHDLC_CODE_VERSION;
			err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
			if (err != CMD_OK) 
				chdlc_error(card, err, mb);
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

#ifdef _WAN_UDP_DEBUG
		printk(KERN_INFO "SIG %s = %s\n\
				  UPP %x = %x\n\
				  PRT %x = %x\n\
				  REQ %i = %i\n\
				  36 th = %x 37th = %x\n",
				  chdlc_udp_pkt->wp_mgmt.signature,
				  UDPMGMT_SIGNATURE,
				  chdlc_udp_pkt->udp_pkt.udp_dst_port,
				  ntohs(card->wandev.udp_port),
				  chdlc_udp_pkt->ip_pkt.protocol,
				  UDPMGMT_UDP_PROTOCOL,
				  chdlc_udp_pkt->wp_mgmt.request_reply,
				  UDPMGMT_REQUEST,
				  skb->data[36], skb->data[37]);
#endif	
		
	if (!strncmp(chdlc_udp_pkt->wp_mgmt.signature,UDPMGMT_SIGNATURE,8) &&
	   (chdlc_udp_pkt->udp_pkt.udp_dst_port == ntohs(card->wandev.udp_port)) &&
	   (chdlc_udp_pkt->ip_pkt.protocol == UDPMGMT_UDP_PROTOCOL) &&
	   (chdlc_udp_pkt->wp_mgmt.request_reply == UDPMGMT_REQUEST)) {

		return UDP_CPIPE_TYPE;

	}else{ 
		return UDP_INVALID_TYPE;
	}
}

/*============================================================================
 * Set PORT state.
 */
static void port_set_state (sdla_t *card, int state)
{
        if (card->u.c.state != state)
        {
                switch (state)
                {
                case WAN_CONNECTED:
                        printk (KERN_INFO "%s: Link connected!\n",
                                card->devname);
                      	break;

                case WAN_CONNECTING:
                        printk (KERN_INFO "%s: Link connecting...\n",
                                card->devname);
                        break;

                case WAN_DISCONNECTED:
                        printk (KERN_INFO "%s: Link disconnected!\n",
                                card->devname);
                        break;
                }

                card->wandev.state = card->u.c.state = state;
		if (card->wandev.dev){
			struct net_device *dev = card->wandev.dev;
			chdlc_private_area_t *chdlc_priv_area = dev->priv;
			chdlc_priv_area->common.state = state;
		}
        }
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
	chdlc_private_area_t *chdlc_priv_area = dev->priv;
	SHARED_MEMORY_INFO_STRUCT *flags = card->u.c.flags;

	if (card->u.c.comm_enabled){

		/* Jun 20. 2000: NC
		 * IP addresses are not used in the API mode */
		
		if ((chdlc_priv_area->ip_local_tmp != chdlc_priv_area->ip_local ||
		     chdlc_priv_area->ip_remote_tmp != chdlc_priv_area->ip_remote) && 
		     card->u.c.usedby == WANPIPE) {
			
			/* The IP addersses have changed, we must
                         * stop the communications and reconfigure
                         * the card. Reason: the firmware must know
                         * the local and remote IP addresses. */
			disable_comm(card);
			port_set_state(card, WAN_DISCONNECTED);
			printk(KERN_INFO 
				"%s: IP addresses changed!\n",
					card->devname);
			printk(KERN_INFO 
				"%s: Restarting communications ...\n",
					card->devname);
		}else{ 
			/* IP addresses are the same and the link is up, 
                         * we don't have to do anything here. Therefore, exit */
			return 0;
		}
	}

	chdlc_priv_area->ip_local = chdlc_priv_area->ip_local_tmp;
	chdlc_priv_area->ip_remote = chdlc_priv_area->ip_remote_tmp;


	/* Setup the Board for asynchronous mode */
	if (card->u.c.async_mode){
		
		if (set_asy_config(card)) {
			printk (KERN_INFO "%s: Failed CHDLC Async configuration!\n",
				card->devname);
			return 0;
		}
	}else{
		/* Setup the Board for CHDLC */
		if (set_chdlc_config(card)) {
			printk (KERN_INFO "%s: Failed CHDLC configuration!\n",
				card->devname);
			return 0;
		}
	}

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

	/* In TTY mode, receive interrupt will be enabled during
	 * wanpipe_tty_open() operation */
	if (card->tty_opt){
		flags->interrupt_info_struct.interrupt_permission &= ~APP_INT_ON_RX_FRAME;
	}

	/* Enable communications */
 	if (card->u.c.async_mode){
		if (asy_comm_enable(card) != 0) {
			printk(KERN_INFO "%s: Failed to enable async commnunication!\n",
					card->devname);
			flags->interrupt_info_struct.interrupt_permission = 0;
			card->u.c.comm_enabled=0;
			chdlc_set_intr_mode(card,0);
			return 0;
		}
        }else{ 
		if (chdlc_comm_enable(card) != 0) {
			printk(KERN_INFO "%s: Failed to enable chdlc communications!\n",
					card->devname);
			flags->interrupt_info_struct.interrupt_permission = 0;
			card->u.c.comm_enabled=0;
			chdlc_set_intr_mode(card,0);
			return 0;
		}
	}

	/* Initialize Rx/Tx buffer control fields */
	init_chdlc_tx_rx_buff(card);
	port_set_state(card, WAN_CONNECTING);
	return 0; 
}


/*============================================================
 * chdlc_poll
 *	
 * Rationale:
 * 	We cannot manipulate the routing tables, or
 *      ip addresses withing the interrupt. Therefore
 *      we must perform such actons outside an interrupt 
 *      at a later time. 
 *
 * Description:	
 *	CHDLC polling routine, responsible for 
 *     	shutting down interfaces upon disconnect
 *     	and adding/removing routes. 
 *      
 * Usage:        
 * 	This function is executed for each CHDLC  
 * 	interface through a tq_schedule bottom half.
 *      
 *      trigger_chdlc_poll() function is used to kick
 *      the chldc_poll routine.  
 */

static void chdlc_poll(struct net_device *dev)
{
	chdlc_private_area_t *chdlc_priv_area;
	sdla_t *card;
	u8 check_gateway=0;	
	SHARED_MEMORY_INFO_STRUCT* flags;

	
	if (!dev || (chdlc_priv_area=dev->priv) == NULL)
		return;

	card = chdlc_priv_area->card;
	flags = card->u.c.flags;
	
	/* (Re)Configuraiton is in progress, stop what you are 
	 * doing and get out */
	if (test_bit(PERI_CRIT,&card->wandev.critical)){
		clear_bit(POLL_CRIT,&card->wandev.critical);
		return;
	}
	
	/* if_open() function has triggered the polling routine
	 * to determine the configured IP addresses.  Once the
	 * addresses are found, trigger the chdlc configuration */
	if (test_bit(0,&chdlc_priv_area->config_chdlc)){

		chdlc_priv_area->ip_local_tmp  = get_ip_address(dev,WAN_LOCAL_IP);
		chdlc_priv_area->ip_remote_tmp = get_ip_address(dev,WAN_POINTOPOINT_IP);
	
	       /* Jun 20. 2000 Bug Fix
	 	* Only perform this check in WANPIPE mode, since
	 	* IP addresses are not used in the API mode. */
	
		if (chdlc_priv_area->ip_local_tmp == chdlc_priv_area->ip_remote_tmp && 
		    card->u.c.slarp_timer == 0x00 && 
		    !card->u.c.backup && 
		    card->u.c.usedby == WANPIPE){

			if (++chdlc_priv_area->ip_error > MAX_IP_ERRORS){
				printk(KERN_INFO "\n%s: --- WARNING ---\n",
						card->devname);
				printk(KERN_INFO 
				"%s: The local IP address is the same as the\n",
						card->devname);
				printk(KERN_INFO 
				"%s: Point-to-Point IP address.\n",
						card->devname);
				printk(KERN_INFO "%s: --- WARNING ---\n\n",
						card->devname);
			}else{
				clear_bit(POLL_CRIT,&card->wandev.critical);
				chdlc_priv_area->poll_delay_timer.expires = jiffies+HZ;
				add_timer(&chdlc_priv_area->poll_delay_timer);
				return;
			}
		}

		clear_bit(0,&chdlc_priv_area->config_chdlc);
		clear_bit(POLL_CRIT,&card->wandev.critical);
		
		chdlc_priv_area->timer_int_enabled |= TMR_INT_ENABLED_CONFIG;
		flags->interrupt_info_struct.interrupt_permission |= APP_INT_ON_TIMER;
		return;
	}
	/* Dynamic interface implementation, as well as dynamic
	 * routing.  */
	
	switch (card->u.c.state){

	case WAN_DISCONNECTED:

		/* If the dynamic interface configuration is on, and interface 
		 * is up, then bring down the netowrk interface */
		
		if (test_bit(DYN_OPT_ON,&chdlc_priv_area->interface_down) && 
		    !test_bit(DEV_DOWN,  &chdlc_priv_area->interface_down) &&		
		    card->wandev.dev->flags & IFF_UP){	

			printk(KERN_INFO "%s: Interface %s down.\n",
				card->devname,card->wandev.dev->name);
			change_dev_flags(card->wandev.dev,(card->wandev.dev->flags&~IFF_UP));
			set_bit(DEV_DOWN,&chdlc_priv_area->interface_down);
			chdlc_priv_area->route_status = NO_ROUTE;

		}else{
			/* We need to check if the local IP address is
               	  	 * zero. If it is, we shouldn't try to remove it.
                 	 */

			if (card->wandev.dev->flags & IFF_UP && 
		    	    get_ip_address(card->wandev.dev,WAN_LOCAL_IP) && 
		    	    chdlc_priv_area->route_status != NO_ROUTE &&
			    card->u.c.slarp_timer){

				process_route(card);
			}
		}
		break;

	case WAN_CONNECTED:

		/* In SMP machine this code can execute before the interface
		 * comes up.  In this case, we must make sure that we do not
		 * try to bring up the interface before dev_open() is finished */


		/* DEV_DOWN will be set only when we bring down the interface
		 * for the very first time. This way we know that it was us
		 * that brought the interface down */
		
		if (test_bit(DYN_OPT_ON,&chdlc_priv_area->interface_down) &&
		    test_bit(DEV_DOWN,  &chdlc_priv_area->interface_down) &&
		    !(card->wandev.dev->flags & IFF_UP)){
			
			printk(KERN_INFO "%s: Interface %s up.\n",
				card->devname,card->wandev.dev->name);
			change_dev_flags(card->wandev.dev,(card->wandev.dev->flags|IFF_UP));
			clear_bit(DEV_DOWN,&chdlc_priv_area->interface_down);
			check_gateway=1;
		}

		if (chdlc_priv_area->route_status == ADD_ROUTE && 
		    card->u.c.slarp_timer){ 

			process_route(card);
			check_gateway=1;
		}

		if (chdlc_priv_area->gateway && check_gateway)
			add_gateway(card,dev);

		break;
	}	

	clear_bit(POLL_CRIT,&card->wandev.critical);
}

/*============================================================
 * trigger_chdlc_poll
 *
 * Description:
 * 	Add a chdlc_poll() work entry into the keventd work queue
 *      for a specific dlci/interface.  This will kick
 *      the fr_poll() routine at a later time. 
 *
 * Usage:
 * 	Interrupts use this to defer a taks to 
 *      a polling routine.
 *
 */	
static void trigger_chdlc_poll(struct net_device *dev)
{
	chdlc_private_area_t *chdlc_priv_area;
	sdla_t *card;

	if (!dev)
		return;
	
	if ((chdlc_priv_area = dev->priv)==NULL)
		return;

	card = chdlc_priv_area->card;
	
	if (test_and_set_bit(POLL_CRIT,&card->wandev.critical)){
		return;
	}
	if (test_bit(PERI_CRIT,&card->wandev.critical)){
		return; 
	}
	schedule_work(&chdlc_priv_area->poll_work);
}


static void chdlc_poll_delay (unsigned long dev_ptr)
{
	struct net_device *dev = (struct net_device *)dev_ptr;
	trigger_chdlc_poll(dev);
}


void s508_lock (sdla_t *card, unsigned long *smp_flags)
{
	spin_lock_irqsave(&card->wandev.lock, *smp_flags);
        if (card->next){
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

//*********** TTY SECTION ****************

static void wanpipe_tty_trigger_tx_irq(sdla_t *card)
{
	SHARED_MEMORY_INFO_STRUCT *flags = card->u.c.flags;
	INTERRUPT_INFORMATION_STRUCT *chdlc_int = &flags->interrupt_info_struct;
	chdlc_int->interrupt_permission |= APP_INT_ON_TX_FRAME;
}

static void wanpipe_tty_trigger_poll(sdla_t *card)
{
	schedule_work(&card->tty_work);
}

static void tty_poll_work (void* data)
{
	sdla_t *card = (sdla_t*)data;
	struct tty_struct *tty;

	if ((tty=card->tty)==NULL)
		return;
	
	tty_wakeup(tty);
#if defined(SERIAL_HAVE_POLL_WAIT)
	wake_up_interruptible(&tty->poll_wait);
#endif	
	return;
}

static void wanpipe_tty_close(struct tty_struct *tty, struct file * filp)
{
	sdla_t *card;
	unsigned long smp_flags;
	
	if (!tty || !tty->driver_data){
		return;
	}
	
	card = (sdla_t*)tty->driver_data;
	
	if (!card)
		return;

	printk(KERN_INFO "%s: Closing TTY Driver!\n",
			card->devname);

	/* Sanity Check */
	if (!card->tty_open)
		return;
	
	wanpipe_close(card);
	if (--card->tty_open == 0){

		lock_adapter_irq(&card->wandev.lock,&smp_flags);	
		card->tty=NULL;
		chdlc_disable_comm_shutdown(card);
		unlock_adapter_irq(&card->wandev.lock,&smp_flags);

		kfree(card->tty_buf);
		card->tty_buf = NULL;			
		kfree(card->tty_rx);
		card->tty_rx = NULL;
	}
	return;
}
static int wanpipe_tty_open(struct tty_struct *tty, struct file * filp)
{
	unsigned long smp_flags;
	sdla_t *card;
	
	if (!tty){
		return -ENODEV;
	}
	
	if (!tty->driver_data){
		int port;
		port = tty->index;
		if ((port < 0) || (port >= NR_PORTS)) 
			return -ENODEV;
		
		tty->driver_data = WAN_CARD(port);
		if (!tty->driver_data)
			return -ENODEV;
	}

	card = (sdla_t*)tty->driver_data;

	if (!card){
		lock_adapter_irq(&card->wandev.lock,&smp_flags);	
		card->tty=NULL;
		unlock_adapter_irq(&card->wandev.lock,&smp_flags);
		return -ENODEV;
	}

	printk(KERN_INFO "%s: Opening TTY Driver!\n",
			card->devname);

	if (card->tty_open == 0){
		lock_adapter_irq(&card->wandev.lock,&smp_flags);	
		card->tty=tty;
		unlock_adapter_irq(&card->wandev.lock,&smp_flags);

		if (!card->tty_buf){
			card->tty_buf = kmalloc(TTY_CHDLC_MAX_MTU, GFP_KERNEL);
			if (!card->tty_buf){
				card->tty_buf=NULL;
				card->tty=NULL;
				return -ENOMEM;	
			}
		}

		if (!card->tty_rx){
			card->tty_rx = kmalloc(TTY_CHDLC_MAX_MTU, GFP_KERNEL);
			if (!card->tty_rx){
				/* Free the buffer above */
				kfree(card->tty_buf);
				card->tty_buf=NULL;
				card->tty=NULL;
				return -ENOMEM;	
			}
		}
	}

	++card->tty_open;
	wanpipe_open(card);
	return 0;
}

static int wanpipe_tty_write(struct tty_struct * tty, const unsigned char *buf, int count)
{
	unsigned long smp_flags=0;
	sdla_t *card=NULL;

	if (!tty){
		dbg_printk(KERN_INFO "NO TTY in Write\n");
		return -ENODEV;
	}

	card = (sdla_t *)tty->driver_data;
			
	if (!card){
		dbg_printk(KERN_INFO "No Card in TTY Write\n");
		return -ENODEV;
	}	

	if (count > card->wandev.mtu){
		dbg_printk(KERN_INFO "Frame too big in Write %i Max: %i\n",
				count,card->wandev.mtu);
		return -EINVAL;
	}
	
	if (card->wandev.state != WAN_CONNECTED){
		dbg_printk(KERN_INFO "Card not connected in TTY Write\n");
		return -EINVAL;
	}

	/* Lock the 508 Card: SMP is supported */
      	if(card->hw.type != SDLA_S514){
		s508_lock(card,&smp_flags);
	} 
	
	if (test_and_set_bit(SEND_CRIT,(void*)&card->wandev.critical)){
		printk(KERN_INFO "%s: Critical in TTY Write\n",
				card->devname);
		
		/* Lock the 508 Card: SMP is supported */
		if(card->hw.type != SDLA_S514)
			s508_unlock(card,&smp_flags);
		
		return -EINVAL; 
	}
	
 	if (chdlc_send(card,(void*)buf,count)){
		dbg_printk(KERN_INFO "%s: Failed to send, retry later: kernel!\n",
				card->devname);
		clear_bit(SEND_CRIT,(void*)&card->wandev.critical);

		wanpipe_tty_trigger_tx_irq(card);
		
		if(card->hw.type != SDLA_S514)
			s508_unlock(card,&smp_flags);
		return 0;
	}
	dbg_printk(KERN_INFO "%s: Packet sent OK: %i\n",card->devname,count);
	clear_bit(SEND_CRIT,(void*)&card->wandev.critical);
	
	if(card->hw.type != SDLA_S514)
		s508_unlock(card,&smp_flags);

	return count;
}

static void wanpipe_tty_receive(sdla_t *card, unsigned addr, unsigned int len)
{
	unsigned offset=0;
	unsigned olen=len;
	char fp=0;
	struct tty_struct *tty;
	int i;
	struct tty_ldisc *ld;
	
	if (!card->tty_open){
		dbg_printk(KERN_INFO "%s: TTY not open during receive\n",
				card->devname);
		return;
	}
	
	if ((tty=card->tty) == NULL){
		dbg_printk(KERN_INFO "%s: No TTY on receive\n",
				card->devname);
		return;
	}
	
	if (!tty->driver_data){
		dbg_printk(KERN_INFO "%s: No Driver Data, or Flip on receive\n",
				card->devname);
		return;
	}
	

	if (card->u.c.async_mode){
		if ((tty->flip.count+len) >= TTY_FLIPBUF_SIZE){
			if (net_ratelimit()){
				printk(KERN_INFO 
					"%s: Received packet size too big: %i bytes, Max: %i!\n",
					card->devname,len,TTY_FLIPBUF_SIZE);
			}
			return;
		}

		
		if((addr + len) > card->u.c.rx_top + 1) {
			offset = card->u.c.rx_top - addr + 1;
			
			sdla_peek(&card->hw, addr, tty->flip.char_buf_ptr, offset);
			
			addr = card->u.c.rx_base;
			len -= offset;
			
			tty->flip.char_buf_ptr+=offset;
			tty->flip.count+=offset;
			for (i=0;i<offset;i++){
				*tty->flip.flag_buf_ptr = 0;
				tty->flip.flag_buf_ptr++;
			}
		}
		
		sdla_peek(&card->hw, addr, tty->flip.char_buf_ptr, len);
			
		tty->flip.char_buf_ptr+=len;
		card->tty->flip.count+=len;
		for (i=0;i<len;i++){
			*tty->flip.flag_buf_ptr = 0;
			tty->flip.flag_buf_ptr++;
		}

		tty->low_latency=1;
		tty_flip_buffer_push(tty);
	}else{
		if (!card->tty_rx){	
			if (net_ratelimit()){
				printk(KERN_INFO 
				"%s: Receive sync buffer not available!\n",
				 card->devname);
			}
			return;
		}
	
		if (len > TTY_CHDLC_MAX_MTU){
			if (net_ratelimit()){
				printk(KERN_INFO 
				"%s: Received packet size too big: %i bytes, Max: %i!\n",
					card->devname,len,TTY_FLIPBUF_SIZE);
			}
			return;
		}

		
		if((addr + len) > card->u.c.rx_top + 1) {
			offset = card->u.c.rx_top - addr + 1;
			
			sdla_peek(&card->hw, addr, card->tty_rx, offset);
			
			addr = card->u.c.rx_base;
			len -= offset;
		}
		sdla_peek(&card->hw, addr, card->tty_rx+offset, len);
		ld = tty_ldisc_ref(tty);
		if (ld) {
			if (ld->receive_buf)
				ld->receive_buf(tty,card->tty_rx,&fp,olen);
			tty_ldisc_deref(ld);
		}else{
			if (net_ratelimit()){
				printk(KERN_INFO 
					"%s: NO TTY Sync line discipline!\n",
					card->devname);
			}
		}
	}

	dbg_printk(KERN_INFO "%s: Received Data %i\n",card->devname,olen);
	return;
}

#if 0
static int wanpipe_tty_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}
#endif

static void wanpipe_tty_stop(struct tty_struct *tty)
{
	return;
}

static void wanpipe_tty_start(struct tty_struct *tty)
{
	return;
}

static int config_tty (sdla_t *card)
{
	SHARED_MEMORY_INFO_STRUCT *flags = card->u.c.flags;

	/* Setup the Board for asynchronous mode */
	if (card->u.c.async_mode){
		
		if (set_asy_config(card)) {
			printk (KERN_INFO "%s: Failed CHDLC Async configuration!\n",
				card->devname);
			return -EINVAL;
		}
	}else{
		/* Setup the Board for CHDLC */
		if (set_chdlc_config(card)) {
			printk (KERN_INFO "%s: Failed CHDLC configuration!\n",
				card->devname);
			return -EINVAL;
		}
	}

	/* Set interrupt mode and mask */
        if (chdlc_set_intr_mode(card, APP_INT_ON_RX_FRAME |
                		APP_INT_ON_GLOBAL_EXCEP_COND |
                		APP_INT_ON_TX_FRAME |
                		APP_INT_ON_CHDLC_EXCEP_COND | APP_INT_ON_TIMER)){
		printk (KERN_INFO "%s: Failed to set interrupt triggers!\n",
				card->devname);
		return -EINVAL;	
        }
	

	/* Mask the Transmit and Timer interrupt */
	flags->interrupt_info_struct.interrupt_permission &= 
		~(APP_INT_ON_TX_FRAME | APP_INT_ON_TIMER);

	
	/* Enable communications */
 	if (card->u.c.async_mode){
		if (asy_comm_enable(card) != 0) {
			printk(KERN_INFO "%s: Failed to enable async commnunication!\n",
					card->devname);
			flags->interrupt_info_struct.interrupt_permission = 0;
			card->u.c.comm_enabled=0;
			chdlc_set_intr_mode(card,0);
			return -EINVAL;
		}
        }else{ 
		if (chdlc_comm_enable(card) != 0) {
			printk(KERN_INFO "%s: Failed to enable chdlc communications!\n",
					card->devname);
			flags->interrupt_info_struct.interrupt_permission = 0;
			card->u.c.comm_enabled=0;
			chdlc_set_intr_mode(card,0);
			return -EINVAL;
		}
	}

	/* Initialize Rx/Tx buffer control fields */
	init_chdlc_tx_rx_buff(card);
	port_set_state(card, WAN_CONNECTING);
	return 0; 
}


static int change_speed(sdla_t *card, struct tty_struct *tty,
			 struct termios *old_termios)
{
	int	baud, ret=0;
	unsigned cflag; 
	int	dbits,sbits,parity,handshaking;

	cflag = tty->termios->c_cflag;

	/* There is always one stop bit */
	sbits=WANOPT_ONE;
	
	/* Parity is defaulted to NONE */
	parity = WANOPT_NONE;

	handshaking=0;
	
	/* byte size and parity */
	switch (cflag & CSIZE) {
	      case CS5: dbits = 5; break;
	      case CS6: dbits = 6; break;
	      case CS7: dbits = 7; break;
	      case CS8: dbits = 8; break;
	      /* Never happens, but GCC is too dumb to figure it out */
	      default:  dbits = 8; break;
	}
	
	/* One more stop bit should be supported, thus increment
	 * the number of stop bits Max=2 */
	if (cflag & CSTOPB) {
		sbits = WANOPT_TWO;
	}
	if (cflag & PARENB) {
		parity = WANOPT_EVEN;
	}
	if (cflag & PARODD){
		parity = WANOPT_ODD;
	}

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(tty);

	if (!baud)
		baud = 9600;	/* B0 transition handled in rs_set_termios */

	if (cflag & CRTSCTS) {
		handshaking|=ASY_RTS_HS_FOR_RX;
	}
	
	if (I_IGNPAR(tty))
		parity = WANOPT_NONE;

	if (I_IXOFF(tty)){
		handshaking|=ASY_XON_XOFF_HS_FOR_RX;
		handshaking|=ASY_XON_XOFF_HS_FOR_TX;
	}

	if (I_IXON(tty)){
		handshaking|=ASY_XON_XOFF_HS_FOR_RX;
		handshaking|=ASY_XON_XOFF_HS_FOR_TX;
	}

	if (card->u.c.async_mode){
		if (card->wandev.bps != baud)
			ret=1;
		card->wandev.bps = baud;
	}

	if (card->u.c.async_mode){
		if (card->u.c.protocol_options != handshaking)
			ret=1;
		card->u.c.protocol_options = handshaking;

		if (card->u.c.tx_bits_per_char != dbits)
			ret=1;
		card->u.c.tx_bits_per_char = dbits;

		if (card->u.c.rx_bits_per_char != dbits)
			ret=1;
		card->u.c.rx_bits_per_char = dbits;
		
		if (card->u.c.stop_bits != sbits)
			ret=1;
		card->u.c.stop_bits = sbits;

		if (card->u.c.parity != parity)
			ret=1;
		card->u.c.parity = parity;	

		card->u.c.break_timer = 50;
		card->u.c.inter_char_timer = 10;
		card->u.c.rx_complete_length = 100;
		card->u.c.xon_char = 0xFE;
	}else{
		card->u.c.protocol_options = HDLC_STREAMING_MODE;
	}
	
	return ret;
}

	
static void wanpipe_tty_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	sdla_t *card;
	int err=1;

	if (!tty){
		return;
	}

	card = (sdla_t *)tty->driver_data;
			
	if (!card)
		return;

	if (change_speed(card, tty, old_termios) || !card->u.c.comm_enabled){
		unsigned long smp_flags;
		
		if (card->u.c.comm_enabled){
			lock_adapter_irq(&card->wandev.lock,&smp_flags);
			chdlc_disable_comm_shutdown(card);
			unlock_adapter_irq(&card->wandev.lock,&smp_flags);
		}
		lock_adapter_irq(&card->wandev.lock,&smp_flags);
		err = config_tty(card);
		unlock_adapter_irq(&card->wandev.lock,&smp_flags);
		if (card->u.c.async_mode){
			printk(KERN_INFO "%s: TTY Async Configuration:\n"
				 "   Baud        =%i\n"
				 "   Handshaking =%s\n"
				 "   Tx Dbits    =%i\n"
				 "   Rx Dbits    =%i\n"
				 "   Parity      =%s\n"
				 "   Stop Bits   =%i\n",
				 card->devname,
				 card->wandev.bps,
				 opt_decode[card->u.c.protocol_options],
				 card->u.c.tx_bits_per_char,
				 card->u.c.rx_bits_per_char,
				 p_decode[card->u.c.parity] ,
				 card->u.c.stop_bits);
		}else{
			printk(KERN_INFO "%s: TTY Sync Configuration:\n"
				 "   Baud        =%i\n"
				 "   Protocol    =HDLC_STREAMING\n",
				 card->devname,card->wandev.bps);
		}
		if (!err){
			port_set_state(card,WAN_CONNECTED);
		}else{
			port_set_state(card,WAN_DISCONNECTED);
		}
	}
	return;
}

static void wanpipe_tty_put_char(struct tty_struct *tty, unsigned char ch)
{
	sdla_t *card;
	unsigned long smp_flags=0;

	if (!tty){
		return;
	}
	
	card = (sdla_t *)tty->driver_data;
			
	if (!card)
		return;

	if (card->wandev.state != WAN_CONNECTED)
		return;

	if(card->hw.type != SDLA_S514)
		s508_lock(card,&smp_flags);
	
	if (test_and_set_bit(SEND_CRIT,(void*)&card->wandev.critical)){
		
		wanpipe_tty_trigger_tx_irq(card);

		if(card->hw.type != SDLA_S514)
			s508_unlock(card,&smp_flags);
		return;
	}

	if (chdlc_send(card,(void*)&ch,1)){
		wanpipe_tty_trigger_tx_irq(card);
		dbg_printk("%s: Failed to TX char!\n",card->devname);
	}
	
	dbg_printk("%s: Char TX OK\n",card->devname);
	
	clear_bit(SEND_CRIT,(void*)&card->wandev.critical);
	
	if(card->hw.type != SDLA_S514)
		s508_unlock(card,&smp_flags);
	
	return;
}

static void wanpipe_tty_flush_chars(struct tty_struct *tty)
{
	return;
}

static void wanpipe_tty_flush_buffer(struct tty_struct *tty)
{
	if (!tty)
		return;
	
#if defined(SERIAL_HAVE_POLL_WAIT)
	wake_up_interruptible(&tty->poll_wait);
#endif
	tty_wakeup(tty);
	return;
}

/*
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 */
static void wanpipe_tty_send_xchar(struct tty_struct *tty, char ch)
{
	return;
}


static int wanpipe_tty_chars_in_buffer(struct tty_struct *tty)
{
	return 0;
}


static int wanpipe_tty_write_room(struct tty_struct *tty)
{
	sdla_t *card;

	printk(KERN_INFO "TTY Write Room\n");
	
	if (!tty){
		return 0;
	}

	card = (sdla_t *)tty->driver_data;
	if (!card)
		return 0;

	if (card->wandev.state != WAN_CONNECTED)
		return 0;
	
	return SEC_MAX_NO_DATA_BYTES_IN_FRAME;
}


static int set_modem_status(sdla_t *card, unsigned char data)
{
	CHDLC_MAILBOX_STRUCT *mb = card->mbox;
	int err;

	mb->buffer_length=1;
	mb->command=SET_MODEM_STATUS;
	mb->data[0]=data;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	if (err != COMMAND_OK) 
		chdlc_error (card, err, mb);
	
	return err;
}

static void wanpipe_tty_hangup(struct tty_struct *tty)
{
	sdla_t *card;
	unsigned long smp_flags;

	printk(KERN_INFO "TTY Hangup!\n");
	
	if (!tty){
		return;
	}

	card = (sdla_t *)tty->driver_data;
	if (!card)
		return;

	lock_adapter_irq(&card->wandev.lock,&smp_flags);
	set_modem_status(card,0);
	unlock_adapter_irq(&card->wandev.lock,&smp_flags);
	return;
}

static void wanpipe_tty_break(struct tty_struct *tty, int break_state)
{
	return;
}

static void wanpipe_tty_wait_until_sent(struct tty_struct *tty, int timeout)
{
	return;
}

static void wanpipe_tty_throttle(struct tty_struct * tty)
{
	return;
}

static void wanpipe_tty_unthrottle(struct tty_struct * tty)
{
	return;
}

int wanpipe_tty_read_proc(char *page, char **start, off_t off, int count,
		 int *eof, void *data)
{
	return 0;
}

/*
 * The serial driver boot-time initialization code!
 */
int wanpipe_tty_init(sdla_t *card)
{
	struct serial_state * state;
	
	/* Initialize the tty_driver structure */

	if (card->tty_minor < 0 || card->tty_minor > NR_PORTS){
		printk(KERN_INFO "%s: Illegal Minor TTY number (0-4): %i\n",
				card->devname,card->tty_minor);
		return -EINVAL;
	}

	if (WAN_CARD(card->tty_minor)){
		printk(KERN_INFO "%s: TTY Minor %i, already in use\n",
				card->devname,card->tty_minor);
		return -EBUSY;
	}

	if (tty_init_cnt==0){
		
		printk(KERN_INFO "%s: TTY %s Driver Init: Major %i, Minor Range %i-%i\n",
				card->devname,
				card->u.c.async_mode ? "ASYNC" : "SYNC",
				WAN_TTY_MAJOR,MIN_PORT,MAX_PORT);
		
		tty_driver_mode = card->u.c.async_mode;
		
		memset(&serial_driver, 0, sizeof(struct tty_driver));
		serial_driver.magic = TTY_DRIVER_MAGIC;
		serial_driver.owner = THIS_MODULE;
		serial_driver.driver_name = "wanpipe_tty"; 
		serial_driver.name = "ttyW";
		serial_driver.major = WAN_TTY_MAJOR;
		serial_driver.minor_start = WAN_TTY_MINOR;
		serial_driver.num = NR_PORTS; 
		serial_driver.type = TTY_DRIVER_TYPE_SERIAL;
		serial_driver.subtype = SERIAL_TYPE_NORMAL;
		
		serial_driver.init_termios = tty_std_termios;
		serial_driver.init_termios.c_cflag =
			B9600 | CS8 | CREAD | HUPCL | CLOCAL;
		serial_driver.flags = TTY_DRIVER_REAL_RAW;
		
		serial_driver.refcount = 1;	/* !@!@^#^&!! */

		serial_driver.open = wanpipe_tty_open;
		serial_driver.close = wanpipe_tty_close;
		serial_driver.write = wanpipe_tty_write;
		
		serial_driver.put_char = wanpipe_tty_put_char;
		serial_driver.flush_chars = wanpipe_tty_flush_chars;
		serial_driver.write_room = wanpipe_tty_write_room;
		serial_driver.chars_in_buffer = wanpipe_tty_chars_in_buffer;
		serial_driver.flush_buffer = wanpipe_tty_flush_buffer;
		//serial_driver.ioctl = wanpipe_tty_ioctl;
		serial_driver.throttle = wanpipe_tty_throttle;
		serial_driver.unthrottle = wanpipe_tty_unthrottle;
		serial_driver.send_xchar = wanpipe_tty_send_xchar;
		serial_driver.set_termios = wanpipe_tty_set_termios;
		serial_driver.stop = wanpipe_tty_stop;
		serial_driver.start = wanpipe_tty_start;
		serial_driver.hangup = wanpipe_tty_hangup;
		serial_driver.break_ctl = wanpipe_tty_break;
		serial_driver.wait_until_sent = wanpipe_tty_wait_until_sent;
		serial_driver.read_proc = wanpipe_tty_read_proc;
		
		if (tty_register_driver(&serial_driver)){
			printk(KERN_INFO "%s: Failed to register serial driver!\n",
					card->devname);
		}
	}


	/* The subsequent ports must comply to the initial configuration */
	if (tty_driver_mode != card->u.c.async_mode){
		printk(KERN_INFO "%s: Error: TTY Driver operation mode mismatch!\n",
				card->devname);
		printk(KERN_INFO "%s: The TTY driver is configured for %s!\n",
				card->devname, tty_driver_mode ? "ASYNC" : "SYNC");
		return -EINVAL;
	}
	
	tty_init_cnt++;
	
	printk(KERN_INFO "%s: Initializing TTY %s Driver Minor %i\n",
			card->devname,
			tty_driver_mode ? "ASYNC" : "SYNC",
			card->tty_minor);
	
	tty_card_map[card->tty_minor] = card;
	state = &rs_table[card->tty_minor];
	
	state->magic = SSTATE_MAGIC;
	state->line = 0;
	state->type = PORT_UNKNOWN;
	state->custom_divisor = 0;
	state->close_delay = 5*HZ/10;
	state->closing_wait = 30*HZ;
	state->icount.cts = state->icount.dsr = 
		state->icount.rng = state->icount.dcd = 0;
	state->icount.rx = state->icount.tx = 0;
	state->icount.frame = state->icount.parity = 0;
	state->icount.overrun = state->icount.brk = 0;
	state->irq = card->wandev.irq; 

	INIT_WORK(&card->tty_work, tty_poll_work, (void*)card);
	return 0;
}


MODULE_LICENSE("GPL");

/****** End ****************************************************************/

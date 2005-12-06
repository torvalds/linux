/*****************************************************************************
* sdla_x25.c	WANPIPE(tm) Multiprotocol WAN Link Driver.  X.25 module.
*
* Author:	Nenad Corbic	<ncorbic@sangoma.com>
*
* Copyright:	(c) 1995-2001 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Apr 03, 2001  Nenad Corbic	 o Fixed the rx_skb=NULL bug in x25 in rx_intr().
* Dec 26, 2000  Nenad Corbic	 o Added a new polling routine, that uses
*                                  a kernel timer (more efficient).
* Dec 25, 2000  Nenad Corbic	 o Updated for 2.4.X kernel
* Jul 26, 2000  Nenad Corbic	 o Increased the local packet buffering
* 				   for API to 4096+header_size. 
* Jul 17, 2000  Nenad Corbic	 o Fixed the x25 startup bug. Enable 
* 				   communications only after all interfaces
* 				   come up.  HIGH SVC/PVC is used to calculate
* 				   the number of channels.
*                                  Enable protocol only after all interfaces
*                                  are enabled.
* Jul 10, 2000	Nenad Corbic	 o Fixed the M_BIT bug. 
* Apr 25, 2000  Nenad Corbic	 o Pass Modem messages to the API.
*                                  Disable idle timeout in X25 API.
* Apr 14, 2000  Nenad Corbic	 o Fixed: Large LCN number support.
*                                  Maximum LCN number is 4095.
*                                  Maximum number of X25 channels is 255.
* Apr 06, 2000  Nenad Corbic	 o Added SMP Support.
* Mar 29, 2000  Nenad Corbic	 o Added support for S514 PCI Card
* Mar 23, 2000  Nenad Corbic	 o Improved task queue, BH handling.
* Mar 14, 2000  Nenad Corbic  	 o Updated Protocol Violation handling
*                                  routines.  Bug Fix.
* Mar 10, 2000  Nenad Corbic	 o Bug Fix: corrupted mbox recovery.
* Mar 09, 2000  Nenad Corbic     o Fixed the auto HDLC bug.
* Mar 08, 2000	Nenad Corbic     o Fixed LAPB HDLC startup problems.
*                                  Application must bring the link up 
*                                  before tx/rx, and bring the 
*                                  link down on close().
* Mar 06, 2000	Nenad Corbic	 o Added an option for logging call setup 
*                                  information. 
* Feb 29, 2000  Nenad Corbic 	 o Added support for LAPB HDLC API
* Feb 25, 2000  Nenad Corbic     o Fixed the modem failure handling.
*                                  No Modem OOB message will be passed 
*                                  to the user.
* Feb 21, 2000  Nenad Corbic 	 o Added Xpipemon Debug Support
* Dec 30, 1999 	Nenad Corbic	 o Socket based X25API 
* Sep 17, 1998	Jaspreet Singh	 o Updates for 2.2.X  kernel
* Mar 15, 1998	Alan Cox	 o 2.1.x porting
* Dec 19, 1997	Jaspreet Singh	 o Added multi-channel IPX support
* Nov 27, 1997	Jaspreet Singh	 o Added protection against enabling of irqs
*				   when they are disabled.
* Nov 17, 1997  Farhan Thawar    o Added IPX support
*				 o Changed if_send() to now buffer packets when
*				   the board is busy
*				 o Removed queueing of packets via the polling
*				   routing
*				 o Changed if_send() critical flags to properly
*				   handle race conditions
* Nov 06, 1997  Farhan Thawar    o Added support for SVC timeouts
*				 o Changed PVC encapsulation to ETH_P_IP
* Jul 21, 1997  Jaspreet Singh	 o Fixed freeing up of buffers using kfree()
*				   when packets are received.
* Mar 11, 1997  Farhan Thawar   Version 3.1.1
*                                o added support for V35
*                                o changed if_send() to return 0 if
*                                  wandev.critical() is true
*                                o free socket buffer in if_send() if
*                                  returning 0
*                                o added support for single '@' address to
*                                  accept all incoming calls
*                                o fixed bug in set_chan_state() to disconnect
* Jan 15, 1997	Gene Kozin	Version 3.1.0
*				 o implemented exec() entry point
* Jan 07, 1997	Gene Kozin	Initial version.
*****************************************************************************/

/*======================================================
 * 	Includes 
 *=====================================================*/

#include <linux/module.h>
#include <linux/kernel.h>	/* printk(), and other useful stuff */
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/string.h>	/* inline memset(), etc. */
#include <linux/ctype.h>
#include <linux/slab.h>	/* kmalloc(), kfree() */
#include <linux/wanrouter.h>	/* WAN router definitions */
#include <linux/wanpipe.h>	/* WANPIPE common user API definitions */
#include <linux/workqueue.h>
#include <linux/jiffies.h>	/* time_after() macro */
#include <asm/byteorder.h>	/* htons(), etc. */
#include <asm/atomic.h>
#include <linux/delay.h>	/* Experimental delay */

#include <asm/uaccess.h>

#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/sdla_x25.h>	/* X.25 firmware API definitions */
#include <linux/if_wanpipe_common.h>
#include <linux/if_wanpipe.h>


/*======================================================
 * 	Defines & Macros 
 *=====================================================*/


#define	CMD_OK		0		/* normal firmware return code */
#define	CMD_TIMEOUT	0xFF		/* firmware command timed out */
#define	MAX_CMD_RETRY	10		/* max number of firmware retries */

#define	X25_CHAN_MTU	4096		/* unfragmented logical channel MTU */
#define	X25_HRDHDR_SZ	7		/* max encapsulation header size */
#define	X25_CONCT_TMOUT	(90*HZ)		/* link connection timeout */
#define	X25_RECON_TMOUT	(10*HZ)		/* link connection timeout */
#define	CONNECT_TIMEOUT	(90*HZ)		/* link connection timeout */
#define	HOLD_DOWN_TIME	(30*HZ)		/* link hold down time */
#define MAX_BH_BUFF	10
#define M_BIT		0x01	

//#define PRINT_DEBUG 1
#ifdef PRINT_DEBUG
#define DBG_PRINTK(format, a...) printk(format, ## a)
#else
#define DBG_PRINTK(format, a...)
#endif  

#define TMR_INT_ENABLED_POLL_ACTIVE      0x01
#define TMR_INT_ENABLED_POLL_CONNECT_ON  0x02
#define TMR_INT_ENABLED_POLL_CONNECT_OFF 0x04
#define TMR_INT_ENABLED_POLL_DISCONNECT  0x08
#define TMR_INT_ENABLED_CMD_EXEC	 0x10
#define TMR_INT_ENABLED_UPDATE		 0x20
#define TMR_INT_ENABLED_UDP_PKT		 0x40

#define MAX_X25_ADDR_SIZE	16
#define MAX_X25_DATA_SIZE 	129
#define MAX_X25_FACL_SIZE	110

#define TRY_CMD_AGAIN	2
#define DELAY_RESULT    1
#define RETURN_RESULT   0

#define DCD(x) (x & 0x03 ? "HIGH" : "LOW")
#define CTS(x) (x & 0x05 ? "HIGH" : "LOW")


/* Driver will not write log messages about 
 * modem status if defined.*/
#define MODEM_NOT_LOG 1

/*==================================================== 
 * 	For IPXWAN 
 *===================================================*/

#define CVHexToAscii(b) (((unsigned char)(b) > (unsigned char)9) ? ((unsigned char)'A' + ((unsigned char)(b) - (unsigned char)10)) : ((unsigned char)'0' + (unsigned char)(b)))


/*====================================================
 *           MEMORY DEBUGGING FUNCTION
 *====================================================

#define KMEM_SAFETYZONE 8

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

==============================================================*/



/*===============================================
 * 	Data Structures 
 *===============================================*/


/*========================================================
 * Name: 	x25_channel
 *
 * Purpose:	To hold private informaton for each  
 *              logical channel.
 *		
 * Rationale:  	Per-channel debugging is possible if each 
 *              channel has its own private area.
 *	
 * Assumptions:
 *
 * Description:	This is an extention of the struct net_device
 *              we create for each network interface to keep 
 *              the rest of X.25 channel-specific data. 
 *
 * Construct:	Typedef
 */
typedef struct x25_channel
{
	wanpipe_common_t common;	/* common area for x25api and socket */
	char name[WAN_IFNAME_SZ+1];	/* interface name, ASCIIZ */
	char addr[WAN_ADDRESS_SZ+1];	/* media address, ASCIIZ */
	unsigned tx_pkt_size;
	unsigned short protocol;	/* ethertype, 0 - multiplexed */
	char drop_sequence;		/* mark sequence for dropping */
	unsigned long state_tick;	/* time of the last state change */
	unsigned idle_timeout;		/* sec, before disconnecting */
	unsigned long i_timeout_sofar;  /* # of sec's we've been idle */
	unsigned hold_timeout;		/* sec, before re-connecting */
	unsigned long tick_counter;	/* counter for transmit time out */
	char devtint;			/* Weather we should dev_tint() */
	struct sk_buff* rx_skb;		/* receive socket buffer */
	struct sk_buff* tx_skb;		/* transmit socket buffer */

	bh_data_t *bh_head;	  	  /* Circular buffer for x25api_bh */
	unsigned long  tq_working;
	volatile int  bh_write;
	volatile int  bh_read;
	atomic_t  bh_buff_used;

	sdla_t* card;			/* -> owner */
	struct net_device *dev;		/* -> bound devce */

	int ch_idx;
	unsigned char enable_IPX;
	unsigned long network_number;
	struct net_device_stats ifstats;	/* interface statistics */
	unsigned short transmit_length;
	unsigned short tx_offset;
	char transmit_buffer[X25_CHAN_MTU+sizeof(x25api_hdr_t)];

	if_send_stat_t   if_send_stat;
        rx_intr_stat_t   rx_intr_stat;
        pipe_mgmt_stat_t pipe_mgmt_stat;    

	unsigned long router_start_time; /* Router start time in seconds */
	unsigned long router_up_time;
	
} x25_channel_t;

/* FIXME Take this out */

#ifdef NEX_OLD_CALL_INFO
typedef struct x25_call_info
{
	char dest[17];			PACKED;/* ASCIIZ destination address */
	char src[17];			PACKED;/* ASCIIZ source address */
	char nuser;			PACKED;/* number of user data bytes */
	unsigned char user[127];	PACKED;/* user data */
	char nfacil;			PACKED;/* number of facilities */
	struct
	{
		unsigned char code;     PACKED;
		unsigned char parm;     PACKED;
	} facil[64];			        /* facilities */
} x25_call_info_t;
#else
typedef struct x25_call_info
{
	char dest[MAX_X25_ADDR_SIZE]		PACKED;/* ASCIIZ destination address */
	char src[MAX_X25_ADDR_SIZE]		PACKED;/* ASCIIZ source address */
	unsigned char nuser			PACKED;
	unsigned char user[MAX_X25_DATA_SIZE]	PACKED;/* user data */
	unsigned char nfacil			PACKED;
	unsigned char facil[MAX_X25_FACL_SIZE]	PACKED;
	unsigned short lcn             		PACKED;
} x25_call_info_t;
#endif


  
/*===============================================
 *	Private Function Prototypes
 *==============================================*/


/*================================================= 
 * WAN link driver entry points. These are 
 * called by the WAN router module.
 */
static int update(struct wan_device* wandev);
static int new_if(struct wan_device* wandev, struct net_device* dev,
		  wanif_conf_t* conf);
static int del_if(struct wan_device* wandev, struct net_device* dev);
static void disable_comm (sdla_t* card);
static void disable_comm_shutdown(sdla_t *card);



/*================================================= 
 *	WANPIPE-specific entry points 
 */
static int wpx_exec (struct sdla* card, void* u_cmd, void* u_data);
static void x25api_bh(struct net_device *dev);
static int x25api_bh_cleanup(struct net_device *dev);
static int bh_enqueue(struct net_device *dev, struct sk_buff *skb);


/*=================================================  
 * 	Network device interface 
 */
static int if_init(struct net_device* dev);
static int if_open(struct net_device* dev);
static int if_close(struct net_device* dev);
static int if_header(struct sk_buff* skb, struct net_device* dev,
	unsigned short type, void* daddr, void* saddr, unsigned len);
static int if_rebuild_hdr (struct sk_buff* skb);
static int if_send(struct sk_buff* skb, struct net_device* dev);
static struct net_device_stats *if_stats(struct net_device* dev);

static void if_tx_timeout(struct net_device *dev);

/*=================================================  
 * 	Interrupt handlers 
 */
static void wpx_isr	(sdla_t *);
static void rx_intr	(sdla_t *);
static void tx_intr	(sdla_t *);
static void status_intr	(sdla_t *);
static void event_intr	(sdla_t *);
static void spur_intr	(sdla_t *);
static void timer_intr  (sdla_t *);

static int tx_intr_send(sdla_t *card, struct net_device *dev);
static struct net_device *move_dev_to_next(sdla_t *card,
					   struct net_device *dev);

/*=================================================  
 *	Background polling routines 
 */
static void wpx_poll (sdla_t* card);
static void poll_disconnected (sdla_t* card);
static void poll_connecting (sdla_t* card);
static void poll_active (sdla_t* card);
static void trigger_x25_poll(sdla_t *card);
static void x25_timer_routine(unsigned long data);



/*=================================================  
 *	X.25 firmware interface functions 
 */
static int x25_get_version (sdla_t* card, char* str);
static int x25_configure (sdla_t* card, TX25Config* conf);
static int hdlc_configure (sdla_t* card, TX25Config* conf);
static int set_hdlc_level (sdla_t* card);
static int x25_get_err_stats (sdla_t* card);
static int x25_get_stats (sdla_t* card);
static int x25_set_intr_mode (sdla_t* card, int mode);
static int x25_close_hdlc (sdla_t* card);
static int x25_open_hdlc (sdla_t* card);
static int x25_setup_hdlc (sdla_t* card);
static int x25_set_dtr (sdla_t* card, int dtr);
static int x25_get_chan_conf (sdla_t* card, x25_channel_t* chan);
static int x25_place_call (sdla_t* card, x25_channel_t* chan);
static int x25_accept_call (sdla_t* card, int lcn, int qdm);
static int x25_clear_call (sdla_t* card, int lcn, int cause, int diagn);
static int x25_send (sdla_t* card, int lcn, int qdm, int len, void* buf);
static int x25_fetch_events (sdla_t* card);
static int x25_error (sdla_t* card, int err, int cmd, int lcn);

/*=================================================  
 *	X.25 asynchronous event handlers 
 */
static int incoming_call (sdla_t* card, int cmd, int lcn, TX25Mbox* mb);
static int call_accepted (sdla_t* card, int cmd, int lcn, TX25Mbox* mb);
static int call_cleared (sdla_t* card, int cmd, int lcn, TX25Mbox* mb);
static int timeout_event (sdla_t* card, int cmd, int lcn, TX25Mbox* mb);
static int restart_event (sdla_t* card, int cmd, int lcn, TX25Mbox* mb);


/*=================================================  
 *	Miscellaneous functions 
 */
static int connect (sdla_t* card);
static int disconnect (sdla_t* card);
static struct net_device* get_dev_by_lcn(struct wan_device* wandev,
					 unsigned lcn);
static int chan_connect(struct net_device* dev);
static int chan_disc(struct net_device* dev);
static void set_chan_state(struct net_device* dev, int state);
static int chan_send(struct net_device *dev, void* buff, unsigned data_len,
		     unsigned char tx_intr);
static unsigned char bps_to_speed_code (unsigned long bps);
static unsigned int dec_to_uint (unsigned char* str, int len);
static unsigned int hex_to_uint (unsigned char*, int);
static void parse_call_info (unsigned char*, x25_call_info_t*);
static struct net_device *find_channel(sdla_t *card, unsigned lcn);
static void bind_lcn_to_dev(sdla_t *card, struct net_device *dev, unsigned lcn);
static void setup_for_delayed_transmit(struct net_device *dev,
				       void *buf, unsigned len);


/*=================================================  
 *      X25 API Functions 
 */
static int wanpipe_pull_data_in_skb(sdla_t *card, struct net_device *dev,
				    struct sk_buff **);
static void timer_intr_exec(sdla_t *, unsigned char);
static int execute_delayed_cmd(sdla_t *card, struct net_device *dev,
			       mbox_cmd_t *usr_cmd, char bad_cmd);
static int api_incoming_call (sdla_t*, TX25Mbox *, int);
static int alloc_and_init_skb_buf (sdla_t *,struct sk_buff **, int);
static void send_delayed_cmd_result(sdla_t *card, struct net_device *dev,
				    TX25Mbox* mbox);
static int clear_confirm_event (sdla_t *, TX25Mbox*);
static void send_oob_msg (sdla_t *card, struct net_device *dev, TX25Mbox *mbox);
static int timer_intr_cmd_exec(sdla_t *card);
static void api_oob_event (sdla_t *card,TX25Mbox *mbox);
static int check_bad_command(sdla_t *card, struct net_device *dev);
static int channel_disconnect(sdla_t* card, struct net_device *dev);
static void hdlc_link_down (sdla_t*);

/*=================================================
 *     XPIPEMON Functions
 */
static int process_udp_mgmt_pkt(sdla_t *);
static int udp_pkt_type( struct sk_buff *, sdla_t*);
static int reply_udp( unsigned char *, unsigned int); 
static void init_x25_channel_struct( x25_channel_t *);
static void init_global_statistics( sdla_t *);
static int store_udp_mgmt_pkt(int udp_type, char udp_pkt_src, sdla_t *card,
			      struct net_device *dev,
			      struct sk_buff *skb, int lcn);
static unsigned short calc_checksum (char *, int);



/*================================================= 
 *	IPX functions 
 */
static void switch_net_numbers(unsigned char *, unsigned long, unsigned char);
static int handle_IPXWAN(unsigned char *, char *, unsigned char , 
			 unsigned long , unsigned short );

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

static void S508_S514_lock(sdla_t *, unsigned long *);
static void S508_S514_unlock(sdla_t *, unsigned long *);


/*=================================================  
 * 	Global Variables 
 *=================================================*/



/*================================================= 
 *	Public Functions 
 *=================================================*/




/*===================================================================
 * wpx_init:	X.25 Protocol Initialization routine.
 *
 * Purpose:	To initialize the protocol/firmware.
 * 
 * Rationale:	This function is called by setup() function, in
 *              sdlamain.c, to dynamically setup the x25 protocol.
 *		This is the first protocol specific function, which
 *              executes once on startup.
 *                
 * Description:	This procedure initializes the x25 firmware and
 *    		sets up the mailbox, transmit and receive buffer
 *              pointers. It also initializes all debugging structures
 *              and sets up the X25 environment.
 *
 *		Sets up hardware options defined by user in [wanpipe#] 
 *		section of wanpipe#.conf configuration file. 
 *
 * 		At this point adapter is completely initialized 
 *      	and X.25 firmware is running.
 *  		o read firmware version (to make sure it's alive)
 *  		o configure adapter
 *  		o initialize protocol-specific fields of the 
 *                adapter data space.
 *
 * Called by:	setup() function in sdlamain.c
 *
 * Assumptions:	None
 *
 * Warnings:	None
 *
 * Return: 	0	o.k.
 *	 	< 0	failure.
 */

int wpx_init (sdla_t* card, wandev_conf_t* conf)
{
	union{
		char str[80];
		TX25Config cfg;
	} u;

	/* Verify configuration ID */
	if (conf->config_id != WANCONFIG_X25){
		printk(KERN_INFO "%s: invalid configuration ID %u!\n",
			card->devname, conf->config_id)
		;
		return -EINVAL;
	}

	/* Initialize protocol-specific fields */
	card->mbox  = (void*)(card->hw.dpmbase + X25_MBOX_OFFS);
	card->rxmb  = (void*)(card->hw.dpmbase + X25_RXMBOX_OFFS);
	card->flags = (void*)(card->hw.dpmbase + X25_STATUS_OFFS);

	/* Initialize for S514 Card */
	if(card->hw.type == SDLA_S514) {
		card->mbox += X25_MB_VECTOR;
		card->flags += X25_MB_VECTOR;
		card->rxmb += X25_MB_VECTOR;
	}


	/* Read firmware version.  Note that when adapter initializes, it
	 * clears the mailbox, so it may appear that the first command was
	 * executed successfully when in fact it was merely erased. To work
	 * around this, we execute the first command twice.
	 */
	if (x25_get_version(card, NULL) || x25_get_version(card, u.str))
		return -EIO;


	/* X25 firmware can run ether in X25 or LAPB HDLC mode.
         * Check the user defined option and configure accordingly */
	if (conf->u.x25.LAPB_hdlc_only == WANOPT_YES){
		if (set_hdlc_level(card) != CMD_OK){
			return -EIO;	
		}else{
			printk(KERN_INFO "%s: running LAP_B HDLC firmware v%s\n",
				card->devname, u.str);
		}
		card->u.x.LAPB_hdlc = 1;
	}else{
		printk(KERN_INFO "%s: running X.25 firmware v%s\n",
				card->devname, u.str);
		card->u.x.LAPB_hdlc = 0;
	}

	/* Configure adapter. Here we set resonable defaults, then parse
	 * device configuration structure and set configuration options.
	 * Most configuration options are verified and corrected (if
	 * necessary) since we can't rely on the adapter to do so.
	 */
	memset(&u.cfg, 0, sizeof(u.cfg));
	u.cfg.t1		= 3;
	u.cfg.n2		= 10;
	u.cfg.autoHdlc		= 1;		/* automatic HDLC connection */
	u.cfg.hdlcWindow	= 7;
	u.cfg.pktWindow		= 2;
	u.cfg.station		= 1;		/* DTE */
	u.cfg.options		= 0x0090;	/* disable D-bit pragmatics */
	u.cfg.ccittCompat	= 1988;
	u.cfg.t10t20		= 30;
	u.cfg.t11t21		= 30;
	u.cfg.t12t22		= 30;
	u.cfg.t13t23		= 30;
	u.cfg.t16t26		= 30;
	u.cfg.t28		= 30;
	u.cfg.r10r20		= 5;
	u.cfg.r12r22		= 5;
	u.cfg.r13r23		= 5;
	u.cfg.responseOpt	= 1;		/* RR's after every packet */

	if (card->u.x.LAPB_hdlc){
		u.cfg.hdlcMTU = 1027;
	}

	if (conf->u.x25.x25_conf_opt){
		u.cfg.options = conf->u.x25.x25_conf_opt;
	}

	if (conf->clocking != WANOPT_EXTERNAL)
		u.cfg.baudRate = bps_to_speed_code(conf->bps);

	if (conf->station != WANOPT_DTE){
		u.cfg.station = 0;		/* DCE mode */
	}

        if (conf->interface != WANOPT_RS232 ){
	        u.cfg.hdlcOptions |= 0x80;      /* V35 mode */
	} 

	/* adjust MTU */
	if (!conf->mtu || (conf->mtu >= 1024))
		card->wandev.mtu = 1024;
	else if (conf->mtu >= 512)
		card->wandev.mtu = 512;
	else if (conf->mtu >= 256)
		card->wandev.mtu = 256;
	else if (conf->mtu >= 128)
		card->wandev.mtu = 128;
	else 
		card->wandev.mtu = 64;

	u.cfg.defPktSize = u.cfg.pktMTU = card->wandev.mtu;

	if (conf->u.x25.hi_pvc){
		card->u.x.hi_pvc = min_t(unsigned int, conf->u.x25.hi_pvc, MAX_LCN_NUM);
		card->u.x.lo_pvc = min_t(unsigned int, conf->u.x25.lo_pvc, card->u.x.hi_pvc);
	}

	if (conf->u.x25.hi_svc){
		card->u.x.hi_svc = min_t(unsigned int, conf->u.x25.hi_svc, MAX_LCN_NUM);
		card->u.x.lo_svc = min_t(unsigned int, conf->u.x25.lo_svc, card->u.x.hi_svc);
	}

	/* Figure out the total number of channels to configure */
	card->u.x.num_of_ch = 0;
	if (card->u.x.hi_svc != 0){
		card->u.x.num_of_ch = (card->u.x.hi_svc - card->u.x.lo_svc) + 1;
	}
	if (card->u.x.hi_pvc != 0){
		card->u.x.num_of_ch += (card->u.x.hi_pvc - card->u.x.lo_pvc) + 1;
	}

	if (card->u.x.num_of_ch == 0){
		printk(KERN_INFO "%s: ERROR, Minimum number of PVC/SVC channels is 1 !\n"
				 "%s: Please set the Lowest/Highest PVC/SVC values !\n",
				 card->devname,card->devname);
		return -ECHRNG;
	}
	
	u.cfg.loPVC = card->u.x.lo_pvc;
	u.cfg.hiPVC = card->u.x.hi_pvc;
	u.cfg.loTwoWaySVC = card->u.x.lo_svc;
	u.cfg.hiTwoWaySVC = card->u.x.hi_svc;

	if (conf->u.x25.hdlc_window)
		u.cfg.hdlcWindow = min_t(unsigned int, conf->u.x25.hdlc_window, 7);
	if (conf->u.x25.pkt_window)
		u.cfg.pktWindow = min_t(unsigned int, conf->u.x25.pkt_window, 7);

	if (conf->u.x25.t1)
		u.cfg.t1 = min_t(unsigned int, conf->u.x25.t1, 30);
	if (conf->u.x25.t2)
		u.cfg.t2 = min_t(unsigned int, conf->u.x25.t2, 29);
	if (conf->u.x25.t4)
		u.cfg.t4 = min_t(unsigned int, conf->u.x25.t4, 240);
	if (conf->u.x25.n2)
		u.cfg.n2 = min_t(unsigned int, conf->u.x25.n2, 30);

	if (conf->u.x25.t10_t20)
		u.cfg.t10t20 = min_t(unsigned int, conf->u.x25.t10_t20,255);
	if (conf->u.x25.t11_t21)
		u.cfg.t11t21 = min_t(unsigned int, conf->u.x25.t11_t21,255);
	if (conf->u.x25.t12_t22)
		u.cfg.t12t22 = min_t(unsigned int, conf->u.x25.t12_t22,255);
	if (conf->u.x25.t13_t23)	
		u.cfg.t13t23 = min_t(unsigned int, conf->u.x25.t13_t23,255);
	if (conf->u.x25.t16_t26)
		u.cfg.t16t26 = min_t(unsigned int, conf->u.x25.t16_t26, 255);
	if (conf->u.x25.t28)
		u.cfg.t28 = min_t(unsigned int, conf->u.x25.t28, 255);

	if (conf->u.x25.r10_r20)
		u.cfg.r10r20 = min_t(unsigned int, conf->u.x25.r10_r20,250);
	if (conf->u.x25.r12_r22)
		u.cfg.r12r22 = min_t(unsigned int, conf->u.x25.r12_r22,250);
	if (conf->u.x25.r13_r23)
		u.cfg.r13r23 = min_t(unsigned int, conf->u.x25.r13_r23,250);


	if (conf->u.x25.ccitt_compat)
		u.cfg.ccittCompat = conf->u.x25.ccitt_compat;

	/* initialize adapter */
	if (card->u.x.LAPB_hdlc){
		if (hdlc_configure(card, &u.cfg) != CMD_OK)
			return -EIO;
	}else{
		if (x25_configure(card, &u.cfg) != CMD_OK)
			return -EIO;
	}

	if ((x25_close_hdlc(card) != CMD_OK) ||		/* close HDLC link */
	    (x25_set_dtr(card, 0) != CMD_OK))		/* drop DTR */
		return -EIO;

	/* Initialize protocol-specific fields of adapter data space */
	card->wandev.bps	= conf->bps;
	card->wandev.interface	= conf->interface;
	card->wandev.clocking	= conf->clocking;
	card->wandev.station	= conf->station;
	card->isr		= &wpx_isr;
	card->poll		= NULL; //&wpx_poll;
	card->disable_comm	= &disable_comm;
	card->exec		= &wpx_exec;
	card->wandev.update	= &update;
	card->wandev.new_if	= &new_if;
	card->wandev.del_if	= &del_if;

	/* WARNING: This function cannot exit with an error
	 *          after the change of state */
	card->wandev.state	= WAN_DISCONNECTED;
	
	card->wandev.enable_tx_int = 0;
	card->irq_dis_if_send_count = 0;
        card->irq_dis_poll_count = 0;
	card->u.x.tx_dev = NULL;
	card->u.x.no_dev = 0;


	/* Configure for S514 PCI Card */
	if (card->hw.type == SDLA_S514) {
		card->u.x.hdlc_buf_status = 
			(volatile unsigned char *)
				(card->hw.dpmbase + X25_MB_VECTOR+ X25_MISC_HDLC_BITS);
	}else{
		card->u.x.hdlc_buf_status = 
			(volatile unsigned char *)(card->hw.dpmbase + X25_MISC_HDLC_BITS); 
	}

	card->u.x.poll_device=NULL;
	card->wandev.udp_port = conf->udp_port;

	/* Enable or disable call setup logging */
	if (conf->u.x25.logging == WANOPT_YES){
		printk(KERN_INFO "%s: Enabling Call Logging.\n",
			card->devname);
		card->u.x.logging = 1;
	}else{	
		card->u.x.logging = 0;
	}

	/* Enable or disable modem status reporting */
	if (conf->u.x25.oob_on_modem == WANOPT_YES){
		printk(KERN_INFO "%s: Enabling OOB on Modem change.\n",
			card->devname);
		card->u.x.oob_on_modem = 1;
	}else{
		card->u.x.oob_on_modem = 0;
	}
	
	init_global_statistics(card);	

	INIT_WORK(&card->u.x.x25_poll_work, (void *)wpx_poll, card);

	init_timer(&card->u.x.x25_timer);
	card->u.x.x25_timer.data = (unsigned long)card;
	card->u.x.x25_timer.function = x25_timer_routine;
	
	return 0;
}

/*=========================================================
 *	WAN Device Driver Entry Points 
 *========================================================*/

/*============================================================
 * Name:	update(),  Update device status & statistics.
 *
 * Purpose:	To provide debugging and statitical
 *              information to the /proc file system.
 *              /proc/net/wanrouter/wanpipe#
 *              	
 * Rationale:	The /proc file system is used to collect
 *              information about the kernel and drivers.
 *              Using the /proc file system the user
 *              can see exactly what the sangoma drivers are
 *              doing. And in what state they are in. 
 *                
 * Description: Collect all driver statistical information
 *              and pass it to the top laywer. 
 *		
 *		Since we have to execute a debugging command, 
 *              to obtain firmware statitics, we trigger a 
 *              UPDATE function within the timer interrtup.
 *              We wait until the timer update is complete.
 *              Once complete return the appropriate return
 *              code to indicate that the update was successful.
 *              
 * Called by:	device_stat() in wanmain.c
 *
 * Assumptions:	
 *
 * Warnings:	This function will degrade the performance
 *              of the router, since it uses the mailbox. 
 *
 * Return: 	0 	OK
 * 		<0	Failed (or busy).
 */

static int update(struct wan_device* wandev)
{
	volatile sdla_t* card;
	TX25Status* status;
	unsigned long timeout;

	/* sanity checks */
	if ((wandev == NULL) || (wandev->private == NULL))
		return -EFAULT;

	if (wandev->state == WAN_UNCONFIGURED)
		return -ENODEV;

	if (test_bit(SEND_CRIT, (void*)&wandev->critical))
		return -EAGAIN;

	if (!wandev->dev)
		return -ENODEV;
	
	card = wandev->private;
	status = card->flags;

	card->u.x.timer_int_enabled |= TMR_INT_ENABLED_UPDATE;
	status->imask |= INTR_ON_TIMER;
	timeout = jiffies;	

	for (;;){
		if (!(card->u.x.timer_int_enabled & TMR_INT_ENABLED_UPDATE)){	
			break;
		}
		if (time_after(jiffies, timeout + 1*HZ)){
			card->u.x.timer_int_enabled &= ~TMR_INT_ENABLED_UPDATE;
			return -EAGAIN;
		}
	}
	return 0;
}


/*===================================================================
 * Name:	new_if
 *
 * Purpose:	To allocate and initialize resources for a 
 *              new logical channel.  
 * 
 * Rationale:	A new channel can be added dynamically via
 *              ioctl call.
 *                
 * Description:	Allocate a private channel structure, x25_channel_t.
 *		Parse the user interface options from wanpipe#.conf 
 *		configuration file. 
 *		Bind the private are into the network device private
 *              area pointer (dev->priv).
 *		Prepare the network device structure for registration.
 *
 * Called by:	ROUTER_IFNEW Ioctl call, from wanrouter_ioctl() 
 *              (wanmain.c)
 *
 * Assumptions: None
 *
 * Warnings:	None
 *
 * Return: 	0 	Ok
 *		<0 	Failed (channel will not be created)
 */
static int new_if(struct wan_device* wandev, struct net_device* dev,
		  wanif_conf_t* conf)
{
	sdla_t* card = wandev->private;
	x25_channel_t* chan;
	int err = 0;

	if ((conf->name[0] == '\0') || (strlen(conf->name) > WAN_IFNAME_SZ)){
		printk(KERN_INFO "%s: invalid interface name!\n",
			card->devname);
		return -EINVAL;
	}

	if(card->wandev.new_if_cnt++ > 0 && card->u.x.LAPB_hdlc) {
		printk(KERN_INFO "%s: Error: Running LAPB HDLC Mode !\n",
						card->devname);
		printk(KERN_INFO 
			"%s: Maximum number of network interfaces must be one !\n",
						card->devname);
		return -EEXIST;
	}

	/* allocate and initialize private data */
	chan = kmalloc(sizeof(x25_channel_t), GFP_ATOMIC);
	if (chan == NULL){
		return -ENOMEM;
	}
	
	memset(chan, 0, sizeof(x25_channel_t));

	/* Bug Fix: Seg Err on PVC startup
	 * It must be here since bind_lcn_to_dev expects 
	 * it bellow */
	dev->priv = chan;
	
	strcpy(chan->name, conf->name);
	chan->card = card;
	chan->dev = dev;
	chan->common.sk = NULL;
	chan->common.func = NULL;
	chan->common.rw_bind = 0;
	chan->tx_skb = chan->rx_skb = NULL;

	/* verify media address */
	if (conf->addr[0] == '@'){		/* SVC */
		chan->common.svc = 1;
		strncpy(chan->addr, &conf->addr[1], WAN_ADDRESS_SZ);

		/* Set channel timeouts (default if not specified) */
		chan->idle_timeout = (conf->idle_timeout) ? 
					conf->idle_timeout : 90;
		chan->hold_timeout = (conf->hold_timeout) ? 
					conf->hold_timeout : 10;

	}else if (isdigit(conf->addr[0])){	/* PVC */
		int lcn = dec_to_uint(conf->addr, 0);

		if ((lcn >= card->u.x.lo_pvc) && (lcn <= card->u.x.hi_pvc)){
			bind_lcn_to_dev (card, dev, lcn);
		}else{
			printk(KERN_ERR
				"%s: PVC %u is out of range on interface %s!\n",
				wandev->name, lcn, chan->name);
			err = -EINVAL;
		}
	}else{
		printk(KERN_ERR
			"%s: invalid media address on interface %s!\n",
			wandev->name, chan->name);
		err = -EINVAL;
	}

	if(strcmp(conf->usedby, "WANPIPE") == 0){
                printk(KERN_INFO "%s: Running in WANPIPE mode %s\n",
			wandev->name, chan->name);
                chan->common.usedby = WANPIPE;
		chan->protocol = htons(ETH_P_IP);

        }else if(strcmp(conf->usedby, "API") == 0){
		chan->common.usedby = API;
                printk(KERN_INFO "%s: Running in API mode %s\n",
			wandev->name, chan->name);
		chan->protocol = htons(X25_PROT);
	}


	if (err){
		kfree(chan);
		dev->priv = NULL;
		return err;
	}
	
	chan->enable_IPX = conf->enable_IPX;
	
	if (chan->enable_IPX)
		chan->protocol = htons(ETH_P_IPX);
	
	if (conf->network_number)
		chan->network_number = conf->network_number;
	else
		chan->network_number = 0xDEADBEEF;

	/* prepare network device data space for registration */
	strcpy(dev->name,chan->name);

	dev->init = &if_init;

	init_x25_channel_struct(chan);

	return 0;
}

/*===================================================================
 * Name:	del_if(),  Remove a logical channel.	 
 *
 * Purpose:	To dynamically remove a logical channel.
 * 
 * Rationale:	Each logical channel should be dynamically
 *              removable. This functin is called by an 
 *              IOCTL_IFDEL ioctl call or shutdown(). 
 *                
 * Description: Do nothing.
 *
 * Called by:	IOCTL_IFDEL : wanrouter_ioctl() from wanmain.c
 *              shutdown() from sdlamain.c
 *
 * Assumptions: 
 *
 * Warnings:
 *
 * Return: 	0 Ok. Void function.
 */

//FIXME Del IF Should be taken out now.

static int del_if(struct wan_device* wandev, struct net_device* dev)
{
	return 0;
}


/*============================================================
 * Name:	wpx_exec
 *
 * Description:	Execute adapter interface command.
 * 		This option is currently dissabled.
 *===========================================================*/

static int wpx_exec (struct sdla* card, void* u_cmd, void* u_data)
{
        return 0;
}

/*============================================================
 * Name:	disable_comm	
 *
 * Description:	Disable communications during shutdown.
 *              Dont check return code because there is 
 *              nothing we can do about it.  
 *
 * Warning:	Dev and private areas are gone at this point.
 *===========================================================*/

static void disable_comm(sdla_t* card)
{
	disable_comm_shutdown(card);
	del_timer(&card->u.x.x25_timer);
	return;
}


/*============================================================
 *	Network Device Interface 
 *===========================================================*/

/*===================================================================
 * Name:	if_init(),   Netowrk Interface Initialization 	 
 *
 * Purpose:	To initialize a network interface device structure.
 * 
 * Rationale:	During network interface startup, the if_init
 *              is called by the kernel to initialize the
 *              netowrk device structure.  Thus a driver
 *              can customze a network device. 
 *                
 * Description:	Initialize the netowrk device call back
 *              routines.  This is where we tell the kernel
 *              which function to use when it wants to send
 *              via our interface. 
 *		Furthermore, we initialize the device flags, 
 *              MTU and physical address of the board.
 *
 * Called by:	Kernel (/usr/src/linux/net/core/dev.c)
 * 		(dev->init())
 *
 * Assumptions: None
 *	
 * Warnings:	None
 *
 * Return: 	0 	Ok : Void function.
 */
static int if_init(struct net_device* dev)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
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
	dev->type		= ARPHRD_PPP;		/* ARP h/w type */
	dev->flags		|= IFF_POINTOPOINT;
	dev->flags		|= IFF_NOARP;

	if (chan->common.usedby == API){
		dev->mtu	= X25_CHAN_MTU+sizeof(x25api_hdr_t);
	}else{
		dev->mtu	= card->wandev.mtu; 	
	}
	
	dev->hard_header_len	= X25_HRDHDR_SZ; /* media header length */
	dev->addr_len		= 2;		/* hardware address length */
	
	if (!chan->common.svc){
		*(unsigned short*)dev->dev_addr = htons(chan->common.lcn);
	}
	
	/* Initialize hardware parameters (just for reference) */
	dev->irq	= wandev->irq;
	dev->dma	= wandev->dma;
	dev->base_addr	= wandev->ioport;
	dev->mem_start	= (unsigned long)wandev->maddr;
	dev->mem_end	= wandev->maddr + wandev->msize - 1;

        /* Set transmit buffer queue length */
        dev->tx_queue_len = 100;
	SET_MODULE_OWNER(dev);

	/* FIXME Why are we doing this */
	set_chan_state(dev, WAN_DISCONNECTED);
	return 0;
}


/*===================================================================
 * Name:	if_open(),   Open/Bring up the Netowrk Interface 
 *
 * Purpose:	To bring up a network interface.
 * 
 * Rationale:	
 *                
 * Description:	Open network interface.
 * 		o prevent module from unloading by incrementing use count
 * 		o if link is disconnected then initiate connection
 *
 * Called by:	Kernel (/usr/src/linux/net/core/dev.c)
 * 		(dev->open())
 *
 * Assumptions: None
 *	
 * Warnings:	None
 *
 * Return: 	0 	Ok
 * 		<0 	Failure: Interface will not come up.
 */

static int if_open(struct net_device* dev)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	struct timeval tv;
	unsigned long smp_flags;
	
	if (netif_running(dev))
		return -EBUSY;

	chan->tq_working = 0;

	/* Initialize the workqueue */
	INIT_WORK(&chan->common.wanpipe_work, (void *)x25api_bh, dev);

	/* Allocate and initialize BH circular buffer */
	/* Add 1 to MAX_BH_BUFF so we don't have test with (MAX_BH_BUFF-1) */
	chan->bh_head = kmalloc((sizeof(bh_data_t)*(MAX_BH_BUFF+1)),GFP_ATOMIC);

	if (chan->bh_head == NULL){
		printk(KERN_INFO "%s: ERROR, failed to allocate memory ! BH_BUFFERS !\n",
				card->devname);

		return -ENOBUFS;
	}
	memset(chan->bh_head,0,(sizeof(bh_data_t)*(MAX_BH_BUFF+1)));
	atomic_set(&chan->bh_buff_used, 0);

	/* Increment the number of interfaces */
	++card->u.x.no_dev;
	
	wanpipe_open(card);

	/* LAPB protocol only uses one interface, thus
	 * start the protocol after it comes up. */
	if (card->u.x.LAPB_hdlc){
		if (card->open_cnt == 1){
			TX25Status* status = card->flags;
			S508_S514_lock(card, &smp_flags);
			x25_set_intr_mode(card, INTR_ON_TIMER); 
			status->imask &= ~INTR_ON_TIMER;
			S508_S514_unlock(card, &smp_flags);
		}
	}else{
		/* X25 can have multiple interfaces thus, start the 
		 * protocol once all interfaces are up */

		//FIXME: There is a bug here. If interface is
		//brought down and up, it will try to enable comm.
		if (card->open_cnt == card->u.x.num_of_ch){

			S508_S514_lock(card, &smp_flags);
			connect(card);
			S508_S514_unlock(card, &smp_flags);

			mod_timer(&card->u.x.x25_timer, jiffies + HZ);
		}
	}
	/* Device is not up until the we are in connected state */
	do_gettimeofday( &tv );
	chan->router_start_time = tv.tv_sec;

	netif_start_queue(dev);

	return 0;
}

/*===================================================================
 * Name:	if_close(),   Close/Bring down the Netowrk Interface 
 *
 * Purpose:	To bring down a network interface.
 * 
 * Rationale:	
 *                
 * Description:	Close network interface.
 * 		o decrement use module use count
 *
 * Called by:	Kernel (/usr/src/linux/net/core/dev.c)
 * 		(dev->close())
 *		ifconfig <name> down: will trigger the kernel
 *              which will call this function.
 *
 * Assumptions: None
 *	
 * Warnings:	None
 *
 * Return: 	0 	Ok
 * 		<0 	Failure: Interface will not exit properly.
 */
static int if_close(struct net_device* dev)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	unsigned long smp_flags;
	
	netif_stop_queue(dev);

	if ((chan->common.state == WAN_CONNECTED) || 
	    (chan->common.state == WAN_CONNECTING)){
		S508_S514_lock(card, &smp_flags);
		chan_disc(dev);
		S508_S514_unlock(card, &smp_flags);
	}

	wanpipe_close(card);

	S508_S514_lock(card, &smp_flags);
	if (chan->bh_head){
		int i;
		struct sk_buff *skb;
	
		for (i=0; i<(MAX_BH_BUFF+1); i++){
			skb = ((bh_data_t *)&chan->bh_head[i])->skb;
			if (skb != NULL){
                		dev_kfree_skb_any(skb);
			}
		}
		kfree(chan->bh_head);
		chan->bh_head=NULL;
	}
	S508_S514_unlock(card, &smp_flags);

	/* If this is the last close, disconnect physical link */
	if (!card->open_cnt){
		S508_S514_lock(card, &smp_flags);
		disconnect(card);
		x25_set_intr_mode(card, 0);
		S508_S514_unlock(card, &smp_flags);
	}
	
	/* Decrement the number of interfaces */
	--card->u.x.no_dev;
	return 0;
}

/*======================================================================
 * 	Build media header.
 * 	o encapsulate packet according to encapsulation type.
 *
 * 	The trick here is to put packet type (Ethertype) into 'protocol' 
 *      field of the socket buffer, so that we don't forget it.  
 *      If encapsulation fails, set skb->protocol to 0 and discard 
 *      packet later.
 *
 * 	Return:		media header length.
 *======================================================================*/

static int if_header(struct sk_buff* skb, struct net_device* dev,
		     unsigned short type, void* daddr, void* saddr,
		     unsigned len)
{
	x25_channel_t* chan = dev->priv;
	int hdr_len = dev->hard_header_len;
	
	skb->protocol = htons(type);
	if (!chan->protocol){
		hdr_len = wanrouter_encapsulate(skb, dev, type);
		if (hdr_len < 0){
			hdr_len = 0;
			skb->protocol = htons(0);
		}
	}
	return hdr_len;
}

/*===============================================================
 * 	Re-build media header.
 *
 * 	Return:		1	physical address resolved.
 *			0	physical address not resolved
 *==============================================================*/

static int if_rebuild_hdr (struct sk_buff* skb)
{
	struct net_device *dev = skb->dev; 
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;

	printk(KERN_INFO "%s: rebuild_header() called for interface %s!\n",
		card->devname, dev->name);
	return 1;
}


/*============================================================================
 * Handle transmit timeout event from netif watchdog
 */
static void if_tx_timeout(struct net_device *dev)
{
    	x25_channel_t* chan = dev->priv;
	sdla_t *card = chan->card;

	/* If our device stays busy for at least 5 seconds then we will
	 * kick start the device by making dev->tbusy = 0.  We expect
	 * that our device never stays busy more than 5 seconds. So this                 
	 * is only used as a last resort.
	 */

	++chan->if_send_stat.if_send_tbusy_timeout;
	printk (KERN_INFO "%s: Transmit timed out on %s\n", 
			card->devname, dev->name);
	netif_wake_queue (dev);
}


/*=========================================================================
 * 	Send a packet on a network interface.
 * 	o set tbusy flag (marks start of the transmission).
 * 	o check link state. If link is not up, then drop the packet.
 * 	o check channel status. If it's down then initiate a call.
 * 	o pass a packet to corresponding WAN device.
 * 	o free socket buffer
 *
 * 	Return:	0	complete (socket buffer must be freed)
 *		non-0	packet may be re-transmitted (tbusy must be set)
 *
 * 	Notes:
 * 	1. This routine is called either by the protocol stack or by the "net
 *    	bottom half" (with interrupts enabled).
 * 	2. Setting tbusy flag will inhibit further transmit requests from the
 *    	protocol stack and can be used for flow control with protocol layer.
 *
 *========================================================================*/

static int if_send(struct sk_buff* skb, struct net_device* dev)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	TX25Status* status = card->flags;
	int udp_type;
	unsigned long smp_flags=0;

	++chan->if_send_stat.if_send_entry;

	netif_stop_queue(dev);

	/* No need to check frame length, since socket code
         * will perform the check for us */

	chan->tick_counter = jiffies;
	
	/* Critical region starts here */
	S508_S514_lock(card, &smp_flags);
	
	if (test_and_set_bit(SEND_CRIT, (void*)&card->wandev.critical)){
		printk(KERN_INFO "Hit critical in if_send()! %lx\n",card->wandev.critical);
		goto if_send_crit_exit;
	}
	
	udp_type = udp_pkt_type(skb, card);

        if(udp_type != UDP_INVALID_TYPE) {

                if(store_udp_mgmt_pkt(udp_type, UDP_PKT_FRM_STACK, card, dev, skb,
                        chan->common.lcn)) {

                        status->imask |= INTR_ON_TIMER;
                        if (udp_type == UDP_XPIPE_TYPE){
                                chan->if_send_stat.if_send_PIPE_request++;
			}
               	}
		netif_start_queue(dev);
		clear_bit(SEND_CRIT,(void*)&card->wandev.critical);
		S508_S514_unlock(card, &smp_flags);
		return 0;
	}

	if (chan->transmit_length){
		//FIXME: This check doesn't make sense any more
		if (chan->common.state != WAN_CONNECTED){
			chan->transmit_length=0;
			atomic_set(&chan->common.driver_busy,0);
		}else{
			netif_stop_queue(dev);
			++card->u.x.tx_interrupts_pending;
		        status->imask |= INTR_ON_TX_FRAME;
			clear_bit(SEND_CRIT,(void*)&card->wandev.critical);
			S508_S514_unlock(card, &smp_flags);
			return 1;
		}
	}

	if (card->wandev.state != WAN_CONNECTED){
		++chan->ifstats.tx_dropped;
		++card->wandev.stats.tx_dropped;
		++chan->if_send_stat.if_send_wan_disconnected;	
		
	}else if ( chan->protocol && (chan->protocol != skb->protocol)){
		printk(KERN_INFO
			"%s: unsupported Ethertype 0x%04X on interface %s!\n",
			chan->name, htons(skb->protocol), dev->name);
		
		printk(KERN_INFO "PROTO %Xn", htons(chan->protocol));
		++chan->ifstats.tx_errors;
		++chan->ifstats.tx_dropped;
		++card->wandev.stats.tx_dropped;
		++chan->if_send_stat.if_send_protocol_error;
		
	}else switch (chan->common.state){

		case WAN_DISCONNECTED:
			/* Try to establish connection. If succeded, then start
			 * transmission, else drop a packet.
			 */
			if (chan->common.usedby == API){
				++chan->ifstats.tx_dropped;
				++card->wandev.stats.tx_dropped;
				break;
			}else{
				if (chan_connect(dev) != 0){
					++chan->ifstats.tx_dropped;
					++card->wandev.stats.tx_dropped;
					break;
				}
			}
			/* fall through */

		case WAN_CONNECTED:
			if( skb->protocol == htons(ETH_P_IPX)) {
				if(chan->enable_IPX) {
					switch_net_numbers( skb->data, 
						chan->network_number, 0);
				} else {
					++card->wandev.stats.tx_dropped;
					++chan->ifstats.tx_dropped;
					++chan->if_send_stat.if_send_protocol_error;
					goto if_send_crit_exit;
				}
			}
			/* We never drop here, if cannot send than, copy
	                 * a packet into a transmit buffer 
                         */
			chan_send(dev, skb->data, skb->len, 0);
			break;

		default:
			++chan->ifstats.tx_dropped;	
			++card->wandev.stats.tx_dropped;
			break;
	}


if_send_crit_exit:
	
       	dev_kfree_skb_any(skb);

	netif_start_queue(dev);
	clear_bit(SEND_CRIT,(void*)&card->wandev.critical);
	S508_S514_unlock(card, &smp_flags);
	return 0;
}

/*============================================================================
 * Setup so that a frame can be transmitted on the occurrence of a transmit
 * interrupt.
 *===========================================================================*/

static void setup_for_delayed_transmit(struct net_device* dev, void* buf,
				       unsigned len)
{
        x25_channel_t* chan = dev->priv;
        sdla_t* card = chan->card;
	TX25Status* status = card->flags;

	++chan->if_send_stat.if_send_adptr_bfrs_full;

        if(chan->transmit_length) {
                printk(KERN_INFO "%s: Error, transmit length set in delayed transmit!\n",
				card->devname);
                return;
        }

	if (chan->common.usedby == API){
		if (len > X25_CHAN_MTU+sizeof(x25api_hdr_t)) {
			++chan->ifstats.tx_dropped;	
			++card->wandev.stats.tx_dropped;
			printk(KERN_INFO "%s: Length is too big for delayed transmit\n",
				card->devname);
			return;
		}
	}else{
		if (len > X25_MAX_DATA) {
			++chan->ifstats.tx_dropped;	
			++card->wandev.stats.tx_dropped;
			printk(KERN_INFO "%s: Length is too big for delayed transmit\n",
				card->devname);
			return;
		}
	}

        chan->transmit_length = len;
	atomic_set(&chan->common.driver_busy,1);
        memcpy(chan->transmit_buffer, buf, len);

	++chan->if_send_stat.if_send_tx_int_enabled;

	/* Enable Transmit Interrupt */
	++card->u.x.tx_interrupts_pending;
        status->imask |= INTR_ON_TX_FRAME;
}


/*===============================================================
 * net_device_stats
 *
 * 	Get ethernet-style interface statistics.
 * 	Return a pointer to struct enet_statistics.
 *
 *==============================================================*/
static struct net_device_stats *if_stats(struct net_device* dev)
{
	x25_channel_t *chan = dev->priv;

	if(chan == NULL)
		return NULL;

	return &chan->ifstats;
}


/*
 *	Interrupt Handlers 
 */

/*
 * X.25 Interrupt Service Routine.
 */

static void wpx_isr (sdla_t* card)
{
	TX25Status* status = card->flags;

	card->in_isr = 1;
	++card->statistics.isr_entry;

	if (test_bit(PERI_CRIT,(void*)&card->wandev.critical)){
		card->in_isr=0;
		status->iflags = 0;
		return;
	}
	
	if (test_bit(SEND_CRIT, (void*)&card->wandev.critical)){

 		printk(KERN_INFO "%s: wpx_isr: wandev.critical set to 0x%02lx, int type = 0x%02x\n", 
			card->devname, card->wandev.critical, status->iflags);
		card->in_isr = 0;
		status->iflags = 0;
		return;
	}

	/* For all interrupts set the critical flag to CRITICAL_RX_INTR.
         * If the if_send routine is called with this flag set it will set
         * the enable transmit flag to 1. (for a delayed interrupt)
         */
	switch (status->iflags){

		case RX_INTR_PENDING:		/* receive interrupt */
			rx_intr(card);
			break;

		case TX_INTR_PENDING:		/* transmit interrupt */
			tx_intr(card);
			break;

		case MODEM_INTR_PENDING:	/* modem status interrupt */
			status_intr(card);
			break;

		case X25_ASY_TRANS_INTR_PENDING:	/* network event interrupt */
			event_intr(card);
			break;

		case TIMER_INTR_PENDING:
			timer_intr(card);
			break;

		default:		/* unwanted interrupt */
			spur_intr(card);
	}

	card->in_isr = 0;
	status->iflags = 0;	/* clear interrupt condition */
}

/*
 * 	Receive interrupt handler.
 * 	This routine handles fragmented IP packets using M-bit according to the
 * 	RFC1356.
 * 	o map ligical channel number to network interface.
 * 	o allocate socket buffer or append received packet to the existing one.
 * 	o if M-bit is reset (i.e. it's the last packet in a sequence) then 
 *   	decapsulate packet and pass socket buffer to the protocol stack.
 *
 * 	Notes:
 * 	1. When allocating a socket buffer, if M-bit is set then more data is
 *    	coming and we have to allocate buffer for the maximum IP packet size
 *    	expected on this channel.
 * 	2. If something goes wrong and X.25 packet has to be dropped (e.g. no
 *    	socket buffers available) the whole packet sequence must be discarded.
 */

static void rx_intr (sdla_t* card)
{
	TX25Mbox* rxmb = card->rxmb;
	unsigned lcn = rxmb->cmd.lcn;
	struct net_device* dev = find_channel(card,lcn);
	x25_channel_t* chan;
	struct sk_buff* skb=NULL;

	if (dev == NULL){
		/* Invalid channel, discard packet */
		printk(KERN_INFO "%s: receiving on orphaned LCN %d!\n",
			card->devname, lcn);
		return;
	}

	chan = dev->priv;
	chan->i_timeout_sofar = jiffies;


	/* Copy the data from the board, into an
         * skb buffer 
	 */
	if (wanpipe_pull_data_in_skb(card,dev,&skb)){
		++chan->ifstats.rx_dropped;
		++card->wandev.stats.rx_dropped;
		++chan->rx_intr_stat.rx_intr_no_socket;
		++chan->rx_intr_stat.rx_intr_bfr_not_passed_to_stack;
		return;
	}

	dev->last_rx = jiffies;		/* timestamp */


	/* ------------ API ----------------*/

	if (chan->common.usedby == API){

		if (bh_enqueue(dev, skb)){
			++chan->ifstats.rx_dropped;
			++card->wandev.stats.rx_dropped;
			++chan->rx_intr_stat.rx_intr_bfr_not_passed_to_stack;
			dev_kfree_skb_any(skb);
			return;
		}		

		++chan->ifstats.rx_packets;
		chan->ifstats.rx_bytes += skb->len;
		

		chan->rx_skb = NULL;
		if (!test_and_set_bit(0, &chan->tq_working)){
			wanpipe_queue_work(&chan->common.wanpipe_work);
		}
		return;
	}


	/* ------------- WANPIPE -------------------*/
	
	/* set rx_skb to NULL so we won't access it later when kernel already owns it */
	chan->rx_skb=NULL;
	
	/* Decapsulate packet, if necessary */
	if (!skb->protocol && !wanrouter_type_trans(skb, dev)){
		/* can't decapsulate packet */
                dev_kfree_skb_any(skb);
		++chan->ifstats.rx_errors;
		++chan->ifstats.rx_dropped;
		++card->wandev.stats.rx_dropped;
		++chan->rx_intr_stat.rx_intr_bfr_not_passed_to_stack;

	}else{
		if( handle_IPXWAN(skb->data, chan->name, 
				  chan->enable_IPX, chan->network_number, 
				  skb->protocol)){

			if( chan->enable_IPX ){
				if(chan_send(dev, skb->data, skb->len,0)){
					chan->tx_skb = skb;
				}else{
                                        dev_kfree_skb_any(skb);
					++chan->rx_intr_stat.rx_intr_bfr_not_passed_to_stack;
				}
			}else{
				/* increment IPX packet dropped statistic */
				++chan->ifstats.rx_dropped;
				++chan->rx_intr_stat.rx_intr_bfr_not_passed_to_stack;
			}
		}else{
			skb->mac.raw = skb->data;
			chan->ifstats.rx_bytes += skb->len;
			++chan->ifstats.rx_packets;
			++chan->rx_intr_stat.rx_intr_bfr_passed_to_stack;
			netif_rx(skb);
		}
	}
	
	return;
}


static int wanpipe_pull_data_in_skb(sdla_t *card, struct net_device *dev,
				    struct sk_buff **skb)
{
	void *bufptr;
	TX25Mbox* rxmb = card->rxmb;
	unsigned len = rxmb->cmd.length;	/* packet length */
	unsigned qdm = rxmb->cmd.qdm;		/* Q,D and M bits */
	x25_channel_t *chan = dev->priv;
	struct sk_buff *new_skb = *skb;

	if (chan->common.usedby == WANPIPE){
		if (chan->drop_sequence){
			if (!(qdm & 0x01)){ 
				chan->drop_sequence = 0;
			}
			return 1;
		}
		new_skb = chan->rx_skb;
	}else{
		/* Add on the API header to the received
                 * data 
		 */
		len += sizeof(x25api_hdr_t);
	}

	if (new_skb == NULL){
		int bufsize;

		if (chan->common.usedby == WANPIPE){
			bufsize = (qdm & 0x01) ? dev->mtu : len;
		}else{
			bufsize = len;
		}

		/* Allocate new socket buffer */
		new_skb = dev_alloc_skb(bufsize + dev->hard_header_len);
		if (new_skb == NULL){
			printk(KERN_INFO "%s: no socket buffers available!\n",
				card->devname);
			chan->drop_sequence = 1;	/* set flag */
			++chan->ifstats.rx_dropped;
			return 1;
		}
	}

	if (skb_tailroom(new_skb) < len){
		/* No room for the packet. Call off the whole thing! */
                dev_kfree_skb_any(new_skb);
		if (chan->common.usedby == WANPIPE){
			chan->rx_skb = NULL;
			if (qdm & 0x01){ 
				chan->drop_sequence = 1;
			}
		}

		printk(KERN_INFO "%s: unexpectedly long packet sequence "
			"on interface %s!\n", card->devname, dev->name);
		++chan->ifstats.rx_length_errors;
		return 1;
	}

	bufptr = skb_put(new_skb,len);


	if (chan->common.usedby == API){
		/* Fill in the x25api header 
		 */
		x25api_t * api_data = (x25api_t*)bufptr;
		api_data->hdr.qdm = rxmb->cmd.qdm;
		api_data->hdr.cause = rxmb->cmd.cause;
		api_data->hdr.diagn = rxmb->cmd.diagn;
		api_data->hdr.length = rxmb->cmd.length;
		memcpy(api_data->data, rxmb->data, rxmb->cmd.length);
	}else{
		memcpy(bufptr, rxmb->data, len);
	}

	new_skb->dev = dev;

	if (chan->common.usedby == API){
		new_skb->mac.raw = new_skb->data;
		new_skb->protocol = htons(X25_PROT);
		new_skb->pkt_type = WAN_PACKET_DATA;
	}else{
		new_skb->protocol = chan->protocol;
		chan->rx_skb = new_skb;
	}

	/* If qdm bit is set, more data is coming 
         * thus, exit and wait for more data before
         * sending the packet up. (Used by router only) 
	 */
	if ((qdm & 0x01) && (chan->common.usedby == WANPIPE)) 
		return 1;	

	*skb = new_skb; 

	return 0;
}

/*===============================================================
 * tx_intr
 *  
 * 	Transmit interrupt handler.
 *	For each dev, check that there is something to send.
 *	If data available, transmit. 	
 *
 *===============================================================*/

static void tx_intr (sdla_t* card)
{
	struct net_device *dev;
	TX25Status* status = card->flags;
	unsigned char more_to_tx=0;
	x25_channel_t *chan=NULL;
	int i=0;	

	if (card->u.x.tx_dev == NULL){
		card->u.x.tx_dev = card->wandev.dev;
	}

	dev = card->u.x.tx_dev;

	for (;;){

		chan = dev->priv;
		if (chan->transmit_length){
			/* Device was set to transmit, check if the TX
                         * buffers are available 
			 */		
			if (chan->common.state != WAN_CONNECTED){
				chan->transmit_length = 0;
				atomic_set(&chan->common.driver_busy,0);
				chan->tx_offset=0;
				if (netif_queue_stopped(dev)){
					if (chan->common.usedby == API){
						netif_start_queue(dev);
						wakeup_sk_bh(dev);
					}else{
						netif_wake_queue(dev);
					}
				}
				dev = move_dev_to_next(card,dev);
				break;
			}				

			if ((status->cflags[chan->ch_idx] & 0x40 || card->u.x.LAPB_hdlc) && 
			     (*card->u.x.hdlc_buf_status & 0x40) ){
				/* Tx buffer available, we can send */
				
				if (tx_intr_send(card, dev)){
					more_to_tx=1;
				}

				/* If more than one interface present, move the
                                 * device pointer to the next interface, so on the 
                                 * next TX interrupt we will try sending from it. 
                                 */
				dev = move_dev_to_next(card,dev);
				break;
			}else{
				/* Tx buffers not available, but device set
                                 * the TX interrupt.  Set more_to_tx and try  
                                 * to transmit for other devices.
				 */
				more_to_tx=1;
				dev = move_dev_to_next(card,dev);
			}

		}else{
			/* This device was not set to transmit,
                         * go to next 
			 */
			dev = move_dev_to_next(card,dev);
		}	

		if (++i == card->u.x.no_dev){
			if (!more_to_tx){
				DBG_PRINTK(KERN_INFO "%s: Nothing to Send in TX INTR\n",
					card->devname);
			}
			break;
		}

	} //End of FOR

	card->u.x.tx_dev = dev;
	
	if (!more_to_tx){
		/* if any other interfaces have transmit interrupts pending, */
		/* do not disable the global transmit interrupt */
		if (!(--card->u.x.tx_interrupts_pending)){
			status->imask &= ~INTR_ON_TX_FRAME;
		}
	}
	return;
}

/*===============================================================
 * move_dev_to_next
 *  
 *
 *===============================================================*/


struct net_device *move_dev_to_next(sdla_t *card, struct net_device *dev)
{
	if (card->u.x.no_dev != 1){
		if (!*((struct net_device **)dev->priv))
			return card->wandev.dev;
		else
			return *((struct net_device **)dev->priv);
	}
	return dev;
}

/*===============================================================
 *  tx_intr_send
 *  
 *
 *===============================================================*/

static int tx_intr_send(sdla_t *card, struct net_device *dev)
{
	x25_channel_t* chan = dev->priv; 

	if (chan_send (dev,chan->transmit_buffer,chan->transmit_length,1)){
		 
                /* Packet was split up due to its size, do not disable
                 * tx_intr 
                 */
		return 1;
	}

	chan->transmit_length=0;
	atomic_set(&chan->common.driver_busy,0);
	chan->tx_offset=0;

	/* If we are in API mode, wakeup the 
         * sock BH handler, not the NET_BH */
	if (netif_queue_stopped(dev)){
		if (chan->common.usedby == API){
			netif_start_queue(dev);
			wakeup_sk_bh(dev);
		}else{
			netif_wake_queue(dev);
		}
	}
	return 0;
}


/*===============================================================
 * timer_intr
 *  
 * 	Timer interrupt handler.
 *	Check who called the timer interrupt and perform
 *      action accordingly.
 *
 *===============================================================*/

static void timer_intr (sdla_t *card)
{
	TX25Status* status = card->flags;

	if (card->u.x.timer_int_enabled & TMR_INT_ENABLED_CMD_EXEC){

		if (timer_intr_cmd_exec(card) == 0){
			card->u.x.timer_int_enabled &=
				~TMR_INT_ENABLED_CMD_EXEC;
		}

	}else  if(card->u.x.timer_int_enabled & TMR_INT_ENABLED_UDP_PKT) {

		if ((*card->u.x.hdlc_buf_status & 0x40) && 
		    card->u.x.udp_type == UDP_XPIPE_TYPE){

                    	if(process_udp_mgmt_pkt(card)) {
		                card->u.x.timer_int_enabled &= 
					~TMR_INT_ENABLED_UDP_PKT;
			}
		}

	}else if (card->u.x.timer_int_enabled & TMR_INT_ENABLED_POLL_ACTIVE) {

		struct net_device *dev = card->u.x.poll_device;
		x25_channel_t *chan = NULL;

		if (!dev){
			card->u.x.timer_int_enabled &= ~TMR_INT_ENABLED_POLL_ACTIVE;
			return;
		}
		chan = dev->priv;

		printk(KERN_INFO 
			"%s: Closing down Idle link %s on LCN %d\n",
					card->devname,chan->name,chan->common.lcn); 
		chan->i_timeout_sofar = jiffies;
		chan_disc(dev);	
         	card->u.x.timer_int_enabled &= ~TMR_INT_ENABLED_POLL_ACTIVE;
		card->u.x.poll_device=NULL;

	}else if (card->u.x.timer_int_enabled & TMR_INT_ENABLED_POLL_CONNECT_ON) {

		wanpipe_set_state(card, WAN_CONNECTED);
		if (card->u.x.LAPB_hdlc){
			struct net_device *dev = card->wandev.dev;
			set_chan_state(dev,WAN_CONNECTED);
			send_delayed_cmd_result(card,dev,card->mbox);	
		}

		/* 0x8F enable all interrupts */
		x25_set_intr_mode(card, INTR_ON_RX_FRAME|	
					INTR_ON_TX_FRAME|
					INTR_ON_MODEM_STATUS_CHANGE|
					//INTR_ON_COMMAND_COMPLETE|
					X25_ASY_TRANS_INTR_PENDING |
					INTR_ON_TIMER |
					DIRECT_RX_INTR_USAGE
				); 

		status->imask &= ~INTR_ON_TX_FRAME;	/* mask Tx interrupts */
		card->u.x.timer_int_enabled &= ~TMR_INT_ENABLED_POLL_CONNECT_ON;

	}else if (card->u.x.timer_int_enabled & TMR_INT_ENABLED_POLL_CONNECT_OFF) {

		//printk(KERN_INFO "Poll connect, Turning OFF\n");
		disconnect(card);
		card->u.x.timer_int_enabled &= ~TMR_INT_ENABLED_POLL_CONNECT_OFF;

	}else if (card->u.x.timer_int_enabled & TMR_INT_ENABLED_POLL_DISCONNECT) {

		//printk(KERN_INFO "POll disconnect, trying to connect\n");
		connect(card);
		card->u.x.timer_int_enabled &= ~TMR_INT_ENABLED_POLL_DISCONNECT;

	}else if (card->u.x.timer_int_enabled & TMR_INT_ENABLED_UPDATE){

		if (*card->u.x.hdlc_buf_status & 0x40){
			x25_get_err_stats(card);
			x25_get_stats(card);
			card->u.x.timer_int_enabled &= ~TMR_INT_ENABLED_UPDATE;
		}
	}

	if(!card->u.x.timer_int_enabled){
		//printk(KERN_INFO "Turning Timer Off \n");
                status->imask &= ~INTR_ON_TIMER;	
	}
}

/*====================================================================
 * 	Modem status interrupt handler.
 *===================================================================*/
static void status_intr (sdla_t* card)
{

	/* Added to avoid Modem status message flooding */
	static TX25ModemStatus last_stat;

	TX25Mbox* mbox = card->mbox;
	TX25ModemStatus *modem_status;
	struct net_device *dev;
	x25_channel_t *chan;
	int err;

	memset(&mbox->cmd, 0, sizeof(TX25Cmd));
	mbox->cmd.command = X25_READ_MODEM_STATUS;
	err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	if (err){ 
		x25_error(card, err, X25_READ_MODEM_STATUS, 0);
	}else{
	
		modem_status = (TX25ModemStatus*)mbox->data;	
	
           	/* Check if the last status was the same
           	 * if it was, do NOT print message again */
	
		if (last_stat.status != modem_status->status){

	     		printk(KERN_INFO "%s: Modem Status Change: DCD=%s, CTS=%s\n",
				card->devname,DCD(modem_status->status),CTS(modem_status->status));

			last_stat.status = modem_status->status;
		
			if (card->u.x.oob_on_modem){

				mbox->cmd.pktType = mbox->cmd.command;
				mbox->cmd.result = 0x08;

				/* Send a OOB to all connected sockets */
				for (dev = card->wandev.dev; dev;
				     dev = *((struct net_device**)dev->priv)) {
					chan=dev->priv;
					if (chan->common.usedby == API){
						send_oob_msg(card,dev,mbox);				
					}
				}

				/* The modem OOB message will probably kill the
				 * the link. If we don't clear the flag here,
				 * a deadlock could occur */ 
				if (atomic_read(&card->u.x.command_busy)){
					atomic_set(&card->u.x.command_busy,0);
				}
			}
		}
	}

	memset(&mbox->cmd, 0, sizeof(TX25Cmd));
	mbox->cmd.command = X25_HDLC_LINK_STATUS;
	err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	if (err){ 
		x25_error(card, err, X25_HDLC_LINK_STATUS, 0);
	}

}

/*====================================================================
 * 	Network event interrupt handler.
 *===================================================================*/
static void event_intr (sdla_t* card)
{
	x25_fetch_events(card);
}

/*====================================================================
 * 	Spurious interrupt handler.
 * 	o print a warning
 * 	o	 
 *====================================================================*/

static void spur_intr (sdla_t* card)
{
	printk(KERN_INFO "%s: spurious interrupt!\n", card->devname);
}


/*
 *	Background Polling Routines  
 */

/*====================================================================
 * 	Main polling routine.
 * 	This routine is repeatedly called by the WANPIPE 'thread' to allow for
 * 	time-dependent housekeeping work.
 *
 * 	Notes:
 * 	1. This routine may be called on interrupt context with all interrupts
 *    	enabled. Beware!
 *====================================================================*/

static void wpx_poll (sdla_t *card)
{
	if (!card->wandev.dev){
		goto wpx_poll_exit;
	}

	if (card->open_cnt != card->u.x.num_of_ch){
		goto wpx_poll_exit;
	}
	
	if (test_bit(PERI_CRIT,&card->wandev.critical)){
		goto wpx_poll_exit;
	}

	if (test_bit(SEND_CRIT,&card->wandev.critical)){
		goto wpx_poll_exit;
	}

	switch(card->wandev.state){
		case WAN_CONNECTED:
			poll_active(card);
			break;

		case WAN_CONNECTING:
			poll_connecting(card);
			break;

		case WAN_DISCONNECTED:
			poll_disconnected(card);
			break;
	}

wpx_poll_exit:
	clear_bit(POLL_CRIT,&card->wandev.critical);
	return;
}

static void trigger_x25_poll(sdla_t *card)
{
	schedule_work(&card->u.x.x25_poll_work);
}

/*====================================================================
 * 	Handle physical link establishment phase.
 * 	o if connection timed out, disconnect the link.
 *===================================================================*/

static void poll_connecting (sdla_t* card)
{
	volatile TX25Status* status = card->flags;

	if (status->gflags & X25_HDLC_ABM){

		timer_intr_exec (card, TMR_INT_ENABLED_POLL_CONNECT_ON);

	}else if ((jiffies - card->state_tick) > CONNECT_TIMEOUT){

		timer_intr_exec (card, TMR_INT_ENABLED_POLL_CONNECT_OFF);

	}
}

/*====================================================================
 * 	Handle physical link disconnected phase.
 * 	o if hold-down timeout has expired and there are open interfaces, 
 *	connect link.
 *===================================================================*/

static void poll_disconnected (sdla_t* card)
{
	struct net_device *dev; 
	x25_channel_t *chan;
	TX25Status* status = card->flags;

	if (!card->u.x.LAPB_hdlc && card->open_cnt && 
	    ((jiffies - card->state_tick) > HOLD_DOWN_TIME)){
		timer_intr_exec(card, TMR_INT_ENABLED_POLL_DISCONNECT);
	}


	if ((dev=card->wandev.dev) == NULL)
		return;

	if ((chan=dev->priv) == NULL)
		return;

	if (chan->common.usedby == API && 
	    atomic_read(&chan->common.command) && 
	    card->u.x.LAPB_hdlc){

		if (!(card->u.x.timer_int_enabled & TMR_INT_ENABLED_CMD_EXEC)) 
			card->u.x.timer_int_enabled |= TMR_INT_ENABLED_CMD_EXEC;

		if (!(status->imask & INTR_ON_TIMER))
			status->imask |= INTR_ON_TIMER;
	}	

}

/*====================================================================
 * 	Handle active link phase.
 * 	o fetch X.25 asynchronous events.
 * 	o kick off transmission on all interfaces.
 *===================================================================*/

static void poll_active (sdla_t* card)
{
	struct net_device* dev;
	TX25Status* status = card->flags;

	for (dev = card->wandev.dev; dev;
	     dev = *((struct net_device **)dev->priv)){
		x25_channel_t* chan = dev->priv;

		/* If SVC has been idle long enough, close virtual circuit */
		if ( chan->common.svc && 
		     chan->common.state == WAN_CONNECTED &&
		     chan->common.usedby == WANPIPE ){
		
			if( (jiffies - chan->i_timeout_sofar) / HZ > chan->idle_timeout ){
				/* Close svc */
				card->u.x.poll_device=dev;
				timer_intr_exec	(card, TMR_INT_ENABLED_POLL_ACTIVE);
			}
		}

#ifdef PRINT_DEBUG
		chan->ifstats.tx_compressed = atomic_read(&chan->common.command);
		chan->ifstats.tx_errors = chan->common.state;
		chan->ifstats.rx_fifo_errors = atomic_read(&card->u.x.command_busy);
		++chan->ifstats.tx_bytes;

		chan->ifstats.rx_fifo_errors=atomic_read(&chan->common.disconnect);
		chan->ifstats.multicast=atomic_read(&chan->bh_buff_used);
		chan->ifstats.rx_length_errors=*card->u.x.hdlc_buf_status;
#endif	

		if (chan->common.usedby == API && 
		    atomic_read(&chan->common.command) && 
	            !card->u.x.LAPB_hdlc){

			if (!(card->u.x.timer_int_enabled & TMR_INT_ENABLED_CMD_EXEC)) 
				card->u.x.timer_int_enabled |= TMR_INT_ENABLED_CMD_EXEC;

			if (!(status->imask & INTR_ON_TIMER))
				status->imask |= INTR_ON_TIMER;
		}	

		if ((chan->common.usedby == API) && 
		     atomic_read(&chan->common.disconnect)){

			if (chan->common.state == WAN_DISCONNECTED){
				atomic_set(&chan->common.disconnect,0);
				return;
			}

			atomic_set(&chan->common.command,X25_CLEAR_CALL);
			if (!(card->u.x.timer_int_enabled & TMR_INT_ENABLED_CMD_EXEC)) 
				card->u.x.timer_int_enabled |= TMR_INT_ENABLED_CMD_EXEC;

			if (!(status->imask & INTR_ON_TIMER))
				status->imask |= INTR_ON_TIMER;
		}
	}
}

static void timer_intr_exec(sdla_t *card, unsigned char TYPE)
{
	TX25Status* status = card->flags;
	card->u.x.timer_int_enabled |= TYPE;
	if (!(status->imask & INTR_ON_TIMER))
		status->imask |= INTR_ON_TIMER;
}


/*==================================================================== 
 * SDLA Firmware-Specific Functions 
 *
 *  Almost all X.25 commands can unexpetedly fail due to so called 'X.25
 *  asynchronous events' such as restart, interrupt, incoming call request,
 *  call clear request, etc.  They can't be ignored and have to be delt with
 *  immediately.  To tackle with this problem we execute each interface 
 *  command in a loop until good return code is received or maximum number 
 *  of retries is reached.  Each interface command returns non-zero return 
 *  code, an asynchronous event/error handler x25_error() is called.
 *====================================================================*/

/*====================================================================
 * 	Read X.25 firmware version.
 *		Put code version as ASCII string in str. 
 *===================================================================*/

static int x25_get_version (sdla_t* card, char* str)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_READ_CODE_VERSION;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- &&
		 x25_error(card, err, X25_READ_CODE_VERSION, 0));

	if (!err && str)
	{
		int len = mbox->cmd.length;

		memcpy(str, mbox->data, len);
		str[len] = '\0';
	}
	return err;
}

/*====================================================================
 * 	Configure adapter.
 *===================================================================*/

static int x25_configure (sdla_t* card, TX25Config* conf)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		memcpy(mbox->data, (void*)conf, sizeof(TX25Config));
		mbox->cmd.length  = sizeof(TX25Config);
		mbox->cmd.command = X25_SET_CONFIGURATION;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_SET_CONFIGURATION, 0));
	return err;
}

/*====================================================================
 * 	Configure adapter for HDLC only.
 *===================================================================*/

static int hdlc_configure (sdla_t* card, TX25Config* conf)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		memcpy(mbox->data, (void*)conf, sizeof(TX25Config));
		mbox->cmd.length  = sizeof(TX25Config);
		mbox->cmd.command = X25_HDLC_SET_CONFIG;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_SET_CONFIGURATION, 0));

	return err;
}

static int set_hdlc_level (sdla_t* card)
{

	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = SET_PROTOCOL_LEVEL;
		mbox->cmd.length = 1;
		mbox->data[0] = HDLC_LEVEL; //| DO_HDLC_LEVEL_ERROR_CHECKING; 	
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, SET_PROTOCOL_LEVEL, 0));

	return err;
}



/*====================================================================
 * Get communications error statistics.
 *====================================================================*/

static int x25_get_err_stats (sdla_t* card)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_HDLC_READ_COMM_ERR;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_HDLC_READ_COMM_ERR, 0));
	
	if (!err)
	{
		THdlcCommErr* stats = (void*)mbox->data;

		card->wandev.stats.rx_over_errors    = stats->rxOverrun;
		card->wandev.stats.rx_crc_errors     = stats->rxBadCrc;
		card->wandev.stats.rx_missed_errors  = stats->rxAborted;
		card->wandev.stats.tx_aborted_errors = stats->txAborted;
	}
	return err;
}

/*====================================================================
 * 	Get protocol statistics.
 *===================================================================*/

static int x25_get_stats (sdla_t* card)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_READ_STATISTICS;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_READ_STATISTICS, 0)) ;
	
	if (!err)
	{
		TX25Stats* stats = (void*)mbox->data;

		card->wandev.stats.rx_packets = stats->rxData;
		card->wandev.stats.tx_packets = stats->txData;
	}
	return err;
}

/*====================================================================
 * 	Close HDLC link.
 *===================================================================*/

static int x25_close_hdlc (sdla_t* card)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_HDLC_LINK_CLOSE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_HDLC_LINK_CLOSE, 0));
	
	return err;
}


/*====================================================================
 * 	Open HDLC link.
 *===================================================================*/

static int x25_open_hdlc (sdla_t* card)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_HDLC_LINK_OPEN;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_HDLC_LINK_OPEN, 0));

	return err;
}

/*=====================================================================
 * Setup HDLC link.
 *====================================================================*/
static int x25_setup_hdlc (sdla_t* card)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_HDLC_LINK_SETUP;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_HDLC_LINK_SETUP, 0));
	
	return err;
}

/*====================================================================
 * Set (raise/drop) DTR.
 *===================================================================*/

static int x25_set_dtr (sdla_t* card, int dtr)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->data[0] = 0;
		mbox->data[2] = 0;
		mbox->data[1] = dtr ? 0x02 : 0x01;
		mbox->cmd.length  = 3;
		mbox->cmd.command = X25_SET_GLOBAL_VARS;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_SET_GLOBAL_VARS, 0));
	
	return err;
}

/*====================================================================
 * 	Set interrupt mode.
 *===================================================================*/

static int x25_set_intr_mode (sdla_t* card, int mode)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->data[0] = mode;
		if (card->hw.fwid == SFID_X25_508){
			mbox->data[1] = card->hw.irq;
			mbox->data[2] = 2;
			mbox->cmd.length = 3;
		}else {
		 	mbox->cmd.length  = 1;
		}
		mbox->cmd.command = X25_SET_INTERRUPT_MODE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_SET_INTERRUPT_MODE, 0));
	
	return err;
}

/*====================================================================
 * 	Read X.25 channel configuration.
 *===================================================================*/

static int x25_get_chan_conf (sdla_t* card, x25_channel_t* chan)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int lcn = chan->common.lcn;
	int err;

	do{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.lcn     = lcn;
		mbox->cmd.command = X25_READ_CHANNEL_CONFIG;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_READ_CHANNEL_CONFIG, lcn));

	if (!err)
	{
		TX25Status* status = card->flags;

		/* calculate an offset into the array of status bytes */
		if (card->u.x.hi_svc <= X25_MAX_CHAN){ 

			chan->ch_idx = lcn - 1;

		}else{
			int offset;

			/* FIX: Apr 14 2000 : Nenad Corbic
			 * The data field was being compared to 0x1F using
                         * '&&' instead of '&'. 
			 * This caused X25API to fail for LCNs greater than 255.
			 */
			switch (mbox->data[0] & 0x1F)
			{
				case 0x01: 
					offset = status->pvc_map; break;
				case 0x03: 
					offset = status->icc_map; break;
				case 0x07: 
					offset = status->twc_map; break;
				case 0x0B: 
					offset = status->ogc_map; break;
				default: 
					offset = 0;
			}
			chan->ch_idx = lcn - 1 - offset;
		}

		/* get actual transmit packet size on this channel */
		switch(mbox->data[1] & 0x38)
		{
			case 0x00: 
				chan->tx_pkt_size = 16; 
				break;
			case 0x08: 
				chan->tx_pkt_size = 32; 
				break;
			case 0x10: 
				chan->tx_pkt_size = 64; 
				break;
			case 0x18: 
				chan->tx_pkt_size = 128; 
				break;
			case 0x20: 
				chan->tx_pkt_size = 256; 
				break;
			case 0x28: 
				chan->tx_pkt_size = 512; 
				break;
			case 0x30: 
				chan->tx_pkt_size = 1024; 
				break;
		}
		if (card->u.x.logging)
			printk(KERN_INFO "%s: X.25 packet size on LCN %d is %d.\n",
				card->devname, lcn, chan->tx_pkt_size);
	}
	return err;
}

/*====================================================================
 * 	Place X.25 call.
 *====================================================================*/

static int x25_place_call (sdla_t* card, x25_channel_t* chan)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;
	char str[64];


	if (chan->protocol == htons(ETH_P_IP)){
		sprintf(str, "-d%s -uCC", chan->addr);
	
	}else if (chan->protocol == htons(ETH_P_IPX)){
		sprintf(str, "-d%s -u800000008137", chan->addr);
	
	}
	
	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		strcpy(mbox->data, str);
		mbox->cmd.length  = strlen(str);
		mbox->cmd.command = X25_PLACE_CALL;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_PLACE_CALL, 0));

	if (!err){
		bind_lcn_to_dev (card, chan->dev, mbox->cmd.lcn);
	}
	return err;
}

/*====================================================================
 * 	Accept X.25 call.
 *====================================================================*/

static int x25_accept_call (sdla_t* card, int lcn, int qdm)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.lcn     = lcn;
		mbox->cmd.qdm     = qdm;
		mbox->cmd.command = X25_ACCEPT_CALL;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_ACCEPT_CALL, lcn));
	
	return err;
}

/*====================================================================
 * 	Clear X.25 call.
 *====================================================================*/

static int x25_clear_call (sdla_t* card, int lcn, int cause, int diagn)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.lcn     = lcn;
		mbox->cmd.cause   = cause;
		mbox->cmd.diagn   = diagn;
		mbox->cmd.command = X25_CLEAR_CALL;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_CLEAR_CALL, lcn));
	
	return err;
}

/*====================================================================
 * 	Send X.25 data packet.
 *====================================================================*/

static int x25_send (sdla_t* card, int lcn, int qdm, int len, void* buf)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;
	unsigned char cmd;
		
	if (card->u.x.LAPB_hdlc)
		cmd = X25_HDLC_WRITE;
	else
		cmd = X25_WRITE;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		memcpy(mbox->data, buf, len);
		mbox->cmd.length  = len;
		mbox->cmd.lcn     = lcn;

		if (card->u.x.LAPB_hdlc){
			mbox->cmd.pf = qdm;
		}else{			
			mbox->cmd.qdm = qdm;
		}

		mbox->cmd.command = cmd;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, cmd , lcn));


	/* If buffers are busy the return code for LAPB HDLC is
         * 1. The above functions are looking for return code
         * of X25RES_NOT_READY if busy. */

	if (card->u.x.LAPB_hdlc && err == 1){
		err = X25RES_NOT_READY;
	}

	return err;
}

/*====================================================================
 * 	Fetch X.25 asynchronous events.
 *===================================================================*/

static int x25_fetch_events (sdla_t* card)
{
	TX25Status* status = card->flags;
	TX25Mbox* mbox = card->mbox;
	int err = 0;

	if (status->gflags & 0x20)
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_IS_DATA_AVAILABLE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
 		if (err) x25_error(card, err, X25_IS_DATA_AVAILABLE, 0);
	}
	return err;
}

/*====================================================================
 * 	X.25 asynchronous event/error handler.
 *		This routine is called each time interface command returns 
 *		non-zero return code to handle X.25 asynchronous events and 
 *		common errors. Return non-zero to repeat command or zero to 
 *		cancel it.
 *
 * 	Notes:
 * 	1. This function may be called recursively, as handling some of the
 *    	asynchronous events (e.g. call request) requires execution of the
 *    	interface command(s) that, in turn, may also return asynchronous
 *    	events.  To avoid re-entrancy problems we copy mailbox to dynamically
 *    	allocated memory before processing events.
 *====================================================================*/

static int x25_error (sdla_t* card, int err, int cmd, int lcn)
{
	int retry = 1;
	unsigned dlen = ((TX25Mbox*)card->mbox)->cmd.length;
	TX25Mbox* mb;

	mb = kmalloc(sizeof(TX25Mbox) + dlen, GFP_ATOMIC);
	if (mb == NULL)
	{
		printk(KERN_ERR "%s: x25_error() out of memory!\n",
			card->devname);
		return 0;
	}
	memcpy(mb, card->mbox, sizeof(TX25Mbox) + dlen);
	switch (err){

	case X25RES_ASYNC_PACKET:	/* X.25 asynchronous packet was received */

		mb->data[dlen] = '\0';

		switch (mb->cmd.pktType & 0x7F){

		case ASE_CALL_RQST:		/* incoming call */
			retry = incoming_call(card, cmd, lcn, mb);
			break;

		case ASE_CALL_ACCEPTED:		/* connected */
			retry = call_accepted(card, cmd, lcn, mb);
			break;

		case ASE_CLEAR_RQST:		/* call clear request */
			retry = call_cleared(card, cmd, lcn, mb);
			break;

		case ASE_RESET_RQST:		/* reset request */
			printk(KERN_INFO "%s: X.25 reset request on LCN %d! "
				"Cause:0x%02X Diagn:0x%02X\n",
				card->devname, mb->cmd.lcn, mb->cmd.cause,
				mb->cmd.diagn);
			api_oob_event (card,mb);
			break;

		case ASE_RESTART_RQST:		/* restart request */
			retry = restart_event(card, cmd, lcn, mb);
			break;

		case ASE_CLEAR_CONFRM:
			if (clear_confirm_event (card,mb))
				break;

			/* I use the goto statement here so if 
	                 * somebody inserts code between the
        	         * case and default, we will not have
                	 * ghost problems */

			goto dflt_1;

		default:
dflt_1:
			printk(KERN_INFO "%s: X.25 event 0x%02X on LCN %d! "
				"Cause:0x%02X Diagn:0x%02X\n",
				card->devname, mb->cmd.pktType,
				mb->cmd.lcn, mb->cmd.cause, mb->cmd.diagn);
		}
		break;

	case X25RES_PROTO_VIOLATION:	/* X.25 protocol violation indication */

		/* Bug Fix: Mar 14 2000
                 * The Protocol violation error conditions were  
                 * not handled previously */

		switch (mb->cmd.pktType & 0x7F){

		case PVE_CLEAR_RQST:	/* Clear request */		
			retry = call_cleared(card, cmd, lcn, mb);
			break;	

		case PVE_RESET_RQST:	/* Reset request */
			printk(KERN_INFO "%s: X.25 reset request on LCN %d! "
				"Cause:0x%02X Diagn:0x%02X\n",
				card->devname, mb->cmd.lcn, mb->cmd.cause,
				mb->cmd.diagn);
			api_oob_event (card,mb);
			break;

		case PVE_RESTART_RQST:	/* Restart request */
			retry = restart_event(card, cmd, lcn, mb);
			break;

		default :
			printk(KERN_INFO
				"%s: X.25 protocol violation on LCN %d! "
				"Packet:0x%02X Cause:0x%02X Diagn:0x%02X\n",
				card->devname, mb->cmd.lcn,
				mb->cmd.pktType & 0x7F, mb->cmd.cause, mb->cmd.diagn);
			api_oob_event(card,mb);
		}
		break;

	case 0x42:	/* X.25 timeout */
		retry = timeout_event(card, cmd, lcn, mb);
		break;

	case 0x43:	/* X.25 retry limit exceeded */
		printk(KERN_INFO
			"%s: exceeded X.25 retry limit on LCN %d! "
			"Packet:0x%02X Diagn:0x%02X\n", card->devname,
			mb->cmd.lcn, mb->cmd.pktType, mb->cmd.diagn)
		;
		break;

	case 0x08:	/* modem failure */
#ifndef MODEM_NOT_LOG
		printk(KERN_INFO "%s: modem failure!\n", card->devname);
#endif /* MODEM_NOT_LOG */
		api_oob_event(card,mb);
		break;

	case 0x09:	/* N2 retry limit */
		printk(KERN_INFO "%s: exceeded HDLC retry limit!\n",
			card->devname);
		api_oob_event(card,mb);
		break;

	case 0x06:	/* unnumbered frame was received while in ABM */
		printk(KERN_INFO "%s: received Unnumbered frame 0x%02X!\n",
			card->devname, mb->data[0]);
		api_oob_event(card,mb);
		break;

	case CMD_TIMEOUT:
		printk(KERN_ERR "%s: command 0x%02X timed out!\n",
			card->devname, cmd)
		;
		retry = 0;	/* abort command */
		break;

	case X25RES_NOT_READY:
		retry = 1;
		break;

	case 0x01:
		if (card->u.x.LAPB_hdlc)
			break;

		if (mb->cmd.command == 0x16)
			break;
		/* I use the goto statement here so if 
                 * somebody inserts code between the
                 * case and default, we will not have
                 * ghost problems */
		goto dflt_2;

	default:
dflt_2:
		printk(KERN_INFO "%s: command 0x%02X returned 0x%02X! Lcn %i\n",
			card->devname, cmd, err, mb->cmd.lcn)
		;
		retry = 0;	/* abort command */
	}
	kfree(mb);
	return retry;
}

/*==================================================================== 
 *	X.25 Asynchronous Event Handlers
 * 	These functions are called by the x25_error() and should return 0, if
 * 	the command resulting in the asynchronous event must be aborted.
 *====================================================================*/



/*====================================================================
 *Handle X.25 incoming call request.
 *	RFC 1356 establishes the following rules:
 *	1. The first octet in the Call User Data (CUD) field of the call
 *     	   request packet contains NLPID identifying protocol encapsulation
 * 	2. Calls MUST NOT be accepted unless router supports requested
 *   	   protocol encapsulation.
 *	3. A diagnostic code 249 defined by ISO/IEC 8208 may be used 
 *	   when clearing a call because protocol encapsulation is not 
 *	   supported.
 *	4. If an incoming call is received while a call request is 
 *	   pending (i.e. call collision has occurred), the incoming call 
 *	   shall be rejected and call request shall be retried.
 *====================================================================*/

static int incoming_call (sdla_t* card, int cmd, int lcn, TX25Mbox* mb)
{
	struct wan_device* wandev = &card->wandev;
	int new_lcn = mb->cmd.lcn;
	struct net_device* dev = get_dev_by_lcn(wandev, new_lcn);
	x25_channel_t* chan = NULL;
	int accept = 0;		/* set to '1' if o.k. to accept call */
	unsigned int user_data;
	x25_call_info_t* info;
	
	/* Make sure there is no call collision */
	if (dev != NULL)
	{
		printk(KERN_INFO
			"%s: X.25 incoming call collision on LCN %d!\n",
			card->devname, new_lcn);

		x25_clear_call(card, new_lcn, 0, 0);
		return 1;
	}

	/* Make sure D bit is not set in call request */
//FIXME: THIS IS NOT TURE !!!! TAKE IT OUT
//	if (mb->cmd.qdm & 0x02)
//	{
//		printk(KERN_INFO
//			"%s: X.25 incoming call on LCN %d with D-bit set!\n",
//			card->devname, new_lcn);
//
//		x25_clear_call(card, new_lcn, 0, 0);
//		return 1;
//	}

	/* Parse call request data */
	info = kmalloc(sizeof(x25_call_info_t), GFP_ATOMIC);
	if (info == NULL)
	{
		printk(KERN_ERR
			"%s: not enough memory to parse X.25 incoming call "
			"on LCN %d!\n", card->devname, new_lcn);
		x25_clear_call(card, new_lcn, 0, 0);
		return 1;
	}
 
	parse_call_info(mb->data, info);

	if (card->u.x.logging)
		printk(KERN_INFO "\n%s: X.25 incoming call on LCN %d!\n",
			card->devname, new_lcn);

	/* Conver the first two ASCII characters into an
         * interger. Used to check the incoming protocol 
         */
	user_data = hex_to_uint(info->user,2);

	/* Find available channel */
	for (dev = wandev->dev; dev; dev = *((struct net_device **)dev->priv)) {
		chan = dev->priv;

		if (chan->common.usedby == API)
			continue;

		if (!chan->common.svc || (chan->common.state != WAN_DISCONNECTED))
			continue;

		if (user_data == NLPID_IP && chan->protocol != htons(ETH_P_IP)){
			printk(KERN_INFO "IP packet but configured for IPX : %x, %x\n",
				       htons(chan->protocol), info->user[0]);
			continue;
		}
	
		if (user_data == NLPID_SNAP && chan->protocol != htons(ETH_P_IPX)){
			printk(KERN_INFO "IPX packet but configured for IP: %x\n",
				       htons(chan->protocol));
			continue;
		}
		if (strcmp(info->src, chan->addr) == 0)
			break;

	        /* If just an '@' is specified, accept all incoming calls */
	        if (strcmp(chan->addr, "") == 0)
	                break;
	}

	if (dev == NULL){

		/* If the call is not for any WANPIPE interfaces
                 * check to see if there is an API listening queue
                 * waiting for data. If there is send the packet
                 * up the stack.
                 */
		if (card->sk != NULL && card->func != NULL){
			if (api_incoming_call(card,mb,new_lcn)){
				x25_clear_call(card, new_lcn, 0, 0);
			}
			accept = 0;
		}else{
			printk(KERN_INFO "%s: no channels available!\n",
				card->devname);
			
			x25_clear_call(card, new_lcn, 0, 0);
		}

	}else if (info->nuser == 0){

		printk(KERN_INFO
			"%s: no user data in incoming call on LCN %d!\n",
			card->devname, new_lcn)
		;
		x25_clear_call(card, new_lcn, 0, 0);

	}else switch (info->user[0]){

		case 0:		/* multiplexed */
			chan->protocol = htons(0);
			accept = 1;
			break;

		case NLPID_IP:	/* IP datagrams */
			accept = 1;
			break;

		case NLPID_SNAP: /* IPX datagrams */
			accept = 1;
			break;

		default:
			printk(KERN_INFO
				"%s: unsupported NLPID 0x%02X in incoming call "
				"on LCN %d!\n", card->devname, info->user[0], new_lcn);
			x25_clear_call(card, new_lcn, 0, 249);
	}
	
	if (accept && (x25_accept_call(card, new_lcn, 0) == CMD_OK)){

		bind_lcn_to_dev (card, chan->dev, new_lcn);
		
		if (x25_get_chan_conf(card, chan) == CMD_OK)
			set_chan_state(dev, WAN_CONNECTED);
		else 
			x25_clear_call(card, new_lcn, 0, 0);
	}
	kfree(info);
	return 1;
}

/*====================================================================
 * 	Handle accepted call.
 *====================================================================*/

static int call_accepted (sdla_t* card, int cmd, int lcn, TX25Mbox* mb)
{
	unsigned new_lcn = mb->cmd.lcn;
	struct net_device* dev = find_channel(card, new_lcn);
	x25_channel_t* chan;

	if (dev == NULL){
		printk(KERN_INFO
			"%s: clearing orphaned connection on LCN %d!\n",
			card->devname, new_lcn);
		x25_clear_call(card, new_lcn, 0, 0);
		return 1;
	}

	if (card->u.x.logging)	
		printk(KERN_INFO "%s: X.25 call accepted on Dev %s and LCN %d!\n",
			card->devname, dev->name, new_lcn);

	/* Get channel configuration and notify router */
	chan = dev->priv;
	if (x25_get_chan_conf(card, chan) != CMD_OK)
	{
		x25_clear_call(card, new_lcn, 0, 0);
		return 1;
	}

	set_chan_state(dev, WAN_CONNECTED);

	if (chan->common.usedby == API){
		send_delayed_cmd_result(card,dev,mb);
		bind_lcn_to_dev (card, dev, new_lcn);
	}

	return 1;
}

/*====================================================================
 * 	Handle cleared call.
 *====================================================================*/

static int call_cleared (sdla_t* card, int cmd, int lcn, TX25Mbox* mb)
{
	unsigned new_lcn = mb->cmd.lcn;
	struct net_device* dev = find_channel(card, new_lcn);
	x25_channel_t *chan;
	unsigned char old_state;

	if (card->u.x.logging){
		printk(KERN_INFO "%s: X.25 clear request on LCN %d! Cause:0x%02X "
		"Diagn:0x%02X\n",
		card->devname, new_lcn, mb->cmd.cause, mb->cmd.diagn);
	}

	if (dev == NULL){ 
		printk(KERN_INFO "%s: X.25 clear request : No device for clear\n",
				card->devname);
		return 1;
	}

	chan=dev->priv;

	old_state = chan->common.state;

	set_chan_state(dev, WAN_DISCONNECTED);

	if (chan->common.usedby == API){

		switch (old_state){
		
		case WAN_CONNECTING:
			send_delayed_cmd_result(card,dev,mb);
			break;
		case WAN_CONNECTED:
			send_oob_msg(card,dev,mb);				
			break;
		}
	}
	
	return ((cmd == X25_WRITE) && (lcn == new_lcn)) ? 0 : 1;
}

/*====================================================================
 * 	Handle X.25 restart event.
 *====================================================================*/

static int restart_event (sdla_t* card, int cmd, int lcn, TX25Mbox* mb)
{
	struct wan_device* wandev = &card->wandev;
	struct net_device* dev;
	x25_channel_t *chan;
	unsigned char old_state;

	printk(KERN_INFO
		"%s: X.25 restart request! Cause:0x%02X Diagn:0x%02X\n",
		card->devname, mb->cmd.cause, mb->cmd.diagn);

	/* down all logical channels */
	for (dev = wandev->dev; dev; dev = *((struct net_device **)dev->priv)) {
		chan=dev->priv;
		old_state = chan->common.state;

		set_chan_state(dev, WAN_DISCONNECTED);

		if (chan->common.usedby == API){
			switch (old_state){
		
			case WAN_CONNECTING:
				send_delayed_cmd_result(card,dev,mb);
				break;
			case WAN_CONNECTED:
				send_oob_msg(card,dev,mb);				
				break;
			}
		}
	}
	return (cmd == X25_WRITE) ? 0 : 1;
}

/*====================================================================
 * Handle timeout event.
 *====================================================================*/

static int timeout_event (sdla_t* card, int cmd, int lcn, TX25Mbox* mb)
{
	unsigned new_lcn = mb->cmd.lcn;

	if (mb->cmd.pktType == 0x05)	/* call request time out */
	{
		struct net_device* dev = find_channel(card,new_lcn);

		printk(KERN_INFO "%s: X.25 call timed timeout on LCN %d!\n",
			card->devname, new_lcn);

		if (dev){
			x25_channel_t *chan = dev->priv;
			set_chan_state(dev, WAN_DISCONNECTED);

			if (chan->common.usedby == API){
				send_delayed_cmd_result(card,dev,card->mbox);
			}
		}
	}else{ 
		printk(KERN_INFO "%s: X.25 packet 0x%02X timeout on LCN %d!\n",
		card->devname, mb->cmd.pktType, new_lcn);
	}
	return 1;
}

/* 
 *	Miscellaneous 
 */

/*====================================================================
 * 	Establish physical connection.
 * 	o open HDLC and raise DTR
 *
 * 	Return:		0	connection established
 *			1	connection is in progress
 *			<0	error
 *===================================================================*/

static int connect (sdla_t* card)
{
	TX25Status* status = card->flags;

	if (x25_open_hdlc(card) || x25_setup_hdlc(card))
		return -EIO;

	wanpipe_set_state(card, WAN_CONNECTING);

	x25_set_intr_mode(card, INTR_ON_TIMER); 
	status->imask &= ~INTR_ON_TIMER;

	return 1;
}

/*
 * 	Tear down physical connection.
 * 	o close HDLC link
 * 	o drop DTR
 *
 * 	Return:		0
 *			<0	error
 */

static int disconnect (sdla_t* card)
{
	wanpipe_set_state(card, WAN_DISCONNECTED);
	x25_set_intr_mode(card, INTR_ON_TIMER);	/* disable all interrupt except timer */
	x25_close_hdlc(card);			/* close HDLC link */
	x25_set_dtr(card, 0);			/* drop DTR */
	return 0;
}

/*
 * Find network device by its channel number.
 */

static struct net_device* get_dev_by_lcn(struct wan_device* wandev,
					 unsigned lcn)
{
	struct net_device* dev;

	for (dev = wandev->dev; dev; dev = *((struct net_device **)dev->priv))
		if (((x25_channel_t*)dev->priv)->common.lcn == lcn) 
			break;
	return dev;
}

/*
 * 	Initiate connection on the logical channel.
 * 	o for PVC we just get channel configuration
 * 	o for SVCs place an X.25 call
 *
 * 	Return:		0	connected
 *			>0	connection in progress
 *			<0	failure
 */

static int chan_connect(struct net_device* dev)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;

	if (chan->common.svc && chan->common.usedby == WANPIPE){
		if (!chan->addr[0]){
			printk(KERN_INFO "%s: No Destination Address\n",
					card->devname);
			return -EINVAL;	/* no destination address */
		}
		printk(KERN_INFO "%s: placing X.25 call to %s ...\n",
			card->devname, chan->addr);

		if (x25_place_call(card, chan) != CMD_OK)
			return -EIO;

		set_chan_state(dev, WAN_CONNECTING);
		return 1;
	}else{
		if (x25_get_chan_conf(card, chan) != CMD_OK)
			return -EIO;

		set_chan_state(dev, WAN_CONNECTED);
	}
	return 0;
}

/*
 * 	Disconnect logical channel.
 * 	o if SVC then clear X.25 call
 */

static int chan_disc(struct net_device* dev)
{
	x25_channel_t* chan = dev->priv;

	if (chan->common.svc){ 
		x25_clear_call(chan->card, chan->common.lcn, 0, 0);

		/* For API we disconnect on clear
                 * confirmation. 
                 */
		if (chan->common.usedby == API)
			return 0;
	}

	set_chan_state(dev, WAN_DISCONNECTED);
	
	return 0;
}

/*
 * 	Set logical channel state.
 */

static void set_chan_state(struct net_device* dev, int state)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	unsigned long flags;

	save_flags(flags);
	cli();
	if (chan->common.state != state)
	{
		switch (state)
		{
			case WAN_CONNECTED:
				if (card->u.x.logging){
					printk (KERN_INFO 
						"%s: interface %s connected, lcn %i !\n", 
						card->devname, dev->name,chan->common.lcn);
				}
				*(unsigned short*)dev->dev_addr = htons(chan->common.lcn);
				chan->i_timeout_sofar = jiffies;

				/* LAPB is PVC Based */
				if (card->u.x.LAPB_hdlc)
					chan->common.svc=0;
				break;

			case WAN_CONNECTING:
				if (card->u.x.logging){
					printk (KERN_INFO 
						"%s: interface %s connecting, lcn %i ...\n", 
						card->devname, dev->name, chan->common.lcn);
				}
				break;

			case WAN_DISCONNECTED:
				if (card->u.x.logging){
					printk (KERN_INFO 
						"%s: interface %s disconnected, lcn %i !\n", 
						card->devname, dev->name,chan->common.lcn);
				}
				atomic_set(&chan->common.disconnect,0);
				
				if (chan->common.svc) {
					*(unsigned short*)dev->dev_addr = 0;
					card->u.x.svc_to_dev_map[(chan->common.lcn%X25_MAX_CHAN)]=NULL;
		                	chan->common.lcn = 0;
				}

				if (chan->transmit_length){
					chan->transmit_length=0;
					atomic_set(&chan->common.driver_busy,0);
					chan->tx_offset=0;
					if (netif_queue_stopped(dev)){
						netif_wake_queue(dev);
					}
				}
				atomic_set(&chan->common.command,0);
				break;

			case WAN_DISCONNECTING:
				if (card->u.x.logging){
					printk (KERN_INFO 
					"\n%s: interface %s disconnecting, lcn %i ...\n", 
					card->devname, dev->name,chan->common.lcn);
				}
				atomic_set(&chan->common.disconnect,0);
				break;
		}
		chan->common.state = state;
	}
	chan->state_tick = jiffies;
	restore_flags(flags);
}

/*
 * 	Send packet on a logical channel.
 *		When this function is called, tx_skb field of the channel data 
 *		space points to the transmit socket buffer.  When transmission 
 *		is complete, release socket buffer and reset 'tbusy' flag.
 *
 * 	Return:		0	- transmission complete
 *			1	- busy
 *
 * 	Notes:
 * 	1. If packet length is greater than MTU for this channel, we'll fragment
 *    	the packet into 'complete sequence' using M-bit.
 * 	2. When transmission is complete, an event notification should be issued
 *    	to the router.
 */

static int chan_send(struct net_device* dev, void* buff, unsigned data_len,
		     unsigned char tx_intr)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	TX25Status* status = card->flags;
	unsigned len=0, qdm=0, res=0, orig_len = 0;
	void *data;

	/* Check to see if channel is ready */
	if ((!(status->cflags[chan->ch_idx] & 0x40) && !card->u.x.LAPB_hdlc)  || 
             !(*card->u.x.hdlc_buf_status & 0x40)){ 
            
		if (!tx_intr){
			setup_for_delayed_transmit (dev, buff, data_len);
			return 0;
		}else{
			/* By returning 0 to tx_intr the packet will be dropped */
			++card->wandev.stats.tx_dropped;
			++chan->ifstats.tx_dropped;
			printk(KERN_INFO "%s: ERROR, Tx intr could not send, dropping %s:\n", 
				card->devname,dev->name);
			++chan->if_send_stat.if_send_bfr_not_passed_to_adptr;
			return 0;
		}
	}

	if (chan->common.usedby == API){
		/* Remove the API Header */
		x25api_hdr_t *api_data = (x25api_hdr_t *)buff;

		/* Set the qdm bits from the packet header 
                 * User has the option to set the qdm bits
                 */
		qdm = api_data->qdm;

		orig_len = len = data_len - sizeof(x25api_hdr_t);
		data = (unsigned char*)buff + sizeof(x25api_hdr_t);
	}else{
		data = buff;
		orig_len = len = data_len;
	}	

	if (tx_intr){
		/* We are in tx_intr, minus the tx_offset from 
                 * the total length. The tx_offset part of the
		 * data has already been sent. Also, move the 
		 * data pointer to proper offset location.
                 */
		len -= chan->tx_offset;
		data = (unsigned char*)data + chan->tx_offset;
	}
		
	/* Check if the packet length is greater than MTU
         * If YES: Cut the len to MTU and set the M bit 
         */
	if (len > chan->tx_pkt_size && !card->u.x.LAPB_hdlc){
		len = chan->tx_pkt_size;
		qdm |= M_BIT;		
	} 


	/* Pass only first three bits of the qdm byte to the send
         * routine. In case user sets any other bit which might
         * cause errors. 
         */

	switch(x25_send(card, chan->common.lcn, (qdm&0x07), len, data)){
		case 0x00:	/* success */
			chan->i_timeout_sofar = jiffies;

			dev->trans_start=jiffies;
			
			if ((qdm & M_BIT) && !card->u.x.LAPB_hdlc){
				if (!tx_intr){
					/* The M bit was set, which means that part of the
                                         * packet has been sent. Copy the packet into a buffer
				         * and set the offset to len, so on next tx_inter 
					 * the packet will be sent using the below offset.
					 */
					chan->tx_offset += len;

					++chan->ifstats.tx_packets;
					chan->ifstats.tx_bytes += len;
					
					if (chan->tx_offset < orig_len){
						setup_for_delayed_transmit (dev, buff, data_len);
					}
					res=0;
				}else{
					/* We are already in tx_inter, thus data is already
                                         * in the buffer. Update the offset and wait for
                                         * next tx_intr. We add on to the offset, since data can
                                         * be X number of times larger than max data size.
					 */
					++chan->ifstats.tx_packets;
					chan->ifstats.tx_bytes += len;
					
					++chan->if_send_stat.if_send_bfr_passed_to_adptr;
					chan->tx_offset += len;

					/* The user can set the qdm bit as well.
                                         * If the entire packet was sent and qdm is still
                                         * set, than it's the user who has set the M bit. In that,
                                         * case indicate that the packet was send by returning 
					 * 0 and wait for a new packet. Otherwise, wait for next
                                         * tx interrupt to send the rest of the packet */

					if (chan->tx_offset < orig_len){
						res=1;
					}else{	
						res=0;
					}
				}
			}else{
				++chan->ifstats.tx_packets;
				chan->ifstats.tx_bytes += len;
				++chan->if_send_stat.if_send_bfr_passed_to_adptr;
				res=0;
			}
			break;

		case 0x33:	/* Tx busy */
			if (tx_intr){
				printk(KERN_INFO "%s: Tx_intr: Big Error dropping packet %s\n",
						card->devname,dev->name);
				++chan->ifstats.tx_dropped;
				++card->wandev.stats.tx_dropped;
				++chan->if_send_stat.if_send_bfr_not_passed_to_adptr;
				res=0;
			}else{
				DBG_PRINTK(KERN_INFO 
					"%s: Send: Big Error should have tx: storring %s\n",
						card->devname,dev->name);
				setup_for_delayed_transmit (dev, buff, data_len);	
				res=1;
			}
			break;

		default:	/* failure */
			++chan->ifstats.tx_errors;
			if (tx_intr){
				printk(KERN_INFO "%s: Tx_intr: Failure to send, dropping %s\n",
					card->devname,dev->name);
				++chan->ifstats.tx_dropped;
				++card->wandev.stats.tx_dropped;
				++chan->if_send_stat.if_send_bfr_not_passed_to_adptr;
				res=0;
			}else{
				DBG_PRINTK(KERN_INFO "%s: Send: Failure to send !!!, storing %s\n",
					card->devname,dev->name);			
				setup_for_delayed_transmit (dev, buff, data_len);
				res=1;
			}
			break;	
	}
	return res;
}


/*
 * 	Parse X.25 call request data and fill x25_call_info_t structure.
 */

static void parse_call_info (unsigned char* str, x25_call_info_t* info)
{
	memset(info, 0, sizeof(x25_call_info_t));
	for (; *str; ++str)
	{
		int i;
		unsigned char ch;

		if (*str == '-') switch (str[1]) {

			/* Take minus 2 off the maximum size so that 
                         * last byte is 0. This way we can use string
                         * manipulaton functions on call information.
                         */

			case 'd':	/* destination address */
				for (i = 0; i < (MAX_X25_ADDR_SIZE-2); ++i){
					ch = str[2+i];
					if (isspace(ch)) break;
					info->dest[i] = ch;
				}
				break;

			case 's':	/* source address */
				for (i = 0; i < (MAX_X25_ADDR_SIZE-2); ++i){
					ch = str[2+i];
					if (isspace(ch)) break;
					info->src[i] = ch;
				}
				break;

			case 'u':	/* user data */
				for (i = 0; i < (MAX_X25_DATA_SIZE-2); ++i){
					ch = str[2+i];
					if (isspace(ch)) break;
					info->user[i] = ch; 
				}
				info->nuser = i;
				break;

			case 'f':	/* facilities */
				for (i = 0; i < (MAX_X25_FACL_SIZE-2); ++i){
					ch = str[2+i];
					if (isspace(ch)) break;
					info->facil[i] = ch;
				}
				info->nfacil = i;
				break;
		}
	}
}

/*
 * 	Convert line speed in bps to a number used by S502 code.
 */

static unsigned char bps_to_speed_code (unsigned long bps)
{
	unsigned char	number;

	if (bps <= 1200)        number = 0x01;
	else if (bps <= 2400)   number = 0x02;
	else if (bps <= 4800)   number = 0x03;
	else if (bps <= 9600)   number = 0x04;
	else if (bps <= 19200)  number = 0x05;
	else if (bps <= 38400)  number = 0x06;
	else if (bps <= 45000)  number = 0x07;
	else if (bps <= 56000)  number = 0x08;
	else if (bps <= 64000)  number = 0x09;
	else if (bps <= 74000)  number = 0x0A;
	else if (bps <= 112000) number = 0x0B;
	else if (bps <= 128000) number = 0x0C;
	else number = 0x0D;

	return number;
}

/*
 * 	Convert decimal string to unsigned integer.
 * 	If len != 0 then only 'len' characters of the string are converted.
 */

static unsigned int dec_to_uint (unsigned char* str, int len)
{
	unsigned val;

	if (!len) 
		len = strlen(str);

	for (val = 0; len && isdigit(*str); ++str, --len)
		val = (val * 10) + (*str - (unsigned)'0');
	
	return val;
}

/*
 * 	Convert hex string to unsigned integer.
 * 	If len != 0 then only 'len' characters of the string are conferted.
 */

static unsigned int hex_to_uint (unsigned char* str, int len)
{
	unsigned val, ch;

	if (!len) 
		len = strlen(str);

	for (val = 0; len; ++str, --len)
	{
		ch = *str;
		if (isdigit(ch))
			val = (val << 4) + (ch - (unsigned)'0');
		else if (isxdigit(ch))
			val = (val << 4) + ((ch & 0xDF) - (unsigned)'A' + 10);
		else break;
	}
	return val;
}


static int handle_IPXWAN(unsigned char *sendpacket, char *devname, unsigned char enable_IPX, unsigned long network_number, unsigned short proto)
{
	int i;

	if( proto == ETH_P_IPX) {
		/* It's an IPX packet */
		if(!enable_IPX) {
			/* Return 1 so we don't pass it up the stack. */
			return 1;
		}
	} else {
		/* It's not IPX so pass it up the stack.*/ 
		return 0;
	}

	if( sendpacket[16] == 0x90 &&
	    sendpacket[17] == 0x04)
	{
		/* It's IPXWAN  */

		if( sendpacket[2] == 0x02 &&
		    sendpacket[34] == 0x00)
		{
			/* It's a timer request packet */
			printk(KERN_INFO "%s: Received IPXWAN Timer Request packet\n",devname);

			/* Go through the routing options and answer no to every
			 * option except Unnumbered RIP/SAP
			 */
			for(i = 41; sendpacket[i] == 0x00; i += 5)
			{
				/* 0x02 is the option for Unnumbered RIP/SAP */
				if( sendpacket[i + 4] != 0x02)
				{
					sendpacket[i + 1] = 0;
				}
			}

			/* Skip over the extended Node ID option */
			if( sendpacket[i] == 0x04 )
			{
				i += 8;
			}

			/* We also want to turn off all header compression opt. 			 */ 
			for(; sendpacket[i] == 0x80 ;)
			{
				sendpacket[i + 1] = 0;
				i += (sendpacket[i + 2] << 8) + (sendpacket[i + 3]) + 4;
			}

			/* Set the packet type to timer response */
			sendpacket[34] = 0x01;

			printk(KERN_INFO "%s: Sending IPXWAN Timer Response\n",devname);
		}
		else if( sendpacket[34] == 0x02 )
		{
			/* This is an information request packet */
			printk(KERN_INFO "%s: Received IPXWAN Information Request packet\n",devname);

			/* Set the packet type to information response */
			sendpacket[34] = 0x03;

			/* Set the router name */
			sendpacket[51] = 'X';
			sendpacket[52] = 'T';
			sendpacket[53] = 'P';
			sendpacket[54] = 'I';
			sendpacket[55] = 'P';
			sendpacket[56] = 'E';
			sendpacket[57] = '-';
			sendpacket[58] = CVHexToAscii(network_number >> 28);
			sendpacket[59] = CVHexToAscii((network_number & 0x0F000000)>> 24);
			sendpacket[60] = CVHexToAscii((network_number & 0x00F00000)>> 20);
			sendpacket[61] = CVHexToAscii((network_number & 0x000F0000)>> 16);
			sendpacket[62] = CVHexToAscii((network_number & 0x0000F000)>> 12);
			sendpacket[63] = CVHexToAscii((network_number & 0x00000F00)>> 8);
			sendpacket[64] = CVHexToAscii((network_number & 0x000000F0)>> 4);
			sendpacket[65] = CVHexToAscii(network_number & 0x0000000F);
			for(i = 66; i < 99; i+= 1)
			{
				sendpacket[i] = 0;
			}

			printk(KERN_INFO "%s: Sending IPXWAN Information Response packet\n",devname);
		}
		else
		{
			printk(KERN_INFO "%s: Unknown IPXWAN packet!\n",devname);
			return 0;
		}

		/* Set the WNodeID to our network address */
		sendpacket[35] = (unsigned char)(network_number >> 24);
		sendpacket[36] = (unsigned char)((network_number & 0x00FF0000) >> 16);
		sendpacket[37] = (unsigned char)((network_number & 0x0000FF00) >> 8);
		sendpacket[38] = (unsigned char)(network_number & 0x000000FF);

		return 1;
	} else {
		/*If we get here it's an IPX-data packet, so it'll get passed up the stack.
		 */
		/* switch the network numbers */
		switch_net_numbers(sendpacket, network_number, 1);	
		return 0;
	}
}

/*
 *  	If incoming is 0 (outgoing)- if the net numbers is ours make it 0
 *  	if incoming is 1 - if the net number is 0 make it ours 
 */

static void switch_net_numbers(unsigned char *sendpacket, unsigned long network_number, unsigned char incoming)
{
	unsigned long pnetwork_number;

	pnetwork_number = (unsigned long)((sendpacket[6] << 24) + 
			  (sendpacket[7] << 16) + (sendpacket[8] << 8) + 
			  sendpacket[9]);
	

	if (!incoming) {
		/*If the destination network number is ours, make it 0 */
		if( pnetwork_number == network_number) {
			sendpacket[6] = sendpacket[7] = sendpacket[8] = 
					 sendpacket[9] = 0x00;
		}
	} else {
		/* If the incoming network is 0, make it ours */
		if( pnetwork_number == 0) {
			sendpacket[6] = (unsigned char)(network_number >> 24);
			sendpacket[7] = (unsigned char)((network_number & 
					 0x00FF0000) >> 16);
			sendpacket[8] = (unsigned char)((network_number & 
					 0x0000FF00) >> 8);
			sendpacket[9] = (unsigned char)(network_number & 
					 0x000000FF);
		}
	}


	pnetwork_number = (unsigned long)((sendpacket[18] << 24) + 
			  (sendpacket[19] << 16) + (sendpacket[20] << 8) + 
			  sendpacket[21]);
	
	
	if( !incoming ) {
		/* If the source network is ours, make it 0 */
		if( pnetwork_number == network_number) {
			sendpacket[18] = sendpacket[19] = sendpacket[20] = 
				 sendpacket[21] = 0x00;
		}
	} else {
		/* If the source network is 0, make it ours */
		if( pnetwork_number == 0 ) {
			sendpacket[18] = (unsigned char)(network_number >> 24);
			sendpacket[19] = (unsigned char)((network_number & 
					 0x00FF0000) >> 16);
			sendpacket[20] = (unsigned char)((network_number & 
					 0x0000FF00) >> 8);
			sendpacket[21] = (unsigned char)(network_number & 
					 0x000000FF);
		}
	}
} /* switch_net_numbers */




/********************* X25API SPECIFIC FUNCTIONS ****************/


/*===============================================================
 *  find_channel
 *
 *	Manages the lcn to device map. It increases performance
 *      because it eliminates the need to search through the link  
 *      list for a device which is bounded to a specific lcn.
 *
 *===============================================================*/


struct net_device *find_channel(sdla_t *card, unsigned lcn)
{
	if (card->u.x.LAPB_hdlc){

		return card->wandev.dev;

	}else{
		/* We don't know whether the incoming lcn
                 * is a PVC or an SVC channel. But we do know that
                 * the lcn cannot be for both the PVC and the SVC
                 * channel.

		 * If the lcn number is greater or equal to 255, 
                 * take the modulo 255 of that number. We only have
                 * 255 locations, thus higher numbers must be mapped
                 * to a number between 0 and 245. 

		 * We must separate pvc's and svc's since two don't
                 * have to be contiguous.  Meaning pvc's can start
                 * from 1 to 10 and svc's can start from 256 to 266.
                 * But 256%255 is 1, i.e. CONFLICT.
		 */


		/* Highest LCN number must be less or equal to 4096 */
		if ((lcn <= MAX_LCN_NUM) && (lcn > 0)){

			if (lcn < X25_MAX_CHAN){
				if (card->u.x.svc_to_dev_map[lcn])
					return card->u.x.svc_to_dev_map[lcn];

				if (card->u.x.pvc_to_dev_map[lcn])
					return card->u.x.pvc_to_dev_map[lcn];
			
			}else{
				int new_lcn = lcn%X25_MAX_CHAN;
				if (card->u.x.svc_to_dev_map[new_lcn])
					return card->u.x.svc_to_dev_map[new_lcn];

				if (card->u.x.pvc_to_dev_map[new_lcn])
					return card->u.x.pvc_to_dev_map[new_lcn];
			}
		}
		return NULL;
	}
}

void bind_lcn_to_dev(sdla_t *card, struct net_device *dev, unsigned lcn)
{
	x25_channel_t *chan = dev->priv;

	/* Modulo the lcn number by X25_MAX_CHAN (255)
	 * because the lcn number can be greater than 255 
         *
	 * We need to split svc and pvc since they don't have
         * to be contigous. 
	 */

	if (chan->common.svc){
		card->u.x.svc_to_dev_map[(lcn % X25_MAX_CHAN)] = dev;
	}else{
		card->u.x.pvc_to_dev_map[(lcn % X25_MAX_CHAN)] = dev;
	}
	chan->common.lcn = lcn;
}



/*===============================================================
 * x25api_bh 
 *
 *
 *==============================================================*/

static void x25api_bh(struct net_device* dev)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	struct sk_buff *skb;

	if (atomic_read(&chan->bh_buff_used) == 0){
		printk(KERN_INFO "%s: BH Buffer Empty in BH\n",
				card->devname);
		clear_bit(0, &chan->tq_working);
		return;
	}

	while (atomic_read(&chan->bh_buff_used)){

		/* If the sock is in the process of unlinking the
		 * driver from the socket, we must get out. 
		 * This never happends but is a sanity check. */
		if (test_bit(0,&chan->common.common_critical)){
			clear_bit(0, &chan->tq_working);
			return;
		}
		
		/* If LAPB HDLC, do not drop packets if socket is
                 * not connected.  Let the buffer fill up and
                 * turn off rx interrupt */
		if (card->u.x.LAPB_hdlc){
			if (chan->common.sk == NULL || chan->common.func == NULL){
				clear_bit(0, &chan->tq_working);			
				return;
			}
		}

		skb  = ((bh_data_t *)&chan->bh_head[chan->bh_read])->skb;

		if (skb == NULL){
			printk(KERN_INFO "%s: BH Skb empty for read %i\n",
					card->devname,chan->bh_read);
		}else{
			
			if (chan->common.sk == NULL || chan->common.func == NULL){
				printk(KERN_INFO "%s: BH: Socket disconnected, dropping\n",
						card->devname);
				dev_kfree_skb_any(skb);
				x25api_bh_cleanup(dev);
				++chan->ifstats.rx_dropped;
				++chan->rx_intr_stat.rx_intr_bfr_not_passed_to_stack;
				continue;
			}


			if (chan->common.func(skb,dev,chan->common.sk) != 0){
				/* Sock full cannot send, queue us for another
                                 * try 
				 */
				printk(KERN_INFO "%s: BH: !!! Packet failed to send !!!!! \n",
						card->devname);
				atomic_set(&chan->common.receive_block,1);
				return;
			}else{
				x25api_bh_cleanup(dev);
				++chan->rx_intr_stat.rx_intr_bfr_passed_to_stack;
			}
		}
	}	
	clear_bit(0, &chan->tq_working);

	return;
}

/*===============================================================
 * x25api_bh_cleanup 
 *
 *
 *==============================================================*/

static int x25api_bh_cleanup(struct net_device *dev)
{
	x25_channel_t* chan = dev->priv;
	sdla_t *card = chan->card;
	TX25Status* status = card->flags;


	((bh_data_t *)&chan->bh_head[chan->bh_read])->skb = NULL;

	if (chan->bh_read == MAX_BH_BUFF){
		chan->bh_read=0;
	}else{
		++chan->bh_read;	
	}

	/* If the Receive interrupt was off, it means
         * that we filled up our circular buffer. Check    
         * that we have space in the buffer. If so 
         * turn the RX interrupt back on. 
	 */
	if (!(status->imask & INTR_ON_RX_FRAME)){
		if (atomic_read(&chan->bh_buff_used) < (MAX_BH_BUFF+1)){
			printk(KERN_INFO "%s: BH: Turning on the interrupt\n",
					card->devname);
			status->imask |= INTR_ON_RX_FRAME;
		}
	}	

	atomic_dec(&chan->bh_buff_used);
	return 0;
}


/*===============================================================
 * bh_enqueue 
 *
 *
 *==============================================================*/

static int bh_enqueue(struct net_device *dev, struct sk_buff *skb)
{
	x25_channel_t* chan = dev->priv;
	sdla_t *card = chan->card;
	TX25Status* status = card->flags;

	if (atomic_read(&chan->bh_buff_used) == (MAX_BH_BUFF+1)){
		printk(KERN_INFO "%s: Bottom half buffer FULL\n",
				card->devname);
		return 1; 
	}

	((bh_data_t *)&chan->bh_head[chan->bh_write])->skb = skb;

	if (chan->bh_write == MAX_BH_BUFF){
		chan->bh_write=0;
	}else{
		++chan->bh_write;
	}

	atomic_inc(&chan->bh_buff_used);

	if (atomic_read(&chan->bh_buff_used) == (MAX_BH_BUFF+1)){
		printk(KERN_INFO "%s: Buffer is now full, Turning off RX Intr\n",
				card->devname);
		status->imask &= ~INTR_ON_RX_FRAME;
	}

	return 0;
}


/*===============================================================
 * timer_intr_cmd_exec
 *  
 *	Called by timer interrupt to execute a command
 *===============================================================*/

static int timer_intr_cmd_exec (sdla_t* card)
{
	struct net_device *dev;
	unsigned char more_to_exec=0;
	volatile x25_channel_t *chan=NULL;
	int i=0,bad_cmd=0,err=0;	

	if (card->u.x.cmd_dev == NULL){
		card->u.x.cmd_dev = card->wandev.dev;
	}

	dev = card->u.x.cmd_dev;

	for (;;){

		chan = dev->priv;
		
		if (atomic_read(&chan->common.command)){ 

			bad_cmd = check_bad_command(card,dev);

			if ((!chan->common.mbox || atomic_read(&chan->common.disconnect)) && 
			     !bad_cmd){

				/* Socket has died or exited, We must bring the
                                 * channel down before anybody else tries to 
                                 * use it */
				err = channel_disconnect(card,dev);
			}else{
			        err = execute_delayed_cmd(card, dev,
							 (mbox_cmd_t*)chan->common.mbox,
							  bad_cmd);
			}

			switch (err){

			case RETURN_RESULT:

				/* Return the result to the socket without
                                 * delay. NO_WAIT Command */	
				atomic_set(&chan->common.command,0);
				if (atomic_read(&card->u.x.command_busy))
					atomic_set(&card->u.x.command_busy,0);

				send_delayed_cmd_result(card,dev,card->mbox);

				more_to_exec=0;
				break;
			case DELAY_RESULT:
		
				/* Wait for the remote to respond, before
                                 * sending the result up to the socket.
                                 * WAIT command */
				if (atomic_read(&card->u.x.command_busy))
					atomic_set(&card->u.x.command_busy,0);
				
				atomic_set(&chan->common.command,0);
				more_to_exec=0;
				break;
			default:

				/* If command could not be executed for
                                 * some reason (i.e return code 0x33 busy)
                                 * set the more_to_exec bit which will
                                 * indicate that this command must be exectued
                                 * again during next timer interrupt 
				 */
				more_to_exec=1;
				if (atomic_read(&card->u.x.command_busy) == 0)
					atomic_set(&card->u.x.command_busy,1);
				break;
			}

			bad_cmd=0;

			/* If flags is set, there are no hdlc buffers,
                         * thus, wait for the next pass and try the
                         * same command again. Otherwise, start searching 
                         * from next device on the next pass. 
			 */
			if (!more_to_exec){
				dev = move_dev_to_next(card,dev);
			}
			break;
		}else{
			/* This device has nothing to execute,
                         * go to next. 
			 */
			if (atomic_read(&card->u.x.command_busy))
					atomic_set(&card->u.x.command_busy,0);
			dev = move_dev_to_next(card,dev);
		}	

		if (++i == card->u.x.no_dev){
			if (!more_to_exec){
				DBG_PRINTK(KERN_INFO "%s: Nothing to execute in Timer\n",
					card->devname);
				if (atomic_read(&card->u.x.command_busy)){
					atomic_set(&card->u.x.command_busy,0);
				}
			}
			break;
		}

	} //End of FOR

	card->u.x.cmd_dev = dev;
	
	if (more_to_exec){
		/* If more commands are pending, do not turn off timer 
                 * interrupt */
		return 1;
	}else{
		/* No more commands, turn off timer interrupt */
		return 0;
	}	
}

/*===============================================================
 * execute_delayed_cmd 
 *
 *	Execute an API command which was passed down from the
 *      sock.  Sock is very limited in which commands it can
 *      execute.  Wait and No Wait commands are supported.  
 *      Place Call, Clear Call and Reset wait commands, where
 *      Accept Call is a no_wait command.
 *
 *===============================================================*/

static int execute_delayed_cmd(sdla_t* card, struct net_device *dev,
			       mbox_cmd_t *usr_cmd, char bad_cmd)
{
	TX25Mbox* mbox = card->mbox;
	int err;
	x25_channel_t *chan = dev->priv;
	int delay=RETURN_RESULT;

	if (!(*card->u.x.hdlc_buf_status & 0x40) && !bad_cmd){
		return TRY_CMD_AGAIN;
	}

	/* This way a command is guaranteed to be executed for
         * a specific lcn, the network interface is bound to. */
	usr_cmd->cmd.lcn = chan->common.lcn;
	

	/* If channel is pvc, instead of place call
         * run x25_channel configuration. If running LAPB HDLC
         * enable communications. 
         */
	if ((!chan->common.svc) && (usr_cmd->cmd.command == X25_PLACE_CALL)){

		if (card->u.x.LAPB_hdlc){
			DBG_PRINTK(KERN_INFO "LAPB: Connecting\n");
			connect(card);
			set_chan_state(dev,WAN_CONNECTING);
			return DELAY_RESULT;
		}else{
			DBG_PRINTK(KERN_INFO "%s: PVC is CONNECTING\n",card->devname);
			if (x25_get_chan_conf(card, chan) == CMD_OK){
				set_chan_state(dev, WAN_CONNECTED);
			}else{ 
				set_chan_state(dev, WAN_DISCONNECTED);
			}
			return RETURN_RESULT;
		}
	}

	/* Copy the socket mbox command onto the board */

	memcpy(&mbox->cmd, &usr_cmd->cmd, sizeof(TX25Cmd));
	if (usr_cmd->cmd.length){
		memcpy(mbox->data, usr_cmd->data, usr_cmd->cmd.length);
	}

	/* Check if command is bad. We need to copy the cmd into
         * the buffer regardless since we return the, mbox to
         * the user */
	if (bad_cmd){
		mbox->cmd.result=0x01;
		return RETURN_RESULT;
	}

	err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;

	if (err != CMD_OK && err != X25RES_NOT_READY)
		x25_error(card, err, usr_cmd->cmd.command, usr_cmd->cmd.lcn);

	if (mbox->cmd.result == X25RES_NOT_READY){
		return TRY_CMD_AGAIN;
	}

	switch (mbox->cmd.command){

	case X25_PLACE_CALL:
		
		switch (mbox->cmd.result){

		case CMD_OK:

			/* Check if Place call is a wait command or a 
               	  	 * no wait command */
			if (atomic_read(&chan->common.command) & 0x80)
 				delay=RETURN_RESULT;
			else
				delay=DELAY_RESULT;
		

			DBG_PRINTK(KERN_INFO "\n%s: PLACE CALL Binding dev %s to lcn %i\n",
					card->devname,dev->name, mbox->cmd.lcn);
		
			bind_lcn_to_dev (card, dev, mbox->cmd.lcn);
			set_chan_state(dev, WAN_CONNECTING);
			break;


		default:
			delay=RETURN_RESULT;
			set_chan_state(dev, WAN_DISCONNECTED);
			break;
		}
		break;

	case X25_ACCEPT_CALL: 
		
		switch (mbox->cmd.result){

		case CMD_OK:

			DBG_PRINTK(KERN_INFO "\n%s: ACCEPT Binding dev %s to lcn %i\n",
				card->devname,dev->name,mbox->cmd.lcn);

			bind_lcn_to_dev (card, dev, mbox->cmd.lcn);

			if (x25_get_chan_conf(card, chan) == CMD_OK){

				set_chan_state(dev, WAN_CONNECTED);
				delay=RETURN_RESULT;

			}else{ 
				if (x25_clear_call(card, usr_cmd->cmd.lcn, 0, 0) == CMD_OK){
					/* if clear is successful, wait for clear confirm 
					 */ 
					delay=DELAY_RESULT;
				}else{
					/* Do not change the state here. If we fail 
					 * the accept the return code is send up 
					 *the stack, which will ether retry
                               	  	 * or clear the call 
					 */
					DBG_PRINTK(KERN_INFO 
						"%s: ACCEPT: STATE MAY BE CURRUPTED 2 !!!!!\n",
						card->devname);
					delay=RETURN_RESULT;
				}
			}
			break;


		case X25RES_ASYNC_PACKET:
			delay=TRY_CMD_AGAIN;
			break;

		default: 
			DBG_PRINTK(KERN_INFO "%s: ACCEPT FAILED\n",card->devname);
			if (x25_clear_call(card, usr_cmd->cmd.lcn, 0, 0) == CMD_OK){
				delay=DELAY_RESULT;
			}else{
				/* Do not change the state here. If we fail the accept. The
                                 * return code is send up the stack, which will ether retry
                                 * or clear the call */
				DBG_PRINTK(KERN_INFO 
					"%s: ACCEPT: STATE MAY BE CORRUPTED 1 !!!!!\n",
						card->devname);
				delay=RETURN_RESULT;
			}
		}
		break;

	case X25_CLEAR_CALL:

		switch (mbox->cmd.result){

		case CMD_OK:
			DBG_PRINTK(KERN_INFO 
					"CALL CLEAR OK: Dev %s Mbox Lcn %i  Chan Lcn %i\n",
					dev->name,mbox->cmd.lcn,chan->common.lcn);
			set_chan_state(dev, WAN_DISCONNECTING);
			delay = DELAY_RESULT;
			break;

		case X25RES_CHANNEL_IN_USE:
		case X25RES_ASYNC_PACKET:
			delay = TRY_CMD_AGAIN;
			break;
			
		case X25RES_LINK_NOT_IN_ABM:
		case X25RES_INVAL_LCN:
		case X25RES_INVAL_STATE:
			set_chan_state(dev, WAN_DISCONNECTED);
			delay = RETURN_RESULT;
			break;
		
		default:
			/* If command did not execute because of user
                         * fault, do not change the state. This will
                         * signal the socket that clear command failed.
                         * User can retry or close the socket.
                         * When socket gets killed, it will set the 
                         * chan->disconnect which will signal
                         * driver to clear the call */
			printk(KERN_INFO "%s: Clear Command Failed, Rc %x\n",
				card->devname,mbox->cmd.command); 
			delay = RETURN_RESULT;
		}
		break;
	}	

	return delay;
}

/*===============================================================
 * api_incoming_call 
 *
 *	Pass an incoming call request up the listening
 *      sock.  If the API sock is not listening reject the
 *      call.
 *
 *===============================================================*/

static int api_incoming_call (sdla_t* card, TX25Mbox *mbox, int lcn)
{
	struct sk_buff *skb;
	int len = sizeof(TX25Cmd)+mbox->cmd.length;

	if (alloc_and_init_skb_buf(card, &skb, len)){
		printk(KERN_INFO "%s: API incoming call, no memory\n",card->devname);
		return 1;
	}

	memcpy(skb_put(skb,len),&mbox->cmd,len);

	skb->mac.raw = skb->data;
	skb->protocol = htons(X25_PROT);
	skb->pkt_type = WAN_PACKET_ASYNC;

	if (card->func(skb,card->sk) < 0){
		printk(KERN_INFO "%s: MAJOR ERROR: Failed to send up place call \n",card->devname);
                dev_kfree_skb_any(skb);
		return 1;
	}

	return 0;
}

/*===============================================================
 * send_delayed_cmd_result
 *
 *	Wait commands like PLEACE CALL or CLEAR CALL must wait
 *      until the result arrives. This function passes
 *      the result to a waiting sock. 
 *
 *===============================================================*/
static void send_delayed_cmd_result(sdla_t *card, struct net_device *dev,
				    TX25Mbox* mbox)
{
	x25_channel_t *chan = dev->priv;
	mbox_cmd_t *usr_cmd = (mbox_cmd_t *)chan->common.mbox;
	struct sk_buff *skb;
	int len=sizeof(unsigned char);

	atomic_set(&chan->common.command,0);

	/* If the sock is in the process of unlinking the
	 * driver from the socket, we must get out. 
	 * This never happends but is a sanity check. */
	if (test_bit(0,&chan->common.common_critical)){
		return;
	}

	if (!usr_cmd || !chan->common.sk || !chan->common.func){
		DBG_PRINTK(KERN_INFO "Delay result: Sock not bounded sk: %u, func: %u, mbox: %u\n",
			(unsigned int)chan->common.sk,
			(unsigned int)chan->common.func,
			(unsigned int)usr_cmd); 
		return;
	}

	memcpy(&usr_cmd->cmd, &mbox->cmd, sizeof(TX25Cmd)); 
	if (mbox->cmd.length > 0){
		memcpy(usr_cmd->data, mbox->data, mbox->cmd.length);
	}

	if (alloc_and_init_skb_buf(card,&skb,len)){
		printk(KERN_INFO "Delay result: No sock buffers\n");
		return;
	}

	memcpy(skb_put(skb,len),&mbox->cmd.command,len);
	
	skb->mac.raw = skb->data;
	skb->pkt_type = WAN_PACKET_CMD;
			
	chan->common.func(skb,dev,chan->common.sk);
}

/*===============================================================
 * clear_confirm_event
 *
 * 	Pass the clear confirmation event up the sock. The
 *      API will disconnect only after the clear confirmation
 *      has been received. 
 *
 *      Depending on the state, clear confirmation could 
 *      be an OOB event, or a result of an API command.
 *===============================================================*/

static int clear_confirm_event (sdla_t *card, TX25Mbox* mb)
{
	struct net_device *dev;
	x25_channel_t *chan;
	unsigned char old_state;	

	dev = find_channel(card,mb->cmd.lcn);
	if (!dev){
		DBG_PRINTK(KERN_INFO "%s: *** GOT CLEAR BUT NO DEV %i\n",
				card->devname,mb->cmd.lcn);
		return 0;
	}

	chan=dev->priv;
	DBG_PRINTK(KERN_INFO "%s: GOT CLEAR CONFIRM %s:  Mbox lcn %i  Chan lcn %i\n",
			card->devname, dev->name, mb->cmd.lcn, chan->common.lcn);

	/* If not API fall through to default. 
	 * If API, send the result to a waiting
         * socket.
	 */
	
	old_state = chan->common.state;
	set_chan_state(dev, WAN_DISCONNECTED);

	if (chan->common.usedby == API){
		switch (old_state) {

		case WAN_DISCONNECTING:
		case WAN_CONNECTING:
			send_delayed_cmd_result(card,dev,mb);
			break;
		case WAN_CONNECTED:
			send_oob_msg(card,dev,mb);
			break;
		}
		return 1;
	}

	return 0;
}

/*===============================================================
 * send_oob_msg
 *
 *    Construct an NEM Message and pass it up the connected
 *    sock. If the sock is not bounded discard the NEM.
 *
 *===============================================================*/

static void send_oob_msg(sdla_t *card, struct net_device *dev, TX25Mbox *mbox)
{
	x25_channel_t *chan = dev->priv;
	mbox_cmd_t *usr_cmd = (mbox_cmd_t *)chan->common.mbox;
	struct sk_buff *skb;
	int len=sizeof(x25api_hdr_t)+mbox->cmd.length;
	x25api_t *api_hdr;

	/* If the sock is in the process of unlinking the
	 * driver from the socket, we must get out. 
	 * This never happends but is a sanity check. */
	if (test_bit(0,&chan->common.common_critical)){
		return;
	}

	if (!usr_cmd || !chan->common.sk || !chan->common.func){
		DBG_PRINTK(KERN_INFO "OOB MSG: Sock not bounded\n"); 
		return;
	}

	memcpy(&usr_cmd->cmd, &mbox->cmd, sizeof(TX25Cmd)); 
	if (mbox->cmd.length > 0){
		memcpy(usr_cmd->data, mbox->data, mbox->cmd.length);
	}

	if (alloc_and_init_skb_buf(card,&skb,len)){
		printk(KERN_INFO "%s: OOB MSG: No sock buffers\n",card->devname);
		return;
	}

	api_hdr = (x25api_t*)skb_put(skb,len); 
	api_hdr->hdr.pktType = mbox->cmd.pktType & 0x7F;
	api_hdr->hdr.qdm     = mbox->cmd.qdm;
	api_hdr->hdr.cause   = mbox->cmd.cause;
	api_hdr->hdr.diagn   = mbox->cmd.diagn;
	api_hdr->hdr.length  = mbox->cmd.length;
	api_hdr->hdr.result  = mbox->cmd.result;
	api_hdr->hdr.lcn     = mbox->cmd.lcn;

	if (mbox->cmd.length > 0){
		memcpy(api_hdr->data,mbox->data,mbox->cmd.length);
	}
	
	skb->mac.raw = skb->data;
	skb->pkt_type = WAN_PACKET_ERR;
			
	if (chan->common.func(skb,dev,chan->common.sk) < 0){
		if (bh_enqueue(dev,skb)){
			printk(KERN_INFO "%s: Dropping OOB MSG\n",card->devname);
                	dev_kfree_skb_any(skb);
		}
	}

	DBG_PRINTK(KERN_INFO "%s: OOB MSG OK, %s, lcn %i\n",
			card->devname, dev->name, mbox->cmd.lcn);
}	

/*===============================================================
 *  alloc_and_init_skb_buf 
 *
 *	Allocate and initialize an skb buffer. 
 *
 *===============================================================*/

static int alloc_and_init_skb_buf (sdla_t *card, struct sk_buff **skb, int len)
{
	struct sk_buff *new_skb = *skb;

	new_skb = dev_alloc_skb(len + X25_HRDHDR_SZ);
	if (new_skb == NULL){
		printk(KERN_INFO "%s: no socket buffers available!\n",
			card->devname);
		return 1;
	}

	if (skb_tailroom(new_skb) < len){
		/* No room for the packet. Call off the whole thing! */
                dev_kfree_skb_any(new_skb);
		printk(KERN_INFO "%s: Listen: unexpectedly long packet sequence\n"
			,card->devname);
		*skb = NULL;
		return 1;
	}

	*skb = new_skb;
	return 0;

}

/*===============================================================
 *  api_oob_event 
 *
 *	Send an OOB event up to the sock 
 *
 *===============================================================*/

static void api_oob_event (sdla_t *card,TX25Mbox *mbox)
{
	struct net_device *dev = find_channel(card, mbox->cmd.lcn);
	x25_channel_t *chan;

	if (!dev)
		return;

	chan=dev->priv;

	if (chan->common.usedby == API)
		send_oob_msg(card,dev,mbox);
	
}




static int channel_disconnect(sdla_t* card, struct net_device *dev)
{

	int err;
	x25_channel_t *chan = dev->priv;

	DBG_PRINTK(KERN_INFO "%s: TIMER: %s, Device down disconnecting\n",
				card->devname,dev->name);

	if (chan->common.svc){
		err = x25_clear_call(card,chan->common.lcn,0,0);
	}else{
		/* If channel is PVC or LAPB HDLC, there is no call
                 * to be cleared, thus drop down to the default
                 * area 
	         */
		err = 1;
	}

	switch (err){
	
		case X25RES_CHANNEL_IN_USE:	
		case X25RES_NOT_READY:
			err = TRY_CMD_AGAIN;
			break;
		case CMD_OK:
			DBG_PRINTK(KERN_INFO "CALL CLEAR OK: Dev %s Chan Lcn %i\n",
						dev->name,chan->common.lcn);

			set_chan_state(dev,WAN_DISCONNECTING);
			atomic_set(&chan->common.command,0);
			err = DELAY_RESULT;
			break;
		default:
			/* If LAPB HDLC protocol, bring the whole link down
                         * once the application terminates 
			 */

			set_chan_state(dev,WAN_DISCONNECTED);

			if (card->u.x.LAPB_hdlc){
				DBG_PRINTK(KERN_INFO "LAPB: Disconnecting Link\n");
				hdlc_link_down (card);
			}
			atomic_set(&chan->common.command,0);
			err = RETURN_RESULT;
			break;
	}

	return err;
}

static void hdlc_link_down (sdla_t *card)
{
	TX25Mbox* mbox = card->mbox;
	int retry = 5;
	int err=0;

	do {
		memset(mbox,0,sizeof(TX25Mbox));
		mbox->cmd.command = X25_HDLC_LINK_DISC;
		mbox->cmd.length = 1;
		mbox->data[0]=0;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;

	} while (err && retry-- && x25_error(card, err, X25_HDLC_LINK_DISC, 0));

	if (err)
		printk(KERN_INFO "%s: Hdlc Link Down Failed %x\n",card->devname,err);

	disconnect (card);
	
}

static int check_bad_command(sdla_t* card, struct net_device *dev)
{
	x25_channel_t *chan = dev->priv;
	int bad_cmd = 0;

	switch (atomic_read(&chan->common.command)&0x7F){

		case X25_PLACE_CALL:
			if (chan->common.state != WAN_DISCONNECTED)
				bad_cmd=1;
			break;
		case X25_CLEAR_CALL:
			if (chan->common.state == WAN_DISCONNECTED)
				bad_cmd=1;
			break;
		case X25_ACCEPT_CALL:
			if (chan->common.state != WAN_CONNECTING)
				bad_cmd=1;
			break;
		case X25_RESET:
			if (chan->common.state != WAN_CONNECTED)
				bad_cmd=1;
			break;
		default:
			bad_cmd=1;
			break;
	}

	if (bad_cmd){
		printk(KERN_INFO "%s: Invalid State, BAD Command %x, dev %s, lcn %i, st %i\n", 
			card->devname,atomic_read(&chan->common.command),dev->name, 
			chan->common.lcn, chan->common.state);
	}

	return bad_cmd;
}



/*************************** XPIPEMON FUNCTIONS **************************/

/*==============================================================================
 * Process UDP call of type XPIPE
 */

static int process_udp_mgmt_pkt(sdla_t *card)
{
	int            c_retry = MAX_CMD_RETRY;
	unsigned int   len;
	struct sk_buff *new_skb;
	TX25Mbox       *mbox = card->mbox;
	int            err;
	int            udp_mgmt_req_valid = 1;
	struct net_device *dev;
        x25_channel_t  *chan;
	unsigned short lcn;
	struct timeval tv;
	

	x25_udp_pkt_t *x25_udp_pkt;
	x25_udp_pkt = (x25_udp_pkt_t *)card->u.x.udp_pkt_data;

	dev = card->u.x.udp_dev;
	chan = dev->priv;
	lcn = chan->common.lcn;

	switch(x25_udp_pkt->cblock.command) {
            
		/* XPIPE_ENABLE_TRACE */
		case XPIPE_ENABLE_TRACING:

		/* XPIPE_GET_TRACE_INFO */
		case XPIPE_GET_TRACE_INFO:
 
		/* SET FT1 MODE */
		case XPIPE_SET_FT1_MODE:
           
			if(card->u.x.udp_pkt_src == UDP_PKT_FRM_NETWORK) {
                    		++chan->pipe_mgmt_stat.UDP_PIPE_mgmt_direction_err;
				udp_mgmt_req_valid = 0;
				break;
			}

		/* XPIPE_FT1_READ_STATUS */
		case XPIPE_FT1_READ_STATUS:

		/* FT1 MONITOR STATUS */
		case XPIPE_FT1_STATUS_CTRL:
			if(card->hw.fwid !=  SFID_X25_508) {
				++chan->pipe_mgmt_stat.UDP_PIPE_mgmt_adptr_type_err;
				udp_mgmt_req_valid = 0;
				break;
			}
		default:
			break;
       	}

	if(!udp_mgmt_req_valid) {
           	/* set length to 0 */
		x25_udp_pkt->cblock.length = 0;
		/* set return code */
		x25_udp_pkt->cblock.result = (card->hw.fwid != SFID_X25_508) ? 0x1F : 0xCD;
		
	} else {   
        
		switch (x25_udp_pkt->cblock.command) {
    
	
		case XPIPE_FLUSH_DRIVER_STATS:
			init_x25_channel_struct(chan);
			init_global_statistics(card);
			mbox->cmd.length = 0;
			break;


		case XPIPE_DRIVER_STAT_IFSEND:
			memcpy(x25_udp_pkt->data, &chan->if_send_stat, sizeof(if_send_stat_t));
			mbox->cmd.length = sizeof(if_send_stat_t);
			x25_udp_pkt->cblock.length =  mbox->cmd.length;	
			break;
	
		case XPIPE_DRIVER_STAT_INTR:
			memcpy(&x25_udp_pkt->data[0], &card->statistics, sizeof(global_stats_t));
                        memcpy(&x25_udp_pkt->data[sizeof(global_stats_t)],
                                &chan->rx_intr_stat, sizeof(rx_intr_stat_t));
			
			mbox->cmd.length = sizeof(global_stats_t) +
					sizeof(rx_intr_stat_t);
			x25_udp_pkt->cblock.length =  mbox->cmd.length;
			break;

		case XPIPE_DRIVER_STAT_GEN:
                        memcpy(x25_udp_pkt->data,
                                &chan->pipe_mgmt_stat.UDP_PIPE_mgmt_kmalloc_err,
                                sizeof(pipe_mgmt_stat_t));

                        memcpy(&x25_udp_pkt->data[sizeof(pipe_mgmt_stat_t)],
                               &card->statistics, sizeof(global_stats_t));

                        x25_udp_pkt->cblock.result = 0;
                        x25_udp_pkt->cblock.length = sizeof(global_stats_t)+
                                                     sizeof(rx_intr_stat_t);
                        mbox->cmd.length = x25_udp_pkt->cblock.length;
                        break;

		case XPIPE_ROUTER_UP_TIME:
			do_gettimeofday(&tv);
			chan->router_up_time = tv.tv_sec - chan->router_start_time;
    	                *(unsigned long *)&x25_udp_pkt->data = chan->router_up_time;	
			x25_udp_pkt->cblock.length = mbox->cmd.length = 4;
			x25_udp_pkt->cblock.result = 0;
			break;
	
		default :

			do {
				memcpy(&mbox->cmd, &x25_udp_pkt->cblock.command, sizeof(TX25Cmd));
				if(mbox->cmd.length){ 
					memcpy(&mbox->data, 
					       (char *)x25_udp_pkt->data, 
					       mbox->cmd.length);
				}	
		
				err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
			} while (err && c_retry-- && x25_error(card, err, mbox->cmd.command, 0));


			if ( err == CMD_OK || 
			    (err == 1 && 
			     (mbox->cmd.command == 0x06 || 
			      mbox->cmd.command == 0x16)  ) ){

				++chan->pipe_mgmt_stat.UDP_PIPE_mgmt_adptr_cmnd_OK;
			} else {
				++chan->pipe_mgmt_stat.UDP_PIPE_mgmt_adptr_cmnd_timeout;
			}

			  /* copy the result back to our buffer */
			memcpy(&x25_udp_pkt->cblock.command, &mbox->cmd, sizeof(TX25Cmd));

      	         	if(mbox->cmd.length) {
        	               memcpy(&x25_udp_pkt->data, &mbox->data, mbox->cmd.length);
			}
			break;

		} //switch

        }
    
        /* Fill UDP TTL */

	x25_udp_pkt->ip_pkt.ttl = card->wandev.ttl;
        len = reply_udp(card->u.x.udp_pkt_data, mbox->cmd.length);


        if(card->u.x.udp_pkt_src == UDP_PKT_FRM_NETWORK) {
		
		err = x25_send(card, lcn, 0, len, card->u.x.udp_pkt_data);
		if (!err) 
			++chan->pipe_mgmt_stat.UDP_PIPE_mgmt_adptr_send_passed;
		else
			++chan->pipe_mgmt_stat.UDP_PIPE_mgmt_adptr_send_failed;
	
	} else {

		/* Allocate socket buffer */
		if((new_skb = dev_alloc_skb(len)) != NULL) {
			void *buf;

			/* copy data into new_skb */
			buf = skb_put(new_skb, len);
			memcpy(buf, card->u.x.udp_pkt_data, len);
        
			/* Decapsulate packet and pass it up the protocol 
			   stack */
			new_skb->dev = dev;
	
			if (chan->common.usedby == API)
                        	new_skb->protocol = htons(X25_PROT);
			else 
				new_skb->protocol = htons(ETH_P_IP);
	
                        new_skb->mac.raw = new_skb->data;

			netif_rx(new_skb);
			++chan->pipe_mgmt_stat.UDP_PIPE_mgmt_passed_to_stack;
            	
		} else {
			++chan->pipe_mgmt_stat.UDP_PIPE_mgmt_no_socket;
			printk(KERN_INFO 
			"%s: UDP mgmt cmnd, no socket buffers available!\n", 
			card->devname);
            	}
        }

	card->u.x.udp_pkt_lgth = 0;

	return 1;
}


/*==============================================================================
 * Determine what type of UDP call it is. DRVSTATS or XPIPE8ND ?
 */
static int udp_pkt_type( struct sk_buff *skb, sdla_t* card )
{
	x25_udp_pkt_t *x25_udp_pkt = (x25_udp_pkt_t *)skb->data;

        if((x25_udp_pkt->ip_pkt.protocol == UDPMGMT_UDP_PROTOCOL) &&
		(x25_udp_pkt->ip_pkt.ver_inet_hdr_length == 0x45) &&
		(x25_udp_pkt->udp_pkt.udp_dst_port == ntohs(card->wandev.udp_port)) &&
		(x25_udp_pkt->wp_mgmt.request_reply == UDPMGMT_REQUEST)) {

                        if(!strncmp(x25_udp_pkt->wp_mgmt.signature,
                                UDPMGMT_XPIPE_SIGNATURE, 8)){
                                return UDP_XPIPE_TYPE;
			}else{
				printk(KERN_INFO "%s: UDP Packet, Failed Signature !\n",
					card->devname);
			}
	}

        return UDP_INVALID_TYPE;
}


/*============================================================================
 * Reply to UDP Management system.
 * Return nothing.
 */
static int reply_udp( unsigned char *data, unsigned int mbox_len ) 
{
	unsigned short len, udp_length, temp, ip_length;
	unsigned long ip_temp;
	int even_bound = 0;

  
	x25_udp_pkt_t *x25_udp_pkt = (x25_udp_pkt_t *)data; 

	/* Set length of packet */
	len = sizeof(ip_pkt_t)+ 
	      sizeof(udp_pkt_t)+
	      sizeof(wp_mgmt_t)+
	      sizeof(cblock_t)+
	      mbox_len;
 

	/* fill in UDP reply */
	x25_udp_pkt->wp_mgmt.request_reply = UDPMGMT_REPLY;
  
	/* fill in UDP length */
	udp_length = sizeof(udp_pkt_t)+ 
		     sizeof(wp_mgmt_t)+
		     sizeof(cblock_t)+
		     mbox_len; 


	/* put it on an even boundary */
	if ( udp_length & 0x0001 ) {
		udp_length += 1;
		len += 1;
		even_bound = 1;
	}

	temp = (udp_length<<8)|(udp_length>>8);
	x25_udp_pkt->udp_pkt.udp_length = temp;
	 
	/* swap UDP ports */
	temp = x25_udp_pkt->udp_pkt.udp_src_port;
	x25_udp_pkt->udp_pkt.udp_src_port = 
			x25_udp_pkt->udp_pkt.udp_dst_port; 
	x25_udp_pkt->udp_pkt.udp_dst_port = temp;



	/* add UDP pseudo header */
	temp = 0x1100;
	*((unsigned short *)
		(x25_udp_pkt->data+mbox_len+even_bound)) = temp;	
	temp = (udp_length<<8)|(udp_length>>8);
	*((unsigned short *)
		(x25_udp_pkt->data+mbox_len+even_bound+2)) = temp;
		 
	/* calculate UDP checksum */
	x25_udp_pkt->udp_pkt.udp_checksum = 0;

	x25_udp_pkt->udp_pkt.udp_checksum = 
		calc_checksum(&data[UDP_OFFSET], udp_length+UDP_OFFSET);

	/* fill in IP length */
	ip_length = len;
	temp = (ip_length<<8)|(ip_length>>8);
	x25_udp_pkt->ip_pkt.total_length = temp;
  
	/* swap IP addresses */
	ip_temp = x25_udp_pkt->ip_pkt.ip_src_address;
	x25_udp_pkt->ip_pkt.ip_src_address = 
				x25_udp_pkt->ip_pkt.ip_dst_address;
	x25_udp_pkt->ip_pkt.ip_dst_address = ip_temp;

		 
	/* fill in IP checksum */
	x25_udp_pkt->ip_pkt.hdr_checksum = 0;
	x25_udp_pkt->ip_pkt.hdr_checksum = calc_checksum(data, sizeof(ip_pkt_t));

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

/*=============================================================================
 * Store a UDP management packet for later processing.
 */

static int store_udp_mgmt_pkt(int udp_type, char udp_pkt_src, sdla_t* card,
			      struct net_device *dev, struct sk_buff *skb,
			      int lcn)
{
        int udp_pkt_stored = 0;

        if(!card->u.x.udp_pkt_lgth && (skb->len <= MAX_LGTH_UDP_MGNT_PKT)){
                card->u.x.udp_pkt_lgth = skb->len;
                card->u.x.udp_type = udp_type;
                card->u.x.udp_pkt_src = udp_pkt_src;
                card->u.x.udp_lcn = lcn;
		card->u.x.udp_dev = dev;
                memcpy(card->u.x.udp_pkt_data, skb->data, skb->len);
                card->u.x.timer_int_enabled |= TMR_INT_ENABLED_UDP_PKT;
                udp_pkt_stored = 1;

        }else{
                printk(KERN_INFO "%s: ERROR: UDP packet not stored for LCN %d\n", 
							card->devname,lcn);
	}

        if(udp_pkt_src == UDP_PKT_FRM_STACK){
                dev_kfree_skb_any(skb);
	}else{
                dev_kfree_skb_any(skb);
	}

        return(udp_pkt_stored);
}



/*=============================================================================
 * Initial the ppp_private_area structure.
 */
static void init_x25_channel_struct( x25_channel_t *chan )
{
	memset(&chan->if_send_stat.if_send_entry,0,sizeof(if_send_stat_t));
	memset(&chan->rx_intr_stat.rx_intr_no_socket,0,sizeof(rx_intr_stat_t));
	memset(&chan->pipe_mgmt_stat.UDP_PIPE_mgmt_kmalloc_err,0,sizeof(pipe_mgmt_stat_t));
}

/*============================================================================
 * Initialize Global Statistics
 */
static void init_global_statistics( sdla_t *card )
{
	memset(&card->statistics.isr_entry,0,sizeof(global_stats_t));
}


/*===============================================================
 * SMP Support
 * ==============================================================*/

static void S508_S514_lock(sdla_t *card, unsigned long *smp_flags)
{
	spin_lock_irqsave(&card->wandev.lock, *smp_flags);
}
static void S508_S514_unlock(sdla_t *card, unsigned long *smp_flags)
{
	spin_unlock_irqrestore(&card->wandev.lock, *smp_flags);
}

/*===============================================================
 * x25_timer_routine
 *
 * 	A more efficient polling routine.  Each half a second
 * 	queue a polling task. We want to do the polling in a 
 * 	task not timer, because timer runs in interrupt time.
 *
 * 	FIXME Polling should be rethinked.
 *==============================================================*/

static void x25_timer_routine(unsigned long data)
{
	sdla_t *card = (sdla_t*)data;

	if (!card->wandev.dev){
		printk(KERN_INFO "%s: Stopping the X25 Poll Timer: No Dev.\n",
				card->devname);
		return;
	}

	if (card->open_cnt != card->u.x.num_of_ch){
		printk(KERN_INFO "%s: Stopping the X25 Poll Timer: Interface down.\n",
				card->devname);
		return;
	}

	if (test_bit(PERI_CRIT,&card->wandev.critical)){
		printk(KERN_INFO "%s: Stopping the X25 Poll Timer: Shutting down.\n",
				card->devname);
		return;
	}
	
	if (!test_and_set_bit(POLL_CRIT,&card->wandev.critical)){
		trigger_x25_poll(card);
	}
	
	card->u.x.x25_timer.expires=jiffies+(HZ>>1);
	add_timer(&card->u.x.x25_timer);
	return;
}

void disable_comm_shutdown(sdla_t *card)
{
	TX25Mbox* mbox = card->mbox;
	int err;

	/* Turn of interrutps */
	mbox->data[0] = 0;
	if (card->hw.fwid == SFID_X25_508){
		mbox->data[1] = card->hw.irq;
		mbox->data[2] = 2;
		mbox->cmd.length = 3;
	}else {
	 	mbox->cmd.length  = 1;
	}
	mbox->cmd.command = X25_SET_INTERRUPT_MODE;
	err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	if (err)
		printk(KERN_INFO "INTERRUPT OFF FAIED %x\n",err);

	/* Bring down HDLC */
	mbox->cmd.command = X25_HDLC_LINK_CLOSE;
	mbox->cmd.length  = 0;
	err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	if (err)
		printk(KERN_INFO "LINK CLOSED FAILED %x\n",err);


	/* Brind down DTR */
	mbox->data[0] = 0;
	mbox->data[2] = 0;
	mbox->data[1] = 0x01;
	mbox->cmd.length  = 3;
	mbox->cmd.command = X25_SET_GLOBAL_VARS;
	err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	if (err)
		printk(KERN_INFO "DTR DOWN FAILED %x\n",err);

}

MODULE_LICENSE("GPL");

/****** End *****************************************************************/

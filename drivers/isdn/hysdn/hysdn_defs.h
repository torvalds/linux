/* $Id: hysdn_defs.h,v 1.5.6.3 2001/09/23 22:24:54 kai Exp $
 *
 * Linux driver for HYSDN cards
 * global definitions and exported vars and functions.
 *
 * Author    Werner Cornelius (werner@titro.de) for Hypercope GmbH
 * Copyright 1999 by Werner Cornelius (werner@titro.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef HYSDN_DEFS_H
#define HYSDN_DEFS_H

#include <linux/hysdn_if.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>

#include "ince1pc.h"

#ifdef CONFIG_HYSDN_CAPI
#include <linux/capi.h>
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>
#include <linux/isdn/capilli.h>

/***************************/
/*   CAPI-Profile values.  */
/***************************/

#define GLOBAL_OPTION_INTERNAL_CONTROLLER 0x0001
#define GLOBAL_OPTION_EXTERNAL_CONTROLLER 0x0002
#define GLOBAL_OPTION_HANDSET             0x0004
#define GLOBAL_OPTION_DTMF                0x0008
#define GLOBAL_OPTION_SUPPL_SERVICES      0x0010
#define GLOBAL_OPTION_CHANNEL_ALLOCATION  0x0020
#define GLOBAL_OPTION_B_CHANNEL_OPERATION 0x0040

#define B1_PROT_64KBIT_HDLC        0x0001
#define B1_PROT_64KBIT_TRANSPARENT 0x0002
#define B1_PROT_V110_ASYNCH        0x0004 
#define B1_PROT_V110_SYNCH         0x0008
#define B1_PROT_T30                0x0010
#define B1_PROT_64KBIT_INV_HDLC    0x0020
#define B1_PROT_56KBIT_TRANSPARENT 0x0040

#define B2_PROT_ISO7776            0x0001
#define B2_PROT_TRANSPARENT        0x0002
#define B2_PROT_SDLC               0x0004
#define B2_PROT_LAPD               0x0008
#define B2_PROT_T30                0x0010
#define B2_PROT_PPP                0x0020
#define B2_PROT_TRANSPARENT_IGNORE_B1_FRAMING_ERRORS 0x0040

#define B3_PROT_TRANSPARENT        0x0001
#define B3_PROT_T90NL              0x0002
#define B3_PROT_ISO8208            0x0004
#define B3_PROT_X25_DCE            0x0008
#define B3_PROT_T30                0x0010
#define B3_PROT_T30EXT             0x0020

#define HYSDN_MAXVERSION		8

/* Number of sendbuffers in CAPI-queue */
#define HYSDN_MAX_CAPI_SKB             20

#endif /* CONFIG_HYSDN_CAPI*/

/************************************************/
/* constants and bits for debugging/log outputs */
/************************************************/
#define LOG_MAX_LINELEN 120
#define DEB_OUT_SYSLOG  0x80000000	/* output to syslog instead of proc fs */
#define LOG_MEM_ERR     0x00000001	/* log memory errors like kmalloc failure */
#define LOG_POF_OPEN    0x00000010	/* log pof open and close activities */
#define LOG_POF_RECORD  0x00000020	/* log pof record parser */
#define LOG_POF_WRITE   0x00000040	/* log detailed pof write operation */
#define LOG_POF_CARD    0x00000080	/* log pof related card functions */
#define LOG_CNF_LINE    0x00000100	/* all conf lines are put to procfs */
#define LOG_CNF_DATA    0x00000200	/* non comment conf lines are shown with channel */
#define LOG_CNF_MISC    0x00000400	/* additional conf line debug outputs */
#define LOG_SCHED_ASYN  0x00001000	/* debug schedulers async tx routines */
#define LOG_PROC_OPEN   0x00100000	/* open and close from procfs are logged */
#define LOG_PROC_ALL    0x00200000	/* all actions from procfs are logged */
#define LOG_NET_INIT    0x00010000	/* network init and deinit logging */

#define DEF_DEB_FLAGS   0x7fff000f	/* everything is logged to procfs */

/**********************************/
/* proc filesystem name constants */
/**********************************/
#define PROC_SUBDIR_NAME "hysdn"
#define PROC_CONF_BASENAME "cardconf"
#define PROC_LOG_BASENAME "cardlog"

/***********************************/
/* PCI 32 bit parms for IO and MEM */
/***********************************/
#define PCI_REG_PLX_MEM_BASE    0
#define PCI_REG_PLX_IO_BASE     1
#define PCI_REG_MEMORY_BASE     3

/**************/
/* card types */
/**************/
#define BD_NONE         0U
#define BD_PERFORMANCE  1U
#define BD_VALUE        2U
#define BD_PCCARD       3U
#define BD_ERGO         4U
#define BD_METRO        5U
#define BD_CHAMP2       6U
#define BD_PLEXUS       7U

/******************************************************/
/* defined states for cards shown by reading cardconf */
/******************************************************/
#define CARD_STATE_UNUSED   0	/* never been used or booted */
#define CARD_STATE_BOOTING  1	/* booting is in progress */
#define CARD_STATE_BOOTERR  2	/* a previous boot was aborted */
#define CARD_STATE_RUN      3	/* card is active */

/*******************************/
/* defines for error_log_state */
/*******************************/
#define ERRLOG_STATE_OFF   0	/* error log is switched off, nothing to do */
#define ERRLOG_STATE_ON    1	/* error log is switched on, wait for data */
#define ERRLOG_STATE_START 2	/* start error logging */
#define ERRLOG_STATE_STOP  3	/* stop error logging */

/*******************************/
/* data structure for one card */
/*******************************/
typedef struct HYSDN_CARD {

	/* general variables for the cards */
	int myid;		/* own driver card id */
	unsigned char bus;	/* pci bus the card is connected to */
	unsigned char devfn;	/* slot+function bit encoded */
	unsigned short subsysid;/* PCI subsystem id */
	unsigned char brdtype;	/* type of card */
	unsigned int bchans;	/* number of available B-channels */
	unsigned int faxchans;	/* number of available fax-channels */
	unsigned char mac_addr[6];/* MAC Address read from card */
	unsigned int irq;	/* interrupt number */
	unsigned int iobase;	/* IO-port base address */
	unsigned long plxbase;	/* PLX memory base */
	unsigned long membase;	/* DPRAM memory base */
	unsigned long memend;	/* DPRAM memory end */
	void *dpram;		/* mapped dpram */
	int state;		/* actual state of card -> CARD_STATE_** */
	struct HYSDN_CARD *next;	/* pointer to next card */

	/* data areas for the /proc file system */
	void *proclog;		/* pointer to proclog filesystem specific data */
	void *procconf;		/* pointer to procconf filesystem specific data */

	/* debugging and logging */
	unsigned char err_log_state;/* actual error log state of the card */
	unsigned long debug_flags;/* tells what should be debugged and where */
	void (*set_errlog_state) (struct HYSDN_CARD *, int);

	/* interrupt handler + interrupt synchronisation */
	struct work_struct irq_queue;	/* interrupt task queue */
	unsigned char volatile irq_enabled;/* interrupt enabled if != 0 */
	unsigned char volatile hw_lock;/* hardware is currently locked -> no access */

	/* boot process */
	void *boot;		/* pointer to boot private data */
	int (*writebootimg) (struct HYSDN_CARD *, unsigned char *, unsigned long);
	int (*writebootseq) (struct HYSDN_CARD *, unsigned char *, int);
	int (*waitpofready) (struct HYSDN_CARD *);
	int (*testram) (struct HYSDN_CARD *);

	/* scheduler for data transfer (only async parts) */
	unsigned char async_data[256];/* async data to be sent (normally for config) */
	unsigned short volatile async_len;/* length of data to sent */
	unsigned short volatile async_channel;/* channel number for async transfer */
	int volatile async_busy;	/* flag != 0 sending in progress */
	int volatile net_tx_busy;	/* a network packet tx is in progress */

	/* network interface */
	void *netif;		/* pointer to network structure */

	/* init and deinit stopcard for booting, too */
	void (*stopcard) (struct HYSDN_CARD *);
	void (*releasehardware) (struct HYSDN_CARD *);

	spinlock_t hysdn_lock;
#ifdef CONFIG_HYSDN_CAPI
	struct hycapictrl_info {
		char cardname[32];
		spinlock_t lock;
		int versionlen;
		char versionbuf[1024];
		char *version[HYSDN_MAXVERSION];

		char infobuf[128];	/* for function procinfo */
		
		struct HYSDN_CARD  *card;
		struct capi_ctr capi_ctrl;
		struct sk_buff *skbs[HYSDN_MAX_CAPI_SKB];
		int in_idx, out_idx;	/* indexes to buffer ring */
		int sk_count;		/* number of buffers currently in ring */
		struct sk_buff *tx_skb;	/* buffer for tx operation */
	  
		struct list_head ncci_head;
	} *hyctrlinfo;
#endif /* CONFIG_HYSDN_CAPI */
} hysdn_card;

#ifdef CONFIG_HYSDN_CAPI
typedef struct hycapictrl_info hycapictrl_info;
#endif /* CONFIG_HYSDN_CAPI */


/*****************/
/* exported vars */
/*****************/
extern hysdn_card *card_root;	/* pointer to first card */



/*************************/
/* im/exported functions */
/*************************/
extern char *hysdn_getrev(const char *);

/* hysdn_procconf.c */
extern int hysdn_procconf_init(void);	/* init proc config filesys */
extern void hysdn_procconf_release(void);	/* deinit proc config filesys */

/* hysdn_proclog.c */
extern int hysdn_proclog_init(hysdn_card *);	/* init proc log entry */
extern void hysdn_proclog_release(hysdn_card *);	/* deinit proc log entry */
extern void hysdn_addlog(hysdn_card *, char *,...);	/* output data to log */
extern void hysdn_card_errlog(hysdn_card *, tErrLogEntry *, int);	/* output card log */

/* boardergo.c */
extern int ergo_inithardware(hysdn_card * card);	/* get hardware -> module init */

/* hysdn_boot.c */
extern int pof_write_close(hysdn_card *);	/* close proc file after writing pof */
extern int pof_write_open(hysdn_card *, unsigned char **);	/* open proc file for writing pof */
extern int pof_write_buffer(hysdn_card *, int);		/* write boot data to card */
extern int EvalSysrTokData(hysdn_card *, unsigned char *, int);		/* Check Sysready Token Data */

/* hysdn_sched.c */
extern int hysdn_sched_tx(hysdn_card *, unsigned char *,
			unsigned short volatile *, unsigned short volatile *,
			unsigned short);
extern int hysdn_sched_rx(hysdn_card *, unsigned char *, unsigned short,
			unsigned short);
extern int hysdn_tx_cfgline(hysdn_card *, unsigned char *,
			unsigned short);	/* send one cfg line */

/* hysdn_net.c */
extern unsigned int hynet_enable; 
extern char *hysdn_net_revision;
extern int hysdn_net_create(hysdn_card *);	/* create a new net device */
extern int hysdn_net_release(hysdn_card *);	/* delete the device */
extern char *hysdn_net_getname(hysdn_card *);	/* get name of net interface */
extern void hysdn_tx_netack(hysdn_card *);	/* acknowledge a packet tx */
extern struct sk_buff *hysdn_tx_netget(hysdn_card *);	/* get next network packet */
extern void hysdn_rx_netpkt(hysdn_card *, unsigned char *,
			unsigned short);	/* rxed packet from network */

#ifdef CONFIG_HYSDN_CAPI
extern unsigned int hycapi_enable; 
extern int hycapi_capi_create(hysdn_card *);	/* create a new capi device */
extern int hycapi_capi_release(hysdn_card *);	/* delete the device */
extern int hycapi_capi_stop(hysdn_card *card);   /* suspend */
extern void hycapi_rx_capipkt(hysdn_card * card, unsigned char * buf,
				unsigned short len);
extern void hycapi_tx_capiack(hysdn_card * card);
extern struct sk_buff *hycapi_tx_capiget(hysdn_card *card);
extern int hycapi_init(void);
extern void hycapi_cleanup(void);
#endif /* CONFIG_HYSDN_CAPI */

#endif /* HYSDN_DEFS_H */

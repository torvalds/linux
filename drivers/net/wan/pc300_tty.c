/*
 * pc300_tty.c	Cyclades-PC300(tm) TTY Driver.
 *
 * Author:	Regina Kodato <reginak@cyclades.com>
 *
 * Copyright:	(c) 1999-2002 Cyclades Corp.
 *
 *	This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *   
 *  $Log: pc300_tty.c,v $
 *  Revision 3.7  2002/03/07 14:17:09  henrique
 *  License data fixed
 *
 *  Revision 3.6  2001/12/10 12:29:42  regina
 *  Fix the MLPPP bug
 *
 *  Revision 3.5  2001/10/31 11:20:05  regina
 *  automatic pppd starts
 *
 *  Revision 3.4  2001/08/06 12:01:51  regina
 *  problem in DSR_DE bit
 *
 *  Revision 3.3  2001/07/26 22:58:41  regina
 *  update EDA value
 *
 *  Revision 3.2  2001/07/12 13:11:20  regina
 *  bug fix - DCD-OFF in pc300 tty driver
 *
 *	DMA transmission bug fix
 *  
 *  Revision 3.1  2001/06/22 13:13:02  regina
 *  MLPPP implementation
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/if.h>
#include <linux/skbuff.h>
/* TTY includes */
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include "pc300.h"

/* defines and macros */
/* TTY Global definitions */
#define	CPC_TTY_NPORTS	8	/* maximum number of the sync tty connections */
#define	CPC_TTY_MAJOR	CYCLADES_MAJOR	
#define CPC_TTY_MINOR_START	240	/* minor of the first PC300 interface */

#define CPC_TTY_MAX_MTU	2000	

/* tty interface state */
#define	CPC_TTY_ST_IDLE	0
#define CPC_TTY_ST_INIT	1	/* configured with MLPPP and up */
#define CPC_TTY_ST_OPEN	2	/* opened by application */

#define	CPC_TTY_LOCK(card,flags)\
	do {\
		spin_lock_irqsave(&card->card_lock, flags);	\
	} while (0)

#define CPC_TTY_UNLOCK(card,flags)	\
	do {\
		spin_unlock_irqrestore(&card->card_lock, flags);	\
	} while (0)

//#define	CPC_TTY_DBG(format,a...)	printk(format,##a)
#define	CPC_TTY_DBG(format,a...)

/* data structures */
typedef struct _st_cpc_rx_buf {
	struct _st_cpc_rx_buf	*next;
	int		size;
	unsigned char	data[1];
} st_cpc_rx_buf;

struct st_cpc_rx_list {
	st_cpc_rx_buf	*first;
	st_cpc_rx_buf	*last;
};

typedef	struct _st_cpc_tty_area {
	int		state;		/* state of the TTY interface */
	int		num_open;	
	unsigned int 	tty_minor;	/* minor this interface */
	volatile struct st_cpc_rx_list buf_rx;	/* ptr. to reception buffer */
	unsigned char*	buf_tx;		/* ptr. to transmission buffer */
	pc300dev_t*	pc300dev;	/* ptr. to info struct in PC300 driver */
	unsigned char	name[20];	/* interf. name + "-tty" */
	struct tty_struct *tty;		
	struct work_struct tty_tx_work; /* tx work - tx interrupt */
	struct work_struct tty_rx_work; /* rx work - rx interrupt */
	} st_cpc_tty_area;

/* TTY data structures */
static struct tty_driver serial_drv;

/* local variables */
st_cpc_tty_area	cpc_tty_area[CPC_TTY_NPORTS];

int cpc_tty_cnt=0;	/* number of intrfaces configured with MLPPP */
int cpc_tty_unreg_flag = 0;

/* TTY functions prototype */
static int cpc_tty_open(struct tty_struct *tty, struct file *flip);
static void cpc_tty_close(struct tty_struct *tty, struct file *flip);
static int cpc_tty_write(struct tty_struct *tty, const unsigned char *buf, int count);
static int cpc_tty_write_room(struct tty_struct *tty);
static int cpc_tty_chars_in_buffer(struct tty_struct *tty);
static void cpc_tty_flush_buffer(struct tty_struct *tty);
static void cpc_tty_hangup(struct tty_struct *tty);
static void cpc_tty_rx_work(void *data);
static void cpc_tty_tx_work(void *data);
static int cpc_tty_send_to_card(pc300dev_t *dev,void *buf, int len);
static void cpc_tty_trace(pc300dev_t *dev, char* buf, int len, char rxtx);
static void cpc_tty_signal_off(pc300dev_t *pc300dev, unsigned char);
static void cpc_tty_signal_on(pc300dev_t *pc300dev, unsigned char);

int pc300_tiocmset(struct tty_struct *, struct file *,
			unsigned int, unsigned int);
int pc300_tiocmget(struct tty_struct *, struct file *);

/* functions called by PC300 driver */
void cpc_tty_init(pc300dev_t *dev);
void cpc_tty_unregister_service(pc300dev_t *pc300dev);
void cpc_tty_receive(pc300dev_t *pc300dev);
void cpc_tty_trigger_poll(pc300dev_t *pc300dev);
void cpc_tty_reset_var(void);

/*
 * PC300 TTY clear "signal"
 */
static void cpc_tty_signal_off(pc300dev_t *pc300dev, unsigned char signal)
{
	pc300ch_t *pc300chan = (pc300ch_t *)pc300dev->chan; 
	pc300_t *card = (pc300_t *) pc300chan->card; 
	int ch = pc300chan->channel; 
	unsigned long flags; 

	CPC_TTY_DBG("%s-tty: Clear signal %x\n",
		pc300dev->dev->name, signal);
	CPC_TTY_LOCK(card, flags); 
	cpc_writeb(card->hw.scabase + M_REG(CTL,ch), 
		cpc_readb(card->hw.scabase+M_REG(CTL,ch))& signal);
	CPC_TTY_UNLOCK(card,flags); 
}

/*
 * PC300 TTY set "signal" to ON
 */
static void cpc_tty_signal_on(pc300dev_t *pc300dev, unsigned char signal)
{
	pc300ch_t *pc300chan = (pc300ch_t *)pc300dev->chan; 
	pc300_t *card = (pc300_t *) pc300chan->card; 
	int ch = pc300chan->channel; 
	unsigned long flags; 

	CPC_TTY_DBG("%s-tty: Set signal %x\n",
		pc300dev->dev->name, signal);
	CPC_TTY_LOCK(card, flags); 
	cpc_writeb(card->hw.scabase + M_REG(CTL,ch), 
		cpc_readb(card->hw.scabase+M_REG(CTL,ch))& ~signal);
	CPC_TTY_UNLOCK(card,flags); 
}

/*
 * PC300 TTY initialization routine
 *
 * This routine is called by the PC300 driver during board configuration 
 * (ioctl=SIOCSP300CONF). At this point the adapter is completely
 * initialized.
 * o verify kernel version (only 2.4.x)
 * o register TTY driver
 * o init cpc_tty_area struct
 */
void cpc_tty_init(pc300dev_t *pc300dev)
{
	unsigned long port;
	int aux;
	st_cpc_tty_area * cpc_tty;

	/* hdlcX - X=interface number */
	port = pc300dev->dev->name[4] - '0';
	if (port >= CPC_TTY_NPORTS) {
		printk("%s-tty: invalid interface selected (0-%i): %li",
			pc300dev->dev->name,
			CPC_TTY_NPORTS-1,port);
		return;
	}

	if (cpc_tty_cnt == 0) { /* first TTY connection -> register driver */
		CPC_TTY_DBG("%s-tty: driver init, major:%i, minor range:%i=%i\n",
			pc300dev->dev->name,
			CPC_TTY_MAJOR, CPC_TTY_MINOR_START,
			CPC_TTY_MINOR_START+CPC_TTY_NPORTS);
		/* initialize tty driver struct */
		memset(&serial_drv,0,sizeof(struct tty_driver));
		serial_drv.magic = TTY_DRIVER_MAGIC;
		serial_drv.owner = THIS_MODULE;
		serial_drv.driver_name = "pc300_tty";
		serial_drv.name = "ttyCP";
		serial_drv.major = CPC_TTY_MAJOR;
		serial_drv.minor_start = CPC_TTY_MINOR_START;
		serial_drv.num = CPC_TTY_NPORTS;
		serial_drv.type = TTY_DRIVER_TYPE_SERIAL;
		serial_drv.subtype = SERIAL_TYPE_NORMAL;

		serial_drv.init_termios = tty_std_termios;
		serial_drv.init_termios.c_cflag = B9600|CS8|CREAD|HUPCL|CLOCAL;
		serial_drv.flags = TTY_DRIVER_REAL_RAW;

		/* interface routines from the upper tty layer to the tty driver */
		serial_drv.open = cpc_tty_open;
		serial_drv.close = cpc_tty_close;
		serial_drv.write = cpc_tty_write; 
		serial_drv.write_room = cpc_tty_write_room; 
		serial_drv.chars_in_buffer = cpc_tty_chars_in_buffer; 
		serial_drv.tiocmset = pc300_tiocmset;
		serial_drv.tiocmget = pc300_tiocmget;
		serial_drv.flush_buffer = cpc_tty_flush_buffer; 
		serial_drv.hangup = cpc_tty_hangup;

		/* register the TTY driver */
		if (tty_register_driver(&serial_drv)) { 
			printk("%s-tty: Failed to register serial driver! ",
				pc300dev->dev->name);
		   	return;
		} 

		memset((void *)cpc_tty_area, 0,
								sizeof(st_cpc_tty_area) * CPC_TTY_NPORTS);
	}

	cpc_tty = &cpc_tty_area[port];
	
	if (cpc_tty->state != CPC_TTY_ST_IDLE) {
		CPC_TTY_DBG("%s-tty: TTY port %i, already in use.\n",
				pc300dev->dev->name, port);
		return;
	}

	cpc_tty_cnt++;
	cpc_tty->state = CPC_TTY_ST_INIT; 
	cpc_tty->num_open= 0;
	cpc_tty->tty_minor = port + CPC_TTY_MINOR_START;
	cpc_tty->pc300dev = pc300dev; 

	INIT_WORK(&cpc_tty->tty_tx_work, cpc_tty_tx_work, (void *)cpc_tty);
	INIT_WORK(&cpc_tty->tty_rx_work, cpc_tty_rx_work, (void *)port);
	
	cpc_tty->buf_rx.first = cpc_tty->buf_rx.last = NULL;

	pc300dev->cpc_tty = (void *)cpc_tty; 
	
	aux = strlen(pc300dev->dev->name);
	memcpy(cpc_tty->name, pc300dev->dev->name, aux);
	memcpy(&cpc_tty->name[aux], "-tty", 5);
	
	cpc_open(pc300dev->dev);
	cpc_tty_signal_off(pc300dev, CTL_DTR);

	CPC_TTY_DBG("%s: Initializing TTY Sync Driver, tty major#%d minor#%i\n",
			cpc_tty->name,CPC_TTY_MAJOR,cpc_tty->tty_minor); 
	return; 
} 

/*
 * PC300 TTY OPEN routine
 *
 * This routine is called by the tty driver to open the interface 
 * o verify minor
 * o allocate buffer to Rx and Tx
 */
static int cpc_tty_open(struct tty_struct *tty, struct file *flip)
{
	int port ;
	st_cpc_tty_area *cpc_tty;

	if (!tty) { 
		return -ENODEV;
	} 

	port = tty->index;

	if ((port < 0) || (port >= CPC_TTY_NPORTS)){ 
		CPC_TTY_DBG("pc300_tty: open invalid port %d\n", port);
		return -ENODEV;
	} 

	cpc_tty = &cpc_tty_area[port];
	
	if (cpc_tty->state == CPC_TTY_ST_IDLE){
		CPC_TTY_DBG("%s: open - invalid interface, port=%d\n",
					cpc_tty->name, tty->index);
		return -ENODEV;
	}

	if (cpc_tty->num_open == 0) { /* first open of this tty */
		if (!cpc_tty_area[port].buf_tx){
			cpc_tty_area[port].buf_tx = kmalloc(CPC_TTY_MAX_MTU,GFP_KERNEL);
			if (cpc_tty_area[port].buf_tx == 0){
				CPC_TTY_DBG("%s: error in memory allocation\n",cpc_tty->name);
				return -ENOMEM;
			}
		} 

		if (cpc_tty_area[port].buf_rx.first) {
			unsigned char * aux;
			while (cpc_tty_area[port].buf_rx.first) {
				aux = (unsigned char *)cpc_tty_area[port].buf_rx.first;
				cpc_tty_area[port].buf_rx.first = cpc_tty_area[port].buf_rx.first->next;
				kfree(aux);
			}
			cpc_tty_area[port].buf_rx.first = NULL;
			cpc_tty_area[port].buf_rx.last = NULL;
		}

		cpc_tty_area[port].state = CPC_TTY_ST_OPEN;
		cpc_tty_area[port].tty = tty;
		tty->driver_data = &cpc_tty_area[port];

		cpc_tty_signal_on(cpc_tty->pc300dev, CTL_DTR);
	} 

	cpc_tty->num_open++;

	CPC_TTY_DBG("%s: opening TTY driver\n", cpc_tty->name);
	
	/* avisar driver PC300 */ 
	return 0; 
}

/*
 * PC300 TTY CLOSE routine
 *
 * This routine is called by the tty driver to close the interface 
 * o call close channel in PC300 driver (cpc_closech)
 * o free Rx and Tx buffers
 */

static void cpc_tty_close(struct tty_struct *tty, struct file *flip)
{
	st_cpc_tty_area    *cpc_tty;
	unsigned long flags;
	int res;

	if (!tty || !tty->driver_data ) {
		CPC_TTY_DBG("hdlx-tty: no TTY in close \n");
		return;
	}

	cpc_tty = (st_cpc_tty_area *) tty->driver_data;

	if ((cpc_tty->tty != tty)|| (cpc_tty->state != CPC_TTY_ST_OPEN)) {
		CPC_TTY_DBG("%s: TTY is not opened\n",cpc_tty->name);
		return;
	}
   	
	if (!cpc_tty->num_open) {
		CPC_TTY_DBG("%s: TTY is closed\n",cpc_tty->name);
		return;
	}

	if (--cpc_tty->num_open > 0) {
		CPC_TTY_DBG("%s: TTY closed\n",cpc_tty->name);
		return;
	}

	cpc_tty_signal_off(cpc_tty->pc300dev, CTL_DTR);

	CPC_TTY_LOCK(cpc_tty->pc300dev->chan->card, flags);  /* lock irq */ 
	cpc_tty->tty = NULL;
	cpc_tty->state = CPC_TTY_ST_INIT;
	CPC_TTY_UNLOCK(cpc_tty->pc300dev->chan->card, flags); /* unlock irq */ 
	
	if (cpc_tty->buf_rx.first) {
		unsigned char * aux;
		while (cpc_tty->buf_rx.first) {
			aux = (unsigned char *)cpc_tty->buf_rx.first;
			cpc_tty->buf_rx.first = cpc_tty->buf_rx.first->next;
			kfree(aux);
		}
		cpc_tty->buf_rx.first = NULL;
		cpc_tty->buf_rx.last = NULL;
	}
	
	kfree(cpc_tty->buf_tx);
	cpc_tty->buf_tx = NULL;

	CPC_TTY_DBG("%s: TTY closed\n",cpc_tty->name);
	
	if (!serial_drv.refcount && cpc_tty_unreg_flag) {
		cpc_tty_unreg_flag = 0;
		CPC_TTY_DBG("%s: unregister the tty driver\n", cpc_tty->name);
		if ((res=tty_unregister_driver(&serial_drv))) { 
			CPC_TTY_DBG("%s: ERROR ->unregister the tty driver error=%d\n",
							cpc_tty->name,res);
		}
	}
	return; 
} 

/*
 * PC300 TTY WRITE routine
 *
 * This routine is called by the tty driver to write a series of characters
 * to the tty device. The characters may come from user or kernel space.
 * o verify the DCD signal
 * o send characters to board and start the transmission
 */
static int cpc_tty_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	st_cpc_tty_area    *cpc_tty; 
	pc300ch_t *pc300chan; 
	pc300_t *card; 
	int ch; 
	unsigned long flags; 
	struct net_device_stats *stats; 

	if (!tty || !tty->driver_data ) { 
		CPC_TTY_DBG("hdlcX-tty: no TTY in write\n");
		return -ENODEV;
	} 

	cpc_tty = (st_cpc_tty_area *) tty->driver_data; 

	if ((cpc_tty->tty != tty) ||  (cpc_tty->state != CPC_TTY_ST_OPEN)) { 
		CPC_TTY_DBG("%s: TTY is not opened\n", cpc_tty->name);
		return -ENODEV; 
	}

	if (count > CPC_TTY_MAX_MTU) { 
		CPC_TTY_DBG("%s: count is invalid\n",cpc_tty->name);
		return -EINVAL;        /* frame too big */ 
	}

	CPC_TTY_DBG("%s: cpc_tty_write data len=%i\n",cpc_tty->name,count);
	
	pc300chan = (pc300ch_t *)((pc300dev_t*)cpc_tty->pc300dev)->chan; 
	stats = hdlc_stats(((pc300dev_t*)cpc_tty->pc300dev)->dev);
	card = (pc300_t *) pc300chan->card;
	ch = pc300chan->channel; 

	/* verify DCD signal*/ 
	if (cpc_readb(card->hw.scabase + M_REG(ST3,ch)) & ST3_DCD) { 
		/* DCD is OFF */ 
		CPC_TTY_DBG("%s : DCD is OFF\n", cpc_tty->name);
		stats->tx_errors++;
		stats->tx_carrier_errors++;
		CPC_TTY_LOCK(card, flags); 
		cpc_writeb(card->hw.scabase + M_REG(CMD, ch), CMD_TX_BUF_CLR); 
		
		if (card->hw.type == PC300_TE) { 
			cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2, 
				cpc_readb(card->hw.falcbase + card->hw.cpld_reg2) & 
				~(CPLD_REG2_FALC_LED1 << (2 *ch))); 
		}

		CPC_TTY_UNLOCK(card, flags); 

		return -EINVAL; 
	}

	if (cpc_tty_send_to_card(cpc_tty->pc300dev, (void*)buf, count)) { 
	   /* failed to send */
	   CPC_TTY_DBG("%s: trasmition error\n", cpc_tty->name);
	   return 0;
	}
	return count; 
} 

/*
 * PC300 TTY Write Room routine
 * 
 * This routine returns the numbers of characteres the tty driver will accept
 * for queuing to be written. 
 * o return MTU
 */
static int cpc_tty_write_room(struct tty_struct *tty)
{
	st_cpc_tty_area    *cpc_tty; 

	if (!tty || !tty->driver_data ) { 
		CPC_TTY_DBG("hdlcX-tty: no TTY to write room\n");
		return -ENODEV;
	}

	cpc_tty = (st_cpc_tty_area *) tty->driver_data; 

	if ((cpc_tty->tty != tty) ||  (cpc_tty->state != CPC_TTY_ST_OPEN)) { 
		CPC_TTY_DBG("%s: TTY is not opened\n",cpc_tty->name);
		return -ENODEV; 
	}
   	
	CPC_TTY_DBG("%s: write room\n",cpc_tty->name);
	
	return CPC_TTY_MAX_MTU;
} 

/*
 * PC300 TTY chars in buffer routine
 * 
 * This routine returns the chars number in the transmission buffer 
 * o returns 0
 */
static int cpc_tty_chars_in_buffer(struct tty_struct *tty)
{
	st_cpc_tty_area    *cpc_tty; 

	if (!tty || !tty->driver_data ) {
		CPC_TTY_DBG("hdlcX-tty: no TTY to chars in buffer\n");
		return -ENODEV; 
	}

	cpc_tty = (st_cpc_tty_area *) tty->driver_data; 

	if ((cpc_tty->tty != tty) || (cpc_tty->state != CPC_TTY_ST_OPEN)) { 
		CPC_TTY_DBG("%s: TTY is not opened\n",cpc_tty->name);
		return -ENODEV; 
	}
   
	return(0); 
} 

int pc300_tiocmset(struct tty_struct *tty, struct file *file,
			unsigned int set, unsigned int clear)
{
	st_cpc_tty_area    *cpc_tty; 

	CPC_TTY_DBG("%s: set:%x clear:%x\n", __FUNCTION__, set, clear);

	if (!tty || !tty->driver_data ) {
	   	CPC_TTY_DBG("hdlcX-tty: no TTY to chars in buffer\n");	
		return -ENODEV; 
	}

	cpc_tty = (st_cpc_tty_area *) tty->driver_data; 

	if (set & TIOCM_RTS)
		cpc_tty_signal_on(cpc_tty->pc300dev, CTL_RTS);
	if (set & TIOCM_DTR)
		cpc_tty_signal_on(cpc_tty->pc300dev, CTL_DTR);

	if (clear & TIOCM_RTS)
		cpc_tty_signal_off(cpc_tty->pc300dev, CTL_RTS);
	if (clear & TIOCM_DTR)
		cpc_tty_signal_off(cpc_tty->pc300dev, CTL_DTR);

	return 0;
}

int pc300_tiocmget(struct tty_struct *tty, struct file *file)
{
	unsigned int result;
	unsigned char status;
	unsigned long flags;
	st_cpc_tty_area  *cpc_tty = (st_cpc_tty_area *) tty->driver_data;
	pc300dev_t *pc300dev = cpc_tty->pc300dev;
	pc300ch_t *pc300chan = (pc300ch_t *)pc300dev->chan;
	pc300_t *card = (pc300_t *) pc300chan->card;
	int ch = pc300chan->channel;

	cpc_tty = (st_cpc_tty_area *) tty->driver_data;

	CPC_TTY_DBG("%s-tty: tiocmget\n",
		((struct net_device*)(pc300dev->hdlc))->name);

	CPC_TTY_LOCK(card, flags);
	status = cpc_readb(card->hw.scabase+M_REG(CTL,ch));
	CPC_TTY_UNLOCK(card,flags);

	result = ((status & CTL_DTR) ? TIOCM_DTR : 0) |
		 ((status & CTL_RTS) ? TIOCM_RTS : 0);

	return result;
}

/*
 * PC300 TTY Flush Buffer routine
 *
 * This routine resets the transmission buffer 
 */
static void cpc_tty_flush_buffer(struct tty_struct *tty)
{ 
	st_cpc_tty_area    *cpc_tty; 
	
	if (!tty || !tty->driver_data ) {
	   	CPC_TTY_DBG("hdlcX-tty: no TTY to flush buffer\n");	
		return; 
	}

	cpc_tty = (st_cpc_tty_area *) tty->driver_data; 

	if ((cpc_tty->tty != tty) ||  (cpc_tty->state != CPC_TTY_ST_OPEN)) { 
		CPC_TTY_DBG("%s: TTY is not opened\n",cpc_tty->name);
		return; 
	}

	CPC_TTY_DBG("%s: call wake_up_interruptible\n",cpc_tty->name);

	tty_wakeup(tty);	
	return; 
} 

/*
 * PC300 TTY Hangup routine
 *
 * This routine is called by the tty driver to hangup the interface 
 * o clear DTR signal
 */

static void cpc_tty_hangup(struct tty_struct *tty)
{ 
	st_cpc_tty_area    *cpc_tty; 
	int res;

	if (!tty || !tty->driver_data ) {
		CPC_TTY_DBG("hdlcX-tty: no TTY to hangup\n");	
		return ; 
	}

	cpc_tty = (st_cpc_tty_area *) tty->driver_data; 

	if ((cpc_tty->tty != tty) || (cpc_tty->state != CPC_TTY_ST_OPEN)) {
		CPC_TTY_DBG("%s: TTY is not opened\n",cpc_tty->name);
		return ;
	}
	if (!serial_drv.refcount && cpc_tty_unreg_flag) {
		cpc_tty_unreg_flag = 0;
		CPC_TTY_DBG("%s: unregister the tty driver\n", cpc_tty->name);
		if ((res=tty_unregister_driver(&serial_drv))) { 
			CPC_TTY_DBG("%s: ERROR ->unregister the tty driver error=%d\n",
							cpc_tty->name,res);
		}
	}
	cpc_tty_signal_off(cpc_tty->pc300dev, CTL_DTR);
}

/*
 * PC300 TTY RX work routine
 * This routine treats RX work
 * o verify read buffer
 * o call the line disc. read
 * o free memory
 */
static void cpc_tty_rx_work(void * data)
{
	unsigned long port;
	int i, j;
	st_cpc_tty_area *cpc_tty; 
	volatile st_cpc_rx_buf *buf;
	char flags=0,flg_rx=1; 
	struct tty_ldisc *ld;

	if (cpc_tty_cnt == 0) return;

	
	for (i=0; (i < 4) && flg_rx ; i++) {
		flg_rx = 0;
		port = (unsigned long)data;
		for (j=0; j < CPC_TTY_NPORTS; j++) {
			cpc_tty = &cpc_tty_area[port];
		
			if ((buf=cpc_tty->buf_rx.first) != 0) {
				if (cpc_tty->tty) {
					ld = tty_ldisc_ref(cpc_tty->tty);
					if (ld) {
						if (ld->receive_buf) {
							CPC_TTY_DBG("%s: call line disc. receive_buf\n",cpc_tty->name);
							ld->receive_buf(cpc_tty->tty, (char *)(buf->data), &flags, buf->size);
						}
						tty_ldisc_deref(ld);
					}
				}	
				cpc_tty->buf_rx.first = cpc_tty->buf_rx.first->next;
				kfree(buf);
				buf = cpc_tty->buf_rx.first;
				flg_rx = 1;
			}
			if (++port == CPC_TTY_NPORTS) port = 0;
		}
	}
} 

/*
 * PC300 TTY RX work routine
 *
 * This routine treats RX interrupt. 
 * o read all frames in card
 * o verify the frame size
 * o read the frame in rx buffer
 */
static void cpc_tty_rx_disc_frame(pc300ch_t *pc300chan)
{
	volatile pcsca_bd_t __iomem * ptdescr; 
	volatile unsigned char status; 
	pc300_t *card = (pc300_t *)pc300chan->card; 
	int ch = pc300chan->channel; 

	/* dma buf read */ 
	ptdescr = (pcsca_bd_t __iomem *)(card->hw.rambase + 
				RX_BD_ADDR(ch, pc300chan->rx_first_bd)); 
	while (pc300chan->rx_first_bd != pc300chan->rx_last_bd) { 
		status = cpc_readb(&ptdescr->status); 
		cpc_writeb(&ptdescr->status, 0); 
		cpc_writeb(&ptdescr->len, 0); 
		pc300chan->rx_first_bd = (pc300chan->rx_first_bd + 1) & 
					(N_DMA_RX_BUF - 1); 
		if (status & DST_EOM) { 
			break; /* end of message */
		}
		ptdescr = (pcsca_bd_t __iomem *)(card->hw.rambase + cpc_readl(&ptdescr->next)); 
	}
}

void cpc_tty_receive(pc300dev_t *pc300dev)
{
	st_cpc_tty_area *cpc_tty; 
	pc300ch_t *pc300chan = (pc300ch_t *)pc300dev->chan; 
	pc300_t *card = (pc300_t *)pc300chan->card; 
	int ch = pc300chan->channel; 
	volatile pcsca_bd_t  __iomem * ptdescr; 
	struct net_device_stats *stats = hdlc_stats(pc300dev->dev);
	int rx_len, rx_aux; 
	volatile unsigned char status; 
	unsigned short first_bd = pc300chan->rx_first_bd;
	st_cpc_rx_buf *new = NULL;
	unsigned char dsr_rx;

	if (pc300dev->cpc_tty == NULL) { 
		return; 
	}

	dsr_rx = cpc_readb(card->hw.scabase + DSR_RX(ch));

	cpc_tty = (st_cpc_tty_area *)pc300dev->cpc_tty; 

	while (1) { 
		rx_len = 0;
		ptdescr = (pcsca_bd_t  __iomem *)(card->hw.rambase + RX_BD_ADDR(ch, first_bd));
		while ((status = cpc_readb(&ptdescr->status)) & DST_OSB) {
			rx_len += cpc_readw(&ptdescr->len);
			first_bd = (first_bd + 1) & (N_DMA_RX_BUF - 1);
			if (status & DST_EOM) {
				break;
			}
			ptdescr = (pcsca_bd_t __iomem *)(card->hw.rambase+cpc_readl(&ptdescr->next));
		}
			
		if (!rx_len) { 
			if (dsr_rx & DSR_BOF) {
				/* update EDA */ 
				cpc_writel(card->hw.scabase + DRX_REG(EDAL, ch), 
						RX_BD_ADDR(ch, pc300chan->rx_last_bd)); 
			}
			kfree(new);
			return; 
		}
		
		if (rx_len > CPC_TTY_MAX_MTU) { 
			/* Free RX descriptors */ 
			CPC_TTY_DBG("%s: frame size is invalid.\n",cpc_tty->name);
			stats->rx_errors++; 
			stats->rx_frame_errors++; 
			cpc_tty_rx_disc_frame(pc300chan);
			continue;
		} 
		
		new = (st_cpc_rx_buf *)kmalloc(rx_len + sizeof(st_cpc_rx_buf), GFP_ATOMIC);
		if (new == 0) {
			cpc_tty_rx_disc_frame(pc300chan);
			continue;
		}
		
		/* dma buf read */ 
		ptdescr = (pcsca_bd_t __iomem *)(card->hw.rambase + 
				RX_BD_ADDR(ch, pc300chan->rx_first_bd)); 

		rx_len = 0;	/* counter frame size */
		
		while ((status = cpc_readb(&ptdescr->status)) & DST_OSB) {
			rx_aux = cpc_readw(&ptdescr->len);
			if ((status & (DST_OVR | DST_CRC | DST_RBIT |  DST_SHRT | DST_ABT))
				|| (rx_aux > BD_DEF_LEN)) {
				CPC_TTY_DBG("%s: reception error\n", cpc_tty->name);
				stats->rx_errors++; 
				if (status & DST_OVR) { 
					stats->rx_fifo_errors++; 
				}
				if (status & DST_CRC) { 
					stats->rx_crc_errors++; 
				}
				if ((status & (DST_RBIT | DST_SHRT | DST_ABT)) ||
					(rx_aux > BD_DEF_LEN))	{ 
					stats->rx_frame_errors++; 
				} 
				/* discard remainig descriptors used by the bad frame */ 
				CPC_TTY_DBG("%s: reception error - discard descriptors",
						cpc_tty->name);
				cpc_tty_rx_disc_frame(pc300chan);
				rx_len = 0;
				kfree(new);
				new = NULL;
				break; /* read next frame - while(1) */
			}

			if (cpc_tty->state != CPC_TTY_ST_OPEN) {
				/* Free RX descriptors */ 
				cpc_tty_rx_disc_frame(pc300chan);
				stats->rx_dropped++; 
				rx_len = 0; 
				kfree(new);
				new = NULL;
				break; /* read next frame - while(1) */
			}

			/* read the segment of the frame */
			if (rx_aux != 0) {
				memcpy_fromio((new->data + rx_len), 
					(void __iomem *)(card->hw.rambase + 
					 cpc_readl(&ptdescr->ptbuf)), rx_aux);
				rx_len += rx_aux; 
			}
			cpc_writeb(&ptdescr->status,0); 
			cpc_writeb(&ptdescr->len, 0); 
			pc300chan->rx_first_bd = (pc300chan->rx_first_bd + 1) & 
					(N_DMA_RX_BUF -1); 
			if (status & DST_EOM)break;
			
			ptdescr = (pcsca_bd_t __iomem *) (card->hw.rambase + 
					cpc_readl(&ptdescr->next)); 
		}
		/* update pointer */ 
		pc300chan->rx_last_bd = (pc300chan->rx_first_bd - 1) & 
					(N_DMA_RX_BUF - 1) ; 
		if (!(dsr_rx & DSR_BOF)) {
			/* update EDA */ 
			cpc_writel(card->hw.scabase + DRX_REG(EDAL, ch), 
					RX_BD_ADDR(ch, pc300chan->rx_last_bd)); 
		}
		if (rx_len != 0) { 
			stats->rx_bytes += rx_len; 
		
			if (pc300dev->trace_on) { 
				cpc_tty_trace(pc300dev, new->data,rx_len, 'R'); 
			} 
			new->size = rx_len;
			new->next = NULL;
			if (cpc_tty->buf_rx.first == 0) {
				cpc_tty->buf_rx.first = new;
				cpc_tty->buf_rx.last = new;
			} else {
				cpc_tty->buf_rx.last->next = new;
				cpc_tty->buf_rx.last = new;
			}
			schedule_work(&(cpc_tty->tty_rx_work));
			stats->rx_packets++;
		}
	} 
} 

/*
 * PC300 TTY TX work routine
 * 
 * This routine treats TX interrupt. 
 * o if need call line discipline wakeup
 * o call wake_up_interruptible
 */
static void cpc_tty_tx_work(void *data)
{
	st_cpc_tty_area *cpc_tty = (st_cpc_tty_area *) data; 
	struct tty_struct *tty; 

	CPC_TTY_DBG("%s: cpc_tty_tx_work init\n",cpc_tty->name);
	
	if ((tty = cpc_tty->tty) == 0) { 
		CPC_TTY_DBG("%s: the interface is not opened\n",cpc_tty->name);
		return; 
	}
	tty_wakeup(tty);
}

/*
 * PC300 TTY send to card routine
 * 
 * This routine send data to card. 
 * o clear descriptors
 * o write data to DMA buffers
 * o start the transmission
 */
static int cpc_tty_send_to_card(pc300dev_t *dev,void* buf, int len)
{
	pc300ch_t *chan = (pc300ch_t *)dev->chan; 
	pc300_t *card = (pc300_t *)chan->card; 
	int ch = chan->channel; 
	struct net_device_stats *stats = hdlc_stats(dev->dev);
	unsigned long flags; 
	volatile pcsca_bd_t __iomem *ptdescr; 
	int i, nchar;
	int tosend = len;
	int nbuf = ((len - 1)/BD_DEF_LEN) + 1;
	unsigned char *pdata=buf;

	CPC_TTY_DBG("%s:cpc_tty_send_to_cars len=%i", 
			(st_cpc_tty_area *)dev->cpc_tty->name,len);	

	if (nbuf >= card->chan[ch].nfree_tx_bd) {
		return 1;
	}
	
	/* write buffer to DMA buffers */ 
	CPC_TTY_DBG("%s: call dma_buf_write\n",
			(st_cpc_tty_area *)dev->cpc_tty->name);	
	for (i = 0 ; i < nbuf ; i++) {
		ptdescr = (pcsca_bd_t __iomem *)(card->hw.rambase + 
			TX_BD_ADDR(ch, card->chan[ch].tx_next_bd));
		nchar = (BD_DEF_LEN > tosend) ? tosend : BD_DEF_LEN;
		if (cpc_readb(&ptdescr->status) & DST_OSB) {
			memcpy_toio((void __iomem *)(card->hw.rambase + 
				cpc_readl(&ptdescr->ptbuf)), 
				&pdata[len - tosend], 
				nchar);
			card->chan[ch].nfree_tx_bd--;
			if ((i + 1) == nbuf) {
				/* This must be the last BD to be used */
				cpc_writeb(&ptdescr->status, DST_EOM);
			} else {
				cpc_writeb(&ptdescr->status, 0);
			}
			cpc_writew(&ptdescr->len, nchar);
		} else {
			CPC_TTY_DBG("%s: error in dma_buf_write\n",
					(st_cpc_tty_area *)dev->cpc_tty->name);	
			stats->tx_dropped++;
			return 1; 
		}
		tosend -= nchar;
		card->chan[ch].tx_next_bd = 
			(card->chan[ch].tx_next_bd + 1) & (N_DMA_TX_BUF - 1);
	}

	if (dev->trace_on) { 
		cpc_tty_trace(dev, buf, len,'T'); 
	}

	/* start transmission */ 
	CPC_TTY_DBG("%s: start transmission\n",
		(st_cpc_tty_area *)dev->cpc_tty->name);	
	
	CPC_TTY_LOCK(card, flags); 
	cpc_writeb(card->hw.scabase + DTX_REG(EDAL, ch), 
			TX_BD_ADDR(ch, chan->tx_next_bd)); 
	cpc_writeb(card->hw.scabase + M_REG(CMD, ch), CMD_TX_ENA); 
	cpc_writeb(card->hw.scabase + DSR_TX(ch), DSR_DE); 

	if (card->hw.type == PC300_TE) { 
		cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2, 
			cpc_readb(card->hw.falcbase + card->hw.cpld_reg2) |
			(CPLD_REG2_FALC_LED1 << (2 * ch))); 
	}
	CPC_TTY_UNLOCK(card, flags); 
	return 0; 
} 

/*
 *	PC300 TTY trace routine
 *
 *  This routine send trace of connection to application. 
 *  o clear descriptors
 *  o write data to DMA buffers
 *  o start the transmission
 */

static void cpc_tty_trace(pc300dev_t *dev, char* buf, int len, char rxtx)
{
	struct sk_buff *skb; 

	if ((skb = dev_alloc_skb(10 + len)) == NULL) { 
		/* out of memory */ 
		CPC_TTY_DBG("%s: tty_trace - out of memory\n", dev->dev->name);
		return; 
	}

	skb_put (skb, 10 + len); 
	skb->dev = dev->dev; 
	skb->protocol = htons(ETH_P_CUST); 
	skb->mac.raw = skb->data; 
	skb->pkt_type = PACKET_HOST; 
	skb->len = 10 + len; 

	memcpy(skb->data,dev->dev->name,5);
	skb->data[5] = '['; 
	skb->data[6] = rxtx; 
	skb->data[7] = ']'; 
	skb->data[8] = ':'; 
	skb->data[9] = ' '; 
	memcpy(&skb->data[10], buf, len); 
	netif_rx(skb); 
} 	

/*
 *	PC300 TTY unregister service routine
 *
 *	This routine unregister one interface. 
 */
void cpc_tty_unregister_service(pc300dev_t *pc300dev)
{
	st_cpc_tty_area *cpc_tty; 
	ulong flags;
	int res;

	if ((cpc_tty= (st_cpc_tty_area *) pc300dev->cpc_tty) == 0) { 
		CPC_TTY_DBG("%s: interface is not TTY\n", pc300dev->dev->name);
		return; 
	}
	CPC_TTY_DBG("%s: cpc_tty_unregister_service", cpc_tty->name);

	if (cpc_tty->pc300dev != pc300dev) { 
		CPC_TTY_DBG("%s: invalid tty ptr=%s\n", 
		pc300dev->dev->name, cpc_tty->name);
		return; 
	}

	if (--cpc_tty_cnt == 0) { 
		if (serial_drv.refcount) {
			CPC_TTY_DBG("%s: unregister is not possible, refcount=%d",
							cpc_tty->name, serial_drv.refcount);
			cpc_tty_cnt++;
			cpc_tty_unreg_flag = 1;
			return;
		} else { 
			CPC_TTY_DBG("%s: unregister the tty driver\n", cpc_tty->name);
			if ((res=tty_unregister_driver(&serial_drv))) { 
				CPC_TTY_DBG("%s: ERROR ->unregister the tty driver error=%d\n",
								cpc_tty->name,res);
			}
		}
	}
	CPC_TTY_LOCK(pc300dev->chan->card,flags);
	cpc_tty->tty = NULL; 
	CPC_TTY_UNLOCK(pc300dev->chan->card, flags);
	cpc_tty->tty_minor = 0; 
	cpc_tty->state = CPC_TTY_ST_IDLE; 
} 

/*
 * PC300 TTY trigger poll routine
 * This routine is called by pc300driver to treats Tx interrupt. 
 */
void cpc_tty_trigger_poll(pc300dev_t *pc300dev)
{
	st_cpc_tty_area *cpc_tty = (st_cpc_tty_area *)pc300dev->cpc_tty; 
	if (!cpc_tty) {
		return;
	}
	schedule_work(&(cpc_tty->tty_tx_work)); 
} 

/*
 * PC300 TTY reset var routine
 * This routine is called by pc300driver to init the TTY area. 
 */

void cpc_tty_reset_var(void)
{
	int i ; 

	CPC_TTY_DBG("hdlcX-tty: reset variables\n");
	/* reset  the tty_driver structure - serial_drv */ 
	memset(&serial_drv, 0, sizeof(struct tty_driver));
	for (i=0; i < CPC_TTY_NPORTS; i++){
		memset(&cpc_tty_area[i],0, sizeof(st_cpc_tty_area)); 
	}
}

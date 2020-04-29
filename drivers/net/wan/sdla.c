// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SDLA		An implementation of a driver for the Sangoma S502/S508 series
 *		multi-protocol PC interface card.  Initial offering is with 
 *		the DLCI driver, providing Frame Relay support for linux.
 *
 *		Global definitions for the Frame relay interface.
 *
 * Version:	@(#)sdla.c   0.30	12 Sep 1996
 *
 * Credits:	Sangoma Technologies, for the use of 2 cards for an extended
 *			period of time.
 *		David Mandelstam <dm@sangoma.com> for getting me started on 
 *			this project, and incentive to complete it.
 *		Gene Kozen <74604.152@compuserve.com> for providing me with
 *			important information about the cards.
 *
 * Author:	Mike McLagan <mike.mclagan@linux.org>
 *
 * Changes:
 *		0.15	Mike McLagan	Improved error handling, packet dropping
 *		0.20	Mike McLagan	New transmit/receive flags for config
 *					If in FR mode, don't accept packets from
 *					non DLCI devices.
 *		0.25	Mike McLagan	Fixed problem with rejecting packets
 *					from non DLCI devices.
 *		0.30	Mike McLagan	Fixed kernel panic when used with modified
 *					ifconfig
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/if_frad.h>
#include <linux/sdla.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <linux/uaccess.h>

static const char* version = "SDLA driver v0.30, 12 Sep 1996, mike.mclagan@linux.org";

static unsigned int valid_port[] = { 0x250, 0x270, 0x280, 0x300, 0x350, 0x360, 0x380, 0x390};

static unsigned int valid_mem[] = {
				    0xA0000, 0xA2000, 0xA4000, 0xA6000, 0xA8000, 0xAA000, 0xAC000, 0xAE000, 
                                    0xB0000, 0xB2000, 0xB4000, 0xB6000, 0xB8000, 0xBA000, 0xBC000, 0xBE000,
                                    0xC0000, 0xC2000, 0xC4000, 0xC6000, 0xC8000, 0xCA000, 0xCC000, 0xCE000,
                                    0xD0000, 0xD2000, 0xD4000, 0xD6000, 0xD8000, 0xDA000, 0xDC000, 0xDE000,
                                    0xE0000, 0xE2000, 0xE4000, 0xE6000, 0xE8000, 0xEA000, 0xEC000, 0xEE000}; 

static DEFINE_SPINLOCK(sdla_lock);

/*********************************************************
 *
 * these are the core routines that access the card itself 
 *
 *********************************************************/

#define SDLA_WINDOW(dev,addr) outb((((addr) >> 13) & 0x1F), (dev)->base_addr + SDLA_REG_Z80_WINDOW)

static void __sdla_read(struct net_device *dev, int addr, void *buf, short len)
{
	char          *temp;
	const void    *base;
	int           offset, bytes;

	temp = buf;
	while(len)
	{	
		offset = addr & SDLA_ADDR_MASK;
		bytes = offset + len > SDLA_WINDOW_SIZE ? SDLA_WINDOW_SIZE - offset : len;
		base = (const void *) (dev->mem_start + offset);

		SDLA_WINDOW(dev, addr);
		memcpy(temp, base, bytes);

		addr += bytes;
		temp += bytes;
		len  -= bytes;
	}  
}

static void sdla_read(struct net_device *dev, int addr, void *buf, short len)
{
	unsigned long flags;
	spin_lock_irqsave(&sdla_lock, flags);
	__sdla_read(dev, addr, buf, len);
	spin_unlock_irqrestore(&sdla_lock, flags);
}

static void __sdla_write(struct net_device *dev, int addr, 
			 const void *buf, short len)
{
	const char    *temp;
	void 	      *base;
	int           offset, bytes;

	temp = buf;
	while(len)
	{
		offset = addr & SDLA_ADDR_MASK;
		bytes = offset + len > SDLA_WINDOW_SIZE ? SDLA_WINDOW_SIZE - offset : len;
		base = (void *) (dev->mem_start + offset);

		SDLA_WINDOW(dev, addr);
		memcpy(base, temp, bytes);

		addr += bytes;
		temp += bytes;
		len  -= bytes;
	}
}

static void sdla_write(struct net_device *dev, int addr, 
		       const void *buf, short len)
{
	unsigned long flags;

	spin_lock_irqsave(&sdla_lock, flags);
	__sdla_write(dev, addr, buf, len);
	spin_unlock_irqrestore(&sdla_lock, flags);
}


static void sdla_clear(struct net_device *dev)
{
	unsigned long flags;
	char          *base;
	int           len, addr, bytes;

	len = 65536;	
	addr = 0;
	bytes = SDLA_WINDOW_SIZE;
	base = (void *) dev->mem_start;

	spin_lock_irqsave(&sdla_lock, flags);
	while(len)
	{
		SDLA_WINDOW(dev, addr);
		memset(base, 0, bytes);

		addr += bytes;
		len  -= bytes;
	}
	spin_unlock_irqrestore(&sdla_lock, flags);

}

static char sdla_byte(struct net_device *dev, int addr)
{
	unsigned long flags;
	char          byte, *temp;

	temp = (void *) (dev->mem_start + (addr & SDLA_ADDR_MASK));

	spin_lock_irqsave(&sdla_lock, flags);
	SDLA_WINDOW(dev, addr);
	byte = *temp;
	spin_unlock_irqrestore(&sdla_lock, flags);

	return byte;
}

static void sdla_stop(struct net_device *dev)
{
	struct frad_local *flp;

	flp = netdev_priv(dev);
	switch(flp->type)
	{
		case SDLA_S502A:
			outb(SDLA_S502A_HALT, dev->base_addr + SDLA_REG_CONTROL);
			flp->state = SDLA_HALT;
			break;
		case SDLA_S502E:
			outb(SDLA_HALT, dev->base_addr + SDLA_REG_Z80_CONTROL);
			outb(SDLA_S502E_ENABLE, dev->base_addr + SDLA_REG_CONTROL);
			flp->state = SDLA_S502E_ENABLE;
			break;
		case SDLA_S507:
			flp->state &= ~SDLA_CPUEN;
			outb(flp->state, dev->base_addr + SDLA_REG_CONTROL);
			break;
		case SDLA_S508:
			flp->state &= ~SDLA_CPUEN;
			outb(flp->state, dev->base_addr + SDLA_REG_CONTROL);
			break;
	}
}

static void sdla_start(struct net_device *dev)
{
	struct frad_local *flp;

	flp = netdev_priv(dev);
	switch(flp->type)
	{
		case SDLA_S502A:
			outb(SDLA_S502A_NMI, dev->base_addr + SDLA_REG_CONTROL);
			outb(SDLA_S502A_START, dev->base_addr + SDLA_REG_CONTROL);
			flp->state = SDLA_S502A_START;
			break;
		case SDLA_S502E:
			outb(SDLA_S502E_CPUEN, dev->base_addr + SDLA_REG_Z80_CONTROL);
			outb(0x00, dev->base_addr + SDLA_REG_CONTROL);
			flp->state = 0;
			break;
		case SDLA_S507:
			flp->state |= SDLA_CPUEN;
			outb(flp->state, dev->base_addr + SDLA_REG_CONTROL);
			break;
		case SDLA_S508:
			flp->state |= SDLA_CPUEN;
			outb(flp->state, dev->base_addr + SDLA_REG_CONTROL);
			break;
	}
}

/****************************************************
 *
 * this is used for the S502A/E cards to determine
 * the speed of the onboard CPU.  Calibration is
 * necessary for the Frame Relay code uploaded 
 * later.  Incorrect results cause timing problems
 * with link checks & status messages
 *
 ***************************************************/

static int sdla_z80_poll(struct net_device *dev, int z80_addr, int jiffs, char resp1, char resp2)
{
	unsigned long start, done, now;
	char          resp, *temp;

	start = now = jiffies;
	done = jiffies + jiffs;

	temp = (void *)dev->mem_start;
	temp += z80_addr & SDLA_ADDR_MASK;
	
	resp = ~resp1;
	while (time_before(jiffies, done) && (resp != resp1) && (!resp2 || (resp != resp2)))
	{
		if (jiffies != now)
		{
			SDLA_WINDOW(dev, z80_addr);
			now = jiffies;
			resp = *temp;
		}
	}
	return time_before(jiffies, done) ? jiffies - start : -1;
}

/* constants for Z80 CPU speed */
#define Z80_READY 		'1'	/* Z80 is ready to begin */
#define LOADER_READY 		'2'	/* driver is ready to begin */
#define Z80_SCC_OK 		'3'	/* SCC is on board */
#define Z80_SCC_BAD	 	'4'	/* SCC was not found */

static int sdla_cpuspeed(struct net_device *dev, struct ifreq *ifr)
{
	int  jiffs;
	char data;

	sdla_start(dev);
	if (sdla_z80_poll(dev, 0, 3*HZ, Z80_READY, 0) < 0)
		return -EIO;

	data = LOADER_READY;
	sdla_write(dev, 0, &data, 1);

	if ((jiffs = sdla_z80_poll(dev, 0, 8*HZ, Z80_SCC_OK, Z80_SCC_BAD)) < 0)
		return -EIO;

	sdla_stop(dev);
	sdla_read(dev, 0, &data, 1);

	if (data == Z80_SCC_BAD)
	{
		printk("%s: SCC bad\n", dev->name);
		return -EIO;
	}

	if (data != Z80_SCC_OK)
		return -EINVAL;

	if (jiffs < 165)
		ifr->ifr_mtu = SDLA_CPU_16M;
	else if (jiffs < 220)
		ifr->ifr_mtu = SDLA_CPU_10M;
	else if (jiffs < 258)
		ifr->ifr_mtu = SDLA_CPU_8M;
	else if (jiffs < 357)
		ifr->ifr_mtu = SDLA_CPU_7M;
	else if (jiffs < 467)
		ifr->ifr_mtu = SDLA_CPU_5M;
	else
		ifr->ifr_mtu = SDLA_CPU_3M;
 
	return 0;
}

/************************************************
 *
 *  Direct interaction with the Frame Relay code 
 *  starts here.
 *
 ************************************************/

struct _dlci_stat 
{
	short dlci;
	char  flags;
} __packed;

struct _frad_stat 
{
	char    flags;
	struct _dlci_stat dlcis[SDLA_MAX_DLCI];
};

static void sdla_errors(struct net_device *dev, int cmd, int dlci, int ret, int len, void *data) 
{
	struct _dlci_stat *pstatus;
	short             *pdlci;
	int               i;
	char              *state, line[30];

	switch (ret)
	{
		case SDLA_RET_MODEM:
			state = data;
			if (*state & SDLA_MODEM_DCD_LOW)
				netdev_info(dev, "Modem DCD unexpectedly low!\n");
			if (*state & SDLA_MODEM_CTS_LOW)
				netdev_info(dev, "Modem CTS unexpectedly low!\n");
			/* I should probably do something about this! */
			break;

		case SDLA_RET_CHANNEL_OFF:
			netdev_info(dev, "Channel became inoperative!\n");
			/* same here */
			break;

		case SDLA_RET_CHANNEL_ON:
			netdev_info(dev, "Channel became operative!\n");
			/* same here */
			break;

		case SDLA_RET_DLCI_STATUS:
			netdev_info(dev, "Status change reported by Access Node\n");
			len /= sizeof(struct _dlci_stat);
			for(pstatus = data, i=0;i < len;i++,pstatus++)
			{
				if (pstatus->flags & SDLA_DLCI_NEW)
					state = "new";
				else if (pstatus->flags & SDLA_DLCI_DELETED)
					state = "deleted";
				else if (pstatus->flags & SDLA_DLCI_ACTIVE)
					state = "active";
				else
				{
					sprintf(line, "unknown status: %02X", pstatus->flags);
					state = line;
				}
				netdev_info(dev, "DLCI %i: %s\n",
					    pstatus->dlci, state);
				/* same here */
			}
			break;

		case SDLA_RET_DLCI_UNKNOWN:
			netdev_info(dev, "Received unknown DLCIs:");
			len /= sizeof(short);
			for(pdlci = data,i=0;i < len;i++,pdlci++)
				pr_cont(" %i", *pdlci);
			pr_cont("\n");
			break;

		case SDLA_RET_TIMEOUT:
			netdev_err(dev, "Command timed out!\n");
			break;

		case SDLA_RET_BUF_OVERSIZE:
			netdev_info(dev, "Bc/CIR overflow, acceptable size is %i\n",
				    len);
			break;

		case SDLA_RET_BUF_TOO_BIG:
			netdev_info(dev, "Buffer size over specified max of %i\n",
				    len);
			break;

		case SDLA_RET_CHANNEL_INACTIVE:
		case SDLA_RET_DLCI_INACTIVE:
		case SDLA_RET_CIR_OVERFLOW:
		case SDLA_RET_NO_BUFS:
			if (cmd == SDLA_INFORMATION_WRITE)
				break;
			/* Else, fall through */

		default: 
			netdev_dbg(dev, "Cmd 0x%02X generated return code 0x%02X\n",
				   cmd, ret);
			/* Further processing could be done here */
			break;
	}
}

static int sdla_cmd(struct net_device *dev, int cmd, short dlci, short flags, 
                        void *inbuf, short inlen, void *outbuf, short *outlen)
{
	static struct _frad_stat status;
	struct frad_local        *flp;
	struct sdla_cmd          *cmd_buf;
	unsigned long            pflags;
	unsigned long		 jiffs;
	int                      ret, waiting, len;
	long                     window;

	flp = netdev_priv(dev);
	window = flp->type == SDLA_S508 ? SDLA_508_CMD_BUF : SDLA_502_CMD_BUF;
	cmd_buf = (struct sdla_cmd *)(dev->mem_start + (window & SDLA_ADDR_MASK));
	ret = 0;
	len = 0;
	jiffs = jiffies + HZ;  /* 1 second is plenty */

	spin_lock_irqsave(&sdla_lock, pflags);
	SDLA_WINDOW(dev, window);
	cmd_buf->cmd = cmd;
	cmd_buf->dlci = dlci;
	cmd_buf->flags = flags;

	if (inbuf)
		memcpy(cmd_buf->data, inbuf, inlen);

	cmd_buf->length = inlen;

	cmd_buf->opp_flag = 1;
	spin_unlock_irqrestore(&sdla_lock, pflags);

	waiting = 1;
	len = 0;
	while (waiting && time_before_eq(jiffies, jiffs))
	{
		if (waiting++ % 3) 
		{
			spin_lock_irqsave(&sdla_lock, pflags);
			SDLA_WINDOW(dev, window);
			waiting = ((volatile int)(cmd_buf->opp_flag));
			spin_unlock_irqrestore(&sdla_lock, pflags);
		}
	}
	
	if (!waiting)
	{

		spin_lock_irqsave(&sdla_lock, pflags);
		SDLA_WINDOW(dev, window);
		ret = cmd_buf->retval;
		len = cmd_buf->length;
		if (outbuf && outlen)
		{
			*outlen = *outlen >= len ? len : *outlen;

			if (*outlen)
				memcpy(outbuf, cmd_buf->data, *outlen);
		}

		/* This is a local copy that's used for error handling */
		if (ret)
			memcpy(&status, cmd_buf->data, len > sizeof(status) ? sizeof(status) : len);

		spin_unlock_irqrestore(&sdla_lock, pflags);
	}
	else
		ret = SDLA_RET_TIMEOUT;

	if (ret != SDLA_RET_OK)
	   	sdla_errors(dev, cmd, dlci, ret, len, &status);

	return ret;
}

/***********************************************
 *
 * these functions are called by the DLCI driver 
 *
 ***********************************************/

static int sdla_reconfig(struct net_device *dev);

static int sdla_activate(struct net_device *slave, struct net_device *master)
{
	struct frad_local *flp;
	int i;

	flp = netdev_priv(slave);

	for(i=0;i<CONFIG_DLCI_MAX;i++)
		if (flp->master[i] == master)
			break;

	if (i == CONFIG_DLCI_MAX)
		return -ENODEV;

	flp->dlci[i] = abs(flp->dlci[i]);

	if (netif_running(slave) && (flp->config.station == FRAD_STATION_NODE))
		sdla_cmd(slave, SDLA_ACTIVATE_DLCI, 0, 0, &flp->dlci[i], sizeof(short), NULL, NULL);

	return 0;
}

static int sdla_deactivate(struct net_device *slave, struct net_device *master)
{
	struct frad_local *flp;
	int               i;

	flp = netdev_priv(slave);

	for(i=0;i<CONFIG_DLCI_MAX;i++)
		if (flp->master[i] == master)
			break;

	if (i == CONFIG_DLCI_MAX)
		return -ENODEV;

	flp->dlci[i] = -abs(flp->dlci[i]);

	if (netif_running(slave) && (flp->config.station == FRAD_STATION_NODE))
		sdla_cmd(slave, SDLA_DEACTIVATE_DLCI, 0, 0, &flp->dlci[i], sizeof(short), NULL, NULL);

	return 0;
}

static int sdla_assoc(struct net_device *slave, struct net_device *master)
{
	struct frad_local *flp;
	int               i;

	if (master->type != ARPHRD_DLCI)
		return -EINVAL;

	flp = netdev_priv(slave);

	for(i=0;i<CONFIG_DLCI_MAX;i++)
	{
		if (!flp->master[i])
			break;
		if (abs(flp->dlci[i]) == *(short *)(master->dev_addr))
			return -EADDRINUSE;
	} 

	if (i == CONFIG_DLCI_MAX)
		return -EMLINK;  /* #### Alan: Comments on this ?? */


	flp->master[i] = master;
	flp->dlci[i] = -*(short *)(master->dev_addr);
	master->mtu = slave->mtu;

	if (netif_running(slave)) {
		if (flp->config.station == FRAD_STATION_CPE)
			sdla_reconfig(slave);
		else
			sdla_cmd(slave, SDLA_ADD_DLCI, 0, 0, master->dev_addr, sizeof(short), NULL, NULL);
	}

	return 0;
}

static int sdla_deassoc(struct net_device *slave, struct net_device *master)
{
	struct frad_local *flp;
	int               i;

	flp = netdev_priv(slave);

	for(i=0;i<CONFIG_DLCI_MAX;i++)
		if (flp->master[i] == master)
			break;

	if (i == CONFIG_DLCI_MAX)
		return -ENODEV;

	flp->master[i] = NULL;
	flp->dlci[i] = 0;


	if (netif_running(slave)) {
		if (flp->config.station == FRAD_STATION_CPE)
			sdla_reconfig(slave);
		else
			sdla_cmd(slave, SDLA_DELETE_DLCI, 0, 0, master->dev_addr, sizeof(short), NULL, NULL);
	}

	return 0;
}

static int sdla_dlci_conf(struct net_device *slave, struct net_device *master, int get)
{
	struct frad_local *flp;
	struct dlci_local *dlp;
	int               i;
	short             len, ret;

	flp = netdev_priv(slave);

	for(i=0;i<CONFIG_DLCI_MAX;i++)
		if (flp->master[i] == master)
			break;

	if (i == CONFIG_DLCI_MAX)
		return -ENODEV;

	dlp = netdev_priv(master);

	ret = SDLA_RET_OK;
	len = sizeof(struct dlci_conf);
	if (netif_running(slave)) {
		if (get)
			ret = sdla_cmd(slave, SDLA_READ_DLCI_CONFIGURATION, abs(flp->dlci[i]), 0,  
			            NULL, 0, &dlp->config, &len);
		else
			ret = sdla_cmd(slave, SDLA_SET_DLCI_CONFIGURATION, abs(flp->dlci[i]), 0,  
			            &dlp->config, sizeof(struct dlci_conf) - 4 * sizeof(short), NULL, NULL);
	}

	return ret == SDLA_RET_OK ? 0 : -EIO;
}

/**************************
 *
 * now for the Linux driver 
 *
 **************************/

/* NOTE: the DLCI driver deals with freeing the SKB!! */
static netdev_tx_t sdla_transmit(struct sk_buff *skb,
				 struct net_device *dev)
{
	struct frad_local *flp;
	int               ret, addr, accept, i;
	short             size;
	unsigned long     flags;
	struct buf_entry  *pbuf;

	flp = netdev_priv(dev);
	ret = 0;
	accept = 1;

	netif_stop_queue(dev);

	/*
	 * stupid GateD insists on setting up the multicast router thru us
	 * and we're ill equipped to handle a non Frame Relay packet at this
	 * time!
	 */

	accept = 1;
	switch (dev->type)
	{
		case ARPHRD_FRAD:
			if (skb->dev->type != ARPHRD_DLCI)
			{
				netdev_warn(dev, "Non DLCI device, type %i, tried to send on FRAD module\n",
					    skb->dev->type);
				accept = 0;
			}
			break;
		default:
			netdev_warn(dev, "unknown firmware type 0x%04X\n",
				    dev->type);
			accept = 0;
			break;
	}
	if (accept)
	{
		/* this is frame specific, but till there's a PPP module, it's the default */
		switch (flp->type)
		{
			case SDLA_S502A:
			case SDLA_S502E:
				ret = sdla_cmd(dev, SDLA_INFORMATION_WRITE, *(short *)(skb->dev->dev_addr), 0, skb->data, skb->len, NULL, NULL);
				break;
				case SDLA_S508:
				size = sizeof(addr);
				ret = sdla_cmd(dev, SDLA_INFORMATION_WRITE, *(short *)(skb->dev->dev_addr), 0, NULL, skb->len, &addr, &size);
				if (ret == SDLA_RET_OK)
				{

					spin_lock_irqsave(&sdla_lock, flags);
					SDLA_WINDOW(dev, addr);
					pbuf = (void *)(dev->mem_start + (addr & SDLA_ADDR_MASK));
					__sdla_write(dev, pbuf->buf_addr, skb->data, skb->len);
					SDLA_WINDOW(dev, addr);
					pbuf->opp_flag = 1;
					spin_unlock_irqrestore(&sdla_lock, flags);
				}
				break;
		}

		switch (ret)
		{
			case SDLA_RET_OK:
				dev->stats.tx_packets++;
				break;

			case SDLA_RET_CIR_OVERFLOW:
			case SDLA_RET_BUF_OVERSIZE:
			case SDLA_RET_NO_BUFS:
				dev->stats.tx_dropped++;
				break;

			default:
				dev->stats.tx_errors++;
				break;
		}
	}
	netif_wake_queue(dev);
	for(i=0;i<CONFIG_DLCI_MAX;i++)
	{
		if(flp->master[i]!=NULL)
			netif_wake_queue(flp->master[i]);
	}		

	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void sdla_receive(struct net_device *dev)
{
	struct net_device	  *master;
	struct frad_local *flp;
	struct dlci_local *dlp;
	struct sk_buff	 *skb;

	struct sdla_cmd	*cmd;
	struct buf_info	*pbufi;
	struct buf_entry  *pbuf;

	unsigned long	  flags;
	int               i=0, received, success, addr, buf_base, buf_top;
	short             dlci, len, len2, split;

	flp = netdev_priv(dev);
	success = 1;
	received = addr = buf_top = buf_base = 0;
	len = dlci = 0;
	skb = NULL;
	master = NULL;
	cmd = NULL;
	pbufi = NULL;
	pbuf = NULL;

	spin_lock_irqsave(&sdla_lock, flags);

	switch (flp->type)
	{
		case SDLA_S502A:
		case SDLA_S502E:
			cmd = (void *) (dev->mem_start + (SDLA_502_RCV_BUF & SDLA_ADDR_MASK));
			SDLA_WINDOW(dev, SDLA_502_RCV_BUF);
			success = cmd->opp_flag;
			if (!success)
				break;

			dlci = cmd->dlci;
			len = cmd->length;
			break;

		case SDLA_S508:
			pbufi = (void *) (dev->mem_start + (SDLA_508_RXBUF_INFO & SDLA_ADDR_MASK));
			SDLA_WINDOW(dev, SDLA_508_RXBUF_INFO);
			pbuf = (void *) (dev->mem_start + ((pbufi->rse_base + flp->buffer * sizeof(struct buf_entry)) & SDLA_ADDR_MASK));
			success = pbuf->opp_flag;
			if (!success)
				break;

			buf_top = pbufi->buf_top;
			buf_base = pbufi->buf_base;
			dlci = pbuf->dlci;
			len = pbuf->length;
			addr = pbuf->buf_addr;
			break;
	}

	/* common code, find the DLCI and get the SKB */
	if (success)
	{
		for (i=0;i<CONFIG_DLCI_MAX;i++)
			if (flp->dlci[i] == dlci)
				break;

		if (i == CONFIG_DLCI_MAX)
		{
			netdev_notice(dev, "Received packet from invalid DLCI %i, ignoring\n",
				      dlci);
			dev->stats.rx_errors++;
			success = 0;
		}
	}

	if (success)
	{
		master = flp->master[i];
		skb = dev_alloc_skb(len + sizeof(struct frhdr));
		if (skb == NULL) 
		{
			netdev_notice(dev, "Memory squeeze, dropping packet\n");
			dev->stats.rx_dropped++;
			success = 0;
		}
		else
			skb_reserve(skb, sizeof(struct frhdr));
	}

	/* pick up the data */
	switch (flp->type)
	{
		case SDLA_S502A:
		case SDLA_S502E:
			if (success)
				__sdla_read(dev, SDLA_502_RCV_BUF + SDLA_502_DATA_OFS, skb_put(skb,len), len);

			SDLA_WINDOW(dev, SDLA_502_RCV_BUF);
			cmd->opp_flag = 0;
			break;

		case SDLA_S508:
			if (success)
			{
				/* is this buffer split off the end of the internal ring buffer */
				split = addr + len > buf_top + 1 ? len - (buf_top - addr + 1) : 0;
				len2 = len - split;

				__sdla_read(dev, addr, skb_put(skb, len2), len2);
				if (split)
					__sdla_read(dev, buf_base, skb_put(skb, split), split);
			}

			/* increment the buffer we're looking at */
			SDLA_WINDOW(dev, SDLA_508_RXBUF_INFO);
			flp->buffer = (flp->buffer + 1) % pbufi->rse_num;
			pbuf->opp_flag = 0;
			break;
	}

	if (success)
	{
		dev->stats.rx_packets++;
		dlp = netdev_priv(master);
		(*dlp->receive)(skb, master);
	}

	spin_unlock_irqrestore(&sdla_lock, flags);
}

static irqreturn_t sdla_isr(int dummy, void *dev_id)
{
	struct net_device     *dev;
	struct frad_local *flp;
	char              byte;

	dev = dev_id;

	flp = netdev_priv(dev);

	if (!flp->initialized)
	{
		netdev_warn(dev, "irq %d for uninitialized device\n", dev->irq);
		return IRQ_NONE;
	}

	byte = sdla_byte(dev, flp->type == SDLA_S508 ? SDLA_508_IRQ_INTERFACE : SDLA_502_IRQ_INTERFACE);
	switch (byte)
	{
		case SDLA_INTR_RX:
			sdla_receive(dev);
			break;

		/* the command will get an error return, which is processed above */
		case SDLA_INTR_MODEM:
		case SDLA_INTR_STATUS:
			sdla_cmd(dev, SDLA_READ_DLC_STATUS, 0, 0, NULL, 0, NULL, NULL);
			break;

		case SDLA_INTR_TX:
		case SDLA_INTR_COMPLETE:
		case SDLA_INTR_TIMER:
			netdev_warn(dev, "invalid irq flag 0x%02X\n", byte);
			break;
	}

	/* the S502E requires a manual acknowledgement of the interrupt */ 
	if (flp->type == SDLA_S502E)
	{
		flp->state &= ~SDLA_S502E_INTACK;
		outb(flp->state, dev->base_addr + SDLA_REG_CONTROL);
		flp->state |= SDLA_S502E_INTACK;
		outb(flp->state, dev->base_addr + SDLA_REG_CONTROL);
	}

	/* this clears the byte, informing the Z80 we're done */
	byte = 0;
	sdla_write(dev, flp->type == SDLA_S508 ? SDLA_508_IRQ_INTERFACE : SDLA_502_IRQ_INTERFACE, &byte, sizeof(byte));
	return IRQ_HANDLED;
}

static void sdla_poll(struct timer_list *t)
{
	struct frad_local *flp = from_timer(flp, t, timer);
	struct net_device *dev = flp->dev;

	if (sdla_byte(dev, SDLA_502_RCV_BUF))
		sdla_receive(dev);

	flp->timer.expires = 1;
	add_timer(&flp->timer);
}

static int sdla_close(struct net_device *dev)
{
	struct frad_local *flp;
	struct intr_info  intr;
	int               len, i;
	short             dlcis[CONFIG_DLCI_MAX];

	flp = netdev_priv(dev);

	len = 0;
	for(i=0;i<CONFIG_DLCI_MAX;i++)
		if (flp->dlci[i])
			dlcis[len++] = abs(flp->dlci[i]);
	len *= 2;

	if (flp->config.station == FRAD_STATION_NODE)
	{
		for(i=0;i<CONFIG_DLCI_MAX;i++)
			if (flp->dlci[i] > 0) 
				sdla_cmd(dev, SDLA_DEACTIVATE_DLCI, 0, 0, dlcis, len, NULL, NULL);
		sdla_cmd(dev, SDLA_DELETE_DLCI, 0, 0, &flp->dlci[i], sizeof(flp->dlci[i]), NULL, NULL);
	}

	memset(&intr, 0, sizeof(intr));
	/* let's start up the reception */
	switch(flp->type)
	{
		case SDLA_S502A:
			del_timer(&flp->timer); 
			break;

		case SDLA_S502E:
			sdla_cmd(dev, SDLA_SET_IRQ_TRIGGER, 0, 0, &intr, sizeof(char) + sizeof(short), NULL, NULL);
			flp->state &= ~SDLA_S502E_INTACK;
			outb(flp->state, dev->base_addr + SDLA_REG_CONTROL);
			break;

		case SDLA_S507:
			break;

		case SDLA_S508:
			sdla_cmd(dev, SDLA_SET_IRQ_TRIGGER, 0, 0, &intr, sizeof(struct intr_info), NULL, NULL);
			flp->state &= ~SDLA_S508_INTEN;
			outb(flp->state, dev->base_addr + SDLA_REG_CONTROL);
			break;
	}

	sdla_cmd(dev, SDLA_DISABLE_COMMUNICATIONS, 0, 0, NULL, 0, NULL, NULL);

	netif_stop_queue(dev);
	
	return 0;
}

struct conf_data {
	struct frad_conf config;
	short            dlci[CONFIG_DLCI_MAX];
};

static int sdla_open(struct net_device *dev)
{
	struct frad_local *flp;
	struct dlci_local *dlp;
	struct conf_data  data;
	struct intr_info  intr;
	int               len, i;
	char              byte;

	flp = netdev_priv(dev);

	if (!flp->initialized)
		return -EPERM;

	if (!flp->configured)
		return -EPERM;

	/* time to send in the configuration */
	len = 0;
	for(i=0;i<CONFIG_DLCI_MAX;i++)
		if (flp->dlci[i])
			data.dlci[len++] = abs(flp->dlci[i]);
	len *= 2;

	memcpy(&data.config, &flp->config, sizeof(struct frad_conf));
	len += sizeof(struct frad_conf);

	sdla_cmd(dev, SDLA_DISABLE_COMMUNICATIONS, 0, 0, NULL, 0, NULL, NULL);
	sdla_cmd(dev, SDLA_SET_DLCI_CONFIGURATION, 0, 0, &data, len, NULL, NULL);

	if (flp->type == SDLA_S508)
		flp->buffer = 0;

	sdla_cmd(dev, SDLA_ENABLE_COMMUNICATIONS, 0, 0, NULL, 0, NULL, NULL);

	/* let's start up the reception */
	memset(&intr, 0, sizeof(intr));
	switch(flp->type)
	{
		case SDLA_S502A:
			flp->timer.expires = 1;
			add_timer(&flp->timer);
			break;

		case SDLA_S502E:
			flp->state |= SDLA_S502E_ENABLE;
			outb(flp->state, dev->base_addr + SDLA_REG_CONTROL);
			flp->state |= SDLA_S502E_INTACK;
			outb(flp->state, dev->base_addr + SDLA_REG_CONTROL);
			byte = 0;
			sdla_write(dev, SDLA_502_IRQ_INTERFACE, &byte, sizeof(byte));
			intr.flags = SDLA_INTR_RX | SDLA_INTR_STATUS | SDLA_INTR_MODEM;
			sdla_cmd(dev, SDLA_SET_IRQ_TRIGGER, 0, 0, &intr, sizeof(char) + sizeof(short), NULL, NULL);
			break;

		case SDLA_S507:
			break;

		case SDLA_S508:
			flp->state |= SDLA_S508_INTEN;
			outb(flp->state, dev->base_addr + SDLA_REG_CONTROL);
			byte = 0;
			sdla_write(dev, SDLA_508_IRQ_INTERFACE, &byte, sizeof(byte));
			intr.flags = SDLA_INTR_RX | SDLA_INTR_STATUS | SDLA_INTR_MODEM;
			intr.irq = dev->irq;
			sdla_cmd(dev, SDLA_SET_IRQ_TRIGGER, 0, 0, &intr, sizeof(struct intr_info), NULL, NULL);
			break;
	}

	if (flp->config.station == FRAD_STATION_CPE)
	{
		byte = SDLA_ICS_STATUS_ENQ;
		sdla_cmd(dev, SDLA_ISSUE_IN_CHANNEL_SIGNAL, 0, 0, &byte, sizeof(byte), NULL, NULL);
	}
	else
	{
		sdla_cmd(dev, SDLA_ADD_DLCI, 0, 0, data.dlci, len - sizeof(struct frad_conf), NULL, NULL);
		for(i=0;i<CONFIG_DLCI_MAX;i++)
			if (flp->dlci[i] > 0)
				sdla_cmd(dev, SDLA_ACTIVATE_DLCI, 0, 0, &flp->dlci[i], 2*sizeof(flp->dlci[i]), NULL, NULL);
	}

	/* configure any specific DLCI settings */
	for(i=0;i<CONFIG_DLCI_MAX;i++)
		if (flp->dlci[i])
		{
			dlp = netdev_priv(flp->master[i]);
			if (dlp->configured)
				sdla_cmd(dev, SDLA_SET_DLCI_CONFIGURATION, abs(flp->dlci[i]), 0, &dlp->config, sizeof(struct dlci_conf), NULL, NULL);
		}

	netif_start_queue(dev);
	
	return 0;
}

static int sdla_config(struct net_device *dev, struct frad_conf __user *conf, int get)
{
	struct frad_local *flp;
	struct conf_data  data;
	int               i;
	short             size;

	if (dev->type == 0xFFFF)
		return -EUNATCH;

	flp = netdev_priv(dev);

	if (!get)
	{
		if (netif_running(dev))
			return -EBUSY;

		if(copy_from_user(&data.config, conf, sizeof(struct frad_conf)))
			return -EFAULT;

		if (data.config.station & ~FRAD_STATION_NODE)
			return -EINVAL;

		if (data.config.flags & ~FRAD_VALID_FLAGS)
			return -EINVAL;

		if ((data.config.kbaud < 0) || 
			 ((data.config.kbaud > 128) && (flp->type != SDLA_S508)))
			return -EINVAL;

		if (data.config.clocking & ~(FRAD_CLOCK_INT | SDLA_S508_PORT_RS232))
			return -EINVAL;

		if ((data.config.mtu < 0) || (data.config.mtu > SDLA_MAX_MTU))
			return -EINVAL;

		if ((data.config.T391 < 5) || (data.config.T391 > 30))
			return -EINVAL;

		if ((data.config.T392 < 5) || (data.config.T392 > 30))
			return -EINVAL;

		if ((data.config.N391 < 1) || (data.config.N391 > 255))
			return -EINVAL;

		if ((data.config.N392 < 1) || (data.config.N392 > 10))
			return -EINVAL;

		if ((data.config.N393 < 1) || (data.config.N393 > 10))
			return -EINVAL;

		memcpy(&flp->config, &data.config, sizeof(struct frad_conf));
		flp->config.flags |= SDLA_DIRECT_RECV;

		if (flp->type == SDLA_S508)
			flp->config.flags |= SDLA_TX70_RX30;

		if (dev->mtu != flp->config.mtu)
		{
			/* this is required to change the MTU */
			dev->mtu = flp->config.mtu;
			for(i=0;i<CONFIG_DLCI_MAX;i++)
				if (flp->master[i])
					flp->master[i]->mtu = flp->config.mtu;
		}

		flp->config.mtu += sizeof(struct frhdr);

		/* off to the races! */
		if (!flp->configured)
			sdla_start(dev);

		flp->configured = 1;
	}
	else
	{
		/* no sense reading if the CPU isn't started */
		if (netif_running(dev))
		{
			size = sizeof(data);
			if (sdla_cmd(dev, SDLA_READ_DLCI_CONFIGURATION, 0, 0, NULL, 0, &data, &size) != SDLA_RET_OK)
				return -EIO;
		}
		else
			if (flp->configured)
				memcpy(&data.config, &flp->config, sizeof(struct frad_conf));
			else
				memset(&data.config, 0, sizeof(struct frad_conf));

		memcpy(&flp->config, &data.config, sizeof(struct frad_conf));
		data.config.flags &= FRAD_VALID_FLAGS;
		data.config.mtu -= data.config.mtu > sizeof(struct frhdr) ? sizeof(struct frhdr) : data.config.mtu;
		return copy_to_user(conf, &data.config, sizeof(struct frad_conf))?-EFAULT:0;
	}

	return 0;
}

static int sdla_xfer(struct net_device *dev, struct sdla_mem __user *info, int read)
{
	struct sdla_mem mem;
	char	*temp;

	if(copy_from_user(&mem, info, sizeof(mem)))
		return -EFAULT;
		
	if (read)
	{	
		temp = kzalloc(mem.len, GFP_KERNEL);
		if (!temp)
			return -ENOMEM;
		sdla_read(dev, mem.addr, temp, mem.len);
		if(copy_to_user(mem.data, temp, mem.len))
		{
			kfree(temp);
			return -EFAULT;
		}
		kfree(temp);
	}
	else
	{
		temp = memdup_user(mem.data, mem.len);
		if (IS_ERR(temp))
			return PTR_ERR(temp);
		sdla_write(dev, mem.addr, temp, mem.len);
		kfree(temp);
	}
	return 0;
}

static int sdla_reconfig(struct net_device *dev)
{
	struct frad_local *flp;
	struct conf_data  data;
	int               i, len;

	flp = netdev_priv(dev);

	len = 0;
	for(i=0;i<CONFIG_DLCI_MAX;i++)
		if (flp->dlci[i])
			data.dlci[len++] = flp->dlci[i];
	len *= 2;

	memcpy(&data, &flp->config, sizeof(struct frad_conf));
	len += sizeof(struct frad_conf);

	sdla_cmd(dev, SDLA_DISABLE_COMMUNICATIONS, 0, 0, NULL, 0, NULL, NULL);
	sdla_cmd(dev, SDLA_SET_DLCI_CONFIGURATION, 0, 0, &data, len, NULL, NULL);
	sdla_cmd(dev, SDLA_ENABLE_COMMUNICATIONS, 0, 0, NULL, 0, NULL, NULL);

	return 0;
}

static int sdla_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct frad_local *flp;

	if(!capable(CAP_NET_ADMIN))
		return -EPERM;
		
	flp = netdev_priv(dev);

	if (!flp->initialized)
		return -EINVAL;

	switch (cmd)
	{
		case FRAD_GET_CONF:
		case FRAD_SET_CONF:
			return sdla_config(dev, ifr->ifr_data, cmd == FRAD_GET_CONF);

		case SDLA_IDENTIFY:
			ifr->ifr_flags = flp->type;
			break;

		case SDLA_CPUSPEED:
			return sdla_cpuspeed(dev, ifr);

/* ==========================================================
NOTE:  This is rather a useless action right now, as the
       current driver does not support protocols other than
       FR.  However, Sangoma has modules for a number of
       other protocols in the works.
============================================================*/
		case SDLA_PROTOCOL:
			if (flp->configured)
				return -EALREADY;

			switch (ifr->ifr_flags)
			{
				case ARPHRD_FRAD:
					dev->type = ifr->ifr_flags;
					break;
				default:
					return -ENOPROTOOPT;
			}
			break;

		case SDLA_CLEARMEM:
			sdla_clear(dev);
			break;

		case SDLA_WRITEMEM:
		case SDLA_READMEM:
			if(!capable(CAP_SYS_RAWIO))
				return -EPERM;
			return sdla_xfer(dev, ifr->ifr_data, cmd == SDLA_READMEM);

		case SDLA_START:
			sdla_start(dev);
			break;

		case SDLA_STOP:
			sdla_stop(dev);
			break;

		default:
			return -EOPNOTSUPP;
	}
	return 0;
}

static int sdla_change_mtu(struct net_device *dev, int new_mtu)
{
	if (netif_running(dev))
		return -EBUSY;

	/* for now, you can't change the MTU! */
	return -EOPNOTSUPP;
}

static int sdla_set_config(struct net_device *dev, struct ifmap *map)
{
	struct frad_local *flp;
	int               i;
	char              byte;
	unsigned base;
	int err = -EINVAL;

	flp = netdev_priv(dev);

	if (flp->initialized)
		return -EINVAL;

	for(i=0; i < ARRAY_SIZE(valid_port); i++)
		if (valid_port[i] == map->base_addr)
			break;   

	if (i == ARRAY_SIZE(valid_port))
		return -EINVAL;

	if (!request_region(map->base_addr, SDLA_IO_EXTENTS, dev->name)){
		pr_warn("io-port 0x%04lx in use\n", dev->base_addr);
		return -EINVAL;
	}
	base = map->base_addr;

	/* test for card types, S502A, S502E, S507, S508                 */
	/* these tests shut down the card completely, so clear the state */
	flp->type = SDLA_UNKNOWN;
	flp->state = 0;
   
	for(i=1;i<SDLA_IO_EXTENTS;i++)
		if (inb(base + i) != 0xFF)
			break;

	if (i == SDLA_IO_EXTENTS) {   
		outb(SDLA_HALT, base + SDLA_REG_Z80_CONTROL);
		if ((inb(base + SDLA_S502_STS) & 0x0F) == 0x08) {
			outb(SDLA_S502E_INTACK, base + SDLA_REG_CONTROL);
			if ((inb(base + SDLA_S502_STS) & 0x0F) == 0x0C) {
				outb(SDLA_HALT, base + SDLA_REG_CONTROL);
				flp->type = SDLA_S502E;
				goto got_type;
			}
		}
	}

	for(byte=inb(base),i=0;i<SDLA_IO_EXTENTS;i++)
		if (inb(base + i) != byte)
			break;

	if (i == SDLA_IO_EXTENTS) {
		outb(SDLA_HALT, base + SDLA_REG_CONTROL);
		if ((inb(base + SDLA_S502_STS) & 0x7E) == 0x30) {
			outb(SDLA_S507_ENABLE, base + SDLA_REG_CONTROL);
			if ((inb(base + SDLA_S502_STS) & 0x7E) == 0x32) {
				outb(SDLA_HALT, base + SDLA_REG_CONTROL);
				flp->type = SDLA_S507;
				goto got_type;
			}
		}
	}

	outb(SDLA_HALT, base + SDLA_REG_CONTROL);
	if ((inb(base + SDLA_S508_STS) & 0x3F) == 0x00) {
		outb(SDLA_S508_INTEN, base + SDLA_REG_CONTROL);
		if ((inb(base + SDLA_S508_STS) & 0x3F) == 0x10) {
			outb(SDLA_HALT, base + SDLA_REG_CONTROL);
			flp->type = SDLA_S508;
			goto got_type;
		}
	}

	outb(SDLA_S502A_HALT, base + SDLA_REG_CONTROL);
	if (inb(base + SDLA_S502_STS) == 0x40) {
		outb(SDLA_S502A_START, base + SDLA_REG_CONTROL);
		if (inb(base + SDLA_S502_STS) == 0x40) {
			outb(SDLA_S502A_INTEN, base + SDLA_REG_CONTROL);
			if (inb(base + SDLA_S502_STS) == 0x44) {
				outb(SDLA_S502A_START, base + SDLA_REG_CONTROL);
				flp->type = SDLA_S502A;
				goto got_type;
			}
		}
	}

	netdev_notice(dev, "Unknown card type\n");
	err = -ENODEV;
	goto fail;

got_type:
	switch(base) {
		case 0x270:
		case 0x280:
		case 0x380: 
		case 0x390:
			if (flp->type != SDLA_S508 && flp->type != SDLA_S507)
				goto fail;
	}

	switch (map->irq) {
		case 2:
			if (flp->type != SDLA_S502E)
				goto fail;
			break;

		case 10:
		case 11:
		case 12:
		case 15:
		case 4:
			if (flp->type != SDLA_S508 && flp->type != SDLA_S507)
				goto fail;
			break;
		case 3:
		case 5:
		case 7:
			if (flp->type == SDLA_S502A)
				goto fail;
			break;

		default:
			goto fail;
	}

	err = -EAGAIN;
	if (request_irq(dev->irq, sdla_isr, 0, dev->name, dev)) 
		goto fail;

	if (flp->type == SDLA_S507) {
		switch(dev->irq) {
			case 3:
				flp->state = SDLA_S507_IRQ3;
				break;
			case 4:
				flp->state = SDLA_S507_IRQ4;
				break;
			case 5:
				flp->state = SDLA_S507_IRQ5;
				break;
			case 7:
				flp->state = SDLA_S507_IRQ7;
				break;
			case 10:
				flp->state = SDLA_S507_IRQ10;
				break;
			case 11:
				flp->state = SDLA_S507_IRQ11;
				break;
			case 12:
				flp->state = SDLA_S507_IRQ12;
				break;
			case 15:
				flp->state = SDLA_S507_IRQ15;
				break;
		}
	}

	for(i=0; i < ARRAY_SIZE(valid_mem); i++)
		if (valid_mem[i] == map->mem_start)
			break;   

	err = -EINVAL;
	if (i == ARRAY_SIZE(valid_mem))
		goto fail2;

	if (flp->type == SDLA_S502A && (map->mem_start & 0xF000) >> 12 == 0x0E)
		goto fail2;

	if (flp->type != SDLA_S507 && map->mem_start >> 16 == 0x0B)
		goto fail2;

	if (flp->type == SDLA_S507 && map->mem_start >> 16 == 0x0D)
		goto fail2;

	byte = flp->type != SDLA_S508 ? SDLA_8K_WINDOW : 0;
	byte |= (map->mem_start & 0xF000) >> (12 + (flp->type == SDLA_S508 ? 1 : 0));
	switch(flp->type) {
		case SDLA_S502A:
		case SDLA_S502E:
			switch (map->mem_start >> 16) {
				case 0x0A:
					byte |= SDLA_S502_SEG_A;
					break;
				case 0x0C:
					byte |= SDLA_S502_SEG_C;
					break;
				case 0x0D:
					byte |= SDLA_S502_SEG_D;
					break;
				case 0x0E:
					byte |= SDLA_S502_SEG_E;
					break;
			}
			break;
		case SDLA_S507:
			switch (map->mem_start >> 16) {
				case 0x0A:
					byte |= SDLA_S507_SEG_A;
					break;
				case 0x0B:
					byte |= SDLA_S507_SEG_B;
					break;
				case 0x0C:
					byte |= SDLA_S507_SEG_C;
					break;
				case 0x0E:
					byte |= SDLA_S507_SEG_E;
					break;
			}
			break;
		case SDLA_S508:
			switch (map->mem_start >> 16) {
				case 0x0A:
					byte |= SDLA_S508_SEG_A;
					break;
				case 0x0C:
					byte |= SDLA_S508_SEG_C;
					break;
				case 0x0D:
					byte |= SDLA_S508_SEG_D;
					break;
				case 0x0E:
					byte |= SDLA_S508_SEG_E;
					break;
			}
			break;
	}

	/* set the memory bits, and enable access */
	outb(byte, base + SDLA_REG_PC_WINDOW);

	switch(flp->type)
	{
		case SDLA_S502E:
			flp->state = SDLA_S502E_ENABLE;
			break;
		case SDLA_S507:
			flp->state |= SDLA_MEMEN;
			break;
		case SDLA_S508:
			flp->state = SDLA_MEMEN;
			break;
	}
	outb(flp->state, base + SDLA_REG_CONTROL);

	dev->irq = map->irq;
	dev->base_addr = base;
	dev->mem_start = map->mem_start;
	dev->mem_end = dev->mem_start + 0x2000;
	flp->initialized = 1;
	return 0;

fail2:
	free_irq(map->irq, dev);
fail:
	release_region(base, SDLA_IO_EXTENTS);
	return err;
}
 
static const struct net_device_ops sdla_netdev_ops = {
	.ndo_open	= sdla_open,
	.ndo_stop	= sdla_close,
	.ndo_do_ioctl	= sdla_ioctl,
	.ndo_set_config	= sdla_set_config,
	.ndo_start_xmit	= sdla_transmit,
	.ndo_change_mtu	= sdla_change_mtu,
};

static void setup_sdla(struct net_device *dev)
{
	struct frad_local *flp = netdev_priv(dev);

	netdev_boot_setup_check(dev);

	dev->netdev_ops		= &sdla_netdev_ops;
	dev->flags		= 0;
	dev->type		= 0xFFFF;
	dev->hard_header_len	= 0;
	dev->addr_len		= 0;
	dev->mtu		= SDLA_MAX_MTU;

	flp->activate		= sdla_activate;
	flp->deactivate		= sdla_deactivate;
	flp->assoc		= sdla_assoc;
	flp->deassoc		= sdla_deassoc;
	flp->dlci_conf		= sdla_dlci_conf;
	flp->dev		= dev;

	timer_setup(&flp->timer, sdla_poll, 0);
	flp->timer.expires	= 1;
}

static struct net_device *sdla;

static int __init init_sdla(void)
{
	int err;

	printk("%s.\n", version);

	sdla = alloc_netdev(sizeof(struct frad_local), "sdla0",
			    NET_NAME_UNKNOWN, setup_sdla);
	if (!sdla) 
		return -ENOMEM;

	err = register_netdev(sdla);
	if (err) 
		free_netdev(sdla);

	return err;
}

static void __exit exit_sdla(void)
{
	struct frad_local *flp = netdev_priv(sdla);

	unregister_netdev(sdla);
	if (flp->initialized) {
		free_irq(sdla->irq, sdla);
		release_region(sdla->base_addr, SDLA_IO_EXTENTS);
	}
	del_timer_sync(&flp->timer);
	free_netdev(sdla);
}

MODULE_LICENSE("GPL");

module_init(init_sdla);
module_exit(exit_sdla);

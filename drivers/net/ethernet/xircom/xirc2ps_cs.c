/* [xirc2ps_cs.c wk 03.11.99] (1.40 1999/11/18 00:06:03)
 * Xircom CreditCard Ethernet Adapter IIps driver
 * Xircom Realport 10/100 (RE-100) driver 
 *
 * This driver supports various Xircom CreditCard Ethernet adapters
 * including the CE2, CE IIps, RE-10, CEM28, CEM33, CE33, CEM56,
 * CE3-100, CE3B, RE-100, REM10BT, and REM56G-100.
 *
 * 2000-09-24 <psheer@icon.co.za> The Xircom CE3B-100 may not
 * autodetect the media properly. In this case use the
 * if_port=1 (for 10BaseT) or if_port=4 (for 100BaseT) options
 * to force the media type.
 * 
 * Written originally by Werner Koch based on David Hinds' skeleton of the
 * PCMCIA driver.
 *
 * Copyright (c) 1997,1998 Werner Koch (dd9jn)
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * It is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 *
 * ALTERNATIVELY, this driver may be distributed under the terms of
 * the following license, in which case the provisions of this license
 * are required INSTEAD OF the GNU General Public License.  (This clause
 * is necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/bitops.h>
#include <linux/mii.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ciscode.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#ifndef MANFID_COMPAQ
  #define MANFID_COMPAQ 	   0x0138
  #define MANFID_COMPAQ2	   0x0183  /* is this correct? */
#endif

#include <pcmcia/ds.h>

/* Time in jiffies before concluding Tx hung */
#define TX_TIMEOUT	((400*HZ)/1000)

/****************
 * Some constants used to access the hardware
 */

/* Register offsets and value constans */
#define XIRCREG_CR  0	/* Command register (wr) */
enum xirc_cr {
    TransmitPacket = 0x01,
    SoftReset = 0x02,
    EnableIntr = 0x04,
    ForceIntr  = 0x08,
    ClearTxFIFO = 0x10,
    ClearRxOvrun = 0x20,
    RestartTx	 = 0x40
};
#define XIRCREG_ESR 0	/* Ethernet status register (rd) */
enum xirc_esr {
    FullPktRcvd = 0x01, /* full packet in receive buffer */
    PktRejected = 0x04, /* a packet has been rejected */
    TxPktPend = 0x08,	/* TX Packet Pending */
    IncorPolarity = 0x10,
    MediaSelect = 0x20	/* set if TP, clear if AUI */
};
#define XIRCREG_PR  1	/* Page Register select */
#define XIRCREG_EDP 4	/* Ethernet Data Port Register */
#define XIRCREG_ISR 6	/* Ethernet Interrupt Status Register */
enum xirc_isr {
    TxBufOvr = 0x01,	/* TX Buffer Overflow */
    PktTxed  = 0x02,	/* Packet Transmitted */
    MACIntr  = 0x04,	/* MAC Interrupt occurred */
    TxResGrant = 0x08,	/* Tx Reservation Granted */
    RxFullPkt = 0x20,	/* Rx Full Packet */
    RxPktRej  = 0x40,	/* Rx Packet Rejected */
    ForcedIntr= 0x80	/* Forced Interrupt */
};
#define XIRCREG1_IMR0 12 /* Ethernet Interrupt Mask Register (on page 1)*/
#define XIRCREG1_IMR1 13
#define XIRCREG0_TSO  8  /* Transmit Space Open Register (on page 0)*/
#define XIRCREG0_TRS  10 /* Transmit reservation Size Register (page 0)*/
#define XIRCREG0_DO   12 /* Data Offset Register (page 0) (wr) */
#define XIRCREG0_RSR  12 /* Receive Status Register (page 0) (rd) */
enum xirc_rsr {
    PhyPkt = 0x01,	/* set:physical packet, clear: multicast packet */
    BrdcstPkt = 0x02,	/* set if it is a broadcast packet */
    PktTooLong = 0x04,	/* set if packet length > 1518 */
    AlignErr = 0x10,	/* incorrect CRC and last octet not complete */
    CRCErr = 0x20,	/* incorrect CRC and last octet is complete */
    PktRxOk = 0x80	/* received ok */
};
#define XIRCREG0_PTR 13 /* packets transmitted register (rd) */
#define XIRCREG0_RBC 14 /* receive byte count regsister (rd) */
#define XIRCREG1_ECR 14 /* ethernet configurationn register */
enum xirc_ecr {
    FullDuplex = 0x04,	/* enable full duplex mode */
    LongTPMode = 0x08,	/* adjust for longer lengths of TP cable */
    DisablePolCor = 0x10,/* disable auto polarity correction */
    DisableLinkPulse = 0x20, /* disable link pulse generation */
    DisableAutoTx = 0x40, /* disable auto-transmit */
};
#define XIRCREG2_RBS 8	/* receive buffer start register */
#define XIRCREG2_LED 10 /* LED Configuration register */
/* values for the leds:    Bits 2-0 for led 1
 *  0 disabled		   Bits 5-3 for led 2
 *  1 collision
 *  2 noncollision
 *  3 link_detected
 *  4 incor_polarity
 *  5 jabber
 *  6 auto_assertion
 *  7 rx_tx_activity
 */
#define XIRCREG2_MSR 12 /* Mohawk specific register */

#define XIRCREG4_GPR0 8 /* General Purpose Register 0 */
#define XIRCREG4_GPR1 9 /* General Purpose Register 1 */
#define XIRCREG2_GPR2 13 /* General Purpose Register 2 (page2!)*/
#define XIRCREG4_BOV 10 /* Bonding Version Register */
#define XIRCREG4_LMA 12 /* Local Memory Address Register */
#define XIRCREG4_LMD 14 /* Local Memory Data Port */
/* MAC register can only by accessed with 8 bit operations */
#define XIRCREG40_CMD0 8    /* Command Register (wr) */
enum xirc_cmd { 	    /* Commands */
    Transmit = 0x01,
    EnableRecv = 0x04,
    DisableRecv = 0x08,
    Abort = 0x10,
    Online = 0x20,
    IntrAck = 0x40,
    Offline = 0x80
};
#define XIRCREG5_RHSA0	10  /* Rx Host Start Address */
#define XIRCREG40_RXST0 9   /* Receive Status Register */
#define XIRCREG40_TXST0 11  /* Transmit Status Register 0 */
#define XIRCREG40_TXST1 12  /* Transmit Status Register 10 */
#define XIRCREG40_RMASK0 13  /* Receive Mask Register */
#define XIRCREG40_TMASK0 14  /* Transmit Mask Register 0 */
#define XIRCREG40_TMASK1 15  /* Transmit Mask Register 0 */
#define XIRCREG42_SWC0	8   /* Software Configuration 0 */
#define XIRCREG42_SWC1	9   /* Software Configuration 1 */
#define XIRCREG42_BOC	10  /* Back-Off Configuration */
#define XIRCREG44_TDR0	8   /* Time Domain Reflectometry 0 */
#define XIRCREG44_TDR1	9   /* Time Domain Reflectometry 1 */
#define XIRCREG44_RXBC_LO 10 /* Rx Byte Count 0 (rd) */
#define XIRCREG44_RXBC_HI 11 /* Rx Byte Count 1 (rd) */
#define XIRCREG45_REV	 15 /* Revision Register (rd) */
#define XIRCREG50_IA	8   /* Individual Address (8-13) */

static const char *if_names[] = { "Auto", "10BaseT", "10Base2", "AUI", "100BaseT" };

/* card types */
#define XIR_UNKNOWN  0	/* unknown: not supported */
#define XIR_CE	     1	/* (prodid 1) different hardware: not supported */
#define XIR_CE2      2	/* (prodid 2) */
#define XIR_CE3      3	/* (prodid 3) */
#define XIR_CEM      4	/* (prodid 1) different hardware: not supported */
#define XIR_CEM2     5	/* (prodid 2) */
#define XIR_CEM3     6	/* (prodid 3) */
#define XIR_CEM33    7	/* (prodid 4) */
#define XIR_CEM56M   8	/* (prodid 5) */
#define XIR_CEM56    9	/* (prodid 6) */
#define XIR_CM28    10	/* (prodid 3) modem only: not supported here */
#define XIR_CM33    11	/* (prodid 4) modem only: not supported here */
#define XIR_CM56    12	/* (prodid 5) modem only: not supported here */
#define XIR_CG	    13	/* (prodid 1) GSM modem only: not supported */
#define XIR_CBE     14	/* (prodid 1) cardbus ethernet: not supported */
/*====================================================================*/

/* Module parameters */

MODULE_DESCRIPTION("Xircom PCMCIA ethernet driver");
MODULE_LICENSE("Dual MPL/GPL");

#define INT_MODULE_PARM(n, v) static int n = v; module_param(n, int, 0)

INT_MODULE_PARM(if_port,	0);
INT_MODULE_PARM(full_duplex,	0);
INT_MODULE_PARM(do_sound, 	1);
INT_MODULE_PARM(lockup_hack,	0);  /* anti lockup hack */

/*====================================================================*/

/* We do not process more than these number of bytes during one
 * interrupt. (Of course we receive complete packets, so this is not
 * an exact value).
 * Something between 2000..22000; first value gives best interrupt latency,
 * the second enables the usage of the complete on-chip buffer. We use the
 * high value as the initial value.
 */
static unsigned maxrx_bytes = 22000;

/* MII management prototypes */
static void mii_idle(unsigned int ioaddr);
static void mii_putbit(unsigned int ioaddr, unsigned data);
static int  mii_getbit(unsigned int ioaddr);
static void mii_wbits(unsigned int ioaddr, unsigned data, int len);
static unsigned mii_rd(unsigned int ioaddr, u_char phyaddr, u_char phyreg);
static void mii_wr(unsigned int ioaddr, u_char phyaddr, u_char phyreg,
		   unsigned data, int len);

static int has_ce2_string(struct pcmcia_device * link);
static int xirc2ps_config(struct pcmcia_device * link);
static void xirc2ps_release(struct pcmcia_device * link);
static void xirc2ps_detach(struct pcmcia_device *p_dev);

static irqreturn_t xirc2ps_interrupt(int irq, void *dev_id);

typedef struct local_info_t {
	struct net_device	*dev;
	struct pcmcia_device	*p_dev;

    int card_type;
    int probe_port;
    int silicon; /* silicon revision. 0=old CE2, 1=Scipper, 4=Mohawk */
    int mohawk;  /* a CE3 type card */
    int dingo;	 /* a CEM56 type card */
    int new_mii; /* has full 10baseT/100baseT MII */
    int modem;	 /* is a multi function card (i.e with a modem) */
    void __iomem *dingo_ccr; /* only used for CEM56 cards */
    unsigned last_ptr_value; /* last packets transmitted value */
    const char *manf_str;
    struct work_struct tx_timeout_task;
} local_info_t;

/****************
 * Some more prototypes
 */
static netdev_tx_t do_start_xmit(struct sk_buff *skb,
				       struct net_device *dev);
static void xirc_tx_timeout(struct net_device *dev);
static void xirc2ps_tx_timeout_task(struct work_struct *work);
static void set_addresses(struct net_device *dev);
static void set_multicast_list(struct net_device *dev);
static int set_card_type(struct pcmcia_device *link);
static int do_config(struct net_device *dev, struct ifmap *map);
static int do_open(struct net_device *dev);
static int do_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static const struct ethtool_ops netdev_ethtool_ops;
static void hardreset(struct net_device *dev);
static void do_reset(struct net_device *dev, int full);
static int init_mii(struct net_device *dev);
static void do_powerdown(struct net_device *dev);
static int do_stop(struct net_device *dev);

/*=============== Helper functions =========================*/
#define SelectPage(pgnr)   outb((pgnr), ioaddr + XIRCREG_PR)
#define GetByte(reg)	   ((unsigned)inb(ioaddr + (reg)))
#define GetWord(reg)	   ((unsigned)inw(ioaddr + (reg)))
#define PutByte(reg,value) outb((value), ioaddr+(reg))
#define PutWord(reg,value) outw((value), ioaddr+(reg))

/*====== Functions used for debugging =================================*/
#if 0 /* reading regs may change system status */
static void
PrintRegisters(struct net_device *dev)
{
    unsigned int ioaddr = dev->base_addr;

    if (pc_debug > 1) {
	int i, page;

	printk(KERN_DEBUG pr_fmt("Register  common: "));
	for (i = 0; i < 8; i++)
	    pr_cont(" %2.2x", GetByte(i));
	pr_cont("\n");
	for (page = 0; page <= 8; page++) {
	    printk(KERN_DEBUG pr_fmt("Register page %2x: "), page);
	    SelectPage(page);
	    for (i = 8; i < 16; i++)
		pr_cont(" %2.2x", GetByte(i));
	    pr_cont("\n");
	}
	for (page=0x40 ; page <= 0x5f; page++) {
		if (page == 0x43 || (page >= 0x46 && page <= 0x4f) ||
		    (page >= 0x51 && page <=0x5e))
			continue;
	    printk(KERN_DEBUG pr_fmt("Register page %2x: "), page);
	    SelectPage(page);
	    for (i = 8; i < 16; i++)
		pr_cont(" %2.2x", GetByte(i));
	    pr_cont("\n");
	}
    }
}
#endif /* 0 */

/*============== MII Management functions ===============*/

/****************
 * Turn around for read
 */
static void
mii_idle(unsigned int ioaddr)
{
    PutByte(XIRCREG2_GPR2, 0x04|0); /* drive MDCK low */
    udelay(1);
    PutByte(XIRCREG2_GPR2, 0x04|1); /* and drive MDCK high */
    udelay(1);
}

/****************
 * Write a bit to MDI/O
 */
static void
mii_putbit(unsigned int ioaddr, unsigned data)
{
  #if 1
    if (data) {
	PutByte(XIRCREG2_GPR2, 0x0c|2|0); /* set MDIO */
	udelay(1);
	PutByte(XIRCREG2_GPR2, 0x0c|2|1); /* and drive MDCK high */
	udelay(1);
    } else {
	PutByte(XIRCREG2_GPR2, 0x0c|0|0); /* clear MDIO */
	udelay(1);
	PutByte(XIRCREG2_GPR2, 0x0c|0|1); /* and drive MDCK high */
	udelay(1);
    }
  #else
    if (data) {
	PutWord(XIRCREG2_GPR2-1, 0x0e0e);
	udelay(1);
	PutWord(XIRCREG2_GPR2-1, 0x0f0f);
	udelay(1);
    } else {
	PutWord(XIRCREG2_GPR2-1, 0x0c0c);
	udelay(1);
	PutWord(XIRCREG2_GPR2-1, 0x0d0d);
	udelay(1);
    }
  #endif
}

/****************
 * Get a bit from MDI/O
 */
static int
mii_getbit(unsigned int ioaddr)
{
    unsigned d;

    PutByte(XIRCREG2_GPR2, 4|0); /* drive MDCK low */
    udelay(1);
    d = GetByte(XIRCREG2_GPR2); /* read MDIO */
    PutByte(XIRCREG2_GPR2, 4|1); /* drive MDCK high again */
    udelay(1);
    return d & 0x20; /* read MDIO */
}

static void
mii_wbits(unsigned int ioaddr, unsigned data, int len)
{
    unsigned m = 1 << (len-1);
    for (; m; m >>= 1)
	mii_putbit(ioaddr, data & m);
}

static unsigned
mii_rd(unsigned int ioaddr,	u_char phyaddr, u_char phyreg)
{
    int i;
    unsigned data=0, m;

    SelectPage(2);
    for (i=0; i < 32; i++)		/* 32 bit preamble */
	mii_putbit(ioaddr, 1);
    mii_wbits(ioaddr, 0x06, 4); 	/* Start and opcode for read */
    mii_wbits(ioaddr, phyaddr, 5);	/* PHY address to be accessed */
    mii_wbits(ioaddr, phyreg, 5);	/* PHY register to read */
    mii_idle(ioaddr);			/* turn around */
    mii_getbit(ioaddr);

    for (m = 1<<15; m; m >>= 1)
	if (mii_getbit(ioaddr))
	    data |= m;
    mii_idle(ioaddr);
    return data;
}

static void
mii_wr(unsigned int ioaddr, u_char phyaddr, u_char phyreg, unsigned data,
       int len)
{
    int i;

    SelectPage(2);
    for (i=0; i < 32; i++)		/* 32 bit preamble */
	mii_putbit(ioaddr, 1);
    mii_wbits(ioaddr, 0x05, 4); 	/* Start and opcode for write */
    mii_wbits(ioaddr, phyaddr, 5);	/* PHY address to be accessed */
    mii_wbits(ioaddr, phyreg, 5);	/* PHY Register to write */
    mii_putbit(ioaddr, 1);		/* turn around */
    mii_putbit(ioaddr, 0);
    mii_wbits(ioaddr, data, len);	/* And write the data */
    mii_idle(ioaddr);
}

/*============= Main bulk of functions	=========================*/

static const struct net_device_ops netdev_ops = {
	.ndo_open		= do_open,
	.ndo_stop		= do_stop,
	.ndo_start_xmit		= do_start_xmit,
	.ndo_tx_timeout 	= xirc_tx_timeout,
	.ndo_set_config		= do_config,
	.ndo_do_ioctl		= do_ioctl,
	.ndo_set_rx_mode	= set_multicast_list,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int
xirc2ps_probe(struct pcmcia_device *link)
{
    struct net_device *dev;
    local_info_t *local;

    dev_dbg(&link->dev, "attach()\n");

    /* Allocate the device structure */
    dev = alloc_etherdev(sizeof(local_info_t));
    if (!dev)
	    return -ENOMEM;
    local = netdev_priv(dev);
    local->dev = dev;
    local->p_dev = link;
    link->priv = dev;

    /* General socket configuration */
    link->config_index = 1;

    /* Fill in card specific entries */
    dev->netdev_ops = &netdev_ops;
    dev->ethtool_ops = &netdev_ethtool_ops;
    dev->watchdog_timeo = TX_TIMEOUT;
    INIT_WORK(&local->tx_timeout_task, xirc2ps_tx_timeout_task);

    return xirc2ps_config(link);
} /* xirc2ps_attach */

static void
xirc2ps_detach(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;

    dev_dbg(&link->dev, "detach\n");

    unregister_netdev(dev);

    xirc2ps_release(link);

    free_netdev(dev);
} /* xirc2ps_detach */

/****************
 * Detect the type of the card. s is the buffer with the data of tuple 0x20
 * Returns: 0 := not supported
 *		       mediaid=11 and prodid=47
 * Media-Id bits:
 *  Ethernet	    0x01
 *  Tokenring	    0x02
 *  Arcnet	    0x04
 *  Wireless	    0x08
 *  Modem	    0x10
 *  GSM only	    0x20
 * Prod-Id bits:
 *  Pocket	    0x10
 *  External	    0x20
 *  Creditcard	    0x40
 *  Cardbus	    0x80
 *
 */
static int
set_card_type(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    local_info_t *local = netdev_priv(dev);
    u8 *buf;
    unsigned int cisrev, mediaid, prodid;
    size_t len;

    len = pcmcia_get_tuple(link, CISTPL_MANFID, &buf);
    if (len < 5) {
	    dev_err(&link->dev, "invalid CIS -- sorry\n");
	    return 0;
    }

    cisrev = buf[2];
    mediaid = buf[3];
    prodid = buf[4];

    dev_dbg(&link->dev, "cisrev=%02x mediaid=%02x prodid=%02x\n",
	  cisrev, mediaid, prodid);

    local->mohawk = 0;
    local->dingo = 0;
    local->modem = 0;
    local->card_type = XIR_UNKNOWN;
    if (!(prodid & 0x40)) {
	pr_notice("Oops: Not a creditcard\n");
	return 0;
    }
    if (!(mediaid & 0x01)) {
	pr_notice("Not an Ethernet card\n");
	return 0;
    }
    if (mediaid & 0x10) {
	local->modem = 1;
	switch(prodid & 15) {
	  case 1: local->card_type = XIR_CEM   ; break;
	  case 2: local->card_type = XIR_CEM2  ; break;
	  case 3: local->card_type = XIR_CEM3  ; break;
	  case 4: local->card_type = XIR_CEM33 ; break;
	  case 5: local->card_type = XIR_CEM56M;
		  local->mohawk = 1;
		  break;
	  case 6:
	  case 7: /* 7 is the RealPort 10/56 */
		  local->card_type = XIR_CEM56 ;
		  local->mohawk = 1;
		  local->dingo = 1;
		  break;
	}
    } else {
	switch(prodid & 15) {
	  case 1: local->card_type = has_ce2_string(link)? XIR_CE2 : XIR_CE ;
		  break;
	  case 2: local->card_type = XIR_CE2; break;
	  case 3: local->card_type = XIR_CE3;
		  local->mohawk = 1;
		  break;
	}
    }
    if (local->card_type == XIR_CE || local->card_type == XIR_CEM) {
	pr_notice("Sorry, this is an old CE card\n");
	return 0;
    }
    if (local->card_type == XIR_UNKNOWN)
	pr_notice("unknown card (mediaid=%02x prodid=%02x)\n", mediaid, prodid);

    return 1;
}

/****************
 * There are some CE2 cards out which claim to be a CE card.
 * This function looks for a "CE2" in the 3rd version field.
 * Returns: true if this is a CE2
 */
static int
has_ce2_string(struct pcmcia_device * p_dev)
{
	if (p_dev->prod_id[2] && strstr(p_dev->prod_id[2], "CE2"))
		return 1;
	return 0;
}

static int
xirc2ps_config_modem(struct pcmcia_device *p_dev, void *priv_data)
{
	unsigned int ioaddr;

	if ((p_dev->resource[0]->start & 0xf) == 8)
		return -ENODEV;

	p_dev->resource[0]->end = 16;
	p_dev->resource[1]->end = 8;
	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_16;
	p_dev->resource[1]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[1]->flags |= IO_DATA_PATH_WIDTH_8;
	p_dev->io_lines = 10;

	p_dev->resource[1]->start = p_dev->resource[0]->start;
	for (ioaddr = 0x300; ioaddr < 0x400; ioaddr += 0x10) {
		p_dev->resource[0]->start = ioaddr;
		if (!pcmcia_request_io(p_dev))
			return 0;
	}
	return -ENODEV;
}

static int
xirc2ps_config_check(struct pcmcia_device *p_dev, void *priv_data)
{
	int *pass = priv_data;
	resource_size_t tmp = p_dev->resource[1]->start;

	tmp += (*pass ? (p_dev->config_index & 0x20 ? -24 : 8)
		: (p_dev->config_index & 0x20 ?   8 : -24));

	if ((p_dev->resource[0]->start & 0xf) == 8)
		return -ENODEV;

	p_dev->resource[0]->end = 18;
	p_dev->resource[1]->end = 8;
	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_16;
	p_dev->resource[1]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[1]->flags |= IO_DATA_PATH_WIDTH_8;
	p_dev->io_lines = 10;

	p_dev->resource[1]->start = p_dev->resource[0]->start;
	p_dev->resource[0]->start = tmp;
	return pcmcia_request_io(p_dev);
}


static int pcmcia_get_mac_ce(struct pcmcia_device *p_dev,
			     tuple_t *tuple,
			     void *priv)
{
	struct net_device *dev = priv;
	int i;

	if (tuple->TupleDataLen != 13)
		return -EINVAL;
	if ((tuple->TupleData[0] != 2) || (tuple->TupleData[1] != 1) ||
		(tuple->TupleData[2] != 6))
		return -EINVAL;
	/* another try	(James Lehmer's CE2 version 4.1)*/
	for (i = 2; i < 6; i++)
		dev->dev_addr[i] = tuple->TupleData[i+2];
	return 0;
};


static int
xirc2ps_config(struct pcmcia_device * link)
{
    struct net_device *dev = link->priv;
    local_info_t *local = netdev_priv(dev);
    unsigned int ioaddr;
    int err;
    u8 *buf;
    size_t len;

    local->dingo_ccr = NULL;

    dev_dbg(&link->dev, "config\n");

    /* Is this a valid	card */
    if (link->has_manf_id == 0) {
	pr_notice("manfid not found in CIS\n");
	goto failure;
    }

    switch (link->manf_id) {
      case MANFID_XIRCOM:
	local->manf_str = "Xircom";
	break;
      case MANFID_ACCTON:
	local->manf_str = "Accton";
	break;
      case MANFID_COMPAQ:
      case MANFID_COMPAQ2:
	local->manf_str = "Compaq";
	break;
      case MANFID_INTEL:
	local->manf_str = "Intel";
	break;
      case MANFID_TOSHIBA:
	local->manf_str = "Toshiba";
	break;
      default:
	pr_notice("Unknown Card Manufacturer ID: 0x%04x\n",
		  (unsigned)link->manf_id);
	goto failure;
    }
    dev_dbg(&link->dev, "found %s card\n", local->manf_str);

    if (!set_card_type(link)) {
	pr_notice("this card is not supported\n");
	goto failure;
    }

    /* get the ethernet address from the CIS */
    err = pcmcia_get_mac_from_cis(link, dev);

    /* not found: try to get the node-id from tuple 0x89 */
    if (err) {
	    len = pcmcia_get_tuple(link, 0x89, &buf);
	    /* data layout looks like tuple 0x22 */
	    if (buf && len == 8) {
		    if (*buf == CISTPL_FUNCE_LAN_NODE_ID) {
			    int i;
			    for (i = 2; i < 6; i++)
				    dev->dev_addr[i] = buf[i+2];
		    } else
			    err = -1;
	    }
	    kfree(buf);
    }

    if (err)
	err = pcmcia_loop_tuple(link, CISTPL_FUNCE, pcmcia_get_mac_ce, dev);

    if (err) {
	pr_notice("node-id not found in CIS\n");
	goto failure;
    }

    if (local->modem) {
	int pass;
	link->config_flags |= CONF_AUTO_SET_IO;

	if (local->dingo) {
	    /* Take the Modem IO port from the CIS and scan for a free
	     * Ethernet port */
	    if (!pcmcia_loop_config(link, xirc2ps_config_modem, NULL))
		    goto port_found;
	} else {
	    /* We do 2 passes here: The first one uses the regular mapping and
	     * the second tries again, thereby considering that the 32 ports are
	     * mirrored every 32 bytes. Actually we use a mirrored port for
	     * the Mako if (on the first pass) the COR bit 5 is set.
	     */
	    for (pass=0; pass < 2; pass++)
		    if (!pcmcia_loop_config(link, xirc2ps_config_check,
						    &pass))
			    goto port_found;
	    /* if special option:
	     * try to configure as Ethernet only.
	     * .... */
	}
	pr_notice("no ports available\n");
    } else {
	link->io_lines = 10;
	link->resource[0]->end = 16;
	link->resource[0]->flags |= IO_DATA_PATH_WIDTH_16;
	for (ioaddr = 0x300; ioaddr < 0x400; ioaddr += 0x10) {
	    link->resource[0]->start = ioaddr;
	    if (!(err = pcmcia_request_io(link)))
		goto port_found;
	}
	link->resource[0]->start = 0; /* let CS decide */
	if ((err = pcmcia_request_io(link)))
	    goto config_error;
    }
  port_found:
    if (err)
	 goto config_error;

    /****************
     * Now allocate an interrupt line.	Note that this does not
     * actually assign a handler to the interrupt.
     */
    if ((err=pcmcia_request_irq(link, xirc2ps_interrupt)))
	goto config_error;

    link->config_flags |= CONF_ENABLE_IRQ;
    if (do_sound)
	    link->config_flags |= CONF_ENABLE_SPKR;

    if ((err = pcmcia_enable_device(link)))
	goto config_error;

    if (local->dingo) {
	/* Reset the modem's BAR to the correct value
	 * This is necessary because in the RequestConfiguration call,
	 * the base address of the ethernet port (BasePort1) is written
	 * to the BAR registers of the modem.
	 */
	err = pcmcia_write_config_byte(link, CISREG_IOBASE_0, (u8)
				link->resource[1]->start & 0xff);
	if (err)
	    goto config_error;

	err = pcmcia_write_config_byte(link, CISREG_IOBASE_1,
				(link->resource[1]->start >> 8) & 0xff);
	if (err)
	    goto config_error;

	/* There is no config entry for the Ethernet part which
	 * is at 0x0800. So we allocate a window into the attribute
	 * memory and write direct to the CIS registers
	 */
	link->resource[2]->flags = WIN_DATA_WIDTH_8 | WIN_MEMORY_TYPE_AM |
					WIN_ENABLE;
	link->resource[2]->start = link->resource[2]->end = 0;
	if ((err = pcmcia_request_window(link, link->resource[2], 0)))
	    goto config_error;

	local->dingo_ccr = ioremap(link->resource[2]->start, 0x1000) + 0x0800;
	if ((err = pcmcia_map_mem_page(link, link->resource[2], 0)))
	    goto config_error;

	/* Setup the CCRs; there are no infos in the CIS about the Ethernet
	 * part.
	 */
	writeb(0x47, local->dingo_ccr + CISREG_COR);
	ioaddr = link->resource[0]->start;
	writeb(ioaddr & 0xff	  , local->dingo_ccr + CISREG_IOBASE_0);
	writeb((ioaddr >> 8)&0xff , local->dingo_ccr + CISREG_IOBASE_1);

      #if 0
	{
	    u_char tmp;
	    pr_info("ECOR:");
	    for (i=0; i < 7; i++) {
		tmp = readb(local->dingo_ccr + i*2);
		pr_cont(" %02x", tmp);
	    }
	    pr_cont("\n");
	    pr_info("DCOR:");
	    for (i=0; i < 4; i++) {
		tmp = readb(local->dingo_ccr + 0x20 + i*2);
		pr_cont(" %02x", tmp);
	    }
	    pr_cont("\n");
	    pr_info("SCOR:");
	    for (i=0; i < 10; i++) {
		tmp = readb(local->dingo_ccr + 0x40 + i*2);
		pr_cont(" %02x", tmp);
	    }
	    pr_cont("\n");
	}
      #endif

	writeb(0x01, local->dingo_ccr + 0x20);
	writeb(0x0c, local->dingo_ccr + 0x22);
	writeb(0x00, local->dingo_ccr + 0x24);
	writeb(0x00, local->dingo_ccr + 0x26);
	writeb(0x00, local->dingo_ccr + 0x28);
    }

    /* The if_port symbol can be set when the module is loaded */
    local->probe_port=0;
    if (!if_port) {
	local->probe_port = dev->if_port = 1;
    } else if ((if_port >= 1 && if_port <= 2) ||
	       (local->mohawk && if_port==4))
	dev->if_port = if_port;
    else
	pr_notice("invalid if_port requested\n");

    /* we can now register the device with the net subsystem */
    dev->irq = link->irq;
    dev->base_addr = link->resource[0]->start;

    if (local->dingo)
	do_reset(dev, 1); /* a kludge to make the cem56 work */

    SET_NETDEV_DEV(dev, &link->dev);

    if ((err=register_netdev(dev))) {
	pr_notice("register_netdev() failed\n");
	goto config_error;
    }

    /* give some infos about the hardware */
    netdev_info(dev, "%s: port %#3lx, irq %d, hwaddr %pM\n",
		local->manf_str, (u_long)dev->base_addr, (int)dev->irq,
		dev->dev_addr);

    return 0;

  config_error:
    xirc2ps_release(link);
    return -ENODEV;

  failure:
    return -ENODEV;
} /* xirc2ps_config */

static void
xirc2ps_release(struct pcmcia_device *link)
{
	dev_dbg(&link->dev, "release\n");

	if (link->resource[2]->end) {
		struct net_device *dev = link->priv;
		local_info_t *local = netdev_priv(dev);
		if (local->dingo)
			iounmap(local->dingo_ccr - 0x0800);
	}
	pcmcia_disable_device(link);
} /* xirc2ps_release */

/*====================================================================*/


static int xirc2ps_suspend(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open) {
		netif_device_detach(dev);
		do_powerdown(dev);
	}

	return 0;
}

static int xirc2ps_resume(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open) {
		do_reset(dev,1);
		netif_device_attach(dev);
	}

	return 0;
}


/*====================================================================*/

/****************
 * This is the Interrupt service route.
 */
static irqreturn_t
xirc2ps_interrupt(int irq, void *dev_id)
{
    struct net_device *dev = (struct net_device *)dev_id;
    local_info_t *lp = netdev_priv(dev);
    unsigned int ioaddr;
    u_char saved_page;
    unsigned bytes_rcvd;
    unsigned int_status, eth_status, rx_status, tx_status;
    unsigned rsr, pktlen;
    ulong start_ticks = jiffies; /* fixme: jiffies rollover every 497 days
				  * is this something to worry about?
				  * -- on a laptop?
				  */

    if (!netif_device_present(dev))
	return IRQ_HANDLED;

    ioaddr = dev->base_addr;
    if (lp->mohawk) { /* must disable the interrupt */
	PutByte(XIRCREG_CR, 0);
    }

    pr_debug("%s: interrupt %d at %#x.\n", dev->name, irq, ioaddr);

    saved_page = GetByte(XIRCREG_PR);
    /* Read the ISR to see whats the cause for the interrupt.
     * This also clears the interrupt flags on CE2 cards
     */
    int_status = GetByte(XIRCREG_ISR);
    bytes_rcvd = 0;
  loop_entry:
    if (int_status == 0xff) { /* card may be ejected */
	pr_debug("%s: interrupt %d for dead card\n", dev->name, irq);
	goto leave;
    }
    eth_status = GetByte(XIRCREG_ESR);

    SelectPage(0x40);
    rx_status  = GetByte(XIRCREG40_RXST0);
    PutByte(XIRCREG40_RXST0, (~rx_status & 0xff));
    tx_status = GetByte(XIRCREG40_TXST0);
    tx_status |= GetByte(XIRCREG40_TXST1) << 8;
    PutByte(XIRCREG40_TXST0, 0);
    PutByte(XIRCREG40_TXST1, 0);

    pr_debug("%s: ISR=%#2.2x ESR=%#2.2x RSR=%#2.2x TSR=%#4.4x\n",
	  dev->name, int_status, eth_status, rx_status, tx_status);

    /***** receive section ******/
    SelectPage(0);
    while (eth_status & FullPktRcvd) {
	rsr = GetByte(XIRCREG0_RSR);
	if (bytes_rcvd > maxrx_bytes && (rsr & PktRxOk)) {
	    /* too many bytes received during this int, drop the rest of the
	     * packets */
	    dev->stats.rx_dropped++;
	    pr_debug("%s: RX drop, too much done\n", dev->name);
	} else if (rsr & PktRxOk) {
	    struct sk_buff *skb;

	    pktlen = GetWord(XIRCREG0_RBC);
	    bytes_rcvd += pktlen;

	    pr_debug("rsr=%#02x packet_length=%u\n", rsr, pktlen);

	    /* 1 extra so we can use insw */
	    skb = netdev_alloc_skb(dev, pktlen + 3);
	    if (!skb) {
		dev->stats.rx_dropped++;
	    } else { /* okay get the packet */
		skb_reserve(skb, 2);
		if (lp->silicon == 0 ) { /* work around a hardware bug */
		    unsigned rhsa; /* receive start address */

		    SelectPage(5);
		    rhsa = GetWord(XIRCREG5_RHSA0);
		    SelectPage(0);
		    rhsa += 3; /* skip control infos */
		    if (rhsa >= 0x8000)
			rhsa = 0;
		    if (rhsa + pktlen > 0x8000) {
			unsigned i;
			u_char *buf = skb_put(skb, pktlen);
			for (i=0; i < pktlen ; i++, rhsa++) {
			    buf[i] = GetByte(XIRCREG_EDP);
			    if (rhsa == 0x8000) {
				rhsa = 0;
				i--;
			    }
			}
		    } else {
			insw(ioaddr+XIRCREG_EDP,
				skb_put(skb, pktlen), (pktlen+1)>>1);
		    }
		}
	      #if 0
		else if (lp->mohawk) {
		    /* To use this 32 bit access we should use
		     * a manual optimized loop
		     * Also the words are swapped, we can get more
		     * performance by using 32 bit access and swapping
		     * the words in a register. Will need this for cardbus
		     *
		     * Note: don't forget to change the ALLOC_SKB to .. +3
		     */
		    unsigned i;
		    u_long *p = skb_put(skb, pktlen);
		    register u_long a;
		    unsigned int edpreg = ioaddr+XIRCREG_EDP-2;
		    for (i=0; i < len ; i += 4, p++) {
			a = inl(edpreg);
			__asm__("rorl $16,%0\n\t"
				:"=q" (a)
				: "0" (a));
			*p = a;
		    }
		}
	      #endif
		else {
		    insw(ioaddr+XIRCREG_EDP, skb_put(skb, pktlen),
			    (pktlen+1)>>1);
		}
		skb->protocol = eth_type_trans(skb, dev);
		netif_rx(skb);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += pktlen;
		if (!(rsr & PhyPkt))
		    dev->stats.multicast++;
	    }
	} else { /* bad packet */
	    pr_debug("rsr=%#02x\n", rsr);
	}
	if (rsr & PktTooLong) {
	    dev->stats.rx_frame_errors++;
	    pr_debug("%s: Packet too long\n", dev->name);
	}
	if (rsr & CRCErr) {
	    dev->stats.rx_crc_errors++;
	    pr_debug("%s: CRC error\n", dev->name);
	}
	if (rsr & AlignErr) {
	    dev->stats.rx_fifo_errors++; /* okay ? */
	    pr_debug("%s: Alignment error\n", dev->name);
	}

	/* clear the received/dropped/error packet */
	PutWord(XIRCREG0_DO, 0x8000); /* issue cmd: skip_rx_packet */

	/* get the new ethernet status */
	eth_status = GetByte(XIRCREG_ESR);
    }
    if (rx_status & 0x10) { /* Receive overrun */
	dev->stats.rx_over_errors++;
	PutByte(XIRCREG_CR, ClearRxOvrun);
	pr_debug("receive overrun cleared\n");
    }

    /***** transmit section ******/
    if (int_status & PktTxed) {
	unsigned n, nn;

	n = lp->last_ptr_value;
	nn = GetByte(XIRCREG0_PTR);
	lp->last_ptr_value = nn;
	if (nn < n) /* rollover */
	    dev->stats.tx_packets += 256 - n;
	else if (n == nn) { /* happens sometimes - don't know why */
	    pr_debug("PTR not changed?\n");
	} else
	    dev->stats.tx_packets += lp->last_ptr_value - n;
	netif_wake_queue(dev);
    }
    if (tx_status & 0x0002) {	/* Execessive collissions */
	pr_debug("tx restarted due to execssive collissions\n");
	PutByte(XIRCREG_CR, RestartTx);  /* restart transmitter process */
    }
    if (tx_status & 0x0040)
	dev->stats.tx_aborted_errors++;

    /* recalculate our work chunk so that we limit the duration of this
     * ISR to about 1/10 of a second.
     * Calculate only if we received a reasonable amount of bytes.
     */
    if (bytes_rcvd > 1000) {
	u_long duration = jiffies - start_ticks;

	if (duration >= HZ/10) { /* if more than about 1/10 second */
	    maxrx_bytes = (bytes_rcvd * (HZ/10)) / duration;
	    if (maxrx_bytes < 2000)
		maxrx_bytes = 2000;
	    else if (maxrx_bytes > 22000)
		maxrx_bytes = 22000;
	    pr_debug("set maxrx=%u (rcvd=%u ticks=%lu)\n",
		  maxrx_bytes, bytes_rcvd, duration);
	} else if (!duration && maxrx_bytes < 22000) {
	    /* now much faster */
	    maxrx_bytes += 2000;
	    if (maxrx_bytes > 22000)
		maxrx_bytes = 22000;
	    pr_debug("set maxrx=%u\n", maxrx_bytes);
	}
    }

  leave:
    if (lockup_hack) {
	if (int_status != 0xff && (int_status = GetByte(XIRCREG_ISR)) != 0)
	    goto loop_entry;
    }
    SelectPage(saved_page);
    PutByte(XIRCREG_CR, EnableIntr);  /* re-enable interrupts */
    /* Instead of dropping packets during a receive, we could
     * force an interrupt with this command:
     *	  PutByte(XIRCREG_CR, EnableIntr|ForceIntr);
     */
    return IRQ_HANDLED;
} /* xirc2ps_interrupt */

/*====================================================================*/

static void
xirc2ps_tx_timeout_task(struct work_struct *work)
{
	local_info_t *local =
		container_of(work, local_info_t, tx_timeout_task);
	struct net_device *dev = local->dev;
    /* reset the card */
    do_reset(dev,1);
    dev->trans_start = jiffies; /* prevent tx timeout */
    netif_wake_queue(dev);
}

static void
xirc_tx_timeout(struct net_device *dev)
{
    local_info_t *lp = netdev_priv(dev);
    dev->stats.tx_errors++;
    netdev_notice(dev, "transmit timed out\n");
    schedule_work(&lp->tx_timeout_task);
}

static netdev_tx_t
do_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    local_info_t *lp = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;
    int okay;
    unsigned freespace;
    unsigned pktlen = skb->len;

    pr_debug("do_start_xmit(skb=%p, dev=%p) len=%u\n",
	  skb, dev, pktlen);


    /* adjust the packet length to min. required
     * and hope that the buffer is large enough
     * to provide some random data.
     * fixme: For Mohawk we can change this by sending
     * a larger packetlen than we actually have; the chip will
     * pad this in his buffer with random bytes
     */
    if (pktlen < ETH_ZLEN)
    {
        if (skb_padto(skb, ETH_ZLEN))
        	return NETDEV_TX_OK;
	pktlen = ETH_ZLEN;
    }

    netif_stop_queue(dev);
    SelectPage(0);
    PutWord(XIRCREG0_TRS, (u_short)pktlen+2);
    freespace = GetWord(XIRCREG0_TSO);
    okay = freespace & 0x8000;
    freespace &= 0x7fff;
    /* TRS doesn't work - (indeed it is eliminated with sil-rev 1) */
    okay = pktlen +2 < freespace;
    pr_debug("%s: avail. tx space=%u%s\n",
	  dev->name, freespace, okay ? " (okay)":" (not enough)");
    if (!okay) { /* not enough space */
	return NETDEV_TX_BUSY;  /* upper layer may decide to requeue this packet */
    }
    /* send the packet */
    PutWord(XIRCREG_EDP, (u_short)pktlen);
    outsw(ioaddr+XIRCREG_EDP, skb->data, pktlen>>1);
    if (pktlen & 1)
	PutByte(XIRCREG_EDP, skb->data[pktlen-1]);

    if (lp->mohawk)
	PutByte(XIRCREG_CR, TransmitPacket|EnableIntr);

    dev_kfree_skb (skb);
    dev->stats.tx_bytes += pktlen;
    netif_start_queue(dev);
    return NETDEV_TX_OK;
}

struct set_address_info {
	int reg_nr;
	int page_nr;
	int mohawk;
	unsigned int ioaddr;
};

static void set_address(struct set_address_info *sa_info, char *addr)
{
	unsigned int ioaddr = sa_info->ioaddr;
	int i;

	for (i = 0; i < 6; i++) {
		if (sa_info->reg_nr > 15) {
			sa_info->reg_nr = 8;
			sa_info->page_nr++;
			SelectPage(sa_info->page_nr);
		}
		if (sa_info->mohawk)
			PutByte(sa_info->reg_nr++, addr[5 - i]);
		else
			PutByte(sa_info->reg_nr++, addr[i]);
	}
}

/****************
 * Set all addresses: This first one is the individual address,
 * the next 9 addresses are taken from the multicast list and
 * the rest is filled with the individual address.
 */
static void set_addresses(struct net_device *dev)
{
	unsigned int ioaddr = dev->base_addr;
	local_info_t *lp = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	struct set_address_info sa_info;
	int i;

	/*
	 * Setup the info structure so that by first set_address call it will do
	 * SelectPage with the right page number. Hence these ones here.
	 */
	sa_info.reg_nr = 15 + 1;
	sa_info.page_nr = 0x50 - 1;
	sa_info.mohawk = lp->mohawk;
	sa_info.ioaddr = ioaddr;

	set_address(&sa_info, dev->dev_addr);
	i = 0;
	netdev_for_each_mc_addr(ha, dev) {
		if (i++ == 9)
			break;
		set_address(&sa_info, ha->addr);
	}
	while (i++ < 9)
		set_address(&sa_info, dev->dev_addr);
	SelectPage(0);
}

/****************
 * Set or clear the multicast filter for this adaptor.
 * We can filter up to 9 addresses, if more are requested we set
 * multicast promiscuous mode.
 */

static void
set_multicast_list(struct net_device *dev)
{
    unsigned int ioaddr = dev->base_addr;
    unsigned value;

    SelectPage(0x42);
    value = GetByte(XIRCREG42_SWC1) & 0xC0;

    if (dev->flags & IFF_PROMISC) { /* snoop */
	PutByte(XIRCREG42_SWC1, value | 0x06); /* set MPE and PME */
    } else if (netdev_mc_count(dev) > 9 || (dev->flags & IFF_ALLMULTI)) {
	PutByte(XIRCREG42_SWC1, value | 0x02); /* set MPE */
    } else if (!netdev_mc_empty(dev)) {
	/* the chip can filter 9 addresses perfectly */
	PutByte(XIRCREG42_SWC1, value | 0x01);
	SelectPage(0x40);
	PutByte(XIRCREG40_CMD0, Offline);
	set_addresses(dev);
	SelectPage(0x40);
	PutByte(XIRCREG40_CMD0, EnableRecv | Online);
    } else { /* standard usage */
	PutByte(XIRCREG42_SWC1, value | 0x00);
    }
    SelectPage(0);
}

static int
do_config(struct net_device *dev, struct ifmap *map)
{
    local_info_t *local = netdev_priv(dev);

    pr_debug("do_config(%p)\n", dev);
    if (map->port != 255 && map->port != dev->if_port) {
	if (map->port > 4)
	    return -EINVAL;
	if (!map->port) {
	    local->probe_port = 1;
	    dev->if_port = 1;
	} else {
	    local->probe_port = 0;
	    dev->if_port = map->port;
	}
	netdev_info(dev, "switching to %s port\n", if_names[dev->if_port]);
	do_reset(dev,1);  /* not the fine way :-) */
    }
    return 0;
}

/****************
 * Open the driver
 */
static int
do_open(struct net_device *dev)
{
    local_info_t *lp = netdev_priv(dev);
    struct pcmcia_device *link = lp->p_dev;

    dev_dbg(&link->dev, "do_open(%p)\n", dev);

    /* Check that the PCMCIA card is still here. */
    /* Physical device present signature. */
    if (!pcmcia_dev_present(link))
	return -ENODEV;

    /* okay */
    link->open++;

    netif_start_queue(dev);
    do_reset(dev,1);

    return 0;
}

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, "xirc2ps_cs", sizeof(info->driver));
	snprintf(info->bus_info, sizeof(info->bus_info), "PCMCIA 0x%lx",
		 dev->base_addr);
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
};

static int
do_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
    local_info_t *local = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;
    struct mii_ioctl_data *data = if_mii(rq);

    pr_debug("%s: ioctl(%-.6s, %#04x) %04x %04x %04x %04x\n",
	  dev->name, rq->ifr_ifrn.ifrn_name, cmd,
	  data->phy_id, data->reg_num, data->val_in, data->val_out);

    if (!local->mohawk)
	return -EOPNOTSUPP;

    switch(cmd) {
      case SIOCGMIIPHY:		/* Get the address of the PHY in use. */
	data->phy_id = 0;	/* we have only this address */
	/* fall through */
      case SIOCGMIIREG:		/* Read the specified MII register. */
	data->val_out = mii_rd(ioaddr, data->phy_id & 0x1f,
			       data->reg_num & 0x1f);
	break;
      case SIOCSMIIREG:		/* Write the specified MII register */
	mii_wr(ioaddr, data->phy_id & 0x1f, data->reg_num & 0x1f, data->val_in,
	       16);
	break;
      default:
	return -EOPNOTSUPP;
    }
    return 0;
}

static void
hardreset(struct net_device *dev)
{
    local_info_t *local = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;

    SelectPage(4);
    udelay(1);
    PutByte(XIRCREG4_GPR1, 0);	     /* clear bit 0: power down */
    msleep(40);				     /* wait 40 msec */
    if (local->mohawk)
	PutByte(XIRCREG4_GPR1, 1);	 /* set bit 0: power up */
    else
	PutByte(XIRCREG4_GPR1, 1 | 4);	 /* set bit 0: power up, bit 2: AIC */
    msleep(20);			     /* wait 20 msec */
}

static void
do_reset(struct net_device *dev, int full)
{
    local_info_t *local = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;
    unsigned value;

    pr_debug("%s: do_reset(%p,%d)\n", dev? dev->name:"eth?", dev, full);

    hardreset(dev);
    PutByte(XIRCREG_CR, SoftReset); /* set */
    msleep(20);			     /* wait 20 msec */
    PutByte(XIRCREG_CR, 0);	     /* clear */
    msleep(40);			     /* wait 40 msec */
    if (local->mohawk) {
	SelectPage(4);
	/* set pin GP1 and GP2 to output  (0x0c)
	 * set GP1 to low to power up the ML6692 (0x00)
	 * set GP2 to high to power up the 10Mhz chip  (0x02)
	 */
	PutByte(XIRCREG4_GPR0, 0x0e);
    }

    /* give the circuits some time to power up */
    msleep(500);			/* about 500ms */

    local->last_ptr_value = 0;
    local->silicon = local->mohawk ? (GetByte(XIRCREG4_BOV) & 0x70) >> 4
				   : (GetByte(XIRCREG4_BOV) & 0x30) >> 4;

    if (local->probe_port) {
	if (!local->mohawk) {
	    SelectPage(4);
	    PutByte(XIRCREG4_GPR0, 4);
	    local->probe_port = 0;
	}
    } else if (dev->if_port == 2) { /* enable 10Base2 */
	SelectPage(0x42);
	PutByte(XIRCREG42_SWC1, 0xC0);
    } else { /* enable 10BaseT */
	SelectPage(0x42);
	PutByte(XIRCREG42_SWC1, 0x80);
    }
    msleep(40);			     /* wait 40 msec to let it complete */

  #if 0
    {
	SelectPage(0);
	value = GetByte(XIRCREG_ESR);	 /* read the ESR */
	pr_debug("%s: ESR is: %#02x\n", dev->name, value);
    }
  #endif

    /* setup the ECR */
    SelectPage(1);
    PutByte(XIRCREG1_IMR0, 0xff); /* allow all ints */
    PutByte(XIRCREG1_IMR1, 1	); /* and Set TxUnderrunDetect */
    value = GetByte(XIRCREG1_ECR);
  #if 0
    if (local->mohawk)
	value |= DisableLinkPulse;
    PutByte(XIRCREG1_ECR, value);
  #endif
    pr_debug("%s: ECR is: %#02x\n", dev->name, value);

    SelectPage(0x42);
    PutByte(XIRCREG42_SWC0, 0x20); /* disable source insertion */

    if (local->silicon != 1) {
	/* set the local memory dividing line.
	 * The comments in the sample code say that this is only
	 * settable with the scipper version 2 which is revision 0.
	 * Always for CE3 cards
	 */
	SelectPage(2);
	PutWord(XIRCREG2_RBS, 0x2000);
    }

    if (full)
	set_addresses(dev);

    /* Hardware workaround:
     * The receive byte pointer after reset is off by 1 so we need
     * to move the offset pointer back to 0.
     */
    SelectPage(0);
    PutWord(XIRCREG0_DO, 0x2000); /* change offset command, off=0 */

    /* setup MAC IMRs and clear status registers */
    SelectPage(0x40);		     /* Bit 7 ... bit 0 */
    PutByte(XIRCREG40_RMASK0, 0xff); /* ROK, RAB, rsv, RO, CRC, AE, PTL, MP */
    PutByte(XIRCREG40_TMASK0, 0xff); /* TOK, TAB, SQE, LL, TU, JAB, EXC, CRS */
    PutByte(XIRCREG40_TMASK1, 0xb0); /* rsv, rsv, PTD, EXT, rsv,rsv,rsv, rsv*/
    PutByte(XIRCREG40_RXST0,  0x00); /* ROK, RAB, REN, RO, CRC, AE, PTL, MP */
    PutByte(XIRCREG40_TXST0,  0x00); /* TOK, TAB, SQE, LL, TU, JAB, EXC, CRS */
    PutByte(XIRCREG40_TXST1,  0x00); /* TEN, rsv, PTD, EXT, retry_counter:4  */

    if (full && local->mohawk && init_mii(dev)) {
	if (dev->if_port == 4 || local->dingo || local->new_mii) {
	    netdev_info(dev, "MII selected\n");
	    SelectPage(2);
	    PutByte(XIRCREG2_MSR, GetByte(XIRCREG2_MSR) | 0x08);
	    msleep(20);
	} else {
	    netdev_info(dev, "MII detected; using 10mbs\n");
	    SelectPage(0x42);
	    if (dev->if_port == 2) /* enable 10Base2 */
		PutByte(XIRCREG42_SWC1, 0xC0);
	    else  /* enable 10BaseT */
		PutByte(XIRCREG42_SWC1, 0x80);
	    msleep(40);			/* wait 40 msec to let it complete */
	}
	if (full_duplex)
	    PutByte(XIRCREG1_ECR, GetByte(XIRCREG1_ECR | FullDuplex));
    } else {  /* No MII */
	SelectPage(0);
	value = GetByte(XIRCREG_ESR);	 /* read the ESR */
	dev->if_port = (value & MediaSelect) ? 1 : 2;
    }

    /* configure the LEDs */
    SelectPage(2);
    if (dev->if_port == 1 || dev->if_port == 4) /* TP: Link and Activity */
	PutByte(XIRCREG2_LED, 0x3b);
    else			      /* Coax: Not-Collision and Activity */
	PutByte(XIRCREG2_LED, 0x3a);

    if (local->dingo)
	PutByte(0x0b, 0x04); /* 100 Mbit LED */

    /* enable receiver and put the mac online */
    if (full) {
	set_multicast_list(dev);
	SelectPage(0x40);
	PutByte(XIRCREG40_CMD0, EnableRecv | Online);
    }

    /* setup Ethernet IMR and enable interrupts */
    SelectPage(1);
    PutByte(XIRCREG1_IMR0, 0xff);
    udelay(1);
    SelectPage(0);
    PutByte(XIRCREG_CR, EnableIntr);
    if (local->modem && !local->dingo) { /* do some magic */
	if (!(GetByte(0x10) & 0x01))
	    PutByte(0x10, 0x11); /* unmask master-int bit */
    }

    if (full)
	netdev_info(dev, "media %s, silicon revision %d\n",
		    if_names[dev->if_port], local->silicon);
    /* We should switch back to page 0 to avoid a bug in revision 0
     * where regs with offset below 8 can't be read after an access
     * to the MAC registers */
    SelectPage(0);
}

/****************
 * Initialize the Media-Independent-Interface
 * Returns: True if we have a good MII
 */
static int
init_mii(struct net_device *dev)
{
    local_info_t *local = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;
    unsigned control, status, linkpartner;
    int i;

    if (if_port == 4 || if_port == 1) { /* force 100BaseT or 10BaseT */
	dev->if_port = if_port;
	local->probe_port = 0;
	return 1;
    }

    status = mii_rd(ioaddr,  0, 1);
    if ((status & 0xff00) != 0x7800)
	return 0; /* No MII */

    local->new_mii = (mii_rd(ioaddr, 0, 2) != 0xffff);
    
    if (local->probe_port)
	control = 0x1000; /* auto neg */
    else if (dev->if_port == 4)
	control = 0x2000; /* no auto neg, 100mbs mode */
    else
	control = 0x0000; /* no auto neg, 10mbs mode */
    mii_wr(ioaddr,  0, 0, control, 16);
    udelay(100);
    control = mii_rd(ioaddr, 0, 0);

    if (control & 0x0400) {
	netdev_notice(dev, "can't take PHY out of isolation mode\n");
	local->probe_port = 0;
	return 0;
    }

    if (local->probe_port) {
	/* according to the DP83840A specs the auto negotiation process
	 * may take up to 3.5 sec, so we use this also for our ML6692
	 * Fixme: Better to use a timer here!
	 */
	for (i=0; i < 35; i++) {
	    msleep(100);	 /* wait 100 msec */
	    status = mii_rd(ioaddr,  0, 1);
	    if ((status & 0x0020) && (status & 0x0004))
		break;
	}

	if (!(status & 0x0020)) {
	    netdev_info(dev, "autonegotiation failed; using 10mbs\n");
	    if (!local->new_mii) {
		control = 0x0000;
		mii_wr(ioaddr,  0, 0, control, 16);
		udelay(100);
		SelectPage(0);
		dev->if_port = (GetByte(XIRCREG_ESR) & MediaSelect) ? 1 : 2;
	    }
	} else {
	    linkpartner = mii_rd(ioaddr, 0, 5);
	    netdev_info(dev, "MII link partner: %04x\n", linkpartner);
	    if (linkpartner & 0x0080) {
		dev->if_port = 4;
	    } else
		dev->if_port = 1;
	}
    }

    return 1;
}

static void
do_powerdown(struct net_device *dev)
{

    unsigned int ioaddr = dev->base_addr;

    pr_debug("do_powerdown(%p)\n", dev);

    SelectPage(4);
    PutByte(XIRCREG4_GPR1, 0);	     /* clear bit 0: power down */
    SelectPage(0);
}

static int
do_stop(struct net_device *dev)
{
    unsigned int ioaddr = dev->base_addr;
    local_info_t *lp = netdev_priv(dev);
    struct pcmcia_device *link = lp->p_dev;

    dev_dbg(&link->dev, "do_stop(%p)\n", dev);

    if (!link)
	return -ENODEV;

    netif_stop_queue(dev);

    SelectPage(0);
    PutByte(XIRCREG_CR, 0);  /* disable interrupts */
    SelectPage(0x01);
    PutByte(XIRCREG1_IMR0, 0x00); /* forbid all ints */
    SelectPage(4);
    PutByte(XIRCREG4_GPR1, 0);	/* clear bit 0: power down */
    SelectPage(0);

    link->open--;
    return 0;
}

static const struct pcmcia_device_id xirc2ps_ids[] = {
	PCMCIA_PFC_DEVICE_MANF_CARD(0, 0x0089, 0x110a),
	PCMCIA_PFC_DEVICE_MANF_CARD(0, 0x0138, 0x110a),
	PCMCIA_PFC_DEVICE_PROD_ID13(0, "Xircom", "CEM28", 0x2e3ee845, 0x0ea978ea),
	PCMCIA_PFC_DEVICE_PROD_ID13(0, "Xircom", "CEM33", 0x2e3ee845, 0x80609023),
	PCMCIA_PFC_DEVICE_PROD_ID13(0, "Xircom", "CEM56", 0x2e3ee845, 0xa650c32a),
	PCMCIA_PFC_DEVICE_PROD_ID13(0, "Xircom", "REM10", 0x2e3ee845, 0x76df1d29),
	PCMCIA_PFC_DEVICE_PROD_ID13(0, "Xircom", "XEM5600", 0x2e3ee845, 0xf1403719),
	PCMCIA_PFC_DEVICE_PROD_ID12(0, "Xircom", "CreditCard Ethernet+Modem II", 0x2e3ee845, 0xeca401bf),
	PCMCIA_DEVICE_MANF_CARD(0x01bf, 0x010a),
	PCMCIA_DEVICE_PROD_ID13("Toshiba Information Systems", "TPCENET", 0x1b3b94fe, 0xf381c1a2),
	PCMCIA_DEVICE_PROD_ID13("Xircom", "CE3-10/100", 0x2e3ee845, 0x0ec0ac37),
	PCMCIA_DEVICE_PROD_ID13("Xircom", "PS-CE2-10", 0x2e3ee845, 0x947d9073),
	PCMCIA_DEVICE_PROD_ID13("Xircom", "R2E-100BTX", 0x2e3ee845, 0x2464a6e3),
	PCMCIA_DEVICE_PROD_ID13("Xircom", "RE-10", 0x2e3ee845, 0x3e08d609),
	PCMCIA_DEVICE_PROD_ID13("Xircom", "XE2000", 0x2e3ee845, 0xf7188e46),
	PCMCIA_DEVICE_PROD_ID12("Compaq", "Ethernet LAN Card", 0x54f7c49c, 0x9fd2f0a2),
	PCMCIA_DEVICE_PROD_ID12("Compaq", "Netelligent 10/100 PC Card", 0x54f7c49c, 0xefe96769),
	PCMCIA_DEVICE_PROD_ID12("Intel", "EtherExpress(TM) PRO/100 PC Card Mobile Adapter16", 0x816cc815, 0x174397db),
	PCMCIA_DEVICE_PROD_ID12("Toshiba", "10/100 Ethernet PC Card", 0x44a09d9c, 0xb44deecf),
	/* also matches CFE-10 cards! */
	/* PCMCIA_DEVICE_MANF_CARD(0x0105, 0x010a), */
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, xirc2ps_ids);


static struct pcmcia_driver xirc2ps_cs_driver = {
	.owner		= THIS_MODULE,
	.name		= "xirc2ps_cs",
	.probe		= xirc2ps_probe,
	.remove		= xirc2ps_detach,
	.id_table       = xirc2ps_ids,
	.suspend	= xirc2ps_suspend,
	.resume		= xirc2ps_resume,
};
module_pcmcia_driver(xirc2ps_cs_driver);

#ifndef MODULE
static int __init setup_xirc2ps_cs(char *str)
{
	/* if_port, full_duplex, do_sound, lockup_hack
	 */
	int ints[10] = { -1 };

	str = get_options(str, 9, ints);

#define MAYBE_SET(X,Y) if (ints[0] >= Y && ints[Y] != -1) { X = ints[Y]; }
	MAYBE_SET(if_port, 3);
	MAYBE_SET(full_duplex, 4);
	MAYBE_SET(do_sound, 5);
	MAYBE_SET(lockup_hack, 6);
#undef  MAYBE_SET

	return 1;
}

__setup("xirc2ps_cs=", setup_xirc2ps_cs);
#endif

/*
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Original driver code supplied by Multi-Tech
 *
 *	Changes
 *	1/9/98	alan@lxorguk.ukuu.org.uk
 *					Merge to 2.0.x kernel tree
 *					Obtain and use official major/minors
 *					Loader switched to a misc device
 *					(fixed range check bug as a side effect)
 *					Printk clean up
 *	9/12/98	alan@lxorguk.ukuu.org.uk
 *					Rough port to 2.1.x
 *
 *	10/6/99 sameer			Merged the ISA and PCI drivers to
 *					a new unified driver.
 *
 *	3/9/99	sameer			Added support for ISI4616 cards.
 *
 *	16/9/99	sameer			We do not force RTS low anymore.
 *					This is to prevent the firmware
 *					from getting confused.
 *
 *	26/10/99 sameer			Cosmetic changes:The driver now
 *					dumps the Port Count information
 *					along with I/O address and IRQ.
 *
 *	13/12/99 sameer			Fixed the problem with IRQ sharing.
 *
 *	10/5/00  sameer			Fixed isicom_shutdown_board()
 *					to not lower DTR on all the ports
 *					when the last port on the card is
 *					closed.
 *
 *	10/5/00  sameer			Signal mask setup command added
 *					to  isicom_setup_port and
 *					isicom_shutdown_port.
 *
 *	24/5/00  sameer			The driver is now SMP aware.
 *
 *
 *	27/11/00 Vinayak P Risbud	Fixed the Driver Crash Problem
 *
 *
 *	03/01/01  anil .s		Added support for resetting the
 *					internal modems on ISI cards.
 *
 *	08/02/01  anil .s		Upgraded the driver for kernel
 *					2.4.x
 *
 *	11/04/01  Kevin			Fixed firmware load problem with
 *					ISIHP-4X card
 *
 *	30/04/01  anil .s		Fixed the remote login through
 *					ISI port problem. Now the link
 *					does not go down before password
 *					prompt.
 *
 *	03/05/01  anil .s		Fixed the problem with IRQ sharing
 *					among ISI-PCI cards.
 *
 *	03/05/01  anil .s		Added support to display the version
 *					info during insmod as well as module
 *					listing by lsmod.
 *
 *	10/05/01  anil .s		Done the modifications to the source
 *					file and Install script so that the
 *					same installation can be used for
 *					2.2.x and 2.4.x kernel.
 *
 *	06/06/01  anil .s		Now we drop both dtr and rts during
 *					shutdown_port as well as raise them
 *					during isicom_config_port.
 *
 *	09/06/01 acme@conectiva.com.br	use capable, not suser, do
 *					restore_flags on failure in
 *					isicom_send_break, verify put_user
 *					result
 *
 *	11/02/03  ranjeeth		Added support for 230 Kbps and 460 Kbps
 *					Baud index extended to 21
 *
 *	20/03/03  ranjeeth		Made to work for Linux Advanced server.
 *					Taken care of license warning.
 *
 *	10/12/03  Ravindra		Made to work for Fedora Core 1 of
 *					Red Hat Distribution
 *
 *	06/01/05  Alan Cox 		Merged the ISI and base kernel strands
 *					into a single 2.6 driver
 *
 *	***********************************************************
 *
 *	To use this driver you also need the support package. You
 *	can find this in RPM format on
 *		ftp://ftp.linux.org.uk/pub/linux/alan
 *
 *	You can find the original tools for this direct from Multitech
 *		ftp://ftp.multitech.com/ISI-Cards/
 *
 *	Having installed the cards the module options (/etc/modprobe.d/)
 *
 *	options isicom   io=card1,card2,card3,card4 irq=card1,card2,card3,card4
 *
 *	Omit those entries for boards you don't have installed.
 *
 *	TODO
 *		Merge testing
 *		64-bit verification
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/termios.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/serial.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/slab.h>

#include <linux/uaccess.h>
#include <linux/io.h>

#include <linux/pci.h>

#include <linux/isicom.h>

#define InterruptTheCard(base) outw(0, (base) + 0xc)
#define ClearInterrupt(base) inw((base) + 0x0a)

#ifdef DEBUG
#define isicom_paranoia_check(a, b, c) __isicom_paranoia_check((a), (b), (c))
#else
#define isicom_paranoia_check(a, b, c) 0
#endif

static int isicom_probe(struct pci_dev *, const struct pci_device_id *);
static void __devexit isicom_remove(struct pci_dev *);

static struct pci_device_id isicom_pci_tbl[] = {
	{ PCI_DEVICE(VENDOR_ID, 0x2028) },
	{ PCI_DEVICE(VENDOR_ID, 0x2051) },
	{ PCI_DEVICE(VENDOR_ID, 0x2052) },
	{ PCI_DEVICE(VENDOR_ID, 0x2053) },
	{ PCI_DEVICE(VENDOR_ID, 0x2054) },
	{ PCI_DEVICE(VENDOR_ID, 0x2055) },
	{ PCI_DEVICE(VENDOR_ID, 0x2056) },
	{ PCI_DEVICE(VENDOR_ID, 0x2057) },
	{ PCI_DEVICE(VENDOR_ID, 0x2058) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, isicom_pci_tbl);

static struct pci_driver isicom_driver = {
	.name		= "isicom",
	.id_table	= isicom_pci_tbl,
	.probe		= isicom_probe,
	.remove		= isicom_remove
};

static int prev_card = 3;	/*	start servicing isi_card[0]	*/
static struct tty_driver *isicom_normal;

static void isicom_tx(unsigned long _data);
static void isicom_start(struct tty_struct *tty);

static DEFINE_TIMER(tx, isicom_tx, 0, 0);

/*   baud index mappings from linux defns to isi */

static signed char linuxb_to_isib[] = {
	-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 13, 15, 16, 17, 18, 19, 20, 21
};

struct	isi_board {
	unsigned long		base;
	int			irq;
	unsigned char		port_count;
	unsigned short		status;
	unsigned short		port_status; /* each bit for each port */
	unsigned short		shift_count;
	struct isi_port		*ports;
	signed char		count;
	spinlock_t		card_lock; /* Card wide lock 11/5/00 -sameer */
	unsigned long		flags;
	unsigned int		index;
};

struct	isi_port {
	unsigned short		magic;
	struct tty_port		port;
	u16			channel;
	u16			status;
	struct isi_board	*card;
	unsigned char		*xmit_buf;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
};

static struct isi_board isi_card[BOARD_COUNT];
static struct isi_port  isi_ports[PORT_COUNT];

/*
 *	Locking functions for card level locking. We need to own both
 *	the kernel lock for the card and have the card in a position that
 *	it wants to talk.
 */

static inline int WaitTillCardIsFree(unsigned long base)
{
	unsigned int count = 0;
	unsigned int a = in_atomic(); /* do we run under spinlock? */

	while (!(inw(base + 0xe) & 0x1) && count++ < 100)
		if (a)
			mdelay(1);
		else
			msleep(1);

	return !(inw(base + 0xe) & 0x1);
}

static int lock_card(struct isi_board *card)
{
	unsigned long base = card->base;
	unsigned int retries, a;

	for (retries = 0; retries < 10; retries++) {
		spin_lock_irqsave(&card->card_lock, card->flags);
		for (a = 0; a < 10; a++) {
			if (inw(base + 0xe) & 0x1)
				return 1;
			udelay(10);
		}
		spin_unlock_irqrestore(&card->card_lock, card->flags);
		msleep(10);
	}
	pr_warning("Failed to lock Card (0x%lx)\n", card->base);

	return 0;	/* Failed to acquire the card! */
}

static void unlock_card(struct isi_board *card)
{
	spin_unlock_irqrestore(&card->card_lock, card->flags);
}

/*
 *  ISI Card specific ops ...
 */

/* card->lock HAS to be held */
static void raise_dtr(struct isi_port *port)
{
	struct isi_board *card = port->card;
	unsigned long base = card->base;
	u16 channel = port->channel;

	if (WaitTillCardIsFree(base))
		return;

	outw(0x8000 | (channel << card->shift_count) | 0x02, base);
	outw(0x0504, base);
	InterruptTheCard(base);
	port->status |= ISI_DTR;
}

/* card->lock HAS to be held */
static inline void drop_dtr(struct isi_port *port)
{
	struct isi_board *card = port->card;
	unsigned long base = card->base;
	u16 channel = port->channel;

	if (WaitTillCardIsFree(base))
		return;

	outw(0x8000 | (channel << card->shift_count) | 0x02, base);
	outw(0x0404, base);
	InterruptTheCard(base);
	port->status &= ~ISI_DTR;
}

/* card->lock HAS to be held */
static inline void raise_rts(struct isi_port *port)
{
	struct isi_board *card = port->card;
	unsigned long base = card->base;
	u16 channel = port->channel;

	if (WaitTillCardIsFree(base))
		return;

	outw(0x8000 | (channel << card->shift_count) | 0x02, base);
	outw(0x0a04, base);
	InterruptTheCard(base);
	port->status |= ISI_RTS;
}

/* card->lock HAS to be held */
static inline void drop_rts(struct isi_port *port)
{
	struct isi_board *card = port->card;
	unsigned long base = card->base;
	u16 channel = port->channel;

	if (WaitTillCardIsFree(base))
		return;

	outw(0x8000 | (channel << card->shift_count) | 0x02, base);
	outw(0x0804, base);
	InterruptTheCard(base);
	port->status &= ~ISI_RTS;
}

/* card->lock MUST NOT be held */

static void isicom_dtr_rts(struct tty_port *port, int on)
{
	struct isi_port *ip = container_of(port, struct isi_port, port);
	struct isi_board *card = ip->card;
	unsigned long base = card->base;
	u16 channel = ip->channel;

	if (!lock_card(card))
		return;

	if (on) {
		outw(0x8000 | (channel << card->shift_count) | 0x02, base);
		outw(0x0f04, base);
		InterruptTheCard(base);
		ip->status |= (ISI_DTR | ISI_RTS);
	} else {
		outw(0x8000 | (channel << card->shift_count) | 0x02, base);
		outw(0x0C04, base);
		InterruptTheCard(base);
		ip->status &= ~(ISI_DTR | ISI_RTS);
	}
	unlock_card(card);
}

/* card->lock HAS to be held */
static void drop_dtr_rts(struct isi_port *port)
{
	struct isi_board *card = port->card;
	unsigned long base = card->base;
	u16 channel = port->channel;

	if (WaitTillCardIsFree(base))
		return;

	outw(0x8000 | (channel << card->shift_count) | 0x02, base);
	outw(0x0c04, base);
	InterruptTheCard(base);
	port->status &= ~(ISI_RTS | ISI_DTR);
}

/*
 *	ISICOM Driver specific routines ...
 *
 */

static inline int __isicom_paranoia_check(struct isi_port const *port,
	char *name, const char *routine)
{
	if (!port) {
		pr_warning("Warning: bad isicom magic for dev %s in %s.\n",
			   name, routine);
		return 1;
	}
	if (port->magic != ISICOM_MAGIC) {
		pr_warning("Warning: NULL isicom port for dev %s in %s.\n",
			   name, routine);
		return 1;
	}

	return 0;
}

/*
 *	Transmitter.
 *
 *	We shovel data into the card buffers on a regular basis. The card
 *	will do the rest of the work for us.
 */

static void isicom_tx(unsigned long _data)
{
	unsigned long flags, base;
	unsigned int retries;
	short count = (BOARD_COUNT-1), card;
	short txcount, wrd, residue, word_count, cnt;
	struct isi_port *port;
	struct tty_struct *tty;

	/*	find next active board	*/
	card = (prev_card + 1) & 0x0003;
	while (count-- > 0) {
		if (isi_card[card].status & BOARD_ACTIVE)
			break;
		card = (card + 1) & 0x0003;
	}
	if (!(isi_card[card].status & BOARD_ACTIVE))
		goto sched_again;

	prev_card = card;

	count = isi_card[card].port_count;
	port = isi_card[card].ports;
	base = isi_card[card].base;

	spin_lock_irqsave(&isi_card[card].card_lock, flags);
	for (retries = 0; retries < 100; retries++) {
		if (inw(base + 0xe) & 0x1)
			break;
		udelay(2);
	}
	if (retries >= 100)
		goto unlock;

	tty = tty_port_tty_get(&port->port);
	if (tty == NULL)
		goto put_unlock;

	for (; count > 0; count--, port++) {
		/* port not active or tx disabled to force flow control */
		if (!(port->port.flags & ASYNC_INITIALIZED) ||
				!(port->status & ISI_TXOK))
			continue;

		txcount = min_t(short, TX_SIZE, port->xmit_cnt);
		if (txcount <= 0 || tty->stopped || tty->hw_stopped)
			continue;

		if (!(inw(base + 0x02) & (1 << port->channel)))
			continue;

		pr_debug("txing %d bytes, port%d.\n",
			 txcount, port->channel + 1);
		outw((port->channel << isi_card[card].shift_count) | txcount,
			base);
		residue = NO;
		wrd = 0;
		while (1) {
			cnt = min_t(int, txcount, (SERIAL_XMIT_SIZE
					- port->xmit_tail));
			if (residue == YES) {
				residue = NO;
				if (cnt > 0) {
					wrd |= (port->port.xmit_buf[port->xmit_tail]
									<< 8);
					port->xmit_tail = (port->xmit_tail + 1)
						& (SERIAL_XMIT_SIZE - 1);
					port->xmit_cnt--;
					txcount--;
					cnt--;
					outw(wrd, base);
				} else {
					outw(wrd, base);
					break;
				}
			}
			if (cnt <= 0)
				break;
			word_count = cnt >> 1;
			outsw(base, port->port.xmit_buf+port->xmit_tail, word_count);
			port->xmit_tail = (port->xmit_tail
				+ (word_count << 1)) & (SERIAL_XMIT_SIZE - 1);
			txcount -= (word_count << 1);
			port->xmit_cnt -= (word_count << 1);
			if (cnt & 0x0001) {
				residue = YES;
				wrd = port->port.xmit_buf[port->xmit_tail];
				port->xmit_tail = (port->xmit_tail + 1)
					& (SERIAL_XMIT_SIZE - 1);
				port->xmit_cnt--;
				txcount--;
			}
		}

		InterruptTheCard(base);
		if (port->xmit_cnt <= 0)
			port->status &= ~ISI_TXOK;
		if (port->xmit_cnt <= WAKEUP_CHARS)
			tty_wakeup(tty);
	}

put_unlock:
	tty_kref_put(tty);
unlock:
	spin_unlock_irqrestore(&isi_card[card].card_lock, flags);
	/*	schedule another tx for hopefully in about 10ms	*/
sched_again:
	mod_timer(&tx, jiffies + msecs_to_jiffies(10));
}

/*
 *	Main interrupt handler routine
 */

static irqreturn_t isicom_interrupt(int irq, void *dev_id)
{
	struct isi_board *card = dev_id;
	struct isi_port *port;
	struct tty_struct *tty;
	unsigned long base;
	u16 header, word_count, count, channel;
	short byte_count;
	unsigned char *rp;

	if (!card || !(card->status & FIRMWARE_LOADED))
		return IRQ_NONE;

	base = card->base;

	/* did the card interrupt us? */
	if (!(inw(base + 0x0e) & 0x02))
		return IRQ_NONE;

	spin_lock(&card->card_lock);

	/*
	 * disable any interrupts from the PCI card and lower the
	 * interrupt line
	 */
	outw(0x8000, base+0x04);
	ClearInterrupt(base);

	inw(base);		/* get the dummy word out */
	header = inw(base);
	channel = (header & 0x7800) >> card->shift_count;
	byte_count = header & 0xff;

	if (channel + 1 > card->port_count) {
		pr_warning("%s(0x%lx): %d(channel) > port_count.\n",
			   __func__, base, channel+1);
		outw(0x0000, base+0x04); /* enable interrupts */
		spin_unlock(&card->card_lock);
		return IRQ_HANDLED;
	}
	port = card->ports + channel;
	if (!(port->port.flags & ASYNC_INITIALIZED)) {
		outw(0x0000, base+0x04); /* enable interrupts */
		spin_unlock(&card->card_lock);
		return IRQ_HANDLED;
	}

	tty = tty_port_tty_get(&port->port);
	if (tty == NULL) {
		word_count = byte_count >> 1;
		while (byte_count > 1) {
			inw(base);
			byte_count -= 2;
		}
		if (byte_count & 0x01)
			inw(base);
		outw(0x0000, base+0x04); /* enable interrupts */
		spin_unlock(&card->card_lock);
		return IRQ_HANDLED;
	}

	if (header & 0x8000) {		/* Status Packet */
		header = inw(base);
		switch (header & 0xff) {
		case 0:	/* Change in EIA signals */
			if (port->port.flags & ASYNC_CHECK_CD) {
				if (port->status & ISI_DCD) {
					if (!(header & ISI_DCD)) {
					/* Carrier has been lost  */
						pr_debug("%s: DCD->low.\n",
							 __func__);
						port->status &= ~ISI_DCD;
						tty_hangup(tty);
					}
				} else if (header & ISI_DCD) {
				/* Carrier has been detected */
					pr_debug("%s: DCD->high.\n",
						__func__);
					port->status |= ISI_DCD;
					wake_up_interruptible(&port->port.open_wait);
				}
			} else {
				if (header & ISI_DCD)
					port->status |= ISI_DCD;
				else
					port->status &= ~ISI_DCD;
			}

			if (tty_port_cts_enabled(&port->port)) {
				if (tty->hw_stopped) {
					if (header & ISI_CTS) {
						tty->hw_stopped = 0;
						/* start tx ing */
						port->status |= (ISI_TXOK
							| ISI_CTS);
						tty_wakeup(tty);
					}
				} else if (!(header & ISI_CTS)) {
					tty->hw_stopped = 1;
					/* stop tx ing */
					port->status &= ~(ISI_TXOK | ISI_CTS);
				}
			} else {
				if (header & ISI_CTS)
					port->status |= ISI_CTS;
				else
					port->status &= ~ISI_CTS;
			}

			if (header & ISI_DSR)
				port->status |= ISI_DSR;
			else
				port->status &= ~ISI_DSR;

			if (header & ISI_RI)
				port->status |= ISI_RI;
			else
				port->status &= ~ISI_RI;

			break;

		case 1:	/* Received Break !!! */
			tty_insert_flip_char(tty, 0, TTY_BREAK);
			if (port->port.flags & ASYNC_SAK)
				do_SAK(tty);
			tty_flip_buffer_push(tty);
			break;

		case 2:	/* Statistics		 */
			pr_debug("%s: stats!!!\n", __func__);
			break;

		default:
			pr_debug("%s: Unknown code in status packet.\n",
				 __func__);
			break;
		}
	} else {				/* Data   Packet */

		count = tty_prepare_flip_string(tty, &rp, byte_count & ~1);
		pr_debug("%s: Can rx %d of %d bytes.\n",
			 __func__, count, byte_count);
		word_count = count >> 1;
		insw(base, rp, word_count);
		byte_count -= (word_count << 1);
		if (count & 0x0001) {
			tty_insert_flip_char(tty,  inw(base) & 0xff,
				TTY_NORMAL);
			byte_count -= 2;
		}
		if (byte_count > 0) {
			pr_debug("%s(0x%lx:%d): Flip buffer overflow! dropping bytes...\n",
				 __func__, base, channel + 1);
		/* drain out unread xtra data */
		while (byte_count > 0) {
				inw(base);
				byte_count -= 2;
			}
		}
		tty_flip_buffer_push(tty);
	}
	outw(0x0000, base+0x04); /* enable interrupts */
	spin_unlock(&card->card_lock);
	tty_kref_put(tty);

	return IRQ_HANDLED;
}

static void isicom_config_port(struct tty_struct *tty)
{
	struct isi_port *port = tty->driver_data;
	struct isi_board *card = port->card;
	unsigned long baud;
	unsigned long base = card->base;
	u16 channel_setup, channel = port->channel,
		shift_count = card->shift_count;
	unsigned char flow_ctrl;

	/* FIXME: Switch to new tty baud API */
	baud = C_BAUD(tty);
	if (baud & CBAUDEX) {
		baud &= ~CBAUDEX;

		/*  if CBAUDEX bit is on and the baud is set to either 50 or 75
		 *  then the card is programmed for 57.6Kbps or 115Kbps
		 *  respectively.
		 */

		/* 1,2,3,4 => 57.6, 115.2, 230, 460 kbps resp. */
		if (baud < 1 || baud > 4)
			tty->termios.c_cflag &= ~CBAUDEX;
		else
			baud += 15;
	}
	if (baud == 15) {

		/*  the ASYNC_SPD_HI and ASYNC_SPD_VHI options are set
		 *  by the set_serial_info ioctl ... this is done by
		 *  the 'setserial' utility.
		 */

		if ((port->port.flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
			baud++; /*  57.6 Kbps */
		if ((port->port.flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
			baud += 2; /*  115  Kbps */
		if ((port->port.flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
			baud += 3; /* 230 kbps*/
		if ((port->port.flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
			baud += 4; /* 460 kbps*/
	}
	if (linuxb_to_isib[baud] == -1) {
		/* hang up */
		drop_dtr(port);
		return;
	} else
		raise_dtr(port);

	if (WaitTillCardIsFree(base) == 0) {
		outw(0x8000 | (channel << shift_count) | 0x03, base);
		outw(linuxb_to_isib[baud] << 8 | 0x03, base);
		channel_setup = 0;
		switch (C_CSIZE(tty)) {
		case CS5:
			channel_setup |= ISICOM_CS5;
			break;
		case CS6:
			channel_setup |= ISICOM_CS6;
			break;
		case CS7:
			channel_setup |= ISICOM_CS7;
			break;
		case CS8:
			channel_setup |= ISICOM_CS8;
			break;
		}

		if (C_CSTOPB(tty))
			channel_setup |= ISICOM_2SB;
		if (C_PARENB(tty)) {
			channel_setup |= ISICOM_EVPAR;
			if (C_PARODD(tty))
				channel_setup |= ISICOM_ODPAR;
		}
		outw(channel_setup, base);
		InterruptTheCard(base);
	}
	if (C_CLOCAL(tty))
		port->port.flags &= ~ASYNC_CHECK_CD;
	else
		port->port.flags |= ASYNC_CHECK_CD;

	/* flow control settings ...*/
	flow_ctrl = 0;
	port->port.flags &= ~ASYNC_CTS_FLOW;
	if (C_CRTSCTS(tty)) {
		port->port.flags |= ASYNC_CTS_FLOW;
		flow_ctrl |= ISICOM_CTSRTS;
	}
	if (I_IXON(tty))
		flow_ctrl |= ISICOM_RESPOND_XONXOFF;
	if (I_IXOFF(tty))
		flow_ctrl |= ISICOM_INITIATE_XONXOFF;

	if (WaitTillCardIsFree(base) == 0) {
		outw(0x8000 | (channel << shift_count) | 0x04, base);
		outw(flow_ctrl << 8 | 0x05, base);
		outw((STOP_CHAR(tty)) << 8 | (START_CHAR(tty)), base);
		InterruptTheCard(base);
	}

	/*	rx enabled -> enable port for rx on the card	*/
	if (C_CREAD(tty)) {
		card->port_status |= (1 << channel);
		outw(card->port_status, base + 0x02);
	}
}

/* open et all */

static inline void isicom_setup_board(struct isi_board *bp)
{
	int channel;
	struct isi_port *port;

	bp->count++;
	if (!(bp->status & BOARD_INIT)) {
		port = bp->ports;
		for (channel = 0; channel < bp->port_count; channel++, port++)
			drop_dtr_rts(port);
	}
	bp->status |= BOARD_ACTIVE | BOARD_INIT;
}

/* Activate and thus setup board are protected from races against shutdown
   by the tty_port mutex */

static int isicom_activate(struct tty_port *tport, struct tty_struct *tty)
{
	struct isi_port *port = container_of(tport, struct isi_port, port);
	struct isi_board *card = port->card;
	unsigned long flags;

	if (tty_port_alloc_xmit_buf(tport) < 0)
		return -ENOMEM;

	spin_lock_irqsave(&card->card_lock, flags);
	isicom_setup_board(card);

	port->xmit_cnt = port->xmit_head = port->xmit_tail = 0;

	/*	discard any residual data	*/
	if (WaitTillCardIsFree(card->base) == 0) {
		outw(0x8000 | (port->channel << card->shift_count) | 0x02,
				card->base);
		outw(((ISICOM_KILLTX | ISICOM_KILLRX) << 8) | 0x06, card->base);
		InterruptTheCard(card->base);
	}
	isicom_config_port(tty);
	spin_unlock_irqrestore(&card->card_lock, flags);

	return 0;
}

static int isicom_carrier_raised(struct tty_port *port)
{
	struct isi_port *ip = container_of(port, struct isi_port, port);
	return (ip->status & ISI_DCD)?1 : 0;
}

static struct tty_port *isicom_find_port(struct tty_struct *tty)
{
	struct isi_port *port;
	struct isi_board *card;
	unsigned int board;
	int line = tty->index;

	board = BOARD(line);
	card = &isi_card[board];

	if (!(card->status & FIRMWARE_LOADED))
		return NULL;

	/*  open on a port greater than the port count for the card !!! */
	if (line > ((board * 16) + card->port_count - 1))
		return NULL;

	port = &isi_ports[line];
	if (isicom_paranoia_check(port, tty->name, "isicom_open"))
		return NULL;

	return &port->port;
}

static int isicom_open(struct tty_struct *tty, struct file *filp)
{
	struct isi_port *port;
	struct tty_port *tport;

	tport = isicom_find_port(tty);
	if (tport == NULL)
		return -ENODEV;
	port = container_of(tport, struct isi_port, port);

	tty->driver_data = port;
	return tty_port_open(tport, tty, filp);
}

/* close et all */

/* card->lock HAS to be held */
static void isicom_shutdown_port(struct isi_port *port)
{
	struct isi_board *card = port->card;

	if (--card->count < 0) {
		pr_debug("%s: bad board(0x%lx) count %d.\n",
			 __func__, card->base, card->count);
		card->count = 0;
	}
	/* last port was closed, shutdown that board too */
	if (!card->count)
		card->status &= BOARD_ACTIVE;
}

static void isicom_flush_buffer(struct tty_struct *tty)
{
	struct isi_port *port = tty->driver_data;
	struct isi_board *card = port->card;
	unsigned long flags;

	if (isicom_paranoia_check(port, tty->name, "isicom_flush_buffer"))
		return;

	spin_lock_irqsave(&card->card_lock, flags);
	port->xmit_cnt = port->xmit_head = port->xmit_tail = 0;
	spin_unlock_irqrestore(&card->card_lock, flags);

	tty_wakeup(tty);
}

static void isicom_shutdown(struct tty_port *port)
{
	struct isi_port *ip = container_of(port, struct isi_port, port);
	struct isi_board *card = ip->card;
	unsigned long flags;

	/* indicate to the card that no more data can be received
	   on this port */
	spin_lock_irqsave(&card->card_lock, flags);
	card->port_status &= ~(1 << ip->channel);
	outw(card->port_status, card->base + 0x02);
	isicom_shutdown_port(ip);
	spin_unlock_irqrestore(&card->card_lock, flags);
	tty_port_free_xmit_buf(port);
}

static void isicom_close(struct tty_struct *tty, struct file *filp)
{
	struct isi_port *ip = tty->driver_data;
	struct tty_port *port;

	if (ip == NULL)
		return;

	port = &ip->port;
	if (isicom_paranoia_check(ip, tty->name, "isicom_close"))
		return;
	tty_port_close(port, tty, filp);
}

/* write et all */
static int isicom_write(struct tty_struct *tty,	const unsigned char *buf,
	int count)
{
	struct isi_port *port = tty->driver_data;
	struct isi_board *card = port->card;
	unsigned long flags;
	int cnt, total = 0;

	if (isicom_paranoia_check(port, tty->name, "isicom_write"))
		return 0;

	spin_lock_irqsave(&card->card_lock, flags);

	while (1) {
		cnt = min_t(int, count, min(SERIAL_XMIT_SIZE - port->xmit_cnt
				- 1, SERIAL_XMIT_SIZE - port->xmit_head));
		if (cnt <= 0)
			break;

		memcpy(port->port.xmit_buf + port->xmit_head, buf, cnt);
		port->xmit_head = (port->xmit_head + cnt) & (SERIAL_XMIT_SIZE
			- 1);
		port->xmit_cnt += cnt;
		buf += cnt;
		count -= cnt;
		total += cnt;
	}
	if (port->xmit_cnt && !tty->stopped && !tty->hw_stopped)
		port->status |= ISI_TXOK;
	spin_unlock_irqrestore(&card->card_lock, flags);
	return total;
}

/* put_char et all */
static int isicom_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct isi_port *port = tty->driver_data;
	struct isi_board *card = port->card;
	unsigned long flags;

	if (isicom_paranoia_check(port, tty->name, "isicom_put_char"))
		return 0;

	spin_lock_irqsave(&card->card_lock, flags);
	if (port->xmit_cnt >= SERIAL_XMIT_SIZE - 1) {
		spin_unlock_irqrestore(&card->card_lock, flags);
		return 0;
	}

	port->port.xmit_buf[port->xmit_head++] = ch;
	port->xmit_head &= (SERIAL_XMIT_SIZE - 1);
	port->xmit_cnt++;
	spin_unlock_irqrestore(&card->card_lock, flags);
	return 1;
}

/* flush_chars et all */
static void isicom_flush_chars(struct tty_struct *tty)
{
	struct isi_port *port = tty->driver_data;

	if (isicom_paranoia_check(port, tty->name, "isicom_flush_chars"))
		return;

	if (port->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
			!port->port.xmit_buf)
		return;

	/* this tells the transmitter to consider this port for
	   data output to the card ... that's the best we can do. */
	port->status |= ISI_TXOK;
}

/* write_room et all */
static int isicom_write_room(struct tty_struct *tty)
{
	struct isi_port *port = tty->driver_data;
	int free;

	if (isicom_paranoia_check(port, tty->name, "isicom_write_room"))
		return 0;

	free = SERIAL_XMIT_SIZE - port->xmit_cnt - 1;
	if (free < 0)
		free = 0;
	return free;
}

/* chars_in_buffer et all */
static int isicom_chars_in_buffer(struct tty_struct *tty)
{
	struct isi_port *port = tty->driver_data;
	if (isicom_paranoia_check(port, tty->name, "isicom_chars_in_buffer"))
		return 0;
	return port->xmit_cnt;
}

/* ioctl et all */
static int isicom_send_break(struct tty_struct *tty, int length)
{
	struct isi_port *port = tty->driver_data;
	struct isi_board *card = port->card;
	unsigned long base = card->base;

	if (length == -1)
		return -EOPNOTSUPP;

	if (!lock_card(card))
		return -EINVAL;

	outw(0x8000 | ((port->channel) << (card->shift_count)) | 0x3, base);
	outw((length & 0xff) << 8 | 0x00, base);
	outw((length & 0xff00), base);
	InterruptTheCard(base);

	unlock_card(card);
	return 0;
}

static int isicom_tiocmget(struct tty_struct *tty)
{
	struct isi_port *port = tty->driver_data;
	/* just send the port status */
	u16 status = port->status;

	if (isicom_paranoia_check(port, tty->name, "isicom_ioctl"))
		return -ENODEV;

	return  ((status & ISI_RTS) ? TIOCM_RTS : 0) |
		((status & ISI_DTR) ? TIOCM_DTR : 0) |
		((status & ISI_DCD) ? TIOCM_CAR : 0) |
		((status & ISI_DSR) ? TIOCM_DSR : 0) |
		((status & ISI_CTS) ? TIOCM_CTS : 0) |
		((status & ISI_RI ) ? TIOCM_RI  : 0);
}

static int isicom_tiocmset(struct tty_struct *tty,
					unsigned int set, unsigned int clear)
{
	struct isi_port *port = tty->driver_data;
	unsigned long flags;

	if (isicom_paranoia_check(port, tty->name, "isicom_ioctl"))
		return -ENODEV;

	spin_lock_irqsave(&port->card->card_lock, flags);
	if (set & TIOCM_RTS)
		raise_rts(port);
	if (set & TIOCM_DTR)
		raise_dtr(port);

	if (clear & TIOCM_RTS)
		drop_rts(port);
	if (clear & TIOCM_DTR)
		drop_dtr(port);
	spin_unlock_irqrestore(&port->card->card_lock, flags);

	return 0;
}

static int isicom_set_serial_info(struct tty_struct *tty,
					struct serial_struct __user *info)
{
	struct isi_port *port = tty->driver_data;
	struct serial_struct newinfo;
	int reconfig_port;

	if (copy_from_user(&newinfo, info, sizeof(newinfo)))
		return -EFAULT;

	mutex_lock(&port->port.mutex);
	reconfig_port = ((port->port.flags & ASYNC_SPD_MASK) !=
		(newinfo.flags & ASYNC_SPD_MASK));

	if (!capable(CAP_SYS_ADMIN)) {
		if ((newinfo.close_delay != port->port.close_delay) ||
				(newinfo.closing_wait != port->port.closing_wait) ||
				((newinfo.flags & ~ASYNC_USR_MASK) !=
				(port->port.flags & ~ASYNC_USR_MASK))) {
			mutex_unlock(&port->port.mutex);
			return -EPERM;
		}
		port->port.flags = ((port->port.flags & ~ASYNC_USR_MASK) |
				(newinfo.flags & ASYNC_USR_MASK));
	} else {
		port->port.close_delay = newinfo.close_delay;
		port->port.closing_wait = newinfo.closing_wait;
		port->port.flags = ((port->port.flags & ~ASYNC_FLAGS) |
				(newinfo.flags & ASYNC_FLAGS));
	}
	if (reconfig_port) {
		unsigned long flags;
		spin_lock_irqsave(&port->card->card_lock, flags);
		isicom_config_port(tty);
		spin_unlock_irqrestore(&port->card->card_lock, flags);
	}
	mutex_unlock(&port->port.mutex);
	return 0;
}

static int isicom_get_serial_info(struct isi_port *port,
	struct serial_struct __user *info)
{
	struct serial_struct out_info;

	mutex_lock(&port->port.mutex);
	memset(&out_info, 0, sizeof(out_info));
/*	out_info.type = ? */
	out_info.line = port - isi_ports;
	out_info.port = port->card->base;
	out_info.irq = port->card->irq;
	out_info.flags = port->port.flags;
/*	out_info.baud_base = ? */
	out_info.close_delay = port->port.close_delay;
	out_info.closing_wait = port->port.closing_wait;
	mutex_unlock(&port->port.mutex);
	if (copy_to_user(info, &out_info, sizeof(out_info)))
		return -EFAULT;
	return 0;
}

static int isicom_ioctl(struct tty_struct *tty,
	unsigned int cmd, unsigned long arg)
{
	struct isi_port *port = tty->driver_data;
	void __user *argp = (void __user *)arg;

	if (isicom_paranoia_check(port, tty->name, "isicom_ioctl"))
		return -ENODEV;

	switch (cmd) {
	case TIOCGSERIAL:
		return isicom_get_serial_info(port, argp);

	case TIOCSSERIAL:
		return isicom_set_serial_info(tty, argp);

	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

/* set_termios et all */
static void isicom_set_termios(struct tty_struct *tty,
	struct ktermios *old_termios)
{
	struct isi_port *port = tty->driver_data;
	unsigned long flags;

	if (isicom_paranoia_check(port, tty->name, "isicom_set_termios"))
		return;

	if (tty->termios.c_cflag == old_termios->c_cflag &&
			tty->termios.c_iflag == old_termios->c_iflag)
		return;

	spin_lock_irqsave(&port->card->card_lock, flags);
	isicom_config_port(tty);
	spin_unlock_irqrestore(&port->card->card_lock, flags);

	if ((old_termios->c_cflag & CRTSCTS) &&
			!(tty->termios.c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		isicom_start(tty);
	}
}

/* throttle et all */
static void isicom_throttle(struct tty_struct *tty)
{
	struct isi_port *port = tty->driver_data;
	struct isi_board *card = port->card;

	if (isicom_paranoia_check(port, tty->name, "isicom_throttle"))
		return;

	/* tell the card that this port cannot handle any more data for now */
	card->port_status &= ~(1 << port->channel);
	outw(card->port_status, card->base + 0x02);
}

/* unthrottle et all */
static void isicom_unthrottle(struct tty_struct *tty)
{
	struct isi_port *port = tty->driver_data;
	struct isi_board *card = port->card;

	if (isicom_paranoia_check(port, tty->name, "isicom_unthrottle"))
		return;

	/* tell the card that this port is ready to accept more data */
	card->port_status |= (1 << port->channel);
	outw(card->port_status, card->base + 0x02);
}

/* stop et all */
static void isicom_stop(struct tty_struct *tty)
{
	struct isi_port *port = tty->driver_data;

	if (isicom_paranoia_check(port, tty->name, "isicom_stop"))
		return;

	/* this tells the transmitter not to consider this port for
	   data output to the card. */
	port->status &= ~ISI_TXOK;
}

/* start et all */
static void isicom_start(struct tty_struct *tty)
{
	struct isi_port *port = tty->driver_data;

	if (isicom_paranoia_check(port, tty->name, "isicom_start"))
		return;

	/* this tells the transmitter to consider this port for
	   data output to the card. */
	port->status |= ISI_TXOK;
}

static void isicom_hangup(struct tty_struct *tty)
{
	struct isi_port *port = tty->driver_data;

	if (isicom_paranoia_check(port, tty->name, "isicom_hangup"))
		return;
	tty_port_hangup(&port->port);
}


/*
 * Driver init and deinit functions
 */

static const struct tty_operations isicom_ops = {
	.open			= isicom_open,
	.close			= isicom_close,
	.write			= isicom_write,
	.put_char		= isicom_put_char,
	.flush_chars		= isicom_flush_chars,
	.write_room		= isicom_write_room,
	.chars_in_buffer	= isicom_chars_in_buffer,
	.ioctl			= isicom_ioctl,
	.set_termios		= isicom_set_termios,
	.throttle		= isicom_throttle,
	.unthrottle		= isicom_unthrottle,
	.stop			= isicom_stop,
	.start			= isicom_start,
	.hangup			= isicom_hangup,
	.flush_buffer		= isicom_flush_buffer,
	.tiocmget		= isicom_tiocmget,
	.tiocmset		= isicom_tiocmset,
	.break_ctl		= isicom_send_break,
};

static const struct tty_port_operations isicom_port_ops = {
	.carrier_raised		= isicom_carrier_raised,
	.dtr_rts		= isicom_dtr_rts,
	.activate		= isicom_activate,
	.shutdown		= isicom_shutdown,
};

static int reset_card(struct pci_dev *pdev,
	const unsigned int card, unsigned int *signature)
{
	struct isi_board *board = pci_get_drvdata(pdev);
	unsigned long base = board->base;
	unsigned int sig, portcount = 0;
	int retval = 0;

	dev_dbg(&pdev->dev, "ISILoad:Resetting Card%d at 0x%lx\n", card + 1,
		base);

	inw(base + 0x8);

	msleep(10);

	outw(0, base + 0x8); /* Reset */

	msleep(1000);

	sig = inw(base + 0x4) & 0xff;

	if (sig != 0xa5 && sig != 0xbb && sig != 0xcc && sig != 0xdd &&
			sig != 0xee) {
		dev_warn(&pdev->dev, "ISILoad:Card%u reset failure (Possible "
			"bad I/O Port Address 0x%lx).\n", card + 1, base);
		dev_dbg(&pdev->dev, "Sig=0x%x\n", sig);
		retval = -EIO;
		goto end;
	}

	msleep(10);

	portcount = inw(base + 0x2);
	if (!(inw(base + 0xe) & 0x1) || (portcount != 0 && portcount != 4 &&
				portcount != 8 && portcount != 16)) {
		dev_err(&pdev->dev, "ISILoad:PCI Card%d reset failure.\n",
			card + 1);
		retval = -EIO;
		goto end;
	}

	switch (sig) {
	case 0xa5:
	case 0xbb:
	case 0xdd:
		board->port_count = (portcount == 4) ? 4 : 8;
		board->shift_count = 12;
		break;
	case 0xcc:
	case 0xee:
		board->port_count = 16;
		board->shift_count = 11;
		break;
	}
	dev_info(&pdev->dev, "-Done\n");
	*signature = sig;

end:
	return retval;
}

static int load_firmware(struct pci_dev *pdev,
	const unsigned int index, const unsigned int signature)
{
	struct isi_board *board = pci_get_drvdata(pdev);
	const struct firmware *fw;
	unsigned long base = board->base;
	unsigned int a;
	u16 word_count, status;
	int retval = -EIO;
	char *name;
	u8 *data;

	struct stframe {
		u16	addr;
		u16	count;
		u8	data[0];
	} *frame;

	switch (signature) {
	case 0xa5:
		name = "isi608.bin";
		break;
	case 0xbb:
		name = "isi608em.bin";
		break;
	case 0xcc:
		name = "isi616em.bin";
		break;
	case 0xdd:
		name = "isi4608.bin";
		break;
	case 0xee:
		name = "isi4616.bin";
		break;
	default:
		dev_err(&pdev->dev, "Unknown signature.\n");
		goto end;
	}

	retval = request_firmware(&fw, name, &pdev->dev);
	if (retval)
		goto end;

	retval = -EIO;

	for (frame = (struct stframe *)fw->data;
			frame < (struct stframe *)(fw->data + fw->size);
			frame = (struct stframe *)((u8 *)(frame + 1) +
				frame->count)) {
		if (WaitTillCardIsFree(base))
			goto errrelfw;

		outw(0xf0, base);	/* start upload sequence */
		outw(0x00, base);
		outw(frame->addr, base); /* lsb of address */

		word_count = frame->count / 2 + frame->count % 2;
		outw(word_count, base);
		InterruptTheCard(base);

		udelay(100); /* 0x2f */

		if (WaitTillCardIsFree(base))
			goto errrelfw;

		status = inw(base + 0x4);
		if (status != 0) {
			dev_warn(&pdev->dev, "Card%d rejected load header:\n"
				 "Address:0x%x\n"
				 "Count:0x%x\n"
				 "Status:0x%x\n",
				 index + 1, frame->addr, frame->count, status);
			goto errrelfw;
		}
		outsw(base, frame->data, word_count);

		InterruptTheCard(base);

		udelay(50); /* 0x0f */

		if (WaitTillCardIsFree(base))
			goto errrelfw;

		status = inw(base + 0x4);
		if (status != 0) {
			dev_err(&pdev->dev, "Card%d got out of sync.Card "
				"Status:0x%x\n", index + 1, status);
			goto errrelfw;
		}
	}

/* XXX: should we test it by reading it back and comparing with original like
 * in load firmware package? */
	for (frame = (struct stframe *)fw->data;
			frame < (struct stframe *)(fw->data + fw->size);
			frame = (struct stframe *)((u8 *)(frame + 1) +
				frame->count)) {
		if (WaitTillCardIsFree(base))
			goto errrelfw;

		outw(0xf1, base); /* start download sequence */
		outw(0x00, base);
		outw(frame->addr, base); /* lsb of address */

		word_count = (frame->count >> 1) + frame->count % 2;
		outw(word_count + 1, base);
		InterruptTheCard(base);

		udelay(50); /* 0xf */

		if (WaitTillCardIsFree(base))
			goto errrelfw;

		status = inw(base + 0x4);
		if (status != 0) {
			dev_warn(&pdev->dev, "Card%d rejected verify header:\n"
				 "Address:0x%x\n"
				 "Count:0x%x\n"
				 "Status: 0x%x\n",
				 index + 1, frame->addr, frame->count, status);
			goto errrelfw;
		}

		data = kmalloc(word_count * 2, GFP_KERNEL);
		if (data == NULL) {
			dev_err(&pdev->dev, "Card%d, firmware upload "
				"failed, not enough memory\n", index + 1);
			goto errrelfw;
		}
		inw(base);
		insw(base, data, word_count);
		InterruptTheCard(base);

		for (a = 0; a < frame->count; a++)
			if (data[a] != frame->data[a]) {
				kfree(data);
				dev_err(&pdev->dev, "Card%d, firmware upload "
					"failed\n", index + 1);
				goto errrelfw;
			}
		kfree(data);

		udelay(50); /* 0xf */

		if (WaitTillCardIsFree(base))
			goto errrelfw;

		status = inw(base + 0x4);
		if (status != 0) {
			dev_err(&pdev->dev, "Card%d verify got out of sync. "
				"Card Status:0x%x\n", index + 1, status);
			goto errrelfw;
		}
	}

	/* xfer ctrl */
	if (WaitTillCardIsFree(base))
		goto errrelfw;

	outw(0xf2, base);
	outw(0x800, base);
	outw(0x0, base);
	outw(0x0, base);
	InterruptTheCard(base);
	outw(0x0, base + 0x4); /* for ISI4608 cards */

	board->status |= FIRMWARE_LOADED;
	retval = 0;

errrelfw:
	release_firmware(fw);
end:
	return retval;
}

/*
 *	Insmod can set static symbols so keep these static
 */
static unsigned int card_count;

static int isicom_probe(struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	unsigned int uninitialized_var(signature), index;
	int retval = -EPERM;
	struct isi_board *board = NULL;

	if (card_count >= BOARD_COUNT)
		goto err;

	retval = pci_enable_device(pdev);
	if (retval) {
		dev_err(&pdev->dev, "failed to enable\n");
		goto err;
	}

	dev_info(&pdev->dev, "ISI PCI Card(Device ID 0x%x)\n", ent->device);

	/* allot the first empty slot in the array */
	for (index = 0; index < BOARD_COUNT; index++) {
		if (isi_card[index].base == 0) {
			board = &isi_card[index];
			break;
		}
	}
	if (index == BOARD_COUNT) {
		retval = -ENODEV;
		goto err_disable;
	}

	board->index = index;
	board->base = pci_resource_start(pdev, 3);
	board->irq = pdev->irq;
	card_count++;

	pci_set_drvdata(pdev, board);

	retval = pci_request_region(pdev, 3, ISICOM_NAME);
	if (retval) {
		dev_err(&pdev->dev, "I/O Region 0x%lx-0x%lx is busy. Card%d "
			"will be disabled.\n", board->base, board->base + 15,
			index + 1);
		retval = -EBUSY;
		goto errdec;
	}

	retval = request_irq(board->irq, isicom_interrupt,
			IRQF_SHARED, ISICOM_NAME, board);
	if (retval < 0) {
		dev_err(&pdev->dev, "Could not install handler at Irq %d. "
			"Card%d will be disabled.\n", board->irq, index + 1);
		goto errunrr;
	}

	retval = reset_card(pdev, index, &signature);
	if (retval < 0)
		goto errunri;

	retval = load_firmware(pdev, index, signature);
	if (retval < 0)
		goto errunri;

	for (index = 0; index < board->port_count; index++) {
		struct tty_port *tport = &board->ports[index].port;
		tty_port_init(tport);
		tport->ops = &isicom_port_ops;
		tport->close_delay = 50 * HZ/100;
		tport->closing_wait = 3000 * HZ/100;
		tty_port_register_device(tport, isicom_normal,
				board->index * 16 + index, &pdev->dev);
	}

	return 0;

errunri:
	free_irq(board->irq, board);
errunrr:
	pci_release_region(pdev, 3);
errdec:
	board->base = 0;
	card_count--;
err_disable:
	pci_disable_device(pdev);
err:
	return retval;
}

static void __devexit isicom_remove(struct pci_dev *pdev)
{
	struct isi_board *board = pci_get_drvdata(pdev);
	unsigned int i;

	for (i = 0; i < board->port_count; i++) {
		tty_unregister_device(isicom_normal, board->index * 16 + i);
		tty_port_destroy(&board->ports[i].port);
	}

	free_irq(board->irq, board);
	pci_release_region(pdev, 3);
	board->base = 0;
	card_count--;
	pci_disable_device(pdev);
}

static int __init isicom_init(void)
{
	int retval, idx, channel;
	struct isi_port *port;

	for (idx = 0; idx < BOARD_COUNT; idx++) {
		port = &isi_ports[idx * 16];
		isi_card[idx].ports = port;
		spin_lock_init(&isi_card[idx].card_lock);
		for (channel = 0; channel < 16; channel++, port++) {
			port->magic = ISICOM_MAGIC;
			port->card = &isi_card[idx];
			port->channel = channel;
			port->status = 0;
			/*  . . .  */
		}
		isi_card[idx].base = 0;
		isi_card[idx].irq = 0;
	}

	/* tty driver structure initialization */
	isicom_normal = alloc_tty_driver(PORT_COUNT);
	if (!isicom_normal) {
		retval = -ENOMEM;
		goto error;
	}

	isicom_normal->name 			= "ttyM";
	isicom_normal->major			= ISICOM_NMAJOR;
	isicom_normal->minor_start		= 0;
	isicom_normal->type			= TTY_DRIVER_TYPE_SERIAL;
	isicom_normal->subtype			= SERIAL_TYPE_NORMAL;
	isicom_normal->init_termios		= tty_std_termios;
	isicom_normal->init_termios.c_cflag	= B9600 | CS8 | CREAD | HUPCL |
		CLOCAL;
	isicom_normal->flags			= TTY_DRIVER_REAL_RAW |
		TTY_DRIVER_DYNAMIC_DEV | TTY_DRIVER_HARDWARE_BREAK;
	tty_set_operations(isicom_normal, &isicom_ops);

	retval = tty_register_driver(isicom_normal);
	if (retval) {
		pr_debug("Couldn't register the dialin driver\n");
		goto err_puttty;
	}

	retval = pci_register_driver(&isicom_driver);
	if (retval < 0) {
		pr_err("Unable to register pci driver.\n");
		goto err_unrtty;
	}

	mod_timer(&tx, jiffies + 1);

	return 0;
err_unrtty:
	tty_unregister_driver(isicom_normal);
err_puttty:
	put_tty_driver(isicom_normal);
error:
	return retval;
}

static void __exit isicom_exit(void)
{
	del_timer_sync(&tx);

	pci_unregister_driver(&isicom_driver);
	tty_unregister_driver(isicom_normal);
	put_tty_driver(isicom_normal);
}

module_init(isicom_init);
module_exit(isicom_exit);

MODULE_AUTHOR("MultiTech");
MODULE_DESCRIPTION("Driver for the ISI series of cards by MultiTech");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("isi608.bin");
MODULE_FIRMWARE("isi608em.bin");
MODULE_FIRMWARE("isi616em.bin");
MODULE_FIRMWARE("isi4608.bin");
MODULE_FIRMWARE("isi4616.bin");

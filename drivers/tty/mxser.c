// SPDX-License-Identifier: GPL-2.0+
/*
 *          mxser.c  -- MOXA Smartio/Industio family multiport serial driver.
 *
 *      Copyright (C) 1999-2006  Moxa Technologies (support@moxa.com).
 *	Copyright (C) 2006-2008  Jiri Slaby <jirislaby@gmail.com>
 *
 *      This code is loosely based on the 1.8 moxa driver which is based on
 *	Linux serial driver, written by Linus Torvalds, Theodore T'so and
 *	others.
 *
 *	Fed through a cleanup, indent and remove of non 2.6 code by Alan Cox
 *	<alan@lxorguk.ukuu.org.uk>. The original 1.8 code is available on
 *	www.moxa.com.
 *	- Fixed x86_64 cleanness
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <linux/uaccess.h>

/*
 *	Semi-public control interfaces
 */

/*
 *	MOXA ioctls
 */

#define MOXA			0x400
#define MOXA_SET_OP_MODE	(MOXA + 66)
#define MOXA_GET_OP_MODE	(MOXA + 67)

#define RS232_MODE		0
#define RS485_2WIRE_MODE	1
#define RS422_MODE		2
#define RS485_4WIRE_MODE	3
#define OP_MODE_MASK		3

/* --------------------------------------------------- */

/*
 * Follow just what Moxa Must chip defines.
 *
 * When LCR register (offset 0x03) is written the following value, the Must chip
 * will enter enhanced mode. And a write to EFR (offset 0x02) bit 6,7 will
 * change bank.
 */
#define MOXA_MUST_ENTER_ENHANCED	0xBF

/* when enhanced mode is enabled, access to general bank register */
#define MOXA_MUST_GDL_REGISTER		0x07
#define MOXA_MUST_GDL_MASK		0x7F
#define MOXA_MUST_GDL_HAS_BAD_DATA	0x80

#define MOXA_MUST_LSR_RERR		0x80	/* error in receive FIFO */
/* enhanced register bank select and enhanced mode setting register */
/* This works only when LCR register equals to 0xBF */
#define MOXA_MUST_EFR_REGISTER		0x02
#define MOXA_MUST_EFR_EFRB_ENABLE	0x10 /* enhanced mode enable */
/* enhanced register bank set 0, 1, 2 */
#define MOXA_MUST_EFR_BANK0		0x00
#define MOXA_MUST_EFR_BANK1		0x40
#define MOXA_MUST_EFR_BANK2		0x80
#define MOXA_MUST_EFR_BANK3		0xC0
#define MOXA_MUST_EFR_BANK_MASK		0xC0

/* set XON1 value register, when LCR=0xBF and change to bank0 */
#define MOXA_MUST_XON1_REGISTER		0x04

/* set XON2 value register, when LCR=0xBF and change to bank0 */
#define MOXA_MUST_XON2_REGISTER		0x05

/* set XOFF1 value register, when LCR=0xBF and change to bank0 */
#define MOXA_MUST_XOFF1_REGISTER	0x06

/* set XOFF2 value register, when LCR=0xBF and change to bank0 */
#define MOXA_MUST_XOFF2_REGISTER	0x07

#define MOXA_MUST_RBRTL_REGISTER	0x04
#define MOXA_MUST_RBRTH_REGISTER	0x05
#define MOXA_MUST_RBRTI_REGISTER	0x06
#define MOXA_MUST_THRTL_REGISTER	0x07
#define MOXA_MUST_ENUM_REGISTER		0x04
#define MOXA_MUST_HWID_REGISTER		0x05
#define MOXA_MUST_ECR_REGISTER		0x06
#define MOXA_MUST_CSR_REGISTER		0x07

#define MOXA_MUST_FCR_GDA_MODE_ENABLE	0x20 /* good data mode enable */
#define MOXA_MUST_FCR_GDA_ONLY_ENABLE	0x10 /* only good data put into RxFIFO */

#define MOXA_MUST_IER_ECTSI		0x80 /* enable CTS interrupt */
#define MOXA_MUST_IER_ERTSI		0x40 /* enable RTS interrupt */
#define MOXA_MUST_IER_XINT		0x20 /* enable Xon/Xoff interrupt */
#define MOXA_MUST_IER_EGDAI		0x10 /* enable GDA interrupt */

#define MOXA_MUST_RECV_ISR		(UART_IER_RDI | MOXA_MUST_IER_EGDAI)

/* GDA interrupt pending */
#define MOXA_MUST_IIR_GDA		0x1C
#define MOXA_MUST_IIR_RDA		0x04
#define MOXA_MUST_IIR_RTO		0x0C
#define MOXA_MUST_IIR_LSR		0x06

/* received Xon/Xoff or specical interrupt pending */
#define MOXA_MUST_IIR_XSC		0x10

/* RTS/CTS change state interrupt pending */
#define MOXA_MUST_IIR_RTSCTS		0x20
#define MOXA_MUST_IIR_MASK		0x3E

#define MOXA_MUST_MCR_XON_FLAG		0x40
#define MOXA_MUST_MCR_XON_ANY		0x80
#define MOXA_MUST_MCR_TX_XON		0x08

#define MOXA_MUST_EFR_SF_MASK		0x0F /* software flow control on chip mask value */
#define MOXA_MUST_EFR_SF_TX1		0x08 /* send Xon1/Xoff1 */
#define MOXA_MUST_EFR_SF_TX2		0x04 /* send Xon2/Xoff2 */
#define MOXA_MUST_EFR_SF_TX12		0x0C /* send Xon1,Xon2/Xoff1,Xoff2 */
#define MOXA_MUST_EFR_SF_TX_NO		0x00 /* don't send Xon/Xoff */
#define MOXA_MUST_EFR_SF_TX_MASK	0x0C /* Tx software flow control mask */
#define MOXA_MUST_EFR_SF_RX_NO		0x00 /* don't receive Xon/Xoff */
#define MOXA_MUST_EFR_SF_RX1		0x02 /* receive Xon1/Xoff1 */
#define MOXA_MUST_EFR_SF_RX2		0x01 /* receive Xon2/Xoff2 */
#define MOXA_MUST_EFR_SF_RX12		0x03 /* receive Xon1,Xon2/Xoff1,Xoff2 */
#define MOXA_MUST_EFR_SF_RX_MASK	0x03 /* Rx software flow control mask */

#define	MXSERMAJOR	 174

#define MXSER_BOARDS		4	/* Max. boards */
#define MXSER_PORTS_PER_BOARD	8	/* Max. ports per board */
#define MXSER_PORTS		(MXSER_BOARDS * MXSER_PORTS_PER_BOARD)
#define MXSER_ISR_PASS_LIMIT	100

#define WAKEUP_CHARS		256

#define MXSER_BAUD_BASE		921600
#define MXSER_CUSTOM_DIVISOR	(MXSER_BAUD_BASE * 16)

#define PCI_DEVICE_ID_POS104UL	0x1044
#define PCI_DEVICE_ID_CB108	0x1080
#define PCI_DEVICE_ID_CP102UF	0x1023
#define PCI_DEVICE_ID_CP112UL	0x1120
#define PCI_DEVICE_ID_CB114	0x1142
#define PCI_DEVICE_ID_CP114UL	0x1143
#define PCI_DEVICE_ID_CB134I	0x1341
#define PCI_DEVICE_ID_CP138U	0x1380

#define MXSER_NPORTS(ddata)		((ddata) & 0xffU)
#define MXSER_HIGHBAUD			0x0100

enum mxser_must_hwid {
	MOXA_OTHER_UART		= 0x00,
	MOXA_MUST_MU150_HWID	= 0x01,
	MOXA_MUST_MU860_HWID	= 0x02,
};

static const struct {
	u8 type;
	u8 fifo_size;
	u8 rx_high_water;
	u8 rx_low_water;
	speed_t max_baud;
} Gpci_uart_info[] = {
	{ MOXA_OTHER_UART,	 16, 14,  1, 921600 },
	{ MOXA_MUST_MU150_HWID,	 64, 48, 16, 230400 },
	{ MOXA_MUST_MU860_HWID, 128, 96, 32, 921600 }
};
#define UART_INFO_NUM	ARRAY_SIZE(Gpci_uart_info)


/* driver_data correspond to the lines in the structure above
   see also ISA probe function before you change something */
static const struct pci_device_id mxser_pcibrds[] = {
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_C168),	.driver_data = 8 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_C104),	.driver_data = 4 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP132),	.driver_data = 2 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP114),	.driver_data = 4 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CT114),	.driver_data = 4 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP102),	.driver_data = 2 | MXSER_HIGHBAUD },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP104U),	.driver_data = 4 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP168U),	.driver_data = 8 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP132U),	.driver_data = 2 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP134U),	.driver_data = 4 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP104JU),.driver_data = 4 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_RC7000),	.driver_data = 8 }, /* RC7000 */
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP118U),	.driver_data = 8 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP102UL),.driver_data = 2 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP102U),	.driver_data = 2 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP118EL),.driver_data = 8 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP168EL),.driver_data = 8 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP104EL),.driver_data = 4 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_CB108),	.driver_data = 8 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_CB114),	.driver_data = 4 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_CB134I),	.driver_data = 4 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_CP138U),	.driver_data = 8 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_POS104UL),	.driver_data = 4 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_CP114UL),	.driver_data = 4 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_CP102UF),	.driver_data = 2 },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_CP112UL),	.driver_data = 2 },
	{ }
};
MODULE_DEVICE_TABLE(pci, mxser_pcibrds);

static int ttymajor = MXSERMAJOR;

/* Variables for insmod */

MODULE_AUTHOR("Casper Yang");
MODULE_DESCRIPTION("MOXA Smartio/Industio Family Multiport Board Device Driver");
module_param(ttymajor, int, 0);
MODULE_LICENSE("GPL");

struct mxser_board;

struct mxser_port {
	struct tty_port port;
	struct mxser_board *board;

	unsigned long ioaddr;
	unsigned long opmode_ioaddr;

	u8 rx_high_water;
	u8 rx_low_water;
	int type;		/* UART type */

	unsigned char x_char;	/* xon/xoff character */
	u8 IER;			/* Interrupt Enable Register */
	u8 MCR;			/* Modem control register */

	unsigned char ldisc_stop_rx;

	struct async_icount icount; /* kernel counters for 4 input interrupts */
	unsigned int timeout;

	u8 read_status_mask;
	u8 ignore_status_mask;
	u8 xmit_fifo_size;
	unsigned int xmit_head;
	unsigned int xmit_tail;
	unsigned int xmit_cnt;

	spinlock_t slock;
};

struct mxser_board {
	unsigned int idx;
	unsigned short nports;
	int irq;
	unsigned long vector;

	enum mxser_must_hwid must_hwid;
	speed_t max_baud;

	struct mxser_port ports[];
};

static DECLARE_BITMAP(mxser_boards, MXSER_BOARDS);
static struct tty_driver *mxvar_sdriver;

static u8 __mxser_must_set_EFR(unsigned long baseio, u8 clear, u8 set,
		bool restore_LCR)
{
	u8 oldlcr, efr;

	oldlcr = inb(baseio + UART_LCR);
	outb(MOXA_MUST_ENTER_ENHANCED, baseio + UART_LCR);

	efr = inb(baseio + MOXA_MUST_EFR_REGISTER);
	efr &= ~clear;
	efr |= set;

	outb(efr, baseio + MOXA_MUST_EFR_REGISTER);

	if (restore_LCR)
		outb(oldlcr, baseio + UART_LCR);

	return oldlcr;
}

static u8 mxser_must_select_bank(unsigned long baseio, u8 bank)
{
	return __mxser_must_set_EFR(baseio, MOXA_MUST_EFR_BANK_MASK, bank,
			false);
}

static void mxser_set_must_xon1_value(unsigned long baseio, u8 value)
{
	u8 oldlcr = mxser_must_select_bank(baseio, MOXA_MUST_EFR_BANK0);
	outb(value, baseio + MOXA_MUST_XON1_REGISTER);
	outb(oldlcr, baseio + UART_LCR);
}

static void mxser_set_must_xoff1_value(unsigned long baseio, u8 value)
{
	u8 oldlcr = mxser_must_select_bank(baseio, MOXA_MUST_EFR_BANK0);
	outb(value, baseio + MOXA_MUST_XOFF1_REGISTER);
	outb(oldlcr, baseio + UART_LCR);
}

static void mxser_set_must_fifo_value(struct mxser_port *info)
{
	u8 oldlcr = mxser_must_select_bank(info->ioaddr, MOXA_MUST_EFR_BANK1);
	outb(info->rx_high_water, info->ioaddr + MOXA_MUST_RBRTH_REGISTER);
	outb(info->rx_high_water, info->ioaddr + MOXA_MUST_RBRTI_REGISTER);
	outb(info->rx_low_water, info->ioaddr + MOXA_MUST_RBRTL_REGISTER);
	outb(oldlcr, info->ioaddr + UART_LCR);
}

static void mxser_set_must_enum_value(unsigned long baseio, u8 value)
{
	u8 oldlcr = mxser_must_select_bank(baseio, MOXA_MUST_EFR_BANK2);
	outb(value, baseio + MOXA_MUST_ENUM_REGISTER);
	outb(oldlcr, baseio + UART_LCR);
}

static u8 mxser_get_must_hardware_id(unsigned long baseio)
{
	u8 oldlcr = mxser_must_select_bank(baseio, MOXA_MUST_EFR_BANK2);
	u8 id = inb(baseio + MOXA_MUST_HWID_REGISTER);
	outb(oldlcr, baseio + UART_LCR);

	return id;
}

static void mxser_must_set_EFR(unsigned long baseio, u8 clear, u8 set)
{
	__mxser_must_set_EFR(baseio, clear, set, true);
}

static void mxser_must_set_enhance_mode(unsigned long baseio, bool enable)
{
	mxser_must_set_EFR(baseio,
			enable ? 0 : MOXA_MUST_EFR_EFRB_ENABLE,
			enable ? MOXA_MUST_EFR_EFRB_ENABLE : 0);
}

static void mxser_must_no_sw_flow_control(unsigned long baseio)
{
	mxser_must_set_EFR(baseio, MOXA_MUST_EFR_SF_MASK, 0);
}

static void mxser_must_set_tx_sw_flow_control(unsigned long baseio, bool enable)
{
	mxser_must_set_EFR(baseio, MOXA_MUST_EFR_SF_TX_MASK,
			enable ? MOXA_MUST_EFR_SF_TX1 : 0);
}

static void mxser_must_set_rx_sw_flow_control(unsigned long baseio, bool enable)
{
	mxser_must_set_EFR(baseio, MOXA_MUST_EFR_SF_RX_MASK,
			enable ? MOXA_MUST_EFR_SF_RX1 : 0);
}

static enum mxser_must_hwid mxser_must_get_hwid(unsigned long io)
{
	u8 oldmcr, hwid;
	int i;

	outb(0, io + UART_LCR);
	mxser_must_set_enhance_mode(io, false);
	oldmcr = inb(io + UART_MCR);
	outb(0, io + UART_MCR);
	mxser_set_must_xon1_value(io, 0x11);
	if ((hwid = inb(io + UART_MCR)) != 0) {
		outb(oldmcr, io + UART_MCR);
		return MOXA_OTHER_UART;
	}

	hwid = mxser_get_must_hardware_id(io);
	for (i = 1; i < UART_INFO_NUM; i++) /* 0 = OTHER_UART */
		if (hwid == Gpci_uart_info[i].type)
			return hwid;

	return MOXA_OTHER_UART;
}

static bool mxser_16550A_or_MUST(struct mxser_port *info)
{
	return info->type == PORT_16550A || info->board->must_hwid;
}

static void mxser_process_txrx_fifo(struct mxser_port *info)
{
	unsigned int i;

	if (info->type == PORT_16450 || info->type == PORT_8250) {
		info->rx_high_water = 1;
		info->rx_low_water = 1;
		info->xmit_fifo_size = 1;
		return;
	}

	for (i = 0; i < UART_INFO_NUM; i++)
		if (info->board->must_hwid == Gpci_uart_info[i].type) {
			info->rx_low_water = Gpci_uart_info[i].rx_low_water;
			info->rx_high_water = Gpci_uart_info[i].rx_high_water;
			info->xmit_fifo_size = Gpci_uart_info[i].fifo_size;
			break;
		}
}

static void __mxser_start_tx(struct mxser_port *info)
{
	outb(info->IER & ~UART_IER_THRI, info->ioaddr + UART_IER);
	info->IER |= UART_IER_THRI;
	outb(info->IER, info->ioaddr + UART_IER);
}

static void mxser_start_tx(struct mxser_port *info)
{
	unsigned long flags;

	spin_lock_irqsave(&info->slock, flags);
	__mxser_start_tx(info);
	spin_unlock_irqrestore(&info->slock, flags);
}

static void __mxser_stop_tx(struct mxser_port *info)
{
	info->IER &= ~UART_IER_THRI;
	outb(info->IER, info->ioaddr + UART_IER);
}

static int mxser_carrier_raised(struct tty_port *port)
{
	struct mxser_port *mp = container_of(port, struct mxser_port, port);
	return (inb(mp->ioaddr + UART_MSR) & UART_MSR_DCD)?1:0;
}

static void mxser_dtr_rts(struct tty_port *port, int on)
{
	struct mxser_port *mp = container_of(port, struct mxser_port, port);
	unsigned long flags;
	u8 mcr;

	spin_lock_irqsave(&mp->slock, flags);
	mcr = inb(mp->ioaddr + UART_MCR);
	if (on)
		mcr |= UART_MCR_DTR | UART_MCR_RTS;
	else
		mcr &= ~(UART_MCR_DTR | UART_MCR_RTS);
	outb(mcr, mp->ioaddr + UART_MCR);
	spin_unlock_irqrestore(&mp->slock, flags);
}

static int mxser_set_baud(struct tty_struct *tty, speed_t newspd)
{
	struct mxser_port *info = tty->driver_data;
	unsigned int quot = 0, baud;
	unsigned char cval;
	u64 timeout;

	if (newspd > info->board->max_baud)
		return -1;

	if (newspd == 134) {
		quot = 2 * MXSER_BAUD_BASE / 269;
		tty_encode_baud_rate(tty, 134, 134);
	} else if (newspd) {
		quot = MXSER_BAUD_BASE / newspd;
		if (quot == 0)
			quot = 1;
		baud = MXSER_BAUD_BASE / quot;
		tty_encode_baud_rate(tty, baud, baud);
	} else {
		quot = 0;
	}

	/*
	 * worst case (128 * 1000 * 10 * 18432) needs 35 bits, so divide in the
	 * u64 domain
	 */
	timeout = (u64)info->xmit_fifo_size * HZ * 10 * quot;
	do_div(timeout, MXSER_BAUD_BASE);
	info->timeout = timeout + HZ / 50; /* Add .02 seconds of slop */

	if (quot) {
		info->MCR |= UART_MCR_DTR;
		outb(info->MCR, info->ioaddr + UART_MCR);
	} else {
		info->MCR &= ~UART_MCR_DTR;
		outb(info->MCR, info->ioaddr + UART_MCR);
		return 0;
	}

	cval = inb(info->ioaddr + UART_LCR);

	outb(cval | UART_LCR_DLAB, info->ioaddr + UART_LCR);	/* set DLAB */

	outb(quot & 0xff, info->ioaddr + UART_DLL);	/* LS of divisor */
	outb(quot >> 8, info->ioaddr + UART_DLM);	/* MS of divisor */
	outb(cval, info->ioaddr + UART_LCR);	/* reset DLAB */

#ifdef BOTHER
	if (C_BAUD(tty) == BOTHER) {
		quot = MXSER_BAUD_BASE % newspd;
		quot *= 8;
		if (quot % newspd > newspd / 2) {
			quot /= newspd;
			quot++;
		} else
			quot /= newspd;

		mxser_set_must_enum_value(info->ioaddr, quot);
	} else
#endif
		mxser_set_must_enum_value(info->ioaddr, 0);

	return 0;
}

static void mxser_handle_cts(struct tty_struct *tty, struct mxser_port *info,
		u8 msr)
{
	bool cts = msr & UART_MSR_CTS;

	if (tty->hw_stopped) {
		if (cts) {
			tty->hw_stopped = 0;

			if (!mxser_16550A_or_MUST(info))
				__mxser_start_tx(info);
			tty_wakeup(tty);
		}
		return;
	} else if (cts)
		return;

	tty->hw_stopped = 1;
	if (!mxser_16550A_or_MUST(info))
		__mxser_stop_tx(info);
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void mxser_change_speed(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;
	unsigned cflag, cval, fcr;

	cflag = tty->termios.c_cflag;

	mxser_set_baud(tty, tty_get_baud_rate(tty));

	/* byte size and parity */
	switch (cflag & CSIZE) {
	default:
	case CS5:
		cval = UART_LCR_WLEN5;
		break;
	case CS6:
		cval = UART_LCR_WLEN6;
		break;
	case CS7:
		cval = UART_LCR_WLEN7;
		break;
	case CS8:
		cval = UART_LCR_WLEN8;
		break;
	}

	if (cflag & CSTOPB)
		cval |= UART_LCR_STOP;
	if (cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;
	if (cflag & CMSPAR)
		cval |= UART_LCR_SPAR;

	if ((info->type == PORT_8250) || (info->type == PORT_16450)) {
		if (info->board->must_hwid) {
			fcr = UART_FCR_ENABLE_FIFO;
			fcr |= MOXA_MUST_FCR_GDA_MODE_ENABLE;
			mxser_set_must_fifo_value(info);
		} else
			fcr = 0;
	} else {
		fcr = UART_FCR_ENABLE_FIFO;
		if (info->board->must_hwid) {
			fcr |= MOXA_MUST_FCR_GDA_MODE_ENABLE;
			mxser_set_must_fifo_value(info);
		} else {
			switch (info->rx_high_water) {
			case 1:
				fcr |= UART_FCR_TRIGGER_1;
				break;
			case 4:
				fcr |= UART_FCR_TRIGGER_4;
				break;
			case 8:
				fcr |= UART_FCR_TRIGGER_8;
				break;
			default:
				fcr |= UART_FCR_TRIGGER_14;
				break;
			}
		}
	}

	/* CTS flow control flag and modem status interrupts */
	info->IER &= ~UART_IER_MSI;
	info->MCR &= ~UART_MCR_AFE;
	tty_port_set_cts_flow(&info->port, cflag & CRTSCTS);
	if (cflag & CRTSCTS) {
		info->IER |= UART_IER_MSI;
		if (mxser_16550A_or_MUST(info)) {
			info->MCR |= UART_MCR_AFE;
		} else {
			mxser_handle_cts(tty, info,
					inb(info->ioaddr + UART_MSR));
		}
	}
	outb(info->MCR, info->ioaddr + UART_MCR);
	tty_port_set_check_carrier(&info->port, ~cflag & CLOCAL);
	if (~cflag & CLOCAL)
		info->IER |= UART_IER_MSI;
	outb(info->IER, info->ioaddr + UART_IER);

	/*
	 * Set up parity check flag
	 */
	info->read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (I_INPCK(tty))
		info->read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (I_BRKINT(tty) || I_PARMRK(tty))
		info->read_status_mask |= UART_LSR_BI;

	info->ignore_status_mask = 0;

	if (I_IGNBRK(tty)) {
		info->ignore_status_mask |= UART_LSR_BI;
		info->read_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignore parity and break indicators, ignore
		 * overruns too.  (For real raw support).
		 */
		if (I_IGNPAR(tty)) {
			info->ignore_status_mask |=
						UART_LSR_OE |
						UART_LSR_PE |
						UART_LSR_FE;
			info->read_status_mask |=
						UART_LSR_OE |
						UART_LSR_PE |
						UART_LSR_FE;
		}
	}
	if (info->board->must_hwid) {
		mxser_set_must_xon1_value(info->ioaddr, START_CHAR(tty));
		mxser_set_must_xoff1_value(info->ioaddr, STOP_CHAR(tty));
		mxser_must_set_rx_sw_flow_control(info->ioaddr, I_IXON(tty));
		mxser_must_set_tx_sw_flow_control(info->ioaddr, I_IXOFF(tty));
	}


	outb(fcr, info->ioaddr + UART_FCR);	/* set fcr */
	outb(cval, info->ioaddr + UART_LCR);
}

static void mxser_check_modem_status(struct tty_struct *tty,
				struct mxser_port *port, int status)
{
	/* update input line counters */
	if (status & UART_MSR_TERI)
		port->icount.rng++;
	if (status & UART_MSR_DDSR)
		port->icount.dsr++;
	if (status & UART_MSR_DDCD)
		port->icount.dcd++;
	if (status & UART_MSR_DCTS)
		port->icount.cts++;
	wake_up_interruptible(&port->port.delta_msr_wait);

	if (tty_port_check_carrier(&port->port) && (status & UART_MSR_DDCD)) {
		if (status & UART_MSR_DCD)
			wake_up_interruptible(&port->port.open_wait);
	}

	if (tty_port_cts_enabled(&port->port))
		mxser_handle_cts(tty, port, status);
}

static int mxser_activate(struct tty_port *port, struct tty_struct *tty)
{
	struct mxser_port *info = container_of(port, struct mxser_port, port);
	unsigned long page;
	unsigned long flags;
	int ret;

	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	spin_lock_irqsave(&info->slock, flags);

	if (!info->type) {
		set_bit(TTY_IO_ERROR, &tty->flags);
		spin_unlock_irqrestore(&info->slock, flags);
		ret = 0;
		goto err_free_xmit;
	}
	info->port.xmit_buf = (unsigned char *) page;

	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in mxser_change_speed())
	 */
	if (info->board->must_hwid)
		outb((UART_FCR_CLEAR_RCVR |
			UART_FCR_CLEAR_XMIT |
			MOXA_MUST_FCR_GDA_MODE_ENABLE), info->ioaddr + UART_FCR);
	else
		outb((UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT),
			info->ioaddr + UART_FCR);

	/*
	 * At this point there's no way the LSR could still be 0xFF;
	 * if it is, then bail out, because there's likely no UART
	 * here.
	 */
	if (inb(info->ioaddr + UART_LSR) == 0xff) {
		spin_unlock_irqrestore(&info->slock, flags);
		if (capable(CAP_SYS_ADMIN)) {
			set_bit(TTY_IO_ERROR, &tty->flags);
			return 0;
		}

		ret = -ENODEV;
		goto err_free_xmit;
	}

	/*
	 * Clear the interrupt registers.
	 */
	(void) inb(info->ioaddr + UART_LSR);
	(void) inb(info->ioaddr + UART_RX);
	(void) inb(info->ioaddr + UART_IIR);
	(void) inb(info->ioaddr + UART_MSR);

	/*
	 * Now, initialize the UART
	 */
	outb(UART_LCR_WLEN8, info->ioaddr + UART_LCR);	/* reset DLAB */
	info->MCR = UART_MCR_DTR | UART_MCR_RTS;
	outb(info->MCR, info->ioaddr + UART_MCR);

	/*
	 * Finally, enable interrupts
	 */
	info->IER = UART_IER_MSI | UART_IER_RLSI | UART_IER_RDI;

	if (info->board->must_hwid)
		info->IER |= MOXA_MUST_IER_EGDAI;
	outb(info->IER, info->ioaddr + UART_IER);	/* enable interrupts */

	/*
	 * And clear the interrupt registers again for luck.
	 */
	(void) inb(info->ioaddr + UART_LSR);
	(void) inb(info->ioaddr + UART_RX);
	(void) inb(info->ioaddr + UART_IIR);
	(void) inb(info->ioaddr + UART_MSR);

	clear_bit(TTY_IO_ERROR, &tty->flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	/*
	 * and set the speed of the serial port
	 */
	mxser_change_speed(tty);
	spin_unlock_irqrestore(&info->slock, flags);

	return 0;
err_free_xmit:
	free_page(page);
	info->port.xmit_buf = NULL;
	return ret;
}

/*
 * This routine will shutdown a serial port
 */
static void mxser_shutdown_port(struct tty_port *port)
{
	struct mxser_port *info = container_of(port, struct mxser_port, port);
	unsigned long flags;

	spin_lock_irqsave(&info->slock, flags);

	/*
	 * clear delta_msr_wait queue to avoid mem leaks: we may free the irq
	 * here so the queue might never be waken up
	 */
	wake_up_interruptible(&info->port.delta_msr_wait);

	/*
	 * Free the xmit buffer, if necessary
	 */
	if (info->port.xmit_buf) {
		free_page((unsigned long) info->port.xmit_buf);
		info->port.xmit_buf = NULL;
	}

	info->IER = 0;
	outb(0x00, info->ioaddr + UART_IER);

	/* clear Rx/Tx FIFO's */
	if (info->board->must_hwid)
		outb(UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT |
				MOXA_MUST_FCR_GDA_MODE_ENABLE,
				info->ioaddr + UART_FCR);
	else
		outb(UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT,
			info->ioaddr + UART_FCR);

	/* read data port to reset things */
	(void) inb(info->ioaddr + UART_RX);


	if (info->board->must_hwid)
		mxser_must_no_sw_flow_control(info->ioaddr);

	spin_unlock_irqrestore(&info->slock, flags);
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its async structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
static int mxser_open(struct tty_struct *tty, struct file *filp)
{
	struct tty_port *tport = tty->port;
	struct mxser_port *port = container_of(tport, struct mxser_port, port);

	tty->driver_data = port;

	return tty_port_open(tport, tty, filp);
}

static void mxser_flush_buffer(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;
	char fcr;
	unsigned long flags;


	spin_lock_irqsave(&info->slock, flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	fcr = inb(info->ioaddr + UART_FCR);
	outb((fcr | UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT),
		info->ioaddr + UART_FCR);
	outb(fcr, info->ioaddr + UART_FCR);

	spin_unlock_irqrestore(&info->slock, flags);

	tty_wakeup(tty);
}


static void mxser_close_port(struct tty_port *port)
{
	struct mxser_port *info = container_of(port, struct mxser_port, port);
	unsigned long timeout;
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	info->IER &= ~UART_IER_RLSI;
	if (info->board->must_hwid)
		info->IER &= ~MOXA_MUST_RECV_ISR;

	outb(info->IER, info->ioaddr + UART_IER);
	/*
	 * Before we drop DTR, make sure the UART transmitter
	 * has completely drained; this is especially
	 * important if there is a transmit FIFO!
	 */
	timeout = jiffies + HZ;
	while (!(inb(info->ioaddr + UART_LSR) & UART_LSR_TEMT)) {
		schedule_timeout_interruptible(5);
		if (time_after(jiffies, timeout))
			break;
	}
}

/*
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * async structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 */
static void mxser_close(struct tty_struct *tty, struct file *filp)
{
	struct mxser_port *info = tty->driver_data;
	struct tty_port *port = &info->port;

	if (info == NULL)
		return;
	if (tty_port_close_start(port, tty, filp) == 0)
		return;
	mutex_lock(&port->mutex);
	mxser_close_port(port);
	mxser_flush_buffer(tty);
	if (tty_port_initialized(port) && C_HUPCL(tty))
		tty_port_lower_dtr_rts(port);
	mxser_shutdown_port(port);
	tty_port_set_initialized(port, 0);
	mutex_unlock(&port->mutex);
	/* Right now the tty_port set is done outside of the close_end helper
	   as we don't yet have everyone using refcounts */	
	tty_port_close_end(port, tty);
	tty_port_tty_set(port, NULL);
}

static int mxser_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	int c, total = 0;
	struct mxser_port *info = tty->driver_data;
	unsigned long flags;

	if (!info->port.xmit_buf)
		return 0;

	while (1) {
		c = min_t(int, count, min(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
					  SERIAL_XMIT_SIZE - info->xmit_head));
		if (c <= 0)
			break;

		memcpy(info->port.xmit_buf + info->xmit_head, buf, c);
		spin_lock_irqsave(&info->slock, flags);
		info->xmit_head = (info->xmit_head + c) &
				  (SERIAL_XMIT_SIZE - 1);
		info->xmit_cnt += c;
		spin_unlock_irqrestore(&info->slock, flags);

		buf += c;
		count -= c;
		total += c;
	}

	if (info->xmit_cnt && !tty->flow.stopped)
		if (!tty->hw_stopped || mxser_16550A_or_MUST(info))
			mxser_start_tx(info);

	return total;
}

static int mxser_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct mxser_port *info = tty->driver_data;
	unsigned long flags;

	if (!info->port.xmit_buf)
		return 0;

	if (info->xmit_cnt >= SERIAL_XMIT_SIZE - 1)
		return 0;

	spin_lock_irqsave(&info->slock, flags);
	info->port.xmit_buf[info->xmit_head++] = ch;
	info->xmit_head &= SERIAL_XMIT_SIZE - 1;
	info->xmit_cnt++;
	spin_unlock_irqrestore(&info->slock, flags);

	return 1;
}


static void mxser_flush_chars(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;

	if (!info->xmit_cnt || tty->flow.stopped || !info->port.xmit_buf ||
			(tty->hw_stopped && !mxser_16550A_or_MUST(info)))
		return;

	mxser_start_tx(info);
}

static unsigned int mxser_write_room(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;
	int ret;

	ret = SERIAL_XMIT_SIZE - info->xmit_cnt - 1;
	return ret < 0 ? 0 : ret;
}

static unsigned int mxser_chars_in_buffer(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;
	return info->xmit_cnt;
}

/*
 * ------------------------------------------------------------
 * friends of mxser_ioctl()
 * ------------------------------------------------------------
 */
static int mxser_get_serial_info(struct tty_struct *tty,
		struct serial_struct *ss)
{
	struct mxser_port *info = tty->driver_data;
	struct tty_port *port = &info->port;
	unsigned int closing_wait, close_delay;

	mutex_lock(&port->mutex);

	close_delay = jiffies_to_msecs(info->port.close_delay) / 10;
	closing_wait = info->port.closing_wait;
	if (closing_wait != ASYNC_CLOSING_WAIT_NONE)
		closing_wait = jiffies_to_msecs(closing_wait) / 10;

	ss->type = info->type;
	ss->line = tty->index;
	ss->port = info->ioaddr;
	ss->irq = info->board->irq;
	ss->flags = info->port.flags;
	ss->baud_base = MXSER_BAUD_BASE;
	ss->close_delay = close_delay;
	ss->closing_wait = closing_wait;
	ss->custom_divisor = MXSER_CUSTOM_DIVISOR,
	mutex_unlock(&port->mutex);
	return 0;
}

static int mxser_set_serial_info(struct tty_struct *tty,
		struct serial_struct *ss)
{
	struct mxser_port *info = tty->driver_data;
	struct tty_port *port = &info->port;
	speed_t baud;
	unsigned long sl_flags;
	unsigned int old_speed, close_delay, closing_wait;
	int retval = 0;

	if (tty_io_error(tty))
		return -EIO;

	mutex_lock(&port->mutex);

	if (ss->irq != info->board->irq ||
			ss->port != info->ioaddr) {
		mutex_unlock(&port->mutex);
		return -EINVAL;
	}

	old_speed = port->flags & ASYNC_SPD_MASK;

	close_delay = msecs_to_jiffies(ss->close_delay * 10);
	closing_wait = ss->closing_wait;
	if (closing_wait != ASYNC_CLOSING_WAIT_NONE)
		closing_wait = msecs_to_jiffies(closing_wait * 10);

	if (!capable(CAP_SYS_ADMIN)) {
		if ((ss->baud_base != MXSER_BAUD_BASE) ||
				(close_delay != port->close_delay) ||
				(closing_wait != port->closing_wait) ||
				((ss->flags & ~ASYNC_USR_MASK) != (port->flags & ~ASYNC_USR_MASK))) {
			mutex_unlock(&port->mutex);
			return -EPERM;
		}
		port->flags = (port->flags & ~ASYNC_USR_MASK) |
				(ss->flags & ASYNC_USR_MASK);
	} else {
		/*
		 * OK, past this point, all the error checking has been done.
		 * At this point, we start making changes.....
		 */
		port->flags = ((port->flags & ~ASYNC_FLAGS) |
				(ss->flags & ASYNC_FLAGS));
		port->close_delay = close_delay;
		port->closing_wait = closing_wait;
		if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST &&
				(ss->baud_base != MXSER_BAUD_BASE ||
				ss->custom_divisor !=
				MXSER_CUSTOM_DIVISOR)) {
			if (ss->custom_divisor == 0) {
				mutex_unlock(&port->mutex);
				return -EINVAL;
			}
			baud = ss->baud_base / ss->custom_divisor;
			tty_encode_baud_rate(tty, baud, baud);
		}

		info->type = ss->type;

		mxser_process_txrx_fifo(info);
	}

	if (tty_port_initialized(port)) {
		if (old_speed != (port->flags & ASYNC_SPD_MASK)) {
			spin_lock_irqsave(&info->slock, sl_flags);
			mxser_change_speed(tty);
			spin_unlock_irqrestore(&info->slock, sl_flags);
		}
	} else {
		retval = mxser_activate(port, tty);
		if (retval == 0)
			tty_port_set_initialized(port, 1);
	}
	mutex_unlock(&port->mutex);
	return retval;
}

/*
 * mxser_get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 *	    is emptied.  On bus types like RS485, the transmitter must
 *	    release the bus after transmitting. This must be done when
 *	    the transmit shift register is empty, not be done when the
 *	    transmit holding register is empty.  This functionality
 *	    allows an RS485 driver to be written in user space.
 */
static int mxser_get_lsr_info(struct mxser_port *info,
		unsigned int __user *value)
{
	unsigned char status;
	unsigned int result;
	unsigned long flags;

	spin_lock_irqsave(&info->slock, flags);
	status = inb(info->ioaddr + UART_LSR);
	spin_unlock_irqrestore(&info->slock, flags);
	result = ((status & UART_LSR_TEMT) ? TIOCSER_TEMT : 0);
	return put_user(result, value);
}

static int mxser_tiocmget(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;
	unsigned char control, status;
	unsigned long flags;

	if (tty_io_error(tty))
		return -EIO;

	spin_lock_irqsave(&info->slock, flags);
	control = info->MCR;
	status = inb(info->ioaddr + UART_MSR);
	if (status & UART_MSR_ANY_DELTA)
		mxser_check_modem_status(tty, info, status);
	spin_unlock_irqrestore(&info->slock, flags);

	return ((control & UART_MCR_RTS) ? TIOCM_RTS : 0) |
		    ((control & UART_MCR_DTR) ? TIOCM_DTR : 0) |
		    ((status & UART_MSR_DCD) ? TIOCM_CAR : 0) |
		    ((status & UART_MSR_RI) ? TIOCM_RNG : 0) |
		    ((status & UART_MSR_DSR) ? TIOCM_DSR : 0) |
		    ((status & UART_MSR_CTS) ? TIOCM_CTS : 0);
}

static int mxser_tiocmset(struct tty_struct *tty,
		unsigned int set, unsigned int clear)
{
	struct mxser_port *info = tty->driver_data;
	unsigned long flags;

	if (tty_io_error(tty))
		return -EIO;

	spin_lock_irqsave(&info->slock, flags);

	if (set & TIOCM_RTS)
		info->MCR |= UART_MCR_RTS;
	if (set & TIOCM_DTR)
		info->MCR |= UART_MCR_DTR;

	if (clear & TIOCM_RTS)
		info->MCR &= ~UART_MCR_RTS;
	if (clear & TIOCM_DTR)
		info->MCR &= ~UART_MCR_DTR;

	outb(info->MCR, info->ioaddr + UART_MCR);
	spin_unlock_irqrestore(&info->slock, flags);
	return 0;
}

static int mxser_cflags_changed(struct mxser_port *info, unsigned long arg,
		struct async_icount *cprev)
{
	struct async_icount cnow;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&info->slock, flags);
	cnow = info->icount;	/* atomic copy */
	spin_unlock_irqrestore(&info->slock, flags);

	ret =	((arg & TIOCM_RNG) && (cnow.rng != cprev->rng)) ||
		((arg & TIOCM_DSR) && (cnow.dsr != cprev->dsr)) ||
		((arg & TIOCM_CD)  && (cnow.dcd != cprev->dcd)) ||
		((arg & TIOCM_CTS) && (cnow.cts != cprev->cts));

	*cprev = cnow;

	return ret;
}

/* We should likely switch to TIOCGRS485/TIOCSRS485. */
static int mxser_ioctl_op_mode(struct mxser_port *port, int index, bool set,
		int __user *u_opmode)
{
	int opmode, p = index % 4;
	int shiftbit = p * 2;
	u8 val;

	if (port->board->must_hwid != MOXA_MUST_MU860_HWID)
		return -EFAULT;

	if (set) {
		if (get_user(opmode, u_opmode))
			return -EFAULT;

		if (opmode & ~OP_MODE_MASK)
			return -EINVAL;

		spin_lock_irq(&port->slock);
		val = inb(port->opmode_ioaddr);
		val &= ~(OP_MODE_MASK << shiftbit);
		val |= (opmode << shiftbit);
		outb(val, port->opmode_ioaddr);
		spin_unlock_irq(&port->slock);

		return 0;
	}

	spin_lock_irq(&port->slock);
	opmode = inb(port->opmode_ioaddr) >> shiftbit;
	spin_unlock_irq(&port->slock);

	return put_user(opmode & OP_MODE_MASK, u_opmode);
}

static int mxser_ioctl(struct tty_struct *tty,
		unsigned int cmd, unsigned long arg)
{
	struct mxser_port *info = tty->driver_data;
	struct async_icount cnow;
	unsigned long flags;
	void __user *argp = (void __user *)arg;

	if (cmd == MOXA_SET_OP_MODE || cmd == MOXA_GET_OP_MODE)
		return mxser_ioctl_op_mode(info, tty->index,
				cmd == MOXA_SET_OP_MODE, argp);

	if (cmd != TIOCMIWAIT && tty_io_error(tty))
		return -EIO;

	switch (cmd) {
	case TIOCSERGETLSR:	/* Get line status register */
		return  mxser_get_lsr_info(info, argp);
		/*
		 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
		 * - mask passed in arg for lines of interest
		 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
		 * Caller should use TIOCGICOUNT to see which one it was
		 */
	case TIOCMIWAIT:
		spin_lock_irqsave(&info->slock, flags);
		cnow = info->icount;	/* note the counters on entry */
		spin_unlock_irqrestore(&info->slock, flags);

		return wait_event_interruptible(info->port.delta_msr_wait,
				mxser_cflags_changed(info, arg, &cnow));
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

	/*
	 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
	 * Return: write counters to the user passed counter struct
	 * NB: both 1->0 and 0->1 transitions are counted except for
	 *     RI where only 0->1 is counted.
	 */

static int mxser_get_icount(struct tty_struct *tty,
		struct serial_icounter_struct *icount)

{
	struct mxser_port *info = tty->driver_data;
	struct async_icount cnow;
	unsigned long flags;

	spin_lock_irqsave(&info->slock, flags);
	cnow = info->icount;
	spin_unlock_irqrestore(&info->slock, flags);

	icount->frame = cnow.frame;
	icount->brk = cnow.brk;
	icount->overrun = cnow.overrun;
	icount->buf_overrun = cnow.buf_overrun;
	icount->parity = cnow.parity;
	icount->rx = cnow.rx;
	icount->tx = cnow.tx;
	icount->cts = cnow.cts;
	icount->dsr = cnow.dsr;
	icount->rng = cnow.rng;
	icount->dcd = cnow.dcd;
	return 0;
}

static void mxser_stoprx(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;

	info->ldisc_stop_rx = 1;
	if (I_IXOFF(tty)) {
		if (info->board->must_hwid) {
			info->IER &= ~MOXA_MUST_RECV_ISR;
			outb(info->IER, info->ioaddr + UART_IER);
		} else {
			info->x_char = STOP_CHAR(tty);
			outb(0, info->ioaddr + UART_IER);
			info->IER |= UART_IER_THRI;
			outb(info->IER, info->ioaddr + UART_IER);
		}
	}

	if (C_CRTSCTS(tty)) {
		info->MCR &= ~UART_MCR_RTS;
		outb(info->MCR, info->ioaddr + UART_MCR);
	}
}

/*
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 */
static void mxser_throttle(struct tty_struct *tty)
{
	mxser_stoprx(tty);
}

static void mxser_unthrottle(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;

	/* startrx */
	info->ldisc_stop_rx = 0;
	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else {
			if (info->board->must_hwid) {
				info->IER |= MOXA_MUST_RECV_ISR;
				outb(info->IER, info->ioaddr + UART_IER);
			} else {
				info->x_char = START_CHAR(tty);
				outb(0, info->ioaddr + UART_IER);
				info->IER |= UART_IER_THRI;
				outb(info->IER, info->ioaddr + UART_IER);
			}
		}
	}

	if (C_CRTSCTS(tty)) {
		info->MCR |= UART_MCR_RTS;
		outb(info->MCR, info->ioaddr + UART_MCR);
	}
}

/*
 * mxser_stop() and mxser_start()
 *
 * This routines are called before setting or resetting tty->flow.stopped.
 * They enable or disable transmitter interrupts, as necessary.
 */
static void mxser_stop(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&info->slock, flags);
	if (info->IER & UART_IER_THRI)
		__mxser_stop_tx(info);
	spin_unlock_irqrestore(&info->slock, flags);
}

static void mxser_start(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&info->slock, flags);
	if (info->xmit_cnt && info->port.xmit_buf)
		__mxser_start_tx(info);
	spin_unlock_irqrestore(&info->slock, flags);
}

static void mxser_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	struct mxser_port *info = tty->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&info->slock, flags);
	mxser_change_speed(tty);
	spin_unlock_irqrestore(&info->slock, flags);

	if ((old_termios->c_cflag & CRTSCTS) && !C_CRTSCTS(tty)) {
		tty->hw_stopped = 0;
		mxser_start(tty);
	}

	/* Handle sw stopped */
	if ((old_termios->c_iflag & IXON) && !I_IXON(tty)) {
		tty->flow.stopped = 0;

		if (info->board->must_hwid) {
			spin_lock_irqsave(&info->slock, flags);
			mxser_must_set_rx_sw_flow_control(info->ioaddr, false);
			spin_unlock_irqrestore(&info->slock, flags);
		}

		mxser_start(tty);
	}
}

/*
 * mxser_wait_until_sent() --- wait until the transmitter is empty
 */
static void mxser_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct mxser_port *info = tty->driver_data;
	unsigned long orig_jiffies, char_time;
	unsigned long flags;
	int lsr;

	if (info->type == PORT_UNKNOWN)
		return;

	if (info->xmit_fifo_size == 0)
		return;		/* Just in case.... */

	orig_jiffies = jiffies;
	/*
	 * Set the check interval to be 1/5 of the estimated time to
	 * send a single character, and make it at least 1.  The check
	 * interval should also be less than the timeout.
	 *
	 * Note: we have to use pretty tight timings here to satisfy
	 * the NIST-PCTS.
	 */
	char_time = (info->timeout - HZ / 50) / info->xmit_fifo_size;
	char_time = char_time / 5;
	if (char_time == 0)
		char_time = 1;
	if (timeout && timeout < char_time)
		char_time = timeout;
	/*
	 * If the transmitter hasn't cleared in twice the approximate
	 * amount of time to send the entire FIFO, it probably won't
	 * ever clear.  This assumes the UART isn't doing flow
	 * control, which is currently the case.  Hence, if it ever
	 * takes longer than info->timeout, this is probably due to a
	 * UART bug of some kind.  So, we clamp the timeout parameter at
	 * 2*info->timeout.
	 */
	if (!timeout || timeout > 2 * info->timeout)
		timeout = 2 * info->timeout;

	spin_lock_irqsave(&info->slock, flags);
	while (!((lsr = inb(info->ioaddr + UART_LSR)) & UART_LSR_TEMT)) {
		spin_unlock_irqrestore(&info->slock, flags);
		schedule_timeout_interruptible(char_time);
		spin_lock_irqsave(&info->slock, flags);
		if (signal_pending(current))
			break;
		if (timeout && time_after(jiffies, orig_jiffies + timeout))
			break;
	}
	spin_unlock_irqrestore(&info->slock, flags);
	set_current_state(TASK_RUNNING);
}

/*
 * This routine is called by tty_hangup() when a hangup is signaled.
 */
static void mxser_hangup(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;

	mxser_flush_buffer(tty);
	tty_port_hangup(&info->port);
}

/*
 * mxser_rs_break() --- routine which turns the break handling on or off
 */
static int mxser_rs_break(struct tty_struct *tty, int break_state)
{
	struct mxser_port *info = tty->driver_data;
	unsigned long flags;
	u8 lcr;

	spin_lock_irqsave(&info->slock, flags);
	lcr = inb(info->ioaddr + UART_LCR);
	if (break_state == -1)
		lcr |= UART_LCR_SBC;
	else
		lcr &= ~UART_LCR_SBC;
	outb(lcr, info->ioaddr + UART_LCR);
	spin_unlock_irqrestore(&info->slock, flags);

	return 0;
}

static bool mxser_receive_chars_new(struct tty_struct *tty,
		struct mxser_port *port, u8 status)
{
	enum mxser_must_hwid hwid = port->board->must_hwid;
	u8 gdl;

	if (hwid == MOXA_OTHER_UART)
		return false;
	if (status & UART_LSR_BRK_ERROR_BITS)
		return false;
	if (hwid == MOXA_MUST_MU860_HWID && (status & MOXA_MUST_LSR_RERR))
		return false;
	if (status & MOXA_MUST_LSR_RERR)
		return false;

	gdl = inb(port->ioaddr + MOXA_MUST_GDL_REGISTER);
	if (hwid == MOXA_MUST_MU150_HWID)
		gdl &= MOXA_MUST_GDL_MASK;

	if (gdl >= tty->receive_room && !port->ldisc_stop_rx)
		mxser_stoprx(tty);

	while (gdl--) {
		u8 ch = inb(port->ioaddr + UART_RX);
		tty_insert_flip_char(&port->port, ch, 0);
	}

	return true;
}

static u8 mxser_receive_chars_old(struct tty_struct *tty,
		                struct mxser_port *port, u8 status)
{
	enum mxser_must_hwid hwid = port->board->must_hwid;
	int recv_room = tty->receive_room;
	int ignored = 0;
	int max = 256;
	int cnt = 0;
	u8 ch;

	do {
		if (max-- < 0)
			break;

		ch = inb(port->ioaddr + UART_RX);
		if (hwid && (status & UART_LSR_OE))
			outb(UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR |
					MOXA_MUST_FCR_GDA_MODE_ENABLE,
					port->ioaddr + UART_FCR);
		status &= port->read_status_mask;
		if (status & port->ignore_status_mask) {
			if (++ignored > 100)
				break;
		} else {
			char flag = 0;
			if (status & UART_LSR_BRK_ERROR_BITS) {
				if (status & UART_LSR_BI) {
					flag = TTY_BREAK;
					port->icount.brk++;

					if (port->port.flags & ASYNC_SAK)
						do_SAK(tty);
				} else if (status & UART_LSR_PE) {
					flag = TTY_PARITY;
					port->icount.parity++;
				} else if (status & UART_LSR_FE) {
					flag = TTY_FRAME;
					port->icount.frame++;
				} else if (status & UART_LSR_OE) {
					flag = TTY_OVERRUN;
					port->icount.overrun++;
				}
			}
			tty_insert_flip_char(&port->port, ch, flag);
			cnt++;
			if (cnt >= recv_room) {
				if (!port->ldisc_stop_rx)
					mxser_stoprx(tty);
				break;
			}

		}

		if (hwid)
			break;

		status = inb(port->ioaddr + UART_LSR);
	} while (status & UART_LSR_DR);

	return status;
}

static u8 mxser_receive_chars(struct tty_struct *tty,
		struct mxser_port *port, u8 status)
{
	if (tty->receive_room == 0 && !port->ldisc_stop_rx)
		mxser_stoprx(tty);

	if (!mxser_receive_chars_new(tty, port, status))
		status = mxser_receive_chars_old(tty, port, status);

	tty_flip_buffer_push(&port->port);

	return status;
}

static void mxser_transmit_chars(struct tty_struct *tty, struct mxser_port *port)
{
	int count, cnt;

	if (port->x_char) {
		outb(port->x_char, port->ioaddr + UART_TX);
		port->x_char = 0;
		port->icount.tx++;
		return;
	}

	if (port->port.xmit_buf == NULL)
		return;

	if (!port->xmit_cnt || tty->flow.stopped ||
			(tty->hw_stopped && !mxser_16550A_or_MUST(port))) {
		__mxser_stop_tx(port);
		return;
	}

	cnt = port->xmit_cnt;
	count = port->xmit_fifo_size;
	do {
		outb(port->port.xmit_buf[port->xmit_tail++],
			port->ioaddr + UART_TX);
		port->xmit_tail = port->xmit_tail & (SERIAL_XMIT_SIZE - 1);
		if (!--port->xmit_cnt)
			break;
	} while (--count > 0);

	port->icount.tx += (cnt - port->xmit_cnt);

	if (port->xmit_cnt < WAKEUP_CHARS)
		tty_wakeup(tty);

	if (!port->xmit_cnt)
		__mxser_stop_tx(port);
}

static bool mxser_port_isr(struct mxser_port *port)
{
	struct tty_struct *tty;
	u8 iir, msr, status;
	bool error = false;

	iir = inb(port->ioaddr + UART_IIR);
	if (iir & UART_IIR_NO_INT)
		return true;

	iir &= MOXA_MUST_IIR_MASK;
	tty = tty_port_tty_get(&port->port);
	if (!tty) {
		status = inb(port->ioaddr + UART_LSR);
		outb(MOXA_MUST_FCR_GDA_MODE_ENABLE | UART_FCR_ENABLE_FIFO |
				UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT,
				port->ioaddr + UART_FCR);
		inb(port->ioaddr + UART_MSR);

		error = true;
		goto put_tty;
	}

	status = inb(port->ioaddr + UART_LSR);

	if (port->board->must_hwid) {
		if (iir == MOXA_MUST_IIR_GDA ||
		    iir == MOXA_MUST_IIR_RDA ||
		    iir == MOXA_MUST_IIR_RTO ||
		    iir == MOXA_MUST_IIR_LSR)
			status = mxser_receive_chars(tty, port, status);
	} else {
		status &= port->read_status_mask;
		if (status & UART_LSR_DR)
			status = mxser_receive_chars(tty, port, status);
	}

	msr = inb(port->ioaddr + UART_MSR);
	if (msr & UART_MSR_ANY_DELTA)
		mxser_check_modem_status(tty, port, msr);

	if (port->board->must_hwid) {
		if (iir == 0x02 && (status & UART_LSR_THRE))
			mxser_transmit_chars(tty, port);
	} else {
		if (status & UART_LSR_THRE)
			mxser_transmit_chars(tty, port);
	}

put_tty:
	tty_kref_put(tty);

	return error;
}

/*
 * This is the serial driver's generic interrupt routine
 */
static irqreturn_t mxser_interrupt(int irq, void *dev_id)
{
	struct mxser_board *brd = dev_id;
	struct mxser_port *port;
	unsigned int int_cnt, pass_counter = 0;
	unsigned int i, max = brd->nports;
	int handled = IRQ_NONE;
	u8 irqbits, bits, mask = BIT(max) - 1;

	while (pass_counter++ < MXSER_ISR_PASS_LIMIT) {
		irqbits = inb(brd->vector) & mask;
		if (irqbits == mask)
			break;

		handled = IRQ_HANDLED;
		for (i = 0, bits = 1; i < max; i++, irqbits |= bits, bits <<= 1) {
			if (irqbits == mask)
				break;
			if (bits & irqbits)
				continue;
			port = &brd->ports[i];

			int_cnt = 0;
			spin_lock(&port->slock);
			do {
				if (mxser_port_isr(port))
					break;
			} while (int_cnt++ < MXSER_ISR_PASS_LIMIT);
			spin_unlock(&port->slock);
		}
	}

	return handled;
}

static const struct tty_operations mxser_ops = {
	.open = mxser_open,
	.close = mxser_close,
	.write = mxser_write,
	.put_char = mxser_put_char,
	.flush_chars = mxser_flush_chars,
	.write_room = mxser_write_room,
	.chars_in_buffer = mxser_chars_in_buffer,
	.flush_buffer = mxser_flush_buffer,
	.ioctl = mxser_ioctl,
	.throttle = mxser_throttle,
	.unthrottle = mxser_unthrottle,
	.set_termios = mxser_set_termios,
	.stop = mxser_stop,
	.start = mxser_start,
	.hangup = mxser_hangup,
	.break_ctl = mxser_rs_break,
	.wait_until_sent = mxser_wait_until_sent,
	.tiocmget = mxser_tiocmget,
	.tiocmset = mxser_tiocmset,
	.set_serial = mxser_set_serial_info,
	.get_serial = mxser_get_serial_info,
	.get_icount = mxser_get_icount,
};

static const struct tty_port_operations mxser_port_ops = {
	.carrier_raised = mxser_carrier_raised,
	.dtr_rts = mxser_dtr_rts,
	.activate = mxser_activate,
	.shutdown = mxser_shutdown_port,
};

/*
 * The MOXA Smartio/Industio serial driver boot-time initialization code!
 */

static void mxser_initbrd(struct mxser_board *brd, bool high_baud)
{
	struct mxser_port *info;
	unsigned int i;
	bool is_mu860;

	brd->must_hwid = mxser_must_get_hwid(brd->ports[0].ioaddr);
	is_mu860 = brd->must_hwid == MOXA_MUST_MU860_HWID;

	for (i = 0; i < UART_INFO_NUM; i++) {
		if (Gpci_uart_info[i].type == brd->must_hwid) {
			brd->max_baud = Gpci_uart_info[i].max_baud;

			/* exception....CP-102 */
			if (high_baud)
				brd->max_baud = 921600;
			break;
		}
	}

	if (is_mu860) {
		/* set to RS232 mode by default */
		outb(0, brd->vector + 4);
		outb(0, brd->vector + 0x0c);
	}

	for (i = 0; i < brd->nports; i++) {
		info = &brd->ports[i];
		if (is_mu860) {
			if (i < 4)
				info->opmode_ioaddr = brd->vector + 4;
			else
				info->opmode_ioaddr = brd->vector + 0x0c;
		}
		tty_port_init(&info->port);
		info->port.ops = &mxser_port_ops;
		info->board = brd;
		info->ldisc_stop_rx = 0;

		/* Enhance mode enabled here */
		if (brd->must_hwid != MOXA_OTHER_UART)
			mxser_must_set_enhance_mode(info->ioaddr, true);

		info->type = PORT_16550A;

		mxser_process_txrx_fifo(info);

		info->port.close_delay = 5 * HZ / 10;
		info->port.closing_wait = 30 * HZ;
		spin_lock_init(&info->slock);

		/* before set INT ISR, disable all int */
		outb(inb(info->ioaddr + UART_IER) & 0xf0,
			info->ioaddr + UART_IER);
	}
}

static int mxser_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	struct mxser_board *brd;
	unsigned int i, base;
	unsigned long ioaddress;
	unsigned short nports = MXSER_NPORTS(ent->driver_data);
	struct device *tty_dev;
	int retval = -EINVAL;

	i = find_first_zero_bit(mxser_boards, MXSER_BOARDS);
	if (i >= MXSER_BOARDS) {
		dev_err(&pdev->dev, "too many boards found (maximum %d), board "
				"not configured\n", MXSER_BOARDS);
		goto err;
	}

	brd = devm_kzalloc(&pdev->dev, struct_size(brd, ports, nports),
			GFP_KERNEL);
	if (!brd)
		goto err;

	brd->idx = i;
	__set_bit(brd->idx, mxser_boards);
	base = i * MXSER_PORTS_PER_BOARD;

	retval = pcim_enable_device(pdev);
	if (retval) {
		dev_err(&pdev->dev, "PCI enable failed\n");
		goto err_zero;
	}

	/* io address */
	ioaddress = pci_resource_start(pdev, 2);
	retval = pci_request_region(pdev, 2, "mxser(IO)");
	if (retval)
		goto err_zero;

	brd->nports = nports;
	for (i = 0; i < nports; i++)
		brd->ports[i].ioaddr = ioaddress + 8 * i;

	/* vector */
	ioaddress = pci_resource_start(pdev, 3);
	retval = pci_request_region(pdev, 3, "mxser(vector)");
	if (retval)
		goto err_zero;
	brd->vector = ioaddress;

	/* irq */
	brd->irq = pdev->irq;

	mxser_initbrd(brd, ent->driver_data & MXSER_HIGHBAUD);

	retval = devm_request_irq(&pdev->dev, brd->irq, mxser_interrupt,
			IRQF_SHARED, "mxser", brd);
	if (retval) {
		dev_err(&pdev->dev, "request irq failed");
		goto err_relbrd;
	}

	for (i = 0; i < nports; i++) {
		tty_dev = tty_port_register_device(&brd->ports[i].port,
				mxvar_sdriver, base + i, &pdev->dev);
		if (IS_ERR(tty_dev)) {
			retval = PTR_ERR(tty_dev);
			for (; i > 0; i--)
				tty_unregister_device(mxvar_sdriver,
					base + i - 1);
			goto err_relbrd;
		}
	}

	pci_set_drvdata(pdev, brd);

	return 0;
err_relbrd:
	for (i = 0; i < nports; i++)
		tty_port_destroy(&brd->ports[i].port);
err_zero:
	__clear_bit(brd->idx, mxser_boards);
err:
	return retval;
}

static void mxser_remove(struct pci_dev *pdev)
{
	struct mxser_board *brd = pci_get_drvdata(pdev);
	unsigned int i, base = brd->idx * MXSER_PORTS_PER_BOARD;

	for (i = 0; i < brd->nports; i++) {
		tty_unregister_device(mxvar_sdriver, base + i);
		tty_port_destroy(&brd->ports[i].port);
	}

	__clear_bit(brd->idx, mxser_boards);
}

static struct pci_driver mxser_driver = {
	.name = "mxser",
	.id_table = mxser_pcibrds,
	.probe = mxser_probe,
	.remove = mxser_remove
};

static int __init mxser_module_init(void)
{
	int retval;

	mxvar_sdriver = tty_alloc_driver(MXSER_PORTS, TTY_DRIVER_REAL_RAW |
			TTY_DRIVER_DYNAMIC_DEV);
	if (IS_ERR(mxvar_sdriver))
		return PTR_ERR(mxvar_sdriver);

	/* Initialize the tty_driver structure */
	mxvar_sdriver->name = "ttyMI";
	mxvar_sdriver->major = ttymajor;
	mxvar_sdriver->minor_start = 0;
	mxvar_sdriver->type = TTY_DRIVER_TYPE_SERIAL;
	mxvar_sdriver->subtype = SERIAL_TYPE_NORMAL;
	mxvar_sdriver->init_termios = tty_std_termios;
	mxvar_sdriver->init_termios.c_cflag = B9600|CS8|CREAD|HUPCL|CLOCAL;
	tty_set_operations(mxvar_sdriver, &mxser_ops);

	retval = tty_register_driver(mxvar_sdriver);
	if (retval) {
		printk(KERN_ERR "Couldn't install MOXA Smartio/Industio family "
				"tty driver !\n");
		goto err_put;
	}

	retval = pci_register_driver(&mxser_driver);
	if (retval) {
		printk(KERN_ERR "mxser: can't register pci driver\n");
		goto err_unr;
	}

	return 0;
err_unr:
	tty_unregister_driver(mxvar_sdriver);
err_put:
	tty_driver_kref_put(mxvar_sdriver);
	return retval;
}

static void __exit mxser_module_exit(void)
{
	pci_unregister_driver(&mxser_driver);
	tty_unregister_driver(mxvar_sdriver);
	tty_driver_kref_put(mxvar_sdriver);
}

module_init(mxser_module_init);
module_exit(mxser_module_exit);

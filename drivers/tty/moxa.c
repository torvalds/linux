// SPDX-License-Identifier: GPL-2.0+
/*****************************************************************************/
/*
 *           moxa.c  -- MOXA Intellio family multiport serial driver.
 *
 *      Copyright (C) 1999-2000  Moxa Technologies (support@moxa.com).
 *      Copyright (c) 2007 Jiri Slaby <jirislaby@gmail.com>
 *
 *      This code is loosely based on the Linux serial driver, written by
 *      Linus Torvalds, Theodore T'so and others.
 */

/*
 *    MOXA Intellio Series Driver
 *      for             : LINUX
 *      date            : 1999/1/7
 *      version         : 5.1
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/serial.h>
#include <linux/tty_driver.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>

#include <asm/io.h>

/*
 *    System Configuration
 */

#define Magic_code	0x404

/*
 *    for C218 BIOS initialization
 */
#define C218_ConfBase	0x800
#define C218_status	(C218_ConfBase + 0)	/* BIOS running status    */
#define C218_diag	(C218_ConfBase + 2)	/* diagnostic status      */
#define C218_key	(C218_ConfBase + 4)	/* WORD (0x218 for C218) */
#define C218DLoad_len	(C218_ConfBase + 6)	/* WORD           */
#define C218check_sum	(C218_ConfBase + 8)	/* BYTE           */
#define C218chksum_ok	(C218_ConfBase + 0x0a)	/* BYTE (1:ok)            */
#define C218_TestRx	(C218_ConfBase + 0x10)	/* 8 bytes for 8 ports    */
#define C218_TestTx	(C218_ConfBase + 0x18)	/* 8 bytes for 8 ports    */
#define C218_RXerr	(C218_ConfBase + 0x20)	/* 8 bytes for 8 ports    */
#define C218_ErrFlag	(C218_ConfBase + 0x28)	/* 8 bytes for 8 ports    */

#define C218_LoadBuf	0x0F00
#define C218_KeyCode	0x218
#define CP204J_KeyCode	0x204

/*
 *    for C320 BIOS initialization
 */
#define C320_ConfBase	0x800
#define C320_LoadBuf	0x0f00
#define STS_init	0x05	/* for C320_status        */

#define C320_status	C320_ConfBase + 0	/* BIOS running status    */
#define C320_diag	C320_ConfBase + 2	/* diagnostic status      */
#define C320_key	C320_ConfBase + 4	/* WORD (0320H for C320) */
#define C320DLoad_len	C320_ConfBase + 6	/* WORD           */
#define C320check_sum	C320_ConfBase + 8	/* WORD           */
#define C320chksum_ok	C320_ConfBase + 0x0a	/* WORD (1:ok)            */
#define C320bapi_len	C320_ConfBase + 0x0c	/* WORD           */
#define C320UART_no	C320_ConfBase + 0x0e	/* WORD           */

#define C320_KeyCode	0x320

#define FixPage_addr	0x0000	/* starting addr of static page  */
#define DynPage_addr	0x2000	/* starting addr of dynamic page */
#define C218_start	0x3000	/* starting addr of C218 BIOS prg */
#define Control_reg	0x1ff0	/* select page and reset control */
#define HW_reset	0x80

/*
 *    Function Codes
 */
#define FC_CardReset	0x80
#define FC_ChannelReset 1	/* C320 firmware not supported */
#define FC_EnableCH	2
#define FC_DisableCH	3
#define FC_SetParam	4
#define FC_SetMode	5
#define FC_SetRate	6
#define FC_LineControl	7
#define FC_LineStatus	8
#define FC_XmitControl	9
#define FC_FlushQueue	10
#define FC_SendBreak	11
#define FC_StopBreak	12
#define FC_LoopbackON	13
#define FC_LoopbackOFF	14
#define FC_ClrIrqTable	15
#define FC_SendXon	16
#define FC_SetTermIrq	17	/* C320 firmware not supported */
#define FC_SetCntIrq	18	/* C320 firmware not supported */
#define FC_SetBreakIrq	19
#define FC_SetLineIrq	20
#define FC_SetFlowCtl	21
#define FC_GenIrq	22
#define FC_InCD180	23
#define FC_OutCD180	24
#define FC_InUARTreg	23
#define FC_OutUARTreg	24
#define FC_SetXonXoff	25
#define FC_OutCD180CCR	26
#define FC_ExtIQueue	27
#define FC_ExtOQueue	28
#define FC_ClrLineIrq	29
#define FC_HWFlowCtl	30
#define FC_GetClockRate 35
#define FC_SetBaud	36
#define FC_SetDataMode  41
#define FC_GetCCSR      43
#define FC_GetDataError 45
#define FC_RxControl	50
#define FC_ImmSend	51
#define FC_SetXonState	52
#define FC_SetXoffState	53
#define FC_SetRxFIFOTrig 54
#define FC_SetTxFIFOCnt 55
#define FC_UnixRate	56
#define FC_UnixResetTimer 57

#define	RxFIFOTrig1	0
#define	RxFIFOTrig4	1
#define	RxFIFOTrig8	2
#define	RxFIFOTrig14	3

/*
 *    Dual-Ported RAM
 */
#define DRAM_global	0
#define INT_data	(DRAM_global + 0)
#define Config_base	(DRAM_global + 0x108)

#define IRQindex	(INT_data + 0)
#define IRQpending	(INT_data + 4)
#define IRQtable	(INT_data + 8)

/*
 *    Interrupt Status
 */
#define IntrRx		0x01	/* receiver data O.K.             */
#define IntrTx		0x02	/* transmit buffer empty  */
#define IntrFunc	0x04	/* function complete              */
#define IntrBreak	0x08	/* received break         */
#define IntrLine	0x10	/* line status change
				   for transmitter                */
#define IntrIntr	0x20	/* received INTR code             */
#define IntrQuit	0x40	/* received QUIT code             */
#define IntrEOF		0x80	/* received EOF code              */

#define IntrRxTrigger	0x100	/* rx data count reach trigger value */
#define IntrTxTrigger	0x200	/* tx data count below trigger value */

#define Magic_no	(Config_base + 0)
#define Card_model_no	(Config_base + 2)
#define Total_ports	(Config_base + 4)
#define Module_cnt	(Config_base + 8)
#define Module_no	(Config_base + 10)
#define Timer_10ms	(Config_base + 14)
#define Disable_IRQ	(Config_base + 20)
#define TMS320_PORT1	(Config_base + 22)
#define TMS320_PORT2	(Config_base + 24)
#define TMS320_CLOCK	(Config_base + 26)

/*
 *    DATA BUFFER in DRAM
 */
#define Extern_table	0x400	/* Base address of the external table
				   (24 words *    64) total 3K bytes
				   (24 words * 128) total 6K bytes */
#define Extern_size	0x60	/* 96 bytes                       */
#define RXrptr		0x00	/* read pointer for RX buffer     */
#define RXwptr		0x02	/* write pointer for RX buffer    */
#define TXrptr		0x04	/* read pointer for TX buffer     */
#define TXwptr		0x06	/* write pointer for TX buffer    */
#define HostStat	0x08	/* IRQ flag and general flag      */
#define FlagStat	0x0A
#define FlowControl	0x0C	/* B7 B6 B5 B4 B3 B2 B1 B0              */
				/*  x  x  x  x  |  |  |  |            */
				/*              |  |  |  + CTS flow   */
				/*              |  |  +--- RTS flow   */
				/*              |  +------ TX Xon/Xoff */
				/*              +--------- RX Xon/Xoff */
#define Break_cnt	0x0E	/* received break count   */
#define CD180TXirq	0x10	/* if non-0: enable TX irq        */
#define RX_mask		0x12
#define TX_mask		0x14
#define Ofs_rxb		0x16
#define Ofs_txb		0x18
#define Page_rxb	0x1A
#define Page_txb	0x1C
#define EndPage_rxb	0x1E
#define EndPage_txb	0x20
#define Data_error	0x22
#define RxTrigger	0x28
#define TxTrigger	0x2a

#define rRXwptr		0x34
#define Low_water	0x36

#define FuncCode	0x40
#define FuncArg		0x42
#define FuncArg1	0x44

#define C218rx_size	0x2000	/* 8K bytes */
#define C218tx_size	0x8000	/* 32K bytes */

#define C218rx_mask	(C218rx_size - 1)
#define C218tx_mask	(C218tx_size - 1)

#define C320p8rx_size	0x2000
#define C320p8tx_size	0x8000
#define C320p8rx_mask	(C320p8rx_size - 1)
#define C320p8tx_mask	(C320p8tx_size - 1)

#define C320p16rx_size	0x2000
#define C320p16tx_size	0x4000
#define C320p16rx_mask	(C320p16rx_size - 1)
#define C320p16tx_mask	(C320p16tx_size - 1)

#define C320p24rx_size	0x2000
#define C320p24tx_size	0x2000
#define C320p24rx_mask	(C320p24rx_size - 1)
#define C320p24tx_mask	(C320p24tx_size - 1)

#define C320p32rx_size	0x1000
#define C320p32tx_size	0x1000
#define C320p32rx_mask	(C320p32rx_size - 1)
#define C320p32tx_mask	(C320p32tx_size - 1)

#define Page_size	0x2000U
#define Page_mask	(Page_size - 1)
#define C218rx_spage	3
#define C218tx_spage	4
#define C218rx_pageno	1
#define C218tx_pageno	4
#define C218buf_pageno	5

#define C320p8rx_spage	3
#define C320p8tx_spage	4
#define C320p8rx_pgno	1
#define C320p8tx_pgno	4
#define C320p8buf_pgno	5

#define C320p16rx_spage 3
#define C320p16tx_spage 4
#define C320p16rx_pgno	1
#define C320p16tx_pgno	2
#define C320p16buf_pgno 3

#define C320p24rx_spage 3
#define C320p24tx_spage 4
#define C320p24rx_pgno	1
#define C320p24tx_pgno	1
#define C320p24buf_pgno 2

#define C320p32rx_spage 3
#define C320p32tx_ofs	C320p32rx_size
#define C320p32tx_spage 3
#define C320p32buf_pgno 1

/*
 *    Host Status
 */
#define WakeupRx	0x01
#define WakeupTx	0x02
#define WakeupBreak	0x08
#define WakeupLine	0x10
#define WakeupIntr	0x20
#define WakeupQuit	0x40
#define WakeupEOF	0x80	/* used in VTIME control */
#define WakeupRxTrigger	0x100
#define WakeupTxTrigger	0x200
/*
 *    Flag status
 */
#define Rx_over		0x01
#define Xoff_state	0x02
#define Tx_flowOff	0x04
#define Tx_enable	0x08
#define CTS_state	0x10
#define DSR_state	0x20
#define DCD_state	0x80
/*
 *    FlowControl
 */
#define CTS_FlowCtl	1
#define RTS_FlowCtl	2
#define Tx_FlowCtl	4
#define Rx_FlowCtl	8
#define IXM_IXANY	0x10

#define LowWater	128

#define DTR_ON		1
#define RTS_ON		2
#define CTS_ON		1
#define DSR_ON		2
#define DCD_ON		8

/* mode definition */
#define	MX_CS8		0x03
#define	MX_CS7		0x02
#define	MX_CS6		0x01
#define	MX_CS5		0x00

#define	MX_STOP1	0x00
#define	MX_STOP15	0x04
#define	MX_STOP2	0x08

#define	MX_PARNONE	0x00
#define	MX_PAREVEN	0x40
#define	MX_PARODD	0xC0
#define	MX_PARMARK	0xA0
#define	MX_PARSPACE	0x20

#define MOXA_FW_HDRLEN		32

#define MOXAMAJOR		172

#define MAX_BOARDS		4	/* Don't change this value */
#define MAX_PORTS_PER_BOARD	32	/* Don't change this value */
#define MAX_PORTS		(MAX_BOARDS * MAX_PORTS_PER_BOARD)

#define MOXA_IS_320(brd)	((brd)->boardType == MOXA_BOARD_C320_PCI)

enum {
	MOXA_BOARD_C218_PCI = 1,
	MOXA_BOARD_C320_PCI,
	MOXA_BOARD_CP204J,
};

static char *moxa_brdname[] =
{
	"C218 Turbo PCI series",
	"C320 Turbo PCI series",
	"CP-204J series",
};

static const struct pci_device_id moxa_pcibrds[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_C218),
		.driver_data = MOXA_BOARD_C218_PCI },
	{ PCI_DEVICE(PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_C320),
		.driver_data = MOXA_BOARD_C320_PCI },
	{ PCI_DEVICE(PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CP204J),
		.driver_data = MOXA_BOARD_CP204J },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, moxa_pcibrds);

struct moxa_port;

static struct moxa_board_conf {
	int boardType;
	int numPorts;

	unsigned int ready;

	struct moxa_port *ports;

	void __iomem *basemem;
	void __iomem *intNdx;
	void __iomem *intPend;
	void __iomem *intTable;
} moxa_boards[MAX_BOARDS];

struct moxa_port {
	struct tty_port port;
	struct moxa_board_conf *board;
	void __iomem *tableAddr;

	int type;
	int cflag;
	unsigned long statusflags;

	u8 DCDState;		/* Protected by the port lock */
	u8 lineCtrl;
	u8 lowChkFlag;
};

/* statusflags */
#define TXSTOPPED	1
#define LOWWAIT 	2
#define EMPTYWAIT	3


#define WAKEUP_CHARS		256

static int ttymajor = MOXAMAJOR;
static unsigned int moxaFuncTout = HZ / 2;
static unsigned int moxaLowWaterChk;
static DEFINE_MUTEX(moxa_openlock);
static DEFINE_SPINLOCK(moxa_lock);

MODULE_AUTHOR("William Chen");
MODULE_DESCRIPTION("MOXA Intellio Family Multiport Board Device Driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("c218tunx.cod");
MODULE_FIRMWARE("cp204unx.cod");
MODULE_FIRMWARE("c320tunx.cod");

module_param(ttymajor, int, 0);

/*
 * static functions:
 */
static int moxa_open(struct tty_struct *, struct file *);
static void moxa_close(struct tty_struct *, struct file *);
static ssize_t moxa_write(struct tty_struct *, const u8 *, size_t);
static unsigned int moxa_write_room(struct tty_struct *);
static void moxa_flush_buffer(struct tty_struct *);
static unsigned int moxa_chars_in_buffer(struct tty_struct *);
static void moxa_set_termios(struct tty_struct *, const struct ktermios *);
static void moxa_stop(struct tty_struct *);
static void moxa_start(struct tty_struct *);
static void moxa_hangup(struct tty_struct *);
static int moxa_tiocmget(struct tty_struct *tty);
static int moxa_tiocmset(struct tty_struct *tty,
			 unsigned int set, unsigned int clear);
static void moxa_poll(struct timer_list *);
static void moxa_set_tty_param(struct tty_struct *, const struct ktermios *);
static void moxa_shutdown(struct tty_port *);
static bool moxa_carrier_raised(struct tty_port *);
static void moxa_dtr_rts(struct tty_port *, bool);
/*
 * moxa board interface functions:
 */
static void MoxaPortEnable(struct moxa_port *);
static void MoxaPortDisable(struct moxa_port *);
static int MoxaPortSetTermio(struct moxa_port *, struct ktermios *, speed_t);
static int MoxaPortGetLineOut(struct moxa_port *, bool *, bool *);
static void MoxaPortLineCtrl(struct moxa_port *, bool, bool);
static void MoxaPortFlowCtrl(struct moxa_port *, int, int, int, int, int);
static int MoxaPortLineStatus(struct moxa_port *);
static void MoxaPortFlushData(struct moxa_port *, int);
static ssize_t MoxaPortWriteData(struct tty_struct *, const u8 *, size_t);
static int MoxaPortReadData(struct moxa_port *);
static unsigned int MoxaPortTxQueue(struct moxa_port *);
static int MoxaPortRxQueue(struct moxa_port *);
static unsigned int MoxaPortTxFree(struct moxa_port *);
static void MoxaPortTxDisable(struct moxa_port *);
static void MoxaPortTxEnable(struct moxa_port *);
static int moxa_get_serial_info(struct tty_struct *, struct serial_struct *);
static int moxa_set_serial_info(struct tty_struct *, struct serial_struct *);
static void MoxaSetFifo(struct moxa_port *port, int enable);

/*
 * I/O functions
 */

static DEFINE_SPINLOCK(moxafunc_lock);

static void moxa_wait_finish(void __iomem *ofsAddr)
{
	unsigned long end = jiffies + moxaFuncTout;

	while (readw(ofsAddr + FuncCode) != 0)
		if (time_after(jiffies, end))
			return;
	if (readw(ofsAddr + FuncCode) != 0)
		printk_ratelimited(KERN_WARNING "moxa function expired\n");
}

static void moxafunc(void __iomem *ofsAddr, u16 cmd, u16 arg)
{
        unsigned long flags;
        spin_lock_irqsave(&moxafunc_lock, flags);
	writew(arg, ofsAddr + FuncArg);
	writew(cmd, ofsAddr + FuncCode);
	moxa_wait_finish(ofsAddr);
	spin_unlock_irqrestore(&moxafunc_lock, flags);
}

static int moxafuncret(void __iomem *ofsAddr, u16 cmd, u16 arg)
{
        unsigned long flags;
        u16 ret;
        spin_lock_irqsave(&moxafunc_lock, flags);
	writew(arg, ofsAddr + FuncArg);
	writew(cmd, ofsAddr + FuncCode);
	moxa_wait_finish(ofsAddr);
	ret = readw(ofsAddr + FuncArg);
	spin_unlock_irqrestore(&moxafunc_lock, flags);
	return ret;
}

static void moxa_low_water_check(void __iomem *ofsAddr)
{
	u16 rptr, wptr, mask, len;

	if (readb(ofsAddr + FlagStat) & Xoff_state) {
		rptr = readw(ofsAddr + RXrptr);
		wptr = readw(ofsAddr + RXwptr);
		mask = readw(ofsAddr + RX_mask);
		len = (wptr - rptr) & mask;
		if (len <= Low_water)
			moxafunc(ofsAddr, FC_SendXon, 0);
	}
}

/*
 * TTY operations
 */

static int moxa_break_ctl(struct tty_struct *tty, int state)
{
	struct moxa_port *port = tty->driver_data;

	moxafunc(port->tableAddr, state ? FC_SendBreak : FC_StopBreak,
			Magic_code);
	return 0;
}

static const struct tty_operations moxa_ops = {
	.open = moxa_open,
	.close = moxa_close,
	.write = moxa_write,
	.write_room = moxa_write_room,
	.flush_buffer = moxa_flush_buffer,
	.chars_in_buffer = moxa_chars_in_buffer,
	.set_termios = moxa_set_termios,
	.stop = moxa_stop,
	.start = moxa_start,
	.hangup = moxa_hangup,
	.break_ctl = moxa_break_ctl,
	.tiocmget = moxa_tiocmget,
	.tiocmset = moxa_tiocmset,
	.set_serial = moxa_set_serial_info,
	.get_serial = moxa_get_serial_info,
};

static const struct tty_port_operations moxa_port_ops = {
	.carrier_raised = moxa_carrier_raised,
	.dtr_rts = moxa_dtr_rts,
	.shutdown = moxa_shutdown,
};

static struct tty_driver *moxaDriver;
static DEFINE_TIMER(moxaTimer, moxa_poll);

/*
 * HW init
 */

static int moxa_check_fw_model(struct moxa_board_conf *brd, u8 model)
{
	switch (brd->boardType) {
	case MOXA_BOARD_C218_PCI:
		if (model != 1)
			goto err;
		break;
	case MOXA_BOARD_CP204J:
		if (model != 3)
			goto err;
		break;
	default:
		if (model != 2)
			goto err;
		break;
	}
	return 0;
err:
	return -EINVAL;
}

static int moxa_check_fw(const void *ptr)
{
	const __le16 *lptr = ptr;

	if (*lptr != cpu_to_le16(0x7980))
		return -EINVAL;

	return 0;
}

static int moxa_load_bios(struct moxa_board_conf *brd, const u8 *buf,
		size_t len)
{
	void __iomem *baseAddr = brd->basemem;
	u16 tmp;

	writeb(HW_reset, baseAddr + Control_reg);	/* reset */
	msleep(10);
	memset_io(baseAddr, 0, 4096);
	memcpy_toio(baseAddr, buf, len);	/* download BIOS */
	writeb(0, baseAddr + Control_reg);	/* restart */

	msleep(2000);

	switch (brd->boardType) {
	case MOXA_BOARD_C218_PCI:
		tmp = readw(baseAddr + C218_key);
		if (tmp != C218_KeyCode)
			goto err;
		break;
	case MOXA_BOARD_CP204J:
		tmp = readw(baseAddr + C218_key);
		if (tmp != CP204J_KeyCode)
			goto err;
		break;
	default:
		tmp = readw(baseAddr + C320_key);
		if (tmp != C320_KeyCode)
			goto err;
		tmp = readw(baseAddr + C320_status);
		if (tmp != STS_init) {
			printk(KERN_ERR "MOXA: bios upload failed -- CPU/Basic "
					"module not found\n");
			return -EIO;
		}
		break;
	}

	return 0;
err:
	printk(KERN_ERR "MOXA: bios upload failed -- board not found\n");
	return -EIO;
}

static int moxa_load_320b(struct moxa_board_conf *brd, const u8 *ptr,
		size_t len)
{
	void __iomem *baseAddr = brd->basemem;

	if (len < 7168) {
		printk(KERN_ERR "MOXA: invalid 320 bios -- too short\n");
		return -EINVAL;
	}

	writew(len - 7168 - 2, baseAddr + C320bapi_len);
	writeb(1, baseAddr + Control_reg);	/* Select Page 1 */
	memcpy_toio(baseAddr + DynPage_addr, ptr, 7168);
	writeb(2, baseAddr + Control_reg);	/* Select Page 2 */
	memcpy_toio(baseAddr + DynPage_addr, ptr + 7168, len - 7168);

	return 0;
}

static int moxa_real_load_code(struct moxa_board_conf *brd, const void *ptr,
		size_t len)
{
	void __iomem *baseAddr = brd->basemem;
	const __le16 *uptr = ptr;
	size_t wlen, len2, j;
	unsigned long key, loadbuf, loadlen, checksum, checksum_ok;
	unsigned int i, retry;
	u16 usum, keycode;

	keycode = (brd->boardType == MOXA_BOARD_CP204J) ? CP204J_KeyCode :
				C218_KeyCode;

	switch (brd->boardType) {
	case MOXA_BOARD_CP204J:
	case MOXA_BOARD_C218_PCI:
		key = C218_key;
		loadbuf = C218_LoadBuf;
		loadlen = C218DLoad_len;
		checksum = C218check_sum;
		checksum_ok = C218chksum_ok;
		break;
	default:
		key = C320_key;
		keycode = C320_KeyCode;
		loadbuf = C320_LoadBuf;
		loadlen = C320DLoad_len;
		checksum = C320check_sum;
		checksum_ok = C320chksum_ok;
		break;
	}

	usum = 0;
	wlen = len >> 1;
	for (i = 0; i < wlen; i++)
		usum += le16_to_cpu(uptr[i]);
	retry = 0;
	do {
		wlen = len >> 1;
		j = 0;
		while (wlen) {
			len2 = (wlen > 2048) ? 2048 : wlen;
			wlen -= len2;
			memcpy_toio(baseAddr + loadbuf, ptr + j, len2 << 1);
			j += len2 << 1;

			writew(len2, baseAddr + loadlen);
			writew(0, baseAddr + key);
			for (i = 0; i < 100; i++) {
				if (readw(baseAddr + key) == keycode)
					break;
				msleep(10);
			}
			if (readw(baseAddr + key) != keycode)
				return -EIO;
		}
		writew(0, baseAddr + loadlen);
		writew(usum, baseAddr + checksum);
		writew(0, baseAddr + key);
		for (i = 0; i < 100; i++) {
			if (readw(baseAddr + key) == keycode)
				break;
			msleep(10);
		}
		retry++;
	} while ((readb(baseAddr + checksum_ok) != 1) && (retry < 3));
	if (readb(baseAddr + checksum_ok) != 1)
		return -EIO;

	writew(0, baseAddr + key);
	for (i = 0; i < 600; i++) {
		if (readw(baseAddr + Magic_no) == Magic_code)
			break;
		msleep(10);
	}
	if (readw(baseAddr + Magic_no) != Magic_code)
		return -EIO;

	if (MOXA_IS_320(brd)) {
		writew(0x3800, baseAddr + TMS320_PORT1);
		writew(0x3900, baseAddr + TMS320_PORT2);
		writew(28499, baseAddr + TMS320_CLOCK);
	}
	writew(1, baseAddr + Disable_IRQ);
	writew(0, baseAddr + Magic_no);
	for (i = 0; i < 500; i++) {
		if (readw(baseAddr + Magic_no) == Magic_code)
			break;
		msleep(10);
	}
	if (readw(baseAddr + Magic_no) != Magic_code)
		return -EIO;

	if (MOXA_IS_320(brd)) {
		j = readw(baseAddr + Module_cnt);
		if (j <= 0)
			return -EIO;
		brd->numPorts = j * 8;
		writew(j, baseAddr + Module_no);
		writew(0, baseAddr + Magic_no);
		for (i = 0; i < 600; i++) {
			if (readw(baseAddr + Magic_no) == Magic_code)
				break;
			msleep(10);
		}
		if (readw(baseAddr + Magic_no) != Magic_code)
			return -EIO;
	}
	brd->intNdx = baseAddr + IRQindex;
	brd->intPend = baseAddr + IRQpending;
	brd->intTable = baseAddr + IRQtable;

	return 0;
}

static int moxa_load_code(struct moxa_board_conf *brd, const void *ptr,
		size_t len)
{
	void __iomem *ofsAddr, *baseAddr = brd->basemem;
	struct moxa_port *port;
	int retval, i;

	if (len % 2) {
		printk(KERN_ERR "MOXA: bios length is not even\n");
		return -EINVAL;
	}

	retval = moxa_real_load_code(brd, ptr, len); /* may change numPorts */
	if (retval)
		return retval;

	switch (brd->boardType) {
	case MOXA_BOARD_C218_PCI:
	case MOXA_BOARD_CP204J:
		port = brd->ports;
		for (i = 0; i < brd->numPorts; i++, port++) {
			port->board = brd;
			port->DCDState = 0;
			port->tableAddr = baseAddr + Extern_table +
					Extern_size * i;
			ofsAddr = port->tableAddr;
			writew(C218rx_mask, ofsAddr + RX_mask);
			writew(C218tx_mask, ofsAddr + TX_mask);
			writew(C218rx_spage + i * C218buf_pageno, ofsAddr + Page_rxb);
			writew(readw(ofsAddr + Page_rxb) + C218rx_pageno, ofsAddr + EndPage_rxb);

			writew(C218tx_spage + i * C218buf_pageno, ofsAddr + Page_txb);
			writew(readw(ofsAddr + Page_txb) + C218tx_pageno, ofsAddr + EndPage_txb);

		}
		break;
	default:
		port = brd->ports;
		for (i = 0; i < brd->numPorts; i++, port++) {
			port->board = brd;
			port->DCDState = 0;
			port->tableAddr = baseAddr + Extern_table +
					Extern_size * i;
			ofsAddr = port->tableAddr;
			switch (brd->numPorts) {
			case 8:
				writew(C320p8rx_mask, ofsAddr + RX_mask);
				writew(C320p8tx_mask, ofsAddr + TX_mask);
				writew(C320p8rx_spage + i * C320p8buf_pgno, ofsAddr + Page_rxb);
				writew(readw(ofsAddr + Page_rxb) + C320p8rx_pgno, ofsAddr + EndPage_rxb);
				writew(C320p8tx_spage + i * C320p8buf_pgno, ofsAddr + Page_txb);
				writew(readw(ofsAddr + Page_txb) + C320p8tx_pgno, ofsAddr + EndPage_txb);

				break;
			case 16:
				writew(C320p16rx_mask, ofsAddr + RX_mask);
				writew(C320p16tx_mask, ofsAddr + TX_mask);
				writew(C320p16rx_spage + i * C320p16buf_pgno, ofsAddr + Page_rxb);
				writew(readw(ofsAddr + Page_rxb) + C320p16rx_pgno, ofsAddr + EndPage_rxb);
				writew(C320p16tx_spage + i * C320p16buf_pgno, ofsAddr + Page_txb);
				writew(readw(ofsAddr + Page_txb) + C320p16tx_pgno, ofsAddr + EndPage_txb);
				break;

			case 24:
				writew(C320p24rx_mask, ofsAddr + RX_mask);
				writew(C320p24tx_mask, ofsAddr + TX_mask);
				writew(C320p24rx_spage + i * C320p24buf_pgno, ofsAddr + Page_rxb);
				writew(readw(ofsAddr + Page_rxb) + C320p24rx_pgno, ofsAddr + EndPage_rxb);
				writew(C320p24tx_spage + i * C320p24buf_pgno, ofsAddr + Page_txb);
				writew(readw(ofsAddr + Page_txb), ofsAddr + EndPage_txb);
				break;
			case 32:
				writew(C320p32rx_mask, ofsAddr + RX_mask);
				writew(C320p32tx_mask, ofsAddr + TX_mask);
				writew(C320p32tx_ofs, ofsAddr + Ofs_txb);
				writew(C320p32rx_spage + i * C320p32buf_pgno, ofsAddr + Page_rxb);
				writew(readb(ofsAddr + Page_rxb), ofsAddr + EndPage_rxb);
				writew(C320p32tx_spage + i * C320p32buf_pgno, ofsAddr + Page_txb);
				writew(readw(ofsAddr + Page_txb), ofsAddr + EndPage_txb);
				break;
			}
		}
		break;
	}
	return 0;
}

static int moxa_load_fw(struct moxa_board_conf *brd, const struct firmware *fw)
{
	const void *ptr = fw->data;
	char rsn[64];
	u16 lens[5];
	size_t len;
	unsigned int a, lenp, lencnt;
	int ret = -EINVAL;
	struct {
		__le32 magic;	/* 0x34303430 */
		u8 reserved1[2];
		u8 type;	/* UNIX = 3 */
		u8 model;	/* C218T=1, C320T=2, CP204=3 */
		u8 reserved2[8];
		__le16 len[5];
	} const *hdr = ptr;

	BUILD_BUG_ON(ARRAY_SIZE(hdr->len) != ARRAY_SIZE(lens));

	if (fw->size < MOXA_FW_HDRLEN) {
		strcpy(rsn, "too short (even header won't fit)");
		goto err;
	}
	if (hdr->magic != cpu_to_le32(0x30343034)) {
		sprintf(rsn, "bad magic: %.8x", le32_to_cpu(hdr->magic));
		goto err;
	}
	if (hdr->type != 3) {
		sprintf(rsn, "not for linux, type is %u", hdr->type);
		goto err;
	}
	if (moxa_check_fw_model(brd, hdr->model)) {
		sprintf(rsn, "not for this card, model is %u", hdr->model);
		goto err;
	}

	len = MOXA_FW_HDRLEN;
	lencnt = hdr->model == 2 ? 5 : 3;
	for (a = 0; a < ARRAY_SIZE(lens); a++) {
		lens[a] = le16_to_cpu(hdr->len[a]);
		if (lens[a] && len + lens[a] <= fw->size &&
				moxa_check_fw(&fw->data[len]))
			printk(KERN_WARNING "MOXA firmware: unexpected input "
				"at offset %u, but going on\n", (u32)len);
		if (!lens[a] && a < lencnt) {
			sprintf(rsn, "too few entries in fw file");
			goto err;
		}
		len += lens[a];
	}

	if (len != fw->size) {
		sprintf(rsn, "bad length: %u (should be %u)", (u32)fw->size,
				(u32)len);
		goto err;
	}

	ptr += MOXA_FW_HDRLEN;
	lenp = 0; /* bios */

	strcpy(rsn, "read above");

	ret = moxa_load_bios(brd, ptr, lens[lenp]);
	if (ret)
		goto err;

	/* we skip the tty section (lens[1]), since we don't need it */
	ptr += lens[lenp] + lens[lenp + 1];
	lenp += 2; /* comm */

	if (hdr->model == 2) {
		ret = moxa_load_320b(brd, ptr, lens[lenp]);
		if (ret)
			goto err;
		/* skip another tty */
		ptr += lens[lenp] + lens[lenp + 1];
		lenp += 2;
	}

	ret = moxa_load_code(brd, ptr, lens[lenp]);
	if (ret)
		goto err;

	return 0;
err:
	printk(KERN_ERR "firmware failed to load, reason: %s\n", rsn);
	return ret;
}

static int moxa_init_board(struct moxa_board_conf *brd, struct device *dev)
{
	const struct firmware *fw;
	const char *file;
	struct moxa_port *p;
	unsigned int i, first_idx;
	int ret;

	brd->ports = kcalloc(MAX_PORTS_PER_BOARD, sizeof(*brd->ports),
			GFP_KERNEL);
	if (brd->ports == NULL) {
		printk(KERN_ERR "cannot allocate memory for ports\n");
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0, p = brd->ports; i < MAX_PORTS_PER_BOARD; i++, p++) {
		tty_port_init(&p->port);
		p->port.ops = &moxa_port_ops;
		p->type = PORT_16550A;
		p->cflag = B9600 | CS8 | CREAD | CLOCAL | HUPCL;
	}

	switch (brd->boardType) {
	case MOXA_BOARD_C218_PCI:
		file = "c218tunx.cod";
		break;
	case MOXA_BOARD_CP204J:
		file = "cp204unx.cod";
		break;
	default:
		file = "c320tunx.cod";
		break;
	}

	ret = request_firmware(&fw, file, dev);
	if (ret) {
		printk(KERN_ERR "MOXA: request_firmware failed. Make sure "
				"you've placed '%s' file into your firmware "
				"loader directory (e.g. /lib/firmware)\n",
				file);
		goto err_free;
	}

	ret = moxa_load_fw(brd, fw);

	release_firmware(fw);

	if (ret)
		goto err_free;

	spin_lock_bh(&moxa_lock);
	brd->ready = 1;
	if (!timer_pending(&moxaTimer))
		mod_timer(&moxaTimer, jiffies + HZ / 50);
	spin_unlock_bh(&moxa_lock);

	first_idx = (brd - moxa_boards) * MAX_PORTS_PER_BOARD;
	for (i = 0; i < brd->numPorts; i++)
		tty_port_register_device(&brd->ports[i].port, moxaDriver,
				first_idx + i, dev);

	return 0;
err_free:
	for (i = 0; i < MAX_PORTS_PER_BOARD; i++)
		tty_port_destroy(&brd->ports[i].port);
	kfree(brd->ports);
err:
	return ret;
}

static void moxa_board_deinit(struct moxa_board_conf *brd)
{
	unsigned int a, opened, first_idx;

	mutex_lock(&moxa_openlock);
	spin_lock_bh(&moxa_lock);
	brd->ready = 0;
	spin_unlock_bh(&moxa_lock);

	/* pci hot-un-plug support */
	for (a = 0; a < brd->numPorts; a++)
		if (tty_port_initialized(&brd->ports[a].port))
			tty_port_tty_hangup(&brd->ports[a].port, false);

	for (a = 0; a < MAX_PORTS_PER_BOARD; a++)
		tty_port_destroy(&brd->ports[a].port);

	while (1) {
		opened = 0;
		for (a = 0; a < brd->numPorts; a++)
			if (tty_port_initialized(&brd->ports[a].port))
				opened++;
		mutex_unlock(&moxa_openlock);
		if (!opened)
			break;
		msleep(50);
		mutex_lock(&moxa_openlock);
	}

	first_idx = (brd - moxa_boards) * MAX_PORTS_PER_BOARD;
	for (a = 0; a < brd->numPorts; a++)
		tty_unregister_device(moxaDriver, first_idx + a);

	iounmap(brd->basemem);
	brd->basemem = NULL;
	kfree(brd->ports);
}

static int moxa_pci_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	struct moxa_board_conf *board;
	unsigned int i;
	int board_type = ent->driver_data;
	int retval;

	retval = pci_enable_device(pdev);
	if (retval) {
		dev_err(&pdev->dev, "can't enable pci device\n");
		goto err;
	}

	for (i = 0; i < MAX_BOARDS; i++)
		if (moxa_boards[i].basemem == NULL)
			break;

	retval = -ENODEV;
	if (i >= MAX_BOARDS) {
		dev_warn(&pdev->dev, "more than %u MOXA Intellio family boards "
				"found. Board is ignored.\n", MAX_BOARDS);
		goto err;
	}

	board = &moxa_boards[i];

	retval = pci_request_region(pdev, 2, "moxa-base");
	if (retval) {
		dev_err(&pdev->dev, "can't request pci region 2\n");
		goto err;
	}

	board->basemem = ioremap(pci_resource_start(pdev, 2), 0x4000);
	if (board->basemem == NULL) {
		dev_err(&pdev->dev, "can't remap io space 2\n");
		retval = -ENOMEM;
		goto err_reg;
	}

	board->boardType = board_type;
	switch (board_type) {
	case MOXA_BOARD_C218_PCI:
		board->numPorts = 8;
		break;

	case MOXA_BOARD_CP204J:
		board->numPorts = 4;
		break;
	default:
		board->numPorts = 0;
		break;
	}

	retval = moxa_init_board(board, &pdev->dev);
	if (retval)
		goto err_base;

	pci_set_drvdata(pdev, board);

	dev_info(&pdev->dev, "board '%s' ready (%u ports, firmware loaded)\n",
			moxa_brdname[board_type - 1], board->numPorts);

	return 0;
err_base:
	iounmap(board->basemem);
	board->basemem = NULL;
err_reg:
	pci_release_region(pdev, 2);
err:
	return retval;
}

static void moxa_pci_remove(struct pci_dev *pdev)
{
	struct moxa_board_conf *brd = pci_get_drvdata(pdev);

	moxa_board_deinit(brd);

	pci_release_region(pdev, 2);
}

static struct pci_driver moxa_pci_driver = {
	.name = "moxa",
	.id_table = moxa_pcibrds,
	.probe = moxa_pci_probe,
	.remove = moxa_pci_remove
};

static int __init moxa_init(void)
{
	int retval = 0;

	moxaDriver = tty_alloc_driver(MAX_PORTS,
			TTY_DRIVER_REAL_RAW |
			TTY_DRIVER_DYNAMIC_DEV);
	if (IS_ERR(moxaDriver))
		return PTR_ERR(moxaDriver);

	moxaDriver->name = "ttyMX";
	moxaDriver->major = ttymajor;
	moxaDriver->minor_start = 0;
	moxaDriver->type = TTY_DRIVER_TYPE_SERIAL;
	moxaDriver->subtype = SERIAL_TYPE_NORMAL;
	moxaDriver->init_termios = tty_std_termios;
	moxaDriver->init_termios.c_cflag = B9600 | CS8 | CREAD | CLOCAL | HUPCL;
	moxaDriver->init_termios.c_ispeed = 9600;
	moxaDriver->init_termios.c_ospeed = 9600;
	tty_set_operations(moxaDriver, &moxa_ops);

	if (tty_register_driver(moxaDriver)) {
		printk(KERN_ERR "can't register MOXA Smartio tty driver!\n");
		tty_driver_kref_put(moxaDriver);
		return -1;
	}

	retval = pci_register_driver(&moxa_pci_driver);
	if (retval)
		printk(KERN_ERR "Can't register MOXA pci driver!\n");

	return retval;
}

static void __exit moxa_exit(void)
{
	pci_unregister_driver(&moxa_pci_driver);

	timer_delete_sync(&moxaTimer);

	tty_unregister_driver(moxaDriver);
	tty_driver_kref_put(moxaDriver);
}

module_init(moxa_init);
module_exit(moxa_exit);

static void moxa_shutdown(struct tty_port *port)
{
	struct moxa_port *ch = container_of(port, struct moxa_port, port);
        MoxaPortDisable(ch);
	MoxaPortFlushData(ch, 2);
}

static bool moxa_carrier_raised(struct tty_port *port)
{
	struct moxa_port *ch = container_of(port, struct moxa_port, port);
	int dcd;

	spin_lock_irq(&port->lock);
	dcd = ch->DCDState;
	spin_unlock_irq(&port->lock);
	return dcd;
}

static void moxa_dtr_rts(struct tty_port *port, bool active)
{
	struct moxa_port *ch = container_of(port, struct moxa_port, port);
	MoxaPortLineCtrl(ch, active, active);
}


static int moxa_open(struct tty_struct *tty, struct file *filp)
{
	struct moxa_board_conf *brd;
	struct moxa_port *ch;
	int port;

	port = tty->index;
	if (mutex_lock_interruptible(&moxa_openlock))
		return -ERESTARTSYS;
	brd = &moxa_boards[port / MAX_PORTS_PER_BOARD];
	if (!brd->ready) {
		mutex_unlock(&moxa_openlock);
		return -ENODEV;
	}

	if (port % MAX_PORTS_PER_BOARD >= brd->numPorts) {
		mutex_unlock(&moxa_openlock);
		return -ENODEV;
	}

	ch = &brd->ports[port % MAX_PORTS_PER_BOARD];
	ch->port.count++;
	tty->driver_data = ch;
	tty_port_tty_set(&ch->port, tty);
	mutex_lock(&ch->port.mutex);
	if (!tty_port_initialized(&ch->port)) {
		ch->statusflags = 0;
		moxa_set_tty_param(tty, &tty->termios);
		MoxaPortLineCtrl(ch, true, true);
		MoxaPortEnable(ch);
		MoxaSetFifo(ch, ch->type == PORT_16550A);
		tty_port_set_initialized(&ch->port, true);
	}
	mutex_unlock(&ch->port.mutex);
	mutex_unlock(&moxa_openlock);

	return tty_port_block_til_ready(&ch->port, tty, filp);
}

static void moxa_close(struct tty_struct *tty, struct file *filp)
{
	struct moxa_port *ch = tty->driver_data;
	ch->cflag = tty->termios.c_cflag;
	tty_port_close(&ch->port, tty, filp);
}

static ssize_t moxa_write(struct tty_struct *tty, const u8 *buf, size_t count)
{
	struct moxa_port *ch = tty->driver_data;
	unsigned long flags;
	int len;

	if (ch == NULL)
		return 0;

	spin_lock_irqsave(&moxa_lock, flags);
	len = MoxaPortWriteData(tty, buf, count);
	spin_unlock_irqrestore(&moxa_lock, flags);

	set_bit(LOWWAIT, &ch->statusflags);
	return len;
}

static unsigned int moxa_write_room(struct tty_struct *tty)
{
	struct moxa_port *ch;

	if (tty->flow.stopped)
		return 0;
	ch = tty->driver_data;
	if (ch == NULL)
		return 0;
	return MoxaPortTxFree(ch);
}

static void moxa_flush_buffer(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;

	if (ch == NULL)
		return;
	MoxaPortFlushData(ch, 1);
	tty_wakeup(tty);
}

static unsigned int moxa_chars_in_buffer(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;
	unsigned int chars;

	chars = MoxaPortTxQueue(ch);
	if (chars)
		/*
		 * Make it possible to wakeup anything waiting for output
		 * in tty_ioctl.c, etc.
		 */
        	set_bit(EMPTYWAIT, &ch->statusflags);
	return chars;
}

static int moxa_tiocmget(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;
	bool dtr_active, rts_active;
	int flag = 0;
	int status;

	MoxaPortGetLineOut(ch, &dtr_active, &rts_active);
	if (dtr_active)
		flag |= TIOCM_DTR;
	if (rts_active)
		flag |= TIOCM_RTS;
	status = MoxaPortLineStatus(ch);
	if (status & 1)
		flag |= TIOCM_CTS;
	if (status & 2)
		flag |= TIOCM_DSR;
	if (status & 4)
		flag |= TIOCM_CD;
	return flag;
}

static int moxa_tiocmset(struct tty_struct *tty,
			 unsigned int set, unsigned int clear)
{
	bool dtr_active, rts_active;
	struct moxa_port *ch;

	mutex_lock(&moxa_openlock);
	ch = tty->driver_data;
	if (!ch) {
		mutex_unlock(&moxa_openlock);
		return -EINVAL;
	}

	MoxaPortGetLineOut(ch, &dtr_active, &rts_active);
	if (set & TIOCM_RTS)
		rts_active = true;
	if (set & TIOCM_DTR)
		dtr_active = true;
	if (clear & TIOCM_RTS)
		rts_active = false;
	if (clear & TIOCM_DTR)
		dtr_active = false;
	MoxaPortLineCtrl(ch, dtr_active, rts_active);
	mutex_unlock(&moxa_openlock);
	return 0;
}

static void moxa_set_termios(struct tty_struct *tty,
		             const struct ktermios *old_termios)
{
	struct moxa_port *ch = tty->driver_data;

	if (ch == NULL)
		return;
	moxa_set_tty_param(tty, old_termios);
	if (!(old_termios->c_cflag & CLOCAL) && C_CLOCAL(tty))
		wake_up_interruptible(&ch->port.open_wait);
}

static void moxa_stop(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;

	if (ch == NULL)
		return;
	MoxaPortTxDisable(ch);
	set_bit(TXSTOPPED, &ch->statusflags);
}


static void moxa_start(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;

	if (ch == NULL)
		return;

	if (!test_bit(TXSTOPPED, &ch->statusflags))
		return;

	MoxaPortTxEnable(ch);
	clear_bit(TXSTOPPED, &ch->statusflags);
}

static void moxa_hangup(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;
	tty_port_hangup(&ch->port);
}

static void moxa_new_dcdstate(struct moxa_port *p, u8 dcd)
{
	unsigned long flags;
	dcd = !!dcd;

	spin_lock_irqsave(&p->port.lock, flags);
	if (dcd != p->DCDState) {
        	p->DCDState = dcd;
        	spin_unlock_irqrestore(&p->port.lock, flags);
		if (!dcd)
			tty_port_tty_hangup(&p->port, true);
	}
	else
		spin_unlock_irqrestore(&p->port.lock, flags);
}

static int moxa_poll_port(struct moxa_port *p, unsigned int handle,
		u16 __iomem *ip)
{
	struct tty_struct *tty = tty_port_tty_get(&p->port);
	bool inited = tty_port_initialized(&p->port);
	void __iomem *ofsAddr;
	u16 intr;

	if (tty) {
		if (test_bit(EMPTYWAIT, &p->statusflags) &&
				MoxaPortTxQueue(p) == 0) {
			clear_bit(EMPTYWAIT, &p->statusflags);
			tty_wakeup(tty);
		}
		if (test_bit(LOWWAIT, &p->statusflags) && !tty->flow.stopped &&
				MoxaPortTxQueue(p) <= WAKEUP_CHARS) {
			clear_bit(LOWWAIT, &p->statusflags);
			tty_wakeup(tty);
		}

		if (inited && !tty_throttled(tty) &&
				MoxaPortRxQueue(p) > 0) { /* RX */
			MoxaPortReadData(p);
			tty_flip_buffer_push(&p->port);
		}
	} else {
		clear_bit(EMPTYWAIT, &p->statusflags);
		MoxaPortFlushData(p, 0); /* flush RX */
	}

	if (!handle) /* nothing else to do */
		goto put;

	intr = readw(ip); /* port irq status */
	if (intr == 0)
		goto put;

	writew(0, ip); /* ACK port */
	ofsAddr = p->tableAddr;
	if (intr & IntrTx) /* disable tx intr */
		writew(readw(ofsAddr + HostStat) & ~WakeupTx,
				ofsAddr + HostStat);

	if (!inited)
		goto put;

	if (tty && (intr & IntrBreak) && !I_IGNBRK(tty)) { /* BREAK */
		tty_insert_flip_char(&p->port, 0, TTY_BREAK);
		tty_flip_buffer_push(&p->port);
	}

	if (intr & IntrLine)
		moxa_new_dcdstate(p, readb(ofsAddr + FlagStat) & DCD_state);
put:
	tty_kref_put(tty);

	return 0;
}

static void moxa_poll(struct timer_list *unused)
{
	struct moxa_board_conf *brd;
	u16 __iomem *ip;
	unsigned int card, port, served = 0;

	spin_lock(&moxa_lock);
	for (card = 0; card < MAX_BOARDS; card++) {
		brd = &moxa_boards[card];
		if (!brd->ready)
			continue;

		served++;

		ip = NULL;
		if (readb(brd->intPend) == 0xff)
			ip = brd->intTable + readb(brd->intNdx);

		for (port = 0; port < brd->numPorts; port++)
			moxa_poll_port(&brd->ports[port], !!ip, ip + port);

		if (ip)
			writeb(0, brd->intPend); /* ACK */

		if (moxaLowWaterChk) {
			struct moxa_port *p = brd->ports;
			for (port = 0; port < brd->numPorts; port++, p++)
				if (p->lowChkFlag) {
					p->lowChkFlag = 0;
					moxa_low_water_check(p->tableAddr);
				}
		}
	}
	moxaLowWaterChk = 0;

	if (served)
		mod_timer(&moxaTimer, jiffies + HZ / 50);
	spin_unlock(&moxa_lock);
}

/******************************************************************************/

static void moxa_set_tty_param(struct tty_struct *tty,
			       const struct ktermios *old_termios)
{
	register struct ktermios *ts = &tty->termios;
	struct moxa_port *ch = tty->driver_data;
	int rts, cts, txflow, rxflow, xany, baud;

	rts = cts = txflow = rxflow = xany = 0;
	if (ts->c_cflag & CRTSCTS)
		rts = cts = 1;
	if (ts->c_iflag & IXON)
		txflow = 1;
	if (ts->c_iflag & IXOFF)
		rxflow = 1;
	if (ts->c_iflag & IXANY)
		xany = 1;

	MoxaPortFlowCtrl(ch, rts, cts, txflow, rxflow, xany);
	baud = MoxaPortSetTermio(ch, ts, tty_get_baud_rate(tty));
	if (baud == -1)
		baud = tty_termios_baud_rate(old_termios);
	/* Not put the baud rate into the termios data */
	tty_encode_baud_rate(tty, baud, baud);
}

/*****************************************************************************
 *	Driver level functions: 					     *
 *****************************************************************************/

static void MoxaPortFlushData(struct moxa_port *port, int mode)
{
	void __iomem *ofsAddr;
	if (mode < 0 || mode > 2)
		return;
	ofsAddr = port->tableAddr;
	moxafunc(ofsAddr, FC_FlushQueue, mode);
	if (mode != 1) {
		port->lowChkFlag = 0;
		moxa_low_water_check(ofsAddr);
	}
}

/*
 *    Moxa Port Number Description:
 *
 *      MOXA serial driver supports up to 4 MOXA-C218/C320 boards. And,
 *      the port number using in MOXA driver functions will be 0 to 31 for
 *      first MOXA board, 32 to 63 for second, 64 to 95 for third and 96
 *      to 127 for fourth. For example, if you setup three MOXA boards,
 *      first board is C218, second board is C320-16 and third board is
 *      C320-32. The port number of first board (C218 - 8 ports) is from
 *      0 to 7. The port number of second board (C320 - 16 ports) is form
 *      32 to 47. The port number of third board (C320 - 32 ports) is from
 *      64 to 95. And those port numbers form 8 to 31, 48 to 63 and 96 to
 *      127 will be invalid.
 *
 *
 *      Moxa Functions Description:
 *
 *      Function 1:     Driver initialization routine, this routine must be
 *                      called when initialized driver.
 *      Syntax:
 *      void MoxaDriverInit();
 *
 *
 *      Function 2:     Moxa driver private IOCTL command processing.
 *      Syntax:
 *      int  MoxaDriverIoctl(unsigned int cmd, unsigned long arg, int port);
 *
 *           unsigned int cmd   : IOCTL command
 *           unsigned long arg  : IOCTL argument
 *           int port           : port number (0 - 127)
 *
 *           return:    0  (OK)
 *                      -EINVAL
 *                      -ENOIOCTLCMD
 *
 *
 *      Function 6:     Enable this port to start Tx/Rx data.
 *      Syntax:
 *      void MoxaPortEnable(int port);
 *           int port           : port number (0 - 127)
 *
 *
 *      Function 7:     Disable this port
 *      Syntax:
 *      void MoxaPortDisable(int port);
 *           int port           : port number (0 - 127)
 *
 *
 *      Function 10:    Setting baud rate of this port.
 *      Syntax:
 *      speed_t MoxaPortSetBaud(int port, speed_t baud);
 *           int port           : port number (0 - 127)
 *           long baud          : baud rate (50 - 115200)
 *
 *           return:    0       : this port is invalid or baud < 50
 *                      50 - 115200 : the real baud rate set to the port, if
 *                                    the argument baud is large than maximun
 *                                    available baud rate, the real setting
 *                                    baud rate will be the maximun baud rate.
 *
 *
 *      Function 12:    Configure the port.
 *      Syntax:
 *      int  MoxaPortSetTermio(int port, struct ktermios *termio, speed_t baud);
 *           int port           : port number (0 - 127)
 *           struct ktermios * termio : termio structure pointer
 *	     speed_t baud	: baud rate
 *
 *           return:    -1      : this port is invalid or termio == NULL
 *                      0       : setting O.K.
 *
 *
 *      Function 13:    Get the DTR/RTS state of this port.
 *      Syntax:
 *      int  MoxaPortGetLineOut(int port, bool *dtrState, bool *rtsState);
 *           int port           : port number (0 - 127)
 *           bool * dtr_active  : pointer to bool to receive the current DTR
 *                                state. (if NULL, this function will not
 *                                write to this address)
 *           bool * rts_active  : pointer to bool to receive the current RTS
 *                                state. (if NULL, this function will not
 *                                write to this address)
 *
 *           return:    -1      : this port is invalid
 *                      0       : O.K.
 *
 *
 *      Function 14:    Setting the DTR/RTS output state of this port.
 *      Syntax:
 *      void MoxaPortLineCtrl(int port, bool dtrState, bool rtsState);
 *           int port           : port number (0 - 127)
 *           bool dtr_active    : DTR output state
 *           bool rts_active    : RTS output state
 *
 *
 *      Function 15:    Setting the flow control of this port.
 *      Syntax:
 *      void MoxaPortFlowCtrl(int port, int rtsFlow, int ctsFlow, int rxFlow,
 *                            int txFlow,int xany);
 *           int port           : port number (0 - 127)
 *           int rtsFlow        : H/W RTS flow control (0: no, 1: yes)
 *           int ctsFlow        : H/W CTS flow control (0: no, 1: yes)
 *           int rxFlow         : S/W Rx XON/XOFF flow control (0: no, 1: yes)
 *           int txFlow         : S/W Tx XON/XOFF flow control (0: no, 1: yes)
 *           int xany           : S/W XANY flow control (0: no, 1: yes)
 *
 *
 *      Function 16:    Get ths line status of this port
 *      Syntax:
 *      int  MoxaPortLineStatus(int port);
 *           int port           : port number (0 - 127)
 *
 *           return:    Bit 0 - CTS state (0: off, 1: on)
 *                      Bit 1 - DSR state (0: off, 1: on)
 *                      Bit 2 - DCD state (0: off, 1: on)
 *
 *
 *      Function 19:    Flush the Rx/Tx buffer data of this port.
 *      Syntax:
 *      void MoxaPortFlushData(int port, int mode);
 *           int port           : port number (0 - 127)
 *           int mode    
 *                      0       : flush the Rx buffer 
 *                      1       : flush the Tx buffer 
 *                      2       : flush the Rx and Tx buffer 
 *
 *
 *      Function 20:    Write data.
 *      Syntax:
 *      ssize_t  MoxaPortWriteData(int port, u8 *buffer, size_t length);
 *           int port           : port number (0 - 127)
 *           u8 *buffer         : pointer to write data buffer.
 *           size_t length      : write data length
 *
 *           return:    0 - length      : real write data length
 *
 *
 *      Function 21:    Read data.
 *      Syntax:
 *      int  MoxaPortReadData(int port, struct tty_struct *tty);
 *           int port           : port number (0 - 127)
 *	     struct tty_struct *tty : tty for data
 *
 *           return:    0 - length      : real read data length
 *
 *
 *      Function 24:    Get the Tx buffer current queued data bytes
 *      Syntax:
 *      int  MoxaPortTxQueue(int port);
 *           int port           : port number (0 - 127)
 *
 *           return:    ..      : Tx buffer current queued data bytes
 *
 *
 *      Function 25:    Get the Tx buffer current free space
 *      Syntax:
 *      int  MoxaPortTxFree(int port);
 *           int port           : port number (0 - 127)
 *
 *           return:    ..      : Tx buffer current free space
 *
 *
 *      Function 26:    Get the Rx buffer current queued data bytes
 *      Syntax:
 *      int  MoxaPortRxQueue(int port);
 *           int port           : port number (0 - 127)
 *
 *           return:    ..      : Rx buffer current queued data bytes
 *
 *
 *      Function 28:    Disable port data transmission.
 *      Syntax:
 *      void MoxaPortTxDisable(int port);
 *           int port           : port number (0 - 127)
 *
 *
 *      Function 29:    Enable port data transmission.
 *      Syntax:
 *      void MoxaPortTxEnable(int port);
 *           int port           : port number (0 - 127)
 *
 *
 *      Function 31:    Get the received BREAK signal count and reset it.
 *      Syntax:
 *      int  MoxaPortResetBrkCnt(int port);
 *           int port           : port number (0 - 127)
 *
 *           return:    0 - ..  : BREAK signal count
 *
 *
 */

static void MoxaPortEnable(struct moxa_port *port)
{
	void __iomem *ofsAddr;
	u16 lowwater = 512;

	ofsAddr = port->tableAddr;
	writew(lowwater, ofsAddr + Low_water);
	if (MOXA_IS_320(port->board))
		moxafunc(ofsAddr, FC_SetBreakIrq, 0);
	else
		writew(readw(ofsAddr + HostStat) | WakeupBreak,
				ofsAddr + HostStat);

	moxafunc(ofsAddr, FC_SetLineIrq, Magic_code);
	moxafunc(ofsAddr, FC_FlushQueue, 2);

	moxafunc(ofsAddr, FC_EnableCH, Magic_code);
	MoxaPortLineStatus(port);
}

static void MoxaPortDisable(struct moxa_port *port)
{
	void __iomem *ofsAddr = port->tableAddr;

	moxafunc(ofsAddr, FC_SetFlowCtl, 0);	/* disable flow control */
	moxafunc(ofsAddr, FC_ClrLineIrq, Magic_code);
	writew(0, ofsAddr + HostStat);
	moxafunc(ofsAddr, FC_DisableCH, Magic_code);
}

static speed_t MoxaPortSetBaud(struct moxa_port *port, speed_t baud)
{
	void __iomem *ofsAddr = port->tableAddr;
	unsigned int clock, val;
	speed_t max;

	max = MOXA_IS_320(port->board) ? 460800 : 921600;
	if (baud < 50)
		return 0;
	if (baud > max)
		baud = max;
	clock = 921600;
	val = clock / baud;
	moxafunc(ofsAddr, FC_SetBaud, val);
	baud = clock / val;
	return baud;
}

static int MoxaPortSetTermio(struct moxa_port *port, struct ktermios *termio,
		speed_t baud)
{
	void __iomem *ofsAddr;
	tcflag_t mode = 0;

	ofsAddr = port->tableAddr;

	mode = termio->c_cflag & CSIZE;
	if (mode == CS5)
		mode = MX_CS5;
	else if (mode == CS6)
		mode = MX_CS6;
	else if (mode == CS7)
		mode = MX_CS7;
	else if (mode == CS8)
		mode = MX_CS8;

	if (termio->c_cflag & CSTOPB) {
		if (mode == MX_CS5)
			mode |= MX_STOP15;
		else
			mode |= MX_STOP2;
	} else
		mode |= MX_STOP1;

	if (termio->c_cflag & PARENB) {
		if (termio->c_cflag & PARODD) {
			if (termio->c_cflag & CMSPAR)
				mode |= MX_PARMARK;
			else
				mode |= MX_PARODD;
		} else {
			if (termio->c_cflag & CMSPAR)
				mode |= MX_PARSPACE;
			else
				mode |= MX_PAREVEN;
		}
	} else
		mode |= MX_PARNONE;

	moxafunc(ofsAddr, FC_SetDataMode, (u16)mode);

	if (MOXA_IS_320(port->board) && baud >= 921600)
		return -1;

	baud = MoxaPortSetBaud(port, baud);

	if (termio->c_iflag & (IXON | IXOFF | IXANY)) {
	        spin_lock_irq(&moxafunc_lock);
		writeb(termio->c_cc[VSTART], ofsAddr + FuncArg);
		writeb(termio->c_cc[VSTOP], ofsAddr + FuncArg1);
		writeb(FC_SetXonXoff, ofsAddr + FuncCode);
		moxa_wait_finish(ofsAddr);
		spin_unlock_irq(&moxafunc_lock);

	}
	return baud;
}

static int MoxaPortGetLineOut(struct moxa_port *port, bool *dtr_active,
		bool *rts_active)
{
	if (dtr_active)
		*dtr_active = port->lineCtrl & DTR_ON;
	if (rts_active)
		*rts_active = port->lineCtrl & RTS_ON;

	return 0;
}

static void MoxaPortLineCtrl(struct moxa_port *port, bool dtr_active, bool rts_active)
{
	u8 mode = 0;

	if (dtr_active)
		mode |= DTR_ON;
	if (rts_active)
		mode |= RTS_ON;
	port->lineCtrl = mode;
	moxafunc(port->tableAddr, FC_LineControl, mode);
}

static void MoxaPortFlowCtrl(struct moxa_port *port, int rts, int cts,
		int txflow, int rxflow, int txany)
{
	int mode = 0;

	if (rts)
		mode |= RTS_FlowCtl;
	if (cts)
		mode |= CTS_FlowCtl;
	if (txflow)
		mode |= Tx_FlowCtl;
	if (rxflow)
		mode |= Rx_FlowCtl;
	if (txany)
		mode |= IXM_IXANY;
	moxafunc(port->tableAddr, FC_SetFlowCtl, mode);
}

static int MoxaPortLineStatus(struct moxa_port *port)
{
	void __iomem *ofsAddr;
	int val;

	ofsAddr = port->tableAddr;
	if (MOXA_IS_320(port->board))
		val = moxafuncret(ofsAddr, FC_LineStatus, 0);
	else
		val = readw(ofsAddr + FlagStat) >> 4;
	val &= 0x0B;
	if (val & 8)
		val |= 4;
	moxa_new_dcdstate(port, val & 8);
	val &= 7;
	return val;
}

static ssize_t MoxaPortWriteData(struct tty_struct *tty, const u8 *buffer,
				 size_t len)
{
	struct moxa_port *port = tty->driver_data;
	void __iomem *baseAddr, *ofsAddr, *ofs;
	size_t c, total;
	u16 head, tail, tx_mask, spage, epage;
	u16 pageno, pageofs, bufhead;

	ofsAddr = port->tableAddr;
	baseAddr = port->board->basemem;
	tx_mask = readw(ofsAddr + TX_mask);
	spage = readw(ofsAddr + Page_txb);
	epage = readw(ofsAddr + EndPage_txb);
	tail = readw(ofsAddr + TXwptr);
	head = readw(ofsAddr + TXrptr);
	c = (head > tail) ? (head - tail - 1) : (head - tail + tx_mask);
	if (c > len)
		c = len;
	total = c;
	if (spage == epage) {
		bufhead = readw(ofsAddr + Ofs_txb);
		writew(spage, baseAddr + Control_reg);
		while (c > 0) {
			if (head > tail)
				len = head - tail - 1;
			else
				len = tx_mask + 1 - tail;
			len = (c > len) ? len : c;
			ofs = baseAddr + DynPage_addr + bufhead + tail;
			memcpy_toio(ofs, buffer, len);
			buffer += len;
			tail = (tail + len) & tx_mask;
			c -= len;
		}
	} else {
		pageno = spage + (tail >> 13);
		pageofs = tail & Page_mask;
		while (c > 0) {
			len = Page_size - pageofs;
			if (len > c)
				len = c;
			writeb(pageno, baseAddr + Control_reg);
			ofs = baseAddr + DynPage_addr + pageofs;
			memcpy_toio(ofs, buffer, len);
			buffer += len;
			if (++pageno == epage)
				pageno = spage;
			pageofs = 0;
			c -= len;
		}
		tail = (tail + total) & tx_mask;
	}
	writew(tail, ofsAddr + TXwptr);
	writeb(1, ofsAddr + CD180TXirq);	/* start to send */
	return total;
}

static int MoxaPortReadData(struct moxa_port *port)
{
	void __iomem *baseAddr, *ofsAddr, *ofs;
	u8 *dst;
	unsigned int count, len, total;
	u16 tail, rx_mask, spage, epage;
	u16 pageno, pageofs, bufhead, head;

	ofsAddr = port->tableAddr;
	baseAddr = port->board->basemem;
	head = readw(ofsAddr + RXrptr);
	tail = readw(ofsAddr + RXwptr);
	rx_mask = readw(ofsAddr + RX_mask);
	spage = readw(ofsAddr + Page_rxb);
	epage = readw(ofsAddr + EndPage_rxb);
	count = (tail >= head) ? (tail - head) : (tail - head + rx_mask + 1);
	if (count == 0)
		return 0;

	total = count;
	if (spage == epage) {
		bufhead = readw(ofsAddr + Ofs_rxb);
		writew(spage, baseAddr + Control_reg);
		while (count > 0) {
			ofs = baseAddr + DynPage_addr + bufhead + head;
			len = (tail >= head) ? (tail - head) :
					(rx_mask + 1 - head);
			len = tty_prepare_flip_string(&port->port, &dst,
					min(len, count));
			memcpy_fromio(dst, ofs, len);
			head = (head + len) & rx_mask;
			count -= len;
		}
	} else {
		pageno = spage + (head >> 13);
		pageofs = head & Page_mask;
		while (count > 0) {
			writew(pageno, baseAddr + Control_reg);
			ofs = baseAddr + DynPage_addr + pageofs;
			len = tty_prepare_flip_string(&port->port, &dst,
					min(Page_size - pageofs, count));
			memcpy_fromio(dst, ofs, len);

			count -= len;
			pageofs = (pageofs + len) & Page_mask;
			if (pageofs == 0 && ++pageno == epage)
				pageno = spage;
		}
		head = (head + total) & rx_mask;
	}
	writew(head, ofsAddr + RXrptr);
	if (readb(ofsAddr + FlagStat) & Xoff_state) {
		moxaLowWaterChk = 1;
		port->lowChkFlag = 1;
	}
	return total;
}


static unsigned int MoxaPortTxQueue(struct moxa_port *port)
{
	void __iomem *ofsAddr = port->tableAddr;
	u16 rptr, wptr, mask;

	rptr = readw(ofsAddr + TXrptr);
	wptr = readw(ofsAddr + TXwptr);
	mask = readw(ofsAddr + TX_mask);
	return (wptr - rptr) & mask;
}

static unsigned int MoxaPortTxFree(struct moxa_port *port)
{
	void __iomem *ofsAddr = port->tableAddr;
	u16 rptr, wptr, mask;

	rptr = readw(ofsAddr + TXrptr);
	wptr = readw(ofsAddr + TXwptr);
	mask = readw(ofsAddr + TX_mask);
	return mask - ((wptr - rptr) & mask);
}

static int MoxaPortRxQueue(struct moxa_port *port)
{
	void __iomem *ofsAddr = port->tableAddr;
	u16 rptr, wptr, mask;

	rptr = readw(ofsAddr + RXrptr);
	wptr = readw(ofsAddr + RXwptr);
	mask = readw(ofsAddr + RX_mask);
	return (wptr - rptr) & mask;
}

static void MoxaPortTxDisable(struct moxa_port *port)
{
	moxafunc(port->tableAddr, FC_SetXoffState, Magic_code);
}

static void MoxaPortTxEnable(struct moxa_port *port)
{
	moxafunc(port->tableAddr, FC_SetXonState, Magic_code);
}

static int moxa_get_serial_info(struct tty_struct *tty,
		struct serial_struct *ss)
{
	struct moxa_port *info = tty->driver_data;

	if (!info)
		return -ENODEV;
	mutex_lock(&info->port.mutex);
	ss->type = info->type;
	ss->line = info->port.tty->index;
	ss->flags = info->port.flags;
	ss->baud_base = 921600;
	ss->close_delay = jiffies_to_msecs(info->port.close_delay) / 10;
	mutex_unlock(&info->port.mutex);
	return 0;
}


static int moxa_set_serial_info(struct tty_struct *tty,
		struct serial_struct *ss)
{
	struct moxa_port *info = tty->driver_data;
	unsigned int close_delay;

	if (!info)
		return -ENODEV;

	close_delay = msecs_to_jiffies(ss->close_delay * 10);

	mutex_lock(&info->port.mutex);
	if (!capable(CAP_SYS_ADMIN)) {
		if (close_delay != info->port.close_delay ||
		    ss->type != info->type ||
		    ((ss->flags & ~ASYNC_USR_MASK) !=
		     (info->port.flags & ~ASYNC_USR_MASK))) {
			mutex_unlock(&info->port.mutex);
			return -EPERM;
		}
	} else {
		info->port.close_delay = close_delay;

		MoxaSetFifo(info, ss->type == PORT_16550A);

		info->type = ss->type;
	}
	mutex_unlock(&info->port.mutex);
	return 0;
}



/*****************************************************************************
 *	Static local functions: 					     *
 *****************************************************************************/

static void MoxaSetFifo(struct moxa_port *port, int enable)
{
	void __iomem *ofsAddr = port->tableAddr;

	if (!enable) {
		moxafunc(ofsAddr, FC_SetRxFIFOTrig, 0);
		moxafunc(ofsAddr, FC_SetTxFIFOCnt, 1);
	} else {
		moxafunc(ofsAddr, FC_SetRxFIFOTrig, 3);
		moxafunc(ofsAddr, FC_SetTxFIFOCnt, 16);
	}
}

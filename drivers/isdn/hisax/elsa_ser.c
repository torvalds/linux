/* $Id: elsa_ser.c,v 2.14.2.3 2004/02/11 13:21:33 keil Exp $
 *
 * stuff for the serial modem on ELSA cards
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>

#define MAX_MODEM_BUF	256
#define WAKEUP_CHARS	(MAX_MODEM_BUF/2)
#define RS_ISR_PASS_LIMIT 256
#define BASE_BAUD ( 1843200 / 16 )

//#define SERIAL_DEBUG_OPEN 1
//#define SERIAL_DEBUG_INTR 1
//#define SERIAL_DEBUG_FLOW 1
#undef SERIAL_DEBUG_OPEN
#undef SERIAL_DEBUG_INTR
#undef SERIAL_DEBUG_FLOW
#undef SERIAL_DEBUG_REG
//#define SERIAL_DEBUG_REG 1

#ifdef SERIAL_DEBUG_REG
static u_char deb[32];
const char *ModemIn[] = {"RBR","IER","IIR","LCR","MCR","LSR","MSR","SCR"};
const char *ModemOut[] = {"THR","IER","FCR","LCR","MCR","LSR","MSR","SCR"};
#endif

static char *MInit_1 = "AT&F&C1E0&D2\r\0";
static char *MInit_2 = "ATL2M1S64=13\r\0";
static char *MInit_3 = "AT+FCLASS=0\r\0";
static char *MInit_4 = "ATV1S2=128X1\r\0";
static char *MInit_5 = "AT\\V8\\N3\r\0";
static char *MInit_6 = "ATL0M0&G0%E1\r\0";
static char *MInit_7 = "AT%L1%M0%C3\r\0";

static char *MInit_speed28800 = "AT%G0%B28800\r\0";

static char *MInit_dialout = "ATs7=60 x1 d\r\0";
static char *MInit_dialin = "ATs7=60 x1 a\r\0";


static inline unsigned int serial_in(struct IsdnCardState *cs, int offset)
{
#ifdef SERIAL_DEBUG_REG
	u_int val = inb(cs->hw.elsa.base + 8 + offset);
	debugl1(cs,"in   %s %02x",ModemIn[offset], val);
	return(val);
#else
	return inb(cs->hw.elsa.base + 8 + offset);
#endif
}

static inline unsigned int serial_inp(struct IsdnCardState *cs, int offset)
{
#ifdef SERIAL_DEBUG_REG
#ifdef ELSA_SERIAL_NOPAUSE_IO
	u_int val = inb(cs->hw.elsa.base + 8 + offset);
	debugl1(cs,"inp  %s %02x",ModemIn[offset], val);
#else
	u_int val = inb_p(cs->hw.elsa.base + 8 + offset);
	debugl1(cs,"inP  %s %02x",ModemIn[offset], val);
#endif
	return(val);
#else
#ifdef ELSA_SERIAL_NOPAUSE_IO
	return inb(cs->hw.elsa.base + 8 + offset);
#else
	return inb_p(cs->hw.elsa.base + 8 + offset);
#endif
#endif
}

static inline void serial_out(struct IsdnCardState *cs, int offset, int value)
{
#ifdef SERIAL_DEBUG_REG
	debugl1(cs,"out  %s %02x",ModemOut[offset], value);
#endif
	outb(value, cs->hw.elsa.base + 8 + offset);
}

static inline void serial_outp(struct IsdnCardState *cs, int offset,
			       int value)
{
#ifdef SERIAL_DEBUG_REG
#ifdef ELSA_SERIAL_NOPAUSE_IO
	debugl1(cs,"outp %s %02x",ModemOut[offset], value);
#else
	debugl1(cs,"outP %s %02x",ModemOut[offset], value);
#endif
#endif
#ifdef ELSA_SERIAL_NOPAUSE_IO
	outb(value, cs->hw.elsa.base + 8 + offset);
#else
    	outb_p(value, cs->hw.elsa.base + 8 + offset);
#endif
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void change_speed(struct IsdnCardState *cs, int baud)
{
	int	quot = 0, baud_base;
	unsigned cval, fcr = 0;
	int	bits;


	/* byte size and parity */
	cval = 0x03; bits = 10;
	/* Determine divisor based on baud rate */
	baud_base = BASE_BAUD;
	quot = baud_base / baud;
	/* If the quotient is ever zero, default to 9600 bps */
	if (!quot)
		quot = baud_base / 9600;

	/* Set up FIFO's */
	if ((baud_base / quot) < 2400)
		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_1;
	else
		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_8;
	serial_outp(cs, UART_FCR, fcr);
	/* CTS flow control flag and modem status interrupts */
	cs->hw.elsa.IER &= ~UART_IER_MSI;
	cs->hw.elsa.IER |= UART_IER_MSI;
	serial_outp(cs, UART_IER, cs->hw.elsa.IER);

	debugl1(cs,"modem quot=0x%x", quot);
	serial_outp(cs, UART_LCR, cval | UART_LCR_DLAB);/* set DLAB */
	serial_outp(cs, UART_DLL, quot & 0xff);		/* LS of divisor */
	serial_outp(cs, UART_DLM, quot >> 8);		/* MS of divisor */
	serial_outp(cs, UART_LCR, cval);		/* reset DLAB */
	serial_inp(cs, UART_RX);
}

static int mstartup(struct IsdnCardState *cs)
{
	int	retval=0;

	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in change_speed())
	 */
	serial_outp(cs, UART_FCR, (UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT));

	/*
	 * At this point there's no way the LSR could still be 0xFF;
	 * if it is, then bail out, because there's likely no UART
	 * here.
	 */
	if (serial_inp(cs, UART_LSR) == 0xff) {
		retval = -ENODEV;
		goto errout;
	}
	
	/*
	 * Clear the interrupt registers.
	 */
	(void) serial_inp(cs, UART_RX);
	(void) serial_inp(cs, UART_IIR);
	(void) serial_inp(cs, UART_MSR);

	/*
	 * Now, initialize the UART 
	 */
	serial_outp(cs, UART_LCR, UART_LCR_WLEN8);	/* reset DLAB */

	cs->hw.elsa.MCR = 0;
	cs->hw.elsa.MCR = UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2;
	serial_outp(cs, UART_MCR, cs->hw.elsa.MCR);
	
	/*
	 * Finally, enable interrupts
	 */
	cs->hw.elsa.IER = UART_IER_MSI | UART_IER_RLSI | UART_IER_RDI;
	serial_outp(cs, UART_IER, cs->hw.elsa.IER);	/* enable interrupts */
	
	/*
	 * And clear the interrupt registers again for luck.
	 */
	(void)serial_inp(cs, UART_LSR);
	(void)serial_inp(cs, UART_RX);
	(void)serial_inp(cs, UART_IIR);
	(void)serial_inp(cs, UART_MSR);

	cs->hw.elsa.transcnt = cs->hw.elsa.transp = 0;
	cs->hw.elsa.rcvcnt = cs->hw.elsa.rcvp =0;

	/*
	 * and set the speed of the serial port
	 */
	change_speed(cs, BASE_BAUD);
	cs->hw.elsa.MFlag = 1;
errout:
	return retval;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void mshutdown(struct IsdnCardState *cs)
{

#ifdef SERIAL_DEBUG_OPEN
	printk(KERN_DEBUG"Shutting down serial ....");
#endif
	
	/*
	 * clear delta_msr_wait queue to avoid mem leaks: we may free the irq
	 * here so the queue might never be waken up
	 */

	cs->hw.elsa.IER = 0;
	serial_outp(cs, UART_IER, 0x00);	/* disable all intrs */
	cs->hw.elsa.MCR &= ~UART_MCR_OUT2;
	
	/* disable break condition */
	serial_outp(cs, UART_LCR, serial_inp(cs, UART_LCR) & ~UART_LCR_SBC);
	
	cs->hw.elsa.MCR &= ~(UART_MCR_DTR|UART_MCR_RTS);
	serial_outp(cs, UART_MCR, cs->hw.elsa.MCR);

	/* disable FIFO's */	
	serial_outp(cs, UART_FCR, (UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT));
	serial_inp(cs, UART_RX);    /* read data port to reset things */
	
#ifdef SERIAL_DEBUG_OPEN
	printk(" done\n");
#endif
}

static inline int
write_modem(struct BCState *bcs) {
	int ret=0;
	struct IsdnCardState *cs = bcs->cs;
	int count, len, fp;
	
	if (!bcs->tx_skb)
		return 0;
	if (bcs->tx_skb->len <= 0)
		return 0;
	len = bcs->tx_skb->len;
	if (len > MAX_MODEM_BUF - cs->hw.elsa.transcnt)
		len = MAX_MODEM_BUF - cs->hw.elsa.transcnt;
	fp = cs->hw.elsa.transcnt + cs->hw.elsa.transp;
	fp &= (MAX_MODEM_BUF -1);
	count = len;
	if (count > MAX_MODEM_BUF - fp) {
		count = MAX_MODEM_BUF - fp;
		skb_copy_from_linear_data(bcs->tx_skb,
					  cs->hw.elsa.transbuf + fp, count);
		skb_pull(bcs->tx_skb, count);
		cs->hw.elsa.transcnt += count;
		ret = count;
		count = len - count;
		fp = 0;
	}
	skb_copy_from_linear_data(bcs->tx_skb,
				  cs->hw.elsa.transbuf + fp, count);
	skb_pull(bcs->tx_skb, count);
	cs->hw.elsa.transcnt += count;
	ret += count;
	
	if (cs->hw.elsa.transcnt && 
	    !(cs->hw.elsa.IER & UART_IER_THRI)) {
			cs->hw.elsa.IER |= UART_IER_THRI;
		serial_outp(cs, UART_IER, cs->hw.elsa.IER);
	}
	return(ret);
}

static inline void
modem_fill(struct BCState *bcs) {
		
	if (bcs->tx_skb) {
		if (bcs->tx_skb->len) {
			write_modem(bcs);
			return;
		} else {
			if (test_bit(FLG_LLI_L1WAKEUP,&bcs->st->lli.flag) &&
				(PACKET_NOACK != bcs->tx_skb->pkt_type)) {
				u_long	flags;
				spin_lock_irqsave(&bcs->aclock, flags);
				bcs->ackcnt += bcs->hw.hscx.count;
				spin_unlock_irqrestore(&bcs->aclock, flags);
				schedule_event(bcs, B_ACKPENDING);
			}
			dev_kfree_skb_any(bcs->tx_skb);
			bcs->tx_skb = NULL;
		}
	}
	if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
		bcs->hw.hscx.count = 0;
		test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
		write_modem(bcs);
	} else {
		test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		schedule_event(bcs, B_XMTBUFREADY);
	}
}

static inline void receive_chars(struct IsdnCardState *cs,
				 int *status)
{
	unsigned char ch;
	struct sk_buff *skb;

	do {
		ch = serial_in(cs, UART_RX);
		if (cs->hw.elsa.rcvcnt >= MAX_MODEM_BUF)
			break;
		cs->hw.elsa.rcvbuf[cs->hw.elsa.rcvcnt++] = ch;
#ifdef SERIAL_DEBUG_INTR
		printk("DR%02x:%02x...", ch, *status);
#endif
		if (*status & (UART_LSR_BI | UART_LSR_PE |
			       UART_LSR_FE | UART_LSR_OE)) {
					
#ifdef SERIAL_DEBUG_INTR
			printk("handling exept....");
#endif
		}
		*status = serial_inp(cs, UART_LSR);
	} while (*status & UART_LSR_DR);
	if (cs->hw.elsa.MFlag == 2) {
		if (!(skb = dev_alloc_skb(cs->hw.elsa.rcvcnt)))
			printk(KERN_WARNING "ElsaSER: receive out of memory\n");
		else {
			memcpy(skb_put(skb, cs->hw.elsa.rcvcnt), cs->hw.elsa.rcvbuf, 
				cs->hw.elsa.rcvcnt);
			skb_queue_tail(& cs->hw.elsa.bcs->rqueue, skb);
		}
		schedule_event(cs->hw.elsa.bcs, B_RCVBUFREADY);
	} else {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "modem read cnt %d", cs->hw.elsa.rcvcnt);
		QuickHex(t, cs->hw.elsa.rcvbuf, cs->hw.elsa.rcvcnt);
		debugl1(cs, tmp);
	}
	cs->hw.elsa.rcvcnt = 0;
}

static inline void transmit_chars(struct IsdnCardState *cs, int *intr_done)
{
	int count;
	
	debugl1(cs, "transmit_chars: p(%x) cnt(%x)", cs->hw.elsa.transp, 
		cs->hw.elsa.transcnt);
	
	if (cs->hw.elsa.transcnt <= 0) {
		cs->hw.elsa.IER &= ~UART_IER_THRI;
		serial_out(cs, UART_IER, cs->hw.elsa.IER);
		return;
	}
	count = 16;
	do {
		serial_outp(cs, UART_TX, cs->hw.elsa.transbuf[cs->hw.elsa.transp++]);
		if (cs->hw.elsa.transp >= MAX_MODEM_BUF)
			cs->hw.elsa.transp=0;
		if (--cs->hw.elsa.transcnt <= 0)
			break;
	} while (--count > 0);
	if ((cs->hw.elsa.transcnt < WAKEUP_CHARS) && (cs->hw.elsa.MFlag==2))
		modem_fill(cs->hw.elsa.bcs);

#ifdef SERIAL_DEBUG_INTR
	printk("THRE...");
#endif
	if (intr_done)
		*intr_done = 0;
	if (cs->hw.elsa.transcnt <= 0) {
		cs->hw.elsa.IER &= ~UART_IER_THRI;
		serial_outp(cs, UART_IER, cs->hw.elsa.IER);
	}
}


static void rs_interrupt_elsa(struct IsdnCardState *cs)
{
	int status, iir, msr;
	int pass_counter = 0;
	
#ifdef SERIAL_DEBUG_INTR
	printk(KERN_DEBUG "rs_interrupt_single(%d)...", cs->irq);
#endif

	do {
		status = serial_inp(cs, UART_LSR);
		debugl1(cs,"rs LSR %02x", status);
#ifdef SERIAL_DEBUG_INTR
		printk("status = %x...", status);
#endif
		if (status & UART_LSR_DR)
			receive_chars(cs, &status);
		if (status & UART_LSR_THRE)
			transmit_chars(cs, NULL);
		if (pass_counter++ > RS_ISR_PASS_LIMIT) {
			printk("rs_single loop break.\n");
			break;
		}
		iir = serial_inp(cs, UART_IIR);
		debugl1(cs,"rs IIR %02x", iir);
		if ((iir & 0xf) == 0) {
			msr = serial_inp(cs, UART_MSR);
			debugl1(cs,"rs MSR %02x", msr);
		}
	} while (!(iir & UART_IIR_NO_INT));
#ifdef SERIAL_DEBUG_INTR
	printk("end.\n");
#endif
}

extern int open_hscxstate(struct IsdnCardState *cs, struct BCState *bcs);
extern void modehscx(struct BCState *bcs, int mode, int bc);
extern void hscx_l2l1(struct PStack *st, int pr, void *arg);

static void
close_elsastate(struct BCState *bcs)
{
	modehscx(bcs, 0, bcs->channel);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (bcs->hw.hscx.rcvbuf) {
			if (bcs->mode != L1_MODE_MODEM)
				kfree(bcs->hw.hscx.rcvbuf);
			bcs->hw.hscx.rcvbuf = NULL;
		}
		skb_queue_purge(&bcs->rqueue);
		skb_queue_purge(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb_any(bcs->tx_skb);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

static void
modem_write_cmd(struct IsdnCardState *cs, u_char *buf, int len) {
	int count, fp;
	u_char *msg = buf;
	
	if (!len)
		return;
	if (len > (MAX_MODEM_BUF - cs->hw.elsa.transcnt)) {
		return;
	}
	fp = cs->hw.elsa.transcnt + cs->hw.elsa.transp;
	fp &= (MAX_MODEM_BUF -1);
	count = len;
	if (count > MAX_MODEM_BUF - fp) {
		count = MAX_MODEM_BUF - fp;
		memcpy(cs->hw.elsa.transbuf + fp, msg, count);
		cs->hw.elsa.transcnt += count;
		msg += count;
		count = len - count;
		fp = 0;
	}
	memcpy(cs->hw.elsa.transbuf + fp, msg, count);
	cs->hw.elsa.transcnt += count;
	if (cs->hw.elsa.transcnt && 
	    !(cs->hw.elsa.IER & UART_IER_THRI)) {
		cs->hw.elsa.IER |= UART_IER_THRI;
		serial_outp(cs, UART_IER, cs->hw.elsa.IER);
	}
}

static void
modem_set_init(struct IsdnCardState *cs) {
	int timeout;

#define RCV_DELAY 20
	modem_write_cmd(cs, MInit_1, strlen(MInit_1));
	timeout = 1000;
	while(timeout-- && cs->hw.elsa.transcnt)
		udelay(1000);
	debugl1(cs, "msi tout=%d", timeout);
	mdelay(RCV_DELAY);
	modem_write_cmd(cs, MInit_2, strlen(MInit_2));
	timeout = 1000;
	while(timeout-- && cs->hw.elsa.transcnt)
		udelay(1000);
	debugl1(cs, "msi tout=%d", timeout);
	mdelay(RCV_DELAY);
	modem_write_cmd(cs, MInit_3, strlen(MInit_3));
	timeout = 1000;
	while(timeout-- && cs->hw.elsa.transcnt)
		udelay(1000);
	debugl1(cs, "msi tout=%d", timeout);
	mdelay(RCV_DELAY);
	modem_write_cmd(cs, MInit_4, strlen(MInit_4));
	timeout = 1000;
	while(timeout-- && cs->hw.elsa.transcnt)
		udelay(1000);
	debugl1(cs, "msi tout=%d", timeout);
	mdelay(RCV_DELAY);
	modem_write_cmd(cs, MInit_5, strlen(MInit_5));
	timeout = 1000;
	while(timeout-- && cs->hw.elsa.transcnt)
		udelay(1000);
	debugl1(cs, "msi tout=%d", timeout);
	mdelay(RCV_DELAY);
	modem_write_cmd(cs, MInit_6, strlen(MInit_6));
	timeout = 1000;
	while(timeout-- && cs->hw.elsa.transcnt)
		udelay(1000);
	debugl1(cs, "msi tout=%d", timeout);
	mdelay(RCV_DELAY);
	modem_write_cmd(cs, MInit_7, strlen(MInit_7));
	timeout = 1000;
	while(timeout-- && cs->hw.elsa.transcnt)
		udelay(1000);
	debugl1(cs, "msi tout=%d", timeout);
	mdelay(RCV_DELAY);
}

static void
modem_set_dial(struct IsdnCardState *cs, int outgoing) {
	int timeout;
#define RCV_DELAY 20

	modem_write_cmd(cs, MInit_speed28800, strlen(MInit_speed28800));
	timeout = 1000;
	while(timeout-- && cs->hw.elsa.transcnt)
		udelay(1000);
	debugl1(cs, "msi tout=%d", timeout);
	mdelay(RCV_DELAY);
	if (outgoing)
		modem_write_cmd(cs, MInit_dialout, strlen(MInit_dialout));
	else
		modem_write_cmd(cs, MInit_dialin, strlen(MInit_dialin));
	timeout = 1000;
	while(timeout-- && cs->hw.elsa.transcnt)
		udelay(1000);
	debugl1(cs, "msi tout=%d", timeout);
	mdelay(RCV_DELAY);
}

static void
modem_l2l1(struct PStack *st, int pr, void *arg)
{
	struct BCState *bcs = st->l1.bcs;
	struct sk_buff *skb = arg;
	u_long flags;

	if (pr == (PH_DATA | REQUEST)) {
		spin_lock_irqsave(&bcs->cs->lock, flags);
		if (bcs->tx_skb) {
			skb_queue_tail(&bcs->squeue, skb);
		} else {
			bcs->tx_skb = skb;
			test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
			bcs->hw.hscx.count = 0;
			write_modem(bcs);
		}
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
	} else if (pr == (PH_ACTIVATE | REQUEST)) {
		test_and_set_bit(BC_FLG_ACTIV, &bcs->Flag);
		st->l1.l1l2(st, PH_ACTIVATE | CONFIRM, NULL);
		set_arcofi(bcs->cs, st->l1.bc);
		mstartup(bcs->cs);
		modem_set_dial(bcs->cs, test_bit(FLG_ORIG, &st->l2.flag));
		bcs->cs->hw.elsa.MFlag=2;
	} else if (pr == (PH_DEACTIVATE | REQUEST)) {
		test_and_clear_bit(BC_FLG_ACTIV, &bcs->Flag);
		bcs->cs->dc.isac.arcofi_bc = st->l1.bc;
		arcofi_fsm(bcs->cs, ARCOFI_START, &ARCOFI_XOP_0);
		interruptible_sleep_on(&bcs->cs->dc.isac.arcofi_wait);
		bcs->cs->hw.elsa.MFlag=1;
	} else {
		printk(KERN_WARNING"ElsaSer: unknown pr %x\n", pr);
	}
}

static int
setstack_elsa(struct PStack *st, struct BCState *bcs)
{

	bcs->channel = st->l1.bc;
	switch (st->l1.mode) {
		case L1_MODE_HDLC:
		case L1_MODE_TRANS:
			if (open_hscxstate(st->l1.hardware, bcs))
				return (-1);
			st->l2.l2l1 = hscx_l2l1;
			break;
		case L1_MODE_MODEM:
			bcs->mode = L1_MODE_MODEM;
			if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
				bcs->hw.hscx.rcvbuf = bcs->cs->hw.elsa.rcvbuf;
				skb_queue_head_init(&bcs->rqueue);
				skb_queue_head_init(&bcs->squeue);
			}
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
			bcs->event = 0;
			bcs->hw.hscx.rcvidx = 0;
			bcs->tx_cnt = 0;
			bcs->cs->hw.elsa.bcs = bcs;
			st->l2.l2l1 = modem_l2l1;
			break;
	}
	st->l1.bcs = bcs;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

static void
init_modem(struct IsdnCardState *cs) {

	cs->bcs[0].BC_SetStack = setstack_elsa;
	cs->bcs[1].BC_SetStack = setstack_elsa;
	cs->bcs[0].BC_Close = close_elsastate;
	cs->bcs[1].BC_Close = close_elsastate;
	if (!(cs->hw.elsa.rcvbuf = kmalloc(MAX_MODEM_BUF,
		GFP_ATOMIC))) {
		printk(KERN_WARNING
			"Elsa: No modem mem hw.elsa.rcvbuf\n");
		return;
	}
	if (!(cs->hw.elsa.transbuf = kmalloc(MAX_MODEM_BUF,
		GFP_ATOMIC))) {
		printk(KERN_WARNING
			"Elsa: No modem mem hw.elsa.transbuf\n");
		kfree(cs->hw.elsa.rcvbuf);
		cs->hw.elsa.rcvbuf = NULL;
		return;
	}
	if (mstartup(cs)) {
		printk(KERN_WARNING "Elsa: problem startup modem\n");
	}
	modem_set_init(cs);
}

static void
release_modem(struct IsdnCardState *cs) {

	cs->hw.elsa.MFlag = 0;
	if (cs->hw.elsa.transbuf) {
		if (cs->hw.elsa.rcvbuf) {
			mshutdown(cs);
			kfree(cs->hw.elsa.rcvbuf);
			cs->hw.elsa.rcvbuf = NULL;
		}
		kfree(cs->hw.elsa.transbuf);
		cs->hw.elsa.transbuf = NULL;
	}
}

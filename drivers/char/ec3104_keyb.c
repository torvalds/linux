/*
 * linux/drivers/char/ec3104_keyb.c
 * 
 * Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 *
 * based on linux/drivers/char/pc_keyb.c, which had the following comments:
 *
 * Separation of the PC low-level part by Geert Uytterhoeven, May 1997
 * See keyboard.c for the whole history.
 *
 * Major cleanup by Martin Mares, May 1997
 *
 * Combined the keyboard and PS/2 mouse handling into one file,
 * because they share the same hardware.
 * Johan Myreen <jem@iki.fi> 1998-10-08.
 *
 * Code fixes to handle mouse ACKs properly.
 * C. Scott Ananian <cananian@alumni.princeton.edu> 1999-01-29.
 */
/* EC3104 note:
 * This code was written without any documentation about the EC3104 chip.  While
 * I hope I got most of the basic functionality right, the register names I use
 * are most likely completely different from those in the chip documentation.
 *
 * If you have any further information about the EC3104, please tell me
 * (prumpf@tux.org).
 */


#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/init.h>
#include <linux/kbd_ll.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/kbd_kern.h>
#include <linux/smp_lock.h>
#include <linux/bitops.h>

#include <asm/keyboard.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/ec3104.h>

#include <asm/io.h>

/* Some configuration switches are present in the include file... */

#include <linux/pc_keyb.h>

#define MSR_CTS 0x10
#define MCR_RTS 0x02
#define LSR_DR 0x01
#define LSR_BOTH_EMPTY 0x60

static struct e5_struct {
	u8 packet[8];
	int pos;
	int length;

	u8 cached_mcr;
	u8 last_msr;
} ec3104_keyb;
	
/* Simple translation table for the SysRq keys */


#ifdef CONFIG_MAGIC_SYSRQ
unsigned char ec3104_kbd_sysrq_xlate[128] =
	"\000\0331234567890-=\177\t"			/* 0x00 - 0x0f */
	"qwertyuiop[]\r\000as"				/* 0x10 - 0x1f */
	"dfghjkl;'`\000\\zxcv"				/* 0x20 - 0x2f */
	"bnm,./\000*\000 \000\201\202\203\204\205"	/* 0x30 - 0x3f */
	"\206\207\210\211\212\000\000789-456+1"		/* 0x40 - 0x4f */
	"230\177\000\000\213\214\000\000\000\000\000\000\000\000\000\000" /* 0x50 - 0x5f */
	"\r\000/";					/* 0x60 - 0x6f */
#endif

static void kbd_write_command_w(int data);
static void kbd_write_output_w(int data);
#ifdef CONFIG_PSMOUSE
static void aux_write_ack(int val);
static void __aux_write_ack(int val);
#endif

static DEFINE_SPINLOCK(kbd_controller_lock);
static unsigned char handle_kbd_event(void);

/* used only by send_data - set by keyboard_interrupt */
static volatile unsigned char reply_expected;
static volatile unsigned char acknowledge;
static volatile unsigned char resend;


int ec3104_kbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	return 0;
}

int ec3104_kbd_getkeycode(unsigned int scancode)
{
	return 0;
}


/* yes, it probably would be faster to use an array.  I don't care. */

static inline unsigned char ec3104_scan2key(unsigned char scancode)
{
	switch (scancode) {
	case  1: /* '`' */
		return 41;
		
	case  2 ... 27:
		return scancode;
		
	case 28: /* '\\' */
		return 43;

	case 29 ... 39:
		return scancode + 1;

	case 40: /* '\r' */
		return 28;

	case 41 ... 50:
		return scancode + 3;

	case 51: /* ' ' */
		return 57;
		
	case 52: /* escape */
		return 1;

	case 54: /* insert/delete (labelled delete) */
		/* this should arguably be 110, but I'd like to have ctrl-alt-del
		 * working with a standard keymap */
		return 111;

	case 55: /* left */
		return 105;
	case 56: /* home */
		return 102;
	case 57: /* end */
		return 107;
	case 58: /* up */
		return 103;
	case 59: /* down */
		return 108;
	case 60: /* pgup */
		return 104;
	case 61: /* pgdown */
		return 109;
	case 62: /* right */
		return 106;

	case 79 ... 88: /* f1 - f10 */
		return scancode - 20;

	case 89 ... 90: /* f11 - f12 */
		return scancode - 2;

	case 91: /* left shift */
		return 42;

	case 92: /* right shift */
		return 54;

	case 93: /* left alt */
		return 56;
	case 94: /* right alt */
		return 100;
	case 95: /* left ctrl */
		return 29;
	case 96: /* right ctrl */
		return 97;

	case 97: /* caps lock */
		return 58;
	case 102: /* left windows */
		return 125;
	case 103: /* right windows */
		return 126;

	case 106: /* Fn */
		/* this is wrong. */
		return 84;

	default:
		return 0;
	}
}
		
int ec3104_kbd_translate(unsigned char scancode, unsigned char *keycode,
		    char raw_mode)
{
	scancode &= 0x7f;

	*keycode = ec3104_scan2key(scancode);

 	return 1;
}

char ec3104_kbd_unexpected_up(unsigned char keycode)
{
	return 0200;
}

static inline void handle_keyboard_event(unsigned char scancode)
{
#ifdef CONFIG_VT
	handle_scancode(scancode, !(scancode & 0x80));
#endif				
	tasklet_schedule(&keyboard_tasklet);
}	

void ec3104_kbd_leds(unsigned char leds)
{
}

static u8 e5_checksum(u8 *packet, int count)
{
	int i;
	u8 sum = 0;

	for (i=0; i<count; i++)
		sum ^= packet[i];
		
	if (sum & 0x80)
		sum ^= 0xc0;

	return sum;
}

static void e5_wait_for_cts(struct e5_struct *k)
{
	u8 msr;
		
	do {
		msr = ctrl_inb(EC3104_SER4_MSR);
	} while (!(msr & MSR_CTS));
}


static void e5_send_byte(u8 byte, struct e5_struct *k)
{
	u8 status;
		
	do {
		status = ctrl_inb(EC3104_SER4_LSR);
	} while ((status & LSR_BOTH_EMPTY) != LSR_BOTH_EMPTY);
	
	printk("<%02x>", byte);

	ctrl_outb(byte, EC3104_SER4_DATA);

	do {
		status = ctrl_inb(EC3104_SER4_LSR);
	} while ((status & LSR_BOTH_EMPTY) != LSR_BOTH_EMPTY);
	
}

static int e5_send_packet(u8 *packet, int count, struct e5_struct *k)
{
	int i;

	disable_irq(EC3104_IRQ_SER4);
	
	if (k->cached_mcr & MCR_RTS) {
		printk("e5_send_packet: too slow\n");
		enable_irq(EC3104_IRQ_SER4);
		return -EAGAIN;
	}

	k->cached_mcr |= MCR_RTS;
	ctrl_outb(k->cached_mcr, EC3104_SER4_MCR);

	e5_wait_for_cts(k);

	printk("p: ");

	for(i=0; i<count; i++)
		e5_send_byte(packet[i], k);

	e5_send_byte(e5_checksum(packet, count), k);

	printk("\n");

	udelay(1500);

	k->cached_mcr &= ~MCR_RTS;
	ctrl_outb(k->cached_mcr, EC3104_SER4_MCR);

	set_current_state(TASK_UNINTERRUPTIBLE);
	
	

	enable_irq(EC3104_IRQ_SER4);

	

	return 0;
}

/*
 * E5 packets we know about:
 * E5->host 0x80 0x05 <checksum> - resend packet
 * host->E5 0x83 0x43 <contrast> - set LCD contrast
 * host->E5 0x85 0x41 0x02 <brightness> 0x02 - set LCD backlight
 * E5->host 0x87 <ps2 packet> 0x00 <checksum> - external PS2 
 * E5->host 0x88 <scancode> <checksum> - key press
 */

static void e5_receive(struct e5_struct *k)
{
	k->packet[k->pos++] = ctrl_inb(EC3104_SER4_DATA);

	if (k->pos == 1) {
		switch(k->packet[0]) {
		case 0x80:
			k->length = 3;
			break;
			
		case 0x87: /* PS2 ext */
			k->length = 6;
			break;

		case 0x88: /* keyboard */
			k->length = 3;
			break;

		default:
			k->length = 1;
			printk(KERN_WARNING "unknown E5 packet %02x\n",
			       k->packet[0]);
		}
	}

	if (k->pos == k->length) {
		int i;

		if (e5_checksum(k->packet, k->length) != 0)
			printk(KERN_WARNING "E5: wrong checksum\n");

#if 0
		printk("E5 packet [");
		for(i=0; i<k->length; i++) {
			printk("%02x ", k->packet[i]);
		}

		printk("(%02x)]\n", e5_checksum(k->packet, k->length-1));
#endif

		switch(k->packet[0]) {
		case 0x80:
		case 0x88:
			handle_keyboard_event(k->packet[1]);
			break;
		}

		k->pos = k->length = 0;
	}
}

static void ec3104_keyb_interrupt(int irq, void *data)
{
	struct e5_struct *k = &ec3104_keyb;
	u8 msr, lsr;

	msr = ctrl_inb(EC3104_SER4_MSR);
	
	if ((msr & MSR_CTS) && !(k->last_msr & MSR_CTS)) {
		if (k->cached_mcr & MCR_RTS)
			printk("confused: RTS already high\n");
		/* CTS went high.  Send RTS. */
		k->cached_mcr |= MCR_RTS;
		
		ctrl_outb(k->cached_mcr, EC3104_SER4_MCR);
	} else if ((!(msr & MSR_CTS)) && (k->last_msr & MSR_CTS)) {
		/* CTS went low. */
		if (!(k->cached_mcr & MCR_RTS))
			printk("confused: RTS already low\n");

		k->cached_mcr &= ~MCR_RTS;

		ctrl_outb(k->cached_mcr, EC3104_SER4_MCR);
	}

	k->last_msr = msr;

	lsr = ctrl_inb(EC3104_SER4_LSR);

	if (lsr & LSR_DR)
		e5_receive(k);
}

static void ec3104_keyb_clear_state(void)
{
	struct e5_struct *k = &ec3104_keyb;
	u8 msr, lsr;
	
	/* we want CTS to be low */
	k->last_msr = 0;

	for (;;) {
		msleep(100);

		msr = ctrl_inb(EC3104_SER4_MSR);
	
		lsr = ctrl_inb(EC3104_SER4_LSR);
		
		if (lsr & LSR_DR) {
			e5_receive(k);
			continue;
		}

		if ((msr & MSR_CTS) && !(k->last_msr & MSR_CTS)) {
			if (k->cached_mcr & MCR_RTS)
				printk("confused: RTS already high\n");
			/* CTS went high.  Send RTS. */
			k->cached_mcr |= MCR_RTS;
		
			ctrl_outb(k->cached_mcr, EC3104_SER4_MCR);
		} else if ((!(msr & MSR_CTS)) && (k->last_msr & MSR_CTS)) {
			/* CTS went low. */
			if (!(k->cached_mcr & MCR_RTS))
				printk("confused: RTS already low\n");
			
			k->cached_mcr &= ~MCR_RTS;
			
			ctrl_outb(k->cached_mcr, EC3104_SER4_MCR);
		} else
			break;

		k->last_msr = msr;

		continue;
	}
}

void __init ec3104_kbd_init_hw(void)
{
	ec3104_keyb.last_msr = ctrl_inb(EC3104_SER4_MSR);
	ec3104_keyb.cached_mcr = ctrl_inb(EC3104_SER4_MCR);

	ec3104_keyb_clear_state();

	/* Ok, finally allocate the IRQ, and off we go.. */
	request_irq(EC3104_IRQ_SER4, ec3104_keyb_interrupt, 0, "keyboard", NULL);
}

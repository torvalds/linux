/*
 * ppc-stub.c:  KGDB support for the Linux kernel.
 *
 * adapted from arch/sparc/kernel/sparc-stub.c for the PowerPC
 * some stuff borrowed from Paul Mackerras' xmon
 * Copyright (C) 1998 Michael AK Tesch (tesch@cs.wisc.edu)
 *
 * Modifications to run under Linux
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 * This file originally came from the gdb sources, and the
 * copyright notices have been retained below.
 */

/****************************************************************************

		THIS SOFTWARE IS NOT COPYRIGHTED

   HP offers the following for use in the public domain.  HP makes no
   warranty with regard to the software or its performance and the
   user accepts the software "AS IS" with all faults.

   HP DISCLAIMS ANY WARRANTIES, EXPRESS OR IMPLIED, WITH REGARD
   TO THIS SOFTWARE INCLUDING BUT NOT LIMITED TO THE WARRANTIES
   OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.

****************************************************************************/

/****************************************************************************
 *  Header: remcom.c,v 1.34 91/03/09 12:29:49 glenne Exp $
 *
 *  Module name: remcom.c $
 *  Revision: 1.34 $
 *  Date: 91/03/09 12:29:49 $
 *  Contributor:     Lake Stevens Instrument Division$
 *
 *  Description:     low level support for gdb debugger. $
 *
 *  Considerations:  only works on target hardware $
 *
 *  Written by:      Glenn Engel $
 *  ModuleState:     Experimental $
 *
 *  NOTES:           See Below $
 *
 *  Modified for SPARC by Stu Grossman, Cygnus Support.
 *
 *  This code has been extensively tested on the Fujitsu SPARClite demo board.
 *
 *  To enable debugger support, two things need to happen.  One, a
 *  call to set_debug_traps() is necessary in order to allow any breakpoints
 *  or error conditions to be properly intercepted and reported to gdb.
 *  Two, a breakpoint needs to be generated to begin communication.  This
 *  is most easily accomplished by a call to breakpoint().  Breakpoint()
 *  simulates a breakpoint by executing a trap #1.
 *
 *************
 *
 *    The following gdb commands are supported:
 *
 * command          function		          Return value
 *
 *    g             return the value of the CPU registers  hex data or ENN
 *    G             set the value of the CPU registers     OK or ENN
 *    qOffsets      Get section offsets.  Reply is Text=xxx;Data=yyy;Bss=zzz
 *
 *    mAA..AA,LLLL  Read LLLL bytes at address AA..AA      hex data or ENN
 *    MAA..AA,LLLL: Write LLLL bytes at address AA.AA      OK or ENN
 *
 *    c             Resume at current address              SNN   ( signal NN)
 *    cAA..AA       Continue at address AA..AA             SNN
 *
 *    s             Step one instruction                   SNN
 *    sAA..AA       Step one instruction from AA..AA       SNN
 *
 *    k             kill
 *
 *    ?             What was the last sigval ?             SNN   (signal NN)
 *
 *    bBB..BB	    Set baud rate to BB..BB		   OK or BNN, then sets
 *							   baud rate
 *
 * All commands and responses are sent with a packet which includes a
 * checksum.  A packet consists of
 *
 * $<packet info>#<checksum>.
 *
 * where
 * <packet info> :: <characters representing the command or response>
 * <checksum>    :: <two hex digits computed as modulo 256 sum of <packetinfo>>
 *
 * When a packet is received, it is first acknowledged with either '+' or '-'.
 * '+' indicates a successful transfer.  '-' indicates a failed transfer.
 *
 * Example:
 *
 * Host:                  Reply:
 * $m0,10#2a               +$00010203040506070809101112131415#42
 *
 ****************************************************************************/

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/sysrq.h>

#include <asm/cacheflush.h>
#include <asm/system.h>
#include <asm/signal.h>
#include <asm/kgdb.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>

void breakinst(void);

/*
 * BUFMAX defines the maximum number of characters in inbound/outbound buffers
 * at least NUMREGBYTES*2 are needed for register packets
 */
#define BUFMAX 2048
static char remcomInBuffer[BUFMAX];
static char remcomOutBuffer[BUFMAX];

static int initialized;
static int kgdb_active;
static int kgdb_started;
static u_int fault_jmp_buf[100];
static int kdebug;


static const char hexchars[]="0123456789abcdef";

/* Place where we save old trap entries for restoration - sparc*/
/* struct tt_entry kgdb_savettable[256]; */
/* typedef void (*trapfunc_t)(void); */

static void kgdb_fault_handler(struct pt_regs *regs);
static int handle_exception (struct pt_regs *regs);

#if 0
/* Install an exception handler for kgdb */
static void exceptionHandler(int tnum, unsigned int *tfunc)
{
	/* We are dorking with a live trap table, all irqs off */
}
#endif

int
kgdb_setjmp(long *buf)
{
	asm ("mflr 0; stw 0,0(%0);"
	     "stw 1,4(%0); stw 2,8(%0);"
	     "mfcr 0; stw 0,12(%0);"
	     "stmw 13,16(%0)"
	     : : "r" (buf));
	/* XXX should save fp regs as well */
	return 0;
}
void
kgdb_longjmp(long *buf, int val)
{
	if (val == 0)
		val = 1;
	asm ("lmw 13,16(%0);"
	     "lwz 0,12(%0); mtcrf 0x38,0;"
	     "lwz 0,0(%0); lwz 1,4(%0); lwz 2,8(%0);"
	     "mtlr 0; mr 3,%1"
	     : : "r" (buf), "r" (val));
}
/* Convert ch from a hex digit to an int */
static int
hex(unsigned char ch)
{
	if (ch >= 'a' && ch <= 'f')
		return ch-'a'+10;
	if (ch >= '0' && ch <= '9')
		return ch-'0';
	if (ch >= 'A' && ch <= 'F')
		return ch-'A'+10;
	return -1;
}

/* Convert the memory pointed to by mem into hex, placing result in buf.
 * Return a pointer to the last char put in buf (null), in case of mem fault,
 * return 0.
 */
static unsigned char *
mem2hex(const char *mem, char *buf, int count)
{
	unsigned char ch;
	unsigned short tmp_s;
	unsigned long tmp_l;

	if (kgdb_setjmp((long*)fault_jmp_buf) == 0) {
		debugger_fault_handler = kgdb_fault_handler;

		/* Accessing 16 bit and 32 bit objects in a single
		** load instruction is required to avoid bad side
		** effects for some IO registers.
		*/

		if ((count == 2) && (((long)mem & 1) == 0)) {
			tmp_s = *(unsigned short *)mem;
			mem += 2;
			*buf++ = hexchars[(tmp_s >> 12) & 0xf];
			*buf++ = hexchars[(tmp_s >> 8) & 0xf];
			*buf++ = hexchars[(tmp_s >> 4) & 0xf];
			*buf++ = hexchars[tmp_s & 0xf];

		} else if ((count == 4) && (((long)mem & 3) == 0)) {
			tmp_l = *(unsigned int *)mem;
			mem += 4;
			*buf++ = hexchars[(tmp_l >> 28) & 0xf];
			*buf++ = hexchars[(tmp_l >> 24) & 0xf];
			*buf++ = hexchars[(tmp_l >> 20) & 0xf];
			*buf++ = hexchars[(tmp_l >> 16) & 0xf];
			*buf++ = hexchars[(tmp_l >> 12) & 0xf];
			*buf++ = hexchars[(tmp_l >> 8) & 0xf];
			*buf++ = hexchars[(tmp_l >> 4) & 0xf];
			*buf++ = hexchars[tmp_l & 0xf];

		} else {
			while (count-- > 0) {
				ch = *mem++;
				*buf++ = hexchars[ch >> 4];
				*buf++ = hexchars[ch & 0xf];
			}
		}

	} else {
		/* error condition */
	}
	debugger_fault_handler = NULL;
	*buf = 0;
	return buf;
}

/* convert the hex array pointed to by buf into binary to be placed in mem
 * return a pointer to the character AFTER the last byte written.
*/
static char *
hex2mem(char *buf, char *mem, int count)
{
	unsigned char ch;
	int i;
	char *orig_mem;
	unsigned short tmp_s;
	unsigned long tmp_l;

	orig_mem = mem;

	if (kgdb_setjmp((long*)fault_jmp_buf) == 0) {
		debugger_fault_handler = kgdb_fault_handler;

		/* Accessing 16 bit and 32 bit objects in a single
		** store instruction is required to avoid bad side
		** effects for some IO registers.
		*/

		if ((count == 2) && (((long)mem & 1) == 0)) {
			tmp_s = hex(*buf++) << 12;
			tmp_s |= hex(*buf++) << 8;
			tmp_s |= hex(*buf++) << 4;
			tmp_s |= hex(*buf++);

			*(unsigned short *)mem = tmp_s;
			mem += 2;

		} else if ((count == 4) && (((long)mem & 3) == 0)) {
			tmp_l = hex(*buf++) << 28;
			tmp_l |= hex(*buf++) << 24;
			tmp_l |= hex(*buf++) << 20;
			tmp_l |= hex(*buf++) << 16;
			tmp_l |= hex(*buf++) << 12;
			tmp_l |= hex(*buf++) << 8;
			tmp_l |= hex(*buf++) << 4;
			tmp_l |= hex(*buf++);

			*(unsigned long *)mem = tmp_l;
			mem += 4;

		} else {
			for (i=0; i<count; i++) {
				ch = hex(*buf++) << 4;
				ch |= hex(*buf++);
				*mem++ = ch;
			}
		}


		/*
		** Flush the data cache, invalidate the instruction cache.
		*/
		flush_icache_range((int)orig_mem, (int)orig_mem + count - 1);

	} else {
		/* error condition */
	}
	debugger_fault_handler = NULL;
	return mem;
}

/*
 * While we find nice hex chars, build an int.
 * Return number of chars processed.
 */
static int
hexToInt(char **ptr, int *intValue)
{
	int numChars = 0;
	int hexValue;

	*intValue = 0;

	if (kgdb_setjmp((long*)fault_jmp_buf) == 0) {
		debugger_fault_handler = kgdb_fault_handler;
		while (**ptr) {
			hexValue = hex(**ptr);
			if (hexValue < 0)
				break;

			*intValue = (*intValue << 4) | hexValue;
			numChars ++;

			(*ptr)++;
		}
	} else {
		/* error condition */
	}
	debugger_fault_handler = NULL;

	return (numChars);
}

/* scan for the sequence $<data>#<checksum> */
static void
getpacket(char *buffer)
{
	unsigned char checksum;
	unsigned char xmitcsum;
	int i;
	int count;
	unsigned char ch;

	do {
		/* wait around for the start character, ignore all other
		 * characters */
		while ((ch = (getDebugChar() & 0x7f)) != '$') ;

		checksum = 0;
		xmitcsum = -1;

		count = 0;

		/* now, read until a # or end of buffer is found */
		while (count < BUFMAX) {
			ch = getDebugChar() & 0x7f;
			if (ch == '#')
				break;
			checksum = checksum + ch;
			buffer[count] = ch;
			count = count + 1;
		}

		if (count >= BUFMAX)
			continue;

		buffer[count] = 0;

		if (ch == '#') {
			xmitcsum = hex(getDebugChar() & 0x7f) << 4;
			xmitcsum |= hex(getDebugChar() & 0x7f);
			if (checksum != xmitcsum)
				putDebugChar('-');	/* failed checksum */
			else {
				putDebugChar('+'); /* successful transfer */
				/* if a sequence char is present, reply the ID */
				if (buffer[2] == ':') {
					putDebugChar(buffer[0]);
					putDebugChar(buffer[1]);
					/* remove sequence chars from buffer */
					count = strlen(buffer);
					for (i=3; i <= count; i++)
						buffer[i-3] = buffer[i];
				}
			}
		}
	} while (checksum != xmitcsum);
}

/* send the packet in buffer. */
static void putpacket(unsigned char *buffer)
{
	unsigned char checksum;
	int count;
	unsigned char ch, recv;

	/* $<packet info>#<checksum>. */
	do {
		putDebugChar('$');
		checksum = 0;
		count = 0;

		while ((ch = buffer[count])) {
			putDebugChar(ch);
			checksum += ch;
			count += 1;
		}

		putDebugChar('#');
		putDebugChar(hexchars[checksum >> 4]);
		putDebugChar(hexchars[checksum & 0xf]);
		recv = getDebugChar();
	} while ((recv & 0x7f) != '+');
}

static void kgdb_flush_cache_all(void)
{
	flush_instruction_cache();
}

/* Set up exception handlers for tracing and breakpoints
 * [could be called kgdb_init()]
 */
void set_debug_traps(void)
{
#if 0
	unsigned char c;

	save_and_cli(flags);

	/* In case GDB is started before us, ack any packets (presumably
	 * "$?#xx") sitting there.
	 *
	 * I've found this code causes more problems than it solves,
	 * so that's why it's commented out.  GDB seems to work fine
	 * now starting either before or after the kernel   -bwb
	 */

	while((c = getDebugChar()) != '$');
	while((c = getDebugChar()) != '#');
	c = getDebugChar(); /* eat first csum byte */
	c = getDebugChar(); /* eat second csum byte */
	putDebugChar('+'); /* ack it */
#endif
	debugger = kgdb;
	debugger_bpt = kgdb_bpt;
	debugger_sstep = kgdb_sstep;
	debugger_iabr_match = kgdb_iabr_match;
	debugger_dabr_match = kgdb_dabr_match;

	initialized = 1;
}

static void kgdb_fault_handler(struct pt_regs *regs)
{
	kgdb_longjmp((long*)fault_jmp_buf, 1);
}

int kgdb_bpt(struct pt_regs *regs)
{
	return handle_exception(regs);
}

int kgdb_sstep(struct pt_regs *regs)
{
	return handle_exception(regs);
}

void kgdb(struct pt_regs *regs)
{
	handle_exception(regs);
}

int kgdb_iabr_match(struct pt_regs *regs)
{
	printk(KERN_ERR "kgdb doesn't support iabr, what?!?\n");
	return handle_exception(regs);
}

int kgdb_dabr_match(struct pt_regs *regs)
{
	printk(KERN_ERR "kgdb doesn't support dabr, what?!?\n");
	return handle_exception(regs);
}

/* Convert the hardware trap type code to a unix signal number. */
/*
 * This table contains the mapping between PowerPC hardware trap types, and
 * signals, which are primarily what GDB understands.
 */
static struct hard_trap_info
{
	unsigned int tt;		/* Trap type code for powerpc */
	unsigned char signo;		/* Signal that we map this trap into */
} hard_trap_info[] = {
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
	{ 0x100, SIGINT  },		/* critical input interrupt */
	{ 0x200, SIGSEGV },		/* machine check */
	{ 0x300, SIGSEGV },		/* data storage */
	{ 0x400, SIGBUS  },		/* instruction storage */
	{ 0x500, SIGINT  },		/* interrupt */
	{ 0x600, SIGBUS  },		/* alignment */
	{ 0x700, SIGILL  },		/* program */
	{ 0x800, SIGILL  },		/* reserved */
	{ 0x900, SIGILL  },		/* reserved */
	{ 0xa00, SIGILL  },		/* reserved */
	{ 0xb00, SIGILL  },		/* reserved */
	{ 0xc00, SIGCHLD },		/* syscall */
	{ 0xd00, SIGILL  },		/* reserved */
	{ 0xe00, SIGILL  },		/* reserved */
	{ 0xf00, SIGILL  },		/* reserved */
	/*
	** 0x1000  PIT
	** 0x1010  FIT
	** 0x1020  watchdog
	** 0x1100  data TLB miss
	** 0x1200  instruction TLB miss
	*/
	{ 0x2002, SIGTRAP},		/* debug */
#else
	{ 0x200, SIGSEGV },		/* machine check */
	{ 0x300, SIGSEGV },		/* address error (store) */
	{ 0x400, SIGBUS },		/* instruction bus error */
	{ 0x500, SIGINT },		/* interrupt */
	{ 0x600, SIGBUS },		/* alingment */
	{ 0x700, SIGTRAP },		/* breakpoint trap */
	{ 0x800, SIGFPE },		/* fpu unavail */
	{ 0x900, SIGALRM },		/* decrementer */
	{ 0xa00, SIGILL },		/* reserved */
	{ 0xb00, SIGILL },		/* reserved */
	{ 0xc00, SIGCHLD },		/* syscall */
	{ 0xd00, SIGTRAP },		/* single-step/watch */
	{ 0xe00, SIGFPE },		/* fp assist */
#endif
	{ 0, 0}				/* Must be last */

};

static int computeSignal(unsigned int tt)
{
	struct hard_trap_info *ht;

	for (ht = hard_trap_info; ht->tt && ht->signo; ht++)
		if (ht->tt == tt)
			return ht->signo;

	return SIGHUP; /* default for things we don't know about */
}

#define PC_REGNUM 64
#define SP_REGNUM 1

/*
 * This function does all command processing for interfacing to gdb.
 */
static int
handle_exception (struct pt_regs *regs)
{
	int sigval;
	int addr;
	int length;
	char *ptr;
	unsigned int msr;

	/* We don't handle user-mode breakpoints. */
	if (user_mode(regs))
		return 0;

	if (debugger_fault_handler) {
		debugger_fault_handler(regs);
		panic("kgdb longjump failed!\n");
	}
	if (kgdb_active) {
		printk(KERN_ERR "interrupt while in kgdb, returning\n");
		return 0;
	}

	kgdb_active = 1;
	kgdb_started = 1;

#ifdef KGDB_DEBUG
	printk("kgdb: entering handle_exception; trap [0x%x]\n",
			(unsigned int)regs->trap);
#endif

	kgdb_interruptible(0);
	lock_kernel();
	msr = mfmsr();
	mtmsr(msr & ~MSR_EE);	/* disable interrupts */

	if (regs->nip == (unsigned long)breakinst) {
		/* Skip over breakpoint trap insn */
		regs->nip += 4;
	}

	/* reply to host that an exception has occurred */
	sigval = computeSignal(regs->trap);
	ptr = remcomOutBuffer;

	*ptr++ = 'T';
	*ptr++ = hexchars[sigval >> 4];
	*ptr++ = hexchars[sigval & 0xf];
	*ptr++ = hexchars[PC_REGNUM >> 4];
	*ptr++ = hexchars[PC_REGNUM & 0xf];
	*ptr++ = ':';
	ptr = mem2hex((char *)&regs->nip, ptr, 4);
	*ptr++ = ';';
	*ptr++ = hexchars[SP_REGNUM >> 4];
	*ptr++ = hexchars[SP_REGNUM & 0xf];
	*ptr++ = ':';
	ptr = mem2hex(((char *)regs) + SP_REGNUM*4, ptr, 4);
	*ptr++ = ';';
	*ptr++ = 0;

	putpacket(remcomOutBuffer);
	if (kdebug)
		printk("remcomOutBuffer: %s\n", remcomOutBuffer);

	/* XXX We may want to add some features dealing with poking the
	 * XXX page tables, ... (look at sparc-stub.c for more info)
	 * XXX also required hacking to the gdb sources directly...
	 */

	while (1) {
		remcomOutBuffer[0] = 0;

		getpacket(remcomInBuffer);
		switch (remcomInBuffer[0]) {
		case '?': /* report most recent signal */
			remcomOutBuffer[0] = 'S';
			remcomOutBuffer[1] = hexchars[sigval >> 4];
			remcomOutBuffer[2] = hexchars[sigval & 0xf];
			remcomOutBuffer[3] = 0;
			break;
#if 0
		case 'q': /* this screws up gdb for some reason...*/
		{
			extern long _start, sdata, __bss_start;

			ptr = &remcomInBuffer[1];
			if (strncmp(ptr, "Offsets", 7) != 0)
				break;

			ptr = remcomOutBuffer;
			sprintf(ptr, "Text=%8.8x;Data=%8.8x;Bss=%8.8x",
				&_start, &sdata, &__bss_start);
			break;
		}
#endif
		case 'd':
			/* toggle debug flag */
			kdebug ^= 1;
			break;

		case 'g':	/* return the value of the CPU registers.
				 * some of them are non-PowerPC names :(
				 * they are stored in gdb like:
				 * struct {
				 *     u32 gpr[32];
				 *     f64 fpr[32];
				 *     u32 pc, ps, cnd, lr; (ps=msr)
				 *     u32 cnt, xer, mq;
				 * }
				 */
		{
			int i;
			ptr = remcomOutBuffer;
			/* General Purpose Regs */
			ptr = mem2hex((char *)regs, ptr, 32 * 4);
			/* Floating Point Regs - FIXME */
			/*ptr = mem2hex((char *), ptr, 32 * 8);*/
			for(i=0; i<(32*8*2); i++) { /* 2chars/byte */
				ptr[i] = '0';
			}
			ptr += 32*8*2;
			/* pc, msr, cr, lr, ctr, xer, (mq is unused) */
			ptr = mem2hex((char *)&regs->nip, ptr, 4);
			ptr = mem2hex((char *)&regs->msr, ptr, 4);
			ptr = mem2hex((char *)&regs->ccr, ptr, 4);
			ptr = mem2hex((char *)&regs->link, ptr, 4);
			ptr = mem2hex((char *)&regs->ctr, ptr, 4);
			ptr = mem2hex((char *)&regs->xer, ptr, 4);
		}
			break;

		case 'G': /* set the value of the CPU registers */
		{
			ptr = &remcomInBuffer[1];

			/*
			 * If the stack pointer has moved, you should pray.
			 * (cause only god can help you).
			 */

			/* General Purpose Regs */
			hex2mem(ptr, (char *)regs, 32 * 4);

			/* Floating Point Regs - FIXME?? */
			/*ptr = hex2mem(ptr, ??, 32 * 8);*/
			ptr += 32*8*2;

			/* pc, msr, cr, lr, ctr, xer, (mq is unused) */
			ptr = hex2mem(ptr, (char *)&regs->nip, 4);
			ptr = hex2mem(ptr, (char *)&regs->msr, 4);
			ptr = hex2mem(ptr, (char *)&regs->ccr, 4);
			ptr = hex2mem(ptr, (char *)&regs->link, 4);
			ptr = hex2mem(ptr, (char *)&regs->ctr, 4);
			ptr = hex2mem(ptr, (char *)&regs->xer, 4);

			strcpy(remcomOutBuffer,"OK");
		}
			break;
		case 'H':
			/* don't do anything, yet, just acknowledge */
			hexToInt(&ptr, &addr);
			strcpy(remcomOutBuffer,"OK");
			break;

		case 'm':	/* mAA..AA,LLLL  Read LLLL bytes at address AA..AA */
				/* Try to read %x,%x.  */

			ptr = &remcomInBuffer[1];

			if (hexToInt(&ptr, &addr) && *ptr++ == ','
					&& hexToInt(&ptr, &length)) {
				if (mem2hex((char *)addr, remcomOutBuffer,
							length))
					break;
				strcpy(remcomOutBuffer, "E03");
			} else
				strcpy(remcomOutBuffer, "E01");
			break;

		case 'M': /* MAA..AA,LLLL: Write LLLL bytes at address AA.AA return OK */
			/* Try to read '%x,%x:'.  */

			ptr = &remcomInBuffer[1];

			if (hexToInt(&ptr, &addr) && *ptr++ == ','
					&& hexToInt(&ptr, &length)
					&& *ptr++ == ':') {
				if (hex2mem(ptr, (char *)addr, length))
					strcpy(remcomOutBuffer, "OK");
				else
					strcpy(remcomOutBuffer, "E03");
				flush_icache_range(addr, addr+length);
			} else
				strcpy(remcomOutBuffer, "E02");
			break;


		case 'k': /* kill the program, actually just continue */
		case 'c': /* cAA..AA  Continue; address AA..AA optional */
			/* try to read optional parameter, pc unchanged if no parm */

			ptr = &remcomInBuffer[1];
			if (hexToInt(&ptr, &addr))
				regs->nip = addr;

/* Need to flush the instruction cache here, as we may have deposited a
 * breakpoint, and the icache probably has no way of knowing that a data ref to
 * some location may have changed something that is in the instruction cache.
 */
			kgdb_flush_cache_all();
			mtmsr(msr);

			kgdb_interruptible(1);
			unlock_kernel();
			kgdb_active = 0;
			if (kdebug) {
				printk("remcomInBuffer: %s\n", remcomInBuffer);
				printk("remcomOutBuffer: %s\n", remcomOutBuffer);
			}
			return 1;

		case 's':
			kgdb_flush_cache_all();
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
			mtspr(SPRN_DBCR0, mfspr(SPRN_DBCR0) | DBCR0_IC);
			regs->msr |= MSR_DE;
#else
			regs->msr |= MSR_SE;
#endif
			unlock_kernel();
			kgdb_active = 0;
			if (kdebug) {
				printk("remcomInBuffer: %s\n", remcomInBuffer);
				printk("remcomOutBuffer: %s\n", remcomOutBuffer);
			}
			return 1;

		case 'r':		/* Reset (if user process..exit ???)*/
			panic("kgdb reset.");
			break;
		}			/* switch */
		if (remcomOutBuffer[0] && kdebug) {
			printk("remcomInBuffer: %s\n", remcomInBuffer);
			printk("remcomOutBuffer: %s\n", remcomOutBuffer);
		}
		/* reply to the request */
		putpacket(remcomOutBuffer);
	} /* while(1) */
}

/* This function will generate a breakpoint exception.  It is used at the
   beginning of a program to sync up with a debugger and can be used
   otherwise as a quick means to stop program execution and "break" into
   the debugger. */

void
breakpoint(void)
{
	if (!initialized) {
		printk("breakpoint() called b4 kgdb init\n");
		return;
	}

	asm("	.globl breakinst	\n\
	     breakinst: .long 0x7d821008");
}

#ifdef CONFIG_KGDB_CONSOLE
/* Output string in GDB O-packet format if GDB has connected. If nothing
   output, returns 0 (caller must then handle output). */
int
kgdb_output_string (const char* s, unsigned int count)
{
	char buffer[512];

	if (!kgdb_started)
		return 0;

	count = (count <= (sizeof(buffer) / 2 - 2))
		? count : (sizeof(buffer) / 2 - 2);

	buffer[0] = 'O';
	mem2hex (s, &buffer[1], count);
	putpacket(buffer);

	return 1;
}
#endif

static void sysrq_handle_gdb(int key, struct pt_regs *pt_regs,
			     struct tty_struct *tty)
{
	printk("Entering GDB stub\n");
	breakpoint();
}
static struct sysrq_key_op sysrq_gdb_op = {
        .handler        = sysrq_handle_gdb,
        .help_msg       = "Gdb",
        .action_msg     = "GDB",
};

static int gdb_register_sysrq(void)
{
	printk("Registering GDB sysrq handler\n");
	register_sysrq_key('g', &sysrq_gdb_op);
	return 0;
}
module_init(gdb_register_sysrq);

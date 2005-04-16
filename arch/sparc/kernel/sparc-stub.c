/* $Id: sparc-stub.c,v 1.28 2001/10/30 04:54:21 davem Exp $
 * sparc-stub.c:  KGDB support for the Linux kernel.
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
 * command          function                               Return value
 *
 *    g             return the value of the CPU registers  hex data or ENN
 *    G             set the value of the CPU registers     OK or ENN
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
 * <checksum>    :: < two hex digits computed as modulo 256 sum of <packetinfo>>
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

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/signal.h>
#include <asm/oplib.h>
#include <asm/head.h>
#include <asm/traps.h>
#include <asm/vac-ops.h>
#include <asm/kgdb.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>

/*
 *
 * external low-level support routines
 */

extern void putDebugChar(char);   /* write a single character      */
extern char getDebugChar(void);   /* read and return a single char */

/*
 * BUFMAX defines the maximum number of characters in inbound/outbound buffers
 * at least NUMREGBYTES*2 are needed for register packets
 */
#define BUFMAX 2048

static int initialized;	/* !0 means we've been initialized */

static const char hexchars[]="0123456789abcdef";

#define NUMREGS 72

/* Number of bytes of registers.  */
#define NUMREGBYTES (NUMREGS * 4)
enum regnames {G0, G1, G2, G3, G4, G5, G6, G7,
		 O0, O1, O2, O3, O4, O5, SP, O7,
		 L0, L1, L2, L3, L4, L5, L6, L7,
		 I0, I1, I2, I3, I4, I5, FP, I7,

		 F0, F1, F2, F3, F4, F5, F6, F7,
		 F8, F9, F10, F11, F12, F13, F14, F15,
		 F16, F17, F18, F19, F20, F21, F22, F23,
		 F24, F25, F26, F27, F28, F29, F30, F31,
		 Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR };


extern void trap_low(void);  /* In arch/sparc/kernel/entry.S */

unsigned long get_sun4cpte(unsigned long addr)
{
	unsigned long entry;

	__asm__ __volatile__("\n\tlda [%1] %2, %0\n\t" : 
			     "=r" (entry) :
			     "r" (addr), "i" (ASI_PTE));
	return entry;
}

unsigned long get_sun4csegmap(unsigned long addr)
{
	unsigned long entry;

	__asm__ __volatile__("\n\tlduba [%1] %2, %0\n\t" : 
			     "=r" (entry) :
			     "r" (addr), "i" (ASI_SEGMAP));
	return entry;
}

#if 0
/* Have to sort this out. This cannot be done after initialization. */
static void flush_cache_all_nop(void) {}
#endif

/* Place where we save old trap entries for restoration */
struct tt_entry kgdb_savettable[256];
typedef void (*trapfunc_t)(void);

/* Helper routine for manipulation of kgdb_savettable */
static inline void copy_ttentry(struct tt_entry *src, struct tt_entry *dest)
{
	dest->inst_one = src->inst_one;
	dest->inst_two = src->inst_two;
	dest->inst_three = src->inst_three;
	dest->inst_four = src->inst_four;
}

/* Initialize the kgdb_savettable so that debugging can commence */
static void eh_init(void)
{
	int i;

	for(i=0; i < 256; i++)
		copy_ttentry(&sparc_ttable[i], &kgdb_savettable[i]);
}

/* Install an exception handler for kgdb */
static void exceptionHandler(int tnum, trapfunc_t trap_entry)
{
	unsigned long te_addr = (unsigned long) trap_entry;

	/* Make new vector */
	sparc_ttable[tnum].inst_one =
		SPARC_BRANCH((unsigned long) te_addr,
			     (unsigned long) &sparc_ttable[tnum].inst_one);
	sparc_ttable[tnum].inst_two = SPARC_RD_PSR_L0;
	sparc_ttable[tnum].inst_three = SPARC_NOP;
	sparc_ttable[tnum].inst_four = SPARC_NOP;
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

/* scan for the sequence $<data>#<checksum>     */
static void
getpacket(char *buffer)
{
	unsigned char checksum;
	unsigned char xmitcsum;
	int i;
	int count;
	unsigned char ch;

	do {
		/* wait around for the start character, ignore all other characters */
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

/* send the packet in buffer.  */

static void
putpacket(unsigned char *buffer)
{
	unsigned char checksum;
	int count;
	unsigned char ch, recv;

	/*  $<packet info>#<checksum>. */
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

static char remcomInBuffer[BUFMAX];
static char remcomOutBuffer[BUFMAX];

/* Convert the memory pointed to by mem into hex, placing result in buf.
 * Return a pointer to the last char put in buf (null), in case of mem fault,
 * return 0.
 */

static unsigned char *
mem2hex(char *mem, char *buf, int count)
{
	unsigned char ch;

	while (count-- > 0) {
		/* This assembler code is basically:  ch = *mem++;
		 * except that we use the SPARC/Linux exception table
		 * mechanism (see how "fixup" works in kernel_mna_trap_fault)
		 * to arrange for a "return 0" upon a memory fault
		 */
		__asm__(
			"\n1:\n\t"
			"ldub [%0], %1\n\t"
			"inc %0\n\t"
			".section .fixup,#alloc,#execinstr\n\t"
			".align 4\n"
			"2:\n\t"
			"retl\n\t"
			" mov 0, %%o0\n\t"
			".section __ex_table, #alloc\n\t"
			".align 4\n\t"
			".word 1b, 2b\n\t"
			".text\n"
			: "=r" (mem), "=r" (ch) : "0" (mem));
		*buf++ = hexchars[ch >> 4];
		*buf++ = hexchars[ch & 0xf];
	}

	*buf = 0;
	return buf;
}

/* convert the hex array pointed to by buf into binary to be placed in mem
 * return a pointer to the character AFTER the last byte written.
*/
static char *
hex2mem(char *buf, char *mem, int count)
{
	int i;
	unsigned char ch;

	for (i=0; i<count; i++) {

		ch = hex(*buf++) << 4;
		ch |= hex(*buf++);
		/* Assembler code is   *mem++ = ch;   with return 0 on fault */
		__asm__(
			"\n1:\n\t"
			"stb %1, [%0]\n\t"
			"inc %0\n\t"
			".section .fixup,#alloc,#execinstr\n\t"
			".align 4\n"
			"2:\n\t"
			"retl\n\t"
			" mov 0, %%o0\n\t"
			".section __ex_table, #alloc\n\t"
			".align 4\n\t"
			".word 1b, 2b\n\t"
			".text\n"
			: "=r" (mem) : "r" (ch) , "0" (mem));
	}
	return mem;
}

/* This table contains the mapping between SPARC hardware trap types, and
   signals, which are primarily what GDB understands.  It also indicates
   which hardware traps we need to commandeer when initializing the stub. */

static struct hard_trap_info
{
  unsigned char tt;		/* Trap type code for SPARC */
  unsigned char signo;		/* Signal that we map this trap into */
} hard_trap_info[] = {
  {SP_TRAP_SBPT, SIGTRAP},      /* ta 1 - Linux/KGDB software breakpoint */
  {0, 0}			/* Must be last */
};

/* Set up exception handlers for tracing and breakpoints */

void
set_debug_traps(void)
{
	struct hard_trap_info *ht;
	unsigned long flags;

	local_irq_save(flags);
#if 0	
/* Have to sort this out. This cannot be done after initialization. */
	BTFIXUPSET_CALL(flush_cache_all, flush_cache_all_nop, BTFIXUPCALL_NOP);
#endif

	/* Initialize our copy of the Linux Sparc trap table */
	eh_init();

	for (ht = hard_trap_info; ht->tt && ht->signo; ht++) {
		/* Only if it doesn't destroy our fault handlers */
		if((ht->tt != SP_TRAP_TFLT) && 
		   (ht->tt != SP_TRAP_DFLT))
			exceptionHandler(ht->tt, trap_low);
	}

	/* In case GDB is started before us, ack any packets (presumably
	 * "$?#xx") sitting there.
	 *
	 * I've found this code causes more problems than it solves,
	 * so that's why it's commented out.  GDB seems to work fine
	 * now starting either before or after the kernel   -bwb
	 */
#if 0
	while((c = getDebugChar()) != '$');
	while((c = getDebugChar()) != '#');
	c = getDebugChar(); /* eat first csum byte */
	c = getDebugChar(); /* eat second csum byte */
	putDebugChar('+'); /* ack it */
#endif

	initialized = 1; /* connect! */
	local_irq_restore(flags);
}

/* Convert the SPARC hardware trap type code to a unix signal number. */

static int
computeSignal(int tt)
{
	struct hard_trap_info *ht;

	for (ht = hard_trap_info; ht->tt && ht->signo; ht++)
		if (ht->tt == tt)
			return ht->signo;

	return SIGHUP;         /* default for things we don't know about */
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

	while (**ptr) {
		hexValue = hex(**ptr);
		if (hexValue < 0)
			break;

		*intValue = (*intValue << 4) | hexValue;
		numChars ++;

		(*ptr)++;
	}

	return (numChars);
}

/*
 * This function does all command processing for interfacing to gdb.  It
 * returns 1 if you should skip the instruction at the trap address, 0
 * otherwise.
 */

extern void breakinst(void);

void
handle_exception (unsigned long *registers)
{
	int tt;       /* Trap type */
	int sigval;
	int addr;
	int length;
	char *ptr;
	unsigned long *sp;

	/* First, we must force all of the windows to be spilled out */

	asm("save %sp, -64, %sp\n\t"
	    "save %sp, -64, %sp\n\t"
	    "save %sp, -64, %sp\n\t"
	    "save %sp, -64, %sp\n\t"
	    "save %sp, -64, %sp\n\t"
	    "save %sp, -64, %sp\n\t"
	    "save %sp, -64, %sp\n\t"
	    "save %sp, -64, %sp\n\t"
	    "restore\n\t"
	    "restore\n\t"
	    "restore\n\t"
	    "restore\n\t"
	    "restore\n\t"
	    "restore\n\t"
	    "restore\n\t"
	    "restore\n\t");

	lock_kernel();
	if (registers[PC] == (unsigned long)breakinst) {
		/* Skip over breakpoint trap insn */
		registers[PC] = registers[NPC];
		registers[NPC] += 4;
	}

	sp = (unsigned long *)registers[SP];

	tt = (registers[TBR] >> 4) & 0xff;

	/* reply to host that an exception has occurred */
	sigval = computeSignal(tt);
	ptr = remcomOutBuffer;

	*ptr++ = 'T';
	*ptr++ = hexchars[sigval >> 4];
	*ptr++ = hexchars[sigval & 0xf];

	*ptr++ = hexchars[PC >> 4];
	*ptr++ = hexchars[PC & 0xf];
	*ptr++ = ':';
	ptr = mem2hex((char *)&registers[PC], ptr, 4);
	*ptr++ = ';';

	*ptr++ = hexchars[FP >> 4];
	*ptr++ = hexchars[FP & 0xf];
	*ptr++ = ':';
	ptr = mem2hex((char *) (sp + 8 + 6), ptr, 4); /* FP */
	*ptr++ = ';';

	*ptr++ = hexchars[SP >> 4];
	*ptr++ = hexchars[SP & 0xf];
	*ptr++ = ':';
	ptr = mem2hex((char *)&sp, ptr, 4);
	*ptr++ = ';';

	*ptr++ = hexchars[NPC >> 4];
	*ptr++ = hexchars[NPC & 0xf];
	*ptr++ = ':';
	ptr = mem2hex((char *)&registers[NPC], ptr, 4);
	*ptr++ = ';';

	*ptr++ = hexchars[O7 >> 4];
	*ptr++ = hexchars[O7 & 0xf];
	*ptr++ = ':';
	ptr = mem2hex((char *)&registers[O7], ptr, 4);
	*ptr++ = ';';

	*ptr++ = 0;

	putpacket(remcomOutBuffer);

	/* XXX We may want to add some features dealing with poking the
	 * XXX page tables, the real ones on the srmmu, and what is currently
	 * XXX loaded in the sun4/sun4c tlb at this point in time.  But this
	 * XXX also required hacking to the gdb sources directly...
	 */

	while (1) {
		remcomOutBuffer[0] = 0;

		getpacket(remcomInBuffer);
		switch (remcomInBuffer[0]) {
		case '?':
			remcomOutBuffer[0] = 'S';
			remcomOutBuffer[1] = hexchars[sigval >> 4];
			remcomOutBuffer[2] = hexchars[sigval & 0xf];
			remcomOutBuffer[3] = 0;
			break;

		case 'd':
			/* toggle debug flag */
			break;

		case 'g':		/* return the value of the CPU registers */
		{
			ptr = remcomOutBuffer;
			/* G & O regs */
			ptr = mem2hex((char *)registers, ptr, 16 * 4);
			/* L & I regs */
			ptr = mem2hex((char *) (sp + 0), ptr, 16 * 4);
			/* Floating point */
			memset(ptr, '0', 32 * 8);
			/* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
			mem2hex((char *)&registers[Y], (ptr + 32 * 4 * 2), (8 * 4));
		}
			break;

		case 'G':	   /* set the value of the CPU registers - return OK */
		{
			unsigned long *newsp, psr;

			psr = registers[PSR];

			ptr = &remcomInBuffer[1];
			/* G & O regs */
			hex2mem(ptr, (char *)registers, 16 * 4);
			/* L & I regs */
			hex2mem(ptr + 16 * 4 * 2, (char *) (sp + 0), 16 * 4);
			/* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
			hex2mem(ptr + 64 * 4 * 2, (char *)&registers[Y], 8 * 4);

			/* See if the stack pointer has moved.  If so,
			 * then copy the saved locals and ins to the
			 * new location.  This keeps the window
			 * overflow and underflow routines happy.
			 */

			newsp = (unsigned long *)registers[SP];
			if (sp != newsp)
				sp = memcpy(newsp, sp, 16 * 4);

			/* Don't allow CWP to be modified. */

			if (psr != registers[PSR])
				registers[PSR] = (psr & 0x1f) | (registers[PSR] & ~0x1f);

			strcpy(remcomOutBuffer,"OK");
		}
			break;

		case 'm':	  /* mAA..AA,LLLL  Read LLLL bytes at address AA..AA */
			/* Try to read %x,%x.  */

			ptr = &remcomInBuffer[1];

			if (hexToInt(&ptr, &addr)
			    && *ptr++ == ','
			    && hexToInt(&ptr, &length))	{
				if (mem2hex((char *)addr, remcomOutBuffer, length))
					break;

				strcpy (remcomOutBuffer, "E03");
			} else {
				strcpy(remcomOutBuffer,"E01");
			}
			break;

		case 'M': /* MAA..AA,LLLL: Write LLLL bytes at address AA.AA return OK */
			/* Try to read '%x,%x:'.  */

			ptr = &remcomInBuffer[1];

			if (hexToInt(&ptr, &addr)
			    && *ptr++ == ','
			    && hexToInt(&ptr, &length)
			    && *ptr++ == ':') {
				if (hex2mem(ptr, (char *)addr, length)) {
					strcpy(remcomOutBuffer, "OK");
				} else {
					strcpy(remcomOutBuffer, "E03");
				}
			} else {
				strcpy(remcomOutBuffer, "E02");
			}
			break;

		case 'c':    /* cAA..AA    Continue at address AA..AA(optional) */
			/* try to read optional parameter, pc unchanged if no parm */

			ptr = &remcomInBuffer[1];
			if (hexToInt(&ptr, &addr)) {
				registers[PC] = addr;
				registers[NPC] = addr + 4;
			}

/* Need to flush the instruction cache here, as we may have deposited a
 * breakpoint, and the icache probably has no way of knowing that a data ref to
 * some location may have changed something that is in the instruction cache.
 */
			flush_cache_all();
			unlock_kernel();
			return;

			/* kill the program */
		case 'k' :		/* do nothing */
			break;
		case 'r':		/* Reset */
			asm ("call 0\n\t"
			     "nop\n\t");
			break;
		}			/* switch */

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
	if (!initialized)
		return;

	/* Again, watch those c-prefixes for ELF kernels */
#if defined(__svr4__) || defined(__ELF__)
	asm(".globl breakinst\n"
	    "breakinst:\n\t"
	    "ta 1\n");
#else
	asm(".globl _breakinst\n"
	    "_breakinst:\n\t"
	    "ta 1\n");
#endif
}

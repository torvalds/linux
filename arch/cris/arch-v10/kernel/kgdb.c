/*!**************************************************************************
*!
*! FILE NAME  : kgdb.c
*!
*! DESCRIPTION: Implementation of the gdb stub with respect to ETRAX 100.
*!              It is a mix of arch/m68k/kernel/kgdb.c and cris_stub.c.
*!
*!---------------------------------------------------------------------------
*! HISTORY
*!
*! DATE         NAME            CHANGES
*! ----         ----            -------
*! Apr 26 1999  Hendrik Ruijter Initial version.
*! May  6 1999  Hendrik Ruijter Removed call to strlen in libc and removed
*!                              struct assignment as it generates calls to
*!                              memcpy in libc.
*! Jun 17 1999  Hendrik Ruijter Added gdb 4.18 support. 'X', 'qC' and 'qL'.
*! Jul 21 1999  Bjorn Wesen     eLinux port
*!
*! $Log: kgdb.c,v $
*! Revision 1.6  2005/01/14 10:12:17  starvik
*! KGDB on separate port.
*! Console fixes from 2.4.
*!
*! Revision 1.5  2004/10/07 13:59:08  starvik
*! Corrected call to set_int_vector
*!
*! Revision 1.4  2003/04/09 05:20:44  starvik
*! Merge of Linux 2.5.67
*!
*! Revision 1.3  2003/01/21 19:11:08  starvik
*! Modified include path for new dir layout
*!
*! Revision 1.2  2002/11/19 14:35:24  starvik
*! Changes from linux 2.4
*! Changed struct initializer syntax to the currently prefered notation
*!
*! Revision 1.1  2001/12/17 13:59:27  bjornw
*! Initial revision
*!
*! Revision 1.6  2001/10/09 13:10:03  matsfg
*! Added $ on registers and removed some underscores
*!
*! Revision 1.5  2001/04/17 13:58:39  orjanf
*! * Renamed CONFIG_KGDB to CONFIG_ETRAX_KGDB.
*!
*! Revision 1.4  2001/02/23 13:45:19  bjornw
*! config.h check
*!
*! Revision 1.3  2001/01/31 18:08:23  orjanf
*! Removed kgdb_handle_breakpoint from being the break 8 handler.
*!
*! Revision 1.2  2001/01/12 14:22:25  orjanf
*! Updated kernel debugging support to work with ETRAX 100LX.
*!
*! Revision 1.1  2000/07/10 16:25:21  bjornw
*! Initial revision
*!
*! Revision 1.1.1.1  1999/12/03 14:57:31  bjornw
*! * Initial version of arch/cris, the latest CRIS architecture with an MMU.
*!   Mostly copied from arch/etrax100 with appropriate renames of files.
*!   The mm/ subdir is copied from arch/i386.
*!   This does not compile yet at all.
*!
*!
*! Revision 1.4  1999/07/22 17:25:25  bjornw
*! Dont wait for + in putpacket if we havent hit the initial breakpoint yet. Added a kgdb_init function which sets up the break and irq vectors.
*!
*! Revision 1.3  1999/07/21 19:51:18  bjornw
*! Check if the interrupting char is a ctrl-C, ignore otherwise.
*!
*! Revision 1.2  1999/07/21 18:09:39  bjornw
*! Ported to eLinux architecture, and added some kgdb documentation.
*!
*!
*!---------------------------------------------------------------------------
*!
*! $Id: kgdb.c,v 1.6 2005/01/14 10:12:17 starvik Exp $
*!
*! (C) Copyright 1999, Axis Communications AB, LUND, SWEDEN
*!
*!**************************************************************************/
/* @(#) cris_stub.c 1.3 06/17/99 */

/*
 *  kgdb usage notes:
 *  -----------------
 *
 * If you select CONFIG_ETRAX_KGDB in the configuration, the kernel will be 
 * built with different gcc flags: "-g" is added to get debug infos, and
 * "-fomit-frame-pointer" is omitted to make debugging easier. Since the
 * resulting kernel will be quite big (approx. > 7 MB), it will be stripped
 * before compresion. Such a kernel will behave just as usually, except if
 * given a "debug=<device>" command line option. (Only serial devices are
 * allowed for <device>, i.e. no printers or the like; possible values are
 * machine depedend and are the same as for the usual debug device, the one
 * for logging kernel messages.) If that option is given and the device can be
 * initialized, the kernel will connect to the remote gdb in trap_init(). The
 * serial parameters are fixed to 8N1 and 115200 bps, for easyness of
 * implementation.
 *
 * To start a debugging session, start that gdb with the debugging kernel
 * image (the one with the symbols, vmlinux.debug) named on the command line.
 * This file will be used by gdb to get symbol and debugging infos about the
 * kernel. Next, select remote debug mode by
 *    target remote <device>
 * where <device> is the name of the serial device over which the debugged
 * machine is connected. Maybe you have to adjust the baud rate by
 *    set remotebaud <rate>
 * or also other parameters with stty:
 *    shell stty ... </dev/...
 * If the kernel to debug has already booted, it waited for gdb and now
 * connects, and you'll see a breakpoint being reported. If the kernel isn't
 * running yet, start it now. The order of gdb and the kernel doesn't matter.
 * Another thing worth knowing about in the getting-started phase is how to
 * debug the remote protocol itself. This is activated with
 *    set remotedebug 1
 * gdb will then print out each packet sent or received. You'll also get some
 * messages about the gdb stub on the console of the debugged machine.
 *
 * If all that works, you can use lots of the usual debugging techniques on
 * the kernel, e.g. inspecting and changing variables/memory, setting
 * breakpoints, single stepping and so on. It's also possible to interrupt the
 * debugged kernel by pressing C-c in gdb. Have fun! :-)
 *
 * The gdb stub is entered (and thus the remote gdb gets control) in the
 * following situations:
 *
 *  - If breakpoint() is called. This is just after kgdb initialization, or if
 *    a breakpoint() call has been put somewhere into the kernel source.
 *    (Breakpoints can of course also be set the usual way in gdb.)
 *    In eLinux, we call breakpoint() in init/main.c after IRQ initialization.
 *
 *  - If there is a kernel exception, i.e. bad_super_trap() or die_if_kernel()
 *    are entered. All the CPU exceptions are mapped to (more or less..., see
 *    the hard_trap_info array below) appropriate signal, which are reported
 *    to gdb. die_if_kernel() is usually called after some kind of access
 *    error and thus is reported as SIGSEGV.
 *
 *  - When panic() is called. This is reported as SIGABRT.
 *
 *  - If C-c is received over the serial line, which is treated as
 *    SIGINT.
 *
 * Of course, all these signals are just faked for gdb, since there is no
 * signal concept as such for the kernel. It also isn't possible --obviously--
 * to set signal handlers from inside gdb, or restart the kernel with a
 * signal.
 *
 * Current limitations:
 *
 *  - While the kernel is stopped, interrupts are disabled for safety reasons
 *    (i.e., variables not changing magically or the like). But this also
 *    means that the clock isn't running anymore, and that interrupts from the
 *    hardware may get lost/not be served in time. This can cause some device
 *    errors...
 *
 *  - When single-stepping, only one instruction of the current thread is
 *    executed, but interrupts are allowed for that time and will be serviced
 *    if pending. Be prepared for that.
 *
 *  - All debugging happens in kernel virtual address space. There's no way to
 *    access physical memory not mapped in kernel space, or to access user
 *    space. A way to work around this is using get_user_long & Co. in gdb
 *    expressions, but only for the current process.
 *
 *  - Interrupting the kernel only works if interrupts are currently allowed,
 *    and the interrupt of the serial line isn't blocked by some other means
 *    (IPL too high, disabled, ...)
 *
 *  - The gdb stub is currently not reentrant, i.e. errors that happen therein
 *    (e.g. accessing invalid memory) may not be caught correctly. This could
 *    be removed in future by introducing a stack of struct registers.
 *
 */

/*
 *  To enable debugger support, two things need to happen.  One, a
 *  call to kgdb_init() is necessary in order to allow any breakpoints
 *  or error conditions to be properly intercepted and reported to gdb.
 *  Two, a breakpoint needs to be generated to begin communication.  This
 *  is most easily accomplished by a call to breakpoint(). 
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
 */


#include <linux/string.h>
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/linkage.h>
#include <linux/reboot.h>

#include <asm/setup.h>
#include <asm/ptrace.h>

#include <asm/arch/svinto.h>
#include <asm/irq.h>

static int kgdb_started = 0;

/********************************* Register image ****************************/
/* Use the order of registers as defined in "AXIS ETRAX CRIS Programmer's
   Reference", p. 1-1, with the additional register definitions of the
   ETRAX 100LX in cris-opc.h.
   There are 16 general 32-bit registers, R0-R15, where R14 is the stack
   pointer, SP, and R15 is the program counter, PC.
   There are 16 special registers, P0-P15, where three of the unimplemented
   registers, P0, P4 and P8, are reserved as zero-registers. A read from
   any of these registers returns zero and a write has no effect. */

typedef
struct register_image
{
	/* Offset */
	unsigned int     r0;   /* 0x00 */
	unsigned int     r1;   /* 0x04 */
	unsigned int     r2;   /* 0x08 */
	unsigned int     r3;   /* 0x0C */
	unsigned int     r4;   /* 0x10 */
	unsigned int     r5;   /* 0x14 */
	unsigned int     r6;   /* 0x18 */
	unsigned int     r7;   /* 0x1C */
	unsigned int     r8;   /* 0x20 Frame pointer */
	unsigned int     r9;   /* 0x24 */
	unsigned int    r10;   /* 0x28 */
	unsigned int    r11;   /* 0x2C */
	unsigned int    r12;   /* 0x30 */
	unsigned int    r13;   /* 0x34 */
	unsigned int     sp;   /* 0x38 Stack pointer */
	unsigned int     pc;   /* 0x3C Program counter */

        unsigned char    p0;   /* 0x40 8-bit zero-register */
	unsigned char    vr;   /* 0x41 Version register */

        unsigned short   p4;   /* 0x42 16-bit zero-register */
	unsigned short  ccr;   /* 0x44 Condition code register */
	
	unsigned int    mof;   /* 0x46 Multiply overflow register */
	
        unsigned int     p8;   /* 0x4A 32-bit zero-register */
	unsigned int    ibr;   /* 0x4E Interrupt base register */
	unsigned int    irp;   /* 0x52 Interrupt return pointer */
	unsigned int    srp;   /* 0x56 Subroutine return pointer */
	unsigned int    bar;   /* 0x5A Breakpoint address register */
	unsigned int   dccr;   /* 0x5E Double condition code register */
	unsigned int    brp;   /* 0x62 Breakpoint return pointer (pc in caller) */
	unsigned int    usp;   /* 0x66 User mode stack pointer */
} registers;

/************** Prototypes for local library functions ***********************/

/* Copy of strcpy from libc. */
static char *gdb_cris_strcpy (char *s1, const char *s2);

/* Copy of strlen from libc. */
static int gdb_cris_strlen (const char *s);

/* Copy of memchr from libc. */
static void *gdb_cris_memchr (const void *s, int c, int n);

/* Copy of strtol from libc. Does only support base 16. */
static int gdb_cris_strtol (const char *s, char **endptr, int base);

/********************** Prototypes for local functions. **********************/
/* Copy the content of a register image into another. The size n is
   the size of the register image. Due to struct assignment generation of
   memcpy in libc. */
static void copy_registers (registers *dptr, registers *sptr, int n);

/* Copy the stored registers from the stack. Put the register contents
   of thread thread_id in the struct reg. */
static void copy_registers_from_stack (int thread_id, registers *reg);

/* Copy the registers to the stack. Put the register contents of thread
   thread_id from struct reg to the stack. */
static void copy_registers_to_stack (int thread_id, registers *reg);

/* Write a value to a specified register regno in the register image
   of the current thread. */
static int write_register (int regno, char *val);

/* Write a value to a specified register in the stack of a thread other
   than the current thread. */
static write_stack_register (int thread_id, int regno, char *valptr);

/* Read a value from a specified register in the register image. Returns the
   status of the read operation. The register value is returned in valptr. */
static int read_register (char regno, unsigned int *valptr);

/* Serial port, reads one character. ETRAX 100 specific. from debugport.c */
int getDebugChar (void);

/* Serial port, writes one character. ETRAX 100 specific. from debugport.c */
void putDebugChar (int val);

void enableDebugIRQ (void);

/* Returns the character equivalent of a nibble, bit 7, 6, 5, and 4 of a byte,
   represented by int x. */
static char highhex (int x);

/* Returns the character equivalent of a nibble, bit 3, 2, 1, and 0 of a byte,
   represented by int x. */
static char lowhex (int x);

/* Returns the integer equivalent of a hexadecimal character. */
static int hex (char ch);

/* Convert the memory, pointed to by mem into hexadecimal representation.
   Put the result in buf, and return a pointer to the last character
   in buf (null). */
static char *mem2hex (char *buf, unsigned char *mem, int count);

/* Convert the array, in hexadecimal representation, pointed to by buf into
   binary representation. Put the result in mem, and return a pointer to
   the character after the last byte written. */
static unsigned char *hex2mem (unsigned char *mem, char *buf, int count);

/* Put the content of the array, in binary representation, pointed to by buf
   into memory pointed to by mem, and return a pointer to
   the character after the last byte written. */
static unsigned char *bin2mem (unsigned char *mem, unsigned char *buf, int count);

/* Await the sequence $<data>#<checksum> and store <data> in the array buffer
   returned. */
static void getpacket (char *buffer);

/* Send $<data>#<checksum> from the <data> in the array buffer. */
static void putpacket (char *buffer);

/* Build and send a response packet in order to inform the host the
   stub is stopped. */
static void stub_is_stopped (int sigval);

/* All expected commands are sent from remote.c. Send a response according
   to the description in remote.c. */
static void handle_exception (int sigval);

/* Performs a complete re-start from scratch. ETRAX specific. */
static void kill_restart (void);

/******************** Prototypes for global functions. ***********************/

/* The string str is prepended with the GDB printout token and sent. */
void putDebugString (const unsigned char *str, int length); /* used by etrax100ser.c */

/* The hook for both static (compiled) and dynamic breakpoints set by GDB.
   ETRAX 100 specific. */
void handle_breakpoint (void);                          /* used by irq.c */

/* The hook for an interrupt generated by GDB. ETRAX 100 specific. */
void handle_interrupt (void);                           /* used by irq.c */

/* A static breakpoint to be used at startup. */
void breakpoint (void);                                 /* called by init/main.c */

/* From osys_int.c, executing_task contains the number of the current
   executing task in osys. Does not know of object-oriented threads. */
extern unsigned char executing_task;

/* The number of characters used for a 64 bit thread identifier. */
#define HEXCHARS_IN_THREAD_ID 16

/* Avoid warning as the internal_stack is not used in the C-code. */
#define USEDVAR(name)    { if (name) { ; } }
#define USEDFUN(name) { void (*pf)(void) = (void *)name; USEDVAR(pf) }

/********************************** Packet I/O ******************************/
/* BUFMAX defines the maximum number of characters in
   inbound/outbound buffers */
#define BUFMAX 512

/* Run-length encoding maximum length. Send 64 at most. */
#define RUNLENMAX 64

/* Definition of all valid hexadecimal characters */
static const char hexchars[] = "0123456789abcdef";

/* The inbound/outbound buffers used in packet I/O */
static char remcomInBuffer[BUFMAX];
static char remcomOutBuffer[BUFMAX];

/* Error and warning messages. */
enum error_type
{
	SUCCESS, E01, E02, E03, E04, E05, E06, E07
};
static char *error_message[] =
{
	"",
	"E01 Set current or general thread - H[c,g] - internal error.",
	"E02 Change register content - P - cannot change read-only register.",
	"E03 Thread is not alive.", /* T, not used. */
	"E04 The command is not supported - [s,C,S,!,R,d,r] - internal error.",
	"E05 Change register content - P - the register is not implemented..",
	"E06 Change memory content - M - internal error.",
	"E07 Change register content - P - the register is not stored on the stack"
};
/********************************* Register image ****************************/
/* Use the order of registers as defined in "AXIS ETRAX CRIS Programmer's
   Reference", p. 1-1, with the additional register definitions of the
   ETRAX 100LX in cris-opc.h.
   There are 16 general 32-bit registers, R0-R15, where R14 is the stack
   pointer, SP, and R15 is the program counter, PC.
   There are 16 special registers, P0-P15, where three of the unimplemented
   registers, P0, P4 and P8, are reserved as zero-registers. A read from
   any of these registers returns zero and a write has no effect. */
enum register_name
{
	R0,  R1,   R2,  R3,
	R4,  R5,   R6,  R7,
	R8,  R9,   R10, R11,
	R12, R13,  SP,  PC,
	P0,  VR,   P2,  P3,
	P4,  CCR,  P6,  MOF,
	P8,  IBR,  IRP, SRP,
	BAR, DCCR, BRP, USP
};

/* The register sizes of the registers in register_name. An unimplemented register
   is designated by size 0 in this array. */
static int register_size[] =
{
	4, 4, 4, 4,
	4, 4, 4, 4,
	4, 4, 4, 4,
	4, 4, 4, 4,
	1, 1, 0, 0,
	2, 2, 0, 4,
	4, 4, 4, 4,
	4, 4, 4, 4
};

/* Contains the register image of the executing thread in the assembler
   part of the code in order to avoid horrible addressing modes. */
static registers reg;

/* FIXME: Should this be used? Delete otherwise. */
/* Contains the assumed consistency state of the register image. Uses the
   enum error_type for state information. */
static int consistency_status = SUCCESS;

/********************************** Handle exceptions ************************/
/* The variable reg contains the register image associated with the
   current_thread_c variable. It is a complete register image created at
   entry. The reg_g contains a register image of a task where the general
   registers are taken from the stack and all special registers are taken
   from the executing task. It is associated with current_thread_g and used
   in order to provide access mainly for 'g', 'G' and 'P'.
*/

/* Need two task id pointers in order to handle Hct and Hgt commands. */
static int current_thread_c = 0;
static int current_thread_g = 0;

/* Need two register images in order to handle Hct and Hgt commands. The
   variable reg_g is in addition to reg above. */
static registers reg_g;

/********************************** Breakpoint *******************************/
/* Use an internal stack in the breakpoint and interrupt response routines */
#define INTERNAL_STACK_SIZE 1024
static char internal_stack[INTERNAL_STACK_SIZE];

/* Due to the breakpoint return pointer, a state variable is needed to keep
   track of whether it is a static (compiled) or dynamic (gdb-invoked)
   breakpoint to be handled. A static breakpoint uses the content of register
   BRP as it is whereas a dynamic breakpoint requires subtraction with 2
   in order to execute the instruction. The first breakpoint is static. */
static unsigned char is_dyn_brkp = 0;

/********************************* String library ****************************/
/* Single-step over library functions creates trap loops. */

/* Copy char s2[] to s1[]. */
static char*
gdb_cris_strcpy (char *s1, const char *s2)
{
	char *s = s1;
	
	for (s = s1; (*s++ = *s2++) != '\0'; )
		;
	return (s1);
}

/* Find length of s[]. */
static int
gdb_cris_strlen (const char *s)
{
	const char *sc;
	
	for (sc = s; *sc != '\0'; sc++)
		;
	return (sc - s);
}

/* Find first occurrence of c in s[n]. */
static void*
gdb_cris_memchr (const void *s, int c, int n)
{
	const unsigned char uc = c;
	const unsigned char *su;
	
	for (su = s; 0 < n; ++su, --n)
		if (*su == uc)
			return ((void *)su);
	return (NULL);
}
/******************************* Standard library ****************************/
/* Single-step over library functions creates trap loops. */
/* Convert string to long. */
static int
gdb_cris_strtol (const char *s, char **endptr, int base)
{
	char *s1;
	char *sd;
	int x = 0;
	
	for (s1 = (char*)s; (sd = gdb_cris_memchr(hexchars, *s1, base)) != NULL; ++s1)
		x = x * base + (sd - hexchars);
        
        if (endptr)
        {
                /* Unconverted suffix is stored in endptr unless endptr is NULL. */
                *endptr = s1;
        }
        
	return x;
}

/********************************* Register image ****************************/
/* Copy the content of a register image into another. The size n is
   the size of the register image. Due to struct assignment generation of
   memcpy in libc. */
static void
copy_registers (registers *dptr, registers *sptr, int n)
{
	unsigned char *dreg;
	unsigned char *sreg;
	
	for (dreg = (unsigned char*)dptr, sreg = (unsigned char*)sptr; n > 0; n--)
		*dreg++ = *sreg++;
}

#ifdef PROCESS_SUPPORT
/* Copy the stored registers from the stack. Put the register contents
   of thread thread_id in the struct reg. */
static void
copy_registers_from_stack (int thread_id, registers *regptr)
{
	int j;
	stack_registers *s = (stack_registers *)stack_list[thread_id];
	unsigned int *d = (unsigned int *)regptr;
	
	for (j = 13; j >= 0; j--)
		*d++ = s->r[j];
	regptr->sp = (unsigned int)stack_list[thread_id];
	regptr->pc = s->pc;
	regptr->dccr = s->dccr;
	regptr->srp = s->srp;
}

/* Copy the registers to the stack. Put the register contents of thread
   thread_id from struct reg to the stack. */
static void
copy_registers_to_stack (int thread_id, registers *regptr)
{
	int i;
	stack_registers *d = (stack_registers *)stack_list[thread_id];
	unsigned int *s = (unsigned int *)regptr;
	
	for (i = 0; i < 14; i++) {
		d->r[i] = *s++;
	}
	d->pc = regptr->pc;
	d->dccr = regptr->dccr;
	d->srp = regptr->srp;
}
#endif

/* Write a value to a specified register in the register image of the current
   thread. Returns status code SUCCESS, E02 or E05. */
static int
write_register (int regno, char *val)
{
	int status = SUCCESS;
	registers *current_reg = &reg;

        if (regno >= R0 && regno <= PC) {
		/* 32-bit register with simple offset. */
		hex2mem ((unsigned char *)current_reg + regno * sizeof(unsigned int),
			 val, sizeof(unsigned int));
	}
        else if (regno == P0 || regno == VR || regno == P4 || regno == P8) {
		/* Do not support read-only registers. */
		status = E02;
	}
        else if (regno == CCR) {
		/* 16 bit register with complex offset. (P4 is read-only, P6 is not implemented, 
                   and P7 (MOF) is 32 bits in ETRAX 100LX. */
		hex2mem ((unsigned char *)&(current_reg->ccr) + (regno-CCR) * sizeof(unsigned short),
			 val, sizeof(unsigned short));
	}
	else if (regno >= MOF && regno <= USP) {
		/* 32 bit register with complex offset.  (P8 has been taken care of.) */
		hex2mem ((unsigned char *)&(current_reg->ibr) + (regno-IBR) * sizeof(unsigned int),
			 val, sizeof(unsigned int));
	} 
        else {
		/* Do not support nonexisting or unimplemented registers (P2, P3, and P6). */
		status = E05;
	}
	return status;
}

#ifdef PROCESS_SUPPORT
/* Write a value to a specified register in the stack of a thread other
   than the current thread. Returns status code SUCCESS or E07. */
static int
write_stack_register (int thread_id, int regno, char *valptr)
{
	int status = SUCCESS;
	stack_registers *d = (stack_registers *)stack_list[thread_id];
	unsigned int val;
	
	hex2mem ((unsigned char *)&val, valptr, sizeof(unsigned int));
	if (regno >= R0 && regno < SP) {
		d->r[regno] = val;
	}
	else if (regno == SP) {
		stack_list[thread_id] = val;
	}
	else if (regno == PC) {
		d->pc = val;
	}
	else if (regno == SRP) {
		d->srp = val;
	}
	else if (regno == DCCR) {
		d->dccr = val;
	}
	else {
		/* Do not support registers in the current thread. */
		status = E07;
	}
	return status;
}
#endif

/* Read a value from a specified register in the register image. Returns the
   value in the register or -1 for non-implemented registers.
   Should check consistency_status after a call which may be E05 after changes
   in the implementation. */
static int
read_register (char regno, unsigned int *valptr)
{
	registers *current_reg = &reg;

	if (regno >= R0 && regno <= PC) {
		/* 32-bit register with simple offset. */
		*valptr = *(unsigned int *)((char *)current_reg + regno * sizeof(unsigned int));
                return SUCCESS;
	}
	else if (regno == P0 || regno == VR) {
		/* 8 bit register with complex offset. */
		*valptr = (unsigned int)(*(unsigned char *)
                                         ((char *)&(current_reg->p0) + (regno-P0) * sizeof(char)));
                return SUCCESS;
	}
	else if (regno == P4 || regno == CCR) {
		/* 16 bit register with complex offset. */
		*valptr = (unsigned int)(*(unsigned short *)
                                         ((char *)&(current_reg->p4) + (regno-P4) * sizeof(unsigned short)));
                return SUCCESS;
	}
	else if (regno >= MOF && regno <= USP) {
		/* 32 bit register with complex offset. */
		*valptr = *(unsigned int *)((char *)&(current_reg->p8)
                                            + (regno-P8) * sizeof(unsigned int));
                return SUCCESS;
	}
	else {
		/* Do not support nonexisting or unimplemented registers (P2, P3, and P6). */
		consistency_status = E05;
		return E05;
	}
}

/********************************** Packet I/O ******************************/
/* Returns the character equivalent of a nibble, bit 7, 6, 5, and 4 of a byte,
   represented by int x. */
static inline char
highhex(int x)
{
	return hexchars[(x >> 4) & 0xf];
}

/* Returns the character equivalent of a nibble, bit 3, 2, 1, and 0 of a byte,
   represented by int x. */
static inline char
lowhex(int x)
{
	return hexchars[x & 0xf];
}

/* Returns the integer equivalent of a hexadecimal character. */
static int
hex (char ch)
{
	if ((ch >= 'a') && (ch <= 'f'))
		return (ch - 'a' + 10);
	if ((ch >= '0') && (ch <= '9'))
		return (ch - '0');
	if ((ch >= 'A') && (ch <= 'F'))
		return (ch - 'A' + 10);
	return (-1);
}

/* Convert the memory, pointed to by mem into hexadecimal representation.
   Put the result in buf, and return a pointer to the last character
   in buf (null). */

static int do_printk = 0;

static char *
mem2hex(char *buf, unsigned char *mem, int count)
{
	int i;
	int ch;
        
        if (mem == NULL) {
                /* Bogus read from m0. FIXME: What constitutes a valid address? */
                for (i = 0; i < count; i++) {
                        *buf++ = '0';
                        *buf++ = '0';
                }
        } else {
                /* Valid mem address. */
                for (i = 0; i < count; i++) {
                        ch = *mem++;
                        *buf++ = highhex (ch);
                        *buf++ = lowhex (ch);
                }
        }
        
        /* Terminate properly. */
	*buf = '\0';
	return (buf);
}

/* Convert the array, in hexadecimal representation, pointed to by buf into
   binary representation. Put the result in mem, and return a pointer to
   the character after the last byte written. */
static unsigned char*
hex2mem (unsigned char *mem, char *buf, int count)
{
	int i;
	unsigned char ch;
	for (i = 0; i < count; i++) {
		ch = hex (*buf++) << 4;
		ch = ch + hex (*buf++);
		*mem++ = ch;
	}
	return (mem);
}

/* Put the content of the array, in binary representation, pointed to by buf
   into memory pointed to by mem, and return a pointer to the character after
   the last byte written.
   Gdb will escape $, #, and the escape char (0x7d). */
static unsigned char*
bin2mem (unsigned char *mem, unsigned char *buf, int count)
{
	int i;
	unsigned char *next;
	for (i = 0; i < count; i++) {
		/* Check for any escaped characters. Be paranoid and
		   only unescape chars that should be escaped. */
		if (*buf == 0x7d) {
			next = buf + 1;
			if (*next == 0x3 || *next == 0x4 || *next == 0x5D) /* #, $, ESC */
				{
					buf++;
					*buf += 0x20;
				}
		}
		*mem++ = *buf++;
	}
	return (mem);
}

/* Await the sequence $<data>#<checksum> and store <data> in the array buffer
   returned. */
static void
getpacket (char *buffer)
{
	unsigned char checksum;
	unsigned char xmitcsum;
	int i;
	int count;
	char ch;
	do {
		while ((ch = getDebugChar ()) != '$')
			/* Wait for the start character $ and ignore all other characters */;
		checksum = 0;
		xmitcsum = -1;
		count = 0;
		/* Read until a # or the end of the buffer is reached */
		while (count < BUFMAX) {
			ch = getDebugChar ();
			if (ch == '#')
				break;
			checksum = checksum + ch;
			buffer[count] = ch;
			count = count + 1;
		}
		buffer[count] = '\0';
		
		if (ch == '#') {
			xmitcsum = hex (getDebugChar ()) << 4;
			xmitcsum += hex (getDebugChar ());
			if (checksum != xmitcsum) {
				/* Wrong checksum */
				putDebugChar ('-');
			}
			else {
				/* Correct checksum */
				putDebugChar ('+');
				/* If sequence characters are received, reply with them */
				if (buffer[2] == ':') {
					putDebugChar (buffer[0]);
					putDebugChar (buffer[1]);
					/* Remove the sequence characters from the buffer */
					count = gdb_cris_strlen (buffer);
					for (i = 3; i <= count; i++)
						buffer[i - 3] = buffer[i];
				}
			}
		}
	} while (checksum != xmitcsum);
}

/* Send $<data>#<checksum> from the <data> in the array buffer. */

static void
putpacket(char *buffer)
{
	int checksum;
	int runlen;
	int encode;
	
	do {
		char *src = buffer;
		putDebugChar ('$');
		checksum = 0;
		while (*src) {
			/* Do run length encoding */
			putDebugChar (*src);
			checksum += *src;
			runlen = 0;
			while (runlen < RUNLENMAX && *src == src[runlen]) {
				runlen++;
			}
			if (runlen > 3) {
				/* Got a useful amount */
				putDebugChar ('*');
				checksum += '*';
				encode = runlen + ' ' - 4;
				putDebugChar (encode);
				checksum += encode;
				src += runlen;
			}
			else {
				src++;
			}
		}
		putDebugChar ('#');
		putDebugChar (highhex (checksum));
		putDebugChar (lowhex (checksum));
	} while(kgdb_started && (getDebugChar() != '+'));
}

/* The string str is prepended with the GDB printout token and sent. Required
   in traditional implementations. */
void
putDebugString (const unsigned char *str, int length)
{
        remcomOutBuffer[0] = 'O';
        mem2hex(&remcomOutBuffer[1], (unsigned char *)str, length);
        putpacket(remcomOutBuffer);
}

/********************************** Handle exceptions ************************/
/* Build and send a response packet in order to inform the host the
   stub is stopped. TAAn...:r...;n...:r...;n...:r...;
                    AA = signal number
                    n... = register number (hex)
                    r... = register contents
                    n... = `thread'
                    r... = thread process ID.  This is a hex integer.
                    n... = other string not starting with valid hex digit.
                    gdb should ignore this n,r pair and go on to the next.
                    This way we can extend the protocol. */
static void
stub_is_stopped(int sigval)
{
	char *ptr = remcomOutBuffer;
	int regno;

	unsigned int reg_cont;
	int status;
        
	/* Send trap type (converted to signal) */

	*ptr++ = 'T';	
	*ptr++ = highhex (sigval);
	*ptr++ = lowhex (sigval);

	/* Send register contents. We probably only need to send the
	 * PC, frame pointer and stack pointer here. Other registers will be
	 * explicitely asked for. But for now, send all. 
	 */
	
	for (regno = R0; regno <= USP; regno++) {
		/* Store n...:r...; for the registers in the buffer. */

                status = read_register (regno, &reg_cont);
                
		if (status == SUCCESS) {
                        
                        *ptr++ = highhex (regno);
                        *ptr++ = lowhex (regno);
                        *ptr++ = ':';

                        ptr = mem2hex(ptr, (unsigned char *)&reg_cont,
                                      register_size[regno]);
                        *ptr++ = ';';
                }
                
	}

#ifdef PROCESS_SUPPORT
	/* Store the registers of the executing thread. Assume that both step,
	   continue, and register content requests are with respect to this
	   thread. The executing task is from the operating system scheduler. */

	current_thread_c = executing_task;
	current_thread_g = executing_task;

	/* A struct assignment translates into a libc memcpy call. Avoid
	   all libc functions in order to prevent recursive break points. */
	copy_registers (&reg_g, &reg, sizeof(registers));

	/* Store thread:r...; with the executing task TID. */
	gdb_cris_strcpy (&remcomOutBuffer[pos], "thread:");
	pos += gdb_cris_strlen ("thread:");
	remcomOutBuffer[pos++] = highhex (executing_task);
	remcomOutBuffer[pos++] = lowhex (executing_task);
	gdb_cris_strcpy (&remcomOutBuffer[pos], ";");
#endif

	/* null-terminate and send it off */

	*ptr = 0;

	putpacket (remcomOutBuffer);
}

/* All expected commands are sent from remote.c. Send a response according
   to the description in remote.c. */
static void
handle_exception (int sigval)
{
	/* Avoid warning of not used. */

	USEDFUN(handle_exception);
	USEDVAR(internal_stack[0]);

	/* Send response. */

	stub_is_stopped (sigval);

	for (;;) {
		remcomOutBuffer[0] = '\0';
		getpacket (remcomInBuffer);
		switch (remcomInBuffer[0]) {
			case 'g':
				/* Read registers: g
				   Success: Each byte of register data is described by two hex digits.
				   Registers are in the internal order for GDB, and the bytes
				   in a register  are in the same order the machine uses.
				   Failure: void. */
				
				{
#ifdef PROCESS_SUPPORT
					/* Use the special register content in the executing thread. */
					copy_registers (&reg_g, &reg, sizeof(registers));
					/* Replace the content available on the stack. */
					if (current_thread_g != executing_task) {
						copy_registers_from_stack (current_thread_g, &reg_g);
					}
					mem2hex ((unsigned char *)remcomOutBuffer, (unsigned char *)&reg_g, sizeof(registers));
#else
					mem2hex(remcomOutBuffer, (char *)&reg, sizeof(registers));
#endif
				}
				break;
				
			case 'G':
				/* Write registers. GXX..XX
				   Each byte of register data  is described by two hex digits.
				   Success: OK
				   Failure: void. */
#ifdef PROCESS_SUPPORT
				hex2mem ((unsigned char *)&reg_g, &remcomInBuffer[1], sizeof(registers));
				if (current_thread_g == executing_task) {
					copy_registers (&reg, &reg_g, sizeof(registers));
				}
				else {
					copy_registers_to_stack(current_thread_g, &reg_g);
				}
#else
				hex2mem((char *)&reg, &remcomInBuffer[1], sizeof(registers));
#endif
				gdb_cris_strcpy (remcomOutBuffer, "OK");
				break;
				
			case 'P':
				/* Write register. Pn...=r...
				   Write register n..., hex value without 0x, with value r...,
				   which contains a hex value without 0x and two hex digits
				   for each byte in the register (target byte order). P1f=11223344 means
				   set register 31 to 44332211.
				   Success: OK
				   Failure: E02, E05 */
				{
					char *suffix;
					int regno = gdb_cris_strtol (&remcomInBuffer[1], &suffix, 16);
					int status;
#ifdef PROCESS_SUPPORT
					if (current_thread_g != executing_task)
						status = write_stack_register (current_thread_g, regno, suffix+1);
					else
#endif
						status = write_register (regno, suffix+1);

					switch (status) {
						case E02:
							/* Do not support read-only registers. */
							gdb_cris_strcpy (remcomOutBuffer, error_message[E02]);
							break;
						case E05:
							/* Do not support non-existing registers. */
							gdb_cris_strcpy (remcomOutBuffer, error_message[E05]);
							break;
						case E07:
							/* Do not support non-existing registers on the stack. */
							gdb_cris_strcpy (remcomOutBuffer, error_message[E07]);
							break;
						default:
							/* Valid register number. */
							gdb_cris_strcpy (remcomOutBuffer, "OK");
							break;
					}
				}
				break;
				
			case 'm':
				/* Read from memory. mAA..AA,LLLL
				   AA..AA is the address and LLLL is the length.
				   Success: XX..XX is the memory content.  Can be fewer bytes than
				   requested if only part of the data may be read. m6000120a,6c means
				   retrieve 108 byte from base address 6000120a.
				   Failure: void. */
				{
                                        char *suffix;
					unsigned char *addr = (unsigned char *)gdb_cris_strtol(&remcomInBuffer[1],
                                                                                               &suffix, 16);                                        int length = gdb_cris_strtol(suffix+1, 0, 16);
                                        
                                        mem2hex(remcomOutBuffer, addr, length);
                                }
				break;
				
			case 'X':
				/* Write to memory. XAA..AA,LLLL:XX..XX
				   AA..AA is the start address,  LLLL is the number of bytes, and
				   XX..XX is the binary data.
				   Success: OK
				   Failure: void. */
			case 'M':
				/* Write to memory. MAA..AA,LLLL:XX..XX
				   AA..AA is the start address,  LLLL is the number of bytes, and
				   XX..XX is the hexadecimal data.
				   Success: OK
				   Failure: void. */
				{
					char *lenptr;
					char *dataptr;
					unsigned char *addr = (unsigned char *)gdb_cris_strtol(&remcomInBuffer[1],
										      &lenptr, 16);
					int length = gdb_cris_strtol(lenptr+1, &dataptr, 16);
					if (*lenptr == ',' && *dataptr == ':') {
						if (remcomInBuffer[0] == 'M') {
							hex2mem(addr, dataptr + 1, length);
						}
						else /* X */ {
							bin2mem(addr, dataptr + 1, length);
						}
						gdb_cris_strcpy (remcomOutBuffer, "OK");
					}
					else {
						gdb_cris_strcpy (remcomOutBuffer, error_message[E06]);
					}
				}
				break;
				
			case 'c':
				/* Continue execution. cAA..AA
				   AA..AA is the address where execution is resumed. If AA..AA is
				   omitted, resume at the present address.
				   Success: return to the executing thread.
				   Failure: will never know. */
				if (remcomInBuffer[1] != '\0') {
					reg.pc = gdb_cris_strtol (&remcomInBuffer[1], 0, 16);
				}
				enableDebugIRQ();
				return;
				
			case 's':
				/* Step. sAA..AA
				   AA..AA is the address where execution is resumed. If AA..AA is
				   omitted, resume at the present address. Success: return to the
				   executing thread. Failure: will never know.
				   
				   Should never be invoked. The single-step is implemented on
				   the host side. If ever invoked, it is an internal error E04. */
				gdb_cris_strcpy (remcomOutBuffer, error_message[E04]);
				putpacket (remcomOutBuffer);
				return;
				
			case '?':
				/* The last signal which caused a stop. ?
				   Success: SAA, where AA is the signal number.
				   Failure: void. */
				remcomOutBuffer[0] = 'S';
				remcomOutBuffer[1] = highhex (sigval);
				remcomOutBuffer[2] = lowhex (sigval);
				remcomOutBuffer[3] = 0;
				break;
				
			case 'D':
				/* Detach from host. D
				   Success: OK, and return to the executing thread.
				   Failure: will never know */
				putpacket ("OK");
				return;
				
			case 'k':
			case 'r':
				/* kill request or reset request.
				   Success: restart of target.
				   Failure: will never know. */
				kill_restart ();
				break;
				
			case 'C':
			case 'S':
			case '!':
			case 'R':
			case 'd':
				/* Continue with signal sig. Csig;AA..AA
				   Step with signal sig. Ssig;AA..AA
				   Use the extended remote protocol. !
				   Restart the target system. R0
				   Toggle debug flag. d
				   Search backwards. tAA:PP,MM
				   Not supported: E04 */
				gdb_cris_strcpy (remcomOutBuffer, error_message[E04]);
				break;
#ifdef PROCESS_SUPPORT

			case 'T':
				/* Thread alive. TXX
				   Is thread XX alive?
				   Success: OK, thread XX is alive.
				   Failure: E03, thread XX is dead. */
				{
					int thread_id = (int)gdb_cris_strtol (&remcomInBuffer[1], 0, 16);
					/* Cannot tell whether it is alive or not. */
					if (thread_id >= 0 && thread_id < number_of_tasks)
						gdb_cris_strcpy (remcomOutBuffer, "OK");
				}
				break;
								
			case 'H':
				/* Set thread for subsequent operations: Hct
				   c = 'c' for thread used in step and continue;
				   t can be -1 for all threads.
				   c = 'g' for thread used in other  operations.
				   t = 0 means pick any thread.
				   Success: OK
				   Failure: E01 */
				{
					int thread_id = gdb_cris_strtol (&remcomInBuffer[2], 0, 16);
					if (remcomInBuffer[1] == 'c') {
						/* c = 'c' for thread used in step and continue */
						/* Do not change current_thread_c here. It would create a mess in
						   the scheduler. */
						gdb_cris_strcpy (remcomOutBuffer, "OK");
					}
					else if (remcomInBuffer[1] == 'g') {
						/* c = 'g' for thread used in other  operations.
						   t = 0 means pick any thread. Impossible since the scheduler does
						   not allow that. */
						if (thread_id >= 0 && thread_id < number_of_tasks) {
							current_thread_g = thread_id;
							gdb_cris_strcpy (remcomOutBuffer, "OK");
						}
						else {
							/* Not expected - send an error message. */
							gdb_cris_strcpy (remcomOutBuffer, error_message[E01]);
						}
					}
					else {
						/* Not expected - send an error message. */
						gdb_cris_strcpy (remcomOutBuffer, error_message[E01]);
					}
				}
				break;
				
			case 'q':
			case 'Q':
				/* Query of general interest. qXXXX
				   Set general value XXXX. QXXXX=yyyy */
				{
					int pos;
					int nextpos;
					int thread_id;
					
					switch (remcomInBuffer[1]) {
						case 'C':
							/* Identify the remote current thread. */
							gdb_cris_strcpy (&remcomOutBuffer[0], "QC");
							remcomOutBuffer[2] = highhex (current_thread_c);
							remcomOutBuffer[3] = lowhex (current_thread_c);
							remcomOutBuffer[4] = '\0';
							break;
						case 'L':
							gdb_cris_strcpy (&remcomOutBuffer[0], "QM");
							/* Reply with number of threads. */
							if (os_is_started()) {
								remcomOutBuffer[2] = highhex (number_of_tasks);
								remcomOutBuffer[3] = lowhex (number_of_tasks);
							}
							else {
								remcomOutBuffer[2] = highhex (0);
								remcomOutBuffer[3] = lowhex (1);
							}
							/* Done with the reply. */
							remcomOutBuffer[4] = lowhex (1);
							pos = 5;
							/* Expects the argument thread id. */
							for (; pos < (5 + HEXCHARS_IN_THREAD_ID); pos++)
								remcomOutBuffer[pos] = remcomInBuffer[pos];
							/* Reply with the thread identifiers. */
							if (os_is_started()) {
								/* Store the thread identifiers of all tasks. */
								for (thread_id = 0; thread_id < number_of_tasks; thread_id++) {
									nextpos = pos + HEXCHARS_IN_THREAD_ID - 1;
									for (; pos < nextpos; pos ++)
										remcomOutBuffer[pos] = lowhex (0);
									remcomOutBuffer[pos++] = lowhex (thread_id);
								}
							}
							else {
								/* Store the thread identifier of the boot task. */
								nextpos = pos + HEXCHARS_IN_THREAD_ID - 1;
								for (; pos < nextpos; pos ++)
									remcomOutBuffer[pos] = lowhex (0);
								remcomOutBuffer[pos++] = lowhex (current_thread_c);
							}
							remcomOutBuffer[pos] = '\0';
							break;
						default:
							/* Not supported: "" */
							/* Request information about section offsets: qOffsets. */
							remcomOutBuffer[0] = 0;
							break;
					}
				}
				break;
#endif /* PROCESS_SUPPORT */
				
			default:
				/* The stub should ignore other request and send an empty
				   response ($#<checksum>). This way we can extend the protocol and GDB
				   can tell whether the stub it is talking to uses the old or the new. */
				remcomOutBuffer[0] = 0;
				break;
		}
		putpacket(remcomOutBuffer);
	}
}

/* Performs a complete re-start from scratch. */
static void
kill_restart ()
{
	machine_restart("");
}

/********************************** Breakpoint *******************************/
/* The hook for both a static (compiled) and a dynamic breakpoint set by GDB.
   An internal stack is used by the stub. The register image of the caller is
   stored in the structure register_image.
   Interactive communication with the host is handled by handle_exception and
   finally the register image is restored. */

void kgdb_handle_breakpoint(void);

asm ("
  .global kgdb_handle_breakpoint
kgdb_handle_breakpoint:
;;
;; Response to the break-instruction
;;
;; Create a register image of the caller
;;
  move     $dccr,[reg+0x5E] ; Save the flags in DCCR before disable interrupts
  di                        ; Disable interrupts
  move.d   $r0,[reg]        ; Save R0
  move.d   $r1,[reg+0x04]   ; Save R1
  move.d   $r2,[reg+0x08]   ; Save R2
  move.d   $r3,[reg+0x0C]   ; Save R3
  move.d   $r4,[reg+0x10]   ; Save R4
  move.d   $r5,[reg+0x14]   ; Save R5
  move.d   $r6,[reg+0x18]   ; Save R6
  move.d   $r7,[reg+0x1C]   ; Save R7
  move.d   $r8,[reg+0x20]   ; Save R8
  move.d   $r9,[reg+0x24]   ; Save R9
  move.d   $r10,[reg+0x28]  ; Save R10
  move.d   $r11,[reg+0x2C]  ; Save R11
  move.d   $r12,[reg+0x30]  ; Save R12
  move.d   $r13,[reg+0x34]  ; Save R13
  move.d   $sp,[reg+0x38]   ; Save SP (R14)
;; Due to the old assembler-versions BRP might not be recognized
  .word 0xE670              ; move brp,$r0
  subq     2,$r0             ; Set to address of previous instruction.
  move.d   $r0,[reg+0x3c]   ; Save the address in PC (R15)
  clear.b  [reg+0x40]      ; Clear P0
  move     $vr,[reg+0x41]   ; Save special register P1
  clear.w  [reg+0x42]      ; Clear P4
  move     $ccr,[reg+0x44]  ; Save special register CCR
  move     $mof,[reg+0x46]  ; P7
  clear.d  [reg+0x4A]      ; Clear P8
  move     $ibr,[reg+0x4E]  ; P9,
  move     $irp,[reg+0x52]  ; P10,
  move     $srp,[reg+0x56]  ; P11,
  move     $dtp0,[reg+0x5A] ; P12, register BAR, assembler might not know BAR
                            ; P13, register DCCR already saved
;; Due to the old assembler-versions BRP might not be recognized
  .word 0xE670              ; move brp,r0
;; Static (compiled) breakpoints must return to the next instruction in order
;; to avoid infinite loops. Dynamic (gdb-invoked) must restore the instruction
;; in order to execute it when execution is continued.
  test.b   [is_dyn_brkp]    ; Is this a dynamic breakpoint?
  beq      is_static         ; No, a static breakpoint
  nop
  subq     2,$r0              ; rerun the instruction the break replaced
is_static:
  moveq    1,$r1
  move.b   $r1,[is_dyn_brkp] ; Set the state variable to dynamic breakpoint
  move.d   $r0,[reg+0x62]    ; Save the return address in BRP
  move     $usp,[reg+0x66]   ; USP
;;
;; Handle the communication
;;
  move.d   internal_stack+1020,$sp ; Use the internal stack which grows upward
  moveq    5,$r10                   ; SIGTRAP
  jsr      handle_exception       ; Interactive routine
;;
;; Return to the caller
;;
   move.d  [reg],$r0         ; Restore R0
   move.d  [reg+0x04],$r1    ; Restore R1
   move.d  [reg+0x08],$r2    ; Restore R2
   move.d  [reg+0x0C],$r3    ; Restore R3
   move.d  [reg+0x10],$r4    ; Restore R4
   move.d  [reg+0x14],$r5    ; Restore R5
   move.d  [reg+0x18],$r6    ; Restore R6
   move.d  [reg+0x1C],$r7    ; Restore R7
   move.d  [reg+0x20],$r8    ; Restore R8
   move.d  [reg+0x24],$r9    ; Restore R9
   move.d  [reg+0x28],$r10   ; Restore R10
   move.d  [reg+0x2C],$r11   ; Restore R11
   move.d  [reg+0x30],$r12   ; Restore R12
   move.d  [reg+0x34],$r13   ; Restore R13
;;
;; FIXME: Which registers should be restored?
;;
   move.d  [reg+0x38],$sp    ; Restore SP (R14)
   move    [reg+0x56],$srp   ; Restore the subroutine return pointer.
   move    [reg+0x5E],$dccr  ; Restore DCCR
   move    [reg+0x66],$usp   ; Restore USP
   jump    [reg+0x62]       ; A jump to the content in register BRP works.
   nop                       ;
");

/* The hook for an interrupt generated by GDB. An internal stack is used
   by the stub. The register image of the caller is stored in the structure
   register_image. Interactive communication with the host is handled by
   handle_exception and finally the register image is restored. Due to the
   old assembler which does not recognise the break instruction and the
   breakpoint return pointer hex-code is used. */

void kgdb_handle_serial(void);

asm ("
  .global kgdb_handle_serial
kgdb_handle_serial:
;;
;; Response to a serial interrupt
;;

  move     $dccr,[reg+0x5E] ; Save the flags in DCCR
  di                        ; Disable interrupts
  move.d   $r0,[reg]        ; Save R0
  move.d   $r1,[reg+0x04]   ; Save R1
  move.d   $r2,[reg+0x08]   ; Save R2
  move.d   $r3,[reg+0x0C]   ; Save R3
  move.d   $r4,[reg+0x10]   ; Save R4
  move.d   $r5,[reg+0x14]   ; Save R5
  move.d   $r6,[reg+0x18]   ; Save R6
  move.d   $r7,[reg+0x1C]   ; Save R7
  move.d   $r8,[reg+0x20]   ; Save R8
  move.d   $r9,[reg+0x24]   ; Save R9
  move.d   $r10,[reg+0x28]  ; Save R10
  move.d   $r11,[reg+0x2C]  ; Save R11
  move.d   $r12,[reg+0x30]  ; Save R12
  move.d   $r13,[reg+0x34]  ; Save R13
  move.d   $sp,[reg+0x38]   ; Save SP (R14)
  move     $irp,[reg+0x3c]  ; Save the address in PC (R15)
  clear.b  [reg+0x40]      ; Clear P0
  move     $vr,[reg+0x41]   ; Save special register P1,
  clear.w  [reg+0x42]      ; Clear P4
  move     $ccr,[reg+0x44]  ; Save special register CCR
  move     $mof,[reg+0x46]  ; P7
  clear.d  [reg+0x4A]      ; Clear P8
  move     $ibr,[reg+0x4E]  ; P9,
  move     $irp,[reg+0x52]  ; P10,
  move     $srp,[reg+0x56]  ; P11,
  move     $dtp0,[reg+0x5A] ; P12, register BAR, assembler might not know BAR
                            ; P13, register DCCR already saved
;; Due to the old assembler-versions BRP might not be recognized
  .word 0xE670              ; move brp,r0
  move.d   $r0,[reg+0x62]   ; Save the return address in BRP
  move     $usp,[reg+0x66]  ; USP

;; get the serial character (from debugport.c) and check if it is a ctrl-c

  jsr getDebugChar
  cmp.b 3, $r10
  bne goback
  nop

  move.d  [reg+0x5E], $r10		; Get DCCR
  btstq	   8, $r10			; Test the U-flag.
  bmi	   goback
  nop

;;
;; Handle the communication
;;
  move.d   internal_stack+1020,$sp ; Use the internal stack
  moveq    2,$r10                   ; SIGINT
  jsr      handle_exception       ; Interactive routine

goback:
;;
;; Return to the caller
;;
   move.d  [reg],$r0         ; Restore R0
   move.d  [reg+0x04],$r1    ; Restore R1
   move.d  [reg+0x08],$r2    ; Restore R2
   move.d  [reg+0x0C],$r3    ; Restore R3
   move.d  [reg+0x10],$r4    ; Restore R4
   move.d  [reg+0x14],$r5    ; Restore R5
   move.d  [reg+0x18],$r6    ; Restore R6
   move.d  [reg+0x1C],$r7    ; Restore R7
   move.d  [reg+0x20],$r8    ; Restore R8
   move.d  [reg+0x24],$r9    ; Restore R9
   move.d  [reg+0x28],$r10   ; Restore R10
   move.d  [reg+0x2C],$r11   ; Restore R11
   move.d  [reg+0x30],$r12   ; Restore R12
   move.d  [reg+0x34],$r13   ; Restore R13
;;
;; FIXME: Which registers should be restored?
;;
   move.d  [reg+0x38],$sp    ; Restore SP (R14)
   move    [reg+0x56],$srp   ; Restore the subroutine return pointer.
   move    [reg+0x5E],$dccr  ; Restore DCCR
   move    [reg+0x66],$usp   ; Restore USP
   reti                      ; Return from the interrupt routine
   nop
");

/* Use this static breakpoint in the start-up only. */

void
breakpoint(void)
{
	kgdb_started = 1;
	is_dyn_brkp = 0;     /* This is a static, not a dynamic breakpoint. */
	__asm__ volatile ("break 8"); /* Jump to handle_breakpoint. */
}

/* initialize kgdb. doesn't break into the debugger, but sets up irq and ports */

void
kgdb_init(void)
{
	/* could initialize debug port as well but it's done in head.S already... */

        /* breakpoint handler is now set in irq.c */
	set_int_vector(8, kgdb_handle_serial);
	
	enableDebugIRQ();
}

/****************************** End of file **********************************/

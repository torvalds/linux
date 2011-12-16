/*
 *  arch/cris/arch-v32/kernel/kgdb.c
 *
 *  CRIS v32 version by Orjan Friberg, Axis Communications AB.
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *
 *  Originally written by Glenn Engel, Lake Stevens Instrument Division
 *
 *  Contributed by HP Systems
 *
 *  Modified for SPARC by Stu Grossman, Cygnus Support.
 *
 *  Modified for Linux/MIPS (and MIPS in general) by Andreas Busse
 *  Send complaints, suggestions etc. to <andy@waldorf-gmbh.de>
 *
 *  Copyright (C) 1995 Andreas Busse
 */

/* FIXME: Check the documentation. */

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

#include <asm/irq.h>
#include <hwregs/reg_map.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/intr_vect_defs.h>
#include <hwregs/ser_defs.h>

/* From entry.S. */
extern void gdb_handle_exception(void);
/* From kgdb_asm.S. */
extern void kgdb_handle_exception(void);

static int kgdb_started = 0;

/********************************* Register image ****************************/

typedef
struct register_image
{
	                      /* Offset */
	unsigned int   r0;    /* 0x00 */
	unsigned int   r1;    /* 0x04 */
	unsigned int   r2;    /* 0x08 */
	unsigned int   r3;    /* 0x0C */
	unsigned int   r4;    /* 0x10 */
	unsigned int   r5;    /* 0x14 */
	unsigned int   r6;    /* 0x18 */
	unsigned int   r7;    /* 0x1C */
	unsigned int   r8;    /* 0x20; Frame pointer (if any) */
	unsigned int   r9;    /* 0x24 */
	unsigned int   r10;   /* 0x28 */
	unsigned int   r11;   /* 0x2C */
	unsigned int   r12;   /* 0x30 */
	unsigned int   r13;   /* 0x34 */
	unsigned int   sp;    /* 0x38; R14, Stack pointer */
	unsigned int   acr;   /* 0x3C; R15, Address calculation register. */

	unsigned char  bz;    /* 0x40; P0, 8-bit zero register */
	unsigned char  vr;    /* 0x41; P1, Version register (8-bit) */
	unsigned int   pid;   /* 0x42; P2, Process ID */
	unsigned char  srs;   /* 0x46; P3, Support register select (8-bit) */
        unsigned short wz;    /* 0x47; P4, 16-bit zero register */
	unsigned int   exs;   /* 0x49; P5, Exception status */
	unsigned int   eda;   /* 0x4D; P6, Exception data address */
	unsigned int   mof;   /* 0x51; P7, Multiply overflow register */
	unsigned int   dz;    /* 0x55; P8, 32-bit zero register */
	unsigned int   ebp;   /* 0x59; P9, Exception base pointer */
	unsigned int   erp;   /* 0x5D; P10, Exception return pointer. Contains the PC we are interested in. */
	unsigned int   srp;   /* 0x61; P11, Subroutine return pointer */
	unsigned int   nrp;   /* 0x65; P12, NMI return pointer */
	unsigned int   ccs;   /* 0x69; P13, Condition code stack */
	unsigned int   usp;   /* 0x6D; P14, User mode stack pointer */
	unsigned int   spc;   /* 0x71; P15, Single step PC */
	unsigned int   pc;    /* 0x75; Pseudo register (for the most part set to ERP). */

} registers;

typedef
struct bp_register_image
{
	/* Support register bank 0. */
	unsigned int   s0_0;
	unsigned int   s1_0;
	unsigned int   s2_0;
	unsigned int   s3_0;
	unsigned int   s4_0;
	unsigned int   s5_0;
	unsigned int   s6_0;
	unsigned int   s7_0;
	unsigned int   s8_0;
	unsigned int   s9_0;
	unsigned int   s10_0;
	unsigned int   s11_0;
	unsigned int   s12_0;
	unsigned int   s13_0;
	unsigned int   s14_0;
	unsigned int   s15_0;

	/* Support register bank 1. */
	unsigned int   s0_1;
	unsigned int   s1_1;
	unsigned int   s2_1;
	unsigned int   s3_1;
	unsigned int   s4_1;
	unsigned int   s5_1;
	unsigned int   s6_1;
	unsigned int   s7_1;
	unsigned int   s8_1;
	unsigned int   s9_1;
	unsigned int   s10_1;
	unsigned int   s11_1;
	unsigned int   s12_1;
	unsigned int   s13_1;
	unsigned int   s14_1;
	unsigned int   s15_1;

	/* Support register bank 2. */
	unsigned int   s0_2;
	unsigned int   s1_2;
	unsigned int   s2_2;
	unsigned int   s3_2;
	unsigned int   s4_2;
	unsigned int   s5_2;
	unsigned int   s6_2;
	unsigned int   s7_2;
	unsigned int   s8_2;
	unsigned int   s9_2;
	unsigned int   s10_2;
	unsigned int   s11_2;
	unsigned int   s12_2;
	unsigned int   s13_2;
	unsigned int   s14_2;
	unsigned int   s15_2;

	/* Support register bank 3. */
	unsigned int   s0_3; /* BP_CTRL */
	unsigned int   s1_3; /* BP_I0_START */
	unsigned int   s2_3; /* BP_I0_END */
	unsigned int   s3_3; /* BP_D0_START */
	unsigned int   s4_3; /* BP_D0_END */
	unsigned int   s5_3; /* BP_D1_START */
	unsigned int   s6_3; /* BP_D1_END */
	unsigned int   s7_3; /* BP_D2_START */
	unsigned int   s8_3; /* BP_D2_END */
	unsigned int   s9_3; /* BP_D3_START */
	unsigned int   s10_3; /* BP_D3_END */
	unsigned int   s11_3; /* BP_D4_START */
	unsigned int   s12_3; /* BP_D4_END */
	unsigned int   s13_3; /* BP_D5_START */
	unsigned int   s14_3; /* BP_D5_END */
	unsigned int   s15_3; /* BP_RESERVED */

} support_registers;

enum register_name
{
	R0,  R1,  R2,  R3,
	R4,  R5,  R6,  R7,
	R8,  R9,  R10, R11,
	R12, R13, SP,  ACR,

	BZ,  VR,  PID, SRS,
	WZ,  EXS, EDA, MOF,
	DZ,  EBP, ERP, SRP,
	NRP, CCS, USP, SPC,
	PC,

	S0,  S1,  S2,  S3,
	S4,  S5,  S6,  S7,
	S8,  S9,  S10, S11,
	S12, S13, S14, S15

};

/* The register sizes of the registers in register_name. An unimplemented register
   is designated by size 0 in this array. */
static int register_size[] =
{
	4, 4, 4, 4,
	4, 4, 4, 4,
	4, 4, 4, 4,
	4, 4, 4, 4,

	1, 1, 4, 1,
	2, 4, 4, 4,
	4, 4, 4, 4,
	4, 4, 4, 4,

	4,

	4, 4, 4, 4,
	4, 4, 4, 4,
	4, 4, 4, 4,
	4, 4, 4

};

/* Contains the register image of the kernel.
   (Global so that they can be reached from assembler code.) */
registers reg;
support_registers sreg;

/************** Prototypes for local library functions ***********************/

/* Copy of strcpy from libc. */
static char *gdb_cris_strcpy(char *s1, const char *s2);

/* Copy of strlen from libc. */
static int gdb_cris_strlen(const char *s);

/* Copy of memchr from libc. */
static void *gdb_cris_memchr(const void *s, int c, int n);

/* Copy of strtol from libc. Does only support base 16. */
static int gdb_cris_strtol(const char *s, char **endptr, int base);

/********************** Prototypes for local functions. **********************/

/* Write a value to a specified register regno in the register image
   of the current thread. */
static int write_register(int regno, char *val);

/* Read a value from a specified register in the register image. Returns the
   status of the read operation. The register value is returned in valptr. */
static int read_register(char regno, unsigned int *valptr);

/* Serial port, reads one character. ETRAX 100 specific. from debugport.c */
int getDebugChar(void);

#ifdef CONFIG_ETRAX_VCS_SIM
int getDebugChar(void)
{
  return socketread();
}
#endif

/* Serial port, writes one character. ETRAX 100 specific. from debugport.c */
void putDebugChar(int val);

#ifdef CONFIG_ETRAX_VCS_SIM
void putDebugChar(int val)
{
  socketwrite((char *)&val, 1);
}
#endif

/* Returns the integer equivalent of a hexadecimal character. */
static int hex(char ch);

/* Convert the memory, pointed to by mem into hexadecimal representation.
   Put the result in buf, and return a pointer to the last character
   in buf (null). */
static char *mem2hex(char *buf, unsigned char *mem, int count);

/* Convert the array, in hexadecimal representation, pointed to by buf into
   binary representation. Put the result in mem, and return a pointer to
   the character after the last byte written. */
static unsigned char *hex2mem(unsigned char *mem, char *buf, int count);

/* Put the content of the array, in binary representation, pointed to by buf
   into memory pointed to by mem, and return a pointer to
   the character after the last byte written. */
static unsigned char *bin2mem(unsigned char *mem, unsigned char *buf, int count);

/* Await the sequence $<data>#<checksum> and store <data> in the array buffer
   returned. */
static void getpacket(char *buffer);

/* Send $<data>#<checksum> from the <data> in the array buffer. */
static void putpacket(char *buffer);

/* Build and send a response packet in order to inform the host the
   stub is stopped. */
static void stub_is_stopped(int sigval);

/* All expected commands are sent from remote.c. Send a response according
   to the description in remote.c. Not static since it needs to be reached
   from assembler code. */
void handle_exception(int sigval);

/* Performs a complete re-start from scratch. ETRAX specific. */
static void kill_restart(void);

/******************** Prototypes for global functions. ***********************/

/* The string str is prepended with the GDB printout token and sent. */
void putDebugString(const unsigned char *str, int len);

/* A static breakpoint to be used at startup. */
void breakpoint(void);

/* Avoid warning as the internal_stack is not used in the C-code. */
#define USEDVAR(name)    { if (name) { ; } }
#define USEDFUN(name) { void (*pf)(void) = (void *)name; USEDVAR(pf) }

/********************************** Packet I/O ******************************/
/* BUFMAX defines the maximum number of characters in
   inbound/outbound buffers */
/* FIXME: How do we know it's enough? */
#define BUFMAX 512

/* Run-length encoding maximum length. Send 64 at most. */
#define RUNLENMAX 64

/* The inbound/outbound buffers used in packet I/O */
static char input_buffer[BUFMAX];
static char output_buffer[BUFMAX];

/* Error and warning messages. */
enum error_type
{
	SUCCESS, E01, E02, E03, E04, E05, E06,
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
};

/********************************** Breakpoint *******************************/
/* Use an internal stack in the breakpoint and interrupt response routines.
   FIXME: How do we know the size of this stack is enough?
   Global so it can be reached from assembler code. */
#define INTERNAL_STACK_SIZE 1024
char internal_stack[INTERNAL_STACK_SIZE];

/* Due to the breakpoint return pointer, a state variable is needed to keep
   track of whether it is a static (compiled) or dynamic (gdb-invoked)
   breakpoint to be handled. A static breakpoint uses the content of register
   ERP as it is whereas a dynamic breakpoint requires subtraction with 2
   in order to execute the instruction. The first breakpoint is static; all
   following are assumed to be dynamic. */
static int dynamic_bp = 0;

/********************************* String library ****************************/
/* Single-step over library functions creates trap loops. */

/* Copy char s2[] to s1[]. */
static char*
gdb_cris_strcpy(char *s1, const char *s2)
{
	char *s = s1;

	for (s = s1; (*s++ = *s2++) != '\0'; )
		;
	return s1;
}

/* Find length of s[]. */
static int
gdb_cris_strlen(const char *s)
{
	const char *sc;

	for (sc = s; *sc != '\0'; sc++)
		;
	return (sc - s);
}

/* Find first occurrence of c in s[n]. */
static void*
gdb_cris_memchr(const void *s, int c, int n)
{
	const unsigned char uc = c;
	const unsigned char *su;

	for (su = s; 0 < n; ++su, --n)
		if (*su == uc)
			return (void *)su;
	return NULL;
}
/******************************* Standard library ****************************/
/* Single-step over library functions creates trap loops. */
/* Convert string to long. */
static int
gdb_cris_strtol(const char *s, char **endptr, int base)
{
	char *s1;
	char *sd;
	int x = 0;

	for (s1 = (char*)s; (sd = gdb_cris_memchr(hex_asc, *s1, base)) != NULL; ++s1)
		x = x * base + (sd - hex_asc);

        if (endptr) {
                /* Unconverted suffix is stored in endptr unless endptr is NULL. */
                *endptr = s1;
        }

	return x;
}

/********************************* Register image ****************************/

/* Write a value to a specified register in the register image of the current
   thread. Returns status code SUCCESS, E02 or E05. */
static int
write_register(int regno, char *val)
{
	int status = SUCCESS;

        if (regno >= R0 && regno <= ACR) {
		/* Consecutive 32-bit registers. */
		hex2mem((unsigned char *)&reg.r0 + (regno - R0) * sizeof(unsigned int),
			val, sizeof(unsigned int));

	} else if (regno == BZ || regno == VR || regno == WZ || regno == DZ) {
		/* Read-only registers. */
		status = E02;

	} else if (regno == PID) {
		/* 32-bit register. (Even though we already checked SRS and WZ, we cannot
		   combine this with the EXS - SPC write since SRS and WZ have different size.) */
		hex2mem((unsigned char *)&reg.pid, val, sizeof(unsigned int));

	} else if (regno == SRS) {
		/* 8-bit register. */
		hex2mem((unsigned char *)&reg.srs, val, sizeof(unsigned char));

	} else if (regno >= EXS && regno <= SPC) {
		/* Consecutive 32-bit registers. */
		hex2mem((unsigned char *)&reg.exs + (regno - EXS) * sizeof(unsigned int),
			 val, sizeof(unsigned int));

       } else if (regno == PC) {
               /* Pseudo-register. Treat as read-only. */
               status = E02;

       } else if (regno >= S0 && regno <= S15) {
               /* 32-bit registers. */
               hex2mem((unsigned char *)&sreg.s0_0 + (reg.srs * 16 * sizeof(unsigned int)) + (regno - S0) * sizeof(unsigned int), val, sizeof(unsigned int));
	} else {
		/* Non-existing register. */
		status = E05;
	}
	return status;
}

/* Read a value from a specified register in the register image. Returns the
   value in the register or -1 for non-implemented registers. */
static int
read_register(char regno, unsigned int *valptr)
{
	int status = SUCCESS;

	/* We read the zero registers from the register struct (instead of just returning 0)
	   to catch errors. */

	if (regno >= R0 && regno <= ACR) {
		/* Consecutive 32-bit registers. */
		*valptr = *(unsigned int *)((char *)&reg.r0 + (regno - R0) * sizeof(unsigned int));

	} else if (regno == BZ || regno == VR) {
		/* Consecutive 8-bit registers. */
		*valptr = (unsigned int)(*(unsigned char *)
                                         ((char *)&reg.bz + (regno - BZ) * sizeof(char)));

	} else if (regno == PID) {
		/* 32-bit register. */
		*valptr =  *(unsigned int *)((char *)&reg.pid);

	} else if (regno == SRS) {
		/* 8-bit register. */
		*valptr = (unsigned int)(*(unsigned char *)((char *)&reg.srs));

	} else if (regno == WZ) {
		/* 16-bit register. */
		*valptr = (unsigned int)(*(unsigned short *)(char *)&reg.wz);

	} else if (regno >= EXS && regno <= PC) {
		/* Consecutive 32-bit registers. */
		*valptr = *(unsigned int *)((char *)&reg.exs + (regno - EXS) * sizeof(unsigned int));

	} else if (regno >= S0 && regno <= S15) {
		/* Consecutive 32-bit registers, located elsewhere. */
		*valptr = *(unsigned int *)((char *)&sreg.s0_0 + (reg.srs * 16 * sizeof(unsigned int)) + (regno - S0) * sizeof(unsigned int));

	} else {
		/* Non-existing register. */
		status = E05;
	}
	return status;

}

/********************************** Packet I/O ******************************/
/* Returns the integer equivalent of a hexadecimal character. */
static int
hex(char ch)
{
	if ((ch >= 'a') && (ch <= 'f'))
		return (ch - 'a' + 10);
	if ((ch >= '0') && (ch <= '9'))
		return (ch - '0');
	if ((ch >= 'A') && (ch <= 'F'))
		return (ch - 'A' + 10);
	return -1;
}

/* Convert the memory, pointed to by mem into hexadecimal representation.
   Put the result in buf, and return a pointer to the last character
   in buf (null). */

static char *
mem2hex(char *buf, unsigned char *mem, int count)
{
	int i;
	int ch;

        if (mem == NULL) {
		/* Invalid address, caught by 'm' packet handler. */
                for (i = 0; i < count; i++) {
                        *buf++ = '0';
                        *buf++ = '0';
                }
        } else {
                /* Valid mem address. */
		for (i = 0; i < count; i++) {
			ch = *mem++;
			buf = hex_byte_pack(buf, ch);
		}
        }
        /* Terminate properly. */
	*buf = '\0';
	return buf;
}

/* Same as mem2hex, but puts it in network byte order. */
static char *
mem2hex_nbo(char *buf, unsigned char *mem, int count)
{
	int i;
	int ch;

	mem += count - 1;
	for (i = 0; i < count; i++) {
		ch = *mem--;
		buf = hex_byte_pack(buf, ch);
        }

        /* Terminate properly. */
	*buf = '\0';
	return buf;
}

/* Convert the array, in hexadecimal representation, pointed to by buf into
   binary representation. Put the result in mem, and return a pointer to
   the character after the last byte written. */
static unsigned char*
hex2mem(unsigned char *mem, char *buf, int count)
{
	int i;
	unsigned char ch;
	for (i = 0; i < count; i++) {
		ch = hex (*buf++) << 4;
		ch = ch + hex (*buf++);
		*mem++ = ch;
	}
	return mem;
}

/* Put the content of the array, in binary representation, pointed to by buf
   into memory pointed to by mem, and return a pointer to the character after
   the last byte written.
   Gdb will escape $, #, and the escape char (0x7d). */
static unsigned char*
bin2mem(unsigned char *mem, unsigned char *buf, int count)
{
	int i;
	unsigned char *next;
	for (i = 0; i < count; i++) {
		/* Check for any escaped characters. Be paranoid and
		   only unescape chars that should be escaped. */
		if (*buf == 0x7d) {
			next = buf + 1;
			if (*next == 0x3 || *next == 0x4 || *next == 0x5D) {
				 /* #, $, ESC */
				buf++;
				*buf += 0x20;
			}
		}
		*mem++ = *buf++;
	}
	return mem;
}

/* Await the sequence $<data>#<checksum> and store <data> in the array buffer
   returned. */
static void
getpacket(char *buffer)
{
	unsigned char checksum;
	unsigned char xmitcsum;
	int i;
	int count;
	char ch;

	do {
		while((ch = getDebugChar ()) != '$')
			/* Wait for the start character $ and ignore all other characters */;
		checksum = 0;
		xmitcsum = -1;
		count = 0;
		/* Read until a # or the end of the buffer is reached */
		while (count < BUFMAX) {
			ch = getDebugChar();
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
			xmitcsum = hex(getDebugChar()) << 4;
			xmitcsum += hex(getDebugChar());
			if (checksum != xmitcsum) {
				/* Wrong checksum */
				putDebugChar('-');
			} else {
				/* Correct checksum */
				putDebugChar('+');
				/* If sequence characters are received, reply with them */
				if (buffer[2] == ':') {
					putDebugChar(buffer[0]);
					putDebugChar(buffer[1]);
					/* Remove the sequence characters from the buffer */
					count = gdb_cris_strlen(buffer);
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
		putDebugChar('$');
		checksum = 0;
		while (*src) {
			/* Do run length encoding */
			putDebugChar(*src);
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
				putDebugChar(encode);
				checksum += encode;
				src += runlen;
			} else {
				src++;
			}
		}
		putDebugChar('#');
		putDebugChar(hex_asc_hi(checksum));
		putDebugChar(hex_asc_lo(checksum));
	} while(kgdb_started && (getDebugChar() != '+'));
}

/* The string str is prepended with the GDB printout token and sent. Required
   in traditional implementations. */
void
putDebugString(const unsigned char *str, int len)
{
	/* Move SPC forward if we are single-stepping. */
	asm("spchere:");
	asm("move $spc, $r10");
	asm("cmp.d spchere, $r10");
	asm("bne nosstep");
	asm("nop");
	asm("move.d spccont, $r10");
	asm("move $r10, $spc");
	asm("nosstep:");

        output_buffer[0] = 'O';
        mem2hex(&output_buffer[1], (unsigned char *)str, len);
        putpacket(output_buffer);

	asm("spccont:");
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
	char *ptr = output_buffer;
	unsigned int reg_cont;

	/* Send trap type (converted to signal) */

	*ptr++ = 'T';
	ptr = hex_byte_pack(ptr, sigval);

	if (((reg.exs & 0xff00) >> 8) == 0xc) {

		/* Some kind of hardware watchpoint triggered. Find which one
		   and determine its type (read/write/access).  */
		int S, bp, trig_bits = 0, rw_bits = 0;
		int trig_mask = 0;
		unsigned int *bp_d_regs = &sreg.s3_3;
		/* In a lot of cases, the stopped data address will simply be EDA.
		   In some cases, we adjust it to match the watched data range.
		   (We don't want to change the actual EDA though). */
		unsigned int stopped_data_address;
		/* The S field of EXS. */
		S = (reg.exs & 0xffff0000) >> 16;

		if (S & 1) {
			/* Instruction watchpoint. */
			/* FIXME: Check against, and possibly adjust reported EDA. */
		} else {
			/* Data watchpoint.  Find the one that triggered. */
			for (bp = 0; bp < 6; bp++) {

				/* Dx_RD, Dx_WR in the S field of EXS for this BP. */
				int bitpos_trig = 1 + bp * 2;
				/* Dx_BPRD, Dx_BPWR in BP_CTRL for this BP. */
				int bitpos_config = 2 + bp * 4;

				/* Get read/write trig bits for this BP. */
				trig_bits = (S & (3 << bitpos_trig)) >> bitpos_trig;

				/* Read/write config bits for this BP. */
				rw_bits = (sreg.s0_3 & (3 << bitpos_config)) >> bitpos_config;
				if (trig_bits) {
					/* Sanity check: the BP shouldn't trigger for accesses
					   that it isn't configured for. */
					if ((rw_bits == 0x1 && trig_bits != 0x1) ||
					    (rw_bits == 0x2 && trig_bits != 0x2))
						panic("Invalid r/w trigging for this BP");

					/* Mark this BP as trigged for future reference. */
					trig_mask |= (1 << bp);

					if (reg.eda >= bp_d_regs[bp * 2] &&
					    reg.eda <= bp_d_regs[bp * 2 + 1]) {
						/* EDA within range for this BP; it must be the one
						   we're looking for. */
						stopped_data_address = reg.eda;
						break;
					}
				}
			}
			if (bp < 6) {
				/* Found a trigged BP with EDA within its configured data range. */
			} else if (trig_mask) {
				/* Something triggered, but EDA doesn't match any BP's range. */
				for (bp = 0; bp < 6; bp++) {
					/* Dx_BPRD, Dx_BPWR in BP_CTRL for this BP. */
					int bitpos_config = 2 + bp * 4;

					/* Read/write config bits for this BP (needed later). */
					rw_bits = (sreg.s0_3 & (3 << bitpos_config)) >> bitpos_config;

					if (trig_mask & (1 << bp)) {
						/* EDA within 31 bytes of the configured start address? */
						if (reg.eda + 31 >= bp_d_regs[bp * 2]) {
							/* Changing the reported address to match
							   the start address of the first applicable BP. */
							stopped_data_address = bp_d_regs[bp * 2];
							break;
						} else {
							/* We continue since we might find another useful BP. */
							printk("EDA doesn't match trigged BP's range");
						}
					}
				}
			}

			/* No match yet? */
			BUG_ON(bp >= 6);
			/* Note that we report the type according to what the BP is configured
			   for (otherwise we'd never report an 'awatch'), not according to how
			   it trigged. We did check that the trigged bits match what the BP is
			   configured for though. */
			if (rw_bits == 0x1) {
				/* read */
				strncpy(ptr, "rwatch", 6);
				ptr += 6;
			} else if (rw_bits == 0x2) {
				/* write */
				strncpy(ptr, "watch", 5);
				ptr += 5;
			} else if (rw_bits == 0x3) {
				/* access */
				strncpy(ptr, "awatch", 6);
				ptr += 6;
			} else {
				panic("Invalid r/w bits for this BP.");
			}

			*ptr++ = ':';
			/* Note that we don't read_register(EDA, ...) */
			ptr = mem2hex_nbo(ptr, (unsigned char *)&stopped_data_address, register_size[EDA]);
			*ptr++ = ';';
		}
	}
	/* Only send PC, frame and stack pointer. */
	read_register(PC, &reg_cont);
	ptr = hex_byte_pack(ptr, PC);
	*ptr++ = ':';
	ptr = mem2hex(ptr, (unsigned char *)&reg_cont, register_size[PC]);
	*ptr++ = ';';

	read_register(R8, &reg_cont);
	ptr = hex_byte_pack(ptr, R8);
	*ptr++ = ':';
	ptr = mem2hex(ptr, (unsigned char *)&reg_cont, register_size[R8]);
	*ptr++ = ';';

	read_register(SP, &reg_cont);
	ptr = hex_byte_pack(ptr, SP);
	*ptr++ = ':';
	ptr = mem2hex(ptr, (unsigned char *)&reg_cont, register_size[SP]);
	*ptr++ = ';';

	/* Send ERP as well; this will save us an entire register fetch in some cases. */
        read_register(ERP, &reg_cont);
	ptr = hex_byte_pack(ptr, ERP);
        *ptr++ = ':';
        ptr = mem2hex(ptr, (unsigned char *)&reg_cont, register_size[ERP]);
        *ptr++ = ';';

	/* null-terminate and send it off */
	*ptr = 0;
	putpacket(output_buffer);
}

/* Returns the size of an instruction that has a delay slot. */

int insn_size(unsigned long pc)
{
	unsigned short opcode = *(unsigned short *)pc;
	int size = 0;

	switch ((opcode & 0x0f00) >> 8) {
	case 0x0:
	case 0x9:
	case 0xb:
		size = 2;
		break;
	case 0xe:
	case 0xf:
		size = 6;
		break;
	case 0xd:
		/* Could be 4 or 6; check more bits. */
		if ((opcode & 0xff) == 0xff)
			size = 4;
		else
			size = 6;
		break;
	default:
		panic("Couldn't find size of opcode 0x%x at 0x%lx\n", opcode, pc);
	}

	return size;
}

void register_fixup(int sigval)
{
	/* Compensate for ACR push at the beginning of exception handler. */
	reg.sp += 4;

	/* Standard case. */
	reg.pc = reg.erp;
	if (reg.erp & 0x1) {
		/* Delay slot bit set.  Report as stopped on proper instruction.  */
		if (reg.spc) {
			/* Rely on SPC if set. */
			reg.pc = reg.spc;
		} else {
			/* Calculate the PC from the size of the instruction
			   that the delay slot we're in belongs to. */
			reg.pc += insn_size(reg.erp & ~1) - 1 ;
		}
	}

	if ((reg.exs & 0x3) == 0x0) {
		/* Bits 1 - 0 indicate the type of memory operation performed
		   by the interrupted instruction. 0 means no memory operation,
		   and EDA is undefined in that case. We zero it to avoid confusion. */
		reg.eda = 0;
	}

	if (sigval == SIGTRAP) {
		/* Break 8, single step or hardware breakpoint exception. */

		/* Check IDX field of EXS. */
		if (((reg.exs & 0xff00) >> 8) == 0x18) {

			/* Break 8. */

                        /* Static (compiled) breakpoints must return to the next instruction
			   in order to avoid infinite loops (default value of ERP). Dynamic
			   (gdb-invoked) must subtract the size of the break instruction from
			   the ERP so that the instruction that was originally in the break
			   instruction's place will be run when we return from the exception. */
			if (!dynamic_bp) {
				/* Assuming that all breakpoints are dynamic from now on. */
				dynamic_bp = 1;
			} else {

				/* Only if not in a delay slot. */
				if (!(reg.erp & 0x1)) {
					reg.erp -= 2;
					reg.pc -= 2;
				}
			}

		} else if (((reg.exs & 0xff00) >> 8) == 0x3) {
			/* Single step. */
			/* Don't fiddle with S1. */

		} else if (((reg.exs & 0xff00) >> 8) == 0xc) {

			/* Hardware watchpoint exception. */

			/* SPC has been updated so that we will get a single step exception
			   when we return, but we don't want that. */
			reg.spc = 0;

			/* Don't fiddle with S1. */
		}

	} else if (sigval == SIGINT) {
		/* Nothing special. */
	}
}

static void insert_watchpoint(char type, int addr, int len)
{
	/* Breakpoint/watchpoint types (GDB terminology):
	   0 = memory breakpoint for instructions
	   (not supported; done via memory write instead)
	   1 = hardware breakpoint for instructions (supported)
	   2 = write watchpoint (supported)
	   3 = read watchpoint (supported)
	   4 = access watchpoint (supported) */

	if (type < '1' || type > '4') {
		output_buffer[0] = 0;
		return;
	}

	/* Read watchpoints are set as access watchpoints, because of GDB's
	   inability to deal with pure read watchpoints. */
	if (type == '3')
		type = '4';

	if (type == '1') {
		/* Hardware (instruction) breakpoint. */
		/* Bit 0 in BP_CTRL holds the configuration for I0. */
		if (sreg.s0_3 & 0x1) {
			/* Already in use. */
			gdb_cris_strcpy(output_buffer, error_message[E04]);
			return;
		}
		/* Configure. */
		sreg.s1_3 = addr;
		sreg.s2_3 = (addr + len - 1);
		sreg.s0_3 |= 1;
	} else {
		int bp;
		unsigned int *bp_d_regs = &sreg.s3_3;

		/* The watchpoint allocation scheme is the simplest possible.
		   For example, if a region is watched for read and
		   a write watch is requested, a new watchpoint will
		   be used. Also, if a watch for a region that is already
		   covered by one or more existing watchpoints, a new
		   watchpoint will be used. */

		/* First, find a free data watchpoint. */
		for (bp = 0; bp < 6; bp++) {
			/* Each data watchpoint's control registers occupy 2 bits
			   (hence the 3), starting at bit 2 for D0 (hence the 2)
			   with 4 bits between for each watchpoint (yes, the 4). */
			if (!(sreg.s0_3 & (0x3 << (2 + (bp * 4))))) {
				break;
			}
		}

		if (bp > 5) {
			/* We're out of watchpoints. */
			gdb_cris_strcpy(output_buffer, error_message[E04]);
			return;
		}

		/* Configure the control register first. */
		if (type == '3' || type == '4') {
			/* Trigger on read. */
			sreg.s0_3 |= (1 << (2 + bp * 4));
		}
		if (type == '2' || type == '4') {
			/* Trigger on write. */
			sreg.s0_3 |= (2 << (2 + bp * 4));
		}

		/* Ugly pointer arithmetics to configure the watched range. */
		bp_d_regs[bp * 2] = addr;
		bp_d_regs[bp * 2 + 1] = (addr + len - 1);
	}

	/* Set the S1 flag to enable watchpoints. */
	reg.ccs |= (1 << (S_CCS_BITNR + CCS_SHIFT));
	gdb_cris_strcpy(output_buffer, "OK");
}

static void remove_watchpoint(char type, int addr, int len)
{
	/* Breakpoint/watchpoint types:
	   0 = memory breakpoint for instructions
	   (not supported; done via memory write instead)
	   1 = hardware breakpoint for instructions (supported)
	   2 = write watchpoint (supported)
	   3 = read watchpoint (supported)
	   4 = access watchpoint (supported) */
	if (type < '1' || type > '4') {
		output_buffer[0] = 0;
		return;
	}

	/* Read watchpoints are set as access watchpoints, because of GDB's
	   inability to deal with pure read watchpoints. */
	if (type == '3')
		type = '4';

	if (type == '1') {
		/* Hardware breakpoint. */
		/* Bit 0 in BP_CTRL holds the configuration for I0. */
		if (!(sreg.s0_3 & 0x1)) {
			/* Not in use. */
			gdb_cris_strcpy(output_buffer, error_message[E04]);
			return;
		}
		/* Deconfigure. */
		sreg.s1_3 = 0;
		sreg.s2_3 = 0;
		sreg.s0_3 &= ~1;
	} else {
		int bp;
		unsigned int *bp_d_regs = &sreg.s3_3;
		/* Try to find a watchpoint that is configured for the
		   specified range, then check that read/write also matches. */

		/* Ugly pointer arithmetic, since I cannot rely on a
		   single switch (addr) as there may be several watchpoints with
		   the same start address for example. */

		for (bp = 0; bp < 6; bp++) {
			if (bp_d_regs[bp * 2] == addr &&
			    bp_d_regs[bp * 2 + 1] == (addr + len - 1)) {
				/* Matching range. */
				int bitpos = 2 + bp * 4;
				int rw_bits;

				/* Read/write bits for this BP. */
				rw_bits = (sreg.s0_3 & (0x3 << bitpos)) >> bitpos;

				if ((type == '3' && rw_bits == 0x1) ||
				    (type == '2' && rw_bits == 0x2) ||
				    (type == '4' && rw_bits == 0x3)) {
					/* Read/write matched. */
					break;
				}
			}
		}

		if (bp > 5) {
			/* No watchpoint matched. */
			gdb_cris_strcpy(output_buffer, error_message[E04]);
			return;
		}

		/* Found a matching watchpoint. Now, deconfigure it by
		   both disabling read/write in bp_ctrl and zeroing its
		   start/end addresses. */
		sreg.s0_3 &= ~(3 << (2 + (bp * 4)));
		bp_d_regs[bp * 2] = 0;
		bp_d_regs[bp * 2 + 1] = 0;
	}

	/* Note that we don't clear the S1 flag here. It's done when continuing.  */
	gdb_cris_strcpy(output_buffer, "OK");
}



/* All expected commands are sent from remote.c. Send a response according
   to the description in remote.c. */
void
handle_exception(int sigval)
{
	/* Avoid warning of not used. */

	USEDFUN(handle_exception);
	USEDVAR(internal_stack[0]);

	register_fixup(sigval);

	/* Send response. */
	stub_is_stopped(sigval);

	for (;;) {
		output_buffer[0] = '\0';
		getpacket(input_buffer);
		switch (input_buffer[0]) {
			case 'g':
				/* Read registers: g
				   Success: Each byte of register data is described by two hex digits.
				   Registers are in the internal order for GDB, and the bytes
				   in a register  are in the same order the machine uses.
				   Failure: void. */
			{
				char *buf;
				/* General and special registers. */
				buf = mem2hex(output_buffer, (char *)&reg, sizeof(registers));
				/* Support registers. */
				/* -1 because of the null termination that mem2hex adds. */
				mem2hex(buf,
					(char *)&sreg + (reg.srs * 16 * sizeof(unsigned int)),
					16 * sizeof(unsigned int));
				break;
			}
			case 'G':
				/* Write registers. GXX..XX
				   Each byte of register data  is described by two hex digits.
				   Success: OK
				   Failure: void. */
				/* General and special registers. */
				hex2mem((char *)&reg, &input_buffer[1], sizeof(registers));
				/* Support registers. */
				hex2mem((char *)&sreg + (reg.srs * 16 * sizeof(unsigned int)),
					&input_buffer[1] + sizeof(registers),
					16 * sizeof(unsigned int));
				gdb_cris_strcpy(output_buffer, "OK");
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
					int regno = gdb_cris_strtol(&input_buffer[1], &suffix, 16);
					int status;

					status = write_register(regno, suffix+1);

					switch (status) {
						case E02:
							/* Do not support read-only registers. */
							gdb_cris_strcpy(output_buffer, error_message[E02]);
							break;
						case E05:
							/* Do not support non-existing registers. */
							gdb_cris_strcpy(output_buffer, error_message[E05]);
							break;
						default:
							/* Valid register number. */
							gdb_cris_strcpy(output_buffer, "OK");
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
					unsigned char *addr = (unsigned char *)gdb_cris_strtol(&input_buffer[1],
                                                                                               &suffix, 16);
					int len = gdb_cris_strtol(suffix+1, 0, 16);

					/* Bogus read (i.e. outside the kernel's
					   segment)? . */
					if (!((unsigned int)addr >= 0xc0000000 &&
					      (unsigned int)addr < 0xd0000000))
						addr = NULL;

                                        mem2hex(output_buffer, addr, len);
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
					unsigned char *addr = (unsigned char *)gdb_cris_strtol(&input_buffer[1],
										      &lenptr, 16);
					int len = gdb_cris_strtol(lenptr+1, &dataptr, 16);
					if (*lenptr == ',' && *dataptr == ':') {
						if (input_buffer[0] == 'M') {
							hex2mem(addr, dataptr + 1, len);
						} else /* X */ {
							bin2mem(addr, dataptr + 1, len);
						}
						gdb_cris_strcpy(output_buffer, "OK");
					}
					else {
						gdb_cris_strcpy(output_buffer, error_message[E06]);
					}
				}
				break;

			case 'c':
				/* Continue execution. cAA..AA
				   AA..AA is the address where execution is resumed. If AA..AA is
				   omitted, resume at the present address.
				   Success: return to the executing thread.
				   Failure: will never know. */

				if (input_buffer[1] != '\0') {
					/* FIXME: Doesn't handle address argument. */
					gdb_cris_strcpy(output_buffer, error_message[E04]);
					break;
				}

				/* Before continuing, make sure everything is set up correctly. */

				/* Set the SPC to some unlikely value.  */
				reg.spc = 0;
				/* Set the S1 flag to 0 unless some watchpoint is enabled (since setting
				   S1 to 0 would also disable watchpoints). (Note that bits 26-31 in BP_CTRL
				   are reserved, so don't check against those). */
				if ((sreg.s0_3 & 0x3fff) == 0) {
					reg.ccs &= ~(1 << (S_CCS_BITNR + CCS_SHIFT));
				}

				return;

			case 's':
				/* Step. sAA..AA
				   AA..AA is the address where execution is resumed. If AA..AA is
				   omitted, resume at the present address. Success: return to the
				   executing thread. Failure: will never know. */

				if (input_buffer[1] != '\0') {
					/* FIXME: Doesn't handle address argument. */
					gdb_cris_strcpy(output_buffer, error_message[E04]);
					break;
				}

				/* Set the SPC to PC, which is where we'll return
				   (deduced previously). */
				reg.spc = reg.pc;

				/* Set the S1 (first stacked, not current) flag, which will
				   kick into action when we rfe. */
				reg.ccs |= (1 << (S_CCS_BITNR + CCS_SHIFT));
				return;

                       case 'Z':

                               /* Insert breakpoint or watchpoint, Ztype,addr,length.
                                  Remote protocol says: A remote target shall return an empty string
                                  for an unrecognized breakpoint or watchpoint packet type. */
                               {
                                       char *lenptr;
                                       char *dataptr;
                                       int addr = gdb_cris_strtol(&input_buffer[3], &lenptr, 16);
                                       int len = gdb_cris_strtol(lenptr + 1, &dataptr, 16);
                                       char type = input_buffer[1];

				       insert_watchpoint(type, addr, len);
                                       break;
                               }

                       case 'z':
                               /* Remove breakpoint or watchpoint, Ztype,addr,length.
                                  Remote protocol says: A remote target shall return an empty string
                                  for an unrecognized breakpoint or watchpoint packet type. */
                               {
                                       char *lenptr;
                                       char *dataptr;
                                       int addr = gdb_cris_strtol(&input_buffer[3], &lenptr, 16);
                                       int len = gdb_cris_strtol(lenptr + 1, &dataptr, 16);
                                       char type = input_buffer[1];

                                       remove_watchpoint(type, addr, len);
                                       break;
                               }


			case '?':
				/* The last signal which caused a stop. ?
				   Success: SAA, where AA is the signal number.
				   Failure: void. */
				output_buffer[0] = 'S';
				output_buffer[1] = hex_asc_hi(sigval);
				output_buffer[2] = hex_asc_lo(sigval);
				output_buffer[3] = 0;
				break;

			case 'D':
				/* Detach from host. D
				   Success: OK, and return to the executing thread.
				   Failure: will never know */
				putpacket("OK");
				return;

			case 'k':
			case 'r':
				/* kill request or reset request.
				   Success: restart of target.
				   Failure: will never know. */
				kill_restart();
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

				/* FIXME: What's the difference between not supported
				   and ignored (below)? */
				gdb_cris_strcpy(output_buffer, error_message[E04]);
				break;

			default:
				/* The stub should ignore other request and send an empty
				   response ($#<checksum>). This way we can extend the protocol and GDB
				   can tell whether the stub it is talking to uses the old or the new. */
				output_buffer[0] = 0;
				break;
		}
		putpacket(output_buffer);
	}
}

void
kgdb_init(void)
{
	reg_intr_vect_rw_mask intr_mask;
	reg_ser_rw_intr_mask ser_intr_mask;

	/* Configure the kgdb serial port. */
#if defined(CONFIG_ETRAX_KGDB_PORT0)
	/* Note: no shortcut registered (not handled by multiple_interrupt).
	   See entry.S.  */
	set_exception_vector(SER0_INTR_VECT, kgdb_handle_exception);
	/* Enable the ser irq in the global config. */
	intr_mask = REG_RD(intr_vect, regi_irq, rw_mask);
	intr_mask.ser0 = 1;
	REG_WR(intr_vect, regi_irq, rw_mask, intr_mask);

	ser_intr_mask = REG_RD(ser, regi_ser0, rw_intr_mask);
	ser_intr_mask.dav = regk_ser_yes;
	REG_WR(ser, regi_ser0, rw_intr_mask, ser_intr_mask);
#elif defined(CONFIG_ETRAX_KGDB_PORT1)
	/* Note: no shortcut registered (not handled by multiple_interrupt).
	   See entry.S.  */
	set_exception_vector(SER1_INTR_VECT, kgdb_handle_exception);
	/* Enable the ser irq in the global config. */
	intr_mask = REG_RD(intr_vect, regi_irq, rw_mask);
	intr_mask.ser1 = 1;
	REG_WR(intr_vect, regi_irq, rw_mask, intr_mask);

	ser_intr_mask = REG_RD(ser, regi_ser1, rw_intr_mask);
	ser_intr_mask.dav = regk_ser_yes;
	REG_WR(ser, regi_ser1, rw_intr_mask, ser_intr_mask);
#elif defined(CONFIG_ETRAX_KGDB_PORT2)
	/* Note: no shortcut registered (not handled by multiple_interrupt).
	   See entry.S.  */
	set_exception_vector(SER2_INTR_VECT, kgdb_handle_exception);
	/* Enable the ser irq in the global config. */
	intr_mask = REG_RD(intr_vect, regi_irq, rw_mask);
	intr_mask.ser2 = 1;
	REG_WR(intr_vect, regi_irq, rw_mask, intr_mask);

	ser_intr_mask = REG_RD(ser, regi_ser2, rw_intr_mask);
	ser_intr_mask.dav = regk_ser_yes;
	REG_WR(ser, regi_ser2, rw_intr_mask, ser_intr_mask);
#elif defined(CONFIG_ETRAX_KGDB_PORT3)
	/* Note: no shortcut registered (not handled by multiple_interrupt).
	   See entry.S.  */
	set_exception_vector(SER3_INTR_VECT, kgdb_handle_exception);
	/* Enable the ser irq in the global config. */
	intr_mask = REG_RD(intr_vect, regi_irq, rw_mask);
	intr_mask.ser3 = 1;
	REG_WR(intr_vect, regi_irq, rw_mask, intr_mask);

	ser_intr_mask = REG_RD(ser, regi_ser3, rw_intr_mask);
	ser_intr_mask.dav = regk_ser_yes;
	REG_WR(ser, regi_ser3, rw_intr_mask, ser_intr_mask);
#endif

}
/* Performs a complete re-start from scratch. */
static void
kill_restart(void)
{
	machine_restart("");
}

/* Use this static breakpoint in the start-up only. */

void
breakpoint(void)
{
	kgdb_started = 1;
	dynamic_bp = 0;     /* This is a static, not a dynamic breakpoint. */
	__asm__ volatile ("break 8"); /* Jump to kgdb_handle_breakpoint. */
}

/****************************** End of file **********************************/

/* gdb-stub.c: FRV GDB stub
 *
 * Copyright (C) 2003,4 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from Linux/MIPS version, Copyright (C) 1995 Andreas Busse
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 *  To enable debugger support, two things need to happen.  One, a
 *  call to set_debug_traps() is necessary in order to allow any breakpoints
 *  or error conditions to be properly intercepted and reported to gdb.
 *  Two, a breakpoint needs to be generated to begin communication.  This
 *  is most easily accomplished by a call to breakpoint().  Breakpoint()
 *  simulates a breakpoint by executing a BREAK instruction.
 *
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
 *
 *  ==============
 *  MORE EXAMPLES:
 *  ==============
 *
 *  For reference -- the following are the steps that one
 *  company took (RidgeRun Inc) to get remote gdb debugging
 *  going. In this scenario the host machine was a PC and the
 *  target platform was a Galileo EVB64120A MIPS evaluation
 *  board.
 *
 *  Step 1:
 *  First download gdb-5.0.tar.gz from the internet.
 *  and then build/install the package.
 *
 *  Example:
 *    $ tar zxf gdb-5.0.tar.gz
 *    $ cd gdb-5.0
 *    $ ./configure --target=frv-elf-gdb
 *    $ make
 *    $ frv-elf-gdb
 *
 *  Step 2:
 *  Configure linux for remote debugging and build it.
 *
 *  Example:
 *    $ cd ~/linux
 *    $ make menuconfig <go to "Kernel Hacking" and turn on remote debugging>
 *    $ make vmlinux
 *
 *  Step 3:
 *  Download the kernel to the remote target and start
 *  the kernel running. It will promptly halt and wait
 *  for the host gdb session to connect. It does this
 *  since the "Kernel Hacking" option has defined
 *  CONFIG_REMOTE_DEBUG which in turn enables your calls
 *  to:
 *     set_debug_traps();
 *     breakpoint();
 *
 *  Step 4:
 *  Start the gdb session on the host.
 *
 *  Example:
 *    $ frv-elf-gdb vmlinux
 *    (gdb) set remotebaud 115200
 *    (gdb) target remote /dev/ttyS1
 *    ...at this point you are connected to
 *       the remote target and can use gdb
 *       in the normal fasion. Setting
 *       breakpoints, single stepping,
 *       printing variables, etc.
 *
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/nmi.h>

#include <asm/asm-offsets.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/gdb-stub.h>

#define LEDS(x) do { /* *(u32*)0xe1200004 = ~(x); mb(); */ } while(0)

#undef GDBSTUB_DEBUG_PROTOCOL

extern void debug_to_serial(const char *p, int n);
extern void gdbstub_console_write(struct console *co, const char *p, unsigned n);

extern volatile uint32_t __break_error_detect[3]; /* ESFR1, ESR15, EAR15 */

struct __debug_amr {
	unsigned long L, P;
} __attribute__((aligned(8)));

struct __debug_mmu {
	struct {
		unsigned long	hsr0, pcsr, esr0, ear0, epcr0;
#ifdef CONFIG_MMU
		unsigned long	tplr, tppr, tpxr, cxnr;
#endif
	} regs;

	struct __debug_amr	iamr[16];
	struct __debug_amr	damr[16];

#ifdef CONFIG_MMU
	struct __debug_amr	tlb[64*2];
#endif
};

static struct __debug_mmu __debug_mmu;

/*
 * BUFMAX defines the maximum number of characters in inbound/outbound buffers
 * at least NUMREGBYTES*2 are needed for register packets
 */
#define BUFMAX 2048

#define BREAK_INSN	0x801000c0	/* use "break" as bkpt */

static const char gdbstub_banner[] = "Linux/FR-V GDB Stub (c) RedHat 2003\n";

volatile u8	gdbstub_rx_buffer[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
volatile u32	gdbstub_rx_inp = 0;
volatile u32	gdbstub_rx_outp = 0;
volatile u8	gdbstub_rx_overflow = 0;
u8		gdbstub_rx_unget = 0;

/* set with GDB whilst running to permit step through exceptions */
extern volatile u32 __attribute__((section(".bss"))) gdbstub_trace_through_exceptions;

static char	input_buffer[BUFMAX];
static char	output_buffer[BUFMAX];

static const char *regnames[] = {
	"PSR ", "ISR ", "CCR ", "CCCR",
	"LR  ", "LCR ", "PC  ", "_stt",
	"sys ", "GR8*", "GNE0", "GNE1",
	"IACH", "IACL",
	"TBR ", "SP  ", "FP  ", "GR3 ",
	"GR4 ", "GR5 ", "GR6 ", "GR7 ",
	"GR8 ", "GR9 ", "GR10", "GR11",
	"GR12", "GR13", "GR14", "GR15",
	"GR16", "GR17", "GR18", "GR19",
	"GR20", "GR21", "GR22", "GR23",
	"GR24", "GR25", "GR26", "GR27",
	"EFRM", "CURR", "GR30", "BFRM"
};

struct gdbstub_bkpt {
	unsigned long	addr;		/* address of breakpoint */
	unsigned	len;		/* size of breakpoint */
	uint32_t	originsns[7];	/* original instructions */
};

static struct gdbstub_bkpt gdbstub_bkpts[256];

/*
 * local prototypes
 */

static void gdbstub_recv_packet(char *buffer);
static int gdbstub_send_packet(char *buffer);
static int gdbstub_compute_signal(unsigned long tbr);
static int hex(unsigned char ch);
static int hexToInt(char **ptr, unsigned long *intValue);
static unsigned char *mem2hex(const void *mem, char *buf, int count, int may_fault);
static char *hex2mem(const char *buf, void *_mem, int count);

/*
 * Convert ch from a hex digit to an int
 */
static int hex(unsigned char ch)
{
	if (ch >= 'a' && ch <= 'f')
		return ch-'a'+10;
	if (ch >= '0' && ch <= '9')
		return ch-'0';
	if (ch >= 'A' && ch <= 'F')
		return ch-'A'+10;
	return -1;
}

void gdbstub_printk(const char *fmt, ...)
{
	static char buf[1024];
	va_list args;
	int len;

	/* Emit the output into the temporary buffer */
	va_start(args, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	debug_to_serial(buf, len);
}

static inline char *gdbstub_strcpy(char *dst, const char *src)
{
	int loop = 0;
	while ((dst[loop] = src[loop]))
	       loop++;
	return dst;
}

static void gdbstub_purge_cache(void)
{
	asm volatile("	dcef	@(gr0,gr0),#1	\n"
		     "	icei	@(gr0,gr0),#1	\n"
		     "	membar			\n"
		     "	bar			\n"
		     );
}

/*****************************************************************************/
/*
 * scan for the sequence $<data>#<checksum>
 */
static void gdbstub_recv_packet(char *buffer)
{
	unsigned char checksum;
	unsigned char xmitcsum;
	unsigned char ch;
	int count, i, ret, error;

	for (;;) {
		/* wait around for the start character, ignore all other characters */
		do {
			gdbstub_rx_char(&ch, 0);
		} while (ch != '$');

		checksum = 0;
		xmitcsum = -1;
		count = 0;
		error = 0;

		/* now, read until a # or end of buffer is found */
		while (count < BUFMAX) {
			ret = gdbstub_rx_char(&ch, 0);
			if (ret < 0)
				error = ret;

			if (ch == '#')
				break;
			checksum += ch;
			buffer[count] = ch;
			count++;
		}

		if (error == -EIO) {
			gdbstub_proto("### GDB Rx Error - Skipping packet ###\n");
			gdbstub_proto("### GDB Tx NAK\n");
			gdbstub_tx_char('-');
			continue;
		}

		if (count >= BUFMAX || error)
			continue;

		buffer[count] = 0;

		/* read the checksum */
		ret = gdbstub_rx_char(&ch, 0);
		if (ret < 0)
			error = ret;
		xmitcsum = hex(ch) << 4;

		ret = gdbstub_rx_char(&ch, 0);
		if (ret < 0)
			error = ret;
		xmitcsum |= hex(ch);

		if (error) {
			if (error == -EIO)
				gdbstub_proto("### GDB Rx Error - Skipping packet\n");
			gdbstub_proto("### GDB Tx NAK\n");
			gdbstub_tx_char('-');
			continue;
		}

		/* check the checksum */
		if (checksum != xmitcsum) {
			gdbstub_proto("### GDB Tx NAK\n");
			gdbstub_tx_char('-');	/* failed checksum */
			continue;
		}

		gdbstub_proto("### GDB Rx '$%s#%02x' ###\n", buffer, checksum);
		gdbstub_proto("### GDB Tx ACK\n");
		gdbstub_tx_char('+'); /* successful transfer */

		/* if a sequence char is present, reply the sequence ID */
		if (buffer[2] == ':') {
			gdbstub_tx_char(buffer[0]);
			gdbstub_tx_char(buffer[1]);

			/* remove sequence chars from buffer */
			count = 0;
			while (buffer[count]) count++;
			for (i=3; i <= count; i++)
				buffer[i - 3] = buffer[i];
		}

		break;
	}
} /* end gdbstub_recv_packet() */

/*****************************************************************************/
/*
 * send the packet in buffer.
 * - return 0 if successfully ACK'd
 * - return 1 if abandoned due to new incoming packet
 */
static int gdbstub_send_packet(char *buffer)
{
	unsigned char checksum;
	int count;
	unsigned char ch;

	/* $<packet info>#<checksum> */
	gdbstub_proto("### GDB Tx '%s' ###\n", buffer);

	do {
		gdbstub_tx_char('$');
		checksum = 0;
		count = 0;

		while ((ch = buffer[count]) != 0) {
			gdbstub_tx_char(ch);
			checksum += ch;
			count += 1;
		}

		gdbstub_tx_char('#');
		gdbstub_tx_char(hex_asc_hi(checksum));
		gdbstub_tx_char(hex_asc_lo(checksum));

	} while (gdbstub_rx_char(&ch,0),
#ifdef GDBSTUB_DEBUG_PROTOCOL
		 ch=='-' && (gdbstub_proto("### GDB Rx NAK\n"),0),
		 ch!='-' && ch!='+' && (gdbstub_proto("### GDB Rx ??? %02x\n",ch),0),
#endif
		 ch!='+' && ch!='$');

	if (ch=='+') {
		gdbstub_proto("### GDB Rx ACK\n");
		return 0;
	}

	gdbstub_proto("### GDB Tx Abandoned\n");
	gdbstub_rx_unget = ch;
	return 1;
} /* end gdbstub_send_packet() */

/*
 * While we find nice hex chars, build an int.
 * Return number of chars processed.
 */
static int hexToInt(char **ptr, unsigned long *_value)
{
	int count = 0, ch;

	*_value = 0;
	while (**ptr) {
		ch = hex(**ptr);
		if (ch < 0)
			break;

		*_value = (*_value << 4) | ((uint8_t) ch & 0xf);
		count++;

		(*ptr)++;
	}

	return count;
}

/*****************************************************************************/
/*
 * probe an address to see whether it maps to anything
 */
static inline int gdbstub_addr_probe(const void *vaddr)
{
#ifdef CONFIG_MMU
	unsigned long paddr;

	asm("lrad %1,%0,#1,#0,#0" : "=r"(paddr) : "r"(vaddr));
	if (!(paddr & xAMPRx_V))
		return 0;
#endif

	return 1;
} /* end gdbstub_addr_probe() */

#ifdef CONFIG_MMU
static unsigned long __saved_dampr, __saved_damlr;

static inline unsigned long gdbstub_virt_to_pte(unsigned long vaddr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long val, dampr5;

	pgd = (pgd_t *) __get_DAMLR(3) + pgd_index(vaddr);
	pud = pud_offset(pgd, vaddr);
	pmd = pmd_offset(pud, vaddr);

	if (pmd_bad(*pmd) || !pmd_present(*pmd))
		return 0;

	/* make sure dampr5 maps to the correct pmd */
	dampr5 = __get_DAMPR(5);
	val = pmd_val(*pmd);
	__set_DAMPR(5, val | xAMPRx_L | xAMPRx_SS_16Kb | xAMPRx_S | xAMPRx_C | xAMPRx_V);

	/* now its safe to access pmd */
	pte = (pte_t *)__get_DAMLR(5) + __pte_index(vaddr);
	if (pte_present(*pte))
		val = pte_val(*pte);
	else
		val = 0;

	/* restore original dampr5 */
	__set_DAMPR(5, dampr5);

	return val;
}
#endif

static inline int gdbstub_addr_map(const void *vaddr)
{
#ifdef CONFIG_MMU
	unsigned long pte;

	__saved_dampr = __get_DAMPR(2);
	__saved_damlr = __get_DAMLR(2);
#endif
	if (gdbstub_addr_probe(vaddr))
		return 1;
#ifdef CONFIG_MMU
	pte = gdbstub_virt_to_pte((unsigned long) vaddr);
	if (pte) {
		__set_DAMPR(2, pte);
		__set_DAMLR(2, (unsigned long) vaddr & PAGE_MASK);
		return 1;
	}
#endif
	return 0;
}

static inline void gdbstub_addr_unmap(void)
{
#ifdef CONFIG_MMU
	__set_DAMPR(2, __saved_dampr);
	__set_DAMLR(2, __saved_damlr);
#endif
}

/*
 * access potentially dodgy memory through a potentially dodgy pointer
 */
static inline int gdbstub_read_dword(const void *addr, uint32_t *_res)
{
	unsigned long brr;
	uint32_t res;

	if (!gdbstub_addr_map(addr))
		return 0;

	asm volatile("	movgs	gr0,brr	\n"
		     "	ld%I2	%M2,%0	\n"
		     "	movsg	brr,%1	\n"
		     : "=r"(res), "=r"(brr)
		     : "m"(*(uint32_t *) addr));
	*_res = res;
	gdbstub_addr_unmap();
	return likely(!brr);
}

static inline int gdbstub_write_dword(void *addr, uint32_t val)
{
	unsigned long brr;

	if (!gdbstub_addr_map(addr))
		return 0;

	asm volatile("	movgs	gr0,brr	\n"
		     "	st%I2	%1,%M2	\n"
		     "	movsg	brr,%0	\n"
		     : "=r"(brr)
		     : "r"(val), "m"(*(uint32_t *) addr));
	gdbstub_addr_unmap();
	return likely(!brr);
}

static inline int gdbstub_read_word(const void *addr, uint16_t *_res)
{
	unsigned long brr;
	uint16_t res;

	if (!gdbstub_addr_map(addr))
		return 0;

	asm volatile("	movgs	gr0,brr	\n"
		     "	lduh%I2	%M2,%0	\n"
		     "	movsg	brr,%1	\n"
		     : "=r"(res), "=r"(brr)
		     : "m"(*(uint16_t *) addr));
	*_res = res;
	gdbstub_addr_unmap();
	return likely(!brr);
}

static inline int gdbstub_write_word(void *addr, uint16_t val)
{
	unsigned long brr;

	if (!gdbstub_addr_map(addr))
		return 0;

	asm volatile("	movgs	gr0,brr	\n"
		     "	sth%I2	%1,%M2	\n"
		     "	movsg	brr,%0	\n"
		     : "=r"(brr)
		     : "r"(val), "m"(*(uint16_t *) addr));
	gdbstub_addr_unmap();
	return likely(!brr);
}

static inline int gdbstub_read_byte(const void *addr, uint8_t *_res)
{
	unsigned long brr;
	uint8_t res;

	if (!gdbstub_addr_map(addr))
		return 0;

	asm volatile("	movgs	gr0,brr	\n"
		     "	ldub%I2	%M2,%0	\n"
		     "	movsg	brr,%1	\n"
		     : "=r"(res), "=r"(brr)
		     : "m"(*(uint8_t *) addr));
	*_res = res;
	gdbstub_addr_unmap();
	return likely(!brr);
}

static inline int gdbstub_write_byte(void *addr, uint8_t val)
{
	unsigned long brr;

	if (!gdbstub_addr_map(addr))
		return 0;

	asm volatile("	movgs	gr0,brr	\n"
		     "	stb%I2	%1,%M2	\n"
		     "	movsg	brr,%0	\n"
		     : "=r"(brr)
		     : "r"(val), "m"(*(uint8_t *) addr));
	gdbstub_addr_unmap();
	return likely(!brr);
}

static void __gdbstub_console_write(struct console *co, const char *p, unsigned n)
{
	char outbuf[26];
	int qty;

	outbuf[0] = 'O';

	while (n > 0) {
		qty = 1;

		while (n > 0 && qty < 20) {
			mem2hex(p, outbuf + qty, 2, 0);
			qty += 2;
			if (*p == 0x0a) {
				outbuf[qty++] = '0';
				outbuf[qty++] = 'd';
			}
			p++;
			n--;
		}

		outbuf[qty] = 0;
		gdbstub_send_packet(outbuf);
	}
}

#if 0
void debug_to_serial(const char *p, int n)
{
	gdbstub_console_write(NULL,p,n);
}
#endif

#ifdef CONFIG_GDB_CONSOLE

static struct console gdbstub_console = {
	.name	= "gdb",
	.write	= gdbstub_console_write,	/* in break.S */
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};

#endif

/*****************************************************************************/
/*
 * Convert the memory pointed to by mem into hex, placing result in buf.
 * - if successful, return a pointer to the last char put in buf (NUL)
 * - in case of mem fault, return NULL
 * may_fault is non-zero if we are reading from arbitrary memory, but is currently
 * not used.
 */
static unsigned char *mem2hex(const void *_mem, char *buf, int count, int may_fault)
{
	const uint8_t *mem = _mem;
	uint8_t ch[4] __attribute__((aligned(4)));

	if ((uint32_t)mem&1 && count>=1) {
		if (!gdbstub_read_byte(mem,ch))
			return NULL;
		buf = pack_hex_byte(buf, ch[0]);
		mem++;
		count--;
	}

	if ((uint32_t)mem&3 && count>=2) {
		if (!gdbstub_read_word(mem,(uint16_t *)ch))
			return NULL;
		buf = pack_hex_byte(buf, ch[0]);
		buf = pack_hex_byte(buf, ch[1]);
		mem += 2;
		count -= 2;
	}

	while (count>=4) {
		if (!gdbstub_read_dword(mem,(uint32_t *)ch))
			return NULL;
		buf = pack_hex_byte(buf, ch[0]);
		buf = pack_hex_byte(buf, ch[1]);
		buf = pack_hex_byte(buf, ch[2]);
		buf = pack_hex_byte(buf, ch[3]);
		mem += 4;
		count -= 4;
	}

	if (count>=2) {
		if (!gdbstub_read_word(mem,(uint16_t *)ch))
			return NULL;
		buf = pack_hex_byte(buf, ch[0]);
		buf = pack_hex_byte(buf, ch[1]);
		mem += 2;
		count -= 2;
	}

	if (count>=1) {
		if (!gdbstub_read_byte(mem,ch))
			return NULL;
		buf = pack_hex_byte(buf, ch[0]);
	}

	*buf = 0;

	return buf;
} /* end mem2hex() */

/*****************************************************************************/
/*
 * convert the hex array pointed to by buf into binary to be placed in mem
 * return a pointer to the character AFTER the last byte of buffer consumed
 */
static char *hex2mem(const char *buf, void *_mem, int count)
{
	uint8_t *mem = _mem;
	union {
		uint32_t l;
		uint16_t w;
		uint8_t  b[4];
	} ch;

	if ((u32)mem&1 && count>=1) {
		ch.b[0]  = hex(*buf++) << 4;
		ch.b[0] |= hex(*buf++);
		if (!gdbstub_write_byte(mem,ch.b[0]))
			return NULL;
		mem++;
		count--;
	}

	if ((u32)mem&3 && count>=2) {
		ch.b[0]  = hex(*buf++) << 4;
		ch.b[0] |= hex(*buf++);
		ch.b[1]  = hex(*buf++) << 4;
		ch.b[1] |= hex(*buf++);
		if (!gdbstub_write_word(mem,ch.w))
			return NULL;
		mem += 2;
		count -= 2;
	}

	while (count>=4) {
		ch.b[0]  = hex(*buf++) << 4;
		ch.b[0] |= hex(*buf++);
		ch.b[1]  = hex(*buf++) << 4;
		ch.b[1] |= hex(*buf++);
		ch.b[2]  = hex(*buf++) << 4;
		ch.b[2] |= hex(*buf++);
		ch.b[3]  = hex(*buf++) << 4;
		ch.b[3] |= hex(*buf++);
		if (!gdbstub_write_dword(mem,ch.l))
			return NULL;
		mem += 4;
		count -= 4;
	}

	if (count>=2) {
		ch.b[0]  = hex(*buf++) << 4;
		ch.b[0] |= hex(*buf++);
		ch.b[1]  = hex(*buf++) << 4;
		ch.b[1] |= hex(*buf++);
		if (!gdbstub_write_word(mem,ch.w))
			return NULL;
		mem += 2;
		count -= 2;
	}

	if (count>=1) {
		ch.b[0]  = hex(*buf++) << 4;
		ch.b[0] |= hex(*buf++);
		if (!gdbstub_write_byte(mem,ch.b[0]))
			return NULL;
	}

	return (char *) buf;
} /* end hex2mem() */

/*****************************************************************************/
/*
 * This table contains the mapping between FRV TBR.TT exception codes,
 * and signals, which are primarily what GDB understands.  It also
 * indicates which hardware traps we need to commandeer when
 * initializing the stub.
 */
static const struct brr_to_sig_map {
	unsigned long	brr_mask;	/* BRR bitmask */
	unsigned long	tbr_tt;		/* TBR.TT code (in BRR.EBTT) */
	unsigned int	signo;		/* Signal that we map this into */
} brr_to_sig_map[] = {
	{ BRR_EB,	TBR_TT_INSTR_ACC_ERROR,	SIGSEGV		},
	{ BRR_EB,	TBR_TT_ILLEGAL_INSTR,	SIGILL		},
	{ BRR_EB,	TBR_TT_PRIV_INSTR,	SIGILL		},
	{ BRR_EB,	TBR_TT_MP_EXCEPTION,	SIGFPE		},
	{ BRR_EB,	TBR_TT_DATA_ACC_ERROR,	SIGSEGV		},
	{ BRR_EB,	TBR_TT_DATA_STR_ERROR,	SIGSEGV		},
	{ BRR_EB,	TBR_TT_DIVISION_EXCEP,	SIGFPE		},
	{ BRR_EB,	TBR_TT_COMPOUND_EXCEP,	SIGSEGV		},
	{ BRR_EB,	TBR_TT_INTERRUPT_13,	SIGALRM		},	/* watchdog */
	{ BRR_EB,	TBR_TT_INTERRUPT_14,	SIGINT		},	/* GDB serial */
	{ BRR_EB,	TBR_TT_INTERRUPT_15,	SIGQUIT		},	/* NMI */
	{ BRR_CB,	0,			SIGUSR1		},
	{ BRR_TB,	0,			SIGUSR2		},
	{ BRR_DBNEx,	0,			SIGTRAP		},
	{ BRR_DBx,	0,			SIGTRAP		},	/* h/w watchpoint */
	{ BRR_IBx,	0,			SIGTRAP		},	/* h/w breakpoint */
	{ BRR_CBB,	0,			SIGTRAP		},
	{ BRR_SB,	0,			SIGTRAP		},
	{ BRR_ST,	0,			SIGTRAP		},	/* single step */
	{ 0,		0,			SIGHUP		}	/* default */
};

/*****************************************************************************/
/*
 * convert the FRV BRR register contents into a UNIX signal number
 */
static inline int gdbstub_compute_signal(unsigned long brr)
{
	const struct brr_to_sig_map *map;
	unsigned long tbr = (brr & BRR_EBTT) >> 12;

	for (map = brr_to_sig_map; map->brr_mask; map++)
		if (map->brr_mask & brr)
			if (!map->tbr_tt || map->tbr_tt == tbr)
				break;

	return map->signo;
} /* end gdbstub_compute_signal() */

/*****************************************************************************/
/*
 * set a software breakpoint or a hardware breakpoint or watchpoint
 */
static int gdbstub_set_breakpoint(unsigned long type, unsigned long addr, unsigned long len)
{
	unsigned long tmp;
	int bkpt, loop, xloop;

	union {
		struct {
			unsigned long mask0, mask1;
		};
		uint8_t bytes[8];
	} dbmr;

	//gdbstub_printk("setbkpt(%ld,%08lx,%ld)\n", type, addr, len);

	switch (type) {
		/* set software breakpoint */
	case 0:
		if (addr & 3 || len > 7*4)
			return -EINVAL;

		for (bkpt = 255; bkpt >= 0; bkpt--)
			if (!gdbstub_bkpts[bkpt].addr)
				break;
		if (bkpt < 0)
			return -ENOSPC;

		for (loop = 0; loop < len/4; loop++)
			if (!gdbstub_read_dword(&((uint32_t *) addr)[loop],
						&gdbstub_bkpts[bkpt].originsns[loop]))
				return -EFAULT;

		for (loop = 0; loop < len/4; loop++)
			if (!gdbstub_write_dword(&((uint32_t *) addr)[loop],
						 BREAK_INSN)
			    ) {
				/* need to undo the changes if possible */
				for (xloop = 0; xloop < loop; xloop++)
					gdbstub_write_dword(&((uint32_t *) addr)[xloop],
							    gdbstub_bkpts[bkpt].originsns[xloop]);
				return -EFAULT;
			}

		gdbstub_bkpts[bkpt].addr = addr;
		gdbstub_bkpts[bkpt].len = len;

#if 0
		gdbstub_printk("Set BKPT[%02x]: %08lx #%d {%04x, %04x} -> { %04x, %04x }\n",
			       bkpt,
			       gdbstub_bkpts[bkpt].addr,
			       gdbstub_bkpts[bkpt].len,
			       gdbstub_bkpts[bkpt].originsns[0],
			       gdbstub_bkpts[bkpt].originsns[1],
			       ((uint32_t *) addr)[0],
			       ((uint32_t *) addr)[1]
			       );
#endif
		return 0;

		/* set hardware breakpoint */
	case 1:
		if (addr & 3 || len != 4)
			return -EINVAL;

		if (!(__debug_regs->dcr & DCR_IBE0)) {
			//gdbstub_printk("set h/w break 0: %08lx\n", addr);
			__debug_regs->dcr |= DCR_IBE0;
			__debug_regs->ibar[0] = addr;
			asm volatile("movgs %0,ibar0" : : "r"(addr));
			return 0;
		}

		if (!(__debug_regs->dcr & DCR_IBE1)) {
			//gdbstub_printk("set h/w break 1: %08lx\n", addr);
			__debug_regs->dcr |= DCR_IBE1;
			__debug_regs->ibar[1] = addr;
			asm volatile("movgs %0,ibar1" : : "r"(addr));
			return 0;
		}

		if (!(__debug_regs->dcr & DCR_IBE2)) {
			//gdbstub_printk("set h/w break 2: %08lx\n", addr);
			__debug_regs->dcr |= DCR_IBE2;
			__debug_regs->ibar[2] = addr;
			asm volatile("movgs %0,ibar2" : : "r"(addr));
			return 0;
		}

		if (!(__debug_regs->dcr & DCR_IBE3)) {
			//gdbstub_printk("set h/w break 3: %08lx\n", addr);
			__debug_regs->dcr |= DCR_IBE3;
			__debug_regs->ibar[3] = addr;
			asm volatile("movgs %0,ibar3" : : "r"(addr));
			return 0;
		}

		return -ENOSPC;

		/* set data read/write/access watchpoint */
	case 2:
	case 3:
	case 4:
		if ((addr & ~7) != ((addr + len - 1) & ~7))
			return -EINVAL;

		tmp = addr & 7;

		memset(dbmr.bytes, 0xff, sizeof(dbmr.bytes));
		for (loop = 0; loop < len; loop++)
			dbmr.bytes[tmp + loop] = 0;

		addr &= ~7;

		if (!(__debug_regs->dcr & (DCR_DRBE0|DCR_DWBE0))) {
			//gdbstub_printk("set h/w watchpoint 0 type %ld: %08lx\n", type, addr);
			tmp = type==2 ? DCR_DWBE0 : type==3 ? DCR_DRBE0 : DCR_DRBE0|DCR_DWBE0;

			__debug_regs->dcr |= tmp;
			__debug_regs->dbar[0] = addr;
			__debug_regs->dbmr[0][0] = dbmr.mask0;
			__debug_regs->dbmr[0][1] = dbmr.mask1;
			__debug_regs->dbdr[0][0] = 0;
			__debug_regs->dbdr[0][1] = 0;

			asm volatile("	movgs	%0,dbar0	\n"
				     "	movgs	%1,dbmr00	\n"
				     "	movgs	%2,dbmr01	\n"
				     "	movgs	gr0,dbdr00	\n"
				     "	movgs	gr0,dbdr01	\n"
				     : : "r"(addr), "r"(dbmr.mask0), "r"(dbmr.mask1));
			return 0;
		}

		if (!(__debug_regs->dcr & (DCR_DRBE1|DCR_DWBE1))) {
			//gdbstub_printk("set h/w watchpoint 1 type %ld: %08lx\n", type, addr);
			tmp = type==2 ? DCR_DWBE1 : type==3 ? DCR_DRBE1 : DCR_DRBE1|DCR_DWBE1;

			__debug_regs->dcr |= tmp;
			__debug_regs->dbar[1] = addr;
			__debug_regs->dbmr[1][0] = dbmr.mask0;
			__debug_regs->dbmr[1][1] = dbmr.mask1;
			__debug_regs->dbdr[1][0] = 0;
			__debug_regs->dbdr[1][1] = 0;

			asm volatile("	movgs	%0,dbar1	\n"
				     "	movgs	%1,dbmr10	\n"
				     "	movgs	%2,dbmr11	\n"
				     "	movgs	gr0,dbdr10	\n"
				     "	movgs	gr0,dbdr11	\n"
				     : : "r"(addr), "r"(dbmr.mask0), "r"(dbmr.mask1));
			return 0;
		}

		return -ENOSPC;

	default:
		return -EINVAL;
	}

} /* end gdbstub_set_breakpoint() */

/*****************************************************************************/
/*
 * clear a breakpoint or watchpoint
 */
int gdbstub_clear_breakpoint(unsigned long type, unsigned long addr, unsigned long len)
{
	unsigned long tmp;
	int bkpt, loop;

	union {
		struct {
			unsigned long mask0, mask1;
		};
		uint8_t bytes[8];
	} dbmr;

	//gdbstub_printk("clearbkpt(%ld,%08lx,%ld)\n", type, addr, len);

	switch (type) {
		/* clear software breakpoint */
	case 0:
		for (bkpt = 255; bkpt >= 0; bkpt--)
			if (gdbstub_bkpts[bkpt].addr == addr && gdbstub_bkpts[bkpt].len == len)
				break;
		if (bkpt < 0)
			return -ENOENT;

		gdbstub_bkpts[bkpt].addr = 0;

		for (loop = 0; loop < len/4; loop++)
			if (!gdbstub_write_dword(&((uint32_t *) addr)[loop],
						 gdbstub_bkpts[bkpt].originsns[loop]))
				return -EFAULT;
		return 0;

		/* clear hardware breakpoint */
	case 1:
		if (addr & 3 || len != 4)
			return -EINVAL;

#define __get_ibar(X) ({ unsigned long x; asm volatile("movsg ibar"#X",%0" : "=r"(x)); x; })

		if (__debug_regs->dcr & DCR_IBE0 && __get_ibar(0) == addr) {
			//gdbstub_printk("clear h/w break 0: %08lx\n", addr);
			__debug_regs->dcr &= ~DCR_IBE0;
			__debug_regs->ibar[0] = 0;
			asm volatile("movgs gr0,ibar0");
			return 0;
		}

		if (__debug_regs->dcr & DCR_IBE1 && __get_ibar(1) == addr) {
			//gdbstub_printk("clear h/w break 1: %08lx\n", addr);
			__debug_regs->dcr &= ~DCR_IBE1;
			__debug_regs->ibar[1] = 0;
			asm volatile("movgs gr0,ibar1");
			return 0;
		}

		if (__debug_regs->dcr & DCR_IBE2 && __get_ibar(2) == addr) {
			//gdbstub_printk("clear h/w break 2: %08lx\n", addr);
			__debug_regs->dcr &= ~DCR_IBE2;
			__debug_regs->ibar[2] = 0;
			asm volatile("movgs gr0,ibar2");
			return 0;
		}

		if (__debug_regs->dcr & DCR_IBE3 && __get_ibar(3) == addr) {
			//gdbstub_printk("clear h/w break 3: %08lx\n", addr);
			__debug_regs->dcr &= ~DCR_IBE3;
			__debug_regs->ibar[3] = 0;
			asm volatile("movgs gr0,ibar3");
			return 0;
		}

		return -EINVAL;

		/* clear data read/write/access watchpoint */
	case 2:
	case 3:
	case 4:
		if ((addr & ~7) != ((addr + len - 1) & ~7))
			return -EINVAL;

		tmp = addr & 7;

		memset(dbmr.bytes, 0xff, sizeof(dbmr.bytes));
		for (loop = 0; loop < len; loop++)
			dbmr.bytes[tmp + loop] = 0;

		addr &= ~7;

#define __get_dbar(X) ({ unsigned long x; asm volatile("movsg dbar"#X",%0" : "=r"(x)); x; })
#define __get_dbmr0(X) ({ unsigned long x; asm volatile("movsg dbmr"#X"0,%0" : "=r"(x)); x; })
#define __get_dbmr1(X) ({ unsigned long x; asm volatile("movsg dbmr"#X"1,%0" : "=r"(x)); x; })

		/* consider DBAR 0 */
		tmp = type==2 ? DCR_DWBE0 : type==3 ? DCR_DRBE0 : DCR_DRBE0|DCR_DWBE0;

		if ((__debug_regs->dcr & (DCR_DRBE0|DCR_DWBE0)) != tmp ||
		    __get_dbar(0) != addr ||
		    __get_dbmr0(0) != dbmr.mask0 ||
		    __get_dbmr1(0) != dbmr.mask1)
			goto skip_dbar0;

		//gdbstub_printk("clear h/w watchpoint 0 type %ld: %08lx\n", type, addr);
		__debug_regs->dcr &= ~(DCR_DRBE0|DCR_DWBE0);
		__debug_regs->dbar[0] = 0;
		__debug_regs->dbmr[0][0] = 0;
		__debug_regs->dbmr[0][1] = 0;
		__debug_regs->dbdr[0][0] = 0;
		__debug_regs->dbdr[0][1] = 0;

		asm volatile("	movgs	gr0,dbar0	\n"
			     "	movgs	gr0,dbmr00	\n"
			     "	movgs	gr0,dbmr01	\n"
			     "	movgs	gr0,dbdr00	\n"
			     "	movgs	gr0,dbdr01	\n");
		return 0;

	skip_dbar0:
		/* consider DBAR 0 */
		tmp = type==2 ? DCR_DWBE1 : type==3 ? DCR_DRBE1 : DCR_DRBE1|DCR_DWBE1;

		if ((__debug_regs->dcr & (DCR_DRBE1|DCR_DWBE1)) != tmp ||
		    __get_dbar(1) != addr ||
		    __get_dbmr0(1) != dbmr.mask0 ||
		    __get_dbmr1(1) != dbmr.mask1)
			goto skip_dbar1;

		//gdbstub_printk("clear h/w watchpoint 1 type %ld: %08lx\n", type, addr);
		__debug_regs->dcr &= ~(DCR_DRBE1|DCR_DWBE1);
		__debug_regs->dbar[1] = 0;
		__debug_regs->dbmr[1][0] = 0;
		__debug_regs->dbmr[1][1] = 0;
		__debug_regs->dbdr[1][0] = 0;
		__debug_regs->dbdr[1][1] = 0;

		asm volatile("	movgs	gr0,dbar1	\n"
			     "	movgs	gr0,dbmr10	\n"
			     "	movgs	gr0,dbmr11	\n"
			     "	movgs	gr0,dbdr10	\n"
			     "	movgs	gr0,dbdr11	\n");
		return 0;

	skip_dbar1:
		return -ENOSPC;

	default:
		return -EINVAL;
	}
} /* end gdbstub_clear_breakpoint() */

/*****************************************************************************/
/*
 * check a for an internal software breakpoint, and wind the PC back if necessary
 */
static void gdbstub_check_breakpoint(void)
{
	unsigned long addr = __debug_frame->pc - 4;
	int bkpt;

	for (bkpt = 255; bkpt >= 0; bkpt--)
		if (gdbstub_bkpts[bkpt].addr == addr)
			break;
	if (bkpt >= 0)
		__debug_frame->pc = addr;

	//gdbstub_printk("alter pc [%d] %08lx\n", bkpt, __debug_frame->pc);

} /* end gdbstub_check_breakpoint() */

/*****************************************************************************/
/*
 *
 */
static void __maybe_unused gdbstub_show_regs(void)
{
	unsigned long *reg;
	int loop;

	gdbstub_printk("\n");

	gdbstub_printk("Frame: @%p [%s]\n",
		       __debug_frame,
		       __debug_frame->psr & PSR_S ? "kernel" : "user");

	reg = (unsigned long *) __debug_frame;
	for (loop = 0; loop < NR_PT_REGS; loop++) {
		printk("%s %08lx", regnames[loop + 0], reg[loop + 0]);

		if (loop == NR_PT_REGS - 1 || loop % 5 == 4)
			printk("\n");
		else
			printk(" | ");
	}

	gdbstub_printk("Process %s (pid: %d)\n", current->comm, current->pid);
} /* end gdbstub_show_regs() */

/*****************************************************************************/
/*
 * dump debugging regs
 */
static void __maybe_unused gdbstub_dump_debugregs(void)
{
	gdbstub_printk("DCR    %08lx  ", __debug_status.dcr);
	gdbstub_printk("BRR    %08lx\n", __debug_status.brr);

	gdbstub_printk("IBAR0  %08lx  ", __get_ibar(0));
	gdbstub_printk("IBAR1  %08lx  ", __get_ibar(1));
	gdbstub_printk("IBAR2  %08lx  ", __get_ibar(2));
	gdbstub_printk("IBAR3  %08lx\n", __get_ibar(3));

	gdbstub_printk("DBAR0  %08lx  ", __get_dbar(0));
	gdbstub_printk("DBMR00 %08lx  ", __get_dbmr0(0));
	gdbstub_printk("DBMR01 %08lx\n", __get_dbmr1(0));

	gdbstub_printk("DBAR1  %08lx  ", __get_dbar(1));
	gdbstub_printk("DBMR10 %08lx  ", __get_dbmr0(1));
	gdbstub_printk("DBMR11 %08lx\n", __get_dbmr1(1));

	gdbstub_printk("\n");
} /* end gdbstub_dump_debugregs() */

/*****************************************************************************/
/*
 * dump the MMU state into a structure so that it can be accessed with GDB
 */
void gdbstub_get_mmu_state(void)
{
	asm volatile("movsg hsr0,%0" : "=r"(__debug_mmu.regs.hsr0));
	asm volatile("movsg pcsr,%0" : "=r"(__debug_mmu.regs.pcsr));
	asm volatile("movsg esr0,%0" : "=r"(__debug_mmu.regs.esr0));
	asm volatile("movsg ear0,%0" : "=r"(__debug_mmu.regs.ear0));
	asm volatile("movsg epcr0,%0" : "=r"(__debug_mmu.regs.epcr0));

	/* read the protection / SAT registers */
	__debug_mmu.iamr[0].L  = __get_IAMLR(0);
	__debug_mmu.iamr[0].P  = __get_IAMPR(0);
	__debug_mmu.iamr[1].L  = __get_IAMLR(1);
	__debug_mmu.iamr[1].P  = __get_IAMPR(1);
	__debug_mmu.iamr[2].L  = __get_IAMLR(2);
	__debug_mmu.iamr[2].P  = __get_IAMPR(2);
	__debug_mmu.iamr[3].L  = __get_IAMLR(3);
	__debug_mmu.iamr[3].P  = __get_IAMPR(3);
	__debug_mmu.iamr[4].L  = __get_IAMLR(4);
	__debug_mmu.iamr[4].P  = __get_IAMPR(4);
	__debug_mmu.iamr[5].L  = __get_IAMLR(5);
	__debug_mmu.iamr[5].P  = __get_IAMPR(5);
	__debug_mmu.iamr[6].L  = __get_IAMLR(6);
	__debug_mmu.iamr[6].P  = __get_IAMPR(6);
	__debug_mmu.iamr[7].L  = __get_IAMLR(7);
	__debug_mmu.iamr[7].P  = __get_IAMPR(7);
	__debug_mmu.iamr[8].L  = __get_IAMLR(8);
	__debug_mmu.iamr[8].P  = __get_IAMPR(8);
	__debug_mmu.iamr[9].L  = __get_IAMLR(9);
	__debug_mmu.iamr[9].P  = __get_IAMPR(9);
	__debug_mmu.iamr[10].L = __get_IAMLR(10);
	__debug_mmu.iamr[10].P = __get_IAMPR(10);
	__debug_mmu.iamr[11].L = __get_IAMLR(11);
	__debug_mmu.iamr[11].P = __get_IAMPR(11);
	__debug_mmu.iamr[12].L = __get_IAMLR(12);
	__debug_mmu.iamr[12].P = __get_IAMPR(12);
	__debug_mmu.iamr[13].L = __get_IAMLR(13);
	__debug_mmu.iamr[13].P = __get_IAMPR(13);
	__debug_mmu.iamr[14].L = __get_IAMLR(14);
	__debug_mmu.iamr[14].P = __get_IAMPR(14);
	__debug_mmu.iamr[15].L = __get_IAMLR(15);
	__debug_mmu.iamr[15].P = __get_IAMPR(15);

	__debug_mmu.damr[0].L  = __get_DAMLR(0);
	__debug_mmu.damr[0].P  = __get_DAMPR(0);
	__debug_mmu.damr[1].L  = __get_DAMLR(1);
	__debug_mmu.damr[1].P  = __get_DAMPR(1);
	__debug_mmu.damr[2].L  = __get_DAMLR(2);
	__debug_mmu.damr[2].P  = __get_DAMPR(2);
	__debug_mmu.damr[3].L  = __get_DAMLR(3);
	__debug_mmu.damr[3].P  = __get_DAMPR(3);
	__debug_mmu.damr[4].L  = __get_DAMLR(4);
	__debug_mmu.damr[4].P  = __get_DAMPR(4);
	__debug_mmu.damr[5].L  = __get_DAMLR(5);
	__debug_mmu.damr[5].P  = __get_DAMPR(5);
	__debug_mmu.damr[6].L  = __get_DAMLR(6);
	__debug_mmu.damr[6].P  = __get_DAMPR(6);
	__debug_mmu.damr[7].L  = __get_DAMLR(7);
	__debug_mmu.damr[7].P  = __get_DAMPR(7);
	__debug_mmu.damr[8].L  = __get_DAMLR(8);
	__debug_mmu.damr[8].P  = __get_DAMPR(8);
	__debug_mmu.damr[9].L  = __get_DAMLR(9);
	__debug_mmu.damr[9].P  = __get_DAMPR(9);
	__debug_mmu.damr[10].L = __get_DAMLR(10);
	__debug_mmu.damr[10].P = __get_DAMPR(10);
	__debug_mmu.damr[11].L = __get_DAMLR(11);
	__debug_mmu.damr[11].P = __get_DAMPR(11);
	__debug_mmu.damr[12].L = __get_DAMLR(12);
	__debug_mmu.damr[12].P = __get_DAMPR(12);
	__debug_mmu.damr[13].L = __get_DAMLR(13);
	__debug_mmu.damr[13].P = __get_DAMPR(13);
	__debug_mmu.damr[14].L = __get_DAMLR(14);
	__debug_mmu.damr[14].P = __get_DAMPR(14);
	__debug_mmu.damr[15].L = __get_DAMLR(15);
	__debug_mmu.damr[15].P = __get_DAMPR(15);

#ifdef CONFIG_MMU
	do {
		/* read the DAT entries from the TLB */
		struct __debug_amr *p;
		int loop;

		asm volatile("movsg tplr,%0" : "=r"(__debug_mmu.regs.tplr));
		asm volatile("movsg tppr,%0" : "=r"(__debug_mmu.regs.tppr));
		asm volatile("movsg tpxr,%0" : "=r"(__debug_mmu.regs.tpxr));
		asm volatile("movsg cxnr,%0" : "=r"(__debug_mmu.regs.cxnr));

		p = __debug_mmu.tlb;

		/* way 0 */
		asm volatile("movgs %0,tpxr" :: "r"(0 << TPXR_WAY_SHIFT));
		for (loop = 0; loop < 64; loop++) {
			asm volatile("tlbpr %0,gr0,#1,#0" :: "r"(loop << PAGE_SHIFT));
			asm volatile("movsg tplr,%0" : "=r"(p->L));
			asm volatile("movsg tppr,%0" : "=r"(p->P));
			p++;
		}

		/* way 1 */
		asm volatile("movgs %0,tpxr" :: "r"(1 << TPXR_WAY_SHIFT));
		for (loop = 0; loop < 64; loop++) {
			asm volatile("tlbpr %0,gr0,#1,#0" :: "r"(loop << PAGE_SHIFT));
			asm volatile("movsg tplr,%0" : "=r"(p->L));
			asm volatile("movsg tppr,%0" : "=r"(p->P));
			p++;
		}

		asm volatile("movgs %0,tplr" :: "r"(__debug_mmu.regs.tplr));
		asm volatile("movgs %0,tppr" :: "r"(__debug_mmu.regs.tppr));
		asm volatile("movgs %0,tpxr" :: "r"(__debug_mmu.regs.tpxr));
	} while(0);
#endif

} /* end gdbstub_get_mmu_state() */

/*****************************************************************************/
/*
 * handle event interception and GDB remote protocol processing
 * - on entry:
 *	PSR.ET==0, PSR.S==1 and the CPU is in debug mode
 *	__debug_frame points to the saved registers
 *	__frame points to the kernel mode exception frame, if it was in kernel
 *      mode when the break happened
 */
void gdbstub(int sigval)
{
	unsigned long addr, length, loop, dbar, temp, temp2, temp3;
	uint32_t zero;
	char *ptr;
	int flush_cache = 0;

	LEDS(0x5000);

	if (sigval < 0) {
#ifndef CONFIG_GDBSTUB_IMMEDIATE
		/* return immediately if GDB immediate activation option not set */
		return;
#else
		sigval = SIGINT;
#endif
	}

	save_user_regs(&__debug_frame0->uc);

#if 0
	gdbstub_printk("--> gdbstub() %08x %p %08x %08x\n",
		       __debug_frame->pc,
		       __debug_frame,
		       __debug_regs->brr,
		       __debug_regs->bpsr);
//	gdbstub_show_regs();
#endif

	LEDS(0x5001);

	/* if we were interrupted by input on the serial gdbstub serial port,
	 * restore the context prior to the interrupt so that we return to that
	 * directly
	 */
	temp = (unsigned long) __entry_kerneltrap_table;
	temp2 = (unsigned long) __entry_usertrap_table;
	temp3 = __debug_frame->pc & ~15;

	if (temp3 == temp + TBR_TT_INTERRUPT_15 ||
	    temp3 == temp2 + TBR_TT_INTERRUPT_15
	    ) {
		asm volatile("movsg pcsr,%0" : "=r"(__debug_frame->pc));
		__debug_frame->psr |= PSR_ET;
		__debug_frame->psr &= ~PSR_S;
		if (__debug_frame->psr & PSR_PS)
			__debug_frame->psr |= PSR_S;
		__debug_status.brr = (__debug_frame->tbr & TBR_TT) << 12;
		__debug_status.brr |= BRR_EB;
		sigval = SIGINT;
	}

	/* handle the decrement timer going off (FR451 only) */
	if (temp3 == temp + TBR_TT_DECREMENT_TIMER ||
	    temp3 == temp2 + TBR_TT_DECREMENT_TIMER
	    ) {
		asm volatile("movgs %0,timerd" :: "r"(10000000));
		asm volatile("movsg pcsr,%0" : "=r"(__debug_frame->pc));
		__debug_frame->psr |= PSR_ET;
		__debug_frame->psr &= ~PSR_S;
		if (__debug_frame->psr & PSR_PS)
			__debug_frame->psr |= PSR_S;
		__debug_status.brr = (__debug_frame->tbr & TBR_TT) << 12;
		__debug_status.brr |= BRR_EB;
		sigval = SIGXCPU;
	}

	LEDS(0x5002);

	/* after a BREAK insn, the PC lands on the far side of it */
	if (__debug_status.brr & BRR_SB)
		gdbstub_check_breakpoint();

	LEDS(0x5003);

	/* handle attempts to write console data via GDB "O" commands */
	if (__debug_frame->pc == (unsigned long) gdbstub_console_write + 4) {
		__gdbstub_console_write((struct console *) __debug_frame->gr8,
					(const char *) __debug_frame->gr9,
					(unsigned) __debug_frame->gr10);
		goto done;
	}

	if (gdbstub_rx_unget) {
		sigval = SIGINT;
		goto packet_waiting;
	}

	if (!sigval)
		sigval = gdbstub_compute_signal(__debug_status.brr);

	LEDS(0x5004);

	/* send a message to the debugger's user saying what happened if it may
	 * not be clear cut (we can't map exceptions onto signals properly)
	 */
	if (sigval != SIGINT && sigval != SIGTRAP && sigval != SIGILL) {
		static const char title[] = "Break ";
		static const char crlf[] = "\r\n";
		unsigned long brr = __debug_status.brr;
		char hx;

		ptr = output_buffer;
		*ptr++ = 'O';
		ptr = mem2hex(title, ptr, sizeof(title) - 1,0);

		hx = hex_asc_hi(brr >> 24);
		ptr = pack_hex_byte(ptr, hx);
		hx = hex_asc_lo(brr >> 24);
		ptr = pack_hex_byte(ptr, hx);
		hx = hex_asc_hi(brr >> 16);
		ptr = pack_hex_byte(ptr, hx);
		hx = hex_asc_lo(brr >> 16);
		ptr = pack_hex_byte(ptr, hx);
		hx = hex_asc_hi(brr >> 8);
		ptr = pack_hex_byte(ptr, hx);
		hx = hex_asc_lo(brr >> 8);
		ptr = pack_hex_byte(ptr, hx);
		hx = hex_asc_hi(brr);
		ptr = pack_hex_byte(ptr, hx);
		hx = hex_asc_lo(brr);
		ptr = pack_hex_byte(ptr, hx);

		ptr = mem2hex(crlf, ptr, sizeof(crlf) - 1, 0);
		*ptr = 0;
		gdbstub_send_packet(output_buffer);	/* send it off... */
	}

	LEDS(0x5005);

	/* tell the debugger that an exception has occurred */
	ptr = output_buffer;

	/* Send trap type (converted to signal) */
	*ptr++ = 'T';
	ptr = pack_hex_byte(ptr, sigval);

	/* Send Error PC */
	ptr = pack_hex_byte(ptr, GDB_REG_PC);
	*ptr++ = ':';
	ptr = mem2hex(&__debug_frame->pc, ptr, 4, 0);
	*ptr++ = ';';

	/*
	 * Send frame pointer
	 */
	ptr = pack_hex_byte(ptr, GDB_REG_FP);
	*ptr++ = ':';
	ptr = mem2hex(&__debug_frame->fp, ptr, 4, 0);
	*ptr++ = ';';

	/*
	 * Send stack pointer
	 */
	ptr = pack_hex_byte(ptr, GDB_REG_SP);
	*ptr++ = ':';
	ptr = mem2hex(&__debug_frame->sp, ptr, 4, 0);
	*ptr++ = ';';

	*ptr++ = 0;
	gdbstub_send_packet(output_buffer);	/* send it off... */

	LEDS(0x5006);

 packet_waiting:
	gdbstub_get_mmu_state();

	/* wait for input from remote GDB */
	while (1) {
		output_buffer[0] = 0;

		LEDS(0x5007);
		gdbstub_recv_packet(input_buffer);
		LEDS(0x5600 | input_buffer[0]);

		switch (input_buffer[0]) {
			/* request repeat of last signal number */
		case '?':
			output_buffer[0] = 'S';
			output_buffer[1] = hex_asc_hi(sigval);
			output_buffer[2] = hex_asc_lo(sigval);
			output_buffer[3] = 0;
			break;

		case 'd':
			/* toggle debug flag */
			break;

			/* return the value of the CPU registers
			 * - GR0,  GR1,  GR2,  GR3,  GR4,  GR5,  GR6,  GR7,
			 * - GR8,  GR9,  GR10, GR11, GR12, GR13, GR14, GR15,
			 * - GR16, GR17, GR18, GR19, GR20, GR21, GR22, GR23,
			 * - GR24, GR25, GR26, GR27, GR28, GR29, GR30, GR31,
			 * - GR32, GR33, GR34, GR35, GR36, GR37, GR38, GR39,
			 * - GR40, GR41, GR42, GR43, GR44, GR45, GR46, GR47,
			 * - GR48, GR49, GR50, GR51, GR52, GR53, GR54, GR55,
			 * - GR56, GR57, GR58, GR59, GR60, GR61, GR62, GR63,
			 * - FP0,  FP1,  FP2,  FP3,  FP4,  FP5,  FP6,  FP7,
			 * - FP8,  FP9,  FP10, FP11, FP12, FP13, FP14, FP15,
			 * - FP16, FP17, FP18, FP19, FP20, FP21, FP22, FP23,
			 * - FP24, FP25, FP26, FP27, FP28, FP29, FP30, FP31,
			 * - FP32, FP33, FP34, FP35, FP36, FP37, FP38, FP39,
			 * - FP40, FP41, FP42, FP43, FP44, FP45, FP46, FP47,
			 * - FP48, FP49, FP50, FP51, FP52, FP53, FP54, FP55,
			 * - FP56, FP57, FP58, FP59, FP60, FP61, FP62, FP63,
			 * - PC, PSR, CCR, CCCR,
			 * - _X132, _X133, _X134
			 * - TBR, BRR, DBAR0, DBAR1, DBAR2, DBAR3,
			 * - _X141, _X142, _X143, _X144,
			 * - LR, LCR
			 */
		case 'g':
			zero = 0;
			ptr = output_buffer;

			/* deal with GR0, GR1-GR27, GR28-GR31, GR32-GR63 */
			ptr = mem2hex(&zero, ptr, 4, 0);

			for (loop = 1; loop <= 27; loop++)
				ptr = mem2hex(&__debug_user_context->i.gr[loop], ptr, 4, 0);
			temp = (unsigned long) __frame;
			ptr = mem2hex(&temp, ptr, 4, 0);
			ptr = mem2hex(&__debug_user_context->i.gr[29], ptr, 4, 0);
			ptr = mem2hex(&__debug_user_context->i.gr[30], ptr, 4, 0);
#ifdef CONFIG_MMU
			ptr = mem2hex(&__debug_user_context->i.gr[31], ptr, 4, 0);
#else
			temp = (unsigned long) __debug_frame;
			ptr = mem2hex(&temp, ptr, 4, 0);
#endif

			for (loop = 32; loop <= 63; loop++)
				ptr = mem2hex(&__debug_user_context->i.gr[loop], ptr, 4, 0);

			/* deal with FR0-FR63 */
			for (loop = 0; loop <= 63; loop++)
				ptr = mem2hex(&__debug_user_context->f.fr[loop], ptr, 4, 0);

			/* deal with special registers */
			ptr = mem2hex(&__debug_frame->pc,    ptr, 4, 0);
			ptr = mem2hex(&__debug_frame->psr,   ptr, 4, 0);
			ptr = mem2hex(&__debug_frame->ccr,   ptr, 4, 0);
			ptr = mem2hex(&__debug_frame->cccr,  ptr, 4, 0);
			ptr = mem2hex(&zero, ptr, 4, 0);
			ptr = mem2hex(&zero, ptr, 4, 0);
			ptr = mem2hex(&zero, ptr, 4, 0);
			ptr = mem2hex(&__debug_frame->tbr,   ptr, 4, 0);
			ptr = mem2hex(&__debug_status.brr ,   ptr, 4, 0);

			asm volatile("movsg dbar0,%0" : "=r"(dbar));
			ptr = mem2hex(&dbar, ptr, 4, 0);
			asm volatile("movsg dbar1,%0" : "=r"(dbar));
			ptr = mem2hex(&dbar, ptr, 4, 0);
			asm volatile("movsg dbar2,%0" : "=r"(dbar));
			ptr = mem2hex(&dbar, ptr, 4, 0);
			asm volatile("movsg dbar3,%0" : "=r"(dbar));
			ptr = mem2hex(&dbar, ptr, 4, 0);

			asm volatile("movsg scr0,%0" : "=r"(dbar));
			ptr = mem2hex(&dbar, ptr, 4, 0);
			asm volatile("movsg scr1,%0" : "=r"(dbar));
			ptr = mem2hex(&dbar, ptr, 4, 0);
			asm volatile("movsg scr2,%0" : "=r"(dbar));
			ptr = mem2hex(&dbar, ptr, 4, 0);
			asm volatile("movsg scr3,%0" : "=r"(dbar));
			ptr = mem2hex(&dbar, ptr, 4, 0);

			ptr = mem2hex(&__debug_frame->lr, ptr, 4, 0);
			ptr = mem2hex(&__debug_frame->lcr, ptr, 4, 0);

			ptr = mem2hex(&__debug_frame->iacc0, ptr, 8, 0);

			ptr = mem2hex(&__debug_user_context->f.fsr[0], ptr, 4, 0);

			for (loop = 0; loop <= 7; loop++)
				ptr = mem2hex(&__debug_user_context->f.acc[loop], ptr, 4, 0);

			ptr = mem2hex(&__debug_user_context->f.accg, ptr, 8, 0);

			for (loop = 0; loop <= 1; loop++)
				ptr = mem2hex(&__debug_user_context->f.msr[loop], ptr, 4, 0);

			ptr = mem2hex(&__debug_frame->gner0, ptr, 4, 0);
			ptr = mem2hex(&__debug_frame->gner1, ptr, 4, 0);

			ptr = mem2hex(&__debug_user_context->f.fner[0], ptr, 4, 0);
			ptr = mem2hex(&__debug_user_context->f.fner[1], ptr, 4, 0);

			break;

			/* set the values of the CPU registers */
		case 'G':
			ptr = &input_buffer[1];

			/* deal with GR0, GR1-GR27, GR28-GR31, GR32-GR63 */
			ptr = hex2mem(ptr, &temp, 4);

			for (loop = 1; loop <= 27; loop++)
				ptr = hex2mem(ptr, &__debug_user_context->i.gr[loop], 4);

			ptr = hex2mem(ptr, &temp, 4);
			__frame = (struct pt_regs *) temp;
			ptr = hex2mem(ptr, &__debug_frame->gr29, 4);
			ptr = hex2mem(ptr, &__debug_frame->gr30, 4);
#ifdef CONFIG_MMU
			ptr = hex2mem(ptr, &__debug_frame->gr31, 4);
#else
			ptr = hex2mem(ptr, &temp, 4);
#endif

			for (loop = 32; loop <= 63; loop++)
				ptr = hex2mem(ptr, &__debug_user_context->i.gr[loop], 4);

			/* deal with FR0-FR63 */
			for (loop = 0; loop <= 63; loop++)
				ptr = mem2hex(&__debug_user_context->f.fr[loop], ptr, 4, 0);

			/* deal with special registers */
			ptr = hex2mem(ptr, &__debug_frame->pc,  4);
			ptr = hex2mem(ptr, &__debug_frame->psr, 4);
			ptr = hex2mem(ptr, &__debug_frame->ccr, 4);
			ptr = hex2mem(ptr, &__debug_frame->cccr,4);

			for (loop = 132; loop <= 140; loop++)
				ptr = hex2mem(ptr, &temp, 4);

			ptr = hex2mem(ptr, &temp, 4);
			asm volatile("movgs %0,scr0" :: "r"(temp));
			ptr = hex2mem(ptr, &temp, 4);
			asm volatile("movgs %0,scr1" :: "r"(temp));
			ptr = hex2mem(ptr, &temp, 4);
			asm volatile("movgs %0,scr2" :: "r"(temp));
			ptr = hex2mem(ptr, &temp, 4);
			asm volatile("movgs %0,scr3" :: "r"(temp));

			ptr = hex2mem(ptr, &__debug_frame->lr,  4);
			ptr = hex2mem(ptr, &__debug_frame->lcr, 4);

			ptr = hex2mem(ptr, &__debug_frame->iacc0, 8);

			ptr = hex2mem(ptr, &__debug_user_context->f.fsr[0], 4);

			for (loop = 0; loop <= 7; loop++)
				ptr = hex2mem(ptr, &__debug_user_context->f.acc[loop], 4);

			ptr = hex2mem(ptr, &__debug_user_context->f.accg, 8);

			for (loop = 0; loop <= 1; loop++)
				ptr = hex2mem(ptr, &__debug_user_context->f.msr[loop], 4);

			ptr = hex2mem(ptr, &__debug_frame->gner0, 4);
			ptr = hex2mem(ptr, &__debug_frame->gner1, 4);

			ptr = hex2mem(ptr, &__debug_user_context->f.fner[0], 4);
			ptr = hex2mem(ptr, &__debug_user_context->f.fner[1], 4);

			gdbstub_strcpy(output_buffer,"OK");
			break;

			/* mAA..AA,LLLL  Read LLLL bytes at address AA..AA */
		case 'm':
			ptr = &input_buffer[1];

			if (hexToInt(&ptr, &addr) &&
			    *ptr++ == ',' &&
			    hexToInt(&ptr, &length)
			    ) {
				if (mem2hex((char *)addr, output_buffer, length, 1))
					break;
				gdbstub_strcpy (output_buffer, "E03");
			}
			else {
				gdbstub_strcpy(output_buffer,"E01");
			}
			break;

			/* MAA..AA,LLLL: Write LLLL bytes at address AA.AA return OK */
		case 'M':
			ptr = &input_buffer[1];

			if (hexToInt(&ptr, &addr) &&
			    *ptr++ == ',' &&
			    hexToInt(&ptr, &length) &&
			    *ptr++ == ':'
			    ) {
				if (hex2mem(ptr, (char *)addr, length)) {
					gdbstub_strcpy(output_buffer, "OK");
				}
				else {
					gdbstub_strcpy(output_buffer, "E03");
				}
			}
			else
				gdbstub_strcpy(output_buffer, "E02");

			flush_cache = 1;
			break;

			/* PNN,=RRRRRRRR: Write value R to reg N return OK */
		case 'P':
			ptr = &input_buffer[1];

			if (!hexToInt(&ptr, &addr) ||
			    *ptr++ != '=' ||
			    !hexToInt(&ptr, &temp)
			    ) {
				gdbstub_strcpy(output_buffer, "E01");
				break;
			}

			temp2 = 1;
			switch (addr) {
			case GDB_REG_GR(0):
				break;
			case GDB_REG_GR(1) ... GDB_REG_GR(63):
				__debug_user_context->i.gr[addr - GDB_REG_GR(0)] = temp;
				break;
			case GDB_REG_FR(0) ... GDB_REG_FR(63):
				__debug_user_context->f.fr[addr - GDB_REG_FR(0)] = temp;
				break;
			case GDB_REG_PC:
				__debug_user_context->i.pc = temp;
				break;
			case GDB_REG_PSR:
				__debug_user_context->i.psr = temp;
				break;
			case GDB_REG_CCR:
				__debug_user_context->i.ccr = temp;
				break;
			case GDB_REG_CCCR:
				__debug_user_context->i.cccr = temp;
				break;
			case GDB_REG_BRR:
				__debug_status.brr = temp;
				break;
			case GDB_REG_LR:
				__debug_user_context->i.lr = temp;
				break;
			case GDB_REG_LCR:
				__debug_user_context->i.lcr = temp;
				break;
			case GDB_REG_FSR0:
				__debug_user_context->f.fsr[0] = temp;
				break;
			case GDB_REG_ACC(0) ... GDB_REG_ACC(7):
				__debug_user_context->f.acc[addr - GDB_REG_ACC(0)] = temp;
				break;
			case GDB_REG_ACCG(0):
				*(uint32_t *) &__debug_user_context->f.accg[0] = temp;
				break;
			case GDB_REG_ACCG(4):
				*(uint32_t *) &__debug_user_context->f.accg[4] = temp;
				break;
			case GDB_REG_MSR(0) ... GDB_REG_MSR(1):
				__debug_user_context->f.msr[addr - GDB_REG_MSR(0)] = temp;
				break;
			case GDB_REG_GNER(0) ... GDB_REG_GNER(1):
				__debug_user_context->i.gner[addr - GDB_REG_GNER(0)] = temp;
				break;
			case GDB_REG_FNER(0) ... GDB_REG_FNER(1):
				__debug_user_context->f.fner[addr - GDB_REG_FNER(0)] = temp;
				break;
			default:
				temp2 = 0;
				break;
			}

			if (temp2) {
				gdbstub_strcpy(output_buffer, "OK");
			}
			else {
				gdbstub_strcpy(output_buffer, "E02");
			}
			break;

			/* cAA..AA    Continue at address AA..AA(optional) */
		case 'c':
			/* try to read optional parameter, pc unchanged if no parm */
			ptr = &input_buffer[1];
			if (hexToInt(&ptr, &addr))
				__debug_frame->pc = addr;
			goto done;

			/* kill the program */
		case 'k' :
			goto done;	/* just continue */


			/* reset the whole machine (FIXME: system dependent) */
		case 'r':
			break;


			/* step to next instruction */
		case 's':
			__debug_regs->dcr |= DCR_SE;
			__debug_status.dcr |= DCR_SE;
			goto done;

			/* set baud rate (bBB) */
		case 'b':
			ptr = &input_buffer[1];
			if (!hexToInt(&ptr, &temp)) {
				gdbstub_strcpy(output_buffer,"B01");
				break;
			}

			if (temp) {
				/* ack before changing speed */
				gdbstub_send_packet("OK");
				gdbstub_set_baud(temp);
			}
			break;

			/* set breakpoint */
		case 'Z':
			ptr = &input_buffer[1];

			if (!hexToInt(&ptr,&temp) || *ptr++ != ',' ||
			    !hexToInt(&ptr,&addr) || *ptr++ != ',' ||
			    !hexToInt(&ptr,&length)
			    ) {
				gdbstub_strcpy(output_buffer,"E01");
				break;
			}

			if (temp >= 5) {
				gdbstub_strcpy(output_buffer,"E03");
				break;
			}

			if (gdbstub_set_breakpoint(temp, addr, length) < 0) {
				gdbstub_strcpy(output_buffer,"E03");
				break;
			}

			if (temp == 0)
				flush_cache = 1; /* soft bkpt by modified memory */

			gdbstub_strcpy(output_buffer,"OK");
			break;

			/* clear breakpoint */
		case 'z':
			ptr = &input_buffer[1];

			if (!hexToInt(&ptr,&temp) || *ptr++ != ',' ||
			    !hexToInt(&ptr,&addr) || *ptr++ != ',' ||
			    !hexToInt(&ptr,&length)
			    ) {
				gdbstub_strcpy(output_buffer,"E01");
				break;
			}

			if (temp >= 5) {
				gdbstub_strcpy(output_buffer,"E03");
				break;
			}

			if (gdbstub_clear_breakpoint(temp, addr, length) < 0) {
				gdbstub_strcpy(output_buffer,"E03");
				break;
			}

			if (temp == 0)
				flush_cache = 1; /* soft bkpt by modified memory */

			gdbstub_strcpy(output_buffer,"OK");
			break;

		default:
			gdbstub_proto("### GDB Unsupported Cmd '%s'\n",input_buffer);
			break;
		}

		/* reply to the request */
		LEDS(0x5009);
		gdbstub_send_packet(output_buffer);
	}

 done:
	restore_user_regs(&__debug_frame0->uc);

	//gdbstub_dump_debugregs();
	//gdbstub_printk("<-- gdbstub() %08x\n", __debug_frame->pc);

	/* need to flush the instruction cache before resuming, as we may have
	 * deposited a breakpoint, and the icache probably has no way of
	 * knowing that a data ref to some location may have changed something
	 * that is in the instruction cache.  NB: We flush both caches, just to
	 * be sure...
	 */

	/* note: flushing the icache will clobber EAR0 on the FR451 */
	if (flush_cache)
		gdbstub_purge_cache();

	LEDS(0x5666);

} /* end gdbstub() */

/*****************************************************************************/
/*
 * initialise the GDB stub
 */
void __init gdbstub_init(void)
{
#ifdef CONFIG_GDBSTUB_IMMEDIATE
	unsigned char ch;
	int ret;
#endif

	gdbstub_printk("%s", gdbstub_banner);

	gdbstub_io_init();

	/* try to talk to GDB (or anyone insane enough to want to type GDB protocol by hand) */
	gdbstub_proto("### GDB Tx ACK\n");
	gdbstub_tx_char('+'); /* 'hello world' */

#ifdef CONFIG_GDBSTUB_IMMEDIATE
	gdbstub_printk("GDB Stub waiting for packet\n");

	/*
	 * In case GDB is started before us, ack any packets
	 * (presumably "$?#xx") sitting there.
	 */
	do { gdbstub_rx_char(&ch, 0); } while (ch != '$');
	do { gdbstub_rx_char(&ch, 0); } while (ch != '#');
	do { ret = gdbstub_rx_char(&ch, 0); } while (ret != 0); /* eat first csum byte */
	do { ret = gdbstub_rx_char(&ch, 0); } while (ret != 0); /* eat second csum byte */

	gdbstub_proto("### GDB Tx NAK\n");
	gdbstub_tx_char('-'); /* nak it */

#else
	gdbstub_printk("GDB Stub set\n");
#endif

#if 0
	/* send banner */
	ptr = output_buffer;
	*ptr++ = 'O';
	ptr = mem2hex(gdbstub_banner, ptr, sizeof(gdbstub_banner) - 1, 0);
	gdbstub_send_packet(output_buffer);
#endif
#if defined(CONFIG_GDB_CONSOLE) && defined(CONFIG_GDBSTUB_IMMEDIATE)
	register_console(&gdbstub_console);
#endif

} /* end gdbstub_init() */

/*****************************************************************************/
/*
 * register the console at a more appropriate time
 */
#if defined (CONFIG_GDB_CONSOLE) && !defined(CONFIG_GDBSTUB_IMMEDIATE)
static int __init gdbstub_postinit(void)
{
	printk("registering console\n");
	register_console(&gdbstub_console);
	return 0;
} /* end gdbstub_postinit() */

__initcall(gdbstub_postinit);
#endif

/*****************************************************************************/
/*
 * send an exit message to GDB
 */
void gdbstub_exit(int status)
{
	unsigned char checksum;
	int count;
	unsigned char ch;

	sprintf(output_buffer,"W%02x",status&0xff);

	gdbstub_tx_char('$');
	checksum = 0;
	count = 0;

	while ((ch = output_buffer[count]) != 0) {
		gdbstub_tx_char(ch);
		checksum += ch;
		count += 1;
	}

	gdbstub_tx_char('#');
	gdbstub_tx_char(hex_asc_hi(checksum));
	gdbstub_tx_char(hex_asc_lo(checksum));

	/* make sure the output is flushed, or else RedBoot might clobber it */
	gdbstub_tx_char('-');
	gdbstub_tx_flush();

} /* end gdbstub_exit() */

/*****************************************************************************/
/*
 * GDB wants to call malloc() and free() to allocate memory for calling kernel
 * functions directly from its command line
 */
static void *malloc(size_t size) __maybe_unused;
static void *malloc(size_t size)
{
	return kmalloc(size, GFP_ATOMIC);
}

static void free(void *p) __maybe_unused;
static void free(void *p)
{
	kfree(p);
}

static uint32_t ___get_HSR0(void) __maybe_unused;
static uint32_t ___get_HSR0(void)
{
	return __get_HSR(0);
}

static uint32_t ___set_HSR0(uint32_t x) __maybe_unused;
static uint32_t ___set_HSR0(uint32_t x)
{
	__set_HSR(0, x);
	return __get_HSR(0);
}

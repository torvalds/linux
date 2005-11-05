/*
 * Routines providing a simple monitor for use on the PowerMac.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/kallsyms.h>
#include <asm/ptrace.h>
#include <asm/string.h>
#include <asm/prom.h>
#include <asm/bootx.h>
#include <asm/machdep.h>
#include <asm/xmon.h>
#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif
#include "nonstdio.h"
#include "privinst.h"

#define scanhex	xmon_scanhex
#define skipbl	xmon_skipbl

#ifdef CONFIG_SMP
static unsigned long cpus_in_xmon = 0;
static unsigned long got_xmon = 0;
static volatile int take_xmon = -1;
#endif /* CONFIG_SMP */

static unsigned adrs;
static int size = 1;
static unsigned ndump = 64;
static unsigned nidump = 16;
static unsigned ncsum = 4096;
static int termch;

static u_int bus_error_jmp[100];
#define setjmp xmon_setjmp
#define longjmp xmon_longjmp

/* Breakpoint stuff */
struct bpt {
	unsigned address;
	unsigned instr;
	unsigned count;
	unsigned char enabled;
};

#define NBPTS	16
static struct bpt bpts[NBPTS];
static struct bpt dabr;
static struct bpt iabr;
static unsigned bpinstr = 0x7fe00008;	/* trap */

/* Prototypes */
extern void (*debugger_fault_handler)(struct pt_regs *);
static int cmds(struct pt_regs *);
static int mread(unsigned, void *, int);
static int mwrite(unsigned, void *, int);
static void handle_fault(struct pt_regs *);
static void byterev(unsigned char *, int);
static void memex(void);
static int bsesc(void);
static void dump(void);
static void prdump(unsigned, int);
#ifdef __MWERKS__
static void prndump(unsigned, int);
static int nvreadb(unsigned);
#endif
static int ppc_inst_dump(unsigned, int);
void print_address(unsigned);
static int getsp(void);
static void dump_hash_table(void);
static void backtrace(struct pt_regs *);
static void excprint(struct pt_regs *);
static void prregs(struct pt_regs *);
static void memops(int);
static void memlocate(void);
static void memzcan(void);
static void memdiffs(unsigned char *, unsigned char *, unsigned, unsigned);
int skipbl(void);
int scanhex(unsigned *valp);
static void scannl(void);
static int hexdigit(int);
void getstring(char *, int);
static void flush_input(void);
static int inchar(void);
static void take_input(char *);
/* static void openforth(void); */
static unsigned read_spr(int);
static void write_spr(int, unsigned);
static void super_regs(void);
static void symbol_lookup(void);
static void remove_bpts(void);
static void insert_bpts(void);
static struct bpt *at_breakpoint(unsigned pc);
static void bpt_cmds(void);
static void cacheflush(void);
#ifdef CONFIG_SMP
static void cpu_cmd(void);
#endif /* CONFIG_SMP */
static void csum(void);
#ifdef CONFIG_BOOTX_TEXT
static void vidcmds(void);
#endif
static void bootcmds(void);
static void proccall(void);
static void printtime(void);

extern int print_insn_big_powerpc(FILE *, unsigned long, unsigned);
extern void printf(const char *fmt, ...);
extern int putchar(int ch);
extern int setjmp(u_int *);
extern void longjmp(u_int *, int);

extern void xmon_enter(void);
extern void xmon_leave(void);

static unsigned start_tb[NR_CPUS][2];
static unsigned stop_tb[NR_CPUS][2];

#define GETWORD(v)	(((v)[0] << 24) + ((v)[1] << 16) + ((v)[2] << 8) + (v)[3])

#define isxdigit(c)	(('0' <= (c) && (c) <= '9') \
			 || ('a' <= (c) && (c) <= 'f') \
			 || ('A' <= (c) && (c) <= 'F'))
#define isalnum(c)	(('0' <= (c) && (c) <= '9') \
			 || ('a' <= (c) && (c) <= 'z') \
			 || ('A' <= (c) && (c) <= 'Z'))
#define isspace(c)	(c == ' ' || c == '\t' || c == 10 || c == 13 || c == 0)

static char *help_string = "\
Commands:\n\
  d	dump bytes\n\
  di	dump instructions\n\
  df	dump float values\n\
  dd	dump double values\n\
  e	print exception information\n\
  h	dump hash table\n\
  m	examine/change memory\n\
  mm	move a block of memory\n\
  ms	set a block of memory\n\
  md	compare two blocks of memory\n\
  r	print registers\n\
  S	print special registers\n\
  t	print backtrace\n\
  la	lookup address\n\
  ls	lookup symbol\n\
  C	checksum\n\
  p	call function with arguments\n\
  T	print time\n\
  x	exit monitor\n\
  zr    reboot\n\
  zh    halt\n\
";

static int xmon_trace[NR_CPUS];
#define SSTEP	1		/* stepping because of 's' command */
#define BRSTEP	2		/* stepping over breakpoint */

static struct pt_regs *xmon_regs[NR_CPUS];

extern inline void sync(void)
{
	asm volatile("sync; isync");
}

extern inline void __delay(unsigned int loops)
{
	if (loops != 0)
		__asm__ __volatile__("mtctr %0; 1: bdnz 1b" : :
				     "r" (loops) : "ctr");
}

/* Print an address in numeric and symbolic form (if possible) */
static void xmon_print_symbol(unsigned long address, const char *mid,
			      const char *after)
{
	char *modname;
	const char *name = NULL;
	unsigned long offset, size;
	static char tmpstr[128];

	printf("%.8lx", address);
	if (setjmp(bus_error_jmp) == 0) {
		debugger_fault_handler = handle_fault;
		sync();
		name = kallsyms_lookup(address, &size, &offset, &modname,
				       tmpstr);
		sync();
		/* wait a little while to see if we get a machine check */
		__delay(200);
	}
	debugger_fault_handler = NULL;

	if (name) {
		printf("%s%s+%#lx/%#lx", mid, name, offset, size);
		if (modname)
			printf(" [%s]", modname);
	}
	printf("%s", after);
}

static void get_tb(unsigned *p)
{
	unsigned hi, lo, hiagain;

	if ((get_pvr() >> 16) == 1)
		return;

	do {
		asm volatile("mftbu %0; mftb %1; mftbu %2"
			     : "=r" (hi), "=r" (lo), "=r" (hiagain));
	} while (hi != hiagain);
	p[0] = hi;
	p[1] = lo;
}

void
xmon(struct pt_regs *excp)
{
	struct pt_regs regs;
	int msr, cmd;

	get_tb(stop_tb[smp_processor_id()]);
	if (excp == NULL) {
		asm volatile ("stw	0,0(%0)\n\
			lwz	0,0(1)\n\
			stw	0,4(%0)\n\
			stmw	2,8(%0)" : : "b" (&regs));
		regs.nip = regs.link = ((unsigned long *)regs.gpr[1])[1];
		regs.msr = get_msr();
		regs.ctr = get_ctr();
		regs.xer = get_xer();
		regs.ccr = get_cr();
		regs.trap = 0;
		excp = &regs;
	}

	msr = get_msr();
	set_msr(msr & ~0x8000);	/* disable interrupts */
	xmon_regs[smp_processor_id()] = excp;
	xmon_enter();
	excprint(excp);
#ifdef CONFIG_SMP
	if (test_and_set_bit(smp_processor_id(), &cpus_in_xmon))
		for (;;)
			;
	while (test_and_set_bit(0, &got_xmon)) {
		if (take_xmon == smp_processor_id()) {
			take_xmon = -1;
			break;
		}
	}
	/*
	 * XXX: breakpoints are removed while any cpu is in xmon
	 */
#endif /* CONFIG_SMP */
	remove_bpts();
#ifdef CONFIG_PMAC_BACKLIGHT
	if( setjmp(bus_error_jmp) == 0 ) {
		debugger_fault_handler = handle_fault;
		sync();
		set_backlight_enable(1);
		set_backlight_level(BACKLIGHT_MAX);
		sync();
	}
	debugger_fault_handler = NULL;
#endif	/* CONFIG_PMAC_BACKLIGHT */
	cmd = cmds(excp);
	if (cmd == 's') {
		xmon_trace[smp_processor_id()] = SSTEP;
		excp->msr |= 0x400;
	} else if (at_breakpoint(excp->nip)) {
		xmon_trace[smp_processor_id()] = BRSTEP;
		excp->msr |= 0x400;
	} else {
		xmon_trace[smp_processor_id()] = 0;
		insert_bpts();
	}
	xmon_leave();
	xmon_regs[smp_processor_id()] = NULL;
#ifdef CONFIG_SMP
	clear_bit(0, &got_xmon);
	clear_bit(smp_processor_id(), &cpus_in_xmon);
#endif /* CONFIG_SMP */
	set_msr(msr);		/* restore interrupt enable */
	get_tb(start_tb[smp_processor_id()]);
}

irqreturn_t
xmon_irq(int irq, void *d, struct pt_regs *regs)
{
	unsigned long flags;
	local_irq_save(flags);
	printf("Keyboard interrupt\n");
	xmon(regs);
	local_irq_restore(flags);
	return IRQ_HANDLED;
}

int
xmon_bpt(struct pt_regs *regs)
{
	struct bpt *bp;

	bp = at_breakpoint(regs->nip);
	if (!bp)
		return 0;
	if (bp->count) {
		--bp->count;
		remove_bpts();
		excprint(regs);
		xmon_trace[smp_processor_id()] = BRSTEP;
		regs->msr |= 0x400;
	} else {
		xmon(regs);
	}
	return 1;
}

int
xmon_sstep(struct pt_regs *regs)
{
	if (!xmon_trace[smp_processor_id()])
		return 0;
	if (xmon_trace[smp_processor_id()] == BRSTEP) {
		xmon_trace[smp_processor_id()] = 0;
		insert_bpts();
	} else {
		xmon(regs);
	}
	return 1;
}

int
xmon_dabr_match(struct pt_regs *regs)
{
	if (dabr.enabled && dabr.count) {
		--dabr.count;
		remove_bpts();
		excprint(regs);
		xmon_trace[smp_processor_id()] = BRSTEP;
		regs->msr |= 0x400;
	} else {
		dabr.instr = regs->nip;
		xmon(regs);
	}
	return 1;
}

int
xmon_iabr_match(struct pt_regs *regs)
{
	if (iabr.enabled && iabr.count) {
		--iabr.count;
		remove_bpts();
		excprint(regs);
		xmon_trace[smp_processor_id()] = BRSTEP;
		regs->msr |= 0x400;
	} else {
		xmon(regs);
	}
	return 1;
}

static struct bpt *
at_breakpoint(unsigned pc)
{
	int i;
	struct bpt *bp;

	if (dabr.enabled && pc == dabr.instr)
		return &dabr;
	if (iabr.enabled && pc == iabr.address)
		return &iabr;
	bp = bpts;
	for (i = 0; i < NBPTS; ++i, ++bp)
		if (bp->enabled && pc == bp->address)
			return bp;
	return NULL;
}

static void
insert_bpts(void)
{
	int i;
	struct bpt *bp;

	bp = bpts;
	for (i = 0; i < NBPTS; ++i, ++bp) {
		if (!bp->enabled)
			continue;
		if (mread(bp->address, &bp->instr, 4) != 4
		    || mwrite(bp->address, &bpinstr, 4) != 4) {
			printf("Couldn't insert breakpoint at %x, disabling\n",
			       bp->address);
			bp->enabled = 0;
		}
		store_inst((void *) bp->address);
	}
#if !defined(CONFIG_8xx)
	if (dabr.enabled)
		set_dabr(dabr.address);
	if (iabr.enabled)
		set_iabr(iabr.address);
#endif
}

static void
remove_bpts(void)
{
	int i;
	struct bpt *bp;
	unsigned instr;

#if !defined(CONFIG_8xx)
	set_dabr(0);
	set_iabr(0);
#endif
	bp = bpts;
	for (i = 0; i < NBPTS; ++i, ++bp) {
		if (!bp->enabled)
			continue;
		if (mread(bp->address, &instr, 4) == 4
		    && instr == bpinstr
		    && mwrite(bp->address, &bp->instr, 4) != 4)
			printf("Couldn't remove breakpoint at %x\n",
			       bp->address);
		store_inst((void *) bp->address);
	}
}

static char *last_cmd;

/* Command interpreting routine */
static int
cmds(struct pt_regs *excp)
{
	int cmd;

	last_cmd = NULL;
	for(;;) {
#ifdef CONFIG_SMP
		printf("%d:", smp_processor_id());
#endif /* CONFIG_SMP */
		printf("mon> ");
		fflush(stdout);
		flush_input();
		termch = 0;
		cmd = skipbl();
		if( cmd == '\n' ) {
			if (last_cmd == NULL)
				continue;
			take_input(last_cmd);
			last_cmd = NULL;
			cmd = inchar();
		}
		switch (cmd) {
		case 'm':
			cmd = inchar();
			switch (cmd) {
			case 'm':
			case 's':
			case 'd':
				memops(cmd);
				break;
			case 'l':
				memlocate();
				break;
			case 'z':
				memzcan();
				break;
			default:
				termch = cmd;
				memex();
			}
			break;
		case 'd':
			dump();
			break;
		case 'l':
			symbol_lookup();
			break;
		case 'r':
			if (excp != NULL)
				prregs(excp);	/* print regs */
			break;
		case 'e':
			if (excp == NULL)
				printf("No exception information\n");
			else
				excprint(excp);
			break;
		case 'S':
			super_regs();
			break;
		case 't':
			backtrace(excp);
			break;
		case 'f':
			cacheflush();
			break;
		case 'h':
			dump_hash_table();
			break;
		case 's':
		case 'x':
		case EOF:
			return cmd;
		case '?':
			printf(help_string);
			break;
		default:
			printf("Unrecognized command: ");
			if( ' ' < cmd && cmd <= '~' )
				putchar(cmd);
			else
				printf("\\x%x", cmd);
			printf(" (type ? for help)\n");
			break;
		case 'b':
			bpt_cmds();
			break;
		case 'C':
			csum();
			break;
#ifdef CONFIG_SMP
		case 'c':
			cpu_cmd();
			break;
#endif /* CONFIG_SMP */
#ifdef CONFIG_BOOTX_TEXT
		case 'v':
			vidcmds();
			break;
#endif
		case 'z':
			bootcmds();
			break;
		case 'p':
			proccall();
			break;
		case 'T':
			printtime();
			break;
		}
	}
}

extern unsigned tb_to_us;

#define mulhwu(x,y) \
({unsigned z; asm ("mulhwu %0,%1,%2" : "=r" (z) : "r" (x), "r" (y)); z;})

static void printtime(void)
{
	unsigned int delta;

	delta = stop_tb[smp_processor_id()][1]
		- start_tb[smp_processor_id()][1];
	delta = mulhwu(tb_to_us, delta);
	printf("%u.%06u seconds\n", delta / 1000000, delta % 1000000);
}

static void bootcmds(void)
{
	int cmd;

	cmd = inchar();
	if (cmd == 'r')
		ppc_md.restart(NULL);
	else if (cmd == 'h')
		ppc_md.halt();
	else if (cmd == 'p')
		ppc_md.power_off();
}

#ifdef CONFIG_SMP
static void cpu_cmd(void)
{
	unsigned cpu;
	int timeout;
	int cmd;

	cmd = inchar();
	if (cmd == 'i') {
		/* interrupt other cpu(s) */
		cpu = MSG_ALL_BUT_SELF;
		if (scanhex(&cpu))
			smp_send_xmon_break(cpu);
		return;
	}
	termch = cmd;
	if (!scanhex(&cpu)) {
		/* print cpus waiting or in xmon */
		printf("cpus stopped:");
		for (cpu = 0; cpu < NR_CPUS; ++cpu) {
			if (test_bit(cpu, &cpus_in_xmon)) {
				printf(" %d", cpu);
				if (cpu == smp_processor_id())
					printf("*", cpu);
			}
		}
		printf("\n");
		return;
	}
	/* try to switch to cpu specified */
	take_xmon = cpu;
	timeout = 10000000;
	while (take_xmon >= 0) {
		if (--timeout == 0) {
			/* yes there's a race here */
			take_xmon = -1;
			printf("cpu %u didn't take control\n", cpu);
			return;
		}
	}
	/* now have to wait to be given control back */
	while (test_and_set_bit(0, &got_xmon)) {
		if (take_xmon == smp_processor_id()) {
			take_xmon = -1;
			break;
		}
	}
}
#endif /* CONFIG_SMP */

#ifdef CONFIG_BOOTX_TEXT
extern boot_infos_t disp_bi;

static void vidcmds(void)
{
	int c = inchar();
	unsigned int val, w;
	extern int boot_text_mapped;

	if (!boot_text_mapped)
		return;
	if (c != '\n' && scanhex(&val)) {
		switch (c) {
		case 'd':
			w = disp_bi.dispDeviceRowBytes
				/ (disp_bi.dispDeviceDepth >> 3);
			disp_bi.dispDeviceDepth = val;
			disp_bi.dispDeviceRowBytes = w * (val >> 3);
			return;
		case 'p':
			disp_bi.dispDeviceRowBytes = val;
			return;
		case 'w':
			disp_bi.dispDeviceRect[2] = val;
			return;
		case 'h':
			disp_bi.dispDeviceRect[3] = val;
			return;
		}
	}
	printf("W = %d (0x%x) H = %d (0x%x) D = %d (0x%x) P = %d (0x%x)\n",
	       disp_bi.dispDeviceRect[2], disp_bi.dispDeviceRect[2],
	       disp_bi.dispDeviceRect[3], disp_bi.dispDeviceRect[3],
	       disp_bi.dispDeviceDepth, disp_bi.dispDeviceDepth,
	       disp_bi.dispDeviceRowBytes, disp_bi.dispDeviceRowBytes);
}
#endif /* CONFIG_BOOTX_TEXT */

static unsigned short fcstab[256] = {
	0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
	0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
	0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
	0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
	0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
	0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
	0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
	0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
	0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
	0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
	0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
	0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
	0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
	0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
	0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
	0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
	0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
	0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
	0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
	0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
	0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
	0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
	0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
	0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
	0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
	0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
	0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
	0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
	0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
	0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
	0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
	0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

#define FCS(fcs, c)	(((fcs) >> 8) ^ fcstab[((fcs) ^ (c)) & 0xff])

static void
csum(void)
{
	unsigned int i;
	unsigned short fcs;
	unsigned char v;

	if (!scanhex(&adrs))
		return;
	if (!scanhex(&ncsum))
		return;
	fcs = 0xffff;
	for (i = 0; i < ncsum; ++i) {
		if (mread(adrs+i, &v, 1) == 0) {
			printf("csum stopped at %x\n", adrs+i);
			break;
		}
		fcs = FCS(fcs, v);
	}
	printf("%x\n", fcs);
}

static void
bpt_cmds(void)
{
	int cmd;
	unsigned a;
	int mode, i;
	struct bpt *bp;

	cmd = inchar();
	switch (cmd) {
#if !defined(CONFIG_8xx)
	case 'd':
		mode = 7;
		cmd = inchar();
		if (cmd == 'r')
			mode = 5;
		else if (cmd == 'w')
			mode = 6;
		else
			termch = cmd;
		cmd = inchar();
		if (cmd == 'p')
			mode &= ~4;
		else
			termch = cmd;
		dabr.address = 0;
		dabr.count = 0;
		dabr.enabled = scanhex(&dabr.address);
		scanhex(&dabr.count);
		if (dabr.enabled)
			dabr.address = (dabr.address & ~7) | mode;
		break;
	case 'i':
		cmd = inchar();
		if (cmd == 'p')
			mode = 2;
		else
			mode = 3;
		iabr.address = 0;
		iabr.count = 0;
		iabr.enabled = scanhex(&iabr.address);
		if (iabr.enabled)
			iabr.address |= mode;
		scanhex(&iabr.count);
		break;
#endif
	case 'c':
		if (!scanhex(&a)) {
			/* clear all breakpoints */
			for (i = 0; i < NBPTS; ++i)
				bpts[i].enabled = 0;
			iabr.enabled = 0;
			dabr.enabled = 0;
			printf("All breakpoints cleared\n");
		} else {
			bp = at_breakpoint(a);
			if (bp == 0) {
				printf("No breakpoint at %x\n", a);
			} else {
				bp->enabled = 0;
			}
		}
		break;
	default:
		termch = cmd;
		if (!scanhex(&a)) {
			/* print all breakpoints */
			printf("type  address   count\n");
			if (dabr.enabled) {
				printf("data %.8x %8x [", dabr.address & ~7,
				       dabr.count);
				if (dabr.address & 1)
					printf("r");
				if (dabr.address & 2)
					printf("w");
				if (!(dabr.address & 4))
					printf("p");
				printf("]\n");
			}
			if (iabr.enabled)
				printf("inst %.8x %8x\n", iabr.address & ~3,
				       iabr.count);
			for (bp = bpts; bp < &bpts[NBPTS]; ++bp)
				if (bp->enabled)
					printf("trap %.8x %8x\n", bp->address,
					       bp->count);
			break;
		}
		bp = at_breakpoint(a);
		if (bp == 0) {
			for (bp = bpts; bp < &bpts[NBPTS]; ++bp)
				if (!bp->enabled)
					break;
			if (bp >= &bpts[NBPTS]) {
				printf("Sorry, no free breakpoints\n");
				break;
			}
		}
		bp->enabled = 1;
		bp->address = a;
		bp->count = 0;
		scanhex(&bp->count);
		break;
	}
}

static void
backtrace(struct pt_regs *excp)
{
	unsigned sp;
	unsigned stack[2];
	struct pt_regs regs;
	extern char ret_from_except, ret_from_except_full, ret_from_syscall;

	printf("backtrace:\n");
	
	if (excp != NULL)
		sp = excp->gpr[1];
	else
		sp = getsp();
	scanhex(&sp);
	scannl();
	for (; sp != 0; sp = stack[0]) {
		if (mread(sp, stack, sizeof(stack)) != sizeof(stack))
			break;
		printf("[%.8lx] ", stack);
		xmon_print_symbol(stack[1], " ", "\n");
		if (stack[1] == (unsigned) &ret_from_except
		    || stack[1] == (unsigned) &ret_from_except_full
		    || stack[1] == (unsigned) &ret_from_syscall) {
			if (mread(sp+16, &regs, sizeof(regs)) != sizeof(regs))
				break;
			printf("exception:%x [%x] %x\n", regs.trap, sp+16,
			       regs.nip);
			sp = regs.gpr[1];
			if (mread(sp, stack, sizeof(stack)) != sizeof(stack))
				break;
		}
	}
}

int
getsp(void)
{
    int x;

    asm("mr %0,1" : "=r" (x) :);
    return x;
}

void
excprint(struct pt_regs *fp)
{
	int trap;

#ifdef CONFIG_SMP
	printf("cpu %d: ", smp_processor_id());
#endif /* CONFIG_SMP */
	printf("vector: %x at pc=", fp->trap);
	xmon_print_symbol(fp->nip, ": ", ", lr=");
	xmon_print_symbol(fp->link, ": ", "\n");
	printf("msr = %x, sp = %x [%x]\n", fp->msr, fp->gpr[1], fp);
	trap = TRAP(fp);
	if (trap == 0x300 || trap == 0x600)
		printf("dar = %x, dsisr = %x\n", fp->dar, fp->dsisr);
	if (current)
		printf("current = %x, pid = %d, comm = %s\n",
		       current, current->pid, current->comm);
}

void
prregs(struct pt_regs *fp)
{
	int n;
	unsigned base;

	if (scanhex(&base))
		fp = (struct pt_regs *) base;
	for (n = 0; n < 32; ++n) {
		printf("R%.2d = %.8x%s", n, fp->gpr[n],
		       (n & 3) == 3? "\n": "   ");
		if (n == 12 && !FULL_REGS(fp)) {
			printf("\n");
			break;
		}
	}
	printf("pc  = %.8x   msr = %.8x   lr  = %.8x   cr  = %.8x\n",
	       fp->nip, fp->msr, fp->link, fp->ccr);
	printf("ctr = %.8x   xer = %.8x   trap = %4x\n",
	       fp->ctr, fp->xer, fp->trap);
}

void
cacheflush(void)
{
	int cmd;
	unsigned nflush;

	cmd = inchar();
	if (cmd != 'i')
		termch = cmd;
	scanhex(&adrs);
	if (termch != '\n')
		termch = 0;
	nflush = 1;
	scanhex(&nflush);
	nflush = (nflush + 31) / 32;
	if (cmd != 'i') {
		for (; nflush > 0; --nflush, adrs += 0x20)
			cflush((void *) adrs);
	} else {
		for (; nflush > 0; --nflush, adrs += 0x20)
			cinval((void *) adrs);
	}
}

unsigned int
read_spr(int n)
{
    unsigned int instrs[2];
    int (*code)(void);

    instrs[0] = 0x7c6002a6 + ((n & 0x1F) << 16) + ((n & 0x3e0) << 6);
    instrs[1] = 0x4e800020;
    store_inst(instrs);
    store_inst(instrs+1);
    code = (int (*)(void)) instrs;
    return code();
}

void
write_spr(int n, unsigned int val)
{
    unsigned int instrs[2];
    int (*code)(unsigned int);

    instrs[0] = 0x7c6003a6 + ((n & 0x1F) << 16) + ((n & 0x3e0) << 6);
    instrs[1] = 0x4e800020;
    store_inst(instrs);
    store_inst(instrs+1);
    code = (int (*)(unsigned int)) instrs;
    code(val);
}

static unsigned int regno;
extern char exc_prolog;
extern char dec_exc;

void
super_regs(void)
{
	int i, cmd;
	unsigned val;

	cmd = skipbl();
	if (cmd == '\n') {
		printf("msr = %x, pvr = %x\n", get_msr(), get_pvr());
		printf("sprg0-3 = %x %x %x %x\n", get_sprg0(), get_sprg1(),
		       get_sprg2(), get_sprg3());
		printf("srr0 = %x, srr1 = %x\n", get_srr0(), get_srr1());
#ifdef CONFIG_PPC_STD_MMU
		printf("sr0-15 =");
		for (i = 0; i < 16; ++i)
			printf(" %x", get_sr(i));
		printf("\n");
#endif
		asm("mr %0,1" : "=r" (i) :);
		printf("sp = %x ", i);
		asm("mr %0,2" : "=r" (i) :);
		printf("toc = %x\n", i);
		return;
	}

	scanhex(&regno);
	switch (cmd) {
	case 'w':
		val = read_spr(regno);
		scanhex(&val);
		write_spr(regno, val);
		/* fall through */
	case 'r':
		printf("spr %x = %x\n", regno, read_spr(regno));
		break;
	case 's':
		val = get_sr(regno);
		scanhex(&val);
		set_sr(regno, val);
		break;
	case 'm':
		val = get_msr();
		scanhex(&val);
		set_msr(val);
		break;
	}
	scannl();
}

#ifndef CONFIG_PPC_STD_MMU
static void
dump_hash_table(void)
{
	printf("This CPU doesn't have a hash table.\n");
}
#else

#ifndef CONFIG_PPC64BRIDGE
static void
dump_hash_table_seg(unsigned seg, unsigned start, unsigned end)
{
	extern void *Hash;
	extern unsigned long Hash_size;
	unsigned *htab = Hash;
	unsigned hsize = Hash_size;
	unsigned v, hmask, va, last_va = 0;
	int found, last_found, i;
	unsigned *hg, w1, last_w2 = 0, last_va0 = 0;

	last_found = 0;
	hmask = hsize / 64 - 1;
	va = start;
	start = (start >> 12) & 0xffff;
	end = (end >> 12) & 0xffff;
	for (v = start; v < end; ++v) {
		found = 0;
		hg = htab + (((v ^ seg) & hmask) * 16);
		w1 = 0x80000000 | (seg << 7) | (v >> 10);
		for (i = 0; i < 8; ++i, hg += 2) {
			if (*hg == w1) {
				found = 1;
				break;
			}
		}
		if (!found) {
			w1 ^= 0x40;
			hg = htab + ((~(v ^ seg) & hmask) * 16);
			for (i = 0; i < 8; ++i, hg += 2) {
				if (*hg == w1) {
					found = 1;
					break;
				}
			}
		}
		if (!(last_found && found && (hg[1] & ~0x180) == last_w2 + 4096)) {
			if (last_found) {
				if (last_va != last_va0)
					printf(" ... %x", last_va);
				printf("\n");
			}
			if (found) {
				printf("%x to %x", va, hg[1]);
				last_va0 = va;
			}
			last_found = found;
		}
		if (found) {
			last_w2 = hg[1] & ~0x180;
			last_va = va;
		}
		va += 4096;
	}
	if (last_found)
		printf(" ... %x\n", last_va);
}

#else /* CONFIG_PPC64BRIDGE */
static void
dump_hash_table_seg(unsigned seg, unsigned start, unsigned end)
{
	extern void *Hash;
	extern unsigned long Hash_size;
	unsigned *htab = Hash;
	unsigned hsize = Hash_size;
	unsigned v, hmask, va, last_va;
	int found, last_found, i;
	unsigned *hg, w1, last_w2, last_va0;

	last_found = 0;
	hmask = hsize / 128 - 1;
	va = start;
	start = (start >> 12) & 0xffff;
	end = (end >> 12) & 0xffff;
	for (v = start; v < end; ++v) {
		found = 0;
		hg = htab + (((v ^ seg) & hmask) * 32);
		w1 = 1 | (seg << 12) | ((v & 0xf800) >> 4);
		for (i = 0; i < 8; ++i, hg += 4) {
			if (hg[1] == w1) {
				found = 1;
				break;
			}
		}
		if (!found) {
			w1 ^= 2;
			hg = htab + ((~(v ^ seg) & hmask) * 32);
			for (i = 0; i < 8; ++i, hg += 4) {
				if (hg[1] == w1) {
					found = 1;
					break;
				}
			}
		}
		if (!(last_found && found && (hg[3] & ~0x180) == last_w2 + 4096)) {
			if (last_found) {
				if (last_va != last_va0)
					printf(" ... %x", last_va);
				printf("\n");
			}
			if (found) {
				printf("%x to %x", va, hg[3]);
				last_va0 = va;
			}
			last_found = found;
		}
		if (found) {
			last_w2 = hg[3] & ~0x180;
			last_va = va;
		}
		va += 4096;
	}
	if (last_found)
		printf(" ... %x\n", last_va);
}
#endif /* CONFIG_PPC64BRIDGE */

static unsigned hash_ctx;
static unsigned hash_start;
static unsigned hash_end;

static void
dump_hash_table(void)
{
	int seg;
	unsigned seg_start, seg_end;

	hash_ctx = 0;
	hash_start = 0;
	hash_end = 0xfffff000;
	scanhex(&hash_ctx);
	scanhex(&hash_start);
	scanhex(&hash_end);
	printf("Mappings for context %x\n", hash_ctx);
	seg_start = hash_start;
	for (seg = hash_start >> 28; seg <= hash_end >> 28; ++seg) {
		seg_end = (seg << 28) | 0x0ffff000;
		if (seg_end > hash_end)
			seg_end = hash_end;
		dump_hash_table_seg((hash_ctx << 4) + (seg * 0x111),
				    seg_start, seg_end);
		seg_start = seg_end + 0x1000;
	}
}
#endif /* CONFIG_PPC_STD_MMU */

/*
 * Stuff for reading and writing memory safely
 */

int
mread(unsigned adrs, void *buf, int size)
{
	volatile int n;
	char *p, *q;

	n = 0;
	if( setjmp(bus_error_jmp) == 0 ){
		debugger_fault_handler = handle_fault;
		sync();
		p = (char *) adrs;
		q = (char *) buf;
		switch (size) {
		case 2: *(short *)q = *(short *)p;	break;
		case 4: *(int *)q = *(int *)p;		break;
		default:
			for( ; n < size; ++n ) {
				*q++ = *p++;
				sync();
			}
		}
		sync();
		/* wait a little while to see if we get a machine check */
		__delay(200);
		n = size;
	}
	debugger_fault_handler = NULL;
	return n;
}

int
mwrite(unsigned adrs, void *buf, int size)
{
	volatile int n;
	char *p, *q;

	n = 0;
	if( setjmp(bus_error_jmp) == 0 ){
		debugger_fault_handler = handle_fault;
		sync();
		p = (char *) adrs;
		q = (char *) buf;
		switch (size) {
		case 2: *(short *)p = *(short *)q;	break;
		case 4: *(int *)p = *(int *)q;		break;
		default:
			for( ; n < size; ++n ) {
				*p++ = *q++;
				sync();
			}
		}
		sync();
		n = size;
	} else {
		printf("*** Error writing address %x\n", adrs + n);
	}
	debugger_fault_handler = NULL;
	return n;
}

static int fault_type;
static int fault_except;
static char *fault_chars[] = { "--", "**", "##" };

static void
handle_fault(struct pt_regs *regs)
{
	fault_except = TRAP(regs);
	fault_type = TRAP(regs) == 0x200? 0: TRAP(regs) == 0x300? 1: 2;
	longjmp(bus_error_jmp, 1);
}

#define SWAP(a, b, t)	((t) = (a), (a) = (b), (b) = (t))

void
byterev(unsigned char *val, int size)
{
	int t;
	
	switch (size) {
	case 2:
		SWAP(val[0], val[1], t);
		break;
	case 4:
		SWAP(val[0], val[3], t);
		SWAP(val[1], val[2], t);
		break;
	}
}

static int brev;
static int mnoread;

void
memex(void)
{
    int cmd, inc, i, nslash;
    unsigned n;
    unsigned char val[4];

    last_cmd = "m\n";
    scanhex(&adrs);
    while ((cmd = skipbl()) != '\n') {
	switch( cmd ){
	case 'b':	size = 1;	break;
	case 'w':	size = 2;	break;
	case 'l':	size = 4;	break;
	case 'r': 	brev = !brev;	break;
	case 'n':	mnoread = 1;	break;
	case '.':	mnoread = 0;	break;
	}
    }
    if( size <= 0 )
	size = 1;
    else if( size > 4 )
	size = 4;
    for(;;){
	if (!mnoread)
	    n = mread(adrs, val, size);
	printf("%.8x%c", adrs, brev? 'r': ' ');
	if (!mnoread) {
	    if (brev)
		byterev(val, size);
	    putchar(' ');
	    for (i = 0; i < n; ++i)
		printf("%.2x", val[i]);
	    for (; i < size; ++i)
		printf("%s", fault_chars[fault_type]);
	}
	putchar(' ');
	inc = size;
	nslash = 0;
	for(;;){
	    if( scanhex(&n) ){
		for (i = 0; i < size; ++i)
		    val[i] = n >> (i * 8);
		if (!brev)
		    byterev(val, size);
		mwrite(adrs, val, size);
		inc = size;
	    }
	    cmd = skipbl();
	    if (cmd == '\n')
		break;
	    inc = 0;
	    switch (cmd) {
	    case '\'':
		for(;;){
		    n = inchar();
		    if( n == '\\' )
			n = bsesc();
		    else if( n == '\'' )
			break;
		    for (i = 0; i < size; ++i)
			val[i] = n >> (i * 8);
		    if (!brev)
			byterev(val, size);
		    mwrite(adrs, val, size);
		    adrs += size;
		}
		adrs -= size;
		inc = size;
		break;
	    case ',':
		adrs += size;
		break;
	    case '.':
		mnoread = 0;
		break;
	    case ';':
		break;
	    case 'x':
	    case EOF:
		scannl();
		return;
	    case 'b':
	    case 'v':
		size = 1;
		break;
	    case 'w':
		size = 2;
		break;
	    case 'l':
		size = 4;
		break;
	    case '^':
		adrs -= size;
		break;
		break;
	    case '/':
		if (nslash > 0)
		    adrs -= 1 << nslash;
		else
		    nslash = 0;
		nslash += 4;
		adrs += 1 << nslash;
		break;
	    case '\\':
		if (nslash < 0)
		    adrs += 1 << -nslash;
		else
		    nslash = 0;
		nslash -= 4;
		adrs -= 1 << -nslash;
		break;
	    case 'm':
		scanhex(&adrs);
		break;
	    case 'n':
		mnoread = 1;
		break;
	    case 'r':
		brev = !brev;
		break;
	    case '<':
		n = size;
		scanhex(&n);
		adrs -= n;
		break;
	    case '>':
		n = size;
		scanhex(&n);
		adrs += n;
		break;
	    }
	}
	adrs += inc;
    }
}

int
bsesc(void)
{
	int c;

	c = inchar();
	switch( c ){
	case 'n':	c = '\n';	break;
	case 'r':	c = '\r';	break;
	case 'b':	c = '\b';	break;
	case 't':	c = '\t';	break;
	}
	return c;
}

void
dump(void)
{
	int c;

	c = inchar();
	if ((isxdigit(c) && c != 'f' && c != 'd') || c == '\n')
		termch = c;
	scanhex(&adrs);
	if( termch != '\n')
		termch = 0;
	if( c == 'i' ){
		scanhex(&nidump);
		if( nidump == 0 )
			nidump = 16;
		adrs += ppc_inst_dump(adrs, nidump);
		last_cmd = "di\n";
	} else {
		scanhex(&ndump);
		if( ndump == 0 )
			ndump = 64;
		prdump(adrs, ndump);
		adrs += ndump;
		last_cmd = "d\n";
	}
}

void
prdump(unsigned adrs, int ndump)
{
	register int n, m, c, r, nr;
	unsigned char temp[16];

	for( n = ndump; n > 0; ){
		printf("%.8x", adrs);
		putchar(' ');
		r = n < 16? n: 16;
		nr = mread(adrs, temp, r);
		adrs += nr;
		for( m = 0; m < r; ++m ){
			putchar((m & 3) == 0 && m > 0? '.': ' ');
			if( m < nr )
				printf("%.2x", temp[m]);
			else
				printf("%s", fault_chars[fault_type]);
		}
		for(; m < 16; ++m )
			printf("   ");
		printf("  |");
		for( m = 0; m < r; ++m ){
			if( m < nr ){
				c = temp[m];
				putchar(' ' <= c && c <= '~'? c: '.');
			} else
				putchar(' ');
		}
		n -= r;
		for(; m < 16; ++m )
			putchar(' ');
		printf("|\n");
		if( nr < r )
			break;
	}
}

int
ppc_inst_dump(unsigned adr, int count)
{
	int nr, dotted;
	unsigned first_adr;
	unsigned long inst, last_inst = 0;
	unsigned char val[4];

	dotted = 0;
	for (first_adr = adr; count > 0; --count, adr += 4){
		nr = mread(adr, val, 4);
		if( nr == 0 ){
			const char *x = fault_chars[fault_type];
			printf("%.8x  %s%s%s%s\n", adr, x, x, x, x);
			break;
		}
		inst = GETWORD(val);
		if (adr > first_adr && inst == last_inst) {
			if (!dotted) {
				printf(" ...\n");
				dotted = 1;
			}
			continue;
		}
		dotted = 0;
		last_inst = inst;
		printf("%.8x  ", adr);
		printf("%.8x\t", inst);
		print_insn_big_powerpc(stdout, inst, adr);	/* always returns 4 */
		printf("\n");
	}
	return adr - first_adr;
}

void
print_address(unsigned addr)
{
	printf("0x%x", addr);
}

/*
 * Memory operations - move, set, print differences
 */
static unsigned mdest;		/* destination address */
static unsigned msrc;		/* source address */
static unsigned mval;		/* byte value to set memory to */
static unsigned mcount;		/* # bytes to affect */
static unsigned mdiffs;		/* max # differences to print */

void
memops(int cmd)
{
	scanhex(&mdest);
	if( termch != '\n' )
		termch = 0;
	scanhex(cmd == 's'? &mval: &msrc);
	if( termch != '\n' )
		termch = 0;
	scanhex(&mcount);
	switch( cmd ){
	case 'm':
		memmove((void *)mdest, (void *)msrc, mcount);
		break;
	case 's':
		memset((void *)mdest, mval, mcount);
		break;
	case 'd':
		if( termch != '\n' )
			termch = 0;
		scanhex(&mdiffs);
		memdiffs((unsigned char *)mdest, (unsigned char *)msrc, mcount, mdiffs);
		break;
	}
}

void
memdiffs(unsigned char *p1, unsigned char *p2, unsigned nb, unsigned maxpr)
{
	unsigned n, prt;

	prt = 0;
	for( n = nb; n > 0; --n )
		if( *p1++ != *p2++ )
			if( ++prt <= maxpr )
				printf("%.8x %.2x # %.8x %.2x\n", (unsigned)p1 - 1,
					p1[-1], (unsigned)p2 - 1, p2[-1]);
	if( prt > maxpr )
		printf("Total of %d differences\n", prt);
}

static unsigned mend;
static unsigned mask;

void
memlocate(void)
{
	unsigned a, n;
	unsigned char val[4];

	last_cmd = "ml";
	scanhex(&mdest);
	if (termch != '\n') {
		termch = 0;
		scanhex(&mend);
		if (termch != '\n') {
			termch = 0;
			scanhex(&mval);
			mask = ~0;
			if (termch != '\n') termch = 0;
			scanhex(&mask);
		}
	}
	n = 0;
	for (a = mdest; a < mend; a += 4) {
		if (mread(a, val, 4) == 4
			&& ((GETWORD(val) ^ mval) & mask) == 0) {
			printf("%.8x:  %.8x\n", a, GETWORD(val));
			if (++n >= 10)
				break;
		}
	}
}

static unsigned mskip = 0x1000;
static unsigned mlim = 0xffffffff;

void
memzcan(void)
{
	unsigned char v;
	unsigned a;
	int ok, ook;

	scanhex(&mdest);
	if (termch != '\n') termch = 0;
	scanhex(&mskip);
	if (termch != '\n') termch = 0;
	scanhex(&mlim);
	ook = 0;
	for (a = mdest; a < mlim; a += mskip) {
		ok = mread(a, &v, 1);
		if (ok && !ook) {
			printf("%.8x .. ", a);
			fflush(stdout);
		} else if (!ok && ook)
			printf("%.8x\n", a - mskip);
		ook = ok;
		if (a + mskip < a)
			break;
	}
	if (ook)
		printf("%.8x\n", a - mskip);
}

void proccall(void)
{
	unsigned int args[8];
	unsigned int ret;
	int i;
	typedef unsigned int (*callfunc_t)(unsigned int, unsigned int,
			unsigned int, unsigned int, unsigned int,
			unsigned int, unsigned int, unsigned int);
	callfunc_t func;

	scanhex(&adrs);
	if (termch != '\n')
		termch = 0;
	for (i = 0; i < 8; ++i)
		args[i] = 0;
	for (i = 0; i < 8; ++i) {
		if (!scanhex(&args[i]) || termch == '\n')
			break;
		termch = 0;
	}
	func = (callfunc_t) adrs;
	ret = 0;
	if (setjmp(bus_error_jmp) == 0) {
		debugger_fault_handler = handle_fault;
		sync();
		ret = func(args[0], args[1], args[2], args[3],
			   args[4], args[5], args[6], args[7]);
		sync();
		printf("return value is %x\n", ret);
	} else {
		printf("*** %x exception occurred\n", fault_except);
	}
	debugger_fault_handler = NULL;
}

/* Input scanning routines */
int
skipbl(void)
{
	int c;

	if( termch != 0 ){
		c = termch;
		termch = 0;
	} else
		c = inchar();
	while( c == ' ' || c == '\t' )
		c = inchar();
	return c;
}

#define N_PTREGS	44
static char *regnames[N_PTREGS] = {
	"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
	"r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
	"r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
	"r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
	"pc", "msr", "or3", "ctr", "lr", "xer", "ccr", "mq",
	"trap", "dar", "dsisr", "res"
};

int
scanhex(unsigned *vp)
{
	int c, d;
	unsigned v;

	c = skipbl();
	if (c == '%') {
		/* parse register name */
		char regname[8];
		int i;

		for (i = 0; i < sizeof(regname) - 1; ++i) {
			c = inchar();
			if (!isalnum(c)) {
				termch = c;
				break;
			}
			regname[i] = c;
		}
		regname[i] = 0;
		for (i = 0; i < N_PTREGS; ++i) {
			if (strcmp(regnames[i], regname) == 0) {
				unsigned *rp = (unsigned *)
					xmon_regs[smp_processor_id()];
				if (rp == NULL) {
					printf("regs not available\n");
					return 0;
				}
				*vp = rp[i];
				return 1;
			}
		}
		printf("invalid register name '%%%s'\n", regname);
		return 0;
	} else if (c == '$') {
		static char symname[128];
		int i;
		for (i=0; i<63; i++) {
			c = inchar();
			if (isspace(c)) {
				termch = c;
				break;
			}
			symname[i] = c;
		}
		symname[i++] = 0;
		*vp = 0;
		if (setjmp(bus_error_jmp) == 0) {
			debugger_fault_handler = handle_fault;
			sync();
			*vp = kallsyms_lookup_name(symname);
			sync();
		}
		debugger_fault_handler = NULL;
		if (!(*vp)) {
			printf("unknown symbol\n");
			return 0;
		}
		return 1;
	}

	d = hexdigit(c);
	if( d == EOF ){
		termch = c;
		return 0;
	}
	v = 0;
	do {
		v = (v << 4) + d;
		c = inchar();
		d = hexdigit(c);
	} while( d != EOF );
	termch = c;
	*vp = v;
	return 1;
}

void
scannl(void)
{
	int c;

	c = termch;
	termch = 0;
	while( c != '\n' )
		c = inchar();
}

int hexdigit(int c)
{
	if( '0' <= c && c <= '9' )
		return c - '0';
	if( 'A' <= c && c <= 'F' )
		return c - ('A' - 10);
	if( 'a' <= c && c <= 'f' )
		return c - ('a' - 10);
	return EOF;
}

void
getstring(char *s, int size)
{
	int c;

	c = skipbl();
	do {
		if( size > 1 ){
			*s++ = c;
			--size;
		}
		c = inchar();
	} while( c != ' ' && c != '\t' && c != '\n' );
	termch = c;
	*s = 0;
}

static char line[256];
static char *lineptr;

void
flush_input(void)
{
	lineptr = NULL;
}

int
inchar(void)
{
	if (lineptr == NULL || *lineptr == 0) {
		if (fgets(line, sizeof(line), stdin) == NULL) {
			lineptr = NULL;
			return EOF;
		}
		lineptr = line;
	}
	return *lineptr++;
}

void
take_input(char *str)
{
	lineptr = str;
}

static void
symbol_lookup(void)
{
	int type = inchar();
	unsigned addr;
	static char tmp[128];

	switch (type) {
	case 'a':
		if (scanhex(&addr))
			xmon_print_symbol(addr, ": ", "\n");
		termch = 0;
		break;
	case 's':
		getstring(tmp, 64);
		if (setjmp(bus_error_jmp) == 0) {
			debugger_fault_handler = handle_fault;
			sync();
			addr = kallsyms_lookup_name(tmp);
			if (addr)
				printf("%s: %lx\n", tmp, addr);
			else
				printf("Symbol '%s' not found.\n", tmp);
			sync();
		}
		debugger_fault_handler = NULL;
		termch = 0;
		break;
	}
}


/*
 *  arch/mips/kernel/gdb-stub.c
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
 *
 *  Copyright (C) 2003 MontaVista Software Inc.
 *  Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
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
 *    $ ./configure --target=mips-linux-elf
 *    $ make
 *    $ install
 *    $ which mips-linux-elf-gdb
 *    /usr/local/bin/mips-linux-elf-gdb
 *
 *  Step 2:
 *  Configure linux for remote debugging and build it.
 *
 *  Example:
 *    $ cd ~/linux
 *    $ make menuconfig <go to "Kernel Hacking" and turn on remote debugging>
 *    $ make
 *
 *  Step 3:
 *  Download the kernel to the remote target and start
 *  the kernel running. It will promptly halt and wait
 *  for the host gdb session to connect. It does this
 *  since the "Kernel Hacking" option has defined
 *  CONFIG_KGDB which in turn enables your calls
 *  to:
 *     set_debug_traps();
 *     breakpoint();
 *
 *  Step 4:
 *  Start the gdb session on the host.
 *
 *  Example:
 *    $ mips-linux-elf-gdb vmlinux
 *    (gdb) set remotebaud 115200
 *    (gdb) target remote /dev/ttyS1
 *    ...at this point you are connected to
 *       the remote target and can use gdb
 *       in the normal fasion. Setting
 *       breakpoints, single stepping,
 *       printing variables, etc.
 */
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/reboot.h>

#include <asm/asm.h>
#include <asm/cacheflush.h>
#include <asm/mipsregs.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/gdb-stub.h>
#include <asm/inst.h>
#include <asm/smp.h>

/*
 * external low-level support routines
 */

extern int putDebugChar(char c);    /* write a single character      */
extern char getDebugChar(void);     /* read and return a single char */
extern void trap_low(void);

/*
 * breakpoint and test functions
 */
extern void breakpoint(void);
extern void breakinst(void);
extern void async_breakpoint(void);
extern void async_breakinst(void);
extern void adel(void);

/*
 * local prototypes
 */

static void getpacket(char *buffer);
static void putpacket(char *buffer);
static int computeSignal(int tt);
static int hex(unsigned char ch);
static int hexToInt(char **ptr, int *intValue);
static int hexToLong(char **ptr, long *longValue);
static unsigned char *mem2hex(char *mem, char *buf, int count, int may_fault);
void handle_exception(struct gdb_regs *regs);

int kgdb_enabled;

/*
 * spin locks for smp case
 */
static DEFINE_SPINLOCK(kgdb_lock);
static raw_spinlock_t kgdb_cpulock[NR_CPUS] = {
	[0 ... NR_CPUS-1] = __RAW_SPIN_LOCK_UNLOCKED,
};

/*
 * BUFMAX defines the maximum number of characters in inbound/outbound buffers
 * at least NUMREGBYTES*2 are needed for register packets
 */
#define BUFMAX 2048

static char input_buffer[BUFMAX];
static char output_buffer[BUFMAX];
static int initialized;	/* !0 means we've been initialized */
static int kgdb_started;
static const char hexchars[]="0123456789abcdef";

/* Used to prevent crashes in memory access.  Note that they'll crash anyway if
   we haven't set up fault handlers yet... */
int kgdb_read_byte(unsigned char *address, unsigned char *dest);
int kgdb_write_byte(unsigned char val, unsigned char *dest);

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

/*
 * scan for the sequence $<data>#<checksum>
 */
static void getpacket(char *buffer)
{
	unsigned char checksum;
	unsigned char xmitcsum;
	int i;
	int count;
	unsigned char ch;

	do {
		/*
		 * wait around for the start character,
		 * ignore all other characters
		 */
		while ((ch = (getDebugChar() & 0x7f)) != '$') ;

		checksum = 0;
		xmitcsum = -1;
		count = 0;

		/*
		 * now, read until a # or end of buffer is found
		 */
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
			xmitcsum = hex(getDebugChar() & 0x7f) << 4;
			xmitcsum |= hex(getDebugChar() & 0x7f);

			if (checksum != xmitcsum)
				putDebugChar('-');	/* failed checksum */
			else {
				putDebugChar('+'); /* successful transfer */

				/*
				 * if a sequence char is present,
				 * reply the sequence ID
				 */
				if (buffer[2] == ':') {
					putDebugChar(buffer[0]);
					putDebugChar(buffer[1]);

					/*
					 * remove sequence chars from buffer
					 */
					count = strlen(buffer);
					for (i=3; i <= count; i++)
						buffer[i-3] = buffer[i];
				}
			}
		}
	}
	while (checksum != xmitcsum);
}

/*
 * send the packet in buffer.
 */
static void putpacket(char *buffer)
{
	unsigned char checksum;
	int count;
	unsigned char ch;

	/*
	 * $<packet info>#<checksum>.
	 */

	do {
		putDebugChar('$');
		checksum = 0;
		count = 0;

		while ((ch = buffer[count]) != 0) {
			if (!(putDebugChar(ch)))
				return;
			checksum += ch;
			count += 1;
		}

		putDebugChar('#');
		putDebugChar(hexchars[checksum >> 4]);
		putDebugChar(hexchars[checksum & 0xf]);

	}
	while ((getDebugChar() & 0x7f) != '+');
}


/*
 * Convert the memory pointed to by mem into hex, placing result in buf.
 * Return a pointer to the last char put in buf (null), in case of mem fault,
 * return 0.
 * may_fault is non-zero if we are reading from arbitrary memory, but is currently
 * not used.
 */
static unsigned char *mem2hex(char *mem, char *buf, int count, int may_fault)
{
	unsigned char ch;

	while (count-- > 0) {
		if (kgdb_read_byte(mem++, &ch) != 0)
			return 0;
		*buf++ = hexchars[ch >> 4];
		*buf++ = hexchars[ch & 0xf];
	}

	*buf = 0;

	return buf;
}

/*
 * convert the hex array pointed to by buf into binary to be placed in mem
 * return a pointer to the character AFTER the last byte written
 * may_fault is non-zero if we are reading from arbitrary memory, but is currently
 * not used.
 */
static char *hex2mem(char *buf, char *mem, int count, int binary, int may_fault)
{
	int i;
	unsigned char ch;

	for (i=0; i<count; i++)
	{
		if (binary) {
			ch = *buf++;
			if (ch == 0x7d)
				ch = 0x20 ^ *buf++;
		}
		else {
			ch = hex(*buf++) << 4;
			ch |= hex(*buf++);
		}
		if (kgdb_write_byte(ch, mem++) != 0)
			return 0;
	}

	return mem;
}

/*
 * This table contains the mapping between SPARC hardware trap types, and
 * signals, which are primarily what GDB understands.  It also indicates
 * which hardware traps we need to commandeer when initializing the stub.
 */
static struct hard_trap_info {
	unsigned char tt;		/* Trap type code for MIPS R3xxx and R4xxx */
	unsigned char signo;		/* Signal that we map this trap into */
} hard_trap_info[] = {
	{ 6, SIGBUS },			/* instruction bus error */
	{ 7, SIGBUS },			/* data bus error */
	{ 9, SIGTRAP },			/* break */
	{ 10, SIGILL },			/* reserved instruction */
/*	{ 11, SIGILL },		*/	/* CPU unusable */
	{ 12, SIGFPE },			/* overflow */
	{ 13, SIGTRAP },		/* trap */
	{ 14, SIGSEGV },		/* virtual instruction cache coherency */
	{ 15, SIGFPE },			/* floating point exception */
	{ 23, SIGSEGV },		/* watch */
	{ 31, SIGSEGV },		/* virtual data cache coherency */
	{ 0, 0}				/* Must be last */
};

/* Save the normal trap handlers for user-mode traps. */
void *saved_vectors[32];

/*
 * Set up exception handlers for tracing and breakpoints
 */
void set_debug_traps(void)
{
	struct hard_trap_info *ht;
	unsigned long flags;
	unsigned char c;

	local_irq_save(flags);
	for (ht = hard_trap_info; ht->tt && ht->signo; ht++)
		saved_vectors[ht->tt] = set_except_vector(ht->tt, trap_low);

	putDebugChar('+'); /* 'hello world' */
	/*
	 * In case GDB is started before us, ack any packets
	 * (presumably "$?#xx") sitting there.
	 */
	while((c = getDebugChar()) != '$');
	while((c = getDebugChar()) != '#');
	c = getDebugChar(); /* eat first csum byte */
	c = getDebugChar(); /* eat second csum byte */
	putDebugChar('+'); /* ack it */

	initialized = 1;
	local_irq_restore(flags);
}

void restore_debug_traps(void)
{
	struct hard_trap_info *ht;
	unsigned long flags;

	local_irq_save(flags);
	for (ht = hard_trap_info; ht->tt && ht->signo; ht++)
		set_except_vector(ht->tt, saved_vectors[ht->tt]);
	local_irq_restore(flags);
}

/*
 * Convert the MIPS hardware trap type code to a Unix signal number.
 */
static int computeSignal(int tt)
{
	struct hard_trap_info *ht;

	for (ht = hard_trap_info; ht->tt && ht->signo; ht++)
		if (ht->tt == tt)
			return ht->signo;

	return SIGHUP;		/* default for things we don't know about */
}

/*
 * While we find nice hex chars, build an int.
 * Return number of chars processed.
 */
static int hexToInt(char **ptr, int *intValue)
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

static int hexToLong(char **ptr, long *longValue)
{
	int numChars = 0;
	int hexValue;

	*longValue = 0;

	while (**ptr) {
		hexValue = hex(**ptr);
		if (hexValue < 0)
			break;

		*longValue = (*longValue << 4) | hexValue;
		numChars ++;

		(*ptr)++;
	}

	return numChars;
}


#if 0
/*
 * Print registers (on target console)
 * Used only to debug the stub...
 */
void show_gdbregs(struct gdb_regs * regs)
{
	/*
	 * Saved main processor registers
	 */
	printk("$0 : %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
	       regs->reg0, regs->reg1, regs->reg2, regs->reg3,
	       regs->reg4, regs->reg5, regs->reg6, regs->reg7);
	printk("$8 : %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
	       regs->reg8, regs->reg9, regs->reg10, regs->reg11,
	       regs->reg12, regs->reg13, regs->reg14, regs->reg15);
	printk("$16: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
	       regs->reg16, regs->reg17, regs->reg18, regs->reg19,
	       regs->reg20, regs->reg21, regs->reg22, regs->reg23);
	printk("$24: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
	       regs->reg24, regs->reg25, regs->reg26, regs->reg27,
	       regs->reg28, regs->reg29, regs->reg30, regs->reg31);

	/*
	 * Saved cp0 registers
	 */
	printk("epc  : %08lx\nStatus: %08lx\nCause : %08lx\n",
	       regs->cp0_epc, regs->cp0_status, regs->cp0_cause);
}
#endif /* dead code */

/*
 * We single-step by setting breakpoints. When an exception
 * is handled, we need to restore the instructions hoisted
 * when the breakpoints were set.
 *
 * This is where we save the original instructions.
 */
static struct gdb_bp_save {
	unsigned long addr;
	unsigned int val;
} step_bp[2];

#define BP 0x0000000d  /* break opcode */

/*
 * Set breakpoint instructions for single stepping.
 */
static void single_step(struct gdb_regs *regs)
{
	union mips_instruction insn;
	unsigned long targ;
	int is_branch, is_cond, i;

	targ = regs->cp0_epc;
	insn.word = *(unsigned int *)targ;
	is_branch = is_cond = 0;

	switch (insn.i_format.opcode) {
	/*
	 * jr and jalr are in r_format format.
	 */
	case spec_op:
		switch (insn.r_format.func) {
		case jalr_op:
		case jr_op:
			targ = *(&regs->reg0 + insn.r_format.rs);
			is_branch = 1;
			break;
		}
		break;

	/*
	 * This group contains:
	 * bltz_op, bgez_op, bltzl_op, bgezl_op,
	 * bltzal_op, bgezal_op, bltzall_op, bgezall_op.
	 */
	case bcond_op:
		is_branch = is_cond = 1;
		targ += 4 + (insn.i_format.simmediate << 2);
		break;

	/*
	 * These are unconditional and in j_format.
	 */
	case jal_op:
	case j_op:
		is_branch = 1;
		targ += 4;
		targ >>= 28;
		targ <<= 28;
		targ |= (insn.j_format.target << 2);
		break;

	/*
	 * These are conditional.
	 */
	case beq_op:
	case beql_op:
	case bne_op:
	case bnel_op:
	case blez_op:
	case blezl_op:
	case bgtz_op:
	case bgtzl_op:
	case cop0_op:
	case cop1_op:
	case cop2_op:
	case cop1x_op:
		is_branch = is_cond = 1;
		targ += 4 + (insn.i_format.simmediate << 2);
		break;
	}

	if (is_branch) {
		i = 0;
		if (is_cond && targ != (regs->cp0_epc + 8)) {
			step_bp[i].addr = regs->cp0_epc + 8;
			step_bp[i++].val = *(unsigned *)(regs->cp0_epc + 8);
			*(unsigned *)(regs->cp0_epc + 8) = BP;
		}
		step_bp[i].addr = targ;
		step_bp[i].val  = *(unsigned *)targ;
		*(unsigned *)targ = BP;
	} else {
		step_bp[0].addr = regs->cp0_epc + 4;
		step_bp[0].val  = *(unsigned *)(regs->cp0_epc + 4);
		*(unsigned *)(regs->cp0_epc + 4) = BP;
	}
}

/*
 *  If asynchronously interrupted by gdb, then we need to set a breakpoint
 *  at the interrupted instruction so that we wind up stopped with a
 *  reasonable stack frame.
 */
static struct gdb_bp_save async_bp;

/*
 * Swap the interrupted EPC with our asynchronous breakpoint routine.
 * This is safer than stuffing the breakpoint in-place, since no cache
 * flushes (or resulting smp_call_functions) are required.  The
 * assumption is that only one CPU will be handling asynchronous bp's,
 * and only one can be active at a time.
 */
extern spinlock_t smp_call_lock;

void set_async_breakpoint(unsigned long *epc)
{
	/* skip breaking into userland */
	if ((*epc & 0x80000000) == 0)
		return;

#ifdef CONFIG_SMP
	/* avoid deadlock if someone is make IPC */
	if (spin_is_locked(&smp_call_lock))
		return;
#endif

	async_bp.addr = *epc;
	*epc = (unsigned long)async_breakpoint;
}

static void kgdb_wait(void *arg)
{
	unsigned flags;
	int cpu = smp_processor_id();

	local_irq_save(flags);

	__raw_spin_lock(&kgdb_cpulock[cpu]);
	__raw_spin_unlock(&kgdb_cpulock[cpu]);

	local_irq_restore(flags);
}

/*
 * GDB stub needs to call kgdb_wait on all processor with interrupts
 * disabled, so it uses it's own special variant.
 */
static int kgdb_smp_call_kgdb_wait(void)
{
#ifdef CONFIG_SMP
	struct call_data_struct data;
	int i, cpus = num_online_cpus() - 1;
	int cpu = smp_processor_id();

	/*
	 * Can die spectacularly if this CPU isn't yet marked online
	 */
	BUG_ON(!cpu_online(cpu));

	if (!cpus)
		return 0;

	if (spin_is_locked(&smp_call_lock)) {
		/*
		 * Some other processor is trying to make us do something
		 * but we're not going to respond... give up
		 */
		return -1;
		}

	/*
	 * We will continue here, accepting the fact that
	 * the kernel may deadlock if another CPU attempts
	 * to call smp_call_function now...
	 */

	data.func = kgdb_wait;
	data.info = NULL;
	atomic_set(&data.started, 0);
	data.wait = 0;

	spin_lock(&smp_call_lock);
	call_data = &data;
	mb();

	/* Send a message to all other CPUs and wait for them to respond */
	for (i = 0; i < NR_CPUS; i++)
		if (cpu_online(i) && i != cpu)
			core_send_ipi(i, SMP_CALL_FUNCTION);

	/* Wait for response */
	/* FIXME: lock-up detection, backtrace on lock-up */
	while (atomic_read(&data.started) != cpus)
		barrier();

	call_data = NULL;
	spin_unlock(&smp_call_lock);
#endif

	return 0;
}

/*
 * This function does all command processing for interfacing to gdb.  It
 * returns 1 if you should skip the instruction at the trap address, 0
 * otherwise.
 */
void handle_exception (struct gdb_regs *regs)
{
	int trap;			/* Trap type */
	int sigval;
	long addr;
	int length;
	char *ptr;
	unsigned long *stack;
	int i;
	int bflag = 0;

	kgdb_started = 1;

	/*
	 * acquire the big kgdb spinlock
	 */
	if (!spin_trylock(&kgdb_lock)) {
		/*
		 * some other CPU has the lock, we should go back to
		 * receive the gdb_wait IPC
		 */
		return;
	}

	/*
	 * If we're in async_breakpoint(), restore the real EPC from
	 * the breakpoint.
	 */
	if (regs->cp0_epc == (unsigned long)async_breakinst) {
		regs->cp0_epc = async_bp.addr;
		async_bp.addr = 0;
	}

	/*
	 * acquire the CPU spinlocks
	 */
	for (i = num_online_cpus()-1; i >= 0; i--)
		if (__raw_spin_trylock(&kgdb_cpulock[i]) == 0)
			panic("kgdb: couldn't get cpulock %d\n", i);

	/*
	 * force other cpus to enter kgdb
	 */
	kgdb_smp_call_kgdb_wait();

	/*
	 * If we're in breakpoint() increment the PC
	 */
	trap = (regs->cp0_cause & 0x7c) >> 2;
	if (trap == 9 && regs->cp0_epc == (unsigned long)breakinst)
		regs->cp0_epc += 4;

	/*
	 * If we were single_stepping, restore the opcodes hoisted
	 * for the breakpoint[s].
	 */
	if (step_bp[0].addr) {
		*(unsigned *)step_bp[0].addr = step_bp[0].val;
		step_bp[0].addr = 0;

		if (step_bp[1].addr) {
			*(unsigned *)step_bp[1].addr = step_bp[1].val;
			step_bp[1].addr = 0;
		}
	}

	stack = (long *)regs->reg29;			/* stack ptr */
	sigval = computeSignal(trap);

	/*
	 * reply to host that an exception has occurred
	 */
	ptr = output_buffer;

	/*
	 * Send trap type (converted to signal)
	 */
	*ptr++ = 'T';
	*ptr++ = hexchars[sigval >> 4];
	*ptr++ = hexchars[sigval & 0xf];

	/*
	 * Send Error PC
	 */
	*ptr++ = hexchars[REG_EPC >> 4];
	*ptr++ = hexchars[REG_EPC & 0xf];
	*ptr++ = ':';
	ptr = mem2hex((char *)&regs->cp0_epc, ptr, sizeof(long), 0);
	*ptr++ = ';';

	/*
	 * Send frame pointer
	 */
	*ptr++ = hexchars[REG_FP >> 4];
	*ptr++ = hexchars[REG_FP & 0xf];
	*ptr++ = ':';
	ptr = mem2hex((char *)&regs->reg30, ptr, sizeof(long), 0);
	*ptr++ = ';';

	/*
	 * Send stack pointer
	 */
	*ptr++ = hexchars[REG_SP >> 4];
	*ptr++ = hexchars[REG_SP & 0xf];
	*ptr++ = ':';
	ptr = mem2hex((char *)&regs->reg29, ptr, sizeof(long), 0);
	*ptr++ = ';';

	*ptr++ = 0;
	putpacket(output_buffer);	/* send it off... */

	/*
	 * Wait for input from remote GDB
	 */
	while (1) {
		output_buffer[0] = 0;
		getpacket(input_buffer);

		switch (input_buffer[0])
		{
		case '?':
			output_buffer[0] = 'S';
			output_buffer[1] = hexchars[sigval >> 4];
			output_buffer[2] = hexchars[sigval & 0xf];
			output_buffer[3] = 0;
			break;

		/*
		 * Detach debugger; let CPU run
		 */
		case 'D':
			putpacket(output_buffer);
			goto finish_kgdb;
			break;

		case 'd':
			/* toggle debug flag */
			break;

		/*
		 * Return the value of the CPU registers
		 */
		case 'g':
			ptr = output_buffer;
			ptr = mem2hex((char *)&regs->reg0, ptr, 32*sizeof(long), 0); /* r0...r31 */
			ptr = mem2hex((char *)&regs->cp0_status, ptr, 6*sizeof(long), 0); /* cp0 */
			ptr = mem2hex((char *)&regs->fpr0, ptr, 32*sizeof(long), 0); /* f0...31 */
			ptr = mem2hex((char *)&regs->cp1_fsr, ptr, 2*sizeof(long), 0); /* cp1 */
			ptr = mem2hex((char *)&regs->frame_ptr, ptr, 2*sizeof(long), 0); /* frp */
			ptr = mem2hex((char *)&regs->cp0_index, ptr, 16*sizeof(long), 0); /* cp0 */
			break;

		/*
		 * set the value of the CPU registers - return OK
		 */
		case 'G':
		{
			ptr = &input_buffer[1];
			hex2mem(ptr, (char *)&regs->reg0, 32*sizeof(long), 0, 0);
			ptr += 32*(2*sizeof(long));
			hex2mem(ptr, (char *)&regs->cp0_status, 6*sizeof(long), 0, 0);
			ptr += 6*(2*sizeof(long));
			hex2mem(ptr, (char *)&regs->fpr0, 32*sizeof(long), 0, 0);
			ptr += 32*(2*sizeof(long));
			hex2mem(ptr, (char *)&regs->cp1_fsr, 2*sizeof(long), 0, 0);
			ptr += 2*(2*sizeof(long));
			hex2mem(ptr, (char *)&regs->frame_ptr, 2*sizeof(long), 0, 0);
			ptr += 2*(2*sizeof(long));
			hex2mem(ptr, (char *)&regs->cp0_index, 16*sizeof(long), 0, 0);
			strcpy(output_buffer,"OK");
		 }
		break;

		/*
		 * mAA..AA,LLLL  Read LLLL bytes at address AA..AA
		 */
		case 'm':
			ptr = &input_buffer[1];

			if (hexToLong(&ptr, &addr)
				&& *ptr++ == ','
				&& hexToInt(&ptr, &length)) {
				if (mem2hex((char *)addr, output_buffer, length, 1))
					break;
				strcpy (output_buffer, "E03");
			} else
				strcpy(output_buffer,"E01");
			break;

		/*
		 * XAA..AA,LLLL: Write LLLL escaped binary bytes at address AA.AA
		 */
		case 'X':
			bflag = 1;
			/* fall through */

		/*
		 * MAA..AA,LLLL: Write LLLL bytes at address AA.AA return OK
		 */
		case 'M':
			ptr = &input_buffer[1];

			if (hexToLong(&ptr, &addr)
				&& *ptr++ == ','
				&& hexToInt(&ptr, &length)
				&& *ptr++ == ':') {
				if (hex2mem(ptr, (char *)addr, length, bflag, 1))
					strcpy(output_buffer, "OK");
				else
					strcpy(output_buffer, "E03");
			}
			else
				strcpy(output_buffer, "E02");
			break;

		/*
		 * cAA..AA    Continue at address AA..AA(optional)
		 */
		case 'c':
			/* try to read optional parameter, pc unchanged if no parm */

			ptr = &input_buffer[1];
			if (hexToLong(&ptr, &addr))
				regs->cp0_epc = addr;

			goto exit_kgdb_exception;
			break;

		/*
		 * kill the program; let us try to restart the machine
		 * Reset the whole machine.
		 */
		case 'k':
		case 'r':
			machine_restart("kgdb restarts machine");
			break;

		/*
		 * Step to next instruction
		 */
		case 's':
			/*
			 * There is no single step insn in the MIPS ISA, so we
			 * use breakpoints and continue, instead.
			 */
			single_step(regs);
			goto exit_kgdb_exception;
			/* NOTREACHED */
			break;

		/*
		 * Set baud rate (bBB)
		 * FIXME: Needs to be written
		 */
		case 'b':
		{
#if 0
			int baudrate;
			extern void set_timer_3();

			ptr = &input_buffer[1];
			if (!hexToInt(&ptr, &baudrate))
			{
				strcpy(output_buffer,"B01");
				break;
			}

			/* Convert baud rate to uart clock divider */

			switch (baudrate)
			{
				case 38400:
					baudrate = 16;
					break;
				case 19200:
					baudrate = 33;
					break;
				case 9600:
					baudrate = 65;
					break;
				default:
					baudrate = 0;
					strcpy(output_buffer,"B02");
					goto x1;
			}

			if (baudrate) {
				putpacket("OK");	/* Ack before changing speed */
				set_timer_3(baudrate); /* Set it */
			}
#endif
		}
		break;

		}			/* switch */

		/*
		 * reply to the request
		 */

		putpacket(output_buffer);

	} /* while */

	return;

finish_kgdb:
	restore_debug_traps();

exit_kgdb_exception:
	/* release locks so other CPUs can go */
	for (i = num_online_cpus()-1; i >= 0; i--)
		__raw_spin_unlock(&kgdb_cpulock[i]);
	spin_unlock(&kgdb_lock);

	__flush_cache_all();
	return;
}

/*
 * This function will generate a breakpoint exception.  It is used at the
 * beginning of a program to sync up with a debugger and can be used
 * otherwise as a quick means to stop program execution and "break" into
 * the debugger.
 */
void breakpoint(void)
{
	if (!initialized)
		return;

	__asm__ __volatile__(
			".globl	breakinst\n\t"
			".set\tnoreorder\n\t"
			"nop\n"
			"breakinst:\tbreak\n\t"
			"nop\n\t"
			".set\treorder"
			);
}

/* Nothing but the break; don't pollute any registers */
void async_breakpoint(void)
{
	__asm__ __volatile__(
			".globl	async_breakinst\n\t"
			".set\tnoreorder\n\t"
			"nop\n"
			"async_breakinst:\tbreak\n\t"
			"nop\n\t"
			".set\treorder"
			);
}

void adel(void)
{
	__asm__ __volatile__(
			".globl\tadel\n\t"
			"lui\t$8,0x8000\n\t"
			"lw\t$9,1($8)\n\t"
			);
}

/*
 * malloc is needed by gdb client in "call func()", even a private one
 * will make gdb happy
 */
static void __used *malloc(size_t size)
{
	return kmalloc(size, GFP_ATOMIC);
}

static void __used free(void *where)
{
	kfree(where);
}

#ifdef CONFIG_GDB_CONSOLE

void gdb_putsn(const char *str, int l)
{
	char outbuf[18];

	if (!kgdb_started)
		return;

	outbuf[0]='O';

	while(l) {
		int i = (l>8)?8:l;
		mem2hex((char *)str, &outbuf[1], i, 0);
		outbuf[(i*2)+1]=0;
		putpacket(outbuf);
		str += i;
		l -= i;
	}
}

static void gdb_console_write(struct console *con, const char *s, unsigned n)
{
	gdb_putsn(s, n);
}

static struct console gdb_console = {
	.name	= "gdb",
	.write	= gdb_console_write,
	.flags	= CON_PRINTBUFFER,
	.index	= -1
};

static int __init register_gdb_console(void)
{
	register_console(&gdb_console);

	return 0;
}

console_initcall(register_gdb_console);

#endif

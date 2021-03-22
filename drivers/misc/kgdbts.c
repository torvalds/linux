// SPDX-License-Identifier: GPL-2.0-only
/*
 * kgdbts is a test suite for kgdb for the sole purpose of validating
 * that key pieces of the kgdb internals are working properly such as
 * HW/SW breakpoints, single stepping, and NMI.
 *
 * Created by: Jason Wessel <jason.wessel@windriver.com>
 *
 * Copyright (c) 2008 Wind River Systems, Inc.
 */
/* Information about the kgdb test suite.
 * -------------------------------------
 *
 * The kgdb test suite is designed as a KGDB I/O module which
 * simulates the communications that a debugger would have with kgdb.
 * The tests are broken up in to a line by line and referenced here as
 * a "get" which is kgdb requesting input and "put" which is kgdb
 * sending a response.
 *
 * The kgdb suite can be invoked from the kernel command line
 * arguments system or executed dynamically at run time.  The test
 * suite uses the variable "kgdbts" to obtain the information about
 * which tests to run and to configure the verbosity level.  The
 * following are the various characters you can use with the kgdbts=
 * line:
 *
 * When using the "kgdbts=" you only choose one of the following core
 * test types:
 * A = Run all the core tests silently
 * V1 = Run all the core tests with minimal output
 * V2 = Run all the core tests in debug mode
 *
 * You can also specify optional tests:
 * N## = Go to sleep with interrupts of for ## seconds
 *       to test the HW NMI watchdog
 * F## = Break at kernel_clone for ## iterations
 * S## = Break at sys_open for ## iterations
 * I## = Run the single step test ## iterations
 *
 * NOTE: that the kernel_clone and sys_open tests are mutually exclusive.
 *
 * To invoke the kgdb test suite from boot you use a kernel start
 * argument as follows:
 * 	kgdbts=V1 kgdbwait
 * Or if you wanted to perform the NMI test for 6 seconds and kernel_clone
 * test for 100 forks, you could use:
 * 	kgdbts=V1N6F100 kgdbwait
 *
 * The test suite can also be invoked at run time with:
 *	echo kgdbts=V1N6F100 > /sys/module/kgdbts/parameters/kgdbts
 * Or as another example:
 *	echo kgdbts=V2 > /sys/module/kgdbts/parameters/kgdbts
 *
 * When developing a new kgdb arch specific implementation or
 * using these tests for the purpose of regression testing,
 * several invocations are required.
 *
 * 1) Boot with the test suite enabled by using the kernel arguments
 *       "kgdbts=V1F100 kgdbwait"
 *    ## If kgdb arch specific implementation has NMI use
 *       "kgdbts=V1N6F100
 *
 * 2) After the system boot run the basic test.
 * echo kgdbts=V1 > /sys/module/kgdbts/parameters/kgdbts
 *
 * 3) Run the concurrency tests.  It is best to use n+1
 *    while loops where n is the number of cpus you have
 *    in your system.  The example below uses only two
 *    loops.
 *
 * ## This tests break points on sys_open
 * while [ 1 ] ; do find / > /dev/null 2>&1 ; done &
 * while [ 1 ] ; do find / > /dev/null 2>&1 ; done &
 * echo kgdbts=V1S10000 > /sys/module/kgdbts/parameters/kgdbts
 * fg # and hit control-c
 * fg # and hit control-c
 * ## This tests break points on kernel_clone
 * while [ 1 ] ; do date > /dev/null ; done &
 * while [ 1 ] ; do date > /dev/null ; done &
 * echo kgdbts=V1F1000 > /sys/module/kgdbts/parameters/kgdbts
 * fg # and hit control-c
 *
 */

#include <linux/kernel.h>
#include <linux/kgdb.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/nmi.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched/task.h>

#include <asm/sections.h>

#define v1printk(a...) do {		\
	if (verbose)			\
		printk(KERN_INFO a);	\
} while (0)
#define v2printk(a...) do {		\
	if (verbose > 1)		\
		printk(KERN_INFO a);	\
	touch_nmi_watchdog();		\
} while (0)
#define eprintk(a...) do {		\
	printk(KERN_ERR a);		\
	WARN_ON(1);			\
} while (0)
#define MAX_CONFIG_LEN		40

static struct kgdb_io kgdbts_io_ops;
static char get_buf[BUFMAX];
static int get_buf_cnt;
static char put_buf[BUFMAX];
static int put_buf_cnt;
static char scratch_buf[BUFMAX];
static int verbose;
static int repeat_test;
static int test_complete;
static int send_ack;
static int final_ack;
static int force_hwbrks;
static int hwbreaks_ok;
static int hw_break_val;
static int hw_break_val2;
static int cont_instead_of_sstep;
static unsigned long cont_thread_id;
static unsigned long sstep_thread_id;
#if defined(CONFIG_ARM) || defined(CONFIG_MIPS) || defined(CONFIG_SPARC)
static int arch_needs_sstep_emulation = 1;
#else
static int arch_needs_sstep_emulation;
#endif
static unsigned long cont_addr;
static unsigned long sstep_addr;
static int restart_from_top_after_write;
static int sstep_state;

/* Storage for the registers, in GDB format. */
static unsigned long kgdbts_gdb_regs[(NUMREGBYTES +
					sizeof(unsigned long) - 1) /
					sizeof(unsigned long)];
static struct pt_regs kgdbts_regs;

/* -1 = init not run yet, 0 = unconfigured, 1 = configured. */
static int configured		= -1;

#ifdef CONFIG_KGDB_TESTS_BOOT_STRING
static char config[MAX_CONFIG_LEN] = CONFIG_KGDB_TESTS_BOOT_STRING;
#else
static char config[MAX_CONFIG_LEN];
#endif
static struct kparam_string kps = {
	.string			= config,
	.maxlen			= MAX_CONFIG_LEN,
};

static void fill_get_buf(char *buf);

struct test_struct {
	char *get;
	char *put;
	void (*get_handler)(char *);
	int (*put_handler)(char *, char *);
};

struct test_state {
	char *name;
	struct test_struct *tst;
	int idx;
	int (*run_test) (int, int);
	int (*validate_put) (char *);
};

static struct test_state ts;

static int kgdbts_unreg_thread(void *ptr)
{
	/* Wait until the tests are complete and then ungresiter the I/O
	 * driver.
	 */
	while (!final_ack)
		msleep_interruptible(1500);
	/* Pause for any other threads to exit after final ack. */
	msleep_interruptible(1000);
	if (configured)
		kgdb_unregister_io_module(&kgdbts_io_ops);
	configured = 0;

	return 0;
}

/* This is noinline such that it can be used for a single location to
 * place a breakpoint
 */
static noinline void kgdbts_break_test(void)
{
	v2printk("kgdbts: breakpoint complete\n");
}

/* Lookup symbol info in the kernel */
static unsigned long lookup_addr(char *arg)
{
	unsigned long addr = 0;

	if (!strcmp(arg, "kgdbts_break_test"))
		addr = (unsigned long)kgdbts_break_test;
	else if (!strcmp(arg, "sys_open"))
		addr = (unsigned long)do_sys_open;
	else if (!strcmp(arg, "kernel_clone"))
		addr = (unsigned long)kernel_clone;
	else if (!strcmp(arg, "hw_break_val"))
		addr = (unsigned long)&hw_break_val;
	addr = (unsigned long) dereference_function_descriptor((void *)addr);
	return addr;
}

static void break_helper(char *bp_type, char *arg, unsigned long vaddr)
{
	unsigned long addr;

	if (arg)
		addr = lookup_addr(arg);
	else
		addr = vaddr;

	sprintf(scratch_buf, "%s,%lx,%i", bp_type, addr,
		BREAK_INSTR_SIZE);
	fill_get_buf(scratch_buf);
}

static void sw_break(char *arg)
{
	break_helper(force_hwbrks ? "Z1" : "Z0", arg, 0);
}

static void sw_rem_break(char *arg)
{
	break_helper(force_hwbrks ? "z1" : "z0", arg, 0);
}

static void hw_break(char *arg)
{
	break_helper("Z1", arg, 0);
}

static void hw_rem_break(char *arg)
{
	break_helper("z1", arg, 0);
}

static void hw_write_break(char *arg)
{
	break_helper("Z2", arg, 0);
}

static void hw_rem_write_break(char *arg)
{
	break_helper("z2", arg, 0);
}

static void hw_access_break(char *arg)
{
	break_helper("Z4", arg, 0);
}

static void hw_rem_access_break(char *arg)
{
	break_helper("z4", arg, 0);
}

static void hw_break_val_access(void)
{
	hw_break_val2 = hw_break_val;
}

static void hw_break_val_write(void)
{
	hw_break_val++;
}

static int get_thread_id_continue(char *put_str, char *arg)
{
	char *ptr = &put_str[11];

	if (put_str[1] != 'T' || put_str[2] != '0')
		return 1;
	kgdb_hex2long(&ptr, &cont_thread_id);
	return 0;
}

static int check_and_rewind_pc(char *put_str, char *arg)
{
	unsigned long addr = lookup_addr(arg);
	unsigned long ip;
	int offset = 0;

	kgdb_hex2mem(&put_str[1], (char *)kgdbts_gdb_regs,
		 NUMREGBYTES);
	gdb_regs_to_pt_regs(kgdbts_gdb_regs, &kgdbts_regs);
	ip = instruction_pointer(&kgdbts_regs);
	v2printk("Stopped at IP: %lx\n", ip);
#ifdef GDB_ADJUSTS_BREAK_OFFSET
	/* On some arches, a breakpoint stop requires it to be decremented */
	if (addr + BREAK_INSTR_SIZE == ip)
		offset = -BREAK_INSTR_SIZE;
#endif

	if (arch_needs_sstep_emulation && sstep_addr &&
	    ip + offset == sstep_addr &&
	    ((!strcmp(arg, "sys_open") || !strcmp(arg, "kernel_clone")))) {
		/* This is special case for emulated single step */
		v2printk("Emul: rewind hit single step bp\n");
		restart_from_top_after_write = 1;
	} else if (strcmp(arg, "silent") && ip + offset != addr) {
		eprintk("kgdbts: BP mismatch %lx expected %lx\n",
			   ip + offset, addr);
		return 1;
	}
	/* Readjust the instruction pointer if needed */
	ip += offset;
	cont_addr = ip;
#ifdef GDB_ADJUSTS_BREAK_OFFSET
	instruction_pointer_set(&kgdbts_regs, ip);
#endif
	return 0;
}

static int check_single_step(char *put_str, char *arg)
{
	unsigned long addr = lookup_addr(arg);
	static int matched_id;

	/*
	 * From an arch indepent point of view the instruction pointer
	 * should be on a different instruction
	 */
	kgdb_hex2mem(&put_str[1], (char *)kgdbts_gdb_regs,
		 NUMREGBYTES);
	gdb_regs_to_pt_regs(kgdbts_gdb_regs, &kgdbts_regs);
	v2printk("Singlestep stopped at IP: %lx\n",
		   instruction_pointer(&kgdbts_regs));

	if (sstep_thread_id != cont_thread_id) {
		/*
		 * Ensure we stopped in the same thread id as before, else the
		 * debugger should continue until the original thread that was
		 * single stepped is scheduled again, emulating gdb's behavior.
		 */
		v2printk("ThrID does not match: %lx\n", cont_thread_id);
		if (arch_needs_sstep_emulation) {
			if (matched_id &&
			    instruction_pointer(&kgdbts_regs) != addr)
				goto continue_test;
			matched_id++;
			ts.idx -= 2;
			sstep_state = 0;
			return 0;
		}
		cont_instead_of_sstep = 1;
		ts.idx -= 4;
		return 0;
	}
continue_test:
	matched_id = 0;
	if (instruction_pointer(&kgdbts_regs) == addr) {
		eprintk("kgdbts: SingleStep failed at %lx\n",
			   instruction_pointer(&kgdbts_regs));
		return 1;
	}

	return 0;
}

static void write_regs(char *arg)
{
	memset(scratch_buf, 0, sizeof(scratch_buf));
	scratch_buf[0] = 'G';
	pt_regs_to_gdb_regs(kgdbts_gdb_regs, &kgdbts_regs);
	kgdb_mem2hex((char *)kgdbts_gdb_regs, &scratch_buf[1], NUMREGBYTES);
	fill_get_buf(scratch_buf);
}

static void skip_back_repeat_test(char *arg)
{
	int go_back = simple_strtol(arg, NULL, 10);

	repeat_test--;
	if (repeat_test <= 0) {
		ts.idx++;
	} else {
		if (repeat_test % 100 == 0)
			v1printk("kgdbts:RUN ... %d remaining\n", repeat_test);

		ts.idx -= go_back;
	}
	fill_get_buf(ts.tst[ts.idx].get);
}

static int got_break(char *put_str, char *arg)
{
	test_complete = 1;
	if (!strncmp(put_str+1, arg, 2)) {
		if (!strncmp(arg, "T0", 2))
			test_complete = 2;
		return 0;
	}
	return 1;
}

static void get_cont_catch(char *arg)
{
	/* Always send detach because the test is completed at this point */
	fill_get_buf("D");
}

static int put_cont_catch(char *put_str, char *arg)
{
	/* This is at the end of the test and we catch any and all input */
	v2printk("kgdbts: cleanup task: %lx\n", sstep_thread_id);
	ts.idx--;
	return 0;
}

static int emul_reset(char *put_str, char *arg)
{
	if (strncmp(put_str, "$OK", 3))
		return 1;
	if (restart_from_top_after_write) {
		restart_from_top_after_write = 0;
		ts.idx = -1;
	}
	return 0;
}

static void emul_sstep_get(char *arg)
{
	if (!arch_needs_sstep_emulation) {
		if (cont_instead_of_sstep) {
			cont_instead_of_sstep = 0;
			fill_get_buf("c");
		} else {
			fill_get_buf(arg);
		}
		return;
	}
	switch (sstep_state) {
	case 0:
		v2printk("Emulate single step\n");
		/* Start by looking at the current PC */
		fill_get_buf("g");
		break;
	case 1:
		/* set breakpoint */
		break_helper("Z0", NULL, sstep_addr);
		break;
	case 2:
		/* Continue */
		fill_get_buf("c");
		break;
	case 3:
		/* Clear breakpoint */
		break_helper("z0", NULL, sstep_addr);
		break;
	default:
		eprintk("kgdbts: ERROR failed sstep get emulation\n");
	}
	sstep_state++;
}

static int emul_sstep_put(char *put_str, char *arg)
{
	if (!arch_needs_sstep_emulation) {
		char *ptr = &put_str[11];
		if (put_str[1] != 'T' || put_str[2] != '0')
			return 1;
		kgdb_hex2long(&ptr, &sstep_thread_id);
		return 0;
	}
	switch (sstep_state) {
	case 1:
		/* validate the "g" packet to get the IP */
		kgdb_hex2mem(&put_str[1], (char *)kgdbts_gdb_regs,
			 NUMREGBYTES);
		gdb_regs_to_pt_regs(kgdbts_gdb_regs, &kgdbts_regs);
		v2printk("Stopped at IP: %lx\n",
			 instruction_pointer(&kgdbts_regs));
		/* Want to stop at IP + break instruction size by default */
		sstep_addr = cont_addr + BREAK_INSTR_SIZE;
		break;
	case 2:
		if (strncmp(put_str, "$OK", 3)) {
			eprintk("kgdbts: failed sstep break set\n");
			return 1;
		}
		break;
	case 3:
		if (strncmp(put_str, "$T0", 3)) {
			eprintk("kgdbts: failed continue sstep\n");
			return 1;
		} else {
			char *ptr = &put_str[11];
			kgdb_hex2long(&ptr, &sstep_thread_id);
		}
		break;
	case 4:
		if (strncmp(put_str, "$OK", 3)) {
			eprintk("kgdbts: failed sstep break unset\n");
			return 1;
		}
		/* Single step is complete so continue on! */
		sstep_state = 0;
		return 0;
	default:
		eprintk("kgdbts: ERROR failed sstep put emulation\n");
	}

	/* Continue on the same test line until emulation is complete */
	ts.idx--;
	return 0;
}

static int final_ack_set(char *put_str, char *arg)
{
	if (strncmp(put_str+1, arg, 2))
		return 1;
	final_ack = 1;
	return 0;
}
/*
 * Test to plant a breakpoint and detach, which should clear out the
 * breakpoint and restore the original instruction.
 */
static struct test_struct plant_and_detach_test[] = {
	{ "?", "S0*" }, /* Clear break points */
	{ "kgdbts_break_test", "OK", sw_break, }, /* set sw breakpoint */
	{ "D", "OK" }, /* Detach */
	{ "", "" },
};

/*
 * Simple test to write in a software breakpoint, check for the
 * correct stop location and detach.
 */
static struct test_struct sw_breakpoint_test[] = {
	{ "?", "S0*" }, /* Clear break points */
	{ "kgdbts_break_test", "OK", sw_break, }, /* set sw breakpoint */
	{ "c", "T0*", }, /* Continue */
	{ "g", "kgdbts_break_test", NULL, check_and_rewind_pc },
	{ "write", "OK", write_regs },
	{ "kgdbts_break_test", "OK", sw_rem_break }, /*remove breakpoint */
	{ "D", "OK" }, /* Detach */
	{ "D", "OK", NULL,  got_break }, /* On success we made it here */
	{ "", "" },
};

/*
 * Test a known bad memory read location to test the fault handler and
 * read bytes 1-8 at the bad address
 */
static struct test_struct bad_read_test[] = {
	{ "?", "S0*" }, /* Clear break points */
	{ "m0,1", "E*" }, /* read 1 byte at address 1 */
	{ "m0,2", "E*" }, /* read 1 byte at address 2 */
	{ "m0,3", "E*" }, /* read 1 byte at address 3 */
	{ "m0,4", "E*" }, /* read 1 byte at address 4 */
	{ "m0,5", "E*" }, /* read 1 byte at address 5 */
	{ "m0,6", "E*" }, /* read 1 byte at address 6 */
	{ "m0,7", "E*" }, /* read 1 byte at address 7 */
	{ "m0,8", "E*" }, /* read 1 byte at address 8 */
	{ "D", "OK" }, /* Detach which removes all breakpoints and continues */
	{ "", "" },
};

/*
 * Test for hitting a breakpoint, remove it, single step, plant it
 * again and detach.
 */
static struct test_struct singlestep_break_test[] = {
	{ "?", "S0*" }, /* Clear break points */
	{ "kgdbts_break_test", "OK", sw_break, }, /* set sw breakpoint */
	{ "c", "T0*", NULL, get_thread_id_continue }, /* Continue */
	{ "kgdbts_break_test", "OK", sw_rem_break }, /*remove breakpoint */
	{ "g", "kgdbts_break_test", NULL, check_and_rewind_pc },
	{ "write", "OK", write_regs }, /* Write registers */
	{ "s", "T0*", emul_sstep_get, emul_sstep_put }, /* Single step */
	{ "g", "kgdbts_break_test", NULL, check_single_step },
	{ "kgdbts_break_test", "OK", sw_break, }, /* set sw breakpoint */
	{ "c", "T0*", }, /* Continue */
	{ "g", "kgdbts_break_test", NULL, check_and_rewind_pc },
	{ "write", "OK", write_regs }, /* Write registers */
	{ "D", "OK" }, /* Remove all breakpoints and continues */
	{ "", "" },
};

/*
 * Test for hitting a breakpoint at kernel_clone for what ever the number
 * of iterations required by the variable repeat_test.
 */
static struct test_struct do_kernel_clone_test[] = {
	{ "?", "S0*" }, /* Clear break points */
	{ "kernel_clone", "OK", sw_break, }, /* set sw breakpoint */
	{ "c", "T0*", NULL, get_thread_id_continue }, /* Continue */
	{ "kernel_clone", "OK", sw_rem_break }, /*remove breakpoint */
	{ "g", "kernel_clone", NULL, check_and_rewind_pc }, /* check location */
	{ "write", "OK", write_regs, emul_reset }, /* Write registers */
	{ "s", "T0*", emul_sstep_get, emul_sstep_put }, /* Single step */
	{ "g", "kernel_clone", NULL, check_single_step },
	{ "kernel_clone", "OK", sw_break, }, /* set sw breakpoint */
	{ "7", "T0*", skip_back_repeat_test }, /* Loop based on repeat_test */
	{ "D", "OK", NULL, final_ack_set }, /* detach and unregister I/O */
	{ "", "", get_cont_catch, put_cont_catch },
};

/* Test for hitting a breakpoint at sys_open for what ever the number
 * of iterations required by the variable repeat_test.
 */
static struct test_struct sys_open_test[] = {
	{ "?", "S0*" }, /* Clear break points */
	{ "sys_open", "OK", sw_break, }, /* set sw breakpoint */
	{ "c", "T0*", NULL, get_thread_id_continue }, /* Continue */
	{ "sys_open", "OK", sw_rem_break }, /*remove breakpoint */
	{ "g", "sys_open", NULL, check_and_rewind_pc }, /* check location */
	{ "write", "OK", write_regs, emul_reset }, /* Write registers */
	{ "s", "T0*", emul_sstep_get, emul_sstep_put }, /* Single step */
	{ "g", "sys_open", NULL, check_single_step },
	{ "sys_open", "OK", sw_break, }, /* set sw breakpoint */
	{ "7", "T0*", skip_back_repeat_test }, /* Loop based on repeat_test */
	{ "D", "OK", NULL, final_ack_set }, /* detach and unregister I/O */
	{ "", "", get_cont_catch, put_cont_catch },
};

/*
 * Test for hitting a simple hw breakpoint
 */
static struct test_struct hw_breakpoint_test[] = {
	{ "?", "S0*" }, /* Clear break points */
	{ "kgdbts_break_test", "OK", hw_break, }, /* set hw breakpoint */
	{ "c", "T0*", }, /* Continue */
	{ "g", "kgdbts_break_test", NULL, check_and_rewind_pc },
	{ "write", "OK", write_regs },
	{ "kgdbts_break_test", "OK", hw_rem_break }, /*remove breakpoint */
	{ "D", "OK" }, /* Detach */
	{ "D", "OK", NULL,  got_break }, /* On success we made it here */
	{ "", "" },
};

/*
 * Test for hitting a hw write breakpoint
 */
static struct test_struct hw_write_break_test[] = {
	{ "?", "S0*" }, /* Clear break points */
	{ "hw_break_val", "OK", hw_write_break, }, /* set hw breakpoint */
	{ "c", "T0*", NULL, got_break }, /* Continue */
	{ "g", "silent", NULL, check_and_rewind_pc },
	{ "write", "OK", write_regs },
	{ "hw_break_val", "OK", hw_rem_write_break }, /*remove breakpoint */
	{ "D", "OK" }, /* Detach */
	{ "D", "OK", NULL,  got_break }, /* On success we made it here */
	{ "", "" },
};

/*
 * Test for hitting a hw access breakpoint
 */
static struct test_struct hw_access_break_test[] = {
	{ "?", "S0*" }, /* Clear break points */
	{ "hw_break_val", "OK", hw_access_break, }, /* set hw breakpoint */
	{ "c", "T0*", NULL, got_break }, /* Continue */
	{ "g", "silent", NULL, check_and_rewind_pc },
	{ "write", "OK", write_regs },
	{ "hw_break_val", "OK", hw_rem_access_break }, /*remove breakpoint */
	{ "D", "OK" }, /* Detach */
	{ "D", "OK", NULL,  got_break }, /* On success we made it here */
	{ "", "" },
};

/*
 * Test for hitting a hw access breakpoint
 */
static struct test_struct nmi_sleep_test[] = {
	{ "?", "S0*" }, /* Clear break points */
	{ "c", "T0*", NULL, got_break }, /* Continue */
	{ "D", "OK" }, /* Detach */
	{ "D", "OK", NULL,  got_break }, /* On success we made it here */
	{ "", "" },
};

static void fill_get_buf(char *buf)
{
	unsigned char checksum = 0;
	int count = 0;
	char ch;

	strcpy(get_buf, "$");
	strcat(get_buf, buf);
	while ((ch = buf[count])) {
		checksum += ch;
		count++;
	}
	strcat(get_buf, "#");
	get_buf[count + 2] = hex_asc_hi(checksum);
	get_buf[count + 3] = hex_asc_lo(checksum);
	get_buf[count + 4] = '\0';
	v2printk("get%i: %s\n", ts.idx, get_buf);
}

static int validate_simple_test(char *put_str)
{
	char *chk_str;

	if (ts.tst[ts.idx].put_handler)
		return ts.tst[ts.idx].put_handler(put_str,
			ts.tst[ts.idx].put);

	chk_str = ts.tst[ts.idx].put;
	if (*put_str == '$')
		put_str++;

	while (*chk_str != '\0' && *put_str != '\0') {
		/* If someone does a * to match the rest of the string, allow
		 * it, or stop if the received string is complete.
		 */
		if (*put_str == '#' || *chk_str == '*')
			return 0;
		if (*put_str != *chk_str)
			return 1;

		chk_str++;
		put_str++;
	}
	if (*chk_str == '\0' && (*put_str == '\0' || *put_str == '#'))
		return 0;

	return 1;
}

static int run_simple_test(int is_get_char, int chr)
{
	int ret = 0;
	if (is_get_char) {
		/* Send an ACK on the get if a prior put completed and set the
		 * send ack variable
		 */
		if (send_ack) {
			send_ack = 0;
			return '+';
		}
		/* On the first get char, fill the transmit buffer and then
		 * take from the get_string.
		 */
		if (get_buf_cnt == 0) {
			if (ts.tst[ts.idx].get_handler)
				ts.tst[ts.idx].get_handler(ts.tst[ts.idx].get);
			else
				fill_get_buf(ts.tst[ts.idx].get);
		}

		if (get_buf[get_buf_cnt] == '\0') {
			eprintk("kgdbts: ERROR GET: EOB on '%s' at %i\n",
			   ts.name, ts.idx);
			get_buf_cnt = 0;
			fill_get_buf("D");
		}
		ret = get_buf[get_buf_cnt];
		get_buf_cnt++;
		return ret;
	}

	/* This callback is a put char which is when kgdb sends data to
	 * this I/O module.
	 */
	if (ts.tst[ts.idx].get[0] == '\0' && ts.tst[ts.idx].put[0] == '\0' &&
	    !ts.tst[ts.idx].get_handler) {
		eprintk("kgdbts: ERROR: beyond end of test on"
			   " '%s' line %i\n", ts.name, ts.idx);
		return 0;
	}

	if (put_buf_cnt >= BUFMAX) {
		eprintk("kgdbts: ERROR: put buffer overflow on"
			   " '%s' line %i\n", ts.name, ts.idx);
		put_buf_cnt = 0;
		return 0;
	}
	/* Ignore everything until the first valid packet start '$' */
	if (put_buf_cnt == 0 && chr != '$')
		return 0;

	put_buf[put_buf_cnt] = chr;
	put_buf_cnt++;

	/* End of packet == #XX so look for the '#' */
	if (put_buf_cnt > 3 && put_buf[put_buf_cnt - 3] == '#') {
		if (put_buf_cnt >= BUFMAX) {
			eprintk("kgdbts: ERROR: put buffer overflow on"
				" '%s' line %i\n", ts.name, ts.idx);
			put_buf_cnt = 0;
			return 0;
		}
		put_buf[put_buf_cnt] = '\0';
		v2printk("put%i: %s\n", ts.idx, put_buf);
		/* Trigger check here */
		if (ts.validate_put && ts.validate_put(put_buf)) {
			eprintk("kgdbts: ERROR PUT: end of test "
			   "buffer on '%s' line %i expected %s got %s\n",
			   ts.name, ts.idx, ts.tst[ts.idx].put, put_buf);
		}
		ts.idx++;
		put_buf_cnt = 0;
		get_buf_cnt = 0;
		send_ack = 1;
	}
	return 0;
}

static void init_simple_test(void)
{
	memset(&ts, 0, sizeof(ts));
	ts.run_test = run_simple_test;
	ts.validate_put = validate_simple_test;
}

static void run_plant_and_detach_test(int is_early)
{
	char before[BREAK_INSTR_SIZE];
	char after[BREAK_INSTR_SIZE];

	copy_from_kernel_nofault(before, (char *)kgdbts_break_test,
	  BREAK_INSTR_SIZE);
	init_simple_test();
	ts.tst = plant_and_detach_test;
	ts.name = "plant_and_detach_test";
	/* Activate test with initial breakpoint */
	if (!is_early)
		kgdb_breakpoint();
	copy_from_kernel_nofault(after, (char *)kgdbts_break_test,
			BREAK_INSTR_SIZE);
	if (memcmp(before, after, BREAK_INSTR_SIZE)) {
		printk(KERN_CRIT "kgdbts: ERROR kgdb corrupted memory\n");
		panic("kgdb memory corruption");
	}

	/* complete the detach test */
	if (!is_early)
		kgdbts_break_test();
}

static void run_breakpoint_test(int is_hw_breakpoint)
{
	test_complete = 0;
	init_simple_test();
	if (is_hw_breakpoint) {
		ts.tst = hw_breakpoint_test;
		ts.name = "hw_breakpoint_test";
	} else {
		ts.tst = sw_breakpoint_test;
		ts.name = "sw_breakpoint_test";
	}
	/* Activate test with initial breakpoint */
	kgdb_breakpoint();
	/* run code with the break point in it */
	kgdbts_break_test();
	kgdb_breakpoint();

	if (test_complete)
		return;

	eprintk("kgdbts: ERROR %s test failed\n", ts.name);
	if (is_hw_breakpoint)
		hwbreaks_ok = 0;
}

static void run_hw_break_test(int is_write_test)
{
	test_complete = 0;
	init_simple_test();
	if (is_write_test) {
		ts.tst = hw_write_break_test;
		ts.name = "hw_write_break_test";
	} else {
		ts.tst = hw_access_break_test;
		ts.name = "hw_access_break_test";
	}
	/* Activate test with initial breakpoint */
	kgdb_breakpoint();
	hw_break_val_access();
	if (is_write_test) {
		if (test_complete == 2) {
			eprintk("kgdbts: ERROR %s broke on access\n",
				ts.name);
			hwbreaks_ok = 0;
		}
		hw_break_val_write();
	}
	kgdb_breakpoint();

	if (test_complete == 1)
		return;

	eprintk("kgdbts: ERROR %s test failed\n", ts.name);
	hwbreaks_ok = 0;
}

static void run_nmi_sleep_test(int nmi_sleep)
{
	unsigned long flags;

	init_simple_test();
	ts.tst = nmi_sleep_test;
	ts.name = "nmi_sleep_test";
	/* Activate test with initial breakpoint */
	kgdb_breakpoint();
	local_irq_save(flags);
	mdelay(nmi_sleep*1000);
	touch_nmi_watchdog();
	local_irq_restore(flags);
	if (test_complete != 2)
		eprintk("kgdbts: ERROR nmi_test did not hit nmi\n");
	kgdb_breakpoint();
	if (test_complete == 1)
		return;

	eprintk("kgdbts: ERROR %s test failed\n", ts.name);
}

static void run_bad_read_test(void)
{
	init_simple_test();
	ts.tst = bad_read_test;
	ts.name = "bad_read_test";
	/* Activate test with initial breakpoint */
	kgdb_breakpoint();
}

static void run_kernel_clone_test(void)
{
	init_simple_test();
	ts.tst = do_kernel_clone_test;
	ts.name = "do_kernel_clone_test";
	/* Activate test with initial breakpoint */
	kgdb_breakpoint();
}

static void run_sys_open_test(void)
{
	init_simple_test();
	ts.tst = sys_open_test;
	ts.name = "sys_open_test";
	/* Activate test with initial breakpoint */
	kgdb_breakpoint();
}

static void run_singlestep_break_test(void)
{
	init_simple_test();
	ts.tst = singlestep_break_test;
	ts.name = "singlestep_breakpoint_test";
	/* Activate test with initial breakpoint */
	kgdb_breakpoint();
	kgdbts_break_test();
	kgdbts_break_test();
}

static void kgdbts_run_tests(void)
{
	char *ptr;
	int clone_test = 0;
	int do_sys_open_test = 0;
	int sstep_test = 1000;
	int nmi_sleep = 0;
	int i;

	verbose = 0;
	if (strstr(config, "V1"))
		verbose = 1;
	if (strstr(config, "V2"))
		verbose = 2;

	ptr = strchr(config, 'F');
	if (ptr)
		clone_test = simple_strtol(ptr + 1, NULL, 10);
	ptr = strchr(config, 'S');
	if (ptr)
		do_sys_open_test = simple_strtol(ptr + 1, NULL, 10);
	ptr = strchr(config, 'N');
	if (ptr)
		nmi_sleep = simple_strtol(ptr+1, NULL, 10);
	ptr = strchr(config, 'I');
	if (ptr)
		sstep_test = simple_strtol(ptr+1, NULL, 10);

	/* All HW break point tests */
	if (arch_kgdb_ops.flags & KGDB_HW_BREAKPOINT) {
		hwbreaks_ok = 1;
		v1printk("kgdbts:RUN hw breakpoint test\n");
		run_breakpoint_test(1);
		v1printk("kgdbts:RUN hw write breakpoint test\n");
		run_hw_break_test(1);
		v1printk("kgdbts:RUN access write breakpoint test\n");
		run_hw_break_test(0);
	}

	/* required internal KGDB tests */
	v1printk("kgdbts:RUN plant and detach test\n");
	run_plant_and_detach_test(0);
	v1printk("kgdbts:RUN sw breakpoint test\n");
	run_breakpoint_test(0);
	v1printk("kgdbts:RUN bad memory access test\n");
	run_bad_read_test();
	v1printk("kgdbts:RUN singlestep test %i iterations\n", sstep_test);
	for (i = 0; i < sstep_test; i++) {
		run_singlestep_break_test();
		if (i % 100 == 0)
			v1printk("kgdbts:RUN singlestep [%i/%i]\n",
				 i, sstep_test);
	}

	/* ===Optional tests=== */

	if (nmi_sleep) {
		v1printk("kgdbts:RUN NMI sleep %i seconds test\n", nmi_sleep);
		run_nmi_sleep_test(nmi_sleep);
	}

	/* If the kernel_clone test is run it will be the last test that is
	 * executed because a kernel thread will be spawned at the very
	 * end to unregister the debug hooks.
	 */
	if (clone_test) {
		repeat_test = clone_test;
		printk(KERN_INFO "kgdbts:RUN kernel_clone for %i breakpoints\n",
			repeat_test);
		kthread_run(kgdbts_unreg_thread, NULL, "kgdbts_unreg");
		run_kernel_clone_test();
		return;
	}

	/* If the sys_open test is run it will be the last test that is
	 * executed because a kernel thread will be spawned at the very
	 * end to unregister the debug hooks.
	 */
	if (do_sys_open_test) {
		repeat_test = do_sys_open_test;
		printk(KERN_INFO "kgdbts:RUN sys_open for %i breakpoints\n",
			repeat_test);
		kthread_run(kgdbts_unreg_thread, NULL, "kgdbts_unreg");
		run_sys_open_test();
		return;
	}
	/* Shutdown and unregister */
	kgdb_unregister_io_module(&kgdbts_io_ops);
	configured = 0;
}

static int kgdbts_option_setup(char *opt)
{
	if (strlen(opt) >= MAX_CONFIG_LEN) {
		printk(KERN_ERR "kgdbts: config string too long\n");
		return -ENOSPC;
	}
	strcpy(config, opt);
	return 0;
}

__setup("kgdbts=", kgdbts_option_setup);

static int configure_kgdbts(void)
{
	int err = 0;

	if (!strlen(config) || isspace(config[0]))
		goto noconfig;

	final_ack = 0;
	run_plant_and_detach_test(1);

	err = kgdb_register_io_module(&kgdbts_io_ops);
	if (err) {
		configured = 0;
		return err;
	}
	configured = 1;
	kgdbts_run_tests();

	return err;

noconfig:
	config[0] = 0;
	configured = 0;

	return err;
}

static int __init init_kgdbts(void)
{
	/* Already configured? */
	if (configured == 1)
		return 0;

	return configure_kgdbts();
}
device_initcall(init_kgdbts);

static int kgdbts_get_char(void)
{
	int val = 0;

	if (ts.run_test)
		val = ts.run_test(1, 0);

	return val;
}

static void kgdbts_put_char(u8 chr)
{
	if (ts.run_test)
		ts.run_test(0, chr);
}

static int param_set_kgdbts_var(const char *kmessage,
				const struct kernel_param *kp)
{
	size_t len = strlen(kmessage);

	if (len >= MAX_CONFIG_LEN) {
		printk(KERN_ERR "kgdbts: config string too long\n");
		return -ENOSPC;
	}

	/* Only copy in the string if the init function has not run yet */
	if (configured < 0) {
		strcpy(config, kmessage);
		return 0;
	}

	if (configured == 1) {
		printk(KERN_ERR "kgdbts: ERROR: Already configured and running.\n");
		return -EBUSY;
	}

	strcpy(config, kmessage);
	/* Chop out \n char as a result of echo */
	if (len && config[len - 1] == '\n')
		config[len - 1] = '\0';

	/* Go and configure with the new params. */
	return configure_kgdbts();
}

static void kgdbts_pre_exp_handler(void)
{
	/* Increment the module count when the debugger is active */
	if (!kgdb_connected)
		try_module_get(THIS_MODULE);
}

static void kgdbts_post_exp_handler(void)
{
	/* decrement the module count when the debugger detaches */
	if (!kgdb_connected)
		module_put(THIS_MODULE);
}

static struct kgdb_io kgdbts_io_ops = {
	.name			= "kgdbts",
	.read_char		= kgdbts_get_char,
	.write_char		= kgdbts_put_char,
	.pre_exception		= kgdbts_pre_exp_handler,
	.post_exception		= kgdbts_post_exp_handler,
};

/*
 * not really modular, but the easiest way to keep compat with existing
 * bootargs behaviour is to continue using module_param here.
 */
module_param_call(kgdbts, param_set_kgdbts_var, param_get_string, &kps, 0644);
MODULE_PARM_DESC(kgdbts, "<A|V1|V2>[F#|S#][N#]");

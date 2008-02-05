/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include "kern_constants.h"
#include "os.h"
#include "task.h"
#include "user.h"
#include "sysdep/archsetjmp.h"

#define MAXTOKEN 64

/* Set during early boot */
int host_has_cmov = 1;
static jmp_buf cmov_test_return;

static void cmov_sigill_test_handler(int sig)
{
	host_has_cmov = 0;
	longjmp(cmov_test_return, 1);
}

static void test_for_host_cmov(void)
{
	struct sigaction old, new;

	printk(UM_KERN_INFO "Checking for host processor cmov support...");
	new.sa_handler = cmov_sigill_test_handler;

	/* Make sure that SIGILL is enabled after the handler longjmps back */
	new.sa_flags = SA_NODEFER;
	sigemptyset(&new.sa_mask);
	sigaction(SIGILL, &new, &old);

	if (setjmp(cmov_test_return) == 0) {
		unsigned long foo = 0;
		__asm__ __volatile__("cmovz %0, %1" : "=r" (foo) : "0" (foo));
		printk(UM_KERN_CONT "Yes\n");
	} else
		printk(UM_KERN_CONT "No\n");

	sigaction(SIGILL, &old, &new);
}

static char token(int fd, char *buf, int len, char stop)
{
	int n;
	char *ptr, *end, c;

	ptr = buf;
	end = &buf[len];
	do {
		n = os_read_file(fd, ptr, sizeof(*ptr));
		c = *ptr++;
		if (n != sizeof(*ptr)) {
			if (n == 0)
				return 0;
			printk(UM_KERN_ERR "Reading /proc/cpuinfo failed, "
			       "err = %d\n", -n);
			if (n < 0)
				return n;
			else return -EIO;
		}
	} while ((c != '\n') && (c != stop) && (ptr < end));

	if (ptr == end) {
		printk(UM_KERN_ERR "Failed to find '%c' in /proc/cpuinfo\n",
		       stop);
		return -1;
	}
	*(ptr - 1) = '\0';
	return c;
}

static int find_cpuinfo_line(int fd, char *key, char *scratch, int len)
{
	int n;
	char c;

	scratch[len - 1] = '\0';
	while (1) {
		c = token(fd, scratch, len - 1, ':');
		if (c <= 0)
			return 0;
		else if (c != ':') {
			printk(UM_KERN_ERR "Failed to find ':' in "
			       "/proc/cpuinfo\n");
			return 0;
		}

		if (!strncmp(scratch, key, strlen(key)))
			return 1;

		do {
			n = os_read_file(fd, &c, sizeof(c));
			if (n != sizeof(c)) {
				printk(UM_KERN_ERR "Failed to find newline in "
				       "/proc/cpuinfo, err = %d\n", -n);
				return 0;
			}
		} while (c != '\n');
	}
	return 0;
}

static int check_cpu_flag(char *feature, int *have_it)
{
	char buf[MAXTOKEN], c;
	int fd, len = ARRAY_SIZE(buf);

	printk(UM_KERN_INFO "Checking for host processor %s support...",
	       feature);
	fd = os_open_file("/proc/cpuinfo", of_read(OPENFLAGS()), 0);
	if (fd < 0) {
		printk(UM_KERN_ERR "Couldn't open /proc/cpuinfo, err = %d\n",
		       -fd);
		return 0;
	}

	*have_it = 0;
	if (!find_cpuinfo_line(fd, "flags", buf, ARRAY_SIZE(buf)))
		goto out;

	c = token(fd, buf, len - 1, ' ');
	if (c < 0)
		goto out;
	else if (c != ' ') {
		printk(UM_KERN_ERR "Failed to find ' ' in /proc/cpuinfo\n");
		goto out;
	}

	while (1) {
		c = token(fd, buf, len - 1, ' ');
		if (c < 0)
			goto out;
		else if (c == '\n')
			break;

		if (!strcmp(buf, feature)) {
			*have_it = 1;
			goto out;
		}
	}
 out:
	if (*have_it == 0)
		printk("No\n");
	else if (*have_it == 1)
		printk("Yes\n");
	os_close_file(fd);
	return 1;
}

#if 0 /*
       * This doesn't work in tt mode, plus it's causing compilation problems
       * for some people.
       */
static void disable_lcall(void)
{
	struct modify_ldt_ldt_s ldt;
	int err;

	bzero(&ldt, sizeof(ldt));
	ldt.entry_number = 7;
	ldt.base_addr = 0;
	ldt.limit = 0;
	err = modify_ldt(1, &ldt, sizeof(ldt));
	if (err)
		printk(UM_KERN_ERR "Failed to disable lcall7 - errno = %d\n",
		       errno);
}
#endif

void arch_init_thread(void)
{
#if 0
	disable_lcall();
#endif
}

void arch_check_bugs(void)
{
	test_for_host_cmov();
}

int arch_handle_signal(int sig, struct uml_pt_regs *regs)
{
	unsigned char tmp[2];

	/*
	 * This is testing for a cmov (0x0f 0x4x) instruction causing a
	 * SIGILL in init.
	 */
	if ((sig != SIGILL) || (TASK_PID(get_current()) != 1))
		return 0;

	if (copy_from_user_proc(tmp, (void *) UPT_IP(regs), 2))
		panic("SIGILL in init, could not read instructions!\n");
	if ((tmp[0] != 0x0f) || ((tmp[1] & 0xf0) != 0x40))
		return 0;

	if (host_has_cmov == 0)
		panic("SIGILL caused by cmov, which this processor doesn't "
		      "implement, boot a filesystem compiled for older "
		      "processors");
	else if (host_has_cmov == 1)
		panic("SIGILL caused by cmov, which this processor claims to "
		      "implement");
	else if (host_has_cmov == -1)
		panic("SIGILL caused by cmov, couldn't tell if this processor "
		      "implements it, boot a filesystem compiled for older "
		      "processors");
	else panic("Bad value for host_has_cmov (%d)", host_has_cmov);
	return 0;
}

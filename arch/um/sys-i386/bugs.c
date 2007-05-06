/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/signal.h>
#include <asm/ldt.h>
#include "kern_util.h"
#include "user.h"
#include "sysdep/ptrace.h"
#include "task.h"
#include "os.h"
#include "user_util.h"

#define MAXTOKEN 64

/* Set during early boot */
int host_has_cmov = 1;
int host_has_xmm = 0;

static char token(int fd, char *buf, int len, char stop)
{
	int n;
	char *ptr, *end, c;

	ptr = buf;
	end = &buf[len];
	do {
		n = os_read_file(fd, ptr, sizeof(*ptr));
		c = *ptr++;
		if(n != sizeof(*ptr)){
			if(n == 0) return(0);
			printk("Reading /proc/cpuinfo failed, err = %d\n", -n);
			if(n < 0)
				return(n);
			else
				return(-EIO);
		}
	} while((c != '\n') && (c != stop) && (ptr < end));

	if(ptr == end){
		printk("Failed to find '%c' in /proc/cpuinfo\n", stop);
		return(-1);
	}
	*(ptr - 1) = '\0';
	return(c);
}

static int find_cpuinfo_line(int fd, char *key, char *scratch, int len)
{
	int n;
	char c;

	scratch[len - 1] = '\0';
	while(1){
		c = token(fd, scratch, len - 1, ':');
		if(c <= 0)
			return(0);
		else if(c != ':'){
			printk("Failed to find ':' in /proc/cpuinfo\n");
			return(0);
		}

		if(!strncmp(scratch, key, strlen(key)))
			return(1);

		do {
			n = os_read_file(fd, &c, sizeof(c));
			if(n != sizeof(c)){
				printk("Failed to find newline in "
				       "/proc/cpuinfo, err = %d\n", -n);
				return(0);
			}
		} while(c != '\n');
	}
	return(0);
}

static int check_cpu_flag(char *feature, int *have_it)
{
	char buf[MAXTOKEN], c;
	int fd, len = ARRAY_SIZE(buf);

	printk("Checking for host processor %s support...", feature);
	fd = os_open_file("/proc/cpuinfo", of_read(OPENFLAGS()), 0);
	if(fd < 0){
		printk("Couldn't open /proc/cpuinfo, err = %d\n", -fd);
		return 0;
	}

	*have_it = 0;
	if(!find_cpuinfo_line(fd, "flags", buf, ARRAY_SIZE(buf)))
		goto out;

	c = token(fd, buf, len - 1, ' ');
	if(c < 0) goto out;
	else if(c != ' '){
		printk("Failed to find ' ' in /proc/cpuinfo\n");
		goto out;
	}

	while(1){
		c = token(fd, buf, len - 1, ' ');
		if(c < 0) goto out;
		else if(c == '\n') break;

		if(!strcmp(buf, feature)){
			*have_it = 1;
			goto out;
		}
	}
 out:
	if(*have_it == 0) printk("No\n");
	else if(*have_it == 1) printk("Yes\n");
	os_close_file(fd);
	return 1;
}

#if 0 /* This doesn't work in tt mode, plus it's causing compilation problems
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
	if(err)
		printk("Failed to disable lcall7 - errno = %d\n", errno);
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
	int have_it;

	if(os_access("/proc/cpuinfo", OS_ACC_R_OK) < 0){
		printk("/proc/cpuinfo not available - skipping CPU capability "
		       "checks\n");
		return;
	}
	if(check_cpu_flag("cmov", &have_it))
		host_has_cmov = have_it;
	if(check_cpu_flag("xmm", &have_it))
		host_has_xmm = have_it;
}

int arch_handle_signal(int sig, union uml_pt_regs *regs)
{
	unsigned char tmp[2];

	/* This is testing for a cmov (0x0f 0x4x) instruction causing a
	 * SIGILL in init.
	 */
	if((sig != SIGILL) || (TASK_PID(get_current()) != 1)) return(0);

	if (copy_from_user_proc(tmp, (void *) UPT_IP(regs), 2))
		panic("SIGILL in init, could not read instructions!\n");
	if((tmp[0] != 0x0f) || ((tmp[1] & 0xf0) != 0x40))
		return(0);

	if(host_has_cmov == 0)
		panic("SIGILL caused by cmov, which this processor doesn't "
		      "implement, boot a filesystem compiled for older "
		      "processors");
	else if(host_has_cmov == 1)
		panic("SIGILL caused by cmov, which this processor claims to "
		      "implement");
	else if(host_has_cmov == -1)
		panic("SIGILL caused by cmov, couldn't tell if this processor "
		      "implements it, boot a filesystem compiled for older "
		      "processors");
	else panic("Bad value for host_has_cmov (%d)", host_has_cmov);
	return(0);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */

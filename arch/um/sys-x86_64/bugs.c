/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/errno.h"
#include "asm/system.h"
#include "asm/pda.h"
#include "sysdep/ptrace.h"
#include "os.h"

void arch_init_thread(void)
{
}

void arch_check_bugs(void)
{
}

int arch_handle_signal(int sig, union uml_pt_regs *regs)
{
	return(0);
}

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

int cpu_feature(char *what, char *buf, int len)
{
	int fd, ret = 0;

	fd = os_open_file("/proc/cpuinfo", of_read(OPENFLAGS()), 0);
	if(fd < 0){
		printk("Couldn't open /proc/cpuinfo, err = %d\n", -fd);
		return(0);
	}

	if(!find_cpuinfo_line(fd, what, buf, len)){
		printk("Couldn't find '%s' line in /proc/cpuinfo\n", what);
		goto out_close;
	}

	token(fd, buf, len, '\n');
	ret = 1;

 out_close:
	os_close_file(fd);
	return(ret);
}

/* Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */

/* 
 * Copyright (C) 2002 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include "ptrace_user.h"
#include "os.h"
#include "user.h"
#include "process.h"
#include "irq_user.h"
#include "kern_util.h"
#include "longjmp.h"
#include "skas_ptrace.h"
#include "kern_constants.h"
#include "uml-config.h"
#include "init.h"

#define ARBITRARY_ADDR -1
#define FAILURE_PID    -1

#define STAT_PATH_LEN sizeof("/proc/#######/stat\0")
#define COMM_SCANF "%*[^)])"

unsigned long os_process_pc(int pid)
{
	char proc_stat[STAT_PATH_LEN], buf[256];
	unsigned long pc;
	int fd, err;

	sprintf(proc_stat, "/proc/%d/stat", pid);
	fd = os_open_file(proc_stat, of_read(OPENFLAGS()), 0);
	if(fd < 0){
		printk("os_process_pc - couldn't open '%s', err = %d\n",
		       proc_stat, -fd);
		return ARBITRARY_ADDR;
	}
	CATCH_EINTR(err = read(fd, buf, sizeof(buf)));
	if(err < 0){
		printk("os_process_pc - couldn't read '%s', err = %d\n",
		       proc_stat, errno);
		os_close_file(fd);
		return ARBITRARY_ADDR;
	}
	os_close_file(fd);
	pc = ARBITRARY_ADDR;
	if(sscanf(buf, "%*d " COMM_SCANF " %*c %*d %*d %*d %*d %*d %*d %*d "
		  "%*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d "
		  "%*d %*d %*d %*d %*d %lu", &pc) != 1){
		printk("os_process_pc - couldn't find pc in '%s'\n", buf);
	}
	return pc;
}

int os_process_parent(int pid)
{
	char stat[STAT_PATH_LEN];
	char data[256];
	int parent, n, fd;

	if(pid == -1)
		return -1;

	snprintf(stat, sizeof(stat), "/proc/%d/stat", pid);
	fd = os_open_file(stat, of_read(OPENFLAGS()), 0);
	if(fd < 0){
		printk("Couldn't open '%s', err = %d\n", stat, -fd);
		return FAILURE_PID;
	}

	CATCH_EINTR(n = read(fd, data, sizeof(data)));
	os_close_file(fd);

	if(n < 0){
		printk("Couldn't read '%s', err = %d\n", stat, errno);
		return FAILURE_PID;
	}

	parent = FAILURE_PID;
	n = sscanf(data, "%*d " COMM_SCANF " %*c %d", &parent);
	if(n != 1)
		printk("Failed to scan '%s'\n", data);

	return parent;
}

void os_stop_process(int pid)
{
	kill(pid, SIGSTOP);
}

void os_kill_process(int pid, int reap_child)
{
	kill(pid, SIGKILL);
	if(reap_child)
		CATCH_EINTR(waitpid(pid, NULL, 0));
		
}

/* This is here uniquely to have access to the userspace errno, i.e. the one
 * used by ptrace in case of error.
 */

long os_ptrace_ldt(long pid, long addr, long data)
{
	int ret;

	ret = ptrace(PTRACE_LDT, pid, addr, data);

	if (ret < 0)
		return -errno;
	return ret;
}

/* Kill off a ptraced child by all means available.  kill it normally first,
 * then PTRACE_KILL it, then PTRACE_CONT it in case it's in a run state from
 * which it can't exit directly.
 */

void os_kill_ptraced_process(int pid, int reap_child)
{
	kill(pid, SIGKILL);
	ptrace(PTRACE_KILL, pid);
	ptrace(PTRACE_CONT, pid);
	if(reap_child)
		CATCH_EINTR(waitpid(pid, NULL, 0));
}

/* Don't use the glibc version, which caches the result in TLS. It misses some
 * syscalls, and also breaks with clone(), which does not unshare the TLS.
 */

int os_getpid(void)
{
	return syscall(__NR_getpid);
}

int os_getpgrp(void)
{
	return getpgrp();
}

int os_map_memory(void *virt, int fd, unsigned long long off, unsigned long len,
		  int r, int w, int x)
{
	void *loc;
	int prot;

	prot = (r ? PROT_READ : 0) | (w ? PROT_WRITE : 0) | 
		(x ? PROT_EXEC : 0);

	loc = mmap64((void *) virt, len, prot, MAP_SHARED | MAP_FIXED,
		     fd, off);
	if(loc == MAP_FAILED)
		return -errno;
	return 0;
}

int os_protect_memory(void *addr, unsigned long len, int r, int w, int x)
{
        int prot = ((r ? PROT_READ : 0) | (w ? PROT_WRITE : 0) | 
		    (x ? PROT_EXEC : 0));

        if(mprotect(addr, len, prot) < 0)
		return -errno;
        return 0;
}

int os_unmap_memory(void *addr, int len)
{
        int err;

        err = munmap(addr, len);
	if(err < 0)
		return -errno;
        return 0;
}

#ifndef MADV_REMOVE
#define MADV_REMOVE KERNEL_MADV_REMOVE
#endif

int os_drop_memory(void *addr, int length)
{
	int err;

	err = madvise(addr, length, MADV_REMOVE);
	if(err < 0)
		err = -errno;
	return err;
}

int __init can_drop_memory(void)
{
	void *addr;
	int fd, ok = 0;

	printk("Checking host MADV_REMOVE support...");
	fd = create_mem_file(UM_KERN_PAGE_SIZE);
	if(fd < 0){
		printk("Creating test memory file failed, err = %d\n", -fd);
		goto out;
	}

	addr = mmap64(NULL, UM_KERN_PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_SHARED, fd, 0);
	if(addr == MAP_FAILED){
		printk("Mapping test memory file failed, err = %d\n", -errno);
		goto out_close;
	}

	if(madvise(addr, UM_KERN_PAGE_SIZE, MADV_REMOVE) != 0){
		printk("MADV_REMOVE failed, err = %d\n", -errno);
		goto out_unmap;
	}

	printk("OK\n");
	ok = 1;

out_unmap:
	munmap(addr, UM_KERN_PAGE_SIZE);
out_close:
	close(fd);
out:
	return ok;
}

void init_new_thread_signals(void)
{
	set_handler(SIGSEGV, (__sighandler_t) sig_handler, SA_ONSTACK,
		    SIGUSR1, SIGIO, SIGWINCH, SIGALRM, SIGVTALRM, -1);
	set_handler(SIGTRAP, (__sighandler_t) sig_handler, SA_ONSTACK,
		    SIGUSR1, SIGIO, SIGWINCH, SIGALRM, SIGVTALRM, -1);
	set_handler(SIGFPE, (__sighandler_t) sig_handler, SA_ONSTACK,
		    SIGUSR1, SIGIO, SIGWINCH, SIGALRM, SIGVTALRM, -1);
	set_handler(SIGILL, (__sighandler_t) sig_handler, SA_ONSTACK,
		    SIGUSR1, SIGIO, SIGWINCH, SIGALRM, SIGVTALRM, -1);
	set_handler(SIGBUS, (__sighandler_t) sig_handler, SA_ONSTACK,
		    SIGUSR1, SIGIO, SIGWINCH, SIGALRM, SIGVTALRM, -1);
	set_handler(SIGUSR2, (__sighandler_t) sig_handler,
		    SA_ONSTACK, SIGUSR1, SIGIO, SIGWINCH, SIGALRM, SIGVTALRM,
		    -1);
	signal(SIGHUP, SIG_IGN);

	init_irq_signals(1);
}

int run_kernel_thread(int (*fn)(void *), void *arg, void **jmp_ptr)
{
	jmp_buf buf;
	int n;

	*jmp_ptr = &buf;
	n = UML_SETJMP(&buf);
	if(n != 0)
		return n;
	(*fn)(arg);
	return 0;
}

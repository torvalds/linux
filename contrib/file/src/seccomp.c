/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * libseccomp hooks.
 */
#include "file.h"

#ifndef	lint
FILE_RCSID("@(#)$File: seccomp.c,v 1.6 2018/06/26 20:29:29 christos Exp $")
#endif	/* lint */

#if HAVE_LIBSECCOMP
#include <seccomp.h> /* libseccomp */
#include <sys/prctl.h> /* prctl */
#include <sys/socket.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>

#define DENY_RULE(call) \
    do \
	if (seccomp_rule_add (ctx, SCMP_ACT_KILL, SCMP_SYS(call), 0) == -1) \
	    goto out; \
    while (/*CONSTCOND*/0)
#define ALLOW_RULE(call) \
    do \
	if (seccomp_rule_add (ctx, SCMP_ACT_ALLOW, SCMP_SYS(call), 0) == -1) \
	    goto out; \
    while (/*CONSTCOND*/0)

static scmp_filter_ctx ctx;


int
enable_sandbox_basic(void)
{

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1)
		return -1;

	if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) == -1)
		return -1;

	// initialize the filter
	ctx = seccomp_init(SCMP_ACT_ALLOW);
	if (ctx == NULL)
	    return 1;

	DENY_RULE(_sysctl);
	DENY_RULE(acct);
	DENY_RULE(add_key);
	DENY_RULE(adjtimex);
	DENY_RULE(chroot);
	DENY_RULE(clock_adjtime);
	DENY_RULE(create_module);
	DENY_RULE(delete_module);
	DENY_RULE(fanotify_init);
	DENY_RULE(finit_module);
	DENY_RULE(get_kernel_syms);
	DENY_RULE(get_mempolicy);
	DENY_RULE(init_module);
	DENY_RULE(io_cancel);
	DENY_RULE(io_destroy);
	DENY_RULE(io_getevents);
	DENY_RULE(io_setup);
	DENY_RULE(io_submit);
	DENY_RULE(ioperm);
	DENY_RULE(iopl);
	DENY_RULE(ioprio_set);
	DENY_RULE(kcmp);
#ifdef __NR_kexec_file_load
	DENY_RULE(kexec_file_load);
#endif
	DENY_RULE(kexec_load);
	DENY_RULE(keyctl);
	DENY_RULE(lookup_dcookie);
	DENY_RULE(mbind);
	DENY_RULE(nfsservctl);
	DENY_RULE(migrate_pages);
	DENY_RULE(modify_ldt);
	DENY_RULE(mount);
	DENY_RULE(move_pages);
	DENY_RULE(name_to_handle_at);
	DENY_RULE(open_by_handle_at);
	DENY_RULE(perf_event_open);
	DENY_RULE(pivot_root);
	DENY_RULE(process_vm_readv);
	DENY_RULE(process_vm_writev);
	DENY_RULE(ptrace);
	DENY_RULE(reboot);
	DENY_RULE(remap_file_pages);
	DENY_RULE(request_key);
	DENY_RULE(set_mempolicy);
	DENY_RULE(swapoff);
	DENY_RULE(swapon);
	DENY_RULE(sysfs);
	DENY_RULE(syslog);
	DENY_RULE(tuxcall);
	DENY_RULE(umount2);
	DENY_RULE(uselib);
	DENY_RULE(vmsplice);

	// blocking dangerous syscalls that file should not need
	DENY_RULE (execve);
	DENY_RULE (socket);
	// ...

	
	// applying filter...
	if (seccomp_load (ctx) == -1)
		goto out;
	// free ctx after the filter has been loaded into the kernel
	seccomp_release(ctx);
	return 0;
	
out:
	seccomp_release(ctx);
	return -1;
}


int
enable_sandbox_full(void)
{

	// prevent child processes from getting more priv e.g. via setuid,
	// capabilities, ...
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1)
		return -1;

	if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) == -1)
		return -1;
	
	// initialize the filter
	ctx = seccomp_init(SCMP_ACT_KILL);
	if (ctx == NULL)
		return -1;

	ALLOW_RULE(access);
	ALLOW_RULE(brk);
	ALLOW_RULE(close);
	ALLOW_RULE(dup2);
	ALLOW_RULE(exit);
	ALLOW_RULE(exit_group);
	ALLOW_RULE(fcntl);  
 	ALLOW_RULE(fcntl64);  
	ALLOW_RULE(fstat);
 	ALLOW_RULE(fstat64);  
	ALLOW_RULE(getdents);
#ifdef __NR_getdents64
	ALLOW_RULE(getdents64);
#endif
	ALLOW_RULE(ioctl);
	ALLOW_RULE(lseek);
 	ALLOW_RULE(_llseek);
	ALLOW_RULE(lstat);
 	ALLOW_RULE(lstat64);
	ALLOW_RULE(mmap);
 	ALLOW_RULE(mmap2);
	ALLOW_RULE(mprotect);
	ALLOW_RULE(mremap);
	ALLOW_RULE(munmap);
#ifdef __NR_newfstatat
	ALLOW_RULE(newfstatat);
#endif
	ALLOW_RULE(open);
	ALLOW_RULE(openat);
	ALLOW_RULE(pread64);
	ALLOW_RULE(read);
	ALLOW_RULE(readlink);
	ALLOW_RULE(rt_sigaction);
	ALLOW_RULE(rt_sigprocmask);
	ALLOW_RULE(rt_sigreturn);
	ALLOW_RULE(select);
	ALLOW_RULE(stat);
	ALLOW_RULE(stat64);
	ALLOW_RULE(sysinfo);
	ALLOW_RULE(unlink);
	ALLOW_RULE(write);


#if 0
	// needed by valgrind
	ALLOW_RULE(gettid);
	ALLOW_RULE(getpid);
	ALLOW_RULE(rt_sigtimedwait);
#endif

#if 0
	 /* special restrictions for socket, only allow AF_UNIX/AF_LOCAL */
	 if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socket), 1,
	     SCMP_CMP(0, SCMP_CMP_EQ, AF_UNIX)) == -1)
	 	goto out;

	 if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socket), 1,
	     SCMP_CMP(0, SCMP_CMP_EQ, AF_LOCAL)) == -1)
	 	goto out;


	 /* special restrictions for open, prevent opening files for writing */
	 if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(open), 1,
	     SCMP_CMP(1, SCMP_CMP_MASKED_EQ, O_WRONLY | O_RDWR, 0)) == -1)
	 	goto out;

	 if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EACCES), SCMP_SYS(open), 1,
	     SCMP_CMP(1, SCMP_CMP_MASKED_EQ, O_WRONLY, O_WRONLY)) == -1)
	 	goto out;

	 if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EACCES), SCMP_SYS(open), 1,
	     SCMP_CMP(1, SCMP_CMP_MASKED_EQ, O_RDWR, O_RDWR)) == -1)
	 	goto out;


	 /* allow stderr */
	 if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 1,
	     SCMP_CMP(0, SCMP_CMP_EQ, 2)) == -1)
		 goto out;
#endif

	// applying filter...
	if (seccomp_load(ctx) == -1)
		goto out;
	// free ctx after the filter has been loaded into the kernel
	seccomp_release(ctx);
	return 0;

out:
	// something went wrong
	seccomp_release(ctx);
	return -1;
}
#endif

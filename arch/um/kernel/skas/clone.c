#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <asm/unistd.h>
#include <asm/page.h>
#include "ptrace_user.h"
#include "skas.h"
#include "stub-data.h"
#include "uml-config.h"
#include "sysdep/stub.h"

/* This is in a separate file because it needs to be compiled with any
 * extraneous gcc flags (-pg, -fprofile-arcs, -ftest-coverage) disabled
 */
void __attribute__ ((__section__ (".__syscall_stub")))
stub_clone_handler(void)
{
	long err;
	struct stub_data *from = (struct stub_data *) UML_CONFIG_STUB_DATA;

	err = stub_syscall2(__NR_clone, CLONE_PARENT | CLONE_FILES | SIGCHLD,
			    UML_CONFIG_STUB_DATA + PAGE_SIZE / 2 -
			    sizeof(void *));
	if(err != 0)
		goto out;

	err = stub_syscall4(__NR_ptrace, PTRACE_TRACEME, 0, 0, 0);
	if(err)
		goto out;

	err = stub_syscall3(__NR_setitimer, ITIMER_VIRTUAL,
			    (long) &from->timer, 0);
	if(err)
		goto out;

	err = stub_syscall6(STUB_MMAP_NR, UML_CONFIG_STUB_DATA, PAGE_SIZE,
			    PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED,
			    from->fd, from->offset);
 out:
	/* save current result. Parent: pid; child: retcode of mmap */
	from->err = err;
	trap_myself();
}

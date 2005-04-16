#include <errno.h>
#include <asm/ptrace.h>
#include "sysdep/ptrace.h"

int ptrace_getregs(long pid, unsigned long *regs_out)
{
    int i;
    for (i=0; i < sizeof(struct sys_pt_regs)/sizeof(PPC_REG); ++i) {
	errno = 0;
	regs_out->regs[i] = ptrace(PTRACE_PEEKUSR, pid, i*4, 0);
	if (errno) {
	    return -errno;
	}
    }
    return 0;
}

int ptrace_setregs(long pid, unsigned long *regs_in)
{
    int i;
    for (i=0; i < sizeof(struct sys_pt_regs)/sizeof(PPC_REG); ++i) {
	if (i != 34 /* FIXME: PT_ORIG_R3 */ && i <= PT_MQ) {
	    if (ptrace(PTRACE_POKEUSR, pid, i*4, regs_in->regs[i]) < 0) {
		return -errno;
	    }
	}
    }
    return 0;
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

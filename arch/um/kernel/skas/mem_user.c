/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <asm/page.h>
#include <asm/unistd.h>
#include "mem_user.h"
#include "mem.h"
#include "mm_id.h"
#include "user.h"
#include "os.h"
#include "proc_mm.h"
#include "ptrace_user.h"
#include "user_util.h"
#include "kern_util.h"
#include "task.h"
#include "registers.h"
#include "uml-config.h"
#include "sysdep/ptrace.h"
#include "sysdep/stub.h"
#include "skas.h"

extern unsigned long syscall_stub, __syscall_stub_start;

extern void wait_stub_done(int pid, int sig, char * fname);

static long run_syscall_stub(struct mm_id * mm_idp, int syscall,
                             unsigned long *args)
{
        int n, pid = mm_idp->u.pid;
        unsigned long regs[MAX_REG_NR];

        get_safe_registers(regs);
        regs[REGS_IP_INDEX] = UML_CONFIG_STUB_CODE +
                ((unsigned long) &syscall_stub -
                 (unsigned long) &__syscall_stub_start);
        /* XXX Don't have a define for starting a syscall */
        regs[REGS_SYSCALL_NR] = syscall;
        regs[REGS_SYSCALL_ARG1] = args[0];
        regs[REGS_SYSCALL_ARG2] = args[1];
        regs[REGS_SYSCALL_ARG3] = args[2];
        regs[REGS_SYSCALL_ARG4] = args[3];
        regs[REGS_SYSCALL_ARG5] = args[4];
        regs[REGS_SYSCALL_ARG6] = args[5];
        n = ptrace_setregs(pid, regs);
        if(n < 0){
                printk("run_syscall_stub : PTRACE_SETREGS failed, "
                       "errno = %d\n", n);
                return(n);
        }

        wait_stub_done(pid, 0, "run_syscall_stub");

        return(*((unsigned long *) mm_idp->stack));
}

int map(struct mm_id *mm_idp, unsigned long virt, unsigned long len,
        int r, int w, int x, int phys_fd, unsigned long long offset)
{
        int prot, n;

        prot = (r ? PROT_READ : 0) | (w ? PROT_WRITE : 0) |
                (x ? PROT_EXEC : 0);

        if(proc_mm){
                struct proc_mm_op map;
                int fd = mm_idp->u.mm_fd;
                map = ((struct proc_mm_op) { .op	= MM_MMAP,
                                             .u		=
                                             { .mmap	=
                                               { .addr	= virt,
                                                 .len	= len,
                                                 .prot	= prot,
                                                 .flags	= MAP_SHARED |
                                                 MAP_FIXED,
                                                 .fd	= phys_fd,
                                                 .offset= offset
                                               } } } );
                n = os_write_file(fd, &map, sizeof(map));
                if(n != sizeof(map))
                        printk("map : /proc/mm map failed, err = %d\n", -n);
        }
        else {
                long res;
                unsigned long args[] = { virt, len, prot,
                                         MAP_SHARED | MAP_FIXED, phys_fd,
                                         MMAP_OFFSET(offset) };

                res = run_syscall_stub(mm_idp, STUB_MMAP_NR, args);
                if((void *) res == MAP_FAILED)
                        printk("mmap stub failed, errno = %d\n", res);
        }

        return 0;
}

int unmap(struct mm_id *mm_idp, void *addr, unsigned long len)
{
        int n;

        if(proc_mm){
                struct proc_mm_op unmap;
                int fd = mm_idp->u.mm_fd;
                unmap = ((struct proc_mm_op) { .op	= MM_MUNMAP,
                                               .u	=
                                               { .munmap	=
                                                 { .addr	=
                                                   (unsigned long) addr,
                                                   .len		= len } } } );
                n = os_write_file(fd, &unmap, sizeof(unmap));
                if(n != sizeof(unmap)) {
                        if(n < 0)
                                return(n);
                        else if(n > 0)
                                return(-EIO);
                }
        }
        else {
                int res;
                unsigned long args[] = { (unsigned long) addr, len, 0, 0, 0,
                                         0 };

                res = run_syscall_stub(mm_idp, __NR_munmap, args);
                if(res < 0)
                        printk("munmap stub failed, errno = %d\n", res);
        }

        return(0);
}

int protect(struct mm_id *mm_idp, unsigned long addr, unsigned long len,
	    int r, int w, int x)
{
        struct proc_mm_op protect;
        int prot, n;

        prot = (r ? PROT_READ : 0) | (w ? PROT_WRITE : 0) |
                (x ? PROT_EXEC : 0);

        if(proc_mm){
                int fd = mm_idp->u.mm_fd;
                protect = ((struct proc_mm_op) { .op	= MM_MPROTECT,
                                                 .u	=
                                                 { .mprotect	=
                                                   { .addr	=
                                                     (unsigned long) addr,
                                                     .len	= len,
                                                     .prot	= prot } } } );

                n = os_write_file(fd, &protect, sizeof(protect));
                if(n != sizeof(protect))
                        panic("protect failed, err = %d", -n);
        }
        else {
                int res;
                unsigned long args[] = { addr, len, prot, 0, 0, 0 };

                res = run_syscall_stub(mm_idp, __NR_mprotect, args);
                if(res < 0)
                        panic("mprotect stub failed, errno = %d\n", res);
        }

        return(0);
}

void before_mem_skas(unsigned long unused)
{
}

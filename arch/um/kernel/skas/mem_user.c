/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <asm/page.h>
#include <asm/unistd.h>
#include "mem_user.h"
#include "mem.h"
#include "skas.h"
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

extern unsigned long batch_syscall_stub, __syscall_stub_start;

extern void wait_stub_done(int pid, int sig, char * fname);

static inline unsigned long *check_init_stack(struct mm_id * mm_idp,
					      unsigned long *stack)
{
	if(stack == NULL){
		stack = (unsigned long *) mm_idp->stack + 2;
		*stack = 0;
	}
	return stack;
}

extern int proc_mm;

int single_count = 0;
int multi_count = 0;
int multi_op_count = 0;

static long do_syscall_stub(struct mm_id *mm_idp, void **addr)
{
	unsigned long regs[MAX_REG_NR];
	unsigned long *data;
	unsigned long *syscall;
	long ret, offset;
        int n, pid = mm_idp->u.pid;

	if(proc_mm)
#warning Need to look up userspace_pid by cpu
		pid = userspace_pid[0];

	multi_count++;

        get_safe_registers(regs);
        regs[REGS_IP_INDEX] = UML_CONFIG_STUB_CODE +
		((unsigned long) &batch_syscall_stub -
                 (unsigned long) &__syscall_stub_start);
	n = ptrace_setregs(pid, regs);
	if(n < 0)
		panic("do_syscall_stub : PTRACE_SETREGS failed, errno = %d\n",
		      n);

	wait_stub_done(pid, 0, "do_syscall_stub");

	/* When the stub stops, we find the following values on the
	 * beginning of the stack:
	 * (long )return_value
	 * (long )offset to failed sycall-data (0, if no error)
	 */
	ret = *((unsigned long *) mm_idp->stack);
	offset = *((unsigned long *) mm_idp->stack + 1);
	if (offset) {
		data = (unsigned long *)(mm_idp->stack +
					 offset - UML_CONFIG_STUB_DATA);
		syscall = (unsigned long *)((unsigned long)data + data[0]);
		printk("do_syscall_stub: syscall %ld failed, return value = "
		       "0x%lx, expected return value = 0x%lx\n",
		       syscall[0], ret, syscall[7]);
		printk("    syscall parameters: "
		       "0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx\n",
		       syscall[1], syscall[2], syscall[3],
		       syscall[4], syscall[5], syscall[6]);
		for(n = 1; n < data[0]/sizeof(long); n++) {
			if(n == 1)
				printk("    additional syscall data:");
			if(n % 4 == 1)
				printk("\n      ");
			printk("  0x%lx", data[n]);
		}
		if(n > 1)
			printk("\n");
	}
	else ret = 0;

	*addr = check_init_stack(mm_idp, NULL);

	return ret;
}

long run_syscall_stub(struct mm_id * mm_idp, int syscall,
		      unsigned long *args, long expected, void **addr,
 		      int done)
{
 	unsigned long *stack = check_init_stack(mm_idp, *addr);

	if(done && *addr == NULL)
		single_count++;

 	*stack += sizeof(long);
	stack += *stack / sizeof(long);

        *stack++ = syscall;
        *stack++ = args[0];
        *stack++ = args[1];
        *stack++ = args[2];
        *stack++ = args[3];
        *stack++ = args[4];
        *stack++ = args[5];
	*stack++ = expected;
        *stack = 0;
        multi_op_count++;

        if(!done && ((((unsigned long) stack) & ~PAGE_MASK) <
		     PAGE_SIZE - 10 * sizeof(long))){
		*addr = stack;
                return 0;
        }

	return do_syscall_stub(mm_idp, addr);
}

long syscall_stub_data(struct mm_id * mm_idp,
		       unsigned long *data, int data_count,
		       void **addr, void **stub_addr)
{
	unsigned long *stack;
	int ret = 0;

	/* If *addr still is uninitialized, it *must* contain NULL.
	 * Thus in this case do_syscall_stub correctly won't be called.
	 */
	if((((unsigned long) *addr) & ~PAGE_MASK) >=
	   PAGE_SIZE - (10 + data_count) * sizeof(long)) {
		ret = do_syscall_stub(mm_idp, addr);
 		/* in case of error, don't overwrite data on stack */
		if(ret)
			return ret;
	}

	stack = check_init_stack(mm_idp, *addr);
	*addr = stack;

	*stack = data_count * sizeof(long);

	memcpy(stack + 1, data, data_count * sizeof(long));

	*stub_addr = (void *)(((unsigned long)(stack + 1) & ~PAGE_MASK) +
			      UML_CONFIG_STUB_DATA);

	return 0;
}

int map(struct mm_id * mm_idp, unsigned long virt, unsigned long len,
	int r, int w, int x, int phys_fd, unsigned long long offset,
	int done, void **data)
{
        int prot, ret;

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
		ret = os_write_file(fd, &map, sizeof(map));
		if(ret != sizeof(map))
			printk("map : /proc/mm map failed, err = %d\n", -ret);
		else ret = 0;
        }
        else {
                unsigned long args[] = { virt, len, prot,
                                         MAP_SHARED | MAP_FIXED, phys_fd,
                                         MMAP_OFFSET(offset) };

		ret = run_syscall_stub(mm_idp, STUB_MMAP_NR, args, virt,
				       data, done);
        }

	return ret;
}

int unmap(struct mm_id * mm_idp, void *addr, unsigned long len, int done,
	  void **data)
{
        int ret;

        if(proc_mm){
                struct proc_mm_op unmap;
                int fd = mm_idp->u.mm_fd;

                unmap = ((struct proc_mm_op) { .op	= MM_MUNMAP,
                                               .u	=
                                               { .munmap	=
                                                 { .addr	=
                                                   (unsigned long) addr,
                                                   .len		= len } } } );
		ret = os_write_file(fd, &unmap, sizeof(unmap));
		if(ret != sizeof(unmap))
			printk("unmap - proc_mm write returned %d\n", ret);
		else ret = 0;
        }
        else {
                unsigned long args[] = { (unsigned long) addr, len, 0, 0, 0,
                                         0 };

		ret = run_syscall_stub(mm_idp, __NR_munmap, args, 0,
				       data, done);
                if(ret < 0)
                        printk("munmap stub failed, errno = %d\n", ret);
        }

        return ret;
}

int protect(struct mm_id * mm_idp, unsigned long addr, unsigned long len,
	    int r, int w, int x, int done, void **data)
{
        struct proc_mm_op protect;
        int prot, ret;

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

                ret = os_write_file(fd, &protect, sizeof(protect));
                if(ret != sizeof(protect))
                        printk("protect failed, err = %d", -ret);
                else ret = 0;
        }
        else {
                unsigned long args[] = { addr, len, prot, 0, 0, 0 };

                ret = run_syscall_stub(mm_idp, __NR_mprotect, args, 0,
                                       data, done);
        }

        return ret;
}

void before_mem_skas(unsigned long unused)
{
}

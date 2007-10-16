/*
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/slab.h"
#include "linux/types.h"
#include "linux/errno.h"
#include "linux/spinlock.h"
#include "asm/uaccess.h"
#include "asm/smp.h"
#include "asm/ldt.h"
#include "asm/unistd.h"
#include "kern.h"
#include "mode_kern.h"
#include "os.h"

extern int modify_ldt(int func, void *ptr, unsigned long bytecount);

#include "skas.h"
#include "skas_ptrace.h"
#include "asm/mmu_context.h"
#include "proc_mm.h"

long write_ldt_entry(struct mm_id * mm_idp, int func, struct user_desc * desc,
		     void **addr, int done)
{
	long res;

	if(proc_mm){
		/* This is a special handling for the case, that the mm to
		 * modify isn't current->active_mm.
		 * If this is called directly by modify_ldt,
		 *     (current->active_mm->context.skas.u == mm_idp)
		 * will be true. So no call to switch_mm_skas(mm_idp) is done.
		 * If this is called in case of init_new_ldt or PTRACE_LDT,
		 * mm_idp won't belong to current->active_mm, but child->mm.
		 * So we need to switch child's mm into our userspace, then
		 * later switch back.
		 *
		 * Note: I'm unsure: should interrupts be disabled here?
		 */
		if(!current->active_mm || current->active_mm == &init_mm ||
		   mm_idp != &current->active_mm->context.skas.id)
			switch_mm_skas(mm_idp);
	}

	if(ptrace_ldt) {
		struct ptrace_ldt ldt_op = (struct ptrace_ldt) {
			.func = func,
			.ptr = desc,
			.bytecount = sizeof(*desc)};
		u32 cpu;
		int pid;

		if(!proc_mm)
			pid = mm_idp->u.pid;
		else {
			cpu = get_cpu();
			pid = userspace_pid[cpu];
		}

		res = os_ptrace_ldt(pid, 0, (unsigned long) &ldt_op);

		if(proc_mm)
			put_cpu();
	}
	else {
		void *stub_addr;
		res = syscall_stub_data(mm_idp, (unsigned long *)desc,
					(sizeof(*desc) + sizeof(long) - 1) &
					    ~(sizeof(long) - 1),
					addr, &stub_addr);
		if(!res){
			unsigned long args[] = { func,
						 (unsigned long)stub_addr,
						 sizeof(*desc),
						 0, 0, 0 };
			res = run_syscall_stub(mm_idp, __NR_modify_ldt, args,
					       0, addr, done);
		}
	}

	if(proc_mm){
		/* This is the second part of special handling, that makes
		 * PTRACE_LDT possible to implement.
		 */
		if(current->active_mm && current->active_mm != &init_mm &&
		   mm_idp != &current->active_mm->context.skas.id)
			switch_mm_skas(&current->active_mm->context.skas.id);
	}

	return res;
}

static long read_ldt_from_host(void __user * ptr, unsigned long bytecount)
{
	int res, n;
	struct ptrace_ldt ptrace_ldt = (struct ptrace_ldt) {
			.func = 0,
			.bytecount = bytecount,
			.ptr = kmalloc(bytecount, GFP_KERNEL)};
	u32 cpu;

	if(ptrace_ldt.ptr == NULL)
		return -ENOMEM;

	/* This is called from sys_modify_ldt only, so userspace_pid gives
	 * us the right number
	 */

	cpu = get_cpu();
	res = os_ptrace_ldt(userspace_pid[cpu], 0, (unsigned long) &ptrace_ldt);
	put_cpu();
	if(res < 0)
		goto out;

	n = copy_to_user(ptr, ptrace_ldt.ptr, res);
	if(n != 0)
		res = -EFAULT;

  out:
	kfree(ptrace_ldt.ptr);

	return res;
}

/*
 * In skas mode, we hold our own ldt data in UML.
 * Thus, the code implementing sys_modify_ldt_skas
 * is very similar to (and mostly stolen from) sys_modify_ldt
 * for arch/i386/kernel/ldt.c
 * The routines copied and modified in part are:
 * - read_ldt
 * - read_default_ldt
 * - write_ldt
 * - sys_modify_ldt_skas
 */

static int read_ldt(void __user * ptr, unsigned long bytecount)
{
	int i, err = 0;
	unsigned long size;
	uml_ldt_t * ldt = &current->mm->context.skas.ldt;

	if(!ldt->entry_count)
		goto out;
	if(bytecount > LDT_ENTRY_SIZE*LDT_ENTRIES)
		bytecount = LDT_ENTRY_SIZE*LDT_ENTRIES;
	err = bytecount;

	if(ptrace_ldt){
		return read_ldt_from_host(ptr, bytecount);
	}

	down(&ldt->semaphore);
	if(ldt->entry_count <= LDT_DIRECT_ENTRIES){
		size = LDT_ENTRY_SIZE*LDT_DIRECT_ENTRIES;
		if(size > bytecount)
			size = bytecount;
		if(copy_to_user(ptr, ldt->u.entries, size))
			err = -EFAULT;
		bytecount -= size;
		ptr += size;
	}
	else {
		for(i=0; i<ldt->entry_count/LDT_ENTRIES_PER_PAGE && bytecount;
			 i++){
			size = PAGE_SIZE;
			if(size > bytecount)
				size = bytecount;
			if(copy_to_user(ptr, ldt->u.pages[i], size)){
				err = -EFAULT;
				break;
			}
			bytecount -= size;
			ptr += size;
		}
	}
	up(&ldt->semaphore);

	if(bytecount == 0 || err == -EFAULT)
		goto out;

	if(clear_user(ptr, bytecount))
		err = -EFAULT;

out:
	return err;
}

static int read_default_ldt(void __user * ptr, unsigned long bytecount)
{
	int err;

	if(bytecount > 5*LDT_ENTRY_SIZE)
		bytecount = 5*LDT_ENTRY_SIZE;

	err = bytecount;
	/* UML doesn't support lcall7 and lcall27.
	 * So, we don't really have a default ldt, but emulate
	 * an empty ldt of common host default ldt size.
	 */
	if(clear_user(ptr, bytecount))
		err = -EFAULT;

	return err;
}

static int write_ldt(void __user * ptr, unsigned long bytecount, int func)
{
	uml_ldt_t * ldt = &current->mm->context.skas.ldt;
	struct mm_id * mm_idp = &current->mm->context.skas.id;
	int i, err;
	struct user_desc ldt_info;
	struct ldt_entry entry0, *ldt_p;
	void *addr = NULL;

	err = -EINVAL;
	if(bytecount != sizeof(ldt_info))
		goto out;
	err = -EFAULT;
	if(copy_from_user(&ldt_info, ptr, sizeof(ldt_info)))
		goto out;

	err = -EINVAL;
	if(ldt_info.entry_number >= LDT_ENTRIES)
		goto out;
	if(ldt_info.contents == 3){
		if (func == 1)
			goto out;
		if (ldt_info.seg_not_present == 0)
			goto out;
	}

        if(!ptrace_ldt)
                down(&ldt->semaphore);

	err = write_ldt_entry(mm_idp, func, &ldt_info, &addr, 1);
	if(err)
		goto out_unlock;
        else if(ptrace_ldt) {
	/* With PTRACE_LDT available, this is used as a flag only */
                ldt->entry_count = 1;
                goto out;
        }

	if(ldt_info.entry_number >= ldt->entry_count &&
	   ldt_info.entry_number >= LDT_DIRECT_ENTRIES){
		for(i=ldt->entry_count/LDT_ENTRIES_PER_PAGE;
		    i*LDT_ENTRIES_PER_PAGE <= ldt_info.entry_number;
		    i++){
			if(i == 0)
				memcpy(&entry0, ldt->u.entries,
				       sizeof(entry0));
			ldt->u.pages[i] = (struct ldt_entry *)
				__get_free_page(GFP_KERNEL|__GFP_ZERO);
			if(!ldt->u.pages[i]){
				err = -ENOMEM;
				/* Undo the change in host */
				memset(&ldt_info, 0, sizeof(ldt_info));
				write_ldt_entry(mm_idp, 1, &ldt_info, &addr, 1);
				goto out_unlock;
			}
			if(i == 0) {
				memcpy(ldt->u.pages[0], &entry0,
				       sizeof(entry0));
				memcpy(ldt->u.pages[0]+1, ldt->u.entries+1,
				       sizeof(entry0)*(LDT_DIRECT_ENTRIES-1));
			}
			ldt->entry_count = (i + 1) * LDT_ENTRIES_PER_PAGE;
		}
	}
	if(ldt->entry_count <= ldt_info.entry_number)
		ldt->entry_count = ldt_info.entry_number + 1;

	if(ldt->entry_count <= LDT_DIRECT_ENTRIES)
		ldt_p = ldt->u.entries + ldt_info.entry_number;
	else
		ldt_p = ldt->u.pages[ldt_info.entry_number/LDT_ENTRIES_PER_PAGE] +
			ldt_info.entry_number%LDT_ENTRIES_PER_PAGE;

	if(ldt_info.base_addr == 0 && ldt_info.limit == 0 &&
	   (func == 1 || LDT_empty(&ldt_info))){
		ldt_p->a = 0;
		ldt_p->b = 0;
	}
	else{
		if (func == 1)
			ldt_info.useable = 0;
		ldt_p->a = LDT_entry_a(&ldt_info);
		ldt_p->b = LDT_entry_b(&ldt_info);
	}
	err = 0;

out_unlock:
	up(&ldt->semaphore);
out:
	return err;
}

static long do_modify_ldt_skas(int func, void __user *ptr,
			       unsigned long bytecount)
{
	int ret = -ENOSYS;

	switch (func) {
		case 0:
			ret = read_ldt(ptr, bytecount);
			break;
		case 1:
		case 0x11:
			ret = write_ldt(ptr, bytecount, func);
			break;
		case 2:
			ret = read_default_ldt(ptr, bytecount);
			break;
	}
	return ret;
}

static DEFINE_SPINLOCK(host_ldt_lock);
static short dummy_list[9] = {0, -1};
static short * host_ldt_entries = NULL;

static void ldt_get_host_info(void)
{
	long ret;
	struct ldt_entry * ldt;
	short *tmp;
	int i, size, k, order;

	spin_lock(&host_ldt_lock);

	if(host_ldt_entries != NULL){
		spin_unlock(&host_ldt_lock);
		return;
	}
	host_ldt_entries = dummy_list+1;

	spin_unlock(&host_ldt_lock);

	for(i = LDT_PAGES_MAX-1, order=0; i; i>>=1, order++);

	ldt = (struct ldt_entry *)
	      __get_free_pages(GFP_KERNEL|__GFP_ZERO, order);
	if(ldt == NULL) {
		printk("ldt_get_host_info: couldn't allocate buffer for host "
		       "ldt\n");
		return;
	}

	ret = modify_ldt(0, ldt, (1<<order)*PAGE_SIZE);
	if(ret < 0) {
		printk("ldt_get_host_info: couldn't read host ldt\n");
		goto out_free;
	}
	if(ret == 0) {
		/* default_ldt is active, simply write an empty entry 0 */
		host_ldt_entries = dummy_list;
		goto out_free;
	}

	for(i=0, size=0; i<ret/LDT_ENTRY_SIZE; i++){
		if(ldt[i].a != 0 || ldt[i].b != 0)
			size++;
	}

	if(size < ARRAY_SIZE(dummy_list))
		host_ldt_entries = dummy_list;
	else {
		size = (size + 1) * sizeof(dummy_list[0]);
		tmp = kmalloc(size, GFP_KERNEL);
		if(tmp == NULL) {
			printk("ldt_get_host_info: couldn't allocate host ldt "
			       "list\n");
			goto out_free;
		}
		host_ldt_entries = tmp;
	}

	for(i=0, k=0; i<ret/LDT_ENTRY_SIZE; i++){
		if(ldt[i].a != 0 || ldt[i].b != 0) {
			host_ldt_entries[k++] = i;
		}
	}
	host_ldt_entries[k] = -1;

out_free:
	free_pages((unsigned long)ldt, order);
}

long init_new_ldt(struct mmu_context_skas * new_mm,
		  struct mmu_context_skas * from_mm)
{
	struct user_desc desc;
	short * num_p;
	int i;
	long page, err=0;
	void *addr = NULL;
	struct proc_mm_op copy;


	if(!ptrace_ldt)
		init_MUTEX(&new_mm->ldt.semaphore);

	if(!from_mm){
		memset(&desc, 0, sizeof(desc));
		/*
		 * We have to initialize a clean ldt.
		 */
		if(proc_mm) {
			/*
			 * If the new mm was created using proc_mm, host's
			 * default-ldt currently is assigned, which normally
			 * contains the call-gates for lcall7 and lcall27.
			 * To remove these gates, we simply write an empty
			 * entry as number 0 to the host.
			 */
			err = write_ldt_entry(&new_mm->id, 1, &desc,
					      &addr, 1);
		}
		else{
			/*
			 * Now we try to retrieve info about the ldt, we
			 * inherited from the host. All ldt-entries found
			 * will be reset in the following loop
			 */
			ldt_get_host_info();
			for(num_p=host_ldt_entries; *num_p != -1; num_p++){
				desc.entry_number = *num_p;
				err = write_ldt_entry(&new_mm->id, 1, &desc,
						      &addr, *(num_p + 1) == -1);
				if(err)
					break;
			}
		}
		new_mm->ldt.entry_count = 0;

		goto out;
	}

	if(proc_mm){
		/* We have a valid from_mm, so we now have to copy the LDT of
		 * from_mm to new_mm, because using proc_mm an new mm with
		 * an empty/default LDT was created in new_mm()
		 */
		copy = ((struct proc_mm_op) { .op 	= MM_COPY_SEGMENTS,
					      .u 	=
					      { .copy_segments =
							from_mm->id.u.mm_fd } } );
		i = os_write_file(new_mm->id.u.mm_fd, &copy, sizeof(copy));
		if(i != sizeof(copy))
			printk("new_mm : /proc/mm copy_segments failed, "
			       "err = %d\n", -i);
	}

	if(!ptrace_ldt) {
		/* Our local LDT is used to supply the data for
		 * modify_ldt(READLDT), if PTRACE_LDT isn't available,
		 * i.e., we have to use the stub for modify_ldt, which
		 * can't handle the big read buffer of up to 64kB.
		 */
		down(&from_mm->ldt.semaphore);
		if(from_mm->ldt.entry_count <= LDT_DIRECT_ENTRIES){
			memcpy(new_mm->ldt.u.entries, from_mm->ldt.u.entries,
			       sizeof(new_mm->ldt.u.entries));
		}
		else{
			i = from_mm->ldt.entry_count / LDT_ENTRIES_PER_PAGE;
			while(i-->0){
				page = __get_free_page(GFP_KERNEL|__GFP_ZERO);
				if (!page){
					err = -ENOMEM;
					break;
				}
				new_mm->ldt.u.pages[i] =
					(struct ldt_entry *) page;
				memcpy(new_mm->ldt.u.pages[i],
				       from_mm->ldt.u.pages[i], PAGE_SIZE);
			}
		}
		new_mm->ldt.entry_count = from_mm->ldt.entry_count;
		up(&from_mm->ldt.semaphore);
	}

    out:
	return err;
}


void free_ldt(struct mmu_context_skas * mm)
{
	int i;

	if(!ptrace_ldt && mm->ldt.entry_count > LDT_DIRECT_ENTRIES){
		i = mm->ldt.entry_count / LDT_ENTRIES_PER_PAGE;
		while(i-- > 0){
			free_page((long )mm->ldt.u.pages[i]);
		}
	}
	mm->ldt.entry_count = 0;
}

int sys_modify_ldt(int func, void __user *ptr, unsigned long bytecount)
{
	return do_modify_ldt_skas(func, ptr, bytecount);
}

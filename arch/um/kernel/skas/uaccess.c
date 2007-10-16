/*
 * Copyright (C) 2002 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/compiler.h"
#include "linux/stddef.h"
#include "linux/kernel.h"
#include "linux/string.h"
#include "linux/fs.h"
#include "linux/hardirq.h"
#include "linux/highmem.h"
#include "asm/page.h"
#include "asm/pgtable.h"
#include "asm/uaccess.h"
#include "kern_util.h"
#include "os.h"

extern void *um_virt_to_phys(struct task_struct *task, unsigned long addr,
			     pte_t *pte_out);

static unsigned long maybe_map(unsigned long virt, int is_write)
{
	pte_t pte;
	int err;

	void *phys = um_virt_to_phys(current, virt, &pte);
	int dummy_code;

	if(IS_ERR(phys) || (is_write && !pte_write(pte))){
		err = handle_page_fault(virt, 0, is_write, 1, &dummy_code);
		if(err)
			return(-1UL);
		phys = um_virt_to_phys(current, virt, NULL);
	}
        if(IS_ERR(phys))
                phys = (void *) -1;

	return((unsigned long) phys);
}

static int do_op_one_page(unsigned long addr, int len, int is_write,
		 int (*op)(unsigned long addr, int len, void *arg), void *arg)
{
	struct page *page;
	int n;

	addr = maybe_map(addr, is_write);
	if(addr == -1UL)
		return(-1);

	page = phys_to_page(addr);
	addr = (unsigned long) kmap_atomic(page, KM_UML_USERCOPY) + (addr & ~PAGE_MASK);

	n = (*op)(addr, len, arg);

	kunmap_atomic(page, KM_UML_USERCOPY);

	return(n);
}

static void do_buffer_op(void *jmpbuf, void *arg_ptr)
{
	va_list args;
	unsigned long addr;
	int len, is_write, size, remain, n;
	int (*op)(unsigned long, int, void *);
	void *arg;
	int *res;

	va_copy(args, *(va_list *)arg_ptr);
	addr = va_arg(args, unsigned long);
	len = va_arg(args, int);
	is_write = va_arg(args, int);
	op = va_arg(args, void *);
	arg = va_arg(args, void *);
	res = va_arg(args, int *);
	va_end(args);
	size = min(PAGE_ALIGN(addr) - addr, (unsigned long) len);
	remain = len;

	current->thread.fault_catcher = jmpbuf;
	n = do_op_one_page(addr, size, is_write, op, arg);
	if(n != 0){
		*res = (n < 0 ? remain : 0);
		goto out;
	}

	addr += size;
	remain -= size;
	if(remain == 0){
		*res = 0;
		goto out;
	}

	while(addr < ((addr + remain) & PAGE_MASK)){
		n = do_op_one_page(addr, PAGE_SIZE, is_write, op, arg);
		if(n != 0){
			*res = (n < 0 ? remain : 0);
			goto out;
		}

		addr += PAGE_SIZE;
		remain -= PAGE_SIZE;
	}
	if(remain == 0){
		*res = 0;
		goto out;
	}

	n = do_op_one_page(addr, remain, is_write, op, arg);
	if(n != 0)
		*res = (n < 0 ? remain : 0);
	else *res = 0;
 out:
	current->thread.fault_catcher = NULL;
}

static int buffer_op(unsigned long addr, int len, int is_write,
		     int (*op)(unsigned long addr, int len, void *arg),
		     void *arg)
{
	int faulted, res;

	faulted = setjmp_wrapper(do_buffer_op, addr, len, is_write, op, arg,
				 &res);
	if(!faulted)
		return(res);

	return(addr + len - (unsigned long) current->thread.fault_addr);
}

static int copy_chunk_from_user(unsigned long from, int len, void *arg)
{
	unsigned long *to_ptr = arg, to = *to_ptr;

	memcpy((void *) to, (void *) from, len);
	*to_ptr += len;
	return(0);
}

int copy_from_user(void *to, const void __user *from, int n)
{
	if(segment_eq(get_fs(), KERNEL_DS)){
		memcpy(to, (__force void*)from, n);
		return(0);
	}

	return(access_ok(VERIFY_READ, from, n) ?
	       buffer_op((unsigned long) from, n, 0, copy_chunk_from_user, &to):
	       n);
}

static int copy_chunk_to_user(unsigned long to, int len, void *arg)
{
	unsigned long *from_ptr = arg, from = *from_ptr;

	memcpy((void *) to, (void *) from, len);
	*from_ptr += len;
	return(0);
}

int copy_to_user(void __user *to, const void *from, int n)
{
	if(segment_eq(get_fs(), KERNEL_DS)){
		memcpy((__force void*)to, from, n);
		return(0);
	}

	return(access_ok(VERIFY_WRITE, to, n) ?
	       buffer_op((unsigned long) to, n, 1, copy_chunk_to_user, &from) :
	       n);
}

static int strncpy_chunk_from_user(unsigned long from, int len, void *arg)
{
	char **to_ptr = arg, *to = *to_ptr;
	int n;

	strncpy(to, (void *) from, len);
	n = strnlen(to, len);
	*to_ptr += n;

	if(n < len)
	        return(1);
	return(0);
}

int strncpy_from_user(char *dst, const char __user *src, int count)
{
	int n;
	char *ptr = dst;

	if(segment_eq(get_fs(), KERNEL_DS)){
		strncpy(dst, (__force void*)src, count);
		return(strnlen(dst, count));
	}

	if(!access_ok(VERIFY_READ, src, 1))
		return(-EFAULT);

	n = buffer_op((unsigned long) src, count, 0, strncpy_chunk_from_user,
		      &ptr);
	if(n != 0)
		return(-EFAULT);
	return(strnlen(dst, count));
}

static int clear_chunk(unsigned long addr, int len, void *unused)
{
	memset((void *) addr, 0, len);
	return(0);
}

int __clear_user(void __user *mem, int len)
{
	return(buffer_op((unsigned long) mem, len, 1, clear_chunk, NULL));
}

int clear_user(void __user *mem, int len)
{
	if(segment_eq(get_fs(), KERNEL_DS)){
		memset((__force void*)mem, 0, len);
		return(0);
	}

	return(access_ok(VERIFY_WRITE, mem, len) ?
	       buffer_op((unsigned long) mem, len, 1, clear_chunk, NULL) : len);
}

static int strnlen_chunk(unsigned long str, int len, void *arg)
{
	int *len_ptr = arg, n;

	n = strnlen((void *) str, len);
	*len_ptr += n;

	if(n < len)
		return(1);
	return(0);
}

int strnlen_user(const void __user *str, int len)
{
	int count = 0, n;

	if(segment_eq(get_fs(), KERNEL_DS))
		return(strnlen((__force char*)str, len) + 1);

	n = buffer_op((unsigned long) str, len, 0, strnlen_chunk, &count);
	if(n == 0)
		return(count + 1);
	return(-EFAULT);
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

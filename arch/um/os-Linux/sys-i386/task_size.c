#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include "longjmp.h"
#include "kern_constants.h"

static jmp_buf buf;

static void segfault(int sig)
{
	longjmp(buf, 1);
}

static int page_ok(unsigned long page)
{
	unsigned long *address = (unsigned long *) (page << UM_KERN_PAGE_SHIFT);
	unsigned long n = ~0UL;
	void *mapped = NULL;
	int ok = 0;

	/*
	 * First see if the page is readable.  If it is, it may still
	 * be a VDSO, so we go on to see if it's writable.  If not
	 * then try mapping memory there.  If that fails, then we're
	 * still in the kernel area.  As a sanity check, we'll fail if
	 * the mmap succeeds, but gives us an address different from
	 * what we wanted.
	 */
	if (setjmp(buf) == 0)
		n = *address;
	else {
		mapped = mmap(address, UM_KERN_PAGE_SIZE,
			      PROT_READ | PROT_WRITE,
			      MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mapped == MAP_FAILED)
			return 0;
		if (mapped != address)
			goto out;
	}

	/*
	 * Now, is it writeable?  If so, then we're in user address
	 * space.  If not, then try mprotecting it and try the write
	 * again.
	 */
	if (setjmp(buf) == 0) {
		*address = n;
		ok = 1;
		goto out;
	} else if (mprotect(address, UM_KERN_PAGE_SIZE,
			    PROT_READ | PROT_WRITE) != 0)
		goto out;

	if (setjmp(buf) == 0) {
		*address = n;
		ok = 1;
	}

 out:
	if (mapped != NULL)
		munmap(mapped, UM_KERN_PAGE_SIZE);
	return ok;
}

unsigned long os_get_task_size(void)
{
	struct sigaction sa, old;
	unsigned long bottom = 0;
	/*
	 * A 32-bit UML on a 64-bit host gets confused about the VDSO at
	 * 0xffffe000.  It is mapped, is readable, can be reprotected writeable
	 * and written.  However, exec discovers later that it can't be
	 * unmapped.  So, just set the highest address to be checked to just
	 * below it.  This might waste some address space on 4G/4G 32-bit
	 * hosts, but shouldn't hurt otherwise.
	 */
	unsigned long top = 0xffffd000 >> UM_KERN_PAGE_SHIFT;
	unsigned long test;

	printf("Locating the top of the address space ... ");
	fflush(stdout);

	/*
	 * We're going to be longjmping out of the signal handler, so
	 * SA_DEFER needs to be set.
	 */
	sa.sa_handler = segfault;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NODEFER;
	sigaction(SIGSEGV, &sa, &old);

	if (!page_ok(bottom)) {
		fprintf(stderr, "Address 0x%x no good?\n",
			bottom << UM_KERN_PAGE_SHIFT);
		exit(1);
	}

	/* This could happen with a 4G/4G split */
	if (page_ok(top))
		goto out;

	do {
		test = bottom + (top - bottom) / 2;
		if (page_ok(test))
			bottom = test;
		else
			top = test;
	} while (top - bottom > 1);

out:
	/* Restore the old SIGSEGV handling */
	sigaction(SIGSEGV, &old, NULL);

	top <<= UM_KERN_PAGE_SHIFT;
	printf("0x%x\n", top);
	fflush(stdout);

	return top;
}

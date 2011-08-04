/*
 * vdso_test.c: Sample code to test parse_vdso.c on x86_64
 * Copyright (c) 2011 Andy Lutomirski
 * Subject to the GNU General Public License, version 2
 *
 * You can amuse yourself by compiling with:
 * gcc -std=gnu99 -nostdlib
 *     -Os -fno-asynchronous-unwind-tables -flto
 *      vdso_test.c parse_vdso.c -o vdso_test
 * to generate a small binary with no dependencies at all.
 */

#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>

extern void *vdso_sym(const char *version, const char *name);
extern void vdso_init_from_sysinfo_ehdr(uintptr_t base);
extern void vdso_init_from_auxv(void *auxv);

/* We need a libc functions... */
int strcmp(const char *a, const char *b)
{
	/* This implementation is buggy: it never returns -1. */
	while (*a || *b) {
		if (*a != *b)
			return 1;
		if (*a == 0 || *b == 0)
			return 1;
		a++;
		b++;
	}

	return 0;
}

/* ...and two syscalls.  This is x86_64-specific. */
static inline long linux_write(int fd, const void *data, size_t len)
{

	long ret;
	asm volatile ("syscall" : "=a" (ret) : "a" (__NR_write),
		      "D" (fd), "S" (data), "d" (len) :
		      "cc", "memory", "rcx",
		      "r8", "r9", "r10", "r11" );
	return ret;
}

static inline void linux_exit(int code)
{
	asm volatile ("syscall" : : "a" (__NR_exit), "D" (code));
}

void to_base10(char *lastdig, uint64_t n)
{
	while (n) {
		*lastdig = (n % 10) + '0';
		n /= 10;
		lastdig--;
	}
}

__attribute__((externally_visible)) void c_main(void **stack)
{
	/* Parse the stack */
	long argc = (long)*stack;
	stack += argc + 2;

	/* Now we're pointing at the environment.  Skip it. */
	while(*stack)
		stack++;
	stack++;

	/* Now we're pointing at auxv.  Initialize the vDSO parser. */
	vdso_init_from_auxv((void *)stack);

	/* Find gettimeofday. */
	typedef long (*gtod_t)(struct timeval *tv, struct timezone *tz);
	gtod_t gtod = (gtod_t)vdso_sym("LINUX_2.6", "__vdso_gettimeofday");

	if (!gtod)
		linux_exit(1);

	struct timeval tv;
	long ret = gtod(&tv, 0);

	if (ret == 0) {
		char buf[] = "The time is                     .000000\n";
		to_base10(buf + 31, tv.tv_sec);
		to_base10(buf + 38, tv.tv_usec);
		linux_write(1, buf, sizeof(buf) - 1);
	} else {
		linux_exit(ret);
	}

	linux_exit(0);
}

/*
 * This is the real entry point.  It passes the initial stack into
 * the C entry point.
 */
asm (
	".text\n"
	".global _start\n"
        ".type _start,@function\n"
        "_start:\n\t"
        "mov %rsp,%rdi\n\t"
        "jmp c_main"
	);

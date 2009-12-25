#ifndef _ASM_PARISC_FTRACE_H
#define _ASM_PARISC_FTRACE_H

#ifndef __ASSEMBLY__
extern void mcount(void);

/*
 * Stack of return addresses for functions of a thread.
 * Used in struct thread_info
 */
struct ftrace_ret_stack {
	unsigned long ret;
	unsigned long func;
	unsigned long long calltime;
};

/*
 * Primary handler of a function return.
 * It relays on ftrace_return_to_handler.
 * Defined in entry.S
 */
extern void return_to_handler(void);


extern unsigned long return_address(unsigned int);

#define HAVE_ARCH_CALLER_ADDR

#define CALLER_ADDR0 ((unsigned long)__builtin_return_address(0))
#define CALLER_ADDR1 return_address(1)
#define CALLER_ADDR2 return_address(2)
#define CALLER_ADDR3 return_address(3)
#define CALLER_ADDR4 return_address(4)
#define CALLER_ADDR5 return_address(5)
#define CALLER_ADDR6 return_address(6)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_PARISC_FTRACE_H */

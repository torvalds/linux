/*
 * EFI call stub for IA32.
 *
 * This stub allows us to make EFI calls in physical mode with interrupts
 * turned off.
 */

#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/page.h>
#include <asm/pgtable.h>

/*
 * efi_call_phys(void *, ...) is a function with variable parameters.
 * All the callers of this function assure that all the parameters are 4-bytes.
 */

/*
 * In gcc calling convention, EBX, ESP, EBP, ESI and EDI are all callee save.
 * So we'd better save all of them at the beginning of this function and restore
 * at the end no matter how many we use, because we can not assure EFI runtime
 * service functions will comply with gcc calling convention, too.
 */

.text
ENTRY(efi_call_phys)
	/*
	 * 0. The function can only be called in Linux kernel. So CS has been
	 * set to 0x0010, DS and SS have been set to 0x0018. In EFI, I found
	 * the values of these registers are the same. And, the corresponding
	 * GDT entries are identical. So I will do nothing about segment reg
	 * and GDT, but change GDT base register in prelog and epilog.
	 */

	/*
	 * 1. Now I am running with EIP = <physical address> + PAGE_OFFSET.
	 * But to make it smoothly switch from virtual mode to flat mode.
	 * The mapping of lower virtual memory has been created in prelog and
	 * epilog.
	 */
	movl	$1f, %edx
	subl	$__PAGE_OFFSET, %edx
	jmp	*%edx
1:

	/*
	 * 2. Now on the top of stack is the return
	 * address in the caller of efi_call_phys(), then parameter 1,
	 * parameter 2, ..., param n. To make things easy, we save the return
	 * address of efi_call_phys in a global variable.
	 */
	popl	%edx
	movl	%edx, saved_return_addr
	/* get the function pointer into ECX*/
	popl	%ecx
	movl	%ecx, efi_rt_function_ptr
	movl	$2f, %edx
	subl	$__PAGE_OFFSET, %edx
	pushl	%edx

	/*
	 * 3. Clear PG bit in %CR0.
	 */
	movl	%cr0, %edx
	andl	$0x7fffffff, %edx
	movl	%edx, %cr0
	jmp	1f
1:

	/*
	 * 4. Adjust stack pointer.
	 */
	subl	$__PAGE_OFFSET, %esp

	/*
	 * 5. Call the physical function.
	 */
	jmp	*%ecx

2:
	/*
	 * 6. After EFI runtime service returns, control will return to
	 * following instruction. We'd better readjust stack pointer first.
	 */
	addl	$__PAGE_OFFSET, %esp

	/*
	 * 7. Restore PG bit
	 */
	movl	%cr0, %edx
	orl	$0x80000000, %edx
	movl	%edx, %cr0
	jmp	1f
1:
	/*
	 * 8. Now restore the virtual mode from flat mode by
	 * adding EIP with PAGE_OFFSET.
	 */
	movl	$1f, %edx
	jmp	*%edx
1:

	/*
	 * 9. Balance the stack. And because EAX contain the return value,
	 * we'd better not clobber it.
	 */
	leal	efi_rt_function_ptr, %edx
	movl	(%edx), %ecx
	pushl	%ecx

	/*
	 * 10. Push the saved return address onto the stack and return.
	 */
	leal	saved_return_addr, %edx
	movl	(%edx), %ecx
	pushl	%ecx
	ret
.previous

.data
saved_return_addr:
	.long 0
efi_rt_function_ptr:
	.long 0

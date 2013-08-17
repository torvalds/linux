/*
 * EFI call stub for IA32.
 *
 * This stub allows us to make EFI calls in physical mode with interrupts
 * turned off. Note that this implementation is different from the one in
 * arch/x86/platform/efi/efi_stub_32.S because we're _already_ in physical
 * mode at this point.
 */

#include <linux/linkage.h>
#include <asm/page_types.h>

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
	 * 1. Because we haven't been relocated by this point we need to
	 * use relative addressing.
	 */
	call	1f
1:	popl	%edx
	subl	$1b, %edx

	/*
	 * 2. Now on the top of stack is the return
	 * address in the caller of efi_call_phys(), then parameter 1,
	 * parameter 2, ..., param n. To make things easy, we save the return
	 * address of efi_call_phys in a global variable.
	 */
	popl	%ecx
	movl	%ecx, saved_return_addr(%edx)
	/* get the function pointer into ECX*/
	popl	%ecx
	movl	%ecx, efi_rt_function_ptr(%edx)

	/*
	 * 3. Call the physical function.
	 */
	call	*%ecx

	/*
	 * 4. Balance the stack. And because EAX contain the return value,
	 * we'd better not clobber it. We need to calculate our address
	 * again because %ecx and %edx are not preserved across EFI function
	 * calls.
	 */
	call	1f
1:	popl	%edx
	subl	$1b, %edx

	movl	efi_rt_function_ptr(%edx), %ecx
	pushl	%ecx

	/*
	 * 10. Push the saved return address onto the stack and return.
	 */
	movl	saved_return_addr(%edx), %ecx
	pushl	%ecx
	ret
ENDPROC(efi_call_phys)
.previous

.data
saved_return_addr:
	.long 0
efi_rt_function_ptr:
	.long 0

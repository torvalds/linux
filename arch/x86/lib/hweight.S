/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/export.h>
#include <linux/linkage.h>

#include <asm/asm.h>

/*
 * unsigned int __sw_hweight32(unsigned int w)
 * %rdi: w
 */
SYM_FUNC_START(__sw_hweight32)

#ifdef CONFIG_X86_64
	movl %edi, %eax				# w
#endif
	__ASM_SIZE(push,) %__ASM_REG(dx)
	movl %eax, %edx				# w -> t
	shrl %edx				# t >>= 1
	andl $0x55555555, %edx			# t &= 0x55555555
	subl %edx, %eax				# w -= t

	movl %eax, %edx				# w -> t
	shrl $2, %eax				# w_tmp >>= 2
	andl $0x33333333, %edx			# t	&= 0x33333333
	andl $0x33333333, %eax			# w_tmp &= 0x33333333
	addl %edx, %eax				# w = w_tmp + t

	movl %eax, %edx				# w -> t
	shrl $4, %edx				# t >>= 4
	addl %edx, %eax				# w_tmp += t
	andl  $0x0f0f0f0f, %eax			# w_tmp &= 0x0f0f0f0f
	imull $0x01010101, %eax, %eax		# w_tmp *= 0x01010101
	shrl $24, %eax				# w = w_tmp >> 24
	__ASM_SIZE(pop,) %__ASM_REG(dx)
	RET
SYM_FUNC_END(__sw_hweight32)
EXPORT_SYMBOL(__sw_hweight32)

/*
 * No 32-bit variant, because it's implemented as an inline wrapper
 * on top of __arch_hweight32():
 */
#ifdef CONFIG_X86_64
SYM_FUNC_START(__sw_hweight64)
	pushq   %rdi
	pushq   %rdx

	movq    %rdi, %rdx                      # w -> t
	movabsq $0x5555555555555555, %rax
	shrq    %rdx                            # t >>= 1
	andq    %rdx, %rax                      # t &= 0x5555555555555555
	movabsq $0x3333333333333333, %rdx
	subq    %rax, %rdi                      # w -= t

	movq    %rdi, %rax                      # w -> t
	shrq    $2, %rdi                        # w_tmp >>= 2
	andq    %rdx, %rax                      # t     &= 0x3333333333333333
	andq    %rdi, %rdx                      # w_tmp &= 0x3333333333333333
	addq    %rdx, %rax                      # w = w_tmp + t

	movq    %rax, %rdx                      # w -> t
	shrq    $4, %rdx                        # t >>= 4
	addq    %rdx, %rax                      # w_tmp += t
	movabsq $0x0f0f0f0f0f0f0f0f, %rdx
	andq    %rdx, %rax                      # w_tmp &= 0x0f0f0f0f0f0f0f0f
	movabsq $0x0101010101010101, %rdx
	imulq   %rdx, %rax                      # w_tmp *= 0x0101010101010101
	shrq    $56, %rax                       # w = w_tmp >> 56

	popq    %rdx
	popq    %rdi
	RET
SYM_FUNC_END(__sw_hweight64)
EXPORT_SYMBOL(__sw_hweight64)
#endif

/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>

# enter salsa20_encrypt_bytes
ENTRY(salsa20_encrypt_bytes)
	mov	%rsp,%r11
	and	$31,%r11
	add	$256,%r11
	sub	%r11,%rsp
	# x = arg1
	mov	%rdi,%r8
	# m = arg2
	mov	%rsi,%rsi
	# out = arg3
	mov	%rdx,%rdi
	# bytes = arg4
	mov	%rcx,%rdx
	#               unsigned>? bytes - 0
	cmp	$0,%rdx
	# comment:fp stack unchanged by jump
	# goto done if !unsigned>
	jbe	._done
	# comment:fp stack unchanged by fallthrough
# start:
._start:
	# r11_stack = r11
	movq	%r11,0(%rsp)
	# r12_stack = r12
	movq	%r12,8(%rsp)
	# r13_stack = r13
	movq	%r13,16(%rsp)
	# r14_stack = r14
	movq	%r14,24(%rsp)
	# r15_stack = r15
	movq	%r15,32(%rsp)
	# rbx_stack = rbx
	movq	%rbx,40(%rsp)
	# rbp_stack = rbp
	movq	%rbp,48(%rsp)
	# in0 = *(uint64 *) (x + 0)
	movq	0(%r8),%rcx
	# in2 = *(uint64 *) (x + 8)
	movq	8(%r8),%r9
	# in4 = *(uint64 *) (x + 16)
	movq	16(%r8),%rax
	# in6 = *(uint64 *) (x + 24)
	movq	24(%r8),%r10
	# in8 = *(uint64 *) (x + 32)
	movq	32(%r8),%r11
	# in10 = *(uint64 *) (x + 40)
	movq	40(%r8),%r12
	# in12 = *(uint64 *) (x + 48)
	movq	48(%r8),%r13
	# in14 = *(uint64 *) (x + 56)
	movq	56(%r8),%r14
	# j0 = in0
	movq	%rcx,56(%rsp)
	# j2 = in2
	movq	%r9,64(%rsp)
	# j4 = in4
	movq	%rax,72(%rsp)
	# j6 = in6
	movq	%r10,80(%rsp)
	# j8 = in8
	movq	%r11,88(%rsp)
	# j10 = in10
	movq	%r12,96(%rsp)
	# j12 = in12
	movq	%r13,104(%rsp)
	# j14 = in14
	movq	%r14,112(%rsp)
	# x_backup = x
	movq	%r8,120(%rsp)
# bytesatleast1:
._bytesatleast1:
	#                   unsigned<? bytes - 64
	cmp	$64,%rdx
	# comment:fp stack unchanged by jump
	#   goto nocopy if !unsigned<
	jae	._nocopy
	#     ctarget = out
	movq	%rdi,128(%rsp)
	#     out = &tmp
	leaq	192(%rsp),%rdi
	#     i = bytes
	mov	%rdx,%rcx
	#     while (i) { *out++ = *m++; --i }
	rep	movsb
	#     out = &tmp
	leaq	192(%rsp),%rdi
	#     m = &tmp
	leaq	192(%rsp),%rsi
	# comment:fp stack unchanged by fallthrough
#   nocopy:
._nocopy:
	#   out_backup = out
	movq	%rdi,136(%rsp)
	#   m_backup = m
	movq	%rsi,144(%rsp)
	#   bytes_backup = bytes
	movq	%rdx,152(%rsp)
	#   x1 = j0
	movq	56(%rsp),%rdi
	#   x0 = x1
	mov	%rdi,%rdx
	#   (uint64) x1 >>= 32
	shr	$32,%rdi
	#   		x3 = j2
	movq	64(%rsp),%rsi
	#   		x2 = x3
	mov	%rsi,%rcx
	#   		(uint64) x3 >>= 32
	shr	$32,%rsi
	#   x5 = j4
	movq	72(%rsp),%r8
	#   x4 = x5
	mov	%r8,%r9
	#   (uint64) x5 >>= 32
	shr	$32,%r8
	#   x5_stack = x5
	movq	%r8,160(%rsp)
	#   		x7 = j6
	movq	80(%rsp),%r8
	#   		x6 = x7
	mov	%r8,%rax
	#   		(uint64) x7 >>= 32
	shr	$32,%r8
	#   x9 = j8
	movq	88(%rsp),%r10
	#   x8 = x9
	mov	%r10,%r11
	#   (uint64) x9 >>= 32
	shr	$32,%r10
	#   		x11 = j10
	movq	96(%rsp),%r12
	#   		x10 = x11
	mov	%r12,%r13
	#   		x10_stack = x10
	movq	%r13,168(%rsp)
	#   		(uint64) x11 >>= 32
	shr	$32,%r12
	#   x13 = j12
	movq	104(%rsp),%r13
	#   x12 = x13
	mov	%r13,%r14
	#   (uint64) x13 >>= 32
	shr	$32,%r13
	#   		x15 = j14
	movq	112(%rsp),%r15
	#   		x14 = x15
	mov	%r15,%rbx
	#   		(uint64) x15 >>= 32
	shr	$32,%r15
	#   		x15_stack = x15
	movq	%r15,176(%rsp)
	#   i = 20
	mov	$20,%r15
#   mainloop:
._mainloop:
	#   i_backup = i
	movq	%r15,184(%rsp)
	# 		x5 = x5_stack
	movq	160(%rsp),%r15
	# a = x12 + x0
	lea	(%r14,%rdx),%rbp
	# (uint32) a <<<= 7
	rol	$7,%ebp
	# x4 ^= a
	xor	%rbp,%r9
	# 		b = x1 + x5
	lea	(%rdi,%r15),%rbp
	# 		(uint32) b <<<= 7
	rol	$7,%ebp
	# 		x9 ^= b
	xor	%rbp,%r10
	# a = x0 + x4
	lea	(%rdx,%r9),%rbp
	# (uint32) a <<<= 9
	rol	$9,%ebp
	# x8 ^= a
	xor	%rbp,%r11
	# 		b = x5 + x9
	lea	(%r15,%r10),%rbp
	# 		(uint32) b <<<= 9
	rol	$9,%ebp
	# 		x13 ^= b
	xor	%rbp,%r13
	# a = x4 + x8
	lea	(%r9,%r11),%rbp
	# (uint32) a <<<= 13
	rol	$13,%ebp
	# x12 ^= a
	xor	%rbp,%r14
	# 		b = x9 + x13
	lea	(%r10,%r13),%rbp
	# 		(uint32) b <<<= 13
	rol	$13,%ebp
	# 		x1 ^= b
	xor	%rbp,%rdi
	# a = x8 + x12
	lea	(%r11,%r14),%rbp
	# (uint32) a <<<= 18
	rol	$18,%ebp
	# x0 ^= a
	xor	%rbp,%rdx
	# 		b = x13 + x1
	lea	(%r13,%rdi),%rbp
	# 		(uint32) b <<<= 18
	rol	$18,%ebp
	# 		x5 ^= b
	xor	%rbp,%r15
	# 				x10 = x10_stack
	movq	168(%rsp),%rbp
	# 		x5_stack = x5
	movq	%r15,160(%rsp)
	# 				c = x6 + x10
	lea	(%rax,%rbp),%r15
	# 				(uint32) c <<<= 7
	rol	$7,%r15d
	# 				x14 ^= c
	xor	%r15,%rbx
	# 				c = x10 + x14
	lea	(%rbp,%rbx),%r15
	# 				(uint32) c <<<= 9
	rol	$9,%r15d
	# 				x2 ^= c
	xor	%r15,%rcx
	# 				c = x14 + x2
	lea	(%rbx,%rcx),%r15
	# 				(uint32) c <<<= 13
	rol	$13,%r15d
	# 				x6 ^= c
	xor	%r15,%rax
	# 				c = x2 + x6
	lea	(%rcx,%rax),%r15
	# 				(uint32) c <<<= 18
	rol	$18,%r15d
	# 				x10 ^= c
	xor	%r15,%rbp
	# 						x15 = x15_stack
	movq	176(%rsp),%r15
	# 				x10_stack = x10
	movq	%rbp,168(%rsp)
	# 						d = x11 + x15
	lea	(%r12,%r15),%rbp
	# 						(uint32) d <<<= 7
	rol	$7,%ebp
	# 						x3 ^= d
	xor	%rbp,%rsi
	# 						d = x15 + x3
	lea	(%r15,%rsi),%rbp
	# 						(uint32) d <<<= 9
	rol	$9,%ebp
	# 						x7 ^= d
	xor	%rbp,%r8
	# 						d = x3 + x7
	lea	(%rsi,%r8),%rbp
	# 						(uint32) d <<<= 13
	rol	$13,%ebp
	# 						x11 ^= d
	xor	%rbp,%r12
	# 						d = x7 + x11
	lea	(%r8,%r12),%rbp
	# 						(uint32) d <<<= 18
	rol	$18,%ebp
	# 						x15 ^= d
	xor	%rbp,%r15
	# 						x15_stack = x15
	movq	%r15,176(%rsp)
	# 		x5 = x5_stack
	movq	160(%rsp),%r15
	# a = x3 + x0
	lea	(%rsi,%rdx),%rbp
	# (uint32) a <<<= 7
	rol	$7,%ebp
	# x1 ^= a
	xor	%rbp,%rdi
	# 		b = x4 + x5
	lea	(%r9,%r15),%rbp
	# 		(uint32) b <<<= 7
	rol	$7,%ebp
	# 		x6 ^= b
	xor	%rbp,%rax
	# a = x0 + x1
	lea	(%rdx,%rdi),%rbp
	# (uint32) a <<<= 9
	rol	$9,%ebp
	# x2 ^= a
	xor	%rbp,%rcx
	# 		b = x5 + x6
	lea	(%r15,%rax),%rbp
	# 		(uint32) b <<<= 9
	rol	$9,%ebp
	# 		x7 ^= b
	xor	%rbp,%r8
	# a = x1 + x2
	lea	(%rdi,%rcx),%rbp
	# (uint32) a <<<= 13
	rol	$13,%ebp
	# x3 ^= a
	xor	%rbp,%rsi
	# 		b = x6 + x7
	lea	(%rax,%r8),%rbp
	# 		(uint32) b <<<= 13
	rol	$13,%ebp
	# 		x4 ^= b
	xor	%rbp,%r9
	# a = x2 + x3
	lea	(%rcx,%rsi),%rbp
	# (uint32) a <<<= 18
	rol	$18,%ebp
	# x0 ^= a
	xor	%rbp,%rdx
	# 		b = x7 + x4
	lea	(%r8,%r9),%rbp
	# 		(uint32) b <<<= 18
	rol	$18,%ebp
	# 		x5 ^= b
	xor	%rbp,%r15
	# 				x10 = x10_stack
	movq	168(%rsp),%rbp
	# 		x5_stack = x5
	movq	%r15,160(%rsp)
	# 				c = x9 + x10
	lea	(%r10,%rbp),%r15
	# 				(uint32) c <<<= 7
	rol	$7,%r15d
	# 				x11 ^= c
	xor	%r15,%r12
	# 				c = x10 + x11
	lea	(%rbp,%r12),%r15
	# 				(uint32) c <<<= 9
	rol	$9,%r15d
	# 				x8 ^= c
	xor	%r15,%r11
	# 				c = x11 + x8
	lea	(%r12,%r11),%r15
	# 				(uint32) c <<<= 13
	rol	$13,%r15d
	# 				x9 ^= c
	xor	%r15,%r10
	# 				c = x8 + x9
	lea	(%r11,%r10),%r15
	# 				(uint32) c <<<= 18
	rol	$18,%r15d
	# 				x10 ^= c
	xor	%r15,%rbp
	# 						x15 = x15_stack
	movq	176(%rsp),%r15
	# 				x10_stack = x10
	movq	%rbp,168(%rsp)
	# 						d = x14 + x15
	lea	(%rbx,%r15),%rbp
	# 						(uint32) d <<<= 7
	rol	$7,%ebp
	# 						x12 ^= d
	xor	%rbp,%r14
	# 						d = x15 + x12
	lea	(%r15,%r14),%rbp
	# 						(uint32) d <<<= 9
	rol	$9,%ebp
	# 						x13 ^= d
	xor	%rbp,%r13
	# 						d = x12 + x13
	lea	(%r14,%r13),%rbp
	# 						(uint32) d <<<= 13
	rol	$13,%ebp
	# 						x14 ^= d
	xor	%rbp,%rbx
	# 						d = x13 + x14
	lea	(%r13,%rbx),%rbp
	# 						(uint32) d <<<= 18
	rol	$18,%ebp
	# 						x15 ^= d
	xor	%rbp,%r15
	# 						x15_stack = x15
	movq	%r15,176(%rsp)
	# 		x5 = x5_stack
	movq	160(%rsp),%r15
	# a = x12 + x0
	lea	(%r14,%rdx),%rbp
	# (uint32) a <<<= 7
	rol	$7,%ebp
	# x4 ^= a
	xor	%rbp,%r9
	# 		b = x1 + x5
	lea	(%rdi,%r15),%rbp
	# 		(uint32) b <<<= 7
	rol	$7,%ebp
	# 		x9 ^= b
	xor	%rbp,%r10
	# a = x0 + x4
	lea	(%rdx,%r9),%rbp
	# (uint32) a <<<= 9
	rol	$9,%ebp
	# x8 ^= a
	xor	%rbp,%r11
	# 		b = x5 + x9
	lea	(%r15,%r10),%rbp
	# 		(uint32) b <<<= 9
	rol	$9,%ebp
	# 		x13 ^= b
	xor	%rbp,%r13
	# a = x4 + x8
	lea	(%r9,%r11),%rbp
	# (uint32) a <<<= 13
	rol	$13,%ebp
	# x12 ^= a
	xor	%rbp,%r14
	# 		b = x9 + x13
	lea	(%r10,%r13),%rbp
	# 		(uint32) b <<<= 13
	rol	$13,%ebp
	# 		x1 ^= b
	xor	%rbp,%rdi
	# a = x8 + x12
	lea	(%r11,%r14),%rbp
	# (uint32) a <<<= 18
	rol	$18,%ebp
	# x0 ^= a
	xor	%rbp,%rdx
	# 		b = x13 + x1
	lea	(%r13,%rdi),%rbp
	# 		(uint32) b <<<= 18
	rol	$18,%ebp
	# 		x5 ^= b
	xor	%rbp,%r15
	# 				x10 = x10_stack
	movq	168(%rsp),%rbp
	# 		x5_stack = x5
	movq	%r15,160(%rsp)
	# 				c = x6 + x10
	lea	(%rax,%rbp),%r15
	# 				(uint32) c <<<= 7
	rol	$7,%r15d
	# 				x14 ^= c
	xor	%r15,%rbx
	# 				c = x10 + x14
	lea	(%rbp,%rbx),%r15
	# 				(uint32) c <<<= 9
	rol	$9,%r15d
	# 				x2 ^= c
	xor	%r15,%rcx
	# 				c = x14 + x2
	lea	(%rbx,%rcx),%r15
	# 				(uint32) c <<<= 13
	rol	$13,%r15d
	# 				x6 ^= c
	xor	%r15,%rax
	# 				c = x2 + x6
	lea	(%rcx,%rax),%r15
	# 				(uint32) c <<<= 18
	rol	$18,%r15d
	# 				x10 ^= c
	xor	%r15,%rbp
	# 						x15 = x15_stack
	movq	176(%rsp),%r15
	# 				x10_stack = x10
	movq	%rbp,168(%rsp)
	# 						d = x11 + x15
	lea	(%r12,%r15),%rbp
	# 						(uint32) d <<<= 7
	rol	$7,%ebp
	# 						x3 ^= d
	xor	%rbp,%rsi
	# 						d = x15 + x3
	lea	(%r15,%rsi),%rbp
	# 						(uint32) d <<<= 9
	rol	$9,%ebp
	# 						x7 ^= d
	xor	%rbp,%r8
	# 						d = x3 + x7
	lea	(%rsi,%r8),%rbp
	# 						(uint32) d <<<= 13
	rol	$13,%ebp
	# 						x11 ^= d
	xor	%rbp,%r12
	# 						d = x7 + x11
	lea	(%r8,%r12),%rbp
	# 						(uint32) d <<<= 18
	rol	$18,%ebp
	# 						x15 ^= d
	xor	%rbp,%r15
	# 						x15_stack = x15
	movq	%r15,176(%rsp)
	# 		x5 = x5_stack
	movq	160(%rsp),%r15
	# a = x3 + x0
	lea	(%rsi,%rdx),%rbp
	# (uint32) a <<<= 7
	rol	$7,%ebp
	# x1 ^= a
	xor	%rbp,%rdi
	# 		b = x4 + x5
	lea	(%r9,%r15),%rbp
	# 		(uint32) b <<<= 7
	rol	$7,%ebp
	# 		x6 ^= b
	xor	%rbp,%rax
	# a = x0 + x1
	lea	(%rdx,%rdi),%rbp
	# (uint32) a <<<= 9
	rol	$9,%ebp
	# x2 ^= a
	xor	%rbp,%rcx
	# 		b = x5 + x6
	lea	(%r15,%rax),%rbp
	# 		(uint32) b <<<= 9
	rol	$9,%ebp
	# 		x7 ^= b
	xor	%rbp,%r8
	# a = x1 + x2
	lea	(%rdi,%rcx),%rbp
	# (uint32) a <<<= 13
	rol	$13,%ebp
	# x3 ^= a
	xor	%rbp,%rsi
	# 		b = x6 + x7
	lea	(%rax,%r8),%rbp
	# 		(uint32) b <<<= 13
	rol	$13,%ebp
	# 		x4 ^= b
	xor	%rbp,%r9
	# a = x2 + x3
	lea	(%rcx,%rsi),%rbp
	# (uint32) a <<<= 18
	rol	$18,%ebp
	# x0 ^= a
	xor	%rbp,%rdx
	# 		b = x7 + x4
	lea	(%r8,%r9),%rbp
	# 		(uint32) b <<<= 18
	rol	$18,%ebp
	# 		x5 ^= b
	xor	%rbp,%r15
	# 				x10 = x10_stack
	movq	168(%rsp),%rbp
	# 		x5_stack = x5
	movq	%r15,160(%rsp)
	# 				c = x9 + x10
	lea	(%r10,%rbp),%r15
	# 				(uint32) c <<<= 7
	rol	$7,%r15d
	# 				x11 ^= c
	xor	%r15,%r12
	# 				c = x10 + x11
	lea	(%rbp,%r12),%r15
	# 				(uint32) c <<<= 9
	rol	$9,%r15d
	# 				x8 ^= c
	xor	%r15,%r11
	# 				c = x11 + x8
	lea	(%r12,%r11),%r15
	# 				(uint32) c <<<= 13
	rol	$13,%r15d
	# 				x9 ^= c
	xor	%r15,%r10
	# 				c = x8 + x9
	lea	(%r11,%r10),%r15
	# 				(uint32) c <<<= 18
	rol	$18,%r15d
	# 				x10 ^= c
	xor	%r15,%rbp
	# 						x15 = x15_stack
	movq	176(%rsp),%r15
	# 				x10_stack = x10
	movq	%rbp,168(%rsp)
	# 						d = x14 + x15
	lea	(%rbx,%r15),%rbp
	# 						(uint32) d <<<= 7
	rol	$7,%ebp
	# 						x12 ^= d
	xor	%rbp,%r14
	# 						d = x15 + x12
	lea	(%r15,%r14),%rbp
	# 						(uint32) d <<<= 9
	rol	$9,%ebp
	# 						x13 ^= d
	xor	%rbp,%r13
	# 						d = x12 + x13
	lea	(%r14,%r13),%rbp
	# 						(uint32) d <<<= 13
	rol	$13,%ebp
	# 						x14 ^= d
	xor	%rbp,%rbx
	# 						d = x13 + x14
	lea	(%r13,%rbx),%rbp
	# 						(uint32) d <<<= 18
	rol	$18,%ebp
	# 						x15 ^= d
	xor	%rbp,%r15
	# 						x15_stack = x15
	movq	%r15,176(%rsp)
	#   i = i_backup
	movq	184(%rsp),%r15
	#                  unsigned>? i -= 4
	sub	$4,%r15
	# comment:fp stack unchanged by jump
	# goto mainloop if unsigned>
	ja	._mainloop
	#   (uint32) x2 += j2
	addl	64(%rsp),%ecx
	#   x3 <<= 32
	shl	$32,%rsi
	#   x3 += j2
	addq	64(%rsp),%rsi
	#   (uint64) x3 >>= 32
	shr	$32,%rsi
	#   x3 <<= 32
	shl	$32,%rsi
	#   x2 += x3
	add	%rsi,%rcx
	#   (uint32) x6 += j6
	addl	80(%rsp),%eax
	#   x7 <<= 32
	shl	$32,%r8
	#   x7 += j6
	addq	80(%rsp),%r8
	#   (uint64) x7 >>= 32
	shr	$32,%r8
	#   x7 <<= 32
	shl	$32,%r8
	#   x6 += x7
	add	%r8,%rax
	#   (uint32) x8 += j8
	addl	88(%rsp),%r11d
	#   x9 <<= 32
	shl	$32,%r10
	#   x9 += j8
	addq	88(%rsp),%r10
	#   (uint64) x9 >>= 32
	shr	$32,%r10
	#   x9 <<= 32
	shl	$32,%r10
	#   x8 += x9
	add	%r10,%r11
	#   (uint32) x12 += j12
	addl	104(%rsp),%r14d
	#   x13 <<= 32
	shl	$32,%r13
	#   x13 += j12
	addq	104(%rsp),%r13
	#   (uint64) x13 >>= 32
	shr	$32,%r13
	#   x13 <<= 32
	shl	$32,%r13
	#   x12 += x13
	add	%r13,%r14
	#   (uint32) x0 += j0
	addl	56(%rsp),%edx
	#   x1 <<= 32
	shl	$32,%rdi
	#   x1 += j0
	addq	56(%rsp),%rdi
	#   (uint64) x1 >>= 32
	shr	$32,%rdi
	#   x1 <<= 32
	shl	$32,%rdi
	#   x0 += x1
	add	%rdi,%rdx
	#   x5 = x5_stack
	movq	160(%rsp),%rdi
	#   (uint32) x4 += j4
	addl	72(%rsp),%r9d
	#   x5 <<= 32
	shl	$32,%rdi
	#   x5 += j4
	addq	72(%rsp),%rdi
	#   (uint64) x5 >>= 32
	shr	$32,%rdi
	#   x5 <<= 32
	shl	$32,%rdi
	#   x4 += x5
	add	%rdi,%r9
	#   x10 = x10_stack
	movq	168(%rsp),%r8
	#   (uint32) x10 += j10
	addl	96(%rsp),%r8d
	#   x11 <<= 32
	shl	$32,%r12
	#   x11 += j10
	addq	96(%rsp),%r12
	#   (uint64) x11 >>= 32
	shr	$32,%r12
	#   x11 <<= 32
	shl	$32,%r12
	#   x10 += x11
	add	%r12,%r8
	#   x15 = x15_stack
	movq	176(%rsp),%rdi
	#   (uint32) x14 += j14
	addl	112(%rsp),%ebx
	#   x15 <<= 32
	shl	$32,%rdi
	#   x15 += j14
	addq	112(%rsp),%rdi
	#   (uint64) x15 >>= 32
	shr	$32,%rdi
	#   x15 <<= 32
	shl	$32,%rdi
	#   x14 += x15
	add	%rdi,%rbx
	#   out = out_backup
	movq	136(%rsp),%rdi
	#   m = m_backup
	movq	144(%rsp),%rsi
	#   x0 ^= *(uint64 *) (m + 0)
	xorq	0(%rsi),%rdx
	#   *(uint64 *) (out + 0) = x0
	movq	%rdx,0(%rdi)
	#   x2 ^= *(uint64 *) (m + 8)
	xorq	8(%rsi),%rcx
	#   *(uint64 *) (out + 8) = x2
	movq	%rcx,8(%rdi)
	#   x4 ^= *(uint64 *) (m + 16)
	xorq	16(%rsi),%r9
	#   *(uint64 *) (out + 16) = x4
	movq	%r9,16(%rdi)
	#   x6 ^= *(uint64 *) (m + 24)
	xorq	24(%rsi),%rax
	#   *(uint64 *) (out + 24) = x6
	movq	%rax,24(%rdi)
	#   x8 ^= *(uint64 *) (m + 32)
	xorq	32(%rsi),%r11
	#   *(uint64 *) (out + 32) = x8
	movq	%r11,32(%rdi)
	#   x10 ^= *(uint64 *) (m + 40)
	xorq	40(%rsi),%r8
	#   *(uint64 *) (out + 40) = x10
	movq	%r8,40(%rdi)
	#   x12 ^= *(uint64 *) (m + 48)
	xorq	48(%rsi),%r14
	#   *(uint64 *) (out + 48) = x12
	movq	%r14,48(%rdi)
	#   x14 ^= *(uint64 *) (m + 56)
	xorq	56(%rsi),%rbx
	#   *(uint64 *) (out + 56) = x14
	movq	%rbx,56(%rdi)
	#   bytes = bytes_backup
	movq	152(%rsp),%rdx
	#   in8 = j8
	movq	88(%rsp),%rcx
	#   in8 += 1
	add	$1,%rcx
	#   j8 = in8
	movq	%rcx,88(%rsp)
	#                          unsigned>? unsigned<? bytes - 64
	cmp	$64,%rdx
	# comment:fp stack unchanged by jump
	#   goto bytesatleast65 if unsigned>
	ja	._bytesatleast65
	# comment:fp stack unchanged by jump
	#     goto bytesatleast64 if !unsigned<
	jae	._bytesatleast64
	#       m = out
	mov	%rdi,%rsi
	#       out = ctarget
	movq	128(%rsp),%rdi
	#       i = bytes
	mov	%rdx,%rcx
	#       while (i) { *out++ = *m++; --i }
	rep	movsb
	# comment:fp stack unchanged by fallthrough
#     bytesatleast64:
._bytesatleast64:
	#     x = x_backup
	movq	120(%rsp),%rdi
	#     in8 = j8
	movq	88(%rsp),%rsi
	#     *(uint64 *) (x + 32) = in8
	movq	%rsi,32(%rdi)
	#     r11 = r11_stack
	movq	0(%rsp),%r11
	#     r12 = r12_stack
	movq	8(%rsp),%r12
	#     r13 = r13_stack
	movq	16(%rsp),%r13
	#     r14 = r14_stack
	movq	24(%rsp),%r14
	#     r15 = r15_stack
	movq	32(%rsp),%r15
	#     rbx = rbx_stack
	movq	40(%rsp),%rbx
	#     rbp = rbp_stack
	movq	48(%rsp),%rbp
	# comment:fp stack unchanged by fallthrough
#     done:
._done:
	#     leave
	add	%r11,%rsp
	mov	%rdi,%rax
	mov	%rsi,%rdx
	ret
#   bytesatleast65:
._bytesatleast65:
	#   bytes -= 64
	sub	$64,%rdx
	#   out += 64
	add	$64,%rdi
	#   m += 64
	add	$64,%rsi
	# comment:fp stack unchanged by jump
	# goto bytesatleast1
	jmp	._bytesatleast1
ENDPROC(salsa20_encrypt_bytes)

# enter salsa20_keysetup
ENTRY(salsa20_keysetup)
	mov	%rsp,%r11
	and	$31,%r11
	add	$256,%r11
	sub	%r11,%rsp
	#   k = arg2
	mov	%rsi,%rsi
	#   kbits = arg3
	mov	%rdx,%rdx
	#   x = arg1
	mov	%rdi,%rdi
	#   in0 = *(uint64 *) (k + 0)
	movq	0(%rsi),%r8
	#   in2 = *(uint64 *) (k + 8)
	movq	8(%rsi),%r9
	#   *(uint64 *) (x + 4) = in0
	movq	%r8,4(%rdi)
	#   *(uint64 *) (x + 12) = in2
	movq	%r9,12(%rdi)
	#                    unsigned<? kbits - 256
	cmp	$256,%rdx
	# comment:fp stack unchanged by jump
	#   goto kbits128 if unsigned<
	jb	._kbits128
#   kbits256:
._kbits256:
	#     in10 = *(uint64 *) (k + 16)
	movq	16(%rsi),%rdx
	#     in12 = *(uint64 *) (k + 24)
	movq	24(%rsi),%rsi
	#     *(uint64 *) (x + 44) = in10
	movq	%rdx,44(%rdi)
	#     *(uint64 *) (x + 52) = in12
	movq	%rsi,52(%rdi)
	#     in0 = 1634760805
	mov	$1634760805,%rsi
	#     in4 = 857760878
	mov	$857760878,%rdx
	#     in10 = 2036477234
	mov	$2036477234,%rcx
	#     in14 = 1797285236
	mov	$1797285236,%r8
	#     *(uint32 *) (x + 0) = in0
	movl	%esi,0(%rdi)
	#     *(uint32 *) (x + 20) = in4
	movl	%edx,20(%rdi)
	#     *(uint32 *) (x + 40) = in10
	movl	%ecx,40(%rdi)
	#     *(uint32 *) (x + 60) = in14
	movl	%r8d,60(%rdi)
	# comment:fp stack unchanged by jump
	#   goto keysetupdone
	jmp	._keysetupdone
#   kbits128:
._kbits128:
	#     in10 = *(uint64 *) (k + 0)
	movq	0(%rsi),%rdx
	#     in12 = *(uint64 *) (k + 8)
	movq	8(%rsi),%rsi
	#     *(uint64 *) (x + 44) = in10
	movq	%rdx,44(%rdi)
	#     *(uint64 *) (x + 52) = in12
	movq	%rsi,52(%rdi)
	#     in0 = 1634760805
	mov	$1634760805,%rsi
	#     in4 = 824206446
	mov	$824206446,%rdx
	#     in10 = 2036477238
	mov	$2036477238,%rcx
	#     in14 = 1797285236
	mov	$1797285236,%r8
	#     *(uint32 *) (x + 0) = in0
	movl	%esi,0(%rdi)
	#     *(uint32 *) (x + 20) = in4
	movl	%edx,20(%rdi)
	#     *(uint32 *) (x + 40) = in10
	movl	%ecx,40(%rdi)
	#     *(uint32 *) (x + 60) = in14
	movl	%r8d,60(%rdi)
#   keysetupdone:
._keysetupdone:
	# leave
	add	%r11,%rsp
	mov	%rdi,%rax
	mov	%rsi,%rdx
	ret
ENDPROC(salsa20_keysetup)

# enter salsa20_ivsetup
ENTRY(salsa20_ivsetup)
	mov	%rsp,%r11
	and	$31,%r11
	add	$256,%r11
	sub	%r11,%rsp
	#   iv = arg2
	mov	%rsi,%rsi
	#   x = arg1
	mov	%rdi,%rdi
	#   in6 = *(uint64 *) (iv + 0)
	movq	0(%rsi),%rsi
	#   in8 = 0
	mov	$0,%r8
	#   *(uint64 *) (x + 24) = in6
	movq	%rsi,24(%rdi)
	#   *(uint64 *) (x + 32) = in8
	movq	%r8,32(%rdi)
	# leave
	add	%r11,%rsp
	mov	%rdi,%rax
	mov	%rsi,%rdx
	ret
ENDPROC(salsa20_ivsetup)

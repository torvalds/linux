# salsa20_pm.s version 20051229
# D. J. Bernstein
# Public domain.

# enter ECRYPT_encrypt_bytes
.text
.p2align 5
.globl ECRYPT_encrypt_bytes
ECRYPT_encrypt_bytes:
	mov	%esp,%eax
	and	$31,%eax
	add	$256,%eax
	sub	%eax,%esp
	# eax_stack = eax
	movl	%eax,80(%esp)
	# ebx_stack = ebx
	movl	%ebx,84(%esp)
	# esi_stack = esi
	movl	%esi,88(%esp)
	# edi_stack = edi
	movl	%edi,92(%esp)
	# ebp_stack = ebp
	movl	%ebp,96(%esp)
	# x = arg1
	movl	4(%esp,%eax),%edx
	# m = arg2
	movl	8(%esp,%eax),%esi
	# out = arg3
	movl	12(%esp,%eax),%edi
	# bytes = arg4
	movl	16(%esp,%eax),%ebx
	# bytes -= 0
	sub	$0,%ebx
	# goto done if unsigned<=
	jbe	._done
._start:
	# in0 = *(uint32 *) (x + 0)
	movl	0(%edx),%eax
	# in1 = *(uint32 *) (x + 4)
	movl	4(%edx),%ecx
	# in2 = *(uint32 *) (x + 8)
	movl	8(%edx),%ebp
	# j0 = in0
	movl	%eax,164(%esp)
	# in3 = *(uint32 *) (x + 12)
	movl	12(%edx),%eax
	# j1 = in1
	movl	%ecx,168(%esp)
	# in4 = *(uint32 *) (x + 16)
	movl	16(%edx),%ecx
	# j2 = in2
	movl	%ebp,172(%esp)
	# in5 = *(uint32 *) (x + 20)
	movl	20(%edx),%ebp
	# j3 = in3
	movl	%eax,176(%esp)
	# in6 = *(uint32 *) (x + 24)
	movl	24(%edx),%eax
	# j4 = in4
	movl	%ecx,180(%esp)
	# in7 = *(uint32 *) (x + 28)
	movl	28(%edx),%ecx
	# j5 = in5
	movl	%ebp,184(%esp)
	# in8 = *(uint32 *) (x + 32)
	movl	32(%edx),%ebp
	# j6 = in6
	movl	%eax,188(%esp)
	# in9 = *(uint32 *) (x + 36)
	movl	36(%edx),%eax
	# j7 = in7
	movl	%ecx,192(%esp)
	# in10 = *(uint32 *) (x + 40)
	movl	40(%edx),%ecx
	# j8 = in8
	movl	%ebp,196(%esp)
	# in11 = *(uint32 *) (x + 44)
	movl	44(%edx),%ebp
	# j9 = in9
	movl	%eax,200(%esp)
	# in12 = *(uint32 *) (x + 48)
	movl	48(%edx),%eax
	# j10 = in10
	movl	%ecx,204(%esp)
	# in13 = *(uint32 *) (x + 52)
	movl	52(%edx),%ecx
	# j11 = in11
	movl	%ebp,208(%esp)
	# in14 = *(uint32 *) (x + 56)
	movl	56(%edx),%ebp
	# j12 = in12
	movl	%eax,212(%esp)
	# in15 = *(uint32 *) (x + 60)
	movl	60(%edx),%eax
	# j13 = in13
	movl	%ecx,216(%esp)
	# j14 = in14
	movl	%ebp,220(%esp)
	# j15 = in15
	movl	%eax,224(%esp)
	# x_backup = x
	movl	%edx,64(%esp)
._bytesatleast1:
	#   bytes - 64
	cmp	$64,%ebx
	#   goto nocopy if unsigned>=
	jae	._nocopy
	#     ctarget = out
	movl	%edi,228(%esp)
	#     out = &tmp
	leal	0(%esp),%edi
	#     i = bytes
	mov	%ebx,%ecx
	#     while (i) { *out++ = *m++; --i }
	rep	movsb
	#     out = &tmp
	leal	0(%esp),%edi
	#     m = &tmp
	leal	0(%esp),%esi
._nocopy:
	#   out_backup = out
	movl	%edi,72(%esp)
	#   m_backup = m
	movl	%esi,68(%esp)
	#   bytes_backup = bytes
	movl	%ebx,76(%esp)
	#   in0 = j0
	movl	164(%esp),%eax
	#   in1 = j1
	movl	168(%esp),%ecx
	#   in2 = j2
	movl	172(%esp),%edx
	#   in3 = j3
	movl	176(%esp),%ebx
	#   x0 = in0
	movl	%eax,100(%esp)
	#   x1 = in1
	movl	%ecx,104(%esp)
	#   x2 = in2
	movl	%edx,108(%esp)
	#   x3 = in3
	movl	%ebx,112(%esp)
	#   in4 = j4
	movl	180(%esp),%eax
	#   in5 = j5
	movl	184(%esp),%ecx
	#   in6 = j6
	movl	188(%esp),%edx
	#   in7 = j7
	movl	192(%esp),%ebx
	#   x4 = in4
	movl	%eax,116(%esp)
	#   x5 = in5
	movl	%ecx,120(%esp)
	#   x6 = in6
	movl	%edx,124(%esp)
	#   x7 = in7
	movl	%ebx,128(%esp)
	#   in8 = j8
	movl	196(%esp),%eax
	#   in9 = j9
	movl	200(%esp),%ecx
	#   in10 = j10
	movl	204(%esp),%edx
	#   in11 = j11
	movl	208(%esp),%ebx
	#   x8 = in8
	movl	%eax,132(%esp)
	#   x9 = in9
	movl	%ecx,136(%esp)
	#   x10 = in10
	movl	%edx,140(%esp)
	#   x11 = in11
	movl	%ebx,144(%esp)
	#   in12 = j12
	movl	212(%esp),%eax
	#   in13 = j13
	movl	216(%esp),%ecx
	#   in14 = j14
	movl	220(%esp),%edx
	#   in15 = j15
	movl	224(%esp),%ebx
	#   x12 = in12
	movl	%eax,148(%esp)
	#   x13 = in13
	movl	%ecx,152(%esp)
	#   x14 = in14
	movl	%edx,156(%esp)
	#   x15 = in15
	movl	%ebx,160(%esp)
	#   i = 20
	mov	$20,%ebp
	# p = x0
	movl	100(%esp),%eax
	# s = x5
	movl	120(%esp),%ecx
	# t = x10
	movl	140(%esp),%edx
	# w = x15
	movl	160(%esp),%ebx
._mainloop:
	# x0 = p
	movl	%eax,100(%esp)
	# 				x10 = t
	movl	%edx,140(%esp)
	# p += x12
	addl	148(%esp),%eax
	# 		x5 = s
	movl	%ecx,120(%esp)
	# 				t += x6
	addl	124(%esp),%edx
	# 						x15 = w
	movl	%ebx,160(%esp)
	# 		r = x1
	movl	104(%esp),%esi
	# 		r += s
	add	%ecx,%esi
	# 						v = x11
	movl	144(%esp),%edi
	# 						v += w
	add	%ebx,%edi
	# p <<<= 7
	rol	$7,%eax
	# p ^= x4
	xorl	116(%esp),%eax
	# 				t <<<= 7
	rol	$7,%edx
	# 				t ^= x14
	xorl	156(%esp),%edx
	# 		r <<<= 7
	rol	$7,%esi
	# 		r ^= x9
	xorl	136(%esp),%esi
	# 						v <<<= 7
	rol	$7,%edi
	# 						v ^= x3
	xorl	112(%esp),%edi
	# x4 = p
	movl	%eax,116(%esp)
	# 				x14 = t
	movl	%edx,156(%esp)
	# p += x0
	addl	100(%esp),%eax
	# 		x9 = r
	movl	%esi,136(%esp)
	# 				t += x10
	addl	140(%esp),%edx
	# 						x3 = v
	movl	%edi,112(%esp)
	# p <<<= 9
	rol	$9,%eax
	# p ^= x8
	xorl	132(%esp),%eax
	# 				t <<<= 9
	rol	$9,%edx
	# 				t ^= x2
	xorl	108(%esp),%edx
	# 		s += r
	add	%esi,%ecx
	# 		s <<<= 9
	rol	$9,%ecx
	# 		s ^= x13
	xorl	152(%esp),%ecx
	# 						w += v
	add	%edi,%ebx
	# 						w <<<= 9
	rol	$9,%ebx
	# 						w ^= x7
	xorl	128(%esp),%ebx
	# x8 = p
	movl	%eax,132(%esp)
	# 				x2 = t
	movl	%edx,108(%esp)
	# p += x4
	addl	116(%esp),%eax
	# 		x13 = s
	movl	%ecx,152(%esp)
	# 				t += x14
	addl	156(%esp),%edx
	# 						x7 = w
	movl	%ebx,128(%esp)
	# p <<<= 13
	rol	$13,%eax
	# p ^= x12
	xorl	148(%esp),%eax
	# 				t <<<= 13
	rol	$13,%edx
	# 				t ^= x6
	xorl	124(%esp),%edx
	# 		r += s
	add	%ecx,%esi
	# 		r <<<= 13
	rol	$13,%esi
	# 		r ^= x1
	xorl	104(%esp),%esi
	# 						v += w
	add	%ebx,%edi
	# 						v <<<= 13
	rol	$13,%edi
	# 						v ^= x11
	xorl	144(%esp),%edi
	# x12 = p
	movl	%eax,148(%esp)
	# 				x6 = t
	movl	%edx,124(%esp)
	# p += x8
	addl	132(%esp),%eax
	# 		x1 = r
	movl	%esi,104(%esp)
	# 				t += x2
	addl	108(%esp),%edx
	# 						x11 = v
	movl	%edi,144(%esp)
	# p <<<= 18
	rol	$18,%eax
	# p ^= x0
	xorl	100(%esp),%eax
	# 				t <<<= 18
	rol	$18,%edx
	# 				t ^= x10
	xorl	140(%esp),%edx
	# 		s += r
	add	%esi,%ecx
	# 		s <<<= 18
	rol	$18,%ecx
	# 		s ^= x5
	xorl	120(%esp),%ecx
	# 						w += v
	add	%edi,%ebx
	# 						w <<<= 18
	rol	$18,%ebx
	# 						w ^= x15
	xorl	160(%esp),%ebx
	# x0 = p
	movl	%eax,100(%esp)
	# 				x10 = t
	movl	%edx,140(%esp)
	# p += x3
	addl	112(%esp),%eax
	# p <<<= 7
	rol	$7,%eax
	# 		x5 = s
	movl	%ecx,120(%esp)
	# 				t += x9
	addl	136(%esp),%edx
	# 						x15 = w
	movl	%ebx,160(%esp)
	# 		r = x4
	movl	116(%esp),%esi
	# 		r += s
	add	%ecx,%esi
	# 						v = x14
	movl	156(%esp),%edi
	# 						v += w
	add	%ebx,%edi
	# p ^= x1
	xorl	104(%esp),%eax
	# 				t <<<= 7
	rol	$7,%edx
	# 				t ^= x11
	xorl	144(%esp),%edx
	# 		r <<<= 7
	rol	$7,%esi
	# 		r ^= x6
	xorl	124(%esp),%esi
	# 						v <<<= 7
	rol	$7,%edi
	# 						v ^= x12
	xorl	148(%esp),%edi
	# x1 = p
	movl	%eax,104(%esp)
	# 				x11 = t
	movl	%edx,144(%esp)
	# p += x0
	addl	100(%esp),%eax
	# 		x6 = r
	movl	%esi,124(%esp)
	# 				t += x10
	addl	140(%esp),%edx
	# 						x12 = v
	movl	%edi,148(%esp)
	# p <<<= 9
	rol	$9,%eax
	# p ^= x2
	xorl	108(%esp),%eax
	# 				t <<<= 9
	rol	$9,%edx
	# 				t ^= x8
	xorl	132(%esp),%edx
	# 		s += r
	add	%esi,%ecx
	# 		s <<<= 9
	rol	$9,%ecx
	# 		s ^= x7
	xorl	128(%esp),%ecx
	# 						w += v
	add	%edi,%ebx
	# 						w <<<= 9
	rol	$9,%ebx
	# 						w ^= x13
	xorl	152(%esp),%ebx
	# x2 = p
	movl	%eax,108(%esp)
	# 				x8 = t
	movl	%edx,132(%esp)
	# p += x1
	addl	104(%esp),%eax
	# 		x7 = s
	movl	%ecx,128(%esp)
	# 				t += x11
	addl	144(%esp),%edx
	# 						x13 = w
	movl	%ebx,152(%esp)
	# p <<<= 13
	rol	$13,%eax
	# p ^= x3
	xorl	112(%esp),%eax
	# 				t <<<= 13
	rol	$13,%edx
	# 				t ^= x9
	xorl	136(%esp),%edx
	# 		r += s
	add	%ecx,%esi
	# 		r <<<= 13
	rol	$13,%esi
	# 		r ^= x4
	xorl	116(%esp),%esi
	# 						v += w
	add	%ebx,%edi
	# 						v <<<= 13
	rol	$13,%edi
	# 						v ^= x14
	xorl	156(%esp),%edi
	# x3 = p
	movl	%eax,112(%esp)
	# 				x9 = t
	movl	%edx,136(%esp)
	# p += x2
	addl	108(%esp),%eax
	# 		x4 = r
	movl	%esi,116(%esp)
	# 				t += x8
	addl	132(%esp),%edx
	# 						x14 = v
	movl	%edi,156(%esp)
	# p <<<= 18
	rol	$18,%eax
	# p ^= x0
	xorl	100(%esp),%eax
	# 				t <<<= 18
	rol	$18,%edx
	# 				t ^= x10
	xorl	140(%esp),%edx
	# 		s += r
	add	%esi,%ecx
	# 		s <<<= 18
	rol	$18,%ecx
	# 		s ^= x5
	xorl	120(%esp),%ecx
	# 						w += v
	add	%edi,%ebx
	# 						w <<<= 18
	rol	$18,%ebx
	# 						w ^= x15
	xorl	160(%esp),%ebx
	# x0 = p
	movl	%eax,100(%esp)
	# 				x10 = t
	movl	%edx,140(%esp)
	# p += x12
	addl	148(%esp),%eax
	# 		x5 = s
	movl	%ecx,120(%esp)
	# 				t += x6
	addl	124(%esp),%edx
	# 						x15 = w
	movl	%ebx,160(%esp)
	# 		r = x1
	movl	104(%esp),%esi
	# 		r += s
	add	%ecx,%esi
	# 						v = x11
	movl	144(%esp),%edi
	# 						v += w
	add	%ebx,%edi
	# p <<<= 7
	rol	$7,%eax
	# p ^= x4
	xorl	116(%esp),%eax
	# 				t <<<= 7
	rol	$7,%edx
	# 				t ^= x14
	xorl	156(%esp),%edx
	# 		r <<<= 7
	rol	$7,%esi
	# 		r ^= x9
	xorl	136(%esp),%esi
	# 						v <<<= 7
	rol	$7,%edi
	# 						v ^= x3
	xorl	112(%esp),%edi
	# x4 = p
	movl	%eax,116(%esp)
	# 				x14 = t
	movl	%edx,156(%esp)
	# p += x0
	addl	100(%esp),%eax
	# 		x9 = r
	movl	%esi,136(%esp)
	# 				t += x10
	addl	140(%esp),%edx
	# 						x3 = v
	movl	%edi,112(%esp)
	# p <<<= 9
	rol	$9,%eax
	# p ^= x8
	xorl	132(%esp),%eax
	# 				t <<<= 9
	rol	$9,%edx
	# 				t ^= x2
	xorl	108(%esp),%edx
	# 		s += r
	add	%esi,%ecx
	# 		s <<<= 9
	rol	$9,%ecx
	# 		s ^= x13
	xorl	152(%esp),%ecx
	# 						w += v
	add	%edi,%ebx
	# 						w <<<= 9
	rol	$9,%ebx
	# 						w ^= x7
	xorl	128(%esp),%ebx
	# x8 = p
	movl	%eax,132(%esp)
	# 				x2 = t
	movl	%edx,108(%esp)
	# p += x4
	addl	116(%esp),%eax
	# 		x13 = s
	movl	%ecx,152(%esp)
	# 				t += x14
	addl	156(%esp),%edx
	# 						x7 = w
	movl	%ebx,128(%esp)
	# p <<<= 13
	rol	$13,%eax
	# p ^= x12
	xorl	148(%esp),%eax
	# 				t <<<= 13
	rol	$13,%edx
	# 				t ^= x6
	xorl	124(%esp),%edx
	# 		r += s
	add	%ecx,%esi
	# 		r <<<= 13
	rol	$13,%esi
	# 		r ^= x1
	xorl	104(%esp),%esi
	# 						v += w
	add	%ebx,%edi
	# 						v <<<= 13
	rol	$13,%edi
	# 						v ^= x11
	xorl	144(%esp),%edi
	# x12 = p
	movl	%eax,148(%esp)
	# 				x6 = t
	movl	%edx,124(%esp)
	# p += x8
	addl	132(%esp),%eax
	# 		x1 = r
	movl	%esi,104(%esp)
	# 				t += x2
	addl	108(%esp),%edx
	# 						x11 = v
	movl	%edi,144(%esp)
	# p <<<= 18
	rol	$18,%eax
	# p ^= x0
	xorl	100(%esp),%eax
	# 				t <<<= 18
	rol	$18,%edx
	# 				t ^= x10
	xorl	140(%esp),%edx
	# 		s += r
	add	%esi,%ecx
	# 		s <<<= 18
	rol	$18,%ecx
	# 		s ^= x5
	xorl	120(%esp),%ecx
	# 						w += v
	add	%edi,%ebx
	# 						w <<<= 18
	rol	$18,%ebx
	# 						w ^= x15
	xorl	160(%esp),%ebx
	# x0 = p
	movl	%eax,100(%esp)
	# 				x10 = t
	movl	%edx,140(%esp)
	# p += x3
	addl	112(%esp),%eax
	# p <<<= 7
	rol	$7,%eax
	# 		x5 = s
	movl	%ecx,120(%esp)
	# 				t += x9
	addl	136(%esp),%edx
	# 						x15 = w
	movl	%ebx,160(%esp)
	# 		r = x4
	movl	116(%esp),%esi
	# 		r += s
	add	%ecx,%esi
	# 						v = x14
	movl	156(%esp),%edi
	# 						v += w
	add	%ebx,%edi
	# p ^= x1
	xorl	104(%esp),%eax
	# 				t <<<= 7
	rol	$7,%edx
	# 				t ^= x11
	xorl	144(%esp),%edx
	# 		r <<<= 7
	rol	$7,%esi
	# 		r ^= x6
	xorl	124(%esp),%esi
	# 						v <<<= 7
	rol	$7,%edi
	# 						v ^= x12
	xorl	148(%esp),%edi
	# x1 = p
	movl	%eax,104(%esp)
	# 				x11 = t
	movl	%edx,144(%esp)
	# p += x0
	addl	100(%esp),%eax
	# 		x6 = r
	movl	%esi,124(%esp)
	# 				t += x10
	addl	140(%esp),%edx
	# 						x12 = v
	movl	%edi,148(%esp)
	# p <<<= 9
	rol	$9,%eax
	# p ^= x2
	xorl	108(%esp),%eax
	# 				t <<<= 9
	rol	$9,%edx
	# 				t ^= x8
	xorl	132(%esp),%edx
	# 		s += r
	add	%esi,%ecx
	# 		s <<<= 9
	rol	$9,%ecx
	# 		s ^= x7
	xorl	128(%esp),%ecx
	# 						w += v
	add	%edi,%ebx
	# 						w <<<= 9
	rol	$9,%ebx
	# 						w ^= x13
	xorl	152(%esp),%ebx
	# x2 = p
	movl	%eax,108(%esp)
	# 				x8 = t
	movl	%edx,132(%esp)
	# p += x1
	addl	104(%esp),%eax
	# 		x7 = s
	movl	%ecx,128(%esp)
	# 				t += x11
	addl	144(%esp),%edx
	# 						x13 = w
	movl	%ebx,152(%esp)
	# p <<<= 13
	rol	$13,%eax
	# p ^= x3
	xorl	112(%esp),%eax
	# 				t <<<= 13
	rol	$13,%edx
	# 				t ^= x9
	xorl	136(%esp),%edx
	# 		r += s
	add	%ecx,%esi
	# 		r <<<= 13
	rol	$13,%esi
	# 		r ^= x4
	xorl	116(%esp),%esi
	# 						v += w
	add	%ebx,%edi
	# 						v <<<= 13
	rol	$13,%edi
	# 						v ^= x14
	xorl	156(%esp),%edi
	# x3 = p
	movl	%eax,112(%esp)
	# 				x9 = t
	movl	%edx,136(%esp)
	# p += x2
	addl	108(%esp),%eax
	# 		x4 = r
	movl	%esi,116(%esp)
	# 				t += x8
	addl	132(%esp),%edx
	# 						x14 = v
	movl	%edi,156(%esp)
	# p <<<= 18
	rol	$18,%eax
	# p ^= x0
	xorl	100(%esp),%eax
	# 				t <<<= 18
	rol	$18,%edx
	# 				t ^= x10
	xorl	140(%esp),%edx
	# 		s += r
	add	%esi,%ecx
	# 		s <<<= 18
	rol	$18,%ecx
	# 		s ^= x5
	xorl	120(%esp),%ecx
	# 						w += v
	add	%edi,%ebx
	# 						w <<<= 18
	rol	$18,%ebx
	# 						w ^= x15
	xorl	160(%esp),%ebx
	# i -= 4
	sub	$4,%ebp
	# goto mainloop if unsigned >
	ja	._mainloop
	# x0 = p
	movl	%eax,100(%esp)
	# x5 = s
	movl	%ecx,120(%esp)
	# x10 = t
	movl	%edx,140(%esp)
	# x15 = w
	movl	%ebx,160(%esp)
	#   out = out_backup
	movl	72(%esp),%edi
	#   m = m_backup
	movl	68(%esp),%esi
	#   in0 = x0
	movl	100(%esp),%eax
	#   in1 = x1
	movl	104(%esp),%ecx
	#   in0 += j0
	addl	164(%esp),%eax
	#   in1 += j1
	addl	168(%esp),%ecx
	#   in0 ^= *(uint32 *) (m + 0)
	xorl	0(%esi),%eax
	#   in1 ^= *(uint32 *) (m + 4)
	xorl	4(%esi),%ecx
	#   *(uint32 *) (out + 0) = in0
	movl	%eax,0(%edi)
	#   *(uint32 *) (out + 4) = in1
	movl	%ecx,4(%edi)
	#   in2 = x2
	movl	108(%esp),%eax
	#   in3 = x3
	movl	112(%esp),%ecx
	#   in2 += j2
	addl	172(%esp),%eax
	#   in3 += j3
	addl	176(%esp),%ecx
	#   in2 ^= *(uint32 *) (m + 8)
	xorl	8(%esi),%eax
	#   in3 ^= *(uint32 *) (m + 12)
	xorl	12(%esi),%ecx
	#   *(uint32 *) (out + 8) = in2
	movl	%eax,8(%edi)
	#   *(uint32 *) (out + 12) = in3
	movl	%ecx,12(%edi)
	#   in4 = x4
	movl	116(%esp),%eax
	#   in5 = x5
	movl	120(%esp),%ecx
	#   in4 += j4
	addl	180(%esp),%eax
	#   in5 += j5
	addl	184(%esp),%ecx
	#   in4 ^= *(uint32 *) (m + 16)
	xorl	16(%esi),%eax
	#   in5 ^= *(uint32 *) (m + 20)
	xorl	20(%esi),%ecx
	#   *(uint32 *) (out + 16) = in4
	movl	%eax,16(%edi)
	#   *(uint32 *) (out + 20) = in5
	movl	%ecx,20(%edi)
	#   in6 = x6
	movl	124(%esp),%eax
	#   in7 = x7
	movl	128(%esp),%ecx
	#   in6 += j6
	addl	188(%esp),%eax
	#   in7 += j7
	addl	192(%esp),%ecx
	#   in6 ^= *(uint32 *) (m + 24)
	xorl	24(%esi),%eax
	#   in7 ^= *(uint32 *) (m + 28)
	xorl	28(%esi),%ecx
	#   *(uint32 *) (out + 24) = in6
	movl	%eax,24(%edi)
	#   *(uint32 *) (out + 28) = in7
	movl	%ecx,28(%edi)
	#   in8 = x8
	movl	132(%esp),%eax
	#   in9 = x9
	movl	136(%esp),%ecx
	#   in8 += j8
	addl	196(%esp),%eax
	#   in9 += j9
	addl	200(%esp),%ecx
	#   in8 ^= *(uint32 *) (m + 32)
	xorl	32(%esi),%eax
	#   in9 ^= *(uint32 *) (m + 36)
	xorl	36(%esi),%ecx
	#   *(uint32 *) (out + 32) = in8
	movl	%eax,32(%edi)
	#   *(uint32 *) (out + 36) = in9
	movl	%ecx,36(%edi)
	#   in10 = x10
	movl	140(%esp),%eax
	#   in11 = x11
	movl	144(%esp),%ecx
	#   in10 += j10
	addl	204(%esp),%eax
	#   in11 += j11
	addl	208(%esp),%ecx
	#   in10 ^= *(uint32 *) (m + 40)
	xorl	40(%esi),%eax
	#   in11 ^= *(uint32 *) (m + 44)
	xorl	44(%esi),%ecx
	#   *(uint32 *) (out + 40) = in10
	movl	%eax,40(%edi)
	#   *(uint32 *) (out + 44) = in11
	movl	%ecx,44(%edi)
	#   in12 = x12
	movl	148(%esp),%eax
	#   in13 = x13
	movl	152(%esp),%ecx
	#   in12 += j12
	addl	212(%esp),%eax
	#   in13 += j13
	addl	216(%esp),%ecx
	#   in12 ^= *(uint32 *) (m + 48)
	xorl	48(%esi),%eax
	#   in13 ^= *(uint32 *) (m + 52)
	xorl	52(%esi),%ecx
	#   *(uint32 *) (out + 48) = in12
	movl	%eax,48(%edi)
	#   *(uint32 *) (out + 52) = in13
	movl	%ecx,52(%edi)
	#   in14 = x14
	movl	156(%esp),%eax
	#   in15 = x15
	movl	160(%esp),%ecx
	#   in14 += j14
	addl	220(%esp),%eax
	#   in15 += j15
	addl	224(%esp),%ecx
	#   in14 ^= *(uint32 *) (m + 56)
	xorl	56(%esi),%eax
	#   in15 ^= *(uint32 *) (m + 60)
	xorl	60(%esi),%ecx
	#   *(uint32 *) (out + 56) = in14
	movl	%eax,56(%edi)
	#   *(uint32 *) (out + 60) = in15
	movl	%ecx,60(%edi)
	#   bytes = bytes_backup
	movl	76(%esp),%ebx
	#   in8 = j8
	movl	196(%esp),%eax
	#   in9 = j9
	movl	200(%esp),%ecx
	#   in8 += 1
	add	$1,%eax
	#   in9 += 0 + carry
	adc	$0,%ecx
	#   j8 = in8
	movl	%eax,196(%esp)
	#   j9 = in9
	movl	%ecx,200(%esp)
	#   bytes - 64
	cmp	$64,%ebx
	#   goto bytesatleast65 if unsigned>
	ja	._bytesatleast65
	#     goto bytesatleast64 if unsigned>=
	jae	._bytesatleast64
	#       m = out
	mov	%edi,%esi
	#       out = ctarget
	movl	228(%esp),%edi
	#       i = bytes
	mov	%ebx,%ecx
	#       while (i) { *out++ = *m++; --i }
	rep	movsb
._bytesatleast64:
	#     x = x_backup
	movl	64(%esp),%eax
	#     in8 = j8
	movl	196(%esp),%ecx
	#     in9 = j9
	movl	200(%esp),%edx
	#     *(uint32 *) (x + 32) = in8
	movl	%ecx,32(%eax)
	#     *(uint32 *) (x + 36) = in9
	movl	%edx,36(%eax)
._done:
	#     eax = eax_stack
	movl	80(%esp),%eax
	#     ebx = ebx_stack
	movl	84(%esp),%ebx
	#     esi = esi_stack
	movl	88(%esp),%esi
	#     edi = edi_stack
	movl	92(%esp),%edi
	#     ebp = ebp_stack
	movl	96(%esp),%ebp
	#     leave
	add	%eax,%esp
	ret
._bytesatleast65:
	#   bytes -= 64
	sub	$64,%ebx
	#   out += 64
	add	$64,%edi
	#   m += 64
	add	$64,%esi
	# goto bytesatleast1
	jmp	._bytesatleast1
# enter ECRYPT_keysetup
.text
.p2align 5
.globl ECRYPT_keysetup
ECRYPT_keysetup:
	mov	%esp,%eax
	and	$31,%eax
	add	$256,%eax
	sub	%eax,%esp
	#   eax_stack = eax
	movl	%eax,64(%esp)
	#   ebx_stack = ebx
	movl	%ebx,68(%esp)
	#   esi_stack = esi
	movl	%esi,72(%esp)
	#   edi_stack = edi
	movl	%edi,76(%esp)
	#   ebp_stack = ebp
	movl	%ebp,80(%esp)
	#   k = arg2
	movl	8(%esp,%eax),%ecx
	#   kbits = arg3
	movl	12(%esp,%eax),%edx
	#   x = arg1
	movl	4(%esp,%eax),%eax
	#   in1 = *(uint32 *) (k + 0)
	movl	0(%ecx),%ebx
	#   in2 = *(uint32 *) (k + 4)
	movl	4(%ecx),%esi
	#   in3 = *(uint32 *) (k + 8)
	movl	8(%ecx),%edi
	#   in4 = *(uint32 *) (k + 12)
	movl	12(%ecx),%ebp
	#   *(uint32 *) (x + 4) = in1
	movl	%ebx,4(%eax)
	#   *(uint32 *) (x + 8) = in2
	movl	%esi,8(%eax)
	#   *(uint32 *) (x + 12) = in3
	movl	%edi,12(%eax)
	#   *(uint32 *) (x + 16) = in4
	movl	%ebp,16(%eax)
	#   kbits - 256
	cmp	$256,%edx
	#   goto kbits128 if unsigned<
	jb	._kbits128
._kbits256:
	#     in11 = *(uint32 *) (k + 16)
	movl	16(%ecx),%edx
	#     in12 = *(uint32 *) (k + 20)
	movl	20(%ecx),%ebx
	#     in13 = *(uint32 *) (k + 24)
	movl	24(%ecx),%esi
	#     in14 = *(uint32 *) (k + 28)
	movl	28(%ecx),%ecx
	#     *(uint32 *) (x + 44) = in11
	movl	%edx,44(%eax)
	#     *(uint32 *) (x + 48) = in12
	movl	%ebx,48(%eax)
	#     *(uint32 *) (x + 52) = in13
	movl	%esi,52(%eax)
	#     *(uint32 *) (x + 56) = in14
	movl	%ecx,56(%eax)
	#     in0 = 1634760805
	mov	$1634760805,%ecx
	#     in5 = 857760878
	mov	$857760878,%edx
	#     in10 = 2036477234
	mov	$2036477234,%ebx
	#     in15 = 1797285236
	mov	$1797285236,%esi
	#     *(uint32 *) (x + 0) = in0
	movl	%ecx,0(%eax)
	#     *(uint32 *) (x + 20) = in5
	movl	%edx,20(%eax)
	#     *(uint32 *) (x + 40) = in10
	movl	%ebx,40(%eax)
	#     *(uint32 *) (x + 60) = in15
	movl	%esi,60(%eax)
	#   goto keysetupdone
	jmp	._keysetupdone
._kbits128:
	#     in11 = *(uint32 *) (k + 0)
	movl	0(%ecx),%edx
	#     in12 = *(uint32 *) (k + 4)
	movl	4(%ecx),%ebx
	#     in13 = *(uint32 *) (k + 8)
	movl	8(%ecx),%esi
	#     in14 = *(uint32 *) (k + 12)
	movl	12(%ecx),%ecx
	#     *(uint32 *) (x + 44) = in11
	movl	%edx,44(%eax)
	#     *(uint32 *) (x + 48) = in12
	movl	%ebx,48(%eax)
	#     *(uint32 *) (x + 52) = in13
	movl	%esi,52(%eax)
	#     *(uint32 *) (x + 56) = in14
	movl	%ecx,56(%eax)
	#     in0 = 1634760805
	mov	$1634760805,%ecx
	#     in5 = 824206446
	mov	$824206446,%edx
	#     in10 = 2036477238
	mov	$2036477238,%ebx
	#     in15 = 1797285236
	mov	$1797285236,%esi
	#     *(uint32 *) (x + 0) = in0
	movl	%ecx,0(%eax)
	#     *(uint32 *) (x + 20) = in5
	movl	%edx,20(%eax)
	#     *(uint32 *) (x + 40) = in10
	movl	%ebx,40(%eax)
	#     *(uint32 *) (x + 60) = in15
	movl	%esi,60(%eax)
._keysetupdone:
	#   eax = eax_stack
	movl	64(%esp),%eax
	#   ebx = ebx_stack
	movl	68(%esp),%ebx
	#   esi = esi_stack
	movl	72(%esp),%esi
	#   edi = edi_stack
	movl	76(%esp),%edi
	#   ebp = ebp_stack
	movl	80(%esp),%ebp
	# leave
	add	%eax,%esp
	ret
# enter ECRYPT_ivsetup
.text
.p2align 5
.globl ECRYPT_ivsetup
ECRYPT_ivsetup:
	mov	%esp,%eax
	and	$31,%eax
	add	$256,%eax
	sub	%eax,%esp
	#   eax_stack = eax
	movl	%eax,64(%esp)
	#   ebx_stack = ebx
	movl	%ebx,68(%esp)
	#   esi_stack = esi
	movl	%esi,72(%esp)
	#   edi_stack = edi
	movl	%edi,76(%esp)
	#   ebp_stack = ebp
	movl	%ebp,80(%esp)
	#   iv = arg2
	movl	8(%esp,%eax),%ecx
	#   x = arg1
	movl	4(%esp,%eax),%eax
	#   in6 = *(uint32 *) (iv + 0)
	movl	0(%ecx),%edx
	#   in7 = *(uint32 *) (iv + 4)
	movl	4(%ecx),%ecx
	#   in8 = 0
	mov	$0,%ebx
	#   in9 = 0
	mov	$0,%esi
	#   *(uint32 *) (x + 24) = in6
	movl	%edx,24(%eax)
	#   *(uint32 *) (x + 28) = in7
	movl	%ecx,28(%eax)
	#   *(uint32 *) (x + 32) = in8
	movl	%ebx,32(%eax)
	#   *(uint32 *) (x + 36) = in9
	movl	%esi,36(%eax)
	#   eax = eax_stack
	movl	64(%esp),%eax
	#   ebx = ebx_stack
	movl	68(%esp),%ebx
	#   esi = esi_stack
	movl	72(%esp),%esi
	#   edi = edi_stack
	movl	76(%esp),%edi
	#   ebp = ebp_stack
	movl	80(%esp),%ebp
	# leave
	add	%eax,%esp
	ret

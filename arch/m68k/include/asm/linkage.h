/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#define __ALIGN .align 4
#define __ALIGN_STR ".align 4"

/*
 * Make sure the compiler doesn't do anything stupid with the
 * arguments on the stack - they are owned by the *caller*, not
 * the callee. This just fools gcc into not spilling into them,
 * and keeps it from doing tailcall recursion and/or using the
 * stack slots for temporaries, since they are live and "used"
 * all the way to the end of the function.
 */
#define asmlinkage_protect(n, ret, args...) \
	__asmlinkage_protect##n(ret, ##args)
#define __asmlinkage_protect_n(ret, args...) \
	__asm__ __volatile__ ("" : "=r" (ret) : "0" (ret), ##args)
#define __asmlinkage_protect0(ret) \
	__asmlinkage_protect_n(ret)
#define __asmlinkage_protect1(ret, arg1) \
	__asmlinkage_protect_n(ret, "m" (arg1))
#define __asmlinkage_protect2(ret, arg1, arg2) \
	__asmlinkage_protect_n(ret, "m" (arg1), "m" (arg2))
#define __asmlinkage_protect3(ret, arg1, arg2, arg3) \
	__asmlinkage_protect_n(ret, "m" (arg1), "m" (arg2), "m" (arg3))
#define __asmlinkage_protect4(ret, arg1, arg2, arg3, arg4) \
	__asmlinkage_protect_n(ret, "m" (arg1), "m" (arg2), "m" (arg3), \
			      "m" (arg4))
#define __asmlinkage_protect5(ret, arg1, arg2, arg3, arg4, arg5) \
	__asmlinkage_protect_n(ret, "m" (arg1), "m" (arg2), "m" (arg3), \
			      "m" (arg4), "m" (arg5))
#define __asmlinkage_protect6(ret, arg1, arg2, arg3, arg4, arg5, arg6) \
	__asmlinkage_protect_n(ret, "m" (arg1), "m" (arg2), "m" (arg3), \
			      "m" (arg4), "m" (arg5), "m" (arg6))

#endif

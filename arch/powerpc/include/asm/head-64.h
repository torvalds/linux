#ifndef _ASM_POWERPC_HEAD_64_H
#define _ASM_POWERPC_HEAD_64_H

#include <asm/cache.h>

#define EXC_REAL_BEGIN(name, start, end)			\
	. = start ;							\
	.global exc_real_##start##_##name ;				\
exc_real_##start##_##name:

#define EXC_REAL_END(name, start, end)

#define EXC_VIRT_BEGIN(name, start, end)			\
	. = start ;							\
	.global exc_virt_##start##_##name ;				\
exc_virt_##start##_##name:

#define EXC_VIRT_END(name, start, end)

#define EXC_COMMON_BEGIN(name)					\
	.align	7;							\
	.global name;							\
name:

#define TRAMP_REAL_BEGIN(name)					\
	.global name ;							\
name:

#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
#define TRAMP_KVM_BEGIN(name)						\
	TRAMP_REAL_BEGIN(name)
#else
#define TRAMP_KVM_BEGIN(name)
#endif

#define EXC_REAL_NONE(start, end)

#define EXC_VIRT_NONE(start, end)


#define EXC_REAL(name, start, end)				\
	EXC_REAL_BEGIN(name, start, end);			\
	STD_EXCEPTION_PSERIES(start, name##_common);			\
	EXC_REAL_END(name, start, end);

#define EXC_VIRT(name, start, end, realvec)			\
	EXC_VIRT_BEGIN(name, start, end);			\
	STD_RELON_EXCEPTION_PSERIES(start, realvec, name##_common);	\
	EXC_VIRT_END(name, start, end);

#define EXC_REAL_MASKABLE(name, start, end)			\
	EXC_REAL_BEGIN(name, start, end);			\
	MASKABLE_EXCEPTION_PSERIES(start, start, name##_common);	\
	EXC_REAL_END(name, start, end);

#define EXC_VIRT_MASKABLE(name, start, end, realvec)		\
	EXC_VIRT_BEGIN(name, start, end);			\
	MASKABLE_RELON_EXCEPTION_PSERIES(start, realvec, name##_common); \
	EXC_VIRT_END(name, start, end);

#define EXC_REAL_HV(name, start, end)			\
	EXC_REAL_BEGIN(name, start, end);			\
	STD_EXCEPTION_HV(start, start, name##_common);			\
	EXC_REAL_END(name, start, end);

#define EXC_VIRT_HV(name, start, end, realvec)		\
	EXC_VIRT_BEGIN(name, start, end);			\
	STD_RELON_EXCEPTION_HV(start, realvec, name##_common);		\
	EXC_VIRT_END(name, start, end);

#define __EXC_REAL_OOL(name, start, end)			\
	EXC_REAL_BEGIN(name, start, end);			\
	__OOL_EXCEPTION(start, label, tramp_real_##name);		\
	EXC_REAL_END(name, start, end);

#define __TRAMP_REAL_REAL_OOL(name, vec)				\
	TRAMP_REAL_BEGIN(tramp_real_##name);				\
	STD_EXCEPTION_PSERIES_OOL(vec, name##_common);			\

#define __EXC_REAL_OOL_MASKABLE(name, start, end)		\
	__EXC_REAL_OOL(name, start, end);

#define __TRAMP_REAL_REAL_OOL_MASKABLE(name, vec)			\
	TRAMP_REAL_BEGIN(tramp_real_##name);				\
	MASKABLE_EXCEPTION_PSERIES_OOL(vec, name##_common);		\

#define __EXC_REAL_OOL_HV_DIRECT(name, start, end, handler)	\
	EXC_REAL_BEGIN(name, start, end);			\
	__OOL_EXCEPTION(start, label, handler);				\
	EXC_REAL_END(name, start, end);

#define __EXC_REAL_OOL_HV(name, start, end)			\
	__EXC_REAL_OOL(name, start, end);

#define __TRAMP_REAL_REAL_OOL_HV(name, vec)				\
	TRAMP_REAL_BEGIN(tramp_real_##name);				\
	STD_EXCEPTION_HV_OOL(vec, name##_common);			\

#define __EXC_REAL_OOL_MASKABLE_HV(name, start, end)		\
	__EXC_REAL_OOL(name, start, end);

#define __TRAMP_REAL_REAL_OOL_MASKABLE_HV(name, vec)			\
	TRAMP_REAL_BEGIN(tramp_real_##name);				\
	MASKABLE_EXCEPTION_HV_OOL(vec, name##_common);			\

#define __EXC_VIRT_OOL(name, start, end)			\
	EXC_VIRT_BEGIN(name, start, end);			\
	__OOL_EXCEPTION(start, label, tramp_virt_##name);		\
	EXC_VIRT_END(name, start, end);

#define __TRAMP_REAL_VIRT_OOL(name, realvec)				\
	TRAMP_REAL_BEGIN(tramp_virt_##name);				\
	STD_RELON_EXCEPTION_PSERIES_OOL(realvec, name##_common);	\

#define __EXC_VIRT_OOL_MASKABLE(name, start, end)		\
	__EXC_VIRT_OOL(name, start, end);

#define __TRAMP_REAL_VIRT_OOL_MASKABLE(name, realvec)		\
	TRAMP_REAL_BEGIN(tramp_virt_##name);				\
	MASKABLE_RELON_EXCEPTION_PSERIES_OOL(realvec, name##_common);	\

#define __EXC_VIRT_OOL_HV(name, start, end)			\
	__EXC_VIRT_OOL(name, start, end);

#define __TRAMP_REAL_VIRT_OOL_HV(name, realvec)			\
	TRAMP_REAL_BEGIN(tramp_virt_##name);				\
	STD_RELON_EXCEPTION_HV_OOL(realvec, name##_common);		\

#define __EXC_VIRT_OOL_MASKABLE_HV(name, start, end)		\
	__EXC_VIRT_OOL(name, start, end);

#define __TRAMP_REAL_VIRT_OOL_MASKABLE_HV(name, realvec)		\
	TRAMP_REAL_BEGIN(tramp_virt_##name);				\
	MASKABLE_RELON_EXCEPTION_HV_OOL(realvec, name##_common);	\

#define TRAMP_KVM(area, n)						\
	TRAMP_KVM_BEGIN(do_kvm_##n);					\
	KVM_HANDLER(area, EXC_STD, n);					\

#define TRAMP_KVM_SKIP(area, n)						\
	TRAMP_KVM_BEGIN(do_kvm_##n);					\
	KVM_HANDLER_SKIP(area, EXC_STD, n);				\

#define TRAMP_KVM_HV(area, n)						\
	TRAMP_KVM_BEGIN(do_kvm_H##n);					\
	KVM_HANDLER(area, EXC_HV, n + 0x2);				\

#define TRAMP_KVM_HV_SKIP(area, n)					\
	TRAMP_KVM_BEGIN(do_kvm_H##n);					\
	KVM_HANDLER_SKIP(area, EXC_HV, n + 0x2);			\

#define EXC_COMMON(name, realvec, hdlr)				\
	EXC_COMMON_BEGIN(name);					\
	STD_EXCEPTION_COMMON(realvec, name, hdlr);			\

#define EXC_COMMON_ASYNC(name, realvec, hdlr)			\
	EXC_COMMON_BEGIN(name);					\
	STD_EXCEPTION_COMMON_ASYNC(realvec, name, hdlr);		\

#define EXC_COMMON_HV(name, realvec, hdlr)				\
	EXC_COMMON_BEGIN(name);					\
	STD_EXCEPTION_COMMON(realvec + 0x2, name, hdlr);		\

#endif	/* _ASM_POWERPC_HEAD_64_H */

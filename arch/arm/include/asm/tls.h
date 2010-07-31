#ifndef __ASMARM_TLS_H
#define __ASMARM_TLS_H

#ifdef __ASSEMBLY__
	.macro set_tls_none, tp, tmp1, tmp2
	.endm

	.macro set_tls_v6k, tp, tmp1, tmp2
	mcr	p15, 0, \tp, c13, c0, 3		@ set TLS register
	.endm

	.macro set_tls_v6, tp, tmp1, tmp2
	ldr	\tmp1, =elf_hwcap
	ldr	\tmp1, [\tmp1, #0]
	mov	\tmp2, #0xffff0fff
	tst	\tmp1, #HWCAP_TLS		@ hardware TLS available?
	mcrne	p15, 0, \tp, c13, c0, 3		@ yes, set TLS register
	streq	\tp, [\tmp2, #-15]		@ set TLS value at 0xffff0ff0
	.endm

	.macro set_tls_software, tp, tmp1, tmp2
	mov	\tmp1, #0xffff0fff
	str	\tp, [\tmp1, #-15]		@ set TLS value at 0xffff0ff0
	.endm
#endif

#ifdef CONFIG_TLS_REG_EMUL
#define tls_emu		1
#define has_tls_reg		1
#define set_tls		set_tls_none
#elif __LINUX_ARM_ARCH__ >= 7 ||					\
	(__LINUX_ARM_ARCH__ == 6 && defined(CONFIG_CPU_32v6K))
#define tls_emu		0
#define has_tls_reg		1
#define set_tls		set_tls_v6k
#elif __LINUX_ARM_ARCH__ == 6
#define tls_emu		0
#define has_tls_reg		(elf_hwcap & HWCAP_TLS)
#define set_tls		set_tls_v6
#else
#define tls_emu		0
#define has_tls_reg		0
#define set_tls		set_tls_software
#endif

#endif	/* __ASMARM_TLS_H */

#include <linux/linkage.h>
#include <asm/export.h>

	.text
	.align	32
ENTRY(__arch_hweight8)
	ba,pt	%xcc, __sw_hweight8
	 nop
	nop
ENDPROC(__arch_hweight8)
EXPORT_SYMBOL(__arch_hweight8)
	.section	.popc_3insn_patch, "ax"
	.word		__arch_hweight8
	sllx		%o0, 64-8, %g1
	retl
	 popc		%g1, %o0
	.previous

ENTRY(__arch_hweight16)
	ba,pt	%xcc, __sw_hweight16
	 nop
	nop
ENDPROC(__arch_hweight16)
EXPORT_SYMBOL(__arch_hweight16)
	.section	.popc_3insn_patch, "ax"
	.word		__arch_hweight16
	sllx		%o0, 64-16, %g1
	retl
	 popc		%g1, %o0
	.previous

ENTRY(__arch_hweight32)
	ba,pt	%xcc, __sw_hweight32
	 nop
	nop
ENDPROC(__arch_hweight32)
EXPORT_SYMBOL(__arch_hweight32)
	.section	.popc_3insn_patch, "ax"
	.word		__arch_hweight32
	sllx		%o0, 64-32, %g1
	retl
	 popc		%g1, %o0
	.previous

ENTRY(__arch_hweight64)
	ba,pt	%xcc, __sw_hweight64
	 nop
	nop
ENDPROC(__arch_hweight64)
EXPORT_SYMBOL(__arch_hweight64)
	.section	.popc_3insn_patch, "ax"
	.word		__arch_hweight64
	retl
	 popc		%o0, %o0
	nop
	.previous

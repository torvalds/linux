/*
 *	arch/alpha/lib/callback_srm.S
 */

#include <asm/console.h>
#include <asm/export.h>

.text
#define HWRPB_CRB_OFFSET 0xc0

#if defined(CONFIG_ALPHA_SRM) || defined(CONFIG_ALPHA_GENERIC)
.align 4
srm_dispatch:
#if defined(CONFIG_ALPHA_GENERIC)
	ldl	$4,alpha_using_srm
	beq	$4,nosrm
#endif
	ldq	$0,hwrpb	# gp is set up by CALLBACK macro.
	ldl	$25,0($25)	# Pick up the wrapper data.
	mov	$20,$21		# Shift arguments right.
	mov	$19,$20
	ldq	$1,HWRPB_CRB_OFFSET($0)
	mov	$18,$19
	mov	$17,$18
	mov	$16,$17
	addq	$0,$1,$2	# CRB address
	ldq	$27,0($2)	# DISPATCH procedure descriptor (VMS call std)
	extwl	$25,0,$16	# SRM callback function code
	ldq	$3,8($27)	# call address
	extwl	$25,2,$25	# argument information (VMS calling std)
	jmp	($3)		# Return directly to caller of wrapper.

.align 4
.globl	srm_fixup
.ent	srm_fixup
srm_fixup:
	ldgp	$29,0($27)
#if defined(CONFIG_ALPHA_GENERIC)
	ldl	$4,alpha_using_srm
	beq	$4,nosrm
#endif
	ldq	$0,hwrpb
	ldq	$1,HWRPB_CRB_OFFSET($0)
	addq	$0,$1,$2	# CRB address
	ldq	$27,16($2)	# VA of FIXUP procedure descriptor
	ldq	$3,8($27)	# call address
	lda	$25,2($31)	# two integer arguments
	jmp	($3)		# Return directly to caller of srm_fixup.
.end    srm_fixup

#if defined(CONFIG_ALPHA_GENERIC)
.align 3
nosrm:
	lda	$0,-1($31)
	ret
#endif

#define CALLBACK(NAME, CODE, ARG_CNT) \
.align 4; .globl callback_##NAME; .ent callback_##NAME; callback_##NAME##: \
ldgp $29,0($27); br $25,srm_dispatch; .word CODE, ARG_CNT; .end callback_##NAME

#else /* defined(CONFIG_ALPHA_SRM) || defined(CONFIG_ALPHA_GENERIC) */

#define CALLBACK(NAME, CODE, ARG_CNT) \
.align 3; .globl callback_##NAME; .ent callback_##NAME; callback_##NAME##: \
lda $0,-1($31); ret; .end callback_##NAME

.align 3
.globl	srm_fixup
.ent	srm_fixup
srm_fixup:
	lda	$0,-1($31)
	ret
.end	srm_fixup
#endif /* defined(CONFIG_ALPHA_SRM) || defined(CONFIG_ALPHA_GENERIC) */

CALLBACK(puts, CCB_PUTS, 4)
CALLBACK(open, CCB_OPEN, 3)
CALLBACK(close, CCB_CLOSE, 2)
CALLBACK(read, CCB_READ, 5)
CALLBACK(open_console, CCB_OPEN_CONSOLE, 1)
CALLBACK(close_console, CCB_CLOSE_CONSOLE, 1)
CALLBACK(getenv, CCB_GET_ENV, 4)
CALLBACK(setenv, CCB_SET_ENV, 4)
CALLBACK(getc, CCB_GETC, 2)
CALLBACK(reset_term, CCB_RESET_TERM, 2)
CALLBACK(term_int, CCB_SET_TERM_INT, 3)
CALLBACK(term_ctl, CCB_SET_TERM_CTL, 3)
CALLBACK(process_keycode, CCB_PROCESS_KEYCODE, 3)
CALLBACK(ioctl, CCB_IOCTL, 6)
CALLBACK(write, CCB_WRITE, 5)
CALLBACK(reset_env, CCB_RESET_ENV, 4)
CALLBACK(save_env, CCB_SAVE_ENV, 1)
CALLBACK(pswitch, CCB_PSWITCH, 3)
CALLBACK(bios_emul, CCB_BIOS_EMUL, 5)

EXPORT_SYMBOL(callback_getenv)
EXPORT_SYMBOL(callback_setenv)
EXPORT_SYMBOL(callback_save_env)
	
.data
__alpha_using_srm:		# For use by bootpheader
	.long 7			# value is not 1 for link debugging
	.weak alpha_using_srm; alpha_using_srm = __alpha_using_srm
__callback_init_done:		# For use by bootpheader
	.long 7			# value is not 1 for link debugging
	.weak callback_init_done; callback_init_done = __callback_init_done


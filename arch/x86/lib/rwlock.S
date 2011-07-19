/* Slow paths of read/write spinlocks. */

#include <linux/linkage.h>
#include <asm/alternative-asm.h>
#include <asm/frame.h>
#include <asm/rwlock.h>

#ifdef CONFIG_X86_32
# define __lock_ptr eax
#else
# define __lock_ptr rdi
#endif

ENTRY(__write_lock_failed)
	CFI_STARTPROC
	FRAME
0:	LOCK_PREFIX
	addl	$RW_LOCK_BIAS, (%__lock_ptr)
1:	rep; nop
	cmpl	$RW_LOCK_BIAS, (%__lock_ptr)
	jne	1b
	LOCK_PREFIX
	subl	$RW_LOCK_BIAS, (%__lock_ptr)
	jnz	0b
	ENDFRAME
	ret
	CFI_ENDPROC
END(__write_lock_failed)

ENTRY(__read_lock_failed)
	CFI_STARTPROC
	FRAME
0:	LOCK_PREFIX
	incl	(%__lock_ptr)
1:	rep; nop
	cmpl	$1, (%__lock_ptr)
	js	1b
	LOCK_PREFIX
	decl	(%__lock_ptr)
	js	0b
	ENDFRAME
	ret
	CFI_ENDPROC
END(__read_lock_failed)

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
	WRITE_LOCK_ADD($RW_LOCK_BIAS) (%__lock_ptr)
1:	rep; nop
	cmpl	$WRITE_LOCK_CMP, (%__lock_ptr)
	jne	1b
	LOCK_PREFIX
	WRITE_LOCK_SUB($RW_LOCK_BIAS) (%__lock_ptr)
	jnz	0b
	ENDFRAME
	ret
	CFI_ENDPROC
END(__write_lock_failed)

ENTRY(__read_lock_failed)
	CFI_STARTPROC
	FRAME
0:	LOCK_PREFIX
	READ_LOCK_SIZE(inc) (%__lock_ptr)
1:	rep; nop
	READ_LOCK_SIZE(cmp) $1, (%__lock_ptr)
	js	1b
	LOCK_PREFIX
	READ_LOCK_SIZE(dec) (%__lock_ptr)
	js	0b
	ENDFRAME
	ret
	CFI_ENDPROC
END(__read_lock_failed)

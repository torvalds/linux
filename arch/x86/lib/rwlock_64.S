/* Slow paths of read/write spinlocks. */

#include <linux/linkage.h>
#include <asm/rwlock.h>
#include <asm/alternative-asm.h>
#include <asm/dwarf2.h>

/* rdi:	pointer to rwlock_t */
ENTRY(__write_lock_failed)
	CFI_STARTPROC
	LOCK_PREFIX
	addl $RW_LOCK_BIAS,(%rdi)
1:	rep
	nop
	cmpl $RW_LOCK_BIAS,(%rdi)
	jne 1b
	LOCK_PREFIX
	subl $RW_LOCK_BIAS,(%rdi)
	jnz  __write_lock_failed
	ret
	CFI_ENDPROC
END(__write_lock_failed)

/* rdi:	pointer to rwlock_t */
ENTRY(__read_lock_failed)
	CFI_STARTPROC
	LOCK_PREFIX
	incl (%rdi)
1:	rep
	nop
	cmpl $1,(%rdi)
	js 1b
	LOCK_PREFIX
	decl (%rdi)
	js __read_lock_failed
	ret
	CFI_ENDPROC
END(__read_lock_failed)

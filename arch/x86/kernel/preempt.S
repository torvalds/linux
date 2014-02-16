
#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/asm.h>
#include <asm/calling.h>

ENTRY(___preempt_schedule)
	CFI_STARTPROC
	SAVE_ALL
	call preempt_schedule
	RESTORE_ALL
	ret
	CFI_ENDPROC

#ifdef CONFIG_CONTEXT_TRACKING

ENTRY(___preempt_schedule_context)
	CFI_STARTPROC
	SAVE_ALL
	call preempt_schedule_context
	RESTORE_ALL
	ret
	CFI_ENDPROC

#endif

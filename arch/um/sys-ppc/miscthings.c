#include "linux/threads.h"
#include "linux/stddef.h"  // for NULL
#include "linux/elf.h"  // for AT_NULL

/* The following function nicked from arch/ppc/kernel/process.c and
 * adapted slightly */
/*
 * XXX ld.so expects the auxiliary table to start on
 * a 16-byte boundary, so we have to find it and
 * move it up. :-(
 */
void shove_aux_table(unsigned long sp)
{
	int argc;
	char *p;
	unsigned long e;
	unsigned long aux_start, offset;

	argc = *(int *)sp;
	sp += sizeof(int) + (argc + 1) * sizeof(char *);
	/* skip over the environment pointers */
	do {
		p = *(char **)sp;
		sp += sizeof(char *);
	} while (p != NULL);
	aux_start = sp;
	/* skip to the end of the auxiliary table */
	do {
		e = *(unsigned long *)sp;
		sp += 2 * sizeof(unsigned long);
	} while (e != AT_NULL);
	offset = ((aux_start + 15) & ~15) - aux_start;
	if (offset != 0) {
		do {
			sp -= sizeof(unsigned long);
			e = *(unsigned long *)sp;
			*(unsigned long *)(sp + offset) = e;
		} while (sp > aux_start);
	}
}
/* END stuff taken from arch/ppc/kernel/process.c */


/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */

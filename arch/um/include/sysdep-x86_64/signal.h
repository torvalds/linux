/*
 * Copyright (C) 2004 PathScale, Inc
 * Licensed under the GPL
 */

#ifndef __X86_64_SIGNAL_H_
#define __X86_64_SIGNAL_H_

#define ARCH_GET_SIGCONTEXT(sc, sig_addr) \
	do { \
		struct ucontext *__uc; \
		asm("movq %%rdx, %0" : "=r" (__uc)); \
		sc = (struct sigcontext *) &__uc->uc_mcontext; \
	} while(0)

#endif

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

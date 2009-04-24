#ifndef _ASM_INIT_H
#define _ASM_INIT_H

#define __init __attribute__ ((__section__ (".text.init")))
#define __initdata __attribute__ ((__section__ (".data.init")))
/* For assembly routines */
#define __INIT		.section	".text.init",#alloc,#execinstr
#define __FINIT		.previous
#define __INITDATA	.section	".data.init",#alloc,#write

#endif


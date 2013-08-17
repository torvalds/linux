#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#include <linux/stringify.h>

#define __ALIGN .align 4, 0x07
#define __ALIGN_STR __stringify(__ALIGN)

#endif

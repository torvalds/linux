#ifndef __UM_PROCESSOR_H
#define __UM_PROCESSOR_H

/* include faultinfo structure */
#include <sysdep/faultinfo.h>

#ifdef CONFIG_X86_32
# include "processor_32.h"
#else
# include "processor_64.h"
#endif

#include <asm/processor-generic.h>

#endif

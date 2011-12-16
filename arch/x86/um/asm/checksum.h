#ifndef __UM_CHECKSUM_H
#define __UM_CHECKSUM_H

#ifdef CONFIG_X86_32
# include "checksum_32.h"
#else
# include "checksum_64.h"
#endif

#endif

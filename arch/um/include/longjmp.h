#ifndef __UML_LONGJMP_H
#define __UML_LONGJMP_H

#include <setjmp.h>
#include "os.h"

#define UML_SIGLONGJMP(buf, val) do { \
	longjmp(*buf, val);	\
} while(0)

#define UML_SIGSETJMP(buf, enable) ({ \
	int n; \
	enable = get_signals(); \
	n = setjmp(*buf); \
	if(n != 0) \
		set_signals(enable); \
	n; })

#endif

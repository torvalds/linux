#ifdef __uClinux__
#include "atomic_no.h"
#else
#include "atomic_mm.h"
#endif

#include <asm-generic/atomic64.h>

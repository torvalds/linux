#ifdef __uClinux__
#include "atomic_no.h"
#else
#include "atomic_mm.h"
#endif

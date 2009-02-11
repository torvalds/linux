#ifdef __uClinux__
#include "ptrace_no.h"
#else
#include "ptrace_mm.h"
#endif

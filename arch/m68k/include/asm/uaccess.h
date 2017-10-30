#ifdef __uClinux__
#include <asm/uaccess_no.h>
#else
#include <asm/uaccess_mm.h>
#endif
#include <asm/extable.h>

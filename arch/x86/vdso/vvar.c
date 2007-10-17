/* Define pointer to external vDSO variables.
   These are part of the vDSO. The kernel fills in the real addresses
   at boot time. This is done because when the vdso is linked the
   kernel isn't yet and we don't know the final addresses. */
#include <linux/kernel.h>
#include <linux/time.h>
#include <asm/vsyscall.h>
#include <asm/timex.h>
#include <asm/vgtod.h>

#define VEXTERN(x) typeof (__ ## x) *const vdso_ ## x = (void *)VMAGIC;
#include "vextern.h"

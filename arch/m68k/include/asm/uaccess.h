/* SPDX-License-Identifier: GPL-2.0 */
#ifdef __uClinux__
#include <asm/uaccess_yes.h>
#else
#include <asm/uaccess_mm.h>
#endif
#include <asm/extable.h>

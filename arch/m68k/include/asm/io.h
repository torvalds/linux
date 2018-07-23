/* SPDX-License-Identifier: GPL-2.0 */
#if defined(__uClinux__) || defined(CONFIG_COLDFIRE)
#include <asm/io_no.h>
#else
#include <asm/io_mm.h>
#endif

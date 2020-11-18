/* SPDX-License-Identifier: GPL-2.0 */
#define  _HAVE_ARCH_COPY_AND_CSUM_FROM_USER 1
#define HAVE_CSUM_COPY_USER
#ifdef CONFIG_X86_32
# include <asm/checksum_32.h>
#else
# include <asm/checksum_64.h>
#endif

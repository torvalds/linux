/* SPDX-License-Identifier: GPL-2.0 */
#ifdef CONFIG_GENERIC_CSUM
# include <asm-generic/checksum.h>
#else
# define  _HAVE_ARCH_COPY_AND_CSUM_FROM_USER 1
# define HAVE_CSUM_COPY_USER
# define _HAVE_ARCH_CSUM_AND_COPY
# ifdef CONFIG_X86_32
#  include <asm/checksum_32.h>
# else
#  include <asm/checksum_64.h>
# endif
#endif

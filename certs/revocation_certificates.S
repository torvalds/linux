/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/export.h>
#include <linux/init.h>

	__INITRODATA

	.align 8
	.globl revocation_certificate_list
revocation_certificate_list:
__revocation_list_start:
	.incbin "certs/x509_revocation_list"
__revocation_list_end:

	.align 8
	.globl revocation_certificate_list_size
revocation_certificate_list_size:
#ifdef CONFIG_64BIT
	.quad __revocation_list_end - __revocation_list_start
#else
	.long __revocation_list_end - __revocation_list_start
#endif

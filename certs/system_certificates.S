/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/export.h>
#include <linux/init.h>

	__INITRODATA

	.align 8
	.globl system_certificate_list
system_certificate_list:
__cert_list_start:
__module_cert_start:
#if defined(CONFIG_MODULE_SIG) || (defined(CONFIG_IMA_APPRAISE_MODSIG) \
			       && defined(CONFIG_MODULES))
	.incbin "certs/signing_key.x509"
#endif
__module_cert_end:
	.incbin "certs/x509_certificate_list"
__cert_list_end:

#ifdef CONFIG_SYSTEM_EXTRA_CERTIFICATE
	.globl system_extra_cert
	.size system_extra_cert, CONFIG_SYSTEM_EXTRA_CERTIFICATE_SIZE
system_extra_cert:
	.fill CONFIG_SYSTEM_EXTRA_CERTIFICATE_SIZE, 1, 0

	.align 4
	.globl system_extra_cert_used
system_extra_cert_used:
	.int 0

#endif /* CONFIG_SYSTEM_EXTRA_CERTIFICATE */

	.align 8
	.globl system_certificate_list_size
system_certificate_list_size:
#ifdef CONFIG_64BIT
	.quad __cert_list_end - __cert_list_start
#else
	.long __cert_list_end - __cert_list_start
#endif

	.align 8
	.globl module_cert_size
module_cert_size:
#ifdef CONFIG_64BIT
	.quad __module_cert_end - __module_cert_start
#else
	.long __module_cert_end - __module_cert_start
#endif

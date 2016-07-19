#include <linux/export.h>
#include <linux/init.h>

	__INITRODATA

	.align 8
	.globl VMLINUX_SYMBOL(system_certificate_list)
VMLINUX_SYMBOL(system_certificate_list):
__cert_list_start:
#ifdef CONFIG_MODULE_SIG
	.incbin "certs/signing_key.x509"
#endif
	.incbin "certs/x509_certificate_list"
__cert_list_end:

#ifdef CONFIG_SYSTEM_EXTRA_CERTIFICATE
	.globl VMLINUX_SYMBOL(system_extra_cert)
	.size system_extra_cert, CONFIG_SYSTEM_EXTRA_CERTIFICATE_SIZE
VMLINUX_SYMBOL(system_extra_cert):
	.fill CONFIG_SYSTEM_EXTRA_CERTIFICATE_SIZE, 1, 0

	.align 4
	.globl VMLINUX_SYMBOL(system_extra_cert_used)
VMLINUX_SYMBOL(system_extra_cert_used):
	.int 0

#endif /* CONFIG_SYSTEM_EXTRA_CERTIFICATE */

	.align 8
	.globl VMLINUX_SYMBOL(system_certificate_list_size)
VMLINUX_SYMBOL(system_certificate_list_size):
#ifdef CONFIG_64BIT
	.quad __cert_list_end - __cert_list_start
#else
	.long __cert_list_end - __cert_list_start
#endif

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 Cavium, Inc.
 */

#include <linux/kernel.h>
#include <linux/virtio_console.h>
#include <linux/kvm_para.h>
#include <asm/setup.h>

/*
 * Emit one character to the boot console.
 */
void prom_putchar(char c)
{
	kvm_hypercall3(KVM_HC_MIPS_CONSOLE_OUTPUT, 0 /*  port 0 */,
		(unsigned long)&c, 1 /* len == 1 */);
}

#ifdef CONFIG_VIRTIO_CONSOLE
static int paravirt_put_chars(u32 vtermno, const char *buf, int count)
{
	kvm_hypercall3(KVM_HC_MIPS_CONSOLE_OUTPUT, vtermno,
		(unsigned long)buf, count);

	return count;
}

static int __init paravirt_cons_init(void)
{
	virtio_cons_early_init(paravirt_put_chars);
	return 0;
}
core_initcall(paravirt_cons_init);

#endif

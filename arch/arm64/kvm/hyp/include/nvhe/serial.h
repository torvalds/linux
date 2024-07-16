/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ARM64_KVM_NVHE_SERIAL_H__
#define __ARM64_KVM_NVHE_SERIAL_H__

void hyp_puts(const char *s);
void hyp_putx64(u64 x);
void hyp_putc(char c);
int __pkvm_register_serial_driver(void (*driver_cb)(char));

#endif

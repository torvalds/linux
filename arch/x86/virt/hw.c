// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kvm_types.h>

#include <asm/virt.h>

__visible bool virt_rebooting;
EXPORT_SYMBOL_FOR_KVM(virt_rebooting);

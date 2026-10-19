// SPDX-License-Identifier: GPL-2.0

#include <linux/kvm_host.h>
#include <linux/bitfield.h>

#include <trace/events/kvm.h>

#include <arm64/kvm_emulate.h>
#include <arm64/sysreg.h>

#include "trace.h"

#define __INCL_GEN_ARM_FILE
#include "generated/mmio.inc"
#undef __INCL_GEN_ARM_FILE

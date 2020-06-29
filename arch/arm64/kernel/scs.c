// SPDX-License-Identifier: GPL-2.0
/*
 * Shadow Call Stack support.
 *
 * Copyright (C) 2019 Google LLC
 */

#include <linux/percpu.h>
#include <linux/scs.h>

DEFINE_SCS(irq_shadow_call_stack);

#ifdef CONFIG_ARM_SDE_INTERFACE
DEFINE_SCS(sdei_shadow_call_stack_normal);
DEFINE_SCS(sdei_shadow_call_stack_critical);
#endif

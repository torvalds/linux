/* SPDX-License-Identifier: GPL-2.0 */

/*
 * This file includes headers whose assembly part includes macros which are
 * commonly used. The macros are precompiled into assmebly file which is later
 * assembled together with each compiled file.
 */

#include <linux/compiler.h>
#include <asm/refcount.h>
#include <asm/alternative-asm.h>
#include <asm/bug.h>
#include <asm/paravirt.h>
#include <asm/asm.h>
#include <asm/cpufeature.h>
#include <asm/jump_label.h>

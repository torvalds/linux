/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_TIMEX_H
#define __UM_TIMEX_H

/*
 * Do not fall back to the host architecture header because latter likely
 * includes facilities like cpu_feature_enabled() which are present only
 * there. That would result in build breakages and/or efforts to "emulate"
 * those facilities in UML.
 */
#include <asm-generic/timex.h>

#endif

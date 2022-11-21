/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RAS_H
#define RAS_H

#include <asm/interrupt.h>

DECLARE_INTERRUPT_HANDLER(cbe_system_error_exception);
DECLARE_INTERRUPT_HANDLER(cbe_maintenance_exception);
DECLARE_INTERRUPT_HANDLER(cbe_thermal_exception);

extern void cbe_ras_init(void);

#endif /* RAS_H */

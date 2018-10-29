/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NITROX_ISR_H
#define __NITROX_ISR_H

#include "nitrox_dev.h"

int nitrox_register_interrupts(struct nitrox_device *ndev);
void nitrox_unregister_interrupts(struct nitrox_device *ndev);

#endif /* __NITROX_ISR_H */

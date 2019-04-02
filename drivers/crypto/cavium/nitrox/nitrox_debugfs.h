// SPDX-License-Identifier: GPL-2.0
#ifndef __NITROX_DEFS_H
#define __NITROX_DEFS_H

#include "nitrox_dev.h"

#ifdef CONFIG_DE_FS
void nitrox_defs_init(struct nitrox_device *ndev);
void nitrox_defs_exit(struct nitrox_device *ndev);
#else
static inline void nitrox_defs_init(struct nitrox_device *ndev)
{
}

static inline void nitrox_defs_exit(struct nitrox_device *ndev)
{
}
#endif /* !CONFIG_DE_FS */

#endif /* __NITROX_DEFS_H */

/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2023 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_HWMON_H
#define BNXT_HWMON_H

#ifdef CONFIG_BNXT_HWMON
void bnxt_hwmon_notify_event(struct bnxt *bp);
void bnxt_hwmon_uninit(struct bnxt *bp);
void bnxt_hwmon_init(struct bnxt *bp);
#else
static inline void bnxt_hwmon_notify_event(struct bnxt *bp)
{
}

static inline void bnxt_hwmon_uninit(struct bnxt *bp)
{
}

static inline void bnxt_hwmon_init(struct bnxt *bp)
{
}
#endif
#endif

/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2025 NXP */

#ifndef __ENETC4_DEBUGFS_H
#define __ENETC4_DEBUGFS_H

#if IS_ENABLED(CONFIG_DEBUG_FS)
void enetc_create_debugfs(struct enetc_si *si);
void enetc_remove_debugfs(struct enetc_si *si);
#else
static inline void enetc_create_debugfs(struct enetc_si *si)
{
}

static inline void enetc_remove_debugfs(struct enetc_si *si)
{
}
#endif

#endif

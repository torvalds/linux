/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cs_dsp.h  --  Private header for cs_dsp driver.
 *
 * Copyright (C) 2026 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef FW_CS_DSP_H
#define FW_CS_DSP_H

#if IS_ENABLED(CONFIG_KUNIT)
bool cs_dsp_can_emit_message(void);
#endif

#endif /* ifndef FW_CS_DSP_H */

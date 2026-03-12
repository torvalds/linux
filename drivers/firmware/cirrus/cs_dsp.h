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
extern bool cs_dsp_suppress_err_messages;
extern bool cs_dsp_suppress_warn_messages;
extern bool cs_dsp_suppress_info_messages;
#endif

#endif /* ifndef FW_CS_DSP_H */

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016-2018 NXP
 */

#ifndef __FSL_DPRTC_H
#define __FSL_DPRTC_H

/* Data Path Real Time Counter API
 * Contains initialization APIs and runtime control APIs for RTC
 */

struct fsl_mc_io;

int dprtc_open(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       int dprtc_id,
	       u16 *token);

int dprtc_close(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token);

int dprtc_set_freq_compensation(struct fsl_mc_io *mc_io,
				u32 cmd_flags,
				u16 token,
				u32 freq_compensation);

int dprtc_get_freq_compensation(struct fsl_mc_io *mc_io,
				u32 cmd_flags,
				u16 token,
				u32 *freq_compensation);

int dprtc_get_time(struct fsl_mc_io *mc_io,
		   u32 cmd_flags,
		   u16 token,
		   uint64_t *time);

int dprtc_set_time(struct fsl_mc_io *mc_io,
		   u32 cmd_flags,
		   u16 token,
		   uint64_t time);

#endif /* __FSL_DPRTC_H */

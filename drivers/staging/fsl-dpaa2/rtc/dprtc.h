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

/**
 * Number of irq's
 */
#define DPRTC_MAX_IRQ_NUM			1
#define DPRTC_IRQ_INDEX				0

/**
 * Interrupt event masks:
 */

/**
 * Interrupt event mask indicating alarm event had occurred
 */
#define DPRTC_EVENT_ALARM			0x40000000
/**
 * Interrupt event mask indicating periodic pulse event had occurred
 */
#define DPRTC_EVENT_PPS				0x08000000

int dprtc_open(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       int dprtc_id,
	       u16 *token);

int dprtc_close(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token);

/**
 * struct dprtc_cfg - Structure representing DPRTC configuration
 * @options:	place holder
 */
struct dprtc_cfg {
	u32 options;
};

int dprtc_create(struct fsl_mc_io *mc_io,
		 u16 dprc_token,
		 u32 cmd_flags,
		 const struct dprtc_cfg *cfg,
		 u32 *obj_id);

int dprtc_destroy(struct fsl_mc_io *mc_io,
		  u16 dprc_token,
		  u32 cmd_flags,
		  u32 object_id);

int dprtc_enable(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token);

int dprtc_disable(struct fsl_mc_io *mc_io,
		  u32 cmd_flags,
		  u16 token);

int dprtc_is_enabled(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token,
		     int *en);

int dprtc_reset(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token);

int dprtc_set_clock_offset(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   int64_t offset);

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

int dprtc_set_alarm(struct fsl_mc_io *mc_io,
		    u32 cmd_flags,
		    u16 token,
		    uint64_t time);

int dprtc_set_irq_enable(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 u8 irq_index,
			 u8 en);

int dprtc_get_irq_enable(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 u8 irq_index,
			 u8 *en);

int dprtc_set_irq_mask(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       u8 irq_index,
		       u32 mask);

int dprtc_get_irq_mask(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       u8 irq_index,
		       u32 *mask);

int dprtc_get_irq_status(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 u8 irq_index,
			 u32 *status);

int dprtc_clear_irq_status(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   u8 irq_index,
			   u32 status);

/**
 * struct dprtc_attr - Structure representing DPRTC attributes
 * @id:		DPRTC object ID
 */
struct dprtc_attr {
	int id;
};

int dprtc_get_attributes(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 struct dprtc_attr *attr);

int dprtc_get_api_version(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  u16 *major_ver,
			  u16 *minor_ver);

#endif /* __FSL_DPRTC_H */

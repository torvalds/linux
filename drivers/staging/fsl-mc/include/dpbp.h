/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 *
 */
#ifndef __FSL_DPBP_H
#define __FSL_DPBP_H

/*
 * Data Path Buffer Pool API
 * Contains initialization APIs and runtime control APIs for DPBP
 */

struct fsl_mc_io;

int dpbp_open(struct fsl_mc_io *mc_io,
	      u32 cmd_flags,
	      int dpbp_id,
	      u16 *token);

int dpbp_close(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token);

int dpbp_enable(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token);

int dpbp_disable(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token);

int dpbp_is_enabled(struct fsl_mc_io *mc_io,
		    u32 cmd_flags,
		    u16 token,
		    int *en);

int dpbp_reset(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token);

/**
 * struct dpbp_attr - Structure representing DPBP attributes
 * @id:		DPBP object ID
 * @bpid:	Hardware buffer pool ID; should be used as an argument in
 *		acquire/release operations on buffers
 */
struct dpbp_attr {
	int id;
	u16 bpid;
};

int dpbp_get_attributes(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			struct dpbp_attr *attr);

int dpbp_get_api_version(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 *major_ver,
			 u16 *minor_ver);

#endif /* __FSL_DPBP_H */

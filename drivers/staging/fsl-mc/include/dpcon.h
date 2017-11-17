/* Copyright 2013-2016 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * * Neither the name of the above-listed copyright holders nor the
 * names of any contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __FSL_DPCON_H
#define __FSL_DPCON_H

/* Data Path Concentrator API
 * Contains initialization APIs and runtime control APIs for DPCON
 */

struct fsl_mc_io;

/** General DPCON macros */

/**
 * Use it to disable notifications; see dpcon_set_notification()
 */
#define DPCON_INVALID_DPIO_ID		(int)(-1)

int dpcon_open(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       int dpcon_id,
	       u16 *token);

int dpcon_close(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token);

int dpcon_enable(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token);

int dpcon_disable(struct fsl_mc_io *mc_io,
		  u32 cmd_flags,
		  u16 token);

int dpcon_reset(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token);

/**
 * struct dpcon_attr - Structure representing DPCON attributes
 * @id: DPCON object ID
 * @qbman_ch_id: Channel ID to be used by dequeue operation
 * @num_priorities: Number of priorities for the DPCON channel (1-8)
 */
struct dpcon_attr {
	int id;
	u16 qbman_ch_id;
	u8 num_priorities;
};

int dpcon_get_attributes(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 struct dpcon_attr *attr);

/**
 * struct dpcon_notification_cfg - Structure representing notification params
 * @dpio_id:	DPIO object ID; must be configured with a notification channel;
 *	to disable notifications set it to 'DPCON_INVALID_DPIO_ID';
 * @priority:	Priority selection within the DPIO channel; valid values
 *		are 0-7, depending on the number of priorities in that channel
 * @user_ctx:	User context value provided with each CDAN message
 */
struct dpcon_notification_cfg {
	int dpio_id;
	u8 priority;
	u64 user_ctx;
};

int dpcon_set_notification(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   struct dpcon_notification_cfg *cfg);

#endif /* __FSL_DPCON_H */

/* Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef MDP_PPP_DQ_H
#define MDP_PPP_DQ_H

#include "msm_fb_def.h"

#define MDP_PPP_DEBUG_MSG MSM_FB_DEBUG

/* The maximum number of <Reg,Val> pairs in an mdp_ppp_roi_cmd_set structure (a
 * node)
 */
#define MDP_PPP_ROI_NODE_SIZE 32

/* ROI config command (<Reg,Val> pair) for MDP PPP block */
struct mdp_ppp_roi_cmd {
	uint32_t reg;
	uint32_t val;
};

/* ROI config commands for MDP PPP block are stored in a list of
 * mdp_ppp_roi_cmd_set structures (nodes).
 */
struct mdp_ppp_roi_cmd_set {
	struct list_head node;
	uint32_t ncmds; /* number of commands in this set (node). */
	struct mdp_ppp_roi_cmd cmd[MDP_PPP_ROI_NODE_SIZE];
};

/* MDP PPP Display Job (DJob) */
struct mdp_ppp_djob {
	struct list_head entry;
	/* One ROI per MDP PPP DJob */
	struct list_head roi_cmd_list;
	struct mdp_blit_req req;
	struct fb_info *info;
	struct delayed_work cleaner;
	struct file *p_src_file, *p_dst_file;
};

extern struct completion mdp_ppp_comp;
extern boolean mdp_ppp_waiting;
extern unsigned long mdp_timer_duration;

unsigned int mdp_ppp_async_op_get(void);
void mdp_ppp_async_op_set(unsigned int flag);
void msm_fb_ensure_mem_coherency_after_dma(struct fb_info *info,
	struct mdp_blit_req *req_list, int req_list_count);
void mdp_ppp_put_img(struct file *p_src_file, struct file *p_dst_file);
void mdp_ppp_dq_init(void);
void mdp_ppp_outdw(uint32_t addr, uint32_t data);
struct mdp_ppp_djob *mdp_ppp_new_djob(void);
void mdp_ppp_clear_curr_djob(void);
void mdp_ppp_process_curr_djob(void);
int mdp_ppp_get_ret_code(void);
void mdp_ppp_djob_done(void);
void mdp_ppp_wait(void);

#endif /* MDP_PPP_DQ_H */

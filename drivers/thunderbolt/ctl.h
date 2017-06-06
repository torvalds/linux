/*
 * Thunderbolt Cactus Ridge driver - control channel and configuration commands
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#ifndef _TB_CFG
#define _TB_CFG

#include "nhi.h"
#include "tb_msgs.h"

/* control channel */
struct tb_ctl;

typedef void (*hotplug_cb)(void *data, u64 route, u8 port, bool unplug);

struct tb_ctl *tb_ctl_alloc(struct tb_nhi *nhi, hotplug_cb cb, void *cb_data);
void tb_ctl_start(struct tb_ctl *ctl);
void tb_ctl_stop(struct tb_ctl *ctl);
void tb_ctl_free(struct tb_ctl *ctl);

/* configuration commands */

#define TB_CFG_DEFAULT_TIMEOUT 5000 /* msec */

struct tb_cfg_result {
	u64 response_route;
	u32 response_port; /*
			    * If err = 1 then this is the port that send the
			    * error.
			    * If err = 0 and if this was a cfg_read/write then
			    * this is the the upstream port of the responding
			    * switch.
			    * Otherwise the field is set to zero.
			    */
	int err; /* negative errors, 0 for success, 1 for tb errors */
	enum tb_cfg_error tb_error; /* valid if err == 1 */
};


int tb_cfg_error(struct tb_ctl *ctl, u64 route, u32 port,
		 enum tb_cfg_error error);
struct tb_cfg_result tb_cfg_reset(struct tb_ctl *ctl, u64 route,
				  int timeout_msec);
struct tb_cfg_result tb_cfg_read_raw(struct tb_ctl *ctl, void *buffer,
				     u64 route, u32 port,
				     enum tb_cfg_space space, u32 offset,
				     u32 length, int timeout_msec);
struct tb_cfg_result tb_cfg_write_raw(struct tb_ctl *ctl, const void *buffer,
				      u64 route, u32 port,
				      enum tb_cfg_space space, u32 offset,
				      u32 length, int timeout_msec);
int tb_cfg_read(struct tb_ctl *ctl, void *buffer, u64 route, u32 port,
		enum tb_cfg_space space, u32 offset, u32 length);
int tb_cfg_write(struct tb_ctl *ctl, const void *buffer, u64 route, u32 port,
		 enum tb_cfg_space space, u32 offset, u32 length);
int tb_cfg_get_upstream_port(struct tb_ctl *ctl, u64 route);


#endif

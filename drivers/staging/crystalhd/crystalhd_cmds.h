/***************************************************************************
 * Copyright (c) 2005-2009, Broadcom Corporation.
 *
 *  Name: crystalhd_cmds . h
 *
 *  Description:
 *		BCM70010 Linux driver user command interfaces.
 *
 *  HISTORY:
 *
 **********************************************************************
 * This file is part of the crystalhd device driver.
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#ifndef _CRYSTALHD_CMDS_H_
#define _CRYSTALHD_CMDS_H_

/*
 * NOTE:: This is the main interface file between the Linux layer
 *        and the harware layer. This file will use the definitions
 *        from _dts_glob and dts_defs etc.. which are defined for
 *        windows.
 */
#include "crystalhd_misc.h"
#include "crystalhd_hw.h"

enum crystalhd_state {
	BC_LINK_INVALID		= 0x00,
	BC_LINK_INIT		= 0x01,
	BC_LINK_CAP_EN		= 0x02,
	BC_LINK_FMT_CHG		= 0x04,
	BC_LINK_SUSPEND		= 0x10,
	BC_LINK_PAUSED		= 0x20,
	BC_LINK_READY	= (BC_LINK_INIT | BC_LINK_CAP_EN | BC_LINK_FMT_CHG),
};

struct crystalhd_user {
	uint32_t	uid;
	uint32_t	in_use;
	uint32_t	mode;
};

#define DTS_MODE_INV	(-1)

struct crystalhd_cmd {
	uint32_t		state;
	struct crystalhd_adp	*adp;
	struct crystalhd_user	user[BC_LINK_MAX_OPENS];

	spinlock_t		ctx_lock;
	uint32_t		tx_list_id;
	uint32_t		cin_wait_exit;
	uint32_t		pwr_state_change;
	struct crystalhd_hw	hw_ctx;
};

typedef enum BC_STATUS(*crystalhd_cmd_proc)(struct crystalhd_cmd *, struct crystalhd_ioctl_data *);

struct crystalhd_cmd_tbl {
	uint32_t		cmd_id;
	const crystalhd_cmd_proc	cmd_proc;
	uint32_t		block_mon;
};

enum BC_STATUS crystalhd_suspend(struct crystalhd_cmd *ctx, struct crystalhd_ioctl_data *idata);
enum BC_STATUS crystalhd_resume(struct crystalhd_cmd *ctx);
crystalhd_cmd_proc crystalhd_get_cmd_proc(struct crystalhd_cmd *ctx, uint32_t cmd,
				      struct crystalhd_user *uc);
enum BC_STATUS crystalhd_user_open(struct crystalhd_cmd *ctx, struct crystalhd_user **user_ctx);
enum BC_STATUS crystalhd_user_close(struct crystalhd_cmd *ctx, struct crystalhd_user *uc);
enum BC_STATUS crystalhd_setup_cmd_context(struct crystalhd_cmd *ctx, struct crystalhd_adp *adp);
enum BC_STATUS crystalhd_delete_cmd_context(struct crystalhd_cmd *ctx);
bool crystalhd_cmd_interrupt(struct crystalhd_cmd *ctx);

#endif

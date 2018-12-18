/*
 * Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Oracle DAX driver API definitions
 */

#ifndef _ORADAX_H
#define	_ORADAX_H

#include <linux/types.h>

#define	CCB_KILL 0
#define	CCB_INFO 1
#define	CCB_DEQUEUE 2

struct dax_command {
	__u16 command;		/* CCB_KILL/INFO/DEQUEUE */
	__u16 ca_offset;	/* offset into mmapped completion area */
};

struct ccb_kill_result {
	__u16 action;		/* action taken to kill ccb */
};

struct ccb_info_result {
	__u16 state;		/* state of enqueued ccb */
	__u16 inst_num;		/* dax instance number of enqueued ccb */
	__u16 q_num;		/* queue number of enqueued ccb */
	__u16 q_pos;		/* ccb position in queue */
};

struct ccb_exec_result {
	__u64	status_data;	/* additional status data (e.g. bad VA) */
	__u32	status;		/* one of DAX_SUBMIT_* */
};

union ccb_result {
	struct ccb_exec_result exec;
	struct ccb_info_result info;
	struct ccb_kill_result kill;
};

#define	DAX_MMAP_LEN		(16 * 1024)
#define	DAX_MAX_CCBS		15
#define	DAX_CCB_BUF_MAXLEN	(DAX_MAX_CCBS * 64)
#define	DAX_NAME		"oradax"

/* CCB_EXEC status */
#define	DAX_SUBMIT_OK			0
#define	DAX_SUBMIT_ERR_RETRY		1
#define	DAX_SUBMIT_ERR_WOULDBLOCK	2
#define	DAX_SUBMIT_ERR_BUSY		3
#define	DAX_SUBMIT_ERR_THR_INIT		4
#define	DAX_SUBMIT_ERR_ARG_INVAL	5
#define	DAX_SUBMIT_ERR_CCB_INVAL	6
#define	DAX_SUBMIT_ERR_NO_CA_AVAIL	7
#define	DAX_SUBMIT_ERR_CCB_ARR_MMU_MISS	8
#define	DAX_SUBMIT_ERR_NOMAP		9
#define	DAX_SUBMIT_ERR_NOACCESS		10
#define	DAX_SUBMIT_ERR_TOOMANY		11
#define	DAX_SUBMIT_ERR_UNAVAIL		12
#define	DAX_SUBMIT_ERR_INTERNAL		13

/* CCB_INFO states - must match HV_CCB_STATE_* definitions */
#define	DAX_CCB_COMPLETED	0
#define	DAX_CCB_ENQUEUED	1
#define	DAX_CCB_INPROGRESS	2
#define	DAX_CCB_NOTFOUND	3

/* CCB_KILL actions - must match HV_CCB_KILL_* definitions */
#define	DAX_KILL_COMPLETED	0
#define	DAX_KILL_DEQUEUED	1
#define	DAX_KILL_KILLED		2
#define	DAX_KILL_NOTFOUND	3

#endif /* _ORADAX_H */

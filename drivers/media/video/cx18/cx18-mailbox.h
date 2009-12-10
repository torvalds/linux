/*
 *  cx18 mailbox functions
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@radix.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#ifndef _CX18_MAILBOX_H_
#define _CX18_MAILBOX_H_

/* mailbox max args */
#define MAX_MB_ARGUMENTS 6
/* compatibility, should be same as the define in cx2341x.h */
#define CX2341X_MBOX_MAX_DATA 16

#define MB_RESERVED_HANDLE_0 0
#define MB_RESERVED_HANDLE_1 0xFFFFFFFF

#define APU 0
#define CPU 1
#define EPU 2
#define HPU 3

struct cx18;

/*
 * This structure is used by CPU to provide completed MDL & buffers information.
 * Its structure is dictated by the layout of the SCB, required by the
 * firmware, but its definition needs to be here, instead of in cx18-scb.h,
 * for mailbox work order scheduling
 */
struct cx18_mdl_ack {
    u32 id;        /* ID of a completed MDL */
    u32 data_used; /* Total data filled in the MDL with 'id' */
};

/* The cx18_mailbox struct is the mailbox structure which is used for passing
   messages between processors */
struct cx18_mailbox {
    /* The sender sets a handle in 'request' after he fills the command. The
       'request' should be different than 'ack'. The sender, also, generates
       an interrupt on XPU2YPU_irq where XPU is the sender and YPU is the
       receiver. */
    u32       request;
    /* The receiver detects a new command when 'req' is different than 'ack'.
       He sets 'ack' to the same value as 'req' to clear the command. He, also,
       generates an interrupt on YPU2XPU_irq where XPU is the sender and YPU
       is the receiver. */
    u32       ack;
    u32       reserved[6];
    /* 'cmd' identifies the command. The list of these commands are in
       cx23418.h */
    u32       cmd;
    /* Each command can have up to 6 arguments */
    u32       args[MAX_MB_ARGUMENTS];
    /* The return code can be one of the codes in the file cx23418.h. If the
       command is completed successfuly, the error will be ERR_SYS_SUCCESS.
       If it is pending, the code is ERR_SYS_PENDING. If it failed, the error
       code would indicate the task from which the error originated and will
       be one of the errors in cx23418.h. In that case, the following
       applies ((error & 0xff) != 0).
       If the command is pending, the return will be passed in a MB from the
       receiver to the sender. 'req' will be returned in args[0] */
    u32       error;
};

struct cx18_stream;

struct cx18_api_func_private {
	struct cx18 *cx;
	struct cx18_stream *s;
};

int cx18_api(struct cx18 *cx, u32 cmd, int args, u32 data[]);
int cx18_vapi_result(struct cx18 *cx, u32 data[MAX_MB_ARGUMENTS], u32 cmd,
		int args, ...);
int cx18_vapi(struct cx18 *cx, u32 cmd, int args, ...);
int cx18_api_func(void *priv, u32 cmd, int in, int out,
		u32 data[CX2341X_MBOX_MAX_DATA]);

void cx18_api_epu_cmd_irq(struct cx18 *cx, int rpu);

void cx18_in_work_handler(struct work_struct *work);

#endif

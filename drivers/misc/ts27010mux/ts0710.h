/*
 * File: ts0710.h
 *
 * Portions derived from rfcomm.c, original header as follows:
 *
 * Copyright (C) 2000, 2001  Axis Communications AB
 * Copyright (C) 2002, 2004, 2009 Motorola
 *
 * Author: Mats Friden <mats.friden@axis.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Exceptionally, Axis Communications AB grants discretionary and
 * conditional permissions for additional use of the text contained
 * in the company's release of the AXIS OpenBT Stack under the
 * provisions set forth hereunder.
 *
 * Provided that, if you use the AXIS OpenBT Stack with other files,
 * that do not implement functionality as specified in the Bluetooth
 * System specification, to produce an executable, this does not by
 * itself cause the resulting executable to be covered by the GNU
 * General Public License. Your use of that executable is in no way
 * restricted on account of using the AXIS OpenBT Stack code with it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the provisions of the GNU
 * General Public License.
 *
 */

#define TS0710_MAX_CHN 17

#define SET_PF(ctr) ((ctr) | (1 << 4))
#define CLR_PF(ctr) ((ctr) & 0xef)
#define GET_PF(ctr) (((ctr) >> 4) & 0x1)

#define SHORT_PAYLOAD_SIZE 127

#define EA 1
#define FCS_SIZE 1
#define FLAG_SIZE 2

#define TS0710_MAX_HDR_SIZE 5
#define DEF_TS0710_MTU 1024

#define TS0710_BASIC_FLAG 0xF9

/* the control field */
#define SABM 0x2f
#define SABM_SIZE 4
#define UA 0x63
#define UA_SIZE 4
#define DM 0x0f
#define DISC 0x43
#define UIH 0xef

/* the type field in a multiplexer command packet */
#define TEST 0x8
#define FCON 0x28
#define FCOFF 0x18
#define MSC 0x38
#define RPN 0x24
#define RLS 0x14
#define PN 0x20
#define NSC 0x4

/* V.24 modem control signals */
#define FC 0x2
#define RTC 0x4
#define RTR 0x8
#define IC 0x40
#define DV 0x80

#define CTRL_CHAN 0		/* The control channel is defined as DLCI 0 */
#define MCC_CR 0x2
#define MCC_CMD 1		/* Multiplexer command cr */
#define MCC_RSP 0		/* Multiplexer response cr */

static inline int mcc_is_cmd(u8 type)
{
	return type & MCC_CR;
}

static inline int mcc_is_rsp(u8 type)
{
	return !(type & MCC_CR);
}


#ifdef __LITTLE_ENDIAN_BITFIELD

struct address_field {
	u8 ea:1;
	u8 cr:1;
	u8 d:1;
	u8 server_chn:5;
} __attribute__ ((packed));

static inline int ts0710_dlci(u8 addr)
{
	return (addr >> 2) & 0x3f;
}


struct short_length {
	u8 ea:1;
	u8 len:7;
} __attribute__ ((packed));

struct long_length {
	u8 ea:1;
	u8 l_len:7;
	u8 h_len;
} __attribute__ ((packed));

struct short_frame_head {
	struct address_field addr;
	u8 control;
	struct short_length length;
} __attribute__ ((packed));

struct short_frame {
	struct short_frame_head h;
	u8 data[0];
} __attribute__ ((packed));

struct long_frame_head {
	struct address_field addr;
	u8 control;
	struct long_length length;
	u8 data[0];
} __attribute__ ((packed));

struct long_frame {
	struct long_frame_head h;
	u8 data[0];
} __attribute__ ((packed));

/* Typedefinitions for structures used for the multiplexer commands */
struct mcc_type {
	u8 ea:1;
	u8 cr:1;
	u8 type:6;
} __attribute__ ((packed));

struct mcc_short_frame_head {
	struct mcc_type type;
	struct short_length length;
	u8 value[0];
} __attribute__ ((packed));

struct mcc_short_frame {
	struct mcc_short_frame_head h;
	u8 value[0];
} __attribute__ ((packed));

struct mcc_long_frame_head {
	struct mcc_type type;
	struct long_length length;
	u8 value[0];
} __attribute__ ((packed));

struct mcc_long_frame {
	struct mcc_long_frame_head h;
	u8 value[0];
} __attribute__ ((packed));

/* MSC-command */
struct v24_sigs {
	u8 ea:1;
	u8 fc:1;
	u8 rtc:1;
	u8 rtr:1;
	u8 reserved:2;
	u8 ic:1;
	u8 dv:1;
} __attribute__ ((packed));

struct brk_sigs {
	u8 ea:1;
	u8 b1:1;
	u8 b2:1;
	u8 b3:1;
	u8 len:4;
} __attribute__ ((packed));

struct msc_msg_data {
	struct address_field dlci;
	u8 v24_sigs;
} __attribute__ ((packed));

struct pn_msg_data {
	u8 dlci:6;
	u8 res1:2;

	u8 frame_type:4;
	u8 credit_flow:4;

	u8 prior:6;
	u8 res2:2;

	u8 ack_timer;
	u8 frame_sizel;
	u8 frame_sizeh;
	u8 max_nbrof_retrans;
	u8 credits;
} __attribute__ ((packed));

#else
#error Only littel-endianess supported now!
#endif

#define TS0710_FRAME_SIZE(len)						\
	((len) > SHORT_PAYLOAD_SIZE ?					\
	 (len) + FLAG_SIZE + sizeof(struct long_frame) + FCS_SIZE :	\
	 (len) + FLAG_SIZE + sizeof(struct short_frame) + FCS_SIZE)

#define TS0710_MCC_FRAME_SIZE(len) \
	TS0710_FRAME_SIZE((len) + sizeof(struct mcc_short_frame))



enum {
	REJECTED = 0,
	DISCONNECTED,
	CONNECTING,
	NEGOTIATING,
	CONNECTED,
	DISCONNECTING,
	FLOW_STOPPED
};

enum ts0710_events {
	CONNECT_IND,
	CONNECT_CFM,
	DISCONN_CFM
};

struct dlci_struct {
	u8 state;
	u8 flow_control;
	u16 mtu;
	int clients;
	struct mutex lock;
	wait_queue_head_t open_wait;
	wait_queue_head_t close_wait;
};

struct chan_struct {
	struct mutex	write_lock;
	u8		*buf;
};


/* user space interfaces */
struct ts0710_con {
	u16 mtu;

	struct dlci_struct	dlci[TS0710_MAX_CHN];
	struct chan_struct	chan[NR_MUXS];
};

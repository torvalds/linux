/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */
 #ifndef __OCTEP_CTRL_MBOX_H__
#define __OCTEP_CTRL_MBOX_H__

/*              barmem structure
 * |===========================================|
 * |Info (16 + 120 + 120 = 256 bytes)          |
 * |-------------------------------------------|
 * |magic number (8 bytes)                     |
 * |bar memory size (4 bytes)                  |
 * |reserved (4 bytes)                         |
 * |-------------------------------------------|
 * |host version (8 bytes)                     |
 * |host status (8 bytes)                      |
 * |host reserved (104 bytes)                  |
 * |-------------------------------------------|
 * |fw version (8 bytes)                       |
 * |fw status (8 bytes)                        |
 * |fw reserved (104 bytes)                    |
 * |===========================================|
 * |Host to Fw Queue info (16 bytes)           |
 * |-------------------------------------------|
 * |producer index (4 bytes)                   |
 * |consumer index (4 bytes)                   |
 * |max element size (4 bytes)                 |
 * |reserved (4 bytes)                         |
 * |===========================================|
 * |Fw to Host Queue info (16 bytes)           |
 * |-------------------------------------------|
 * |producer index (4 bytes)                   |
 * |consumer index (4 bytes)                   |
 * |max element size (4 bytes)                 |
 * |reserved (4 bytes)                         |
 * |===========================================|
 * |Host to Fw Queue ((total size-288/2) bytes)|
 * |-------------------------------------------|
 * |                                           |
 * |===========================================|
 * |===========================================|
 * |Fw to Host Queue ((total size-288/2) bytes)|
 * |-------------------------------------------|
 * |                                           |
 * |===========================================|
 */

#define OCTEP_CTRL_MBOX_MAGIC_NUMBER			0xdeaddeadbeefbeefull

/* Valid request message */
#define OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ		BIT(0)
/* Valid response message */
#define OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP		BIT(1)
/* Valid notification, no response required */
#define OCTEP_CTRL_MBOX_MSG_HDR_FLAG_NOTIFY		BIT(2)
/* Valid custom message */
#define OCTEP_CTRL_MBOX_MSG_HDR_FLAG_CUSTOM		BIT(3)

#define OCTEP_CTRL_MBOX_MSG_DESC_MAX			4

enum octep_ctrl_mbox_status {
	OCTEP_CTRL_MBOX_STATUS_INVALID = 0,
	OCTEP_CTRL_MBOX_STATUS_INIT,
	OCTEP_CTRL_MBOX_STATUS_READY,
	OCTEP_CTRL_MBOX_STATUS_UNINIT
};

/* mbox message */
union octep_ctrl_mbox_msg_hdr {
	u64 words[2];
	struct {
		/* must be 0 */
		u16 reserved1:15;
		/* vf_idx is valid if 1 */
		u16 is_vf:1;
		/* sender vf index 0-(n-1), 0 if (is_vf==0) */
		u16 vf_idx;
		/* total size of message excluding header */
		u32 sz;
		/* OCTEP_CTRL_MBOX_MSG_HDR_FLAG_* */
		u32 flags;
		/* identifier to match responses */
		u16 msg_id;
		u16 reserved2;
	} s;
};

/* mbox message buffer */
struct octep_ctrl_mbox_msg_buf {
	u32 reserved1;
	u16 reserved2;
	/* size of buffer */
	u16 sz;
	/* pointer to message buffer */
	void *msg;
};

/* mbox message */
struct octep_ctrl_mbox_msg {
	/* mbox transaction header */
	union octep_ctrl_mbox_msg_hdr hdr;
	/* number of sg buffer's */
	int sg_num;
	/* message buffer's */
	struct octep_ctrl_mbox_msg_buf sg_list[OCTEP_CTRL_MBOX_MSG_DESC_MAX];
};

/* Mbox queue */
struct octep_ctrl_mbox_q {
	/* size of queue buffer */
	u32 sz;
	/* producer address in bar mem */
	u8 __iomem *hw_prod;
	/* consumer address in bar mem */
	u8 __iomem *hw_cons;
	/* q base address in bar mem */
	u8 __iomem *hw_q;
};

struct octep_ctrl_mbox {
	/* size of bar memory */
	u32 barmem_sz;
	/* pointer to BAR memory */
	u8 __iomem *barmem;
	/* host-to-fw queue */
	struct octep_ctrl_mbox_q h2fq;
	/* fw-to-host queue */
	struct octep_ctrl_mbox_q f2hq;
	/* lock for h2fq */
	struct mutex h2fq_lock;
	/* lock for f2hq */
	struct mutex f2hq_lock;
};

/* Initialize control mbox.
 *
 * @param mbox: non-null pointer to struct octep_ctrl_mbox.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_mbox_init(struct octep_ctrl_mbox *mbox);

/* Send mbox message.
 *
 * @param mbox: non-null pointer to struct octep_ctrl_mbox.
 * @param msg:  non-null pointer to struct octep_ctrl_mbox_msg.
 *              Caller should fill msg.sz and msg.desc.sz for each message.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_mbox_send(struct octep_ctrl_mbox *mbox, struct octep_ctrl_mbox_msg *msg);

/* Retrieve mbox message.
 *
 * @param mbox: non-null pointer to struct octep_ctrl_mbox.
 * @param msg:  non-null pointer to struct octep_ctrl_mbox_msg.
 *              Caller should fill msg.sz and msg.desc.sz for each message.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_mbox_recv(struct octep_ctrl_mbox *mbox, struct octep_ctrl_mbox_msg *msg);

/* Uninitialize control mbox.
 *
 * @param mbox: non-null pointer to struct octep_ctrl_mbox.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_mbox_uninit(struct octep_ctrl_mbox *mbox);

#endif /* __OCTEP_CTRL_MBOX_H__ */

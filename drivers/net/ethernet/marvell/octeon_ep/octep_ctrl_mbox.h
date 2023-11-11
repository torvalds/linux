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
 * |element size (4 bytes)                     |
 * |element count (4 bytes)                    |
 * |===========================================|
 * |Fw to Host Queue info (16 bytes)           |
 * |-------------------------------------------|
 * |producer index (4 bytes)                   |
 * |consumer index (4 bytes)                   |
 * |element size (4 bytes)                     |
 * |element count (4 bytes)                    |
 * |===========================================|
 * |Host to Fw Queue                           |
 * |-------------------------------------------|
 * |((elem_sz + hdr(8 bytes)) * elem_cnt) bytes|
 * |===========================================|
 * |===========================================|
 * |Fw to Host Queue                           |
 * |-------------------------------------------|
 * |((elem_sz + hdr(8 bytes)) * elem_cnt) bytes|
 * |===========================================|
 */

#define OCTEP_CTRL_MBOX_MAGIC_NUMBER			0xdeaddeadbeefbeefull

/* Size of mbox info in bytes */
#define OCTEP_CTRL_MBOX_INFO_SZ				256
/* Size of mbox host to target queue info in bytes */
#define OCTEP_CTRL_MBOX_H2FQ_INFO_SZ			16
/* Size of mbox target to host queue info in bytes */
#define OCTEP_CTRL_MBOX_F2HQ_INFO_SZ			16
/* Size of mbox queue in bytes */
#define OCTEP_CTRL_MBOX_Q_SZ(sz, cnt)			(((sz) + 8) * (cnt))
/* Size of mbox in bytes */
#define OCTEP_CTRL_MBOX_SZ(hsz, hcnt, fsz, fcnt)	(OCTEP_CTRL_MBOX_INFO_SZ + \
							 OCTEP_CTRL_MBOX_H2FQ_INFO_SZ + \
							 OCTEP_CTRL_MBOX_F2HQ_INFO_SZ + \
							 OCTEP_CTRL_MBOX_Q_SZ(hsz, hcnt) + \
							 OCTEP_CTRL_MBOX_Q_SZ(fsz, fcnt))

/* Valid request message */
#define OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ		BIT(0)
/* Valid response message */
#define OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP		BIT(1)
/* Valid notification, no response required */
#define OCTEP_CTRL_MBOX_MSG_HDR_FLAG_NOTIFY		BIT(2)

enum octep_ctrl_mbox_status {
	OCTEP_CTRL_MBOX_STATUS_INVALID = 0,
	OCTEP_CTRL_MBOX_STATUS_INIT,
	OCTEP_CTRL_MBOX_STATUS_READY,
	OCTEP_CTRL_MBOX_STATUS_UNINIT
};

/* mbox message */
union octep_ctrl_mbox_msg_hdr {
	u64 word0;
	struct {
		/* OCTEP_CTRL_MBOX_MSG_HDR_FLAG_* */
		u32 flags;
		/* size of message in words excluding header */
		u32 sizew;
	};
};

/* mbox message */
struct octep_ctrl_mbox_msg {
	/* mbox transaction header */
	union octep_ctrl_mbox_msg_hdr hdr;
	/* pointer to message buffer */
	void *msg;
};

/* Mbox queue */
struct octep_ctrl_mbox_q {
	/* q element size, should be aligned to unsigned long */
	u16 elem_sz;
	/* q element count, should be power of 2 */
	u16 elem_cnt;
	/* q mask */
	u16 mask;
	/* producer address in bar mem */
	u8 __iomem *hw_prod;
	/* consumer address in bar mem */
	u8 __iomem *hw_cons;
	/* q base address in bar mem */
	u8 __iomem *hw_q;
};

struct octep_ctrl_mbox {
	/* host driver version */
	u64 version;
	/* size of bar memory */
	u32 barmem_sz;
	/* pointer to BAR memory */
	u8 __iomem *barmem;
	/* user context for callback, can be null */
	void *user_ctx;
	/* callback handler for processing request, called from octep_ctrl_mbox_recv */
	int (*process_req)(void *user_ctx, struct octep_ctrl_mbox_msg *msg);
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
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_mbox_send(struct octep_ctrl_mbox *mbox, struct octep_ctrl_mbox_msg *msg);

/* Retrieve mbox message.
 *
 * @param mbox: non-null pointer to struct octep_ctrl_mbox.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_mbox_recv(struct octep_ctrl_mbox *mbox, struct octep_ctrl_mbox_msg *msg);

/* Uninitialize control mbox.
 *
 * @param ep: non-null pointer to struct octep_ctrl_mbox.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_mbox_uninit(struct octep_ctrl_mbox *mbox);

#endif /* __OCTEP_CTRL_MBOX_H__ */

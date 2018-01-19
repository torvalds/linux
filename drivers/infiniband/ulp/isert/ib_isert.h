/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <rdma/rw.h>
#include <scsi/iser.h>


#define DRV_NAME	"isert"
#define PFX		DRV_NAME ": "

#define isert_dbg(fmt, arg...)				 \
	do {						 \
		if (unlikely(isert_debug_level > 2))	 \
			printk(KERN_DEBUG PFX "%s: " fmt,\
				__func__ , ## arg);	 \
	} while (0)

#define isert_warn(fmt, arg...)				\
	do {						\
		if (unlikely(isert_debug_level > 0))	\
			pr_warn(PFX "%s: " fmt,         \
				__func__ , ## arg);	\
	} while (0)

#define isert_info(fmt, arg...)				\
	do {						\
		if (unlikely(isert_debug_level > 1))	\
			pr_info(PFX "%s: " fmt,         \
				__func__ , ## arg);	\
	} while (0)

#define isert_err(fmt, arg...) \
	pr_err(PFX "%s: " fmt, __func__ , ## arg)

/* Constant PDU lengths calculations */
#define ISER_HEADERS_LEN	(sizeof(struct iser_ctrl) + \
				 sizeof(struct iscsi_hdr))
#define ISER_RX_PAYLOAD_SIZE	(ISER_HEADERS_LEN + ISCSI_DEF_MAX_RECV_SEG_LEN)

/* QP settings */
/* Maximal bounds on received asynchronous PDUs */
#define ISERT_MAX_TX_MISC_PDUS	4 /* NOOP_IN(2) , ASYNC_EVENT(2)   */

#define ISERT_MAX_RX_MISC_PDUS	6 /*
				   * NOOP_OUT(2), TEXT(1),
				   * SCSI_TMFUNC(2), LOGOUT(1)
				   */

#define ISCSI_DEF_XMIT_CMDS_MAX 128 /* from libiscsi.h, must be power of 2 */

#define ISERT_QP_MAX_RECV_DTOS	(ISCSI_DEF_XMIT_CMDS_MAX)

#define ISERT_MIN_POSTED_RX	(ISCSI_DEF_XMIT_CMDS_MAX >> 2)

#define ISERT_QP_MAX_REQ_DTOS	(ISCSI_DEF_XMIT_CMDS_MAX +    \
				ISERT_MAX_TX_MISC_PDUS	+ \
				ISERT_MAX_RX_MISC_PDUS)

#define ISER_RX_PAD_SIZE	(ISCSI_DEF_MAX_RECV_SEG_LEN + 4096 - \
		(ISER_RX_PAYLOAD_SIZE + sizeof(u64) + sizeof(struct ib_sge) + \
		 sizeof(struct ib_cqe) + sizeof(bool)))

#define ISCSI_ISER_SG_TABLESIZE		256

enum isert_desc_type {
	ISCSI_TX_CONTROL,
	ISCSI_TX_DATAIN
};

enum iser_conn_state {
	ISER_CONN_INIT,
	ISER_CONN_UP,
	ISER_CONN_BOUND,
	ISER_CONN_FULL_FEATURE,
	ISER_CONN_TERMINATING,
	ISER_CONN_DOWN,
};

struct iser_rx_desc {
	struct iser_ctrl iser_header;
	struct iscsi_hdr iscsi_header;
	char		data[ISCSI_DEF_MAX_RECV_SEG_LEN];
	u64		dma_addr;
	struct ib_sge	rx_sg;
	struct ib_cqe	rx_cqe;
	bool		in_use;
	char		pad[ISER_RX_PAD_SIZE];
} __packed;

static inline struct iser_rx_desc *cqe_to_rx_desc(struct ib_cqe *cqe)
{
	return container_of(cqe, struct iser_rx_desc, rx_cqe);
}

struct iser_tx_desc {
	struct iser_ctrl iser_header;
	struct iscsi_hdr iscsi_header;
	enum isert_desc_type type;
	u64		dma_addr;
	struct ib_sge	tx_sg[2];
	struct ib_cqe	tx_cqe;
	int		num_sge;
	struct ib_send_wr send_wr;
} __packed;

static inline struct iser_tx_desc *cqe_to_tx_desc(struct ib_cqe *cqe)
{
	return container_of(cqe, struct iser_tx_desc, tx_cqe);
}

struct isert_cmd {
	uint32_t		read_stag;
	uint32_t		write_stag;
	uint64_t		read_va;
	uint64_t		write_va;
	uint32_t		inv_rkey;
	u64			pdu_buf_dma;
	u32			pdu_buf_len;
	struct isert_conn	*conn;
	struct iscsi_cmd	*iscsi_cmd;
	struct iser_tx_desc	tx_desc;
	struct iser_rx_desc	*rx_desc;
	struct rdma_rw_ctx	rw;
	struct work_struct	comp_work;
	struct scatterlist	sg;
};

static inline struct isert_cmd *tx_desc_to_cmd(struct iser_tx_desc *desc)
{
	return container_of(desc, struct isert_cmd, tx_desc);
}

struct isert_device;

struct isert_conn {
	enum iser_conn_state	state;
	u32			responder_resources;
	u32			initiator_depth;
	bool			pi_support;
	struct iser_rx_desc	*login_req_buf;
	char			*login_rsp_buf;
	u64			login_req_dma;
	int			login_req_len;
	u64			login_rsp_dma;
	struct iser_rx_desc	*rx_descs;
	struct ib_recv_wr	rx_wr[ISERT_QP_MAX_RECV_DTOS];
	struct iscsi_conn	*conn;
	struct list_head	node;
	struct completion	login_comp;
	struct completion	login_req_comp;
	struct iser_tx_desc	login_tx_desc;
	struct rdma_cm_id	*cm_id;
	struct ib_qp		*qp;
	struct isert_device	*device;
	struct mutex		mutex;
	struct kref		kref;
	struct work_struct	release_work;
	bool                    logout_posted;
	bool                    snd_w_inv;
	wait_queue_head_t	rem_wait;
	bool			dev_removed;
};

#define ISERT_MAX_CQ 64

/**
 * struct isert_comp - iSER completion context
 *
 * @device:     pointer to device handle
 * @cq:         completion queue
 * @active_qps: Number of active QPs attached
 *              to completion context
 */
struct isert_comp {
	struct isert_device     *device;
	struct ib_cq		*cq;
	int                      active_qps;
};

struct isert_device {
	bool			pi_capable;
	int			refcount;
	struct ib_device	*ib_device;
	struct ib_pd		*pd;
	struct isert_comp	*comps;
	int                     comps_used;
	struct list_head	dev_node;
};

struct isert_np {
	struct iscsi_np         *np;
	struct semaphore	sem;
	struct rdma_cm_id	*cm_id;
	struct mutex		mutex;
	struct list_head	accepted;
	struct list_head	pending;
};

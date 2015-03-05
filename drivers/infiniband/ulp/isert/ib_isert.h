#include <linux/socket.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>

#define ISERT_RDMA_LISTEN_BACKLOG	10

enum isert_desc_type {
	ISCSI_TX_CONTROL,
	ISCSI_TX_DATAIN
};

enum iser_ib_op_code {
	ISER_IB_RECV,
	ISER_IB_SEND,
	ISER_IB_RDMA_WRITE,
	ISER_IB_RDMA_READ,
};

enum iser_conn_state {
	ISER_CONN_INIT,
	ISER_CONN_UP,
	ISER_CONN_FULL_FEATURE,
	ISER_CONN_TERMINATING,
	ISER_CONN_DOWN,
};

struct iser_rx_desc {
	struct iser_hdr iser_header;
	struct iscsi_hdr iscsi_header;
	char		data[ISER_RECV_DATA_SEG_LEN];
	u64		dma_addr;
	struct ib_sge	rx_sg;
	char		pad[ISER_RX_PAD_SIZE];
} __packed;

struct iser_tx_desc {
	struct iser_hdr iser_header;
	struct iscsi_hdr iscsi_header;
	enum isert_desc_type type;
	u64		dma_addr;
	struct ib_sge	tx_sg[2];
	int		num_sge;
	struct isert_cmd *isert_cmd;
	struct ib_send_wr send_wr;
} __packed;

struct isert_rdma_wr {
	struct list_head	wr_list;
	struct isert_cmd	*isert_cmd;
	enum iser_ib_op_code	iser_ib_op;
	struct ib_sge		*ib_sge;
	int			num_sge;
	struct scatterlist	*sge;
	int			send_wr_num;
	struct ib_send_wr	*send_wr;
};

struct isert_cmd {
	uint32_t		read_stag;
	uint32_t		write_stag;
	uint64_t		read_va;
	uint64_t		write_va;
	u64			sense_buf_dma;
	u32			sense_buf_len;
	u32			read_va_off;
	u32			write_va_off;
	u32			rdma_wr_num;
	struct isert_conn	*conn;
	struct iscsi_cmd	iscsi_cmd;
	struct ib_sge		*ib_sge;
	struct iser_tx_desc	tx_desc;
	struct isert_rdma_wr	rdma_wr;
	struct work_struct	comp_work;
};

struct isert_device;

struct isert_conn {
	enum iser_conn_state	state;
	int			post_recv_buf_count;
	atomic_t		post_send_buf_count;
	u32			responder_resources;
	u32			initiator_depth;
	u32			max_sge;
	char			*login_buf;
	char			*login_req_buf;
	char			*login_rsp_buf;
	u64			login_req_dma;
	int			login_req_len;
	u64			login_rsp_dma;
	unsigned int		conn_rx_desc_head;
	struct iser_rx_desc	*conn_rx_descs;
	struct ib_recv_wr	conn_rx_wr[ISERT_MIN_POSTED_RX];
	struct iscsi_conn	*conn;
	struct list_head	conn_accept_node;
	struct completion	conn_login_comp;
	struct completion	login_req_comp;
	struct iser_tx_desc	conn_login_tx_desc;
	struct rdma_cm_id	*conn_cm_id;
	struct ib_pd		*conn_pd;
	struct ib_mr		*conn_mr;
	struct ib_qp		*conn_qp;
	struct isert_device	*conn_device;
	struct mutex		conn_mutex;
	struct completion	conn_wait;
	struct completion	conn_wait_comp_err;
	struct kref		conn_kref;
	struct work_struct	release_work;
};

#define ISERT_MAX_CQ 64

struct isert_cq_desc {
	struct isert_device	*device;
	int			cq_index;
	struct work_struct	cq_rx_work;
	struct work_struct	cq_tx_work;
};

struct isert_device {
	int			cqs_used;
	int			refcount;
	int			cq_active_qps[ISERT_MAX_CQ];
	struct ib_device	*ib_device;
	struct ib_pd		*dev_pd;
	struct ib_mr		*dev_mr;
	struct ib_cq		*dev_rx_cq[ISERT_MAX_CQ];
	struct ib_cq		*dev_tx_cq[ISERT_MAX_CQ];
	struct isert_cq_desc	*cq_desc;
	struct list_head	dev_node;
};

struct isert_np {
	struct iscsi_np         *np;
	struct semaphore	np_sem;
	struct rdma_cm_id	*np_cm_id;
	struct mutex		np_accept_mutex;
	struct list_head	np_accept_list;
	struct completion	np_login_comp;
};

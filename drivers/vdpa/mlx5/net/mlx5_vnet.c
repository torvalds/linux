// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd. */

#include <linux/module.h>
#include <linux/vdpa.h>
#include <linux/vringh.h>
#include <uapi/linux/virtio_net.h>
#include <uapi/linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/auxiliary_bus.h>
#include <linux/mlx5/cq.h>
#include <linux/mlx5/qp.h>
#include <linux/mlx5/device.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/fs.h>
#include <linux/mlx5/mlx5_ifc_vdpa.h>
#include <linux/mlx5/mpfs.h>
#include "mlx5_vdpa.h"

MODULE_AUTHOR("Eli Cohen <eli@mellanox.com>");
MODULE_DESCRIPTION("Mellanox VDPA driver");
MODULE_LICENSE("Dual BSD/GPL");

#define to_mlx5_vdpa_ndev(__mvdev)                                             \
	container_of(__mvdev, struct mlx5_vdpa_net, mvdev)
#define to_mvdev(__vdev) container_of((__vdev), struct mlx5_vdpa_dev, vdev)

#define VALID_FEATURES_MASK                                                                        \
	(BIT_ULL(VIRTIO_NET_F_CSUM) | BIT_ULL(VIRTIO_NET_F_GUEST_CSUM) |                                   \
	 BIT_ULL(VIRTIO_NET_F_CTRL_GUEST_OFFLOADS) | BIT_ULL(VIRTIO_NET_F_MTU) | BIT_ULL(VIRTIO_NET_F_MAC) |   \
	 BIT_ULL(VIRTIO_NET_F_GUEST_TSO4) | BIT_ULL(VIRTIO_NET_F_GUEST_TSO6) |                             \
	 BIT_ULL(VIRTIO_NET_F_GUEST_ECN) | BIT_ULL(VIRTIO_NET_F_GUEST_UFO) | BIT_ULL(VIRTIO_NET_F_HOST_TSO4) | \
	 BIT_ULL(VIRTIO_NET_F_HOST_TSO6) | BIT_ULL(VIRTIO_NET_F_HOST_ECN) | BIT_ULL(VIRTIO_NET_F_HOST_UFO) |   \
	 BIT_ULL(VIRTIO_NET_F_MRG_RXBUF) | BIT_ULL(VIRTIO_NET_F_STATUS) | BIT_ULL(VIRTIO_NET_F_CTRL_VQ) |      \
	 BIT_ULL(VIRTIO_NET_F_CTRL_RX) | BIT_ULL(VIRTIO_NET_F_CTRL_VLAN) |                                 \
	 BIT_ULL(VIRTIO_NET_F_CTRL_RX_EXTRA) | BIT_ULL(VIRTIO_NET_F_GUEST_ANNOUNCE) |                      \
	 BIT_ULL(VIRTIO_NET_F_MQ) | BIT_ULL(VIRTIO_NET_F_CTRL_MAC_ADDR) | BIT_ULL(VIRTIO_NET_F_HASH_REPORT) |  \
	 BIT_ULL(VIRTIO_NET_F_RSS) | BIT_ULL(VIRTIO_NET_F_RSC_EXT) | BIT_ULL(VIRTIO_NET_F_STANDBY) |           \
	 BIT_ULL(VIRTIO_NET_F_SPEED_DUPLEX) | BIT_ULL(VIRTIO_F_NOTIFY_ON_EMPTY) |                          \
	 BIT_ULL(VIRTIO_F_ANY_LAYOUT) | BIT_ULL(VIRTIO_F_VERSION_1) | BIT_ULL(VIRTIO_F_ACCESS_PLATFORM) |      \
	 BIT_ULL(VIRTIO_F_RING_PACKED) | BIT_ULL(VIRTIO_F_ORDER_PLATFORM) | BIT_ULL(VIRTIO_F_SR_IOV))

#define VALID_STATUS_MASK                                                                          \
	(VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER | VIRTIO_CONFIG_S_DRIVER_OK |        \
	 VIRTIO_CONFIG_S_FEATURES_OK | VIRTIO_CONFIG_S_NEEDS_RESET | VIRTIO_CONFIG_S_FAILED)

#define MLX5_FEATURE(_mvdev, _feature) (!!((_mvdev)->actual_features & BIT_ULL(_feature)))

struct mlx5_vdpa_net_resources {
	u32 tisn;
	u32 tdn;
	u32 tirn;
	u32 rqtn;
	bool valid;
};

struct mlx5_vdpa_cq_buf {
	struct mlx5_frag_buf_ctrl fbc;
	struct mlx5_frag_buf frag_buf;
	int cqe_size;
	int nent;
};

struct mlx5_vdpa_cq {
	struct mlx5_core_cq mcq;
	struct mlx5_vdpa_cq_buf buf;
	struct mlx5_db db;
	int cqe;
};

struct mlx5_vdpa_umem {
	struct mlx5_frag_buf_ctrl fbc;
	struct mlx5_frag_buf frag_buf;
	int size;
	u32 id;
};

struct mlx5_vdpa_qp {
	struct mlx5_core_qp mqp;
	struct mlx5_frag_buf frag_buf;
	struct mlx5_db db;
	u16 head;
	bool fw;
};

struct mlx5_vq_restore_info {
	u32 num_ent;
	u64 desc_addr;
	u64 device_addr;
	u64 driver_addr;
	u16 avail_index;
	u16 used_index;
	bool ready;
	bool restore;
};

struct mlx5_vdpa_virtqueue {
	bool ready;
	u64 desc_addr;
	u64 device_addr;
	u64 driver_addr;
	u32 num_ent;

	/* Resources for implementing the notification channel from the device
	 * to the driver. fwqp is the firmware end of an RC connection; the
	 * other end is vqqp used by the driver. cq is is where completions are
	 * reported.
	 */
	struct mlx5_vdpa_cq cq;
	struct mlx5_vdpa_qp fwqp;
	struct mlx5_vdpa_qp vqqp;

	/* umem resources are required for the virtqueue operation. They're use
	 * is internal and they must be provided by the driver.
	 */
	struct mlx5_vdpa_umem umem1;
	struct mlx5_vdpa_umem umem2;
	struct mlx5_vdpa_umem umem3;

	bool initialized;
	int index;
	u32 virtq_id;
	struct mlx5_vdpa_net *ndev;
	u16 avail_idx;
	u16 used_idx;
	int fw_state;

	/* keep last in the struct */
	struct mlx5_vq_restore_info ri;
};

/* We will remove this limitation once mlx5_vdpa_alloc_resources()
 * provides for driver space allocation
 */
#define MLX5_MAX_SUPPORTED_VQS 16

static bool is_index_valid(struct mlx5_vdpa_dev *mvdev, u16 idx)
{
	if (unlikely(idx > mvdev->max_idx))
		return false;

	return true;
}

struct mlx5_vdpa_net {
	struct mlx5_vdpa_dev mvdev;
	struct mlx5_vdpa_net_resources res;
	struct virtio_net_config config;
	struct mlx5_vdpa_virtqueue vqs[MLX5_MAX_SUPPORTED_VQS];
	struct vdpa_callback event_cbs[MLX5_MAX_SUPPORTED_VQS + 1];

	/* Serialize vq resources creation and destruction. This is required
	 * since memory map might change and we need to destroy and create
	 * resources while driver in operational.
	 */
	struct mutex reslock;
	struct mlx5_flow_table *rxft;
	struct mlx5_fc *rx_counter;
	struct mlx5_flow_handle *rx_rule;
	bool setup;
	u16 mtu;
	u32 cur_num_vqs;
};

static void free_resources(struct mlx5_vdpa_net *ndev);
static void init_mvqs(struct mlx5_vdpa_net *ndev);
static int setup_driver(struct mlx5_vdpa_dev *mvdev);
static void teardown_driver(struct mlx5_vdpa_net *ndev);

static bool mlx5_vdpa_debug;

#define MLX5_CVQ_MAX_ENT 16

#define MLX5_LOG_VIO_FLAG(_feature)                                                                \
	do {                                                                                       \
		if (features & BIT_ULL(_feature))                                                  \
			mlx5_vdpa_info(mvdev, "%s\n", #_feature);                                  \
	} while (0)

#define MLX5_LOG_VIO_STAT(_status)                                                                 \
	do {                                                                                       \
		if (status & (_status))                                                            \
			mlx5_vdpa_info(mvdev, "%s\n", #_status);                                   \
	} while (0)

/* TODO: cross-endian support */
static inline bool mlx5_vdpa_is_little_endian(struct mlx5_vdpa_dev *mvdev)
{
	return virtio_legacy_is_little_endian() ||
		(mvdev->actual_features & BIT_ULL(VIRTIO_F_VERSION_1));
}

static u16 mlx5vdpa16_to_cpu(struct mlx5_vdpa_dev *mvdev, __virtio16 val)
{
	return __virtio16_to_cpu(mlx5_vdpa_is_little_endian(mvdev), val);
}

static __virtio16 cpu_to_mlx5vdpa16(struct mlx5_vdpa_dev *mvdev, u16 val)
{
	return __cpu_to_virtio16(mlx5_vdpa_is_little_endian(mvdev), val);
}

static inline u32 mlx5_vdpa_max_qps(int max_vqs)
{
	return max_vqs / 2;
}

static u16 ctrl_vq_idx(struct mlx5_vdpa_dev *mvdev)
{
	if (!(mvdev->actual_features & BIT_ULL(VIRTIO_NET_F_MQ)))
		return 2;

	return 2 * mlx5_vdpa_max_qps(mvdev->max_vqs);
}

static bool is_ctrl_vq_idx(struct mlx5_vdpa_dev *mvdev, u16 idx)
{
	return idx == ctrl_vq_idx(mvdev);
}

static void print_status(struct mlx5_vdpa_dev *mvdev, u8 status, bool set)
{
	if (status & ~VALID_STATUS_MASK)
		mlx5_vdpa_warn(mvdev, "Warning: there are invalid status bits 0x%x\n",
			       status & ~VALID_STATUS_MASK);

	if (!mlx5_vdpa_debug)
		return;

	mlx5_vdpa_info(mvdev, "driver status %s", set ? "set" : "get");
	if (set && !status) {
		mlx5_vdpa_info(mvdev, "driver resets the device\n");
		return;
	}

	MLX5_LOG_VIO_STAT(VIRTIO_CONFIG_S_ACKNOWLEDGE);
	MLX5_LOG_VIO_STAT(VIRTIO_CONFIG_S_DRIVER);
	MLX5_LOG_VIO_STAT(VIRTIO_CONFIG_S_DRIVER_OK);
	MLX5_LOG_VIO_STAT(VIRTIO_CONFIG_S_FEATURES_OK);
	MLX5_LOG_VIO_STAT(VIRTIO_CONFIG_S_NEEDS_RESET);
	MLX5_LOG_VIO_STAT(VIRTIO_CONFIG_S_FAILED);
}

static void print_features(struct mlx5_vdpa_dev *mvdev, u64 features, bool set)
{
	if (features & ~VALID_FEATURES_MASK)
		mlx5_vdpa_warn(mvdev, "There are invalid feature bits 0x%llx\n",
			       features & ~VALID_FEATURES_MASK);

	if (!mlx5_vdpa_debug)
		return;

	mlx5_vdpa_info(mvdev, "driver %s feature bits:\n", set ? "sets" : "reads");
	if (!features)
		mlx5_vdpa_info(mvdev, "all feature bits are cleared\n");

	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_CSUM);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_GUEST_CSUM);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_CTRL_GUEST_OFFLOADS);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_MTU);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_MAC);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_GUEST_TSO4);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_GUEST_TSO6);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_GUEST_ECN);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_GUEST_UFO);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_HOST_TSO4);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_HOST_TSO6);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_HOST_ECN);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_HOST_UFO);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_MRG_RXBUF);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_STATUS);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_CTRL_VQ);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_CTRL_RX);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_CTRL_VLAN);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_CTRL_RX_EXTRA);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_GUEST_ANNOUNCE);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_MQ);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_CTRL_MAC_ADDR);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_HASH_REPORT);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_RSS);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_RSC_EXT);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_STANDBY);
	MLX5_LOG_VIO_FLAG(VIRTIO_NET_F_SPEED_DUPLEX);
	MLX5_LOG_VIO_FLAG(VIRTIO_F_NOTIFY_ON_EMPTY);
	MLX5_LOG_VIO_FLAG(VIRTIO_F_ANY_LAYOUT);
	MLX5_LOG_VIO_FLAG(VIRTIO_F_VERSION_1);
	MLX5_LOG_VIO_FLAG(VIRTIO_F_ACCESS_PLATFORM);
	MLX5_LOG_VIO_FLAG(VIRTIO_F_RING_PACKED);
	MLX5_LOG_VIO_FLAG(VIRTIO_F_ORDER_PLATFORM);
	MLX5_LOG_VIO_FLAG(VIRTIO_F_SR_IOV);
}

static int create_tis(struct mlx5_vdpa_net *ndev)
{
	struct mlx5_vdpa_dev *mvdev = &ndev->mvdev;
	u32 in[MLX5_ST_SZ_DW(create_tis_in)] = {};
	void *tisc;
	int err;

	tisc = MLX5_ADDR_OF(create_tis_in, in, ctx);
	MLX5_SET(tisc, tisc, transport_domain, ndev->res.tdn);
	err = mlx5_vdpa_create_tis(mvdev, in, &ndev->res.tisn);
	if (err)
		mlx5_vdpa_warn(mvdev, "create TIS (%d)\n", err);

	return err;
}

static void destroy_tis(struct mlx5_vdpa_net *ndev)
{
	mlx5_vdpa_destroy_tis(&ndev->mvdev, ndev->res.tisn);
}

#define MLX5_VDPA_CQE_SIZE 64
#define MLX5_VDPA_LOG_CQE_SIZE ilog2(MLX5_VDPA_CQE_SIZE)

static int cq_frag_buf_alloc(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_cq_buf *buf, int nent)
{
	struct mlx5_frag_buf *frag_buf = &buf->frag_buf;
	u8 log_wq_stride = MLX5_VDPA_LOG_CQE_SIZE;
	u8 log_wq_sz = MLX5_VDPA_LOG_CQE_SIZE;
	int err;

	err = mlx5_frag_buf_alloc_node(ndev->mvdev.mdev, nent * MLX5_VDPA_CQE_SIZE, frag_buf,
				       ndev->mvdev.mdev->priv.numa_node);
	if (err)
		return err;

	mlx5_init_fbc(frag_buf->frags, log_wq_stride, log_wq_sz, &buf->fbc);

	buf->cqe_size = MLX5_VDPA_CQE_SIZE;
	buf->nent = nent;

	return 0;
}

static int umem_frag_buf_alloc(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_umem *umem, int size)
{
	struct mlx5_frag_buf *frag_buf = &umem->frag_buf;

	return mlx5_frag_buf_alloc_node(ndev->mvdev.mdev, size, frag_buf,
					ndev->mvdev.mdev->priv.numa_node);
}

static void cq_frag_buf_free(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_cq_buf *buf)
{
	mlx5_frag_buf_free(ndev->mvdev.mdev, &buf->frag_buf);
}

static void *get_cqe(struct mlx5_vdpa_cq *vcq, int n)
{
	return mlx5_frag_buf_get_wqe(&vcq->buf.fbc, n);
}

static void cq_frag_buf_init(struct mlx5_vdpa_cq *vcq, struct mlx5_vdpa_cq_buf *buf)
{
	struct mlx5_cqe64 *cqe64;
	void *cqe;
	int i;

	for (i = 0; i < buf->nent; i++) {
		cqe = get_cqe(vcq, i);
		cqe64 = cqe;
		cqe64->op_own = MLX5_CQE_INVALID << 4;
	}
}

static void *get_sw_cqe(struct mlx5_vdpa_cq *cq, int n)
{
	struct mlx5_cqe64 *cqe64 = get_cqe(cq, n & (cq->cqe - 1));

	if (likely(get_cqe_opcode(cqe64) != MLX5_CQE_INVALID) &&
	    !((cqe64->op_own & MLX5_CQE_OWNER_MASK) ^ !!(n & cq->cqe)))
		return cqe64;

	return NULL;
}

static void rx_post(struct mlx5_vdpa_qp *vqp, int n)
{
	vqp->head += n;
	vqp->db.db[0] = cpu_to_be32(vqp->head);
}

static void qp_prepare(struct mlx5_vdpa_net *ndev, bool fw, void *in,
		       struct mlx5_vdpa_virtqueue *mvq, u32 num_ent)
{
	struct mlx5_vdpa_qp *vqp;
	__be64 *pas;
	void *qpc;

	vqp = fw ? &mvq->fwqp : &mvq->vqqp;
	MLX5_SET(create_qp_in, in, uid, ndev->mvdev.res.uid);
	qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);
	if (vqp->fw) {
		/* Firmware QP is allocated by the driver for the firmware's
		 * use so we can skip part of the params as they will be chosen by firmware
		 */
		qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);
		MLX5_SET(qpc, qpc, rq_type, MLX5_ZERO_LEN_RQ);
		MLX5_SET(qpc, qpc, no_sq, 1);
		return;
	}

	MLX5_SET(qpc, qpc, st, MLX5_QP_ST_RC);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
	MLX5_SET(qpc, qpc, pd, ndev->mvdev.res.pdn);
	MLX5_SET(qpc, qpc, mtu, MLX5_QPC_MTU_256_BYTES);
	MLX5_SET(qpc, qpc, uar_page, ndev->mvdev.res.uar->index);
	MLX5_SET(qpc, qpc, log_page_size, vqp->frag_buf.page_shift - MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET(qpc, qpc, no_sq, 1);
	MLX5_SET(qpc, qpc, cqn_rcv, mvq->cq.mcq.cqn);
	MLX5_SET(qpc, qpc, log_rq_size, ilog2(num_ent));
	MLX5_SET(qpc, qpc, rq_type, MLX5_NON_ZERO_RQ);
	pas = (__be64 *)MLX5_ADDR_OF(create_qp_in, in, pas);
	mlx5_fill_page_frag_array(&vqp->frag_buf, pas);
}

static int rq_buf_alloc(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_qp *vqp, u32 num_ent)
{
	return mlx5_frag_buf_alloc_node(ndev->mvdev.mdev,
					num_ent * sizeof(struct mlx5_wqe_data_seg), &vqp->frag_buf,
					ndev->mvdev.mdev->priv.numa_node);
}

static void rq_buf_free(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_qp *vqp)
{
	mlx5_frag_buf_free(ndev->mvdev.mdev, &vqp->frag_buf);
}

static int qp_create(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq,
		     struct mlx5_vdpa_qp *vqp)
{
	struct mlx5_core_dev *mdev = ndev->mvdev.mdev;
	int inlen = MLX5_ST_SZ_BYTES(create_qp_in);
	u32 out[MLX5_ST_SZ_DW(create_qp_out)] = {};
	void *qpc;
	void *in;
	int err;

	if (!vqp->fw) {
		vqp = &mvq->vqqp;
		err = rq_buf_alloc(ndev, vqp, mvq->num_ent);
		if (err)
			return err;

		err = mlx5_db_alloc(ndev->mvdev.mdev, &vqp->db);
		if (err)
			goto err_db;
		inlen += vqp->frag_buf.npages * sizeof(__be64);
	}

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_kzalloc;
	}

	qp_prepare(ndev, vqp->fw, in, mvq, mvq->num_ent);
	qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);
	MLX5_SET(qpc, qpc, st, MLX5_QP_ST_RC);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
	MLX5_SET(qpc, qpc, pd, ndev->mvdev.res.pdn);
	MLX5_SET(qpc, qpc, mtu, MLX5_QPC_MTU_256_BYTES);
	if (!vqp->fw)
		MLX5_SET64(qpc, qpc, dbr_addr, vqp->db.dma);
	MLX5_SET(create_qp_in, in, opcode, MLX5_CMD_OP_CREATE_QP);
	err = mlx5_cmd_exec(mdev, in, inlen, out, sizeof(out));
	kfree(in);
	if (err)
		goto err_kzalloc;

	vqp->mqp.uid = ndev->mvdev.res.uid;
	vqp->mqp.qpn = MLX5_GET(create_qp_out, out, qpn);

	if (!vqp->fw)
		rx_post(vqp, mvq->num_ent);

	return 0;

err_kzalloc:
	if (!vqp->fw)
		mlx5_db_free(ndev->mvdev.mdev, &vqp->db);
err_db:
	if (!vqp->fw)
		rq_buf_free(ndev, vqp);

	return err;
}

static void qp_destroy(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_qp *vqp)
{
	u32 in[MLX5_ST_SZ_DW(destroy_qp_in)] = {};

	MLX5_SET(destroy_qp_in, in, opcode, MLX5_CMD_OP_DESTROY_QP);
	MLX5_SET(destroy_qp_in, in, qpn, vqp->mqp.qpn);
	MLX5_SET(destroy_qp_in, in, uid, ndev->mvdev.res.uid);
	if (mlx5_cmd_exec_in(ndev->mvdev.mdev, destroy_qp, in))
		mlx5_vdpa_warn(&ndev->mvdev, "destroy qp 0x%x\n", vqp->mqp.qpn);
	if (!vqp->fw) {
		mlx5_db_free(ndev->mvdev.mdev, &vqp->db);
		rq_buf_free(ndev, vqp);
	}
}

static void *next_cqe_sw(struct mlx5_vdpa_cq *cq)
{
	return get_sw_cqe(cq, cq->mcq.cons_index);
}

static int mlx5_vdpa_poll_one(struct mlx5_vdpa_cq *vcq)
{
	struct mlx5_cqe64 *cqe64;

	cqe64 = next_cqe_sw(vcq);
	if (!cqe64)
		return -EAGAIN;

	vcq->mcq.cons_index++;
	return 0;
}

static void mlx5_vdpa_handle_completions(struct mlx5_vdpa_virtqueue *mvq, int num)
{
	struct mlx5_vdpa_net *ndev = mvq->ndev;
	struct vdpa_callback *event_cb;

	event_cb = &ndev->event_cbs[mvq->index];
	mlx5_cq_set_ci(&mvq->cq.mcq);

	/* make sure CQ cosumer update is visible to the hardware before updating
	 * RX doorbell record.
	 */
	dma_wmb();
	rx_post(&mvq->vqqp, num);
	if (event_cb->callback)
		event_cb->callback(event_cb->private);
}

static void mlx5_vdpa_cq_comp(struct mlx5_core_cq *mcq, struct mlx5_eqe *eqe)
{
	struct mlx5_vdpa_virtqueue *mvq = container_of(mcq, struct mlx5_vdpa_virtqueue, cq.mcq);
	struct mlx5_vdpa_net *ndev = mvq->ndev;
	void __iomem *uar_page = ndev->mvdev.res.uar->map;
	int num = 0;

	while (!mlx5_vdpa_poll_one(&mvq->cq)) {
		num++;
		if (num > mvq->num_ent / 2) {
			/* If completions keep coming while we poll, we want to
			 * let the hardware know that we consumed them by
			 * updating the doorbell record.  We also let vdpa core
			 * know about this so it passes it on the virtio driver
			 * on the guest.
			 */
			mlx5_vdpa_handle_completions(mvq, num);
			num = 0;
		}
	}

	if (num)
		mlx5_vdpa_handle_completions(mvq, num);

	mlx5_cq_arm(&mvq->cq.mcq, MLX5_CQ_DB_REQ_NOT, uar_page, mvq->cq.mcq.cons_index);
}

static int cq_create(struct mlx5_vdpa_net *ndev, u16 idx, u32 num_ent)
{
	struct mlx5_vdpa_virtqueue *mvq = &ndev->vqs[idx];
	struct mlx5_core_dev *mdev = ndev->mvdev.mdev;
	void __iomem *uar_page = ndev->mvdev.res.uar->map;
	u32 out[MLX5_ST_SZ_DW(create_cq_out)];
	struct mlx5_vdpa_cq *vcq = &mvq->cq;
	__be64 *pas;
	int inlen;
	void *cqc;
	void *in;
	int err;
	int eqn;

	err = mlx5_db_alloc(mdev, &vcq->db);
	if (err)
		return err;

	vcq->mcq.set_ci_db = vcq->db.db;
	vcq->mcq.arm_db = vcq->db.db + 1;
	vcq->mcq.cqe_sz = 64;

	err = cq_frag_buf_alloc(ndev, &vcq->buf, num_ent);
	if (err)
		goto err_db;

	cq_frag_buf_init(vcq, &vcq->buf);

	inlen = MLX5_ST_SZ_BYTES(create_cq_in) +
		MLX5_FLD_SZ_BYTES(create_cq_in, pas[0]) * vcq->buf.frag_buf.npages;
	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_vzalloc;
	}

	MLX5_SET(create_cq_in, in, uid, ndev->mvdev.res.uid);
	pas = (__be64 *)MLX5_ADDR_OF(create_cq_in, in, pas);
	mlx5_fill_page_frag_array(&vcq->buf.frag_buf, pas);

	cqc = MLX5_ADDR_OF(create_cq_in, in, cq_context);
	MLX5_SET(cqc, cqc, log_page_size, vcq->buf.frag_buf.page_shift - MLX5_ADAPTER_PAGE_SHIFT);

	/* Use vector 0 by default. Consider adding code to choose least used
	 * vector.
	 */
	err = mlx5_vector2eqn(mdev, 0, &eqn);
	if (err)
		goto err_vec;

	cqc = MLX5_ADDR_OF(create_cq_in, in, cq_context);
	MLX5_SET(cqc, cqc, log_cq_size, ilog2(num_ent));
	MLX5_SET(cqc, cqc, uar_page, ndev->mvdev.res.uar->index);
	MLX5_SET(cqc, cqc, c_eqn, eqn);
	MLX5_SET64(cqc, cqc, dbr_addr, vcq->db.dma);

	err = mlx5_core_create_cq(mdev, &vcq->mcq, in, inlen, out, sizeof(out));
	if (err)
		goto err_vec;

	vcq->mcq.comp = mlx5_vdpa_cq_comp;
	vcq->cqe = num_ent;
	vcq->mcq.set_ci_db = vcq->db.db;
	vcq->mcq.arm_db = vcq->db.db + 1;
	mlx5_cq_arm(&mvq->cq.mcq, MLX5_CQ_DB_REQ_NOT, uar_page, mvq->cq.mcq.cons_index);
	kfree(in);
	return 0;

err_vec:
	kfree(in);
err_vzalloc:
	cq_frag_buf_free(ndev, &vcq->buf);
err_db:
	mlx5_db_free(ndev->mvdev.mdev, &vcq->db);
	return err;
}

static void cq_destroy(struct mlx5_vdpa_net *ndev, u16 idx)
{
	struct mlx5_vdpa_virtqueue *mvq = &ndev->vqs[idx];
	struct mlx5_core_dev *mdev = ndev->mvdev.mdev;
	struct mlx5_vdpa_cq *vcq = &mvq->cq;

	if (mlx5_core_destroy_cq(mdev, &vcq->mcq)) {
		mlx5_vdpa_warn(&ndev->mvdev, "destroy CQ 0x%x\n", vcq->mcq.cqn);
		return;
	}
	cq_frag_buf_free(ndev, &vcq->buf);
	mlx5_db_free(ndev->mvdev.mdev, &vcq->db);
}

static void set_umem_size(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq, int num,
			  struct mlx5_vdpa_umem **umemp)
{
	struct mlx5_core_dev *mdev = ndev->mvdev.mdev;
	int p_a;
	int p_b;

	switch (num) {
	case 1:
		p_a = MLX5_CAP_DEV_VDPA_EMULATION(mdev, umem_1_buffer_param_a);
		p_b = MLX5_CAP_DEV_VDPA_EMULATION(mdev, umem_1_buffer_param_b);
		*umemp = &mvq->umem1;
		break;
	case 2:
		p_a = MLX5_CAP_DEV_VDPA_EMULATION(mdev, umem_2_buffer_param_a);
		p_b = MLX5_CAP_DEV_VDPA_EMULATION(mdev, umem_2_buffer_param_b);
		*umemp = &mvq->umem2;
		break;
	case 3:
		p_a = MLX5_CAP_DEV_VDPA_EMULATION(mdev, umem_3_buffer_param_a);
		p_b = MLX5_CAP_DEV_VDPA_EMULATION(mdev, umem_3_buffer_param_b);
		*umemp = &mvq->umem3;
		break;
	}
	(*umemp)->size = p_a * mvq->num_ent + p_b;
}

static void umem_frag_buf_free(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_umem *umem)
{
	mlx5_frag_buf_free(ndev->mvdev.mdev, &umem->frag_buf);
}

static int create_umem(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq, int num)
{
	int inlen;
	u32 out[MLX5_ST_SZ_DW(create_umem_out)] = {};
	void *um;
	void *in;
	int err;
	__be64 *pas;
	struct mlx5_vdpa_umem *umem;

	set_umem_size(ndev, mvq, num, &umem);
	err = umem_frag_buf_alloc(ndev, umem, umem->size);
	if (err)
		return err;

	inlen = MLX5_ST_SZ_BYTES(create_umem_in) + MLX5_ST_SZ_BYTES(mtt) * umem->frag_buf.npages;

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_in;
	}

	MLX5_SET(create_umem_in, in, opcode, MLX5_CMD_OP_CREATE_UMEM);
	MLX5_SET(create_umem_in, in, uid, ndev->mvdev.res.uid);
	um = MLX5_ADDR_OF(create_umem_in, in, umem);
	MLX5_SET(umem, um, log_page_size, umem->frag_buf.page_shift - MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(umem, um, num_of_mtt, umem->frag_buf.npages);

	pas = (__be64 *)MLX5_ADDR_OF(umem, um, mtt[0]);
	mlx5_fill_page_frag_array_perm(&umem->frag_buf, pas, MLX5_MTT_PERM_RW);

	err = mlx5_cmd_exec(ndev->mvdev.mdev, in, inlen, out, sizeof(out));
	if (err) {
		mlx5_vdpa_warn(&ndev->mvdev, "create umem(%d)\n", err);
		goto err_cmd;
	}

	kfree(in);
	umem->id = MLX5_GET(create_umem_out, out, umem_id);

	return 0;

err_cmd:
	kfree(in);
err_in:
	umem_frag_buf_free(ndev, umem);
	return err;
}

static void umem_destroy(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq, int num)
{
	u32 in[MLX5_ST_SZ_DW(destroy_umem_in)] = {};
	u32 out[MLX5_ST_SZ_DW(destroy_umem_out)] = {};
	struct mlx5_vdpa_umem *umem;

	switch (num) {
	case 1:
		umem = &mvq->umem1;
		break;
	case 2:
		umem = &mvq->umem2;
		break;
	case 3:
		umem = &mvq->umem3;
		break;
	}

	MLX5_SET(destroy_umem_in, in, opcode, MLX5_CMD_OP_DESTROY_UMEM);
	MLX5_SET(destroy_umem_in, in, umem_id, umem->id);
	if (mlx5_cmd_exec(ndev->mvdev.mdev, in, sizeof(in), out, sizeof(out)))
		return;

	umem_frag_buf_free(ndev, umem);
}

static int umems_create(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq)
{
	int num;
	int err;

	for (num = 1; num <= 3; num++) {
		err = create_umem(ndev, mvq, num);
		if (err)
			goto err_umem;
	}
	return 0;

err_umem:
	for (num--; num > 0; num--)
		umem_destroy(ndev, mvq, num);

	return err;
}

static void umems_destroy(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq)
{
	int num;

	for (num = 3; num > 0; num--)
		umem_destroy(ndev, mvq, num);
}

static int get_queue_type(struct mlx5_vdpa_net *ndev)
{
	u32 type_mask;

	type_mask = MLX5_CAP_DEV_VDPA_EMULATION(ndev->mvdev.mdev, virtio_queue_type);

	/* prefer split queue */
	if (type_mask & MLX5_VIRTIO_EMULATION_CAP_VIRTIO_QUEUE_TYPE_SPLIT)
		return MLX5_VIRTIO_EMULATION_VIRTIO_QUEUE_TYPE_SPLIT;

	WARN_ON(!(type_mask & MLX5_VIRTIO_EMULATION_CAP_VIRTIO_QUEUE_TYPE_PACKED));

	return MLX5_VIRTIO_EMULATION_VIRTIO_QUEUE_TYPE_PACKED;
}

static bool vq_is_tx(u16 idx)
{
	return idx % 2;
}

static u16 get_features_12_3(u64 features)
{
	return (!!(features & BIT_ULL(VIRTIO_NET_F_HOST_TSO4)) << 9) |
	       (!!(features & BIT_ULL(VIRTIO_NET_F_HOST_TSO6)) << 8) |
	       (!!(features & BIT_ULL(VIRTIO_NET_F_CSUM)) << 7) |
	       (!!(features & BIT_ULL(VIRTIO_NET_F_GUEST_CSUM)) << 6);
}

static int create_virtqueue(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq)
{
	int inlen = MLX5_ST_SZ_BYTES(create_virtio_net_q_in);
	u32 out[MLX5_ST_SZ_DW(create_virtio_net_q_out)] = {};
	void *obj_context;
	void *cmd_hdr;
	void *vq_ctx;
	void *in;
	int err;

	err = umems_create(ndev, mvq);
	if (err)
		return err;

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_alloc;
	}

	cmd_hdr = MLX5_ADDR_OF(create_virtio_net_q_in, in, general_obj_in_cmd_hdr);

	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, obj_type, MLX5_OBJ_TYPE_VIRTIO_NET_Q);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, uid, ndev->mvdev.res.uid);

	obj_context = MLX5_ADDR_OF(create_virtio_net_q_in, in, obj_context);
	MLX5_SET(virtio_net_q_object, obj_context, hw_available_index, mvq->avail_idx);
	MLX5_SET(virtio_net_q_object, obj_context, hw_used_index, mvq->used_idx);
	MLX5_SET(virtio_net_q_object, obj_context, queue_feature_bit_mask_12_3,
		 get_features_12_3(ndev->mvdev.actual_features));
	vq_ctx = MLX5_ADDR_OF(virtio_net_q_object, obj_context, virtio_q_context);
	MLX5_SET(virtio_q, vq_ctx, virtio_q_type, get_queue_type(ndev));

	if (vq_is_tx(mvq->index))
		MLX5_SET(virtio_net_q_object, obj_context, tisn_or_qpn, ndev->res.tisn);

	MLX5_SET(virtio_q, vq_ctx, event_mode, MLX5_VIRTIO_Q_EVENT_MODE_QP_MODE);
	MLX5_SET(virtio_q, vq_ctx, queue_index, mvq->index);
	MLX5_SET(virtio_q, vq_ctx, event_qpn_or_msix, mvq->fwqp.mqp.qpn);
	MLX5_SET(virtio_q, vq_ctx, queue_size, mvq->num_ent);
	MLX5_SET(virtio_q, vq_ctx, virtio_version_1_0,
		 !!(ndev->mvdev.actual_features & BIT_ULL(VIRTIO_F_VERSION_1)));
	MLX5_SET64(virtio_q, vq_ctx, desc_addr, mvq->desc_addr);
	MLX5_SET64(virtio_q, vq_ctx, used_addr, mvq->device_addr);
	MLX5_SET64(virtio_q, vq_ctx, available_addr, mvq->driver_addr);
	MLX5_SET(virtio_q, vq_ctx, virtio_q_mkey, ndev->mvdev.mr.mkey.key);
	MLX5_SET(virtio_q, vq_ctx, umem_1_id, mvq->umem1.id);
	MLX5_SET(virtio_q, vq_ctx, umem_1_size, mvq->umem1.size);
	MLX5_SET(virtio_q, vq_ctx, umem_2_id, mvq->umem2.id);
	MLX5_SET(virtio_q, vq_ctx, umem_2_size, mvq->umem2.size);
	MLX5_SET(virtio_q, vq_ctx, umem_3_id, mvq->umem3.id);
	MLX5_SET(virtio_q, vq_ctx, umem_3_size, mvq->umem3.size);
	MLX5_SET(virtio_q, vq_ctx, pd, ndev->mvdev.res.pdn);
	if (MLX5_CAP_DEV_VDPA_EMULATION(ndev->mvdev.mdev, eth_frame_offload_type))
		MLX5_SET(virtio_q, vq_ctx, virtio_version_1_0, 1);

	err = mlx5_cmd_exec(ndev->mvdev.mdev, in, inlen, out, sizeof(out));
	if (err)
		goto err_cmd;

	kfree(in);
	mvq->virtq_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);

	return 0;

err_cmd:
	kfree(in);
err_alloc:
	umems_destroy(ndev, mvq);
	return err;
}

static void destroy_virtqueue(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq)
{
	u32 in[MLX5_ST_SZ_DW(destroy_virtio_net_q_in)] = {};
	u32 out[MLX5_ST_SZ_DW(destroy_virtio_net_q_out)] = {};

	MLX5_SET(destroy_virtio_net_q_in, in, general_obj_out_cmd_hdr.opcode,
		 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(destroy_virtio_net_q_in, in, general_obj_out_cmd_hdr.obj_id, mvq->virtq_id);
	MLX5_SET(destroy_virtio_net_q_in, in, general_obj_out_cmd_hdr.uid, ndev->mvdev.res.uid);
	MLX5_SET(destroy_virtio_net_q_in, in, general_obj_out_cmd_hdr.obj_type,
		 MLX5_OBJ_TYPE_VIRTIO_NET_Q);
	if (mlx5_cmd_exec(ndev->mvdev.mdev, in, sizeof(in), out, sizeof(out))) {
		mlx5_vdpa_warn(&ndev->mvdev, "destroy virtqueue 0x%x\n", mvq->virtq_id);
		return;
	}
	umems_destroy(ndev, mvq);
}

static u32 get_rqpn(struct mlx5_vdpa_virtqueue *mvq, bool fw)
{
	return fw ? mvq->vqqp.mqp.qpn : mvq->fwqp.mqp.qpn;
}

static u32 get_qpn(struct mlx5_vdpa_virtqueue *mvq, bool fw)
{
	return fw ? mvq->fwqp.mqp.qpn : mvq->vqqp.mqp.qpn;
}

static void alloc_inout(struct mlx5_vdpa_net *ndev, int cmd, void **in, int *inlen, void **out,
			int *outlen, u32 qpn, u32 rqpn)
{
	void *qpc;
	void *pp;

	switch (cmd) {
	case MLX5_CMD_OP_2RST_QP:
		*inlen = MLX5_ST_SZ_BYTES(qp_2rst_in);
		*outlen = MLX5_ST_SZ_BYTES(qp_2rst_out);
		*in = kzalloc(*inlen, GFP_KERNEL);
		*out = kzalloc(*outlen, GFP_KERNEL);
		if (!*in || !*out)
			goto outerr;

		MLX5_SET(qp_2rst_in, *in, opcode, cmd);
		MLX5_SET(qp_2rst_in, *in, uid, ndev->mvdev.res.uid);
		MLX5_SET(qp_2rst_in, *in, qpn, qpn);
		break;
	case MLX5_CMD_OP_RST2INIT_QP:
		*inlen = MLX5_ST_SZ_BYTES(rst2init_qp_in);
		*outlen = MLX5_ST_SZ_BYTES(rst2init_qp_out);
		*in = kzalloc(*inlen, GFP_KERNEL);
		*out = kzalloc(MLX5_ST_SZ_BYTES(rst2init_qp_out), GFP_KERNEL);
		if (!*in || !*out)
			goto outerr;

		MLX5_SET(rst2init_qp_in, *in, opcode, cmd);
		MLX5_SET(rst2init_qp_in, *in, uid, ndev->mvdev.res.uid);
		MLX5_SET(rst2init_qp_in, *in, qpn, qpn);
		qpc = MLX5_ADDR_OF(rst2init_qp_in, *in, qpc);
		MLX5_SET(qpc, qpc, remote_qpn, rqpn);
		MLX5_SET(qpc, qpc, rwe, 1);
		pp = MLX5_ADDR_OF(qpc, qpc, primary_address_path);
		MLX5_SET(ads, pp, vhca_port_num, 1);
		break;
	case MLX5_CMD_OP_INIT2RTR_QP:
		*inlen = MLX5_ST_SZ_BYTES(init2rtr_qp_in);
		*outlen = MLX5_ST_SZ_BYTES(init2rtr_qp_out);
		*in = kzalloc(*inlen, GFP_KERNEL);
		*out = kzalloc(MLX5_ST_SZ_BYTES(init2rtr_qp_out), GFP_KERNEL);
		if (!*in || !*out)
			goto outerr;

		MLX5_SET(init2rtr_qp_in, *in, opcode, cmd);
		MLX5_SET(init2rtr_qp_in, *in, uid, ndev->mvdev.res.uid);
		MLX5_SET(init2rtr_qp_in, *in, qpn, qpn);
		qpc = MLX5_ADDR_OF(rst2init_qp_in, *in, qpc);
		MLX5_SET(qpc, qpc, mtu, MLX5_QPC_MTU_256_BYTES);
		MLX5_SET(qpc, qpc, log_msg_max, 30);
		MLX5_SET(qpc, qpc, remote_qpn, rqpn);
		pp = MLX5_ADDR_OF(qpc, qpc, primary_address_path);
		MLX5_SET(ads, pp, fl, 1);
		break;
	case MLX5_CMD_OP_RTR2RTS_QP:
		*inlen = MLX5_ST_SZ_BYTES(rtr2rts_qp_in);
		*outlen = MLX5_ST_SZ_BYTES(rtr2rts_qp_out);
		*in = kzalloc(*inlen, GFP_KERNEL);
		*out = kzalloc(MLX5_ST_SZ_BYTES(rtr2rts_qp_out), GFP_KERNEL);
		if (!*in || !*out)
			goto outerr;

		MLX5_SET(rtr2rts_qp_in, *in, opcode, cmd);
		MLX5_SET(rtr2rts_qp_in, *in, uid, ndev->mvdev.res.uid);
		MLX5_SET(rtr2rts_qp_in, *in, qpn, qpn);
		qpc = MLX5_ADDR_OF(rst2init_qp_in, *in, qpc);
		pp = MLX5_ADDR_OF(qpc, qpc, primary_address_path);
		MLX5_SET(ads, pp, ack_timeout, 14);
		MLX5_SET(qpc, qpc, retry_count, 7);
		MLX5_SET(qpc, qpc, rnr_retry, 7);
		break;
	default:
		goto outerr_nullify;
	}

	return;

outerr:
	kfree(*in);
	kfree(*out);
outerr_nullify:
	*in = NULL;
	*out = NULL;
}

static void free_inout(void *in, void *out)
{
	kfree(in);
	kfree(out);
}

/* Two QPs are used by each virtqueue. One is used by the driver and one by
 * firmware. The fw argument indicates whether the subjected QP is the one used
 * by firmware.
 */
static int modify_qp(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq, bool fw, int cmd)
{
	int outlen;
	int inlen;
	void *out;
	void *in;
	int err;

	alloc_inout(ndev, cmd, &in, &inlen, &out, &outlen, get_qpn(mvq, fw), get_rqpn(mvq, fw));
	if (!in || !out)
		return -ENOMEM;

	err = mlx5_cmd_exec(ndev->mvdev.mdev, in, inlen, out, outlen);
	free_inout(in, out);
	return err;
}

static int connect_qps(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq)
{
	int err;

	err = modify_qp(ndev, mvq, true, MLX5_CMD_OP_2RST_QP);
	if (err)
		return err;

	err = modify_qp(ndev, mvq, false, MLX5_CMD_OP_2RST_QP);
	if (err)
		return err;

	err = modify_qp(ndev, mvq, true, MLX5_CMD_OP_RST2INIT_QP);
	if (err)
		return err;

	err = modify_qp(ndev, mvq, false, MLX5_CMD_OP_RST2INIT_QP);
	if (err)
		return err;

	err = modify_qp(ndev, mvq, true, MLX5_CMD_OP_INIT2RTR_QP);
	if (err)
		return err;

	err = modify_qp(ndev, mvq, false, MLX5_CMD_OP_INIT2RTR_QP);
	if (err)
		return err;

	return modify_qp(ndev, mvq, true, MLX5_CMD_OP_RTR2RTS_QP);
}

struct mlx5_virtq_attr {
	u8 state;
	u16 available_index;
	u16 used_index;
};

static int query_virtqueue(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq,
			   struct mlx5_virtq_attr *attr)
{
	int outlen = MLX5_ST_SZ_BYTES(query_virtio_net_q_out);
	u32 in[MLX5_ST_SZ_DW(query_virtio_net_q_in)] = {};
	void *out;
	void *obj_context;
	void *cmd_hdr;
	int err;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	cmd_hdr = MLX5_ADDR_OF(query_virtio_net_q_in, in, general_obj_in_cmd_hdr);

	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, opcode, MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, obj_type, MLX5_OBJ_TYPE_VIRTIO_NET_Q);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, obj_id, mvq->virtq_id);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, uid, ndev->mvdev.res.uid);
	err = mlx5_cmd_exec(ndev->mvdev.mdev, in, sizeof(in), out, outlen);
	if (err)
		goto err_cmd;

	obj_context = MLX5_ADDR_OF(query_virtio_net_q_out, out, obj_context);
	memset(attr, 0, sizeof(*attr));
	attr->state = MLX5_GET(virtio_net_q_object, obj_context, state);
	attr->available_index = MLX5_GET(virtio_net_q_object, obj_context, hw_available_index);
	attr->used_index = MLX5_GET(virtio_net_q_object, obj_context, hw_used_index);
	kfree(out);
	return 0;

err_cmd:
	kfree(out);
	return err;
}

static int modify_virtqueue(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq, int state)
{
	int inlen = MLX5_ST_SZ_BYTES(modify_virtio_net_q_in);
	u32 out[MLX5_ST_SZ_DW(modify_virtio_net_q_out)] = {};
	void *obj_context;
	void *cmd_hdr;
	void *in;
	int err;

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	cmd_hdr = MLX5_ADDR_OF(modify_virtio_net_q_in, in, general_obj_in_cmd_hdr);

	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, opcode, MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, obj_type, MLX5_OBJ_TYPE_VIRTIO_NET_Q);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, obj_id, mvq->virtq_id);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, uid, ndev->mvdev.res.uid);

	obj_context = MLX5_ADDR_OF(modify_virtio_net_q_in, in, obj_context);
	MLX5_SET64(virtio_net_q_object, obj_context, modify_field_select,
		   MLX5_VIRTQ_MODIFY_MASK_STATE);
	MLX5_SET(virtio_net_q_object, obj_context, state, state);
	err = mlx5_cmd_exec(ndev->mvdev.mdev, in, inlen, out, sizeof(out));
	kfree(in);
	if (!err)
		mvq->fw_state = state;

	return err;
}

static int setup_vq(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq)
{
	u16 idx = mvq->index;
	int err;

	if (!mvq->num_ent)
		return 0;

	if (mvq->initialized)
		return 0;

	err = cq_create(ndev, idx, mvq->num_ent);
	if (err)
		return err;

	err = qp_create(ndev, mvq, &mvq->fwqp);
	if (err)
		goto err_fwqp;

	err = qp_create(ndev, mvq, &mvq->vqqp);
	if (err)
		goto err_vqqp;

	err = connect_qps(ndev, mvq);
	if (err)
		goto err_connect;

	err = create_virtqueue(ndev, mvq);
	if (err)
		goto err_connect;

	if (mvq->ready) {
		err = modify_virtqueue(ndev, mvq, MLX5_VIRTIO_NET_Q_OBJECT_STATE_RDY);
		if (err) {
			mlx5_vdpa_warn(&ndev->mvdev, "failed to modify to ready vq idx %d(%d)\n",
				       idx, err);
			goto err_connect;
		}
	}

	mvq->initialized = true;
	return 0;

err_connect:
	qp_destroy(ndev, &mvq->vqqp);
err_vqqp:
	qp_destroy(ndev, &mvq->fwqp);
err_fwqp:
	cq_destroy(ndev, idx);
	return err;
}

static void suspend_vq(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq)
{
	struct mlx5_virtq_attr attr;

	if (!mvq->initialized)
		return;

	if (mvq->fw_state != MLX5_VIRTIO_NET_Q_OBJECT_STATE_RDY)
		return;

	if (modify_virtqueue(ndev, mvq, MLX5_VIRTIO_NET_Q_OBJECT_STATE_SUSPEND))
		mlx5_vdpa_warn(&ndev->mvdev, "modify to suspend failed\n");

	if (query_virtqueue(ndev, mvq, &attr)) {
		mlx5_vdpa_warn(&ndev->mvdev, "failed to query virtqueue\n");
		return;
	}
	mvq->avail_idx = attr.available_index;
	mvq->used_idx = attr.used_index;
}

static void suspend_vqs(struct mlx5_vdpa_net *ndev)
{
	int i;

	for (i = 0; i < MLX5_MAX_SUPPORTED_VQS; i++)
		suspend_vq(ndev, &ndev->vqs[i]);
}

static void teardown_vq(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq)
{
	if (!mvq->initialized)
		return;

	suspend_vq(ndev, mvq);
	destroy_virtqueue(ndev, mvq);
	qp_destroy(ndev, &mvq->vqqp);
	qp_destroy(ndev, &mvq->fwqp);
	cq_destroy(ndev, mvq->index);
	mvq->initialized = false;
}

static int create_rqt(struct mlx5_vdpa_net *ndev)
{
	__be32 *list;
	int max_rqt;
	void *rqtc;
	int inlen;
	void *in;
	int i, j;
	int err;

	max_rqt = min_t(int, MLX5_MAX_SUPPORTED_VQS / 2,
			1 << MLX5_CAP_GEN(ndev->mvdev.mdev, log_max_rqt_size));
	if (max_rqt < 1)
		return -EOPNOTSUPP;

	inlen = MLX5_ST_SZ_BYTES(create_rqt_in) + max_rqt * MLX5_ST_SZ_BYTES(rq_num);
	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_rqt_in, in, uid, ndev->mvdev.res.uid);
	rqtc = MLX5_ADDR_OF(create_rqt_in, in, rqt_context);

	MLX5_SET(rqtc, rqtc, list_q_type, MLX5_RQTC_LIST_Q_TYPE_VIRTIO_NET_Q);
	MLX5_SET(rqtc, rqtc, rqt_max_size, max_rqt);
	list = MLX5_ADDR_OF(rqtc, rqtc, rq_num[0]);
	for (i = 0, j = 0; j < max_rqt; j++) {
		if (!ndev->vqs[j].initialized)
			continue;

		if (!vq_is_tx(ndev->vqs[j].index)) {
			list[i] = cpu_to_be32(ndev->vqs[j].virtq_id);
			i++;
		}
	}
	MLX5_SET(rqtc, rqtc, rqt_actual_size, i);

	err = mlx5_vdpa_create_rqt(&ndev->mvdev, in, inlen, &ndev->res.rqtn);
	kfree(in);
	if (err)
		return err;

	return 0;
}

#define MLX5_MODIFY_RQT_NUM_RQS ((u64)1)

static int modify_rqt(struct mlx5_vdpa_net *ndev, int num)
{
	__be32 *list;
	int max_rqt;
	void *rqtc;
	int inlen;
	void *in;
	int i, j;
	int err;

	max_rqt = min_t(int, ndev->cur_num_vqs / 2,
			1 << MLX5_CAP_GEN(ndev->mvdev.mdev, log_max_rqt_size));
	if (max_rqt < 1)
		return -EOPNOTSUPP;

	inlen = MLX5_ST_SZ_BYTES(modify_rqt_in) + max_rqt * MLX5_ST_SZ_BYTES(rq_num);
	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_rqt_in, in, uid, ndev->mvdev.res.uid);
	MLX5_SET64(modify_rqt_in, in, bitmask, MLX5_MODIFY_RQT_NUM_RQS);
	rqtc = MLX5_ADDR_OF(modify_rqt_in, in, ctx);
	MLX5_SET(rqtc, rqtc, list_q_type, MLX5_RQTC_LIST_Q_TYPE_VIRTIO_NET_Q);

	list = MLX5_ADDR_OF(rqtc, rqtc, rq_num[0]);
	for (i = 0, j = 0; j < num; j++) {
		if (!ndev->vqs[j].initialized)
			continue;

		if (!vq_is_tx(ndev->vqs[j].index)) {
			list[i] = cpu_to_be32(ndev->vqs[j].virtq_id);
			i++;
		}
	}
	MLX5_SET(rqtc, rqtc, rqt_actual_size, i);
	err = mlx5_vdpa_modify_rqt(&ndev->mvdev, in, inlen, ndev->res.rqtn);
	kfree(in);
	if (err)
		return err;

	return 0;
}

static void destroy_rqt(struct mlx5_vdpa_net *ndev)
{
	mlx5_vdpa_destroy_rqt(&ndev->mvdev, ndev->res.rqtn);
}

static int create_tir(struct mlx5_vdpa_net *ndev)
{
#define HASH_IP_L4PORTS                                                                            \
	(MLX5_HASH_FIELD_SEL_SRC_IP | MLX5_HASH_FIELD_SEL_DST_IP | MLX5_HASH_FIELD_SEL_L4_SPORT |  \
	 MLX5_HASH_FIELD_SEL_L4_DPORT)
	static const u8 rx_hash_toeplitz_key[] = { 0x2c, 0xc6, 0x81, 0xd1, 0x5b, 0xdb, 0xf4, 0xf7,
						   0xfc, 0xa2, 0x83, 0x19, 0xdb, 0x1a, 0x3e, 0x94,
						   0x6b, 0x9e, 0x38, 0xd9, 0x2c, 0x9c, 0x03, 0xd1,
						   0xad, 0x99, 0x44, 0xa7, 0xd9, 0x56, 0x3d, 0x59,
						   0x06, 0x3c, 0x25, 0xf3, 0xfc, 0x1f, 0xdc, 0x2a };
	void *rss_key;
	void *outer;
	void *tirc;
	void *in;
	int err;

	in = kzalloc(MLX5_ST_SZ_BYTES(create_tir_in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_tir_in, in, uid, ndev->mvdev.res.uid);
	tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);
	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_INDIRECT);

	MLX5_SET(tirc, tirc, rx_hash_symmetric, 1);
	MLX5_SET(tirc, tirc, rx_hash_fn, MLX5_RX_HASH_FN_TOEPLITZ);
	rss_key = MLX5_ADDR_OF(tirc, tirc, rx_hash_toeplitz_key);
	memcpy(rss_key, rx_hash_toeplitz_key, sizeof(rx_hash_toeplitz_key));

	outer = MLX5_ADDR_OF(tirc, tirc, rx_hash_field_selector_outer);
	MLX5_SET(rx_hash_field_select, outer, l3_prot_type, MLX5_L3_PROT_TYPE_IPV4);
	MLX5_SET(rx_hash_field_select, outer, l4_prot_type, MLX5_L4_PROT_TYPE_TCP);
	MLX5_SET(rx_hash_field_select, outer, selected_fields, HASH_IP_L4PORTS);

	MLX5_SET(tirc, tirc, indirect_table, ndev->res.rqtn);
	MLX5_SET(tirc, tirc, transport_domain, ndev->res.tdn);

	err = mlx5_vdpa_create_tir(&ndev->mvdev, in, &ndev->res.tirn);
	kfree(in);
	return err;
}

static void destroy_tir(struct mlx5_vdpa_net *ndev)
{
	mlx5_vdpa_destroy_tir(&ndev->mvdev, ndev->res.tirn);
}

static int add_fwd_to_tir(struct mlx5_vdpa_net *ndev)
{
	struct mlx5_flow_destination dest[2] = {};
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_namespace *ns;
	int err;

	/* for now, one entry, match all, forward to tir */
	ft_attr.max_fte = 1;
	ft_attr.autogroup.max_num_groups = 1;

	ns = mlx5_get_flow_namespace(ndev->mvdev.mdev, MLX5_FLOW_NAMESPACE_BYPASS);
	if (!ns) {
		mlx5_vdpa_warn(&ndev->mvdev, "get flow namespace\n");
		return -EOPNOTSUPP;
	}

	ndev->rxft = mlx5_create_auto_grouped_flow_table(ns, &ft_attr);
	if (IS_ERR(ndev->rxft))
		return PTR_ERR(ndev->rxft);

	ndev->rx_counter = mlx5_fc_create(ndev->mvdev.mdev, false);
	if (IS_ERR(ndev->rx_counter)) {
		err = PTR_ERR(ndev->rx_counter);
		goto err_fc;
	}

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST | MLX5_FLOW_CONTEXT_ACTION_COUNT;
	dest[0].type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	dest[0].tir_num = ndev->res.tirn;
	dest[1].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest[1].counter_id = mlx5_fc_id(ndev->rx_counter);
	ndev->rx_rule = mlx5_add_flow_rules(ndev->rxft, NULL, &flow_act, dest, 2);
	if (IS_ERR(ndev->rx_rule)) {
		err = PTR_ERR(ndev->rx_rule);
		ndev->rx_rule = NULL;
		goto err_rule;
	}

	return 0;

err_rule:
	mlx5_fc_destroy(ndev->mvdev.mdev, ndev->rx_counter);
err_fc:
	mlx5_destroy_flow_table(ndev->rxft);
	return err;
}

static void remove_fwd_to_tir(struct mlx5_vdpa_net *ndev)
{
	if (!ndev->rx_rule)
		return;

	mlx5_del_flow_rules(ndev->rx_rule);
	mlx5_fc_destroy(ndev->mvdev.mdev, ndev->rx_counter);
	mlx5_destroy_flow_table(ndev->rxft);

	ndev->rx_rule = NULL;
}

static virtio_net_ctrl_ack handle_ctrl_mac(struct mlx5_vdpa_dev *mvdev, u8 cmd)
{
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct mlx5_control_vq *cvq = &mvdev->cvq;
	virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
	struct mlx5_core_dev *pfmdev;
	size_t read;
	u8 mac[ETH_ALEN];

	pfmdev = pci_get_drvdata(pci_physfn(mvdev->mdev->pdev));
	switch (cmd) {
	case VIRTIO_NET_CTRL_MAC_ADDR_SET:
		read = vringh_iov_pull_iotlb(&cvq->vring, &cvq->riov, (void *)mac, ETH_ALEN);
		if (read != ETH_ALEN)
			break;

		if (!memcmp(ndev->config.mac, mac, 6)) {
			status = VIRTIO_NET_OK;
			break;
		}

		if (!is_zero_ether_addr(ndev->config.mac)) {
			if (mlx5_mpfs_del_mac(pfmdev, ndev->config.mac)) {
				mlx5_vdpa_warn(mvdev, "failed to delete old MAC %pM from MPFS table\n",
					       ndev->config.mac);
				break;
			}
		}

		if (mlx5_mpfs_add_mac(pfmdev, mac)) {
			mlx5_vdpa_warn(mvdev, "failed to insert new MAC %pM into MPFS table\n",
				       mac);
			break;
		}

		memcpy(ndev->config.mac, mac, ETH_ALEN);
		status = VIRTIO_NET_OK;
		break;

	default:
		break;
	}

	return status;
}

static int change_num_qps(struct mlx5_vdpa_dev *mvdev, int newqps)
{
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	int cur_qps = ndev->cur_num_vqs / 2;
	int err;
	int i;

	if (cur_qps > newqps) {
		err = modify_rqt(ndev, 2 * newqps);
		if (err)
			return err;

		for (i = ndev->cur_num_vqs - 1; i >= 2 * newqps; i--)
			teardown_vq(ndev, &ndev->vqs[i]);

		ndev->cur_num_vqs = 2 * newqps;
	} else {
		ndev->cur_num_vqs = 2 * newqps;
		for (i = cur_qps * 2; i < 2 * newqps; i++) {
			err = setup_vq(ndev, &ndev->vqs[i]);
			if (err)
				goto clean_added;
		}
		err = modify_rqt(ndev, 2 * newqps);
		if (err)
			goto clean_added;
	}
	return 0;

clean_added:
	for (--i; i >= cur_qps; --i)
		teardown_vq(ndev, &ndev->vqs[i]);

	return err;
}

static virtio_net_ctrl_ack handle_ctrl_mq(struct mlx5_vdpa_dev *mvdev, u8 cmd)
{
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
	struct mlx5_control_vq *cvq = &mvdev->cvq;
	struct virtio_net_ctrl_mq mq;
	size_t read;
	u16 newqps;

	switch (cmd) {
	case VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET:
		read = vringh_iov_pull_iotlb(&cvq->vring, &cvq->riov, (void *)&mq, sizeof(mq));
		if (read != sizeof(mq))
			break;

		newqps = mlx5vdpa16_to_cpu(mvdev, mq.virtqueue_pairs);
		if (ndev->cur_num_vqs == 2 * newqps) {
			status = VIRTIO_NET_OK;
			break;
		}

		if (newqps & (newqps - 1))
			break;

		if (!change_num_qps(mvdev, newqps))
			status = VIRTIO_NET_OK;

		break;
	default:
		break;
	}

	return status;
}

static void mlx5_cvq_kick_handler(struct work_struct *work)
{
	virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
	struct virtio_net_ctrl_hdr ctrl;
	struct mlx5_ctrl_wq_ent *wqent;
	struct mlx5_vdpa_dev *mvdev;
	struct mlx5_control_vq *cvq;
	struct mlx5_vdpa_net *ndev;
	size_t read, write;
	int err;

	wqent = container_of(work, struct mlx5_ctrl_wq_ent, work);
	mvdev = wqent->mvdev;
	ndev = to_mlx5_vdpa_ndev(mvdev);
	cvq = &mvdev->cvq;
	if (!(ndev->mvdev.actual_features & BIT_ULL(VIRTIO_NET_F_CTRL_VQ)))
		goto out;

	if (!cvq->ready)
		goto out;

	while (true) {
		err = vringh_getdesc_iotlb(&cvq->vring, &cvq->riov, &cvq->wiov, &cvq->head,
					   GFP_ATOMIC);
		if (err <= 0)
			break;

		read = vringh_iov_pull_iotlb(&cvq->vring, &cvq->riov, &ctrl, sizeof(ctrl));
		if (read != sizeof(ctrl))
			break;

		switch (ctrl.class) {
		case VIRTIO_NET_CTRL_MAC:
			status = handle_ctrl_mac(mvdev, ctrl.cmd);
			break;
		case VIRTIO_NET_CTRL_MQ:
			status = handle_ctrl_mq(mvdev, ctrl.cmd);
			break;

		default:
			break;
		}

		/* Make sure data is written before advancing index */
		smp_wmb();

		write = vringh_iov_push_iotlb(&cvq->vring, &cvq->wiov, &status, sizeof(status));
		vringh_complete_iotlb(&cvq->vring, cvq->head, write);
		vringh_kiov_cleanup(&cvq->riov);
		vringh_kiov_cleanup(&cvq->wiov);

		if (vringh_need_notify_iotlb(&cvq->vring))
			vringh_notify(&cvq->vring);
	}
out:
	kfree(wqent);
}

static void mlx5_vdpa_kick_vq(struct vdpa_device *vdev, u16 idx)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct mlx5_vdpa_virtqueue *mvq;
	struct mlx5_ctrl_wq_ent *wqent;

	if (!is_index_valid(mvdev, idx))
		return;

	if (unlikely(is_ctrl_vq_idx(mvdev, idx))) {
		if (!mvdev->cvq.ready)
			return;

		wqent = kzalloc(sizeof(*wqent), GFP_ATOMIC);
		if (!wqent)
			return;

		wqent->mvdev = mvdev;
		INIT_WORK(&wqent->work, mlx5_cvq_kick_handler);
		queue_work(mvdev->wq, &wqent->work);
		return;
	}

	mvq = &ndev->vqs[idx];
	if (unlikely(!mvq->ready))
		return;

	iowrite16(idx, ndev->mvdev.res.kick_addr);
}

static int mlx5_vdpa_set_vq_address(struct vdpa_device *vdev, u16 idx, u64 desc_area,
				    u64 driver_area, u64 device_area)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct mlx5_vdpa_virtqueue *mvq;

	if (!is_index_valid(mvdev, idx))
		return -EINVAL;

	if (is_ctrl_vq_idx(mvdev, idx)) {
		mvdev->cvq.desc_addr = desc_area;
		mvdev->cvq.device_addr = device_area;
		mvdev->cvq.driver_addr = driver_area;
		return 0;
	}

	mvq = &ndev->vqs[idx];
	mvq->desc_addr = desc_area;
	mvq->device_addr = device_area;
	mvq->driver_addr = driver_area;
	return 0;
}

static void mlx5_vdpa_set_vq_num(struct vdpa_device *vdev, u16 idx, u32 num)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct mlx5_vdpa_virtqueue *mvq;

	if (!is_index_valid(mvdev, idx) || is_ctrl_vq_idx(mvdev, idx))
		return;

	mvq = &ndev->vqs[idx];
	mvq->num_ent = num;
}

static void mlx5_vdpa_set_vq_cb(struct vdpa_device *vdev, u16 idx, struct vdpa_callback *cb)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);

	ndev->event_cbs[idx] = *cb;
}

static void mlx5_cvq_notify(struct vringh *vring)
{
	struct mlx5_control_vq *cvq = container_of(vring, struct mlx5_control_vq, vring);

	if (!cvq->event_cb.callback)
		return;

	cvq->event_cb.callback(cvq->event_cb.private);
}

static void set_cvq_ready(struct mlx5_vdpa_dev *mvdev, bool ready)
{
	struct mlx5_control_vq *cvq = &mvdev->cvq;

	cvq->ready = ready;
	if (!ready)
		return;

	cvq->vring.notify = mlx5_cvq_notify;
}

static void mlx5_vdpa_set_vq_ready(struct vdpa_device *vdev, u16 idx, bool ready)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct mlx5_vdpa_virtqueue *mvq;

	if (!is_index_valid(mvdev, idx))
		return;

	if (is_ctrl_vq_idx(mvdev, idx)) {
		set_cvq_ready(mvdev, ready);
		return;
	}

	mvq = &ndev->vqs[idx];
	if (!ready)
		suspend_vq(ndev, mvq);

	mvq->ready = ready;
}

static bool mlx5_vdpa_get_vq_ready(struct vdpa_device *vdev, u16 idx)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);

	if (!is_index_valid(mvdev, idx))
		return false;

	if (is_ctrl_vq_idx(mvdev, idx))
		return mvdev->cvq.ready;

	return ndev->vqs[idx].ready;
}

static int mlx5_vdpa_set_vq_state(struct vdpa_device *vdev, u16 idx,
				  const struct vdpa_vq_state *state)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct mlx5_vdpa_virtqueue *mvq;

	if (!is_index_valid(mvdev, idx))
		return -EINVAL;

	if (is_ctrl_vq_idx(mvdev, idx)) {
		mvdev->cvq.vring.last_avail_idx = state->split.avail_index;
		return 0;
	}

	mvq = &ndev->vqs[idx];
	if (mvq->fw_state == MLX5_VIRTIO_NET_Q_OBJECT_STATE_RDY) {
		mlx5_vdpa_warn(mvdev, "can't modify available index\n");
		return -EINVAL;
	}

	mvq->used_idx = state->split.avail_index;
	mvq->avail_idx = state->split.avail_index;
	return 0;
}

static int mlx5_vdpa_get_vq_state(struct vdpa_device *vdev, u16 idx, struct vdpa_vq_state *state)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct mlx5_vdpa_virtqueue *mvq;
	struct mlx5_virtq_attr attr;
	int err;

	if (!is_index_valid(mvdev, idx))
		return -EINVAL;

	if (is_ctrl_vq_idx(mvdev, idx)) {
		state->split.avail_index = mvdev->cvq.vring.last_avail_idx;
		return 0;
	}

	mvq = &ndev->vqs[idx];
	/* If the virtq object was destroyed, use the value saved at
	 * the last minute of suspend_vq. This caters for userspace
	 * that cares about emulating the index after vq is stopped.
	 */
	if (!mvq->initialized) {
		/* Firmware returns a wrong value for the available index.
		 * Since both values should be identical, we take the value of
		 * used_idx which is reported correctly.
		 */
		state->split.avail_index = mvq->used_idx;
		return 0;
	}

	err = query_virtqueue(ndev, mvq, &attr);
	if (err) {
		mlx5_vdpa_warn(mvdev, "failed to query virtqueue\n");
		return err;
	}
	state->split.avail_index = attr.used_index;
	return 0;
}

static u32 mlx5_vdpa_get_vq_align(struct vdpa_device *vdev)
{
	return PAGE_SIZE;
}

enum { MLX5_VIRTIO_NET_F_GUEST_CSUM = 1 << 9,
	MLX5_VIRTIO_NET_F_CSUM = 1 << 10,
	MLX5_VIRTIO_NET_F_HOST_TSO6 = 1 << 11,
	MLX5_VIRTIO_NET_F_HOST_TSO4 = 1 << 12,
};

static u64 mlx_to_vritio_features(u16 dev_features)
{
	u64 result = 0;

	if (dev_features & MLX5_VIRTIO_NET_F_GUEST_CSUM)
		result |= BIT_ULL(VIRTIO_NET_F_GUEST_CSUM);
	if (dev_features & MLX5_VIRTIO_NET_F_CSUM)
		result |= BIT_ULL(VIRTIO_NET_F_CSUM);
	if (dev_features & MLX5_VIRTIO_NET_F_HOST_TSO6)
		result |= BIT_ULL(VIRTIO_NET_F_HOST_TSO6);
	if (dev_features & MLX5_VIRTIO_NET_F_HOST_TSO4)
		result |= BIT_ULL(VIRTIO_NET_F_HOST_TSO4);

	return result;
}

static u64 mlx5_vdpa_get_features(struct vdpa_device *vdev)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	u16 dev_features;

	dev_features = MLX5_CAP_DEV_VDPA_EMULATION(mvdev->mdev, device_features_bits_mask);
	ndev->mvdev.mlx_features |= mlx_to_vritio_features(dev_features);
	if (MLX5_CAP_DEV_VDPA_EMULATION(mvdev->mdev, virtio_version_1_0))
		ndev->mvdev.mlx_features |= BIT_ULL(VIRTIO_F_VERSION_1);
	ndev->mvdev.mlx_features |= BIT_ULL(VIRTIO_F_ACCESS_PLATFORM);
	ndev->mvdev.mlx_features |= BIT_ULL(VIRTIO_NET_F_CTRL_VQ);
	ndev->mvdev.mlx_features |= BIT_ULL(VIRTIO_NET_F_CTRL_MAC_ADDR);
	ndev->mvdev.mlx_features |= BIT_ULL(VIRTIO_NET_F_MQ);

	print_features(mvdev, ndev->mvdev.mlx_features, false);
	return ndev->mvdev.mlx_features;
}

static int verify_min_features(struct mlx5_vdpa_dev *mvdev, u64 features)
{
	if (!(features & BIT_ULL(VIRTIO_F_ACCESS_PLATFORM)))
		return -EOPNOTSUPP;

	return 0;
}

static int setup_virtqueues(struct mlx5_vdpa_dev *mvdev)
{
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct mlx5_control_vq *cvq = &mvdev->cvq;
	int err;
	int i;

	for (i = 0; i < 2 * mlx5_vdpa_max_qps(mvdev->max_vqs); i++) {
		err = setup_vq(ndev, &ndev->vqs[i]);
		if (err)
			goto err_vq;
	}

	if (mvdev->actual_features & BIT_ULL(VIRTIO_NET_F_CTRL_VQ)) {
		err = vringh_init_iotlb(&cvq->vring, mvdev->actual_features,
					MLX5_CVQ_MAX_ENT, false,
					(struct vring_desc *)(uintptr_t)cvq->desc_addr,
					(struct vring_avail *)(uintptr_t)cvq->driver_addr,
					(struct vring_used *)(uintptr_t)cvq->device_addr);
		if (err)
			goto err_vq;
	}

	return 0;

err_vq:
	for (--i; i >= 0; i--)
		teardown_vq(ndev, &ndev->vqs[i]);

	return err;
}

static void teardown_virtqueues(struct mlx5_vdpa_net *ndev)
{
	struct mlx5_vdpa_virtqueue *mvq;
	int i;

	for (i = ndev->mvdev.max_vqs - 1; i >= 0; i--) {
		mvq = &ndev->vqs[i];
		if (!mvq->initialized)
			continue;

		teardown_vq(ndev, mvq);
	}
}

static void update_cvq_info(struct mlx5_vdpa_dev *mvdev)
{
	if (MLX5_FEATURE(mvdev, VIRTIO_NET_F_CTRL_VQ)) {
		if (MLX5_FEATURE(mvdev, VIRTIO_NET_F_MQ)) {
			/* MQ supported. CVQ index is right above the last data virtqueue's */
			mvdev->max_idx = mvdev->max_vqs;
		} else {
			/* Only CVQ supportted. data virtqueues occupy indices 0 and 1.
			 * CVQ gets index 2
			 */
			mvdev->max_idx = 2;
		}
	} else {
		/* Two data virtqueues only: one for rx and one for tx */
		mvdev->max_idx = 1;
	}
}

static int mlx5_vdpa_set_features(struct vdpa_device *vdev, u64 features)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	int err;

	print_features(mvdev, features, true);

	err = verify_min_features(mvdev, features);
	if (err)
		return err;

	ndev->mvdev.actual_features = features & ndev->mvdev.mlx_features;
	ndev->config.mtu = cpu_to_mlx5vdpa16(mvdev, ndev->mtu);
	ndev->config.status |= cpu_to_mlx5vdpa16(mvdev, VIRTIO_NET_S_LINK_UP);
	update_cvq_info(mvdev);
	return err;
}

static void mlx5_vdpa_set_config_cb(struct vdpa_device *vdev, struct vdpa_callback *cb)
{
	/* not implemented */
	mlx5_vdpa_warn(to_mvdev(vdev), "set config callback not supported\n");
}

#define MLX5_VDPA_MAX_VQ_ENTRIES 256
static u16 mlx5_vdpa_get_vq_num_max(struct vdpa_device *vdev)
{
	return MLX5_VDPA_MAX_VQ_ENTRIES;
}

static u32 mlx5_vdpa_get_device_id(struct vdpa_device *vdev)
{
	return VIRTIO_ID_NET;
}

static u32 mlx5_vdpa_get_vendor_id(struct vdpa_device *vdev)
{
	return PCI_VENDOR_ID_MELLANOX;
}

static u8 mlx5_vdpa_get_status(struct vdpa_device *vdev)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);

	print_status(mvdev, ndev->mvdev.status, false);
	return ndev->mvdev.status;
}

static int save_channel_info(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq)
{
	struct mlx5_vq_restore_info *ri = &mvq->ri;
	struct mlx5_virtq_attr attr = {};
	int err;

	if (mvq->initialized) {
		err = query_virtqueue(ndev, mvq, &attr);
		if (err)
			return err;
	}

	ri->avail_index = attr.available_index;
	ri->used_index = attr.used_index;
	ri->ready = mvq->ready;
	ri->num_ent = mvq->num_ent;
	ri->desc_addr = mvq->desc_addr;
	ri->device_addr = mvq->device_addr;
	ri->driver_addr = mvq->driver_addr;
	ri->restore = true;
	return 0;
}

static int save_channels_info(struct mlx5_vdpa_net *ndev)
{
	int i;

	for (i = 0; i < ndev->mvdev.max_vqs; i++) {
		memset(&ndev->vqs[i].ri, 0, sizeof(ndev->vqs[i].ri));
		save_channel_info(ndev, &ndev->vqs[i]);
	}
	return 0;
}

static void mlx5_clear_vqs(struct mlx5_vdpa_net *ndev)
{
	int i;

	for (i = 0; i < ndev->mvdev.max_vqs; i++)
		memset(&ndev->vqs[i], 0, offsetof(struct mlx5_vdpa_virtqueue, ri));
}

static void restore_channels_info(struct mlx5_vdpa_net *ndev)
{
	struct mlx5_vdpa_virtqueue *mvq;
	struct mlx5_vq_restore_info *ri;
	int i;

	mlx5_clear_vqs(ndev);
	init_mvqs(ndev);
	for (i = 0; i < ndev->mvdev.max_vqs; i++) {
		mvq = &ndev->vqs[i];
		ri = &mvq->ri;
		if (!ri->restore)
			continue;

		mvq->avail_idx = ri->avail_index;
		mvq->used_idx = ri->used_index;
		mvq->ready = ri->ready;
		mvq->num_ent = ri->num_ent;
		mvq->desc_addr = ri->desc_addr;
		mvq->device_addr = ri->device_addr;
		mvq->driver_addr = ri->driver_addr;
	}
}

static int mlx5_vdpa_change_map(struct mlx5_vdpa_dev *mvdev, struct vhost_iotlb *iotlb)
{
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	int err;

	suspend_vqs(ndev);
	err = save_channels_info(ndev);
	if (err)
		goto err_mr;

	teardown_driver(ndev);
	mlx5_vdpa_destroy_mr(mvdev);
	err = mlx5_vdpa_create_mr(mvdev, iotlb);
	if (err)
		goto err_mr;

	if (!(mvdev->status & VIRTIO_CONFIG_S_DRIVER_OK))
		return 0;

	restore_channels_info(ndev);
	err = setup_driver(mvdev);
	if (err)
		goto err_setup;

	return 0;

err_setup:
	mlx5_vdpa_destroy_mr(mvdev);
err_mr:
	return err;
}

static int setup_driver(struct mlx5_vdpa_dev *mvdev)
{
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	int err;

	mutex_lock(&ndev->reslock);
	if (ndev->setup) {
		mlx5_vdpa_warn(mvdev, "setup driver called for already setup driver\n");
		err = 0;
		goto out;
	}
	err = setup_virtqueues(mvdev);
	if (err) {
		mlx5_vdpa_warn(mvdev, "setup_virtqueues\n");
		goto out;
	}

	err = create_rqt(ndev);
	if (err) {
		mlx5_vdpa_warn(mvdev, "create_rqt\n");
		goto err_rqt;
	}

	err = create_tir(ndev);
	if (err) {
		mlx5_vdpa_warn(mvdev, "create_tir\n");
		goto err_tir;
	}

	err = add_fwd_to_tir(ndev);
	if (err) {
		mlx5_vdpa_warn(mvdev, "add_fwd_to_tir\n");
		goto err_fwd;
	}
	ndev->setup = true;
	mutex_unlock(&ndev->reslock);

	return 0;

err_fwd:
	destroy_tir(ndev);
err_tir:
	destroy_rqt(ndev);
err_rqt:
	teardown_virtqueues(ndev);
out:
	mutex_unlock(&ndev->reslock);
	return err;
}

static void teardown_driver(struct mlx5_vdpa_net *ndev)
{
	mutex_lock(&ndev->reslock);
	if (!ndev->setup)
		goto out;

	remove_fwd_to_tir(ndev);
	destroy_tir(ndev);
	destroy_rqt(ndev);
	teardown_virtqueues(ndev);
	ndev->setup = false;
out:
	mutex_unlock(&ndev->reslock);
}

static void clear_vqs_ready(struct mlx5_vdpa_net *ndev)
{
	int i;

	for (i = 0; i < ndev->mvdev.max_vqs; i++)
		ndev->vqs[i].ready = false;
}

static void mlx5_vdpa_set_status(struct vdpa_device *vdev, u8 status)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	int err;

	print_status(mvdev, status, true);

	if ((status ^ ndev->mvdev.status) & VIRTIO_CONFIG_S_DRIVER_OK) {
		if (status & VIRTIO_CONFIG_S_DRIVER_OK) {
			err = setup_driver(mvdev);
			if (err) {
				mlx5_vdpa_warn(mvdev, "failed to setup driver\n");
				goto err_setup;
			}
		} else {
			mlx5_vdpa_warn(mvdev, "did not expect DRIVER_OK to be cleared\n");
			return;
		}
	}

	ndev->mvdev.status = status;
	return;

err_setup:
	mlx5_vdpa_destroy_mr(&ndev->mvdev);
	ndev->mvdev.status |= VIRTIO_CONFIG_S_FAILED;
}

static int mlx5_vdpa_reset(struct vdpa_device *vdev)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);

	print_status(mvdev, 0, true);
	mlx5_vdpa_info(mvdev, "performing device reset\n");
	teardown_driver(ndev);
	clear_vqs_ready(ndev);
	mlx5_vdpa_destroy_mr(&ndev->mvdev);
	ndev->mvdev.status = 0;
	ndev->mvdev.mlx_features = 0;
	memset(ndev->event_cbs, 0, sizeof(ndev->event_cbs));
	ndev->mvdev.actual_features = 0;
	++mvdev->generation;
	if (MLX5_CAP_GEN(mvdev->mdev, umem_uid_0)) {
		if (mlx5_vdpa_create_mr(mvdev, NULL))
			mlx5_vdpa_warn(mvdev, "create MR failed\n");
	}

	return 0;
}

static size_t mlx5_vdpa_get_config_size(struct vdpa_device *vdev)
{
	return sizeof(struct virtio_net_config);
}

static void mlx5_vdpa_get_config(struct vdpa_device *vdev, unsigned int offset, void *buf,
				 unsigned int len)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);

	if (offset + len <= sizeof(struct virtio_net_config))
		memcpy(buf, (u8 *)&ndev->config + offset, len);
}

static void mlx5_vdpa_set_config(struct vdpa_device *vdev, unsigned int offset, const void *buf,
				 unsigned int len)
{
	/* not supported */
}

static u32 mlx5_vdpa_get_generation(struct vdpa_device *vdev)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);

	return mvdev->generation;
}

static int mlx5_vdpa_set_map(struct vdpa_device *vdev, struct vhost_iotlb *iotlb)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	bool change_map;
	int err;

	err = mlx5_vdpa_handle_set_map(mvdev, iotlb, &change_map);
	if (err) {
		mlx5_vdpa_warn(mvdev, "set map failed(%d)\n", err);
		return err;
	}

	if (change_map)
		return mlx5_vdpa_change_map(mvdev, iotlb);

	return 0;
}

static void mlx5_vdpa_free(struct vdpa_device *vdev)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_core_dev *pfmdev;
	struct mlx5_vdpa_net *ndev;

	ndev = to_mlx5_vdpa_ndev(mvdev);

	free_resources(ndev);
	mlx5_vdpa_destroy_mr(mvdev);
	if (!is_zero_ether_addr(ndev->config.mac)) {
		pfmdev = pci_get_drvdata(pci_physfn(mvdev->mdev->pdev));
		mlx5_mpfs_del_mac(pfmdev, ndev->config.mac);
	}
	mlx5_vdpa_free_resources(&ndev->mvdev);
	mutex_destroy(&ndev->reslock);
}

static struct vdpa_notification_area mlx5_get_vq_notification(struct vdpa_device *vdev, u16 idx)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct vdpa_notification_area ret = {};
	struct mlx5_vdpa_net *ndev;
	phys_addr_t addr;

	if (!is_index_valid(mvdev, idx) || is_ctrl_vq_idx(mvdev, idx))
		return ret;

	/* If SF BAR size is smaller than PAGE_SIZE, do not use direct
	 * notification to avoid the risk of mapping pages that contain BAR of more
	 * than one SF
	 */
	if (MLX5_CAP_GEN(mvdev->mdev, log_min_sf_size) + 12 < PAGE_SHIFT)
		return ret;

	ndev = to_mlx5_vdpa_ndev(mvdev);
	addr = (phys_addr_t)ndev->mvdev.res.phys_kick_addr;
	ret.addr = addr;
	ret.size = PAGE_SIZE;
	return ret;
}

static int mlx5_get_vq_irq(struct vdpa_device *vdv, u16 idx)
{
	return -EOPNOTSUPP;
}

static const struct vdpa_config_ops mlx5_vdpa_ops = {
	.set_vq_address = mlx5_vdpa_set_vq_address,
	.set_vq_num = mlx5_vdpa_set_vq_num,
	.kick_vq = mlx5_vdpa_kick_vq,
	.set_vq_cb = mlx5_vdpa_set_vq_cb,
	.set_vq_ready = mlx5_vdpa_set_vq_ready,
	.get_vq_ready = mlx5_vdpa_get_vq_ready,
	.set_vq_state = mlx5_vdpa_set_vq_state,
	.get_vq_state = mlx5_vdpa_get_vq_state,
	.get_vq_notification = mlx5_get_vq_notification,
	.get_vq_irq = mlx5_get_vq_irq,
	.get_vq_align = mlx5_vdpa_get_vq_align,
	.get_features = mlx5_vdpa_get_features,
	.set_features = mlx5_vdpa_set_features,
	.set_config_cb = mlx5_vdpa_set_config_cb,
	.get_vq_num_max = mlx5_vdpa_get_vq_num_max,
	.get_device_id = mlx5_vdpa_get_device_id,
	.get_vendor_id = mlx5_vdpa_get_vendor_id,
	.get_status = mlx5_vdpa_get_status,
	.set_status = mlx5_vdpa_set_status,
	.reset = mlx5_vdpa_reset,
	.get_config_size = mlx5_vdpa_get_config_size,
	.get_config = mlx5_vdpa_get_config,
	.set_config = mlx5_vdpa_set_config,
	.get_generation = mlx5_vdpa_get_generation,
	.set_map = mlx5_vdpa_set_map,
	.free = mlx5_vdpa_free,
};

static int query_mtu(struct mlx5_core_dev *mdev, u16 *mtu)
{
	u16 hw_mtu;
	int err;

	err = mlx5_query_nic_vport_mtu(mdev, &hw_mtu);
	if (err)
		return err;

	*mtu = hw_mtu - MLX5V_ETH_HARD_MTU;
	return 0;
}

static int alloc_resources(struct mlx5_vdpa_net *ndev)
{
	struct mlx5_vdpa_net_resources *res = &ndev->res;
	int err;

	if (res->valid) {
		mlx5_vdpa_warn(&ndev->mvdev, "resources already allocated\n");
		return -EEXIST;
	}

	err = mlx5_vdpa_alloc_transport_domain(&ndev->mvdev, &res->tdn);
	if (err)
		return err;

	err = create_tis(ndev);
	if (err)
		goto err_tis;

	res->valid = true;

	return 0;

err_tis:
	mlx5_vdpa_dealloc_transport_domain(&ndev->mvdev, res->tdn);
	return err;
}

static void free_resources(struct mlx5_vdpa_net *ndev)
{
	struct mlx5_vdpa_net_resources *res = &ndev->res;

	if (!res->valid)
		return;

	destroy_tis(ndev);
	mlx5_vdpa_dealloc_transport_domain(&ndev->mvdev, res->tdn);
	res->valid = false;
}

static void init_mvqs(struct mlx5_vdpa_net *ndev)
{
	struct mlx5_vdpa_virtqueue *mvq;
	int i;

	for (i = 0; i < 2 * mlx5_vdpa_max_qps(ndev->mvdev.max_vqs); ++i) {
		mvq = &ndev->vqs[i];
		memset(mvq, 0, offsetof(struct mlx5_vdpa_virtqueue, ri));
		mvq->index = i;
		mvq->ndev = ndev;
		mvq->fwqp.fw = true;
	}
	for (; i < ndev->mvdev.max_vqs; i++) {
		mvq = &ndev->vqs[i];
		memset(mvq, 0, offsetof(struct mlx5_vdpa_virtqueue, ri));
		mvq->index = i;
		mvq->ndev = ndev;
	}
}

struct mlx5_vdpa_mgmtdev {
	struct vdpa_mgmt_dev mgtdev;
	struct mlx5_adev *madev;
	struct mlx5_vdpa_net *ndev;
};

static int mlx5_vdpa_dev_add(struct vdpa_mgmt_dev *v_mdev, const char *name)
{
	struct mlx5_vdpa_mgmtdev *mgtdev = container_of(v_mdev, struct mlx5_vdpa_mgmtdev, mgtdev);
	struct virtio_net_config *config;
	struct mlx5_core_dev *pfmdev;
	struct mlx5_vdpa_dev *mvdev;
	struct mlx5_vdpa_net *ndev;
	struct mlx5_core_dev *mdev;
	u32 max_vqs;
	int err;

	if (mgtdev->ndev)
		return -ENOSPC;

	mdev = mgtdev->madev->mdev;
	if (!(MLX5_CAP_DEV_VDPA_EMULATION(mdev, virtio_queue_type) &
	    MLX5_VIRTIO_EMULATION_CAP_VIRTIO_QUEUE_TYPE_SPLIT)) {
		dev_warn(mdev->device, "missing support for split virtqueues\n");
		return -EOPNOTSUPP;
	}

	/* we save one virtqueue for control virtqueue should we require it */
	max_vqs = MLX5_CAP_DEV_VDPA_EMULATION(mdev, max_num_virtio_queues);
	max_vqs = min_t(u32, max_vqs, MLX5_MAX_SUPPORTED_VQS);

	ndev = vdpa_alloc_device(struct mlx5_vdpa_net, mvdev.vdev, mdev->device, &mlx5_vdpa_ops,
				 name, false);
	if (IS_ERR(ndev))
		return PTR_ERR(ndev);

	ndev->mvdev.max_vqs = max_vqs;
	mvdev = &ndev->mvdev;
	mvdev->mdev = mdev;
	init_mvqs(ndev);
	mutex_init(&ndev->reslock);
	config = &ndev->config;
	err = query_mtu(mdev, &ndev->mtu);
	if (err)
		goto err_mtu;

	err = mlx5_query_nic_vport_mac_address(mdev, 0, 0, config->mac);
	if (err)
		goto err_mtu;

	if (!is_zero_ether_addr(config->mac)) {
		pfmdev = pci_get_drvdata(pci_physfn(mdev->pdev));
		err = mlx5_mpfs_add_mac(pfmdev, config->mac);
		if (err)
			goto err_mtu;

		ndev->mvdev.mlx_features |= BIT_ULL(VIRTIO_NET_F_MAC);
	}

	config->max_virtqueue_pairs = cpu_to_mlx5vdpa16(mvdev, mlx5_vdpa_max_qps(max_vqs));
	mvdev->vdev.dma_dev = &mdev->pdev->dev;
	err = mlx5_vdpa_alloc_resources(&ndev->mvdev);
	if (err)
		goto err_mpfs;

	if (MLX5_CAP_GEN(mvdev->mdev, umem_uid_0)) {
		err = mlx5_vdpa_create_mr(mvdev, NULL);
		if (err)
			goto err_res;
	}

	err = alloc_resources(ndev);
	if (err)
		goto err_mr;

	mvdev->wq = create_singlethread_workqueue("mlx5_vdpa_ctrl_wq");
	if (!mvdev->wq) {
		err = -ENOMEM;
		goto err_res2;
	}

	ndev->cur_num_vqs = 2 * mlx5_vdpa_max_qps(max_vqs);
	mvdev->vdev.mdev = &mgtdev->mgtdev;
	err = _vdpa_register_device(&mvdev->vdev, ndev->cur_num_vqs + 1);
	if (err)
		goto err_reg;

	mgtdev->ndev = ndev;
	return 0;

err_reg:
	destroy_workqueue(mvdev->wq);
err_res2:
	free_resources(ndev);
err_mr:
	mlx5_vdpa_destroy_mr(mvdev);
err_res:
	mlx5_vdpa_free_resources(&ndev->mvdev);
err_mpfs:
	if (!is_zero_ether_addr(config->mac))
		mlx5_mpfs_del_mac(pfmdev, config->mac);
err_mtu:
	mutex_destroy(&ndev->reslock);
	put_device(&mvdev->vdev.dev);
	return err;
}

static void mlx5_vdpa_dev_del(struct vdpa_mgmt_dev *v_mdev, struct vdpa_device *dev)
{
	struct mlx5_vdpa_mgmtdev *mgtdev = container_of(v_mdev, struct mlx5_vdpa_mgmtdev, mgtdev);
	struct mlx5_vdpa_dev *mvdev = to_mvdev(dev);

	destroy_workqueue(mvdev->wq);
	_vdpa_unregister_device(dev);
	mgtdev->ndev = NULL;
}

static const struct vdpa_mgmtdev_ops mdev_ops = {
	.dev_add = mlx5_vdpa_dev_add,
	.dev_del = mlx5_vdpa_dev_del,
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_NET, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static int mlx5v_probe(struct auxiliary_device *adev,
		       const struct auxiliary_device_id *id)

{
	struct mlx5_adev *madev = container_of(adev, struct mlx5_adev, adev);
	struct mlx5_core_dev *mdev = madev->mdev;
	struct mlx5_vdpa_mgmtdev *mgtdev;
	int err;

	mgtdev = kzalloc(sizeof(*mgtdev), GFP_KERNEL);
	if (!mgtdev)
		return -ENOMEM;

	mgtdev->mgtdev.ops = &mdev_ops;
	mgtdev->mgtdev.device = mdev->device;
	mgtdev->mgtdev.id_table = id_table;
	mgtdev->madev = madev;

	err = vdpa_mgmtdev_register(&mgtdev->mgtdev);
	if (err)
		goto reg_err;

	dev_set_drvdata(&adev->dev, mgtdev);

	return 0;

reg_err:
	kfree(mgtdev);
	return err;
}

static void mlx5v_remove(struct auxiliary_device *adev)
{
	struct mlx5_vdpa_mgmtdev *mgtdev;

	mgtdev = dev_get_drvdata(&adev->dev);
	vdpa_mgmtdev_unregister(&mgtdev->mgtdev);
	kfree(mgtdev);
}

static const struct auxiliary_device_id mlx5v_id_table[] = {
	{ .name = MLX5_ADEV_NAME ".vnet", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, mlx5v_id_table);

static struct auxiliary_driver mlx5v_driver = {
	.name = "vnet",
	.probe = mlx5v_probe,
	.remove = mlx5v_remove,
	.id_table = mlx5v_id_table,
};

module_auxiliary_driver(mlx5v_driver);

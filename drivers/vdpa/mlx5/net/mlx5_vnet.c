// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd. */

#include <linux/module.h>
#include <linux/vdpa.h>
#include <linux/vringh.h>
#include <uapi/linux/virtio_net.h>
#include <uapi/linux/virtio_ids.h>
#include <uapi/linux/vdpa.h>
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
#include "mlx5_vnet.h"

MODULE_AUTHOR("Eli Cohen <eli@mellanox.com>");
MODULE_DESCRIPTION("Mellanox VDPA driver");
MODULE_LICENSE("Dual BSD/GPL");

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

#define MLX5V_UNTAGGED 0x1000

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
	struct msi_map map;
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
	 * other end is vqqp used by the driver. cq is where completions are
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

	u32 counter_set_id;
	bool initialized;
	int index;
	u32 virtq_id;
	struct mlx5_vdpa_net *ndev;
	u16 avail_idx;
	u16 used_idx;
	int fw_state;
	struct msi_map map;

	/* keep last in the struct */
	struct mlx5_vq_restore_info ri;
};

static bool is_index_valid(struct mlx5_vdpa_dev *mvdev, u16 idx)
{
	if (!(mvdev->actual_features & BIT_ULL(VIRTIO_NET_F_MQ))) {
		if (!(mvdev->actual_features & BIT_ULL(VIRTIO_NET_F_CTRL_VQ)))
			return idx < 2;
		else
			return idx < 3;
	}

	return idx <= mvdev->max_idx;
}

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

static u16 ctrl_vq_idx(struct mlx5_vdpa_dev *mvdev)
{
	if (!(mvdev->actual_features & BIT_ULL(VIRTIO_NET_F_MQ)))
		return 2;

	return mvdev->max_vqs;
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
	MLX5_SET(cqc, cqc, c_eqn_or_apu_element, eqn);
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

enum {
	MLX5_VIRTIO_NET_F_MRG_RXBUF = 2,
	MLX5_VIRTIO_NET_F_HOST_ECN = 4,
	MLX5_VIRTIO_NET_F_GUEST_ECN = 6,
	MLX5_VIRTIO_NET_F_GUEST_TSO6 = 7,
	MLX5_VIRTIO_NET_F_GUEST_TSO4 = 8,
	MLX5_VIRTIO_NET_F_GUEST_CSUM = 9,
	MLX5_VIRTIO_NET_F_CSUM = 10,
	MLX5_VIRTIO_NET_F_HOST_TSO6 = 11,
	MLX5_VIRTIO_NET_F_HOST_TSO4 = 12,
};

static u16 get_features(u64 features)
{
	return (!!(features & BIT_ULL(VIRTIO_NET_F_MRG_RXBUF)) << MLX5_VIRTIO_NET_F_MRG_RXBUF) |
	       (!!(features & BIT_ULL(VIRTIO_NET_F_HOST_ECN)) << MLX5_VIRTIO_NET_F_HOST_ECN) |
	       (!!(features & BIT_ULL(VIRTIO_NET_F_GUEST_ECN)) << MLX5_VIRTIO_NET_F_GUEST_ECN) |
	       (!!(features & BIT_ULL(VIRTIO_NET_F_GUEST_TSO6)) << MLX5_VIRTIO_NET_F_GUEST_TSO6) |
	       (!!(features & BIT_ULL(VIRTIO_NET_F_GUEST_TSO4)) << MLX5_VIRTIO_NET_F_GUEST_TSO4) |
	       (!!(features & BIT_ULL(VIRTIO_NET_F_CSUM)) << MLX5_VIRTIO_NET_F_CSUM) |
	       (!!(features & BIT_ULL(VIRTIO_NET_F_HOST_TSO6)) << MLX5_VIRTIO_NET_F_HOST_TSO6) |
	       (!!(features & BIT_ULL(VIRTIO_NET_F_HOST_TSO4)) << MLX5_VIRTIO_NET_F_HOST_TSO4);
}

static bool counters_supported(const struct mlx5_vdpa_dev *mvdev)
{
	return MLX5_CAP_GEN_64(mvdev->mdev, general_obj_types) &
	       BIT_ULL(MLX5_OBJ_TYPE_VIRTIO_Q_COUNTERS);
}

static bool msix_mode_supported(struct mlx5_vdpa_dev *mvdev)
{
	return MLX5_CAP_DEV_VDPA_EMULATION(mvdev->mdev, event_mode) &
		(1 << MLX5_VIRTIO_Q_EVENT_MODE_MSIX_MODE) &&
		pci_msix_can_alloc_dyn(mvdev->mdev->pdev);
}

static int create_virtqueue(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq)
{
	int inlen = MLX5_ST_SZ_BYTES(create_virtio_net_q_in);
	u32 out[MLX5_ST_SZ_DW(create_virtio_net_q_out)] = {};
	void *obj_context;
	u16 mlx_features;
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

	mlx_features = get_features(ndev->mvdev.actual_features);
	cmd_hdr = MLX5_ADDR_OF(create_virtio_net_q_in, in, general_obj_in_cmd_hdr);

	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, obj_type, MLX5_OBJ_TYPE_VIRTIO_NET_Q);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, uid, ndev->mvdev.res.uid);

	obj_context = MLX5_ADDR_OF(create_virtio_net_q_in, in, obj_context);
	MLX5_SET(virtio_net_q_object, obj_context, hw_available_index, mvq->avail_idx);
	MLX5_SET(virtio_net_q_object, obj_context, hw_used_index, mvq->used_idx);
	MLX5_SET(virtio_net_q_object, obj_context, queue_feature_bit_mask_12_3,
		 mlx_features >> 3);
	MLX5_SET(virtio_net_q_object, obj_context, queue_feature_bit_mask_2_0,
		 mlx_features & 7);
	vq_ctx = MLX5_ADDR_OF(virtio_net_q_object, obj_context, virtio_q_context);
	MLX5_SET(virtio_q, vq_ctx, virtio_q_type, get_queue_type(ndev));

	if (vq_is_tx(mvq->index))
		MLX5_SET(virtio_net_q_object, obj_context, tisn_or_qpn, ndev->res.tisn);

	if (mvq->map.virq) {
		MLX5_SET(virtio_q, vq_ctx, event_mode, MLX5_VIRTIO_Q_EVENT_MODE_MSIX_MODE);
		MLX5_SET(virtio_q, vq_ctx, event_qpn_or_msix, mvq->map.index);
	} else {
		MLX5_SET(virtio_q, vq_ctx, event_mode, MLX5_VIRTIO_Q_EVENT_MODE_QP_MODE);
		MLX5_SET(virtio_q, vq_ctx, event_qpn_or_msix, mvq->fwqp.mqp.qpn);
	}

	MLX5_SET(virtio_q, vq_ctx, queue_index, mvq->index);
	MLX5_SET(virtio_q, vq_ctx, queue_size, mvq->num_ent);
	MLX5_SET(virtio_q, vq_ctx, virtio_version_1_0,
		 !!(ndev->mvdev.actual_features & BIT_ULL(VIRTIO_F_VERSION_1)));
	MLX5_SET64(virtio_q, vq_ctx, desc_addr, mvq->desc_addr);
	MLX5_SET64(virtio_q, vq_ctx, used_addr, mvq->device_addr);
	MLX5_SET64(virtio_q, vq_ctx, available_addr, mvq->driver_addr);
	MLX5_SET(virtio_q, vq_ctx, virtio_q_mkey, ndev->mvdev.mr.mkey);
	MLX5_SET(virtio_q, vq_ctx, umem_1_id, mvq->umem1.id);
	MLX5_SET(virtio_q, vq_ctx, umem_1_size, mvq->umem1.size);
	MLX5_SET(virtio_q, vq_ctx, umem_2_id, mvq->umem2.id);
	MLX5_SET(virtio_q, vq_ctx, umem_2_size, mvq->umem2.size);
	MLX5_SET(virtio_q, vq_ctx, umem_3_id, mvq->umem3.id);
	MLX5_SET(virtio_q, vq_ctx, umem_3_size, mvq->umem3.size);
	MLX5_SET(virtio_q, vq_ctx, pd, ndev->mvdev.res.pdn);
	if (counters_supported(&ndev->mvdev))
		MLX5_SET(virtio_q, vq_ctx, counter_set_id, mvq->counter_set_id);

	err = mlx5_cmd_exec(ndev->mvdev.mdev, in, inlen, out, sizeof(out));
	if (err)
		goto err_cmd;

	mvq->fw_state = MLX5_VIRTIO_NET_Q_OBJECT_STATE_INIT;
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
	mvq->fw_state = MLX5_VIRTIO_NET_Q_OBJECT_NONE;
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

static bool is_valid_state_change(int oldstate, int newstate)
{
	switch (oldstate) {
	case MLX5_VIRTIO_NET_Q_OBJECT_STATE_INIT:
		return newstate == MLX5_VIRTIO_NET_Q_OBJECT_STATE_RDY;
	case MLX5_VIRTIO_NET_Q_OBJECT_STATE_RDY:
		return newstate == MLX5_VIRTIO_NET_Q_OBJECT_STATE_SUSPEND;
	case MLX5_VIRTIO_NET_Q_OBJECT_STATE_SUSPEND:
	case MLX5_VIRTIO_NET_Q_OBJECT_STATE_ERR:
	default:
		return false;
	}
}

static int modify_virtqueue(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq, int state)
{
	int inlen = MLX5_ST_SZ_BYTES(modify_virtio_net_q_in);
	u32 out[MLX5_ST_SZ_DW(modify_virtio_net_q_out)] = {};
	void *obj_context;
	void *cmd_hdr;
	void *in;
	int err;

	if (mvq->fw_state == MLX5_VIRTIO_NET_Q_OBJECT_NONE)
		return 0;

	if (!is_valid_state_change(mvq->fw_state, state))
		return -EINVAL;

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

static int counter_set_alloc(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq)
{
	u32 in[MLX5_ST_SZ_DW(create_virtio_q_counters_in)] = {};
	u32 out[MLX5_ST_SZ_DW(create_virtio_q_counters_out)] = {};
	void *cmd_hdr;
	int err;

	if (!counters_supported(&ndev->mvdev))
		return 0;

	cmd_hdr = MLX5_ADDR_OF(create_virtio_q_counters_in, in, hdr);

	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, obj_type, MLX5_OBJ_TYPE_VIRTIO_Q_COUNTERS);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, uid, ndev->mvdev.res.uid);

	err = mlx5_cmd_exec(ndev->mvdev.mdev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	mvq->counter_set_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);

	return 0;
}

static void counter_set_dealloc(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq)
{
	u32 in[MLX5_ST_SZ_DW(destroy_virtio_q_counters_in)] = {};
	u32 out[MLX5_ST_SZ_DW(destroy_virtio_q_counters_out)] = {};

	if (!counters_supported(&ndev->mvdev))
		return;

	MLX5_SET(destroy_virtio_q_counters_in, in, hdr.opcode, MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(destroy_virtio_q_counters_in, in, hdr.obj_id, mvq->counter_set_id);
	MLX5_SET(destroy_virtio_q_counters_in, in, hdr.uid, ndev->mvdev.res.uid);
	MLX5_SET(destroy_virtio_q_counters_in, in, hdr.obj_type, MLX5_OBJ_TYPE_VIRTIO_Q_COUNTERS);
	if (mlx5_cmd_exec(ndev->mvdev.mdev, in, sizeof(in), out, sizeof(out)))
		mlx5_vdpa_warn(&ndev->mvdev, "dealloc counter set 0x%x\n", mvq->counter_set_id);
}

static irqreturn_t mlx5_vdpa_int_handler(int irq, void *priv)
{
	struct vdpa_callback *cb = priv;

	if (cb->callback)
		return cb->callback(cb->private);

	return IRQ_HANDLED;
}

static void alloc_vector(struct mlx5_vdpa_net *ndev,
			 struct mlx5_vdpa_virtqueue *mvq)
{
	struct mlx5_vdpa_irq_pool *irqp = &ndev->irqp;
	struct mlx5_vdpa_irq_pool_entry *ent;
	int err;
	int i;

	for (i = 0; i < irqp->num_ent; i++) {
		ent = &irqp->entries[i];
		if (!ent->used) {
			snprintf(ent->name, MLX5_VDPA_IRQ_NAME_LEN, "%s-vq-%d",
				 dev_name(&ndev->mvdev.vdev.dev), mvq->index);
			ent->dev_id = &ndev->event_cbs[mvq->index];
			err = request_irq(ent->map.virq, mlx5_vdpa_int_handler, 0,
					  ent->name, ent->dev_id);
			if (err)
				return;

			ent->used = true;
			mvq->map = ent->map;
			return;
		}
	}
}

static void dealloc_vector(struct mlx5_vdpa_net *ndev,
			   struct mlx5_vdpa_virtqueue *mvq)
{
	struct mlx5_vdpa_irq_pool *irqp = &ndev->irqp;
	int i;

	for (i = 0; i < irqp->num_ent; i++)
		if (mvq->map.virq == irqp->entries[i].map.virq) {
			free_irq(mvq->map.virq, irqp->entries[i].dev_id);
			irqp->entries[i].used = false;
			return;
		}
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

	err = counter_set_alloc(ndev, mvq);
	if (err)
		goto err_connect;

	alloc_vector(ndev, mvq);
	err = create_virtqueue(ndev, mvq);
	if (err)
		goto err_vq;

	if (mvq->ready) {
		err = modify_virtqueue(ndev, mvq, MLX5_VIRTIO_NET_Q_OBJECT_STATE_RDY);
		if (err) {
			mlx5_vdpa_warn(&ndev->mvdev, "failed to modify to ready vq idx %d(%d)\n",
				       idx, err);
			goto err_modify;
		}
	}

	mvq->initialized = true;
	return 0;

err_modify:
	destroy_virtqueue(ndev, mvq);
err_vq:
	dealloc_vector(ndev, mvq);
	counter_set_dealloc(ndev, mvq);
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

	for (i = 0; i < ndev->mvdev.max_vqs; i++)
		suspend_vq(ndev, &ndev->vqs[i]);
}

static void teardown_vq(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq)
{
	if (!mvq->initialized)
		return;

	suspend_vq(ndev, mvq);
	destroy_virtqueue(ndev, mvq);
	dealloc_vector(ndev, mvq);
	counter_set_dealloc(ndev, mvq);
	qp_destroy(ndev, &mvq->vqqp);
	qp_destroy(ndev, &mvq->fwqp);
	cq_destroy(ndev, mvq->index);
	mvq->initialized = false;
}

static int create_rqt(struct mlx5_vdpa_net *ndev)
{
	int rqt_table_size = roundup_pow_of_two(ndev->rqt_size);
	int act_sz = roundup_pow_of_two(ndev->cur_num_vqs / 2);
	__be32 *list;
	void *rqtc;
	int inlen;
	void *in;
	int i, j;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_rqt_in) + rqt_table_size * MLX5_ST_SZ_BYTES(rq_num);
	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_rqt_in, in, uid, ndev->mvdev.res.uid);
	rqtc = MLX5_ADDR_OF(create_rqt_in, in, rqt_context);

	MLX5_SET(rqtc, rqtc, list_q_type, MLX5_RQTC_LIST_Q_TYPE_VIRTIO_NET_Q);
	MLX5_SET(rqtc, rqtc, rqt_max_size, rqt_table_size);
	list = MLX5_ADDR_OF(rqtc, rqtc, rq_num[0]);
	for (i = 0, j = 0; i < act_sz; i++, j += 2)
		list[i] = cpu_to_be32(ndev->vqs[j % ndev->cur_num_vqs].virtq_id);

	MLX5_SET(rqtc, rqtc, rqt_actual_size, act_sz);
	err = mlx5_vdpa_create_rqt(&ndev->mvdev, in, inlen, &ndev->res.rqtn);
	kfree(in);
	if (err)
		return err;

	return 0;
}

#define MLX5_MODIFY_RQT_NUM_RQS ((u64)1)

static int modify_rqt(struct mlx5_vdpa_net *ndev, int num)
{
	int act_sz = roundup_pow_of_two(num / 2);
	__be32 *list;
	void *rqtc;
	int inlen;
	void *in;
	int i, j;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rqt_in) + act_sz * MLX5_ST_SZ_BYTES(rq_num);
	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_rqt_in, in, uid, ndev->mvdev.res.uid);
	MLX5_SET64(modify_rqt_in, in, bitmask, MLX5_MODIFY_RQT_NUM_RQS);
	rqtc = MLX5_ADDR_OF(modify_rqt_in, in, ctx);
	MLX5_SET(rqtc, rqtc, list_q_type, MLX5_RQTC_LIST_Q_TYPE_VIRTIO_NET_Q);

	list = MLX5_ADDR_OF(rqtc, rqtc, rq_num[0]);
	for (i = 0, j = 0; i < act_sz; i++, j = j + 2)
		list[i] = cpu_to_be32(ndev->vqs[j % num].virtq_id);

	MLX5_SET(rqtc, rqtc, rqt_actual_size, act_sz);
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
	if (err)
		return err;

	mlx5_vdpa_add_tirn(ndev);
	return err;
}

static void destroy_tir(struct mlx5_vdpa_net *ndev)
{
	mlx5_vdpa_remove_tirn(ndev);
	mlx5_vdpa_destroy_tir(&ndev->mvdev, ndev->res.tirn);
}

#define MAX_STEERING_ENT 0x8000
#define MAX_STEERING_GROUPS 2

#if defined(CONFIG_MLX5_VDPA_STEERING_DEBUG)
       #define NUM_DESTS 2
#else
       #define NUM_DESTS 1
#endif

static int add_steering_counters(struct mlx5_vdpa_net *ndev,
				 struct macvlan_node *node,
				 struct mlx5_flow_act *flow_act,
				 struct mlx5_flow_destination *dests)
{
#if defined(CONFIG_MLX5_VDPA_STEERING_DEBUG)
	int err;

	node->ucast_counter.counter = mlx5_fc_create(ndev->mvdev.mdev, false);
	if (IS_ERR(node->ucast_counter.counter))
		return PTR_ERR(node->ucast_counter.counter);

	node->mcast_counter.counter = mlx5_fc_create(ndev->mvdev.mdev, false);
	if (IS_ERR(node->mcast_counter.counter)) {
		err = PTR_ERR(node->mcast_counter.counter);
		goto err_mcast_counter;
	}

	dests[1].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	flow_act->action |= MLX5_FLOW_CONTEXT_ACTION_COUNT;
	return 0;

err_mcast_counter:
	mlx5_fc_destroy(ndev->mvdev.mdev, node->ucast_counter.counter);
	return err;
#else
	return 0;
#endif
}

static void remove_steering_counters(struct mlx5_vdpa_net *ndev,
				     struct macvlan_node *node)
{
#if defined(CONFIG_MLX5_VDPA_STEERING_DEBUG)
	mlx5_fc_destroy(ndev->mvdev.mdev, node->mcast_counter.counter);
	mlx5_fc_destroy(ndev->mvdev.mdev, node->ucast_counter.counter);
#endif
}

static int mlx5_vdpa_add_mac_vlan_rules(struct mlx5_vdpa_net *ndev, u8 *mac,
					struct macvlan_node *node)
{
	struct mlx5_flow_destination dests[NUM_DESTS] = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_spec *spec;
	void *headers_c;
	void *headers_v;
	u8 *dmac_c;
	u8 *dmac_v;
	int err;
	u16 vid;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	vid = key2vid(node->macvlan);
	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, outer_headers);
	headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value, outer_headers);
	dmac_c = MLX5_ADDR_OF(fte_match_param, headers_c, outer_headers.dmac_47_16);
	dmac_v = MLX5_ADDR_OF(fte_match_param, headers_v, outer_headers.dmac_47_16);
	eth_broadcast_addr(dmac_c);
	ether_addr_copy(dmac_v, mac);
	if (ndev->mvdev.actual_features & BIT_ULL(VIRTIO_NET_F_CTRL_VLAN)) {
		MLX5_SET(fte_match_set_lyr_2_4, headers_c, cvlan_tag, 1);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, first_vid);
	}
	if (node->tagged) {
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, cvlan_tag, 1);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, first_vid, vid);
	}
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dests[0].type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	dests[0].tir_num = ndev->res.tirn;
	err = add_steering_counters(ndev, node, &flow_act, dests);
	if (err)
		goto out_free;

#if defined(CONFIG_MLX5_VDPA_STEERING_DEBUG)
	dests[1].counter_id = mlx5_fc_id(node->ucast_counter.counter);
#endif
	node->ucast_rule = mlx5_add_flow_rules(ndev->rxft, spec, &flow_act, dests, NUM_DESTS);
	if (IS_ERR(node->ucast_rule)) {
		err = PTR_ERR(node->ucast_rule);
		goto err_ucast;
	}

#if defined(CONFIG_MLX5_VDPA_STEERING_DEBUG)
	dests[1].counter_id = mlx5_fc_id(node->mcast_counter.counter);
#endif

	memset(dmac_c, 0, ETH_ALEN);
	memset(dmac_v, 0, ETH_ALEN);
	dmac_c[0] = 1;
	dmac_v[0] = 1;
	node->mcast_rule = mlx5_add_flow_rules(ndev->rxft, spec, &flow_act, dests, NUM_DESTS);
	if (IS_ERR(node->mcast_rule)) {
		err = PTR_ERR(node->mcast_rule);
		goto err_mcast;
	}
	kvfree(spec);
	mlx5_vdpa_add_rx_counters(ndev, node);
	return 0;

err_mcast:
	mlx5_del_flow_rules(node->ucast_rule);
err_ucast:
	remove_steering_counters(ndev, node);
out_free:
	kvfree(spec);
	return err;
}

static void mlx5_vdpa_del_mac_vlan_rules(struct mlx5_vdpa_net *ndev,
					 struct macvlan_node *node)
{
	mlx5_vdpa_remove_rx_counters(ndev, node);
	mlx5_del_flow_rules(node->ucast_rule);
	mlx5_del_flow_rules(node->mcast_rule);
}

static u64 search_val(u8 *mac, u16 vlan, bool tagged)
{
	u64 val;

	if (!tagged)
		vlan = MLX5V_UNTAGGED;

	val = (u64)vlan << 48 |
	      (u64)mac[0] << 40 |
	      (u64)mac[1] << 32 |
	      (u64)mac[2] << 24 |
	      (u64)mac[3] << 16 |
	      (u64)mac[4] << 8 |
	      (u64)mac[5];

	return val;
}

static struct macvlan_node *mac_vlan_lookup(struct mlx5_vdpa_net *ndev, u64 value)
{
	struct macvlan_node *pos;
	u32 idx;

	idx = hash_64(value, 8); // tbd 8
	hlist_for_each_entry(pos, &ndev->macvlan_hash[idx], hlist) {
		if (pos->macvlan == value)
			return pos;
	}
	return NULL;
}

static int mac_vlan_add(struct mlx5_vdpa_net *ndev, u8 *mac, u16 vid, bool tagged)
{
	struct macvlan_node *ptr;
	u64 val;
	u32 idx;
	int err;

	val = search_val(mac, vid, tagged);
	if (mac_vlan_lookup(ndev, val))
		return -EEXIST;

	ptr = kzalloc(sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ptr->tagged = tagged;
	ptr->macvlan = val;
	ptr->ndev = ndev;
	err = mlx5_vdpa_add_mac_vlan_rules(ndev, ndev->config.mac, ptr);
	if (err)
		goto err_add;

	idx = hash_64(val, 8);
	hlist_add_head(&ptr->hlist, &ndev->macvlan_hash[idx]);
	return 0;

err_add:
	kfree(ptr);
	return err;
}

static void mac_vlan_del(struct mlx5_vdpa_net *ndev, u8 *mac, u16 vlan, bool tagged)
{
	struct macvlan_node *ptr;

	ptr = mac_vlan_lookup(ndev, search_val(mac, vlan, tagged));
	if (!ptr)
		return;

	hlist_del(&ptr->hlist);
	mlx5_vdpa_del_mac_vlan_rules(ndev, ptr);
	remove_steering_counters(ndev, ptr);
	kfree(ptr);
}

static void clear_mac_vlan_table(struct mlx5_vdpa_net *ndev)
{
	struct macvlan_node *pos;
	struct hlist_node *n;
	int i;

	for (i = 0; i < MLX5V_MACVLAN_SIZE; i++) {
		hlist_for_each_entry_safe(pos, n, &ndev->macvlan_hash[i], hlist) {
			hlist_del(&pos->hlist);
			mlx5_vdpa_del_mac_vlan_rules(ndev, pos);
			remove_steering_counters(ndev, pos);
			kfree(pos);
		}
	}
}

static int setup_steering(struct mlx5_vdpa_net *ndev)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_namespace *ns;
	int err;

	ft_attr.max_fte = MAX_STEERING_ENT;
	ft_attr.autogroup.max_num_groups = MAX_STEERING_GROUPS;

	ns = mlx5_get_flow_namespace(ndev->mvdev.mdev, MLX5_FLOW_NAMESPACE_BYPASS);
	if (!ns) {
		mlx5_vdpa_warn(&ndev->mvdev, "failed to get flow namespace\n");
		return -EOPNOTSUPP;
	}

	ndev->rxft = mlx5_create_auto_grouped_flow_table(ns, &ft_attr);
	if (IS_ERR(ndev->rxft)) {
		mlx5_vdpa_warn(&ndev->mvdev, "failed to create flow table\n");
		return PTR_ERR(ndev->rxft);
	}
	mlx5_vdpa_add_rx_flow_table(ndev);

	err = mac_vlan_add(ndev, ndev->config.mac, 0, false);
	if (err)
		goto err_add;

	return 0;

err_add:
	mlx5_vdpa_remove_rx_flow_table(ndev);
	mlx5_destroy_flow_table(ndev->rxft);
	return err;
}

static void teardown_steering(struct mlx5_vdpa_net *ndev)
{
	clear_mac_vlan_table(ndev);
	mlx5_vdpa_remove_rx_flow_table(ndev);
	mlx5_destroy_flow_table(ndev->rxft);
}

static virtio_net_ctrl_ack handle_ctrl_mac(struct mlx5_vdpa_dev *mvdev, u8 cmd)
{
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct mlx5_control_vq *cvq = &mvdev->cvq;
	virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
	struct mlx5_core_dev *pfmdev;
	size_t read;
	u8 mac[ETH_ALEN], mac_back[ETH_ALEN];

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

		if (is_zero_ether_addr(mac))
			break;

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

		/* backup the original mac address so that if failed to add the forward rules
		 * we could restore it
		 */
		memcpy(mac_back, ndev->config.mac, ETH_ALEN);

		memcpy(ndev->config.mac, mac, ETH_ALEN);

		/* Need recreate the flow table entry, so that the packet could forward back
		 */
		mac_vlan_del(ndev, mac_back, 0, false);

		if (mac_vlan_add(ndev, ndev->config.mac, 0, false)) {
			mlx5_vdpa_warn(mvdev, "failed to insert forward rules, try to restore\n");

			/* Although it hardly run here, we still need double check */
			if (is_zero_ether_addr(mac_back)) {
				mlx5_vdpa_warn(mvdev, "restore mac failed: Original MAC is zero\n");
				break;
			}

			/* Try to restore original mac address to MFPS table, and try to restore
			 * the forward rule entry.
			 */
			if (mlx5_mpfs_del_mac(pfmdev, ndev->config.mac)) {
				mlx5_vdpa_warn(mvdev, "restore mac failed: delete MAC %pM from MPFS table failed\n",
					       ndev->config.mac);
			}

			if (mlx5_mpfs_add_mac(pfmdev, mac_back)) {
				mlx5_vdpa_warn(mvdev, "restore mac failed: insert old MAC %pM into MPFS table failed\n",
					       mac_back);
			}

			memcpy(ndev->config.mac, mac_back, ETH_ALEN);

			if (mac_vlan_add(ndev, ndev->config.mac, 0, false))
				mlx5_vdpa_warn(mvdev, "restore forward rules failed: insert forward rules failed\n");

			break;
		}

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
	for (--i; i >= 2 * cur_qps; --i)
		teardown_vq(ndev, &ndev->vqs[i]);

	ndev->cur_num_vqs = 2 * cur_qps;

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
		/* This mq feature check aligns with pre-existing userspace
		 * implementation.
		 *
		 * Without it, an untrusted driver could fake a multiqueue config
		 * request down to a non-mq device that may cause kernel to
		 * panic due to uninitialized resources for extra vqs. Even with
		 * a well behaving guest driver, it is not expected to allow
		 * changing the number of vqs on a non-mq device.
		 */
		if (!MLX5_FEATURE(mvdev, VIRTIO_NET_F_MQ))
			break;

		read = vringh_iov_pull_iotlb(&cvq->vring, &cvq->riov, (void *)&mq, sizeof(mq));
		if (read != sizeof(mq))
			break;

		newqps = mlx5vdpa16_to_cpu(mvdev, mq.virtqueue_pairs);
		if (newqps < VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MIN ||
		    newqps > ndev->rqt_size)
			break;

		if (ndev->cur_num_vqs == 2 * newqps) {
			status = VIRTIO_NET_OK;
			break;
		}

		if (!change_num_qps(mvdev, newqps))
			status = VIRTIO_NET_OK;

		break;
	default:
		break;
	}

	return status;
}

static virtio_net_ctrl_ack handle_ctrl_vlan(struct mlx5_vdpa_dev *mvdev, u8 cmd)
{
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
	struct mlx5_control_vq *cvq = &mvdev->cvq;
	__virtio16 vlan;
	size_t read;
	u16 id;

	if (!(ndev->mvdev.actual_features & BIT_ULL(VIRTIO_NET_F_CTRL_VLAN)))
		return status;

	switch (cmd) {
	case VIRTIO_NET_CTRL_VLAN_ADD:
		read = vringh_iov_pull_iotlb(&cvq->vring, &cvq->riov, &vlan, sizeof(vlan));
		if (read != sizeof(vlan))
			break;

		id = mlx5vdpa16_to_cpu(mvdev, vlan);
		if (mac_vlan_add(ndev, ndev->config.mac, id, true))
			break;

		status = VIRTIO_NET_OK;
		break;
	case VIRTIO_NET_CTRL_VLAN_DEL:
		read = vringh_iov_pull_iotlb(&cvq->vring, &cvq->riov, &vlan, sizeof(vlan));
		if (read != sizeof(vlan))
			break;

		id = mlx5vdpa16_to_cpu(mvdev, vlan);
		mac_vlan_del(ndev, ndev->config.mac, id, true);
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
	struct mlx5_vdpa_wq_ent *wqent;
	struct mlx5_vdpa_dev *mvdev;
	struct mlx5_control_vq *cvq;
	struct mlx5_vdpa_net *ndev;
	size_t read, write;
	int err;

	wqent = container_of(work, struct mlx5_vdpa_wq_ent, work);
	mvdev = wqent->mvdev;
	ndev = to_mlx5_vdpa_ndev(mvdev);
	cvq = &mvdev->cvq;

	down_write(&ndev->reslock);

	if (!(mvdev->status & VIRTIO_CONFIG_S_DRIVER_OK))
		goto out;

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

		cvq->received_desc++;
		switch (ctrl.class) {
		case VIRTIO_NET_CTRL_MAC:
			status = handle_ctrl_mac(mvdev, ctrl.cmd);
			break;
		case VIRTIO_NET_CTRL_MQ:
			status = handle_ctrl_mq(mvdev, ctrl.cmd);
			break;
		case VIRTIO_NET_CTRL_VLAN:
			status = handle_ctrl_vlan(mvdev, ctrl.cmd);
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

		cvq->completed_desc++;
		queue_work(mvdev->wq, &wqent->work);
		break;
	}

out:
	up_write(&ndev->reslock);
}

static void mlx5_vdpa_kick_vq(struct vdpa_device *vdev, u16 idx)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct mlx5_vdpa_virtqueue *mvq;

	if (!is_index_valid(mvdev, idx))
		return;

	if (unlikely(is_ctrl_vq_idx(mvdev, idx))) {
		if (!mvdev->wq || !mvdev->cvq.ready)
			return;

		queue_work(mvdev->wq, &ndev->cvq_ent.work);
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
	if (is_ctrl_vq_idx(mvdev, idx))
		mvdev->cvq.event_cb = *cb;
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
	int err;

	if (!mvdev->actual_features)
		return;

	if (!is_index_valid(mvdev, idx))
		return;

	if (is_ctrl_vq_idx(mvdev, idx)) {
		set_cvq_ready(mvdev, ready);
		return;
	}

	mvq = &ndev->vqs[idx];
	if (!ready) {
		suspend_vq(ndev, mvq);
	} else {
		err = modify_virtqueue(ndev, mvq, MLX5_VIRTIO_NET_Q_OBJECT_STATE_RDY);
		if (err) {
			mlx5_vdpa_warn(mvdev, "modify VQ %d to ready failed (%d)\n", idx, err);
			ready = false;
		}
	}


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

static u32 mlx5_vdpa_get_vq_group(struct vdpa_device *vdev, u16 idx)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);

	if (is_ctrl_vq_idx(mvdev, idx))
		return MLX5_VDPA_CVQ_GROUP;

	return MLX5_VDPA_DATAVQ_GROUP;
}

static u64 mlx_to_vritio_features(u16 dev_features)
{
	u64 result = 0;

	if (dev_features & BIT_ULL(MLX5_VIRTIO_NET_F_MRG_RXBUF))
		result |= BIT_ULL(VIRTIO_NET_F_MRG_RXBUF);
	if (dev_features & BIT_ULL(MLX5_VIRTIO_NET_F_HOST_ECN))
		result |= BIT_ULL(VIRTIO_NET_F_HOST_ECN);
	if (dev_features & BIT_ULL(MLX5_VIRTIO_NET_F_GUEST_ECN))
		result |= BIT_ULL(VIRTIO_NET_F_GUEST_ECN);
	if (dev_features & BIT_ULL(MLX5_VIRTIO_NET_F_GUEST_TSO6))
		result |= BIT_ULL(VIRTIO_NET_F_GUEST_TSO6);
	if (dev_features & BIT_ULL(MLX5_VIRTIO_NET_F_GUEST_TSO4))
		result |= BIT_ULL(VIRTIO_NET_F_GUEST_TSO4);
	if (dev_features & BIT_ULL(MLX5_VIRTIO_NET_F_GUEST_CSUM))
		result |= BIT_ULL(VIRTIO_NET_F_GUEST_CSUM);
	if (dev_features & BIT_ULL(MLX5_VIRTIO_NET_F_CSUM))
		result |= BIT_ULL(VIRTIO_NET_F_CSUM);
	if (dev_features & BIT_ULL(MLX5_VIRTIO_NET_F_HOST_TSO6))
		result |= BIT_ULL(VIRTIO_NET_F_HOST_TSO6);
	if (dev_features & BIT_ULL(MLX5_VIRTIO_NET_F_HOST_TSO4))
		result |= BIT_ULL(VIRTIO_NET_F_HOST_TSO4);

	return result;
}

static u64 get_supported_features(struct mlx5_core_dev *mdev)
{
	u64 mlx_vdpa_features = 0;
	u16 dev_features;

	dev_features = MLX5_CAP_DEV_VDPA_EMULATION(mdev, device_features_bits_mask);
	mlx_vdpa_features |= mlx_to_vritio_features(dev_features);
	if (MLX5_CAP_DEV_VDPA_EMULATION(mdev, virtio_version_1_0))
		mlx_vdpa_features |= BIT_ULL(VIRTIO_F_VERSION_1);
	mlx_vdpa_features |= BIT_ULL(VIRTIO_F_ACCESS_PLATFORM);
	mlx_vdpa_features |= BIT_ULL(VIRTIO_NET_F_CTRL_VQ);
	mlx_vdpa_features |= BIT_ULL(VIRTIO_NET_F_CTRL_MAC_ADDR);
	mlx_vdpa_features |= BIT_ULL(VIRTIO_NET_F_MQ);
	mlx_vdpa_features |= BIT_ULL(VIRTIO_NET_F_STATUS);
	mlx_vdpa_features |= BIT_ULL(VIRTIO_NET_F_MTU);
	mlx_vdpa_features |= BIT_ULL(VIRTIO_NET_F_CTRL_VLAN);
	mlx_vdpa_features |= BIT_ULL(VIRTIO_NET_F_MAC);

	return mlx_vdpa_features;
}

static u64 mlx5_vdpa_get_device_features(struct vdpa_device *vdev)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);

	print_features(mvdev, ndev->mvdev.mlx_features, false);
	return ndev->mvdev.mlx_features;
}

static int verify_driver_features(struct mlx5_vdpa_dev *mvdev, u64 features)
{
	/* Minimum features to expect */
	if (!(features & BIT_ULL(VIRTIO_F_ACCESS_PLATFORM)))
		return -EOPNOTSUPP;

	/* Double check features combination sent down by the driver.
	 * Fail invalid features due to absence of the depended feature.
	 *
	 * Per VIRTIO v1.1 specification, section 5.1.3.1 Feature bit
	 * requirements: "VIRTIO_NET_F_MQ Requires VIRTIO_NET_F_CTRL_VQ".
	 * By failing the invalid features sent down by untrusted drivers,
	 * we're assured the assumption made upon is_index_valid() and
	 * is_ctrl_vq_idx() will not be compromised.
	 */
	if ((features & (BIT_ULL(VIRTIO_NET_F_MQ) | BIT_ULL(VIRTIO_NET_F_CTRL_VQ))) ==
            BIT_ULL(VIRTIO_NET_F_MQ))
		return -EINVAL;

	return 0;
}

static int setup_virtqueues(struct mlx5_vdpa_dev *mvdev)
{
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	int err;
	int i;

	for (i = 0; i < mvdev->max_vqs; i++) {
		err = setup_vq(ndev, &ndev->vqs[i]);
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

static u8 query_vport_state(struct mlx5_core_dev *mdev, u8 opmod, u16 vport)
{
	u32 out[MLX5_ST_SZ_DW(query_vport_state_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_vport_state_in)] = {};
	int err;

	MLX5_SET(query_vport_state_in, in, opcode, MLX5_CMD_OP_QUERY_VPORT_STATE);
	MLX5_SET(query_vport_state_in, in, op_mod, opmod);
	MLX5_SET(query_vport_state_in, in, vport_number, vport);
	if (vport)
		MLX5_SET(query_vport_state_in, in, other_vport, 1);

	err = mlx5_cmd_exec_inout(mdev, query_vport_state, in, out);
	if (err)
		return 0;

	return MLX5_GET(query_vport_state_out, out, state);
}

static bool get_link_state(struct mlx5_vdpa_dev *mvdev)
{
	if (query_vport_state(mvdev->mdev, MLX5_VPORT_STATE_OP_MOD_VNIC_VPORT, 0) ==
	    VPORT_STATE_UP)
		return true;

	return false;
}

static void update_carrier(struct work_struct *work)
{
	struct mlx5_vdpa_wq_ent *wqent;
	struct mlx5_vdpa_dev *mvdev;
	struct mlx5_vdpa_net *ndev;

	wqent = container_of(work, struct mlx5_vdpa_wq_ent, work);
	mvdev = wqent->mvdev;
	ndev = to_mlx5_vdpa_ndev(mvdev);
	if (get_link_state(mvdev))
		ndev->config.status |= cpu_to_mlx5vdpa16(mvdev, VIRTIO_NET_S_LINK_UP);
	else
		ndev->config.status &= cpu_to_mlx5vdpa16(mvdev, ~VIRTIO_NET_S_LINK_UP);

	if (ndev->config_cb.callback)
		ndev->config_cb.callback(ndev->config_cb.private);

	kfree(wqent);
}

static int queue_link_work(struct mlx5_vdpa_net *ndev)
{
	struct mlx5_vdpa_wq_ent *wqent;

	wqent = kzalloc(sizeof(*wqent), GFP_ATOMIC);
	if (!wqent)
		return -ENOMEM;

	wqent->mvdev = &ndev->mvdev;
	INIT_WORK(&wqent->work, update_carrier);
	queue_work(ndev->mvdev.wq, &wqent->work);
	return 0;
}

static int event_handler(struct notifier_block *nb, unsigned long event, void *param)
{
	struct mlx5_vdpa_net *ndev = container_of(nb, struct mlx5_vdpa_net, nb);
	struct mlx5_eqe *eqe = param;
	int ret = NOTIFY_DONE;

	if (event == MLX5_EVENT_TYPE_PORT_CHANGE) {
		switch (eqe->sub_type) {
		case MLX5_PORT_CHANGE_SUBTYPE_DOWN:
		case MLX5_PORT_CHANGE_SUBTYPE_ACTIVE:
			if (queue_link_work(ndev))
				return NOTIFY_DONE;

			ret = NOTIFY_OK;
			break;
		default:
			return NOTIFY_DONE;
		}
		return ret;
	}
	return ret;
}

static void register_link_notifier(struct mlx5_vdpa_net *ndev)
{
	if (!(ndev->mvdev.actual_features & BIT_ULL(VIRTIO_NET_F_STATUS)))
		return;

	ndev->nb.notifier_call = event_handler;
	mlx5_notifier_register(ndev->mvdev.mdev, &ndev->nb);
	ndev->nb_registered = true;
	queue_link_work(ndev);
}

static void unregister_link_notifier(struct mlx5_vdpa_net *ndev)
{
	if (!ndev->nb_registered)
		return;

	ndev->nb_registered = false;
	mlx5_notifier_unregister(ndev->mvdev.mdev, &ndev->nb);
	if (ndev->mvdev.wq)
		flush_workqueue(ndev->mvdev.wq);
}

static int mlx5_vdpa_set_driver_features(struct vdpa_device *vdev, u64 features)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	int err;

	print_features(mvdev, features, true);

	err = verify_driver_features(mvdev, features);
	if (err)
		return err;

	ndev->mvdev.actual_features = features & ndev->mvdev.mlx_features;
	if (ndev->mvdev.actual_features & BIT_ULL(VIRTIO_NET_F_MQ))
		ndev->rqt_size = mlx5vdpa16_to_cpu(mvdev, ndev->config.max_virtqueue_pairs);
	else
		ndev->rqt_size = 1;

	ndev->cur_num_vqs = 2 * ndev->rqt_size;

	update_cvq_info(mvdev);
	return err;
}

static void mlx5_vdpa_set_config_cb(struct vdpa_device *vdev, struct vdpa_callback *cb)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);

	ndev->config_cb = *cb;
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
	ri->map = mvq->map;
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
		mvq->map = ri->map;
	}
}

static int mlx5_vdpa_change_map(struct mlx5_vdpa_dev *mvdev,
				struct vhost_iotlb *iotlb, unsigned int asid)
{
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	int err;

	suspend_vqs(ndev);
	err = save_channels_info(ndev);
	if (err)
		goto err_mr;

	teardown_driver(ndev);
	mlx5_vdpa_destroy_mr(mvdev);
	err = mlx5_vdpa_create_mr(mvdev, iotlb, asid);
	if (err)
		goto err_mr;

	if (!(mvdev->status & VIRTIO_CONFIG_S_DRIVER_OK) || mvdev->suspended)
		goto err_mr;

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

/* reslock must be held for this function */
static int setup_driver(struct mlx5_vdpa_dev *mvdev)
{
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	int err;

	WARN_ON(!rwsem_is_locked(&ndev->reslock));

	if (ndev->setup) {
		mlx5_vdpa_warn(mvdev, "setup driver called for already setup driver\n");
		err = 0;
		goto out;
	}
	mlx5_vdpa_add_debugfs(ndev);
	err = setup_virtqueues(mvdev);
	if (err) {
		mlx5_vdpa_warn(mvdev, "setup_virtqueues\n");
		goto err_setup;
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

	err = setup_steering(ndev);
	if (err) {
		mlx5_vdpa_warn(mvdev, "setup_steering\n");
		goto err_fwd;
	}
	ndev->setup = true;

	return 0;

err_fwd:
	destroy_tir(ndev);
err_tir:
	destroy_rqt(ndev);
err_rqt:
	teardown_virtqueues(ndev);
err_setup:
	mlx5_vdpa_remove_debugfs(ndev->debugfs);
out:
	return err;
}

/* reslock must be held for this function */
static void teardown_driver(struct mlx5_vdpa_net *ndev)
{

	WARN_ON(!rwsem_is_locked(&ndev->reslock));

	if (!ndev->setup)
		return;

	mlx5_vdpa_remove_debugfs(ndev->debugfs);
	ndev->debugfs = NULL;
	teardown_steering(ndev);
	destroy_tir(ndev);
	destroy_rqt(ndev);
	teardown_virtqueues(ndev);
	ndev->setup = false;
}

static void clear_vqs_ready(struct mlx5_vdpa_net *ndev)
{
	int i;

	for (i = 0; i < ndev->mvdev.max_vqs; i++)
		ndev->vqs[i].ready = false;

	ndev->mvdev.cvq.ready = false;
}

static int setup_cvq_vring(struct mlx5_vdpa_dev *mvdev)
{
	struct mlx5_control_vq *cvq = &mvdev->cvq;
	int err = 0;

	if (mvdev->actual_features & BIT_ULL(VIRTIO_NET_F_CTRL_VQ))
		err = vringh_init_iotlb(&cvq->vring, mvdev->actual_features,
					MLX5_CVQ_MAX_ENT, false,
					(struct vring_desc *)(uintptr_t)cvq->desc_addr,
					(struct vring_avail *)(uintptr_t)cvq->driver_addr,
					(struct vring_used *)(uintptr_t)cvq->device_addr);

	return err;
}

static void mlx5_vdpa_set_status(struct vdpa_device *vdev, u8 status)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	int err;

	print_status(mvdev, status, true);

	down_write(&ndev->reslock);

	if ((status ^ ndev->mvdev.status) & VIRTIO_CONFIG_S_DRIVER_OK) {
		if (status & VIRTIO_CONFIG_S_DRIVER_OK) {
			err = setup_cvq_vring(mvdev);
			if (err) {
				mlx5_vdpa_warn(mvdev, "failed to setup control VQ vring\n");
				goto err_setup;
			}
			register_link_notifier(ndev);
			err = setup_driver(mvdev);
			if (err) {
				mlx5_vdpa_warn(mvdev, "failed to setup driver\n");
				goto err_driver;
			}
		} else {
			mlx5_vdpa_warn(mvdev, "did not expect DRIVER_OK to be cleared\n");
			goto err_clear;
		}
	}

	ndev->mvdev.status = status;
	up_write(&ndev->reslock);
	return;

err_driver:
	unregister_link_notifier(ndev);
err_setup:
	mlx5_vdpa_destroy_mr(&ndev->mvdev);
	ndev->mvdev.status |= VIRTIO_CONFIG_S_FAILED;
err_clear:
	up_write(&ndev->reslock);
}

static void init_group_to_asid_map(struct mlx5_vdpa_dev *mvdev)
{
	int i;

	/* default mapping all groups are mapped to asid 0 */
	for (i = 0; i < MLX5_VDPA_NUMVQ_GROUPS; i++)
		mvdev->group2asid[i] = 0;
}

static int mlx5_vdpa_reset(struct vdpa_device *vdev)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);

	print_status(mvdev, 0, true);
	mlx5_vdpa_info(mvdev, "performing device reset\n");

	down_write(&ndev->reslock);
	unregister_link_notifier(ndev);
	teardown_driver(ndev);
	clear_vqs_ready(ndev);
	mlx5_vdpa_destroy_mr(&ndev->mvdev);
	ndev->mvdev.status = 0;
	ndev->mvdev.suspended = false;
	ndev->cur_num_vqs = 0;
	ndev->mvdev.cvq.received_desc = 0;
	ndev->mvdev.cvq.completed_desc = 0;
	memset(ndev->event_cbs, 0, sizeof(*ndev->event_cbs) * (mvdev->max_vqs + 1));
	ndev->mvdev.actual_features = 0;
	init_group_to_asid_map(mvdev);
	++mvdev->generation;

	if (MLX5_CAP_GEN(mvdev->mdev, umem_uid_0)) {
		if (mlx5_vdpa_create_mr(mvdev, NULL, 0))
			mlx5_vdpa_warn(mvdev, "create MR failed\n");
	}
	up_write(&ndev->reslock);

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

static int set_map_data(struct mlx5_vdpa_dev *mvdev, struct vhost_iotlb *iotlb,
			unsigned int asid)
{
	bool change_map;
	int err;

	err = mlx5_vdpa_handle_set_map(mvdev, iotlb, &change_map, asid);
	if (err) {
		mlx5_vdpa_warn(mvdev, "set map failed(%d)\n", err);
		return err;
	}

	if (change_map)
		err = mlx5_vdpa_change_map(mvdev, iotlb, asid);

	return err;
}

static int mlx5_vdpa_set_map(struct vdpa_device *vdev, unsigned int asid,
			     struct vhost_iotlb *iotlb)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	int err = -EINVAL;

	down_write(&ndev->reslock);
	err = set_map_data(mvdev, iotlb, asid);
	up_write(&ndev->reslock);
	return err;
}

static struct device *mlx5_get_vq_dma_dev(struct vdpa_device *vdev, u16 idx)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);

	if (is_ctrl_vq_idx(mvdev, idx))
		return &vdev->dev;

	return mvdev->vdev.dma_dev;
}

static void free_irqs(struct mlx5_vdpa_net *ndev)
{
	struct mlx5_vdpa_irq_pool_entry *ent;
	int i;

	if (!msix_mode_supported(&ndev->mvdev))
		return;

	if (!ndev->irqp.entries)
		return;

	for (i = ndev->irqp.num_ent - 1; i >= 0; i--) {
		ent = ndev->irqp.entries + i;
		if (ent->map.virq)
			pci_msix_free_irq(ndev->mvdev.mdev->pdev, ent->map);
	}
	kfree(ndev->irqp.entries);
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
	free_irqs(ndev);
	kfree(ndev->event_cbs);
	kfree(ndev->vqs);
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

static int mlx5_get_vq_irq(struct vdpa_device *vdev, u16 idx)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct mlx5_vdpa_virtqueue *mvq;

	if (!is_index_valid(mvdev, idx))
		return -EINVAL;

	if (is_ctrl_vq_idx(mvdev, idx))
		return -EOPNOTSUPP;

	mvq = &ndev->vqs[idx];
	if (!mvq->map.virq)
		return -EOPNOTSUPP;

	return mvq->map.virq;
}

static u64 mlx5_vdpa_get_driver_features(struct vdpa_device *vdev)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);

	return mvdev->actual_features;
}

static int counter_set_query(struct mlx5_vdpa_net *ndev, struct mlx5_vdpa_virtqueue *mvq,
			     u64 *received_desc, u64 *completed_desc)
{
	u32 in[MLX5_ST_SZ_DW(query_virtio_q_counters_in)] = {};
	u32 out[MLX5_ST_SZ_DW(query_virtio_q_counters_out)] = {};
	void *cmd_hdr;
	void *ctx;
	int err;

	if (!counters_supported(&ndev->mvdev))
		return -EOPNOTSUPP;

	if (mvq->fw_state != MLX5_VIRTIO_NET_Q_OBJECT_STATE_RDY)
		return -EAGAIN;

	cmd_hdr = MLX5_ADDR_OF(query_virtio_q_counters_in, in, hdr);

	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, opcode, MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, obj_type, MLX5_OBJ_TYPE_VIRTIO_Q_COUNTERS);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, uid, ndev->mvdev.res.uid);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, obj_id, mvq->counter_set_id);

	err = mlx5_cmd_exec(ndev->mvdev.mdev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	ctx = MLX5_ADDR_OF(query_virtio_q_counters_out, out, counters);
	*received_desc = MLX5_GET64(virtio_q_counters, ctx, received_desc);
	*completed_desc = MLX5_GET64(virtio_q_counters, ctx, completed_desc);
	return 0;
}

static int mlx5_vdpa_get_vendor_vq_stats(struct vdpa_device *vdev, u16 idx,
					 struct sk_buff *msg,
					 struct netlink_ext_ack *extack)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct mlx5_vdpa_virtqueue *mvq;
	struct mlx5_control_vq *cvq;
	u64 received_desc;
	u64 completed_desc;
	int err = 0;

	down_read(&ndev->reslock);
	if (!is_index_valid(mvdev, idx)) {
		NL_SET_ERR_MSG_MOD(extack, "virtqueue index is not valid");
		err = -EINVAL;
		goto out_err;
	}

	if (idx == ctrl_vq_idx(mvdev)) {
		cvq = &mvdev->cvq;
		received_desc = cvq->received_desc;
		completed_desc = cvq->completed_desc;
		goto out;
	}

	mvq = &ndev->vqs[idx];
	err = counter_set_query(ndev, mvq, &received_desc, &completed_desc);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "failed to query hardware");
		goto out_err;
	}

out:
	err = -EMSGSIZE;
	if (nla_put_string(msg, VDPA_ATTR_DEV_VENDOR_ATTR_NAME, "received_desc"))
		goto out_err;

	if (nla_put_u64_64bit(msg, VDPA_ATTR_DEV_VENDOR_ATTR_VALUE, received_desc,
			      VDPA_ATTR_PAD))
		goto out_err;

	if (nla_put_string(msg, VDPA_ATTR_DEV_VENDOR_ATTR_NAME, "completed_desc"))
		goto out_err;

	if (nla_put_u64_64bit(msg, VDPA_ATTR_DEV_VENDOR_ATTR_VALUE, completed_desc,
			      VDPA_ATTR_PAD))
		goto out_err;

	err = 0;
out_err:
	up_read(&ndev->reslock);
	return err;
}

static void mlx5_vdpa_cvq_suspend(struct mlx5_vdpa_dev *mvdev)
{
	struct mlx5_control_vq *cvq;

	if (!(mvdev->actual_features & BIT_ULL(VIRTIO_NET_F_CTRL_VQ)))
		return;

	cvq = &mvdev->cvq;
	cvq->ready = false;
}

static int mlx5_vdpa_suspend(struct vdpa_device *vdev)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct mlx5_vdpa_virtqueue *mvq;
	int i;

	mlx5_vdpa_info(mvdev, "suspending device\n");

	down_write(&ndev->reslock);
	unregister_link_notifier(ndev);
	for (i = 0; i < ndev->cur_num_vqs; i++) {
		mvq = &ndev->vqs[i];
		suspend_vq(ndev, mvq);
	}
	mlx5_vdpa_cvq_suspend(mvdev);
	mvdev->suspended = true;
	up_write(&ndev->reslock);
	return 0;
}

static int mlx5_set_group_asid(struct vdpa_device *vdev, u32 group,
			       unsigned int asid)
{
	struct mlx5_vdpa_dev *mvdev = to_mvdev(vdev);

	if (group >= MLX5_VDPA_NUMVQ_GROUPS)
		return -EINVAL;

	mvdev->group2asid[group] = asid;
	return 0;
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
	.get_vendor_vq_stats = mlx5_vdpa_get_vendor_vq_stats,
	.get_vq_notification = mlx5_get_vq_notification,
	.get_vq_irq = mlx5_get_vq_irq,
	.get_vq_align = mlx5_vdpa_get_vq_align,
	.get_vq_group = mlx5_vdpa_get_vq_group,
	.get_device_features = mlx5_vdpa_get_device_features,
	.set_driver_features = mlx5_vdpa_set_driver_features,
	.get_driver_features = mlx5_vdpa_get_driver_features,
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
	.set_group_asid = mlx5_set_group_asid,
	.get_vq_dma_dev = mlx5_get_vq_dma_dev,
	.free = mlx5_vdpa_free,
	.suspend = mlx5_vdpa_suspend,
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

	for (i = 0; i < ndev->mvdev.max_vqs; ++i) {
		mvq = &ndev->vqs[i];
		memset(mvq, 0, offsetof(struct mlx5_vdpa_virtqueue, ri));
		mvq->index = i;
		mvq->ndev = ndev;
		mvq->fwqp.fw = true;
		mvq->fw_state = MLX5_VIRTIO_NET_Q_OBJECT_NONE;
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

static int config_func_mtu(struct mlx5_core_dev *mdev, u16 mtu)
{
	int inlen = MLX5_ST_SZ_BYTES(modify_nic_vport_context_in);
	void *in;
	int err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_nic_vport_context_in, in, field_select.mtu, 1);
	MLX5_SET(modify_nic_vport_context_in, in, nic_vport_context.mtu,
		 mtu + MLX5V_ETH_HARD_MTU);
	MLX5_SET(modify_nic_vport_context_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_NIC_VPORT_CONTEXT);

	err = mlx5_cmd_exec_in(mdev, modify_nic_vport_context, in);

	kvfree(in);
	return err;
}

static void allocate_irqs(struct mlx5_vdpa_net *ndev)
{
	struct mlx5_vdpa_irq_pool_entry *ent;
	int i;

	if (!msix_mode_supported(&ndev->mvdev))
		return;

	if (!ndev->mvdev.mdev->pdev)
		return;

	ndev->irqp.entries = kcalloc(ndev->mvdev.max_vqs, sizeof(*ndev->irqp.entries), GFP_KERNEL);
	if (!ndev->irqp.entries)
		return;


	for (i = 0; i < ndev->mvdev.max_vqs; i++) {
		ent = ndev->irqp.entries + i;
		snprintf(ent->name, MLX5_VDPA_IRQ_NAME_LEN, "%s-vq-%d",
			 dev_name(&ndev->mvdev.vdev.dev), i);
		ent->map = pci_msix_alloc_irq_at(ndev->mvdev.mdev->pdev, MSI_ANY_INDEX, NULL);
		if (!ent->map.virq)
			return;

		ndev->irqp.num_ent++;
	}
}

static int mlx5_vdpa_dev_add(struct vdpa_mgmt_dev *v_mdev, const char *name,
			     const struct vdpa_dev_set_config *add_config)
{
	struct mlx5_vdpa_mgmtdev *mgtdev = container_of(v_mdev, struct mlx5_vdpa_mgmtdev, mgtdev);
	struct virtio_net_config *config;
	struct mlx5_core_dev *pfmdev;
	struct mlx5_vdpa_dev *mvdev;
	struct mlx5_vdpa_net *ndev;
	struct mlx5_core_dev *mdev;
	u64 device_features;
	u32 max_vqs;
	u16 mtu;
	int err;

	if (mgtdev->ndev)
		return -ENOSPC;

	mdev = mgtdev->madev->mdev;
	device_features = mgtdev->mgtdev.supported_features;
	if (add_config->mask & BIT_ULL(VDPA_ATTR_DEV_FEATURES)) {
		if (add_config->device_features & ~device_features) {
			dev_warn(mdev->device,
				 "The provisioned features 0x%llx are not supported by this device with features 0x%llx\n",
				 add_config->device_features, device_features);
			return -EINVAL;
		}
		device_features &= add_config->device_features;
	} else {
		device_features &= ~BIT_ULL(VIRTIO_NET_F_MRG_RXBUF);
	}
	if (!(device_features & BIT_ULL(VIRTIO_F_VERSION_1) &&
	      device_features & BIT_ULL(VIRTIO_F_ACCESS_PLATFORM))) {
		dev_warn(mdev->device,
			 "Must provision minimum features 0x%llx for this device",
			 BIT_ULL(VIRTIO_F_VERSION_1) | BIT_ULL(VIRTIO_F_ACCESS_PLATFORM));
		return -EOPNOTSUPP;
	}

	if (!(MLX5_CAP_DEV_VDPA_EMULATION(mdev, virtio_queue_type) &
	    MLX5_VIRTIO_EMULATION_CAP_VIRTIO_QUEUE_TYPE_SPLIT)) {
		dev_warn(mdev->device, "missing support for split virtqueues\n");
		return -EOPNOTSUPP;
	}

	max_vqs = min_t(int, MLX5_CAP_DEV_VDPA_EMULATION(mdev, max_num_virtio_queues),
			1 << MLX5_CAP_GEN(mdev, log_max_rqt_size));
	if (max_vqs < 2) {
		dev_warn(mdev->device,
			 "%d virtqueues are supported. At least 2 are required\n",
			 max_vqs);
		return -EAGAIN;
	}

	if (add_config->mask & BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MAX_VQP)) {
		if (add_config->net.max_vq_pairs > max_vqs / 2)
			return -EINVAL;
		max_vqs = min_t(u32, max_vqs, 2 * add_config->net.max_vq_pairs);
	} else {
		max_vqs = 2;
	}

	ndev = vdpa_alloc_device(struct mlx5_vdpa_net, mvdev.vdev, mdev->device, &mlx5_vdpa_ops,
				 MLX5_VDPA_NUMVQ_GROUPS, MLX5_VDPA_NUM_AS, name, false);
	if (IS_ERR(ndev))
		return PTR_ERR(ndev);

	ndev->mvdev.max_vqs = max_vqs;
	mvdev = &ndev->mvdev;
	mvdev->mdev = mdev;

	ndev->vqs = kcalloc(max_vqs, sizeof(*ndev->vqs), GFP_KERNEL);
	ndev->event_cbs = kcalloc(max_vqs + 1, sizeof(*ndev->event_cbs), GFP_KERNEL);
	if (!ndev->vqs || !ndev->event_cbs) {
		err = -ENOMEM;
		goto err_alloc;
	}

	init_mvqs(ndev);
	allocate_irqs(ndev);
	init_rwsem(&ndev->reslock);
	config = &ndev->config;

	if (add_config->mask & BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MTU)) {
		err = config_func_mtu(mdev, add_config->net.mtu);
		if (err)
			goto err_alloc;
	}

	if (device_features & BIT_ULL(VIRTIO_NET_F_MTU)) {
		err = query_mtu(mdev, &mtu);
		if (err)
			goto err_alloc;

		ndev->config.mtu = cpu_to_mlx5vdpa16(mvdev, mtu);
	}

	if (device_features & BIT_ULL(VIRTIO_NET_F_STATUS)) {
		if (get_link_state(mvdev))
			ndev->config.status |= cpu_to_mlx5vdpa16(mvdev, VIRTIO_NET_S_LINK_UP);
		else
			ndev->config.status &= cpu_to_mlx5vdpa16(mvdev, ~VIRTIO_NET_S_LINK_UP);
	}

	if (add_config->mask & (1 << VDPA_ATTR_DEV_NET_CFG_MACADDR)) {
		memcpy(ndev->config.mac, add_config->net.mac, ETH_ALEN);
	/* No bother setting mac address in config if not going to provision _F_MAC */
	} else if ((add_config->mask & BIT_ULL(VDPA_ATTR_DEV_FEATURES)) == 0 ||
		   device_features & BIT_ULL(VIRTIO_NET_F_MAC)) {
		err = mlx5_query_nic_vport_mac_address(mdev, 0, 0, config->mac);
		if (err)
			goto err_alloc;
	}

	if (!is_zero_ether_addr(config->mac)) {
		pfmdev = pci_get_drvdata(pci_physfn(mdev->pdev));
		err = mlx5_mpfs_add_mac(pfmdev, config->mac);
		if (err)
			goto err_alloc;
	} else if ((add_config->mask & BIT_ULL(VDPA_ATTR_DEV_FEATURES)) == 0) {
		/*
		 * We used to clear _F_MAC feature bit if seeing
		 * zero mac address when device features are not
		 * specifically provisioned. Keep the behaviour
		 * so old scripts do not break.
		 */
		device_features &= ~BIT_ULL(VIRTIO_NET_F_MAC);
	} else if (device_features & BIT_ULL(VIRTIO_NET_F_MAC)) {
		/* Don't provision zero mac address for _F_MAC */
		mlx5_vdpa_warn(&ndev->mvdev,
			       "No mac address provisioned?\n");
		err = -EINVAL;
		goto err_alloc;
	}

	if (device_features & BIT_ULL(VIRTIO_NET_F_MQ))
		config->max_virtqueue_pairs = cpu_to_mlx5vdpa16(mvdev, max_vqs / 2);

	ndev->mvdev.mlx_features = device_features;
	mvdev->vdev.dma_dev = &mdev->pdev->dev;
	err = mlx5_vdpa_alloc_resources(&ndev->mvdev);
	if (err)
		goto err_mpfs;

	if (MLX5_CAP_GEN(mvdev->mdev, umem_uid_0)) {
		err = mlx5_vdpa_create_mr(mvdev, NULL, 0);
		if (err)
			goto err_res;
	}

	err = alloc_resources(ndev);
	if (err)
		goto err_mr;

	ndev->cvq_ent.mvdev = mvdev;
	INIT_WORK(&ndev->cvq_ent.work, mlx5_cvq_kick_handler);
	mvdev->wq = create_singlethread_workqueue("mlx5_vdpa_wq");
	if (!mvdev->wq) {
		err = -ENOMEM;
		goto err_res2;
	}

	mvdev->vdev.mdev = &mgtdev->mgtdev;
	err = _vdpa_register_device(&mvdev->vdev, max_vqs + 1);
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
err_alloc:
	put_device(&mvdev->vdev.dev);
	return err;
}

static void mlx5_vdpa_dev_del(struct vdpa_mgmt_dev *v_mdev, struct vdpa_device *dev)
{
	struct mlx5_vdpa_mgmtdev *mgtdev = container_of(v_mdev, struct mlx5_vdpa_mgmtdev, mgtdev);
	struct mlx5_vdpa_dev *mvdev = to_mvdev(dev);
	struct mlx5_vdpa_net *ndev = to_mlx5_vdpa_ndev(mvdev);
	struct workqueue_struct *wq;

	mlx5_vdpa_remove_debugfs(ndev->debugfs);
	ndev->debugfs = NULL;
	unregister_link_notifier(ndev);
	_vdpa_unregister_device(dev);
	wq = mvdev->wq;
	mvdev->wq = NULL;
	destroy_workqueue(wq);
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
	mgtdev->mgtdev.config_attr_mask = BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MACADDR) |
					  BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MAX_VQP) |
					  BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MTU) |
					  BIT_ULL(VDPA_ATTR_DEV_FEATURES);
	mgtdev->mgtdev.max_supported_vqs =
		MLX5_CAP_DEV_VDPA_EMULATION(mdev, max_num_virtio_queues) + 1;
	mgtdev->mgtdev.supported_features = get_supported_features(mdev);
	mgtdev->madev = madev;

	err = vdpa_mgmtdev_register(&mgtdev->mgtdev);
	if (err)
		goto reg_err;

	auxiliary_set_drvdata(adev, mgtdev);

	return 0;

reg_err:
	kfree(mgtdev);
	return err;
}

static void mlx5v_remove(struct auxiliary_device *adev)
{
	struct mlx5_vdpa_mgmtdev *mgtdev;

	mgtdev = auxiliary_get_drvdata(adev);
	vdpa_mgmtdev_unregister(&mgtdev->mgtdev);
	kfree(mgtdev);
}

static void mlx5v_shutdown(struct auxiliary_device *auxdev)
{
	struct mlx5_vdpa_mgmtdev *mgtdev;
	struct mlx5_vdpa_net *ndev;

	mgtdev = auxiliary_get_drvdata(auxdev);
	ndev = mgtdev->ndev;

	free_irqs(ndev);
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
	.shutdown = mlx5v_shutdown,
	.id_table = mlx5v_id_table,
};

module_auxiliary_driver(mlx5v_driver);

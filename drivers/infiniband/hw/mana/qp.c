// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Microsoft Corporation. All rights reserved.
 */

#include "mana_ib.h"

static int mana_ib_cfg_vport_steering(struct mana_ib_dev *dev,
				      struct net_device *ndev,
				      mana_handle_t default_rxobj,
				      mana_handle_t ind_table[],
				      u32 log_ind_tbl_size, u32 rx_hash_key_len,
				      u8 *rx_hash_key)
{
	struct mana_port_context *mpc = netdev_priv(ndev);
	struct mana_cfg_rx_steer_req_v2 *req;
	struct mana_cfg_rx_steer_resp resp = {};
	mana_handle_t *req_indir_tab;
	struct gdma_context *gc;
	u32 req_buf_size;
	int i, err;

	gc = mdev_to_gc(dev);

	req_buf_size =
		sizeof(*req) + sizeof(mana_handle_t) * MANA_INDIRECT_TABLE_SIZE;
	req = kzalloc(req_buf_size, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	mana_gd_init_req_hdr(&req->hdr, MANA_CONFIG_VPORT_RX, req_buf_size,
			     sizeof(resp));

	req->hdr.req.msg_version = GDMA_MESSAGE_V2;

	req->vport = mpc->port_handle;
	req->rx_enable = 1;
	req->update_default_rxobj = 1;
	req->default_rxobj = default_rxobj;
	req->hdr.dev_id = gc->mana.dev_id;

	/* If there are more than 1 entries in indirection table, enable RSS */
	if (log_ind_tbl_size)
		req->rss_enable = true;

	req->num_indir_entries = MANA_INDIRECT_TABLE_SIZE;
	req->indir_tab_offset = sizeof(*req);
	req->update_indir_tab = true;
	req->cqe_coalescing_enable = 1;

	req_indir_tab = (mana_handle_t *)(req + 1);
	/* The ind table passed to the hardware must have
	 * MANA_INDIRECT_TABLE_SIZE entries. Adjust the verb
	 * ind_table to MANA_INDIRECT_TABLE_SIZE if required
	 */
	ibdev_dbg(&dev->ib_dev, "ind table size %u\n", 1 << log_ind_tbl_size);
	for (i = 0; i < MANA_INDIRECT_TABLE_SIZE; i++) {
		req_indir_tab[i] = ind_table[i % (1 << log_ind_tbl_size)];
		ibdev_dbg(&dev->ib_dev, "index %u handle 0x%llx\n", i,
			  req_indir_tab[i]);
	}

	req->update_hashkey = true;
	if (rx_hash_key_len)
		memcpy(req->hashkey, rx_hash_key, rx_hash_key_len);
	else
		netdev_rss_key_fill(req->hashkey, MANA_HASH_KEY_SIZE);

	ibdev_dbg(&dev->ib_dev, "vport handle %llu default_rxobj 0x%llx\n",
		  req->vport, default_rxobj);

	err = mana_gd_send_request(gc, req_buf_size, req, sizeof(resp), &resp);
	if (err) {
		netdev_err(ndev, "Failed to configure vPort RX: %d\n", err);
		goto out;
	}

	if (resp.hdr.status) {
		netdev_err(ndev, "vPort RX configuration failed: 0x%x\n",
			   resp.hdr.status);
		err = -EPROTO;
		goto out;
	}

	netdev_info(ndev, "Configured steering vPort %llu log_entries %u\n",
		    mpc->port_handle, log_ind_tbl_size);

out:
	kfree(req);
	return err;
}

static int mana_ib_create_qp_rss(struct ib_qp *ibqp, struct ib_pd *pd,
				 struct ib_qp_init_attr *attr,
				 struct ib_udata *udata)
{
	struct mana_ib_qp *qp = container_of(ibqp, struct mana_ib_qp, ibqp);
	struct mana_ib_dev *mdev =
		container_of(pd->device, struct mana_ib_dev, ib_dev);
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct ib_rwq_ind_table *ind_tbl = attr->rwq_ind_tbl;
	struct mana_ib_create_qp_rss_resp resp = {};
	struct mana_ib_create_qp_rss ucmd = {};
	struct gdma_queue **gdma_cq_allocated;
	mana_handle_t *mana_ind_table;
	struct mana_port_context *mpc;
	unsigned int ind_tbl_size;
	struct net_device *ndev;
	struct mana_ib_cq *cq;
	struct mana_ib_wq *wq;
	struct mana_eq *eq;
	struct ib_cq *ibcq;
	struct ib_wq *ibwq;
	int i = 0;
	u32 port;
	int ret;

	if (!udata || udata->inlen < sizeof(ucmd))
		return -EINVAL;

	ret = ib_copy_from_udata(&ucmd, udata, min(sizeof(ucmd), udata->inlen));
	if (ret) {
		ibdev_dbg(&mdev->ib_dev,
			  "Failed copy from udata for create rss-qp, err %d\n",
			  ret);
		return ret;
	}

	if (attr->cap.max_recv_wr > mdev->adapter_caps.max_qp_wr) {
		ibdev_dbg(&mdev->ib_dev,
			  "Requested max_recv_wr %d exceeding limit\n",
			  attr->cap.max_recv_wr);
		return -EINVAL;
	}

	if (attr->cap.max_recv_sge > MAX_RX_WQE_SGL_ENTRIES) {
		ibdev_dbg(&mdev->ib_dev,
			  "Requested max_recv_sge %d exceeding limit\n",
			  attr->cap.max_recv_sge);
		return -EINVAL;
	}

	ind_tbl_size = 1 << ind_tbl->log_ind_tbl_size;
	if (ind_tbl_size > MANA_INDIRECT_TABLE_SIZE) {
		ibdev_dbg(&mdev->ib_dev,
			  "Indirect table size %d exceeding limit\n",
			  ind_tbl_size);
		return -EINVAL;
	}

	if (ucmd.rx_hash_function != MANA_IB_RX_HASH_FUNC_TOEPLITZ) {
		ibdev_dbg(&mdev->ib_dev,
			  "RX Hash function is not supported, %d\n",
			  ucmd.rx_hash_function);
		return -EINVAL;
	}

	/* IB ports start with 1, MANA start with 0 */
	port = ucmd.port;
	ndev = mana_ib_get_netdev(pd->device, port);
	if (!ndev) {
		ibdev_dbg(&mdev->ib_dev, "Invalid port %u in creating qp\n",
			  port);
		return -EINVAL;
	}
	mpc = netdev_priv(ndev);

	ibdev_dbg(&mdev->ib_dev, "rx_hash_function %d port %d\n",
		  ucmd.rx_hash_function, port);

	mana_ind_table = kcalloc(ind_tbl_size, sizeof(mana_handle_t),
				 GFP_KERNEL);
	if (!mana_ind_table) {
		ret = -ENOMEM;
		goto fail;
	}

	gdma_cq_allocated = kcalloc(ind_tbl_size, sizeof(*gdma_cq_allocated),
				    GFP_KERNEL);
	if (!gdma_cq_allocated) {
		ret = -ENOMEM;
		goto fail;
	}

	qp->port = port;

	for (i = 0; i < ind_tbl_size; i++) {
		struct mana_obj_spec wq_spec = {};
		struct mana_obj_spec cq_spec = {};

		ibwq = ind_tbl->ind_tbl[i];
		wq = container_of(ibwq, struct mana_ib_wq, ibwq);

		ibcq = ibwq->cq;
		cq = container_of(ibcq, struct mana_ib_cq, ibcq);

		wq_spec.gdma_region = wq->queue.gdma_region;
		wq_spec.queue_size = wq->wq_buf_size;

		cq_spec.gdma_region = cq->queue.gdma_region;
		cq_spec.queue_size = cq->cqe * COMP_ENTRY_SIZE;
		cq_spec.modr_ctx_id = 0;
		eq = &mpc->ac->eqs[cq->comp_vector % gc->max_num_queues];
		cq_spec.attached_eq = eq->eq->id;

		ret = mana_create_wq_obj(mpc, mpc->port_handle, GDMA_RQ,
					 &wq_spec, &cq_spec, &wq->rx_object);
		if (ret) {
			/* Do cleanup starting with index i-1 */
			i--;
			goto fail;
		}

		/* The GDMA regions are now owned by the WQ object */
		wq->queue.gdma_region = GDMA_INVALID_DMA_REGION;
		cq->queue.gdma_region = GDMA_INVALID_DMA_REGION;

		wq->queue.id = wq_spec.queue_index;
		cq->queue.id = cq_spec.queue_index;

		ibdev_dbg(&mdev->ib_dev,
			  "ret %d rx_object 0x%llx wq id %llu cq id %llu\n",
			  ret, wq->rx_object, wq->queue.id, cq->queue.id);

		resp.entries[i].cqid = cq->queue.id;
		resp.entries[i].wqid = wq->queue.id;

		mana_ind_table[i] = wq->rx_object;

		/* Create CQ table entry */
		ret = mana_ib_install_cq_cb(mdev, cq);
		if (ret)
			goto fail;

		gdma_cq_allocated[i] = gc->cq_table[cq->queue.id];
	}
	resp.num_entries = i;

	ret = mana_ib_cfg_vport_steering(mdev, ndev, wq->rx_object,
					 mana_ind_table,
					 ind_tbl->log_ind_tbl_size,
					 ucmd.rx_hash_key_len,
					 ucmd.rx_hash_key);
	if (ret)
		goto fail;

	ret = ib_copy_to_udata(udata, &resp, sizeof(resp));
	if (ret) {
		ibdev_dbg(&mdev->ib_dev,
			  "Failed to copy to udata create rss-qp, %d\n",
			  ret);
		goto fail;
	}

	kfree(gdma_cq_allocated);
	kfree(mana_ind_table);

	return 0;

fail:
	while (i-- > 0) {
		ibwq = ind_tbl->ind_tbl[i];
		ibcq = ibwq->cq;
		wq = container_of(ibwq, struct mana_ib_wq, ibwq);
		cq = container_of(ibcq, struct mana_ib_cq, ibcq);

		gc->cq_table[cq->queue.id] = NULL;
		kfree(gdma_cq_allocated[i]);

		mana_destroy_wq_obj(mpc, GDMA_RQ, wq->rx_object);
	}

	kfree(gdma_cq_allocated);
	kfree(mana_ind_table);

	return ret;
}

static int mana_ib_create_qp_raw(struct ib_qp *ibqp, struct ib_pd *ibpd,
				 struct ib_qp_init_attr *attr,
				 struct ib_udata *udata)
{
	struct mana_ib_pd *pd = container_of(ibpd, struct mana_ib_pd, ibpd);
	struct mana_ib_qp *qp = container_of(ibqp, struct mana_ib_qp, ibqp);
	struct mana_ib_dev *mdev =
		container_of(ibpd->device, struct mana_ib_dev, ib_dev);
	struct mana_ib_cq *send_cq =
		container_of(attr->send_cq, struct mana_ib_cq, ibcq);
	struct mana_ib_ucontext *mana_ucontext =
		rdma_udata_to_drv_context(udata, struct mana_ib_ucontext,
					  ibucontext);
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct mana_ib_create_qp_resp resp = {};
	struct mana_ib_create_qp ucmd = {};
	struct gdma_queue *gdma_cq = NULL;
	struct mana_obj_spec wq_spec = {};
	struct mana_obj_spec cq_spec = {};
	struct mana_port_context *mpc;
	struct net_device *ndev;
	struct ib_umem *umem;
	struct mana_eq *eq;
	int eq_vec;
	u32 port;
	int err;

	if (!mana_ucontext || udata->inlen < sizeof(ucmd))
		return -EINVAL;

	err = ib_copy_from_udata(&ucmd, udata, min(sizeof(ucmd), udata->inlen));
	if (err) {
		ibdev_dbg(&mdev->ib_dev,
			  "Failed to copy from udata create qp-raw, %d\n", err);
		return err;
	}

	if (attr->cap.max_send_wr > mdev->adapter_caps.max_qp_wr) {
		ibdev_dbg(&mdev->ib_dev,
			  "Requested max_send_wr %d exceeding limit\n",
			  attr->cap.max_send_wr);
		return -EINVAL;
	}

	if (attr->cap.max_send_sge > MAX_TX_WQE_SGL_ENTRIES) {
		ibdev_dbg(&mdev->ib_dev,
			  "Requested max_send_sge %d exceeding limit\n",
			  attr->cap.max_send_sge);
		return -EINVAL;
	}

	port = ucmd.port;
	ndev = mana_ib_get_netdev(ibpd->device, port);
	if (!ndev) {
		ibdev_dbg(&mdev->ib_dev, "Invalid port %u in creating qp\n",
			  port);
		return -EINVAL;
	}
	mpc = netdev_priv(ndev);
	ibdev_dbg(&mdev->ib_dev, "port %u ndev %p mpc %p\n", port, ndev, mpc);

	err = mana_ib_cfg_vport(mdev, port, pd, mana_ucontext->doorbell);
	if (err)
		return -ENODEV;

	qp->port = port;

	ibdev_dbg(&mdev->ib_dev, "ucmd sq_buf_addr 0x%llx port %u\n",
		  ucmd.sq_buf_addr, ucmd.port);

	umem = ib_umem_get(ibpd->device, ucmd.sq_buf_addr, ucmd.sq_buf_size,
			   IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(umem)) {
		err = PTR_ERR(umem);
		ibdev_dbg(&mdev->ib_dev,
			  "Failed to get umem for create qp-raw, err %d\n",
			  err);
		goto err_free_vport;
	}
	qp->sq_umem = umem;

	err = mana_ib_create_zero_offset_dma_region(mdev, qp->sq_umem,
						    &qp->sq_gdma_region);
	if (err) {
		ibdev_dbg(&mdev->ib_dev,
			  "Failed to create dma region for create qp-raw, %d\n",
			  err);
		goto err_release_umem;
	}

	ibdev_dbg(&mdev->ib_dev,
		  "create_dma_region ret %d gdma_region 0x%llx\n",
		  err, qp->sq_gdma_region);

	/* Create a WQ on the same port handle used by the Ethernet */
	wq_spec.gdma_region = qp->sq_gdma_region;
	wq_spec.queue_size = ucmd.sq_buf_size;

	cq_spec.gdma_region = send_cq->queue.gdma_region;
	cq_spec.queue_size = send_cq->cqe * COMP_ENTRY_SIZE;
	cq_spec.modr_ctx_id = 0;
	eq_vec = send_cq->comp_vector % gc->max_num_queues;
	eq = &mpc->ac->eqs[eq_vec];
	cq_spec.attached_eq = eq->eq->id;

	err = mana_create_wq_obj(mpc, mpc->port_handle, GDMA_SQ, &wq_spec,
				 &cq_spec, &qp->tx_object);
	if (err) {
		ibdev_dbg(&mdev->ib_dev,
			  "Failed to create wq for create raw-qp, err %d\n",
			  err);
		goto err_destroy_dma_region;
	}

	/* The GDMA regions are now owned by the WQ object */
	qp->sq_gdma_region = GDMA_INVALID_DMA_REGION;
	send_cq->queue.gdma_region = GDMA_INVALID_DMA_REGION;

	qp->sq_id = wq_spec.queue_index;
	send_cq->queue.id = cq_spec.queue_index;

	/* Create CQ table entry */
	err = mana_ib_install_cq_cb(mdev, send_cq);
	if (err)
		goto err_destroy_wq_obj;

	ibdev_dbg(&mdev->ib_dev,
		  "ret %d qp->tx_object 0x%llx sq id %llu cq id %llu\n", err,
		  qp->tx_object, qp->sq_id, send_cq->queue.id);

	resp.sqid = qp->sq_id;
	resp.cqid = send_cq->queue.id;
	resp.tx_vp_offset = pd->tx_vp_offset;

	err = ib_copy_to_udata(udata, &resp, sizeof(resp));
	if (err) {
		ibdev_dbg(&mdev->ib_dev,
			  "Failed copy udata for create qp-raw, %d\n",
			  err);
		goto err_release_gdma_cq;
	}

	return 0;

err_release_gdma_cq:
	kfree(gdma_cq);
	gc->cq_table[send_cq->queue.id] = NULL;

err_destroy_wq_obj:
	mana_destroy_wq_obj(mpc, GDMA_SQ, qp->tx_object);

err_destroy_dma_region:
	mana_ib_gd_destroy_dma_region(mdev, qp->sq_gdma_region);

err_release_umem:
	ib_umem_release(umem);

err_free_vport:
	mana_ib_uncfg_vport(mdev, pd, port);

	return err;
}

int mana_ib_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *attr,
		      struct ib_udata *udata)
{
	switch (attr->qp_type) {
	case IB_QPT_RAW_PACKET:
		/* When rwq_ind_tbl is used, it's for creating WQs for RSS */
		if (attr->rwq_ind_tbl)
			return mana_ib_create_qp_rss(ibqp, ibqp->pd, attr,
						     udata);

		return mana_ib_create_qp_raw(ibqp, ibqp->pd, attr, udata);
	default:
		/* Creating QP other than IB_QPT_RAW_PACKET is not supported */
		ibdev_dbg(ibqp->device, "Creating QP type %u not supported\n",
			  attr->qp_type);
	}

	return -EINVAL;
}

int mana_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask, struct ib_udata *udata)
{
	/* modify_qp is not supported by this version of the driver */
	return -EOPNOTSUPP;
}

static int mana_ib_destroy_qp_rss(struct mana_ib_qp *qp,
				  struct ib_rwq_ind_table *ind_tbl,
				  struct ib_udata *udata)
{
	struct mana_ib_dev *mdev =
		container_of(qp->ibqp.device, struct mana_ib_dev, ib_dev);
	struct mana_port_context *mpc;
	struct net_device *ndev;
	struct mana_ib_wq *wq;
	struct ib_wq *ibwq;
	int i;

	ndev = mana_ib_get_netdev(qp->ibqp.device, qp->port);
	mpc = netdev_priv(ndev);

	for (i = 0; i < (1 << ind_tbl->log_ind_tbl_size); i++) {
		ibwq = ind_tbl->ind_tbl[i];
		wq = container_of(ibwq, struct mana_ib_wq, ibwq);
		ibdev_dbg(&mdev->ib_dev, "destroying wq->rx_object %llu\n",
			  wq->rx_object);
		mana_destroy_wq_obj(mpc, GDMA_RQ, wq->rx_object);
	}

	return 0;
}

static int mana_ib_destroy_qp_raw(struct mana_ib_qp *qp, struct ib_udata *udata)
{
	struct mana_ib_dev *mdev =
		container_of(qp->ibqp.device, struct mana_ib_dev, ib_dev);
	struct ib_pd *ibpd = qp->ibqp.pd;
	struct mana_port_context *mpc;
	struct net_device *ndev;
	struct mana_ib_pd *pd;

	ndev = mana_ib_get_netdev(qp->ibqp.device, qp->port);
	mpc = netdev_priv(ndev);
	pd = container_of(ibpd, struct mana_ib_pd, ibpd);

	mana_destroy_wq_obj(mpc, GDMA_SQ, qp->tx_object);

	if (qp->sq_umem) {
		mana_ib_gd_destroy_dma_region(mdev, qp->sq_gdma_region);
		ib_umem_release(qp->sq_umem);
	}

	mana_ib_uncfg_vport(mdev, pd, qp->port);

	return 0;
}

int mana_ib_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
{
	struct mana_ib_qp *qp = container_of(ibqp, struct mana_ib_qp, ibqp);

	switch (ibqp->qp_type) {
	case IB_QPT_RAW_PACKET:
		if (ibqp->rwq_ind_tbl)
			return mana_ib_destroy_qp_rss(qp, ibqp->rwq_ind_tbl,
						      udata);

		return mana_ib_destroy_qp_raw(qp, udata);

	default:
		ibdev_dbg(ibqp->device, "Unexpected QP type %u\n",
			  ibqp->qp_type);
	}

	return -ENOENT;
}

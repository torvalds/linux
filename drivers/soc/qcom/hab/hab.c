// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "hab.h"

#define CREATE_TRACE_POINTS
#include "hab_trace_os.h"

#define HAB_DEVICE_CNSTR(__name__, __id__, __num__) { \
	.name = __name__,\
	.id = __id__,\
	.pchannels = LIST_HEAD_INIT(hab_devices[__num__].pchannels),\
	.pchan_lock = __RW_LOCK_UNLOCKED(hab_devices[__num__].pchan_lock),\
	.openq_list = LIST_HEAD_INIT(hab_devices[__num__].openq_list),\
	.openlock = __SPIN_LOCK_UNLOCKED(&hab_devices[__num__].openlock)\
	}

static const char hab_info_str[] = "Change: 17280941 Revision: #81";

/*
 * The following has to match habmm definitions, order does not matter if
 * hab config does not care either. When hab config is not present, the default
 * is as guest VM all pchans are pchan opener (FE)
 */
static struct hab_device hab_devices[] = {
	HAB_DEVICE_CNSTR(DEVICE_AUD1_NAME, MM_AUD_1, 0),
	HAB_DEVICE_CNSTR(DEVICE_AUD2_NAME, MM_AUD_2, 1),
	HAB_DEVICE_CNSTR(DEVICE_AUD3_NAME, MM_AUD_3, 2),
	HAB_DEVICE_CNSTR(DEVICE_AUD4_NAME, MM_AUD_4, 3),
	HAB_DEVICE_CNSTR(DEVICE_CAM1_NAME, MM_CAM_1, 4),
	HAB_DEVICE_CNSTR(DEVICE_CAM2_NAME, MM_CAM_2, 5),
	HAB_DEVICE_CNSTR(DEVICE_DISP1_NAME, MM_DISP_1, 6),
	HAB_DEVICE_CNSTR(DEVICE_DISP2_NAME, MM_DISP_2, 7),
	HAB_DEVICE_CNSTR(DEVICE_DISP3_NAME, MM_DISP_3, 8),
	HAB_DEVICE_CNSTR(DEVICE_DISP4_NAME, MM_DISP_4, 9),
	HAB_DEVICE_CNSTR(DEVICE_DISP5_NAME, MM_DISP_5, 10),
	HAB_DEVICE_CNSTR(DEVICE_GFX_NAME, MM_GFX, 11),
	HAB_DEVICE_CNSTR(DEVICE_VID_NAME, MM_VID, 12),
	HAB_DEVICE_CNSTR(DEVICE_VID2_NAME, MM_VID_2, 13),
	HAB_DEVICE_CNSTR(DEVICE_VID3_NAME, MM_VID_3, 14),
	HAB_DEVICE_CNSTR(DEVICE_MISC_NAME, MM_MISC, 15),
	HAB_DEVICE_CNSTR(DEVICE_QCPE1_NAME, MM_QCPE_VM1, 16),
	HAB_DEVICE_CNSTR(DEVICE_CLK1_NAME, MM_CLK_VM1, 17),
	HAB_DEVICE_CNSTR(DEVICE_CLK2_NAME, MM_CLK_VM2, 18),
	HAB_DEVICE_CNSTR(DEVICE_FDE1_NAME, MM_FDE_1, 19),
	HAB_DEVICE_CNSTR(DEVICE_BUFFERQ1_NAME, MM_BUFFERQ_1, 20),
	HAB_DEVICE_CNSTR(DEVICE_DATA1_NAME, MM_DATA_NETWORK_1, 21),
	HAB_DEVICE_CNSTR(DEVICE_DATA2_NAME, MM_DATA_NETWORK_2, 22),
	HAB_DEVICE_CNSTR(DEVICE_HSI2S1_NAME, MM_HSI2S_1, 23),
	HAB_DEVICE_CNSTR(DEVICE_XVM1_NAME, MM_XVM_1, 24),
	HAB_DEVICE_CNSTR(DEVICE_XVM2_NAME, MM_XVM_2, 25),
	HAB_DEVICE_CNSTR(DEVICE_XVM3_NAME, MM_XVM_3, 26),
	HAB_DEVICE_CNSTR(DEVICE_VNW1_NAME, MM_VNW_1, 27),
	HAB_DEVICE_CNSTR(DEVICE_EXT1_NAME, MM_EXT_1, 28),
	HAB_DEVICE_CNSTR(DEVICE_GPCE1_NAME, MM_GPCE_1, 29),
};

struct hab_driver hab_driver = {
	.ndevices = ARRAY_SIZE(hab_devices),
	.devp = hab_devices,
	.uctx_list = LIST_HEAD_INIT(hab_driver.uctx_list),
	.drvlock = __SPIN_LOCK_UNLOCKED(hab_driver.drvlock),
	.imp_list = LIST_HEAD_INIT(hab_driver.imp_list),
	.imp_lock = __SPIN_LOCK_UNLOCKED(hab_driver.imp_lock),
	.hab_init_success = 0,
	.reclaim_list = LIST_HEAD_INIT(hab_driver.reclaim_list),
	.reclaim_lock = __SPIN_LOCK_UNLOCKED(hab_driver.reclaim_lock),
};

struct uhab_context *hab_ctx_alloc(int kernel)
{
	struct uhab_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->closing = 0;
	INIT_LIST_HEAD(&ctx->vchannels);
	INIT_LIST_HEAD(&ctx->exp_whse);
	hab_rb_init(&ctx->imp_whse);

	INIT_LIST_HEAD(&ctx->exp_rxq);
	init_waitqueue_head(&ctx->exp_wq);
	spin_lock_init(&ctx->expq_lock);

	INIT_LIST_HEAD(&ctx->imp_rxq);
	init_waitqueue_head(&ctx->imp_wq);
	spin_lock_init(&ctx->impq_lock);

	spin_lock_init(&ctx->imp_lock);
	rwlock_init(&ctx->exp_lock);
	rwlock_init(&ctx->ctx_lock);

	INIT_LIST_HEAD(&ctx->pending_open);
	kref_init(&ctx->refcount);
	ctx->import_ctx = habmem_imp_hyp_open();
	if (!ctx->import_ctx) {
		pr_err("habmem_imp_hyp_open failed\n");
		kfree(ctx);
		return NULL;
	}
	ctx->kernel = kernel;

	spin_lock_bh(&hab_driver.drvlock);
	list_add_tail(&ctx->node, &hab_driver.uctx_list);
	hab_driver.ctx_cnt++;
	ctx->lb_be = hab_driver.b_loopback_be; /* loopback only */
	hab_driver.b_loopback_be = ~hab_driver.b_loopback_be; /* loopback only*/
	spin_unlock_bh(&hab_driver.drvlock);
	pr_debug("ctx %pK live %d loopback be %d\n",
		ctx, hab_driver.ctx_cnt, ctx->lb_be);

	return ctx;
}

/*
 * This function might sleep. One scenario (only applicable for Linux)
 * is as below, hab_ctx_free_fn->habmem_remove_export->habmem_export_put
 * ->habmem_export_destroy->habmem_exp_release,
 * where dma_buf_unmap_attachment() & dma_buf_detach() might sleep.
 */
void hab_ctx_free_fn(struct uhab_context *ctx)
{
	struct hab_export_ack_recvd *exp_ack_recvd, *expack_tmp;
	struct hab_import_ack_recvd *imp_ack_recvd, *impack_tmp;
	struct virtual_channel *vchan;
	struct physical_channel *pchan;
	int i;
	struct uhab_context *ctxdel, *ctxtmp;
	struct hab_open_node *open_node;
	struct export_desc *exp = NULL, *exp_tmp = NULL;
	struct export_desc_super *exp_super = NULL;
	int irqs_disabled = irqs_disabled();
	struct hab_header header = HAB_HEADER_INITIALIZER;
	int ret;

	/* garbage-collect exp/imp buffers */
	write_lock(&ctx->exp_lock);
	list_for_each_entry_safe(exp, exp_tmp, &ctx->exp_whse, node) {
		list_del(&exp->node);
		exp_super = container_of(exp, struct export_desc_super, exp);
		if ((exp_super->remote_imported != 0) && (exp->pchan->mem_proto == 1)) {
			pr_warn("exp id %d still imported on remote side on %s, pcnt %d\n",
				exp->export_id, exp->pchan->name, exp->payload_count);
			hab_spin_lock(&hab_driver.reclaim_lock, irqs_disabled);
			list_add_tail(&exp->node, &hab_driver.reclaim_list);
			hab_spin_unlock(&hab_driver.reclaim_lock, irqs_disabled);
			schedule_work(&hab_driver.reclaim_work);
		} else {
			pr_debug("potential leak exp %d vcid %X recovered\n",
					exp->export_id, exp->vcid_local);
			habmem_hyp_revoke(exp->payload, exp->payload_count);
			write_unlock(&ctx->exp_lock);

			pchan = exp->pchan;
			hab_spin_lock(&pchan->expid_lock, irqs_disabled);
			idr_remove(&pchan->expid_idr, exp->export_id);
			hab_spin_unlock(&pchan->expid_lock, irqs_disabled);

			habmem_remove_export(exp);
			write_lock(&ctx->exp_lock);
		}
	}
	write_unlock(&ctx->exp_lock);

	spin_lock_bh(&ctx->imp_lock);
	for (exp_super = hab_rb_min(&ctx->imp_whse, struct export_desc_super, node);
	     exp_super != NULL;
	     exp_super = hab_rb_min(&ctx->imp_whse, struct export_desc_super, node)) {
		exp = &exp_super->exp;
		hab_rb_remove(&ctx->imp_whse, exp_super);
		ctx->import_total--;
		pr_debug("leaked imp %d vcid %X for ctx is collected total %d\n",
			exp->export_id, exp->vcid_local,
			ctx->import_total);
		ret = habmm_imp_hyp_unmap(ctx->import_ctx, exp, ctx->kernel);
		if (exp->pchan->mem_proto == 1) {
			if (!ret) {
				pr_warn("unimp msg sent for exp id %u on %s\n",
					exp->export_id, exp->pchan->name);
				HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_UNIMPORT);
				HAB_HEADER_SET_SIZE(header, sizeof(uint32_t));
				HAB_HEADER_SET_ID(header, HAB_VCID_UNIMPORT);
				HAB_HEADER_SET_SESSION_ID(header, HAB_SESSIONID_UNIMPORT);
				ret = physical_channel_send(exp->pchan, &header, &exp->export_id,
						HABMM_SOCKET_SEND_FLAGS_NON_BLOCKING);
				if (ret != 0)
					pr_err("failed to send unimp msg %d, vcid %d, exp id %d\n",
						ret, exp->vcid_local, exp->export_id);
			} else
				pr_err("exp id %d pcnt %d unmap fail on vcid %X\n",
					exp->export_id, exp->payload_count, exp->vcid_local);
		}
		exp_super = container_of(exp, struct export_desc_super, exp);
		kfree(exp_super);
	}
	spin_unlock_bh(&ctx->imp_lock);

	habmem_imp_hyp_close(ctx->import_ctx, ctx->kernel);

	/*
	 * Below rxq only used when vchan is alive. At this moment, it is safe without
	 * holding lock as all vchans in this ctx have been freed.
	 * Only one of the rx queues is used decided by the mem protocol. It cannot be
	 * queried from pchan gracefully if above two warehouses are empty.
	 * So both queues are always checked to decrease the code complexity.
	 */
	list_for_each_entry_safe(imp_ack_recvd, impack_tmp, &ctx->imp_rxq, node) {
		list_del(&imp_ack_recvd->node);
		kfree(imp_ack_recvd);
	}

	list_for_each_entry_safe(exp_ack_recvd, expack_tmp, &ctx->exp_rxq, node) {
		list_del(&exp_ack_recvd->node);
		kfree(exp_ack_recvd);
	}

	/* walk vchan list to find the leakage */
	spin_lock_bh(&hab_driver.drvlock);
	hab_driver.ctx_cnt--;
	list_for_each_entry_safe(ctxdel, ctxtmp, &hab_driver.uctx_list, node) {
		if (ctxdel == ctx)
			list_del(&ctxdel->node);
	}
	spin_unlock_bh(&hab_driver.drvlock);
	pr_debug("live ctx %d refcnt %d kernel %d close %d owner %d\n",
			hab_driver.ctx_cnt, get_refcnt(ctx->refcount),
			ctx->kernel, ctx->closing, ctx->owner);

	/* check vchans in this ctx */
	read_lock(&ctx->ctx_lock);
	list_for_each_entry(vchan, &ctx->vchannels, node) {
		pr_warn("leak vchan id %X cnt %X remote %d in ctx\n",
				vchan->id, get_refcnt(vchan->refcount),
				vchan->otherend_id);
	}
	read_unlock(&ctx->ctx_lock);

	/* check pending open */
	if (ctx->pending_cnt)
		pr_warn("potential leak of pendin_open nodes %d\n",
			ctx->pending_cnt);

	read_lock(&ctx->ctx_lock);
	list_for_each_entry(open_node, &ctx->pending_open, node) {
		pr_warn("leak pending open vcid %X type %d subid %d openid %d\n",
			open_node->request.xdata.vchan_id,
			open_node->request.type,
			open_node->request.xdata.sub_id,
			open_node->request.xdata.open_id);
	}
	read_unlock(&ctx->ctx_lock);

	/* check vchans belong to this ctx in all hab/mmid devices */
	for (i = 0; i < hab_driver.ndevices; i++) {
		struct hab_device *habdev = &hab_driver.devp[i];

		read_lock_bh(&habdev->pchan_lock);
		list_for_each_entry(pchan, &habdev->pchannels, node) {

			/* check vchan ctx owner */
			read_lock(&pchan->vchans_lock);
			list_for_each_entry(vchan, &pchan->vchannels, pnode) {
				if (vchan->ctx == ctx) {
					pr_warn("leak vcid %X cnt %d pchan %s local %d remote %d\n",
						vchan->id,
						get_refcnt(vchan->refcount),
						pchan->name, pchan->vmid_local,
						pchan->vmid_remote);
				}
			}
			read_unlock(&pchan->vchans_lock);
		}
		read_unlock_bh(&habdev->pchan_lock);
	}
	kfree(ctx);
}

void hab_ctx_free(struct kref *ref)
{
	hab_ctx_free_os(ref);
}

/*
 * caller needs to call vchan_put() afterwards. this is used to refcnt
 * the local ioctl access based on ctx
 */
struct virtual_channel *hab_get_vchan_fromvcid(int32_t vcid,
		struct uhab_context *ctx, int ignore_remote)
{
	struct virtual_channel *vchan;

	read_lock(&ctx->ctx_lock);
	list_for_each_entry(vchan, &ctx->vchannels, node) {
		if (vcid == vchan->id) {
			if ((ignore_remote ? 0 : vchan->otherend_closed) ||
				vchan->closed ||
				!kref_get_unless_zero(&vchan->refcount)) {
				pr_debug("failed to inc vcid %x remote %x session %d refcnt %d close_flg remote %d local %d\n",
					vchan->id, vchan->otherend_id,
					vchan->session_id,
					get_refcnt(vchan->refcount),
					vchan->otherend_closed, vchan->closed);
				vchan = NULL;
			}
			read_unlock(&ctx->ctx_lock);
			return vchan;
		}
	}
	read_unlock(&ctx->ctx_lock);
	return NULL;
}

struct hab_device *find_hab_device(unsigned int mm_id)
{
	int i;

	for (i = 0; i < hab_driver.ndevices; i++) {
		if (hab_driver.devp[i].id == HAB_MMID_GET_MAJOR(mm_id))
			return &hab_driver.devp[i];
	}

	pr_err("%s: id=%d\n", __func__, mm_id);
	return NULL;
}
/*
 *   open handshake in FE and BE

 *   frontend            backend
 *  send(INIT)          wait(INIT)
 *  wait(INIT_ACK)      send(INIT_ACK)
 *  send(INIT_DONE)     wait(INIT_DONE)

 */
struct virtual_channel *frontend_open(struct uhab_context *ctx,
		unsigned int mm_id,
		int dom_id,
		uint32_t flags)
{
	int ret, ret2, open_id = 0;
	struct physical_channel *pchan = NULL;
	struct hab_device *dev;
	struct virtual_channel *vchan = NULL;
	static atomic_t open_id_counter = ATOMIC_INIT(0);
	struct hab_open_request request;
	struct hab_open_request *recv_request;
	int sub_id = HAB_MMID_GET_MINOR(mm_id);
	struct hab_open_node pending_open = { { 0 } };

	dev = find_hab_device(mm_id);
	if (dev == NULL) {
		pr_err("HAB device %d is not initialized\n", mm_id);
		ret = -EINVAL;
		goto err;
	}

	/* guest can find its own id */
	pchan = hab_pchan_find_domid(dev, dom_id);
	if (!pchan) {
		pr_err("hab_pchan_find_domid failed: dom_id=%d\n", dom_id);
		ret = -EINVAL;
		goto err;
	}

	open_id = atomic_inc_return(&open_id_counter);
	vchan = hab_vchan_alloc(ctx, pchan, open_id);
	if (!vchan) {
		pr_err("vchan alloc failed\n");
		ret = -ENOMEM;
		goto err;
	}

	/* Send Init sequence */
	hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT, pchan,
		vchan->id, sub_id, open_id);
	request.xdata.ver_fe = HAB_API_VER;
	ret = hab_open_request_send(&request);
	if (ret) {
		pr_err("hab_open_request_send failed: %d\n", ret);
		goto err;
	}

	pending_open.request = request;

	/* during wait app could be terminated */
	hab_open_pending_enter(ctx, pchan, &pending_open);

	/* Wait for Init-Ack sequence */
	hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT_ACK, pchan,
		0, sub_id, open_id);
	ret = hab_open_listen(ctx, dev, &request, &recv_request, 0, flags);
	if (!ret && recv_request && ((recv_request->xdata.ver_fe & 0xFFFF0000)
		!= (recv_request->xdata.ver_be & 0xFFFF0000))) {
		/* version check */
		pr_err("hab major version mismatch fe %X be %X on mmid %d\n",
			recv_request->xdata.ver_fe,
			recv_request->xdata.ver_be, mm_id);

		hab_open_pending_exit(ctx, pchan, &pending_open);
		ret = -EPROTO;
		goto err;
	} else if (ret || !recv_request) {
		pr_err("hab_open_listen failed: %d, send cancel vcid %x subid %d openid %d\n",
			ret, vchan->id,
			sub_id, open_id);
		/* send cancel to BE due to FE's local close */
		hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT_CANCEL,
					pchan, vchan->id, sub_id, open_id);
		request.xdata.ver_fe = HAB_API_VER;
		ret2 = hab_open_request_send(&request);
		if (ret2)
			pr_err("send init_cancel failed %d on vcid %x\n", ret2,
				   vchan->id);
		hab_open_pending_exit(ctx, pchan, &pending_open);

		if (ret != -EINTR)
			ret = -EINVAL;
		goto err;
	}

	/* remove pending open locally after good pairing */
	hab_open_pending_exit(ctx, pchan, &pending_open);

	pr_debug("hab version fe %X be %X on mmid %d\n",
		recv_request->xdata.ver_fe, recv_request->xdata.ver_be,
		mm_id);
	pchan->mem_proto = (recv_request->xdata.ver_proto == 0) ? 0 : 1;
	pr_info_once("mem proto ver %u\n", pchan->mem_proto);

	vchan->otherend_id = recv_request->xdata.vchan_id;
	hab_open_request_free(recv_request);

	/* Send Init-Done sequence */
	hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT_DONE, pchan,
		0, sub_id, open_id);
	request.xdata.ver_fe = HAB_API_VER;
	ret = hab_open_request_send(&request);
	if (ret) {
		pr_err("failed to send init-done vcid %x remote %x openid %d\n",
		   vchan->id, vchan->otherend_id, vchan->session_id);
		goto err;
	}

	hab_pchan_put(pchan);

	return vchan;
err:
	if (vchan)
		hab_vchan_put(vchan);
	if (pchan)
		hab_pchan_put(pchan);

	return ERR_PTR(ret);
}

struct virtual_channel *backend_listen(struct uhab_context *ctx,
		unsigned int mm_id, int timeout, uint32_t flags)
{
	int ret, ret2;
	int open_id, ver_fe;
	int sub_id = HAB_MMID_GET_MINOR(mm_id);
	struct physical_channel *pchan = NULL;
	struct hab_device *dev;
	struct virtual_channel *vchan = NULL;
	struct hab_open_request request;
	struct hab_open_request *recv_request;
	uint32_t otherend_vchan_id;
	struct hab_open_node pending_open = { { 0 } };

	dev = find_hab_device(mm_id);
	if (dev == NULL) {
		pr_err("failed to find dev based on id %d\n", mm_id);
		ret = -EINVAL;
		goto err;
	}

	while (1) {
		/* Wait for Init sequence */
		hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT,
			NULL, 0, sub_id, 0);
		/* cancel should not happen at this moment */
		ret = hab_open_listen(ctx, dev, &request, &recv_request,
				timeout, flags);
		if (ret || !recv_request) {
			if (!ret && !recv_request)
				ret = -EINVAL;
			if (-EAGAIN == ret) {
				ret = -ETIMEDOUT;
			} else {
				/* device is closed */
				pr_err("open request wait failed ctx closing %d\n",
						ctx->closing);
			}
			goto err;
		} else if (!ret && recv_request &&
				   ((recv_request->xdata.ver_fe & 0xFFFF0000) !=
					(HAB_API_VER & 0xFFFF0000))) {
			int ret2;
			/* version check */
			pr_err("version mismatch fe %X be %X on mmid %d\n",
			   recv_request->xdata.ver_fe, HAB_API_VER, mm_id);
			hab_open_request_init(&request,
				HAB_PAYLOAD_TYPE_INIT_ACK,
				NULL, 0, sub_id, recv_request->xdata.open_id);
			request.xdata.ver_be = HAB_API_VER;
			/* reply to allow FE to bail out */
			ret2 = hab_open_request_send(&request);
			if (ret2)
				pr_err("send FE version mismatch failed mmid %d sub %d\n",
					   mm_id, sub_id);
			ret = -EPROTO;
			goto err;
		}

		recv_request->pchan->mem_proto = (recv_request->xdata.ver_proto == 0) ? 0 : 1;
		pr_info_once("mem proto ver %u\n", recv_request->pchan->mem_proto);

		/* guest id from guest */
		otherend_vchan_id = recv_request->xdata.vchan_id;
		open_id = recv_request->xdata.open_id;
		ver_fe = recv_request->xdata.ver_fe;
		pchan = recv_request->pchan;
		hab_pchan_get(pchan);
		hab_open_request_free(recv_request);
		recv_request = NULL;

		vchan = hab_vchan_alloc(ctx, pchan, open_id);
		if (!vchan) {
			ret = -ENOMEM;
			goto err;
		}

		vchan->otherend_id = otherend_vchan_id;

		/* Send Init-Ack sequence */
		hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT_ACK,
				pchan, vchan->id, sub_id, open_id);
		request.xdata.ver_fe = ver_fe; /* carry over */
		request.xdata.ver_be = HAB_API_VER;
		ret = hab_open_request_send(&request);
		if (ret)
			goto err;

		pending_open.request = request;
		/* wait only after init-ack is sent */
		hab_open_pending_enter(ctx, pchan, &pending_open);

		/* Wait for Ack sequence */
		hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT_DONE,
				pchan, 0, sub_id, open_id);
		ret = hab_open_listen(ctx, dev, &request, &recv_request,
			HAB_HS_TIMEOUT, flags);
		hab_open_pending_exit(ctx, pchan, &pending_open);
		if (ret && recv_request &&
			recv_request->type == HAB_PAYLOAD_TYPE_INIT_CANCEL) {
			pr_err("listen cancelled vcid %x subid %d openid %d ret %d\n",
				request.xdata.vchan_id, request.xdata.sub_id,
				request.xdata.open_id, ret);

			/* FE cancels this session.
			 * So BE has to cancel its too
			 */
			hab_open_request_init(&request,
					HAB_PAYLOAD_TYPE_INIT_CANCEL, pchan,
					vchan->id, sub_id, open_id);
			ret2 = hab_open_request_send(&request);
			if (ret2)
				pr_err("send init_ack failed %d on vcid %x\n",
					ret2, vchan->id);
			hab_open_pending_exit(ctx, pchan, &pending_open);

			ret = -ENODEV; /* open request cancelled remotely */
			break;
		} else if (ret != -EAGAIN) {
			hab_open_pending_exit(ctx, pchan, &pending_open);
			break; /* received something. good case! */
		}

		/* stay in the loop retry */
		pr_warn("retry open ret %d vcid %X remote %X sub %d open %d\n",
			ret, vchan->id, vchan->otherend_id, sub_id, open_id);

		/* retry path starting here. free previous vchan */
		hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT_CANCEL,
					pchan, vchan->id, sub_id, open_id);
		request.xdata.ver_fe = ver_fe;
		request.xdata.ver_be = HAB_API_VER;
		ret2 = hab_open_request_send(&request);
		if (ret2)
			pr_err("send init_ack failed %d on vcid %x\n", ret2,
				   vchan->id);
		hab_open_pending_exit(ctx, pchan, &pending_open);

		hab_vchan_put(vchan);
		vchan = NULL;
		hab_pchan_put(pchan);
		pchan = NULL;
	}

	if (ret || !recv_request) {
		pr_err("backend mmid %d listen error %d\n", mm_id, ret);
		ret = -EINVAL;
		goto err;
	}

	hab_open_request_free(recv_request);
	hab_pchan_put(pchan);
	return vchan;
err:
	if (ret != -ETIMEDOUT)
		pr_err("listen on mmid %d failed\n", mm_id);
	if (vchan)
		hab_vchan_put(vchan);
	if (pchan)
		hab_pchan_put(pchan);
	return ERR_PTR(ret);
}

long hab_vchan_send(struct uhab_context *ctx,
		int vcid,
		size_t sizebytes,
		void *data,
		unsigned int flags)
{
	struct virtual_channel *vchan;
	int ret;
	struct hab_header header = HAB_HEADER_INITIALIZER;
	unsigned int nonblocking_flag = flags & HABMM_SOCKET_SEND_FLAGS_NON_BLOCKING;

	if (sizebytes > (size_t)HAB_HEADER_SIZE_MAX) {
		pr_err("Message too large, %lu bytes, max is %d\n",
			sizebytes, HAB_HEADER_SIZE_MAX);
		return -EINVAL;
	}

	vchan = hab_get_vchan_fromvcid(vcid, ctx, 0);
	if (!vchan || vchan->otherend_closed) {
		ret = -ENODEV;
		goto err;
	}

	/**
	 * Without non-blocking configured, when the shared memory (vdev-shmem project) or
	 * vh_buf_header (virtio-hab project) used by HAB for front-end and back-end messaging
	 * is exhausted, the current path will be blocked.
	 * 1. The vdev-shmem project will be blocked in the hab_vchan_send function;
	 * 2. The virtio-hab project will be blocked in the hab_physical_send function;
	 */
	if (!nonblocking_flag)
		might_sleep();

	/* log msg send timestamp: enter hab_vchan_send */
	trace_hab_vchan_send_start(vchan);

	HAB_HEADER_SET_SIZE(header, sizebytes);
	if (flags & HABMM_SOCKET_SEND_FLAGS_XING_VM_STAT) {
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_PROFILE);
		if (sizebytes < sizeof(struct habmm_xing_vm_stat)) {
			pr_err("wrong profiling buffer size %zd, expect %zd\n",
				sizebytes,
				sizeof(struct habmm_xing_vm_stat));
			return -EINVAL;
		}
	} else if (flags & HABMM_SOCKET_XVM_SCHE_TEST) {
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_SCHE_MSG);
	} else if (flags & HABMM_SOCKET_XVM_SCHE_TEST_ACK) {
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_SCHE_MSG_ACK);
	} else if (flags & HABMM_SOCKET_XVM_SCHE_RESULT_REQ) {
		if (sizebytes < sizeof(unsigned long long)) {
			pr_err("Message buffer too small, %lu bytes, expect %d\n",
				sizebytes,
				sizeof(unsigned long long));
			return -EINVAL;
		}
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_SCHE_RESULT_REQ);
	} else if (flags & HABMM_SOCKET_XVM_SCHE_RESULT_RSP) {
		if (sizebytes < 3 * sizeof(unsigned long long)) {
			pr_err("Message buffer too small, %lu bytes, expect %d\n",
				sizebytes,
				3 * sizeof(unsigned long long));
			return -EINVAL;
		}
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_SCHE_RESULT_RSP);
	} else {
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_MSG);
	}
	HAB_HEADER_SET_ID(header, vchan->otherend_id);
	HAB_HEADER_SET_SESSION_ID(header, vchan->session_id);

	while (1) {
		ret = physical_channel_send(vchan->pchan, &header, data, nonblocking_flag);

		if (vchan->otherend_closed || nonblocking_flag ||
			ret != -EAGAIN)
			break;

		schedule();
	}

	/*
	 * The ret here as 0 indicates the message was already sent out
	 * from the hab_vchan_send()'s perspective.
	 */
	if (!ret)
		atomic64_inc(&vchan->tx_cnt);
err:

	/* log msg send timestamp: exit hab_vchan_send */
	trace_hab_vchan_send_done(vchan);

	if (vchan)
		hab_vchan_put(vchan);

	return ret;
}

int hab_vchan_recv(struct uhab_context *ctx,
				struct hab_message **message,
				int vcid,
				int *rsize,
				unsigned int timeout,
				unsigned int flags)
{
	struct virtual_channel *vchan;
	int ret = 0;
	int nonblocking_flag = flags & HABMM_SOCKET_RECV_FLAGS_NON_BLOCKING;

	vchan = hab_get_vchan_fromvcid(vcid, ctx, 1);
	if (!vchan) {
		pr_err("vcid %X vchan 0x%pK ctx %pK\n", vcid, vchan, ctx);
		*message = NULL;
		return -ENODEV;
	}

	vchan->rx_inflight = 1;

	if (nonblocking_flag) {
		/*
		 * Try to pull data from the ring in this context instead of
		 * IRQ handler. Any available messages will be copied and queued
		 * internally, then fetched by hab_msg_dequeue()
		 */
		physical_channel_rx_dispatch((unsigned long) vchan->pchan);
	}

	ret = hab_msg_dequeue(vchan, message, rsize, timeout, flags);
	if (!ret && *message) {
		/* log msg recv timestamp: exit hab_vchan_recv */
		trace_hab_vchan_recv_done(vchan, *message);

		/*
		 * Here, it is for sure that a message was received from the
		 * hab_vchan_recv()'s view w/ the ret as 0 and *message as
		 * non-zero.
		 */
		atomic64_inc(&vchan->rx_cnt);
	}

	vchan->rx_inflight = 0;

	hab_vchan_put(vchan);
	return ret;
}

bool hab_is_loopback(void)
{
	return hab_driver.b_loopback;
}

int hab_vchan_open(struct uhab_context *ctx,
		unsigned int mmid,
		int32_t *vcid,
		int32_t timeout,
		uint32_t flags)
{
	struct virtual_channel *vchan = NULL;
	struct hab_device *dev;

	pr_debug("Open mmid=%d, loopback mode=%d, loopback be ctx %d\n",
		mmid, hab_driver.b_loopback, ctx->lb_be);

	if (!vcid)
		return -EINVAL;

	if (hab_is_loopback()) {
		if (ctx->lb_be)
			vchan = backend_listen(ctx, mmid, timeout, flags);
		else
			vchan = frontend_open(ctx, mmid, LOOPBACK_DOM, flags);
	} else {
		dev = find_hab_device(mmid);

		if (dev) {
			struct physical_channel *pchan =
				hab_pchan_find_domid(dev,
					HABCFG_VMID_DONT_CARE);
			if (pchan) {
				if (pchan->kernel_only && !ctx->kernel) {
					pr_err("pchan only serves the kernel: mmid %d\n", mmid);
					return -EPERM;
				}

				if (pchan->is_be)
					vchan = backend_listen(ctx, mmid,
							timeout, flags);
				else
					vchan = frontend_open(ctx, mmid,
							HABCFG_VMID_DONT_CARE, flags);
			} else {
				pr_err("open on nonexistent pchan (mmid %x)\n",
					mmid);
				return -ENODEV;
			}
		} else {
			pr_err("failed to find device, mmid %d\n", mmid);
			return -ENODEV;
		}
	}

	if (IS_ERR(vchan)) {
		if (-ETIMEDOUT != PTR_ERR(vchan) && -EAGAIN != PTR_ERR(vchan))
			pr_err("vchan open failed mmid=%d\n", mmid);
		return PTR_ERR(vchan);
	}

	pr_debug("vchan id %x remote id %x session %d\n", vchan->id,
			vchan->otherend_id, vchan->session_id);

	hab_write_lock(&ctx->ctx_lock, !ctx->kernel);
	list_add_tail(&vchan->node, &ctx->vchannels);
	ctx->vcnt++;
	*vcid = vchan->id;
	hab_write_unlock(&ctx->ctx_lock, !ctx->kernel);

	return 0;
}

void hab_send_close_msg(struct virtual_channel *vchan)
{
	struct hab_header header = HAB_HEADER_INITIALIZER;
	int ret = 0;

	if (vchan && !vchan->otherend_closed) {
		HAB_HEADER_SET_SIZE(header, 0);
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_CLOSE);
		HAB_HEADER_SET_ID(header, vchan->otherend_id);
		HAB_HEADER_SET_SESSION_ID(header, vchan->session_id);
		ret = physical_channel_send(vchan->pchan, &header, NULL,
				HABMM_SOCKET_SEND_FLAGS_NON_BLOCKING);
		if (ret != 0)
			pr_err("failed to send close msg %d, vcid %x\n",
				ret, vchan->id);
	}
}

void hab_send_unimport_msg(struct virtual_channel *vchan, uint32_t exp_id)
{
	struct hab_header header = HAB_HEADER_INITIALIZER;
	int ret = 0;

	if (vchan) {
		HAB_HEADER_SET_SIZE(header, sizeof(uint32_t));
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_UNIMPORT);
		HAB_HEADER_SET_ID(header, vchan->otherend_id);
		HAB_HEADER_SET_SESSION_ID(header, vchan->session_id);
		ret = physical_channel_send(vchan->pchan, &header, &exp_id,
				HABMM_SOCKET_SEND_FLAGS_NON_BLOCKING);
		if (ret != 0)
			pr_err("failed to send unimp msg %d, vcid %x\n",
				ret, vchan->id);
	}
}

int hab_vchan_close(struct uhab_context *ctx, int32_t vcid)
{
	struct virtual_channel *vchan = NULL, *tmp = NULL;
	int vchan_found = 0;
	int ret = 0;
	int irqs_disabled = irqs_disabled();

	if (!ctx)
		return -EINVAL;

	hab_write_lock(&ctx->ctx_lock, !ctx->kernel || irqs_disabled);
	list_for_each_entry_safe(vchan, tmp, &ctx->vchannels, node) {
		if (vchan->id == vcid) {
			/* local close starts */
			vchan->closed = 1;

			/* vchan is not in this ctx anymore */
			list_del(&vchan->node);
			ctx->vcnt--;

			pr_debug("vcid %x remote %x session %d refcnt %d\n",
				vchan->id, vchan->otherend_id,
				vchan->session_id, get_refcnt(vchan->refcount));

			hab_write_unlock(&ctx->ctx_lock, !ctx->kernel || irqs_disabled);
			/* unblocking blocked in-calls */
			hab_vchan_stop_notify(vchan);
			hab_vchan_put(vchan); /* there is a lock inside */
			hab_write_lock(&ctx->ctx_lock, !ctx->kernel || irqs_disabled);
			vchan_found = 1;
			break;
		}
	}
	hab_write_unlock(&ctx->ctx_lock, !ctx->kernel || irqs_disabled);

	if (!vchan_found)
		ret = -ENODEV;

	return ret;
}

/*
 * To name the pchan - the pchan has two ends, either FE or BE locally.
 * if is_be is true, then this is listener for BE. pchane name use remote
 * FF's vmid from the table.
 * if is_be is false, then local is FE as opener. pchan name use local FE's
 * vmid (self)
 */
static int hab_initialize_pchan_entry(struct hab_device *mmid_device,
				int vmid_local, int vmid_remote, int is_be, int kernel_only)
{
	char pchan_name[MAX_VMID_NAME_SIZE];
	struct physical_channel *pchan = NULL;
	int ret;
	int vmid = is_be ? vmid_remote : vmid_local; /* used for naming only */

	if (!mmid_device) {
		pr_err("habdev %pK, vmid local %d, remote %d, is be %d\n",
				mmid_device, vmid_local, vmid_remote, is_be);
		return -EINVAL;
	}

	snprintf(pchan_name, MAX_VMID_NAME_SIZE, "vm%d-", vmid);
	strlcat(pchan_name, mmid_device->name, MAX_VMID_NAME_SIZE);

	ret = habhyp_commdev_alloc((void **)&pchan, is_be, pchan_name,
					vmid_remote, mmid_device);
	if (ret) {
		pr_err("failed %d to allocate pchan %s, vmid local %d, remote %d, is_be %d, total %d\n",
				ret, pchan_name, vmid_local, vmid_remote,
				is_be, mmid_device->pchan_cnt);
	} else {
		/* local/remote id setting should be kept in lower level */
		pchan->vmid_local = vmid_local;
		pchan->vmid_remote = vmid_remote;
		pchan->kernel_only = kernel_only;
		pr_debug("pchan %s mmid %s local %d remote %d role %d, kernel only %d\n",
				pchan_name, mmid_device->name,
				pchan->vmid_local, pchan->vmid_remote,
				pchan->dom_id, pchan->kernel_only);
	}

	return ret;
}

static int hab_generate_pchan_group(struct local_vmid *settings,
								int i, int j, int start, int end)
{
	int k, ret = 0;

	for (k = start + 1; k < end; k++) {
		/*
		 * if this local pchan end is BE, then use
		 * remote FE's vmid. If local end is FE, then
		 * use self vmid
		 */
		ret += hab_initialize_pchan_entry(
				find_hab_device(k),
				settings->self,
				HABCFG_GET_VMID(settings, i),
				HABCFG_GET_BE(settings, i, j),
				HABCFG_GET_KERNEL(settings, i, j));
	}

	ret += hab_create_cdev_node(HABCFG_GET_MMID(settings, i, j));

	return ret;
}

/*
 * generate pchan list based on hab settings table.
 * return status 0: success, otherwise failure
 */
static int hab_generate_pchan(struct local_vmid *settings, int i, int j)
{
	int ret = 0;

	pr_debug("%d as mmid %d in vmid %d\n",
			HABCFG_GET_MMID(settings, i, j), j, i);

	switch (HABCFG_GET_MMID(settings, i, j)) {
	case MM_AUD_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_AUD_START, MM_AUD_END);
		break;
	case MM_CAM_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_CAM_START, MM_CAM_END);
		break;
	case MM_DISP_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_DISP_START, MM_DISP_END);
		break;
	case MM_GFX_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_GFX_START, MM_GFX_END);
		break;
	case MM_VID_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_VID_START, MM_VID_END);
		break;
	case MM_MISC_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_MISC_START, MM_MISC_END);
		break;
	case MM_QCPE_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_QCPE_START, MM_QCPE_END);
		break;
	case MM_CLK_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_CLK_START, MM_CLK_END);
		break;
	case MM_FDE_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_FDE_START, MM_FDE_END);
		break;
	case MM_BUFFERQ_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_BUFFERQ_START, MM_BUFFERQ_END);
		break;
	case MM_DATA_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_DATA_START, MM_DATA_END);
		break;
	case MM_HSI2S_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_HSI2S_START, MM_HSI2S_END);
		break;
	case MM_XVM_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_XVM_START, MM_XVM_END);
		break;
	case MM_VNW_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_VNW_START, MM_VNW_END);
		break;
	case MM_EXT_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_EXT_START, MM_EXT_END);
		break;
	case MM_GPCE_START/100:
		ret = hab_generate_pchan_group(settings, i, j, MM_GPCE_START, MM_GPCE_END);
		break;
	default:
		pr_err("failed to find mmid %d, i %d, j %d\n",
			HABCFG_GET_MMID(settings, i, j), i, j);

		break;
	}
	return ret;
}

/*
 * generate pchan list based on hab settings table.
 * return status 0: success, otherwise failure
 */
static int hab_generate_pchan_list(struct local_vmid *settings)
{
	int i, j, ret = 0;

	/* scan by valid VMs, then mmid */
	pr_debug("self vmid is %d\n", settings->self);
	for (i = 0; i < HABCFG_VMID_MAX; i++) {
		if (HABCFG_GET_VMID(settings, i) != HABCFG_VMID_INVALID &&
			HABCFG_GET_VMID(settings, i) != settings->self) {
			pr_debug("create pchans for vm %d\n", i);

			for (j = 1; j <= HABCFG_MMID_AREA_MAX; j++) {
				if (HABCFG_GET_MMID(settings, i, j)
						!= HABCFG_VMID_INVALID)
					ret = hab_generate_pchan(settings,
								i, j);
			}
		}
	}
	return ret;
}

/*
 * This function checks hypervisor plug-in readiness, read in hab configs,
 * and configure pchans
 */
#ifdef CONFIG_MSM_HAB_DEFAULT_VMID
#define DEFAULT_GVMID CONFIG_MSM_HAB_DEFAULT_VMID
#else
#define DEFAULT_GVMID 2
#endif

int do_hab_parse(void)
{
	int result;
	int i;
	struct hab_device *device;

	/* single GVM is 2, multigvm is 2 or 3. GHS LV-GVM 2, LA-GVM 3 */
	int default_gvmid = DEFAULT_GVMID;

	pr_debug("hab parse starts for %s\n", hab_info_str);

	/* first check if hypervisor plug-in is ready */
	result = hab_hypervisor_register();
	if (result) {
		pr_err("register HYP plug-in failed, ret %d\n", result);
		return result;
	}

	/*
	 * Initialize open Q before first pchan starts.
	 * Each is for one pchan list
	 */
	for (i = 0; i < hab_driver.ndevices; i++) {
		device = &hab_driver.devp[i];
		init_waitqueue_head(&device->openq);
	}

	/* read in hab config and create pchans*/
	memset(&hab_driver.settings, HABCFG_VMID_INVALID,
				sizeof(hab_driver.settings));
	result = hab_parse(&hab_driver.settings);
	if (result) {
		pr_err("hab config open failed, prepare default gvm %d settings\n",
			   default_gvmid);
		fill_default_gvm_settings(&hab_driver.settings, default_gvmid,
						MM_AUD_START, MM_ID_MAX);
	}

	/* now generate hab pchan list */
	result  = hab_generate_pchan_list(&hab_driver.settings);
	if (result) {
		pr_err("generate pchan list failed, ret %d\n", result);
	} else {
		int pchan_total = 0;

		for (i = 0; i < hab_driver.ndevices; i++) {
			device = &hab_driver.devp[i];
			pchan_total += device->pchan_cnt;
		}
		pr_debug("ret %d, total %d pchans added, ndevices %d\n",
				 result, pchan_total, hab_driver.ndevices);
	}

	return result;
}

void hab_hypervisor_unregister_common(void)
{
	int status, i;
	struct uhab_context *ctx;
	struct virtual_channel *vchan;

	for (i = 0; i < hab_driver.ndevices; i++) {
		struct hab_device *habdev = &hab_driver.devp[i];
		struct physical_channel *pchan, *pchan_tmp;

		list_for_each_entry_safe(pchan, pchan_tmp,
				&habdev->pchannels, node) {
			status = habhyp_commdev_dealloc(pchan);
			if (status) {
				pr_err("failed to free pchan %pK, i %d, ret %d\n",
					pchan, i, status);
			}
		}
	}

	/* detect leaking uctx */
	spin_lock_bh(&hab_driver.drvlock);
	list_for_each_entry(ctx, &hab_driver.uctx_list, node) {
		pr_warn("leaking ctx owner %d refcnt %d kernel %d\n",
			ctx->owner, get_refcnt(ctx->refcount), ctx->kernel);
		/* further check vchan leak */
		read_lock(&ctx->ctx_lock);
		list_for_each_entry(vchan, &ctx->vchannels, node) {
			pr_warn("leaking vchan id %X remote %X refcnt %d\n",
					vchan->id, vchan->otherend_id,
					get_refcnt(vchan->refcount));
		}
		read_unlock(&ctx->ctx_lock);
	}
	spin_unlock_bh(&hab_driver.drvlock);
}

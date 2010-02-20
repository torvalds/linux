/*
 * zfcp device driver
 *
 * Fibre Channel related functions for the zfcp device driver.
 *
 * Copyright IBM Corporation 2008, 2009
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/types.h>
#include <scsi/fc/fc_els.h>
#include <scsi/libfc.h>
#include "zfcp_ext.h"
#include "zfcp_fc.h"

static u32 zfcp_fc_rscn_range_mask[] = {
	[ELS_ADDR_FMT_PORT]		= 0xFFFFFF,
	[ELS_ADDR_FMT_AREA]		= 0xFFFF00,
	[ELS_ADDR_FMT_DOM]		= 0xFF0000,
	[ELS_ADDR_FMT_FAB]		= 0x000000,
};

static int zfcp_fc_wka_port_get(struct zfcp_fc_wka_port *wka_port)
{
	if (mutex_lock_interruptible(&wka_port->mutex))
		return -ERESTARTSYS;

	if (wka_port->status == ZFCP_FC_WKA_PORT_OFFLINE ||
	    wka_port->status == ZFCP_FC_WKA_PORT_CLOSING) {
		wka_port->status = ZFCP_FC_WKA_PORT_OPENING;
		if (zfcp_fsf_open_wka_port(wka_port))
			wka_port->status = ZFCP_FC_WKA_PORT_OFFLINE;
	}

	mutex_unlock(&wka_port->mutex);

	wait_event(wka_port->completion_wq,
		   wka_port->status == ZFCP_FC_WKA_PORT_ONLINE ||
		   wka_port->status == ZFCP_FC_WKA_PORT_OFFLINE);

	if (wka_port->status == ZFCP_FC_WKA_PORT_ONLINE) {
		atomic_inc(&wka_port->refcount);
		return 0;
	}
	return -EIO;
}

static void zfcp_fc_wka_port_offline(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct zfcp_fc_wka_port *wka_port =
			container_of(dw, struct zfcp_fc_wka_port, work);

	mutex_lock(&wka_port->mutex);
	if ((atomic_read(&wka_port->refcount) != 0) ||
	    (wka_port->status != ZFCP_FC_WKA_PORT_ONLINE))
		goto out;

	wka_port->status = ZFCP_FC_WKA_PORT_CLOSING;
	if (zfcp_fsf_close_wka_port(wka_port)) {
		wka_port->status = ZFCP_FC_WKA_PORT_OFFLINE;
		wake_up(&wka_port->completion_wq);
	}
out:
	mutex_unlock(&wka_port->mutex);
}

static void zfcp_fc_wka_port_put(struct zfcp_fc_wka_port *wka_port)
{
	if (atomic_dec_return(&wka_port->refcount) != 0)
		return;
	/* wait 10 milliseconds, other reqs might pop in */
	schedule_delayed_work(&wka_port->work, HZ / 100);
}

static void zfcp_fc_wka_port_init(struct zfcp_fc_wka_port *wka_port, u32 d_id,
				  struct zfcp_adapter *adapter)
{
	init_waitqueue_head(&wka_port->completion_wq);

	wka_port->adapter = adapter;
	wka_port->d_id = d_id;

	wka_port->status = ZFCP_FC_WKA_PORT_OFFLINE;
	atomic_set(&wka_port->refcount, 0);
	mutex_init(&wka_port->mutex);
	INIT_DELAYED_WORK(&wka_port->work, zfcp_fc_wka_port_offline);
}

static void zfcp_fc_wka_port_force_offline(struct zfcp_fc_wka_port *wka)
{
	cancel_delayed_work_sync(&wka->work);
	mutex_lock(&wka->mutex);
	wka->status = ZFCP_FC_WKA_PORT_OFFLINE;
	mutex_unlock(&wka->mutex);
}

void zfcp_fc_wka_ports_force_offline(struct zfcp_fc_wka_ports *gs)
{
	if (!gs)
		return;
	zfcp_fc_wka_port_force_offline(&gs->ms);
	zfcp_fc_wka_port_force_offline(&gs->ts);
	zfcp_fc_wka_port_force_offline(&gs->ds);
	zfcp_fc_wka_port_force_offline(&gs->as);
}

static void _zfcp_fc_incoming_rscn(struct zfcp_fsf_req *fsf_req, u32 range,
				   struct fc_els_rscn_page *page)
{
	unsigned long flags;
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct zfcp_port *port;

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list) {
		if ((port->d_id & range) == (ntoh24(page->rscn_fid) & range))
			zfcp_fc_test_link(port);
		if (!port->d_id)
			zfcp_erp_port_reopen(port,
					     ZFCP_STATUS_COMMON_ERP_FAILED,
					     "fcrscn1", NULL);
	}
	read_unlock_irqrestore(&adapter->port_list_lock, flags);
}

static void zfcp_fc_incoming_rscn(struct zfcp_fsf_req *fsf_req)
{
	struct fsf_status_read_buffer *status_buffer = (void *)fsf_req->data;
	struct fc_els_rscn *head;
	struct fc_els_rscn_page *page;
	u16 i;
	u16 no_entries;
	unsigned int afmt;

	head = (struct fc_els_rscn *) status_buffer->payload.data;
	page = (struct fc_els_rscn_page *) head;

	/* see FC-FS */
	no_entries = head->rscn_plen / sizeof(struct fc_els_rscn_page);

	for (i = 1; i < no_entries; i++) {
		/* skip head and start with 1st element */
		page++;
		afmt = page->rscn_page_flags & ELS_RSCN_ADDR_FMT_MASK;
		_zfcp_fc_incoming_rscn(fsf_req, zfcp_fc_rscn_range_mask[afmt],
				       page);
	}
	queue_work(fsf_req->adapter->work_queue, &fsf_req->adapter->scan_work);
}

static void zfcp_fc_incoming_wwpn(struct zfcp_fsf_req *req, u64 wwpn)
{
	unsigned long flags;
	struct zfcp_adapter *adapter = req->adapter;
	struct zfcp_port *port;

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list)
		if (port->wwpn == wwpn) {
			zfcp_erp_port_forced_reopen(port, 0, "fciwwp1", req);
			break;
		}
	read_unlock_irqrestore(&adapter->port_list_lock, flags);
}

static void zfcp_fc_incoming_plogi(struct zfcp_fsf_req *req)
{
	struct fsf_status_read_buffer *status_buffer;
	struct fc_els_flogi *plogi;

	status_buffer = (struct fsf_status_read_buffer *) req->data;
	plogi = (struct fc_els_flogi *) status_buffer->payload.data;
	zfcp_fc_incoming_wwpn(req, plogi->fl_wwpn);
}

static void zfcp_fc_incoming_logo(struct zfcp_fsf_req *req)
{
	struct fsf_status_read_buffer *status_buffer =
		(struct fsf_status_read_buffer *)req->data;
	struct fc_els_logo *logo =
		(struct fc_els_logo *) status_buffer->payload.data;

	zfcp_fc_incoming_wwpn(req, logo->fl_n_port_wwn);
}

/**
 * zfcp_fc_incoming_els - handle incoming ELS
 * @fsf_req - request which contains incoming ELS
 */
void zfcp_fc_incoming_els(struct zfcp_fsf_req *fsf_req)
{
	struct fsf_status_read_buffer *status_buffer =
		(struct fsf_status_read_buffer *) fsf_req->data;
	unsigned int els_type = status_buffer->payload.data[0];

	zfcp_dbf_san_incoming_els(fsf_req);
	if (els_type == ELS_PLOGI)
		zfcp_fc_incoming_plogi(fsf_req);
	else if (els_type == ELS_LOGO)
		zfcp_fc_incoming_logo(fsf_req);
	else if (els_type == ELS_RSCN)
		zfcp_fc_incoming_rscn(fsf_req);
}

static void zfcp_fc_ns_gid_pn_eval(void *data)
{
	struct zfcp_fc_gid_pn *gid_pn = data;
	struct zfcp_fsf_ct_els *ct = &gid_pn->ct;
	struct zfcp_fc_gid_pn_req *gid_pn_req = sg_virt(ct->req);
	struct zfcp_fc_gid_pn_resp *gid_pn_resp = sg_virt(ct->resp);
	struct zfcp_port *port = gid_pn->port;

	if (ct->status)
		return;
	if (gid_pn_resp->ct_hdr.ct_cmd != FC_FS_ACC)
		return;

	/* paranoia */
	if (gid_pn_req->gid_pn.fn_wwpn != port->wwpn)
		return;
	/* looks like a valid d_id */
	port->d_id = ntoh24(gid_pn_resp->gid_pn.fp_fid);
}

static void zfcp_fc_complete(void *data)
{
	complete(data);
}

static int zfcp_fc_ns_gid_pn_request(struct zfcp_port *port,
				     struct zfcp_fc_gid_pn *gid_pn)
{
	struct zfcp_adapter *adapter = port->adapter;
	DECLARE_COMPLETION_ONSTACK(completion);
	int ret;

	/* setup parameters for send generic command */
	gid_pn->port = port;
	gid_pn->ct.handler = zfcp_fc_complete;
	gid_pn->ct.handler_data = &completion;
	gid_pn->ct.req = &gid_pn->sg_req;
	gid_pn->ct.resp = &gid_pn->sg_resp;
	sg_init_one(&gid_pn->sg_req, &gid_pn->gid_pn_req,
		    sizeof(struct zfcp_fc_gid_pn_req));
	sg_init_one(&gid_pn->sg_resp, &gid_pn->gid_pn_resp,
		    sizeof(struct zfcp_fc_gid_pn_resp));

	/* setup nameserver request */
	gid_pn->gid_pn_req.ct_hdr.ct_rev = FC_CT_REV;
	gid_pn->gid_pn_req.ct_hdr.ct_fs_type = FC_FST_DIR;
	gid_pn->gid_pn_req.ct_hdr.ct_fs_subtype = FC_NS_SUBTYPE;
	gid_pn->gid_pn_req.ct_hdr.ct_options = 0;
	gid_pn->gid_pn_req.ct_hdr.ct_cmd = FC_NS_GID_PN;
	gid_pn->gid_pn_req.ct_hdr.ct_mr_size = ZFCP_FC_CT_SIZE_PAGE / 4;
	gid_pn->gid_pn_req.gid_pn.fn_wwpn = port->wwpn;

	ret = zfcp_fsf_send_ct(&adapter->gs->ds, &gid_pn->ct,
			       adapter->pool.gid_pn_req,
			       ZFCP_FC_CTELS_TMO);
	if (!ret) {
		wait_for_completion(&completion);
		zfcp_fc_ns_gid_pn_eval(gid_pn);
	}
	return ret;
}

/**
 * zfcp_fc_ns_gid_pn_request - initiate GID_PN nameserver request
 * @port: port where GID_PN request is needed
 * return: -ENOMEM on error, 0 otherwise
 */
static int zfcp_fc_ns_gid_pn(struct zfcp_port *port)
{
	int ret;
	struct zfcp_fc_gid_pn *gid_pn;
	struct zfcp_adapter *adapter = port->adapter;

	gid_pn = mempool_alloc(adapter->pool.gid_pn, GFP_ATOMIC);
	if (!gid_pn)
		return -ENOMEM;

	memset(gid_pn, 0, sizeof(*gid_pn));

	ret = zfcp_fc_wka_port_get(&adapter->gs->ds);
	if (ret)
		goto out;

	ret = zfcp_fc_ns_gid_pn_request(port, gid_pn);

	zfcp_fc_wka_port_put(&adapter->gs->ds);
out:
	mempool_free(gid_pn, adapter->pool.gid_pn);
	return ret;
}

void zfcp_fc_port_did_lookup(struct work_struct *work)
{
	int ret;
	struct zfcp_port *port = container_of(work, struct zfcp_port,
					      gid_pn_work);

	ret = zfcp_fc_ns_gid_pn(port);
	if (ret) {
		/* could not issue gid_pn for some reason */
		zfcp_erp_adapter_reopen(port->adapter, 0, "fcgpn_1", NULL);
		goto out;
	}

	if (!port->d_id) {
		zfcp_erp_port_failed(port, "fcgpn_2", NULL);
		goto out;
	}

	zfcp_erp_port_reopen(port, 0, "fcgpn_3", NULL);
out:
	put_device(&port->sysfs_device);
}

/**
 * zfcp_fc_trigger_did_lookup - trigger the d_id lookup using a GID_PN request
 * @port: The zfcp_port to lookup the d_id for.
 */
void zfcp_fc_trigger_did_lookup(struct zfcp_port *port)
{
	get_device(&port->sysfs_device);
	if (!queue_work(port->adapter->work_queue, &port->gid_pn_work))
		put_device(&port->sysfs_device);
}

/**
 * zfcp_fc_plogi_evaluate - evaluate PLOGI playload
 * @port: zfcp_port structure
 * @plogi: plogi payload
 *
 * Evaluate PLOGI playload and copy important fields into zfcp_port structure
 */
void zfcp_fc_plogi_evaluate(struct zfcp_port *port, struct fc_els_flogi *plogi)
{
	if (plogi->fl_wwpn != port->wwpn) {
		port->d_id = 0;
		dev_warn(&port->adapter->ccw_device->dev,
			 "A port opened with WWPN 0x%016Lx returned data that "
			 "identifies it as WWPN 0x%016Lx\n",
			 (unsigned long long) port->wwpn,
			 (unsigned long long) plogi->fl_wwpn);
		return;
	}

	port->wwnn = plogi->fl_wwnn;
	port->maxframe_size = plogi->fl_csp.sp_bb_data;

	if (plogi->fl_cssp[0].cp_class & FC_CPC_VALID)
		port->supported_classes |= FC_COS_CLASS1;
	if (plogi->fl_cssp[1].cp_class & FC_CPC_VALID)
		port->supported_classes |= FC_COS_CLASS2;
	if (plogi->fl_cssp[2].cp_class & FC_CPC_VALID)
		port->supported_classes |= FC_COS_CLASS3;
	if (plogi->fl_cssp[3].cp_class & FC_CPC_VALID)
		port->supported_classes |= FC_COS_CLASS4;
}

static void zfcp_fc_adisc_handler(void *data)
{
	struct zfcp_fc_els_adisc *adisc = data;
	struct zfcp_port *port = adisc->els.port;
	struct fc_els_adisc *adisc_resp = &adisc->adisc_resp;

	if (adisc->els.status) {
		/* request rejected or timed out */
		zfcp_erp_port_forced_reopen(port, ZFCP_STATUS_COMMON_ERP_FAILED,
					    "fcadh_1", NULL);
		goto out;
	}

	if (!port->wwnn)
		port->wwnn = adisc_resp->adisc_wwnn;

	if ((port->wwpn != adisc_resp->adisc_wwpn) ||
	    !(atomic_read(&port->status) & ZFCP_STATUS_COMMON_OPEN)) {
		zfcp_erp_port_reopen(port, ZFCP_STATUS_COMMON_ERP_FAILED,
				     "fcadh_2", NULL);
		goto out;
	}

	/* port is good, unblock rport without going through erp */
	zfcp_scsi_schedule_rport_register(port);
 out:
	atomic_clear_mask(ZFCP_STATUS_PORT_LINK_TEST, &port->status);
	put_device(&port->sysfs_device);
	kmem_cache_free(zfcp_data.adisc_cache, adisc);
}

static int zfcp_fc_adisc(struct zfcp_port *port)
{
	struct zfcp_fc_els_adisc *adisc;
	struct zfcp_adapter *adapter = port->adapter;
	int ret;

	adisc = kmem_cache_alloc(zfcp_data.adisc_cache, GFP_ATOMIC);
	if (!adisc)
		return -ENOMEM;

	adisc->els.port = port;
	adisc->els.req = &adisc->req;
	adisc->els.resp = &adisc->resp;
	sg_init_one(adisc->els.req, &adisc->adisc_req,
		    sizeof(struct fc_els_adisc));
	sg_init_one(adisc->els.resp, &adisc->adisc_resp,
		    sizeof(struct fc_els_adisc));

	adisc->els.handler = zfcp_fc_adisc_handler;
	adisc->els.handler_data = adisc;

	/* acc. to FC-FS, hard_nport_id in ADISC should not be set for ports
	   without FC-AL-2 capability, so we don't set it */
	adisc->adisc_req.adisc_wwpn = fc_host_port_name(adapter->scsi_host);
	adisc->adisc_req.adisc_wwnn = fc_host_node_name(adapter->scsi_host);
	adisc->adisc_req.adisc_cmd = ELS_ADISC;
	hton24(adisc->adisc_req.adisc_port_id,
	       fc_host_port_id(adapter->scsi_host));

	ret = zfcp_fsf_send_els(adapter, port->d_id, &adisc->els,
				ZFCP_FC_CTELS_TMO);
	if (ret)
		kmem_cache_free(zfcp_data.adisc_cache, adisc);

	return ret;
}

void zfcp_fc_link_test_work(struct work_struct *work)
{
	struct zfcp_port *port =
		container_of(work, struct zfcp_port, test_link_work);
	int retval;

	get_device(&port->sysfs_device);
	port->rport_task = RPORT_DEL;
	zfcp_scsi_rport_work(&port->rport_work);

	/* only issue one test command at one time per port */
	if (atomic_read(&port->status) & ZFCP_STATUS_PORT_LINK_TEST)
		goto out;

	atomic_set_mask(ZFCP_STATUS_PORT_LINK_TEST, &port->status);

	retval = zfcp_fc_adisc(port);
	if (retval == 0)
		return;

	/* send of ADISC was not possible */
	atomic_clear_mask(ZFCP_STATUS_PORT_LINK_TEST, &port->status);
	zfcp_erp_port_forced_reopen(port, 0, "fcltwk1", NULL);

out:
	put_device(&port->sysfs_device);
}

/**
 * zfcp_fc_test_link - lightweight link test procedure
 * @port: port to be tested
 *
 * Test status of a link to a remote port using the ELS command ADISC.
 * If there is a problem with the remote port, error recovery steps
 * will be triggered.
 */
void zfcp_fc_test_link(struct zfcp_port *port)
{
	get_device(&port->sysfs_device);
	if (!queue_work(port->adapter->work_queue, &port->test_link_work))
		put_device(&port->sysfs_device);
}

static void zfcp_free_sg_env(struct zfcp_fc_gpn_ft *gpn_ft, int buf_num)
{
	struct scatterlist *sg = &gpn_ft->sg_req;

	kmem_cache_free(zfcp_data.gpn_ft_cache, sg_virt(sg));
	zfcp_sg_free_table(gpn_ft->sg_resp, buf_num);

	kfree(gpn_ft);
}

static struct zfcp_fc_gpn_ft *zfcp_alloc_sg_env(int buf_num)
{
	struct zfcp_fc_gpn_ft *gpn_ft;
	struct zfcp_fc_gpn_ft_req *req;

	gpn_ft = kzalloc(sizeof(*gpn_ft), GFP_KERNEL);
	if (!gpn_ft)
		return NULL;

	req = kmem_cache_alloc(zfcp_data.gpn_ft_cache, GFP_KERNEL);
	if (!req) {
		kfree(gpn_ft);
		gpn_ft = NULL;
		goto out;
	}
	sg_init_one(&gpn_ft->sg_req, req, sizeof(*req));

	if (zfcp_sg_setup_table(gpn_ft->sg_resp, buf_num)) {
		zfcp_free_sg_env(gpn_ft, buf_num);
		gpn_ft = NULL;
	}
out:
	return gpn_ft;
}


static int zfcp_fc_send_gpn_ft(struct zfcp_fc_gpn_ft *gpn_ft,
			       struct zfcp_adapter *adapter, int max_bytes)
{
	struct zfcp_fsf_ct_els *ct = &gpn_ft->ct;
	struct zfcp_fc_gpn_ft_req *req = sg_virt(&gpn_ft->sg_req);
	DECLARE_COMPLETION_ONSTACK(completion);
	int ret;

	/* prepare CT IU for GPN_FT */
	req->ct_hdr.ct_rev = FC_CT_REV;
	req->ct_hdr.ct_fs_type = FC_FST_DIR;
	req->ct_hdr.ct_fs_subtype = FC_NS_SUBTYPE;
	req->ct_hdr.ct_options = 0;
	req->ct_hdr.ct_cmd = FC_NS_GPN_FT;
	req->ct_hdr.ct_mr_size = max_bytes / 4;
	req->gpn_ft.fn_domain_id_scope = 0;
	req->gpn_ft.fn_area_id_scope = 0;
	req->gpn_ft.fn_fc4_type = FC_TYPE_FCP;

	/* prepare zfcp_send_ct */
	ct->handler = zfcp_fc_complete;
	ct->handler_data = &completion;
	ct->req = &gpn_ft->sg_req;
	ct->resp = gpn_ft->sg_resp;

	ret = zfcp_fsf_send_ct(&adapter->gs->ds, ct, NULL,
			       ZFCP_FC_CTELS_TMO);
	if (!ret)
		wait_for_completion(&completion);
	return ret;
}

static void zfcp_fc_validate_port(struct zfcp_port *port, struct list_head *lh)
{
	if (!(atomic_read(&port->status) & ZFCP_STATUS_COMMON_NOESC))
		return;

	atomic_clear_mask(ZFCP_STATUS_COMMON_NOESC, &port->status);

	if ((port->supported_classes != 0) ||
	    !list_empty(&port->unit_list))
		return;

	list_move_tail(&port->list, lh);
}

static int zfcp_fc_eval_gpn_ft(struct zfcp_fc_gpn_ft *gpn_ft,
			       struct zfcp_adapter *adapter, int max_entries)
{
	struct zfcp_fsf_ct_els *ct = &gpn_ft->ct;
	struct scatterlist *sg = gpn_ft->sg_resp;
	struct fc_ct_hdr *hdr = sg_virt(sg);
	struct fc_gpn_ft_resp *acc = sg_virt(sg);
	struct zfcp_port *port, *tmp;
	unsigned long flags;
	LIST_HEAD(remove_lh);
	u32 d_id;
	int ret = 0, x, last = 0;

	if (ct->status)
		return -EIO;

	if (hdr->ct_cmd != FC_FS_ACC) {
		if (hdr->ct_reason == FC_BA_RJT_UNABLE)
			return -EAGAIN; /* might be a temporary condition */
		return -EIO;
	}

	if (hdr->ct_mr_size) {
		dev_warn(&adapter->ccw_device->dev,
			 "The name server reported %d words residual data\n",
			 hdr->ct_mr_size);
		return -E2BIG;
	}

	/* first entry is the header */
	for (x = 1; x < max_entries && !last; x++) {
		if (x % (ZFCP_FC_GPN_FT_ENT_PAGE + 1))
			acc++;
		else
			acc = sg_virt(++sg);

		last = acc->fp_flags & FC_NS_FID_LAST;
		d_id = ntoh24(acc->fp_fid);

		/* don't attach ports with a well known address */
		if (d_id >= FC_FID_WELL_KNOWN_BASE)
			continue;
		/* skip the adapter's port and known remote ports */
		if (acc->fp_wwpn == fc_host_port_name(adapter->scsi_host))
			continue;

		port = zfcp_port_enqueue(adapter, acc->fp_wwpn,
					 ZFCP_STATUS_COMMON_NOESC, d_id);
		if (!IS_ERR(port))
			zfcp_erp_port_reopen(port, 0, "fcegpf1", NULL);
		else if (PTR_ERR(port) != -EEXIST)
			ret = PTR_ERR(port);
	}

	zfcp_erp_wait(adapter);
	write_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry_safe(port, tmp, &adapter->port_list, list)
		zfcp_fc_validate_port(port, &remove_lh);
	write_unlock_irqrestore(&adapter->port_list_lock, flags);

	list_for_each_entry_safe(port, tmp, &remove_lh, list) {
		zfcp_erp_port_shutdown(port, 0, "fcegpf2", NULL);
		zfcp_device_unregister(&port->sysfs_device,
				       &zfcp_sysfs_port_attrs);
	}

	return ret;
}

/**
 * zfcp_fc_scan_ports - scan remote ports and attach new ports
 * @work: reference to scheduled work
 */
void zfcp_fc_scan_ports(struct work_struct *work)
{
	struct zfcp_adapter *adapter = container_of(work, struct zfcp_adapter,
						    scan_work);
	int ret, i;
	struct zfcp_fc_gpn_ft *gpn_ft;
	int chain, max_entries, buf_num, max_bytes;

	chain = adapter->adapter_features & FSF_FEATURE_ELS_CT_CHAINED_SBALS;
	buf_num = chain ? ZFCP_FC_GPN_FT_NUM_BUFS : 1;
	max_entries = chain ? ZFCP_FC_GPN_FT_MAX_ENT : ZFCP_FC_GPN_FT_ENT_PAGE;
	max_bytes = chain ? ZFCP_FC_GPN_FT_MAX_SIZE : ZFCP_FC_CT_SIZE_PAGE;

	if (fc_host_port_type(adapter->scsi_host) != FC_PORTTYPE_NPORT &&
	    fc_host_port_type(adapter->scsi_host) != FC_PORTTYPE_NPIV)
		return;

	if (zfcp_fc_wka_port_get(&adapter->gs->ds))
		return;

	gpn_ft = zfcp_alloc_sg_env(buf_num);
	if (!gpn_ft)
		goto out;

	for (i = 0; i < 3; i++) {
		ret = zfcp_fc_send_gpn_ft(gpn_ft, adapter, max_bytes);
		if (!ret) {
			ret = zfcp_fc_eval_gpn_ft(gpn_ft, adapter, max_entries);
			if (ret == -EAGAIN)
				ssleep(1);
			else
				break;
		}
	}
	zfcp_free_sg_env(gpn_ft, buf_num);
out:
	zfcp_fc_wka_port_put(&adapter->gs->ds);
}

static void zfcp_fc_ct_els_job_handler(void *data)
{
	struct fc_bsg_job *job = data;
	struct zfcp_fsf_ct_els *zfcp_ct_els = job->dd_data;
	struct fc_bsg_reply *jr = job->reply;

	jr->reply_payload_rcv_len = job->reply_payload.payload_len;
	jr->reply_data.ctels_reply.status = FC_CTELS_STATUS_OK;
	jr->result = zfcp_ct_els->status ? -EIO : 0;
	job->job_done(job);
}

static struct zfcp_fc_wka_port *zfcp_fc_job_wka_port(struct fc_bsg_job *job)
{
	u32 preamble_word1;
	u8 gs_type;
	struct zfcp_adapter *adapter;

	preamble_word1 = job->request->rqst_data.r_ct.preamble_word1;
	gs_type = (preamble_word1 & 0xff000000) >> 24;

	adapter = (struct zfcp_adapter *) job->shost->hostdata[0];

	switch (gs_type) {
	case FC_FST_ALIAS:
		return &adapter->gs->as;
	case FC_FST_MGMT:
		return &adapter->gs->ms;
	case FC_FST_TIME:
		return &adapter->gs->ts;
		break;
	case FC_FST_DIR:
		return &adapter->gs->ds;
		break;
	default:
		return NULL;
	}
}

static void zfcp_fc_ct_job_handler(void *data)
{
	struct fc_bsg_job *job = data;
	struct zfcp_fc_wka_port *wka_port;

	wka_port = zfcp_fc_job_wka_port(job);
	zfcp_fc_wka_port_put(wka_port);

	zfcp_fc_ct_els_job_handler(data);
}

static int zfcp_fc_exec_els_job(struct fc_bsg_job *job,
				struct zfcp_adapter *adapter)
{
	struct zfcp_fsf_ct_els *els = job->dd_data;
	struct fc_rport *rport = job->rport;
	struct zfcp_port *port;
	u32 d_id;

	if (rport) {
		port = zfcp_get_port_by_wwpn(adapter, rport->port_name);
		if (!port)
			return -EINVAL;

		d_id = port->d_id;
		put_device(&port->sysfs_device);
	} else
		d_id = ntoh24(job->request->rqst_data.h_els.port_id);

	els->handler = zfcp_fc_ct_els_job_handler;
	return zfcp_fsf_send_els(adapter, d_id, els, job->req->timeout / HZ);
}

static int zfcp_fc_exec_ct_job(struct fc_bsg_job *job,
			       struct zfcp_adapter *adapter)
{
	int ret;
	struct zfcp_fsf_ct_els *ct = job->dd_data;
	struct zfcp_fc_wka_port *wka_port;

	wka_port = zfcp_fc_job_wka_port(job);
	if (!wka_port)
		return -EINVAL;

	ret = zfcp_fc_wka_port_get(wka_port);
	if (ret)
		return ret;

	ct->handler = zfcp_fc_ct_job_handler;
	ret = zfcp_fsf_send_ct(wka_port, ct, NULL, job->req->timeout / HZ);
	if (ret)
		zfcp_fc_wka_port_put(wka_port);

	return ret;
}

int zfcp_fc_exec_bsg_job(struct fc_bsg_job *job)
{
	struct Scsi_Host *shost;
	struct zfcp_adapter *adapter;
	struct zfcp_fsf_ct_els *ct_els = job->dd_data;

	shost = job->rport ? rport_to_shost(job->rport) : job->shost;
	adapter = (struct zfcp_adapter *)shost->hostdata[0];

	if (!(atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_OPEN))
		return -EINVAL;

	ct_els->req = job->request_payload.sg_list;
	ct_els->resp = job->reply_payload.sg_list;
	ct_els->handler_data = job;

	switch (job->request->msgcode) {
	case FC_BSG_RPT_ELS:
	case FC_BSG_HST_ELS_NOLOGIN:
		return zfcp_fc_exec_els_job(job, adapter);
	case FC_BSG_RPT_CT:
	case FC_BSG_HST_CT:
		return zfcp_fc_exec_ct_job(job, adapter);
	default:
		return -EINVAL;
	}
}

int zfcp_fc_timeout_bsg_job(struct fc_bsg_job *job)
{
	/* hardware tracks timeout, reset bsg timeout to not interfere */
	return -EAGAIN;
}

int zfcp_fc_gs_setup(struct zfcp_adapter *adapter)
{
	struct zfcp_fc_wka_ports *wka_ports;

	wka_ports = kzalloc(sizeof(struct zfcp_fc_wka_ports), GFP_KERNEL);
	if (!wka_ports)
		return -ENOMEM;

	adapter->gs = wka_ports;
	zfcp_fc_wka_port_init(&wka_ports->ms, FC_FID_MGMT_SERV, adapter);
	zfcp_fc_wka_port_init(&wka_ports->ts, FC_FID_TIME_SERV, adapter);
	zfcp_fc_wka_port_init(&wka_ports->ds, FC_FID_DIR_SERV, adapter);
	zfcp_fc_wka_port_init(&wka_ports->as, FC_FID_ALIASES, adapter);

	return 0;
}

void zfcp_fc_gs_destroy(struct zfcp_adapter *adapter)
{
	kfree(adapter->gs);
	adapter->gs = NULL;
}


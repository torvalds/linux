/*
 * zfcp device driver
 *
 * Fibre Channel related functions for the zfcp device driver.
 *
 * Copyright IBM Corp. 2008, 2010
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include <linux/random.h>
#include <linux/bsg-lib.h>
#include <scsi/fc/fc_els.h>
#include <scsi/libfc.h>
#include "zfcp_ext.h"
#include "zfcp_fc.h"

struct kmem_cache *zfcp_fc_req_cache;

static u32 zfcp_fc_rscn_range_mask[] = {
	[ELS_ADDR_FMT_PORT]		= 0xFFFFFF,
	[ELS_ADDR_FMT_AREA]		= 0xFFFF00,
	[ELS_ADDR_FMT_DOM]		= 0xFF0000,
	[ELS_ADDR_FMT_FAB]		= 0x000000,
};

static bool no_auto_port_rescan;
module_param_named(no_auto_port_rescan, no_auto_port_rescan, bool, 0600);
MODULE_PARM_DESC(no_auto_port_rescan,
		 "no automatic port_rescan (default off)");

static unsigned int port_scan_backoff = 500;
module_param(port_scan_backoff, uint, 0600);
MODULE_PARM_DESC(port_scan_backoff,
	"upper limit of port scan random backoff in msecs (default 500)");

static unsigned int port_scan_ratelimit = 60000;
module_param(port_scan_ratelimit, uint, 0600);
MODULE_PARM_DESC(port_scan_ratelimit,
	"minimum interval between port scans in msecs (default 60000)");

unsigned int zfcp_fc_port_scan_backoff(void)
{
	if (!port_scan_backoff)
		return 0;
	return get_random_int() % port_scan_backoff;
}

static void zfcp_fc_port_scan_time(struct zfcp_adapter *adapter)
{
	unsigned long interval = msecs_to_jiffies(port_scan_ratelimit);
	unsigned long backoff = msecs_to_jiffies(zfcp_fc_port_scan_backoff());

	adapter->next_port_scan = jiffies + interval + backoff;
}

static void zfcp_fc_port_scan(struct zfcp_adapter *adapter)
{
	unsigned long now = jiffies;
	unsigned long next = adapter->next_port_scan;
	unsigned long delay = 0, max;

	/* delay only needed within waiting period */
	if (time_before(now, next)) {
		delay = next - now;
		/* paranoia: never ever delay scans longer than specified */
		max = msecs_to_jiffies(port_scan_ratelimit + port_scan_backoff);
		delay = min(delay, max);
	}

	queue_delayed_work(adapter->work_queue, &adapter->scan_work, delay);
}

void zfcp_fc_conditional_port_scan(struct zfcp_adapter *adapter)
{
	if (no_auto_port_rescan)
		return;

	zfcp_fc_port_scan(adapter);
}

void zfcp_fc_inverse_conditional_port_scan(struct zfcp_adapter *adapter)
{
	if (!no_auto_port_rescan)
		return;

	zfcp_fc_port_scan(adapter);
}

/**
 * zfcp_fc_post_event - post event to userspace via fc_transport
 * @work: work struct with enqueued events
 */
void zfcp_fc_post_event(struct work_struct *work)
{
	struct zfcp_fc_event *event = NULL, *tmp = NULL;
	LIST_HEAD(tmp_lh);
	struct zfcp_fc_events *events = container_of(work,
					struct zfcp_fc_events, work);
	struct zfcp_adapter *adapter = container_of(events, struct zfcp_adapter,
						events);

	spin_lock_bh(&events->list_lock);
	list_splice_init(&events->list, &tmp_lh);
	spin_unlock_bh(&events->list_lock);

	list_for_each_entry_safe(event, tmp, &tmp_lh, list) {
		fc_host_post_event(adapter->scsi_host, fc_get_event_number(),
				event->code, event->data);
		list_del(&event->list);
		kfree(event);
	}

}

/**
 * zfcp_fc_enqueue_event - safely enqueue FC HBA API event from irq context
 * @adapter: The adapter where to enqueue the event
 * @event_code: The event code (as defined in fc_host_event_code in
 *		scsi_transport_fc.h)
 * @event_data: The event data (e.g. n_port page in case of els)
 */
void zfcp_fc_enqueue_event(struct zfcp_adapter *adapter,
			enum fc_host_event_code event_code, u32 event_data)
{
	struct zfcp_fc_event *event;

	event = kmalloc(sizeof(struct zfcp_fc_event), GFP_ATOMIC);
	if (!event)
		return;

	event->code = event_code;
	event->data = event_data;

	spin_lock(&adapter->events.list_lock);
	list_add_tail(&event->list, &adapter->events.list);
	spin_unlock(&adapter->events.list_lock);

	queue_work(adapter->work_queue, &adapter->events.work);
}

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
					     "fcrscn1");
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
		zfcp_fc_enqueue_event(fsf_req->adapter, FCH_EVT_RSCN,
				      *(u32 *)page);
	}
	zfcp_fc_conditional_port_scan(fsf_req->adapter);
}

static void zfcp_fc_incoming_wwpn(struct zfcp_fsf_req *req, u64 wwpn)
{
	unsigned long flags;
	struct zfcp_adapter *adapter = req->adapter;
	struct zfcp_port *port;

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list)
		if (port->wwpn == wwpn) {
			zfcp_erp_port_forced_reopen(port, 0, "fciwwp1");
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

	zfcp_dbf_san_in_els("fciels1", fsf_req);
	if (els_type == ELS_PLOGI)
		zfcp_fc_incoming_plogi(fsf_req);
	else if (els_type == ELS_LOGO)
		zfcp_fc_incoming_logo(fsf_req);
	else if (els_type == ELS_RSCN)
		zfcp_fc_incoming_rscn(fsf_req);
}

static void zfcp_fc_ns_gid_pn_eval(struct zfcp_fc_req *fc_req)
{
	struct zfcp_fsf_ct_els *ct_els = &fc_req->ct_els;
	struct zfcp_fc_gid_pn_rsp *gid_pn_rsp = &fc_req->u.gid_pn.rsp;

	if (ct_els->status)
		return;
	if (gid_pn_rsp->ct_hdr.ct_cmd != FC_FS_ACC)
		return;

	/* looks like a valid d_id */
	ct_els->port->d_id = ntoh24(gid_pn_rsp->gid_pn.fp_fid);
}

static void zfcp_fc_complete(void *data)
{
	complete(data);
}

static void zfcp_fc_ct_ns_init(struct fc_ct_hdr *ct_hdr, u16 cmd, u16 mr_size)
{
	ct_hdr->ct_rev = FC_CT_REV;
	ct_hdr->ct_fs_type = FC_FST_DIR;
	ct_hdr->ct_fs_subtype = FC_NS_SUBTYPE;
	ct_hdr->ct_cmd = cmd;
	ct_hdr->ct_mr_size = mr_size / 4;
}

static int zfcp_fc_ns_gid_pn_request(struct zfcp_port *port,
				     struct zfcp_fc_req *fc_req)
{
	struct zfcp_adapter *adapter = port->adapter;
	DECLARE_COMPLETION_ONSTACK(completion);
	struct zfcp_fc_gid_pn_req *gid_pn_req = &fc_req->u.gid_pn.req;
	struct zfcp_fc_gid_pn_rsp *gid_pn_rsp = &fc_req->u.gid_pn.rsp;
	int ret;

	/* setup parameters for send generic command */
	fc_req->ct_els.port = port;
	fc_req->ct_els.handler = zfcp_fc_complete;
	fc_req->ct_els.handler_data = &completion;
	fc_req->ct_els.req = &fc_req->sg_req;
	fc_req->ct_els.resp = &fc_req->sg_rsp;
	sg_init_one(&fc_req->sg_req, gid_pn_req, sizeof(*gid_pn_req));
	sg_init_one(&fc_req->sg_rsp, gid_pn_rsp, sizeof(*gid_pn_rsp));

	zfcp_fc_ct_ns_init(&gid_pn_req->ct_hdr,
			   FC_NS_GID_PN, ZFCP_FC_CT_SIZE_PAGE);
	gid_pn_req->gid_pn.fn_wwpn = port->wwpn;

	ret = zfcp_fsf_send_ct(&adapter->gs->ds, &fc_req->ct_els,
			       adapter->pool.gid_pn_req,
			       ZFCP_FC_CTELS_TMO);
	if (!ret) {
		wait_for_completion(&completion);
		zfcp_fc_ns_gid_pn_eval(fc_req);
	}
	return ret;
}

/**
 * zfcp_fc_ns_gid_pn - initiate GID_PN nameserver request
 * @port: port where GID_PN request is needed
 * return: -ENOMEM on error, 0 otherwise
 */
static int zfcp_fc_ns_gid_pn(struct zfcp_port *port)
{
	int ret;
	struct zfcp_fc_req *fc_req;
	struct zfcp_adapter *adapter = port->adapter;

	fc_req = mempool_alloc(adapter->pool.gid_pn, GFP_ATOMIC);
	if (!fc_req)
		return -ENOMEM;

	memset(fc_req, 0, sizeof(*fc_req));

	ret = zfcp_fc_wka_port_get(&adapter->gs->ds);
	if (ret)
		goto out;

	ret = zfcp_fc_ns_gid_pn_request(port, fc_req);

	zfcp_fc_wka_port_put(&adapter->gs->ds);
out:
	mempool_free(fc_req, adapter->pool.gid_pn);
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
		zfcp_erp_adapter_reopen(port->adapter, 0, "fcgpn_1");
		goto out;
	}

	if (!port->d_id) {
		zfcp_erp_set_port_status(port, ZFCP_STATUS_COMMON_ERP_FAILED);
		goto out;
	}

	zfcp_erp_port_reopen(port, 0, "fcgpn_3");
out:
	put_device(&port->dev);
}

/**
 * zfcp_fc_trigger_did_lookup - trigger the d_id lookup using a GID_PN request
 * @port: The zfcp_port to lookup the d_id for.
 */
void zfcp_fc_trigger_did_lookup(struct zfcp_port *port)
{
	get_device(&port->dev);
	if (!queue_work(port->adapter->work_queue, &port->gid_pn_work))
		put_device(&port->dev);
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
	struct zfcp_fc_req *fc_req = data;
	struct zfcp_port *port = fc_req->ct_els.port;
	struct fc_els_adisc *adisc_resp = &fc_req->u.adisc.rsp;

	if (fc_req->ct_els.status) {
		/* request rejected or timed out */
		zfcp_erp_port_forced_reopen(port, ZFCP_STATUS_COMMON_ERP_FAILED,
					    "fcadh_1");
		goto out;
	}

	if (!port->wwnn)
		port->wwnn = adisc_resp->adisc_wwnn;

	if ((port->wwpn != adisc_resp->adisc_wwpn) ||
	    !(atomic_read(&port->status) & ZFCP_STATUS_COMMON_OPEN)) {
		zfcp_erp_port_reopen(port, ZFCP_STATUS_COMMON_ERP_FAILED,
				     "fcadh_2");
		goto out;
	}

	/* port is good, unblock rport without going through erp */
	zfcp_scsi_schedule_rport_register(port);
 out:
	atomic_andnot(ZFCP_STATUS_PORT_LINK_TEST, &port->status);
	put_device(&port->dev);
	kmem_cache_free(zfcp_fc_req_cache, fc_req);
}

static int zfcp_fc_adisc(struct zfcp_port *port)
{
	struct zfcp_fc_req *fc_req;
	struct zfcp_adapter *adapter = port->adapter;
	struct Scsi_Host *shost = adapter->scsi_host;
	int ret;

	fc_req = kmem_cache_zalloc(zfcp_fc_req_cache, GFP_ATOMIC);
	if (!fc_req)
		return -ENOMEM;

	fc_req->ct_els.port = port;
	fc_req->ct_els.req = &fc_req->sg_req;
	fc_req->ct_els.resp = &fc_req->sg_rsp;
	sg_init_one(&fc_req->sg_req, &fc_req->u.adisc.req,
		    sizeof(struct fc_els_adisc));
	sg_init_one(&fc_req->sg_rsp, &fc_req->u.adisc.rsp,
		    sizeof(struct fc_els_adisc));

	fc_req->ct_els.handler = zfcp_fc_adisc_handler;
	fc_req->ct_els.handler_data = fc_req;

	/* acc. to FC-FS, hard_nport_id in ADISC should not be set for ports
	   without FC-AL-2 capability, so we don't set it */
	fc_req->u.adisc.req.adisc_wwpn = fc_host_port_name(shost);
	fc_req->u.adisc.req.adisc_wwnn = fc_host_node_name(shost);
	fc_req->u.adisc.req.adisc_cmd = ELS_ADISC;
	hton24(fc_req->u.adisc.req.adisc_port_id, fc_host_port_id(shost));

	ret = zfcp_fsf_send_els(adapter, port->d_id, &fc_req->ct_els,
				ZFCP_FC_CTELS_TMO);
	if (ret)
		kmem_cache_free(zfcp_fc_req_cache, fc_req);

	return ret;
}

void zfcp_fc_link_test_work(struct work_struct *work)
{
	struct zfcp_port *port =
		container_of(work, struct zfcp_port, test_link_work);
	int retval;

	get_device(&port->dev);
	port->rport_task = RPORT_DEL;
	zfcp_scsi_rport_work(&port->rport_work);

	/* only issue one test command at one time per port */
	if (atomic_read(&port->status) & ZFCP_STATUS_PORT_LINK_TEST)
		goto out;

	atomic_or(ZFCP_STATUS_PORT_LINK_TEST, &port->status);

	retval = zfcp_fc_adisc(port);
	if (retval == 0)
		return;

	/* send of ADISC was not possible */
	atomic_andnot(ZFCP_STATUS_PORT_LINK_TEST, &port->status);
	zfcp_erp_port_forced_reopen(port, 0, "fcltwk1");

out:
	put_device(&port->dev);
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
	get_device(&port->dev);
	if (!queue_work(port->adapter->work_queue, &port->test_link_work))
		put_device(&port->dev);
}

static struct zfcp_fc_req *zfcp_alloc_sg_env(int buf_num)
{
	struct zfcp_fc_req *fc_req;

	fc_req = kmem_cache_zalloc(zfcp_fc_req_cache, GFP_KERNEL);
	if (!fc_req)
		return NULL;

	if (zfcp_sg_setup_table(&fc_req->sg_rsp, buf_num)) {
		kmem_cache_free(zfcp_fc_req_cache, fc_req);
		return NULL;
	}

	sg_init_one(&fc_req->sg_req, &fc_req->u.gpn_ft.req,
		    sizeof(struct zfcp_fc_gpn_ft_req));

	return fc_req;
}

static int zfcp_fc_send_gpn_ft(struct zfcp_fc_req *fc_req,
			       struct zfcp_adapter *adapter, int max_bytes)
{
	struct zfcp_fsf_ct_els *ct_els = &fc_req->ct_els;
	struct zfcp_fc_gpn_ft_req *req = &fc_req->u.gpn_ft.req;
	DECLARE_COMPLETION_ONSTACK(completion);
	int ret;

	zfcp_fc_ct_ns_init(&req->ct_hdr, FC_NS_GPN_FT, max_bytes);
	req->gpn_ft.fn_fc4_type = FC_TYPE_FCP;

	ct_els->handler = zfcp_fc_complete;
	ct_els->handler_data = &completion;
	ct_els->req = &fc_req->sg_req;
	ct_els->resp = &fc_req->sg_rsp;

	ret = zfcp_fsf_send_ct(&adapter->gs->ds, ct_els, NULL,
			       ZFCP_FC_CTELS_TMO);
	if (!ret)
		wait_for_completion(&completion);
	return ret;
}

static void zfcp_fc_validate_port(struct zfcp_port *port, struct list_head *lh)
{
	if (!(atomic_read(&port->status) & ZFCP_STATUS_COMMON_NOESC))
		return;

	atomic_andnot(ZFCP_STATUS_COMMON_NOESC, &port->status);

	if ((port->supported_classes != 0) ||
	    !list_empty(&port->unit_list))
		return;

	list_move_tail(&port->list, lh);
}

static int zfcp_fc_eval_gpn_ft(struct zfcp_fc_req *fc_req,
			       struct zfcp_adapter *adapter, int max_entries)
{
	struct zfcp_fsf_ct_els *ct_els = &fc_req->ct_els;
	struct scatterlist *sg = &fc_req->sg_rsp;
	struct fc_ct_hdr *hdr = sg_virt(sg);
	struct fc_gpn_ft_resp *acc = sg_virt(sg);
	struct zfcp_port *port, *tmp;
	unsigned long flags;
	LIST_HEAD(remove_lh);
	u32 d_id;
	int ret = 0, x, last = 0;

	if (ct_els->status)
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
			zfcp_erp_port_reopen(port, 0, "fcegpf1");
		else if (PTR_ERR(port) != -EEXIST)
			ret = PTR_ERR(port);
	}

	zfcp_erp_wait(adapter);
	write_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry_safe(port, tmp, &adapter->port_list, list)
		zfcp_fc_validate_port(port, &remove_lh);
	write_unlock_irqrestore(&adapter->port_list_lock, flags);

	list_for_each_entry_safe(port, tmp, &remove_lh, list) {
		zfcp_erp_port_shutdown(port, 0, "fcegpf2");
		device_unregister(&port->dev);
	}

	return ret;
}

/**
 * zfcp_fc_scan_ports - scan remote ports and attach new ports
 * @work: reference to scheduled work
 */
void zfcp_fc_scan_ports(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct zfcp_adapter *adapter = container_of(dw, struct zfcp_adapter,
						    scan_work);
	int ret, i;
	struct zfcp_fc_req *fc_req;
	int chain, max_entries, buf_num, max_bytes;

	zfcp_fc_port_scan_time(adapter);

	chain = adapter->adapter_features & FSF_FEATURE_ELS_CT_CHAINED_SBALS;
	buf_num = chain ? ZFCP_FC_GPN_FT_NUM_BUFS : 1;
	max_entries = chain ? ZFCP_FC_GPN_FT_MAX_ENT : ZFCP_FC_GPN_FT_ENT_PAGE;
	max_bytes = chain ? ZFCP_FC_GPN_FT_MAX_SIZE : ZFCP_FC_CT_SIZE_PAGE;

	if (fc_host_port_type(adapter->scsi_host) != FC_PORTTYPE_NPORT &&
	    fc_host_port_type(adapter->scsi_host) != FC_PORTTYPE_NPIV)
		return;

	if (zfcp_fc_wka_port_get(&adapter->gs->ds))
		return;

	fc_req = zfcp_alloc_sg_env(buf_num);
	if (!fc_req)
		goto out;

	for (i = 0; i < 3; i++) {
		ret = zfcp_fc_send_gpn_ft(fc_req, adapter, max_bytes);
		if (!ret) {
			ret = zfcp_fc_eval_gpn_ft(fc_req, adapter, max_entries);
			if (ret == -EAGAIN)
				ssleep(1);
			else
				break;
		}
	}
	zfcp_sg_free_table(&fc_req->sg_rsp, buf_num);
	kmem_cache_free(zfcp_fc_req_cache, fc_req);
out:
	zfcp_fc_wka_port_put(&adapter->gs->ds);
}

static int zfcp_fc_gspn(struct zfcp_adapter *adapter,
			struct zfcp_fc_req *fc_req)
{
	DECLARE_COMPLETION_ONSTACK(completion);
	char devno[] = "DEVNO:";
	struct zfcp_fsf_ct_els *ct_els = &fc_req->ct_els;
	struct zfcp_fc_gspn_req *gspn_req = &fc_req->u.gspn.req;
	struct zfcp_fc_gspn_rsp *gspn_rsp = &fc_req->u.gspn.rsp;
	int ret;

	zfcp_fc_ct_ns_init(&gspn_req->ct_hdr, FC_NS_GSPN_ID,
			   FC_SYMBOLIC_NAME_SIZE);
	hton24(gspn_req->gspn.fp_fid, fc_host_port_id(adapter->scsi_host));

	sg_init_one(&fc_req->sg_req, gspn_req, sizeof(*gspn_req));
	sg_init_one(&fc_req->sg_rsp, gspn_rsp, sizeof(*gspn_rsp));

	ct_els->handler = zfcp_fc_complete;
	ct_els->handler_data = &completion;
	ct_els->req = &fc_req->sg_req;
	ct_els->resp = &fc_req->sg_rsp;

	ret = zfcp_fsf_send_ct(&adapter->gs->ds, ct_els, NULL,
			       ZFCP_FC_CTELS_TMO);
	if (ret)
		return ret;

	wait_for_completion(&completion);
	if (ct_els->status)
		return ct_els->status;

	if (fc_host_port_type(adapter->scsi_host) == FC_PORTTYPE_NPIV &&
	    !(strstr(gspn_rsp->gspn.fp_name, devno)))
		snprintf(fc_host_symbolic_name(adapter->scsi_host),
			 FC_SYMBOLIC_NAME_SIZE, "%s%s %s NAME: %s",
			 gspn_rsp->gspn.fp_name, devno,
			 dev_name(&adapter->ccw_device->dev),
			 init_utsname()->nodename);
	else
		strlcpy(fc_host_symbolic_name(adapter->scsi_host),
			gspn_rsp->gspn.fp_name, FC_SYMBOLIC_NAME_SIZE);

	return 0;
}

static void zfcp_fc_rspn(struct zfcp_adapter *adapter,
			 struct zfcp_fc_req *fc_req)
{
	DECLARE_COMPLETION_ONSTACK(completion);
	struct Scsi_Host *shost = adapter->scsi_host;
	struct zfcp_fsf_ct_els *ct_els = &fc_req->ct_els;
	struct zfcp_fc_rspn_req *rspn_req = &fc_req->u.rspn.req;
	struct fc_ct_hdr *rspn_rsp = &fc_req->u.rspn.rsp;
	int ret, len;

	zfcp_fc_ct_ns_init(&rspn_req->ct_hdr, FC_NS_RSPN_ID,
			   FC_SYMBOLIC_NAME_SIZE);
	hton24(rspn_req->rspn.fr_fid.fp_fid, fc_host_port_id(shost));
	len = strlcpy(rspn_req->rspn.fr_name, fc_host_symbolic_name(shost),
		      FC_SYMBOLIC_NAME_SIZE);
	rspn_req->rspn.fr_name_len = len;

	sg_init_one(&fc_req->sg_req, rspn_req, sizeof(*rspn_req));
	sg_init_one(&fc_req->sg_rsp, rspn_rsp, sizeof(*rspn_rsp));

	ct_els->handler = zfcp_fc_complete;
	ct_els->handler_data = &completion;
	ct_els->req = &fc_req->sg_req;
	ct_els->resp = &fc_req->sg_rsp;

	ret = zfcp_fsf_send_ct(&adapter->gs->ds, ct_els, NULL,
			       ZFCP_FC_CTELS_TMO);
	if (!ret)
		wait_for_completion(&completion);
}

/**
 * zfcp_fc_sym_name_update - Retrieve and update the symbolic port name
 * @work: ns_up_work of the adapter where to update the symbolic port name
 *
 * Retrieve the current symbolic port name that may have been set by
 * the hardware using the GSPN request and update the fc_host
 * symbolic_name sysfs attribute. When running in NPIV mode (and hence
 * the port name is unique for this system), update the symbolic port
 * name to add Linux specific information and update the FC nameserver
 * using the RSPN request.
 */
void zfcp_fc_sym_name_update(struct work_struct *work)
{
	struct zfcp_adapter *adapter = container_of(work, struct zfcp_adapter,
						    ns_up_work);
	int ret;
	struct zfcp_fc_req *fc_req;

	if (fc_host_port_type(adapter->scsi_host) != FC_PORTTYPE_NPORT &&
	    fc_host_port_type(adapter->scsi_host) != FC_PORTTYPE_NPIV)
		return;

	fc_req = kmem_cache_zalloc(zfcp_fc_req_cache, GFP_KERNEL);
	if (!fc_req)
		return;

	ret = zfcp_fc_wka_port_get(&adapter->gs->ds);
	if (ret)
		goto out_free;

	ret = zfcp_fc_gspn(adapter, fc_req);
	if (ret || fc_host_port_type(adapter->scsi_host) != FC_PORTTYPE_NPIV)
		goto out_ds_put;

	memset(fc_req, 0, sizeof(*fc_req));
	zfcp_fc_rspn(adapter, fc_req);

out_ds_put:
	zfcp_fc_wka_port_put(&adapter->gs->ds);
out_free:
	kmem_cache_free(zfcp_fc_req_cache, fc_req);
}

static void zfcp_fc_ct_els_job_handler(void *data)
{
	struct bsg_job *job = data;
	struct zfcp_fsf_ct_els *zfcp_ct_els = job->dd_data;
	struct fc_bsg_reply *jr = job->reply;

	jr->reply_payload_rcv_len = job->reply_payload.payload_len;
	jr->reply_data.ctels_reply.status = FC_CTELS_STATUS_OK;
	jr->result = zfcp_ct_els->status ? -EIO : 0;
	bsg_job_done(job, jr->result, jr->reply_payload_rcv_len);
}

static struct zfcp_fc_wka_port *zfcp_fc_job_wka_port(struct bsg_job *job)
{
	u32 preamble_word1;
	u8 gs_type;
	struct zfcp_adapter *adapter;
	struct fc_bsg_request *bsg_request = job->request;
	struct fc_rport *rport = fc_bsg_to_rport(job);
	struct Scsi_Host *shost;

	preamble_word1 = bsg_request->rqst_data.r_ct.preamble_word1;
	gs_type = (preamble_word1 & 0xff000000) >> 24;

	shost = rport ? rport_to_shost(rport) : fc_bsg_to_shost(job);
	adapter = (struct zfcp_adapter *) shost->hostdata[0];

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
	struct bsg_job *job = data;
	struct zfcp_fc_wka_port *wka_port;

	wka_port = zfcp_fc_job_wka_port(job);
	zfcp_fc_wka_port_put(wka_port);

	zfcp_fc_ct_els_job_handler(data);
}

static int zfcp_fc_exec_els_job(struct bsg_job *job,
				struct zfcp_adapter *adapter)
{
	struct zfcp_fsf_ct_els *els = job->dd_data;
	struct fc_rport *rport = fc_bsg_to_rport(job);
	struct fc_bsg_request *bsg_request = job->request;
	struct zfcp_port *port;
	u32 d_id;

	if (rport) {
		port = zfcp_get_port_by_wwpn(adapter, rport->port_name);
		if (!port)
			return -EINVAL;

		d_id = port->d_id;
		put_device(&port->dev);
	} else
		d_id = ntoh24(bsg_request->rqst_data.h_els.port_id);

	els->handler = zfcp_fc_ct_els_job_handler;
	return zfcp_fsf_send_els(adapter, d_id, els, job->req->timeout / HZ);
}

static int zfcp_fc_exec_ct_job(struct bsg_job *job,
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

int zfcp_fc_exec_bsg_job(struct bsg_job *job)
{
	struct Scsi_Host *shost;
	struct zfcp_adapter *adapter;
	struct zfcp_fsf_ct_els *ct_els = job->dd_data;
	struct fc_bsg_request *bsg_request = job->request;
	struct fc_rport *rport = fc_bsg_to_rport(job);

	shost = rport ? rport_to_shost(rport) : fc_bsg_to_shost(job);
	adapter = (struct zfcp_adapter *)shost->hostdata[0];

	if (!(atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_OPEN))
		return -EINVAL;

	ct_els->req = job->request_payload.sg_list;
	ct_els->resp = job->reply_payload.sg_list;
	ct_els->handler_data = job;

	switch (bsg_request->msgcode) {
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

int zfcp_fc_timeout_bsg_job(struct bsg_job *job)
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


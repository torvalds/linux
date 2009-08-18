/*
 * zfcp device driver
 *
 * Setup and helper functions to access QDIO.
 *
 * Copyright IBM Corporation 2002, 2009
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include "zfcp_ext.h"

#define QBUFF_PER_PAGE		(PAGE_SIZE / sizeof(struct qdio_buffer))

static int zfcp_qdio_buffers_enqueue(struct qdio_buffer **sbal)
{
	int pos;

	for (pos = 0; pos < QDIO_MAX_BUFFERS_PER_Q; pos += QBUFF_PER_PAGE) {
		sbal[pos] = (struct qdio_buffer *) get_zeroed_page(GFP_KERNEL);
		if (!sbal[pos])
			return -ENOMEM;
	}
	for (pos = 0; pos < QDIO_MAX_BUFFERS_PER_Q; pos++)
		if (pos % QBUFF_PER_PAGE)
			sbal[pos] = sbal[pos - 1] + 1;
	return 0;
}

static struct qdio_buffer_element *
zfcp_qdio_sbale(struct zfcp_qdio_queue *q, int sbal_idx, int sbale_idx)
{
	return &q->sbal[sbal_idx]->element[sbale_idx];
}

/**
 * zfcp_qdio_free - free memory used by request- and resposne queue
 * @adapter: pointer to the zfcp_adapter structure
 */
void zfcp_qdio_free(struct zfcp_adapter *adapter)
{
	struct qdio_buffer **sbal_req, **sbal_resp;
	int p;

	if (adapter->ccw_device)
		qdio_free(adapter->ccw_device);

	sbal_req = adapter->req_q.sbal;
	sbal_resp = adapter->resp_q.sbal;

	for (p = 0; p < QDIO_MAX_BUFFERS_PER_Q; p += QBUFF_PER_PAGE) {
		free_page((unsigned long) sbal_req[p]);
		free_page((unsigned long) sbal_resp[p]);
	}
}

static void zfcp_qdio_handler_error(struct zfcp_adapter *adapter, char *id)
{
	dev_warn(&adapter->ccw_device->dev, "A QDIO problem occurred\n");

	zfcp_erp_adapter_reopen(adapter,
				ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
				ZFCP_STATUS_COMMON_ERP_FAILED, id, NULL);
}

static void zfcp_qdio_zero_sbals(struct qdio_buffer *sbal[], int first, int cnt)
{
	int i, sbal_idx;

	for (i = first; i < first + cnt; i++) {
		sbal_idx = i % QDIO_MAX_BUFFERS_PER_Q;
		memset(sbal[sbal_idx], 0, sizeof(struct qdio_buffer));
	}
}

/* this needs to be called prior to updating the queue fill level */
static void zfcp_qdio_account(struct zfcp_adapter *adapter)
{
	ktime_t now;
	s64 span;
	int free, used;

	spin_lock(&adapter->qdio_stat_lock);
	now = ktime_get();
	span = ktime_us_delta(now, adapter->req_q_time);
	free = max(0, atomic_read(&adapter->req_q.count));
	used = QDIO_MAX_BUFFERS_PER_Q - free;
	adapter->req_q_util += used * span;
	adapter->req_q_time = now;
	spin_unlock(&adapter->qdio_stat_lock);
}

static void zfcp_qdio_int_req(struct ccw_device *cdev, unsigned int qdio_err,
			      int queue_no, int first, int count,
			      unsigned long parm)
{
	struct zfcp_adapter *adapter = (struct zfcp_adapter *) parm;
	struct zfcp_qdio_queue *queue = &adapter->req_q;

	if (unlikely(qdio_err)) {
		zfcp_hba_dbf_event_qdio(adapter, qdio_err, first, count);
		zfcp_qdio_handler_error(adapter, "qdireq1");
		return;
	}

	/* cleanup all SBALs being program-owned now */
	zfcp_qdio_zero_sbals(queue->sbal, first, count);

	zfcp_qdio_account(adapter);
	atomic_add(count, &queue->count);
	wake_up(&adapter->request_wq);
}

static void zfcp_qdio_resp_put_back(struct zfcp_adapter *adapter, int processed)
{
	struct zfcp_qdio_queue *queue = &adapter->resp_q;
	struct ccw_device *cdev = adapter->ccw_device;
	u8 count, start = queue->first;
	unsigned int retval;

	count = atomic_read(&queue->count) + processed;

	retval = do_QDIO(cdev, QDIO_FLAG_SYNC_INPUT, 0, start, count);

	if (unlikely(retval)) {
		atomic_set(&queue->count, count);
		/* FIXME: Recover this with an adapter reopen? */
	} else {
		queue->first += count;
		queue->first %= QDIO_MAX_BUFFERS_PER_Q;
		atomic_set(&queue->count, 0);
	}
}

static void zfcp_qdio_int_resp(struct ccw_device *cdev, unsigned int qdio_err,
			       int queue_no, int first, int count,
			       unsigned long parm)
{
	struct zfcp_adapter *adapter = (struct zfcp_adapter *) parm;
	int sbal_idx, sbal_no;

	if (unlikely(qdio_err)) {
		zfcp_hba_dbf_event_qdio(adapter, qdio_err, first, count);
		zfcp_qdio_handler_error(adapter, "qdires1");
		return;
	}

	/*
	 * go through all SBALs from input queue currently
	 * returned by QDIO layer
	 */
	for (sbal_no = 0; sbal_no < count; sbal_no++) {
		sbal_idx = (first + sbal_no) % QDIO_MAX_BUFFERS_PER_Q;
		/* go through all SBALEs of SBAL */
		zfcp_fsf_reqid_check(adapter, sbal_idx);
	}

	/*
	 * put range of SBALs back to response queue
	 * (including SBALs which have already been free before)
	 */
	zfcp_qdio_resp_put_back(adapter, count);
}

/**
 * zfcp_qdio_sbale_req - return ptr to SBALE of req_q for a struct zfcp_fsf_req
 * @adapter: pointer to struct zfcp_adapter
 * @q_rec: pointer to struct zfcp_queue_rec
 * Returns: pointer to qdio_buffer_element (SBALE) structure
 */
struct qdio_buffer_element *zfcp_qdio_sbale_req(struct zfcp_adapter *adapter,
						struct zfcp_queue_req *q_req)
{
	return zfcp_qdio_sbale(&adapter->req_q, q_req->sbal_last, 0);
}

/**
 * zfcp_qdio_sbale_curr - return curr SBALE on req_q for a struct zfcp_fsf_req
 * @fsf_req: pointer to struct fsf_req
 * Returns: pointer to qdio_buffer_element (SBALE) structure
 */
struct qdio_buffer_element *zfcp_qdio_sbale_curr(struct zfcp_adapter *adapter,
						 struct zfcp_queue_req *q_req)
{
	return zfcp_qdio_sbale(&adapter->req_q, q_req->sbal_last,
			       q_req->sbale_curr);
}

static void zfcp_qdio_sbal_limit(struct zfcp_adapter *adapter,
				 struct zfcp_queue_req *q_req, int max_sbals)
{
	int count = atomic_read(&adapter->req_q.count);
	count = min(count, max_sbals);
	q_req->sbal_limit = (q_req->sbal_first + count - 1)
					% QDIO_MAX_BUFFERS_PER_Q;
}

static struct qdio_buffer_element *
zfcp_qdio_sbal_chain(struct zfcp_adapter *adapter, struct zfcp_queue_req *q_req,
		     unsigned long sbtype)
{
	struct qdio_buffer_element *sbale;

	/* set last entry flag in current SBALE of current SBAL */
	sbale = zfcp_qdio_sbale_curr(adapter, q_req);
	sbale->flags |= SBAL_FLAGS_LAST_ENTRY;

	/* don't exceed last allowed SBAL */
	if (q_req->sbal_last == q_req->sbal_limit)
		return NULL;

	/* set chaining flag in first SBALE of current SBAL */
	sbale = zfcp_qdio_sbale_req(adapter, q_req);
	sbale->flags |= SBAL_FLAGS0_MORE_SBALS;

	/* calculate index of next SBAL */
	q_req->sbal_last++;
	q_req->sbal_last %= QDIO_MAX_BUFFERS_PER_Q;

	/* keep this requests number of SBALs up-to-date */
	q_req->sbal_number++;

	/* start at first SBALE of new SBAL */
	q_req->sbale_curr = 0;

	/* set storage-block type for new SBAL */
	sbale = zfcp_qdio_sbale_curr(adapter, q_req);
	sbale->flags |= sbtype;

	return sbale;
}

static struct qdio_buffer_element *
zfcp_qdio_sbale_next(struct zfcp_adapter *adapter, struct zfcp_queue_req *q_req,
		     unsigned int sbtype)
{
	if (q_req->sbale_curr == ZFCP_LAST_SBALE_PER_SBAL)
		return zfcp_qdio_sbal_chain(adapter, q_req, sbtype);
	q_req->sbale_curr++;
	return zfcp_qdio_sbale_curr(adapter, q_req);
}

static void zfcp_qdio_undo_sbals(struct zfcp_adapter *adapter,
				 struct zfcp_queue_req *q_req)
{
	struct qdio_buffer **sbal = adapter->req_q.sbal;
	int first = q_req->sbal_first;
	int last = q_req->sbal_last;
	int count = (last - first + QDIO_MAX_BUFFERS_PER_Q) %
		QDIO_MAX_BUFFERS_PER_Q + 1;
	zfcp_qdio_zero_sbals(sbal, first, count);
}

static int zfcp_qdio_fill_sbals(struct zfcp_adapter *adapter,
				struct zfcp_queue_req *q_req,
				unsigned int sbtype, void *start_addr,
				unsigned int total_length)
{
	struct qdio_buffer_element *sbale;
	unsigned long remaining, length;
	void *addr;

	/* split segment up */
	for (addr = start_addr, remaining = total_length; remaining > 0;
	     addr += length, remaining -= length) {
		sbale = zfcp_qdio_sbale_next(adapter, q_req, sbtype);
		if (!sbale) {
			atomic_inc(&adapter->qdio_outb_full);
			zfcp_qdio_undo_sbals(adapter, q_req);
			return -EINVAL;
		}

		/* new piece must not exceed next page boundary */
		length = min(remaining,
			     (PAGE_SIZE - ((unsigned long)addr &
					   (PAGE_SIZE - 1))));
		sbale->addr = addr;
		sbale->length = length;
	}
	return 0;
}

/**
 * zfcp_qdio_sbals_from_sg - fill SBALs from scatter-gather list
 * @fsf_req: request to be processed
 * @sbtype: SBALE flags
 * @sg: scatter-gather list
 * @max_sbals: upper bound for number of SBALs to be used
 * Returns: number of bytes, or error (negativ)
 */
int zfcp_qdio_sbals_from_sg(struct zfcp_adapter *adapter,
			    struct zfcp_queue_req *q_req,
			    unsigned long sbtype, struct scatterlist *sg,
			    int max_sbals)
{
	struct qdio_buffer_element *sbale;
	int retval, bytes = 0;

	/* figure out last allowed SBAL */
	zfcp_qdio_sbal_limit(adapter, q_req, max_sbals);

	/* set storage-block type for this request */
	sbale = zfcp_qdio_sbale_req(adapter, q_req);
	sbale->flags |= sbtype;

	for (; sg; sg = sg_next(sg)) {
		retval = zfcp_qdio_fill_sbals(adapter, q_req, sbtype,
					      sg_virt(sg), sg->length);
		if (retval < 0)
			return retval;
		bytes += sg->length;
	}

	/* assume that no other SBALEs are to follow in the same SBAL */
	sbale = zfcp_qdio_sbale_curr(adapter, q_req);
	sbale->flags |= SBAL_FLAGS_LAST_ENTRY;

	return bytes;
}

/**
 * zfcp_qdio_send - set PCI flag in first SBALE and send req to QDIO
 * @fsf_req: pointer to struct zfcp_fsf_req
 * Returns: 0 on success, error otherwise
 */
int zfcp_qdio_send(struct zfcp_adapter *adapter, struct zfcp_queue_req *q_req)
{
	struct zfcp_qdio_queue *req_q = &adapter->req_q;
	int first = q_req->sbal_first;
	int count = q_req->sbal_number;
	int retval;
	unsigned int qdio_flags = QDIO_FLAG_SYNC_OUTPUT;

	zfcp_qdio_account(adapter);

	retval = do_QDIO(adapter->ccw_device, qdio_flags, 0, first, count);
	if (unlikely(retval)) {
		zfcp_qdio_zero_sbals(req_q->sbal, first, count);
		return retval;
	}

	/* account for transferred buffers */
	atomic_sub(count, &req_q->count);
	req_q->first += count;
	req_q->first %= QDIO_MAX_BUFFERS_PER_Q;
	return 0;
}

/**
 * zfcp_qdio_allocate - allocate queue memory and initialize QDIO data
 * @adapter: pointer to struct zfcp_adapter
 * Returns: -ENOMEM on memory allocation error or return value from
 *          qdio_allocate
 */
int zfcp_qdio_allocate(struct zfcp_adapter *adapter)
{
	struct qdio_initialize *init_data;

	if (zfcp_qdio_buffers_enqueue(adapter->req_q.sbal) ||
		   zfcp_qdio_buffers_enqueue(adapter->resp_q.sbal))
		return -ENOMEM;

	init_data = &adapter->qdio_init_data;

	init_data->cdev = adapter->ccw_device;
	init_data->q_format = QDIO_ZFCP_QFMT;
	memcpy(init_data->adapter_name, dev_name(&adapter->ccw_device->dev), 8);
	ASCEBC(init_data->adapter_name, 8);
	init_data->qib_param_field_format = 0;
	init_data->qib_param_field = NULL;
	init_data->input_slib_elements = NULL;
	init_data->output_slib_elements = NULL;
	init_data->no_input_qs = 1;
	init_data->no_output_qs = 1;
	init_data->input_handler = zfcp_qdio_int_resp;
	init_data->output_handler = zfcp_qdio_int_req;
	init_data->int_parm = (unsigned long) adapter;
	init_data->flags = QDIO_INBOUND_0COPY_SBALS |
			QDIO_OUTBOUND_0COPY_SBALS | QDIO_USE_OUTBOUND_PCIS;
	init_data->input_sbal_addr_array =
			(void **) (adapter->resp_q.sbal);
	init_data->output_sbal_addr_array =
			(void **) (adapter->req_q.sbal);

	return qdio_allocate(init_data);
}

/**
 * zfcp_close_qdio - close qdio queues for an adapter
 */
void zfcp_qdio_close(struct zfcp_adapter *adapter)
{
	struct zfcp_qdio_queue *req_q;
	int first, count;

	if (!(atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_QDIOUP))
		return;

	/* clear QDIOUP flag, thus do_QDIO is not called during qdio_shutdown */
	req_q = &adapter->req_q;
	spin_lock_bh(&adapter->req_q_lock);
	atomic_clear_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status);
	spin_unlock_bh(&adapter->req_q_lock);

	qdio_shutdown(adapter->ccw_device, QDIO_FLAG_CLEANUP_USING_CLEAR);

	/* cleanup used outbound sbals */
	count = atomic_read(&req_q->count);
	if (count < QDIO_MAX_BUFFERS_PER_Q) {
		first = (req_q->first + count) % QDIO_MAX_BUFFERS_PER_Q;
		count = QDIO_MAX_BUFFERS_PER_Q - count;
		zfcp_qdio_zero_sbals(req_q->sbal, first, count);
	}
	req_q->first = 0;
	atomic_set(&req_q->count, 0);
	adapter->resp_q.first = 0;
	atomic_set(&adapter->resp_q.count, 0);
}

/**
 * zfcp_qdio_open - prepare and initialize response queue
 * @adapter: pointer to struct zfcp_adapter
 * Returns: 0 on success, otherwise -EIO
 */
int zfcp_qdio_open(struct zfcp_adapter *adapter)
{
	struct qdio_buffer_element *sbale;
	int cc;

	if (atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_QDIOUP)
		return -EIO;

	if (qdio_establish(&adapter->qdio_init_data))
		goto failed_establish;

	if (qdio_activate(adapter->ccw_device))
		goto failed_qdio;

	for (cc = 0; cc < QDIO_MAX_BUFFERS_PER_Q; cc++) {
		sbale = &(adapter->resp_q.sbal[cc]->element[0]);
		sbale->length = 0;
		sbale->flags = SBAL_FLAGS_LAST_ENTRY;
		sbale->addr = NULL;
	}

	if (do_QDIO(adapter->ccw_device, QDIO_FLAG_SYNC_INPUT, 0, 0,
		     QDIO_MAX_BUFFERS_PER_Q))
		goto failed_qdio;

	/* set index of first avalable SBALS / number of available SBALS */
	adapter->req_q.first = 0;
	atomic_set(&adapter->req_q.count, QDIO_MAX_BUFFERS_PER_Q);

	return 0;

failed_qdio:
	qdio_shutdown(adapter->ccw_device, QDIO_FLAG_CLEANUP_USING_CLEAR);
failed_establish:
	dev_err(&adapter->ccw_device->dev,
		"Setting up the QDIO connection to the FCP adapter failed\n");
	return -EIO;
}

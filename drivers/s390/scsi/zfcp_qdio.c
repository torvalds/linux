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
#include "zfcp_qdio.h"

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

static void zfcp_qdio_handler_error(struct zfcp_qdio *qdio, char *id)
{
	struct zfcp_adapter *adapter = qdio->adapter;

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
static inline void zfcp_qdio_account(struct zfcp_qdio *qdio)
{
	unsigned long long now, span;
	int free, used;

	spin_lock(&qdio->stat_lock);
	now = get_clock_monotonic();
	span = (now - qdio->req_q_time) >> 12;
	free = atomic_read(&qdio->req_q.count);
	used = QDIO_MAX_BUFFERS_PER_Q - free;
	qdio->req_q_util += used * span;
	qdio->req_q_time = now;
	spin_unlock(&qdio->stat_lock);
}

static void zfcp_qdio_int_req(struct ccw_device *cdev, unsigned int qdio_err,
			      int queue_no, int first, int count,
			      unsigned long parm)
{
	struct zfcp_qdio *qdio = (struct zfcp_qdio *) parm;
	struct zfcp_qdio_queue *queue = &qdio->req_q;

	if (unlikely(qdio_err)) {
		zfcp_dbf_hba_qdio(qdio->adapter->dbf, qdio_err, first,
					count);
		zfcp_qdio_handler_error(qdio, "qdireq1");
		return;
	}

	/* cleanup all SBALs being program-owned now */
	zfcp_qdio_zero_sbals(queue->sbal, first, count);

	zfcp_qdio_account(qdio);
	atomic_add(count, &queue->count);
	wake_up(&qdio->req_q_wq);
}

static void zfcp_qdio_resp_put_back(struct zfcp_qdio *qdio, int processed)
{
	struct zfcp_qdio_queue *queue = &qdio->resp_q;
	struct ccw_device *cdev = qdio->adapter->ccw_device;
	u8 count, start = queue->first;
	unsigned int retval;

	count = atomic_read(&queue->count) + processed;

	retval = do_QDIO(cdev, QDIO_FLAG_SYNC_INPUT, 0, start, count);

	if (unlikely(retval)) {
		atomic_set(&queue->count, count);
		zfcp_erp_adapter_reopen(qdio->adapter, 0, "qdrpb_1", NULL);
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
	struct zfcp_qdio *qdio = (struct zfcp_qdio *) parm;
	int sbal_idx, sbal_no;

	if (unlikely(qdio_err)) {
		zfcp_dbf_hba_qdio(qdio->adapter->dbf, qdio_err, first,
					count);
		zfcp_qdio_handler_error(qdio, "qdires1");
		return;
	}

	/*
	 * go through all SBALs from input queue currently
	 * returned by QDIO layer
	 */
	for (sbal_no = 0; sbal_no < count; sbal_no++) {
		sbal_idx = (first + sbal_no) % QDIO_MAX_BUFFERS_PER_Q;
		/* go through all SBALEs of SBAL */
		zfcp_fsf_reqid_check(qdio, sbal_idx);
	}

	/*
	 * put range of SBALs back to response queue
	 * (including SBALs which have already been free before)
	 */
	zfcp_qdio_resp_put_back(qdio, count);
}

static void zfcp_qdio_sbal_limit(struct zfcp_qdio *qdio,
				 struct zfcp_qdio_req *q_req, int max_sbals)
{
	int count = atomic_read(&qdio->req_q.count);
	count = min(count, max_sbals);
	q_req->sbal_limit = (q_req->sbal_first + count - 1)
					% QDIO_MAX_BUFFERS_PER_Q;
}

static struct qdio_buffer_element *
zfcp_qdio_sbal_chain(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req,
		     unsigned long sbtype)
{
	struct qdio_buffer_element *sbale;

	/* set last entry flag in current SBALE of current SBAL */
	sbale = zfcp_qdio_sbale_curr(qdio, q_req);
	sbale->flags |= SBAL_FLAGS_LAST_ENTRY;

	/* don't exceed last allowed SBAL */
	if (q_req->sbal_last == q_req->sbal_limit)
		return NULL;

	/* set chaining flag in first SBALE of current SBAL */
	sbale = zfcp_qdio_sbale_req(qdio, q_req);
	sbale->flags |= SBAL_FLAGS0_MORE_SBALS;

	/* calculate index of next SBAL */
	q_req->sbal_last++;
	q_req->sbal_last %= QDIO_MAX_BUFFERS_PER_Q;

	/* keep this requests number of SBALs up-to-date */
	q_req->sbal_number++;

	/* start at first SBALE of new SBAL */
	q_req->sbale_curr = 0;

	/* set storage-block type for new SBAL */
	sbale = zfcp_qdio_sbale_curr(qdio, q_req);
	sbale->flags |= sbtype;

	return sbale;
}

static struct qdio_buffer_element *
zfcp_qdio_sbale_next(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req,
		     unsigned int sbtype)
{
	if (q_req->sbale_curr == ZFCP_LAST_SBALE_PER_SBAL)
		return zfcp_qdio_sbal_chain(qdio, q_req, sbtype);
	q_req->sbale_curr++;
	return zfcp_qdio_sbale_curr(qdio, q_req);
}

static void zfcp_qdio_undo_sbals(struct zfcp_qdio *qdio,
				 struct zfcp_qdio_req *q_req)
{
	struct qdio_buffer **sbal = qdio->req_q.sbal;
	int first = q_req->sbal_first;
	int last = q_req->sbal_last;
	int count = (last - first + QDIO_MAX_BUFFERS_PER_Q) %
		QDIO_MAX_BUFFERS_PER_Q + 1;
	zfcp_qdio_zero_sbals(sbal, first, count);
}

static int zfcp_qdio_fill_sbals(struct zfcp_qdio *qdio,
				struct zfcp_qdio_req *q_req,
				unsigned int sbtype, void *start_addr,
				unsigned int total_length)
{
	struct qdio_buffer_element *sbale;
	unsigned long remaining, length;
	void *addr;

	/* split segment up */
	for (addr = start_addr, remaining = total_length; remaining > 0;
	     addr += length, remaining -= length) {
		sbale = zfcp_qdio_sbale_next(qdio, q_req, sbtype);
		if (!sbale) {
			atomic_inc(&qdio->req_q_full);
			zfcp_qdio_undo_sbals(qdio, q_req);
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
int zfcp_qdio_sbals_from_sg(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req,
			    unsigned long sbtype, struct scatterlist *sg,
			    int max_sbals)
{
	struct qdio_buffer_element *sbale;
	int retval, bytes = 0;

	/* figure out last allowed SBAL */
	zfcp_qdio_sbal_limit(qdio, q_req, max_sbals);

	/* set storage-block type for this request */
	sbale = zfcp_qdio_sbale_req(qdio, q_req);
	sbale->flags |= sbtype;

	for (; sg; sg = sg_next(sg)) {
		retval = zfcp_qdio_fill_sbals(qdio, q_req, sbtype,
					      sg_virt(sg), sg->length);
		if (retval < 0)
			return retval;
		bytes += sg->length;
	}

	/* assume that no other SBALEs are to follow in the same SBAL */
	sbale = zfcp_qdio_sbale_curr(qdio, q_req);
	sbale->flags |= SBAL_FLAGS_LAST_ENTRY;

	return bytes;
}

/**
 * zfcp_qdio_send - set PCI flag in first SBALE and send req to QDIO
 * @qdio: pointer to struct zfcp_qdio
 * @q_req: pointer to struct zfcp_qdio_req
 * Returns: 0 on success, error otherwise
 */
int zfcp_qdio_send(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req)
{
	struct zfcp_qdio_queue *req_q = &qdio->req_q;
	int first = q_req->sbal_first;
	int count = q_req->sbal_number;
	int retval;
	unsigned int qdio_flags = QDIO_FLAG_SYNC_OUTPUT;

	zfcp_qdio_account(qdio);

	retval = do_QDIO(qdio->adapter->ccw_device, qdio_flags, 0, first,
			 count);
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


static void zfcp_qdio_setup_init_data(struct qdio_initialize *id,
				      struct zfcp_qdio *qdio)
{

	id->cdev = qdio->adapter->ccw_device;
	id->q_format = QDIO_ZFCP_QFMT;
	memcpy(id->adapter_name, dev_name(&id->cdev->dev), 8);
	ASCEBC(id->adapter_name, 8);
	id->qib_param_field_format = 0;
	id->qib_param_field = NULL;
	id->input_slib_elements = NULL;
	id->output_slib_elements = NULL;
	id->no_input_qs = 1;
	id->no_output_qs = 1;
	id->input_handler = zfcp_qdio_int_resp;
	id->output_handler = zfcp_qdio_int_req;
	id->int_parm = (unsigned long) qdio;
	id->flags = QDIO_INBOUND_0COPY_SBALS |
		    QDIO_OUTBOUND_0COPY_SBALS | QDIO_USE_OUTBOUND_PCIS;
	id->input_sbal_addr_array = (void **) (qdio->resp_q.sbal);
	id->output_sbal_addr_array = (void **) (qdio->req_q.sbal);

}
/**
 * zfcp_qdio_allocate - allocate queue memory and initialize QDIO data
 * @adapter: pointer to struct zfcp_adapter
 * Returns: -ENOMEM on memory allocation error or return value from
 *          qdio_allocate
 */
static int zfcp_qdio_allocate(struct zfcp_qdio *qdio)
{
	struct qdio_initialize init_data;

	if (zfcp_qdio_buffers_enqueue(qdio->req_q.sbal) ||
	    zfcp_qdio_buffers_enqueue(qdio->resp_q.sbal))
		return -ENOMEM;

	zfcp_qdio_setup_init_data(&init_data, qdio);

	return qdio_allocate(&init_data);
}

/**
 * zfcp_close_qdio - close qdio queues for an adapter
 * @qdio: pointer to structure zfcp_qdio
 */
void zfcp_qdio_close(struct zfcp_qdio *qdio)
{
	struct zfcp_qdio_queue *req_q;
	int first, count;

	if (!(atomic_read(&qdio->adapter->status) & ZFCP_STATUS_ADAPTER_QDIOUP))
		return;

	/* clear QDIOUP flag, thus do_QDIO is not called during qdio_shutdown */
	req_q = &qdio->req_q;
	spin_lock_bh(&qdio->req_q_lock);
	atomic_clear_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &qdio->adapter->status);
	spin_unlock_bh(&qdio->req_q_lock);

	qdio_shutdown(qdio->adapter->ccw_device,
		      QDIO_FLAG_CLEANUP_USING_CLEAR);

	/* cleanup used outbound sbals */
	count = atomic_read(&req_q->count);
	if (count < QDIO_MAX_BUFFERS_PER_Q) {
		first = (req_q->first + count) % QDIO_MAX_BUFFERS_PER_Q;
		count = QDIO_MAX_BUFFERS_PER_Q - count;
		zfcp_qdio_zero_sbals(req_q->sbal, first, count);
	}
	req_q->first = 0;
	atomic_set(&req_q->count, 0);
	qdio->resp_q.first = 0;
	atomic_set(&qdio->resp_q.count, 0);
}

/**
 * zfcp_qdio_open - prepare and initialize response queue
 * @qdio: pointer to struct zfcp_qdio
 * Returns: 0 on success, otherwise -EIO
 */
int zfcp_qdio_open(struct zfcp_qdio *qdio)
{
	struct qdio_buffer_element *sbale;
	struct qdio_initialize init_data;
	struct ccw_device *cdev = qdio->adapter->ccw_device;
	int cc;

	if (atomic_read(&qdio->adapter->status) & ZFCP_STATUS_ADAPTER_QDIOUP)
		return -EIO;

	zfcp_qdio_setup_init_data(&init_data, qdio);

	if (qdio_establish(&init_data))
		goto failed_establish;

	if (qdio_activate(cdev))
		goto failed_qdio;

	for (cc = 0; cc < QDIO_MAX_BUFFERS_PER_Q; cc++) {
		sbale = &(qdio->resp_q.sbal[cc]->element[0]);
		sbale->length = 0;
		sbale->flags = SBAL_FLAGS_LAST_ENTRY;
		sbale->addr = NULL;
	}

	if (do_QDIO(cdev, QDIO_FLAG_SYNC_INPUT, 0, 0,
		     QDIO_MAX_BUFFERS_PER_Q))
		goto failed_qdio;

	/* set index of first avalable SBALS / number of available SBALS */
	qdio->req_q.first = 0;
	atomic_set(&qdio->req_q.count, QDIO_MAX_BUFFERS_PER_Q);

	return 0;

failed_qdio:
	qdio_shutdown(cdev, QDIO_FLAG_CLEANUP_USING_CLEAR);
failed_establish:
	dev_err(&cdev->dev,
		"Setting up the QDIO connection to the FCP adapter failed\n");
	return -EIO;
}

void zfcp_qdio_destroy(struct zfcp_qdio *qdio)
{
	struct qdio_buffer **sbal_req, **sbal_resp;
	int p;

	if (!qdio)
		return;

	if (qdio->adapter->ccw_device)
		qdio_free(qdio->adapter->ccw_device);

	sbal_req = qdio->req_q.sbal;
	sbal_resp = qdio->resp_q.sbal;

	for (p = 0; p < QDIO_MAX_BUFFERS_PER_Q; p += QBUFF_PER_PAGE) {
		free_page((unsigned long) sbal_req[p]);
		free_page((unsigned long) sbal_resp[p]);
	}

	kfree(qdio);
}

int zfcp_qdio_setup(struct zfcp_adapter *adapter)
{
	struct zfcp_qdio *qdio;

	qdio = kzalloc(sizeof(struct zfcp_qdio), GFP_KERNEL);
	if (!qdio)
		return -ENOMEM;

	qdio->adapter = adapter;

	if (zfcp_qdio_allocate(qdio)) {
		zfcp_qdio_destroy(qdio);
		return -ENOMEM;
	}

	spin_lock_init(&qdio->req_q_lock);
	spin_lock_init(&qdio->stat_lock);

	adapter->qdio = qdio;
	return 0;
}


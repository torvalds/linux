/*
 * This file is part of the zfcp device driver for
 * FCP adapters for IBM System z9 and zSeries.
 *
 * (C) Copyright IBM Corp. 2002, 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "zfcp_ext.h"

static void zfcp_qdio_sbal_limit(struct zfcp_fsf_req *, int);
static inline volatile struct qdio_buffer_element *zfcp_qdio_sbale_get
	(struct zfcp_qdio_queue *, int, int);
static inline volatile struct qdio_buffer_element *zfcp_qdio_sbale_resp
	(struct zfcp_fsf_req *, int, int);
static volatile struct qdio_buffer_element *zfcp_qdio_sbal_chain
	(struct zfcp_fsf_req *, unsigned long);
static volatile struct qdio_buffer_element *zfcp_qdio_sbale_next
	(struct zfcp_fsf_req *, unsigned long);
static int zfcp_qdio_sbals_zero(struct zfcp_qdio_queue *, int, int);
static inline int zfcp_qdio_sbals_wipe(struct zfcp_fsf_req *);
static void zfcp_qdio_sbale_fill
	(struct zfcp_fsf_req *, unsigned long, void *, int);
static int zfcp_qdio_sbals_from_segment
	(struct zfcp_fsf_req *, unsigned long, void *, unsigned long);

static qdio_handler_t zfcp_qdio_request_handler;
static qdio_handler_t zfcp_qdio_response_handler;
static int zfcp_qdio_handler_error_check(struct zfcp_adapter *,
	unsigned int, unsigned int, unsigned int, int, int);

#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_QDIO

/*
 * Frees BUFFER memory for each of the pointers of the struct qdio_buffer array
 * in the adapter struct sbuf is the pointer array.
 *
 * locks:       must only be called with zfcp_data.config_sema taken
 */
static void
zfcp_qdio_buffers_dequeue(struct qdio_buffer **sbuf)
{
	int pos;

	for (pos = 0; pos < QDIO_MAX_BUFFERS_PER_Q; pos += QBUFF_PER_PAGE)
		free_page((unsigned long) sbuf[pos]);
}

/*
 * Allocates BUFFER memory to each of the pointers of the qdio_buffer_t
 * array in the adapter struct.
 * Cur_buf is the pointer array
 *
 * returns:	zero on success else -ENOMEM
 * locks:       must only be called with zfcp_data.config_sema taken
 */
static int
zfcp_qdio_buffers_enqueue(struct qdio_buffer **sbuf)
{
	int pos;

	for (pos = 0; pos < QDIO_MAX_BUFFERS_PER_Q; pos += QBUFF_PER_PAGE) {
		sbuf[pos] = (struct qdio_buffer *) get_zeroed_page(GFP_KERNEL);
		if (!sbuf[pos]) {
			zfcp_qdio_buffers_dequeue(sbuf);
			return -ENOMEM;
		}
	}
	for (pos = 0; pos < QDIO_MAX_BUFFERS_PER_Q; pos++)
		if (pos % QBUFF_PER_PAGE)
			sbuf[pos] = sbuf[pos - 1] + 1;
	return 0;
}

/* locks:       must only be called with zfcp_data.config_sema taken */
int
zfcp_qdio_allocate_queues(struct zfcp_adapter *adapter)
{
	int ret;

	ret = zfcp_qdio_buffers_enqueue(adapter->request_queue.buffer);
	if (ret)
		return ret;
	return zfcp_qdio_buffers_enqueue(adapter->response_queue.buffer);
}

/* locks:       must only be called with zfcp_data.config_sema taken */
void
zfcp_qdio_free_queues(struct zfcp_adapter *adapter)
{
	ZFCP_LOG_TRACE("freeing request_queue buffers\n");
	zfcp_qdio_buffers_dequeue(adapter->request_queue.buffer);

	ZFCP_LOG_TRACE("freeing response_queue buffers\n");
	zfcp_qdio_buffers_dequeue(adapter->response_queue.buffer);
}

int
zfcp_qdio_allocate(struct zfcp_adapter *adapter)
{
	struct qdio_initialize *init_data;

	init_data = &adapter->qdio_init_data;

	init_data->cdev = adapter->ccw_device;
	init_data->q_format = QDIO_SCSI_QFMT;
	memcpy(init_data->adapter_name, zfcp_get_busid_by_adapter(adapter), 8);
	ASCEBC(init_data->adapter_name, 8);
	init_data->qib_param_field_format = 0;
	init_data->qib_param_field = NULL;
	init_data->input_slib_elements = NULL;
	init_data->output_slib_elements = NULL;
	init_data->min_input_threshold = ZFCP_MIN_INPUT_THRESHOLD;
	init_data->max_input_threshold = ZFCP_MAX_INPUT_THRESHOLD;
	init_data->min_output_threshold = ZFCP_MIN_OUTPUT_THRESHOLD;
	init_data->max_output_threshold = ZFCP_MAX_OUTPUT_THRESHOLD;
	init_data->no_input_qs = 1;
	init_data->no_output_qs = 1;
	init_data->input_handler = zfcp_qdio_response_handler;
	init_data->output_handler = zfcp_qdio_request_handler;
	init_data->int_parm = (unsigned long) adapter;
	init_data->flags = QDIO_INBOUND_0COPY_SBALS |
	    QDIO_OUTBOUND_0COPY_SBALS | QDIO_USE_OUTBOUND_PCIS;
	init_data->input_sbal_addr_array =
	    (void **) (adapter->response_queue.buffer);
	init_data->output_sbal_addr_array =
	    (void **) (adapter->request_queue.buffer);

	return qdio_allocate(init_data);
}

/*
 * function:   	zfcp_qdio_handler_error_check
 *
 * purpose:     called by the response handler to determine error condition
 *
 * returns:	error flag
 *
 */
static int
zfcp_qdio_handler_error_check(struct zfcp_adapter *adapter, unsigned int status,
			      unsigned int qdio_error, unsigned int siga_error,
			      int first_element, int elements_processed)
{
	int retval = 0;

	if (unlikely(status & QDIO_STATUS_LOOK_FOR_ERROR)) {
		retval = -EIO;

		ZFCP_LOG_INFO("QDIO problem occurred (status=0x%x, "
			      "qdio_error=0x%x, siga_error=0x%x)\n",
			      status, qdio_error, siga_error);

		zfcp_hba_dbf_event_qdio(adapter, status, qdio_error, siga_error,
				first_element, elements_processed);
               /*
               	* Restarting IO on the failed adapter from scratch.
                * Since we have been using this adapter, it is save to assume
                * that it is not failed but recoverable. The card seems to
                * report link-up events by self-initiated queue shutdown.
                * That is why we need to clear the link-down flag
                * which is set again in case we have missed by a mile.
                */
		zfcp_erp_adapter_reopen(adapter,
				       ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
				       ZFCP_STATUS_COMMON_ERP_FAILED);
	}
	return retval;
}

/*
 * function:    zfcp_qdio_request_handler
 *
 * purpose:	is called by QDIO layer for completed SBALs in request queue
 *
 * returns:	(void)
 */
static void
zfcp_qdio_request_handler(struct ccw_device *ccw_device,
			  unsigned int status,
			  unsigned int qdio_error,
			  unsigned int siga_error,
			  unsigned int queue_number,
			  int first_element,
			  int elements_processed,
			  unsigned long int_parm)
{
	struct zfcp_adapter *adapter;
	struct zfcp_qdio_queue *queue;

	adapter = (struct zfcp_adapter *) int_parm;
	queue = &adapter->request_queue;

	ZFCP_LOG_DEBUG("adapter %s, first=%d, elements_processed=%d\n",
		       zfcp_get_busid_by_adapter(adapter),
		       first_element, elements_processed);

	if (unlikely(zfcp_qdio_handler_error_check(adapter, status, qdio_error,
						   siga_error, first_element,
						   elements_processed)))
		goto out;
	/*
	 * we stored address of struct zfcp_adapter  data structure
	 * associated with irq in int_parm
	 */

	/* cleanup all SBALs being program-owned now */
	zfcp_qdio_zero_sbals(queue->buffer, first_element, elements_processed);

	/* increase free space in outbound queue */
	atomic_add(elements_processed, &queue->free_count);
	ZFCP_LOG_DEBUG("free_count=%d\n", atomic_read(&queue->free_count));
	wake_up(&adapter->request_wq);
	ZFCP_LOG_DEBUG("elements_processed=%d, free count=%d\n",
		       elements_processed, atomic_read(&queue->free_count));
 out:
	return;
}

/**
 * zfcp_qdio_reqid_check - checks for valid reqids.
 */
static void zfcp_qdio_reqid_check(struct zfcp_adapter *adapter,
				  unsigned long req_id)
{
	struct zfcp_fsf_req *fsf_req;
	unsigned long flags;

	debug_long_event(adapter->erp_dbf, 4, req_id);

	spin_lock_irqsave(&adapter->req_list_lock, flags);
	fsf_req = zfcp_reqlist_find(adapter, req_id);

	if (!fsf_req)
		/*
		 * Unknown request means that we have potentially memory
		 * corruption and must stop the machine immediatly.
		 */
		panic("error: unknown request id (%ld) on adapter %s.\n",
		      req_id, zfcp_get_busid_by_adapter(adapter));

	zfcp_reqlist_remove(adapter, fsf_req);
	atomic_dec(&adapter->reqs_active);
	spin_unlock_irqrestore(&adapter->req_list_lock, flags);

	/* finish the FSF request */
	zfcp_fsf_req_complete(fsf_req);
}

/*
 * function:   	zfcp_qdio_response_handler
 *
 * purpose:	is called by QDIO layer for completed SBALs in response queue
 *
 * returns:	(void)
 */
static void
zfcp_qdio_response_handler(struct ccw_device *ccw_device,
			   unsigned int status,
			   unsigned int qdio_error,
			   unsigned int siga_error,
			   unsigned int queue_number,
			   int first_element,
			   int elements_processed,
			   unsigned long int_parm)
{
	struct zfcp_adapter *adapter;
	struct zfcp_qdio_queue *queue;
	int buffer_index;
	int i;
	struct qdio_buffer *buffer;
	int retval = 0;
	u8 count;
	u8 start;
	volatile struct qdio_buffer_element *buffere = NULL;
	int buffere_index;

	adapter = (struct zfcp_adapter *) int_parm;
	queue = &adapter->response_queue;

	if (unlikely(zfcp_qdio_handler_error_check(adapter, status, qdio_error,
						   siga_error, first_element,
						   elements_processed)))
		goto out;

	/*
	 * we stored address of struct zfcp_adapter  data structure
	 * associated with irq in int_parm
	 */

	buffere = &(queue->buffer[first_element]->element[0]);
	ZFCP_LOG_DEBUG("first BUFFERE flags=0x%x\n", buffere->flags);
	/*
	 * go through all SBALs from input queue currently
	 * returned by QDIO layer
	 */

	for (i = 0; i < elements_processed; i++) {

		buffer_index = first_element + i;
		buffer_index %= QDIO_MAX_BUFFERS_PER_Q;
		buffer = queue->buffer[buffer_index];

		/* go through all SBALEs of SBAL */
		for (buffere_index = 0;
		     buffere_index < QDIO_MAX_ELEMENTS_PER_BUFFER;
		     buffere_index++) {

			/* look for QDIO request identifiers in SB */
			buffere = &buffer->element[buffere_index];
			zfcp_qdio_reqid_check(adapter,
					      (unsigned long) buffere->addr);

			/*
			 * A single used SBALE per inbound SBALE has been
			 * implemented by QDIO so far. Hope they will
			 * do some optimisation. Will need to change to
			 * unlikely() then.
			 */
			if (likely(buffere->flags & SBAL_FLAGS_LAST_ENTRY))
				break;
		};

		if (unlikely(!(buffere->flags & SBAL_FLAGS_LAST_ENTRY))) {
			ZFCP_LOG_NORMAL("bug: End of inbound data "
					"not marked!\n");
		}
	}

	/*
	 * put range of SBALs back to response queue
	 * (including SBALs which have already been free before)
	 */
	count = atomic_read(&queue->free_count) + elements_processed;
	start = queue->free_index;

	ZFCP_LOG_TRACE("calling do_QDIO on adapter %s (flags=0x%x, "
		       "queue_no=%i, index_in_queue=%i, count=%i, "
		       "buffers=0x%lx\n",
		       zfcp_get_busid_by_adapter(adapter),
		       QDIO_FLAG_SYNC_INPUT | QDIO_FLAG_UNDER_INTERRUPT,
		       0, start, count, (unsigned long) &queue->buffer[start]);

	retval = do_QDIO(ccw_device,
			 QDIO_FLAG_SYNC_INPUT | QDIO_FLAG_UNDER_INTERRUPT,
			 0, start, count, NULL);

	if (unlikely(retval)) {
		atomic_set(&queue->free_count, count);
		ZFCP_LOG_DEBUG("clearing of inbound data regions failed, "
			       "queues may be down "
			       "(count=%d, start=%d, retval=%d)\n",
			       count, start, retval);
	} else {
		queue->free_index += count;
		queue->free_index %= QDIO_MAX_BUFFERS_PER_Q;
		atomic_set(&queue->free_count, 0);
		ZFCP_LOG_TRACE("%i buffers enqueued to response "
			       "queue at position %i\n", count, start);
	}
 out:
	return;
}

/**
 * zfcp_qdio_sbale_get - return pointer to SBALE of qdio_queue
 * @queue: queue from which SBALE should be returned
 * @sbal: specifies number of SBAL in queue
 * @sbale: specifes number of SBALE in SBAL
 */
static inline volatile struct qdio_buffer_element *
zfcp_qdio_sbale_get(struct zfcp_qdio_queue *queue, int sbal, int sbale)
{
	return &queue->buffer[sbal]->element[sbale];
}

/**
 * zfcp_qdio_sbale_req - return pointer to SBALE of request_queue for
 *	a struct zfcp_fsf_req
 */
volatile struct qdio_buffer_element *
zfcp_qdio_sbale_req(struct zfcp_fsf_req *fsf_req, int sbal, int sbale)
{
	return zfcp_qdio_sbale_get(&fsf_req->adapter->request_queue,
				   sbal, sbale);
}

/**
 * zfcp_qdio_sbale_resp - return pointer to SBALE of response_queue for
 *	a struct zfcp_fsf_req
 */
static inline volatile struct qdio_buffer_element *
zfcp_qdio_sbale_resp(struct zfcp_fsf_req *fsf_req, int sbal, int sbale)
{
	return zfcp_qdio_sbale_get(&fsf_req->adapter->response_queue,
				   sbal, sbale);
}

/**
 * zfcp_qdio_sbale_curr - return current SBALE on request_queue for
 *	a struct zfcp_fsf_req
 */
volatile struct qdio_buffer_element *
zfcp_qdio_sbale_curr(struct zfcp_fsf_req *fsf_req)
{
	return zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr,
				   fsf_req->sbale_curr);
}

/**
 * zfcp_qdio_sbal_limit - determine maximum number of SBALs that can be used
 *	on the request_queue for a struct zfcp_fsf_req
 * @fsf_req: the number of the last SBAL that can be used is stored herein
 * @max_sbals: used to pass an upper limit for the number of SBALs
 *
 * Note: We can assume at least one free SBAL in the request_queue when called.
 */
static void
zfcp_qdio_sbal_limit(struct zfcp_fsf_req *fsf_req, int max_sbals)
{
	int count = atomic_read(&fsf_req->adapter->request_queue.free_count);
	count = min(count, max_sbals);
	fsf_req->sbal_last  = fsf_req->sbal_first;
	fsf_req->sbal_last += (count - 1);
	fsf_req->sbal_last %= QDIO_MAX_BUFFERS_PER_Q;
}

/**
 * zfcp_qdio_sbal_chain - chain SBALs if more than one SBAL is needed for a
 *	request
 * @fsf_req: zfcp_fsf_req to be processed
 * @sbtype: SBAL flags which have to be set in first SBALE of new SBAL
 *
 * This function changes sbal_curr, sbale_curr, sbal_number of fsf_req.
 */
static volatile struct qdio_buffer_element *
zfcp_qdio_sbal_chain(struct zfcp_fsf_req *fsf_req, unsigned long sbtype)
{
	volatile struct qdio_buffer_element *sbale;

	/* set last entry flag in current SBALE of current SBAL */
	sbale = zfcp_qdio_sbale_curr(fsf_req);
	sbale->flags |= SBAL_FLAGS_LAST_ENTRY;

	/* don't exceed last allowed SBAL */
	if (fsf_req->sbal_curr == fsf_req->sbal_last)
		return NULL;

	/* set chaining flag in first SBALE of current SBAL */
	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
	sbale->flags |= SBAL_FLAGS0_MORE_SBALS;

	/* calculate index of next SBAL */
	fsf_req->sbal_curr++;
	fsf_req->sbal_curr %= QDIO_MAX_BUFFERS_PER_Q;

	/* keep this requests number of SBALs up-to-date */
	fsf_req->sbal_number++;

	/* start at first SBALE of new SBAL */
	fsf_req->sbale_curr = 0;

	/* set storage-block type for new SBAL */
	sbale = zfcp_qdio_sbale_curr(fsf_req);
	sbale->flags |= sbtype;

	return sbale;
}

/**
 * zfcp_qdio_sbale_next - switch to next SBALE, chain SBALs if needed
 */
static volatile struct qdio_buffer_element *
zfcp_qdio_sbale_next(struct zfcp_fsf_req *fsf_req, unsigned long sbtype)
{
	if (fsf_req->sbale_curr == ZFCP_LAST_SBALE_PER_SBAL)
		return zfcp_qdio_sbal_chain(fsf_req, sbtype);

	fsf_req->sbale_curr++;

	return zfcp_qdio_sbale_curr(fsf_req);
}

/**
 * zfcp_qdio_sbals_zero - initialize SBALs between first and last in queue
 *	with zero from
 */
static int
zfcp_qdio_sbals_zero(struct zfcp_qdio_queue *queue, int first, int last)
{
	struct qdio_buffer **buf = queue->buffer;
	int curr = first;
	int count = 0;

	for(;;) {
		curr %= QDIO_MAX_BUFFERS_PER_Q;
		count++;
		memset(buf[curr], 0, sizeof(struct qdio_buffer));
		if (curr == last)
			break;
		curr++;
	}
	return count;
}


/**
 * zfcp_qdio_sbals_wipe - reset all changes in SBALs for an fsf_req
 */
static inline int
zfcp_qdio_sbals_wipe(struct zfcp_fsf_req *fsf_req)
{
	return zfcp_qdio_sbals_zero(&fsf_req->adapter->request_queue,
				    fsf_req->sbal_first, fsf_req->sbal_curr);
}


/**
 * zfcp_qdio_sbale_fill - set address and length in current SBALE
 *	on request_queue
 */
static void
zfcp_qdio_sbale_fill(struct zfcp_fsf_req *fsf_req, unsigned long sbtype,
		     void *addr, int length)
{
	volatile struct qdio_buffer_element *sbale;

	sbale = zfcp_qdio_sbale_curr(fsf_req);
	sbale->addr = addr;
	sbale->length = length;
}

/**
 * zfcp_qdio_sbals_from_segment - map memory segment to SBALE(s)
 * @fsf_req: request to be processed
 * @sbtype: SBALE flags
 * @start_addr: address of memory segment
 * @total_length: length of memory segment
 *
 * Alignment and length of the segment determine how many SBALEs are needed
 * for the memory segment.
 */
static int
zfcp_qdio_sbals_from_segment(struct zfcp_fsf_req *fsf_req, unsigned long sbtype,
			     void *start_addr, unsigned long total_length)
{
	unsigned long remaining, length;
	void *addr;

	/* split segment up heeding page boundaries */
	for (addr = start_addr, remaining = total_length; remaining > 0;
	     addr += length, remaining -= length) {
		/* get next free SBALE for new piece */
		if (NULL == zfcp_qdio_sbale_next(fsf_req, sbtype)) {
			/* no SBALE left, clean up and leave */
			zfcp_qdio_sbals_wipe(fsf_req);
			return -EINVAL;
		}
		/* calculate length of new piece */
		length = min(remaining,
			     (PAGE_SIZE - ((unsigned long) addr &
					   (PAGE_SIZE - 1))));
		/* fill current SBALE with calculated piece */
		zfcp_qdio_sbale_fill(fsf_req, sbtype, addr, length);
	}
	return total_length;
}


/**
 * zfcp_qdio_sbals_from_sg - fill SBALs from scatter-gather list
 * @fsf_req: request to be processed
 * @sbtype: SBALE flags
 * @sg: scatter-gather list
 * @sg_count: number of elements in scatter-gather list
 * @max_sbals: upper bound for number of SBALs to be used
 */
int
zfcp_qdio_sbals_from_sg(struct zfcp_fsf_req *fsf_req, unsigned long sbtype,
                        struct scatterlist *sgl, int sg_count, int max_sbals)
{
	int sg_index;
	struct scatterlist *sg_segment;
	int retval;
	volatile struct qdio_buffer_element *sbale;
	int bytes = 0;

	/* figure out last allowed SBAL */
	zfcp_qdio_sbal_limit(fsf_req, max_sbals);

	/* set storage-block type for current SBAL */
	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
	sbale->flags |= sbtype;

	/* process all segements of scatter-gather list */
	for_each_sg(sgl, sg_segment, sg_count, sg_index) {
		retval = zfcp_qdio_sbals_from_segment(
				fsf_req,
				sbtype,
				zfcp_sg_to_address(sg_segment),
				sg_segment->length);
		if (retval < 0) {
			bytes = retval;
			goto out;
		} else
                        bytes += retval;
	}
	/* assume that no other SBALEs are to follow in the same SBAL */
	sbale = zfcp_qdio_sbale_curr(fsf_req);
	sbale->flags |= SBAL_FLAGS_LAST_ENTRY;
out:
	return bytes;
}


/**
 * zfcp_qdio_sbals_from_scsicmnd - fill SBALs from scsi command
 * @fsf_req: request to be processed
 * @sbtype: SBALE flags
 * @scsi_cmnd: either scatter-gather list or buffer contained herein is used
 *	to fill SBALs
 */
int
zfcp_qdio_sbals_from_scsicmnd(struct zfcp_fsf_req *fsf_req,
			      unsigned long sbtype, struct scsi_cmnd *scsi_cmnd)
{
	return zfcp_qdio_sbals_from_sg(fsf_req,	sbtype, scsi_sglist(scsi_cmnd),
				       scsi_sg_count(scsi_cmnd),
				       ZFCP_MAX_SBALS_PER_REQ);
}

/**
 * zfcp_qdio_determine_pci - set PCI flag in first SBALE on qdio queue if needed
 */
int
zfcp_qdio_determine_pci(struct zfcp_qdio_queue *req_queue,
			struct zfcp_fsf_req *fsf_req)
{
	int new_distance_from_int;
	int pci_pos;
	volatile struct qdio_buffer_element *sbale;

	new_distance_from_int = req_queue->distance_from_int +
                fsf_req->sbal_number;

	if (unlikely(new_distance_from_int >= ZFCP_QDIO_PCI_INTERVAL)) {
		new_distance_from_int %= ZFCP_QDIO_PCI_INTERVAL;
                pci_pos  = fsf_req->sbal_first;
		pci_pos += fsf_req->sbal_number;
		pci_pos -= new_distance_from_int;
		pci_pos -= 1;
		pci_pos %= QDIO_MAX_BUFFERS_PER_Q;
		sbale = zfcp_qdio_sbale_req(fsf_req, pci_pos, 0);
		sbale->flags |= SBAL_FLAGS0_PCI;
	}
	return new_distance_from_int;
}

/*
 * function:	zfcp_zero_sbals
 *
 * purpose:	zeros specified range of SBALs
 *
 * returns:
 */
void
zfcp_qdio_zero_sbals(struct qdio_buffer *buf[], int first, int clean_count)
{
	int cur_pos;
	int index;

	for (cur_pos = first; cur_pos < (first + clean_count); cur_pos++) {
		index = cur_pos % QDIO_MAX_BUFFERS_PER_Q;
		memset(buf[index], 0, sizeof (struct qdio_buffer));
		ZFCP_LOG_TRACE("zeroing BUFFER %d at address %p\n",
			       index, buf[index]);
	}
}

#undef ZFCP_LOG_AREA

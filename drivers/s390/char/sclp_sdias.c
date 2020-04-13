// SPDX-License-Identifier: GPL-2.0
/*
 * SCLP "store data in absolute storage"
 *
 * Copyright IBM Corp. 2003, 2013
 * Author(s): Michael Holzheu
 */

#define KMSG_COMPONENT "sclp_sdias"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/completion.h>
#include <linux/sched.h>
#include <asm/sclp.h>
#include <asm/debug.h>
#include <asm/ipl.h>

#include "sclp_sdias.h"
#include "sclp.h"
#include "sclp_rw.h"

#define TRACE(x...) debug_sprintf_event(sdias_dbf, 1, x)

#define SDIAS_RETRIES 300

static struct debug_info *sdias_dbf;

static struct sclp_register sclp_sdias_register = {
	.send_mask = EVTYP_SDIAS_MASK,
};

static struct sdias_sccb *sclp_sdias_sccb;
static struct sdias_evbuf sdias_evbuf;

static DECLARE_COMPLETION(evbuf_accepted);
static DECLARE_COMPLETION(evbuf_done);
static DEFINE_MUTEX(sdias_mutex);

/*
 * Called by SCLP base when read event data has been completed (async mode only)
 */
static void sclp_sdias_receiver_fn(struct evbuf_header *evbuf)
{
	memcpy(&sdias_evbuf, evbuf,
	       min_t(unsigned long, sizeof(sdias_evbuf), evbuf->length));
	complete(&evbuf_done);
	TRACE("sclp_sdias_receiver_fn done\n");
}

/*
 * Called by SCLP base when sdias event has been accepted
 */
static void sdias_callback(struct sclp_req *request, void *data)
{
	complete(&evbuf_accepted);
	TRACE("callback done\n");
}

static int sdias_sclp_send(struct sclp_req *req)
{
	struct sdias_sccb *sccb = sclp_sdias_sccb;
	int retries;
	int rc;

	for (retries = SDIAS_RETRIES; retries; retries--) {
		TRACE("add request\n");
		rc = sclp_add_request(req);
		if (rc) {
			/* not initiated, wait some time and retry */
			set_current_state(TASK_INTERRUPTIBLE);
			TRACE("add request failed: rc = %i\n",rc);
			schedule_timeout(msecs_to_jiffies(500));
			continue;
		}
		/* initiated, wait for completion of service call */
		wait_for_completion(&evbuf_accepted);
		if (req->status == SCLP_REQ_FAILED) {
			TRACE("sclp request failed\n");
			continue;
		}
		/* if not accepted, retry */
		if (!(sccb->evbuf.hdr.flags & 0x80)) {
			TRACE("sclp request failed: flags=%x\n",
			      sccb->evbuf.hdr.flags);
			continue;
		}
		/*
		 * for the sync interface the response is in the initial sccb
		 */
		if (!sclp_sdias_register.receiver_fn) {
			memcpy(&sdias_evbuf, &sccb->evbuf, sizeof(sdias_evbuf));
			TRACE("sync request done\n");
			return 0;
		}
		/* otherwise we wait for completion */
		wait_for_completion(&evbuf_done);
		TRACE("request done\n");
		return 0;
	}
	return -EIO;
}

/*
 * Get number of blocks (4K) available in the HSA
 */
int sclp_sdias_blk_count(void)
{
	struct sdias_sccb *sccb = sclp_sdias_sccb;
	struct sclp_req request;
	int rc;

	mutex_lock(&sdias_mutex);

	memset(sccb, 0, sizeof(*sccb));
	memset(&request, 0, sizeof(request));

	sccb->hdr.length = sizeof(*sccb);
	sccb->evbuf.hdr.length = sizeof(struct sdias_evbuf);
	sccb->evbuf.hdr.type = EVTYP_SDIAS;
	sccb->evbuf.event_qual = SDIAS_EQ_SIZE;
	sccb->evbuf.data_id = SDIAS_DI_FCP_DUMP;
	sccb->evbuf.event_id = 4712;
	sccb->evbuf.dbs = 1;

	request.sccb = sccb;
	request.command = SCLP_CMDW_WRITE_EVENT_DATA;
	request.status = SCLP_REQ_FILLED;
	request.callback = sdias_callback;

	rc = sdias_sclp_send(&request);
	if (rc) {
		pr_err("sclp_send failed for get_nr_blocks\n");
		goto out;
	}
	if (sccb->hdr.response_code != 0x0020) {
		TRACE("send failed: %x\n", sccb->hdr.response_code);
		rc = -EIO;
		goto out;
	}

	switch (sdias_evbuf.event_status) {
		case 0:
			rc = sdias_evbuf.blk_cnt;
			break;
		default:
			pr_err("SCLP error: %x\n", sdias_evbuf.event_status);
			rc = -EIO;
			goto out;
	}
	TRACE("%i blocks\n", rc);
out:
	mutex_unlock(&sdias_mutex);
	return rc;
}

/*
 * Copy from HSA to absolute storage (not reentrant):
 *
 * @dest     : Address of buffer where data should be copied
 * @start_blk: Start Block (beginning with 1)
 * @nr_blks  : Number of 4K blocks to copy
 *
 * Return Value: 0 : Requested 'number' of blocks of data copied
 *		 <0: ERROR - negative event status
 */
int sclp_sdias_copy(void *dest, int start_blk, int nr_blks)
{
	struct sdias_sccb *sccb = sclp_sdias_sccb;
	struct sclp_req request;
	int rc;

	mutex_lock(&sdias_mutex);

	memset(sccb, 0, sizeof(*sccb));
	memset(&request, 0, sizeof(request));

	sccb->hdr.length = sizeof(*sccb);
	sccb->evbuf.hdr.length = sizeof(struct sdias_evbuf);
	sccb->evbuf.hdr.type = EVTYP_SDIAS;
	sccb->evbuf.hdr.flags = 0;
	sccb->evbuf.event_qual = SDIAS_EQ_STORE_DATA;
	sccb->evbuf.data_id = SDIAS_DI_FCP_DUMP;
	sccb->evbuf.event_id = 4712;
	sccb->evbuf.asa_size = SDIAS_ASA_SIZE_64;
	sccb->evbuf.event_status = 0;
	sccb->evbuf.blk_cnt = nr_blks;
	sccb->evbuf.asa = (unsigned long)dest;
	sccb->evbuf.fbn = start_blk;
	sccb->evbuf.lbn = 0;
	sccb->evbuf.dbs = 1;

	request.sccb	 = sccb;
	request.command  = SCLP_CMDW_WRITE_EVENT_DATA;
	request.status	 = SCLP_REQ_FILLED;
	request.callback = sdias_callback;

	rc = sdias_sclp_send(&request);
	if (rc) {
		pr_err("sclp_send failed: %x\n", rc);
		goto out;
	}
	if (sccb->hdr.response_code != 0x0020) {
		TRACE("copy failed: %x\n", sccb->hdr.response_code);
		rc = -EIO;
		goto out;
	}

	switch (sdias_evbuf.event_status) {
	case SDIAS_EVSTATE_ALL_STORED:
		TRACE("all stored\n");
		break;
	case SDIAS_EVSTATE_PART_STORED:
		TRACE("part stored: %i\n", sdias_evbuf.blk_cnt);
		break;
	case SDIAS_EVSTATE_NO_DATA:
		TRACE("no data\n");
		fallthrough;
	default:
		pr_err("Error from SCLP while copying hsa. Event status = %x\n",
		       sdias_evbuf.event_status);
		rc = -EIO;
	}
out:
	mutex_unlock(&sdias_mutex);
	return rc;
}

static int __init sclp_sdias_register_check(void)
{
	int rc;

	rc = sclp_register(&sclp_sdias_register);
	if (rc)
		return rc;
	if (sclp_sdias_blk_count() == 0) {
		sclp_unregister(&sclp_sdias_register);
		return -ENODEV;
	}
	return 0;
}

static int __init sclp_sdias_init_sync(void)
{
	TRACE("Try synchronous mode\n");
	sclp_sdias_register.receive_mask = 0;
	sclp_sdias_register.receiver_fn = NULL;
	return sclp_sdias_register_check();
}

static int __init sclp_sdias_init_async(void)
{
	TRACE("Try asynchronous mode\n");
	sclp_sdias_register.receive_mask = EVTYP_SDIAS_MASK;
	sclp_sdias_register.receiver_fn = sclp_sdias_receiver_fn;
	return sclp_sdias_register_check();
}

int __init sclp_sdias_init(void)
{
	if (ipl_info.type != IPL_TYPE_FCP_DUMP)
		return 0;
	sclp_sdias_sccb = (void *) __get_free_page(GFP_KERNEL | GFP_DMA);
	BUG_ON(!sclp_sdias_sccb);
	sdias_dbf = debug_register("dump_sdias", 4, 1, 4 * sizeof(long));
	debug_register_view(sdias_dbf, &debug_sprintf_view);
	debug_set_level(sdias_dbf, 6);
	if (sclp_sdias_init_sync() == 0)
		goto out;
	if (sclp_sdias_init_async() == 0)
		goto out;
	TRACE("init failed\n");
	free_page((unsigned long) sclp_sdias_sccb);
	return -ENODEV;
out:
	TRACE("init done\n");
	return 0;
}

void __exit sclp_sdias_exit(void)
{
	debug_unregister(sdias_dbf);
	sclp_unregister(&sclp_sdias_register);
}

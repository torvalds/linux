/*
 * Copyright 2014 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/mempool.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>

#include "snic_io.h"
#include "snic.h"

/*
 * snic_get_trc_buf : Allocates a trace record and returns.
 */
struct snic_trc_data *
snic_get_trc_buf(void)
{
	struct snic_trc *trc = &snic_glob->trc;
	struct snic_trc_data *td = NULL;
	unsigned long flags;

	spin_lock_irqsave(&trc->lock, flags);
	td = &trc->buf[trc->wr_idx];
	trc->wr_idx++;

	if (trc->wr_idx == trc->max_idx)
		trc->wr_idx = 0;

	if (trc->wr_idx != trc->rd_idx) {
		spin_unlock_irqrestore(&trc->lock, flags);

		goto end;
	}

	trc->rd_idx++;
	if (trc->rd_idx == trc->max_idx)
		trc->rd_idx = 0;

	td->ts = 0;	/* Marker for checking the record, for complete data*/
	spin_unlock_irqrestore(&trc->lock, flags);

end:

	return td;
} /* end of snic_get_trc_buf */

/*
 * snic_fmt_trc_data : Formats trace data for printing.
 */
static int
snic_fmt_trc_data(struct snic_trc_data *td, char *buf, int buf_sz)
{
	int len = 0;
	struct timespec tmspec;

	jiffies_to_timespec(td->ts, &tmspec);

	len += snprintf(buf, buf_sz,
			"%lu.%10lu %-25s %3d %4x %16llx %16llx %16llx %16llx %16llx\n",
			tmspec.tv_sec,
			tmspec.tv_nsec,
			td->fn,
			td->hno,
			td->tag,
			td->data[0], td->data[1], td->data[2], td->data[3],
			td->data[4]);

	return len;
} /* end of snic_fmt_trc_data */

/*
 * snic_get_trc_data : Returns a formatted trace buffer.
 */
int
snic_get_trc_data(char *buf, int buf_sz)
{
	struct snic_trc_data *td = NULL;
	struct snic_trc *trc = &snic_glob->trc;
	unsigned long flags;

	spin_lock_irqsave(&trc->lock, flags);
	if (trc->rd_idx == trc->wr_idx) {
		spin_unlock_irqrestore(&trc->lock, flags);

		return -1;
	}
	td = &trc->buf[trc->rd_idx];

	if (td->ts == 0) {
		/* write in progress. */
		spin_unlock_irqrestore(&trc->lock, flags);

		return -1;
	}

	trc->rd_idx++;
	if (trc->rd_idx == trc->max_idx)
		trc->rd_idx = 0;
	spin_unlock_irqrestore(&trc->lock, flags);

	return snic_fmt_trc_data(td, buf, buf_sz);
} /* end of snic_get_trc_data */

/*
 * snic_trc_init() : Configures Trace Functionality for snic.
 */
int
snic_trc_init(void)
{
	struct snic_trc *trc = &snic_glob->trc;
	void *tbuf = NULL;
	int tbuf_sz = 0, ret;

	tbuf_sz = (snic_trace_max_pages * PAGE_SIZE);
	tbuf = vmalloc(tbuf_sz);
	if (!tbuf) {
		SNIC_ERR("Failed to Allocate Trace Buffer Size. %d\n", tbuf_sz);
		SNIC_ERR("Trace Facility not enabled.\n");
		ret = -ENOMEM;

		return ret;
	}

	memset(tbuf, 0, tbuf_sz);
	trc->buf = (struct snic_trc_data *) tbuf;
	spin_lock_init(&trc->lock);

	ret = snic_trc_debugfs_init();
	if (ret) {
		SNIC_ERR("Failed to create Debugfs Files.\n");

		goto error;
	}

	trc->max_idx = (tbuf_sz / SNIC_TRC_ENTRY_SZ);
	trc->rd_idx = trc->wr_idx = 0;
	trc->enable = 1;
	SNIC_INFO("Trace Facility Enabled.\n Trace Buffer SZ %lu Pages.\n",
		  tbuf_sz / PAGE_SIZE);
	ret = 0;

	return ret;

error:
	snic_trc_free();

	return ret;
} /* end of snic_trc_init */

/*
 * snic_trc_free : Releases the trace buffer and disables the tracing.
 */
void
snic_trc_free(void)
{
	struct snic_trc *trc = &snic_glob->trc;

	trc->enable = 0;
	snic_trc_debugfs_term();

	if (trc->buf) {
		vfree(trc->buf);
		trc->buf = NULL;
	}

	SNIC_INFO("Trace Facility Disabled.\n");
} /* end of snic_trc_free */

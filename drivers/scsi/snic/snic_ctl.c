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

#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/mempool.h>
#include <scsi/scsi_tcq.h>
#include <linux/ctype.h>

#include "snic_io.h"
#include "snic.h"
#include "cq_enet_desc.h"
#include "snic_fwint.h"

/*
 * snic_handle_link : Handles link flaps.
 */
void
snic_handle_link(struct work_struct *work)
{
	struct snic *snic = container_of(work, struct snic, link_work);

	if (snic->config.xpt_type != SNIC_DAS) {
		SNIC_HOST_INFO(snic->shost, "Link Event Received.\n");
		SNIC_ASSERT_NOT_IMPL(1);

		return;
	}

	snic->link_status = svnic_dev_link_status(snic->vdev);
	snic->link_down_cnt = svnic_dev_link_down_cnt(snic->vdev);
	SNIC_HOST_INFO(snic->shost, "Link Event: Link %s.\n",
		       ((snic->link_status) ? "Up" : "Down"));
}


/*
 * snic_ver_enc : Encodes version str to int
 * version string is similar to netmask string
 */
static int
snic_ver_enc(const char *s)
{
	int v[4] = {0};
	int  i = 0, x = 0;
	char c;
	const char *p = s;

	/* validate version string */
	if ((strlen(s) > 15) || (strlen(s) < 7))
		goto end;

	while ((c = *p++)) {
		if (c == '.') {
			i++;
			continue;
		}

		if (i > 4 || !isdigit(c))
			goto end;

		v[i] = v[i] * 10 + (c - '0');
	}

	/* validate sub version numbers */
	for (i = 3; i >= 0; i--)
		if (v[i] > 0xff)
			goto end;

	x |= (v[0] << 24) | v[1] << 16 | v[2] << 8 | v[3];

end:
	if (x == 0) {
		SNIC_ERR("Invalid version string [%s].\n", s);

		return -1;
	}

	return x;
} /* end of snic_ver_enc */

/*
 * snic_qeueue_exch_ver_req :
 *
 * Queues Exchange Version Request, to communicate host information
 * in return, it gets firmware version details
 */
int
snic_queue_exch_ver_req(struct snic *snic)
{
	struct snic_req_info *rqi = NULL;
	struct snic_host_req *req = NULL;
	u32 ver = 0;
	int ret = 0;

	SNIC_HOST_INFO(snic->shost, "Exch Ver Req Preparing...\n");

	rqi = snic_req_init(snic, 0);
	if (!rqi) {
		SNIC_HOST_ERR(snic->shost,
			      "Queuing Exch Ver Req failed, err = %d\n",
			      ret);

		ret = -ENOMEM;
		goto error;
	}

	req = rqi_to_req(rqi);

	/* Initialize snic_host_req */
	snic_io_hdr_enc(&req->hdr, SNIC_REQ_EXCH_VER, 0, SCSI_NO_TAG,
			snic->config.hid, 0, (ulong)rqi);
	ver = snic_ver_enc(SNIC_DRV_VERSION);
	req->u.exch_ver.drvr_ver = cpu_to_le32(ver);
	req->u.exch_ver.os_type = cpu_to_le32(SNIC_OS_LINUX);

	snic_handle_untagged_req(snic, rqi);

	ret = snic_queue_wq_desc(snic, req, sizeof(*req));
	if (ret) {
		snic_release_untagged_req(snic, rqi);
		SNIC_HOST_ERR(snic->shost,
			      "Queuing Exch Ver Req failed, err = %d\n",
			      ret);
		goto error;
	}

	SNIC_HOST_INFO(snic->shost, "Exch Ver Req is issued. ret = %d\n", ret);

error:
	return ret;
} /* end of snic_queue_exch_ver_req */

/*
 * snic_io_exch_ver_cmpl_handler
 */
int
snic_io_exch_ver_cmpl_handler(struct snic *snic, struct snic_fw_req *fwreq)
{
	struct snic_req_info *rqi = NULL;
	struct snic_exch_ver_rsp *exv_cmpl = &fwreq->u.exch_ver_cmpl;
	u8 typ, hdr_stat;
	u32 cmnd_id, hid, max_sgs;
	ulong ctx = 0;
	unsigned long flags;
	int ret = 0;

	SNIC_HOST_INFO(snic->shost, "Exch Ver Compl Received.\n");
	snic_io_hdr_dec(&fwreq->hdr, &typ, &hdr_stat, &cmnd_id, &hid, &ctx);
	SNIC_BUG_ON(snic->config.hid != hid);
	rqi = (struct snic_req_info *) ctx;

	if (hdr_stat) {
		SNIC_HOST_ERR(snic->shost,
			      "Exch Ver Completed w/ err status %d\n",
			      hdr_stat);

		goto exch_cmpl_end;
	}

	spin_lock_irqsave(&snic->snic_lock, flags);
	snic->fwinfo.fw_ver = le32_to_cpu(exv_cmpl->version);
	snic->fwinfo.hid = le32_to_cpu(exv_cmpl->hid);
	snic->fwinfo.max_concur_ios = le32_to_cpu(exv_cmpl->max_concur_ios);
	snic->fwinfo.max_sgs_per_cmd = le32_to_cpu(exv_cmpl->max_sgs_per_cmd);
	snic->fwinfo.max_io_sz = le32_to_cpu(exv_cmpl->max_io_sz);
	snic->fwinfo.max_tgts = le32_to_cpu(exv_cmpl->max_tgts);
	snic->fwinfo.io_tmo = le16_to_cpu(exv_cmpl->io_timeout);

	SNIC_HOST_INFO(snic->shost,
		       "vers %u hid %u max_concur_ios %u max_sgs_per_cmd %u max_io_sz %u max_tgts %u fw tmo %u\n",
		       snic->fwinfo.fw_ver,
		       snic->fwinfo.hid,
		       snic->fwinfo.max_concur_ios,
		       snic->fwinfo.max_sgs_per_cmd,
		       snic->fwinfo.max_io_sz,
		       snic->fwinfo.max_tgts,
		       snic->fwinfo.io_tmo);

	SNIC_HOST_INFO(snic->shost,
		       "HBA Capabilities = 0x%x\n",
		       le32_to_cpu(exv_cmpl->hba_cap));

	/* Updating SGList size */
	max_sgs = snic->fwinfo.max_sgs_per_cmd;
	if (max_sgs && max_sgs < SNIC_MAX_SG_DESC_CNT) {
		snic->shost->sg_tablesize = max_sgs;
		SNIC_HOST_INFO(snic->shost, "Max SGs set to %d\n",
			       snic->shost->sg_tablesize);
	} else if (max_sgs > snic->shost->sg_tablesize) {
		SNIC_HOST_INFO(snic->shost,
			       "Target type %d Supports Larger Max SGList %d than driver's Max SG List %d.\n",
			       snic->config.xpt_type, max_sgs,
			       snic->shost->sg_tablesize);
	}

	if (snic->shost->can_queue > snic->fwinfo.max_concur_ios)
		snic->shost->can_queue = snic->fwinfo.max_concur_ios;

	snic->shost->max_sectors = snic->fwinfo.max_io_sz >> 9;
	if (snic->fwinfo.wait)
		complete(snic->fwinfo.wait);

	spin_unlock_irqrestore(&snic->snic_lock, flags);

exch_cmpl_end:
	snic_release_untagged_req(snic, rqi);

	SNIC_HOST_INFO(snic->shost, "Exch_cmpl Done, hdr_stat %d.\n", hdr_stat);

	return ret;
} /* end of snic_io_exch_ver_cmpl_handler */

/*
 * snic_get_conf
 *
 * Synchronous call, and Retrieves snic params.
 */
int
snic_get_conf(struct snic *snic)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	unsigned long flags;
	int ret;
	int nr_retries = 3;

	SNIC_HOST_INFO(snic->shost, "Retrieving snic params.\n");
	spin_lock_irqsave(&snic->snic_lock, flags);
	memset(&snic->fwinfo, 0, sizeof(snic->fwinfo));
	snic->fwinfo.wait = &wait;
	spin_unlock_irqrestore(&snic->snic_lock, flags);

	/* Additional delay to handle HW Resource initialization. */
	msleep(50);

	/*
	 * Exch ver req can be ignored by FW, if HW Resource initialization
	 * is in progress, Hence retry.
	 */
	do {
		ret = snic_queue_exch_ver_req(snic);
		if (ret)
			return ret;

		wait_for_completion_timeout(&wait, msecs_to_jiffies(2000));
		spin_lock_irqsave(&snic->snic_lock, flags);
		ret = (snic->fwinfo.fw_ver != 0) ? 0 : -ETIMEDOUT;
		if (ret)
			SNIC_HOST_ERR(snic->shost,
				      "Failed to retrieve snic params,\n");

		/* Unset fwinfo.wait, on success or on last retry */
		if (ret == 0 || nr_retries == 1)
			snic->fwinfo.wait = NULL;

		spin_unlock_irqrestore(&snic->snic_lock, flags);
	} while (ret && --nr_retries);

	return ret;
} /* end of snic_get_info */

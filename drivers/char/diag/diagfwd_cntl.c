/* Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/diagchar.h>
#include <linux/platform_device.h>
#include "diagchar.h"
#include "diagfwd.h"
#include "diagfwd_cntl.h"

#define HDR_SIZ 8

void diag_smd_cntl_notify(void *ctxt, unsigned event)
{
	int r1, r2;

	if (!(driver->ch_cntl))
		return;

	switch (event) {
	case SMD_EVENT_DATA:
		r1 = smd_read_avail(driver->ch_cntl);
		r2 = smd_cur_packet_size(driver->ch_cntl);
		if (r1 > 0 && r1 == r2)
			queue_work(driver->diag_wq,
				 &(driver->diag_read_smd_cntl_work));
		else
			pr_debug("diag: incomplete pkt on Modem CNTL ch\n");
		break;
	case SMD_EVENT_OPEN:
		queue_work(driver->diag_cntl_wq,
			 &(driver->diag_modem_mask_update_work));
		break;
	}
}

void diag_smd_qdsp_cntl_notify(void *ctxt, unsigned event)
{
	int r1, r2;

	if (!(driver->chqdsp_cntl))
		return;

	switch (event) {
	case SMD_EVENT_DATA:
		r1 = smd_read_avail(driver->chqdsp_cntl);
		r2 = smd_cur_packet_size(driver->chqdsp_cntl);
		if (r1 > 0 && r1 == r2)
			queue_work(driver->diag_wq,
				 &(driver->diag_read_smd_qdsp_cntl_work));
		else
			pr_debug("diag: incomplete pkt on LPASS CNTL ch\n");
		break;
	case SMD_EVENT_OPEN:
		queue_work(driver->diag_cntl_wq,
			 &(driver->diag_qdsp_mask_update_work));
		break;
	}
}

void diag_smd_wcnss_cntl_notify(void *ctxt, unsigned event)
{
	int r1, r2;

	if (!(driver->ch_wcnss_cntl))
		return;

	switch (event) {
	case SMD_EVENT_DATA:
		r1 = smd_read_avail(driver->ch_wcnss_cntl);
		r2 = smd_cur_packet_size(driver->ch_wcnss_cntl);
		if (r1 > 0 && r1 == r2)
			queue_work(driver->diag_wq,
				 &(driver->diag_read_smd_wcnss_cntl_work));
		else
			pr_debug("diag: incomplete pkt on WCNSS CNTL ch\n");
		break;
	case SMD_EVENT_OPEN:
		queue_work(driver->diag_cntl_wq,
			 &(driver->diag_wcnss_mask_update_work));
		break;
	}
}

static void diag_smd_cntl_send_req(int proc_num)
{
	int data_len = 0, type = -1, count_bytes = 0, j, r, flag = 0;
	struct bindpkt_params_per_process *pkt_params =
		 kzalloc(sizeof(struct bindpkt_params_per_process), GFP_KERNEL);
	struct diag_ctrl_msg *msg;
	struct cmd_code_range *range;
	struct bindpkt_params *temp;
	void *buf = NULL;
	smd_channel_t *smd_ch = NULL;

	if (pkt_params == NULL) {
		pr_alert("diag: Memory allocation failure\n");
		return;
	}

	if (proc_num == MODEM_PROC) {
		buf = driver->buf_in_cntl;
		smd_ch = driver->ch_cntl;
	} else if (proc_num == QDSP_PROC) {
		buf = driver->buf_in_qdsp_cntl;
		smd_ch = driver->chqdsp_cntl;
	} else if (proc_num == WCNSS_PROC) {
		buf = driver->buf_in_wcnss_cntl;
		smd_ch = driver->ch_wcnss_cntl;
	}

	if (!smd_ch || !buf) {
		kfree(pkt_params);
		return;
	}

	r = smd_read_avail(smd_ch);
	if (r > IN_BUF_SIZE) {
		if (r < MAX_IN_BUF_SIZE) {
			pr_err("diag: SMD CNTL sending pkt upto %d bytes", r);
			buf = krealloc(buf, r, GFP_KERNEL);
		} else {
			pr_err("diag: CNTL pkt > %d bytes", MAX_IN_BUF_SIZE);
			kfree(pkt_params);
			return;
		}
	}
	if (buf && r > 0) {
		smd_read(smd_ch, buf, r);
		while (count_bytes + HDR_SIZ <= r) {
			type = *(uint32_t *)(buf);
			data_len = *(uint32_t *)(buf + 4);
			if (type < DIAG_CTRL_MSG_REG ||
					 type > DIAG_CTRL_MSG_F3_MASK_V2) {
				pr_alert("diag: Invalid Msg type %d proc %d",
					 type, proc_num);
				break;
			}
			if (data_len < 0 || data_len > r) {
				pr_alert("diag: Invalid data len %d proc %d",
					 data_len, proc_num);
				break;
			}
			count_bytes = count_bytes+HDR_SIZ+data_len;
			if (type == DIAG_CTRL_MSG_REG && r >= count_bytes) {
				msg = buf+HDR_SIZ;
				range = buf+HDR_SIZ+
						sizeof(struct diag_ctrl_msg);
				pkt_params->count = msg->count_entries;
				temp = kzalloc(pkt_params->count * sizeof(struct
						 bindpkt_params), GFP_KERNEL);
				if (temp == NULL) {
					pr_alert("diag: Memory alloc fail\n");
					kfree(pkt_params);
					return;
				}
				for (j = 0; j < pkt_params->count; j++) {
					temp->cmd_code = msg->cmd_code;
					temp->subsys_id = msg->subsysid;
					temp->client_id = proc_num;
					temp->proc_id = proc_num;
					temp->cmd_code_lo = range->cmd_code_lo;
					temp->cmd_code_hi = range->cmd_code_hi;
					range++;
					temp++;
				}
				temp -= pkt_params->count;
				pkt_params->params = temp;
				flag = 1;
				diagchar_ioctl(NULL, DIAG_IOCTL_COMMAND_REG,
						 (unsigned long)pkt_params);
				kfree(temp);
			}
			buf = buf + HDR_SIZ + data_len;
		}
	}
	kfree(pkt_params);
	if (flag) {
		/* Poll SMD CNTL channels to check for data */
		if (proc_num == MODEM_PROC)
			diag_smd_cntl_notify(NULL, SMD_EVENT_DATA);
		else if (proc_num == QDSP_PROC)
			diag_smd_qdsp_cntl_notify(NULL, SMD_EVENT_DATA);
		else if (proc_num == WCNSS_PROC)
			diag_smd_wcnss_cntl_notify(NULL, SMD_EVENT_DATA);
	}
}

void diag_read_smd_cntl_work_fn(struct work_struct *work)
{
	diag_smd_cntl_send_req(MODEM_PROC);
}

void diag_read_smd_qdsp_cntl_work_fn(struct work_struct *work)
{
	diag_smd_cntl_send_req(QDSP_PROC);
}

void diag_read_smd_wcnss_cntl_work_fn(struct work_struct *work)
{
	diag_smd_cntl_send_req(WCNSS_PROC);
}

static int diag_smd_cntl_probe(struct platform_device *pdev)
{
	int r = 0;

	/* open control ports only on 8960 & newer targets */
	if (chk_apps_only()) {
		if (pdev->id == SMD_APPS_MODEM)
			r = smd_open("DIAG_CNTL", &driver->ch_cntl, driver,
							diag_smd_cntl_notify);
		if (pdev->id == SMD_APPS_QDSP)
			r = smd_named_open_on_edge("DIAG_CNTL", SMD_APPS_QDSP
				, &driver->chqdsp_cntl, driver,
					 diag_smd_qdsp_cntl_notify);
		if (pdev->id == SMD_APPS_WCNSS)
			r = smd_named_open_on_edge("APPS_RIVA_CTRL",
				SMD_APPS_WCNSS, &driver->ch_wcnss_cntl,
					driver, diag_smd_wcnss_cntl_notify);
		pr_debug("diag: open CNTL port, ID = %d,r = %d\n", pdev->id, r);
	}
	return 0;
}

static int diagfwd_cntl_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "pm_runtime: suspending...\n");
	return 0;
}

static int diagfwd_cntl_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "pm_runtime: resuming...\n");
	return 0;
}

static const struct dev_pm_ops diagfwd_cntl_dev_pm_ops = {
	.runtime_suspend = diagfwd_cntl_runtime_suspend,
	.runtime_resume = diagfwd_cntl_runtime_resume,
};

static struct platform_driver msm_smd_ch1_cntl_driver = {

	.probe = diag_smd_cntl_probe,
	.driver = {
			.name = "DIAG_CNTL",
			.owner = THIS_MODULE,
			.pm   = &diagfwd_cntl_dev_pm_ops,
		   },
};

static struct platform_driver diag_smd_lite_cntl_driver = {

	.probe = diag_smd_cntl_probe,
	.driver = {
			.name = "APPS_RIVA_CTRL",
			.owner = THIS_MODULE,
			.pm   = &diagfwd_cntl_dev_pm_ops,
		   },
};

void diagfwd_cntl_init(void)
{
	driver->polling_reg_flag = 0;
	driver->diag_cntl_wq = create_singlethread_workqueue("diag_cntl_wq");
	if (driver->buf_in_cntl == NULL) {
		driver->buf_in_cntl = kzalloc(IN_BUF_SIZE, GFP_KERNEL);
		if (driver->buf_in_cntl == NULL)
			goto err;
	}
	if (driver->buf_in_qdsp_cntl == NULL) {
		driver->buf_in_qdsp_cntl = kzalloc(IN_BUF_SIZE, GFP_KERNEL);
		if (driver->buf_in_qdsp_cntl == NULL)
			goto err;
	}
	if (driver->buf_in_wcnss_cntl == NULL) {
		driver->buf_in_wcnss_cntl = kzalloc(IN_BUF_SIZE, GFP_KERNEL);
		if (driver->buf_in_wcnss_cntl == NULL)
			goto err;
	}
	platform_driver_register(&msm_smd_ch1_cntl_driver);
	platform_driver_register(&diag_smd_lite_cntl_driver);

	return;
err:
		pr_err("diag: Could not initialize diag buffers");
		kfree(driver->buf_in_cntl);
		kfree(driver->buf_in_qdsp_cntl);
		kfree(driver->buf_in_wcnss_cntl);
		if (driver->diag_cntl_wq)
			destroy_workqueue(driver->diag_cntl_wq);
}

void diagfwd_cntl_exit(void)
{
	smd_close(driver->ch_cntl);
	smd_close(driver->chqdsp_cntl);
	smd_close(driver->ch_wcnss_cntl);
	driver->ch_cntl = 0;
	driver->chqdsp_cntl = 0;
	driver->ch_wcnss_cntl = 0;
	destroy_workqueue(driver->diag_cntl_wq);
	platform_driver_unregister(&msm_smd_ch1_cntl_driver);
	platform_driver_unregister(&diag_smd_lite_cntl_driver);

	kfree(driver->buf_in_cntl);
	kfree(driver->buf_in_qdsp_cntl);
	kfree(driver->buf_in_wcnss_cntl);
}

// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2014 Cisco Systems, Inc.  All rights reserved.

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>

#include "wq_enet_desc.h"
#include "cq_enet_desc.h"
#include "vnic_resource.h"
#include "vnic_dev.h"
#include "vnic_wq.h"
#include "vnic_cq.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "snic.h"

int
snic_get_vnic_config(struct snic *snic)
{
	struct vnic_snic_config *c = &snic->config;
	int ret;

#define GET_CONFIG(m) \
	do { \
		ret = svnic_dev_spec(snic->vdev, \
				     offsetof(struct vnic_snic_config, m), \
				     sizeof(c->m), \
				     &c->m); \
		if (ret) { \
			SNIC_HOST_ERR(snic->shost, \
				      "Error getting %s, %d\n", #m, ret); \
			return ret; \
		} \
	} while (0)

	GET_CONFIG(wq_enet_desc_count);
	GET_CONFIG(maxdatafieldsize);
	GET_CONFIG(intr_timer);
	GET_CONFIG(intr_timer_type);
	GET_CONFIG(flags);
	GET_CONFIG(io_throttle_count);
	GET_CONFIG(port_down_timeout);
	GET_CONFIG(port_down_io_retries);
	GET_CONFIG(luns_per_tgt);
	GET_CONFIG(xpt_type);
	GET_CONFIG(hid);

	c->wq_enet_desc_count = min_t(u32,
				      VNIC_SNIC_WQ_DESCS_MAX,
				      max_t(u32,
					    VNIC_SNIC_WQ_DESCS_MIN,
					    c->wq_enet_desc_count));

	c->wq_enet_desc_count = ALIGN(c->wq_enet_desc_count, 16);

	c->maxdatafieldsize = min_t(u32,
				    VNIC_SNIC_MAXDATAFIELDSIZE_MAX,
				    max_t(u32,
					  VNIC_SNIC_MAXDATAFIELDSIZE_MIN,
					  c->maxdatafieldsize));

	c->io_throttle_count = min_t(u32,
				     VNIC_SNIC_IO_THROTTLE_COUNT_MAX,
				     max_t(u32,
					   VNIC_SNIC_IO_THROTTLE_COUNT_MIN,
					   c->io_throttle_count));

	c->port_down_timeout = min_t(u32,
				     VNIC_SNIC_PORT_DOWN_TIMEOUT_MAX,
				     c->port_down_timeout);

	c->port_down_io_retries = min_t(u32,
				     VNIC_SNIC_PORT_DOWN_IO_RETRIES_MAX,
				     c->port_down_io_retries);

	c->luns_per_tgt = min_t(u32,
				VNIC_SNIC_LUNS_PER_TARGET_MAX,
				max_t(u32,
				      VNIC_SNIC_LUNS_PER_TARGET_MIN,
				      c->luns_per_tgt));

	c->intr_timer = min_t(u32, VNIC_INTR_TIMER_MAX, c->intr_timer);

	SNIC_INFO("vNIC resources wq %d\n", c->wq_enet_desc_count);
	SNIC_INFO("vNIC mtu %d intr timer %d\n",
		  c->maxdatafieldsize,
		  c->intr_timer);

	SNIC_INFO("vNIC flags 0x%x luns per tgt %d\n",
		  c->flags,
		  c->luns_per_tgt);

	SNIC_INFO("vNIC io throttle count %d\n", c->io_throttle_count);
	SNIC_INFO("vNIC port down timeout %d port down io retries %d\n",
		  c->port_down_timeout,
		  c->port_down_io_retries);

	SNIC_INFO("vNIC back end type = %d\n", c->xpt_type);
	SNIC_INFO("vNIC hid = %d\n", c->hid);

	return 0;
}

void
snic_get_res_counts(struct snic *snic)
{
	snic->wq_count = svnic_dev_get_res_count(snic->vdev, RES_TYPE_WQ);
	SNIC_BUG_ON(snic->wq_count == 0);
	snic->cq_count = svnic_dev_get_res_count(snic->vdev, RES_TYPE_CQ);
	SNIC_BUG_ON(snic->cq_count == 0);
	snic->intr_count = svnic_dev_get_res_count(snic->vdev,
						  RES_TYPE_INTR_CTRL);
	SNIC_BUG_ON(snic->intr_count == 0);
}

void
snic_free_vnic_res(struct snic *snic)
{
	unsigned int i;

	for (i = 0; i < snic->wq_count; i++)
		svnic_wq_free(&snic->wq[i]);

	for (i = 0; i < snic->cq_count; i++)
		svnic_cq_free(&snic->cq[i]);

	for (i = 0; i < snic->intr_count; i++)
		svnic_intr_free(&snic->intr[i]);
}

int
snic_alloc_vnic_res(struct snic *snic)
{
	enum vnic_dev_intr_mode intr_mode;
	unsigned int mask_on_assertion;
	unsigned int intr_offset;
	unsigned int err_intr_enable;
	unsigned int err_intr_offset;
	unsigned int i;
	int ret;

	intr_mode = svnic_dev_get_intr_mode(snic->vdev);

	SNIC_INFO("vNIC interrupt mode: %s\n",
		  ((intr_mode == VNIC_DEV_INTR_MODE_INTX) ?
		   "Legacy PCI INTx" :
		   ((intr_mode == VNIC_DEV_INTR_MODE_MSI) ?
		    "MSI" :
		    ((intr_mode == VNIC_DEV_INTR_MODE_MSIX) ?
		     "MSI-X" : "Unknown"))));

	/* only MSI-X is supported */
	SNIC_BUG_ON(intr_mode != VNIC_DEV_INTR_MODE_MSIX);

	SNIC_INFO("wq %d cq %d intr %d\n", snic->wq_count,
		  snic->cq_count,
		  snic->intr_count);


	/* Allocate WQs used for SCSI IOs */
	for (i = 0; i < snic->wq_count; i++) {
		ret = svnic_wq_alloc(snic->vdev,
				     &snic->wq[i],
				     i,
				     snic->config.wq_enet_desc_count,
				     sizeof(struct wq_enet_desc));
		if (ret)
			goto error_cleanup;
	}

	/* CQ for each WQ */
	for (i = 0; i < snic->wq_count; i++) {
		ret = svnic_cq_alloc(snic->vdev,
				     &snic->cq[i],
				     i,
				     snic->config.wq_enet_desc_count,
				     sizeof(struct cq_enet_wq_desc));
		if (ret)
			goto error_cleanup;
	}

	SNIC_BUG_ON(snic->cq_count != 2 * snic->wq_count);
	/* CQ for FW TO host */
	for (i = snic->wq_count; i < snic->cq_count; i++) {
		ret = svnic_cq_alloc(snic->vdev,
				     &snic->cq[i],
				     i,
				     (snic->config.wq_enet_desc_count * 3),
				     sizeof(struct snic_fw_req));
		if (ret)
			goto error_cleanup;
	}

	for (i = 0; i < snic->intr_count; i++) {
		ret = svnic_intr_alloc(snic->vdev, &snic->intr[i], i);
		if (ret)
			goto error_cleanup;
	}

	/*
	 * Init WQ Resources.
	 * WQ[0 to n] points to CQ[0 to n-1]
	 * firmware to host comm points to CQ[n to m+1]
	 */
	err_intr_enable = 1;
	err_intr_offset = snic->err_intr_offset;

	for (i = 0; i < snic->wq_count; i++) {
		svnic_wq_init(&snic->wq[i],
			      i,
			      err_intr_enable,
			      err_intr_offset);
	}

	for (i = 0; i < snic->cq_count; i++) {
		intr_offset = i;

		svnic_cq_init(&snic->cq[i],
			      0 /* flow_control_enable */,
			      1 /* color_enable */,
			      0 /* cq_head */,
			      0 /* cq_tail */,
			      1 /* cq_tail_color */,
			      1 /* interrupt_enable */,
			      1 /* cq_entry_enable */,
			      0 /* cq_message_enable */,
			      intr_offset,
			      0 /* cq_message_addr */);
	}

	/*
	 * Init INTR resources
	 * Assumption : snic is always in MSI-X mode
	 */
	SNIC_BUG_ON(intr_mode != VNIC_DEV_INTR_MODE_MSIX);
	mask_on_assertion = 1;

	for (i = 0; i < snic->intr_count; i++) {
		svnic_intr_init(&snic->intr[i],
				snic->config.intr_timer,
				snic->config.intr_timer_type,
				mask_on_assertion);
	}

	/* init the stats memory by making the first call here */
	ret = svnic_dev_stats_dump(snic->vdev, &snic->stats);
	if (ret) {
		SNIC_HOST_ERR(snic->shost,
			      "svnic_dev_stats_dump failed - x%x\n",
			      ret);
		goto error_cleanup;
	}

	/* Clear LIF stats */
	svnic_dev_stats_clear(snic->vdev);
	ret = 0;

	return ret;

error_cleanup:
	snic_free_vnic_res(snic);

	return ret;
}

void
snic_log_q_error(struct snic *snic)
{
	unsigned int i;
	u32 err_status;

	for (i = 0; i < snic->wq_count; i++) {
		err_status = ioread32(&snic->wq[i].ctrl->error_status);
		if (err_status)
			SNIC_HOST_ERR(snic->shost,
				      "WQ[%d] error status %d\n",
				      i,
				      err_status);
	}
} /* end of snic_log_q_error */

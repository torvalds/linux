/* ------------------------------------------------------------
 * iSeries_vscsi.c
 * (C) Copyright IBM Corporation 1994, 2003
 * Authors: Colin DeVilbiss (devilbis@us.ibm.com)
 *          Santiago Leon (santil@us.ibm.com)
 *          Dave Boutcher (sleddog@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 * ------------------------------------------------------------
 * iSeries-specific functions of the SCSI host adapter for Virtual I/O devices
 *
 * This driver allows the Linux SCSI peripheral drivers to directly
 * access devices in the hosting partition, either on an iSeries
 * hypervisor system or a converged hypervisor system.
 */

#include <asm/iseries/vio.h>
#include <asm/iseries/hv_lp_event.h>
#include <asm/iseries/hv_types.h>
#include <asm/iseries/hv_lp_config.h>
#include <asm/vio.h>
#include <linux/device.h>
#include "ibmvscsi.h"

/* global variables */
static struct ibmvscsi_host_data *single_host_data;

/* ------------------------------------------------------------
 * Routines for direct interpartition interaction
 */
struct srp_lp_event {
	struct HvLpEvent lpevt;	/* 0x00-0x17          */
	u32 reserved1;		/* 0x18-0x1B; unused  */
	u16 version;		/* 0x1C-0x1D; unused  */
	u16 subtype_rc;		/* 0x1E-0x1F; unused  */
	struct viosrp_crq crq;	/* 0x20-0x3F          */
};

/** 
 * standard interface for handling logical partition events.
 */
static void ibmvscsi_handle_event(struct HvLpEvent *lpevt)
{
	struct srp_lp_event *evt = (struct srp_lp_event *)lpevt;

	if (!evt) {
		printk(KERN_ERR "ibmvscsi: received null event\n");
		return;
	}

	if (single_host_data == NULL) {
		printk(KERN_ERR
		       "ibmvscsi: received event, no adapter present\n");
		return;
	}

	ibmvscsi_handle_crq(&evt->crq, single_host_data);
}

/* ------------------------------------------------------------
 * Routines for driver initialization
 */
int ibmvscsi_init_crq_queue(struct crq_queue *queue,
			    struct ibmvscsi_host_data *hostdata,
			    int max_requests)
{
	int rc;

	single_host_data = hostdata;
	rc = viopath_open(viopath_hostLp, viomajorsubtype_scsi, 0);
	if (rc < 0) {
		printk("viopath_open failed with rc %d in open_event_path\n",
		       rc);
		goto viopath_open_failed;
	}

	rc = vio_setHandler(viomajorsubtype_scsi, ibmvscsi_handle_event);
	if (rc < 0) {
		printk("vio_setHandler failed with rc %d in open_event_path\n",
		       rc);
		goto vio_setHandler_failed;
	}
	return 0;

      vio_setHandler_failed:
	viopath_close(viopath_hostLp, viomajorsubtype_scsi, max_requests);
      viopath_open_failed:
	return -1;
}

void ibmvscsi_release_crq_queue(struct crq_queue *queue,
				struct ibmvscsi_host_data *hostdata,
				int max_requests)
{
	vio_clearHandler(viomajorsubtype_scsi);
	viopath_close(viopath_hostLp, viomajorsubtype_scsi, max_requests);
}

/**
 * reset_crq_queue: - resets a crq after a failure
 * @queue:	crq_queue to initialize and register
 * @hostdata:	ibmvscsi_host_data of host
 *
 * no-op for iSeries
 */
void ibmvscsi_reset_crq_queue(struct crq_queue *queue,
			      struct ibmvscsi_host_data *hostdata)
{
}

/**
 * ibmvscsi_send_crq: - Send a CRQ
 * @hostdata:	the adapter
 * @word1:	the first 64 bits of the data
 * @word2:	the second 64 bits of the data
 */
int ibmvscsi_send_crq(struct ibmvscsi_host_data *hostdata, u64 word1, u64 word2)
{
	single_host_data = hostdata;
	return HvCallEvent_signalLpEventFast(viopath_hostLp,
					     HvLpEvent_Type_VirtualIo,
					     viomajorsubtype_scsi,
					     HvLpEvent_AckInd_NoAck,
					     HvLpEvent_AckType_ImmediateAck,
					     viopath_sourceinst(viopath_hostLp),
					     viopath_targetinst(viopath_hostLp),
					     0,
					     VIOVERSION << 16, word1, word2, 0,
					     0);
}

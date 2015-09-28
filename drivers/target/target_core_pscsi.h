#ifndef TARGET_CORE_PSCSI_H
#define TARGET_CORE_PSCSI_H

#define PSCSI_VERSION		"v4.0"

/* used in pscsi_find_alloc_len() */
#ifndef INQUIRY_DATA_SIZE
#define INQUIRY_DATA_SIZE	0x24
#endif

/* used in pscsi_add_device_to_list() */
#define PSCSI_DEFAULT_QUEUEDEPTH	1

#define PS_RETRY		5
#define PS_TIMEOUT_DISK		(15*HZ)
#define PS_TIMEOUT_OTHER	(500*HZ)

#include <linux/device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_device.h>
#include <linux/kref.h>
#include <linux/kobject.h>

struct pscsi_plugin_task {
	unsigned char pscsi_sense[SCSI_SENSE_BUFFERSIZE];
	int	pscsi_direction;
	int	pscsi_result;
	u32	pscsi_resid;
	unsigned char pscsi_cdb[0];
} ____cacheline_aligned;

#define PDF_HAS_CHANNEL_ID	0x01
#define PDF_HAS_TARGET_ID	0x02
#define PDF_HAS_LUN_ID		0x04
#define PDF_HAS_VPD_UNIT_SERIAL 0x08
#define PDF_HAS_VPD_DEV_IDENT	0x10
#define PDF_HAS_VIRT_HOST_ID	0x20

struct pscsi_dev_virt {
	struct se_device dev;
	int	pdv_flags;
	int	pdv_host_id;
	int	pdv_channel_id;
	int	pdv_target_id;
	int	pdv_lun_id;
	struct block_device *pdv_bd;
	struct scsi_device *pdv_sd;
	struct Scsi_Host *pdv_lld_host;
} ____cacheline_aligned;

typedef enum phv_modes {
	PHV_VIRTUAL_HOST_ID,
	PHV_LLD_SCSI_HOST_NO
} phv_modes_t;

struct pscsi_hba_virt {
	int			phv_host_id;
	phv_modes_t		phv_mode;
	struct Scsi_Host	*phv_lld_host;
} ____cacheline_aligned;

#endif   /*** TARGET_CORE_PSCSI_H ***/

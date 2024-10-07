// SPDX-License-Identifier: GPL-2.0
/*
 *    driver for Microchip PQI-based storage controllers
 *    Copyright (c) 2019-2023 Microchip Technology Inc. and its subsidiaries
 *    Copyright (c) 2016-2018 Microsemi Corporation
 *    Copyright (c) 2016 PMC-Sierra, Inc.
 *
 *    Questions/Comments/Bugfixes to storagedev@microchip.com
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/reboot.h>
#include <linux/cciss_ioctl.h>
#include <linux/blk-mq-pci.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_transport_sas.h>
#include <linux/unaligned.h>
#include "smartpqi.h"
#include "smartpqi_sis.h"

#if !defined(BUILD_TIMESTAMP)
#define BUILD_TIMESTAMP
#endif

#define DRIVER_VERSION		"2.1.30-031"
#define DRIVER_MAJOR		2
#define DRIVER_MINOR		1
#define DRIVER_RELEASE		30
#define DRIVER_REVISION		31

#define DRIVER_NAME		"Microchip SmartPQI Driver (v" \
				DRIVER_VERSION BUILD_TIMESTAMP ")"
#define DRIVER_NAME_SHORT	"smartpqi"

#define PQI_EXTRA_SGL_MEMORY	(12 * sizeof(struct pqi_sg_descriptor))

#define PQI_POST_RESET_DELAY_SECS			5
#define PQI_POST_OFA_RESET_DELAY_UPON_TIMEOUT_SECS	10

#define PQI_NO_COMPLETION	((void *)-1)

MODULE_AUTHOR("Microchip");
MODULE_DESCRIPTION("Driver for Microchip Smart Family Controller version "
	DRIVER_VERSION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

struct pqi_cmd_priv {
	int this_residual;
};

static struct pqi_cmd_priv *pqi_cmd_priv(struct scsi_cmnd *cmd)
{
	return scsi_cmd_priv(cmd);
}

static void pqi_verify_structures(void);
static void pqi_take_ctrl_offline(struct pqi_ctrl_info *ctrl_info,
	enum pqi_ctrl_shutdown_reason ctrl_shutdown_reason);
static void pqi_ctrl_offline_worker(struct work_struct *work);
static int pqi_scan_scsi_devices(struct pqi_ctrl_info *ctrl_info);
static void pqi_scan_start(struct Scsi_Host *shost);
static void pqi_start_io(struct pqi_ctrl_info *ctrl_info,
	struct pqi_queue_group *queue_group, enum pqi_io_path path,
	struct pqi_io_request *io_request);
static int pqi_submit_raid_request_synchronous(struct pqi_ctrl_info *ctrl_info,
	struct pqi_iu_header *request, unsigned int flags,
	struct pqi_raid_error_info *error_info);
static int pqi_aio_submit_io(struct pqi_ctrl_info *ctrl_info,
	struct scsi_cmnd *scmd, u32 aio_handle, u8 *cdb,
	unsigned int cdb_length, struct pqi_queue_group *queue_group,
	struct pqi_encryption_info *encryption_info, bool raid_bypass, bool io_high_prio);
static  int pqi_aio_submit_r1_write_io(struct pqi_ctrl_info *ctrl_info,
	struct scsi_cmnd *scmd, struct pqi_queue_group *queue_group,
	struct pqi_encryption_info *encryption_info, struct pqi_scsi_dev *device,
	struct pqi_scsi_dev_raid_map_data *rmd);
static int pqi_aio_submit_r56_write_io(struct pqi_ctrl_info *ctrl_info,
	struct scsi_cmnd *scmd, struct pqi_queue_group *queue_group,
	struct pqi_encryption_info *encryption_info, struct pqi_scsi_dev *device,
	struct pqi_scsi_dev_raid_map_data *rmd);
static void pqi_ofa_ctrl_quiesce(struct pqi_ctrl_info *ctrl_info);
static void pqi_ofa_ctrl_unquiesce(struct pqi_ctrl_info *ctrl_info);
static int pqi_ofa_ctrl_restart(struct pqi_ctrl_info *ctrl_info, unsigned int delay_secs);
static void pqi_host_setup_buffer(struct pqi_ctrl_info *ctrl_info, struct pqi_host_memory_descriptor *host_memory_descriptor, u32 total_size, u32 min_size);
static void pqi_host_free_buffer(struct pqi_ctrl_info *ctrl_info, struct pqi_host_memory_descriptor *host_memory_descriptor);
static int pqi_host_memory_update(struct pqi_ctrl_info *ctrl_info, struct pqi_host_memory_descriptor *host_memory_descriptor, u16 function_code);
static int pqi_device_wait_for_pending_io(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device, u8 lun, unsigned long timeout_msecs);
static void pqi_fail_all_outstanding_requests(struct pqi_ctrl_info *ctrl_info);
static void pqi_tmf_worker(struct work_struct *work);

/* for flags argument to pqi_submit_raid_request_synchronous() */
#define PQI_SYNC_FLAGS_INTERRUPTABLE	0x1

static struct scsi_transport_template *pqi_sas_transport_template;

static atomic_t pqi_controller_count = ATOMIC_INIT(0);

enum pqi_lockup_action {
	NONE,
	REBOOT,
	PANIC
};

static enum pqi_lockup_action pqi_lockup_action = NONE;

static struct {
	enum pqi_lockup_action	action;
	char			*name;
} pqi_lockup_actions[] = {
	{
		.action = NONE,
		.name = "none",
	},
	{
		.action = REBOOT,
		.name = "reboot",
	},
	{
		.action = PANIC,
		.name = "panic",
	},
};

static unsigned int pqi_supported_event_types[] = {
	PQI_EVENT_TYPE_HOTPLUG,
	PQI_EVENT_TYPE_HARDWARE,
	PQI_EVENT_TYPE_PHYSICAL_DEVICE,
	PQI_EVENT_TYPE_LOGICAL_DEVICE,
	PQI_EVENT_TYPE_OFA,
	PQI_EVENT_TYPE_AIO_STATE_CHANGE,
	PQI_EVENT_TYPE_AIO_CONFIG_CHANGE,
};

static int pqi_disable_device_id_wildcards;
module_param_named(disable_device_id_wildcards,
	pqi_disable_device_id_wildcards, int, 0644);
MODULE_PARM_DESC(disable_device_id_wildcards,
	"Disable device ID wildcards.");

static int pqi_disable_heartbeat;
module_param_named(disable_heartbeat,
	pqi_disable_heartbeat, int, 0644);
MODULE_PARM_DESC(disable_heartbeat,
	"Disable heartbeat.");

static int pqi_disable_ctrl_shutdown;
module_param_named(disable_ctrl_shutdown,
	pqi_disable_ctrl_shutdown, int, 0644);
MODULE_PARM_DESC(disable_ctrl_shutdown,
	"Disable controller shutdown when controller locked up.");

static char *pqi_lockup_action_param;
module_param_named(lockup_action,
	pqi_lockup_action_param, charp, 0644);
MODULE_PARM_DESC(lockup_action, "Action to take when controller locked up.\n"
	"\t\tSupported: none, reboot, panic\n"
	"\t\tDefault: none");

static int pqi_expose_ld_first;
module_param_named(expose_ld_first,
	pqi_expose_ld_first, int, 0644);
MODULE_PARM_DESC(expose_ld_first, "Expose logical drives before physical drives.");

static int pqi_hide_vsep;
module_param_named(hide_vsep,
	pqi_hide_vsep, int, 0644);
MODULE_PARM_DESC(hide_vsep, "Hide the virtual SEP for direct attached drives.");

static int pqi_disable_managed_interrupts;
module_param_named(disable_managed_interrupts,
	pqi_disable_managed_interrupts, int, 0644);
MODULE_PARM_DESC(disable_managed_interrupts,
	"Disable the kernel automatically assigning SMP affinity to IRQs.");

static unsigned int pqi_ctrl_ready_timeout_secs;
module_param_named(ctrl_ready_timeout,
	pqi_ctrl_ready_timeout_secs, uint, 0644);
MODULE_PARM_DESC(ctrl_ready_timeout,
	"Timeout in seconds for driver to wait for controller ready.");

static char *raid_levels[] = {
	"RAID-0",
	"RAID-4",
	"RAID-1(1+0)",
	"RAID-5",
	"RAID-5+1",
	"RAID-6",
	"RAID-1(Triple)",
};

static char *pqi_raid_level_to_string(u8 raid_level)
{
	if (raid_level < ARRAY_SIZE(raid_levels))
		return raid_levels[raid_level];

	return "RAID UNKNOWN";
}

#define SA_RAID_0		0
#define SA_RAID_4		1
#define SA_RAID_1		2	/* also used for RAID 10 */
#define SA_RAID_5		3	/* also used for RAID 50 */
#define SA_RAID_51		4
#define SA_RAID_6		5	/* also used for RAID 60 */
#define SA_RAID_TRIPLE		6	/* also used for RAID 1+0 Triple */
#define SA_RAID_MAX		SA_RAID_TRIPLE
#define SA_RAID_UNKNOWN		0xff

static inline void pqi_scsi_done(struct scsi_cmnd *scmd)
{
	pqi_prep_for_scsi_done(scmd);
	scsi_done(scmd);
}

static inline void pqi_disable_write_same(struct scsi_device *sdev)
{
	sdev->no_write_same = 1;
}

static inline bool pqi_scsi3addr_equal(u8 *scsi3addr1, u8 *scsi3addr2)
{
	return memcmp(scsi3addr1, scsi3addr2, 8) == 0;
}

static inline bool pqi_is_logical_device(struct pqi_scsi_dev *device)
{
	return !device->is_physical_device;
}

static inline bool pqi_is_external_raid_addr(u8 *scsi3addr)
{
	return scsi3addr[2] != 0;
}

static inline bool pqi_ctrl_offline(struct pqi_ctrl_info *ctrl_info)
{
	return !ctrl_info->controller_online;
}

static inline void pqi_check_ctrl_health(struct pqi_ctrl_info *ctrl_info)
{
	if (ctrl_info->controller_online)
		if (!sis_is_firmware_running(ctrl_info))
			pqi_take_ctrl_offline(ctrl_info, PQI_FIRMWARE_KERNEL_NOT_UP);
}

static inline bool pqi_is_hba_lunid(u8 *scsi3addr)
{
	return pqi_scsi3addr_equal(scsi3addr, RAID_CTLR_LUNID);
}

#define PQI_DRIVER_SCRATCH_PQI_MODE			0x1
#define PQI_DRIVER_SCRATCH_FW_TRIAGE_SUPPORTED		0x2

static inline enum pqi_ctrl_mode pqi_get_ctrl_mode(struct pqi_ctrl_info *ctrl_info)
{
	return sis_read_driver_scratch(ctrl_info) & PQI_DRIVER_SCRATCH_PQI_MODE ? PQI_MODE : SIS_MODE;
}

static inline void pqi_save_ctrl_mode(struct pqi_ctrl_info *ctrl_info,
	enum pqi_ctrl_mode mode)
{
	u32 driver_scratch;

	driver_scratch = sis_read_driver_scratch(ctrl_info);

	if (mode == PQI_MODE)
		driver_scratch |= PQI_DRIVER_SCRATCH_PQI_MODE;
	else
		driver_scratch &= ~PQI_DRIVER_SCRATCH_PQI_MODE;

	sis_write_driver_scratch(ctrl_info, driver_scratch);
}

static inline bool pqi_is_fw_triage_supported(struct pqi_ctrl_info *ctrl_info)
{
	return (sis_read_driver_scratch(ctrl_info) & PQI_DRIVER_SCRATCH_FW_TRIAGE_SUPPORTED) != 0;
}

static inline void pqi_save_fw_triage_setting(struct pqi_ctrl_info *ctrl_info, bool is_supported)
{
	u32 driver_scratch;

	driver_scratch = sis_read_driver_scratch(ctrl_info);

	if (is_supported)
		driver_scratch |= PQI_DRIVER_SCRATCH_FW_TRIAGE_SUPPORTED;
	else
		driver_scratch &= ~PQI_DRIVER_SCRATCH_FW_TRIAGE_SUPPORTED;

	sis_write_driver_scratch(ctrl_info, driver_scratch);
}

static inline void pqi_ctrl_block_scan(struct pqi_ctrl_info *ctrl_info)
{
	ctrl_info->scan_blocked = true;
	mutex_lock(&ctrl_info->scan_mutex);
}

static inline void pqi_ctrl_unblock_scan(struct pqi_ctrl_info *ctrl_info)
{
	ctrl_info->scan_blocked = false;
	mutex_unlock(&ctrl_info->scan_mutex);
}

static inline bool pqi_ctrl_scan_blocked(struct pqi_ctrl_info *ctrl_info)
{
	return ctrl_info->scan_blocked;
}

static inline void pqi_ctrl_block_device_reset(struct pqi_ctrl_info *ctrl_info)
{
	mutex_lock(&ctrl_info->lun_reset_mutex);
}

static inline void pqi_ctrl_unblock_device_reset(struct pqi_ctrl_info *ctrl_info)
{
	mutex_unlock(&ctrl_info->lun_reset_mutex);
}

static inline void pqi_scsi_block_requests(struct pqi_ctrl_info *ctrl_info)
{
	struct Scsi_Host *shost;
	unsigned int num_loops;
	int msecs_sleep;

	shost = ctrl_info->scsi_host;

	scsi_block_requests(shost);

	num_loops = 0;
	msecs_sleep = 20;
	while (scsi_host_busy(shost)) {
		num_loops++;
		if (num_loops == 10)
			msecs_sleep = 500;
		msleep(msecs_sleep);
	}
}

static inline void pqi_scsi_unblock_requests(struct pqi_ctrl_info *ctrl_info)
{
	scsi_unblock_requests(ctrl_info->scsi_host);
}

static inline void pqi_ctrl_busy(struct pqi_ctrl_info *ctrl_info)
{
	atomic_inc(&ctrl_info->num_busy_threads);
}

static inline void pqi_ctrl_unbusy(struct pqi_ctrl_info *ctrl_info)
{
	atomic_dec(&ctrl_info->num_busy_threads);
}

static inline bool pqi_ctrl_blocked(struct pqi_ctrl_info *ctrl_info)
{
	return ctrl_info->block_requests;
}

static inline void pqi_ctrl_block_requests(struct pqi_ctrl_info *ctrl_info)
{
	ctrl_info->block_requests = true;
}

static inline void pqi_ctrl_unblock_requests(struct pqi_ctrl_info *ctrl_info)
{
	ctrl_info->block_requests = false;
	wake_up_all(&ctrl_info->block_requests_wait);
}

static void pqi_wait_if_ctrl_blocked(struct pqi_ctrl_info *ctrl_info)
{
	if (!pqi_ctrl_blocked(ctrl_info))
		return;

	atomic_inc(&ctrl_info->num_blocked_threads);
	wait_event(ctrl_info->block_requests_wait,
		!pqi_ctrl_blocked(ctrl_info));
	atomic_dec(&ctrl_info->num_blocked_threads);
}

#define PQI_QUIESCE_WARNING_TIMEOUT_SECS		10

static inline void pqi_ctrl_wait_until_quiesced(struct pqi_ctrl_info *ctrl_info)
{
	unsigned long start_jiffies;
	unsigned long warning_timeout;
	bool displayed_warning;

	displayed_warning = false;
	start_jiffies = jiffies;
	warning_timeout = (PQI_QUIESCE_WARNING_TIMEOUT_SECS * HZ) + start_jiffies;

	while (atomic_read(&ctrl_info->num_busy_threads) >
		atomic_read(&ctrl_info->num_blocked_threads)) {
		if (time_after(jiffies, warning_timeout)) {
			dev_warn(&ctrl_info->pci_dev->dev,
				"waiting %u seconds for driver activity to quiesce\n",
				jiffies_to_msecs(jiffies - start_jiffies) / 1000);
			displayed_warning = true;
			warning_timeout = (PQI_QUIESCE_WARNING_TIMEOUT_SECS * HZ) + jiffies;
		}
		usleep_range(1000, 2000);
	}

	if (displayed_warning)
		dev_warn(&ctrl_info->pci_dev->dev,
			"driver activity quiesced after waiting for %u seconds\n",
			jiffies_to_msecs(jiffies - start_jiffies) / 1000);
}

static inline bool pqi_device_offline(struct pqi_scsi_dev *device)
{
	return device->device_offline;
}

static inline void pqi_ctrl_ofa_start(struct pqi_ctrl_info *ctrl_info)
{
	mutex_lock(&ctrl_info->ofa_mutex);
}

static inline void pqi_ctrl_ofa_done(struct pqi_ctrl_info *ctrl_info)
{
	mutex_unlock(&ctrl_info->ofa_mutex);
}

static inline void pqi_wait_until_ofa_finished(struct pqi_ctrl_info *ctrl_info)
{
	mutex_lock(&ctrl_info->ofa_mutex);
	mutex_unlock(&ctrl_info->ofa_mutex);
}

static inline bool pqi_ofa_in_progress(struct pqi_ctrl_info *ctrl_info)
{
	return mutex_is_locked(&ctrl_info->ofa_mutex);
}

static inline void pqi_device_remove_start(struct pqi_scsi_dev *device)
{
	device->in_remove = true;
}

static inline bool pqi_device_in_remove(struct pqi_scsi_dev *device)
{
	return device->in_remove;
}

static inline void pqi_device_reset_start(struct pqi_scsi_dev *device, u8 lun)
{
	device->in_reset[lun] = true;
}

static inline void pqi_device_reset_done(struct pqi_scsi_dev *device, u8 lun)
{
	device->in_reset[lun] = false;
}

static inline bool pqi_device_in_reset(struct pqi_scsi_dev *device, u8 lun)
{
	return device->in_reset[lun];
}

static inline int pqi_event_type_to_event_index(unsigned int event_type)
{
	int index;

	for (index = 0; index < ARRAY_SIZE(pqi_supported_event_types); index++)
		if (event_type == pqi_supported_event_types[index])
			return index;

	return -1;
}

static inline bool pqi_is_supported_event(unsigned int event_type)
{
	return pqi_event_type_to_event_index(event_type) != -1;
}

static inline void pqi_schedule_rescan_worker_with_delay(struct pqi_ctrl_info *ctrl_info,
	unsigned long delay)
{
	if (pqi_ctrl_offline(ctrl_info))
		return;

	schedule_delayed_work(&ctrl_info->rescan_work, delay);
}

static inline void pqi_schedule_rescan_worker(struct pqi_ctrl_info *ctrl_info)
{
	pqi_schedule_rescan_worker_with_delay(ctrl_info, 0);
}

#define PQI_RESCAN_WORK_DELAY	(10 * HZ)

static inline void pqi_schedule_rescan_worker_delayed(struct pqi_ctrl_info *ctrl_info)
{
	pqi_schedule_rescan_worker_with_delay(ctrl_info, PQI_RESCAN_WORK_DELAY);
}

static inline void pqi_cancel_rescan_worker(struct pqi_ctrl_info *ctrl_info)
{
	cancel_delayed_work_sync(&ctrl_info->rescan_work);
}

static inline u32 pqi_read_heartbeat_counter(struct pqi_ctrl_info *ctrl_info)
{
	if (!ctrl_info->heartbeat_counter)
		return 0;

	return readl(ctrl_info->heartbeat_counter);
}

static inline u8 pqi_read_soft_reset_status(struct pqi_ctrl_info *ctrl_info)
{
	return readb(ctrl_info->soft_reset_status);
}

static inline void pqi_clear_soft_reset_status(struct pqi_ctrl_info *ctrl_info)
{
	u8 status;

	status = pqi_read_soft_reset_status(ctrl_info);
	status &= ~PQI_SOFT_RESET_ABORT;
	writeb(status, ctrl_info->soft_reset_status);
}

static inline bool pqi_is_io_high_priority(struct pqi_scsi_dev *device, struct scsi_cmnd *scmd)
{
	bool io_high_prio;
	int priority_class;

	io_high_prio = false;

	if (device->ncq_prio_enable) {
		priority_class =
			IOPRIO_PRIO_CLASS(req_get_ioprio(scsi_cmd_to_rq(scmd)));
		if (priority_class == IOPRIO_CLASS_RT) {
			/* Set NCQ priority for read/write commands. */
			switch (scmd->cmnd[0]) {
			case WRITE_16:
			case READ_16:
			case WRITE_12:
			case READ_12:
			case WRITE_10:
			case READ_10:
			case WRITE_6:
			case READ_6:
				io_high_prio = true;
				break;
			}
		}
	}

	return io_high_prio;
}

static int pqi_map_single(struct pci_dev *pci_dev,
	struct pqi_sg_descriptor *sg_descriptor, void *buffer,
	size_t buffer_length, enum dma_data_direction data_direction)
{
	dma_addr_t bus_address;

	if (!buffer || buffer_length == 0 || data_direction == DMA_NONE)
		return 0;

	bus_address = dma_map_single(&pci_dev->dev, buffer, buffer_length,
		data_direction);
	if (dma_mapping_error(&pci_dev->dev, bus_address))
		return -ENOMEM;

	put_unaligned_le64((u64)bus_address, &sg_descriptor->address);
	put_unaligned_le32(buffer_length, &sg_descriptor->length);
	put_unaligned_le32(CISS_SG_LAST, &sg_descriptor->flags);

	return 0;
}

static void pqi_pci_unmap(struct pci_dev *pci_dev,
	struct pqi_sg_descriptor *descriptors, int num_descriptors,
	enum dma_data_direction data_direction)
{
	int i;

	if (data_direction == DMA_NONE)
		return;

	for (i = 0; i < num_descriptors; i++)
		dma_unmap_single(&pci_dev->dev,
			(dma_addr_t)get_unaligned_le64(&descriptors[i].address),
			get_unaligned_le32(&descriptors[i].length),
			data_direction);
}

static int pqi_build_raid_path_request(struct pqi_ctrl_info *ctrl_info,
	struct pqi_raid_path_request *request, u8 cmd,
	u8 *scsi3addr, void *buffer, size_t buffer_length,
	u16 vpd_page, enum dma_data_direction *dir)
{
	u8 *cdb;
	size_t cdb_length = buffer_length;

	memset(request, 0, sizeof(*request));

	request->header.iu_type = PQI_REQUEST_IU_RAID_PATH_IO;
	put_unaligned_le16(offsetof(struct pqi_raid_path_request,
		sg_descriptors[1]) - PQI_REQUEST_HEADER_LENGTH,
		&request->header.iu_length);
	put_unaligned_le32(buffer_length, &request->buffer_length);
	memcpy(request->lun_number, scsi3addr, sizeof(request->lun_number));
	request->task_attribute = SOP_TASK_ATTRIBUTE_SIMPLE;
	request->additional_cdb_bytes_usage = SOP_ADDITIONAL_CDB_BYTES_0;

	cdb = request->cdb;

	switch (cmd) {
	case INQUIRY:
		request->data_direction = SOP_READ_FLAG;
		cdb[0] = INQUIRY;
		if (vpd_page & VPD_PAGE) {
			cdb[1] = 0x1;
			cdb[2] = (u8)vpd_page;
		}
		cdb[4] = (u8)cdb_length;
		break;
	case CISS_REPORT_LOG:
	case CISS_REPORT_PHYS:
		request->data_direction = SOP_READ_FLAG;
		cdb[0] = cmd;
		if (cmd == CISS_REPORT_PHYS) {
			if (ctrl_info->rpl_extended_format_4_5_supported)
				cdb[1] = CISS_REPORT_PHYS_FLAG_EXTENDED_FORMAT_4;
			else
				cdb[1] = CISS_REPORT_PHYS_FLAG_EXTENDED_FORMAT_2;
		} else {
			cdb[1] = ctrl_info->ciss_report_log_flags;
		}
		put_unaligned_be32(cdb_length, &cdb[6]);
		break;
	case CISS_GET_RAID_MAP:
		request->data_direction = SOP_READ_FLAG;
		cdb[0] = CISS_READ;
		cdb[1] = CISS_GET_RAID_MAP;
		put_unaligned_be32(cdb_length, &cdb[6]);
		break;
	case SA_FLUSH_CACHE:
		request->header.driver_flags = PQI_DRIVER_NONBLOCKABLE_REQUEST;
		request->data_direction = SOP_WRITE_FLAG;
		cdb[0] = BMIC_WRITE;
		cdb[6] = BMIC_FLUSH_CACHE;
		put_unaligned_be16(cdb_length, &cdb[7]);
		break;
	case BMIC_SENSE_DIAG_OPTIONS:
		cdb_length = 0;
		fallthrough;
	case BMIC_IDENTIFY_CONTROLLER:
	case BMIC_IDENTIFY_PHYSICAL_DEVICE:
	case BMIC_SENSE_SUBSYSTEM_INFORMATION:
	case BMIC_SENSE_FEATURE:
		request->data_direction = SOP_READ_FLAG;
		cdb[0] = BMIC_READ;
		cdb[6] = cmd;
		put_unaligned_be16(cdb_length, &cdb[7]);
		break;
	case BMIC_SET_DIAG_OPTIONS:
		cdb_length = 0;
		fallthrough;
	case BMIC_WRITE_HOST_WELLNESS:
		request->data_direction = SOP_WRITE_FLAG;
		cdb[0] = BMIC_WRITE;
		cdb[6] = cmd;
		put_unaligned_be16(cdb_length, &cdb[7]);
		break;
	case BMIC_CSMI_PASSTHRU:
		request->data_direction = SOP_BIDIRECTIONAL;
		cdb[0] = BMIC_WRITE;
		cdb[5] = CSMI_CC_SAS_SMP_PASSTHRU;
		cdb[6] = cmd;
		put_unaligned_be16(cdb_length, &cdb[7]);
		break;
	default:
		dev_err(&ctrl_info->pci_dev->dev, "unknown command 0x%c\n", cmd);
		break;
	}

	switch (request->data_direction) {
	case SOP_READ_FLAG:
		*dir = DMA_FROM_DEVICE;
		break;
	case SOP_WRITE_FLAG:
		*dir = DMA_TO_DEVICE;
		break;
	case SOP_NO_DIRECTION_FLAG:
		*dir = DMA_NONE;
		break;
	default:
		*dir = DMA_BIDIRECTIONAL;
		break;
	}

	return pqi_map_single(ctrl_info->pci_dev, &request->sg_descriptors[0],
		buffer, buffer_length, *dir);
}

static inline void pqi_reinit_io_request(struct pqi_io_request *io_request)
{
	io_request->scmd = NULL;
	io_request->status = 0;
	io_request->error_info = NULL;
	io_request->raid_bypass = false;
}

static inline struct pqi_io_request *pqi_alloc_io_request(struct pqi_ctrl_info *ctrl_info, struct scsi_cmnd *scmd)
{
	struct pqi_io_request *io_request;
	u16 i;

	if (scmd) { /* SML I/O request */
		u32 blk_tag = blk_mq_unique_tag(scsi_cmd_to_rq(scmd));

		i = blk_mq_unique_tag_to_tag(blk_tag);
		io_request = &ctrl_info->io_request_pool[i];
		if (atomic_inc_return(&io_request->refcount) > 1) {
			atomic_dec(&io_request->refcount);
			return NULL;
		}
	} else { /* IOCTL or driver internal request */
		/*
		 * benignly racy - may have to wait for an open slot.
		 * command slot range is scsi_ml_can_queue -
		 *         [scsi_ml_can_queue + (PQI_RESERVED_IO_SLOTS - 1)]
		 */
		i = 0;
		while (1) {
			io_request = &ctrl_info->io_request_pool[ctrl_info->scsi_ml_can_queue + i];
			if (atomic_inc_return(&io_request->refcount) == 1)
				break;
			atomic_dec(&io_request->refcount);
			i = (i + 1) % PQI_RESERVED_IO_SLOTS;
		}
	}

	if (io_request)
		pqi_reinit_io_request(io_request);

	return io_request;
}

static void pqi_free_io_request(struct pqi_io_request *io_request)
{
	atomic_dec(&io_request->refcount);
}

static int pqi_send_scsi_raid_request(struct pqi_ctrl_info *ctrl_info, u8 cmd,
	u8 *scsi3addr, void *buffer, size_t buffer_length, u16 vpd_page,
	struct pqi_raid_error_info *error_info)
{
	int rc;
	struct pqi_raid_path_request request;
	enum dma_data_direction dir;

	rc = pqi_build_raid_path_request(ctrl_info, &request, cmd, scsi3addr,
		buffer, buffer_length, vpd_page, &dir);
	if (rc)
		return rc;

	rc = pqi_submit_raid_request_synchronous(ctrl_info, &request.header, 0, error_info);

	pqi_pci_unmap(ctrl_info->pci_dev, request.sg_descriptors, 1, dir);

	return rc;
}

/* helper functions for pqi_send_scsi_raid_request */

static inline int pqi_send_ctrl_raid_request(struct pqi_ctrl_info *ctrl_info,
	u8 cmd, void *buffer, size_t buffer_length)
{
	return pqi_send_scsi_raid_request(ctrl_info, cmd, RAID_CTLR_LUNID,
		buffer, buffer_length, 0, NULL);
}

static inline int pqi_send_ctrl_raid_with_error(struct pqi_ctrl_info *ctrl_info,
	u8 cmd, void *buffer, size_t buffer_length,
	struct pqi_raid_error_info *error_info)
{
	return pqi_send_scsi_raid_request(ctrl_info, cmd, RAID_CTLR_LUNID,
		buffer, buffer_length, 0, error_info);
}

static inline int pqi_identify_controller(struct pqi_ctrl_info *ctrl_info,
	struct bmic_identify_controller *buffer)
{
	return pqi_send_ctrl_raid_request(ctrl_info, BMIC_IDENTIFY_CONTROLLER,
		buffer, sizeof(*buffer));
}

static inline int pqi_sense_subsystem_info(struct  pqi_ctrl_info *ctrl_info,
	struct bmic_sense_subsystem_info *sense_info)
{
	return pqi_send_ctrl_raid_request(ctrl_info,
		BMIC_SENSE_SUBSYSTEM_INFORMATION, sense_info,
		sizeof(*sense_info));
}

static inline int pqi_scsi_inquiry(struct pqi_ctrl_info *ctrl_info,
	u8 *scsi3addr, u16 vpd_page, void *buffer, size_t buffer_length)
{
	return pqi_send_scsi_raid_request(ctrl_info, INQUIRY, scsi3addr,
		buffer, buffer_length, vpd_page, NULL);
}

static int pqi_identify_physical_device(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device,
	struct bmic_identify_physical_device *buffer, size_t buffer_length)
{
	int rc;
	enum dma_data_direction dir;
	u16 bmic_device_index;
	struct pqi_raid_path_request request;

	rc = pqi_build_raid_path_request(ctrl_info, &request,
		BMIC_IDENTIFY_PHYSICAL_DEVICE, RAID_CTLR_LUNID, buffer,
		buffer_length, 0, &dir);
	if (rc)
		return rc;

	bmic_device_index = CISS_GET_DRIVE_NUMBER(device->scsi3addr);
	request.cdb[2] = (u8)bmic_device_index;
	request.cdb[9] = (u8)(bmic_device_index >> 8);

	rc = pqi_submit_raid_request_synchronous(ctrl_info, &request.header, 0, NULL);

	pqi_pci_unmap(ctrl_info->pci_dev, request.sg_descriptors, 1, dir);

	return rc;
}

static inline u32 pqi_aio_limit_to_bytes(__le16 *limit)
{
	u32 bytes;

	bytes = get_unaligned_le16(limit);
	if (bytes == 0)
		bytes = ~0;
	else
		bytes *= 1024;

	return bytes;
}

#pragma pack(1)

struct bmic_sense_feature_buffer {
	struct bmic_sense_feature_buffer_header header;
	struct bmic_sense_feature_io_page_aio_subpage aio_subpage;
};

#pragma pack()

#define MINIMUM_AIO_SUBPAGE_BUFFER_LENGTH	\
	offsetofend(struct bmic_sense_feature_buffer, \
		aio_subpage.max_write_raid_1_10_3drive)

#define MINIMUM_AIO_SUBPAGE_LENGTH	\
	(offsetofend(struct bmic_sense_feature_io_page_aio_subpage, \
		max_write_raid_1_10_3drive) - \
		sizeof_field(struct bmic_sense_feature_io_page_aio_subpage, header))

static int pqi_get_advanced_raid_bypass_config(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	enum dma_data_direction dir;
	struct pqi_raid_path_request request;
	struct bmic_sense_feature_buffer *buffer;

	buffer = kmalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	rc = pqi_build_raid_path_request(ctrl_info, &request, BMIC_SENSE_FEATURE, RAID_CTLR_LUNID,
		buffer, sizeof(*buffer), 0, &dir);
	if (rc)
		goto error;

	request.cdb[2] = BMIC_SENSE_FEATURE_IO_PAGE;
	request.cdb[3] = BMIC_SENSE_FEATURE_IO_PAGE_AIO_SUBPAGE;

	rc = pqi_submit_raid_request_synchronous(ctrl_info, &request.header, 0, NULL);

	pqi_pci_unmap(ctrl_info->pci_dev, request.sg_descriptors, 1, dir);

	if (rc)
		goto error;

	if (buffer->header.page_code != BMIC_SENSE_FEATURE_IO_PAGE ||
		buffer->header.subpage_code !=
			BMIC_SENSE_FEATURE_IO_PAGE_AIO_SUBPAGE ||
		get_unaligned_le16(&buffer->header.buffer_length) <
			MINIMUM_AIO_SUBPAGE_BUFFER_LENGTH ||
		buffer->aio_subpage.header.page_code !=
			BMIC_SENSE_FEATURE_IO_PAGE ||
		buffer->aio_subpage.header.subpage_code !=
			BMIC_SENSE_FEATURE_IO_PAGE_AIO_SUBPAGE ||
		get_unaligned_le16(&buffer->aio_subpage.header.page_length) <
			MINIMUM_AIO_SUBPAGE_LENGTH) {
		goto error;
	}

	ctrl_info->max_transfer_encrypted_sas_sata =
		pqi_aio_limit_to_bytes(
			&buffer->aio_subpage.max_transfer_encrypted_sas_sata);

	ctrl_info->max_transfer_encrypted_nvme =
		pqi_aio_limit_to_bytes(
			&buffer->aio_subpage.max_transfer_encrypted_nvme);

	ctrl_info->max_write_raid_5_6 =
		pqi_aio_limit_to_bytes(
			&buffer->aio_subpage.max_write_raid_5_6);

	ctrl_info->max_write_raid_1_10_2drive =
		pqi_aio_limit_to_bytes(
			&buffer->aio_subpage.max_write_raid_1_10_2drive);

	ctrl_info->max_write_raid_1_10_3drive =
		pqi_aio_limit_to_bytes(
			&buffer->aio_subpage.max_write_raid_1_10_3drive);

error:
	kfree(buffer);

	return rc;
}

static int pqi_flush_cache(struct pqi_ctrl_info *ctrl_info,
	enum bmic_flush_cache_shutdown_event shutdown_event)
{
	int rc;
	struct bmic_flush_cache *flush_cache;

	flush_cache = kzalloc(sizeof(*flush_cache), GFP_KERNEL);
	if (!flush_cache)
		return -ENOMEM;

	flush_cache->shutdown_event = shutdown_event;

	rc = pqi_send_ctrl_raid_request(ctrl_info, SA_FLUSH_CACHE, flush_cache,
		sizeof(*flush_cache));

	kfree(flush_cache);

	return rc;
}

int pqi_csmi_smp_passthru(struct pqi_ctrl_info *ctrl_info,
	struct bmic_csmi_smp_passthru_buffer *buffer, size_t buffer_length,
	struct pqi_raid_error_info *error_info)
{
	return pqi_send_ctrl_raid_with_error(ctrl_info, BMIC_CSMI_PASSTHRU,
		buffer, buffer_length, error_info);
}

#define PQI_FETCH_PTRAID_DATA		(1 << 31)

static int pqi_set_diag_rescan(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	struct bmic_diag_options *diag;

	diag = kzalloc(sizeof(*diag), GFP_KERNEL);
	if (!diag)
		return -ENOMEM;

	rc = pqi_send_ctrl_raid_request(ctrl_info, BMIC_SENSE_DIAG_OPTIONS,
		diag, sizeof(*diag));
	if (rc)
		goto out;

	diag->options |= cpu_to_le32(PQI_FETCH_PTRAID_DATA);

	rc = pqi_send_ctrl_raid_request(ctrl_info, BMIC_SET_DIAG_OPTIONS, diag,
		sizeof(*diag));

out:
	kfree(diag);

	return rc;
}

static inline int pqi_write_host_wellness(struct pqi_ctrl_info *ctrl_info,
	void *buffer, size_t buffer_length)
{
	return pqi_send_ctrl_raid_request(ctrl_info, BMIC_WRITE_HOST_WELLNESS,
		buffer, buffer_length);
}

#pragma pack(1)

struct bmic_host_wellness_driver_version {
	u8	start_tag[4];
	u8	driver_version_tag[2];
	__le16	driver_version_length;
	char	driver_version[32];
	u8	dont_write_tag[2];
	u8	end_tag[2];
};

#pragma pack()

static int pqi_write_driver_version_to_host_wellness(
	struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	struct bmic_host_wellness_driver_version *buffer;
	size_t buffer_length;

	buffer_length = sizeof(*buffer);

	buffer = kmalloc(buffer_length, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	buffer->start_tag[0] = '<';
	buffer->start_tag[1] = 'H';
	buffer->start_tag[2] = 'W';
	buffer->start_tag[3] = '>';
	buffer->driver_version_tag[0] = 'D';
	buffer->driver_version_tag[1] = 'V';
	put_unaligned_le16(sizeof(buffer->driver_version),
		&buffer->driver_version_length);
	strscpy(buffer->driver_version, "Linux " DRIVER_VERSION,
		sizeof(buffer->driver_version));
	buffer->dont_write_tag[0] = 'D';
	buffer->dont_write_tag[1] = 'W';
	buffer->end_tag[0] = 'Z';
	buffer->end_tag[1] = 'Z';

	rc = pqi_write_host_wellness(ctrl_info, buffer, buffer_length);

	kfree(buffer);

	return rc;
}

#pragma pack(1)

struct bmic_host_wellness_time {
	u8	start_tag[4];
	u8	time_tag[2];
	__le16	time_length;
	u8	time[8];
	u8	dont_write_tag[2];
	u8	end_tag[2];
};

#pragma pack()

static int pqi_write_current_time_to_host_wellness(
	struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	struct bmic_host_wellness_time *buffer;
	size_t buffer_length;
	time64_t local_time;
	unsigned int year;
	struct tm tm;

	buffer_length = sizeof(*buffer);

	buffer = kmalloc(buffer_length, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	buffer->start_tag[0] = '<';
	buffer->start_tag[1] = 'H';
	buffer->start_tag[2] = 'W';
	buffer->start_tag[3] = '>';
	buffer->time_tag[0] = 'T';
	buffer->time_tag[1] = 'D';
	put_unaligned_le16(sizeof(buffer->time),
		&buffer->time_length);

	local_time = ktime_get_real_seconds();
	time64_to_tm(local_time, -sys_tz.tz_minuteswest * 60, &tm);
	year = tm.tm_year + 1900;

	buffer->time[0] = bin2bcd(tm.tm_hour);
	buffer->time[1] = bin2bcd(tm.tm_min);
	buffer->time[2] = bin2bcd(tm.tm_sec);
	buffer->time[3] = 0;
	buffer->time[4] = bin2bcd(tm.tm_mon + 1);
	buffer->time[5] = bin2bcd(tm.tm_mday);
	buffer->time[6] = bin2bcd(year / 100);
	buffer->time[7] = bin2bcd(year % 100);

	buffer->dont_write_tag[0] = 'D';
	buffer->dont_write_tag[1] = 'W';
	buffer->end_tag[0] = 'Z';
	buffer->end_tag[1] = 'Z';

	rc = pqi_write_host_wellness(ctrl_info, buffer, buffer_length);

	kfree(buffer);

	return rc;
}

#define PQI_UPDATE_TIME_WORK_INTERVAL	(24UL * 60 * 60 * HZ)

static void pqi_update_time_worker(struct work_struct *work)
{
	int rc;
	struct pqi_ctrl_info *ctrl_info;

	ctrl_info = container_of(to_delayed_work(work), struct pqi_ctrl_info,
		update_time_work);

	rc = pqi_write_current_time_to_host_wellness(ctrl_info);
	if (rc)
		dev_warn(&ctrl_info->pci_dev->dev,
			"error updating time on controller\n");

	schedule_delayed_work(&ctrl_info->update_time_work,
		PQI_UPDATE_TIME_WORK_INTERVAL);
}

static inline void pqi_schedule_update_time_worker(struct pqi_ctrl_info *ctrl_info)
{
	schedule_delayed_work(&ctrl_info->update_time_work, 0);
}

static inline void pqi_cancel_update_time_worker(struct pqi_ctrl_info *ctrl_info)
{
	cancel_delayed_work_sync(&ctrl_info->update_time_work);
}

static inline int pqi_report_luns(struct pqi_ctrl_info *ctrl_info, u8 cmd, void *buffer,
	size_t buffer_length)
{
	return pqi_send_ctrl_raid_request(ctrl_info, cmd, buffer, buffer_length);
}

static int pqi_report_phys_logical_luns(struct pqi_ctrl_info *ctrl_info, u8 cmd, void **buffer)
{
	int rc;
	size_t lun_list_length;
	size_t lun_data_length;
	size_t new_lun_list_length;
	void *lun_data = NULL;
	struct report_lun_header *report_lun_header;

	report_lun_header = kmalloc(sizeof(*report_lun_header), GFP_KERNEL);
	if (!report_lun_header) {
		rc = -ENOMEM;
		goto out;
	}

	rc = pqi_report_luns(ctrl_info, cmd, report_lun_header, sizeof(*report_lun_header));
	if (rc)
		goto out;

	lun_list_length = get_unaligned_be32(&report_lun_header->list_length);

again:
	lun_data_length = sizeof(struct report_lun_header) + lun_list_length;

	lun_data = kmalloc(lun_data_length, GFP_KERNEL);
	if (!lun_data) {
		rc = -ENOMEM;
		goto out;
	}

	if (lun_list_length == 0) {
		memcpy(lun_data, report_lun_header, sizeof(*report_lun_header));
		goto out;
	}

	rc = pqi_report_luns(ctrl_info, cmd, lun_data, lun_data_length);
	if (rc)
		goto out;

	new_lun_list_length =
		get_unaligned_be32(&((struct report_lun_header *)lun_data)->list_length);

	if (new_lun_list_length > lun_list_length) {
		lun_list_length = new_lun_list_length;
		kfree(lun_data);
		goto again;
	}

out:
	kfree(report_lun_header);

	if (rc) {
		kfree(lun_data);
		lun_data = NULL;
	}

	*buffer = lun_data;

	return rc;
}

static inline int pqi_report_phys_luns(struct pqi_ctrl_info *ctrl_info, void **buffer)
{
	int rc;
	unsigned int i;
	u8 rpl_response_format;
	u32 num_physicals;
	void *rpl_list;
	struct report_lun_header *rpl_header;
	struct report_phys_lun_8byte_wwid_list *rpl_8byte_wwid_list;
	struct report_phys_lun_16byte_wwid_list *rpl_16byte_wwid_list;

	rc = pqi_report_phys_logical_luns(ctrl_info, CISS_REPORT_PHYS, &rpl_list);
	if (rc)
		return rc;

	if (ctrl_info->rpl_extended_format_4_5_supported) {
		rpl_header = rpl_list;
		rpl_response_format = rpl_header->flags & CISS_REPORT_PHYS_FLAG_EXTENDED_FORMAT_MASK;
		if (rpl_response_format == CISS_REPORT_PHYS_FLAG_EXTENDED_FORMAT_4) {
			*buffer = rpl_list;
			return 0;
		} else if (rpl_response_format != CISS_REPORT_PHYS_FLAG_EXTENDED_FORMAT_2) {
			dev_err(&ctrl_info->pci_dev->dev,
				"RPL returned unsupported data format %u\n",
				rpl_response_format);
			return -EINVAL;
		} else {
			dev_warn(&ctrl_info->pci_dev->dev,
				"RPL returned extended format 2 instead of 4\n");
		}
	}

	rpl_8byte_wwid_list = rpl_list;
	num_physicals = get_unaligned_be32(&rpl_8byte_wwid_list->header.list_length) / sizeof(rpl_8byte_wwid_list->lun_entries[0]);

	rpl_16byte_wwid_list = kmalloc(struct_size(rpl_16byte_wwid_list, lun_entries,
						   num_physicals), GFP_KERNEL);
	if (!rpl_16byte_wwid_list)
		return -ENOMEM;

	put_unaligned_be32(num_physicals * sizeof(struct report_phys_lun_16byte_wwid),
		&rpl_16byte_wwid_list->header.list_length);
	rpl_16byte_wwid_list->header.flags = rpl_8byte_wwid_list->header.flags;

	for (i = 0; i < num_physicals; i++) {
		memcpy(&rpl_16byte_wwid_list->lun_entries[i].lunid, &rpl_8byte_wwid_list->lun_entries[i].lunid, sizeof(rpl_8byte_wwid_list->lun_entries[i].lunid));
		memcpy(&rpl_16byte_wwid_list->lun_entries[i].wwid[0], &rpl_8byte_wwid_list->lun_entries[i].wwid, sizeof(rpl_8byte_wwid_list->lun_entries[i].wwid));
		memset(&rpl_16byte_wwid_list->lun_entries[i].wwid[8], 0, 8);
		rpl_16byte_wwid_list->lun_entries[i].device_type = rpl_8byte_wwid_list->lun_entries[i].device_type;
		rpl_16byte_wwid_list->lun_entries[i].device_flags = rpl_8byte_wwid_list->lun_entries[i].device_flags;
		rpl_16byte_wwid_list->lun_entries[i].lun_count = rpl_8byte_wwid_list->lun_entries[i].lun_count;
		rpl_16byte_wwid_list->lun_entries[i].redundant_paths = rpl_8byte_wwid_list->lun_entries[i].redundant_paths;
		rpl_16byte_wwid_list->lun_entries[i].aio_handle = rpl_8byte_wwid_list->lun_entries[i].aio_handle;
	}

	kfree(rpl_8byte_wwid_list);
	*buffer = rpl_16byte_wwid_list;

	return 0;
}

static inline int pqi_report_logical_luns(struct pqi_ctrl_info *ctrl_info, void **buffer)
{
	return pqi_report_phys_logical_luns(ctrl_info, CISS_REPORT_LOG, buffer);
}

static int pqi_get_device_lists(struct pqi_ctrl_info *ctrl_info,
	struct report_phys_lun_16byte_wwid_list **physdev_list,
	struct report_log_lun_list **logdev_list)
{
	int rc;
	size_t logdev_list_length;
	size_t logdev_data_length;
	struct report_log_lun_list *internal_logdev_list;
	struct report_log_lun_list *logdev_data;
	struct report_lun_header report_lun_header;

	rc = pqi_report_phys_luns(ctrl_info, (void **)physdev_list);
	if (rc)
		dev_err(&ctrl_info->pci_dev->dev,
			"report physical LUNs failed\n");

	rc = pqi_report_logical_luns(ctrl_info, (void **)logdev_list);
	if (rc)
		dev_err(&ctrl_info->pci_dev->dev,
			"report logical LUNs failed\n");

	/*
	 * Tack the controller itself onto the end of the logical device list
	 * by adding a list entry that is all zeros.
	 */

	logdev_data = *logdev_list;

	if (logdev_data) {
		logdev_list_length =
			get_unaligned_be32(&logdev_data->header.list_length);
	} else {
		memset(&report_lun_header, 0, sizeof(report_lun_header));
		logdev_data =
			(struct report_log_lun_list *)&report_lun_header;
		logdev_list_length = 0;
	}

	logdev_data_length = sizeof(struct report_lun_header) +
		logdev_list_length;

	internal_logdev_list = kmalloc(logdev_data_length +
		sizeof(struct report_log_lun), GFP_KERNEL);
	if (!internal_logdev_list) {
		kfree(*logdev_list);
		*logdev_list = NULL;
		return -ENOMEM;
	}

	memcpy(internal_logdev_list, logdev_data, logdev_data_length);
	memset((u8 *)internal_logdev_list + logdev_data_length, 0,
		sizeof(struct report_log_lun));
	put_unaligned_be32(logdev_list_length +
		sizeof(struct report_log_lun),
		&internal_logdev_list->header.list_length);

	kfree(*logdev_list);
	*logdev_list = internal_logdev_list;

	return 0;
}

static inline void pqi_set_bus_target_lun(struct pqi_scsi_dev *device,
	int bus, int target, int lun)
{
	device->bus = bus;
	device->target = target;
	device->lun = lun;
}

static void pqi_assign_bus_target_lun(struct pqi_scsi_dev *device)
{
	u8 *scsi3addr;
	u32 lunid;
	int bus;
	int target;
	int lun;

	scsi3addr = device->scsi3addr;
	lunid = get_unaligned_le32(scsi3addr);

	if (pqi_is_hba_lunid(scsi3addr)) {
		/* The specified device is the controller. */
		pqi_set_bus_target_lun(device, PQI_HBA_BUS, 0, lunid & 0x3fff);
		device->target_lun_valid = true;
		return;
	}

	if (pqi_is_logical_device(device)) {
		if (device->is_external_raid_device) {
			bus = PQI_EXTERNAL_RAID_VOLUME_BUS;
			target = (lunid >> 16) & 0x3fff;
			lun = lunid & 0xff;
		} else {
			bus = PQI_RAID_VOLUME_BUS;
			target = 0;
			lun = lunid & 0x3fff;
		}
		pqi_set_bus_target_lun(device, bus, target, lun);
		device->target_lun_valid = true;
		return;
	}

	/*
	 * Defer target and LUN assignment for non-controller physical devices
	 * because the SAS transport layer will make these assignments later.
	 */
	pqi_set_bus_target_lun(device, PQI_PHYSICAL_DEVICE_BUS, 0, 0);
}

static void pqi_get_raid_level(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device)
{
	int rc;
	u8 raid_level;
	u8 *buffer;

	raid_level = SA_RAID_UNKNOWN;

	buffer = kmalloc(64, GFP_KERNEL);
	if (buffer) {
		rc = pqi_scsi_inquiry(ctrl_info, device->scsi3addr,
			VPD_PAGE | CISS_VPD_LV_DEVICE_GEOMETRY, buffer, 64);
		if (rc == 0) {
			raid_level = buffer[8];
			if (raid_level > SA_RAID_MAX)
				raid_level = SA_RAID_UNKNOWN;
		}
		kfree(buffer);
	}

	device->raid_level = raid_level;
}

static int pqi_validate_raid_map(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device, struct raid_map *raid_map)
{
	char *err_msg;
	u32 raid_map_size;
	u32 r5or6_blocks_per_row;

	raid_map_size = get_unaligned_le32(&raid_map->structure_size);

	if (raid_map_size < offsetof(struct raid_map, disk_data)) {
		err_msg = "RAID map too small";
		goto bad_raid_map;
	}

	if (device->raid_level == SA_RAID_1) {
		if (get_unaligned_le16(&raid_map->layout_map_count) != 2) {
			err_msg = "invalid RAID-1 map";
			goto bad_raid_map;
		}
	} else if (device->raid_level == SA_RAID_TRIPLE) {
		if (get_unaligned_le16(&raid_map->layout_map_count) != 3) {
			err_msg = "invalid RAID-1(Triple) map";
			goto bad_raid_map;
		}
	} else if ((device->raid_level == SA_RAID_5 ||
		device->raid_level == SA_RAID_6) &&
		get_unaligned_le16(&raid_map->layout_map_count) > 1) {
		/* RAID 50/60 */
		r5or6_blocks_per_row =
			get_unaligned_le16(&raid_map->strip_size) *
			get_unaligned_le16(&raid_map->data_disks_per_row);
		if (r5or6_blocks_per_row == 0) {
			err_msg = "invalid RAID-5 or RAID-6 map";
			goto bad_raid_map;
		}
	}

	return 0;

bad_raid_map:
	dev_warn(&ctrl_info->pci_dev->dev,
		"logical device %08x%08x %s\n",
		*((u32 *)&device->scsi3addr),
		*((u32 *)&device->scsi3addr[4]), err_msg);

	return -EINVAL;
}

static int pqi_get_raid_map(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device)
{
	int rc;
	u32 raid_map_size;
	struct raid_map *raid_map;

	raid_map = kmalloc(sizeof(*raid_map), GFP_KERNEL);
	if (!raid_map)
		return -ENOMEM;

	rc = pqi_send_scsi_raid_request(ctrl_info, CISS_GET_RAID_MAP,
		device->scsi3addr, raid_map, sizeof(*raid_map), 0, NULL);
	if (rc)
		goto error;

	raid_map_size = get_unaligned_le32(&raid_map->structure_size);

	if (raid_map_size > sizeof(*raid_map)) {

		kfree(raid_map);

		raid_map = kmalloc(raid_map_size, GFP_KERNEL);
		if (!raid_map)
			return -ENOMEM;

		rc = pqi_send_scsi_raid_request(ctrl_info, CISS_GET_RAID_MAP,
			device->scsi3addr, raid_map, raid_map_size, 0, NULL);
		if (rc)
			goto error;

		if (get_unaligned_le32(&raid_map->structure_size)
			!= raid_map_size) {
			dev_warn(&ctrl_info->pci_dev->dev,
				"requested %u bytes, received %u bytes\n",
				raid_map_size,
				get_unaligned_le32(&raid_map->structure_size));
			rc = -EINVAL;
			goto error;
		}
	}

	rc = pqi_validate_raid_map(ctrl_info, device, raid_map);
	if (rc)
		goto error;

	device->raid_io_stats = alloc_percpu(struct pqi_raid_io_stats);
	if (!device->raid_io_stats) {
		rc = -ENOMEM;
		goto error;
	}

	device->raid_map = raid_map;

	return 0;

error:
	kfree(raid_map);

	return rc;
}

static void pqi_set_max_transfer_encrypted(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device)
{
	if (!ctrl_info->lv_drive_type_mix_valid) {
		device->max_transfer_encrypted = ~0;
		return;
	}

	switch (LV_GET_DRIVE_TYPE_MIX(device->scsi3addr)) {
	case LV_DRIVE_TYPE_MIX_SAS_HDD_ONLY:
	case LV_DRIVE_TYPE_MIX_SATA_HDD_ONLY:
	case LV_DRIVE_TYPE_MIX_SAS_OR_SATA_SSD_ONLY:
	case LV_DRIVE_TYPE_MIX_SAS_SSD_ONLY:
	case LV_DRIVE_TYPE_MIX_SATA_SSD_ONLY:
	case LV_DRIVE_TYPE_MIX_SAS_ONLY:
	case LV_DRIVE_TYPE_MIX_SATA_ONLY:
		device->max_transfer_encrypted =
			ctrl_info->max_transfer_encrypted_sas_sata;
		break;
	case LV_DRIVE_TYPE_MIX_NVME_ONLY:
		device->max_transfer_encrypted =
			ctrl_info->max_transfer_encrypted_nvme;
		break;
	case LV_DRIVE_TYPE_MIX_UNKNOWN:
	case LV_DRIVE_TYPE_MIX_NO_RESTRICTION:
	default:
		device->max_transfer_encrypted =
			min(ctrl_info->max_transfer_encrypted_sas_sata,
				ctrl_info->max_transfer_encrypted_nvme);
		break;
	}
}

static void pqi_get_raid_bypass_status(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device)
{
	int rc;
	u8 *buffer;
	u8 bypass_status;

	buffer = kmalloc(64, GFP_KERNEL);
	if (!buffer)
		return;

	rc = pqi_scsi_inquiry(ctrl_info, device->scsi3addr,
		VPD_PAGE | CISS_VPD_LV_BYPASS_STATUS, buffer, 64);
	if (rc)
		goto out;

#define RAID_BYPASS_STATUS		4
#define RAID_BYPASS_CONFIGURED		0x1
#define RAID_BYPASS_ENABLED		0x2

	bypass_status = buffer[RAID_BYPASS_STATUS];
	device->raid_bypass_configured =
		(bypass_status & RAID_BYPASS_CONFIGURED) != 0;
	if (device->raid_bypass_configured &&
		(bypass_status & RAID_BYPASS_ENABLED) &&
		pqi_get_raid_map(ctrl_info, device) == 0) {
		device->raid_bypass_enabled = true;
		if (get_unaligned_le16(&device->raid_map->flags) &
			RAID_MAP_ENCRYPTION_ENABLED)
			pqi_set_max_transfer_encrypted(ctrl_info, device);
	}

out:
	kfree(buffer);
}

/*
 * Use vendor-specific VPD to determine online/offline status of a volume.
 */

static void pqi_get_volume_status(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device)
{
	int rc;
	size_t page_length;
	u8 volume_status = CISS_LV_STATUS_UNAVAILABLE;
	bool volume_offline = true;
	u32 volume_flags;
	struct ciss_vpd_logical_volume_status *vpd;

	vpd = kmalloc(sizeof(*vpd), GFP_KERNEL);
	if (!vpd)
		goto no_buffer;

	rc = pqi_scsi_inquiry(ctrl_info, device->scsi3addr,
		VPD_PAGE | CISS_VPD_LV_STATUS, vpd, sizeof(*vpd));
	if (rc)
		goto out;

	if (vpd->page_code != CISS_VPD_LV_STATUS)
		goto out;

	page_length = offsetof(struct ciss_vpd_logical_volume_status,
		volume_status) + vpd->page_length;
	if (page_length < sizeof(*vpd))
		goto out;

	volume_status = vpd->volume_status;
	volume_flags = get_unaligned_be32(&vpd->flags);
	volume_offline = (volume_flags & CISS_LV_FLAGS_NO_HOST_IO) != 0;

out:
	kfree(vpd);
no_buffer:
	device->volume_status = volume_status;
	device->volume_offline = volume_offline;
}

#define PQI_DEVICE_NCQ_PRIO_SUPPORTED	0x01
#define PQI_DEVICE_PHY_MAP_SUPPORTED	0x10
#define PQI_DEVICE_ERASE_IN_PROGRESS	0x10

static int pqi_get_physical_device_info(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device,
	struct bmic_identify_physical_device *id_phys)
{
	int rc;

	memset(id_phys, 0, sizeof(*id_phys));

	rc = pqi_identify_physical_device(ctrl_info, device,
		id_phys, sizeof(*id_phys));
	if (rc) {
		device->queue_depth = PQI_PHYSICAL_DISK_DEFAULT_MAX_QUEUE_DEPTH;
		return rc;
	}

	scsi_sanitize_inquiry_string(&id_phys->model[0], 8);
	scsi_sanitize_inquiry_string(&id_phys->model[8], 16);

	memcpy(device->vendor, &id_phys->model[0], sizeof(device->vendor));
	memcpy(device->model, &id_phys->model[8], sizeof(device->model));

	device->box_index = id_phys->box_index;
	device->phys_box_on_bus = id_phys->phys_box_on_bus;
	device->phy_connected_dev_type = id_phys->phy_connected_dev_type[0];
	device->queue_depth =
		get_unaligned_le16(&id_phys->current_queue_depth_limit);
	device->active_path_index = id_phys->active_path_number;
	device->path_map = id_phys->redundant_path_present_map;
	memcpy(&device->box,
		&id_phys->alternate_paths_phys_box_on_port,
		sizeof(device->box));
	memcpy(&device->phys_connector,
		&id_phys->alternate_paths_phys_connector,
		sizeof(device->phys_connector));
	device->bay = id_phys->phys_bay_in_box;
	device->lun_count = id_phys->multi_lun_device_lun_count;
	if ((id_phys->even_more_flags & PQI_DEVICE_PHY_MAP_SUPPORTED) &&
		id_phys->phy_count)
		device->phy_id =
			id_phys->phy_to_phy_map[device->active_path_index];
	else
		device->phy_id = 0xFF;

	device->ncq_prio_support =
		((get_unaligned_le32(&id_phys->misc_drive_flags) >> 16) &
		PQI_DEVICE_NCQ_PRIO_SUPPORTED);

	device->erase_in_progress = !!(get_unaligned_le16(&id_phys->extra_physical_drive_flags) & PQI_DEVICE_ERASE_IN_PROGRESS);

	return 0;
}

static int pqi_get_logical_device_info(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device)
{
	int rc;
	u8 *buffer;

	buffer = kmalloc(64, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	/* Send an inquiry to the device to see what it is. */
	rc = pqi_scsi_inquiry(ctrl_info, device->scsi3addr, 0, buffer, 64);
	if (rc)
		goto out;

	scsi_sanitize_inquiry_string(&buffer[8], 8);
	scsi_sanitize_inquiry_string(&buffer[16], 16);

	device->devtype = buffer[0] & 0x1f;
	memcpy(device->vendor, &buffer[8], sizeof(device->vendor));
	memcpy(device->model, &buffer[16], sizeof(device->model));

	if (device->devtype == TYPE_DISK) {
		if (device->is_external_raid_device) {
			device->raid_level = SA_RAID_UNKNOWN;
			device->volume_status = CISS_LV_OK;
			device->volume_offline = false;
		} else {
			pqi_get_raid_level(ctrl_info, device);
			pqi_get_raid_bypass_status(ctrl_info, device);
			pqi_get_volume_status(ctrl_info, device);
		}
	}

out:
	kfree(buffer);

	return rc;
}

/*
 * Prevent adding drive to OS for some corner cases such as a drive
 * undergoing a sanitize (erase) operation. Some OSes will continue to poll
 * the drive until the sanitize completes, which can take hours,
 * resulting in long bootup delays. Commands such as TUR, READ_CAP
 * are allowed, but READ/WRITE cause check condition. So the OS
 * cannot check/read the partition table.
 * Note: devices that have completed sanitize must be re-enabled
 *       using the management utility.
 */
static inline bool pqi_keep_device_offline(struct pqi_scsi_dev *device)
{
	return device->erase_in_progress;
}

static int pqi_get_device_info_phys_logical(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device,
	struct bmic_identify_physical_device *id_phys)
{
	int rc;

	if (device->is_expander_smp_device)
		return 0;

	if (pqi_is_logical_device(device))
		rc = pqi_get_logical_device_info(ctrl_info, device);
	else
		rc = pqi_get_physical_device_info(ctrl_info, device, id_phys);

	return rc;
}

static int pqi_get_device_info(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device,
	struct bmic_identify_physical_device *id_phys)
{
	int rc;

	rc = pqi_get_device_info_phys_logical(ctrl_info, device, id_phys);

	if (rc == 0 && device->lun_count == 0)
		device->lun_count = 1;

	return rc;
}

static void pqi_show_volume_status(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device)
{
	char *status;
	static const char unknown_state_str[] =
		"Volume is in an unknown state (%u)";
	char unknown_state_buffer[sizeof(unknown_state_str) + 10];

	switch (device->volume_status) {
	case CISS_LV_OK:
		status = "Volume online";
		break;
	case CISS_LV_FAILED:
		status = "Volume failed";
		break;
	case CISS_LV_NOT_CONFIGURED:
		status = "Volume not configured";
		break;
	case CISS_LV_DEGRADED:
		status = "Volume degraded";
		break;
	case CISS_LV_READY_FOR_RECOVERY:
		status = "Volume ready for recovery operation";
		break;
	case CISS_LV_UNDERGOING_RECOVERY:
		status = "Volume undergoing recovery";
		break;
	case CISS_LV_WRONG_PHYSICAL_DRIVE_REPLACED:
		status = "Wrong physical drive was replaced";
		break;
	case CISS_LV_PHYSICAL_DRIVE_CONNECTION_PROBLEM:
		status = "A physical drive not properly connected";
		break;
	case CISS_LV_HARDWARE_OVERHEATING:
		status = "Hardware is overheating";
		break;
	case CISS_LV_HARDWARE_HAS_OVERHEATED:
		status = "Hardware has overheated";
		break;
	case CISS_LV_UNDERGOING_EXPANSION:
		status = "Volume undergoing expansion";
		break;
	case CISS_LV_NOT_AVAILABLE:
		status = "Volume waiting for transforming volume";
		break;
	case CISS_LV_QUEUED_FOR_EXPANSION:
		status = "Volume queued for expansion";
		break;
	case CISS_LV_DISABLED_SCSI_ID_CONFLICT:
		status = "Volume disabled due to SCSI ID conflict";
		break;
	case CISS_LV_EJECTED:
		status = "Volume has been ejected";
		break;
	case CISS_LV_UNDERGOING_ERASE:
		status = "Volume undergoing background erase";
		break;
	case CISS_LV_READY_FOR_PREDICTIVE_SPARE_REBUILD:
		status = "Volume ready for predictive spare rebuild";
		break;
	case CISS_LV_UNDERGOING_RPI:
		status = "Volume undergoing rapid parity initialization";
		break;
	case CISS_LV_PENDING_RPI:
		status = "Volume queued for rapid parity initialization";
		break;
	case CISS_LV_ENCRYPTED_NO_KEY:
		status = "Encrypted volume inaccessible - key not present";
		break;
	case CISS_LV_UNDERGOING_ENCRYPTION:
		status = "Volume undergoing encryption process";
		break;
	case CISS_LV_UNDERGOING_ENCRYPTION_REKEYING:
		status = "Volume undergoing encryption re-keying process";
		break;
	case CISS_LV_ENCRYPTED_IN_NON_ENCRYPTED_CONTROLLER:
		status = "Volume encrypted but encryption is disabled";
		break;
	case CISS_LV_PENDING_ENCRYPTION:
		status = "Volume pending migration to encrypted state";
		break;
	case CISS_LV_PENDING_ENCRYPTION_REKEYING:
		status = "Volume pending encryption rekeying";
		break;
	case CISS_LV_NOT_SUPPORTED:
		status = "Volume not supported on this controller";
		break;
	case CISS_LV_STATUS_UNAVAILABLE:
		status = "Volume status not available";
		break;
	default:
		snprintf(unknown_state_buffer, sizeof(unknown_state_buffer),
			unknown_state_str, device->volume_status);
		status = unknown_state_buffer;
		break;
	}

	dev_info(&ctrl_info->pci_dev->dev,
		"scsi %d:%d:%d:%d %s\n",
		ctrl_info->scsi_host->host_no,
		device->bus, device->target, device->lun, status);
}

static void pqi_rescan_worker(struct work_struct *work)
{
	struct pqi_ctrl_info *ctrl_info;

	ctrl_info = container_of(to_delayed_work(work), struct pqi_ctrl_info,
		rescan_work);

	pqi_scan_scsi_devices(ctrl_info);
}

static int pqi_add_device(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device)
{
	int rc;

	if (pqi_is_logical_device(device))
		rc = scsi_add_device(ctrl_info->scsi_host, device->bus,
			device->target, device->lun);
	else
		rc = pqi_add_sas_device(ctrl_info->sas_host, device);

	return rc;
}

#define PQI_REMOVE_DEVICE_PENDING_IO_TIMEOUT_MSECS	(20 * 1000)

static inline void pqi_remove_device(struct pqi_ctrl_info *ctrl_info, struct pqi_scsi_dev *device)
{
	int rc;
	int lun;

	for (lun = 0; lun < device->lun_count; lun++) {
		rc = pqi_device_wait_for_pending_io(ctrl_info, device, lun,
			PQI_REMOVE_DEVICE_PENDING_IO_TIMEOUT_MSECS);
		if (rc)
			dev_err(&ctrl_info->pci_dev->dev,
				"scsi %d:%d:%d:%d removing device with %d outstanding command(s)\n",
				ctrl_info->scsi_host->host_no, device->bus,
				device->target, lun,
				atomic_read(&device->scsi_cmds_outstanding[lun]));
	}

	if (pqi_is_logical_device(device))
		scsi_remove_device(device->sdev);
	else
		pqi_remove_sas_device(device);

	pqi_device_remove_start(device);
}

/* Assumes the SCSI device list lock is held. */

static struct pqi_scsi_dev *pqi_find_scsi_dev(struct pqi_ctrl_info *ctrl_info,
	int bus, int target, int lun)
{
	struct pqi_scsi_dev *device;

	list_for_each_entry(device, &ctrl_info->scsi_device_list, scsi_device_list_entry)
		if (device->bus == bus && device->target == target && device->lun == lun)
			return device;

	return NULL;
}

static inline bool pqi_device_equal(struct pqi_scsi_dev *dev1, struct pqi_scsi_dev *dev2)
{
	if (dev1->is_physical_device != dev2->is_physical_device)
		return false;

	if (dev1->is_physical_device)
		return memcmp(dev1->wwid, dev2->wwid, sizeof(dev1->wwid)) == 0;

	return memcmp(dev1->volume_id, dev2->volume_id, sizeof(dev1->volume_id)) == 0;
}

enum pqi_find_result {
	DEVICE_NOT_FOUND,
	DEVICE_CHANGED,
	DEVICE_SAME,
};

static enum pqi_find_result pqi_scsi_find_entry(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device_to_find, struct pqi_scsi_dev **matching_device)
{
	struct pqi_scsi_dev *device;

	list_for_each_entry(device, &ctrl_info->scsi_device_list, scsi_device_list_entry) {
		if (pqi_scsi3addr_equal(device_to_find->scsi3addr, device->scsi3addr)) {
			*matching_device = device;
			if (pqi_device_equal(device_to_find, device)) {
				if (device_to_find->volume_offline)
					return DEVICE_CHANGED;
				return DEVICE_SAME;
			}
			return DEVICE_CHANGED;
		}
	}

	return DEVICE_NOT_FOUND;
}

static inline const char *pqi_device_type(struct pqi_scsi_dev *device)
{
	if (device->is_expander_smp_device)
		return "Enclosure SMP    ";

	return scsi_device_type(device->devtype);
}

#define PQI_DEV_INFO_BUFFER_LENGTH	128

static void pqi_dev_info(struct pqi_ctrl_info *ctrl_info,
	char *action, struct pqi_scsi_dev *device)
{
	ssize_t count;
	char buffer[PQI_DEV_INFO_BUFFER_LENGTH];

	count = scnprintf(buffer, PQI_DEV_INFO_BUFFER_LENGTH,
		"%d:%d:", ctrl_info->scsi_host->host_no, device->bus);

	if (device->target_lun_valid)
		count += scnprintf(buffer + count,
			PQI_DEV_INFO_BUFFER_LENGTH - count,
			"%d:%d",
			device->target,
			device->lun);
	else
		count += scnprintf(buffer + count,
			PQI_DEV_INFO_BUFFER_LENGTH - count,
			"-:-");

	if (pqi_is_logical_device(device))
		count += scnprintf(buffer + count,
			PQI_DEV_INFO_BUFFER_LENGTH - count,
			" %08x%08x",
			*((u32 *)&device->scsi3addr),
			*((u32 *)&device->scsi3addr[4]));
	else
		count += scnprintf(buffer + count,
			PQI_DEV_INFO_BUFFER_LENGTH - count,
			" %016llx%016llx",
			get_unaligned_be64(&device->wwid[0]),
			get_unaligned_be64(&device->wwid[8]));

	count += scnprintf(buffer + count, PQI_DEV_INFO_BUFFER_LENGTH - count,
		" %s %.8s %.16s ",
		pqi_device_type(device),
		device->vendor,
		device->model);

	if (pqi_is_logical_device(device)) {
		if (device->devtype == TYPE_DISK)
			count += scnprintf(buffer + count,
				PQI_DEV_INFO_BUFFER_LENGTH - count,
				"SSDSmartPathCap%c En%c %-12s",
				device->raid_bypass_configured ? '+' : '-',
				device->raid_bypass_enabled ? '+' : '-',
				pqi_raid_level_to_string(device->raid_level));
	} else {
		count += scnprintf(buffer + count,
			PQI_DEV_INFO_BUFFER_LENGTH - count,
			"AIO%c", device->aio_enabled ? '+' : '-');
		if (device->devtype == TYPE_DISK ||
			device->devtype == TYPE_ZBC)
			count += scnprintf(buffer + count,
				PQI_DEV_INFO_BUFFER_LENGTH - count,
				" qd=%-6d", device->queue_depth);
	}

	dev_info(&ctrl_info->pci_dev->dev, "%s %s\n", action, buffer);
}

static bool pqi_raid_maps_equal(struct raid_map *raid_map1, struct raid_map *raid_map2)
{
	u32 raid_map1_size;
	u32 raid_map2_size;

	if (raid_map1 == NULL || raid_map2 == NULL)
		return raid_map1 == raid_map2;

	raid_map1_size = get_unaligned_le32(&raid_map1->structure_size);
	raid_map2_size = get_unaligned_le32(&raid_map2->structure_size);

	if (raid_map1_size != raid_map2_size)
		return false;

	return memcmp(raid_map1, raid_map2, raid_map1_size) == 0;
}

/* Assumes the SCSI device list lock is held. */

static void pqi_scsi_update_device(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *existing_device, struct pqi_scsi_dev *new_device)
{
	existing_device->device_type = new_device->device_type;
	existing_device->bus = new_device->bus;
	if (new_device->target_lun_valid) {
		existing_device->target = new_device->target;
		existing_device->lun = new_device->lun;
		existing_device->target_lun_valid = true;
	}

	/* By definition, the scsi3addr and wwid fields are already the same. */

	existing_device->is_physical_device = new_device->is_physical_device;
	memcpy(existing_device->vendor, new_device->vendor, sizeof(existing_device->vendor));
	memcpy(existing_device->model, new_device->model, sizeof(existing_device->model));
	existing_device->sas_address = new_device->sas_address;
	existing_device->queue_depth = new_device->queue_depth;
	existing_device->device_offline = false;
	existing_device->lun_count = new_device->lun_count;

	if (pqi_is_logical_device(existing_device)) {
		existing_device->is_external_raid_device = new_device->is_external_raid_device;

		if (existing_device->devtype == TYPE_DISK) {
			existing_device->raid_level = new_device->raid_level;
			existing_device->volume_status = new_device->volume_status;
			memset(existing_device->next_bypass_group, 0, sizeof(existing_device->next_bypass_group));
			if (!pqi_raid_maps_equal(existing_device->raid_map, new_device->raid_map)) {
				kfree(existing_device->raid_map);
				existing_device->raid_map = new_device->raid_map;
				/* To prevent this from being freed later. */
				new_device->raid_map = NULL;
			}
			if (new_device->raid_bypass_enabled && existing_device->raid_io_stats == NULL) {
				existing_device->raid_io_stats = new_device->raid_io_stats;
				new_device->raid_io_stats = NULL;
			}
			existing_device->raid_bypass_configured = new_device->raid_bypass_configured;
			existing_device->raid_bypass_enabled = new_device->raid_bypass_enabled;
		}
	} else {
		existing_device->aio_enabled = new_device->aio_enabled;
		existing_device->aio_handle = new_device->aio_handle;
		existing_device->is_expander_smp_device = new_device->is_expander_smp_device;
		existing_device->active_path_index = new_device->active_path_index;
		existing_device->phy_id = new_device->phy_id;
		existing_device->path_map = new_device->path_map;
		existing_device->bay = new_device->bay;
		existing_device->box_index = new_device->box_index;
		existing_device->phys_box_on_bus = new_device->phys_box_on_bus;
		existing_device->phy_connected_dev_type = new_device->phy_connected_dev_type;
		memcpy(existing_device->box, new_device->box, sizeof(existing_device->box));
		memcpy(existing_device->phys_connector, new_device->phys_connector, sizeof(existing_device->phys_connector));
	}
}

static inline void pqi_free_device(struct pqi_scsi_dev *device)
{
	if (device) {
		free_percpu(device->raid_io_stats);
		kfree(device->raid_map);
		kfree(device);
	}
}

/*
 * Called when exposing a new device to the OS fails in order to re-adjust
 * our internal SCSI device list to match the SCSI ML's view.
 */

static inline void pqi_fixup_botched_add(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device)
{
	unsigned long flags;

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);
	list_del(&device->scsi_device_list_entry);
	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	/* Allow the device structure to be freed later. */
	device->keep_device = false;
}

static inline bool pqi_is_device_added(struct pqi_scsi_dev *device)
{
	if (device->is_expander_smp_device)
		return device->sas_port != NULL;

	return device->sdev != NULL;
}

static inline void pqi_init_device_tmf_work(struct pqi_scsi_dev *device)
{
	unsigned int lun;
	struct pqi_tmf_work *tmf_work;

	for (lun = 0, tmf_work = device->tmf_work; lun < PQI_MAX_LUNS_PER_DEVICE; lun++, tmf_work++)
		INIT_WORK(&tmf_work->work_struct, pqi_tmf_worker);
}

static inline bool pqi_volume_rescan_needed(struct pqi_scsi_dev *device)
{
	if (pqi_device_in_remove(device))
		return false;

	if (device->sdev == NULL)
		return false;

	if (!scsi_device_online(device->sdev))
		return false;

	return device->rescan;
}

static void pqi_update_device_list(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *new_device_list[], unsigned int num_new_devices)
{
	int rc;
	unsigned int i;
	unsigned long flags;
	enum pqi_find_result find_result;
	struct pqi_scsi_dev *device;
	struct pqi_scsi_dev *next;
	struct pqi_scsi_dev *matching_device;
	LIST_HEAD(add_list);
	LIST_HEAD(delete_list);

	/*
	 * The idea here is to do as little work as possible while holding the
	 * spinlock.  That's why we go to great pains to defer anything other
	 * than updating the internal device list until after we release the
	 * spinlock.
	 */

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	/* Assume that all devices in the existing list have gone away. */
	list_for_each_entry(device, &ctrl_info->scsi_device_list, scsi_device_list_entry)
		device->device_gone = true;

	for (i = 0; i < num_new_devices; i++) {
		device = new_device_list[i];

		find_result = pqi_scsi_find_entry(ctrl_info, device,
			&matching_device);

		switch (find_result) {
		case DEVICE_SAME:
			/*
			 * The newly found device is already in the existing
			 * device list.
			 */
			device->new_device = false;
			matching_device->device_gone = false;
			pqi_scsi_update_device(ctrl_info, matching_device, device);
			break;
		case DEVICE_NOT_FOUND:
			/*
			 * The newly found device is NOT in the existing device
			 * list.
			 */
			device->new_device = true;
			break;
		case DEVICE_CHANGED:
			/*
			 * The original device has gone away and we need to add
			 * the new device.
			 */
			device->new_device = true;
			break;
		}
	}

	/* Process all devices that have gone away. */
	list_for_each_entry_safe(device, next, &ctrl_info->scsi_device_list,
		scsi_device_list_entry) {
		if (device->device_gone) {
			list_del(&device->scsi_device_list_entry);
			list_add_tail(&device->delete_list_entry, &delete_list);
		}
	}

	/* Process all new devices. */
	for (i = 0; i < num_new_devices; i++) {
		device = new_device_list[i];
		if (!device->new_device)
			continue;
		if (device->volume_offline)
			continue;
		list_add_tail(&device->scsi_device_list_entry,
			&ctrl_info->scsi_device_list);
		list_add_tail(&device->add_list_entry, &add_list);
		/* To prevent this device structure from being freed later. */
		device->keep_device = true;
		pqi_init_device_tmf_work(device);
	}

	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	/*
	 * If OFA is in progress and there are devices that need to be deleted,
	 * allow any pending reset operations to continue and unblock any SCSI
	 * requests before removal.
	 */
	if (pqi_ofa_in_progress(ctrl_info)) {
		list_for_each_entry_safe(device, next, &delete_list, delete_list_entry)
			if (pqi_is_device_added(device))
				pqi_device_remove_start(device);
		pqi_ctrl_unblock_device_reset(ctrl_info);
		pqi_scsi_unblock_requests(ctrl_info);
	}

	/* Remove all devices that have gone away. */
	list_for_each_entry_safe(device, next, &delete_list, delete_list_entry) {
		if (device->volume_offline) {
			pqi_dev_info(ctrl_info, "offline", device);
			pqi_show_volume_status(ctrl_info, device);
		} else {
			pqi_dev_info(ctrl_info, "removed", device);
		}
		if (pqi_is_device_added(device))
			pqi_remove_device(ctrl_info, device);
		list_del(&device->delete_list_entry);
		pqi_free_device(device);
	}

	/*
	 * Notify the SML of any existing device changes such as;
	 * queue depth, device size.
	 */
	list_for_each_entry(device, &ctrl_info->scsi_device_list, scsi_device_list_entry) {
		/*
		 * Check for queue depth change.
		 */
		if (device->sdev && device->queue_depth != device->advertised_queue_depth) {
			device->advertised_queue_depth = device->queue_depth;
			scsi_change_queue_depth(device->sdev, device->advertised_queue_depth);
		}
		spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);
		/*
		 * Check for changes in the device, such as size.
		 */
		if (pqi_volume_rescan_needed(device)) {
			device->rescan = false;
			spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
			scsi_rescan_device(device->sdev);
		} else {
			spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
		}
	}

	/* Expose any new devices. */
	list_for_each_entry_safe(device, next, &add_list, add_list_entry) {
		if (!pqi_is_device_added(device)) {
			rc = pqi_add_device(ctrl_info, device);
			if (rc == 0) {
				pqi_dev_info(ctrl_info, "added", device);
			} else {
				dev_warn(&ctrl_info->pci_dev->dev,
					"scsi %d:%d:%d:%d addition failed, device not added\n",
					ctrl_info->scsi_host->host_no,
					device->bus, device->target,
					device->lun);
				pqi_fixup_botched_add(ctrl_info, device);
			}
		}
	}

}

static inline bool pqi_is_supported_device(struct pqi_scsi_dev *device)
{
	/*
	 * Only support the HBA controller itself as a RAID
	 * controller.  If it's a RAID controller other than
	 * the HBA itself (an external RAID controller, for
	 * example), we don't support it.
	 */
	if (device->device_type == SA_DEVICE_TYPE_CONTROLLER &&
		!pqi_is_hba_lunid(device->scsi3addr))
			return false;

	return true;
}

static inline bool pqi_skip_device(u8 *scsi3addr)
{
	/* Ignore all masked devices. */
	if (MASKED_DEVICE(scsi3addr))
		return true;

	return false;
}

static inline void pqi_mask_device(u8 *scsi3addr)
{
	scsi3addr[3] |= 0xc0;
}

static inline bool pqi_expose_device(struct pqi_scsi_dev *device)
{
	return !device->is_physical_device || !pqi_skip_device(device->scsi3addr);
}

static int pqi_update_scsi_devices(struct pqi_ctrl_info *ctrl_info)
{
	int i;
	int rc;
	LIST_HEAD(new_device_list_head);
	struct report_phys_lun_16byte_wwid_list *physdev_list = NULL;
	struct report_log_lun_list *logdev_list = NULL;
	struct report_phys_lun_16byte_wwid *phys_lun;
	struct report_log_lun *log_lun;
	struct bmic_identify_physical_device *id_phys = NULL;
	u32 num_physicals;
	u32 num_logicals;
	struct pqi_scsi_dev **new_device_list = NULL;
	struct pqi_scsi_dev *device;
	struct pqi_scsi_dev *next;
	unsigned int num_new_devices;
	unsigned int num_valid_devices;
	bool is_physical_device;
	u8 *scsi3addr;
	unsigned int physical_index;
	unsigned int logical_index;
	static char *out_of_memory_msg =
		"failed to allocate memory, device discovery stopped";

	rc = pqi_get_device_lists(ctrl_info, &physdev_list, &logdev_list);
	if (rc)
		goto out;

	if (physdev_list)
		num_physicals =
			get_unaligned_be32(&physdev_list->header.list_length)
				/ sizeof(physdev_list->lun_entries[0]);
	else
		num_physicals = 0;

	if (logdev_list)
		num_logicals =
			get_unaligned_be32(&logdev_list->header.list_length)
				/ sizeof(logdev_list->lun_entries[0]);
	else
		num_logicals = 0;

	if (num_physicals) {
		/*
		 * We need this buffer for calls to pqi_get_physical_disk_info()
		 * below.  We allocate it here instead of inside
		 * pqi_get_physical_disk_info() because it's a fairly large
		 * buffer.
		 */
		id_phys = kmalloc(sizeof(*id_phys), GFP_KERNEL);
		if (!id_phys) {
			dev_warn(&ctrl_info->pci_dev->dev, "%s\n",
				out_of_memory_msg);
			rc = -ENOMEM;
			goto out;
		}

		if (pqi_hide_vsep) {
			for (i = num_physicals - 1; i >= 0; i--) {
				phys_lun = &physdev_list->lun_entries[i];
				if (CISS_GET_DRIVE_NUMBER(phys_lun->lunid) == PQI_VSEP_CISS_BTL) {
					pqi_mask_device(phys_lun->lunid);
					break;
				}
			}
		}
	}

	if (num_logicals &&
		(logdev_list->header.flags & CISS_REPORT_LOG_FLAG_DRIVE_TYPE_MIX))
		ctrl_info->lv_drive_type_mix_valid = true;

	num_new_devices = num_physicals + num_logicals;

	new_device_list = kmalloc_array(num_new_devices,
					sizeof(*new_device_list),
					GFP_KERNEL);
	if (!new_device_list) {
		dev_warn(&ctrl_info->pci_dev->dev, "%s\n", out_of_memory_msg);
		rc = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_new_devices; i++) {
		device = kzalloc(sizeof(*device), GFP_KERNEL);
		if (!device) {
			dev_warn(&ctrl_info->pci_dev->dev, "%s\n",
				out_of_memory_msg);
			rc = -ENOMEM;
			goto out;
		}
		list_add_tail(&device->new_device_list_entry,
			&new_device_list_head);
	}

	device = NULL;
	num_valid_devices = 0;
	physical_index = 0;
	logical_index = 0;

	for (i = 0; i < num_new_devices; i++) {

		if ((!pqi_expose_ld_first && i < num_physicals) ||
			(pqi_expose_ld_first && i >= num_logicals)) {
			is_physical_device = true;
			phys_lun = &physdev_list->lun_entries[physical_index++];
			log_lun = NULL;
			scsi3addr = phys_lun->lunid;
		} else {
			is_physical_device = false;
			phys_lun = NULL;
			log_lun = &logdev_list->lun_entries[logical_index++];
			scsi3addr = log_lun->lunid;
		}

		if (is_physical_device && pqi_skip_device(scsi3addr))
			continue;

		if (device)
			device = list_next_entry(device, new_device_list_entry);
		else
			device = list_first_entry(&new_device_list_head,
				struct pqi_scsi_dev, new_device_list_entry);

		memcpy(device->scsi3addr, scsi3addr, sizeof(device->scsi3addr));
		device->is_physical_device = is_physical_device;
		if (is_physical_device) {
			device->device_type = phys_lun->device_type;
			if (device->device_type == SA_DEVICE_TYPE_EXPANDER_SMP)
				device->is_expander_smp_device = true;
		} else {
			device->is_external_raid_device =
				pqi_is_external_raid_addr(scsi3addr);
		}

		if (!pqi_is_supported_device(device))
			continue;

		/* Gather information about the device. */
		rc = pqi_get_device_info(ctrl_info, device, id_phys);
		if (rc == -ENOMEM) {
			dev_warn(&ctrl_info->pci_dev->dev, "%s\n",
				out_of_memory_msg);
			goto out;
		}
		if (rc) {
			if (device->is_physical_device)
				dev_warn(&ctrl_info->pci_dev->dev,
					"obtaining device info failed, skipping physical device %016llx%016llx\n",
					get_unaligned_be64(&phys_lun->wwid[0]),
					get_unaligned_be64(&phys_lun->wwid[8]));
			else
				dev_warn(&ctrl_info->pci_dev->dev,
					"obtaining device info failed, skipping logical device %08x%08x\n",
					*((u32 *)&device->scsi3addr),
					*((u32 *)&device->scsi3addr[4]));
			rc = 0;
			continue;
		}

		/* Do not present disks that the OS cannot fully probe. */
		if (pqi_keep_device_offline(device))
			continue;

		pqi_assign_bus_target_lun(device);

		if (device->is_physical_device) {
			memcpy(device->wwid, phys_lun->wwid, sizeof(device->wwid));
			if ((phys_lun->device_flags &
				CISS_REPORT_PHYS_DEV_FLAG_AIO_ENABLED) &&
				phys_lun->aio_handle) {
					device->aio_enabled = true;
					device->aio_handle =
						phys_lun->aio_handle;
			}
		} else {
			memcpy(device->volume_id, log_lun->volume_id,
				sizeof(device->volume_id));
		}

		device->sas_address = get_unaligned_be64(&device->wwid[0]);

		new_device_list[num_valid_devices++] = device;
	}

	pqi_update_device_list(ctrl_info, new_device_list, num_valid_devices);

out:
	list_for_each_entry_safe(device, next, &new_device_list_head,
		new_device_list_entry) {
		if (device->keep_device)
			continue;
		list_del(&device->new_device_list_entry);
		pqi_free_device(device);
	}

	kfree(new_device_list);
	kfree(physdev_list);
	kfree(logdev_list);
	kfree(id_phys);

	return rc;
}

static int pqi_scan_scsi_devices(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	int mutex_acquired;

	if (pqi_ctrl_offline(ctrl_info))
		return -ENXIO;

	mutex_acquired = mutex_trylock(&ctrl_info->scan_mutex);

	if (!mutex_acquired) {
		if (pqi_ctrl_scan_blocked(ctrl_info))
			return -EBUSY;
		pqi_schedule_rescan_worker_delayed(ctrl_info);
		return -EINPROGRESS;
	}

	rc = pqi_update_scsi_devices(ctrl_info);
	if (rc && !pqi_ctrl_scan_blocked(ctrl_info))
		pqi_schedule_rescan_worker_delayed(ctrl_info);

	mutex_unlock(&ctrl_info->scan_mutex);

	return rc;
}

static void pqi_scan_start(struct Scsi_Host *shost)
{
	struct pqi_ctrl_info *ctrl_info;

	ctrl_info = shost_to_hba(shost);

	pqi_scan_scsi_devices(ctrl_info);
}

/* Returns TRUE if scan is finished. */

static int pqi_scan_finished(struct Scsi_Host *shost,
	unsigned long elapsed_time)
{
	struct pqi_ctrl_info *ctrl_info;

	ctrl_info = shost_priv(shost);

	return !mutex_is_locked(&ctrl_info->scan_mutex);
}

static inline void pqi_set_encryption_info(struct pqi_encryption_info *encryption_info,
	struct raid_map *raid_map, u64 first_block)
{
	u32 volume_blk_size;

	/*
	 * Set the encryption tweak values based on logical block address.
	 * If the block size is 512, the tweak value is equal to the LBA.
	 * For other block sizes, tweak value is (LBA * block size) / 512.
	 */
	volume_blk_size = get_unaligned_le32(&raid_map->volume_blk_size);
	if (volume_blk_size != 512)
		first_block = (first_block * volume_blk_size) / 512;

	encryption_info->data_encryption_key_index =
		get_unaligned_le16(&raid_map->data_encryption_key_index);
	encryption_info->encrypt_tweak_lower = lower_32_bits(first_block);
	encryption_info->encrypt_tweak_upper = upper_32_bits(first_block);
}

/*
 * Attempt to perform RAID bypass mapping for a logical volume I/O.
 */

static bool pqi_aio_raid_level_supported(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev_raid_map_data *rmd)
{
	bool is_supported = true;

	switch (rmd->raid_level) {
	case SA_RAID_0:
		break;
	case SA_RAID_1:
		if (rmd->is_write && (!ctrl_info->enable_r1_writes ||
			rmd->data_length > ctrl_info->max_write_raid_1_10_2drive))
			is_supported = false;
		break;
	case SA_RAID_TRIPLE:
		if (rmd->is_write && (!ctrl_info->enable_r1_writes ||
			rmd->data_length > ctrl_info->max_write_raid_1_10_3drive))
			is_supported = false;
		break;
	case SA_RAID_5:
		if (rmd->is_write && (!ctrl_info->enable_r5_writes ||
			rmd->data_length > ctrl_info->max_write_raid_5_6))
			is_supported = false;
		break;
	case SA_RAID_6:
		if (rmd->is_write && (!ctrl_info->enable_r6_writes ||
			rmd->data_length > ctrl_info->max_write_raid_5_6))
			is_supported = false;
		break;
	default:
		is_supported = false;
		break;
	}

	return is_supported;
}

#define PQI_RAID_BYPASS_INELIGIBLE	1

static int pqi_get_aio_lba_and_block_count(struct scsi_cmnd *scmd,
	struct pqi_scsi_dev_raid_map_data *rmd)
{
	/* Check for valid opcode, get LBA and block count. */
	switch (scmd->cmnd[0]) {
	case WRITE_6:
		rmd->is_write = true;
		fallthrough;
	case READ_6:
		rmd->first_block = (u64)(((scmd->cmnd[1] & 0x1f) << 16) |
			(scmd->cmnd[2] << 8) | scmd->cmnd[3]);
		rmd->block_cnt = (u32)scmd->cmnd[4];
		if (rmd->block_cnt == 0)
			rmd->block_cnt = 256;
		break;
	case WRITE_10:
		rmd->is_write = true;
		fallthrough;
	case READ_10:
		rmd->first_block = (u64)get_unaligned_be32(&scmd->cmnd[2]);
		rmd->block_cnt = (u32)get_unaligned_be16(&scmd->cmnd[7]);
		break;
	case WRITE_12:
		rmd->is_write = true;
		fallthrough;
	case READ_12:
		rmd->first_block = (u64)get_unaligned_be32(&scmd->cmnd[2]);
		rmd->block_cnt = get_unaligned_be32(&scmd->cmnd[6]);
		break;
	case WRITE_16:
		rmd->is_write = true;
		fallthrough;
	case READ_16:
		rmd->first_block = get_unaligned_be64(&scmd->cmnd[2]);
		rmd->block_cnt = get_unaligned_be32(&scmd->cmnd[10]);
		break;
	default:
		/* Process via normal I/O path. */
		return PQI_RAID_BYPASS_INELIGIBLE;
	}

	put_unaligned_le32(scsi_bufflen(scmd), &rmd->data_length);

	return 0;
}

static int pci_get_aio_common_raid_map_values(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev_raid_map_data *rmd, struct raid_map *raid_map)
{
#if BITS_PER_LONG == 32
	u64 tmpdiv;
#endif

	rmd->last_block = rmd->first_block + rmd->block_cnt - 1;

	/* Check for invalid block or wraparound. */
	if (rmd->last_block >=
		get_unaligned_le64(&raid_map->volume_blk_cnt) ||
		rmd->last_block < rmd->first_block)
		return PQI_RAID_BYPASS_INELIGIBLE;

	rmd->data_disks_per_row =
		get_unaligned_le16(&raid_map->data_disks_per_row);
	rmd->strip_size = get_unaligned_le16(&raid_map->strip_size);
	rmd->layout_map_count = get_unaligned_le16(&raid_map->layout_map_count);

	/* Calculate stripe information for the request. */
	rmd->blocks_per_row = rmd->data_disks_per_row * rmd->strip_size;
	if (rmd->blocks_per_row == 0) /* Used as a divisor in many calculations */
		return PQI_RAID_BYPASS_INELIGIBLE;
#if BITS_PER_LONG == 32
	tmpdiv = rmd->first_block;
	do_div(tmpdiv, rmd->blocks_per_row);
	rmd->first_row = tmpdiv;
	tmpdiv = rmd->last_block;
	do_div(tmpdiv, rmd->blocks_per_row);
	rmd->last_row = tmpdiv;
	rmd->first_row_offset = (u32)(rmd->first_block - (rmd->first_row * rmd->blocks_per_row));
	rmd->last_row_offset = (u32)(rmd->last_block - (rmd->last_row * rmd->blocks_per_row));
	tmpdiv = rmd->first_row_offset;
	do_div(tmpdiv, rmd->strip_size);
	rmd->first_column = tmpdiv;
	tmpdiv = rmd->last_row_offset;
	do_div(tmpdiv, rmd->strip_size);
	rmd->last_column = tmpdiv;
#else
	rmd->first_row = rmd->first_block / rmd->blocks_per_row;
	rmd->last_row = rmd->last_block / rmd->blocks_per_row;
	rmd->first_row_offset = (u32)(rmd->first_block -
		(rmd->first_row * rmd->blocks_per_row));
	rmd->last_row_offset = (u32)(rmd->last_block - (rmd->last_row *
		rmd->blocks_per_row));
	rmd->first_column = rmd->first_row_offset / rmd->strip_size;
	rmd->last_column = rmd->last_row_offset / rmd->strip_size;
#endif

	/* If this isn't a single row/column then give to the controller. */
	if (rmd->first_row != rmd->last_row ||
		rmd->first_column != rmd->last_column)
		return PQI_RAID_BYPASS_INELIGIBLE;

	/* Proceeding with driver mapping. */
	rmd->total_disks_per_row = rmd->data_disks_per_row +
		get_unaligned_le16(&raid_map->metadata_disks_per_row);
	rmd->map_row = ((u32)(rmd->first_row >>
		raid_map->parity_rotation_shift)) %
		get_unaligned_le16(&raid_map->row_cnt);
	rmd->map_index = (rmd->map_row * rmd->total_disks_per_row) +
		rmd->first_column;

	return 0;
}

static int pqi_calc_aio_r5_or_r6(struct pqi_scsi_dev_raid_map_data *rmd,
	struct raid_map *raid_map)
{
#if BITS_PER_LONG == 32
	u64 tmpdiv;
#endif

	if (rmd->blocks_per_row == 0) /* Used as a divisor in many calculations */
		return PQI_RAID_BYPASS_INELIGIBLE;

	/* RAID 50/60 */
	/* Verify first and last block are in same RAID group. */
	rmd->stripesize = rmd->blocks_per_row * rmd->layout_map_count;
#if BITS_PER_LONG == 32
	tmpdiv = rmd->first_block;
	rmd->first_group = do_div(tmpdiv, rmd->stripesize);
	tmpdiv = rmd->first_group;
	do_div(tmpdiv, rmd->blocks_per_row);
	rmd->first_group = tmpdiv;
	tmpdiv = rmd->last_block;
	rmd->last_group = do_div(tmpdiv, rmd->stripesize);
	tmpdiv = rmd->last_group;
	do_div(tmpdiv, rmd->blocks_per_row);
	rmd->last_group = tmpdiv;
#else
	rmd->first_group = (rmd->first_block % rmd->stripesize) / rmd->blocks_per_row;
	rmd->last_group = (rmd->last_block % rmd->stripesize) / rmd->blocks_per_row;
#endif
	if (rmd->first_group != rmd->last_group)
		return PQI_RAID_BYPASS_INELIGIBLE;

	/* Verify request is in a single row of RAID 5/6. */
#if BITS_PER_LONG == 32
	tmpdiv = rmd->first_block;
	do_div(tmpdiv, rmd->stripesize);
	rmd->first_row = tmpdiv;
	rmd->r5or6_first_row = tmpdiv;
	tmpdiv = rmd->last_block;
	do_div(tmpdiv, rmd->stripesize);
	rmd->r5or6_last_row = tmpdiv;
#else
	rmd->first_row = rmd->r5or6_first_row =
		rmd->first_block / rmd->stripesize;
	rmd->r5or6_last_row = rmd->last_block / rmd->stripesize;
#endif
	if (rmd->r5or6_first_row != rmd->r5or6_last_row)
		return PQI_RAID_BYPASS_INELIGIBLE;

	/* Verify request is in a single column. */
#if BITS_PER_LONG == 32
	tmpdiv = rmd->first_block;
	rmd->first_row_offset = do_div(tmpdiv, rmd->stripesize);
	tmpdiv = rmd->first_row_offset;
	rmd->first_row_offset = (u32)do_div(tmpdiv, rmd->blocks_per_row);
	rmd->r5or6_first_row_offset = rmd->first_row_offset;
	tmpdiv = rmd->last_block;
	rmd->r5or6_last_row_offset = do_div(tmpdiv, rmd->stripesize);
	tmpdiv = rmd->r5or6_last_row_offset;
	rmd->r5or6_last_row_offset = do_div(tmpdiv, rmd->blocks_per_row);
	tmpdiv = rmd->r5or6_first_row_offset;
	do_div(tmpdiv, rmd->strip_size);
	rmd->first_column = rmd->r5or6_first_column = tmpdiv;
	tmpdiv = rmd->r5or6_last_row_offset;
	do_div(tmpdiv, rmd->strip_size);
	rmd->r5or6_last_column = tmpdiv;
#else
	rmd->first_row_offset = rmd->r5or6_first_row_offset =
		(u32)((rmd->first_block % rmd->stripesize) %
		rmd->blocks_per_row);

	rmd->r5or6_last_row_offset =
		(u32)((rmd->last_block % rmd->stripesize) %
		rmd->blocks_per_row);

	rmd->first_column =
		rmd->r5or6_first_row_offset / rmd->strip_size;
	rmd->r5or6_first_column = rmd->first_column;
	rmd->r5or6_last_column = rmd->r5or6_last_row_offset / rmd->strip_size;
#endif
	if (rmd->r5or6_first_column != rmd->r5or6_last_column)
		return PQI_RAID_BYPASS_INELIGIBLE;

	/* Request is eligible. */
	rmd->map_row =
		((u32)(rmd->first_row >> raid_map->parity_rotation_shift)) %
		get_unaligned_le16(&raid_map->row_cnt);

	rmd->map_index = (rmd->first_group *
		(get_unaligned_le16(&raid_map->row_cnt) *
		rmd->total_disks_per_row)) +
		(rmd->map_row * rmd->total_disks_per_row) + rmd->first_column;

	if (rmd->is_write) {
		u32 index;

		/*
		 * p_parity_it_nexus and q_parity_it_nexus are pointers to the
		 * parity entries inside the device's raid_map.
		 *
		 * A device's RAID map is bounded by: number of RAID disks squared.
		 *
		 * The devices RAID map size is checked during device
		 * initialization.
		 */
		index = DIV_ROUND_UP(rmd->map_index + 1, rmd->total_disks_per_row);
		index *= rmd->total_disks_per_row;
		index -= get_unaligned_le16(&raid_map->metadata_disks_per_row);

		rmd->p_parity_it_nexus = raid_map->disk_data[index].aio_handle;
		if (rmd->raid_level == SA_RAID_6) {
			rmd->q_parity_it_nexus = raid_map->disk_data[index + 1].aio_handle;
			rmd->xor_mult = raid_map->disk_data[rmd->map_index].xor_mult[1];
		}
#if BITS_PER_LONG == 32
		tmpdiv = rmd->first_block;
		do_div(tmpdiv, rmd->blocks_per_row);
		rmd->row = tmpdiv;
#else
		rmd->row = rmd->first_block / rmd->blocks_per_row;
#endif
	}

	return 0;
}

static void pqi_set_aio_cdb(struct pqi_scsi_dev_raid_map_data *rmd)
{
	/* Build the new CDB for the physical disk I/O. */
	if (rmd->disk_block > 0xffffffff) {
		rmd->cdb[0] = rmd->is_write ? WRITE_16 : READ_16;
		rmd->cdb[1] = 0;
		put_unaligned_be64(rmd->disk_block, &rmd->cdb[2]);
		put_unaligned_be32(rmd->disk_block_cnt, &rmd->cdb[10]);
		rmd->cdb[14] = 0;
		rmd->cdb[15] = 0;
		rmd->cdb_length = 16;
	} else {
		rmd->cdb[0] = rmd->is_write ? WRITE_10 : READ_10;
		rmd->cdb[1] = 0;
		put_unaligned_be32((u32)rmd->disk_block, &rmd->cdb[2]);
		rmd->cdb[6] = 0;
		put_unaligned_be16((u16)rmd->disk_block_cnt, &rmd->cdb[7]);
		rmd->cdb[9] = 0;
		rmd->cdb_length = 10;
	}
}

static void pqi_calc_aio_r1_nexus(struct raid_map *raid_map,
	struct pqi_scsi_dev_raid_map_data *rmd)
{
	u32 index;
	u32 group;

	group = rmd->map_index / rmd->data_disks_per_row;

	index = rmd->map_index - (group * rmd->data_disks_per_row);
	rmd->it_nexus[0] = raid_map->disk_data[index].aio_handle;
	index += rmd->data_disks_per_row;
	rmd->it_nexus[1] = raid_map->disk_data[index].aio_handle;
	if (rmd->layout_map_count > 2) {
		index += rmd->data_disks_per_row;
		rmd->it_nexus[2] = raid_map->disk_data[index].aio_handle;
	}

	rmd->num_it_nexus_entries = rmd->layout_map_count;
}

static int pqi_raid_bypass_submit_scsi_cmd(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device, struct scsi_cmnd *scmd,
	struct pqi_queue_group *queue_group)
{
	int rc;
	struct raid_map *raid_map;
	u32 group;
	u32 next_bypass_group;
	struct pqi_encryption_info *encryption_info_ptr;
	struct pqi_encryption_info encryption_info;
	struct pqi_scsi_dev_raid_map_data rmd = { 0 };

	rc = pqi_get_aio_lba_and_block_count(scmd, &rmd);
	if (rc)
		return PQI_RAID_BYPASS_INELIGIBLE;

	rmd.raid_level = device->raid_level;

	if (!pqi_aio_raid_level_supported(ctrl_info, &rmd))
		return PQI_RAID_BYPASS_INELIGIBLE;

	if (unlikely(rmd.block_cnt == 0))
		return PQI_RAID_BYPASS_INELIGIBLE;

	raid_map = device->raid_map;

	rc = pci_get_aio_common_raid_map_values(ctrl_info, &rmd, raid_map);
	if (rc)
		return PQI_RAID_BYPASS_INELIGIBLE;

	if (device->raid_level == SA_RAID_1 ||
		device->raid_level == SA_RAID_TRIPLE) {
		if (rmd.is_write) {
			pqi_calc_aio_r1_nexus(raid_map, &rmd);
		} else {
			group = device->next_bypass_group[rmd.map_index];
			next_bypass_group = group + 1;
			if (next_bypass_group >= rmd.layout_map_count)
				next_bypass_group = 0;
			device->next_bypass_group[rmd.map_index] = next_bypass_group;
			rmd.map_index += group * rmd.data_disks_per_row;
		}
	} else if ((device->raid_level == SA_RAID_5 ||
		device->raid_level == SA_RAID_6) &&
		(rmd.layout_map_count > 1 || rmd.is_write)) {
		rc = pqi_calc_aio_r5_or_r6(&rmd, raid_map);
		if (rc)
			return PQI_RAID_BYPASS_INELIGIBLE;
	}

	if (unlikely(rmd.map_index >= RAID_MAP_MAX_ENTRIES))
		return PQI_RAID_BYPASS_INELIGIBLE;

	rmd.aio_handle = raid_map->disk_data[rmd.map_index].aio_handle;
	rmd.disk_block = get_unaligned_le64(&raid_map->disk_starting_blk) +
		rmd.first_row * rmd.strip_size +
		(rmd.first_row_offset - rmd.first_column * rmd.strip_size);
	rmd.disk_block_cnt = rmd.block_cnt;

	/* Handle differing logical/physical block sizes. */
	if (raid_map->phys_blk_shift) {
		rmd.disk_block <<= raid_map->phys_blk_shift;
		rmd.disk_block_cnt <<= raid_map->phys_blk_shift;
	}

	if (unlikely(rmd.disk_block_cnt > 0xffff))
		return PQI_RAID_BYPASS_INELIGIBLE;

	pqi_set_aio_cdb(&rmd);

	if (get_unaligned_le16(&raid_map->flags) & RAID_MAP_ENCRYPTION_ENABLED) {
		if (rmd.data_length > device->max_transfer_encrypted)
			return PQI_RAID_BYPASS_INELIGIBLE;
		pqi_set_encryption_info(&encryption_info, raid_map, rmd.first_block);
		encryption_info_ptr = &encryption_info;
	} else {
		encryption_info_ptr = NULL;
	}

	if (rmd.is_write) {
		switch (device->raid_level) {
		case SA_RAID_1:
		case SA_RAID_TRIPLE:
			return pqi_aio_submit_r1_write_io(ctrl_info, scmd, queue_group,
				encryption_info_ptr, device, &rmd);
		case SA_RAID_5:
		case SA_RAID_6:
			return pqi_aio_submit_r56_write_io(ctrl_info, scmd, queue_group,
				encryption_info_ptr, device, &rmd);
		}
	}

	return pqi_aio_submit_io(ctrl_info, scmd, rmd.aio_handle,
		rmd.cdb, rmd.cdb_length, queue_group,
		encryption_info_ptr, true, false);
}

#define PQI_STATUS_IDLE		0x0

#define PQI_CREATE_ADMIN_QUEUE_PAIR	1
#define PQI_DELETE_ADMIN_QUEUE_PAIR	2

#define PQI_DEVICE_STATE_POWER_ON_AND_RESET		0x0
#define PQI_DEVICE_STATE_STATUS_AVAILABLE		0x1
#define PQI_DEVICE_STATE_ALL_REGISTERS_READY		0x2
#define PQI_DEVICE_STATE_ADMIN_QUEUE_PAIR_READY		0x3
#define PQI_DEVICE_STATE_ERROR				0x4

#define PQI_MODE_READY_TIMEOUT_SECS		30
#define PQI_MODE_READY_POLL_INTERVAL_MSECS	1

static int pqi_wait_for_pqi_mode_ready(struct pqi_ctrl_info *ctrl_info)
{
	struct pqi_device_registers __iomem *pqi_registers;
	unsigned long timeout;
	u64 signature;
	u8 status;

	pqi_registers = ctrl_info->pqi_registers;
	timeout = (PQI_MODE_READY_TIMEOUT_SECS * HZ) + jiffies;

	while (1) {
		signature = readq(&pqi_registers->signature);
		if (memcmp(&signature, PQI_DEVICE_SIGNATURE,
			sizeof(signature)) == 0)
			break;
		if (time_after(jiffies, timeout)) {
			dev_err(&ctrl_info->pci_dev->dev,
				"timed out waiting for PQI signature\n");
			return -ETIMEDOUT;
		}
		msleep(PQI_MODE_READY_POLL_INTERVAL_MSECS);
	}

	while (1) {
		status = readb(&pqi_registers->function_and_status_code);
		if (status == PQI_STATUS_IDLE)
			break;
		if (time_after(jiffies, timeout)) {
			dev_err(&ctrl_info->pci_dev->dev,
				"timed out waiting for PQI IDLE\n");
			return -ETIMEDOUT;
		}
		msleep(PQI_MODE_READY_POLL_INTERVAL_MSECS);
	}

	while (1) {
		if (readl(&pqi_registers->device_status) ==
			PQI_DEVICE_STATE_ALL_REGISTERS_READY)
			break;
		if (time_after(jiffies, timeout)) {
			dev_err(&ctrl_info->pci_dev->dev,
				"timed out waiting for PQI all registers ready\n");
			return -ETIMEDOUT;
		}
		msleep(PQI_MODE_READY_POLL_INTERVAL_MSECS);
	}

	return 0;
}

static inline void pqi_aio_path_disabled(struct pqi_io_request *io_request)
{
	struct pqi_scsi_dev *device;

	device = io_request->scmd->device->hostdata;
	device->raid_bypass_enabled = false;
	device->aio_enabled = false;
}

static inline void pqi_take_device_offline(struct scsi_device *sdev, char *path)
{
	struct pqi_ctrl_info *ctrl_info;
	struct pqi_scsi_dev *device;

	device = sdev->hostdata;
	if (device->device_offline)
		return;

	device->device_offline = true;
	ctrl_info = shost_to_hba(sdev->host);
	pqi_schedule_rescan_worker(ctrl_info);
	dev_err(&ctrl_info->pci_dev->dev, "re-scanning %s scsi %d:%d:%d:%d\n",
		path, ctrl_info->scsi_host->host_no, device->bus,
		device->target, device->lun);
}

static void pqi_process_raid_io_error(struct pqi_io_request *io_request)
{
	u8 scsi_status;
	u8 host_byte;
	struct scsi_cmnd *scmd;
	struct pqi_raid_error_info *error_info;
	size_t sense_data_length;
	int residual_count;
	int xfer_count;
	struct scsi_sense_hdr sshdr;

	scmd = io_request->scmd;
	if (!scmd)
		return;

	error_info = io_request->error_info;
	scsi_status = error_info->status;
	host_byte = DID_OK;

	switch (error_info->data_out_result) {
	case PQI_DATA_IN_OUT_GOOD:
		break;
	case PQI_DATA_IN_OUT_UNDERFLOW:
		xfer_count =
			get_unaligned_le32(&error_info->data_out_transferred);
		residual_count = scsi_bufflen(scmd) - xfer_count;
		scsi_set_resid(scmd, residual_count);
		if (xfer_count < scmd->underflow)
			host_byte = DID_SOFT_ERROR;
		break;
	case PQI_DATA_IN_OUT_UNSOLICITED_ABORT:
	case PQI_DATA_IN_OUT_ABORTED:
		host_byte = DID_ABORT;
		break;
	case PQI_DATA_IN_OUT_TIMEOUT:
		host_byte = DID_TIME_OUT;
		break;
	case PQI_DATA_IN_OUT_BUFFER_OVERFLOW:
	case PQI_DATA_IN_OUT_PROTOCOL_ERROR:
	case PQI_DATA_IN_OUT_BUFFER_ERROR:
	case PQI_DATA_IN_OUT_BUFFER_OVERFLOW_DESCRIPTOR_AREA:
	case PQI_DATA_IN_OUT_BUFFER_OVERFLOW_BRIDGE:
	case PQI_DATA_IN_OUT_ERROR:
	case PQI_DATA_IN_OUT_HARDWARE_ERROR:
	case PQI_DATA_IN_OUT_PCIE_FABRIC_ERROR:
	case PQI_DATA_IN_OUT_PCIE_COMPLETION_TIMEOUT:
	case PQI_DATA_IN_OUT_PCIE_COMPLETER_ABORT_RECEIVED:
	case PQI_DATA_IN_OUT_PCIE_UNSUPPORTED_REQUEST_RECEIVED:
	case PQI_DATA_IN_OUT_PCIE_ECRC_CHECK_FAILED:
	case PQI_DATA_IN_OUT_PCIE_UNSUPPORTED_REQUEST:
	case PQI_DATA_IN_OUT_PCIE_ACS_VIOLATION:
	case PQI_DATA_IN_OUT_PCIE_TLP_PREFIX_BLOCKED:
	case PQI_DATA_IN_OUT_PCIE_POISONED_MEMORY_READ:
	default:
		host_byte = DID_ERROR;
		break;
	}

	sense_data_length = get_unaligned_le16(&error_info->sense_data_length);
	if (sense_data_length == 0)
		sense_data_length =
			get_unaligned_le16(&error_info->response_data_length);
	if (sense_data_length) {
		if (sense_data_length > sizeof(error_info->data))
			sense_data_length = sizeof(error_info->data);

		if (scsi_status == SAM_STAT_CHECK_CONDITION &&
			scsi_normalize_sense(error_info->data,
				sense_data_length, &sshdr) &&
				sshdr.sense_key == HARDWARE_ERROR &&
				sshdr.asc == 0x3e) {
			struct pqi_ctrl_info *ctrl_info = shost_to_hba(scmd->device->host);
			struct pqi_scsi_dev *device = scmd->device->hostdata;

			switch (sshdr.ascq) {
			case 0x1: /* LOGICAL UNIT FAILURE */
				if (printk_ratelimit())
					scmd_printk(KERN_ERR, scmd, "received 'logical unit failure' from controller for scsi %d:%d:%d:%d\n",
						ctrl_info->scsi_host->host_no, device->bus, device->target, device->lun);
				pqi_take_device_offline(scmd->device, "RAID");
				host_byte = DID_NO_CONNECT;
				break;

			default: /* See http://www.t10.org/lists/asc-num.htm#ASC_3E */
				if (printk_ratelimit())
					scmd_printk(KERN_ERR, scmd, "received unhandled error %d from controller for scsi %d:%d:%d:%d\n",
						sshdr.ascq, ctrl_info->scsi_host->host_no, device->bus, device->target, device->lun);
				break;
			}
		}

		if (sense_data_length > SCSI_SENSE_BUFFERSIZE)
			sense_data_length = SCSI_SENSE_BUFFERSIZE;
		memcpy(scmd->sense_buffer, error_info->data,
			sense_data_length);
	}

	if (pqi_cmd_priv(scmd)->this_residual &&
	    !pqi_is_logical_device(scmd->device->hostdata) &&
	    scsi_status == SAM_STAT_CHECK_CONDITION &&
	    host_byte == DID_OK &&
	    sense_data_length &&
	    scsi_normalize_sense(error_info->data, sense_data_length, &sshdr) &&
	    sshdr.sense_key == ILLEGAL_REQUEST &&
	    sshdr.asc == 0x26 &&
	    sshdr.ascq == 0x0) {
		host_byte = DID_NO_CONNECT;
		pqi_take_device_offline(scmd->device, "AIO");
		scsi_build_sense_buffer(0, scmd->sense_buffer, HARDWARE_ERROR, 0x3e, 0x1);
	}

	scmd->result = scsi_status;
	set_host_byte(scmd, host_byte);
}

static void pqi_process_aio_io_error(struct pqi_io_request *io_request)
{
	u8 scsi_status;
	u8 host_byte;
	struct scsi_cmnd *scmd;
	struct pqi_aio_error_info *error_info;
	size_t sense_data_length;
	int residual_count;
	int xfer_count;
	bool device_offline;

	scmd = io_request->scmd;
	error_info = io_request->error_info;
	host_byte = DID_OK;
	sense_data_length = 0;
	device_offline = false;

	switch (error_info->service_response) {
	case PQI_AIO_SERV_RESPONSE_COMPLETE:
		scsi_status = error_info->status;
		break;
	case PQI_AIO_SERV_RESPONSE_FAILURE:
		switch (error_info->status) {
		case PQI_AIO_STATUS_IO_ABORTED:
			scsi_status = SAM_STAT_TASK_ABORTED;
			break;
		case PQI_AIO_STATUS_UNDERRUN:
			scsi_status = SAM_STAT_GOOD;
			residual_count = get_unaligned_le32(
						&error_info->residual_count);
			scsi_set_resid(scmd, residual_count);
			xfer_count = scsi_bufflen(scmd) - residual_count;
			if (xfer_count < scmd->underflow)
				host_byte = DID_SOFT_ERROR;
			break;
		case PQI_AIO_STATUS_OVERRUN:
			scsi_status = SAM_STAT_GOOD;
			break;
		case PQI_AIO_STATUS_AIO_PATH_DISABLED:
			pqi_aio_path_disabled(io_request);
			scsi_status = SAM_STAT_GOOD;
			io_request->status = -EAGAIN;
			break;
		case PQI_AIO_STATUS_NO_PATH_TO_DEVICE:
		case PQI_AIO_STATUS_INVALID_DEVICE:
			if (!io_request->raid_bypass) {
				device_offline = true;
				pqi_take_device_offline(scmd->device, "AIO");
				host_byte = DID_NO_CONNECT;
			}
			scsi_status = SAM_STAT_CHECK_CONDITION;
			break;
		case PQI_AIO_STATUS_IO_ERROR:
		default:
			scsi_status = SAM_STAT_CHECK_CONDITION;
			break;
		}
		break;
	case PQI_AIO_SERV_RESPONSE_TMF_COMPLETE:
	case PQI_AIO_SERV_RESPONSE_TMF_SUCCEEDED:
		scsi_status = SAM_STAT_GOOD;
		break;
	case PQI_AIO_SERV_RESPONSE_TMF_REJECTED:
	case PQI_AIO_SERV_RESPONSE_TMF_INCORRECT_LUN:
	default:
		scsi_status = SAM_STAT_CHECK_CONDITION;
		break;
	}

	if (error_info->data_present) {
		sense_data_length =
			get_unaligned_le16(&error_info->data_length);
		if (sense_data_length) {
			if (sense_data_length > sizeof(error_info->data))
				sense_data_length = sizeof(error_info->data);
			if (sense_data_length > SCSI_SENSE_BUFFERSIZE)
				sense_data_length = SCSI_SENSE_BUFFERSIZE;
			memcpy(scmd->sense_buffer, error_info->data,
				sense_data_length);
		}
	}

	if (device_offline && sense_data_length == 0)
		scsi_build_sense(scmd, 0, HARDWARE_ERROR, 0x3e, 0x1);

	scmd->result = scsi_status;
	set_host_byte(scmd, host_byte);
}

static void pqi_process_io_error(unsigned int iu_type,
	struct pqi_io_request *io_request)
{
	switch (iu_type) {
	case PQI_RESPONSE_IU_RAID_PATH_IO_ERROR:
		pqi_process_raid_io_error(io_request);
		break;
	case PQI_RESPONSE_IU_AIO_PATH_IO_ERROR:
		pqi_process_aio_io_error(io_request);
		break;
	}
}

static int pqi_interpret_task_management_response(struct pqi_ctrl_info *ctrl_info,
	struct pqi_task_management_response *response)
{
	int rc;

	switch (response->response_code) {
	case SOP_TMF_COMPLETE:
	case SOP_TMF_FUNCTION_SUCCEEDED:
		rc = 0;
		break;
	case SOP_TMF_REJECTED:
		rc = -EAGAIN;
		break;
	case SOP_TMF_INCORRECT_LOGICAL_UNIT:
		rc = -ENODEV;
		break;
	default:
		rc = -EIO;
		break;
	}

	if (rc)
		dev_err(&ctrl_info->pci_dev->dev,
			"Task Management Function error: %d (response code: %u)\n", rc, response->response_code);

	return rc;
}

static inline void pqi_invalid_response(struct pqi_ctrl_info *ctrl_info,
	enum pqi_ctrl_shutdown_reason ctrl_shutdown_reason)
{
	pqi_take_ctrl_offline(ctrl_info, ctrl_shutdown_reason);
}

static int pqi_process_io_intr(struct pqi_ctrl_info *ctrl_info, struct pqi_queue_group *queue_group)
{
	int num_responses;
	pqi_index_t oq_pi;
	pqi_index_t oq_ci;
	struct pqi_io_request *io_request;
	struct pqi_io_response *response;
	u16 request_id;

	num_responses = 0;
	oq_ci = queue_group->oq_ci_copy;

	while (1) {
		oq_pi = readl(queue_group->oq_pi);
		if (oq_pi >= ctrl_info->num_elements_per_oq) {
			pqi_invalid_response(ctrl_info, PQI_IO_PI_OUT_OF_RANGE);
			dev_err(&ctrl_info->pci_dev->dev,
				"I/O interrupt: producer index (%u) out of range (0-%u): consumer index: %u\n",
				oq_pi, ctrl_info->num_elements_per_oq - 1, oq_ci);
			return -1;
		}
		if (oq_pi == oq_ci)
			break;

		num_responses++;
		response = queue_group->oq_element_array +
			(oq_ci * PQI_OPERATIONAL_OQ_ELEMENT_LENGTH);

		request_id = get_unaligned_le16(&response->request_id);
		if (request_id >= ctrl_info->max_io_slots) {
			pqi_invalid_response(ctrl_info, PQI_INVALID_REQ_ID);
			dev_err(&ctrl_info->pci_dev->dev,
				"request ID in response (%u) out of range (0-%u): producer index: %u  consumer index: %u\n",
				request_id, ctrl_info->max_io_slots - 1, oq_pi, oq_ci);
			return -1;
		}

		io_request = &ctrl_info->io_request_pool[request_id];
		if (atomic_read(&io_request->refcount) == 0) {
			pqi_invalid_response(ctrl_info, PQI_UNMATCHED_REQ_ID);
			dev_err(&ctrl_info->pci_dev->dev,
				"request ID in response (%u) does not match an outstanding I/O request: producer index: %u  consumer index: %u\n",
				request_id, oq_pi, oq_ci);
			return -1;
		}

		switch (response->header.iu_type) {
		case PQI_RESPONSE_IU_RAID_PATH_IO_SUCCESS:
		case PQI_RESPONSE_IU_AIO_PATH_IO_SUCCESS:
			if (io_request->scmd)
				io_request->scmd->result = 0;
			fallthrough;
		case PQI_RESPONSE_IU_GENERAL_MANAGEMENT:
			break;
		case PQI_RESPONSE_IU_VENDOR_GENERAL:
			io_request->status =
				get_unaligned_le16(
				&((struct pqi_vendor_general_response *)response)->status);
			break;
		case PQI_RESPONSE_IU_TASK_MANAGEMENT:
			io_request->status = pqi_interpret_task_management_response(ctrl_info,
				(void *)response);
			break;
		case PQI_RESPONSE_IU_AIO_PATH_DISABLED:
			pqi_aio_path_disabled(io_request);
			io_request->status = -EAGAIN;
			break;
		case PQI_RESPONSE_IU_RAID_PATH_IO_ERROR:
		case PQI_RESPONSE_IU_AIO_PATH_IO_ERROR:
			io_request->error_info = ctrl_info->error_buffer +
				(get_unaligned_le16(&response->error_index) *
				PQI_ERROR_BUFFER_ELEMENT_LENGTH);
			pqi_process_io_error(response->header.iu_type, io_request);
			break;
		default:
			pqi_invalid_response(ctrl_info, PQI_UNEXPECTED_IU_TYPE);
			dev_err(&ctrl_info->pci_dev->dev,
				"unexpected IU type: 0x%x: producer index: %u  consumer index: %u\n",
				response->header.iu_type, oq_pi, oq_ci);
			return -1;
		}

		io_request->io_complete_callback(io_request, io_request->context);

		/*
		 * Note that the I/O request structure CANNOT BE TOUCHED after
		 * returning from the I/O completion callback!
		 */
		oq_ci = (oq_ci + 1) % ctrl_info->num_elements_per_oq;
	}

	if (num_responses) {
		queue_group->oq_ci_copy = oq_ci;
		writel(oq_ci, queue_group->oq_ci);
	}

	return num_responses;
}

static inline unsigned int pqi_num_elements_free(unsigned int pi,
	unsigned int ci, unsigned int elements_in_queue)
{
	unsigned int num_elements_used;

	if (pi >= ci)
		num_elements_used = pi - ci;
	else
		num_elements_used = elements_in_queue - ci + pi;

	return elements_in_queue - num_elements_used - 1;
}

static void pqi_send_event_ack(struct pqi_ctrl_info *ctrl_info,
	struct pqi_event_acknowledge_request *iu, size_t iu_length)
{
	pqi_index_t iq_pi;
	pqi_index_t iq_ci;
	unsigned long flags;
	void *next_element;
	struct pqi_queue_group *queue_group;

	queue_group = &ctrl_info->queue_groups[PQI_DEFAULT_QUEUE_GROUP];
	put_unaligned_le16(queue_group->oq_id, &iu->header.response_queue_id);

	while (1) {
		spin_lock_irqsave(&queue_group->submit_lock[RAID_PATH], flags);

		iq_pi = queue_group->iq_pi_copy[RAID_PATH];
		iq_ci = readl(queue_group->iq_ci[RAID_PATH]);

		if (pqi_num_elements_free(iq_pi, iq_ci,
			ctrl_info->num_elements_per_iq))
			break;

		spin_unlock_irqrestore(
			&queue_group->submit_lock[RAID_PATH], flags);

		if (pqi_ctrl_offline(ctrl_info))
			return;
	}

	next_element = queue_group->iq_element_array[RAID_PATH] +
		(iq_pi * PQI_OPERATIONAL_IQ_ELEMENT_LENGTH);

	memcpy(next_element, iu, iu_length);

	iq_pi = (iq_pi + 1) % ctrl_info->num_elements_per_iq;
	queue_group->iq_pi_copy[RAID_PATH] = iq_pi;

	/*
	 * This write notifies the controller that an IU is available to be
	 * processed.
	 */
	writel(iq_pi, queue_group->iq_pi[RAID_PATH]);

	spin_unlock_irqrestore(&queue_group->submit_lock[RAID_PATH], flags);
}

static void pqi_acknowledge_event(struct pqi_ctrl_info *ctrl_info,
	struct pqi_event *event)
{
	struct pqi_event_acknowledge_request request;

	memset(&request, 0, sizeof(request));

	request.header.iu_type = PQI_REQUEST_IU_ACKNOWLEDGE_VENDOR_EVENT;
	put_unaligned_le16(sizeof(request) - PQI_REQUEST_HEADER_LENGTH,
		&request.header.iu_length);
	request.event_type = event->event_type;
	put_unaligned_le16(event->event_id, &request.event_id);
	put_unaligned_le32(event->additional_event_id, &request.additional_event_id);

	pqi_send_event_ack(ctrl_info, &request, sizeof(request));
}

#define PQI_SOFT_RESET_STATUS_TIMEOUT_SECS		30
#define PQI_SOFT_RESET_STATUS_POLL_INTERVAL_SECS	1

static enum pqi_soft_reset_status pqi_poll_for_soft_reset_status(
	struct pqi_ctrl_info *ctrl_info)
{
	u8 status;
	unsigned long timeout;

	timeout = (PQI_SOFT_RESET_STATUS_TIMEOUT_SECS * HZ) + jiffies;

	while (1) {
		status = pqi_read_soft_reset_status(ctrl_info);
		if (status & PQI_SOFT_RESET_INITIATE)
			return RESET_INITIATE_DRIVER;

		if (status & PQI_SOFT_RESET_ABORT)
			return RESET_ABORT;

		if (!sis_is_firmware_running(ctrl_info))
			return RESET_NORESPONSE;

		if (time_after(jiffies, timeout)) {
			dev_warn(&ctrl_info->pci_dev->dev,
				"timed out waiting for soft reset status\n");
			return RESET_TIMEDOUT;
		}

		ssleep(PQI_SOFT_RESET_STATUS_POLL_INTERVAL_SECS);
	}
}

static void pqi_process_soft_reset(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	unsigned int delay_secs;
	enum pqi_soft_reset_status reset_status;

	if (ctrl_info->soft_reset_handshake_supported)
		reset_status = pqi_poll_for_soft_reset_status(ctrl_info);
	else
		reset_status = RESET_INITIATE_FIRMWARE;

	delay_secs = PQI_POST_RESET_DELAY_SECS;

	switch (reset_status) {
	case RESET_TIMEDOUT:
		delay_secs = PQI_POST_OFA_RESET_DELAY_UPON_TIMEOUT_SECS;
		fallthrough;
	case RESET_INITIATE_DRIVER:
		dev_info(&ctrl_info->pci_dev->dev,
				"Online Firmware Activation: resetting controller\n");
		sis_soft_reset(ctrl_info);
		fallthrough;
	case RESET_INITIATE_FIRMWARE:
		ctrl_info->pqi_mode_enabled = false;
		pqi_save_ctrl_mode(ctrl_info, SIS_MODE);
		rc = pqi_ofa_ctrl_restart(ctrl_info, delay_secs);
		pqi_host_free_buffer(ctrl_info, &ctrl_info->ofa_memory);
		pqi_ctrl_ofa_done(ctrl_info);
		dev_info(&ctrl_info->pci_dev->dev,
				"Online Firmware Activation: %s\n",
				rc == 0 ? "SUCCESS" : "FAILED");
		break;
	case RESET_ABORT:
		dev_info(&ctrl_info->pci_dev->dev,
				"Online Firmware Activation ABORTED\n");
		if (ctrl_info->soft_reset_handshake_supported)
			pqi_clear_soft_reset_status(ctrl_info);
		pqi_host_free_buffer(ctrl_info, &ctrl_info->ofa_memory);
		pqi_ctrl_ofa_done(ctrl_info);
		pqi_ofa_ctrl_unquiesce(ctrl_info);
		break;
	case RESET_NORESPONSE:
		fallthrough;
	default:
		dev_err(&ctrl_info->pci_dev->dev,
			"unexpected Online Firmware Activation reset status: 0x%x\n",
			reset_status);
		pqi_host_free_buffer(ctrl_info, &ctrl_info->ofa_memory);
		pqi_ctrl_ofa_done(ctrl_info);
		pqi_ofa_ctrl_unquiesce(ctrl_info);
		pqi_take_ctrl_offline(ctrl_info, PQI_OFA_RESPONSE_TIMEOUT);
		break;
	}
}

static void pqi_ofa_memory_alloc_worker(struct work_struct *work)
{
	struct pqi_ctrl_info *ctrl_info;

	ctrl_info = container_of(work, struct pqi_ctrl_info, ofa_memory_alloc_work);

	pqi_ctrl_ofa_start(ctrl_info);
	pqi_host_setup_buffer(ctrl_info, &ctrl_info->ofa_memory, ctrl_info->ofa_bytes_requested, ctrl_info->ofa_bytes_requested);
	pqi_host_memory_update(ctrl_info, &ctrl_info->ofa_memory, PQI_VENDOR_GENERAL_OFA_MEMORY_UPDATE);
}

static void pqi_ofa_quiesce_worker(struct work_struct *work)
{
	struct pqi_ctrl_info *ctrl_info;
	struct pqi_event *event;

	ctrl_info = container_of(work, struct pqi_ctrl_info, ofa_quiesce_work);

	event = &ctrl_info->events[pqi_event_type_to_event_index(PQI_EVENT_TYPE_OFA)];

	pqi_ofa_ctrl_quiesce(ctrl_info);
	pqi_acknowledge_event(ctrl_info, event);
	pqi_process_soft_reset(ctrl_info);
}

static bool pqi_ofa_process_event(struct pqi_ctrl_info *ctrl_info,
	struct pqi_event *event)
{
	bool ack_event;

	ack_event = true;

	switch (event->event_id) {
	case PQI_EVENT_OFA_MEMORY_ALLOCATION:
		dev_info(&ctrl_info->pci_dev->dev,
			"received Online Firmware Activation memory allocation request\n");
		schedule_work(&ctrl_info->ofa_memory_alloc_work);
		break;
	case PQI_EVENT_OFA_QUIESCE:
		dev_info(&ctrl_info->pci_dev->dev,
			"received Online Firmware Activation quiesce request\n");
		schedule_work(&ctrl_info->ofa_quiesce_work);
		ack_event = false;
		break;
	case PQI_EVENT_OFA_CANCELED:
		dev_info(&ctrl_info->pci_dev->dev,
			"received Online Firmware Activation cancel request: reason: %u\n",
			ctrl_info->ofa_cancel_reason);
		pqi_host_free_buffer(ctrl_info, &ctrl_info->ofa_memory);
		pqi_ctrl_ofa_done(ctrl_info);
		break;
	default:
		dev_err(&ctrl_info->pci_dev->dev,
			"received unknown Online Firmware Activation request: event ID: %u\n",
			event->event_id);
		break;
	}

	return ack_event;
}

static void pqi_mark_volumes_for_rescan(struct pqi_ctrl_info *ctrl_info)
{
	unsigned long flags;
	struct pqi_scsi_dev *device;

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	list_for_each_entry(device, &ctrl_info->scsi_device_list, scsi_device_list_entry) {
		if (pqi_is_logical_device(device) && device->devtype == TYPE_DISK)
			device->rescan = true;
	}

	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
}

static void pqi_disable_raid_bypass(struct pqi_ctrl_info *ctrl_info)
{
	unsigned long flags;
	struct pqi_scsi_dev *device;

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	list_for_each_entry(device, &ctrl_info->scsi_device_list, scsi_device_list_entry)
		if (device->raid_bypass_enabled)
			device->raid_bypass_enabled = false;

	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
}

static void pqi_event_worker(struct work_struct *work)
{
	unsigned int i;
	bool rescan_needed;
	struct pqi_ctrl_info *ctrl_info;
	struct pqi_event *event;
	bool ack_event;

	ctrl_info = container_of(work, struct pqi_ctrl_info, event_work);

	pqi_ctrl_busy(ctrl_info);
	pqi_wait_if_ctrl_blocked(ctrl_info);
	if (pqi_ctrl_offline(ctrl_info))
		goto out;

	rescan_needed = false;
	event = ctrl_info->events;
	for (i = 0; i < PQI_NUM_SUPPORTED_EVENTS; i++) {
		if (event->pending) {
			event->pending = false;
			if (event->event_type == PQI_EVENT_TYPE_OFA) {
				ack_event = pqi_ofa_process_event(ctrl_info, event);
			} else {
				ack_event = true;
				rescan_needed = true;
				if (event->event_type == PQI_EVENT_TYPE_LOGICAL_DEVICE)
					pqi_mark_volumes_for_rescan(ctrl_info);
				else if (event->event_type == PQI_EVENT_TYPE_AIO_STATE_CHANGE)
					pqi_disable_raid_bypass(ctrl_info);
			}
			if (ack_event)
				pqi_acknowledge_event(ctrl_info, event);
		}
		event++;
	}

#define PQI_RESCAN_WORK_FOR_EVENT_DELAY		(5 * HZ)

	if (rescan_needed)
		pqi_schedule_rescan_worker_with_delay(ctrl_info,
			PQI_RESCAN_WORK_FOR_EVENT_DELAY);

out:
	pqi_ctrl_unbusy(ctrl_info);
}

#define PQI_HEARTBEAT_TIMER_INTERVAL	(10 * HZ)

static void pqi_heartbeat_timer_handler(struct timer_list *t)
{
	int num_interrupts;
	u32 heartbeat_count;
	struct pqi_ctrl_info *ctrl_info = from_timer(ctrl_info, t, heartbeat_timer);

	pqi_check_ctrl_health(ctrl_info);
	if (pqi_ctrl_offline(ctrl_info))
		return;

	num_interrupts = atomic_read(&ctrl_info->num_interrupts);
	heartbeat_count = pqi_read_heartbeat_counter(ctrl_info);

	if (num_interrupts == ctrl_info->previous_num_interrupts) {
		if (heartbeat_count == ctrl_info->previous_heartbeat_count) {
			dev_err(&ctrl_info->pci_dev->dev,
				"no heartbeat detected - last heartbeat count: %u\n",
				heartbeat_count);
			pqi_take_ctrl_offline(ctrl_info, PQI_NO_HEARTBEAT);
			return;
		}
	} else {
		ctrl_info->previous_num_interrupts = num_interrupts;
	}

	ctrl_info->previous_heartbeat_count = heartbeat_count;
	mod_timer(&ctrl_info->heartbeat_timer,
		jiffies + PQI_HEARTBEAT_TIMER_INTERVAL);
}

static void pqi_start_heartbeat_timer(struct pqi_ctrl_info *ctrl_info)
{
	if (!ctrl_info->heartbeat_counter)
		return;

	ctrl_info->previous_num_interrupts =
		atomic_read(&ctrl_info->num_interrupts);
	ctrl_info->previous_heartbeat_count =
		pqi_read_heartbeat_counter(ctrl_info);

	ctrl_info->heartbeat_timer.expires =
		jiffies + PQI_HEARTBEAT_TIMER_INTERVAL;
	add_timer(&ctrl_info->heartbeat_timer);
}

static inline void pqi_stop_heartbeat_timer(struct pqi_ctrl_info *ctrl_info)
{
	del_timer_sync(&ctrl_info->heartbeat_timer);
}

static void pqi_ofa_capture_event_payload(struct pqi_ctrl_info *ctrl_info,
	struct pqi_event *event, struct pqi_event_response *response)
{
	switch (event->event_id) {
	case PQI_EVENT_OFA_MEMORY_ALLOCATION:
		ctrl_info->ofa_bytes_requested =
			get_unaligned_le32(&response->data.ofa_memory_allocation.bytes_requested);
		break;
	case PQI_EVENT_OFA_CANCELED:
		ctrl_info->ofa_cancel_reason =
			get_unaligned_le16(&response->data.ofa_cancelled.reason);
		break;
	}
}

static int pqi_process_event_intr(struct pqi_ctrl_info *ctrl_info)
{
	int num_events;
	pqi_index_t oq_pi;
	pqi_index_t oq_ci;
	struct pqi_event_queue *event_queue;
	struct pqi_event_response *response;
	struct pqi_event *event;
	int event_index;

	event_queue = &ctrl_info->event_queue;
	num_events = 0;
	oq_ci = event_queue->oq_ci_copy;

	while (1) {
		oq_pi = readl(event_queue->oq_pi);
		if (oq_pi >= PQI_NUM_EVENT_QUEUE_ELEMENTS) {
			pqi_invalid_response(ctrl_info, PQI_EVENT_PI_OUT_OF_RANGE);
			dev_err(&ctrl_info->pci_dev->dev,
				"event interrupt: producer index (%u) out of range (0-%u): consumer index: %u\n",
				oq_pi, PQI_NUM_EVENT_QUEUE_ELEMENTS - 1, oq_ci);
			return -1;
		}

		if (oq_pi == oq_ci)
			break;

		num_events++;
		response = event_queue->oq_element_array + (oq_ci * PQI_EVENT_OQ_ELEMENT_LENGTH);

		event_index = pqi_event_type_to_event_index(response->event_type);

		if (event_index >= 0 && response->request_acknowledge) {
			event = &ctrl_info->events[event_index];
			event->pending = true;
			event->event_type = response->event_type;
			event->event_id = get_unaligned_le16(&response->event_id);
			event->additional_event_id =
				get_unaligned_le32(&response->additional_event_id);
			if (event->event_type == PQI_EVENT_TYPE_OFA)
				pqi_ofa_capture_event_payload(ctrl_info, event, response);
		}

		oq_ci = (oq_ci + 1) % PQI_NUM_EVENT_QUEUE_ELEMENTS;
	}

	if (num_events) {
		event_queue->oq_ci_copy = oq_ci;
		writel(oq_ci, event_queue->oq_ci);
		schedule_work(&ctrl_info->event_work);
	}

	return num_events;
}

#define PQI_LEGACY_INTX_MASK	0x1

static inline void pqi_configure_legacy_intx(struct pqi_ctrl_info *ctrl_info, bool enable_intx)
{
	u32 intx_mask;
	struct pqi_device_registers __iomem *pqi_registers;
	volatile void __iomem *register_addr;

	pqi_registers = ctrl_info->pqi_registers;

	if (enable_intx)
		register_addr = &pqi_registers->legacy_intx_mask_clear;
	else
		register_addr = &pqi_registers->legacy_intx_mask_set;

	intx_mask = readl(register_addr);
	intx_mask |= PQI_LEGACY_INTX_MASK;
	writel(intx_mask, register_addr);
}

static void pqi_change_irq_mode(struct pqi_ctrl_info *ctrl_info,
	enum pqi_irq_mode new_mode)
{
	switch (ctrl_info->irq_mode) {
	case IRQ_MODE_MSIX:
		switch (new_mode) {
		case IRQ_MODE_MSIX:
			break;
		case IRQ_MODE_INTX:
			pqi_configure_legacy_intx(ctrl_info, true);
			sis_enable_intx(ctrl_info);
			break;
		case IRQ_MODE_NONE:
			break;
		}
		break;
	case IRQ_MODE_INTX:
		switch (new_mode) {
		case IRQ_MODE_MSIX:
			pqi_configure_legacy_intx(ctrl_info, false);
			sis_enable_msix(ctrl_info);
			break;
		case IRQ_MODE_INTX:
			break;
		case IRQ_MODE_NONE:
			pqi_configure_legacy_intx(ctrl_info, false);
			break;
		}
		break;
	case IRQ_MODE_NONE:
		switch (new_mode) {
		case IRQ_MODE_MSIX:
			sis_enable_msix(ctrl_info);
			break;
		case IRQ_MODE_INTX:
			pqi_configure_legacy_intx(ctrl_info, true);
			sis_enable_intx(ctrl_info);
			break;
		case IRQ_MODE_NONE:
			break;
		}
		break;
	}

	ctrl_info->irq_mode = new_mode;
}

#define PQI_LEGACY_INTX_PENDING		0x1

static inline bool pqi_is_valid_irq(struct pqi_ctrl_info *ctrl_info)
{
	bool valid_irq;
	u32 intx_status;

	switch (ctrl_info->irq_mode) {
	case IRQ_MODE_MSIX:
		valid_irq = true;
		break;
	case IRQ_MODE_INTX:
		intx_status = readl(&ctrl_info->pqi_registers->legacy_intx_status);
		if (intx_status & PQI_LEGACY_INTX_PENDING)
			valid_irq = true;
		else
			valid_irq = false;
		break;
	case IRQ_MODE_NONE:
	default:
		valid_irq = false;
		break;
	}

	return valid_irq;
}

static irqreturn_t pqi_irq_handler(int irq, void *data)
{
	struct pqi_ctrl_info *ctrl_info;
	struct pqi_queue_group *queue_group;
	int num_io_responses_handled;
	int num_events_handled;

	queue_group = data;
	ctrl_info = queue_group->ctrl_info;

	if (!pqi_is_valid_irq(ctrl_info))
		return IRQ_NONE;

	num_io_responses_handled = pqi_process_io_intr(ctrl_info, queue_group);
	if (num_io_responses_handled < 0)
		goto out;

	if (irq == ctrl_info->event_irq) {
		num_events_handled = pqi_process_event_intr(ctrl_info);
		if (num_events_handled < 0)
			goto out;
	} else {
		num_events_handled = 0;
	}

	if (num_io_responses_handled + num_events_handled > 0)
		atomic_inc(&ctrl_info->num_interrupts);

	pqi_start_io(ctrl_info, queue_group, RAID_PATH, NULL);
	pqi_start_io(ctrl_info, queue_group, AIO_PATH, NULL);

out:
	return IRQ_HANDLED;
}

static int pqi_request_irqs(struct pqi_ctrl_info *ctrl_info)
{
	struct pci_dev *pci_dev = ctrl_info->pci_dev;
	int i;
	int rc;

	ctrl_info->event_irq = pci_irq_vector(pci_dev, 0);

	for (i = 0; i < ctrl_info->num_msix_vectors_enabled; i++) {
		rc = request_irq(pci_irq_vector(pci_dev, i), pqi_irq_handler, 0,
			DRIVER_NAME_SHORT, &ctrl_info->queue_groups[i]);
		if (rc) {
			dev_err(&pci_dev->dev,
				"irq %u init failed with error %d\n",
				pci_irq_vector(pci_dev, i), rc);
			return rc;
		}
		ctrl_info->num_msix_vectors_initialized++;
	}

	return 0;
}

static void pqi_free_irqs(struct pqi_ctrl_info *ctrl_info)
{
	int i;

	for (i = 0; i < ctrl_info->num_msix_vectors_initialized; i++)
		free_irq(pci_irq_vector(ctrl_info->pci_dev, i),
			&ctrl_info->queue_groups[i]);

	ctrl_info->num_msix_vectors_initialized = 0;
}

static int pqi_enable_msix_interrupts(struct pqi_ctrl_info *ctrl_info)
{
	int num_vectors_enabled;
	unsigned int flags = PCI_IRQ_MSIX;

	if (!pqi_disable_managed_interrupts)
		flags |= PCI_IRQ_AFFINITY;

	num_vectors_enabled = pci_alloc_irq_vectors(ctrl_info->pci_dev,
			PQI_MIN_MSIX_VECTORS, ctrl_info->num_queue_groups,
			flags);
	if (num_vectors_enabled < 0) {
		dev_err(&ctrl_info->pci_dev->dev,
			"MSI-X init failed with error %d\n",
			num_vectors_enabled);
		return num_vectors_enabled;
	}

	ctrl_info->num_msix_vectors_enabled = num_vectors_enabled;
	ctrl_info->irq_mode = IRQ_MODE_MSIX;
	return 0;
}

static void pqi_disable_msix_interrupts(struct pqi_ctrl_info *ctrl_info)
{
	if (ctrl_info->num_msix_vectors_enabled) {
		pci_free_irq_vectors(ctrl_info->pci_dev);
		ctrl_info->num_msix_vectors_enabled = 0;
	}
}

static int pqi_alloc_operational_queues(struct pqi_ctrl_info *ctrl_info)
{
	unsigned int i;
	size_t alloc_length;
	size_t element_array_length_per_iq;
	size_t element_array_length_per_oq;
	void *element_array;
	void __iomem *next_queue_index;
	void *aligned_pointer;
	unsigned int num_inbound_queues;
	unsigned int num_outbound_queues;
	unsigned int num_queue_indexes;
	struct pqi_queue_group *queue_group;

	element_array_length_per_iq =
		PQI_OPERATIONAL_IQ_ELEMENT_LENGTH *
		ctrl_info->num_elements_per_iq;
	element_array_length_per_oq =
		PQI_OPERATIONAL_OQ_ELEMENT_LENGTH *
		ctrl_info->num_elements_per_oq;
	num_inbound_queues = ctrl_info->num_queue_groups * 2;
	num_outbound_queues = ctrl_info->num_queue_groups;
	num_queue_indexes = (ctrl_info->num_queue_groups * 3) + 1;

	aligned_pointer = NULL;

	for (i = 0; i < num_inbound_queues; i++) {
		aligned_pointer = PTR_ALIGN(aligned_pointer,
			PQI_QUEUE_ELEMENT_ARRAY_ALIGNMENT);
		aligned_pointer += element_array_length_per_iq;
	}

	for (i = 0; i < num_outbound_queues; i++) {
		aligned_pointer = PTR_ALIGN(aligned_pointer,
			PQI_QUEUE_ELEMENT_ARRAY_ALIGNMENT);
		aligned_pointer += element_array_length_per_oq;
	}

	aligned_pointer = PTR_ALIGN(aligned_pointer,
		PQI_QUEUE_ELEMENT_ARRAY_ALIGNMENT);
	aligned_pointer += PQI_NUM_EVENT_QUEUE_ELEMENTS *
		PQI_EVENT_OQ_ELEMENT_LENGTH;

	for (i = 0; i < num_queue_indexes; i++) {
		aligned_pointer = PTR_ALIGN(aligned_pointer,
			PQI_OPERATIONAL_INDEX_ALIGNMENT);
		aligned_pointer += sizeof(pqi_index_t);
	}

	alloc_length = (size_t)aligned_pointer +
		PQI_QUEUE_ELEMENT_ARRAY_ALIGNMENT;

	alloc_length += PQI_EXTRA_SGL_MEMORY;

	ctrl_info->queue_memory_base =
		dma_alloc_coherent(&ctrl_info->pci_dev->dev, alloc_length,
				   &ctrl_info->queue_memory_base_dma_handle,
				   GFP_KERNEL);

	if (!ctrl_info->queue_memory_base)
		return -ENOMEM;

	ctrl_info->queue_memory_length = alloc_length;

	element_array = PTR_ALIGN(ctrl_info->queue_memory_base,
		PQI_QUEUE_ELEMENT_ARRAY_ALIGNMENT);

	for (i = 0; i < ctrl_info->num_queue_groups; i++) {
		queue_group = &ctrl_info->queue_groups[i];
		queue_group->iq_element_array[RAID_PATH] = element_array;
		queue_group->iq_element_array_bus_addr[RAID_PATH] =
			ctrl_info->queue_memory_base_dma_handle +
				(element_array - ctrl_info->queue_memory_base);
		element_array += element_array_length_per_iq;
		element_array = PTR_ALIGN(element_array,
			PQI_QUEUE_ELEMENT_ARRAY_ALIGNMENT);
		queue_group->iq_element_array[AIO_PATH] = element_array;
		queue_group->iq_element_array_bus_addr[AIO_PATH] =
			ctrl_info->queue_memory_base_dma_handle +
			(element_array - ctrl_info->queue_memory_base);
		element_array += element_array_length_per_iq;
		element_array = PTR_ALIGN(element_array,
			PQI_QUEUE_ELEMENT_ARRAY_ALIGNMENT);
	}

	for (i = 0; i < ctrl_info->num_queue_groups; i++) {
		queue_group = &ctrl_info->queue_groups[i];
		queue_group->oq_element_array = element_array;
		queue_group->oq_element_array_bus_addr =
			ctrl_info->queue_memory_base_dma_handle +
			(element_array - ctrl_info->queue_memory_base);
		element_array += element_array_length_per_oq;
		element_array = PTR_ALIGN(element_array,
			PQI_QUEUE_ELEMENT_ARRAY_ALIGNMENT);
	}

	ctrl_info->event_queue.oq_element_array = element_array;
	ctrl_info->event_queue.oq_element_array_bus_addr =
		ctrl_info->queue_memory_base_dma_handle +
		(element_array - ctrl_info->queue_memory_base);
	element_array += PQI_NUM_EVENT_QUEUE_ELEMENTS *
		PQI_EVENT_OQ_ELEMENT_LENGTH;

	next_queue_index = (void __iomem *)PTR_ALIGN(element_array,
		PQI_OPERATIONAL_INDEX_ALIGNMENT);

	for (i = 0; i < ctrl_info->num_queue_groups; i++) {
		queue_group = &ctrl_info->queue_groups[i];
		queue_group->iq_ci[RAID_PATH] = next_queue_index;
		queue_group->iq_ci_bus_addr[RAID_PATH] =
			ctrl_info->queue_memory_base_dma_handle +
			(next_queue_index -
			(void __iomem *)ctrl_info->queue_memory_base);
		next_queue_index += sizeof(pqi_index_t);
		next_queue_index = PTR_ALIGN(next_queue_index,
			PQI_OPERATIONAL_INDEX_ALIGNMENT);
		queue_group->iq_ci[AIO_PATH] = next_queue_index;
		queue_group->iq_ci_bus_addr[AIO_PATH] =
			ctrl_info->queue_memory_base_dma_handle +
			(next_queue_index -
			(void __iomem *)ctrl_info->queue_memory_base);
		next_queue_index += sizeof(pqi_index_t);
		next_queue_index = PTR_ALIGN(next_queue_index,
			PQI_OPERATIONAL_INDEX_ALIGNMENT);
		queue_group->oq_pi = next_queue_index;
		queue_group->oq_pi_bus_addr =
			ctrl_info->queue_memory_base_dma_handle +
			(next_queue_index -
			(void __iomem *)ctrl_info->queue_memory_base);
		next_queue_index += sizeof(pqi_index_t);
		next_queue_index = PTR_ALIGN(next_queue_index,
			PQI_OPERATIONAL_INDEX_ALIGNMENT);
	}

	ctrl_info->event_queue.oq_pi = next_queue_index;
	ctrl_info->event_queue.oq_pi_bus_addr =
		ctrl_info->queue_memory_base_dma_handle +
		(next_queue_index -
		(void __iomem *)ctrl_info->queue_memory_base);

	return 0;
}

static void pqi_init_operational_queues(struct pqi_ctrl_info *ctrl_info)
{
	unsigned int i;
	u16 next_iq_id = PQI_MIN_OPERATIONAL_QUEUE_ID;
	u16 next_oq_id = PQI_MIN_OPERATIONAL_QUEUE_ID;

	/*
	 * Initialize the backpointers to the controller structure in
	 * each operational queue group structure.
	 */
	for (i = 0; i < ctrl_info->num_queue_groups; i++)
		ctrl_info->queue_groups[i].ctrl_info = ctrl_info;

	/*
	 * Assign IDs to all operational queues.  Note that the IDs
	 * assigned to operational IQs are independent of the IDs
	 * assigned to operational OQs.
	 */
	ctrl_info->event_queue.oq_id = next_oq_id++;
	for (i = 0; i < ctrl_info->num_queue_groups; i++) {
		ctrl_info->queue_groups[i].iq_id[RAID_PATH] = next_iq_id++;
		ctrl_info->queue_groups[i].iq_id[AIO_PATH] = next_iq_id++;
		ctrl_info->queue_groups[i].oq_id = next_oq_id++;
	}

	/*
	 * Assign MSI-X table entry indexes to all queues.  Note that the
	 * interrupt for the event queue is shared with the first queue group.
	 */
	ctrl_info->event_queue.int_msg_num = 0;
	for (i = 0; i < ctrl_info->num_queue_groups; i++)
		ctrl_info->queue_groups[i].int_msg_num = i;

	for (i = 0; i < ctrl_info->num_queue_groups; i++) {
		spin_lock_init(&ctrl_info->queue_groups[i].submit_lock[0]);
		spin_lock_init(&ctrl_info->queue_groups[i].submit_lock[1]);
		INIT_LIST_HEAD(&ctrl_info->queue_groups[i].request_list[0]);
		INIT_LIST_HEAD(&ctrl_info->queue_groups[i].request_list[1]);
	}
}

static int pqi_alloc_admin_queues(struct pqi_ctrl_info *ctrl_info)
{
	size_t alloc_length;
	struct pqi_admin_queues_aligned *admin_queues_aligned;
	struct pqi_admin_queues *admin_queues;

	alloc_length = sizeof(struct pqi_admin_queues_aligned) +
		PQI_QUEUE_ELEMENT_ARRAY_ALIGNMENT;

	ctrl_info->admin_queue_memory_base =
		dma_alloc_coherent(&ctrl_info->pci_dev->dev, alloc_length,
				   &ctrl_info->admin_queue_memory_base_dma_handle,
				   GFP_KERNEL);

	if (!ctrl_info->admin_queue_memory_base)
		return -ENOMEM;

	ctrl_info->admin_queue_memory_length = alloc_length;

	admin_queues = &ctrl_info->admin_queues;
	admin_queues_aligned = PTR_ALIGN(ctrl_info->admin_queue_memory_base,
		PQI_QUEUE_ELEMENT_ARRAY_ALIGNMENT);
	admin_queues->iq_element_array =
		&admin_queues_aligned->iq_element_array;
	admin_queues->oq_element_array =
		&admin_queues_aligned->oq_element_array;
	admin_queues->iq_ci =
		(pqi_index_t __iomem *)&admin_queues_aligned->iq_ci;
	admin_queues->oq_pi =
		(pqi_index_t __iomem *)&admin_queues_aligned->oq_pi;

	admin_queues->iq_element_array_bus_addr =
		ctrl_info->admin_queue_memory_base_dma_handle +
		(admin_queues->iq_element_array -
		ctrl_info->admin_queue_memory_base);
	admin_queues->oq_element_array_bus_addr =
		ctrl_info->admin_queue_memory_base_dma_handle +
		(admin_queues->oq_element_array -
		ctrl_info->admin_queue_memory_base);
	admin_queues->iq_ci_bus_addr =
		ctrl_info->admin_queue_memory_base_dma_handle +
		((void __iomem *)admin_queues->iq_ci -
		(void __iomem *)ctrl_info->admin_queue_memory_base);
	admin_queues->oq_pi_bus_addr =
		ctrl_info->admin_queue_memory_base_dma_handle +
		((void __iomem *)admin_queues->oq_pi -
		(void __iomem *)ctrl_info->admin_queue_memory_base);

	return 0;
}

#define PQI_ADMIN_QUEUE_CREATE_TIMEOUT_JIFFIES		HZ
#define PQI_ADMIN_QUEUE_CREATE_POLL_INTERVAL_MSECS	1

static int pqi_create_admin_queues(struct pqi_ctrl_info *ctrl_info)
{
	struct pqi_device_registers __iomem *pqi_registers;
	struct pqi_admin_queues *admin_queues;
	unsigned long timeout;
	u8 status;
	u32 reg;

	pqi_registers = ctrl_info->pqi_registers;
	admin_queues = &ctrl_info->admin_queues;

	writeq((u64)admin_queues->iq_element_array_bus_addr,
		&pqi_registers->admin_iq_element_array_addr);
	writeq((u64)admin_queues->oq_element_array_bus_addr,
		&pqi_registers->admin_oq_element_array_addr);
	writeq((u64)admin_queues->iq_ci_bus_addr,
		&pqi_registers->admin_iq_ci_addr);
	writeq((u64)admin_queues->oq_pi_bus_addr,
		&pqi_registers->admin_oq_pi_addr);

	reg = PQI_ADMIN_IQ_NUM_ELEMENTS |
		(PQI_ADMIN_OQ_NUM_ELEMENTS << 8) |
		(admin_queues->int_msg_num << 16);
	writel(reg, &pqi_registers->admin_iq_num_elements);

	writel(PQI_CREATE_ADMIN_QUEUE_PAIR,
		&pqi_registers->function_and_status_code);

	timeout = PQI_ADMIN_QUEUE_CREATE_TIMEOUT_JIFFIES + jiffies;
	while (1) {
		msleep(PQI_ADMIN_QUEUE_CREATE_POLL_INTERVAL_MSECS);
		status = readb(&pqi_registers->function_and_status_code);
		if (status == PQI_STATUS_IDLE)
			break;
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
	}

	/*
	 * The offset registers are not initialized to the correct
	 * offsets until *after* the create admin queue pair command
	 * completes successfully.
	 */
	admin_queues->iq_pi = ctrl_info->iomem_base +
		PQI_DEVICE_REGISTERS_OFFSET +
		readq(&pqi_registers->admin_iq_pi_offset);
	admin_queues->oq_ci = ctrl_info->iomem_base +
		PQI_DEVICE_REGISTERS_OFFSET +
		readq(&pqi_registers->admin_oq_ci_offset);

	return 0;
}

static void pqi_submit_admin_request(struct pqi_ctrl_info *ctrl_info,
	struct pqi_general_admin_request *request)
{
	struct pqi_admin_queues *admin_queues;
	void *next_element;
	pqi_index_t iq_pi;

	admin_queues = &ctrl_info->admin_queues;
	iq_pi = admin_queues->iq_pi_copy;

	next_element = admin_queues->iq_element_array +
		(iq_pi * PQI_ADMIN_IQ_ELEMENT_LENGTH);

	memcpy(next_element, request, sizeof(*request));

	iq_pi = (iq_pi + 1) % PQI_ADMIN_IQ_NUM_ELEMENTS;
	admin_queues->iq_pi_copy = iq_pi;

	/*
	 * This write notifies the controller that an IU is available to be
	 * processed.
	 */
	writel(iq_pi, admin_queues->iq_pi);
}

#define PQI_ADMIN_REQUEST_TIMEOUT_SECS	60

static int pqi_poll_for_admin_response(struct pqi_ctrl_info *ctrl_info,
	struct pqi_general_admin_response *response)
{
	struct pqi_admin_queues *admin_queues;
	pqi_index_t oq_pi;
	pqi_index_t oq_ci;
	unsigned long timeout;

	admin_queues = &ctrl_info->admin_queues;
	oq_ci = admin_queues->oq_ci_copy;

	timeout = (PQI_ADMIN_REQUEST_TIMEOUT_SECS * HZ) + jiffies;

	while (1) {
		oq_pi = readl(admin_queues->oq_pi);
		if (oq_pi != oq_ci)
			break;
		if (time_after(jiffies, timeout)) {
			dev_err(&ctrl_info->pci_dev->dev,
				"timed out waiting for admin response\n");
			return -ETIMEDOUT;
		}
		if (!sis_is_firmware_running(ctrl_info))
			return -ENXIO;
		usleep_range(1000, 2000);
	}

	memcpy(response, admin_queues->oq_element_array +
		(oq_ci * PQI_ADMIN_OQ_ELEMENT_LENGTH), sizeof(*response));

	oq_ci = (oq_ci + 1) % PQI_ADMIN_OQ_NUM_ELEMENTS;
	admin_queues->oq_ci_copy = oq_ci;
	writel(oq_ci, admin_queues->oq_ci);

	return 0;
}

static void pqi_start_io(struct pqi_ctrl_info *ctrl_info,
	struct pqi_queue_group *queue_group, enum pqi_io_path path,
	struct pqi_io_request *io_request)
{
	struct pqi_io_request *next;
	void *next_element;
	pqi_index_t iq_pi;
	pqi_index_t iq_ci;
	size_t iu_length;
	unsigned long flags;
	unsigned int num_elements_needed;
	unsigned int num_elements_to_end_of_queue;
	size_t copy_count;
	struct pqi_iu_header *request;

	spin_lock_irqsave(&queue_group->submit_lock[path], flags);

	if (io_request) {
		io_request->queue_group = queue_group;
		list_add_tail(&io_request->request_list_entry,
			&queue_group->request_list[path]);
	}

	iq_pi = queue_group->iq_pi_copy[path];

	list_for_each_entry_safe(io_request, next,
		&queue_group->request_list[path], request_list_entry) {

		request = io_request->iu;

		iu_length = get_unaligned_le16(&request->iu_length) +
			PQI_REQUEST_HEADER_LENGTH;
		num_elements_needed =
			DIV_ROUND_UP(iu_length,
				PQI_OPERATIONAL_IQ_ELEMENT_LENGTH);

		iq_ci = readl(queue_group->iq_ci[path]);

		if (num_elements_needed > pqi_num_elements_free(iq_pi, iq_ci,
			ctrl_info->num_elements_per_iq))
			break;

		put_unaligned_le16(queue_group->oq_id,
			&request->response_queue_id);

		next_element = queue_group->iq_element_array[path] +
			(iq_pi * PQI_OPERATIONAL_IQ_ELEMENT_LENGTH);

		num_elements_to_end_of_queue =
			ctrl_info->num_elements_per_iq - iq_pi;

		if (num_elements_needed <= num_elements_to_end_of_queue) {
			memcpy(next_element, request, iu_length);
		} else {
			copy_count = num_elements_to_end_of_queue *
				PQI_OPERATIONAL_IQ_ELEMENT_LENGTH;
			memcpy(next_element, request, copy_count);
			memcpy(queue_group->iq_element_array[path],
				(u8 *)request + copy_count,
				iu_length - copy_count);
		}

		iq_pi = (iq_pi + num_elements_needed) %
			ctrl_info->num_elements_per_iq;

		list_del(&io_request->request_list_entry);
	}

	if (iq_pi != queue_group->iq_pi_copy[path]) {
		queue_group->iq_pi_copy[path] = iq_pi;
		/*
		 * This write notifies the controller that one or more IUs are
		 * available to be processed.
		 */
		writel(iq_pi, queue_group->iq_pi[path]);
	}

	spin_unlock_irqrestore(&queue_group->submit_lock[path], flags);
}

#define PQI_WAIT_FOR_COMPLETION_IO_TIMEOUT_SECS		10

static int pqi_wait_for_completion_io(struct pqi_ctrl_info *ctrl_info,
	struct completion *wait)
{
	int rc;

	while (1) {
		if (wait_for_completion_io_timeout(wait,
			PQI_WAIT_FOR_COMPLETION_IO_TIMEOUT_SECS * HZ)) {
			rc = 0;
			break;
		}

		pqi_check_ctrl_health(ctrl_info);
		if (pqi_ctrl_offline(ctrl_info)) {
			rc = -ENXIO;
			break;
		}
	}

	return rc;
}

static void pqi_raid_synchronous_complete(struct pqi_io_request *io_request,
	void *context)
{
	struct completion *waiting = context;

	complete(waiting);
}

static int pqi_process_raid_io_error_synchronous(
	struct pqi_raid_error_info *error_info)
{
	int rc = -EIO;

	switch (error_info->data_out_result) {
	case PQI_DATA_IN_OUT_GOOD:
		if (error_info->status == SAM_STAT_GOOD)
			rc = 0;
		break;
	case PQI_DATA_IN_OUT_UNDERFLOW:
		if (error_info->status == SAM_STAT_GOOD ||
			error_info->status == SAM_STAT_CHECK_CONDITION)
			rc = 0;
		break;
	case PQI_DATA_IN_OUT_ABORTED:
		rc = PQI_CMD_STATUS_ABORTED;
		break;
	}

	return rc;
}

static inline bool pqi_is_blockable_request(struct pqi_iu_header *request)
{
	return (request->driver_flags & PQI_DRIVER_NONBLOCKABLE_REQUEST) == 0;
}

static int pqi_submit_raid_request_synchronous(struct pqi_ctrl_info *ctrl_info,
	struct pqi_iu_header *request, unsigned int flags,
	struct pqi_raid_error_info *error_info)
{
	int rc = 0;
	struct pqi_io_request *io_request;
	size_t iu_length;
	DECLARE_COMPLETION_ONSTACK(wait);

	if (flags & PQI_SYNC_FLAGS_INTERRUPTABLE) {
		if (down_interruptible(&ctrl_info->sync_request_sem))
			return -ERESTARTSYS;
	} else {
		down(&ctrl_info->sync_request_sem);
	}

	pqi_ctrl_busy(ctrl_info);
	/*
	 * Wait for other admin queue updates such as;
	 * config table changes, OFA memory updates, ...
	 */
	if (pqi_is_blockable_request(request))
		pqi_wait_if_ctrl_blocked(ctrl_info);

	if (pqi_ctrl_offline(ctrl_info)) {
		rc = -ENXIO;
		goto out;
	}

	io_request = pqi_alloc_io_request(ctrl_info, NULL);

	put_unaligned_le16(io_request->index,
		&(((struct pqi_raid_path_request *)request)->request_id));

	if (request->iu_type == PQI_REQUEST_IU_RAID_PATH_IO)
		((struct pqi_raid_path_request *)request)->error_index =
			((struct pqi_raid_path_request *)request)->request_id;

	iu_length = get_unaligned_le16(&request->iu_length) +
		PQI_REQUEST_HEADER_LENGTH;
	memcpy(io_request->iu, request, iu_length);

	io_request->io_complete_callback = pqi_raid_synchronous_complete;
	io_request->context = &wait;

	pqi_start_io(ctrl_info, &ctrl_info->queue_groups[PQI_DEFAULT_QUEUE_GROUP], RAID_PATH,
		io_request);

	pqi_wait_for_completion_io(ctrl_info, &wait);

	if (error_info) {
		if (io_request->error_info)
			memcpy(error_info, io_request->error_info, sizeof(*error_info));
		else
			memset(error_info, 0, sizeof(*error_info));
	} else if (rc == 0 && io_request->error_info) {
		rc = pqi_process_raid_io_error_synchronous(io_request->error_info);
	}

	pqi_free_io_request(io_request);

out:
	pqi_ctrl_unbusy(ctrl_info);
	up(&ctrl_info->sync_request_sem);

	return rc;
}

static int pqi_validate_admin_response(
	struct pqi_general_admin_response *response, u8 expected_function_code)
{
	if (response->header.iu_type != PQI_RESPONSE_IU_GENERAL_ADMIN)
		return -EINVAL;

	if (get_unaligned_le16(&response->header.iu_length) !=
		PQI_GENERAL_ADMIN_IU_LENGTH)
		return -EINVAL;

	if (response->function_code != expected_function_code)
		return -EINVAL;

	if (response->status != PQI_GENERAL_ADMIN_STATUS_SUCCESS)
		return -EINVAL;

	return 0;
}

static int pqi_submit_admin_request_synchronous(
	struct pqi_ctrl_info *ctrl_info,
	struct pqi_general_admin_request *request,
	struct pqi_general_admin_response *response)
{
	int rc;

	pqi_submit_admin_request(ctrl_info, request);

	rc = pqi_poll_for_admin_response(ctrl_info, response);

	if (rc == 0)
		rc = pqi_validate_admin_response(response, request->function_code);

	return rc;
}

static int pqi_report_device_capability(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	struct pqi_general_admin_request request;
	struct pqi_general_admin_response response;
	struct pqi_device_capability *capability;
	struct pqi_iu_layer_descriptor *sop_iu_layer_descriptor;

	capability = kmalloc(sizeof(*capability), GFP_KERNEL);
	if (!capability)
		return -ENOMEM;

	memset(&request, 0, sizeof(request));

	request.header.iu_type = PQI_REQUEST_IU_GENERAL_ADMIN;
	put_unaligned_le16(PQI_GENERAL_ADMIN_IU_LENGTH,
		&request.header.iu_length);
	request.function_code =
		PQI_GENERAL_ADMIN_FUNCTION_REPORT_DEVICE_CAPABILITY;
	put_unaligned_le32(sizeof(*capability),
		&request.data.report_device_capability.buffer_length);

	rc = pqi_map_single(ctrl_info->pci_dev,
		&request.data.report_device_capability.sg_descriptor,
		capability, sizeof(*capability),
		DMA_FROM_DEVICE);
	if (rc)
		goto out;

	rc = pqi_submit_admin_request_synchronous(ctrl_info, &request, &response);

	pqi_pci_unmap(ctrl_info->pci_dev,
		&request.data.report_device_capability.sg_descriptor, 1,
		DMA_FROM_DEVICE);

	if (rc)
		goto out;

	if (response.status != PQI_GENERAL_ADMIN_STATUS_SUCCESS) {
		rc = -EIO;
		goto out;
	}

	ctrl_info->max_inbound_queues =
		get_unaligned_le16(&capability->max_inbound_queues);
	ctrl_info->max_elements_per_iq =
		get_unaligned_le16(&capability->max_elements_per_iq);
	ctrl_info->max_iq_element_length =
		get_unaligned_le16(&capability->max_iq_element_length)
		* 16;
	ctrl_info->max_outbound_queues =
		get_unaligned_le16(&capability->max_outbound_queues);
	ctrl_info->max_elements_per_oq =
		get_unaligned_le16(&capability->max_elements_per_oq);
	ctrl_info->max_oq_element_length =
		get_unaligned_le16(&capability->max_oq_element_length)
		* 16;

	sop_iu_layer_descriptor =
		&capability->iu_layer_descriptors[PQI_PROTOCOL_SOP];

	ctrl_info->max_inbound_iu_length_per_firmware =
		get_unaligned_le16(
			&sop_iu_layer_descriptor->max_inbound_iu_length);
	ctrl_info->inbound_spanning_supported =
		sop_iu_layer_descriptor->inbound_spanning_supported;
	ctrl_info->outbound_spanning_supported =
		sop_iu_layer_descriptor->outbound_spanning_supported;

out:
	kfree(capability);

	return rc;
}

static int pqi_validate_device_capability(struct pqi_ctrl_info *ctrl_info)
{
	if (ctrl_info->max_iq_element_length <
		PQI_OPERATIONAL_IQ_ELEMENT_LENGTH) {
		dev_err(&ctrl_info->pci_dev->dev,
			"max. inbound queue element length of %d is less than the required length of %d\n",
			ctrl_info->max_iq_element_length,
			PQI_OPERATIONAL_IQ_ELEMENT_LENGTH);
		return -EINVAL;
	}

	if (ctrl_info->max_oq_element_length <
		PQI_OPERATIONAL_OQ_ELEMENT_LENGTH) {
		dev_err(&ctrl_info->pci_dev->dev,
			"max. outbound queue element length of %d is less than the required length of %d\n",
			ctrl_info->max_oq_element_length,
			PQI_OPERATIONAL_OQ_ELEMENT_LENGTH);
		return -EINVAL;
	}

	if (ctrl_info->max_inbound_iu_length_per_firmware <
		PQI_OPERATIONAL_IQ_ELEMENT_LENGTH) {
		dev_err(&ctrl_info->pci_dev->dev,
			"max. inbound IU length of %u is less than the min. required length of %d\n",
			ctrl_info->max_inbound_iu_length_per_firmware,
			PQI_OPERATIONAL_IQ_ELEMENT_LENGTH);
		return -EINVAL;
	}

	if (!ctrl_info->inbound_spanning_supported) {
		dev_err(&ctrl_info->pci_dev->dev,
			"the controller does not support inbound spanning\n");
		return -EINVAL;
	}

	if (ctrl_info->outbound_spanning_supported) {
		dev_err(&ctrl_info->pci_dev->dev,
			"the controller supports outbound spanning but this driver does not\n");
		return -EINVAL;
	}

	return 0;
}

static int pqi_create_event_queue(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	struct pqi_event_queue *event_queue;
	struct pqi_general_admin_request request;
	struct pqi_general_admin_response response;

	event_queue = &ctrl_info->event_queue;

	/*
	 * Create OQ (Outbound Queue - device to host queue) to dedicate
	 * to events.
	 */
	memset(&request, 0, sizeof(request));
	request.header.iu_type = PQI_REQUEST_IU_GENERAL_ADMIN;
	put_unaligned_le16(PQI_GENERAL_ADMIN_IU_LENGTH,
		&request.header.iu_length);
	request.function_code = PQI_GENERAL_ADMIN_FUNCTION_CREATE_OQ;
	put_unaligned_le16(event_queue->oq_id,
		&request.data.create_operational_oq.queue_id);
	put_unaligned_le64((u64)event_queue->oq_element_array_bus_addr,
		&request.data.create_operational_oq.element_array_addr);
	put_unaligned_le64((u64)event_queue->oq_pi_bus_addr,
		&request.data.create_operational_oq.pi_addr);
	put_unaligned_le16(PQI_NUM_EVENT_QUEUE_ELEMENTS,
		&request.data.create_operational_oq.num_elements);
	put_unaligned_le16(PQI_EVENT_OQ_ELEMENT_LENGTH / 16,
		&request.data.create_operational_oq.element_length);
	request.data.create_operational_oq.queue_protocol = PQI_PROTOCOL_SOP;
	put_unaligned_le16(event_queue->int_msg_num,
		&request.data.create_operational_oq.int_msg_num);

	rc = pqi_submit_admin_request_synchronous(ctrl_info, &request,
		&response);
	if (rc)
		return rc;

	event_queue->oq_ci = ctrl_info->iomem_base +
		PQI_DEVICE_REGISTERS_OFFSET +
		get_unaligned_le64(
			&response.data.create_operational_oq.oq_ci_offset);

	return 0;
}

static int pqi_create_queue_group(struct pqi_ctrl_info *ctrl_info,
	unsigned int group_number)
{
	int rc;
	struct pqi_queue_group *queue_group;
	struct pqi_general_admin_request request;
	struct pqi_general_admin_response response;

	queue_group = &ctrl_info->queue_groups[group_number];

	/*
	 * Create IQ (Inbound Queue - host to device queue) for
	 * RAID path.
	 */
	memset(&request, 0, sizeof(request));
	request.header.iu_type = PQI_REQUEST_IU_GENERAL_ADMIN;
	put_unaligned_le16(PQI_GENERAL_ADMIN_IU_LENGTH,
		&request.header.iu_length);
	request.function_code = PQI_GENERAL_ADMIN_FUNCTION_CREATE_IQ;
	put_unaligned_le16(queue_group->iq_id[RAID_PATH],
		&request.data.create_operational_iq.queue_id);
	put_unaligned_le64(
		(u64)queue_group->iq_element_array_bus_addr[RAID_PATH],
		&request.data.create_operational_iq.element_array_addr);
	put_unaligned_le64((u64)queue_group->iq_ci_bus_addr[RAID_PATH],
		&request.data.create_operational_iq.ci_addr);
	put_unaligned_le16(ctrl_info->num_elements_per_iq,
		&request.data.create_operational_iq.num_elements);
	put_unaligned_le16(PQI_OPERATIONAL_IQ_ELEMENT_LENGTH / 16,
		&request.data.create_operational_iq.element_length);
	request.data.create_operational_iq.queue_protocol = PQI_PROTOCOL_SOP;

	rc = pqi_submit_admin_request_synchronous(ctrl_info, &request,
		&response);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error creating inbound RAID queue\n");
		return rc;
	}

	queue_group->iq_pi[RAID_PATH] = ctrl_info->iomem_base +
		PQI_DEVICE_REGISTERS_OFFSET +
		get_unaligned_le64(
			&response.data.create_operational_iq.iq_pi_offset);

	/*
	 * Create IQ (Inbound Queue - host to device queue) for
	 * Advanced I/O (AIO) path.
	 */
	memset(&request, 0, sizeof(request));
	request.header.iu_type = PQI_REQUEST_IU_GENERAL_ADMIN;
	put_unaligned_le16(PQI_GENERAL_ADMIN_IU_LENGTH,
		&request.header.iu_length);
	request.function_code = PQI_GENERAL_ADMIN_FUNCTION_CREATE_IQ;
	put_unaligned_le16(queue_group->iq_id[AIO_PATH],
		&request.data.create_operational_iq.queue_id);
	put_unaligned_le64((u64)queue_group->
		iq_element_array_bus_addr[AIO_PATH],
		&request.data.create_operational_iq.element_array_addr);
	put_unaligned_le64((u64)queue_group->iq_ci_bus_addr[AIO_PATH],
		&request.data.create_operational_iq.ci_addr);
	put_unaligned_le16(ctrl_info->num_elements_per_iq,
		&request.data.create_operational_iq.num_elements);
	put_unaligned_le16(PQI_OPERATIONAL_IQ_ELEMENT_LENGTH / 16,
		&request.data.create_operational_iq.element_length);
	request.data.create_operational_iq.queue_protocol = PQI_PROTOCOL_SOP;

	rc = pqi_submit_admin_request_synchronous(ctrl_info, &request,
		&response);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error creating inbound AIO queue\n");
		return rc;
	}

	queue_group->iq_pi[AIO_PATH] = ctrl_info->iomem_base +
		PQI_DEVICE_REGISTERS_OFFSET +
		get_unaligned_le64(
			&response.data.create_operational_iq.iq_pi_offset);

	/*
	 * Designate the 2nd IQ as the AIO path.  By default, all IQs are
	 * assumed to be for RAID path I/O unless we change the queue's
	 * property.
	 */
	memset(&request, 0, sizeof(request));
	request.header.iu_type = PQI_REQUEST_IU_GENERAL_ADMIN;
	put_unaligned_le16(PQI_GENERAL_ADMIN_IU_LENGTH,
		&request.header.iu_length);
	request.function_code = PQI_GENERAL_ADMIN_FUNCTION_CHANGE_IQ_PROPERTY;
	put_unaligned_le16(queue_group->iq_id[AIO_PATH],
		&request.data.change_operational_iq_properties.queue_id);
	put_unaligned_le32(PQI_IQ_PROPERTY_IS_AIO_QUEUE,
		&request.data.change_operational_iq_properties.vendor_specific);

	rc = pqi_submit_admin_request_synchronous(ctrl_info, &request,
		&response);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error changing queue property\n");
		return rc;
	}

	/*
	 * Create OQ (Outbound Queue - device to host queue).
	 */
	memset(&request, 0, sizeof(request));
	request.header.iu_type = PQI_REQUEST_IU_GENERAL_ADMIN;
	put_unaligned_le16(PQI_GENERAL_ADMIN_IU_LENGTH,
		&request.header.iu_length);
	request.function_code = PQI_GENERAL_ADMIN_FUNCTION_CREATE_OQ;
	put_unaligned_le16(queue_group->oq_id,
		&request.data.create_operational_oq.queue_id);
	put_unaligned_le64((u64)queue_group->oq_element_array_bus_addr,
		&request.data.create_operational_oq.element_array_addr);
	put_unaligned_le64((u64)queue_group->oq_pi_bus_addr,
		&request.data.create_operational_oq.pi_addr);
	put_unaligned_le16(ctrl_info->num_elements_per_oq,
		&request.data.create_operational_oq.num_elements);
	put_unaligned_le16(PQI_OPERATIONAL_OQ_ELEMENT_LENGTH / 16,
		&request.data.create_operational_oq.element_length);
	request.data.create_operational_oq.queue_protocol = PQI_PROTOCOL_SOP;
	put_unaligned_le16(queue_group->int_msg_num,
		&request.data.create_operational_oq.int_msg_num);

	rc = pqi_submit_admin_request_synchronous(ctrl_info, &request,
		&response);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error creating outbound queue\n");
		return rc;
	}

	queue_group->oq_ci = ctrl_info->iomem_base +
		PQI_DEVICE_REGISTERS_OFFSET +
		get_unaligned_le64(
			&response.data.create_operational_oq.oq_ci_offset);

	return 0;
}

static int pqi_create_queues(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	unsigned int i;

	rc = pqi_create_event_queue(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error creating event queue\n");
		return rc;
	}

	for (i = 0; i < ctrl_info->num_queue_groups; i++) {
		rc = pqi_create_queue_group(ctrl_info, i);
		if (rc) {
			dev_err(&ctrl_info->pci_dev->dev,
				"error creating queue group number %u/%u\n",
				i, ctrl_info->num_queue_groups);
			return rc;
		}
	}

	return 0;
}

#define PQI_REPORT_EVENT_CONFIG_BUFFER_LENGTH	\
	struct_size_t(struct pqi_event_config,  descriptors, PQI_MAX_EVENT_DESCRIPTORS)

static int pqi_configure_events(struct pqi_ctrl_info *ctrl_info,
	bool enable_events)
{
	int rc;
	unsigned int i;
	struct pqi_event_config *event_config;
	struct pqi_event_descriptor *event_descriptor;
	struct pqi_general_management_request request;

	event_config = kmalloc(PQI_REPORT_EVENT_CONFIG_BUFFER_LENGTH,
		GFP_KERNEL);
	if (!event_config)
		return -ENOMEM;

	memset(&request, 0, sizeof(request));

	request.header.iu_type = PQI_REQUEST_IU_REPORT_VENDOR_EVENT_CONFIG;
	put_unaligned_le16(offsetof(struct pqi_general_management_request,
		data.report_event_configuration.sg_descriptors[1]) -
		PQI_REQUEST_HEADER_LENGTH, &request.header.iu_length);
	put_unaligned_le32(PQI_REPORT_EVENT_CONFIG_BUFFER_LENGTH,
		&request.data.report_event_configuration.buffer_length);

	rc = pqi_map_single(ctrl_info->pci_dev,
		request.data.report_event_configuration.sg_descriptors,
		event_config, PQI_REPORT_EVENT_CONFIG_BUFFER_LENGTH,
		DMA_FROM_DEVICE);
	if (rc)
		goto out;

	rc = pqi_submit_raid_request_synchronous(ctrl_info, &request.header, 0, NULL);

	pqi_pci_unmap(ctrl_info->pci_dev,
		request.data.report_event_configuration.sg_descriptors, 1,
		DMA_FROM_DEVICE);

	if (rc)
		goto out;

	for (i = 0; i < event_config->num_event_descriptors; i++) {
		event_descriptor = &event_config->descriptors[i];
		if (enable_events &&
			pqi_is_supported_event(event_descriptor->event_type))
				put_unaligned_le16(ctrl_info->event_queue.oq_id,
					&event_descriptor->oq_id);
		else
			put_unaligned_le16(0, &event_descriptor->oq_id);
	}

	memset(&request, 0, sizeof(request));

	request.header.iu_type = PQI_REQUEST_IU_SET_VENDOR_EVENT_CONFIG;
	put_unaligned_le16(offsetof(struct pqi_general_management_request,
		data.report_event_configuration.sg_descriptors[1]) -
		PQI_REQUEST_HEADER_LENGTH, &request.header.iu_length);
	put_unaligned_le32(PQI_REPORT_EVENT_CONFIG_BUFFER_LENGTH,
		&request.data.report_event_configuration.buffer_length);

	rc = pqi_map_single(ctrl_info->pci_dev,
		request.data.report_event_configuration.sg_descriptors,
		event_config, PQI_REPORT_EVENT_CONFIG_BUFFER_LENGTH,
		DMA_TO_DEVICE);
	if (rc)
		goto out;

	rc = pqi_submit_raid_request_synchronous(ctrl_info, &request.header, 0, NULL);

	pqi_pci_unmap(ctrl_info->pci_dev,
		request.data.report_event_configuration.sg_descriptors, 1,
		DMA_TO_DEVICE);

out:
	kfree(event_config);

	return rc;
}

static inline int pqi_enable_events(struct pqi_ctrl_info *ctrl_info)
{
	return pqi_configure_events(ctrl_info, true);
}

static void pqi_free_all_io_requests(struct pqi_ctrl_info *ctrl_info)
{
	unsigned int i;
	struct device *dev;
	size_t sg_chain_buffer_length;
	struct pqi_io_request *io_request;

	if (!ctrl_info->io_request_pool)
		return;

	dev = &ctrl_info->pci_dev->dev;
	sg_chain_buffer_length = ctrl_info->sg_chain_buffer_length;
	io_request = ctrl_info->io_request_pool;

	for (i = 0; i < ctrl_info->max_io_slots; i++) {
		kfree(io_request->iu);
		if (!io_request->sg_chain_buffer)
			break;
		dma_free_coherent(dev, sg_chain_buffer_length,
			io_request->sg_chain_buffer,
			io_request->sg_chain_buffer_dma_handle);
		io_request++;
	}

	kfree(ctrl_info->io_request_pool);
	ctrl_info->io_request_pool = NULL;
}

static inline int pqi_alloc_error_buffer(struct pqi_ctrl_info *ctrl_info)
{
	ctrl_info->error_buffer = dma_alloc_coherent(&ctrl_info->pci_dev->dev,
				     ctrl_info->error_buffer_length,
				     &ctrl_info->error_buffer_dma_handle,
				     GFP_KERNEL);
	if (!ctrl_info->error_buffer)
		return -ENOMEM;

	return 0;
}

static int pqi_alloc_io_resources(struct pqi_ctrl_info *ctrl_info)
{
	unsigned int i;
	void *sg_chain_buffer;
	size_t sg_chain_buffer_length;
	dma_addr_t sg_chain_buffer_dma_handle;
	struct device *dev;
	struct pqi_io_request *io_request;

	ctrl_info->io_request_pool = kcalloc(ctrl_info->max_io_slots,
		sizeof(ctrl_info->io_request_pool[0]), GFP_KERNEL);

	if (!ctrl_info->io_request_pool) {
		dev_err(&ctrl_info->pci_dev->dev,
			"failed to allocate I/O request pool\n");
		goto error;
	}

	dev = &ctrl_info->pci_dev->dev;
	sg_chain_buffer_length = ctrl_info->sg_chain_buffer_length;
	io_request = ctrl_info->io_request_pool;

	for (i = 0; i < ctrl_info->max_io_slots; i++) {
		io_request->iu = kmalloc(ctrl_info->max_inbound_iu_length, GFP_KERNEL);

		if (!io_request->iu) {
			dev_err(&ctrl_info->pci_dev->dev,
				"failed to allocate IU buffers\n");
			goto error;
		}

		sg_chain_buffer = dma_alloc_coherent(dev,
			sg_chain_buffer_length, &sg_chain_buffer_dma_handle,
			GFP_KERNEL);

		if (!sg_chain_buffer) {
			dev_err(&ctrl_info->pci_dev->dev,
				"failed to allocate PQI scatter-gather chain buffers\n");
			goto error;
		}

		io_request->index = i;
		io_request->sg_chain_buffer = sg_chain_buffer;
		io_request->sg_chain_buffer_dma_handle = sg_chain_buffer_dma_handle;
		io_request++;
	}

	return 0;

error:
	pqi_free_all_io_requests(ctrl_info);

	return -ENOMEM;
}

/*
 * Calculate required resources that are sized based on max. outstanding
 * requests and max. transfer size.
 */

static void pqi_calculate_io_resources(struct pqi_ctrl_info *ctrl_info)
{
	u32 max_transfer_size;
	u32 max_sg_entries;

	ctrl_info->scsi_ml_can_queue =
		ctrl_info->max_outstanding_requests - PQI_RESERVED_IO_SLOTS;
	ctrl_info->max_io_slots = ctrl_info->max_outstanding_requests;

	ctrl_info->error_buffer_length =
		ctrl_info->max_io_slots * PQI_ERROR_BUFFER_ELEMENT_LENGTH;

	if (reset_devices)
		max_transfer_size = min(ctrl_info->max_transfer_size,
			PQI_MAX_TRANSFER_SIZE_KDUMP);
	else
		max_transfer_size = min(ctrl_info->max_transfer_size,
			PQI_MAX_TRANSFER_SIZE);

	max_sg_entries = max_transfer_size / PAGE_SIZE;

	/* +1 to cover when the buffer is not page-aligned. */
	max_sg_entries++;

	max_sg_entries = min(ctrl_info->max_sg_entries, max_sg_entries);

	max_transfer_size = (max_sg_entries - 1) * PAGE_SIZE;

	ctrl_info->sg_chain_buffer_length =
		(max_sg_entries * sizeof(struct pqi_sg_descriptor)) +
		PQI_EXTRA_SGL_MEMORY;
	ctrl_info->sg_tablesize = max_sg_entries;
	ctrl_info->max_sectors = max_transfer_size / 512;
}

static void pqi_calculate_queue_resources(struct pqi_ctrl_info *ctrl_info)
{
	int num_queue_groups;
	u16 num_elements_per_iq;
	u16 num_elements_per_oq;

	if (reset_devices) {
		num_queue_groups = 1;
	} else {
		int num_cpus;
		int max_queue_groups;

		max_queue_groups = min(ctrl_info->max_inbound_queues / 2,
			ctrl_info->max_outbound_queues - 1);
		max_queue_groups = min(max_queue_groups, PQI_MAX_QUEUE_GROUPS);

		num_cpus = num_online_cpus();
		num_queue_groups = min(num_cpus, ctrl_info->max_msix_vectors);
		num_queue_groups = min(num_queue_groups, max_queue_groups);
	}

	ctrl_info->num_queue_groups = num_queue_groups;

	/*
	 * Make sure that the max. inbound IU length is an even multiple
	 * of our inbound element length.
	 */
	ctrl_info->max_inbound_iu_length =
		(ctrl_info->max_inbound_iu_length_per_firmware /
		PQI_OPERATIONAL_IQ_ELEMENT_LENGTH) *
		PQI_OPERATIONAL_IQ_ELEMENT_LENGTH;

	num_elements_per_iq =
		(ctrl_info->max_inbound_iu_length /
		PQI_OPERATIONAL_IQ_ELEMENT_LENGTH);

	/* Add one because one element in each queue is unusable. */
	num_elements_per_iq++;

	num_elements_per_iq = min(num_elements_per_iq,
		ctrl_info->max_elements_per_iq);

	num_elements_per_oq = ((num_elements_per_iq - 1) * 2) + 1;
	num_elements_per_oq = min(num_elements_per_oq,
		ctrl_info->max_elements_per_oq);

	ctrl_info->num_elements_per_iq = num_elements_per_iq;
	ctrl_info->num_elements_per_oq = num_elements_per_oq;

	ctrl_info->max_sg_per_iu =
		((ctrl_info->max_inbound_iu_length -
		PQI_OPERATIONAL_IQ_ELEMENT_LENGTH) /
		sizeof(struct pqi_sg_descriptor)) +
		PQI_MAX_EMBEDDED_SG_DESCRIPTORS;

	ctrl_info->max_sg_per_r56_iu =
		((ctrl_info->max_inbound_iu_length -
		PQI_OPERATIONAL_IQ_ELEMENT_LENGTH) /
		sizeof(struct pqi_sg_descriptor)) +
		PQI_MAX_EMBEDDED_R56_SG_DESCRIPTORS;
}

static inline void pqi_set_sg_descriptor(struct pqi_sg_descriptor *sg_descriptor,
	struct scatterlist *sg)
{
	u64 address = (u64)sg_dma_address(sg);
	unsigned int length = sg_dma_len(sg);

	put_unaligned_le64(address, &sg_descriptor->address);
	put_unaligned_le32(length, &sg_descriptor->length);
	put_unaligned_le32(0, &sg_descriptor->flags);
}

static unsigned int pqi_build_sg_list(struct pqi_sg_descriptor *sg_descriptor,
	struct scatterlist *sg, int sg_count, struct pqi_io_request *io_request,
	int max_sg_per_iu, bool *chained)
{
	int i;
	unsigned int num_sg_in_iu;

	*chained = false;
	i = 0;
	num_sg_in_iu = 0;
	max_sg_per_iu--;	/* Subtract 1 to leave room for chain marker. */

	while (1) {
		pqi_set_sg_descriptor(sg_descriptor, sg);
		if (!*chained)
			num_sg_in_iu++;
		i++;
		if (i == sg_count)
			break;
		sg_descriptor++;
		if (i == max_sg_per_iu) {
			put_unaligned_le64((u64)io_request->sg_chain_buffer_dma_handle,
				&sg_descriptor->address);
			put_unaligned_le32((sg_count - num_sg_in_iu) * sizeof(*sg_descriptor),
				&sg_descriptor->length);
			put_unaligned_le32(CISS_SG_CHAIN, &sg_descriptor->flags);
			*chained = true;
			num_sg_in_iu++;
			sg_descriptor = io_request->sg_chain_buffer;
		}
		sg = sg_next(sg);
	}

	put_unaligned_le32(CISS_SG_LAST, &sg_descriptor->flags);

	return num_sg_in_iu;
}

static int pqi_build_raid_sg_list(struct pqi_ctrl_info *ctrl_info,
	struct pqi_raid_path_request *request, struct scsi_cmnd *scmd,
	struct pqi_io_request *io_request)
{
	u16 iu_length;
	int sg_count;
	bool chained;
	unsigned int num_sg_in_iu;
	struct scatterlist *sg;
	struct pqi_sg_descriptor *sg_descriptor;

	sg_count = scsi_dma_map(scmd);
	if (sg_count < 0)
		return sg_count;

	iu_length = offsetof(struct pqi_raid_path_request, sg_descriptors) -
		PQI_REQUEST_HEADER_LENGTH;

	if (sg_count == 0)
		goto out;

	sg = scsi_sglist(scmd);
	sg_descriptor = request->sg_descriptors;

	num_sg_in_iu = pqi_build_sg_list(sg_descriptor, sg, sg_count, io_request,
		ctrl_info->max_sg_per_iu, &chained);

	request->partial = chained;
	iu_length += num_sg_in_iu * sizeof(*sg_descriptor);

out:
	put_unaligned_le16(iu_length, &request->header.iu_length);

	return 0;
}

static int pqi_build_aio_r1_sg_list(struct pqi_ctrl_info *ctrl_info,
	struct pqi_aio_r1_path_request *request, struct scsi_cmnd *scmd,
	struct pqi_io_request *io_request)
{
	u16 iu_length;
	int sg_count;
	bool chained;
	unsigned int num_sg_in_iu;
	struct scatterlist *sg;
	struct pqi_sg_descriptor *sg_descriptor;

	sg_count = scsi_dma_map(scmd);
	if (sg_count < 0)
		return sg_count;

	iu_length = offsetof(struct pqi_aio_r1_path_request, sg_descriptors) -
		PQI_REQUEST_HEADER_LENGTH;
	num_sg_in_iu = 0;

	if (sg_count == 0)
		goto out;

	sg = scsi_sglist(scmd);
	sg_descriptor = request->sg_descriptors;

	num_sg_in_iu = pqi_build_sg_list(sg_descriptor, sg, sg_count, io_request,
		ctrl_info->max_sg_per_iu, &chained);

	request->partial = chained;
	iu_length += num_sg_in_iu * sizeof(*sg_descriptor);

out:
	put_unaligned_le16(iu_length, &request->header.iu_length);
	request->num_sg_descriptors = num_sg_in_iu;

	return 0;
}

static int pqi_build_aio_r56_sg_list(struct pqi_ctrl_info *ctrl_info,
	struct pqi_aio_r56_path_request *request, struct scsi_cmnd *scmd,
	struct pqi_io_request *io_request)
{
	u16 iu_length;
	int sg_count;
	bool chained;
	unsigned int num_sg_in_iu;
	struct scatterlist *sg;
	struct pqi_sg_descriptor *sg_descriptor;

	sg_count = scsi_dma_map(scmd);
	if (sg_count < 0)
		return sg_count;

	iu_length = offsetof(struct pqi_aio_r56_path_request, sg_descriptors) -
		PQI_REQUEST_HEADER_LENGTH;
	num_sg_in_iu = 0;

	if (sg_count != 0) {
		sg = scsi_sglist(scmd);
		sg_descriptor = request->sg_descriptors;

		num_sg_in_iu = pqi_build_sg_list(sg_descriptor, sg, sg_count, io_request,
			ctrl_info->max_sg_per_r56_iu, &chained);

		request->partial = chained;
		iu_length += num_sg_in_iu * sizeof(*sg_descriptor);
	}

	put_unaligned_le16(iu_length, &request->header.iu_length);
	request->num_sg_descriptors = num_sg_in_iu;

	return 0;
}

static int pqi_build_aio_sg_list(struct pqi_ctrl_info *ctrl_info,
	struct pqi_aio_path_request *request, struct scsi_cmnd *scmd,
	struct pqi_io_request *io_request)
{
	u16 iu_length;
	int sg_count;
	bool chained;
	unsigned int num_sg_in_iu;
	struct scatterlist *sg;
	struct pqi_sg_descriptor *sg_descriptor;

	sg_count = scsi_dma_map(scmd);
	if (sg_count < 0)
		return sg_count;

	iu_length = offsetof(struct pqi_aio_path_request, sg_descriptors) -
		PQI_REQUEST_HEADER_LENGTH;
	num_sg_in_iu = 0;

	if (sg_count == 0)
		goto out;

	sg = scsi_sglist(scmd);
	sg_descriptor = request->sg_descriptors;

	num_sg_in_iu = pqi_build_sg_list(sg_descriptor, sg, sg_count, io_request,
		ctrl_info->max_sg_per_iu, &chained);

	request->partial = chained;
	iu_length += num_sg_in_iu * sizeof(*sg_descriptor);

out:
	put_unaligned_le16(iu_length, &request->header.iu_length);
	request->num_sg_descriptors = num_sg_in_iu;

	return 0;
}

static void pqi_raid_io_complete(struct pqi_io_request *io_request,
	void *context)
{
	struct scsi_cmnd *scmd;

	scmd = io_request->scmd;
	pqi_free_io_request(io_request);
	scsi_dma_unmap(scmd);
	pqi_scsi_done(scmd);
}

static int pqi_raid_submit_io(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device, struct scsi_cmnd *scmd,
	struct pqi_queue_group *queue_group, bool io_high_prio)
{
	int rc;
	size_t cdb_length;
	struct pqi_io_request *io_request;
	struct pqi_raid_path_request *request;

	io_request = pqi_alloc_io_request(ctrl_info, scmd);
	if (!io_request)
		return SCSI_MLQUEUE_HOST_BUSY;

	io_request->io_complete_callback = pqi_raid_io_complete;
	io_request->scmd = scmd;

	request = io_request->iu;
	memset(request, 0, offsetof(struct pqi_raid_path_request, sg_descriptors));

	request->header.iu_type = PQI_REQUEST_IU_RAID_PATH_IO;
	put_unaligned_le32(scsi_bufflen(scmd), &request->buffer_length);
	request->task_attribute = SOP_TASK_ATTRIBUTE_SIMPLE;
	request->command_priority = io_high_prio;
	put_unaligned_le16(io_request->index, &request->request_id);
	request->error_index = request->request_id;
	memcpy(request->lun_number, device->scsi3addr, sizeof(request->lun_number));
	request->ml_device_lun_number = (u8)scmd->device->lun;

	cdb_length = min_t(size_t, scmd->cmd_len, sizeof(request->cdb));
	memcpy(request->cdb, scmd->cmnd, cdb_length);

	switch (cdb_length) {
	case 6:
	case 10:
	case 12:
	case 16:
		request->additional_cdb_bytes_usage = SOP_ADDITIONAL_CDB_BYTES_0;
		break;
	case 20:
		request->additional_cdb_bytes_usage = SOP_ADDITIONAL_CDB_BYTES_4;
		break;
	case 24:
		request->additional_cdb_bytes_usage = SOP_ADDITIONAL_CDB_BYTES_8;
		break;
	case 28:
		request->additional_cdb_bytes_usage = SOP_ADDITIONAL_CDB_BYTES_12;
		break;
	case 32:
	default:
		request->additional_cdb_bytes_usage = SOP_ADDITIONAL_CDB_BYTES_16;
		break;
	}

	switch (scmd->sc_data_direction) {
	case DMA_FROM_DEVICE:
		request->data_direction = SOP_READ_FLAG;
		break;
	case DMA_TO_DEVICE:
		request->data_direction = SOP_WRITE_FLAG;
		break;
	case DMA_NONE:
		request->data_direction = SOP_NO_DIRECTION_FLAG;
		break;
	case DMA_BIDIRECTIONAL:
		request->data_direction = SOP_BIDIRECTIONAL;
		break;
	default:
		dev_err(&ctrl_info->pci_dev->dev,
			"unknown data direction: %d\n",
			scmd->sc_data_direction);
		break;
	}

	rc = pqi_build_raid_sg_list(ctrl_info, request, scmd, io_request);
	if (rc) {
		pqi_free_io_request(io_request);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	pqi_start_io(ctrl_info, queue_group, RAID_PATH, io_request);

	return 0;
}

static inline int pqi_raid_submit_scsi_cmd(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device, struct scsi_cmnd *scmd,
	struct pqi_queue_group *queue_group)
{
	bool io_high_prio;

	io_high_prio = pqi_is_io_high_priority(device, scmd);

	return pqi_raid_submit_io(ctrl_info, device, scmd, queue_group, io_high_prio);
}

static bool pqi_raid_bypass_retry_needed(struct pqi_io_request *io_request)
{
	struct scsi_cmnd *scmd;
	struct pqi_scsi_dev *device;
	struct pqi_ctrl_info *ctrl_info;

	if (!io_request->raid_bypass)
		return false;

	scmd = io_request->scmd;
	if ((scmd->result & 0xff) == SAM_STAT_GOOD)
		return false;
	if (host_byte(scmd->result) == DID_NO_CONNECT)
		return false;

	device = scmd->device->hostdata;
	if (pqi_device_offline(device) || pqi_device_in_remove(device))
		return false;

	ctrl_info = shost_to_hba(scmd->device->host);
	if (pqi_ctrl_offline(ctrl_info))
		return false;

	return true;
}

static void pqi_aio_io_complete(struct pqi_io_request *io_request,
	void *context)
{
	struct scsi_cmnd *scmd;

	scmd = io_request->scmd;
	scsi_dma_unmap(scmd);
	if (io_request->status == -EAGAIN || pqi_raid_bypass_retry_needed(io_request)) {
		set_host_byte(scmd, DID_IMM_RETRY);
		pqi_cmd_priv(scmd)->this_residual++;
	}

	pqi_free_io_request(io_request);
	pqi_scsi_done(scmd);
}

static inline int pqi_aio_submit_scsi_cmd(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device, struct scsi_cmnd *scmd,
	struct pqi_queue_group *queue_group)
{
	bool io_high_prio;

	io_high_prio = pqi_is_io_high_priority(device, scmd);

	return pqi_aio_submit_io(ctrl_info, scmd, device->aio_handle,
		scmd->cmnd, scmd->cmd_len, queue_group, NULL,
		false, io_high_prio);
}

static int pqi_aio_submit_io(struct pqi_ctrl_info *ctrl_info,
	struct scsi_cmnd *scmd, u32 aio_handle, u8 *cdb,
	unsigned int cdb_length, struct pqi_queue_group *queue_group,
	struct pqi_encryption_info *encryption_info, bool raid_bypass,
	bool io_high_prio)
{
	int rc;
	struct pqi_io_request *io_request;
	struct pqi_aio_path_request *request;

	io_request = pqi_alloc_io_request(ctrl_info, scmd);
	if (!io_request)
		return SCSI_MLQUEUE_HOST_BUSY;

	io_request->io_complete_callback = pqi_aio_io_complete;
	io_request->scmd = scmd;
	io_request->raid_bypass = raid_bypass;

	request = io_request->iu;
	memset(request, 0, offsetof(struct pqi_aio_path_request, sg_descriptors));

	request->header.iu_type = PQI_REQUEST_IU_AIO_PATH_IO;
	put_unaligned_le32(aio_handle, &request->nexus_id);
	put_unaligned_le32(scsi_bufflen(scmd), &request->buffer_length);
	request->task_attribute = SOP_TASK_ATTRIBUTE_SIMPLE;
	request->command_priority = io_high_prio;
	put_unaligned_le16(io_request->index, &request->request_id);
	request->error_index = request->request_id;
	if (!raid_bypass && ctrl_info->multi_lun_device_supported)
		put_unaligned_le64(scmd->device->lun << 8, &request->lun_number);
	if (cdb_length > sizeof(request->cdb))
		cdb_length = sizeof(request->cdb);
	request->cdb_length = cdb_length;
	memcpy(request->cdb, cdb, cdb_length);

	switch (scmd->sc_data_direction) {
	case DMA_TO_DEVICE:
		request->data_direction = SOP_READ_FLAG;
		break;
	case DMA_FROM_DEVICE:
		request->data_direction = SOP_WRITE_FLAG;
		break;
	case DMA_NONE:
		request->data_direction = SOP_NO_DIRECTION_FLAG;
		break;
	case DMA_BIDIRECTIONAL:
		request->data_direction = SOP_BIDIRECTIONAL;
		break;
	default:
		dev_err(&ctrl_info->pci_dev->dev,
			"unknown data direction: %d\n",
			scmd->sc_data_direction);
		break;
	}

	if (encryption_info) {
		request->encryption_enable = true;
		put_unaligned_le16(encryption_info->data_encryption_key_index,
			&request->data_encryption_key_index);
		put_unaligned_le32(encryption_info->encrypt_tweak_lower,
			&request->encrypt_tweak_lower);
		put_unaligned_le32(encryption_info->encrypt_tweak_upper,
			&request->encrypt_tweak_upper);
	}

	rc = pqi_build_aio_sg_list(ctrl_info, request, scmd, io_request);
	if (rc) {
		pqi_free_io_request(io_request);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	pqi_start_io(ctrl_info, queue_group, AIO_PATH, io_request);

	return 0;
}

static  int pqi_aio_submit_r1_write_io(struct pqi_ctrl_info *ctrl_info,
	struct scsi_cmnd *scmd, struct pqi_queue_group *queue_group,
	struct pqi_encryption_info *encryption_info, struct pqi_scsi_dev *device,
	struct pqi_scsi_dev_raid_map_data *rmd)
{
	int rc;
	struct pqi_io_request *io_request;
	struct pqi_aio_r1_path_request *r1_request;

	io_request = pqi_alloc_io_request(ctrl_info, scmd);
	if (!io_request)
		return SCSI_MLQUEUE_HOST_BUSY;

	io_request->io_complete_callback = pqi_aio_io_complete;
	io_request->scmd = scmd;
	io_request->raid_bypass = true;

	r1_request = io_request->iu;
	memset(r1_request, 0, offsetof(struct pqi_aio_r1_path_request, sg_descriptors));

	r1_request->header.iu_type = PQI_REQUEST_IU_AIO_PATH_RAID1_IO;
	put_unaligned_le16(*(u16 *)device->scsi3addr & 0x3fff, &r1_request->volume_id);
	r1_request->num_drives = rmd->num_it_nexus_entries;
	put_unaligned_le32(rmd->it_nexus[0], &r1_request->it_nexus_1);
	put_unaligned_le32(rmd->it_nexus[1], &r1_request->it_nexus_2);
	if (rmd->num_it_nexus_entries == 3)
		put_unaligned_le32(rmd->it_nexus[2], &r1_request->it_nexus_3);

	put_unaligned_le32(scsi_bufflen(scmd), &r1_request->data_length);
	r1_request->task_attribute = SOP_TASK_ATTRIBUTE_SIMPLE;
	put_unaligned_le16(io_request->index, &r1_request->request_id);
	r1_request->error_index = r1_request->request_id;
	if (rmd->cdb_length > sizeof(r1_request->cdb))
		rmd->cdb_length = sizeof(r1_request->cdb);
	r1_request->cdb_length = rmd->cdb_length;
	memcpy(r1_request->cdb, rmd->cdb, rmd->cdb_length);

	/* The direction is always write. */
	r1_request->data_direction = SOP_READ_FLAG;

	if (encryption_info) {
		r1_request->encryption_enable = true;
		put_unaligned_le16(encryption_info->data_encryption_key_index,
				&r1_request->data_encryption_key_index);
		put_unaligned_le32(encryption_info->encrypt_tweak_lower,
				&r1_request->encrypt_tweak_lower);
		put_unaligned_le32(encryption_info->encrypt_tweak_upper,
				&r1_request->encrypt_tweak_upper);
	}

	rc = pqi_build_aio_r1_sg_list(ctrl_info, r1_request, scmd, io_request);
	if (rc) {
		pqi_free_io_request(io_request);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	pqi_start_io(ctrl_info, queue_group, AIO_PATH, io_request);

	return 0;
}

static int pqi_aio_submit_r56_write_io(struct pqi_ctrl_info *ctrl_info,
	struct scsi_cmnd *scmd, struct pqi_queue_group *queue_group,
	struct pqi_encryption_info *encryption_info, struct pqi_scsi_dev *device,
	struct pqi_scsi_dev_raid_map_data *rmd)
{
	int rc;
	struct pqi_io_request *io_request;
	struct pqi_aio_r56_path_request *r56_request;

	io_request = pqi_alloc_io_request(ctrl_info, scmd);
	if (!io_request)
		return SCSI_MLQUEUE_HOST_BUSY;
	io_request->io_complete_callback = pqi_aio_io_complete;
	io_request->scmd = scmd;
	io_request->raid_bypass = true;

	r56_request = io_request->iu;
	memset(r56_request, 0, offsetof(struct pqi_aio_r56_path_request, sg_descriptors));

	if (device->raid_level == SA_RAID_5 || device->raid_level == SA_RAID_51)
		r56_request->header.iu_type = PQI_REQUEST_IU_AIO_PATH_RAID5_IO;
	else
		r56_request->header.iu_type = PQI_REQUEST_IU_AIO_PATH_RAID6_IO;

	put_unaligned_le16(*(u16 *)device->scsi3addr & 0x3fff, &r56_request->volume_id);
	put_unaligned_le32(rmd->aio_handle, &r56_request->data_it_nexus);
	put_unaligned_le32(rmd->p_parity_it_nexus, &r56_request->p_parity_it_nexus);
	if (rmd->raid_level == SA_RAID_6) {
		put_unaligned_le32(rmd->q_parity_it_nexus, &r56_request->q_parity_it_nexus);
		r56_request->xor_multiplier = rmd->xor_mult;
	}
	put_unaligned_le32(scsi_bufflen(scmd), &r56_request->data_length);
	r56_request->task_attribute = SOP_TASK_ATTRIBUTE_SIMPLE;
	put_unaligned_le64(rmd->row, &r56_request->row);

	put_unaligned_le16(io_request->index, &r56_request->request_id);
	r56_request->error_index = r56_request->request_id;

	if (rmd->cdb_length > sizeof(r56_request->cdb))
		rmd->cdb_length = sizeof(r56_request->cdb);
	r56_request->cdb_length = rmd->cdb_length;
	memcpy(r56_request->cdb, rmd->cdb, rmd->cdb_length);

	/* The direction is always write. */
	r56_request->data_direction = SOP_READ_FLAG;

	if (encryption_info) {
		r56_request->encryption_enable = true;
		put_unaligned_le16(encryption_info->data_encryption_key_index,
				&r56_request->data_encryption_key_index);
		put_unaligned_le32(encryption_info->encrypt_tweak_lower,
				&r56_request->encrypt_tweak_lower);
		put_unaligned_le32(encryption_info->encrypt_tweak_upper,
				&r56_request->encrypt_tweak_upper);
	}

	rc = pqi_build_aio_r56_sg_list(ctrl_info, r56_request, scmd, io_request);
	if (rc) {
		pqi_free_io_request(io_request);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	pqi_start_io(ctrl_info, queue_group, AIO_PATH, io_request);

	return 0;
}

static inline u16 pqi_get_hw_queue(struct pqi_ctrl_info *ctrl_info,
	struct scsi_cmnd *scmd)
{
	/*
	 * We are setting host_tagset = 1 during init.
	 */
	return blk_mq_unique_tag_to_hwq(blk_mq_unique_tag(scsi_cmd_to_rq(scmd)));
}

static inline bool pqi_is_bypass_eligible_request(struct scsi_cmnd *scmd)
{
	if (blk_rq_is_passthrough(scsi_cmd_to_rq(scmd)))
		return false;

	return pqi_cmd_priv(scmd)->this_residual == 0;
}

/*
 * This function gets called just before we hand the completed SCSI request
 * back to the SML.
 */

void pqi_prep_for_scsi_done(struct scsi_cmnd *scmd)
{
	struct pqi_scsi_dev *device;
	struct completion *wait;

	if (!scmd->device) {
		set_host_byte(scmd, DID_NO_CONNECT);
		return;
	}

	device = scmd->device->hostdata;
	if (!device) {
		set_host_byte(scmd, DID_NO_CONNECT);
		return;
	}

	atomic_dec(&device->scsi_cmds_outstanding[scmd->device->lun]);

	wait = (struct completion *)xchg(&scmd->host_scribble, NULL);
	if (wait != PQI_NO_COMPLETION)
		complete(wait);
}

static bool pqi_is_parity_write_stream(struct pqi_ctrl_info *ctrl_info,
	struct scsi_cmnd *scmd)
{
	u32 oldest_jiffies;
	u8 lru_index;
	int i;
	int rc;
	struct pqi_scsi_dev *device;
	struct pqi_stream_data *pqi_stream_data;
	struct pqi_scsi_dev_raid_map_data rmd = { 0 };

	if (!ctrl_info->enable_stream_detection)
		return false;

	rc = pqi_get_aio_lba_and_block_count(scmd, &rmd);
	if (rc)
		return false;

	/* Check writes only. */
	if (!rmd.is_write)
		return false;

	device = scmd->device->hostdata;

	/* Check for RAID 5/6 streams. */
	if (device->raid_level != SA_RAID_5 && device->raid_level != SA_RAID_6)
		return false;

	/*
	 * If controller does not support AIO RAID{5,6} writes, need to send
	 * requests down non-AIO path.
	 */
	if ((device->raid_level == SA_RAID_5 && !ctrl_info->enable_r5_writes) ||
		(device->raid_level == SA_RAID_6 && !ctrl_info->enable_r6_writes))
		return true;

	lru_index = 0;
	oldest_jiffies = INT_MAX;
	for (i = 0; i < NUM_STREAMS_PER_LUN; i++) {
		pqi_stream_data = &device->stream_data[i];
		/*
		 * Check for adjacent request or request is within
		 * the previous request.
		 */
		if ((pqi_stream_data->next_lba &&
			rmd.first_block >= pqi_stream_data->next_lba) &&
			rmd.first_block <= pqi_stream_data->next_lba +
				rmd.block_cnt) {
			pqi_stream_data->next_lba = rmd.first_block +
				rmd.block_cnt;
			pqi_stream_data->last_accessed = jiffies;
			per_cpu_ptr(device->raid_io_stats, smp_processor_id())->write_stream_cnt++;
			return true;
		}

		/* unused entry */
		if (pqi_stream_data->last_accessed == 0) {
			lru_index = i;
			break;
		}

		/* Find entry with oldest last accessed time. */
		if (pqi_stream_data->last_accessed <= oldest_jiffies) {
			oldest_jiffies = pqi_stream_data->last_accessed;
			lru_index = i;
		}
	}

	/* Set LRU entry. */
	pqi_stream_data = &device->stream_data[lru_index];
	pqi_stream_data->last_accessed = jiffies;
	pqi_stream_data->next_lba = rmd.first_block + rmd.block_cnt;

	return false;
}

static int pqi_scsi_queue_command(struct Scsi_Host *shost, struct scsi_cmnd *scmd)
{
	int rc;
	struct pqi_ctrl_info *ctrl_info;
	struct pqi_scsi_dev *device;
	u16 hw_queue;
	struct pqi_queue_group *queue_group;
	bool raid_bypassed;
	u8 lun;

	scmd->host_scribble = PQI_NO_COMPLETION;

	device = scmd->device->hostdata;

	if (!device) {
		set_host_byte(scmd, DID_NO_CONNECT);
		pqi_scsi_done(scmd);
		return 0;
	}

	lun = (u8)scmd->device->lun;

	atomic_inc(&device->scsi_cmds_outstanding[lun]);

	ctrl_info = shost_to_hba(shost);

	if (pqi_ctrl_offline(ctrl_info) || pqi_device_offline(device) || pqi_device_in_remove(device)) {
		set_host_byte(scmd, DID_NO_CONNECT);
		pqi_scsi_done(scmd);
		return 0;
	}

	if (pqi_ctrl_blocked(ctrl_info) || pqi_device_in_reset(device, lun)) {
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}

	/*
	 * This is necessary because the SML doesn't zero out this field during
	 * error recovery.
	 */
	scmd->result = 0;

	hw_queue = pqi_get_hw_queue(ctrl_info, scmd);
	queue_group = &ctrl_info->queue_groups[hw_queue];

	if (pqi_is_logical_device(device)) {
		raid_bypassed = false;
		if (device->raid_bypass_enabled &&
			pqi_is_bypass_eligible_request(scmd) &&
			!pqi_is_parity_write_stream(ctrl_info, scmd)) {
			rc = pqi_raid_bypass_submit_scsi_cmd(ctrl_info, device, scmd, queue_group);
			if (rc == 0 || rc == SCSI_MLQUEUE_HOST_BUSY) {
				raid_bypassed = true;
				per_cpu_ptr(device->raid_io_stats, smp_processor_id())->raid_bypass_cnt++;
			}
		}
		if (!raid_bypassed)
			rc = pqi_raid_submit_scsi_cmd(ctrl_info, device, scmd, queue_group);
	} else {
		if (device->aio_enabled)
			rc = pqi_aio_submit_scsi_cmd(ctrl_info, device, scmd, queue_group);
		else
			rc = pqi_raid_submit_scsi_cmd(ctrl_info, device, scmd, queue_group);
	}

out:
	if (rc) {
		scmd->host_scribble = NULL;
		atomic_dec(&device->scsi_cmds_outstanding[lun]);
	}

	return rc;
}

static unsigned int pqi_queued_io_count(struct pqi_ctrl_info *ctrl_info)
{
	unsigned int i;
	unsigned int path;
	unsigned long flags;
	unsigned int queued_io_count;
	struct pqi_queue_group *queue_group;
	struct pqi_io_request *io_request;

	queued_io_count = 0;

	for (i = 0; i < ctrl_info->num_queue_groups; i++) {
		queue_group = &ctrl_info->queue_groups[i];
		for (path = 0; path < 2; path++) {
			spin_lock_irqsave(&queue_group->submit_lock[path], flags);
			list_for_each_entry(io_request, &queue_group->request_list[path], request_list_entry)
				queued_io_count++;
			spin_unlock_irqrestore(&queue_group->submit_lock[path], flags);
		}
	}

	return queued_io_count;
}

static unsigned int pqi_nonempty_inbound_queue_count(struct pqi_ctrl_info *ctrl_info)
{
	unsigned int i;
	unsigned int path;
	unsigned int nonempty_inbound_queue_count;
	struct pqi_queue_group *queue_group;
	pqi_index_t iq_pi;
	pqi_index_t iq_ci;

	nonempty_inbound_queue_count = 0;

	for (i = 0; i < ctrl_info->num_queue_groups; i++) {
		queue_group = &ctrl_info->queue_groups[i];
		for (path = 0; path < 2; path++) {
			iq_pi = queue_group->iq_pi_copy[path];
			iq_ci = readl(queue_group->iq_ci[path]);
			if (iq_ci != iq_pi)
				nonempty_inbound_queue_count++;
		}
	}

	return nonempty_inbound_queue_count;
}

#define PQI_INBOUND_QUEUES_NONEMPTY_WARNING_TIMEOUT_SECS	10

static int pqi_wait_until_inbound_queues_empty(struct pqi_ctrl_info *ctrl_info)
{
	unsigned long start_jiffies;
	unsigned long warning_timeout;
	unsigned int queued_io_count;
	unsigned int nonempty_inbound_queue_count;
	bool displayed_warning;

	displayed_warning = false;
	start_jiffies = jiffies;
	warning_timeout = (PQI_INBOUND_QUEUES_NONEMPTY_WARNING_TIMEOUT_SECS * HZ) + start_jiffies;

	while (1) {
		queued_io_count = pqi_queued_io_count(ctrl_info);
		nonempty_inbound_queue_count = pqi_nonempty_inbound_queue_count(ctrl_info);
		if (queued_io_count == 0 && nonempty_inbound_queue_count == 0)
			break;
		pqi_check_ctrl_health(ctrl_info);
		if (pqi_ctrl_offline(ctrl_info))
			return -ENXIO;
		if (time_after(jiffies, warning_timeout)) {
			dev_warn(&ctrl_info->pci_dev->dev,
				"waiting %u seconds for queued I/O to drain (queued I/O count: %u; non-empty inbound queue count: %u)\n",
				jiffies_to_msecs(jiffies - start_jiffies) / 1000, queued_io_count, nonempty_inbound_queue_count);
			displayed_warning = true;
			warning_timeout = (PQI_INBOUND_QUEUES_NONEMPTY_WARNING_TIMEOUT_SECS * HZ) + jiffies;
		}
		usleep_range(1000, 2000);
	}

	if (displayed_warning)
		dev_warn(&ctrl_info->pci_dev->dev,
			"queued I/O drained after waiting for %u seconds\n",
			jiffies_to_msecs(jiffies - start_jiffies) / 1000);

	return 0;
}

static void pqi_fail_io_queued_for_device(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device, u8 lun)
{
	unsigned int i;
	unsigned int path;
	struct pqi_queue_group *queue_group;
	unsigned long flags;
	struct pqi_io_request *io_request;
	struct pqi_io_request *next;
	struct scsi_cmnd *scmd;
	struct pqi_scsi_dev *scsi_device;

	for (i = 0; i < ctrl_info->num_queue_groups; i++) {
		queue_group = &ctrl_info->queue_groups[i];

		for (path = 0; path < 2; path++) {
			spin_lock_irqsave(
				&queue_group->submit_lock[path], flags);

			list_for_each_entry_safe(io_request, next,
				&queue_group->request_list[path],
				request_list_entry) {

				scmd = io_request->scmd;
				if (!scmd)
					continue;

				scsi_device = scmd->device->hostdata;

				list_del(&io_request->request_list_entry);
				if (scsi_device == device && (u8)scmd->device->lun == lun)
					set_host_byte(scmd, DID_RESET);
				else
					set_host_byte(scmd, DID_REQUEUE);
				pqi_free_io_request(io_request);
				scsi_dma_unmap(scmd);
				pqi_scsi_done(scmd);
			}

			spin_unlock_irqrestore(
				&queue_group->submit_lock[path], flags);
		}
	}
}

#define PQI_PENDING_IO_WARNING_TIMEOUT_SECS	10

static int pqi_device_wait_for_pending_io(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device, u8 lun, unsigned long timeout_msecs)
{
	int cmds_outstanding;
	unsigned long start_jiffies;
	unsigned long warning_timeout;
	unsigned long msecs_waiting;

	start_jiffies = jiffies;
	warning_timeout = (PQI_PENDING_IO_WARNING_TIMEOUT_SECS * HZ) + start_jiffies;

	while ((cmds_outstanding = atomic_read(&device->scsi_cmds_outstanding[lun])) > 0) {
		if (ctrl_info->ctrl_removal_state != PQI_CTRL_GRACEFUL_REMOVAL) {
			pqi_check_ctrl_health(ctrl_info);
			if (pqi_ctrl_offline(ctrl_info))
				return -ENXIO;
		}
		msecs_waiting = jiffies_to_msecs(jiffies - start_jiffies);
		if (msecs_waiting >= timeout_msecs) {
			dev_err(&ctrl_info->pci_dev->dev,
				"scsi %d:%d:%d:%d: timed out after %lu seconds waiting for %d outstanding command(s)\n",
				ctrl_info->scsi_host->host_no, device->bus, device->target,
				lun, msecs_waiting / 1000, cmds_outstanding);
			return -ETIMEDOUT;
		}
		if (time_after(jiffies, warning_timeout)) {
			dev_warn(&ctrl_info->pci_dev->dev,
				"scsi %d:%d:%d:%d: waiting %lu seconds for %d outstanding command(s)\n",
				ctrl_info->scsi_host->host_no, device->bus, device->target,
				lun, msecs_waiting / 1000, cmds_outstanding);
			warning_timeout = (PQI_PENDING_IO_WARNING_TIMEOUT_SECS * HZ) + jiffies;
		}
		usleep_range(1000, 2000);
	}

	return 0;
}

static void pqi_lun_reset_complete(struct pqi_io_request *io_request,
	void *context)
{
	struct completion *waiting = context;

	complete(waiting);
}

#define PQI_LUN_RESET_POLL_COMPLETION_SECS	10

static int pqi_wait_for_lun_reset_completion(struct pqi_ctrl_info *ctrl_info,
	struct pqi_scsi_dev *device, u8 lun, struct completion *wait)
{
	int rc;
	unsigned int wait_secs;
	int cmds_outstanding;

	wait_secs = 0;

	while (1) {
		if (wait_for_completion_io_timeout(wait,
			PQI_LUN_RESET_POLL_COMPLETION_SECS * HZ)) {
			rc = 0;
			break;
		}

		pqi_check_ctrl_health(ctrl_info);
		if (pqi_ctrl_offline(ctrl_info)) {
			rc = -ENXIO;
			break;
		}

		wait_secs += PQI_LUN_RESET_POLL_COMPLETION_SECS;
		cmds_outstanding = atomic_read(&device->scsi_cmds_outstanding[lun]);
		dev_warn(&ctrl_info->pci_dev->dev,
			"scsi %d:%d:%d:%d: waiting %u seconds for LUN reset to complete (%d command(s) outstanding)\n",
			ctrl_info->scsi_host->host_no, device->bus, device->target, lun, wait_secs, cmds_outstanding);
	}

	return rc;
}

#define PQI_LUN_RESET_FIRMWARE_TIMEOUT_SECS	30

static int pqi_lun_reset(struct pqi_ctrl_info *ctrl_info, struct pqi_scsi_dev *device, u8 lun)
{
	int rc;
	struct pqi_io_request *io_request;
	DECLARE_COMPLETION_ONSTACK(wait);
	struct pqi_task_management_request *request;

	io_request = pqi_alloc_io_request(ctrl_info, NULL);
	io_request->io_complete_callback = pqi_lun_reset_complete;
	io_request->context = &wait;

	request = io_request->iu;
	memset(request, 0, sizeof(*request));

	request->header.iu_type = PQI_REQUEST_IU_TASK_MANAGEMENT;
	put_unaligned_le16(sizeof(*request) - PQI_REQUEST_HEADER_LENGTH,
		&request->header.iu_length);
	put_unaligned_le16(io_request->index, &request->request_id);
	memcpy(request->lun_number, device->scsi3addr,
		sizeof(request->lun_number));
	if (!pqi_is_logical_device(device) && ctrl_info->multi_lun_device_supported)
		request->ml_device_lun_number = lun;
	request->task_management_function = SOP_TASK_MANAGEMENT_LUN_RESET;
	if (ctrl_info->tmf_iu_timeout_supported)
		put_unaligned_le16(PQI_LUN_RESET_FIRMWARE_TIMEOUT_SECS, &request->timeout);

	pqi_start_io(ctrl_info, &ctrl_info->queue_groups[PQI_DEFAULT_QUEUE_GROUP], RAID_PATH,
		io_request);

	rc = pqi_wait_for_lun_reset_completion(ctrl_info, device, lun, &wait);
	if (rc == 0)
		rc = io_request->status;

	pqi_free_io_request(io_request);

	return rc;
}

#define PQI_LUN_RESET_RETRIES				3
#define PQI_LUN_RESET_RETRY_INTERVAL_MSECS		(10 * 1000)
#define PQI_LUN_RESET_PENDING_IO_TIMEOUT_MSECS		(10 * 60 * 1000)
#define PQI_LUN_RESET_FAILED_PENDING_IO_TIMEOUT_MSECS	(2 * 60 * 1000)

static int pqi_lun_reset_with_retries(struct pqi_ctrl_info *ctrl_info, struct pqi_scsi_dev *device, u8 lun)
{
	int reset_rc;
	int wait_rc;
	unsigned int retries;
	unsigned long timeout_msecs;

	for (retries = 0;;) {
		reset_rc = pqi_lun_reset(ctrl_info, device, lun);
		if (reset_rc == 0 || reset_rc == -ENODEV || reset_rc == -ENXIO || ++retries > PQI_LUN_RESET_RETRIES)
			break;
		msleep(PQI_LUN_RESET_RETRY_INTERVAL_MSECS);
	}

	timeout_msecs = reset_rc ? PQI_LUN_RESET_FAILED_PENDING_IO_TIMEOUT_MSECS :
		PQI_LUN_RESET_PENDING_IO_TIMEOUT_MSECS;

	wait_rc = pqi_device_wait_for_pending_io(ctrl_info, device, lun, timeout_msecs);
	if (wait_rc && reset_rc == 0)
		reset_rc = wait_rc;

	return reset_rc == 0 ? SUCCESS : FAILED;
}

static int pqi_device_reset(struct pqi_ctrl_info *ctrl_info, struct pqi_scsi_dev *device, u8 lun)
{
	int rc;

	pqi_ctrl_block_requests(ctrl_info);
	pqi_ctrl_wait_until_quiesced(ctrl_info);
	pqi_fail_io_queued_for_device(ctrl_info, device, lun);
	rc = pqi_wait_until_inbound_queues_empty(ctrl_info);
	pqi_device_reset_start(device, lun);
	pqi_ctrl_unblock_requests(ctrl_info);
	if (rc)
		rc = FAILED;
	else
		rc = pqi_lun_reset_with_retries(ctrl_info, device, lun);
	pqi_device_reset_done(device, lun);

	return rc;
}

static int pqi_device_reset_handler(struct pqi_ctrl_info *ctrl_info, struct pqi_scsi_dev *device, u8 lun, struct scsi_cmnd *scmd, u8 scsi_opcode)
{
	int rc;

	mutex_lock(&ctrl_info->lun_reset_mutex);

	dev_err(&ctrl_info->pci_dev->dev,
		"resetting scsi %d:%d:%d:%u SCSI cmd at %p due to cmd opcode 0x%02x\n",
		ctrl_info->scsi_host->host_no, device->bus, device->target, lun, scmd, scsi_opcode);

	pqi_check_ctrl_health(ctrl_info);
	if (pqi_ctrl_offline(ctrl_info))
		rc = FAILED;
	else
		rc = pqi_device_reset(ctrl_info, device, lun);

	dev_err(&ctrl_info->pci_dev->dev,
		"reset of scsi %d:%d:%d:%u: %s\n",
		ctrl_info->scsi_host->host_no, device->bus, device->target, lun,
		rc == SUCCESS ? "SUCCESS" : "FAILED");

	mutex_unlock(&ctrl_info->lun_reset_mutex);

	return rc;
}

static int pqi_eh_device_reset_handler(struct scsi_cmnd *scmd)
{
	struct Scsi_Host *shost;
	struct pqi_ctrl_info *ctrl_info;
	struct pqi_scsi_dev *device;
	u8 scsi_opcode;

	shost = scmd->device->host;
	ctrl_info = shost_to_hba(shost);
	device = scmd->device->hostdata;
	scsi_opcode = scmd->cmd_len > 0 ? scmd->cmnd[0] : 0xff;

	return pqi_device_reset_handler(ctrl_info, device, (u8)scmd->device->lun, scmd, scsi_opcode);
}

static void pqi_tmf_worker(struct work_struct *work)
{
	struct pqi_tmf_work *tmf_work;
	struct scsi_cmnd *scmd;

	tmf_work = container_of(work, struct pqi_tmf_work, work_struct);
	scmd = (struct scsi_cmnd *)xchg(&tmf_work->scmd, NULL);

	pqi_device_reset_handler(tmf_work->ctrl_info, tmf_work->device, tmf_work->lun, scmd, tmf_work->scsi_opcode);
}

static int pqi_eh_abort_handler(struct scsi_cmnd *scmd)
{
	struct Scsi_Host *shost;
	struct pqi_ctrl_info *ctrl_info;
	struct pqi_scsi_dev *device;
	struct pqi_tmf_work *tmf_work;
	DECLARE_COMPLETION_ONSTACK(wait);

	shost = scmd->device->host;
	ctrl_info = shost_to_hba(shost);
	device = scmd->device->hostdata;

	dev_err(&ctrl_info->pci_dev->dev,
		"attempting TASK ABORT on scsi %d:%d:%d:%d for SCSI cmd at %p\n",
		shost->host_no, device->bus, device->target, (int)scmd->device->lun, scmd);

	if (cmpxchg(&scmd->host_scribble, PQI_NO_COMPLETION, (void *)&wait) == NULL) {
		dev_err(&ctrl_info->pci_dev->dev,
			"scsi %d:%d:%d:%d for SCSI cmd at %p already completed\n",
			shost->host_no, device->bus, device->target, (int)scmd->device->lun, scmd);
		scmd->result = DID_RESET << 16;
		goto out;
	}

	tmf_work = &device->tmf_work[scmd->device->lun];

	if (cmpxchg(&tmf_work->scmd, NULL, scmd) == NULL) {
		tmf_work->ctrl_info = ctrl_info;
		tmf_work->device = device;
		tmf_work->lun = (u8)scmd->device->lun;
		tmf_work->scsi_opcode = scmd->cmd_len > 0 ? scmd->cmnd[0] : 0xff;
		schedule_work(&tmf_work->work_struct);
	}

	wait_for_completion(&wait);

	dev_err(&ctrl_info->pci_dev->dev,
		"TASK ABORT on scsi %d:%d:%d:%d for SCSI cmd at %p: SUCCESS\n",
		shost->host_no, device->bus, device->target, (int)scmd->device->lun, scmd);

out:

	return SUCCESS;
}

static int pqi_slave_alloc(struct scsi_device *sdev)
{
	struct pqi_scsi_dev *device;
	unsigned long flags;
	struct pqi_ctrl_info *ctrl_info;
	struct scsi_target *starget;
	struct sas_rphy *rphy;

	ctrl_info = shost_to_hba(sdev->host);

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	if (sdev_channel(sdev) == PQI_PHYSICAL_DEVICE_BUS) {
		starget = scsi_target(sdev);
		rphy = target_to_rphy(starget);
		device = pqi_find_device_by_sas_rphy(ctrl_info, rphy);
		if (device) {
			if (device->target_lun_valid) {
				device->ignore_device = true;
			} else {
				device->target = sdev_id(sdev);
				device->lun = sdev->lun;
				device->target_lun_valid = true;
			}
		}
	} else {
		device = pqi_find_scsi_dev(ctrl_info, sdev_channel(sdev),
			sdev_id(sdev), sdev->lun);
	}

	if (device) {
		sdev->hostdata = device;
		device->sdev = sdev;
		if (device->queue_depth) {
			device->advertised_queue_depth = device->queue_depth;
			scsi_change_queue_depth(sdev,
				device->advertised_queue_depth);
		}
		if (pqi_is_logical_device(device)) {
			pqi_disable_write_same(sdev);
		} else {
			sdev->allow_restart = 1;
			if (device->device_type == SA_DEVICE_TYPE_NVME)
				pqi_disable_write_same(sdev);
		}
	}

	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	return 0;
}

static void pqi_map_queues(struct Scsi_Host *shost)
{
	struct pqi_ctrl_info *ctrl_info = shost_to_hba(shost);

	if (!ctrl_info->disable_managed_interrupts)
		return blk_mq_pci_map_queues(&shost->tag_set.map[HCTX_TYPE_DEFAULT],
			      ctrl_info->pci_dev, 0);
	else
		return blk_mq_map_queues(&shost->tag_set.map[HCTX_TYPE_DEFAULT]);
}

static inline bool pqi_is_tape_changer_device(struct pqi_scsi_dev *device)
{
	return device->devtype == TYPE_TAPE || device->devtype == TYPE_MEDIUM_CHANGER;
}

static int pqi_slave_configure(struct scsi_device *sdev)
{
	int rc = 0;
	struct pqi_scsi_dev *device;

	device = sdev->hostdata;
	device->devtype = sdev->type;

	if (pqi_is_tape_changer_device(device) && device->ignore_device) {
		rc = -ENXIO;
		device->ignore_device = false;
	}

	return rc;
}

static void pqi_slave_destroy(struct scsi_device *sdev)
{
	struct pqi_ctrl_info *ctrl_info;
	struct pqi_scsi_dev *device;
	int mutex_acquired;
	unsigned long flags;

	ctrl_info = shost_to_hba(sdev->host);

	mutex_acquired = mutex_trylock(&ctrl_info->scan_mutex);
	if (!mutex_acquired)
		return;

	device = sdev->hostdata;
	if (!device) {
		mutex_unlock(&ctrl_info->scan_mutex);
		return;
	}

	device->lun_count--;
	if (device->lun_count > 0) {
		mutex_unlock(&ctrl_info->scan_mutex);
		return;
	}

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);
	list_del(&device->scsi_device_list_entry);
	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	mutex_unlock(&ctrl_info->scan_mutex);

	pqi_dev_info(ctrl_info, "removed", device);
	pqi_free_device(device);
}

static int pqi_getpciinfo_ioctl(struct pqi_ctrl_info *ctrl_info, void __user *arg)
{
	struct pci_dev *pci_dev;
	u32 subsystem_vendor;
	u32 subsystem_device;
	cciss_pci_info_struct pci_info;

	if (!arg)
		return -EINVAL;

	pci_dev = ctrl_info->pci_dev;

	pci_info.domain = pci_domain_nr(pci_dev->bus);
	pci_info.bus = pci_dev->bus->number;
	pci_info.dev_fn = pci_dev->devfn;
	subsystem_vendor = pci_dev->subsystem_vendor;
	subsystem_device = pci_dev->subsystem_device;
	pci_info.board_id = ((subsystem_device << 16) & 0xffff0000) | subsystem_vendor;

	if (copy_to_user(arg, &pci_info, sizeof(pci_info)))
		return -EFAULT;

	return 0;
}

static int pqi_getdrivver_ioctl(void __user *arg)
{
	u32 version;

	if (!arg)
		return -EINVAL;

	version = (DRIVER_MAJOR << 28) | (DRIVER_MINOR << 24) |
		(DRIVER_RELEASE << 16) | DRIVER_REVISION;

	if (copy_to_user(arg, &version, sizeof(version)))
		return -EFAULT;

	return 0;
}

struct ciss_error_info {
	u8	scsi_status;
	int	command_status;
	size_t	sense_data_length;
};

static void pqi_error_info_to_ciss(struct pqi_raid_error_info *pqi_error_info,
	struct ciss_error_info *ciss_error_info)
{
	int ciss_cmd_status;
	size_t sense_data_length;

	switch (pqi_error_info->data_out_result) {
	case PQI_DATA_IN_OUT_GOOD:
		ciss_cmd_status = CISS_CMD_STATUS_SUCCESS;
		break;
	case PQI_DATA_IN_OUT_UNDERFLOW:
		ciss_cmd_status = CISS_CMD_STATUS_DATA_UNDERRUN;
		break;
	case PQI_DATA_IN_OUT_BUFFER_OVERFLOW:
		ciss_cmd_status = CISS_CMD_STATUS_DATA_OVERRUN;
		break;
	case PQI_DATA_IN_OUT_PROTOCOL_ERROR:
	case PQI_DATA_IN_OUT_BUFFER_ERROR:
	case PQI_DATA_IN_OUT_BUFFER_OVERFLOW_DESCRIPTOR_AREA:
	case PQI_DATA_IN_OUT_BUFFER_OVERFLOW_BRIDGE:
	case PQI_DATA_IN_OUT_ERROR:
		ciss_cmd_status = CISS_CMD_STATUS_PROTOCOL_ERROR;
		break;
	case PQI_DATA_IN_OUT_HARDWARE_ERROR:
	case PQI_DATA_IN_OUT_PCIE_FABRIC_ERROR:
	case PQI_DATA_IN_OUT_PCIE_COMPLETION_TIMEOUT:
	case PQI_DATA_IN_OUT_PCIE_COMPLETER_ABORT_RECEIVED:
	case PQI_DATA_IN_OUT_PCIE_UNSUPPORTED_REQUEST_RECEIVED:
	case PQI_DATA_IN_OUT_PCIE_ECRC_CHECK_FAILED:
	case PQI_DATA_IN_OUT_PCIE_UNSUPPORTED_REQUEST:
	case PQI_DATA_IN_OUT_PCIE_ACS_VIOLATION:
	case PQI_DATA_IN_OUT_PCIE_TLP_PREFIX_BLOCKED:
	case PQI_DATA_IN_OUT_PCIE_POISONED_MEMORY_READ:
		ciss_cmd_status = CISS_CMD_STATUS_HARDWARE_ERROR;
		break;
	case PQI_DATA_IN_OUT_UNSOLICITED_ABORT:
		ciss_cmd_status = CISS_CMD_STATUS_UNSOLICITED_ABORT;
		break;
	case PQI_DATA_IN_OUT_ABORTED:
		ciss_cmd_status = CISS_CMD_STATUS_ABORTED;
		break;
	case PQI_DATA_IN_OUT_TIMEOUT:
		ciss_cmd_status = CISS_CMD_STATUS_TIMEOUT;
		break;
	default:
		ciss_cmd_status = CISS_CMD_STATUS_TARGET_STATUS;
		break;
	}

	sense_data_length =
		get_unaligned_le16(&pqi_error_info->sense_data_length);
	if (sense_data_length == 0)
		sense_data_length =
		get_unaligned_le16(&pqi_error_info->response_data_length);
	if (sense_data_length)
		if (sense_data_length > sizeof(pqi_error_info->data))
			sense_data_length = sizeof(pqi_error_info->data);

	ciss_error_info->scsi_status = pqi_error_info->status;
	ciss_error_info->command_status = ciss_cmd_status;
	ciss_error_info->sense_data_length = sense_data_length;
}

static int pqi_passthru_ioctl(struct pqi_ctrl_info *ctrl_info, void __user *arg)
{
	int rc;
	char *kernel_buffer = NULL;
	u16 iu_length;
	size_t sense_data_length;
	IOCTL_Command_struct iocommand;
	struct pqi_raid_path_request request;
	struct pqi_raid_error_info pqi_error_info;
	struct ciss_error_info ciss_error_info;

	if (pqi_ctrl_offline(ctrl_info))
		return -ENXIO;
	if (pqi_ofa_in_progress(ctrl_info) && pqi_ctrl_blocked(ctrl_info))
		return -EBUSY;
	if (!arg)
		return -EINVAL;
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	if (copy_from_user(&iocommand, arg, sizeof(iocommand)))
		return -EFAULT;
	if (iocommand.buf_size < 1 &&
		iocommand.Request.Type.Direction != XFER_NONE)
		return -EINVAL;
	if (iocommand.Request.CDBLen > sizeof(request.cdb))
		return -EINVAL;
	if (iocommand.Request.Type.Type != TYPE_CMD)
		return -EINVAL;

	switch (iocommand.Request.Type.Direction) {
	case XFER_NONE:
	case XFER_WRITE:
	case XFER_READ:
	case XFER_READ | XFER_WRITE:
		break;
	default:
		return -EINVAL;
	}

	if (iocommand.buf_size > 0) {
		kernel_buffer = kmalloc(iocommand.buf_size, GFP_KERNEL);
		if (!kernel_buffer)
			return -ENOMEM;
		if (iocommand.Request.Type.Direction & XFER_WRITE) {
			if (copy_from_user(kernel_buffer, iocommand.buf,
				iocommand.buf_size)) {
				rc = -EFAULT;
				goto out;
			}
		} else {
			memset(kernel_buffer, 0, iocommand.buf_size);
		}
	}

	memset(&request, 0, sizeof(request));

	request.header.iu_type = PQI_REQUEST_IU_RAID_PATH_IO;
	iu_length = offsetof(struct pqi_raid_path_request, sg_descriptors) -
		PQI_REQUEST_HEADER_LENGTH;
	memcpy(request.lun_number, iocommand.LUN_info.LunAddrBytes,
		sizeof(request.lun_number));
	memcpy(request.cdb, iocommand.Request.CDB, iocommand.Request.CDBLen);
	request.additional_cdb_bytes_usage = SOP_ADDITIONAL_CDB_BYTES_0;

	switch (iocommand.Request.Type.Direction) {
	case XFER_NONE:
		request.data_direction = SOP_NO_DIRECTION_FLAG;
		break;
	case XFER_WRITE:
		request.data_direction = SOP_WRITE_FLAG;
		break;
	case XFER_READ:
		request.data_direction = SOP_READ_FLAG;
		break;
	case XFER_READ | XFER_WRITE:
		request.data_direction = SOP_BIDIRECTIONAL;
		break;
	}

	request.task_attribute = SOP_TASK_ATTRIBUTE_SIMPLE;

	if (iocommand.buf_size > 0) {
		put_unaligned_le32(iocommand.buf_size, &request.buffer_length);

		rc = pqi_map_single(ctrl_info->pci_dev,
			&request.sg_descriptors[0], kernel_buffer,
			iocommand.buf_size, DMA_BIDIRECTIONAL);
		if (rc)
			goto out;

		iu_length += sizeof(request.sg_descriptors[0]);
	}

	put_unaligned_le16(iu_length, &request.header.iu_length);

	if (ctrl_info->raid_iu_timeout_supported)
		put_unaligned_le32(iocommand.Request.Timeout, &request.timeout);

	rc = pqi_submit_raid_request_synchronous(ctrl_info, &request.header,
		PQI_SYNC_FLAGS_INTERRUPTABLE, &pqi_error_info);

	if (iocommand.buf_size > 0)
		pqi_pci_unmap(ctrl_info->pci_dev, request.sg_descriptors, 1,
			DMA_BIDIRECTIONAL);

	memset(&iocommand.error_info, 0, sizeof(iocommand.error_info));

	if (rc == 0) {
		pqi_error_info_to_ciss(&pqi_error_info, &ciss_error_info);
		iocommand.error_info.ScsiStatus = ciss_error_info.scsi_status;
		iocommand.error_info.CommandStatus =
			ciss_error_info.command_status;
		sense_data_length = ciss_error_info.sense_data_length;
		if (sense_data_length) {
			if (sense_data_length >
				sizeof(iocommand.error_info.SenseInfo))
				sense_data_length =
					sizeof(iocommand.error_info.SenseInfo);
			memcpy(iocommand.error_info.SenseInfo,
				pqi_error_info.data, sense_data_length);
			iocommand.error_info.SenseLen = sense_data_length;
		}
	}

	if (copy_to_user(arg, &iocommand, sizeof(iocommand))) {
		rc = -EFAULT;
		goto out;
	}

	if (rc == 0 && iocommand.buf_size > 0 &&
		(iocommand.Request.Type.Direction & XFER_READ)) {
		if (copy_to_user(iocommand.buf, kernel_buffer,
			iocommand.buf_size)) {
			rc = -EFAULT;
		}
	}

out:
	kfree(kernel_buffer);

	return rc;
}

static int pqi_ioctl(struct scsi_device *sdev, unsigned int cmd,
		     void __user *arg)
{
	int rc;
	struct pqi_ctrl_info *ctrl_info;

	ctrl_info = shost_to_hba(sdev->host);

	switch (cmd) {
	case CCISS_DEREGDISK:
	case CCISS_REGNEWDISK:
	case CCISS_REGNEWD:
		rc = pqi_scan_scsi_devices(ctrl_info);
		break;
	case CCISS_GETPCIINFO:
		rc = pqi_getpciinfo_ioctl(ctrl_info, arg);
		break;
	case CCISS_GETDRIVVER:
		rc = pqi_getdrivver_ioctl(arg);
		break;
	case CCISS_PASSTHRU:
		rc = pqi_passthru_ioctl(ctrl_info, arg);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static ssize_t pqi_firmware_version_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct Scsi_Host *shost;
	struct pqi_ctrl_info *ctrl_info;

	shost = class_to_shost(dev);
	ctrl_info = shost_to_hba(shost);

	return scnprintf(buffer, PAGE_SIZE, "%s\n", ctrl_info->firmware_version);
}

static ssize_t pqi_serial_number_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct Scsi_Host *shost;
	struct pqi_ctrl_info *ctrl_info;

	shost = class_to_shost(dev);
	ctrl_info = shost_to_hba(shost);

	return scnprintf(buffer, PAGE_SIZE, "%s\n", ctrl_info->serial_number);
}

static ssize_t pqi_model_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct Scsi_Host *shost;
	struct pqi_ctrl_info *ctrl_info;

	shost = class_to_shost(dev);
	ctrl_info = shost_to_hba(shost);

	return scnprintf(buffer, PAGE_SIZE, "%s\n", ctrl_info->model);
}

static ssize_t pqi_vendor_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct Scsi_Host *shost;
	struct pqi_ctrl_info *ctrl_info;

	shost = class_to_shost(dev);
	ctrl_info = shost_to_hba(shost);

	return scnprintf(buffer, PAGE_SIZE, "%s\n", ctrl_info->vendor);
}

static ssize_t pqi_host_rescan_store(struct device *dev,
	struct device_attribute *attr, const char *buffer, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);

	pqi_scan_start(shost);

	return count;
}

static ssize_t pqi_lockup_action_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	int count = 0;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pqi_lockup_actions); i++) {
		if (pqi_lockup_actions[i].action == pqi_lockup_action)
			count += scnprintf(buffer + count, PAGE_SIZE - count,
				"[%s] ", pqi_lockup_actions[i].name);
		else
			count += scnprintf(buffer + count, PAGE_SIZE - count,
				"%s ", pqi_lockup_actions[i].name);
	}

	count += scnprintf(buffer + count, PAGE_SIZE - count, "\n");

	return count;
}

static ssize_t pqi_lockup_action_store(struct device *dev,
	struct device_attribute *attr, const char *buffer, size_t count)
{
	unsigned int i;
	char *action_name;
	char action_name_buffer[32];

	strscpy(action_name_buffer, buffer, sizeof(action_name_buffer));
	action_name = strstrip(action_name_buffer);

	for (i = 0; i < ARRAY_SIZE(pqi_lockup_actions); i++) {
		if (strcmp(action_name, pqi_lockup_actions[i].name) == 0) {
			pqi_lockup_action = pqi_lockup_actions[i].action;
			return count;
		}
	}

	return -EINVAL;
}

static ssize_t pqi_host_enable_stream_detection_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct pqi_ctrl_info *ctrl_info = shost_to_hba(shost);

	return scnprintf(buffer, 10, "%x\n",
			ctrl_info->enable_stream_detection);
}

static ssize_t pqi_host_enable_stream_detection_store(struct device *dev,
	struct device_attribute *attr, const char *buffer, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct pqi_ctrl_info *ctrl_info = shost_to_hba(shost);
	u8 set_stream_detection = 0;

	if (kstrtou8(buffer, 0, &set_stream_detection))
		return -EINVAL;

	if (set_stream_detection > 0)
		set_stream_detection = 1;

	ctrl_info->enable_stream_detection = set_stream_detection;

	return count;
}

static ssize_t pqi_host_enable_r5_writes_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct pqi_ctrl_info *ctrl_info = shost_to_hba(shost);

	return scnprintf(buffer, 10, "%x\n", ctrl_info->enable_r5_writes);
}

static ssize_t pqi_host_enable_r5_writes_store(struct device *dev,
	struct device_attribute *attr, const char *buffer, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct pqi_ctrl_info *ctrl_info = shost_to_hba(shost);
	u8 set_r5_writes = 0;

	if (kstrtou8(buffer, 0, &set_r5_writes))
		return -EINVAL;

	if (set_r5_writes > 0)
		set_r5_writes = 1;

	ctrl_info->enable_r5_writes = set_r5_writes;

	return count;
}

static ssize_t pqi_host_enable_r6_writes_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct pqi_ctrl_info *ctrl_info = shost_to_hba(shost);

	return scnprintf(buffer, 10, "%x\n", ctrl_info->enable_r6_writes);
}

static ssize_t pqi_host_enable_r6_writes_store(struct device *dev,
	struct device_attribute *attr, const char *buffer, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct pqi_ctrl_info *ctrl_info = shost_to_hba(shost);
	u8 set_r6_writes = 0;

	if (kstrtou8(buffer, 0, &set_r6_writes))
		return -EINVAL;

	if (set_r6_writes > 0)
		set_r6_writes = 1;

	ctrl_info->enable_r6_writes = set_r6_writes;

	return count;
}

static DEVICE_STRING_ATTR_RO(driver_version, 0444,
	DRIVER_VERSION BUILD_TIMESTAMP);
static DEVICE_ATTR(firmware_version, 0444, pqi_firmware_version_show, NULL);
static DEVICE_ATTR(model, 0444, pqi_model_show, NULL);
static DEVICE_ATTR(serial_number, 0444, pqi_serial_number_show, NULL);
static DEVICE_ATTR(vendor, 0444, pqi_vendor_show, NULL);
static DEVICE_ATTR(rescan, 0200, NULL, pqi_host_rescan_store);
static DEVICE_ATTR(lockup_action, 0644, pqi_lockup_action_show,
	pqi_lockup_action_store);
static DEVICE_ATTR(enable_stream_detection, 0644,
	pqi_host_enable_stream_detection_show,
	pqi_host_enable_stream_detection_store);
static DEVICE_ATTR(enable_r5_writes, 0644,
	pqi_host_enable_r5_writes_show, pqi_host_enable_r5_writes_store);
static DEVICE_ATTR(enable_r6_writes, 0644,
	pqi_host_enable_r6_writes_show, pqi_host_enable_r6_writes_store);

static struct attribute *pqi_shost_attrs[] = {
	&dev_attr_driver_version.attr.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_model.attr,
	&dev_attr_serial_number.attr,
	&dev_attr_vendor.attr,
	&dev_attr_rescan.attr,
	&dev_attr_lockup_action.attr,
	&dev_attr_enable_stream_detection.attr,
	&dev_attr_enable_r5_writes.attr,
	&dev_attr_enable_r6_writes.attr,
	NULL
};

ATTRIBUTE_GROUPS(pqi_shost);

static ssize_t pqi_unique_id_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct pqi_ctrl_info *ctrl_info;
	struct scsi_device *sdev;
	struct pqi_scsi_dev *device;
	unsigned long flags;
	u8 unique_id[16];

	sdev = to_scsi_device(dev);
	ctrl_info = shost_to_hba(sdev->host);

	if (pqi_ctrl_offline(ctrl_info))
		return -ENODEV;

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	device = sdev->hostdata;
	if (!device) {
		spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
		return -ENODEV;
	}

	if (device->is_physical_device)
		memcpy(unique_id, device->wwid, sizeof(device->wwid));
	else
		memcpy(unique_id, device->volume_id, sizeof(device->volume_id));

	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	return scnprintf(buffer, PAGE_SIZE,
		"%02X%02X%02X%02X%02X%02X%02X%02X"
		"%02X%02X%02X%02X%02X%02X%02X%02X\n",
		unique_id[0], unique_id[1], unique_id[2], unique_id[3],
		unique_id[4], unique_id[5], unique_id[6], unique_id[7],
		unique_id[8], unique_id[9], unique_id[10], unique_id[11],
		unique_id[12], unique_id[13], unique_id[14], unique_id[15]);
}

static ssize_t pqi_lunid_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct pqi_ctrl_info *ctrl_info;
	struct scsi_device *sdev;
	struct pqi_scsi_dev *device;
	unsigned long flags;
	u8 lunid[8];

	sdev = to_scsi_device(dev);
	ctrl_info = shost_to_hba(sdev->host);

	if (pqi_ctrl_offline(ctrl_info))
		return -ENODEV;

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	device = sdev->hostdata;
	if (!device) {
		spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
		return -ENODEV;
	}

	memcpy(lunid, device->scsi3addr, sizeof(lunid));

	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	return scnprintf(buffer, PAGE_SIZE, "0x%8phN\n", lunid);
}

#define MAX_PATHS	8

static ssize_t pqi_path_info_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct pqi_ctrl_info *ctrl_info;
	struct scsi_device *sdev;
	struct pqi_scsi_dev *device;
	unsigned long flags;
	int i;
	int output_len = 0;
	u8 box;
	u8 bay;
	u8 path_map_index;
	char *active;
	u8 phys_connector[2];

	sdev = to_scsi_device(dev);
	ctrl_info = shost_to_hba(sdev->host);

	if (pqi_ctrl_offline(ctrl_info))
		return -ENODEV;

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	device = sdev->hostdata;
	if (!device) {
		spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
		return -ENODEV;
	}

	bay = device->bay;
	for (i = 0; i < MAX_PATHS; i++) {
		path_map_index = 1 << i;
		if (i == device->active_path_index)
			active = "Active";
		else if (device->path_map & path_map_index)
			active = "Inactive";
		else
			continue;

		output_len += scnprintf(buf + output_len,
					PAGE_SIZE - output_len,
					"[%d:%d:%d:%d] %20.20s ",
					ctrl_info->scsi_host->host_no,
					device->bus, device->target,
					device->lun,
					scsi_device_type(device->devtype));

		if (device->devtype == TYPE_RAID ||
			pqi_is_logical_device(device))
			goto end_buffer;

		memcpy(&phys_connector, &device->phys_connector[i],
			sizeof(phys_connector));
		if (phys_connector[0] < '0')
			phys_connector[0] = '0';
		if (phys_connector[1] < '0')
			phys_connector[1] = '0';

		output_len += scnprintf(buf + output_len,
					PAGE_SIZE - output_len,
					"PORT: %.2s ", phys_connector);

		box = device->box[i];
		if (box != 0 && box != 0xFF)
			output_len += scnprintf(buf + output_len,
						PAGE_SIZE - output_len,
						"BOX: %hhu ", box);

		if ((device->devtype == TYPE_DISK ||
			device->devtype == TYPE_ZBC) &&
			pqi_expose_device(device))
			output_len += scnprintf(buf + output_len,
						PAGE_SIZE - output_len,
						"BAY: %hhu ", bay);

end_buffer:
		output_len += scnprintf(buf + output_len,
					PAGE_SIZE - output_len,
					"%s\n", active);
	}

	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	return output_len;
}

static ssize_t pqi_sas_address_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct pqi_ctrl_info *ctrl_info;
	struct scsi_device *sdev;
	struct pqi_scsi_dev *device;
	unsigned long flags;
	u64 sas_address;

	sdev = to_scsi_device(dev);
	ctrl_info = shost_to_hba(sdev->host);

	if (pqi_ctrl_offline(ctrl_info))
		return -ENODEV;

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	device = sdev->hostdata;
	if (!device) {
		spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
		return -ENODEV;
	}

	sas_address = device->sas_address;

	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	return scnprintf(buffer, PAGE_SIZE, "0x%016llx\n", sas_address);
}

static ssize_t pqi_ssd_smart_path_enabled_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct pqi_ctrl_info *ctrl_info;
	struct scsi_device *sdev;
	struct pqi_scsi_dev *device;
	unsigned long flags;

	sdev = to_scsi_device(dev);
	ctrl_info = shost_to_hba(sdev->host);

	if (pqi_ctrl_offline(ctrl_info))
		return -ENODEV;

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	device = sdev->hostdata;
	if (!device) {
		spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
		return -ENODEV;
	}

	buffer[0] = device->raid_bypass_enabled ? '1' : '0';
	buffer[1] = '\n';
	buffer[2] = '\0';

	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	return 2;
}

static ssize_t pqi_raid_level_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct pqi_ctrl_info *ctrl_info;
	struct scsi_device *sdev;
	struct pqi_scsi_dev *device;
	unsigned long flags;
	char *raid_level;

	sdev = to_scsi_device(dev);
	ctrl_info = shost_to_hba(sdev->host);

	if (pqi_ctrl_offline(ctrl_info))
		return -ENODEV;

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	device = sdev->hostdata;
	if (!device) {
		spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
		return -ENODEV;
	}

	if (pqi_is_logical_device(device) && device->devtype == TYPE_DISK)
		raid_level = pqi_raid_level_to_string(device->raid_level);
	else
		raid_level = "N/A";

	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	return scnprintf(buffer, PAGE_SIZE, "%s\n", raid_level);
}

static ssize_t pqi_raid_bypass_cnt_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct pqi_ctrl_info *ctrl_info;
	struct scsi_device *sdev;
	struct pqi_scsi_dev *device;
	unsigned long flags;
	u64 raid_bypass_cnt;
	int cpu;

	sdev = to_scsi_device(dev);
	ctrl_info = shost_to_hba(sdev->host);

	if (pqi_ctrl_offline(ctrl_info))
		return -ENODEV;

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	device = sdev->hostdata;
	if (!device) {
		spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
		return -ENODEV;
	}

	raid_bypass_cnt = 0;

	if (device->raid_io_stats) {
		for_each_online_cpu(cpu) {
			raid_bypass_cnt += per_cpu_ptr(device->raid_io_stats, cpu)->raid_bypass_cnt;
		}
	}

	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	return scnprintf(buffer, PAGE_SIZE, "0x%llx\n", raid_bypass_cnt);
}

static ssize_t pqi_sas_ncq_prio_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pqi_ctrl_info *ctrl_info;
	struct scsi_device *sdev;
	struct pqi_scsi_dev *device;
	unsigned long flags;
	int output_len = 0;

	sdev = to_scsi_device(dev);
	ctrl_info = shost_to_hba(sdev->host);

	if (pqi_ctrl_offline(ctrl_info))
		return -ENODEV;

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	device = sdev->hostdata;
	if (!device) {
		spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
		return -ENODEV;
	}

	output_len = snprintf(buf, PAGE_SIZE, "%d\n",
				device->ncq_prio_enable);
	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	return output_len;
}

static ssize_t pqi_sas_ncq_prio_enable_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct pqi_ctrl_info *ctrl_info;
	struct scsi_device *sdev;
	struct pqi_scsi_dev *device;
	unsigned long flags;
	u8 ncq_prio_enable = 0;

	if (kstrtou8(buf, 0, &ncq_prio_enable))
		return -EINVAL;

	sdev = to_scsi_device(dev);
	ctrl_info = shost_to_hba(sdev->host);

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	device = sdev->hostdata;

	if (!device) {
		spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
		return -ENODEV;
	}

	if (!device->ncq_prio_support) {
		spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
		return -EINVAL;
	}

	device->ncq_prio_enable = ncq_prio_enable;

	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	return  strlen(buf);
}

static ssize_t pqi_numa_node_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct scsi_device *sdev;
	struct pqi_ctrl_info *ctrl_info;

	sdev = to_scsi_device(dev);
	ctrl_info = shost_to_hba(sdev->host);

	return scnprintf(buffer, PAGE_SIZE, "%d\n", ctrl_info->numa_node);
}

static ssize_t pqi_write_stream_cnt_show(struct device *dev,
	struct device_attribute *attr, char *buffer)
{
	struct pqi_ctrl_info *ctrl_info;
	struct scsi_device *sdev;
	struct pqi_scsi_dev *device;
	unsigned long flags;
	u64 write_stream_cnt;
	int cpu;

	sdev = to_scsi_device(dev);
	ctrl_info = shost_to_hba(sdev->host);

	if (pqi_ctrl_offline(ctrl_info))
		return -ENODEV;

	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);

	device = sdev->hostdata;
	if (!device) {
		spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);
		return -ENODEV;
	}

	write_stream_cnt = 0;

	if (device->raid_io_stats) {
		for_each_online_cpu(cpu) {
			write_stream_cnt += per_cpu_ptr(device->raid_io_stats, cpu)->write_stream_cnt;
		}
	}

	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	return scnprintf(buffer, PAGE_SIZE, "0x%llx\n", write_stream_cnt);
}

static DEVICE_ATTR(lunid, 0444, pqi_lunid_show, NULL);
static DEVICE_ATTR(unique_id, 0444, pqi_unique_id_show, NULL);
static DEVICE_ATTR(path_info, 0444, pqi_path_info_show, NULL);
static DEVICE_ATTR(sas_address, 0444, pqi_sas_address_show, NULL);
static DEVICE_ATTR(ssd_smart_path_enabled, 0444, pqi_ssd_smart_path_enabled_show, NULL);
static DEVICE_ATTR(raid_level, 0444, pqi_raid_level_show, NULL);
static DEVICE_ATTR(raid_bypass_cnt, 0444, pqi_raid_bypass_cnt_show, NULL);
static DEVICE_ATTR(sas_ncq_prio_enable, 0644,
		pqi_sas_ncq_prio_enable_show, pqi_sas_ncq_prio_enable_store);
static DEVICE_ATTR(numa_node, 0444, pqi_numa_node_show, NULL);
static DEVICE_ATTR(write_stream_cnt, 0444, pqi_write_stream_cnt_show, NULL);

static struct attribute *pqi_sdev_attrs[] = {
	&dev_attr_lunid.attr,
	&dev_attr_unique_id.attr,
	&dev_attr_path_info.attr,
	&dev_attr_sas_address.attr,
	&dev_attr_ssd_smart_path_enabled.attr,
	&dev_attr_raid_level.attr,
	&dev_attr_raid_bypass_cnt.attr,
	&dev_attr_sas_ncq_prio_enable.attr,
	&dev_attr_numa_node.attr,
	&dev_attr_write_stream_cnt.attr,
	NULL
};

ATTRIBUTE_GROUPS(pqi_sdev);

static const struct scsi_host_template pqi_driver_template = {
	.module = THIS_MODULE,
	.name = DRIVER_NAME_SHORT,
	.proc_name = DRIVER_NAME_SHORT,
	.queuecommand = pqi_scsi_queue_command,
	.scan_start = pqi_scan_start,
	.scan_finished = pqi_scan_finished,
	.this_id = -1,
	.eh_device_reset_handler = pqi_eh_device_reset_handler,
	.eh_abort_handler = pqi_eh_abort_handler,
	.ioctl = pqi_ioctl,
	.slave_alloc = pqi_slave_alloc,
	.slave_configure = pqi_slave_configure,
	.slave_destroy = pqi_slave_destroy,
	.map_queues = pqi_map_queues,
	.sdev_groups = pqi_sdev_groups,
	.shost_groups = pqi_shost_groups,
	.cmd_size = sizeof(struct pqi_cmd_priv),
};

static int pqi_register_scsi(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	struct Scsi_Host *shost;

	shost = scsi_host_alloc(&pqi_driver_template, sizeof(ctrl_info));
	if (!shost) {
		dev_err(&ctrl_info->pci_dev->dev, "scsi_host_alloc failed\n");
		return -ENOMEM;
	}

	shost->io_port = 0;
	shost->n_io_port = 0;
	shost->this_id = -1;
	shost->max_channel = PQI_MAX_BUS;
	shost->max_cmd_len = MAX_COMMAND_SIZE;
	shost->max_lun = PQI_MAX_LUNS_PER_DEVICE;
	shost->max_id = ~0;
	shost->max_sectors = ctrl_info->max_sectors;
	shost->can_queue = ctrl_info->scsi_ml_can_queue;
	shost->cmd_per_lun = shost->can_queue;
	shost->sg_tablesize = ctrl_info->sg_tablesize;
	shost->transportt = pqi_sas_transport_template;
	shost->irq = pci_irq_vector(ctrl_info->pci_dev, 0);
	shost->unique_id = shost->irq;
	shost->nr_hw_queues = ctrl_info->num_queue_groups;
	shost->host_tagset = 1;
	shost->hostdata[0] = (unsigned long)ctrl_info;

	rc = scsi_add_host(shost, &ctrl_info->pci_dev->dev);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev, "scsi_add_host failed\n");
		goto free_host;
	}

	rc = pqi_add_sas_host(shost, ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev, "add SAS host failed\n");
		goto remove_host;
	}

	ctrl_info->scsi_host = shost;

	return 0;

remove_host:
	scsi_remove_host(shost);
free_host:
	scsi_host_put(shost);

	return rc;
}

static void pqi_unregister_scsi(struct pqi_ctrl_info *ctrl_info)
{
	struct Scsi_Host *shost;

	pqi_delete_sas_host(ctrl_info);

	shost = ctrl_info->scsi_host;
	if (!shost)
		return;

	scsi_remove_host(shost);
	scsi_host_put(shost);
}

static int pqi_wait_for_pqi_reset_completion(struct pqi_ctrl_info *ctrl_info)
{
	int rc = 0;
	struct pqi_device_registers __iomem *pqi_registers;
	unsigned long timeout;
	unsigned int timeout_msecs;
	union pqi_reset_register reset_reg;

	pqi_registers = ctrl_info->pqi_registers;
	timeout_msecs = readw(&pqi_registers->max_reset_timeout) * 100;
	timeout = msecs_to_jiffies(timeout_msecs) + jiffies;

	while (1) {
		msleep(PQI_RESET_POLL_INTERVAL_MSECS);
		reset_reg.all_bits = readl(&pqi_registers->device_reset);
		if (reset_reg.bits.reset_action == PQI_RESET_ACTION_COMPLETED)
			break;
		if (!sis_is_firmware_running(ctrl_info)) {
			rc = -ENXIO;
			break;
		}
		if (time_after(jiffies, timeout)) {
			rc = -ETIMEDOUT;
			break;
		}
	}

	return rc;
}

static int pqi_reset(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	union pqi_reset_register reset_reg;

	if (ctrl_info->pqi_reset_quiesce_supported) {
		rc = sis_pqi_reset_quiesce(ctrl_info);
		if (rc) {
			dev_err(&ctrl_info->pci_dev->dev,
				"PQI reset failed during quiesce with error %d\n", rc);
			return rc;
		}
	}

	reset_reg.all_bits = 0;
	reset_reg.bits.reset_type = PQI_RESET_TYPE_HARD_RESET;
	reset_reg.bits.reset_action = PQI_RESET_ACTION_RESET;

	writel(reset_reg.all_bits, &ctrl_info->pqi_registers->device_reset);

	rc = pqi_wait_for_pqi_reset_completion(ctrl_info);
	if (rc)
		dev_err(&ctrl_info->pci_dev->dev,
			"PQI reset failed with error %d\n", rc);

	return rc;
}

static int pqi_get_ctrl_serial_number(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	struct bmic_sense_subsystem_info *sense_info;

	sense_info = kzalloc(sizeof(*sense_info), GFP_KERNEL);
	if (!sense_info)
		return -ENOMEM;

	rc = pqi_sense_subsystem_info(ctrl_info, sense_info);
	if (rc)
		goto out;

	memcpy(ctrl_info->serial_number, sense_info->ctrl_serial_number,
		sizeof(sense_info->ctrl_serial_number));
	ctrl_info->serial_number[sizeof(sense_info->ctrl_serial_number)] = '\0';

out:
	kfree(sense_info);

	return rc;
}

static int pqi_get_ctrl_product_details(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	struct bmic_identify_controller *identify;

	identify = kmalloc(sizeof(*identify), GFP_KERNEL);
	if (!identify)
		return -ENOMEM;

	rc = pqi_identify_controller(ctrl_info, identify);
	if (rc)
		goto out;

	if (get_unaligned_le32(&identify->extra_controller_flags) &
		BMIC_IDENTIFY_EXTRA_FLAGS_LONG_FW_VERSION_SUPPORTED) {
		memcpy(ctrl_info->firmware_version,
			identify->firmware_version_long,
			sizeof(identify->firmware_version_long));
	} else {
		memcpy(ctrl_info->firmware_version,
			identify->firmware_version_short,
			sizeof(identify->firmware_version_short));
		ctrl_info->firmware_version
			[sizeof(identify->firmware_version_short)] = '\0';
		snprintf(ctrl_info->firmware_version +
			strlen(ctrl_info->firmware_version),
			sizeof(ctrl_info->firmware_version) -
			sizeof(identify->firmware_version_short),
			"-%u",
			get_unaligned_le16(&identify->firmware_build_number));
	}

	memcpy(ctrl_info->model, identify->product_id,
		sizeof(identify->product_id));
	ctrl_info->model[sizeof(identify->product_id)] = '\0';

	memcpy(ctrl_info->vendor, identify->vendor_id,
		sizeof(identify->vendor_id));
	ctrl_info->vendor[sizeof(identify->vendor_id)] = '\0';

	dev_info(&ctrl_info->pci_dev->dev,
		"Firmware version: %s\n", ctrl_info->firmware_version);

out:
	kfree(identify);

	return rc;
}

struct pqi_config_table_section_info {
	struct pqi_ctrl_info *ctrl_info;
	void		*section;
	u32		section_offset;
	void __iomem	*section_iomem_addr;
};

static inline bool pqi_is_firmware_feature_supported(
	struct pqi_config_table_firmware_features *firmware_features,
	unsigned int bit_position)
{
	unsigned int byte_index;

	byte_index = bit_position / BITS_PER_BYTE;

	if (byte_index >= le16_to_cpu(firmware_features->num_elements))
		return false;

	return firmware_features->features_supported[byte_index] &
		(1 << (bit_position % BITS_PER_BYTE)) ? true : false;
}

static inline bool pqi_is_firmware_feature_enabled(
	struct pqi_config_table_firmware_features *firmware_features,
	void __iomem *firmware_features_iomem_addr,
	unsigned int bit_position)
{
	unsigned int byte_index;
	u8 __iomem *features_enabled_iomem_addr;

	byte_index = (bit_position / BITS_PER_BYTE) +
		(le16_to_cpu(firmware_features->num_elements) * 2);

	features_enabled_iomem_addr = firmware_features_iomem_addr +
		offsetof(struct pqi_config_table_firmware_features,
			features_supported) + byte_index;

	return *((__force u8 *)features_enabled_iomem_addr) &
		(1 << (bit_position % BITS_PER_BYTE)) ? true : false;
}

static inline void pqi_request_firmware_feature(
	struct pqi_config_table_firmware_features *firmware_features,
	unsigned int bit_position)
{
	unsigned int byte_index;

	byte_index = (bit_position / BITS_PER_BYTE) +
		le16_to_cpu(firmware_features->num_elements);

	firmware_features->features_supported[byte_index] |=
		(1 << (bit_position % BITS_PER_BYTE));
}

static int pqi_config_table_update(struct pqi_ctrl_info *ctrl_info,
	u16 first_section, u16 last_section)
{
	struct pqi_vendor_general_request request;

	memset(&request, 0, sizeof(request));

	request.header.iu_type = PQI_REQUEST_IU_VENDOR_GENERAL;
	put_unaligned_le16(sizeof(request) - PQI_REQUEST_HEADER_LENGTH,
		&request.header.iu_length);
	put_unaligned_le16(PQI_VENDOR_GENERAL_CONFIG_TABLE_UPDATE,
		&request.function_code);
	put_unaligned_le16(first_section,
		&request.data.config_table_update.first_section);
	put_unaligned_le16(last_section,
		&request.data.config_table_update.last_section);

	return pqi_submit_raid_request_synchronous(ctrl_info, &request.header, 0, NULL);
}

static int pqi_enable_firmware_features(struct pqi_ctrl_info *ctrl_info,
	struct pqi_config_table_firmware_features *firmware_features,
	void __iomem *firmware_features_iomem_addr)
{
	void *features_requested;
	void __iomem *features_requested_iomem_addr;
	void __iomem *host_max_known_feature_iomem_addr;

	features_requested = firmware_features->features_supported +
		le16_to_cpu(firmware_features->num_elements);

	features_requested_iomem_addr = firmware_features_iomem_addr +
		(features_requested - (void *)firmware_features);

	memcpy_toio(features_requested_iomem_addr, features_requested,
		le16_to_cpu(firmware_features->num_elements));

	if (pqi_is_firmware_feature_supported(firmware_features,
		PQI_FIRMWARE_FEATURE_MAX_KNOWN_FEATURE)) {
		host_max_known_feature_iomem_addr =
			features_requested_iomem_addr +
			(le16_to_cpu(firmware_features->num_elements) * 2) +
			sizeof(__le16);
		writeb(PQI_FIRMWARE_FEATURE_MAXIMUM & 0xFF, host_max_known_feature_iomem_addr);
		writeb((PQI_FIRMWARE_FEATURE_MAXIMUM & 0xFF00) >> 8, host_max_known_feature_iomem_addr + 1);
	}

	return pqi_config_table_update(ctrl_info,
		PQI_CONFIG_TABLE_SECTION_FIRMWARE_FEATURES,
		PQI_CONFIG_TABLE_SECTION_FIRMWARE_FEATURES);
}

struct pqi_firmware_feature {
	char		*feature_name;
	unsigned int	feature_bit;
	bool		supported;
	bool		enabled;
	void (*feature_status)(struct pqi_ctrl_info *ctrl_info,
		struct pqi_firmware_feature *firmware_feature);
};

static void pqi_firmware_feature_status(struct pqi_ctrl_info *ctrl_info,
	struct pqi_firmware_feature *firmware_feature)
{
	if (!firmware_feature->supported) {
		dev_info(&ctrl_info->pci_dev->dev, "%s not supported by controller\n",
			firmware_feature->feature_name);
		return;
	}

	if (firmware_feature->enabled) {
		dev_info(&ctrl_info->pci_dev->dev,
			"%s enabled\n", firmware_feature->feature_name);
		return;
	}

	dev_err(&ctrl_info->pci_dev->dev, "failed to enable %s\n",
		firmware_feature->feature_name);
}

static void pqi_ctrl_update_feature_flags(struct pqi_ctrl_info *ctrl_info,
	struct pqi_firmware_feature *firmware_feature)
{
	switch (firmware_feature->feature_bit) {
	case PQI_FIRMWARE_FEATURE_RAID_1_WRITE_BYPASS:
		ctrl_info->enable_r1_writes = firmware_feature->enabled;
		break;
	case PQI_FIRMWARE_FEATURE_RAID_5_WRITE_BYPASS:
		ctrl_info->enable_r5_writes = firmware_feature->enabled;
		break;
	case PQI_FIRMWARE_FEATURE_RAID_6_WRITE_BYPASS:
		ctrl_info->enable_r6_writes = firmware_feature->enabled;
		break;
	case PQI_FIRMWARE_FEATURE_SOFT_RESET_HANDSHAKE:
		ctrl_info->soft_reset_handshake_supported =
			firmware_feature->enabled &&
			pqi_read_soft_reset_status(ctrl_info);
		break;
	case PQI_FIRMWARE_FEATURE_RAID_IU_TIMEOUT:
		ctrl_info->raid_iu_timeout_supported = firmware_feature->enabled;
		break;
	case PQI_FIRMWARE_FEATURE_TMF_IU_TIMEOUT:
		ctrl_info->tmf_iu_timeout_supported = firmware_feature->enabled;
		break;
	case PQI_FIRMWARE_FEATURE_FW_TRIAGE:
		ctrl_info->firmware_triage_supported = firmware_feature->enabled;
		pqi_save_fw_triage_setting(ctrl_info, firmware_feature->enabled);
		break;
	case PQI_FIRMWARE_FEATURE_RPL_EXTENDED_FORMAT_4_5:
		ctrl_info->rpl_extended_format_4_5_supported = firmware_feature->enabled;
		break;
	case PQI_FIRMWARE_FEATURE_MULTI_LUN_DEVICE_SUPPORT:
		ctrl_info->multi_lun_device_supported = firmware_feature->enabled;
		break;
	case PQI_FIRMWARE_FEATURE_CTRL_LOGGING:
		ctrl_info->ctrl_logging_supported = firmware_feature->enabled;
		break;
	}

	pqi_firmware_feature_status(ctrl_info, firmware_feature);
}

static inline void pqi_firmware_feature_update(struct pqi_ctrl_info *ctrl_info,
	struct pqi_firmware_feature *firmware_feature)
{
	if (firmware_feature->feature_status)
		firmware_feature->feature_status(ctrl_info, firmware_feature);
}

static DEFINE_MUTEX(pqi_firmware_features_mutex);

static struct pqi_firmware_feature pqi_firmware_features[] = {
	{
		.feature_name = "Online Firmware Activation",
		.feature_bit = PQI_FIRMWARE_FEATURE_OFA,
		.feature_status = pqi_firmware_feature_status,
	},
	{
		.feature_name = "Serial Management Protocol",
		.feature_bit = PQI_FIRMWARE_FEATURE_SMP,
		.feature_status = pqi_firmware_feature_status,
	},
	{
		.feature_name = "Maximum Known Feature",
		.feature_bit = PQI_FIRMWARE_FEATURE_MAX_KNOWN_FEATURE,
		.feature_status = pqi_firmware_feature_status,
	},
	{
		.feature_name = "RAID 0 Read Bypass",
		.feature_bit = PQI_FIRMWARE_FEATURE_RAID_0_READ_BYPASS,
		.feature_status = pqi_firmware_feature_status,
	},
	{
		.feature_name = "RAID 1 Read Bypass",
		.feature_bit = PQI_FIRMWARE_FEATURE_RAID_1_READ_BYPASS,
		.feature_status = pqi_firmware_feature_status,
	},
	{
		.feature_name = "RAID 5 Read Bypass",
		.feature_bit = PQI_FIRMWARE_FEATURE_RAID_5_READ_BYPASS,
		.feature_status = pqi_firmware_feature_status,
	},
	{
		.feature_name = "RAID 6 Read Bypass",
		.feature_bit = PQI_FIRMWARE_FEATURE_RAID_6_READ_BYPASS,
		.feature_status = pqi_firmware_feature_status,
	},
	{
		.feature_name = "RAID 0 Write Bypass",
		.feature_bit = PQI_FIRMWARE_FEATURE_RAID_0_WRITE_BYPASS,
		.feature_status = pqi_firmware_feature_status,
	},
	{
		.feature_name = "RAID 1 Write Bypass",
		.feature_bit = PQI_FIRMWARE_FEATURE_RAID_1_WRITE_BYPASS,
		.feature_status = pqi_ctrl_update_feature_flags,
	},
	{
		.feature_name = "RAID 5 Write Bypass",
		.feature_bit = PQI_FIRMWARE_FEATURE_RAID_5_WRITE_BYPASS,
		.feature_status = pqi_ctrl_update_feature_flags,
	},
	{
		.feature_name = "RAID 6 Write Bypass",
		.feature_bit = PQI_FIRMWARE_FEATURE_RAID_6_WRITE_BYPASS,
		.feature_status = pqi_ctrl_update_feature_flags,
	},
	{
		.feature_name = "New Soft Reset Handshake",
		.feature_bit = PQI_FIRMWARE_FEATURE_SOFT_RESET_HANDSHAKE,
		.feature_status = pqi_ctrl_update_feature_flags,
	},
	{
		.feature_name = "RAID IU Timeout",
		.feature_bit = PQI_FIRMWARE_FEATURE_RAID_IU_TIMEOUT,
		.feature_status = pqi_ctrl_update_feature_flags,
	},
	{
		.feature_name = "TMF IU Timeout",
		.feature_bit = PQI_FIRMWARE_FEATURE_TMF_IU_TIMEOUT,
		.feature_status = pqi_ctrl_update_feature_flags,
	},
	{
		.feature_name = "RAID Bypass on encrypted logical volumes on NVMe",
		.feature_bit = PQI_FIRMWARE_FEATURE_RAID_BYPASS_ON_ENCRYPTED_NVME,
		.feature_status = pqi_firmware_feature_status,
	},
	{
		.feature_name = "Firmware Triage",
		.feature_bit = PQI_FIRMWARE_FEATURE_FW_TRIAGE,
		.feature_status = pqi_ctrl_update_feature_flags,
	},
	{
		.feature_name = "RPL Extended Formats 4 and 5",
		.feature_bit = PQI_FIRMWARE_FEATURE_RPL_EXTENDED_FORMAT_4_5,
		.feature_status = pqi_ctrl_update_feature_flags,
	},
	{
		.feature_name = "Multi-LUN Target",
		.feature_bit = PQI_FIRMWARE_FEATURE_MULTI_LUN_DEVICE_SUPPORT,
		.feature_status = pqi_ctrl_update_feature_flags,
	},
	{
		.feature_name = "Controller Data Logging",
		.feature_bit = PQI_FIRMWARE_FEATURE_CTRL_LOGGING,
		.feature_status = pqi_ctrl_update_feature_flags,
	},
};

static void pqi_process_firmware_features(
	struct pqi_config_table_section_info *section_info)
{
	int rc;
	struct pqi_ctrl_info *ctrl_info;
	struct pqi_config_table_firmware_features *firmware_features;
	void __iomem *firmware_features_iomem_addr;
	unsigned int i;
	unsigned int num_features_supported;

	ctrl_info = section_info->ctrl_info;
	firmware_features = section_info->section;
	firmware_features_iomem_addr = section_info->section_iomem_addr;

	for (i = 0, num_features_supported = 0;
		i < ARRAY_SIZE(pqi_firmware_features); i++) {
		if (pqi_is_firmware_feature_supported(firmware_features,
			pqi_firmware_features[i].feature_bit)) {
			pqi_firmware_features[i].supported = true;
			num_features_supported++;
		} else {
			pqi_firmware_feature_update(ctrl_info,
				&pqi_firmware_features[i]);
		}
	}

	if (num_features_supported == 0)
		return;

	for (i = 0; i < ARRAY_SIZE(pqi_firmware_features); i++) {
		if (!pqi_firmware_features[i].supported)
			continue;
		pqi_request_firmware_feature(firmware_features,
			pqi_firmware_features[i].feature_bit);
	}

	rc = pqi_enable_firmware_features(ctrl_info, firmware_features,
		firmware_features_iomem_addr);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"failed to enable firmware features in PQI configuration table\n");
		for (i = 0; i < ARRAY_SIZE(pqi_firmware_features); i++) {
			if (!pqi_firmware_features[i].supported)
				continue;
			pqi_firmware_feature_update(ctrl_info,
				&pqi_firmware_features[i]);
		}
		return;
	}

	for (i = 0; i < ARRAY_SIZE(pqi_firmware_features); i++) {
		if (!pqi_firmware_features[i].supported)
			continue;
		if (pqi_is_firmware_feature_enabled(firmware_features,
			firmware_features_iomem_addr,
			pqi_firmware_features[i].feature_bit)) {
				pqi_firmware_features[i].enabled = true;
		}
		pqi_firmware_feature_update(ctrl_info,
			&pqi_firmware_features[i]);
	}
}

static void pqi_init_firmware_features(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pqi_firmware_features); i++) {
		pqi_firmware_features[i].supported = false;
		pqi_firmware_features[i].enabled = false;
	}
}

static void pqi_process_firmware_features_section(
	struct pqi_config_table_section_info *section_info)
{
	mutex_lock(&pqi_firmware_features_mutex);
	pqi_init_firmware_features();
	pqi_process_firmware_features(section_info);
	mutex_unlock(&pqi_firmware_features_mutex);
}

/*
 * Reset all controller settings that can be initialized during the processing
 * of the PQI Configuration Table.
 */

static void pqi_ctrl_reset_config(struct pqi_ctrl_info *ctrl_info)
{
	ctrl_info->heartbeat_counter = NULL;
	ctrl_info->soft_reset_status = NULL;
	ctrl_info->soft_reset_handshake_supported = false;
	ctrl_info->enable_r1_writes = false;
	ctrl_info->enable_r5_writes = false;
	ctrl_info->enable_r6_writes = false;
	ctrl_info->raid_iu_timeout_supported = false;
	ctrl_info->tmf_iu_timeout_supported = false;
	ctrl_info->firmware_triage_supported = false;
	ctrl_info->rpl_extended_format_4_5_supported = false;
	ctrl_info->multi_lun_device_supported = false;
	ctrl_info->ctrl_logging_supported = false;
}

static int pqi_process_config_table(struct pqi_ctrl_info *ctrl_info)
{
	u32 table_length;
	u32 section_offset;
	bool firmware_feature_section_present;
	void __iomem *table_iomem_addr;
	struct pqi_config_table *config_table;
	struct pqi_config_table_section_header *section;
	struct pqi_config_table_section_info section_info;
	struct pqi_config_table_section_info feature_section_info = {0};

	table_length = ctrl_info->config_table_length;
	if (table_length == 0)
		return 0;

	config_table = kmalloc(table_length, GFP_KERNEL);
	if (!config_table) {
		dev_err(&ctrl_info->pci_dev->dev,
			"failed to allocate memory for PQI configuration table\n");
		return -ENOMEM;
	}

	/*
	 * Copy the config table contents from I/O memory space into the
	 * temporary buffer.
	 */
	table_iomem_addr = ctrl_info->iomem_base + ctrl_info->config_table_offset;
	memcpy_fromio(config_table, table_iomem_addr, table_length);

	firmware_feature_section_present = false;
	section_info.ctrl_info = ctrl_info;
	section_offset = get_unaligned_le32(&config_table->first_section_offset);

	while (section_offset) {
		section = (void *)config_table + section_offset;

		section_info.section = section;
		section_info.section_offset = section_offset;
		section_info.section_iomem_addr = table_iomem_addr + section_offset;

		switch (get_unaligned_le16(&section->section_id)) {
		case PQI_CONFIG_TABLE_SECTION_FIRMWARE_FEATURES:
			firmware_feature_section_present = true;
			feature_section_info = section_info;
			break;
		case PQI_CONFIG_TABLE_SECTION_HEARTBEAT:
			if (pqi_disable_heartbeat)
				dev_warn(&ctrl_info->pci_dev->dev,
				"heartbeat disabled by module parameter\n");
			else
				ctrl_info->heartbeat_counter =
					table_iomem_addr +
					section_offset +
					offsetof(struct pqi_config_table_heartbeat,
						heartbeat_counter);
			break;
		case PQI_CONFIG_TABLE_SECTION_SOFT_RESET:
			ctrl_info->soft_reset_status =
				table_iomem_addr +
				section_offset +
				offsetof(struct pqi_config_table_soft_reset,
					soft_reset_status);
			break;
		}

		section_offset = get_unaligned_le16(&section->next_section_offset);
	}

	/*
	 * We process the firmware feature section after all other sections
	 * have been processed so that the feature bit callbacks can take
	 * into account the settings configured by other sections.
	 */
	if (firmware_feature_section_present)
		pqi_process_firmware_features_section(&feature_section_info);

	kfree(config_table);

	return 0;
}

/* Switches the controller from PQI mode back into SIS mode. */

static int pqi_revert_to_sis_mode(struct pqi_ctrl_info *ctrl_info)
{
	int rc;

	pqi_change_irq_mode(ctrl_info, IRQ_MODE_NONE);
	rc = pqi_reset(ctrl_info);
	if (rc)
		return rc;
	rc = sis_reenable_sis_mode(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"re-enabling SIS mode failed with error %d\n", rc);
		return rc;
	}
	pqi_save_ctrl_mode(ctrl_info, SIS_MODE);

	return 0;
}

/*
 * If the controller isn't already in SIS mode, this function forces it into
 * SIS mode.
 */

static int pqi_force_sis_mode(struct pqi_ctrl_info *ctrl_info)
{
	if (!sis_is_firmware_running(ctrl_info))
		return -ENXIO;

	if (pqi_get_ctrl_mode(ctrl_info) == SIS_MODE)
		return 0;

	if (sis_is_kernel_up(ctrl_info)) {
		pqi_save_ctrl_mode(ctrl_info, SIS_MODE);
		return 0;
	}

	return pqi_revert_to_sis_mode(ctrl_info);
}

static void pqi_perform_lockup_action(void)
{
	switch (pqi_lockup_action) {
	case PANIC:
		panic("FATAL: Smart Family Controller lockup detected");
		break;
	case REBOOT:
		emergency_restart();
		break;
	case NONE:
	default:
		break;
	}
}

#define PQI_CTRL_LOG_TOTAL_SIZE	(4 * 1024 * 1024)
#define PQI_CTRL_LOG_MIN_SIZE	(PQI_CTRL_LOG_TOTAL_SIZE / PQI_HOST_MAX_SG_DESCRIPTORS)

static int pqi_ctrl_init(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	u32 product_id;

	if (reset_devices) {
		if (pqi_is_fw_triage_supported(ctrl_info)) {
			rc = sis_wait_for_fw_triage_completion(ctrl_info);
			if (rc)
				return rc;
		}
		if (sis_is_ctrl_logging_supported(ctrl_info)) {
			sis_notify_kdump(ctrl_info);
			rc = sis_wait_for_ctrl_logging_completion(ctrl_info);
			if (rc)
				return rc;
		}
		sis_soft_reset(ctrl_info);
		ssleep(PQI_POST_RESET_DELAY_SECS);
	} else {
		rc = pqi_force_sis_mode(ctrl_info);
		if (rc)
			return rc;
	}

	/*
	 * Wait until the controller is ready to start accepting SIS
	 * commands.
	 */
	rc = sis_wait_for_ctrl_ready(ctrl_info);
	if (rc) {
		if (reset_devices) {
			dev_err(&ctrl_info->pci_dev->dev,
				"kdump init failed with error %d\n", rc);
			pqi_lockup_action = REBOOT;
			pqi_perform_lockup_action();
		}
		return rc;
	}

	/*
	 * Get the controller properties.  This allows us to determine
	 * whether or not it supports PQI mode.
	 */
	rc = sis_get_ctrl_properties(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error obtaining controller properties\n");
		return rc;
	}

	rc = sis_get_pqi_capabilities(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error obtaining controller capabilities\n");
		return rc;
	}

	product_id = sis_get_product_id(ctrl_info);
	ctrl_info->product_id = (u8)product_id;
	ctrl_info->product_revision = (u8)(product_id >> 8);

	if (reset_devices) {
		if (ctrl_info->max_outstanding_requests >
			PQI_MAX_OUTSTANDING_REQUESTS_KDUMP)
				ctrl_info->max_outstanding_requests =
					PQI_MAX_OUTSTANDING_REQUESTS_KDUMP;
	} else {
		if (ctrl_info->max_outstanding_requests >
			PQI_MAX_OUTSTANDING_REQUESTS)
				ctrl_info->max_outstanding_requests =
					PQI_MAX_OUTSTANDING_REQUESTS;
	}

	pqi_calculate_io_resources(ctrl_info);

	rc = pqi_alloc_error_buffer(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"failed to allocate PQI error buffer\n");
		return rc;
	}

	/*
	 * If the function we are about to call succeeds, the
	 * controller will transition from legacy SIS mode
	 * into PQI mode.
	 */
	rc = sis_init_base_struct_addr(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error initializing PQI mode\n");
		return rc;
	}

	/* Wait for the controller to complete the SIS -> PQI transition. */
	rc = pqi_wait_for_pqi_mode_ready(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"transition to PQI mode failed\n");
		return rc;
	}

	/* From here on, we are running in PQI mode. */
	ctrl_info->pqi_mode_enabled = true;
	pqi_save_ctrl_mode(ctrl_info, PQI_MODE);

	rc = pqi_alloc_admin_queues(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"failed to allocate admin queues\n");
		return rc;
	}

	rc = pqi_create_admin_queues(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error creating admin queues\n");
		return rc;
	}

	rc = pqi_report_device_capability(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"obtaining device capability failed\n");
		return rc;
	}

	rc = pqi_validate_device_capability(ctrl_info);
	if (rc)
		return rc;

	pqi_calculate_queue_resources(ctrl_info);

	rc = pqi_enable_msix_interrupts(ctrl_info);
	if (rc)
		return rc;

	if (ctrl_info->num_msix_vectors_enabled < ctrl_info->num_queue_groups) {
		ctrl_info->max_msix_vectors =
			ctrl_info->num_msix_vectors_enabled;
		pqi_calculate_queue_resources(ctrl_info);
	}

	rc = pqi_alloc_io_resources(ctrl_info);
	if (rc)
		return rc;

	rc = pqi_alloc_operational_queues(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"failed to allocate operational queues\n");
		return rc;
	}

	pqi_init_operational_queues(ctrl_info);

	rc = pqi_create_queues(ctrl_info);
	if (rc)
		return rc;

	rc = pqi_request_irqs(ctrl_info);
	if (rc)
		return rc;

	pqi_change_irq_mode(ctrl_info, IRQ_MODE_MSIX);

	ctrl_info->controller_online = true;

	rc = pqi_process_config_table(ctrl_info);
	if (rc)
		return rc;

	pqi_start_heartbeat_timer(ctrl_info);

	if (ctrl_info->enable_r5_writes || ctrl_info->enable_r6_writes) {
		rc = pqi_get_advanced_raid_bypass_config(ctrl_info);
		if (rc) { /* Supported features not returned correctly. */
			dev_err(&ctrl_info->pci_dev->dev,
				"error obtaining advanced RAID bypass configuration\n");
			return rc;
		}
		ctrl_info->ciss_report_log_flags |=
			CISS_REPORT_LOG_FLAG_DRIVE_TYPE_MIX;
	}

	rc = pqi_enable_events(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error enabling events\n");
		return rc;
	}

	/* Register with the SCSI subsystem. */
	rc = pqi_register_scsi(ctrl_info);
	if (rc)
		return rc;

	if (ctrl_info->ctrl_logging_supported && !reset_devices) {
		pqi_host_setup_buffer(ctrl_info, &ctrl_info->ctrl_log_memory, PQI_CTRL_LOG_TOTAL_SIZE, PQI_CTRL_LOG_MIN_SIZE);
		pqi_host_memory_update(ctrl_info, &ctrl_info->ctrl_log_memory, PQI_VENDOR_GENERAL_CTRL_LOG_MEMORY_UPDATE);
	}

	rc = pqi_get_ctrl_product_details(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error obtaining product details\n");
		return rc;
	}

	rc = pqi_get_ctrl_serial_number(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error obtaining ctrl serial number\n");
		return rc;
	}

	rc = pqi_set_diag_rescan(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error enabling multi-lun rescan\n");
		return rc;
	}

	rc = pqi_write_driver_version_to_host_wellness(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error updating host wellness\n");
		return rc;
	}

	pqi_schedule_update_time_worker(ctrl_info);

	pqi_scan_scsi_devices(ctrl_info);

	return 0;
}

static void pqi_reinit_queues(struct pqi_ctrl_info *ctrl_info)
{
	unsigned int i;
	struct pqi_admin_queues *admin_queues;
	struct pqi_event_queue *event_queue;

	admin_queues = &ctrl_info->admin_queues;
	admin_queues->iq_pi_copy = 0;
	admin_queues->oq_ci_copy = 0;
	writel(0, admin_queues->oq_pi);

	for (i = 0; i < ctrl_info->num_queue_groups; i++) {
		ctrl_info->queue_groups[i].iq_pi_copy[RAID_PATH] = 0;
		ctrl_info->queue_groups[i].iq_pi_copy[AIO_PATH] = 0;
		ctrl_info->queue_groups[i].oq_ci_copy = 0;

		writel(0, ctrl_info->queue_groups[i].iq_ci[RAID_PATH]);
		writel(0, ctrl_info->queue_groups[i].iq_ci[AIO_PATH]);
		writel(0, ctrl_info->queue_groups[i].oq_pi);
	}

	event_queue = &ctrl_info->event_queue;
	writel(0, event_queue->oq_pi);
	event_queue->oq_ci_copy = 0;
}

static int pqi_ctrl_init_resume(struct pqi_ctrl_info *ctrl_info)
{
	int rc;

	rc = pqi_force_sis_mode(ctrl_info);
	if (rc)
		return rc;

	/*
	 * Wait until the controller is ready to start accepting SIS
	 * commands.
	 */
	rc = sis_wait_for_ctrl_ready_resume(ctrl_info);
	if (rc)
		return rc;

	/*
	 * Get the controller properties.  This allows us to determine
	 * whether or not it supports PQI mode.
	 */
	rc = sis_get_ctrl_properties(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error obtaining controller properties\n");
		return rc;
	}

	rc = sis_get_pqi_capabilities(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error obtaining controller capabilities\n");
		return rc;
	}

	/*
	 * If the function we are about to call succeeds, the
	 * controller will transition from legacy SIS mode
	 * into PQI mode.
	 */
	rc = sis_init_base_struct_addr(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error initializing PQI mode\n");
		return rc;
	}

	/* Wait for the controller to complete the SIS -> PQI transition. */
	rc = pqi_wait_for_pqi_mode_ready(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"transition to PQI mode failed\n");
		return rc;
	}

	/* From here on, we are running in PQI mode. */
	ctrl_info->pqi_mode_enabled = true;
	pqi_save_ctrl_mode(ctrl_info, PQI_MODE);

	pqi_reinit_queues(ctrl_info);

	rc = pqi_create_admin_queues(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error creating admin queues\n");
		return rc;
	}

	rc = pqi_create_queues(ctrl_info);
	if (rc)
		return rc;

	pqi_change_irq_mode(ctrl_info, IRQ_MODE_MSIX);

	ctrl_info->controller_online = true;
	pqi_ctrl_unblock_requests(ctrl_info);

	pqi_ctrl_reset_config(ctrl_info);

	rc = pqi_process_config_table(ctrl_info);
	if (rc)
		return rc;

	pqi_start_heartbeat_timer(ctrl_info);

	if (ctrl_info->enable_r5_writes || ctrl_info->enable_r6_writes) {
		rc = pqi_get_advanced_raid_bypass_config(ctrl_info);
		if (rc) {
			dev_err(&ctrl_info->pci_dev->dev,
				"error obtaining advanced RAID bypass configuration\n");
			return rc;
		}
		ctrl_info->ciss_report_log_flags |=
			CISS_REPORT_LOG_FLAG_DRIVE_TYPE_MIX;
	}

	rc = pqi_enable_events(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error enabling events\n");
		return rc;
	}

	rc = pqi_get_ctrl_product_details(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error obtaining product details\n");
		return rc;
	}

	rc = pqi_set_diag_rescan(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error enabling multi-lun rescan\n");
		return rc;
	}

	rc = pqi_write_driver_version_to_host_wellness(ctrl_info);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"error updating host wellness\n");
		return rc;
	}

	if (pqi_ofa_in_progress(ctrl_info)) {
		pqi_ctrl_unblock_scan(ctrl_info);
		if (ctrl_info->ctrl_logging_supported) {
			if (!ctrl_info->ctrl_log_memory.host_memory)
				pqi_host_setup_buffer(ctrl_info,
					&ctrl_info->ctrl_log_memory,
					PQI_CTRL_LOG_TOTAL_SIZE,
					PQI_CTRL_LOG_MIN_SIZE);
			pqi_host_memory_update(ctrl_info,
				&ctrl_info->ctrl_log_memory, PQI_VENDOR_GENERAL_CTRL_LOG_MEMORY_UPDATE);
		} else {
			if (ctrl_info->ctrl_log_memory.host_memory)
				pqi_host_free_buffer(ctrl_info,
					&ctrl_info->ctrl_log_memory);
		}
	}

	pqi_scan_scsi_devices(ctrl_info);

	return 0;
}

static inline int pqi_set_pcie_completion_timeout(struct pci_dev *pci_dev, u16 timeout)
{
	int rc;

	rc = pcie_capability_clear_and_set_word(pci_dev, PCI_EXP_DEVCTL2,
		PCI_EXP_DEVCTL2_COMP_TIMEOUT, timeout);

	return pcibios_err_to_errno(rc);
}

static int pqi_pci_init(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	u64 mask;

	rc = pci_enable_device(ctrl_info->pci_dev);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"failed to enable PCI device\n");
		return rc;
	}

	if (sizeof(dma_addr_t) > 4)
		mask = DMA_BIT_MASK(64);
	else
		mask = DMA_BIT_MASK(32);

	rc = dma_set_mask_and_coherent(&ctrl_info->pci_dev->dev, mask);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev, "failed to set DMA mask\n");
		goto disable_device;
	}

	rc = pci_request_regions(ctrl_info->pci_dev, DRIVER_NAME_SHORT);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"failed to obtain PCI resources\n");
		goto disable_device;
	}

	ctrl_info->iomem_base = ioremap(pci_resource_start(
		ctrl_info->pci_dev, 0),
		pci_resource_len(ctrl_info->pci_dev, 0));
	if (!ctrl_info->iomem_base) {
		dev_err(&ctrl_info->pci_dev->dev,
			"failed to map memory for controller registers\n");
		rc = -ENOMEM;
		goto release_regions;
	}

#define PCI_EXP_COMP_TIMEOUT_65_TO_210_MS		0x6

	/* Increase the PCIe completion timeout. */
	rc = pqi_set_pcie_completion_timeout(ctrl_info->pci_dev,
		PCI_EXP_COMP_TIMEOUT_65_TO_210_MS);
	if (rc) {
		dev_err(&ctrl_info->pci_dev->dev,
			"failed to set PCIe completion timeout\n");
		goto release_regions;
	}

	/* Enable bus mastering. */
	pci_set_master(ctrl_info->pci_dev);

	ctrl_info->registers = ctrl_info->iomem_base;
	ctrl_info->pqi_registers = &ctrl_info->registers->pqi_registers;

	pci_set_drvdata(ctrl_info->pci_dev, ctrl_info);

	return 0;

release_regions:
	pci_release_regions(ctrl_info->pci_dev);
disable_device:
	pci_disable_device(ctrl_info->pci_dev);

	return rc;
}

static void pqi_cleanup_pci_init(struct pqi_ctrl_info *ctrl_info)
{
	iounmap(ctrl_info->iomem_base);
	pci_release_regions(ctrl_info->pci_dev);
	if (pci_is_enabled(ctrl_info->pci_dev))
		pci_disable_device(ctrl_info->pci_dev);
	pci_set_drvdata(ctrl_info->pci_dev, NULL);
}

static struct pqi_ctrl_info *pqi_alloc_ctrl_info(int numa_node)
{
	struct pqi_ctrl_info *ctrl_info;

	ctrl_info = kzalloc_node(sizeof(struct pqi_ctrl_info),
			GFP_KERNEL, numa_node);
	if (!ctrl_info)
		return NULL;

	mutex_init(&ctrl_info->scan_mutex);
	mutex_init(&ctrl_info->lun_reset_mutex);
	mutex_init(&ctrl_info->ofa_mutex);

	INIT_LIST_HEAD(&ctrl_info->scsi_device_list);
	spin_lock_init(&ctrl_info->scsi_device_list_lock);

	INIT_WORK(&ctrl_info->event_work, pqi_event_worker);
	atomic_set(&ctrl_info->num_interrupts, 0);

	INIT_DELAYED_WORK(&ctrl_info->rescan_work, pqi_rescan_worker);
	INIT_DELAYED_WORK(&ctrl_info->update_time_work, pqi_update_time_worker);

	timer_setup(&ctrl_info->heartbeat_timer, pqi_heartbeat_timer_handler, 0);
	INIT_WORK(&ctrl_info->ctrl_offline_work, pqi_ctrl_offline_worker);

	INIT_WORK(&ctrl_info->ofa_memory_alloc_work, pqi_ofa_memory_alloc_worker);
	INIT_WORK(&ctrl_info->ofa_quiesce_work, pqi_ofa_quiesce_worker);

	sema_init(&ctrl_info->sync_request_sem,
		PQI_RESERVED_IO_SLOTS_SYNCHRONOUS_REQUESTS);
	init_waitqueue_head(&ctrl_info->block_requests_wait);

	ctrl_info->ctrl_id = atomic_inc_return(&pqi_controller_count) - 1;
	ctrl_info->irq_mode = IRQ_MODE_NONE;
	ctrl_info->max_msix_vectors = PQI_MAX_MSIX_VECTORS;

	ctrl_info->ciss_report_log_flags = CISS_REPORT_LOG_FLAG_UNIQUE_LUN_ID;
	ctrl_info->max_transfer_encrypted_sas_sata =
		PQI_DEFAULT_MAX_TRANSFER_ENCRYPTED_SAS_SATA;
	ctrl_info->max_transfer_encrypted_nvme =
		PQI_DEFAULT_MAX_TRANSFER_ENCRYPTED_NVME;
	ctrl_info->max_write_raid_5_6 = PQI_DEFAULT_MAX_WRITE_RAID_5_6;
	ctrl_info->max_write_raid_1_10_2drive = ~0;
	ctrl_info->max_write_raid_1_10_3drive = ~0;
	ctrl_info->disable_managed_interrupts = pqi_disable_managed_interrupts;

	return ctrl_info;
}

static inline void pqi_free_ctrl_info(struct pqi_ctrl_info *ctrl_info)
{
	kfree(ctrl_info);
}

static void pqi_free_interrupts(struct pqi_ctrl_info *ctrl_info)
{
	pqi_free_irqs(ctrl_info);
	pqi_disable_msix_interrupts(ctrl_info);
}

static void pqi_free_ctrl_resources(struct pqi_ctrl_info *ctrl_info)
{
	pqi_free_interrupts(ctrl_info);
	if (ctrl_info->queue_memory_base)
		dma_free_coherent(&ctrl_info->pci_dev->dev,
			ctrl_info->queue_memory_length,
			ctrl_info->queue_memory_base,
			ctrl_info->queue_memory_base_dma_handle);
	if (ctrl_info->admin_queue_memory_base)
		dma_free_coherent(&ctrl_info->pci_dev->dev,
			ctrl_info->admin_queue_memory_length,
			ctrl_info->admin_queue_memory_base,
			ctrl_info->admin_queue_memory_base_dma_handle);
	pqi_free_all_io_requests(ctrl_info);
	if (ctrl_info->error_buffer)
		dma_free_coherent(&ctrl_info->pci_dev->dev,
			ctrl_info->error_buffer_length,
			ctrl_info->error_buffer,
			ctrl_info->error_buffer_dma_handle);
	if (ctrl_info->iomem_base)
		pqi_cleanup_pci_init(ctrl_info);
	pqi_free_ctrl_info(ctrl_info);
}

static void pqi_remove_ctrl(struct pqi_ctrl_info *ctrl_info)
{
	ctrl_info->controller_online = false;
	pqi_stop_heartbeat_timer(ctrl_info);
	pqi_ctrl_block_requests(ctrl_info);
	pqi_cancel_rescan_worker(ctrl_info);
	pqi_cancel_update_time_worker(ctrl_info);
	if (ctrl_info->ctrl_removal_state == PQI_CTRL_SURPRISE_REMOVAL) {
		pqi_fail_all_outstanding_requests(ctrl_info);
		ctrl_info->pqi_mode_enabled = false;
	}
	pqi_host_free_buffer(ctrl_info, &ctrl_info->ctrl_log_memory);
	pqi_unregister_scsi(ctrl_info);
	if (ctrl_info->pqi_mode_enabled)
		pqi_revert_to_sis_mode(ctrl_info);
	pqi_free_ctrl_resources(ctrl_info);
}

static void pqi_ofa_ctrl_quiesce(struct pqi_ctrl_info *ctrl_info)
{
	pqi_ctrl_block_scan(ctrl_info);
	pqi_scsi_block_requests(ctrl_info);
	pqi_ctrl_block_device_reset(ctrl_info);
	pqi_ctrl_block_requests(ctrl_info);
	pqi_ctrl_wait_until_quiesced(ctrl_info);
	pqi_stop_heartbeat_timer(ctrl_info);
}

static void pqi_ofa_ctrl_unquiesce(struct pqi_ctrl_info *ctrl_info)
{
	pqi_start_heartbeat_timer(ctrl_info);
	pqi_ctrl_unblock_requests(ctrl_info);
	pqi_ctrl_unblock_device_reset(ctrl_info);
	pqi_scsi_unblock_requests(ctrl_info);
	pqi_ctrl_unblock_scan(ctrl_info);
}

static int pqi_ofa_ctrl_restart(struct pqi_ctrl_info *ctrl_info, unsigned int delay_secs)
{
	ssleep(delay_secs);

	return pqi_ctrl_init_resume(ctrl_info);
}

static int pqi_host_alloc_mem(struct pqi_ctrl_info *ctrl_info,
	struct pqi_host_memory_descriptor *host_memory_descriptor,
	u32 total_size, u32 chunk_size)
{
	int i;
	u32 sg_count;
	struct device *dev;
	struct pqi_host_memory *host_memory;
	struct pqi_sg_descriptor *mem_descriptor;
	dma_addr_t dma_handle;

	sg_count = DIV_ROUND_UP(total_size, chunk_size);
	if (sg_count == 0 || sg_count > PQI_HOST_MAX_SG_DESCRIPTORS)
		goto out;

	host_memory_descriptor->host_chunk_virt_address = kmalloc(sg_count * sizeof(void *), GFP_KERNEL);
	if (!host_memory_descriptor->host_chunk_virt_address)
		goto out;

	dev = &ctrl_info->pci_dev->dev;
	host_memory = host_memory_descriptor->host_memory;

	for (i = 0; i < sg_count; i++) {
		host_memory_descriptor->host_chunk_virt_address[i] = dma_alloc_coherent(dev, chunk_size, &dma_handle, GFP_KERNEL);
		if (!host_memory_descriptor->host_chunk_virt_address[i])
			goto out_free_chunks;
		mem_descriptor = &host_memory->sg_descriptor[i];
		put_unaligned_le64((u64)dma_handle, &mem_descriptor->address);
		put_unaligned_le32(chunk_size, &mem_descriptor->length);
	}

	put_unaligned_le32(CISS_SG_LAST, &mem_descriptor->flags);
	put_unaligned_le16(sg_count, &host_memory->num_memory_descriptors);
	put_unaligned_le32(sg_count * chunk_size, &host_memory->bytes_allocated);

	return 0;

out_free_chunks:
	while (--i >= 0) {
		mem_descriptor = &host_memory->sg_descriptor[i];
		dma_free_coherent(dev, chunk_size,
			host_memory_descriptor->host_chunk_virt_address[i],
			get_unaligned_le64(&mem_descriptor->address));
	}
	kfree(host_memory_descriptor->host_chunk_virt_address);
out:
	return -ENOMEM;
}

static int pqi_host_alloc_buffer(struct pqi_ctrl_info *ctrl_info,
	struct pqi_host_memory_descriptor *host_memory_descriptor,
	u32 total_required_size, u32 min_required_size)
{
	u32 chunk_size;
	u32 min_chunk_size;

	if (total_required_size == 0 || min_required_size == 0)
		return 0;

	total_required_size = PAGE_ALIGN(total_required_size);
	min_required_size = PAGE_ALIGN(min_required_size);
	min_chunk_size = DIV_ROUND_UP(total_required_size, PQI_HOST_MAX_SG_DESCRIPTORS);
	min_chunk_size = PAGE_ALIGN(min_chunk_size);

	while (total_required_size >= min_required_size) {
		for (chunk_size = total_required_size; chunk_size >= min_chunk_size;) {
			if (pqi_host_alloc_mem(ctrl_info,
				host_memory_descriptor, total_required_size,
				chunk_size) == 0)
				return 0;
			chunk_size /= 2;
			chunk_size = PAGE_ALIGN(chunk_size);
		}
		total_required_size /= 2;
		total_required_size = PAGE_ALIGN(total_required_size);
	}

	return -ENOMEM;
}

static void pqi_host_setup_buffer(struct pqi_ctrl_info *ctrl_info,
	struct pqi_host_memory_descriptor *host_memory_descriptor,
	u32 total_size, u32 min_size)
{
	struct device *dev;
	struct pqi_host_memory *host_memory;

	dev = &ctrl_info->pci_dev->dev;

	host_memory = dma_alloc_coherent(dev, sizeof(*host_memory),
		&host_memory_descriptor->host_memory_dma_handle, GFP_KERNEL);
	if (!host_memory)
		return;

	host_memory_descriptor->host_memory = host_memory;

	if (pqi_host_alloc_buffer(ctrl_info, host_memory_descriptor,
		total_size, min_size) < 0) {
		dev_err(dev, "failed to allocate firmware usable host buffer\n");
		dma_free_coherent(dev, sizeof(*host_memory), host_memory,
			host_memory_descriptor->host_memory_dma_handle);
		host_memory_descriptor->host_memory = NULL;
		return;
	}
}

static void pqi_host_free_buffer(struct pqi_ctrl_info *ctrl_info,
	struct pqi_host_memory_descriptor *host_memory_descriptor)
{
	unsigned int i;
	struct device *dev;
	struct pqi_host_memory *host_memory;
	struct pqi_sg_descriptor *mem_descriptor;
	unsigned int num_memory_descriptors;

	host_memory = host_memory_descriptor->host_memory;
	if (!host_memory)
		return;

	dev = &ctrl_info->pci_dev->dev;

	if (get_unaligned_le32(&host_memory->bytes_allocated) == 0)
		goto out;

	mem_descriptor = host_memory->sg_descriptor;
	num_memory_descriptors = get_unaligned_le16(&host_memory->num_memory_descriptors);

	for (i = 0; i < num_memory_descriptors; i++) {
		dma_free_coherent(dev,
			get_unaligned_le32(&mem_descriptor[i].length),
			host_memory_descriptor->host_chunk_virt_address[i],
			get_unaligned_le64(&mem_descriptor[i].address));
	}
	kfree(host_memory_descriptor->host_chunk_virt_address);

out:
	dma_free_coherent(dev, sizeof(*host_memory), host_memory,
		host_memory_descriptor->host_memory_dma_handle);
	host_memory_descriptor->host_memory = NULL;
}

static int pqi_host_memory_update(struct pqi_ctrl_info *ctrl_info,
	struct pqi_host_memory_descriptor *host_memory_descriptor,
	u16 function_code)
{
	u32 buffer_length;
	struct pqi_vendor_general_request request;
	struct pqi_host_memory *host_memory;

	memset(&request, 0, sizeof(request));

	request.header.iu_type = PQI_REQUEST_IU_VENDOR_GENERAL;
	put_unaligned_le16(sizeof(request) - PQI_REQUEST_HEADER_LENGTH, &request.header.iu_length);
	put_unaligned_le16(function_code, &request.function_code);

	host_memory = host_memory_descriptor->host_memory;

	if (host_memory) {
		buffer_length = offsetof(struct pqi_host_memory, sg_descriptor) + get_unaligned_le16(&host_memory->num_memory_descriptors) * sizeof(struct pqi_sg_descriptor);
		put_unaligned_le64((u64)host_memory_descriptor->host_memory_dma_handle, &request.data.host_memory_allocation.buffer_address);
		put_unaligned_le32(buffer_length, &request.data.host_memory_allocation.buffer_length);

		if (function_code == PQI_VENDOR_GENERAL_OFA_MEMORY_UPDATE) {
			put_unaligned_le16(PQI_OFA_VERSION, &host_memory->version);
			memcpy(&host_memory->signature, PQI_OFA_SIGNATURE, sizeof(host_memory->signature));
		} else if (function_code == PQI_VENDOR_GENERAL_CTRL_LOG_MEMORY_UPDATE) {
			put_unaligned_le16(PQI_CTRL_LOG_VERSION, &host_memory->version);
			memcpy(&host_memory->signature, PQI_CTRL_LOG_SIGNATURE, sizeof(host_memory->signature));
		}
	}

	return pqi_submit_raid_request_synchronous(ctrl_info, &request.header, 0, NULL);
}

static struct pqi_raid_error_info pqi_ctrl_offline_raid_error_info = {
	.data_out_result = PQI_DATA_IN_OUT_HARDWARE_ERROR,
	.status = SAM_STAT_CHECK_CONDITION,
};

static void pqi_fail_all_outstanding_requests(struct pqi_ctrl_info *ctrl_info)
{
	unsigned int i;
	struct pqi_io_request *io_request;
	struct scsi_cmnd *scmd;
	struct scsi_device *sdev;

	for (i = 0; i < ctrl_info->max_io_slots; i++) {
		io_request = &ctrl_info->io_request_pool[i];
		if (atomic_read(&io_request->refcount) == 0)
			continue;

		scmd = io_request->scmd;
		if (scmd) {
			sdev = scmd->device;
			if (!sdev || !scsi_device_online(sdev)) {
				pqi_free_io_request(io_request);
				continue;
			} else {
				set_host_byte(scmd, DID_NO_CONNECT);
			}
		} else {
			io_request->status = -ENXIO;
			io_request->error_info =
				&pqi_ctrl_offline_raid_error_info;
		}

		io_request->io_complete_callback(io_request,
			io_request->context);
	}
}

static void pqi_take_ctrl_offline_deferred(struct pqi_ctrl_info *ctrl_info)
{
	pqi_perform_lockup_action();
	pqi_stop_heartbeat_timer(ctrl_info);
	pqi_free_interrupts(ctrl_info);
	pqi_cancel_rescan_worker(ctrl_info);
	pqi_cancel_update_time_worker(ctrl_info);
	pqi_ctrl_wait_until_quiesced(ctrl_info);
	pqi_fail_all_outstanding_requests(ctrl_info);
	pqi_ctrl_unblock_requests(ctrl_info);
}

static void pqi_ctrl_offline_worker(struct work_struct *work)
{
	struct pqi_ctrl_info *ctrl_info;

	ctrl_info = container_of(work, struct pqi_ctrl_info, ctrl_offline_work);
	pqi_take_ctrl_offline_deferred(ctrl_info);
}

static char *pqi_ctrl_shutdown_reason_to_string(enum pqi_ctrl_shutdown_reason ctrl_shutdown_reason)
{
	char *string;

	switch (ctrl_shutdown_reason) {
	case PQI_IQ_NOT_DRAINED_TIMEOUT:
		string = "inbound queue not drained timeout";
		break;
	case PQI_LUN_RESET_TIMEOUT:
		string = "LUN reset timeout";
		break;
	case PQI_IO_PENDING_POST_LUN_RESET_TIMEOUT:
		string = "I/O pending timeout after LUN reset";
		break;
	case PQI_NO_HEARTBEAT:
		string = "no controller heartbeat detected";
		break;
	case PQI_FIRMWARE_KERNEL_NOT_UP:
		string = "firmware kernel not ready";
		break;
	case PQI_OFA_RESPONSE_TIMEOUT:
		string = "OFA response timeout";
		break;
	case PQI_INVALID_REQ_ID:
		string = "invalid request ID";
		break;
	case PQI_UNMATCHED_REQ_ID:
		string = "unmatched request ID";
		break;
	case PQI_IO_PI_OUT_OF_RANGE:
		string = "I/O queue producer index out of range";
		break;
	case PQI_EVENT_PI_OUT_OF_RANGE:
		string = "event queue producer index out of range";
		break;
	case PQI_UNEXPECTED_IU_TYPE:
		string = "unexpected IU type";
		break;
	default:
		string = "unknown reason";
		break;
	}

	return string;
}

static void pqi_take_ctrl_offline(struct pqi_ctrl_info *ctrl_info,
	enum pqi_ctrl_shutdown_reason ctrl_shutdown_reason)
{
	if (!ctrl_info->controller_online)
		return;

	ctrl_info->controller_online = false;
	ctrl_info->pqi_mode_enabled = false;
	pqi_ctrl_block_requests(ctrl_info);
	if (!pqi_disable_ctrl_shutdown)
		sis_shutdown_ctrl(ctrl_info, ctrl_shutdown_reason);
	pci_disable_device(ctrl_info->pci_dev);
	dev_err(&ctrl_info->pci_dev->dev,
		"controller offline: reason code 0x%x (%s)\n",
		ctrl_shutdown_reason, pqi_ctrl_shutdown_reason_to_string(ctrl_shutdown_reason));
	schedule_work(&ctrl_info->ctrl_offline_work);
}

static void pqi_print_ctrl_info(struct pci_dev *pci_dev,
	const struct pci_device_id *id)
{
	char *ctrl_description;

	if (id->driver_data)
		ctrl_description = (char *)id->driver_data;
	else
		ctrl_description = "Microchip Smart Family Controller";

	dev_info(&pci_dev->dev, "%s found\n", ctrl_description);
}

static int pqi_pci_probe(struct pci_dev *pci_dev,
	const struct pci_device_id *id)
{
	int rc;
	int node;
	struct pqi_ctrl_info *ctrl_info;

	pqi_print_ctrl_info(pci_dev, id);

	if (pqi_disable_device_id_wildcards &&
		id->subvendor == PCI_ANY_ID &&
		id->subdevice == PCI_ANY_ID) {
		dev_warn(&pci_dev->dev,
			"controller not probed because device ID wildcards are disabled\n");
		return -ENODEV;
	}

	if (id->subvendor == PCI_ANY_ID || id->subdevice == PCI_ANY_ID)
		dev_warn(&pci_dev->dev,
			"controller device ID matched using wildcards\n");

	node = dev_to_node(&pci_dev->dev);
	if (node == NUMA_NO_NODE) {
		node = cpu_to_node(0);
		if (node == NUMA_NO_NODE)
			node = 0;
		set_dev_node(&pci_dev->dev, node);
	}

	ctrl_info = pqi_alloc_ctrl_info(node);
	if (!ctrl_info) {
		dev_err(&pci_dev->dev,
			"failed to allocate controller info block\n");
		return -ENOMEM;
	}
	ctrl_info->numa_node = node;

	ctrl_info->pci_dev = pci_dev;

	rc = pqi_pci_init(ctrl_info);
	if (rc)
		goto error;

	rc = pqi_ctrl_init(ctrl_info);
	if (rc)
		goto error;

	return 0;

error:
	pqi_remove_ctrl(ctrl_info);

	return rc;
}

static void pqi_pci_remove(struct pci_dev *pci_dev)
{
	struct pqi_ctrl_info *ctrl_info;
	u16 vendor_id;
	int rc;

	ctrl_info = pci_get_drvdata(pci_dev);
	if (!ctrl_info)
		return;

	pci_read_config_word(ctrl_info->pci_dev, PCI_SUBSYSTEM_VENDOR_ID, &vendor_id);
	if (vendor_id == 0xffff)
		ctrl_info->ctrl_removal_state = PQI_CTRL_SURPRISE_REMOVAL;
	else
		ctrl_info->ctrl_removal_state = PQI_CTRL_GRACEFUL_REMOVAL;

	if (ctrl_info->ctrl_removal_state == PQI_CTRL_GRACEFUL_REMOVAL) {
		rc = pqi_flush_cache(ctrl_info, RESTART);
		if (rc)
			dev_err(&pci_dev->dev,
				"unable to flush controller cache during remove\n");
	}

	pqi_remove_ctrl(ctrl_info);
}

static void pqi_crash_if_pending_command(struct pqi_ctrl_info *ctrl_info)
{
	unsigned int i;
	struct pqi_io_request *io_request;
	struct scsi_cmnd *scmd;

	for (i = 0; i < ctrl_info->max_io_slots; i++) {
		io_request = &ctrl_info->io_request_pool[i];
		if (atomic_read(&io_request->refcount) == 0)
			continue;
		scmd = io_request->scmd;
		WARN_ON(scmd != NULL); /* IO command from SML */
		WARN_ON(scmd == NULL); /* Non-IO cmd or driver initiated*/
	}
}

static void pqi_shutdown(struct pci_dev *pci_dev)
{
	int rc;
	struct pqi_ctrl_info *ctrl_info;
	enum bmic_flush_cache_shutdown_event shutdown_event;

	ctrl_info = pci_get_drvdata(pci_dev);
	if (!ctrl_info) {
		dev_err(&pci_dev->dev,
			"cache could not be flushed\n");
		return;
	}

	pqi_wait_until_ofa_finished(ctrl_info);

	pqi_scsi_block_requests(ctrl_info);
	pqi_ctrl_block_device_reset(ctrl_info);
	pqi_ctrl_block_requests(ctrl_info);
	pqi_ctrl_wait_until_quiesced(ctrl_info);

	if (system_state == SYSTEM_RESTART)
		shutdown_event = RESTART;
	else
		shutdown_event = SHUTDOWN;

	/*
	 * Write all data in the controller's battery-backed cache to
	 * storage.
	 */
	rc = pqi_flush_cache(ctrl_info, shutdown_event);
	if (rc)
		dev_err(&pci_dev->dev,
			"unable to flush controller cache during shutdown\n");

	pqi_crash_if_pending_command(ctrl_info);
	pqi_reset(ctrl_info);
}

static void pqi_process_lockup_action_param(void)
{
	unsigned int i;

	if (!pqi_lockup_action_param)
		return;

	for (i = 0; i < ARRAY_SIZE(pqi_lockup_actions); i++) {
		if (strcmp(pqi_lockup_action_param,
			pqi_lockup_actions[i].name) == 0) {
			pqi_lockup_action = pqi_lockup_actions[i].action;
			return;
		}
	}

	pr_warn("%s: invalid lockup action setting \"%s\" - supported settings: none, reboot, panic\n",
		DRIVER_NAME_SHORT, pqi_lockup_action_param);
}

#define PQI_CTRL_READY_TIMEOUT_PARAM_MIN_SECS		30
#define PQI_CTRL_READY_TIMEOUT_PARAM_MAX_SECS		(30 * 60)

static void pqi_process_ctrl_ready_timeout_param(void)
{
	if (pqi_ctrl_ready_timeout_secs == 0)
		return;

	if (pqi_ctrl_ready_timeout_secs < PQI_CTRL_READY_TIMEOUT_PARAM_MIN_SECS) {
		pr_warn("%s: ctrl_ready_timeout parm of %u second(s) is less than minimum timeout of %d seconds - setting timeout to %d seconds\n",
			DRIVER_NAME_SHORT, pqi_ctrl_ready_timeout_secs, PQI_CTRL_READY_TIMEOUT_PARAM_MIN_SECS, PQI_CTRL_READY_TIMEOUT_PARAM_MIN_SECS);
		pqi_ctrl_ready_timeout_secs = PQI_CTRL_READY_TIMEOUT_PARAM_MIN_SECS;
	} else if (pqi_ctrl_ready_timeout_secs > PQI_CTRL_READY_TIMEOUT_PARAM_MAX_SECS) {
		pr_warn("%s: ctrl_ready_timeout parm of %u seconds is greater than maximum timeout of %d seconds - setting timeout to %d seconds\n",
			DRIVER_NAME_SHORT, pqi_ctrl_ready_timeout_secs, PQI_CTRL_READY_TIMEOUT_PARAM_MAX_SECS, PQI_CTRL_READY_TIMEOUT_PARAM_MAX_SECS);
		pqi_ctrl_ready_timeout_secs = PQI_CTRL_READY_TIMEOUT_PARAM_MAX_SECS;
	}

	sis_ctrl_ready_timeout_secs = pqi_ctrl_ready_timeout_secs;
}

static void pqi_process_module_params(void)
{
	pqi_process_lockup_action_param();
	pqi_process_ctrl_ready_timeout_param();
}

#if defined(CONFIG_PM)

static inline enum bmic_flush_cache_shutdown_event pqi_get_flush_cache_shutdown_event(struct pci_dev *pci_dev)
{
	if (pci_dev->subsystem_vendor == PCI_VENDOR_ID_ADAPTEC2 && pci_dev->subsystem_device == 0x1304)
		return RESTART;

	return SUSPEND;
}

static int pqi_suspend_or_freeze(struct device *dev, bool suspend)
{
	struct pci_dev *pci_dev;
	struct pqi_ctrl_info *ctrl_info;

	pci_dev = to_pci_dev(dev);
	ctrl_info = pci_get_drvdata(pci_dev);

	pqi_wait_until_ofa_finished(ctrl_info);

	pqi_ctrl_block_scan(ctrl_info);
	pqi_scsi_block_requests(ctrl_info);
	pqi_ctrl_block_device_reset(ctrl_info);
	pqi_ctrl_block_requests(ctrl_info);
	pqi_ctrl_wait_until_quiesced(ctrl_info);

	if (suspend) {
		enum bmic_flush_cache_shutdown_event shutdown_event;

		shutdown_event = pqi_get_flush_cache_shutdown_event(pci_dev);
		pqi_flush_cache(ctrl_info, shutdown_event);
	}

	pqi_stop_heartbeat_timer(ctrl_info);
	pqi_crash_if_pending_command(ctrl_info);
	pqi_free_irqs(ctrl_info);

	ctrl_info->controller_online = false;
	ctrl_info->pqi_mode_enabled = false;

	return 0;
}

static __maybe_unused int pqi_suspend(struct device *dev)
{
	return pqi_suspend_or_freeze(dev, true);
}

static int pqi_resume_or_restore(struct device *dev)
{
	int rc;
	struct pci_dev *pci_dev;
	struct pqi_ctrl_info *ctrl_info;

	pci_dev = to_pci_dev(dev);
	ctrl_info = pci_get_drvdata(pci_dev);

	rc = pqi_request_irqs(ctrl_info);
	if (rc)
		return rc;

	pqi_ctrl_unblock_device_reset(ctrl_info);
	pqi_ctrl_unblock_requests(ctrl_info);
	pqi_scsi_unblock_requests(ctrl_info);
	pqi_ctrl_unblock_scan(ctrl_info);

	ssleep(PQI_POST_RESET_DELAY_SECS);

	return pqi_ctrl_init_resume(ctrl_info);
}

static int pqi_freeze(struct device *dev)
{
	return pqi_suspend_or_freeze(dev, false);
}

static int pqi_thaw(struct device *dev)
{
	int rc;
	struct pci_dev *pci_dev;
	struct pqi_ctrl_info *ctrl_info;

	pci_dev = to_pci_dev(dev);
	ctrl_info = pci_get_drvdata(pci_dev);

	rc = pqi_request_irqs(ctrl_info);
	if (rc)
		return rc;

	ctrl_info->controller_online = true;
	ctrl_info->pqi_mode_enabled = true;

	pqi_ctrl_unblock_device_reset(ctrl_info);
	pqi_ctrl_unblock_requests(ctrl_info);
	pqi_scsi_unblock_requests(ctrl_info);
	pqi_ctrl_unblock_scan(ctrl_info);

	return 0;
}

static int pqi_poweroff(struct device *dev)
{
	struct pci_dev *pci_dev;
	struct pqi_ctrl_info *ctrl_info;
	enum bmic_flush_cache_shutdown_event shutdown_event;

	pci_dev = to_pci_dev(dev);
	ctrl_info = pci_get_drvdata(pci_dev);

	shutdown_event = pqi_get_flush_cache_shutdown_event(pci_dev);
	pqi_flush_cache(ctrl_info, shutdown_event);

	return 0;
}

static const struct dev_pm_ops pqi_pm_ops = {
	.suspend = pqi_suspend,
	.resume = pqi_resume_or_restore,
	.freeze = pqi_freeze,
	.thaw = pqi_thaw,
	.poweroff = pqi_poweroff,
	.restore = pqi_resume_or_restore,
};

#endif /* CONFIG_PM */

/* Define the PCI IDs for the controllers that we support. */
static const struct pci_device_id pqi_pci_id_table[] = {
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x105b, 0x1211)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x105b, 0x1321)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x152d, 0x8a22)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x152d, 0x8a23)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x152d, 0x8a24)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x152d, 0x8a36)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x152d, 0x8a37)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0x0462)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0x1104)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0x1105)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0x1106)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0x1107)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0x1108)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0x1109)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0x110b)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0x1110)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0x8460)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0x8461)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0x8462)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0xc460)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0xc461)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0xf460)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x193d, 0xf461)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0045)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0046)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0047)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0048)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x004a)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x004b)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x004c)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x004f)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0051)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0052)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0053)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0054)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x006b)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x006c)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x006d)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x006f)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0070)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0071)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0072)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0086)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0087)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0088)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1bd4, 0x0089)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x00a1)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1f3a, 0x0104)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x19e5, 0xd227)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x19e5, 0xd228)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x19e5, 0xd229)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x19e5, 0xd22a)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x19e5, 0xd22b)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x19e5, 0xd22c)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0110)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0608)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0659)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0800)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0801)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0802)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0803)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0804)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0805)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0806)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0807)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0808)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0809)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x080a)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0900)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0901)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0902)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0903)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0904)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0905)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0906)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0907)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x0908)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x090a)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1200)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1201)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1202)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1280)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1281)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1282)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1300)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1301)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1302)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1303)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1304)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1380)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1400)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1402)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1410)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1411)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1412)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1420)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1430)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1440)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1441)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1450)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1452)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1460)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1461)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1462)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1463)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1470)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1471)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1472)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1473)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1474)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1475)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1480)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1490)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x1491)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14a0)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14a1)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14a2)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14a4)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14a5)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14a6)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14b0)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14b1)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14c0)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14c1)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14c2)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14c3)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14c4)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14d0)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14e0)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADAPTEC2, 0x14f0)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_ADVANTECH, 0x8312)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_DELL, 0x1fe0)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x0600)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x0601)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x0602)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x0603)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x0609)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x0650)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x0651)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x0652)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x0653)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x0654)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x0655)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x0700)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x0701)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x1001)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x1002)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x1100)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_HP, 0x1101)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1590, 0x0294)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1590, 0x02db)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1590, 0x02dc)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1590, 0x032e)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1590, 0x036f)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1590, 0x0381)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1590, 0x0382)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1590, 0x0383)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1d8d, 0x0800)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1d8d, 0x0908)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1d8d, 0x0806)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1d8d, 0x0916)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_GIGABYTE, 0x1000)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1dfc, 0x3161)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1f0c, 0x3161)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x0804)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x0805)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x0806)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x5445)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x5446)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x5447)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x5449)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x544a)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x544b)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x544d)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x544e)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x544f)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x54da)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x54db)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x54dc)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x0b27)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x0b29)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cf2, 0x0b45)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cc4, 0x0101)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1cc4, 0x0201)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_LENOVO, 0x0220)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_LENOVO, 0x0221)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_LENOVO, 0x0520)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_LENOVO, 0x0522)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_LENOVO, 0x0620)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_LENOVO, 0x0621)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_LENOVO, 0x0622)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_VENDOR_ID_LENOVO, 0x0623)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1014, 0x0718)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1137, 0x02f8)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1137, 0x02f9)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1137, 0x02fa)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1137, 0x02fe)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1137, 0x02ff)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1137, 0x0300)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0045)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0046)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0047)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0048)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x004a)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x004b)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x004c)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x004f)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0051)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0052)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0053)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0054)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x006b)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x006c)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x006d)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x006f)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0070)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0071)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0072)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0086)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0087)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0088)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x0089)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1e93, 0x1000)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1e93, 0x1001)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1e93, 0x1002)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1e93, 0x1005)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1f51, 0x1001)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1f51, 0x1002)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1f51, 0x1003)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1f51, 0x1004)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1f51, 0x1005)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1f51, 0x1006)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1f51, 0x1007)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1f51, 0x1008)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1f51, 0x1009)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
				0x1f51, 0x100a)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1f51, 0x100e)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1f51, 0x100f)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1f51, 0x1010)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1f51, 0x1011)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1f51, 0x1043)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1f51, 0x1044)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1f51, 0x1045)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       0x1ff9, 0x00a3)
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x028f,
			       PCI_ANY_ID, PCI_ANY_ID)
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, pqi_pci_id_table);

static struct pci_driver pqi_pci_driver = {
	.name = DRIVER_NAME_SHORT,
	.id_table = pqi_pci_id_table,
	.probe = pqi_pci_probe,
	.remove = pqi_pci_remove,
	.shutdown = pqi_shutdown,
#if defined(CONFIG_PM)
	.driver = {
		.pm = &pqi_pm_ops
	},
#endif
};

static int __init pqi_init(void)
{
	int rc;

	pr_info(DRIVER_NAME "\n");
	pqi_verify_structures();
	sis_verify_structures();

	pqi_sas_transport_template = sas_attach_transport(&pqi_sas_transport_functions);
	if (!pqi_sas_transport_template)
		return -ENODEV;

	pqi_process_module_params();

	rc = pci_register_driver(&pqi_pci_driver);
	if (rc)
		sas_release_transport(pqi_sas_transport_template);

	return rc;
}

static void __exit pqi_cleanup(void)
{
	pci_unregister_driver(&pqi_pci_driver);
	sas_release_transport(pqi_sas_transport_template);
}

module_init(pqi_init);
module_exit(pqi_cleanup);

static void pqi_verify_structures(void)
{
	BUILD_BUG_ON(offsetof(struct pqi_ctrl_registers,
		sis_host_to_ctrl_doorbell) != 0x20);
	BUILD_BUG_ON(offsetof(struct pqi_ctrl_registers,
		sis_interrupt_mask) != 0x34);
	BUILD_BUG_ON(offsetof(struct pqi_ctrl_registers,
		sis_ctrl_to_host_doorbell) != 0x9c);
	BUILD_BUG_ON(offsetof(struct pqi_ctrl_registers,
		sis_ctrl_to_host_doorbell_clear) != 0xa0);
	BUILD_BUG_ON(offsetof(struct pqi_ctrl_registers,
		sis_driver_scratch) != 0xb0);
	BUILD_BUG_ON(offsetof(struct pqi_ctrl_registers,
		sis_product_identifier) != 0xb4);
	BUILD_BUG_ON(offsetof(struct pqi_ctrl_registers,
		sis_firmware_status) != 0xbc);
	BUILD_BUG_ON(offsetof(struct pqi_ctrl_registers,
		sis_ctrl_shutdown_reason_code) != 0xcc);
	BUILD_BUG_ON(offsetof(struct pqi_ctrl_registers,
		sis_mailbox) != 0x1000);
	BUILD_BUG_ON(offsetof(struct pqi_ctrl_registers,
		pqi_registers) != 0x4000);

	BUILD_BUG_ON(offsetof(struct pqi_iu_header,
		iu_type) != 0x0);
	BUILD_BUG_ON(offsetof(struct pqi_iu_header,
		iu_length) != 0x2);
	BUILD_BUG_ON(offsetof(struct pqi_iu_header,
		response_queue_id) != 0x4);
	BUILD_BUG_ON(offsetof(struct pqi_iu_header,
		driver_flags) != 0x6);
	BUILD_BUG_ON(sizeof(struct pqi_iu_header) != 0x8);

	BUILD_BUG_ON(offsetof(struct pqi_aio_error_info,
		status) != 0x0);
	BUILD_BUG_ON(offsetof(struct pqi_aio_error_info,
		service_response) != 0x1);
	BUILD_BUG_ON(offsetof(struct pqi_aio_error_info,
		data_present) != 0x2);
	BUILD_BUG_ON(offsetof(struct pqi_aio_error_info,
		reserved) != 0x3);
	BUILD_BUG_ON(offsetof(struct pqi_aio_error_info,
		residual_count) != 0x4);
	BUILD_BUG_ON(offsetof(struct pqi_aio_error_info,
		data_length) != 0x8);
	BUILD_BUG_ON(offsetof(struct pqi_aio_error_info,
		reserved1) != 0xa);
	BUILD_BUG_ON(offsetof(struct pqi_aio_error_info,
		data) != 0xc);
	BUILD_BUG_ON(sizeof(struct pqi_aio_error_info) != 0x10c);

	BUILD_BUG_ON(offsetof(struct pqi_raid_error_info,
		data_in_result) != 0x0);
	BUILD_BUG_ON(offsetof(struct pqi_raid_error_info,
		data_out_result) != 0x1);
	BUILD_BUG_ON(offsetof(struct pqi_raid_error_info,
		reserved) != 0x2);
	BUILD_BUG_ON(offsetof(struct pqi_raid_error_info,
		status) != 0x5);
	BUILD_BUG_ON(offsetof(struct pqi_raid_error_info,
		status_qualifier) != 0x6);
	BUILD_BUG_ON(offsetof(struct pqi_raid_error_info,
		sense_data_length) != 0x8);
	BUILD_BUG_ON(offsetof(struct pqi_raid_error_info,
		response_data_length) != 0xa);
	BUILD_BUG_ON(offsetof(struct pqi_raid_error_info,
		data_in_transferred) != 0xc);
	BUILD_BUG_ON(offsetof(struct pqi_raid_error_info,
		data_out_transferred) != 0x10);
	BUILD_BUG_ON(offsetof(struct pqi_raid_error_info,
		data) != 0x14);
	BUILD_BUG_ON(sizeof(struct pqi_raid_error_info) != 0x114);

	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		signature) != 0x0);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		function_and_status_code) != 0x8);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		max_admin_iq_elements) != 0x10);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		max_admin_oq_elements) != 0x11);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		admin_iq_element_length) != 0x12);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		admin_oq_element_length) != 0x13);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		max_reset_timeout) != 0x14);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		legacy_intx_status) != 0x18);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		legacy_intx_mask_set) != 0x1c);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		legacy_intx_mask_clear) != 0x20);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		device_status) != 0x40);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		admin_iq_pi_offset) != 0x48);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		admin_oq_ci_offset) != 0x50);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		admin_iq_element_array_addr) != 0x58);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		admin_oq_element_array_addr) != 0x60);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		admin_iq_ci_addr) != 0x68);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		admin_oq_pi_addr) != 0x70);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		admin_iq_num_elements) != 0x78);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		admin_oq_num_elements) != 0x79);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		admin_queue_int_msg_num) != 0x7a);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		device_error) != 0x80);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		error_details) != 0x88);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		device_reset) != 0x90);
	BUILD_BUG_ON(offsetof(struct pqi_device_registers,
		power_action) != 0x94);
	BUILD_BUG_ON(sizeof(struct pqi_device_registers) != 0x100);

	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		header.iu_type) != 0);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		header.iu_length) != 2);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		header.driver_flags) != 6);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		request_id) != 8);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		function_code) != 10);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.report_device_capability.buffer_length) != 44);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.report_device_capability.sg_descriptor) != 48);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_iq.queue_id) != 12);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_iq.element_array_addr) != 16);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_iq.ci_addr) != 24);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_iq.num_elements) != 32);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_iq.element_length) != 34);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_iq.queue_protocol) != 36);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_oq.queue_id) != 12);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_oq.element_array_addr) != 16);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_oq.pi_addr) != 24);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_oq.num_elements) != 32);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_oq.element_length) != 34);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_oq.queue_protocol) != 36);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_oq.int_msg_num) != 40);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_oq.coalescing_count) != 42);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_oq.min_coalescing_time) != 44);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.create_operational_oq.max_coalescing_time) != 48);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_request,
		data.delete_operational_queue.queue_id) != 12);
	BUILD_BUG_ON(sizeof(struct pqi_general_admin_request) != 64);
	BUILD_BUG_ON(sizeof_field(struct pqi_general_admin_request,
		data.create_operational_iq) != 64 - 11);
	BUILD_BUG_ON(sizeof_field(struct pqi_general_admin_request,
		data.create_operational_oq) != 64 - 11);
	BUILD_BUG_ON(sizeof_field(struct pqi_general_admin_request,
		data.delete_operational_queue) != 64 - 11);

	BUILD_BUG_ON(offsetof(struct pqi_general_admin_response,
		header.iu_type) != 0);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_response,
		header.iu_length) != 2);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_response,
		header.driver_flags) != 6);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_response,
		request_id) != 8);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_response,
		function_code) != 10);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_response,
		status) != 11);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_response,
		data.create_operational_iq.status_descriptor) != 12);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_response,
		data.create_operational_iq.iq_pi_offset) != 16);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_response,
		data.create_operational_oq.status_descriptor) != 12);
	BUILD_BUG_ON(offsetof(struct pqi_general_admin_response,
		data.create_operational_oq.oq_ci_offset) != 16);
	BUILD_BUG_ON(sizeof(struct pqi_general_admin_response) != 64);

	BUILD_BUG_ON(offsetof(struct pqi_raid_path_request,
		header.iu_type) != 0);
	BUILD_BUG_ON(offsetof(struct pqi_raid_path_request,
		header.iu_length) != 2);
	BUILD_BUG_ON(offsetof(struct pqi_raid_path_request,
		header.response_queue_id) != 4);
	BUILD_BUG_ON(offsetof(struct pqi_raid_path_request,
		header.driver_flags) != 6);
	BUILD_BUG_ON(offsetof(struct pqi_raid_path_request,
		request_id) != 8);
	BUILD_BUG_ON(offsetof(struct pqi_raid_path_request,
		nexus_id) != 10);
	BUILD_BUG_ON(offsetof(struct pqi_raid_path_request,
		buffer_length) != 12);
	BUILD_BUG_ON(offsetof(struct pqi_raid_path_request,
		lun_number) != 16);
	BUILD_BUG_ON(offsetof(struct pqi_raid_path_request,
		protocol_specific) != 24);
	BUILD_BUG_ON(offsetof(struct pqi_raid_path_request,
		error_index) != 27);
	BUILD_BUG_ON(offsetof(struct pqi_raid_path_request,
		cdb) != 32);
	BUILD_BUG_ON(offsetof(struct pqi_raid_path_request,
		timeout) != 60);
	BUILD_BUG_ON(offsetof(struct pqi_raid_path_request,
		sg_descriptors) != 64);
	BUILD_BUG_ON(sizeof(struct pqi_raid_path_request) !=
		PQI_OPERATIONAL_IQ_ELEMENT_LENGTH);

	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		header.iu_type) != 0);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		header.iu_length) != 2);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		header.response_queue_id) != 4);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		header.driver_flags) != 6);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		request_id) != 8);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		nexus_id) != 12);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		buffer_length) != 16);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		data_encryption_key_index) != 22);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		encrypt_tweak_lower) != 24);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		encrypt_tweak_upper) != 28);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		cdb) != 32);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		error_index) != 48);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		num_sg_descriptors) != 50);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		cdb_length) != 51);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		lun_number) != 52);
	BUILD_BUG_ON(offsetof(struct pqi_aio_path_request,
		sg_descriptors) != 64);
	BUILD_BUG_ON(sizeof(struct pqi_aio_path_request) !=
		PQI_OPERATIONAL_IQ_ELEMENT_LENGTH);

	BUILD_BUG_ON(offsetof(struct pqi_io_response,
		header.iu_type) != 0);
	BUILD_BUG_ON(offsetof(struct pqi_io_response,
		header.iu_length) != 2);
	BUILD_BUG_ON(offsetof(struct pqi_io_response,
		request_id) != 8);
	BUILD_BUG_ON(offsetof(struct pqi_io_response,
		error_index) != 10);

	BUILD_BUG_ON(offsetof(struct pqi_general_management_request,
		header.iu_type) != 0);
	BUILD_BUG_ON(offsetof(struct pqi_general_management_request,
		header.iu_length) != 2);
	BUILD_BUG_ON(offsetof(struct pqi_general_management_request,
		header.response_queue_id) != 4);
	BUILD_BUG_ON(offsetof(struct pqi_general_management_request,
		request_id) != 8);
	BUILD_BUG_ON(offsetof(struct pqi_general_management_request,
		data.report_event_configuration.buffer_length) != 12);
	BUILD_BUG_ON(offsetof(struct pqi_general_management_request,
		data.report_event_configuration.sg_descriptors) != 16);
	BUILD_BUG_ON(offsetof(struct pqi_general_management_request,
		data.set_event_configuration.global_event_oq_id) != 10);
	BUILD_BUG_ON(offsetof(struct pqi_general_management_request,
		data.set_event_configuration.buffer_length) != 12);
	BUILD_BUG_ON(offsetof(struct pqi_general_management_request,
		data.set_event_configuration.sg_descriptors) != 16);

	BUILD_BUG_ON(offsetof(struct pqi_iu_layer_descriptor,
		max_inbound_iu_length) != 6);
	BUILD_BUG_ON(offsetof(struct pqi_iu_layer_descriptor,
		max_outbound_iu_length) != 14);
	BUILD_BUG_ON(sizeof(struct pqi_iu_layer_descriptor) != 16);

	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		data_length) != 0);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		iq_arbitration_priority_support_bitmask) != 8);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		maximum_aw_a) != 9);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		maximum_aw_b) != 10);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		maximum_aw_c) != 11);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		max_inbound_queues) != 16);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		max_elements_per_iq) != 18);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		max_iq_element_length) != 24);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		min_iq_element_length) != 26);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		max_outbound_queues) != 30);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		max_elements_per_oq) != 32);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		intr_coalescing_time_granularity) != 34);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		max_oq_element_length) != 36);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		min_oq_element_length) != 38);
	BUILD_BUG_ON(offsetof(struct pqi_device_capability,
		iu_layer_descriptors) != 64);
	BUILD_BUG_ON(sizeof(struct pqi_device_capability) != 576);

	BUILD_BUG_ON(offsetof(struct pqi_event_descriptor,
		event_type) != 0);
	BUILD_BUG_ON(offsetof(struct pqi_event_descriptor,
		oq_id) != 2);
	BUILD_BUG_ON(sizeof(struct pqi_event_descriptor) != 4);

	BUILD_BUG_ON(offsetof(struct pqi_event_config,
		num_event_descriptors) != 2);
	BUILD_BUG_ON(offsetof(struct pqi_event_config,
		descriptors) != 4);

	BUILD_BUG_ON(PQI_NUM_SUPPORTED_EVENTS !=
		ARRAY_SIZE(pqi_supported_event_types));

	BUILD_BUG_ON(offsetof(struct pqi_event_response,
		header.iu_type) != 0);
	BUILD_BUG_ON(offsetof(struct pqi_event_response,
		header.iu_length) != 2);
	BUILD_BUG_ON(offsetof(struct pqi_event_response,
		event_type) != 8);
	BUILD_BUG_ON(offsetof(struct pqi_event_response,
		event_id) != 10);
	BUILD_BUG_ON(offsetof(struct pqi_event_response,
		additional_event_id) != 12);
	BUILD_BUG_ON(offsetof(struct pqi_event_response,
		data) != 16);
	BUILD_BUG_ON(sizeof(struct pqi_event_response) != 32);

	BUILD_BUG_ON(offsetof(struct pqi_event_acknowledge_request,
		header.iu_type) != 0);
	BUILD_BUG_ON(offsetof(struct pqi_event_acknowledge_request,
		header.iu_length) != 2);
	BUILD_BUG_ON(offsetof(struct pqi_event_acknowledge_request,
		event_type) != 8);
	BUILD_BUG_ON(offsetof(struct pqi_event_acknowledge_request,
		event_id) != 10);
	BUILD_BUG_ON(offsetof(struct pqi_event_acknowledge_request,
		additional_event_id) != 12);
	BUILD_BUG_ON(sizeof(struct pqi_event_acknowledge_request) != 16);

	BUILD_BUG_ON(offsetof(struct pqi_task_management_request,
		header.iu_type) != 0);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_request,
		header.iu_length) != 2);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_request,
		request_id) != 8);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_request,
		nexus_id) != 10);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_request,
		timeout) != 14);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_request,
		lun_number) != 16);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_request,
		protocol_specific) != 24);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_request,
		outbound_queue_id_to_manage) != 26);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_request,
		request_id_to_manage) != 28);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_request,
		task_management_function) != 30);
	BUILD_BUG_ON(sizeof(struct pqi_task_management_request) != 32);

	BUILD_BUG_ON(offsetof(struct pqi_task_management_response,
		header.iu_type) != 0);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_response,
		header.iu_length) != 2);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_response,
		request_id) != 8);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_response,
		nexus_id) != 10);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_response,
		additional_response_info) != 12);
	BUILD_BUG_ON(offsetof(struct pqi_task_management_response,
		response_code) != 15);
	BUILD_BUG_ON(sizeof(struct pqi_task_management_response) != 16);

	BUILD_BUG_ON(offsetof(struct bmic_identify_controller,
		configured_logical_drive_count) != 0);
	BUILD_BUG_ON(offsetof(struct bmic_identify_controller,
		configuration_signature) != 1);
	BUILD_BUG_ON(offsetof(struct bmic_identify_controller,
		firmware_version_short) != 5);
	BUILD_BUG_ON(offsetof(struct bmic_identify_controller,
		extended_logical_unit_count) != 154);
	BUILD_BUG_ON(offsetof(struct bmic_identify_controller,
		firmware_build_number) != 190);
	BUILD_BUG_ON(offsetof(struct bmic_identify_controller,
		vendor_id) != 200);
	BUILD_BUG_ON(offsetof(struct bmic_identify_controller,
		product_id) != 208);
	BUILD_BUG_ON(offsetof(struct bmic_identify_controller,
		extra_controller_flags) != 286);
	BUILD_BUG_ON(offsetof(struct bmic_identify_controller,
		controller_mode) != 292);
	BUILD_BUG_ON(offsetof(struct bmic_identify_controller,
		spare_part_number) != 293);
	BUILD_BUG_ON(offsetof(struct bmic_identify_controller,
		firmware_version_long) != 325);

	BUILD_BUG_ON(offsetof(struct bmic_identify_physical_device,
		phys_bay_in_box) != 115);
	BUILD_BUG_ON(offsetof(struct bmic_identify_physical_device,
		device_type) != 120);
	BUILD_BUG_ON(offsetof(struct bmic_identify_physical_device,
		redundant_path_present_map) != 1736);
	BUILD_BUG_ON(offsetof(struct bmic_identify_physical_device,
		active_path_number) != 1738);
	BUILD_BUG_ON(offsetof(struct bmic_identify_physical_device,
		alternate_paths_phys_connector) != 1739);
	BUILD_BUG_ON(offsetof(struct bmic_identify_physical_device,
		alternate_paths_phys_box_on_port) != 1755);
	BUILD_BUG_ON(offsetof(struct bmic_identify_physical_device,
		current_queue_depth_limit) != 1796);
	BUILD_BUG_ON(sizeof(struct bmic_identify_physical_device) != 2560);

	BUILD_BUG_ON(sizeof(struct bmic_sense_feature_buffer_header) != 4);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_buffer_header,
		page_code) != 0);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_buffer_header,
		subpage_code) != 1);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_buffer_header,
		buffer_length) != 2);

	BUILD_BUG_ON(sizeof(struct bmic_sense_feature_page_header) != 4);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_page_header,
		page_code) != 0);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_page_header,
		subpage_code) != 1);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_page_header,
		page_length) != 2);

	BUILD_BUG_ON(sizeof(struct bmic_sense_feature_io_page_aio_subpage)
		!= 18);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_io_page_aio_subpage,
		header) != 0);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_io_page_aio_subpage,
		firmware_read_support) != 4);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_io_page_aio_subpage,
		driver_read_support) != 5);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_io_page_aio_subpage,
		firmware_write_support) != 6);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_io_page_aio_subpage,
		driver_write_support) != 7);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_io_page_aio_subpage,
		max_transfer_encrypted_sas_sata) != 8);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_io_page_aio_subpage,
		max_transfer_encrypted_nvme) != 10);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_io_page_aio_subpage,
		max_write_raid_5_6) != 12);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_io_page_aio_subpage,
		max_write_raid_1_10_2drive) != 14);
	BUILD_BUG_ON(offsetof(struct bmic_sense_feature_io_page_aio_subpage,
		max_write_raid_1_10_3drive) != 16);

	BUILD_BUG_ON(PQI_ADMIN_IQ_NUM_ELEMENTS > 255);
	BUILD_BUG_ON(PQI_ADMIN_OQ_NUM_ELEMENTS > 255);
	BUILD_BUG_ON(PQI_ADMIN_IQ_ELEMENT_LENGTH %
		PQI_QUEUE_ELEMENT_LENGTH_ALIGNMENT != 0);
	BUILD_BUG_ON(PQI_ADMIN_OQ_ELEMENT_LENGTH %
		PQI_QUEUE_ELEMENT_LENGTH_ALIGNMENT != 0);
	BUILD_BUG_ON(PQI_OPERATIONAL_IQ_ELEMENT_LENGTH > 1048560);
	BUILD_BUG_ON(PQI_OPERATIONAL_IQ_ELEMENT_LENGTH %
		PQI_QUEUE_ELEMENT_LENGTH_ALIGNMENT != 0);
	BUILD_BUG_ON(PQI_OPERATIONAL_OQ_ELEMENT_LENGTH > 1048560);
	BUILD_BUG_ON(PQI_OPERATIONAL_OQ_ELEMENT_LENGTH %
		PQI_QUEUE_ELEMENT_LENGTH_ALIGNMENT != 0);

	BUILD_BUG_ON(PQI_RESERVED_IO_SLOTS >= PQI_MAX_OUTSTANDING_REQUESTS);
	BUILD_BUG_ON(PQI_RESERVED_IO_SLOTS >=
		PQI_MAX_OUTSTANDING_REQUESTS_KDUMP);
}
